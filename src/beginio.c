/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/devices.h>
#include <exec/errors.h>
#include <proto/exec.h>

#include <common/compiler.h>

#include "base.h"

/* hi:lo += a * b, 64 bits; the 32x32->64 multiply of the 68020 and later */
static inline void MulAdd(ULONG *hi, ULONG *lo, ULONG a, ULONG b)
{
    ULONG ph, pl = a;

    __asm__ ("mulu.l %2,%0:%1" : "=d"(ph), "+d"(pl) : "d"(b) : "cc");

    *hi += ph + (*lo + pl < *lo);
    *lo += pl;
}

/* The counter value at the system time tv (UNIT_WAITUNTIL), 0:0 when tv is not after the base of the system time. With the interrupts
   disabled until the request is in the queue: a TR_SETSYSTIME comes before this, or finds the request there and moves its deadline. */
static void SysTimeToCounter(struct BrcmTimerBase *base, const struct timeval *tv, ULONG *hi, ULONG *lo)
{
    ULONG secs = base->bt_BaseTime.tv_secs;
    ULONG micro = base->bt_BaseTime.tv_micro;

    *hi = base->bt_BaseHi;
    *lo = base->bt_BaseLo;

    if (tv->tv_secs < secs || (tv->tv_secs == secs && tv->tv_micro <= micro))
    {
        *hi = 0;
        *lo = 0;
        return;
    }

    /* base counter + (tv - base time) in microseconds; tv_micro may be the larger or the smaller */
    MulAdd(hi, lo, tv->tv_secs - secs, 1000000);
    if (tv->tv_micro >= micro)
    {
        ULONG d = tv->tv_micro - micro;

        *hi += *lo + d < *lo;
        *lo += d;
    }
    else
    {
        ULONG d = micro - tv->tv_micro;

        *hi -= *lo < d;
        *lo -= d;
    }
}

/* The end of a request that is not queued: replied, or left to the caller when it asked for a quick I/O */
static void TermIO(struct BrcmTimerBase *base, struct IORequest *io)
{
    struct ExecBase *SysBase = base->bt_SysBase;

    if (io->io_Flags & IOF_QUICK)
    {
        io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    }
    else
    {
        ReplyMsg(&io->io_Message);
    }
}

/* TR_ADDREQUEST: the deadline of the request as a counter value, in its tr_time; due already, it ends at once, else it is queued and,
   first of the queue, armed. Always replied: IOF_QUICK is ignored, as in timer.device. UNIT_VBLANK shares the queue of UNIT_MICROHZ: it
   waits at least the time asked, without the 20 ms steps. */
static void AddRequest(struct BrcmTimerBase *base, struct timerequest *tr)
{
    struct ExecBase *SysBase = base->bt_SysBase;
    ULONG unit = ((struct BTimUnit *)tr->tr_node.io_Unit)->bu_Number;
    ULONG nowhi, nowlo, hi, lo;

    tr->tr_node.io_Flags &= ~IOF_QUICK;
    tr->tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;      /* until ReplyMsg(): WaitIO() and CheckIO() look at it */

    Disable();

    BTIM_ReadCounter(&nowhi, &nowlo);

    switch (unit)
    {
        case UNIT_MICROHZ:
        case UNIT_VBLANK:
            hi = nowhi;
            lo = nowlo + tr->tr_time.tv_micro;
            hi += lo < nowlo;
            MulAdd(&hi, &lo, tr->tr_time.tv_secs, 1000000);
            break;

        case UNIT_ECLOCK:
            /* an EClockVal of ticks in the timeval: ev_hi, ev_lo */
            lo = nowlo + tr->tr_time.tv_micro;
            hi = nowhi + tr->tr_time.tv_secs + (lo < nowlo);
            break;

        case UNIT_WAITUNTIL:
            SysTimeToCounter(base, &tr->tr_time, &hi, &lo);
            break;

        default: /* UNIT_WAITECLOCK: an absolute EClockVal */
            hi = tr->tr_time.tv_secs;
            lo = tr->tr_time.tv_micro;
            break;
    }

    if (hi < nowhi || (hi == nowhi && lo <= nowlo))
    {
        Enable();

        tr->tr_time.tv_secs = 0;
        tr->tr_time.tv_micro = 0;
        ReplyMsg(&tr->tr_node.io_Message);
        return;
    }

    DEADLINE_HI(tr) = hi;
    DEADLINE_LO(tr) = lo;

    BTIM_Insert(base, tr);
    if (base->bt_Queue.lh_Head == &tr->tr_node.io_Message.mn_Node)
    {
        BTIM_Kick(base);
    }
    Enable();
}

/* The commands of timer.device: TR_ADDREQUEST queued (or ended at once when due), TR_GETSYSTIME and TR_SETSYSTIME done at once */
VOID BTIM_BeginIO(
    REGARG(struct IORequest *io, "a1"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    struct timerequest *tr = (struct timerequest *)io;

    io->io_Error = 0;

    /* Sent again while it still waits: taken back and replied; queued twice, it would break the queue */
    if (io->io_Message.mn_Node.ln_Type == NT_MESSAGE && BTIM_AbortIO(io, base) == 0)
    {
        return;
    }

    switch (io->io_Command)
    {
        case TR_ADDREQUEST:
            AddRequest(base, tr);
            return;

        case TR_GETSYSTIME:
            BTIM_ReadSysTime(base, &tr->tr_time);
            break;

        case TR_SETSYSTIME:
            BTIM_WriteSysTime(base, &tr->tr_time);
            break;

        default:
            io->io_Error = IOERR_NOCMD;
            break;
    }

    TermIO(base, io);
}
