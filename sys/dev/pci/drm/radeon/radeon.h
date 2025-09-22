/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#ifndef __RADEON_H__
#define __RADEON_H__

/* TODO: Here are things that needs to be done :
 *	- surface allocator & initializer : (bit like scratch reg) should
 *	  initialize HDP_ stuff on RS600, R600, R700 hw, well anythings
 *	  related to surface
 *	- WB : write back stuff (do it bit like scratch reg things)
 *	- Vblank : look at Jesse's rework and what we should do
 *	- r600/r700: gart & cp
 *	- cs : clean cs ioctl use bitmap & things like that.
 *	- power management stuff
 *	- Barrier in gart code
 *	- Unmappabled vram ?
 *	- TESTING, TESTING, TESTING
 */

/* Initialization path:
 *  We expect that acceleration initialization might fail for various
 *  reasons even thought we work hard to make it works on most
 *  configurations. In order to still have a working userspace in such
 *  situation the init path must succeed up to the memory controller
 *  initialization point. Failure before this point are considered as
 *  fatal error. Here is the init callchain :
 *      radeon_device_init  perform common structure, mutex initialization
 *      asic_init           setup the GPU memory layout and perform all
 *                          one time initialization (failure in this
 *                          function are considered fatal)
 *      asic_startup        setup the GPU acceleration, in order to
 *                          follow guideline the first thing this
 *                          function should do is setting the GPU
 *                          memory controller (only MC setup failure
 *                          are considered as fatal)
 */

#include <linux/agp_backend.h>
#include <linux/atomic.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/interval_tree.h>
#include <linux/hashtable.h>
#include <linux/dma-fence.h>

#ifdef CONFIG_MMU_NOTIFIER
#include <linux/mmu_notifier.h>
#endif

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_execbuf_util.h>

#include <drm/drm_gem.h>
#include <drm/drm_audio_component.h>
#include <drm/drm_suballoc.h>
#include <drm/drm_drv.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <dev/pci/pcivar.h>

#ifdef __sparc64__
#include <machine/fbvar.h>
#endif

#include "radeon_family.h"
#include "radeon_mode.h"
#include "radeon_reg.h"

/*
 * Modules parameters.
 */
extern int radeon_no_wb;
extern int radeon_modeset;
extern int radeon_dynclks;
extern int radeon_r4xx_atom;
extern int radeon_agpmode;
extern int radeon_vram_limit;
extern int radeon_gart_size;
extern int radeon_benchmarking;
extern int radeon_testing;
extern int radeon_connector_table;
extern int radeon_tv;
extern int radeon_audio;
extern int radeon_disp_priority;
extern int radeon_hw_i2c;
extern int radeon_pcie_gen2;
extern int radeon_msi;
extern int radeon_lockup_timeout;
extern int radeon_fastfb;
extern int radeon_dpm;
extern int radeon_aspm;
extern int radeon_runtime_pm;
extern int radeon_hard_reset;
extern int radeon_vm_size;
extern int radeon_vm_block_size;
extern int radeon_deep_color;
extern int radeon_use_pflipirq;
extern int radeon_bapm;
extern int radeon_backlight;
extern int radeon_auxch;
extern int radeon_uvd;
extern int radeon_vce;
extern int radeon_si_support;
extern int radeon_cik_support;

/*
 * Copy from radeon_drv.h so we don't have to include both and have conflicting
 * symbol;
 */
#define RADEON_MAX_USEC_TIMEOUT			100000	/* 100 ms */
#define RADEON_FENCE_JIFFIES_TIMEOUT		(HZ / 2)
#define RADEON_USEC_IB_TEST_TIMEOUT		1000000 /* 1s */
/* RADEON_IB_POOL_SIZE must be a power of 2 */
#define RADEON_IB_POOL_SIZE			16
#define RADEON_DEBUGFS_MAX_COMPONENTS		32
#define RADEON_BIOS_NUM_SCRATCH			8

/* internal ring indices */
/* r1xx+ has gfx CP ring */
#define RADEON_RING_TYPE_GFX_INDEX		0

/* cayman has 2 compute CP rings */
#define CAYMAN_RING_TYPE_CP1_INDEX		1
#define CAYMAN_RING_TYPE_CP2_INDEX		2

/* R600+ has an async dma ring */
#define R600_RING_TYPE_DMA_INDEX		3
/* cayman add a second async dma ring */
#define CAYMAN_RING_TYPE_DMA1_INDEX		4

/* R600+ */
#define R600_RING_TYPE_UVD_INDEX		5

/* TN+ */
#define TN_RING_TYPE_VCE1_INDEX			6
#define TN_RING_TYPE_VCE2_INDEX			7

/* max number of rings */
#define RADEON_NUM_RINGS			8

/* number of hw syncs before falling back on blocking */
#define RADEON_NUM_SYNCS			4

/* hardcode those limit for now */
#define RADEON_VA_IB_OFFSET			(1 << 20)
#define RADEON_VA_RESERVED_SIZE			(8 << 20)
#define RADEON_IB_VM_MAX_SIZE			(64 << 10)

/* hard reset data */
#define RADEON_ASIC_RESET_DATA                  0x39d5e86b

/* reset flags */
#define RADEON_RESET_GFX			(1 << 0)
#define RADEON_RESET_COMPUTE			(1 << 1)
#define RADEON_RESET_DMA			(1 << 2)
#define RADEON_RESET_CP				(1 << 3)
#define RADEON_RESET_GRBM			(1 << 4)
#define RADEON_RESET_DMA1			(1 << 5)
#define RADEON_RESET_RLC			(1 << 6)
#define RADEON_RESET_SEM			(1 << 7)
#define RADEON_RESET_IH				(1 << 8)
#define RADEON_RESET_VMC			(1 << 9)
#define RADEON_RESET_MC				(1 << 10)
#define RADEON_RESET_DISPLAY			(1 << 11)

/* CG block flags */
#define RADEON_CG_BLOCK_GFX			(1 << 0)
#define RADEON_CG_BLOCK_MC			(1 << 1)
#define RADEON_CG_BLOCK_SDMA			(1 << 2)
#define RADEON_CG_BLOCK_UVD			(1 << 3)
#define RADEON_CG_BLOCK_VCE			(1 << 4)
#define RADEON_CG_BLOCK_HDP			(1 << 5)
#define RADEON_CG_BLOCK_BIF			(1 << 6)

/* CG flags */
#define RADEON_CG_SUPPORT_GFX_MGCG		(1 << 0)
#define RADEON_CG_SUPPORT_GFX_MGLS		(1 << 1)
#define RADEON_CG_SUPPORT_GFX_CGCG		(1 << 2)
#define RADEON_CG_SUPPORT_GFX_CGLS		(1 << 3)
#define RADEON_CG_SUPPORT_GFX_CGTS		(1 << 4)
#define RADEON_CG_SUPPORT_GFX_CGTS_LS		(1 << 5)
#define RADEON_CG_SUPPORT_GFX_CP_LS		(1 << 6)
#define RADEON_CG_SUPPORT_GFX_RLC_LS		(1 << 7)
#define RADEON_CG_SUPPORT_MC_LS			(1 << 8)
#define RADEON_CG_SUPPORT_MC_MGCG		(1 << 9)
#define RADEON_CG_SUPPORT_SDMA_LS		(1 << 10)
#define RADEON_CG_SUPPORT_SDMA_MGCG		(1 << 11)
#define RADEON_CG_SUPPORT_BIF_LS		(1 << 12)
#define RADEON_CG_SUPPORT_UVD_MGCG		(1 << 13)
#define RADEON_CG_SUPPORT_VCE_MGCG		(1 << 14)
#define RADEON_CG_SUPPORT_HDP_LS		(1 << 15)
#define RADEON_CG_SUPPORT_HDP_MGCG		(1 << 16)

/* PG flags */
#define RADEON_PG_SUPPORT_GFX_PG		(1 << 0)
#define RADEON_PG_SUPPORT_GFX_SMG		(1 << 1)
#define RADEON_PG_SUPPORT_GFX_DMG		(1 << 2)
#define RADEON_PG_SUPPORT_UVD			(1 << 3)
#define RADEON_PG_SUPPORT_VCE			(1 << 4)
#define RADEON_PG_SUPPORT_CP			(1 << 5)
#define RADEON_PG_SUPPORT_GDS			(1 << 6)
#define RADEON_PG_SUPPORT_RLC_SMU_HS		(1 << 7)
#define RADEON_PG_SUPPORT_SDMA			(1 << 8)
#define RADEON_PG_SUPPORT_ACP			(1 << 9)
#define RADEON_PG_SUPPORT_SAMU			(1 << 10)

/* max cursor sizes (in pixels) */
#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

#define CIK_CURSOR_WIDTH 128
#define CIK_CURSOR_HEIGHT 128

/*
 * Errata workarounds.
 */
enum radeon_pll_errata {
	CHIP_ERRATA_R300_CG             = 0x00000001,
	CHIP_ERRATA_PLL_DUMMYREADS      = 0x00000002,
	CHIP_ERRATA_PLL_DELAY           = 0x00000004
};


struct radeon_device;


/*
 * BIOS.
 */
bool radeon_get_bios(struct radeon_device *rdev);

/*
 * Dummy page
 */
struct radeon_dummy_page {
	uint64_t	entry;
	struct drm_dmamem	*dmah;
	dma_addr_t	addr;
};
int radeon_dummy_page_init(struct radeon_device *rdev);
void radeon_dummy_page_fini(struct radeon_device *rdev);


/*
 * Clocks
 */
struct radeon_clock {
	struct radeon_pll p1pll;
	struct radeon_pll p2pll;
	struct radeon_pll dcpll;
	struct radeon_pll spll;
	struct radeon_pll mpll;
	/* 10 Khz units */
	uint32_t default_mclk;
	uint32_t default_sclk;
	uint32_t default_dispclk;
	uint32_t current_dispclk;
	uint32_t dp_extclk;
	uint32_t max_pixel_clock;
	uint32_t vco_freq;
};

/*
 * Power management
 */
int radeon_pm_init(struct radeon_device *rdev);
int radeon_pm_late_init(struct radeon_device *rdev);
void radeon_pm_fini(struct radeon_device *rdev);
void radeon_pm_compute_clocks(struct radeon_device *rdev);
void radeon_pm_suspend(struct radeon_device *rdev);
void radeon_pm_resume(struct radeon_device *rdev);
void radeon_combios_get_power_modes(struct radeon_device *rdev);
void radeon_atombios_get_power_modes(struct radeon_device *rdev);
int radeon_atom_get_clock_dividers(struct radeon_device *rdev,
				   u8 clock_type,
				   u32 clock,
				   bool strobe_mode,
				   struct atom_clock_dividers *dividers);
int radeon_atom_get_memory_pll_dividers(struct radeon_device *rdev,
					u32 clock,
					bool strobe_mode,
					struct atom_mpll_param *mpll_param);
void radeon_atom_set_voltage(struct radeon_device *rdev, u16 voltage_level, u8 voltage_type);
int radeon_atom_get_voltage_gpio_settings(struct radeon_device *rdev,
					  u16 voltage_level, u8 voltage_type,
					  u32 *gpio_value, u32 *gpio_mask);
void radeon_atom_set_engine_dram_timings(struct radeon_device *rdev,
					 u32 eng_clock, u32 mem_clock);
int radeon_atom_get_voltage_step(struct radeon_device *rdev,
				 u8 voltage_type, u16 *voltage_step);
int radeon_atom_get_max_vddc(struct radeon_device *rdev, u8 voltage_type,
			     u16 voltage_id, u16 *voltage);
int radeon_atom_get_leakage_vddc_based_on_leakage_idx(struct radeon_device *rdev,
						      u16 *voltage,
						      u16 leakage_idx);
int radeon_atom_get_leakage_id_from_vbios(struct radeon_device *rdev,
					  u16 *leakage_id);
int radeon_atom_get_leakage_vddc_based_on_leakage_params(struct radeon_device *rdev,
							 u16 *vddc, u16 *vddci,
							 u16 virtual_voltage_id,
							 u16 vbios_voltage_id);
int radeon_atom_get_voltage_evv(struct radeon_device *rdev,
				u16 virtual_voltage_id,
				u16 *voltage);
int radeon_atom_round_to_true_voltage(struct radeon_device *rdev,
				      u8 voltage_type,
				      u16 nominal_voltage,
				      u16 *true_voltage);
int radeon_atom_get_min_voltage(struct radeon_device *rdev,
				u8 voltage_type, u16 *min_voltage);
int radeon_atom_get_max_voltage(struct radeon_device *rdev,
				u8 voltage_type, u16 *max_voltage);
int radeon_atom_get_voltage_table(struct radeon_device *rdev,
				  u8 voltage_type, u8 voltage_mode,
				  struct atom_voltage_table *voltage_table);
bool radeon_atom_is_voltage_gpio(struct radeon_device *rdev,
				 u8 voltage_type, u8 voltage_mode);
int radeon_atom_get_svi2_info(struct radeon_device *rdev,
			      u8 voltage_type,
			      u8 *svd_gpio_id, u8 *svc_gpio_id);
void radeon_atom_update_memory_dll(struct radeon_device *rdev,
				   u32 mem_clock);
void radeon_atom_set_ac_timing(struct radeon_device *rdev,
			       u32 mem_clock);
int radeon_atom_init_mc_reg_table(struct radeon_device *rdev,
				  u8 module_index,
				  struct atom_mc_reg_table *reg_table);
int radeon_atom_get_memory_info(struct radeon_device *rdev,
				u8 module_index, struct atom_memory_info *mem_info);
int radeon_atom_get_mclk_range_table(struct radeon_device *rdev,
				     bool gddr5, u8 module_index,
				     struct atom_memory_clock_range_table *mclk_range_table);
int radeon_atom_get_max_vddc(struct radeon_device *rdev, u8 voltage_type,
			     u16 voltage_id, u16 *voltage);
void rs690_pm_info(struct radeon_device *rdev);
extern void evergreen_tiling_fields(unsigned tiling_flags, unsigned *bankw,
				    unsigned *bankh, unsigned *mtaspect,
				    unsigned *tile_split);

/*
 * Fences.
 */
struct radeon_fence_driver {
	struct radeon_device		*rdev;
	uint32_t			scratch_reg;
	uint64_t			gpu_addr;
	volatile uint32_t		*cpu_addr;
	/* sync_seq is protected by ring emission lock */
	uint64_t			sync_seq[RADEON_NUM_RINGS];
	atomic64_t			last_seq;
	bool				initialized, delayed_irq;
	struct delayed_work		lockup_work;
};

struct radeon_fence {
	struct dma_fence		base;

	struct radeon_device	*rdev;
	uint64_t		seq;
	/* RB, DMA, etc. */
	unsigned		ring;
	bool			is_vm_update;

	wait_queue_entry_t		fence_wake;
};

int radeon_fence_driver_start_ring(struct radeon_device *rdev, int ring);
void radeon_fence_driver_init(struct radeon_device *rdev);
void radeon_fence_driver_fini(struct radeon_device *rdev);
void radeon_fence_driver_force_completion(struct radeon_device *rdev, int ring);
int radeon_fence_emit(struct radeon_device *rdev, struct radeon_fence **fence, int ring);
void radeon_fence_process(struct radeon_device *rdev, int ring);
bool radeon_fence_signaled(struct radeon_fence *fence);
long radeon_fence_wait_timeout(struct radeon_fence *fence, bool interruptible, long timeout);
int radeon_fence_wait(struct radeon_fence *fence, bool interruptible);
int radeon_fence_wait_next(struct radeon_device *rdev, int ring);
int radeon_fence_wait_empty(struct radeon_device *rdev, int ring);
int radeon_fence_wait_any(struct radeon_device *rdev,
			  struct radeon_fence **fences,
			  bool intr);
struct radeon_fence *radeon_fence_ref(struct radeon_fence *fence);
void radeon_fence_unref(struct radeon_fence **fence);
unsigned radeon_fence_count_emitted(struct radeon_device *rdev, int ring);
bool radeon_fence_need_sync(struct radeon_fence *fence, int ring);
void radeon_fence_note_sync(struct radeon_fence *fence, int ring);
static inline struct radeon_fence *radeon_fence_later(struct radeon_fence *a,
						      struct radeon_fence *b)
{
	if (!a) {
		return b;
	}

	if (!b) {
		return a;
	}

	BUG_ON(a->ring != b->ring);

	if (a->seq > b->seq) {
		return a;
	} else {
		return b;
	}
}

static inline bool radeon_fence_is_earlier(struct radeon_fence *a,
					   struct radeon_fence *b)
{
	if (!a) {
		return false;
	}

	if (!b) {
		return true;
	}

	BUG_ON(a->ring != b->ring);

	return a->seq < b->seq;
}

/*
 * Tiling registers
 */
struct radeon_surface_reg {
	struct radeon_bo *bo;
};

#define RADEON_GEM_MAX_SURFACES 8

/*
 * TTM.
 */
struct radeon_mman {
	struct ttm_device		bdev;
	bool				initialized;
};

struct radeon_bo_list {
	struct radeon_bo		*robj;
	struct ttm_validate_buffer	tv;
	uint64_t			gpu_offset;
	unsigned			preferred_domains;
	unsigned			allowed_domains;
	uint32_t			tiling_flags;
};

/* bo virtual address in a specific vm */
struct radeon_bo_va {
	/* protected by bo being reserved */
	struct list_head		bo_list;
	uint32_t			flags;
	struct radeon_fence		*last_pt_update;
	unsigned			ref_count;

	/* protected by vm mutex */
	struct interval_tree_node	it;
	struct list_head		vm_status;

	/* constant after initialization */
	struct radeon_vm		*vm;
	struct radeon_bo		*bo;
};

struct radeon_bo {
	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	u32				initial_domain;
	struct ttm_place		placements[4];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	u32				flags;
	void				*kptr;
	u32				tiling_flags;
	u32				pitch;
	int				surface_reg;
	unsigned			prime_shared_count;
	/* list of all virtual address to which this bo
	 * is associated to
	 */
	struct list_head		va;
	/* Constant after initialization */
	struct radeon_device		*rdev;

	pid_t				pid;

#ifdef CONFIG_MMU_NOTIFIER
	struct mmu_interval_notifier	notifier;
#endif
};
#define gem_to_radeon_bo(gobj) container_of((gobj), struct radeon_bo, tbo.base)

struct radeon_sa_manager {
	struct drm_suballoc_manager	base;
	struct radeon_bo		*bo;
	uint64_t			gpu_addr;
	void				*cpu_ptr;
	u32 domain;
};

/*
 * GEM objects.
 */
struct radeon_gem {
	struct rwlock		mutex;
	struct list_head	objects;
};

extern const struct drm_gem_object_funcs radeon_gem_object_funcs;

int radeon_align_pitch(struct radeon_device *rdev, int width, int cpp, bool tiled);

int radeon_gem_init(struct radeon_device *rdev);
void radeon_gem_fini(struct radeon_device *rdev);
int radeon_gem_object_create(struct radeon_device *rdev, unsigned long size,
				int alignment, int initial_domain,
				u32 flags, bool kernel,
				struct drm_gem_object **obj);

int radeon_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args);
int radeon_mode_dumb_mmap(struct drm_file *filp,
			  struct drm_device *dev,
			  uint32_t handle, uint64_t *offset_p);

/*
 * Semaphores.
 */
struct radeon_semaphore {
	struct drm_suballoc	*sa_bo;
	signed			waiters;
	uint64_t		gpu_addr;
};

int radeon_semaphore_create(struct radeon_device *rdev,
			    struct radeon_semaphore **semaphore);
bool radeon_semaphore_emit_signal(struct radeon_device *rdev, int ring,
				  struct radeon_semaphore *semaphore);
bool radeon_semaphore_emit_wait(struct radeon_device *rdev, int ring,
				struct radeon_semaphore *semaphore);
void radeon_semaphore_free(struct radeon_device *rdev,
			   struct radeon_semaphore **semaphore,
			   struct radeon_fence *fence);

/*
 * Synchronization
 */
struct radeon_sync {
	struct radeon_semaphore *semaphores[RADEON_NUM_SYNCS];
	struct radeon_fence	*sync_to[RADEON_NUM_RINGS];
	struct radeon_fence	*last_vm_update;
};

void radeon_sync_create(struct radeon_sync *sync);
void radeon_sync_fence(struct radeon_sync *sync,
		       struct radeon_fence *fence);
int radeon_sync_resv(struct radeon_device *rdev,
		     struct radeon_sync *sync,
		     struct dma_resv *resv,
		     bool shared);
int radeon_sync_rings(struct radeon_device *rdev,
		      struct radeon_sync *sync,
		      int waiting_ring);
void radeon_sync_free(struct radeon_device *rdev, struct radeon_sync *sync,
		      struct radeon_fence *fence);

/*
 * GART structures, functions & helpers
 */
struct radeon_mc;

#define RADEON_GPU_PAGE_SIZE 4096
#define RADEON_GPU_PAGE_MASK (RADEON_GPU_PAGE_SIZE - 1)
#define RADEON_GPU_PAGE_SHIFT 12
#define RADEON_GPU_PAGE_ALIGN(a) (((a) + RADEON_GPU_PAGE_MASK) & ~RADEON_GPU_PAGE_MASK)

#define RADEON_GART_PAGE_DUMMY  0
#define RADEON_GART_PAGE_VALID	(1 << 0)
#define RADEON_GART_PAGE_READ	(1 << 1)
#define RADEON_GART_PAGE_WRITE	(1 << 2)
#define RADEON_GART_PAGE_SNOOP	(1 << 3)

struct radeon_gart {
	dma_addr_t			table_addr;
	struct drm_dmamem		*dmah;
	struct radeon_bo		*robj;
	void				*ptr;
	unsigned			num_gpu_pages;
	unsigned			num_cpu_pages;
	unsigned			table_size;
	struct vm_page			**pages;
	uint64_t			*pages_entry;
	bool				ready;
};

int radeon_gart_table_ram_alloc(struct radeon_device *rdev);
void radeon_gart_table_ram_free(struct radeon_device *rdev);
int radeon_gart_table_vram_alloc(struct radeon_device *rdev);
void radeon_gart_table_vram_free(struct radeon_device *rdev);
int radeon_gart_table_vram_pin(struct radeon_device *rdev);
void radeon_gart_table_vram_unpin(struct radeon_device *rdev);
int radeon_gart_init(struct radeon_device *rdev);
void radeon_gart_fini(struct radeon_device *rdev);
void radeon_gart_unbind(struct radeon_device *rdev, unsigned offset,
			int pages);
int radeon_gart_bind(struct radeon_device *rdev, unsigned offset,
		     int pages, struct vm_page **pagelist,
		     dma_addr_t *dma_addr, uint32_t flags);


/*
 * GPU MC structures, functions & helpers
 */
struct radeon_mc {
	resource_size_t		aper_size;
	resource_size_t		aper_base;
	resource_size_t		agp_base;
	/* for some chips with <= 32MB we need to lie
	 * about vram size near mc fb location */
	u64			mc_vram_size;
	u64			visible_vram_size;
	u64			gtt_size;
	u64			gtt_start;
	u64			gtt_end;
	u64			vram_start;
	u64			vram_end;
	unsigned		vram_width;
	u64			real_vram_size;
	int			vram_mtrr;
	bool			vram_is_ddr;
	bool			igp_sideport_enabled;
	u64                     gtt_base_align;
	u64                     mc_mask;
};

bool radeon_combios_sideport_present(struct radeon_device *rdev);
bool radeon_atombios_sideport_present(struct radeon_device *rdev);

/*
 * GPU scratch registers structures, functions & helpers
 */
struct radeon_scratch {
	unsigned		num_reg;
	uint32_t                reg_base;
	bool			free[32];
	uint32_t		reg[32];
};

int radeon_scratch_get(struct radeon_device *rdev, uint32_t *reg);
void radeon_scratch_free(struct radeon_device *rdev, uint32_t reg);

/*
 * GPU doorbell structures, functions & helpers
 */
#define RADEON_MAX_DOORBELLS 1024	/* Reserve at most 1024 doorbell slots for radeon-owned rings. */

struct radeon_doorbell {
	/* doorbell mmio */
	resource_size_t		base;
	resource_size_t		size;
	u32 __iomem		*ptr;
	bus_space_handle_t	bsh;
	u32			num_doorbells;	/* Number of doorbells actually reserved for radeon. */
	DECLARE_BITMAP(used, RADEON_MAX_DOORBELLS);
};

int radeon_doorbell_get(struct radeon_device *rdev, u32 *page);
void radeon_doorbell_free(struct radeon_device *rdev, u32 doorbell);

/*
 * IRQS.
 */

struct radeon_flip_work {
	struct work_struct		flip_work;
	struct work_struct		unpin_work;
	struct radeon_device		*rdev;
	int				crtc_id;
	u32				target_vblank;
	uint64_t			base;
	struct drm_pending_vblank_event *event;
	struct radeon_bo		*old_rbo;
	struct dma_fence		*fence;
	bool				async;
};

struct r500_irq_stat_regs {
	u32 disp_int;
	u32 hdmi0_status;
};

struct r600_irq_stat_regs {
	u32 disp_int;
	u32 disp_int_cont;
	u32 disp_int_cont2;
	u32 d1grph_int;
	u32 d2grph_int;
	u32 hdmi0_status;
	u32 hdmi1_status;
};

struct evergreen_irq_stat_regs {
	u32 disp_int[6];
	u32 grph_int[6];
	u32 afmt_status[6];
};

struct cik_irq_stat_regs {
	u32 disp_int;
	u32 disp_int_cont;
	u32 disp_int_cont2;
	u32 disp_int_cont3;
	u32 disp_int_cont4;
	u32 disp_int_cont5;
	u32 disp_int_cont6;
	u32 d1grph_int;
	u32 d2grph_int;
	u32 d3grph_int;
	u32 d4grph_int;
	u32 d5grph_int;
	u32 d6grph_int;
};

union radeon_irq_stat_regs {
	struct r500_irq_stat_regs r500;
	struct r600_irq_stat_regs r600;
	struct evergreen_irq_stat_regs evergreen;
	struct cik_irq_stat_regs cik;
};

struct radeon_irq {
	bool				installed;
	spinlock_t			lock;
	atomic_t			ring_int[RADEON_NUM_RINGS];
	bool				crtc_vblank_int[RADEON_MAX_CRTCS];
	atomic_t			pflip[RADEON_MAX_CRTCS];
	wait_queue_head_t		vblank_queue;
	bool				hpd[RADEON_MAX_HPD_PINS];
	bool				afmt[RADEON_MAX_AFMT_BLOCKS];
	union radeon_irq_stat_regs	stat_regs;
	bool				dpm_thermal;
};

int radeon_irq_kms_init(struct radeon_device *rdev);
void radeon_irq_kms_fini(struct radeon_device *rdev);
void radeon_irq_kms_sw_irq_get(struct radeon_device *rdev, int ring);
bool radeon_irq_kms_sw_irq_get_delayed(struct radeon_device *rdev, int ring);
void radeon_irq_kms_sw_irq_put(struct radeon_device *rdev, int ring);
void radeon_irq_kms_pflip_irq_get(struct radeon_device *rdev, int crtc);
void radeon_irq_kms_pflip_irq_put(struct radeon_device *rdev, int crtc);
void radeon_irq_kms_enable_afmt(struct radeon_device *rdev, int block);
void radeon_irq_kms_disable_afmt(struct radeon_device *rdev, int block);
void radeon_irq_kms_enable_hpd(struct radeon_device *rdev, unsigned hpd_mask);
void radeon_irq_kms_disable_hpd(struct radeon_device *rdev, unsigned hpd_mask);

/*
 * CP & rings.
 */

struct radeon_ib {
	struct drm_suballoc		*sa_bo;
	uint32_t			length_dw;
	uint64_t			gpu_addr;
	uint32_t			*ptr;
	int				ring;
	struct radeon_fence		*fence;
	struct radeon_vm		*vm;
	bool				is_const_ib;
	struct radeon_sync		sync;
};

struct radeon_ring {
	struct radeon_device	*rdev;
	struct radeon_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr_offs;
	unsigned		rptr_save_reg;
	u64			next_rptr_gpu_addr;
	volatile u32		*next_rptr_cpu_addr;
	unsigned		wptr;
	unsigned		wptr_old;
	unsigned		ring_size;
	unsigned		ring_free_dw;
	int			count_dw;
	atomic_t		last_rptr;
	atomic64_t		last_activity;
	uint64_t		gpu_addr;
	uint32_t		align_mask;
	uint32_t		ptr_mask;
	bool			ready;
	u32			nop;
	u32			idx;
	u64			last_semaphore_signal_addr;
	u64			last_semaphore_wait_addr;
	/* for CIK queues */
	u32 me;
	u32 pipe;
	u32 queue;
	struct radeon_bo	*mqd_obj;
	u32 doorbell_index;
	unsigned		wptr_offs;
};

struct radeon_mec {
	struct radeon_bo	*hpd_eop_obj;
	u64			hpd_eop_gpu_addr;
	u32 num_pipe;
	u32 num_mec;
	u32 num_queue;
};

/*
 * VM
 */

/* maximum number of VMIDs */
#define RADEON_NUM_VM	16

/* number of entries in page table */
#define RADEON_VM_PTE_COUNT (1 << radeon_vm_block_size)

/* PTBs (Page Table Blocks) need to be aligned to 32K */
#define RADEON_VM_PTB_ALIGN_SIZE   32768
#define RADEON_VM_PTB_ALIGN_MASK (RADEON_VM_PTB_ALIGN_SIZE - 1)
#define RADEON_VM_PTB_ALIGN(a) (((a) + RADEON_VM_PTB_ALIGN_MASK) & ~RADEON_VM_PTB_ALIGN_MASK)

#define R600_PTE_VALID		(1 << 0)
#define R600_PTE_SYSTEM		(1 << 1)
#define R600_PTE_SNOOPED	(1 << 2)
#define R600_PTE_READABLE	(1 << 5)
#define R600_PTE_WRITEABLE	(1 << 6)

/* PTE (Page Table Entry) fragment field for different page sizes */
#define R600_PTE_FRAG_4KB	(0 << 7)
#define R600_PTE_FRAG_64KB	(4 << 7)
#define R600_PTE_FRAG_256KB	(6 << 7)

/* flags needed to be set so we can copy directly from the GART table */
#define R600_PTE_GART_MASK	( R600_PTE_READABLE | R600_PTE_WRITEABLE | \
				  R600_PTE_SYSTEM | R600_PTE_VALID )

struct radeon_vm_pt {
	struct radeon_bo		*bo;
	uint64_t			addr;
};

struct radeon_vm_id {
	unsigned		id;
	uint64_t		pd_gpu_addr;
	/* last flushed PD/PT update */
	struct radeon_fence	*flushed_updates;
	/* last use of vmid */
	struct radeon_fence	*last_id_use;
};

struct radeon_vm {
	struct rwlock		mutex;

	struct rb_root_cached	va;

	/* protecting invalidated and freed */
	spinlock_t		status_lock;

	/* BOs moved, but not yet updated in the PT */
	struct list_head	invalidated;

	/* BOs freed, but not yet updated in the PT */
	struct list_head	freed;

	/* BOs cleared in the PT */
	struct list_head	cleared;

	/* contains the page directory */
	struct radeon_bo	*page_directory;
	unsigned		max_pde_used;

	/* array of page tables, one for each page directory entry */
	struct radeon_vm_pt	*page_tables;

	struct radeon_bo_va	*ib_bo_va;

	/* for id and flush management per ring */
	struct radeon_vm_id	ids[RADEON_NUM_RINGS];
};

struct radeon_vm_manager {
	struct radeon_fence		*active[RADEON_NUM_VM];
	uint32_t			max_pfn;
	/* number of VMIDs */
	unsigned			nvm;
	/* vram base address for page table entry  */
	u64				vram_base_offset;
	/* is vm enabled? */
	bool				enabled;
	/* for hw to save the PD addr on suspend/resume */
	uint32_t			saved_table_addr[RADEON_NUM_VM];
};

/*
 * file private structure
 */
struct radeon_fpriv {
	struct radeon_vm		vm;
};

/*
 * R6xx+ IH ring
 */
struct r600_ih {
	struct radeon_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr;
	unsigned		ring_size;
	uint64_t		gpu_addr;
	uint32_t		ptr_mask;
	atomic_t		lock;
	bool                    enabled;
};

/*
 * RLC stuff
 */
#include "clearstate_defs.h"

struct radeon_rlc {
	/* for power gating */
	struct radeon_bo	*save_restore_obj;
	uint64_t		save_restore_gpu_addr;
	volatile uint32_t	*sr_ptr;
	const u32               *reg_list;
	u32                     reg_list_size;
	/* for clear state */
	struct radeon_bo	*clear_state_obj;
	uint64_t		clear_state_gpu_addr;
	volatile uint32_t	*cs_ptr;
	const struct cs_section_def   *cs_data;
	u32                     clear_state_size;
	/* for cp tables */
	struct radeon_bo	*cp_table_obj;
	uint64_t		cp_table_gpu_addr;
	volatile uint32_t	*cp_table_ptr;
	u32                     cp_table_size;
};

int radeon_ib_get(struct radeon_device *rdev, int ring,
		  struct radeon_ib *ib, struct radeon_vm *vm,
		  unsigned size);
void radeon_ib_free(struct radeon_device *rdev, struct radeon_ib *ib);
int radeon_ib_schedule(struct radeon_device *rdev, struct radeon_ib *ib,
		       struct radeon_ib *const_ib, bool hdp_flush);
int radeon_ib_pool_init(struct radeon_device *rdev);
void radeon_ib_pool_fini(struct radeon_device *rdev);
int radeon_ib_ring_tests(struct radeon_device *rdev);
/* Ring access between begin & end cannot sleep */
bool radeon_ring_supports_scratch_reg(struct radeon_device *rdev,
				      struct radeon_ring *ring);
void radeon_ring_free_size(struct radeon_device *rdev, struct radeon_ring *cp);
int radeon_ring_alloc(struct radeon_device *rdev, struct radeon_ring *cp, unsigned ndw);
int radeon_ring_lock(struct radeon_device *rdev, struct radeon_ring *cp, unsigned ndw);
void radeon_ring_commit(struct radeon_device *rdev, struct radeon_ring *cp,
			bool hdp_flush);
void radeon_ring_unlock_commit(struct radeon_device *rdev, struct radeon_ring *cp,
			       bool hdp_flush);
void radeon_ring_undo(struct radeon_ring *ring);
void radeon_ring_unlock_undo(struct radeon_device *rdev, struct radeon_ring *cp);
int radeon_ring_test(struct radeon_device *rdev, struct radeon_ring *cp);
void radeon_ring_lockup_update(struct radeon_device *rdev,
			       struct radeon_ring *ring);
bool radeon_ring_test_lockup(struct radeon_device *rdev, struct radeon_ring *ring);
unsigned radeon_ring_backup(struct radeon_device *rdev, struct radeon_ring *ring,
			    uint32_t **data);
int radeon_ring_restore(struct radeon_device *rdev, struct radeon_ring *ring,
			unsigned size, uint32_t *data);
int radeon_ring_init(struct radeon_device *rdev, struct radeon_ring *cp, unsigned ring_size,
		     unsigned rptr_offs, u32 nop);
void radeon_ring_fini(struct radeon_device *rdev, struct radeon_ring *cp);


/* r600 async dma */
void r600_dma_stop(struct radeon_device *rdev);
int r600_dma_resume(struct radeon_device *rdev);
void r600_dma_fini(struct radeon_device *rdev);

void cayman_dma_stop(struct radeon_device *rdev);
int cayman_dma_resume(struct radeon_device *rdev);
void cayman_dma_fini(struct radeon_device *rdev);

/*
 * CS.
 */
struct radeon_cs_chunk {
	uint32_t		length_dw;
	uint32_t		*kdata;
	void __user		*user_ptr;
};

struct radeon_cs_parser {
	struct device		*dev;
	struct radeon_device	*rdev;
	struct drm_file		*filp;
	/* chunks */
	unsigned		nchunks;
	struct radeon_cs_chunk	*chunks;
	uint64_t		*chunks_array;
	/* IB */
	unsigned		idx;
	/* relocations */
	unsigned		nrelocs;
	struct radeon_bo_list	*relocs;
	struct radeon_bo_list	*vm_bos;
	struct list_head	validated;
	unsigned		dma_reloc_idx;
	/* indices of various chunks */
	struct radeon_cs_chunk  *chunk_ib;
	struct radeon_cs_chunk  *chunk_relocs;
	struct radeon_cs_chunk  *chunk_flags;
	struct radeon_cs_chunk  *chunk_const_ib;
	struct radeon_ib	ib;
	struct radeon_ib	const_ib;
	void			*track;
	unsigned		family;
	int			parser_error;
	u32			cs_flags;
	u32			ring;
	s32			priority;
	struct ww_acquire_ctx	ticket;
};

static inline u32 radeon_get_ib_value(struct radeon_cs_parser *p, int idx)
{
	struct radeon_cs_chunk *ibc = p->chunk_ib;

	if (ibc->kdata)
		return ibc->kdata[idx];
	return p->ib.ptr[idx];
}


struct radeon_cs_packet {
	unsigned	idx;
	unsigned	type;
	unsigned	reg;
	unsigned	opcode;
	int		count;
	unsigned	one_reg_wr;
};

typedef int (*radeon_packet0_check_t)(struct radeon_cs_parser *p,
				      struct radeon_cs_packet *pkt,
				      unsigned idx, unsigned reg);

/*
 * AGP
 */

struct radeon_agp_mode {
	unsigned long mode;	/**< AGP mode */
};

struct radeon_agp_info {
	int agp_version_major;
	int agp_version_minor;
	unsigned long mode;
	unsigned long aperture_base;	/* physical address */
	unsigned long aperture_size;	/* bytes */
	unsigned long memory_allowed;	/* bytes */
	unsigned long memory_used;

	/* PCI information */
	unsigned short id_vendor;
	unsigned short id_device;
};

struct radeon_agp_head {
#ifdef notyet
	struct agp_kern_info agp_info;
#endif
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

#if IS_ENABLED(CONFIG_AGP)
struct radeon_agp_head *radeon_agp_head_init(struct drm_device *dev);
#else
static inline struct radeon_agp_head *radeon_agp_head_init(struct drm_device *dev)
{
	return NULL;
}
#endif
int radeon_agp_init(struct radeon_device *rdev);
void radeon_agp_resume(struct radeon_device *rdev);
void radeon_agp_suspend(struct radeon_device *rdev);
void radeon_agp_fini(struct radeon_device *rdev);


/*
 * Writeback
 */
struct radeon_wb {
	struct radeon_bo	*wb_obj;
	volatile uint32_t	*wb;
	uint64_t		gpu_addr;
	bool                    enabled;
	bool                    use_event;
};

#define RADEON_WB_SCRATCH_OFFSET 0
#define RADEON_WB_RING0_NEXT_RPTR 256
#define RADEON_WB_CP_RPTR_OFFSET 1024
#define RADEON_WB_CP1_RPTR_OFFSET 1280
#define RADEON_WB_CP2_RPTR_OFFSET 1536
#define R600_WB_DMA_RPTR_OFFSET   1792
#define R600_WB_IH_WPTR_OFFSET   2048
#define CAYMAN_WB_DMA1_RPTR_OFFSET   2304
#define R600_WB_EVENT_OFFSET     3072
#define CIK_WB_CP1_WPTR_OFFSET     3328
#define CIK_WB_CP2_WPTR_OFFSET     3584
#define R600_WB_DMA_RING_TEST_OFFSET 3588
#define CAYMAN_WB_DMA1_RING_TEST_OFFSET 3592

/**
 * struct radeon_pm - power management datas
 * @max_bandwidth:      maximum bandwidth the gpu has (MByte/s)
 * @igp_sideport_mclk:  sideport memory clock Mhz (rs690,rs740,rs780,rs880)
 * @igp_system_mclk:    system clock Mhz (rs690,rs740,rs780,rs880)
 * @igp_ht_link_clk:    ht link clock Mhz (rs690,rs740,rs780,rs880)
 * @igp_ht_link_width:  ht link width in bits (rs690,rs740,rs780,rs880)
 * @k8_bandwidth:       k8 bandwidth the gpu has (MByte/s) (IGP)
 * @sideport_bandwidth: sideport bandwidth the gpu has (MByte/s) (IGP)
 * @ht_bandwidth:       ht bandwidth the gpu has (MByte/s) (IGP)
 * @core_bandwidth:     core GPU bandwidth the gpu has (MByte/s) (IGP)
 * @sclk:          	GPU clock Mhz (core bandwidth depends of this clock)
 * @needed_bandwidth:   current bandwidth needs
 *
 * It keeps track of various data needed to take powermanagement decision.
 * Bandwidth need is used to determine minimun clock of the GPU and memory.
 * Equation between gpu/memory clock and available bandwidth is hw dependent
 * (type of memory, bus size, efficiency, ...)
 */

enum radeon_pm_method {
	PM_METHOD_PROFILE,
	PM_METHOD_DYNPM,
	PM_METHOD_DPM,
};

enum radeon_dynpm_state {
	DYNPM_STATE_DISABLED,
	DYNPM_STATE_MINIMUM,
	DYNPM_STATE_PAUSED,
	DYNPM_STATE_ACTIVE,
	DYNPM_STATE_SUSPENDED,
};
enum radeon_dynpm_action {
	DYNPM_ACTION_NONE,
	DYNPM_ACTION_MINIMUM,
	DYNPM_ACTION_DOWNCLOCK,
	DYNPM_ACTION_UPCLOCK,
	DYNPM_ACTION_DEFAULT
};

enum radeon_voltage_type {
	VOLTAGE_NONE = 0,
	VOLTAGE_GPIO,
	VOLTAGE_VDDC,
	VOLTAGE_SW
};

enum radeon_pm_state_type {
	/* not used for dpm */
	POWER_STATE_TYPE_DEFAULT,
	POWER_STATE_TYPE_POWERSAVE,
	/* user selectable states */
	POWER_STATE_TYPE_BATTERY,
	POWER_STATE_TYPE_BALANCED,
	POWER_STATE_TYPE_PERFORMANCE,
	/* internal states */
	POWER_STATE_TYPE_INTERNAL_UVD,
	POWER_STATE_TYPE_INTERNAL_UVD_SD,
	POWER_STATE_TYPE_INTERNAL_UVD_HD,
	POWER_STATE_TYPE_INTERNAL_UVD_HD2,
	POWER_STATE_TYPE_INTERNAL_UVD_MVC,
	POWER_STATE_TYPE_INTERNAL_BOOT,
	POWER_STATE_TYPE_INTERNAL_THERMAL,
	POWER_STATE_TYPE_INTERNAL_ACPI,
	POWER_STATE_TYPE_INTERNAL_ULV,
	POWER_STATE_TYPE_INTERNAL_3DPERF,
};

enum radeon_pm_profile_type {
	PM_PROFILE_DEFAULT,
	PM_PROFILE_AUTO,
	PM_PROFILE_LOW,
	PM_PROFILE_MID,
	PM_PROFILE_HIGH,
};

#define PM_PROFILE_DEFAULT_IDX 0
#define PM_PROFILE_LOW_SH_IDX  1
#define PM_PROFILE_MID_SH_IDX  2
#define PM_PROFILE_HIGH_SH_IDX 3
#define PM_PROFILE_LOW_MH_IDX  4
#define PM_PROFILE_MID_MH_IDX  5
#define PM_PROFILE_HIGH_MH_IDX 6
#define PM_PROFILE_MAX         7

struct radeon_pm_profile {
	int dpms_off_ps_idx;
	int dpms_on_ps_idx;
	int dpms_off_cm_idx;
	int dpms_on_cm_idx;
};

enum radeon_int_thermal_type {
	THERMAL_TYPE_NONE,
	THERMAL_TYPE_EXTERNAL,
	THERMAL_TYPE_EXTERNAL_GPIO,
	THERMAL_TYPE_RV6XX,
	THERMAL_TYPE_RV770,
	THERMAL_TYPE_ADT7473_WITH_INTERNAL,
	THERMAL_TYPE_EVERGREEN,
	THERMAL_TYPE_SUMO,
	THERMAL_TYPE_NI,
	THERMAL_TYPE_SI,
	THERMAL_TYPE_EMC2103_WITH_INTERNAL,
	THERMAL_TYPE_CI,
	THERMAL_TYPE_KV,
};

struct radeon_voltage {
	enum radeon_voltage_type type;
	/* gpio voltage */
	struct radeon_gpio_rec gpio;
	u32 delay; /* delay in usec from voltage drop to sclk change */
	bool active_high; /* voltage drop is active when bit is high */
	/* VDDC voltage */
	u8 vddc_id; /* index into vddc voltage table */
	u8 vddci_id; /* index into vddci voltage table */
	bool vddci_enabled;
	/* r6xx+ sw */
	u16 voltage;
	/* evergreen+ vddci */
	u16 vddci;
};

/* clock mode flags */
#define RADEON_PM_MODE_NO_DISPLAY          (1 << 0)

struct radeon_pm_clock_info {
	/* memory clock */
	u32 mclk;
	/* engine clock */
	u32 sclk;
	/* voltage info */
	struct radeon_voltage voltage;
	/* standardized clock flags */
	u32 flags;
};

/* state flags */
#define RADEON_PM_STATE_SINGLE_DISPLAY_ONLY (1 << 0)

struct radeon_power_state {
	enum radeon_pm_state_type type;
	struct radeon_pm_clock_info *clock_info;
	/* number of valid clock modes in this power state */
	int num_clock_modes;
	struct radeon_pm_clock_info *default_clock_mode;
	/* standardized state flags */
	u32 flags;
	u32 misc; /* vbios specific flags */
	u32 misc2; /* vbios specific flags */
	int pcie_lanes; /* pcie lanes */
};

/*
 * Some modes are overclocked by very low value, accept them
 */
#define RADEON_MODE_OVERCLOCK_MARGIN 500 /* 5 MHz */

enum radeon_dpm_auto_throttle_src {
	RADEON_DPM_AUTO_THROTTLE_SRC_THERMAL,
	RADEON_DPM_AUTO_THROTTLE_SRC_EXTERNAL
};

enum radeon_dpm_event_src {
	RADEON_DPM_EVENT_SRC_ANALOG = 0,
	RADEON_DPM_EVENT_SRC_EXTERNAL = 1,
	RADEON_DPM_EVENT_SRC_DIGITAL = 2,
	RADEON_DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,
	RADEON_DPM_EVENT_SRC_DIGIAL_OR_EXTERNAL = 4
};

#define RADEON_MAX_VCE_LEVELS 6

enum radeon_vce_level {
	RADEON_VCE_LEVEL_AC_ALL = 0,     /* AC, All cases */
	RADEON_VCE_LEVEL_DC_EE = 1,      /* DC, entropy encoding */
	RADEON_VCE_LEVEL_DC_LL_LOW = 2,  /* DC, low latency queue, res <= 720 */
	RADEON_VCE_LEVEL_DC_LL_HIGH = 3, /* DC, low latency queue, 1080 >= res > 720 */
	RADEON_VCE_LEVEL_DC_GP_LOW = 4,  /* DC, general purpose queue, res <= 720 */
	RADEON_VCE_LEVEL_DC_GP_HIGH = 5, /* DC, general purpose queue, 1080 >= res > 720 */
};

struct radeon_ps {
	u32 caps; /* vbios flags */
	u32 class; /* vbios flags */
	u32 class2; /* vbios flags */
	/* UVD clocks */
	u32 vclk;
	u32 dclk;
	/* VCE clocks */
	u32 evclk;
	u32 ecclk;
	bool vce_active;
	enum radeon_vce_level vce_level;
	/* asic priv */
	void *ps_priv;
};

struct radeon_dpm_thermal {
	/* thermal interrupt work */
	struct work_struct work;
	/* low temperature threshold */
	int                min_temp;
	/* high temperature threshold */
	int                max_temp;
	/* was interrupt low to high or high to low */
	bool               high_to_low;
};

enum radeon_clk_action {
	RADEON_SCLK_UP = 1,
	RADEON_SCLK_DOWN
};

struct radeon_blacklist_clocks {
	u32 sclk;
	u32 mclk;
	enum radeon_clk_action action;
};

struct radeon_clock_and_voltage_limits {
	u32 sclk;
	u32 mclk;
	u16 vddc;
	u16 vddci;
};

struct radeon_clock_array {
	u32 count;
	u32 *values;
};

struct radeon_clock_voltage_dependency_entry {
	u32 clk;
	u16 v;
};

struct radeon_clock_voltage_dependency_table {
	u32 count;
	struct radeon_clock_voltage_dependency_entry *entries;
};

union radeon_cac_leakage_entry {
	struct {
		u16 vddc;
		u32 leakage;
	};
	struct {
		u16 vddc1;
		u16 vddc2;
		u16 vddc3;
	};
};

struct radeon_cac_leakage_table {
	u32 count;
	union radeon_cac_leakage_entry *entries;
};

struct radeon_phase_shedding_limits_entry {
	u16 voltage;
	u32 sclk;
	u32 mclk;
};

struct radeon_phase_shedding_limits_table {
	u32 count;
	struct radeon_phase_shedding_limits_entry *entries;
};

struct radeon_uvd_clock_voltage_dependency_entry {
	u32 vclk;
	u32 dclk;
	u16 v;
};

struct radeon_uvd_clock_voltage_dependency_table {
	u8 count;
	struct radeon_uvd_clock_voltage_dependency_entry *entries;
};

struct radeon_vce_clock_voltage_dependency_entry {
	u32 ecclk;
	u32 evclk;
	u16 v;
};

struct radeon_vce_clock_voltage_dependency_table {
	u8 count;
	struct radeon_vce_clock_voltage_dependency_entry *entries;
};

struct radeon_ppm_table {
	u8 ppm_design;
	u16 cpu_core_number;
	u32 platform_tdp;
	u32 small_ac_platform_tdp;
	u32 platform_tdc;
	u32 small_ac_platform_tdc;
	u32 apu_tdp;
	u32 dgpu_tdp;
	u32 dgpu_ulv_power;
	u32 tj_max;
};

struct radeon_cac_tdp_table {
	u16 tdp;
	u16 configurable_tdp;
	u16 tdc;
	u16 battery_power_limit;
	u16 small_power_limit;
	u16 low_cac_leakage;
	u16 high_cac_leakage;
	u16 maximum_power_delivery_limit;
};

struct radeon_dpm_dynamic_state {
	struct radeon_clock_voltage_dependency_table vddc_dependency_on_sclk;
	struct radeon_clock_voltage_dependency_table vddci_dependency_on_mclk;
	struct radeon_clock_voltage_dependency_table vddc_dependency_on_mclk;
	struct radeon_clock_voltage_dependency_table mvdd_dependency_on_mclk;
	struct radeon_clock_voltage_dependency_table vddc_dependency_on_dispclk;
	struct radeon_uvd_clock_voltage_dependency_table uvd_clock_voltage_dependency_table;
	struct radeon_vce_clock_voltage_dependency_table vce_clock_voltage_dependency_table;
	struct radeon_clock_voltage_dependency_table samu_clock_voltage_dependency_table;
	struct radeon_clock_voltage_dependency_table acp_clock_voltage_dependency_table;
	struct radeon_clock_array valid_sclk_values;
	struct radeon_clock_array valid_mclk_values;
	struct radeon_clock_and_voltage_limits max_clock_voltage_on_dc;
	struct radeon_clock_and_voltage_limits max_clock_voltage_on_ac;
	u32 mclk_sclk_ratio;
	u32 sclk_mclk_delta;
	u16 vddc_vddci_delta;
	u16 min_vddc_for_pcie_gen2;
	struct radeon_cac_leakage_table cac_leakage_table;
	struct radeon_phase_shedding_limits_table phase_shedding_limits_table;
	struct radeon_ppm_table *ppm_table;
	struct radeon_cac_tdp_table *cac_tdp_table;
};

struct radeon_dpm_fan {
	u16 t_min;
	u16 t_med;
	u16 t_high;
	u16 pwm_min;
	u16 pwm_med;
	u16 pwm_high;
	u8 t_hyst;
	u32 cycle_delay;
	u16 t_max;
	u8 control_mode;
	u16 default_max_fan_pwm;
	u16 default_fan_output_sensitivity;
	u16 fan_output_sensitivity;
	bool ucode_fan_control;
};

enum radeon_pcie_gen {
	RADEON_PCIE_GEN1 = 0,
	RADEON_PCIE_GEN2 = 1,
	RADEON_PCIE_GEN3 = 2,
	RADEON_PCIE_GEN_INVALID = 0xffff
};

enum radeon_dpm_forced_level {
	RADEON_DPM_FORCED_LEVEL_AUTO = 0,
	RADEON_DPM_FORCED_LEVEL_LOW = 1,
	RADEON_DPM_FORCED_LEVEL_HIGH = 2,
};

struct radeon_vce_state {
	/* vce clocks */
	u32 evclk;
	u32 ecclk;
	/* gpu clocks */
	u32 sclk;
	u32 mclk;
	u8 clk_idx;
	u8 pstate;
};

struct radeon_dpm {
	struct radeon_ps        *ps;
	/* number of valid power states */
	int                     num_ps;
	/* current power state that is active */
	struct radeon_ps        *current_ps;
	/* requested power state */
	struct radeon_ps        *requested_ps;
	/* boot up power state */
	struct radeon_ps        *boot_ps;
	/* default uvd power state */
	struct radeon_ps        *uvd_ps;
	/* vce requirements */
	struct radeon_vce_state vce_states[RADEON_MAX_VCE_LEVELS];
	enum radeon_vce_level vce_level;
	enum radeon_pm_state_type state;
	enum radeon_pm_state_type user_state;
	u32                     platform_caps;
	u32                     voltage_response_time;
	u32                     backbias_response_time;
	void                    *priv;
	u32			new_active_crtcs;
	int			new_active_crtc_count;
	int			high_pixelclock_count;
	u32			current_active_crtcs;
	int			current_active_crtc_count;
	bool single_display;
	struct radeon_dpm_dynamic_state dyn_state;
	struct radeon_dpm_fan fan;
	u32 tdp_limit;
	u32 near_tdp_limit;
	u32 near_tdp_limit_adjusted;
	u32 sq_ramping_threshold;
	u32 cac_leakage;
	u16 tdp_od_limit;
	u32 tdp_adjustment;
	u16 load_line_slope;
	bool power_control;
	bool ac_power;
	/* special states active */
	bool                    thermal_active;
	bool                    uvd_active;
	bool                    vce_active;
	/* thermal handling */
	struct radeon_dpm_thermal thermal;
	/* forced levels */
	enum radeon_dpm_forced_level forced_level;
	/* track UVD streams */
	unsigned sd;
	unsigned hd;
};

void radeon_dpm_enable_uvd(struct radeon_device *rdev, bool enable);
void radeon_dpm_enable_vce(struct radeon_device *rdev, bool enable);

struct radeon_pm {
	struct rwlock		mutex;
	/* write locked while reprogramming mclk */
	struct rwlock		mclk_lock;
	u32			active_crtcs;
	int			active_crtc_count;
	int			req_vblank;
	bool			vblank_sync;
	fixed20_12		max_bandwidth;
	fixed20_12		igp_sideport_mclk;
	fixed20_12		igp_system_mclk;
	fixed20_12		igp_ht_link_clk;
	fixed20_12		igp_ht_link_width;
	fixed20_12		k8_bandwidth;
	fixed20_12		sideport_bandwidth;
	fixed20_12		ht_bandwidth;
	fixed20_12		core_bandwidth;
	fixed20_12		sclk;
	fixed20_12		mclk;
	fixed20_12		needed_bandwidth;
	struct radeon_power_state *power_state;
	/* number of valid power states */
	int                     num_power_states;
	int                     current_power_state_index;
	int                     current_clock_mode_index;
	int                     requested_power_state_index;
	int                     requested_clock_mode_index;
	int                     default_power_state_index;
	u32                     current_sclk;
	u32                     current_mclk;
	u16                     current_vddc;
	u16                     current_vddci;
	u32                     default_sclk;
	u32                     default_mclk;
	u16                     default_vddc;
	u16                     default_vddci;
	struct radeon_i2c_chan *i2c_bus;
	/* selected pm method */
	enum radeon_pm_method     pm_method;
	/* dynpm power management */
	struct delayed_work	dynpm_idle_work;
	enum radeon_dynpm_state	dynpm_state;
	enum radeon_dynpm_action	dynpm_planned_action;
	unsigned long		dynpm_action_timeout;
	bool                    dynpm_can_upclock;
	bool                    dynpm_can_downclock;
	/* profile-based power management */
	enum radeon_pm_profile_type profile;
	int                     profile_index;
	struct radeon_pm_profile profiles[PM_PROFILE_MAX];
	/* internal thermal controller on rv6xx+ */
	enum radeon_int_thermal_type int_thermal_type;
	struct device	        *int_hwmon_dev;
	/* fan control parameters */
	bool                    no_fan;
	u8                      fan_pulses_per_revolution;
	u8                      fan_min_rpm;
	u8                      fan_max_rpm;
	/* dpm */
	bool                    dpm_enabled;
	bool                    sysfs_initialized;
	struct radeon_dpm       dpm;
};

#define RADEON_PCIE_SPEED_25 1
#define RADEON_PCIE_SPEED_50 2
#define RADEON_PCIE_SPEED_80 4

int radeon_pm_get_type_index(struct radeon_device *rdev,
			     enum radeon_pm_state_type ps_type,
			     int instance);
/*
 * UVD
 */
#define RADEON_DEFAULT_UVD_HANDLES	10
#define RADEON_MAX_UVD_HANDLES		30
#define RADEON_UVD_STACK_SIZE		(200*1024)
#define RADEON_UVD_HEAP_SIZE		(256*1024)
#define RADEON_UVD_SESSION_SIZE		(50*1024)

struct radeon_uvd {
	bool			fw_header_present;
	struct radeon_bo	*vcpu_bo;
	void			*cpu_addr;
	uint64_t		gpu_addr;
	unsigned		max_handles;
	atomic_t		handles[RADEON_MAX_UVD_HANDLES];
	struct drm_file		*filp[RADEON_MAX_UVD_HANDLES];
	unsigned		img_size[RADEON_MAX_UVD_HANDLES];
	struct delayed_work	idle_work;
};

int radeon_uvd_init(struct radeon_device *rdev);
void radeon_uvd_fini(struct radeon_device *rdev);
int radeon_uvd_suspend(struct radeon_device *rdev);
int radeon_uvd_resume(struct radeon_device *rdev);
int radeon_uvd_get_create_msg(struct radeon_device *rdev, int ring,
			      uint32_t handle, struct radeon_fence **fence);
int radeon_uvd_get_destroy_msg(struct radeon_device *rdev, int ring,
			       uint32_t handle, struct radeon_fence **fence);
void radeon_uvd_force_into_uvd_segment(struct radeon_bo *rbo,
				       uint32_t allowed_domains);
void radeon_uvd_free_handles(struct radeon_device *rdev,
			     struct drm_file *filp);
int radeon_uvd_cs_parse(struct radeon_cs_parser *parser);
void radeon_uvd_note_usage(struct radeon_device *rdev);
int radeon_uvd_calc_upll_dividers(struct radeon_device *rdev,
				  unsigned vclk, unsigned dclk,
				  unsigned vco_min, unsigned vco_max,
				  unsigned fb_factor, unsigned fb_mask,
				  unsigned pd_min, unsigned pd_max,
				  unsigned pd_even,
				  unsigned *optimal_fb_div,
				  unsigned *optimal_vclk_div,
				  unsigned *optimal_dclk_div);
int radeon_uvd_send_upll_ctlreq(struct radeon_device *rdev,
                                unsigned cg_upll_func_cntl);

/*
 * VCE
 */
#define RADEON_MAX_VCE_HANDLES	16

struct radeon_vce {
	struct radeon_bo	*vcpu_bo;
	uint64_t		gpu_addr;
	unsigned		fw_version;
	unsigned		fb_version;
	atomic_t		handles[RADEON_MAX_VCE_HANDLES];
	struct drm_file		*filp[RADEON_MAX_VCE_HANDLES];
	unsigned		img_size[RADEON_MAX_VCE_HANDLES];
	struct delayed_work	idle_work;
	uint32_t		keyselect;
};

int radeon_vce_init(struct radeon_device *rdev);
void radeon_vce_fini(struct radeon_device *rdev);
int radeon_vce_suspend(struct radeon_device *rdev);
int radeon_vce_resume(struct radeon_device *rdev);
int radeon_vce_get_create_msg(struct radeon_device *rdev, int ring,
			      uint32_t handle, struct radeon_fence **fence);
int radeon_vce_get_destroy_msg(struct radeon_device *rdev, int ring,
			       uint32_t handle, struct radeon_fence **fence);
void radeon_vce_free_handles(struct radeon_device *rdev, struct drm_file *filp);
void radeon_vce_note_usage(struct radeon_device *rdev);
int radeon_vce_cs_reloc(struct radeon_cs_parser *p, int lo, int hi, unsigned size);
int radeon_vce_cs_parse(struct radeon_cs_parser *p);
bool radeon_vce_semaphore_emit(struct radeon_device *rdev,
			       struct radeon_ring *ring,
			       struct radeon_semaphore *semaphore,
			       bool emit_wait);
void radeon_vce_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib);
void radeon_vce_fence_emit(struct radeon_device *rdev,
			   struct radeon_fence *fence);
int radeon_vce_ring_test(struct radeon_device *rdev, struct radeon_ring *ring);
int radeon_vce_ib_test(struct radeon_device *rdev, struct radeon_ring *ring);

struct r600_audio_pin {
	int			channels;
	int			rate;
	int			bits_per_sample;
	u8			status_bits;
	u8			category_code;
	u32			offset;
	bool			connected;
	u32			id;
};

struct r600_audio {
	bool enabled;
	struct r600_audio_pin pin[RADEON_MAX_AFMT_BLOCKS];
	int num_pins;
	struct radeon_audio_funcs *hdmi_funcs;
	struct radeon_audio_funcs *dp_funcs;
	struct radeon_audio_basic_funcs *funcs;
	struct drm_audio_component *component;
	bool component_registered;
	struct rwlock component_mutex;
};

/*
 * Benchmarking
 */
void radeon_benchmark(struct radeon_device *rdev, int test_number);


/*
 * Testing
 */
void radeon_test_moves(struct radeon_device *rdev);
void radeon_test_ring_sync(struct radeon_device *rdev,
			   struct radeon_ring *cpA,
			   struct radeon_ring *cpB);
void radeon_test_syncing(struct radeon_device *rdev);

/*
 * MMU Notifier
 */
#if defined(CONFIG_MMU_NOTIFIER)
int radeon_mn_register(struct radeon_bo *bo, unsigned long addr);
void radeon_mn_unregister(struct radeon_bo *bo);
#else
static inline int radeon_mn_register(struct radeon_bo *bo, unsigned long addr)
{
	return -ENODEV;
}
static inline void radeon_mn_unregister(struct radeon_bo *bo) {}
#endif

/*
 * Debugfs
 */
void radeon_debugfs_fence_init(struct radeon_device *rdev);
void radeon_gem_debugfs_init(struct radeon_device *rdev);

/*
 * ASIC ring specific functions.
 */
struct radeon_asic_ring {
	/* ring read/write ptr handling */
	u32 (*get_rptr)(struct radeon_device *rdev, struct radeon_ring *ring);
	u32 (*get_wptr)(struct radeon_device *rdev, struct radeon_ring *ring);
	void (*set_wptr)(struct radeon_device *rdev, struct radeon_ring *ring);

	/* validating and patching of IBs */
	int (*ib_parse)(struct radeon_device *rdev, struct radeon_ib *ib);
	int (*cs_parse)(struct radeon_cs_parser *p);

	/* command emmit functions */
	void (*ib_execute)(struct radeon_device *rdev, struct radeon_ib *ib);
	void (*emit_fence)(struct radeon_device *rdev, struct radeon_fence *fence);
	void (*hdp_flush)(struct radeon_device *rdev, struct radeon_ring *ring);
	bool (*emit_semaphore)(struct radeon_device *rdev, struct radeon_ring *cp,
			       struct radeon_semaphore *semaphore, bool emit_wait);
	void (*vm_flush)(struct radeon_device *rdev, struct radeon_ring *ring,
			 unsigned vm_id, uint64_t pd_addr);

	/* testing functions */
	int (*ring_test)(struct radeon_device *rdev, struct radeon_ring *cp);
	int (*ib_test)(struct radeon_device *rdev, struct radeon_ring *cp);
	bool (*is_lockup)(struct radeon_device *rdev, struct radeon_ring *cp);

	/* deprecated */
	void (*ring_start)(struct radeon_device *rdev, struct radeon_ring *cp);
};

/*
 * ASIC specific functions.
 */
struct radeon_asic {
	int (*init)(struct radeon_device *rdev);
	void (*fini)(struct radeon_device *rdev);
	int (*resume)(struct radeon_device *rdev);
	int (*suspend)(struct radeon_device *rdev);
	void (*vga_set_state)(struct radeon_device *rdev, bool state);
	int (*asic_reset)(struct radeon_device *rdev, bool hard);
	/* Flush the HDP cache via MMIO */
	void (*mmio_hdp_flush)(struct radeon_device *rdev);
	/* check if 3D engine is idle */
	bool (*gui_idle)(struct radeon_device *rdev);
	/* wait for mc_idle */
	int (*mc_wait_for_idle)(struct radeon_device *rdev);
	/* get the reference clock */
	u32 (*get_xclk)(struct radeon_device *rdev);
	/* get the gpu clock counter */
	uint64_t (*get_gpu_clock_counter)(struct radeon_device *rdev);
	/* get register for info ioctl */
	int (*get_allowed_info_register)(struct radeon_device *rdev, u32 reg, u32 *val);
	/* gart */
	struct {
		void (*tlb_flush)(struct radeon_device *rdev);
		uint64_t (*get_page_entry)(uint64_t addr, uint32_t flags);
		void (*set_page)(struct radeon_device *rdev, unsigned i,
				 uint64_t entry);
	} gart;
	struct {
		int (*init)(struct radeon_device *rdev);
		void (*fini)(struct radeon_device *rdev);
		void (*copy_pages)(struct radeon_device *rdev,
				   struct radeon_ib *ib,
				   uint64_t pe, uint64_t src,
				   unsigned count);
		void (*write_pages)(struct radeon_device *rdev,
				    struct radeon_ib *ib,
				    uint64_t pe,
				    uint64_t addr, unsigned count,
				    uint32_t incr, uint32_t flags);
		void (*set_pages)(struct radeon_device *rdev,
				  struct radeon_ib *ib,
				  uint64_t pe,
				  uint64_t addr, unsigned count,
				  uint32_t incr, uint32_t flags);
		void (*pad_ib)(struct radeon_ib *ib);
	} vm;
	/* ring specific callbacks */
	const struct radeon_asic_ring *ring[RADEON_NUM_RINGS];
	/* irqs */
	struct {
		int (*set)(struct radeon_device *rdev);
		int (*process)(struct radeon_device *rdev);
	} irq;
	/* displays */
	struct {
		/* display watermarks */
		void (*bandwidth_update)(struct radeon_device *rdev);
		/* get frame count */
		u32 (*get_vblank_counter)(struct radeon_device *rdev, int crtc);
		/* wait for vblank */
		void (*wait_for_vblank)(struct radeon_device *rdev, int crtc);
		/* set backlight level */
		void (*set_backlight_level)(struct radeon_encoder *radeon_encoder, u8 level);
		/* get backlight level */
		u8 (*get_backlight_level)(struct radeon_encoder *radeon_encoder);
		/* audio callbacks */
		void (*hdmi_enable)(struct drm_encoder *encoder, bool enable);
		void (*hdmi_setmode)(struct drm_encoder *encoder, struct drm_display_mode *mode);
	} display;
	/* copy functions for bo handling */
	struct {
		struct radeon_fence *(*blit)(struct radeon_device *rdev,
					     uint64_t src_offset,
					     uint64_t dst_offset,
					     unsigned num_gpu_pages,
					     struct dma_resv *resv);
		u32 blit_ring_index;
		struct radeon_fence *(*dma)(struct radeon_device *rdev,
					    uint64_t src_offset,
					    uint64_t dst_offset,
					    unsigned num_gpu_pages,
					    struct dma_resv *resv);
		u32 dma_ring_index;
		/* method used for bo copy */
		struct radeon_fence *(*copy)(struct radeon_device *rdev,
					     uint64_t src_offset,
					     uint64_t dst_offset,
					     unsigned num_gpu_pages,
					     struct dma_resv *resv);
		/* ring used for bo copies */
		u32 copy_ring_index;
	} copy;
	/* surfaces */
	struct {
		int (*set_reg)(struct radeon_device *rdev, int reg,
				       uint32_t tiling_flags, uint32_t pitch,
				       uint32_t offset, uint32_t obj_size);
		void (*clear_reg)(struct radeon_device *rdev, int reg);
	} surface;
	/* hotplug detect */
	struct {
		void (*init)(struct radeon_device *rdev);
		void (*fini)(struct radeon_device *rdev);
		bool (*sense)(struct radeon_device *rdev, enum radeon_hpd_id hpd);
		void (*set_polarity)(struct radeon_device *rdev, enum radeon_hpd_id hpd);
	} hpd;
	/* static power management */
	struct {
		void (*misc)(struct radeon_device *rdev);
		void (*prepare)(struct radeon_device *rdev);
		void (*finish)(struct radeon_device *rdev);
		void (*init_profile)(struct radeon_device *rdev);
		void (*get_dynpm_state)(struct radeon_device *rdev);
		uint32_t (*get_engine_clock)(struct radeon_device *rdev);
		void (*set_engine_clock)(struct radeon_device *rdev, uint32_t eng_clock);
		uint32_t (*get_memory_clock)(struct radeon_device *rdev);
		void (*set_memory_clock)(struct radeon_device *rdev, uint32_t mem_clock);
		int (*get_pcie_lanes)(struct radeon_device *rdev);
		void (*set_pcie_lanes)(struct radeon_device *rdev, int lanes);
		void (*set_clock_gating)(struct radeon_device *rdev, int enable);
		int (*set_uvd_clocks)(struct radeon_device *rdev, u32 vclk, u32 dclk);
		int (*set_vce_clocks)(struct radeon_device *rdev, u32 evclk, u32 ecclk);
		int (*get_temperature)(struct radeon_device *rdev);
	} pm;
	/* dynamic power management */
	struct {
		int (*init)(struct radeon_device *rdev);
		void (*setup_asic)(struct radeon_device *rdev);
		int (*enable)(struct radeon_device *rdev);
		int (*late_enable)(struct radeon_device *rdev);
		void (*disable)(struct radeon_device *rdev);
		int (*pre_set_power_state)(struct radeon_device *rdev);
		int (*set_power_state)(struct radeon_device *rdev);
		void (*post_set_power_state)(struct radeon_device *rdev);
		void (*display_configuration_changed)(struct radeon_device *rdev);
		void (*fini)(struct radeon_device *rdev);
		u32 (*get_sclk)(struct radeon_device *rdev, bool low);
		u32 (*get_mclk)(struct radeon_device *rdev, bool low);
		void (*print_power_state)(struct radeon_device *rdev, struct radeon_ps *ps);
		void (*debugfs_print_current_performance_level)(struct radeon_device *rdev, struct seq_file *m);
		int (*force_performance_level)(struct radeon_device *rdev, enum radeon_dpm_forced_level level);
		bool (*vblank_too_short)(struct radeon_device *rdev);
		void (*powergate_uvd)(struct radeon_device *rdev, bool gate);
		void (*enable_bapm)(struct radeon_device *rdev, bool enable);
		void (*fan_ctrl_set_mode)(struct radeon_device *rdev, u32 mode);
		u32 (*fan_ctrl_get_mode)(struct radeon_device *rdev);
		int (*set_fan_speed_percent)(struct radeon_device *rdev, u32 speed);
		int (*get_fan_speed_percent)(struct radeon_device *rdev, u32 *speed);
		u32 (*get_current_sclk)(struct radeon_device *rdev);
		u32 (*get_current_mclk)(struct radeon_device *rdev);
		u16 (*get_current_vddc)(struct radeon_device *rdev);
	} dpm;
	/* pageflipping */
	struct {
		void (*page_flip)(struct radeon_device *rdev, int crtc, u64 crtc_base, bool async);
		bool (*page_flip_pending)(struct radeon_device *rdev, int crtc);
	} pflip;
};

/*
 * Asic structures
 */
struct r100_asic {
	const unsigned		*reg_safe_bm;
	unsigned		reg_safe_bm_size;
	u32			hdp_cntl;
};

struct r300_asic {
	const unsigned		*reg_safe_bm;
	unsigned		reg_safe_bm_size;
	u32			resync_scratch;
	u32			hdp_cntl;
};

struct r600_asic {
	unsigned		max_pipes;
	unsigned		max_tile_pipes;
	unsigned		max_simds;
	unsigned		max_backends;
	unsigned		max_gprs;
	unsigned		max_threads;
	unsigned		max_stack_entries;
	unsigned		max_hw_contexts;
	unsigned		max_gs_threads;
	unsigned		sx_max_export_size;
	unsigned		sx_max_export_pos_size;
	unsigned		sx_max_export_smx_size;
	unsigned		sq_num_cf_insts;
	unsigned		tiling_nbanks;
	unsigned		tiling_npipes;
	unsigned		tiling_group_size;
	unsigned		tile_config;
	unsigned		backend_map;
	unsigned		active_simds;
};

struct rv770_asic {
	unsigned		max_pipes;
	unsigned		max_tile_pipes;
	unsigned		max_simds;
	unsigned		max_backends;
	unsigned		max_gprs;
	unsigned		max_threads;
	unsigned		max_stack_entries;
	unsigned		max_hw_contexts;
	unsigned		max_gs_threads;
	unsigned		sx_max_export_size;
	unsigned		sx_max_export_pos_size;
	unsigned		sx_max_export_smx_size;
	unsigned		sq_num_cf_insts;
	unsigned		sx_num_of_sets;
	unsigned		sc_prim_fifo_size;
	unsigned		sc_hiz_tile_fifo_size;
	unsigned		sc_earlyz_tile_fifo_fize;
	unsigned		tiling_nbanks;
	unsigned		tiling_npipes;
	unsigned		tiling_group_size;
	unsigned		tile_config;
	unsigned		backend_map;
	unsigned		active_simds;
};

struct evergreen_asic {
	unsigned num_ses;
	unsigned max_pipes;
	unsigned max_tile_pipes;
	unsigned max_simds;
	unsigned max_backends;
	unsigned max_gprs;
	unsigned max_threads;
	unsigned max_stack_entries;
	unsigned max_hw_contexts;
	unsigned max_gs_threads;
	unsigned sx_max_export_size;
	unsigned sx_max_export_pos_size;
	unsigned sx_max_export_smx_size;
	unsigned sq_num_cf_insts;
	unsigned sx_num_of_sets;
	unsigned sc_prim_fifo_size;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;
	unsigned tiling_nbanks;
	unsigned tiling_npipes;
	unsigned tiling_group_size;
	unsigned tile_config;
	unsigned backend_map;
	unsigned active_simds;
};

struct cayman_asic {
	unsigned max_shader_engines;
	unsigned max_pipes_per_simd;
	unsigned max_tile_pipes;
	unsigned max_simds_per_se;
	unsigned max_backends_per_se;
	unsigned max_texture_channel_caches;
	unsigned max_gprs;
	unsigned max_threads;
	unsigned max_gs_threads;
	unsigned max_stack_entries;
	unsigned sx_num_of_sets;
	unsigned sx_max_export_size;
	unsigned sx_max_export_pos_size;
	unsigned sx_max_export_smx_size;
	unsigned max_hw_contexts;
	unsigned sq_num_cf_insts;
	unsigned sc_prim_fifo_size;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;

	unsigned num_shader_engines;
	unsigned num_shader_pipes_per_simd;
	unsigned num_tile_pipes;
	unsigned num_simds_per_se;
	unsigned num_backends_per_se;
	unsigned backend_disable_mask_per_asic;
	unsigned backend_map;
	unsigned num_texture_channel_caches;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;

	unsigned tile_config;
	unsigned active_simds;
};

struct si_asic {
	unsigned max_shader_engines;
	unsigned max_tile_pipes;
	unsigned max_cu_per_sh;
	unsigned max_sh_per_se;
	unsigned max_backends_per_se;
	unsigned max_texture_channel_caches;
	unsigned max_gprs;
	unsigned max_gs_threads;
	unsigned max_hw_contexts;
	unsigned sc_prim_fifo_size_frontend;
	unsigned sc_prim_fifo_size_backend;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;

	unsigned num_tile_pipes;
	unsigned backend_enable_mask;
	unsigned backend_disable_mask_per_asic;
	unsigned backend_map;
	unsigned num_texture_channel_caches;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;

	unsigned tile_config;
	uint32_t tile_mode_array[32];
	uint32_t active_cus;
};

struct cik_asic {
	unsigned max_shader_engines;
	unsigned max_tile_pipes;
	unsigned max_cu_per_sh;
	unsigned max_sh_per_se;
	unsigned max_backends_per_se;
	unsigned max_texture_channel_caches;
	unsigned max_gprs;
	unsigned max_gs_threads;
	unsigned max_hw_contexts;
	unsigned sc_prim_fifo_size_frontend;
	unsigned sc_prim_fifo_size_backend;
	unsigned sc_hiz_tile_fifo_size;
	unsigned sc_earlyz_tile_fifo_size;

	unsigned num_tile_pipes;
	unsigned backend_enable_mask;
	unsigned backend_disable_mask_per_asic;
	unsigned backend_map;
	unsigned num_texture_channel_caches;
	unsigned mem_max_burst_length_bytes;
	unsigned mem_row_size_in_kb;
	unsigned shader_engine_tile_size;
	unsigned num_gpus;
	unsigned multi_gpu_tile_size;

	unsigned tile_config;
	uint32_t tile_mode_array[32];
	uint32_t macrotile_mode_array[16];
	uint32_t active_cus;
};

union radeon_asic_config {
	struct r300_asic	r300;
	struct r100_asic	r100;
	struct r600_asic	r600;
	struct rv770_asic	rv770;
	struct evergreen_asic	evergreen;
	struct cayman_asic	cayman;
	struct si_asic		si;
	struct cik_asic		cik;
};

/*
 * asic initizalization from radeon_asic.c
 */
void radeon_agp_disable(struct radeon_device *rdev);
int radeon_asic_init(struct radeon_device *rdev);


/*
 * IOCTL.
 */
int radeon_gem_info_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_gem_create_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *filp);
int radeon_gem_userptr_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *filp);
int radeon_gem_pin_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int radeon_gem_unpin_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int radeon_gem_set_domain_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);
int radeon_gem_mmap_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_gem_busy_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_gem_wait_idle_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *filp);
int radeon_gem_va_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *filp);
int radeon_gem_op_ioctl(struct drm_device *dev, void *data,
			struct drm_file *filp);
int radeon_cs_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
int radeon_gem_set_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);
int radeon_gem_get_tiling_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp);
int radeon_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);

/* VRAM scratch page for HDP bug, default vram page */
struct r600_vram_scratch {
	struct radeon_bo		*robj;
	volatile uint32_t		*ptr;
	u64				gpu_addr;
};

/*
 * ACPI
 */
struct radeon_atif_notification_cfg {
	bool enabled;
	int command_code;
};

struct radeon_atif_notifications {
	bool display_switch;
	bool expansion_mode_change;
	bool thermal_state;
	bool forced_power_state;
	bool system_power_state;
	bool display_conf_change;
	bool px_gfx_switch;
	bool brightness_change;
	bool dgpu_display_event;
};

struct radeon_atif_functions {
	bool system_params;
	bool sbios_requests;
	bool select_active_disp;
	bool lid_state;
	bool get_tv_standard;
	bool set_tv_standard;
	bool get_panel_expansion_mode;
	bool set_panel_expansion_mode;
	bool temperature_change;
	bool graphics_device_types;
};

struct radeon_atif {
	struct radeon_atif_notifications notifications;
	struct radeon_atif_functions functions;
	struct radeon_atif_notification_cfg notification_cfg;
	struct radeon_encoder *encoder_for_bl;
};

struct radeon_atcs_functions {
	bool get_ext_state;
	bool pcie_perf_req;
	bool pcie_dev_rdy;
	bool pcie_bus_width;
};

struct radeon_atcs {
	struct radeon_atcs_functions functions;
};

/*
 * Core structure, functions and helpers.
 */
typedef uint32_t (*radeon_rreg_t)(struct radeon_device*, uint32_t);
typedef void (*radeon_wreg_t)(struct radeon_device*, uint32_t, uint32_t);

struct radeon_device {
	struct device			self;
	struct device			*dev;
	struct drm_device		ddev;
	struct pci_dev			*pdev;
#ifdef __alpha__
	struct pci_controller		*hose;
#endif
	struct radeon_agp_head		*agp;
	struct rwlock			exclusive_lock;

	pci_chipset_tag_t		pc;
	pcitag_t			pa_tag;
	pci_intr_handle_t		intrh;
	bus_space_tag_t			iot;
	bus_space_tag_t			memt;
	bus_dma_tag_t			dmat;
	void				*irqh;

	void				(*switchcb)(void *, int, int);
	void				*switchcbarg;
	void				*switchcookie;
	struct task			switchtask;
	struct rasops_info		ro;
	int				console;
	int				primary;

	struct task			burner_task;
	int				burner_fblank;

#ifdef __sparc64__
	struct sunfb			sf;
	bus_size_t			fb_offset;
	bus_space_handle_t		memh;
#endif

	unsigned long			fb_aper_offset;
	unsigned long			fb_aper_size;

	/* ASIC */
	union radeon_asic_config	config;
	enum radeon_family		family;
	unsigned long			flags;
	int				usec_timeout;
	enum radeon_pll_errata		pll_errata;
	int				num_gb_pipes;
	int				num_z_pipes;
	int				disp_priority;
	/* BIOS */
	uint8_t				*bios;
	bool				is_atom_bios;
	uint16_t			bios_header_start;
	struct radeon_bo		*stolen_vga_memory;
	/* Register mmio */
	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	/* protects concurrent MM_INDEX/DATA based register access */
	spinlock_t mmio_idx_lock;
	/* protects concurrent SMC based register access */
	spinlock_t smc_idx_lock;
	/* protects concurrent PLL register access */
	spinlock_t pll_idx_lock;
	/* protects concurrent MC register access */
	spinlock_t mc_idx_lock;
	/* protects concurrent PCIE register access */
	spinlock_t pcie_idx_lock;
	/* protects concurrent PCIE_PORT register access */
	spinlock_t pciep_idx_lock;
	/* protects concurrent PIF register access */
	spinlock_t pif_idx_lock;
	/* protects concurrent CG register access */
	spinlock_t cg_idx_lock;
	/* protects concurrent UVD register access */
	spinlock_t uvd_idx_lock;
	/* protects concurrent RCU register access */
	spinlock_t rcu_idx_lock;
	/* protects concurrent DIDT register access */
	spinlock_t didt_idx_lock;
	/* protects concurrent ENDPOINT (audio) register access */
	spinlock_t end_idx_lock;
	bus_space_handle_t		rmmio_bsh;
	void __iomem			*rmmio;
	radeon_rreg_t			mc_rreg;
	radeon_wreg_t			mc_wreg;
	radeon_rreg_t			pll_rreg;
	radeon_wreg_t			pll_wreg;
	uint32_t                        pcie_reg_mask;
	radeon_rreg_t			pciep_rreg;
	radeon_wreg_t			pciep_wreg;
	/* io port */
	bus_space_handle_t		rio_mem;
	resource_size_t			rio_mem_size;
	struct radeon_clock             clock;
	struct radeon_mc		mc;
	struct radeon_gart		gart;
	struct radeon_mode_info		mode_info;
	struct radeon_scratch		scratch;
	struct radeon_doorbell		doorbell;
	struct radeon_mman		mman;
	struct radeon_fence_driver	fence_drv[RADEON_NUM_RINGS];
	wait_queue_head_t		fence_queue;
	u64				fence_context;
	struct rwlock			ring_lock;
	struct radeon_ring		ring[RADEON_NUM_RINGS];
	bool				ib_pool_ready;
	struct radeon_sa_manager	ring_tmp_bo;
	struct radeon_irq		irq;
	struct radeon_asic		*asic;
	struct radeon_gem		gem;
	struct radeon_pm		pm;
	struct radeon_uvd		uvd;
	struct radeon_vce		vce;
	uint32_t			bios_scratch[RADEON_BIOS_NUM_SCRATCH];
	struct radeon_wb		wb;
	struct radeon_dummy_page	dummy_page;
	bool				shutdown;
	bool				need_swiotlb;
	bool				accel_working;
	bool				fastfb_working; /* IGP feature*/
	bool				needs_reset, in_reset;
	struct radeon_surface_reg surface_regs[RADEON_GEM_MAX_SURFACES];
	const struct firmware *me_fw;	/* all family ME firmware */
	const struct firmware *pfp_fw;	/* r6/700 PFP firmware */
	const struct firmware *rlc_fw;	/* r6/700 RLC firmware */
	const struct firmware *mc_fw;	/* NI MC firmware */
	const struct firmware *ce_fw;	/* SI CE firmware */
	const struct firmware *mec_fw;	/* CIK MEC firmware */
	const struct firmware *mec2_fw;	/* KV MEC2 firmware */
	const struct firmware *sdma_fw;	/* CIK SDMA firmware */
	const struct firmware *smc_fw;	/* SMC firmware */
	const struct firmware *uvd_fw;	/* UVD firmware */
	const struct firmware *vce_fw;	/* VCE firmware */
	bool new_fw;
	struct r600_vram_scratch vram_scratch;
	int msi_enabled; /* msi enabled */
	struct r600_ih ih; /* r6/700 interrupt ring */
	struct radeon_rlc rlc;
	struct radeon_mec mec;
	struct delayed_work hotplug_work;
	struct work_struct dp_work;
	struct work_struct audio_work;
	int num_crtc; /* number of crtcs */
	struct rwlock dc_hw_i2c_mutex; /* display controller hw i2c mutex */
	bool has_uvd;
	bool has_vce;
	struct r600_audio audio; /* audio stuff */
	struct notifier_block acpi_nb;
	/* only one userspace can use Hyperz features or CMASK at a time */
	struct drm_file *hyperz_filp;
	struct drm_file *cmask_filp;
	/* i2c buses */
	struct radeon_i2c_chan *i2c_bus[RADEON_MAX_I2C_BUS];
	/* virtual memory */
	struct radeon_vm_manager	vm_manager;
	struct rwlock			gpu_clock_mutex;
	/* memory stats */
	atomic64_t			num_bytes_moved;
	atomic_t			gpu_reset_counter;
	/* ACPI interface */
	struct radeon_atif		atif;
	struct radeon_atcs		atcs;
	/* srbm instance registers */
	struct rwlock			srbm_mutex;
	/* clock, powergating flags */
	u32 cg_flags;
	u32 pg_flags;

	struct dev_pm_domain vga_pm_domain;
	bool have_disp_power_ref;
	u32 px_quirk_flags;

	/* tracking pinned memory */
	u64 vram_pin_size;
	u64 gart_pin_size;
};

bool radeon_is_px(struct drm_device *dev);
int radeon_device_init(struct radeon_device *rdev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev,
		       uint32_t flags);
void radeon_device_fini(struct radeon_device *rdev);
int radeon_gpu_wait_for_idle(struct radeon_device *rdev);

#define RADEON_MIN_MMIO_SIZE 0x10000

uint32_t r100_mm_rreg_slow(struct radeon_device *rdev, uint32_t reg);
void r100_mm_wreg_slow(struct radeon_device *rdev, uint32_t reg, uint32_t v);
static inline uint32_t r100_mm_rreg(struct radeon_device *rdev, uint32_t reg,
				    bool always_indirect)
{
	/* The mmio size is 64kb at minimum. Allows the if to be optimized out. */
	if ((reg < rdev->rmmio_size || reg < RADEON_MIN_MMIO_SIZE) && !always_indirect)
		return readl(((void __iomem *)rdev->rmmio) + reg);
	else
		return r100_mm_rreg_slow(rdev, reg);
}
static inline void r100_mm_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v,
				bool always_indirect)
{
	if ((reg < rdev->rmmio_size || reg < RADEON_MIN_MMIO_SIZE) && !always_indirect)
		writel(v, ((void __iomem *)rdev->rmmio) + reg);
	else
		r100_mm_wreg_slow(rdev, reg, v);
}

u32 r100_io_rreg(struct radeon_device *rdev, u32 reg);
void r100_io_wreg(struct radeon_device *rdev, u32 reg, u32 v);

u32 cik_mm_rdoorbell(struct radeon_device *rdev, u32 index);
void cik_mm_wdoorbell(struct radeon_device *rdev, u32 index, u32 v);

static inline struct drm_device *rdev_to_drm(struct radeon_device *rdev)
{
	return &rdev->ddev;
}

/*
 * Cast helper
 */
extern const struct dma_fence_ops radeon_fence_ops;

static inline struct radeon_fence *to_radeon_fence(struct dma_fence *f)
{
	struct radeon_fence *__f = container_of(f, struct radeon_fence, base);

	if (__f->base.ops == &radeon_fence_ops)
		return __f;

	return NULL;
}

/*
 * Registers read & write functions.
 */
#define RREG8(reg) readb((rdev->rmmio) + (reg))
#define WREG8(reg, v) writeb(v, (rdev->rmmio) + (reg))
#define RREG16(reg) readw((rdev->rmmio) + (reg))
#define WREG16(reg, v) writew(v, (rdev->rmmio) + (reg))
#define RREG32(reg) r100_mm_rreg(rdev, (reg), false)
#define RREG32_IDX(reg) r100_mm_rreg(rdev, (reg), true)
#define DREG32(reg) pr_info("REGISTER: " #reg " : 0x%08X\n",	\
			    r100_mm_rreg(rdev, (reg), false))
#define WREG32(reg, v) r100_mm_wreg(rdev, (reg), (v), false)
#define WREG32_IDX(reg, v) r100_mm_wreg(rdev, (reg), (v), true)
#define REG_SET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define REG_GET(FIELD, v) (((v) << FIELD##_SHIFT) & FIELD##_MASK)
#define RREG32_PLL(reg) rdev->pll_rreg(rdev, (reg))
#define WREG32_PLL(reg, v) rdev->pll_wreg(rdev, (reg), (v))
#define RREG32_MC(reg) rdev->mc_rreg(rdev, (reg))
#define WREG32_MC(reg, v) rdev->mc_wreg(rdev, (reg), (v))
#define RREG32_PCIE(reg) rv370_pcie_rreg(rdev, (reg))
#define WREG32_PCIE(reg, v) rv370_pcie_wreg(rdev, (reg), (v))
#define RREG32_PCIE_PORT(reg) rdev->pciep_rreg(rdev, (reg))
#define WREG32_PCIE_PORT(reg, v) rdev->pciep_wreg(rdev, (reg), (v))
#define RREG32_SMC(reg) tn_smc_rreg(rdev, (reg))
#define WREG32_SMC(reg, v) tn_smc_wreg(rdev, (reg), (v))
#define RREG32_RCU(reg) r600_rcu_rreg(rdev, (reg))
#define WREG32_RCU(reg, v) r600_rcu_wreg(rdev, (reg), (v))
#define RREG32_CG(reg) eg_cg_rreg(rdev, (reg))
#define WREG32_CG(reg, v) eg_cg_wreg(rdev, (reg), (v))
#define RREG32_PIF_PHY0(reg) eg_pif_phy0_rreg(rdev, (reg))
#define WREG32_PIF_PHY0(reg, v) eg_pif_phy0_wreg(rdev, (reg), (v))
#define RREG32_PIF_PHY1(reg) eg_pif_phy1_rreg(rdev, (reg))
#define WREG32_PIF_PHY1(reg, v) eg_pif_phy1_wreg(rdev, (reg), (v))
#define RREG32_UVD_CTX(reg) r600_uvd_ctx_rreg(rdev, (reg))
#define WREG32_UVD_CTX(reg, v) r600_uvd_ctx_wreg(rdev, (reg), (v))
#define RREG32_DIDT(reg) cik_didt_rreg(rdev, (reg))
#define WREG32_DIDT(reg, v) cik_didt_wreg(rdev, (reg), (v))
#define WREG32_P(reg, val, mask)				\
	do {							\
		uint32_t tmp_ = RREG32(reg);			\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)
#define WREG32_AND(reg, and) WREG32_P(reg, 0, and)
#define WREG32_OR(reg, or) WREG32_P(reg, or, ~(or))
#define WREG32_PLL_P(reg, val, mask)				\
	do {							\
		uint32_t tmp_ = RREG32_PLL(reg);		\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32_PLL(reg, tmp_);				\
	} while (0)
#define WREG32_SMC_P(reg, val, mask)				\
	do {							\
		uint32_t tmp_ = RREG32_SMC(reg);		\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32_SMC(reg, tmp_);				\
	} while (0)
#define DREG32_SYS(sqf, rdev, reg) seq_printf((sqf), #reg " : 0x%08X\n", r100_mm_rreg((rdev), (reg), false))
#define RREG32_IO(reg) r100_io_rreg(rdev, (reg))
#define WREG32_IO(reg, v) r100_io_wreg(rdev, (reg), (v))

#define RDOORBELL32(index) cik_mm_rdoorbell(rdev, (index))
#define WDOORBELL32(index, v) cik_mm_wdoorbell(rdev, (index), (v))

/*
 * Indirect registers accessors.
 * They used to be inlined, but this increases code size by ~65 kbytes.
 * Since each performs a pair of MMIO ops
 * within a spin_lock_irqsave/spin_unlock_irqrestore region,
 * the cost of call+ret is almost negligible. MMIO and locking
 * costs several dozens of cycles each at best, call+ret is ~5 cycles.
 */
uint32_t rv370_pcie_rreg(struct radeon_device *rdev, uint32_t reg);
void rv370_pcie_wreg(struct radeon_device *rdev, uint32_t reg, uint32_t v);
u32 tn_smc_rreg(struct radeon_device *rdev, u32 reg);
void tn_smc_wreg(struct radeon_device *rdev, u32 reg, u32 v);
u32 r600_rcu_rreg(struct radeon_device *rdev, u32 reg);
void r600_rcu_wreg(struct radeon_device *rdev, u32 reg, u32 v);
u32 eg_cg_rreg(struct radeon_device *rdev, u32 reg);
void eg_cg_wreg(struct radeon_device *rdev, u32 reg, u32 v);
u32 eg_pif_phy0_rreg(struct radeon_device *rdev, u32 reg);
void eg_pif_phy0_wreg(struct radeon_device *rdev, u32 reg, u32 v);
u32 eg_pif_phy1_rreg(struct radeon_device *rdev, u32 reg);
void eg_pif_phy1_wreg(struct radeon_device *rdev, u32 reg, u32 v);
u32 r600_uvd_ctx_rreg(struct radeon_device *rdev, u32 reg);
void r600_uvd_ctx_wreg(struct radeon_device *rdev, u32 reg, u32 v);
u32 cik_didt_rreg(struct radeon_device *rdev, u32 reg);
void cik_didt_wreg(struct radeon_device *rdev, u32 reg, u32 v);

void r100_pll_errata_after_index(struct radeon_device *rdev);


/*
 * ASICs helpers.
 */
#define ASIC_IS_RN50(rdev) ((rdev->pdev->device == 0x515e) || \
			    (rdev->pdev->device == 0x5969))
#define ASIC_IS_RV100(rdev) ((rdev->family == CHIP_RV100) || \
		(rdev->family == CHIP_RV200) || \
		(rdev->family == CHIP_RS100) || \
		(rdev->family == CHIP_RS200) || \
		(rdev->family == CHIP_RV250) || \
		(rdev->family == CHIP_RV280) || \
		(rdev->family == CHIP_RS300))
#define ASIC_IS_R300(rdev) ((rdev->family == CHIP_R300)  ||	\
		(rdev->family == CHIP_RV350) ||			\
		(rdev->family == CHIP_R350)  ||			\
		(rdev->family == CHIP_RV380) ||			\
		(rdev->family == CHIP_R420)  ||			\
		(rdev->family == CHIP_R423)  ||			\
		(rdev->family == CHIP_RV410) ||			\
		(rdev->family == CHIP_RS400) ||			\
		(rdev->family == CHIP_RS480))
#define ASIC_IS_X2(rdev) ((rdev->pdev->device == 0x9441) || \
		(rdev->pdev->device == 0x9443) || \
		(rdev->pdev->device == 0x944B) || \
		(rdev->pdev->device == 0x9506) || \
		(rdev->pdev->device == 0x9509) || \
		(rdev->pdev->device == 0x950F) || \
		(rdev->pdev->device == 0x689C) || \
		(rdev->pdev->device == 0x689D))
#define ASIC_IS_AVIVO(rdev) ((rdev->family >= CHIP_RS600))
#define ASIC_IS_DCE2(rdev) ((rdev->family == CHIP_RS600)  ||	\
			    (rdev->family == CHIP_RS690)  ||	\
			    (rdev->family == CHIP_RS740)  ||	\
			    (rdev->family >= CHIP_R600))
#define ASIC_IS_DCE3(rdev) ((rdev->family >= CHIP_RV620))
#define ASIC_IS_DCE32(rdev) ((rdev->family >= CHIP_RV730))
#define ASIC_IS_DCE4(rdev) ((rdev->family >= CHIP_CEDAR))
#define ASIC_IS_DCE41(rdev) ((rdev->family >= CHIP_PALM) && \
			     (rdev->flags & RADEON_IS_IGP))
#define ASIC_IS_DCE5(rdev) ((rdev->family >= CHIP_BARTS))
#define ASIC_IS_DCE6(rdev) ((rdev->family >= CHIP_ARUBA))
#define ASIC_IS_DCE61(rdev) ((rdev->family >= CHIP_ARUBA) && \
			     (rdev->flags & RADEON_IS_IGP))
#define ASIC_IS_DCE64(rdev) ((rdev->family == CHIP_OLAND))
#define ASIC_IS_NODCE(rdev) ((rdev->family == CHIP_HAINAN))
#define ASIC_IS_DCE8(rdev) ((rdev->family >= CHIP_BONAIRE))
#define ASIC_IS_DCE81(rdev) ((rdev->family == CHIP_KAVERI))
#define ASIC_IS_DCE82(rdev) ((rdev->family == CHIP_BONAIRE))
#define ASIC_IS_DCE83(rdev) ((rdev->family == CHIP_KABINI) || \
			     (rdev->family == CHIP_MULLINS))

#define ASIC_IS_LOMBOK(rdev) ((rdev->pdev->device == 0x6849) || \
			      (rdev->pdev->device == 0x6850) || \
			      (rdev->pdev->device == 0x6858) || \
			      (rdev->pdev->device == 0x6859) || \
			      (rdev->pdev->device == 0x6840) || \
			      (rdev->pdev->device == 0x6841) || \
			      (rdev->pdev->device == 0x6842) || \
			      (rdev->pdev->device == 0x6843))

/*
 * BIOS helpers.
 */
#define RBIOS8(i) (rdev->bios[i])
#define RBIOS16(i) (RBIOS8(i) | (RBIOS8((i)+1) << 8))
#define RBIOS32(i) ((RBIOS16(i)) | (RBIOS16((i)+2) << 16))

int radeon_combios_init(struct radeon_device *rdev);
void radeon_combios_fini(struct radeon_device *rdev);
int radeon_atombios_init(struct radeon_device *rdev);
void radeon_atombios_fini(struct radeon_device *rdev);


/*
 * RING helpers.
 */

/**
 * radeon_ring_write - write a value to the ring
 *
 * @ring: radeon_ring structure holding ring information
 * @v: dword (dw) value to write
 *
 * Write a value to the requested ring buffer (all asics).
 */
static inline void radeon_ring_write(struct radeon_ring *ring, uint32_t v)
{
	if (ring->count_dw <= 0)
		DRM_ERROR("radeon: writing more dwords to the ring than expected!\n");

	ring->ring[ring->wptr++] = v;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw--;
	ring->ring_free_dw--;
}

/*
 * ASICs macro.
 */
#define radeon_init(rdev) (rdev)->asic->init((rdev))
#define radeon_fini(rdev) (rdev)->asic->fini((rdev))
#define radeon_resume(rdev) (rdev)->asic->resume((rdev))
#define radeon_suspend(rdev) (rdev)->asic->suspend((rdev))
#define radeon_cs_parse(rdev, r, p) (rdev)->asic->ring[(r)]->cs_parse((p))
#define radeon_vga_set_state(rdev, state) (rdev)->asic->vga_set_state((rdev), (state))
#define radeon_asic_reset(rdev) (rdev)->asic->asic_reset((rdev), false)
#define radeon_gart_tlb_flush(rdev) (rdev)->asic->gart.tlb_flush((rdev))
#define radeon_gart_get_page_entry(a, f) (rdev)->asic->gart.get_page_entry((a), (f))
#define radeon_gart_set_page(rdev, i, e) (rdev)->asic->gart.set_page((rdev), (i), (e))
#define radeon_asic_vm_init(rdev) (rdev)->asic->vm.init((rdev))
#define radeon_asic_vm_fini(rdev) (rdev)->asic->vm.fini((rdev))
#define radeon_asic_vm_copy_pages(rdev, ib, pe, src, count) ((rdev)->asic->vm.copy_pages((rdev), (ib), (pe), (src), (count)))
#define radeon_asic_vm_write_pages(rdev, ib, pe, addr, count, incr, flags) ((rdev)->asic->vm.write_pages((rdev), (ib), (pe), (addr), (count), (incr), (flags)))
#define radeon_asic_vm_set_pages(rdev, ib, pe, addr, count, incr, flags) ((rdev)->asic->vm.set_pages((rdev), (ib), (pe), (addr), (count), (incr), (flags)))
#define radeon_asic_vm_pad_ib(rdev, ib) ((rdev)->asic->vm.pad_ib((ib)))
#define radeon_ring_start(rdev, r, cp) (rdev)->asic->ring[(r)]->ring_start((rdev), (cp))
#define radeon_ring_test(rdev, r, cp) (rdev)->asic->ring[(r)]->ring_test((rdev), (cp))
#define radeon_ib_test(rdev, r, cp) (rdev)->asic->ring[(r)]->ib_test((rdev), (cp))
#define radeon_ring_ib_execute(rdev, r, ib) (rdev)->asic->ring[(r)]->ib_execute((rdev), (ib))
#define radeon_ring_ib_parse(rdev, r, ib) (rdev)->asic->ring[(r)]->ib_parse((rdev), (ib))
#define radeon_ring_is_lockup(rdev, r, cp) (rdev)->asic->ring[(r)]->is_lockup((rdev), (cp))
#define radeon_ring_vm_flush(rdev, r, vm_id, pd_addr) (rdev)->asic->ring[(r)->idx]->vm_flush((rdev), (r), (vm_id), (pd_addr))
#define radeon_ring_get_rptr(rdev, r) (rdev)->asic->ring[(r)->idx]->get_rptr((rdev), (r))
#define radeon_ring_get_wptr(rdev, r) (rdev)->asic->ring[(r)->idx]->get_wptr((rdev), (r))
#define radeon_ring_set_wptr(rdev, r) (rdev)->asic->ring[(r)->idx]->set_wptr((rdev), (r))
#define radeon_irq_set(rdev) (rdev)->asic->irq.set((rdev))
#define radeon_irq_process(rdev) (rdev)->asic->irq.process((rdev))
#define radeon_get_vblank_counter(rdev, crtc) (rdev)->asic->display.get_vblank_counter((rdev), (crtc))
#define radeon_set_backlight_level(rdev, e, l) (rdev)->asic->display.set_backlight_level((e), (l))
#define radeon_get_backlight_level(rdev, e) (rdev)->asic->display.get_backlight_level((e))
#define radeon_hdmi_enable(rdev, e, b) (rdev)->asic->display.hdmi_enable((e), (b))
#define radeon_hdmi_setmode(rdev, e, m) (rdev)->asic->display.hdmi_setmode((e), (m))
#define radeon_fence_ring_emit(rdev, r, fence) (rdev)->asic->ring[(r)]->emit_fence((rdev), (fence))
#define radeon_semaphore_ring_emit(rdev, r, cp, semaphore, emit_wait) (rdev)->asic->ring[(r)]->emit_semaphore((rdev), (cp), (semaphore), (emit_wait))
#define radeon_copy_blit(rdev, s, d, np, resv) (rdev)->asic->copy.blit((rdev), (s), (d), (np), (resv))
#define radeon_copy_dma(rdev, s, d, np, resv) (rdev)->asic->copy.dma((rdev), (s), (d), (np), (resv))
#define radeon_copy(rdev, s, d, np, resv) (rdev)->asic->copy.copy((rdev), (s), (d), (np), (resv))
#define radeon_copy_blit_ring_index(rdev) (rdev)->asic->copy.blit_ring_index
#define radeon_copy_dma_ring_index(rdev) (rdev)->asic->copy.dma_ring_index
#define radeon_copy_ring_index(rdev) (rdev)->asic->copy.copy_ring_index
#define radeon_get_engine_clock(rdev) (rdev)->asic->pm.get_engine_clock((rdev))
#define radeon_set_engine_clock(rdev, e) (rdev)->asic->pm.set_engine_clock((rdev), (e))
#define radeon_get_memory_clock(rdev) (rdev)->asic->pm.get_memory_clock((rdev))
#define radeon_set_memory_clock(rdev, e) (rdev)->asic->pm.set_memory_clock((rdev), (e))
#define radeon_get_pcie_lanes(rdev) (rdev)->asic->pm.get_pcie_lanes((rdev))
#define radeon_set_pcie_lanes(rdev, l) (rdev)->asic->pm.set_pcie_lanes((rdev), (l))
#define radeon_set_clock_gating(rdev, e) (rdev)->asic->pm.set_clock_gating((rdev), (e))
#define radeon_set_uvd_clocks(rdev, v, d) (rdev)->asic->pm.set_uvd_clocks((rdev), (v), (d))
#define radeon_set_vce_clocks(rdev, ev, ec) (rdev)->asic->pm.set_vce_clocks((rdev), (ev), (ec))
#define radeon_get_temperature(rdev) (rdev)->asic->pm.get_temperature((rdev))
#define radeon_set_surface_reg(rdev, r, f, p, o, s) ((rdev)->asic->surface.set_reg((rdev), (r), (f), (p), (o), (s)))
#define radeon_clear_surface_reg(rdev, r) ((rdev)->asic->surface.clear_reg((rdev), (r)))
#define radeon_bandwidth_update(rdev) (rdev)->asic->display.bandwidth_update((rdev))
#define radeon_hpd_init(rdev) (rdev)->asic->hpd.init((rdev))
#define radeon_hpd_fini(rdev) (rdev)->asic->hpd.fini((rdev))
#define radeon_hpd_sense(rdev, h) (rdev)->asic->hpd.sense((rdev), (h))
#define radeon_hpd_set_polarity(rdev, h) (rdev)->asic->hpd.set_polarity((rdev), (h))
#define radeon_gui_idle(rdev) (rdev)->asic->gui_idle((rdev))
#define radeon_pm_misc(rdev) (rdev)->asic->pm.misc((rdev))
#define radeon_pm_prepare(rdev) (rdev)->asic->pm.prepare((rdev))
#define radeon_pm_finish(rdev) (rdev)->asic->pm.finish((rdev))
#define radeon_pm_init_profile(rdev) (rdev)->asic->pm.init_profile((rdev))
#define radeon_pm_get_dynpm_state(rdev) (rdev)->asic->pm.get_dynpm_state((rdev))
#define radeon_page_flip(rdev, crtc, base, async) (rdev)->asic->pflip.page_flip((rdev), (crtc), (base), (async))
#define radeon_page_flip_pending(rdev, crtc) (rdev)->asic->pflip.page_flip_pending((rdev), (crtc))
#define radeon_wait_for_vblank(rdev, crtc) (rdev)->asic->display.wait_for_vblank((rdev), (crtc))
#define radeon_mc_wait_for_idle(rdev) (rdev)->asic->mc_wait_for_idle((rdev))
#define radeon_get_xclk(rdev) (rdev)->asic->get_xclk((rdev))
#define radeon_get_gpu_clock_counter(rdev) (rdev)->asic->get_gpu_clock_counter((rdev))
#define radeon_get_allowed_info_register(rdev, r, v) (rdev)->asic->get_allowed_info_register((rdev), (r), (v))
#define radeon_dpm_init(rdev) rdev->asic->dpm.init((rdev))
#define radeon_dpm_setup_asic(rdev) rdev->asic->dpm.setup_asic((rdev))
#define radeon_dpm_enable(rdev) rdev->asic->dpm.enable((rdev))
#define radeon_dpm_late_enable(rdev) rdev->asic->dpm.late_enable((rdev))
#define radeon_dpm_disable(rdev) rdev->asic->dpm.disable((rdev))
#define radeon_dpm_pre_set_power_state(rdev) rdev->asic->dpm.pre_set_power_state((rdev))
#define radeon_dpm_set_power_state(rdev) rdev->asic->dpm.set_power_state((rdev))
#define radeon_dpm_post_set_power_state(rdev) rdev->asic->dpm.post_set_power_state((rdev))
#define radeon_dpm_display_configuration_changed(rdev) rdev->asic->dpm.display_configuration_changed((rdev))
#define radeon_dpm_fini(rdev) rdev->asic->dpm.fini((rdev))
#define radeon_dpm_get_sclk(rdev, l) rdev->asic->dpm.get_sclk((rdev), (l))
#define radeon_dpm_get_mclk(rdev, l) rdev->asic->dpm.get_mclk((rdev), (l))
#define radeon_dpm_print_power_state(rdev, ps) rdev->asic->dpm.print_power_state((rdev), (ps))
#define radeon_dpm_debugfs_print_current_performance_level(rdev, m) rdev->asic->dpm.debugfs_print_current_performance_level((rdev), (m))
#define radeon_dpm_force_performance_level(rdev, l) rdev->asic->dpm.force_performance_level((rdev), (l))
#define radeon_dpm_vblank_too_short(rdev) rdev->asic->dpm.vblank_too_short((rdev))
#define radeon_dpm_powergate_uvd(rdev, g) rdev->asic->dpm.powergate_uvd((rdev), (g))
#define radeon_dpm_enable_bapm(rdev, e) rdev->asic->dpm.enable_bapm((rdev), (e))
#define radeon_dpm_get_current_sclk(rdev) rdev->asic->dpm.get_current_sclk((rdev))
#define radeon_dpm_get_current_mclk(rdev) rdev->asic->dpm.get_current_mclk((rdev))

/* Common functions */
/* AGP */
extern int radeon_gpu_reset(struct radeon_device *rdev);
extern void radeon_pci_config_reset(struct radeon_device *rdev);
extern void r600_set_bios_scratch_engine_hung(struct radeon_device *rdev, bool hung);
extern void radeon_agp_disable(struct radeon_device *rdev);
extern int radeon_modeset_init(struct radeon_device *rdev);
extern void radeon_modeset_fini(struct radeon_device *rdev);
extern bool radeon_card_posted(struct radeon_device *rdev);
extern void radeon_update_bandwidth_info(struct radeon_device *rdev);
extern void radeon_update_display_priority(struct radeon_device *rdev);
extern bool radeon_boot_test_post_card(struct radeon_device *rdev);
extern void radeon_scratch_init(struct radeon_device *rdev);
extern void radeon_wb_fini(struct radeon_device *rdev);
extern int radeon_wb_init(struct radeon_device *rdev);
extern void radeon_wb_disable(struct radeon_device *rdev);
extern void radeon_surface_init(struct radeon_device *rdev);
extern int radeon_cs_parser_init(struct radeon_cs_parser *p, void *data);
extern void radeon_legacy_set_clock_gating(struct radeon_device *rdev, int enable);
extern void radeon_atom_set_clock_gating(struct radeon_device *rdev, int enable);
extern void radeon_ttm_placement_from_domain(struct radeon_bo *rbo, u32 domain);
extern bool radeon_ttm_bo_is_radeon_bo(struct ttm_buffer_object *bo);
extern int radeon_ttm_tt_set_userptr(struct radeon_device *rdev,
				     struct ttm_tt *ttm, uint64_t addr,
				     uint32_t flags);
extern bool radeon_ttm_tt_has_userptr(struct radeon_device *rdev, struct ttm_tt *ttm);
extern bool radeon_ttm_tt_is_readonly(struct radeon_device *rdev, struct ttm_tt *ttm);
bool radeon_ttm_tt_is_bound(struct ttm_device *bdev, struct ttm_tt *ttm);
extern void radeon_vram_location(struct radeon_device *rdev, struct radeon_mc *mc, u64 base);
extern void radeon_gtt_location(struct radeon_device *rdev, struct radeon_mc *mc);
extern int radeon_resume_kms(struct drm_device *dev, bool resume, bool fbcon);
extern int radeon_suspend_kms(struct drm_device *dev, bool suspend,
			      bool fbcon, bool freeze);
extern void radeon_ttm_set_active_vram_size(struct radeon_device *rdev, u64 size);
extern void radeon_program_register_sequence(struct radeon_device *rdev,
					     const u32 *registers,
					     const u32 array_size);
struct radeon_device *radeon_get_rdev(struct ttm_device *bdev);

/* KMS */

u32 radeon_get_vblank_counter_kms(struct drm_crtc *crtc);
int radeon_enable_vblank_kms(struct drm_crtc *crtc);
void radeon_disable_vblank_kms(struct drm_crtc *crtc);

/*
 * vm
 */
int radeon_vm_manager_init(struct radeon_device *rdev);
void radeon_vm_manager_fini(struct radeon_device *rdev);
int radeon_vm_init(struct radeon_device *rdev, struct radeon_vm *vm);
void radeon_vm_fini(struct radeon_device *rdev, struct radeon_vm *vm);
struct radeon_bo_list *radeon_vm_get_bos(struct radeon_device *rdev,
					  struct radeon_vm *vm,
                                          struct list_head *head);
struct radeon_fence *radeon_vm_grab_id(struct radeon_device *rdev,
				       struct radeon_vm *vm, int ring);
void radeon_vm_flush(struct radeon_device *rdev,
                     struct radeon_vm *vm,
		     int ring, struct radeon_fence *fence);
void radeon_vm_fence(struct radeon_device *rdev,
		     struct radeon_vm *vm,
		     struct radeon_fence *fence);
uint64_t radeon_vm_map_gart(struct radeon_device *rdev, uint64_t addr);
int radeon_vm_update_page_directory(struct radeon_device *rdev,
				    struct radeon_vm *vm);
int radeon_vm_clear_freed(struct radeon_device *rdev,
			  struct radeon_vm *vm);
int radeon_vm_clear_invalids(struct radeon_device *rdev,
			     struct radeon_vm *vm);
int radeon_vm_bo_update(struct radeon_device *rdev,
			struct radeon_bo_va *bo_va,
			struct ttm_resource *mem);
void radeon_vm_bo_invalidate(struct radeon_device *rdev,
			     struct radeon_bo *bo);
struct radeon_bo_va *radeon_vm_bo_find(struct radeon_vm *vm,
				       struct radeon_bo *bo);
struct radeon_bo_va *radeon_vm_bo_add(struct radeon_device *rdev,
				      struct radeon_vm *vm,
				      struct radeon_bo *bo);
int radeon_vm_bo_set_addr(struct radeon_device *rdev,
			  struct radeon_bo_va *bo_va,
			  uint64_t offset,
			  uint32_t flags);
void radeon_vm_bo_rmv(struct radeon_device *rdev,
		      struct radeon_bo_va *bo_va);

/* audio */
void r600_audio_update_hdmi(struct work_struct *work);
struct r600_audio_pin *r600_audio_get_pin(struct radeon_device *rdev);
struct r600_audio_pin *dce6_audio_get_pin(struct radeon_device *rdev);
void r600_audio_enable(struct radeon_device *rdev,
		       struct r600_audio_pin *pin,
		       u8 enable_mask);
void dce6_audio_enable(struct radeon_device *rdev,
		       struct r600_audio_pin *pin,
		       u8 enable_mask);

/*
 * R600 vram scratch functions
 */
int r600_vram_scratch_init(struct radeon_device *rdev);
void r600_vram_scratch_fini(struct radeon_device *rdev);

/*
 * r600 cs checking helper
 */
unsigned r600_mip_minify(unsigned size, unsigned level);
bool r600_fmt_is_valid_color(u32 format);
bool r600_fmt_is_valid_texture(u32 format, enum radeon_family family);
int r600_fmt_get_blocksize(u32 format);
int r600_fmt_get_nblocksx(u32 format, u32 w);
int r600_fmt_get_nblocksy(u32 format, u32 h);

/*
 * r600 functions used by radeon_encoder.c
 */
struct radeon_hdmi_acr {
	u32 clock;

	int n_32khz;
	int cts_32khz;

	int n_44_1khz;
	int cts_44_1khz;

	int n_48khz;
	int cts_48khz;

};

extern u32 r6xx_remap_render_backend(struct radeon_device *rdev,
				     u32 tiling_pipe_num,
				     u32 max_rb_num,
				     u32 total_max_rb_num,
				     u32 enabled_rb_mask);

/*
 * evergreen functions used by radeon_encoder.c
 */

extern int ni_init_microcode(struct radeon_device *rdev);
extern int ni_mc_load_microcode(struct radeon_device *rdev);

/* radeon_acpi.c */
#if defined(CONFIG_ACPI)
extern int radeon_acpi_init(struct radeon_device *rdev);
extern void radeon_acpi_fini(struct radeon_device *rdev);
extern bool radeon_acpi_is_pcie_performance_request_supported(struct radeon_device *rdev);
extern int radeon_acpi_pcie_performance_request(struct radeon_device *rdev,
						u8 perf_req, bool advertise);
extern int radeon_acpi_pcie_notify_device_ready(struct radeon_device *rdev);
#else
static inline int radeon_acpi_init(struct radeon_device *rdev) { return 0; }
static inline void radeon_acpi_fini(struct radeon_device *rdev) { }
#endif

int radeon_cs_packet_parse(struct radeon_cs_parser *p,
			   struct radeon_cs_packet *pkt,
			   unsigned idx);
bool radeon_cs_packet_next_is_pkt3_nop(struct radeon_cs_parser *p);
void radeon_cs_dump_packet(struct radeon_cs_parser *p,
			   struct radeon_cs_packet *pkt);
int radeon_cs_packet_next_reloc(struct radeon_cs_parser *p,
				struct radeon_bo_list **cs_reloc,
				int nomm);
int r600_cs_common_vline_parse(struct radeon_cs_parser *p,
			       uint32_t *vline_start_end,
			       uint32_t *vline_status);

/* interrupt control register helpers */
void radeon_irq_kms_set_irq_n_enabled(struct radeon_device *rdev,
				      u32 reg, u32 mask,
				      bool enable, const char *name,
				      unsigned n);

/* Audio component binding */
void radeon_audio_component_init(struct radeon_device *rdev);
void radeon_audio_component_fini(struct radeon_device *rdev);

#include "radeon_object.h"

#endif
