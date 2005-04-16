/*
 * JFFS -- Journaling Flash File System, Linux implementation.
 *
 * Copyright (C) 2000  Axis Communications AB.
 *
 * Created by Simon Kagstrom <simonk@axis.com>.
 *
 * $Id: jffs_proc.c,v 1.5 2001/06/02 14:34:55 dwmw2 Exp $
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  Overview:
 *   This file defines JFFS partition entries in the proc file system.
 *
 *  TODO:
 *   Create some more proc files for different kinds of info, i.e. statistics
 *   about written and read bytes, number of calls to different routines,
 *   reports about failures.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/jffs.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/types.h>
#include "jffs_fm.h"
#include "jffs_proc.h"

/*
 * Structure for a JFFS partition in the system
 */
struct jffs_partition_dir {
	struct jffs_control *c;
	struct proc_dir_entry *part_root;
	struct proc_dir_entry *part_info;
	struct proc_dir_entry *part_layout;
	struct jffs_partition_dir *next;
};

/*
 * Structure for top-level entry in '/proc/fs' directory
 */
struct proc_dir_entry *jffs_proc_root;

/*
 * Linked list of 'jffs_partition_dirs' to help us track
 * the mounted JFFS partitions in the system
 */
static struct jffs_partition_dir *jffs_part_dirs;

/*
 * Read functions for entries
 */
static int jffs_proc_info_read(char *page, char **start, off_t off,
		int count, int *eof, void *data);
static int jffs_proc_layout_read (char *page, char **start, off_t off,
		int count, int *eof, void *data);


/*
 * Register a JFFS partition directory (called upon mount)
 */
int jffs_register_jffs_proc_dir(int mtd, struct jffs_control *c)
{
	struct jffs_partition_dir *part_dir;
	struct proc_dir_entry *part_info = NULL;
	struct proc_dir_entry *part_layout = NULL;
	struct proc_dir_entry *part_root = NULL;
	char name[10];

	sprintf(name, "%d", mtd);
	/* Allocate structure for local JFFS partition table */
	part_dir = (struct jffs_partition_dir *)
		kmalloc(sizeof (struct jffs_partition_dir), GFP_KERNEL);
	if (!part_dir)
		goto out;

	/* Create entry for this partition */
	part_root = proc_mkdir(name, jffs_proc_root);
	if (!part_root)
		goto out1;

	/* Create entry for 'info' file */
	part_info = create_proc_entry ("info", 0, part_root);
	if (!part_info)
		goto out2;
	part_info->read_proc = jffs_proc_info_read;
	part_info->data = (void *) c;

	/* Create entry for 'layout' file */
	part_layout = create_proc_entry ("layout", 0, part_root);
	if (!part_layout)
		goto out3;
	part_layout->read_proc = jffs_proc_layout_read;
	part_layout->data = (void *) c;

	/* Fill in structure for table and insert in the list */
	part_dir->c = c;
	part_dir->part_root = part_root;
	part_dir->part_info = part_info;
	part_dir->part_layout = part_layout;
	part_dir->next = jffs_part_dirs;
	jffs_part_dirs = part_dir;

	/* Return happy */
	return 0;

out3:
	remove_proc_entry("info", part_root);
out2:
	remove_proc_entry(name, jffs_proc_root);
out1:
	kfree(part_dir);
out:
	return -ENOMEM;
}


/*
 * Unregister a JFFS partition directory (called at umount)
 */
int jffs_unregister_jffs_proc_dir(struct jffs_control *c)
{
	struct jffs_partition_dir *part_dir = jffs_part_dirs;
	struct jffs_partition_dir *prev_part_dir = NULL;

	while (part_dir) {
		if (part_dir->c == c) {
			/* Remove entries for partition */
			remove_proc_entry (part_dir->part_info->name,
				part_dir->part_root);
			remove_proc_entry (part_dir->part_layout->name,
				part_dir->part_root);
			remove_proc_entry (part_dir->part_root->name,
				jffs_proc_root);

			/* Remove entry from list */
			if (prev_part_dir)
				prev_part_dir->next = part_dir->next;
			else
				jffs_part_dirs = part_dir->next;

			/*
			 * Check to see if this is the last one
			 * and remove the entry from '/proc/fs'
			 * if it is.
			 */
			if (jffs_part_dirs == part_dir->next)
				remove_proc_entry ("jffs", proc_root_fs);

			/* Free memory for entry */
			kfree(part_dir);

			/* Return happy */
			return 0;
		}

		/* Move to next entry */
		prev_part_dir = part_dir;
		part_dir = part_dir->next;
	}

	/* Return unhappy */
	return -1;
}


/*
 * Read a JFFS partition's `info' file
 */
static int jffs_proc_info_read (char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	struct jffs_control *c = (struct jffs_control *) data;
	int len = 0;

	/* Get information on the parition */
	len += sprintf (page,
		"partition size:     %08lX (%u)\n"
		"sector size:        %08lX (%u)\n"
		"used size:          %08lX (%u)\n"
		"dirty size:         %08lX (%u)\n"
		"free size:          %08lX (%u)\n\n",
		(unsigned long) c->fmc->flash_size, c->fmc->flash_size,
		(unsigned long) c->fmc->sector_size, c->fmc->sector_size,
		(unsigned long) c->fmc->used_size, c->fmc->used_size,
		(unsigned long) c->fmc->dirty_size, c->fmc->dirty_size,
		(unsigned long) (c->fmc->flash_size -
			(c->fmc->used_size + c->fmc->dirty_size)),
		c->fmc->flash_size - (c->fmc->used_size + c->fmc->dirty_size));

	/* We're done */
	*eof = 1;

	/* Return length */
	return len;
}


/*
 * Read a JFFS partition's `layout' file
 */
static int jffs_proc_layout_read (char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	struct jffs_control *c = (struct jffs_control *) data;
	struct jffs_fm *fm = NULL;
	struct jffs_fm *last_fm = NULL;
	int len = 0;

	/* Get the first item in the list */
 	fm = c->fmc->head;

	/* Print free space */
	if (fm && fm->offset) {
		len += sprintf (page, "00000000 %08lX free\n",
			(unsigned long) fm->offset);
	}

	/* Loop through all of the flash control structures */
	while (fm && (len < (off + count))) {
		if (fm->nodes) {
			len += sprintf (page + len,
				"%08lX %08lX ino=%08lX, ver=%08lX\n",
				(unsigned long) fm->offset,
				(unsigned long) fm->size,
				(unsigned long) fm->nodes->node->ino,
				(unsigned long) fm->nodes->node->version);
		}
		else {
			len += sprintf (page + len,
				"%08lX %08lX dirty\n",
				(unsigned long) fm->offset,
				(unsigned long) fm->size);
		}
		last_fm = fm;
		fm = fm->next;
	}

	/* Print free space */
	if ((len < (off + count)) && last_fm
	    && (last_fm->offset < c->fmc->flash_size)) {
		len += sprintf (page + len,
			       "%08lX %08lX free\n",
			       (unsigned long) last_fm->offset + 
				last_fm->size,
			       (unsigned long) (c->fmc->flash_size -
						    (last_fm->offset + last_fm->size)));
	}

	/* We're done */
	*eof = 1;

	/* Return length */
	return len;
}
