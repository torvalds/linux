/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 1999 Nicolas Souchu
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
 * $FreeBSD$
 *
 */
#ifndef __PPBCONF_H
#define __PPBCONF_H

#define n(flags) (~(flags) & (flags))

/*
 * Parallel Port Chipset control bits.
 */
#define STROBE		0x01
#define AUTOFEED	0x02
#define nINIT		0x04
#define SELECTIN	0x08
#define IRQENABLE	0x10
#define PCD		0x20

#define nSTROBE		n(STROBE)
#define nAUTOFEED	n(AUTOFEED)
#define INIT		n(nINIT)
#define nSELECTIN	n(SELECTIN)
#define nPCD		n(PCD)

/*
 * Parallel Port Chipset status bits.
 */
#define TIMEOUT		0x01
#define nFAULT		0x08
#define SELECT		0x10
#define PERROR		0x20
#define nACK		0x40
#define nBUSY		0x80

#ifdef _KERNEL
#include <sys/queue.h>

/*
 * Parallel Port Bus sleep/wakeup queue.
 */
#define PPBPRI	(PZERO+8)

/*
 * Parallel Port Chipset mode masks.
 * NIBBLE mode is supposed to be available under each other modes.
 */
#define PPB_COMPATIBLE	0x0	/* Centronics compatible mode */

#define PPB_NIBBLE	0x1	/* reverse 4 bit mode */
#define PPB_PS2		0x2	/* PS/2 byte mode */
#define PPB_EPP		0x4	/* EPP mode, 32 bit */
#define PPB_ECP		0x8	/* ECP mode */

/* mode aliases */
#define PPB_SPP		PPB_NIBBLE|PPB_PS2
#define PPB_BYTE	PPB_PS2

#define PPB_MASK		0x0f
#define PPB_OPTIONS_MASK	0xf0

#define PPB_IS_EPP(mode) (mode & PPB_EPP)
#define PPB_IN_EPP_MODE(bus) (PPB_IS_EPP (ppb_get_mode (bus)))
#define PPB_IN_NIBBLE_MODE(bus) (ppb_get_mode (bus) & PPB_NIBBLE)
#define PPB_IN_PS2_MODE(bus) (ppb_get_mode (bus) & PPB_PS2)

/*
 * Structure to store status information.
 */
struct ppb_status {
	unsigned char status;

	unsigned int timeout:1;
	unsigned int error:1;
	unsigned int select:1;
	unsigned int paper_end:1;
	unsigned int ack:1;
	unsigned int busy:1;
};

/* Parallel port bus I/O opcodes */
#define PPB_OUTSB_EPP	1
#define PPB_OUTSW_EPP	2
#define PPB_OUTSL_EPP	3
#define PPB_INSB_EPP	4
#define PPB_INSW_EPP	5
#define PPB_INSL_EPP	6
#define PPB_RDTR	7
#define PPB_RSTR	8
#define PPB_RCTR	9
#define PPB_REPP_A	10
#define PPB_REPP_D	11
#define PPB_RECR	12
#define PPB_RFIFO	13
#define PPB_WDTR	14
#define PPB_WSTR	15
#define PPB_WCTR	16
#define PPB_WEPP_A	17
#define PPB_WEPP_D	18
#define PPB_WECR	19
#define PPB_WFIFO	20

/*
 * How tsleep() is called in ppb_request_bus().
 */
#define PPB_DONTWAIT	0
#define PPB_NOINTR	0
#define PPB_WAIT	0x1
#define PPB_INTR	0x2
#define PPB_POLL	0x4
#define PPB_FOREVER	-1

/*
 * Microsequence stuff.
 */
#define PPB_MS_MAXLEN	64		/* XXX according to MS_INS_MASK */
#define PPB_MS_MAXARGS	3		/* according to MS_ARG_MASK */

/* maximum number of mode dependent
 * submicrosequences for in/out operations
 */
#define PPB_MAX_XFER	6

union ppb_insarg {
	int	i;
	void	*p;
	char	*c;
	int	(* f)(void *, char *);
};

struct ppb_microseq {
	int			opcode;			/* microins. opcode */
	union ppb_insarg	arg[PPB_MS_MAXARGS];	/* arguments */
};

/* microseqences used for GET/PUT operations */
struct ppb_xfer {
	struct ppb_microseq *loop;		/* the loop microsequence */
};

/*
 * Parallel Port Bus Device structure.
 */
struct ppb_data;			/* see below */

struct ppb_context {
	int valid;			/* 1 if the struct is valid */
	int mode;			/* XXX chipset operating mode */

	struct microseq *curpc;		/* pc in curmsq */
	struct microseq *curmsq;	/* currently executed microseqence */
};

/*
 * List of IVARS available to ppb device drivers
 */
#define PPBUS_IVAR_MODE 0

/* other fields are reserved to the ppbus internals */

struct ppb_device {

	const char *name;		/* name of the device */

	u_int flags;			/* flags */

	struct ppb_context ctx;		/* context of the device */

					/* mode dependent get msq. If NULL,
					 * IEEE1284 code is used */
	struct ppb_xfer
		get_xfer[PPB_MAX_XFER];

					/* mode dependent put msq. If NULL,
					 * IEEE1284 code is used */
	struct ppb_xfer
		put_xfer[PPB_MAX_XFER];

	driver_intr_t *intr_hook;
	void *intr_arg;
};

/* EPP standards */
#define EPP_1_9		0x0			/* default */
#define EPP_1_7		0x1

/* Parallel Port Chipset IVARS */		/* elsewhere XXX */
#define PPC_IVAR_EPP_PROTO	0
#define PPC_IVAR_LOCK		1
#define PPC_IVAR_INTR_HANDLER	2

/*
 * Maximum size of the PnP info string
 */
#define PPB_PnP_STRING_SIZE	256			/* XXX */

/*
 * Parallel Port Bus structure.
 */
struct ppb_data {

#define PPB_PnP_PRINTER	0
#define PPB_PnP_MODEM	1
#define PPB_PnP_NET	2
#define PPB_PnP_HDC	3
#define PPB_PnP_PCMCIA	4
#define PPB_PnP_MEDIA	5
#define PPB_PnP_FDC	6
#define PPB_PnP_PORTS	7
#define PPB_PnP_SCANNER	8
#define PPB_PnP_DIGICAM	9
#define PPB_PnP_UNKNOWN	10
	int class_id;		/* not a PnP device if class_id < 0 */

	int state;		/* current IEEE1284 state */
	int error;		/* last IEEE1284 error */

	int mode;		/* IEEE 1284-1994 mode
				 * NIBBLE, PS2, EPP or ECP */

	device_t ppb_owner;	/* device which owns the bus */

	struct mtx *ppc_lock;	/* lock of parent device */
	struct resource *ppc_irq_res;
};

struct callout;

typedef int (*ppc_intr_handler)(void *);

extern int ppb_attach_device(device_t);
extern int ppb_request_bus(device_t, device_t, int);
extern int ppb_release_bus(device_t, device_t);

/* bus related functions */
extern void ppb_lock(device_t);
extern void ppb_unlock(device_t);
extern void _ppb_assert_locked(device_t, const char *, int);
extern void ppb_init_callout(device_t, struct callout *, int);
extern int ppb_sleep(device_t, void *, int, const char *, int);
extern int ppb_get_status(device_t, struct ppb_status *);
extern int ppb_poll_bus(device_t, int, uint8_t, uint8_t, int);
extern int ppb_reset_epp_timeout(device_t);
extern int ppb_ecp_sync(device_t);
extern int ppb_get_epp_protocol(device_t);
extern int ppb_set_mode(device_t, int);		/* returns old mode */
extern int ppb_get_mode(device_t);		/* returns current mode */
extern int ppb_write(device_t, char *, int, int);

#ifdef INVARIANTS
#define ppb_assert_locked(dev)	_ppb_assert_locked(dev, __FILE__, __LINE__)
#else
#define ppb_assert_locked(dev)
#endif
#endif /* _KERNEL */

#endif /* !__PPBCONF_H */
