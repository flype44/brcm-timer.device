# brcm-timer.device benchmarks

`brcmtimer-stress R=2000` runs the same battery on `timer.device` and on `brcm-timer.device`, timed for both with the system timer of the Pi.

| | |
|---|---|
| Machine | Raspberry Pi 4 Model B at 1.8 GHz, PiStorm32 lite, Emu68 1.1.0-beta.2 |
| System | AmigaOS 3.2 (Kickstart 47.13), `timer.device` 46.1 |
| Conditions | idle machine, a VideoCore driver not loaded |

## The cost of a call

| Call | timer.device | brcm-timer.device | Factor |
|---|---|---|---|
| `ReadEClock()` | 20.10 µs | **0.14 µs** | 143 |
| `GetSysTime()` | 20.18 µs | **0.23 µs** | 87 |
| `AddTime()` / `SubTime()` / `CmpTime()` | 0.09 / 0.07 / 0.08 µs | 0.06 / 0.08 / 0.06 µs | about 1 |
| `DoIO()` `TR_GETSYSTIME` | 25.70 µs | **0.37 µs** | 69 |
| `DoIO()` `TR_ADDREQUEST`, no time | 164.83 µs | **22.51 µs** | 7.3 |
| `SendIO()` + `AbortIO()` + `WaitIO()` | 122.72 µs | **28.67 µs** | 4.2 |
| `SendIO()` into a queue growing to 128 | 62.64 µs | **6.25 µs** | 10 |
| `AbortIO()` + `WaitIO()` in that queue | 58.33 µs | **22.78 µs** | 2.5 |

Reading the time on `timer.device` goes through the CIAs on the Amiga bus; `BTIM_ReadEClock()` and `BTIM_GetSysTime()` do not touch that bus. A request
still does, less often: `Disable()` and `Enable()` write `INTENA`, and exec reads `INTENAR` and `INTREQR` when the interrupt comes.

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
