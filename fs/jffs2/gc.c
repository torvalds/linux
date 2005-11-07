/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: gc.c,v 1.155 2005/11/07 11:14:39 gleixner Exp $
 *
 */

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
		D1(printk(KERN_DEBUG "Picking block from bad_used_list to GC next\n"));
		nextlist = &c->bad_used_list;
	} else if (n < 50 && !list_empty(&c->erasable_list)) {
		/* Note that most of them will have gone directly to be erased.
		   So don't favour the erasable_list _too_ much. */
		D1(printk(KERN_DEBUG "Picking block from erasable_list to GC next\n"));
		nextlist = &c->erasable_list;
	} else if (n < 110 && !list_empty(&c->very_dirty_list)) {
		/* Most of the time, pick one off the very_dirty list */
		D1(printk(KERN_DEBUG "Picking block from very_dirty_list to GC next\n"));
		nextlist = &c->very_dirty_list;
	} else if (n < 126 && !list_empty(&c->dirty_list)) {
		D1(printk(KERN_DEBUG "Picking block from dirty_list to GC next\n"));
		nextlist = &c->dirty_list;
	} else if (!list_empty(&c->clean_list)) {
		D1(printk(KERN_DEBUG "Picking block from clean_list to GC next\n"));
		nextlist = &c->clean_list;
	} else if (!list_empty(&c->dirty_list)) {
		D1(printk(KERN_DEBUG "Picking block from dirty_list to GC next (clean_list was empty)\n"));

		nextlist = &c->dirty_list;
	} else if (!list_empty(&c->very_dirty_list)) {
		D1(printk(KERN_DEBUG "Picking block from very_dirty_list to GC next (clean_list and dirty_list were empty)\n"));
		nextlist = &c->very_dirty_list;
	} else if (!list_empty(&c->erasable_list)) {
		D1(printk(KERN_DEBUG "Picking block from erasable_list to GC next (clean_list and {very_,}dirty_list were empty)\n"));

		nextlist = &c->erasable_list;
	} else if (!list_empty(&c->erasable_pending_wbuf_list)) {
		/* There are blocks are wating for the wbuf sync */
		D1(printk(KERN_DEBUG "Synching wbuf in order to reuse erasable_pending_wbuf_list blocks\n"));
		spin_unlock(&c->erase_completion_lock);
		jffs2_flush_wbuf_pad(c);
		spin_lock(&c->erase_completion_lock);
		goto again;
	} else {
		/* Eep. All were empty */
		D1(printk(KERN_NOTICE "jffs2: No clean, dirty _or_ erasable blocks to GC from! Where are they all?\n"));
		return NULL;
	}

	ret = list_entry(nextlist->next, struct jffs2_eraseblock, list);
	list_del(&ret->list);
	c->gcblock = ret;
	ret->gc_node = ret->first_node;
	if (!ret->gc_node) {
		printk(KERN_WARNING "Eep. ret->gc_node for block at 0x%08x is NULL\n", ret->offset);
		BUG();
	}

	/* Have we accidentally picked a clean block with wasted space ? */
	if (ret->wasted_size) {
		D1(printk(KERN_DEBUG "Converting wasted_size %08x to dirty_size\n", ret->wasted_size));
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
	int ret = 0, inum, nlink;

	if (down_interruptible(&c->alloc_sem))
		return -EINTR;

	for (;;) {
		spin_lock(&c->erase_completion_lock);
		if (!c->unchecked_size)
			break;

		/* We can't start doing GC yet. We haven't finished checking
		   the node CRCs etc. Do it now. */

		/* checked_ino is protected by the alloc_sem */
		if (c->checked_ino > c->highest_ino) {
			printk(KERN_CRIT "Checked all inodes but still 0x%x bytes of unchecked space?\n",
			       c->unchecked_size);
			jffs2_dbg_dump_block_lists_nolock(c);
			spin_unlock(&c->erase_completion_lock);
			BUG();
		}

		spin_unlock(&c->erase_completion_lock);

		spin_lock(&c->inocache_lock);

		ic = jffs2_get_ino_cache(c, c->checked_ino++);

		if (!ic) {
			spin_unlock(&c->inocache_lock);
			continue;
		}

		if (!ic->nlink) {
			D1(printk(KERN_DEBUG "Skipping check of ino #%d with nlink zero\n",
				  ic->ino));
			spin_unlock(&c->inocache_lock);
			continue;
		}
		switch(ic->state) {
		case INO_STATE_CHECKEDABSENT:
		case INO_STATE_PRESENT:
			D1(printk(KERN_DEBUG "Skipping ino #%u already checked\n", ic->ino));
			spin_unlock(&c->inocache_lock);
			continue;

		case INO_STATE_GC:
		case INO_STATE_CHECKING:
			printk(KERN_WARNING "Inode #%u is in state %d during CRC check phase!\n", ic->ino, ic->state);
			spin_unlock(&c->inocache_lock);
			BUG();

		case INO_STATE_READING:
			/* We need to wait for it to finish, lest we move on
			   and trigger the BUG() above while we haven't yet
			   finished checking all its nodes */
			D1(printk(KERN_DEBUG "Waiting for ino #%u to finish reading\n", ic->ino));
			up(&c->alloc_sem);
			sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
			return 0;

		default:
			BUG();

		case INO_STATE_UNCHECKED:
			;
		}
		ic->state = INO_STATE_CHECKING;
		spin_unlock(&c->inocache_lock);

		D1(printk(KERN_DEBUG "jffs2_garbage_collect_pass() triggering inode scan of ino#%u\n", ic->ino));

		ret = jffs2_do_crccheck_inode(c, ic);
		if (ret)
			printk(KERN_WARNING "Returned error for crccheck of ino #%u. Expect badness...\n", ic->ino);

		jffs2_set_inocache_state(c, ic, INO_STATE_CHECKEDABSENT);
		up(&c->alloc_sem);
		return ret;
	}

	/* First, work out which block we're garbage-collecting */
	jeb = c->gcblock;

	if (!jeb)
		jeb = jffs2_find_gc_block(c);

	if (!jeb) {
		D1 (printk(KERN_NOTICE "jffs2: Couldn't find erase block to garbage collect!\n"));
		spin_unlock(&c->erase_completion_lock);
		up(&c->alloc_sem);
		return -EIO;
	}

	D1(printk(KERN_DEBUG "GC from block %08x, used_size %08x, dirty_size %08x, free_size %08x\n", jeb->offset, jeb->used_size, jeb->dirty_size, jeb->free_size));
	D1(if (c->nextblock)
	   printk(KERN_DEBUG "Nextblock at  %08x, used_size %08x, dirty_size %08x, wasted_size %08x, free_size %08x\n", c->nextblock->offset, c->nextblock->used_size, c->nextblock->dirty_size, c->nextblock->wasted_size, c->nextblock->free_size));

	if (!jeb->used_size) {
		up(&c->alloc_sem);
		goto eraseit;
	}

	raw = jeb->gc_node;

	while(ref_obsolete(raw)) {
		D1(printk(KERN_DEBUG "Node at 0x%08x is obsolete... skipping\n", ref_offset(raw)));
		raw = raw->next_phys;
		if (unlikely(!raw)) {
			printk(KERN_WARNING "eep. End of raw list while still supposedly nodes to GC\n");
			printk(KERN_WARNING "erase block at 0x%08x. free_size 0x%08x, dirty_size 0x%08x, used_size 0x%08x\n",
			       jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size);
			jeb->gc_node = raw;
			spin_unlock(&c->erase_completion_lock);
			up(&c->alloc_sem);
			BUG();
		}
	}
	jeb->gc_node = raw;

	D1(printk(KERN_DEBUG "Going to garbage collect node at 0x%08x\n", ref_offset(raw)));

	if (!raw->next_in_ino) {
		/* Inode-less node. Clean marker, snapshot or something like that */
		/* FIXME: If it's something that needs to be copied, including something
		   we don't grok that has JFFS2_NODETYPE_RWCOMPAT_COPY, we should do so */
		spin_unlock(&c->erase_completion_lock);
		jffs2_mark_node_obsolete(c, raw);
		up(&c->alloc_sem);
		goto eraseit_lock;
	}

	ic = jffs2_raw_ref_to_ic(raw);

	/* We need to hold the inocache. Either the erase_completion_lock or
	   the inocache_lock are sufficient; we trade down since the inocache_lock
	   causes less contention. */
	spin_lock(&c->inocache_lock);

	spin_unlock(&c->erase_completion_lock);

	D1(printk(KERN_DEBUG "jffs2_garbage_collect_pass collecting from block @0x%08x. Node @0x%08x(%d), ino #%u\n", jeb->offset, ref_offset(raw), ref_flags(raw), ic->ino));

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
			D1(printk(KERN_DEBUG "Ino #%u is absent but node not REF_PRISTINE. Reading.\n",
				  ic->ino));
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
		printk(KERN_CRIT "Inode #%u already in state %d in jffs2_garbage_collect_pass()!\n",
		       ic->ino, ic->state);
		up(&c->alloc_sem);
		spin_unlock(&c->inocache_lock);
		BUG();

	case INO_STATE_READING:
		/* Someone's currently trying to read it. We must wait for
		   them to finish and then go through the full iget() route
		   to do the GC. However, sometimes read_inode() needs to get
		   the alloc_sem() (for marking nodes invalid) so we must
		   drop the alloc_sem before sleeping. */

		up(&c->alloc_sem);
		D1(printk(KERN_DEBUG "jffs2_garbage_collect_pass() waiting for ino #%u in state %d\n",
			  ic->ino, ic->state));
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
			goto release_sem;
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
	nlink = ic->nlink;
	spin_unlock(&c->inocache_lock);

	f = jffs2_gc_fetch_inode(c, inum, nlink);
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

 release_sem:
	up(&c->alloc_sem);

 eraseit_lock:
	/* If we've finished this block, start it erasing */
	spin_lock(&c->erase_completion_lock);

 eraseit:
	if (c->gcblock && !c->gcblock->used_size) {
		D1(printk(KERN_DEBUG "Block at 0x%08x completely obsoleted by GC. Moving to erase_pending_list\n", c->gcblock->offset));
		/* We're GC'ing an empty block? */
		list_add_tail(&c->gcblock->list, &c->erase_pending_list);
		c->gcblock = NULL;
		c->nr_erasing_blocks++;
		jffs2_erase_pending_trigger(c);
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

	down(&f->sem);

	/* Now we have the lock for this inode. Check that it's still the one at the head
	   of the list. */

	spin_lock(&c->erase_completion_lock);

	if (c->gcblock != jeb) {
		spin_unlock(&c->erase_completion_lock);
		D1(printk(KERN_DEBUG "GC block is no longer gcblock. Restart\n"));
		goto upnout;
	}
	if (ref_obsolete(raw)) {
		spin_unlock(&c->erase_completion_lock);
		D1(printk(KERN_DEBUG "node to be GC'd was obsoleted in the meantime.\n"));
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
		if((start >> PAGE_CACHE_SHIFT) < ((end-1) >> PAGE_CACHE_SHIFT)) {
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
		printk(KERN_WARNING "Raw node at 0x%08x wasn't in node lists for ino #%u\n",
		       ref_offset(raw), f->inocache->ino);
		if (ref_obsolete(raw)) {
			printk(KERN_WARNING "But it's obsolete so we don't mind too much\n");
		} else {
			jffs2_dbg_dump_node(c, ref_offset(raw));
			BUG();
		}
	}
 upnout:
	up(&f->sem);

	return ret;
}

static int jffs2_garbage_collect_pristine(struct jffs2_sb_info *c,
					  struct jffs2_inode_cache *ic,
					  struct jffs2_raw_node_ref *raw)
{
	union jffs2_node_union *node;
	struct jffs2_raw_node_ref *nraw;
	size_t retlen;
	int ret;
	uint32_t phys_ofs, alloclen;
	uint32_t crc, rawlen;
	int retried = 0;

	D1(printk(KERN_DEBUG "Going to GC REF_PRISTINE node at 0x%08x\n", ref_offset(raw)));

	rawlen = ref_totlen(c, c->gcblock, raw);

	/* Ask for a small amount of space (or the totlen if smaller) because we
	   don't want to force wastage of the end of a block if splitting would
	   work. */
	ret = jffs2_reserve_space_gc(c, min_t(uint32_t, sizeof(struct jffs2_raw_inode) +
				JFFS2_MIN_DATA_LEN, rawlen), &phys_ofs, &alloclen, rawlen);
				/* this is not the exact summary size of it,
					it is only an upper estimation */

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
		printk(KERN_WARNING "Header CRC failed on REF_PRISTINE node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
		       ref_offset(raw), je32_to_cpu(node->u.hdr_crc), crc);
		goto bail;
	}

	switch(je16_to_cpu(node->u.nodetype)) {
	case JFFS2_NODETYPE_INODE:
		crc = crc32(0, node, sizeof(node->i)-8);
		if (je32_to_cpu(node->i.node_crc) != crc) {
			printk(KERN_WARNING "Node CRC failed on REF_PRISTINE data node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			       ref_offset(raw), je32_to_cpu(node->i.node_crc), crc);
			goto bail;
		}

		if (je32_to_cpu(node->i.dsize)) {
			crc = crc32(0, node->i.data, je32_to_cpu(node->i.csize));
			if (je32_to_cpu(node->i.data_crc) != crc) {
				printk(KERN_WARNING "Data CRC failed on REF_PRISTINE data node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
				       ref_offset(raw), je32_to_cpu(node->i.data_crc), crc);
				goto bail;
			}
		}
		break;

	case JFFS2_NODETYPE_DIRENT:
		crc = crc32(0, node, sizeof(node->d)-8);
		if (je32_to_cpu(node->d.node_crc) != crc) {
			printk(KERN_WARNING "Node CRC failed on REF_PRISTINE dirent node at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			       ref_offset(raw), je32_to_cpu(node->d.node_crc), crc);
			goto bail;
		}

		if (node->d.nsize) {
			crc = crc32(0, node->d.name, node->d.nsize);
			if (je32_to_cpu(node->d.name_crc) != crc) {
				printk(KERN_WARNING "Name CRC failed on REF_PRISTINE dirent ode at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
				       ref_offset(raw), je32_to_cpu(node->d.name_crc), crc);
				goto bail;
			}
		}
		break;
	default:
		printk(KERN_WARNING "Unknown node type for REF_PRISTINE node at 0x%08x: 0x%04x\n",
		       ref_offset(raw), je16_to_cpu(node->u.nodetype));
		goto bail;
	}

	nraw = jffs2_alloc_raw_node_ref();
	if (!nraw) {
		ret = -ENOMEM;
		goto out_node;
	}

	/* OK, all the CRCs are good; this node can just be copied as-is. */
 retry:
	nraw->flash_offset = phys_ofs;
	nraw->__totlen = rawlen;
	nraw->next_phys = NULL;

	ret = jffs2_flash_write(c, phys_ofs, rawlen, &retlen, (char *)node);

	if (ret || (retlen != rawlen)) {
		printk(KERN_NOTICE "Write of %d bytes at 0x%08x failed. returned %d, retlen %zd\n",
                       rawlen, phys_ofs, ret, retlen);
		if (retlen) {
                        /* Doesn't belong to any inode */
			nraw->next_in_ino = NULL;

			nraw->flash_offset |= REF_OBSOLETE;
			jffs2_add_physical_node_ref(c, nraw);
			jffs2_mark_node_obsolete(c, nraw);
		} else {
			printk(KERN_NOTICE "Not marking the space at 0x%08x as dirty because the flash driver returned retlen zero\n", nraw->flash_offset);
                        jffs2_free_raw_node_ref(nraw);
		}
		if (!retried && (nraw = jffs2_alloc_raw_node_ref())) {
			/* Try to reallocate space and retry */
			uint32_t dummy;
			struct jffs2_eraseblock *jeb = &c->blocks[phys_ofs / c->sector_size];

			retried = 1;

			D1(printk(KERN_DEBUG "Retrying failed write of REF_PRISTINE node.\n"));

			jffs2_dbg_acct_sanity_check(c,jeb);
			jffs2_dbg_acct_paranoia_check(c, jeb);

			ret = jffs2_reserve_space_gc(c, rawlen, &phys_ofs, &dummy, rawlen);
						/* this is not the exact summary size of it,
							it is only an upper estimation */

			if (!ret) {
				D1(printk(KERN_DEBUG "Allocated space at 0x%08x to retry failed write.\n", phys_ofs));

				jffs2_dbg_acct_sanity_check(c,jeb);
				jffs2_dbg_acct_paranoia_check(c, jeb);

				goto retry;
			}
			D1(printk(KERN_DEBUG "Failed to allocate space to retry failed write: %d!\n", ret));
			jffs2_free_raw_node_ref(nraw);
		}

		jffs2_free_raw_node_ref(nraw);
		if (!ret)
			ret = -EIO;
		goto out_node;
	}
	nraw->flash_offset |= REF_PRISTINE;
	jffs2_add_physical_node_ref(c, nraw);

	/* Link into per-inode list. This is safe because of the ic
	   state being INO_STATE_GC. Note that if we're doing this
	   for an inode which is in-core, the 'nraw' pointer is then
	   going to be fetched from ic->nodes by our caller. */
	spin_lock(&c->erase_completion_lock);
        nraw->next_in_ino = ic->nodes;
        ic->nodes = nraw;
	spin_unlock(&c->erase_completion_lock);

	jffs2_mark_node_obsolete(c, raw);
	D1(printk(KERN_DEBUG "WHEEE! GC REF_PRISTINE node at 0x%08x succeeded\n", ref_offset(raw)));

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
	jint16_t dev;
	char *mdata = NULL, mdatalen = 0;
	uint32_t alloclen, phys_ofs, ilen;
	int ret;

	if (S_ISBLK(JFFS2_F_I_MODE(f)) ||
	    S_ISCHR(JFFS2_F_I_MODE(f)) ) {
		/* For these, we don't actually need to read the old node */
		/* FIXME: for minor or major > 255. */
		dev = cpu_to_je16(((JFFS2_F_I_RDEV_MAJ(f) << 8) |
			JFFS2_F_I_RDEV_MIN(f)));
		mdata = (char *)&dev;
		mdatalen = sizeof(dev);
		D1(printk(KERN_DEBUG "jffs2_garbage_collect_metadata(): Writing %d bytes of kdev_t\n", mdatalen));
	} else if (S_ISLNK(JFFS2_F_I_MODE(f))) {
		mdatalen = fn->size;
		mdata = kmalloc(fn->size, GFP_KERNEL);
		if (!mdata) {
			printk(KERN_WARNING "kmalloc of mdata failed in jffs2_garbage_collect_metadata()\n");
			return -ENOMEM;
		}
		ret = jffs2_read_dnode(c, f, fn, mdata, 0, mdatalen);
		if (ret) {
			printk(KERN_WARNING "read of old metadata failed in jffs2_garbage_collect_metadata(): %d\n", ret);
			kfree(mdata);
			return ret;
		}
		D1(printk(KERN_DEBUG "jffs2_garbage_collect_metadata(): Writing %d bites of symlink target\n", mdatalen));

	}

	ret = jffs2_reserve_space_gc(c, sizeof(ri) + mdatalen, &phys_ofs, &alloclen,
				JFFS2_SUMMARY_INODE_SIZE);
	if (ret) {
		printk(KERN_WARNING "jffs2_reserve_space_gc of %zd bytes for garbage_collect_metadata failed: %d\n",
		       sizeof(ri)+ mdatalen, ret);
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

	new_fn = jffs2_write_dnode(c, f, &ri, mdata, mdatalen, phys_ofs, ALLOC_GC);

	if (IS_ERR(new_fn)) {
		printk(KERN_WARNING "Error writing new dnode: %ld\n", PTR_ERR(new_fn));
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
	uint32_t alloclen, phys_ofs;
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

	ret = jffs2_reserve_space_gc(c, sizeof(rd)+rd.nsize, &phys_ofs, &alloclen,
				JFFS2_SUMMARY_DIRENT_SIZE(rd.nsize));
	if (ret) {
		printk(KERN_WARNING "jffs2_reserve_space_gc of %zd bytes for garbage_collect_dirent failed: %d\n",
		       sizeof(rd)+rd.nsize, ret);
		return ret;
	}
	new_fd = jffs2_write_dirent(c, f, &rd, fd->name, rd.nsize, phys_ofs, ALLOC_GC);

	if (IS_ERR(new_fd)) {
		printk(KERN_WARNING "jffs2_write_dirent in garbage_collect_dirent failed: %ld\n", PTR_ERR(new_fd));
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
		down(&c->erase_free_sem);

		for (raw = f->inocache->nodes; raw != (void *)f->inocache; raw = raw->next_in_ino) {

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

			D1(printk(KERN_DEBUG "Check potential deletion dirent at %08x\n", ref_offset(raw)));

			/* This is an obsolete node belonging to the same directory, and it's of the right
			   length. We need to take a closer look...*/
			ret = jffs2_flash_read(c, ref_offset(raw), rawlen, &retlen, (char *)rd);
			if (ret) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Read error (%d) reading obsolete node at %08x\n", ret, ref_offset(raw));
				/* If we can't read it, we don't need to continue to obsolete it. Continue */
				continue;
			}
			if (retlen != rawlen) {
				printk(KERN_WARNING "jffs2_g_c_deletion_dirent(): Short read (%zd not %u) reading header from obsolete node at %08x\n",
				       retlen, rawlen, ref_offset(raw));
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
			up(&c->erase_free_sem);

			D1(printk(KERN_DEBUG "Deletion dirent at %08x still obsoletes real dirent \"%s\" at %08x for ino #%u\n",
				  ref_offset(fd->raw), fd->name, ref_offset(raw), je32_to_cpu(rd->ino)));
			kfree(rd);

			return jffs2_garbage_collect_dirent(c, jeb, f, fd);
		}

		up(&c->erase_free_sem);
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
		printk(KERN_WARNING "Deletion dirent \"%s\" not found in list for ino #%u\n", fd->name, f->inocache->ino);
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
	uint32_t alloclen, phys_ofs, ilen;
	int ret;

	D1(printk(KERN_DEBUG "Writing replacement hole node for ino #%u from offset 0x%x to 0x%x\n",
		  f->inocache->ino, start, end));

	memset(&ri, 0, sizeof(ri));

	if(fn->frags > 1) {
		size_t readlen;
		uint32_t crc;
		/* It's partially obsoleted by a later write. So we have to
		   write it out again with the _same_ version as before */
		ret = jffs2_flash_read(c, ref_offset(fn->raw), sizeof(ri), &readlen, (char *)&ri);
		if (readlen != sizeof(ri) || ret) {
			printk(KERN_WARNING "Node read failed in jffs2_garbage_collect_hole. Ret %d, retlen %zd. Data will be lost by writing new hole node\n", ret, readlen);
			goto fill;
		}
		if (je16_to_cpu(ri.nodetype) != JFFS2_NODETYPE_INODE) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node at 0x%08x had node type 0x%04x instead of JFFS2_NODETYPE_INODE(0x%04x)\n",
			       ref_offset(fn->raw),
			       je16_to_cpu(ri.nodetype), JFFS2_NODETYPE_INODE);
			return -EIO;
		}
		if (je32_to_cpu(ri.totlen) != sizeof(ri)) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node at 0x%08x had totlen 0x%x instead of expected 0x%zx\n",
			       ref_offset(fn->raw),
			       je32_to_cpu(ri.totlen), sizeof(ri));
			return -EIO;
		}
		crc = crc32(0, &ri, sizeof(ri)-8);
		if (crc != je32_to_cpu(ri.node_crc)) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node at 0x%08x had CRC 0x%08x which doesn't match calculated CRC 0x%08x\n",
			       ref_offset(fn->raw),
			       je32_to_cpu(ri.node_crc), crc);
			/* FIXME: We could possibly deal with this by writing new holes for each frag */
			printk(KERN_WARNING "Data in the range 0x%08x to 0x%08x of inode #%u will be lost\n",
			       start, end, f->inocache->ino);
			goto fill;
		}
		if (ri.compr != JFFS2_COMPR_ZERO) {
			printk(KERN_WARNING "jffs2_garbage_collect_hole: Node 0x%08x wasn't a hole node!\n", ref_offset(fn->raw));
			printk(KERN_WARNING "Data in the range 0x%08x to 0x%08x of inode #%u will be lost\n",
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

	ret = jffs2_reserve_space_gc(c, sizeof(ri), &phys_ofs, &alloclen,
				JFFS2_SUMMARY_INODE_SIZE);
	if (ret) {
		printk(KERN_WARNING "jffs2_reserve_space_gc of %zd bytes for garbage_collect_hole failed: %d\n",
		       sizeof(ri), ret);
		return ret;
	}
	new_fn = jffs2_write_dnode(c, f, &ri, NULL, 0, phys_ofs, ALLOC_GC);

	if (IS_ERR(new_fn)) {
		printk(KERN_WARNING "Error writing new hole node: %ld\n", PTR_ERR(new_fn));
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
		printk(KERN_WARNING "jffs2_garbage_collect_hole: Replacing fn with %d frag(s) but new ver %d != highest_version %d of ino #%d\n",
		       fn->frags, je32_to_cpu(ri.version), f->highest_version,
		       je32_to_cpu(ri.ino));
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
		printk(KERN_WARNING "jffs2_garbage_collect_hole: Old node still has frags!\n");
		BUG();
	}
	if (!new_fn->frags) {
		printk(KERN_WARNING "jffs2_garbage_collect_hole: New node has no frags!\n");
		BUG();
	}

	jffs2_mark_node_obsolete(c, fn->raw);
	jffs2_free_full_dnode(fn);

	return 0;
}

static int jffs2_garbage_collect_dnode(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				       struct jffs2_inode_info *f, struct jffs2_full_dnode *fn,
				       uint32_t start, uint32_t end)
{
	struct jffs2_full_dnode *new_fn;
	struct jffs2_raw_inode ri;
	uint32_t alloclen, phys_ofs, offset, orig_end, orig_start;
	int ret = 0;
	unsigned char *comprbuf = NULL, *writebuf;
	unsigned long pg;
	unsigned char *pg_ptr;

	memset(&ri, 0, sizeof(ri));

	D1(printk(KERN_DEBUG "Writing replacement dnode for ino #%u from offset 0x%x to 0x%x\n",
		  f->inocache->ino, start, end));

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

		min = start & ~(PAGE_CACHE_SIZE-1);
		max = min + PAGE_CACHE_SIZE;

		frag = jffs2_lookup_node_frag(&f->fragtree, start);

		/* BUG_ON(!frag) but that'll happen anyway... */

		BUG_ON(frag->ofs != start);

		/* First grow down... */
		while((frag = frag_prev(frag)) && frag->ofs >= min) {

			/* If the previous frag doesn't even reach the beginning, there's
			   excessive fragmentation. Just merge. */
			if (frag->ofs > min) {
				D1(printk(KERN_DEBUG "Expanding down to cover partial frag (0x%x-0x%x)\n",
					  frag->ofs, frag->ofs+frag->size));
				start = frag->ofs;
				continue;
			}
			/* OK. This frag holds the first byte of the page. */
			if (!frag->node || !frag->node->raw) {
				D1(printk(KERN_DEBUG "First frag in page is hole (0x%x-0x%x). Not expanding down.\n",
					  frag->ofs, frag->ofs+frag->size));
				break;
			} else {

				/* OK, it's a frag which extends to the beginning of the page. Does it live
				   in a block which is still considered clean? If so, don't obsolete it.
				   If not, cover it anyway. */

				struct jffs2_raw_node_ref *raw = frag->node->raw;
				struct jffs2_eraseblock *jeb;

				jeb = &c->blocks[raw->flash_offset / c->sector_size];

				if (jeb == c->gcblock) {
					D1(printk(KERN_DEBUG "Expanding down to cover frag (0x%x-0x%x) in gcblock at %08x\n",
						  frag->ofs, frag->ofs+frag->size, ref_offset(raw)));
					start = frag->ofs;
					break;
				}
				if (!ISDIRTY(jeb->dirty_size + jeb->wasted_size)) {
					D1(printk(KERN_DEBUG "Not expanding down to cover frag (0x%x-0x%x) in clean block %08x\n",
						  frag->ofs, frag->ofs+frag->size, jeb->offset));
					break;
				}

				D1(printk(KERN_DEBUG "Expanding down to cover frag (0x%x-0x%x) in dirty block %08x\n",
						  frag->ofs, frag->ofs+frag->size, jeb->offset));
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
				D1(printk(KERN_DEBUG "Expanding up to cover partial frag (0x%x-0x%x)\n",
					  frag->ofs, frag->ofs+frag->size));
				end = frag->ofs + frag->size;
				continue;
			}

			if (!frag->node || !frag->node->raw) {
				D1(printk(KERN_DEBUG "Last frag in page is hole (0x%x-0x%x). Not expanding up.\n",
					  frag->ofs, frag->ofs+frag->size));
				break;
			} else {

				/* OK, it's a frag which extends to the beginning of the page. Does it live
				   in a block which is still considered clean? If so, don't obsolete it.
				   If not, cover it anyway. */

				struct jffs2_raw_node_ref *raw = frag->node->raw;
				struct jffs2_eraseblock *jeb;

				jeb = &c->blocks[raw->flash_offset / c->sector_size];

				if (jeb == c->gcblock) {
					D1(printk(KERN_DEBUG "Expanding up to cover frag (0x%x-0x%x) in gcblock at %08x\n",
						  frag->ofs, frag->ofs+frag->size, ref_offset(raw)));
					end = frag->ofs + frag->size;
					break;
				}
				if (!ISDIRTY(jeb->dirty_size + jeb->wasted_size)) {
					D1(printk(KERN_DEBUG "Not expanding up to cover frag (0x%x-0x%x) in clean block %08x\n",
						  frag->ofs, frag->ofs+frag->size, jeb->offset));
					break;
				}

				D1(printk(KERN_DEBUG "Expanding up to cover frag (0x%x-0x%x) in dirty block %08x\n",
						  frag->ofs, frag->ofs+frag->size, jeb->offset));
				end = frag->ofs + frag->size;
				break;
			}
		}
		D1(printk(KERN_DEBUG "Expanded dnode to write from (0x%x-0x%x) to (0x%x-0x%x)\n",
			  orig_start, orig_end, start, end));

		D1(BUG_ON(end > frag_last(&f->fragtree)->ofs + frag_last(&f->fragtree)->size));
		BUG_ON(end < orig_end);
		BUG_ON(start > orig_start);
	}

	/* First, use readpage() to read the appropriate page into the page cache */
	/* Q: What happens if we actually try to GC the _same_ page for which commit_write()
	 *    triggered garbage collection in the first place?
	 * A: I _think_ it's OK. read_cache_page shouldn't deadlock, we'll write out the
	 *    page OK. We'll actually write it out again in commit_write, which is a little
	 *    suboptimal, but at least we're correct.
	 */
	pg_ptr = jffs2_gc_fetch_page(c, f, start, &pg);

	if (IS_ERR(pg_ptr)) {
		printk(KERN_WARNING "read_cache_page() returned error: %ld\n", PTR_ERR(pg_ptr));
		return PTR_ERR(pg_ptr);
	}

	offset = start;
	while(offset < orig_end) {
		uint32_t datalen;
		uint32_t cdatalen;
		uint16_t comprtype = JFFS2_COMPR_NONE;

		ret = jffs2_reserve_space_gc(c, sizeof(ri) + JFFS2_MIN_DATA_LEN, &phys_ofs,
					&alloclen, JFFS2_SUMMARY_INODE_SIZE);

		if (ret) {
			printk(KERN_WARNING "jffs2_reserve_space_gc of %zd bytes for garbage_collect_dnode failed: %d\n",
			       sizeof(ri)+ JFFS2_MIN_DATA_LEN, ret);
			break;
		}
		cdatalen = min_t(uint32_t, alloclen - sizeof(ri), end - offset);
		datalen = end - offset;

		writebuf = pg_ptr + (offset & (PAGE_CACHE_SIZE -1));

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

		new_fn = jffs2_write_dnode(c, f, &ri, comprbuf, cdatalen, phys_ofs, ALLOC_GC);

		jffs2_free_comprbuf(comprbuf, writebuf);

		if (IS_ERR(new_fn)) {
			printk(KERN_WARNING "Error writing new dnode: %ld\n", PTR_ERR(new_fn));
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

	jffs2_gc_release_page(c, pg_ptr, &pg);
	return ret;
}
