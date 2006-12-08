/*
 * JFFS -- Journaling Flash File System, Linux implementation.
 *
 * Copyright (C) 1999, 2000  Axis Communications AB.
 *
 * Created by Finn Hakansson <finn@axis.com>.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * $Id: jffs_fm.c,v 1.27 2001/09/20 12:29:47 dwmw2 Exp $
 *
 * Ported to Linux 2.3.x and MTD:
 * Copyright (C) 2000  Alexander Larsson (alex@cendio.se), Cendio Systems AB
 *
 */
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/jffs.h>
#include "jffs_fm.h"
#include "intrep.h"

#if defined(JFFS_MARK_OBSOLETE) && JFFS_MARK_OBSOLETE
static int jffs_mark_obsolete(struct jffs_fmcontrol *fmc, __u32 fm_offset);
#endif

static struct jffs_fm *jffs_alloc_fm(void);
static void jffs_free_fm(struct jffs_fm *n);

extern struct kmem_cache     *fm_cache;
extern struct kmem_cache     *node_cache;

#if CONFIG_JFFS_FS_VERBOSE > 0
void
jffs_print_fmcontrol(struct jffs_fmcontrol *fmc)
{
	D(printk("struct jffs_fmcontrol: 0x%p\n", fmc));
	D(printk("{\n"));
	D(printk("        %u, /* flash_size  */\n", fmc->flash_size));
	D(printk("        %u, /* used_size  */\n", fmc->used_size));
	D(printk("        %u, /* dirty_size  */\n", fmc->dirty_size));
	D(printk("        %u, /* free_size  */\n", fmc->free_size));
	D(printk("        %u, /* sector_size  */\n", fmc->sector_size));
	D(printk("        %u, /* min_free_size  */\n", fmc->min_free_size));
	D(printk("        %u, /* max_chunk_size  */\n", fmc->max_chunk_size));
	D(printk("        0x%p, /* mtd  */\n", fmc->mtd));
	D(printk("        0x%p, /* head  */    "
		 "(head->offset = 0x%08x)\n",
		 fmc->head, (fmc->head ? fmc->head->offset : 0)));
	D(printk("        0x%p, /* tail  */    "
		 "(tail->offset + tail->size = 0x%08x)\n",
		 fmc->tail,
		 (fmc->tail ? fmc->tail->offset + fmc->tail->size : 0)));
	D(printk("        0x%p, /* head_extra  */\n", fmc->head_extra));
	D(printk("        0x%p, /* tail_extra  */\n", fmc->tail_extra));
	D(printk("}\n"));
}
#endif  /*  CONFIG_JFFS_FS_VERBOSE > 0  */

#if CONFIG_JFFS_FS_VERBOSE > 2
static void
jffs_print_fm(struct jffs_fm *fm)
{
	D(printk("struct jffs_fm: 0x%p\n", fm));
	D(printk("{\n"));
	D(printk("       0x%08x, /* offset  */\n", fm->offset));
	D(printk("       %u, /* size  */\n", fm->size));
	D(printk("       0x%p, /* prev  */\n", fm->prev));
	D(printk("       0x%p, /* next  */\n", fm->next));
	D(printk("       0x%p, /* nodes  */\n", fm->nodes));
	D(printk("}\n"));
}
#endif  /*  CONFIG_JFFS_FS_VERBOSE > 2  */

#if 0
void
jffs_print_node_ref(struct jffs_node_ref *ref)
{
	D(printk("struct jffs_node_ref: 0x%p\n", ref));
	D(printk("{\n"));
	D(printk("       0x%p, /* node  */\n", ref->node));
	D(printk("       0x%p, /* next  */\n", ref->next));
	D(printk("}\n"));
}
#endif  /*  0  */

/* This function creates a new shiny flash memory control structure.  */
struct jffs_fmcontrol *
jffs_build_begin(struct jffs_control *c, int unit)
{
	struct jffs_fmcontrol *fmc;
	struct mtd_info *mtd;
	
	D3(printk("jffs_build_begin()\n"));
	fmc = kmalloc(sizeof(*fmc), GFP_KERNEL);
	if (!fmc) {
		D(printk("jffs_build_begin(): Allocation of "
			 "struct jffs_fmcontrol failed!\n"));
		return (struct jffs_fmcontrol *)0;
	}
	DJM(no_jffs_fmcontrol++);

	mtd = get_mtd_device(NULL, unit);

	if (!mtd) {
		kfree(fmc);
		DJM(no_jffs_fmcontrol--);
		return NULL;
	}
	
	/* Retrieve the size of the flash memory.  */
	fmc->flash_size = mtd->size;
	D3(printk("  fmc->flash_size = %d bytes\n", fmc->flash_size));

	fmc->used_size = 0;
	fmc->dirty_size = 0;
	fmc->free_size = mtd->size;
	fmc->sector_size = mtd->erasesize;
	fmc->max_chunk_size = fmc->sector_size >> 1;
	/* min_free_size:
	   1 sector, obviously.
	   + 1 x max_chunk_size, for when a nodes overlaps the end of a sector
	   + 1 x max_chunk_size again, which ought to be enough to handle 
		   the case where a rename causes a name to grow, and GC has
		   to write out larger nodes than the ones it's obsoleting.
		   We should fix it so it doesn't have to write the name
		   _every_ time. Later.
	   + another 2 sectors because people keep getting GC stuck and
	           we don't know why. This scares me - I want formal proof
		   of correctness of whatever number we put here. dwmw2.
	*/
	fmc->min_free_size = fmc->sector_size << 2;
	fmc->mtd = mtd;
	fmc->c = c;
	fmc->head = NULL;
	fmc->tail = NULL;
	fmc->head_extra = NULL;
	fmc->tail_extra = NULL;
	mutex_init(&fmc->biglock);
	return fmc;
}


/* When the flash memory scan has completed, this function should be called
   before use of the control structure.  */
void
jffs_build_end(struct jffs_fmcontrol *fmc)
{
	D3(printk("jffs_build_end()\n"));

	if (!fmc->head) {
		fmc->head = fmc->head_extra;
		fmc->tail = fmc->tail_extra;
	}
	else if (fmc->head_extra) {
		fmc->tail_extra->next = fmc->head;
		fmc->head->prev = fmc->tail_extra;
		fmc->head = fmc->head_extra;
	}
	fmc->head_extra = NULL; /* These two instructions should be omitted.  */
	fmc->tail_extra = NULL;
	D3(jffs_print_fmcontrol(fmc));
}


/* Call this function when the file system is unmounted.  This function
   frees all memory used by this module.  */
void
jffs_cleanup_fmcontrol(struct jffs_fmcontrol *fmc)
{
	if (fmc) {
		struct jffs_fm *next = fmc->head;
		while (next) {
			struct jffs_fm *cur = next;
			next = next->next;
			jffs_free_fm(cur);
		}
		put_mtd_device(fmc->mtd);
		kfree(fmc);
		DJM(no_jffs_fmcontrol--);
	}
}


/* This function returns the size of the first chunk of free space on the
   flash memory.  This function will return something nonzero if the flash
   memory contains any free space.  */
__u32
jffs_free_size1(struct jffs_fmcontrol *fmc)
{
	__u32 head;
	__u32 tail;
	__u32 end = fmc->flash_size;

	if (!fmc->head) {
		/* There is nothing on the flash.  */
		return fmc->flash_size;
	}

	/* Compute the beginning and ending of the contents of the flash.  */
	head = fmc->head->offset;
	tail = fmc->tail->offset + fmc->tail->size;
	if (tail == end) {
		tail = 0;
	}
	ASSERT(else if (tail > end) {
		printk(KERN_WARNING "jffs_free_size1(): tail > end\n");
		tail = 0;
	});

	if (head <= tail) {
		return end - tail;
	}
	else {
		return head - tail;
	}
}

/* This function will return something nonzero in case there are two free
   areas on the flash.  Like this:

     +----------------+------------------+----------------+
     |     FREE 1     |   USED / DIRTY   |     FREE 2     |
     +----------------+------------------+----------------+
       fmc->head -----^
       fmc->tail ------------------------^

   The value returned, will be the size of the first empty area on the
   flash, in this case marked "FREE 1".  */
__u32
jffs_free_size2(struct jffs_fmcontrol *fmc)
{
	if (fmc->head) {
		__u32 head = fmc->head->offset;
		__u32 tail = fmc->tail->offset + fmc->tail->size;
		if (tail == fmc->flash_size) {
			tail = 0;
		}

		if (tail >= head) {
			return head;
		}
	}
	return 0;
}


/* Allocate a chunk of flash memory.  If there is enough space on the
   device, a reference to the associated node is stored in the jffs_fm
   struct.  */
int
jffs_fmalloc(struct jffs_fmcontrol *fmc, __u32 size, struct jffs_node *node,
	     struct jffs_fm **result)
{
	struct jffs_fm *fm;
	__u32 free_chunk_size1;
	__u32 free_chunk_size2;

	D2(printk("jffs_fmalloc(): fmc = 0x%p, size = %d, "
		  "node = 0x%p\n", fmc, size, node));

	*result = NULL;

	if (!(fm = jffs_alloc_fm())) {
		D(printk("jffs_fmalloc(): kmalloc() failed! (fm)\n"));
		return -ENOMEM;
	}

	free_chunk_size1 = jffs_free_size1(fmc);
	free_chunk_size2 = jffs_free_size2(fmc);
	if (free_chunk_size1 + free_chunk_size2 != fmc->free_size) {
		printk(KERN_WARNING "Free size accounting screwed\n");
		printk(KERN_WARNING "free_chunk_size1 == 0x%x, free_chunk_size2 == 0x%x, fmc->free_size == 0x%x\n", free_chunk_size1, free_chunk_size2, fmc->free_size);
	}

	D3(printk("jffs_fmalloc(): free_chunk_size1 = %u, "
		  "free_chunk_size2 = %u\n",
		  free_chunk_size1, free_chunk_size2));

	if (size <= free_chunk_size1) {
		if (!(fm->nodes = (struct jffs_node_ref *)
				  kmalloc(sizeof(struct jffs_node_ref),
					  GFP_KERNEL))) {
			D(printk("jffs_fmalloc(): kmalloc() failed! "
				 "(node_ref)\n"));
			jffs_free_fm(fm);
			return -ENOMEM;
		}
		DJM(no_jffs_node_ref++);
		fm->nodes->node = node;
		fm->nodes->next = NULL;
		if (fmc->tail) {
			fm->offset = fmc->tail->offset + fmc->tail->size;
			if (fm->offset == fmc->flash_size) {
				fm->offset = 0;
			}
			ASSERT(else if (fm->offset > fmc->flash_size) {
				printk(KERN_WARNING "jffs_fmalloc(): "
				       "offset > flash_end\n");
				fm->offset = 0;
			});
		}
		else {
			/* There don't have to be files in the file
			   system yet.  */
			fm->offset = 0;
		}
		fm->size = size;
		fmc->free_size -= size;
		fmc->used_size += size;
	}
	else if (size > free_chunk_size2) {
		printk(KERN_WARNING "JFFS: Tried to allocate a too "
		       "large flash memory chunk. (size = %u)\n", size);
		jffs_free_fm(fm);
		return -ENOSPC;
	}
	else {
		fm->offset = fmc->tail->offset + fmc->tail->size;
		fm->size = free_chunk_size1;
		fm->nodes = NULL;
		fmc->free_size -= fm->size;
		fmc->dirty_size += fm->size; /* Changed by simonk. This seemingly fixes a 
						bug that caused infinite garbage collection.
						It previously set fmc->dirty_size to size (which is the
						size of the requested chunk).
					     */
	}

	fm->next = NULL;
	if (!fmc->head) {
		fm->prev = NULL;
		fmc->head = fm;
		fmc->tail = fm;
	}
	else {
		fm->prev = fmc->tail;
		fmc->tail->next = fm;
		fmc->tail = fm;
	}

	D3(jffs_print_fmcontrol(fmc));
	D3(jffs_print_fm(fm));
	*result = fm;
	return 0;
}


/* The on-flash space is not needed anymore by the passed node.  Remove
   the reference to the node from the node list.  If the data chunk in
   the flash memory isn't used by any more nodes anymore (fm->nodes == 0),
   then mark that chunk as dirty.  */
int
jffs_fmfree(struct jffs_fmcontrol *fmc, struct jffs_fm *fm, struct jffs_node *node)
{
	struct jffs_node_ref *ref;
	struct jffs_node_ref *prev;
	ASSERT(int del = 0);

	D2(printk("jffs_fmfree(): node->ino = %u, node->version = %u\n",
		 node->ino, node->version));

	ASSERT(if (!fmc || !fm || !fm->nodes) {
		printk(KERN_ERR "jffs_fmfree(): fmc: 0x%p, fm: 0x%p, "
		       "fm->nodes: 0x%p\n",
		       fmc, fm, (fm ? fm->nodes : NULL));
		return -1;
	});

	/* Find the reference to the node that is going to be removed
	   and remove it.  */
	for (ref = fm->nodes, prev = NULL; ref; ref = ref->next) {
		if (ref->node == node) {
			if (prev) {
				prev->next = ref->next;
			}
			else {
				fm->nodes = ref->next;
			}
			kfree(ref);
			DJM(no_jffs_node_ref--);
			ASSERT(del = 1);
			break;
		}
		prev = ref;
	}

	/* If the data chunk in the flash memory isn't used anymore
	   just mark it as obsolete.  */
	if (!fm->nodes) {
		/* No node uses this chunk so let's remove it.  */
		fmc->used_size -= fm->size;
		fmc->dirty_size += fm->size;
#if defined(JFFS_MARK_OBSOLETE) && JFFS_MARK_OBSOLETE
		if (jffs_mark_obsolete(fmc, fm->offset) < 0) {
			D1(printk("jffs_fmfree(): Failed to mark an on-flash "
				  "node obsolete!\n"));
			return -1;
		}
#endif
	}

	ASSERT(if (!del) {
		printk(KERN_WARNING "***jffs_fmfree(): "
		       "Didn't delete any node reference!\n");
	});

	return 0;
}


/* This allocation function is used during the initialization of
   the file system.  */
struct jffs_fm *
jffs_fmalloced(struct jffs_fmcontrol *fmc, __u32 offset, __u32 size,
	       struct jffs_node *node)
{
	struct jffs_fm *fm;

	D3(printk("jffs_fmalloced()\n"));

	if (!(fm = jffs_alloc_fm())) {
		D(printk("jffs_fmalloced(0x%p, %u, %u, 0x%p): failed!\n",
			 fmc, offset, size, node));
		return NULL;
	}
	fm->offset = offset;
	fm->size = size;
	fm->prev = NULL;
	fm->next = NULL;
	fm->nodes = NULL;
	if (node) {
		/* `node' exists and it should be associated with the
		    jffs_fm structure `fm'.  */
		if (!(fm->nodes = (struct jffs_node_ref *)
				  kmalloc(sizeof(struct jffs_node_ref),
					  GFP_KERNEL))) {
			D(printk("jffs_fmalloced(): !fm->nodes\n"));
			jffs_free_fm(fm);
			return NULL;
		}
		DJM(no_jffs_node_ref++);
		fm->nodes->node = node;
		fm->nodes->next = NULL;
		fmc->used_size += size;
		fmc->free_size -= size;
	}
	else {
		/* If there is no node, then this is just a chunk of dirt.  */
		fmc->dirty_size += size;
		fmc->free_size -= size;
	}

	if (fmc->head_extra) {
		fm->prev = fmc->tail_extra;
		fmc->tail_extra->next = fm;
		fmc->tail_extra = fm;
	}
	else if (!fmc->head) {
		fmc->head = fm;
		fmc->tail = fm;
	}
	else if (fmc->tail->offset + fmc->tail->size < offset) {
		fmc->head_extra = fm;
		fmc->tail_extra = fm;
	}
	else {
		fm->prev = fmc->tail;
		fmc->tail->next = fm;
		fmc->tail = fm;
	}
	D3(jffs_print_fmcontrol(fmc));
	D3(jffs_print_fm(fm));
	return fm;
}


/* Add a new node to an already existing jffs_fm struct.  */
int
jffs_add_node(struct jffs_node *node)
{
	struct jffs_node_ref *ref;

	D3(printk("jffs_add_node(): ino = %u\n", node->ino));

	ref = kmalloc(sizeof(*ref), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;

	DJM(no_jffs_node_ref++);
	ref->node = node;
	ref->next = node->fm->nodes;
	node->fm->nodes = ref;
	return 0;
}


/* Free a part of some allocated space.  */
void
jffs_fmfree_partly(struct jffs_fmcontrol *fmc, struct jffs_fm *fm, __u32 size)
{
	D1(printk("***jffs_fmfree_partly(): fm = 0x%p, fm->nodes = 0x%p, "
		  "fm->nodes->node->ino = %u, size = %u\n",
		  fm, (fm ? fm->nodes : 0),
		  (!fm ? 0 : (!fm->nodes ? 0 : fm->nodes->node->ino)), size));

	if (fm->nodes) {
		kfree(fm->nodes);
		DJM(no_jffs_node_ref--);
		fm->nodes = NULL;
	}
	fmc->used_size -= fm->size;
	if (fm == fmc->tail) {
		fm->size -= size;
		fmc->free_size += size;
	}
	fmc->dirty_size += fm->size;
}


/* Find the jffs_fm struct that contains the end of the data chunk that
   begins at the logical beginning of the flash memory and spans `size'
   bytes.  If we want to erase a sector of the flash memory, we use this
   function to find where the sector limit cuts a chunk of data.  */
struct jffs_fm *
jffs_cut_node(struct jffs_fmcontrol *fmc, __u32 size)
{
	struct jffs_fm *fm;
	__u32 pos = 0;

	if (size == 0) {
		return NULL;
	}

	ASSERT(if (!fmc) {
		printk(KERN_ERR "jffs_cut_node(): fmc == NULL\n");
		return NULL;
	});

	fm = fmc->head;

	while (fm) {
		pos += fm->size;
		if (pos < size) {
			fm = fm->next;
		}
		else if (pos > size) {
			break;
		}
		else {
			fm = NULL;
			break;
		}
	}

	return fm;
}


/* Move the head of the fmc structures and delete the obsolete parts.  */
void
jffs_sync_erase(struct jffs_fmcontrol *fmc, int erased_size)
{
	struct jffs_fm *fm;
	struct jffs_fm *del;

	ASSERT(if (!fmc) {
		printk(KERN_ERR "jffs_sync_erase(): fmc == NULL\n");
		return;
	});

	fmc->dirty_size -= erased_size;
	fmc->free_size += erased_size;

	for (fm = fmc->head; fm && (erased_size > 0);) {
		if (erased_size >= fm->size) {
			erased_size -= fm->size;
			del = fm;
			fm = fm->next;
			fm->prev = NULL;
			fmc->head = fm;
			jffs_free_fm(del);
		}
		else {
			fm->size -= erased_size;
			fm->offset += erased_size;
			break;
		}
	}
}


/* Return the oldest used node in the flash memory.  */
struct jffs_node *
jffs_get_oldest_node(struct jffs_fmcontrol *fmc)
{
	struct jffs_fm *fm;
	struct jffs_node_ref *nref;
	struct jffs_node *node = NULL;

	ASSERT(if (!fmc) {
		printk(KERN_ERR "jffs_get_oldest_node(): fmc == NULL\n");
		return NULL;
	});

	for (fm = fmc->head; fm && !fm->nodes; fm = fm->next);

	if (!fm) {
		return NULL;
	}

	/* The oldest node is the last one in the reference list.  This list
	   shouldn't be too long; just one or perhaps two elements.  */
	for (nref = fm->nodes; nref; nref = nref->next) {
		node = nref->node;
	}

	D2(printk("jffs_get_oldest_node(): ino = %u, version = %u\n",
		  (node ? node->ino : 0), (node ? node->version : 0)));

	return node;
}


#if defined(JFFS_MARK_OBSOLETE) && JFFS_MARK_OBSOLETE

/* Mark an on-flash node as obsolete.

   Note that this is just an optimization that isn't necessary for the
   filesystem to work.  */

static int
jffs_mark_obsolete(struct jffs_fmcontrol *fmc, __u32 fm_offset)
{
	/* The `accurate_pos' holds the position of the accurate byte
	   in the jffs_raw_inode structure that we are going to mark
	   as obsolete.  */
	__u32 accurate_pos = fm_offset + JFFS_RAW_INODE_ACCURATE_OFFSET;
	unsigned char zero = 0x00;
	size_t len;

	D3(printk("jffs_mark_obsolete(): accurate_pos = %u\n", accurate_pos));
	ASSERT(if (!fmc) {
		printk(KERN_ERR "jffs_mark_obsolete(): fmc == NULL\n");
		return -1;
	});

	/* Write 0x00 to the raw inode's accurate member.  Don't care
	   about the return value.  */
	MTD_WRITE(fmc->mtd, accurate_pos, 1, &len, &zero);
	return 0;
}

#endif /* JFFS_MARK_OBSOLETE  */

/* check if it's possible to erase the wanted range, and if not, return
 * the range that IS erasable, or a negative error code.
 */
static long
jffs_flash_erasable_size(struct mtd_info *mtd, __u32 offset, __u32 size)
{
         u_long ssize;

	/* assume that sector size for a partition is constant even
	 * if it spans more than one chip (you usually put the same
	 * type of chips in a system)
	 */

        ssize = mtd->erasesize;

	if (offset % ssize) {
		printk(KERN_WARNING "jffs_flash_erasable_size() given non-aligned offset %x (erasesize %lx)\n", offset, ssize);
		/* The offset is not sector size aligned.  */
		return -1;
	}
	else if (offset > mtd->size) {
		printk(KERN_WARNING "jffs_flash_erasable_size given offset off the end of device (%x > %x)\n", offset, mtd->size);
		return -2;
	}
	else if (offset + size > mtd->size) {
		printk(KERN_WARNING "jffs_flash_erasable_size() given length which runs off the end of device (ofs %x + len %x = %x, > %x)\n", offset,size, offset+size, mtd->size);
		return -3;
	}

	return (size / ssize) * ssize;
}


/* How much dirty flash memory is possible to erase at the moment?  */
long
jffs_erasable_size(struct jffs_fmcontrol *fmc)
{
	struct jffs_fm *fm;
	__u32 size = 0;
	long ret;

	ASSERT(if (!fmc) {
		printk(KERN_ERR "jffs_erasable_size(): fmc = NULL\n");
		return -1;
	});

	if (!fmc->head) {
		/* The flash memory is totally empty. No nodes. No dirt.
		   Just return.  */
		return 0;
	}

	/* Calculate how much space that is dirty.  */
	for (fm = fmc->head; fm && !fm->nodes; fm = fm->next) {
		if (size && fm->offset == 0) {
			/* We have reached the beginning of the flash.  */
			break;
		}
		size += fm->size;
	}

	/* Someone's signature contained this:
	   There's a fine line between fishing and just standing on
	   the shore like an idiot...  */
	ret = jffs_flash_erasable_size(fmc->mtd, fmc->head->offset, size);

	ASSERT(if (ret < 0) {
		printk("jffs_erasable_size: flash_erasable_size() "
		       "returned something less than zero (%ld).\n", ret);
		printk("jffs_erasable_size: offset = 0x%08x\n",
		       fmc->head->offset);
	});

	/* If there is dirt on the flash (which is the reason to why
	   this function was called in the first place) but no space is
	   possible to erase right now, the initial part of the list of
	   jffs_fm structs, that hold place for dirty space, could perhaps
	   be shortened.  The list's initial "dirty" elements are merged
	   into just one large dirty jffs_fm struct.  This operation must
	   only be performed if nothing is possible to erase.  Otherwise,
	   jffs_clear_end_of_node() won't work as expected.  */
	if (ret == 0) {
		struct jffs_fm *head = fmc->head;
		struct jffs_fm *del;
		/* While there are two dirty nodes beside each other.*/
		while (head->nodes == 0
		       && head->next
		       && head->next->nodes == 0) {
			del = head->next;
			head->size += del->size;
			head->next = del->next;
			if (del->next) {
				del->next->prev = head;
			}
			jffs_free_fm(del);
		}
	}

	return (ret >= 0 ? ret : 0);
}

static struct jffs_fm *jffs_alloc_fm(void)
{
	struct jffs_fm *fm;

	fm = kmem_cache_alloc(fm_cache,GFP_KERNEL);
	DJM(if (fm) no_jffs_fm++;);
	
	return fm;
}

static void jffs_free_fm(struct jffs_fm *n)
{
	kmem_cache_free(fm_cache,n);
	DJM(no_jffs_fm--);
}



struct jffs_node *jffs_alloc_node(void)
{
	struct jffs_node *n;

	n = (struct jffs_node *)kmem_cache_alloc(node_cache,GFP_KERNEL);
	if(n != NULL)
		no_jffs_node++;
	return n;
}

void jffs_free_node(struct jffs_node *n)
{
	kmem_cache_free(node_cache,n);
	no_jffs_node--;
}


int jffs_get_node_inuse(void)
{
	return no_jffs_node;
}
