/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_TUI_HEAP_H
#define _QCOM_TUI_HEAP_H

#include <linux/mem-buf.h>

struct platform_heap;

#ifdef CONFIG_QCOM_DMABUF_HEAPS_TUI_CARVEOUT
int qcom_tui_carveout_heap_create(struct platform_heap *heap_data);
int qcom_tui_heap_add_pool_fd(struct mem_buf_allocation_data *alloc_data);
void *qcom_tvm_heap_add_kernel_pool(struct dma_heap *heap, size_t size);
void qcom_tvm_heap_remove_kernel_pool(void *handle);
#else
static inline int qcom_tui_carveout_heap_create(struct platform_heap *heap_data)
{
	return -EINVAL;
}
static inline int qcom_tui_heap_add_pool_fd(struct mem_buf_allocation_data *alloc_data)
{
	return -EINVAL;
}
static inline void *qcom_tvm_heap_add_kernel_pool(struct dma_heap *heap, size_t size)
{
	return ERR_PTR(-EINVAL);
}
static inline void qcom_tvm_heap_remove_kernel_pool(void *handle) {}
#endif


#endif /* _QCOM_TUI_HEAP_H */
