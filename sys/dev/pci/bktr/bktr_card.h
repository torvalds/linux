/*	$OpenBSD: bktr_card.h,v 1.7 2007/01/08 08:29:37 pedro Exp $	*/
/* $FreeBSD: src/sys/dev/bktr/bktr_card.h,v 1.4 2000/06/26 09:41:31 roger Exp $ */

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_card : This deals with identifying TV cards.
 *               trying to find the card make and model of card.
 *               trying to find the type of tuner fitted.
 *               reading the configuration EEPROM.
 *               locating i2c devices.
 *
 */

/*
 * 1. Redistributions of source code must retain the
 * Copyright (c) 1997 Amancio Hasty, 1999 Roger Hardiman
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Amancio Hasty and
 *      Roger Hardiman
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * If probeCard() fails to detect the correct card on boot you can
 * override it by setting adding the following option to your kernel config
 *  options BKTR_OVERRIDE_CARD  <card type>
 *  eg options BKTR_OVERRIDE CARD=1
 *
 * or using the sysctl  hw.bt848.card
 *  eg sysctl hw.bt848.card=1
 *
 * where <card type> is one of the following card defines.
 */
 
#define CARD_UNKNOWN		0
#define CARD_MIRO		1
#define CARD_HAUPPAUGE		2
#define CARD_STB		3
#define CARD_INTEL		4   /* Also for VideoLogic Captivator PCI */
#define CARD_IMS_TURBO		5
#define CARD_AVER_MEDIA		6
#define CARD_OSPREY		7
#define CARD_NEC_PK		8
#define CARD_IO_GV		9
#define CARD_FLYVIDEO		10
#define CARD_ZOLTRIX		11
#define CARD_KISS		12
#define CARD_VIDEO_HIGHWAY_XTREME	13
#define CARD_ASKEY_DYNALINK_MAGIC_TVIEW	14
#define CARD_LEADTEK		15
#define CARD_TERRATVPLUS	16
#define CARD_TVWONDER		17
#define CARD_IO_BCTV3		18
#define CARD_AOPEN_VA1000	19
#define CARD_ZOLTRIX_GENIE_FM	20
#define CARD_LEADTEK_XP		21
#define Bt848_MAX_CARD		22


int	signCard( bktr_ptr_t bktr, int offset, int count, u_char *sig );
void	probeCard( bktr_ptr_t bktr, int verbose, int unit);

int	writeEEProm( bktr_ptr_t bktr, int offset, int count, u_char *data );
int	readEEProm( bktr_ptr_t bktr, int offset, int count, u_char *data );

