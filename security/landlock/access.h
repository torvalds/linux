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

typedef u16 layer_mask_t;

/* Makes sure all layers can be checked. */
static_assert(BITS_PER_TYPE(layer_mask_t) >= LANDLOCK_MAX_NUM_LAYERS);

/*
 * Tracks domains responsible of a denied access.  This is required to avoid
 * storing in each object the full layer_masks[] required by update_request().
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

#endif /* _SECURITY_LANDLOCK_ACCESS_H */
