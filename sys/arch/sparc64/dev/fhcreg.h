/*	$OpenBSD: fhcreg.h,v 1.4 2007/05/01 19:44:56 kettenis Exp $	*/

/*
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
 */

#define	FHC_P_ID	0x00000000		/* ID */
#define	FHC_P_RCS	0x00000010		/* reset ctrl/status */
#define	FHC_P_CTRL	0x00000020		/* control */
#define	FHC_P_BSR	0x00000030		/* board status */
#define	FHC_P_ECC	0x00000040		/* ECC control */
#define	FHC_P_JCTRL	0x000000f0		/* JTAG control */

#define	FHC_P_CTRL_ICS		0x00100000	/* ignore centerplane sigs */
#define	FHC_P_CTRL_FRST		0x00080000	/* fatal error reset enable */
#define	FHC_P_CTRL_LFAT		0x00080000	/* AC/DC local error */
#define	FHC_P_CTRL_SLINE	0x00010000	/* firmware sync line */
#define	FHC_P_CTRL_DCD		0x00008000	/* DC/DC converter disable */
#define	FHC_P_CTRL_POFF		0x00004000	/* AC/DC ctlr PLL disable */
#define	FHC_P_CTRL_FOFF		0x00002000	/* FHC ctlr PLL disable */
#define	FHC_P_CTRL_AOFF		0x00001000	/* cpu a sram low pwr mode */
#define	FHC_P_CTRL_BOFF		0x00000800	/* cpu b sram low pwr mode */
#define	FHC_P_CTRL_PSOFF	0x00000400	/* disable fhc power supply */
#define	FHC_P_CTRL_IXIST	0x00000200	/* fhc notifies clock-board */
#define	FHC_P_CTRL_XMSTR	0x00000100	/* xir master enable */
#define	FHC_P_CTRL_LLED		0x00000040	/* left led (reversed) */
#define	FHC_P_CTRL_MLED		0x00000020	/* middle led */
#define	FHC_P_CTRL_RLED		0x00000010	/* right led */
#define	FHC_P_CTRL_BPINS	0x00000003	/* spare bidir pins */

#define	FHC_I_IGN	0x00000000		/* IGN register */

#define	FHC_F_IMAP	0x00000000		/* fanfail intr map */
#define	FHC_F_ICLR	0x00000010		/* fanfail intr clr */

#define	FHC_S_IMAP	0x00000000		/* system intr map */
#define	FHC_S_ICLR	0x00000010		/* system intr clr */

#define	FHC_U_IMAP	0x00000000		/* uart intr map */
#define	FHC_U_ICLR	0x00000010		/* uart intr clr */

#define	FHC_T_IMAP	0x00000000		/* tod intr map */
#define	FHC_T_ICLR	0x00000010		/* tod intr clr */

struct fhc_intr_reg {
	u_int64_t imap;
	u_int64_t unused_0;
	u_int64_t iclr;
	u_int64_t unused_1;
};

#define FHC_INO(ino)	((ino) & 0x7)
#define FHC_S_INO	0
#define FHC_U_INO	1
#define FHC_T_INO	2
#define FHC_F_INO	3
