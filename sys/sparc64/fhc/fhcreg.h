/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause
 *
 * Copyright (c) 2003 Jake Burkholder.
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
 */
/*-
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net).
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
 *
 *	from: OpenBSD: fhcreg.h,v 1.3 2004/09/28 16:26:03 jason Exp
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_FHC_FHCREG_H_
#define	_SPARC64_FHC_FHCREG_H_

#define	FHC_NREG	(6)

#define	FHC_INTERNAL	(0)
#define	FHC_IGN		(1)
#define	FHC_FANFAIL	(2)
#define	FHC_SYSTEM	(3)
#define	FHC_UART	(4)
#define	FHC_TOD		(5)

#define	FHC_IMAP	0x0
#define	FHC_ICLR	0x10

#define	FHC_ID		0x00000000	/* ID */
#define	FHC_RCS		0x00000010	/* reset ctrl/status */
#define	FHC_CTRL	0x00000020	/* control */
#define	FHC_BSR		0x00000030	/* board status */
#define	FHC_ECC		0x00000040	/* ECC control */
#define	FHC_JCTRL	0x000000f0	/* JTAG control */

#define	FHC_CTRL_ICS	0x00100000	/* ignore centerplane sigs */
#define	FHC_CTRL_FRST	0x00080000	/* fatal error reset enable */
#define	FHC_CTRL_LFAT	0x00040000	/* AC/DC local error */
#define	FHC_CTRL_SLINE	0x00010000	/* firmware sync line */
#define	FHC_CTRL_DCD	0x00008000	/* DC/DC converter disable */
#define	FHC_CTRL_POFF	0x00004000	/* AC/DC ctlr PLL disable */
#define	FHC_CTRL_FOFF	0x00002000	/* FHC ctlr PLL disable */
#define	FHC_CTRL_AOFF	0x00001000	/* cpu a sram low pwr mode */
#define	FHC_CTRL_BOFF	0x00000800	/* cpu b sram low pwr mode */
#define	FHC_CTRL_PSOFF	0x00000400	/* disable fhc power supply */
#define	FHC_CTRL_IXIST	0x00000200	/* fhc notifies clock-board */
#define	FHC_CTRL_XMSTR	0x00000100	/* xir master enable */
#define	FHC_CTRL_LLED	0x00000040	/* left led (reversed) */
#define	FHC_CTRL_MLED	0x00000020	/* middle led */
#define	FHC_CTRL_RLED	0x00000010	/* right led */
#define	FHC_CTRL_BPINS	0x00000003	/* spare bidir pins */

#endif /* !_SPARC64_FHC_FHCREG_H_ */
