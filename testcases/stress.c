/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/*
    brcmtimer-stress: the same battery on timer.device, the reference, then on brcm-timer.device, reported side by side. Called through
    proto/timer.h with TimerBase pointed at the device under test, timed with the system timer of the Pi for both.
      - the contract: units, commands, every unit and delay, IOF_QUICK, AbortIO(), a request sent again, order of the replies, far and
        not normalised times, calls from an interrupt, the registers kept by the calls, the time arithmetic on random values;
      - the clocks: frequency, system time against the counter, monotonic, equal readings;
      - the load: worker processes of several priorities and a long queue at once, with aborts: none early, lost, twice;
      - the side effects: signals, open count, free memory;
      - brcm-timer.device only: TR_SETSYSTIME many times a second against GetSysTime(), for readings that mix two bases;
      - the performance of the two devices.
    The verdict of a row: "ok", "same" or "spec" (a difference that the Autodoc documents); only brcm-timer.device can make it FAIL.
    The time of timer.device is left alone, unless SETSYS asks to set it once to its own value (it moves by some microseconds).
    Template: R/N,SETSYS/S (R: rounds of the load, default 20).
*/

#include <exec/errors.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <clib/alib_protos.h>

#include <common/compiler.h>
#include <devices/brcmtimer.h>

static const char version[] __attribute__((used)) = "$VER: brcmtimer-stress 0.1 (7.11.2026)";

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Device *TimerBase;

#define Print(fmt, ...)     Printf((CONST_STRPTR)(fmt), ##__VA_ARGS__)

/* exec.library/AbortIO() with the result of the device in D0: the headers of this toolchain declare it VOID */
#define AbortResult(io)     LP1(0x1e0, LONG, AbortResult, struct IORequest *, (io), a1, , SysBase)

/* The offsets of timer.device */
#define LVO_ADDTIME         (-42)
#define LVO_SUBTIME         (-48)
#define LVO_CMPTIME         (-54)
#define LVO_READECLOCK      (-60)
#define LVO_GETSYSTIME      (-66)

static inline ULONG Now(void)
{
    return __builtin_bswap32(*(volatile ULONG *)0xf2003004UL);
}

/* ---- the report ---- */

enum { K_PASS, K_SAME, K_SPEC, K_COST, K_LATE };

struct Row
{
    const char *name;
    ULONG kind;
    LONG v[2];
};

#define MAX_ROWS    96

static struct Row *rows;
static ULONG nrows;
static ULONG dev;       /* 0 timer.device, 1 brcm-timer.device */
static const char *const devnames[2] = { TIMERNAME, BRCMTIMERNAME };

static void Put(const char *name, ULONG kind, LONG value)
{
    if (nrows < MAX_ROWS)
    {
        rows[nrows].name = name;
        rows[nrows].kind = kind;
        rows[nrows].v[dev] = value;
        nrows++;
    }
}

static void Section(const char *name)
{
    Put(name, 100, 0);
}

/* ---- helpers ---- */

static ULONG eclockfreq;

static ULONG Random(ULONG *seed, ULONG n)
{
    *seed = *seed * 1103515245 + 12345;
    return ((*seed >> 8) & 0xffffff) % n;
}

/* a * b / c, 64 bits in the middle */
static inline ULONG Scale(ULONG a, ULONG b, ULONG c)
{
    ULONG hi, lo = a;

    __asm__ ("mulu.l %2,%0:%1" : "=d"(hi), "+d"(lo) : "d"(b) : "cc");
    __asm__ ("divu.l %2,%0:%1" : "+d"(hi), "+d"(lo) : "d"(c) : "cc");

    return lo;
}

/* The microseconds d as EClock ticks, rounded up: never a shorter wait */
static ULONG Ticks(ULONG d)
{
    return Scale(d, eclockfreq, 1000000) + 1;
}

/* tr_time for a wait of d microseconds from now, on any unit */
static void SetDelay(struct timerequest *tr, ULONG unit, ULONG d)
{
    tr->tr_node.io_Command = TR_ADDREQUEST;

    switch (unit)
    {
        case UNIT_MICROHZ:
        case UNIT_VBLANK:
            tr->tr_time.tv_secs = d / 1000000;
            tr->tr_time.tv_micro = d % 1000000;
            break;

        case UNIT_ECLOCK:
            tr->tr_time.tv_secs = 0;
            tr->tr_time.tv_micro = Ticks(d);
            break;

        case UNIT_WAITUNTIL:
        {
            struct timeval add;

            GetSysTime(&tr->tr_time);
            add.tv_secs = d / 1000000;
            add.tv_micro = d % 1000000;
            AddTime(&tr->tr_time, &add);
            break;
        }

        default:
        {
            struct EClockVal e;
            ULONG t = Ticks(d);

            ReadEClock(&e);
            e.ev_lo += t;
            e.ev_hi += e.ev_lo < t;
            tr->tr_time.tv_secs = e.ev_hi;
            tr->tr_time.tv_micro = e.ev_lo;
            break;
        }
    }
}

static BOOL OpenUnit(struct timerequest *tr, ULONG unit, struct MsgPort *port)
{
    tr->tr_node.io_Message.mn_ReplyPort = port;
    tr->tr_node.io_Message.mn_Length = sizeof(*tr);

    return OpenDevice((CONST_STRPTR)devnames[dev], unit, (struct IORequest *)tr, 0) == 0;
}

/* ---- the registers kept by a call: D2-D7, A2-A6 must be, A0 and A1 are documented as kept by timer.device only ---- */

ULONG CallCheck(APTR base, LONG lvo, APTR a, APTR b);

/* Returns a mask of the registers changed by the call: bits 0-5 D2-D7, 6-9 A2-A5, 10 A6, 11 A0, 12 A1 */
__asm__(
    "	.text\n"
    "	.even\n"
    "_CallCheck:\n"
    "	movem.l	d2-d7/a2-a6,-(sp)\n"
    "	movea.l	48(sp),a6\n"
    "	move.l	52(sp),d0\n"
    "	movea.l	56(sp),a0\n"
    "	movea.l	60(sp),a1\n"
    "	move.l	#0x22222222,d2\n"
    "	move.l	#0x33333333,d3\n"
    "	move.l	#0x44444444,d4\n"
    "	move.l	#0x55555555,d5\n"
    "	move.l	#0x66666666,d6\n"
    "	move.l	#0x77777777,d7\n"
    "	movea.l	#0x12222222,a2\n"
    "	movea.l	#0x13333333,a3\n"
    "	movea.l	#0x14444444,a4\n"
    "	movea.l	#0x15555555,a5\n"
    "	jsr	0(a6,d0.l)\n"
    "	moveq	#0,d0\n"
    "	cmp.l	#0x22222222,d2\n"
    "	beq.s	1f\n"
    "	bset	#0,d0\n"
    "1:	cmp.l	#0x33333333,d3\n"
    "	beq.s	1f\n"
    "	bset	#1,d0\n"
    "1:	cmp.l	#0x44444444,d4\n"
    "	beq.s	1f\n"
    "	bset	#2,d0\n"
    "1:	cmp.l	#0x55555555,d5\n"
    "	beq.s	1f\n"
    "	bset	#3,d0\n"
    "1:	cmp.l	#0x66666666,d6\n"
    "	beq.s	1f\n"
    "	bset	#4,d0\n"
    "1:	cmp.l	#0x77777777,d7\n"
    "	beq.s	1f\n"
    "	bset	#5,d0\n"
    "1:	cmpa.l	#0x12222222,a2\n"
    "	beq.s	1f\n"
    "	bset	#6,d0\n"
    "1:	cmpa.l	#0x13333333,a3\n"
    "	beq.s	1f\n"
    "	bset	#7,d0\n"
    "1:	cmpa.l	#0x14444444,a4\n"
    "	beq.s	1f\n"
    "	bset	#8,d0\n"
    "1:	cmpa.l	#0x15555555,a5\n"
    "	beq.s	1f\n"
    "	bset	#9,d0\n"
    "1:	cmpa.l	48(sp),a6\n"
    "	beq.s	1f\n"
    "	bset	#10,d0\n"
    "1:	cmpa.l	56(sp),a0\n"
    "	beq.s	1f\n"
    "	bset	#11,d0\n"
    "1:	cmpa.l	60(sp),a1\n"
    "	beq.s	1f\n"
    "	bset	#12,d0\n"
    "1:	movem.l	(sp)+,d2-d7/a2-a6\n"
    "	rts\n"
);

/* ---- the contract ---- */

static struct timerequest set[8];

static void TestOpen(struct MsgPort *port)
{
    struct timerequest tr;
    ULONG opened = 0;

    Section("Opening");

    for (ULONG u = 0; u < 5; u++)
    {
        if (OpenUnit(&tr, u, port))
        {
            if (tr.tr_node.io_Device == TimerBase && tr.tr_node.io_Unit != NULL)
                opened++;
            CloseDevice((struct IORequest *)&tr);
        }
    }
    Put("units 0 to 4 open", K_PASS, opened == 5);
    Put("closed: io_Device and io_Unit are -1", K_SAME, tr.tr_node.io_Device == (struct Device *)-1 && tr.tr_node.io_Unit == (struct Unit *)-1);
    Put("unit 5: the error of OpenDevice()", K_SAME, OpenUnit(&tr, 5, port) ? 0 : tr.tr_node.io_Error);
}

static void TestCommands(struct timerequest *base)
{
    static const UWORD invalid[] = { CMD_INVALID, CMD_RESET, CMD_READ, CMD_WRITE, CMD_UPDATE, CMD_CLEAR, CMD_STOP, CMD_START, CMD_FLUSH, 12, 100 };
    struct timerequest tr = *base;
    struct timeval now;
    ULONG nocmd = 0;
    LONG error;

    Section("Commands");

    for (ULONG i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    {
        tr.tr_node.io_Command = invalid[i];
        if (DoIO((struct IORequest *)&tr) == IOERR_NOCMD)
            nocmd++;
    }
    Put("11 other commands answer IOERR_NOCMD", K_SAME, nocmd);

    tr.tr_node.io_Command = TR_GETSYSTIME;
    GetSysTime(&now);
    error = DoIO((struct IORequest *)&tr);
    SubTime(&tr.tr_time, &now);
    Put("TR_GETSYSTIME with DoIO(): the time, no error", K_PASS, error == 0 && tr.tr_time.tv_secs == 0 && tr.tr_time.tv_micro < 100000);

    tr.tr_node.io_Command = TR_GETSYSTIME;
    tr.tr_node.io_Flags = IOF_QUICK;
    BeginIO((struct IORequest *)&tr);
    Put("TR_GETSYSTIME IOF_QUICK: done at once, no reply", K_PASS, (tr.tr_node.io_Flags & IOF_QUICK) != 0 && GetMsg(tr.tr_node.io_Message.mn_ReplyPort) == NULL);

    tr.tr_node.io_Command = TR_GETSYSTIME;
    SendIO((struct IORequest *)&tr);
    WaitIO((struct IORequest *)&tr);
    Put("TR_GETSYSTIME with SendIO(): replied", K_PASS, tr.tr_node.io_Error == 0 && GetMsg(tr.tr_node.io_Message.mn_ReplyPort) == NULL);
}

static void TestUnits(struct timerequest *opened)
{
    static const ULONG delays[] = { 1, 100, 1000, 20000, 100000 };
    static const char *const names[5] = { "UNIT_MICROHZ: 1 us to 100 ms, not early, cleared", "UNIT_VBLANK: 1 us to 100 ms, not early, cleared",
        "UNIT_ECLOCK: 1 us to 100 ms, not early, cleared", "UNIT_WAITUNTIL: 1 us to 100 ms, not early, cleared",
        "UNIT_WAITECLOCK: 1 us to 100 ms, not early, cleared" };

    Section("Every unit, every delay");

    for (ULONG u = 0; u < 5; u++)
    {
        struct timerequest tr = opened[u];
        ULONG good = 0;

        for (ULONG i = 0; i < sizeof(delays) / sizeof(delays[0]); i++)
        {
            ULONG t0 = Now(), took;
            LONG error;

            SetDelay(&tr, u, delays[i]);
            error = DoIO((struct IORequest *)&tr);
            took = Now() - t0;

            if (error == 0 && took + 2 >= delays[i] && tr.tr_time.tv_secs == 0 && tr.tr_time.tv_micro == 0)
                good++;
        }
        Put(names[u], K_PASS, good == sizeof(delays) / sizeof(delays[0]));
    }

    /* a time already passed, on the absolute units; no time at all on the relative ones */
    {
        ULONG good = 0;

        for (ULONG u = 0; u < 5; u++)
        {
            struct timerequest tr = opened[u];

            tr.tr_node.io_Command = TR_ADDREQUEST;
            tr.tr_time.tv_secs = 0;
            tr.tr_time.tv_micro = 0;
            if (DoIO((struct IORequest *)&tr) == 0)
                good++;
        }
        Put("zero or a time passed: replied, all units", K_PASS, good == 5);
    }
}

static void TestRequests(struct timerequest *opened, struct MsgPort *port)
{
    struct timerequest w = opened[UNIT_MICROHZ];
    LONG first, second;
    ULONG t0, took;

    Section("IOF_QUICK, AbortIO(), sending again");

    w.tr_node.io_Command = TR_ADDREQUEST;
    w.tr_time.tv_secs = 0;
    w.tr_time.tv_micro = 0;
    w.tr_node.io_Flags = IOF_QUICK;
    BeginIO((struct IORequest *)&w);
    WaitPort(port);
    Put("TR_ADDREQUEST IOF_QUICK: ignored, always replied", K_PASS, !(w.tr_node.io_Flags & IOF_QUICK) && GetMsg(port) == (struct Message *)&w);

    Put("AbortIO() of a request never sent: -1", K_PASS, AbortResult((struct IORequest *)&w) == -1);

    w.tr_time.tv_secs = 10;
    w.tr_time.tv_micro = 0;
    SendIO((struct IORequest *)&w);
    Put("waiting: CheckIO() says no", K_PASS, CheckIO((struct IORequest *)&w) == NULL);
    first = AbortResult((struct IORequest *)&w);
    WaitIO((struct IORequest *)&w);
    second = AbortResult((struct IORequest *)&w);
    Put("AbortIO(): 0 waiting, IOERR_ABORTED, tr_time cleared", K_PASS, first == 0 && w.tr_node.io_Error == IOERR_ABORTED &&
        w.tr_time.tv_secs == 0 && w.tr_time.tv_micro == 0);
    Put("AbortIO() again: -1", K_PASS, second == -1);

    w.tr_time.tv_secs = 10;
    w.tr_time.tv_micro = 0;
    SendIO((struct IORequest *)&w);
    SendIO((struct IORequest *)&w);
    WaitIO((struct IORequest *)&w);
    Put("sent again while waiting: taken back, one reply", K_PASS, w.tr_node.io_Error == IOERR_ABORTED && GetMsg(port) == NULL);

    /* Replies by deadline, whatever the order of the sends */
    {
        ULONG order = 0, last = 0;

        for (ULONG i = 0; i < 6; i++)
        {
            set[i] = opened[UNIT_MICROHZ];
            SetDelay(&set[i], UNIT_MICROHZ, (6 - i) * 3000);
            SendIO((struct IORequest *)&set[i]);
        }
        for (ULONG i = 0; i < 6; i++)
        {
            struct timerequest *r;

            WaitPort(port);
            r = (struct timerequest *)GetMsg(port);
            /* the sixth sent has the shortest delay */
            if ((ULONG)(&set[5] - r) == i)
                order++;
            last = i;
        }
        (void)last;
        Put("6 delays sent longest first: replied by deadline", K_PASS, order == 6);
    }

    /* Replies in the order of the sends for one same deadline */
    {
        struct EClockVal e;
        ULONG order = 0;

        ReadEClock(&e);
        e.ev_lo += Ticks(20000);
        e.ev_hi += e.ev_lo < Ticks(20000);
        for (ULONG i = 0; i < 4; i++)
        {
            set[i] = opened[UNIT_WAITECLOCK];
            set[i].tr_node.io_Command = TR_ADDREQUEST;
            set[i].tr_time.tv_secs = e.ev_hi;
            set[i].tr_time.tv_micro = e.ev_lo;
            SendIO((struct IORequest *)&set[i]);
        }
        for (ULONG i = 0; i < 4; i++)
        {
            struct Message *m;

            WaitPort(port);
            m = GetMsg(port);
            if (m == (struct Message *)&set[i])
                order++;
        }
        Put("one deadline: replied in the order of the sends", K_PASS, order == 4);
    }

    /* The request in the middle aborted, those around it keep their time */
    {
        ULONG good = 0;

        t0 = Now();
        for (ULONG i = 0; i < 3; i++)
        {
            set[i] = opened[UNIT_MICROHZ];
            SetDelay(&set[i], UNIT_MICROHZ, (i + 1) * 20000);
            SendIO((struct IORequest *)&set[i]);
        }
        if (AbortResult((struct IORequest *)&set[1]) == 0)
            good++;
        WaitIO((struct IORequest *)&set[1]);
        WaitIO((struct IORequest *)&set[0]);
        took = Now() - t0;
        if (took >= 20000 && set[0].tr_node.io_Error == 0)
            good++;
        WaitIO((struct IORequest *)&set[2]);
        took = Now() - t0;
        if (took >= 60000 && set[2].tr_node.io_Error == 0)
            good++;
        Put("abort in the middle: the others keep their time", K_PASS, good == 3);
    }

    /* Far away and not normalised */
    w.tr_node.io_Command = TR_ADDREQUEST;
    w.tr_time.tv_secs = 3600;
    w.tr_time.tv_micro = 0;
    SendIO((struct IORequest *)&w);
    Delay(1);
    first = CheckIO((struct IORequest *)&w) == NULL;
    second = AbortResult((struct IORequest *)&w);
    WaitIO((struct IORequest *)&w);
    Put("1 hour: still waiting, aborted", K_PASS, first && second == 0);

    w.tr_time.tv_secs = 0;
    w.tr_time.tv_micro = 1100000;
    t0 = Now();
    DoIO((struct IORequest *)&w);
    took = Now() - t0;
    Put("tv_micro 1100000: 1.1 s", K_PASS, took >= 1100000 && took < 1300000);
}

/* ---- from an interrupt ---- */

struct IntTest
{
    struct timerequest *send;
    struct timerequest *abort;
    struct timeval tv;
    struct EClockVal e;
    ULONG freq;
    LONG aborted;
    volatile BOOL ran;
};

static void SoftCode(REGARG(struct IntTest *t, "a1"))
{
    GetSysTime(&t->tv);
    t->freq = ReadEClock(&t->e);
    AddTime(&t->tv, &t->tv);
    t->aborted = AbortResult((struct IORequest *)t->abort);
    SendIO((struct IORequest *)t->send);
    t->ran = TRUE;
}

static void TestInterrupt(struct timerequest *opened)
{
    static struct IntTest t;
    static struct Interrupt soft;
    struct timerequest a = opened[UNIT_MICROHZ];
    struct timerequest s = opened[UNIT_MICROHZ];

    Section("From a software interrupt");

    t.send = &s;
    t.abort = &a;
    t.ran = FALSE;
    t.aborted = 1;
    t.freq = 0;

    a.tr_node.io_Command = TR_ADDREQUEST;
    a.tr_time.tv_secs = 10;
    a.tr_time.tv_micro = 0;
    SendIO((struct IORequest *)&a);

    SetDelay(&s, UNIT_MICROHZ, 1000);

    soft.is_Node.ln_Type = NT_INTERRUPT;
    soft.is_Node.ln_Pri = 0;
    soft.is_Node.ln_Name = (char *)"brcmtimer-stress";
    soft.is_Data = &t;
    soft.is_Code = (VOID (*)())SoftCode;
    Cause(&soft);

    while (!t.ran)
        Delay(1);

    WaitIO((struct IORequest *)&a);
    WaitIO((struct IORequest *)&s);

    Put("time functions, AbortIO(), SendIO(): all work", K_PASS, t.freq == eclockfreq && t.aborted == 0 && a.tr_node.io_Error == IOERR_ABORTED &&
        s.tr_node.io_Error == 0);
}

/* ---- the registers and the arithmetic ---- */

static void TestCalls(void)
{
    struct timeval a, b;
    struct EClockVal e;
    static const LONG lvos[5] = { LVO_ADDTIME, LVO_SUBTIME, LVO_CMPTIME, LVO_READECLOCK, LVO_GETSYSTIME };
    ULONG kept = 0, scratch = 0;
    ULONG h, seed;

    Section("The calls");

    for (ULONG i = 0; i < 5; i++)
    {
        ULONG changed;

        a.tv_secs = 100;
        a.tv_micro = 200;
        b.tv_secs = 3;
        b.tv_micro = 4;
        changed = CallCheck(TimerBase, lvos[i], i == 3 ? (APTR)&e : (APTR)&a, &b);
        kept |= changed & 0x7ff;
        scratch |= changed >> 11;
    }
    Put("D2-D7, A2-A6 kept: mask of those changed", K_SAME, kept);
    Put("A0, A1 changed (1 A0, 2 A1): scratch in the Autodoc", K_SPEC, scratch);

    /* Normalised values */
    h = 0;
    seed = 4711;
    for (ULONG i = 0; i < 20000; i++)
    {
        struct timeval d, s, r;

        d.tv_secs = Random(&seed, 1 << 20) * Random(&seed, 1 << 11);
        d.tv_micro = Random(&seed, 1000000);
        s.tv_secs = i & 7 ? Random(&seed, 1 << 20) : d.tv_secs;
        s.tv_micro = i & 15 ? Random(&seed, 1000000) : d.tv_micro;

        r = d;
        AddTime(&r, &s);
        h = h * 31 + r.tv_secs;
        h = h * 31 + r.tv_micro;
        r = d;
        SubTime(&r, &s);
        h = h * 31 + r.tv_secs;
        h = h * 31 + r.tv_micro;
        h = h * 31 + (ULONG)CmpTime(&d, &s);
    }
    Put("AddTime, SubTime, CmpTime, 20000 values: checksum", K_SAME, (LONG)h);

    /* Not normalised: the Autodoc asks for normalised values */
    h = 0;
    for (ULONG i = 0; i < 2000; i++)
    {
        struct timeval d, s, r;

        d.tv_secs = Random(&seed, 1000);
        d.tv_micro = Random(&seed, 1 << 23);
        s.tv_secs = Random(&seed, 1000);
        s.tv_micro = Random(&seed, 1 << 23);
        r = d;
        AddTime(&r, &s);
        h = h * 31 + r.tv_secs;
        h = h * 31 + r.tv_micro;
    }
    Put("AddTime not normalised: checksum (Autodoc: not supported)", K_SPEC, (LONG)h);
}

/* ---- the clocks ---- */

static void TestClocks(struct timerequest *opened)
{
    struct EClockVal e0, e1;
    struct timeval t0, t1;
    ULONG c0, c1, us, hz, equal = 0;
    BOOL mono = TRUE;
    struct timerequest tr = opened[UNIT_MICROHZ];

    Section("The clocks");

    Put("EClock frequency, Hz (1000000 here)", K_SPEC, eclockfreq);

    c0 = Now();
    ReadEClock(&e0);
    GetSysTime(&t0);
    SetDelay(&tr, UNIT_MICROHZ, 1000000);
    DoIO((struct IORequest *)&tr);
    c1 = Now();
    ReadEClock(&e1);
    GetSysTime(&t1);

    us = c1 - c0;
    hz = Scale(e1.ev_lo - e0.ev_lo, 1000000, us);
    Put("EClock against the Pi counter: within 0.1 %", K_PASS, Scale(hz, 1000, eclockfreq) >= 999 && Scale(hz, 1000, eclockfreq) <= 1001);
    SubTime(&t1, &t0);
    Put("system time against the Pi counter: within 1 ms in 1 s", K_PASS, t1.tv_secs * 1000000 + t1.tv_micro - us + 1000 <= 2000);

    t0.tv_secs = 0;
    t0.tv_micro = 0;
    for (ULONG i = 0; i < 100000; i++)
    {
        GetSysTime(&t1);
        if (t1.tv_secs < t0.tv_secs || (t1.tv_secs == t0.tv_secs && t1.tv_micro < t0.tv_micro))
            mono = FALSE;
        else if (t1.tv_secs == t0.tv_secs && t1.tv_micro == t0.tv_micro)
            equal++;
        t0 = t1;
    }
    Put("GetSysTime() never goes back, 100000 readings", K_PASS, mono);
    Put("equal readings out of 100000 (Autodoc: possible here)", K_SPEC, equal);

    mono = TRUE;
    ReadEClock(&e0);
    for (ULONG i = 0; i < 100000; i++)
    {
        ReadEClock(&e1);
        if (e1.ev_hi < e0.ev_hi || (e1.ev_hi == e0.ev_hi && e1.ev_lo < e0.ev_lo))
            mono = FALSE;
        e0 = e1;
    }
    Put("ReadEClock() never goes back, 100000 readings", K_PASS, mono);
}

/* ---- the load ---- */

struct Counts
{
    ULONG sent, aborted, early, twice, lost, errors;
};

struct Probe
{
    struct timerequest tr;
    ULONG sent;
    ULONG delay;
    BOOL aborted;
    BOOL back;
};

/* k probes on random units with delays up to dmax, one in seven aborted, then all the replies; a guard on timer.device for a lost one */
static void Round(struct Probe *probes, ULONG k, ULONG dmax, ULONG *seed, struct timerequest *opened, struct MsgPort *port, struct timerequest *guard,
    struct Counts *c)
{
    ULONG outstanding = k;

    for (ULONG i = 0; i < k; i++)
    {
        struct Probe *p = &probes[i];
        ULONG unit = Random(seed, 5);

        p->tr = opened[unit];
        p->delay = Random(seed, dmax);
        p->aborted = FALSE;
        p->back = FALSE;

        /* the clock first: the absolute units read their time after it, never a deadline earlier than sent + delay */
        p->sent = Now();
        SetDelay(&p->tr, unit, p->delay);
        SendIO((struct IORequest *)&p->tr);
        c->sent++;

        if (Random(seed, 7) == 0)
        {
            AbortIO((struct IORequest *)&p->tr);
            p->aborted = TRUE;
            c->aborted++;
        }
    }

    guard->tr_node.io_Command = TR_ADDREQUEST;
    guard->tr_time.tv_secs = dmax / 1000000 + 3;
    guard->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)guard);

    while (outstanding != 0 && CheckIO((struct IORequest *)guard) == NULL)
    {
        struct Message *m;

        while ((m = GetMsg(port)) != NULL)
        {
            struct Probe *p = (struct Probe *)m;
            ULONG took = Now() - p->sent;

            if (p->back)
                c->twice++;
            p->back = TRUE;
            outstanding--;

            if (p->tr.tr_node.io_Error == IOERR_ABORTED && p->aborted)
                continue;
            if (p->tr.tr_node.io_Error != 0)
                c->errors++;
            else if (took + 2 < p->delay)
                c->early++;
        }

        if (outstanding != 0)
            Wait((1UL << port->mp_SigBit) | (1UL << guard->tr_node.io_Message.mn_ReplyPort->mp_SigBit));
    }

    AbortIO((struct IORequest *)guard);
    WaitIO((struct IORequest *)guard);

    c->lost += outstanding;

    if (outstanding != 0)
    {
        for (ULONG i = 0; i < k; i++)
            if (!probes[i].back && AbortResult((struct IORequest *)&probes[i].tr) == 0)
                WaitIO((struct IORequest *)&probes[i].tr);

        while (GetMsg(port) != NULL)
            ;
    }
}

#define WORKERS     6
#define WORKER_K    16
#define MAIN_K      128

struct Worker
{
    struct Counts c;
    ULONG rounds;
    ULONG seed;
    BOOL finished;
    BOOL failed;
};

static struct Worker workers[WORKERS];
static ULONG nextworker;
static struct Task *parent;

/* A process of its own: its port, its requests opened on every unit, its time readings */
static void WorkerEntry(void)
{
    struct Worker *w;
    struct MsgPort *port, *gport;
    struct timerequest guard;
    struct timerequest opened[5];
    struct Probe probes[WORKER_K];
    ULONG units = 0;

    Forbid();
    w = &workers[nextworker++];
    Permit();

    port = CreateMsgPort();
    gport = CreateMsgPort();
    guard.tr_node.io_Message.mn_ReplyPort = gport;
    guard.tr_node.io_Message.mn_Length = sizeof(guard);

    if (port != NULL && gport != NULL && OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&guard, 0) == 0)
    {
        while (units < 5 && OpenUnit(&opened[units], units, port))
            units++;

        if (units == 5)
        {
            for (ULONG r = 0; r < w->rounds; r++)
                Round(probes, WORKER_K, 5000, &w->seed, opened, port, &guard, &w->c);
        }
        else
            w->failed = TRUE;

        while (units > 0)
            CloseDevice((struct IORequest *)&opened[--units]);
        CloseDevice((struct IORequest *)&guard);
    }
    else
        w->failed = TRUE;

    DeleteMsgPort(gport);
    DeleteMsgPort(port);

    Forbid();
    w->finished = TRUE;
    Signal(parent, SIGBREAKF_CTRL_F);
}

static void TestLoad(struct timerequest *opened, struct MsgPort *port, ULONG rounds)
{
    static const BYTE pris[WORKERS] = { -2, -1, 0, 1, 2, 5 };
    static struct Probe probes[MAIN_K];
    struct MsgPort *gport = CreateMsgPort();
    struct timerequest guard;
    struct Counts total = { 0 };
    struct Counts *c = &total;
    ULONG seed = 99, started = 0, failed = 0, t0, took;

    Section("The load: 6 processes + a queue of 128, with aborts");

    nextworker = 0;
    parent = FindTask(NULL);
    SetSignal(0, SIGBREAKF_CTRL_F);

    for (ULONG i = 0; i < WORKERS; i++)
    {
        struct TagItem tags[] = {
            { NP_Entry, (ULONG)WorkerEntry },
            { NP_Name, (ULONG)"brcmtimer-stress worker" },
            { NP_Priority, (ULONG)(LONG)pris[i] },
            { NP_StackSize, 32768 },
            { NP_Output, 0 },
            { NP_Input, 0 },
            { NP_CloseOutput, FALSE },
            { NP_CloseInput, FALSE },
            { TAG_DONE, 0 }
        };

        workers[i].rounds = rounds;
        workers[i].seed = 1000 + i * 77;
        workers[i].finished = FALSE;
        workers[i].failed = FALSE;
        workers[i].c.sent = workers[i].c.aborted = workers[i].c.early = 0;
        workers[i].c.twice = workers[i].c.lost = workers[i].c.errors = 0;

        if (CreateNewProc(tags) != NULL)
            started++;
    }

    guard.tr_node.io_Message.mn_ReplyPort = gport;
    guard.tr_node.io_Message.mn_Length = sizeof(guard);
    OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&guard, 0);

    t0 = Now();
    for (ULONG r = 0; r < rounds; r++)
        Round(probes, MAIN_K, 20000, &seed, opened, port, &guard, c);

    for (;;)
    {
        ULONG done = 0;

        for (ULONG i = 0; i < started; i++)
            if (workers[i].finished)
                done++;
        if (done == started)
            break;
        Wait(SIGBREAKF_CTRL_F);
    }
    took = Now() - t0;
    Delay(5);       /* the last worker, in Forbid() since its flag, is gone */

    CloseDevice((struct IORequest *)&guard);
    DeleteMsgPort(gport);

    for (ULONG i = 0; i < started; i++)
    {
        if (workers[i].failed)
            failed++;
        total.sent += workers[i].c.sent;
        total.aborted += workers[i].c.aborted;
        total.early += workers[i].c.early;
        total.twice += workers[i].c.twice;
        total.lost += workers[i].c.lost;
        total.errors += workers[i].c.errors;
    }

    Put("6 processes started and opened all the units", K_PASS, started == WORKERS && failed == 0);
    Put("requests sent", K_SAME, total.sent);
    Put("of which aborted right after", K_SAME, total.aborted);
    Put("none early", K_PASS, total.early == 0);
    Put("none replied twice", K_PASS, total.twice == 0);
    Put("none lost", K_PASS, total.lost == 0);
    Put("none with an error", K_PASS, total.errors == 0);
    Put("time of the load, ms", K_SPEC, took / 1000);
}

/* ---- brcm-timer.device only: readings against many TR_SETSYSTIME ---- */

static volatile BOOL flipstop;
static volatile ULONG flips;
static volatile BOOL flipgone;
static struct timeval flipref;
static struct EClockVal flipcount;

#define FLIP_OFFSET     1000
#define FLIP_US         2000
#define FLIP_WINDOW     500

/* The time since the reference, 0 or FLIP_OFFSET seconds ahead by turns, every FLIP_US, at a high priority */
static void FlipEntry(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest tr, wait;
    ULONG n = 0;

    if (port != NULL && OpenUnit(&tr, UNIT_MICROHZ, port))
    {
        wait = tr;

        while (!flipstop)
        {
            struct EClockVal e;
            struct timeval tv, add;
            ULONG d;

            ReadEClock(&e);
            d = e.ev_lo - flipcount.ev_lo;
            tv = flipref;
            add.tv_secs = d / 1000000;
            add.tv_micro = d % 1000000;
            AddTime(&tv, &add);
            tv.tv_secs += n & 1 ? FLIP_OFFSET : 0;

            tr.tr_node.io_Command = TR_SETSYSTIME;
            tr.tr_time = tv;
            DoIO((struct IORequest *)&tr);

            n++;
            flips = n;

            SetDelay(&wait, UNIT_MICROHZ, FLIP_US);
            DoIO((struct IORequest *)&wait);
        }

        CloseDevice((struct IORequest *)&tr);
    }

    DeleteMsgPort(port);

    Forbid();
    flipgone = TRUE;
    Signal(parent, SIGBREAKF_CTRL_F);
}

static void TestSetTime(void)
{
    struct TagItem tags[] = {
        { NP_Entry, (ULONG)FlipEntry },
        { NP_Name, (ULONG)"brcmtimer-stress flip" },
        { NP_Priority, 20 },
        { NP_StackSize, 16384 },
        { NP_Output, 0 },
        { NP_Input, 0 },
        { NP_CloseOutput, FALSE },
        { NP_CloseInput, FALSE },
        { TAG_DONE, 0 }
    };
    ULONG samples = 0, mixed = 0, worst = 0, skipped = 0, start;

    Section("TR_SETSYSTIME against GetSysTime() (brcm-timer.device only)");

    GetSysTime(&flipref);
    ReadEClock(&flipcount);
    flipstop = FALSE;
    flips = 0;
    flipgone = FALSE;
    parent = FindTask(NULL);
    SetSignal(0, SIGBREAKF_CTRL_F);

    if (CreateNewProc(tags) != NULL)
    {
        start = Now();

        while (Now() - start < 5000000)
        {
            struct EClockVal e0, e1;
            struct timeval t;
            ULONG real;
            LONG d0, d1;

            ReadEClock(&e0);
            GetSysTime(&t);
            ReadEClock(&e1);

            if (e1.ev_lo - e0.ev_lo > 100)
            {
                skipped++;
                continue;
            }

            /* The reading in microseconds since the reference: at one of the two bases, within the window */
            real = e0.ev_lo - flipcount.ev_lo;
            SubTime(&t, &flipref);
            d0 = (LONG)(t.tv_secs * 1000000 + t.tv_micro - real);
            d1 = (LONG)((t.tv_secs - FLIP_OFFSET) * 1000000 + t.tv_micro - real);
            if (d0 < 0)
                d0 = -d0;
            if (d1 < 0)
                d1 = -d1;
            if (d1 < d0)
                d0 = d1;

            samples++;
            if ((ULONG)d0 > FLIP_WINDOW)
                mixed++;
            if ((ULONG)d0 > worst)
                worst = d0;
        }

        flipstop = TRUE;
        while (!flipgone)
            Wait(SIGBREAKF_CTRL_F);
        Delay(5);
    }

    /* Back to the time of the system */
    {
        struct MsgPort *port = CreateMsgPort();
        struct timerequest tr;
        struct timerequest sys;

        if (port != NULL && OpenUnit(&tr, UNIT_MICROHZ, port))
        {
            sys.tr_node.io_Message.mn_ReplyPort = port;
            sys.tr_node.io_Message.mn_Length = sizeof(sys);
            if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&sys, 0) == 0)
            {
                sys.tr_node.io_Command = TR_GETSYSTIME;
                DoIO((struct IORequest *)&sys);
                tr.tr_node.io_Command = TR_SETSYSTIME;
                tr.tr_time = sys.tr_time;
                DoIO((struct IORequest *)&tr);
                CloseDevice((struct IORequest *)&sys);
            }
            CloseDevice((struct IORequest *)&tr);
        }
        DeleteMsgPort(port);
    }

    Put("time set, count in 5 s", K_SPEC, flips);
    Put("readings", K_SPEC, samples);
    Put("readings skipped (scheduled out)", K_SPEC, skipped);
    Put("worst distance to a base, us", K_SPEC, worst);
    Put("no reading mixes two bases", K_PASS, samples > 1000 && flips > 100 && mixed == 0);
}

/* ---- the performance ---- */

#define COST(name, n, statement) \
    do { \
        ULONG t = Now(); \
        for (ULONG i = 0; i < (n); i++) \
            statement; \
        t = Now() - t; \
        Put(name, K_COST, Scale(t, 100, (n))); \
    } while (0)

static void Lateness(struct timerequest *opened, ULONG unit, ULONG n, const char *avgname, const char *worstname)
{
    struct timerequest tr = opened[unit];
    ULONG sum = 0, worst = 0;

    for (ULONG i = 0; i < n; i++)
    {
        ULONG t0 = Now(), late;

        SetDelay(&tr, unit, 3000);
        DoIO((struct IORequest *)&tr);
        late = Now() - t0;
        late = late > 3000 ? late - 3000 : 0;
        sum += late;
        if (late > worst)
            worst = late;
    }

    Put(avgname, K_LATE, sum / n);
    Put(worstname, K_LATE, worst);
}

static void TestPerformance(struct timerequest *opened)
{
    struct timerequest tr = opened[UNIT_MICROHZ];
    struct EClockVal e;
    struct timeval a, b;

    Section("Performance: the cost of a call, us");

    a.tv_secs = 1000;
    a.tv_micro = 1;
    b.tv_secs = 0;
    b.tv_micro = 1;

    COST("ReadEClock()", 10000, ReadEClock(&e));
    COST("GetSysTime()", 10000, GetSysTime(&a));
    COST("AddTime()", 100000, AddTime(&a, &b));
    COST("SubTime()", 100000, SubTime(&a, &b));
    COST("CmpTime()", 100000, CmpTime(&a, &b));

    tr.tr_node.io_Command = TR_GETSYSTIME;
    COST("DoIO() TR_GETSYSTIME", 2000, DoIO((struct IORequest *)&tr));

    tr.tr_node.io_Command = TR_ADDREQUEST;
    COST("DoIO() TR_ADDREQUEST, no time", 2000, { tr.tr_time.tv_secs = 0; tr.tr_time.tv_micro = 0; DoIO((struct IORequest *)&tr); });
    COST("SendIO() + AbortIO() + WaitIO()", 2000, { tr.tr_time.tv_secs = 100; tr.tr_time.tv_micro = 0; SendIO((struct IORequest *)&tr); AbortIO((struct IORequest *)&tr); WaitIO((struct IORequest *)&tr); });

    /* 128 waiting, the longest first (each insert walks the queue): to send one, to take one back */
    {
        static struct timerequest deep[128];
        ULONG t;

        t = Now();
        for (ULONG i = 0; i < 128; i++)
        {
            deep[i] = opened[UNIT_MICROHZ];
            deep[i].tr_node.io_Command = TR_ADDREQUEST;
            deep[i].tr_time.tv_secs = 1000 - i;
            deep[i].tr_time.tv_micro = 0;
            SendIO((struct IORequest *)&deep[i]);
        }
        t = Now() - t;
        Put("SendIO() into a queue growing to 128", K_COST, Scale(t, 100, 128));

        t = Now();
        for (ULONG i = 0; i < 128; i++)
        {
            AbortIO((struct IORequest *)&deep[i]);
            WaitIO((struct IORequest *)&deep[i]);
        }
        t = Now() - t;
        Put("AbortIO() + WaitIO() in that queue", K_COST, Scale(t, 100, 128));
    }

    Section("Performance: lateness of a 3 ms wait, us");

    Lateness(opened, UNIT_MICROHZ, 200, "UNIT_MICROHZ average", "UNIT_MICROHZ worst");
    Lateness(opened, UNIT_ECLOCK, 200, "UNIT_ECLOCK average", "UNIT_ECLOCK worst");
    Lateness(opened, UNIT_WAITECLOCK, 200, "UNIT_WAITECLOCK average", "UNIT_WAITECLOCK worst");
    Lateness(opened, UNIT_WAITUNTIL, 100, "UNIT_WAITUNTIL average", "UNIT_WAITUNTIL worst");
    Lateness(opened, UNIT_VBLANK, 100, "UNIT_VBLANK average", "UNIT_VBLANK worst");
}

/* ---- the battery ---- */

static BOOL Battery(ULONG d, ULONG rounds, BOOL setsys)
{
    struct MsgPort *port;
    struct timerequest opened[5];
    struct EClockVal e;
    ULONG units = 0, sigs, count;
    LONG before;

    dev = d;
    nrows = 0;

    Print("--- %s\n", (LONG)devnames[d]);

    port = CreateMsgPort();
    if (port == NULL)
        return FALSE;

    while (units < 5 && OpenUnit(&opened[units], units, port))
        units++;
    if (units != 5)
    {
        Print("cannot open unit %ld of %s\n", units, (LONG)devnames[d]);
        while (units > 0)
            CloseDevice((struct IORequest *)&opened[--units]);
        DeleteMsgPort(port);
        return FALSE;
    }

    TimerBase = opened[0].tr_node.io_Device;
    eclockfreq = ReadEClock(&e);
    Print("%s\n", (LONG)TimerBase->dd_Library.lib_IdString);

    sigs = FindTask(NULL)->tc_SigAlloc;
    count = TimerBase->dd_Library.lib_OpenCnt;
    before = AvailMem(MEMF_ANY);

    TestOpen(port);
    TestCommands(&opened[0]);
    TestUnits(opened);
    TestRequests(opened, port);
    TestInterrupt(opened);
    TestCalls();
    TestClocks(opened);

    if (setsys)
    {
        struct timerequest tr = opened[0];
        struct timeval now;

        Section("TR_SETSYSTIME");
        GetSysTime(&now);
        tr.tr_node.io_Command = TR_SETSYSTIME;
        tr.tr_time = now;
        Put("set to its own value: no error", K_PASS, DoIO((struct IORequest *)&tr) == 0);
        Put("tr_time after it, not 0 (Autodoc of timer.device: junk)", K_SPEC, tr.tr_time.tv_secs != 0);
    }

    TestLoad(opened, port, rounds);

    Print("  (load done)\n");

    TestPerformance(opened);

    Section("Side effects");
    Put("signals of the task: as before", K_PASS, FindTask(NULL)->tc_SigAlloc == sigs);
    Put("open count of the device: as before", K_PASS, TimerBase->dd_Library.lib_OpenCnt == count);
    Put("free memory: difference, bytes (other tasks count)", K_SPEC, (LONG)AvailMem(MEMF_ANY) - before);
    Put("port empty", K_PASS, GetMsg(port) == NULL);

    while (units > 0)
        CloseDevice((struct IORequest *)&opened[--units]);

    DeleteMsgPort(port);

    return TRUE;
}

static void PrintValue(ULONG kind, LONG v)
{
    switch (kind)
    {
        case K_PASS:
            Print(" %12s", (LONG)(v ? "yes" : "NO"));
            break;

        case K_COST:
            Print(" %9ld.%02ld", v / 100, v % 100);
            break;

        default:
            Print(" %12ld", v);
            break;
    }
}

int main(void)
{
    LONG args[2] = { 0, 0 };
    struct RDArgs *rda;
    ULONG rounds = 20, failed = 0;
    BOOL setsys;
    ULONG count;

    SysBase = *(struct ExecBase **)4UL;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    if (DOSBase == NULL)
        return RETURN_FAIL;

    rda = ReadArgs((CONST_STRPTR)"R/N,SETSYS/S", args, NULL);
    if (rda != NULL)
    {
        if (args[0])
            rounds = *(ULONG *)args[0];
        FreeArgs(rda);
    }
    setsys = args[1] != 0;

    rows = AllocVec(sizeof(struct Row) * MAX_ROWS, MEMF_PUBLIC | MEMF_CLEAR);
    if (rows == NULL)
        return RETURN_FAIL;

    if (!Battery(0, rounds, setsys))
        return RETURN_FAIL;
    count = nrows;
    if (!Battery(1, rounds, setsys))
        return RETURN_FAIL;
    if (count != nrows)
        Print("The two batteries differ: %ld and %ld rows\n", count, nrows);

    /* brcm-timer.device only */
    {
        struct MsgPort *port = CreateMsgPort();
        struct timerequest tr;

        if (port != NULL && OpenUnit(&tr, UNIT_MICROHZ, port))
        {
            TimerBase = tr.tr_node.io_Device;
            TestSetTime();
            CloseDevice((struct IORequest *)&tr);
        }
        DeleteMsgPort(port);
    }

    Print("\n%-56s %12s %12s\n", (LONG)"", (LONG)"timer.device", (LONG)"brcm-timer");

    for (ULONG i = 0; i < nrows; i++)
    {
        struct Row *r = &rows[i];
        const char *verdict = "";

        if (r->kind == 100)
        {
            Print("\n%s\n", (LONG)r->name);
            continue;
        }

        Print("  %-54s", (LONG)r->name);

        if (i >= count)
        {
            Print(" %12s", (LONG)"-");
        }
        else
        {
            PrintValue(r->kind, r->v[0]);
        }
        PrintValue(r->kind, r->v[1]);

        switch (r->kind)
        {
            case K_PASS:
                if (!r->v[1])
                {
                    verdict = "FAIL";
                    failed++;
                }
                else
                    verdict = i < count && !r->v[0] ? "ok, timer.device not" : "ok";
                break;

            case K_SAME:
                if (r->v[0] != r->v[1])
                {
                    verdict = "FAIL";
                    failed++;
                }
                else
                    verdict = "same";
                break;

            case K_SPEC:
                verdict = "spec";
                break;

            default:
                break;
        }

        if ((r->kind == K_COST || r->kind == K_LATE) && r->v[1] > 0)
        {
            ULONG x10 = Scale((ULONG)r->v[0], 10, (ULONG)r->v[1]);

            Print("  x%ld.%ld\n", x10 / 10, x10 % 10);
        }
        else
            Print("  %s\n", (LONG)verdict);
    }

    Print("\nfailed: %ld\n", failed);

    FreeVec(rows);
    CloseLibrary((struct Library *)DOSBase);

    return failed == 0 ? RETURN_OK : RETURN_WARN;
}
