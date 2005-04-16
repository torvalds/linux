/*
 * Type definitions for the multi-level security (MLS) policy.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
/*
 * Updated: Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 *	Support for enhanced MLS infrastructure.
 *
 * Copyright (C) 2004-2005 Trusted Computer Solutions, Inc.
 */

#ifndef _SS_MLS_TYPES_H_
#define _SS_MLS_TYPES_H_

#include "security.h"

struct mls_level {
	u32 sens;		/* sensitivity */
	struct ebitmap cat;	/* category set */
};

struct mls_range {
	struct mls_level level[2]; /* low == level[0], high == level[1] */
};

static inline int mls_level_eq(struct mls_level *l1, struct mls_level *l2)
{
	if (!selinux_mls_enabled)
		return 1;

	return ((l1->sens == l2->sens) &&
	        ebitmap_cmp(&l1->cat, &l2->cat));
}

static inline int mls_level_dom(struct mls_level *l1, struct mls_level *l2)
{
	if (!selinux_mls_enabled)
		return 1;

	return ((l1->sens >= l2->sens) &&
	        ebitmap_contains(&l1->cat, &l2->cat));
}

#define mls_level_incomp(l1, l2) \
(!mls_level_dom((l1), (l2)) && !mls_level_dom((l2), (l1)))

#define mls_level_between(l1, l2, l3) \
(mls_level_dom((l1), (l2)) && mls_level_dom((l3), (l1)))

#define mls_range_contains(r1, r2) \
(mls_level_dom(&(r2).level[0], &(r1).level[0]) && \
 mls_level_dom(&(r1).level[1], &(r2).level[1]))

#endif	/* _SS_MLS_TYPES_H_ */
