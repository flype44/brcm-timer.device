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
#include <proto/exec.h>

#include <common/compiler.h>

#include "base.h"

/* The request is looked for in the queue: its ln_Type alone does not say it is there. Found, it leaves the
   queue (the channel is armed again when it was the first) and is replied with IOERR_ABORTED: 0. Not found (replied already, never sent,
   or being replied by another AbortIO()): -1. Its ln_Type stays NT_MESSAGE until ReplyMsg(), so that WaitIO() waits for the reply. */
LONG BTIM_AbortIO(
    REGARG(struct IORequest *io, "a1"),
    REGARG(struct BrcmTimerBase *base, "a6"))
{
    struct ExecBase *SysBase = base->bt_SysBase;
    struct timerequest *tr = (struct timerequest *)io;
    struct Node *node;

    Disable();

    for (node = base->bt_Queue.lh_Head; node->ln_Succ != NULL; node = node->ln_Succ)
    {
        if (node == &io->io_Message.mn_Node)
        {
            BOOL first = base->bt_Queue.lh_Head == node;

            Remove(node);

            if (first)
            {
                BTIM_Kick(base);
            }

            Enable();

            tr->tr_time.tv_secs = 0;
            tr->tr_time.tv_micro = 0;
            io->io_Error = IOERR_ABORTED;
            ReplyMsg(&io->io_Message);

            return 0;
        }
    }

    Enable();

    return -1;
}
