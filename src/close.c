/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/devices.h>

#include <common/compiler.h>

#include "base.h"

/* Called by CloseDevice() with the device in A6. Gives the seglist back (to unload the device) if this was the last opener and an
   expunge was waiting for it, else 0. The request can no longer be used: its device and unit are cleared. */
BPTR BTIM_DevClose(
    REGARG(struct IORequest *io, "a1"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    struct BTimUnit *unit = (struct BTimUnit *)io->io_Unit;

    if (unit != NULL && unit->bu_Unit.unit_OpenCnt != 0)
    {
        unit->bu_Unit.unit_OpenCnt--;
    }

    io->io_Device = (struct Device *)-1;
    io->io_Unit = (struct Unit *)-1;

    if (base->bt_Device.dd_Library.lib_OpenCnt != 0)
    {
        base->bt_Device.dd_Library.lib_OpenCnt--;
    }

    if (base->bt_Device.dd_Library.lib_OpenCnt == 0 && (base->bt_Device.dd_Library.lib_Flags & LIBF_DELEXP))
    {
        return BTIM_DevExpunge(base);
    }

    return 0;
}
