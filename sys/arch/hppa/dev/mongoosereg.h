/*	$OpenBSD: mongoosereg.h,v 1.2 2008/08/24 18:53:36 miod Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* EISA Bus Adapter registers definitions */
#define	MONGOOSE_MONGOOSE	0x10000
struct mongoose_regs {
	u_int8_t	version;
	u_int8_t	lock;
	u_int8_t	liowait;
	u_int8_t	clock;
	u_int8_t	reserved[0xf000 - 4];
	u_int8_t	intack;
};

#define	MONGOOSE_CTRL		0x00000
#define	MONGOOSE_NINTS		16
struct mongoose_ctrl {
	struct dma0 {
		struct {
			u_int16_t	addr : 8;
			u_int16_t	count: 8;
		} ch[4];
		u_int8_t	command;
		u_int8_t	request;
		u_int8_t	mask_channel;
		u_int8_t	mode;
		u_int8_t	clr_byte_ptr;
		u_int8_t	master_clear;
		u_int8_t	mask_clear;
		u_int8_t	master_write;
		u_int8_t	pad[15];
	}	dma0;

	u_int8_t	irr0;		/* 0x20 */
	u_int8_t	imr0;
	u_int8_t	iack;		/* 0x22 -- 2 b2b reads generate
					(e)isa Iack cycle & returns int level */
	u_int8_t	pad0[29];

	struct timers {
		u_int8_t	sysclk;
		u_int8_t	refresh;
		u_int8_t	spkr;
		u_int8_t	ctrl;
		u_int32_t	pad;
	}	tmr[2];			/* 0x40 -- timers control */
	u_int8_t	pad1[16];

	u_int16_t	inmi;		/* 0x60 NMI control */
	u_int8_t	pad2[30];
	struct {
		u_int8_t	pad0;
		u_int8_t	ch2;
		u_int8_t	ch3;
		u_int8_t	ch1;
		u_int8_t	pad1;
		u_int8_t	pad2[3];
		u_int8_t	ch0;
		u_int8_t	pad4;
		u_int8_t	ch6;
		u_int8_t	ch7;
		u_int8_t	ch5;
		u_int8_t	pad5[3];
		u_int8_t	pad6[16];
	} pr;				/* 0x80 */

	u_int8_t	irr1;		/* 0xa0 */
	u_int8_t	imr1;
	u_int8_t	pad3[30];

	struct dma1 {
		struct {
			u_int32_t	addr : 16;
			u_int32_t	count: 16;
		} ch[4];
		u_int16_t	command;
		u_int16_t	request;
		u_int16_t	mask_channel;
		u_int16_t	mode;
		u_int16_t	clr_byte_ptr;
		u_int16_t	master_clear;
		u_int16_t	mask_clear;
		u_int16_t	master_write;
	}	dma1;			/* 0xc0 */

	u_int8_t	master_req;	/* 0xe0 master request register */
	u_int8_t	pad4[31];

	u_int8_t	pad5[0x3d0];	/* 0x4d0 */
	u_int8_t	pic0;		/* 0 - edge, 1 - level */
	u_int8_t	pic1;
	u_int8_t	pad6[0x460];
	u_int8_t	nmi;
	u_int8_t	nmi_ext;
#define	MONGOOSE_NMI_BUSRESET	0x01
#define	MONGOOSE_NMI_IOPORT_EN	0x02
#define	MONGOOSE_NMI_EN		0x04
#define	MONGOOSE_NMI_MTMO_EN	0x08
#define	MONGOOSE_NMI_RES4	0x10
#define	MONGOOSE_NMI_IOPORT_INT	0x20
#define	MONGOOSE_NMI_MASTER_INT	0x40
#define	MONGOOSE_NMI_INT	0x80
};

#define	MONGOOSE_IOMAP	0x100000
