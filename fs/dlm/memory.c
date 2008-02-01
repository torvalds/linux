/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
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

static struct kmem_cache *lkb_cache;


int __init dlm_memory_init(void)
{
	int ret = 0;

	lkb_cache = kmem_cache_create("dlm_lkb", sizeof(struct dlm_lkb),
				__alignof__(struct dlm_lkb), 0, NULL);
	if (!lkb_cache)
		ret = -ENOMEM;
	return ret;
}

void dlm_memory_exit(void)
{
	if (lkb_cache)
		kmem_cache_destroy(lkb_cache);
}

char *dlm_allocate_lvb(struct dlm_ls *ls)
{
	char *p;

	p = kzalloc(ls->ls_lvblen, GFP_KERNEL);
	return p;
}

void dlm_free_lvb(char *p)
{
	kfree(p);
}

/* FIXME: have some minimal space built-in to rsb for the name and
   kmalloc a separate name if needed, like dentries are done */

struct dlm_rsb *dlm_allocate_rsb(struct dlm_ls *ls, int namelen)
{
	struct dlm_rsb *r;

	DLM_ASSERT(namelen <= DLM_RESNAME_MAXLEN,);

	r = kzalloc(sizeof(*r) + namelen, GFP_KERNEL);
	return r;
}

void dlm_free_rsb(struct dlm_rsb *r)
{
	if (r->res_lvbptr)
		dlm_free_lvb(r->res_lvbptr);
	kfree(r);
}

struct dlm_lkb *dlm_allocate_lkb(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;

	lkb = kmem_cache_zalloc(lkb_cache, GFP_KERNEL);
	return lkb;
}

void dlm_free_lkb(struct dlm_lkb *lkb)
{
	if (lkb->lkb_flags & DLM_IFL_USER) {
		struct dlm_user_args *ua;
		ua = lkb->lkb_ua;
		if (ua) {
			if (ua->lksb.sb_lvbptr)
				kfree(ua->lksb.sb_lvbptr);
			kfree(ua);
		}
	}
	kmem_cache_free(lkb_cache, lkb);
}

