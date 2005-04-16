/*********************************************************/
/* This file was written by someone, somewhere, sometime */
/* And is released into the Public Domain                */
/*********************************************************/

#ifndef _AWACS_DEFS_H_
#define _AWACS_DEFS_H_

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

#define GAINRIGHT(x)	((x) & MASK_GAINRIGHT)
#define GAINLEFT(x)	(((x) << 4) & MASK_GAINLEFT)

#define DEF_CD_GAIN 0x00bb
#define DEF_MIC_GAIN 0x00cc

/* Address 1 Bit Masks */
/* ------- - --- ----- */
#define MASK_ADDR1RES1	(0x3)		/* Reserved */
#define MASK_RECALIBRATE (0x1 << 2)	/* Recalibrate */
#define MASK_SAMPLERATE	(0x7 << 3)	/* Sample Rate: */
#define MASK_LOOPTHRU	(0x1 << 6)	/* Loopthrough Enable */
#define MASK_CMUTE	(0x1 << 7)	/* Output C (Speaker) Mute when 1 */
#define MASK_SPKMUTE	MASK_CMUTE
#define MASK_ADDR1RES2	(0x1 << 8)	/* Reserved */
#define MASK_AMUTE	(0x1 << 9)	/* Output A (Headphone) Mute when 1 */
#define MASK_HDMUTE	MASK_AMUTE
#define MASK_PAROUT0	(0x1 << 10)	/* Parallel Output 0 */
#define MASK_PAROUT1	(0x2 << 10)	/* Parallel Output 1 */

#define MASK_MIC_BOOST  (0x4)           /* screamer mic boost */

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
#define MASK_INPPORT	(0xf)		/* Input Port */
#define MASK_HDPCONN	8		/* headphone plugged in */

/* Clipping Count Reg Bit Masks */
/* -------- ----- --- --- ----- */
#define MASK_CLIPLEFT	(0xff << 7)	/* Clipping Count, Left Channel */
#define MASK_CLIPRIGHT	(0xff)		/* Clipping Count, Right Channel */

/* DBDMA ChannelStatus Bit Masks */
/* ----- ------------- --- ----- */
#define MASK_CSERR	(0x1 << 7)	/* Error */
#define MASK_EOI	(0x1 << 6)	/* End of Input -- only for Input Channel */
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

/*******************/
/* Burgundy values */
/*******************/

#define MASK_ADDR_BURGUNDY_INPSEL21 (0x11 << 12)
#define MASK_ADDR_BURGUNDY_INPSEL3 (0x12 << 12)

#define MASK_ADDR_BURGUNDY_GAINCH1 (0x13 << 12)
#define MASK_ADDR_BURGUNDY_GAINCH2 (0x14 << 12)
#define MASK_ADDR_BURGUNDY_GAINCH3 (0x15 << 12)
#define MASK_ADDR_BURGUNDY_GAINCH4 (0x16 << 12)

#define MASK_ADDR_BURGUNDY_VOLCH1 (0x20 << 12)
#define MASK_ADDR_BURGUNDY_VOLCH2 (0x21 << 12)
#define MASK_ADDR_BURGUNDY_VOLCH3 (0x22 << 12)
#define MASK_ADDR_BURGUNDY_VOLCH4 (0x23 << 12)

#define MASK_ADDR_BURGUNDY_OUTPUTSELECTS (0x2B << 12)
#define MASK_ADDR_BURGUNDY_OUTPUTENABLES (0x2F << 12)

#define MASK_ADDR_BURGUNDY_MASTER_VOLUME (0x30 << 12)

#define MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES (0x60 << 12)

#define MASK_ADDR_BURGUNDY_ATTENSPEAKER (0x62 << 12)
#define MASK_ADDR_BURGUNDY_ATTENLINEOUT (0x63 << 12)
#define MASK_ADDR_BURGUNDY_ATTENHP (0x64 << 12)

#define MASK_ADDR_BURGUNDY_VOLCD (MASK_ADDR_BURGUNDY_VOLCH1)
#define MASK_ADDR_BURGUNDY_VOLLINE (MASK_ADDR_BURGUNDY_VOLCH2)
#define MASK_ADDR_BURGUNDY_VOLMIC (MASK_ADDR_BURGUNDY_VOLCH3)
#define MASK_ADDR_BURGUNDY_VOLMODEM (MASK_ADDR_BURGUNDY_VOLCH4)

#define MASK_ADDR_BURGUNDY_GAINCD (MASK_ADDR_BURGUNDY_GAINCH1)
#define MASK_ADDR_BURGUNDY_GAINLINE (MASK_ADDR_BURGUNDY_GAINCH2)
#define MASK_ADDR_BURGUNDY_GAINMIC (MASK_ADDR_BURGUNDY_GAINCH3)
#define MASK_ADDR_BURGUNDY_GAINMODEM (MASK_ADDR_BURGUNDY_VOLCH4)


/* These are all default values for the burgundy */
#define DEF_BURGUNDY_INPSEL21 (0xAA)
#define DEF_BURGUNDY_INPSEL3 (0x0A)

#define DEF_BURGUNDY_GAINCD (0x33)
#define DEF_BURGUNDY_GAINLINE (0x44)
#define DEF_BURGUNDY_GAINMIC (0x44)
#define DEF_BURGUNDY_GAINMODEM (0x06)

/* Remember: lowest volume here is 0x9b */
#define DEF_BURGUNDY_VOLCD (0xCCCCCCCC)
#define DEF_BURGUNDY_VOLLINE (0x00000000)
#define DEF_BURGUNDY_VOLMIC (0x00000000)
#define DEF_BURGUNDY_VOLMODEM (0xCCCCCCCC)

#define DEF_BURGUNDY_OUTPUTSELECTS (0x010f010f)
#define DEF_BURGUNDY_OUTPUTENABLES (0x0A)

#define DEF_BURGUNDY_MASTER_VOLUME (0xFFFFFFFF)

#define DEF_BURGUNDY_MORE_OUTPUTENABLES (0x7E)

#define DEF_BURGUNDY_ATTENSPEAKER (0x44)
#define DEF_BURGUNDY_ATTENLINEOUT (0xCC)
#define DEF_BURGUNDY_ATTENHP (0xCC)

/*********************/
/* i2s layout values */
/*********************/

#define I2S_REG_INT_CTL			0x00
#define I2S_REG_SERIAL_FORMAT		0x10
#define I2S_REG_CODEC_MSG_OUT		0x20
#define I2S_REG_CODEC_MSG_IN		0x30
#define I2S_REG_FRAME_COUNT		0x40
#define I2S_REG_FRAME_MATCH		0x50
#define I2S_REG_DATAWORD_SIZES		0x60
#define I2S_REG_PEAKLEVEL_SEL		0x70
#define I2S_REG_PEAKLEVEL_IN0		0x80
#define I2S_REG_PEAKLEVEL_IN1		0x90

#endif /* _AWACS_DEFS_H_ */
