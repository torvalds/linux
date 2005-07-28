/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: nodelist.c,v 1.102 2005/07/28 12:45:10 dedekind Exp $
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
	
	JFFS2_DBG_DENTLIST("add dirent \"%s\", ino #%u\n", new->name, new->ino);

	while ((*prev) && (*prev)->nhash <= new->nhash) {
		if ((*prev)->nhash == new->nhash && !strcmp((*prev)->name, new->name)) {
			/* Duplicate. Free one */
			if (new->version < (*prev)->version) {
				JFFS2_DBG_DENTLIST("Eep! Marking new dirent node is obsolete, old is \"%s\", ino #%u\n",
					(*prev)->name, (*prev)->ino);
				jffs2_mark_node_obsolete(c, new->raw);
				jffs2_free_full_dirent(new);
			} else {
				JFFS2_DBG_DENTLIST("marking old dirent \"%s\", ino #%u bsolete\n",
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

void jffs2_obsolete_node_frag(struct jffs2_sb_info *c, struct jffs2_node_frag *this)
{
	if (this->node) {
		this->node->frags--;
		if (!this->node->frags) {
			/* The node has no valid frags left. It's totally obsoleted */
			JFFS2_DBG_FRAGTREE2("marking old node @0x%08x (0x%04x-0x%04x) obsolete\n",
				ref_offset(this->node->raw), this->node->ofs, this->node->ofs+this->node->size);
			jffs2_mark_node_obsolete(c, this->node->raw);
			jffs2_free_full_dnode(this->node);
		} else {
			JFFS2_DBG_FRAGTREE2("marking old node @0x%08x (0x%04x-0x%04x) REF_NORMAL. frags is %d\n",
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

	JFFS2_DBG_FRAGTREE2("insert frag (0x%04x-0x%04x)\n", newfrag->ofs, newfrag->ofs + newfrag->size);

	while (*link) {
		parent = *link;
		base = rb_entry(parent, struct jffs2_node_frag, rb);
	
		JFFS2_DBG_FRAGTREE2("considering frag at 0x%08x\n", base->ofs);
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

/* Doesn't set inode->i_size */
static int jffs2_add_frag_to_fragtree(struct jffs2_sb_info *c, struct rb_root *list, struct jffs2_node_frag *newfrag)
{
	struct jffs2_node_frag *this;
	uint32_t lastend;

	/* Skip all the nodes which are completed before this one starts */
	this = jffs2_lookup_node_frag(list, newfrag->node->ofs);

	if (this) {
		JFFS2_DBG_FRAGTREE2("lookup gave frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n",
			  this->ofs, this->ofs+this->size, this->node?(ref_offset(this->node->raw)):0xffffffff, this);
		lastend = this->ofs + this->size;
	} else {
		JFFS2_DBG_FRAGTREE2("lookup gave no frag\n");
		lastend = 0;
	}
			  
	/* See if we ran off the end of the list */
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

		if (lastend < newfrag->node->ofs) {
			/* ... and we need to put a hole in before the new node */
			struct jffs2_node_frag *holefrag = jffs2_alloc_node_frag();
			if (!holefrag) {
				jffs2_free_node_frag(newfrag);
				return -ENOMEM;
			}
			holefrag->ofs = lastend;
			holefrag->size = newfrag->node->ofs - lastend;
			holefrag->node = NULL;
			if (this) {
				/* By definition, the 'this' node has no right-hand child, 
				   because there are no frags with offset greater than it.
				   So that's where we want to put the hole */
				JFFS2_DBG_FRAGTREE2("adding hole frag (%p) on right of node at (%p)\n", holefrag, this);
				rb_link_node(&holefrag->rb, &this->rb, &this->rb.rb_right);
			} else {
				JFFS2_DBG_FRAGTREE2("adding hole frag (%p) at root of tree\n", holefrag);
				rb_link_node(&holefrag->rb, NULL, &list->rb_node);
			}
			rb_insert_color(&holefrag->rb, list);
			this = holefrag;
		}
		if (this) {
			/* By definition, the 'this' node has no right-hand child, 
			   because there are no frags with offset greater than it.
			   So that's where we want to put new fragment */
			JFFS2_DBG_FRAGTREE2("adding new frag (%p) on right of node at (%p)\n", newfrag, this);
			rb_link_node(&newfrag->rb, &this->rb, &this->rb.rb_right);			
		} else {
			JFFS2_DBG_FRAGTREE2("adding new frag (%p) at root of tree\n", newfrag);
			rb_link_node(&newfrag->rb, NULL, &list->rb_node);
		}
		rb_insert_color(&newfrag->rb, list);
		return 0;
	}

	JFFS2_DBG_FRAGTREE2("dealing with frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n", 
		  this->ofs, this->ofs+this->size, this->node?(ref_offset(this->node->raw)):0xffffffff, this);

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
			struct jffs2_node_frag *newfrag2 = jffs2_alloc_node_frag();
			if (!newfrag2) {
				jffs2_free_node_frag(newfrag);
				return -ENOMEM;
			}
			if (this->node)
				JFFS2_DBG_FRAGTREE2("split old frag 0x%04x-0x%04x, phys 0x%08x\n",
					this->ofs, this->ofs+this->size, ref_offset(this->node->raw));
			else 
				JFFS2_DBG_FRAGTREE2("split old hole frag 0x%04x-0x%04x\n",
					this->ofs, this->ofs+this->size, ref_offset(this->node->raw));
			
			/* New second frag pointing to this's node */
			newfrag2->ofs = newfrag->ofs + newfrag->size;
			newfrag2->size = (this->ofs+this->size) - newfrag2->ofs;
			newfrag2->node = this->node;
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
			rb_insert_color(&newfrag->rb, list);
			
			jffs2_fragtree_insert(newfrag2, newfrag);
			rb_insert_color(&newfrag2->rb, list);
			
			return 0;
		}
		/* New node just reduces 'this' frag in size, doesn't split it */
		this->size = newfrag->ofs - this->ofs;

		/* Again, we know it lives down here in the tree */
		jffs2_fragtree_insert(newfrag, this);
		rb_insert_color(&newfrag->rb, list);
	} else {
		/* New frag starts at the same point as 'this' used to. Replace 
		   it in the tree without doing a delete and insertion */
		JFFS2_DBG_FRAGTREE2("inserting newfrag (*%p),%d-%d in before 'this' (*%p),%d-%d\n",
			  newfrag, newfrag->ofs, newfrag->ofs+newfrag->size, this, this->ofs, this->ofs+this->size);
	
		rb_replace_node(&this->rb, &newfrag->rb, list);
		
		if (newfrag->ofs + newfrag->size >= this->ofs+this->size) {
			JFFS2_DBG_FRAGTREE2("obsoleting node frag %p (%x-%x)\n", this, this->ofs, this->ofs+this->size);
			jffs2_obsolete_node_frag(c, this);
		} else {
			this->ofs += newfrag->size;
			this->size -= newfrag->size;

			jffs2_fragtree_insert(this, newfrag);
			rb_insert_color(&this->rb, list);
			return 0;
		}
	}
	/* OK, now we have newfrag added in the correct place in the tree, but
	   frag_next(newfrag) may be a fragment which is overlapped by it 
	*/
	while ((this = frag_next(newfrag)) && newfrag->ofs + newfrag->size >= this->ofs + this->size) {
		/* 'this' frag is obsoleted completely. */
		JFFS2_DBG_FRAGTREE2("obsoleting node frag %p (%x-%x) and removing from tree\n",
			this, this->ofs, this->ofs+this->size);
		rb_erase(&this->rb, list);
		jffs2_obsolete_node_frag(c, this);
	}
	/* Now we're pointing at the first frag which isn't totally obsoleted by 
	   the new frag */

	if (!this || newfrag->ofs + newfrag->size == this->ofs) {
		return 0;
	}
	/* Still some overlap but we don't need to move it in the tree */
	this->size = (this->ofs + this->size) - (newfrag->ofs + newfrag->size);
	this->ofs = newfrag->ofs + newfrag->size;

	/* And mark them REF_NORMAL so the GC takes a look at them */
	if (this->node)
		mark_ref_normal(this->node->raw);
	mark_ref_normal(newfrag->node->raw);

	return 0;
}

/* Given an inode, probably with existing list of fragments, add the new node
 * to the fragment list.
 */
int jffs2_add_full_dnode_to_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_full_dnode *fn)
{
	int ret;
	struct jffs2_node_frag *newfrag;

	if (unlikely(!fn->size))
		return 0;

	newfrag = jffs2_alloc_node_frag();
	if (unlikely(!newfrag))
		return -ENOMEM;

	JFFS2_DBG_FRAGTREE("adding node %#04x-%#04x @0x%08x on flash, newfrag *%p\n",
		  fn->ofs, fn->ofs+fn->size, ref_offset(fn->raw), newfrag);
	
	newfrag->ofs = fn->ofs;
	newfrag->size = fn->size;
	newfrag->node = fn;
	newfrag->node->frags = 1;

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
	jffs2_dbg_dump_fragtree_nolock(f);
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

	JFFS2_DBG_INOCACHE("add %p (ino #%u)\n", new, new->ino);

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

	JFFS2_DBG_INOCACHE("del %p (ino #%u)\n", old, old->ino);
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

	JFFS2_DBG_FRAGTREE2("root %p, offset %d\n", fragtree, offset);

	next = fragtree->rb_node;

	while(next) {
		frag = rb_entry(next, struct jffs2_node_frag, rb);

		JFFS2_DBG_FRAGTREE2("considering frag %#04x-%#04x (%p). left %p, right %p\n",
			  frag->ofs, frag->ofs+frag->size, frag, frag->rb.rb_left, frag->rb.rb_right);
		if (frag->ofs + frag->size <= offset) {
			JFFS2_DBG_FRAGTREE2("going right from frag %#04x-%#04x, before the region we care about\n",
				  frag->ofs, frag->ofs+frag->size);
			/* Remember the closest smaller match on the way down */
			if (!prev || frag->ofs > prev->ofs)
				prev = frag;
			next = frag->rb.rb_right;
		} else if (frag->ofs > offset) {
			JFFS2_DBG_FRAGTREE2("going left from frag %#04x-%#04x, after the region we care about\n",
				  frag->ofs, frag->ofs+frag->size);
			next = frag->rb.rb_left;
		} else {
			JFFS2_DBG_FRAGTREE2("returning frag %#04x-%#04x, matched\n",
				  frag->ofs, frag->ofs+frag->size);
			return frag;
		}
	}

	/* Exact match not found. Go back up looking at each parent,
	   and return the closest smaller one */

	if (prev)
		JFFS2_DBG_FRAGTREE2("no match. Returning frag %#04x-%#04x, closest previous\n",
			  prev->ofs, prev->ofs+prev->size);
	else 
		JFFS2_DBG_FRAGTREE2("returning NULL, empty fragtree\n");
	
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

	JFFS2_DBG_FRAGTREE("killing\n");
	
	frag = (rb_entry(root->rb_node, struct jffs2_node_frag, rb));
	while(frag) {
		if (frag->rb.rb_left) {
			JFFS2_DBG_FRAGTREE2("going left from frag (%p) %#04x-%#04x\n",
				frag, frag->ofs, frag->ofs+frag->size);
			frag = frag_left(frag);
			continue;
		}
		if (frag->rb.rb_right) {
			JFFS2_DBG_FRAGTREE2("going right from frag (%p) %#04x-%#04x\n", 
				  frag, frag->ofs, frag->ofs+frag->size);
			frag = frag_right(frag);
			continue;
		}

		JFFS2_DBG_FRAGTREE2("frag %#04x-%#04x: node %p, frags %d\n",
			  frag->ofs, frag->ofs+frag->size, frag->node, frag->node?frag->node->frags:0);
			
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
