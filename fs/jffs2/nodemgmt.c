/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include <linux/sched.h> /* For cond_resched() */
#include "nodelist.h"
#include "debug.h"

/**
 *	jffs2_reserve_space - request physical space to write nodes to flash
 *	@c: superblock info
 *	@minsize: Minimum acceptable size of allocation
 *	@len: Returned value of allocation length
 *	@prio: Allocation type - ALLOC_{NORMAL,DELETION}
 *
 *	Requests a block of physical space on the flash. Returns zero for success
 *	and puts 'len' into the appropriate place, or returns -ENOSPC or other 
 *	error if appropriate. Doesn't return len since that's 
 *
 *	If it returns zero, jffs2_reserve_space() also downs the per-filesystem
 *	allocation semaphore, to prevent more than one allocation from being
 *	active at any time. The semaphore is later released by jffs2_commit_allocation()
 *
 *	jffs2_reserve_space() may trigger garbage collection in order to make room
 *	for the requested allocation.
 */

static int jffs2_do_reserve_space(struct jffs2_sb_info *c,  uint32_t minsize,
				  uint32_t *len, uint32_t sumsize);

int jffs2_reserve_space(struct jffs2_sb_info *c, uint32_t minsize,
			uint32_t *len, int prio, uint32_t sumsize)
{
	int ret = -EAGAIN;
	int blocksneeded = c->resv_blocks_write;
	/* align it */
	minsize = PAD(minsize);

	D1(printk(KERN_DEBUG "jffs2_reserve_space(): Requested 0x%x bytes\n", minsize));
	mutex_lock(&c->alloc_sem);

	D1(printk(KERN_DEBUG "jffs2_reserve_space(): alloc sem got\n"));

	spin_lock(&c->erase_completion_lock);

	/* this needs a little more thought (true <tglx> :)) */
	while(ret == -EAGAIN) {
		while(c->nr_free_blocks + c->nr_erasing_blocks < blocksneeded) {
			uint32_t dirty, avail;

			/* calculate real dirty size
			 * dirty_size contains blocks on erase_pending_list
			 * those blocks are counted in c->nr_erasing_blocks.
			 * If one block is actually erased, it is not longer counted as dirty_space
			 * but it is counted in c->nr_erasing_blocks, so we add it and subtract it
			 * with c->nr_erasing_blocks * c->sector_size again.
			 * Blocks on erasable_list are counted as dirty_size, but not in c->nr_erasing_blocks
			 * This helps us to force gc and pick eventually a clean block to spread the load.
			 * We add unchecked_size here, as we hopefully will find some space to use.
			 * This will affect the sum only once, as gc first finishes checking
			 * of nodes.
			 */
			dirty = c->dirty_size + c->erasing_size - c->nr_erasing_blocks * c->sector_size + c->unchecked_size;
			if (dirty < c->nospc_dirty_size) {
				if (prio == ALLOC_DELETION && c->nr_free_blocks + c->nr_erasing_blocks >= c->resv_blocks_deletion) {
					D1(printk(KERN_NOTICE "jffs2_reserve_space(): Low on dirty space to GC, but it's a deletion. Allowing...\n"));
					break;
				}
				D1(printk(KERN_DEBUG "dirty size 0x%08x + unchecked_size 0x%08x < nospc_dirty_size 0x%08x, returning -ENOSPC\n",
					  dirty, c->unchecked_size, c->sector_size));

				spin_unlock(&c->erase_completion_lock);
				mutex_unlock(&c->alloc_sem);
				return -ENOSPC;
			}

			/* Calc possibly available space. Possibly available means that we
			 * don't know, if unchecked size contains obsoleted nodes, which could give us some
			 * more usable space. This will affect the sum only once, as gc first finishes checking
			 * of nodes.
			 + Return -ENOSPC, if the maximum possibly available space is less or equal than
			 * blocksneeded * sector_size.
			 * This blocks endless gc looping on a filesystem, which is nearly full, even if
			 * the check above passes.
			 */
			avail = c->free_size + c->dirty_size + c->erasing_size + c->unchecked_size;
			if ( (avail / c->sector_size) <= blocksneeded) {
				if (prio == ALLOC_DELETION && c->nr_free_blocks + c->nr_erasing_blocks >= c->resv_blocks_deletion) {
					D1(printk(KERN_NOTICE "jffs2_reserve_space(): Low on possibly available space, but it's a deletion. Allowing...\n"));
					break;
				}

				D1(printk(KERN_DEBUG "max. available size 0x%08x  < blocksneeded * sector_size 0x%08x, returning -ENOSPC\n",
					  avail, blocksneeded * c->sector_size));
				spin_unlock(&c->erase_completion_lock);
				mutex_unlock(&c->alloc_sem);
				return -ENOSPC;
			}

			mutex_unlock(&c->alloc_sem);

			D1(printk(KERN_DEBUG "Triggering GC pass. nr_free_blocks %d, nr_erasing_blocks %d, free_size 0x%08x, dirty_size 0x%08x, wasted_size 0x%08x, used_size 0x%08x, erasing_size 0x%08x, bad_size 0x%08x (total 0x%08x of 0x%08x)\n",
				  c->nr_free_blocks, c->nr_erasing_blocks, c->free_size, c->dirty_size, c->wasted_size, c->used_size, c->erasing_size, c->bad_size,
				  c->free_size + c->dirty_size + c->wasted_size + c->used_size + c->erasing_size + c->bad_size, c->flash_size));
			spin_unlock(&c->erase_completion_lock);

			ret = jffs2_garbage_collect_pass(c);

			if (ret == -EAGAIN)
				jffs2_erase_pending_blocks(c, 1);
			else if (ret)
				return ret;

			cond_resched();

			if (signal_pending(current))
				return -EINTR;

			mutex_lock(&c->alloc_sem);
			spin_lock(&c->erase_completion_lock);
		}

		ret = jffs2_do_reserve_space(c, minsize, len, sumsize);
		if (ret) {
			D1(printk(KERN_DEBUG "jffs2_reserve_space: ret is %d\n", ret));
		}
	}
	spin_unlock(&c->erase_completion_lock);
	if (!ret)
		ret = jffs2_prealloc_raw_node_refs(c, c->nextblock, 1);
	if (ret)
		mutex_unlock(&c->alloc_sem);
	return ret;
}

int jffs2_reserve_space_gc(struct jffs2_sb_info *c, uint32_t minsize,
			   uint32_t *len, uint32_t sumsize)
{
	int ret = -EAGAIN;
	minsize = PAD(minsize);

	D1(printk(KERN_DEBUG "jffs2_reserve_space_gc(): Requested 0x%x bytes\n", minsize));

	spin_lock(&c->erase_completion_lock);
	while(ret == -EAGAIN) {
		ret = jffs2_do_reserve_space(c, minsize, len, sumsize);
		if (ret) {
			D1(printk(KERN_DEBUG "jffs2_reserve_space_gc: looping, ret is %d\n", ret));
		}
	}
	spin_unlock(&c->erase_completion_lock);
	if (!ret)
		ret = jffs2_prealloc_raw_node_refs(c, c->nextblock, 1);

	return ret;
}


/* Classify nextblock (clean, dirty of verydirty) and force to select an other one */

static void jffs2_close_nextblock(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{

	if (c->nextblock == NULL) {
		D1(printk(KERN_DEBUG "jffs2_close_nextblock: Erase block at 0x%08x has already been placed in a list\n",
		  jeb->offset));
		return;
	}
	/* Check, if we have a dirty block now, or if it was dirty already */
	if (ISDIRTY (jeb->wasted_size + jeb->dirty_size)) {
		c->dirty_size += jeb->wasted_size;
		c->wasted_size -= jeb->wasted_size;
		jeb->dirty_size += jeb->wasted_size;
		jeb->wasted_size = 0;
		if (VERYDIRTY(c, jeb->dirty_size)) {
			D1(printk(KERN_DEBUG "Adding full erase block at 0x%08x to very_dirty_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size));
			list_add_tail(&jeb->list, &c->very_dirty_list);
		} else {
			D1(printk(KERN_DEBUG "Adding full erase block at 0x%08x to dirty_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size));
			list_add_tail(&jeb->list, &c->dirty_list);
		}
	} else {
		D1(printk(KERN_DEBUG "Adding full erase block at 0x%08x to clean_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
		  jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size));
		list_add_tail(&jeb->list, &c->clean_list);
	}
	c->nextblock = NULL;

}

/* Select a new jeb for nextblock */

static int jffs2_find_nextblock(struct jffs2_sb_info *c)
{
	struct list_head *next;

	/* Take the next block off the 'free' list */

	if (list_empty(&c->free_list)) {

		if (!c->nr_erasing_blocks &&
			!list_empty(&c->erasable_list)) {
			struct jffs2_eraseblock *ejeb;

			ejeb = list_entry(c->erasable_list.next, struct jffs2_eraseblock, list);
			list_move_tail(&ejeb->list, &c->erase_pending_list);
			c->nr_erasing_blocks++;
			jffs2_erase_pending_trigger(c);
			D1(printk(KERN_DEBUG "jffs2_find_nextblock: Triggering erase of erasable block at 0x%08x\n",
				  ejeb->offset));
		}

		if (!c->nr_erasing_blocks &&
			!list_empty(&c->erasable_pending_wbuf_list)) {
			D1(printk(KERN_DEBUG "jffs2_find_nextblock: Flushing write buffer\n"));
			/* c->nextblock is NULL, no update to c->nextblock allowed */
			spin_unlock(&c->erase_completion_lock);
			jffs2_flush_wbuf_pad(c);
			spin_lock(&c->erase_completion_lock);
			/* Have another go. It'll be on the erasable_list now */
			return -EAGAIN;
		}

		if (!c->nr_erasing_blocks) {
			/* Ouch. We're in GC, or we wouldn't have got here.
			   And there's no space left. At all. */
			printk(KERN_CRIT "Argh. No free space left for GC. nr_erasing_blocks is %d. nr_free_blocks is %d. (erasableempty: %s, erasingempty: %s, erasependingempty: %s)\n",
				   c->nr_erasing_blocks, c->nr_free_blocks, list_empty(&c->erasable_list)?"yes":"no",
				   list_empty(&c->erasing_list)?"yes":"no", list_empty(&c->erase_pending_list)?"yes":"no");
			return -ENOSPC;
		}

		spin_unlock(&c->erase_completion_lock);
		/* Don't wait for it; just erase one right now */
		jffs2_erase_pending_blocks(c, 1);
		spin_lock(&c->erase_completion_lock);

		/* An erase may have failed, decreasing the
		   amount of free space available. So we must
		   restart from the beginning */
		return -EAGAIN;
	}

	next = c->free_list.next;
	list_del(next);
	c->nextblock = list_entry(next, struct jffs2_eraseblock, list);
	c->nr_free_blocks--;

	jffs2_sum_reset_collected(c->summary); /* reset collected summary */

	D1(printk(KERN_DEBUG "jffs2_find_nextblock(): new nextblock = 0x%08x\n", c->nextblock->offset));

	return 0;
}

/* Called with alloc sem _and_ erase_completion_lock */
static int jffs2_do_reserve_space(struct jffs2_sb_info *c, uint32_t minsize,
				  uint32_t *len, uint32_t sumsize)
{
	struct jffs2_eraseblock *jeb = c->nextblock;
	uint32_t reserved_size;				/* for summary information at the end of the jeb */
	int ret;

 restart:
	reserved_size = 0;

	if (jffs2_sum_active() && (sumsize != JFFS2_SUMMARY_NOSUM_SIZE)) {
							/* NOSUM_SIZE means not to generate summary */

		if (jeb) {
			reserved_size = PAD(sumsize + c->summary->sum_size + JFFS2_SUMMARY_FRAME_SIZE);
			dbg_summary("minsize=%d , jeb->free=%d ,"
						"summary->size=%d , sumsize=%d\n",
						minsize, jeb->free_size,
						c->summary->sum_size, sumsize);
		}

		/* Is there enough space for writing out the current node, or we have to
		   write out summary information now, close this jeb and select new nextblock? */
		if (jeb && (PAD(minsize) + PAD(c->summary->sum_size + sumsize +
					JFFS2_SUMMARY_FRAME_SIZE) > jeb->free_size)) {

			/* Has summary been disabled for this jeb? */
			if (jffs2_sum_is_disabled(c->summary)) {
				sumsize = JFFS2_SUMMARY_NOSUM_SIZE;
				goto restart;
			}

			/* Writing out the collected summary information */
			dbg_summary("generating summary for 0x%08x.\n", jeb->offset);
			ret = jffs2_sum_write_sumnode(c);

			if (ret)
				return ret;

			if (jffs2_sum_is_disabled(c->summary)) {
				/* jffs2_write_sumnode() couldn't write out the summary information
				   diabling summary for this jeb and free the collected information
				 */
				sumsize = JFFS2_SUMMARY_NOSUM_SIZE;
				goto restart;
			}

			jffs2_close_nextblock(c, jeb);
			jeb = NULL;
			/* keep always valid value in reserved_size */
			reserved_size = PAD(sumsize + c->summary->sum_size + JFFS2_SUMMARY_FRAME_SIZE);
		}
	} else {
		if (jeb && minsize > jeb->free_size) {
			uint32_t waste;

			/* Skip the end of this block and file it as having some dirty space */
			/* If there's a pending write to it, flush now */

			if (jffs2_wbuf_dirty(c)) {
				spin_unlock(&c->erase_completion_lock);
				D1(printk(KERN_DEBUG "jffs2_do_reserve_space: Flushing write buffer\n"));
				jffs2_flush_wbuf_pad(c);
				spin_lock(&c->erase_completion_lock);
				jeb = c->nextblock;
				goto restart;
			}

			spin_unlock(&c->erase_completion_lock);

			ret = jffs2_prealloc_raw_node_refs(c, jeb, 1);
			if (ret)
				return ret;
			/* Just lock it again and continue. Nothing much can change because
			   we hold c->alloc_sem anyway. In fact, it's not entirely clear why
			   we hold c->erase_completion_lock in the majority of this function...
			   but that's a question for another (more caffeine-rich) day. */
			spin_lock(&c->erase_completion_lock);

			waste = jeb->free_size;
			jffs2_link_node_ref(c, jeb,
					    (jeb->offset + c->sector_size - waste) | REF_OBSOLETE,
					    waste, NULL);
			/* FIXME: that made it count as dirty. Convert to wasted */
			jeb->dirty_size -= waste;
			c->dirty_size -= waste;
			jeb->wasted_size += waste;
			c->wasted_size += waste;

			jffs2_close_nextblock(c, jeb);
			jeb = NULL;
		}
	}

	if (!jeb) {

		ret = jffs2_find_nextblock(c);
		if (ret)
			return ret;

		jeb = c->nextblock;

		if (jeb->free_size != c->sector_size - c->cleanmarker_size) {
			printk(KERN_WARNING "Eep. Block 0x%08x taken from free_list had free_size of 0x%08x!!\n", jeb->offset, jeb->free_size);
			goto restart;
		}
	}
	/* OK, jeb (==c->nextblock) is now pointing at a block which definitely has
	   enough space */
	*len = jeb->free_size - reserved_size;

	if (c->cleanmarker_size && jeb->used_size == c->cleanmarker_size &&
	    !jeb->first_node->next_in_ino) {
		/* Only node in it beforehand was a CLEANMARKER node (we think).
		   So mark it obsolete now that there's going to be another node
		   in the block. This will reduce used_size to zero but We've
		   already set c->nextblock so that jffs2_mark_node_obsolete()
		   won't try to refile it to the dirty_list.
		*/
		spin_unlock(&c->erase_completion_lock);
		jffs2_mark_node_obsolete(c, jeb->first_node);
		spin_lock(&c->erase_completion_lock);
	}

	D1(printk(KERN_DEBUG "jffs2_do_reserve_space(): Giving 0x%x bytes at 0x%x\n",
		  *len, jeb->offset + (c->sector_size - jeb->free_size)));
	return 0;
}

/**
 *	jffs2_add_physical_node_ref - add a physical node reference to the list
 *	@c: superblock info
 *	@new: new node reference to add
 *	@len: length of this physical node
 *
 *	Should only be used to report nodes for which space has been allocated
 *	by jffs2_reserve_space.
 *
 *	Must be called with the alloc_sem held.
 */

struct jffs2_raw_node_ref *jffs2_add_physical_node_ref(struct jffs2_sb_info *c,
						       uint32_t ofs, uint32_t len,
						       struct jffs2_inode_cache *ic)
{
	struct jffs2_eraseblock *jeb;
	struct jffs2_raw_node_ref *new;

	jeb = &c->blocks[ofs / c->sector_size];

	D1(printk(KERN_DEBUG "jffs2_add_physical_node_ref(): Node at 0x%x(%d), size 0x%x\n",
		  ofs & ~3, ofs & 3, len));
#if 1
	/* Allow non-obsolete nodes only to be added at the end of c->nextblock, 
	   if c->nextblock is set. Note that wbuf.c will file obsolete nodes
	   even after refiling c->nextblock */
	if ((c->nextblock || ((ofs & 3) != REF_OBSOLETE))
	    && (jeb != c->nextblock || (ofs & ~3) != jeb->offset + (c->sector_size - jeb->free_size))) {
		printk(KERN_WARNING "argh. node added in wrong place at 0x%08x(%d)\n", ofs & ~3, ofs & 3);
		if (c->nextblock)
			printk(KERN_WARNING "nextblock 0x%08x", c->nextblock->offset);
		else
			printk(KERN_WARNING "No nextblock");
		printk(", expected at %08x\n", jeb->offset + (c->sector_size - jeb->free_size));
		return ERR_PTR(-EINVAL);
	}
#endif
	spin_lock(&c->erase_completion_lock);

	new = jffs2_link_node_ref(c, jeb, ofs, len, ic);

	if (!jeb->free_size && !jeb->dirty_size && !ISDIRTY(jeb->wasted_size)) {
		/* If it lives on the dirty_list, jffs2_reserve_space will put it there */
		D1(printk(KERN_DEBUG "Adding full erase block at 0x%08x to clean_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size));
		if (jffs2_wbuf_dirty(c)) {
			/* Flush the last write in the block if it's outstanding */
			spin_unlock(&c->erase_completion_lock);
			jffs2_flush_wbuf_pad(c);
			spin_lock(&c->erase_completion_lock);
		}

		list_add_tail(&jeb->list, &c->clean_list);
		c->nextblock = NULL;
	}
	jffs2_dbg_acct_sanity_check_nolock(c,jeb);
	jffs2_dbg_acct_paranoia_check_nolock(c, jeb);

	spin_unlock(&c->erase_completion_lock);

	return new;
}


void jffs2_complete_reservation(struct jffs2_sb_info *c)
{
	D1(printk(KERN_DEBUG "jffs2_complete_reservation()\n"));
	jffs2_garbage_collect_trigger(c);
	mutex_unlock(&c->alloc_sem);
}

static inline int on_list(struct list_head *obj, struct list_head *head)
{
	struct list_head *this;

	list_for_each(this, head) {
		if (this == obj) {
			D1(printk("%p is on list at %p\n", obj, head));
			return 1;

		}
	}
	return 0;
}

void jffs2_mark_node_obsolete(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *ref)
{
	struct jffs2_eraseblock *jeb;
	int blocknr;
	struct jffs2_unknown_node n;
	int ret, addedsize;
	size_t retlen;
	uint32_t freed_len;

	if(unlikely(!ref)) {
		printk(KERN_NOTICE "EEEEEK. jffs2_mark_node_obsolete called with NULL node\n");
		return;
	}
	if (ref_obsolete(ref)) {
		D1(printk(KERN_DEBUG "jffs2_mark_node_obsolete called with already obsolete node at 0x%08x\n", ref_offset(ref)));
		return;
	}
	blocknr = ref->flash_offset / c->sector_size;
	if (blocknr >= c->nr_blocks) {
		printk(KERN_NOTICE "raw node at 0x%08x is off the end of device!\n", ref->flash_offset);
		BUG();
	}
	jeb = &c->blocks[blocknr];

	if (jffs2_can_mark_obsolete(c) && !jffs2_is_readonly(c) &&
	    !(c->flags & (JFFS2_SB_FLAG_SCANNING | JFFS2_SB_FLAG_BUILDING))) {
		/* Hm. This may confuse static lock analysis. If any of the above
		   three conditions is false, we're going to return from this
		   function without actually obliterating any nodes or freeing
		   any jffs2_raw_node_refs. So we don't need to stop erases from
		   happening, or protect against people holding an obsolete
		   jffs2_raw_node_ref without the erase_completion_lock. */
		mutex_lock(&c->erase_free_sem);
	}

	spin_lock(&c->erase_completion_lock);

	freed_len = ref_totlen(c, jeb, ref);

	if (ref_flags(ref) == REF_UNCHECKED) {
		D1(if (unlikely(jeb->unchecked_size < freed_len)) {
			printk(KERN_NOTICE "raw unchecked node of size 0x%08x freed from erase block %d at 0x%08x, but unchecked_size was already 0x%08x\n",
			       freed_len, blocknr, ref->flash_offset, jeb->used_size);
			BUG();
		})
		D1(printk(KERN_DEBUG "Obsoleting previously unchecked node at 0x%08x of len %x: ", ref_offset(ref), freed_len));
		jeb->unchecked_size -= freed_len;
		c->unchecked_size -= freed_len;
	} else {
		D1(if (unlikely(jeb->used_size < freed_len)) {
			printk(KERN_NOTICE "raw node of size 0x%08x freed from erase block %d at 0x%08x, but used_size was already 0x%08x\n",
			       freed_len, blocknr, ref->flash_offset, jeb->used_size);
			BUG();
		})
		D1(printk(KERN_DEBUG "Obsoleting node at 0x%08x of len %#x: ", ref_offset(ref), freed_len));
		jeb->used_size -= freed_len;
		c->used_size -= freed_len;
	}

	// Take care, that wasted size is taken into concern
	if ((jeb->dirty_size || ISDIRTY(jeb->wasted_size + freed_len)) && jeb != c->nextblock) {
		D1(printk("Dirtying\n"));
		addedsize = freed_len;
		jeb->dirty_size += freed_len;
		c->dirty_size += freed_len;

		/* Convert wasted space to dirty, if not a bad block */
		if (jeb->wasted_size) {
			if (on_list(&jeb->list, &c->bad_used_list)) {
				D1(printk(KERN_DEBUG "Leaving block at %08x on the bad_used_list\n",
					  jeb->offset));
				addedsize = 0; /* To fool the refiling code later */
			} else {
				D1(printk(KERN_DEBUG "Converting %d bytes of wasted space to dirty in block at %08x\n",
					  jeb->wasted_size, jeb->offset));
				addedsize += jeb->wasted_size;
				jeb->dirty_size += jeb->wasted_size;
				c->dirty_size += jeb->wasted_size;
				c->wasted_size -= jeb->wasted_size;
				jeb->wasted_size = 0;
			}
		}
	} else {
		D1(printk("Wasting\n"));
		addedsize = 0;
		jeb->wasted_size += freed_len;
		c->wasted_size += freed_len;
	}
	ref->flash_offset = ref_offset(ref) | REF_OBSOLETE;

	jffs2_dbg_acct_sanity_check_nolock(c, jeb);
	jffs2_dbg_acct_paranoia_check_nolock(c, jeb);

	if (c->flags & JFFS2_SB_FLAG_SCANNING) {
		/* Flash scanning is in progress. Don't muck about with the block
		   lists because they're not ready yet, and don't actually
		   obliterate nodes that look obsolete. If they weren't
		   marked obsolete on the flash at the time they _became_
		   obsolete, there was probably a reason for that. */
		spin_unlock(&c->erase_completion_lock);
		/* We didn't lock the erase_free_sem */
		return;
	}

	if (jeb == c->nextblock) {
		D2(printk(KERN_DEBUG "Not moving nextblock 0x%08x to dirty/erase_pending list\n", jeb->offset));
	} else if (!jeb->used_size && !jeb->unchecked_size) {
		if (jeb == c->gcblock) {
			D1(printk(KERN_DEBUG "gcblock at 0x%08x completely dirtied. Clearing gcblock...\n", jeb->offset));
			c->gcblock = NULL;
		} else {
			D1(printk(KERN_DEBUG "Eraseblock at 0x%08x completely dirtied. Removing from (dirty?) list...\n", jeb->offset));
			list_del(&jeb->list);
		}
		if (jffs2_wbuf_dirty(c)) {
			D1(printk(KERN_DEBUG "...and adding to erasable_pending_wbuf_list\n"));
			list_add_tail(&jeb->list, &c->erasable_pending_wbuf_list);
		} else {
			if (jiffies & 127) {
				/* Most of the time, we just erase it immediately. Otherwise we
				   spend ages scanning it on mount, etc. */
				D1(printk(KERN_DEBUG "...and adding to erase_pending_list\n"));
				list_add_tail(&jeb->list, &c->erase_pending_list);
				c->nr_erasing_blocks++;
				jffs2_erase_pending_trigger(c);
			} else {
				/* Sometimes, however, we leave it elsewhere so it doesn't get
				   immediately reused, and we spread the load a bit. */
				D1(printk(KERN_DEBUG "...and adding to erasable_list\n"));
				list_add_tail(&jeb->list, &c->erasable_list);
			}
		}
		D1(printk(KERN_DEBUG "Done OK\n"));
	} else if (jeb == c->gcblock) {
		D2(printk(KERN_DEBUG "Not moving gcblock 0x%08x to dirty_list\n", jeb->offset));
	} else if (ISDIRTY(jeb->dirty_size) && !ISDIRTY(jeb->dirty_size - addedsize)) {
		D1(printk(KERN_DEBUG "Eraseblock at 0x%08x is freshly dirtied. Removing from clean list...\n", jeb->offset));
		list_del(&jeb->list);
		D1(printk(KERN_DEBUG "...and adding to dirty_list\n"));
		list_add_tail(&jeb->list, &c->dirty_list);
	} else if (VERYDIRTY(c, jeb->dirty_size) &&
		   !VERYDIRTY(c, jeb->dirty_size - addedsize)) {
		D1(printk(KERN_DEBUG "Eraseblock at 0x%08x is now very dirty. Removing from dirty list...\n", jeb->offset));
		list_del(&jeb->list);
		D1(printk(KERN_DEBUG "...and adding to very_dirty_list\n"));
		list_add_tail(&jeb->list, &c->very_dirty_list);
	} else {
		D1(printk(KERN_DEBUG "Eraseblock at 0x%08x not moved anywhere. (free 0x%08x, dirty 0x%08x, used 0x%08x)\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size));
	}

	spin_unlock(&c->erase_completion_lock);

	if (!jffs2_can_mark_obsolete(c) || jffs2_is_readonly(c) ||
		(c->flags & JFFS2_SB_FLAG_BUILDING)) {
		/* We didn't lock the erase_free_sem */
		return;
	}

	/* The erase_free_sem is locked, and has been since before we marked the node obsolete
	   and potentially put its eraseblock onto the erase_pending_list. Thus, we know that
	   the block hasn't _already_ been erased, and that 'ref' itself hasn't been freed yet
	   by jffs2_free_jeb_node_refs() in erase.c. Which is nice. */

	D1(printk(KERN_DEBUG "obliterating obsoleted node at 0x%08x\n", ref_offset(ref)));
	ret = jffs2_flash_read(c, ref_offset(ref), sizeof(n), &retlen, (char *)&n);
	if (ret) {
		printk(KERN_WARNING "Read error reading from obsoleted node at 0x%08x: %d\n", ref_offset(ref), ret);
		goto out_erase_sem;
	}
	if (retlen != sizeof(n)) {
		printk(KERN_WARNING "Short read from obsoleted node at 0x%08x: %zd\n", ref_offset(ref), retlen);
		goto out_erase_sem;
	}
	if (PAD(je32_to_cpu(n.totlen)) != PAD(freed_len)) {
		printk(KERN_WARNING "Node totlen on flash (0x%08x) != totlen from node ref (0x%08x)\n", je32_to_cpu(n.totlen), freed_len);
		goto out_erase_sem;
	}
	if (!(je16_to_cpu(n.nodetype) & JFFS2_NODE_ACCURATE)) {
		D1(printk(KERN_DEBUG "Node at 0x%08x was already marked obsolete (nodetype 0x%04x)\n", ref_offset(ref), je16_to_cpu(n.nodetype)));
		goto out_erase_sem;
	}
	/* XXX FIXME: This is ugly now */
	n.nodetype = cpu_to_je16(je16_to_cpu(n.nodetype) & ~JFFS2_NODE_ACCURATE);
	ret = jffs2_flash_write(c, ref_offset(ref), sizeof(n), &retlen, (char *)&n);
	if (ret) {
		printk(KERN_WARNING "Write error in obliterating obsoleted node at 0x%08x: %d\n", ref_offset(ref), ret);
		goto out_erase_sem;
	}
	if (retlen != sizeof(n)) {
		printk(KERN_WARNING "Short write in obliterating obsoleted node at 0x%08x: %zd\n", ref_offset(ref), retlen);
		goto out_erase_sem;
	}

	/* Nodes which have been marked obsolete no longer need to be
	   associated with any inode. Remove them from the per-inode list.

	   Note we can't do this for NAND at the moment because we need
	   obsolete dirent nodes to stay on the lists, because of the
	   horridness in jffs2_garbage_collect_deletion_dirent(). Also
	   because we delete the inocache, and on NAND we need that to
	   stay around until all the nodes are actually erased, in order
	   to stop us from giving the same inode number to another newly
	   created inode. */
	if (ref->next_in_ino) {
		struct jffs2_inode_cache *ic;
		struct jffs2_raw_node_ref **p;

		spin_lock(&c->erase_completion_lock);

		ic = jffs2_raw_ref_to_ic(ref);
		for (p = &ic->nodes; (*p) != ref; p = &((*p)->next_in_ino))
			;

		*p = ref->next_in_ino;
		ref->next_in_ino = NULL;

		switch (ic->class) {
#ifdef CONFIG_JFFS2_FS_XATTR
			case RAWNODE_CLASS_XATTR_DATUM:
				jffs2_release_xattr_datum(c, (struct jffs2_xattr_datum *)ic);
				break;
			case RAWNODE_CLASS_XATTR_REF:
				jffs2_release_xattr_ref(c, (struct jffs2_xattr_ref *)ic);
				break;
#endif
			default:
				if (ic->nodes == (void *)ic && ic->nlink == 0)
					jffs2_del_ino_cache(c, ic);
				break;
		}
		spin_unlock(&c->erase_completion_lock);
	}

 out_erase_sem:
	mutex_unlock(&c->erase_free_sem);
}

int jffs2_thread_should_wake(struct jffs2_sb_info *c)
{
	int ret = 0;
	uint32_t dirty;
	int nr_very_dirty = 0;
	struct jffs2_eraseblock *jeb;

	if (c->unchecked_size) {
		D1(printk(KERN_DEBUG "jffs2_thread_should_wake(): unchecked_size %d, checked_ino #%d\n",
			  c->unchecked_size, c->checked_ino));
		return 1;
	}

	/* dirty_size contains blocks on erase_pending_list
	 * those blocks are counted in c->nr_erasing_blocks.
	 * If one block is actually erased, it is not longer counted as dirty_space
	 * but it is counted in c->nr_erasing_blocks, so we add it and subtract it
	 * with c->nr_erasing_blocks * c->sector_size again.
	 * Blocks on erasable_list are counted as dirty_size, but not in c->nr_erasing_blocks
	 * This helps us to force gc and pick eventually a clean block to spread the load.
	 */
	dirty = c->dirty_size + c->erasing_size - c->nr_erasing_blocks * c->sector_size;

	if (c->nr_free_blocks + c->nr_erasing_blocks < c->resv_blocks_gctrigger &&
			(dirty > c->nospc_dirty_size))
		ret = 1;

	list_for_each_entry(jeb, &c->very_dirty_list, list) {
		nr_very_dirty++;
		if (nr_very_dirty == c->vdirty_blocks_gctrigger) {
			ret = 1;
			/* In debug mode, actually go through and count them all */
			D1(continue);
			break;
		}
	}

	D1(printk(KERN_DEBUG "jffs2_thread_should_wake(): nr_free_blocks %d, nr_erasing_blocks %d, dirty_size 0x%x, vdirty_blocks %d: %s\n",
		  c->nr_free_blocks, c->nr_erasing_blocks, c->dirty_size, nr_very_dirty, ret?"yes":"no"));

	return ret;
}
