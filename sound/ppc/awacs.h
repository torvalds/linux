/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for PowerMac AWACS onboard soundchips
 * Copyright (c) 2001 by Takashi Iwai <tiwai@suse.de>
 *   based on dmasound.c.
 */


#ifndef __AWACS_H
#define __AWACS_H

/*******************************/
/* AWACs Audio Register Layout */
/*******************************/

struct awacs_regs {
    unsigned	control;	/* Audio control register */
    unsigned	pad0[3];
    unsigned	codec_ctrl;	/* Codec control register */
    unsigned	pad1[3];
    unsigned	codec_stat;	/* Codec status register */
    unsigned	pad2[3];
    unsigned	clip_count;	/* Clipping count register */
    unsigned	pad3[3];
    unsigned	byteswap;	/* Data is little-endian if 1 */
};

/*******************/
/* Audio Bit Masks */
/*******************/

/* Audio Control Reg Bit Masks */
/* ----- ------- --- --- ----- */
#define MASK_ISFSEL	(0xf)		/* Input SubFrame Select */
#define MASK_OSFSEL	(0xf << 4)	/* Output SubFrame Select */
#define MASK_RATE	(0x7 << 8)	/* Sound Rate */
#define MASK_CNTLERR	(0x1 << 11)	/* Error */
#define MASK_PORTCHG	(0x1 << 12)	/* Port Change */
#define MASK_IEE	(0x1 << 13)	/* Enable Interrupt on Error */
#define MASK_IEPC	(0x1 << 14)	/* Enable Interrupt on Port Change */
#define MASK_SSFSEL	(0x3 << 15)	/* Status SubFrame Select */

/* Audio Codec Control Reg Bit Masks */
/* ----- ----- ------- --- --- ----- */
#define MASK_NEWECMD	(0x1 << 24)	/* Lock: don't write to reg when 1 */
#define MASK_EMODESEL	(0x3 << 22)	/* Send info out on which frame? */
#define MASK_EXMODEADDR	(0x3ff << 12)	/* Extended Mode Address -- 10 bits */
#define MASK_EXMODEDATA	(0xfff)		/* Extended Mode Data -- 12 bits */

/* Audio Codec Control Address Values / Masks */
/* ----- ----- ------- ------- ------ - ----- */
#define MASK_ADDR0	(0x0 << 12)	/* Expanded Data Mode Address 0 */
#define MASK_ADDR_MUX	MASK_ADDR0	/* Mux Control */
#define MASK_ADDR_GAIN	MASK_ADDR0

#define MASK_ADDR1	(0x1 << 12)	/* Expanded Data Mode Address 1 */
#define MASK_ADDR_MUTE	MASK_ADDR1
#define MASK_ADDR_RATE	MASK_ADDR1

#define MASK_ADDR2	(0x2 << 12)	/* Expanded Data Mode Address 2 */
#define MASK_ADDR_VOLA	MASK_ADDR2	/* Volume Control A -- Headphones */
#define MASK_ADDR_VOLHD MASK_ADDR2

#define MASK_ADDR4	(0x4 << 12)	/* Expanded Data Mode Address 4 */
#define MASK_ADDR_VOLC	MASK_ADDR4	/* Volume Control C -- Speaker */
#define MASK_ADDR_VOLSPK MASK_ADDR4

/* additional registers of screamer */
#define MASK_ADDR5	(0x5 << 12)	/* Expanded Data Mode Address 5 */
#define MASK_ADDR6	(0x6 << 12)	/* Expanded Data Mode Address 6 */
#define MASK_ADDR7	(0x7 << 12)	/* Expanded Data Mode Address 7 */

/* Address 0 Bit Masks & Macros */
/* ------- - --- ----- - ------ */
#define MASK_GAINRIGHT	(0xf)		/* Gain Right Mask */
#define MASK_GAINLEFT	(0xf << 4)	/* Gain Left Mask */
#define MASK_GAINLINE	(0x1 << 8)	/* Disable Mic preamp */
#define MASK_GAINMIC	(0x0 << 8)	/* Enable Mic preamp */
#define MASK_MUX_CD	(0x1 << 9)	/* Select CD in MUX */
#define MASK_MUX_MIC	(0x1 << 10)	/* Select Mic in MUX */
#define MASK_MUX_AUDIN	(0x1 << 11)	/* Select Audio In in MUX */
#define MASK_MUX_LINE	MASK_MUX_AUDIN
#define SHIFT_GAINLINE	8
#define SHIFT_MUX_CD	9
#define SHIFT_MUX_MIC	10
#define SHIFT_MUX_LINE	11

#define GAINRIGHT(x)	((x) & MASK_GAINRIGHT)
#define GAINLEFT(x)	(((x) << 4) & MASK_GAINLEFT)

/* Address 1 Bit Masks */
/* ------- - --- ----- */
#define MASK_ADDR1RES1	(0x3)		/* Reserved */
#define MASK_RECALIBRATE (0x1 << 2)	/* Recalibrate */
#define MASK_SAMPLERATE	(0x7 << 3)	/* Sample Rate: */
#define MASK_LOOPTHRU	(0x1 << 6)	/* Loopthrough Enable */
#define SHIFT_LOOPTHRU	6
#define MASK_CMUTE	(0x1 << 7)	/* Output C (Speaker) Mute when 1 */
#define MASK_SPKMUTE	MASK_CMUTE
#define SHIFT_SPKMUTE	7
#define MASK_ADDR1RES2	(0x1 << 8)	/* Reserved */
#define MASK_AMUTE	(0x1 << 9)	/* Output A (Headphone) Mute when 1 */
#define MASK_HDMUTE	MASK_AMUTE
#define SHIFT_HDMUTE	9
#define MASK_PAROUT	(0x3 << 10)	/* Parallel Out (???) */
#define MASK_PAROUT0	(0x1 << 10)	/* Parallel Out (???) */
#define MASK_PAROUT1	(0x1 << 11)	/* Parallel Out (enable speaker) */
#define SHIFT_PAROUT	10
#define SHIFT_PAROUT0	10
#define SHIFT_PAROUT1	11

#define SAMPLERATE_48000	(0x0 << 3)	/* 48 or 44.1 kHz */
#define SAMPLERATE_32000	(0x1 << 3)	/* 32 or 29.4 kHz */
#define SAMPLERATE_24000	(0x2 << 3)	/* 24 or 22.05 kHz */
#define SAMPLERATE_19200	(0x3 << 3)	/* 19.2 or 17.64 kHz */
#define SAMPLERATE_16000	(0x4 << 3)	/* 16 or 14.7 kHz */
#define SAMPLERATE_12000	(0x5 << 3)	/* 12 or 11.025 kHz */
#define SAMPLERATE_9600		(0x6 << 3)	/* 9.6 or 8.82 kHz */
#define SAMPLERATE_8000		(0x7 << 3)	/* 8 or 7.35 kHz */

/* Address 2 & 4 Bit Masks & Macros */
/* ------- - - - --- ----- - ------ */
#define MASK_OUTVOLRIGHT (0xf)		/* Output Right Volume */
#define MASK_ADDR2RES1	(0x2 << 4)	/* Reserved */
#define MASK_ADDR4RES1	MASK_ADDR2RES1
#define MASK_OUTVOLLEFT	(0xf << 6)	/* Output Left Volume */
#define MASK_ADDR2RES2	(0x2 << 10)	/* Reserved */
#define MASK_ADDR4RES2	MASK_ADDR2RES2

#define VOLRIGHT(x)	(((~(x)) & MASK_OUTVOLRIGHT))
#define VOLLEFT(x)	(((~(x)) << 6) & MASK_OUTVOLLEFT)

/* address 6 */
#define MASK_MIC_BOOST  (0x4)		/* screamer mic boost */
#define SHIFT_MIC_BOOST	2

/* Audio Codec Status Reg Bit Masks */
/* ----- ----- ------ --- --- ----- */
#define MASK_EXTEND	(0x1 << 23)	/* Extend */
#define MASK_VALID	(0x1 << 22)	/* Valid Data? */
#define MASK_OFLEFT	(0x1 << 21)	/* Overflow Left */
#define MASK_OFRIGHT	(0x1 << 20)	/* Overflow Right */
#define MASK_ERRCODE	(0xf << 16)	/* Error Code */
#define MASK_REVISION	(0xf << 12)	/* Revision Number */
#define MASK_MFGID	(0xf << 8)	/* Mfg. ID */
#define MASK_CODSTATRES	(0xf << 4)	/* bits 4 - 7 reserved */
#define MASK_INSENSE	(0xf)		/* port sense bits: */
#define MASK_HDPCONN		8	/* headphone plugged in */
#define MASK_LOCONN		4	/* line-out plugged in */
#define MASK_LICONN		2	/* line-in plugged in */
#define MASK_MICCONN		1	/* microphone plugged in */
#define MASK_LICONN_IMAC	8	/* line-in plugged in */
#define MASK_HDPRCONN_IMAC	4	/* headphone right plugged in */
#define MASK_HDPLCONN_IMAC	2	/* headphone left plugged in */
#define MASK_LOCONN_IMAC	1	/* line-out plugged in */

/* Clipping Count Reg Bit Masks */
/* -------- ----- --- --- ----- */
#define MASK_CLIPLEFT	(0xff << 7)	/* Clipping Count, Left Channel */
#define MASK_CLIPRIGHT	(0xff)		/* Clipping Count, Right Channel */

/* DBDMA ChannelStatus Bit Masks */
/* ----- ------------- --- ----- */
#define MASK_CSERR	(0x1 << 7)	/* Error */
#define MASK_EOI	(0x1 << 6)	/* End of Input --
					   only for Input Channel */
#define MASK_CSUNUSED	(0x1f << 1)	/* bits 1-5 not used */
#define MASK_WAIT	(0x1)		/* Wait */

/* Various Rates */
/* ------- ----- */
#define RATE_48000	(0x0 << 8)	/* 48 kHz */
#define RATE_44100	(0x0 << 8)	/* 44.1 kHz */
#define RATE_32000	(0x1 << 8)	/* 32 kHz */
#define RATE_29400	(0x1 << 8)	/* 29.4 kHz */
#define RATE_24000	(0x2 << 8)	/* 24 kHz */
#define RATE_22050	(0x2 << 8)	/* 22.05 kHz */
#define RATE_19200	(0x3 << 8)	/* 19.2 kHz */
#define RATE_17640	(0x3 << 8)	/* 17.64 kHz */
#define RATE_16000	(0x4 << 8)	/* 16 kHz */
#define RATE_14700	(0x4 << 8)	/* 14.7 kHz */
#define RATE_12000	(0x5 << 8)	/* 12 kHz */
#define RATE_11025	(0x5 << 8)	/* 11.025 kHz */
#define RATE_9600	(0x6 << 8)	/* 9.6 kHz */
#define RATE_8820	(0x6 << 8)	/* 8.82 kHz */
#define RATE_8000	(0x7 << 8)	/* 8 kHz */
#define RATE_7350	(0x7 << 8)	/* 7.35 kHz */

#define RATE_LOW	1	/* HIGH = 48kHz, etc;  LOW = 44.1kHz, etc. */


#endif /* __AWACS_H */
