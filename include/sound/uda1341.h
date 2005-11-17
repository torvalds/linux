/*
 *  linux/include/linux/l3/uda1341.h
 *
 * Philips UDA1341 mixer device driver for ALSA
 *
 * Copyright (c) 2002 Tomas Kasparek <tomas.kasparek@seznam.cz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 *
 * History:
 *
 * 2002-03-13 Tomas Kasparek Initial release - based on uda1341.h from OSS
 * 2002-03-30 Tomas Kasparek Proc filesystem support, complete mixer and DSP
 *                           features support
 */

/* $Id: uda1341.h,v 1.8 2005/11/17 14:17:21 tiwai Exp $ */

#define UDA1341_ALSA_NAME "snd-uda1341"

/*
 * Default rate set after inicialization
 */
#define AUDIO_RATE_DEFAULT	44100

/*
 * UDA1341 L3 address and command types
 */
#define UDA1341_L3ADDR		5
#define UDA1341_DATA0		(UDA1341_L3ADDR << 2 | 0)
#define UDA1341_DATA1		(UDA1341_L3ADDR << 2 | 1)
#define UDA1341_STATUS		(UDA1341_L3ADDR << 2 | 2)

enum uda1341_onoff {
	OFF=0,
	ON,
};

enum uda1341_format {
	I2S=0,
	LSB16,
	LSB18,
	LSB20,
	MSB,
	LSB16MSB,
	LSB18MSB,
	LSB20MSB,        
};

enum uda1341_fs {
	F512=0,
	F384,
	F256,
	Funused,
};

enum uda1341_peak {
	BEFORE=0,
	AFTER,
};

enum uda1341_filter {
	FLAT=0,
	MIN,
	MIN2,
	MAX,
};

enum uda1341_mixer {
	DOUBLE,
	LINE,
	MIC,
	MIXER,
};

enum uda1341_deemp {
	NONE,
	D32,
	D44,
	D48,
};

enum uda1341_config {
	CMD_READ_REG = 0,
	CMD_RESET,
	CMD_FS,
	CMD_FORMAT,
	CMD_OGAIN,
	CMD_IGAIN,
	CMD_DAC,
	CMD_ADC,
	CMD_VOLUME,
	CMD_BASS,
	CMD_TREBBLE,
	CMD_PEAK,
	CMD_DEEMP,
	CMD_MUTE,        
	CMD_FILTER,
	CMD_CH1,
	CMD_CH2,
	CMD_MIC,       
	CMD_MIXER,
	CMD_AGC,
	CMD_IG,
	CMD_AGC_TIME,
	CMD_AGC_LEVEL,
#ifdef CONFIG_PM
	CMD_SUSPEND,
	CMD_RESUME,
#endif
	CMD_LAST,
};

enum write_through {
	//used in update_bits (write_cfg) to avoid l3_write - just update local copy of regs.
	REGS_ONLY=0,
	//update local regs and write value to uda1341 - do l3_write
	FLUSH,
};

int __init snd_chip_uda1341_mixer_new(struct snd_card *card, struct l3_client **clnt);

/*
 * Local variables:
 * indent-tabs-mode: t
 * End:
 */
