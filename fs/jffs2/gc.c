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
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/crc32.h>
#include <linux/compiler.h>
#include <linux/stat.h>
#include "nodelist.h"
#include "compr.h"

static int jffs2_garbage_collect_pristine(struct jffs2_sb_info *c,
					  struct jffs2_inode_cache *ic,
					  struct jffs2_raw_node_ref *raw);
static int jffs2_garbage_collect_metadata(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_inode_info *f, struct jffs2_full_dnode *fd);
static int jffs2_garbage_collect_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd);
static int jffs2_garbage_collect_deletion_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd);
static int jffs2_garbage_collect_hole(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				      struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				      uint32_t start, uint32_t end);
static int jffs2_garbage_collect_dnode(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				       struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				       uint32_t start, uint32_t end);
static int jffs2_garbage_collect_live(struct jffs2_sb_info *c,  struct jffs2_eraseblock *jeb,
			       struct jffs2_raw_node_ref *raw, struct jffs2_inode_info *f);

/* Called with erase_completion_lock held */
static struct jffs2_eraseblock *jffs2_find_gc_block(struct jffs2_sb_info *c)
{
	struct jffs2_eraseblock *ret;
	struct list_head *nextlist = NULL;
	int n = jiffies % 128;

	/* Pick an eraseblock to garbage collect next. This is where we'll
	   put the clever wear-levelling algorithms. Eventually.  */
	/* We possibly want to favour the dirtier blocks more when the
	   number of free blocks is low. */
again:
	if (!list_empty(&c->bad_used_list) && c->nr_free_blocks > c->resv_blocks_gcbad) {
		jffs2_dbg(1, "Picking block from bad_used_list to GC next\n");
		nextlist = &c->bad_used_list;
	} else if (n < 50 && !list_empty(&c->erasable_list)) {
		/* Note that most of them will have gone directly to be erased.
		   So don't favour the erasable_list _too_ much. */
		jffs2_dbg(1, "Picking block from erasable_list to GC next\n");
		nextlist = &c->erasable_list;
	} else if (n < 110 && !list_empty(&c->very_dirty_list)) {
		/* Most of the time, pick one off the very_dirty list */
		jffs2_dbg(1, "Picking block from very_dirty_list to GC next\n");
		nextlist = &c->very_dirty_list;
	} else if (n < 126 && !list_empty(&c->dirty_list)) {
		jffs2_dbg(1, "Picking block from dirty_list to GC next\n");
		nextlist = &c->dirty_list;
	} else if (!list_empty(&c->clean_list)) {
		jffs2_dbg(1, "Picking block from clean_list to GC next\n");
		nextlist = &c->clean_list;
	} else if (!list_empty(&c->dirty_list)) {
		jffs2_dbg(1, "Picking block from dirty_list to GC next (clean_list was empty)\n");

		nextlist = &c->dirty_list;
	} else if (!list_empty(&c->very_dirty_list)) {
		jffs2_dbg(1, "Picking block from very_dirty_list to GC next (clean_list and dirty_list were empty)\n");
		nextlist = &c->very_dirty_list;
	} else if (!list_empty(&c->erasable_list)) {
		jffs2_dbg(1, "Picking block from erasable_list to GC next (clean_list and {very_,}dirty_list were empty)\n");

		nextlist = &c->erasable_list;
	} else if (!list_empty(&c->erasable_pending_wbuf_list)) {
		/* There are blocks are waiting for the wbuf sync */
		jffs2_dbg(1, "Synching wbuf in order to reuse erasable_pending_wbuf_list blocks\n");
		spin_unlock(&c->erase_completion_lock);
		jffs2_flush_wbuf_pad(c);
		spin_lock(&c->erase_completion_lock);
		goto again;
	} else {
		/* Eep. All were empty */
		jffs2_dbg(1, "No clean, dirty _or_ erasable blocks to GC from! Where are they all?\n");
		return NULL;
	}

	ret = list_entry(nextlist->next, struct jffs2_eraseblock, list);
	list_del(&ret->list);
	c->gcblock = ret;
	ret->gc_node = ret->first_node;
	if (!ret->gc_node) {
		pr_warn("Eep. ret->gc_node for block at 0x%08x is NULL\n",
			ret->offset);
		BUG();
	}

	/* Have we accidentally picked a clean block with wasted space ? */
	if (ret->wasted_size) {
		jffs2_dbg(1, "Converting wasted_size %08x to dirty_size\n",
			  ret->wasted_size);
		ret->dirty_size += ret->wasted_size;
		c->wasted_size -= ret->wasted_size;
		c->dirty_size += ret->wasted_size;
		ret->wasted_size = 0;
	}

	return ret;
}

/* jffs2_garbage_collect_pass
 * Make a single attempt to progress GC. Move one node, and possibly
 * start erasing one eraseblock.
 */
int jffs2_garbage_collect_pass(struct jffs2_sb_info *c)
{
	struct jffs2_inode_info *f;
	struct jffs2_inode_cache *ic;
	struct jffs2_eraseblock *jeb;
	struct jffs2_raw_node_ref *raw;
	uint32_t gcblock_dirty;
	int ret = 0, inum, nlink;
	int xattr = 0;

	if (mutex_lock_interruptible(&c->alloc_sem))
		return -EINTR;


	for (;;) {
		/* We can't start doing GC until we've finished checking
		   the node CRCs etc. */
		int bucket, want_ino;

		spin_lock(&c->erase_completion_lock);
		if (!c->unchecked_size)
			break;
		spin_unlock(&c->erase_completion_lock);

		if (!xattr)
			xattr = jffs2_verify_xattr(c);

		spin_lock(&c->inocache_lock);
		/* Instead of doing the inodes in numeric order, doing a lookup
		 * in the hash for each possible number, just walk the hash
		 * buckets of *existing* inodes. This means that we process
		 * them out-of-order, but it can be a lot faster if there's
		 * a sparse inode# space. Which there often is. */
		want_ino = c->check_ino;
		for (bucket = c->check_ino % c->inocache_hashsize ; bucket < c->inocache_hashsize; bucket++) {
			for (ic = c->inocache_list[bucket]; ic; ic = ic->next) {
				if (ic->ino < want_ino)
					continue;

				if (ic->state != INO_STATE_CHECKEDABSENT &&
				    ic->state != INO_STATE_PRESENT)
					goto got_next; /* with inocache_lock held */

				jffs2_dbg(1, "Skipping ino #%u already checked\n",
					  ic->ino);
			}
			want_ino = 0;
		}

		/* Point c->check_ino past the end of the last bucket. */
		c->check_ino = ((c->highest_ino + c->inocache_hashsize + 1) &
				~c->inocache_hashsize) - 1;

		spin_unlock(&c->inocache_lock);

		pr_crit("Checked all inodes but still 0x%x bytes of unchecked space?\n",
			c->unchecked_size);
		jffs2_dbg_dump_block_lists_nolock(c);
		mutex_unlock(&c->alloc_sem);
		return -ENOSPC;

	got_next:
		/* For next time round the loop, we want c->checked_ino to indicate
		 * the *next* one we want to check. And since we're walking the
		 * buckets rather than doing it sequentially, it's: */
		c->check_ino = ic->ino + c->inocache_hashsize;

		if (!ic->pino_nlink) {
			jffs2_dbg(1, "Skipping check of ino #%d with nlink/pino zero\n",
				  ic->ino);
			spin_unlock(&c->inocache_lock);
			jffs2_xattr_delete_inode(c, ic);
			continue;
		}
		switch(ic->state) {
		case INO_STATE_CHECKEDABSENT:
		case INO_STATE_PRESENT:
			spin_unlock(&c->inocache_lock);
			continue;

		case INO_STATE_GC:
		case INO_STATE_CHECKING:
			pr_warn("Inode #%u is in state %d during CRC check phase!\n",
				ic->ino, ic->state);
			spin_unlock(&c->inocache_lock);
			BUG();

		case INO_STATE_READING:
			/* We need to wait for it to finish, lest we move on
			   and trigger the BUG() above while we haven't yet
			   finished checking all its nodes */
			jffs2_dbg(1, "Waiting for ino #%u to finish reading\n",
				  ic->ino);
			/* We need to come back again for the _same_ inode. We've
			 made no progress in this case, but that should be OK */
			c->check_ino = ic->ino;

			mutex_unlock(&c->alloc_sem);
			sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
			return 0;

		default:
			BUG();

		case INO_STATE_UNCHECKED:
			;
		}
		ic->state = INO_STATE_CHECKING;
		spin_unlock(&c->inocache_lock);

		jffs2_dbg(1, "%s(): triggering inode scan of ino#%u\n",
			  __func__, ic->ino);

		ret = jffs2_do_crccheck_inode(c, ic);
		if (ret)
			pr_warn("Returned error for crccheck of ino #%u. Expect badness...\n",
				ic->ino);

		jffs2_set_inocache_state(c, ic, INO_STATE_CHECKEDABSENT);
		mutex_unlock(&c->alloc_sem);
		return ret;
	}

	/* If there are any blocks which need erasing, erase them now */
	if (!list_empty(&c->erase_complete_list) ||
	    !list_empty(&c->erase_pending_list)) {
		spin_unlock(&c->erase_completion_lock);
		mutex_unlock(&c->alloc_sem);
		jffs2_dbg(1, "%s(): erasing pending blocks\n", __func__);
		if (jffs2_erase_pending_blocks(c, 1))
			return 0;

		jffs2_dbg(1, "No progress from erasing block; doing GC anyway\n");
		mutex_lock(&c->alloc_sem);
		spin_lock(&c->erase_completion_lock);
	}

	/* First, work out which block we're garbage-collecting */
	jeb = c->gcblock;

	if (!jeb)
		jeb = jffs2_find_gc_block(c);

	if (!jeb) {
		/* Couldn't find a free block. But maybe we can just erase one and make 'progress'? */
		if (c->nr_erasing_blocks) {
			spin_unlock(&c->erase_completion_lock);
			mutex_unlock(&c->alloc_sem);
			return -EAGAIN;
		}
		jffs2_dbg(1, "Couldn't find erase block to garbage collect!\n");
		spin_unlock(&c->erase_completion_lock);
		mutex_unlock(&c->alloc_sem);
		return -EIO;
	}

	jffs2_dbg(1, "GC from block %08x, used_size %08x, dirty_size %08x, free_size %08x\n",
		  jeb->offset, jeb->used_size, jeb->dirty_size, jeb->free_size);
	D1(if (c->nextblock)
	   printk(KERN_DEBUG "Nextblock at  %08x, used_size %08x, dirty_size %08x, wasted_size %08x, free_size %08x\n", c->nextblock->offset, c->nextblock->used_size, c->nextblock->dirty_size, c->nextblock->wasted_size, c->nextblock->free_size));

	if (!jeb->used_size) {
		mutex_unlock(&c->alloc_sem);
		goto eraseit;
	}

	raw = jeb->gc_node;
	gcblock_dirty = jeb->dirty_size;

	while(ref_obsolete(raw)) {
		jffs2_dbg(1, "Node at 0x%08x is obsolete... skipping\n",
			  ref_offset(raw));
		raw = ref_next(raw);
		if (unlikely(!raw)) {
			pr_warn("eep. End of raw list while still supposedly nodes to GC\n");
			pr_warn("erase block at 0x%08x. free_size 0x%08x, dirty_size 0x%08x, used_size 0x%08x\n",
				jeb->offset, jeb->free_size,
				jeb->dirty_size, jeb->used_size);
			jeb->gc_node = raw;
			spin_unlock(&c->erase_completion_lock);
			mutex_unlock(&c->alloc_sem);
			BUG();
		}
	}
	jeb->gc_node = raw;

	jffs2_dbg(1, "Going to garbage collect node at 0x%08x\n",
		  ref_offset(raw));

	if (!raw->next_in_ino) {
		/* Inode-less node. Clean marker, snapshot or something like that */
		spin_unlock(&c->erase_completion_lock);
		if (ref_flags(raw) == REF_PRISTINE) {
			/* It's an unknown node with JFFS2_FEATURE_RWCOMPAT_COPY */
			jffs2_garbage_collect_pristine(c, NULL, raw);
		} else {
			/* Just mark it obsolete */
			jffs2_mark_node_obsolete(c, raw);
		}
		mutex_unlock(&c->alloc_sem);
		goto eraseit_lock;
	}

	ic = jffs2_raw_ref_to_ic(raw);

#ifdef CONFIG_JFFS2_FS_XATTR
	/* When 'ic' refers xattr_datum/xattr_ref, this node is GCed as xattr.
	 * We can decide whether this node is inode or xattr by ic->class.     */
	if (ic->class == RAWNODE_CLASS_XATTR_DATUM
	    || ic->class == RAWNODE_CLASS_XATTR_REF) {
		spin_unlock(&c->erase_completion_lock);

		if (ic->class == RAWNODE_CLASS_XATTR_DATUM) {
			ret = jffs2_garbage_collect_xattr_datum(c, (struct jffs2_xattr_datum *)ic, raw);
		} else {
			ret = jffs2_garbage_collect_xattr_ref(c, (struct jffs2_xattr_ref *)ic, raw);
		}
		goto test_gcnode;
	}
#endif

	/* We need to hold the inocache. Either the erase_completion_lock or
	   the inocache_lock are sufficient; we trade down since the inocache_lock
	   causes less contention. */
	spin_lock(&c->inocache_lock);

	spin_unlock(&c->erase_completion_lock);

	jffs2_dbg(1, "%s(): collecting from block @0x%08x. Node @0x%08x(%d), ino #%u\n",
		  __func__, jeb->offset, ref_offset(raw), ref_flags(raw),
		  ic->ino);

	/* Three possibilities:
	   1. Inode is already in-core. We must iget it and do proper
	      updating to its fragtree, etc.
	   2. Inode is not in-core, node is REF_PRISTINE. We lock the
	      inocache to prevent a read_inode(), copy the node intact.
	   3. Inode is not in-core, node is not pristine. We must iget()
	      and take the slow path.
	*/

	switch(ic->state) {
	case INO_STATE_CHECKEDABSENT:
		/* It's been checked, but it's not currently in-core.
		   We can just copy any pristine nodes, but have
		   to prevent anyone else from doing read_inode() while
		   we're at it, so we set the state accordingly */
		if (ref_flags(raw) == REF_PRISTINE)
			ic->state = INO_STATE_GC;
		else {
			jffs2_dbg(1, "Ino #%u is absent but node not REF_PRISTINE. Reading.\n",
				  ic->ino);
		}
		break;

	case INO_STATE_PRESENT:
		/* It's in-core. GC must iget() it. */
		break;

	case INO_STATE_UNCHECKED:
	case INO_STATE_CHECKING:
	case INO_STATE_GC:
		/* Should never happen. We should have finished checking
		   by the time we actually start doing any GC, and since
		   we're holding the alloc_sem, no other garbage collection
		   can happen.
		*/
		pr_crit("Inode #%u already in state %d in jffs2_garbage_collect_pass()!\n",
			ic->ino, ic->state);
		mutex_unlock(&c->alloc_sem);
		spin_unlock(&c->inocache_lock);
		BUG();

	case INO_STATE_READING:
		/* Someone's currently trying to read it. We must wait for
		   them to finish and then go through the full iget() route
		   to do the GC. However, sometimes read_inode() needs to get
		   the alloc_sem() (for marking nodes invalid) so we must
		   drop the alloc_sem before sleeping. */

		mutex_unlock(&c->alloc_sem);
		jffs2_dbg(1, "%s(): waiting for ino #%u in state %d\n",
			  __func__, ic->ino, ic->state);
		sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
		/* And because we dropped the alloc_sem we must start again from the
		   beginning. Ponder chance of livelock here -- we're returning success
		   without actually making any progress.

		   Q: What are the chances that the inode is back in INO_STATE_READING
		   again by the time we next enter this function? And that this happens
		   enough times to cause a real delay?

		   A: Small enough that I don't care :)
		*/
		return 0;
	}

	/* OK. Now if the inode is in state INO_STATE_GC, we are going to copy the
	   node intact, and we don't have to muck about with the fragtree etc.
	   because we know it's not in-core. If it _was_ in-core, we go through
	   all the iget() crap anyway */

	if (ic->state == INO_STATE_GC) {
		spin_unlock(&c->inocache_lock);

		ret = jffs2_garbage_collect_pristine(c, ic, raw);

		spin_lock(&c->inocache_lock);
		ic->state = INO_STATE_CHECKEDABSENT;
		wake_up(&c->inocache_wq);

		if (ret != -EBADFD) {
			spin_unlock(&c->inocache_lock);
			goto test_gcnode;
		}

		/* Fall through if it wanted us to, with inocache_lock held */
	}

	/* Prevent the fairly unlikely race where the gcblock is
	   entirely obsoleted by the final close of a file which had
	   the only valid nodes in the block, followed by erasure,
	   followed by freeing of the ic because the erased block(s)
	   held _all_ the nodes of that inode.... never been seen but
	   it's vaguely possible. */

	inum = ic->ino;
	nlink = ic->pino_nlink;
	spin_unlock(&c->inocache_lock);

	f = jffs2_gc_fetch_inode(c, inum, !nlink);
	if (IS_ERR(f)) {
		ret = PTR_ERR(f);
		goto release_sem;
	}
	if (!f) {
		ret = 0;
		goto release_sem;
	}

	ret = jffs2_garbage_collect_live(c, jeb, raw, f);

	jffs2_gc_release_inode(c, f);

 test_gcnode:
	if (jeb->dirty_size == gcblock_dirty && !ref_obsolete(jeb->gc_node)) {
		/* Eep. This really should never happen. GC is broken */
		pr_err("Error garbage collecting node at %08x!\n",
		       ref_offset(jeb->gc_node));
		ret = -ENOSPC;
	}
 release_sem:
	mutex_unlock(&c->alloc_sem);

 eraseit_lock:
	/* If we've finished this block, start it erasing */
	spin_lock(&c->erase_completion_lock);

 eraseit:
	if (c->gcblock && !c->gcblock->used_size) {
		jffs2_dbg(1, "Block at 0x%08x completely obsoleted by GC. Moving to erase_pending_list\n",
			  c->gcblock->offset);
		/* We're GC'ing an empty block? */
		list_add_tail(&c->gcblock->list, &c->erase_pending_list);
		c->gcblock = NULL;
		c->nr_erasing_blocks++;
		jffs2_garbage_collect_trigger(c);
	}
	spin_unlock(&c->erase_completion_lock);

	return ret;
}

static int jffs2_garbage_collect_live(struct jffs2_sb_info *c,  struct jffs2_eraseblock *jeb,
				      struct jffs2_raw_node_ref *raw, struct jffs2_inode_info *f)
{
	struct jffs2_node_frag *frag;
	struct jffs2_full_dnode *fn = NULL;
	struct jffs2_full_dirent *fd;
	uint32_t start = 0, end = 0, nrfrags = 0;
	int ret = 0;

	mutex_lock(&f->sem);

	/* Now we have the lock for this inode. Check that it's still the one at the head
	   of the list. */

	spin_lock(&c->erase_completion_lock);

	if (c->gcblock != jeb) {
		spin_unlock(&c->erase_completion_lock);
		jffs2_dbg(1, "GC block is no longer gcblock. Restart\n");
		goto upnout;
	}
	if (ref_obsolete(raw)) {
		spin_unlock(&c->erase_completion_lock);
		jffs2_dbg(1, "node to be GC'd was obsoleted in the meantime.\n");
		/* They'll call again */
		goto upnout;
	}
	spin_unlock(&c->erase_completion_lock);

	/* OK. Looks safe. And nobody can get us now because we have the semaphore. Move the block */
	if (f->metadata && f->metadata->raw == raw) {
		fn = f->metadata;
		ret = jffs2_garbage_collect_metadata(c, jeb, f, fn);
		goto upnout;
	}

	/* FIXME. Read node and do lookup? */
	for (frag = frag_first(&f->fragtree); frag; frag = frag_next(frag)) {
		if (frag->node && frag->node->raw == raw) {
			fn = frag->node;
			end = frag->ofs + frag->size;
			if (!nrfrags++)
				start = frag->ofs;
			if (nrfrags == frag->node->frags)
				break; /* We've found them all */
		}
	}
	if (fn) {
		if (ref_flags(raw) == REF_PRISTINE) {
			ret = jffs2_garbage_collect_pristine(c, f->inocache, raw);
			if (!ret) {
				/* Urgh. Return it sensibly. */
				frag->node->raw = f->inocache->nodes;
			}
			if (ret != -EBADFD)
				goto upnout;
		}
		/* We found a datanode. Do the GC */
		if((start >> PAGE_SHIFT) < ((end-1) >> PAGE_SHIFT)) {
			/* It crosses a page boundary. Therefore, it must be a hole. */
			ret = jffs2_garbage_collect_hole(c, jeb, f, fn, start, end);
		} else {
			/* It could still be a hole. But we GC the page this way anyway */
			ret = jffs2_garbage_collect_dnode(c, jeb, f, fn, start, end);
		}
		goto upnout;
	}

	/* Wasn't a dnode. Try dirent */
	for (fd = f->dents; fd; fd=fd->next) {
		if (fd->raw == raw)
			break;
	}

	if (fd && fd->ino) {
		ret = jffs2_garbage_collect_dirent(c, jeb, f, fd);
	} else if (fd) {
		ret = jffs2_garbage_collect_deletion_dirent(c, jeb, f, fd);
	} else {
		pr_warn("Raw node at 0x%08x wasn't in node lists for ino #%u\n",
			ref_offset(raw), f->inocache->ino);
		if (ref_obsolete(raw)) {
			pr_warn("But it's obsolete so we don't mind too much\n");
		} else {
			jffs2_dbg_dump_node(c, ref_offset(raw));
			BUG();
		}
	}
 upnout:
	mutex_unlock(&f->sem);

	return ret;
}

static int jffs2_garbage_collect_pristine(struct jffs2_sb_info *c,
					  struct jffs2_inode_cache *ic,
					  struct jffs2_raw_node_ref *raw)
{
	union jffs2_node_union *node;
	size_t retlen;
	int ret;
	uint32_t phys_ofs, alloclen;
	uint32_t crc, rawlen;
	int retried = 0;

	jffs2_dbg(1, "Going to GC REF_PRISTINE node at 0x%08x\n",
		  ref_offset(raw));

	alloclen = rawlen = ref_totlen(c, c->gcblock, raw);

	/* Ask for a small amount of space (or the totlen if smaller) because we
	   don't want to force wastage of the end of a block if splitting would
	   work. */
	if (ic && alloclen > sizeof(struct jffs2_raw_inode) + JFFS2_MIN_DATA_LEN)
		alloclen = sizeof(struct jffs2_raw_inode) + JFFS2_MIN_DATA_LEN;

	ret = jffs2_reserve_space_gc(c, alloclen, &alloclen, rawlen);
	/* 'rawlen' is not the exact summary size; it is only an upper estimation */

	if (ret)
		return ret;

	if (alloclen < rawlen) {
		/* Doesn't fit untouched. We'll go the old route and split it */
		return -EBADFD;
	}

	node = kmalloc(rawlen, GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	ret = jffs2_flash_read(c, ref_offset(raw), rawlen, &retlen, (char *)node);
	if (!ret && retlen != rawlen)
		ret = -EIO;
	if (ret)
		goto out_node;

	crc = crc32(0, node, sizeof(struct jffs2_unknown_node)-4);
	if (je32_to_cpu(node->u.hdr_crc) != crc) {
		pr_warn("Header CRC failed on REF_PRISTINE node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			ref_offset(raw), je32_to_cpu(node->u.hdr_crc), crc);
		goto bail;
	}

	switch(je16_to_cpu(node->u.nodetype)) {
	case JFFS2_NODETYPE_INODE:
		crc = crc32(0, node, sizeof(node->i)-8);
		if (je32_to_cpu(node->i.node_crc) != crc) {
			pr_warn("Node CRC failed on REF_PRISTINE data node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
				ref_offset(raw), je32_to_cpu(node->i.node_crc),
				crc);
			goto bail;
		}

		if (je32_to_cpu(node->i.dsize)) {
			crc = crc32(0, node->i.data, je32_to_cpu(node->i.csize));
			if (je32_to_cpu(node->i.data_crc) != crc) {
				pr_warn("Data CRC failed on REF_PRISTINE data node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
					ref_offset(raw),
					je32_to_cpu(node->i.data_crc), crc);
				goto bail;
			}
		}
		break;

	case JFFS2_NODETYPE_DIRENT:
		crc = crc32(0, node, sizeof(node->d)-8);
		if (je32_to_cpu(node->d.node_crc) != crc) {
			pr_warn("Node CRC failed on REF_PRISTINE dirent node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
				ref_offset(raw),
				je32_to_cpu(node->d.node_crc), crc);
			goto bail;
		}

		if (strnlen(node->d.name, node->d.nsize) != node->d.nsize) {
			pr_warn("Name in dirent node at 0x%08x contains zeroes\n",
				ref_offset(raw));
			goto bail;
		}

		if (node->d.nsize) {
			crc = crc32(0, node->d.name, node->d.nsize);
			if (je32_to_cpu(node->d.name_crc) != crc) {
				pr_warn("Name CRC failed on REF_PRISTINE dirent node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
					ref_offset(raw),
					je32_to_cpu(node->d.name_crc), crc);
				goto bail;
			}
		}
		break;
	default:
		/* If it's inode-less, we don't _know_ what it is. Just copy it intact */
		if (ic) {
			pr_warn("Unknown node type for REF_PRISTINE node at 0x%08x: 0x%04x\n",
				ref_offset(raw), je16_to_cpu(node->u.nodetype));
			goto bail;
		}
	}

	/* OK, all the CRCs are good; this node can just be copied as-is. */
 retry:
	phys_ofs = write_ofs(c);

	ret = jffs2_flash_write(c, phys_ofs, rawlen, &retlen, (char *)node);

	if (ret || (retlen != rawlen)) {
		pr_notice("Write of %d bytes at 0x%08x failed. returned %d, retlen %zd\n",
			  rawlen, phys_ofs, ret, retlen);
		if (retlen) {
			jffs2_add_physical_node_ref(c, phys_ofs | REF_OBSOLETE, rawlen, NULL);
		} else {
			pr_notice("Not marking the space at 0x%08x as dirty because the flash driver returned retlen zero\n",
				  phys_ofs);
		}
		if (!retried) {
			/* Try to reallocate space and retry */
			uint32_t dummy;
			struct jffs2_eraseblock *jeb = &c->blocks[phys_ofs / c->sector_size];

			retried = 1;

			jffs2_dbg(1, "Retrying failed write of REF_PRISTINE node.\n");

			jffs2_dbg_acct_sanity_check(c,jeb);
			jffs2_dbg_acct_paranoia_check(c, jeb);

			ret = jffs2_reserve_space_gc(c, rawlen, &dummy, rawlen);
						/* this is not the exact summary size of it,
							it is only an upper estimation */

			if (!ret) {
				jffs2_dbg(1, "Allocated space at 0x%08x to retry failed write.\n",
					  phys_ofs);

				jffs2_dbg_acct_sanity_check(c,jeb);
				jffs2_dbg_acct_paranoia_check(c, jeb);

				goto retry;
			}
			jffs2_dbg(1, "Failed to allocate space to retry failed write: %d!\n",
				  ret);
		}

		if (!ret)
			ret = -EIO;
		goto out_node;
	}
	jffs2_add_physical_node_ref(c, phys_ofs | REF_PRISTINE, rawlen, ic);

	jffs2_mark_node_obsolete(c, raw);
	jffs2_dbg(1, "WHEEE! GC REF_PRISTINE node at 0x%08x succeeded\n",
		  ref_offset(raw));

 out_node:
	kfree(node);
	return ret;
 bail:
	ret = -EBADFD;
	goto out_node;
}

static int jffs2_garbage_collect_metadata(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_inode_info *f, struct jffs2_full_dnode *fn)
{
	struct jffs2_full_dnode *new_fn;
	struct jffs2_raw_inode ri;
	struct jffs2_node_frag *last_frag;
	union jffs2_device_node dev;
	char *mdata = NULL;
	int mdatalen = 0;
	uint32_t alloclen, ilen;
	int ret;

	if (S_ISBLK(JFFS2_F_I_MODE(f)) ||
	    S_ISCHR(JFFS2_F_I_MODE(f)) ) {
		/* For these, we don't actually need to read the old node */
		mdatalen = jffs2_encode_dev(&dev, JFFS2_F_I_RDEV(f));
		mdata = (char *)&dev;
		jffs2_dbg(1, "%s(): Writing %d bytes of kdev_t\n",
			  __func__, mdatalen);
	} else if (S_ISLNK(JFFS2_F_I_MODE(f))) {
		mdatalen = fn->size;
		mdata = kmalloc(fn->size, GFP_KERNEL);
		if (!mdata) {
			pr_warn("kmalloc of mdata failed in jffs2_garbage_collect_metadata()\n");
			return -ENOMEM;
		}
		ret = jffs2_read_dnode(c, f, fn, mdata, 0, mdatalen);
		if (ret) {
			pr_warn("read of old metadata failed in jffs2_garbage_collect_metadata(): %d\n",
				ret);
			kfree(mdata);
			return ret;
		}
		jffs2_dbg(1, "%s(): Writing %d bites of symlink target\n",
			  __func__, mdatalen);

	}

	ret = jffs2_reserve_space_gc(c, sizeof(ri) + mdatalen, &alloclen,
				JFFS2_SUMMARY_INODE_SIZE);
	if (ret) {
		pr_warn("jffs2_reserve_space_gc of %zd bytes for garbage_collect_metadata failed: %d\n",
			sizeof(ri) + mdatalen, ret);
		goto out;
	}

	last_frag = frag_last(&f->fragtree);
	if (last_frag)
		/* Fetch the inode length from the fragtree rather then
		 * from i_size since i_size may have not been updated yet */
		ilen = last_frag->ofs + last_frag->size;
	else
		ilen = JFFS2_F_I_SIZE(f);

	memset(&ri, 0, sizeof(ri));
	ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri.nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
	ri.totlen = cpu_to_je32(sizeof(ri) + mdatalen);
	ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4));

	ri.ino = cpu_to_je32(f->inocache->ino);
	ri.version = cpu_to_je32(++f->highest_version);
	ri.mode = cpu_to_jemode(JFFS2_F_I_MODE(f));
	ri.uid = cpu_to_je16(JFFS2_F_I_UID(f));
	ri.gid = cpu_to_je16(JFFS2_F_I_GID(f));
	ri.isize = cpu_to_je32(ilen);
	ri.atime = cpu_to_je32(JFFS2_F_I_ATIME(f));
	ri.ctime = cpu_to_je32(JFFS2_F_I_CTIME(f));
	ri.mtime = cpu_to_je32(JFFS2_F_I_MTIME(f));
	ri.offset = cpu_to_je32(0);
	ri.csize = cpu_to_je32(mdatalen);
	ri.dsize = cpu_to_je32(mdatalen);
	ri.compr = JFFS2_COMPR_NONE;
	ri.node_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
	ri.data_crc = cpu_to_je32(crc32(0, mdata, mdatalen));

	new_fn = jffs2_write_dnode(c, f, &ri, mdata, mdatalen, ALLOC_GC);

	if (IS_ERR(new_fn)) {
		pr_warn("Error writing new dnode: %ld\n", PTR_ERR(new_fn));
		ret = PTR_ERR(new_fn);
		goto out;
	}
	jffs2_mark_node_obsolete(c, fn->raw);
	jffs2_free_full_dnode(fn);
	f->metadata = new_fn;
 out:
	if (S_ISLNK(JFFS2_F_I_MODE(f)))
		kfree(mdata);
	return ret;
}

static int jffs2_garbage_collect_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent *new_fd;
	struct jffs2_raw_dirent rd;
	uint32_t alloclen;
	int ret;

	rd.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd.nodetype = cpu_to_je16(JFFS2_NODETYPE_DIRENT);
	rd.nsize = strlen(fd->name);
	rd.totlen = cpu_to_je32(sizeof(rd) + rd.nsize);
	rd.hdr_crc = cpu_to_je32(crc32(0, &rd, sizeof(struct jffs2_unknown_node)-4));

	rd.pino = cpu_to_je32(f->inocache->ino);
	rd.version = cpu_to_je32(++f->highest_version);
	rd.ino = cpu_to_je32(fd->ino);
	/* If the times on this inode were set by explicit utime() they can be different,
	   so refrain from splatting them. */
	if (JFFS2_F_I_MTIME(f) == JFFS2_F_I_CTIME(f))
		rd.mctime = cpu_to_je32(JFFS2_F_I_MTIME(f));
	else
		rd.mctime = cpu_to_je32(0);
	rd.type = fd->type;
	rd.node_crc = cpu_to_je32(crc32(0, &rd, sizeof(rd)-8));
	rd.name_crc = cpu_to_je32(crc32(0, fd->name, rd.nsize));

	ret = jffs2_reserve_space_gc(c, sizeof(rd)+rd.nsize, &alloclen,
				JFFS2_SUMMARY_DIRENT_SIZE(rd.nsize));
	if (ret) {
		pr_warn("jffs2_reserve_space_gc of %zd bytes for garbage_collect_dirent failed: %d\n",
			sizeof(rd)+rd.nsize, ret);
		return ret;
	}
	new_fd = jffs2_write_dirent(c, f, &rd, fd->name, rd.nsize, ALLOC_GC);

	if (IS_ERR(new_fd)) {
		pr_warn("jffs2_write_dirent in garbage_collect_dirent failed: %ld\n",
			PTR_ERR(new_fd));
		return PTR_ERR(new_fd);
	}
	jffs2_add_fd_to_list(c, new_fd, &f->dents);
	return 0;
}

static int jffs2_garbage_collect_deletion_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_inode_info *f, struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent **fdp = &f->dents;
	int found = 0;

	/* On a medium where we can't actually mark nodes obsolete
	   pernamently, such as NAND flash, we need to work out
	   whether this deletion dirent is still needed to actively
	   delete a 'real' dirent with the same name that's still
	   somewhere else on the flash. */
	if (!jffs2_can_mark_obsolete(c)) {
		struct jffs2_raw_dirent *rd;
		struct jffs2_raw_node_ref *raw;
		int ret;
		size_t retlen;
		int name_len = strlen(fd->name);
		uint32_t name_crc = crc32(0, fd->name, name_len);
		uint32_t rawlen = ref_totlen(c, jeb, fd->raw);

		rd = kmalloc(rawlen, GFP_KERNEL);
		if (!rd)
			return -ENOMEM;

		/* Prevent the erase code from nicking the obsolete node refs while
		   we're looking at them. I really don't like this extra lock but
		   can't see any alternative. Suggestions on a postcard to... */
		mutex_lock(&c->erase_free_sem);

		for (raw = f->inocache->nodes; raw != (void *)f->inocache; raw = raw->next_in_ino) {

			cond_resched();

			/* We only care about obsolete ones */
			if (!(ref_obsolete(raw)))
				continue;

			/* Any dirent with the same name is going to have the same length... */
			if (ref_totlen(c, NULL, raw) != rawlen)
				continue;

			/* Doesn't matter if there's one in the same erase block. We're going to
			   delete it too at the same time. */
			if (SECTOR_ADDR(raw->flash_offset) == SECTOR_ADDR(fd->raw->flash_offset))
				continue;

			jffs2_dbg(1, "Check potential deletion dirent at %08x\n",
				  ref_offset(raw));

			/* This is an obsolete node belonging to the same directory, and it's of the right
			   length. We need to take a closer look...*/
			ret = jffs2_flash_read(c, ref_offset(raw), rawlen, &retlen, (char *)rd);
			if (ret) {
				pr_warn("%s(): Read error (%d) reading obsolete node at %08x\n",
					__func__, ret, ref_offset(raw));
				/* If we can't read it, we don't need to continue to obsolete it. Continue */
				continue;
			}
			if (retlen != rawlen) {
				pr_warn("%s(): Short read (%zd not %u) reading header from obsolete node at %08x\n",
					__func__, retlen, rawlen,
					ref_offset(raw));
				continue;
			}

			if (je16_to_cpu(rd->nodetype) != JFFS2_NODETYPE_DIRENT)
				continue;

			/* If the name CRC doesn't match, skip */
			if (je32_to_cpu(rd->name_crc) != name_crc)
				continue;

			/* If the name length doesn't match, or it's another deletion dirent, skip */
			if (rd->nsize != name_len || !je32_to_cpu(rd->ino))
				continue;

			/* OK, check the actual name now */
			if (memcmp(rd->name, fd->name, name_len))
				continue;

			/* OK. The name really does match. There really is still an older node on
			   the flash which our deletion dirent obsoletes. So we have to write out
			   a new deletion dirent to replace it */
			mutex_unlock(&c->erase_free_sem);

			jffs2_dbg(1, "Deletion dirent at %08x still obsoletes real dirent \"%s\" at %08x for ino #%u\n",
				  ref_offset(fd->raw), fd->name,
				  ref_offset(raw), je32_to_cpu(rd->ino));
			kfree(rd);

			return jffs2_garbage_collect_dirent(c, jeb, f, fd);
		}

		mutex_unlock(&c->erase_free_sem);
		kfree(rd);
	}

	/* FIXME: If we're deleting a dirent which contains the current mtime and ctime,
	   we should update the metadata node with those times accordingly */

	/* No need for it any more. Just mark it obsolete and remove it from the list */
	while (*fdp) {
		if ((*fdp) == fd) {
			found = 1;
			*fdp = fd->next;
			break;
		}
		fdp = &(*fdp)->next;
	}
	if (!found) {
		pr_warn("Deletion dirent \"%s\" not found in list for ino #%u\n",
			fd->name, f->inocache->ino);
	}
	jffs2_mark_node_obsolete(c, fd->raw);
	jffs2_free_full_dirent(fd);
	return 0;
}

static int jffs2_garbage_collect_hole(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				      struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				      uint32_t start, uint32_t end)
{
	struct jffs2_raw_inode ri;
	struct jffs2_node_frag *frag;
	struct jffs2_full_dnode *new_fn;
	uint32_t alloclen, ilen;
	int ret;

	jffs2_dbg(1, "Writing replacement hole node for ino #%u from offset 0x%x to 0x%x\n",
		  f->inocache->ino, start, end);

	memset(&ri, 0, sizeof(ri));

	if(fn->frags > 1) {
		size_t readlen;
		uint32_t crc;
		/* It's partially obsoleted by a later write. So we have to
		   write it out again with the _same_ version as before */
		ret = jffs2_flash_read(c, ref_offset(fn->raw), sizeof(ri), &readlen, (char *)&ri);
		if (readlen != sizeof(ri) || ret) {
			pr_warn("Node read failed in jffs2_garbage_collect_hole. Ret %d, retlen %zd. Data will be lost by writing new hole node\n",
				ret, readlen);
			goto fill;
		}
		if (je16_to_cpu(ri.nodetype) != JFFS2_NODETYPE_INODE) {
			pr_warn("%s(): Node at 0x%08x had node type 0x%04x instead of JFFS2_NODETYPE_INODE(0x%04x)\n",
				__func__, ref_offset(fn->raw),
				je16_to_cpu(ri.nodetype), JFFS2_NODETYPE_INODE);
			return -EIO;
		}
		if (je32_to_cpu(ri.totlen) != sizeof(ri)) {
			pr_warn("%s(): Node at 0x%08x had totlen 0x%x instead of expected 0x%zx\n",
				__func__, ref_offset(fn->raw),
				je32_to_cpu(ri.totlen), sizeof(ri));
			return -EIO;
		}
		crc = crc32(0, &ri, sizeof(ri)-8);
		if (crc != je32_to_cpu(ri.node_crc)) {
			pr_warn("%s: Node at 0x%08x had CRC 0x%08x which doesn't match calculated CRC 0x%08x\n",
				__func__, ref_offset(fn->raw),
				je32_to_cpu(ri.node_crc), crc);
			/* FIXME: We could possibly deal with this by writing new holes for each frag */
			pr_warn("Data in the range 0x%08x to 0x%08x of inode #%u will be lost\n",
				start, end, f->inocache->ino);
			goto fill;
		}
		if (ri.compr != JFFS2_COMPR_ZERO) {
			pr_warn("%s(): Node 0x%08x wasn't a hole node!\n",
				__func__, ref_offset(fn->raw));
			pr_warn("Data in the range 0x%08x to 0x%08x of inode #%u will be lost\n",
				start, end, f->inocache->ino);
			goto fill;
		}
	} else {
	fill:
		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
		ri.totlen = cpu_to_je32(sizeof(ri));
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4));

		ri.ino = cpu_to_je32(f->inocache->ino);
		ri.version = cpu_to_je32(++f->highest_version);
		ri.offset = cpu_to_je32(start);
		ri.dsize = cpu_to_je32(end - start);
		ri.csize = cpu_to_je32(0);
		ri.compr = JFFS2_COMPR_ZERO;
	}

	frag = frag_last(&f->fragtree);
	if (frag)
		/* Fetch the inode length from the fragtree rather then
		 * from i_size since i_size may have not been updated yet */
		ilen = frag->ofs + frag->size;
	else
		ilen = JFFS2_F_I_SIZE(f);

	ri.mode = cpu_to_jemode(JFFS2_F_I_MODE(f));
	ri.uid = cpu_to_je16(JFFS2_F_I_UID(f));
	ri.gid = cpu_to_je16(JFFS2_F_I_GID(f));
	ri.isize = cpu_to_je32(ilen);
	ri.atime = cpu_to_je32(JFFS2_F_I_ATIME(f));
	ri.ctime = cpu_to_je32(JFFS2_F_I_CTIME(f));
	ri.mtime = cpu_to_je32(JFFS2_F_I_MTIME(f));
	ri.data_crc = cpu_to_je32(0);
	ri.node_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));

	ret = jffs2_reserve_space_gc(c, sizeof(ri), &alloclen,
				     JFFS2_SUMMARY_INODE_SIZE);
	if (ret) {
		pr_warn("jffs2_reserve_space_gc of %zd bytes for garbage_collect_hole failed: %d\n",
			sizeof(ri), ret);
		return ret;
	}
	new_fn = jffs2_write_dnode(c, f, &ri, NULL, 0, ALLOC_GC);

	if (IS_ERR(new_fn)) {
		pr_warn("Error writing new hole node: %ld\n", PTR_ERR(new_fn));
		return PTR_ERR(new_fn);
	}
	if (je32_to_cpu(ri.version) == f->highest_version) {
		jffs2_add_full_dnode_to_inode(c, f, new_fn);
		if (f->metadata) {
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
		}
		return 0;
	}

	/*
	 * We should only get here in the case where the node we are
	 * replacing had more than one frag, so we kept the same version
	 * number as before. (Except in case of error -- see 'goto fill;'
	 * above.)
	 */
	D1(if(unlikely(fn->frags <= 1)) {
			pr_warn("%s(): Replacing fn with %d frag(s) but new ver %d != highest_version %d of ino #%d\n",
				__func__, fn->frags, je32_to_cpu(ri.version),
				f->highest_version, je32_to_cpu(ri.ino));
	});

	/* This is a partially-overlapped hole node. Mark it REF_NORMAL not REF_PRISTINE */
	mark_ref_normal(new_fn->raw);

	for (frag = jffs2_lookup_node_frag(&f->fragtree, fn->ofs);
	     frag; frag = frag_next(frag)) {
		if (frag->ofs > fn->size + fn->ofs)
			break;
		if (frag->node == fn) {
			frag->node = new_fn;
			new_fn->frags++;
			fn->frags--;
		}
	}
	if (fn->frags) {
		pr_warn("%s(): Old node still has frags!\n", __func__);
		BUG();
	}
	if (!new_fn->frags) {
		pr_warn("%s(): New node has no frags!\n", __func__);
		BUG();
	}

	jffs2_mark_node_obsolete(c, fn->raw);
	jffs2_free_full_dnode(fn);

	return 0;
}

static int jffs2_garbage_collect_dnode(struct jffs2_sb_info *c, struct jffs2_eraseblock *orig_jeb,
				       struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				       uint32_t start, uint32_t end)
{
	struct inode *inode = OFNI_EDONI_2SFFJ(f);
	struct jffs2_full_dnode *new_fn;
	struct jffs2_raw_inode ri;
	uint32_t alloclen, offset, orig_end, orig_start;
	int ret = 0;
	unsigned char *comprbuf = NULL, *writebuf;
	struct folio *folio;
	unsigned char *pg_ptr;

	memset(&ri, 0, sizeof(ri));

	jffs2_dbg(1, "Writing replacement dnode for ino #%u from offset 0x%x to 0x%x\n",
		  f->inocache->ino, start, end);

	orig_end = end;
	orig_start = start;

	if (c->nr_free_blocks + c->nr_erasing_blocks > c->resv_blocks_gcmerge) {
		/* Attempt to do some merging. But only expand to cover logically
		   adjacent frags if the block containing them is already considered
		   to be dirty. Otherwise we end up with GC just going round in
		   circles dirtying the nodes it already wrote out, especially
		   on NAND where we have small eraseblocks and hence a much higher
		   chance of nodes having to be split to cross boundaries. */

		struct jffs2_node_frag *frag;
		uint32_t min, max;

		min = start & ~(PAGE_SIZE-1);
		max = min + PAGE_SIZE;

		frag = jffs2_lookup_node_frag(&f->fragtree, start);

		/* BUG_ON(!frag) but that'll happen anyway... */

		BUG_ON(frag->ofs != start);

		/* First grow down... */
		while((frag = frag_prev(frag)) && frag->ofs >= min) {

			/* If the previous frag doesn't even reach the beginning, there's
			   excessive fragmentation. Just merge. */
			if (frag->ofs > min) {
				jffs2_dbg(1, "Expanding down to cover partial frag (0x%x-0x%x)\n",
					  frag->ofs, frag->ofs+frag->size);
				start = frag->ofs;
				continue;
			}
			/* OK. This frag holds the first byte of the page. */
			if (!frag->node || !frag->node->raw) {
				jffs2_dbg(1, "First frag in page is hole (0x%x-0x%x). Not expanding down.\n",
					  frag->ofs, frag->ofs+frag->size);
				break;
			} else {

				/* OK, it's a frag which extends to the beginning of the page. Does it live
				   in a block which is still considered clean? If so, don't obsolete it.
				   If not, cover it anyway. */

				struct jffs2_raw_node_ref *raw = frag->node->raw;
				struct jffs2_eraseblock *jeb;

				jeb = &c->blocks[raw->flash_offset / c->sector_size];

				if (jeb == c->gcblock) {
					jffs2_dbg(1, "Expanding down to cover frag (0x%x-0x%x) in gcblock at %08x\n",
						  frag->ofs,
						  frag->ofs + frag->size,
						  ref_offset(raw));
					start = frag->ofs;
					break;
				}
				if (!ISDIRTY(jeb->dirty_size + jeb->wasted_size)) {
					jffs2_dbg(1, "Not expanding down to cover frag (0x%x-0x%x) in clean block %08x\n",
						  frag->ofs,
						  frag->ofs + frag->size,
						  jeb->offset);
					break;
				}

				jffs2_dbg(1, "Expanding down to cover frag (0x%x-0x%x) in dirty block %08x\n",
					  frag->ofs,
					  frag->ofs + frag->size,
					  jeb->offset);
				start = frag->ofs;
				break;
			}
		}

		/* ... then up */

		/* Find last frag which is actually part of the node we're to GC. */
		frag = jffs2_lookup_node_frag(&f->fragtree, end-1);

		while((frag = frag_next(frag)) && frag->ofs+frag->size <= max) {

			/* If the previous frag doesn't even reach the beginning, there's lots
			   of fragmentation. Just merge. */
			if (frag->ofs+frag->size < max) {
				jffs2_dbg(1, "Expanding up to cover partial frag (0x%x-0x%x)\n",
					  frag->ofs, frag->ofs+frag->size);
				end = frag->ofs + frag->size;
				continue;
			}

			if (!frag->node || !frag->node->raw) {
				jffs2_dbg(1, "Last frag in page is hole (0x%x-0x%x). Not expanding up.\n",
					  frag->ofs, frag->ofs+frag->size);
				break;
			} else {

				/* OK, it's a frag which extends to the beginning of the page. Does it live
				   in a block which is still considered clean? If so, don't obsolete it.
				   If not, cover it anyway. */

				struct jffs2_raw_node_ref *raw = frag->node->raw;
				struct jffs2_eraseblock *jeb;

				jeb = &c->blocks[raw->flash_offset / c->sector_size];

				if (jeb == c->gcblock) {
					jffs2_dbg(1, "Expanding up to cover frag (0x%x-0x%x) in gcblock at %08x\n",
						  frag->ofs,
						  frag->ofs + frag->size,
						  ref_offset(raw));
					end = frag->ofs + frag->size;
					break;
				}
				if (!ISDIRTY(jeb->dirty_size + jeb->wasted_size)) {
					jffs2_dbg(1, "Not expanding up to cover frag (0x%x-0x%x) in clean block %08x\n",
						  frag->ofs,
						  frag->ofs + frag->size,
						  jeb->offset);
					break;
				}

				jffs2_dbg(1, "Expanding up to cover frag (0x%x-0x%x) in dirty block %08x\n",
					  frag->ofs,
					  frag->ofs + frag->size,
					  jeb->offset);
				end = frag->ofs + frag->size;
				break;
			}
		}
		jffs2_dbg(1, "Expanded dnode to write from (0x%x-0x%x) to (0x%x-0x%x)\n",
			  orig_start, orig_end, start, end);

		D1(BUG_ON(end > frag_last(&f->fragtree)->ofs + frag_last(&f->fragtree)->size));
		BUG_ON(end < orig_end);
		BUG_ON(start > orig_start);
	}

	/* The rules state that we must obtain the folio lock *before* f->sem, so
	 * drop f->sem temporarily. Since we also hold c->alloc_sem, nothing's
	 * actually going to *change* so we're safe; we only allow reading.
	 *
	 * It is important to note that jffs2_write_begin() will ensure that its
	 * folio is marked uptodate before allocating space. That means that if we
	 * end up here trying to GC the *same* folio that jffs2_write_begin() is
	 * trying to write out, read_cache_folio() will not deadlock. */
	mutex_unlock(&f->sem);
	folio = read_cache_folio(inode->i_mapping, start >> PAGE_SHIFT,
			       __jffs2_read_folio, NULL);
	if (IS_ERR(folio)) {
		pr_warn("read_cache_folio() returned error: %ld\n",
			PTR_ERR(folio));
		mutex_lock(&f->sem);
		return PTR_ERR(folio);
	}

	pg_ptr = kmap_local_folio(folio, 0);
	mutex_lock(&f->sem);

	offset = start;
	while(offset < orig_end) {
		uint32_t datalen;
		uint32_t cdatalen;
		uint16_t comprtype = JFFS2_COMPR_NONE;

		ret = jffs2_reserve_space_gc(c, sizeof(ri) + JFFS2_MIN_DATA_LEN,
					&alloclen, JFFS2_SUMMARY_INODE_SIZE);

		if (ret) {
			pr_warn("jffs2_reserve_space_gc of %zd bytes for garbage_collect_dnode failed: %d\n",
				sizeof(ri) + JFFS2_MIN_DATA_LEN, ret);
			break;
		}
		cdatalen = min_t(uint32_t, alloclen - sizeof(ri), end - offset);
		datalen = end - offset;

		writebuf = pg_ptr + (offset & (PAGE_SIZE -1));

		comprtype = jffs2_compress(c, f, writebuf, &comprbuf, &datalen, &cdatalen);

		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
		ri.totlen = cpu_to_je32(sizeof(ri) + cdatalen);
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unknown_node)-4));

		ri.ino = cpu_to_je32(f->inocache->ino);
		ri.version = cpu_to_je32(++f->highest_version);
		ri.mode = cpu_to_jemode(JFFS2_F_I_MODE(f));
		ri.uid = cpu_to_je16(JFFS2_F_I_UID(f));
		ri.gid = cpu_to_je16(JFFS2_F_I_GID(f));
		ri.isize = cpu_to_je32(JFFS2_F_I_SIZE(f));
		ri.atime = cpu_to_je32(JFFS2_F_I_ATIME(f));
		ri.ctime = cpu_to_je32(JFFS2_F_I_CTIME(f));
		ri.mtime = cpu_to_je32(JFFS2_F_I_MTIME(f));
		ri.offset = cpu_to_je32(offset);
		ri.csize = cpu_to_je32(cdatalen);
		ri.dsize = cpu_to_je32(datalen);
		ri.compr = comprtype & 0xff;
		ri.usercompr = (comprtype >> 8) & 0xff;
		ri.node_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
		ri.data_crc = cpu_to_je32(crc32(0, comprbuf, cdatalen));

		new_fn = jffs2_write_dnode(c, f, &ri, comprbuf, cdatalen, ALLOC_GC);

		jffs2_free_comprbuf(comprbuf, writebuf);

		if (IS_ERR(new_fn)) {
			pr_warn("Error writing new dnode: %ld\n",
				PTR_ERR(new_fn));
			ret = PTR_ERR(new_fn);
			break;
		}
		ret = jffs2_add_full_dnode_to_inode(c, f, new_fn);
		offset += datalen;
		if (f->metadata) {
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
		}
	}

	folio_release_kmap(folio, pg_ptr);
	return ret;
}
