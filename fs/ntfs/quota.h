/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for NTFS kernel quota ($Quota) handling.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_QUOTA_H
#define _LINUX_NTFS_QUOTA_H

#include "volume.h"

bool ntfs_mark_quotas_out_of_date(struct ntfs_volume *vol);

#endif /* _LINUX_NTFS_QUOTA_H */
