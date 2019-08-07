/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#ifndef _ION_KERNEL_H
#define _ION_KERNEL_H

#include <linux/dma-buf.h>
#include <linux/err.h>

#ifdef CONFIG_ION


/**
 * ion_alloc - Allocates an ion buffer of given size from given heap
 *
 * @len:               size of the buffer to be allocated.
 * @heap_id_mask:      a bitwise maks of heap ids to allocate from
 * @flags:             ION_BUFFER_XXXX flags for the new buffer.
 *
 * The function exports a dma_buf object for the new ion buffer internally
 * and returns that to the caller. So, the buffer is ready to be used by other
 * drivers immediately. Returns ERR_PTR in case of failure.
 */
struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
			  unsigned int flags);

#else
static inline struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
					unsigned int flags)
{
	return ERR_PTR(-ENOMEM);
}


#endif /* CONFIG_ION */
#endif /* _ION_KERNEL_H */
