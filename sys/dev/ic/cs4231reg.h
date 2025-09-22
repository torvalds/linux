/* $OpenBSD: cs4231reg.h,v 1.8 2022/01/09 05:42:38 jsg Exp $ */
/* $NetBSD: cs4231reg.h,v 1.4 1996/02/16 08:12:33 mycroft Exp $ */

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
 * Register defs for Crystal Semiconductor CS4231 Audio Codec/mixer
 * chip, used on Gravis UltraSound MAX cards.
 *
 * Block diagram:
 *             +----------------------------------------------------+
 *             |						    |
 *             |   +----------------------------------------------+ |
 *	       |   |mixed in       +-+  		          | |
 *	       |   +------------>--| |  		          | |
 *             | mic in            | |			          | |
 *   Mic --+-->| --------- GAIN ->-| |			          | |
 *         |   | AUX 1 in          |M|				  | |
 *   GF1 --)-->| -------------+-->-|U|				  | |
 *	   |   | Line in      |	   |X|---- GAIN ----------+	  | |
 *  Line --)-->| ---------+---)-->-| |			  |	  | |
 *	   |   |	  |   |    | |			  |	  | |
 *	   |   |	  |   |    +-+		         ADC 	  | |
 *	   |   |	  |   |      		          | 	  | |
 *	   |   |	  |   |				  |	  | |
 *	   |   |	  |   +--- L/M --\		  |	  | | AMP-->
 *	   |   |	  |   	   	  \		  |	  | |  |
 *	   |   |	  |   	   	   \	          |	  | |  |
 *	   |   |	  +---- L/M -------O-->--+--------)-------+-|--+-> line
 *	   |   |   mono in	       	  /|     |        |	    |
 *	   +---|-->------------ L/M -----/ |     |        |	    |
 *	       |   AUX 2 in		   |     |        |	    |
 *  CD --------|-->------------ L/M -------+    L/M       |	    |
 *	       |				 |        v	    |
 *	       |				 |        |	    |
 *	       |				DAC       |	    |
 *	       |				 |        |	    |
 *             +----------------------------------------------------+
 *	       					 |        |
 *						 |        |
 *						 v        v
 *     	       	       	       	       	       	  Pc BUS (DISK) ???
 *
 * Documentation for this chip can be found at:
 *	http://www.cirrus.com/products/overviews/cs4231.html
 */

/* CS4231/AD1845 mode2 registers; added to AD1848 registers */
#define CS_ALT_FEATURE1		0x10
#define CS_ALT_FEATURE2		0x11
#define CS_LEFT_LINE_CONTROL	0x12
#define CS_RIGHT_LINE_CONTROL	0x13
#define		LINE_INPUT_ATTEN_BITS	0x1f
#define		LINE_INPUT_ATTEN_MASK	0xe0
#define		LINE_INPUT_MUTE		0x80
#define		LINE_INPUT_MUTE_MASK	0x7f
#define CS_TIMER_LOW		0x14
#define CS_TIMER_HIGH		0x15
#define CS_UPPER_FREQUENCY_SEL	0x16
#define CS_LOWER_FREQUENCY_SEL	0x17
#define CS_IRQ_STATUS		0x18
#define		CS_IRQ_PU		0x01	/* Playback Underrun */
#define		CS_IRQ_PO		0x02	/* Playback Overrun */
#define		CS_IRQ_CO		0x04	/* Capture Overrun */
#define		CS_IRQ_CU		0x08	/* Capture Underrun */
#define		CS_IRQ_PI		0x10	/* Playback Interrupt */
#define		CS_IRQ_CI		0x20	/* Capture Interrupt */
#define		CS_IRQ_TI		0x40	/* Timer Interrupt */
#define		CS_IRQ_RES		0x80	/* reserved */
#define CS_VERSION_ID		0x19
#define CS_MONO_IO_CONTROL	0x1A
#define		MONO_INPUT_ATTEN_BITS	0x0f
#define		MONO_INPUT_ATTEN_MASK	0xf0
#define		MONO_OUTPUT_MUTE	0x40
#define		MONO_INPUT_MUTE		0x80
#define		MONO_INPUT_MUTE_MASK	0x7f
#define CS_POWERDOWN_CONTROL	0x1B
#define CS_REC_FORMAT		0x1C
#define CS_XTAL_SELECT		0x1D
#define CS_UPPER_REC_CNT	0x1E
#define CS_LOWER_REC_CNT	0x1F
