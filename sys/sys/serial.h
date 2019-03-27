/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Poul-Henning Kamp
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file contains definitions which pertain to serial ports as such,
 * (both async and sync), but which do not necessarily have anything to
 * do with tty processing.
 *
 * $FreeBSD$
 */

#ifndef _SYS_SERIAL_H_
#define	_SYS_SERIAL_H_


/*
 * Indentification of modem control signals.  These definitions match
 * the TIOCMGET definitions in <sys/ttycom.h> shifted a bit down, and
 * that identity is enforced with CTASSERT at the bottom of kern/tty.c
 * Both the modem bits and delta bits must fit in 16 bit.
 */
#define	SER_DTR		0x0001		/* data terminal ready */
#define	SER_RTS		0x0002		/* request to send */
#define	SER_STX		0x0004		/* secondary transmit */
#define	SER_SRX		0x0008		/* secondary receive */
#define	SER_CTS		0x0010		/* clear to send */
#define	SER_DCD		0x0020		/* data carrier detect */
#define	SER_RI	 	0x0040		/* ring indicate */
#define	SER_DSR		0x0080		/* data set ready */

#define	SER_MASK_STATE	0x00ff

/* Delta bits, used to indicate which signals should/was affected */
#define	SER_DELTA(x)	((x) << 8)

#define	SER_DDTR	SER_DELTA(SER_DTR)
#define	SER_DRTS	SER_DELTA(SER_RTS)
#define	SER_DSTX	SER_DELTA(SER_STX)
#define	SER_DSRX	SER_DELTA(SER_SRX)
#define	SER_DCTS	SER_DELTA(SER_CTS)
#define	SER_DDCD	SER_DELTA(SER_DCD)
#define	SER_DRI		SER_DELTA(SER_RI)
#define	SER_DDSR	SER_DELTA(SER_DSR)

#define	SER_MASK_DELTA	SER_DELTA(SER_MASK_STATE)

#ifdef _KERNEL
/*
 * Specification of interrupt sources typical for serial ports. These are
 * useful when some umbrella driver like scc(4) has enough knowledge of
 * the hardware to obtain the set of pending interrupts but does not itself
 * handle the interrupt. Each interrupt source can be given an interrupt
 * resource for which inferior drivers can install handlers. The lower 16
 * bits are kept free for the signals above.
 */
#define	SER_INT_OVERRUN	0x010000
#define	SER_INT_BREAK	0x020000
#define	SER_INT_RXREADY	0x040000
#define	SER_INT_SIGCHG	0x080000
#define	SER_INT_TXIDLE	0x100000

#define	SER_INT_MASK	0xff0000
#define	SER_INT_SIGMASK	(SER_MASK_DELTA | SER_MASK_STATE)

#ifndef LOCORE
typedef int serdev_intr_t(void*);
#endif

#endif	/* _KERNEL */

#endif /* !_SYS_SERIAL_H_ */
