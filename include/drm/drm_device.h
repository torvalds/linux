#ifndef _DRM_DEVICE_H_
#define _DRM_DEVICE_H_

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/idr.h>

#include <drm/drm_hashtab.h>
#include <drm/drm_mode_config.h>

struct drm_driver;
struct drm_minor;
struct drm_master;
struct drm_device_dma;
struct drm_vblank_crtc;
struct drm_sg_mem;
struct drm_local_map;
struct drm_vma_offset_manager;
struct drm_fb_helper;

struct inode;

struct pci_dev;
struct pci_controller;

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
	struct drm_minor *primary;		/**< Primary node */
	struct drm_minor *render;		/**< Render node */
	bool registered;

	/* currently active master for this device. Protected by master_mutex */
	struct drm_master *master;

	/**
	 * @driver_features: per-device driver features
	 *
	 * Drivers can clear specific flags here to disallow
	 * certain features on a per-device basis while still
	 * sharing a single &struct drm_driver instance across
	 * all devices.
	 */
	u32 driver_features;

	/**
	 * @unplugged:
	 *
	 * Flag to tell if the device has been unplugged.
	 * See drm_dev_enter() and drm_dev_is_unplugged().
	 */
	bool unplugged;

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

	/**
	 * @filelist_internal:
	 *
	 * List of open DRM files for in-kernel clients. Protected by @filelist_mutex.
	 */
	struct list_head filelist_internal;

	/**
	 * @clientlist_mutex:
	 *
	 * Protects @clientlist access.
	 */
	struct mutex clientlist_mutex;

	/**
	 * @clientlist:
	 *
	 * List of in-kernel clients. Protected by @clientlist_mutex.
	 */
	struct list_head clientlist;

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

	/**
	 * @irq_enabled:
	 *
	 * Indicates that interrupt handling is enabled, specifically vblank
	 * handling. Drivers which don't use drm_irq_install() need to set this
	 * to true manually.
	 */
	bool irq_enabled;
	int irq;

	/**
	 * @vblank_disable_immediate:
	 *
	 * If true, vblank interrupt will be disabled immediately when the
	 * refcount drops to zero, as opposed to via the vblank disable
	 * timer.
	 *
	 * This can be set to true it the hardware has a working vblank counter
	 * with high-precision timestamping (otherwise there are races) and the
	 * driver uses drm_crtc_vblank_on() and drm_crtc_vblank_off()
	 * appropriately. See also @max_vblank_count and
	 * &drm_crtc_funcs.get_vblank_counter.
	 */
	bool vblank_disable_immediate;

	/**
	 * @vblank:
	 *
	 * Array of vblank tracking structures, one per &struct drm_crtc. For
	 * historical reasons (vblank support predates kernel modesetting) this
	 * is free-standing and not part of &struct drm_crtc itself. It must be
	 * initialized explicitly by calling drm_vblank_init().
	 */
	struct drm_vblank_crtc *vblank;

	spinlock_t vblank_time_lock;    /**< Protects vblank count and time updates during vblank enable/disable */
	spinlock_t vbl_lock;

	/**
	 * @max_vblank_count:
	 *
	 * Maximum value of the vblank registers. This value +1 will result in a
	 * wrap-around of the vblank register. It is used by the vblank core to
	 * handle wrap-arounds.
	 *
	 * If set to zero the vblank core will try to guess the elapsed vblanks
	 * between times when the vblank interrupt is disabled through
	 * high-precision timestamps. That approach is suffering from small
	 * races and imprecision over longer time periods, hence exposing a
	 * hardware vblank counter is always recommended.
	 *
	 * If non-zeor, &drm_crtc_funcs.get_vblank_counter must be set.
	 */
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

	/**
	 * @fb_helper:
	 *
	 * Pointer to the fbdev emulation structure.
	 * Set by drm_fb_helper_init() and cleared by drm_fb_helper_fini().
	 */
	struct drm_fb_helper *fb_helper;
};

#endif
