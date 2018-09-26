/*
 * Xilinx DMA Engine drivers support header file
 *
 * Copyright (C) 2010-2014 Xilinx, Inc. All rights reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DMA_XILINX_DMA_H
#define __DMA_XILINX_DMA_H

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

/**
 * struct xilinx_vdma_config - VDMA Configuration structure
 * @frm_dly: Frame delay
 * @gen_lock: Whether in gen-lock mode
 * @master: Master that it syncs to
 * @frm_cnt_en: Enable frame count enable
 * @park: Whether wants to park
 * @park_frm: Frame to park on
 * @coalesc: Interrupt coalescing threshold
 * @delay: Delay counter
 * @reset: Reset Channel
 * @ext_fsync: External Frame Sync source
 * @vflip_en:  Vertical Flip enable
 */
struct xilinx_vdma_config {
	int frm_dly;
	int gen_lock;
	int master;
	int frm_cnt_en;
	int park;
	int park_frm;
	int coalesc;
	int delay;
	int reset;
	int ext_fsync;
	bool vflip_en;
};

int xilinx_vdma_channel_set_config(struct dma_chan *dchan,
					struct xilinx_vdma_config *cfg);

#endif
