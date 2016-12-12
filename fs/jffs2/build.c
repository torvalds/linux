/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 * Copyright © 2004-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mtd/mtd.h>
#include "nodelist.h"

static void jffs2_build_remove_unlinked_inode(struct jffs2_sb_info *,
		struct jffs2_inode_cache *, struct jffs2_full_dirent **);

static inline struct jffs2_inode_cache *
first_inode_chain(int *i, struct jffs2_sb_info *c)
{
	for (; *i < c->inocache_hashsize; (*i)++) {
		if (c->inocache_list[*i])
			return c->inocache_list[*i];
	}
	return NULL;
}

static inline struct jffs2_inode_cache *
next_inode(int *i, struct jffs2_inode_cache *ic, struct jffs2_sb_info *c)
{
	/* More in this chain? */
	if (ic->next)
		return ic->next;
	(*i)++;
	return first_inode_chain(i, c);
}

#define for_each_inode(i, c, ic)			\
	for (i = 0, ic = first_inode_chain(&i, (c));	\
	     ic;					\
	     ic = next_inode(&i, ic, (c)))


static void jffs2_build_inode_pass1(struct jffs2_sb_info *c,
				    struct jffs2_inode_cache *ic,
				    int *dir_hardlinks)
{
	struct jffs2_full_dirent *fd;

	dbg_fsbuild("building directory inode #%u\n", ic->ino);

	/* For each child, increase nlink */
	for(fd = ic->scan_dents; fd; fd = fd->next) {
		struct jffs2_inode_cache *child_ic;
		if (!fd->ino)
			continue;

		/* we can get high latency here with huge directories */

		child_ic = jffs2_get_ino_cache(c, fd->ino);
		if (!child_ic) {
			dbg_fsbuild("child \"%s\" (ino #%u) of dir ino #%u doesn't exist!\n",
				  fd->name, fd->ino, ic->ino);
			jffs2_mark_node_obsolete(c, fd->raw);
			/* Clear the ic/raw union so it doesn't cause problems later. */
			fd->ic = NULL;
			continue;
		}

		/* From this point, fd->raw is no longer used so we can set fd->ic */
		fd->ic = child_ic;
		child_ic->pino_nlink++;
		/* If we appear (at this stage) to have hard-linked directories,
		 * set a flag to trigger a scan later */
		if (fd->type == DT_DIR) {
			child_ic->flags |= INO_FLAGS_IS_DIR;
			if (child_ic->pino_nlink > 1)
				*dir_hardlinks = 1;
		}

		dbg_fsbuild("increased nlink for child \"%s\" (ino #%u)\n", fd->name, fd->ino);
		/* Can't free scan_dents so far. We might need them in pass 2 */
	}
}

/* Scan plan:
 - Scan physical nodes. Build map of inodes/dirents. Allocate inocaches as we go
 - Scan directory tree from top down, setting nlink in inocaches
 - Scan inocaches for inodes with nlink==0
*/
static int jffs2_build_filesystem(struct jffs2_sb_info *c)
{
	int ret, i, dir_hardlinks = 0;
	struct jffs2_inode_cache *ic;
	struct jffs2_full_dirent *fd;
	struct jffs2_full_dirent *dead_fds = NULL;

	dbg_fsbuild("build FS data structures\n");

	/* First, scan the medium and build all the inode caches with
	   lists of physical nodes */

	c->flags |= JFFS2_SB_FLAG_SCANNING;
	ret = jffs2_scan_medium(c);
	c->flags &= ~JFFS2_SB_FLAG_SCANNING;
	if (ret)
		goto exit;

	dbg_fsbuild("scanned flash completely\n");
	jffs2_dbg_dump_block_lists_nolock(c);

	dbg_fsbuild("pass 1 starting\n");
	c->flags |= JFFS2_SB_FLAG_BUILDING;
	/* Now scan the directory tree, increasing nlink according to every dirent found. */
	for_each_inode(i, c, ic) {
		if (ic->scan_dents) {
			jffs2_build_inode_pass1(c, ic, &dir_hardlinks);
			cond_resched();
		}
	}

	dbg_fsbuild("pass 1 complete\n");

	/* Next, scan for inodes with nlink == 0 and remove them. If
	   they were directories, then decrement the nlink of their
	   children too, and repeat the scan. As that's going to be
	   a fairly uncommon occurrence, it's not so evil to do it this
	   way. Recursion bad. */
	dbg_fsbuild("pass 2 starting\n");

	for_each_inode(i, c, ic) {
		if (ic->pino_nlink)
			continue;

		jffs2_build_remove_unlinked_inode(c, ic, &dead_fds);
		cond_resched();
	}

	dbg_fsbuild("pass 2a starting\n");

	while (dead_fds) {
		fd = dead_fds;
		dead_fds = fd->next;

		ic = jffs2_get_ino_cache(c, fd->ino);

		if (ic)
			jffs2_build_remove_unlinked_inode(c, ic, &dead_fds);
		jffs2_free_full_dirent(fd);
	}

	dbg_fsbuild("pass 2a complete\n");

	if (dir_hardlinks) {
		/* If we detected directory hardlinks earlier, *hopefully*
		 * they are gone now because some of the links were from
		 * dead directories which still had some old dirents lying
		 * around and not yet garbage-collected, but which have
		 * been discarded above. So clear the pino_nlink field
		 * in each directory, so that the final scan below can
		 * print appropriate warnings. */
		for_each_inode(i, c, ic) {
			if (ic->flags & INO_FLAGS_IS_DIR)
				ic->pino_nlink = 0;
		}
	}
	dbg_fsbuild("freeing temporary data structures\n");

	/* Finally, we can scan again and free the dirent structs */
	for_each_inode(i, c, ic) {
		while(ic->scan_dents) {
			fd = ic->scan_dents;
			ic->scan_dents = fd->next;
			/* We do use the pino_nlink field to count nlink of
			 * directories during fs build, so set it to the
			 * parent ino# now. Now that there's hopefully only
			 * one. */
			if (fd->type == DT_DIR) {
				if (!fd->ic) {
					/* We'll have complained about it and marked the coresponding
					   raw node obsolete already. Just skip it. */
					continue;
				}

				/* We *have* to have set this in jffs2_build_inode_pass1() */
				BUG_ON(!(fd->ic->flags & INO_FLAGS_IS_DIR));

				/* We clear ic->pino_nlink ∀ directories' ic *only* if dir_hardlinks
				 * is set. Otherwise, we know this should never trigger anyway, so
				 * we don't do the check. And ic->pino_nlink still contains the nlink
				 * value (which is 1). */
				if (dir_hardlinks && fd->ic->pino_nlink) {
					JFFS2_ERROR("child dir \"%s\" (ino #%u) of dir ino #%u is also hard linked from dir ino #%u\n",
						    fd->name, fd->ino, ic->ino, fd->ic->pino_nlink);
					/* Should we unlink it from its previous parent? */
				}

				/* For directories, ic->pino_nlink holds that parent inode # */
				fd->ic->pino_nlink = ic->ino;
			}
			jffs2_free_full_dirent(fd);
		}
		ic->scan_dents = NULL;
		cond_resched();
	}
	jffs2_build_xattr_subsystem(c);
	c->flags &= ~JFFS2_SB_FLAG_BUILDING;

	dbg_fsbuild("FS build complete\n");

	/* Rotate the lists by some number to ensure wear levelling */
	jffs2_rotate_lists(c);

	ret = 0;

exit:
	if (ret) {
		for_each_inode(i, c, ic) {
			while(ic->scan_dents) {
				fd = ic->scan_dents;
				ic->scan_dents = fd->next;
				jffs2_free_full_dirent(fd);
			}
		}
		jffs2_clear_xattr_subsystem(c);
	}

	return ret;
}

static void jffs2_build_remove_unlinked_inode(struct jffs2_sb_info *c,
					struct jffs2_inode_cache *ic,
					struct jffs2_full_dirent **dead_fds)
{
	struct jffs2_raw_node_ref *raw;
	struct jffs2_full_dirent *fd;

	dbg_fsbuild("removing ino #%u with nlink == zero.\n", ic->ino);

	raw = ic->nodes;
	while (raw != (void *)ic) {
		struct jffs2_raw_node_ref *next = raw->next_in_ino;
		dbg_fsbuild("obsoleting node at 0x%08x\n", ref_offset(raw));
		jffs2_mark_node_obsolete(c, raw);
		raw = next;
	}

	if (ic->scan_dents) {
		int whinged = 0;
		dbg_fsbuild("inode #%u was a directory which may have children...\n", ic->ino);

		while(ic->scan_dents) {
			struct jffs2_inode_cache *child_ic;

			fd = ic->scan_dents;
			ic->scan_dents = fd->next;

			if (!fd->ino) {
				/* It's a deletion dirent. Ignore it */
				dbg_fsbuild("child \"%s\" is a deletion dirent, skipping...\n", fd->name);
				jffs2_free_full_dirent(fd);
				continue;
			}
			if (!whinged)
				whinged = 1;

			dbg_fsbuild("removing child \"%s\", ino #%u\n", fd->name, fd->ino);

			child_ic = jffs2_get_ino_cache(c, fd->ino);
			if (!child_ic) {
				dbg_fsbuild("cannot remove child \"%s\", ino #%u, because it doesn't exist\n",
						fd->name, fd->ino);
				jffs2_free_full_dirent(fd);
				continue;
			}

			/* Reduce nlink of the child. If it's now zero, stick it on the
			   dead_fds list to be cleaned up later. Else just free the fd */
			child_ic->pino_nlink--;

			if (!child_ic->pino_nlink) {
				dbg_fsbuild("inode #%u (\"%s\") now has no links; adding to dead_fds list.\n",
					  fd->ino, fd->name);
				fd->next = *dead_fds;
				*dead_fds = fd;
			} else {
				dbg_fsbuild("inode #%u (\"%s\") has now got nlink %d. Ignoring.\n",
					  fd->ino, fd->name, child_ic->pino_nlink);
				jffs2_free_full_dirent(fd);
			}
		}
	}

	/*
	   We don't delete the inocache from the hash list and free it yet.
	   The erase code will do that, when all the nodes are completely gone.
	*/
}

static void jffs2_calc_trigger_levels(struct jffs2_sb_info *c)
{
	uint32_t size;

	/* Deletion should almost _always_ be allowed. We're fairly
	   buggered once we stop allowing people to delete stuff
	   because there's not enough free space... */
	c->resv_blocks_deletion = 2;

	/* Be conservative about how much space we need before we allow writes.
	   On top of that which is required for deletia, require an extra 2%
	   of the medium to be available, for overhead caused by nodes being
	   split across blocks, etc. */

	size = c->flash_size / 50; /* 2% of flash size */
	size += c->nr_blocks * 100; /* And 100 bytes per eraseblock */
	size += c->sector_size - 1; /* ... and round up */

	c->resv_blocks_write = c->resv_blocks_deletion + (size / c->sector_size);

	/* When do we let the GC thread run in the background */

	c->resv_blocks_gctrigger = c->resv_blocks_write + 1;

	/* When do we allow garbage collection to merge nodes to make
	   long-term progress at the expense of short-term space exhaustion? */
	c->resv_blocks_gcmerge = c->resv_blocks_deletion + 1;

	/* When do we allow garbage collection to eat from bad blocks rather
	   than actually making progress? */
	c->resv_blocks_gcbad = 0;//c->resv_blocks_deletion + 2;

	/* What number of 'very dirty' eraseblocks do we allow before we
	   trigger the GC thread even if we don't _need_ the space. When we
	   can't mark nodes obsolete on the medium, the old dirty nodes cause
	   performance problems because we have to inspect and discard them. */
	c->vdirty_blocks_gctrigger = c->resv_blocks_gctrigger;
	if (jffs2_can_mark_obsolete(c))
		c->vdirty_blocks_gctrigger *= 10;

	/* If there's less than this amount of dirty space, don't bother
	   trying to GC to make more space. It'll be a fruitless task */
	c->nospc_dirty_size = c->sector_size + (c->flash_size / 100);

	dbg_fsbuild("trigger levels (size %d KiB, block size %d KiB, %d blocks)\n",
		    c->flash_size / 1024, c->sector_size / 1024, c->nr_blocks);
	dbg_fsbuild("Blocks required to allow deletion:    %d (%d KiB)\n",
		  c->resv_blocks_deletion, c->resv_blocks_deletion*c->sector_size/1024);
	dbg_fsbuild("Blocks required to allow writes:      %d (%d KiB)\n",
		  c->resv_blocks_write, c->resv_blocks_write*c->sector_size/1024);
	dbg_fsbuild("Blocks required to quiesce GC thread: %d (%d KiB)\n",
		  c->resv_blocks_gctrigger, c->resv_blocks_gctrigger*c->sector_size/1024);
	dbg_fsbuild("Blocks required to allow GC merges:   %d (%d KiB)\n",
		  c->resv_blocks_gcmerge, c->resv_blocks_gcmerge*c->sector_size/1024);
	dbg_fsbuild("Blocks required to GC bad blocks:     %d (%d KiB)\n",
		  c->resv_blocks_gcbad, c->resv_blocks_gcbad*c->sector_size/1024);
	dbg_fsbuild("Amount of dirty space required to GC: %d bytes\n",
		  c->nospc_dirty_size);
	dbg_fsbuild("Very dirty blocks before GC triggered: %d\n",
		  c->vdirty_blocks_gctrigger);
}

int jffs2_do_mount_fs(struct jffs2_sb_info *c)
{
	int ret;
	int i;
	int size;

	c->free_size = c->flash_size;
	c->nr_blocks = c->flash_size / c->sector_size;
	size = sizeof(struct jffs2_eraseblock) * c->nr_blocks;
#ifndef __ECOS
	if (jffs2_blocks_use_vmalloc(c))
		c->blocks = vzalloc(size);
	else
#endif
		c->blocks = kzalloc(size, GFP_KERNEL);
	if (!c->blocks)
		return -ENOMEM;

	for (i=0; i<c->nr_blocks; i++) {
		INIT_LIST_HEAD(&c->blocks[i].list);
		c->blocks[i].offset = i * c->sector_size;
		c->blocks[i].free_size = c->sector_size;
	}

	INIT_LIST_HEAD(&c->clean_list);
	INIT_LIST_HEAD(&c->very_dirty_list);
	INIT_LIST_HEAD(&c->dirty_list);
	INIT_LIST_HEAD(&c->erasable_list);
	INIT_LIST_HEAD(&c->erasing_list);
	INIT_LIST_HEAD(&c->erase_checking_list);
	INIT_LIST_HEAD(&c->erase_pending_list);
	INIT_LIST_HEAD(&c->erasable_pending_wbuf_list);
	INIT_LIST_HEAD(&c->erase_complete_list);
	INIT_LIST_HEAD(&c->free_list);
	INIT_LIST_HEAD(&c->bad_list);
	INIT_LIST_HEAD(&c->bad_used_list);
	c->highest_ino = 1;
	c->summary = NULL;

	ret = jffs2_sum_init(c);
	if (ret)
		goto out_free;

	if (jffs2_build_filesystem(c)) {
		dbg_fsbuild("build_fs failed\n");
		jffs2_free_ino_caches(c);
		jffs2_free_raw_node_refs(c);
		ret = -EIO;
		goto out_free;
	}

	jffs2_calc_trigger_levels(c);

	return 0;

 out_free:
#ifndef __ECOS
	if (jffs2_blocks_use_vmalloc(c))
		vfree(c->blocks);
	else
#endif
		kfree(c->blocks);

	return ret;
}
