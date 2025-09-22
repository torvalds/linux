/*	$OpenBSD: scifcons.c,v 1.6 2014/07/17 13:14:06 miod Exp $	*/
/*	$NetBSD: scifcons.c,v 1.1 2006/09/01 21:26:18 uwe Exp $ */
/*	NetBSD: scif.c,v 1.38 2004/12/13 02:14:13 chs Exp */

/*-
 * Copyright (C) 1999 T.Horiuchi and SAITOH Masanobu.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)com.c	7.5 (Berkeley) 5/16/91
 */
/*
 * SH internal serial driver
 *
 * This code is derived from both z8530tty.c and com.c
 */

#include <libsa.h>
#include <dev/cons.h>

#include <arch/sh/dev/scifreg.h>

#define scif_smr_read()		SHREG_SCSMR2
#define scif_smr_write(v)	(SHREG_SCSMR2 = (v))

#define scif_brr_read()		SHREG_SCBRR2
#define scif_brr_write(v)	(SHREG_SCBRR2 = (v))

#define scif_scr_read()		SHREG_SCSCR2
#define scif_scr_write(v)	(SHREG_SCSCR2 = (v))

#define scif_ftdr_write(v)	(SHREG_SCFTDR2 = (v))

#define scif_ssr_read()		SHREG_SCSSR2
#define scif_ssr_write(v)	(SHREG_SCSSR2 = (v))

#define scif_frdr_read()	SHREG_SCFRDR2

#define scif_fcr_read()		SHREG_SCFCR2
#define scif_fcr_write(v)	(SHREG_SCFCR2 = (v))

#define scif_fdr_read()		SHREG_SCFDR2

#define scif_sptr_read()	SHREG_SCSPTR2
#define scif_sptr_write(v)	(SHREG_SCSPTR2 = (v))

#define scif_lsr_read()		SHREG_SCLSR2
#define scif_lsr_write(v)	(SHREG_SCLSR2 = (v))

#define	divrnd(n, q)	(((n)*2/(q)+1)/2)	/* divide and round off */

#define	SERBUFSIZE	16
static u_char serbuf[SERBUFSIZE];
static int serbuf_read = 0;
static int serbuf_write = 0;

void
scif_init(unsigned int bps)
{

	serbuf_read = 0;
	serbuf_write = 0;

	/* Initialize SCR */
	scif_scr_write(0x00);

	/* Clear FIFO */
	scif_fcr_write(SCFCR2_TFRST | SCFCR2_RFRST);

	/* Serial Mode Register */
	scif_smr_write(0x00);	/* 8bit,NonParity,Even,1Stop */

	/* Bit Rate Register */
	scif_brr_write(divrnd(PCLOCK, 32 * bps) - 1);

	delay(100);

	scif_sptr_write(SCSPTR2_RTSIO);

	scif_fcr_write(FIFO_RCV_TRIGGER_1 | FIFO_XMT_TRIGGER_8);

	/* Send permission, Receive permission ON */
	scif_scr_write(SCSCR2_TE | SCSCR2_RE);

	/* Serial Status Register */
	scif_ssr_write(scif_ssr_read() & SCSSR2_TDFE); /* Clear Status */
}

void
scif_cnprobe(struct consdev *cn)
{
	cn->cn_pri = CN_HIGHPRI;
}

void
scif_cninit(struct consdev *cn)
{
	scif_init(9600);
}

int
scif_cngetc(dev_t dev)
{
	unsigned char c, err_c;
	unsigned short err_c2;

	if (serbuf_read != serbuf_write) {
		if (dev & 0x80)
			return 1;
		c = serbuf[serbuf_read];
		serbuf_read = (serbuf_read + 1) % SERBUFSIZE;
		return (c);
	}

	for (;;) {
		/* wait for ready */
		if ((scif_fdr_read() & SCFDR2_RECVCNT) != 0) {
			c = scif_frdr_read();
			err_c = scif_ssr_read();
			scif_ssr_write(scif_ssr_read() &
			    ~(SCSSR2_ER | SCSSR2_BRK | SCSSR2_RDF | SCSSR2_DR));

			err_c2 = scif_lsr_read();
			scif_lsr_write(scif_lsr_read() & ~SCLSR2_ORER);

			if ((err_c & (SCSSR2_ER | SCSSR2_BRK | SCSSR2_FER |
			    SCSSR2_PER)) == 0) {
				if ((err_c2 & SCLSR2_ORER) == 0) {
					if ((dev & 0x80) == 0)
						return (c);
					/* stuff char into preread buffer */
					serbuf[serbuf_write] = (u_char)c;
					serbuf_write = (serbuf_write + 1) %
					    SERBUFSIZE;
					return (1);
				}
			}
		}
		if (dev & 0x80)
			return 0;
	}
}

void
scif_cnputc(dev_t dev, int c)
{

	/* wait for ready */
	while ((scif_fdr_read() & SCFDR2_TXCNT) == SCFDR2_TXF_FULL)
		continue;

	/* write send data to send register */
	scif_ftdr_write(c);

	/* clear ready flag */
	scif_ssr_write(scif_ssr_read() & ~(SCSSR2_TDFE | SCSSR2_TEND));
}
