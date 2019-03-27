/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2008 by Marco Trillo. All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 *	Apple DAVbus audio controller.
 */

#ifndef _SOUND_DAVBUS_H
#define _SOUND_DAVBUS_H

/* DAVbus controller registers. */
#define DAVBUS_SOUND_CTRL	0x00
#define DAVBUS_CODEC_CTRL	0x10
#define DAVBUS_CODEC_STATUS	0x20
#define DAVBUS_CLIP_COUNT	0x30
#define DAVBUS_BYTE_SWAP	0x40

/*
 * The DAVbus uses a serial bus time multiplexed in four subframes,
 * but the controller itself uses subframe 0 to communicate with the codec.
 * In some machines, the other subframes may be used by external devices
 * thorugh the DAV interface.
 */
/* DAVBUS_SOUND_CTRL bit definitions. */
#define DAVBUS_INPUT_SUBFRAME0	0x00000001
#define DAVBUS_INPUT_SUBFRAME1	0x00000002
#define DAVBUS_INPUT_SUBFRAME2	0x00000004
#define DAVBUS_INPUT_SUBFRAME3	0x00000008

#define DAVBUS_OUTPUT_SUBFRAME0	0x00000010
#define DAVBUS_OUTPUT_SUBFRAME1	0x00000020
#define DAVBUS_OUTPUT_SUBFRAME2	0x00000040
#define DAVBUS_OUTPUT_SUBFRAME3	0x00000080

#define DAVBUS_RATE_44100	0x00000000
#define DAVBUS_RATE_29400	0x00000100
#define DAVBUS_RATE_22050	0x00000200
#define DAVBUS_RATE_17640	0x00000300
#define DAVBUS_RATE_14700	0x00000400
#define DAVBUS_RATE_11025	0x00000500
#define DAVBUS_RATE_8820	0x00000600
#define DAVBUS_RATE_7350	0x00000700
#define DAVBUS_RATE_MASK	0x00000700

#define DAVBUS_ERROR		0x00000800
#define DAVBUS_PORTCHG		0x00001000
#define DAVBUS_INTR_ERROR	0x00002000	/* interrupt on error */
#define DAVBUS_INTR_PORTCHG	0x00004000	/* interrupt on port change */

#define DAVBUS_STATUS_SUBFRAME	0x00018000	/* mask */

/* DAVBUS_CODEC_CTRL bit definitions. */
#define DAVBUS_CODEC_BUSY	0x01000000


/*
 * Burgundy Codec Control Bits
 */

/* Burgundy transaction bits. */
#define BURGUNDY_CTRL_RESET	0x00100000
#define BURGUNDY_CTRL_WRITE	0x00200000

/* Mute control for each analog output port. */
#define BURGUNDY_MUTE_REG	0x16000
#define BURGUNDY_P13M_EN	0x01
#define BURGUNDY_P14L_EN	0x02
#define BURGUNDY_P14R_EN	0x04
#define BURGUNDY_P15L_EN	0x08
#define BURGUNDY_P15R_EN	0x10
#define BURGUNDY_P16L_EN	0x20
#define BURGUNDY_P16R_EN	0x40
#define BURGUNDY_P17M_EN	0x80

/* Attenuation of each analog output port. */
#define BURGUNDY_OL13_REG	0x16100
#define BURGUNDY_OL14_REG	0x16200
#define BURGUNDY_OL15_REG	0x16300
#define BURGUNDY_OL16_REG	0x16400
#define BURGUNDY_OL17_REG	0x16500

/* Inputs of four digital mixers. */
#define BURGUNDY_MIX0_REG	0x42900
#define BURGUNDY_MIX1_REG	0x42A00
#define BURGUNDY_MIX2_REG	0x42B00
#define BURGUNDY_MIX3_REG	0x42C00
#define BURGUNDY_MIX_IS0	0x00010001
#define BURGUNDY_MIX_IS1	0x00020002
#define BURGUNDY_MIX_IS2	0x00040004
#define BURGUNDY_MIX_IS3	0x00080008
#define BURGUNDY_MIX_IS4	0x00100010
#define BURGUNDY_MIX_ISA	0x01000100 /* Digital stream ISA. */
#define BURGUNDY_MIX_ISB	0x02000200 /* Digital stream ISB. */
#define BURGUNDY_MIX_ISC	0x04000400 /* Digital stream ISC. */
#define BURGUNDY_MIX_ISD	0x08000800 /* Digital stream ISD. */
#define BURGUNDY_MIX_ISE	0x10001000 /* Digital stream ISE. */
#define BURGUNDY_MIX_ISF	0x20002000 /* Digital stream ISF. */
#define BURGUNDY_MIX_ISG	0x40004000 /* Digital stream ISG. */
#define BURGUNDY_MIX_ISH	0x80008000 /* Digital stream ISH. */

/* A digital scalar at the output of each mixer. */
#define BURGUNDY_MXS0L_REG	0x12D00
#define BURGUNDY_MXS0R_REG	0x12D01
#define BURGUNDY_MXS1L_REG	0x12D02
#define BURGUNDY_MXS1R_REG	0x12D03
#define BURGUNDY_MXS2L_REG	0x12E00
#define BURGUNDY_MXS2R_REG	0x12E01
#define BURGUNDY_MXS3L_REG	0x12E02
#define BURGUNDY_MXS3R_REG	0x12E03
#define BURGUNDY_MXS_UNITY	0xDF

/* Demultiplexer. Routes the mixer 0-3 (see above) to output sources.
   Output sources 0-2 can be converted to analog. */
#define BURGUNDY_OS_REG		0x42F00
#define BURGUNDY_OS0_MIX0	0x00000000
#define BURGUNDY_OS0_MIX1	0x00000001
#define BURGUNDY_OS0_MIX2	0x00000002
#define BURGUNDY_OS0_MIX3	0x00000003
#define BURGUNDY_OS1_MIX0       0x00000000
#define BURGUNDY_OS1_MIX1       0x00000004
#define BURGUNDY_OS1_MIX2       0x00000008
#define BURGUNDY_OS1_MIX3       0x0000000C
#define BURGUNDY_OS2_MIX0       0x00000000
#define BURGUNDY_OS2_MIX1       0x00000010
#define BURGUNDY_OS2_MIX2       0x00000020
#define BURGUNDY_OS2_MIX3       0x00000030
#define BURGUNDY_OS3_MIX0       0x00000000
#define BURGUNDY_OS3_MIX1       0x00000040
#define BURGUNDY_OS3_MIX2       0x00000080
#define BURGUNDY_OS3_MIX3       0x000000C0
#define BURGUNDY_OSA_MIX0	0x00000000
#define BURGUNDY_OSA_MIX1	0x00010000	
#define BURGUNDY_OSA_MIX2	0x00020000
#define BURGUNDY_OSA_MIX3	0x00030000
#define BURGUNDY_OSB_MIX0       0x00000000
#define BURGUNDY_OSB_MIX1       0x00040000
#define BURGUNDY_OSB_MIX2	0x00080000
#define BURGUNDY_OSB_MIX3	0x000C0000
#define BURGUNDY_OSC_MIX0       0x00000000
#define BURGUNDY_OSC_MIX1       0x00100000
#define BURGUNDY_OSC_MIX2	0x00200000
#define BURGUNDY_OSC_MIX3	0x00300000
#define BURGUNDY_OSD_MIX0       0x00000000
#define BURGUNDY_OSD_MIX1       0x00400000
#define BURGUNDY_OSD_MIX2	0x00800000
#define BURGUNDY_OSD_MIX3	0x00C00000
#define BURGUNDY_OSE_MIX0       0x00000000
#define BURGUNDY_OSE_MIX1	0x01000000
#define BURGUNDY_OSE_MIX2	0x02000000
#define BURGUNDY_OSE_MIX3	0x03000000
#define BURGUNDY_OSF_MIX0       0x00000000
#define BURGUNDY_OSF_MIX1       0x04000000
#define BURGUNDY_OSF_MIX2	0x08000000
#define BURGUNDY_OSF_MIX3	0x0C000000
#define BURGUNDY_OSG_MIX0       0x00000000
#define BURGUNDY_OSG_MIX1       0x10000000
#define BURGUNDY_OSG_MIX2	0x20000000
#define BURGUNDY_OSG_MIX3	0x30000000
#define BURGUNDY_OSH_MIX0       0x00000000
#define BURGUNDY_OSH_MIX1       0x40000000
#define BURGUNDY_OSH_MIX2	0x80000000
#define BURGUNDY_OSH_MIX3	0xC0000000

/* A digital scalar for output sources 0 to 3. */
#define BURGUNDY_OSS0L_REG	0x13000
#define BURGUNDY_OSS0R_REG	0x13001
#define BURGUNDY_OSS1L_REG	0x13002
#define BURGUNDY_OSS1R_REG	0x13003
#define BURGUNDY_OSS2L_REG	0x13100
#define BURGUNDY_OSS2R_REG	0x13101
#define BURGUNDY_OSS3L_REG	0x13102
#define BURGUNDY_OSS3R_REG	0x13103
#define BURGUNDY_OSS_UNITY	0xDF

/* Digital input streams ISA-ISC. A stream may be derived from data coming 
   from the controller in subframes 0 to 3 as well as from internal 
   output sources OSA-OSD. */
#define BURGUNDY_SDIN_REG	0x17800
#define BURGUNDY_ISA_SF0	0x00
#define BURGUNDY_ISA_OSA	0x02
#define BURGUNDY_ISB_SF1	0x00
#define BURGUNDY_ISB_OSB	0x08
#define BURGUNDY_ISC_SF2	0x00
#define BURGUNDY_ISC_OSC	0x20
#define BURGUNDY_ISD_SF3	0x00
#define BURGUNDY_ISD_OSD	0x80

/* A digital scaler for input streams 0-4 A-H. */
#define BURGUNDY_ISSAL_REG	0x12500
#define BURGUNDY_ISSAR_REG	0x12501
#define BURGUNDY_ISS_UNITY	0xDF

/*
 * Screamer codec control bits 
 * This codec has the following 12-bit control registers:
 *	cc0 cc1 cc2 cc4 cc5 cc6 cc7
 */

/* screamer transaction bits. */
#define SCREAMER_CODEC_ADDR0	0x00000000
#define SCREAMER_CODEC_ADDR1	0x00001000
#define SCREAMER_CODEC_ADDR2	0x00002000
#define SCREAMER_CODEC_ADDR4	0x00004000
#define SCREAMER_CODEC_ADDR5	0x00005000
#define SCREAMER_CODEC_ADDR6	0x00006000
#define SCREAMER_CODEC_ADDR7	0x00007000
#define SCREAMER_CODEC_EMSEL0	0x00000000
#define SCREAMER_CODEC_EMSEL1	0x00400000
#define SCREAMER_CODEC_EMSEL2	0x00800000
#define SCREAMER_CODEC_EMSEL4	0x00c00000


/* cc0 */
/*
 * Bits 7-4 specify the left ADC input gain;
 * bits 3-0 specify the right ADC input gain.
 *
 * The gain is a 4-bit value expressed in units of 1.5 dB, 
 * ranging from 0 dB (0) to +22.5 dB (15).
 */
#define SCREAMER_DEFAULT_CD_GAIN	0x000000bb /* +16.5 dB */
#define SCREAMER_INPUT_CD		0x00000200
#define SCREAMER_INPUT_LINE		0x00000400
#define SCREAMER_INPUT_MICROPHONE	0x00000800
#define SCREAMER_INPUT_MASK		0x00000e00

/* cc1 */
#define SCREAMER_LOOP_THROUGH		0x00000040
#define SCREAMER_MUTE_SPEAKER		0x00000080
#define SCREAMER_MUTE_HEADPHONES	0x00000200
#define SCREAMER_PARALLEL_OUTPUT	0x00000c00
#define SCREAMER_PROG_OUTPUT0      	0x00000400
#define SCREAMER_PROG_OUTPUT1      	0x00000800

/* cc2: headphones/external port attenuation */
/* cc4: internal speaker attenuation */
/*
 * Bits 9-6 specify left DAC output attenuation.
 * Bits 3-0 specify right DAC output attenuation.
 *
 * The attenuation is a 4-bit value expressed in units of -1.5 dB,
 * ranging from 0 dB (0) to -22.5 dB (15).
 */

/* screamer codec status bits. */
#define SCREAMER_STATUS_MASK 	0x00FFFFFF
#define SCREAMER_STATUS_SENSEMASK 	0x0000000F
#define  SCREAMER_STATUS_SENSE0 	0x00000008
#define  SCREAMER_STATUS_SENSE1 	0x00000004
#define  SCREAMER_STATUS_SENSE2 	0x00000002
#define  SCREAMER_STATUS_SENSE3 	0x00000001
#define SCREAMER_STATUS_PARTMASK	0x00000300
#define SCREAMER_STATUS_PARTSHFT	8
#define  SCREAMER_PART_CRYSTAL	0x00000100
#define  SCREAMER_PART_NATIONAL	0x00000200
#define  SCREAMER_PART_TI		0x00000300
#define SCREAMER_STATUS_REVMASK	0x0000F000
#define SCREAMER_STATUS_REVSHFT	12

#endif /* _SOUND_DAVBUS_H */

