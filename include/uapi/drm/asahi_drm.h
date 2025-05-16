/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) The Asahi Linux Contributors
 * Copyright (C) 2018-2023 Collabora Ltd.
 * Copyright (C) 2014-2018 Broadcom
 */
#ifndef _ASAHI_DRM_H_
#define _ASAHI_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * DOC: Introduction to the Asahi UAPI
 *
 * This documentation describes the Asahi IOCTLs.
 *
 * Just a few generic rules about the data passed to the Asahi IOCTLs (cribbed
 * from Panthor):
 *
 * - Structures must be aligned on 64-bit/8-byte. If the object is not
 *   naturally aligned, a padding field must be added.
 * - Fields must be explicitly aligned to their natural type alignment with
 *   pad[0..N] fields.
 * - All padding fields will be checked by the driver to make sure they are
 *   zeroed.
 * - Flags can be added, but not removed/replaced.
 * - New fields can be added to the main structures (the structures
 *   directly passed to the ioctl). Those fields can be added at the end of
 *   the structure, or replace existing padding fields. Any new field being
 *   added must preserve the behavior that existed before those fields were
 *   added when a value of zero is passed.
 * - New fields can be added to indirect objects (objects pointed by the
 *   main structure), iff those objects are passed a size to reflect the
 *   size known by the userspace driver (see
 *   drm_asahi_cmd_header::size).
 * - If the kernel driver is too old to know some fields, those will be
 *   ignored if zero, and otherwise rejected (and so will be zero on output).
 * - If userspace is too old to know some fields, those will be zeroed
 *   (input) before the structure is parsed by the kernel driver.
 * - Each new flag/field addition must come with a driver version update so
 *   the userspace driver doesn't have to guess which flags are supported.
 * - Structures should not contain unions, as this would defeat the
 *   extensibility of such structures.
 * - IOCTLs can't be removed or replaced. New IOCTL IDs should be placed
 *   at the end of the drm_asahi_ioctl_id enum.
 */

/**
 * enum drm_asahi_ioctl_id - IOCTL IDs
 *
 * Place new ioctls at the end, don't re-order, don't replace or remove entries.
 *
 * These IDs are not meant to be used directly. Use the DRM_IOCTL_ASAHI_xxx
 * definitions instead.
 */
enum drm_asahi_ioctl_id {
	/** @DRM_ASAHI_GET_PARAMS: Query device properties. */
	DRM_ASAHI_GET_PARAMS = 0,

	/** @DRM_ASAHI_GET_TIME: Query device time. */
	DRM_ASAHI_GET_TIME,

	/** @DRM_ASAHI_VM_CREATE: Create a GPU VM address space. */
	DRM_ASAHI_VM_CREATE,

	/** @DRM_ASAHI_VM_DESTROY: Destroy a VM. */
	DRM_ASAHI_VM_DESTROY,

	/** @DRM_ASAHI_VM_BIND: Bind/unbind memory to a VM. */
	DRM_ASAHI_VM_BIND,

	/** @DRM_ASAHI_GEM_CREATE: Create a buffer object. */
	DRM_ASAHI_GEM_CREATE,

	/**
	 * @DRM_ASAHI_GEM_MMAP_OFFSET: Get offset to pass to mmap() to map a
	 * given GEM handle.
	 */
	DRM_ASAHI_GEM_MMAP_OFFSET,

	/** @DRM_ASAHI_GEM_BIND_OBJECT: Bind memory as a special object */
	DRM_ASAHI_GEM_BIND_OBJECT,

	/** @DRM_ASAHI_QUEUE_CREATE: Create a scheduling queue. */
	DRM_ASAHI_QUEUE_CREATE,

	/** @DRM_ASAHI_QUEUE_DESTROY: Destroy a scheduling queue. */
	DRM_ASAHI_QUEUE_DESTROY,

	/** @DRM_ASAHI_SUBMIT: Submit commands to a queue. */
	DRM_ASAHI_SUBMIT,
};

#define DRM_ASAHI_MAX_CLUSTERS	64

/**
 * struct drm_asahi_params_global - Global parameters.
 *
 * This struct may be queried by drm_asahi_get_params.
 */
struct drm_asahi_params_global {
	/** @features: Feature bits from drm_asahi_feature */
	__u64 features;

	/** @gpu_generation: GPU generation, e.g. 13 for G13G */
	__u32 gpu_generation;

	/** @gpu_variant: GPU variant as a character, e.g. 'C' for G13C */
	__u32 gpu_variant;

	/**
	 * @gpu_revision: GPU revision in BCD, e.g. 0x00 for 'A0' or
	 * 0x21 for 'C1'
	 */
	__u32 gpu_revision;

	/** @chip_id: Chip ID in BCD, e.g. 0x8103 for T8103 */
	__u32 chip_id;

	/** @num_dies: Number of dies in the SoC */
	__u32 num_dies;

	/** @num_clusters_total: Number of GPU clusters (across all dies) */
	__u32 num_clusters_total;

	/**
	 * @num_cores_per_cluster: Number of logical cores per cluster
	 * (including inactive/nonexistent)
	 */
	__u32 num_cores_per_cluster;

	/** @max_frequency_khz: Maximum GPU core clock frequency */
	__u32 max_frequency_khz;

	/** @core_masks: Bitmask of present/enabled cores per cluster */
	__u64 core_masks[DRM_ASAHI_MAX_CLUSTERS];

	/**
	 * @vm_start: VM range start VMA. Together with @vm_end, this defines
	 * the window of valid GPU VAs. Userspace is expected to subdivide VAs
	 * out of this window.
	 *
	 * This window contains all virtual addresses that userspace needs to
	 * know about. There may be kernel-internal GPU VAs outside this range,
	 * but that detail is not relevant here.
	 */
	__u64 vm_start;

	/** @vm_end: VM range end VMA */
	__u64 vm_end;

	/**
	 * @vm_kernel_min_size: Minimum kernel VMA window size.
	 *
	 * When creating a VM, userspace is required to carve out a section of
	 * virtual addresses (within the range given by @vm_start and
	 * @vm_end). The kernel will allocate various internal structures
	 * within the specified VA range.
	 *
	 * Allowing userspace to choose the VA range for the kernel, rather than
	 * the kernel reserving VAs and requiring userspace to cope, can assist
	 * in implementing SVM.
	 */
	__u64 vm_kernel_min_size;

	/**
	 * @max_commands_per_submission: Maximum number of supported commands
	 * per submission. This mirrors firmware limits. Userspace must split up
	 * larger command buffers, which may require inserting additional
	 * synchronization.
	 */
	__u32 max_commands_per_submission;

	/**
	 * @max_attachments: Maximum number of drm_asahi_attachment's per
	 * command
	 */
	__u32 max_attachments;

	/**
	 * @command_timestamp_frequency_hz: Timebase frequency for timestamps
	 * written during command execution, specified via drm_asahi_timestamp
	 * structures. As this rate is controlled by the firmware, it is a
	 * queryable parameter.
	 *
	 * Userspace must divide by this frequency to convert timestamps to
	 * seconds, rather than hardcoding a particular firmware's rate.
	 */
	__u64 command_timestamp_frequency_hz;
};

/**
 * enum drm_asahi_feature - Feature bits
 *
 * This covers only features that userspace cannot infer from the architecture
 * version. Most features don't need to be here.
 */
enum drm_asahi_feature {
	/**
	 * @DRM_ASAHI_FEATURE_SOFT_FAULTS: GPU has "soft fault" enabled. Shader
	 * loads of unmapped memory will return zero. Shader stores to unmapped
	 * memory will be silently discarded. Note that only shader load/store
	 * is affected. Other hardware units are not affected, notably including
	 * texture sampling.
	 *
	 * Soft fault is set when initializing the GPU and cannot be runtime
	 * toggled. Therefore, it is exposed as a feature bit and not a
	 * userspace-settable flag on the VM. When soft fault is enabled,
	 * userspace can speculate memory accesses more aggressively.
	 */
	DRM_ASAHI_FEATURE_SOFT_FAULTS = (1UL) << 0,
};

/**
 * struct drm_asahi_get_params - Arguments passed to DRM_IOCTL_ASAHI_GET_PARAMS
 */
struct drm_asahi_get_params {
	/** @param_group: Parameter group to fetch (MBZ) */
	__u32 param_group;

	/** @pad: MBZ */
	__u32 pad;

	/** @pointer: User pointer to write parameter struct */
	__u64 pointer;

	/**
	 * @size: Size of the user buffer. In case of older userspace, this may
	 * be less than sizeof(struct drm_asahi_params_global). The kernel will
	 * not write past the length specified here, allowing extensibility.
	 */
	__u64 size;
};

/**
 * struct drm_asahi_vm_create - Arguments passed to DRM_IOCTL_ASAHI_VM_CREATE
 */
struct drm_asahi_vm_create {
	/**
	 * @kernel_start: Start of the kernel-reserved address range. See
	 * drm_asahi_params_global::vm_kernel_min_size.
	 *
	 * Both @kernel_start and @kernel_end must be within the range of
	 * valid VAs given by drm_asahi_params_global::vm_start and
	 * drm_asahi_params_global::vm_end. The size of the kernel range
	 * (@kernel_end - @kernel_start) must be at least
	 * drm_asahi_params_global::vm_kernel_min_size.
	 *
	 * Userspace must not bind any memory on this VM into this reserved
	 * range, it is for kernel use only.
	 */
	__u64 kernel_start;

	/**
	 * @kernel_end: End of the kernel-reserved address range. See
	 * @kernel_start.
	 */
	__u64 kernel_end;

	/** @vm_id: Returned VM ID */
	__u32 vm_id;

	/** @pad: MBZ */
	__u32 pad;
};

/**
 * struct drm_asahi_vm_destroy - Arguments passed to DRM_IOCTL_ASAHI_VM_DESTROY
 */
struct drm_asahi_vm_destroy {
	/** @vm_id: VM ID to be destroyed */
	__u32 vm_id;

	/** @pad: MBZ */
	__u32 pad;
};

/**
 * enum drm_asahi_gem_flags - Flags for GEM creation
 */
enum drm_asahi_gem_flags {
	/**
	 * @DRM_ASAHI_GEM_WRITEBACK: BO should be CPU-mapped as writeback.
	 *
	 * Map as writeback instead of write-combine. This optimizes for CPU
	 * reads.
	 */
	DRM_ASAHI_GEM_WRITEBACK = (1L << 0),

	/**
	 * @DRM_ASAHI_GEM_VM_PRIVATE: BO is private to this GPU VM (no exports).
	 */
	DRM_ASAHI_GEM_VM_PRIVATE = (1L << 1),
};

/**
 * struct drm_asahi_gem_create - Arguments passed to DRM_IOCTL_ASAHI_GEM_CREATE
 */
struct drm_asahi_gem_create {
	/** @size: Size of the BO */
	__u64 size;

	/** @flags: Combination of drm_asahi_gem_flags flags. */
	__u32 flags;

	/**
	 * @vm_id: VM ID to assign to the BO, if DRM_ASAHI_GEM_VM_PRIVATE is set
	 */
	__u32 vm_id;

	/** @handle: Returned GEM handle for the BO */
	__u32 handle;

	/** @pad: MBZ */
	__u32 pad;
};

/**
 * struct drm_asahi_gem_mmap_offset - Arguments passed to
 * DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET
 */
struct drm_asahi_gem_mmap_offset {
	/** @handle: Handle for the object being mapped. */
	__u32 handle;

	/** @flags: Must be zero */
	__u32 flags;

	/** @offset: The fake offset to use for subsequent mmap call */
	__u64 offset;
};

/**
 * enum drm_asahi_bind_flags - Flags for GEM binding
 */
enum drm_asahi_bind_flags {
	/**
	 * @DRM_ASAHI_BIND_UNBIND: Instead of binding a GEM object to the range,
	 * simply unbind the GPU VMA range.
	 */
	DRM_ASAHI_BIND_UNBIND = (1L << 0),

	/** @DRM_ASAHI_BIND_READ: Map BO with GPU read permission */
	DRM_ASAHI_BIND_READ = (1L << 1),

	/** @DRM_ASAHI_BIND_WRITE: Map BO with GPU write permission */
	DRM_ASAHI_BIND_WRITE = (1L << 2),

	/**
	 * @DRM_ASAHI_BIND_SINGLE_PAGE: Map a single page of the BO repeatedly
	 * across the VA range.
	 *
	 * This is useful to fill a VA range with scratch pages or zero pages.
	 * It is intended as a mechanism to accelerate sparse.
	 */
	DRM_ASAHI_BIND_SINGLE_PAGE = (1L << 3),
};

/**
 * struct drm_asahi_gem_bind_op - Description of a single GEM bind operation.
 */
struct drm_asahi_gem_bind_op {
	/** @flags: Combination of drm_asahi_bind_flags flags. */
	__u32 flags;

	/** @handle: GEM object to bind (except for UNBIND) */
	__u32 handle;

	/**
	 * @offset: Offset into the object (except for UNBIND).
	 *
	 * For a regular bind, this is the beginning of the region of the GEM
	 * object to bind.
	 *
	 * For a single-page bind, this is the offset to the single page that
	 * will be repeatedly bound.
	 *
	 * Must be page-size aligned.
	 */
	__u64 offset;

	/**
	 * @range: Number of bytes to bind/unbind to @addr.
	 *
	 * Must be page-size aligned.
	 */
	__u64 range;

	/**
	 * @addr: Address to bind to.
	 *
	 * Must be page-size aligned.
	 */
	__u64 addr;
};

/**
 * struct drm_asahi_vm_bind - Arguments passed to
 * DRM_IOCTL_ASAHI_VM_BIND
 */
struct drm_asahi_vm_bind {
	/** @vm_id: The ID of the VM to bind to */
	__u32 vm_id;

	/** @num_binds: number of binds in this IOCTL. */
	__u32 num_binds;

	/**
	 * @stride: Stride in bytes between consecutive binds. This allows
	 * extensibility of drm_asahi_gem_bind_op.
	 */
	__u32 stride;

	/** @pad: MBZ */
	__u32 pad;

	/**
	 * @userptr: User pointer to an array of @num_binds structures of type
	 * @drm_asahi_gem_bind_op and size @stride bytes.
	 */
	__u64 userptr;
};

/**
 * enum drm_asahi_bind_object_op - Special object bind operation
 */
enum drm_asahi_bind_object_op {
	/** @DRM_ASAHI_BIND_OBJECT_OP_BIND: Bind a BO as a special GPU object */
	DRM_ASAHI_BIND_OBJECT_OP_BIND = 0,

	/** @DRM_ASAHI_BIND_OBJECT_OP_UNBIND: Unbind a special GPU object */
	DRM_ASAHI_BIND_OBJECT_OP_UNBIND = 1,
};

/**
 * enum drm_asahi_bind_object_flags - Special object bind flags
 */
enum drm_asahi_bind_object_flags {
	/**
	 * @DRM_ASAHI_BIND_OBJECT_USAGE_TIMESTAMPS: Map a BO as a timestamp
	 * buffer.
	 */
	DRM_ASAHI_BIND_OBJECT_USAGE_TIMESTAMPS = (1L << 0),
};

/**
 * struct drm_asahi_gem_bind_object - Arguments passed to
 * DRM_IOCTL_ASAHI_GEM_BIND_OBJECT
 */
struct drm_asahi_gem_bind_object {
	/** @op: Bind operation (enum drm_asahi_bind_object_op) */
	__u32 op;

	/** @flags: Combination of drm_asahi_bind_object_flags flags. */
	__u32 flags;

	/** @handle: GEM object to bind/unbind (BIND) */
	__u32 handle;

	/** @vm_id: The ID of the VM to operate on (MBZ currently) */
	__u32 vm_id;

	/** @offset: Offset into the object (BIND only) */
	__u64 offset;

	/** @range: Number of bytes to bind/unbind (BIND only) */
	__u64 range;

	/** @object_handle: Object handle (out for BIND, in for UNBIND) */
	__u32 object_handle;

	/** @pad: MBZ */
	__u32 pad;
};

/**
 * enum drm_asahi_cmd_type - Command type
 */
enum drm_asahi_cmd_type {
	/**
	 * @DRM_ASAHI_CMD_RENDER: Render command, executing on the render
	 * subqueue. Combined vertex and fragment operation.
	 *
	 * Followed by a @drm_asahi_cmd_render payload.
	 */
	DRM_ASAHI_CMD_RENDER = 0,

	/**
	 * @DRM_ASAHI_CMD_COMPUTE: Compute command on the compute subqueue.
	 *
	 * Followed by a @drm_asahi_cmd_compute payload.
	 */
	DRM_ASAHI_CMD_COMPUTE = 1,

	/**
	 * @DRM_ASAHI_SET_VERTEX_ATTACHMENTS: Software command to set
	 * attachments for subsequent vertex shaders in the same submit.
	 *
	 * Followed by (possibly multiple) @drm_asahi_attachment payloads.
	 */
	DRM_ASAHI_SET_VERTEX_ATTACHMENTS = 2,

	/**
	 * @DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS: Software command to set
	 * attachments for subsequent fragment shaders in the same submit.
	 *
	 * Followed by (possibly multiple) @drm_asahi_attachment payloads.
	 */
	DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS = 3,

	/**
	 * @DRM_ASAHI_SET_COMPUTE_ATTACHMENTS: Software command to set
	 * attachments for subsequent compute shaders in the same submit.
	 *
	 * Followed by (possibly multiple) @drm_asahi_attachment payloads.
	 */
	DRM_ASAHI_SET_COMPUTE_ATTACHMENTS = 4,
};

/**
 * enum drm_asahi_priority - Scheduling queue priority.
 *
 * These priorities are forwarded to the firmware to influence firmware
 * scheduling. The exact policy is ultimately decided by firmware, but
 * these enums allow userspace to communicate the intentions.
 */
enum drm_asahi_priority {
	/** @DRM_ASAHI_PRIORITY_LOW: Low priority queue. */
	DRM_ASAHI_PRIORITY_LOW = 0,

	/** @DRM_ASAHI_PRIORITY_MEDIUM: Medium priority queue. */
	DRM_ASAHI_PRIORITY_MEDIUM = 1,

	/**
	 * @DRM_ASAHI_PRIORITY_HIGH: High priority queue.
	 *
	 * Reserved for future extension.
	 */
	DRM_ASAHI_PRIORITY_HIGH = 2,

	/**
	 * @DRM_ASAHI_PRIORITY_REALTIME: Real-time priority queue.
	 *
	 * Reserved for future extension.
	 */
	DRM_ASAHI_PRIORITY_REALTIME = 3,
};

/**
 * struct drm_asahi_queue_create - Arguments passed to
 * DRM_IOCTL_ASAHI_QUEUE_CREATE
 */
struct drm_asahi_queue_create {
	/** @flags: MBZ */
	__u32 flags;

	/** @vm_id: The ID of the VM this queue is bound to */
	__u32 vm_id;

	/** @priority: One of drm_asahi_priority */
	__u32 priority;

	/** @queue_id: The returned queue ID */
	__u32 queue_id;

	/**
	 * @usc_exec_base: GPU base address for all USC binaries (shaders) on
	 * this queue. USC addresses are 32-bit relative to this 64-bit base.
	 *
	 * This sets the following registers on all queue commands:
	 *
	 *	USC_EXEC_BASE_TA  (vertex)
	 *	USC_EXEC_BASE_ISP (fragment)
	 *	USC_EXEC_BASE_CP  (compute)
	 *
	 * While the hardware lets us configure these independently per command,
	 * we do not have a use case for this. Instead, we expect userspace to
	 * fix a 4GiB VA carveout for USC memory and pass its base address here.
	 */
	__u64 usc_exec_base;
};

/**
 * struct drm_asahi_queue_destroy - Arguments passed to
 * DRM_IOCTL_ASAHI_QUEUE_DESTROY
 */
struct drm_asahi_queue_destroy {
	/** @queue_id: The queue ID to be destroyed */
	__u32 queue_id;

	/** @pad: MBZ */
	__u32 pad;
};

/**
 * enum drm_asahi_sync_type - Sync item type
 */
enum drm_asahi_sync_type {
	/** @DRM_ASAHI_SYNC_SYNCOBJ: Binary sync object */
	DRM_ASAHI_SYNC_SYNCOBJ = 0,

	/** @DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ: Timeline sync object */
	DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ = 1,
};

/**
 * struct drm_asahi_sync - Sync item
 */
struct drm_asahi_sync {
	/** @sync_type: One of drm_asahi_sync_type */
	__u32 sync_type;

	/** @handle: The sync object handle */
	__u32 handle;

	/** @timeline_value: Timeline value for timeline sync objects */
	__u64 timeline_value;
};

/**
 * define DRM_ASAHI_BARRIER_NONE - Command index for no barrier
 *
 * This special value may be passed in to drm_asahi_command::vdm_barrier or
 * drm_asahi_command::cdm_barrier to indicate that the respective subqueue
 * should not wait on any previous work.
 */
#define DRM_ASAHI_BARRIER_NONE (0xFFFFu)

/**
 * struct drm_asahi_cmd_header - Top level command structure
 *
 * This struct is core to the command buffer definition and therefore is not
 * extensible.
 */
struct drm_asahi_cmd_header {
	/** @cmd_type: One of drm_asahi_cmd_type */
	__u16 cmd_type;

	/**
	 * @size: Size of this command, not including this header.
	 *
	 * For hardware commands, this enables extensibility of commands without
	 * requiring extra command types. Passing a command that is shorter
	 * than expected is explicitly allowed for backwards-compatibility.
	 * Truncated fields will be zeroed.
	 *
	 * For the synthetic attachment setting commands, this implicitly
	 * encodes the number of attachments. These commands take multiple
	 * fixed-size @drm_asahi_attachment structures as their payload, so size
	 * equals number of attachments * sizeof(struct drm_asahi_attachment).
	 */
	__u16 size;

	/**
	 * @vdm_barrier: VDM (render) command index to wait on.
	 *
	 * Barriers are indices relative to the beginning of a given submit. A
	 * barrier of 0 waits on commands submitted to the respective subqueue
	 * in previous submit ioctls. A barrier of N waits on N previous
	 * commands on the subqueue within the current submit ioctl. As a
	 * special case, passing @DRM_ASAHI_BARRIER_NONE avoids waiting on any
	 * commands in the subqueue.
	 *
	 * Examples:
	 *
	 *   0: This waits on all previous work.
	 *
	 *   NONE: This does not wait for anything on this subqueue.
	 *
	 *   1: This waits on the first render command in the submit.
	 *   This is valid only if there are multiple render commands in the
	 *   same submit.
	 *
	 * Barriers are valid only for hardware commands. Synthetic software
	 * commands to set attachments must pass NONE here.
	 */
	__u16 vdm_barrier;

	/**
	 * @cdm_barrier: CDM (compute) command index to wait on.
	 *
	 * See @vdm_barrier, and replace VDM/render with CDM/compute.
	 */
	__u16 cdm_barrier;
};

/**
 * struct drm_asahi_submit - Arguments passed to DRM_IOCTL_ASAHI_SUBMIT
 */
struct drm_asahi_submit {
	/**
	 * @syncs: An optional pointer to an array of drm_asahi_sync. The first
	 * @in_sync_count elements are in-syncs, then the remaining
	 * @out_sync_count elements are out-syncs. Using a single array with
	 * explicit partitioning simplifies handling.
	 */
	__u64 syncs;

	/**
	 * @cmdbuf: Pointer to the command buffer to submit.
	 *
	 * This is a flat command buffer. By design, it contains no CPU
	 * pointers, which makes it suitable for a virtgpu wire protocol without
	 * requiring any serializing/deserializing step.
	 *
	 * It consists of a series of commands. Each command begins with a
	 * fixed-size @drm_asahi_cmd_header header and is followed by a
	 * variable-length payload according to the type and size in the header.
	 *
	 * The combined count of "real" hardware commands must be nonzero and at
	 * most drm_asahi_params_global::max_commands_per_submission.
	 */
	__u64 cmdbuf;

	/** @flags: Flags for command submission (MBZ) */
	__u32 flags;

	/** @queue_id: The queue ID to be submitted to */
	__u32 queue_id;

	/**
	 * @in_sync_count: Number of sync objects to wait on before starting
	 * this job.
	 */
	__u32 in_sync_count;

	/**
	 * @out_sync_count: Number of sync objects to signal upon completion of
	 * this job.
	 */
	__u32 out_sync_count;

	/** @cmdbuf_size: Command buffer size in bytes */
	__u32 cmdbuf_size;

	/** @pad: MBZ */
	__u32 pad;
};

/**
 * struct drm_asahi_attachment - Describe an "attachment".
 *
 * Attachments are any memory written by shaders, notably including render
 * target attachments written by the end-of-tile program. This is purely a hint
 * about the accessed memory regions. It is optional to specify, which is
 * fortunate as it cannot be specified precisely with bindless access anyway.
 * But where possible, it's probably a good idea for userspace to include these
 * hints, forwarded to the firmware.
 *
 * This struct is implicitly sized and therefore is not extensible.
 */
struct drm_asahi_attachment {
	/** @pointer: Base address of the attachment */
	__u64 pointer;

	/** @size: Size of the attachment in bytes */
	__u64 size;

	/** @pad: MBZ */
	__u32 pad;

	/** @flags: MBZ */
	__u32 flags;
};

enum drm_asahi_render_flags {
	/**
	 * @DRM_ASAHI_RENDER_VERTEX_SCRATCH: A vertex stage shader uses scratch
	 * memory.
	 */
	DRM_ASAHI_RENDER_VERTEX_SCRATCH = (1U << 0),

	/**
	 * @DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES: Process even empty tiles.
	 * This must be set when clearing render targets.
	 */
	DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES = (1U << 1),

	/**
	 * @DRM_ASAHI_RENDER_NO_VERTEX_CLUSTERING: Run vertex stage on a single
	 * cluster (on multi-cluster GPUs)
	 *
	 * This harms performance but can workaround certain sync/coherency
	 * bugs, and therefore is useful for debugging.
	 */
	DRM_ASAHI_RENDER_NO_VERTEX_CLUSTERING = (1U << 2),

	/**
	 * @DRM_ASAHI_RENDER_DBIAS_IS_INT: Use integer depth bias formula.
	 *
	 * Graphics specifications contain two alternate formulas for depth
	 * bias, a float formula used with floating-point depth buffers and an
	 * integer formula using with unorm depth buffers. This flag specifies
	 * that the integer formula should be used. If omitted, the float
	 * formula is used instead.
	 *
	 * This corresponds to bit 18 of the relevant hardware control register,
	 * so we match that here for efficiency.
	 */
	DRM_ASAHI_RENDER_DBIAS_IS_INT = (1U << 18),
};

/**
 * struct drm_asahi_zls_buffer - Describe a depth or stencil buffer.
 *
 * These fields correspond to hardware registers in the ZLS (Z Load/Store) unit.
 * There are three hardware registers for each field respectively for loads,
 * stores, and partial renders. In practice, it makes sense to set all to the
 * same values, except in exceptional cases not yet implemented in userspace, so
 * we do not duplicate here for simplicity/efficiency.
 *
 * This struct is embedded in other structs and therefore is not extensible.
 */
struct drm_asahi_zls_buffer {
	/** @base: Base address of the buffer */
	__u64 base;

	/**
	 * @comp_base: If the load buffer is compressed, address of the
	 * compression metadata section.
	 */
	__u64 comp_base;

	/**
	 * @stride: If layered rendering is enabled, the number of bytes
	 * between each layer of the buffer.
	 */
	__u32 stride;

	/**
	 * @comp_stride: If layered rendering is enabled, the number of bytes
	 * between each layer of the compression metadata.
	 */
	__u32 comp_stride;
};

/**
 * struct drm_asahi_timestamp - Describe a timestamp write.
 *
 * The firmware can optionally write the GPU timestamp at render pass
 * granularities, but it needs to be mapped specially via
 * DRM_IOCTL_ASAHI_GEM_BIND_OBJECT. This structure therefore describes where to
 * write as a handle-offset pair, rather than a GPU address like normal.
 *
 * This struct is embedded in other structs and therefore is not extensible.
 */
struct drm_asahi_timestamp {
	/**
	 * @handle: Handle of the timestamp buffer, or 0 to skip this
	 * timestamp. If nonzero, this must equal the value returned in
	 * drm_asahi_gem_bind_object::object_handle.
	 */
	__u32 handle;

	/** @offset: Offset to write into the timestamp buffer */
	__u32 offset;
};

/**
 * struct drm_asahi_timestamps - Describe timestamp writes.
 *
 * Each operation that can be timestamped, can be timestamped at the start and
 * end. Therefore, drm_asahi_timestamp structs always come in pairs, bundled
 * together into drm_asahi_timestamps.
 *
 * This struct is embedded in other structs and therefore is not extensible.
 */
struct drm_asahi_timestamps {
	/** @start: Timestamp recorded at the start of the operation */
	struct drm_asahi_timestamp start;

	/** @end: Timestamp recorded at the end of the operation */
	struct drm_asahi_timestamp end;
};

/**
 * struct drm_asahi_helper_program - Describe helper program configuration.
 *
 * The helper program is a compute-like kernel required for various hardware
 * functionality. Its most important role is dynamically allocating
 * scratch/stack memory for individual subgroups, by partitioning a static
 * allocation shared for the whole device. It is supplied by userspace via
 * drm_asahi_helper_program and internally dispatched by the hardware as needed.
 *
 * This struct is embedded in other structs and therefore is not extensible.
 */
struct drm_asahi_helper_program {
	/**
	 * @binary: USC address to the helper program binary. This is a tagged
	 * pointer with configuration in the bottom bits.
	 */
	__u32 binary;

	/** @cfg: Additional configuration bits for the helper program. */
	__u32 cfg;

	/**
	 * @data: Data passed to the helper program. This value is not
	 * interpreted by the kernel, firmware, or hardware in any way. It is
	 * simply a sideband for userspace, set with the submit ioctl and read
	 * via special registers inside the helper program.
	 *
	 * In practice, userspace will pass a 64-bit GPU VA here pointing to the
	 * actual arguments, which presumably don't fit in 64-bits.
	 */
	__u64 data;
};

/**
 * struct drm_asahi_bg_eot - Describe a background or end-of-tile program.
 *
 * The background and end-of-tile programs are dispatched by the hardware at the
 * beginning and end of rendering. As the hardware "tilebuffer" is simply local
 * memory, these programs are necessary to implement API-level render targets.
 * The fragment-like background program is responsible for loading either the
 * clear colour or the existing render target contents, while the compute-like
 * end-of-tile program stores the tilebuffer contents to memory.
 *
 * This struct is embedded in other structs and therefore is not extensible.
 */
struct drm_asahi_bg_eot {
	/**
	 * @usc: USC address of the hardware USC words binding resources
	 * (including images and uniforms) and the program itself. Note this is
	 * an additional layer of indirection compared to the helper program,
	 * avoiding the need for a sideband for data. This is a tagged pointer
	 * with additional configuration in the bottom bits.
	 */
	__u32 usc;

	/**
	 * @rsrc_spec: Resource specifier for the program. This is a packed
	 * hardware data structure describing the required number of registers,
	 * uniforms, bound textures, and bound samplers.
	 */
	__u32 rsrc_spec;
};

/**
 * struct drm_asahi_cmd_render - Command to submit 3D
 *
 * This command submits a single render pass. The hardware control stream may
 * include many draws and subpasses, but within the command, the framebuffer
 * dimensions and attachments are fixed.
 *
 * The hardware requires the firmware to set a large number of Control Registers
 * setting up state at render pass granularity before each command rendering 3D.
 * The firmware bundles this state into data structures. Unfortunately, we
 * cannot expose either any of that directly to userspace, because the
 * kernel-firmware ABI is not stable. Although we can guarantee the firmware
 * updates in tandem with the kernel, we cannot break old userspace when
 * upgrading the firmware and kernel. Therefore, we need to abstract well the
 * data structures to avoid tying our hands with future firmwares.
 *
 * The bulk of drm_asahi_cmd_render therefore consists of values of hardware
 * control registers, marshalled via the firmware interface.
 *
 * The framebuffer/tilebuffer dimensions are also specified here. In addition to
 * being passed to the firmware/hardware, the kernel requires these dimensions
 * to calculate various essential tiling-related data structures. It is
 * unfortunate that our submits are heavier than on vendors with saner
 * hardware-software interfaces. The upshot is all of this information is
 * readily available to userspace with all current APIs.
 *
 * It looks odd - but it's not overly burdensome and it ensures we can remain
 * compatible with old userspace.
 */
struct drm_asahi_cmd_render {
	/** @flags: Combination of drm_asahi_render_flags flags. */
	__u32 flags;

	/**
	 * @isp_zls_pixels: ISP_ZLS_PIXELS register value. This contains the
	 * depth/stencil width/height, which may differ from the framebuffer
	 * width/height.
	 */
	__u32 isp_zls_pixels;

	/**
	 * @vdm_ctrl_stream_base: VDM_CTRL_STREAM_BASE register value. GPU
	 * address to the beginning of the VDM control stream.
	 */
	__u64 vdm_ctrl_stream_base;

	/** @vertex_helper: Helper program used for the vertex shader */
	struct drm_asahi_helper_program vertex_helper;

	/** @fragment_helper: Helper program used for the fragment shader */
	struct drm_asahi_helper_program fragment_helper;

	/**
	 * @isp_scissor_base: ISP_SCISSOR_BASE register value. GPU address of an
	 * array of scissor descriptors indexed in the render pass.
	 */
	__u64 isp_scissor_base;

	/**
	 * @isp_dbias_base: ISP_DBIAS_BASE register value. GPU address of an
	 * array of depth bias values indexed in the render pass.
	 */
	__u64 isp_dbias_base;

	/**
	 * @isp_oclqry_base: ISP_OCLQRY_BASE register value. GPU address of an
	 * array of occlusion query results written by the render pass.
	 */
	__u64 isp_oclqry_base;

	/** @depth: Depth buffer */
	struct drm_asahi_zls_buffer depth;

	/** @stencil: Stencil buffer */
	struct drm_asahi_zls_buffer stencil;

	/** @zls_ctrl: ZLS_CTRL register value */
	__u64 zls_ctrl;

	/** @ppp_multisamplectl: PPP_MULTISAMPLECTL register value */
	__u64 ppp_multisamplectl;

	/**
	 * @sampler_heap: Base address of the sampler heap. This heap is used
	 * for both vertex shaders and fragment shaders. The registers are
	 * per-stage, but there is no known use case for separate heaps.
	 */
	__u64 sampler_heap;

	/** @ppp_ctrl: PPP_CTRL register value */
	__u32 ppp_ctrl;

	/** @width_px: Framebuffer width in pixels */
	__u16 width_px;

	/** @height_px: Framebuffer height in pixels */
	__u16 height_px;

	/** @layers: Number of layers in the framebuffer */
	__u16 layers;

	/** @sampler_count: Number of samplers in the sampler heap. */
	__u16 sampler_count;

	/** @utile_width_px: Width of a logical tilebuffer tile in pixels */
	__u8 utile_width_px;

	/** @utile_height_px: Height of a logical tilebuffer tile in pixels */
	__u8 utile_height_px;

	/** @samples: # of samples in the framebuffer. Must be 1, 2, or 4. */
	__u8 samples;

	/** @sample_size_B: # of bytes in the tilebuffer required per sample. */
	__u8 sample_size_B;

	/**
	 * @isp_merge_upper_x: 32-bit float used in the hardware triangle
	 * merging. Calculate as: tan(60 deg) * width.
	 *
	 * Making these values UAPI avoids requiring floating-point calculations
	 * in the kernel in the hot path.
	 */
	__u32 isp_merge_upper_x;

	/**
	 * @isp_merge_upper_y: 32-bit float. Calculate as: tan(60 deg) * height.
	 * See @isp_merge_upper_x.
	 */
	__u32 isp_merge_upper_y;

	/** @bg: Background program run for each tile at the start */
	struct drm_asahi_bg_eot bg;

	/** @eot: End-of-tile program ran for each tile at the end */
	struct drm_asahi_bg_eot eot;

	/**
	 * @partial_bg: Background program ran at the start of each tile when
	 * resuming the render pass during a partial render.
	 */
	struct drm_asahi_bg_eot partial_bg;

	/**
	 * @partial_eot: End-of-tile program ran at the end of each tile when
	 * pausing the render pass during a partial render.
	 */
	struct drm_asahi_bg_eot partial_eot;

	/**
	 * @isp_bgobjdepth: ISP_BGOBJDEPTH register value. This is the depth
	 * buffer clear value, encoded in the depth buffer's format: either a
	 * 32-bit float or a 16-bit unorm (with upper bits zeroed).
	 */
	__u32 isp_bgobjdepth;

	/**
	 * @isp_bgobjvals: ISP_BGOBJVALS register value. The bottom 8-bits
	 * contain the stencil buffer clear value.
	 */
	__u32 isp_bgobjvals;

	/** @ts_vtx: Timestamps for the vertex portion of the render */
	struct drm_asahi_timestamps ts_vtx;

	/** @ts_frag: Timestamps for the fragment portion of the render */
	struct drm_asahi_timestamps ts_frag;
};

/**
 * struct drm_asahi_cmd_compute - Command to submit compute
 *
 * This command submits a control stream consisting of compute dispatches. There
 * is essentially no limit on how many compute dispatches may be included in a
 * single compute command, although timestamps are at command granularity.
 */
struct drm_asahi_cmd_compute {
	/** @flags: MBZ */
	__u32 flags;

	/** @sampler_count: Number of samplers in the sampler heap. */
	__u32 sampler_count;

	/**
	 * @cdm_ctrl_stream_base: CDM_CTRL_STREAM_BASE register value. GPU
	 * address to the beginning of the CDM control stream.
	 */
	__u64 cdm_ctrl_stream_base;

	/**
	 * @cdm_ctrl_stream_end: GPU base address to the end of the hardware
	 * control stream. Note this only considers the first contiguous segment
	 * of the control stream, as the stream might jump elsewhere.
	 */
	__u64 cdm_ctrl_stream_end;

	/** @sampler_heap: Base address of the sampler heap. */
	__u64 sampler_heap;

	/** @helper: Helper program used for this compute command */
	struct drm_asahi_helper_program helper;

	/** @ts: Timestamps for the compute command */
	struct drm_asahi_timestamps ts;
};

/**
 * struct drm_asahi_get_time - Arguments passed to DRM_IOCTL_ASAHI_GET_TIME
 */
struct drm_asahi_get_time {
	/** @flags: MBZ. */
	__u64 flags;

	/** @gpu_timestamp: On return, the GPU timestamp in nanoseconds. */
	__u64 gpu_timestamp;
};

/**
 * DRM_IOCTL_ASAHI() - Build an Asahi IOCTL number
 * @__access: Access type. Must be R, W or RW.
 * @__id: One of the DRM_ASAHI_xxx id.
 * @__type: Suffix of the type being passed to the IOCTL.
 *
 * Don't use this macro directly, use the DRM_IOCTL_ASAHI_xxx
 * values instead.
 *
 * Return: An IOCTL number to be passed to ioctl() from userspace.
 */
#define DRM_IOCTL_ASAHI(__access, __id, __type) \
	DRM_IO ## __access(DRM_COMMAND_BASE + DRM_ASAHI_ ## __id, \
			   struct drm_asahi_ ## __type)

/* Note: this is an enum so that it can be resolved by Rust bindgen. */
enum {
	DRM_IOCTL_ASAHI_GET_PARAMS       = DRM_IOCTL_ASAHI(W, GET_PARAMS, get_params),
	DRM_IOCTL_ASAHI_GET_TIME         = DRM_IOCTL_ASAHI(WR, GET_TIME, get_time),
	DRM_IOCTL_ASAHI_VM_CREATE        = DRM_IOCTL_ASAHI(WR, VM_CREATE, vm_create),
	DRM_IOCTL_ASAHI_VM_DESTROY       = DRM_IOCTL_ASAHI(W, VM_DESTROY, vm_destroy),
	DRM_IOCTL_ASAHI_VM_BIND          = DRM_IOCTL_ASAHI(W, VM_BIND, vm_bind),
	DRM_IOCTL_ASAHI_GEM_CREATE       = DRM_IOCTL_ASAHI(WR, GEM_CREATE, gem_create),
	DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET  = DRM_IOCTL_ASAHI(WR, GEM_MMAP_OFFSET, gem_mmap_offset),
	DRM_IOCTL_ASAHI_GEM_BIND_OBJECT  = DRM_IOCTL_ASAHI(WR, GEM_BIND_OBJECT, gem_bind_object),
	DRM_IOCTL_ASAHI_QUEUE_CREATE     = DRM_IOCTL_ASAHI(WR, QUEUE_CREATE, queue_create),
	DRM_IOCTL_ASAHI_QUEUE_DESTROY    = DRM_IOCTL_ASAHI(W, QUEUE_DESTROY, queue_destroy),
	DRM_IOCTL_ASAHI_SUBMIT           = DRM_IOCTL_ASAHI(W, SUBMIT, submit),
};

#if defined(__cplusplus)
}
#endif

#endif /* _ASAHI_DRM_H_ */
