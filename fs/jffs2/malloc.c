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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/jffs2.h>
#include "yesdelist.h"

/* These are initialised to NULL in the kernel startup code.
   If you're porting to other operating systems, beware */
static struct kmem_cache *full_dyesde_slab;
static struct kmem_cache *raw_dirent_slab;
static struct kmem_cache *raw_iyesde_slab;
static struct kmem_cache *tmp_dyesde_info_slab;
static struct kmem_cache *raw_yesde_ref_slab;
static struct kmem_cache *yesde_frag_slab;
static struct kmem_cache *iyesde_cache_slab;
#ifdef CONFIG_JFFS2_FS_XATTR
static struct kmem_cache *xattr_datum_cache;
static struct kmem_cache *xattr_ref_cache;
#endif

int __init jffs2_create_slab_caches(void)
{
	full_dyesde_slab = kmem_cache_create("jffs2_full_dyesde",
					    sizeof(struct jffs2_full_dyesde),
					    0, 0, NULL);
	if (!full_dyesde_slab)
		goto err;

	raw_dirent_slab = kmem_cache_create("jffs2_raw_dirent",
					    sizeof(struct jffs2_raw_dirent),
					    0, SLAB_HWCACHE_ALIGN, NULL);
	if (!raw_dirent_slab)
		goto err;

	raw_iyesde_slab = kmem_cache_create("jffs2_raw_iyesde",
					   sizeof(struct jffs2_raw_iyesde),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!raw_iyesde_slab)
		goto err;

	tmp_dyesde_info_slab = kmem_cache_create("jffs2_tmp_dyesde",
						sizeof(struct jffs2_tmp_dyesde_info),
						0, 0, NULL);
	if (!tmp_dyesde_info_slab)
		goto err;

	raw_yesde_ref_slab = kmem_cache_create("jffs2_refblock",
					      sizeof(struct jffs2_raw_yesde_ref) * (REFS_PER_BLOCK + 1),
					      0, 0, NULL);
	if (!raw_yesde_ref_slab)
		goto err;

	yesde_frag_slab = kmem_cache_create("jffs2_yesde_frag",
					   sizeof(struct jffs2_yesde_frag),
					   0, 0, NULL);
	if (!yesde_frag_slab)
		goto err;

	iyesde_cache_slab = kmem_cache_create("jffs2_iyesde_cache",
					     sizeof(struct jffs2_iyesde_cache),
					     0, 0, NULL);
	if (!iyesde_cache_slab)
		goto err;

#ifdef CONFIG_JFFS2_FS_XATTR
	xattr_datum_cache = kmem_cache_create("jffs2_xattr_datum",
					     sizeof(struct jffs2_xattr_datum),
					     0, 0, NULL);
	if (!xattr_datum_cache)
		goto err;

	xattr_ref_cache = kmem_cache_create("jffs2_xattr_ref",
					   sizeof(struct jffs2_xattr_ref),
					   0, 0, NULL);
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
	kmem_cache_destroy(full_dyesde_slab);
	kmem_cache_destroy(raw_dirent_slab);
	kmem_cache_destroy(raw_iyesde_slab);
	kmem_cache_destroy(tmp_dyesde_info_slab);
	kmem_cache_destroy(raw_yesde_ref_slab);
	kmem_cache_destroy(yesde_frag_slab);
	kmem_cache_destroy(iyesde_cache_slab);
#ifdef CONFIG_JFFS2_FS_XATTR
	kmem_cache_destroy(xattr_datum_cache);
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

struct jffs2_full_dyesde *jffs2_alloc_full_dyesde(void)
{
	struct jffs2_full_dyesde *ret;
	ret = kmem_cache_alloc(full_dyesde_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_full_dyesde(struct jffs2_full_dyesde *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(full_dyesde_slab, x);
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

struct jffs2_raw_iyesde *jffs2_alloc_raw_iyesde(void)
{
	struct jffs2_raw_iyesde *ret;
	ret = kmem_cache_alloc(raw_iyesde_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_raw_iyesde(struct jffs2_raw_iyesde *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(raw_iyesde_slab, x);
}

struct jffs2_tmp_dyesde_info *jffs2_alloc_tmp_dyesde_info(void)
{
	struct jffs2_tmp_dyesde_info *ret;
	ret = kmem_cache_alloc(tmp_dyesde_info_slab, GFP_KERNEL);
	dbg_memalloc("%p\n",
		ret);
	return ret;
}

void jffs2_free_tmp_dyesde_info(struct jffs2_tmp_dyesde_info *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(tmp_dyesde_info_slab, x);
}

static struct jffs2_raw_yesde_ref *jffs2_alloc_refblock(void)
{
	struct jffs2_raw_yesde_ref *ret;

	ret = kmem_cache_alloc(raw_yesde_ref_slab, GFP_KERNEL);
	if (ret) {
		int i = 0;
		for (i=0; i < REFS_PER_BLOCK; i++) {
			ret[i].flash_offset = REF_EMPTY_NODE;
			ret[i].next_in_iyes = NULL;
		}
		ret[i].flash_offset = REF_LINK_NODE;
		ret[i].next_in_iyes = NULL;
	}
	return ret;
}

int jffs2_prealloc_raw_yesde_refs(struct jffs2_sb_info *c,
				 struct jffs2_eraseblock *jeb, int nr)
{
	struct jffs2_raw_yesde_ref **p, *ref;
	int i = nr;

	dbg_memalloc("%d\n", nr);

	p = &jeb->last_yesde;
	ref = *p;

	dbg_memalloc("Reserving %d refs for block @0x%08x\n", nr, jeb->offset);

	/* If jeb->last_yesde is really a valid yesde then skip over it */
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
			p = &ref->next_in_iyes;
			ref = *p;
			continue;
		}
		i--;
		ref++;
	}
	jeb->allocated_refs = nr;

	dbg_memalloc("Reserved %d refs for block @0x%08x, last_yesde is %p (%08x,%p)\n",
		  nr, jeb->offset, jeb->last_yesde, jeb->last_yesde->flash_offset,
		  jeb->last_yesde->next_in_iyes);

	return 0;
}

void jffs2_free_refblock(struct jffs2_raw_yesde_ref *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(raw_yesde_ref_slab, x);
}

struct jffs2_yesde_frag *jffs2_alloc_yesde_frag(void)
{
	struct jffs2_yesde_frag *ret;
	ret = kmem_cache_alloc(yesde_frag_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_yesde_frag(struct jffs2_yesde_frag *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(yesde_frag_slab, x);
}

struct jffs2_iyesde_cache *jffs2_alloc_iyesde_cache(void)
{
	struct jffs2_iyesde_cache *ret;
	ret = kmem_cache_alloc(iyesde_cache_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_iyesde_cache(struct jffs2_iyesde_cache *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(iyesde_cache_slab, x);
}

#ifdef CONFIG_JFFS2_FS_XATTR
struct jffs2_xattr_datum *jffs2_alloc_xattr_datum(void)
{
	struct jffs2_xattr_datum *xd;
	xd = kmem_cache_zalloc(xattr_datum_cache, GFP_KERNEL);
	dbg_memalloc("%p\n", xd);
	if (!xd)
		return NULL;

	xd->class = RAWNODE_CLASS_XATTR_DATUM;
	xd->yesde = (void *)xd;
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
	ref = kmem_cache_zalloc(xattr_ref_cache, GFP_KERNEL);
	dbg_memalloc("%p\n", ref);
	if (!ref)
		return NULL;

	ref->class = RAWNODE_CLASS_XATTR_REF;
	ref->yesde = (void *)ref;
	return ref;
}

void jffs2_free_xattr_ref(struct jffs2_xattr_ref *ref)
{
	dbg_memalloc("%p\n", ref);
	kmem_cache_free(xattr_ref_cache, ref);
}
#endif
