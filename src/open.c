/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/devices.h>
#include <exec/errors.h>

#include <common/compiler.h>

#include "base.h"

/* Called by OpenDevice() with the system forbidden. Units 0 to 4 as timer.device; io_Unit points to the unit. Nothing is allocated:
   only a wrong unit fails. */
VOID BTIM_DevOpen(
    REGARG(struct IORequest *io, "a1"),
    REGARG(ULONG unit, "d0"),
    REGARG(ULONG flags, "d1"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    (void)flags;

    if (unit >= BTIM_UNITS)
    {
        io->io_Error = IOERR_OPENFAIL;
        io->io_Device = NULL;
        return;
    }

    base->bt_Device.dd_Library.lib_OpenCnt++;
    base->bt_Device.dd_Library.lib_Flags &= ~LIBF_DELEXP;
    base->bt_Units[unit].bu_Unit.unit_OpenCnt++;

    io->io_Device = &base->bt_Device;
    io->io_Unit = &base->bt_Units[unit].bu_Unit;
    io->io_Error = 0;
    io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
}
