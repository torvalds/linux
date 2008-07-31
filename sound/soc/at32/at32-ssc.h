/* sound/soc/at32/at32-ssc.h
 * ASoC SSC interface for Atmel AT32 SoC
 *
 * Copyright (C) 2008 Long Range Systems
 *    Geoffrey Wossum <gwossum@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SOUND_SOC_AT32_AT32_SSC_H
#define __SOUND_SOC_AT32_AT32_SSC_H __FILE__

#include <linux/types.h>
#include <linux/atmel-ssc.h>

#include "at32-pcm.h"



struct at32_ssc_state {
	u32 ssc_cmr;
	u32 ssc_rcmr;
	u32 ssc_rfmr;
	u32 ssc_tcmr;
	u32 ssc_tfmr;
	u32 ssc_sr;
	u32 ssc_imr;
};



struct at32_ssc_info {
	char *name;
	struct ssc_device *ssc;
	spinlock_t lock;	/* lock for dir_mask */
	unsigned short dir_mask;	/* 0=unused, 1=playback, 2=capture */
	unsigned short initialized;	/* true if SSC has been initialized */
	unsigned short daifmt;
	unsigned short cmr_div;
	unsigned short tcmr_period;
	unsigned short rcmr_period;
	struct at32_pcm_dma_params *dma_params[2];
	struct at32_ssc_state ssc_state;
};


/* SSC divider ids */
#define AT32_SSC_CMR_DIV        0	/* MCK divider for BCLK */
#define AT32_SSC_TCMR_PERIOD    1	/* BCLK divider for transmit FS */
#define AT32_SSC_RCMR_PERIOD    2	/* BCLK divider for receive FS */


extern struct snd_soc_dai at32_ssc_dai[];



#endif /* __SOUND_SOC_AT32_AT32_SSC_H */
