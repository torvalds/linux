#ifndef __DRM_DRM_LEGACY_H__
#define __DRM_DRM_LEGACY_H__
/*
 * Legacy driver interfaces for the Direct Rendering Manager
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 * Copyright © 2014 Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/agp_backend.h>

#include <drm/drm.h>
#include <drm/drm_auth.h>
#include <drm/drm_hashtab.h>

struct drm_device;
struct drm_driver;
struct file;
struct pci_driver;

/*
 * Legacy Support for palateontologic DRM drivers
 *
 * If you add a new driver and it uses any of these functions or structures,
 * you're doing it terribly wrong.
 */

/**
 * DMA buffer.
 */
struct drm_buf {
	int idx;		       /**< Index into master buflist */
	int total;		       /**< Buffer size */
	int order;		       /**< log-base-2(total) */
	int used;		       /**< Amount of buffer in use (for DMA) */
	unsigned long offset;	       /**< Byte offset (used internally) */
	void *address;		       /**< Address of buffer */
	unsigned long bus_address;     /**< Bus address of buffer */
	struct drm_buf *next;	       /**< Kernel-only: used for free list */
	__volatile__ int waiting;      /**< On kernel DMA queue */
	__volatile__ int pending;      /**< On hardware DMA queue */
	struct drm_file *file_priv;    /**< Private of holding file descr */
	int context;		       /**< Kernel queue for this buffer */
	int while_locked;	       /**< Dispatch this buffer while locked */
	enum {
		DRM_LIST_NONE = 0,
		DRM_LIST_FREE = 1,
		DRM_LIST_WAIT = 2,
		DRM_LIST_PEND = 3,
		DRM_LIST_PRIO = 4,
		DRM_LIST_RECLAIM = 5
	} list;			       /**< Which list we're on */

	int dev_priv_size;		 /**< Size of buffer private storage */
	void *dev_private;		 /**< Per-buffer private storage */
};

typedef struct drm_dma_handle {
	dma_addr_t busaddr;
	void *vaddr;
	size_t size;
} drm_dma_handle_t;

/**
 * Buffer entry.  There is one of this for each buffer size order.
 */
struct drm_buf_entry {
	int buf_size;			/**< size */
	int buf_count;			/**< number of buffers */
	struct drm_buf *buflist;		/**< buffer list */
	int seg_count;
	int page_order;
	struct drm_dma_handle **seglist;

	int low_mark;			/**< Low water mark */
	int high_mark;			/**< High water mark */
};

/**
 * DMA data.
 */
struct drm_device_dma {

	struct drm_buf_entry bufs[DRM_MAX_ORDER + 1];	/**< buffers, grouped by their size order */
	int buf_count;			/**< total number of buffers */
	struct drm_buf **buflist;		/**< Vector of pointers into drm_device_dma::bufs */
	int seg_count;
	int page_count;			/**< number of pages */
	unsigned long *pagelist;	/**< page list */
	unsigned long byte_count;
	enum {
		_DRM_DMA_USE_AGP = 0x01,
		_DRM_DMA_USE_SG = 0x02,
		_DRM_DMA_USE_FB = 0x04,
		_DRM_DMA_USE_PCI_RO = 0x08
	} flags;

};

/**
 * Scatter-gather memory.
 */
struct drm_sg_mem {
	unsigned long handle;
	void *virtual;
	int pages;
	struct page **pagelist;
	dma_addr_t *busaddr;
};

/**
 * Kernel side of a mapping
 */
struct drm_local_map {
	dma_addr_t offset;	 /**< Requested physical address (0 for SAREA)*/
	unsigned long size;	 /**< Requested physical size (bytes) */
	enum drm_map_type type;	 /**< Type of memory to map */
	enum drm_map_flags flags;	 /**< Flags */
	void *handle;		 /**< User-space: "Handle" to pass to mmap() */
				 /**< Kernel-space: kernel-virtual address */
	int mtrr;		 /**< MTRR slot used */
};

typedef struct drm_local_map drm_local_map_t;

/**
 * Mappings list
 */
struct drm_map_list {
	struct list_head head;		/**< list head */
	struct drm_hash_item hash;
	struct drm_local_map *map;	/**< mapping */
	uint64_t user_token;
	struct drm_master *master;
};

int drm_legacy_addmap(struct drm_device *d, resource_size_t offset,
		      unsigned int size, enum drm_map_type type,
		      enum drm_map_flags flags, struct drm_local_map **map_p);
struct drm_local_map *drm_legacy_findmap(struct drm_device *dev, unsigned int token);
void drm_legacy_rmmap(struct drm_device *d, struct drm_local_map *map);
int drm_legacy_rmmap_locked(struct drm_device *d, struct drm_local_map *map);
struct drm_local_map *drm_legacy_getsarea(struct drm_device *dev);
int drm_legacy_mmap(struct file *filp, struct vm_area_struct *vma);

int drm_legacy_addbufs_agp(struct drm_device *d, struct drm_buf_desc *req);
int drm_legacy_addbufs_pci(struct drm_device *d, struct drm_buf_desc *req);

/**
 * Test that the hardware lock is held by the caller, returning otherwise.
 *
 * \param dev DRM device.
 * \param filp file pointer of the caller.
 */
#define LOCK_TEST_WITH_RETURN( dev, _file_priv )				\
do {										\
	if (!_DRM_LOCK_IS_HELD(_file_priv->master->lock.hw_lock->lock) ||	\
	    _file_priv->master->lock.file_priv != _file_priv)	{		\
		DRM_ERROR( "%s called without lock held, held  %d owner %p %p\n",\
			   __func__, _DRM_LOCK_IS_HELD(_file_priv->master->lock.hw_lock->lock),\
			   _file_priv->master->lock.file_priv, _file_priv);	\
		return -EINVAL;							\
	}									\
} while (0)

void drm_legacy_idlelock_take(struct drm_lock_data *lock);
void drm_legacy_idlelock_release(struct drm_lock_data *lock);

/* drm_pci.c */

#ifdef CONFIG_PCI

int drm_legacy_pci_init(const struct drm_driver *driver,
			struct pci_driver *pdriver);
void drm_legacy_pci_exit(const struct drm_driver *driver,
			 struct pci_driver *pdriver);

#else

static inline struct drm_dma_handle *drm_pci_alloc(struct drm_device *dev,
						   size_t size, size_t align)
{
	return NULL;
}

static inline void drm_pci_free(struct drm_device *dev,
				struct drm_dma_handle *dmah)
{
}

static inline int drm_legacy_pci_init(const struct drm_driver *driver,
				      struct pci_driver *pdriver)
{
	return -EINVAL;
}

static inline void drm_legacy_pci_exit(const struct drm_driver *driver,
				       struct pci_driver *pdriver)
{
}

#endif

/*
 * AGP Support
 */

struct drm_agp_head {
	struct agp_kern_info agp_info;
	struct list_head memory;
	unsigned long mode;
	struct agp_bridge_data *bridge;
	int enabled;
	int acquired;
	unsigned long base;
	int agp_mtrr;
	int cant_use_aperture;
	unsigned long page_mask;
};

#if IS_ENABLED(CONFIG_DRM_LEGACY) && IS_ENABLED(CONFIG_AGP)
struct drm_agp_head *drm_legacy_agp_init(struct drm_device *dev);
int drm_legacy_agp_acquire(struct drm_device *dev);
int drm_legacy_agp_release(struct drm_device *dev);
int drm_legacy_agp_enable(struct drm_device *dev, struct drm_agp_mode mode);
int drm_legacy_agp_info(struct drm_device *dev, struct drm_agp_info *info);
int drm_legacy_agp_alloc(struct drm_device *dev, struct drm_agp_buffer *request);
int drm_legacy_agp_free(struct drm_device *dev, struct drm_agp_buffer *request);
int drm_legacy_agp_unbind(struct drm_device *dev, struct drm_agp_binding *request);
int drm_legacy_agp_bind(struct drm_device *dev, struct drm_agp_binding *request);
#else
static inline struct drm_agp_head *drm_legacy_agp_init(struct drm_device *dev)
{
	return NULL;
}

static inline int drm_legacy_agp_acquire(struct drm_device *dev)
{
	return -ENODEV;
}

static inline int drm_legacy_agp_release(struct drm_device *dev)
{
	return -ENODEV;
}

static inline int drm_legacy_agp_enable(struct drm_device *dev,
					struct drm_agp_mode mode)
{
	return -ENODEV;
}

static inline int drm_legacy_agp_info(struct drm_device *dev,
				      struct drm_agp_info *info)
{
	return -ENODEV;
}

static inline int drm_legacy_agp_alloc(struct drm_device *dev,
				       struct drm_agp_buffer *request)
{
	return -ENODEV;
}

static inline int drm_legacy_agp_free(struct drm_device *dev,
				      struct drm_agp_buffer *request)
{
	return -ENODEV;
}

static inline int drm_legacy_agp_unbind(struct drm_device *dev,
					struct drm_agp_binding *request)
{
	return -ENODEV;
}

static inline int drm_legacy_agp_bind(struct drm_device *dev,
				      struct drm_agp_binding *request)
{
	return -ENODEV;
}
#endif

/* drm_memory.c */
void drm_legacy_ioremap(struct drm_local_map *map, struct drm_device *dev);
void drm_legacy_ioremap_wc(struct drm_local_map *map, struct drm_device *dev);
void drm_legacy_ioremapfree(struct drm_local_map *map, struct drm_device *dev);

#endif /* __DRM_DRM_LEGACY_H__ */
