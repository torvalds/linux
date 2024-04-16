/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timb_dma.h timberdale FPGA DMA driver defines
 * Copyright (c) 2010 Intel Corporation
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
