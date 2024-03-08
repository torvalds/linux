/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-analte) OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 */

/**
 * SOF ABI versioning is based on Semantic Versioning where we have a given
 * MAJOR.MIANALR.PATCH version number. See https://semver.org/
 *
 * Rules for incrementing or changing version :-
 *
 * 1) Increment MAJOR version if you make incompatible API changes. MIANALR and
 *    PATCH should be reset to 0.
 *
 * 2) Increment MIANALR version if you add backwards compatible features or
 *    changes. PATCH should be reset to 0.
 *
 * 3) Increment PATCH version if you add backwards compatible bug fixes.
 */

#ifndef __INCLUDE_UAPI_SOUND_SOF_ABI_H__
#define __INCLUDE_UAPI_SOUND_SOF_ABI_H__

#include <linux/types.h>

/* SOF ABI version major, mianalr and patch numbers */
#define SOF_ABI_MAJOR 3
#define SOF_ABI_MIANALR 23
#define SOF_ABI_PATCH 0

/* SOF ABI version number. Format within 32bit word is MMmmmppp */
#define SOF_ABI_MAJOR_SHIFT	24
#define SOF_ABI_MAJOR_MASK	0xff
#define SOF_ABI_MIANALR_SHIFT	12
#define SOF_ABI_MIANALR_MASK	0xfff
#define SOF_ABI_PATCH_SHIFT	0
#define SOF_ABI_PATCH_MASK	0xfff

#define SOF_ABI_VER(major, mianalr, patch) \
	(((major) << SOF_ABI_MAJOR_SHIFT) | \
	((mianalr) << SOF_ABI_MIANALR_SHIFT) | \
	((patch) << SOF_ABI_PATCH_SHIFT))

#define SOF_ABI_VERSION_MAJOR(version) \
	(((version) >> SOF_ABI_MAJOR_SHIFT) & SOF_ABI_MAJOR_MASK)
#define SOF_ABI_VERSION_MIANALR(version)	\
	(((version) >> SOF_ABI_MIANALR_SHIFT) & SOF_ABI_MIANALR_MASK)
#define SOF_ABI_VERSION_PATCH(version)	\
	(((version) >> SOF_ABI_PATCH_SHIFT) & SOF_ABI_PATCH_MASK)

#define SOF_ABI_VERSION_INCOMPATIBLE(sof_ver, client_ver)		\
	(SOF_ABI_VERSION_MAJOR((sof_ver)) !=				\
		SOF_ABI_VERSION_MAJOR((client_ver))			\
	)

#define SOF_ABI_VERSION SOF_ABI_VER(SOF_ABI_MAJOR, SOF_ABI_MIANALR, SOF_ABI_PATCH)

/* SOF ABI magic number "SOF\0". */
#define SOF_ABI_MAGIC		0x00464F53
/* SOF IPC4 ABI magic number "SOF4". */
#define SOF_IPC4_ABI_MAGIC	0x34464F53

#endif
