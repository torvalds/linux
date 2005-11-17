#ifndef __SOUND_CS8427_H
#define __SOUND_CS8427_H

/*
 *  Routines for Cirrus Logic CS8427
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>,
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/i2c.h>

#define CS8427_BASE_ADDR	0x10	/* base I2C address */

#define CS8427_REG_AUTOINC	0x80	/* flag - autoincrement */
#define CS8427_REG_CONTROL1	0x01
#define CS8427_REG_CONTROL2	0x02
#define CS8427_REG_DATAFLOW	0x03
#define CS8427_REG_CLOCKSOURCE	0x04
#define CS8427_REG_SERIALINPUT	0x05
#define CS8427_REG_SERIALOUTPUT	0x06
#define CS8427_REG_INT1STATUS	0x07
#define CS8427_REG_INT2STATUS	0x08
#define CS8427_REG_INT1MASK	0x09
#define CS8427_REG_INT1MODEMSB	0x0a
#define CS8427_REG_INT1MODELSB	0x0b
#define CS8427_REG_INT2MASK	0x0c
#define CS8427_REG_INT2MODEMSB	0x0d
#define CS8427_REG_INT2MODELSB	0x0e
#define CS8427_REG_RECVCSDATA	0x0f
#define CS8427_REG_RECVERRORS	0x10
#define CS8427_REG_RECVERRMASK	0x11
#define CS8427_REG_CSDATABUF	0x12
#define CS8427_REG_UDATABUF	0x13
#define CS8427_REG_QSUBCODE	0x14	/* 0x14-0x1d (10 bytes) */
#define CS8427_REG_OMCKRMCKRATIO 0x1e
#define CS8427_REG_CORU_DATABUF	0x20	/* 24 byte buffer area */
#define CS8427_REG_ID_AND_VER	0x7f

/* CS8427_REG_CONTROL1 bits */
#define CS8427_SWCLK		(1<<7)	/* 0 = RMCK default, 1 = OMCK output on RMCK pin */
#define CS8427_VSET		(1<<6)	/* 0 = valid PCM data, 1 = invalid PCM data */
#define CS8427_MUTESAO		(1<<5)	/* mute control for the serial audio output port, 0 = disabled, 1 = enabled */
#define CS8427_MUTEAES		(1<<4)	/* mute control for the AES transmitter output, 0 = disabled, 1 = enabled */
#define CS8427_INTMASK		(3<<1)	/* interrupt output pin setup mask */
#define CS8427_INTACTHIGH	(0<<1)	/* active high */
#define CS8427_INTACTLOW	(1<<1)	/* active low */
#define CS8427_INTOPENDRAIN	(2<<1)	/* open drain, active low */
#define CS8427_TCBLDIR		(1<<0)	/* 0 = TCBL is an input, 1 = TCBL is an output */

/* CS8427_REQ_CONTROL2 bits */
#define CS8427_HOLDMASK		(3<<5)	/* action when a receiver error occurs */
#define CS8427_HOLDLASTSAMPLE	(0<<5)	/* hold the last valid sample */
#define CS8427_HOLDZERO		(1<<5)	/* replace the current audio sample with zero (mute) */
#define CS8427_HOLDNOCHANGE	(2<<5)	/* do not change the received audio sample */
#define CS8427_RMCKF		(1<<4)	/* 0 = 256*Fsi, 1 = 128*Fsi */
#define CS8427_MMR		(1<<3)	/* AES3 receiver operation, 0 = stereo, 1 = mono */
#define CS8427_MMT		(1<<2)	/* AES3 transmitter operation, 0 = stereo, 1 = mono */
#define CS8427_MMTCS		(1<<1)	/* 0 = use A + B CS data, 1 = use MMTLR CS data */
#define CS8427_MMTLR		(1<<0)	/* 0 = use A CS data, 1 = use B CS data */

/* CS8427_REG_DATAFLOW */
#define CS8427_TXOFF		(1<<6)	/* AES3 transmitter Output, 0 = normal operation, 1 = off (0V) */
#define CS8427_AESBP		(1<<5)	/* AES3 hardware bypass mode, 0 = normal, 1 = bypass (RX->TX) */
#define CS8427_TXDMASK		(3<<3)	/* AES3 Transmitter Data Source Mask */
#define CS8427_TXDSERIAL	(1<<3)	/* TXD - serial audio input port */
#define CS8427_TXAES3DRECEIVER	(2<<3)	/* TXD - AES3 receiver */
#define CS8427_SPDMASK		(3<<1)	/* Serial Audio Output Port Data Source Mask */
#define CS8427_SPDSERIAL	(1<<1)	/* SPD - serial audio input port */
#define CS8427_SPDAES3RECEIVER	(2<<1)	/* SPD - AES3 receiver */

/* CS8427_REG_CLOCKSOURCE */
#define CS8427_RUN		(1<<6)	/* 0 = clock off, 1 = clock on */
#define CS8427_CLKMASK		(3<<4)	/* OMCK frequency mask */
#define CS8427_CLK256		(0<<4)	/* 256*Fso */
#define CS8427_CLK384		(1<<4)	/* 384*Fso */
#define CS8427_CLK512		(2<<4)	/* 512*Fso */
#define CS8427_OUTC		(1<<3)	/* Output Time Base, 0 = OMCK, 1 = recovered input clock */
#define CS8427_INC		(1<<2)	/* Input Time Base Clock Source, 0 = recoverd input clock, 1 = OMCK input pin */
#define CS8427_RXDMASK		(3<<0)	/* Recovered Input Clock Source Mask */
#define CS8427_RXDILRCK		(0<<0)	/* 256*Fsi from ILRCK pin */
#define CS8427_RXDAES3INPUT	(1<<0)	/* 256*Fsi from AES3 input */
#define CS8427_EXTCLOCKRESET	(2<<0)	/* bypass PLL, 256*Fsi clock, synchronous reset */
#define CS8427_EXTCLOCK		(3<<0)	/* bypass PLL, 256*Fsi clock */

/* CS8427_REG_SERIALINPUT */
#define CS8427_SIMS		(1<<7)	/* 0 = slave, 1 = master mode */
#define CS8427_SISF		(1<<6)	/* ISCLK freq, 0 = 64*Fsi, 1 = 128*Fsi */
#define CS8427_SIRESMASK	(3<<4)	/* Resolution of the input data for right justified formats */
#define CS8427_SIRES24		(0<<4)	/* SIRES 24-bit */
#define CS8427_SIRES20		(1<<4)	/* SIRES 20-bit */
#define CS8427_SIRES16		(2<<4)	/* SIRES 16-bit */
#define CS8427_SIJUST		(1<<3)	/* Justification of SDIN data relative to ILRCK, 0 = left-justified, 1 = right-justified */
#define CS8427_SIDEL		(1<<2)	/* Delay of SDIN data relative to ILRCK for left-justified data formats, 0 = first ISCLK period, 1 = second ISCLK period */
#define CS8427_SISPOL		(1<<1)	/* ICLK clock polarity, 0 = rising edge of ISCLK, 1 = falling edge of ISCLK */
#define CS8427_SILRPOL		(1<<0)	/* ILRCK clock polarity, 0 = SDIN data left channel when ILRCK is high, 1 = SDIN right when ILRCK is high */

/* CS8427_REG_SERIALOUTPUT */
#define CS8427_SOMS		(1<<7)	/* 0 = slave, 1 = master mode */
#define CS8427_SOSF		(1<<6)	/* OSCLK freq, 0 = 64*Fso, 1 = 128*Fso */
#define CS8427_SORESMASK	(3<<4)	/* Resolution of the output data on SDOUT and AES3 output */
#define CS8427_SORES24		(0<<4)	/* SIRES 24-bit */
#define CS8427_SORES20		(1<<4)	/* SIRES 20-bit */
#define CS8427_SORES16		(2<<4)	/* SIRES 16-bit */
#define CS8427_SORESDIRECT	(2<<4)	/* SIRES direct copy from AES3 receiver */
#define CS8427_SOJUST		(1<<3)	/* Justification of SDOUT data relative to OLRCK, 0 = left-justified, 1 = right-justified */
#define CS8427_SODEL		(1<<2)	/* Delay of SDOUT data relative to OLRCK for left-justified data formats, 0 = first OSCLK period, 1 = second OSCLK period */
#define CS8427_SOSPOL		(1<<1)	/* OSCLK clock polarity, 0 = rising edge of ISCLK, 1 = falling edge of ISCLK */
#define CS8427_SOLRPOL		(1<<0)	/* OLRCK clock polarity, 0 = SDOUT data left channel when OLRCK is high, 1 = SDOUT right when OLRCK is high */

/* CS8427_REG_INT1STATUS */
#define CS8427_TSLIP		(1<<7)	/* AES3 transmitter source data slip interrupt */
#define CS8427_OSLIP		(1<<6)	/* Serial audio output port data slip interrupt */
#define CS8427_DETC		(1<<2)	/* D to E C-buffer transfer interrupt */
#define CS8427_EFTC		(1<<1)	/* E to F C-buffer transfer interrupt */
#define CS8427_RERR		(1<<0)	/* A receiver error has occurred */

/* CS8427_REG_INT2STATUS */
#define CS8427_DETU		(1<<3)	/* D to E U-buffer transfer interrupt */
#define CS8427_EFTU		(1<<2)	/* E to F U-buffer transfer interrupt */
#define CS8427_QCH		(1<<1)	/* A new block of Q-subcode data is available for reading */

/* CS8427_REG_INT1MODEMSB && CS8427_REG_INT1MODELSB */
/* bits are defined in CS8427_REG_INT1STATUS */
/* CS8427_REG_INT2MODEMSB && CS8427_REG_INT2MODELSB */
/* bits are defined in CS8427_REG_INT2STATUS */
#define CS8427_INTMODERISINGMSB	0
#define CS8427_INTMODERESINGLSB	0
#define CS8427_INTMODEFALLINGMSB 0
#define CS8427_INTMODEFALLINGLSB 1
#define CS8427_INTMODELEVELMSB	1
#define CS8427_INTMODELEVELLSB	0

/* CS8427_REG_RECVCSDATA */
#define CS8427_AUXMASK		(15<<4)	/* auxiliary data field width */
#define CS8427_AUXSHIFT		4
#define CS8427_PRO		(1<<3)	/* Channel status block format indicator */
#define CS8427_AUDIO		(1<<2)	/* Audio indicator (0 = audio, 1 = nonaudio */
#define CS8427_COPY		(1<<1)	/* 0 = copyright asserted, 1 = copyright not asserted */
#define CS8427_ORIG		(1<<0)	/* SCMS generation indicator, 0 = 1st generation or highter, 1 = original */

/* CS8427_REG_RECVERRORS */
/* CS8427_REG_RECVERRMASK for CS8427_RERR */
#define CS8427_QCRC		(1<<6)	/* Q-subcode data CRC error indicator */
#define CS8427_CCRC		(1<<5)	/* Chancnel Status Block Cyclick Redundancy Check Bit */
#define CS8427_UNLOCK		(1<<4)	/* PLL lock status bit */
#define CS8427_V		(1<<3)	/* 0 = valid data */
#define CS8427_CONF		(1<<2)	/* Confidence bit */
#define CS8427_BIP		(1<<1)	/* Bi-phase error bit */
#define CS8427_PAR		(1<<0)	/* Parity error */

/* CS8427_REG_CSDATABUF	*/
#define CS8427_BSEL		(1<<5)	/* 0 = CS data, 1 = U data */
#define CS8427_CBMR		(1<<4)	/* 0 = overwrite first 5 bytes for CS D to E buffer, 1 = prevent */
#define CS8427_DETCI		(1<<3)	/* D to E CS data buffer transfer inhibit bit, 0 = allow, 1 = inhibit */
#define CS8427_EFTCI		(1<<2)	/* E to F CS data buffer transfer inhibit bit, 0 = allow, 1 = inhibit */
#define CS8427_CAM		(1<<1)	/* CS data buffer control port access mode bit, 0 = one byte, 1 = two byte */
#define CS8427_CHS		(1<<0)	/* Channel select bit, 0 = Channel A, 1 = Channel B */

/* CS8427_REG_UDATABUF */
#define CS8427_UD		(1<<4)	/* User data pin (U) direction, 0 = input, 1 = output */
#define CS8427_UBMMASK		(3<<2)	/* Operating mode of the AES3 U bit manager */
#define CS8427_UBMZEROS		(0<<2)	/* transmit all zeros mode */
#define CS8427_UBMBLOCK		(1<<2)	/* block mode */
#define CS8427_DETUI		(1<<1)	/* D to E U-data buffer transfer inhibit bit, 0 = allow, 1 = inhibit */
#define CS8427_EFTUI		(1<<1)	/* E to F U-data buffer transfer inhibit bit, 0 = allow, 1 = inhibit */

/* CS8427_REG_ID_AND_VER */
#define CS8427_IDMASK		(15<<4)
#define CS8427_IDSHIFT		4
#define CS8427_VERMASK		(15<<0)
#define CS8427_VERSHIFT		0
#define CS8427_VER8427A		0x71

struct snd_pcm_substream;

int snd_cs8427_create(struct snd_i2c_bus *bus, unsigned char addr,
		      unsigned int reset_timeout, struct snd_i2c_device **r_cs8427);
int snd_cs8427_reg_write(struct snd_i2c_device *device, unsigned char reg,
			 unsigned char val);
int snd_cs8427_iec958_build(struct snd_i2c_device *cs8427,
			    struct snd_pcm_substream *playback_substream,
			    struct snd_pcm_substream *capture_substream);
int snd_cs8427_iec958_active(struct snd_i2c_device *cs8427, int active);
int snd_cs8427_iec958_pcm(struct snd_i2c_device *cs8427, unsigned int rate);

#endif /* __SOUND_CS8427_H */
