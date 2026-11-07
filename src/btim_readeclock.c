/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <devices/timer.h>

#include <common/compiler.h>

#include "base.h"

/* The 64-bit counter of the system timer (microseconds since the Pi started), and its frequency. Any context, interrupts included. */
ULONG L_BTIM_ReadEClock(
    REGARG(struct EClockVal *dest, "a0"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    (void)base;

    BTIM_ReadCounter(&dest->ev_hi, &dest->ev_lo);

    return BTIM_ECLOCK_HZ;
}
