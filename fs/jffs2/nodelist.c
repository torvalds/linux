/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: nodelist.c,v 1.115 2005/11/07 11:14:40 gleixner Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mtd/mtd.h>
#include <linux/rbtree.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include "nodelist.h"

void jffs2_add_fd_to_list(struct jffs2_sb_info *c, struct jffs2_full_dirent *new, struct jffs2_full_dirent **list)
{
	struct jffs2_full_dirent **prev = list;

	dbg_dentlist("add dirent \"%s\", ino #%u\n", new->name, new->ino);

	while ((*prev) && (*prev)->nhash <= new->nhash) {
		if ((*prev)->nhash == new->nhash && !strcmp((*prev)->name, new->name)) {
			/* Duplicate. Free one */
			if (new->version < (*prev)->version) {
				dbg_dentlist("Eep! Marking new dirent node is obsolete, old is \"%s\", ino #%u\n",
					(*prev)->name, (*prev)->ino);
				jffs2_mark_node_obsolete(c, new->raw);
				jffs2_free_full_dirent(new);
			} else {
				dbg_dentlist("marking old dirent \"%s\", ino #%u bsolete\n",
					(*prev)->name, (*prev)->ino);
				new->next = (*prev)->next;
				jffs2_mark_node_obsolete(c, ((*prev)->raw));
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

void jffs2_truncate_fragtree(struct jffs2_sb_info *c, struct rb_root *list, uint32_t size)
{
	struct jffs2_node_frag *frag = jffs2_lookup_node_frag(list, size);

	dbg_fragtree("truncating fragtree to 0x%08x bytes\n", size);

	/* We know frag->ofs <= size. That's what lookup does for us */
	if (frag && frag->ofs != size) {
		if (frag->ofs+frag->size > size) {
			frag->size = size - frag->ofs;
		}
		frag = frag_next(frag);
	}
	while (frag && frag->ofs >= size) {
		struct jffs2_node_frag *next = frag_next(frag);

		frag_erase(frag, list);
		jffs2_obsolete_node_frag(c, frag);
		frag = next;
	}

	if (size == 0)
		return;

	/*
	 * If the last fragment starts at the RAM page boundary, it is
	 * REF_PRISTINE irrespective of its size.
	 */
	frag = frag_last(list);
	if (frag->node && (frag->ofs & (PAGE_CACHE_SIZE - 1)) == 0) {
		dbg_fragtree2("marking the last fragment 0x%08x-0x%08x REF_PRISTINE.\n",
			frag->ofs, frag->ofs + frag->size);
		frag->node->raw->flash_offset = ref_offset(frag->node->raw) | REF_PRISTINE;
	}
}

void jffs2_obsolete_node_frag(struct jffs2_sb_info *c, struct jffs2_node_frag *this)
{
	if (this->node) {
		this->node->frags--;
		if (!this->node->frags) {
			/* The node has no valid frags left. It's totally obsoleted */
			dbg_fragtree2("marking old node @0x%08x (0x%04x-0x%04x) obsolete\n",
				ref_offset(this->node->raw), this->node->ofs, this->node->ofs+this->node->size);
			jffs2_mark_node_obsolete(c, this->node->raw);
			jffs2_free_full_dnode(this->node);
		} else {
			dbg_fragtree2("marking old node @0x%08x (0x%04x-0x%04x) REF_NORMAL. frags is %d\n",
				ref_offset(this->node->raw), this->node->ofs, this->node->ofs+this->node->size, this->node->frags);
			mark_ref_normal(this->node->raw);
		}

	}
	jffs2_free_node_frag(this);
}

static void jffs2_fragtree_insert(struct jffs2_node_frag *newfrag, struct jffs2_node_frag *base)
{
	struct rb_node *parent = &base->rb;
	struct rb_node **link = &parent;

	dbg_fragtree2("insert frag (0x%04x-0x%04x)\n", newfrag->ofs, newfrag->ofs + newfrag->size);

	while (*link) {
		parent = *link;
		base = rb_entry(parent, struct jffs2_node_frag, rb);

		if (newfrag->ofs > base->ofs)
			link = &base->rb.rb_right;
		else if (newfrag->ofs < base->ofs)
			link = &base->rb.rb_left;
		else {
			JFFS2_ERROR("duplicate frag at %08x (%p,%p)\n", newfrag->ofs, newfrag, base);
			BUG();
		}
	}

	rb_link_node(&newfrag->rb, &base->rb, link);
}

/*
 * Allocate and initializes a new fragment.
 */
static struct jffs2_node_frag * new_fragment(struct jffs2_full_dnode *fn, uint32_t ofs, uint32_t size)
{
	struct jffs2_node_frag *newfrag;

	newfrag = jffs2_alloc_node_frag();
	if (likely(newfrag)) {
		newfrag->ofs = ofs;
		newfrag->size = size;
		newfrag->node = fn;
	} else {
		JFFS2_ERROR("cannot allocate a jffs2_node_frag object\n");
	}

	return newfrag;
}

/*
 * Called when there is no overlapping fragment exist. Inserts a hole before the new
 * fragment and inserts the new fragment to the fragtree.
 */
static int no_overlapping_node(struct jffs2_sb_info *c, struct rb_root *root,
		 	       struct jffs2_node_frag *newfrag,
			       struct jffs2_node_frag *this, uint32_t lastend)
{
	if (lastend < newfrag->node->ofs) {
		/* put a hole in before the new fragment */
		struct jffs2_node_frag *holefrag;

		holefrag= new_fragment(NULL, lastend, newfrag->node->ofs - lastend);
		if (unlikely(!holefrag)) {
			jffs2_free_node_frag(newfrag);
			return -ENOMEM;
		}

		if (this) {
			/* By definition, the 'this' node has no right-hand child,
			   because there are no frags with offset greater than it.
			   So that's where we want to put the hole */
			dbg_fragtree2("add hole frag %#04x-%#04x on the right of the new frag.\n",
				holefrag->ofs, holefrag->ofs + holefrag->size);
			rb_link_node(&holefrag->rb, &this->rb, &this->rb.rb_right);
		} else {
			dbg_fragtree2("Add hole frag %#04x-%#04x to the root of the tree.\n",
				holefrag->ofs, holefrag->ofs + holefrag->size);
			rb_link_node(&holefrag->rb, NULL, &root->rb_node);
		}
		rb_insert_color(&holefrag->rb, root);
		this = holefrag;
	}

	if (this) {
		/* By definition, the 'this' node has no right-hand child,
		   because there are no frags with offset greater than it.
		   So that's where we want to put new fragment */
		dbg_fragtree2("add the new node at the right\n");
		rb_link_node(&newfrag->rb, &this->rb, &this->rb.rb_right);
	} else {
		dbg_fragtree2("insert the new node at the root of the tree\n");
		rb_link_node(&newfrag->rb, NULL, &root->rb_node);
	}
	rb_insert_color(&newfrag->rb, root);

	return 0;
}

/* Doesn't set inode->i_size */
static int jffs2_add_frag_to_fragtree(struct jffs2_sb_info *c, struct rb_root *root, struct jffs2_node_frag *newfrag)
{
	struct jffs2_node_frag *this;
	uint32_t lastend;

	/* Skip all the nodes which are completed before this one starts */
	this = jffs2_lookup_node_frag(root, newfrag->node->ofs);

	if (this) {
		dbg_fragtree2("lookup gave frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n",
			  this->ofs, this->ofs+this->size, this->node?(ref_offset(this->node->raw)):0xffffffff, this);
		lastend = this->ofs + this->size;
	} else {
		dbg_fragtree2("lookup gave no frag\n");
		lastend = 0;
	}

	/* See if we ran off the end of the fragtree */
	if (lastend <= newfrag->ofs) {
		/* We did */

		/* Check if 'this' node was on the same page as the new node.
		   If so, both 'this' and the new node get marked REF_NORMAL so
		   the GC can take a look.
		*/
		if (lastend && (lastend-1) >> PAGE_CACHE_SHIFT == newfrag->ofs >> PAGE_CACHE_SHIFT) {
			if (this->node)
				mark_ref_normal(this->node->raw);
			mark_ref_normal(newfrag->node->raw);
		}

		return no_overlapping_node(c, root, newfrag, this, lastend);
	}

	if (this->node)
		dbg_fragtree2("dealing with frag %u-%u, phys %#08x(%d).\n",
		this->ofs, this->ofs + this->size,
		ref_offset(this->node->raw), ref_flags(this->node->raw));
	else
		dbg_fragtree2("dealing with hole frag %u-%u.\n",
		this->ofs, this->ofs + this->size);

	/* OK. 'this' is pointing at the first frag that newfrag->ofs at least partially obsoletes,
	 * - i.e. newfrag->ofs < this->ofs+this->size && newfrag->ofs >= this->ofs
	 */
	if (newfrag->ofs > this->ofs) {
		/* This node isn't completely obsoleted. The start of it remains valid */

		/* Mark the new node and the partially covered node REF_NORMAL -- let
		   the GC take a look at them */
		mark_ref_normal(newfrag->node->raw);
		if (this->node)
			mark_ref_normal(this->node->raw);

		if (this->ofs + this->size > newfrag->ofs + newfrag->size) {
			/* The new node splits 'this' frag into two */
			struct jffs2_node_frag *newfrag2;

			if (this->node)
				dbg_fragtree2("split old frag 0x%04x-0x%04x, phys 0x%08x\n",
					this->ofs, this->ofs+this->size, ref_offset(this->node->raw));
			else
				dbg_fragtree2("split old hole frag 0x%04x-0x%04x\n",
					this->ofs, this->ofs+this->size);

			/* New second frag pointing to this's node */
			newfrag2 = new_fragment(this->node, newfrag->ofs + newfrag->size,
						this->ofs + this->size - newfrag->ofs - newfrag->size);
			if (unlikely(!newfrag2))
				return -ENOMEM;
			if (this->node)
				this->node->frags++;

			/* Adjust size of original 'this' */
			this->size = newfrag->ofs - this->ofs;

			/* Now, we know there's no node with offset
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
		/* New node just reduces 'this' frag in size, doesn't split it */
		this->size = newfrag->ofs - this->ofs;

		/* Again, we know it lives down here in the tree */
		jffs2_fragtree_insert(newfrag, this);
		rb_insert_color(&newfrag->rb, root);
	} else {
		/* New frag starts at the same point as 'this' used to. Replace
		   it in the tree without doing a delete and insertion */
		dbg_fragtree2("inserting newfrag (*%p),%d-%d in before 'this' (*%p),%d-%d\n",
			  newfrag, newfrag->ofs, newfrag->ofs+newfrag->size, this, this->ofs, this->ofs+this->size);

		rb_replace_node(&this->rb, &newfrag->rb, root);

		if (newfrag->ofs + newfrag->size >= this->ofs+this->size) {
			dbg_fragtree2("obsoleting node frag %p (%x-%x)\n", this, this->ofs, this->ofs+this->size);
			jffs2_obsolete_node_frag(c, this);
		} else {
			this->ofs += newfrag->size;
			this->size -= newfrag->size;

			jffs2_fragtree_insert(this, newfrag);
			rb_insert_color(&this->rb, root);
			return 0;
		}
	}
	/* OK, now we have newfrag added in the correct place in the tree, but
	   frag_next(newfrag) may be a fragment which is overlapped by it
	*/
	while ((this = frag_next(newfrag)) && newfrag->ofs + newfrag->size >= this->ofs + this->size) {
		/* 'this' frag is obsoleted completely. */
		dbg_fragtree2("obsoleting node frag %p (%x-%x) and removing from tree\n",
			this, this->ofs, this->ofs+this->size);
		rb_erase(&this->rb, root);
		jffs2_obsolete_node_frag(c, this);
	}
	/* Now we're pointing at the first frag which isn't totally obsoleted by
	   the new frag */

	if (!this || newfrag->ofs + newfrag->size == this->ofs)
		return 0;

	/* Still some overlap but we don't need to move it in the tree */
	this->size = (this->ofs + this->size) - (newfrag->ofs + newfrag->size);
	this->ofs = newfrag->ofs + newfrag->size;

	/* And mark them REF_NORMAL so the GC takes a look at them */
	if (this->node)
		mark_ref_normal(this->node->raw);
	mark_ref_normal(newfrag->node->raw);

	return 0;
}

/*
 * Given an inode, probably with existing tree of fragments, add the new node
 * to the fragment tree.
 */
int jffs2_add_full_dnode_to_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_full_dnode *fn)
{
	int ret;
	struct jffs2_node_frag *newfrag;

	if (unlikely(!fn->size))
		return 0;

	newfrag = new_fragment(fn, fn->ofs, fn->size);
	if (unlikely(!newfrag))
		return -ENOMEM;
	newfrag->node->frags = 1;

	dbg_fragtree("adding node %#04x-%#04x @0x%08x on flash, newfrag *%p\n",
		  fn->ofs, fn->ofs+fn->size, ref_offset(fn->raw), newfrag);

	ret = jffs2_add_frag_to_fragtree(c, &f->fragtree, newfrag);
	if (unlikely(ret))
		return ret;

	/* If we now share a page with other nodes, mark either previous
	   or next node REF_NORMAL, as appropriate.  */
	if (newfrag->ofs & (PAGE_CACHE_SIZE-1)) {
		struct jffs2_node_frag *prev = frag_prev(newfrag);

		mark_ref_normal(fn->raw);
		/* If we don't start at zero there's _always_ a previous */
		if (prev->node)
			mark_ref_normal(prev->node->raw);
	}

	if ((newfrag->ofs+newfrag->size) & (PAGE_CACHE_SIZE-1)) {
		struct jffs2_node_frag *next = frag_next(newfrag);

		if (next) {
			mark_ref_normal(fn->raw);
			if (next->node)
				mark_ref_normal(next->node->raw);
		}
	}
	jffs2_dbg_fragtree_paranoia_check_nolock(f);

	return 0;
}

/*
 * Check the data CRC of the node.
 *
 * Returns: 0 if the data CRC is correct;
 * 	    1 - if incorrect;
 *	    error code if an error occured.
 */
static int check_node_data(struct jffs2_sb_info *c, struct jffs2_tmp_dnode_info *tn)
{
	struct jffs2_raw_node_ref *ref = tn->fn->raw;
	int err = 0, pointed = 0;
	struct jffs2_eraseblock *jeb;
	unsigned char *buffer;
	uint32_t crc, ofs, len;
	size_t retlen;

	BUG_ON(tn->csize == 0);

	if (!jffs2_is_writebuffered(c))
		goto adj_acc;

	/* Calculate how many bytes were already checked */
	ofs = ref_offset(ref) + sizeof(struct jffs2_raw_inode);
	len = ofs % c->wbuf_pagesize;
	if (likely(len))
		len = c->wbuf_pagesize - len;

	if (len >= tn->csize) {
		dbg_readinode("no need to check node at %#08x, data length %u, data starts at %#08x - it has already been checked.\n",
			ref_offset(ref), tn->csize, ofs);
		goto adj_acc;
	}

	ofs += len;
	len = tn->csize - len;

	dbg_readinode("check node at %#08x, data length %u, partial CRC %#08x, correct CRC %#08x, data starts at %#08x, start checking from %#08x - %u bytes.\n",
		ref_offset(ref), tn->csize, tn->partial_crc, tn->data_crc, ofs - len, ofs, len);

#ifndef __ECOS
	/* TODO: instead, incapsulate point() stuff to jffs2_flash_read(),
	 * adding and jffs2_flash_read_end() interface. */
	if (c->mtd->point) {
		err = c->mtd->point(c->mtd, ofs, len, &retlen, &buffer);
		if (!err && retlen < tn->csize) {
			JFFS2_WARNING("MTD point returned len too short: %zu instead of %u.\n", retlen, tn->csize);
			c->mtd->unpoint(c->mtd, buffer, ofs, len);
		} else if (err)
			JFFS2_WARNING("MTD point failed: error code %d.\n", err);
		else
			pointed = 1; /* succefully pointed to device */
	}
#endif

	if (!pointed) {
		buffer = kmalloc(len, GFP_KERNEL);
		if (unlikely(!buffer))
			return -ENOMEM;

		/* TODO: this is very frequent pattern, make it a separate
		 * routine */
		err = jffs2_flash_read(c, ofs, len, &retlen, buffer);
		if (err) {
			JFFS2_ERROR("can not read %d bytes from 0x%08x, error code: %d.\n", len, ofs, err);
			goto free_out;
		}

		if (retlen != len) {
			JFFS2_ERROR("short read at %#08x: %zd instead of %d.\n", ofs, retlen, len);
			err = -EIO;
			goto free_out;
		}
	}

	/* Continue calculating CRC */
	crc = crc32(tn->partial_crc, buffer, len);
	if(!pointed)
		kfree(buffer);
#ifndef __ECOS
	else
		c->mtd->unpoint(c->mtd, buffer, ofs, len);
#endif

	if (crc != tn->data_crc) {
		JFFS2_NOTICE("wrong data CRC in data node at 0x%08x: read %#08x, calculated %#08x.\n",
			ofs, tn->data_crc, crc);
		return 1;
	}

adj_acc:
	jeb = &c->blocks[ref->flash_offset / c->sector_size];
	len = ref_totlen(c, jeb, ref);

	/*
	 * Mark the node as having been checked and fix the
	 * accounting accordingly.
	 */
	spin_lock(&c->erase_completion_lock);
	jeb->used_size += len;
	jeb->unchecked_size -= len;
	c->used_size += len;
	c->unchecked_size -= len;
	spin_unlock(&c->erase_completion_lock);

	return 0;

free_out:
	if(!pointed)
		kfree(buffer);
#ifndef __ECOS
	else
		c->mtd->unpoint(c->mtd, buffer, ofs, len);
#endif
	return err;
}

/*
 * Helper function for jffs2_add_older_frag_to_fragtree().
 *
 * Checks the node if we are in the checking stage.
 */
static int check_node(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_tmp_dnode_info *tn)
{
	int ret;

	BUG_ON(ref_obsolete(tn->fn->raw));

	/* We only check the data CRC of unchecked nodes */
	if (ref_flags(tn->fn->raw) != REF_UNCHECKED)
		return 0;

	dbg_fragtree2("check node %#04x-%#04x, phys offs %#08x.\n",
		tn->fn->ofs, tn->fn->ofs + tn->fn->size, ref_offset(tn->fn->raw));

	ret = check_node_data(c, tn);
	if (unlikely(ret < 0)) {
		JFFS2_ERROR("check_node_data() returned error: %d.\n",
			ret);
	} else if (unlikely(ret > 0)) {
		dbg_fragtree2("CRC error, mark it obsolete.\n");
		jffs2_mark_node_obsolete(c, tn->fn->raw);
	}

	return ret;
}

/*
 * Helper function for jffs2_add_older_frag_to_fragtree().
 *
 * Called when the new fragment that is being inserted
 * splits a hole fragment.
 */
static int split_hole(struct jffs2_sb_info *c, struct rb_root *root,
		      struct jffs2_node_frag *newfrag, struct jffs2_node_frag *hole)
{
	dbg_fragtree2("fragment %#04x-%#04x splits the hole %#04x-%#04x\n",
		newfrag->ofs, newfrag->ofs + newfrag->size, hole->ofs, hole->ofs + hole->size);

	if (hole->ofs == newfrag->ofs) {
		/*
		 * Well, the new fragment actually starts at the same offset as
		 * the hole.
		 */
		if (hole->ofs + hole->size > newfrag->ofs + newfrag->size) {
			/*
			 * We replace the overlapped left part of the hole by
			 * the new node.
			 */

			dbg_fragtree2("insert fragment %#04x-%#04x and cut the left part of the hole\n",
				newfrag->ofs, newfrag->ofs + newfrag->size);
			rb_replace_node(&hole->rb, &newfrag->rb, root);

			hole->ofs += newfrag->size;
			hole->size -= newfrag->size;

			/*
			 * We know that 'hole' should be the right hand
			 * fragment.
			 */
			jffs2_fragtree_insert(hole, newfrag);
			rb_insert_color(&hole->rb, root);
		} else {
			/*
			 * Ah, the new fragment is of the same size as the hole.
			 * Relace the hole by it.
			 */
			dbg_fragtree2("insert fragment %#04x-%#04x and overwrite hole\n",
				newfrag->ofs, newfrag->ofs + newfrag->size);
			rb_replace_node(&hole->rb, &newfrag->rb, root);
			jffs2_free_node_frag(hole);
		}
	} else {
		/* The new fragment lefts some hole space at the left */

		struct jffs2_node_frag * newfrag2 = NULL;

		if (hole->ofs + hole->size > newfrag->ofs + newfrag->size) {
			/* The new frag also lefts some space at the right */
			newfrag2 = new_fragment(NULL, newfrag->ofs +
				newfrag->size, hole->ofs + hole->size
				- newfrag->ofs - newfrag->size);
			if (unlikely(!newfrag2)) {
				jffs2_free_node_frag(newfrag);
				return -ENOMEM;
			}
		}

		hole->size = newfrag->ofs - hole->ofs;
		dbg_fragtree2("left the hole %#04x-%#04x at the left and inserd fragment %#04x-%#04x\n",
			hole->ofs, hole->ofs + hole->size, newfrag->ofs, newfrag->ofs + newfrag->size);

		jffs2_fragtree_insert(newfrag, hole);
		rb_insert_color(&newfrag->rb, root);

		if (newfrag2) {
			dbg_fragtree2("left the hole %#04x-%#04x at the right\n",
				newfrag2->ofs, newfrag2->ofs + newfrag2->size);
			jffs2_fragtree_insert(newfrag2, newfrag);
			rb_insert_color(&newfrag2->rb, root);
		}
	}

	return 0;
}

/*
 * This function is used when we build inode. It expects the nodes are passed
 * in the decreasing version order. The whole point of this is to improve the
 * inodes checking on NAND: we check the nodes' data CRC only when they are not
 * obsoleted. Previously, add_frag_to_fragtree() function was used and
 * nodes were passed to it in the increasing version ordes and CRCs of all
 * nodes were checked.
 *
 * Note: tn->fn->size shouldn't be zero.
 *
 * Returns 0 if the node was inserted
 *         1 if it wasn't inserted (since it is obsolete)
 *         < 0 an if error occured
 */
int jffs2_add_older_frag_to_fragtree(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
				     struct jffs2_tmp_dnode_info *tn)
{
	struct jffs2_node_frag *this, *newfrag;
	uint32_t lastend;
	struct jffs2_full_dnode *fn = tn->fn;
	struct rb_root *root = &f->fragtree;
	uint32_t fn_size = fn->size, fn_ofs = fn->ofs;
	int err, checked = 0;
	int ref_flag;

	dbg_fragtree("insert fragment %#04x-%#04x, ver %u\n", fn_ofs, fn_ofs + fn_size, tn->version);

	/* Skip all the nodes which are completed before this one starts */
	this = jffs2_lookup_node_frag(root, fn_ofs);
	if (this)
		dbg_fragtree2("'this' found %#04x-%#04x (%s)\n", this->ofs, this->ofs + this->size, this->node ? "data" : "hole");

	if (this)
		lastend = this->ofs + this->size;
	else
		lastend = 0;

	/* Detect the preliminary type of node */
	if (fn->size >= PAGE_CACHE_SIZE)
		ref_flag = REF_PRISTINE;
	else
		ref_flag = REF_NORMAL;

	/* See if we ran off the end of the root */
	if (lastend <= fn_ofs) {
		/* We did */

		/*
		 * We are going to insert the new node into the
		 * fragment tree, so check it.
		 */
		err = check_node(c, f, tn);
		if (err != 0)
			return err;

		fn->frags = 1;

		newfrag = new_fragment(fn, fn_ofs, fn_size);
		if (unlikely(!newfrag))
			return -ENOMEM;

		err = no_overlapping_node(c, root, newfrag, this, lastend);
		if (unlikely(err != 0)) {
			jffs2_free_node_frag(newfrag);
			return err;
		}

		goto out_ok;
	}

	fn->frags = 0;

	while (1) {
		/*
		 * Here we have:
		 * fn_ofs < this->ofs + this->size && fn_ofs >= this->ofs.
		 *
		 * Remember, 'this' has higher version, any non-hole node
		 * which is already in the fragtree is newer then the newly
		 * inserted.
		 */
		if (!this->node) {
			/*
			 * 'this' is the hole fragment, so at least the
			 * beginning of the new fragment is valid.
			 */

			/*
			 * We are going to insert the new node into the
			 * fragment tree, so check it.
			 */
			if (!checked) {
				err = check_node(c, f, tn);
				if (unlikely(err != 0))
					return err;
				checked = 1;
			}

			if (this->ofs + this->size >= fn_ofs + fn_size) {
				/* We split the hole on two parts */

				fn->frags += 1;
				newfrag = new_fragment(fn, fn_ofs, fn_size);
				if (unlikely(!newfrag))
					return -ENOMEM;

				err = split_hole(c, root, newfrag, this);
				if (unlikely(err))
					return err;
				goto out_ok;
			}

			/*
			 * The beginning of the new fragment is valid since it
			 * overlaps the hole node.
			 */

			ref_flag = REF_NORMAL;

			fn->frags += 1;
			newfrag = new_fragment(fn, fn_ofs,
					this->ofs + this->size - fn_ofs);
			if (unlikely(!newfrag))
				return -ENOMEM;

			if (fn_ofs == this->ofs) {
				/*
				 * The new node starts at the same offset as
				 * the hole and supersieds the hole.
				 */
				dbg_fragtree2("add the new fragment instead of hole %#04x-%#04x, refcnt %d\n",
					fn_ofs, fn_ofs + this->ofs + this->size - fn_ofs, fn->frags);

				rb_replace_node(&this->rb, &newfrag->rb, root);
				jffs2_free_node_frag(this);
			} else {
				/*
				 * The hole becomes shorter as its right part
				 * is supersieded by the new fragment.
				 */
				dbg_fragtree2("reduce size of hole %#04x-%#04x to %#04x-%#04x\n",
					this->ofs, this->ofs + this->size, this->ofs, this->ofs + this->size - newfrag->size);

				dbg_fragtree2("add new fragment %#04x-%#04x, refcnt %d\n", fn_ofs,
					fn_ofs + this->ofs + this->size - fn_ofs, fn->frags);

				this->size -= newfrag->size;
				jffs2_fragtree_insert(newfrag, this);
				rb_insert_color(&newfrag->rb, root);
			}

			fn_ofs += newfrag->size;
			fn_size -= newfrag->size;
			this = rb_entry(rb_next(&newfrag->rb),
					struct jffs2_node_frag, rb);

			dbg_fragtree2("switch to the next 'this' fragment: %#04x-%#04x %s\n",
				this->ofs, this->ofs + this->size, this->node ? "(data)" : "(hole)");
		}

		/*
		 * 'This' node is not the hole so it obsoletes the new fragment
		 * either fully or partially.
		 */
		if (this->ofs + this->size >= fn_ofs + fn_size) {
			/* The new node is obsolete, drop it */
			if (fn->frags == 0) {
				dbg_fragtree2("%#04x-%#04x is obsolete, mark it obsolete\n", fn_ofs, fn_ofs + fn_size);
				ref_flag = REF_OBSOLETE;
			}
			goto out_ok;
		} else {
			struct jffs2_node_frag *new_this;

			/* 'This' node obsoletes the beginning of the new node */
			dbg_fragtree2("the beginning %#04x-%#04x is obsolete\n", fn_ofs, this->ofs + this->size);

			ref_flag = REF_NORMAL;

			fn_size -= this->ofs + this->size - fn_ofs;
			fn_ofs = this->ofs + this->size;
			dbg_fragtree2("now considering %#04x-%#04x\n", fn_ofs, fn_ofs + fn_size);

			new_this = rb_entry(rb_next(&this->rb), struct jffs2_node_frag, rb);
			if (!new_this) {
				/*
				 * There is no next fragment. Add the rest of
				 * the new node as the right-hand child.
				 */
				if (!checked) {
					err = check_node(c, f, tn);
					if (unlikely(err != 0))
						return err;
					checked = 1;
				}

				fn->frags += 1;
				newfrag = new_fragment(fn, fn_ofs, fn_size);
				if (unlikely(!newfrag))
					return -ENOMEM;

				dbg_fragtree2("there are no more fragments, insert %#04x-%#04x\n",
					newfrag->ofs, newfrag->ofs + newfrag->size);
				rb_link_node(&newfrag->rb, &this->rb, &this->rb.rb_right);
				rb_insert_color(&newfrag->rb, root);
				goto out_ok;
			} else {
				this = new_this;
				dbg_fragtree2("switch to the next 'this' fragment: %#04x-%#04x %s\n",
					this->ofs, this->ofs + this->size, this->node ? "(data)" : "(hole)");
			}
		}
	}

out_ok:
	BUG_ON(fn->size < PAGE_CACHE_SIZE && ref_flag == REF_PRISTINE);

	if (ref_flag == REF_OBSOLETE) {
		dbg_fragtree2("the node is obsolete now\n");
		/* jffs2_mark_node_obsolete() will adjust space accounting */
		jffs2_mark_node_obsolete(c, fn->raw);
		return 1;
	}

	dbg_fragtree2("the node is \"%s\" now\n", ref_flag == REF_NORMAL ? "REF_NORMAL" : "REF_PRISTINE");

	/* Space accounting was adjusted at check_node_data() */
	spin_lock(&c->erase_completion_lock);
	fn->raw->flash_offset = ref_offset(fn->raw) | ref_flag;
	spin_unlock(&c->erase_completion_lock);

	return 0;
}

void jffs2_set_inocache_state(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic, int state)
{
	spin_lock(&c->inocache_lock);
	ic->state = state;
	wake_up(&c->inocache_wq);
	spin_unlock(&c->inocache_lock);
}

/* During mount, this needs no locking. During normal operation, its
   callers want to do other stuff while still holding the inocache_lock.
   Rather than introducing special case get_ino_cache functions or
   callbacks, we just let the caller do the locking itself. */

struct jffs2_inode_cache *jffs2_get_ino_cache(struct jffs2_sb_info *c, uint32_t ino)
{
	struct jffs2_inode_cache *ret;

	ret = c->inocache_list[ino % INOCACHE_HASHSIZE];
	while (ret && ret->ino < ino) {
		ret = ret->next;
	}

	if (ret && ret->ino != ino)
		ret = NULL;

	return ret;
}

void jffs2_add_ino_cache (struct jffs2_sb_info *c, struct jffs2_inode_cache *new)
{
	struct jffs2_inode_cache **prev;

	spin_lock(&c->inocache_lock);
	if (!new->ino)
		new->ino = ++c->highest_ino;

	dbg_inocache("add %p (ino #%u)\n", new, new->ino);

	prev = &c->inocache_list[new->ino % INOCACHE_HASHSIZE];

	while ((*prev) && (*prev)->ino < new->ino) {
		prev = &(*prev)->next;
	}
	new->next = *prev;
	*prev = new;

	spin_unlock(&c->inocache_lock);
}

void jffs2_del_ino_cache(struct jffs2_sb_info *c, struct jffs2_inode_cache *old)
{
	struct jffs2_inode_cache **prev;

	dbg_inocache("del %p (ino #%u)\n", old, old->ino);
	spin_lock(&c->inocache_lock);

	prev = &c->inocache_list[old->ino % INOCACHE_HASHSIZE];

	while ((*prev) && (*prev)->ino < old->ino) {
		prev = &(*prev)->next;
	}
	if ((*prev) == old) {
		*prev = old->next;
	}

	/* Free it now unless it's in READING or CLEARING state, which
	   are the transitions upon read_inode() and clear_inode(). The
	   rest of the time we know nobody else is looking at it, and
	   if it's held by read_inode() or clear_inode() they'll free it
	   for themselves. */
	if (old->state != INO_STATE_READING && old->state != INO_STATE_CLEARING)
		jffs2_free_inode_cache(old);

	spin_unlock(&c->inocache_lock);
}

void jffs2_free_ino_caches(struct jffs2_sb_info *c)
{
	int i;
	struct jffs2_inode_cache *this, *next;

	for (i=0; i<INOCACHE_HASHSIZE; i++) {
		this = c->inocache_list[i];
		while (this) {
			next = this->next;
			jffs2_xattr_free_inode(c, this);
			jffs2_free_inode_cache(this);
			this = next;
		}
		c->inocache_list[i] = NULL;
	}
}

void jffs2_free_raw_node_refs(struct jffs2_sb_info *c)
{
	int i;
	struct jffs2_raw_node_ref *this, *next;

	for (i=0; i<c->nr_blocks; i++) {
		this = c->blocks[i].first_node;
		while(this) {
			next = this->next_phys;
			jffs2_free_raw_node_ref(this);
			this = next;
		}
		c->blocks[i].first_node = c->blocks[i].last_node = NULL;
	}
}

struct jffs2_node_frag *jffs2_lookup_node_frag(struct rb_root *fragtree, uint32_t offset)
{
	/* The common case in lookup is that there will be a node
	   which precisely matches. So we go looking for that first */
	struct rb_node *next;
	struct jffs2_node_frag *prev = NULL;
	struct jffs2_node_frag *frag = NULL;

	dbg_fragtree2("root %p, offset %d\n", fragtree, offset);

	next = fragtree->rb_node;

	while(next) {
		frag = rb_entry(next, struct jffs2_node_frag, rb);

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

	/* Exact match not found. Go back up looking at each parent,
	   and return the closest smaller one */

	if (prev)
		dbg_fragtree2("no match. Returning frag %#04x-%#04x, closest previous\n",
			  prev->ofs, prev->ofs+prev->size);
	else
		dbg_fragtree2("returning NULL, empty fragtree\n");

	return prev;
}

/* Pass 'c' argument to indicate that nodes should be marked obsolete as
   they're killed. */
void jffs2_kill_fragtree(struct rb_root *root, struct jffs2_sb_info *c)
{
	struct jffs2_node_frag *frag;
	struct jffs2_node_frag *parent;

	if (!root->rb_node)
		return;

	dbg_fragtree("killing\n");

	frag = (rb_entry(root->rb_node, struct jffs2_node_frag, rb));
	while(frag) {
		if (frag->rb.rb_left) {
			frag = frag_left(frag);
			continue;
		}
		if (frag->rb.rb_right) {
			frag = frag_right(frag);
			continue;
		}

		if (frag->node && !(--frag->node->frags)) {
			/* Not a hole, and it's the final remaining frag
			   of this node. Free the node */
			if (c)
				jffs2_mark_node_obsolete(c, frag->node->raw);

			jffs2_free_full_dnode(frag->node);
		}
		parent = frag_parent(frag);
		if (parent) {
			if (frag_left(parent) == frag)
				parent->rb.rb_left = NULL;
			else
				parent->rb.rb_right = NULL;
		}

		jffs2_free_node_frag(frag);
		frag = parent;

		cond_resched();
	}
}

void jffs2_link_node_ref(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,
			 struct jffs2_raw_node_ref *ref, uint32_t len)
{
	if (!jeb->first_node)
		jeb->first_node = ref;
	if (jeb->last_node)
		jeb->last_node->next_phys = ref;
	jeb->last_node = ref;

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
		jeb->used_size += len;
		break;
	}
	c->free_size -= len;
	jeb->free_size -= len;

	/* Set __totlen field... for now */
	ref->__totlen = len;
	ref->next_phys = NULL;
}
