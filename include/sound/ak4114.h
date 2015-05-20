#ifndef __SOUND_AK4114_H
#define __SOUND_AK4114_H

/*
 *  Routines for Asahi Kasei AK4114
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
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

/* AK4114 registers */
#define AK4114_REG_PWRDN	0x00	/* power down */
#define AK4114_REG_FORMAT	0x01	/* format control */
#define AK4114_REG_IO0		0x02	/* input/output control */
#define AK4114_REG_IO1		0x03	/* input/output control */
#define AK4114_REG_INT0_MASK	0x04	/* interrupt0 mask */
#define AK4114_REG_INT1_MASK	0x05	/* interrupt1 mask */
#define AK4114_REG_RCS0		0x06	/* receiver status 0 */
#define AK4114_REG_RCS1		0x07	/* receiver status 1 */
#define AK4114_REG_RXCSB0	0x08	/* RX channel status byte 0 */
#define AK4114_REG_RXCSB1	0x09	/* RX channel status byte 1 */
#define AK4114_REG_RXCSB2	0x0a	/* RX channel status byte 2 */
#define AK4114_REG_RXCSB3	0x0b	/* RX channel status byte 3 */
#define AK4114_REG_RXCSB4	0x0c	/* RX channel status byte 4 */
#define AK4114_REG_TXCSB0	0x0d	/* TX channel status byte 0 */
#define AK4114_REG_TXCSB1	0x0e	/* TX channel status byte 1 */
#define AK4114_REG_TXCSB2	0x0f	/* TX channel status byte 2 */
#define AK4114_REG_TXCSB3	0x10	/* TX channel status byte 3 */
#define AK4114_REG_TXCSB4	0x11	/* TX channel status byte 4 */
#define AK4114_REG_Pc0		0x12	/* burst preamble Pc byte 0 */
#define AK4114_REG_Pc1		0x13	/* burst preamble Pc byte 1 */
#define AK4114_REG_Pd0		0x14	/* burst preamble Pd byte 0 */
#define AK4114_REG_Pd1		0x15	/* burst preamble Pd byte 1 */
#define AK4114_REG_QSUB_ADDR	0x16	/* Q-subcode address + control */
#define AK4114_REG_QSUB_TRACK	0x17	/* Q-subcode track */
#define AK4114_REG_QSUB_INDEX	0x18	/* Q-subcode index */
#define AK4114_REG_QSUB_MINUTE	0x19	/* Q-subcode minute */
#define AK4114_REG_QSUB_SECOND	0x1a	/* Q-subcode second */
#define AK4114_REG_QSUB_FRAME	0x1b	/* Q-subcode frame */
#define AK4114_REG_QSUB_ZERO	0x1c	/* Q-subcode zero */
#define AK4114_REG_QSUB_ABSMIN	0x1d	/* Q-subcode absolute minute */
#define AK4114_REG_QSUB_ABSSEC	0x1e	/* Q-subcode absolute second */
#define AK4114_REG_QSUB_ABSFRM	0x1f	/* Q-subcode absolute frame */

/* sizes */
#define AK4114_REG_RXCSB_SIZE	((AK4114_REG_RXCSB4-AK4114_REG_RXCSB0)+1)
#define AK4114_REG_TXCSB_SIZE	((AK4114_REG_TXCSB4-AK4114_REG_TXCSB0)+1)
#define AK4114_REG_QSUB_SIZE	((AK4114_REG_QSUB_ABSFRM-AK4114_REG_QSUB_ADDR)+1)

/* AK4117_REG_PWRDN bits */
#define AK4114_CS12		(1<<7)	/* Channel Status Select */
#define AK4114_BCU		(1<<6)	/* Block Start & C/U Output Mode */
#define AK4114_CM1		(1<<5)	/* Master Clock Operation Select */
#define AK4114_CM0		(1<<4)	/* Master Clock Operation Select */
#define AK4114_OCKS1		(1<<3)	/* Master Clock Frequency Select */
#define AK4114_OCKS0		(1<<2)	/* Master Clock Frequency Select */
#define AK4114_PWN		(1<<1)	/* 0 = power down, 1 = normal operation */
#define AK4114_RST		(1<<0)	/* 0 = reset & initialize (except this register), 1 = normal operation */

/* AK4114_REQ_FORMAT bits */
#define AK4114_MONO		(1<<7)	/* Double Sampling Frequency Mode: 0 = stereo, 1 = mono */
#define AK4114_DIF2		(1<<6)	/* Audio Data Control */
#define AK4114_DIF1		(1<<5)	/* Audio Data Control */
#define AK4114_DIF0		(1<<4)	/* Audio Data Control */
#define AK4114_DIF_16R		(0)				/* STDO: 16-bit, right justified */
#define AK4114_DIF_18R		(AK4114_DIF0)			/* STDO: 18-bit, right justified */
#define AK4114_DIF_20R		(AK4114_DIF1)			/* STDO: 20-bit, right justified */
#define AK4114_DIF_24R		(AK4114_DIF1|AK4114_DIF0)	/* STDO: 24-bit, right justified */
#define AK4114_DIF_24L		(AK4114_DIF2)			/* STDO: 24-bit, left justified */
#define AK4114_DIF_24I2S	(AK4114_DIF2|AK4114_DIF0)	/* STDO: I2S */
#define AK4114_DIF_I24L		(AK4114_DIF2|AK4114_DIF1)	/* STDO: 24-bit, left justified; LRCLK, BICK = Input */
#define AK4114_DIF_I24I2S	(AK4114_DIF2|AK4114_DIF1|AK4114_DIF0) /* STDO: I2S;  LRCLK, BICK = Input */
#define AK4114_DEAU		(1<<3)	/* Deemphasis Autodetect Enable (1 = enable) */
#define AK4114_DEM1		(1<<2)	/* 32kHz-48kHz Deemphasis Control */
#define AK4114_DEM0		(1<<1)	/* 32kHz-48kHz Deemphasis Control */
#define AK4114_DEM_44KHZ	(0)
#define AK4114_DEM_48KHZ	(AK4114_DEM1)
#define AK4114_DEM_32KHZ	(AK4114_DEM0|AK4114_DEM1)
#define AK4114_DEM_96KHZ	(AK4114_DEM1)	/* DFS must be set */
#define AK4114_DFS		(1<<0)	/* 96kHz Deemphasis Control */

/* AK4114_REG_IO0 */
#define AK4114_TX1E		(1<<7)	/* TX1 Output Enable (1 = enable) */
#define AK4114_OPS12		(1<<6)	/* Output Data Selector for TX1 pin */
#define AK4114_OPS11		(1<<5)	/* Output Data Selector for TX1 pin */
#define AK4114_OPS10		(1<<4)	/* Output Data Selector for TX1 pin */
#define AK4114_TX0E		(1<<3)	/* TX0 Output Enable (1 = enable) */
#define AK4114_OPS02		(1<<2)	/* Output Data Selector for TX0 pin */
#define AK4114_OPS01		(1<<1)	/* Output Data Selector for TX0 pin */
#define AK4114_OPS00		(1<<0)	/* Output Data Selector for TX0 pin */

/* AK4114_REG_IO1 */
#define AK4114_EFH1		(1<<7)	/* Interrupt 0 pin Hold */
#define AK4114_EFH0		(1<<6)	/* Interrupt 0 pin Hold */
#define AK4114_EFH_512		(0)
#define AK4114_EFH_1024		(AK4114_EFH0)
#define AK4114_EFH_2048		(AK4114_EFH1)
#define AK4114_EFH_4096		(AK4114_EFH1|AK4114_EFH0)
#define AK4114_UDIT		(1<<5)	/* U-bit Control for DIT (0 = fixed '0', 1 = recovered) */
#define AK4114_TLR		(1<<4)	/* Double Sampling Frequency Select for DIT (0 = L channel, 1 = R channel) */
#define AK4114_DIT		(1<<3)	/* TX1 out: 0 = Through Data (RX data), 1 = Transmit Data (DAUX data) */
#define AK4114_IPS2		(1<<2)	/* Input Recovery Data Select */
#define AK4114_IPS1		(1<<1)	/* Input Recovery Data Select */
#define AK4114_IPS0		(1<<0)	/* Input Recovery Data Select */
#define AK4114_IPS(x)		((x)&7)

/* AK4114_REG_INT0_MASK && AK4114_REG_INT1_MASK*/
#define AK4117_MQI              (1<<7)  /* mask enable for QINT bit */
#define AK4117_MAT              (1<<6)  /* mask enable for AUTO bit */
#define AK4117_MCI              (1<<5)  /* mask enable for CINT bit */
#define AK4117_MUL              (1<<4)  /* mask enable for UNLOCK bit */
#define AK4117_MDTS             (1<<3)  /* mask enable for DTSCD bit */
#define AK4117_MPE              (1<<2)  /* mask enable for PEM bit */
#define AK4117_MAN              (1<<1)  /* mask enable for AUDN bit */
#define AK4117_MPR              (1<<0)  /* mask enable for PAR bit */

/* AK4114_REG_RCS0 */
#define AK4114_QINT		(1<<7)	/* Q-subcode buffer interrupt, 0 = no change, 1 = changed */
#define AK4114_AUTO		(1<<6)	/* Non-PCM or DTS stream auto detection, 0 = no detect, 1 = detect */
#define AK4114_CINT		(1<<5)	/* channel status buffer interrupt, 0 = no change, 1 = change */
#define AK4114_UNLCK		(1<<4)	/* PLL lock status, 0 = lock, 1 = unlock */
#define AK4114_DTSCD		(1<<3)	/* DTS-CD Detect, 0 = No detect, 1 = Detect */
#define AK4114_PEM		(1<<2)	/* Pre-emphasis Detect, 0 = OFF, 1 = ON */
#define AK4114_AUDION		(1<<1)	/* audio bit output, 0 = audio, 1 = non-audio */
#define AK4114_PAR		(1<<0)	/* parity error or biphase error status, 0 = no error, 1 = error */

/* AK4114_REG_RCS1 */
#define AK4114_FS3		(1<<7)	/* sampling frequency detection */
#define AK4114_FS2		(1<<6)
#define AK4114_FS1		(1<<5)
#define AK4114_FS0		(1<<4)
#define AK4114_FS_44100HZ	(0)
#define AK4114_FS_48000HZ	(AK4114_FS1)
#define AK4114_FS_32000HZ	(AK4114_FS1|AK4114_FS0)
#define AK4114_FS_88200HZ	(AK4114_FS3)
#define AK4114_FS_96000HZ	(AK4114_FS3|AK4114_FS1)
#define AK4114_FS_176400HZ	(AK4114_FS3|AK4114_FS2)
#define AK4114_FS_192000HZ	(AK4114_FS3|AK4114_FS2|AK4114_FS1)
#define AK4114_V		(1<<3)	/* Validity of Channel Status, 0 = Valid, 1 = Invalid */
#define AK4114_QCRC		(1<<1)	/* CRC for Q-subcode, 0 = no error, 1 = error */
#define AK4114_CCRC		(1<<0)	/* CRC for channel status, 0 = no error, 1 = error */

/* flags for snd_ak4114_check_rate_and_errors() */
#define AK4114_CHECK_NO_STAT	(1<<0)	/* no statistics */
#define AK4114_CHECK_NO_RATE	(1<<1)	/* no rate check */

#define AK4114_CONTROLS		15

typedef void (ak4114_write_t)(void *private_data, unsigned char addr, unsigned char data);
typedef unsigned char (ak4114_read_t)(void *private_data, unsigned char addr);

struct ak4114 {
	struct snd_card *card;
	ak4114_write_t * write;
	ak4114_read_t * read;
	void * private_data;
	atomic_t wq_processing;
	spinlock_t lock;
	unsigned char regmap[7];
	unsigned char txcsb[5];
	struct snd_kcontrol *kctls[AK4114_CONTROLS];
	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;
	unsigned long parity_errors;
	unsigned long v_bit_errors;
	unsigned long qcrc_errors;
	unsigned long ccrc_errors;
	unsigned char rcs0;
	unsigned char rcs1;
	struct delayed_work work;
	unsigned int check_flags;
	void *change_callback_private;
	void (*change_callback)(struct ak4114 *ak4114, unsigned char c0, unsigned char c1);
};

int snd_ak4114_create(struct snd_card *card,
		      ak4114_read_t *read, ak4114_write_t *write,
		      const unsigned char pgm[7], const unsigned char txcsb[5],
		      void *private_data, struct ak4114 **r_ak4114);
void snd_ak4114_reg_write(struct ak4114 *ak4114, unsigned char reg, unsigned char mask, unsigned char val);
void snd_ak4114_reinit(struct ak4114 *ak4114);
int snd_ak4114_build(struct ak4114 *ak4114,
		     struct snd_pcm_substream *playback_substream,
                     struct snd_pcm_substream *capture_substream);
int snd_ak4114_external_rate(struct ak4114 *ak4114);
int snd_ak4114_check_rate_and_errors(struct ak4114 *ak4114, unsigned int flags);

#endif /* __SOUND_AK4114_H */

