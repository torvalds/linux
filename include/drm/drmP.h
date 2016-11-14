/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
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

#ifndef _DRM_P_H_
#define _DRM_P_H_

#include <linux/agp_backend.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/ratelimit.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/dma-fence.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>

#include <uapi/drm/drm.h>
#include <uapi/drm/drm_mode.h>

#include <drm/drm_agpsupport.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_global.h>
#include <drm/drm_hashtab.h>
#include <drm/drm_mem_util.h>
#include <drm/drm_mm.h>
#include <drm/drm_os_linux.h>
#include <drm/drm_sarea.h>
#include <drm/drm_vma_manager.h>
#include <drm/drm_drv.h>

struct module;

struct drm_file;
struct drm_device;
struct drm_agp_head;
struct drm_local_map;
struct drm_device_dma;
struct drm_dma_handle;
struct drm_gem_object;
struct drm_master;
struct drm_vblank_crtc;

struct device_node;
struct videomode;
struct reservation_object;
struct dma_buf_attachment;

/*
 * The following categories are defined:
 *
 * CORE: Used in the generic drm code: drm_ioctl.c, drm_mm.c, drm_memory.c, ...
 *	 This is the category used by the DRM_DEBUG() macro.
 *
 * DRIVER: Used in the vendor specific part of the driver: i915, radeon, ...
 *	   This is the category used by the DRM_DEBUG_DRIVER() macro.
 *
 * KMS: used in the modesetting code.
 *	This is the category used by the DRM_DEBUG_KMS() macro.
 *
 * PRIME: used in the prime code.
 *	  This is the category used by the DRM_DEBUG_PRIME() macro.
 *
 * ATOMIC: used in the atomic code.
 *	  This is the category used by the DRM_DEBUG_ATOMIC() macro.
 *
 * VBL: used for verbose debug message in the vblank code
 *	  This is the category used by the DRM_DEBUG_VBL() macro.
 *
 * Enabling verbose debug messages is done through the drm.debug parameter,
 * each category being enabled by a bit.
 *
 * drm.debug=0x1 will enable CORE messages
 * drm.debug=0x2 will enable DRIVER messages
 * drm.debug=0x3 will enable CORE and DRIVER messages
 * ...
 * drm.debug=0x3f will enable all messages
 *
 * An interesting feature is that it's possible to enable verbose logging at
 * run-time by echoing the debug value in its sysfs node:
 *   # echo 0xf > /sys/module/drm/parameters/debug
 */
#define DRM_UT_NONE		0x00
#define DRM_UT_CORE 		0x01
#define DRM_UT_DRIVER		0x02
#define DRM_UT_KMS		0x04
#define DRM_UT_PRIME		0x08
#define DRM_UT_ATOMIC		0x10
#define DRM_UT_VBL		0x20
#define DRM_UT_STATE		0x40

/***********************************************************************/
/** \name DRM template customization defaults */
/*@{*/

/***********************************************************************/
/** \name Macros to make printk easier */
/*@{*/

#define _DRM_PRINTK(once, level, fmt, ...)				\
	do {								\
		printk##once(KERN_##level "[" DRM_NAME "] " fmt,	\
			     ##__VA_ARGS__);				\
	} while (0)

#define DRM_INFO(fmt, ...)						\
	_DRM_PRINTK(, INFO, fmt, ##__VA_ARGS__)
#define DRM_NOTE(fmt, ...)						\
	_DRM_PRINTK(, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN(fmt, ...)						\
	_DRM_PRINTK(, WARNING, fmt, ##__VA_ARGS__)

#define DRM_INFO_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, INFO, fmt, ##__VA_ARGS__)
#define DRM_NOTE_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, NOTICE, fmt, ##__VA_ARGS__)
#define DRM_WARN_ONCE(fmt, ...)						\
	_DRM_PRINTK(_once, WARNING, fmt, ##__VA_ARGS__)

/**
 * Error output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_DEV_ERROR(dev, fmt, ...)					\
	drm_dev_printk(dev, KERN_ERR, DRM_UT_NONE, __func__, " *ERROR*",\
		       fmt, ##__VA_ARGS__)
#define DRM_ERROR(fmt, ...)						\
	drm_printk(KERN_ERR, DRM_UT_NONE, fmt,	##__VA_ARGS__)

/**
 * Rate limited error output.  Like DRM_ERROR() but won't flood the log.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_DEV_ERROR_RATELIMITED(dev, fmt, ...)			\
({									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
									\
	if (__ratelimit(&_rs))						\
		DRM_DEV_ERROR(dev, fmt, ##__VA_ARGS__);			\
})
#define DRM_ERROR_RATELIMITED(fmt, ...)					\
	DRM_DEV_ERROR_RATELIMITED(NULL, fmt, ##__VA_ARGS__)

#define DRM_DEV_INFO(dev, fmt, ...)					\
	drm_dev_printk(dev, KERN_INFO, DRM_UT_NONE, __func__, "", fmt,	\
		       ##__VA_ARGS__)

#define DRM_DEV_INFO_ONCE(dev, fmt, ...)				\
({									\
	static bool __print_once __read_mostly;				\
	if (!__print_once) {						\
		__print_once = true;					\
		DRM_DEV_INFO(dev, fmt, ##__VA_ARGS__);			\
	}								\
})

/**
 * Debug output.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_DEV_DEBUG(dev, fmt, args...)				\
	drm_dev_printk(dev, KERN_DEBUG, DRM_UT_CORE, __func__, "", fmt,	\
		       ##args)
#define DRM_DEBUG(fmt, ...)						\
	drm_printk(KERN_DEBUG, DRM_UT_CORE, fmt, ##__VA_ARGS__)

#define DRM_DEV_DEBUG_DRIVER(dev, fmt, args...)				\
	drm_dev_printk(dev, KERN_DEBUG, DRM_UT_DRIVER, __func__, "",	\
		       fmt, ##args)
#define DRM_DEBUG_DRIVER(fmt, ...)					\
	drm_printk(KERN_DEBUG, DRM_UT_DRIVER, fmt, ##__VA_ARGS__)

#define DRM_DEV_DEBUG_KMS(dev, fmt, args...)				\
	drm_dev_printk(dev, KERN_DEBUG, DRM_UT_KMS, __func__, "", fmt,	\
		       ##args)
#define DRM_DEBUG_KMS(fmt, ...)					\
	drm_printk(KERN_DEBUG, DRM_UT_KMS, fmt, ##__VA_ARGS__)

#define DRM_DEV_DEBUG_PRIME(dev, fmt, args...)				\
	drm_dev_printk(dev, KERN_DEBUG, DRM_UT_PRIME, __func__, "",	\
		       fmt, ##args)
#define DRM_DEBUG_PRIME(fmt, ...)					\
	drm_printk(KERN_DEBUG, DRM_UT_PRIME, fmt, ##__VA_ARGS__)

#define DRM_DEV_DEBUG_ATOMIC(dev, fmt, args...)				\
	drm_dev_printk(dev, KERN_DEBUG, DRM_UT_ATOMIC, __func__, "",	\
		       fmt, ##args)
#define DRM_DEBUG_ATOMIC(fmt, ...)					\
	drm_printk(KERN_DEBUG, DRM_UT_ATOMIC, fmt, ##__VA_ARGS__)

#define DRM_DEV_DEBUG_VBL(dev, fmt, args...)				\
	drm_dev_printk(dev, KERN_DEBUG, DRM_UT_VBL, __func__, "", fmt,	\
		       ##args)
#define DRM_DEBUG_VBL(fmt, ...)					\
	drm_printk(KERN_DEBUG, DRM_UT_VBL, fmt, ##__VA_ARGS__)

#define _DRM_DEV_DEFINE_DEBUG_RATELIMITED(dev, level, fmt, args...)	\
({									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	if (__ratelimit(&_rs))						\
		drm_dev_printk(dev, KERN_DEBUG, DRM_UT_ ## level,	\
			       __func__, "", fmt, ##args);		\
})

/**
 * Rate limited debug output. Like DRM_DEBUG() but won't flood the log.
 *
 * \param fmt printf() like format string.
 * \param arg arguments
 */
#define DRM_DEV_DEBUG_RATELIMITED(dev, fmt, args...)			\
	DEV__DRM_DEFINE_DEBUG_RATELIMITED(dev, CORE, fmt, ##args)
#define DRM_DEBUG_RATELIMITED(fmt, args...)				\
	DRM_DEV_DEBUG_RATELIMITED(NULL, fmt, ##args)
#define DRM_DEV_DEBUG_DRIVER_RATELIMITED(dev, fmt, args...)		\
	_DRM_DEV_DEFINE_DEBUG_RATELIMITED(dev, DRIVER, fmt, ##args)
#define DRM_DEBUG_DRIVER_RATELIMITED(fmt, args...)			\
	DRM_DEV_DEBUG_DRIVER_RATELIMITED(NULL, fmt, ##args)
#define DRM_DEV_DEBUG_KMS_RATELIMITED(dev, fmt, args...)		\
	_DRM_DEV_DEFINE_DEBUG_RATELIMITED(dev, KMS, fmt, ##args)
#define DRM_DEBUG_KMS_RATELIMITED(fmt, args...)				\
	DRM_DEV_DEBUG_KMS_RATELIMITED(NULL, fmt, ##args)
#define DRM_DEV_DEBUG_PRIME_RATELIMITED(dev, fmt, args...)		\
	_DRM_DEV_DEFINE_DEBUG_RATELIMITED(dev, PRIME, fmt, ##args)
#define DRM_DEBUG_PRIME_RATELIMITED(fmt, args...)			\
	DRM_DEV_DEBUG_PRIME_RATELIMITED(NULL, fmt, ##args)

/* Format strings and argument splitters to simplify printing
 * various "complex" objects
 */
#define DRM_MODE_FMT    "%d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x"
#define DRM_MODE_ARG(m) \
	(m)->base.id, (m)->name, (m)->vrefresh, (m)->clock, \
	(m)->hdisplay, (m)->hsync_start, (m)->hsync_end, (m)->htotal, \
	(m)->vdisplay, (m)->vsync_start, (m)->vsync_end, (m)->vtotal, \
	(m)->type, (m)->flags

#define DRM_RECT_FMT    "%dx%d%+d%+d"
#define DRM_RECT_ARG(r) drm_rect_width(r), drm_rect_height(r), (r)->x1, (r)->y1

/* for rect's in fixed-point format: */
#define DRM_RECT_FP_FMT "%d.%06ux%d.%06u%+d.%06u%+d.%06u"
#define DRM_RECT_FP_ARG(r) \
		drm_rect_width(r) >> 16, ((drm_rect_width(r) & 0xffff) * 15625) >> 10, \
		drm_rect_height(r) >> 16, ((drm_rect_height(r) & 0xffff) * 15625) >> 10, \
		(r)->x1 >> 16, (((r)->x1 & 0xffff) * 15625) >> 10, \
		(r)->y1 >> 16, (((r)->y1 & 0xffff) * 15625) >> 10

/*@}*/

/***********************************************************************/
/** \name Internal types and structures */
/*@{*/

#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

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
#define DRM_RENDER_ALLOW 0x20

struct drm_ioctl_desc {
	unsigned int cmd;
	int flags;
	drm_ioctl_t *func;
	const char *name;
};

/**
 * Creates a driver or general drm_ioctl_desc array entry for the given
 * ioctl, for use by drm_ioctl().
 */

#define DRM_IOCTL_DEF_DRV(ioctl, _func, _flags)				\
	[DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = {	\
		.cmd = DRM_IOCTL_##ioctl,				\
		.func = _func,						\
		.flags = _flags,					\
		.name = #ioctl						\
	 }

/* Event queued up for userspace to read */
struct drm_pending_event {
	struct completion *completion;
	struct drm_event *event;
	struct dma_fence *fence;
	struct list_head link;
	struct list_head pending_link;
	struct drm_file *file_priv;
	pid_t pid; /* pid of requester, no guarantee it's valid by the time
		      we deliver the event, for tracing only */
};

struct drm_prime_file_private {
	struct mutex lock;
	struct rb_root dmabufs;
	struct rb_root handles;
};

/** File private data */
struct drm_file {
	unsigned authenticated :1;
	/* true when the client has asked us to expose stereo 3D mode flags */
	unsigned stereo_allowed :1;
	/*
	 * true if client understands CRTC primary planes and cursor planes
	 * in the plane list
	 */
	unsigned universal_planes:1;
	/* true if client understands atomic properties */
	unsigned atomic:1;
	/*
	 * This client is the creator of @master.
	 * Protected by struct drm_device::master_mutex.
	 */
	unsigned is_master:1;

	struct pid *pid;
	drm_magic_t magic;
	struct list_head lhead;
	struct drm_minor *minor;
	unsigned long lock_count;

	/** Mapping of mm object handles to object pointers. */
	struct idr object_idr;
	/** Lock for synchronization of access to object_idr. */
	spinlock_t table_lock;

	struct file *filp;
	void *driver_priv;

	struct drm_master *master; /* master this node is currently associated with
				      N.B. not always dev->master */
	/**
	 * fbs - List of framebuffers associated with this file.
	 *
	 * Protected by fbs_lock. Note that the fbs list holds a reference on
	 * the fb object to prevent it from untimely disappearing.
	 */
	struct list_head fbs;
	struct mutex fbs_lock;

	/** User-created blob properties; this retains a reference on the
	 *  property. */
	struct list_head blobs;

	wait_queue_head_t event_wait;
	struct list_head pending_event_list;
	struct list_head event_list;
	int event_space;

	struct mutex event_read_lock;

	struct drm_prime_file_private prime;
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

/* Flags and return codes for get_vblank_timestamp() driver function. */
#define DRM_CALLED_FROM_VBLIRQ 1
#define DRM_VBLANKTIME_SCANOUTPOS_METHOD (1 << 0)
#define DRM_VBLANKTIME_IN_VBLANK         (1 << 1)

/* get_scanout_position() return flags */
#define DRM_SCANOUTPOS_VALID        (1 << 0)
#define DRM_SCANOUTPOS_IN_VBLANK    (1 << 1)
#define DRM_SCANOUTPOS_ACCURATE     (1 << 2)

enum drm_minor_type {
	DRM_MINOR_PRIMARY,
	DRM_MINOR_CONTROL,
	DRM_MINOR_RENDER,
	DRM_MINOR_CNT,
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
	const struct drm_info_list *info_ent;
	struct dentry *dent;
};

/**
 * DRM minor structure. This structure represents a drm minor number.
 */
struct drm_minor {
	int index;			/**< Minor device number */
	int type;                       /**< Control or render */
	struct device *kdev;		/**< Linux device */
	struct drm_device *dev;

	struct dentry *debugfs_root;

	struct list_head debugfs_list;
	struct mutex debugfs_lock; /* Protects debugfs_list. */
};

/**
 * DRM device structure. This structure represent a complete card that
 * may contain multiple heads.
 */
struct drm_device {
	struct list_head legacy_dev_list;/**< list of devices per driver for stealth attach cleanup */
	int if_version;			/**< Highest interface version set */

	/** \name Lifetime Management */
	/*@{ */
	struct kref ref;		/**< Object ref-count */
	struct device *dev;		/**< Device structure of bus-device */
	struct drm_driver *driver;	/**< DRM driver managing the device */
	void *dev_private;		/**< DRM driver private data */
	struct drm_minor *control;		/**< Control node */
	struct drm_minor *primary;		/**< Primary node */
	struct drm_minor *render;		/**< Render node */

	/* currently active master for this device. Protected by master_mutex */
	struct drm_master *master;

	atomic_t unplugged;			/**< Flag whether dev is dead */
	struct inode *anon_inode;		/**< inode for private address-space */
	char *unique;				/**< unique name of the device */
	/*@} */

	/** \name Locks */
	/*@{ */
	struct mutex struct_mutex;	/**< For others */
	struct mutex master_mutex;      /**< For drm_minor::master and drm_file::is_master */
	/*@} */

	/** \name Usage Counters */
	/*@{ */
	int open_count;			/**< Outstanding files open, protected by drm_global_mutex. */
	spinlock_t buf_lock;		/**< For drm_device::buf_use and a few other things. */
	int buf_use;			/**< Buffers in use -- cannot alloc */
	atomic_t buf_alloc;		/**< Buffer allocation in progress */
	/*@} */

	struct mutex filelist_mutex;
	struct list_head filelist;

	/** \name Memory management */
	/*@{ */
	struct list_head maplist;	/**< Linked list of regions */
	struct drm_open_hash map_hash;	/**< User token hash table for maps */

	/** \name Context handle management */
	/*@{ */
	struct list_head ctxlist;	/**< Linked list of context handles */
	struct mutex ctxlist_mutex;	/**< For ctxlist */

	struct idr ctx_idr;

	struct list_head vmalist;	/**< List of vmas (for debugging) */

	/*@} */

	/** \name DMA support */
	/*@{ */
	struct drm_device_dma *dma;		/**< Optional pointer for DMA support */
	/*@} */

	/** \name Context support */
	/*@{ */

	__volatile__ long context_flag;	/**< Context swapping flag */
	int last_context;		/**< Last current context */
	/*@} */

	/** \name VBLANK IRQ support */
	/*@{ */
	bool irq_enabled;
	int irq;

	/*
	 * If true, vblank interrupt will be disabled immediately when the
	 * refcount drops to zero, as opposed to via the vblank disable
	 * timer.
	 * This can be set to true it the hardware has a working vblank
	 * counter and the driver uses drm_vblank_on() and drm_vblank_off()
	 * appropriately.
	 */
	bool vblank_disable_immediate;

	/* array of size num_crtcs */
	struct drm_vblank_crtc *vblank;

	spinlock_t vblank_time_lock;    /**< Protects vblank count and time updates during vblank enable/disable */
	spinlock_t vbl_lock;

	u32 max_vblank_count;           /**< size of vblank counter register */

	/**
	 * List of events
	 */
	struct list_head vblank_event_list;
	spinlock_t event_lock;

	/*@} */

	struct drm_agp_head *agp;	/**< AGP data */

	struct pci_dev *pdev;		/**< PCI device structure */
#ifdef __alpha__
	struct pci_controller *hose;
#endif

	struct platform_device *platformdev; /**< Platform device struture */
	struct virtio_device *virtdev;

	struct drm_sg_mem *sg;	/**< Scatter gather memory */
	unsigned int num_crtcs;                  /**< Number of CRTCs on this device */

	struct {
		int context;
		struct drm_hw_lock *lock;
	} sigdata;

	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;

	struct drm_mode_config mode_config;	/**< Current mode config */

	/** \name GEM information */
	/*@{ */
	struct mutex object_name_lock;
	struct idr object_name_idr;
	struct drm_vma_offset_manager *vma_offset_manager;
	/*@} */
	int switch_power_state;
};

#include <drm/drm_irq.h>

#define DRM_SWITCH_POWER_ON 0
#define DRM_SWITCH_POWER_OFF 1
#define DRM_SWITCH_POWER_CHANGING 2
#define DRM_SWITCH_POWER_DYNAMIC_OFF 3

static __inline__ int drm_core_check_feature(struct drm_device *dev,
					     int feature)
{
	return ((dev->driver->driver_features & feature) ? 1 : 0);
}

static inline void drm_device_set_unplugged(struct drm_device *dev)
{
	smp_wmb();
	atomic_set(&dev->unplugged, 1);
}

static inline int drm_device_is_unplugged(struct drm_device *dev)
{
	int ret = atomic_read(&dev->unplugged);
	smp_rmb();
	return ret;
}

static inline bool drm_is_render_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_RENDER;
}

static inline bool drm_is_control_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_CONTROL;
}

static inline bool drm_is_primary_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_PRIMARY;
}

/******************************************************************/
/** \name Internal function definitions */
/*@{*/

				/* Driver support (drm_drv.h) */
extern int drm_ioctl_permit(u32 flags, struct drm_file *file_priv);
extern long drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
extern long drm_compat_ioctl(struct file *filp,
			     unsigned int cmd, unsigned long arg);
#else
/* Let drm_compat_ioctl be assigned to .compat_ioctl unconditionally */
#define drm_compat_ioctl NULL
#endif
extern bool drm_ioctl_flags(unsigned int nr, unsigned int *flags);

/* File Operations (drm_fops.c) */
int drm_open(struct inode *inode, struct file *filp);
ssize_t drm_read(struct file *filp, char __user *buffer,
		 size_t count, loff_t *offset);
int drm_release(struct inode *inode, struct file *filp);
unsigned int drm_poll(struct file *filp, struct poll_table_struct *wait);
int drm_event_reserve_init_locked(struct drm_device *dev,
				  struct drm_file *file_priv,
				  struct drm_pending_event *p,
				  struct drm_event *e);
int drm_event_reserve_init(struct drm_device *dev,
			   struct drm_file *file_priv,
			   struct drm_pending_event *p,
			   struct drm_event *e);
void drm_event_cancel_free(struct drm_device *dev,
			   struct drm_pending_event *p);
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e);
void drm_send_event(struct drm_device *dev, struct drm_pending_event *e);

/* Misc. IOCTL support (drm_ioctl.c) */
int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv);
int drm_invalid_op(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);

/* Cache management (drm_cache.c) */
void drm_clflush_pages(struct page *pages[], unsigned long num_pages);
void drm_clflush_sg(struct sg_table *st);
void drm_clflush_virt_range(void *addr, unsigned long length);

/*
 * These are exported to drivers so that they can implement fencing using
 * DMA quiscent + idle. DMA quiescent usually requires the hardware lock.
 */

				/* Debugfs support */
#if defined(CONFIG_DEBUG_FS)
extern int drm_debugfs_create_files(const struct drm_info_list *files,
				    int count, struct dentry *root,
				    struct drm_minor *minor);
extern int drm_debugfs_remove_files(const struct drm_info_list *files,
				    int count, struct drm_minor *minor);
#else
static inline int drm_debugfs_create_files(const struct drm_info_list *files,
					   int count, struct dentry *root,
					   struct drm_minor *minor)
{
	return 0;
}

static inline int drm_debugfs_remove_files(const struct drm_info_list *files,
					   int count, struct drm_minor *minor)
{
	return 0;
}
#endif

struct dma_buf_export_info;

extern struct dma_buf *drm_gem_prime_export(struct drm_device *dev,
					    struct drm_gem_object *obj,
					    int flags);
extern int drm_gem_prime_handle_to_fd(struct drm_device *dev,
		struct drm_file *file_priv, uint32_t handle, uint32_t flags,
		int *prime_fd);
extern struct drm_gem_object *drm_gem_prime_import(struct drm_device *dev,
		struct dma_buf *dma_buf);
extern int drm_gem_prime_fd_to_handle(struct drm_device *dev,
		struct drm_file *file_priv, int prime_fd, uint32_t *handle);
struct dma_buf *drm_gem_dmabuf_export(struct drm_device *dev,
				      struct dma_buf_export_info *exp_info);
extern void drm_gem_dmabuf_release(struct dma_buf *dma_buf);

extern int drm_prime_sg_to_page_addr_arrays(struct sg_table *sgt, struct page **pages,
					    dma_addr_t *addrs, int max_pages);
extern struct sg_table *drm_prime_pages_to_sg(struct page **pages, unsigned int nr_pages);
extern void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg);


extern struct drm_dma_handle *drm_pci_alloc(struct drm_device *dev, size_t size,
					    size_t align);
extern void drm_pci_free(struct drm_device *dev, struct drm_dma_handle * dmah);

			       /* sysfs support (drm_sysfs.c) */
extern void drm_sysfs_hotplug_event(struct drm_device *dev);



/*@}*/

/* PCI section */
static __inline__ int drm_pci_device_is_agp(struct drm_device *dev)
{
	if (dev->driver->device_is_agp != NULL) {
		int err = (*dev->driver->device_is_agp) (dev);

		if (err != 2) {
			return err;
		}
	}

	return pci_find_capability(dev->pdev, PCI_CAP_ID_AGP);
}
void drm_pci_agp_destroy(struct drm_device *dev);

extern int drm_pci_init(struct drm_driver *driver, struct pci_driver *pdriver);
extern void drm_pci_exit(struct drm_driver *driver, struct pci_driver *pdriver);
#ifdef CONFIG_PCI
extern int drm_get_pci_dev(struct pci_dev *pdev,
			   const struct pci_device_id *ent,
			   struct drm_driver *driver);
extern int drm_pci_set_busid(struct drm_device *dev, struct drm_master *master);
#else
static inline int drm_get_pci_dev(struct pci_dev *pdev,
				  const struct pci_device_id *ent,
				  struct drm_driver *driver)
{
	return -ENOSYS;
}

static inline int drm_pci_set_busid(struct drm_device *dev,
				    struct drm_master *master)
{
	return -ENOSYS;
}
#endif

#define DRM_PCIE_SPEED_25 1
#define DRM_PCIE_SPEED_50 2
#define DRM_PCIE_SPEED_80 4

extern int drm_pcie_get_speed_cap_mask(struct drm_device *dev, u32 *speed_mask);
extern int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw);

/* platform section */
extern int drm_platform_init(struct drm_driver *driver, struct platform_device *platform_device);

/* returns true if currently okay to sleep */
static __inline__ bool drm_can_sleep(void)
{
	if (in_atomic() || in_dbg_master() || irqs_disabled())
		return false;
	return true;
}

/* helper for handling conditionals in various for_each macros */
#define for_each_if(condition) if (!(condition)) {} else

#endif
