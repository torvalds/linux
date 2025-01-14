/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

/* The pipe-id just uses the lower bits, so can be OR'd with flags in
 * the upper 16 bits (which could be extended further, if needed, maybe
 * we extend/overload the pipe-id some day to deal with multiple rings,
 * but even then I don't think we need the full lower 16 bits).
 */
#define MSM_PIPE_ID_MASK     0xffff
#define MSM_PIPE_ID(x)       ((x) & MSM_PIPE_ID_MASK)
#define MSM_PIPE_FLAGS(x)    ((x) & ~MSM_PIPE_ID_MASK)

/* timeouts are specified in clock-monotonic absolute times (to simplify
 * restarting interrupted ioctls).  The following struct is logically the
 * same as 'struct timespec' but 32/64b ABI safe.
 */
struct drm_msm_timespec {
	__s64 tv_sec;          /* seconds */
	__s64 tv_nsec;         /* nanoseconds */
};

/* Below "RO" indicates a read-only param, "WO" indicates write-only, and
 * "RW" indicates a param that can be both read (GET_PARAM) and written
 * (SET_PARAM)
 */
#define MSM_PARAM_GPU_ID     0x01  /* RO */
#define MSM_PARAM_GMEM_SIZE  0x02  /* RO */
#define MSM_PARAM_CHIP_ID    0x03  /* RO */
#define MSM_PARAM_MAX_FREQ   0x04  /* RO */
#define MSM_PARAM_TIMESTAMP  0x05  /* RO */
#define MSM_PARAM_GMEM_BASE  0x06  /* RO */
#define MSM_PARAM_PRIORITIES 0x07  /* RO: The # of priority levels */
#define MSM_PARAM_PP_PGTABLE 0x08  /* RO: Deprecated, always returns zero */
#define MSM_PARAM_FAULTS     0x09  /* RO */
#define MSM_PARAM_SUSPENDS   0x0a  /* RO */
#define MSM_PARAM_SYSPROF    0x0b  /* WO: 1 preserves perfcntrs, 2 also disables suspend */
#define MSM_PARAM_COMM       0x0c  /* WO: override for task->comm */
#define MSM_PARAM_CMDLINE    0x0d  /* WO: override for task cmdline */
#define MSM_PARAM_VA_START   0x0e  /* RO: start of valid GPU iova range */
#define MSM_PARAM_VA_SIZE    0x0f  /* RO: size of valid GPU iova range (bytes) */
#define MSM_PARAM_HIGHEST_BANK_BIT 0x10 /* RO */
#define MSM_PARAM_RAYTRACING 0x11 /* RO */
#define MSM_PARAM_UBWC_SWIZZLE 0x12 /* RO */
#define MSM_PARAM_MACROTILE_MODE 0x13 /* RO */
#define MSM_PARAM_UCHE_TRAP_BASE 0x14 /* RO */

/* For backwards compat.  The original support for preemption was based on
 * a single ring per priority level so # of priority levels equals the #
 * of rings.  With drm/scheduler providing additional levels of priority,
 * the number of priorities is greater than the # of rings.  The param is
 * renamed to better reflect this.
 */
#define MSM_PARAM_NR_RINGS   MSM_PARAM_PRIORITIES

struct drm_msm_param {
	__u32 pipe;           /* in, MSM_PIPE_x */
	__u32 param;          /* in, MSM_PARAM_x */
	__u64 value;          /* out (get_param) or in (set_param) */
	__u32 len;            /* zero for non-pointer params */
	__u32 pad;            /* must be zero */
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
#define MSM_BO_UNCACHED      0x00040000 /* deprecated, use MSM_BO_WC */
#define MSM_BO_CACHED_COHERENT 0x080000

#define MSM_BO_FLAGS         (MSM_BO_SCANOUT | \
                              MSM_BO_GPU_READONLY | \
                              MSM_BO_CACHE_MASK)

struct drm_msm_gem_new {
	__u64 size;           /* in */
	__u32 flags;          /* in, mask of MSM_BO_x */
	__u32 handle;         /* out */
};

/* Get or set GEM buffer info.  The requested value can be passed
 * directly in 'value', or for data larger than 64b 'value' is a
 * pointer to userspace buffer, with 'len' specifying the number of
 * bytes copied into that buffer.  For info returned by pointer,
 * calling the GEM_INFO ioctl with null 'value' will return the
 * required buffer size in 'len'
 */
#define MSM_INFO_GET_OFFSET	0x00   /* get mmap() offset, returned by value */
#define MSM_INFO_GET_IOVA	0x01   /* get iova, returned by value */
#define MSM_INFO_SET_NAME	0x02   /* set the debug name (by pointer) */
#define MSM_INFO_GET_NAME	0x03   /* get debug name, returned by pointer */
#define MSM_INFO_SET_IOVA	0x04   /* set the iova, passed by value */
#define MSM_INFO_GET_FLAGS	0x05   /* get the MSM_BO_x flags */
#define MSM_INFO_SET_METADATA	0x06   /* set userspace metadata */
#define MSM_INFO_GET_METADATA	0x07   /* get userspace metadata */

struct drm_msm_gem_info {
	__u32 handle;         /* in */
	__u32 info;           /* in - one of MSM_INFO_* */
	__u64 value;          /* in or out */
	__u32 len;            /* in or out */
	__u32 pad;
};

#define MSM_PREP_READ        0x01
#define MSM_PREP_WRITE       0x02
#define MSM_PREP_NOSYNC      0x04
#define MSM_PREP_BOOST       0x08

#define MSM_PREP_FLAGS       (MSM_PREP_READ | \
			      MSM_PREP_WRITE | \
			      MSM_PREP_NOSYNC | \
			      MSM_PREP_BOOST | \
			      0)

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
#ifdef __cplusplus
	__u32 _or;            /* in, value OR'd with result */
#else
	__u32 or;             /* in, value OR'd with result */
#endif
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
	__u64 relocs;         /* in, ptr to array of submit_reloc's */
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
#define MSM_SUBMIT_BO_DUMP             0x0004
#define MSM_SUBMIT_BO_NO_IMPLICIT      0x0008

#define MSM_SUBMIT_BO_FLAGS            (MSM_SUBMIT_BO_READ | \
					MSM_SUBMIT_BO_WRITE | \
					MSM_SUBMIT_BO_DUMP | \
					MSM_SUBMIT_BO_NO_IMPLICIT)

struct drm_msm_gem_submit_bo {
	__u32 flags;          /* in, mask of MSM_SUBMIT_BO_x */
	__u32 handle;         /* in, GEM handle */
	__u64 presumed;       /* in/out, presumed buffer address */
};

/* Valid submit ioctl flags: */
#define MSM_SUBMIT_NO_IMPLICIT   0x80000000 /* disable implicit sync */
#define MSM_SUBMIT_FENCE_FD_IN   0x40000000 /* enable input fence_fd */
#define MSM_SUBMIT_FENCE_FD_OUT  0x20000000 /* enable output fence_fd */
#define MSM_SUBMIT_SUDO          0x10000000 /* run submitted cmds from RB */
#define MSM_SUBMIT_SYNCOBJ_IN    0x08000000 /* enable input syncobj */
#define MSM_SUBMIT_SYNCOBJ_OUT   0x04000000 /* enable output syncobj */
#define MSM_SUBMIT_FENCE_SN_IN   0x02000000 /* userspace passes in seqno fence */
#define MSM_SUBMIT_FLAGS                ( \
		MSM_SUBMIT_NO_IMPLICIT   | \
		MSM_SUBMIT_FENCE_FD_IN   | \
		MSM_SUBMIT_FENCE_FD_OUT  | \
		MSM_SUBMIT_SUDO          | \
		MSM_SUBMIT_SYNCOBJ_IN    | \
		MSM_SUBMIT_SYNCOBJ_OUT   | \
		MSM_SUBMIT_FENCE_SN_IN   | \
		0)

#define MSM_SUBMIT_SYNCOBJ_RESET 0x00000001 /* Reset syncobj after wait. */
#define MSM_SUBMIT_SYNCOBJ_FLAGS        ( \
		MSM_SUBMIT_SYNCOBJ_RESET | \
		0)

struct drm_msm_gem_submit_syncobj {
	__u32 handle;     /* in, syncobj handle. */
	__u32 flags;      /* in, from MSM_SUBMIT_SYNCOBJ_FLAGS */
	__u64 point;      /* in, timepoint for timeline syncobjs. */
};

/* Each cmdstream submit consists of a table of buffers involved, and
 * one or more cmdstream buffers.  This allows for conditional execution
 * (context-restore), and IB buffers needed for per tile/bin draw cmds.
 */
struct drm_msm_gem_submit {
	__u32 flags;          /* MSM_PIPE_x | MSM_SUBMIT_x */
	__u32 fence;          /* out (or in with MSM_SUBMIT_FENCE_SN_IN flag) */
	__u32 nr_bos;         /* in, number of submit_bo's */
	__u32 nr_cmds;        /* in, number of submit_cmd's */
	__u64 bos;            /* in, ptr to array of submit_bo's */
	__u64 cmds;           /* in, ptr to array of submit_cmd's */
	__s32 fence_fd;       /* in/out fence fd (see MSM_SUBMIT_FENCE_FD_IN/OUT) */
	__u32 queueid;        /* in, submitqueue id */
	__u64 in_syncobjs;    /* in, ptr to array of drm_msm_gem_submit_syncobj */
	__u64 out_syncobjs;   /* in, ptr to array of drm_msm_gem_submit_syncobj */
	__u32 nr_in_syncobjs; /* in, number of entries in in_syncobj */
	__u32 nr_out_syncobjs; /* in, number of entries in out_syncobj. */
	__u32 syncobj_stride; /* in, stride of syncobj arrays. */
	__u32 pad;            /*in, reserved for future use, always 0. */

};

#define MSM_WAIT_FENCE_BOOST	0x00000001
#define MSM_WAIT_FENCE_FLAGS	( \
		MSM_WAIT_FENCE_BOOST | \
		0)

/* The normal way to synchronize with the GPU is just to CPU_PREP on
 * a buffer if you need to access it from the CPU (other cmdstream
 * submission from same or other contexts, PAGE_FLIP ioctl, etc, all
 * handle the required synchronization under the hood).  This ioctl
 * mainly just exists as a way to implement the gallium pipe_fence
 * APIs without requiring a dummy bo to synchronize on.
 */
struct drm_msm_wait_fence {
	__u32 fence;          /* in */
	__u32 flags;          /* in, bitmask of MSM_WAIT_FENCE_x */
	struct drm_msm_timespec timeout;   /* in */
	__u32 queueid;         /* in, submitqueue id */
};

/* madvise provides a way to tell the kernel in case a buffers contents
 * can be discarded under memory pressure, which is useful for userspace
 * bo cache where we want to optimistically hold on to buffer allocate
 * and potential mmap, but allow the pages to be discarded under memory
 * pressure.
 *
 * Typical usage would involve madvise(DONTNEED) when buffer enters BO
 * cache, and madvise(WILLNEED) if trying to recycle buffer from BO cache.
 * In the WILLNEED case, 'retained' indicates to userspace whether the
 * backing pages still exist.
 */
#define MSM_MADV_WILLNEED 0       /* backing pages are needed, status returned in 'retained' */
#define MSM_MADV_DONTNEED 1       /* backing pages not needed */
#define __MSM_MADV_PURGED 2       /* internal state */

struct drm_msm_gem_madvise {
	__u32 handle;         /* in, GEM handle */
	__u32 madv;           /* in, MSM_MADV_x */
	__u32 retained;       /* out, whether backing store still exists */
};

/*
 * Draw queues allow the user to set specific submission parameter. Command
 * submissions specify a specific submitqueue to use.  ID 0 is reserved for
 * backwards compatibility as a "default" submitqueue
 */

#define MSM_SUBMITQUEUE_ALLOW_PREEMPT	0x00000001
#define MSM_SUBMITQUEUE_FLAGS		    ( \
		MSM_SUBMITQUEUE_ALLOW_PREEMPT | \
		0)

/*
 * The submitqueue priority should be between 0 and MSM_PARAM_PRIORITIES-1,
 * a lower numeric value is higher priority.
 */
struct drm_msm_submitqueue {
	__u32 flags;   /* in, MSM_SUBMITQUEUE_x */
	__u32 prio;    /* in, Priority level */
	__u32 id;      /* out, identifier */
};

#define MSM_SUBMITQUEUE_PARAM_FAULTS   0

struct drm_msm_submitqueue_query {
	__u64 data;
	__u32 id;
	__u32 param;
	__u32 len;
	__u32 pad;
};

#define DRM_MSM_GET_PARAM              0x00
#define DRM_MSM_SET_PARAM              0x01
#define DRM_MSM_GEM_NEW                0x02
#define DRM_MSM_GEM_INFO               0x03
#define DRM_MSM_GEM_CPU_PREP           0x04
#define DRM_MSM_GEM_CPU_FINI           0x05
#define DRM_MSM_GEM_SUBMIT             0x06
#define DRM_MSM_WAIT_FENCE             0x07
#define DRM_MSM_GEM_MADVISE            0x08
/* placeholder:
#define DRM_MSM_GEM_SVM_NEW            0x09
 */
#define DRM_MSM_SUBMITQUEUE_NEW        0x0A
#define DRM_MSM_SUBMITQUEUE_CLOSE      0x0B
#define DRM_MSM_SUBMITQUEUE_QUERY      0x0C

#define DRM_IOCTL_MSM_GET_PARAM        DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GET_PARAM, struct drm_msm_param)
#define DRM_IOCTL_MSM_SET_PARAM        DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_SET_PARAM, struct drm_msm_param)
#define DRM_IOCTL_MSM_GEM_NEW          DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_NEW, struct drm_msm_gem_new)
#define DRM_IOCTL_MSM_GEM_INFO         DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_INFO, struct drm_msm_gem_info)
#define DRM_IOCTL_MSM_GEM_CPU_PREP     DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_GEM_CPU_PREP, struct drm_msm_gem_cpu_prep)
#define DRM_IOCTL_MSM_GEM_CPU_FINI     DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_GEM_CPU_FINI, struct drm_msm_gem_cpu_fini)
#define DRM_IOCTL_MSM_GEM_SUBMIT       DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_SUBMIT, struct drm_msm_gem_submit)
#define DRM_IOCTL_MSM_WAIT_FENCE       DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_WAIT_FENCE, struct drm_msm_wait_fence)
#define DRM_IOCTL_MSM_GEM_MADVISE      DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_GEM_MADVISE, struct drm_msm_gem_madvise)
#define DRM_IOCTL_MSM_SUBMITQUEUE_NEW    DRM_IOWR(DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_NEW, struct drm_msm_submitqueue)
#define DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE  DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_CLOSE, __u32)
#define DRM_IOCTL_MSM_SUBMITQUEUE_QUERY  DRM_IOW (DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_QUERY, struct drm_msm_submitqueue_query)

#if defined(__cplusplus)
}
#endif

#endif /* __MSM_DRM_H__ */
