/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: malloc.c,v 1.31 2005/11/07 11:14:40 gleixner Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/jffs2.h>
#include "nodelist.h"

/* These are initialised to NULL in the kernel startup code.
   If you're porting to other operating systems, beware */
static kmem_cache_t *full_dnode_slab;
static kmem_cache_t *raw_dirent_slab;
static kmem_cache_t *raw_inode_slab;
static kmem_cache_t *tmp_dnode_info_slab;
static kmem_cache_t *raw_node_ref_slab;
static kmem_cache_t *node_frag_slab;
static kmem_cache_t *inode_cache_slab;
#ifdef CONFIG_JFFS2_FS_XATTR
static kmem_cache_t *xattr_datum_cache;
static kmem_cache_t *xattr_ref_cache;
#endif

int __init jffs2_create_slab_caches(void)
{
	full_dnode_slab = kmem_cache_create("jffs2_full_dnode",
					    sizeof(struct jffs2_full_dnode),
					    0, 0, NULL, NULL);
	if (!full_dnode_slab)
		goto err;

	raw_dirent_slab = kmem_cache_create("jffs2_raw_dirent",
					    sizeof(struct jffs2_raw_dirent),
					    0, 0, NULL, NULL);
	if (!raw_dirent_slab)
		goto err;

	raw_inode_slab = kmem_cache_create("jffs2_raw_inode",
					   sizeof(struct jffs2_raw_inode),
					   0, 0, NULL, NULL);
	if (!raw_inode_slab)
		goto err;

	tmp_dnode_info_slab = kmem_cache_create("jffs2_tmp_dnode",
						sizeof(struct jffs2_tmp_dnode_info),
						0, 0, NULL, NULL);
	if (!tmp_dnode_info_slab)
		goto err;

	raw_node_ref_slab = kmem_cache_create("jffs2_refblock",
					      sizeof(struct jffs2_raw_node_ref) * (REFS_PER_BLOCK + 1),
					      0, 0, NULL, NULL);
	if (!raw_node_ref_slab)
		goto err;

	node_frag_slab = kmem_cache_create("jffs2_node_frag",
					   sizeof(struct jffs2_node_frag),
					   0, 0, NULL, NULL);
	if (!node_frag_slab)
		goto err;

	inode_cache_slab = kmem_cache_create("jffs2_inode_cache",
					     sizeof(struct jffs2_inode_cache),
					     0, 0, NULL, NULL);
	if (!inode_cache_slab)
		goto err;

#ifdef CONFIG_JFFS2_FS_XATTR
	xattr_datum_cache = kmem_cache_create("jffs2_xattr_datum",
					     sizeof(struct jffs2_xattr_datum),
					     0, 0, NULL, NULL);
	if (!xattr_datum_cache)
		goto err;

	xattr_ref_cache = kmem_cache_create("jffs2_xattr_ref",
					   sizeof(struct jffs2_xattr_ref),
					   0, 0, NULL, NULL);
	if (!xattr_ref_cache)
		goto err;
#endif

	return 0;
 err:
	jffs2_destroy_slab_caches();
	return -ENOMEM;
}

void jffs2_destroy_slab_caches(void)
{
	if(full_dnode_slab)
		kmem_cache_destroy(full_dnode_slab);
	if(raw_dirent_slab)
		kmem_cache_destroy(raw_dirent_slab);
	if(raw_inode_slab)
		kmem_cache_destroy(raw_inode_slab);
	if(tmp_dnode_info_slab)
		kmem_cache_destroy(tmp_dnode_info_slab);
	if(raw_node_ref_slab)
		kmem_cache_destroy(raw_node_ref_slab);
	if(node_frag_slab)
		kmem_cache_destroy(node_frag_slab);
	if(inode_cache_slab)
		kmem_cache_destroy(inode_cache_slab);
#ifdef CONFIG_JFFS2_FS_XATTR
	if (xattr_datum_cache)
		kmem_cache_destroy(xattr_datum_cache);
	if (xattr_ref_cache)
		kmem_cache_destroy(xattr_ref_cache);
#endif
}

struct jffs2_full_dirent *jffs2_alloc_full_dirent(int namesize)
{
	struct jffs2_full_dirent *ret;
	ret = kmalloc(sizeof(struct jffs2_full_dirent) + namesize, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_full_dirent(struct jffs2_full_dirent *x)
{
	dbg_memalloc("%p\n", x);
	kfree(x);
}

struct jffs2_full_dnode *jffs2_alloc_full_dnode(void)
{
	struct jffs2_full_dnode *ret;
	ret = kmem_cache_alloc(full_dnode_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_full_dnode(struct jffs2_full_dnode *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(full_dnode_slab, x);
}

struct jffs2_raw_dirent *jffs2_alloc_raw_dirent(void)
{
	struct jffs2_raw_dirent *ret;
	ret = kmem_cache_alloc(raw_dirent_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_raw_dirent(struct jffs2_raw_dirent *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(raw_dirent_slab, x);
}

struct jffs2_raw_inode *jffs2_alloc_raw_inode(void)
{
	struct jffs2_raw_inode *ret;
	ret = kmem_cache_alloc(raw_inode_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_raw_inode(struct jffs2_raw_inode *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(raw_inode_slab, x);
}

struct jffs2_tmp_dnode_info *jffs2_alloc_tmp_dnode_info(void)
{
	struct jffs2_tmp_dnode_info *ret;
	ret = kmem_cache_alloc(tmp_dnode_info_slab, GFP_KERNEL);
	dbg_memalloc("%p\n",
		ret);
	return ret;
}

void jffs2_free_tmp_dnode_info(struct jffs2_tmp_dnode_info *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(tmp_dnode_info_slab, x);
}

struct jffs2_raw_node_ref *jffs2_alloc_refblock(void)
{
	struct jffs2_raw_node_ref *ret;

	ret = kmem_cache_alloc(raw_node_ref_slab, GFP_KERNEL);
	if (ret) {
		int i = 0;
		for (i=0; i < REFS_PER_BLOCK; i++) {
			ret[i].flash_offset = REF_EMPTY_NODE;
			ret[i].next_in_ino = NULL;
		}
		ret[i].flash_offset = REF_LINK_NODE;
		ret[i].next_in_ino = NULL;
	}
	return ret;
}

int jffs2_prealloc_raw_node_refs(struct jffs2_sb_info *c,
				 struct jffs2_eraseblock *jeb, int nr)
{
	struct jffs2_raw_node_ref **p, *ref;
	int i = nr;

	dbg_memalloc("%d\n", nr);

	p = &jeb->last_node;
	ref = *p;

	dbg_memalloc("Reserving %d refs for block @0x%08x\n", nr, jeb->offset);

	/* If jeb->last_node is really a valid node then skip over it */
	if (ref && ref->flash_offset != REF_EMPTY_NODE)
		ref++;

	while (i) {
		if (!ref) {
			dbg_memalloc("Allocating new refblock linked from %p\n", p);
			ref = *p = jffs2_alloc_refblock();
			if (!ref)
				return -ENOMEM;
		}
		if (ref->flash_offset == REF_LINK_NODE) {
			p = &ref->next_in_ino;
			ref = *p;
			continue;
		}
		i--;
		ref++;
	}
	jeb->allocated_refs = nr;

	dbg_memalloc("Reserved %d refs for block @0x%08x, last_node is %p (%08x,%p)\n",
		  nr, jeb->offset, jeb->last_node, jeb->last_node->flash_offset,
		  jeb->last_node->next_in_ino);

	return 0;
}

void jffs2_free_refblock(struct jffs2_raw_node_ref *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(raw_node_ref_slab, x);
}

struct jffs2_node_frag *jffs2_alloc_node_frag(void)
{
	struct jffs2_node_frag *ret;
	ret = kmem_cache_alloc(node_frag_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_node_frag(struct jffs2_node_frag *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(node_frag_slab, x);
}

struct jffs2_inode_cache *jffs2_alloc_inode_cache(void)
{
	struct jffs2_inode_cache *ret;
	ret = kmem_cache_alloc(inode_cache_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_inode_cache(struct jffs2_inode_cache *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(inode_cache_slab, x);
}

#ifdef CONFIG_JFFS2_FS_XATTR
struct jffs2_xattr_datum *jffs2_alloc_xattr_datum(void)
{
	struct jffs2_xattr_datum *xd;
	xd = kmem_cache_alloc(xattr_datum_cache, GFP_KERNEL);
	dbg_memalloc("%p\n", xd);

	memset(xd, 0, sizeof(struct jffs2_xattr_datum));
	xd->class = RAWNODE_CLASS_XATTR_DATUM;
	INIT_LIST_HEAD(&xd->xindex);
	return xd;
}

void jffs2_free_xattr_datum(struct jffs2_xattr_datum *xd)
{
	dbg_memalloc("%p\n", xd);
	kmem_cache_free(xattr_datum_cache, xd);
}

struct jffs2_xattr_ref *jffs2_alloc_xattr_ref(void)
{
	struct jffs2_xattr_ref *ref;
	ref = kmem_cache_alloc(xattr_ref_cache, GFP_KERNEL);
	dbg_memalloc("%p\n", ref);

	memset(ref, 0, sizeof(struct jffs2_xattr_ref));
	ref->class = RAWNODE_CLASS_XATTR_REF;
	return ref;
}

void jffs2_free_xattr_ref(struct jffs2_xattr_ref *ref)
{
	dbg_memalloc("%p\n", ref);
	kmem_cache_free(xattr_ref_cache, ref);
}
#endif
