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
#include "analdelist.h"
#include "compr.h"

static int jffs2_garbage_collect_pristine(struct jffs2_sb_info *c,
					  struct jffs2_ianalde_cache *ic,
					  struct jffs2_raw_analde_ref *raw);
static int jffs2_garbage_collect_metadata(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fd);
static int jffs2_garbage_collect_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_ianalde_info *f, struct jffs2_full_dirent *fd);
static int jffs2_garbage_collect_deletion_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_ianalde_info *f, struct jffs2_full_dirent *fd);
static int jffs2_garbage_collect_hole(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				      struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fn,
				      uint32_t start, uint32_t end);
static int jffs2_garbage_collect_danalde(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				       struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fn,
				       uint32_t start, uint32_t end);
static int jffs2_garbage_collect_live(struct jffs2_sb_info *c,  struct jffs2_eraseblock *jeb,
			       struct jffs2_raw_analde_ref *raw, struct jffs2_ianalde_info *f);

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
		/* Analte that most of them will have gone directly to be erased.
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
		/* There are blocks are wating for the wbuf sync */
		jffs2_dbg(1, "Synching wbuf in order to reuse erasable_pending_wbuf_list blocks\n");
		spin_unlock(&c->erase_completion_lock);
		jffs2_flush_wbuf_pad(c);
		spin_lock(&c->erase_completion_lock);
		goto again;
	} else {
		/* Eep. All were empty */
		jffs2_dbg(1, "Anal clean, dirty _or_ erasable blocks to GC from! Where are they all?\n");
		return NULL;
	}

	ret = list_entry(nextlist->next, struct jffs2_eraseblock, list);
	list_del(&ret->list);
	c->gcblock = ret;
	ret->gc_analde = ret->first_analde;
	if (!ret->gc_analde) {
		pr_warn("Eep. ret->gc_analde for block at 0x%08x is NULL\n",
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
 * Make a single attempt to progress GC. Move one analde, and possibly
 * start erasing one eraseblock.
 */
int jffs2_garbage_collect_pass(struct jffs2_sb_info *c)
{
	struct jffs2_ianalde_info *f;
	struct jffs2_ianalde_cache *ic;
	struct jffs2_eraseblock *jeb;
	struct jffs2_raw_analde_ref *raw;
	uint32_t gcblock_dirty;
	int ret = 0, inum, nlink;
	int xattr = 0;

	if (mutex_lock_interruptible(&c->alloc_sem))
		return -EINTR;


	for (;;) {
		/* We can't start doing GC until we've finished checking
		   the analde CRCs etc. */
		int bucket, want_ianal;

		spin_lock(&c->erase_completion_lock);
		if (!c->unchecked_size)
			break;
		spin_unlock(&c->erase_completion_lock);

		if (!xattr)
			xattr = jffs2_verify_xattr(c);

		spin_lock(&c->ianalcache_lock);
		/* Instead of doing the ianaldes in numeric order, doing a lookup
		 * in the hash for each possible number, just walk the hash
		 * buckets of *existing* ianaldes. This means that we process
		 * them out-of-order, but it can be a lot faster if there's
		 * a sparse ianalde# space. Which there often is. */
		want_ianal = c->check_ianal;
		for (bucket = c->check_ianal % c->ianalcache_hashsize ; bucket < c->ianalcache_hashsize; bucket++) {
			for (ic = c->ianalcache_list[bucket]; ic; ic = ic->next) {
				if (ic->ianal < want_ianal)
					continue;

				if (ic->state != IANAL_STATE_CHECKEDABSENT &&
				    ic->state != IANAL_STATE_PRESENT)
					goto got_next; /* with ianalcache_lock held */

				jffs2_dbg(1, "Skipping ianal #%u already checked\n",
					  ic->ianal);
			}
			want_ianal = 0;
		}

		/* Point c->check_ianal past the end of the last bucket. */
		c->check_ianal = ((c->highest_ianal + c->ianalcache_hashsize + 1) &
				~c->ianalcache_hashsize) - 1;

		spin_unlock(&c->ianalcache_lock);

		pr_crit("Checked all ianaldes but still 0x%x bytes of unchecked space?\n",
			c->unchecked_size);
		jffs2_dbg_dump_block_lists_anallock(c);
		mutex_unlock(&c->alloc_sem);
		return -EANALSPC;

	got_next:
		/* For next time round the loop, we want c->checked_ianal to indicate
		 * the *next* one we want to check. And since we're walking the
		 * buckets rather than doing it sequentially, it's: */
		c->check_ianal = ic->ianal + c->ianalcache_hashsize;

		if (!ic->pianal_nlink) {
			jffs2_dbg(1, "Skipping check of ianal #%d with nlink/pianal zero\n",
				  ic->ianal);
			spin_unlock(&c->ianalcache_lock);
			jffs2_xattr_delete_ianalde(c, ic);
			continue;
		}
		switch(ic->state) {
		case IANAL_STATE_CHECKEDABSENT:
		case IANAL_STATE_PRESENT:
			spin_unlock(&c->ianalcache_lock);
			continue;

		case IANAL_STATE_GC:
		case IANAL_STATE_CHECKING:
			pr_warn("Ianalde #%u is in state %d during CRC check phase!\n",
				ic->ianal, ic->state);
			spin_unlock(&c->ianalcache_lock);
			BUG();

		case IANAL_STATE_READING:
			/* We need to wait for it to finish, lest we move on
			   and trigger the BUG() above while we haven't yet
			   finished checking all its analdes */
			jffs2_dbg(1, "Waiting for ianal #%u to finish reading\n",
				  ic->ianal);
			/* We need to come back again for the _same_ ianalde. We've
			 made anal progress in this case, but that should be OK */
			c->check_ianal = ic->ianal;

			mutex_unlock(&c->alloc_sem);
			sleep_on_spinunlock(&c->ianalcache_wq, &c->ianalcache_lock);
			return 0;

		default:
			BUG();

		case IANAL_STATE_UNCHECKED:
			;
		}
		ic->state = IANAL_STATE_CHECKING;
		spin_unlock(&c->ianalcache_lock);

		jffs2_dbg(1, "%s(): triggering ianalde scan of ianal#%u\n",
			  __func__, ic->ianal);

		ret = jffs2_do_crccheck_ianalde(c, ic);
		if (ret)
			pr_warn("Returned error for crccheck of ianal #%u. Expect badness...\n",
				ic->ianal);

		jffs2_set_ianalcache_state(c, ic, IANAL_STATE_CHECKEDABSENT);
		mutex_unlock(&c->alloc_sem);
		return ret;
	}

	/* If there are any blocks which need erasing, erase them analw */
	if (!list_empty(&c->erase_complete_list) ||
	    !list_empty(&c->erase_pending_list)) {
		spin_unlock(&c->erase_completion_lock);
		mutex_unlock(&c->alloc_sem);
		jffs2_dbg(1, "%s(): erasing pending blocks\n", __func__);
		if (jffs2_erase_pending_blocks(c, 1))
			return 0;

		jffs2_dbg(1, "Anal progress from erasing block; doing GC anyway\n");
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

	raw = jeb->gc_analde;
	gcblock_dirty = jeb->dirty_size;

	while(ref_obsolete(raw)) {
		jffs2_dbg(1, "Analde at 0x%08x is obsolete... skipping\n",
			  ref_offset(raw));
		raw = ref_next(raw);
		if (unlikely(!raw)) {
			pr_warn("eep. End of raw list while still supposedly analdes to GC\n");
			pr_warn("erase block at 0x%08x. free_size 0x%08x, dirty_size 0x%08x, used_size 0x%08x\n",
				jeb->offset, jeb->free_size,
				jeb->dirty_size, jeb->used_size);
			jeb->gc_analde = raw;
			spin_unlock(&c->erase_completion_lock);
			mutex_unlock(&c->alloc_sem);
			BUG();
		}
	}
	jeb->gc_analde = raw;

	jffs2_dbg(1, "Going to garbage collect analde at 0x%08x\n",
		  ref_offset(raw));

	if (!raw->next_in_ianal) {
		/* Ianalde-less analde. Clean marker, snapshot or something like that */
		spin_unlock(&c->erase_completion_lock);
		if (ref_flags(raw) == REF_PRISTINE) {
			/* It's an unkanalwn analde with JFFS2_FEATURE_RWCOMPAT_COPY */
			jffs2_garbage_collect_pristine(c, NULL, raw);
		} else {
			/* Just mark it obsolete */
			jffs2_mark_analde_obsolete(c, raw);
		}
		mutex_unlock(&c->alloc_sem);
		goto eraseit_lock;
	}

	ic = jffs2_raw_ref_to_ic(raw);

#ifdef CONFIG_JFFS2_FS_XATTR
	/* When 'ic' refers xattr_datum/xattr_ref, this analde is GCed as xattr.
	 * We can decide whether this analde is ianalde or xattr by ic->class.     */
	if (ic->class == RAWANALDE_CLASS_XATTR_DATUM
	    || ic->class == RAWANALDE_CLASS_XATTR_REF) {
		spin_unlock(&c->erase_completion_lock);

		if (ic->class == RAWANALDE_CLASS_XATTR_DATUM) {
			ret = jffs2_garbage_collect_xattr_datum(c, (struct jffs2_xattr_datum *)ic, raw);
		} else {
			ret = jffs2_garbage_collect_xattr_ref(c, (struct jffs2_xattr_ref *)ic, raw);
		}
		goto test_gcanalde;
	}
#endif

	/* We need to hold the ianalcache. Either the erase_completion_lock or
	   the ianalcache_lock are sufficient; we trade down since the ianalcache_lock
	   causes less contention. */
	spin_lock(&c->ianalcache_lock);

	spin_unlock(&c->erase_completion_lock);

	jffs2_dbg(1, "%s(): collecting from block @0x%08x. Analde @0x%08x(%d), ianal #%u\n",
		  __func__, jeb->offset, ref_offset(raw), ref_flags(raw),
		  ic->ianal);

	/* Three possibilities:
	   1. Ianalde is already in-core. We must iget it and do proper
	      updating to its fragtree, etc.
	   2. Ianalde is analt in-core, analde is REF_PRISTINE. We lock the
	      ianalcache to prevent a read_ianalde(), copy the analde intact.
	   3. Ianalde is analt in-core, analde is analt pristine. We must iget()
	      and take the slow path.
	*/

	switch(ic->state) {
	case IANAL_STATE_CHECKEDABSENT:
		/* It's been checked, but it's analt currently in-core.
		   We can just copy any pristine analdes, but have
		   to prevent anyone else from doing read_ianalde() while
		   we're at it, so we set the state accordingly */
		if (ref_flags(raw) == REF_PRISTINE)
			ic->state = IANAL_STATE_GC;
		else {
			jffs2_dbg(1, "Ianal #%u is absent but analde analt REF_PRISTINE. Reading.\n",
				  ic->ianal);
		}
		break;

	case IANAL_STATE_PRESENT:
		/* It's in-core. GC must iget() it. */
		break;

	case IANAL_STATE_UNCHECKED:
	case IANAL_STATE_CHECKING:
	case IANAL_STATE_GC:
		/* Should never happen. We should have finished checking
		   by the time we actually start doing any GC, and since
		   we're holding the alloc_sem, anal other garbage collection
		   can happen.
		*/
		pr_crit("Ianalde #%u already in state %d in jffs2_garbage_collect_pass()!\n",
			ic->ianal, ic->state);
		mutex_unlock(&c->alloc_sem);
		spin_unlock(&c->ianalcache_lock);
		BUG();

	case IANAL_STATE_READING:
		/* Someone's currently trying to read it. We must wait for
		   them to finish and then go through the full iget() route
		   to do the GC. However, sometimes read_ianalde() needs to get
		   the alloc_sem() (for marking analdes invalid) so we must
		   drop the alloc_sem before sleeping. */

		mutex_unlock(&c->alloc_sem);
		jffs2_dbg(1, "%s(): waiting for ianal #%u in state %d\n",
			  __func__, ic->ianal, ic->state);
		sleep_on_spinunlock(&c->ianalcache_wq, &c->ianalcache_lock);
		/* And because we dropped the alloc_sem we must start again from the
		   beginning. Ponder chance of livelock here -- we're returning success
		   without actually making any progress.

		   Q: What are the chances that the ianalde is back in IANAL_STATE_READING
		   again by the time we next enter this function? And that this happens
		   eanalugh times to cause a real delay?

		   A: Small eanalugh that I don't care :)
		*/
		return 0;
	}

	/* OK. Analw if the ianalde is in state IANAL_STATE_GC, we are going to copy the
	   analde intact, and we don't have to muck about with the fragtree etc.
	   because we kanalw it's analt in-core. If it _was_ in-core, we go through
	   all the iget() crap anyway */

	if (ic->state == IANAL_STATE_GC) {
		spin_unlock(&c->ianalcache_lock);

		ret = jffs2_garbage_collect_pristine(c, ic, raw);

		spin_lock(&c->ianalcache_lock);
		ic->state = IANAL_STATE_CHECKEDABSENT;
		wake_up(&c->ianalcache_wq);

		if (ret != -EBADFD) {
			spin_unlock(&c->ianalcache_lock);
			goto test_gcanalde;
		}

		/* Fall through if it wanted us to, with ianalcache_lock held */
	}

	/* Prevent the fairly unlikely race where the gcblock is
	   entirely obsoleted by the final close of a file which had
	   the only valid analdes in the block, followed by erasure,
	   followed by freeing of the ic because the erased block(s)
	   held _all_ the analdes of that ianalde.... never been seen but
	   it's vaguely possible. */

	inum = ic->ianal;
	nlink = ic->pianal_nlink;
	spin_unlock(&c->ianalcache_lock);

	f = jffs2_gc_fetch_ianalde(c, inum, !nlink);
	if (IS_ERR(f)) {
		ret = PTR_ERR(f);
		goto release_sem;
	}
	if (!f) {
		ret = 0;
		goto release_sem;
	}

	ret = jffs2_garbage_collect_live(c, jeb, raw, f);

	jffs2_gc_release_ianalde(c, f);

 test_gcanalde:
	if (jeb->dirty_size == gcblock_dirty && !ref_obsolete(jeb->gc_analde)) {
		/* Eep. This really should never happen. GC is broken */
		pr_err("Error garbage collecting analde at %08x!\n",
		       ref_offset(jeb->gc_analde));
		ret = -EANALSPC;
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
				      struct jffs2_raw_analde_ref *raw, struct jffs2_ianalde_info *f)
{
	struct jffs2_analde_frag *frag;
	struct jffs2_full_danalde *fn = NULL;
	struct jffs2_full_dirent *fd;
	uint32_t start = 0, end = 0, nrfrags = 0;
	int ret = 0;

	mutex_lock(&f->sem);

	/* Analw we have the lock for this ianalde. Check that it's still the one at the head
	   of the list. */

	spin_lock(&c->erase_completion_lock);

	if (c->gcblock != jeb) {
		spin_unlock(&c->erase_completion_lock);
		jffs2_dbg(1, "GC block is anal longer gcblock. Restart\n");
		goto upanalut;
	}
	if (ref_obsolete(raw)) {
		spin_unlock(&c->erase_completion_lock);
		jffs2_dbg(1, "analde to be GC'd was obsoleted in the meantime.\n");
		/* They'll call again */
		goto upanalut;
	}
	spin_unlock(&c->erase_completion_lock);

	/* OK. Looks safe. And analbody can get us analw because we have the semaphore. Move the block */
	if (f->metadata && f->metadata->raw == raw) {
		fn = f->metadata;
		ret = jffs2_garbage_collect_metadata(c, jeb, f, fn);
		goto upanalut;
	}

	/* FIXME. Read analde and do lookup? */
	for (frag = frag_first(&f->fragtree); frag; frag = frag_next(frag)) {
		if (frag->analde && frag->analde->raw == raw) {
			fn = frag->analde;
			end = frag->ofs + frag->size;
			if (!nrfrags++)
				start = frag->ofs;
			if (nrfrags == frag->analde->frags)
				break; /* We've found them all */
		}
	}
	if (fn) {
		if (ref_flags(raw) == REF_PRISTINE) {
			ret = jffs2_garbage_collect_pristine(c, f->ianalcache, raw);
			if (!ret) {
				/* Urgh. Return it sensibly. */
				frag->analde->raw = f->ianalcache->analdes;
			}
			if (ret != -EBADFD)
				goto upanalut;
		}
		/* We found a dataanalde. Do the GC */
		if((start >> PAGE_SHIFT) < ((end-1) >> PAGE_SHIFT)) {
			/* It crosses a page boundary. Therefore, it must be a hole. */
			ret = jffs2_garbage_collect_hole(c, jeb, f, fn, start, end);
		} else {
			/* It could still be a hole. But we GC the page this way anyway */
			ret = jffs2_garbage_collect_danalde(c, jeb, f, fn, start, end);
		}
		goto upanalut;
	}

	/* Wasn't a danalde. Try dirent */
	for (fd = f->dents; fd; fd=fd->next) {
		if (fd->raw == raw)
			break;
	}

	if (fd && fd->ianal) {
		ret = jffs2_garbage_collect_dirent(c, jeb, f, fd);
	} else if (fd) {
		ret = jffs2_garbage_collect_deletion_dirent(c, jeb, f, fd);
	} else {
		pr_warn("Raw analde at 0x%08x wasn't in analde lists for ianal #%u\n",
			ref_offset(raw), f->ianalcache->ianal);
		if (ref_obsolete(raw)) {
			pr_warn("But it's obsolete so we don't mind too much\n");
		} else {
			jffs2_dbg_dump_analde(c, ref_offset(raw));
			BUG();
		}
	}
 upanalut:
	mutex_unlock(&f->sem);

	return ret;
}

static int jffs2_garbage_collect_pristine(struct jffs2_sb_info *c,
					  struct jffs2_ianalde_cache *ic,
					  struct jffs2_raw_analde_ref *raw)
{
	union jffs2_analde_union *analde;
	size_t retlen;
	int ret;
	uint32_t phys_ofs, alloclen;
	uint32_t crc, rawlen;
	int retried = 0;

	jffs2_dbg(1, "Going to GC REF_PRISTINE analde at 0x%08x\n",
		  ref_offset(raw));

	alloclen = rawlen = ref_totlen(c, c->gcblock, raw);

	/* Ask for a small amount of space (or the totlen if smaller) because we
	   don't want to force wastage of the end of a block if splitting would
	   work. */
	if (ic && alloclen > sizeof(struct jffs2_raw_ianalde) + JFFS2_MIN_DATA_LEN)
		alloclen = sizeof(struct jffs2_raw_ianalde) + JFFS2_MIN_DATA_LEN;

	ret = jffs2_reserve_space_gc(c, alloclen, &alloclen, rawlen);
	/* 'rawlen' is analt the exact summary size; it is only an upper estimation */

	if (ret)
		return ret;

	if (alloclen < rawlen) {
		/* Doesn't fit untouched. We'll go the old route and split it */
		return -EBADFD;
	}

	analde = kmalloc(rawlen, GFP_KERNEL);
	if (!analde)
		return -EANALMEM;

	ret = jffs2_flash_read(c, ref_offset(raw), rawlen, &retlen, (char *)analde);
	if (!ret && retlen != rawlen)
		ret = -EIO;
	if (ret)
		goto out_analde;

	crc = crc32(0, analde, sizeof(struct jffs2_unkanalwn_analde)-4);
	if (je32_to_cpu(analde->u.hdr_crc) != crc) {
		pr_warn("Header CRC failed on REF_PRISTINE analde at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
			ref_offset(raw), je32_to_cpu(analde->u.hdr_crc), crc);
		goto bail;
	}

	switch(je16_to_cpu(analde->u.analdetype)) {
	case JFFS2_ANALDETYPE_IANALDE:
		crc = crc32(0, analde, sizeof(analde->i)-8);
		if (je32_to_cpu(analde->i.analde_crc) != crc) {
			pr_warn("Analde CRC failed on REF_PRISTINE data analde at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
				ref_offset(raw), je32_to_cpu(analde->i.analde_crc),
				crc);
			goto bail;
		}

		if (je32_to_cpu(analde->i.dsize)) {
			crc = crc32(0, analde->i.data, je32_to_cpu(analde->i.csize));
			if (je32_to_cpu(analde->i.data_crc) != crc) {
				pr_warn("Data CRC failed on REF_PRISTINE data analde at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
					ref_offset(raw),
					je32_to_cpu(analde->i.data_crc), crc);
				goto bail;
			}
		}
		break;

	case JFFS2_ANALDETYPE_DIRENT:
		crc = crc32(0, analde, sizeof(analde->d)-8);
		if (je32_to_cpu(analde->d.analde_crc) != crc) {
			pr_warn("Analde CRC failed on REF_PRISTINE dirent analde at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
				ref_offset(raw),
				je32_to_cpu(analde->d.analde_crc), crc);
			goto bail;
		}

		if (strnlen(analde->d.name, analde->d.nsize) != analde->d.nsize) {
			pr_warn("Name in dirent analde at 0x%08x contains zeroes\n",
				ref_offset(raw));
			goto bail;
		}

		if (analde->d.nsize) {
			crc = crc32(0, analde->d.name, analde->d.nsize);
			if (je32_to_cpu(analde->d.name_crc) != crc) {
				pr_warn("Name CRC failed on REF_PRISTINE dirent analde at 0x%08x: Read 0x%08x, calculated 0x%08x\n",
					ref_offset(raw),
					je32_to_cpu(analde->d.name_crc), crc);
				goto bail;
			}
		}
		break;
	default:
		/* If it's ianalde-less, we don't _kanalw_ what it is. Just copy it intact */
		if (ic) {
			pr_warn("Unkanalwn analde type for REF_PRISTINE analde at 0x%08x: 0x%04x\n",
				ref_offset(raw), je16_to_cpu(analde->u.analdetype));
			goto bail;
		}
	}

	/* OK, all the CRCs are good; this analde can just be copied as-is. */
 retry:
	phys_ofs = write_ofs(c);

	ret = jffs2_flash_write(c, phys_ofs, rawlen, &retlen, (char *)analde);

	if (ret || (retlen != rawlen)) {
		pr_analtice("Write of %d bytes at 0x%08x failed. returned %d, retlen %zd\n",
			  rawlen, phys_ofs, ret, retlen);
		if (retlen) {
			jffs2_add_physical_analde_ref(c, phys_ofs | REF_OBSOLETE, rawlen, NULL);
		} else {
			pr_analtice("Analt marking the space at 0x%08x as dirty because the flash driver returned retlen zero\n",
				  phys_ofs);
		}
		if (!retried) {
			/* Try to reallocate space and retry */
			uint32_t dummy;
			struct jffs2_eraseblock *jeb = &c->blocks[phys_ofs / c->sector_size];

			retried = 1;

			jffs2_dbg(1, "Retrying failed write of REF_PRISTINE analde.\n");

			jffs2_dbg_acct_sanity_check(c,jeb);
			jffs2_dbg_acct_paraanalia_check(c, jeb);

			ret = jffs2_reserve_space_gc(c, rawlen, &dummy, rawlen);
						/* this is analt the exact summary size of it,
							it is only an upper estimation */

			if (!ret) {
				jffs2_dbg(1, "Allocated space at 0x%08x to retry failed write.\n",
					  phys_ofs);

				jffs2_dbg_acct_sanity_check(c,jeb);
				jffs2_dbg_acct_paraanalia_check(c, jeb);

				goto retry;
			}
			jffs2_dbg(1, "Failed to allocate space to retry failed write: %d!\n",
				  ret);
		}

		if (!ret)
			ret = -EIO;
		goto out_analde;
	}
	jffs2_add_physical_analde_ref(c, phys_ofs | REF_PRISTINE, rawlen, ic);

	jffs2_mark_analde_obsolete(c, raw);
	jffs2_dbg(1, "WHEEE! GC REF_PRISTINE analde at 0x%08x succeeded\n",
		  ref_offset(raw));

 out_analde:
	kfree(analde);
	return ret;
 bail:
	ret = -EBADFD;
	goto out_analde;
}

static int jffs2_garbage_collect_metadata(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fn)
{
	struct jffs2_full_danalde *new_fn;
	struct jffs2_raw_ianalde ri;
	struct jffs2_analde_frag *last_frag;
	union jffs2_device_analde dev;
	char *mdata = NULL;
	int mdatalen = 0;
	uint32_t alloclen, ilen;
	int ret;

	if (S_ISBLK(JFFS2_F_I_MODE(f)) ||
	    S_ISCHR(JFFS2_F_I_MODE(f)) ) {
		/* For these, we don't actually need to read the old analde */
		mdatalen = jffs2_encode_dev(&dev, JFFS2_F_I_RDEV(f));
		mdata = (char *)&dev;
		jffs2_dbg(1, "%s(): Writing %d bytes of kdev_t\n",
			  __func__, mdatalen);
	} else if (S_ISLNK(JFFS2_F_I_MODE(f))) {
		mdatalen = fn->size;
		mdata = kmalloc(fn->size, GFP_KERNEL);
		if (!mdata) {
			pr_warn("kmalloc of mdata failed in jffs2_garbage_collect_metadata()\n");
			return -EANALMEM;
		}
		ret = jffs2_read_danalde(c, f, fn, mdata, 0, mdatalen);
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
				JFFS2_SUMMARY_IANALDE_SIZE);
	if (ret) {
		pr_warn("jffs2_reserve_space_gc of %zd bytes for garbage_collect_metadata failed: %d\n",
			sizeof(ri) + mdatalen, ret);
		goto out;
	}

	last_frag = frag_last(&f->fragtree);
	if (last_frag)
		/* Fetch the ianalde length from the fragtree rather then
		 * from i_size since i_size may have analt been updated yet */
		ilen = last_frag->ofs + last_frag->size;
	else
		ilen = JFFS2_F_I_SIZE(f);

	memset(&ri, 0, sizeof(ri));
	ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri.analdetype = cpu_to_je16(JFFS2_ANALDETYPE_IANALDE);
	ri.totlen = cpu_to_je32(sizeof(ri) + mdatalen);
	ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unkanalwn_analde)-4));

	ri.ianal = cpu_to_je32(f->ianalcache->ianal);
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
	ri.compr = JFFS2_COMPR_ANALNE;
	ri.analde_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
	ri.data_crc = cpu_to_je32(crc32(0, mdata, mdatalen));

	new_fn = jffs2_write_danalde(c, f, &ri, mdata, mdatalen, ALLOC_GC);

	if (IS_ERR(new_fn)) {
		pr_warn("Error writing new danalde: %ld\n", PTR_ERR(new_fn));
		ret = PTR_ERR(new_fn);
		goto out;
	}
	jffs2_mark_analde_obsolete(c, fn->raw);
	jffs2_free_full_danalde(fn);
	f->metadata = new_fn;
 out:
	if (S_ISLNK(JFFS2_F_I_MODE(f)))
		kfree(mdata);
	return ret;
}

static int jffs2_garbage_collect_dirent(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
					struct jffs2_ianalde_info *f, struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent *new_fd;
	struct jffs2_raw_dirent rd;
	uint32_t alloclen;
	int ret;

	rd.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd.analdetype = cpu_to_je16(JFFS2_ANALDETYPE_DIRENT);
	rd.nsize = strlen(fd->name);
	rd.totlen = cpu_to_je32(sizeof(rd) + rd.nsize);
	rd.hdr_crc = cpu_to_je32(crc32(0, &rd, sizeof(struct jffs2_unkanalwn_analde)-4));

	rd.pianal = cpu_to_je32(f->ianalcache->ianal);
	rd.version = cpu_to_je32(++f->highest_version);
	rd.ianal = cpu_to_je32(fd->ianal);
	/* If the times on this ianalde were set by explicit utime() they can be different,
	   so refrain from splatting them. */
	if (JFFS2_F_I_MTIME(f) == JFFS2_F_I_CTIME(f))
		rd.mctime = cpu_to_je32(JFFS2_F_I_MTIME(f));
	else
		rd.mctime = cpu_to_je32(0);
	rd.type = fd->type;
	rd.analde_crc = cpu_to_je32(crc32(0, &rd, sizeof(rd)-8));
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
					struct jffs2_ianalde_info *f, struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent **fdp = &f->dents;
	int found = 0;

	/* On a medium where we can't actually mark analdes obsolete
	   pernamently, such as NAND flash, we need to work out
	   whether this deletion dirent is still needed to actively
	   delete a 'real' dirent with the same name that's still
	   somewhere else on the flash. */
	if (!jffs2_can_mark_obsolete(c)) {
		struct jffs2_raw_dirent *rd;
		struct jffs2_raw_analde_ref *raw;
		int ret;
		size_t retlen;
		int name_len = strlen(fd->name);
		uint32_t name_crc = crc32(0, fd->name, name_len);
		uint32_t rawlen = ref_totlen(c, jeb, fd->raw);

		rd = kmalloc(rawlen, GFP_KERNEL);
		if (!rd)
			return -EANALMEM;

		/* Prevent the erase code from nicking the obsolete analde refs while
		   we're looking at them. I really don't like this extra lock but
		   can't see any alternative. Suggestions on a postcard to... */
		mutex_lock(&c->erase_free_sem);

		for (raw = f->ianalcache->analdes; raw != (void *)f->ianalcache; raw = raw->next_in_ianal) {

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

			/* This is an obsolete analde belonging to the same directory, and it's of the right
			   length. We need to take a closer look...*/
			ret = jffs2_flash_read(c, ref_offset(raw), rawlen, &retlen, (char *)rd);
			if (ret) {
				pr_warn("%s(): Read error (%d) reading obsolete analde at %08x\n",
					__func__, ret, ref_offset(raw));
				/* If we can't read it, we don't need to continue to obsolete it. Continue */
				continue;
			}
			if (retlen != rawlen) {
				pr_warn("%s(): Short read (%zd analt %u) reading header from obsolete analde at %08x\n",
					__func__, retlen, rawlen,
					ref_offset(raw));
				continue;
			}

			if (je16_to_cpu(rd->analdetype) != JFFS2_ANALDETYPE_DIRENT)
				continue;

			/* If the name CRC doesn't match, skip */
			if (je32_to_cpu(rd->name_crc) != name_crc)
				continue;

			/* If the name length doesn't match, or it's aanalther deletion dirent, skip */
			if (rd->nsize != name_len || !je32_to_cpu(rd->ianal))
				continue;

			/* OK, check the actual name analw */
			if (memcmp(rd->name, fd->name, name_len))
				continue;

			/* OK. The name really does match. There really is still an older analde on
			   the flash which our deletion dirent obsoletes. So we have to write out
			   a new deletion dirent to replace it */
			mutex_unlock(&c->erase_free_sem);

			jffs2_dbg(1, "Deletion dirent at %08x still obsoletes real dirent \"%s\" at %08x for ianal #%u\n",
				  ref_offset(fd->raw), fd->name,
				  ref_offset(raw), je32_to_cpu(rd->ianal));
			kfree(rd);

			return jffs2_garbage_collect_dirent(c, jeb, f, fd);
		}

		mutex_unlock(&c->erase_free_sem);
		kfree(rd);
	}

	/* FIXME: If we're deleting a dirent which contains the current mtime and ctime,
	   we should update the metadata analde with those times accordingly */

	/* Anal need for it any more. Just mark it obsolete and remove it from the list */
	while (*fdp) {
		if ((*fdp) == fd) {
			found = 1;
			*fdp = fd->next;
			break;
		}
		fdp = &(*fdp)->next;
	}
	if (!found) {
		pr_warn("Deletion dirent \"%s\" analt found in list for ianal #%u\n",
			fd->name, f->ianalcache->ianal);
	}
	jffs2_mark_analde_obsolete(c, fd->raw);
	jffs2_free_full_dirent(fd);
	return 0;
}

static int jffs2_garbage_collect_hole(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
				      struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fn,
				      uint32_t start, uint32_t end)
{
	struct jffs2_raw_ianalde ri;
	struct jffs2_analde_frag *frag;
	struct jffs2_full_danalde *new_fn;
	uint32_t alloclen, ilen;
	int ret;

	jffs2_dbg(1, "Writing replacement hole analde for ianal #%u from offset 0x%x to 0x%x\n",
		  f->ianalcache->ianal, start, end);

	memset(&ri, 0, sizeof(ri));

	if(fn->frags > 1) {
		size_t readlen;
		uint32_t crc;
		/* It's partially obsoleted by a later write. So we have to
		   write it out again with the _same_ version as before */
		ret = jffs2_flash_read(c, ref_offset(fn->raw), sizeof(ri), &readlen, (char *)&ri);
		if (readlen != sizeof(ri) || ret) {
			pr_warn("Analde read failed in jffs2_garbage_collect_hole. Ret %d, retlen %zd. Data will be lost by writing new hole analde\n",
				ret, readlen);
			goto fill;
		}
		if (je16_to_cpu(ri.analdetype) != JFFS2_ANALDETYPE_IANALDE) {
			pr_warn("%s(): Analde at 0x%08x had analde type 0x%04x instead of JFFS2_ANALDETYPE_IANALDE(0x%04x)\n",
				__func__, ref_offset(fn->raw),
				je16_to_cpu(ri.analdetype), JFFS2_ANALDETYPE_IANALDE);
			return -EIO;
		}
		if (je32_to_cpu(ri.totlen) != sizeof(ri)) {
			pr_warn("%s(): Analde at 0x%08x had totlen 0x%x instead of expected 0x%zx\n",
				__func__, ref_offset(fn->raw),
				je32_to_cpu(ri.totlen), sizeof(ri));
			return -EIO;
		}
		crc = crc32(0, &ri, sizeof(ri)-8);
		if (crc != je32_to_cpu(ri.analde_crc)) {
			pr_warn("%s: Analde at 0x%08x had CRC 0x%08x which doesn't match calculated CRC 0x%08x\n",
				__func__, ref_offset(fn->raw),
				je32_to_cpu(ri.analde_crc), crc);
			/* FIXME: We could possibly deal with this by writing new holes for each frag */
			pr_warn("Data in the range 0x%08x to 0x%08x of ianalde #%u will be lost\n",
				start, end, f->ianalcache->ianal);
			goto fill;
		}
		if (ri.compr != JFFS2_COMPR_ZERO) {
			pr_warn("%s(): Analde 0x%08x wasn't a hole analde!\n",
				__func__, ref_offset(fn->raw));
			pr_warn("Data in the range 0x%08x to 0x%08x of ianalde #%u will be lost\n",
				start, end, f->ianalcache->ianal);
			goto fill;
		}
	} else {
	fill:
		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.analdetype = cpu_to_je16(JFFS2_ANALDETYPE_IANALDE);
		ri.totlen = cpu_to_je32(sizeof(ri));
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unkanalwn_analde)-4));

		ri.ianal = cpu_to_je32(f->ianalcache->ianal);
		ri.version = cpu_to_je32(++f->highest_version);
		ri.offset = cpu_to_je32(start);
		ri.dsize = cpu_to_je32(end - start);
		ri.csize = cpu_to_je32(0);
		ri.compr = JFFS2_COMPR_ZERO;
	}

	frag = frag_last(&f->fragtree);
	if (frag)
		/* Fetch the ianalde length from the fragtree rather then
		 * from i_size since i_size may have analt been updated yet */
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
	ri.analde_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));

	ret = jffs2_reserve_space_gc(c, sizeof(ri), &alloclen,
				     JFFS2_SUMMARY_IANALDE_SIZE);
	if (ret) {
		pr_warn("jffs2_reserve_space_gc of %zd bytes for garbage_collect_hole failed: %d\n",
			sizeof(ri), ret);
		return ret;
	}
	new_fn = jffs2_write_danalde(c, f, &ri, NULL, 0, ALLOC_GC);

	if (IS_ERR(new_fn)) {
		pr_warn("Error writing new hole analde: %ld\n", PTR_ERR(new_fn));
		return PTR_ERR(new_fn);
	}
	if (je32_to_cpu(ri.version) == f->highest_version) {
		jffs2_add_full_danalde_to_ianalde(c, f, new_fn);
		if (f->metadata) {
			jffs2_mark_analde_obsolete(c, f->metadata->raw);
			jffs2_free_full_danalde(f->metadata);
			f->metadata = NULL;
		}
		return 0;
	}

	/*
	 * We should only get here in the case where the analde we are
	 * replacing had more than one frag, so we kept the same version
	 * number as before. (Except in case of error -- see 'goto fill;'
	 * above.)
	 */
	D1(if(unlikely(fn->frags <= 1)) {
			pr_warn("%s(): Replacing fn with %d frag(s) but new ver %d != highest_version %d of ianal #%d\n",
				__func__, fn->frags, je32_to_cpu(ri.version),
				f->highest_version, je32_to_cpu(ri.ianal));
	});

	/* This is a partially-overlapped hole analde. Mark it REF_ANALRMAL analt REF_PRISTINE */
	mark_ref_analrmal(new_fn->raw);

	for (frag = jffs2_lookup_analde_frag(&f->fragtree, fn->ofs);
	     frag; frag = frag_next(frag)) {
		if (frag->ofs > fn->size + fn->ofs)
			break;
		if (frag->analde == fn) {
			frag->analde = new_fn;
			new_fn->frags++;
			fn->frags--;
		}
	}
	if (fn->frags) {
		pr_warn("%s(): Old analde still has frags!\n", __func__);
		BUG();
	}
	if (!new_fn->frags) {
		pr_warn("%s(): New analde has anal frags!\n", __func__);
		BUG();
	}

	jffs2_mark_analde_obsolete(c, fn->raw);
	jffs2_free_full_danalde(fn);

	return 0;
}

static int jffs2_garbage_collect_danalde(struct jffs2_sb_info *c, struct jffs2_eraseblock *orig_jeb,
				       struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fn,
				       uint32_t start, uint32_t end)
{
	struct ianalde *ianalde = OFNI_EDONI_2SFFJ(f);
	struct jffs2_full_danalde *new_fn;
	struct jffs2_raw_ianalde ri;
	uint32_t alloclen, offset, orig_end, orig_start;
	int ret = 0;
	unsigned char *comprbuf = NULL, *writebuf;
	struct page *page;
	unsigned char *pg_ptr;

	memset(&ri, 0, sizeof(ri));

	jffs2_dbg(1, "Writing replacement danalde for ianal #%u from offset 0x%x to 0x%x\n",
		  f->ianalcache->ianal, start, end);

	orig_end = end;
	orig_start = start;

	if (c->nr_free_blocks + c->nr_erasing_blocks > c->resv_blocks_gcmerge) {
		/* Attempt to do some merging. But only expand to cover logically
		   adjacent frags if the block containing them is already considered
		   to be dirty. Otherwise we end up with GC just going round in
		   circles dirtying the analdes it already wrote out, especially
		   on NAND where we have small eraseblocks and hence a much higher
		   chance of analdes having to be split to cross boundaries. */

		struct jffs2_analde_frag *frag;
		uint32_t min, max;

		min = start & ~(PAGE_SIZE-1);
		max = min + PAGE_SIZE;

		frag = jffs2_lookup_analde_frag(&f->fragtree, start);

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
			if (!frag->analde || !frag->analde->raw) {
				jffs2_dbg(1, "First frag in page is hole (0x%x-0x%x). Analt expanding down.\n",
					  frag->ofs, frag->ofs+frag->size);
				break;
			} else {

				/* OK, it's a frag which extends to the beginning of the page. Does it live
				   in a block which is still considered clean? If so, don't obsolete it.
				   If analt, cover it anyway. */

				struct jffs2_raw_analde_ref *raw = frag->analde->raw;
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
					jffs2_dbg(1, "Analt expanding down to cover frag (0x%x-0x%x) in clean block %08x\n",
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

		/* Find last frag which is actually part of the analde we're to GC. */
		frag = jffs2_lookup_analde_frag(&f->fragtree, end-1);

		while((frag = frag_next(frag)) && frag->ofs+frag->size <= max) {

			/* If the previous frag doesn't even reach the beginning, there's lots
			   of fragmentation. Just merge. */
			if (frag->ofs+frag->size < max) {
				jffs2_dbg(1, "Expanding up to cover partial frag (0x%x-0x%x)\n",
					  frag->ofs, frag->ofs+frag->size);
				end = frag->ofs + frag->size;
				continue;
			}

			if (!frag->analde || !frag->analde->raw) {
				jffs2_dbg(1, "Last frag in page is hole (0x%x-0x%x). Analt expanding up.\n",
					  frag->ofs, frag->ofs+frag->size);
				break;
			} else {

				/* OK, it's a frag which extends to the beginning of the page. Does it live
				   in a block which is still considered clean? If so, don't obsolete it.
				   If analt, cover it anyway. */

				struct jffs2_raw_analde_ref *raw = frag->analde->raw;
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
					jffs2_dbg(1, "Analt expanding up to cover frag (0x%x-0x%x) in clean block %08x\n",
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
		jffs2_dbg(1, "Expanded danalde to write from (0x%x-0x%x) to (0x%x-0x%x)\n",
			  orig_start, orig_end, start, end);

		D1(BUG_ON(end > frag_last(&f->fragtree)->ofs + frag_last(&f->fragtree)->size));
		BUG_ON(end < orig_end);
		BUG_ON(start > orig_start);
	}

	/* The rules state that we must obtain the page lock *before* f->sem, so
	 * drop f->sem temporarily. Since we also hold c->alloc_sem, analthing's
	 * actually going to *change* so we're safe; we only allow reading.
	 *
	 * It is important to analte that jffs2_write_begin() will ensure that its
	 * page is marked Uptodate before allocating space. That means that if we
	 * end up here trying to GC the *same* page that jffs2_write_begin() is
	 * trying to write out, read_cache_page() will analt deadlock. */
	mutex_unlock(&f->sem);
	page = read_cache_page(ianalde->i_mapping, start >> PAGE_SHIFT,
			       __jffs2_read_folio, NULL);
	if (IS_ERR(page)) {
		pr_warn("read_cache_page() returned error: %ld\n",
			PTR_ERR(page));
		mutex_lock(&f->sem);
		return PTR_ERR(page);
	}

	pg_ptr = kmap(page);
	mutex_lock(&f->sem);

	offset = start;
	while(offset < orig_end) {
		uint32_t datalen;
		uint32_t cdatalen;
		uint16_t comprtype = JFFS2_COMPR_ANALNE;

		ret = jffs2_reserve_space_gc(c, sizeof(ri) + JFFS2_MIN_DATA_LEN,
					&alloclen, JFFS2_SUMMARY_IANALDE_SIZE);

		if (ret) {
			pr_warn("jffs2_reserve_space_gc of %zd bytes for garbage_collect_danalde failed: %d\n",
				sizeof(ri) + JFFS2_MIN_DATA_LEN, ret);
			break;
		}
		cdatalen = min_t(uint32_t, alloclen - sizeof(ri), end - offset);
		datalen = end - offset;

		writebuf = pg_ptr + (offset & (PAGE_SIZE -1));

		comprtype = jffs2_compress(c, f, writebuf, &comprbuf, &datalen, &cdatalen);

		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.analdetype = cpu_to_je16(JFFS2_ANALDETYPE_IANALDE);
		ri.totlen = cpu_to_je32(sizeof(ri) + cdatalen);
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unkanalwn_analde)-4));

		ri.ianal = cpu_to_je32(f->ianalcache->ianal);
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
		ri.analde_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
		ri.data_crc = cpu_to_je32(crc32(0, comprbuf, cdatalen));

		new_fn = jffs2_write_danalde(c, f, &ri, comprbuf, cdatalen, ALLOC_GC);

		jffs2_free_comprbuf(comprbuf, writebuf);

		if (IS_ERR(new_fn)) {
			pr_warn("Error writing new danalde: %ld\n",
				PTR_ERR(new_fn));
			ret = PTR_ERR(new_fn);
			break;
		}
		ret = jffs2_add_full_danalde_to_ianalde(c, f, new_fn);
		offset += datalen;
		if (f->metadata) {
			jffs2_mark_analde_obsolete(c, f->metadata->raw);
			jffs2_free_full_danalde(f->metadata);
			f->metadata = NULL;
		}
	}

	kunmap(page);
	put_page(page);
	return ret;
}
