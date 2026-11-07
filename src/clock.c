/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <proto/exec.h>

#include <common/compiler.h>

#include "base.h"

/* The 64-bit counter, coherent: CHI, CLO, CHI again; when the high word moved meanwhile, CLO wrapped: read it again */
void BTIM_ReadCounter(
    ULONG *hi,
    ULONG *lo)
{
    ULONG h = st_rd(ST_CHI);
    ULONG l = st_rd(ST_CLO);
    ULONG h2 = st_rd(ST_CHI);

    if (h2 != h)
    {
        l = st_rd(ST_CLO);
        h = h2;
    }

    *hi = h;
    *lo = l;
}

/* The system time: the base plus the microseconds elapsed since. The elapsed count is split into seconds and microseconds by one
   64/32 divide of the 68020 and later (there is no libgcc for a 64-bit division); its quotient fits until 136 years of uptime. */
void BTIM_ReadSysTime(
    struct BrcmTimerBase *base,
    struct timeval *tv)
{
    ULONG sequence, hi, lo, secs, micro;

    do
    {
        sequence = *(volatile ULONG *)&base->bt_Sequence;
        BTIM_BARRIER();

        BTIM_ReadCounter(&hi, &lo);

        /* elapsed = counter - base counter, 64 bits */
        hi -= base->bt_BaseHi + (lo < base->bt_BaseLo);
        lo -= base->bt_BaseLo;

        secs = base->bt_BaseTime.tv_secs;
        micro = base->bt_BaseTime.tv_micro;

        BTIM_BARRIER();
    }
    while (sequence != *(volatile ULONG *)&base->bt_Sequence);

    /* hi:lo / 1000000: quotient into lo, remainder into hi */
    __asm__ ("divu.l %2,%0:%1" : "+d"(hi), "+d"(lo) : "d"(1000000UL) : "cc");

    micro += hi;
    secs += lo;
    if (micro >= 1000000)
    {
        micro -= 1000000;
        secs++;
    }

    tv->tv_secs = secs;
    tv->tv_micro = micro;
}

/* TR_SETSYSTIME: the new base, at the counter value of now. With the interrupts disabled: no reader can come in between, and one that
   was cut by this sees the sequence moved and reads again. The waiting UNIT_WAITUNTIL requests wait for a system time, as in timer.device:
   their deadlines move by (new counter - old counter) - (new time - old time). */
void BTIM_WriteSysTime(
    struct BrcmTimerBase *base,
    const struct timeval *tv)
{
    struct ExecBase *SysBase = base->bt_SysBase;
    ULONG hi, lo;
    ULONG secs = tv->tv_secs + tv->tv_micro / 1000000;
    ULONG micro = tv->tv_micro % 1000000;
    LONG dhi, ds, dm;
    ULONG dlo, ph, pl;

    Disable();

    base->bt_Sequence++;
    BTIM_ReadCounter(&hi, &lo);

    /* d = (counter now - base counter) - (new time - base time), in microseconds, signed 64 bits; the time difference is taken as a
       signed 32-bit count of seconds (68 years either way) times 10^6 with the 32x32->64 signed multiply of the 68020 */
    dlo = lo - base->bt_BaseLo;
    dhi = (LONG)(hi - base->bt_BaseHi - (lo < base->bt_BaseLo));
    ds = (LONG)(secs - base->bt_BaseTime.tv_secs);
    dm = (LONG)micro - (LONG)base->bt_BaseTime.tv_micro;
    pl = (ULONG)ds;
    __asm__ ("muls.l %2,%0:%1" : "=d"(ph), "+d"(pl) : "d"(1000000L) : "cc");
    ph += (dm < 0 ? -1 : 0) + (pl + (ULONG)dm < pl);   /* + dm, sign extended */
    pl += (ULONG)dm;
    dhi -= (LONG)ph + (dlo < pl);
    dlo -= pl;

    base->bt_BaseHi = hi;
    base->bt_BaseLo = lo;
    base->bt_BaseTime.tv_secs = secs;
    base->bt_BaseTime.tv_micro = micro;
    base->bt_Sequence++;

    BTIM_Retime(base, dhi, dlo);

    Enable();
}
