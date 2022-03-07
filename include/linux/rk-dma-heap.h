/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMABUF Heaps Allocation Infrastructure
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#ifndef _DMA_HEAPS_H
#define _DMA_HEAPS_H

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/dma-buf.h>

#if defined(CONFIG_DMABUF_RK_HEAPS_DEBUG)
#define dma_heap_print(fmt, ...)	\
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#else
#define dma_heap_print(fmt, ...)	\
	no_printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

#define RK_DMA_HEAP_NAME_LEN 16

struct rk_cma_heap {
	struct rk_dma_heap *heap;
	struct cma *cma;
};

struct rk_cma_heap_buffer {
	struct rk_cma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct page *cma_pages;
	struct page **pages;
	pgoff_t pagecount;
	int vmap_cnt;
	void *vaddr;
	phys_addr_t phys;
	bool attached;
};

struct rk_vmap_pfn_data {
	unsigned long	pfn; /* first pfn of contiguous */
	pgprot_t	prot;
};

/**
 * struct rk_dma_heap_ops - ops to operate on a given heap
 * @allocate:		allocate dmabuf and return struct dma_buf ptr
 * @get_pool_size:	if heap maintains memory pools, get pool size in bytes
 *
 * allocate returns dmabuf on success, ERR_PTR(-errno) on error.
 */
struct rk_dma_heap_ops {
	struct dma_buf *(*allocate)(struct rk_dma_heap *heap,
			unsigned long len,
			unsigned long fd_flags,
			unsigned long heap_flags,
			const char *name);
	struct page *(*alloc_contig_pages)(struct rk_dma_heap *heap,
					   size_t len, const char *name);
	void (*free_contig_pages)(struct rk_dma_heap *heap,
				  struct page *pages, size_t len,
				  const char *name);
	long (*get_pool_size)(struct rk_dma_heap *heap);
};

/**
 * struct rk_dma_heap_export_info - information needed to export a new dmabuf heap
 * @name:	used for debugging/device-node name
 * @ops:	ops struct for this heap
 * @priv:	heap exporter private data
 *
 * Information needed to export a new dmabuf heap.
 */
struct rk_dma_heap_export_info {
	const char *name;
	const struct rk_dma_heap_ops *ops;
	void *priv;
	bool support_cma;
};

/**
 * struct rk_dma_heap - represents a dmabuf heap in the system
 * @name:		used for debugging/device-node name
 * @ops:		ops struct for this heap
 * @heap_devt		heap device node
 * @list		list head connecting to list of heaps
 * @heap_cdev		heap char device
 * @heap_dev		heap device struct
 *
 * Represents a heap of memory from which buffers can be made.
 */
struct rk_dma_heap {
	const char *name;
	const struct rk_dma_heap_ops *ops;
	void *priv;
	dev_t heap_devt;
	struct list_head list;
	struct list_head dmabuf_list; /* dmabuf attach to this node */
	struct mutex dmabuf_lock;
	struct list_head contig_list; /* contig buffer attach to this node */
	struct mutex contig_lock;
	struct cdev heap_cdev;
	struct kref refcount;
	struct device *heap_dev;
	bool support_cma;
	struct seq_file *s;
};

struct rk_dma_heap_dmabuf {
	struct list_head node;
	struct dma_buf *dmabuf;
	const char *orig_alloc;
	phys_addr_t start;
	phys_addr_t end;
};

struct rk_dma_heap_contig_buf {
	struct list_head node;
	const char *orig_alloc;
	phys_addr_t start;
	phys_addr_t end;
};

#if defined(CONFIG_DMABUF_HEAPS_ROCKCHIP)
int rk_dma_heap_cma_setup(void);

/**
 * rk_dma_heap_get_drvdata() - get per-heap driver data
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-heap data for the heap.
 */
void *rk_dma_heap_get_drvdata(struct rk_dma_heap *heap);

/**
 * rk_dma_heap_get_dev() - get device struct for the heap
 * @heap: DMA-Heap to retrieve device struct from
 *
 * Returns:
 * The device struct for the heap.
 */
struct device *rk_dma_heap_get_dev(struct rk_dma_heap *heap);

/**
 * rk_dma_heap_get_name() - get heap name
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The char* for the heap name.
 */
const char *rk_dma_heap_get_name(struct rk_dma_heap *heap);

/**
 * rk_dma_heap_add - adds a heap to dmabuf heaps
 * @exp_info:		information needed to register this heap
 */
struct rk_dma_heap *rk_dma_heap_add(const struct rk_dma_heap_export_info *exp_info);

/**
 * rk_dma_heap_put - drops a reference to a dmabuf heaps, potentially freeing it
 * @heap:		heap pointer
 */
void rk_dma_heap_put(struct rk_dma_heap *heap);

/**
 * rk_dma_heap_set_dev - set heap dev dma param
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * Zero on success, ERR_PTR(-errno) on error
 */
int rk_dma_heap_set_dev(struct device *heap_dev);

/**
 * rk_dma_heap_find - Returns the registered dma_heap with the specified name
 * @name: Name of the heap to find
 *
 * NOTE: dma_heaps returned from this function MUST be released
 * using rk_dma_heap_put() when the user is done.
 */
struct rk_dma_heap *rk_dma_heap_find(const char *name);

/**
 * rk_dma_heap_buffer_alloc - Allocate dma-buf from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @fd_flags:	flags to set on returned dma-buf fd
 * @heap_flags:	flags to pass to the dma heap
 *
 * This is for internal dma-buf allocations only.
 */
struct dma_buf *rk_dma_heap_buffer_alloc(struct rk_dma_heap *heap, size_t len,
				      unsigned int fd_flags,
				      unsigned int heap_flags,
				      const char *name);

/** rk_dma_heap_buffer_free - Free dma_buf allocated by rk_dma_heap_buffer_alloc
 * @dma_buf:	dma_buf to free
 *
 * This is really only a simple wrapper to dma_buf_put()
 */
void rk_dma_heap_buffer_free(struct dma_buf *dmabuf);

/**
 * rk_dma_heap_bufferfd_alloc - Allocate dma-buf fd from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @fd_flags:	flags to set on returned dma-buf fd
 * @heap_flags:	flags to pass to the dma heap
 */
int rk_dma_heap_bufferfd_alloc(struct rk_dma_heap *heap, size_t len,
			    unsigned int fd_flags,
			    unsigned int heap_flags,
			    const char *name);

/**
 * rk_dma_heap_alloc_contig_pages - Allocate contiguous pages from a dma_heap
 * @heap:	dma_heap to allocate from
 * @len:	size to allocate
 * @name:	the name who allocate
 */
struct page *rk_dma_heap_alloc_contig_pages(struct rk_dma_heap *heap,
						size_t len, const char *name);
/**
 * rk_dma_heap_free_contig_pages - Free contiguous pages to a dma_heap
 * @heap:	dma_heap to free to
 * @pages:	pages to free to
 * @len:	size to free
 * @name:	the name who allocate
 */
void rk_dma_heap_free_contig_pages(struct rk_dma_heap *heap,
			struct page *pages, size_t len, const char *name);
/**
 * rk_vmap_contig_pfn - Map contiguous pfn to vm area
 * @pfn:	indicate the first pfn of contig
 * @count:	count of pfns
 * @prot:	for mapping
 */
void *rk_vmap_contig_pfn(unsigned long pfn, unsigned int count,
				 pgprot_t prot);

/**
 * rk_dma_heap_get_cma - get cma structure
 */
struct cma *rk_dma_heap_get_cma(void);
#else
static inline int rk_dma_heap_cma_setup(void)
{
	return -ENODEV;
}

static inline void *rk_dma_heap_get_drvdata(struct rk_dma_heap *heap)
{
	return NULL;
}

static inline struct device *rk_dma_heap_get_dev(struct rk_dma_heap *heap)
{
	return NULL;
}

static inline const char *rk_dma_heap_get_name(struct rk_dma_heap *heap)
{
	return NULL;
}

static inline struct rk_dma_heap *rk_dma_heap_add(const struct rk_dma_heap_export_info *exp_info)
{
	return NULL;
}

static inline void rk_dma_heap_put(struct rk_dma_heap *heap)
{
}

static inline int rk_dma_heap_set_dev(struct device *heap_dev)
{
	return -ENODEV;
}

static inline struct rk_dma_heap *rk_dma_heap_find(const char *name)
{
	return NULL;
}

static inline struct dma_buf *rk_dma_heap_buffer_alloc(struct rk_dma_heap *heap, size_t len,
				      unsigned int fd_flags,
				      unsigned int heap_flags,
				      const char *name)
{
	return NULL;
}

static inline void rk_dma_heap_buffer_free(struct dma_buf *dmabuf)
{
}

static inline int rk_dma_heap_bufferfd_alloc(struct rk_dma_heap *heap, size_t len,
			    unsigned int fd_flags,
			    unsigned int heap_flags,
			    const char *name)
{
	return -ENODEV;
}

static inline struct page *rk_dma_heap_alloc_contig_pages(struct rk_dma_heap *heap,
						size_t len, const char *name)
{
	return NULL;
}

static inline void rk_dma_heap_free_contig_pages(struct rk_dma_heap *heap,
			struct page *pages, size_t len, const char *name)
{
}

static inline void *rk_vmap_contig_pfn(unsigned long pfn, unsigned int count,
				 pgprot_t prot)
{
	return NULL;
}

static inline struct cma *rk_dma_heap_get_cma(void)
{
	return NULL;
}

#endif
#endif /* _DMA_HEAPS_H */
