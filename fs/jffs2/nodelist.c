/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: nodelist.c,v 1.100 2005/07/22 10:32:08 dedekind Exp $
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
	D1(printk(KERN_DEBUG "jffs2_add_fd_to_list( %p, %p (->%p))\n", new, list, *list));

	while ((*prev) && (*prev)->nhash <= new->nhash) {
		if ((*prev)->nhash == new->nhash && !strcmp((*prev)->name, new->name)) {
			/* Duplicate. Free one */
			if (new->version < (*prev)->version) {
				D1(printk(KERN_DEBUG "Eep! Marking new dirent node obsolete\n"));
				D1(printk(KERN_DEBUG "New dirent is \"%s\"->ino #%u. Old is \"%s\"->ino #%u\n", new->name, new->ino, (*prev)->name, (*prev)->ino));
				jffs2_mark_node_obsolete(c, new->raw);
				jffs2_free_full_dirent(new);
			} else {
				D1(printk(KERN_DEBUG "Marking old dirent node (ino #%u) obsolete\n", (*prev)->ino));
				new->next = (*prev)->next;
				jffs2_mark_node_obsolete(c, ((*prev)->raw));
				jffs2_free_full_dirent(*prev);
				*prev = new;
			}
			goto out;
		}
		prev = &((*prev)->next);
	}
	new->next = *prev;
	*prev = new;

 out:
	D2(while(*list) {
		printk(KERN_DEBUG "Dirent \"%s\" (hash 0x%08x, ino #%u\n", (*list)->name, (*list)->nhash, (*list)->ino);
		list = &(*list)->next;
	});
}

/* 
 * Put a new tmp_dnode_info into the temporaty RB-tree, keeping the list in 
 * order of increasing version.
 */
static void jffs2_add_tn_to_tree(struct jffs2_tmp_dnode_info *tn, struct rb_root *list)
{
	struct rb_node **p = &list->rb_node;
	struct rb_node * parent = NULL;
	struct jffs2_tmp_dnode_info *this;

	while (*p) {
		parent = *p;
		this = rb_entry(parent, struct jffs2_tmp_dnode_info, rb);

		/* There may actually be a collision here, but it doesn't
		   actually matter. As long as the two nodes with the same
		   version are together, it's all fine. */
		if (tn->version < this->version)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
        }

	rb_link_node(&tn->rb, parent, p);
	rb_insert_color(&tn->rb, list);
}

static void jffs2_free_tmp_dnode_info_list(struct rb_root *list)
{
	struct rb_node *this;
	struct jffs2_tmp_dnode_info *tn;

	this = list->rb_node;

	/* Now at bottom of tree */
	while (this) {
		if (this->rb_left)
			this = this->rb_left;
		else if (this->rb_right)
			this = this->rb_right;
		else {
			tn = rb_entry(this, struct jffs2_tmp_dnode_info, rb);
			jffs2_free_full_dnode(tn->fn);
			jffs2_free_tmp_dnode_info(tn);

			this = this->rb_parent;
			if (!this)
				break;

			if (this->rb_left == &tn->rb)
				this->rb_left = NULL;
			else if (this->rb_right == &tn->rb)
				this->rb_right = NULL;
			else BUG();
		}
	}
	list->rb_node = NULL;
}

static void jffs2_free_full_dirent_list(struct jffs2_full_dirent *fd)
{
	struct jffs2_full_dirent *next;

	while (fd) {
		next = fd->next;
		jffs2_free_full_dirent(fd);
		fd = next;
	}
}

/* Returns first valid node after 'ref'. May return 'ref' */
static struct jffs2_raw_node_ref *jffs2_first_valid_node(struct jffs2_raw_node_ref *ref)
{
	while (ref && ref->next_in_ino) {
		if (!ref_obsolete(ref))
			return ref;
		D1(printk(KERN_DEBUG "node at 0x%08x is obsoleted. Ignoring.\n", ref_offset(ref)));
		ref = ref->next_in_ino;
	}
	return NULL;
}

/*
 * Helper function for jffs2_get_inode_nodes().
 * It is called every time an directory entry node is found.
 *
 * Returns: 0 on succes;
 * 	    1 if the node should be marked obsolete;
 * 	    negative error code on failure.
 */
static inline int
read_direntry(struct jffs2_sb_info *c,
	      struct jffs2_raw_node_ref *ref,
	      struct jffs2_raw_dirent *rd,
	      uint32_t read,
	      struct jffs2_full_dirent **fdp,
	      int32_t *latest_mctime,
	      uint32_t *mctime_ver)
{
	struct jffs2_full_dirent *fd;
	
	/* The direntry nodes are checked during the flash scanning */
	BUG_ON(ref_flags(ref) == REF_UNCHECKED);
	/* Obsoleted. This cannot happen, surely? dwmw2 20020308 */
	BUG_ON(ref_obsolete(ref));
			
	/* Sanity check */
	if (unlikely(PAD((rd->nsize + sizeof(*rd))) != PAD(je32_to_cpu(rd->totlen)))) {
		printk(KERN_ERR "Error! Illegal nsize in node at %#08x: nsize %#02x, totlen %#04x\n",
		       ref_offset(ref), rd->nsize, je32_to_cpu(rd->totlen));
		return 1;
	}
	
	fd = jffs2_alloc_full_dirent(rd->nsize + 1);
	if (unlikely(!fd))
		return -ENOMEM;

	fd->raw = ref;
	fd->version = je32_to_cpu(rd->version);
	fd->ino = je32_to_cpu(rd->ino);
	fd->type = rd->type;

	/* Pick out the mctime of the latest dirent */
	if(fd->version > *mctime_ver) {
		*mctime_ver = fd->version;
		*latest_mctime = je32_to_cpu(rd->mctime);
	}

	/* 
	 * Copy as much of the name as possible from the raw
	 * dirent we've already read from the flash.
	 */
	if (read > sizeof(*rd))
		memcpy(&fd->name[0], &rd->name[0],
		       min_t(uint32_t, rd->nsize, (read - sizeof(*rd)) ));
		
	/* Do we need to copy any more of the name directly from the flash? */
	if (rd->nsize + sizeof(*rd) > read) {
		/* FIXME: point() */
		int err;
		int already = read - sizeof(*rd);
			
		err = jffs2_flash_read(c, (ref_offset(ref)) + read, 
				rd->nsize - already, &read, &fd->name[already]);
		if (unlikely(read != rd->nsize - already) && likely(!err))
			return -EIO;
			
		if (unlikely(err)) {
			printk(KERN_WARNING "Read remainder of name: error %d\n", err);
			jffs2_free_full_dirent(fd);
			return -EIO;
		}
	}
	
	fd->nhash = full_name_hash(fd->name, rd->nsize);
	fd->next = NULL;
	fd->name[rd->nsize] = '\0';
	
	/*
	 * Wheee. We now have a complete jffs2_full_dirent structure, with
	 * the name in it and everything. Link it into the list 
	 */
	D1(printk(KERN_DEBUG "Adding fd \"%s\", ino #%u\n", fd->name, fd->ino));

	jffs2_add_fd_to_list(c, fd, fdp);

	return 0;
}

/*
 * Helper function for jffs2_get_inode_nodes().
 * It is called every time an inode node is found.
 *
 * Returns: 0 on succes;
 * 	    1 if the node should be marked obsolete;
 * 	    negative error code on failure.
 */
static inline int
read_dnode(struct jffs2_sb_info *c,
	   struct jffs2_raw_node_ref *ref,
	   struct jffs2_raw_inode *rd,
	   uint32_t read,
	   struct rb_root *tnp,
	   int32_t *latest_mctime,
	   uint32_t *mctime_ver)
{
	struct jffs2_eraseblock *jeb;
	struct jffs2_tmp_dnode_info *tn;
	
	/* Obsoleted. This cannot happen, surely? dwmw2 20020308 */
	BUG_ON(ref_obsolete(ref));

	/* If we've never checked the CRCs on this node, check them now */
	if (ref_flags(ref) == REF_UNCHECKED) {
		uint32_t crc, len;

		crc = crc32(0, rd, sizeof(*rd) - 8);
		if (unlikely(crc != je32_to_cpu(rd->node_crc))) {
			printk(KERN_WARNING "Header CRC failed on node at %#08x: read %#08x, calculated %#08x\n",
					ref_offset(ref), je32_to_cpu(rd->node_crc), crc);
			return 1;
		}
		
		/* Sanity checks */
		if (unlikely(je32_to_cpu(rd->offset) > je32_to_cpu(rd->isize)) ||
		    unlikely(PAD(je32_to_cpu(rd->csize) + sizeof(*rd)) != PAD(je32_to_cpu(rd->totlen)))) {
			printk(KERN_WARNING "Inode corrupted at %#08x, totlen %d, #ino  %d, version %d, "
				"isize %d, csize %d, dsize %d \n",
				ref_offset(ref),  je32_to_cpu(rd->totlen),  je32_to_cpu(rd->ino),
				je32_to_cpu(rd->version),  je32_to_cpu(rd->isize), 
				je32_to_cpu(rd->csize), je32_to_cpu(rd->dsize));
			return 1;
		}

		if (rd->compr != JFFS2_COMPR_ZERO && je32_to_cpu(rd->csize)) {
			unsigned char *buf = NULL;
			uint32_t pointed = 0;
			int err;
#ifndef __ECOS
			if (c->mtd->point) {
				err = c->mtd->point (c->mtd, ref_offset(ref) + sizeof(*rd), je32_to_cpu(rd->csize),
						     &read, &buf);
				if (unlikely(read < je32_to_cpu(rd->csize)) && likely(!err)) {
					D1(printk(KERN_DEBUG "MTD point returned len too short: 0x%zx\n", read));
					c->mtd->unpoint(c->mtd, buf, ref_offset(ref) + sizeof(*rd),
							je32_to_cpu(rd->csize));
				} else if (unlikely(err)){
					D1(printk(KERN_DEBUG "MTD point failed %d\n", err));
				} else
					pointed = 1; /* succefully pointed to device */
			}
#endif					
			if(!pointed){
				buf = kmalloc(je32_to_cpu(rd->csize), GFP_KERNEL);
				if (!buf)
					return -ENOMEM;
				
				err = jffs2_flash_read(c, ref_offset(ref) + sizeof(*rd), je32_to_cpu(rd->csize),
							&read, buf);
				if (unlikely(read != je32_to_cpu(rd->csize)) && likely(!err))
					err = -EIO;
				if (err) {
					kfree(buf);
					return err;
				}
			}
			crc = crc32(0, buf, je32_to_cpu(rd->csize));
			if(!pointed)
				kfree(buf);
#ifndef __ECOS
			else
				c->mtd->unpoint(c->mtd, buf, ref_offset(ref) + sizeof(*rd), je32_to_cpu(rd->csize));
#endif

			if (crc != je32_to_cpu(rd->data_crc)) {
				printk(KERN_NOTICE "Data CRC failed on node at %#08x: read %#08x, calculated %#08x\n",
				       ref_offset(ref), je32_to_cpu(rd->data_crc), crc);
				return 1;
			}
			
		}

		/* Mark the node as having been checked and fix the accounting accordingly */
		jeb = &c->blocks[ref->flash_offset / c->sector_size];
		len = ref_totlen(c, jeb, ref);

		spin_lock(&c->erase_completion_lock);
		jeb->used_size += len;
		jeb->unchecked_size -= len;
		c->used_size += len;
		c->unchecked_size -= len;

		/* If node covers at least a whole page, or if it starts at the 
		   beginning of a page and runs to the end of the file, or if 
		   it's a hole node, mark it REF_PRISTINE, else REF_NORMAL. 

		   If it's actually overlapped, it'll get made NORMAL (or OBSOLETE) 
		   when the overlapping node(s) get added to the tree anyway. 
		*/
		if ((je32_to_cpu(rd->dsize) >= PAGE_CACHE_SIZE) ||
		    ( ((je32_to_cpu(rd->offset) & (PAGE_CACHE_SIZE-1))==0) &&
		      (je32_to_cpu(rd->dsize) + je32_to_cpu(rd->offset) == je32_to_cpu(rd->isize)))) {
			D1(printk(KERN_DEBUG "Marking node at %#08x REF_PRISTINE\n", ref_offset(ref)));
			ref->flash_offset = ref_offset(ref) | REF_PRISTINE;
		} else {
			D1(printk(KERN_DEBUG "Marking node at %#08x REF_NORMAL\n", ref_offset(ref)));
			ref->flash_offset = ref_offset(ref) | REF_NORMAL;
		}
		spin_unlock(&c->erase_completion_lock);
	}

	tn = jffs2_alloc_tmp_dnode_info();
	if (!tn) {
		D1(printk(KERN_DEBUG "alloc tn failed\n"));
		return -ENOMEM;
	}

	tn->fn = jffs2_alloc_full_dnode();
	if (!tn->fn) {
		D1(printk(KERN_DEBUG "alloc fn failed\n"));
		jffs2_free_tmp_dnode_info(tn);
		return -ENOMEM;
	}
	
	tn->version = je32_to_cpu(rd->version);
	tn->fn->ofs = je32_to_cpu(rd->offset);
	tn->fn->raw = ref;
	
	/* There was a bug where we wrote hole nodes out with
	   csize/dsize swapped. Deal with it */
	if (rd->compr == JFFS2_COMPR_ZERO && !je32_to_cpu(rd->dsize) && je32_to_cpu(rd->csize))
		tn->fn->size = je32_to_cpu(rd->csize);
	else // normal case...
		tn->fn->size = je32_to_cpu(rd->dsize);

	D1(printk(KERN_DEBUG "dnode @%08x: ver %u, offset %#04x, dsize %#04x\n",
		  ref_offset(ref), je32_to_cpu(rd->version),
		  je32_to_cpu(rd->offset), je32_to_cpu(rd->dsize)));
	
	jffs2_add_tn_to_tree(tn, tnp);

	return 0;
}

/*
 * Helper function for jffs2_get_inode_nodes().
 * It is called every time an unknown node is found.
 *
 * Returns: 0 on succes;
 * 	    1 if the node should be marked obsolete;
 * 	    negative error code on failure.
 */
static inline int
read_unknown(struct jffs2_sb_info *c,
	     struct jffs2_raw_node_ref *ref,
	     struct jffs2_unknown_node *un,
	     uint32_t read)
{
	/* We don't mark unknown nodes as REF_UNCHECKED */
	BUG_ON(ref_flags(ref) == REF_UNCHECKED);
	
	un->nodetype = cpu_to_je16(JFFS2_NODE_ACCURATE | je16_to_cpu(un->nodetype));

	if (crc32(0, un, sizeof(struct jffs2_unknown_node) - 4) != je32_to_cpu(un->hdr_crc)) {

		/* Hmmm. This should have been caught at scan time. */
		printk(KERN_WARNING "Warning! Node header CRC failed at %#08x. "
				"But it must have been OK earlier.\n", ref_offset(ref));
		D1(printk(KERN_DEBUG "Node was: { %#04x, %#04x, %#08x, %#08x }\n", 
			je16_to_cpu(un->magic), je16_to_cpu(un->nodetype),
			je32_to_cpu(un->totlen), je32_to_cpu(un->hdr_crc)));
		return 1;
	} else {
		switch(je16_to_cpu(un->nodetype) & JFFS2_COMPAT_MASK) {

		case JFFS2_FEATURE_INCOMPAT:
			printk(KERN_NOTICE "Unknown INCOMPAT nodetype %#04X at %#08x\n",
					je16_to_cpu(un->nodetype), ref_offset(ref));
			/* EEP */
			BUG();
			break;

		case JFFS2_FEATURE_ROCOMPAT:
			printk(KERN_NOTICE "Unknown ROCOMPAT nodetype %#04X at %#08x\n",
					je16_to_cpu(un->nodetype), ref_offset(ref));
			BUG_ON(!(c->flags & JFFS2_SB_FLAG_RO));
			break;

		case JFFS2_FEATURE_RWCOMPAT_COPY:
			printk(KERN_NOTICE "Unknown RWCOMPAT_COPY nodetype %#04X at %#08x\n",
					je16_to_cpu(un->nodetype), ref_offset(ref));
			break;

		case JFFS2_FEATURE_RWCOMPAT_DELETE:
			printk(KERN_NOTICE "Unknown RWCOMPAT_DELETE nodetype %#04X at %#08x\n",
					je16_to_cpu(un->nodetype), ref_offset(ref));
			return 1;
		}
	}

	return 0;
}

/* Get tmp_dnode_info and full_dirent for all non-obsolete nodes associated
   with this ino, returning the former in order of version */

int jffs2_get_inode_nodes(struct jffs2_sb_info *c, struct jffs2_inode_info *f,
			  struct rb_root *tnp, struct jffs2_full_dirent **fdp,
			  uint32_t *highest_version, uint32_t *latest_mctime,
			  uint32_t *mctime_ver)
{
	struct jffs2_raw_node_ref *ref, *valid_ref;
	struct rb_root ret_tn = RB_ROOT;
	struct jffs2_full_dirent *ret_fd = NULL;
	union jffs2_node_union node;
	size_t retlen;
	int err;

	*mctime_ver = 0;
	
	D1(printk(KERN_DEBUG "jffs2_get_inode_nodes(): ino #%u\n", f->inocache->ino));

	spin_lock(&c->erase_completion_lock);

	valid_ref = jffs2_first_valid_node(f->inocache->nodes);

	if (!valid_ref && (f->inocache->ino != 1))
		printk(KERN_WARNING "Eep. No valid nodes for ino #%u\n", f->inocache->ino);

	while (valid_ref) {
		/* We can hold a pointer to a non-obsolete node without the spinlock,
		   but _obsolete_ nodes may disappear at any time, if the block
		   they're in gets erased. So if we mark 'ref' obsolete while we're
		   not holding the lock, it can go away immediately. For that reason,
		   we find the next valid node first, before processing 'ref'.
		*/
		ref = valid_ref;
		valid_ref = jffs2_first_valid_node(ref->next_in_ino);
		spin_unlock(&c->erase_completion_lock);

		cond_resched();

		/* FIXME: point() */
		err = jffs2_flash_read(c, (ref_offset(ref)), 
				       min_t(uint32_t, ref_totlen(c, NULL, ref), sizeof(node)),
				       &retlen, (void *)&node);
		if (err) {
			printk(KERN_WARNING "error %d reading node at 0x%08x in get_inode_nodes()\n", err, ref_offset(ref));
			goto free_out;
		}
			
		switch (je16_to_cpu(node.u.nodetype)) {
			
		case JFFS2_NODETYPE_DIRENT:
			D1(printk(KERN_DEBUG "Node at %08x (%d) is a dirent node\n", ref_offset(ref), ref_flags(ref)));
			
			if (retlen < sizeof(node.d)) {
				printk(KERN_WARNING "Warning! Short read dirent at %#08x\n", ref_offset(ref));
				err = -EIO;
				goto free_out;
			}

			err = read_direntry(c, ref, &node.d, retlen, &ret_fd, latest_mctime, mctime_ver);
			if (err == 1) {
				jffs2_mark_node_obsolete(c, ref);
				break;
			} else if (unlikely(err))
				goto free_out;
			
			if (je32_to_cpu(node.d.version) > *highest_version)
				*highest_version = je32_to_cpu(node.d.version);

			break;

		case JFFS2_NODETYPE_INODE:
			D1(printk(KERN_DEBUG "Node at %08x (%d) is a data node\n", ref_offset(ref), ref_flags(ref)));
			
			if (retlen < sizeof(node.i)) {
				printk(KERN_WARNING "Warning! Short read dnode at %#08x\n", ref_offset(ref));
				err = -EIO;
				goto free_out;
			}

			err = read_dnode(c, ref, &node.i, retlen, &ret_tn, latest_mctime, mctime_ver);
			if (err == 1) {
				jffs2_mark_node_obsolete(c, ref);
				break;
			} else if (unlikely(err))
				goto free_out;

			if (je32_to_cpu(node.i.version) > *highest_version)
				*highest_version = je32_to_cpu(node.i.version);
			
			D1(printk(KERN_DEBUG "version %d, highest_version now %d\n",
					je32_to_cpu(node.i.version), *highest_version));

			break;

		default:
			/* Check we've managed to read at least the common node header */
			if (retlen < sizeof(struct jffs2_unknown_node)) {
				printk(KERN_WARNING "Warning! Short read unknown node at %#08x\n",
						ref_offset(ref));
				return -EIO;
			}

			err = read_unknown(c, ref, &node.u, retlen);
			if (err == 1) {
				jffs2_mark_node_obsolete(c, ref);
				break;
			} else if (unlikely(err))
				goto free_out;

		}
		spin_lock(&c->erase_completion_lock);

	}
	spin_unlock(&c->erase_completion_lock);
	*tnp = ret_tn;
	*fdp = ret_fd;

	return 0;

 free_out:
	jffs2_free_tmp_dnode_info_list(&ret_tn);
	jffs2_free_full_dirent_list(ret_fd);
	return err;
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

	D2(printk(KERN_DEBUG "jffs2_get_ino_cache(): ino %u\n", ino));

	ret = c->inocache_list[ino % INOCACHE_HASHSIZE];
	while (ret && ret->ino < ino) {
		ret = ret->next;
	}
	
	if (ret && ret->ino != ino)
		ret = NULL;

	D2(printk(KERN_DEBUG "jffs2_get_ino_cache found %p for ino %u\n", ret, ino));
	return ret;
}

void jffs2_add_ino_cache (struct jffs2_sb_info *c, struct jffs2_inode_cache *new)
{
	struct jffs2_inode_cache **prev;

	spin_lock(&c->inocache_lock);
	if (!new->ino)
		new->ino = ++c->highest_ino;

	D2(printk(KERN_DEBUG "jffs2_add_ino_cache: Add %p (ino #%u)\n", new, new->ino));

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
	D1(printk(KERN_DEBUG "jffs2_del_ino_cache: Del %p (ino #%u)\n", old, old->ino));
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

	D2(printk(KERN_DEBUG "jffs2_lookup_node_frag(%p, %d)\n", fragtree, offset));

	next = fragtree->rb_node;

	while(next) {
		frag = rb_entry(next, struct jffs2_node_frag, rb);

		D2(printk(KERN_DEBUG "Considering frag %d-%d (%p). left %p, right %p\n",
			  frag->ofs, frag->ofs+frag->size, frag, frag->rb.rb_left, frag->rb.rb_right));
		if (frag->ofs + frag->size <= offset) {
			D2(printk(KERN_DEBUG "Going right from frag %d-%d, before the region we care about\n",
				  frag->ofs, frag->ofs+frag->size));
			/* Remember the closest smaller match on the way down */
			if (!prev || frag->ofs > prev->ofs)
				prev = frag;
			next = frag->rb.rb_right;
		} else if (frag->ofs > offset) {
			D2(printk(KERN_DEBUG "Going left from frag %d-%d, after the region we care about\n",
				  frag->ofs, frag->ofs+frag->size));
			next = frag->rb.rb_left;
		} else {
			D2(printk(KERN_DEBUG "Returning frag %d,%d, matched\n",
				  frag->ofs, frag->ofs+frag->size));
			return frag;
		}
	}

	/* Exact match not found. Go back up looking at each parent,
	   and return the closest smaller one */

	if (prev)
		D2(printk(KERN_DEBUG "No match. Returning frag %d,%d, closest previous\n",
			  prev->ofs, prev->ofs+prev->size));
	else 
		D2(printk(KERN_DEBUG "Returning NULL, empty fragtree\n"));
	
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

	frag = (rb_entry(root->rb_node, struct jffs2_node_frag, rb));

	while(frag) {
		if (frag->rb.rb_left) {
			D2(printk(KERN_DEBUG "Going left from frag (%p) %d-%d\n", 
				  frag, frag->ofs, frag->ofs+frag->size));
			frag = frag_left(frag);
			continue;
		}
		if (frag->rb.rb_right) {
			D2(printk(KERN_DEBUG "Going right from frag (%p) %d-%d\n", 
				  frag, frag->ofs, frag->ofs+frag->size));
			frag = frag_right(frag);
			continue;
		}

		D2(printk(KERN_DEBUG "jffs2_kill_fragtree: frag at 0x%x-0x%x: node %p, frags %d--\n",
			  frag->ofs, frag->ofs+frag->size, frag->node,
			  frag->node?frag->node->frags:0));
			
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

void jffs2_fragtree_insert(struct jffs2_node_frag *newfrag, struct jffs2_node_frag *base)
{
	struct rb_node *parent = &base->rb;
	struct rb_node **link = &parent;

	D2(printk(KERN_DEBUG "jffs2_fragtree_insert(%p; %d-%d, %p)\n", newfrag, 
		  newfrag->ofs, newfrag->ofs+newfrag->size, base));

	while (*link) {
		parent = *link;
		base = rb_entry(parent, struct jffs2_node_frag, rb);
	
		D2(printk(KERN_DEBUG "fragtree_insert considering frag at 0x%x\n", base->ofs));
		if (newfrag->ofs > base->ofs)
			link = &base->rb.rb_right;
		else if (newfrag->ofs < base->ofs)
			link = &base->rb.rb_left;
		else {
			printk(KERN_CRIT "Duplicate frag at %08x (%p,%p)\n", newfrag->ofs, newfrag, base);
			BUG();
		}
	}

	rb_link_node(&newfrag->rb, &base->rb, link);
}
