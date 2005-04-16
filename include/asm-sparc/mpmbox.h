/* $Id: mpmbox.h,v 1.4 1996/04/25 06:13:19 davem Exp $
 * mpmbox.h:  Interface and defines for the OpenProm mailbox
 *               facilities for MP machines under Linux.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_MPMBOX_H
#define _SPARC_MPMBOX_H

/* The prom allocates, for each CPU on the machine an unsigned
 * byte in physical ram.  You probe the device tree prom nodes
 * for these values.  The purpose of this byte is to be able to
 * pass messages from one cpu to another.
 */

/* These are the main message types we have to look for in our
 * Cpu mailboxes, based upon these values we decide what course
 * of action to take.
 */

/* The CPU is executing code in the kernel. */
#define MAILBOX_ISRUNNING     0xf0

/* Another CPU called romvec->pv_exit(), you should call
 * prom_stopcpu() when you see this in your mailbox.
 */
#define MAILBOX_EXIT          0xfb

/* Another CPU called romvec->pv_enter(), you should call
 * prom_cpuidle() when this is seen.
 */
#define MAILBOX_GOSPIN        0xfc

/* Another CPU has hit a breakpoint either into kadb or the prom
 * itself.  Just like MAILBOX_GOSPIN, you should call prom_cpuidle()
 * at this point.
 */
#define MAILBOX_BPT_SPIN      0xfd

/* Oh geese, some other nitwit got a damn watchdog reset.  The party's
 * over so go call prom_stopcpu().
 */
#define MAILBOX_WDOG_STOP     0xfe

#ifndef __ASSEMBLY__

/* Handy macro's to determine a cpu's state. */

/* Is the cpu still in Power On Self Test? */
#define MBOX_POST_P(letter)  ((letter) >= 0x00 && (letter) <= 0x7f)

/* Is the cpu at the 'ok' prompt of the PROM? */
#define MBOX_PROMPROMPT_P(letter) ((letter) >= 0x80 && (letter) <= 0x8f)

/* Is the cpu spinning in the PROM? */
#define MBOX_PROMSPIN_P(letter) ((letter) >= 0x90 && (letter) <= 0xef)

/* Sanity check... This is junk mail, throw it out. */
#define MBOX_BOGON_P(letter) ((letter) >= 0xf1 && (letter) <= 0xfa)

/* Is the cpu actively running an application/kernel-code? */
#define MBOX_RUNNING_P(letter) ((letter) == MAILBOX_ISRUNNING)

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC_MPMBOX_H) */
