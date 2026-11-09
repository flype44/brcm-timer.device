/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _BASE_H
#define _BASE_H

#include <exec/types.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <exec/semaphores.h>
#include <exec/interrupts.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <devices/timer.h>

#include <devices/brcmtimer.h>

#include <common/compiler.h>

/* The version comes from project() in CMakeLists.txt, which defines BTIM_VERSION and BTIM_REVISION. These are only for a build that does
   not go through CMake: keep them equal to project(). */
#ifndef BTIM_VERSION
#define BTIM_VERSION        0
#endif
#ifndef BTIM_REVISION
#define BTIM_REVISION       10
#endif

#define BTIM_UNITS          5               /* UNIT_MICROHZ, UNIT_VBLANK, UNIT_ECLOCK, UNIT_WAITUNTIL, UNIT_WAITECLOCK */

/* The system timer of the Pi (BCM2711 datasheet, chapter 10), where Emu68 maps the peripherals (0x7e003000 for the SoC). Little
   endian: read with one 32-bit move, then swapped. */
#define ST_BASE             0xf2003000UL
#define ST_CS               0x00
#define ST_CLO              0x04
#define ST_CHI              0x08
#define ST_C1               0x10            /* compare channel 1: C0 and C2 belong to the firmware */
#define ST_M1               (1UL << 1)      /* CS: channel 1 matched; write one to clear it and the interrupt line */
#define ST_SPI_C1           97              /* GIC: VC peripheral IRQs from SPI 96, channel 1 is VC IRQ 1 */
#define ST_AIC_C1           33              /* arm-intc.library: GPU line 1 (bank 1, line 1) */

/* gic400.library 1.3: the first that finds the GIC through the interrupt-parent of the device tree */
#define GIC400_MIN_VERSION  1
#define GIC400_MIN_REVISION 3

/* arm-intc.library 0.3: the ARM interrupt controller of the BCM2837 (Pi 3, Zero 2), where there is no GIC; the first published */
#define ARM_INTC_MIN_VERSION  0
#define ARM_INTC_MIN_REVISION 3

/* The JIT of Emu68 merges two moves that follow each other into one access of the ARM: another instruction after each access keeps them
   apart. After a read, move.l Dn,Dn: it changes nothing and costs nothing measurable (a nop, a full barrier under Emu68, costs about 50 ns).
   After a write, a nop: the barrier also makes sure the write reached the timer before what follows, such as reading CLO again. */
static inline ULONG st_rd(ULONG reg)
{
    ULONG value = *(volatile ULONG *)(ST_BASE + reg);

    __asm__ volatile ("move.l %0,%0" : "+d"(value) : : "cc");

    return __builtin_bswap32(value);
}

static inline void st_wr(ULONG reg, ULONG value)
{
    *(volatile ULONG *)(ST_BASE + reg) = __builtin_bswap32(value);
    __asm__ volatile ("nop");
}

/* Keeps the compiler from moving a memory access across it: bt_BaseTime and the others are not volatile, GCC moves their reads out of
   the loop that checks bt_Sequence otherwise */
#define BTIM_BARRIER()      __asm__ volatile ("" : : : "memory")

/* A queued request keeps its deadline (counter value, 64 bits) in its tr_time, as timer.device converts tr_time to its own format: it
   is cleared when the request is replied */
#define DEADLINE_HI(tr)     ((tr)->tr_time.tv_secs)
#define DEADLINE_LO(tr)     ((tr)->tr_time.tv_micro)

/* A unit: what io_Unit of an opened request points to */
struct BTimUnit
{
    struct Unit             bu_Unit;
    ULONG                   bu_Number;      /* UNIT_* */
};

struct BrcmTimerBase
{
    struct Device           bt_Device;
    UWORD                   bt_Pad;
    struct ExecBase *       bt_SysBase;
    BPTR                    bt_SegList;

    /* The system time is bt_BaseTime at the counter value bt_BaseHi:bt_BaseLo. TR_SETSYSTIME changes the three with the interrupts
       disabled and bumps bt_Sequence before and after; a reader takes the three again when the sequence moved meanwhile. */
    struct timeval          bt_BaseTime;
    ULONG                   bt_BaseHi;
    ULONG                   bt_BaseLo;
    ULONG                   bt_Sequence;

    struct BTimUnit         bt_Units[BTIM_UNITS];

    /* The requests waiting, sorted by deadline, all units together; compare channel 1 is armed for the first one. Changed with the
       interrupts disabled, or in the server of the match. */
    struct List             bt_Queue;
    struct Library *        bt_Gic400Base;  /* the match through the GIC (Pi 4)... */
    struct Library *        bt_ArmIntcBase; /* ...or through arm-intc.library (Pi 3): one of the two */
    struct Interrupt        bt_Server;
};

/* Device */
struct BrcmTimerBase * BTIM_DevInit(REGARG(struct BrcmTimerBase *base, "d0"), REGARG(BPTR seglist, "a0"), REGARG(struct ExecBase *SysBase, "a6"));
VOID BTIM_DevOpen(REGARG(struct IORequest *io, "a1"), REGARG(ULONG unit, "d0"), REGARG(ULONG flags, "d1"), REGARG(struct BrcmTimerBase *base, "a6"));
BPTR BTIM_DevClose(REGARG(struct IORequest *io, "a1"), REGARG(struct BrcmTimerBase *base, "a6"));
BPTR BTIM_DevExpunge(REGARG(struct BrcmTimerBase *base, "a6"));
VOID BTIM_BeginIO(REGARG(struct IORequest *io, "a1"), REGARG(struct BrcmTimerBase *base, "a6"));
LONG BTIM_AbortIO(REGARG(struct IORequest *io, "a1"), REGARG(struct BrcmTimerBase *base, "a6"));

/* The clock (clock.c) */
void BTIM_ReadCounter(ULONG *hi, ULONG *lo);
void BTIM_ReadSysTime(struct BrcmTimerBase *base, struct timeval *tv);
void BTIM_WriteSysTime(struct BrcmTimerBase *base, const struct timeval *tv);

/* The queue (queue.c) */
BOOL BTIM_StartTimer(struct BrcmTimerBase *base);
void BTIM_StopTimer(struct BrcmTimerBase *base);
void BTIM_Insert(struct BrcmTimerBase *base, struct timerequest *tr);
void BTIM_Kick(struct BrcmTimerBase *base);
void BTIM_Retime(struct BrcmTimerBase *base, LONG dhi, ULONG dlo);
void BTIM_Server(REGARG(struct BrcmTimerBase *base, "a1"));

/* Calls, in the order of sfd/brcmtimer_lib.sfd */
VOID L_BTIM_AddTime(REGARG(struct timeval *dest, "a0"), REGARG(const struct timeval *src, "a1"), REGARG(struct BrcmTimerBase *base, "a6"));
VOID L_BTIM_SubTime(REGARG(struct timeval *dest, "a0"), REGARG(const struct timeval *src, "a1"), REGARG(struct BrcmTimerBase *base, "a6"));
LONG L_BTIM_CmpTime(REGARG(const struct timeval *dest, "a0"), REGARG(const struct timeval *src, "a1"), REGARG(struct BrcmTimerBase *base, "a6"));
ULONG L_BTIM_ReadEClock(REGARG(struct EClockVal *dest, "a0"), REGARG(struct BrcmTimerBase *base, "a6"));
VOID L_BTIM_GetSysTime(REGARG(struct timeval *dest, "a0"), REGARG(struct BrcmTimerBase *base, "a6"));

#endif /* _BASE_H */
