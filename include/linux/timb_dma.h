/*
 * timb_dma.h timberdale FPGA DMA driver defines
 * Copyright (c) 2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Timberdale FPGA DMA engine
 */

#ifndef _LINUX_TIMB_DMA_H
#define _LINUX_TIMB_DMA_H

/**
 * struct timb_dma_platform_data_channel - Description of each individual
 *	DMA channel for the timberdale DMA driver
 * @rx:			true if this channel handles data in the direction to
 *	the CPU.
 * @bytes_per_line:	Number of bytes per line, this is specific for channels
 *	handling video data. For other channels this shall be left to 0.
 * @descriptors:	Number of descriptors to allocate for this channel.
 * @descriptor_elements: Number of elements in each descriptor.
 *
 */
struct timb_dma_platform_data_channel {
	bool rx;
	unsigned int bytes_per_line;
	unsigned int descriptors;
	unsigned int descriptor_elements;
};

/**
 * struct timb_dma_platform_data - Platform data of the timberdale DMA driver
 * @nr_channels:	Number of defined channels in the channels array.
 * @channels:		Definition of the each channel.
 *
 */
struct timb_dma_platform_data {
	unsigned nr_channels;
	struct timb_dma_platform_data_channel channels[32];
};

#endif
