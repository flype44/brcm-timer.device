# brcm-timer.device

AmigaOS disk device for PiStorm / Emu68 on a Raspberry Pi 4 or 3: the interface of `timer.device` on the system timer of the Pi. A timer for the drivers and programs of that
ecosystem with a real need for one, which also spares the Amiga bus. It does not replace `timer.device` and has its own version numbers.

## Layout

| Path | Content |
|---|---|
| `src/clock.c` | Coherent read of CHI:CLO; system time = base + elapsed, 64/32 `divu.l`, sequence counter and `Disable()` for `TR_SETSYSTIME` |
| `src/queue.c` | The sorted queue of deadlines kept in `tr_time`; `BTIM_Kick` arms C1 and reads CLO again; half-wrap arming beyond 2^31 us; the server of the match, through `gic400.library` or `arm-intc.library` |
| `src/beginio.c` | `TR_ADDREQUEST` per unit, `TR_GETSYSTIME`, `TR_SETSYSTIME`; a request sent again while waiting is taken back |
| `src/abortio.c` | Looks for the request in the queue; 0 or -1 |
| `src/main.c`, `init.c`, `open.c`, `close.c`, `expunge.c`, `end.c` | RomTag and life cycle of the device |
| `src/btim_*.c` | One file per function of the SFD |
| `sfd/`, `include/devices/brcmtimer.h`, `Autodocs/` | The SDK |
| `gic400.library/` | Submodule: the SFD and the header of the library in the ROM of Emu68 (Pi 4) |
| `arm-intc.library/` | Submodule: the SFD and the header of the library of the ARM interrupt controller (Pi 3), from `LIBS:` |
| `testcases/stress.c` | The same battery on `timer.device` and this device |
| `examples/` | A complete client, part of the SDK |

## Design

- One queue for all the units, sorted by deadline as a value of the 64-bit counter. Compare channel C1 holds the low word of the first deadline; its match calls the server through GIC SPI 97 (BCM2711) or line 33 of `arm-intc.library` (BCM2837).
- The channel matches only on equality: after each write CLO is read again, and a deadline less than two ticks ahead is ended at once.
- `UNIT_WAITUNTIL` is converted to a counter value under `Disable()`; `TR_SETSYSTIME` moves those deadlines. `UNIT_VBLANK` shares the queue: at least the time asked, no chipset.
- `BTIM_ReadEClock()` and `BTIM_GetSysTime()` take no lock and do not touch the Amiga bus. Two readings may be equal: documented, on purpose.
- The system time is copied from `timer.device` at load. A later change of that time is not followed.
- C0 and C2 belong to the VideoCore firmware. SPI 97 is refused when a server of `gic400.library` holds it, or when it is enabled and routed to a CPU; line 33 when a server of `arm-intc.library` holds it.
- `arm-intc.library` is tried first: it opens only on a BCM2837. When it opens, `gic400.library` is never tried (an old copy on disk would open on a Pi 3 and touch a GIC that does not exist). `arm-intc.library` 0.3 or later.
- Other boards: `OpenDevice()` fails.

## Conventions

- bebbo gcc 6.5, `-Os -m68040 -msmall-code`, no libgcc, no C library, no writable global data, zero warnings.
- SoC registers through `st_rd()` and `st_wr()` only: 32-bit volatile accesses, little endian swapped, `move.l Dn,Dn` after each read and a `nop` after each write (no merging by the JIT; the `nop` is also a barrier under Emu68), peripherals at `0xf2000000` under Emu68.
- A function is added at the end of the SFD, never inserted. One file per function. MPL-2.0 header.
- English for code, comments, docs and commit messages. `Autodocs/` follows the layout of the NDK.

## Build and test

```sh
git submodule update --init
CC=m68k-amigaos-gcc cmake -S . -B build && make -C build install
```

`brcmtimer-stress [R=<rounds>]` on the target must report `failed: 0`.
