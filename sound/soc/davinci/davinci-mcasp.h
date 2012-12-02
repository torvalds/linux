/*
 * ALSA SoC McASP Audio Layer for TI DAVINCI processor
 *
 * MCASP related definitions
 *
 * Author: Nirmal Pandey <n-pandey@ti.com>,
 *         Suresh Rajashekara <suresh.r@ti.com>
 *         Steve Chen <schen@.mvista.com>
 *
 * Copyright:   (C) 2009 MontaVista Software, Inc., <source@mvista.com>
 * Copyright:   (C) 2009  Texas Instruments, India
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DAVINCI_MCASP_H
#define DAVINCI_MCASP_H

#include <linux/io.h>
#include <linux/platform_data/davinci_asp.h>

#include "davinci-pcm.h"

#define DAVINCI_MCASP_RATES	SNDRV_PCM_RATE_8000_192000
#define DAVINCI_MCASP_I2S_DAI	0
#define DAVINCI_MCASP_DIT_DAI	1

enum {
	DAVINCI_AUDIO_WORD_8 = 0,
	DAVINCI_AUDIO_WORD_12,
	DAVINCI_AUDIO_WORD_16,
	DAVINCI_AUDIO_WORD_20,
	DAVINCI_AUDIO_WORD_24,
	DAVINCI_AUDIO_WORD_32,
	DAVINCI_AUDIO_WORD_28,  /* This is only valid for McASP */
};

struct davinci_audio_dev {
	struct davinci_pcm_dma_params dma_params[2];
	void __iomem *base;
	int sample_rate;
	struct device *dev;
	unsigned int codec_fmt;

	/* McASP specific data */
	int	tdm_slots;
	u8	op_mode;
	u8	num_serializer;
	u8	*serial_dir;
	u8	version;

	/* McASP FIFO related */
	u8	txnumevt;
	u8	rxnumevt;
};

#endif	/* DAVINCI_MCASP_H */
