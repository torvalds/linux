/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Access types and helpers
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2024-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_ACCESS_H
#define _SECURITY_LANDLOCK_ACCESS_H

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/kernel.h>
#include <uapi/linux/landlock.h>

#include "limits.h"

/*
 * All access rights that are denied by default whether they are handled or not
 * by a ruleset/layer.  This must be ORed with all ruleset->access_masks[]
 * entries when we need to get the absolute handled access masks, see
 * landlock_upgrade_handled_access_masks().
 */
/* clang-format off */
#define _LANDLOCK_ACCESS_FS_INITIALLY_DENIED ( \
	LANDLOCK_ACCESS_FS_REFER)
/* clang-format on */

/* clang-format off */
#define _LANDLOCK_ACCESS_FS_OPTIONAL ( \
	LANDLOCK_ACCESS_FS_TRUNCATE | \
	LANDLOCK_ACCESS_FS_IOCTL_DEV)
/* clang-format on */

typedef u16 access_mask_t;

/* Makes sure all filesystem access rights can be stored. */
static_assert(BITS_PER_TYPE(access_mask_t) >= LANDLOCK_NUM_ACCESS_FS);
/* Makes sure all network access rights can be stored. */
static_assert(BITS_PER_TYPE(access_mask_t) >= LANDLOCK_NUM_ACCESS_NET);
/* Makes sure all scoped rights can be stored. */
static_assert(BITS_PER_TYPE(access_mask_t) >= LANDLOCK_NUM_SCOPE);
/* Makes sure for_each_set_bit() and for_each_clear_bit() calls are OK. */
static_assert(sizeof(unsigned long) >= sizeof(access_mask_t));

/* Ruleset access masks. */
struct access_masks {
	access_mask_t fs : LANDLOCK_NUM_ACCESS_FS;
	access_mask_t net : LANDLOCK_NUM_ACCESS_NET;
	access_mask_t scope : LANDLOCK_NUM_SCOPE;
};

union access_masks_all {
	struct access_masks masks;
	u32 all;
};

/* Makes sure all fields are covered. */
static_assert(sizeof(typeof_member(union access_masks_all, masks)) ==
	      sizeof(typeof_member(union access_masks_all, all)));

/**
 * struct layer_access_masks - A boolean matrix of layers and access rights
 *
 * This has a bit for each combination of layer numbers and access rights.
 * During access checks, it is used to represent the access rights for each
 * layer which still need to be fulfilled.  When all bits are 0, the access
 * request is considered to be fulfilled.
 */
struct layer_access_masks {
	/**
	 * @access: The unfulfilled access rights for each layer.
	 */
	access_mask_t access[LANDLOCK_MAX_NUM_LAYERS];
};

/*
 * Tracks domains responsible of a denied access.  This avoids storing in each
 * object the full matrix of per-layer unfulfilled access rights, which is
 * required by update_request().
 *
 * Each nibble represents the layer index of the newest layer which denied a
 * certain access right.  For file system access rights, the upper four bits are
 * the index of the layer which denies LANDLOCK_ACCESS_FS_IOCTL_DEV and the
 * lower nibble represents LANDLOCK_ACCESS_FS_TRUNCATE.
 */
typedef u8 deny_masks_t;

/*
 * Makes sure all optional access rights can be tied to a layer index (cf.
 * get_deny_mask).
 */
static_assert(BITS_PER_TYPE(deny_masks_t) >=
	      (HWEIGHT(LANDLOCK_MAX_NUM_LAYERS - 1) *
	       HWEIGHT(_LANDLOCK_ACCESS_FS_OPTIONAL)));

/* LANDLOCK_MAX_NUM_LAYERS must be a power of two (cf. deny_masks_t assert). */
static_assert(HWEIGHT(LANDLOCK_MAX_NUM_LAYERS) == 1);

/* Upgrades with all initially denied by default access rights. */
static inline struct access_masks
landlock_upgrade_handled_access_masks(struct access_masks access_masks)
{
	/*
	 * All access rights that are denied by default whether they are
	 * explicitly handled or not.
	 */
	if (access_masks.fs)
		access_masks.fs |= _LANDLOCK_ACCESS_FS_INITIALLY_DENIED;

	return access_masks;
}

/* Checks the subset relation between access masks. */
static inline bool access_mask_subset(access_mask_t subset,
				      access_mask_t superset)
{
	return (subset | superset) == superset;
}

#endif /* _SECURITY_LANDLOCK_ACCESS_H */
