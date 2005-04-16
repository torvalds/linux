/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: readinode.c,v 1.117 2004/11/20 18:06:54 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/pagemap.h>
#include <linux/mtd/mtd.h>
#include <linux/compiler.h>
#include "nodelist.h"

static int jffs2_add_frag_to_fragtree(struct jffs2_sb_info *c, struct rb_root *list, struct jffs2_node_frag *newfrag);

#if CONFIG_JFFS2_FS_DEBUG >= 2
static void jffs2_print_fragtree(struct rb_root *list, int permitbug)
{
	struct jffs2_node_frag *this = frag_first(list);
	uint32_t lastofs = 0;
	int buggy = 0;

	while(this) {
		if (this->node)
			printk(KERN_DEBUG "frag %04x-%04x: 0x%08x(%d) on flash (*%p). left (%p), right (%p), parent (%p)\n",
			       this->ofs, this->ofs+this->size, ref_offset(this->node->raw), ref_flags(this->node->raw),
			       this, frag_left(this), frag_right(this), frag_parent(this));
		else 
			printk(KERN_DEBUG "frag %04x-%04x: hole (*%p). left (%p} right (%p), parent (%p)\n", this->ofs, 
			       this->ofs+this->size, this, frag_left(this), frag_right(this), frag_parent(this));
		if (this->ofs != lastofs)
			buggy = 1;
		lastofs = this->ofs+this->size;
		this = frag_next(this);
	}
	if (buggy && !permitbug) {
		printk(KERN_CRIT "Frag tree got a hole in it\n");
		BUG();
	}
}

void jffs2_print_frag_list(struct jffs2_inode_info *f)
{
	jffs2_print_fragtree(&f->fragtree, 0);

	if (f->metadata) {
		printk(KERN_DEBUG "metadata at 0x%08x\n", ref_offset(f->metadata->raw));
	}
}
#endif

#if CONFIG_JFFS2_FS_DEBUG >= 1
static int jffs2_sanitycheck_fragtree(struct jffs2_inode_info *f)
{
	struct jffs2_node_frag *frag;
	int bitched = 0;

	for (frag = frag_first(&f->fragtree); frag; frag = frag_next(frag)) {

		struct jffs2_full_dnode *fn = frag->node;
		if (!fn || !fn->raw)
			continue;

		if (ref_flags(fn->raw) == REF_PRISTINE) {

			if (fn->frags > 1) {
				printk(KERN_WARNING "REF_PRISTINE node at 0x%08x had %d frags. Tell dwmw2\n", ref_offset(fn->raw), fn->frags);
				bitched = 1;
			}
			/* A hole node which isn't multi-page should be garbage-collected
			   and merged anyway, so we just check for the frag size here,
			   rather than mucking around with actually reading the node
			   and checking the compression type, which is the real way
			   to tell a hole node. */
			if (frag->ofs & (PAGE_CACHE_SIZE-1) && frag_prev(frag) && frag_prev(frag)->size < PAGE_CACHE_SIZE && frag_prev(frag)->node) {
				printk(KERN_WARNING "REF_PRISTINE node at 0x%08x had a previous non-hole frag in the same page. Tell dwmw2\n",
				       ref_offset(fn->raw));
				bitched = 1;
			}

			if ((frag->ofs+frag->size) & (PAGE_CACHE_SIZE-1) && frag_next(frag) && frag_next(frag)->size < PAGE_CACHE_SIZE && frag_next(frag)->node) {
				printk(KERN_WARNING "REF_PRISTINE node at 0x%08x (%08x-%08x) had a following non-hole frag in the same page. Tell dwmw2\n",
				       ref_offset(fn->raw), frag->ofs, frag->ofs+frag->size);
				bitched = 1;
			}
		}
	}
	
	if (bitched) {
		struct jffs2_node_frag *thisfrag;

		printk(KERN_WARNING "Inode is #%u\n", f->inocache->ino);
		thisfrag = frag_first(&f->fragtree);
		while (thisfrag) {
			if (!thisfrag->node) {
				printk("Frag @0x%x-0x%x; node-less hole\n",
				       thisfrag->ofs, thisfrag->size + thisfrag->ofs);
			} else if (!thisfrag->node->raw) {
				printk("Frag @0x%x-0x%x; raw-less hole\n",
				       thisfrag->ofs, thisfrag->size + thisfrag->ofs);
			} else {
				printk("Frag @0x%x-0x%x; raw at 0x%08x(%d) (0x%x-0x%x)\n",
				       thisfrag->ofs, thisfrag->size + thisfrag->ofs,
				       ref_offset(thisfrag->node->raw), ref_flags(thisfrag->node->raw),
				       thisfrag->node->ofs, thisfrag->node->ofs+thisfrag->node->size);
			}
			thisfrag = frag_next(thisfrag);
		}
	}
	return bitched;
}
#endif /* D1 */

static void jffs2_obsolete_node_frag(struct jffs2_sb_info *c, struct jffs2_node_frag *this)
{
	if (this->node) {
		this->node->frags--;
		if (!this->node->frags) {
			/* The node has no valid frags left. It's totally obsoleted */
			D2(printk(KERN_DEBUG "Marking old node @0x%08x (0x%04x-0x%04x) obsolete\n",
				  ref_offset(this->node->raw), this->node->ofs, this->node->ofs+this->node->size));
			jffs2_mark_node_obsolete(c, this->node->raw);
			jffs2_free_full_dnode(this->node);
		} else {
			D2(printk(KERN_DEBUG "Marking old node @0x%08x (0x%04x-0x%04x) REF_NORMAL. frags is %d\n",
				  ref_offset(this->node->raw), this->node->ofs, this->node->ofs+this->node->size,
				  this->node->frags));
			mark_ref_normal(this->node->raw);
		}
		
	}
	jffs2_free_node_frag(this);
}

/* Given an inode, probably with existing list of fragments, add the new node
 * to the fragment list.
 */
int jffs2_add_full_dnode_to_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, struct jffs2_full_dnode *fn)
{
	int ret;
	struct jffs2_node_frag *newfrag;

	D1(printk(KERN_DEBUG "jffs2_add_full_dnode_to_inode(ino #%u, f %p, fn %p)\n", f->inocache->ino, f, fn));

	newfrag = jffs2_alloc_node_frag();
	if (unlikely(!newfrag))
		return -ENOMEM;

	D2(printk(KERN_DEBUG "adding node %04x-%04x @0x%08x on flash, newfrag *%p\n",
		  fn->ofs, fn->ofs+fn->size, ref_offset(fn->raw), newfrag));
	
	if (unlikely(!fn->size)) {
		jffs2_free_node_frag(newfrag);
		return 0;
	}

	newfrag->ofs = fn->ofs;
	newfrag->size = fn->size;
	newfrag->node = fn;
	newfrag->node->frags = 1;

	ret = jffs2_add_frag_to_fragtree(c, &f->fragtree, newfrag);
	if (ret)
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
	D2(if (jffs2_sanitycheck_fragtree(f)) {
		   printk(KERN_WARNING "Just added node %04x-%04x @0x%08x on flash, newfrag *%p\n",
			  fn->ofs, fn->ofs+fn->size, ref_offset(fn->raw), newfrag);
		   return 0;
	   })
	D2(jffs2_print_frag_list(f));
	return 0;
}

/* Doesn't set inode->i_size */
static int jffs2_add_frag_to_fragtree(struct jffs2_sb_info *c, struct rb_root *list, struct jffs2_node_frag *newfrag)
{
	struct jffs2_node_frag *this;
	uint32_t lastend;

	/* Skip all the nodes which are completed before this one starts */
	this = jffs2_lookup_node_frag(list, newfrag->node->ofs);

	if (this) {
		D2(printk(KERN_DEBUG "j_a_f_d_t_f: Lookup gave frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n",
			  this->ofs, this->ofs+this->size, this->node?(ref_offset(this->node->raw)):0xffffffff, this));
		lastend = this->ofs + this->size;
	} else {
		D2(printk(KERN_DEBUG "j_a_f_d_t_f: Lookup gave no frag\n"));
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
				D2(printk(KERN_DEBUG "Adding hole frag (%p) on right of node at (%p)\n", holefrag, this));
				rb_link_node(&holefrag->rb, &this->rb, &this->rb.rb_right);
			} else {
				D2(printk(KERN_DEBUG "Adding hole frag (%p) at root of tree\n", holefrag));
				rb_link_node(&holefrag->rb, NULL, &list->rb_node);
			}
			rb_insert_color(&holefrag->rb, list);
			this = holefrag;
		}
		if (this) {
			/* By definition, the 'this' node has no right-hand child, 
			   because there are no frags with offset greater than it.
			   So that's where we want to put the hole */
			D2(printk(KERN_DEBUG "Adding new frag (%p) on right of node at (%p)\n", newfrag, this));
			rb_link_node(&newfrag->rb, &this->rb, &this->rb.rb_right);			
		} else {
			D2(printk(KERN_DEBUG "Adding new frag (%p) at root of tree\n", newfrag));
			rb_link_node(&newfrag->rb, NULL, &list->rb_node);
		}
		rb_insert_color(&newfrag->rb, list);
		return 0;
	}

	D2(printk(KERN_DEBUG "j_a_f_d_t_f: dealing with frag 0x%04x-0x%04x; phys 0x%08x (*%p)\n", 
		  this->ofs, this->ofs+this->size, this->node?(ref_offset(this->node->raw)):0xffffffff, this));

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
			D2(printk(KERN_DEBUG "split old frag 0x%04x-0x%04x -->", this->ofs, this->ofs+this->size);
			if (this->node)
				printk("phys 0x%08x\n", ref_offset(this->node->raw));
			else 
				printk("hole\n");
			   )
			
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
		D2(printk(KERN_DEBUG "Inserting newfrag (*%p),%d-%d in before 'this' (*%p),%d-%d\n",
			  newfrag, newfrag->ofs, newfrag->ofs+newfrag->size,
			  this, this->ofs, this->ofs+this->size));
	
		rb_replace_node(&this->rb, &newfrag->rb, list);
		
		if (newfrag->ofs + newfrag->size >= this->ofs+this->size) {
			D2(printk(KERN_DEBUG "Obsoleting node frag %p (%x-%x)\n", this, this->ofs, this->ofs+this->size));
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
		D2(printk(KERN_DEBUG "Obsoleting node frag %p (%x-%x) and removing from tree\n", this, this->ofs, this->ofs+this->size));
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

void jffs2_truncate_fraglist (struct jffs2_sb_info *c, struct rb_root *list, uint32_t size)
{
	struct jffs2_node_frag *frag = jffs2_lookup_node_frag(list, size);

	D1(printk(KERN_DEBUG "Truncating fraglist to 0x%08x bytes\n", size));

	/* We know frag->ofs <= size. That's what lookup does for us */
	if (frag && frag->ofs != size) {
		if (frag->ofs+frag->size >= size) {
			D1(printk(KERN_DEBUG "Truncating frag 0x%08x-0x%08x\n", frag->ofs, frag->ofs+frag->size));
			frag->size = size - frag->ofs;
		}
		frag = frag_next(frag);
	}
	while (frag && frag->ofs >= size) {
		struct jffs2_node_frag *next = frag_next(frag);

		D1(printk(KERN_DEBUG "Removing frag 0x%08x-0x%08x\n", frag->ofs, frag->ofs+frag->size));
		frag_erase(frag, list);
		jffs2_obsolete_node_frag(c, frag);
		frag = next;
	}
}

/* Scan the list of all nodes present for this ino, build map of versions, etc. */

static int jffs2_do_read_inode_internal(struct jffs2_sb_info *c, 
					struct jffs2_inode_info *f,
					struct jffs2_raw_inode *latest_node);

int jffs2_do_read_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f, 
			uint32_t ino, struct jffs2_raw_inode *latest_node)
{
	D2(printk(KERN_DEBUG "jffs2_do_read_inode(): getting inocache\n"));

 retry_inocache:
	spin_lock(&c->inocache_lock);
	f->inocache = jffs2_get_ino_cache(c, ino);

	D2(printk(KERN_DEBUG "jffs2_do_read_inode(): Got inocache at %p\n", f->inocache));

	if (f->inocache) {
		/* Check its state. We may need to wait before we can use it */
		switch(f->inocache->state) {
		case INO_STATE_UNCHECKED:
		case INO_STATE_CHECKEDABSENT:
			f->inocache->state = INO_STATE_READING;
			break;
			
		case INO_STATE_CHECKING:
		case INO_STATE_GC:
			/* If it's in either of these states, we need
			   to wait for whoever's got it to finish and
			   put it back. */
			D1(printk(KERN_DEBUG "jffs2_get_ino_cache_read waiting for ino #%u in state %d\n",
				  ino, f->inocache->state));
			sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
			goto retry_inocache;

		case INO_STATE_READING:
		case INO_STATE_PRESENT:
			/* Eep. This should never happen. It can
			happen if Linux calls read_inode() again
			before clear_inode() has finished though. */
			printk(KERN_WARNING "Eep. Trying to read_inode #%u when it's already in state %d!\n", ino, f->inocache->state);
			/* Fail. That's probably better than allowing it to succeed */
			f->inocache = NULL;
			break;

		default:
			BUG();
		}
	}
	spin_unlock(&c->inocache_lock);

	if (!f->inocache && ino == 1) {
		/* Special case - no root inode on medium */
		f->inocache = jffs2_alloc_inode_cache();
		if (!f->inocache) {
			printk(KERN_CRIT "jffs2_do_read_inode(): Cannot allocate inocache for root inode\n");
			return -ENOMEM;
		}
		D1(printk(KERN_DEBUG "jffs2_do_read_inode(): Creating inocache for root inode\n"));
		memset(f->inocache, 0, sizeof(struct jffs2_inode_cache));
		f->inocache->ino = f->inocache->nlink = 1;
		f->inocache->nodes = (struct jffs2_raw_node_ref *)f->inocache;
		f->inocache->state = INO_STATE_READING;
		jffs2_add_ino_cache(c, f->inocache);
	}
	if (!f->inocache) {
		printk(KERN_WARNING "jffs2_do_read_inode() on nonexistent ino %u\n", ino);
		return -ENOENT;
	}

	return jffs2_do_read_inode_internal(c, f, latest_node);
}

int jffs2_do_crccheck_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic)
{
	struct jffs2_raw_inode n;
	struct jffs2_inode_info *f = kmalloc(sizeof(*f), GFP_KERNEL);
	int ret;

	if (!f)
		return -ENOMEM;

	memset(f, 0, sizeof(*f));
	init_MUTEX_LOCKED(&f->sem);
	f->inocache = ic;

	ret = jffs2_do_read_inode_internal(c, f, &n);
	if (!ret) {
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
	}
	kfree (f);
	return ret;
}

static int jffs2_do_read_inode_internal(struct jffs2_sb_info *c, 
					struct jffs2_inode_info *f,
					struct jffs2_raw_inode *latest_node)
{
	struct jffs2_tmp_dnode_info *tn_list, *tn;
	struct jffs2_full_dirent *fd_list;
	struct jffs2_full_dnode *fn = NULL;
	uint32_t crc;
	uint32_t latest_mctime, mctime_ver;
	uint32_t mdata_ver = 0;
	size_t retlen;
	int ret;

	D1(printk(KERN_DEBUG "jffs2_do_read_inode_internal(): ino #%u nlink is %d\n", f->inocache->ino, f->inocache->nlink));

	/* Grab all nodes relevant to this ino */
	ret = jffs2_get_inode_nodes(c, f, &tn_list, &fd_list, &f->highest_version, &latest_mctime, &mctime_ver);

	if (ret) {
		printk(KERN_CRIT "jffs2_get_inode_nodes() for ino %u returned %d\n", f->inocache->ino, ret);
		if (f->inocache->state == INO_STATE_READING)
			jffs2_set_inocache_state(c, f->inocache, INO_STATE_CHECKEDABSENT);
		return ret;
	}
	f->dents = fd_list;

	while (tn_list) {
		tn = tn_list;

		fn = tn->fn;

		if (f->metadata) {
			if (likely(tn->version >= mdata_ver)) {
				D1(printk(KERN_DEBUG "Obsoleting old metadata at 0x%08x\n", ref_offset(f->metadata->raw)));
				jffs2_mark_node_obsolete(c, f->metadata->raw);
				jffs2_free_full_dnode(f->metadata);
				f->metadata = NULL;
				
				mdata_ver = 0;
			} else {
				/* This should never happen. */
				printk(KERN_WARNING "Er. New metadata at 0x%08x with ver %d is actually older than previous ver %d at 0x%08x\n",
					  ref_offset(fn->raw), tn->version, mdata_ver, ref_offset(f->metadata->raw));
				jffs2_mark_node_obsolete(c, fn->raw);
				jffs2_free_full_dnode(fn);
				/* Fill in latest_node from the metadata, not this one we're about to free... */
				fn = f->metadata;
				goto next_tn;
			}
		}

		if (fn->size) {
			jffs2_add_full_dnode_to_inode(c, f, fn);
		} else {
			/* Zero-sized node at end of version list. Just a metadata update */
			D1(printk(KERN_DEBUG "metadata @%08x: ver %d\n", ref_offset(fn->raw), tn->version));
			f->metadata = fn;
			mdata_ver = tn->version;
		}
	next_tn:
		tn_list = tn->next;
		jffs2_free_tmp_dnode_info(tn);
	}
	D1(jffs2_sanitycheck_fragtree(f));

	if (!fn) {
		/* No data nodes for this inode. */
		if (f->inocache->ino != 1) {
			printk(KERN_WARNING "jffs2_do_read_inode(): No data nodes found for ino #%u\n", f->inocache->ino);
			if (!fd_list) {
				if (f->inocache->state == INO_STATE_READING)
					jffs2_set_inocache_state(c, f->inocache, INO_STATE_CHECKEDABSENT);
				return -EIO;
			}
			printk(KERN_WARNING "jffs2_do_read_inode(): But it has children so we fake some modes for it\n");
		}
		latest_node->mode = cpu_to_jemode(S_IFDIR|S_IRUGO|S_IWUSR|S_IXUGO);
		latest_node->version = cpu_to_je32(0);
		latest_node->atime = latest_node->ctime = latest_node->mtime = cpu_to_je32(0);
		latest_node->isize = cpu_to_je32(0);
		latest_node->gid = cpu_to_je16(0);
		latest_node->uid = cpu_to_je16(0);
		if (f->inocache->state == INO_STATE_READING)
			jffs2_set_inocache_state(c, f->inocache, INO_STATE_PRESENT);
		return 0;
	}

	ret = jffs2_flash_read(c, ref_offset(fn->raw), sizeof(*latest_node), &retlen, (void *)latest_node);
	if (ret || retlen != sizeof(*latest_node)) {
		printk(KERN_NOTICE "MTD read in jffs2_do_read_inode() failed: Returned %d, %zd of %zd bytes read\n",
		       ret, retlen, sizeof(*latest_node));
		/* FIXME: If this fails, there seems to be a memory leak. Find it. */
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
		return ret?ret:-EIO;
	}

	crc = crc32(0, latest_node, sizeof(*latest_node)-8);
	if (crc != je32_to_cpu(latest_node->node_crc)) {
		printk(KERN_NOTICE "CRC failed for read_inode of inode %u at physical location 0x%x\n", f->inocache->ino, ref_offset(fn->raw));
		up(&f->sem);
		jffs2_do_clear_inode(c, f);
		return -EIO;
	}

	switch(jemode_to_cpu(latest_node->mode) & S_IFMT) {
	case S_IFDIR:
		if (mctime_ver > je32_to_cpu(latest_node->version)) {
			/* The times in the latest_node are actually older than
			   mctime in the latest dirent. Cheat. */
			latest_node->ctime = latest_node->mtime = cpu_to_je32(latest_mctime);
		}
		break;

			
	case S_IFREG:
		/* If it was a regular file, truncate it to the latest node's isize */
		jffs2_truncate_fraglist(c, &f->fragtree, je32_to_cpu(latest_node->isize));
		break;

	case S_IFLNK:
		/* Hack to work around broken isize in old symlink code.
		   Remove this when dwmw2 comes to his senses and stops
		   symlinks from being an entirely gratuitous special
		   case. */
		if (!je32_to_cpu(latest_node->isize))
			latest_node->isize = latest_node->dsize;
		/* fall through... */

	case S_IFBLK:
	case S_IFCHR:
		/* Certain inode types should have only one data node, and it's
		   kept as the metadata node */
		if (f->metadata) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o had metadata node\n",
			       f->inocache->ino, jemode_to_cpu(latest_node->mode));
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		if (!frag_first(&f->fragtree)) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0%o has no fragments\n",
			       f->inocache->ino, jemode_to_cpu(latest_node->mode));
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* ASSERT: f->fraglist != NULL */
		if (frag_next(frag_first(&f->fragtree))) {
			printk(KERN_WARNING "Argh. Special inode #%u with mode 0x%x had more than one node\n",
			       f->inocache->ino, jemode_to_cpu(latest_node->mode));
			/* FIXME: Deal with it - check crc32, check for duplicate node, check times and discard the older one */
			up(&f->sem);
			jffs2_do_clear_inode(c, f);
			return -EIO;
		}
		/* OK. We're happy */
		f->metadata = frag_first(&f->fragtree)->node;
		jffs2_free_node_frag(frag_first(&f->fragtree));
		f->fragtree = RB_ROOT;
		break;
	}
	if (f->inocache->state == INO_STATE_READING)
		jffs2_set_inocache_state(c, f->inocache, INO_STATE_PRESENT);

	return 0;
}

void jffs2_do_clear_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f)
{
	struct jffs2_full_dirent *fd, *fds;
	int deleted;

	down(&f->sem);
	deleted = f->inocache && !f->inocache->nlink;

	if (f->metadata) {
		if (deleted)
			jffs2_mark_node_obsolete(c, f->metadata->raw);
		jffs2_free_full_dnode(f->metadata);
	}

	jffs2_kill_fragtree(&f->fragtree, deleted?c:NULL);

	fds = f->dents;

	while(fds) {
		fd = fds;
		fds = fd->next;
		jffs2_free_full_dirent(fd);
	}

	if (f->inocache && f->inocache->state != INO_STATE_CHECKING)
		jffs2_set_inocache_state(c, f->inocache, INO_STATE_CHECKEDABSENT);

	up(&f->sem);
}
