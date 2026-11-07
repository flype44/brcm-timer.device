# brcm-timer.device

AmigaOS disk device for PiStorm / Emu68 on a Raspberry Pi 4: the interface of `timer.device` on the system timer of the Pi. A timer for the drivers and programs of that
ecosystem with a real need for one, which also spares the Amiga bus. It does not replace `timer.device` and has its own version numbers.

## Layout

| Path | Content |
|---|---|
| `src/clock.c` | Coherent read of CHI:CLO; system time = base + elapsed, 64/32 `divu.l`, sequence counter and `Disable()` for `TR_SETSYSTIME` |
| `src/queue.c` | The sorted queue of deadlines kept in `tr_time`; `BTIM_Kick` arms C1 and reads CLO again; half-wrap arming beyond 2^31 us; the server of SPI 97 |
| `src/beginio.c` | `TR_ADDREQUEST` per unit, `TR_GETSYSTIME`, `TR_SETSYSTIME`; a request sent again while waiting is taken back |
| `src/abortio.c` | Looks for the request in the queue; 0 or -1 |
| `src/main.c`, `init.c`, `open.c`, `close.c`, `expunge.c`, `end.c` | RomTag and life cycle of the device |
| `src/btim_*.c` | One file per function of the SFD |
| `sfd/`, `include/devices/brcmtimer.h`, `Autodocs/` | The SDK |
| `gic400.library/` | Submodule: the SFD and the header of the library in the ROM of Emu68 |
| `testcases/stress.c` | The same battery on `timer.device` and this device |
| `examples/` | A complete client, part of the SDK |

## Design

- One queue for all the units, sorted by deadline as a value of the 64-bit counter. Compare channel C1 holds the low word of the first deadline, GIC SPI 97 calls the server.
- The channel matches only on equality: after each write CLO is read again, and a deadline less than two ticks ahead is ended at once.
- `UNIT_WAITUNTIL` is converted to a counter value under `Disable()`; `TR_SETSYSTIME` moves those deadlines. `UNIT_VBLANK` shares the queue: at least the time asked, no chipset.
- `BTIM_ReadEClock()` and `BTIM_GetSysTime()` take no lock and do not touch the Amiga bus. Two readings may be equal: documented, on purpose.
- The system time is copied from `timer.device` at load. A later change of that time is not followed.
- C0 and C2 belong to the VideoCore firmware. SPI 97 is refused when a server of `gic400.library` holds it, or when it is enabled and routed to a CPU.
- Pi 3 and earlier: no GIC, `OpenDevice()` fails.

## Conventions

- bebbo gcc 6.5, `-Os -m68040 -msmall-code`, no libgcc, no C library, no writable global data, zero warnings.
- SoC registers through the `move.l` helpers only: 32-bit accesses, little endian swapped, peripherals at `0xf2000000` under Emu68.
- A function is added at the end of the SFD, never inserted. One file per function. MPL-2.0 header.
- English for code, comments, docs and commit messages. `Autodocs/` follows the layout of the NDK.

## Build and test

```sh
git submodule update --init
CC=m68k-amigaos-gcc cmake -S . -B build && make -C build install
```

`brcmtimer-stress [R=<rounds>]` on the target must report `failed: 0`.
