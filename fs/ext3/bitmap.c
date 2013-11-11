/*
 *  linux/fs/ext3/bitmap.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include "ext3.h"

#ifdef EXT3FS_DEBUG

unsigned long ext3_count_free (struct buffer_head * map, unsigned int numchars)
{
	return numchars * BITS_PER_BYTE - memweight(map->b_data, numchars);
}

#endif  /*  EXT3FS_DEBUG  */

