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
 * Copyright (C) 2005-2006 Jens Axboe <axboe@suse.de>
 * Copyright (C) 2005-2006 Linus Torvalds <torvalds@osdl.org>
 * Copyright (C) 2006 Ingo Molnar <mingo@elte.hu>
 *
 */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/pipe_fs_i.h>
#include <linux/mm_inline.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/syscalls.h>

/*
 * Passed to the actors
 */
struct splice_desc {
	unsigned int len, total_len;	/* current and remaining length */
	unsigned int flags;		/* splice flags */
	struct file *file;		/* file to read/write */
	loff_t pos;			/* file position */
};

/*
 * Attempt to steal a page from a pipe buffer. This should perhaps go into
 * a vm helper function, it's already simplified quite a bit by the
 * addition of remove_mapping(). If success is returned, the caller may
 * attempt to reuse this page for another destination.
 */
static int page_cache_pipe_buf_steal(struct pipe_inode_info *info,
				     struct pipe_buffer *buf)
{
	struct page *page = buf->page;
	struct address_space *mapping = page_mapping(page);

	WARN_ON(!PageLocked(page));
	WARN_ON(!PageUptodate(page));

	/*
	 * At least for ext2 with nobh option, we need to wait on writeback
	 * completing on this page, since we'll remove it from the pagecache.
	 * Otherwise truncate wont wait on the page, allowing the disk
	 * blocks to be reused by someone else before we actually wrote our
	 * data to them. fs corruption ensues.
	 */
	wait_on_page_writeback(page);

	if (PagePrivate(page))
		try_to_release_page(page, mapping_gfp_mask(mapping));

	if (!remove_mapping(mapping, page))
		return 1;

	buf->flags |= PIPE_BUF_FLAG_STOLEN | PIPE_BUF_FLAG_LRU;
	return 0;
}

static void page_cache_pipe_buf_release(struct pipe_inode_info *info,
					struct pipe_buffer *buf)
{
	page_cache_release(buf->page);
	buf->page = NULL;
	buf->flags &= ~(PIPE_BUF_FLAG_STOLEN | PIPE_BUF_FLAG_LRU);
}

static void *page_cache_pipe_buf_map(struct file *file,
				     struct pipe_inode_info *info,
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
		 * Page is ok afterall, fall through to mapping.
		 */
		unlock_page(page);
	}

	return kmap(page);
error:
	unlock_page(page);
	return ERR_PTR(err);
}

static void page_cache_pipe_buf_unmap(struct pipe_inode_info *info,
				      struct pipe_buffer *buf)
{
	kunmap(buf->page);
}

static struct pipe_buf_operations page_cache_pipe_buf_ops = {
	.can_merge = 0,
	.map = page_cache_pipe_buf_map,
	.unmap = page_cache_pipe_buf_unmap,
	.release = page_cache_pipe_buf_release,
	.steal = page_cache_pipe_buf_steal,
};

/*
 * Pipe output worker. This sets up our pipe format with the page cache
 * pipe buffer operations. Otherwise very similar to the regular pipe_writev().
 */
static ssize_t move_to_pipe(struct pipe_inode_info *pipe, struct page **pages,
			    int nr_pages, unsigned long offset,
			    unsigned long len, unsigned int flags)
{
	int ret, do_wakeup, i;

	ret = 0;
	do_wakeup = 0;
	i = 0;

	if (pipe->inode)
		mutex_lock(&pipe->inode->i_mutex);

	for (;;) {
		if (!pipe->readers) {
			send_sig(SIGPIPE, current, 0);
			if (!ret)
				ret = -EPIPE;
			break;
		}

		if (pipe->nrbufs < PIPE_BUFFERS) {
			int newbuf = (pipe->curbuf + pipe->nrbufs) & (PIPE_BUFFERS - 1);
			struct pipe_buffer *buf = pipe->bufs + newbuf;
			struct page *page = pages[i++];
			unsigned long this_len;

			this_len = PAGE_CACHE_SIZE - offset;
			if (this_len > len)
				this_len = len;

			buf->page = page;
			buf->offset = offset;
			buf->len = this_len;
			buf->ops = &page_cache_pipe_buf_ops;
			pipe->nrbufs++;
			if (pipe->inode)
				do_wakeup = 1;

			ret += this_len;
			len -= this_len;
			offset = 0;
			if (!--nr_pages)
				break;
			if (!len)
				break;
			if (pipe->nrbufs < PIPE_BUFFERS)
				continue;

			break;
		}

		if (flags & SPLICE_F_NONBLOCK) {
			if (!ret)
				ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			if (!ret)
				ret = -ERESTARTSYS;
			break;
		}

		if (do_wakeup) {
			smp_mb();
			if (waitqueue_active(&pipe->wait))
				wake_up_interruptible_sync(&pipe->wait);
			kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
			do_wakeup = 0;
		}

		pipe->waiting_writers++;
		pipe_wait(pipe);
		pipe->waiting_writers--;
	}

	if (pipe->inode)
		mutex_unlock(&pipe->inode->i_mutex);

	if (do_wakeup) {
		smp_mb();
		if (waitqueue_active(&pipe->wait))
			wake_up_interruptible(&pipe->wait);
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	}

	while (i < nr_pages)
		page_cache_release(pages[i++]);

	return ret;
}

static int
__generic_file_splice_read(struct file *in, struct pipe_inode_info *pipe,
			   size_t len, unsigned int flags)
{
	struct address_space *mapping = in->f_mapping;
	unsigned int offset, nr_pages;
	struct page *pages[PIPE_BUFFERS];
	struct page *page;
	pgoff_t index;
	int i, error;

	index = in->f_pos >> PAGE_CACHE_SHIFT;
	offset = in->f_pos & ~PAGE_CACHE_MASK;
	nr_pages = (len + offset + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	if (nr_pages > PIPE_BUFFERS)
		nr_pages = PIPE_BUFFERS;

	/*
	 * Initiate read-ahead on this page range. however, don't call into
	 * read-ahead if this is a non-zero offset (we are likely doing small
	 * chunk splice and the page is already there) for a single page.
	 */
	if (!offset || nr_pages > 1)
		do_page_cache_readahead(mapping, in, index, nr_pages);

	/*
	 * Now fill in the holes:
	 */
	error = 0;
	for (i = 0; i < nr_pages; i++, index++) {
find_page:
		/*
		 * lookup the page for this index
		 */
		page = find_get_page(mapping, index);
		if (!page) {
			/*
			 * If in nonblock mode then dont block on
			 * readpage (we've kicked readahead so there
			 * will be asynchronous progress):
			 */
			if (flags & SPLICE_F_NONBLOCK)
				break;

			/*
			 * page didn't exist, allocate one
			 */
			page = page_cache_alloc_cold(mapping);
			if (!page)
				break;

			error = add_to_page_cache_lru(page, mapping, index,
						mapping_gfp_mask(mapping));
			if (unlikely(error)) {
				page_cache_release(page);
				break;
			}

			goto readpage;
		}

		/*
		 * If the page isn't uptodate, we may need to start io on it
		 */
		if (!PageUptodate(page)) {
			lock_page(page);

			/*
			 * page was truncated, stop here. if this isn't the
			 * first page, we'll just complete what we already
			 * added
			 */
			if (!page->mapping) {
				unlock_page(page);
				page_cache_release(page);
				break;
			}
			/*
			 * page was already under io and is now done, great
			 */
			if (PageUptodate(page)) {
				unlock_page(page);
				goto fill_it;
			}

readpage:
			/*
			 * need to read in the page
			 */
			error = mapping->a_ops->readpage(in, page);

			if (unlikely(error)) {
				page_cache_release(page);
				if (error == AOP_TRUNCATED_PAGE)
					goto find_page;
				break;
			}
		}
fill_it:
		pages[i] = page;
	}

	if (i)
		return move_to_pipe(pipe, pages, i, offset, len, flags);

	return error;
}

/**
 * generic_file_splice_read - splice data from file to a pipe
 * @in:		file to splice from
 * @pipe:	pipe to splice to
 * @len:	number of bytes to splice
 * @flags:	splice modifier flags
 *
 * Will read pages from given file and fill them into a pipe.
 */
ssize_t generic_file_splice_read(struct file *in, struct pipe_inode_info *pipe,
				 size_t len, unsigned int flags)
{
	ssize_t spliced;
	int ret;

	ret = 0;
	spliced = 0;

	while (len) {
		ret = __generic_file_splice_read(in, pipe, len, flags);

		if (ret <= 0)
			break;

		in->f_pos += ret;
		len -= ret;
		spliced += ret;

		if (!(flags & SPLICE_F_NONBLOCK))
			continue;
		ret = -EAGAIN;
		break;
	}

	if (spliced)
		return spliced;

	return ret;
}

EXPORT_SYMBOL(generic_file_splice_read);

/*
 * Send 'sd->len' bytes to socket from 'sd->file' at position 'sd->pos'
 * using sendpage().
 */
static int pipe_to_sendpage(struct pipe_inode_info *info,
			    struct pipe_buffer *buf, struct splice_desc *sd)
{
	struct file *file = sd->file;
	loff_t pos = sd->pos;
	unsigned int offset;
	ssize_t ret;
	void *ptr;
	int more;

	/*
	 * Sub-optimal, but we are limited by the pipe ->map. We don't
	 * need a kmap'ed buffer here, we just want to make sure we
	 * have the page pinned if the pipe page originates from the
	 * page cache.
	 */
	ptr = buf->ops->map(file, info, buf);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	offset = pos & ~PAGE_CACHE_MASK;
	more = (sd->flags & SPLICE_F_MORE) || sd->len < sd->total_len;

	ret = file->f_op->sendpage(file, buf->page, offset, sd->len, &pos,more);

	buf->ops->unmap(info, buf);
	if (ret == sd->len)
		return 0;

	return -EIO;
}

/*
 * This is a little more tricky than the file -> pipe splicing. There are
 * basically three cases:
 *
 *	- Destination page already exists in the address space and there
 *	  are users of it. For that case we have no other option that
 *	  copying the data. Tough luck.
 *	- Destination page already exists in the address space, but there
 *	  are no users of it. Make sure it's uptodate, then drop it. Fall
 *	  through to last case.
 *	- Destination page does not exist, we can add the pipe page to
 *	  the page cache and avoid the copy.
 *
 * If asked to move pages to the output file (SPLICE_F_MOVE is set in
 * sd->flags), we attempt to migrate pages from the pipe to the output
 * file address space page cache. This is possible if no one else has
 * the pipe page referenced outside of the pipe and page cache. If
 * SPLICE_F_MOVE isn't set, or we cannot move the page, we simply create
 * a new page in the output file page cache and fill/dirty that.
 */
static int pipe_to_file(struct pipe_inode_info *info, struct pipe_buffer *buf,
			struct splice_desc *sd)
{
	struct file *file = sd->file;
	struct address_space *mapping = file->f_mapping;
	gfp_t gfp_mask = mapping_gfp_mask(mapping);
	unsigned int offset;
	struct page *page;
	pgoff_t index;
	char *src;
	int ret;

	/*
	 * make sure the data in this buffer is uptodate
	 */
	src = buf->ops->map(file, info, buf);
	if (IS_ERR(src))
		return PTR_ERR(src);

	index = sd->pos >> PAGE_CACHE_SHIFT;
	offset = sd->pos & ~PAGE_CACHE_MASK;

	/*
	 * Reuse buf page, if SPLICE_F_MOVE is set.
	 */
	if (sd->flags & SPLICE_F_MOVE) {
		/*
		 * If steal succeeds, buf->page is now pruned from the vm
		 * side (LRU and page cache) and we can reuse it.
		 */
		if (buf->ops->steal(info, buf))
			goto find_page;

		/*
		 * this will also set the page locked
		 */
		page = buf->page;
		if (add_to_page_cache(page, mapping, index, gfp_mask))
			goto find_page;

		if (!(buf->flags & PIPE_BUF_FLAG_LRU))
			lru_cache_add(page);
	} else {
find_page:
		ret = -ENOMEM;
		page = find_or_create_page(mapping, index, gfp_mask);
		if (!page)
			goto out_nomem;

		/*
		 * If the page is uptodate, it is also locked. If it isn't
		 * uptodate, we can mark it uptodate if we are filling the
		 * full page. Otherwise we need to read it in first...
		 */
		if (!PageUptodate(page)) {
			if (sd->len < PAGE_CACHE_SIZE) {
				ret = mapping->a_ops->readpage(file, page);
				if (unlikely(ret))
					goto out;

				lock_page(page);

				if (!PageUptodate(page)) {
					/*
					 * Page got invalidated, repeat.
					 */
					if (!page->mapping) {
						unlock_page(page);
						page_cache_release(page);
						goto find_page;
					}
					ret = -EIO;
					goto out;
				}
			} else {
				WARN_ON(!PageLocked(page));
				SetPageUptodate(page);
			}
		}
	}

	ret = mapping->a_ops->prepare_write(file, page, 0, sd->len);
	if (ret == AOP_TRUNCATED_PAGE) {
		page_cache_release(page);
		goto find_page;
	} else if (ret)
		goto out;

	if (!(buf->flags & PIPE_BUF_FLAG_STOLEN)) {
		char *dst = kmap_atomic(page, KM_USER0);

		memcpy(dst + offset, src + buf->offset, sd->len);
		flush_dcache_page(page);
		kunmap_atomic(dst, KM_USER0);
	}

	ret = mapping->a_ops->commit_write(file, page, 0, sd->len);
	if (ret == AOP_TRUNCATED_PAGE) {
		page_cache_release(page);
		goto find_page;
	} else if (ret)
		goto out;

	mark_page_accessed(page);
	balance_dirty_pages_ratelimited(mapping);
out:
	if (!(buf->flags & PIPE_BUF_FLAG_STOLEN)) {
		page_cache_release(page);
		unlock_page(page);
	}
out_nomem:
	buf->ops->unmap(info, buf);
	return ret;
}

typedef int (splice_actor)(struct pipe_inode_info *, struct pipe_buffer *,
			   struct splice_desc *);

/*
 * Pipe input worker. Most of this logic works like a regular pipe, the
 * key here is the 'actor' worker passed in that actually moves the data
 * to the wanted destination. See pipe_to_file/pipe_to_sendpage above.
 */
static ssize_t move_from_pipe(struct pipe_inode_info *pipe, struct file *out,
			      size_t len, unsigned int flags,
			      splice_actor *actor)
{
	int ret, do_wakeup, err;
	struct splice_desc sd;

	ret = 0;
	do_wakeup = 0;

	sd.total_len = len;
	sd.flags = flags;
	sd.file = out;
	sd.pos = out->f_pos;

	if (pipe->inode)
		mutex_lock(&pipe->inode->i_mutex);

	for (;;) {
		if (pipe->nrbufs) {
			struct pipe_buffer *buf = pipe->bufs + pipe->curbuf;
			struct pipe_buf_operations *ops = buf->ops;

			sd.len = buf->len;
			if (sd.len > sd.total_len)
				sd.len = sd.total_len;

			err = actor(pipe, buf, &sd);
			if (err) {
				if (!ret && err != -ENODATA)
					ret = err;

				break;
			}

			ret += sd.len;
			buf->offset += sd.len;
			buf->len -= sd.len;

			if (!buf->len) {
				buf->ops = NULL;
				ops->release(pipe, buf);
				pipe->curbuf = (pipe->curbuf + 1) & (PIPE_BUFFERS - 1);
				pipe->nrbufs--;
				if (pipe->inode)
					do_wakeup = 1;
			}

			sd.pos += sd.len;
			sd.total_len -= sd.len;
			if (!sd.total_len)
				break;
		}

		if (pipe->nrbufs)
			continue;
		if (!pipe->writers)
			break;
		if (!pipe->waiting_writers) {
			if (ret)
				break;
		}

		if (flags & SPLICE_F_NONBLOCK) {
			if (!ret)
				ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			if (!ret)
				ret = -ERESTARTSYS;
			break;
		}

		if (do_wakeup) {
			smp_mb();
			if (waitqueue_active(&pipe->wait))
				wake_up_interruptible_sync(&pipe->wait);
			kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
			do_wakeup = 0;
		}

		pipe_wait(pipe);
	}

	if (pipe->inode)
		mutex_unlock(&pipe->inode->i_mutex);

	if (do_wakeup) {
		smp_mb();
		if (waitqueue_active(&pipe->wait))
			wake_up_interruptible(&pipe->wait);
		kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
	}

	out->f_pos = sd.pos;
	return ret;

}

/**
 * generic_file_splice_write - splice data from a pipe to a file
 * @pipe:	pipe info
 * @out:	file to write to
 * @len:	number of bytes to splice
 * @flags:	splice modifier flags
 *
 * Will either move or copy pages (determined by @flags options) from
 * the given pipe inode to the given file.
 *
 */
ssize_t
generic_file_splice_write(struct pipe_inode_info *pipe, struct file *out,
			  size_t len, unsigned int flags)
{
	struct address_space *mapping = out->f_mapping;
	ssize_t ret;

	ret = move_from_pipe(pipe, out, len, flags, pipe_to_file);

	/*
	 * If file or inode is SYNC and we actually wrote some data, sync it.
	 */
	if (unlikely((out->f_flags & O_SYNC) || IS_SYNC(mapping->host))
	    && ret > 0) {
		struct inode *inode = mapping->host;
		int err;

		mutex_lock(&inode->i_mutex);
		err = generic_osync_inode(mapping->host, mapping,
					  OSYNC_METADATA|OSYNC_DATA);
		mutex_unlock(&inode->i_mutex);

		if (err)
			ret = err;
	}

	return ret;
}

EXPORT_SYMBOL(generic_file_splice_write);

/**
 * generic_splice_sendpage - splice data from a pipe to a socket
 * @inode:	pipe inode
 * @out:	socket to write to
 * @len:	number of bytes to splice
 * @flags:	splice modifier flags
 *
 * Will send @len bytes from the pipe to a network socket. No data copying
 * is involved.
 *
 */
ssize_t generic_splice_sendpage(struct pipe_inode_info *pipe, struct file *out,
				size_t len, unsigned int flags)
{
	return move_from_pipe(pipe, out, len, flags, pipe_to_sendpage);
}

EXPORT_SYMBOL(generic_splice_sendpage);

/*
 * Attempt to initiate a splice from pipe to file.
 */
static long do_splice_from(struct pipe_inode_info *pipe, struct file *out,
			   size_t len, unsigned int flags)
{
	loff_t pos;
	int ret;

	if (unlikely(!out->f_op || !out->f_op->splice_write))
		return -EINVAL;

	if (unlikely(!(out->f_mode & FMODE_WRITE)))
		return -EBADF;

	pos = out->f_pos;

	ret = rw_verify_area(WRITE, out, &pos, len);
	if (unlikely(ret < 0))
		return ret;

	return out->f_op->splice_write(pipe, out, len, flags);
}

/*
 * Attempt to initiate a splice from a file to a pipe.
 */
static long do_splice_to(struct file *in, struct pipe_inode_info *pipe,
			 size_t len, unsigned int flags)
{
	loff_t pos, isize, left;
	int ret;

	if (unlikely(!in->f_op || !in->f_op->splice_read))
		return -EINVAL;

	if (unlikely(!(in->f_mode & FMODE_READ)))
		return -EBADF;

	pos = in->f_pos;

	ret = rw_verify_area(READ, in, &pos, len);
	if (unlikely(ret < 0))
		return ret;

	isize = i_size_read(in->f_mapping->host);
	if (unlikely(in->f_pos >= isize))
		return 0;
	
	left = isize - in->f_pos;
	if (unlikely(left < len))
		len = left;

	return in->f_op->splice_read(in, pipe, len, flags);
}

long do_splice_direct(struct file *in, struct file *out, size_t len,
		      unsigned int flags)
{
	struct pipe_inode_info *pipe;
	long ret, bytes;
	umode_t i_mode;
	int i;

	/*
	 * We require the input being a regular file, as we don't want to
	 * randomly drop data for eg socket -> socket splicing. Use the
	 * piped splicing for that!
	 */
	i_mode = in->f_dentry->d_inode->i_mode;
	if (unlikely(!S_ISREG(i_mode) && !S_ISBLK(i_mode)))
		return -EINVAL;

	/*
	 * neither in nor out is a pipe, setup an internal pipe attached to
	 * 'out' and transfer the wanted data from 'in' to 'out' through that
	 */
	pipe = current->splice_pipe;
	if (unlikely(!pipe)) {
		pipe = alloc_pipe_info(NULL);
		if (!pipe)
			return -ENOMEM;

		/*
		 * We don't have an immediate reader, but we'll read the stuff
		 * out of the pipe right after the move_to_pipe(). So set
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

	while (len) {
		size_t read_len, max_read_len;

		/*
		 * Do at most PIPE_BUFFERS pages worth of transfer:
		 */
		max_read_len = min(len, (size_t)(PIPE_BUFFERS*PAGE_SIZE));

		ret = do_splice_to(in, pipe, max_read_len, flags);
		if (unlikely(ret < 0))
			goto out_release;

		read_len = ret;

		/*
		 * NOTE: nonblocking mode only applies to the input. We
		 * must not do the output in nonblocking mode as then we
		 * could get stuck data in the internal pipe:
		 */
		ret = do_splice_from(pipe, out, read_len,
				     flags & ~SPLICE_F_NONBLOCK);
		if (unlikely(ret < 0))
			goto out_release;

		bytes += ret;
		len -= ret;

		/*
		 * In nonblocking mode, if we got back a short read then
		 * that was due to either an IO error or due to the
		 * pagecache entry not being there. In the IO error case
		 * the _next_ splice attempt will produce a clean IO error
		 * return value (not a short read), so in both cases it's
		 * correct to break out of the loop here:
		 */
		if ((flags & SPLICE_F_NONBLOCK) && (read_len < max_read_len))
			break;
	}

	pipe->nrbufs = pipe->curbuf = 0;

	return bytes;

out_release:
	/*
	 * If we did an incomplete transfer we must release
	 * the pipe buffers in question:
	 */
	for (i = 0; i < PIPE_BUFFERS; i++) {
		struct pipe_buffer *buf = pipe->bufs + i;

		if (buf->ops) {
			buf->ops->release(pipe, buf);
			buf->ops = NULL;
		}
	}
	pipe->nrbufs = pipe->curbuf = 0;

	/*
	 * If we transferred some data, return the number of bytes:
	 */
	if (bytes > 0)
		return bytes;

	return ret;
}

EXPORT_SYMBOL(do_splice_direct);

/*
 * Determine where to splice to/from.
 */
static long do_splice(struct file *in, loff_t __user *off_in,
		      struct file *out, loff_t __user *off_out,
		      size_t len, unsigned int flags)
{
	struct pipe_inode_info *pipe;

	pipe = in->f_dentry->d_inode->i_pipe;
	if (pipe) {
		if (off_in)
			return -ESPIPE;
		if (off_out) {
			if (out->f_op->llseek == no_llseek)
				return -EINVAL;
			if (copy_from_user(&out->f_pos, off_out,
					   sizeof(loff_t)))
				return -EFAULT;
		}

		return do_splice_from(pipe, out, len, flags);
	}

	pipe = out->f_dentry->d_inode->i_pipe;
	if (pipe) {
		if (off_out)
			return -ESPIPE;
		if (off_in) {
			if (in->f_op->llseek == no_llseek)
				return -EINVAL;
			if (copy_from_user(&in->f_pos, off_in, sizeof(loff_t)))
				return -EFAULT;
		}

		return do_splice_to(in, pipe, len, flags);
	}

	return -EINVAL;
}

asmlinkage long sys_splice(int fd_in, loff_t __user *off_in,
			   int fd_out, loff_t __user *off_out,
			   size_t len, unsigned int flags)
{
	long error;
	struct file *in, *out;
	int fput_in, fput_out;

	if (unlikely(!len))
		return 0;

	error = -EBADF;
	in = fget_light(fd_in, &fput_in);
	if (in) {
		if (in->f_mode & FMODE_READ) {
			out = fget_light(fd_out, &fput_out);
			if (out) {
				if (out->f_mode & FMODE_WRITE)
					error = do_splice(in, off_in,
							  out, off_out,
							  len, flags);
				fput_light(out, fput_out);
			}
		}

		fput_light(in, fput_in);
	}

	return error;
}
