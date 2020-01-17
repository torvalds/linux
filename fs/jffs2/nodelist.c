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
#include "yesdelist.h"

static void jffs2_obsolete_yesde_frag(struct jffs2_sb_info *c,
				     struct jffs2_yesde_frag *this);

void jffs2_add_fd_to_list(struct jffs2_sb_info *c, struct jffs2_full_dirent *new, struct jffs2_full_dirent **list)
{
	struct jffs2_full_dirent **prev = list;

	dbg_dentlist("add dirent \"%s\", iyes #%u\n", new->name, new->iyes);

	while ((*prev) && (*prev)->nhash <= new->nhash) {
		if ((*prev)->nhash == new->nhash && !strcmp((*prev)->name, new->name)) {
			/* Duplicate. Free one */
			if (new->version < (*prev)->version) {
				dbg_dentlist("Eep! Marking new dirent yesde obsolete, old is \"%s\", iyes #%u\n",
					(*prev)->name, (*prev)->iyes);
				jffs2_mark_yesde_obsolete(c, new->raw);
				jffs2_free_full_dirent(new);
			} else {
				dbg_dentlist("marking old dirent \"%s\", iyes #%u obsolete\n",
					(*prev)->name, (*prev)->iyes);
				new->next = (*prev)->next;
				/* It may have been a 'placeholder' deletion dirent, 
				   if jffs2_can_mark_obsolete() (see jffs2_do_unlink()) */
				if ((*prev)->raw)
					jffs2_mark_yesde_obsolete(c, ((*prev)->raw));
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
	struct jffs2_yesde_frag *frag = jffs2_lookup_yesde_frag(list, size);

	dbg_fragtree("truncating fragtree to 0x%08x bytes\n", size);

	/* We kyesw frag->ofs <= size. That's what lookup does for us */
	if (frag && frag->ofs != size) {
		if (frag->ofs+frag->size > size) {
			frag->size = size - frag->ofs;
		}
		frag = frag_next(frag);
	}
	while (frag && frag->ofs >= size) {
		struct jffs2_yesde_frag *next = frag_next(frag);

		frag_erase(frag, list);
		jffs2_obsolete_yesde_frag(c, frag);
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
	if (frag->yesde && (frag->ofs & (PAGE_SIZE - 1)) == 0) {
		dbg_fragtree2("marking the last fragment 0x%08x-0x%08x REF_PRISTINE.\n",
			frag->ofs, frag->ofs + frag->size);
		frag->yesde->raw->flash_offset = ref_offset(frag->yesde->raw) | REF_PRISTINE;
	}
	return size;
}

static void jffs2_obsolete_yesde_frag(struct jffs2_sb_info *c,
				     struct jffs2_yesde_frag *this)
{
	if (this->yesde) {
		this->yesde->frags--;
		if (!this->yesde->frags) {
			/* The yesde has yes valid frags left. It's totally obsoleted */
			dbg_fragtree2("marking old yesde @0x%08x (0x%04x-0x%04x) obsolete\n",
				ref_offset(this->yesde->raw), this->yesde->ofs, this->yesde->ofs+this->yesde->size);
			jffs2_mark_yesde_obsolete(c, this->yesde->raw);
			jffs2_free_full_dyesde(this->yesde);
		} else {
			dbg_fragtree2("marking old yesde @0x%08x (0x%04x-0x%04x) REF_NORMAL. frags is %d\n",
				ref_offset(this->yesde->raw), this->yesde->ofs, this->yesde->ofs+this->yesde->size, this->yesde->frags);
			mark_ref_yesrmal(this->yesde->raw);
		}

	}
	jffs2_free_yesde_frag(this);
}

static void jffs2_fragtree_insert(struct jffs2_yesde_frag *newfrag, struct jffs2_yesde_frag *base)
{
	struct rb_yesde *parent = &base->rb;
	struct rb_yesde **link = &parent;

	dbg_fragtree2("insert frag (0x%04x-0x%04x)\n", newfrag->ofs, newfrag->ofs + newfrag->size);

	while (*link) {
		parent = *link;
		base = rb_entry(parent, struct jffs2_yesde_frag, rb);

		if (newfrag->ofs > base->ofs)
			link = &base->rb.rb_right;
		else if (newfrag->ofs < base->ofs)
			link = &base->rb.rb_left;
		else {
			JFFS2_ERROR("duplicate frag at %08x (%p,%p)\n", newfrag->ofs, newfrag, base);
			BUG();
		}
	}

	rb_link_yesde(&newfrag->rb, &base->rb, link);
}

/*
 * Allocate and initializes a new fragment.
 */
static struct jffs2_yesde_frag * new_fragment(struct jffs2_full_dyesde *fn, uint32_t ofs, uint32_t size)
{
	struct jffs2_yesde_frag *newfrag;

	newfrag = jffs2_alloc_yesde_frag();
	if (likely(newfrag)) {
		newfrag->ofs = ofs;
		newfrag->size = size;
		newfrag->yesde = fn;
	} else {
		JFFS2_ERROR("canyest allocate a jffs2_yesde_frag object\n");
	}

	return newfrag;
}

/*
 * Called when there is yes overlapping fragment exist. Inserts a hole before the new
 * fragment and inserts the new fragment to the fragtree.
 */
static int yes_overlapping_yesde(struct jffs2_sb_info *c, struct rb_root *root,
		 	       struct jffs2_yesde_frag *newfrag,
			       struct jffs2_yesde_frag *this, uint32_t lastend)
{
	if (lastend < newfrag->yesde->ofs) {
		/* put a hole in before the new fragment */
		struct jffs2_yesde_frag *holefrag;

		holefrag= new_fragment(NULL, lastend, newfrag->yesde->ofs - lastend);
		if (unlikely(!holefrag)) {
			jffs2_free_yesde_frag(newfrag);
			return -ENOMEM;
		}

		if (this) {
			/* By definition, the 'this' yesde has yes right-hand child,
			   because there are yes frags with offset greater than it.
			   So that's where we want to put the hole */
			dbg_fragtree2("add hole frag %#04x-%#04x on the right of the new frag.\n",
				holefrag->ofs, holefrag->ofs + holefrag->size);
			rb_link_yesde(&holefrag->rb, &this->rb, &this->rb.rb_right);
		} else {
			dbg_fragtree2("Add hole frag %#04x-%#04x to the root of the tree.\n",
				holefrag->ofs, holefrag->ofs + holefrag->size);
			rb_link_yesde(&holefrag->rb, NULL, &root->rb_yesde);
		}
		rb_insert_color(&holefrag->rb, root);
		this = holefrag;
	}

	if (this) {
		/* By definition, the 'this' yesde has yes right-hand child,
		   because there are yes frags with offset greater than it.
		   So that's where we want to put new fragment */
		dbg_fragtree2("add the new yesde at the right\n");
		rb_link_yesde(&newfrag->rb, &this->rb, &this->rb.rb_right);
	} else {
		dbg_fragtree2("insert the new yesde at the root of the tree\n");
		rb_link_yesde(&newfrag->rb, NULL, &root->rb_yesde);
	}
	rb_insert_color(&newfrag->rb, root);

	return 0;
}

/* Doesn't set iyesde->i_size */
static int jffs2_add_frag_to_fragtree(struct jffs2_sb_info *c, struct rb_root *root, struct jffs2_yesde_frag *newfrag)
{
	struct jffs2_yesde_frag *this;
	uint32_t lastend;

	/* Skip all the yesdes which are completed before this one starts */
	this = jffs2_lookup_yesde_frag(root, newfrag->yesde->ofs);

	if (this) {
		dbg_fragtree2("lookup gave frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n",
			  this->ofs, this->ofs+this->size, this->yesde?(ref_offset(this->yesde->raw)):0xffffffff, this);
		lastend = this->ofs + this->size;
	} else {
		dbg_fragtree2("lookup gave yes frag\n");
		lastend = 0;
	}

	/* See if we ran off the end of the fragtree */
	if (lastend <= newfrag->ofs) {
		/* We did */

		/* Check if 'this' yesde was on the same page as the new yesde.
		   If so, both 'this' and the new yesde get marked REF_NORMAL so
		   the GC can take a look.
		*/
		if (lastend && (lastend-1) >> PAGE_SHIFT == newfrag->ofs >> PAGE_SHIFT) {
			if (this->yesde)
				mark_ref_yesrmal(this->yesde->raw);
			mark_ref_yesrmal(newfrag->yesde->raw);
		}

		return yes_overlapping_yesde(c, root, newfrag, this, lastend);
	}

	if (this->yesde)
		dbg_fragtree2("dealing with frag %u-%u, phys %#08x(%d).\n",
		this->ofs, this->ofs + this->size,
		ref_offset(this->yesde->raw), ref_flags(this->yesde->raw));
	else
		dbg_fragtree2("dealing with hole frag %u-%u.\n",
		this->ofs, this->ofs + this->size);

	/* OK. 'this' is pointing at the first frag that newfrag->ofs at least partially obsoletes,
	 * - i.e. newfrag->ofs < this->ofs+this->size && newfrag->ofs >= this->ofs
	 */
	if (newfrag->ofs > this->ofs) {
		/* This yesde isn't completely obsoleted. The start of it remains valid */

		/* Mark the new yesde and the partially covered yesde REF_NORMAL -- let
		   the GC take a look at them */
		mark_ref_yesrmal(newfrag->yesde->raw);
		if (this->yesde)
			mark_ref_yesrmal(this->yesde->raw);

		if (this->ofs + this->size > newfrag->ofs + newfrag->size) {
			/* The new yesde splits 'this' frag into two */
			struct jffs2_yesde_frag *newfrag2;

			if (this->yesde)
				dbg_fragtree2("split old frag 0x%04x-0x%04x, phys 0x%08x\n",
					this->ofs, this->ofs+this->size, ref_offset(this->yesde->raw));
			else
				dbg_fragtree2("split old hole frag 0x%04x-0x%04x\n",
					this->ofs, this->ofs+this->size);

			/* New second frag pointing to this's yesde */
			newfrag2 = new_fragment(this->yesde, newfrag->ofs + newfrag->size,
						this->ofs + this->size - newfrag->ofs - newfrag->size);
			if (unlikely(!newfrag2))
				return -ENOMEM;
			if (this->yesde)
				this->yesde->frags++;

			/* Adjust size of original 'this' */
			this->size = newfrag->ofs - this->ofs;

			/* Now, we kyesw there's yes yesde with offset
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
		/* New yesde just reduces 'this' frag in size, doesn't split it */
		this->size = newfrag->ofs - this->ofs;

		/* Again, we kyesw it lives down here in the tree */
		jffs2_fragtree_insert(newfrag, this);
		rb_insert_color(&newfrag->rb, root);
	} else {
		/* New frag starts at the same point as 'this' used to. Replace
		   it in the tree without doing a delete and insertion */
		dbg_fragtree2("inserting newfrag (*%p),%d-%d in before 'this' (*%p),%d-%d\n",
			  newfrag, newfrag->ofs, newfrag->ofs+newfrag->size, this, this->ofs, this->ofs+this->size);

		rb_replace_yesde(&this->rb, &newfrag->rb, root);

		if (newfrag->ofs + newfrag->size >= this->ofs+this->size) {
			dbg_fragtree2("obsoleting yesde frag %p (%x-%x)\n", this, this->ofs, this->ofs+this->size);
			jffs2_obsolete_yesde_frag(c, this);
		} else {
			this->ofs += newfrag->size;
			this->size -= newfrag->size;

			jffs2_fragtree_insert(this, newfrag);
			rb_insert_color(&this->rb, root);
			return 0;
		}
	}
	/* OK, yesw we have newfrag added in the correct place in the tree, but
	   frag_next(newfrag) may be a fragment which is overlapped by it
	*/
	while ((this = frag_next(newfrag)) && newfrag->ofs + newfrag->size >= this->ofs + this->size) {
		/* 'this' frag is obsoleted completely. */
		dbg_fragtree2("obsoleting yesde frag %p (%x-%x) and removing from tree\n",
			this, this->ofs, this->ofs+this->size);
		rb_erase(&this->rb, root);
		jffs2_obsolete_yesde_frag(c, this);
	}
	/* Now we're pointing at the first frag which isn't totally obsoleted by
	   the new frag */

	if (!this || newfrag->ofs + newfrag->size == this->ofs)
		return 0;

	/* Still some overlap but we don't need to move it in the tree */
	this->size = (this->ofs + this->size) - (newfrag->ofs + newfrag->size);
	this->ofs = newfrag->ofs + newfrag->size;

	/* And mark them REF_NORMAL so the GC takes a look at them */
	if (this->yesde)
		mark_ref_yesrmal(this->yesde->raw);
	mark_ref_yesrmal(newfrag->yesde->raw);

	return 0;
}

/*
 * Given an iyesde, probably with existing tree of fragments, add the new yesde
 * to the fragment tree.
 */
int jffs2_add_full_dyesde_to_iyesde(struct jffs2_sb_info *c, struct jffs2_iyesde_info *f, struct jffs2_full_dyesde *fn)
{
	int ret;
	struct jffs2_yesde_frag *newfrag;

	if (unlikely(!fn->size))
		return 0;

	newfrag = new_fragment(fn, fn->ofs, fn->size);
	if (unlikely(!newfrag))
		return -ENOMEM;
	newfrag->yesde->frags = 1;

	dbg_fragtree("adding yesde %#04x-%#04x @0x%08x on flash, newfrag *%p\n",
		  fn->ofs, fn->ofs+fn->size, ref_offset(fn->raw), newfrag);

	ret = jffs2_add_frag_to_fragtree(c, &f->fragtree, newfrag);
	if (unlikely(ret))
		return ret;

	/* If we yesw share a page with other yesdes, mark either previous
	   or next yesde REF_NORMAL, as appropriate.  */
	if (newfrag->ofs & (PAGE_SIZE-1)) {
		struct jffs2_yesde_frag *prev = frag_prev(newfrag);

		mark_ref_yesrmal(fn->raw);
		/* If we don't start at zero there's _always_ a previous */
		if (prev->yesde)
			mark_ref_yesrmal(prev->yesde->raw);
	}

	if ((newfrag->ofs+newfrag->size) & (PAGE_SIZE-1)) {
		struct jffs2_yesde_frag *next = frag_next(newfrag);

		if (next) {
			mark_ref_yesrmal(fn->raw);
			if (next->yesde)
				mark_ref_yesrmal(next->yesde->raw);
		}
	}
	jffs2_dbg_fragtree_parayesia_check_yeslock(f);

	return 0;
}

void jffs2_set_iyescache_state(struct jffs2_sb_info *c, struct jffs2_iyesde_cache *ic, int state)
{
	spin_lock(&c->iyescache_lock);
	ic->state = state;
	wake_up(&c->iyescache_wq);
	spin_unlock(&c->iyescache_lock);
}

/* During mount, this needs yes locking. During yesrmal operation, its
   callers want to do other stuff while still holding the iyescache_lock.
   Rather than introducing special case get_iyes_cache functions or
   callbacks, we just let the caller do the locking itself. */

struct jffs2_iyesde_cache *jffs2_get_iyes_cache(struct jffs2_sb_info *c, uint32_t iyes)
{
	struct jffs2_iyesde_cache *ret;

	ret = c->iyescache_list[iyes % c->iyescache_hashsize];
	while (ret && ret->iyes < iyes) {
		ret = ret->next;
	}

	if (ret && ret->iyes != iyes)
		ret = NULL;

	return ret;
}

void jffs2_add_iyes_cache (struct jffs2_sb_info *c, struct jffs2_iyesde_cache *new)
{
	struct jffs2_iyesde_cache **prev;

	spin_lock(&c->iyescache_lock);
	if (!new->iyes)
		new->iyes = ++c->highest_iyes;

	dbg_iyescache("add %p (iyes #%u)\n", new, new->iyes);

	prev = &c->iyescache_list[new->iyes % c->iyescache_hashsize];

	while ((*prev) && (*prev)->iyes < new->iyes) {
		prev = &(*prev)->next;
	}
	new->next = *prev;
	*prev = new;

	spin_unlock(&c->iyescache_lock);
}

void jffs2_del_iyes_cache(struct jffs2_sb_info *c, struct jffs2_iyesde_cache *old)
{
	struct jffs2_iyesde_cache **prev;

#ifdef CONFIG_JFFS2_FS_XATTR
	BUG_ON(old->xref);
#endif
	dbg_iyescache("del %p (iyes #%u)\n", old, old->iyes);
	spin_lock(&c->iyescache_lock);

	prev = &c->iyescache_list[old->iyes % c->iyescache_hashsize];

	while ((*prev) && (*prev)->iyes < old->iyes) {
		prev = &(*prev)->next;
	}
	if ((*prev) == old) {
		*prev = old->next;
	}

	/* Free it yesw unless it's in READING or CLEARING state, which
	   are the transitions upon read_iyesde() and clear_iyesde(). The
	   rest of the time we kyesw yesbody else is looking at it, and
	   if it's held by read_iyesde() or clear_iyesde() they'll free it
	   for themselves. */
	if (old->state != INO_STATE_READING && old->state != INO_STATE_CLEARING)
		jffs2_free_iyesde_cache(old);

	spin_unlock(&c->iyescache_lock);
}

void jffs2_free_iyes_caches(struct jffs2_sb_info *c)
{
	int i;
	struct jffs2_iyesde_cache *this, *next;

	for (i=0; i < c->iyescache_hashsize; i++) {
		this = c->iyescache_list[i];
		while (this) {
			next = this->next;
			jffs2_xattr_free_iyesde(c, this);
			jffs2_free_iyesde_cache(this);
			this = next;
		}
		c->iyescache_list[i] = NULL;
	}
}

void jffs2_free_raw_yesde_refs(struct jffs2_sb_info *c)
{
	int i;
	struct jffs2_raw_yesde_ref *this, *next;

	for (i=0; i<c->nr_blocks; i++) {
		this = c->blocks[i].first_yesde;
		while (this) {
			if (this[REFS_PER_BLOCK].flash_offset == REF_LINK_NODE)
				next = this[REFS_PER_BLOCK].next_in_iyes;
			else
				next = NULL;

			jffs2_free_refblock(this);
			this = next;
		}
		c->blocks[i].first_yesde = c->blocks[i].last_yesde = NULL;
	}
}

struct jffs2_yesde_frag *jffs2_lookup_yesde_frag(struct rb_root *fragtree, uint32_t offset)
{
	/* The common case in lookup is that there will be a yesde
	   which precisely matches. So we go looking for that first */
	struct rb_yesde *next;
	struct jffs2_yesde_frag *prev = NULL;
	struct jffs2_yesde_frag *frag = NULL;

	dbg_fragtree2("root %p, offset %d\n", fragtree, offset);

	next = fragtree->rb_yesde;

	while(next) {
		frag = rb_entry(next, struct jffs2_yesde_frag, rb);

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

	/* Exact match yest found. Go back up looking at each parent,
	   and return the closest smaller one */

	if (prev)
		dbg_fragtree2("yes match. Returning frag %#04x-%#04x, closest previous\n",
			  prev->ofs, prev->ofs+prev->size);
	else
		dbg_fragtree2("returning NULL, empty fragtree\n");

	return prev;
}

/* Pass 'c' argument to indicate that yesdes should be marked obsolete as
   they're killed. */
void jffs2_kill_fragtree(struct rb_root *root, struct jffs2_sb_info *c)
{
	struct jffs2_yesde_frag *frag, *next;

	dbg_fragtree("killing\n");
	rbtree_postorder_for_each_entry_safe(frag, next, root, rb) {
		if (frag->yesde && !(--frag->yesde->frags)) {
			/* Not a hole, and it's the final remaining frag
			   of this yesde. Free the yesde */
			if (c)
				jffs2_mark_yesde_obsolete(c, frag->yesde->raw);

			jffs2_free_full_dyesde(frag->yesde);
		}

		jffs2_free_yesde_frag(frag);
		cond_resched();
	}
}

struct jffs2_raw_yesde_ref *jffs2_link_yesde_ref(struct jffs2_sb_info *c,
					       struct jffs2_eraseblock *jeb,
					       uint32_t ofs, uint32_t len,
					       struct jffs2_iyesde_cache *ic)
{
	struct jffs2_raw_yesde_ref *ref;

	BUG_ON(!jeb->allocated_refs);
	jeb->allocated_refs--;

	ref = jeb->last_yesde;

	dbg_yesderef("Last yesde at %p is (%08x,%p)\n", ref, ref->flash_offset,
		    ref->next_in_iyes);

	while (ref->flash_offset != REF_EMPTY_NODE) {
		if (ref->flash_offset == REF_LINK_NODE)
			ref = ref->next_in_iyes;
		else
			ref++;
	}

	dbg_yesderef("New ref is %p (%08x becomes %08x,%p) len 0x%x\n", ref, 
		    ref->flash_offset, ofs, ref->next_in_iyes, len);

	ref->flash_offset = ofs;

	if (!jeb->first_yesde) {
		jeb->first_yesde = ref;
		BUG_ON(ref_offset(ref) != jeb->offset);
	} else if (unlikely(ref_offset(ref) != jeb->offset + c->sector_size - jeb->free_size)) {
		uint32_t last_len = ref_totlen(c, jeb, jeb->last_yesde);

		JFFS2_ERROR("Adding new ref %p at (0x%08x-0x%08x) yest immediately after previous (0x%08x-0x%08x)\n",
			    ref, ref_offset(ref), ref_offset(ref)+len,
			    ref_offset(jeb->last_yesde), 
			    ref_offset(jeb->last_yesde)+last_len);
		BUG();
	}
	jeb->last_yesde = ref;

	if (ic) {
		ref->next_in_iyes = ic->yesdes;
		ic->yesdes = ref;
	} else {
		ref->next_in_iyes = NULL;
	}

	switch(ref_flags(ref)) {
	case REF_UNCHECKED:
		c->unchecked_size += len;
		jeb->unchecked_size += len;
		break;

	case REF_NORMAL:
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
	/* Set (and test) __totlen field... for yesw */
	ref->__totlen = len;
	ref_totlen(c, jeb, ref);
#endif
	return ref;
}

/* No locking, yes reservation of 'ref'. Do yest use on a live file system */
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
	/* REF_EMPTY_NODE is !obsolete, so that works OK */
	if (jeb->last_yesde && ref_obsolete(jeb->last_yesde)) {
#ifdef TEST_TOTLEN
		jeb->last_yesde->__totlen += size;
#endif
		c->dirty_size += size;
		c->free_size -= size;
		jeb->dirty_size += size;
		jeb->free_size -= size;
	} else {
		uint32_t ofs = jeb->offset + c->sector_size - jeb->free_size;
		ofs |= REF_OBSOLETE;

		jffs2_link_yesde_ref(c, jeb, ofs, size, NULL);
	}

	return 0;
}

/* Calculate totlen from surrounding yesdes or eraseblock */
static inline uint32_t __ref_totlen(struct jffs2_sb_info *c,
				    struct jffs2_eraseblock *jeb,
				    struct jffs2_raw_yesde_ref *ref)
{
	uint32_t ref_end;
	struct jffs2_raw_yesde_ref *next_ref = ref_next(ref);

	if (next_ref)
		ref_end = ref_offset(next_ref);
	else {
		if (!jeb)
			jeb = &c->blocks[ref->flash_offset / c->sector_size];

		/* Last yesde in block. Use free_space */
		if (unlikely(ref != jeb->last_yesde)) {
			pr_crit("ref %p @0x%08x is yest jeb->last_yesde (%p @0x%08x)\n",
				ref, ref_offset(ref), jeb->last_yesde,
				jeb->last_yesde ?
				ref_offset(jeb->last_yesde) : 0);
			BUG();
		}
		ref_end = jeb->offset + c->sector_size - jeb->free_size;
	}
	return ref_end - ref_offset(ref);
}

uint32_t __jffs2_ref_totlen(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
			    struct jffs2_raw_yesde_ref *ref)
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
			pr_crit("No next ref. jeb->last_yesde is %p\n",
				jeb->last_yesde);

		pr_crit("jeb->wasted_size %x, dirty_size %x, used_size %x, free_size %x\n",
			jeb->wasted_size, jeb->dirty_size, jeb->used_size,
			jeb->free_size);

#if defined(JFFS2_DBG_DUMPS) || defined(JFFS2_DBG_PARANOIA_CHECKS)
		__jffs2_dbg_dump_yesde_refs_yeslock(c, jeb);
#endif

		WARN_ON(1);

		ret = ref->__totlen;
	}
#endif /* TEST_TOTLEN */
	return ret;
}
