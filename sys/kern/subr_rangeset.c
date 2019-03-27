/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/pctrie.h>
#include <sys/rangeset.h>
#include <vm/uma.h>

#ifdef DIAGNOSTIC
static void rangeset_check(struct rangeset *rs);
#else
#define	rangeset_check(rs)
#endif

static uma_zone_t rs_node_zone;

static void
rs_rangeset_init(void *arg __unused)
{

	rs_node_zone = uma_zcreate("rangeset pctrie nodes",
	    pctrie_node_size(), NULL, NULL, pctrie_zone_init, NULL,
	    UMA_ALIGN_PTR, 0);
}
SYSINIT(rs, SI_SUB_LOCK, SI_ORDER_ANY, rs_rangeset_init, NULL);

static void *
rs_node_alloc(struct pctrie *ptree)
{
	struct rangeset *rs;

	rs = __containerof(ptree, struct rangeset, rs_trie);
	return (uma_zalloc(rs_node_zone, rs->rs_alloc_flags));
}

static void
rs_node_free(struct pctrie *ptree __unused, void *node)
{

	uma_zfree(rs_node_zone, node);
}

void
rangeset_init(struct rangeset *rs, rs_dup_data_t dup_data,
    rs_free_data_t free_data, void *data_ctx, u_int alloc_flags)
{

	pctrie_init(&rs->rs_trie);
	rs->rs_dup_data = dup_data;
	rs->rs_free_data = free_data;
	rs->rs_data_ctx = data_ctx;
	rs->rs_alloc_flags = alloc_flags;
}

void
rangeset_fini(struct rangeset *rs)
{

	rangeset_check(rs);
	rangeset_remove_all(rs);
}

bool
rangeset_check_empty(struct rangeset *rs, uint64_t start, uint64_t end)
{
	struct rs_el *r;
	uint64_t *r1;

	rangeset_check(rs);
	r1 = pctrie_lookup_le(&rs->rs_trie, end);
	if (r1 != NULL) {
		r = __containerof(r1, struct rs_el, re_start);
		if (r->re_end > start)
			return (false);
	}
	return (true);
}

int
rangeset_insert(struct rangeset *rs, uint64_t start, uint64_t end,
    void *data)
{
	struct rs_el *r;
	int error;

	rangeset_check(rs);
	error = rangeset_remove(rs, start, end);
	if (error != 0)
		return (error);
	r = data;
	r->re_start = start;
	r->re_end = end;
	error = pctrie_insert(&rs->rs_trie, &r->re_start, rs_node_alloc);
	rangeset_check(rs);
	return (error);
}

int
rangeset_remove_pred(struct rangeset *rs, uint64_t start, uint64_t end,
    rs_pred_t pred)
{
	struct rs_el *r, *rn;
	uint64_t *r1;
	int error;

	rangeset_check(rs);
	error = 0;
	for (; end > 0 && start < end;) {
		r1 = pctrie_lookup_le(&rs->rs_trie, end - 1);
		if (r1 == NULL)
			break;
		r = __containerof(r1, struct rs_el, re_start);

		/*
		 * ------============================--|-------|----
		 *	 rs    	       	       	   re  s       e
		 */
		if (r->re_end <= start)
			break;

		if (r->re_end <= end) {
			if (r->re_start < start) {
				/*
				 * ------========|==============-------|----
				 *	 rs    	 s     	      re       e
				 */
				if (pred(rs->rs_data_ctx, r))
					r->re_end = start;
				break;
			}

			/*
			 * ------|--------===================----------|----
			 *	 s    	  rs   	       	   re          e
			 */
			end = r->re_start;
			if (pred(rs->rs_data_ctx, r)) {
				pctrie_remove(&rs->rs_trie, r->re_start,
				    rs_node_free);
				rs->rs_free_data(rs->rs_data_ctx, r);
			}
			continue;
		}

		/*
		 * ------|--------====================|==========----
		 *	 s    	  rs   	       	      e         re
		 */
		if (r->re_start >= start) {
			if (pred(rs->rs_data_ctx, r)) {
				pctrie_remove(&rs->rs_trie, r->re_start,
				    rs_node_free);
				r->re_start = end;
				error = pctrie_insert(&rs->rs_trie,
				    &r->re_start, rs_node_alloc);
				/*
				 * The insert above must succeed
				 * because rs_node zone is marked
				 * nofree and we freed one element
				 * just before.
				 */
				MPASS(error == 0);
			} else {
				end = r->re_start;
			}
			continue;
		}

		/*
		 * ------=========|===================|==========----
		 *	 rs   	  s    	       	      e         re
		 */
		if (pred(rs->rs_data_ctx, r)) {
			/*
			 * Split.  Can only happen once, and then if
			 * any allocation fails, the rangeset is kept
			 * intact.
			 */
			rn = rs->rs_dup_data(rs->rs_data_ctx, r);
			if (rn == NULL) {
				error = ENOMEM;
				break;
			}
			rn->re_start = end;
			rn->re_end = r->re_end;
			error = pctrie_insert(&rs->rs_trie, &rn->re_start,
			    rs_node_alloc);
			if (error != 0) {
				rs->rs_free_data(rs->rs_data_ctx, rn);
				break;
			}
			r->re_end = start;
		}
		break;
	}
	rangeset_check(rs);
	return (error);
}

static bool
rangeset_true_pred(void *ctx __unused, void *r __unused)
{

	return (true);
}

int
rangeset_remove(struct rangeset *rs, uint64_t start, uint64_t end)
{

	return (rangeset_remove_pred(rs, start, end, rangeset_true_pred));
}

void
rangeset_remove_all(struct rangeset *rs)
{
	struct rs_el *r;
	uint64_t *r1;

	for (;;) {
		r1 = pctrie_lookup_ge(&rs->rs_trie, 0);
		if (r1 == NULL)
			break;
		r = __containerof(r1, struct rs_el, re_start);
		pctrie_remove(&rs->rs_trie, r->re_start, rs_node_free);
		rs->rs_free_data(rs->rs_data_ctx, r);
	}
}

void *
rangeset_lookup(struct rangeset *rs, uint64_t place)
{
	struct rs_el *r;
	uint64_t *r1;

	rangeset_check(rs);
	r1 = pctrie_lookup_le(&rs->rs_trie, place);
	if (r1 == NULL)
		return (NULL);
	r = __containerof(r1, struct rs_el, re_start);
	if (r->re_end <= place)
		return (NULL);
	return (r);
}

int
rangeset_copy(struct rangeset *dst_rs, struct rangeset *src_rs)
{
	struct rs_el *src_r, *dst_r;
	uint64_t cursor, *r1;
	int error;

	MPASS(pctrie_is_empty(&dst_rs->rs_trie));
	rangeset_check(src_rs);
	MPASS(dst_rs->rs_dup_data == src_rs->rs_dup_data);

	error = 0;
	for (cursor = 0;; cursor = src_r->re_start + 1) {
		r1 = pctrie_lookup_ge(&src_rs->rs_trie, cursor);
		if (r1 == NULL)
			break;
		src_r = __containerof(r1, struct rs_el, re_start);
		dst_r = dst_rs->rs_dup_data(dst_rs->rs_data_ctx, src_r);
		if (dst_r == NULL) {
			error = ENOMEM;
			break;
		}
		error = pctrie_insert(&dst_rs->rs_trie, &dst_r->re_start,
		    rs_node_alloc);
		if (error != 0)
			break;
	}
	if (error != 0)
		rangeset_remove_all(dst_rs);
	return (error);
}

#ifdef DIAGNOSTIC
static void
rangeset_check(struct rangeset *rs)
{
	struct rs_el *r, *rp;
	uint64_t cursor, *r1;

	for (cursor = 0, rp = NULL;; cursor = r->re_start + 1, rp = r) {
		r1 = pctrie_lookup_ge(&rs->rs_trie, cursor);
		if (r1 == NULL)
			break;
		r = __containerof(r1, struct rs_el, re_start);
		KASSERT(r->re_start < r->re_end,
		    ("invalid interval rs %p elem %p (%#jx, %#jx)",
		    rs, r, (uintmax_t)r->re_start,  (uintmax_t)r->re_end));
		if (rp != NULL) {
			KASSERT(rp->re_end <= r->re_start,
			    ("non-ascending neighbors rs %p "
			    "prev elem %p (%#jx, %#jx) elem %p (%#jx, %#jx)",
			    rs, rp,  (uintmax_t)rp->re_start,
			    (uintmax_t)rp->re_end, r,  (uintmax_t)r->re_start,
			    (uintmax_t)r->re_end));
		}
	}
}
#endif

#include "opt_ddb.h"
#ifdef DDB
#include <sys/kernel.h>
#include <ddb/ddb.h>

DB_SHOW_COMMAND(rangeset, rangeset_show_fn)
{
	struct rangeset *rs;
	struct rs_el *r;
	uint64_t cursor, *r1;

	if (!have_addr) {
		db_printf("show rangeset addr\n");
		return;
	}

	rs = (struct rangeset *)addr;
	db_printf("rangeset %p\n", rs);
	for (cursor = 0;; cursor = r->re_start + 1) {
		r1 = pctrie_lookup_ge(&rs->rs_trie, cursor);
		if (r1 == NULL)
			break;
		r = __containerof(r1, struct rs_el, re_start);
		db_printf("  el %p start %#jx end %#jx\n",
		    r, r->re_start, r->re_end);
	}
}
#endif
