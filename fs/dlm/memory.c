/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "config.h"
#include "memory.h"

static kmem_cache_t *lkb_cache;


int dlm_memory_init(void)
{
	int ret = 0;

	lkb_cache = kmem_cache_create("dlm_lkb", sizeof(struct dlm_lkb),
				__alignof__(struct dlm_lkb), 0, NULL, NULL);
	if (!lkb_cache)
		ret = -ENOMEM;
	return ret;
}

void dlm_memory_exit(void)
{
	if (lkb_cache)
		kmem_cache_destroy(lkb_cache);
}

char *allocate_lvb(struct dlm_ls *ls)
{
	char *p;

	p = kmalloc(ls->ls_lvblen, GFP_KERNEL);
	if (p)
		memset(p, 0, ls->ls_lvblen);
	return p;
}

void free_lvb(char *p)
{
	kfree(p);
}

/* FIXME: have some minimal space built-in to rsb for the name and
   kmalloc a separate name if needed, like dentries are done */

struct dlm_rsb *allocate_rsb(struct dlm_ls *ls, int namelen)
{
	struct dlm_rsb *r;

	DLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,);

	r = kmalloc(sizeof(*r) + namelen, GFP_KERNEL);
	if (r)
		memset(r, 0, sizeof(*r) + namelen);
	return r;
}

void free_rsb(struct dlm_rsb *r)
{
	if (r->res_lvbptr)
		free_lvb(r->res_lvbptr);
	kfree(r);
}

struct dlm_lkb *allocate_lkb(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;

	lkb = kmem_cache_alloc(lkb_cache, GFP_KERNEL);
	if (lkb)
		memset(lkb, 0, sizeof(*lkb));
	return lkb;
}

void free_lkb(struct dlm_lkb *lkb)
{
	if (lkb->lkb_flags & DLM_IFL_USER) {
		struct dlm_user_args *ua;
		ua = (struct dlm_user_args *)lkb->lkb_astparam;
		if (ua) {
			if (ua->lksb.sb_lvbptr)
				kfree(ua->lksb.sb_lvbptr);
			kfree(ua);
		}
	}
	kmem_cache_free(lkb_cache, lkb);
}

struct dlm_direntry *allocate_direntry(struct dlm_ls *ls, int namelen)
{
	struct dlm_direntry *de;

	DLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,
		   printk("namelen = %d\n", namelen););

	de = kmalloc(sizeof(*de) + namelen, GFP_KERNEL);
	if (de)
		memset(de, 0, sizeof(*de) + namelen);
	return de;
}

void free_direntry(struct dlm_direntry *de)
{
	kfree(de);
}

