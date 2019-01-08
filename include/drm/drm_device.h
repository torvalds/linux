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
 * struct drm_device - DRM device structure
 *
 * This structure represent a complete card that
 * may contain multiple heads.
 */
struct drm_device {
	/**
	 * @legacy_dev_list:
	 *
	 * List of devices per driver for stealth attach cleanup
	 */
	struct list_head legacy_dev_list;

	/** @if_version: Highest interface version set */
	int if_version;

	/** @ref: Object ref-count */
	struct kref ref;

	/** @dev: Device structure of bus-device */
	struct device *dev;

	/** @driver: DRM driver managing the device */
	struct drm_driver *driver;

	/** @dev_private: DRM driver private data */
	void *dev_private;

	/** @primary: Primary node */
	struct drm_minor *primary;

	/** @render: Render node */
	struct drm_minor *render;

	bool registered;

	/**
	 * @master:
	 *
	 * Currently active master for this device.
	 * Protected by &master_mutex
	 */
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

	/** @anon_inode: inode for private address-space */
	struct inode *anon_inode;

	/** @unique: Unique name of the device */
	char *unique;

	/**
	 * @struct_mutex:
	 *
	 * Lock for others (not &drm_minor.master and &drm_file.is_master)
	 */
	struct mutex struct_mutex;

	/**
	 * @master_mutex:
	 *
	 * Lock for &drm_minor.master and &drm_file.is_master
	 */
	struct mutex master_mutex;

	/**
	 * @open_count:
	 *
	 * Usage counter for outstanding files open,
	 * protected by drm_global_mutex
	 */
	int open_count;

	/** @buf_lock: Lock for &buf_use and a few other things. */
	spinlock_t buf_lock;

	/** @buf_use: Usage counter for buffers in use -- cannot alloc */
	int buf_use;

	/** @buf_alloc: Buffer allocation in progress */
	atomic_t buf_alloc;

	struct mutex filelist_mutex;
	struct list_head filelist;

	/**
	 * @filelist_internal:
	 *
	 * List of open DRM files for in-kernel clients.
	 * Protected by &filelist_mutex.
	 */
	struct list_head filelist_internal;

	/**
	 * @clientlist_mutex:
	 *
	 * Protects &clientlist access.
	 */
	struct mutex clientlist_mutex;

	/**
	 * @clientlist:
	 *
	 * List of in-kernel clients. Protected by &clientlist_mutex.
	 */
	struct list_head clientlist;

	/** @maplist: Memory management - linked list of regions */
	struct list_head maplist;

	/** @map_hash: Memory management - user token hash table for maps */
	struct drm_open_hash map_hash;

	/**
	 * @ctxlist:
	 * Context handle management - linked list of context handles
	 */
	struct list_head ctxlist;

	/**
	 * @ctxlist_mutex:
	 *
	 * Context handle management - mutex for &ctxlist
	 */
	struct mutex ctxlist_mutex;

	/**
	 * @ctx_idr:
	 * Context handle management
	 */
	struct idr ctx_idr;

	/**
	 * @vmalist:
	 * Context handle management - list of vmas (for debugging)
	 */
	struct list_head vmalist;

	/** @dma: Optional pointer for DMA support */
	struct drm_device_dma *dma;

	/** @context_flag: Context swapping flag */
	__volatile__ long context_flag;

	/** @last_context: Last current context */
	int last_context;

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

	/**
	 * @vblank_time_lock:
	 *
	 *  Protects vblank count and time updates during vblank enable/disable
	 */
	spinlock_t vblank_time_lock;
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

	/** @max_vblank_count: Size of vblank counter register */
	u32 max_vblank_count;

	/** @vblank_event_list: List of vblank events */
	struct list_head vblank_event_list;
	spinlock_t event_lock;

	/** @agp: AGP data */
	struct drm_agp_head *agp;

	/** @pdev: PCI device structure */
	struct pci_dev *pdev;

#ifdef __alpha__
	struct pci_controller *hose;
#endif

	/** @sg: Scatter gather memory */
	struct drm_sg_mem *sg;

	/** @num_crtcs: Number of CRTCs on this device */
	unsigned int num_crtcs;

	struct {
		int context;
		struct drm_hw_lock *lock;
	} sigdata;

	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;

	/** @mode_config: Current mode config */
	struct drm_mode_config mode_config;

	/** @object_name_lock: GEM information */
	struct mutex object_name_lock;

	/** @object_name_idr: GEM information */
	struct idr object_name_idr;

	/** @vma_offset_manager: GEM information */
	struct drm_vma_offset_manager *vma_offset_manager;

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
