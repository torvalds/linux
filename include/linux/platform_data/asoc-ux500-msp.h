/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __MSP_H
#define __MSP_H

#include <linux/platform_data/dma-ste-dma40.h>

/* Platform data structure for a MSP I2S-device */
struct msp_i2s_platform_data {
	int id;
	struct stedma40_chan_cfg *msp_i2s_dma_rx;
	struct stedma40_chan_cfg *msp_i2s_dma_tx;
};

#endif
