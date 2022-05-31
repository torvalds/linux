/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation. All rights reserved.
 */

#ifndef __SOC_SOF_UTILS_H
#define __SOC_SOF_UTILS_H

struct snd_dma_buffer;
struct device;

int snd_sof_create_page_table(struct device *dev,
			      struct snd_dma_buffer *dmab,
			      unsigned char *page_table, size_t size);

#endif
