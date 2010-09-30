/**
 * \file drmP.h
 * Private header for Direct Rendering Manager
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
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

#ifndef _DRM_P_H_
#define _DRM_P_H_

#ifdef __KERNEL__
#ifdef __alpha__
/* add include of current.h so that "current" is defined
 * before static inline funcs in wait.h. Doing this so we
 * can build the DRM (part of PI DRI). 4/21/2000 S + B */
#include <asm/current.h>
#endif				/* __alpha__ */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#if defined(__alpha__) || defined(__powerpc__)
#include <asm/pgtable.h>	/* For pte_wrprotect */
#endif
#include <asm/io.h>
#include <asm/mman.h>
#include <asm/uaccess.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
#include <linux/types.h>
#include <linux/agp_backend.h>
#endif
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <asm/pgalloc.h>
#include "drm.h"

#include <linux/idr.h>

#define __OS_HAS_AGP (defined(CONFIG_AGP) || (defined(CONFIG_AGP_MODULE) && defined(MODULE)))
#define __OS_HAS_MTRR (defined(CONFIG_MTRR))

struct drm_file;
struct drm_device;

#include "drm_os_linux.h"
#include "drm_hashtab.h"
#include "drm_mm.h"

#define DRM_UT_CORE 		0x01
#define DRM_UT_DRIVER		0x02
#define DRM_UT_KMS		0x04
/*
 * Three debug levels are defined.
 * drm_core, drm_driver, drm_kms
 * drm_core level can be used in the generic drm code. For example:
 * 	drm_ioctl, drm_mm, drm_memory
 * The macro definiton of DRM_DEBUG is used.
 * 	DRM_DEBUG(fmt, args...)
 * 	The debug info by using the DRM_DEBUG can be obtained by adding
 * 	the boot option of "drm.debug=1".
 *
 * drm_driver level can be used in the specific drm driver. It is used
 * to add the debug info related with the drm driver. For example:
 * i915_drv, i915_dma, i915_gem, radeon_drv,
 * 	The macro definition of DRM_DEBUG_DRIVER can be used.
 * 	DRM_DEBUG_DRIVER(fmt, args...)
 * 	The debug info by using the DRM_DEBUG_DRIVER can be obtained by
 * 	adding the boot option of "drm.debug=0x02"
 *
 * drm_kms level can be used in the KMS code related with specific drm driver.
 * It is used to add the debug info related with KMS mode. For example:
 * the connector/crtc ,
 * 	The macro definition of DRM_DEBUG_KMS can be used.
 * 	DRM_DEBUG_KMS(fmt, args...)
 * 	The debug info by using the DRM_DEBUG_KMS can be obtained by
 * 	adding the boot option of "drm.debug=0x04"
 *
 * If we add the boot option of "drm.debug=0x06", we can get the debug info by
 * using the DRM_DEBUG_KMS and DRM_DEBUG_DRIVER.
 * If we add the boot option of "drm.debug=0x05", we can get the debug info by
 * using the DRM_DEBUG_KMS and DRM_DEBUG.
 */

extern void drm_ut_debug_printk(unsigned int request_level,
				const char *prefix,
				const char *function_name,
				const char *format, ...);
/***********************************************************************/
/** \name DRM template customization defaults */
/*@{*/

/* driver capabilities and requirements mask */
#define DRIVER_USE_AGP     0x1
#define DRIVER_REQUIRE_AGP 0x2
#define DRIVER_USE_MTRR    0x4
#define DRIVER_PCI_DMA     0x8
#define DRIVER_SG          0x10
#define DRIVER_HAVE_DMA    0x20
#define DRIVER_HAVE_IRQ    0x40
#define DRIVER_IRQ_SHARED  0x80
#define DRIVER_IRQ_VBL     0x100
#define DRIVER_DMA_QUEUE   0x200
#define DRIVER_FB_DMA      0x400
#define DRIVER_IRQ_VBL2    0x800
#define DRIVER_GEM         0x1000
#define DRIVER_MODESET     0x2000
#define DRIVER_USE_PLATFORM_DEVICE  0x4000

/***********************************************************************/
/** \name Begin the DRM... */
/*@{*/

#define DRM_DEBUG_CODE 2	  /**< Include debugging code if > 1, then
				     also include looping detection. */

#define DRM_MAGIC_HASH_ORDER  4  /**< Size of key hash table. Must be power of 2. */
#define DRM_KERNEL_CONTEXT    0	 /**< Change drm_resctx if changed */
#define DRM_RESERVED_CONTEXTS 1	 /**< Change drm_resctx if changed */
#define DRM_LOOPING_LIMIT     5000000
#define DRM_TIME_SLICE	      (HZ/20)  /**< Time slice for GLXContexts */
#define DRM_LOCK_SLICE	      1	/**< Time slice for lock, in jiffies */

#define DRM_FLAG_DEBUG	  0x01

#define DRM_MAX_CTXBITMAP (PAGE_SIZE * 8)
#define DRM_MAP_HASH_OFFSET 0x10000000

/*@}*/

/***********************************************************************/
/** \name Macros to make printk easier */
/*@{*/

/**
 * Error output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_ERROR(fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":%s] *ERROR* " fmt , __func__ , ##arg)

/**
 * Memory error output.
 *
 * \param area memory area where the error occurred.
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_MEM_ERROR(area, fmt, arg...) \
	printk(KERN_ERR "[" DRM_NAME ":%s:%s] *ERROR* " fmt , __func__, \
	       drm_mem_stats[area].name , ##arg)

#define DRM_INFO(fmt, arg...)  printk(KERN_INFO "[" DRM_NAME "] " fmt , ##arg)

/**
 * Debug output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#if DRM_DEBUG_CODE
#define DRM_DEBUG(fmt, args...)						\
	do {								\
		drm_ut_debug_printk(DRM_UT_CORE, DRM_NAME, 		\
					__func__, fmt, ##args);		\
	} while (0)

#define DRM_DEBUG_DRIVER(fmt, args...)					\
	do {								\
		drm_ut_debug_printk(DRM_UT_DRIVER, DRM_NAME,		\
					__func__, fmt, ##args);		\
	} while (0)
#define DRM_DEBUG_KMS(fmt, args...)				\
	do {								\
		drm_ut_debug_printk(DRM_UT_KMS, DRM_NAME, 		\
					 __func__, fmt, ##args);	\
	} while (0)
#define DRM_LOG(fmt, args...)						\
	do {								\
		drm_ut_debug_printk(DRM_UT_CORE, NULL,			\
					NULL, fmt, ##args);		\
	} while (0)
#define DRM_LOG_KMS(fmt, args...)					\
	do {								\
		drm_ut_debug_printk(DRM_UT_KMS, NULL,			\
					NULL, fmt, ##args);		\
	} while (0)
#define DRM_LOG_MODE(fmt, args...)					\
	do {								\
		drm_ut_debug_printk(DRM_UT_MODE, NULL,			\
					NULL, fmt, ##args);		\
	} while (0)
#define DRM_LOG_DRIVER(fmt, args...)					\
	do {								\
		drm_ut_debug_printk(DRM_UT_DRIVER, NULL,		\
					NULL, fmt, ##args);		\
	} while (0)
#else
#define DRM_DEBUG_DRIVER(fmt, args...) do { } while (0)
#define DRM_DEBUG_KMS(fmt, args...)	do { } while (0)
#define DRM_DEBUG(fmt, arg...)		 do { } while (0)
#define DRM_LOG(fmt, arg...)		do { } while (0)
#define DRM_LOG_KMS(fmt, args...) do { } while (0)
#define DRM_LOG_MODE(fmt, arg...) do { } while (0)
#define DRM_LOG_DRIVER(fmt, arg...) do { } while (0)

#endif

/*@}*/

/***********************************************************************/
/** \name Internal types and structures */
/*@{*/

#define DRM_ARRAY_SIZE(x) ARRAY_SIZE(x)

#define DRM_LEFTCOUNT(x) (((x)->rp + (x)->count - (x)->wp) % ((x)->count + 1))
#define DRM_BUFCOUNT(x) ((x)->count - DRM_LEFTCOUNT(x))

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

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

/**
 * Ioctl function type.
 *
 * \param inode device inode.
 * \param file_priv DRM file private pointer.
 * \param cmd command.
 * \param arg argument.
 */
typedef int drm_ioctl_t(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

typedef int drm_ioctl_compat_t(struct file *filp, unsigned int cmd,
			       unsigned long arg);

#define DRM_IOCTL_NR(n)                _IOC_NR(n)
#define DRM_MAJOR       226

#define DRM_AUTH	0x1
#define	DRM_MASTER	0x2
#define DRM_ROOT_ONLY	0x4
#define DRM_CONTROL_ALLOW 0x8
#define DRM_UNLOCKED	0x10

struct drm_ioctl_desc {
	unsigned int cmd;
	int flags;
	drm_ioctl_t *func;
	unsigned int cmd_drv;
};

/**
 * Creates a driver or general drm_ioctl_desc array entry for the given
 * ioctl, for use by drm_ioctl().
 */

#define DRM_IOCTL_DEF_DRV(ioctl, _func, _flags)			\
	[DRM_IOCTL_NR(DRM_##ioctl)] = {.cmd = DRM_##ioctl, .func = _func, .flags = _flags, .cmd_drv = DRM_IOCTL_##ioctl}

struct drm_magic_entry {
	struct list_head head;
	struct drm_hash_item hash_item;
	struct drm_file *priv;
};

struct drm_vma_entry {
	struct list_head head;
	struct vm_area_struct *vma;
	pid_t pid;
};

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
	wait_queue_head_t dma_wait;    /**< Processes waiting */
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

/** bufs is one longer than it has to be */
struct drm_waitlist {
	int count;			/**< Number of possible buffers */
	struct drm_buf **bufs;		/**< List of pointers to buffers */
	struct drm_buf **rp;			/**< Read pointer */
	struct drm_buf **wp;			/**< Write pointer */
	struct drm_buf **end;		/**< End pointer */
	spinlock_t read_lock;
	spinlock_t write_lock;
};

struct drm_freelist {
	int initialized;	       /**< Freelist in use */
	atomic_t count;		       /**< Number of free buffers */
	struct drm_buf *next;	       /**< End pointer */

	wait_queue_head_t waiting;     /**< Processes waiting on free bufs */
	int low_mark;		       /**< Low water mark */
	int high_mark;		       /**< High water mark */
	atomic_t wfh;		       /**< If waiting for high mark */
	spinlock_t lock;
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

	struct drm_freelist freelist;
};

/* Event queued up for userspace to read */
struct drm_pending_event {
	struct drm_event *event;
	struct list_head link;
	struct drm_file *file_priv;
	pid_t pid; /* pid of requester, no guarantee it's valid by the time
		      we deliver the event, for tracing only */
	void (*destroy)(struct drm_pending_event *event);
};

/** File private data */
struct drm_file {
	int authenticated;
	pid_t pid;
	uid_t uid;
	drm_magic_t magic;
	unsigned long ioctl_count;
	struct list_head lhead;
	struct drm_minor *minor;
	unsigned long lock_count;

	/** Mapping of mm object handles to object pointers. */
	struct idr object_idr;
	/** Lock for synchronization of access to object_idr. */
	spinlock_t table_lock;

	struct file *filp;
	void *driver_priv;

	int is_master; /* this file private is a master for a minor */
	struct drm_master *master; /* master this node is currently associated with
				      N.B. not always minor->master */
	struct list_head fbs;

	wait_queue_head_t event_wait;
	struct list_head event_list;
	int event_space;
};

/** Wait queue */
struct drm_queue {
	atomic_t use_count;		/**< Outstanding uses (+1) */
	atomic_t finalization;		/**< Finalization in progress */
	atomic_t block_count;		/**< Count of processes waiting */
	atomic_t block_read;		/**< Queue blocked for reads */
	wait_queue_head_t read_queue;	/**< Processes waiting on block_read */
	atomic_t block_write;		/**< Queue blocked for writes */
	wait_queue_head_t write_queue;	/**< Processes waiting on block_write */
	atomic_t total_queued;		/**< Total queued statistic */
	atomic_t total_flushed;		/**< Total flushes statistic */
	atomic_t total_locks;		/**< Total locks statistics */
	enum drm_ctx_flags flags;	/**< Context preserving and 2D-only */
	struct drm_waitlist waitlist;	/**< Pending buffers */
	wait_queue_head_t flush_queue;	/**< Processes waiting until flush */
};

/**
 * Lock data.
 */
struct drm_lock_data {
	struct drm_hw_lock *hw_lock;	/**< Hardware lock */
	/** Private of lock holder's file (NULL=kernel) */
	struct drm_file *file_priv;
	wait_queue_head_t lock_queue;	/**< Queue of blocked processes */
	unsigned long lock_time;	/**< Time of last lock in jiffies */
	spinlock_t spinlock;
	uint32_t kernel_waiters;
	uint32_t user_waiters;
	int idle_has_lock;
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
 * AGP memory entry.  Stored as a doubly linked list.
 */
struct drm_agp_mem {
	unsigned long handle;		/**< handle */
	DRM_AGP_MEM *memory;
	unsigned long bound;		/**< address */
	int pages;
	struct list_head head;
};

/**
 * AGP data.
 *
 * \sa drm_agp_init() and drm_device::agp.
 */
struct drm_agp_head {
	DRM_AGP_KERN agp_info;		/**< AGP device information */
	struct list_head memory;
	unsigned long mode;		/**< AGP mode */
	struct agp_bridge_data *bridge;
	int enabled;			/**< whether the AGP bus as been enabled */
	int acquired;			/**< whether the AGP device has been acquired */
	unsigned long base;
	int agp_mtrr;
	int cant_use_aperture;
	unsigned long page_mask;
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

struct drm_sigdata {
	int context;
	struct drm_hw_lock *lock;
};


/**
 * Kernel side of a mapping
 */
struct drm_local_map {
	resource_size_t offset;	 /**< Requested physical address (0 for SAREA)*/
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
	struct drm_mm_node *file_offset_node;	/**< fake offset */
};

/**
 * Context handle list
 */
struct drm_ctx_list {
	struct list_head head;		/**< list head */
	drm_context_t handle;		/**< context handle */
	struct drm_file *tag;		/**< associated fd private data */
};

/* location of GART table */
#define DRM_ATI_GART_MAIN 1
#define DRM_ATI_GART_FB   2

#define DRM_ATI_GART_PCI 1
#define DRM_ATI_GART_PCIE 2
#define DRM_ATI_GART_IGP 3

struct drm_ati_pcigart_info {
	int gart_table_location;
	int gart_reg_if;
	void *addr;
	dma_addr_t bus_addr;
	dma_addr_t table_mask;
	struct drm_dma_handle *table_handle;
	struct drm_local_map mapping;
	int table_size;
};

/**
 * GEM specific mm private for tracking GEM objects
 */
struct drm_gem_mm {
	struct drm_mm offset_manager;	/**< Offset mgmt for buffer objects */
	struct drm_open_hash offset_hash; /**< User token hash table for maps */
};

/**
 * This structure defines the drm_mm memory object, which will be used by the
 * DRM for its buffer objects.
 */
struct drm_gem_object {
	/** Reference count of this object */
	struct kref refcount;

	/** Handle count of this object. Each handle also holds a reference */
	atomic_t handle_count; /* number of handles on this object */

	/** Related drm device */
	struct drm_device *dev;

	/** File representing the shmem storage */
	struct file *filp;

	/* Mapping info for this object */
	struct drm_map_list map_list;

	/**
	 * Size of the object, in bytes.  Immutable over the object's
	 * lifetime.
	 */
	size_t size;

	/**
	 * Global name for this object, starts at 1. 0 means unnamed.
	 * Access is covered by the object_name_lock in the related drm_device
	 */
	int name;

	/**
	 * Memory domains. These monitor which caches contain read/write data
	 * related to the object. When transitioning from one set of domains
	 * to another, the driver is called to ensure that caches are suitably
	 * flushed and invalidated
	 */
	uint32_t read_domains;
	uint32_t write_domain;

	/**
	 * While validating an exec operation, the
	 * new read/write domain values are computed here.
	 * They will be transferred to the above values
	 * at the point that any cache flushing occurs
	 */
	uint32_t pending_read_domains;
	uint32_t pending_write_domain;

	void *driver_private;
};

#include "drm_crtc.h"

/* per-master structure */
struct drm_master {

	struct kref refcount; /* refcount for this master */

	struct list_head head; /**< each minor contains a list of masters */
	struct drm_minor *minor; /**< link back to minor we are a master for */

	char *unique;			/**< Unique identifier: e.g., busid */
	int unique_len;			/**< Length of unique field */
	int unique_size;		/**< amount allocated */

	int blocked;			/**< Blocked due to VC switch? */

	/** \name Authentication */
	/*@{ */
	struct drm_open_hash magiclist;
	struct list_head magicfree;
	/*@} */

	struct drm_lock_data lock;	/**< Information on hardware lock */

	void *driver_priv; /**< Private structure for driver to use */
};

/**
 * DRM driver structure. This structure represent the common code for
 * a family of cards. There will one drm_device for each card present
 * in this family
 */
struct drm_driver {
	int (*load) (struct drm_device *, unsigned long flags);
	int (*firstopen) (struct drm_device *);
	int (*open) (struct drm_device *, struct drm_file *);
	void (*preclose) (struct drm_device *, struct drm_file *file_priv);
	void (*postclose) (struct drm_device *, struct drm_file *);
	void (*lastclose) (struct drm_device *);
	int (*unload) (struct drm_device *);
	int (*suspend) (struct drm_device *, pm_message_t state);
	int (*resume) (struct drm_device *);
	int (*dma_ioctl) (struct drm_device *dev, void *data, struct drm_file *file_priv);
	void (*dma_ready) (struct drm_device *);
	int (*dma_quiescent) (struct drm_device *);
	int (*context_ctor) (struct drm_device *dev, int context);
	int (*context_dtor) (struct drm_device *dev, int context);
	int (*kernel_context_switch) (struct drm_device *dev, int old,
				      int new);
	void (*kernel_context_switch_unlock) (struct drm_device *dev);

	/**
	 * get_vblank_counter - get raw hardware vblank counter
	 * @dev: DRM device
	 * @crtc: counter to fetch
	 *
	 * Driver callback for fetching a raw hardware vblank counter
	 * for @crtc.  If a device doesn't have a hardware counter, the
	 * driver can simply return the value of drm_vblank_count and
	 * make the enable_vblank() and disable_vblank() hooks into no-ops,
	 * leaving interrupts enabled at all times.
	 *
	 * Wraparound handling and loss of events due to modesetting is dealt
	 * with in the DRM core code.
	 *
	 * RETURNS
	 * Raw vblank counter value.
	 */
	u32 (*get_vblank_counter) (struct drm_device *dev, int crtc);

	/**
	 * enable_vblank - enable vblank interrupt events
	 * @dev: DRM device
	 * @crtc: which irq to enable
	 *
	 * Enable vblank interrupts for @crtc.  If the device doesn't have
	 * a hardware vblank counter, this routine should be a no-op, since
	 * interrupts will have to stay on to keep the count accurate.
	 *
	 * RETURNS
	 * Zero on success, appropriate errno if the given @crtc's vblank
	 * interrupt cannot be enabled.
	 */
	int (*enable_vblank) (struct drm_device *dev, int crtc);

	/**
	 * disable_vblank - disable vblank interrupt events
	 * @dev: DRM device
	 * @crtc: which irq to enable
	 *
	 * Disable vblank interrupts for @crtc.  If the device doesn't have
	 * a hardware vblank counter, this routine should be a no-op, since
	 * interrupts will have to stay on to keep the count accurate.
	 */
	void (*disable_vblank) (struct drm_device *dev, int crtc);

	/**
	 * Called by \c drm_device_is_agp.  Typically used to determine if a
	 * card is really attached to AGP or not.
	 *
	 * \param dev  DRM device handle
	 *
	 * \returns
	 * One of three values is returned depending on whether or not the
	 * card is absolutely \b not AGP (return of 0), absolutely \b is AGP
	 * (return of 1), or may or may not be AGP (return of 2).
	 */
	int (*device_is_agp) (struct drm_device *dev);

	/* these have to be filled in */

	irqreturn_t(*irq_handler) (DRM_IRQ_ARGS);
	void (*irq_preinstall) (struct drm_device *dev);
	int (*irq_postinstall) (struct drm_device *dev);
	void (*irq_uninstall) (struct drm_device *dev);
	void (*reclaim_buffers) (struct drm_device *dev,
				 struct drm_file * file_priv);
	void (*reclaim_buffers_locked) (struct drm_device *dev,
					struct drm_file *file_priv);
	void (*reclaim_buffers_idlelocked) (struct drm_device *dev,
					    struct drm_file *file_priv);
	resource_size_t (*get_map_ofs) (struct drm_local_map * map);
	resource_size_t (*get_reg_ofs) (struct drm_device *dev);
	void (*set_version) (struct drm_device *dev,
			     struct drm_set_version *sv);

	/* Master routines */
	int (*master_create)(struct drm_device *dev, struct drm_master *master);
	void (*master_destroy)(struct drm_device *dev, struct drm_master *master);
	/**
	 * master_set is called whenever the minor master is set.
	 * master_drop is called whenever the minor master is dropped.
	 */

	int (*master_set)(struct drm_device *dev, struct drm_file *file_priv,
			  bool from_open);
	void (*master_drop)(struct drm_device *dev, struct drm_file *file_priv,
			    bool from_release);

	int (*proc_init)(struct drm_minor *minor);
	void (*proc_cleanup)(struct drm_minor *minor);
	int (*debugfs_init)(struct drm_minor *minor);
	void (*debugfs_cleanup)(struct drm_minor *minor);

	/**
	 * Driver-specific constructor for drm_gem_objects, to set up
	 * obj->driver_private.
	 *
	 * Returns 0 on success.
	 */
	int (*gem_init_object) (struct drm_gem_object *obj);
	void (*gem_free_object) (struct drm_gem_object *obj);

	/* vga arb irq handler */
	void (*vgaarb_irq)(struct drm_device *dev, bool state);

	/* Driver private ops for this object */
	struct vm_operations_struct *gem_vm_ops;

	int major;
	int minor;
	int patchlevel;
	char *name;
	char *desc;
	char *date;

	u32 driver_features;
	int dev_priv_size;
	struct drm_ioctl_desc *ioctls;
	int num_ioctls;
	struct file_operations fops;
	struct pci_driver pci_driver;
	struct platform_device *platform_device;
	/* List of devices hanging off this driver */
	struct list_head device_list;
};

#define DRM_MINOR_UNASSIGNED 0
#define DRM_MINOR_LEGACY 1
#define DRM_MINOR_CONTROL 2
#define DRM_MINOR_RENDER 3


/**
 * debugfs node list. This structure represents a debugfs file to
 * be created by the drm core
 */
struct drm_debugfs_list {
	const char *name; /** file name */
	int (*show)(struct seq_file*, void*); /** show callback */
	u32 driver_features; /**< Required driver features for this entry */
};

/**
 * debugfs node structure. This structure represents a debugfs file.
 */
struct drm_debugfs_node {
	struct list_head list;
	struct drm_minor *minor;
	struct drm_debugfs_list *debugfs_ent;
	struct dentry *dent;
};

/**
 * Info file list entry. This structure represents a debugfs or proc file to
 * be created by the drm core
 */
struct drm_info_list {
	const char *name; /** file name */
	int (*show)(struct seq_file*, void*); /** show callback */
	u32 driver_features; /**< Required driver features for this entry */
	void *data;
};

/**
 * debugfs node structure. This structure represents a debugfs file.
 */
struct drm_info_node {
	struct list_head list;
	struct drm_minor *minor;
	struct drm_info_list *info_ent;
	struct dentry *dent;
};

/**
 * DRM minor structure. This structure represents a drm minor number.
 */
struct drm_minor {
	int index;			/**< Minor device number */
	int type;                       /**< Control or render */
	dev_t device;			/**< Device number for mknod */
	struct device kdev;		/**< Linux device */
	struct drm_device *dev;

	struct proc_dir_entry *proc_root;  /**< proc directory entry */
	struct drm_info_node proc_nodes;
	struct dentry *debugfs_root;
	struct drm_info_node debugfs_nodes;

	struct drm_master *master; /* currently active master for this node */
	struct list_head master_list;
	struct drm_mode_group mode_group;
};

struct drm_pending_vblank_event {
	struct drm_pending_event base;
	int pipe;
	struct drm_event_vblank event;
};

/**
 * DRM device structure. This structure represent a complete card that
 * may contain multiple heads.
 */
struct drm_device {
	struct list_head driver_item;	/**< list of devices per driver */
	char *devname;			/**< For /proc/interrupts */
	int if_version;			/**< Highest interface version set */

	/** \name Locks */
	/*@{ */
	spinlock_t count_lock;		/**< For inuse, drm_device::open_count, drm_device::buf_use */
	struct mutex struct_mutex;	/**< For others */
	/*@} */

	/** \name Usage Counters */
	/*@{ */
	int open_count;			/**< Outstanding files open */
	atomic_t ioctl_count;		/**< Outstanding IOCTLs pending */
	atomic_t vma_count;		/**< Outstanding vma areas open */
	int buf_use;			/**< Buffers in use -- cannot alloc */
	atomic_t buf_alloc;		/**< Buffer allocation in progress */
	/*@} */

	/** \name Performance counters */
	/*@{ */
	unsigned long counters;
	enum drm_stat_type types[15];
	atomic_t counts[15];
	/*@} */

	struct list_head filelist;

	/** \name Memory management */
	/*@{ */
	struct list_head maplist;	/**< Linked list of regions */
	int map_count;			/**< Number of mappable regions */
	struct drm_open_hash map_hash;	/**< User token hash table for maps */

	/** \name Context handle management */
	/*@{ */
	struct list_head ctxlist;	/**< Linked list of context handles */
	int ctx_count;			/**< Number of context handles */
	struct mutex ctxlist_mutex;	/**< For ctxlist */

	struct idr ctx_idr;

	struct list_head vmalist;	/**< List of vmas (for debugging) */

	/*@} */

	/** \name DMA queues (contexts) */
	/*@{ */
	int queue_count;		/**< Number of active DMA queues */
	int queue_reserved;		  /**< Number of reserved DMA queues */
	int queue_slots;		/**< Actual length of queuelist */
	struct drm_queue **queuelist;	/**< Vector of pointers to DMA queues */
	struct drm_device_dma *dma;		/**< Optional pointer for DMA support */
	/*@} */

	/** \name Context support */
	/*@{ */
	int irq_enabled;		/**< True if irq handler is enabled */
	__volatile__ long context_flag;	/**< Context swapping flag */
	__volatile__ long interrupt_flag; /**< Interruption handler flag */
	__volatile__ long dma_flag;	/**< DMA dispatch flag */
	struct timer_list timer;	/**< Timer for delaying ctx switch */
	wait_queue_head_t context_wait;	/**< Processes waiting on ctx switch */
	int last_checked;		/**< Last context checked for DMA */
	int last_context;		/**< Last current context */
	unsigned long last_switch;	/**< jiffies at last context switch */
	/*@} */

	struct work_struct work;
	/** \name VBLANK IRQ support */
	/*@{ */

	/*
	 * At load time, disabling the vblank interrupt won't be allowed since
	 * old clients may not call the modeset ioctl and therefore misbehave.
	 * Once the modeset ioctl *has* been called though, we can safely
	 * disable them when unused.
	 */
	int vblank_disable_allowed;

	wait_queue_head_t *vbl_queue;   /**< VBLANK wait queue */
	atomic_t *_vblank_count;        /**< number of VBLANK interrupts (driver must alloc the right number of counters) */
	spinlock_t vbl_lock;
	atomic_t *vblank_refcount;      /* number of users of vblank interruptsper crtc */
	u32 *last_vblank;               /* protected by dev->vbl_lock, used */
					/* for wraparound handling */
	int *vblank_enabled;            /* so we don't call enable more than
					   once per disable */
	int *vblank_inmodeset;          /* Display driver is setting mode */
	u32 *last_vblank_wait;		/* Last vblank seqno waited per CRTC */
	struct timer_list vblank_disable_timer;

	u32 max_vblank_count;           /**< size of vblank counter register */

	/**
	 * List of events
	 */
	struct list_head vblank_event_list;
	spinlock_t event_lock;

	/*@} */
	cycles_t ctx_start;
	cycles_t lck_start;

	struct fasync_struct *buf_async;/**< Processes waiting for SIGIO */
	wait_queue_head_t buf_readers;	/**< Processes waiting to read */
	wait_queue_head_t buf_writers;	/**< Processes waiting to ctx switch */

	struct drm_agp_head *agp;	/**< AGP data */

	struct device *dev;             /**< Device structure */
	struct pci_dev *pdev;		/**< PCI device structure */
	int pci_vendor;			/**< PCI vendor id */
	int pci_device;			/**< PCI device id */
#ifdef __alpha__
	struct pci_controller *hose;
#endif

	struct platform_device *platformdev; /**< Platform device struture */

	struct drm_sg_mem *sg;	/**< Scatter gather memory */
	int num_crtcs;                  /**< Number of CRTCs on this device */
	void *dev_private;		/**< device private data */
	void *mm_private;
	struct address_space *dev_mapping;
	struct drm_sigdata sigdata;	   /**< For block_all_signals */
	sigset_t sigmask;

	struct drm_driver *driver;
	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;
	struct drm_minor *control;		/**< Control node for card */
	struct drm_minor *primary;		/**< render type primary screen head */

	/** \name Drawable information */
	/*@{ */
	spinlock_t drw_lock;
	struct idr drw_idr;
	/*@} */

        struct drm_mode_config mode_config;	/**< Current mode config */

	/** \name GEM information */
	/*@{ */
	spinlock_t object_name_lock;
	struct idr object_name_idr;
	atomic_t object_count;
	atomic_t object_memory;
	atomic_t pin_count;
	atomic_t pin_memory;
	atomic_t gtt_count;
	atomic_t gtt_memory;
	uint32_t gtt_total;
	uint32_t invalidate_domains;    /* domains pending invalidation */
	uint32_t flush_domains;         /* domains pending flush */
	/*@} */

};

static __inline__ int drm_core_check_feature(struct drm_device *dev,
					     int feature)
{
	return ((dev->driver->driver_features & feature) ? 1 : 0);
}

static inline int drm_dev_to_irq(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_USE_PLATFORM_DEVICE))
		return platform_get_irq(dev->platformdev, 0);
	else
		return dev->pdev->irq;
}

static inline int drm_get_pci_domain(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_USE_PLATFORM_DEVICE))
		return 0;

#ifndef __alpha__
	/* For historical reasons, drm_get_pci_domain() is busticated
	 * on most archs and has to remain so for userspace interface
	 * < 1.4, except on alpha which was right from the beginning
	 */
	if (dev->if_version < 0x10004)
		return 0;
#endif /* __alpha__ */

	return pci_domain_nr(dev->pdev->bus);
}

#if __OS_HAS_AGP
static inline int drm_core_has_AGP(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_USE_AGP);
}
#else
#define drm_core_has_AGP(dev) (0)
#endif

#if __OS_HAS_MTRR
static inline int drm_core_has_MTRR(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_USE_MTRR);
}

#define DRM_MTRR_WC		MTRR_TYPE_WRCOMB

static inline int drm_mtrr_add(unsigned long offset, unsigned long size,
			       unsigned int flags)
{
	return mtrr_add(offset, size, flags, 1);
}

static inline int drm_mtrr_del(int handle, unsigned long offset,
			       unsigned long size, unsigned int flags)
{
	return mtrr_del(handle, offset, size);
}

#else
#define drm_core_has_MTRR(dev) (0)

#define DRM_MTRR_WC		0

static inline int drm_mtrr_add(unsigned long offset, unsigned long size,
			       unsigned int flags)
{
	return 0;
}

static inline int drm_mtrr_del(int handle, unsigned long offset,
			       unsigned long size, unsigned int flags)
{
	return 0;
}
#endif

/******************************************************************/
/** \name Internal function definitions */
/*@{*/

				/* Driver support (drm_drv.h) */
extern int drm_init(struct drm_driver *driver);
extern void drm_exit(struct drm_driver *driver);
extern long drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg);
extern long drm_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern int drm_lastclose(struct drm_device *dev);

				/* Device support (drm_fops.h) */
extern struct mutex drm_global_mutex;
extern int drm_open(struct inode *inode, struct file *filp);
extern int drm_stub_open(struct inode *inode, struct file *filp);
extern int drm_fasync(int fd, struct file *filp, int on);
extern ssize_t drm_read(struct file *filp, char __user *buffer,
			size_t count, loff_t *offset);
extern int drm_release(struct inode *inode, struct file *filp);

				/* Mapping support (drm_vm.h) */
extern int drm_mmap(struct file *filp, struct vm_area_struct *vma);
extern int drm_mmap_locked(struct file *filp, struct vm_area_struct *vma);
extern void drm_vm_open_locked(struct vm_area_struct *vma);
extern void drm_vm_close_locked(struct vm_area_struct *vma);
extern resource_size_t drm_core_get_map_ofs(struct drm_local_map * map);
extern resource_size_t drm_core_get_reg_ofs(struct drm_device *dev);
extern unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait);

				/* Memory management support (drm_memory.h) */
#include "drm_memory.h"
extern void drm_mem_init(void);
extern int drm_mem_info(char *buf, char **start, off_t offset,
			int request, int *eof, void *data);
extern void *drm_realloc(void *oldpt, size_t oldsize, size_t size, int area);

extern DRM_AGP_MEM *drm_alloc_agp(struct drm_device *dev, int pages, u32 type);
extern int drm_free_agp(DRM_AGP_MEM * handle, int pages);
extern int drm_bind_agp(DRM_AGP_MEM * handle, unsigned int start);
extern DRM_AGP_MEM *drm_agp_bind_pages(struct drm_device *dev,
				       struct page **pages,
				       unsigned long num_pages,
				       uint32_t gtt_offset,
				       uint32_t type);
extern int drm_unbind_agp(DRM_AGP_MEM * handle);

				/* Misc. IOCTL support (drm_ioctl.h) */
extern int drm_irq_by_busid(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
extern int drm_getunique(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_setunique(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_getmap(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_getclient(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_getstats(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_setversion(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
extern int drm_noop(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);

				/* Context IOCTL support (drm_context.h) */
extern int drm_resctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_addctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_modctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_getctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_switchctx(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_newctx(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_rmctx(struct drm_device *dev, void *data,
		     struct drm_file *file_priv);

extern int drm_ctxbitmap_init(struct drm_device *dev);
extern void drm_ctxbitmap_cleanup(struct drm_device *dev);
extern void drm_ctxbitmap_free(struct drm_device *dev, int ctx_handle);

extern int drm_setsareactx(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
extern int drm_getsareactx(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);

				/* Drawable IOCTL support (drm_drawable.h) */
extern int drm_adddraw(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern int drm_rmdraw(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_update_drawable_info(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern struct drm_drawable_info *drm_get_drawable_info(struct drm_device *dev,
						  drm_drawable_t id);
extern void drm_drawable_free_all(struct drm_device *dev);

				/* Authentication IOCTL support (drm_auth.h) */
extern int drm_getmagic(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_authmagic(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);

/* Cache management (drm_cache.c) */
void drm_clflush_pages(struct page *pages[], unsigned long num_pages);

				/* Locking IOCTL support (drm_lock.h) */
extern int drm_lock(struct drm_device *dev, void *data,
		    struct drm_file *file_priv);
extern int drm_unlock(struct drm_device *dev, void *data,
		      struct drm_file *file_priv);
extern int drm_lock_take(struct drm_lock_data *lock_data, unsigned int context);
extern int drm_lock_free(struct drm_lock_data *lock_data, unsigned int context);
extern void drm_idlelock_take(struct drm_lock_data *lock_data);
extern void drm_idlelock_release(struct drm_lock_data *lock_data);

/*
 * These are exported to drivers so that they can implement fencing using
 * DMA quiscent + idle. DMA quiescent usually requires the hardware lock.
 */

extern int drm_i_have_hw_lock(struct drm_device *dev, struct drm_file *file_priv);

				/* Buffer management support (drm_bufs.h) */
extern int drm_addbufs_agp(struct drm_device *dev, struct drm_buf_desc * request);
extern int drm_addbufs_pci(struct drm_device *dev, struct drm_buf_desc * request);
extern int drm_addmap(struct drm_device *dev, resource_size_t offset,
		      unsigned int size, enum drm_map_type type,
		      enum drm_map_flags flags, struct drm_local_map **map_ptr);
extern int drm_addmap_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
extern int drm_rmmap(struct drm_device *dev, struct drm_local_map *map);
extern int drm_rmmap_locked(struct drm_device *dev, struct drm_local_map *map);
extern int drm_rmmap_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
extern int drm_addbufs(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern int drm_infobufs(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_markbufs(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_freebufs(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_mapbufs(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern int drm_order(unsigned long size);

				/* DMA support (drm_dma.h) */
extern int drm_dma_setup(struct drm_device *dev);
extern void drm_dma_takedown(struct drm_device *dev);
extern void drm_free_buffer(struct drm_device *dev, struct drm_buf * buf);
extern void drm_core_reclaim_buffers(struct drm_device *dev,
				     struct drm_file *filp);

				/* IRQ support (drm_irq.h) */
extern int drm_control(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
extern irqreturn_t drm_irq_handler(DRM_IRQ_ARGS);
extern int drm_irq_install(struct drm_device *dev);
extern int drm_irq_uninstall(struct drm_device *dev);
extern void drm_driver_irq_preinstall(struct drm_device *dev);
extern void drm_driver_irq_postinstall(struct drm_device *dev);
extern void drm_driver_irq_uninstall(struct drm_device *dev);

extern int drm_vblank_init(struct drm_device *dev, int num_crtcs);
extern int drm_wait_vblank(struct drm_device *dev, void *data,
			   struct drm_file *filp);
extern int drm_vblank_wait(struct drm_device *dev, unsigned int *vbl_seq);
extern u32 drm_vblank_count(struct drm_device *dev, int crtc);
extern void drm_handle_vblank(struct drm_device *dev, int crtc);
extern int drm_vblank_get(struct drm_device *dev, int crtc);
extern void drm_vblank_put(struct drm_device *dev, int crtc);
extern void drm_vblank_off(struct drm_device *dev, int crtc);
extern void drm_vblank_cleanup(struct drm_device *dev);
/* Modesetting support */
extern void drm_vblank_pre_modeset(struct drm_device *dev, int crtc);
extern void drm_vblank_post_modeset(struct drm_device *dev, int crtc);
extern int drm_modeset_ctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);

				/* AGP/GART support (drm_agpsupport.h) */
extern struct drm_agp_head *drm_agp_init(struct drm_device *dev);
extern int drm_agp_acquire(struct drm_device *dev);
extern int drm_agp_acquire_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int drm_agp_release(struct drm_device *dev);
extern int drm_agp_release_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int drm_agp_enable(struct drm_device *dev, struct drm_agp_mode mode);
extern int drm_agp_enable_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int drm_agp_info(struct drm_device *dev, struct drm_agp_info *info);
extern int drm_agp_info_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_agp_alloc(struct drm_device *dev, struct drm_agp_buffer *request);
extern int drm_agp_alloc_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int drm_agp_free(struct drm_device *dev, struct drm_agp_buffer *request);
extern int drm_agp_free_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_agp_unbind(struct drm_device *dev, struct drm_agp_binding *request);
extern int drm_agp_unbind_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
extern int drm_agp_bind(struct drm_device *dev, struct drm_agp_binding *request);
extern int drm_agp_bind_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern DRM_AGP_MEM *drm_agp_allocate_memory(struct agp_bridge_data *bridge, size_t pages, u32 type);
extern int drm_agp_free_memory(DRM_AGP_MEM * handle);
extern int drm_agp_bind_memory(DRM_AGP_MEM * handle, off_t start);
extern int drm_agp_unbind_memory(DRM_AGP_MEM * handle);
extern void drm_agp_chipset_flush(struct drm_device *dev);

				/* Stub support (drm_stub.h) */
extern int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
extern int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
struct drm_master *drm_master_create(struct drm_minor *minor);
extern struct drm_master *drm_master_get(struct drm_master *master);
extern void drm_master_put(struct drm_master **master);
extern int drm_get_pci_dev(struct pci_dev *pdev,
			   const struct pci_device_id *ent,
			   struct drm_driver *driver);
extern int drm_get_platform_dev(struct platform_device *pdev,
				struct drm_driver *driver);
extern void drm_put_dev(struct drm_device *dev);
extern int drm_put_minor(struct drm_minor **minor);
extern unsigned int drm_debug;

extern struct class *drm_class;
extern struct proc_dir_entry *drm_proc_root;
extern struct dentry *drm_debugfs_root;

extern struct idr drm_minors_idr;

extern struct drm_local_map *drm_getsarea(struct drm_device *dev);

				/* Proc support (drm_proc.h) */
extern int drm_proc_init(struct drm_minor *minor, int minor_id,
			 struct proc_dir_entry *root);
extern int drm_proc_cleanup(struct drm_minor *minor, struct proc_dir_entry *root);

				/* Debugfs support */
#if defined(CONFIG_DEBUG_FS)
extern int drm_debugfs_init(struct drm_minor *minor, int minor_id,
			    struct dentry *root);
extern int drm_debugfs_create_files(struct drm_info_list *files, int count,
				    struct dentry *root, struct drm_minor *minor);
extern int drm_debugfs_remove_files(struct drm_info_list *files, int count,
                                    struct drm_minor *minor);
extern int drm_debugfs_cleanup(struct drm_minor *minor);
#endif

				/* Info file support */
extern int drm_name_info(struct seq_file *m, void *data);
extern int drm_vm_info(struct seq_file *m, void *data);
extern int drm_queues_info(struct seq_file *m, void *data);
extern int drm_bufs_info(struct seq_file *m, void *data);
extern int drm_vblank_info(struct seq_file *m, void *data);
extern int drm_clients_info(struct seq_file *m, void* data);
extern int drm_gem_name_info(struct seq_file *m, void *data);
extern int drm_gem_object_info(struct seq_file *m, void* data);

#if DRM_DEBUG_CODE
extern int drm_vma_info(struct seq_file *m, void *data);
#endif

				/* Scatter Gather Support (drm_scatter.h) */
extern void drm_sg_cleanup(struct drm_sg_mem * entry);
extern int drm_sg_alloc_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
extern int drm_sg_alloc(struct drm_device *dev, struct drm_scatter_gather * request);
extern int drm_sg_free(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);

			       /* ATI PCIGART support (ati_pcigart.h) */
extern int drm_ati_pcigart_init(struct drm_device *dev,
				struct drm_ati_pcigart_info * gart_info);
extern int drm_ati_pcigart_cleanup(struct drm_device *dev,
				   struct drm_ati_pcigart_info * gart_info);

extern drm_dma_handle_t *drm_pci_alloc(struct drm_device *dev, size_t size,
				       size_t align);
extern void __drm_pci_free(struct drm_device *dev, drm_dma_handle_t * dmah);
extern void drm_pci_free(struct drm_device *dev, drm_dma_handle_t * dmah);

			       /* sysfs support (drm_sysfs.c) */
struct drm_sysfs_class;
extern struct class *drm_sysfs_create(struct module *owner, char *name);
extern void drm_sysfs_destroy(void);
extern int drm_sysfs_device_add(struct drm_minor *minor);
extern void drm_sysfs_hotplug_event(struct drm_device *dev);
extern void drm_sysfs_device_remove(struct drm_minor *minor);
extern char *drm_get_connector_status_name(enum drm_connector_status status);
extern int drm_sysfs_connector_add(struct drm_connector *connector);
extern void drm_sysfs_connector_remove(struct drm_connector *connector);

/* Graphics Execution Manager library functions (drm_gem.c) */
int drm_gem_init(struct drm_device *dev);
void drm_gem_destroy(struct drm_device *dev);
void drm_gem_object_release(struct drm_gem_object *obj);
void drm_gem_object_free(struct kref *kref);
struct drm_gem_object *drm_gem_object_alloc(struct drm_device *dev,
					    size_t size);
int drm_gem_object_init(struct drm_device *dev,
			struct drm_gem_object *obj, size_t size);
void drm_gem_object_handle_free(struct drm_gem_object *obj);
void drm_gem_vm_open(struct vm_area_struct *vma);
void drm_gem_vm_close(struct vm_area_struct *vma);
int drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);

#include "drm_global.h"

static inline void
drm_gem_object_reference(struct drm_gem_object *obj)
{
	kref_get(&obj->refcount);
}

static inline void
drm_gem_object_unreference(struct drm_gem_object *obj)
{
	if (obj != NULL)
		kref_put(&obj->refcount, drm_gem_object_free);
}

static inline void
drm_gem_object_unreference_unlocked(struct drm_gem_object *obj)
{
	if (obj != NULL) {
		struct drm_device *dev = obj->dev;
		mutex_lock(&dev->struct_mutex);
		kref_put(&obj->refcount, drm_gem_object_free);
		mutex_unlock(&dev->struct_mutex);
	}
}

int drm_gem_handle_create(struct drm_file *file_priv,
			  struct drm_gem_object *obj,
			  u32 *handlep);

static inline void
drm_gem_object_handle_reference(struct drm_gem_object *obj)
{
	drm_gem_object_reference(obj);
	atomic_inc(&obj->handle_count);
}

static inline void
drm_gem_object_handle_unreference(struct drm_gem_object *obj)
{
	if (obj == NULL)
		return;

	if (atomic_read(&obj->handle_count) == 0)
		return;
	/*
	 * Must bump handle count first as this may be the last
	 * ref, in which case the object would disappear before we
	 * checked for a name
	 */
	if (atomic_dec_and_test(&obj->handle_count))
		drm_gem_object_handle_free(obj);
	drm_gem_object_unreference(obj);
}

static inline void
drm_gem_object_handle_unreference_unlocked(struct drm_gem_object *obj)
{
	if (obj == NULL)
		return;

	if (atomic_read(&obj->handle_count) == 0)
		return;

	/*
	* Must bump handle count first as this may be the last
	* ref, in which case the object would disappear before we
	* checked for a name
	*/

	if (atomic_dec_and_test(&obj->handle_count))
		drm_gem_object_handle_free(obj);
	drm_gem_object_unreference_unlocked(obj);
}

struct drm_gem_object *drm_gem_object_lookup(struct drm_device *dev,
					     struct drm_file *filp,
					     u32 handle);
int drm_gem_close_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_gem_flink_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int drm_gem_open_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
void drm_gem_open(struct drm_device *dev, struct drm_file *file_private);
void drm_gem_release(struct drm_device *dev, struct drm_file *file_private);

extern void drm_core_ioremap(struct drm_local_map *map, struct drm_device *dev);
extern void drm_core_ioremap_wc(struct drm_local_map *map, struct drm_device *dev);
extern void drm_core_ioremapfree(struct drm_local_map *map, struct drm_device *dev);

static __inline__ struct drm_local_map *drm_core_findmap(struct drm_device *dev,
							 unsigned int token)
{
	struct drm_map_list *_entry;
	list_for_each_entry(_entry, &dev->maplist, head)
	    if (_entry->user_token == token)
		return _entry->map;
	return NULL;
}

static __inline__ int drm_device_is_agp(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_USE_PLATFORM_DEVICE))
		return 0;

	if (dev->driver->device_is_agp != NULL) {
		int err = (*dev->driver->device_is_agp) (dev);

		if (err != 2) {
			return err;
		}
	}

	return pci_find_capability(dev->pdev, PCI_CAP_ID_AGP);
}

static __inline__ int drm_device_is_pcie(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_USE_PLATFORM_DEVICE))
		return 0;
	else
		return pci_find_capability(dev->pdev, PCI_CAP_ID_EXP);
}

static __inline__ void drm_core_dropmap(struct drm_local_map *map)
{
}

#include "drm_mem_util.h"

static inline void *drm_get_device(struct drm_device *dev)
{
	if (drm_core_check_feature(dev, DRIVER_USE_PLATFORM_DEVICE))
		return dev->platformdev;
	else
		return dev->pdev;
}

extern int drm_platform_init(struct drm_driver *driver);
extern int drm_pci_init(struct drm_driver *driver);
extern int drm_fill_in_dev(struct drm_device *dev,
			   const struct pci_device_id *ent,
			   struct drm_driver *driver);
int drm_get_minor(struct drm_device *dev, struct drm_minor **minor, int type);
/*@}*/

#endif				/* __KERNEL__ */
#endif
