/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __HASH__
#define __HASH__

int __init btrfs_hash_init(void);

void btrfs_hash_exit(void);

u32 btrfs_crc32c(u32 crc, const void *address, unsigned int length);

static inline u64 btrfs_name_hash(const char *name, int len)
{
	return btrfs_crc32c((u32)~1, name, len);
}

/*
 * Figure the key offset of an extended inode ref
 */
static inline u64 btrfs_extref_hash(u64 parent_objectid, const char *name,
				    int len)
{
	return (u64) btrfs_crc32c(parent_objectid, name, len);
}

#endif
