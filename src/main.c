/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdint.h>

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/devices.h>
#include <exec/resident.h>

#include <common/compiler.h>

#include "base.h"

/* The first code of the file: started as a program, the device does nothing */
int __attribute__((no_reorder)) _start()
{
    return -1;
}

extern const char deviceEnd;
extern const char deviceName[];
extern const char deviceIdString[];
extern const ULONG InitTable[];

const struct Resident RomTag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&RomTag,
    (APTR)&deviceEnd,
    RTF_AUTOINIT,
    BTIM_VERSION,
    NT_DEVICE,
    0,
    (char *)((intptr_t)&deviceName),
    (char *)((intptr_t)&deviceIdString),
    (APTR)InitTable,
};

const char deviceName[] = BRCMTIMERNAME;
const char deviceIdString[] = VERSION_STRING;

/* The reserved vector of every library */
static ULONG ExtFunc(VOID)
{
    return 0;
}

/* Open, Close, Expunge, the reserved vector, BeginIO, AbortIO, then the functions in the order of sfd/brcmtimer_lib.sfd: the offsets of
   timer.device. A call is added at the END (the revision goes up). */
static const ULONG btim_functions[] = {
    (ULONG)BTIM_DevOpen,
    (ULONG)BTIM_DevClose,
    (ULONG)BTIM_DevExpunge,
    (ULONG)ExtFunc,
    (ULONG)BTIM_BeginIO,
    (ULONG)BTIM_AbortIO,
    (ULONG)L_BTIM_AddTime,
    (ULONG)L_BTIM_SubTime,
    (ULONG)L_BTIM_CmpTime,
    (ULONG)L_BTIM_ReadEClock,
    (ULONG)L_BTIM_GetSysTime,
    (ULONG)-1
};

const ULONG InitTable[4] = {
    sizeof(struct BrcmTimerBase),
    (ULONG)btim_functions,
    0,
    (ULONG)BTIM_DevInit
};
