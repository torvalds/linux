// SPDX-License-Identifier: GPL-2.0-or-later
/* Cache data I/O routines
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#define FSCACHE_DEBUG_LEVEL OPERATION
#include <linux/fscache-cache.h>
#include <linux/uio.h>
#include <linux/bvec.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include "internal.h"

/**
 * fscache_wait_for_operation - Wait for an object become accessible
 * @cres: The cache resources for the operation being performed
 * @want_state: The minimum state the object must be at
 *
 * See if the target cache object is at the specified minimum state of
 * accessibility yet, and if not, wait for it.
 */
bool fscache_wait_for_operation(struct netfs_cache_resources *cres,
				enum fscache_want_state want_state)
{
	struct fscache_cookie *cookie = fscache_cres_cookie(cres);
	enum fscache_cookie_state state;

again:
	if (!fscache_cache_is_live(cookie->volume->cache)) {
		kleave(" [broken]");
		return false;
	}

	state = fscache_cookie_state(cookie);
	kenter("c=%08x{%u},%x", cookie->debug_id, state, want_state);

	switch (state) {
	case FSCACHE_COOKIE_STATE_CREATING:
	case FSCACHE_COOKIE_STATE_INVALIDATING:
		if (want_state == FSCACHE_WANT_PARAMS)
			goto ready; /* There can be no content */
		fallthrough;
	case FSCACHE_COOKIE_STATE_LOOKING_UP:
	case FSCACHE_COOKIE_STATE_LRU_DISCARDING:
		wait_var_event(&cookie->state,
			       fscache_cookie_state(cookie) != state);
		goto again;

	case FSCACHE_COOKIE_STATE_ACTIVE:
		goto ready;
	case FSCACHE_COOKIE_STATE_DROPPED:
	case FSCACHE_COOKIE_STATE_RELINQUISHING:
	default:
		kleave(" [not live]");
		return false;
	}

ready:
	if (!cres->cache_priv2)
		return cookie->volume->cache->ops->begin_operation(cres, want_state);
	return true;
}
EXPORT_SYMBOL(fscache_wait_for_operation);

/*
 * Begin an I/O operation on the cache, waiting till we reach the right state.
 *
 * Attaches the resources required to the operation resources record.
 */
static int fscache_begin_operation(struct netfs_cache_resources *cres,
				   struct fscache_cookie *cookie,
				   enum fscache_want_state want_state,
				   enum fscache_access_trace why)
{
	enum fscache_cookie_state state;
	long timeo;
	bool once_only = false;

	cres->ops		= NULL;
	cres->cache_priv	= cookie;
	cres->cache_priv2	= NULL;
	cres->debug_id		= cookie->debug_id;
	cres->inval_counter	= cookie->inval_counter;

	if (!fscache_begin_cookie_access(cookie, why)) {
		cres->cache_priv = NULL;
		return -ENOBUFS;
	}

again:
	spin_lock(&cookie->lock);

	state = fscache_cookie_state(cookie);
	kenter("c=%08x{%u},%x", cookie->debug_id, state, want_state);

	switch (state) {
	case FSCACHE_COOKIE_STATE_LOOKING_UP:
	case FSCACHE_COOKIE_STATE_LRU_DISCARDING:
	case FSCACHE_COOKIE_STATE_INVALIDATING:
		goto wait_for_file_wrangling;
	case FSCACHE_COOKIE_STATE_CREATING:
		if (want_state == FSCACHE_WANT_PARAMS)
			goto ready; /* There can be no content */
		goto wait_for_file_wrangling;
	case FSCACHE_COOKIE_STATE_ACTIVE:
		goto ready;
	case FSCACHE_COOKIE_STATE_DROPPED:
	case FSCACHE_COOKIE_STATE_RELINQUISHING:
		WARN(1, "Can't use cookie in state %u\n", cookie->state);
		goto not_live;
	default:
		goto not_live;
	}

ready:
	spin_unlock(&cookie->lock);
	if (!cookie->volume->cache->ops->begin_operation(cres, want_state))
		goto failed;
	return 0;

wait_for_file_wrangling:
	spin_unlock(&cookie->lock);
	trace_fscache_access(cookie->debug_id, refcount_read(&cookie->ref),
			     atomic_read(&cookie->n_accesses),
			     fscache_access_io_wait);
	timeo = wait_var_event_timeout(&cookie->state,
				       fscache_cookie_state(cookie) != state, 20 * HZ);
	if (timeo <= 1 && !once_only) {
		pr_warn("%s: cookie state change wait timed out: cookie->state=%u state=%u",
			__func__, fscache_cookie_state(cookie), state);
		fscache_print_cookie(cookie, 'O');
		once_only = true;
	}
	goto again;

not_live:
	spin_unlock(&cookie->lock);
failed:
	cres->cache_priv = NULL;
	cres->ops = NULL;
	fscache_end_cookie_access(cookie, fscache_access_io_not_live);
	kleave(" = -ENOBUFS");
	return -ENOBUFS;
}

int __fscache_begin_read_operation(struct netfs_cache_resources *cres,
				   struct fscache_cookie *cookie)
{
	return fscache_begin_operation(cres, cookie, FSCACHE_WANT_PARAMS,
				       fscache_access_io_read);
}
EXPORT_SYMBOL(__fscache_begin_read_operation);

int __fscache_begin_write_operation(struct netfs_cache_resources *cres,
				    struct fscache_cookie *cookie)
{
	return fscache_begin_operation(cres, cookie, FSCACHE_WANT_PARAMS,
				       fscache_access_io_write);
}
EXPORT_SYMBOL(__fscache_begin_write_operation);

struct fscache_write_request {
	struct netfs_cache_resources cache_resources;
	struct address_space	*mapping;
	loff_t			start;
	size_t			len;
	bool			set_bits;
	bool			using_pgpriv2;
	netfs_io_terminated_t	term_func;
	void			*term_func_priv;
};

void __fscache_clear_page_bits(struct address_space *mapping,
			       loff_t start, size_t len)
{
	pgoff_t first = start / PAGE_SIZE;
	pgoff_t last = (start + len - 1) / PAGE_SIZE;
	struct page *page;

	if (len) {
		XA_STATE(xas, &mapping->i_pages, first);

		rcu_read_lock();
		xas_for_each(&xas, page, last) {
			folio_end_private_2(page_folio(page));
		}
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(__fscache_clear_page_bits);

/*
 * Deal with the completion of writing the data to the cache.
 */
static void fscache_wreq_done(void *priv, ssize_t transferred_or_error,
			      bool was_async)
{
	struct fscache_write_request *wreq = priv;

	if (wreq->using_pgpriv2)
		fscache_clear_page_bits(wreq->mapping, wreq->start, wreq->len,
					wreq->set_bits);

	if (wreq->term_func)
		wreq->term_func(wreq->term_func_priv, transferred_or_error,
				was_async);
	fscache_end_operation(&wreq->cache_resources);
	kfree(wreq);
}

void __fscache_write_to_cache(struct fscache_cookie *cookie,
			      struct address_space *mapping,
			      loff_t start, size_t len, loff_t i_size,
			      netfs_io_terminated_t term_func,
			      void *term_func_priv,
			      bool using_pgpriv2, bool cond)
{
	struct fscache_write_request *wreq;
	struct netfs_cache_resources *cres;
	struct iov_iter iter;
	int ret = -ENOBUFS;

	if (len == 0)
		goto abandon;

	kenter("%llx,%zx", start, len);

	wreq = kzalloc(sizeof(struct fscache_write_request), GFP_NOFS);
	if (!wreq)
		goto abandon;
	wreq->mapping		= mapping;
	wreq->start		= start;
	wreq->len		= len;
	wreq->using_pgpriv2	= using_pgpriv2;
	wreq->set_bits		= cond;
	wreq->term_func		= term_func;
	wreq->term_func_priv	= term_func_priv;

	cres = &wreq->cache_resources;
	if (fscache_begin_operation(cres, cookie, FSCACHE_WANT_WRITE,
				    fscache_access_io_write) < 0)
		goto abandon_free;

	ret = cres->ops->prepare_write(cres, &start, &len, len, i_size, false);
	if (ret < 0)
		goto abandon_end;

	/* TODO: Consider clearing page bits now for space the write isn't
	 * covering.  This is more complicated than it appears when THPs are
	 * taken into account.
	 */

	iov_iter_xarray(&iter, ITER_SOURCE, &mapping->i_pages, start, len);
	fscache_write(cres, start, &iter, fscache_wreq_done, wreq);
	return;

abandon_end:
	return fscache_wreq_done(wreq, ret, false);
abandon_free:
	kfree(wreq);
abandon:
	if (using_pgpriv2)
		fscache_clear_page_bits(mapping, start, len, cond);
	if (term_func)
		term_func(term_func_priv, ret, false);
}
EXPORT_SYMBOL(__fscache_write_to_cache);

/*
 * Change the size of a backing object.
 */
void __fscache_resize_cookie(struct fscache_cookie *cookie, loff_t new_size)
{
	struct netfs_cache_resources cres;

	trace_fscache_resize(cookie, new_size);
	if (fscache_begin_operation(&cres, cookie, FSCACHE_WANT_WRITE,
				    fscache_access_io_resize) == 0) {
		fscache_stat(&fscache_n_resizes);
		set_bit(FSCACHE_COOKIE_NEEDS_UPDATE, &cookie->flags);

		/* We cannot defer a resize as we need to do it inside the
		 * netfs's inode lock so that we're serialised with respect to
		 * writes.
		 */
		cookie->volume->cache->ops->resize_cookie(&cres, new_size);
		fscache_end_operation(&cres);
	} else {
		fscache_stat(&fscache_n_resizes_null);
	}
}
EXPORT_SYMBOL(__fscache_resize_cookie);
