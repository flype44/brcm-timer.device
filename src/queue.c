/*
    Copyright © 2026 Philippe Carpentier
    https://github.com/flype44

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/*
    The timer queue. Every waiting request, whatever its unit, is in bt_Queue sorted by its deadline (a value of the 64-bit counter
    of the system timer, kept in its tr_time). Compare channel 1 holds the low 32 bits of the first deadline; its match raises SPI 97,
    whose server ends the requests that are due and arms the channel for the next one.

    The channel matches only when the counter equals it: a deadline that passes while it is being written would wait for the counter
    to wrap (71 minutes). So after each write the counter is read again, and a deadline less than two ticks ahead is waited for here
    and ended at once: at most 2 us, against a match that the write could miss.
    A deadline more than 2^31 us away arms the channel half a wrap ahead, and the server arms it again from there.
*/

#include <exec/types.h>
#include <exec/interrupts.h>
#include <exec/errors.h>
#include <proto/exec.h>
#include <proto/gic400.h>

#include <common/compiler.h>

#include "base.h"

/* a <= b, 64 bits */
static inline BOOL NotAfter(ULONG ahi, ULONG alo, ULONG bhi, ULONG blo)
{
    return ahi < bhi || (ahi == bhi && alo <= blo);
}

/* Takes the first request off the queue and replies it: its tr_time cleared, as timer.device does */
static void Complete(struct BrcmTimerBase *base, struct timerequest *tr)
{
    struct ExecBase *SysBase = base->bt_SysBase;

    Remove(&tr->tr_node.io_Message.mn_Node);
    tr->tr_time.tv_secs = 0;
    tr->tr_time.tv_micro = 0;
    tr->tr_node.io_Error = 0;
    ReplyMsg(&tr->tr_node.io_Message);
}

/* Ends what is due and arms channel 1 for the first deadline left. With the interrupts disabled, or in the server. */
void BTIM_Kick(
    struct BrcmTimerBase *base)
{
    for (;;)
    {
        struct timerequest *tr = (struct timerequest *)base->bt_Queue.lh_Head;
        ULONG hi, lo, target;

        if (tr->tr_node.io_Message.mn_Node.ln_Succ == NULL)
        {
            return;
        }

        BTIM_ReadCounter(&hi, &lo);

        if (NotAfter(DEADLINE_HI(tr), DEADLINE_LO(tr), hi, lo))
        {
            Complete(base, tr);
            continue;
        }

        /* far away (2^31 us or more ahead): half a wrap now, the server arms it again then */
        if (DEADLINE_HI(tr) - hi - (DEADLINE_LO(tr) < lo) != 0 || DEADLINE_LO(tr) - lo >= 0x80000000UL)
        {
            target = lo + 0x80000000UL;
        }
        else
        {
            target = DEADLINE_LO(tr);
        }

        st_wr(ST_C1, target);

        /* far enough ahead after the write: the match will come */
        if ((LONG)(target - st_rd(ST_CLO)) > 1)
        {
            return;
        }
    }
}

/* Puts a request into the queue by its deadline (after those with the same one). With the interrupts disabled. */
void BTIM_Insert(
    struct BrcmTimerBase *base,
    struct timerequest *tr)
{
    struct ExecBase *SysBase = base->bt_SysBase;
    struct Node *prev = (struct Node *)&base->bt_Queue.lh_Head;
    struct Node *node;

    for (node = base->bt_Queue.lh_Head; node->ln_Succ != NULL; node = node->ln_Succ)
    {
        struct timerequest *other = (struct timerequest *)node;

        if (!NotAfter(DEADLINE_HI(other), DEADLINE_LO(other), DEADLINE_HI(tr), DEADLINE_LO(tr)))
        {
            break;
        }

        prev = node;
    }

    Insert(&base->bt_Queue, &tr->tr_node.io_Message.mn_Node, prev);
}

/* TR_SETSYSTIME moved the system time: every waiting UNIT_WAITUNTIL request gets its deadline moved by dhi:dlo (signed, us; a deadline
   that would go below 0 becomes 0, due) and goes back into the queue in order, then the channel is armed for the first. With the
   interrupts disabled. */
void BTIM_Retime(
    struct BrcmTimerBase *base,
    LONG dhi,
    ULONG dlo)
{
    struct ExecBase *SysBase = base->bt_SysBase;
    struct List moved;
    struct Node *node, *next;

    moved.lh_Head = (struct Node *)&moved.lh_Tail;
    moved.lh_Tail = NULL;
    moved.lh_TailPred = (struct Node *)&moved.lh_Head;

    for (node = base->bt_Queue.lh_Head; (next = node->ln_Succ) != NULL; node = next)
    {
        struct timerequest *tr = (struct timerequest *)node;

        if (((struct BTimUnit *)tr->tr_node.io_Unit)->bu_Number == UNIT_WAITUNTIL)
        {
            ULONG lo = DEADLINE_LO(tr) + dlo;
            LONG hi = (LONG)DEADLINE_HI(tr) + dhi + (lo < dlo);

            if (hi < 0)
            {
                hi = 0;
                lo = 0;
            }

            DEADLINE_HI(tr) = (ULONG)hi;
            DEADLINE_LO(tr) = lo;

            Remove(node);
            AddTail(&moved, node);
        }
    }

    while ((node = RemHead(&moved)) != NULL)
    {
        BTIM_Insert(base, (struct timerequest *)node);
    }

    BTIM_Kick(base);
}

/* The server of SPI 97 (is_Data in A1): the match of channel 1 cleared, then what is due ended and the next deadline armed */
static void Server(REGARG(struct BrcmTimerBase *base, "a1"))
{
    st_wr(ST_CS, ST_M1);
    BTIM_Kick(base);
}

/* At init: gic400.library (a BCM2711: version 0.1 refuses the boards without it), SPI 97 free, the server installed. gic400.library
   unroutes every SPI when it starts but leaves the enables as they were, so after a reboot of the OS alone SPI 97 is still
   enabled from the boot before: it is someone else's only when it is routed to a CPU too (a server of gic400.library is refused by
   AddIntServerEx anyway) */
BOOL BTIM_StartTimer(
    struct BrcmTimerBase *base)
{
    struct ExecBase *SysBase = base->bt_SysBase;
    struct Library *GIC400_Base;
    BOOL enabled = FALSE;

    GIC400_Base = OpenLibrary((CONST_STRPTR)"gic400.library", GIC400_MIN_VERSION);
    if (GIC400_Base == NULL)
    {
        return FALSE;
    }

    if ((GIC400_Base->lib_Version == GIC400_MIN_VERSION && GIC400_Base->lib_Revision < GIC400_MIN_REVISION) ||
        GetIntStatus(ST_SPI_C1, NULL, NULL, &enabled) != 0 || (enabled && QueryIntRoute(ST_SPI_C1) != 0))
    {
        /* too old, or the interrupt belongs to someone else */
        CloseLibrary(GIC400_Base);
        return FALSE;
    }

    base->bt_Server.is_Node.ln_Type = NT_INTERRUPT;
    base->bt_Server.is_Node.ln_Pri = 0;
    base->bt_Server.is_Node.ln_Name = base->bt_Device.dd_Library.lib_Node.ln_Name;
    base->bt_Server.is_Data = base;
    base->bt_Server.is_Code = (VOID (*)())Server;

    /* level triggered, priority 0. A stale match (CS.M1 is set since the counter last passed the value of C1) calls the server once,
       which clears it: it is not cleared here, the interrupt could still be someone else's until this succeeds. */
    if (AddIntServerEx(ST_SPI_C1, 0, FALSE, &base->bt_Server) != 0)
    {
        CloseLibrary(GIC400_Base);
        return FALSE;
    }

    base->bt_Gic400Base = GIC400_Base;

    return TRUE;
}

/* At expunge, the queue empty (every request belongs to an opener, and nobody has the device open) */
void BTIM_StopTimer(
    struct BrcmTimerBase *base)
{
    struct ExecBase *SysBase = base->bt_SysBase;
    struct Library *GIC400_Base = base->bt_Gic400Base;

    if (GIC400_Base == NULL)
    {
        return;
    }

    RemIntServerEx(ST_SPI_C1, &base->bt_Server);
    st_wr(ST_CS, ST_M1);
    CloseLibrary(GIC400_Base);
    base->bt_Gic400Base = NULL;
}
