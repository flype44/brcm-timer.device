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

/* As timer.device: 0 when equal, 1 when dest is the earlier, -1 when dest is the later; unsigned comparisons */
LONG L_BTIM_CmpTime(
    REGARG(const struct timeval *dest, "a0"),
    REGARG(const struct timeval *src, "a1"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    (void)base;

    if (dest->tv_secs != src->tv_secs)
    {
        return src->tv_secs > dest->tv_secs ? 1 : -1;
    }

    if (dest->tv_micro != src->tv_micro)
    {
        return src->tv_micro > dest->tv_micro ? 1 : -1;
    }

    return 0;
}
