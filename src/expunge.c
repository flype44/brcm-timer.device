/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/devices.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <common/compiler.h>

#include "base.h"

/* Called by the system with the device in A6, to remove it: when the memory runs short ("Avail FLUSH") or on request. Done at once if
   nobody has it open, else by the Close of the last opener. Gives the seglist to unload, or 0. */
BPTR BTIM_DevExpunge(
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    struct ExecBase *SysBase = base->bt_SysBase;
    BPTR seglist = 0;

    if (base->bt_Device.dd_Library.lib_OpenCnt == 0)
    {
        ULONG neg = base->bt_Device.dd_Library.lib_NegSize;
        ULONG pos = base->bt_Device.dd_Library.lib_PosSize;

        Remove(&base->bt_Device.dd_Library.lib_Node);

        BTIM_StopTimer(base);

        seglist = base->bt_SegList;
        FreeMem((UBYTE *)base - neg, neg + pos);
    }
    else
    {
        /* Still in use: the Close of the last opener does it */
        base->bt_Device.dd_Library.lib_Flags |= LIBF_DELEXP;
    }

    return seglist;
}
