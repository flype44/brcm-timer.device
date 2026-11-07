/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/*
    brcmtimer-example: what a client of brcm-timer.device does: open it, read the clock, wait, read the time, close it.

        m68k-amigaos-gcc -noixemul -O2 -m68040 -I<sdk>/include -o brcmtimer-example brcmtimer-example.c
*/

#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <devices/brcmtimer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/brcmtimer.h>

struct Device *BrcmTimerBase;

int main(void)
{
    struct MsgPort *port;
    struct timerequest tr;
    struct EClockVal start, end;
    struct timeval now;
    ULONG frequency;
    int result = RETURN_FAIL;

    port = CreateMsgPort();
    if (port == NULL)
        return RETURN_FAIL;

    tr.tr_node.io_Message.mn_ReplyPort = port;
    tr.tr_node.io_Message.mn_Length = sizeof(tr);

    /* It fails on a board other than a Raspberry Pi 4: a program may then open timer.device */
    if (OpenDevice((CONST_STRPTR)BRCMTIMERNAME, UNIT_MICROHZ, (struct IORequest *)&tr, 0) == 0)
    {
        BrcmTimerBase = tr.tr_node.io_Device;

        frequency = BTIM_ReadEClock(&start);

        tr.tr_node.io_Command = TR_ADDREQUEST;
        tr.tr_time.tv_secs = 0;
        tr.tr_time.tv_micro = 2000;
        DoIO((struct IORequest *)&tr);

        BTIM_ReadEClock(&end);
        BTIM_GetSysTime(&now);

        Printf((CONST_STRPTR)"EClock %lu Hz; a wait of 2000 us took %lu us; system time %lu.%06lu\n", frequency, end.ev_lo - start.ev_lo,
            now.tv_secs, now.tv_micro);

        CloseDevice((struct IORequest *)&tr);

        result = RETURN_OK;
    }
    else
        Printf((CONST_STRPTR)"Cannot open %s\n", (LONG)BRCMTIMERNAME);

    DeleteMsgPort(port);

    return result;
}
