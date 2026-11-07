/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef DEVICES_BRCMTIMER_H
#define DEVICES_BRCMTIMER_H
/*
**	$VER: brcmtimer.h 0.9
**
**	Public definitions of "brcm-timer.device": the interface of timer.device (units, commands, struct timerequest,
**	struct EClockVal of <devices/timer.h>), driven by the system timer of the Raspberry Pi (PiStorm / Emu68) instead of the
**	CIAs. See Autodocs/brcmtimer.doc.
**
*/

#ifndef DEVICES_TIMER_H
#include <devices/timer.h>
#endif

#define BRCMTIMERNAME       "brcm-timer.device"

/* The EClock of this device counts microseconds since the Pi started: BTIM_ReadEClock() returns this frequency */
#define BTIM_ECLOCK_HZ      1000000

/* Not named by the <devices/timer.h> of the older NDKs */
#ifndef UNIT_WAITECLOCK
#define UNIT_WAITECLOCK     4
#endif

#endif /* DEVICES_BRCMTIMER_H */
