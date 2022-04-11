/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2015 Etnaviv Project
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

#ifndef __ETNAVIV_DRM_H__
#define __ETNAVIV_DRM_H__

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

/* timeouts are specified in clock-monotonic absolute times (to simplify
 * restarting interrupted ioctls).  The following struct is logically the
 * same as 'struct timespec' but 32/64b ABI safe.
 */
struct drm_etnaviv_timespec {
	__s64 tv_sec;          /* seconds */
	__s64 tv_nsec;         /* nanoseconds */
};

#define ETNAVIV_PARAM_GPU_MODEL                     0x01
#define ETNAVIV_PARAM_GPU_REVISION                  0x02
#define ETNAVIV_PARAM_GPU_FEATURES_0                0x03
#define ETNAVIV_PARAM_GPU_FEATURES_1                0x04
#define ETNAVIV_PARAM_GPU_FEATURES_2                0x05
#define ETNAVIV_PARAM_GPU_FEATURES_3                0x06
#define ETNAVIV_PARAM_GPU_FEATURES_4                0x07
#define ETNAVIV_PARAM_GPU_FEATURES_5                0x08
#define ETNAVIV_PARAM_GPU_FEATURES_6                0x09
#define ETNAVIV_PARAM_GPU_FEATURES_7                0x0a
#define ETNAVIV_PARAM_GPU_FEATURES_8                0x0b
#define ETNAVIV_PARAM_GPU_FEATURES_9                0x0c
#define ETNAVIV_PARAM_GPU_FEATURES_10               0x0d
#define ETNAVIV_PARAM_GPU_FEATURES_11               0x0e
#define ETNAVIV_PARAM_GPU_FEATURES_12               0x0f

#define ETNAVIV_PARAM_GPU_STREAM_COUNT              0x10
#define ETNAVIV_PARAM_GPU_REGISTER_MAX              0x11
#define ETNAVIV_PARAM_GPU_THREAD_COUNT              0x12
#define ETNAVIV_PARAM_GPU_VERTEX_CACHE_SIZE         0x13
#define ETNAVIV_PARAM_GPU_SHADER_CORE_COUNT         0x14
#define ETNAVIV_PARAM_GPU_PIXEL_PIPES               0x15
#define ETNAVIV_PARAM_GPU_VERTEX_OUTPUT_BUFFER_SIZE 0x16
#define ETNAVIV_PARAM_GPU_BUFFER_SIZE               0x17
#define ETNAVIV_PARAM_GPU_INSTRUCTION_COUNT         0x18
#define ETNAVIV_PARAM_GPU_NUM_CONSTANTS             0x19
#define ETNAVIV_PARAM_GPU_NUM_VARYINGS              0x1a
#define ETNAVIV_PARAM_SOFTPIN_START_ADDR            0x1b

#define ETNA_MAX_PIPES 4

struct drm_etnaviv_param {
	__u32 pipe;           /* in */
	__u32 param;          /* in, ETNAVIV_PARAM_x */
	__u64 value;          /* out (get_param) or in (set_param) */
};

/*
 * GEM buffers:
 */

#define ETNA_BO_CACHE_MASK   0x000f0000
/* cache modes */
#define ETNA_BO_CACHED       0x00010000
#define ETNA_BO_WC           0x00020000
#define ETNA_BO_UNCACHED     0x00040000
/* map flags */
#define ETNA_BO_FORCE_MMU    0x00100000

struct drm_etnaviv_gem_new {
	__u64 size;           /* in */
	__u32 flags;          /* in, mask of ETNA_BO_x */
	__u32 handle;         /* out */
};

struct drm_etnaviv_gem_info {
	__u32 handle;         /* in */
	__u32 pad;
	__u64 offset;         /* out, offset to pass to mmap() */
};

#define ETNA_PREP_READ        0x01
#define ETNA_PREP_WRITE       0x02
#define ETNA_PREP_NOSYNC      0x04

struct drm_etnaviv_gem_cpu_prep {
	__u32 handle;         /* in */
	__u32 op;             /* in, mask of ETNA_PREP_x */
	struct drm_etnaviv_timespec timeout;   /* in */
};

struct drm_etnaviv_gem_cpu_fini {
	__u32 handle;         /* in */
	__u32 flags;          /* in, placeholder for now, no defined values */
};

/*
 * Cmdstream Submission:
 */

/* The value written into the cmdstream is logically:
 * relocbuf->gpuaddr + reloc_offset
 *
 * NOTE that reloc's must be sorted by order of increasing submit_offset,
 * otherwise EINVAL.
 */
struct drm_etnaviv_gem_submit_reloc {
	__u32 submit_offset;  /* in, offset from submit_bo */
	__u32 reloc_idx;      /* in, index of reloc_bo buffer */
	__u64 reloc_offset;   /* in, offset from start of reloc_bo */
	__u32 flags;          /* in, placeholder for now, no defined values */
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
 * If the submit is a softpin submit (ETNA_SUBMIT_SOFTPIN) the 'presumed'
 * field is interpreted as the fixed location to map the bo into the gpu
 * virtual address space. If the kernel is unable to map the buffer at
 * this location the submit will fail. This means userspace is responsible
 * for the whole gpu virtual address management.
 */
#define ETNA_SUBMIT_BO_READ             0x0001
#define ETNA_SUBMIT_BO_WRITE            0x0002
struct drm_etnaviv_gem_submit_bo {
	__u32 flags;          /* in, mask of ETNA_SUBMIT_BO_x */
	__u32 handle;         /* in, GEM handle */
	__u64 presumed;       /* in/out, presumed buffer address */
};

/* performance monitor request (pmr) */
#define ETNA_PM_PROCESS_PRE             0x0001
#define ETNA_PM_PROCESS_POST            0x0002
struct drm_etnaviv_gem_submit_pmr {
	__u32 flags;          /* in, when to process request (ETNA_PM_PROCESS_x) */
	__u8  domain;         /* in, pm domain */
	__u8  pad;
	__u16 signal;         /* in, pm signal */
	__u32 sequence;       /* in, sequence number */
	__u32 read_offset;    /* in, offset from read_bo */
	__u32 read_idx;       /* in, index of read_bo buffer */
};

/* Each cmdstream submit consists of a table of buffers involved, and
 * one or more cmdstream buffers.  This allows for conditional execution
 * (context-restore), and IB buffers needed for per tile/bin draw cmds.
 */
#define ETNA_SUBMIT_NO_IMPLICIT         0x0001
#define ETNA_SUBMIT_FENCE_FD_IN         0x0002
#define ETNA_SUBMIT_FENCE_FD_OUT        0x0004
#define ETNA_SUBMIT_SOFTPIN             0x0008
#define ETNA_SUBMIT_FLAGS		(ETNA_SUBMIT_NO_IMPLICIT | \
					 ETNA_SUBMIT_FENCE_FD_IN | \
					 ETNA_SUBMIT_FENCE_FD_OUT| \
					 ETNA_SUBMIT_SOFTPIN)
#define ETNA_PIPE_3D      0x00
#define ETNA_PIPE_2D      0x01
#define ETNA_PIPE_VG      0x02
struct drm_etnaviv_gem_submit {
	__u32 fence;          /* out */
	__u32 pipe;           /* in */
	__u32 exec_state;     /* in, initial execution state (ETNA_PIPE_x) */
	__u32 nr_bos;         /* in, number of submit_bo's */
	__u32 nr_relocs;      /* in, number of submit_reloc's */
	__u32 stream_size;    /* in, cmdstream size */
	__u64 bos;            /* in, ptr to array of submit_bo's */
	__u64 relocs;         /* in, ptr to array of submit_reloc's */
	__u64 stream;         /* in, ptr to cmdstream */
	__u32 flags;          /* in, mask of ETNA_SUBMIT_x */
	__s32 fence_fd;       /* in/out, fence fd (see ETNA_SUBMIT_FENCE_FD_x) */
	__u64 pmrs;           /* in, ptr to array of submit_pmr's */
	__u32 nr_pmrs;        /* in, number of submit_pmr's */
	__u32 pad;
};

/* The normal way to synchronize with the GPU is just to CPU_PREP on
 * a buffer if you need to access it from the CPU (other cmdstream
 * submission from same or other contexts, PAGE_FLIP ioctl, etc, all
 * handle the required synchronization under the hood).  This ioctl
 * mainly just exists as a way to implement the gallium pipe_fence
 * APIs without requiring a dummy bo to synchronize on.
 */
#define ETNA_WAIT_NONBLOCK      0x01
struct drm_etnaviv_wait_fence {
	__u32 pipe;           /* in */
	__u32 fence;          /* in */
	__u32 flags;          /* in, mask of ETNA_WAIT_x */
	__u32 pad;
	struct drm_etnaviv_timespec timeout;   /* in */
};

#define ETNA_USERPTR_READ	0x01
#define ETNA_USERPTR_WRITE	0x02
struct drm_etnaviv_gem_userptr {
	__u64 user_ptr;	/* in, page aligned user pointer */
	__u64 user_size;	/* in, page aligned user size */
	__u32 flags;		/* in, flags */
	__u32 handle;	/* out, non-zero handle */
};

struct drm_etnaviv_gem_wait {
	__u32 pipe;				/* in */
	__u32 handle;				/* in, bo to be waited for */
	__u32 flags;				/* in, mask of ETNA_WAIT_x  */
	__u32 pad;
	struct drm_etnaviv_timespec timeout;	/* in */
};

/*
 * Performance Monitor (PM):
 */

struct drm_etnaviv_pm_domain {
	__u32 pipe;       /* in */
	__u8  iter;       /* in/out, select pm domain at index iter */
	__u8  id;         /* out, id of domain */
	__u16 nr_signals; /* out, how many signals does this domain provide */
	char  name[64];   /* out, name of domain */
};

struct drm_etnaviv_pm_signal {
	__u32 pipe;       /* in */
	__u8  domain;     /* in, pm domain index */
	__u8  pad;
	__u16 iter;       /* in/out, select pm source at index iter */
	__u16 id;         /* out, id of signal */
	char  name[64];   /* out, name of domain */
};

#define DRM_ETNAVIV_GET_PARAM          0x00
/* placeholder:
#define DRM_ETNAVIV_SET_PARAM          0x01
 */
#define DRM_ETNAVIV_GEM_NEW            0x02
#define DRM_ETNAVIV_GEM_INFO           0x03
#define DRM_ETNAVIV_GEM_CPU_PREP       0x04
#define DRM_ETNAVIV_GEM_CPU_FINI       0x05
#define DRM_ETNAVIV_GEM_SUBMIT         0x06
#define DRM_ETNAVIV_WAIT_FENCE         0x07
#define DRM_ETNAVIV_GEM_USERPTR        0x08
#define DRM_ETNAVIV_GEM_WAIT           0x09
#define DRM_ETNAVIV_PM_QUERY_DOM       0x0a
#define DRM_ETNAVIV_PM_QUERY_SIG       0x0b
#define DRM_ETNAVIV_NUM_IOCTLS         0x0c

#define DRM_IOCTL_ETNAVIV_GET_PARAM    DRM_IOWR(DRM_COMMAND_BASE + DRM_ETNAVIV_GET_PARAM, struct drm_etnaviv_param)
#define DRM_IOCTL_ETNAVIV_GEM_NEW      DRM_IOWR(DRM_COMMAND_BASE + DRM_ETNAVIV_GEM_NEW, struct drm_etnaviv_gem_new)
#define DRM_IOCTL_ETNAVIV_GEM_INFO     DRM_IOWR(DRM_COMMAND_BASE + DRM_ETNAVIV_GEM_INFO, struct drm_etnaviv_gem_info)
#define DRM_IOCTL_ETNAVIV_GEM_CPU_PREP DRM_IOW(DRM_COMMAND_BASE + DRM_ETNAVIV_GEM_CPU_PREP, struct drm_etnaviv_gem_cpu_prep)
#define DRM_IOCTL_ETNAVIV_GEM_CPU_FINI DRM_IOW(DRM_COMMAND_BASE + DRM_ETNAVIV_GEM_CPU_FINI, struct drm_etnaviv_gem_cpu_fini)
#define DRM_IOCTL_ETNAVIV_GEM_SUBMIT   DRM_IOWR(DRM_COMMAND_BASE + DRM_ETNAVIV_GEM_SUBMIT, struct drm_etnaviv_gem_submit)
#define DRM_IOCTL_ETNAVIV_WAIT_FENCE   DRM_IOW(DRM_COMMAND_BASE + DRM_ETNAVIV_WAIT_FENCE, struct drm_etnaviv_wait_fence)
#define DRM_IOCTL_ETNAVIV_GEM_USERPTR  DRM_IOWR(DRM_COMMAND_BASE + DRM_ETNAVIV_GEM_USERPTR, struct drm_etnaviv_gem_userptr)
#define DRM_IOCTL_ETNAVIV_GEM_WAIT     DRM_IOW(DRM_COMMAND_BASE + DRM_ETNAVIV_GEM_WAIT, struct drm_etnaviv_gem_wait)
#define DRM_IOCTL_ETNAVIV_PM_QUERY_DOM DRM_IOWR(DRM_COMMAND_BASE + DRM_ETNAVIV_PM_QUERY_DOM, struct drm_etnaviv_pm_domain)
#define DRM_IOCTL_ETNAVIV_PM_QUERY_SIG DRM_IOWR(DRM_COMMAND_BASE + DRM_ETNAVIV_PM_QUERY_SIG, struct drm_etnaviv_pm_signal)

#if defined(__cplusplus)
}
#endif

#endif /* __ETNAVIV_DRM_H__ */
