/*	$OpenBSD: clkbrdreg.h,v 1.2 2004/10/01 15:36:30 jason Exp $	*/

/*
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define	CLK_CTRL	0x00
#define	CLK_STS1	0x10
#define	CLK_STS2	0x20
#define	CLK_PSTS1	0x30
#define	CLK_PPRES	0x40
#define	CLK_TEMP	0x50
#define	CLK_IRQ		0x60
#define	CLK_PSTS2	0x70

#define	CLK_CTRL_IEN_FAN	0x80	/* intr enable: fan failure */
#define	CLK_CTRL_IEN_DC		0x40	/* intr enable: pwr supply DC */
#define	CLK_CTRL_IEN_AC		0x20	/* intr enable: AC pwr supply */
#define	CLK_CTRL_IEN_BRD	0x10	/* intr enable: board insert */
#define	CLK_CTRL_POFF		0x08	/* turn off system power */
#define	CLK_CTRL_LLED		0x04	/* left led (reversed) */
#define	CLK_CTRL_MLED		0x02	/* middle led */
#define	CLK_CTRL_RLED		0x01	/* right led */
