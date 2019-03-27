/*-
 * Copyright (c) 1997-2001 Granch, Ltd. All rights reserved.
 * Author: Denis I.Timofeev <timofeev@granch.ru>
 *
 * Redistributon and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * LIABILITY, OR TORT (INCLUDING NEIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * We don't have registered vendor id yet...
 */
#define SBNI_PCI_VENDOR 	0x55 
#define SBNI_PCI_DEVICE 	0x9f

#define ISA_MODE 0x00
#define PCI_MODE 0x01

#define	SBNI_PORTS	4

enum sbni_reg {
	CSR0 = 0,
	CSR1 = 1,
	DAT  = 2
};

/* CSR0 mapping */
enum {
	BU_EMP = 0x02,
	RC_CHK = 0x04,
	CT_ZER = 0x08,
	TR_REQ = 0x10,
	TR_RDY = 0x20,
	EN_INT = 0x40,
	RC_RDY = 0x80
};


/* CSR1 mapping */
#define PR_RES 0x80

struct sbni_csr1 {
	unsigned rxl	: 5;
	unsigned rate	: 2;
	unsigned 	: 1;
};



#define FRAME_ACK_MASK  (u_int16_t)0x7000
#define FRAME_LEN_MASK  (u_int16_t)0x03FF
#define FRAME_FIRST     (u_int16_t)0x8000
#define FRAME_RETRY     (u_int16_t)0x0800

#define FRAME_SENT_BAD  (u_int16_t)0x4000
#define FRAME_SENT_OK   (u_int16_t)0x3000


enum {
	FL_WAIT_ACK    = 1,
	FL_NEED_RESEND = 2,
	FL_PREV_OK     = 4,
	FL_SLOW_MODE   = 8
};


enum {
	DEFAULT_IOBASEADDR = 0x210,
	DEFAULT_INTERRUPTNUMBER = 5,
	DEFAULT_RATE = 0,
	DEFAULT_FRAME_LEN = 1012
};

#define DEF_RXL_DELTA	-1
#define DEF_RXL		0xf

#define SBNI_SIG 0x5a

#define	SBNI_MIN_LEN	(ETHER_MIN_LEN - 4)
#define SBNI_MAX_FRAME	1023

#define SBNI_HZ 18 /* ticks to wait for pong or packet */
		/* sbni watchdog called SBNI_HZ times per sec. */

#define TR_ERROR_COUNT 32
#define CHANGE_LEVEL_START_TICKS 4
