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
 * Jens to support splicing to files and fixing the initial implementation
 * bugs.
 *
 * Copyright (C) 2005 Jens Axboe <axboe@suse.de>
 * Copyright (C) 2005 Linus Torvalds <torvalds@osdl.org>
 *
 */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/pipe_fs_i.h>
#include <linux/mm_inline.h>
#include <linux/swap.h>
#include <linux/module.h>

/*
 * Passed to the actors
 */
struct splice_desc {
	unsigned int len, total_len;	/* current and remaining length */
	unsigned int flags;		/* splice flags */
	struct file *file;		/* file to read/write */
	loff_t pos;			/* file position */
};

static int page_cache_pipe_buf_steal(struct pipe_inode_info *info,
				     struct pipe_buffer *buf)
{
	struct page *page = buf->page;

	WARN_ON(!PageLocked(page));
	WARN_ON(!PageUptodate(page));

	if (!remove_mapping(page_mapping(page), page))
		return 1;

	if (PageLRU(page)) {
		struct zone *zone = page_zone(page);

		spin_lock_irq(&zone->lru_lock);
		BUG_ON(!PageLRU(page));
		__ClearPageLRU(page);
		del_page_from_lru(zone, page);
		spin_unlock_irq(&zone->lru_lock);
	}

	buf->stolen = 1;
	return 0;
}

static void page_cache_pipe_buf_release(struct pipe_inode_info *info,
					struct pipe_buffer *buf)
{
	page_cache_release(buf->page);
	buf->page = NULL;
	buf->stolen = 0;
}

static void *page_cache_pipe_buf_map(struct file *file,
				     struct pipe_inode_info *info,
				     struct pipe_buffer *buf)
{
	struct page *page = buf->page;

	lock_page(page);

	if (!PageUptodate(page)) {
		unlock_page(page);
		return ERR_PTR(-EIO);
	}

	if (!page->mapping) {
		unlock_page(page);
		return ERR_PTR(-ENODATA);
	}

	return kmap(buf->page);
}

static void page_cache_pipe_buf_unmap(struct pipe_inode_info *info,
				      struct pipe_buffer *buf)
{
	if (!buf->stolen)
		unlock_page(buf->page);
	kunmap(buf->page);
}

static struct pipe_buf_operations page_cache_pipe_buf_ops = {
	.can_merge = 0,
	.map = page_cache_pipe_buf_map,
	.unmap = page_cache_pipe_buf_unmap,
	.release = page_cache_pipe_buf_release,
	.steal = page_cache_pipe_buf_steal,
};

static ssize_t move_to_pipe(struct inode *inode, struct page **pages,
			    int nr_pages, unsigned long offset,
			    unsigned long len, unsigned int flags)
{
	struct pipe_inode_info *info;
	int ret, do_wakeup, i;

	ret = 0;
	do_wakeup = 0;
	i = 0;

	mutex_lock(PIPE_MUTEX(*inode));

	info = inode->i_pipe;
	for (;;) {
		int bufs;

		if (!PIPE_READERS(*inode)) {
			send_sig(SIGPIPE, current, 0);
			if (!ret)
				ret = -EPIPE;
			break;
		}

		bufs = info->nrbufs;
		if (bufs < PIPE_BUFFERS) {
			int newbuf = (info->curbuf + bufs) & (PIPE_BUFFERS - 1);
			struct pipe_buffer *buf = info->bufs + newbuf;
			struct page *page = pages[i++];
			unsigned long this_len;

			this_len = PAGE_CACHE_SIZE - offset;
			if (this_len > len)
				this_len = len;

			buf->page = page;
			buf->offset = offset;
			buf->len = this_len;
			buf->ops = &page_cache_pipe_buf_ops;
			info->nrbufs = ++bufs;
			do_wakeup = 1;

			ret += this_len;
			len -= this_len;
			offset = 0;
			if (!--nr_pages)
				break;
			if (!len)
				break;
			if (bufs < PIPE_BUFFERS)
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
			wake_up_interruptible_sync(PIPE_WAIT(*inode));
			kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO,
				    POLL_IN);
			do_wakeup = 0;
		}

		PIPE_WAITING_WRITERS(*inode)++;
		pipe_wait(inode);
		PIPE_WAITING_WRITERS(*inode)--;
	}

	mutex_unlock(PIPE_MUTEX(*inode));

	if (do_wakeup) {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_READERS(*inode), SIGIO, POLL_IN);
	}

	while (i < nr_pages)
		page_cache_release(pages[i++]);

	return ret;
}

static int __generic_file_splice_read(struct file *in, struct inode *pipe,
				      size_t len, unsigned int flags)
{
	struct address_space *mapping = in->f_mapping;
	unsigned int offset, nr_pages;
	struct page *pages[PIPE_BUFFERS], *shadow[PIPE_BUFFERS];
	struct page *page;
	pgoff_t index, pidx;
	int i, j;

	index = in->f_pos >> PAGE_CACHE_SHIFT;
	offset = in->f_pos & ~PAGE_CACHE_MASK;
	nr_pages = (len + offset + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	if (nr_pages > PIPE_BUFFERS)
		nr_pages = PIPE_BUFFERS;

	/*
	 * initiate read-ahead on this page range
	 */
	do_page_cache_readahead(mapping, in, index, nr_pages);

	/*
	 * Get as many pages from the page cache as possible..
	 * Start IO on the page cache entries we create (we
	 * can assume that any pre-existing ones we find have
	 * already had IO started on them).
	 */
	i = find_get_pages(mapping, index, nr_pages, pages);

	/*
	 * common case - we found all pages and they are contiguous,
	 * kick them off
	 */
	if (i && (pages[i - 1]->index == index + i - 1))
		goto splice_them;

	/*
	 * fill shadow[] with pages at the right locations, so we only
	 * have to fill holes
	 */
	memset(shadow, 0, nr_pages * sizeof(struct page *));
	for (j = 0; j < i; j++)
		shadow[pages[j]->index - index] = pages[j];

	/*
	 * now fill in the holes
	 */
	for (i = 0, pidx = index; i < nr_pages; pidx++, i++) {
		int error;

		if (shadow[i])
			continue;

		/*
		 * no page there, look one up / create it
		 */
		page = find_or_create_page(mapping, pidx,
						   mapping_gfp_mask(mapping));
		if (!page)
			break;

		if (PageUptodate(page))
			unlock_page(page);
		else {
			error = mapping->a_ops->readpage(in, page);

			if (unlikely(error)) {
				page_cache_release(page);
				break;
			}
		}
		shadow[i] = page;
	}

	if (!i) {
		for (i = 0; i < nr_pages; i++) {
			 if (shadow[i])
				page_cache_release(shadow[i]);
		}
		return 0;
	}

	memcpy(pages, shadow, i * sizeof(struct page *));

	/*
	 * Now we splice them into the pipe..
	 */
splice_them:
	return move_to_pipe(pipe, pages, i, offset, len, flags);
}

ssize_t generic_file_splice_read(struct file *in, struct inode *pipe,
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

/*
 * Send 'len' bytes to socket from 'file' at position 'pos' using sendpage().
 */
static int pipe_to_sendpage(struct pipe_inode_info *info,
			    struct pipe_buffer *buf, struct splice_desc *sd)
{
	struct file *file = sd->file;
	loff_t pos = sd->pos;
	unsigned int offset;
	ssize_t ret;
	void *ptr;

	/*
	 * sub-optimal, but we are limited by the pipe ->map. we don't
	 * need a kmap'ed buffer here, we just want to make sure we
	 * have the page pinned if the pipe page originates from the
	 * page cache
	 */
	ptr = buf->ops->map(file, info, buf);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	offset = pos & ~PAGE_CACHE_MASK;

	ret = file->f_op->sendpage(file, buf->page, offset, sd->len, &pos,
					sd->len < sd->total_len);

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
 * For now we just do the slower thing and always copy pages over, it's
 * easier than migrating pages from the pipe to the target file. For the
 * case of doing file | file splicing, the migrate approach had some LRU
 * nastiness...
 */
static int pipe_to_file(struct pipe_inode_info *info, struct pipe_buffer *buf,
			struct splice_desc *sd)
{
	struct file *file = sd->file;
	struct address_space *mapping = file->f_mapping;
	unsigned int offset;
	struct page *page;
	pgoff_t index;
	char *src;
	int ret;

	/*
	 * after this, page will be locked and unmapped
	 */
	src = buf->ops->map(file, info, buf);
	if (IS_ERR(src))
		return PTR_ERR(src);

	index = sd->pos >> PAGE_CACHE_SHIFT;
	offset = sd->pos & ~PAGE_CACHE_MASK;

	/*
	 * reuse buf page, if SPLICE_F_MOVE is set
	 */
	if (sd->flags & SPLICE_F_MOVE) {
		if (buf->ops->steal(info, buf))
			goto find_page;

		page = buf->page;
		if (add_to_page_cache_lru(page, mapping, index,
						mapping_gfp_mask(mapping)))
			goto find_page;
	} else {
find_page:
		ret = -ENOMEM;
		page = find_or_create_page(mapping, index,
						mapping_gfp_mask(mapping));
		if (!page)
			goto out;

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
					 * page got invalidated, repeat
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
	if (ret)
		goto out;

	if (!buf->stolen) {
		char *dst = kmap_atomic(page, KM_USER0);

		memcpy(dst + offset, src + buf->offset, sd->len);
		flush_dcache_page(page);
		kunmap_atomic(dst, KM_USER0);
	}

	ret = mapping->a_ops->commit_write(file, page, 0, sd->len);
	if (ret < 0)
		goto out;

	set_page_dirty(page);
	ret = write_one_page(page, 0);
out:
	if (ret < 0)
		unlock_page(page);
	if (!buf->stolen)
		page_cache_release(page);
	buf->ops->unmap(info, buf);
	return ret;
}

typedef int (splice_actor)(struct pipe_inode_info *, struct pipe_buffer *,
			   struct splice_desc *);

static ssize_t move_from_pipe(struct inode *inode, struct file *out,
			      size_t len, unsigned int flags,
			      splice_actor *actor)
{
	struct pipe_inode_info *info;
	int ret, do_wakeup, err;
	struct splice_desc sd;

	ret = 0;
	do_wakeup = 0;

	sd.total_len = len;
	sd.flags = flags;
	sd.file = out;
	sd.pos = out->f_pos;

	mutex_lock(PIPE_MUTEX(*inode));

	info = inode->i_pipe;
	for (;;) {
		int bufs = info->nrbufs;

		if (bufs) {
			int curbuf = info->curbuf;
			struct pipe_buffer *buf = info->bufs + curbuf;
			struct pipe_buf_operations *ops = buf->ops;

			sd.len = buf->len;
			if (sd.len > sd.total_len)
				sd.len = sd.total_len;

			err = actor(info, buf, &sd);
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
				ops->release(info, buf);
				curbuf = (curbuf + 1) & (PIPE_BUFFERS - 1);
				info->curbuf = curbuf;
				info->nrbufs = --bufs;
				do_wakeup = 1;
			}

			sd.pos += sd.len;
			sd.total_len -= sd.len;
			if (!sd.total_len)
				break;
		}

		if (bufs)
			continue;
		if (!PIPE_WRITERS(*inode))
			break;
		if (!PIPE_WAITING_WRITERS(*inode)) {
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
			wake_up_interruptible_sync(PIPE_WAIT(*inode));
			kill_fasync(PIPE_FASYNC_WRITERS(*inode),SIGIO,POLL_OUT);
			do_wakeup = 0;
		}

		pipe_wait(inode);
	}

	mutex_unlock(PIPE_MUTEX(*inode));

	if (do_wakeup) {
		wake_up_interruptible(PIPE_WAIT(*inode));
		kill_fasync(PIPE_FASYNC_WRITERS(*inode), SIGIO, POLL_OUT);
	}

	mutex_lock(&out->f_mapping->host->i_mutex);
	out->f_pos = sd.pos;
	mutex_unlock(&out->f_mapping->host->i_mutex);
	return ret;

}

ssize_t generic_file_splice_write(struct inode *inode, struct file *out,
				  size_t len, unsigned int flags)
{
	return move_from_pipe(inode, out, len, flags, pipe_to_file);
}

ssize_t generic_splice_sendpage(struct inode *inode, struct file *out,
				size_t len, unsigned int flags)
{
	return move_from_pipe(inode, out, len, flags, pipe_to_sendpage);
}

EXPORT_SYMBOL(generic_file_splice_write);
EXPORT_SYMBOL(generic_file_splice_read);

static long do_splice_from(struct inode *pipe, struct file *out, size_t len,
			   unsigned int flags)
{
	loff_t pos;
	int ret;

	if (!out->f_op || !out->f_op->splice_write)
		return -EINVAL;

	if (!(out->f_mode & FMODE_WRITE))
		return -EBADF;

	pos = out->f_pos;
	ret = rw_verify_area(WRITE, out, &pos, len);
	if (unlikely(ret < 0))
		return ret;

	return out->f_op->splice_write(pipe, out, len, flags);
}

static long do_splice_to(struct file *in, struct inode *pipe, size_t len,
			 unsigned int flags)
{
	loff_t pos, isize, left;
	int ret;

	if (!in->f_op || !in->f_op->splice_read)
		return -EINVAL;

	if (!(in->f_mode & FMODE_READ))
		return -EBADF;

	pos = in->f_pos;
	ret = rw_verify_area(READ, in, &pos, len);
	if (unlikely(ret < 0))
		return ret;

	isize = i_size_read(in->f_mapping->host);
	if (unlikely(in->f_pos >= isize))
		return 0;
	
	left = isize - in->f_pos;
	if (left < len)
		len = left;

	return in->f_op->splice_read(in, pipe, len, flags);
}

static long do_splice(struct file *in, struct file *out, size_t len,
		      unsigned int flags)
{
	struct inode *pipe;

	pipe = in->f_dentry->d_inode;
	if (pipe->i_pipe)
		return do_splice_from(pipe, out, len, flags);

	pipe = out->f_dentry->d_inode;
	if (pipe->i_pipe)
		return do_splice_to(in, pipe, len, flags);

	return -EINVAL;
}

asmlinkage long sys_splice(int fdin, int fdout, size_t len, unsigned int flags)
{
	long error;
	struct file *in, *out;
	int fput_in, fput_out;

	if (unlikely(!len))
		return 0;

	error = -EBADF;
	in = fget_light(fdin, &fput_in);
	if (in) {
		if (in->f_mode & FMODE_READ) {
			out = fget_light(fdout, &fput_out);
			if (out) {
				if (out->f_mode & FMODE_WRITE)
					error = do_splice(in, out, len, flags);
				fput_light(out, fput_out);
			}
		}

		fput_light(in, fput_in);
	}

	return error;
}
