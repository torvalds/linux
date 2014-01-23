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

struct davinci_audio_dev {
	struct davinci_pcm_dma_params dma_params[2];
	void __iomem *base;
	struct device *dev;

	/* McASP specific data */
	int	tdm_slots;
	u8	op_mode;
	u8	num_serializer;
	u8	*serial_dir;
	u8	version;
	u16	bclk_lrclk_ratio;

	/* McASP FIFO related */
	u8	txnumevt;
	u8	rxnumevt;

#ifdef CONFIG_PM_SLEEP
	struct {
		u32	txfmtctl;
		u32	rxfmtctl;
		u32	txfmt;
		u32	rxfmt;
		u32	aclkxctl;
		u32	aclkrctl;
		u32	pdir;
	} context;
#endif
};

#endif	/* DAVINCI_MCASP_H */
