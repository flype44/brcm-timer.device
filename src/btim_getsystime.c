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

/* The system time, read from the counter: no lock, any context, interrupts included */
VOID L_BTIM_GetSysTime(
    REGARG(struct timeval *dest, "a0"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    BTIM_ReadSysTime(base, dest);
}
