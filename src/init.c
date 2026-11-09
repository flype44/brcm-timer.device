/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/devices.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include <common/compiler.h>

#include "base.h"

extern const char deviceName[];
extern const char deviceIdString[];

/* exec.library has no NewList(): amiga.lib's, in place */
static void InitList(struct List *list)
{
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
}

/* Called once by InitResident(), with the base that MakeLibrary() has just made. The system time starts as the one of timer.device
   (a program that uses both devices sees the same time at first; the two clocks then drift apart slowly), at the counter value of
   now. Then the server of compare channel 1 (SPI 97 through gic400.library on a BCM2711, line 33 through arm-intc.library on a BCM2837):
   without it (another board, or the interrupt in use) the device is not made, and OpenDevice() fails. */
struct BrcmTimerBase *BTIM_DevInit(
    REGARG(struct BrcmTimerBase *base, "d0"),
    REGARG(BPTR seglist, "a0"),
    REGARG(struct ExecBase *SysBase, "a6"))
{
    struct timerequest tr;
    struct timeval now;

    base->bt_Device.dd_Library.lib_Node.ln_Type = NT_DEVICE;
    base->bt_Device.dd_Library.lib_Node.ln_Pri = 0;
    base->bt_Device.dd_Library.lib_Node.ln_Name = (STRPTR)deviceName;
    base->bt_Device.dd_Library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->bt_Device.dd_Library.lib_Version = BTIM_VERSION;
    base->bt_Device.dd_Library.lib_Revision = BTIM_REVISION;
    base->bt_Device.dd_Library.lib_IdString = (STRPTR)deviceIdString;

    base->bt_SysBase = SysBase;
    base->bt_SegList = seglist;

    now.tv_secs = 0;
    now.tv_micro = 0;

    for (ULONG i = 0; i < BTIM_UNITS; i++)
    {
        base->bt_Units[i].bu_Number = i;
    }

    tr.tr_node.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    tr.tr_node.io_Message.mn_ReplyPort = NULL;
    tr.tr_node.io_Message.mn_Length = sizeof(tr);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)&tr, 0) == 0)
    {
        struct Device *TimerBase = tr.tr_node.io_Device;

        GetSysTime(&now);
        CloseDevice((struct IORequest *)&tr);
    }

    /* the queue first: setting the system time moves the waiting UNIT_WAITUNTIL requests (none yet) */
    InitList(&base->bt_Queue);

    BTIM_WriteSysTime(base, &now);

    if (!BTIM_StartTimer(base))
    {
        ULONG neg = base->bt_Device.dd_Library.lib_NegSize;
        ULONG pos = base->bt_Device.dd_Library.lib_PosSize;

        FreeMem((UBYTE *)base - neg, neg + pos);
        return NULL;
    }

    return base;
}
