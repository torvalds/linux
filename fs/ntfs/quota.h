/*
 * quota.h - Defines for NTFS kernel quota ($Quota) handling.  Part of the
 *	     Linux-NTFS project.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_QUOTA_H
#define _LINUX_NTFS_QUOTA_H

#ifdef NTFS_RW

#include "types.h"
#include "volume.h"

extern bool ntfs_mark_quotas_out_of_date(ntfs_volume *vol);

#endif /* NTFS_RW */

#endif /* _LINUX_NTFS_QUOTA_H */
