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

typedef u32 access_mask_t;

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
} __packed __aligned(sizeof(u32));

union access_masks_all {
	struct access_masks masks;
	u32 all;
};

/* Makes sure all fields are covered. */
static_assert(sizeof(typeof_member(union access_masks_all, masks)) ==
	      sizeof(typeof_member(union access_masks_all, all)));

/**
 * struct layer_mask - The access rights and rule flags for a layer.
 *
 * This has a bit for each access rights and rule flags.  During access checks,
 * it is used to represent the access rights for each layer which still need to
 * be fulfilled.  When all bits are 0, the access request is considered to be
 * fulfilled.
 */
struct layer_mask {
	/**
	 * @access: The unfulfilled access rights for this layer.
	 */
	access_mask_t access : LANDLOCK_NUM_ACCESS_MAX;
#ifdef CONFIG_AUDIT
	/**
	 * @quiet: Whether we have encountered a rule with the quiet flag for
	 * this layer.  Used to control logging.
	 */
	access_mask_t quiet : 1;
#endif /* CONFIG_AUDIT */
} __packed __aligned(sizeof(access_mask_t));

/*
 * Make sure that we don't increase the size of struct layer_mask when storing
 * rule flags.
 */
static_assert(sizeof(struct layer_mask) == sizeof(access_mask_t));

/**
 * struct layer_masks - An array of struct layer_mask, one per layer.
 */
struct layer_masks {
	/**
	 * @layers: The unfulfilled access rights for each layer.
	 */
	struct layer_mask layers[LANDLOCK_MAX_NUM_LAYERS];
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

/* A bitmask that is large enough to hold set of optional accesses. */
typedef u8 optional_access_t;
static_assert(BITS_PER_TYPE(optional_access_t) >=
	      HWEIGHT(_LANDLOCK_ACCESS_FS_OPTIONAL));

#endif /* _SECURITY_LANDLOCK_ACCESS_H */
