// SPDX-License-Identifier: GPL-2.0
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
#include <linux/log2.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/magic.h>
#include <linux/pipe_fs_i.h>
#include <linux/uio.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/audit.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/memcontrol.h>
#include <linux/watch_queue.h>
#include <linux/sysctl.h>

#include <linux/uaccess.h>
#include <asm/ioctls.h>

#include "internal.h"

/*
 * New pipe buffers will be restricted to this size while the user is exceeding
 * their pipe buffer quota. The general pipe use case needs at least two
 * buffers: one for data yet to be read, and one for new data. If this is less
 * than two, then a write to a non-empty pipe may block even if the pipe is not
 * full. This can occur with GNU make jobserver or similar uses of pipes as
 * semaphores: multiple processes may be waiting to write tokens back to the
 * pipe before reading tokens: https://lore.kernel.org/lkml/1628086770.5rn8p04n6j.none@localhost/.
 *
 * Users can reduce their pipe buffers with F_SETPIPE_SZ below this at their
 * own risk, namely: pipe writes to non-full pipes may block until the pipe is
 * emptied.
 */
#define PIPE_MIN_DEF_BUFFERS 2

/*
 * The max size that a non-root user is allowed to grow the pipe. Can
 * be set by root in /proc/sys/fs/pipe-max-size
 */
static unsigned int pipe_max_size = 1048576;

/* Maximum allocatable pages per user. Hard limit is unset by default, soft
 * matches default values.
 */
static unsigned long pipe_user_pages_hard;
static unsigned long pipe_user_pages_soft = PIPE_DEF_BUFFERS * INR_OPEN_CUR;

/*
 * We use head and tail indices that aren't masked off, except at the point of
 * dereference, but rather they're allowed to wrap naturally.  This means there
 * isn't a dead spot in the buffer, but the ring has to be a power of two and
 * <= 2^31.
 * -- David Howells 2019-09-23.
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
	if (pipe->files)
		mutex_lock_nested(&pipe->mutex, subclass);
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
	if (pipe->files)
		mutex_unlock(&pipe->mutex);
}
EXPORT_SYMBOL(pipe_unlock);

static inline void __pipe_lock(struct pipe_inode_info *pipe)
{
	mutex_lock_nested(&pipe->mutex, I_MUTEX_PARENT);
}

static inline void __pipe_unlock(struct pipe_inode_info *pipe)
{
	mutex_unlock(&pipe->mutex);
}

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
		put_page(page);
}

static bool anon_pipe_buf_try_steal(struct pipe_inode_info *pipe,
		struct pipe_buffer *buf)
{
	struct page *page = buf->page;

	if (page_count(page) != 1)
		return false;
	memcg_kmem_uncharge_page(page, 0);
	__SetPageLocked(page);
	return true;
}

/**
 * generic_pipe_buf_try_steal - attempt to take ownership of a &pipe_buffer
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
bool generic_pipe_buf_try_steal(struct pipe_inode_info *pipe,
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
		return true;
	}
	return false;
}
EXPORT_SYMBOL(generic_pipe_buf_try_steal);

/**
 * generic_pipe_buf_get - get a reference to a &struct pipe_buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to get a reference to
 *
 * Description:
 *	This function grabs an extra reference to @buf. It's used in
 *	the tee() system call, when we duplicate the buffers in one
 *	pipe into another.
 */
bool generic_pipe_buf_get(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
{
	return try_get_page(buf->page);
}
EXPORT_SYMBOL(generic_pipe_buf_get);

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
	put_page(buf->page);
}
EXPORT_SYMBOL(generic_pipe_buf_release);

static const struct pipe_buf_operations anon_pipe_buf_ops = {
	.release	= anon_pipe_buf_release,
	.try_steal	= anon_pipe_buf_try_steal,
	.get		= generic_pipe_buf_get,
};

/* Done while waiting without holding the pipe lock - thus the READ_ONCE() */
static inline bool pipe_readable(const struct pipe_inode_info *pipe)
{
	unsigned int head = READ_ONCE(pipe->head);
	unsigned int tail = READ_ONCE(pipe->tail);
	unsigned int writers = READ_ONCE(pipe->writers);

	return !pipe_empty(head, tail) || !writers;
}

static ssize_t
pipe_read(struct kiocb *iocb, struct iov_iter *to)
{
	size_t total_len = iov_iter_count(to);
	struct file *filp = iocb->ki_filp;
	struct pipe_inode_info *pipe = filp->private_data;
	bool was_full, wake_next_reader = false;
	ssize_t ret;

	/* Null read succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	ret = 0;
	__pipe_lock(pipe);

	/*
	 * We only wake up writers if the pipe was full when we started
	 * reading in order to avoid unnecessary wakeups.
	 *
	 * But when we do wake up writers, we do so using a sync wakeup
	 * (WF_SYNC), because we want them to get going and generate more
	 * data for us.
	 */
	was_full = pipe_full(pipe->head, pipe->tail, pipe->max_usage);
	for (;;) {
		/* Read ->head with a barrier vs post_one_notification() */
		unsigned int head = smp_load_acquire(&pipe->head);
		unsigned int tail = pipe->tail;
		unsigned int mask = pipe->ring_size - 1;

#ifdef CONFIG_WATCH_QUEUE
		if (pipe->note_loss) {
			struct watch_notification n;

			if (total_len < 8) {
				if (ret == 0)
					ret = -ENOBUFS;
				break;
			}

			n.type = WATCH_TYPE_META;
			n.subtype = WATCH_META_LOSS_NOTIFICATION;
			n.info = watch_sizeof(n);
			if (copy_to_iter(&n, sizeof(n), to) != sizeof(n)) {
				if (ret == 0)
					ret = -EFAULT;
				break;
			}
			ret += sizeof(n);
			total_len -= sizeof(n);
			pipe->note_loss = false;
		}
#endif

		if (!pipe_empty(head, tail)) {
			struct pipe_buffer *buf = &pipe->bufs[tail & mask];
			size_t chars = buf->len;
			size_t written;
			int error;

			if (chars > total_len) {
				if (buf->flags & PIPE_BUF_FLAG_WHOLE) {
					if (ret == 0)
						ret = -ENOBUFS;
					break;
				}
				chars = total_len;
			}

			error = pipe_buf_confirm(pipe, buf);
			if (error) {
				if (!ret)
					ret = error;
				break;
			}

			written = copy_page_to_iter(buf->page, buf->offset, chars, to);
			if (unlikely(written < chars)) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			ret += chars;
			buf->offset += chars;
			buf->len -= chars;

			/* Was it a packet buffer? Clean up and exit */
			if (buf->flags & PIPE_BUF_FLAG_PACKET) {
				total_len = chars;
				buf->len = 0;
			}

			if (!buf->len) {
				pipe_buf_release(pipe, buf);
				spin_lock_irq(&pipe->rd_wait.lock);
#ifdef CONFIG_WATCH_QUEUE
				if (buf->flags & PIPE_BUF_FLAG_LOSS)
					pipe->note_loss = true;
#endif
				tail++;
				pipe->tail = tail;
				spin_unlock_irq(&pipe->rd_wait.lock);
			}
			total_len -= chars;
			if (!total_len)
				break;	/* common path: read succeeded */
			if (!pipe_empty(head, tail))	/* More to do? */
				continue;
		}

		if (!pipe->writers)
			break;
		if (ret)
			break;
		if ((filp->f_flags & O_NONBLOCK) ||
		    (iocb->ki_flags & IOCB_NOWAIT)) {
			ret = -EAGAIN;
			break;
		}
		__pipe_unlock(pipe);

		/*
		 * We only get here if we didn't actually read anything.
		 *
		 * However, we could have seen (and removed) a zero-sized
		 * pipe buffer, and might have made space in the buffers
		 * that way.
		 *
		 * You can't make zero-sized pipe buffers by doing an empty
		 * write (not even in packet mode), but they can happen if
		 * the writer gets an EFAULT when trying to fill a buffer
		 * that already got allocated and inserted in the buffer
		 * array.
		 *
		 * So we still need to wake up any pending writers in the
		 * _very_ unlikely case that the pipe was full, but we got
		 * no data.
		 */
		if (unlikely(was_full))
			wake_up_interruptible_sync_poll(&pipe->wr_wait, EPOLLOUT | EPOLLWRNORM);
		kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);

		/*
		 * But because we didn't read anything, at this point we can
		 * just return directly with -ERESTARTSYS if we're interrupted,
		 * since we've done any required wakeups and there's no need
		 * to mark anything accessed. And we've dropped the lock.
		 */
		if (wait_event_interruptible_exclusive(pipe->rd_wait, pipe_readable(pipe)) < 0)
			return -ERESTARTSYS;

		__pipe_lock(pipe);
		was_full = pipe_full(pipe->head, pipe->tail, pipe->max_usage);
		wake_next_reader = true;
	}
	if (pipe_empty(pipe->head, pipe->tail))
		wake_next_reader = false;
	__pipe_unlock(pipe);

	if (was_full)
		wake_up_interruptible_sync_poll(&pipe->wr_wait, EPOLLOUT | EPOLLWRNORM);
	if (wake_next_reader)
		wake_up_interruptible_sync_poll(&pipe->rd_wait, EPOLLIN | EPOLLRDNORM);
	kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
	if (ret > 0)
		file_accessed(filp);
	return ret;
}

static inline int is_packetized(struct file *file)
{
	return (file->f_flags & O_DIRECT) != 0;
}

/* Done while waiting without holding the pipe lock - thus the READ_ONCE() */
static inline bool pipe_writable(const struct pipe_inode_info *pipe)
{
	unsigned int head = READ_ONCE(pipe->head);
	unsigned int tail = READ_ONCE(pipe->tail);
	unsigned int max_usage = READ_ONCE(pipe->max_usage);

	return !pipe_full(head, tail, max_usage) ||
		!READ_ONCE(pipe->readers);
}

static ssize_t
pipe_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *filp = iocb->ki_filp;
	struct pipe_inode_info *pipe = filp->private_data;
	unsigned int head;
	ssize_t ret = 0;
	size_t total_len = iov_iter_count(from);
	ssize_t chars;
	bool was_empty = false;
	bool wake_next_writer = false;

	/* Null write succeeds. */
	if (unlikely(total_len == 0))
		return 0;

	__pipe_lock(pipe);

	if (!pipe->readers) {
		send_sig(SIGPIPE, current, 0);
		ret = -EPIPE;
		goto out;
	}

	if (pipe_has_watch_queue(pipe)) {
		ret = -EXDEV;
		goto out;
	}

	/*
	 * If it wasn't empty we try to merge new data into
	 * the last buffer.
	 *
	 * That naturally merges small writes, but it also
	 * page-aligns the rest of the writes for large writes
	 * spanning multiple pages.
	 */
	head = pipe->head;
	was_empty = pipe_empty(head, pipe->tail);
	chars = total_len & (PAGE_SIZE-1);
	if (chars && !was_empty) {
		unsigned int mask = pipe->ring_size - 1;
		struct pipe_buffer *buf = &pipe->bufs[(head - 1) & mask];
		int offset = buf->offset + buf->len;

		if ((buf->flags & PIPE_BUF_FLAG_CAN_MERGE) &&
		    offset + chars <= PAGE_SIZE) {
			ret = pipe_buf_confirm(pipe, buf);
			if (ret)
				goto out;

			ret = copy_page_from_iter(buf->page, offset, chars, from);
			if (unlikely(ret < chars)) {
				ret = -EFAULT;
				goto out;
			}

			buf->len += ret;
			if (!iov_iter_count(from))
				goto out;
		}
	}

	for (;;) {
		if (!pipe->readers) {
			send_sig(SIGPIPE, current, 0);
			if (!ret)
				ret = -EPIPE;
			break;
		}

		head = pipe->head;
		if (!pipe_full(head, pipe->tail, pipe->max_usage)) {
			unsigned int mask = pipe->ring_size - 1;
			struct pipe_buffer *buf;
			struct page *page = pipe->tmp_page;
			int copied;

			if (!page) {
				page = alloc_page(GFP_HIGHUSER | __GFP_ACCOUNT);
				if (unlikely(!page)) {
					ret = ret ? : -ENOMEM;
					break;
				}
				pipe->tmp_page = page;
			}

			/* Allocate a slot in the ring in advance and attach an
			 * empty buffer.  If we fault or otherwise fail to use
			 * it, either the reader will consume it or it'll still
			 * be there for the next write.
			 */
			spin_lock_irq(&pipe->rd_wait.lock);

			head = pipe->head;
			if (pipe_full(head, pipe->tail, pipe->max_usage)) {
				spin_unlock_irq(&pipe->rd_wait.lock);
				continue;
			}

			pipe->head = head + 1;
			spin_unlock_irq(&pipe->rd_wait.lock);

			/* Insert it into the buffer array */
			buf = &pipe->bufs[head & mask];
			buf->page = page;
			buf->ops = &anon_pipe_buf_ops;
			buf->offset = 0;
			buf->len = 0;
			if (is_packetized(filp))
				buf->flags = PIPE_BUF_FLAG_PACKET;
			else
				buf->flags = PIPE_BUF_FLAG_CAN_MERGE;
			pipe->tmp_page = NULL;

			copied = copy_page_from_iter(page, 0, PAGE_SIZE, from);
			if (unlikely(copied < PAGE_SIZE && iov_iter_count(from))) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			ret += copied;
			buf->len = copied;

			if (!iov_iter_count(from))
				break;
		}

		if (!pipe_full(head, pipe->tail, pipe->max_usage))
			continue;

		/* Wait for buffer space to become available. */
		if ((filp->f_flags & O_NONBLOCK) ||
		    (iocb->ki_flags & IOCB_NOWAIT)) {
			if (!ret)
				ret = -EAGAIN;
			break;
		}
		if (signal_pending(current)) {
			if (!ret)
				ret = -ERESTARTSYS;
			break;
		}

		/*
		 * We're going to release the pipe lock and wait for more
		 * space. We wake up any readers if necessary, and then
		 * after waiting we need to re-check whether the pipe
		 * become empty while we dropped the lock.
		 */
		__pipe_unlock(pipe);
		if (was_empty)
			wake_up_interruptible_sync_poll(&pipe->rd_wait, EPOLLIN | EPOLLRDNORM);
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
		wait_event_interruptible_exclusive(pipe->wr_wait, pipe_writable(pipe));
		__pipe_lock(pipe);
		was_empty = pipe_empty(pipe->head, pipe->tail);
		wake_next_writer = true;
	}
out:
	if (pipe_full(pipe->head, pipe->tail, pipe->max_usage))
		wake_next_writer = false;
	__pipe_unlock(pipe);

	/*
	 * If we do do a wakeup event, we do a 'sync' wakeup, because we
	 * want the reader to start processing things asap, rather than
	 * leave the data pending.
	 *
	 * This is particularly important for small writes, because of
	 * how (for example) the GNU make jobserver uses small writes to
	 * wake up pending jobs
	 *
	 * Epoll nonsensically wants a wakeup whether the pipe
	 * was already empty or not.
	 */
	if (was_empty || pipe->poll_usage)
		wake_up_interruptible_sync_poll(&pipe->rd_wait, EPOLLIN | EPOLLRDNORM);
	kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	if (wake_next_writer)
		wake_up_interruptible_sync_poll(&pipe->wr_wait, EPOLLOUT | EPOLLWRNORM);
	if (ret > 0 && sb_start_write_trylock(file_inode(filp)->i_sb)) {
		int err = file_update_time(filp);
		if (err)
			ret = err;
		sb_end_write(file_inode(filp)->i_sb);
	}
	return ret;
}

static long pipe_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct pipe_inode_info *pipe = filp->private_data;
	unsigned int count, head, tail, mask;

	switch (cmd) {
	case FIONREAD:
		__pipe_lock(pipe);
		count = 0;
		head = pipe->head;
		tail = pipe->tail;
		mask = pipe->ring_size - 1;

		while (tail != head) {
			count += pipe->bufs[tail & mask].len;
			tail++;
		}
		__pipe_unlock(pipe);

		return put_user(count, (int __user *)arg);

#ifdef CONFIG_WATCH_QUEUE
	case IOC_WATCH_QUEUE_SET_SIZE: {
		int ret;
		__pipe_lock(pipe);
		ret = watch_queue_set_size(pipe, arg);
		__pipe_unlock(pipe);
		return ret;
	}

	case IOC_WATCH_QUEUE_SET_FILTER:
		return watch_queue_set_filter(
			pipe, (struct watch_notification_filter __user *)arg);
#endif

	default:
		return -ENOIOCTLCMD;
	}
}

/* No kernel lock held - fine */
static __poll_t
pipe_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask;
	struct pipe_inode_info *pipe = filp->private_data;
	unsigned int head, tail;

	/* Epoll has some historical nasty semantics, this enables them */
	WRITE_ONCE(pipe->poll_usage, true);

	/*
	 * Reading pipe state only -- no need for acquiring the semaphore.
	 *
	 * But because this is racy, the code has to add the
	 * entry to the poll table _first_ ..
	 */
	if (filp->f_mode & FMODE_READ)
		poll_wait(filp, &pipe->rd_wait, wait);
	if (filp->f_mode & FMODE_WRITE)
		poll_wait(filp, &pipe->wr_wait, wait);

	/*
	 * .. and only then can you do the racy tests. That way,
	 * if something changes and you got it wrong, the poll
	 * table entry will wake you up and fix it.
	 */
	head = READ_ONCE(pipe->head);
	tail = READ_ONCE(pipe->tail);

	mask = 0;
	if (filp->f_mode & FMODE_READ) {
		if (!pipe_empty(head, tail))
			mask |= EPOLLIN | EPOLLRDNORM;
		if (!pipe->writers && filp->f_version != pipe->w_counter)
			mask |= EPOLLHUP;
	}

	if (filp->f_mode & FMODE_WRITE) {
		if (!pipe_full(head, tail, pipe->max_usage))
			mask |= EPOLLOUT | EPOLLWRNORM;
		/*
		 * Most Unices do not set EPOLLERR for FIFOs but on Linux they
		 * behave exactly like pipes for poll().
		 */
		if (!pipe->readers)
			mask |= EPOLLERR;
	}

	return mask;
}

static void put_pipe_info(struct inode *inode, struct pipe_inode_info *pipe)
{
	int kill = 0;

	spin_lock(&inode->i_lock);
	if (!--pipe->files) {
		inode->i_pipe = NULL;
		kill = 1;
	}
	spin_unlock(&inode->i_lock);

	if (kill)
		free_pipe_info(pipe);
}

static int
pipe_release(struct inode *inode, struct file *file)
{
	struct pipe_inode_info *pipe = file->private_data;

	__pipe_lock(pipe);
	if (file->f_mode & FMODE_READ)
		pipe->readers--;
	if (file->f_mode & FMODE_WRITE)
		pipe->writers--;

	/* Was that the last reader or writer, but not the other side? */
	if (!pipe->readers != !pipe->writers) {
		wake_up_interruptible_all(&pipe->rd_wait);
		wake_up_interruptible_all(&pipe->wr_wait);
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
		kill_fasync(&pipe->fasync_writers, SIGIO, POLL_OUT);
	}
	__pipe_unlock(pipe);

	put_pipe_info(inode, pipe);
	return 0;
}

static int
pipe_fasync(int fd, struct file *filp, int on)
{
	struct pipe_inode_info *pipe = filp->private_data;
	int retval = 0;

	__pipe_lock(pipe);
	if (filp->f_mode & FMODE_READ)
		retval = fasync_helper(fd, filp, on, &pipe->fasync_readers);
	if ((filp->f_mode & FMODE_WRITE) && retval >= 0) {
		retval = fasync_helper(fd, filp, on, &pipe->fasync_writers);
		if (retval < 0 && (filp->f_mode & FMODE_READ))
			/* this can happen only if on == T */
			fasync_helper(-1, filp, 0, &pipe->fasync_readers);
	}
	__pipe_unlock(pipe);
	return retval;
}

unsigned long account_pipe_buffers(struct user_struct *user,
				   unsigned long old, unsigned long new)
{
	return atomic_long_add_return(new - old, &user->pipe_bufs);
}

bool too_many_pipe_buffers_soft(unsigned long user_bufs)
{
	unsigned long soft_limit = READ_ONCE(pipe_user_pages_soft);

	return soft_limit && user_bufs > soft_limit;
}

bool too_many_pipe_buffers_hard(unsigned long user_bufs)
{
	unsigned long hard_limit = READ_ONCE(pipe_user_pages_hard);

	return hard_limit && user_bufs > hard_limit;
}

bool pipe_is_unprivileged_user(void)
{
	return !capable(CAP_SYS_RESOURCE) && !capable(CAP_SYS_ADMIN);
}

struct pipe_inode_info *alloc_pipe_info(void)
{
	struct pipe_inode_info *pipe;
	unsigned long pipe_bufs = PIPE_DEF_BUFFERS;
	struct user_struct *user = get_current_user();
	unsigned long user_bufs;
	unsigned int max_size = READ_ONCE(pipe_max_size);

	pipe = kzalloc(sizeof(struct pipe_inode_info), GFP_KERNEL_ACCOUNT);
	if (pipe == NULL)
		goto out_free_uid;

	if (pipe_bufs * PAGE_SIZE > max_size && !capable(CAP_SYS_RESOURCE))
		pipe_bufs = max_size >> PAGE_SHIFT;

	user_bufs = account_pipe_buffers(user, 0, pipe_bufs);

	if (too_many_pipe_buffers_soft(user_bufs) && pipe_is_unprivileged_user()) {
		user_bufs = account_pipe_buffers(user, pipe_bufs, PIPE_MIN_DEF_BUFFERS);
		pipe_bufs = PIPE_MIN_DEF_BUFFERS;
	}

	if (too_many_pipe_buffers_hard(user_bufs) && pipe_is_unprivileged_user())
		goto out_revert_acct;

	pipe->bufs = kcalloc(pipe_bufs, sizeof(struct pipe_buffer),
			     GFP_KERNEL_ACCOUNT);

	if (pipe->bufs) {
		init_waitqueue_head(&pipe->rd_wait);
		init_waitqueue_head(&pipe->wr_wait);
		pipe->r_counter = pipe->w_counter = 1;
		pipe->max_usage = pipe_bufs;
		pipe->ring_size = pipe_bufs;
		pipe->nr_accounted = pipe_bufs;
		pipe->user = user;
		mutex_init(&pipe->mutex);
		return pipe;
	}

out_revert_acct:
	(void) account_pipe_buffers(user, pipe_bufs, 0);
	kfree(pipe);
out_free_uid:
	free_uid(user);
	return NULL;
}

void free_pipe_info(struct pipe_inode_info *pipe)
{
	unsigned int i;

#ifdef CONFIG_WATCH_QUEUE
	if (pipe->watch_queue)
		watch_queue_clear(pipe->watch_queue);
#endif

	(void) account_pipe_buffers(pipe->user, pipe->nr_accounted, 0);
	free_uid(pipe->user);
	for (i = 0; i < pipe->ring_size; i++) {
		struct pipe_buffer *buf = pipe->bufs + i;
		if (buf->ops)
			pipe_buf_release(pipe, buf);
	}
#ifdef CONFIG_WATCH_QUEUE
	if (pipe->watch_queue)
		put_watch_queue(pipe->watch_queue);
#endif
	if (pipe->tmp_page)
		__free_page(pipe->tmp_page);
	kfree(pipe->bufs);
	kfree(pipe);
}

static struct vfsmount *pipe_mnt __read_mostly;

/*
 * pipefs_dname() is called from d_path().
 */
static char *pipefs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(buffer, buflen, "pipe:[%lu]",
				d_inode(dentry)->i_ino);
}

static const struct dentry_operations pipefs_dentry_operations = {
	.d_dname	= pipefs_dname,
};

static struct inode * get_pipe_inode(void)
{
	struct inode *inode = new_inode_pseudo(pipe_mnt->mnt_sb);
	struct pipe_inode_info *pipe;

	if (!inode)
		goto fail_inode;

	inode->i_ino = get_next_ino();

	pipe = alloc_pipe_info();
	if (!pipe)
		goto fail_iput;

	inode->i_pipe = pipe;
	pipe->files = 2;
	pipe->readers = pipe->writers = 1;
	inode->i_fop = &pipefifo_fops;

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
	inode->i_atime = inode->i_mtime = inode_set_ctime_current(inode);

	return inode;

fail_iput:
	iput(inode);

fail_inode:
	return NULL;
}

int create_pipe_files(struct file **res, int flags)
{
	struct inode *inode = get_pipe_inode();
	struct file *f;
	int error;

	if (!inode)
		return -ENFILE;

	if (flags & O_NOTIFICATION_PIPE) {
		error = watch_queue_init(inode->i_pipe);
		if (error) {
			free_pipe_info(inode->i_pipe);
			iput(inode);
			return error;
		}
	}

	f = alloc_file_pseudo(inode, pipe_mnt, "",
				O_WRONLY | (flags & (O_NONBLOCK | O_DIRECT)),
				&pipefifo_fops);
	if (IS_ERR(f)) {
		free_pipe_info(inode->i_pipe);
		iput(inode);
		return PTR_ERR(f);
	}

	f->private_data = inode->i_pipe;

	res[0] = alloc_file_clone(f, O_RDONLY | (flags & O_NONBLOCK),
				  &pipefifo_fops);
	if (IS_ERR(res[0])) {
		put_pipe_info(inode, inode->i_pipe);
		fput(f);
		return PTR_ERR(res[0]);
	}
	res[0]->private_data = inode->i_pipe;
	res[1] = f;
	stream_open(inode, res[0]);
	stream_open(inode, res[1]);
	return 0;
}

static int __do_pipe_flags(int *fd, struct file **files, int flags)
{
	int error;
	int fdw, fdr;

	if (flags & ~(O_CLOEXEC | O_NONBLOCK | O_DIRECT | O_NOTIFICATION_PIPE))
		return -EINVAL;

	error = create_pipe_files(files, flags);
	if (error)
		return error;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		goto err_read_pipe;
	fdr = error;

	error = get_unused_fd_flags(flags);
	if (error < 0)
		goto err_fdr;
	fdw = error;

	audit_fd_pair(fdr, fdw);
	fd[0] = fdr;
	fd[1] = fdw;
	/* pipe groks IOCB_NOWAIT */
	files[0]->f_mode |= FMODE_NOWAIT;
	files[1]->f_mode |= FMODE_NOWAIT;
	return 0;

 err_fdr:
	put_unused_fd(fdr);
 err_read_pipe:
	fput(files[0]);
	fput(files[1]);
	return error;
}

int do_pipe_flags(int *fd, int flags)
{
	struct file *files[2];
	int error = __do_pipe_flags(fd, files, flags);
	if (!error) {
		fd_install(fd[0], files[0]);
		fd_install(fd[1], files[1]);
	}
	return error;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
static int do_pipe2(int __user *fildes, int flags)
{
	struct file *files[2];
	int fd[2];
	int error;

	error = __do_pipe_flags(fd, files, flags);
	if (!error) {
		if (unlikely(copy_to_user(fildes, fd, sizeof(fd)))) {
			fput(files[0]);
			fput(files[1]);
			put_unused_fd(fd[0]);
			put_unused_fd(fd[1]);
			error = -EFAULT;
		} else {
			fd_install(fd[0], files[0]);
			fd_install(fd[1], files[1]);
		}
	}
	return error;
}

SYSCALL_DEFINE2(pipe2, int __user *, fildes, int, flags)
{
	return do_pipe2(fildes, flags);
}

SYSCALL_DEFINE1(pipe, int __user *, fildes)
{
	return do_pipe2(fildes, 0);
}

/*
 * This is the stupid "wait for pipe to be readable or writable"
 * model.
 *
 * See pipe_read/write() for the proper kind of exclusive wait,
 * but that requires that we wake up any other readers/writers
 * if we then do not end up reading everything (ie the whole
 * "wake_next_reader/writer" logic in pipe_read/write()).
 */
void pipe_wait_readable(struct pipe_inode_info *pipe)
{
	pipe_unlock(pipe);
	wait_event_interruptible(pipe->rd_wait, pipe_readable(pipe));
	pipe_lock(pipe);
}

void pipe_wait_writable(struct pipe_inode_info *pipe)
{
	pipe_unlock(pipe);
	wait_event_interruptible(pipe->wr_wait, pipe_writable(pipe));
	pipe_lock(pipe);
}

/*
 * This depends on both the wait (here) and the wakeup (wake_up_partner)
 * holding the pipe lock, so "*cnt" is stable and we know a wakeup cannot
 * race with the count check and waitqueue prep.
 *
 * Normally in order to avoid races, you'd do the prepare_to_wait() first,
 * then check the condition you're waiting for, and only then sleep. But
 * because of the pipe lock, we can check the condition before being on
 * the wait queue.
 *
 * We use the 'rd_wait' waitqueue for pipe partner waiting.
 */
static int wait_for_partner(struct pipe_inode_info *pipe, unsigned int *cnt)
{
	DEFINE_WAIT(rdwait);
	int cur = *cnt;

	while (cur == *cnt) {
		prepare_to_wait(&pipe->rd_wait, &rdwait, TASK_INTERRUPTIBLE);
		pipe_unlock(pipe);
		schedule();
		finish_wait(&pipe->rd_wait, &rdwait);
		pipe_lock(pipe);
		if (signal_pending(current))
			break;
	}
	return cur == *cnt ? -ERESTARTSYS : 0;
}

static void wake_up_partner(struct pipe_inode_info *pipe)
{
	wake_up_interruptible_all(&pipe->rd_wait);
}

static int fifo_open(struct inode *inode, struct file *filp)
{
	struct pipe_inode_info *pipe;
	bool is_pipe = inode->i_sb->s_magic == PIPEFS_MAGIC;
	int ret;

	filp->f_version = 0;

	spin_lock(&inode->i_lock);
	if (inode->i_pipe) {
		pipe = inode->i_pipe;
		pipe->files++;
		spin_unlock(&inode->i_lock);
	} else {
		spin_unlock(&inode->i_lock);
		pipe = alloc_pipe_info();
		if (!pipe)
			return -ENOMEM;
		pipe->files = 1;
		spin_lock(&inode->i_lock);
		if (unlikely(inode->i_pipe)) {
			inode->i_pipe->files++;
			spin_unlock(&inode->i_lock);
			free_pipe_info(pipe);
			pipe = inode->i_pipe;
		} else {
			inode->i_pipe = pipe;
			spin_unlock(&inode->i_lock);
		}
	}
	filp->private_data = pipe;
	/* OK, we have a pipe and it's pinned down */

	__pipe_lock(pipe);

	/* We can only do regular read/write on fifos */
	stream_open(inode, filp);

	switch (filp->f_mode & (FMODE_READ | FMODE_WRITE)) {
	case FMODE_READ:
	/*
	 *  O_RDONLY
	 *  POSIX.1 says that O_NONBLOCK means return with the FIFO
	 *  opened, even when there is no process writing the FIFO.
	 */
		pipe->r_counter++;
		if (pipe->readers++ == 0)
			wake_up_partner(pipe);

		if (!is_pipe && !pipe->writers) {
			if ((filp->f_flags & O_NONBLOCK)) {
				/* suppress EPOLLHUP until we have
				 * seen a writer */
				filp->f_version = pipe->w_counter;
			} else {
				if (wait_for_partner(pipe, &pipe->w_counter))
					goto err_rd;
			}
		}
		break;

	case FMODE_WRITE:
	/*
	 *  O_WRONLY
	 *  POSIX.1 says that O_NONBLOCK means return -1 with
	 *  errno=ENXIO when there is no process reading the FIFO.
	 */
		ret = -ENXIO;
		if (!is_pipe && (filp->f_flags & O_NONBLOCK) && !pipe->readers)
			goto err;

		pipe->w_counter++;
		if (!pipe->writers++)
			wake_up_partner(pipe);

		if (!is_pipe && !pipe->readers) {
			if (wait_for_partner(pipe, &pipe->r_counter))
				goto err_wr;
		}
		break;

	case FMODE_READ | FMODE_WRITE:
	/*
	 *  O_RDWR
	 *  POSIX.1 leaves this case "undefined" when O_NONBLOCK is set.
	 *  This implementation will NEVER block on a O_RDWR open, since
	 *  the process can at least talk to itself.
	 */

		pipe->readers++;
		pipe->writers++;
		pipe->r_counter++;
		pipe->w_counter++;
		if (pipe->readers == 1 || pipe->writers == 1)
			wake_up_partner(pipe);
		break;

	default:
		ret = -EINVAL;
		goto err;
	}

	/* Ok! */
	__pipe_unlock(pipe);
	return 0;

err_rd:
	if (!--pipe->readers)
		wake_up_interruptible(&pipe->wr_wait);
	ret = -ERESTARTSYS;
	goto err;

err_wr:
	if (!--pipe->writers)
		wake_up_interruptible_all(&pipe->rd_wait);
	ret = -ERESTARTSYS;
	goto err;

err:
	__pipe_unlock(pipe);

	put_pipe_info(inode, pipe);
	return ret;
}

const struct file_operations pipefifo_fops = {
	.open		= fifo_open,
	.llseek		= no_llseek,
	.read_iter	= pipe_read,
	.write_iter	= pipe_write,
	.poll		= pipe_poll,
	.unlocked_ioctl	= pipe_ioctl,
	.release	= pipe_release,
	.fasync		= pipe_fasync,
	.splice_write	= iter_file_splice_write,
};

/*
 * Currently we rely on the pipe array holding a power-of-2 number
 * of pages. Returns 0 on error.
 */
unsigned int round_pipe_size(unsigned int size)
{
	if (size > (1U << 31))
		return 0;

	/* Minimum pipe size, as required by POSIX */
	if (size < PAGE_SIZE)
		return PAGE_SIZE;

	return roundup_pow_of_two(size);
}

/*
 * Resize the pipe ring to a number of slots.
 *
 * Note the pipe can be reduced in capacity, but only if the current
 * occupancy doesn't exceed nr_slots; if it does, EBUSY will be
 * returned instead.
 */
int pipe_resize_ring(struct pipe_inode_info *pipe, unsigned int nr_slots)
{
	struct pipe_buffer *bufs;
	unsigned int head, tail, mask, n;

	bufs = kcalloc(nr_slots, sizeof(*bufs),
		       GFP_KERNEL_ACCOUNT | __GFP_NOWARN);
	if (unlikely(!bufs))
		return -ENOMEM;

	spin_lock_irq(&pipe->rd_wait.lock);
	mask = pipe->ring_size - 1;
	head = pipe->head;
	tail = pipe->tail;

	n = pipe_occupancy(head, tail);
	if (nr_slots < n) {
		spin_unlock_irq(&pipe->rd_wait.lock);
		kfree(bufs);
		return -EBUSY;
	}

	/*
	 * The pipe array wraps around, so just start the new one at zero
	 * and adjust the indices.
	 */
	if (n > 0) {
		unsigned int h = head & mask;
		unsigned int t = tail & mask;
		if (h > t) {
			memcpy(bufs, pipe->bufs + t,
			       n * sizeof(struct pipe_buffer));
		} else {
			unsigned int tsize = pipe->ring_size - t;
			if (h > 0)
				memcpy(bufs + tsize, pipe->bufs,
				       h * sizeof(struct pipe_buffer));
			memcpy(bufs, pipe->bufs + t,
			       tsize * sizeof(struct pipe_buffer));
		}
	}

	head = n;
	tail = 0;

	kfree(pipe->bufs);
	pipe->bufs = bufs;
	pipe->ring_size = nr_slots;
	if (pipe->max_usage > nr_slots)
		pipe->max_usage = nr_slots;
	pipe->tail = tail;
	pipe->head = head;

	if (!pipe_has_watch_queue(pipe)) {
		pipe->max_usage = nr_slots;
		pipe->nr_accounted = nr_slots;
	}

	spin_unlock_irq(&pipe->rd_wait.lock);

	/* This might have made more room for writers */
	wake_up_interruptible(&pipe->wr_wait);
	return 0;
}

/*
 * Allocate a new array of pipe buffers and copy the info over. Returns the
 * pipe size if successful, or return -ERROR on error.
 */
static long pipe_set_size(struct pipe_inode_info *pipe, unsigned int arg)
{
	unsigned long user_bufs;
	unsigned int nr_slots, size;
	long ret = 0;

	if (pipe_has_watch_queue(pipe))
		return -EBUSY;

	size = round_pipe_size(arg);
	nr_slots = size >> PAGE_SHIFT;

	if (!nr_slots)
		return -EINVAL;

	/*
	 * If trying to increase the pipe capacity, check that an
	 * unprivileged user is not trying to exceed various limits
	 * (soft limit check here, hard limit check just below).
	 * Decreasing the pipe capacity is always permitted, even
	 * if the user is currently over a limit.
	 */
	if (nr_slots > pipe->max_usage &&
			size > pipe_max_size && !capable(CAP_SYS_RESOURCE))
		return -EPERM;

	user_bufs = account_pipe_buffers(pipe->user, pipe->nr_accounted, nr_slots);

	if (nr_slots > pipe->max_usage &&
			(too_many_pipe_buffers_hard(user_bufs) ||
			 too_many_pipe_buffers_soft(user_bufs)) &&
			pipe_is_unprivileged_user()) {
		ret = -EPERM;
		goto out_revert_acct;
	}

	ret = pipe_resize_ring(pipe, nr_slots);
	if (ret < 0)
		goto out_revert_acct;

	return pipe->max_usage * PAGE_SIZE;

out_revert_acct:
	(void) account_pipe_buffers(pipe->user, nr_slots, pipe->nr_accounted);
	return ret;
}

/*
 * Note that i_pipe and i_cdev share the same location, so checking ->i_pipe is
 * not enough to verify that this is a pipe.
 */
struct pipe_inode_info *get_pipe_info(struct file *file, bool for_splice)
{
	struct pipe_inode_info *pipe = file->private_data;

	if (file->f_op != &pipefifo_fops || !pipe)
		return NULL;
	if (for_splice && pipe_has_watch_queue(pipe))
		return NULL;
	return pipe;
}

long pipe_fcntl(struct file *file, unsigned int cmd, unsigned int arg)
{
	struct pipe_inode_info *pipe;
	long ret;

	pipe = get_pipe_info(file, false);
	if (!pipe)
		return -EBADF;

	__pipe_lock(pipe);

	switch (cmd) {
	case F_SETPIPE_SZ:
		ret = pipe_set_size(pipe, arg);
		break;
	case F_GETPIPE_SZ:
		ret = pipe->max_usage * PAGE_SIZE;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	__pipe_unlock(pipe);
	return ret;
}

static const struct super_operations pipefs_ops = {
	.destroy_inode = free_inode_nonrcu,
	.statfs = simple_statfs,
};

/*
 * pipefs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pipe: will go nicely and kill the special-casing in procfs.
 */

static int pipefs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, PIPEFS_MAGIC);
	if (!ctx)
		return -ENOMEM;
	ctx->ops = &pipefs_ops;
	ctx->dops = &pipefs_dentry_operations;
	return 0;
}

static struct file_system_type pipe_fs_type = {
	.name		= "pipefs",
	.init_fs_context = pipefs_init_fs_context,
	.kill_sb	= kill_anon_super,
};

#ifdef CONFIG_SYSCTL
static int do_proc_dopipe_max_size_conv(unsigned long *lvalp,
					unsigned int *valp,
					int write, void *data)
{
	if (write) {
		unsigned int val;

		val = round_pipe_size(*lvalp);
		if (val == 0)
			return -EINVAL;

		*valp = val;
	} else {
		unsigned int val = *valp;
		*lvalp = (unsigned long) val;
	}

	return 0;
}

static int proc_dopipe_max_size(struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_douintvec(table, write, buffer, lenp, ppos,
				 do_proc_dopipe_max_size_conv, NULL);
}

static struct ctl_table fs_pipe_sysctls[] = {
	{
		.procname	= "pipe-max-size",
		.data		= &pipe_max_size,
		.maxlen		= sizeof(pipe_max_size),
		.mode		= 0644,
		.proc_handler	= proc_dopipe_max_size,
	},
	{
		.procname	= "pipe-user-pages-hard",
		.data		= &pipe_user_pages_hard,
		.maxlen		= sizeof(pipe_user_pages_hard),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "pipe-user-pages-soft",
		.data		= &pipe_user_pages_soft,
		.maxlen		= sizeof(pipe_user_pages_soft),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{ }
};
#endif

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
#ifdef CONFIG_SYSCTL
	register_sysctl_init("fs", fs_pipe_sysctls);
#endif
	return err;
}

fs_initcall(init_pipe_fs);
