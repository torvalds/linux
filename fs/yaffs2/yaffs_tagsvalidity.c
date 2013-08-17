/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yaffs_tagsvalidity.h"

void yaffs_init_tags(struct yaffs_ext_tags *tags)
{
	memset(tags, 0, sizeof(struct yaffs_ext_tags));
	tags->validity0 = 0xAAAAAAAA;
	tags->validity1 = 0x55555555;
}

int yaffs_validate_tags(struct yaffs_ext_tags *tags)
{
	return (tags->validity0 == 0xAAAAAAAA && tags->validity1 == 0x55555555);

}
