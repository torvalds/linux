/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2012-2020 NVIDIA Corporation */

#ifndef _UAPI_TEGRA_DRM_H_
#define _UAPI_TEGRA_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Tegra DRM legacy UAPI. Only enabled with STAGING */

#define DRM_TEGRA_GEM_CREATE_TILED     (1 << 0)
#define DRM_TEGRA_GEM_CREATE_BOTTOM_UP (1 << 1)

/**
 * struct drm_tegra_gem_create - parameters for the GEM object creation IOCTL
 */
struct drm_tegra_gem_create {
	/**
	 * @size:
	 *
	 * The size, in bytes, of the buffer object to be created.
	 */
	__u64 size;

	/**
	 * @flags:
	 *
	 * A bitmask of flags that influence the creation of GEM objects:
	 *
	 * DRM_TEGRA_GEM_CREATE_TILED
	 *   Use the 16x16 tiling format for this buffer.
	 *
	 * DRM_TEGRA_GEM_CREATE_BOTTOM_UP
	 *   The buffer has a bottom-up layout.
	 */
	__u32 flags;

	/**
	 * @handle:
	 *
	 * The handle of the created GEM object. Set by the kernel upon
	 * successful completion of the IOCTL.
	 */
	__u32 handle;
};

/**
 * struct drm_tegra_gem_mmap - parameters for the GEM mmap IOCTL
 */
struct drm_tegra_gem_mmap {
	/**
	 * @handle:
	 *
	 * Handle of the GEM object to obtain an mmap offset for.
	 */
	__u32 handle;

	/**
	 * @pad:
	 *
	 * Structure padding that may be used in the future. Must be 0.
	 */
	__u32 pad;

	/**
	 * @offset:
	 *
	 * The mmap offset for the given GEM object. Set by the kernel upon
	 * successful completion of the IOCTL.
	 */
	__u64 offset;
};

/**
 * struct drm_tegra_syncpt_read - parameters for the read syncpoint IOCTL
 */
struct drm_tegra_syncpt_read {
	/**
	 * @id:
	 *
	 * ID of the syncpoint to read the current value from.
	 */
	__u32 id;

	/**
	 * @value:
	 *
	 * The current syncpoint value. Set by the kernel upon successful
	 * completion of the IOCTL.
	 */
	__u32 value;
};

/**
 * struct drm_tegra_syncpt_incr - parameters for the increment syncpoint IOCTL
 */
struct drm_tegra_syncpt_incr {
	/**
	 * @id:
	 *
	 * ID of the syncpoint to increment.
	 */
	__u32 id;

	/**
	 * @pad:
	 *
	 * Structure padding that may be used in the future. Must be 0.
	 */
	__u32 pad;
};

/**
 * struct drm_tegra_syncpt_wait - parameters for the wait syncpoint IOCTL
 */
struct drm_tegra_syncpt_wait {
	/**
	 * @id:
	 *
	 * ID of the syncpoint to wait on.
	 */
	__u32 id;

	/**
	 * @thresh:
	 *
	 * Threshold value for which to wait.
	 */
	__u32 thresh;

	/**
	 * @timeout:
	 *
	 * Timeout, in milliseconds, to wait.
	 */
	__u32 timeout;

	/**
	 * @value:
	 *
	 * The new syncpoint value after the wait. Set by the kernel upon
	 * successful completion of the IOCTL.
	 */
	__u32 value;
};

#define DRM_TEGRA_NO_TIMEOUT	(0xffffffff)

/**
 * struct drm_tegra_open_channel - parameters for the open channel IOCTL
 */
struct drm_tegra_open_channel {
	/**
	 * @client:
	 *
	 * The client ID for this channel.
	 */
	__u32 client;

	/**
	 * @pad:
	 *
	 * Structure padding that may be used in the future. Must be 0.
	 */
	__u32 pad;

	/**
	 * @context:
	 *
	 * The application context of this channel. Set by the kernel upon
	 * successful completion of the IOCTL. This context needs to be passed
	 * to the DRM_TEGRA_CHANNEL_CLOSE or the DRM_TEGRA_SUBMIT IOCTLs.
	 */
	__u64 context;
};

/**
 * struct drm_tegra_close_channel - parameters for the close channel IOCTL
 */
struct drm_tegra_close_channel {
	/**
	 * @context:
	 *
	 * The application context of this channel. This is obtained from the
	 * DRM_TEGRA_OPEN_CHANNEL IOCTL.
	 */
	__u64 context;
};

/**
 * struct drm_tegra_get_syncpt - parameters for the get syncpoint IOCTL
 */
struct drm_tegra_get_syncpt {
	/**
	 * @context:
	 *
	 * The application context identifying the channel for which to obtain
	 * the syncpoint ID.
	 */
	__u64 context;

	/**
	 * @index:
	 *
	 * Index of the client syncpoint for which to obtain the ID.
	 */
	__u32 index;

	/**
	 * @id:
	 *
	 * The ID of the given syncpoint. Set by the kernel upon successful
	 * completion of the IOCTL.
	 */
	__u32 id;
};

/**
 * struct drm_tegra_get_syncpt_base - parameters for the get wait base IOCTL
 */
struct drm_tegra_get_syncpt_base {
	/**
	 * @context:
	 *
	 * The application context identifying for which channel to obtain the
	 * wait base.
	 */
	__u64 context;

	/**
	 * @syncpt:
	 *
	 * ID of the syncpoint for which to obtain the wait base.
	 */
	__u32 syncpt;

	/**
	 * @id:
	 *
	 * The ID of the wait base corresponding to the client syncpoint. Set
	 * by the kernel upon successful completion of the IOCTL.
	 */
	__u32 id;
};

/**
 * struct drm_tegra_syncpt - syncpoint increment operation
 */
struct drm_tegra_syncpt {
	/**
	 * @id:
	 *
	 * ID of the syncpoint to operate on.
	 */
	__u32 id;

	/**
	 * @incrs:
	 *
	 * Number of increments to perform for the syncpoint.
	 */
	__u32 incrs;
};

/**
 * struct drm_tegra_cmdbuf - structure describing a command buffer
 */
struct drm_tegra_cmdbuf {
	/**
	 * @handle:
	 *
	 * Handle to a GEM object containing the command buffer.
	 */
	__u32 handle;

	/**
	 * @offset:
	 *
	 * Offset, in bytes, into the GEM object identified by @handle at
	 * which the command buffer starts.
	 */
	__u32 offset;

	/**
	 * @words:
	 *
	 * Number of 32-bit words in this command buffer.
	 */
	__u32 words;

	/**
	 * @pad:
	 *
	 * Structure padding that may be used in the future. Must be 0.
	 */
	__u32 pad;
};

/**
 * struct drm_tegra_reloc - GEM object relocation structure
 */
struct drm_tegra_reloc {
	struct {
		/**
		 * @cmdbuf.handle:
		 *
		 * Handle to the GEM object containing the command buffer for
		 * which to perform this GEM object relocation.
		 */
		__u32 handle;

		/**
		 * @cmdbuf.offset:
		 *
		 * Offset, in bytes, into the command buffer at which to
		 * insert the relocated address.
		 */
		__u32 offset;
	} cmdbuf;
	struct {
		/**
		 * @target.handle:
		 *
		 * Handle to the GEM object to be relocated.
		 */
		__u32 handle;

		/**
		 * @target.offset:
		 *
		 * Offset, in bytes, into the target GEM object at which the
		 * relocated data starts.
		 */
		__u32 offset;
	} target;

	/**
	 * @shift:
	 *
	 * The number of bits by which to shift relocated addresses.
	 */
	__u32 shift;

	/**
	 * @pad:
	 *
	 * Structure padding that may be used in the future. Must be 0.
	 */
	__u32 pad;
};

/**
 * struct drm_tegra_waitchk - wait check structure
 */
struct drm_tegra_waitchk {
	/**
	 * @handle:
	 *
	 * Handle to the GEM object containing a command stream on which to
	 * perform the wait check.
	 */
	__u32 handle;

	/**
	 * @offset:
	 *
	 * Offset, in bytes, of the location in the command stream to perform
	 * the wait check on.
	 */
	__u32 offset;

	/**
	 * @syncpt:
	 *
	 * ID of the syncpoint to wait check.
	 */
	__u32 syncpt;

	/**
	 * @thresh:
	 *
	 * Threshold value for which to check.
	 */
	__u32 thresh;
};

/**
 * struct drm_tegra_submit - job submission structure
 */
struct drm_tegra_submit {
	/**
	 * @context:
	 *
	 * The application context identifying the channel to use for the
	 * execution of this job.
	 */
	__u64 context;

	/**
	 * @num_syncpts:
	 *
	 * The number of syncpoints operated on by this job. This defines the
	 * length of the array pointed to by @syncpts.
	 */
	__u32 num_syncpts;

	/**
	 * @num_cmdbufs:
	 *
	 * The number of command buffers to execute as part of this job. This
	 * defines the length of the array pointed to by @cmdbufs.
	 */
	__u32 num_cmdbufs;

	/**
	 * @num_relocs:
	 *
	 * The number of relocations to perform before executing this job.
	 * This defines the length of the array pointed to by @relocs.
	 */
	__u32 num_relocs;

	/**
	 * @num_waitchks:
	 *
	 * The number of wait checks to perform as part of this job. This
	 * defines the length of the array pointed to by @waitchks.
	 */
	__u32 num_waitchks;

	/**
	 * @waitchk_mask:
	 *
	 * Bitmask of valid wait checks.
	 */
	__u32 waitchk_mask;

	/**
	 * @timeout:
	 *
	 * Timeout, in milliseconds, before this job is cancelled.
	 */
	__u32 timeout;

	/**
	 * @syncpts:
	 *
	 * A pointer to an array of &struct drm_tegra_syncpt structures that
	 * specify the syncpoint operations performed as part of this job.
	 * The number of elements in the array must be equal to the value
	 * given by @num_syncpts.
	 */
	__u64 syncpts;

	/**
	 * @cmdbufs:
	 *
	 * A pointer to an array of &struct drm_tegra_cmdbuf structures that
	 * define the command buffers to execute as part of this job. The
	 * number of elements in the array must be equal to the value given
	 * by @num_syncpts.
	 */
	__u64 cmdbufs;

	/**
	 * @relocs:
	 *
	 * A pointer to an array of &struct drm_tegra_reloc structures that
	 * specify the relocations that need to be performed before executing
	 * this job. The number of elements in the array must be equal to the
	 * value given by @num_relocs.
	 */
	__u64 relocs;

	/**
	 * @waitchks:
	 *
	 * A pointer to an array of &struct drm_tegra_waitchk structures that
	 * specify the wait checks to be performed while executing this job.
	 * The number of elements in the array must be equal to the value
	 * given by @num_waitchks.
	 */
	__u64 waitchks;

	/**
	 * @fence:
	 *
	 * The threshold of the syncpoint associated with this job after it
	 * has been completed. Set by the kernel upon successful completion of
	 * the IOCTL. This can be used with the DRM_TEGRA_SYNCPT_WAIT IOCTL to
	 * wait for this job to be finished.
	 */
	__u32 fence;

	/**
	 * @reserved:
	 *
	 * This field is reserved for future use. Must be 0.
	 */
	__u32 reserved[5];
};

#define DRM_TEGRA_GEM_TILING_MODE_PITCH 0
#define DRM_TEGRA_GEM_TILING_MODE_TILED 1
#define DRM_TEGRA_GEM_TILING_MODE_BLOCK 2

/**
 * struct drm_tegra_gem_set_tiling - parameters for the set tiling IOCTL
 */
struct drm_tegra_gem_set_tiling {
	/**
	 * @handle:
	 *
	 * Handle to the GEM object for which to set the tiling parameters.
	 */
	__u32 handle;

	/**
	 * @mode:
	 *
	 * The tiling mode to set. Must be one of:
	 *
	 * DRM_TEGRA_GEM_TILING_MODE_PITCH
	 *   pitch linear format
	 *
	 * DRM_TEGRA_GEM_TILING_MODE_TILED
	 *   16x16 tiling format
	 *
	 * DRM_TEGRA_GEM_TILING_MODE_BLOCK
	 *   16Bx2 tiling format
	 */
	__u32 mode;

	/**
	 * @value:
	 *
	 * The value to set for the tiling mode parameter.
	 */
	__u32 value;

	/**
	 * @pad:
	 *
	 * Structure padding that may be used in the future. Must be 0.
	 */
	__u32 pad;
};

/**
 * struct drm_tegra_gem_get_tiling - parameters for the get tiling IOCTL
 */
struct drm_tegra_gem_get_tiling {
	/**
	 * @handle:
	 *
	 * Handle to the GEM object for which to query the tiling parameters.
	 */
	__u32 handle;

	/**
	 * @mode:
	 *
	 * The tiling mode currently associated with the GEM object. Set by
	 * the kernel upon successful completion of the IOCTL.
	 */
	__u32 mode;

	/**
	 * @value:
	 *
	 * The tiling mode parameter currently associated with the GEM object.
	 * Set by the kernel upon successful completion of the IOCTL.
	 */
	__u32 value;

	/**
	 * @pad:
	 *
	 * Structure padding that may be used in the future. Must be 0.
	 */
	__u32 pad;
};

#define DRM_TEGRA_GEM_BOTTOM_UP		(1 << 0)
#define DRM_TEGRA_GEM_FLAGS		(DRM_TEGRA_GEM_BOTTOM_UP)

/**
 * struct drm_tegra_gem_set_flags - parameters for the set flags IOCTL
 */
struct drm_tegra_gem_set_flags {
	/**
	 * @handle:
	 *
	 * Handle to the GEM object for which to set the flags.
	 */
	__u32 handle;

	/**
	 * @flags:
	 *
	 * The flags to set for the GEM object.
	 */
	__u32 flags;
};

/**
 * struct drm_tegra_gem_get_flags - parameters for the get flags IOCTL
 */
struct drm_tegra_gem_get_flags {
	/**
	 * @handle:
	 *
	 * Handle to the GEM object for which to query the flags.
	 */
	__u32 handle;

	/**
	 * @flags:
	 *
	 * The flags currently associated with the GEM object. Set by the
	 * kernel upon successful completion of the IOCTL.
	 */
	__u32 flags;
};

#define DRM_TEGRA_GEM_CREATE		0x00
#define DRM_TEGRA_GEM_MMAP		0x01
#define DRM_TEGRA_SYNCPT_READ		0x02
#define DRM_TEGRA_SYNCPT_INCR		0x03
#define DRM_TEGRA_SYNCPT_WAIT		0x04
#define DRM_TEGRA_OPEN_CHANNEL	        0x05
#define DRM_TEGRA_CLOSE_CHANNEL	        0x06
#define DRM_TEGRA_GET_SYNCPT		0x07
#define DRM_TEGRA_SUBMIT		0x08
#define DRM_TEGRA_GET_SYNCPT_BASE	0x09
#define DRM_TEGRA_GEM_SET_TILING	0x0a
#define DRM_TEGRA_GEM_GET_TILING	0x0b
#define DRM_TEGRA_GEM_SET_FLAGS		0x0c
#define DRM_TEGRA_GEM_GET_FLAGS		0x0d

#define DRM_IOCTL_TEGRA_GEM_CREATE DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_CREATE, struct drm_tegra_gem_create)
#define DRM_IOCTL_TEGRA_GEM_MMAP DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_MMAP, struct drm_tegra_gem_mmap)
#define DRM_IOCTL_TEGRA_SYNCPT_READ DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SYNCPT_READ, struct drm_tegra_syncpt_read)
#define DRM_IOCTL_TEGRA_SYNCPT_INCR DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SYNCPT_INCR, struct drm_tegra_syncpt_incr)
#define DRM_IOCTL_TEGRA_SYNCPT_WAIT DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SYNCPT_WAIT, struct drm_tegra_syncpt_wait)
#define DRM_IOCTL_TEGRA_OPEN_CHANNEL DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_OPEN_CHANNEL, struct drm_tegra_open_channel)
#define DRM_IOCTL_TEGRA_CLOSE_CHANNEL DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_CLOSE_CHANNEL, struct drm_tegra_close_channel)
#define DRM_IOCTL_TEGRA_GET_SYNCPT DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GET_SYNCPT, struct drm_tegra_get_syncpt)
#define DRM_IOCTL_TEGRA_SUBMIT DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_SUBMIT, struct drm_tegra_submit)
#define DRM_IOCTL_TEGRA_GET_SYNCPT_BASE DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GET_SYNCPT_BASE, struct drm_tegra_get_syncpt_base)
#define DRM_IOCTL_TEGRA_GEM_SET_TILING DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_SET_TILING, struct drm_tegra_gem_set_tiling)
#define DRM_IOCTL_TEGRA_GEM_GET_TILING DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_GET_TILING, struct drm_tegra_gem_get_tiling)
#define DRM_IOCTL_TEGRA_GEM_SET_FLAGS DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_SET_FLAGS, struct drm_tegra_gem_set_flags)
#define DRM_IOCTL_TEGRA_GEM_GET_FLAGS DRM_IOWR(DRM_COMMAND_BASE + DRM_TEGRA_GEM_GET_FLAGS, struct drm_tegra_gem_get_flags)

/* New Tegra DRM UAPI */

/*
 * Reported by the driver in the `capabilities` field.
 *
 * DRM_TEGRA_CHANNEL_CAP_CACHE_COHERENT: If set, the engine is cache coherent
 * with regard to the system memory.
 */
#define DRM_TEGRA_CHANNEL_CAP_CACHE_COHERENT (1 << 0)

struct drm_tegra_channel_open {
	/**
	 * @host1x_class: [in]
	 *
	 * Host1x class of the engine that will be programmed using this
	 * channel.
	 */
	__u32 host1x_class;

	/**
	 * @flags: [in]
	 *
	 * Flags.
	 */
	__u32 flags;

	/**
	 * @context: [out]
	 *
	 * Opaque identifier corresponding to the opened channel.
	 */
	__u32 context;

	/**
	 * @version: [out]
	 *
	 * Version of the engine hardware. This can be used by userspace
	 * to determine how the engine needs to be programmed.
	 */
	__u32 version;

	/**
	 * @capabilities: [out]
	 *
	 * Flags describing the hardware capabilities.
	 */
	__u32 capabilities;
	__u32 padding;
};

struct drm_tegra_channel_close {
	/**
	 * @context: [in]
	 *
	 * Identifier of the channel to close.
	 */
	__u32 context;
	__u32 padding;
};

/*
 * Mapping flags that can be used to influence how the mapping is created.
 *
 * DRM_TEGRA_CHANNEL_MAP_READ: create mapping that allows HW read access
 * DRM_TEGRA_CHANNEL_MAP_WRITE: create mapping that allows HW write access
 */
#define DRM_TEGRA_CHANNEL_MAP_READ  (1 << 0)
#define DRM_TEGRA_CHANNEL_MAP_WRITE (1 << 1)
#define DRM_TEGRA_CHANNEL_MAP_READ_WRITE (DRM_TEGRA_CHANNEL_MAP_READ | \
					  DRM_TEGRA_CHANNEL_MAP_WRITE)

struct drm_tegra_channel_map {
	/**
	 * @context: [in]
	 *
	 * Identifier of the channel to which make memory available for.
	 */
	__u32 context;

	/**
	 * @handle: [in]
	 *
	 * GEM handle of the memory to map.
	 */
	__u32 handle;

	/**
	 * @flags: [in]
	 *
	 * Flags.
	 */
	__u32 flags;

	/**
	 * @mapping: [out]
	 *
	 * Identifier corresponding to the mapping, to be used for
	 * relocations or unmapping later.
	 */
	__u32 mapping;
};

struct drm_tegra_channel_unmap {
	/**
	 * @context: [in]
	 *
	 * Channel identifier of the channel to unmap memory from.
	 */
	__u32 context;

	/**
	 * @mapping: [in]
	 *
	 * Mapping identifier of the memory mapping to unmap.
	 */
	__u32 mapping;
};

/* Submission */

/**
 * Specify that bit 39 of the patched-in address should be set to switch
 * swizzling between Tegra and non-Tegra sector layout on systems that store
 * surfaces in system memory in non-Tegra sector layout.
 */
#define DRM_TEGRA_SUBMIT_RELOC_SECTOR_LAYOUT (1 << 0)

struct drm_tegra_submit_buf {
	/**
	 * @mapping: [in]
	 *
	 * Identifier of the mapping to use in the submission.
	 */
	__u32 mapping;

	/**
	 * @flags: [in]
	 *
	 * Flags.
	 */
	__u32 flags;

	/**
	 * Information for relocation patching.
	 */
	struct {
		/**
		 * @target_offset: [in]
		 *
		 * Offset from the start of the mapping of the data whose
		 * address is to be patched into the gather.
		 */
		__u64 target_offset;

		/**
		 * @gather_offset_words: [in]
		 *
		 * Offset in words from the start of the gather data to
		 * where the address should be patched into.
		 */
		__u32 gather_offset_words;

		/**
		 * @shift: [in]
		 *
		 * Number of bits the address should be shifted right before
		 * patching in.
		 */
		__u32 shift;
	} reloc;
};

/**
 * Execute `words` words of Host1x opcodes specified in the `gather_data_ptr`
 * buffer. Each GATHER_UPTR command uses successive words from the buffer.
 */
#define DRM_TEGRA_SUBMIT_CMD_GATHER_UPTR		0
/**
 * Wait for a syncpoint to reach a value before continuing with further
 * commands.
 */
#define DRM_TEGRA_SUBMIT_CMD_WAIT_SYNCPT		1
/**
 * Wait for a syncpoint to reach a value before continuing with further
 * commands. The threshold is calculated relative to the start of the job.
 */
#define DRM_TEGRA_SUBMIT_CMD_WAIT_SYNCPT_RELATIVE	2

struct drm_tegra_submit_cmd_gather_uptr {
	__u32 words;
	__u32 reserved[3];
};

struct drm_tegra_submit_cmd_wait_syncpt {
	__u32 id;
	__u32 value;
	__u32 reserved[2];
};

struct drm_tegra_submit_cmd {
	/**
	 * @type: [in]
	 *
	 * Command type to execute. One of the DRM_TEGRA_SUBMIT_CMD*
	 * defines.
	 */
	__u32 type;

	/**
	 * @flags: [in]
	 *
	 * Flags.
	 */
	__u32 flags;

	union {
		struct drm_tegra_submit_cmd_gather_uptr gather_uptr;
		struct drm_tegra_submit_cmd_wait_syncpt wait_syncpt;
		__u32 reserved[4];
	};
};

struct drm_tegra_submit_syncpt {
	/**
	 * @id: [in]
	 *
	 * ID of the syncpoint that the job will increment.
	 */
	__u32 id;

	/**
	 * @flags: [in]
	 *
	 * Flags.
	 */
	__u32 flags;

	/**
	 * @increments: [in]
	 *
	 * Number of times the job will increment this syncpoint.
	 */
	__u32 increments;

	/**
	 * @value: [out]
	 *
	 * Value the syncpoint will have once the job has completed all
	 * its specified syncpoint increments.
	 *
	 * Note that the kernel may increment the syncpoint before or after
	 * the job. These increments are not reflected in this field.
	 *
	 * If the job hangs or times out, not all of the increments may
	 * get executed.
	 */
	__u32 value;
};

struct drm_tegra_channel_submit {
	/**
	 * @context: [in]
	 *
	 * Identifier of the channel to submit this job to.
	 */
	__u32 context;

	/**
	 * @num_bufs: [in]
	 *
	 * Number of elements in the `bufs_ptr` array.
	 */
	__u32 num_bufs;

	/**
	 * @num_cmds: [in]
	 *
	 * Number of elements in the `cmds_ptr` array.
	 */
	__u32 num_cmds;

	/**
	 * @gather_data_words: [in]
	 *
	 * Number of 32-bit words in the `gather_data_ptr` array.
	 */
	__u32 gather_data_words;

	/**
	 * @bufs_ptr: [in]
	 *
	 * Pointer to an array of drm_tegra_submit_buf structures.
	 */
	__u64 bufs_ptr;

	/**
	 * @cmds_ptr: [in]
	 *
	 * Pointer to an array of drm_tegra_submit_cmd structures.
	 */
	__u64 cmds_ptr;

	/**
	 * @gather_data_ptr: [in]
	 *
	 * Pointer to an array of Host1x opcodes to be used by GATHER_UPTR
	 * commands.
	 */
	__u64 gather_data_ptr;

	/**
	 * @syncobj_in: [in]
	 *
	 * Handle for DRM syncobj that will be waited before submission.
	 * Ignored if zero.
	 */
	__u32 syncobj_in;

	/**
	 * @syncobj_out: [in]
	 *
	 * Handle for DRM syncobj that will have its fence replaced with
	 * the job's completion fence. Ignored if zero.
	 */
	__u32 syncobj_out;

	/**
	 * @syncpt_incr: [in,out]
	 *
	 * Information about the syncpoint the job will increment.
	 */
	struct drm_tegra_submit_syncpt syncpt;
};

struct drm_tegra_syncpoint_allocate {
	/**
	 * @id: [out]
	 *
	 * ID of allocated syncpoint.
	 */
	__u32 id;
	__u32 padding;
};

struct drm_tegra_syncpoint_free {
	/**
	 * @id: [in]
	 *
	 * ID of syncpoint to free.
	 */
	__u32 id;
	__u32 padding;
};

struct drm_tegra_syncpoint_wait {
	/**
	 * @timeout: [in]
	 *
	 * Absolute timestamp at which the wait will time out.
	 */
	__s64 timeout_ns;

	/**
	 * @id: [in]
	 *
	 * ID of syncpoint to wait on.
	 */
	__u32 id;

	/**
	 * @threshold: [in]
	 *
	 * Threshold to wait for.
	 */
	__u32 threshold;

	/**
	 * @value: [out]
	 *
	 * Value of the syncpoint upon wait completion.
	 */
	__u32 value;

	__u32 padding;
};

#define DRM_IOCTL_TEGRA_CHANNEL_OPEN DRM_IOWR(DRM_COMMAND_BASE + 0x10, struct drm_tegra_channel_open)
#define DRM_IOCTL_TEGRA_CHANNEL_CLOSE DRM_IOWR(DRM_COMMAND_BASE + 0x11, struct drm_tegra_channel_close)
#define DRM_IOCTL_TEGRA_CHANNEL_MAP DRM_IOWR(DRM_COMMAND_BASE + 0x12, struct drm_tegra_channel_map)
#define DRM_IOCTL_TEGRA_CHANNEL_UNMAP DRM_IOWR(DRM_COMMAND_BASE + 0x13, struct drm_tegra_channel_unmap)
#define DRM_IOCTL_TEGRA_CHANNEL_SUBMIT DRM_IOWR(DRM_COMMAND_BASE + 0x14, struct drm_tegra_channel_submit)

#define DRM_IOCTL_TEGRA_SYNCPOINT_ALLOCATE DRM_IOWR(DRM_COMMAND_BASE + 0x20, struct drm_tegra_syncpoint_allocate)
#define DRM_IOCTL_TEGRA_SYNCPOINT_FREE DRM_IOWR(DRM_COMMAND_BASE + 0x21, struct drm_tegra_syncpoint_free)
#define DRM_IOCTL_TEGRA_SYNCPOINT_WAIT DRM_IOWR(DRM_COMMAND_BASE + 0x22, struct drm_tegra_syncpoint_wait)

#if defined(__cplusplus)
}
#endif

#endif
