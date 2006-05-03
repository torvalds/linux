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
#include <linux/uio.h>

struct partial_page {
	unsigned int offset;
	unsigned int len;
};

/*
 * Passed to splice_to_pipe
 */
struct splice_pipe_desc {
	struct page **pages;		/* page map */
	struct partial_page *partial;	/* pages[] may not be contig */
	int nr_pages;			/* number of pages in map */
	unsigned int flags;		/* splice flags */
	struct pipe_buf_operations *ops;/* ops associated with output pipe */
};

/*
 * Attempt to steal a page from a pipe buffer. This should perhaps go into
 * a vm helper function, it's already simplified quite a bit by the
 * addition of remove_mapping(). If success is returned, the caller may
 * attempt to reuse this page for another destination.
 */
static int page_cache_pipe_buf_steal(struct pipe_inode_info *pipe,
				     struct pipe_buffer *buf)
{
	struct page *page = buf->page;
	struct address_space *mapping = page_mapping(page);

	lock_page(page);

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

	if (!remove_mapping(mapping, page)) {
		unlock_page(page);
		return 1;
	}

	buf->flags |= PIPE_BUF_FLAG_LRU;
	return 0;
}

static void page_cache_pipe_buf_release(struct pipe_inode_info *pipe,
					struct pipe_buffer *buf)
{
	page_cache_release(buf->page);
	buf->flags &= ~PIPE_BUF_FLAG_LRU;
}

static int page_cache_pipe_buf_pin(struct pipe_inode_info *pipe,
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

static struct pipe_buf_operations page_cache_pipe_buf_ops = {
	.can_merge = 0,
	.map = generic_pipe_buf_map,
	.unmap = generic_pipe_buf_unmap,
	.pin = page_cache_pipe_buf_pin,
	.release = page_cache_pipe_buf_release,
	.steal = page_cache_pipe_buf_steal,
	.get = generic_pipe_buf_get,
};

static int user_page_pipe_buf_steal(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	if (!(buf->flags & PIPE_BUF_FLAG_GIFT))
		return 1;

	buf->flags |= PIPE_BUF_FLAG_LRU;
	return generic_pipe_buf_steal(pipe, buf);
}

static struct pipe_buf_operations user_page_pipe_buf_ops = {
	.can_merge = 0,
	.map = generic_pipe_buf_map,
	.unmap = generic_pipe_buf_unmap,
	.pin = generic_pipe_buf_pin,
	.release = page_cache_pipe_buf_release,
	.steal = user_page_pipe_buf_steal,
	.get = generic_pipe_buf_get,
};

/*
 * Pipe output worker. This sets up our pipe format with the page cache
 * pipe buffer operations. Otherwise very similar to the regular pipe_writev().
 */
static ssize_t splice_to_pipe(struct pipe_inode_info *pipe,
			      struct splice_pipe_desc *spd)
{
	int ret, do_wakeup, page_nr;

	ret = 0;
	do_wakeup = 0;
	page_nr = 0;

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

			buf->page = spd->pages[page_nr];
			buf->offset = spd->partial[page_nr].offset;
			buf->len = spd->partial[page_nr].len;
			buf->ops = spd->ops;
			if (spd->flags & SPLICE_F_GIFT)
				buf->flags |= PIPE_BUF_FLAG_GIFT;

			pipe->nrbufs++;
			page_nr++;
			ret += buf->len;

			if (pipe->inode)
				do_wakeup = 1;

			if (!--spd->nr_pages)
				break;
			if (pipe->nrbufs < PIPE_BUFFERS)
				continue;

			break;
		}

		if (spd->flags & SPLICE_F_NONBLOCK) {
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

	while (page_nr < spd->nr_pages)
		page_cache_release(spd->pages[page_nr++]);

	return ret;
}

static int
__generic_file_splice_read(struct file *in, loff_t *ppos,
			   struct pipe_inode_info *pipe, size_t len,
			   unsigned int flags)
{
	struct address_space *mapping = in->f_mapping;
	unsigned int loff, nr_pages;
	struct page *pages[PIPE_BUFFERS];
	struct partial_page partial[PIPE_BUFFERS];
	struct page *page;
	pgoff_t index, end_index;
	loff_t isize;
	size_t total_len;
	int error, page_nr;
	struct splice_pipe_desc spd = {
		.pages = pages,
		.partial = partial,
		.flags = flags,
		.ops = &page_cache_pipe_buf_ops,
	};

	index = *ppos >> PAGE_CACHE_SHIFT;
	loff = *ppos & ~PAGE_CACHE_MASK;
	nr_pages = (len + loff + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	if (nr_pages > PIPE_BUFFERS)
		nr_pages = PIPE_BUFFERS;

	/*
	 * Initiate read-ahead on this page range. however, don't call into
	 * read-ahead if this is a non-zero offset (we are likely doing small
	 * chunk splice and the page is already there) for a single page.
	 */
	if (!loff || nr_pages > 1)
		page_cache_readahead(mapping, &in->f_ra, in, index, nr_pages);

	/*
	 * Now fill in the holes:
	 */
	error = 0;
	total_len = 0;

	/*
	 * Lookup the (hopefully) full range of pages we need.
	 */
	spd.nr_pages = find_get_pages_contig(mapping, index, nr_pages, pages);

	/*
	 * If find_get_pages_contig() returned fewer pages than we needed,
	 * allocate the rest.
	 */
	index += spd.nr_pages;
	while (spd.nr_pages < nr_pages) {
		/*
		 * Page could be there, find_get_pages_contig() breaks on
		 * the first hole.
		 */
		page = find_get_page(mapping, index);
		if (!page) {
			/*
			 * Make sure the read-ahead engine is notified
			 * about this failure.
			 */
			handle_ra_miss(mapping, &in->f_ra, index);

			/*
			 * page didn't exist, allocate one.
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
			/*
			 * add_to_page_cache() locks the page, unlock it
			 * to avoid convoluting the logic below even more.
			 */
			unlock_page(page);
		}

		pages[spd.nr_pages++] = page;
		index++;
	}

	/*
	 * Now loop over the map and see if we need to start IO on any
	 * pages, fill in the partial map, etc.
	 */
	index = *ppos >> PAGE_CACHE_SHIFT;
	nr_pages = spd.nr_pages;
	spd.nr_pages = 0;
	for (page_nr = 0; page_nr < nr_pages; page_nr++) {
		unsigned int this_len;

		if (!len)
			break;

		/*
		 * this_len is the max we'll use from this page
		 */
		this_len = min_t(unsigned long, len, PAGE_CACHE_SIZE - loff);
		page = pages[page_nr];

		/*
		 * If the page isn't uptodate, we may need to start io on it
		 */
		if (!PageUptodate(page)) {
			/*
			 * If in nonblock mode then dont block on waiting
			 * for an in-flight io page
			 */
			if (flags & SPLICE_F_NONBLOCK)
				break;

			lock_page(page);

			/*
			 * page was truncated, stop here. if this isn't the
			 * first page, we'll just complete what we already
			 * added
			 */
			if (!page->mapping) {
				unlock_page(page);
				break;
			}
			/*
			 * page was already under io and is now done, great
			 */
			if (PageUptodate(page)) {
				unlock_page(page);
				goto fill_it;
			}

			/*
			 * need to read in the page
			 */
			error = mapping->a_ops->readpage(in, page);
			if (unlikely(error)) {
				/*
				 * We really should re-lookup the page here,
				 * but it complicates things a lot. Instead
				 * lets just do what we already stored, and
				 * we'll get it the next time we are called.
				 */
				if (error == AOP_TRUNCATED_PAGE)
					error = 0;

				break;
			}

			/*
			 * i_size must be checked after ->readpage().
			 */
			isize = i_size_read(mapping->host);
			end_index = (isize - 1) >> PAGE_CACHE_SHIFT;
			if (unlikely(!isize || index > end_index))
				break;

			/*
			 * if this is the last page, see if we need to shrink
			 * the length and stop
			 */
			if (end_index == index) {
				loff = PAGE_CACHE_SIZE - (isize & ~PAGE_CACHE_MASK);
				if (total_len + loff > isize)
					break;
				/*
				 * force quit after adding this page
				 */
				len = this_len;
				this_len = min(this_len, loff);
				loff = 0;
			}
		}
fill_it:
		partial[page_nr].offset = loff;
		partial[page_nr].len = this_len;
		len -= this_len;
		total_len += this_len;
		loff = 0;
		spd.nr_pages++;
		index++;
	}

	/*
	 * Release any pages at the end, if we quit early. 'i' is how far
	 * we got, 'nr_pages' is how many pages are in the map.
	 */
	while (page_nr < nr_pages)
		page_cache_release(pages[page_nr++]);

	if (spd.nr_pages)
		return splice_to_pipe(pipe, &spd);

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
ssize_t generic_file_splice_read(struct file *in, loff_t *ppos,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags)
{
	ssize_t spliced;
	int ret;

	ret = 0;
	spliced = 0;

	while (len) {
		ret = __generic_file_splice_read(in, ppos, pipe, len, flags);

		if (ret < 0)
			break;
		else if (!ret) {
			if (spliced)
				break;
			if (flags & SPLICE_F_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
		}

		*ppos += ret;
		len -= ret;
		spliced += ret;
	}

	if (spliced)
		return spliced;

	return ret;
}

EXPORT_SYMBOL(generic_file_splice_read);

/*
 * Send 'sd->len' bytes to socket from 'sd->file' at position 'sd->pos'
 * using sendpage(). Return the number of bytes sent.
 */
static int pipe_to_sendpage(struct pipe_inode_info *pipe,
			    struct pipe_buffer *buf, struct splice_desc *sd)
{
	struct file *file = sd->file;
	loff_t pos = sd->pos;
	int ret, more;

	ret = buf->ops->pin(pipe, buf);
	if (!ret) {
		more = (sd->flags & SPLICE_F_MORE) || sd->len < sd->total_len;

		ret = file->f_op->sendpage(file, buf->page, buf->offset,
					   sd->len, &pos, more);
	}

	return ret;
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
static int pipe_to_file(struct pipe_inode_info *pipe, struct pipe_buffer *buf,
			struct splice_desc *sd)
{
	struct file *file = sd->file;
	struct address_space *mapping = file->f_mapping;
	gfp_t gfp_mask = mapping_gfp_mask(mapping);
	unsigned int offset, this_len;
	struct page *page;
	pgoff_t index;
	int ret;

	/*
	 * make sure the data in this buffer is uptodate
	 */
	ret = buf->ops->pin(pipe, buf);
	if (unlikely(ret))
		return ret;

	index = sd->pos >> PAGE_CACHE_SHIFT;
	offset = sd->pos & ~PAGE_CACHE_MASK;

	this_len = sd->len;
	if (this_len + offset > PAGE_CACHE_SIZE)
		this_len = PAGE_CACHE_SIZE - offset;

	/*
	 * Reuse buf page, if SPLICE_F_MOVE is set and we are doing a full
	 * page.
	 */
	if ((sd->flags & SPLICE_F_MOVE) && this_len == PAGE_CACHE_SIZE) {
		/*
		 * If steal succeeds, buf->page is now pruned from the
		 * pagecache and we can reuse it. The page will also be
		 * locked on successful return.
		 */
		if (buf->ops->steal(pipe, buf))
			goto find_page;

		page = buf->page;
		if (add_to_page_cache(page, mapping, index, gfp_mask)) {
			unlock_page(page);
			goto find_page;
		}

		page_cache_get(page);

		if (!(buf->flags & PIPE_BUF_FLAG_LRU))
			lru_cache_add(page);
	} else {
find_page:
		page = find_lock_page(mapping, index);
		if (!page) {
			ret = -ENOMEM;
			page = page_cache_alloc_cold(mapping);
			if (unlikely(!page))
				goto out_nomem;

			/*
			 * This will also lock the page
			 */
			ret = add_to_page_cache_lru(page, mapping, index,
						    gfp_mask);
			if (unlikely(ret))
				goto out;
		}

		/*
		 * We get here with the page locked. If the page is also
		 * uptodate, we don't need to do more. If it isn't, we
		 * may need to bring it in if we are not going to overwrite
		 * the full page.
		 */
		if (!PageUptodate(page)) {
			if (this_len < PAGE_CACHE_SIZE) {
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
			} else
				SetPageUptodate(page);
		}
	}

	ret = mapping->a_ops->prepare_write(file, page, offset, offset+this_len);
	if (unlikely(ret)) {
		loff_t isize = i_size_read(mapping->host);

		if (ret != AOP_TRUNCATED_PAGE)
			unlock_page(page);
		page_cache_release(page);
		if (ret == AOP_TRUNCATED_PAGE)
			goto find_page;

		/*
		 * prepare_write() may have instantiated a few blocks
		 * outside i_size.  Trim these off again.
		 */
		if (sd->pos + this_len > isize)
			vmtruncate(mapping->host, isize);

		goto out;
	}

	if (buf->page != page) {
		/*
		 * Careful, ->map() uses KM_USER0!
		 */
		char *src = buf->ops->map(pipe, buf, 1);
		char *dst = kmap_atomic(page, KM_USER1);

		memcpy(dst + offset, src + buf->offset, this_len);
		flush_dcache_page(page);
		kunmap_atomic(dst, KM_USER1);
		buf->ops->unmap(pipe, buf, src);
	}

	ret = mapping->a_ops->commit_write(file, page, offset, offset+this_len);
	if (!ret) {
		/*
		 * Return the number of bytes written and mark page as
		 * accessed, we are now done!
		 */
		ret = this_len;
		mark_page_accessed(page);
		balance_dirty_pages_ratelimited(mapping);
	} else if (ret == AOP_TRUNCATED_PAGE) {
		page_cache_release(page);
		goto find_page;
	}
out:
	page_cache_release(page);
	unlock_page(page);
out_nomem:
	return ret;
}

/*
 * Pipe input worker. Most of this logic works like a regular pipe, the
 * key here is the 'actor' worker passed in that actually moves the data
 * to the wanted destination. See pipe_to_file/pipe_to_sendpage above.
 */
ssize_t splice_from_pipe(struct pipe_inode_info *pipe, struct file *out,
			 loff_t *ppos, size_t len, unsigned int flags,
			 splice_actor *actor)
{
	int ret, do_wakeup, err;
	struct splice_desc sd;

	ret = 0;
	do_wakeup = 0;

	sd.total_len = len;
	sd.flags = flags;
	sd.file = out;
	sd.pos = *ppos;

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
			if (err <= 0) {
				if (!ret && err != -ENODATA)
					ret = err;

				break;
			}

			ret += err;
			buf->offset += err;
			buf->len -= err;

			sd.len -= err;
			sd.pos += err;
			sd.total_len -= err;
			if (sd.len)
				continue;

			if (!buf->len) {
				buf->ops = NULL;
				ops->release(pipe, buf);
				pipe->curbuf = (pipe->curbuf + 1) & (PIPE_BUFFERS - 1);
				pipe->nrbufs--;
				if (pipe->inode)
					do_wakeup = 1;
			}

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
			  loff_t *ppos, size_t len, unsigned int flags)
{
	struct address_space *mapping = out->f_mapping;
	ssize_t ret;

	ret = splice_from_pipe(pipe, out, ppos, len, flags, pipe_to_file);
	if (ret > 0) {
		struct inode *inode = mapping->host;

		*ppos += ret;

		/*
		 * If file or inode is SYNC and we actually wrote some data,
		 * sync it.
		 */
		if (unlikely((out->f_flags & O_SYNC) || IS_SYNC(inode))) {
			int err;

			mutex_lock(&inode->i_mutex);
			err = generic_osync_inode(inode, mapping,
						  OSYNC_METADATA|OSYNC_DATA);
			mutex_unlock(&inode->i_mutex);

			if (err)
				ret = err;
		}
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
				loff_t *ppos, size_t len, unsigned int flags)
{
	return splice_from_pipe(pipe, out, ppos, len, flags, pipe_to_sendpage);
}

EXPORT_SYMBOL(generic_splice_sendpage);

/*
 * Attempt to initiate a splice from pipe to file.
 */
static long do_splice_from(struct pipe_inode_info *pipe, struct file *out,
			   loff_t *ppos, size_t len, unsigned int flags)
{
	int ret;

	if (unlikely(!out->f_op || !out->f_op->splice_write))
		return -EINVAL;

	if (unlikely(!(out->f_mode & FMODE_WRITE)))
		return -EBADF;

	ret = rw_verify_area(WRITE, out, ppos, len);
	if (unlikely(ret < 0))
		return ret;

	return out->f_op->splice_write(pipe, out, ppos, len, flags);
}

/*
 * Attempt to initiate a splice from a file to a pipe.
 */
static long do_splice_to(struct file *in, loff_t *ppos,
			 struct pipe_inode_info *pipe, size_t len,
			 unsigned int flags)
{
	loff_t isize, left;
	int ret;

	if (unlikely(!in->f_op || !in->f_op->splice_read))
		return -EINVAL;

	if (unlikely(!(in->f_mode & FMODE_READ)))
		return -EBADF;

	ret = rw_verify_area(READ, in, ppos, len);
	if (unlikely(ret < 0))
		return ret;

	isize = i_size_read(in->f_mapping->host);
	if (unlikely(*ppos >= isize))
		return 0;
	
	left = isize - *ppos;
	if (unlikely(left < len))
		len = left;

	return in->f_op->splice_read(in, ppos, pipe, len, flags);
}

long do_splice_direct(struct file *in, loff_t *ppos, struct file *out,
		      size_t len, unsigned int flags)
{
	struct pipe_inode_info *pipe;
	long ret, bytes;
	loff_t out_off;
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
	out_off = 0;

	while (len) {
		size_t read_len, max_read_len;

		/*
		 * Do at most PIPE_BUFFERS pages worth of transfer:
		 */
		max_read_len = min(len, (size_t)(PIPE_BUFFERS*PAGE_SIZE));

		ret = do_splice_to(in, ppos, pipe, max_read_len, flags);
		if (unlikely(ret < 0))
			goto out_release;

		read_len = ret;

		/*
		 * NOTE: nonblocking mode only applies to the input. We
		 * must not do the output in nonblocking mode as then we
		 * could get stuck data in the internal pipe:
		 */
		ret = do_splice_from(pipe, out, &out_off, read_len,
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
	loff_t offset, *off;
	long ret;

	pipe = in->f_dentry->d_inode->i_pipe;
	if (pipe) {
		if (off_in)
			return -ESPIPE;
		if (off_out) {
			if (out->f_op->llseek == no_llseek)
				return -EINVAL;
			if (copy_from_user(&offset, off_out, sizeof(loff_t)))
				return -EFAULT;
			off = &offset;
		} else
			off = &out->f_pos;

		ret = do_splice_from(pipe, out, off, len, flags);

		if (off_out && copy_to_user(off_out, off, sizeof(loff_t)))
			ret = -EFAULT;

		return ret;
	}

	pipe = out->f_dentry->d_inode->i_pipe;
	if (pipe) {
		if (off_out)
			return -ESPIPE;
		if (off_in) {
			if (in->f_op->llseek == no_llseek)
				return -EINVAL;
			if (copy_from_user(&offset, off_in, sizeof(loff_t)))
				return -EFAULT;
			off = &offset;
		} else
			off = &in->f_pos;

		ret = do_splice_to(in, off, pipe, len, flags);

		if (off_in && copy_to_user(off_in, off, sizeof(loff_t)))
			ret = -EFAULT;

		return ret;
	}

	return -EINVAL;
}

/*
 * Map an iov into an array of pages and offset/length tupples. With the
 * partial_page structure, we can map several non-contiguous ranges into
 * our ones pages[] map instead of splitting that operation into pieces.
 * Could easily be exported as a generic helper for other users, in which
 * case one would probably want to add a 'max_nr_pages' parameter as well.
 */
static int get_iovec_page_array(const struct iovec __user *iov,
				unsigned int nr_vecs, struct page **pages,
				struct partial_page *partial, int aligned)
{
	int buffers = 0, error = 0;

	/*
	 * It's ok to take the mmap_sem for reading, even
	 * across a "get_user()".
	 */
	down_read(&current->mm->mmap_sem);

	while (nr_vecs) {
		unsigned long off, npages;
		void __user *base;
		size_t len;
		int i;

		/*
		 * Get user address base and length for this iovec.
		 */
		error = get_user(base, &iov->iov_base);
		if (unlikely(error))
			break;
		error = get_user(len, &iov->iov_len);
		if (unlikely(error))
			break;

		/*
		 * Sanity check this iovec. 0 read succeeds.
		 */
		if (unlikely(!len))
			break;
		error = -EFAULT;
		if (unlikely(!base))
			break;

		/*
		 * Get this base offset and number of pages, then map
		 * in the user pages.
		 */
		off = (unsigned long) base & ~PAGE_MASK;

		/*
		 * If asked for alignment, the offset must be zero and the
		 * length a multiple of the PAGE_SIZE.
		 */
		error = -EINVAL;
		if (aligned && (off || len & ~PAGE_MASK))
			break;

		npages = (off + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		if (npages > PIPE_BUFFERS - buffers)
			npages = PIPE_BUFFERS - buffers;

		error = get_user_pages(current, current->mm,
				       (unsigned long) base, npages, 0, 0,
				       &pages[buffers], NULL);

		if (unlikely(error <= 0))
			break;

		/*
		 * Fill this contiguous range into the partial page map.
		 */
		for (i = 0; i < error; i++) {
			const int plen = min_t(size_t, len, PAGE_SIZE - off);

			partial[buffers].offset = off;
			partial[buffers].len = plen;

			off = 0;
			len -= plen;
			buffers++;
		}

		/*
		 * We didn't complete this iov, stop here since it probably
		 * means we have to move some of this into a pipe to
		 * be able to continue.
		 */
		if (len)
			break;

		/*
		 * Don't continue if we mapped fewer pages than we asked for,
		 * or if we mapped the max number of pages that we have
		 * room for.
		 */
		if (error < npages || buffers == PIPE_BUFFERS)
			break;

		nr_vecs--;
		iov++;
	}

	up_read(&current->mm->mmap_sem);

	if (buffers)
		return buffers;

	return error;
}

/*
 * vmsplice splices a user address range into a pipe. It can be thought of
 * as splice-from-memory, where the regular splice is splice-from-file (or
 * to file). In both cases the output is a pipe, naturally.
 *
 * Note that vmsplice only supports splicing _from_ user memory to a pipe,
 * not the other way around. Splicing from user memory is a simple operation
 * that can be supported without any funky alignment restrictions or nasty
 * vm tricks. We simply map in the user memory and fill them into a pipe.
 * The reverse isn't quite as easy, though. There are two possible solutions
 * for that:
 *
 *	- memcpy() the data internally, at which point we might as well just
 *	  do a regular read() on the buffer anyway.
 *	- Lots of nasty vm tricks, that are neither fast nor flexible (it
 *	  has restriction limitations on both ends of the pipe).
 *
 * Alas, it isn't here.
 *
 */
static long do_vmsplice(struct file *file, const struct iovec __user *iov,
			unsigned long nr_segs, unsigned int flags)
{
	struct pipe_inode_info *pipe = file->f_dentry->d_inode->i_pipe;
	struct page *pages[PIPE_BUFFERS];
	struct partial_page partial[PIPE_BUFFERS];
	struct splice_pipe_desc spd = {
		.pages = pages,
		.partial = partial,
		.flags = flags,
		.ops = &user_page_pipe_buf_ops,
	};

	if (unlikely(!pipe))
		return -EBADF;
	if (unlikely(nr_segs > UIO_MAXIOV))
		return -EINVAL;
	else if (unlikely(!nr_segs))
		return 0;

	spd.nr_pages = get_iovec_page_array(iov, nr_segs, pages, partial,
					    flags & SPLICE_F_GIFT);
	if (spd.nr_pages <= 0)
		return spd.nr_pages;

	return splice_to_pipe(pipe, &spd);
}

asmlinkage long sys_vmsplice(int fd, const struct iovec __user *iov,
			     unsigned long nr_segs, unsigned int flags)
{
	struct file *file;
	long error;
	int fput;

	error = -EBADF;
	file = fget_light(fd, &fput);
	if (file) {
		if (file->f_mode & FMODE_WRITE)
			error = do_vmsplice(file, iov, nr_segs, flags);

		fput_light(file, fput);
	}

	return error;
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

/*
 * Link contents of ipipe to opipe.
 */
static int link_pipe(struct pipe_inode_info *ipipe,
		     struct pipe_inode_info *opipe,
		     size_t len, unsigned int flags)
{
	struct pipe_buffer *ibuf, *obuf;
	int ret, do_wakeup, i, ipipe_first;

	ret = do_wakeup = ipipe_first = 0;

	/*
	 * Potential ABBA deadlock, work around it by ordering lock
	 * grabbing by inode address. Otherwise two different processes
	 * could deadlock (one doing tee from A -> B, the other from B -> A).
	 */
	if (ipipe->inode < opipe->inode) {
		ipipe_first = 1;
		mutex_lock(&ipipe->inode->i_mutex);
		mutex_lock(&opipe->inode->i_mutex);
	} else {
		mutex_lock(&opipe->inode->i_mutex);
		mutex_lock(&ipipe->inode->i_mutex);
	}

	for (i = 0;; i++) {
		if (!opipe->readers) {
			send_sig(SIGPIPE, current, 0);
			if (!ret)
				ret = -EPIPE;
			break;
		}
		if (ipipe->nrbufs - i) {
			ibuf = ipipe->bufs + ((ipipe->curbuf + i) & (PIPE_BUFFERS - 1));

			/*
			 * If we have room, fill this buffer
			 */
			if (opipe->nrbufs < PIPE_BUFFERS) {
				int nbuf = (opipe->curbuf + opipe->nrbufs) & (PIPE_BUFFERS - 1);

				/*
				 * Get a reference to this pipe buffer,
				 * so we can copy the contents over.
				 */
				ibuf->ops->get(ipipe, ibuf);

				obuf = opipe->bufs + nbuf;
				*obuf = *ibuf;

				/*
				 * Don't inherit the gift flag, we need to
				 * prevent multiple steals of this page.
				 */
				obuf->flags &= ~PIPE_BUF_FLAG_GIFT;

				if (obuf->len > len)
					obuf->len = len;

				opipe->nrbufs++;
				do_wakeup = 1;
				ret += obuf->len;
				len -= obuf->len;

				if (!len)
					break;
				if (opipe->nrbufs < PIPE_BUFFERS)
					continue;
			}

			/*
			 * We have input available, but no output room.
			 * If we already copied data, return that. If we
			 * need to drop the opipe lock, it must be ordered
			 * last to avoid deadlocks.
			 */
			if ((flags & SPLICE_F_NONBLOCK) || !ipipe_first) {
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
				if (waitqueue_active(&opipe->wait))
					wake_up_interruptible(&opipe->wait);
				kill_fasync(&opipe->fasync_readers, SIGIO, POLL_IN);
				do_wakeup = 0;
			}

			opipe->waiting_writers++;
			pipe_wait(opipe);
			opipe->waiting_writers--;
			continue;
		}

		/*
		 * No input buffers, do the usual checks for available
		 * writers and blocking and wait if necessary
		 */
		if (!ipipe->writers)
			break;
		if (!ipipe->waiting_writers) {
			if (ret)
				break;
		}
		/*
		 * pipe_wait() drops the ipipe mutex. To avoid deadlocks
		 * with another process, we can only safely do that if
		 * the ipipe lock is ordered last.
		 */
		if ((flags & SPLICE_F_NONBLOCK) || ipipe_first) {
			if (!ret)
				ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			if (!ret)
				ret = -ERESTARTSYS;
			break;
		}

		if (waitqueue_active(&ipipe->wait))
			wake_up_interruptible_sync(&ipipe->wait);
		kill_fasync(&ipipe->fasync_writers, SIGIO, POLL_OUT);

		pipe_wait(ipipe);
	}

	mutex_unlock(&ipipe->inode->i_mutex);
	mutex_unlock(&opipe->inode->i_mutex);

	if (do_wakeup) {
		smp_mb();
		if (waitqueue_active(&opipe->wait))
			wake_up_interruptible(&opipe->wait);
		kill_fasync(&opipe->fasync_readers, SIGIO, POLL_IN);
	}

	return ret;
}

/*
 * This is a tee(1) implementation that works on pipes. It doesn't copy
 * any data, it simply references the 'in' pages on the 'out' pipe.
 * The 'flags' used are the SPLICE_F_* variants, currently the only
 * applicable one is SPLICE_F_NONBLOCK.
 */
static long do_tee(struct file *in, struct file *out, size_t len,
		   unsigned int flags)
{
	struct pipe_inode_info *ipipe = in->f_dentry->d_inode->i_pipe;
	struct pipe_inode_info *opipe = out->f_dentry->d_inode->i_pipe;

	/*
	 * Link ipipe to the two output pipes, consuming as we go along.
	 */
	if (ipipe && opipe)
		return link_pipe(ipipe, opipe, len, flags);

	return -EINVAL;
}

asmlinkage long sys_tee(int fdin, int fdout, size_t len, unsigned int flags)
{
	struct file *in;
	int error, fput_in;

	if (unlikely(!len))
		return 0;

	error = -EBADF;
	in = fget_light(fdin, &fput_in);
	if (in) {
		if (in->f_mode & FMODE_READ) {
			int fput_out;
			struct file *out = fget_light(fdout, &fput_out);

			if (out) {
				if (out->f_mode & FMODE_WRITE)
					error = do_tee(in, out, len, flags);
				fput_light(out, fput_out);
			}
		}
 		fput_light(in, fput_in);
 	}

	return error;
}
