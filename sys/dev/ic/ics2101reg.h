/* $OpenBSD: ics2101reg.h,v 1.4 2024/09/01 03:08:56 jsg Exp $ */
/* $NetBSD: ics2101reg.h,v 1.3 1996/02/05 02:18:52 jtc Exp $ */

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein and John Kohl.
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
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Register defs for Integrated Circuit Systems, Inc. ICS-2101 mixer
 * chip, used on Gravis UltraSound cards.
 *
 * Block diagram:
 *                                  port #
 *				       	0 +----+
 *   Mic in (Right/Left)	-->--->---|    |
 *					1 |    |	  amp --->---- amp out
 *   Line in (Right/Left)	-->--->---|    |	   |
 *					2 |    |	   |
 *   CD in (Right/Left)		-->--->---|    |--->---+---+----->---- line out
 *					3 |    |       |
 *   GF1 Out (Right/Left)	-->--->---|    |       |
 *					4 |    |       |
 *   Unused (Right/Left)	-->--->---|    |       |
 *					  +----+       v
 *					ICS 2101       |
 *						       |
 *	        To GF1 Sample Input ---<---------------+
 *
 *  Master output volume: mixer channel #5
 */

/*
 * ICS Mixer registers
 */

#define ICSMIX_CTRL_LEFT	0x00		/* Control left channel */
#define ICSMIX_CTRL_RIGHT	0x01		/* Control right channel */
#define ICSMIX_ATTN_LEFT	0x02		/* Attenuate left channel */
#define ICSMIX_ATTN_RIGHT	0x03		/* Attenuate right channel */
#define ICSMIX_PAEN		0x04		/* Panning control */
#define ICSMIX_CHAN_0		0		/* Values for mixer channels */
#define ICSMIX_CHAN_1		1
#define ICSMIX_CHAN_2		2
#define ICSMIX_CHAN_3		3
#define ICSMIX_CHAN_4		4
#define ICSMIX_CHAN_5		5

#define	ICSMIX_MIN_ATTN		0x7f
#define	ICSMIX_MAX_ATTN		0x00
/*
 * The ICS mixer is write-only--it cannot be queried for current settings.
 * Drivers must keep track of current values themselves.
 */
