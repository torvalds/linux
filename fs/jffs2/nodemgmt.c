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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include <linux/sched/signal.h>
#include "nodelist.h"
#include "debug.h"

/*
 * Check whether the user is allowed to write.
 */
static int jffs2_rp_can_write(struct jffs2_sb_info *c)
{
	uint32_t avail;
	struct jffs2_mount_opts *opts = &c->mount_opts;

	avail = c->dirty_size + c->free_size + c->unchecked_size +
		c->erasing_size - c->resv_blocks_write * c->sector_size
		- c->nospc_dirty_size;

	if (avail < 2 * opts->rp_size)
		jffs2_dbg(1, "rpsize %u, dirty_size %u, free_size %u, "
			  "erasing_size %u, unchecked_size %u, "
			  "nr_erasing_blocks %u, avail %u, resrv %u\n",
			  opts->rp_size, c->dirty_size, c->free_size,
			  c->erasing_size, c->unchecked_size,
			  c->nr_erasing_blocks, avail, c->nospc_dirty_size);

	if (avail > opts->rp_size)
		return 1;

	/* Always allow root */
	if (capable(CAP_SYS_RESOURCE))
		return 1;

	jffs2_dbg(1, "forbid writing\n");
	return 0;
}

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

	jffs2_dbg(1, "%s(): Requested 0x%x bytes\n", __func__, minsize);
	mutex_lock(&c->alloc_sem);

	jffs2_dbg(1, "%s(): alloc sem got\n", __func__);

	spin_lock(&c->erase_completion_lock);

	/*
	 * Check if the free space is greater then size of the reserved pool.
	 * If not, only allow root to proceed with writing.
	 */
	if (prio != ALLOC_DELETION && !jffs2_rp_can_write(c)) {
		ret = -ENOSPC;
		goto out;
	}

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
					jffs2_dbg(1, "%s(): Low on dirty space to GC, but it's a deletion. Allowing...\n",
						  __func__);
					break;
				}
				jffs2_dbg(1, "dirty size 0x%08x + unchecked_size 0x%08x < nospc_dirty_size 0x%08x, returning -ENOSPC\n",
					  dirty, c->unchecked_size,
					  c->sector_size);

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
					jffs2_dbg(1, "%s(): Low on possibly available space, but it's a deletion. Allowing...\n",
						  __func__);
					break;
				}

				jffs2_dbg(1, "max. available size 0x%08x  < blocksneeded * sector_size 0x%08x, returning -ENOSPC\n",
					  avail, blocksneeded * c->sector_size);
				spin_unlock(&c->erase_completion_lock);
				mutex_unlock(&c->alloc_sem);
				return -ENOSPC;
			}

			mutex_unlock(&c->alloc_sem);

			jffs2_dbg(1, "Triggering GC pass. nr_free_blocks %d, nr_erasing_blocks %d, free_size 0x%08x, dirty_size 0x%08x, wasted_size 0x%08x, used_size 0x%08x, erasing_size 0x%08x, bad_size 0x%08x (total 0x%08x of 0x%08x)\n",
				  c->nr_free_blocks, c->nr_erasing_blocks,
				  c->free_size, c->dirty_size, c->wasted_size,
				  c->used_size, c->erasing_size, c->bad_size,
				  c->free_size + c->dirty_size +
				  c->wasted_size + c->used_size +
				  c->erasing_size + c->bad_size,
				  c->flash_size);
			spin_unlock(&c->erase_completion_lock);

			ret = jffs2_garbage_collect_pass(c);

			if (ret == -EAGAIN) {
				spin_lock(&c->erase_completion_lock);
				if (c->nr_erasing_blocks &&
				    list_empty(&c->erase_pending_list) &&
				    list_empty(&c->erase_complete_list)) {
					DECLARE_WAITQUEUE(wait, current);
					set_current_state(TASK_UNINTERRUPTIBLE);
					add_wait_queue(&c->erase_wait, &wait);
					jffs2_dbg(1, "%s waiting for erase to complete\n",
						  __func__);
					spin_unlock(&c->erase_completion_lock);

					schedule();
					remove_wait_queue(&c->erase_wait, &wait);
				} else
					spin_unlock(&c->erase_completion_lock);
			} else if (ret)
				return ret;

			cond_resched();

			if (signal_pending(current))
				return -EINTR;

			mutex_lock(&c->alloc_sem);
			spin_lock(&c->erase_completion_lock);
		}

		ret = jffs2_do_reserve_space(c, minsize, len, sumsize);
		if (ret) {
			jffs2_dbg(1, "%s(): ret is %d\n", __func__, ret);
		}
	}

out:
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
	int ret;
	minsize = PAD(minsize);

	jffs2_dbg(1, "%s(): Requested 0x%x bytes\n", __func__, minsize);

	while (true) {
		spin_lock(&c->erase_completion_lock);
		ret = jffs2_do_reserve_space(c, minsize, len, sumsize);
		if (ret) {
			jffs2_dbg(1, "%s(): looping, ret is %d\n",
				  __func__, ret);
		}
		spin_unlock(&c->erase_completion_lock);

		if (ret == -EAGAIN)
			cond_resched();
		else
			break;
	}
	if (!ret)
		ret = jffs2_prealloc_raw_node_refs(c, c->nextblock, 1);

	return ret;
}


/* Classify nextblock (clean, dirty of verydirty) and force to select an other one */

static void jffs2_close_nextblock(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb)
{

	if (c->nextblock == NULL) {
		jffs2_dbg(1, "%s(): Erase block at 0x%08x has already been placed in a list\n",
			  __func__, jeb->offset);
		return;
	}
	/* Check, if we have a dirty block now, or if it was dirty already */
	if (ISDIRTY (jeb->wasted_size + jeb->dirty_size)) {
		c->dirty_size += jeb->wasted_size;
		c->wasted_size -= jeb->wasted_size;
		jeb->dirty_size += jeb->wasted_size;
		jeb->wasted_size = 0;
		if (VERYDIRTY(c, jeb->dirty_size)) {
			jffs2_dbg(1, "Adding full erase block at 0x%08x to very_dirty_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
				  jeb->offset, jeb->free_size, jeb->dirty_size,
				  jeb->used_size);
			list_add_tail(&jeb->list, &c->very_dirty_list);
		} else {
			jffs2_dbg(1, "Adding full erase block at 0x%08x to dirty_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
				  jeb->offset, jeb->free_size, jeb->dirty_size,
				  jeb->used_size);
			list_add_tail(&jeb->list, &c->dirty_list);
		}
	} else {
		jffs2_dbg(1, "Adding full erase block at 0x%08x to clean_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size,
			  jeb->used_size);
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
			jffs2_garbage_collect_trigger(c);
			jffs2_dbg(1, "%s(): Triggering erase of erasable block at 0x%08x\n",
				  __func__, ejeb->offset);
		}

		if (!c->nr_erasing_blocks &&
			!list_empty(&c->erasable_pending_wbuf_list)) {
			jffs2_dbg(1, "%s(): Flushing write buffer\n",
				  __func__);
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
			pr_crit("Argh. No free space left for GC. nr_erasing_blocks is %d. nr_free_blocks is %d. (erasableempty: %s, erasingempty: %s, erasependingempty: %s)\n",
				c->nr_erasing_blocks, c->nr_free_blocks,
				list_empty(&c->erasable_list) ? "yes" : "no",
				list_empty(&c->erasing_list) ? "yes" : "no",
				list_empty(&c->erase_pending_list) ? "yes" : "no");
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

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	/* adjust write buffer offset, else we get a non contiguous write bug */
	if (!(c->wbuf_ofs % c->sector_size) && !c->wbuf_len)
		c->wbuf_ofs = 0xffffffff;
#endif

	jffs2_dbg(1, "%s(): new nextblock = 0x%08x\n",
		  __func__, c->nextblock->offset);

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
				jffs2_dbg(1, "%s(): Flushing write buffer\n",
					  __func__);
				jffs2_flush_wbuf_pad(c);
				spin_lock(&c->erase_completion_lock);
				jeb = c->nextblock;
				goto restart;
			}

			spin_unlock(&c->erase_completion_lock);

			ret = jffs2_prealloc_raw_node_refs(c, jeb, 1);

			/* Just lock it again and continue. Nothing much can change because
			   we hold c->alloc_sem anyway. In fact, it's not entirely clear why
			   we hold c->erase_completion_lock in the majority of this function...
			   but that's a question for another (more caffeine-rich) day. */
			spin_lock(&c->erase_completion_lock);

			if (ret)
				return ret;

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
			pr_warn("Eep. Block 0x%08x taken from free_list had free_size of 0x%08x!!\n",
				jeb->offset, jeb->free_size);
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

	jffs2_dbg(1, "%s(): Giving 0x%x bytes at 0x%x\n",
		  __func__,
		  *len, jeb->offset + (c->sector_size - jeb->free_size));
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

	jffs2_dbg(1, "%s(): Node at 0x%x(%d), size 0x%x\n",
		  __func__, ofs & ~3, ofs & 3, len);
#if 1
	/* Allow non-obsolete nodes only to be added at the end of c->nextblock, 
	   if c->nextblock is set. Note that wbuf.c will file obsolete nodes
	   even after refiling c->nextblock */
	if ((c->nextblock || ((ofs & 3) != REF_OBSOLETE))
	    && (jeb != c->nextblock || (ofs & ~3) != jeb->offset + (c->sector_size - jeb->free_size))) {
		pr_warn("argh. node added in wrong place at 0x%08x(%d)\n",
			ofs & ~3, ofs & 3);
		if (c->nextblock)
			pr_warn("nextblock 0x%08x", c->nextblock->offset);
		else
			pr_warn("No nextblock");
		pr_cont(", expected at %08x\n",
			jeb->offset + (c->sector_size - jeb->free_size));
		return ERR_PTR(-EINVAL);
	}
#endif
	spin_lock(&c->erase_completion_lock);

	new = jffs2_link_node_ref(c, jeb, ofs, len, ic);

	if (!jeb->free_size && !jeb->dirty_size && !ISDIRTY(jeb->wasted_size)) {
		/* If it lives on the dirty_list, jffs2_reserve_space will put it there */
		jffs2_dbg(1, "Adding full erase block at 0x%08x to clean_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size,
			  jeb->used_size);
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
	jffs2_dbg(1, "jffs2_complete_reservation()\n");
	spin_lock(&c->erase_completion_lock);
	jffs2_garbage_collect_trigger(c);
	spin_unlock(&c->erase_completion_lock);
	mutex_unlock(&c->alloc_sem);
}

static inline int on_list(struct list_head *obj, struct list_head *head)
{
	struct list_head *this;

	list_for_each(this, head) {
		if (this == obj) {
			jffs2_dbg(1, "%p is on list at %p\n", obj, head);
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
		pr_notice("EEEEEK. jffs2_mark_node_obsolete called with NULL node\n");
		return;
	}
	if (ref_obsolete(ref)) {
		jffs2_dbg(1, "%s(): called with already obsolete node at 0x%08x\n",
			  __func__, ref_offset(ref));
		return;
	}
	blocknr = ref->flash_offset / c->sector_size;
	if (blocknr >= c->nr_blocks) {
		pr_notice("raw node at 0x%08x is off the end of device!\n",
			  ref->flash_offset);
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
				pr_notice("raw unchecked node of size 0x%08x freed from erase block %d at 0x%08x, but unchecked_size was already 0x%08x\n",
					  freed_len, blocknr,
					  ref->flash_offset, jeb->used_size);
			BUG();
		})
			jffs2_dbg(1, "Obsoleting previously unchecked node at 0x%08x of len %x\n",
				  ref_offset(ref), freed_len);
		jeb->unchecked_size -= freed_len;
		c->unchecked_size -= freed_len;
	} else {
		D1(if (unlikely(jeb->used_size < freed_len)) {
				pr_notice("raw node of size 0x%08x freed from erase block %d at 0x%08x, but used_size was already 0x%08x\n",
					  freed_len, blocknr,
					  ref->flash_offset, jeb->used_size);
			BUG();
		})
			jffs2_dbg(1, "Obsoleting node at 0x%08x of len %#x: ",
				  ref_offset(ref), freed_len);
		jeb->used_size -= freed_len;
		c->used_size -= freed_len;
	}

	// Take care, that wasted size is taken into concern
	if ((jeb->dirty_size || ISDIRTY(jeb->wasted_size + freed_len)) && jeb != c->nextblock) {
		jffs2_dbg(1, "Dirtying\n");
		addedsize = freed_len;
		jeb->dirty_size += freed_len;
		c->dirty_size += freed_len;

		/* Convert wasted space to dirty, if not a bad block */
		if (jeb->wasted_size) {
			if (on_list(&jeb->list, &c->bad_used_list)) {
				jffs2_dbg(1, "Leaving block at %08x on the bad_used_list\n",
					  jeb->offset);
				addedsize = 0; /* To fool the refiling code later */
			} else {
				jffs2_dbg(1, "Converting %d bytes of wasted space to dirty in block at %08x\n",
					  jeb->wasted_size, jeb->offset);
				addedsize += jeb->wasted_size;
				jeb->dirty_size += jeb->wasted_size;
				c->dirty_size += jeb->wasted_size;
				c->wasted_size -= jeb->wasted_size;
				jeb->wasted_size = 0;
			}
		}
	} else {
		jffs2_dbg(1, "Wasting\n");
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
		jffs2_dbg(2, "Not moving nextblock 0x%08x to dirty/erase_pending list\n",
			  jeb->offset);
	} else if (!jeb->used_size && !jeb->unchecked_size) {
		if (jeb == c->gcblock) {
			jffs2_dbg(1, "gcblock at 0x%08x completely dirtied. Clearing gcblock...\n",
				  jeb->offset);
			c->gcblock = NULL;
		} else {
			jffs2_dbg(1, "Eraseblock at 0x%08x completely dirtied. Removing from (dirty?) list...\n",
				  jeb->offset);
			list_del(&jeb->list);
		}
		if (jffs2_wbuf_dirty(c)) {
			jffs2_dbg(1, "...and adding to erasable_pending_wbuf_list\n");
			list_add_tail(&jeb->list, &c->erasable_pending_wbuf_list);
		} else {
			if (jiffies & 127) {
				/* Most of the time, we just erase it immediately. Otherwise we
				   spend ages scanning it on mount, etc. */
				jffs2_dbg(1, "...and adding to erase_pending_list\n");
				list_add_tail(&jeb->list, &c->erase_pending_list);
				c->nr_erasing_blocks++;
				jffs2_garbage_collect_trigger(c);
			} else {
				/* Sometimes, however, we leave it elsewhere so it doesn't get
				   immediately reused, and we spread the load a bit. */
				jffs2_dbg(1, "...and adding to erasable_list\n");
				list_add_tail(&jeb->list, &c->erasable_list);
			}
		}
		jffs2_dbg(1, "Done OK\n");
	} else if (jeb == c->gcblock) {
		jffs2_dbg(2, "Not moving gcblock 0x%08x to dirty_list\n",
			  jeb->offset);
	} else if (ISDIRTY(jeb->dirty_size) && !ISDIRTY(jeb->dirty_size - addedsize)) {
		jffs2_dbg(1, "Eraseblock at 0x%08x is freshly dirtied. Removing from clean list...\n",
			  jeb->offset);
		list_del(&jeb->list);
		jffs2_dbg(1, "...and adding to dirty_list\n");
		list_add_tail(&jeb->list, &c->dirty_list);
	} else if (VERYDIRTY(c, jeb->dirty_size) &&
		   !VERYDIRTY(c, jeb->dirty_size - addedsize)) {
		jffs2_dbg(1, "Eraseblock at 0x%08x is now very dirty. Removing from dirty list...\n",
			  jeb->offset);
		list_del(&jeb->list);
		jffs2_dbg(1, "...and adding to very_dirty_list\n");
		list_add_tail(&jeb->list, &c->very_dirty_list);
	} else {
		jffs2_dbg(1, "Eraseblock at 0x%08x not moved anywhere. (free 0x%08x, dirty 0x%08x, used 0x%08x)\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size,
			  jeb->used_size);
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

	jffs2_dbg(1, "obliterating obsoleted node at 0x%08x\n",
		  ref_offset(ref));
	ret = jffs2_flash_read(c, ref_offset(ref), sizeof(n), &retlen, (char *)&n);
	if (ret) {
		pr_warn("Read error reading from obsoleted node at 0x%08x: %d\n",
			ref_offset(ref), ret);
		goto out_erase_sem;
	}
	if (retlen != sizeof(n)) {
		pr_warn("Short read from obsoleted node at 0x%08x: %zd\n",
			ref_offset(ref), retlen);
		goto out_erase_sem;
	}
	if (PAD(je32_to_cpu(n.totlen)) != PAD(freed_len)) {
		pr_warn("Node totlen on flash (0x%08x) != totlen from node ref (0x%08x)\n",
			je32_to_cpu(n.totlen), freed_len);
		goto out_erase_sem;
	}
	if (!(je16_to_cpu(n.nodetype) & JFFS2_NODE_ACCURATE)) {
		jffs2_dbg(1, "Node at 0x%08x was already marked obsolete (nodetype 0x%04x)\n",
			  ref_offset(ref), je16_to_cpu(n.nodetype));
		goto out_erase_sem;
	}
	/* XXX FIXME: This is ugly now */
	n.nodetype = cpu_to_je16(je16_to_cpu(n.nodetype) & ~JFFS2_NODE_ACCURATE);
	ret = jffs2_flash_write(c, ref_offset(ref), sizeof(n), &retlen, (char *)&n);
	if (ret) {
		pr_warn("Write error in obliterating obsoleted node at 0x%08x: %d\n",
			ref_offset(ref), ret);
		goto out_erase_sem;
	}
	if (retlen != sizeof(n)) {
		pr_warn("Short write in obliterating obsoleted node at 0x%08x: %zd\n",
			ref_offset(ref), retlen);
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
				if (ic->nodes == (void *)ic && ic->pino_nlink == 0)
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

	if (!list_empty(&c->erase_complete_list) ||
	    !list_empty(&c->erase_pending_list))
		return 1;

	if (c->unchecked_size) {
		jffs2_dbg(1, "jffs2_thread_should_wake(): unchecked_size %d, check_ino #%d\n",
			  c->unchecked_size, c->check_ino);
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

	jffs2_dbg(1, "%s(): nr_free_blocks %d, nr_erasing_blocks %d, dirty_size 0x%x, vdirty_blocks %d: %s\n",
		  __func__, c->nr_free_blocks, c->nr_erasing_blocks,
		  c->dirty_size, nr_very_dirty, ret ? "yes" : "no");

	return ret;
}
