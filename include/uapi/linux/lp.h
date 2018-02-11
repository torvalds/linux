/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * usr/include/linux/lp.h c.1991-1992 James Wiegand
 * many modifications copyright (C) 1992 Michael K. Johnson
 * Interrupt support added 1993 Nigel Gamble
 * Removed 8255 status defines from inside __KERNEL__ Marcelo Tosatti 
 */
#ifndef _UAPI_LINUX_LP_H
#define _UAPI_LINUX_LP_H


/*
 * Per POSIX guidelines, this module reserves the LP and lp prefixes
 * These are the lp_table[minor].flags flags...
 */
#define LP_EXIST 0x0001
#define LP_SELEC 0x0002
#define LP_BUSY	 0x0004
#define LP_BUSY_BIT_POS 2
#define LP_OFFL	 0x0008
#define LP_NOPA  0x0010
#define LP_ERR   0x0020
#define LP_ABORT 0x0040
#define LP_CAREFUL 0x0080 /* obsoleted -arca */
#define LP_ABORTOPEN 0x0100

#define LP_TRUST_IRQ_  0x0200 /* obsolete */
#define LP_NO_REVERSE  0x0400 /* No reverse mode available. */
#define LP_DATA_AVAIL  0x0800 /* Data is available. */

/* 
 * bit defines for 8255 status port
 * base + 1
 * accessed with LP_S(minor), which gets the byte...
 */
#define LP_PBUSY	0x80  /* inverted input, active high */
#define LP_PACK		0x40  /* unchanged input, active low */
#define LP_POUTPA	0x20  /* unchanged input, active high */
#define LP_PSELECD	0x10  /* unchanged input, active high */
#define LP_PERRORP	0x08  /* unchanged input, active low */

/* timeout for each character.  This is relative to bus cycles -- it
 * is the count in a busy loop.  THIS IS THE VALUE TO CHANGE if you
 * have extremely slow printing, or if the machine seems to slow down
 * a lot when you print.  If you have slow printing, increase this
 * number and recompile, and if your system gets bogged down, decrease
 * this number.  This can be changed with the tunelp(8) command as well.
 */

#define LP_INIT_CHAR 1000

/* The parallel port specs apparently say that there needs to be
 * a .5usec wait before and after the strobe.
 */

#define LP_INIT_WAIT 1

/* This is the amount of time that the driver waits for the printer to
 * catch up when the printer's buffer appears to be filled.  If you
 * want to tune this and have a fast printer (i.e. HPIIIP), decrease
 * this number, and if you have a slow printer, increase this number.
 * This is in hundredths of a second, the default 2 being .05 second.
 * Or use the tunelp(8) command, which is especially nice if you want
 * change back and forth between character and graphics printing, which
 * are wildly different...
 */

#define LP_INIT_TIME 2

/* IOCTL numbers */
#define LPCHAR   0x0601  /* corresponds to LP_INIT_CHAR */
#define LPTIME   0x0602  /* corresponds to LP_INIT_TIME */
#define LPABORT  0x0604  /* call with TRUE arg to abort on error,
			    FALSE to retry.  Default is retry.  */
#define LPSETIRQ 0x0605  /* call with new IRQ number,
			    or 0 for polling (no IRQ) */
#define LPGETIRQ 0x0606  /* get the current IRQ number */
#define LPWAIT   0x0608  /* corresponds to LP_INIT_WAIT */
/* NOTE: LPCAREFUL is obsoleted and it' s always the default right now -arca */
#define LPCAREFUL   0x0609  /* call with TRUE arg to require out-of-paper, off-
			    line, and error indicators good on all writes,
			    FALSE to ignore them.  Default is ignore. */
#define LPABORTOPEN 0x060a  /* call with TRUE arg to abort open() on error,
			    FALSE to ignore error.  Default is ignore.  */
#define LPGETSTATUS 0x060b  /* return LP_S(minor) */
#define LPRESET     0x060c  /* reset printer */
#ifdef LP_STATS
#define LPGETSTATS  0x060d  /* get statistics (struct lp_stats) */
#endif
#define LPGETFLAGS  0x060e  /* get status flags */
#define LPSETTIMEOUT 0x060f /* set parport timeout */

/* timeout for printk'ing a timeout, in jiffies (100ths of a second).
   This is also used for re-checking error conditions if LP_ABORT is
   not set.  This is the default behavior. */

#define LP_TIMEOUT_INTERRUPT	(60 * HZ)
#define LP_TIMEOUT_POLLED	(10 * HZ)


#endif /* _UAPI_LINUX_LP_H */
