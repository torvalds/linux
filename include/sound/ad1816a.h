/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_AD1816A_H
#define __SOUND_AD1816A_H

/*
    ad1816a.h - definitions for ADI SoundPort AD1816A chip.
    Copyright (C) 1999-2000 by Massimo Piccioni <dafastidio@libero.it>

*/

#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/timer.h>

#define AD1816A_REG(r)			(chip->port + r)

#define AD1816A_CHIP_STATUS		0x00
#define AD1816A_INDIR_ADDR		0x00
#define AD1816A_INTERRUPT_STATUS	0x01
#define AD1816A_INDIR_DATA_LOW		0x02
#define AD1816A_INDIR_DATA_HIGH		0x03
#define AD1816A_PIO_DEBUG		0x04
#define AD1816A_PIO_STATUS		0x05
#define AD1816A_PIO_DATA		0x06
#define AD1816A_RESERVED_7		0x07
#define AD1816A_PLAYBACK_CONFIG		0x08
#define AD1816A_CAPTURE_CONFIG		0x09
#define AD1816A_RESERVED_10		0x0a
#define AD1816A_RESERVED_11		0x0b
#define AD1816A_JOYSTICK_RAW_DATA	0x0c
#define AD1816A_JOYSTICK_CTRL		0x0d
#define AD1816A_JOY_POS_DATA_LOW	0x0e
#define AD1816A_JOY_POS_DATA_HIGH	0x0f

#define AD1816A_LOW_BYTE_TMP		0x00
#define AD1816A_INTERRUPT_ENABLE	0x01
#define AD1816A_EXTERNAL_CTRL		0x01
#define AD1816A_PLAYBACK_SAMPLE_RATE	0x02
#define AD1816A_CAPTURE_SAMPLE_RATE	0x03
#define AD1816A_VOICE_ATT		0x04
#define AD1816A_FM_ATT			0x05
#define AD1816A_I2S_1_ATT		0x06
#define AD1816A_I2S_0_ATT		0x07
#define AD1816A_PLAYBACK_BASE_COUNT	0x08
#define AD1816A_PLAYBACK_CURR_COUNT	0x09
#define AD1816A_CAPTURE_BASE_COUNT	0x0a
#define AD1816A_CAPTURE_CURR_COUNT	0x0b
#define AD1816A_TIMER_BASE_COUNT	0x0c
#define AD1816A_TIMER_CURR_COUNT	0x0d
#define AD1816A_MASTER_ATT		0x0e
#define AD1816A_CD_GAIN_ATT		0x0f
#define AD1816A_SYNTH_GAIN_ATT		0x10
#define AD1816A_VID_GAIN_ATT		0x11
#define AD1816A_LINE_GAIN_ATT		0x12
#define AD1816A_MIC_GAIN_ATT		0x13
#define AD1816A_PHONE_IN_GAIN_ATT	0x13
#define AD1816A_ADC_SOURCE_SEL		0x14
#define AD1816A_ADC_PGA			0x14
#define AD1816A_CHIP_CONFIG		0x20
#define AD1816A_DSP_CONFIG		0x21
#define AD1816A_FM_SAMPLE_RATE		0x22
#define AD1816A_I2S_1_SAMPLE_RATE	0x23
#define AD1816A_I2S_0_SAMPLE_RATE	0x24
#define AD1816A_RESERVED_37		0x25
#define AD1816A_PROGRAM_CLOCK_RATE	0x26
#define AD1816A_3D_PHAT_CTRL		0x27
#define AD1816A_PHONE_OUT_ATT		0x27
#define AD1816A_RESERVED_40		0x28
#define AD1816A_HW_VOL_BUT		0x29
#define AD1816A_DSP_MAILBOX_0		0x2a
#define AD1816A_DSP_MAILBOX_1		0x2b
#define AD1816A_POWERDOWN_CTRL		0x2c
#define AD1816A_TIMER_CTRL		0x2c
#define AD1816A_VERSION_ID		0x2d
#define AD1816A_RESERVED_46		0x2e

#define AD1816A_READY			0x80

#define AD1816A_PLAYBACK_IRQ_PENDING	0x80
#define AD1816A_CAPTURE_IRQ_PENDING	0x40
#define AD1816A_TIMER_IRQ_PENDING	0x20

#define AD1816A_PLAYBACK_ENABLE		0x01
#define AD1816A_PLAYBACK_PIO		0x02
#define AD1816A_CAPTURE_ENABLE		0x01
#define AD1816A_CAPTURE_PIO		0x02

#define AD1816A_FMT_LINEAR_8		0x00
#define AD1816A_FMT_ULAW_8		0x08
#define AD1816A_FMT_LINEAR_16_LIT	0x10
#define AD1816A_FMT_ALAW_8		0x18
#define AD1816A_FMT_LINEAR_16_BIG	0x30
#define AD1816A_FMT_ALL			0x38
#define AD1816A_FMT_STEREO		0x04

#define AD1816A_PLAYBACK_IRQ_ENABLE	0x8000
#define AD1816A_CAPTURE_IRQ_ENABLE	0x4000
#define AD1816A_TIMER_IRQ_ENABLE	0x2000
#define AD1816A_TIMER_ENABLE		0x0080

#define AD1816A_SRC_LINE		0x00
#define AD1816A_SRC_OUT			0x10
#define AD1816A_SRC_CD			0x20
#define AD1816A_SRC_SYNTH		0x30
#define AD1816A_SRC_VIDEO		0x40
#define AD1816A_SRC_MIC			0x50
#define AD1816A_SRC_MONO		0x50
#define AD1816A_SRC_PHONE_IN		0x60
#define AD1816A_SRC_MASK		0x70

#define AD1816A_CAPTURE_NOT_EQUAL	0x1000
#define AD1816A_WSS_ENABLE		0x8000

struct snd_ad1816a {
	unsigned long port;
	struct resource *res_port;
	int irq;
	int dma1;
	int dma2;

	unsigned short hardware;
	unsigned short version;

	spinlock_t lock;

	unsigned short mode;
	unsigned int clock_freq;

	struct snd_card *card;
	struct snd_pcm *pcm;

	struct snd_pcm_substream *playback_substream;
	struct snd_pcm_substream *capture_substream;
	unsigned int p_dma_size;
	unsigned int c_dma_size;

	struct snd_timer *timer;
#ifdef CONFIG_PM
	unsigned short image[48];
#endif
};


#define AD1816A_HW_AUTO		0
#define AD1816A_HW_AD1816A	1
#define AD1816A_HW_AD1815	2
#define AD1816A_HW_AD18MAX10	3

#define AD1816A_MODE_PLAYBACK	0x01
#define AD1816A_MODE_CAPTURE	0x02
#define AD1816A_MODE_TIMER	0x04
#define AD1816A_MODE_OPEN	(AD1816A_MODE_PLAYBACK |	\
				AD1816A_MODE_CAPTURE |		\
				AD1816A_MODE_TIMER)


extern int snd_ad1816a_create(struct snd_card *card, unsigned long port,
			      int irq, int dma1, int dma2,
			      struct snd_ad1816a *chip);

extern int snd_ad1816a_pcm(struct snd_ad1816a *chip, int device);
extern int snd_ad1816a_mixer(struct snd_ad1816a *chip);
extern int snd_ad1816a_timer(struct snd_ad1816a *chip, int device);
#ifdef CONFIG_PM
extern void snd_ad1816a_suspend(struct snd_ad1816a *chip);
extern void snd_ad1816a_resume(struct snd_ad1816a *chip);
#endif

#endif	/* __SOUND_AD1816A_H */
