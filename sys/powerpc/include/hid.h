/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000 Tsubai Masanari.  All rights reserved.
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
 *
 * $NetBSD: hid.h,v 1.2 2001/08/22 21:05:25 matt Exp $
 * $FreeBSD$
 */

#ifndef _POWERPC_HID_H_
#define _POWERPC_HID_H_

/* Hardware Implementation Dependent registers for the PowerPC */
#define	HID0_RADIX	0x0080000000000000	/* Enable Radix page tables (POWER9) */

#define HID0_EMCP	0x80000000  /* Enable machine check pin */
#define HID0_DBP	0x40000000  /* Disable 60x bus parity generation */
#define HID0_EBA	0x20000000  /* Enable 60x bus address parity checking */
#define HID0_EBD	0x10000000  /* Enable 60x bus data parity checking */
#define HID0_BCLK	0x08000000  /* CLK_OUT clock type selection */
#define HID0_EICE	0x04000000  /* Enable ICE output */
#define HID0_ECLK	0x02000000  /* CLK_OUT clock type selection */
#define HID0_PAR	0x01000000  /* Disable precharge of ARTRY */
#define HID0_STEN	0x01000000  /* Software table search enable (7450) */
#define HID0_DEEPNAP	0x01000000  /* Enable deep nap mode (970) */
#define HID0_HBATEN	0x00800000  /* High BAT enable (74[45][578])  */
#define HID0_DOZE	0x00800000  /* Enable doze mode */
#define HID0_NAP	0x00400000  /* Enable nap mode */
#define HID0_SLEEP	0x00200000  /* Enable sleep mode */
#define HID0_DPM	0x00100000  /* Enable Dynamic power management */
#define HID0_RISEG	0x00080000  /* Read I-SEG */
#define HID0_TG		0x00040000  /* Timebase Granularity (OEA64) */
#define HID0_BHTCLR	0x00040000  /* Clear branch history table (7450) */
#define HID0_EIEC	0x00040000  /* Enable internal error checking */
#define HID0_XAEN	0x00020000  /* Enable eXtended Addressing (7450) */
#define HID0_NHR	0x00010000  /* Not hard reset */
#define HID0_ICE	0x00008000  /* Enable i-cache */
#define HID0_DCE	0x00004000  /* Enable d-cache */
#define HID0_ILOCK	0x00002000  /* i-cache lock */
#define HID0_DLOCK	0x00001000  /* d-cache lock */
#define HID0_ICFI	0x00000800  /* i-cache flush invalidate */
#define HID0_DCFI	0x00000400  /* d-cache flush invalidate */
#define HID0_SPD	0x00000200  /* Disable speculative cache access */
#define HID0_XBSEN	0x00000100  /* Extended BAT block-size enable (7457) */
#define HID0_IFEM	0x00000100  /* Enable M-bit for I-fetch */
#define HID0_XBSEN	0x00000100  /* Extended BAT block size enable (7455+)*/
#define HID0_SGE	0x00000080  /* Enable store gathering */
#define HID0_DCFA	0x00000040  /* Data cache flush assist */
#define HID0_BTIC	0x00000020  /* Enable BTIC */
#define HID0_LRSTK	0x00000010  /* Link register stack enable (7450) */
#define HID0_ABE	0x00000008  /* Enable address broadcast */
#define HID0_FOLD	0x00000008  /* Branch folding enable (7450) */
#define HID0_BHT	0x00000004  /* Enable branch history table */
#define HID0_NOPTI	0x00000001  /* No-op the dcbt(st) */

#define HID0_AIM_TBEN	0x04000000  /* Time base enable (7450) */

#define HID0_E500_TBEN		0x00004000 /* Time Base and decr. enable */
#define HID0_E500_SEL_TBCLK	0x00002000 /* Select Time Base clock */
#define HID0_E500_MAS7UPDEN	0x00000080 /* Enable MAS7 update (e500v2) */

#define HID0_E500MC_L2MMU_MHD	0x40000000 /* L2MMU Multiple Hit Detection */

#define HID0_BITMASK							\
    "\20"								\
    "\040EMCP\037DBP\036EBA\035EBD\034BCLK\033EICE\032ECLK\031PAR"	\
    "\030DOZE\027NAP\026SLEEP\025DPM\024RISEG\023EIEC\022res\021NHR"	\
    "\020ICE\017DCE\016ILOCK\015DLOCK\014ICFI\013DCFI\012SPD\011IFEM"	\
    "\010SGE\007DCFA\006BTIC\005FBIOB\004ABE\003BHT\002NOPDST\001NOPTI"

#define HID0_7450_BITMASK						\
    "\20"								\
    "\040EMCP\037b1\036b2\035b3\034b4\033TBEN\032b6\031STEN"		\
    "\030HBATEN\027NAP\026SLEEP\025DPM\024b12\023BHTCLR\022XAEN\021NHR"	\
    "\020ICE\017DCE\016ILOCK\015DLOCK\014ICFI\013DCFI\012SPD\011XBSEN"	\
    "\010SGE\007b25\006BTIC\005LRSTK\004FOLD\003BHT\002NOPDST\001NOPTI"

#define HID0_E500_BITMASK						\
    "\20"								\
    "\040EMCP\037b1\036b2\035b3\034b4\033b5\032b6\031b7"		\
    "\030DOZE\027NAP\026SLEEP\025b11\024b12\023b13\022b14\021b15"	\
    "\020b16\017TBEN\016SEL_TBCLK\015b19\014b20\013b21\012b22\011b23"	\
    "\010EN_MAS7_UPDATE\007DCFA\006b26\005b27\004b28\003b29\002b30\001NOPTI"

#define HID0_970_BITMASK						\
    "\20"								\
    "\040ONEPPC\037SINGLE\036ISYNCSC\035SERGP\031DEEPNAP\030DOZE"	\
    "\027NAP\025DPM\023TG\022HANGDETECT\021NHR\020INORDER"		\
    "\016TBCTRL\015TBEN\012CIABREN\011HDICEEN\001ENATTN"		

#define HID0_E500MC_BITMASK						\
    "\20"								\
    "\040EMCP\037EN_L2MMU_MHD\036b2\035b3\034b4\033b5\032b6\031b7"	\
    "\030b8\027b9\026b10\025b11\024b12\023b13\022b14\021b15"		\
    "\020b16\017b17\016b18\015b19\014b20\013b21\012b22\011b23"		\
    "\010EN_MAS7_UPDATE\007DCFA\006b26\005CIGLSO\004b28\003b29\002b30\001NOPTI"

#define HID0_E5500_BITMASK						\
    "\20"								\
    "\040EMCP\037EN_L2MMU_MHD\036b2\035b3\034b4\033b5\032b6\031b7"	\
    "\030b8\027b9\026b10\025b11\024b12\023b13\022b14\021b15"		\
    "\020b16\017b17\016b18\015b19\014b20\013b21\012b22\011b23"		\
    "\010b24\007DCFA\006b26\005CIGLSO\004b28\003b29\002b30\001NOPTI"

/*
 *  HID0 bit definitions per cpu model
 *
 * bit	603	604	750	7400	7410	7450	7457	e500
 *   0	EMCP	EMCP	EMCP	EMCP	EMCP	-	-	EMCP
 *   1	-	ECP	DBP	-	-	-	-	-
 *   2	EBA	EBA	EBA	EBA	EDA	-	-	-
 *   3	EBD	EBD	EBD	EBD	EBD	-	-	-
 *   4	SBCLK	-	BCLK	BCKL	BCLK	-	-	-
 *   5	EICE	-	-	-	-	TBEN	TBEN	-
 *   6	ECLK	-	ECLK	ECLK	ECLK	-	-	-
 *   7	PAR	PAR	PAR	PAR	PAR	STEN	STEN	-
 *   8	DOZE	-	DOZE	DOZE	DOZE	-	HBATEN	DOZE
 *   9	NAP	-	NAP	NAP	NAP	NAP	NAP	NAP
 *  10	SLEEP	-	SLEEP	SLEEP	SLEEP	SLEEP	SLEEP	SLEEP
 *  11	DPM	-	DPM	DPM	DPM	DPM	DPM	-
 *  12	RISEG	-	-	RISEG	-	-	-	-
 *  13	-	-	-	EIEC	EIEC	BHTCLR	BHTCLR	-
 *  14	-	-	-	-	-	XAEN	XAEN	-
 *  15	-	NHR	NHR	NHR	NHR	NHR	NHR	-
 *  16	ICE	ICE	ICE	ICE	ICE	ICE	ICE	-
 *  17	DCE	DCE	DCE	DCE	DCE	DCE	DCE	TBEN
 *  18	ILOCK	ILOCK	ILOCK	ILOCK	ILOCK	ILOCK	ILOCK	SEL_TBCLK
 *  19	DLOCK	DLOCK	DLOCK	DLOCK	DLOCK	DLOCK	DLOCK	-
 *  20	ICFI	ICFI	ICFI	ICFI	ICFI	ICFI	ICFI	-
 *  21	DCFI	DCFI	DCFI	DCFI	DCFI	DCFI	DCFI	-
 *  22	-	-	SPD	SPD	SPG	SPD	SPD	-
 *  23	-	-	IFEM	IFTT	IFTT	-	XBSEN	-
 *  24	-	SIE	SGE	SGE	SGE	SGE	SGE	EN_MAS7_UPDATE
 *  25	-	-	DCFA	DCFA	DCFA	-	-	DCFA
 *  26	-	-	BTIC	BTIC	BTIC	BTIC	BTIC	-
 *  27	FBIOB	-	-	-	-	LRSTK	LRSTK	-
 *  28	-	-	ABE	-	-	FOLD	FOLD	-
 *  29	-	BHT	BHT	BHT	BHT	BHT	BHT	-
 *  30	-	-	-	NOPDST	NOPDST	NOPDST	NOPDST	-
 *  31	NOOPTI	-	NOOPTI	NOPTI	NOPTI	NOPTI	NOPTI	NOPTI
 *
 * bit	e500mc		e5500
 *   0	EMCP		EMCP
 *   1	EN_L2MMU_MHD	EN_L2MMU_MHD
 *   2	-		-
 *   3	-		-
 *   4	-		-
 *   5	-		-
 *   6	-		-
 *   7	-		-
 *   8	-		-
 *   9	-		-
 *  10	-		-
 *  11	-		-
 *  12	-		-
 *  13	-		-
 *  14	-		-
 *  15	-		-
 *  16	-		-
 *  17	-		-
 *  18	-		-
 *  19	-		-
 *  20	-		-
 *  21	-		-
 *  22	-		-
 *  23	-		-
 *  24	EN_MAS7_UPDATE	-
 *  25	DCFA		DCFA
 *  26	-		-
 *  27	CIGLSO		CIGLSO
 *  28	-		-
 *  29	-		-
 *  30	-		-
 *  31	NOPTI		NOPTI
 *
 *  604: ECP = Enable cache parity checking
 *  604: SIE = Serial instruction execution disable
 * 7450: TBEN = Time Base Enable
 * 7450: STEN = Software table lookup enable
 * 7450: BHTCLR = Branch history clear
 * 7450: XAEN = Extended Addressing Enabled
 * 7450: LRSTK = Link Register Stack Enable
 * 7450: FOLD = Branch folding enable
 * 7457: HBATEN = High BAT Enable
 * 7457: XBSEN = Extended BAT Block Size Enable
 */

#define HID1_E500_ABE	0x00001000  /* Address broadcast enable */
#define HID1_E500_ASTME	0x00002000  /* Address bus streaming mode enable */
#define HID1_E500_RFXE	0x00020000  /* Read fault exception enable */

#define HID0_E500_DEFAULT_SET	(HID0_EMCP | HID0_E500_TBEN | \
				 HID0_E500_MAS7UPDEN)
#define HID1_E500_DEFAULT_SET	(HID1_E500_ABE | HID1_E500_ASTME)
#define HID0_E500MC_DEFAULT_SET	(HID0_EMCP | HID0_E500MC_L2MMU_MHD | \
				 HID0_E500_MAS7UPDEN)
#define HID0_E5500_DEFAULT_SET	(HID0_EMCP | HID0_E500MC_L2MMU_MHD)

#define HID5_970_DCBZ_SIZE_HI	0x00000080UL	/* dcbz does a 32-byte store */
#define HID4_970_DISABLE_LG_PG	0x00000004ULL	/* disables large pages */

#endif /* _POWERPC_HID_H_ */
