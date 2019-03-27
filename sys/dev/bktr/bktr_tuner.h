/* $FreeBSD$ */

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 * 
 * bktr_tuner : This deals with controlling the tuner fitted to TV cards.
 *
 */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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

/* Definitions for Tuners */

#define NO_TUNER		0
#define TEMIC_NTSC		1
#define TEMIC_PAL		2
#define TEMIC_SECAM		3
#define PHILIPS_NTSC		4
#define PHILIPS_PAL		5
#define PHILIPS_SECAM		6
#define TEMIC_PALI		7
#define PHILIPS_PALI		8
#define PHILIPS_FR1236_NTSC	9	/* These have FM radio support */
#define PHILIPS_FR1216_PAL	10	/* These have FM radio support */
#define PHILIPS_FR1236_SECAM	11	/* These have FM radio support */
#define ALPS_TSCH5		12
#define ALPS_TSBH1		13
#define TUNER_MT2032		14
#define	LG_TPI8PSB12P_PAL	15
#define PHILIPS_FI1216          16
#define Bt848_MAX_TUNER		17

/* experimental code for Automatic Frequency Control */ 
#define TUNER_AFC

/*
 * Fill in the tuner entries in the bktr_softc based on the selected tuner
 * type (from the list of tuners above)
 */
void	select_tuner( bktr_ptr_t bktr, int tuner_type );

/*
 * The Channel Set maps TV channels eg Ch 36, Ch 51, onto frequencies
 * and is country specific.
 */
int	tuner_getchnlset( struct bktr_chnlset *chnlset );

/*
 * tv_channel sets the tuner to channel 'n' using the current Channel Set
 * tv_freq sets the tuner to a specific frequency for TV or for FM Radio
 * get_tuner_status can be used to get the signal strength.
 */
#define TV_FREQUENCY       0
#define FM_RADIO_FREQUENCY 1
int	tv_channel( bktr_ptr_t bktr, int channel );
int	tv_freq( bktr_ptr_t bktr, int frequency, int type );
int	get_tuner_status( bktr_ptr_t bktr );

#if defined( TUNER_AFC )
int	do_afc( bktr_ptr_t bktr, int addr, int frequency );
#endif /* TUNER_AFC */

int mt2032_init(bktr_ptr_t bktr);

/* 
 * This is for start-up convenience only, NOT mandatory.
 */
#if !defined( DEFAULT_CHNLSET )
#define DEFAULT_CHNLSET CHNLSET_WEUROPE
#endif

