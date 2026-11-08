# brcm-timer.device

A timer for the drivers and programs of the AmigaOS + Emu68 + Raspberry Pi 4 ecosystem that have a real need for one. It has the
interface of `timer.device` (units, commands, `struct timerequest`, functions) on the system timer of the Pi: a 64-bit counter at
1 MHz and a compare channel, through `gic400.library`. Reading the time does not touch the Amiga bus.

It is a disk device next to the system `timer.device`, which it does not replace or change.

| | timer.device | brcm-timer.device |
|---|---|---|
| `ReadEClock()` | 20.08 µs | **0.29 µs** |
| `GetSysTime()` | 20.13 µs | **0.35 µs** |
| A 3 ms wait, average lateness | 142-153 µs (about 14 ms on `UNIT_WAITUNTIL`, `UNIT_VBLANK`) | **34-67 µs** on every unit |

Method and all the measures: [docs/Benchmarks.md](docs/Benchmarks.md).

**Status:** release candidate of 1.0. The interface may still change until 1.0.

## Requirements

A Raspberry Pi 4, 400 or CM4 under Emu68, with `gic400.library` 1.3 or later in its ROM. `OpenDevice()` fails elsewhere: a program can
then open `timer.device`.

## Using it

```c
struct Device *BrcmTimerBase;

if (OpenDevice(BRCMTIMERNAME, UNIT_MICROHZ, (struct IORequest *)&tr, 0) == 0)
{
    BrcmTimerBase = tr.tr_node.io_Device;
    frequency = BTIM_ReadEClock(&now);
    /* ... */
}
```

The API is in [Autodocs/brcmtimer.md](Autodocs/brcmtimer.md), a complete client in
[examples/brcmtimer-example.c](examples/brcmtimer-example.c).

What differs from `timer.device`, all described in the Autodoc:

- `lib_Version` has its own numbers: do not test it against those of `timer.device`.
- The EClock runs at 1000000 Hz, while `ex_EClockFrequency` of exec still gives the one of the Amiga: use what `BTIM_ReadEClock()` returns.
- Two readings of `BTIM_GetSysTime()` may be equal.
- The functions keep the usual registers only: A0 and A1 are scratch.
- The system time is copied from `timer.device` when the device is loaded: a later change of that time does not reach this device.
- `UNIT_VBLANK` and `UNIT_WAITUNTIL` have the precision of the other units.

## Building

The [bebbo m68k-amigaos-gcc](https://github.com/bebbo/amiga-gcc) toolchain with `sfdc`, and CMake 3.30 or later.

```sh
git clone --recursive https://github.com/flype44/brcm-timer.device.git
cd brcm-timer.device
CC=m68k-amigaos-gcc cmake -S . -B build
make -C build install
```

`build/install/` then holds `devs/brcm-timer.device` for `DEVS:`, `sdk/` for the developers (`include/`, `sfd/`, `FD/`, `Autodocs/`,
`examples/`) and `tools/`: `brcmtimer-example` and `brcmtimer-stress`, which runs the same battery on `timer.device` and on this device
and reports them side by side.

## License

[Mozilla Public License 2.0](LICENSE).
