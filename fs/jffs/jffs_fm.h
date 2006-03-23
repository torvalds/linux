/*
 * JFFS -- Journaling Flash File System, Linux implementation.
 *
 * Copyright (C) 1999, 2000  Axis Communications AB.
 *
 * Created by Finn Hakansson <finn@axis.com>.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * $Id: jffs_fm.h,v 1.13 2001/01/11 12:03:25 dwmw2 Exp $
 *
 * Ported to Linux 2.3.x and MTD:
 * Copyright (C) 2000  Alexander Larsson (alex@cendio.se), Cendio Systems AB
 *
 */

#ifndef __LINUX_JFFS_FM_H__
#define __LINUX_JFFS_FM_H__

#include <linux/config.h>
#include <linux/types.h>
#include <linux/jffs.h>
#include <linux/mtd/mtd.h>
#include <linux/mutex.h>

/* The alignment between two nodes in the flash memory.  */
#define JFFS_ALIGN_SIZE 4

/* Mark the on-flash space as obsolete when appropriate.  */
#define JFFS_MARK_OBSOLETE 0

#ifndef CONFIG_JFFS_FS_VERBOSE
#define CONFIG_JFFS_FS_VERBOSE 1
#endif

#if CONFIG_JFFS_FS_VERBOSE > 0
#define D(x) x
#define D1(x) D(x)
#else
#define D(x)
#define D1(x)
#endif

#if CONFIG_JFFS_FS_VERBOSE > 1
#define D2(x) D(x)
#else
#define D2(x)
#endif

#if CONFIG_JFFS_FS_VERBOSE > 2
#define D3(x) D(x)
#else
#define D3(x)
#endif

#define ASSERT(x) x

/* How many padding bytes should be inserted between two chunks of data
   on the flash?  */
#define JFFS_GET_PAD_BYTES(size) ( (JFFS_ALIGN_SIZE-1) & -(__u32)(size) )
#define JFFS_PAD(size) ( (size + (JFFS_ALIGN_SIZE-1)) & ~(JFFS_ALIGN_SIZE-1) )



struct jffs_node_ref
{
	struct jffs_node *node;
	struct jffs_node_ref *next;
};


/* The struct jffs_fm represents a chunk of data in the flash memory.  */
struct jffs_fm
{
	__u32 offset;
	__u32 size;
	struct jffs_fm *prev;
	struct jffs_fm *next;
	struct jffs_node_ref *nodes; /* USED if != 0.  */
};

struct jffs_fmcontrol
{
	__u32 flash_size;
	__u32 used_size;
	__u32 dirty_size;
	__u32 free_size;
	__u32 sector_size;
	__u32 min_free_size;  /* The minimum free space needed to be able
				 to perform garbage collections.  */
	__u32 max_chunk_size; /* The maximum size of a chunk of data.  */
	struct mtd_info *mtd;
	struct jffs_control *c;
	struct jffs_fm *head;
	struct jffs_fm *tail;
	struct jffs_fm *head_extra;
	struct jffs_fm *tail_extra;
	struct mutex biglock;
};

/* Notice the two members head_extra and tail_extra in the jffs_control
   structure above. Those are only used during the scanning of the flash
   memory; while the file system is being built. If the data in the flash
   memory is organized like

      +----------------+------------------+----------------+
      |  USED / DIRTY  |       FREE       |  USED / DIRTY  |
      +----------------+------------------+----------------+

   then the scan is split in two parts. The first scanned part of the
   flash memory is organized through the members head and tail. The
   second scanned part is organized with head_extra and tail_extra. When
   the scan is completed, the two lists are merged together. The jffs_fm
   struct that head_extra references is the logical beginning of the
   flash memory so it will be referenced by the head member.  */



struct jffs_fmcontrol *jffs_build_begin(struct jffs_control *c, int unit);
void jffs_build_end(struct jffs_fmcontrol *fmc);
void jffs_cleanup_fmcontrol(struct jffs_fmcontrol *fmc);

int jffs_fmalloc(struct jffs_fmcontrol *fmc, __u32 size,
		 struct jffs_node *node, struct jffs_fm **result);
int jffs_fmfree(struct jffs_fmcontrol *fmc, struct jffs_fm *fm,
		struct jffs_node *node);

__u32 jffs_free_size1(struct jffs_fmcontrol *fmc);
__u32 jffs_free_size2(struct jffs_fmcontrol *fmc);
void jffs_sync_erase(struct jffs_fmcontrol *fmc, int erased_size);
struct jffs_fm *jffs_cut_node(struct jffs_fmcontrol *fmc, __u32 size);
struct jffs_node *jffs_get_oldest_node(struct jffs_fmcontrol *fmc);
long jffs_erasable_size(struct jffs_fmcontrol *fmc);
struct jffs_fm *jffs_fmalloced(struct jffs_fmcontrol *fmc, __u32 offset,
			       __u32 size, struct jffs_node *node);
int jffs_add_node(struct jffs_node *node);
void jffs_fmfree_partly(struct jffs_fmcontrol *fmc, struct jffs_fm *fm,
			__u32 size);

#if CONFIG_JFFS_FS_VERBOSE > 0
void jffs_print_fmcontrol(struct jffs_fmcontrol *fmc);
#endif
#if 0
void jffs_print_node_ref(struct jffs_node_ref *ref);
#endif  /*  0  */

#endif /* __LINUX_JFFS_FM_H__  */
