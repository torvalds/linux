/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MSM_DRM_H__
#define __MSM_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints:
 *  1) Do not use pointers, use __u64 instead for 32 bit / 64 bit
 *     user/kernel compatibility
 *  2) Keep fields aligned to their size
 *  3) Because of how drm_ioctl() works, we can add new fields at
 *     the end of an ioctl if some care is taken: drm_ioctl() will
 *     zero out the new fields at the tail of the ioctl, so a zero
 *     value should have a backwards compatible meaning.  And for
 *     output params, userspace won't see the newly added output
 *     fields.. so that has to be somehow ok.
 */

#define MSM_PIPE_NONE        0x00
#define MSM_PIPE_2D0         0x01
#define MSM_PIPE_2D1         0x02
#define MSM_PIPE_3D0         0x10

/* timeouts are specified in clock-monotonic absolute times (to simplify
 * restarting interrupted ioctls).  The following struct is logically the
 * same as 'struct timespec' but 32/64b ABI safe.
 */
struct drm_msm_timespec {
	__s64 tv_sec;          /* seconds */
	__s64 tv_nsec;         /* nanoseconds */
};

#define MSM_PARAM_GPU_ID     0x01
#define MSM_PARAM_GMEM_SIZE  0x02
#define MSM_PARAM_CHIP_ID    0x03
#define MSM_PARAM_MAX_FREQ   0x04
#define MSM_PARAM_TIMESTAMP  0x05

struct drm_msm_param {
	__u32 pipe;           /* in, MSM_PIPE_x */
	__u32 param;          /* in, MSM_PARAM_x */
	__u64 value;          /* out (get_param) or in (set_param) */
};

/*
 * GEM buffers:
 */

#define MSM_BO_SCANOUT       0x00000001     /* scanout capable */
#define MSM_BO_GPU_READONLY  0x00000002
#define MSM_BO_CACHE_MASK    0x000f0000
/* cache modes */
#define MSM_BO_CACHED        0x00010000
#define MSM_BO_WC            0x00020000
#define MSM_BO_UNCACHED      0x00040000

#define MSM_BO_FLAGS         (MSM_BO_SCANOUT | \
                              MSM_BO_GPU_READONLY | \
                              MSM_BO_CACHED | \
                              MSM_BO_WC | \
                              MSM_BO_UNCACHED)

struct drm_msm_gem_new {
	__u64 size;           /* in */
	__u32 flags;          /* in, mask of MSM_BO_x */
	__u32 handle;         /* out */
};

struct drm_msm_gem_info {
	__u32 handle;         /* in */
	__u32 pad;
	__u64 offset;         /* out, offset to pass to mmap() */
};

#define MSM_PREP_READ        0x01
#define MSM_PREP_WRITE       0x02
#define MSM_PREP_NOSYNC      0x04

#define MSM_PREP_FLAGS       (MSM_PREP_READ | MSM_PREP_WRITE | MSM_PREP_NOSYNC)

struct drm_msm_gem_cpu_prep {
	__u32 handle;         /* in */
	__u32 op;             /* in, mask of MSM_PREP_x */
	struct drm_msm_timespec timeout;   /* in */
};

struct drm_msm_gem_cpu_fini {
	__u32 handle;         /* in */
};

/*
 * Cmdstream Submission:
 */

/* The value written into the cmdstream is logically:
 *
 *   ((relocbuf->gpuaddr + reloc_offset) << shift) | or
 *
 * When we have GPU's w/ >32bit ptrs, it should be possible to deal
 * with this by emit'ing two reloc entries with appropriate shift
 * values.  Or a new MSM_SUBMIT_CMD_x type would also be an option.
 *
 * NOTE that reloc's must be sorted by order of increasing submit_offset,
 * otherwise EINVAL.
 */
struct drm_msm_gem_submit_reloc {
	__u32 submit_offset;  /* in, offset from submit_bo */
	__u32 or;             /* in, value OR'd with result */
	__s32 shift;          /* in, amount of left shift (can be negative) */
	__u32 reloc_idx;      /* in, index of reloc_bo buffer */
	__u64 reloc_offset;   /* in, offset from start of reloc_bo */
};

/* submit-types:
 *   BUF - this cmd buffer is executed normally.
 *   IB_TARGET_BUF - this cmd buffer is an IB target.  Reloc's are
 *      processed normally, but the kernel does not setup an IB to
 *      this buffer in the first-level ringbuffer
 *   CTX_RESTORE_BUF - only executed if there has been a GPU context
 *      switch since the last SUBMIT ioctl
 */
#define MSM_SUBMIT_CMD_BUF             0x0001
#define MSM_SUBMIT_CMD_IB_TARGET_BUF   0x0002
#define MSM_SUBMIT_CMD_CTX_RESTORE_BUF 0x0003
struct drm_msm_gem_submit_cmd {
	__u32 type;           /* in, one of MSM_SUBMIT_CMD_x */
	__u32 submit_idx;     /* in, index of submit_bo cmdstream buffer */
	__u32 submit_offset;  /* in, offset into submit_bo */
	__u32 size;           /* in, cmdstream size */
	__u32 pad;
	__u32 nr_relocs;      /* in, number of submit_reloc's */
	__u64 __user relocs;  /* in, ptr to array of submit_reloc's */
};

/* Each buffer referenced elsewhere in the cmdstream submit (ie. the
 * cmdstream buffer(s) themselves or reloc entries) has one (and only
 * one) entry in the submit->bos[] table.
 *
 * As a optimization, the current buffer (gpu virtual address) can be
 * passed back through the 'presumed' field.  If on a subsequent reloc,
 * userspace passes back a 'presumed' address that is still valid,
 * then patching the cmdstream for this entry is skipped.  This can
 * avoid kernel needing to map/access the cmdstream bo in the common
 * case.
 */
#define MSM_SUBMIT_BO_READ             0x0001
#define MSM_SUBMIT_BO_WRITE            0x0002

#define MSM_SUBMIT_BO_FLAGS            (MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE)

struct drm_msm_gem_submit_bo {
	__u32 flags;          /* in, mask of MSM_SUBMIT_BO_x */
	__u32 handle;         /* in, GEM handle */
	__u64 presumed;       /* in/out, presumed buffer address */
};

/* Each cmdstream submit consists of a table of buffers involved, and
 * one or more cmdstream buffers.  This allows for conditional execution
 * (context-restore), and IB buffers needed for per tile/bin draw cmds.
 */
struct drm_msm_gem_submit {
	__u32 pipe;           /* in, MSM_PIPE_x */
	__u32 fence;          /* out */
	__u32 nr_bos;         /* in, number of submit_bo's */
	__u32 nr_cmds;        /* in, number of submit_cmd's */
	__u64 __user bos;     /* in, ptr to array of submit_bo's */
	__u64 __user cmds;    /* in, ptr to array of submit_cmd's */
};

/* The normal way to synchronize with the GPU is just to CPU_PREP on
 * a buffer if you need to access it from the CPU (other cmdstream
 * submission from same or other contexts, PAGE_FLIP ioctl, etc, all
 * handle the required synchronization under the hood).  This ioctl
 * mainly just exists as a way to implement the gallium pipe_fence
 * APIs without requiring a dummy bo to synchronize on.
 */
struct drm_msm_wait_fence {
	__u32 fence;          /* in */
	__u32 pad;
	struct drm_msm_timespec timeout;   /* in */
};

#define DRM_MSM_GET_PARAM              0x00
/* placeholder:
#define DRM_MSM_SET_PARAM              0x01
 */
#define DRM_MSM_GEM_NEW                0x02
#define DRM_MSM_GEM_INFO               0x03
#define DRM_MSM_GEM_CPU_PREP           0x04
#define DRM_MSM_GEM_CPU_FINI           0x05
#define DRM_MSM_GEM_SUBMIT             0x06
#define DRM_MSM_WAIT_FENCE             0x07
#define DRM_MSM_NUM_IOCTLS             0x08

#define DRM_IOCTL_MSM_GET_PARAM        DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GET_PARAM, struct drm_msm_param)
#define DRM_IOCTL_MSM_GEM_NEW          DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_NEW, struct drm_msm_gem_new)
#define DRM_IOCTL_MSM_GEM_INFO         DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_INFO, struct drm_msm_gem_info)
#define DRM_IOCTL_MSM_GEM_CPU_PREP     DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_GEM_CPU_PREP, struct drm_msm_gem_cpu_prep)
#define DRM_IOCTL_MSM_GEM_CPU_FINI     DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_GEM_CPU_FINI, struct drm_msm_gem_cpu_fini)
#define DRM_IOCTL_MSM_GEM_SUBMIT       DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_SUBMIT, struct drm_msm_gem_submit)
#define DRM_IOCTL_MSM_WAIT_FENCE       DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_WAIT_FENCE, struct drm_msm_wait_fence)

#if defined(__cplusplus)
}
#endif

#endif /* __MSM_DRM_H__ */
