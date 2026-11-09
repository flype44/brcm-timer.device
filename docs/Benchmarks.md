# brcm-timer.device benchmarks

`brcmtimer-stress` runs the same battery on `timer.device` and on `brcm-timer.device`, timed for both with the system timer of the Pi. The figures
below are those of a Raspberry Pi 4; the [Raspberry Pi 3](#raspberry-pi-3) has its own section at the end. On the Pi 4, the cost of a call
comes from a run of 1.0-rc2, the lateness and the load from a run of `R=2000`. The reads of the time were measured on a build with nothing
after a read; the `move.l Dn,Dn` of 1.0-rc3, timed alone, costs nothing more (1.0-rc2 put a `nop` after each read: 0.29 µs and 0.35 µs).

| | |
|---|---|
| Machine | Raspberry Pi 4 Model B at 1.8 GHz, PiStorm32 lite, Emu68 1.1.0-beta.2 |
| System | AmigaOS 3.2 (Kickstart 47.13), `timer.device` 46.1 |
| Conditions | idle machine, a VideoCore driver not loaded |

## The cost of a call

| Call | timer.device | brcm-timer.device | Factor |
|---|---|---|---|
| `ReadEClock()` | 20.08 µs | **0.15 µs** | 134 |
| `GetSysTime()` | 20.13 µs | **0.25 µs** | 81 |
| `AddTime()` / `SubTime()` / `CmpTime()` | 0.07 / 0.07 / 0.07 µs | 0.05 / 0.07 / 0.06 µs | about 1 |
| `DoIO()` `TR_GETSYSTIME` | 25.78 µs | **0.46 µs** | 56 |
| `DoIO()` `TR_ADDREQUEST`, no time | 167.74 µs | **26.28 µs** | 6.4 |
| `SendIO()` + `AbortIO()` + `WaitIO()` | 119.56 µs | **29.64 µs** | 4.0 |
| `SendIO()` into a queue growing to 128 | 62.33 µs | **6.54 µs** | 9.5 |
| `AbortIO()` + `WaitIO()` in that queue | 56.93 µs | **22.89 µs** | 2.5 |

Reading the time on `timer.device` goes through the CIAs on the Amiga bus; `BTIM_ReadEClock()` and `BTIM_GetSysTime()` do not touch that bus. A request
still does, less often: `Disable()` and `Enable()` write `INTENA`, and exec reads `INTENAR` and `INTREQR` when the interrupt comes. Each read of a register
of the system timer is followed by `move.l Dn,Dn`, which keeps the JIT of Emu68 from merging two reads and costs nothing measurable; each write by
a `nop`, a barrier under Emu68 (about 50 ns).

## The lateness of a 3 ms wait

Average and worst, from before the send to the task running after the reply.

| Unit (samples) | timer.device | brcm-timer.device |
|---|---|---|
| `UNIT_MICROHZ` (200) | 153 / 417 µs | **34 / 110 µs** |
| `UNIT_ECLOCK` (200) | 151 / 423 µs | **40 / 276 µs** |
| `UNIT_WAITECLOCK` (200) | 142 / 515 µs | **36 / 181 µs** |
| `UNIT_WAITUNTIL` (100) | 13897 / 16001 µs | **67 / 2505 µs** |
| `UNIT_VBLANK` (100) | 13906 / 14499 µs | **34 / 87 µs** |

The averages hold from run to run. The worst cases do not: they depend on the scheduling of the tasks, and `UNIT_WAITUNTIL` was 247, 1148 and 2505 µs on
three runs. These figures are for an idle machine; they say nothing of a machine under a heavy serial or MIDI load.

## Under load

Six processes of priorities -2 to 5 and a queue of 128 requests at once, on random units, with delays up to 20 ms and one request in seven aborted.

| | timer.device | brcm-timer.device |
|---|---|---|
| Requests (aborted) | 448000 (64413) | 448000 (64413) |
| Early, lost, replied twice, with an error | 0 | 0 |
| Time of the load | 102.8 s | **43.0 s** |

## Reading the time in a tight loop

The system time changes once a microsecond and a call takes less: 73 % of the readings of a tight `BTIM_GetSysTime()` loop equal the one before. The reading
never goes back unless the time is set back.

The two devices run on different crystals: they drift apart by about 9 ppm.

## Raspberry Pi 3

One run of `brcmtimer-stress R=20` on 0.10, the match of the timer through `arm-intc.library`.

| | |
|---|---|
| Machine | Raspberry Pi 3 Model B, PiStorm32 lite, Emu68 1.0.7 |
| System | CaffeineOS (exec 45.20), `timer.device` 39.4, `arm-intc.library` 0.3 |
| Conditions | a busy machine, not idle |

| Call | timer.device | brcm-timer.device | Factor |
|---|---|---|---|
| `ReadEClock()` | 22.53 µs | **0.49 µs** | 46 |
| `GetSysTime()` | 22.39 µs | **0.69 µs** | 32 |
| `AddTime()` / `SubTime()` / `CmpTime()` | 0.29 / 0.21 / 0.22 µs | 0.17 / 0.20 / 0.17 µs | about 1 |
| `DoIO()` `TR_GETSYSTIME` | 29.67 µs | **1.02 µs** | 29 |
| `DoIO()` `TR_ADDREQUEST`, no time | 192.65 µs | **28.04 µs** | 6.9 |
| `SendIO()` + `AbortIO()` + `WaitIO()` | 146.40 µs | **36.99 µs** | 4.0 |
| `SendIO()` into a queue growing to 128 | 77.21 µs | **9.21 µs** | 8.4 |
| `AbortIO()` + `WaitIO()` in that queue | 70.71 µs | **38.96 µs** | 1.8 |

The lateness of a 3 ms wait, average and worst:

| Unit | timer.device | brcm-timer.device |
|---|---|---|
| `UNIT_MICROHZ` | 181 / 955 µs | **61** / 1564 µs |
| `UNIT_ECLOCK` | 183 / 590 µs | **54** / 618 µs |
| `UNIT_WAITECLOCK` | 174 / 1090 µs | **49** / 1001 µs |
| `UNIT_WAITUNTIL` | 13807 / 16240 µs | **43 / 411 µs** |
| `UNIT_VBLANK` | 13905 / 15039 µs | **49 / 403 µs** |

The load (6 processes, a queue of 128, 4480 requests of which 654 aborted): none early, lost, replied twice or with an error on either
device; 832 ms on `timer.device`, **460 ms** on `brcm-timer.device`. A run of 20 rounds is short: its worst cases say less than those of the
Pi 4 above. `brcmtimer-queue` over 100 rounds of 64 requests (5477 done, 923 aborted) found none early, lost or replied twice.
