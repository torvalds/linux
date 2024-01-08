// SPDX-License-Identifier: GPL-2.0-or-later
/* kiocb-using read/write
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/falloc.h>
#include <linux/sched/mm.h>
#include <trace/events/fscache.h>
#include "internal.h"

struct cachefiles_kiocb {
	struct kiocb		iocb;
	refcount_t		ki_refcnt;
	loff_t			start;
	union {
		size_t		skipped;
		size_t		len;
	};
	struct cachefiles_object *object;
	netfs_io_terminated_t	term_func;
	void			*term_func_priv;
	bool			was_async;
	unsigned int		inval_counter;	/* Copy of cookie->inval_counter */
	u64			b_writing;
};

static inline void cachefiles_put_kiocb(struct cachefiles_kiocb *ki)
{
	if (refcount_dec_and_test(&ki->ki_refcnt)) {
		cachefiles_put_object(ki->object, cachefiles_obj_put_ioreq);
		fput(ki->iocb.ki_filp);
		kfree(ki);
	}
}

/*
 * Handle completion of a read from the cache.
 */
static void cachefiles_read_complete(struct kiocb *iocb, long ret)
{
	struct cachefiles_kiocb *ki = container_of(iocb, struct cachefiles_kiocb, iocb);
	struct inode *inode = file_inode(ki->iocb.ki_filp);

	_enter("%ld", ret);

	if (ret < 0)
		trace_cachefiles_io_error(ki->object, inode, ret,
					  cachefiles_trace_read_error);

	if (ki->term_func) {
		if (ret >= 0) {
			if (ki->object->cookie->inval_counter == ki->inval_counter)
				ki->skipped += ret;
			else
				ret = -ESTALE;
		}

		ki->term_func(ki->term_func_priv, ret, ki->was_async);
	}

	cachefiles_put_kiocb(ki);
}

/*
 * Initiate a read from the cache.
 */
static int cachefiles_read(struct netfs_cache_resources *cres,
			   loff_t start_pos,
			   struct iov_iter *iter,
			   enum netfs_read_from_hole read_hole,
			   netfs_io_terminated_t term_func,
			   void *term_func_priv)
{
	struct cachefiles_object *object;
	struct cachefiles_kiocb *ki;
	struct file *file;
	unsigned int old_nofs;
	ssize_t ret = -ENOBUFS;
	size_t len = iov_iter_count(iter), skipped = 0;

	if (!fscache_wait_for_operation(cres, FSCACHE_WANT_READ))
		goto presubmission_error;

	fscache_count_read();
	object = cachefiles_cres_object(cres);
	file = cachefiles_cres_file(cres);

	_enter("%pD,%li,%llx,%zx/%llx",
	       file, file_inode(file)->i_ino, start_pos, len,
	       i_size_read(file_inode(file)));

	/* If the caller asked us to seek for data before doing the read, then
	 * we should do that now.  If we find a gap, we fill it with zeros.
	 */
	if (read_hole != NETFS_READ_HOLE_IGNORE) {
		loff_t off = start_pos, off2;

		off2 = cachefiles_inject_read_error();
		if (off2 == 0)
			off2 = vfs_llseek(file, off, SEEK_DATA);
		if (off2 < 0 && off2 >= (loff_t)-MAX_ERRNO && off2 != -ENXIO) {
			skipped = 0;
			ret = off2;
			goto presubmission_error;
		}

		if (off2 == -ENXIO || off2 >= start_pos + len) {
			/* The region is beyond the EOF or there's no more data
			 * in the region, so clear the rest of the buffer and
			 * return success.
			 */
			ret = -ENODATA;
			if (read_hole == NETFS_READ_HOLE_FAIL)
				goto presubmission_error;

			iov_iter_zero(len, iter);
			skipped = len;
			ret = 0;
			goto presubmission_error;
		}

		skipped = off2 - off;
		iov_iter_zero(skipped, iter);
	}

	ret = -ENOMEM;
	ki = kzalloc(sizeof(struct cachefiles_kiocb), GFP_KERNEL);
	if (!ki)
		goto presubmission_error;

	refcount_set(&ki->ki_refcnt, 2);
	ki->iocb.ki_filp	= file;
	ki->iocb.ki_pos		= start_pos + skipped;
	ki->iocb.ki_flags	= IOCB_DIRECT;
	ki->iocb.ki_ioprio	= get_current_ioprio();
	ki->skipped		= skipped;
	ki->object		= object;
	ki->inval_counter	= cres->inval_counter;
	ki->term_func		= term_func;
	ki->term_func_priv	= term_func_priv;
	ki->was_async		= true;

	if (ki->term_func)
		ki->iocb.ki_complete = cachefiles_read_complete;

	get_file(ki->iocb.ki_filp);
	cachefiles_grab_object(object, cachefiles_obj_get_ioreq);

	trace_cachefiles_read(object, file_inode(file), ki->iocb.ki_pos, len - skipped);
	old_nofs = memalloc_nofs_save();
	ret = cachefiles_inject_read_error();
	if (ret == 0)
		ret = vfs_iocb_iter_read(file, &ki->iocb, iter);
	memalloc_nofs_restore(old_nofs);
	switch (ret) {
	case -EIOCBQUEUED:
		goto in_progress;

	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
	case -ERESTARTNOHAND:
	case -ERESTART_RESTARTBLOCK:
		/* There's no easy way to restart the syscall since other AIO's
		 * may be already running. Just fail this IO with EINTR.
		 */
		ret = -EINTR;
		fallthrough;
	default:
		ki->was_async = false;
		cachefiles_read_complete(&ki->iocb, ret);
		if (ret > 0)
			ret = 0;
		break;
	}

in_progress:
	cachefiles_put_kiocb(ki);
	_leave(" = %zd", ret);
	return ret;

presubmission_error:
	if (term_func)
		term_func(term_func_priv, ret < 0 ? ret : skipped, false);
	return ret;
}

/*
 * Query the occupancy of the cache in a region, returning where the next chunk
 * of data starts and how long it is.
 */
static int cachefiles_query_occupancy(struct netfs_cache_resources *cres,
				      loff_t start, size_t len, size_t granularity,
				      loff_t *_data_start, size_t *_data_len)
{
	struct cachefiles_object *object;
	struct file *file;
	loff_t off, off2;

	*_data_start = -1;
	*_data_len = 0;

	if (!fscache_wait_for_operation(cres, FSCACHE_WANT_READ))
		return -ENOBUFS;

	object = cachefiles_cres_object(cres);
	file = cachefiles_cres_file(cres);
	granularity = max_t(size_t, object->volume->cache->bsize, granularity);

	_enter("%pD,%li,%llx,%zx/%llx",
	       file, file_inode(file)->i_ino, start, len,
	       i_size_read(file_inode(file)));

	off = cachefiles_inject_read_error();
	if (off == 0)
		off = vfs_llseek(file, start, SEEK_DATA);
	if (off == -ENXIO)
		return -ENODATA; /* Beyond EOF */
	if (off < 0 && off >= (loff_t)-MAX_ERRNO)
		return -ENOBUFS; /* Error. */
	if (round_up(off, granularity) >= start + len)
		return -ENODATA; /* No data in range */

	off2 = cachefiles_inject_read_error();
	if (off2 == 0)
		off2 = vfs_llseek(file, off, SEEK_HOLE);
	if (off2 == -ENXIO)
		return -ENODATA; /* Beyond EOF */
	if (off2 < 0 && off2 >= (loff_t)-MAX_ERRNO)
		return -ENOBUFS; /* Error. */

	/* Round away partial blocks */
	off = round_up(off, granularity);
	off2 = round_down(off2, granularity);
	if (off2 <= off)
		return -ENODATA;

	*_data_start = off;
	if (off2 > start + len)
		*_data_len = len;
	else
		*_data_len = off2 - off;
	return 0;
}

/*
 * Handle completion of a write to the cache.
 */
static void cachefiles_write_complete(struct kiocb *iocb, long ret)
{
	struct cachefiles_kiocb *ki = container_of(iocb, struct cachefiles_kiocb, iocb);
	struct cachefiles_object *object = ki->object;
	struct inode *inode = file_inode(ki->iocb.ki_filp);

	_enter("%ld", ret);

	if (ki->was_async)
		kiocb_end_write(iocb);

	if (ret < 0)
		trace_cachefiles_io_error(object, inode, ret,
					  cachefiles_trace_write_error);

	atomic_long_sub(ki->b_writing, &object->volume->cache->b_writing);
	set_bit(FSCACHE_COOKIE_HAVE_DATA, &object->cookie->flags);
	if (ki->term_func)
		ki->term_func(ki->term_func_priv, ret, ki->was_async);
	cachefiles_put_kiocb(ki);
}

/*
 * Initiate a write to the cache.
 */
int __cachefiles_write(struct cachefiles_object *object,
		       struct file *file,
		       loff_t start_pos,
		       struct iov_iter *iter,
		       netfs_io_terminated_t term_func,
		       void *term_func_priv)
{
	struct cachefiles_cache *cache;
	struct cachefiles_kiocb *ki;
	unsigned int old_nofs;
	ssize_t ret;
	size_t len = iov_iter_count(iter);

	fscache_count_write();
	cache = object->volume->cache;

	_enter("%pD,%li,%llx,%zx/%llx",
	       file, file_inode(file)->i_ino, start_pos, len,
	       i_size_read(file_inode(file)));

	ki = kzalloc(sizeof(struct cachefiles_kiocb), GFP_KERNEL);
	if (!ki) {
		if (term_func)
			term_func(term_func_priv, -ENOMEM, false);
		return -ENOMEM;
	}

	refcount_set(&ki->ki_refcnt, 2);
	ki->iocb.ki_filp	= file;
	ki->iocb.ki_pos		= start_pos;
	ki->iocb.ki_flags	= IOCB_DIRECT | IOCB_WRITE;
	ki->iocb.ki_ioprio	= get_current_ioprio();
	ki->object		= object;
	ki->start		= start_pos;
	ki->len			= len;
	ki->term_func		= term_func;
	ki->term_func_priv	= term_func_priv;
	ki->was_async		= true;
	ki->b_writing		= (len + (1 << cache->bshift) - 1) >> cache->bshift;

	if (ki->term_func)
		ki->iocb.ki_complete = cachefiles_write_complete;
	atomic_long_add(ki->b_writing, &cache->b_writing);

	get_file(ki->iocb.ki_filp);
	cachefiles_grab_object(object, cachefiles_obj_get_ioreq);

	trace_cachefiles_write(object, file_inode(file), ki->iocb.ki_pos, len);
	old_nofs = memalloc_nofs_save();
	ret = cachefiles_inject_write_error();
	if (ret == 0)
		ret = vfs_iocb_iter_write(file, &ki->iocb, iter);
	memalloc_nofs_restore(old_nofs);
	switch (ret) {
	case -EIOCBQUEUED:
		goto in_progress;

	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
	case -ERESTARTNOHAND:
	case -ERESTART_RESTARTBLOCK:
		/* There's no easy way to restart the syscall since other AIO's
		 * may be already running. Just fail this IO with EINTR.
		 */
		ret = -EINTR;
		fallthrough;
	default:
		ki->was_async = false;
		cachefiles_write_complete(&ki->iocb, ret);
		if (ret > 0)
			ret = 0;
		break;
	}

in_progress:
	cachefiles_put_kiocb(ki);
	_leave(" = %zd", ret);
	return ret;
}

static int cachefiles_write(struct netfs_cache_resources *cres,
			    loff_t start_pos,
			    struct iov_iter *iter,
			    netfs_io_terminated_t term_func,
			    void *term_func_priv)
{
	if (!fscache_wait_for_operation(cres, FSCACHE_WANT_WRITE)) {
		if (term_func)
			term_func(term_func_priv, -ENOBUFS, false);
		return -ENOBUFS;
	}

	return __cachefiles_write(cachefiles_cres_object(cres),
				  cachefiles_cres_file(cres),
				  start_pos, iter,
				  term_func, term_func_priv);
}

static inline enum netfs_io_source
cachefiles_do_prepare_read(struct netfs_cache_resources *cres,
			   loff_t start, size_t *_len, loff_t i_size,
			   unsigned long *_flags, ino_t netfs_ino)
{
	enum cachefiles_prepare_read_trace why;
	struct cachefiles_object *object = NULL;
	struct cachefiles_cache *cache;
	struct fscache_cookie *cookie = fscache_cres_cookie(cres);
	const struct cred *saved_cred;
	struct file *file = cachefiles_cres_file(cres);
	enum netfs_io_source ret = NETFS_DOWNLOAD_FROM_SERVER;
	size_t len = *_len;
	loff_t off, to;
	ino_t ino = file ? file_inode(file)->i_ino : 0;
	int rc;

	_enter("%zx @%llx/%llx", len, start, i_size);

	if (start >= i_size) {
		ret = NETFS_FILL_WITH_ZEROES;
		why = cachefiles_trace_read_after_eof;
		goto out_no_object;
	}

	if (test_bit(FSCACHE_COOKIE_NO_DATA_TO_READ, &cookie->flags)) {
		__set_bit(NETFS_SREQ_COPY_TO_CACHE, _flags);
		why = cachefiles_trace_read_no_data;
		if (!test_bit(NETFS_SREQ_ONDEMAND, _flags))
			goto out_no_object;
	}

	/* The object and the file may be being created in the background. */
	if (!file) {
		why = cachefiles_trace_read_no_file;
		if (!fscache_wait_for_operation(cres, FSCACHE_WANT_READ))
			goto out_no_object;
		file = cachefiles_cres_file(cres);
		if (!file)
			goto out_no_object;
		ino = file_inode(file)->i_ino;
	}

	object = cachefiles_cres_object(cres);
	cache = object->volume->cache;
	cachefiles_begin_secure(cache, &saved_cred);
retry:
	off = cachefiles_inject_read_error();
	if (off == 0)
		off = vfs_llseek(file, start, SEEK_DATA);
	if (off < 0 && off >= (loff_t)-MAX_ERRNO) {
		if (off == (loff_t)-ENXIO) {
			why = cachefiles_trace_read_seek_nxio;
			goto download_and_store;
		}
		trace_cachefiles_io_error(object, file_inode(file), off,
					  cachefiles_trace_seek_error);
		why = cachefiles_trace_read_seek_error;
		goto out;
	}

	if (off >= start + len) {
		why = cachefiles_trace_read_found_hole;
		goto download_and_store;
	}

	if (off > start) {
		off = round_up(off, cache->bsize);
		len = off - start;
		*_len = len;
		why = cachefiles_trace_read_found_part;
		goto download_and_store;
	}

	to = cachefiles_inject_read_error();
	if (to == 0)
		to = vfs_llseek(file, start, SEEK_HOLE);
	if (to < 0 && to >= (loff_t)-MAX_ERRNO) {
		trace_cachefiles_io_error(object, file_inode(file), to,
					  cachefiles_trace_seek_error);
		why = cachefiles_trace_read_seek_error;
		goto out;
	}

	if (to < start + len) {
		if (start + len >= i_size)
			to = round_up(to, cache->bsize);
		else
			to = round_down(to, cache->bsize);
		len = to - start;
		*_len = len;
	}

	why = cachefiles_trace_read_have_data;
	ret = NETFS_READ_FROM_CACHE;
	goto out;

download_and_store:
	__set_bit(NETFS_SREQ_COPY_TO_CACHE, _flags);
	if (test_bit(NETFS_SREQ_ONDEMAND, _flags)) {
		rc = cachefiles_ondemand_read(object, start, len);
		if (!rc) {
			__clear_bit(NETFS_SREQ_ONDEMAND, _flags);
			goto retry;
		}
		ret = NETFS_INVALID_READ;
	}
out:
	cachefiles_end_secure(cache, saved_cred);
out_no_object:
	trace_cachefiles_prep_read(object, start, len, *_flags, ret, why, ino, netfs_ino);
	return ret;
}

/*
 * Prepare a read operation, shortening it to a cached/uncached
 * boundary as appropriate.
 */
static enum netfs_io_source cachefiles_prepare_read(struct netfs_io_subrequest *subreq,
						    loff_t i_size)
{
	return cachefiles_do_prepare_read(&subreq->rreq->cache_resources,
					  subreq->start, &subreq->len, i_size,
					  &subreq->flags, subreq->rreq->inode->i_ino);
}

/*
 * Prepare an on-demand read operation, shortening it to a cached/uncached
 * boundary as appropriate.
 */
static enum netfs_io_source
cachefiles_prepare_ondemand_read(struct netfs_cache_resources *cres,
				 loff_t start, size_t *_len, loff_t i_size,
				 unsigned long *_flags, ino_t ino)
{
	return cachefiles_do_prepare_read(cres, start, _len, i_size, _flags, ino);
}

/*
 * Prepare for a write to occur.
 */
int __cachefiles_prepare_write(struct cachefiles_object *object,
			       struct file *file,
			       loff_t *_start, size_t *_len,
			       bool no_space_allocated_yet)
{
	struct cachefiles_cache *cache = object->volume->cache;
	loff_t start = *_start, pos;
	size_t len = *_len, down;
	int ret;

	/* Round to DIO size */
	down = start - round_down(start, PAGE_SIZE);
	*_start = start - down;
	*_len = round_up(down + len, PAGE_SIZE);

	/* We need to work out whether there's sufficient disk space to perform
	 * the write - but we can skip that check if we have space already
	 * allocated.
	 */
	if (no_space_allocated_yet)
		goto check_space;

	pos = cachefiles_inject_read_error();
	if (pos == 0)
		pos = vfs_llseek(file, *_start, SEEK_DATA);
	if (pos < 0 && pos >= (loff_t)-MAX_ERRNO) {
		if (pos == -ENXIO)
			goto check_space; /* Unallocated tail */
		trace_cachefiles_io_error(object, file_inode(file), pos,
					  cachefiles_trace_seek_error);
		return pos;
	}
	if ((u64)pos >= (u64)*_start + *_len)
		goto check_space; /* Unallocated region */

	/* We have a block that's at least partially filled - if we're low on
	 * space, we need to see if it's fully allocated.  If it's not, we may
	 * want to cull it.
	 */
	if (cachefiles_has_space(cache, 0, *_len / PAGE_SIZE,
				 cachefiles_has_space_check) == 0)
		return 0; /* Enough space to simply overwrite the whole block */

	pos = cachefiles_inject_read_error();
	if (pos == 0)
		pos = vfs_llseek(file, *_start, SEEK_HOLE);
	if (pos < 0 && pos >= (loff_t)-MAX_ERRNO) {
		trace_cachefiles_io_error(object, file_inode(file), pos,
					  cachefiles_trace_seek_error);
		return pos;
	}
	if ((u64)pos >= (u64)*_start + *_len)
		return 0; /* Fully allocated */

	/* Partially allocated, but insufficient space: cull. */
	fscache_count_no_write_space();
	ret = cachefiles_inject_remove_error();
	if (ret == 0)
		ret = vfs_fallocate(file, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				    *_start, *_len);
	if (ret < 0) {
		trace_cachefiles_io_error(object, file_inode(file), ret,
					  cachefiles_trace_fallocate_error);
		cachefiles_io_error_obj(object,
					"CacheFiles: fallocate failed (%d)\n", ret);
		ret = -EIO;
	}

	return ret;

check_space:
	return cachefiles_has_space(cache, 0, *_len / PAGE_SIZE,
				    cachefiles_has_space_for_write);
}

static int cachefiles_prepare_write(struct netfs_cache_resources *cres,
				    loff_t *_start, size_t *_len, loff_t i_size,
				    bool no_space_allocated_yet)
{
	struct cachefiles_object *object = cachefiles_cres_object(cres);
	struct cachefiles_cache *cache = object->volume->cache;
	const struct cred *saved_cred;
	int ret;

	if (!cachefiles_cres_file(cres)) {
		if (!fscache_wait_for_operation(cres, FSCACHE_WANT_WRITE))
			return -ENOBUFS;
		if (!cachefiles_cres_file(cres))
			return -ENOBUFS;
	}

	cachefiles_begin_secure(cache, &saved_cred);
	ret = __cachefiles_prepare_write(object, cachefiles_cres_file(cres),
					 _start, _len,
					 no_space_allocated_yet);
	cachefiles_end_secure(cache, saved_cred);
	return ret;
}

/*
 * Clean up an operation.
 */
static void cachefiles_end_operation(struct netfs_cache_resources *cres)
{
	struct file *file = cachefiles_cres_file(cres);

	if (file)
		fput(file);
	fscache_end_cookie_access(fscache_cres_cookie(cres), fscache_access_io_end);
}

static const struct netfs_cache_ops cachefiles_netfs_cache_ops = {
	.end_operation		= cachefiles_end_operation,
	.read			= cachefiles_read,
	.write			= cachefiles_write,
	.prepare_read		= cachefiles_prepare_read,
	.prepare_write		= cachefiles_prepare_write,
	.prepare_ondemand_read	= cachefiles_prepare_ondemand_read,
	.query_occupancy	= cachefiles_query_occupancy,
};

/*
 * Open the cache file when beginning a cache operation.
 */
bool cachefiles_begin_operation(struct netfs_cache_resources *cres,
				enum fscache_want_state want_state)
{
	struct cachefiles_object *object = cachefiles_cres_object(cres);

	if (!cachefiles_cres_file(cres)) {
		cres->ops = &cachefiles_netfs_cache_ops;
		if (object->file) {
			spin_lock(&object->lock);
			if (!cres->cache_priv2 && object->file)
				cres->cache_priv2 = get_file(object->file);
			spin_unlock(&object->lock);
		}
	}

	if (!cachefiles_cres_file(cres) && want_state != FSCACHE_WANT_PARAMS) {
		pr_err("failed to get cres->file\n");
		return false;
	}

	return true;
}
