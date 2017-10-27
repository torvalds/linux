#ifndef __SOUND_AK4113_H
#define __SOUND_AK4113_H

/*
 *  Routines for Asahi Kasei AK4113
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
 *  Copyright (c) by Pavel Hofman <pavel.hofman@ivitera.com>,
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

/* AK4113 registers */
/* power down */
#define AK4113_REG_PWRDN	0x00
/* format control */
#define AK4113_REG_FORMAT	0x01
/* input/output control */
#define AK4113_REG_IO0		0x02
/* input/output control */
#define AK4113_REG_IO1		0x03
/* interrupt0 mask */
#define AK4113_REG_INT0_MASK	0x04
/* interrupt1 mask */
#define AK4113_REG_INT1_MASK	0x05
/* DAT mask & DTS select */
#define AK4113_REG_DATDTS	0x06
/* receiver status 0 */
#define AK4113_REG_RCS0		0x07
/* receiver status 1 */
#define AK4113_REG_RCS1		0x08
/* receiver status 2 */
#define AK4113_REG_RCS2		0x09
/* RX channel status byte 0 */
#define AK4113_REG_RXCSB0	0x0a
/* RX channel status byte 1 */
#define AK4113_REG_RXCSB1	0x0b
/* RX channel status byte 2 */
#define AK4113_REG_RXCSB2	0x0c
/* RX channel status byte 3 */
#define AK4113_REG_RXCSB3	0x0d
/* RX channel status byte 4 */
#define AK4113_REG_RXCSB4	0x0e
/* burst preamble Pc byte 0 */
#define AK4113_REG_Pc0		0x0f
/* burst preamble Pc byte 1 */
#define AK4113_REG_Pc1		0x10
/* burst preamble Pd byte 0 */
#define AK4113_REG_Pd0		0x11
/* burst preamble Pd byte 1 */
#define AK4113_REG_Pd1		0x12
/* Q-subcode address + control */
#define AK4113_REG_QSUB_ADDR	0x13
/* Q-subcode track */
#define AK4113_REG_QSUB_TRACK	0x14
/* Q-subcode index */
#define AK4113_REG_QSUB_INDEX	0x15
/* Q-subcode minute */
#define AK4113_REG_QSUB_MINUTE	0x16
/* Q-subcode second */
#define AK4113_REG_QSUB_SECOND	0x17
/* Q-subcode frame */
#define AK4113_REG_QSUB_FRAME	0x18
/* Q-subcode zero */
#define AK4113_REG_QSUB_ZERO	0x19
/* Q-subcode absolute minute */
#define AK4113_REG_QSUB_ABSMIN	0x1a
/* Q-subcode absolute second */
#define AK4113_REG_QSUB_ABSSEC	0x1b
/* Q-subcode absolute frame */
#define AK4113_REG_QSUB_ABSFRM	0x1c

/* sizes */
#define AK4113_REG_RXCSB_SIZE	((AK4113_REG_RXCSB4-AK4113_REG_RXCSB0)+1)
#define AK4113_REG_QSUB_SIZE	((AK4113_REG_QSUB_ABSFRM-AK4113_REG_QSUB_ADDR)\
		+1)

#define AK4113_WRITABLE_REGS	(AK4113_REG_DATDTS + 1)

/* AK4113_REG_PWRDN bits */
/* Channel Status Select */
#define AK4113_CS12		(1<<7)
/* Block Start & C/U Output Mode */
#define AK4113_BCU		(1<<6)
/* Master Clock Operation Select */
#define AK4113_CM1		(1<<5)
/* Master Clock Operation Select */
#define AK4113_CM0		(1<<4)
/* Master Clock Frequency Select */
#define AK4113_OCKS1		(1<<3)
/* Master Clock Frequency Select */
#define AK4113_OCKS0		(1<<2)
/* 0 = power down, 1 = normal operation */
#define AK4113_PWN		(1<<1)
/* 0 = reset & initialize (except thisregister), 1 = normal operation */
#define AK4113_RST		(1<<0)

/* AK4113_REQ_FORMAT bits */
/* V/TX Output select: 0 = Validity Flag Output, 1 = TX */
#define AK4113_VTX		(1<<7)
/* Audio Data Control */
#define AK4113_DIF2		(1<<6)
/* Audio Data Control */
#define AK4113_DIF1		(1<<5)
/* Audio Data Control */
#define AK4113_DIF0		(1<<4)
/* Deemphasis Autodetect Enable (1 = enable) */
#define AK4113_DEAU		(1<<3)
/* 32kHz-48kHz Deemphasis Control */
#define AK4113_DEM1		(1<<2)
/* 32kHz-48kHz Deemphasis Control */
#define AK4113_DEM0		(1<<1)
#define AK4113_DEM_OFF		(AK4113_DEM0)
#define AK4113_DEM_44KHZ	(0)
#define AK4113_DEM_48KHZ	(AK4113_DEM1)
#define AK4113_DEM_32KHZ	(AK4113_DEM0|AK4113_DEM1)
/* STDO: 16-bit, right justified */
#define AK4113_DIF_16R		(0)
/* STDO: 18-bit, right justified */
#define AK4113_DIF_18R		(AK4113_DIF0)
/* STDO: 20-bit, right justified */
#define AK4113_DIF_20R		(AK4113_DIF1)
/* STDO: 24-bit, right justified */
#define AK4113_DIF_24R		(AK4113_DIF1|AK4113_DIF0)
/* STDO: 24-bit, left justified */
#define AK4113_DIF_24L		(AK4113_DIF2)
/* STDO: I2S */
#define AK4113_DIF_24I2S	(AK4113_DIF2|AK4113_DIF0)
/* STDO: 24-bit, left justified; LRCLK, BICK = Input */
#define AK4113_DIF_I24L		(AK4113_DIF2|AK4113_DIF1)
/* STDO: I2S;  LRCLK, BICK = Input */
#define AK4113_DIF_I24I2S	(AK4113_DIF2|AK4113_DIF1|AK4113_DIF0)

/* AK4113_REG_IO0 */
/* XTL1=0,XTL0=0 -> 11.2896Mhz; XTL1=0,XTL0=1 -> 12.288Mhz */
#define AK4113_XTL1		(1<<6)
/* XTL1=1,XTL0=0 -> 24.576Mhz; XTL1=1,XTL0=1 -> use channel status */
#define AK4113_XTL0		(1<<5)
/* Block Start Signal Output: 0 = U-bit, 1 = C-bit (req. BCU = 1) */
#define AK4113_UCE		(1<<4)
/* TX Output Enable (1 = enable) */
#define AK4113_TXE		(1<<3)
/* Output Through Data Selector for TX pin */
#define AK4113_OPS2		(1<<2)
/* Output Through Data Selector for TX pin */
#define AK4113_OPS1		(1<<1)
/* Output Through Data Selector for TX pin */
#define AK4113_OPS0		(1<<0)
/* 11.2896 MHz ref. Xtal freq. */
#define AK4113_XTL_11_2896M	(0)
/* 12.288 MHz ref. Xtal freq. */
#define AK4113_XTL_12_288M	(AK4113_XTL0)
/* 24.576 MHz ref. Xtal freq. */
#define AK4113_XTL_24_576M	(AK4113_XTL1)

/* AK4113_REG_IO1 */
/* Interrupt 0 pin Hold */
#define AK4113_EFH1		(1<<7)
/* Interrupt 0 pin Hold */
#define AK4113_EFH0		(1<<6)
#define AK4113_EFH_512LRCLK	(0)
#define AK4113_EFH_1024LRCLK	(AK4113_EFH0)
#define AK4113_EFH_2048LRCLK	(AK4113_EFH1)
#define AK4113_EFH_4096LRCLK	(AK4113_EFH1|AK4113_EFH0)
/* PLL Lock Time: 0 = 384/fs, 1 = 1/fs */
#define AK4113_FAST		(1<<5)
/* MCKO2 Output Select: 0 = CMx/OCKSx, 1 = Xtal */
#define AK4113_XMCK		(1<<4)
/* MCKO2 Output Freq. Select: 0 = x1, 1 = x0.5  (req. XMCK = 1) */
#define AK4113_DIV		(1<<3)
/* Input Recovery Data Select */
#define AK4113_IPS2		(1<<2)
/* Input Recovery Data Select */
#define AK4113_IPS1		(1<<1)
/* Input Recovery Data Select */
#define AK4113_IPS0		(1<<0)
#define AK4113_IPS(x)		((x)&7)

/* AK4113_REG_INT0_MASK && AK4113_REG_INT1_MASK*/
/* mask enable for QINT bit */
#define AK4113_MQI		(1<<7)
/* mask enable for AUTO bit */
#define AK4113_MAUT		(1<<6)
/* mask enable for CINT bit */
#define AK4113_MCIT		(1<<5)
/* mask enable for UNLOCK bit */
#define AK4113_MULK		(1<<4)
/* mask enable for V bit */
#define AK4113_V		(1<<3)
/* mask enable for STC bit */
#define AK4113_STC		(1<<2)
/* mask enable for AUDN bit */
#define AK4113_MAN		(1<<1)
/* mask enable for PAR bit */
#define AK4113_MPR		(1<<0)

/* AK4113_REG_DATDTS */
/* DAT Start ID Counter */
#define AK4113_DCNT		(1<<4)
/* DTS-CD 16-bit Sync Word Detect */
#define AK4113_DTS16		(1<<3)
/* DTS-CD 14-bit Sync Word Detect */
#define AK4113_DTS14		(1<<2)
/* mask enable for DAT bit (if 1, no INT1 effect */
#define AK4113_MDAT1		(1<<1)
/* mask enable for DAT bit (if 1, no INT0 effect */
#define AK4113_MDAT0		(1<<0)

/* AK4113_REG_RCS0 */
/* Q-subcode buffer interrupt, 0 = no change, 1 = changed */
#define AK4113_QINT		(1<<7)
/* Non-PCM or DTS stream auto detection, 0 = no detect, 1 = detect */
#define AK4113_AUTO		(1<<6)
/* channel status buffer interrupt, 0 = no change, 1 = change */
#define AK4113_CINT		(1<<5)
/* PLL lock status, 0 = lock, 1 = unlock */
#define AK4113_UNLCK		(1<<4)
/* Validity bit, 0 = valid, 1 = invalid */
#define AK4113_V		(1<<3)
/* sampling frequency or Pre-emphasis change, 0 = no detect, 1 = detect */
#define AK4113_STC		(1<<2)
/* audio bit output, 0 = audio, 1 = non-audio */
#define AK4113_AUDION		(1<<1)
/* parity error or biphase error status, 0 = no error, 1 = error */
#define AK4113_PAR		(1<<0)

/* AK4113_REG_RCS1 */
/* sampling frequency detection */
#define AK4113_FS3		(1<<7)
#define AK4113_FS2		(1<<6)
#define AK4113_FS1		(1<<5)
#define AK4113_FS0		(1<<4)
/* Pre-emphasis detect, 0 = OFF, 1 = ON */
#define AK4113_PEM		(1<<3)
/* DAT Start ID Detect, 0 = no detect, 1 = detect */
#define AK4113_DAT		(1<<2)
/* DTS-CD bit audio stream detect, 0 = no detect, 1 = detect */
#define AK4113_DTSCD		(1<<1)
/* Non-PCM bit stream detection, 0 = no detect, 1 = detect */
#define AK4113_NPCM		(1<<0)
#define AK4113_FS_8000HZ	(AK4113_FS3|AK4113_FS0)
#define AK4113_FS_11025HZ	(AK4113_FS2|AK4113_FS0)
#define AK4113_FS_16000HZ	(AK4113_FS2|AK4113_FS1|AK4113_FS0)
#define AK4113_FS_22050HZ	(AK4113_FS2)
#define AK4113_FS_24000HZ	(AK4113_FS2|AK4113_FS1)
#define AK4113_FS_32000HZ	(AK4113_FS1|AK4113_FS0)
#define AK4113_FS_44100HZ	(0)
#define AK4113_FS_48000HZ	(AK4113_FS1)
#define AK4113_FS_64000HZ	(AK4113_FS3|AK4113_FS1|AK4113_FS0)
#define AK4113_FS_88200HZ	(AK4113_FS3)
#define AK4113_FS_96000HZ	(AK4113_FS3|AK4113_FS1)
#define AK4113_FS_176400HZ	(AK4113_FS3|AK4113_FS2)
#define AK4113_FS_192000HZ	(AK4113_FS3|AK4113_FS2|AK4113_FS1)

/* AK4113_REG_RCS2 */
/* CRC for Q-subcode, 0 = no error, 1 = error */
#define AK4113_QCRC		(1<<1)
/* CRC for channel status, 0 = no error, 1 = error */
#define AK4113_CCRC		(1<<0)

/* flags for snd_ak4113_check_rate_and_errors() */
#define AK4113_CHECK_NO_STAT	(1<<0)	/* no statistics */
#define AK4113_CHECK_NO_RATE	(1<<1)	/* no rate check */

#define AK4113_CONTROLS		13

typedef void (ak4113_write_t)(void *private_data, unsigned char addr,
		unsigned char data);
typedef unsigned char (ak4113_read_t)(void *private_data, unsigned char addr);

enum {
	AK4113_PARITY_ERRORS,
	AK4113_V_BIT_ERRORS,
	AK4113_QCRC_ERRORS,
	AK4113_CCRC_ERRORS,
	AK4113_NUM_ERRORS
};

struct ak4113 {
	struct snd_card *card;
	ak4113_write_t *write;
	ak4113_read_t *read;
	void *private_data;
	atomic_t wq_processing;
	struct mutex reinit_mutex;
	spinlock_t lock;
	unsigned char regmap[AK4113_WRITABLE_REGS];
	struct snd_kcontrol *kctls[AK4113_CONTROLS];
	struct snd_pcm_substream *substream;
	unsigned long errors[AK4113_NUM_ERRORS];
	unsigned char rcs0;
	unsigned char rcs1;
	unsigned char rcs2;
	struct delayed_work work;
	unsigned int check_flags;
	void *change_callback_private;
	void (*change_callback)(struct ak4113 *ak4113, unsigned char c0,
			unsigned char c1);
};

int snd_ak4113_create(struct snd_card *card, ak4113_read_t *read,
		ak4113_write_t *write,
		const unsigned char *pgm,
		void *private_data, struct ak4113 **r_ak4113);
void snd_ak4113_reg_write(struct ak4113 *ak4113, unsigned char reg,
		unsigned char mask, unsigned char val);
void snd_ak4113_reinit(struct ak4113 *ak4113);
int snd_ak4113_build(struct ak4113 *ak4113,
		struct snd_pcm_substream *capture_substream);
int snd_ak4113_external_rate(struct ak4113 *ak4113);
int snd_ak4113_check_rate_and_errors(struct ak4113 *ak4113, unsigned int flags);

#ifdef CONFIG_PM
void snd_ak4113_suspend(struct ak4113 *chip);
void snd_ak4113_resume(struct ak4113 *chip);
#else
static inline void snd_ak4113_suspend(struct ak4113 *chip) {}
static inline void snd_ak4113_resume(struct ak4113 *chip) {}
#endif

#endif /* __SOUND_AK4113_H */

