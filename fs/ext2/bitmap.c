/*
 *  linux/fs/ext2/bitmap.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#ifdef EXT2FS_DEBUG

#include <linux/buffer_head.h>

#include "ext2.h"

static int nibblemap[] = {4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0};

unsigned long ext2_count_free (struct buffer_head * map, unsigned int numchars)
{
	unsigned int i;
	unsigned long sum = 0;
	
	if (!map) 
		return (0);
	for (i = 0; i < numchars; i++)
		sum += nibblemap[map->b_data[i] & 0xf] +
			nibblemap[(map->b_data[i] >> 4) & 0xf];
	return (sum);
}

#endif  /*  EXT2FS_DEBUG  */

