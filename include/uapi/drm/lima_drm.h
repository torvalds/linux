/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT */
/* Copyright 2017-2018 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_DRM_H__
#define __LIMA_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

enum drm_lima_param_gpu_id {
	DRM_LIMA_PARAM_GPU_ID_UNKNOWN,
	DRM_LIMA_PARAM_GPU_ID_MALI400,
	DRM_LIMA_PARAM_GPU_ID_MALI450,
};

enum drm_lima_param {
	DRM_LIMA_PARAM_GPU_ID,
	DRM_LIMA_PARAM_NUM_PP,
	DRM_LIMA_PARAM_GP_VERSION,
	DRM_LIMA_PARAM_PP_VERSION,
};

/**
 * get various information of the GPU
 */
struct drm_lima_get_param {
	__u32 param; /* in, value in enum drm_lima_param */
	__u32 pad;   /* pad, must be zero */
	__u64 value; /* out, parameter value */
};

/**
 * create a buffer for used by GPU
 */
struct drm_lima_gem_create {
	__u32 size;    /* in, buffer size */
	__u32 flags;   /* in, currently no flags, must be zero */
	__u32 handle;  /* out, GEM buffer handle */
	__u32 pad;     /* pad, must be zero */
};

/**
 * get information of a buffer
 */
struct drm_lima_gem_info {
	__u32 handle;  /* in, GEM buffer handle */
	__u32 va;      /* out, virtual address mapped into GPU MMU */
	__u64 offset;  /* out, used to mmap this buffer to CPU */
};

#define LIMA_SUBMIT_BO_READ   0x01
#define LIMA_SUBMIT_BO_WRITE  0x02

/* buffer information used by one task */
struct drm_lima_gem_submit_bo {
	__u32 handle;  /* in, GEM buffer handle */
	__u32 flags;   /* in, buffer read/write by GPU */
};

#define LIMA_GP_FRAME_REG_NUM 6

/* frame used to setup GP for each task */
struct drm_lima_gp_frame {
	__u32 frame[LIMA_GP_FRAME_REG_NUM];
};

#define LIMA_PP_FRAME_REG_NUM 23
#define LIMA_PP_WB_REG_NUM 12

/* frame used to setup mali400 GPU PP for each task */
struct drm_lima_m400_pp_frame {
	__u32 frame[LIMA_PP_FRAME_REG_NUM];
	__u32 num_pp;
	__u32 wb[3 * LIMA_PP_WB_REG_NUM];
	__u32 plbu_array_address[4];
	__u32 fragment_stack_address[4];
};

/* frame used to setup mali450 GPU PP for each task */
struct drm_lima_m450_pp_frame {
	__u32 frame[LIMA_PP_FRAME_REG_NUM];
	__u32 num_pp;
	__u32 wb[3 * LIMA_PP_WB_REG_NUM];
	__u32 use_dlbu;
	__u32 _pad;
	union {
		__u32 plbu_array_address[8];
		__u32 dlbu_regs[4];
	};
	__u32 fragment_stack_address[8];
};

#define LIMA_PIPE_GP  0x00
#define LIMA_PIPE_PP  0x01

#define LIMA_SUBMIT_FLAG_EXPLICIT_FENCE (1 << 0)

/**
 * submit a task to GPU
 *
 * User can always merge multi sync_file and drm_syncobj
 * into one drm_syncobj as in_sync[0], but we reserve
 * in_sync[1] for another task's out_sync to avoid the
 * export/import/merge pass when explicit sync.
 */
struct drm_lima_gem_submit {
	__u32 ctx;         /* in, context handle task is submitted to */
	__u32 pipe;        /* in, which pipe to use, GP/PP */
	__u32 nr_bos;      /* in, array length of bos field */
	__u32 frame_size;  /* in, size of frame field */
	__u64 bos;         /* in, array of drm_lima_gem_submit_bo */
	__u64 frame;       /* in, GP/PP frame */
	__u32 flags;       /* in, submit flags */
	__u32 out_sync;    /* in, drm_syncobj handle used to wait task finish after submission */
	__u32 in_sync[2];  /* in, drm_syncobj handle used to wait before start this task */
};

#define LIMA_GEM_WAIT_READ   0x01
#define LIMA_GEM_WAIT_WRITE  0x02

/**
 * wait pending GPU task finish of a buffer
 */
struct drm_lima_gem_wait {
	__u32 handle;      /* in, GEM buffer handle */
	__u32 op;          /* in, CPU want to read/write this buffer */
	__s64 timeout_ns;  /* in, wait timeout in absulute time */
};

/**
 * create a context
 */
struct drm_lima_ctx_create {
	__u32 id;          /* out, context handle */
	__u32 _pad;        /* pad, must be zero */
};

/**
 * free a context
 */
struct drm_lima_ctx_free {
	__u32 id;          /* in, context handle */
	__u32 _pad;        /* pad, must be zero */
};

#define DRM_LIMA_GET_PARAM   0x00
#define DRM_LIMA_GEM_CREATE  0x01
#define DRM_LIMA_GEM_INFO    0x02
#define DRM_LIMA_GEM_SUBMIT  0x03
#define DRM_LIMA_GEM_WAIT    0x04
#define DRM_LIMA_CTX_CREATE  0x05
#define DRM_LIMA_CTX_FREE    0x06

#define DRM_IOCTL_LIMA_GET_PARAM DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GET_PARAM, struct drm_lima_get_param)
#define DRM_IOCTL_LIMA_GEM_CREATE DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_CREATE, struct drm_lima_gem_create)
#define DRM_IOCTL_LIMA_GEM_INFO DRM_IOWR(DRM_COMMAND_BASE + DRM_LIMA_GEM_INFO, struct drm_lima_gem_info)
#define DRM_IOCTL_LIMA_GEM_SUBMIT DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_GEM_SUBMIT, struct drm_lima_gem_submit)
#define DRM_IOCTL_LIMA_GEM_WAIT DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_GEM_WAIT, struct drm_lima_gem_wait)
#define DRM_IOCTL_LIMA_CTX_CREATE DRM_IOR(DRM_COMMAND_BASE + DRM_LIMA_CTX_CREATE, struct drm_lima_ctx_create)
#define DRM_IOCTL_LIMA_CTX_FREE DRM_IOW(DRM_COMMAND_BASE + DRM_LIMA_CTX_FREE, struct drm_lima_ctx_free)

#if defined(__cplusplus)
}
#endif

#endif /* __LIMA_DRM_H__ */
