# brcm-timer.device

## Contents

- [Background](#background)
- [`BTIM_AddTime`](#btim_addtime) - add one time value to another
- [`BTIM_CmpTime`](#btim_cmptime) - compare two time values
- [`BTIM_GetSysTime`](#btim_getsystime) - read the system time
- [`BTIM_ReadEClock`](#btim_readeclock) - read the EClock counter and its frequency
- [`BTIM_SubTime`](#btim_subtime) - subtract one time value from another

## Background

### Purpose

brcm-timer.device is a timer for the drivers and programs of the AmigaOS + Emu68 + Raspberry Pi ecosystem that have a real need for it. It has the interface of timer.device (units, commands, request structure, functions) on the system timer of the Pi instead of the CIAs of the Amiga: reading the time does not touch the Amiga bus and takes a quarter of a microsecond instead of 20, and a task wakes about 40 microseconds after the time asked instead of about 150 (up to 20 ms for UNIT_WAITUNTIL and UNIT_VBLANK).

It is a disk device (DEVS:brcm-timer.device) next to the system timer.device, which it does not replace or change: that one stays for everything else. Its interface is still being validated and may change before version 1.0.

### Using it

Open BRCMTIMERNAME (<devices/brcmtimer.h>) with the units and the struct timerequest of timer.device. If OpenDevice() fails (not a Pi 4, or an older Emu68), open timer.device as before.

The functions are BTIM_AddTime(), BTIM_SubTime(), BTIM_CmpTime(), BTIM_ReadEClock() and BTIM_GetSysTime() with BrcmTimerBase = the io_Device of an opened request (<proto/brcmtimer.h>). They sit at the offsets of timer.device, so TimerBase may also point at this device.

What differs from timer.device:

lib_Version has its own numbers (0.x today): do not test it against the versions of timer.device, such as 36 for GetSysTime().

The EClock runs at 1000000 Hz: exec's ex_EClockFrequency still gives the one of the Amiga. Use the frequency that BTIM_ReadEClock() returns.

Two readings of the system time may be equal (see BTIM_GetSysTime()).

The functions follow the usual rule of the libraries, D0, D1, A0 and A1 are scratch: A0 and A1 are not kept as timer.device documents.

### Units

UNIT_MICROHZ (0): wait for a time interval in tr_time.

UNIT_VBLANK (1): the same as UNIT_MICROHZ here: at least the time asked, with no steps of a vertical blank and no use of the display.

UNIT_ECLOCK (2): wait for a number of EClock ticks (struct EClockVal in tr_time); one tick is one microsecond.

UNIT_WAITUNTIL (3): wait until the system time is the one in tr_time. TR_SETSYSTIME moves these waits, as timer.device does: a time set ahead ends those it passes, a time set back makes them wait longer.

UNIT_WAITECLOCK (4): wait until the EClock counter reaches the value in tr_time.

All units share one queue with the same precision.

### Commands

TR_ADDREQUEST: wait as the unit says, then reply; always replied, IOF_QUICK is ignored. A time already passed is replied at once. AbortIO() takes a waiting request back: it returns 0 and the request is replied with io_Error IOERR_ABORTED; for a request that is not waiting it returns -1. A request sent again while it waits is taken back in the same way and not started again. When the request is replied tr_time is cleared, as timer.device does.

TR_GETSYSTIME: tr_time receives the system time.

TR_SETSYSTIME: the system time of this device becomes tr_time (not the one of timer.device).

Other commands answer IOERR_NOCMD. TR_GETSYSTIME and TR_SETSYSTIME honour IOF_QUICK.

### The clock

The EClock is the 64-bit free running counter of the system timer: one tick a microsecond since the Pi started; BTIM_ReadEClock() returns 1000000 as the frequency. Use the frequency it returns: an EClock of 709379 Hz assumed by a program would make its times 41 % short.

The system time starts as the system time of timer.device when the device is loaded, then follows the counter. A later change of the time of timer.device (TR_SETSYSTIME, a network time) does not reach this device. The two devices run on different crystals: they drift apart by some microseconds a second (9 ppm measured).

### Requirements

A Raspberry Pi 4, 400 or CM4 (BCM2711) under Emu68 with gic400.library 1.3 or later in its ROM. The device uses compare channel 1 of the system timer and its interrupt (GIC SPI 97); channels 0 and 2 belong to the VideoCore firmware. If the interrupt is already in use (a server of gic400.library on it, or enabled and routed to a CPU by someone else), or on other boards, the device is not loaded and OpenDevice() fails.

The Amiga bus: BTIM_ReadEClock() and BTIM_GetSysTime() do not touch it. A request does, less than with timer.device: Disable() and Enable() write INTENA, ReplyMsg() goes through them too, and exec reads INTENAR and INTREQR when the interrupt comes.

### Measured

Raspberry Pi 4B, Emu68, idle machine, against timer.device 46.1. A wait of 3 ms ends late on average by 34 to 67 us depending on the unit (timer.device: 142 to 153 us, about 14 ms on UNIT_WAITUNTIL and UNIT_VBLANK). BTIM_ReadEClock() costs 0.29 us and BTIM_GetSysTime() 0.35 us (timer.device: about 20 us each, the CIAs are on the Amiga bus). A worst case depends on the scheduling of the tasks and varies from run to run. Details in docs/Benchmarks.md of the sources.

### Versions

| Version | Date | Changes |
|---|---|---|
| 0.9 | 7.11.2026 | First release candidate of 1.0. |

## BTIM_AddTime

Add one time value to another

```c
VOID BTIM_AddTime(struct timeval *dest, CONST struct timeval *src);    /* a0,a1 */
```

| Input | Register | Meaning |
|---|---|---|
| `dest` | a0 | the time to add to, normalised |
| `src` | a1 | the time to add, normalised |

dest = dest + src; the microseconds carry into the seconds. The same as timer.device/AddTime().

See also: [`BTIM_SubTime`](#btim_subtime), [`BTIM_CmpTime`](#btim_cmptime), `timer.device/AddTime`

## BTIM_CmpTime

Compare two time values

```c
LONG BTIM_CmpTime(CONST struct timeval *dest, CONST struct timeval *src);    /* a0,a1 */
```

| Input | Register | Meaning |
|---|---|---|
| `dest` | a0 | a time |
| `src` | a1 | another time |

Compares dest with src as timer.device/CmpTime() does.

**Result**

- `result`: 0 when they are equal, 1 when dest is the earlier, -1 when dest is the later.

See also: `timer.device/CmpTime`

## BTIM_GetSysTime

Read the system time

```c
VOID BTIM_GetSysTime(struct timeval *dest);    /* a0 */
```

| Input | Register | Meaning |
|---|---|---|
| `dest` | a0 | receives the system time |

The system time of this device: the time it had when the device was loaded (that of timer.device) or the last TR_SETSYSTIME, plus the microseconds counted since. No lock: callable from any context, interrupts included; it costs about 0.35 us.

Unlike timer.device, two calls may return the same time: the value never goes back (unless the time is set back), but it only changes once a microsecond. Do not divide by a difference of two times without checking it, and do not use the time as a unique identifier.

See also: [`BTIM_ReadEClock`](#btim_readeclock), `timer.device/GetSysTime`

## BTIM_ReadEClock

Read the eclock counter and its frequency

```c
ULONG BTIM_ReadEClock(struct EClockVal *dest);    /* a0 */
```

| Input | Register | Meaning |
|---|---|---|
| `dest` | a0 | receives the 64-bit counter (ev_hi, ev_lo) |

Reads the 64-bit counter of the system timer of the Pi, coherently (the high word read again when the low word wrapped). It counts microseconds since the Pi started and never goes back. Callable from any context, interrupts included; it costs about 0.3 us.

**Result**

- `frequency`: the ticks a second: 1000000 (BTIM_ECLOCK_HZ).

**Example**

```c
    struct EClockVal start, end;
    ULONG freq = BTIM_ReadEClock(&start);

    /* ... */
    BTIM_ReadEClock(&end);
    Printf("%ld us\n", end.ev_lo - start.ev_lo);  /* under 71 minutes */
```

See also: [`BTIM_GetSysTime`](#btim_getsystime), `timer.device/ReadEClock`

## BTIM_SubTime

Subtract one time value from another

```c
VOID BTIM_SubTime(struct timeval *dest, CONST struct timeval *src);    /* a0,a1 */
```

| Input | Register | Meaning |
|---|---|---|
| `dest` | a0 | the time to subtract from, normalised |
| `src` | a1 | the time to subtract, normalised |

dest = dest - src; a second is borrowed when the microseconds need it. The same as timer.device/SubTime().

See also: [`BTIM_AddTime`](#btim_addtime), `timer.device/SubTime`
