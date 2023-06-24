/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *  Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _MEM_BUF_EXPORTER_H
#define _MEM_BUF_EXPORTER_H

#include <linux/dma-buf.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <soc/qcom/secure_buffer.h>

int mem_buf_dma_buf_attach(struct dma_buf *dmabuf,
		      struct dma_buf_attachment *attachment);

/* Private data for managing a dmabuf's Virtual Machine permissions. */
struct mem_buf_vmperm;

/*
 * @lookup: Returns the mem_buf_vmperm data structure contained somewhere
 * in the exporter's private_data, or a negative number on error.
 * @attach: The exporter's normal dma_buf_attach callback
 * @dma_ops: The exporter's standard dma_buf callbacks, except for
 * attach which must be NULL.
 */
struct mem_buf_dma_buf_ops {
	struct mem_buf_vmperm *(*lookup)(struct dma_buf *dmabuf);
	int (*attach)(struct dma_buf *dmabuf, struct dma_buf_attachment *a);
	struct dma_buf_ops dma_ops;
};

struct dma_buf *
mem_buf_dma_buf_export(struct dma_buf_export_info *exp_info,
			struct mem_buf_dma_buf_ops *ops);


#define MEM_BUF_WRAPPER_FLAG_STATIC_VM BIT(0)
#define MEM_BUF_WRAPPER_FLAG_LENDSHARE BIT(1)
#define MEM_BUF_WRAPPER_FLAG_ACCEPT BIT(2)
#define MEM_BUF_WRAPPER_FLAG_ERR BIT(3)

/*
 * A dmabuf owned by the current VM with RWX permissions.
 * All variants should be free'd via mem_buf_vmperm_free().
 *
 * @dmabuf: value returned from dma_buf_export()
 * @sgt: Reference to the exporter's internal memory descriptor.
 * Will not be freed by mem_buf_vmperm_free().
 */
struct mem_buf_vmperm *mem_buf_vmperm_alloc(struct sg_table *sgt);

/*
 * A dmabuf which permantently belongs to the given VMs & permissions.
 */
struct mem_buf_vmperm *mem_buf_vmperm_alloc_staticvm(struct sg_table *sgt, int *vmids, int *perms,
		u32 nr_acl_entries);

/*
 * A dmabuf in the "MEMACCEPT" state.
 */
struct mem_buf_vmperm *mem_buf_vmperm_alloc_accept(struct sg_table *sgt,
	gh_memparcel_handle_t memparcel_hdl, int *vmids, int *perms,
	unsigned int nr_acl_entries);

/*
 * Performs the expected close step based on whether the dmabuf
 * is of the "STATICVM" "MEMACCEPT" or "DEFAULT" type.
 * Exporters should call this from dma_buf_release. If this function
 * Returns an error, exporters should consider the underlying memory
 * to have undefined permissions.
 */
int mem_buf_vmperm_release(struct mem_buf_vmperm *vmperm);

/*
 * Pins ths permissions of the dmabuf.
 * The exporter should call this from dma_buf_mmap, vm_ops->open(), and
 * dma_buf_map_attachment. It must not be called from
 * dma_buf_begin_cpu_access as userspace may call this function unsafely
 * via DMA_BUF_IOCTL_SYNC.
 */
void mem_buf_vmperm_pin(struct mem_buf_vmperm *vmperm);

/*
 * Unpins ths permissions of the dmabuf.
 * The exporter should call this from vm_ops->close(), and
 * dma_buf_unmap_attachment. It must not be called from
 * dma_buf_end_cpu_access as userspace may call this function unsafely
 * via DMA_BUF_IOCTL_SYNC.
 */
void mem_buf_vmperm_unpin(struct mem_buf_vmperm *vmperm);

/*
 * Check whether the current permissions of the dmabuf allow CMO.
 * Requires RW.
 *
 * The exporter should call this during dma_buf_map/unmap_attachment,
 * and dma_buf_begin/end_cpu_access.
 */
bool mem_buf_vmperm_can_cmo(struct mem_buf_vmperm *vmperm);

/*
 * Check whether the current permissions of the dmabuf allow mmap.
 * Requires at minimum the permissions specified in vma.
 *
 * The exporter should call mem_buf_vmperm_pin() from mmap.
 * The exporter should implement the open() and close() operations of
 * vma->vm_ops, and call mem_buf_vmperm_pin()/unpin() respectively.
 */
bool mem_buf_vmperm_can_mmap(struct mem_buf_vmperm *vmperm, struct vm_area_struct *vma);

/*
 * Check whether the current permissions of the dmabuf allow vmap.
 * Requires RW.
 *
 * The exporter should call this during dma_buf_vmap,
 */
bool mem_buf_vmperm_can_vmap(struct mem_buf_vmperm *vmperm);

#endif /*_MEM_BUF_EXPORTER_H */
