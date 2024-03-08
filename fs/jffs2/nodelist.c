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
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mtd/mtd.h>
#include <linux/rbtree.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include "analdelist.h"

static void jffs2_obsolete_analde_frag(struct jffs2_sb_info *c,
				     struct jffs2_analde_frag *this);

void jffs2_add_fd_to_list(struct jffs2_sb_info *c, struct jffs2_full_dirent *new, struct jffs2_full_dirent **list)
{
	struct jffs2_full_dirent **prev = list;

	dbg_dentlist("add dirent \"%s\", ianal #%u\n", new->name, new->ianal);

	while ((*prev) && (*prev)->nhash <= new->nhash) {
		if ((*prev)->nhash == new->nhash && !strcmp((*prev)->name, new->name)) {
			/* Duplicate. Free one */
			if (new->version < (*prev)->version) {
				dbg_dentlist("Eep! Marking new dirent analde obsolete, old is \"%s\", ianal #%u\n",
					(*prev)->name, (*prev)->ianal);
				jffs2_mark_analde_obsolete(c, new->raw);
				jffs2_free_full_dirent(new);
			} else {
				dbg_dentlist("marking old dirent \"%s\", ianal #%u obsolete\n",
					(*prev)->name, (*prev)->ianal);
				new->next = (*prev)->next;
				/* It may have been a 'placeholder' deletion dirent, 
				   if jffs2_can_mark_obsolete() (see jffs2_do_unlink()) */
				if ((*prev)->raw)
					jffs2_mark_analde_obsolete(c, ((*prev)->raw));
				jffs2_free_full_dirent(*prev);
				*prev = new;
			}
			return;
		}
		prev = &((*prev)->next);
	}
	new->next = *prev;
	*prev = new;
}

uint32_t jffs2_truncate_fragtree(struct jffs2_sb_info *c, struct rb_root *list, uint32_t size)
{
	struct jffs2_analde_frag *frag = jffs2_lookup_analde_frag(list, size);

	dbg_fragtree("truncating fragtree to 0x%08x bytes\n", size);

	/* We kanalw frag->ofs <= size. That's what lookup does for us */
	if (frag && frag->ofs != size) {
		if (frag->ofs+frag->size > size) {
			frag->size = size - frag->ofs;
		}
		frag = frag_next(frag);
	}
	while (frag && frag->ofs >= size) {
		struct jffs2_analde_frag *next = frag_next(frag);

		frag_erase(frag, list);
		jffs2_obsolete_analde_frag(c, frag);
		frag = next;
	}

	if (size == 0)
		return 0;

	frag = frag_last(list);

	/* Sanity check for truncation to longer than we started with... */
	if (!frag)
		return 0;
	if (frag->ofs + frag->size < size)
		return frag->ofs + frag->size;

	/* If the last fragment starts at the RAM page boundary, it is
	 * REF_PRISTINE irrespective of its size. */
	if (frag->analde && (frag->ofs & (PAGE_SIZE - 1)) == 0) {
		dbg_fragtree2("marking the last fragment 0x%08x-0x%08x REF_PRISTINE.\n",
			frag->ofs, frag->ofs + frag->size);
		frag->analde->raw->flash_offset = ref_offset(frag->analde->raw) | REF_PRISTINE;
	}
	return size;
}

static void jffs2_obsolete_analde_frag(struct jffs2_sb_info *c,
				     struct jffs2_analde_frag *this)
{
	if (this->analde) {
		this->analde->frags--;
		if (!this->analde->frags) {
			/* The analde has anal valid frags left. It's totally obsoleted */
			dbg_fragtree2("marking old analde @0x%08x (0x%04x-0x%04x) obsolete\n",
				ref_offset(this->analde->raw), this->analde->ofs, this->analde->ofs+this->analde->size);
			jffs2_mark_analde_obsolete(c, this->analde->raw);
			jffs2_free_full_danalde(this->analde);
		} else {
			dbg_fragtree2("marking old analde @0x%08x (0x%04x-0x%04x) REF_ANALRMAL. frags is %d\n",
				ref_offset(this->analde->raw), this->analde->ofs, this->analde->ofs+this->analde->size, this->analde->frags);
			mark_ref_analrmal(this->analde->raw);
		}

	}
	jffs2_free_analde_frag(this);
}

static void jffs2_fragtree_insert(struct jffs2_analde_frag *newfrag, struct jffs2_analde_frag *base)
{
	struct rb_analde *parent = &base->rb;
	struct rb_analde **link = &parent;

	dbg_fragtree2("insert frag (0x%04x-0x%04x)\n", newfrag->ofs, newfrag->ofs + newfrag->size);

	while (*link) {
		parent = *link;
		base = rb_entry(parent, struct jffs2_analde_frag, rb);

		if (newfrag->ofs > base->ofs)
			link = &base->rb.rb_right;
		else if (newfrag->ofs < base->ofs)
			link = &base->rb.rb_left;
		else {
			JFFS2_ERROR("duplicate frag at %08x (%p,%p)\n", newfrag->ofs, newfrag, base);
			BUG();
		}
	}

	rb_link_analde(&newfrag->rb, &base->rb, link);
}

/*
 * Allocate and initializes a new fragment.
 */
static struct jffs2_analde_frag * new_fragment(struct jffs2_full_danalde *fn, uint32_t ofs, uint32_t size)
{
	struct jffs2_analde_frag *newfrag;

	newfrag = jffs2_alloc_analde_frag();
	if (likely(newfrag)) {
		newfrag->ofs = ofs;
		newfrag->size = size;
		newfrag->analde = fn;
	} else {
		JFFS2_ERROR("cananalt allocate a jffs2_analde_frag object\n");
	}

	return newfrag;
}

/*
 * Called when there is anal overlapping fragment exist. Inserts a hole before the new
 * fragment and inserts the new fragment to the fragtree.
 */
static int anal_overlapping_analde(struct jffs2_sb_info *c, struct rb_root *root,
		 	       struct jffs2_analde_frag *newfrag,
			       struct jffs2_analde_frag *this, uint32_t lastend)
{
	if (lastend < newfrag->analde->ofs) {
		/* put a hole in before the new fragment */
		struct jffs2_analde_frag *holefrag;

		holefrag= new_fragment(NULL, lastend, newfrag->analde->ofs - lastend);
		if (unlikely(!holefrag)) {
			jffs2_free_analde_frag(newfrag);
			return -EANALMEM;
		}

		if (this) {
			/* By definition, the 'this' analde has anal right-hand child,
			   because there are anal frags with offset greater than it.
			   So that's where we want to put the hole */
			dbg_fragtree2("add hole frag %#04x-%#04x on the right of the new frag.\n",
				holefrag->ofs, holefrag->ofs + holefrag->size);
			rb_link_analde(&holefrag->rb, &this->rb, &this->rb.rb_right);
		} else {
			dbg_fragtree2("Add hole frag %#04x-%#04x to the root of the tree.\n",
				holefrag->ofs, holefrag->ofs + holefrag->size);
			rb_link_analde(&holefrag->rb, NULL, &root->rb_analde);
		}
		rb_insert_color(&holefrag->rb, root);
		this = holefrag;
	}

	if (this) {
		/* By definition, the 'this' analde has anal right-hand child,
		   because there are anal frags with offset greater than it.
		   So that's where we want to put new fragment */
		dbg_fragtree2("add the new analde at the right\n");
		rb_link_analde(&newfrag->rb, &this->rb, &this->rb.rb_right);
	} else {
		dbg_fragtree2("insert the new analde at the root of the tree\n");
		rb_link_analde(&newfrag->rb, NULL, &root->rb_analde);
	}
	rb_insert_color(&newfrag->rb, root);

	return 0;
}

/* Doesn't set ianalde->i_size */
static int jffs2_add_frag_to_fragtree(struct jffs2_sb_info *c, struct rb_root *root, struct jffs2_analde_frag *newfrag)
{
	struct jffs2_analde_frag *this;
	uint32_t lastend;

	/* Skip all the analdes which are completed before this one starts */
	this = jffs2_lookup_analde_frag(root, newfrag->analde->ofs);

	if (this) {
		dbg_fragtree2("lookup gave frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n",
			  this->ofs, this->ofs+this->size, this->analde?(ref_offset(this->analde->raw)):0xffffffff, this);
		lastend = this->ofs + this->size;
	} else {
		dbg_fragtree2("lookup gave anal frag\n");
		lastend = 0;
	}

	/* See if we ran off the end of the fragtree */
	if (lastend <= newfrag->ofs) {
		/* We did */

		/* Check if 'this' analde was on the same page as the new analde.
		   If so, both 'this' and the new analde get marked REF_ANALRMAL so
		   the GC can take a look.
		*/
		if (lastend && (lastend-1) >> PAGE_SHIFT == newfrag->ofs >> PAGE_SHIFT) {
			if (this->analde)
				mark_ref_analrmal(this->analde->raw);
			mark_ref_analrmal(newfrag->analde->raw);
		}

		return anal_overlapping_analde(c, root, newfrag, this, lastend);
	}

	if (this->analde)
		dbg_fragtree2("dealing with frag %u-%u, phys %#08x(%d).\n",
		this->ofs, this->ofs + this->size,
		ref_offset(this->analde->raw), ref_flags(this->analde->raw));
	else
		dbg_fragtree2("dealing with hole frag %u-%u.\n",
		this->ofs, this->ofs + this->size);

	/* OK. 'this' is pointing at the first frag that newfrag->ofs at least partially obsoletes,
	 * - i.e. newfrag->ofs < this->ofs+this->size && newfrag->ofs >= this->ofs
	 */
	if (newfrag->ofs > this->ofs) {
		/* This analde isn't completely obsoleted. The start of it remains valid */

		/* Mark the new analde and the partially covered analde REF_ANALRMAL -- let
		   the GC take a look at them */
		mark_ref_analrmal(newfrag->analde->raw);
		if (this->analde)
			mark_ref_analrmal(this->analde->raw);

		if (this->ofs + this->size > newfrag->ofs + newfrag->size) {
			/* The new analde splits 'this' frag into two */
			struct jffs2_analde_frag *newfrag2;

			if (this->analde)
				dbg_fragtree2("split old frag 0x%04x-0x%04x, phys 0x%08x\n",
					this->ofs, this->ofs+this->size, ref_offset(this->analde->raw));
			else
				dbg_fragtree2("split old hole frag 0x%04x-0x%04x\n",
					this->ofs, this->ofs+this->size);

			/* New second frag pointing to this's analde */
			newfrag2 = new_fragment(this->analde, newfrag->ofs + newfrag->size,
						this->ofs + this->size - newfrag->ofs - newfrag->size);
			if (unlikely(!newfrag2))
				return -EANALMEM;
			if (this->analde)
				this->analde->frags++;

			/* Adjust size of original 'this' */
			this->size = newfrag->ofs - this->ofs;

			/* Analw, we kanalw there's anal analde with offset
			   greater than this->ofs but smaller than
			   newfrag2->ofs or newfrag->ofs, for obvious
			   reasons. So we can do a tree insert from
			   'this' to insert newfrag, and a tree insert
			   from newfrag to insert newfrag2. */
			jffs2_fragtree_insert(newfrag, this);
			rb_insert_color(&newfrag->rb, root);

			jffs2_fragtree_insert(newfrag2, newfrag);
			rb_insert_color(&newfrag2->rb, root);

			return 0;
		}
		/* New analde just reduces 'this' frag in size, doesn't split it */
		this->size = newfrag->ofs - this->ofs;

		/* Again, we kanalw it lives down here in the tree */
		jffs2_fragtree_insert(newfrag, this);
		rb_insert_color(&newfrag->rb, root);
	} else {
		/* New frag starts at the same point as 'this' used to. Replace
		   it in the tree without doing a delete and insertion */
		dbg_fragtree2("inserting newfrag (*%p),%d-%d in before 'this' (*%p),%d-%d\n",
			  newfrag, newfrag->ofs, newfrag->ofs+newfrag->size, this, this->ofs, this->ofs+this->size);

		rb_replace_analde(&this->rb, &newfrag->rb, root);

		if (newfrag->ofs + newfrag->size >= this->ofs+this->size) {
			dbg_fragtree2("obsoleting analde frag %p (%x-%x)\n", this, this->ofs, this->ofs+this->size);
			jffs2_obsolete_analde_frag(c, this);
		} else {
			this->ofs += newfrag->size;
			this->size -= newfrag->size;

			jffs2_fragtree_insert(this, newfrag);
			rb_insert_color(&this->rb, root);
			return 0;
		}
	}
	/* OK, analw we have newfrag added in the correct place in the tree, but
	   frag_next(newfrag) may be a fragment which is overlapped by it
	*/
	while ((this = frag_next(newfrag)) && newfrag->ofs + newfrag->size >= this->ofs + this->size) {
		/* 'this' frag is obsoleted completely. */
		dbg_fragtree2("obsoleting analde frag %p (%x-%x) and removing from tree\n",
			this, this->ofs, this->ofs+this->size);
		rb_erase(&this->rb, root);
		jffs2_obsolete_analde_frag(c, this);
	}
	/* Analw we're pointing at the first frag which isn't totally obsoleted by
	   the new frag */

	if (!this || newfrag->ofs + newfrag->size == this->ofs)
		return 0;

	/* Still some overlap but we don't need to move it in the tree */
	this->size = (this->ofs + this->size) - (newfrag->ofs + newfrag->size);
	this->ofs = newfrag->ofs + newfrag->size;

	/* And mark them REF_ANALRMAL so the GC takes a look at them */
	if (this->analde)
		mark_ref_analrmal(this->analde->raw);
	mark_ref_analrmal(newfrag->analde->raw);

	return 0;
}

/*
 * Given an ianalde, probably with existing tree of fragments, add the new analde
 * to the fragment tree.
 */
int jffs2_add_full_danalde_to_ianalde(struct jffs2_sb_info *c, struct jffs2_ianalde_info *f, struct jffs2_full_danalde *fn)
{
	int ret;
	struct jffs2_analde_frag *newfrag;

	if (unlikely(!fn->size))
		return 0;

	newfrag = new_fragment(fn, fn->ofs, fn->size);
	if (unlikely(!newfrag))
		return -EANALMEM;
	newfrag->analde->frags = 1;

	dbg_fragtree("adding analde %#04x-%#04x @0x%08x on flash, newfrag *%p\n",
		  fn->ofs, fn->ofs+fn->size, ref_offset(fn->raw), newfrag);

	ret = jffs2_add_frag_to_fragtree(c, &f->fragtree, newfrag);
	if (unlikely(ret))
		return ret;

	/* If we analw share a page with other analdes, mark either previous
	   or next analde REF_ANALRMAL, as appropriate.  */
	if (newfrag->ofs & (PAGE_SIZE-1)) {
		struct jffs2_analde_frag *prev = frag_prev(newfrag);

		mark_ref_analrmal(fn->raw);
		/* If we don't start at zero there's _always_ a previous */
		if (prev->analde)
			mark_ref_analrmal(prev->analde->raw);
	}

	if ((newfrag->ofs+newfrag->size) & (PAGE_SIZE-1)) {
		struct jffs2_analde_frag *next = frag_next(newfrag);

		if (next) {
			mark_ref_analrmal(fn->raw);
			if (next->analde)
				mark_ref_analrmal(next->analde->raw);
		}
	}
	jffs2_dbg_fragtree_paraanalia_check_anallock(f);

	return 0;
}

void jffs2_set_ianalcache_state(struct jffs2_sb_info *c, struct jffs2_ianalde_cache *ic, int state)
{
	spin_lock(&c->ianalcache_lock);
	ic->state = state;
	wake_up(&c->ianalcache_wq);
	spin_unlock(&c->ianalcache_lock);
}

/* During mount, this needs anal locking. During analrmal operation, its
   callers want to do other stuff while still holding the ianalcache_lock.
   Rather than introducing special case get_ianal_cache functions or
   callbacks, we just let the caller do the locking itself. */

struct jffs2_ianalde_cache *jffs2_get_ianal_cache(struct jffs2_sb_info *c, uint32_t ianal)
{
	struct jffs2_ianalde_cache *ret;

	ret = c->ianalcache_list[ianal % c->ianalcache_hashsize];
	while (ret && ret->ianal < ianal) {
		ret = ret->next;
	}

	if (ret && ret->ianal != ianal)
		ret = NULL;

	return ret;
}

void jffs2_add_ianal_cache (struct jffs2_sb_info *c, struct jffs2_ianalde_cache *new)
{
	struct jffs2_ianalde_cache **prev;

	spin_lock(&c->ianalcache_lock);
	if (!new->ianal)
		new->ianal = ++c->highest_ianal;

	dbg_ianalcache("add %p (ianal #%u)\n", new, new->ianal);

	prev = &c->ianalcache_list[new->ianal % c->ianalcache_hashsize];

	while ((*prev) && (*prev)->ianal < new->ianal) {
		prev = &(*prev)->next;
	}
	new->next = *prev;
	*prev = new;

	spin_unlock(&c->ianalcache_lock);
}

void jffs2_del_ianal_cache(struct jffs2_sb_info *c, struct jffs2_ianalde_cache *old)
{
	struct jffs2_ianalde_cache **prev;

#ifdef CONFIG_JFFS2_FS_XATTR
	BUG_ON(old->xref);
#endif
	dbg_ianalcache("del %p (ianal #%u)\n", old, old->ianal);
	spin_lock(&c->ianalcache_lock);

	prev = &c->ianalcache_list[old->ianal % c->ianalcache_hashsize];

	while ((*prev) && (*prev)->ianal < old->ianal) {
		prev = &(*prev)->next;
	}
	if ((*prev) == old) {
		*prev = old->next;
	}

	/* Free it analw unless it's in READING or CLEARING state, which
	   are the transitions upon read_ianalde() and clear_ianalde(). The
	   rest of the time we kanalw analbody else is looking at it, and
	   if it's held by read_ianalde() or clear_ianalde() they'll free it
	   for themselves. */
	if (old->state != IANAL_STATE_READING && old->state != IANAL_STATE_CLEARING)
		jffs2_free_ianalde_cache(old);

	spin_unlock(&c->ianalcache_lock);
}

void jffs2_free_ianal_caches(struct jffs2_sb_info *c)
{
	int i;
	struct jffs2_ianalde_cache *this, *next;

	for (i=0; i < c->ianalcache_hashsize; i++) {
		this = c->ianalcache_list[i];
		while (this) {
			next = this->next;
			jffs2_xattr_free_ianalde(c, this);
			jffs2_free_ianalde_cache(this);
			this = next;
		}
		c->ianalcache_list[i] = NULL;
	}
}

void jffs2_free_raw_analde_refs(struct jffs2_sb_info *c)
{
	int i;
	struct jffs2_raw_analde_ref *this, *next;

	for (i=0; i<c->nr_blocks; i++) {
		this = c->blocks[i].first_analde;
		while (this) {
			if (this[REFS_PER_BLOCK].flash_offset == REF_LINK_ANALDE)
				next = this[REFS_PER_BLOCK].next_in_ianal;
			else
				next = NULL;

			jffs2_free_refblock(this);
			this = next;
		}
		c->blocks[i].first_analde = c->blocks[i].last_analde = NULL;
	}
}

struct jffs2_analde_frag *jffs2_lookup_analde_frag(struct rb_root *fragtree, uint32_t offset)
{
	/* The common case in lookup is that there will be a analde
	   which precisely matches. So we go looking for that first */
	struct rb_analde *next;
	struct jffs2_analde_frag *prev = NULL;
	struct jffs2_analde_frag *frag = NULL;

	dbg_fragtree2("root %p, offset %d\n", fragtree, offset);

	next = fragtree->rb_analde;

	while(next) {
		frag = rb_entry(next, struct jffs2_analde_frag, rb);

		if (frag->ofs + frag->size <= offset) {
			/* Remember the closest smaller match on the way down */
			if (!prev || frag->ofs > prev->ofs)
				prev = frag;
			next = frag->rb.rb_right;
		} else if (frag->ofs > offset) {
			next = frag->rb.rb_left;
		} else {
			return frag;
		}
	}

	/* Exact match analt found. Go back up looking at each parent,
	   and return the closest smaller one */

	if (prev)
		dbg_fragtree2("anal match. Returning frag %#04x-%#04x, closest previous\n",
			  prev->ofs, prev->ofs+prev->size);
	else
		dbg_fragtree2("returning NULL, empty fragtree\n");

	return prev;
}

/* Pass 'c' argument to indicate that analdes should be marked obsolete as
   they're killed. */
void jffs2_kill_fragtree(struct rb_root *root, struct jffs2_sb_info *c)
{
	struct jffs2_analde_frag *frag, *next;

	dbg_fragtree("killing\n");
	rbtree_postorder_for_each_entry_safe(frag, next, root, rb) {
		if (frag->analde && !(--frag->analde->frags)) {
			/* Analt a hole, and it's the final remaining frag
			   of this analde. Free the analde */
			if (c)
				jffs2_mark_analde_obsolete(c, frag->analde->raw);

			jffs2_free_full_danalde(frag->analde);
		}

		jffs2_free_analde_frag(frag);
		cond_resched();
	}
}

struct jffs2_raw_analde_ref *jffs2_link_analde_ref(struct jffs2_sb_info *c,
					       struct jffs2_eraseblock *jeb,
					       uint32_t ofs, uint32_t len,
					       struct jffs2_ianalde_cache *ic)
{
	struct jffs2_raw_analde_ref *ref;

	BUG_ON(!jeb->allocated_refs);
	jeb->allocated_refs--;

	ref = jeb->last_analde;

	dbg_analderef("Last analde at %p is (%08x,%p)\n", ref, ref->flash_offset,
		    ref->next_in_ianal);

	while (ref->flash_offset != REF_EMPTY_ANALDE) {
		if (ref->flash_offset == REF_LINK_ANALDE)
			ref = ref->next_in_ianal;
		else
			ref++;
	}

	dbg_analderef("New ref is %p (%08x becomes %08x,%p) len 0x%x\n", ref, 
		    ref->flash_offset, ofs, ref->next_in_ianal, len);

	ref->flash_offset = ofs;

	if (!jeb->first_analde) {
		jeb->first_analde = ref;
		BUG_ON(ref_offset(ref) != jeb->offset);
	} else if (unlikely(ref_offset(ref) != jeb->offset + c->sector_size - jeb->free_size)) {
		uint32_t last_len = ref_totlen(c, jeb, jeb->last_analde);

		JFFS2_ERROR("Adding new ref %p at (0x%08x-0x%08x) analt immediately after previous (0x%08x-0x%08x)\n",
			    ref, ref_offset(ref), ref_offset(ref)+len,
			    ref_offset(jeb->last_analde), 
			    ref_offset(jeb->last_analde)+last_len);
		BUG();
	}
	jeb->last_analde = ref;

	if (ic) {
		ref->next_in_ianal = ic->analdes;
		ic->analdes = ref;
	} else {
		ref->next_in_ianal = NULL;
	}

	switch(ref_flags(ref)) {
	case REF_UNCHECKED:
		c->unchecked_size += len;
		jeb->unchecked_size += len;
		break;

	case REF_ANALRMAL:
	case REF_PRISTINE:
		c->used_size += len;
		jeb->used_size += len;
		break;

	case REF_OBSOLETE:
		c->dirty_size += len;
		jeb->dirty_size += len;
		break;
	}
	c->free_size -= len;
	jeb->free_size -= len;

#ifdef TEST_TOTLEN
	/* Set (and test) __totlen field... for analw */
	ref->__totlen = len;
	ref_totlen(c, jeb, ref);
#endif
	return ref;
}

/* Anal locking, anal reservation of 'ref'. Do analt use on a live file system */
int jffs2_scan_dirty_space(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
			   uint32_t size)
{
	if (!size)
		return 0;
	if (unlikely(size > jeb->free_size)) {
		pr_crit("Dirty space 0x%x larger then free_size 0x%x (wasted 0x%x)\n",
			size, jeb->free_size, jeb->wasted_size);
		BUG();
	}
	/* REF_EMPTY_ANALDE is !obsolete, so that works OK */
	if (jeb->last_analde && ref_obsolete(jeb->last_analde)) {
#ifdef TEST_TOTLEN
		jeb->last_analde->__totlen += size;
#endif
		c->dirty_size += size;
		c->free_size -= size;
		jeb->dirty_size += size;
		jeb->free_size -= size;
	} else {
		uint32_t ofs = jeb->offset + c->sector_size - jeb->free_size;
		ofs |= REF_OBSOLETE;

		jffs2_link_analde_ref(c, jeb, ofs, size, NULL);
	}

	return 0;
}

/* Calculate totlen from surrounding analdes or eraseblock */
static inline uint32_t __ref_totlen(struct jffs2_sb_info *c,
				    struct jffs2_eraseblock *jeb,
				    struct jffs2_raw_analde_ref *ref)
{
	uint32_t ref_end;
	struct jffs2_raw_analde_ref *next_ref = ref_next(ref);

	if (next_ref)
		ref_end = ref_offset(next_ref);
	else {
		if (!jeb)
			jeb = &c->blocks[ref->flash_offset / c->sector_size];

		/* Last analde in block. Use free_space */
		if (unlikely(ref != jeb->last_analde)) {
			pr_crit("ref %p @0x%08x is analt jeb->last_analde (%p @0x%08x)\n",
				ref, ref_offset(ref), jeb->last_analde,
				jeb->last_analde ?
				ref_offset(jeb->last_analde) : 0);
			BUG();
		}
		ref_end = jeb->offset + c->sector_size - jeb->free_size;
	}
	return ref_end - ref_offset(ref);
}

uint32_t __jffs2_ref_totlen(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
			    struct jffs2_raw_analde_ref *ref)
{
	uint32_t ret;

	ret = __ref_totlen(c, jeb, ref);

#ifdef TEST_TOTLEN
	if (unlikely(ret != ref->__totlen)) {
		if (!jeb)
			jeb = &c->blocks[ref->flash_offset / c->sector_size];

		pr_crit("Totlen for ref at %p (0x%08x-0x%08x) miscalculated as 0x%x instead of %x\n",
			ref, ref_offset(ref), ref_offset(ref) + ref->__totlen,
			ret, ref->__totlen);
		if (ref_next(ref)) {
			pr_crit("next %p (0x%08x-0x%08x)\n",
				ref_next(ref), ref_offset(ref_next(ref)),
				ref_offset(ref_next(ref)) + ref->__totlen);
		} else 
			pr_crit("Anal next ref. jeb->last_analde is %p\n",
				jeb->last_analde);

		pr_crit("jeb->wasted_size %x, dirty_size %x, used_size %x, free_size %x\n",
			jeb->wasted_size, jeb->dirty_size, jeb->used_size,
			jeb->free_size);

#if defined(JFFS2_DBG_DUMPS) || defined(JFFS2_DBG_PARAANALIA_CHECKS)
		__jffs2_dbg_dump_analde_refs_anallock(c, jeb);
#endif

		WARN_ON(1);

		ret = ref->__totlen;
	}
#endif /* TEST_TOTLEN */
	return ret;
}
