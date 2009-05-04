/*
 * bmap_union.h - NILFS block mapping.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 */

#ifndef _NILFS_BMAP_UNION_H
#define _NILFS_BMAP_UNION_H

#include "bmap.h"
#include "direct.h"
#include "btree.h"

/**
 * nilfs_bmap_union -
 * @bi_bmap: bmap structure
 * @bi_btree: direct map structure
 * @bi_direct: B-tree structure
 */
union nilfs_bmap_union {
	struct nilfs_bmap bi_bmap;
	struct nilfs_direct bi_direct;
	struct nilfs_btree bi_btree;
};

#endif	/* _NILFS_BMAP_UNION_H */
