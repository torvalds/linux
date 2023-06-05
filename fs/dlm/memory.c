// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "midcomms.h"
#include "lowcomms.h"
#include "config.h"
#include "memory.h"
#include "ast.h"

static struct kmem_cache *writequeue_cache;
static struct kmem_cache *mhandle_cache;
static struct kmem_cache *msg_cache;
static struct kmem_cache *lkb_cache;
static struct kmem_cache *rsb_cache;
static struct kmem_cache *cb_cache;


int __init dlm_memory_init(void)
{
	writequeue_cache = dlm_lowcomms_writequeue_cache_create();
	if (!writequeue_cache)
		goto out;

	mhandle_cache = dlm_midcomms_cache_create();
	if (!mhandle_cache)
		goto mhandle;

	lkb_cache = kmem_cache_create("dlm_lkb", sizeof(struct dlm_lkb),
				__alignof__(struct dlm_lkb), 0, NULL);
	if (!lkb_cache)
		goto lkb;

	msg_cache = dlm_lowcomms_msg_cache_create();
	if (!msg_cache)
		goto msg;

	rsb_cache = kmem_cache_create("dlm_rsb", sizeof(struct dlm_rsb),
				__alignof__(struct dlm_rsb), 0, NULL);
	if (!rsb_cache)
		goto rsb;

	cb_cache = kmem_cache_create("dlm_cb", sizeof(struct dlm_callback),
				     __alignof__(struct dlm_callback), 0,
				     NULL);
	if (!cb_cache)
		goto cb;

	return 0;

cb:
	kmem_cache_destroy(rsb_cache);
rsb:
	kmem_cache_destroy(msg_cache);
msg:
	kmem_cache_destroy(lkb_cache);
lkb:
	kmem_cache_destroy(mhandle_cache);
mhandle:
	kmem_cache_destroy(writequeue_cache);
out:
	return -ENOMEM;
}

void dlm_memory_exit(void)
{
	kmem_cache_destroy(writequeue_cache);
	kmem_cache_destroy(mhandle_cache);
	kmem_cache_destroy(msg_cache);
	kmem_cache_destroy(lkb_cache);
	kmem_cache_destroy(rsb_cache);
	kmem_cache_destroy(cb_cache);
}

char *dlm_allocate_lvb(struct dlm_ls *ls)
{
	char *p;

	p = kzalloc(ls->ls_lvblen, GFP_NOFS);
	return p;
}

void dlm_free_lvb(char *p)
{
	kfree(p);
}

struct dlm_rsb *dlm_allocate_rsb(struct dlm_ls *ls)
{
	struct dlm_rsb *r;

	r = kmem_cache_zalloc(rsb_cache, GFP_NOFS);
	return r;
}

void dlm_free_rsb(struct dlm_rsb *r)
{
	if (r->res_lvbptr)
		dlm_free_lvb(r->res_lvbptr);
	kmem_cache_free(rsb_cache, r);
}

struct dlm_lkb *dlm_allocate_lkb(struct dlm_ls *ls)
{
	struct dlm_lkb *lkb;

	lkb = kmem_cache_zalloc(lkb_cache, GFP_NOFS);
	return lkb;
}

void dlm_free_lkb(struct dlm_lkb *lkb)
{
	if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
		struct dlm_user_args *ua;
		ua = lkb->lkb_ua;
		if (ua) {
			kfree(ua->lksb.sb_lvbptr);
			kfree(ua);
		}
	}

	/* drop references if they are set */
	dlm_callback_set_last_ptr(&lkb->lkb_last_cast, NULL);
	dlm_callback_set_last_ptr(&lkb->lkb_last_cb, NULL);

	kmem_cache_free(lkb_cache, lkb);
}

struct dlm_mhandle *dlm_allocate_mhandle(gfp_t allocation)
{
	return kmem_cache_alloc(mhandle_cache, allocation);
}

void dlm_free_mhandle(struct dlm_mhandle *mhandle)
{
	kmem_cache_free(mhandle_cache, mhandle);
}

struct writequeue_entry *dlm_allocate_writequeue(void)
{
	return kmem_cache_alloc(writequeue_cache, GFP_ATOMIC);
}

void dlm_free_writequeue(struct writequeue_entry *writequeue)
{
	kmem_cache_free(writequeue_cache, writequeue);
}

struct dlm_msg *dlm_allocate_msg(gfp_t allocation)
{
	return kmem_cache_alloc(msg_cache, allocation);
}

void dlm_free_msg(struct dlm_msg *msg)
{
	kmem_cache_free(msg_cache, msg);
}

struct dlm_callback *dlm_allocate_cb(void)
{
	return kmem_cache_alloc(cb_cache, GFP_ATOMIC);
}

void dlm_free_cb(struct dlm_callback *cb)
{
	kmem_cache_free(cb_cache, cb);
}
