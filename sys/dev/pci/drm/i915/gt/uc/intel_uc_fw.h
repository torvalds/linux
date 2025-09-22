/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#ifndef _INTEL_UC_FW_H_
#define _INTEL_UC_FW_H_

#include <linux/sizes.h>
#include <linux/types.h>
#include "intel_uc_fw_abi.h"
#include "intel_device_info.h"
#include "i915_gem.h"
#include "i915_vma.h"

struct drm_printer;
struct drm_i915_private;
struct intel_gt;

/* Home of GuC, HuC and DMC firmwares */
#define INTEL_UC_FIRMWARE_URL "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/i915"

/*
 * +------------+---------------------------------------------------+
 * |   PHASE    |           FIRMWARE STATUS TRANSITIONS             |
 * +============+===================================================+
 * |            |               UNINITIALIZED                       |
 * +------------+-               /   |   \                         -+
 * |            |   DISABLED <--/    |    \--> NOT_SUPPORTED        |
 * | init_early |                    V                              |
 * |            |                 SELECTED                          |
 * +------------+-               /   |   \                         -+
 * |            |    MISSING <--/    |    \--> ERROR                |
 * |   fetch    |                    V                              |
 * |            |                 AVAILABLE                         |
 * +------------+-                   |   \                         -+
 * |            |                    |    \--> INIT FAIL            |
 * |   init     |                    V                              |
 * |            |        /------> LOADABLE <----<-----------\       |
 * +------------+-       \         /    \        \           \     -+
 * |            |    LOAD FAIL <--<      \--> TRANSFERRED     \     |
 * |   upload   |                  \           /   \          /     |
 * |            |                   \---------/     \--> RUNNING    |
 * +------------+---------------------------------------------------+
 */

enum intel_uc_fw_status {
	INTEL_UC_FIRMWARE_NOT_SUPPORTED = -1, /* no uc HW */
	INTEL_UC_FIRMWARE_UNINITIALIZED = 0, /* used to catch checks done too early */
	INTEL_UC_FIRMWARE_DISABLED, /* disabled */
	INTEL_UC_FIRMWARE_SELECTED, /* selected the blob we want to load */
	INTEL_UC_FIRMWARE_MISSING, /* blob not found on the system */
	INTEL_UC_FIRMWARE_ERROR, /* invalid format or version */
	INTEL_UC_FIRMWARE_AVAILABLE, /* blob found and copied in mem */
	INTEL_UC_FIRMWARE_INIT_FAIL, /* failed to prepare fw objects for load */
	INTEL_UC_FIRMWARE_LOADABLE, /* all fw-required objects are ready */
	INTEL_UC_FIRMWARE_LOAD_FAIL, /* failed to xfer or init/auth the fw */
	INTEL_UC_FIRMWARE_TRANSFERRED, /* dma xfer done */
	INTEL_UC_FIRMWARE_RUNNING /* init/auth done */
};

enum intel_uc_fw_type {
	INTEL_UC_FW_TYPE_GUC = 0,
	INTEL_UC_FW_TYPE_HUC,
	INTEL_UC_FW_TYPE_GSC,
};
#define INTEL_UC_FW_NUM_TYPES 3

struct intel_uc_fw_ver {
	u32 major;
	u32 minor;
	u32 patch;
	u32 build;
};

/*
 * The firmware build process will generate a version header file with major and
 * minor version defined. The versions are built into CSS header of firmware.
 * i915 kernel driver set the minimal firmware version required per platform.
 */
struct intel_uc_fw_file {
	const char *path;
	struct intel_uc_fw_ver ver;
};

/*
 * This structure encapsulates all the data needed during the process
 * of fetching, caching, and loading the firmware image into the uC.
 */
struct intel_uc_fw {
	enum intel_uc_fw_type type;
	union {
		const enum intel_uc_fw_status status;
		enum intel_uc_fw_status __status; /* no accidental overwrites */
	};
	struct intel_uc_fw_file file_wanted;
	struct intel_uc_fw_file file_selected;
	bool user_overridden;
	size_t size;
	struct drm_i915_gem_object *obj;

	/**
	 * @needs_ggtt_mapping: indicates whether the fw object needs to be
	 * pinned to ggtt. If true, the fw is pinned at init time and unpinned
	 * during driver unload.
	 */
	bool needs_ggtt_mapping;

	/**
	 * @vma_res: A vma resource used in binding the uc fw to ggtt. The fw is
	 * pinned in a reserved area of the ggtt (above the maximum address
	 * usable by GuC); therefore, we can't use the normal vma functions to
	 * do the pinning and we instead use this resource to do so.
	 */
	struct i915_vma_resource vma_res;
	struct i915_vma *rsa_data;

	u32 rsa_size;
	u32 ucode_size;
	u32 private_data_size;

	u32 dma_start_offset;

	bool has_gsc_headers;
};

/*
 * When we load the uC binaries, we pin them in a reserved section at the top of
 * the GGTT, which is ~18 MBs. On multi-GT systems where the GTs share the GGTT,
 * we also need to make sure that each binary is pinned to a unique location
 * during load, because the different GT can go through the FW load at the same
 * time (see uc_fw_ggtt_offset() for details).
 * Given that the available space is much greater than what is required by the
 * binaries, to keep things simple instead of dynamically partitioning the
 * reserved section to make space for all the blobs we can just reserve a static
 * chunk for each binary.
 */
#define INTEL_UC_RSVD_GGTT_PER_FW SZ_2M

#ifdef CONFIG_DRM_I915_DEBUG_GUC
void intel_uc_fw_change_status(struct intel_uc_fw *uc_fw,
			       enum intel_uc_fw_status status);
#else
static inline void intel_uc_fw_change_status(struct intel_uc_fw *uc_fw,
					     enum intel_uc_fw_status status)
{
	uc_fw->__status = status;
}
#endif

static inline
const char *intel_uc_fw_status_repr(enum intel_uc_fw_status status)
{
	switch (status) {
	case INTEL_UC_FIRMWARE_NOT_SUPPORTED:
		return "N/A";
	case INTEL_UC_FIRMWARE_UNINITIALIZED:
		return "UNINITIALIZED";
	case INTEL_UC_FIRMWARE_DISABLED:
		return "DISABLED";
	case INTEL_UC_FIRMWARE_SELECTED:
		return "SELECTED";
	case INTEL_UC_FIRMWARE_MISSING:
		return "MISSING";
	case INTEL_UC_FIRMWARE_ERROR:
		return "ERROR";
	case INTEL_UC_FIRMWARE_AVAILABLE:
		return "AVAILABLE";
	case INTEL_UC_FIRMWARE_INIT_FAIL:
		return "INIT FAIL";
	case INTEL_UC_FIRMWARE_LOADABLE:
		return "LOADABLE";
	case INTEL_UC_FIRMWARE_LOAD_FAIL:
		return "LOAD FAIL";
	case INTEL_UC_FIRMWARE_TRANSFERRED:
		return "TRANSFERRED";
	case INTEL_UC_FIRMWARE_RUNNING:
		return "RUNNING";
	}
	return "<invalid>";
}

static inline int intel_uc_fw_status_to_error(enum intel_uc_fw_status status)
{
	switch (status) {
	case INTEL_UC_FIRMWARE_NOT_SUPPORTED:
		return -ENODEV;
	case INTEL_UC_FIRMWARE_UNINITIALIZED:
		return -EACCES;
	case INTEL_UC_FIRMWARE_DISABLED:
		return -EPERM;
	case INTEL_UC_FIRMWARE_MISSING:
		return -ENOENT;
	case INTEL_UC_FIRMWARE_ERROR:
		return -ENOEXEC;
	case INTEL_UC_FIRMWARE_INIT_FAIL:
	case INTEL_UC_FIRMWARE_LOAD_FAIL:
		return -EIO;
	case INTEL_UC_FIRMWARE_SELECTED:
		return -ESTALE;
	case INTEL_UC_FIRMWARE_AVAILABLE:
	case INTEL_UC_FIRMWARE_LOADABLE:
	case INTEL_UC_FIRMWARE_TRANSFERRED:
	case INTEL_UC_FIRMWARE_RUNNING:
		return 0;
	}
	return -EINVAL;
}

static inline const char *intel_uc_fw_type_repr(enum intel_uc_fw_type type)
{
	switch (type) {
	case INTEL_UC_FW_TYPE_GUC:
		return "GuC";
	case INTEL_UC_FW_TYPE_HUC:
		return "HuC";
	case INTEL_UC_FW_TYPE_GSC:
		return "GSC";
	}
	return "uC";
}

static inline enum intel_uc_fw_status
__intel_uc_fw_status(struct intel_uc_fw *uc_fw)
{
	/* shouldn't call this before checking hw/blob availability */
	GEM_BUG_ON(uc_fw->status == INTEL_UC_FIRMWARE_UNINITIALIZED);
	return uc_fw->status;
}

static inline bool intel_uc_fw_is_supported(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) != INTEL_UC_FIRMWARE_NOT_SUPPORTED;
}

static inline bool intel_uc_fw_is_enabled(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) > INTEL_UC_FIRMWARE_DISABLED;
}

static inline bool intel_uc_fw_is_available(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) >= INTEL_UC_FIRMWARE_AVAILABLE;
}

static inline bool intel_uc_fw_is_loadable(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) >= INTEL_UC_FIRMWARE_LOADABLE;
}

static inline bool intel_uc_fw_is_loaded(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) >= INTEL_UC_FIRMWARE_TRANSFERRED;
}

static inline bool intel_uc_fw_is_running(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) == INTEL_UC_FIRMWARE_RUNNING;
}

static inline bool intel_uc_fw_is_in_error(struct intel_uc_fw *uc_fw)
{
	return intel_uc_fw_status_to_error(__intel_uc_fw_status(uc_fw)) != 0;
}

static inline bool intel_uc_fw_is_overridden(const struct intel_uc_fw *uc_fw)
{
	return uc_fw->user_overridden;
}

static inline void intel_uc_fw_sanitize(struct intel_uc_fw *uc_fw)
{
	if (intel_uc_fw_is_loaded(uc_fw))
		intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_LOADABLE);
}

static inline u32 __intel_uc_fw_get_upload_size(struct intel_uc_fw *uc_fw)
{
	return sizeof(struct uc_css_header) + uc_fw->ucode_size;
}

/**
 * intel_uc_fw_get_upload_size() - Get size of firmware needed to be uploaded.
 * @uc_fw: uC firmware.
 *
 * Get the size of the firmware and header that will be uploaded to WOPCM.
 *
 * Return: Upload firmware size, or zero on firmware fetch failure.
 */
static inline u32 intel_uc_fw_get_upload_size(struct intel_uc_fw *uc_fw)
{
	if (!intel_uc_fw_is_available(uc_fw))
		return 0;

	return __intel_uc_fw_get_upload_size(uc_fw);
}

void intel_uc_fw_version_from_gsc_manifest(struct intel_uc_fw_ver *ver,
					   const void *data);
int intel_uc_check_file_version(struct intel_uc_fw *uc_fw, bool *old_ver);
void intel_uc_fw_init_early(struct intel_uc_fw *uc_fw,
			    enum intel_uc_fw_type type,
			    bool needs_ggtt_mapping);
int intel_uc_fw_fetch(struct intel_uc_fw *uc_fw);
void intel_uc_fw_cleanup_fetch(struct intel_uc_fw *uc_fw);
int intel_uc_fw_upload(struct intel_uc_fw *uc_fw, u32 offset, u32 dma_flags);
int intel_uc_fw_init(struct intel_uc_fw *uc_fw);
void intel_uc_fw_fini(struct intel_uc_fw *uc_fw);
void intel_uc_fw_resume_mapping(struct intel_uc_fw *uc_fw);
size_t intel_uc_fw_copy_rsa(struct intel_uc_fw *uc_fw, void *dst, u32 max_len);
int intel_uc_fw_mark_load_failed(struct intel_uc_fw *uc_fw, int err);
void intel_uc_fw_dump(const struct intel_uc_fw *uc_fw, struct drm_printer *p);

#endif
