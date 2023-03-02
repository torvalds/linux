/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _QCOM_DMA_HEAP_H
#define _QCOM_DMA_HEAP_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/dma-buf.h>

#include <linux/qcom_dma_heap_dt_constants.h>
#include <linux/err.h>

/* Heap flags */
#define QCOM_DMA_HEAP_FLAG_CACHED	BIT(1)

#define QCOM_DMA_HEAP_FLAG_CP_TRUSTED_VM		BIT(15)
#define QCOM_DMA_HEAP_FLAG_CP_TZ		BIT(16)
#define QCOM_DMA_HEAP_FLAG_CP_TOUCH		BIT(17)
#define QCOM_DMA_HEAP_FLAG_CP_BITSTREAM		BIT(18)
#define QCOM_DMA_HEAP_FLAG_CP_PIXEL		BIT(19)
#define QCOM_DMA_HEAP_FLAG_CP_NON_PIXEL		BIT(20)
#define QCOM_DMA_HEAP_FLAG_CP_CAMERA		BIT(21)
#define QCOM_DMA_HEAP_FLAG_CP_HLOS		BIT(22)
#define QCOM_DMA_HEAP_FLAG_CP_SPSS_SP		BIT(23)
#define QCOM_DMA_HEAP_FLAG_CP_SPSS_SP_SHARED	BIT(24)
#define QCOM_DMA_HEAP_FLAG_CP_SEC_DISPLAY	BIT(25)
#define QCOM_DMA_HEAP_FLAG_CP_APP		BIT(26)
#define QCOM_DMA_HEAP_FLAG_CP_CAMERA_PREVIEW	BIT(27)
#define QCOM_DMA_HEAP_FLAG_CP_MSS_MSA		BIT(28)
#define QCOM_DMA_HEAP_FLAG_CP_CDSP		BIT(29)
#define QCOM_DMA_HEAP_FLAG_CP_SPSS_HLOS_SHARED	BIT(30)

#define QCOM_DMA_HEAP_FLAGS_CP_MASK	GENMASK(30, 15)

#define QCOM_DMA_HEAP_FLAG_SECURE	BIT(31)

bool qcom_is_dma_buf_file(struct file *file);

struct dma_buf_heap_prefetch_region {
	u64 size;
	struct dma_heap *heap;
};

int qcom_secure_system_heap_prefetch(struct dma_buf_heap_prefetch_region *regions,
				     size_t nr_regions);

int qcom_secure_system_heap_drain(struct dma_buf_heap_prefetch_region *regions,
				  size_t nr_regions);

/**
 * dma_buf_heap_hyp_assign - wrapper function for hyp-assigning a dma_buf
 * @buf:		dma_buf to hyp-assign away from HLOS
 * @dest_vmids:		array of QCOM_DMA_HEAP_FLAG VMIDs (as defined above)
 * @dest_perms:		array of PERM_READ/PERM_WRITE/PERM_EXEC permission bits (as
 *			defined in include/soc/qcom/secure_buffer.h), such that
 *			dest_perms[i] specifies the permissions for VMID dest_vmids[i]
 * @dest_nelems:	number of elements in dest_vmids and dest_perms
 *
 * Return: Temporarily return -EINVAL whilst the functon isn't present. Otherwise, return
 * 0 on success, or a negative value returned by hyp_assign_table() on failure.
 */
static inline int dma_buf_heap_hyp_assign(struct dma_buf *buf, int *dest_vmids, int *dest_perms,
				   int dest_nelems)
{
	return -EINVAL;
}

/**
 * dma_buf_heap_hyp_unassign - wrapper function that hyp-assigns a dma_buf back to HLOS
 * @buf:	dma_buf to hyp-assign back to HLOS
 *
 * This function takes a dma_buf, and re-assigns it to HLOS with RWX permissions (at the
 * S2 level). The end-points to which the buffer was assigned to are tracked by the flags
 * kept in the msm_heap_helper_buffer->flags field (the helper_buffer is accesed through
 * dma_buf->priv), so the corresponding VMIDs don't need to be supplied as arguments.
 *
 * Return: Temporarily return -EINVAL whilst the functon isn't present. Otherwise, return
 * 0 on success, or a negative value returned by hyp_assign_table() on failure.
 */
static inline int dma_buf_heap_hyp_unassign(struct dma_buf *buf)
{
	return -EINVAL;
}

#endif /* _QCOM_DMA_HEAP_H */
