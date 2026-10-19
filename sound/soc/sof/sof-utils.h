/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2022 Intel Corporation
 */

#ifndef __SOC_SOF_UTILS_H
#define __SOC_SOF_UTILS_H

struct snd_dma_buffer;
struct device;

/*
 * Number of PFNs which can be stored in a page table of @bytes size.
 * The PFNs are compressed to 2.5 bytes each but they are written with 32 bit
 * accesses, therefore the last PFN can reach up to 3 bytes past the space it
 * needs for itself.
 */
#define SOF_PAGE_TABLE_MAX_PFNS(bytes)	\
	(((((bytes) - sizeof(u32)) << 1) + 1) / 5 + 1)

int snd_sof_create_page_table(struct device *dev,
			      struct snd_dma_buffer *dmab,
			      struct snd_dma_buffer *page_table, size_t size);

#endif
