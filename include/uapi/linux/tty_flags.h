/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_TTY_FLAGS_H
#define _LINUX_TTY_FLAGS_H

/*
 * Definitions for async_struct (and serial_struct) flags field also
 * shared by the tty_port flags structures.
 *
 * Define ASYNCB_* for convenient use with {test,set,clear}_bit.
 *
 * Bits [0..ASYNCB_LAST_USER] are userspace defined/visible/changeable
 * [x] in the bit comments indicates the flag is defunct and no longer used.
 */
#define ASYNCB_HUP_NOTIFY	 0 /* Notify getty on hangups and closes
				    * on the callout port */
#define ASYNCB_FOURPORT		 1 /* Set OUT1, OUT2 per AST Fourport settings */
#define ASYNCB_SAK		 2 /* Secure Attention Key (Orange book) */
#define ASYNCB_SPLIT_TERMIOS	 3 /* [x] Separate termios for dialin/callout */
#define ASYNCB_SPD_HI		 4 /* Use 57600 instead of 38400 bps */
#define ASYNCB_SPD_VHI		 5 /* Use 115200 instead of 38400 bps */
#define ASYNCB_SKIP_TEST	 6 /* Skip UART test during autoconfiguration */
#define ASYNCB_AUTO_IRQ		 7 /* Do automatic IRQ during
				    * autoconfiguration */
#define ASYNCB_SESSION_LOCKOUT	 8 /* [x] Lock out cua opens based on session */
#define ASYNCB_PGRP_LOCKOUT	 9 /* [x] Lock out cua opens based on pgrp */
#define ASYNCB_CALLOUT_NOHUP	10 /* [x] Don't do hangups for cua device */
#define ASYNCB_HARDPPS_CD	11 /* Call hardpps when CD goes high  */
#define ASYNCB_SPD_SHI		12 /* Use 230400 instead of 38400 bps */
#define ASYNCB_LOW_LATENCY	13 /* Request low latency behaviour */
#define ASYNCB_BUGGY_UART	14 /* This is a buggy UART, skip some safety
				    * checks.  Note: can be dangerous! */
#define ASYNCB_AUTOPROBE	15 /* [x] Port was autoprobed by PCI/PNP code */
#define ASYNCB_MAGIC_MULTIPLIER	16 /* Use special CLK or divisor */
#define ASYNCB_LAST_USER	16

/*
 * Internal flags used only by kernel (read-only)
 *
 * WARNING: These flags are no longer used and have been superceded by the
 *	    TTY_PORT_ flags in the iflags field (and not userspace-visible)
 */
#ifndef _KERNEL_
#define ASYNCB_INITIALIZED	31 /* Serial port was initialized */
#define ASYNCB_SUSPENDED	30 /* Serial port is suspended */
#define ASYNCB_NORMAL_ACTIVE	29 /* Normal device is active */
#define ASYNCB_BOOT_AUTOCONF	28 /* Autoconfigure port on bootup */
#define ASYNCB_CLOSING		27 /* Serial port is closing */
#define ASYNCB_CTS_FLOW		26 /* Do CTS flow control */
#define ASYNCB_CHECK_CD		25 /* i.e., CLOCAL */
#define ASYNCB_SHARE_IRQ	24 /* for multifunction cards, no longer used */
#define ASYNCB_CONS_FLOW	23 /* flow control for console  */
#define ASYNCB_FIRST_KERNEL	22
#endif

/* Masks */
#define ASYNC_HUP_NOTIFY	(1U << ASYNCB_HUP_NOTIFY)
#define ASYNC_SUSPENDED		(1U << ASYNCB_SUSPENDED)
#define ASYNC_FOURPORT		(1U << ASYNCB_FOURPORT)
#define ASYNC_SAK		(1U << ASYNCB_SAK)
#define ASYNC_SPLIT_TERMIOS	(1U << ASYNCB_SPLIT_TERMIOS)
#define ASYNC_SPD_HI		(1U << ASYNCB_SPD_HI)
#define ASYNC_SPD_VHI		(1U << ASYNCB_SPD_VHI)
#define ASYNC_SKIP_TEST		(1U << ASYNCB_SKIP_TEST)
#define ASYNC_AUTO_IRQ		(1U << ASYNCB_AUTO_IRQ)
#define ASYNC_SESSION_LOCKOUT	(1U << ASYNCB_SESSION_LOCKOUT)
#define ASYNC_PGRP_LOCKOUT	(1U << ASYNCB_PGRP_LOCKOUT)
#define ASYNC_CALLOUT_NOHUP	(1U << ASYNCB_CALLOUT_NOHUP)
#define ASYNC_HARDPPS_CD	(1U << ASYNCB_HARDPPS_CD)
#define ASYNC_SPD_SHI		(1U << ASYNCB_SPD_SHI)
#define ASYNC_LOW_LATENCY	(1U << ASYNCB_LOW_LATENCY)
#define ASYNC_BUGGY_UART	(1U << ASYNCB_BUGGY_UART)
#define ASYNC_AUTOPROBE		(1U << ASYNCB_AUTOPROBE)
#define ASYNC_MAGIC_MULTIPLIER	(1U << ASYNCB_MAGIC_MULTIPLIER)

#define ASYNC_FLAGS		((1U << (ASYNCB_LAST_USER + 1)) - 1)
#define ASYNC_DEPRECATED	(ASYNC_SESSION_LOCKOUT | ASYNC_PGRP_LOCKOUT | \
		ASYNC_CALLOUT_NOHUP | ASYNC_AUTOPROBE)
#define ASYNC_USR_MASK		(ASYNC_SPD_MASK|ASYNC_CALLOUT_NOHUP| \
		ASYNC_LOW_LATENCY)
#define ASYNC_SPD_CUST		(ASYNC_SPD_HI|ASYNC_SPD_VHI)
#define ASYNC_SPD_WARP		(ASYNC_SPD_HI|ASYNC_SPD_SHI)
#define ASYNC_SPD_MASK		(ASYNC_SPD_HI|ASYNC_SPD_VHI|ASYNC_SPD_SHI)

#ifndef _KERNEL_
/* These flags are no longer used (and were always masked from userspace) */
#define ASYNC_INITIALIZED	(1U << ASYNCB_INITIALIZED)
#define ASYNC_NORMAL_ACTIVE	(1U << ASYNCB_NORMAL_ACTIVE)
#define ASYNC_BOOT_AUTOCONF	(1U << ASYNCB_BOOT_AUTOCONF)
#define ASYNC_CLOSING		(1U << ASYNCB_CLOSING)
#define ASYNC_CTS_FLOW		(1U << ASYNCB_CTS_FLOW)
#define ASYNC_CHECK_CD		(1U << ASYNCB_CHECK_CD)
#define ASYNC_SHARE_IRQ		(1U << ASYNCB_SHARE_IRQ)
#define ASYNC_CONS_FLOW		(1U << ASYNCB_CONS_FLOW)
#define ASYNC_INTERNAL_FLAGS	(~((1U << ASYNCB_FIRST_KERNEL) - 1))
#endif

#endif
