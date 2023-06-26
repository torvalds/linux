/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *  Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _MEM_BUF_H
#define _MEM_BUF_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/types.h>
#include <linux/dma-buf.h>
#include <uapi/linux/mem-buf.h>

/* For in-kernel use only, not allowed for userspace ioctl */
#define MEM_BUF_BUDDY_MEM_TYPE (MEM_BUF_ION_MEM_TYPE + 2)

/* Used to obtain the underlying vmperm struct of a DMA-BUF */
struct mem_buf_vmperm *to_mem_buf_vmperm(struct dma_buf *dmabuf);

/* Returns true if the local VM has exclusive access and is the owner */
bool mem_buf_dma_buf_exclusive_owner(struct dma_buf *dmabuf);

/*
 * Returns the Virtual Machine vmids & permissions of the dmabuf. Can't be
 * modified.
 */
int mem_buf_dma_buf_get_vmperm(struct dma_buf *dmabuf, const int **vmids,
		const int **perms, int *nr_acl_entries);
/*
 * Returns a copy of the Virtual Machine vmids & permissions of the dmabuf.
 * The caller must kfree() when finished.
 */
int mem_buf_dma_buf_copy_vmperm(struct dma_buf *dmabuf, int **vmids, int **perms,
		int *nr_acl_entries);

/*
 * Returns 0 if @dmabuf has a valid memparcel handle and stores it in
 * memparcel_hdl
 */
int mem_buf_dma_buf_get_memparcel_hdl(struct dma_buf *dmabuf,
				      gh_memparcel_handle_t *memparcel_hdl);

typedef int (*mem_buf_dma_buf_destructor)(void *dtor_data);
int mem_buf_dma_buf_set_destructor(struct dma_buf *dmabuf,
				   mem_buf_dma_buf_destructor dtor,
				   void *dtor_data);

/**
 * struct mem_buf_allocation_data - Data structure that contains information
 * about a memory buffer allocation request.
 * @size: The size (in bytes) of the memory to be requested from a remote VM
 * @nr_acl_entries: The number of ACL entries in @acl_list
 * @acl_list: A list of VMID and permission pairs that describe what VMIDs will
 * have access to the memory, and with what permissions
 * @trans_type: One of GH_RM_TRANS_TYPE_DONATE/LEND/SHARE
 * @sgl_desc: Optional. Requests a specific set of IPA addresses.
 * @src_mem_type: The type of memory that the remote VM should allocate
 * (e.g. ION memory)
 * @src_data: A pointer to memory type specific data that the remote VM may need
 * when performing an allocation (e.g. ION memory allocations require a heap ID)
 * @dst_mem_type: The type of memory that the native VM wants (e.g. ION memory)
 * @dst_data: A pointer to memory type specific data that the native VM may
 * need when adding the memory from the remote VM (e.g. ION memory requires a
 * heap ID to add the memory to).
 */
struct mem_buf_allocation_data {
	size_t size;
	unsigned int nr_acl_entries;
	int *vmids;
	int *perms;
	u32 trans_type;
	struct gh_sgl_desc *sgl_desc;
	enum mem_buf_mem_type src_mem_type;
	void *src_data;
	enum mem_buf_mem_type dst_mem_type;
	void *dst_data;
};

struct mem_buf_lend_kernel_arg {
	unsigned int nr_acl_entries;
	int *vmids;
	int *perms;
	gh_memparcel_handle_t memparcel_hdl;
	u32 flags;
	u64 label;
};

int mem_buf_lend(struct dma_buf *dmabuf,
		struct mem_buf_lend_kernel_arg *arg);

/*
 * mem_buf_share
 * Grant the local VM, as well as one or more remote VMs access
 * to the dmabuf. The permissions of the local VM default to RWX
 * unless otherwise specified.
 */
int mem_buf_share(struct dma_buf *dmabuf,
		struct mem_buf_lend_kernel_arg *arg);


struct mem_buf_retrieve_kernel_arg {
	int sender_vmid;
	unsigned int nr_acl_entries;
	int *vmids;
	int *perms;
	gh_memparcel_handle_t memparcel_hdl;
	int fd_flags;
};
struct dma_buf *mem_buf_retrieve(struct mem_buf_retrieve_kernel_arg *arg);
int mem_buf_reclaim(struct dma_buf *dmabuf);

#if IS_ENABLED(CONFIG_QCOM_MEM_BUF)

void *mem_buf_alloc(struct mem_buf_allocation_data *alloc_data);
void mem_buf_free(void *membuf);
struct gh_sgl_desc *mem_buf_get_sgl(void *membuf);
int mem_buf_current_vmid(void);
#else

static inline void *mem_buf_alloc(struct mem_buf_allocation_data *alloc_data)
{
	return ERR_PTR(-ENODEV);
}

static inline void mem_buf_free(void *membuf) {}

static inline struct gh_sgl_desc *mem_buf_get_sgl(void *membuf)
{
	return ERR_PTR(-EINVAL);
}
static inline int mem_buf_current_vmid(void)
{
	return -EINVAL;
}
#endif /* CONFIG_QCOM_MEM_BUF */


#ifdef CONFIG_QCOM_MEM_BUF_DEV_GH
int mem_buf_map_mem_s1(struct gh_sgl_desc *sgl_desc);
int mem_buf_unmap_mem_s1(struct gh_sgl_desc *sgl_desc);
#else
static inline int mem_buf_map_mem_s1(struct gh_sgl_desc *sgl_desc)
{
	return -EINVAL;
}

static inline int mem_buf_unmap_mem_s1(struct gh_sgl_desc *sgl_desc)
{
	return -EINVAL;
}
#endif /* CONFIG_QCOM_MEM_BUF_DEV_GH */
#endif /* _MEM_BUF_H */
