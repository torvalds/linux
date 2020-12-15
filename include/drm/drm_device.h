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
struct drm_vram_mm;
struct drm_fb_helper;

struct inode;

struct pci_dev;
struct pci_controller;


/**
 * enum drm_switch_power - power state of drm device
 */

enum switch_power_state {
	/** @DRM_SWITCH_POWER_ON: Power state is ON */
	DRM_SWITCH_POWER_ON = 0,

	/** @DRM_SWITCH_POWER_OFF: Power state is OFF */
	DRM_SWITCH_POWER_OFF = 1,

	/** @DRM_SWITCH_POWER_CHANGING: Power state is changing */
	DRM_SWITCH_POWER_CHANGING = 2,

	/** @DRM_SWITCH_POWER_DYNAMIC_OFF: Suspended */
	DRM_SWITCH_POWER_DYNAMIC_OFF = 3,
};

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

	/**
	 * @managed:
	 *
	 * Managed resources linked to the lifetime of this &drm_device as
	 * tracked by @ref.
	 */
	struct {
		/** @managed.resources: managed resources list */
		struct list_head resources;
		/** @managed.final_kfree: pointer for final kfree() call */
		void *final_kfree;
		/** @managed.lock: protects @managed.resources */
		spinlock_t lock;
	} managed;

	/** @driver: DRM driver managing the device */
	struct drm_driver *driver;

	/**
	 * @dev_private:
	 *
	 * DRM driver private data. This is deprecated and should be left set to
	 * NULL.
	 *
	 * Instead of using this pointer it is recommended that drivers use
	 * drm_dev_init() and embed struct &drm_device in their larger
	 * per-device structure.
	 */
	void *dev_private;

	/** @primary: Primary node */
	struct drm_minor *primary;

	/** @render: Render node */
	struct drm_minor *render;

	/**
	 * @registered:
	 *
	 * Internally used by drm_dev_register() and drm_connector_register().
	 */
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
	 *
	 * WARNING:
	 * Only drivers annotated with DRIVER_LEGACY should be using this.
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
	atomic_t open_count;

	/** @filelist_mutex: Protects @filelist. */
	struct mutex filelist_mutex;
	/**
	 * @filelist:
	 *
	 * List of userspace clients, linked through &drm_file.lhead.
	 */
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

	/**
	 * @irq_enabled:
	 *
	 * Indicates that interrupt handling is enabled, specifically vblank
	 * handling. Drivers which don't use drm_irq_install() need to set this
	 * to true manually.
	 */
	bool irq_enabled;

	/**
	 * @irq: Used by the drm_irq_install() and drm_irq_unistall() helpers.
	 */
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
	/**
	 * @vbl_lock: Top-level vblank references lock, wraps the low-level
	 * @vblank_time_lock.
	 */
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
	 * This is the statically configured device wide maximum. The driver
	 * can instead choose to use a runtime configurable per-crtc value
	 * &drm_vblank_crtc.max_vblank_count, in which case @max_vblank_count
	 * must be left at zero. See drm_crtc_set_max_vblank_count() on how
	 * to use the per-crtc value.
	 *
	 * If non-zero, &drm_crtc_funcs.get_vblank_counter must be set.
	 */
	u32 max_vblank_count;

	/** @vblank_event_list: List of vblank events */
	struct list_head vblank_event_list;

	/**
	 * @event_lock:
	 *
	 * Protects @vblank_event_list and event delivery in
	 * general. See drm_send_event() and drm_send_event_locked().
	 */
	spinlock_t event_lock;

	/** @agp: AGP data */
	struct drm_agp_head *agp;

	/** @pdev: PCI device structure */
	struct pci_dev *pdev;

#ifdef __alpha__
	/** @hose: PCI hose, only used on ALPHA platforms. */
	struct pci_controller *hose;
#endif
	/** @num_crtcs: Number of CRTCs on this device */
	unsigned int num_crtcs;

	/** @mode_config: Current mode config */
	struct drm_mode_config mode_config;

	/** @object_name_lock: GEM information */
	struct mutex object_name_lock;

	/** @object_name_idr: GEM information */
	struct idr object_name_idr;

	/** @vma_offset_manager: GEM information */
	struct drm_vma_offset_manager *vma_offset_manager;

	/** @vram_mm: VRAM MM memory manager */
	struct drm_vram_mm *vram_mm;

	/**
	 * @switch_power_state:
	 *
	 * Power state of the client.
	 * Used by drivers supporting the switcheroo driver.
	 * The state is maintained in the
	 * &vga_switcheroo_client_ops.set_gpu_state callback
	 */
	enum switch_power_state switch_power_state;

	/**
	 * @fb_helper:
	 *
	 * Pointer to the fbdev emulation structure.
	 * Set by drm_fb_helper_init() and cleared by drm_fb_helper_fini().
	 */
	struct drm_fb_helper *fb_helper;

	/* Everything below here is for legacy driver, never use! */
	/* private: */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
	/* Context handle management - linked list of context handles */
	struct list_head ctxlist;

	/* Context handle management - mutex for &ctxlist */
	struct mutex ctxlist_mutex;

	/* Context handle management */
	struct idr ctx_idr;

	/* Memory management - linked list of regions */
	struct list_head maplist;

	/* Memory management - user token hash table for maps */
	struct drm_open_hash map_hash;

	/* Context handle management - list of vmas (for debugging) */
	struct list_head vmalist;

	/* Optional pointer for DMA support */
	struct drm_device_dma *dma;

	/* Context swapping flag */
	__volatile__ long context_flag;

	/* Last current context */
	int last_context;

	/* Lock for &buf_use and a few other things. */
	spinlock_t buf_lock;

	/* Usage counter for buffers in use -- cannot alloc */
	int buf_use;

	/* Buffer allocation in progress */
	atomic_t buf_alloc;

	struct {
		int context;
		struct drm_hw_lock *lock;
	} sigdata;

	struct drm_local_map *agp_buffer_map;
	unsigned int agp_buffer_token;

	/* Scatter gather memory */
	struct drm_sg_mem *sg;
#endif
};

#endif
