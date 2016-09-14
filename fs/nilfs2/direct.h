/*
 * direct.h - NILFS direct block pointer.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Koji Sato.
 */

#ifndef _NILFS_DIRECT_H
#define _NILFS_DIRECT_H

#include <linux/types.h>
#include <linux/buffer_head.h>
#include "bmap.h"


/**
 * struct nilfs_direct_node - direct node
 * @dn_flags: flags
 * @dn_pad: padding
 */
struct nilfs_direct_node {
	__u8 dn_flags;
	__u8 pad[7];
};

#define NILFS_DIRECT_NBLOCKS	(NILFS_BMAP_SIZE / sizeof(__le64) - 1)
#define NILFS_DIRECT_KEY_MIN	0
#define NILFS_DIRECT_KEY_MAX	(NILFS_DIRECT_NBLOCKS - 1)


int nilfs_direct_init(struct nilfs_bmap *);
int nilfs_direct_delete_and_convert(struct nilfs_bmap *, __u64, __u64 *,
				    __u64 *, int);


#endif	/* _NILFS_DIRECT_H */
