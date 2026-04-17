/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for NTFS kernel collation handling.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 *
 * Part of this file is based on code from the NTFS-3G.
 * and is copyrighted by the respective authors below:
 * Copyright (c) 2004 Anton Altaparmakov
 * Copyright (c) 2005 Yura Pakhuchiy
 */

#ifndef _LINUX_NTFS_COLLATE_H
#define _LINUX_NTFS_COLLATE_H

#include "volume.h"

static inline bool ntfs_is_collation_rule_supported(__le32 cr)
{
	int i;

	if (unlikely(cr != COLLATION_BINARY && cr != COLLATION_NTOFS_ULONG &&
		     cr != COLLATION_FILE_NAME) && cr != COLLATION_NTOFS_ULONGS)
		return false;
	i = le32_to_cpu(cr);
	if (likely(((i >= 0) && (i <= 0x02)) ||
			((i >= 0x10) && (i <= 0x13))))
		return true;
	return false;
}

int ntfs_collate(struct ntfs_volume *vol, __le32 cr,
		const void *data1, const u32 data1_len,
		const void *data2, const u32 data2_len);

#endif /* _LINUX_NTFS_COLLATE_H */
