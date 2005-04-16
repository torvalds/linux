#ifndef __SOUND_AK4117_H
#define __SOUND_AK4117_H

/*
 *  Routines for Asahi Kasei AK4117
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

#define AK4117_REG_PWRDN	0x00	/* power down */
#define AK4117_REG_CLOCK	0x01	/* clock control */
#define AK4117_REG_IO		0x02	/* input/output control */
#define AK4117_REG_INT0_MASK	0x03	/* interrupt0 mask */
#define AK4117_REG_INT1_MASK	0x04	/* interrupt1 mask */
#define AK4117_REG_RCS0		0x05	/* receiver status 0 */
#define AK4117_REG_RCS1		0x06	/* receiver status 1 */
#define AK4117_REG_RCS2		0x07	/* receiver status 2 */
#define AK4117_REG_RXCSB0	0x08	/* RX channel status byte 0 */
#define AK4117_REG_RXCSB1	0x09	/* RX channel status byte 1 */
#define AK4117_REG_RXCSB2	0x0a	/* RX channel status byte 2 */
#define AK4117_REG_RXCSB3	0x0b	/* RX channel status byte 3 */
#define AK4117_REG_RXCSB4	0x0c	/* RX channel status byte 4 */
#define AK4117_REG_Pc0		0x0d	/* burst preamble Pc byte 0 */
#define AK4117_REG_Pc1		0x0e	/* burst preamble Pc byte 1 */
#define AK4117_REG_Pd0		0x0f	/* burst preamble Pd byte 0 */
#define AK4117_REG_Pd1		0x10	/* burst preamble Pd byte 1 */
#define AK4117_REG_QSUB_ADDR	0x11	/* Q-subcode address + control */
#define AK4117_REG_QSUB_TRACK	0x12	/* Q-subcode track */
#define AK4117_REG_QSUB_INDEX	0x13	/* Q-subcode index */
#define AK4117_REG_QSUB_MINUTE	0x14	/* Q-subcode minute */
#define AK4117_REG_QSUB_SECOND	0x15	/* Q-subcode second */
#define AK4117_REG_QSUB_FRAME	0x16	/* Q-subcode frame */
#define AK4117_REG_QSUB_ZERO	0x17	/* Q-subcode zero */
#define AK4117_REG_QSUB_ABSMIN	0x18	/* Q-subcode absolute minute */
#define AK4117_REG_QSUB_ABSSEC	0x19	/* Q-subcode absolute second */
#define AK4117_REG_QSUB_ABSFRM	0x1a	/* Q-subcode absolute frame */

/* sizes */
#define AK4117_REG_RXCSB_SIZE	((AK4117_REG_RXCSB4-AK4117_REG_RXCSB0)+1)
#define AK4117_REG_QSUB_SIZE	((AK4117_REG_QSUB_ABSFRM-AK4117_REG_QSUB_ADDR)+1)

/* AK4117_REG_PWRDN bits */
#define AK4117_EXCT		(1<<4)	/* 0 = X'tal mode, 1 = external clock mode */
#define AK4117_XTL1		(1<<3)	/* XTL1=0,XTL0=0 -> 11.2896Mhz; XTL1=0,XTL0=1 -> 12.288Mhz */
#define AK4117_XTL0		(1<<2)	/* XTL1=1,XTL0=0 -> 24.576Mhz; XTL1=1,XTL0=1 -> use channel status */
#define AK4117_XTL_11_2896M	(0)
#define AK4117_XTL_12_288M	AK4117_XTL0
#define AK4117_XTL_24_576M	AK4117_XTL1
#define AK4117_XTL_EXT		(AK4117_XTL1|AK4117_XTL0)
#define AK4117_PWN		(1<<1)	/* 0 = power down, 1 = normal operation */
#define AK4117_RST		(1<<0)	/* 0 = reset & initialize (except this register), 1 = normal operation */

/* AK4117_REQ_CLOCK bits */
#define AK4117_LP		(1<<7)	/* 0 = normal mode, 1 = low power mode (Fs up to 48kHz only) */
#define AK4117_PKCS1		(1<<6)	/* master clock frequency at PLL mode (when LP == 0) */
#define AK4117_PKCS0		(1<<5)
#define AK4117_PKCS_512fs	(0)
#define AK4117_PKCS_256fs	AK4117_PKCS0
#define AK4117_PKCS_128fs	AK4117_PKCS1
#define AK4117_DIV		(1<<4)	/* 0 = MCKO == Fs, 1 = MCKO == Fs / 2; X'tal mode only */
#define AK4117_XCKS1		(1<<3)	/* master clock frequency at X'tal mode */
#define AK4117_XCKS0		(1<<2)
#define AK4117_XCKS_128fs	(0)
#define AK4117_XCKS_256fs	AK4117_XCKS0
#define AK4117_XCKS_512fs	AK4117_XCKS1
#define AK4117_XCKS_1024fs	(AK4117_XCKS1|AK4117_XCKS0)
#define AK4117_CM1		(1<<1)	/* MCKO operation mode select */
#define AK4117_CM0		(1<<0)
#define AK4117_CM_PLL		(0)		/* use RX input as master clock */
#define AK4117_CM_XTAL		(AK4117_CM0)	/* use X'tal as master clock */
#define AK4117_CM_PLL_XTAL	(AK4117_CM1)	/* use Rx input but X'tal when PLL loses lock */
#define AK4117_CM_MONITOR	(AK4117_CM0|AK4117_CM1) /* use X'tal as master clock, but use PLL for monitoring */

/* AK4117_REG_IO */
#define AK4117_IPS		(1<<7)	/* Input Recovery Data Select, 0 = RX0, 1 = RX1 */
#define AK4117_UOUTE		(1<<6)	/* U-bit output enable to UOUT, 0 = disable, 1 = enable */
#define AK4117_CS12		(1<<5)	/* channel status select, 0 = channel1, 1 = channel2 */
#define AK4117_EFH2		(1<<4)	/* INT0 pin hold count select */
#define AK4117_EFH1		(1<<3)
#define AK4117_EFH_512LRCLK	(0)
#define AK4117_EFH_1024LRCLK	(AK4117_EFH1)
#define AK4117_EFH_2048LRCLK	(AK4117_EFH2)
#define AK4117_EFH_4096LRCLK	(AK4117_EFH1|AK4117_EFH2)
#define AK4117_DIF2		(1<<2)	/* audio data format control */
#define AK4117_DIF1		(1<<1)
#define AK4117_DIF0		(1<<0)
#define AK4117_DIF_16R		(0)				/* STDO: 16-bit, right justified */
#define AK4117_DIF_18R		(AK4117_DIF0)			/* STDO: 18-bit, right justified */
#define AK4117_DIF_20R		(AK4117_DIF1)			/* STDO: 20-bit, right justified */
#define AK4117_DIF_24R		(AK4117_DIF1|AK4117_DIF0)	/* STDO: 24-bit, right justified */
#define AK4117_DIF_24L		(AK4117_DIF2)			/* STDO: 24-bit, left justified */
#define AK4117_DIF_24I2S	(AK4117_DIF2|AK4117_DIF0)	/* STDO: I2S */

/* AK4117_REG_INT0_MASK & AK4117_REG_INT1_MASK */
#define AK4117_MULK		(1<<7)	/* mask enable for UNLOCK bit */
#define AK4117_MPAR		(1<<6)	/* mask enable for PAR bit */
#define AK4117_MAUTO		(1<<5)	/* mask enable for AUTO bit */
#define AK4117_MV		(1<<4)	/* mask enable for V bit */
#define AK4117_MAUD		(1<<3)	/* mask enable for AUDION bit */
#define AK4117_MSTC		(1<<2)	/* mask enable for STC bit */
#define AK4117_MCIT		(1<<1)	/* mask enable for CINT bit */
#define AK4117_MQIT		(1<<0)	/* mask enable for QINT bit */

/* AK4117_REG_RCS0 */
#define AK4117_UNLCK		(1<<7)	/* PLL lock status, 0 = lock, 1 = unlock */
#define AK4117_PAR		(1<<6)	/* parity error or biphase error status, 0 = no error, 1 = error */
#define AK4117_AUTO		(1<<5)	/* Non-PCM or DTS stream auto detection, 0 = no detect, 1 = detect */
#define AK4117_V		(1<<4)	/* Validity bit, 0 = valid, 1 = invalid */
#define AK4117_AUDION		(1<<3)	/* audio bit output, 0 = audio, 1 = non-audio */
#define AK4117_STC		(1<<2)	/* sampling frequency or Pre-emphasis change, 0 = no detect, 1 = detect */
#define AK4117_CINT		(1<<1)	/* channel status buffer interrupt, 0 = no change, 1 = change */
#define AK4117_QINT		(1<<0)	/* Q-subcode buffer interrupt, 0 = no change, 1 = changed */

/* AK4117_REG_RCS1 */
#define AK4117_DTSCD		(1<<6)	/* DTS-CD bit audio stream detect, 0 = no detect, 1 = detect */
#define AK4117_NPCM		(1<<5)	/* Non-PCM bit stream detection, 0 = no detect, 1 = detect */
#define AK4117_PEM		(1<<4)	/* Pre-emphasis detect, 0 = OFF, 1 = ON */
#define AK4117_FS3		(1<<3)	/* sampling frequency detection */
#define AK4117_FS2		(1<<2)
#define AK4117_FS1		(1<<1)
#define AK4117_FS0		(1<<0)
#define AK4117_FS_44100HZ	(0)
#define AK4117_FS_48000HZ	(AK4117_FS1)
#define AK4117_FS_32000HZ	(AK4117_FS1|AK4117_FS0)
#define AK4117_FS_88200HZ	(AK4117_FS3)
#define AK4117_FS_96000HZ	(AK4117_FS3|AK4117_FS1)
#define AK4117_FS_176400HZ	(AK4117_FS3|AK4117_FS2)
#define AK4117_FS_192000HZ	(AK4117_FS3|AK4117_FS2|AK4117_FS1)

/* AK4117_REG_RCS2 */
#define AK4117_CCRC		(1<<1)	/* CRC for channel status, 0 = no error, 1 = error */
#define AK4117_QCRC		(1<<0)	/* CRC for Q-subcode, 0 = no error, 1 = error */

/* flags for snd_ak4117_check_rate_and_errors() */
#define AK4117_CHECK_NO_STAT	(1<<0)	/* no statistics */
#define AK4117_CHECK_NO_RATE	(1<<1)	/* no rate check */

#define AK4117_CONTROLS		13

typedef void (ak4117_write_t)(void *private_data, unsigned char addr, unsigned char data);
typedef unsigned char (ak4117_read_t)(void *private_data, unsigned char addr);

typedef struct ak4117 ak4117_t;

struct ak4117 {
	snd_card_t * card;
	ak4117_write_t * write;
	ak4117_read_t * read;
	void * private_data;
	unsigned int init: 1;
	spinlock_t lock;
	unsigned char regmap[5];
	snd_kcontrol_t *kctls[AK4117_CONTROLS];
	snd_pcm_substream_t *substream;
	unsigned long parity_errors;
	unsigned long v_bit_errors;
	unsigned long qcrc_errors;
	unsigned long ccrc_errors;
	unsigned char rcs0;
	unsigned char rcs1;
	unsigned char rcs2;
	struct timer_list timer;	/* statistic timer */
	void *change_callback_private;
	void (*change_callback)(ak4117_t *ak4117, unsigned char c0, unsigned char c1);
};

int snd_ak4117_create(snd_card_t *card, ak4117_read_t *read, ak4117_write_t *write,
		      unsigned char pgm[5], void *private_data, ak4117_t **r_ak4117);
void snd_ak4117_reg_write(ak4117_t *ak4117, unsigned char reg, unsigned char mask, unsigned char val);
void snd_ak4117_reinit(ak4117_t *ak4117);
int snd_ak4117_build(ak4117_t *ak4117, snd_pcm_substream_t *capture_substream);
int snd_ak4117_external_rate(ak4117_t *ak4117);
int snd_ak4117_check_rate_and_errors(ak4117_t *ak4117, unsigned int flags);

#endif /* __SOUND_AK4117_H */

