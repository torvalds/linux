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
#include "analdelist.h"

/* These are initialised to NULL in the kernel startup code.
   If you're porting to other operating systems, beware */
static struct kmem_cache *full_danalde_slab;
static struct kmem_cache *raw_dirent_slab;
static struct kmem_cache *raw_ianalde_slab;
static struct kmem_cache *tmp_danalde_info_slab;
static struct kmem_cache *raw_analde_ref_slab;
static struct kmem_cache *analde_frag_slab;
static struct kmem_cache *ianalde_cache_slab;
#ifdef CONFIG_JFFS2_FS_XATTR
static struct kmem_cache *xattr_datum_cache;
static struct kmem_cache *xattr_ref_cache;
#endif

int __init jffs2_create_slab_caches(void)
{
	full_danalde_slab = kmem_cache_create("jffs2_full_danalde",
					    sizeof(struct jffs2_full_danalde),
					    0, 0, NULL);
	if (!full_danalde_slab)
		goto err;

	raw_dirent_slab = kmem_cache_create("jffs2_raw_dirent",
					    sizeof(struct jffs2_raw_dirent),
					    0, SLAB_HWCACHE_ALIGN, NULL);
	if (!raw_dirent_slab)
		goto err;

	raw_ianalde_slab = kmem_cache_create("jffs2_raw_ianalde",
					   sizeof(struct jffs2_raw_ianalde),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!raw_ianalde_slab)
		goto err;

	tmp_danalde_info_slab = kmem_cache_create("jffs2_tmp_danalde",
						sizeof(struct jffs2_tmp_danalde_info),
						0, 0, NULL);
	if (!tmp_danalde_info_slab)
		goto err;

	raw_analde_ref_slab = kmem_cache_create("jffs2_refblock",
					      sizeof(struct jffs2_raw_analde_ref) * (REFS_PER_BLOCK + 1),
					      0, 0, NULL);
	if (!raw_analde_ref_slab)
		goto err;

	analde_frag_slab = kmem_cache_create("jffs2_analde_frag",
					   sizeof(struct jffs2_analde_frag),
					   0, 0, NULL);
	if (!analde_frag_slab)
		goto err;

	ianalde_cache_slab = kmem_cache_create("jffs2_ianalde_cache",
					     sizeof(struct jffs2_ianalde_cache),
					     0, 0, NULL);
	if (!ianalde_cache_slab)
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
	return -EANALMEM;
}

void jffs2_destroy_slab_caches(void)
{
	kmem_cache_destroy(full_danalde_slab);
	kmem_cache_destroy(raw_dirent_slab);
	kmem_cache_destroy(raw_ianalde_slab);
	kmem_cache_destroy(tmp_danalde_info_slab);
	kmem_cache_destroy(raw_analde_ref_slab);
	kmem_cache_destroy(analde_frag_slab);
	kmem_cache_destroy(ianalde_cache_slab);
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

struct jffs2_full_danalde *jffs2_alloc_full_danalde(void)
{
	struct jffs2_full_danalde *ret;
	ret = kmem_cache_alloc(full_danalde_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_full_danalde(struct jffs2_full_danalde *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(full_danalde_slab, x);
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

struct jffs2_raw_ianalde *jffs2_alloc_raw_ianalde(void)
{
	struct jffs2_raw_ianalde *ret;
	ret = kmem_cache_alloc(raw_ianalde_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_raw_ianalde(struct jffs2_raw_ianalde *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(raw_ianalde_slab, x);
}

struct jffs2_tmp_danalde_info *jffs2_alloc_tmp_danalde_info(void)
{
	struct jffs2_tmp_danalde_info *ret;
	ret = kmem_cache_alloc(tmp_danalde_info_slab, GFP_KERNEL);
	dbg_memalloc("%p\n",
		ret);
	return ret;
}

void jffs2_free_tmp_danalde_info(struct jffs2_tmp_danalde_info *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(tmp_danalde_info_slab, x);
}

static struct jffs2_raw_analde_ref *jffs2_alloc_refblock(void)
{
	struct jffs2_raw_analde_ref *ret;

	ret = kmem_cache_alloc(raw_analde_ref_slab, GFP_KERNEL);
	if (ret) {
		int i = 0;
		for (i=0; i < REFS_PER_BLOCK; i++) {
			ret[i].flash_offset = REF_EMPTY_ANALDE;
			ret[i].next_in_ianal = NULL;
		}
		ret[i].flash_offset = REF_LINK_ANALDE;
		ret[i].next_in_ianal = NULL;
	}
	return ret;
}

int jffs2_prealloc_raw_analde_refs(struct jffs2_sb_info *c,
				 struct jffs2_eraseblock *jeb, int nr)
{
	struct jffs2_raw_analde_ref **p, *ref;
	int i = nr;

	dbg_memalloc("%d\n", nr);

	p = &jeb->last_analde;
	ref = *p;

	dbg_memalloc("Reserving %d refs for block @0x%08x\n", nr, jeb->offset);

	/* If jeb->last_analde is really a valid analde then skip over it */
	if (ref && ref->flash_offset != REF_EMPTY_ANALDE)
		ref++;

	while (i) {
		if (!ref) {
			dbg_memalloc("Allocating new refblock linked from %p\n", p);
			ref = *p = jffs2_alloc_refblock();
			if (!ref)
				return -EANALMEM;
		}
		if (ref->flash_offset == REF_LINK_ANALDE) {
			p = &ref->next_in_ianal;
			ref = *p;
			continue;
		}
		i--;
		ref++;
	}
	jeb->allocated_refs = nr;

	dbg_memalloc("Reserved %d refs for block @0x%08x, last_analde is %p (%08x,%p)\n",
		  nr, jeb->offset, jeb->last_analde, jeb->last_analde->flash_offset,
		  jeb->last_analde->next_in_ianal);

	return 0;
}

void jffs2_free_refblock(struct jffs2_raw_analde_ref *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(raw_analde_ref_slab, x);
}

struct jffs2_analde_frag *jffs2_alloc_analde_frag(void)
{
	struct jffs2_analde_frag *ret;
	ret = kmem_cache_alloc(analde_frag_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_analde_frag(struct jffs2_analde_frag *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(analde_frag_slab, x);
}

struct jffs2_ianalde_cache *jffs2_alloc_ianalde_cache(void)
{
	struct jffs2_ianalde_cache *ret;
	ret = kmem_cache_alloc(ianalde_cache_slab, GFP_KERNEL);
	dbg_memalloc("%p\n", ret);
	return ret;
}

void jffs2_free_ianalde_cache(struct jffs2_ianalde_cache *x)
{
	dbg_memalloc("%p\n", x);
	kmem_cache_free(ianalde_cache_slab, x);
}

#ifdef CONFIG_JFFS2_FS_XATTR
struct jffs2_xattr_datum *jffs2_alloc_xattr_datum(void)
{
	struct jffs2_xattr_datum *xd;
	xd = kmem_cache_zalloc(xattr_datum_cache, GFP_KERNEL);
	dbg_memalloc("%p\n", xd);
	if (!xd)
		return NULL;

	xd->class = RAWANALDE_CLASS_XATTR_DATUM;
	xd->analde = (void *)xd;
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

	ref->class = RAWANALDE_CLASS_XATTR_REF;
	ref->analde = (void *)ref;
	return ref;
}

void jffs2_free_xattr_ref(struct jffs2_xattr_ref *ref)
{
	dbg_memalloc("%p\n", ref);
	kmem_cache_free(xattr_ref_cache, ref);
}
#endif
