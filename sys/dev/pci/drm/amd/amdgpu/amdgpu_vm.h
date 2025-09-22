/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Authors: Christian König
 */
#ifndef __AMDGPU_VM_H__
#define __AMDGPU_VM_H__

#include <linux/idr.h>
#include <linux/kfifo.h>
#include <linux/rbtree.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_file.h>
#include <drm/ttm/ttm_bo.h>
#include <linux/sched/mm.h>

#include "amdgpu_sync.h"
#include "amdgpu_ring.h"
#include "amdgpu_ids.h"

struct drm_exec;

struct amdgpu_bo_va;
struct amdgpu_job;
struct amdgpu_bo_list_entry;
struct amdgpu_bo_vm;
struct amdgpu_mem_stats;

/*
 * GPUVM handling
 */

/* Maximum number of PTEs the hardware can write with one command */
#define AMDGPU_VM_MAX_UPDATE_SIZE	0x3FFFF

/* number of entries in page table */
#define AMDGPU_VM_PTE_COUNT(adev) (1 << (adev)->vm_manager.block_size)

#define AMDGPU_PTE_VALID	(1ULL << 0)
#define AMDGPU_PTE_SYSTEM	(1ULL << 1)
#define AMDGPU_PTE_SNOOPED	(1ULL << 2)

/* RV+ */
#define AMDGPU_PTE_TMZ		(1ULL << 3)

/* VI only */
#define AMDGPU_PTE_EXECUTABLE	(1ULL << 4)

#define AMDGPU_PTE_READABLE	(1ULL << 5)
#define AMDGPU_PTE_WRITEABLE	(1ULL << 6)

#define AMDGPU_PTE_FRAG(x)	((x & 0x1fULL) << 7)

/* TILED for VEGA10, reserved for older ASICs  */
#define AMDGPU_PTE_PRT		(1ULL << 51)

/* PDE is handled as PTE for VEGA10 */
#define AMDGPU_PDE_PTE		(1ULL << 54)

#define AMDGPU_PTE_LOG          (1ULL << 55)

/* PTE is handled as PDE for VEGA10 (Translate Further) */
#define AMDGPU_PTE_TF		(1ULL << 56)

/* MALL noalloc for sienna_cichlid, reserved for older ASICs  */
#define AMDGPU_PTE_NOALLOC	(1ULL << 58)

/* PDE Block Fragment Size for VEGA10 */
#define AMDGPU_PDE_BFS(a)	((uint64_t)a << 59)

/* Flag combination to set no-retry with TF disabled */
#define AMDGPU_VM_NORETRY_FLAGS	(AMDGPU_PTE_EXECUTABLE | AMDGPU_PDE_PTE | \
				AMDGPU_PTE_TF)

/* Flag combination to set no-retry with TF enabled */
#define AMDGPU_VM_NORETRY_FLAGS_TF (AMDGPU_PTE_VALID | AMDGPU_PTE_SYSTEM | \
				   AMDGPU_PTE_PRT)
/* For GFX9 */
#define AMDGPU_PTE_MTYPE_VG10_SHIFT(mtype)	((uint64_t)(mtype) << 57)
#define AMDGPU_PTE_MTYPE_VG10_MASK	AMDGPU_PTE_MTYPE_VG10_SHIFT(3ULL)
#define AMDGPU_PTE_MTYPE_VG10(flags, mtype)			\
	(((uint64_t)(flags) & (~AMDGPU_PTE_MTYPE_VG10_MASK)) |	\
	  AMDGPU_PTE_MTYPE_VG10_SHIFT(mtype))

#define AMDGPU_MTYPE_NC 0
#define AMDGPU_MTYPE_CC 2

#define AMDGPU_PTE_DEFAULT_ATC  (AMDGPU_PTE_SYSTEM      \
                                | AMDGPU_PTE_SNOOPED    \
                                | AMDGPU_PTE_EXECUTABLE \
                                | AMDGPU_PTE_READABLE   \
                                | AMDGPU_PTE_WRITEABLE  \
                                | AMDGPU_PTE_MTYPE_VG10(AMDGPU_MTYPE_CC))

/* gfx10 */
#define AMDGPU_PTE_MTYPE_NV10_SHIFT(mtype)	((uint64_t)(mtype) << 48)
#define AMDGPU_PTE_MTYPE_NV10_MASK     AMDGPU_PTE_MTYPE_NV10_SHIFT(7ULL)
#define AMDGPU_PTE_MTYPE_NV10(flags, mtype)			\
	(((uint64_t)(flags) & (~AMDGPU_PTE_MTYPE_NV10_MASK)) |	\
	  AMDGPU_PTE_MTYPE_NV10_SHIFT(mtype))

/* gfx12 */
#define AMDGPU_PTE_PRT_GFX12		(1ULL << 56)
#define AMDGPU_PTE_PRT_FLAG(adev)	\
	((amdgpu_ip_version((adev), GC_HWIP, 0) >= IP_VERSION(12, 0, 0)) ? AMDGPU_PTE_PRT_GFX12 : AMDGPU_PTE_PRT)

#define AMDGPU_PTE_MTYPE_GFX12_SHIFT(mtype)	((uint64_t)(mtype) << 54)
#define AMDGPU_PTE_MTYPE_GFX12_MASK	AMDGPU_PTE_MTYPE_GFX12_SHIFT(3ULL)
#define AMDGPU_PTE_MTYPE_GFX12(flags, mtype)				\
	(((uint64_t)(flags) & (~AMDGPU_PTE_MTYPE_GFX12_MASK)) |	\
	  AMDGPU_PTE_MTYPE_GFX12_SHIFT(mtype))

#define AMDGPU_PTE_DCC			(1ULL << 58)
#define AMDGPU_PTE_IS_PTE		(1ULL << 63)

/* PDE Block Fragment Size for gfx v12 */
#define AMDGPU_PDE_BFS_GFX12(a)		((uint64_t)((a) & 0x1fULL) << 58)
#define AMDGPU_PDE_BFS_FLAG(adev, a)	\
	((amdgpu_ip_version((adev), GC_HWIP, 0) >= IP_VERSION(12, 0, 0)) ? AMDGPU_PDE_BFS_GFX12(a) : AMDGPU_PDE_BFS(a))
/* PDE is handled as PTE for gfx v12 */
#define AMDGPU_PDE_PTE_GFX12		(1ULL << 63)
#define AMDGPU_PDE_PTE_FLAG(adev)	\
	((amdgpu_ip_version((adev), GC_HWIP, 0) >= IP_VERSION(12, 0, 0)) ? AMDGPU_PDE_PTE_GFX12 : AMDGPU_PDE_PTE)

/* How to program VM fault handling */
#define AMDGPU_VM_FAULT_STOP_NEVER	0
#define AMDGPU_VM_FAULT_STOP_FIRST	1
#define AMDGPU_VM_FAULT_STOP_ALWAYS	2

/* How much VRAM be reserved for page tables */
#define AMDGPU_VM_RESERVED_VRAM		(8ULL << 20)

/*
 * max number of VMHUB
 * layout: max 8 GFXHUB + 4 MMHUB0 + 1 MMHUB1
 */
#define AMDGPU_MAX_VMHUBS			13
#define AMDGPU_GFXHUB_START			0
#define AMDGPU_MMHUB0_START			8
#define AMDGPU_MMHUB1_START			12
#define AMDGPU_GFXHUB(x)			(AMDGPU_GFXHUB_START + (x))
#define AMDGPU_MMHUB0(x)			(AMDGPU_MMHUB0_START + (x))
#define AMDGPU_MMHUB1(x)			(AMDGPU_MMHUB1_START + (x))

#define AMDGPU_IS_GFXHUB(x) ((x) >= AMDGPU_GFXHUB_START && (x) < AMDGPU_MMHUB0_START)
#define AMDGPU_IS_MMHUB0(x) ((x) >= AMDGPU_MMHUB0_START && (x) < AMDGPU_MMHUB1_START)
#define AMDGPU_IS_MMHUB1(x) ((x) >= AMDGPU_MMHUB1_START && (x) < AMDGPU_MAX_VMHUBS)

/* Reserve space at top/bottom of address space for kernel use */
#define AMDGPU_VA_RESERVED_CSA_SIZE		(2ULL << 20)
#define AMDGPU_VA_RESERVED_CSA_START(adev)	(((adev)->vm_manager.max_pfn \
						  << AMDGPU_GPU_PAGE_SHIFT)  \
						 - AMDGPU_VA_RESERVED_CSA_SIZE)
#define AMDGPU_VA_RESERVED_SEQ64_SIZE		(2ULL << 20)
#define AMDGPU_VA_RESERVED_SEQ64_START(adev)	(AMDGPU_VA_RESERVED_CSA_START(adev) \
						 - AMDGPU_VA_RESERVED_SEQ64_SIZE)
#define AMDGPU_VA_RESERVED_TRAP_SIZE		(2ULL << 12)
#define AMDGPU_VA_RESERVED_TRAP_START(adev)	(AMDGPU_VA_RESERVED_SEQ64_START(adev) \
						 - AMDGPU_VA_RESERVED_TRAP_SIZE)
#define AMDGPU_VA_RESERVED_BOTTOM		(1ULL << 16)
#define AMDGPU_VA_RESERVED_TOP			(AMDGPU_VA_RESERVED_TRAP_SIZE + \
						 AMDGPU_VA_RESERVED_SEQ64_SIZE + \
						 AMDGPU_VA_RESERVED_CSA_SIZE)

/* See vm_update_mode */
#define AMDGPU_VM_USE_CPU_FOR_GFX (1 << 0)
#define AMDGPU_VM_USE_CPU_FOR_COMPUTE (1 << 1)

/* VMPT level enumerate, and the hiberachy is:
 * PDB2->PDB1->PDB0->PTB
 */
enum amdgpu_vm_level {
	AMDGPU_VM_PDB2,
	AMDGPU_VM_PDB1,
	AMDGPU_VM_PDB0,
	AMDGPU_VM_PTB
};

/* base structure for tracking BO usage in a VM */
struct amdgpu_vm_bo_base {
	/* constant after initialization */
	struct amdgpu_vm		*vm;
	struct amdgpu_bo		*bo;

	/* protected by bo being reserved */
	struct amdgpu_vm_bo_base	*next;

	/* protected by spinlock */
	struct list_head		vm_status;

	/* protected by the BO being reserved */
	bool				moved;
};

/* provided by hw blocks that can write ptes, e.g., sdma */
struct amdgpu_vm_pte_funcs {
	/* number of dw to reserve per operation */
	unsigned	copy_pte_num_dw;

	/* copy pte entries from GART */
	void (*copy_pte)(struct amdgpu_ib *ib,
			 uint64_t pe, uint64_t src,
			 unsigned count);

	/* write pte one entry at a time with addr mapping */
	void (*write_pte)(struct amdgpu_ib *ib, uint64_t pe,
			  uint64_t value, unsigned count,
			  uint32_t incr);
	/* for linear pte/pde updates without addr mapping */
	void (*set_pte_pde)(struct amdgpu_ib *ib,
			    uint64_t pe,
			    uint64_t addr, unsigned count,
			    uint32_t incr, uint64_t flags);
};

struct amdgpu_task_info {
	char		process_name[TASK_COMM_LEN];
	char		task_name[TASK_COMM_LEN];
	pid_t		pid;
	pid_t		tgid;
	struct kref	refcount;
};

/**
 * struct amdgpu_vm_update_params
 *
 * Encapsulate some VM table update parameters to reduce
 * the number of function parameters
 *
 */
struct amdgpu_vm_update_params {

	/**
	 * @adev: amdgpu device we do this update for
	 */
	struct amdgpu_device *adev;

	/**
	 * @vm: optional amdgpu_vm we do this update for
	 */
	struct amdgpu_vm *vm;

	/**
	 * @immediate: if changes should be made immediately
	 */
	bool immediate;

	/**
	 * @unlocked: true if the root BO is not locked
	 */
	bool unlocked;

	/**
	 * @pages_addr:
	 *
	 * DMA addresses to use for mapping
	 */
	dma_addr_t *pages_addr;

	/**
	 * @job: job to used for hw submission
	 */
	struct amdgpu_job *job;

	/**
	 * @num_dw_left: number of dw left for the IB
	 */
	unsigned int num_dw_left;

	/**
	 * @needs_flush: true whenever we need to invalidate the TLB
	 */
	bool needs_flush;

	/**
	 * @allow_override: true for memory that is not uncached: allows MTYPE
	 * to be overridden for NUMA local memory.
	 */
	bool allow_override;

	/**
	 * @tlb_flush_waitlist: temporary storage for BOs until tlb_flush
	 */
	struct list_head tlb_flush_waitlist;
};

struct amdgpu_vm_update_funcs {
	int (*map_table)(struct amdgpu_bo_vm *bo);
	int (*prepare)(struct amdgpu_vm_update_params *p,
		       struct amdgpu_sync *sync);
	int (*update)(struct amdgpu_vm_update_params *p,
		      struct amdgpu_bo_vm *bo, uint64_t pe, uint64_t addr,
		      unsigned count, uint32_t incr, uint64_t flags);
	int (*commit)(struct amdgpu_vm_update_params *p,
		      struct dma_fence **fence);
};

struct amdgpu_vm_fault_info {
	/* fault address */
	uint64_t	addr;
	/* fault status register */
	uint32_t	status;
	/* which vmhub? gfxhub, mmhub, etc. */
	unsigned int	vmhub;
};

struct amdgpu_vm_fault {
	SIMPLEQ_ENTRY(amdgpu_vm_fault)	vm_fault_entry;
	uint64_t			val;
};
SIMPLEQ_HEAD(amdgpu_vm_faults, amdgpu_vm_fault);

struct amdgpu_vm {
	/* tree of virtual addresses mapped */
	struct rb_root_cached	va;

	/* Lock to prevent eviction while we are updating page tables
	 * use vm_eviction_lock/unlock(vm)
	 */
	struct rwlock		eviction_lock;
	bool			evicting;
	unsigned int		saved_flags;

	/* Lock to protect vm_bo add/del/move on all lists of vm */
	spinlock_t		status_lock;

	/* Per-VM and PT BOs who needs a validation */
	struct list_head	evicted;

	/* BOs for user mode queues that need a validation */
	struct list_head	evicted_user;

	/* PT BOs which relocated and their parent need an update */
	struct list_head	relocated;

	/* per VM BOs moved, but not yet updated in the PT */
	struct list_head	moved;

	/* All BOs of this VM not currently in the state machine */
	struct list_head	idle;

	/* regular invalidated BOs, but not yet updated in the PT */
	struct list_head	invalidated;

	/* BO mappings freed, but not yet updated in the PT */
	struct list_head	freed;

	/* BOs which are invalidated, has been updated in the PTs */
	struct list_head        done;

	/* contains the page directory */
	struct amdgpu_vm_bo_base     root;
	struct dma_fence	*last_update;

	/* Scheduler entities for page table updates */
	struct drm_sched_entity	immediate;
	struct drm_sched_entity	delayed;

	/* Last finished delayed update */
	atomic64_t		tlb_seq;
	struct dma_fence	*last_tlb_flush;
	atomic64_t		kfd_last_flushed_seq;
	uint64_t		tlb_fence_context;

	/* How many times we had to re-generate the page tables */
	uint64_t		generation;

	/* Last unlocked submission to the scheduler entities */
	struct dma_fence	*last_unlocked;

	unsigned int		pasid;
	bool			reserved_vmid[AMDGPU_MAX_VMHUBS];

	/* Flag to indicate if VM tables are updated by CPU or GPU (SDMA) */
	bool					use_cpu_for_update;

	/* Functions to use for VM table updates */
	const struct amdgpu_vm_update_funcs	*update_funcs;

#ifdef __linux__
	/* Up to 128 pending retry page faults */
	DECLARE_KFIFO(faults, u64, 128);
#else
	struct amdgpu_vm_faults faults;
#endif

	/* Points to the KFD process VM info */
	struct amdkfd_process_info *process_info;

	/* List node in amdkfd_process_info.vm_list_head */
	struct list_head	vm_list_node;

	/* Valid while the PD is reserved or fenced */
	uint64_t		pd_phys_addr;

	/* Some basic info about the task */
	struct amdgpu_task_info *task_info;

	/* Store positions of group of BOs */
	struct ttm_lru_bulk_move lru_bulk_move;
	/* Flag to indicate if VM is used for compute */
	bool			is_compute_context;

	/* Memory partition number, -1 means any partition */
	int8_t			mem_id;

	/* cached fault info */
	struct amdgpu_vm_fault_info fault_info;
};

struct amdgpu_vm_manager {
	/* Handling of VMIDs */
	struct amdgpu_vmid_mgr			id_mgr[AMDGPU_MAX_VMHUBS];
	unsigned int				first_kfd_vmid;
	bool					concurrent_flush;

	/* Handling of VM fences */
	u64					fence_context;
	unsigned				seqno[AMDGPU_MAX_RINGS];

	uint64_t				max_pfn;
	uint32_t				num_level;
	uint32_t				block_size;
	uint32_t				fragment_size;
	enum amdgpu_vm_level			root_level;
	/* vram base address for page table entry  */
	u64					vram_base_offset;
	/* vm pte handling */
	const struct amdgpu_vm_pte_funcs	*vm_pte_funcs;
	struct drm_gpu_scheduler		*vm_pte_scheds[AMDGPU_MAX_RINGS];
	unsigned				vm_pte_num_scheds;
	struct amdgpu_ring			*page_fault;

	/* partial resident texture handling */
	spinlock_t				prt_lock;
	atomic_t				num_prt_users;

	/* controls how VM page tables are updated for Graphics and Compute.
	 * BIT0[= 0] Graphics updated by SDMA [= 1] by CPU
	 * BIT1[= 0] Compute updated by SDMA [= 1] by CPU
	 */
	int					vm_update_mode;

	/* PASID to VM mapping, will be used in interrupt context to
	 * look up VM of a page fault
	 */
	struct xarray				pasids;
	/* Global registration of recent page fault information */
	struct amdgpu_vm_fault_info	fault_info;
};

struct amdgpu_bo_va_mapping;

#define amdgpu_vm_copy_pte(adev, ib, pe, src, count) ((adev)->vm_manager.vm_pte_funcs->copy_pte((ib), (pe), (src), (count)))
#define amdgpu_vm_write_pte(adev, ib, pe, value, count, incr) ((adev)->vm_manager.vm_pte_funcs->write_pte((ib), (pe), (value), (count), (incr)))
#define amdgpu_vm_set_pte_pde(adev, ib, pe, addr, count, incr, flags) ((adev)->vm_manager.vm_pte_funcs->set_pte_pde((ib), (pe), (addr), (count), (incr), (flags)))

extern const struct amdgpu_vm_update_funcs amdgpu_vm_cpu_funcs;
extern const struct amdgpu_vm_update_funcs amdgpu_vm_sdma_funcs;

void amdgpu_vm_manager_init(struct amdgpu_device *adev);
void amdgpu_vm_manager_fini(struct amdgpu_device *adev);

int amdgpu_vm_set_pasid(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			u32 pasid);

long amdgpu_vm_wait_idle(struct amdgpu_vm *vm, long timeout);
int amdgpu_vm_init(struct amdgpu_device *adev, struct amdgpu_vm *vm, int32_t xcp_id);
int amdgpu_vm_make_compute(struct amdgpu_device *adev, struct amdgpu_vm *vm);
void amdgpu_vm_release_compute(struct amdgpu_device *adev, struct amdgpu_vm *vm);
void amdgpu_vm_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm);
int amdgpu_vm_lock_pd(struct amdgpu_vm *vm, struct drm_exec *exec,
		      unsigned int num_fences);
bool amdgpu_vm_ready(struct amdgpu_vm *vm);
uint64_t amdgpu_vm_generation(struct amdgpu_device *adev, struct amdgpu_vm *vm);
int amdgpu_vm_validate(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		       struct ww_acquire_ctx *ticket,
		       int (*callback)(void *p, struct amdgpu_bo *bo),
		       void *param);
int amdgpu_vm_flush(struct amdgpu_ring *ring, struct amdgpu_job *job, bool need_pipe_sync);
int amdgpu_vm_update_pdes(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm, bool immediate);
int amdgpu_vm_clear_freed(struct amdgpu_device *adev,
			  struct amdgpu_vm *vm,
			  struct dma_fence **fence);
int amdgpu_vm_handle_moved(struct amdgpu_device *adev,
			   struct amdgpu_vm *vm,
			   struct ww_acquire_ctx *ticket);
int amdgpu_vm_flush_compute_tlb(struct amdgpu_device *adev,
				struct amdgpu_vm *vm,
				uint32_t flush_type,
				uint32_t xcc_mask);
void amdgpu_vm_bo_base_init(struct amdgpu_vm_bo_base *base,
			    struct amdgpu_vm *vm, struct amdgpu_bo *bo);
int amdgpu_vm_update_range(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			   bool immediate, bool unlocked, bool flush_tlb,
			   bool allow_override, struct amdgpu_sync *sync,
			   uint64_t start, uint64_t last, uint64_t flags,
			   uint64_t offset, uint64_t vram_base,
			   struct ttm_resource *res, dma_addr_t *pages_addr,
			   struct dma_fence **fence);
int amdgpu_vm_bo_update(struct amdgpu_device *adev,
			struct amdgpu_bo_va *bo_va,
			bool clear);
bool amdgpu_vm_evictable(struct amdgpu_bo *bo);
void amdgpu_vm_bo_invalidate(struct amdgpu_device *adev,
			     struct amdgpu_bo *bo, bool evicted);
uint64_t amdgpu_vm_map_gart(const dma_addr_t *pages_addr, uint64_t addr);
struct amdgpu_bo_va *amdgpu_vm_bo_find(struct amdgpu_vm *vm,
				       struct amdgpu_bo *bo);
struct amdgpu_bo_va *amdgpu_vm_bo_add(struct amdgpu_device *adev,
				      struct amdgpu_vm *vm,
				      struct amdgpu_bo *bo);
int amdgpu_vm_bo_map(struct amdgpu_device *adev,
		     struct amdgpu_bo_va *bo_va,
		     uint64_t addr, uint64_t offset,
		     uint64_t size, uint64_t flags);
int amdgpu_vm_bo_replace_map(struct amdgpu_device *adev,
			     struct amdgpu_bo_va *bo_va,
			     uint64_t addr, uint64_t offset,
			     uint64_t size, uint64_t flags);
int amdgpu_vm_bo_unmap(struct amdgpu_device *adev,
		       struct amdgpu_bo_va *bo_va,
		       uint64_t addr);
int amdgpu_vm_bo_clear_mappings(struct amdgpu_device *adev,
				struct amdgpu_vm *vm,
				uint64_t saddr, uint64_t size);
struct amdgpu_bo_va_mapping *amdgpu_vm_bo_lookup_mapping(struct amdgpu_vm *vm,
							 uint64_t addr);
void amdgpu_vm_bo_trace_cs(struct amdgpu_vm *vm, struct ww_acquire_ctx *ticket);
void amdgpu_vm_bo_del(struct amdgpu_device *adev,
		      struct amdgpu_bo_va *bo_va);
void amdgpu_vm_adjust_size(struct amdgpu_device *adev, uint32_t min_vm_size,
			   uint32_t fragment_size_default, unsigned max_level,
			   unsigned max_bits);
int amdgpu_vm_ioctl(struct drm_device *dev, void *data, struct drm_file *filp);
bool amdgpu_vm_need_pipeline_sync(struct amdgpu_ring *ring,
				  struct amdgpu_job *job);
void amdgpu_vm_check_compute_bug(struct amdgpu_device *adev);

struct amdgpu_task_info *
amdgpu_vm_get_task_info_pasid(struct amdgpu_device *adev, u32 pasid);

struct amdgpu_task_info *
amdgpu_vm_get_task_info_vm(struct amdgpu_vm *vm);

void amdgpu_vm_put_task_info(struct amdgpu_task_info *task_info);

bool amdgpu_vm_handle_fault(struct amdgpu_device *adev, u32 pasid,
			    u32 vmid, u32 node_id, uint64_t addr, uint64_t ts,
			    bool write_fault);

void amdgpu_vm_set_task_info(struct amdgpu_vm *vm);

void amdgpu_vm_move_to_lru_tail(struct amdgpu_device *adev,
				struct amdgpu_vm *vm);
void amdgpu_vm_get_memory(struct amdgpu_vm *vm,
			  struct amdgpu_mem_stats *stats);

int amdgpu_vm_pt_clear(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		       struct amdgpu_bo_vm *vmbo, bool immediate);
int amdgpu_vm_pt_create(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			int level, bool immediate, struct amdgpu_bo_vm **vmbo,
			int32_t xcp_id);
void amdgpu_vm_pt_free_root(struct amdgpu_device *adev, struct amdgpu_vm *vm);

int amdgpu_vm_pde_update(struct amdgpu_vm_update_params *params,
			 struct amdgpu_vm_bo_base *entry);
int amdgpu_vm_ptes_update(struct amdgpu_vm_update_params *params,
			  uint64_t start, uint64_t end,
			  uint64_t dst, uint64_t flags);
void amdgpu_vm_pt_free_work(struct work_struct *work);
void amdgpu_vm_pt_free_list(struct amdgpu_device *adev,
			    struct amdgpu_vm_update_params *params);

#if defined(CONFIG_DEBUG_FS)
void amdgpu_debugfs_vm_bo_info(struct amdgpu_vm *vm, struct seq_file *m);
#endif

int amdgpu_vm_pt_map_tables(struct amdgpu_device *adev, struct amdgpu_vm *vm);

bool amdgpu_vm_is_bo_always_valid(struct amdgpu_vm *vm, struct amdgpu_bo *bo);

/**
 * amdgpu_vm_tlb_seq - return tlb flush sequence number
 * @vm: the amdgpu_vm structure to query
 *
 * Returns the tlb flush sequence number which indicates that the VM TLBs needs
 * to be invalidated whenever the sequence number change.
 */
static inline uint64_t amdgpu_vm_tlb_seq(struct amdgpu_vm *vm)
{
	unsigned long flags;
	spinlock_t *lock;

	/*
	 * Workaround to stop racing between the fence signaling and handling
	 * the cb. The lock is static after initially setting it up, just make
	 * sure that the dma_fence structure isn't freed up.
	 */
	rcu_read_lock();
	lock = vm->last_tlb_flush->lock;
	rcu_read_unlock();

	spin_lock_irqsave(lock, flags);
	spin_unlock_irqrestore(lock, flags);

	return atomic64_read(&vm->tlb_seq);
}

/*
 * vm eviction_lock can be taken in MMU notifiers. Make sure no reclaim-FS
 * happens while holding this lock anywhere to prevent deadlocks when
 * an MMU notifier runs in reclaim-FS context.
 */
static inline void amdgpu_vm_eviction_lock(struct amdgpu_vm *vm)
{
	mutex_lock(&vm->eviction_lock);
#ifdef notyet
	vm->saved_flags = memalloc_noreclaim_save();
#endif
}

static inline bool amdgpu_vm_eviction_trylock(struct amdgpu_vm *vm)
{
	if (mutex_trylock(&vm->eviction_lock)) {
#ifdef notyet
		vm->saved_flags = memalloc_noreclaim_save();
#endif
		return true;
	}
	return false;
}

static inline void amdgpu_vm_eviction_unlock(struct amdgpu_vm *vm)
{
#ifdef notyet
	memalloc_noreclaim_restore(vm->saved_flags);
#endif
	mutex_unlock(&vm->eviction_lock);
}

void amdgpu_vm_update_fault_cache(struct amdgpu_device *adev,
				  unsigned int pasid,
				  uint64_t addr,
				  uint32_t status,
				  unsigned int vmhub);
void amdgpu_vm_tlb_fence_create(struct amdgpu_device *adev,
				 struct amdgpu_vm *vm,
				 struct dma_fence **fence);

#endif
