/*	$FreeBSD$	*/
/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
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

/**
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

/*
 * This file was merged from two header files.(ad1848reg.h and cs4231reg.h)
 * And the suffix AD1848 and SP was changed to CS4231 and CS respectively.
 */
/* CS4231 direct registers */
#define CS4231_IADDR		0x00
#define CS4231_IDATA		0x01
#define CS4231_STATUS		0x02
#define CS4231_PIO		0x03

/* Index address register */
#define CS_IN_INIT		0x80
#define MODE_CHANGE_ENABLE	0x40
#define TRANSFER_DISABLE	0x20
#define ADDRESS_MASK		0xe0

/* Status bits */
#define INTERRUPT_STATUS	0x01
#define PLAYBACK_READY		0x02
#define PLAYBACK_LEFT		0x04
/* pbright is not left */
#define PLAYBACK_UPPER		0x08
/* bplower is not upper */
#define SAMPLE_ERROR		0x10
#define CAPTURE_READY		0x20
#define CAPTURE_LEFT		0x40
/* cpright is not left */
#define CAPTURE_UPPER		0x80
/* cplower is not upper */

/* CS4231 indirect mapped registers */
#define CS_LEFT_INPUT_CONTROL	0x00
#define CS_RIGHT_INPUT_CONTROL	0x01
#define CS_LEFT_AUX1_CONTROL	0x02
#define CS_RIGHT_AUX1_CONTROL	0x03
#define CS_LEFT_AUX2_CONTROL	0x04
#define CS_RIGHT_AUX2_CONTROL	0x05
#define CS_LEFT_OUTPUT_CONTROL	0x06
#define CS_RIGHT_OUTPUT_CONTROL	0x07
#define CS_CLOCK_DATA_FORMAT	0x08
#define CS_INTERFACE_CONFIG	0x09
#define CS_PIN_CONTROL		0x0a
#define CS_TEST_AND_INIT	0x0b
#define CS_MISC_INFO		0x0c
#define CS_DIGITAL_MIX		0x0d
#define CS_UPPER_BASE_COUNT	0x0e
#define CS_LOWER_BASE_COUNT	0x0f
/* CS4231/AD1845 mode2 registers; added to AD1848 registers */
#define CS_ALT_FEATURE1		0x10
#define CS_ALT_FEATURE2		0x11
#define CS_LEFT_LINE_CONTROL	0x12
#define CS_RIGHT_LINE_CONTROL	0x13
#define CS_TIMER_LOW		0x14
#define CS_TIMER_HIGH		0x15
#define CS_UPPER_FREQUENCY_SEL	0x16
#define CS_LOWER_FREQUENCY_SEL	0x17
#define CS_IRQ_STATUS		0x18
#define CS_VERSION_ID		0x19
#define CS_MONO_IO_CONTROL	0x1a
#define CS_POWERDOWN_CONTROL	0x1b
#define CS_REC_FORMAT		0x1c
#define CS_XTAL_SELECT		0x1d
#define CS_UPPER_REC_CNT	0x1e
#define CS_LOWER_REC_CNT	0x1f
#define CS_REG_NONE		0xff

#define CS_IN_MASK		0x2f
#define CS_IN_LINE		0x00
#define CS_IN_AUX1		0x40
#define CS_IN_MIC		0x80
#define CS_IN_DAC		0xc0
#define CS_MIC_GAIN_ENABLE	0x20
#define CS_IN_GAIN_MASK		0xf0

/* ADC input control - registers I0 (channel 1,left); I1 (channel 1,right) */
#define ADC_INPUT_ATTEN_BITS	0x0f
#define ADC_INPUT_GAIN_ENABLE	0x20

/* Aux input control - registers I2 (channel 1,left); I3 (channel 1,right)
				 I4 (channel 2,left); I5 (channel 2,right) */
#define AUX_INPUT_ATTEN_BITS	0x1f
#define AUX_INPUT_ATTEN_MASK	0xe0
#define AUX_INPUT_MUTE		0x80

/* Output bits - registers I6,I7*/
#define OUTPUT_MUTE		0x80
#define OUTPUT_ATTEN_BITS	0x3f
#define OUTPUT_ATTEN_MASK	(~OUTPUT_ATTEN_BITS & 0xff)

/* Clock and Data format reg bits (some also Capture Data format) - reg I8 */
#define CS_CLOCK_DATA_FORMAT_MASK 0x0f
#define CLOCK_XTAL1		0x00
#define CLOCK_XTAL2		0x01
#define CLOCK_FREQ_MASK		0xf1
#define CS_AFMT_STEREO		0x10
#define CS_AFMT_U8		0x00
#define CS_AFMT_MU_LAW		0x20
#define CS_AFMT_S16_LE		0x40
#define CS_AFMT_A_LAW		0x60
#define CS_AFMT_IMA_ADPCM	0xa0
#define CS_AFMT_S16_BE		0xc0

/* Interface Configuration reg bits - register I9 */
#define PLAYBACK_ENABLE		0x01
#define CAPTURE_ENABLE		0x02
#define DUAL_DMA		0x00
#define SINGLE_DMA		0x04
#define AUTO_CAL_ENABLE		0x08
#define PLAYBACK_PIO_ENABLE	0x40
#define CAPTURE_PIO_ENABLE	0x80

/* Pin control bits - register I10 */
#define INTERRUPT_ENABLE	0x02
#define XCTL0_ENABLE		0x40
#define XCTL1_ENABLE		0x80

/* Test and init reg bits - register I11 (read-only) */
#define OVERRANGE_LEFT_MASK	0xfc
#define OVERRANGE_RIGHT_MASK	0xf3
#define DATA_REQUEST_STATUS	0x10
#define AUTO_CAL_IN_PROG	0x20
#define PLAYBACK_UNDERRUN	0x40
#define CAPTURE_OVERRUN		0x80

/* Miscellaneous Control reg bits - register I12 */
#define CS_ID_MASK		0x70
#define CS_MODE2		0x40
#define CS_CODEC_ID_MASK	0x0f

/* Digital Mix Control reg bits - register I13 */
#define DIGITAL_MIX1_ENABLE	0x01
#define MIX_ATTEN_MASK		0x03

/* Alternate Feature Enable I - register I16 */
#define CS_DAC_ZERO		0x01
#define CS_PMC_ENABLE		0x10
#define CS_CMC_ENABLE		0x20
#define CS_OUTPUT_LVL		0x80

/* Alternate Feature Enable II - register I17 */
#define CS_HPF_ENABLE		0x01
#define DUAL_XTAL_ENABLE	0x02

/* alternate feature status(I24) */
#define CS_AFS_TI		0x40		/* timer interrupt */
#define CS_AFS_CI		0x20		/* capture interrupt */
#define CS_AFS_PI		0x10		/* playback interrupt */
#define CS_AFS_CU		0x08		/* capture underrun */
#define CS_AFS_CO		0x04		/* capture overrun */
#define CS_AFS_PO		0x02		/* playback overrun */
#define CS_AFS_PU		0x01		/* playback underrun */

/* Version - register I25 */
#define CS_VERSION_NUMBER	0xe0
#define CS_VERSION_CHIPID	0x07

/* Miscellaneous Control reg bits */
#define CS_MODE2		0x40

#define MONO_INPUT_ATTEN_BITS	0x0f
#define MONO_INPUT_ATTEN_MASK	0xf0
#define MONO_OUTPUT_MUTE	0x40
#define MONO_INPUT_MUTE		0x80
#define MONO_INPUT_MUTE_MASK	0x7f

#define LINE_INPUT_ATTEN_BITS	0x1f
#define LINE_INPUT_ATTEN_MASK	0xe0
#define LINE_INPUT_MUTE		0x80
#define LINE_INPUT_MUTE_MASK	0x7f
