// SPDX-License-Identifier: GPL-2.0-only
/*
 * "splice": joining two ropes together by interweaving their strands.
 *
 * This is the "extended pipe" functionality, where a pipe is used as
 * an arbitrary in-memory buffer. Think of a pipe as a small kernel
 * buffer that you can use to transfer data from one end to the other.
 *
 * The traditional unix read/write is extended with a "splice()" operation
 * that transfers data buffers to or from a pipe buffer.
 *
 * Named by Larry McVoy, original implementation from Linus, extended by
 * Jens to support splicing to files, network, direct splicing, etc and
 * fixing lots of bugs.
 *
 * Copyright (C) 2005-2006 Jens Axboe <axboe@kernel.dk>
 * Copyright (C) 2005-2006 Linus Torvalds <torvalds@osdl.org>
 * Copyright (C) 2006 Ingo Molnar <mingo@elte.hu>
 *
 */
#include <linux/bvec.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/uio.h>
#include <linux/security.h>
#include <linux/gfp.h>
#include <linux/socket.h>
#include <linux/sched/signal.h>

#include "internal.h"

/*
 * Attempt to steal a page from a pipe buffer. This should perhaps go into
 * a vm helper function, it's already simplified quite a bit by the
 * addition of remove_mapping(). If success is returned, the caller may
 * attempt to reuse this page for another destination.
 */
static bool page_cache_pipe_buf_try_steal(struct pipe_inode_info *pipe,
		struct pipe_buffer *buf)
{
	struct folio *folio = page_folio(buf->page);
	struct address_space *mapping;

	folio_lock(folio);

	mapping = folio_mapping(folio);
	if (mapping) {
		WARN_ON(!folio_test_uptodate(folio));

		/*
		 * At least for ext2 with nobh option, we need to wait on
		 * writeback completing on this folio, since we'll remove it
		 * from the pagecache.  Otherwise truncate wont wait on the
		 * folio, allowing the disk blocks to be reused by someone else
		 * before we actually wrote our data to them. fs corruption
		 * ensues.
		 */
		folio_wait_writeback(folio);

		if (folio_has_private(folio) &&
		    !filemap_release_folio(folio, GFP_KERNEL))
			goto out_unlock;

		/*
		 * If we succeeded in removing the mapping, set LRU flag
		 * and return good.
		 */
		if (remove_mapping(mapping, folio)) {
			buf->flags |= PIPE_BUF_FLAG_LRU;
			return true;
		}
	}

	/*
	 * Raced with truncate or failed to remove folio from current
	 * address space, unlock and return failure.
	 */
out_unlock:
	folio_unlock(folio);
	return false;
}

static void page_cache_pipe_buf_release(struct pipe_inode_info *pipe,
					struct pipe_buffer *buf)
{
	put_page(buf->page);
	buf->flags &= ~PIPE_BUF_FLAG_LRU;
}

/*
 * Check whether the contents of buf is OK to access. Since the content
 * is a page cache page, IO may be in flight.
 */
static int page_cache_pipe_buf_confirm(struct pipe_inode_info *pipe,
				       struct pipe_buffer *buf)
{
	struct page *page = buf->page;
	int err;

	if (!PageUptodate(page)) {
		lock_page(page);

		/*
		 * Page got truncated/unhashed. This will cause a 0-byte
		 * splice, if this is the first page.
		 */
		if (!page->mapping) {
			err = -ENODATA;
			goto error;
		}

		/*
		 * Uh oh, read-error from disk.
		 */
		if (!PageUptodate(page)) {
			err = -EIO;
			goto error;
		}

		/*
		 * Page is ok afterall, we are done.
		 */
		unlock_page(page);
	}

	return 0;
error:
	unlock_page(page);
	return err;
}

const struct pipe_buf_operations page_cache_pipe_buf_ops = {
	.confirm	= page_cache_pipe_buf_confirm,
	.release	= page_cache_pipe_buf_release,
	.try_steal	= page_cache_pipe_buf_try_steal,
	.get		= generic_pipe_buf_get,
};

static bool user_page_pipe_buf_try_steal(struct pipe_inode_info *pipe,
		struct pipe_buffer *buf)
{
	if (!(buf->flags & PIPE_BUF_FLAG_GIFT))
		return false;

	buf->flags |= PIPE_BUF_FLAG_LRU;
	return generic_pipe_buf_try_steal(pipe, buf);
}

static const struct pipe_buf_operations user_page_pipe_buf_ops = {
	.release	= page_cache_pipe_buf_release,
	.try_steal	= user_page_pipe_buf_try_steal,
	.get		= generic_pipe_buf_get,
};

static void wakeup_pipe_readers(struct pipe_inode_info *pipe)
{
	smp_mb();
	if (waitqueue_active(&pipe->rd_wait))
		wake_up_interruptible(&pipe->rd_wait);
	kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
}

/**
 * splice_to_pipe - fill passed data into a pipe
 * @pipe:	pipe to fill
 * @spd:	data to fill
 *
 * Description:
 *    @spd contains a map of pages and len/offset tuples, along with
 *    the struct pipe_buf_operations associated with these pages. This
 *    function will link that data to the pipe.
 *
 */
ssize_t splice_to_pipe(struct pipe_inode_info *pipe,
		       struct splice_pipe_desc *spd)
{
	unsigned int spd_pages = spd->nr_pages;
	unsigned int tail = pipe->tail;
	unsigned int head = pipe->head;
	unsigned int mask = pipe->ring_size - 1;
	int ret = 0, page_nr = 0;

	if (!spd_pages)
		return 0;

	if (unlikely(!pipe->readers)) {
		send_sig(SIGPIPE, current, 0);
		ret = -EPIPE;
		goto out;
	}

	while (!pipe_full(head, tail, pipe->max_usage)) {
		struct pipe_buffer *buf = &pipe->bufs[head & mask];

		buf->page = spd->pages[page_nr];
		buf->offset = spd->partial[page_nr].offset;
		buf->len = spd->partial[page_nr].len;
		buf->private = spd->partial[page_nr].private;
		buf->ops = spd->ops;
		buf->flags = 0;

		head++;
		pipe->head = head;
		page_nr++;
		ret += buf->len;

		if (!--spd->nr_pages)
			break;
	}

	if (!ret)
		ret = -EAGAIN;

out:
	while (page_nr < spd_pages)
		spd->spd_release(spd, page_nr++);

	return ret;
}
EXPORT_SYMBOL_GPL(splice_to_pipe);

ssize_t add_to_pipe(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
{
	unsigned int head = pipe->head;
	unsigned int tail = pipe->tail;
	unsigned int mask = pipe->ring_size - 1;
	int ret;

	if (unlikely(!pipe->readers)) {
		send_sig(SIGPIPE, current, 0);
		ret = -EPIPE;
	} else if (pipe_full(head, tail, pipe->max_usage)) {
		ret = -EAGAIN;
	} else {
		pipe->bufs[head & mask] = *buf;
		pipe->head = head + 1;
		return buf->len;
	}
	pipe_buf_release(pipe, buf);
	return ret;
}
EXPORT_SYMBOL(add_to_pipe);

/*
 * Check if we need to grow the arrays holding pages and partial page
 * descriptions.
 */
int splice_grow_spd(const struct pipe_inode_info *pipe, struct splice_pipe_desc *spd)
{
	unsigned int max_usage = READ_ONCE(pipe->max_usage);

	spd->nr_pages_max = max_usage;
	if (max_usage <= PIPE_DEF_BUFFERS)
		return 0;

	spd->pages = kmalloc_array(max_usage, sizeof(struct page *), GFP_KERNEL);
	spd->partial = kmalloc_array(max_usage, sizeof(struct partial_page),
				     GFP_KERNEL);

	if (spd->pages && spd->partial)
		return 0;

	kfree(spd->pages);
	kfree(spd->partial);
	return -ENOMEM;
}

void splice_shrink_spd(struct splice_pipe_desc *spd)
{
	if (spd->nr_pages_max <= PIPE_DEF_BUFFERS)
		return;

	kfree(spd->pages);
	kfree(spd->partial);
}

/**
 * generic_file_splice_read - splice data from file to a pipe
 * @in:		file to splice from
 * @ppos:	position in @in
 * @pipe:	pipe to splice to
 * @len:	number of bytes to splice
 * @flags:	splice modifier flags
 *
 * Description:
 *    Will read pages from given file and fill them into a pipe. Can be
 *    used as long as it has more or less sane ->read_iter().
 *
 */
ssize_t generic_file_splice_read(struct file *in, loff_t *ppos,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags)
{
	struct iov_iter to;
	struct kiocb kiocb;
	int ret;

	iov_iter_pipe(&to, ITER_DEST, pipe, len);
	init_sync_kiocb(&kiocb, in);
	kiocb.ki_pos = *ppos;
	ret = call_read_iter(in, &kiocb, &to);
	if (ret > 0) {
		*ppos = kiocb.ki_pos;
		file_accessed(in);
	} else if (ret < 0) {
		/* free what was emitted */
		pipe_discard_from(pipe, to.start_head);
		/*
		 * callers of ->splice_read() expect -EAGAIN on
		 * "can't put anything in there", rather than -EFAULT.
		 */
		if (ret == -EFAULT)
			ret = -EAGAIN;
	}

	return ret;
}
EXPORT_SYMBOL(generic_file_splice_read);

const struct pipe_buf_operations default_pipe_buf_ops = {
	.release	= generic_pipe_buf_release,
	.try_steal	= generic_pipe_buf_try_steal,
	.get		= generic_pipe_buf_get,
};

/* Pipe buffer operations for a socket and similar. */
const struct pipe_buf_operations nosteal_pipe_buf_ops = {
	.release	= generic_pipe_buf_release,
	.get		= generic_pipe_buf_get,
};
EXPORT_SYMBOL(nosteal_pipe_buf_ops);

/*
 * Send 'sd->len' bytes to socket from 'sd->file' at position 'sd->pos'
 * using sendpage(). Return the number of bytes sent.
 */
static int pipe_to_sendpage(struct pipe_inode_info *pipe,
			    struct pipe_buffer *buf, struct splice_desc *sd)
{
	struct file *file = sd->u.file;
	loff_t pos = sd->pos;
	int more;

	if (!likely(file->f_op->sendpage))
		return -EINVAL;

	more = (sd->flags & SPLICE_F_MORE) ? MSG_MORE : 0;

	if (sd->len < sd->total_len &&
	    pipe_occupancy(pipe->head, pipe->tail) > 1)
		more |= MSG_SENDPAGE_NOTLAST;

	return file->f_op->sendpage(file, buf->page, buf->offset,
				    sd->len, &pos, more);
}

static void wakeup_pipe_writers(struct pipe_inode_info *pipe)
{
	smp_mb();
	if (waitqueue_active(&pipe->wr_wait))
		wake_up_interruptible(&pipe->wr_wait);
	kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
}

/**
 * splice_from_pipe_feed - feed available data from a pipe to a file
 * @pipe:	pipe to splice from
 * @sd:		information to @actor
 * @actor:	handler that splices the data
 *
 * Description:
 *    This function loops over the pipe and calls @actor to do the
 *    actual moving of a single struct pipe_buffer to the desired
 *    destination.  It returns when there's no more buffers left in
 *    the pipe or if the requested number of bytes (@sd->total_len)
 *    have been copied.  It returns a positive number (one) if the
 *    pipe needs to be filled with more data, zero if the required
 *    number of bytes have been copied and -errno on error.
 *
 *    This, together with splice_from_pipe_{begin,end,next}, may be
 *    used to implement the functionality of __splice_from_pipe() when
 *    locking is required around copying the pipe buffers to the
 *    destination.
 */
static int splice_from_pipe_feed(struct pipe_inode_info *pipe, struct splice_desc *sd,
			  splice_actor *actor)
{
	unsigned int head = pipe->head;
	unsigned int tail = pipe->tail;
	unsigned int mask = pipe->ring_size - 1;
	int ret;

	while (!pipe_empty(head, tail)) {
		struct pipe_buffer *buf = &pipe->bufs[tail & mask];

		sd->len = buf->len;
		if (sd->len > sd->total_len)
			sd->len = sd->total_len;

		ret = pipe_buf_confirm(pipe, buf);
		if (unlikely(ret)) {
			if (ret == -ENODATA)
				ret = 0;
			return ret;
		}

		ret = actor(pipe, buf, sd);
		if (ret <= 0)
			return ret;

		buf->offset += ret;
		buf->len -= ret;

		sd->num_spliced += ret;
		sd->len -= ret;
		sd->pos += ret;
		sd->total_len -= ret;

		if (!buf->len) {
			pipe_buf_release(pipe, buf);
			tail++;
			pipe->tail = tail;
			if (pipe->files)
				sd->need_wakeup = true;
		}

		if (!sd->total_len)
			return 0;
	}

	return 1;
}

/* We know we have a pipe buffer, but maybe it's empty? */
static inline bool eat_empty_buffer(struct pipe_inode_info *pipe)
{
	unsigned int tail = pipe->tail;
	unsigned int mask = pipe->ring_size - 1;
	struct pipe_buffer *buf = &pipe->bufs[tail & mask];

	if (unlikely(!buf->len)) {
		pipe_buf_release(pipe, buf);
		pipe->tail = tail+1;
		return true;
	}

	return false;
}

/**
 * splice_from_pipe_next - wait for some data to splice from
 * @pipe:	pipe to splice from
 * @sd:		information about the splice operation
 *
 * Description:
 *    This function will wait for some data and return a positive
 *    value (one) if pipe buffers are available.  It will return zero
 *    or -errno if no more data needs to be spliced.
 */
static int splice_from_pipe_next(struct pipe_inode_info *pipe, struct splice_desc *sd)
{
	/*
	 * Check for signal early to make process killable when there are
	 * always buffers available
	 */
	if (signal_pending(current))
		return -ERESTARTSYS;

repeat:
	while (pipe_empty(pipe->head, pipe->tail)) {
		if (!pipe->writers)
			return 0;

		if (sd->num_spliced)
			return 0;

		if (sd->flags & SPLICE_F_NONBLOCK)
			return -EAGAIN;

		if (signal_pending(current))
			return -ERESTARTSYS;

		if (sd->need_wakeup) {
			wakeup_pipe_writers(pipe);
			sd->need_wakeup = false;
		}

		pipe_wait_readable(pipe);
	}

	if (eat_empty_buffer(pipe))
		goto repeat;

	return 1;
}

/**
 * splice_from_pipe_begin - start splicing from pipe
 * @sd:		information about the splice operation
 *
 * Description:
 *    This function should be called before a loop containing
 *    splice_from_pipe_next() and splice_from_pipe_feed() to
 *    initialize the necessary fields of @sd.
 */
static void splice_from_pipe_begin(struct splice_desc *sd)
{
	sd->num_spliced = 0;
	sd->need_wakeup = false;
}

/**
 * splice_from_pipe_end - finish splicing from pipe
 * @pipe:	pipe to splice from
 * @sd:		information about the splice operation
 *
 * Description:
 *    This function will wake up pipe writers if necessary.  It should
 *    be called after a loop containing splice_from_pipe_next() and
 *    splice_from_pipe_feed().
 */
static void splice_from_pipe_end(struct pipe_inode_info *pipe, struct splice_desc *sd)
{
	if (sd->need_wakeup)
		wakeup_pipe_writers(pipe);
}

/**
 * __splice_from_pipe - splice data from a pipe to given actor
 * @pipe:	pipe to splice from
 * @sd:		information to @actor
 * @actor:	handler that splices the data
 *
 * Description:
 *    This function does little more than loop over the pipe and call
 *    @actor to do the actual moving of a single struct pipe_buffer to
 *    the desired destination. See pipe_to_file, pipe_to_sendpage, or
 *    pipe_to_user.
 *
 */
ssize_t __splice_from_pipe(struct pipe_inode_info *pipe, struct splice_desc *sd,
			   splice_actor *actor)
{
	int ret;

	splice_from_pipe_begin(sd);
	do {
		cond_resched();
		ret = splice_from_pipe_next(pipe, sd);
		if (ret > 0)
			ret = splice_from_pipe_feed(pipe, sd, actor);
	} while (ret > 0);
	splice_from_pipe_end(pipe, sd);

	return sd->num_spliced ? sd->num_spliced : ret;
}
EXPORT_SYMBOL(__splice_from_pipe);

/**
 * splice_from_pipe - splice data from a pipe to a file
 * @pipe:	pipe to splice from
 * @out:	file to splice to
 * @ppos:	position in @out
 * @len:	how many bytes to splice
 * @flags:	splice modifier flags
 * @actor:	handler that splices the data
 *
 * Description:
 *    See __splice_from_pipe. This function locks the pipe inode,
 *    otherwise it's identical to __splice_from_pipe().
 *
 */
ssize_t splice_from_pipe(struct pipe_inode_info *pipe, struct file *out,
			 loff_t *ppos, size_t len, unsigned int flags,
			 splice_actor *actor)
{
	ssize_t ret;
	struct splice_desc sd = {
		.total_len = len,
		.flags = flags,
		.pos = *ppos,
		.u.file = out,
	};

	pipe_lock(pipe);
	ret = __splice_from_pipe(pipe, &sd, actor);
	pipe_unlock(pipe);

	return ret;
}

/**
 * iter_file_splice_write - splice data from a pipe to a file
 * @pipe:	pipe info
 * @out:	file to write to
 * @ppos:	position in @out
 * @len:	number of bytes to splice
 * @flags:	splice modifier flags
 *
 * Description:
 *    Will either move or copy pages (determined by @flags options) from
 *    the given pipe inode to the given file.
 *    This one is ->write_iter-based.
 *
 */
ssize_t
iter_file_splice_write(struct pipe_inode_info *pipe, struct file *out,
			  loff_t *ppos, size_t len, unsigned int flags)
{
	struct splice_desc sd = {
		.total_len = len,
		.flags = flags,
		.pos = *ppos,
		.u.file = out,
	};
	int nbufs = pipe->max_usage;
	struct bio_vec *array = kcalloc(nbufs, sizeof(struct bio_vec),
					GFP_KERNEL);
	ssize_t ret;

	if (unlikely(!array))
		return -ENOMEM;

	pipe_lock(pipe);

	splice_from_pipe_begin(&sd);
	while (sd.total_len) {
		struct iov_iter from;
		unsigned int head, tail, mask;
		size_t left;
		int n;

		ret = splice_from_pipe_next(pipe, &sd);
		if (ret <= 0)
			break;

		if (unlikely(nbufs < pipe->max_usage)) {
			kfree(array);
			nbufs = pipe->max_usage;
			array = kcalloc(nbufs, sizeof(struct bio_vec),
					GFP_KERNEL);
			if (!array) {
				ret = -ENOMEM;
				break;
			}
		}

		head = pipe->head;
		tail = pipe->tail;
		mask = pipe->ring_size - 1;

		/* build the vector */
		left = sd.total_len;
		for (n = 0; !pipe_empty(head, tail) && left && n < nbufs; tail++) {
			struct pipe_buffer *buf = &pipe->bufs[tail & mask];
			size_t this_len = buf->len;

			/* zero-length bvecs are not supported, skip them */
			if (!this_len)
				continue;
			this_len = min(this_len, left);

			ret = pipe_buf_confirm(pipe, buf);
			if (unlikely(ret)) {
				if (ret == -ENODATA)
					ret = 0;
				goto done;
			}

			array[n].bv_page = buf->page;
			array[n].bv_len = this_len;
			array[n].bv_offset = buf->offset;
			left -= this_len;
			n++;
		}

		iov_iter_bvec(&from, ITER_SOURCE, array, n, sd.total_len - left);
		ret = vfs_iter_write(out, &from, &sd.pos, 0);
		if (ret <= 0)
			break;

		sd.num_spliced += ret;
		sd.total_len -= ret;
		*ppos = sd.pos;

		/* dismiss the fully eaten buffers, adjust the partial one */
		tail = pipe->tail;
		while (ret) {
			struct pipe_buffer *buf = &pipe->bufs[tail & mask];
			if (ret >= buf->len) {
				ret -= buf->len;
				buf->len = 0;
				pipe_buf_release(pipe, buf);
				tail++;
				pipe->tail = tail;
				if (pipe->files)
					sd.need_wakeup = true;
			} else {
				buf->offset += ret;
				buf->len -= ret;
				ret = 0;
			}
		}
	}
done:
	kfree(array);
	splice_from_pipe_end(pipe, &sd);

	pipe_unlock(pipe);

	if (sd.num_spliced)
		ret = sd.num_spliced;

	return ret;
}

EXPORT_SYMBOL(iter_file_splice_write);

/**
 * generic_splice_sendpage - splice data from a pipe to a socket
 * @pipe:	pipe to splice from
 * @out:	socket to write to
 * @ppos:	position in @out
 * @len:	number of bytes to splice
 * @flags:	splice modifier flags
 *
 * Description:
 *    Will send @len bytes from the pipe to a network socket. No data copying
 *    is involved.
 *
 */
ssize_t generic_splice_sendpage(struct pipe_inode_info *pipe, struct file *out,
				loff_t *ppos, size_t len, unsigned int flags)
{
	return splice_from_pipe(pipe, out, ppos, len, flags, pipe_to_sendpage);
}

EXPORT_SYMBOL(generic_splice_sendpage);

static int warn_unsupported(struct file *file, const char *op)
{
	pr_debug_ratelimited(
		"splice %s not supported for file %pD4 (pid: %d comm: %.20s)\n",
		op, file, current->pid, current->comm);
	return -EINVAL;
}

/*
 * Attempt to initiate a splice from pipe to file.
 */
static long do_splice_from(struct pipe_inode_info *pipe, struct file *out,
			   loff_t *ppos, size_t len, unsigned int flags)
{
	if (unlikely(!out->f_op->splice_write))
		return warn_unsupported(out, "write");
	return out->f_op->splice_write(pipe, out, ppos, len, flags);
}

/*
 * Attempt to initiate a splice from a file to a pipe.
 */
static long do_splice_to(struct file *in, loff_t *ppos,
			 struct pipe_inode_info *pipe, size_t len,
			 unsigned int flags)
{
	unsigned int p_space;
	int ret;

	if (unlikely(!(in->f_mode & FMODE_READ)))
		return -EBADF;

	/* Don't try to read more the pipe has space for. */
	p_space = pipe->max_usage - pipe_occupancy(pipe->head, pipe->tail);
	len = min_t(size_t, len, p_space << PAGE_SHIFT);

	ret = rw_verify_area(READ, in, ppos, len);
	if (unlikely(ret < 0))
		return ret;

	if (unlikely(len > MAX_RW_COUNT))
		len = MAX_RW_COUNT;

	if (unlikely(!in->f_op->splice_read))
		return warn_unsupported(in, "read");
	return in->f_op->splice_read(in, ppos, pipe, len, flags);
}

/**
 * splice_direct_to_actor - splices data directly between two non-pipes
 * @in:		file to splice from
 * @sd:		actor information on where to splice to
 * @actor:	handles the data splicing
 *
 * Description:
 *    This is a special case helper to splice directly between two
 *    points, without requiring an explicit pipe. Internally an allocated
 *    pipe is cached in the process, and reused during the lifetime of
 *    that process.
 *
 */
ssize_t splice_direct_to_actor(struct file *in, struct splice_desc *sd,
			       splice_direct_actor *actor)
{
	struct pipe_inode_info *pipe;
	long ret, bytes;
	size_t len;
	int i, flags, more;

	/*
	 * We require the input to be seekable, as we don't want to randomly
	 * drop data for eg socket -> socket splicing. Use the piped splicing
	 * for that!
	 */
	if (unlikely(!(in->f_mode & FMODE_LSEEK)))
		return -EINVAL;

	/*
	 * neither in nor out is a pipe, setup an internal pipe attached to
	 * 'out' and transfer the wanted data from 'in' to 'out' through that
	 */
	pipe = current->splice_pipe;
	if (unlikely(!pipe)) {
		pipe = alloc_pipe_info();
		if (!pipe)
			return -ENOMEM;

		/*
		 * We don't have an immediate reader, but we'll read the stuff
		 * out of the pipe right after the splice_to_pipe(). So set
		 * PIPE_READERS appropriately.
		 */
		pipe->readers = 1;

		current->splice_pipe = pipe;
	}

	/*
	 * Do the splice.
	 */
	ret = 0;
	bytes = 0;
	len = sd->total_len;
	flags = sd->flags;

	/*
	 * Don't block on output, we have to drain the direct pipe.
	 */
	sd->flags &= ~SPLICE_F_NONBLOCK;
	more = sd->flags & SPLICE_F_MORE;

	WARN_ON_ONCE(!pipe_empty(pipe->head, pipe->tail));

	while (len) {
		size_t read_len;
		loff_t pos = sd->pos, prev_pos = pos;

		ret = do_splice_to(in, &pos, pipe, len, flags);
		if (unlikely(ret <= 0))
			goto out_release;

		read_len = ret;
		sd->total_len = read_len;

		/*
		 * If more data is pending, set SPLICE_F_MORE
		 * If this is the last data and SPLICE_F_MORE was not set
		 * initially, clears it.
		 */
		if (read_len < len)
			sd->flags |= SPLICE_F_MORE;
		else if (!more)
			sd->flags &= ~SPLICE_F_MORE;
		/*
		 * NOTE: nonblocking mode only applies to the input. We
		 * must not do the output in nonblocking mode as then we
		 * could get stuck data in the internal pipe:
		 */
		ret = actor(pipe, sd);
		if (unlikely(ret <= 0)) {
			sd->pos = prev_pos;
			goto out_release;
		}

		bytes += ret;
		len -= ret;
		sd->pos = pos;

		if (ret < read_len) {
			sd->pos = prev_pos + ret;
			goto out_release;
		}
	}

done:
	pipe->tail = pipe->head = 0;
	file_accessed(in);
	return bytes;

out_release:
	/*
	 * If we did an incomplete transfer we must release
	 * the pipe buffers in question:
	 */
	for (i = 0; i < pipe->ring_size; i++) {
		struct pipe_buffer *buf = &pipe->bufs[i];

		if (buf->ops)
			pipe_buf_release(pipe, buf);
	}

	if (!bytes)
		bytes = ret;

	goto done;
}
EXPORT_SYMBOL(splice_direct_to_actor);

static int direct_splice_actor(struct pipe_inode_info *pipe,
			       struct splice_desc *sd)
{
	struct file *file = sd->u.file;

	return do_splice_from(pipe, file, sd->opos, sd->total_len,
			      sd->flags);
}

/**
 * do_splice_direct - splices data directly between two files
 * @in:		file to splice from
 * @ppos:	input file offset
 * @out:	file to splice to
 * @opos:	output file offset
 * @len:	number of bytes to splice
 * @flags:	splice modifier flags
 *
 * Description:
 *    For use by do_sendfile(). splice can easily emulate sendfile, but
 *    doing it in the application would incur an extra system call
 *    (splice in + splice out, as compared to just sendfile()). So this helper
 *    can splice directly through a process-private pipe.
 *
 */
long do_splice_direct(struct file *in, loff_t *ppos, struct file *out,
		      loff_t *opos, size_t len, unsigned int flags)
{
	struct splice_desc sd = {
		.len		= len,
		.total_len	= len,
		.flags		= flags,
		.pos		= *ppos,
		.u.file		= out,
		.opos		= opos,
	};
	long ret;

	if (unlikely(!(out->f_mode & FMODE_WRITE)))
		return -EBADF;

	if (unlikely(out->f_flags & O_APPEND))
		return -EINVAL;

	ret = rw_verify_area(WRITE, out, opos, len);
	if (unlikely(ret < 0))
		return ret;

	ret = splice_direct_to_actor(in, &sd, direct_splice_actor);
	if (ret > 0)
		*ppos = sd.pos;

	return ret;
}
EXPORT_SYMBOL(do_splice_direct);

static int wait_for_space(struct pipe_inode_info *pipe, unsigned flags)
{
	for (;;) {
		if (unlikely(!pipe->readers)) {
			send_sig(SIGPIPE, current, 0);
			return -EPIPE;
		}
		if (!pipe_full(pipe->head, pipe->tail, pipe->max_usage))
			return 0;
		if (flags & SPLICE_F_NONBLOCK)
			return -EAGAIN;
		if (signal_pending(current))
			return -ERESTARTSYS;
		pipe_wait_writable(pipe);
	}
}

static int splice_pipe_to_pipe(struct pipe_inode_info *ipipe,
			       struct pipe_inode_info *opipe,
			       size_t len, unsigned int flags);

long splice_file_to_pipe(struct file *in,
			 struct pipe_inode_info *opipe,
			 loff_t *offset,
			 size_t len, unsigned int flags)
{
	long ret;

	pipe_lock(opipe);
	ret = wait_for_space(opipe, flags);
	if (!ret)
		ret = do_splice_to(in, offset, opipe, len, flags);
	pipe_unlock(opipe);
	if (ret > 0)
		wakeup_pipe_readers(opipe);
	return ret;
}

/*
 * Determine where to splice to/from.
 */
long do_splice(struct file *in, loff_t *off_in, struct file *out,
	       loff_t *off_out, size_t len, unsigned int flags)
{
	struct pipe_inode_info *ipipe;
	struct pipe_inode_info *opipe;
	loff_t offset;
	long ret;

	if (unlikely(!(in->f_mode & FMODE_READ) ||
		     !(out->f_mode & FMODE_WRITE)))
		return -EBADF;

	ipipe = get_pipe_info(in, true);
	opipe = get_pipe_info(out, true);

	if (ipipe && opipe) {
		if (off_in || off_out)
			return -ESPIPE;

		/* Splicing to self would be fun, but... */
		if (ipipe == opipe)
			return -EINVAL;

		if ((in->f_flags | out->f_flags) & O_NONBLOCK)
			flags |= SPLICE_F_NONBLOCK;

		return splice_pipe_to_pipe(ipipe, opipe, len, flags);
	}

	if (ipipe) {
		if (off_in)
			return -ESPIPE;
		if (off_out) {
			if (!(out->f_mode & FMODE_PWRITE))
				return -EINVAL;
			offset = *off_out;
		} else {
			offset = out->f_pos;
		}

		if (unlikely(out->f_flags & O_APPEND))
			return -EINVAL;

		ret = rw_verify_area(WRITE, out, &offset, len);
		if (unlikely(ret < 0))
			return ret;

		if (in->f_flags & O_NONBLOCK)
			flags |= SPLICE_F_NONBLOCK;

		file_start_write(out);
		ret = do_splice_from(ipipe, out, &offset, len, flags);
		file_end_write(out);

		if (!off_out)
			out->f_pos = offset;
		else
			*off_out = offset;

		return ret;
	}

	if (opipe) {
		if (off_out)
			return -ESPIPE;
		if (off_in) {
			if (!(in->f_mode & FMODE_PREAD))
				return -EINVAL;
			offset = *off_in;
		} else {
			offset = in->f_pos;
		}

		if (out->f_flags & O_NONBLOCK)
			flags |= SPLICE_F_NONBLOCK;

		ret = splice_file_to_pipe(in, opipe, &offset, len, flags);
		if (!off_in)
			in->f_pos = offset;
		else
			*off_in = offset;

		return ret;
	}

	return -EINVAL;
}

static long __do_splice(struct file *in, loff_t __user *off_in,
			struct file *out, loff_t __user *off_out,
			size_t len, unsigned int flags)
{
	struct pipe_inode_info *ipipe;
	struct pipe_inode_info *opipe;
	loff_t offset, *__off_in = NULL, *__off_out = NULL;
	long ret;

	ipipe = get_pipe_info(in, true);
	opipe = get_pipe_info(out, true);

	if (ipipe && off_in)
		return -ESPIPE;
	if (opipe && off_out)
		return -ESPIPE;

	if (off_out) {
		if (copy_from_user(&offset, off_out, sizeof(loff_t)))
			return -EFAULT;
		__off_out = &offset;
	}
	if (off_in) {
		if (copy_from_user(&offset, off_in, sizeof(loff_t)))
			return -EFAULT;
		__off_in = &offset;
	}

	ret = do_splice(in, __off_in, out, __off_out, len, flags);
	if (ret < 0)
		return ret;

	if (__off_out && copy_to_user(off_out, __off_out, sizeof(loff_t)))
		return -EFAULT;
	if (__off_in && copy_to_user(off_in, __off_in, sizeof(loff_t)))
		return -EFAULT;

	return ret;
}

static int iter_to_pipe(struct iov_iter *from,
			struct pipe_inode_info *pipe,
			unsigned flags)
{
	struct pipe_buffer buf = {
		.ops = &user_page_pipe_buf_ops,
		.flags = flags
	};
	size_t total = 0;
	int ret = 0;

	while (iov_iter_count(from)) {
		struct page *pages[16];
		ssize_t left;
		size_t start;
		int i, n;

		left = iov_iter_get_pages2(from, pages, ~0UL, 16, &start);
		if (left <= 0) {
			ret = left;
			break;
		}

		n = DIV_ROUND_UP(left + start, PAGE_SIZE);
		for (i = 0; i < n; i++) {
			int size = min_t(int, left, PAGE_SIZE - start);

			buf.page = pages[i];
			buf.offset = start;
			buf.len = size;
			ret = add_to_pipe(pipe, &buf);
			if (unlikely(ret < 0)) {
				iov_iter_revert(from, left);
				// this one got dropped by add_to_pipe()
				while (++i < n)
					put_page(pages[i]);
				goto out;
			}
			total += ret;
			left -= size;
			start = 0;
		}
	}
out:
	return total ? total : ret;
}

static int pipe_to_user(struct pipe_inode_info *pipe, struct pipe_buffer *buf,
			struct splice_desc *sd)
{
	int n = copy_page_to_iter(buf->page, buf->offset, sd->len, sd->u.data);
	return n == sd->len ? n : -EFAULT;
}

/*
 * For lack of a better implementation, implement vmsplice() to userspace
 * as a simple copy of the pipes pages to the user iov.
 */
static long vmsplice_to_user(struct file *file, struct iov_iter *iter,
			     unsigned int flags)
{
	struct pipe_inode_info *pipe = get_pipe_info(file, true);
	struct splice_desc sd = {
		.total_len = iov_iter_count(iter),
		.flags = flags,
		.u.data = iter
	};
	long ret = 0;

	if (!pipe)
		return -EBADF;

	if (sd.total_len) {
		pipe_lock(pipe);
		ret = __splice_from_pipe(pipe, &sd, pipe_to_user);
		pipe_unlock(pipe);
	}

	return ret;
}

/*
 * vmsplice splices a user address range into a pipe. It can be thought of
 * as splice-from-memory, where the regular splice is splice-from-file (or
 * to file). In both cases the output is a pipe, naturally.
 */
static long vmsplice_to_pipe(struct file *file, struct iov_iter *iter,
			     unsigned int flags)
{
	struct pipe_inode_info *pipe;
	long ret = 0;
	unsigned buf_flag = 0;

	if (flags & SPLICE_F_GIFT)
		buf_flag = PIPE_BUF_FLAG_GIFT;

	pipe = get_pipe_info(file, true);
	if (!pipe)
		return -EBADF;

	pipe_lock(pipe);
	ret = wait_for_space(pipe, flags);
	if (!ret)
		ret = iter_to_pipe(iter, pipe, buf_flag);
	pipe_unlock(pipe);
	if (ret > 0)
		wakeup_pipe_readers(pipe);
	return ret;
}

static int vmsplice_type(struct fd f, int *type)
{
	if (!f.file)
		return -EBADF;
	if (f.file->f_mode & FMODE_WRITE) {
		*type = ITER_SOURCE;
	} else if (f.file->f_mode & FMODE_READ) {
		*type = ITER_DEST;
	} else {
		fdput(f);
		return -EBADF;
	}
	return 0;
}

/*
 * Note that vmsplice only really supports true splicing _from_ user memory
 * to a pipe, not the other way around. Splicing from user memory is a simple
 * operation that can be supported without any funky alignment restrictions
 * or nasty vm tricks. We simply map in the user memory and fill them into
 * a pipe. The reverse isn't quite as easy, though. There are two possible
 * solutions for that:
 *
 *	- memcpy() the data internally, at which point we might as well just
 *	  do a regular read() on the buffer anyway.
 *	- Lots of nasty vm tricks, that are neither fast nor flexible (it
 *	  has restriction limitations on both ends of the pipe).
 *
 * Currently we punt and implement it as a normal copy, see pipe_to_user().
 *
 */
SYSCALL_DEFINE4(vmsplice, int, fd, const struct iovec __user *, uiov,
		unsigned long, nr_segs, unsigned int, flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	ssize_t error;
	struct fd f;
	int type;

	if (unlikely(flags & ~SPLICE_F_ALL))
		return -EINVAL;

	f = fdget(fd);
	error = vmsplice_type(f, &type);
	if (error)
		return error;

	error = import_iovec(type, uiov, nr_segs,
			     ARRAY_SIZE(iovstack), &iov, &iter);
	if (error < 0)
		goto out_fdput;

	if (!iov_iter_count(&iter))
		error = 0;
	else if (type == ITER_SOURCE)
		error = vmsplice_to_pipe(f.file, &iter, flags);
	else
		error = vmsplice_to_user(f.file, &iter, flags);

	kfree(iov);
out_fdput:
	fdput(f);
	return error;
}

SYSCALL_DEFINE6(splice, int, fd_in, loff_t __user *, off_in,
		int, fd_out, loff_t __user *, off_out,
		size_t, len, unsigned int, flags)
{
	struct fd in, out;
	long error;

	if (unlikely(!len))
		return 0;

	if (unlikely(flags & ~SPLICE_F_ALL))
		return -EINVAL;

	error = -EBADF;
	in = fdget(fd_in);
	if (in.file) {
		out = fdget(fd_out);
		if (out.file) {
			error = __do_splice(in.file, off_in, out.file, off_out,
						len, flags);
			fdput(out);
		}
		fdput(in);
	}
	return error;
}

/*
 * Make sure there's data to read. Wait for input if we can, otherwise
 * return an appropriate error.
 */
static int ipipe_prep(struct pipe_inode_info *pipe, unsigned int flags)
{
	int ret;

	/*
	 * Check the pipe occupancy without the inode lock first. This function
	 * is speculative anyways, so missing one is ok.
	 */
	if (!pipe_empty(pipe->head, pipe->tail))
		return 0;

	ret = 0;
	pipe_lock(pipe);

	while (pipe_empty(pipe->head, pipe->tail)) {
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		if (!pipe->writers)
			break;
		if (flags & SPLICE_F_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		pipe_wait_readable(pipe);
	}

	pipe_unlock(pipe);
	return ret;
}

/*
 * Make sure there's writeable room. Wait for room if we can, otherwise
 * return an appropriate error.
 */
static int opipe_prep(struct pipe_inode_info *pipe, unsigned int flags)
{
	int ret;

	/*
	 * Check pipe occupancy without the inode lock first. This function
	 * is speculative anyways, so missing one is ok.
	 */
	if (!pipe_full(pipe->head, pipe->tail, pipe->max_usage))
		return 0;

	ret = 0;
	pipe_lock(pipe);

	while (pipe_full(pipe->head, pipe->tail, pipe->max_usage)) {
		if (!pipe->readers) {
			send_sig(SIGPIPE, current, 0);
			ret = -EPIPE;
			break;
		}
		if (flags & SPLICE_F_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		pipe_wait_writable(pipe);
	}

	pipe_unlock(pipe);
	return ret;
}

/*
 * Splice contents of ipipe to opipe.
 */
static int splice_pipe_to_pipe(struct pipe_inode_info *ipipe,
			       struct pipe_inode_info *opipe,
			       size_t len, unsigned int flags)
{
	struct pipe_buffer *ibuf, *obuf;
	unsigned int i_head, o_head;
	unsigned int i_tail, o_tail;
	unsigned int i_mask, o_mask;
	int ret = 0;
	bool input_wakeup = false;


retry:
	ret = ipipe_prep(ipipe, flags);
	if (ret)
		return ret;

	ret = opipe_prep(opipe, flags);
	if (ret)
		return ret;

	/*
	 * Potential ABBA deadlock, work around it by ordering lock
	 * grabbing by pipe info address. Otherwise two different processes
	 * could deadlock (one doing tee from A -> B, the other from B -> A).
	 */
	pipe_double_lock(ipipe, opipe);

	i_tail = ipipe->tail;
	i_mask = ipipe->ring_size - 1;
	o_head = opipe->head;
	o_mask = opipe->ring_size - 1;

	do {
		size_t o_len;

		if (!opipe->readers) {
			send_sig(SIGPIPE, current, 0);
			if (!ret)
				ret = -EPIPE;
			break;
		}

		i_head = ipipe->head;
		o_tail = opipe->tail;

		if (pipe_empty(i_head, i_tail) && !ipipe->writers)
			break;

		/*
		 * Cannot make any progress, because either the input
		 * pipe is empty or the output pipe is full.
		 */
		if (pipe_empty(i_head, i_tail) ||
		    pipe_full(o_head, o_tail, opipe->max_usage)) {
			/* Already processed some buffers, break */
			if (ret)
				break;

			if (flags & SPLICE_F_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}

			/*
			 * We raced with another reader/writer and haven't
			 * managed to process any buffers.  A zero return
			 * value means EOF, so retry instead.
			 */
			pipe_unlock(ipipe);
			pipe_unlock(opipe);
			goto retry;
		}

		ibuf = &ipipe->bufs[i_tail & i_mask];
		obuf = &opipe->bufs[o_head & o_mask];

		if (len >= ibuf->len) {
			/*
			 * Simply move the whole buffer from ipipe to opipe
			 */
			*obuf = *ibuf;
			ibuf->ops = NULL;
			i_tail++;
			ipipe->tail = i_tail;
			input_wakeup = true;
			o_len = obuf->len;
			o_head++;
			opipe->head = o_head;
		} else {
			/*
			 * Get a reference to this pipe buffer,
			 * so we can copy the contents over.
			 */
			if (!pipe_buf_get(ipipe, ibuf)) {
				if (ret == 0)
					ret = -EFAULT;
				break;
			}
			*obuf = *ibuf;

			/*
			 * Don't inherit the gift and merge flags, we need to
			 * prevent multiple steals of this page.
			 */
			obuf->flags &= ~PIPE_BUF_FLAG_GIFT;
			obuf->flags &= ~PIPE_BUF_FLAG_CAN_MERGE;

			obuf->len = len;
			ibuf->offset += len;
			ibuf->len -= len;
			o_len = len;
			o_head++;
			opipe->head = o_head;
		}
		ret += o_len;
		len -= o_len;
	} while (len);

	pipe_unlock(ipipe);
	pipe_unlock(opipe);

	/*
	 * If we put data in the output pipe, wakeup any potential readers.
	 */
	if (ret > 0)
		wakeup_pipe_readers(opipe);

	if (input_wakeup)
		wakeup_pipe_writers(ipipe);

	return ret;
}

/*
 * Link contents of ipipe to opipe.
 */
static int link_pipe(struct pipe_inode_info *ipipe,
		     struct pipe_inode_info *opipe,
		     size_t len, unsigned int flags)
{
	struct pipe_buffer *ibuf, *obuf;
	unsigned int i_head, o_head;
	unsigned int i_tail, o_tail;
	unsigned int i_mask, o_mask;
	int ret = 0;

	/*
	 * Potential ABBA deadlock, work around it by ordering lock
	 * grabbing by pipe info address. Otherwise two different processes
	 * could deadlock (one doing tee from A -> B, the other from B -> A).
	 */
	pipe_double_lock(ipipe, opipe);

	i_tail = ipipe->tail;
	i_mask = ipipe->ring_size - 1;
	o_head = opipe->head;
	o_mask = opipe->ring_size - 1;

	do {
		if (!opipe->readers) {
			send_sig(SIGPIPE, current, 0);
			if (!ret)
				ret = -EPIPE;
			break;
		}

		i_head = ipipe->head;
		o_tail = opipe->tail;

		/*
		 * If we have iterated all input buffers or run out of
		 * output room, break.
		 */
		if (pipe_empty(i_head, i_tail) ||
		    pipe_full(o_head, o_tail, opipe->max_usage))
			break;

		ibuf = &ipipe->bufs[i_tail & i_mask];
		obuf = &opipe->bufs[o_head & o_mask];

		/*
		 * Get a reference to this pipe buffer,
		 * so we can copy the contents over.
		 */
		if (!pipe_buf_get(ipipe, ibuf)) {
			if (ret == 0)
				ret = -EFAULT;
			break;
		}

		*obuf = *ibuf;

		/*
		 * Don't inherit the gift and merge flag, we need to prevent
		 * multiple steals of this page.
		 */
		obuf->flags &= ~PIPE_BUF_FLAG_GIFT;
		obuf->flags &= ~PIPE_BUF_FLAG_CAN_MERGE;

		if (obuf->len > len)
			obuf->len = len;
		ret += obuf->len;
		len -= obuf->len;

		o_head++;
		opipe->head = o_head;
		i_tail++;
	} while (len);

	pipe_unlock(ipipe);
	pipe_unlock(opipe);

	/*
	 * If we put data in the output pipe, wakeup any potential readers.
	 */
	if (ret > 0)
		wakeup_pipe_readers(opipe);

	return ret;
}

/*
 * This is a tee(1) implementation that works on pipes. It doesn't copy
 * any data, it simply references the 'in' pages on the 'out' pipe.
 * The 'flags' used are the SPLICE_F_* variants, currently the only
 * applicable one is SPLICE_F_NONBLOCK.
 */
long do_tee(struct file *in, struct file *out, size_t len, unsigned int flags)
{
	struct pipe_inode_info *ipipe = get_pipe_info(in, true);
	struct pipe_inode_info *opipe = get_pipe_info(out, true);
	int ret = -EINVAL;

	if (unlikely(!(in->f_mode & FMODE_READ) ||
		     !(out->f_mode & FMODE_WRITE)))
		return -EBADF;

	/*
	 * Duplicate the contents of ipipe to opipe without actually
	 * copying the data.
	 */
	if (ipipe && opipe && ipipe != opipe) {
		if ((in->f_flags | out->f_flags) & O_NONBLOCK)
			flags |= SPLICE_F_NONBLOCK;

		/*
		 * Keep going, unless we encounter an error. The ipipe/opipe
		 * ordering doesn't really matter.
		 */
		ret = ipipe_prep(ipipe, flags);
		if (!ret) {
			ret = opipe_prep(opipe, flags);
			if (!ret)
				ret = link_pipe(ipipe, opipe, len, flags);
		}
	}

	return ret;
}

SYSCALL_DEFINE4(tee, int, fdin, int, fdout, size_t, len, unsigned int, flags)
{
	struct fd in, out;
	int error;

	if (unlikely(flags & ~SPLICE_F_ALL))
		return -EINVAL;

	if (unlikely(!len))
		return 0;

	error = -EBADF;
	in = fdget(fdin);
	if (in.file) {
		out = fdget(fdout);
		if (out.file) {
			error = do_tee(in.file, out.file, len, flags);
			fdput(out);
		}
 		fdput(in);
 	}

	return error;
}
