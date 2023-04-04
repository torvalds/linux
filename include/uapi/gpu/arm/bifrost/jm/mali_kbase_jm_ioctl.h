/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2020-2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _UAPI_KBASE_JM_IOCTL_H_
#define _UAPI_KBASE_JM_IOCTL_H_

#include <asm-generic/ioctl.h>
#include <linux/types.h>

/*
 * 11.1:
 * - Add BASE_MEM_TILER_ALIGN_TOP under base_mem_alloc_flags
 * 11.2:
 * - KBASE_MEM_QUERY_FLAGS can return KBASE_REG_PF_GROW and KBASE_REG_PROTECTED,
 *   which some user-side clients prior to 11.2 might fault if they received
 *   them
 * 11.3:
 * - New ioctls KBASE_IOCTL_STICKY_RESOURCE_MAP and
 *   KBASE_IOCTL_STICKY_RESOURCE_UNMAP
 * 11.4:
 * - New ioctl KBASE_IOCTL_MEM_FIND_GPU_START_AND_OFFSET
 * 11.5:
 * - New ioctl: KBASE_IOCTL_MEM_JIT_INIT (old ioctl renamed to _OLD)
 * 11.6:
 * - Added flags field to base_jit_alloc_info structure, which can be used to
 *   specify pseudo chunked tiler alignment for JIT allocations.
 * 11.7:
 * - Removed UMP support
 * 11.8:
 * - Added BASE_MEM_UNCACHED_GPU under base_mem_alloc_flags
 * 11.9:
 * - Added BASE_MEM_PERMANENT_KERNEL_MAPPING and BASE_MEM_FLAGS_KERNEL_ONLY
 *   under base_mem_alloc_flags
 * 11.10:
 * - Enabled the use of nr_extres field of base_jd_atom_v2 structure for
 *   JIT_ALLOC and JIT_FREE type softjobs to enable multiple JIT allocations
 *   with one softjob.
 * 11.11:
 * - Added BASE_MEM_GPU_VA_SAME_4GB_PAGE under base_mem_alloc_flags
 * 11.12:
 * - Removed ioctl: KBASE_IOCTL_GET_PROFILING_CONTROLS
 * 11.13:
 * - New ioctl: KBASE_IOCTL_MEM_EXEC_INIT
 * 11.14:
 * - Add BASE_MEM_GROUP_ID_MASK, base_mem_group_id_get, base_mem_group_id_set
 *   under base_mem_alloc_flags
 * 11.15:
 * - Added BASEP_CONTEXT_MMU_GROUP_ID_MASK under base_context_create_flags.
 * - Require KBASE_IOCTL_SET_FLAGS before BASE_MEM_MAP_TRACKING_HANDLE can be
 *   passed to mmap().
 * 11.16:
 * - Extended ioctl KBASE_IOCTL_MEM_SYNC to accept imported dma-buf.
 * - Modified (backwards compatible) ioctl KBASE_IOCTL_MEM_IMPORT behavior for
 *   dma-buf. Now, buffers are mapped on GPU when first imported, no longer
 *   requiring external resource or sticky resource tracking. UNLESS,
 *   CONFIG_MALI_DMA_BUF_MAP_ON_DEMAND is enabled.
 * 11.17:
 * - Added BASE_JD_REQ_JOB_SLOT.
 * - Reused padding field in base_jd_atom_v2 to pass job slot number.
 * - New ioctl: KBASE_IOCTL_GET_CPU_GPU_TIMEINFO
 * 11.18:
 * - Added BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP under base_mem_alloc_flags
 * 11.19:
 * - Extended base_jd_atom_v2 to allow a renderpass ID to be specified.
 * 11.20:
 * - Added new phys_pages member to kbase_ioctl_mem_jit_init for
 *   KBASE_IOCTL_MEM_JIT_INIT, previous variants of this renamed to use _10_2
 *   (replacing '_OLD') and _11_5 suffixes
 * - Replaced compat_core_req (deprecated in 10.3) with jit_id[2] in
 *   base_jd_atom_v2. It must currently be initialized to zero.
 * - Added heap_info_gpu_addr to base_jit_alloc_info, and
 *   BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE allowable in base_jit_alloc_info's
 *   flags member. Previous variants of this structure are kept and given _10_2
 *   and _11_5 suffixes.
 * - The above changes are checked for safe values in usual builds
 * 11.21:
 * - v2.0 of mali_trace debugfs file, which now versions the file separately
 * 11.22:
 * - Added base_jd_atom (v3), which is seq_nr + base_jd_atom_v2.
 *   KBASE_IOCTL_JOB_SUBMIT supports both in parallel.
 * 11.23:
 * - Modified KBASE_IOCTL_MEM_COMMIT behavior to reject requests to modify
 *   the physical memory backing of JIT allocations. This was not supposed
 *   to be a valid use case, but it was allowed by the previous implementation.
 * 11.24:
 * - Added a sysfs file 'serialize_jobs' inside a new sub-directory
 *   'scheduling'.
 * 11.25:
 * - Enabled JIT pressure limit in base/kbase by default
 * 11.26
 * - Added kinstr_jm API
 * 11.27
 * - Backwards compatible extension to HWC ioctl.
 * 11.28:
 * - Added kernel side cache ops needed hint
 * 11.29:
 * - Reserve ioctl 52
 * 11.30:
 * - Add a new priority level BASE_JD_PRIO_REALTIME
 * - Add ioctl 54: This controls the priority setting.
 * 11.31:
 * - Added BASE_JD_REQ_LIMITED_CORE_MASK.
 * - Added ioctl 55: set_limited_core_count.
 * 11.32:
 * - Added new HW performance counters interface to all GPUs.
 * 11.33:
 * - Removed Kernel legacy HWC interface
 * 11.34:
 * - First release of new HW performance counters interface.
 * 11.35:
 * - Dummy model (no mali) backend will now clear HWC values after each sample
 * 11.36:
 * - Remove legacy definitions:
 *   - base_jit_alloc_info_10_2
 *   - base_jit_alloc_info_11_5
 *   - kbase_ioctl_mem_jit_init_10_2
 *   - kbase_ioctl_mem_jit_init_11_5
 * 11.37:
 * - Fix kinstr_prfcnt issues:
 *   - Missing implicit sample for CMD_STOP when HWCNT buffer is full.
 *   - Race condition when stopping periodic sampling.
 *   - prfcnt_block_metadata::block_idx gaps.
 *   - PRFCNT_CONTROL_CMD_SAMPLE_ASYNC is removed.
 * 11.38:
 * - Relax the requirement to create a mapping with BASE_MEM_MAP_TRACKING_HANDLE
 *   before allocating GPU memory for the context.
 * - CPU mappings of USER_BUFFER imported memory handles must be cached.
 */
#define BASE_UK_VERSION_MAJOR 11
#define BASE_UK_VERSION_MINOR 38

/**
 * struct kbase_ioctl_version_check - Check version compatibility between
 * kernel and userspace
 *
 * @major: Major version number
 * @minor: Minor version number
 */
struct kbase_ioctl_version_check {
	__u16 major;
	__u16 minor;
};

#define KBASE_IOCTL_VERSION_CHECK \
	_IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)


/**
 * struct kbase_ioctl_job_submit - Submit jobs/atoms to the kernel
 *
 * @addr: Memory address of an array of struct base_jd_atom_v2 or v3
 * @nr_atoms: Number of entries in the array
 * @stride: sizeof(struct base_jd_atom_v2) or sizeof(struct base_jd_atom)
 */
struct kbase_ioctl_job_submit {
	__u64 addr;
	__u32 nr_atoms;
	__u32 stride;
};

#define KBASE_IOCTL_JOB_SUBMIT \
	_IOW(KBASE_IOCTL_TYPE, 2, struct kbase_ioctl_job_submit)

#define KBASE_IOCTL_POST_TERM \
	_IO(KBASE_IOCTL_TYPE, 4)

/**
 * struct kbase_ioctl_soft_event_update - Update the status of a soft-event
 * @event: GPU address of the event which has been updated
 * @new_status: The new status to set
 * @flags: Flags for future expansion
 */
struct kbase_ioctl_soft_event_update {
	__u64 event;
	__u32 new_status;
	__u32 flags;
};

#define KBASE_IOCTL_SOFT_EVENT_UPDATE \
	_IOW(KBASE_IOCTL_TYPE, 28, struct kbase_ioctl_soft_event_update)

/**
 * struct kbase_kinstr_jm_fd_out - Explains the compatibility information for
 * the `struct kbase_kinstr_jm_atom_state_change` structure returned from the
 * kernel
 *
 * @size:    The size of the `struct kbase_kinstr_jm_atom_state_change`
 * @version: Represents a breaking change in the
 *           `struct kbase_kinstr_jm_atom_state_change`
 * @padding: Explicit padding to get the structure up to 64bits. See
 * https://www.kernel.org/doc/Documentation/ioctl/botching-up-ioctls.rst
 *
 * The `struct kbase_kinstr_jm_atom_state_change` may have extra members at the
 * end of the structure that older user space might not understand. If the
 * `version` is the same, the structure is still compatible with newer kernels.
 * The `size` can be used to cast the opaque memory returned from the kernel.
 */
struct kbase_kinstr_jm_fd_out {
	__u16 size;
	__u8 version;
	__u8 padding[5];
};

/**
 * struct kbase_kinstr_jm_fd_in - Options when creating the file descriptor
 *
 * @count: Number of atom states that can be stored in the kernel circular
 *         buffer. Must be a power of two
 * @padding: Explicit padding to get the structure up to 64bits. See
 * https://www.kernel.org/doc/Documentation/ioctl/botching-up-ioctls.rst
 */
struct kbase_kinstr_jm_fd_in {
	__u16 count;
	__u8 padding[6];
};

union kbase_kinstr_jm_fd {
	struct kbase_kinstr_jm_fd_in in;
	struct kbase_kinstr_jm_fd_out out;
};

#define KBASE_IOCTL_KINSTR_JM_FD \
	_IOWR(KBASE_IOCTL_TYPE, 51, union kbase_kinstr_jm_fd)


#define KBASE_IOCTL_VERSION_CHECK_RESERVED \
	_IOWR(KBASE_IOCTL_TYPE, 52, struct kbase_ioctl_version_check)

#endif /* _UAPI_KBASE_JM_IOCTL_H_ */
