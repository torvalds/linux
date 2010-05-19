/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */


#ifndef __YAFFS_TAGS_VALIDITY_H__
#define __YAFFS_TAGS_VALIDITY_H__

#include "yaffs_guts.h"

void yaffs_InitialiseTags(yaffs_ExtendedTags *tags);
int yaffs_ValidateTags(yaffs_ExtendedTags *tags);
#endif
