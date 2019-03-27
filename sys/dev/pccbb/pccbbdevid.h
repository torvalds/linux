/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2004 M. Warner Losh.
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
 */

/* Vendor/Device IDs */
#define	PCIC_ID_CLPD6729	0x11001013ul	/* 16bit I/O */
#define	PCIC_ID_CLPD6832	0x11101013ul
#define	PCIC_ID_CLPD6833	0x11131013ul
#define	PCIC_ID_CLPD6834	0x11121013ul
#define	PCIC_ID_ENE_CB710	0x14111524ul
#define	PCIC_ID_ENE_CB720	0x14211524ul	/* ??? */
#define	PCIC_ID_ENE_CB1211	0x12111524ul	/* ??? */
#define	PCIC_ID_ENE_CB1225	0x12251524ul	/* ??? */
#define	PCIC_ID_ENE_CB1410	0x14101524ul
#define	PCIC_ID_ENE_CB1420	0x14201524ul
#define	PCIC_ID_INTEL_82092AA_0	0x12218086ul	/* 16bit I/O */
#define	PCIC_ID_INTEL_82092AA_1	0x12228086ul	/* 16bit I/O */
#define	PCIC_ID_OMEGA_82C094	0x1221119bul	/* 16bit I/O */
#define	PCIC_ID_OZ6729		0x67291217ul	/* 16bit I/O */
#define	PCIC_ID_OZ6730		0x673a1217ul	/* 16bit I/O */
#define	PCIC_ID_OZ6832		0x68321217ul	/* Also 6833 */
#define	PCIC_ID_OZ6860		0x68361217ul	/* Also 6836 */
#define	PCIC_ID_OZ6872		0x68721217ul	/* Also 6812 */
#define	PCIC_ID_OZ6912		0x69721217ul	/* Also 6972 */
#define	PCIC_ID_OZ6922		0x69251217ul
#define	PCIC_ID_OZ6933		0x69331217ul
#define	PCIC_ID_OZ711EC1	0x71121217ul	/* O2Micro 711EC1/M1 */
#define	PCIC_ID_OZ711E1		0x71131217ul	/* O2Micro 711E1 */
#define	PCIC_ID_OZ711M1		0x71141217ul	/* O2Micro 711M1 */
#define	PCIC_ID_OZ711E2		0x71e21217ul
#define	PCIC_ID_OZ711M2		0x72121217ul
#define	PCIC_ID_OZ711M3		0x72231217ul
#define	PCIC_ID_RICOH_RL5C465	0x04651180ul
#define	PCIC_ID_RICOH_RL5C466	0x04661180ul
#define	PCIC_ID_RICOH_RL5C475	0x04751180ul
#define	PCIC_ID_RICOH_RL5C476	0x04761180ul
#define	PCIC_ID_RICOH_RL5C477	0x04771180ul
#define	PCIC_ID_RICOH_RL5C478	0x04781180ul
#define	PCIC_ID_SMC_34C90	0xb10610b3ul
#define	PCIC_ID_TI1031		0xac13104cul
#define	PCIC_ID_TI1130		0xac12104cul
#define	PCIC_ID_TI1131		0xac15104cul
#define	PCIC_ID_TI1210		0xac1a104cul
#define	PCIC_ID_TI1211		0xac1e104cul
#define	PCIC_ID_TI1220		0xac17104cul
#define	PCIC_ID_TI1221		0xac19104cul	/* never sold */
#define	PCIC_ID_TI1225		0xac1c104cul
#define	PCIC_ID_TI1250		0xac16104cul	/* Rare */
#define	PCIC_ID_TI1251		0xac1d104cul
#define	PCIC_ID_TI1251B		0xac1f104cul
#define	PCIC_ID_TI1260		0xac18104cul	/* never sold */
#define	PCIC_ID_TI1260B		0xac30104cul	/* never sold */
#define	PCIC_ID_TI1410		0xac50104cul
#define	PCIC_ID_TI1420		0xac51104cul
#define	PCIC_ID_TI1421		0xac53104cul	/* never sold */
#define	PCIC_ID_TI1450		0xac1b104cul
#define	PCIC_ID_TI1451		0xac52104cul
#define	PCIC_ID_TI1510		0xac56104cul
#define	PCIC_ID_TI1515		0xac58104cul
#define	PCIC_ID_TI1520		0xac55104cul
#define	PCIC_ID_TI1530		0xac57104cul
#define	PCIC_ID_TI1620		0xac54104cul
#define	PCIC_ID_TI4410		0xac41104cul
#define	PCIC_ID_TI4450		0xac40104cul
#define	PCIC_ID_TI4451		0xac42104cul
#define	PCIC_ID_TI4510		0xac44104cul
#define	PCIC_ID_TI4520		0xac46104cul
#define	PCIC_ID_TI6411		0x8031104cul	/* PCI[67]x[12]1 */
#define	PCIC_ID_TI6420		0xac8d104cul	/* PCI[67]x20 Smartcard dis */
#define	PCIC_ID_TI6420SC	0xac8e104cul	/* PCI[67]x20 Smartcard en */
#define	PCIC_ID_TI7410		0xac49104cul
#define	PCIC_ID_TI7510		0xac47104cul
#define	PCIC_ID_TI7610		0xac48104cul
#define	PCIC_ID_TI7610M		0xac4a104cul
#define	PCIC_ID_TI7610SD	0xac4b104cul
#define	PCIC_ID_TI7610MS	0xac4c104cul
#define	PCIC_ID_TOPIC95		0x06031179ul
#define	PCIC_ID_TOPIC95B	0x060a1179ul
#define	PCIC_ID_TOPIC97		0x060f1179ul
#define	PCIC_ID_TOPIC100	0x06171179ul

/*
 * Other ID, from sources too vague to be reliable
 *	Mfg		  model		PCI ID
 *   smc/Databook	DB87144		0x310610b3
 *   Omega/Trident	82c194		0x01941023
 *   Omega/Trident	82c722		0x07221023?
 *   Opti		82c814		0xc8141045
 *   Opti		82c824		0xc8241045
 *   NEC		uPD66369	0x003e1033
 */
