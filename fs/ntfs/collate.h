/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * collate.h - Defines for NTFS kernel collation handling.  Part of the
 *	       Linux-NTFS project.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_COLLATE_H
#define _LINUX_NTFS_COLLATE_H

#include "types.h"
#include "volume.h"

static inline bool ntfs_is_collation_rule_supported(COLLATION_RULE cr) {
	int i;

	/*
	 * FIXME:  At the moment we only support COLLATION_BINARY and
	 * COLLATION_NTOFS_ULONG, so we return false for everything else for
	 * now.
	 */
	if (unlikely(cr != COLLATION_BINARY && cr != COLLATION_NTOFS_ULONG))
		return false;
	i = le32_to_cpu(cr);
	if (likely(((i >= 0) && (i <= 0x02)) ||
			((i >= 0x10) && (i <= 0x13))))
		return true;
	return false;
}

extern int ntfs_collate(ntfs_volume *vol, COLLATION_RULE cr,
		const void *data1, const int data1_len,
		const void *data2, const int data2_len);

#endif /* _LINUX_NTFS_COLLATE_H */
