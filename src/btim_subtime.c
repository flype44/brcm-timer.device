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

/* dest = dest - src, a microsecond borrowed from the seconds when needed */
VOID L_BTIM_SubTime(
    REGARG(struct timeval *dest, "a0"),
    REGARG(const struct timeval *src, "a1"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    (void)base;

    dest->tv_secs -= src->tv_secs;
    if (dest->tv_micro < src->tv_micro)
    {
        dest->tv_micro += 1000000 - src->tv_micro;
        dest->tv_secs--;
    }
    else
    {
        dest->tv_micro -= src->tv_micro;
    }
}
