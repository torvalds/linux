/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2007 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Martin Fouts <Martin.Fouts@palmsource.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __YAFFS_CONFIG_H__
#define __YAFFS_CONFIG_H__

#ifdef YAFFS_OUT_OF_TREE

/* DO NOT UNSET THESE THREE. YAFFS2 will not compile if you do. */
#define CONFIG_YAFFS_FS
#define CONFIG_YAFFS_YAFFS1
#define CONFIG_YAFFS_YAFFS2

/* These options are independent of each other.  Select those that matter. */

/* Default: Not selected */
/* Meaning: Yaffs does its own ECC, rather than using MTD ECC */
/* #define CONFIG_YAFFS_DOES_ECC */

/* Default: Not selected */
/* Meaning: ECC byte order is 'wrong'.  Only meaningful if */
/*          CONFIG_YAFFS_DOES_ECC is set */
/* #define CONFIG_YAFFS_ECC_WRONG_ORDER */

/* Default: Selected */
/* Meaning: Disables testing whether chunks are erased before writing to them*/
#define CONFIG_YAFFS_DISABLE_CHUNK_ERASED_CHECK

/* Default: Selected */
/* Meaning: Cache short names, taking more RAM, but faster look-ups */
#define CONFIG_YAFFS_SHORT_NAMES_IN_RAM

/* Default: 10 */
/* Meaning: set the count of blocks to reserve for checkpointing */
#define CONFIG_YAFFS_CHECKPOINT_RESERVED_BLOCKS 10

/*
Older-style on-NAND data format has a "pageStatus" byte to record
chunk/page state.  This byte is zeroed when the page is discarded.
Choose this option if you have existing on-NAND data in this format
that you need to continue to support.  New data written also uses the
older-style format.
Note: Use of this option generally requires that MTD's oob layout be
adjusted to use the older-style format.  See notes on tags formats and
MTD versions in yaffs_mtdif1.c.
*/
/* Default: Not selected */
/* Meaning: Use older-style on-NAND data format with pageStatus byte */
/* #define CONFIG_YAFFS_9BYTE_TAGS */

#endif /* YAFFS_OUT_OF_TREE */

#endif /* __YAFFS_CONFIG_H__ */
