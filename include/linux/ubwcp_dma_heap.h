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

/**
 * Set the callbacks that will allow the UBWC-P DMA-BUF heap driver
 * to call back into the UBWC-P driver.
 *
 * @param init_buf_fn_ptr : Pointer to init_buffer function
 * @param free_buf_fn_ptr : Pointer to free_buffer function
 * @param lock_buf_fn_ptr : Pointer to lock_buffer function
 * @param unlock_buf_fn_ptr : Pointer to unlock_buffer function
 *
 * @return int : 0 on success, otherwise error code
 */
int msm_ubwcp_set_ops(init_buffer init_buf_fn_ptr,
		      free_buffer free_buf_fn_ptr,
		      lock_buffer lock_buf_fn_ptr,
		      unlock_buffer unlock_buf_fn_ptr);

/**
 * Configures whether a DMA-BUF allocated from the UBWC-P heap is in
 * linear or UBWC-P mode.
 *
 * @param dmabuf : ptr to the dma buf
 * @param linear : controls which mode this buffer will be placed into
 * @param ula_pa_addr : ULA-PA "physical address" to mmap a buffer to
 * @param ula_pa_size : size of the ULA-PA buffer mapping to be used
 * during mmap
 *
 * @return int : 0 on success, otherwise error code
 */
int msm_ubwcp_dma_buf_configure_mmap(struct dma_buf *dmabuf, bool linear,
				     phys_addr_t ula_pa_addr,
				     size_t ula_pa_size);

#endif /* __UBWCP_DMA_HEAP_H_ */
