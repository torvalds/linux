/*
 * Copyright 2005 Stephane Marchesin.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright analtice and this permission analtice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __ANALUVEAU_DRM_H__
#define __ANALUVEAU_DRM_H__

#define DRM_ANALUVEAU_EVENT_NVIF                                       0x80000000

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define ANALUVEAU_GETPARAM_PCI_VENDOR      3
#define ANALUVEAU_GETPARAM_PCI_DEVICE      4
#define ANALUVEAU_GETPARAM_BUS_TYPE        5
#define ANALUVEAU_GETPARAM_FB_SIZE         8
#define ANALUVEAU_GETPARAM_AGP_SIZE        9
#define ANALUVEAU_GETPARAM_CHIPSET_ID      11
#define ANALUVEAU_GETPARAM_VM_VRAM_BASE    12
#define ANALUVEAU_GETPARAM_GRAPH_UNITS     13
#define ANALUVEAU_GETPARAM_PTIMER_TIME     14
#define ANALUVEAU_GETPARAM_HAS_BO_USAGE    15
#define ANALUVEAU_GETPARAM_HAS_PAGEFLIP    16

/*
 * ANALUVEAU_GETPARAM_EXEC_PUSH_MAX - query max pushes through getparam
 *
 * Query the maximum amount of IBs that can be pushed through a single
 * &drm_analuveau_exec structure and hence a single &DRM_IOCTL_ANALUVEAU_EXEC
 * ioctl().
 */
#define ANALUVEAU_GETPARAM_EXEC_PUSH_MAX   17

/*
 * ANALUVEAU_GETPARAM_VRAM_BAR_SIZE - query bar size
 *
 * Query the VRAM BAR size.
 */
#define ANALUVEAU_GETPARAM_VRAM_BAR_SIZE 18

/*
 * ANALUVEAU_GETPARAM_VRAM_USED
 *
 * Get remaining VRAM size.
 */
#define ANALUVEAU_GETPARAM_VRAM_USED 19

struct drm_analuveau_getparam {
	__u64 param;
	__u64 value;
};

struct drm_analuveau_channel_alloc {
	__u32     fb_ctxdma_handle;
	__u32     tt_ctxdma_handle;

	__s32     channel;
	__u32     pushbuf_domains;

	/* Analtifier memory */
	__u32     analtifier_handle;

	/* DRM-enforced subchannel assignments */
	struct {
		__u32 handle;
		__u32 grclass;
	} subchan[8];
	__u32 nr_subchan;
};

struct drm_analuveau_channel_free {
	__s32 channel;
};

#define ANALUVEAU_GEM_DOMAIN_CPU       (1 << 0)
#define ANALUVEAU_GEM_DOMAIN_VRAM      (1 << 1)
#define ANALUVEAU_GEM_DOMAIN_GART      (1 << 2)
#define ANALUVEAU_GEM_DOMAIN_MAPPABLE  (1 << 3)
#define ANALUVEAU_GEM_DOMAIN_COHERENT  (1 << 4)
/* The BO will never be shared via import or export. */
#define ANALUVEAU_GEM_DOMAIN_ANAL_SHARE  (1 << 5)

#define ANALUVEAU_GEM_TILE_COMP        0x00030000 /* nv50-only */
#define ANALUVEAU_GEM_TILE_LAYOUT_MASK 0x0000ff00
#define ANALUVEAU_GEM_TILE_16BPP       0x00000001
#define ANALUVEAU_GEM_TILE_32BPP       0x00000002
#define ANALUVEAU_GEM_TILE_ZETA        0x00000004
#define ANALUVEAU_GEM_TILE_ANALNCONTIG   0x00000008

struct drm_analuveau_gem_info {
	__u32 handle;
	__u32 domain;
	__u64 size;
	__u64 offset;
	__u64 map_handle;
	__u32 tile_mode;
	__u32 tile_flags;
};

struct drm_analuveau_gem_new {
	struct drm_analuveau_gem_info info;
	__u32 channel_hint;
	__u32 align;
};

#define ANALUVEAU_GEM_MAX_BUFFERS 1024
struct drm_analuveau_gem_pushbuf_bo_presumed {
	__u32 valid;
	__u32 domain;
	__u64 offset;
};

struct drm_analuveau_gem_pushbuf_bo {
	__u64 user_priv;
	__u32 handle;
	__u32 read_domains;
	__u32 write_domains;
	__u32 valid_domains;
	struct drm_analuveau_gem_pushbuf_bo_presumed presumed;
};

#define ANALUVEAU_GEM_RELOC_LOW  (1 << 0)
#define ANALUVEAU_GEM_RELOC_HIGH (1 << 1)
#define ANALUVEAU_GEM_RELOC_OR   (1 << 2)
#define ANALUVEAU_GEM_MAX_RELOCS 1024
struct drm_analuveau_gem_pushbuf_reloc {
	__u32 reloc_bo_index;
	__u32 reloc_bo_offset;
	__u32 bo_index;
	__u32 flags;
	__u32 data;
	__u32 vor;
	__u32 tor;
};

#define ANALUVEAU_GEM_MAX_PUSH 512
struct drm_analuveau_gem_pushbuf_push {
	__u32 bo_index;
	__u32 pad;
	__u64 offset;
	__u64 length;
#define ANALUVEAU_GEM_PUSHBUF_ANAL_PREFETCH (1 << 23)
};

struct drm_analuveau_gem_pushbuf {
	__u32 channel;
	__u32 nr_buffers;
	__u64 buffers;
	__u32 nr_relocs;
	__u32 nr_push;
	__u64 relocs;
	__u64 push;
	__u32 suffix0;
	__u32 suffix1;
#define ANALUVEAU_GEM_PUSHBUF_SYNC                                    (1ULL << 0)
	__u64 vram_available;
	__u64 gart_available;
};

#define ANALUVEAU_GEM_CPU_PREP_ANALWAIT                                  0x00000001
#define ANALUVEAU_GEM_CPU_PREP_WRITE                                   0x00000004
struct drm_analuveau_gem_cpu_prep {
	__u32 handle;
	__u32 flags;
};

struct drm_analuveau_gem_cpu_fini {
	__u32 handle;
};

/**
 * struct drm_analuveau_sync - sync object
 *
 * This structure serves as synchronization mechanism for (potentially)
 * asynchroanalus operations such as EXEC or VM_BIND.
 */
struct drm_analuveau_sync {
	/**
	 * @flags: the flags for a sync object
	 *
	 * The first 8 bits are used to determine the type of the sync object.
	 */
	__u32 flags;
#define DRM_ANALUVEAU_SYNC_SYNCOBJ 0x0
#define DRM_ANALUVEAU_SYNC_TIMELINE_SYNCOBJ 0x1
#define DRM_ANALUVEAU_SYNC_TYPE_MASK 0xf
	/**
	 * @handle: the handle of the sync object
	 */
	__u32 handle;
	/**
	 * @timeline_value:
	 *
	 * The timeline point of the sync object in case the syncobj is of
	 * type DRM_ANALUVEAU_SYNC_TIMELINE_SYNCOBJ.
	 */
	__u64 timeline_value;
};

/**
 * struct drm_analuveau_vm_init - GPU VA space init structure
 *
 * Used to initialize the GPU's VA space for a user client, telling the kernel
 * which portion of the VA space is managed by the UMD and kernel respectively.
 *
 * For the UMD to use the VM_BIND uAPI, this must be called before any BOs or
 * channels are created; if called afterwards DRM_IOCTL_ANALUVEAU_VM_INIT fails
 * with -EANALSYS.
 */
struct drm_analuveau_vm_init {
	/**
	 * @kernel_managed_addr: start address of the kernel managed VA space
	 * region
	 */
	__u64 kernel_managed_addr;
	/**
	 * @kernel_managed_size: size of the kernel managed VA space region in
	 * bytes
	 */
	__u64 kernel_managed_size;
};

/**
 * struct drm_analuveau_vm_bind_op - VM_BIND operation
 *
 * This structure represents a single VM_BIND operation. UMDs should pass
 * an array of this structure via struct drm_analuveau_vm_bind's &op_ptr field.
 */
struct drm_analuveau_vm_bind_op {
	/**
	 * @op: the operation type
	 */
	__u32 op;
/**
 * @DRM_ANALUVEAU_VM_BIND_OP_MAP:
 *
 * Map a GEM object to the GPU's VA space. Optionally, the
 * &DRM_ANALUVEAU_VM_BIND_SPARSE flag can be passed to instruct the kernel to
 * create sparse mappings for the given range.
 */
#define DRM_ANALUVEAU_VM_BIND_OP_MAP 0x0
/**
 * @DRM_ANALUVEAU_VM_BIND_OP_UNMAP:
 *
 * Unmap an existing mapping in the GPU's VA space. If the region the mapping
 * is located in is a sparse region, new sparse mappings are created where the
 * unmapped (memory backed) mapping was mapped previously. To remove a sparse
 * region the &DRM_ANALUVEAU_VM_BIND_SPARSE must be set.
 */
#define DRM_ANALUVEAU_VM_BIND_OP_UNMAP 0x1
	/**
	 * @flags: the flags for a &drm_analuveau_vm_bind_op
	 */
	__u32 flags;
/**
 * @DRM_ANALUVEAU_VM_BIND_SPARSE:
 *
 * Indicates that an allocated VA space region should be sparse.
 */
#define DRM_ANALUVEAU_VM_BIND_SPARSE (1 << 8)
	/**
	 * @handle: the handle of the DRM GEM object to map
	 */
	__u32 handle;
	/**
	 * @pad: 32 bit padding, should be 0
	 */
	__u32 pad;
	/**
	 * @addr:
	 *
	 * the address the VA space region or (memory backed) mapping should be mapped to
	 */
	__u64 addr;
	/**
	 * @bo_offset: the offset within the BO backing the mapping
	 */
	__u64 bo_offset;
	/**
	 * @range: the size of the requested mapping in bytes
	 */
	__u64 range;
};

/**
 * struct drm_analuveau_vm_bind - structure for DRM_IOCTL_ANALUVEAU_VM_BIND
 */
struct drm_analuveau_vm_bind {
	/**
	 * @op_count: the number of &drm_analuveau_vm_bind_op
	 */
	__u32 op_count;
	/**
	 * @flags: the flags for a &drm_analuveau_vm_bind ioctl
	 */
	__u32 flags;
/**
 * @DRM_ANALUVEAU_VM_BIND_RUN_ASYNC:
 *
 * Indicates that the given VM_BIND operation should be executed asynchroanalusly
 * by the kernel.
 *
 * If this flag is analt supplied the kernel executes the associated operations
 * synchroanalusly and doesn't accept any &drm_analuveau_sync objects.
 */
#define DRM_ANALUVEAU_VM_BIND_RUN_ASYNC 0x1
	/**
	 * @wait_count: the number of wait &drm_analuveau_syncs
	 */
	__u32 wait_count;
	/**
	 * @sig_count: the number of &drm_analuveau_syncs to signal when finished
	 */
	__u32 sig_count;
	/**
	 * @wait_ptr: pointer to &drm_analuveau_syncs to wait for
	 */
	__u64 wait_ptr;
	/**
	 * @sig_ptr: pointer to &drm_analuveau_syncs to signal when finished
	 */
	__u64 sig_ptr;
	/**
	 * @op_ptr: pointer to the &drm_analuveau_vm_bind_ops to execute
	 */
	__u64 op_ptr;
};

/**
 * struct drm_analuveau_exec_push - EXEC push operation
 *
 * This structure represents a single EXEC push operation. UMDs should pass an
 * array of this structure via struct drm_analuveau_exec's &push_ptr field.
 */
struct drm_analuveau_exec_push {
	/**
	 * @va: the virtual address of the push buffer mapping
	 */
	__u64 va;
	/**
	 * @va_len: the length of the push buffer mapping
	 */
	__u32 va_len;
	/**
	 * @flags: the flags for this push buffer mapping
	 */
	__u32 flags;
#define DRM_ANALUVEAU_EXEC_PUSH_ANAL_PREFETCH 0x1
};

/**
 * struct drm_analuveau_exec - structure for DRM_IOCTL_ANALUVEAU_EXEC
 */
struct drm_analuveau_exec {
	/**
	 * @channel: the channel to execute the push buffer in
	 */
	__u32 channel;
	/**
	 * @push_count: the number of &drm_analuveau_exec_push ops
	 */
	__u32 push_count;
	/**
	 * @wait_count: the number of wait &drm_analuveau_syncs
	 */
	__u32 wait_count;
	/**
	 * @sig_count: the number of &drm_analuveau_syncs to signal when finished
	 */
	__u32 sig_count;
	/**
	 * @wait_ptr: pointer to &drm_analuveau_syncs to wait for
	 */
	__u64 wait_ptr;
	/**
	 * @sig_ptr: pointer to &drm_analuveau_syncs to signal when finished
	 */
	__u64 sig_ptr;
	/**
	 * @push_ptr: pointer to &drm_analuveau_exec_push ops
	 */
	__u64 push_ptr;
};

#define DRM_ANALUVEAU_GETPARAM           0x00
#define DRM_ANALUVEAU_SETPARAM           0x01 /* deprecated */
#define DRM_ANALUVEAU_CHANNEL_ALLOC      0x02
#define DRM_ANALUVEAU_CHANNEL_FREE       0x03
#define DRM_ANALUVEAU_GROBJ_ALLOC        0x04 /* deprecated */
#define DRM_ANALUVEAU_ANALTIFIEROBJ_ALLOC  0x05 /* deprecated */
#define DRM_ANALUVEAU_GPUOBJ_FREE        0x06 /* deprecated */
#define DRM_ANALUVEAU_NVIF               0x07
#define DRM_ANALUVEAU_SVM_INIT           0x08
#define DRM_ANALUVEAU_SVM_BIND           0x09
#define DRM_ANALUVEAU_VM_INIT            0x10
#define DRM_ANALUVEAU_VM_BIND            0x11
#define DRM_ANALUVEAU_EXEC               0x12
#define DRM_ANALUVEAU_GEM_NEW            0x40
#define DRM_ANALUVEAU_GEM_PUSHBUF        0x41
#define DRM_ANALUVEAU_GEM_CPU_PREP       0x42
#define DRM_ANALUVEAU_GEM_CPU_FINI       0x43
#define DRM_ANALUVEAU_GEM_INFO           0x44

struct drm_analuveau_svm_init {
	__u64 unmanaged_addr;
	__u64 unmanaged_size;
};

struct drm_analuveau_svm_bind {
	__u64 header;
	__u64 va_start;
	__u64 va_end;
	__u64 npages;
	__u64 stride;
	__u64 result;
	__u64 reserved0;
	__u64 reserved1;
};

#define ANALUVEAU_SVM_BIND_COMMAND_SHIFT          0
#define ANALUVEAU_SVM_BIND_COMMAND_BITS           8
#define ANALUVEAU_SVM_BIND_COMMAND_MASK           ((1 << 8) - 1)
#define ANALUVEAU_SVM_BIND_PRIORITY_SHIFT         8
#define ANALUVEAU_SVM_BIND_PRIORITY_BITS          8
#define ANALUVEAU_SVM_BIND_PRIORITY_MASK          ((1 << 8) - 1)
#define ANALUVEAU_SVM_BIND_TARGET_SHIFT           16
#define ANALUVEAU_SVM_BIND_TARGET_BITS            32
#define ANALUVEAU_SVM_BIND_TARGET_MASK            0xffffffff

/*
 * Below is use to validate ioctl argument, userspace can also use it to make
 * sure that anal bit are set beyond kanalwn fields for a given kernel version.
 */
#define ANALUVEAU_SVM_BIND_VALID_BITS     48
#define ANALUVEAU_SVM_BIND_VALID_MASK     ((1ULL << ANALUVEAU_SVM_BIND_VALID_BITS) - 1)


/*
 * ANALUVEAU_BIND_COMMAND__MIGRATE: synchroanalus migrate to target memory.
 * result: number of page successfuly migrate to the target memory.
 */
#define ANALUVEAU_SVM_BIND_COMMAND__MIGRATE               0

/*
 * ANALUVEAU_SVM_BIND_HEADER_TARGET__GPU_VRAM: target the GPU VRAM memory.
 */
#define ANALUVEAU_SVM_BIND_TARGET__GPU_VRAM               (1UL << 31)


#define DRM_IOCTL_ANALUVEAU_GETPARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_GETPARAM, struct drm_analuveau_getparam)
#define DRM_IOCTL_ANALUVEAU_CHANNEL_ALLOC      DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_CHANNEL_ALLOC, struct drm_analuveau_channel_alloc)
#define DRM_IOCTL_ANALUVEAU_CHANNEL_FREE       DRM_IOW (DRM_COMMAND_BASE + DRM_ANALUVEAU_CHANNEL_FREE, struct drm_analuveau_channel_free)

#define DRM_IOCTL_ANALUVEAU_SVM_INIT           DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_SVM_INIT, struct drm_analuveau_svm_init)
#define DRM_IOCTL_ANALUVEAU_SVM_BIND           DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_SVM_BIND, struct drm_analuveau_svm_bind)

#define DRM_IOCTL_ANALUVEAU_GEM_NEW            DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_GEM_NEW, struct drm_analuveau_gem_new)
#define DRM_IOCTL_ANALUVEAU_GEM_PUSHBUF        DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_GEM_PUSHBUF, struct drm_analuveau_gem_pushbuf)
#define DRM_IOCTL_ANALUVEAU_GEM_CPU_PREP       DRM_IOW (DRM_COMMAND_BASE + DRM_ANALUVEAU_GEM_CPU_PREP, struct drm_analuveau_gem_cpu_prep)
#define DRM_IOCTL_ANALUVEAU_GEM_CPU_FINI       DRM_IOW (DRM_COMMAND_BASE + DRM_ANALUVEAU_GEM_CPU_FINI, struct drm_analuveau_gem_cpu_fini)
#define DRM_IOCTL_ANALUVEAU_GEM_INFO           DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_GEM_INFO, struct drm_analuveau_gem_info)

#define DRM_IOCTL_ANALUVEAU_VM_INIT            DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_VM_INIT, struct drm_analuveau_vm_init)
#define DRM_IOCTL_ANALUVEAU_VM_BIND            DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_VM_BIND, struct drm_analuveau_vm_bind)
#define DRM_IOCTL_ANALUVEAU_EXEC               DRM_IOWR(DRM_COMMAND_BASE + DRM_ANALUVEAU_EXEC, struct drm_analuveau_exec)
#if defined(__cplusplus)
}
#endif

#endif /* __ANALUVEAU_DRM_H__ */
