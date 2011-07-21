/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _MXS_PCM_H
#define _MXS_PCM_H

#include <mach/dma.h>

struct mxs_pcm_dma_params {
	int chan_irq;
	int chan_num;
};

struct mxs_pcm_runtime_data {
	int period_bytes;
	int periods;
	int dma;
	unsigned long offset;
	unsigned long size;
	void *buf;
	int period_time;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *dma_chan;
	struct mxs_dma_data dma_data;
	struct mxs_pcm_dma_params *dma_params;
};

#endif
