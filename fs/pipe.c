/*
 *  linux/fs/pipe.c
 *
 *  Copyright (C) 1991, 1992, 1999  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pipe_fs_i.h>
#include <linux/uio.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/audit.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>
#include <asm/ioctls.h>

/*
 * We use a start+len construction, which provides full use of the 
 * allocated memory.
 * -- Florian Coosmann (FGC)
 * 
 * Reads with count = 0 should always return 0.
 * -- Julian Bradfield 1999-06-07.
 *
 * FIFOs and Pipes now generate SIGIO for both readers and writers.
 * -- Jeremy Elson <jelson@circlemud.org> 2001-08-16
 *
 * pipe_read & write cleanup
 * -- Manfred Spraul <manfred@colorfullife.com> 2002-05-09
 */

static void pipe_lock_nested(struct pipe_inode_info *pipe, int subclass)
{
	if (pipe->inode)
		mutex_lock_nested(&pipe->inode->i_mutex, subclass);
}

void pipe_lock(struct pipe_inode_info *pipe)
{
	/*
	 * pipe_lock() nests non-pipe inode locks (for writing to a file)
	 */
	pipe_lock_nested(pipe, I_MUTEX_PARENT);
}
EXPORT_SYMBOL(pipe_lock);

void pipe_unlock(struct pipe_inode_info *pipe)
{
	if (pipe->inode)
		mutex_unlock(&pipe->inode->i_mutex);
}
EXPORT_SYMBOL(pipe_unlock);

void pipe_double_lock(struct pipe_inode_info *pipe1,
		      struct pipe_inode_info *pipe2)
{
	BUG_ON(pipe1 == pipe2);

	if (pipe1 < pipe2) {
		pipe_lock_nested(pipe1, I_MUTEX_PARENT);
		pipe_lock_nested(pipe2, I_MUTEX_CHILD);
	} else {
		pipe_lock_nested(pipe2, I_MUTEX_PARENT);
		pipe_lock_nested(pipe1, I_MUTEX_CHILD);
	}
}

/* Drop the inode semaphore and wait for a pipe event, atomically */
void pipe_wait(struct pipe_inode_info *pipe)
{
	DEFINE_WAIT(wait);

	/*
	 * Pipes are system-local resources, so sleeping on them
	 * is considered a noninteractive wait:
	 */
	prepare_to_wait(&pipe->wait, &wait, TASK_INTERRUPTIBLE);
	pipe_unlock(pipe);
	schedule();
	finish_wait(&pipe->wait, &wait);
	pipe_lock(pipe);
}

static int
pipe_iov_copy_from_user(void *to, struct iovec *iov, unsigned long len,
			int atomic)
{
	unsigned long copy;

	while (len > 0) {
		while (!iov->iov_len)
			iov++;
		copy = min_t(unsigned long, len, iov->iov_len);

		if (atomic) {
			if (__copy_from_user_inatomic(to, iov->iov_base, copy))
				return -EFAULT;
		} else {
			if (copy_from_user(to, iov->iov_base, copy))
				return -EFAULT;
		}
		to += copy;
		len -= copy;
		iov->iov_base += copy;
		iov->iov_len -= copy;
	}
	return 0;
}

static int
pipe_iov_copy_to_user(struct iovec *iov, const void *from, unsigned long len,
		      int atomic)
{
	unsigned long copy;

	while (len > 0) {
		while (!iov->iov_len)
			iov++;
		copy = min_t(unsigned long, len, iov->iov_len);

		if (atomic) {
			if (__copy_to_user_inatomic(iov->iov_base, from, copy))
				return -EFAULT;
		} else {
			if (copy_to_user(iov->iov_base, from, copy))
				return -EFAULT;
		}
		from += copy;
		len -= copy;
		iov->iov_base += copy;
		iov->iov_len -= copy;
	}
	return 0;
}

/*
 * Attempt to pre-fault in the user memory, so we can use atomic copies.
 * Returns the number of bytes not faulted in.
 */
static int iov_fault_in_pages_write(struct iovec *iov, unsigned long len)
{
	while (!iov->iov_len)
		iov++;

	while (len > 0) {
		unsigned long this_len;

		this_len = min_t(unsigned long, len, iov->iov_len);
		if (fault_in_pages_writeable(iov->iov_base, this_len))
			break;

		len -= this_len;
		iov++;
	}

	return len;
}

/*
 * Pre-fault in the user memory, so we can use atomic copies.
 */
static void iov_fault_in_pages_read(struct iovec *iov, unsigned long len)
{
	while (!iov->iov_len)
		iov++;

	while (len > 0) {
		unsigned long this_len;

		this_len = min_t(unsigned long, len, iov->iov_len);
		fault_in_pages_readable(iov->iov_base, this_len);
		len -= this_len;
		iov++;
	}
}

static void anon_pipe_buf_release(struct pipe_inode_info *pipe,
				  struct pipe_buffer *buf)
{
	struct page *page = buf->page;

	/*
	 * If nobody else uses this page, and we don't already have a
	 * temporary page, let's keep track of it as a one-deep
	 * allocation cache. (Otherwise just release our reference to it)
	 */
	if (page_count(page) == 1 && !pipe->tmp_page)
		pipe->tmp_page = page;
	else
		page_cache_release(page);
}

/**
 * generic_pipe_buf_map - virtually map a pipe buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer that should be mapped
 * @atomic:	whether to use an atomic map
 *
 * Description:
 *	This function returns a kernel virtual address mapping for the
 *	pipe_buffer passed in @buf. If @atomic is set, an atomic map is provided
 *	and the caller has to be careful not to fault before calling
 *	the unmap function.
 *
 *	Note that this function occupies KM_USER0 if @atomic != 0.
 */
void *generic_pipe_buf_map(struct pipe_inode_info *pipe,
			   struct pipe_buffer *buf, int atomic)
{
	if (atomic) {
		buf->flags |= PIPE_BUF_FLAG_ATOMIC;
		return kmap_atomic(buf->page, KM_USER0);
	}

	return kmap(buf->page);
}

/**
 * generic_pipe_buf_unmap - unmap a previously mapped pipe buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer that should be unmapped
 * @map_data:	the data that the mapping function returned
 *
 * Description:
 *	This function undoes the mapping that ->map() provided.
 */
void generic_pipe_buf_unmap(struct pipe_inode_info *pipe,
			    struct pipe_buffer *buf, void *map_data)
{
	if (buf->flags & PIPE_BUF_FLAG_ATOMIC) {
		buf->flags &= ~PIPE_BUF_FLAG_ATOMIC;
		kunmap_atomic(map_data, KM_USER0);
	} else
		kunmap(buf->page);
}

/**
 * generic_pipe_buf_steal - attempt to take ownership of a &pipe_buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to attempt to steal
 *
 * Description:
 *	This function attempts to steal the &struct page attached to
 *	@buf. If successful, this function returns 0 and returns with
 *	the page locked. The caller may then reuse the page for whatever
 *	he wishes; the typical use is insertion into a different file
 *	page cache.
 */
int generic_pipe_buf_steal(struct pipe_inode_info *pipe,
			   struct pipe_buffer *buf)
{
	struct page *page = buf->page;

	/*
	 * A reference of one is golden, that means that the owner of this
	 * page is the only one holding a reference to it. lock the page
	 * and return OK.
	 */
	if (page_count(page) == 1) {
		lock_page(page);
		return 0;
	}

	return 1;
}

/**
 * generic_pipe_buf_get - get a reference to a &struct pipe_buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to get a reference to
 *
 * Description:
 *	This function grabs an extra reference to @buf. It's used in
 *	in the tee() system call, when we duplicate the buffers in one
 *	pipe into another.
 */
void generic_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
{
	page_cache_get(buf->page);
}

/**
 * generic_pipe_buf_confirm - verify contents of the pipe buffer
 * @info:	the pipe that the buffer belongs to
 * @buf:	the buffer to confirm
 *
 * Description:
 *	This function does nothing, because the generic pipe code uses
 *	pages that are always good when inserted into the pipe.
 */
int generic_pipe_buf_confirm(struct pipe_inode_info *info,
			     struct pipe_buffer *buf)
{
	return 0;
}

/**
 * generic_pipe_buf_release - put a reference to a &struct pipe_buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to put a reference to
 *
 * Description:
 *	This function releases a reference to @buf.
 */
void generic_pipe_buf_release(struct pipe_inode_info *pipe,
			      struct pipe_buffer *buf)
{
	page_cache_release(buf->page);
}

static const struct pipe_buf_operations anon_pipe_buf_ops = {
	.can_merge = 1,
	.map = generic_pipe_buf_map,
	.unmap = generic_pipe_buf_unmap,
	.confirm = generic_pipe_buf_confirm,
	.release = anon_pipe_buf_release,
	.steal = generic_pipe_buf_steal,
	.get = generic_pipe_buf_get,
};

static ssize_t
pipe_read(struct kiocb *iocb, const struct iovec *_iov,
	   unsigned long nr_segs, loff_t pos)
{
	struct file *filp = iocb->ki_filp;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct pipe_inode_info *pipe;
	int do_wakeup;
	ssize_t ret;
	struct iovec *iov = (struct iovec *)_iov;
	size_t total_len;

	total_len = iov_length(iov, nr_segs);
	/* Null read succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	do_wakeup = 0;
	ret = 0;
	mutex_lock(&inode->i_mutex);
	pipe = inode->i_pipe;
	for (;;) {
		int bufs = pipe->nrbufs;
		if (bufs) {
			int curbuf = pipe->curbuf;
			struct pipe_buffer *buf = pipe->bufs + curbuf;
			const struct pipe_buf_operations *ops = buf->ops;
			void *addr;
			size_t chars = buf->len;
			int error, atomic;

			if (chars > total_len)
				chars = total_len;

			error = ops->confirm(pipe, buf);
			if (error) {
				if (!ret)
					error = ret;
				break;
			}

			atomic = !iov_fault_in_pages_write(iov, chars);
redo:
			addr = ops->map(pipe, buf, atomic);
			error = pipe_iov_copy_to_user(iov, addr + buf->offset, chars, atomic);
			ops->unmap(pipe, buf, addr);
			if (unlikely(error)) {
				/*
				 * Just retry with the slow path if we failed.
				 */
				if (atomic) {
					atomic = 0;
					goto redo;
				}
				if (!ret)
					ret = error;
				break;
			}
			ret += chars;
			buf->offset += chars;
			buf->len -= chars;
			if (!buf->len) {
				buf->ops = NULL;
				ops->release(pipe, buf);
				curbuf = (curbuf + 1) & (PIPE_BUFFERS-1);
				pipe->curbuf = curbuf;
				pipe->nrbufs = --bufs;
				do_wakeup = 1;
			}
			total_len -= chars;
			if (!total_len)
				break;	/* common path: read succeeded */
		}
		if (bufs)	/* More to do? */
			continue;
		if (!pipe->writers)
			break;
		if (!pipe->waiting_writers) {
			/* syscall merging: Usually we must not sleep
			 * if O_NONBLOCK is set, or if we got some data.
			 * But if a writer sleeps in kernel space, then
			 * we can wait for that data without violating POSIX.
			 */
			if (ret)
				break;
			if (filp->f_flags & O_NONBLOCK) {
				ret = -EAGAIN;
				break;
			}
		}
		if (signal_pending(current)) {
			if (!ret)
				ret = -ERESTARTSYS;
			break;
		}
		if (do_wakeup) {
			wake_up_interruptible_sync(&pipe->wait);
 			kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
		}
		pipe_wait(pipe);
	}
	mutex_unlock(&inode->i_mutex);

	/* Signal writers asynchronously that there is more room. */
	if (do_wakeup) {
		wake_up_interruptible_sync(&pipe->wait);
		kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
	}
	if (ret > 0)
		file_accessed(filp);
	return ret;
}

static ssize_t
pipe_write(struct kiocb *iocb, const struct iovec *_iov,
	    unsigned long nr_segs, loff_t ppos)
{
	struct file *filp = iocb->ki_filp;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct pipe_inode_info *pipe;
	ssize_t ret;
	int do_wakeup;
	struct iovec *iov = (struct iovec *)_iov;
	size_t total_len;
	ssize_t chars;

	total_len = iov_length(iov, nr_segs);
	/* Null write succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	do_wakeup = 0;
	ret = 0;
	mutex_lock(&inode->i_mutex);
	pipe = inode->i_pipe;

	if (!pipe->readers) {
		send_sig(SIGPIPE, current, 0);
		ret = -EPIPE;
		goto out;
	}

	/* We try to merge small writes */
	chars = total_len & (PAGE_SIZE-1); /* size of the last buffer */
	if (pipe->nrbufs && chars != 0) {
		int lastbuf = (pipe->curbuf + pipe->nrbufs - 1) &
							(PIPE_BUFFERS-1);
		struct pipe_buffer *buf = pipe->bufs + lastbuf;
		const struct pipe_buf_operations *ops = buf->ops;
		int offset = buf->offset + buf->len;

		if (ops->can_merge && offset + chars <= PAGE_SIZE) {
			int error, atomic = 1;
			void *addr;

			error = ops->confirm(pipe, buf);
			if (error)
				goto out;

			iov_fault_in_pages_read(iov, chars);
redo1:
			addr = ops->map(pipe, buf, atomic);
			error = pipe_iov_copy_from_user(offset + addr, iov,
							chars, atomic);
			ops->unmap(pipe, buf, addr);
			ret = error;
			do_wakeup = 1;
			if (error) {
				if (atomic) {
					atomic = 0;
					goto redo1;
				}
				goto out;
			}
			buf->len += chars;
			total_len -= chars;
			ret = chars;
			if (!total_len)
				goto out;
		}
	}

	for (;;) {
		int bufs;

		if (!pipe->readers) {
			send_sig(SIGPIPE, current, 0);
			if (!ret)
				ret = -EPIPE;
			break;
		}
		bufs = pipe->nrbufs;
		if (bufs < PIPE_BUFFERS) {
			int newbuf = (pipe->curbuf + bufs) & (PIPE_BUFFERS-1);
			struct pipe_buffer *buf = pipe->bufs + newbuf;
			struct page *page = pipe->tmp_page;
			char *src;
			int error, atomic = 1;

			if (!page) {
				page = alloc_page(GFP_HIGHUSER);
				if (unlikely(!page)) {
					ret = ret ? : -ENOMEM;
					break;
				}
				pipe->tmp_page = page;
			}
			/* Always wake up, even if the copy fails. Otherwise
			 * we lock up (O_NONBLOCK-)readers that sleep due to
			 * syscall merging.
			 * FIXME! Is this really true?
			 */
			do_wakeup = 1;
			chars = PAGE_SIZE;
			if (chars > total_len)
				chars = total_len;

			iov_fault_in_pages_read(iov, chars);
redo2:
			if (atomic)
				src = kmap_atomic(page, KM_USER0);
			else
				src = kmap(page);

			error = pipe_iov_copy_from_user(src, iov, chars,
							atomic);
			if (atomic)
				kunmap_atomic(src, KM_USER0);
			else
				kunmap(page);

			if (unlikely(error)) {
				if (atomic) {
					atomic = 0;
					goto redo2;
				}
				if (!ret)
					ret = error;
				break;
			}
			ret += chars;

			/* Insert it into the buffer array */
			buf->page = page;
			buf->ops = &anon_pipe_buf_ops;
			buf->offset = 0;
			buf->len = chars;
			pipe->nrbufs = ++bufs;
			pipe->tmp_page = NULL;

			total_len -= chars;
			if (!total_len)
				break;
		}
		if (bufs < PIPE_BUFFERS)
			continue;
		if (filp->f_flags & O_NONBLOCK) {
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
			wake_up_interruptible_sync(&pipe->wait);
			kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
			do_wakeup = 0;
		}
		pipe->waiting_writers++;
		pipe_wait(pipe);
		pipe->waiting_writers--;
	}
out:
	mutex_unlock(&inode->i_mutex);
	if (do_wakeup) {
		wake_up_interruptible_sync(&pipe->wait);
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	}
	if (ret > 0)
		file_update_time(filp);
	return ret;
}

static ssize_t
bad_pipe_r(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	return -EBADF;
}

static ssize_t
bad_pipe_w(struct file *filp, const char __user *buf, size_t count,
	   loff_t *ppos)
{
	return -EBADF;
}

static long pipe_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct pipe_inode_info *pipe;
	int count, buf, nrbufs;

	switch (cmd) {
		case FIONREAD:
			mutex_lock(&inode->i_mutex);
			pipe = inode->i_pipe;
			count = 0;
			buf = pipe->curbuf;
			nrbufs = pipe->nrbufs;
			while (--nrbufs >= 0) {
				count += pipe->bufs[buf].len;
				buf = (buf+1) & (PIPE_BUFFERS-1);
			}
			mutex_unlock(&inode->i_mutex);

			return put_user(count, (int __user *)arg);
		default:
			return -EINVAL;
	}
}

/* No kernel lock held - fine */
static unsigned int
pipe_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct pipe_inode_info *pipe = inode->i_pipe;
	int nrbufs;

	poll_wait(filp, &pipe->wait, wait);

	/* Reading only -- no need for acquiring the semaphore.  */
	nrbufs = pipe->nrbufs;
	mask = 0;
	if (filp->f_mode & FMODE_READ) {
		mask = (nrbufs > 0) ? POLLIN | POLLRDNORM : 0;
		if (!pipe->writers && filp->f_version != pipe->w_counter)
			mask |= POLLHUP;
	}

	if (filp->f_mode & FMODE_WRITE) {
		mask |= (nrbufs < PIPE_BUFFERS) ? POLLOUT | POLLWRNORM : 0;
		/*
		 * Most Unices do not set POLLERR for FIFOs but on Linux they
		 * behave exactly like pipes for poll().
		 */
		if (!pipe->readers)
			mask |= POLLERR;
	}

	return mask;
}

static int
pipe_release(struct inode *inode, int decr, int decw)
{
	struct pipe_inode_info *pipe;

	mutex_lock(&inode->i_mutex);
	pipe = inode->i_pipe;
	pipe->readers -= decr;
	pipe->writers -= decw;

	if (!pipe->readers && !pipe->writers) {
		free_pipe_info(inode);
	} else {
		wake_up_interruptible_sync(&pipe->wait);
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
		kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
	}
	mutex_unlock(&inode->i_mutex);

	return 0;
}

static int
pipe_read_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	int retval;

	mutex_lock(&inode->i_mutex);
	retval = fasync_helper(fd, filp, on, &inode->i_pipe->fasync_readers);
	mutex_unlock(&inode->i_mutex);

	return retval;
}


static int
pipe_write_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	int retval;

	mutex_lock(&inode->i_mutex);
	retval = fasync_helper(fd, filp, on, &inode->i_pipe->fasync_writers);
	mutex_unlock(&inode->i_mutex);

	return retval;
}


static int
pipe_rdwr_fasync(int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct pipe_inode_info *pipe = inode->i_pipe;
	int retval;

	mutex_lock(&inode->i_mutex);
	retval = fasync_helper(fd, filp, on, &pipe->fasync_readers);
	if (retval >= 0) {
		retval = fasync_helper(fd, filp, on, &pipe->fasync_writers);
		if (retval < 0) /* this can happen only if on == T */
			fasync_helper(-1, filp, 0, &pipe->fasync_readers);
	}
	mutex_unlock(&inode->i_mutex);
	return retval;
}


static int
pipe_read_release(struct inode *inode, struct file *filp)
{
	return pipe_release(inode, 1, 0);
}

static int
pipe_write_release(struct inode *inode, struct file *filp)
{
	return pipe_release(inode, 0, 1);
}

static int
pipe_rdwr_release(struct inode *inode, struct file *filp)
{
	int decr, decw;

	decr = (filp->f_mode & FMODE_READ) != 0;
	decw = (filp->f_mode & FMODE_WRITE) != 0;
	return pipe_release(inode, decr, decw);
}

static int
pipe_read_open(struct inode *inode, struct file *filp)
{
	int ret = -ENOENT;

	mutex_lock(&inode->i_mutex);

	if (inode->i_pipe) {
		ret = 0;
		inode->i_pipe->readers++;
	}

	mutex_unlock(&inode->i_mutex);

	return ret;
}

static int
pipe_write_open(struct inode *inode, struct file *filp)
{
	int ret = -ENOENT;

	mutex_lock(&inode->i_mutex);

	if (inode->i_pipe) {
		ret = 0;
		inode->i_pipe->writers++;
	}

	mutex_unlock(&inode->i_mutex);

	return ret;
}

static int
pipe_rdwr_open(struct inode *inode, struct file *filp)
{
	int ret = -ENOENT;

	mutex_lock(&inode->i_mutex);

	if (inode->i_pipe) {
		ret = 0;
		if (filp->f_mode & FMODE_READ)
			inode->i_pipe->readers++;
		if (filp->f_mode & FMODE_WRITE)
			inode->i_pipe->writers++;
	}

	mutex_unlock(&inode->i_mutex);

	return ret;
}

/*
 * The file_operations structs are not static because they
 * are also used in linux/fs/fifo.c to do operations on FIFOs.
 *
 * Pipes reuse fifos' file_operations structs.
 */
const struct file_operations read_pipefifo_fops = {
	.llseek		= no_llseek,
	.read		= do_sync_read,
	.aio_read	= pipe_read,
	.write		= bad_pipe_w,
	.poll		= pipe_poll,
	.unlocked_ioctl	= pipe_ioctl,
	.open		= pipe_read_open,
	.release	= pipe_read_release,
	.fasync		= pipe_read_fasync,
};

const struct file_operations write_pipefifo_fops = {
	.llseek		= no_llseek,
	.read		= bad_pipe_r,
	.write		= do_sync_write,
	.aio_write	= pipe_write,
	.poll		= pipe_poll,
	.unlocked_ioctl	= pipe_ioctl,
	.open		= pipe_write_open,
	.release	= pipe_write_release,
	.fasync		= pipe_write_fasync,
};

const struct file_operations rdwr_pipefifo_fops = {
	.llseek		= no_llseek,
	.read		= do_sync_read,
	.aio_read	= pipe_read,
	.write		= do_sync_write,
	.aio_write	= pipe_write,
	.poll		= pipe_poll,
	.unlocked_ioctl	= pipe_ioctl,
	.open		= pipe_rdwr_open,
	.release	= pipe_rdwr_release,
	.fasync		= pipe_rdwr_fasync,
};

struct pipe_inode_info * alloc_pipe_info(struct inode *inode)
{
	struct pipe_inode_info *pipe;

	pipe = kzalloc(sizeof(struct pipe_inode_info), GFP_KERNEL);
	if (pipe) {
		init_waitqueue_head(&pipe->wait);
		pipe->r_counter = pipe->w_counter = 1;
		pipe->inode = inode;
	}

	return pipe;
}

void __free_pipe_info(struct pipe_inode_info *pipe)
{
	int i;

	for (i = 0; i < PIPE_BUFFERS; i++) {
		struct pipe_buffer *buf = pipe->bufs + i;
		if (buf->ops)
			buf->ops->release(pipe, buf);
	}
	if (pipe->tmp_page)
		__free_page(pipe->tmp_page);
	kfree(pipe);
}

void free_pipe_info(struct inode *inode)
{
	__free_pipe_info(inode->i_pipe);
	inode->i_pipe = NULL;
}

static struct vfsmount *pipe_mnt __read_mostly;
static int pipefs_delete_dentry(struct dentry *dentry)
{
	/*
	 * At creation time, we pretended this dentry was hashed
	 * (by clearing DCACHE_UNHASHED bit in d_flags)
	 * At delete time, we restore the truth : not hashed.
	 * (so that dput() can proceed correctly)
	 */
	dentry->d_flags |= DCACHE_UNHASHED;
	return 0;
}

/*
 * pipefs_dname() is called from d_path().
 */
static char *pipefs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "pipe:[%lu]",
				dentry->d_inode->i_ino);
}

static const struct dentry_operations pipefs_dentry_operations = {
	.d_delete	= pipefs_delete_dentry,
	.d_dname	= pipefs_dname,
};

static struct inode * get_pipe_inode(void)
{
	struct inode *inode = new_inode(pipe_mnt->mnt_sb);
	struct pipe_inode_info *pipe;

	if (!inode)
		goto fail_inode;

	pipe = alloc_pipe_info(inode);
	if (!pipe)
		goto fail_iput;
	inode->i_pipe = pipe;

	pipe->readers = pipe->writers = 1;
	inode->i_fop = &rdwr_pipefifo_fops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because "mark_inode_dirty()" will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IFIFO | S_IRUSR | S_IWUSR;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	return inode;

fail_iput:
	iput(inode);

fail_inode:
	return NULL;
}

struct file *create_write_pipe(int flags)
{
	int err;
	struct inode *inode;
	struct file *f;
	struct dentry *dentry;
	struct qstr name = { .name = "" };

	err = -ENFILE;
	inode = get_pipe_inode();
	if (!inode)
		goto err;

	err = -ENOMEM;
	dentry = d_alloc(pipe_mnt->mnt_sb->s_root, &name);
	if (!dentry)
		goto err_inode;

	dentry->d_op = &pipefs_dentry_operations;
	/*
	 * We dont want to publish this dentry into global dentry hash table.
	 * We pretend dentry is already hashed, by unsetting DCACHE_UNHASHED
	 * This permits a working /proc/$pid/fd/XXX on pipes
	 */
	dentry->d_flags &= ~DCACHE_UNHASHED;
	d_instantiate(dentry, inode);

	err = -ENFILE;
	f = alloc_file(pipe_mnt, dentry, FMODE_WRITE, &write_pipefifo_fops);
	if (!f)
		goto err_dentry;
	f->f_mapping = inode->i_mapping;

	f->f_flags = O_WRONLY | (flags & O_NONBLOCK);
	f->f_version = 0;

	return f;

 err_dentry:
	free_pipe_info(inode);
	dput(dentry);
	return ERR_PTR(err);

 err_inode:
	free_pipe_info(inode);
	iput(inode);
 err:
	return ERR_PTR(err);
}

void free_write_pipe(struct file *f)
{
	free_pipe_info(f->f_dentry->d_inode);
	path_put(&f->f_path);
	put_filp(f);
}

struct file *create_read_pipe(struct file *wrf, int flags)
{
	struct file *f = get_empty_filp();
	if (!f)
		return ERR_PTR(-ENFILE);

	/* Grab pipe from the writer */
	f->f_path = wrf->f_path;
	path_get(&wrf->f_path);
	f->f_mapping = wrf->f_path.dentry->d_inode->i_mapping;

	f->f_pos = 0;
	f->f_flags = O_RDONLY | (flags & O_NONBLOCK);
	f->f_op = &read_pipefifo_fops;
	f->f_mode = FMODE_READ;
	f->f_version = 0;

	return f;
}

int do_pipe_flags(int *fd, int flags)
{
	struct file *fw, *fr;
	int error;
	int fdw, fdr;

	if (flags & ~(O_CLOEXEC | O_NONBLOCK))
		return -EINVAL;

	fw = create_write_pipe(flags);
	if (IS_ERR(fw))
		return PTR_ERR(fw);
	fr = create_read_pipe(fw, flags);
	error = PTR_ERR(fr);
	if (IS_ERR(fr))
		goto err_write_pipe;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		goto err_read_pipe;
	fdr = error;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		goto err_fdr;
	fdw = error;

	audit_fd_pair(fdr, fdw);
	fd_install(fdr, fr);
	fd_install(fdw, fw);
	fd[0] = fdr;
	fd[1] = fdw;

	return 0;

 err_fdr:
	put_unused_fd(fdr);
 err_read_pipe:
	path_put(&fr->f_path);
	put_filp(fr);
 err_write_pipe:
	free_write_pipe(fw);
	return error;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags)
{
	int fd[2];
	int error;

	error = do_pipe_flags(fd, flags);
	if (!error) {
		if (copy_to_user(fildes, fd, sizeof(fd))) {
			sys_close(fd[0]);
			sys_close(fd[1]);
			error = -EFAULT;
		}
	}
	return error;
}

SYSCALL_DEFINE1(pipe, int __user *, fildes)
{
	return sys_pipe2(fildes, 0);
}

/*
 * pipefs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pipe: will go nicely and kill the special-casing in procfs.
 */
static int pipefs_get_sb(struct file_system_type *fs_type,
			 int flags, const char *dev_name, void *data,
			 struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "pipe:", NULL, PIPEFS_MAGIC, mnt);
}

static struct file_system_type pipe_fs_type = {
	.name		= "pipefs",
	.get_sb		= pipefs_get_sb,
	.kill_sb	= kill_anon_super,
};

static int __init init_pipe_fs(void)
{
	int err = register_filesystem(&pipe_fs_type);

	if (!err) {
		pipe_mnt = kern_mount(&pipe_fs_type);
		if (IS_ERR(pipe_mnt)) {
			err = PTR_ERR(pipe_mnt);
			unregister_filesystem(&pipe_fs_type);
		}
	}
	return err;
}

static void __exit exit_pipe_fs(void)
{
	unregister_filesystem(&pipe_fs_type);
	mntput(pipe_mnt);
}

fs_initcall(init_pipe_fs);
module_exit(exit_pipe_fs);
