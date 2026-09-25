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
#include <linux/bio.h>
#include <linux/falloc.h>
#include <linux/sched/mm.h>
#include <trace/events/fscache.h>
#include <trace/events/netfs.h>
#include "internal.h"

struct cachefiles_kiocb {
	struct kiocb		iocb;
	refcount_t		ki_refcnt;
	uoff_t			start;
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

#define IS_ERR_VALUE_LL(x) unlikely((x) >= (unsigned long long)-MAX_ERRNO)

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

		ki->term_func(ki->term_func_priv, ret);
	}

	cachefiles_put_kiocb(ki);
}

/*
 * Initiate a read from the cache.
 */
static int cachefiles_read(struct netfs_cache_resources *cres,
			   uoff_t start_pos,
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

	_enter("%pD,%llu,%llx,%zx/%llx",
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
	ki = kzalloc_obj(struct cachefiles_kiocb);
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
		term_func(term_func_priv, ret < 0 ? ret : skipped);
	return ret;
}

/*
 * Query the occupancy of the cache in a region, returning the extent of the
 * next two chunks of cached data and the next hole.
 */
static int cachefiles_query_occupancy(struct netfs_cache_resources *cres,
				      struct fscache_occupancy *occ)
{
	struct cachefiles_object *object;
	struct inode *inode;
	struct file *file;
	uoff_t read_limit;
	loff_t ret;
	int i;

	if (!fscache_wait_for_operation(cres, FSCACHE_WANT_READ))
		return -ENOBUFS;

	object = cachefiles_cres_object(cres);
	file = cachefiles_cres_file(cres);
	inode = file_inode(file);
	occ->granularity = object->volume->cache->bsize;
	/* Read read_limit before content_info. */
	read_limit = atomic64_read_acquire(&object->read_limit);

	_enter("%pD,%llu,%llx-%llx/%llx",
	       file, inode->i_ino, occ->query_from, occ->query_to, read_limit);

	if (read_limit == 0)
		goto done;

	switch (READ_ONCE(object->content_info)) {
	case CACHEFILES_CONTENT_ALL:
	case CACHEFILES_CONTENT_SINGLE:
		if (read_limit > occ->query_from) {
			occ->cached_from[0] = 0;
			occ->cached_to[0] = read_limit;
			occ->cached_type[0] = FSCACHE_EXTENT_DATA;
			occ->query_from = ULLONG_MAX;
		}
		goto done;
	default:
		break;
	}

	for (i = 0; i < ARRAY_SIZE(occ->cached_from); i++) {
		ret = cachefiles_inject_read_error();
		if (ret == 0)
			ret = vfs_llseek(file, occ->query_from, SEEK_DATA);
		if (IS_ERR_VALUE_LL(ret)) {
			if (ret != -ENXIO)
				return ret;
			occ->query_from = ULLONG_MAX;
			goto done;
		}
		occ->cached_type[i] = FSCACHE_EXTENT_DATA;
		occ->cached_from[i] = ret;
		occ->query_from = ret;

		ret = cachefiles_inject_read_error();
		if (ret == 0)
			ret = vfs_llseek(file, occ->query_from, SEEK_HOLE);
		if (IS_ERR_VALUE_LL(ret)) {
			if (ret != -ENXIO)
				return ret;
			occ->query_from = ULLONG_MAX;
			goto done;
		}
		occ->cached_to[i] = ret;
		occ->query_from = ret;
		if (occ->query_from >= occ->query_to)
			break;
	}

done:
	_debug("query[0] %llx-%llx", occ->cached_from[0], occ->cached_to[0]);
	_debug("query[1] %llx-%llx", occ->cached_from[1], occ->cached_to[1]);
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
		ki->term_func(ki->term_func_priv, ret);
	cachefiles_put_kiocb(ki);
}

/*
 * Initiate a write to the cache.
 */
int __cachefiles_write(struct cachefiles_object *object,
		       struct file *file,
		       uoff_t start_pos,
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

	_enter("%pD,%llu,%llx,%zx/%llx",
	       file, file_inode(file)->i_ino, start_pos, len,
	       i_size_read(file_inode(file)));

	ki = kzalloc_obj(struct cachefiles_kiocb);
	if (!ki) {
		if (term_func)
			term_func(term_func_priv, -ENOMEM);
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
		break;
	}

in_progress:
	cachefiles_put_kiocb(ki);
	_leave(" = %zd", ret);
	return ret;
}

static int cachefiles_write(struct netfs_cache_resources *cres,
			    uoff_t start_pos,
			    struct iov_iter *iter,
			    netfs_io_terminated_t term_func,
			    void *term_func_priv)
{
	if (!fscache_wait_for_operation(cres, FSCACHE_WANT_WRITE)) {
		if (term_func)
			term_func(term_func_priv, -ENOBUFS);
		trace_netfs_sreq(term_func_priv, netfs_sreq_trace_cache_nowrite);
		return -ENOBUFS;
	}

	return __cachefiles_write(cachefiles_cres_object(cres),
				  cachefiles_cres_file(cres),
				  start_pos, iter,
				  term_func, term_func_priv);
}

/*
 * Prepare for a write to occur.
 */
int __cachefiles_prepare_write(struct cachefiles_object *object,
			       struct file *file,
			       uoff_t *_start, size_t *_len, size_t upper_len,
			       bool no_space_allocated_yet)
{
	struct cachefiles_cache *cache = object->volume->cache;
	loff_t start = *_start, pos;
	size_t len = *_len;
	int ret;

	/* Round to DIO size */
	start = round_down(*_start, cache->bsize);
	if (start != *_start || *_len > upper_len) {
		/* Probably asked to cache a streaming write written into the
		 * pagecache when the cookie was temporarily out of service to
		 * culling.
		 */
		fscache_count_dio_misfit();
		return -ENOBUFS;
	}

	*_len = round_up(len, cache->bsize);

	/* We need to work out whether there's sufficient disk space to perform
	 * the write - but we can skip that check if we have space already
	 * allocated.
	 */
	if (no_space_allocated_yet)
		goto check_space;

	pos = cachefiles_inject_read_error();
	if (pos == 0)
		pos = vfs_llseek(file, start, SEEK_DATA);
	if (pos < 0 && pos >= (loff_t)-MAX_ERRNO) {
		if (pos == -ENXIO)
			goto check_space; /* Unallocated tail */
		trace_cachefiles_io_error(object, file_inode(file), pos,
					  cachefiles_trace_seek_error);
		return pos;
	}
	if ((u64)pos >= (u64)start + *_len)
		goto check_space; /* Unallocated region */

	/* We have a block that's at least partially filled - if we're low on
	 * space, we need to see if it's fully allocated.  If it's not, we may
	 * want to cull it.
	 */
	ret = cachefiles_has_space(cache, 0, *_len / cache->bsize,
				   cachefiles_has_space_check);
	if (ret == 0)
		return 0; /* Enough space to simply overwrite the whole block */

	if (ret == -ENOBUFS)
		trace_cachefiles_no_space(object, cachefiles_trace_write_nospace_2);

	pos = cachefiles_inject_read_error();
	if (pos == 0)
		pos = vfs_llseek(file, start, SEEK_HOLE);
	if (pos < 0 && pos >= (loff_t)-MAX_ERRNO) {
		trace_cachefiles_io_error(object, file_inode(file), pos,
					  cachefiles_trace_seek_error);
		return pos;
	}
	if ((u64)pos >= (u64)start + *_len)
		return 0; /* Fully allocated */

	/* Partially allocated, but insufficient space: cull. */
	fscache_count_no_write_space();
	ret = cachefiles_inject_remove_error();
	if (ret == 0)
		ret = vfs_fallocate(file, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				    start, *_len);
	if (ret < 0) {
		trace_cachefiles_io_error(object, file_inode(file), ret,
					  cachefiles_trace_fallocate_error);
		cachefiles_io_error_obj(object,
					"CacheFiles: fallocate failed (%d)\n", ret);
		ret = -EIO;
	}

	return ret;

check_space:
	ret = cachefiles_has_space(cache, 0, *_len / cache->bsize,
				   cachefiles_has_space_for_write);
	if (ret == -ENOBUFS)
		trace_cachefiles_no_space(object, cachefiles_trace_write_nospace);
	return ret;
}

static int cachefiles_prepare_write(struct netfs_cache_resources *cres,
				    uoff_t *_start, size_t *_len, size_t upper_len,
				    uoff_t i_size, bool no_space_allocated_yet)
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
					 _start, _len, upper_len,
					 no_space_allocated_yet);
	cachefiles_end_secure(cache, saved_cred);
	return ret;
}

static void cachefiles_prepare_write_subreq(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *wreq = subreq->rreq;
	struct netfs_cache_resources *cres = &wreq->cache_resources;
	struct netfs_io_stream *stream = &wreq->io_streams[subreq->stream_nr];

	_enter("W=%x[%x] %llx", wreq->debug_id, subreq->debug_index, subreq->start);

	stream->sreq_max_len = MAX_RW_COUNT;
	stream->sreq_max_segs = BIO_MAX_VECS;

	if (!cachefiles_cres_file(cres)) {
		if (!fscache_wait_for_operation(cres, FSCACHE_WANT_WRITE)) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_cache_waitfail);
			return netfs_prepare_write_failed(subreq);
		}
		if (!cachefiles_cres_file(cres)) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_cache_nofile);
			return netfs_prepare_write_failed(subreq);
		}
	}
}

static void cachefiles_issue_write(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *wreq = subreq->rreq;
	struct netfs_cache_resources *cres = &wreq->cache_resources;
	struct cachefiles_object *object = cachefiles_cres_object(cres);
	struct cachefiles_cache *cache = object->volume->cache;
	struct netfs_io_stream *stream = &wreq->io_streams[subreq->stream_nr];
	const struct cred *saved_cred;
	size_t off, pre, post, len = subreq->len;
	uoff_t start = subreq->start;
	int ret;

	_enter("W=%x[%x] %llx-%llx",
	       wreq->debug_id, subreq->debug_index, start, start + len - 1);

	/* We need to start on the cache granularity boundary */
	off = start & (cache->bsize - 1);
	if (off) {
		pre = cache->bsize - off;
		if (pre >= len) {
			fscache_count_dio_misfit();
			netfs_write_subrequest_terminated(subreq, len);
			return;
		}
		subreq->transferred += pre;
		start += pre;
		len -= pre;
		iov_iter_advance(&subreq->io_iter, pre);
	}

	/* We also need to end on the cache granularity boundary */
	if (start + len == wreq->i_size) {
		size_t part = len & (cache->bsize - 1);
		size_t need = cache->bsize - part;

		if (part && stream->submit_extendable_to >= need) {
			len += need;
			subreq->len += need;
			subreq->io_iter.count += need;
		}
	}

	post = len & (cache->bsize - 1);
	if (post) {
		len -= post;
		if (len == 0) {
			fscache_count_dio_misfit();
			netfs_write_subrequest_terminated(subreq, post);
			return;
		}
		iov_iter_truncate(&subreq->io_iter, len);
	}

	trace_netfs_sreq(subreq, netfs_sreq_trace_cache_prepare);
	cachefiles_begin_secure(cache, &saved_cred);
	ret = __cachefiles_prepare_write(object, cachefiles_cres_file(cres),
					 &start, &len, len, true);
	cachefiles_end_secure(cache, saved_cred);
	if (ret < 0) {
		netfs_write_subrequest_terminated(subreq, ret);
		return;
	}

	trace_netfs_sreq(subreq, netfs_sreq_trace_cache_write);
	cachefiles_write(&subreq->rreq->cache_resources,
			 subreq->start, &subreq->io_iter,
			 netfs_write_subrequest_terminated, subreq);
}

/*
 * Collect the result of buffered writeback to the cache.  This includes
 * copying a read to the cache.  Netfslib collates the results, which might
 * occur out of order, and delivers them to the cache so that it can update its
 * content record.
 *
 * block_type is one of:
 * - NETFS_CACHE_COLLECT_WRITE_DATA for a contiguous block of data
 * - NETFS_CACHE_COLLECT_WRITE_GAP if a discontiguity was skipped
 * - NETFS_CACHE_COLLECT_WRITE_CANCEL for a hole due to a failed/cancelled write
 *
 * The writes we made are all rounded out at both sides to the nearest DIO
 * block boundary, so if the final block contains the EOF in the middle of it
 * (rather than at the end), padding will have been written to the file.  The
 * backing file's filesize will have been updated if the write extended the
 * file; the filesize may still change due to outstanding subreqs.
 *
 * The metadata in the cache file xattr records the size of the object we have
 * stored, but the cache file EOF only goes up to where we've cached data to
 * and, furthermore, is rounded up to the nearest DIO block boundary.
 *
 * Concurrent updates should be protected against by the caller.  Netfslib
 * holds NETFS_ICTX_WB_LOCK as a lock on writeback requests.  DIO writes
 * invalidate the cookie and caching is kept disabled until all users have
 * unused the cookie.
 */
static void cachefiles_collect_write(struct netfs_io_request *wreq,
				     uoff_t start, size_t len,
				     enum netfs_cache_collect block_type)
{
	struct netfs_cache_resources *cres = &wreq->cache_resources;
	struct cachefiles_object *object = cachefiles_cres_object(cres);
	struct cachefiles_cache *cache = object->volume->cache;
	struct inode *inode;
	struct file *file = cachefiles_cres_file(cres);
	uoff_t read_limit;
	uoff_t old_size = cres->cache_i_size;
	uoff_t new_size;
	uoff_t data_to = object->object_size;
	uoff_t end = start + len;
	int ret;

	if (!file)
		return;

	inode = file_inode(file);
	new_size = i_size_read(inode);

	_enter("%llx,%zx,%x", start, len, cache->bsize);

	if (WARN_ON(old_size	& (cache->bsize - 1)) ||
	    WARN_ON(new_size	& (cache->bsize - 1)) ||
	    WARN_ON(start	& (cache->bsize - 1)) ||
	    WARN_ON(len		& (cache->bsize - 1))) {
		trace_cachefiles_io_error(object, inode, -EIO,
					  cachefiles_trace_alignment_error);
		cachefiles_remove_object_xattr(cache, object, file->f_path.dentry);
		return;
	}

	/* If this is recording a gap, due to discontiguous writes or lack of
	 * cache space, then a hole may have been introduced into the backing
	 * file.  Treat it as a zero-length data block.
	 */
	if (block_type == NETFS_CACHE_COLLECT_WRITE_GAP ||
	    block_type == NETFS_CACHE_COLLECT_WRITE_CANCEL) {
		start = end;
		len = 0;
	}

	/* Zeroth case: Single monolithic files are handled specially.
	 */
	if (wreq->origin == NETFS_WRITEBACK_SINGLE) {
		if (block_type == NETFS_CACHE_COLLECT_WRITE_GAP ||
		    block_type == NETFS_CACHE_COLLECT_WRITE_CANCEL) {
			trace_cachefiles_trunc(object, inode, data_to, 0,
					       cachefiles_trunc_zap);
			ret = cachefiles_inject_remove_error();
			if (ret == 0)
				ret = vfs_truncate(&file->f_path, 0);
			if (ret < 0) {
				trace_cachefiles_io_error(object, inode, ret,
							  cachefiles_trace_trunc_error);
				cachefiles_io_error_obj(object, "truncate failed %d", ret);
				cachefiles_remove_object_xattr(cache, object, file->f_path.dentry);
				return;
			}

			object->content_info = CACHEFILES_CONTENT_NO_DATA;
			read_limit = 0;
		} else {
			object->content_info = CACHEFILES_CONTENT_SINGLE;
			read_limit = len;
		}
		goto update_sizes_2;
	}

	/* First case: The backing file was empty. */
	if (old_size == 0) {
		if (start == 0)
			object->content_info = CACHEFILES_CONTENT_ALL;
		else
			object->content_info = CACHEFILES_CONTENT_BACKFS_MAP;
		goto update_sizes;
	}

	/* Second case: The backing file is entirely within the old object size
	 * and thus there can be no partial tail block to deal with in the
	 * cache file.
	 */
	if (old_size <= data_to) {
		if (start > old_size)
			goto discontiguous;
		goto update_sizes;
	}

	/* Third case: The write happened entirely within the bounds of the
	 * current cache file's size.
	 */
	if (end <= old_size)
		goto update_sizes;

	/* Fourth case: The write overwrote the partial tail block and extended
	 * the file.  We only need to update the object size because netfslib
	 * rounds out/pads cache writes to whole disk blocks.
	 */
	if (start < old_size)
		goto update_sizes;

	/* Fifth case: The write started from the end of the whole tail block
	 * and extended the file.  Just extend our notion of the filesize.
	 */
	if (start == old_size && old_size == data_to)
		goto update_sizes;

	/* Sixth case: The write continued on from the partial tail block and
	 * extended the file.  Need to clear the gap.
	 */
	if (start == old_size && old_size > data_to)
		goto clear_gap;

discontiguous:
	/* Seventh case: The write was beyond the EOF on the cache file, so now
	 * there's a hole in the file and we can no longer say in the metadata
	 * that we can assume we have it all.  We may also need to clear the
	 * end of the partial tail block.
	 */
	/* TODO: For the moment, we will have to use SEEK_HOLE/SEEK_DATA. */
	if (object->content_info != CACHEFILES_CONTENT_BACKFS_MAP) {
		object->content_info = CACHEFILES_CONTENT_BACKFS_MAP;
		trace_cachefiles_coherency(object, inode->i_ino, data_to, NULL,
					   CACHEFILES_CONTENT_BACKFS_MAP,
					   cachefiles_coherency_discontiguous);
	}

clear_gap:
	/* We need to clear any partial padding that got jumped over.  It
	 * *should* be all zeros, but shared-writable mmap exists...
	 */
	if (old_size > data_to) {
		trace_cachefiles_trunc(object, inode, data_to, old_size,
				       cachefiles_trunc_clear_padding);
		ret = cachefiles_inject_write_error();
		if (ret == 0)
			ret = vfs_fallocate(file, FALLOC_FL_ZERO_RANGE,
					    data_to, old_size - data_to);
		if (ret < 0) {
			trace_cachefiles_io_error(object, inode, ret,
						  cachefiles_trace_fallocate_error);
			cachefiles_io_error_obj(object, "fallocate zero pad failed %d", ret);
			cachefiles_remove_object_xattr(cache, object, file->f_path.dentry);
			return;
		}
	}

update_sizes:
	read_limit = umax(old_size, end);
update_sizes_2:
	cres->cache_i_size = read_limit;

	/* We need to be careful setting the object_size: we may have written
	 * more to the cache than to the server (due to cache DIO rounding) and
	 * the i_size set on the netfs inode may include unwritten data that
	 * the server doesn't know about yet.
	 */
	object->object_size = umin(read_limit, wreq->i_size);

	/* Raise the limit at which reads can access the file. */
	/* Update read_limit after content_info */
	atomic64_set_release(&object->read_limit, read_limit);
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
	.issue_write		= cachefiles_issue_write,
	.prepare_write		= cachefiles_prepare_write,
	.prepare_write_subreq	= cachefiles_prepare_write_subreq,
	.query_occupancy	= cachefiles_query_occupancy,
	.collect_write		= cachefiles_collect_write,
};

/*
 * Open the cache file when beginning a cache operation.
 */
bool cachefiles_begin_operation(struct netfs_cache_resources *cres,
				enum fscache_want_state want_state)
{
	struct cachefiles_object *object = cachefiles_cres_object(cres);
	struct file *file;

	cres->dio_size = object->volume->cache->bsize;

	if (!cachefiles_cres_file(cres)) {
		cres->ops = &cachefiles_netfs_cache_ops;
		cres->object_id = object->debug_id;
		if (object->file) {
			spin_lock(&object->lock);
			file = object->file;
			if (!cres->cache_priv2 && file) {
				cres->cache_priv2 = get_file(file);
				cres->cache_i_size = i_size_read(file_inode(file));
			}
			spin_unlock(&object->lock);
		}
	}

	if (!cachefiles_cres_file(cres) && want_state != FSCACHE_WANT_PARAMS) {
		pr_err("failed to get cres->file\n");
		return false;
	}

	return true;
}
