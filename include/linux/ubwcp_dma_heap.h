/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __UBWCP_DMA_HEAP_H_
#define __UBWCP_DMA_HEAP_H_

#include <linux/types.h>
#include <linux/dma-buf.h>

/**
 *
 * Initialize ubwcp buffer for the given dma_buf. This
 * initializes ubwcp internal data structures and possibly hw to
 * use ubwcp for this buffer.
 *
 * @param dmabuf : ptr to the buffer to be configured for ubwcp
 *
 * @return int : 0 on success, otherwise error code
 */
typedef int (*init_buffer)(struct dma_buf *dmabuf);

/**
 * Free up ubwcp resources for this buffer.
 *
 * @param dmabuf : ptr to the dma buf
 *
 * @return int : 0 on success, otherwise error code
 */
typedef int (*free_buffer)(struct dma_buf *dmabuf);

/**
 * Lock buffer for CPU access. This prepares ubwcp hw to allow
 * CPU access to the compressed buffer. It will perform
 * necessary address translation configuration and cache maintenance ops
 * so that CPU can safely access ubwcp buffer, if this call is
 * successful.
 *
 * @param dmabuf : ptr to the dma buf
 * @param direction : direction of access
 *
 * @return int : 0 on success, otherwise error code
 */
typedef int (*lock_buffer)(struct dma_buf *dma_buf, enum dma_data_direction direction);

/**
 * Unlock buffer from CPU access. This prepares ubwcp hw to
 * safely allow for device access to the compressed buffer including any
 * necessary cache maintenance ops. It may also free up certain ubwcp
 * resources that could result in error when accessed by CPU in
 * unlocked state.
 *
 * @param dmabuf : ptr to the dma buf
 * @param direction : direction of access
 *
 * @return int : 0 on success, otherwise error code
 */
typedef int (*unlock_buffer)(struct dma_buf *dmabuf, enum dma_data_direction direction);

#endif /* __UBWCP_DMA_HEAP_H_ */
