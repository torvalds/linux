// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "thread_with_file.h"

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/sched/sysctl.h>

void bch2_thread_with_file_exit(struct thread_with_file *thr)
{
	if (thr->task) {
		kthread_stop(thr->task);
		put_task_struct(thr->task);
	}
}

int bch2_run_thread_with_file(struct thread_with_file *thr,
			      const struct file_operations *fops,
			      int (*fn)(void *))
{
	struct file *file = NULL;
	int ret, fd = -1;
	unsigned fd_flags = O_CLOEXEC;

	if (fops->read && fops->write)
		fd_flags |= O_RDWR;
	else if (fops->read)
		fd_flags |= O_RDONLY;
	else if (fops->write)
		fd_flags |= O_WRONLY;

	char name[TASK_COMM_LEN];
	get_task_comm(name, current);

	thr->ret = 0;
	thr->task = kthread_create(fn, thr, "%s", name);
	ret = PTR_ERR_OR_ZERO(thr->task);
	if (ret)
		return ret;

	ret = get_unused_fd_flags(fd_flags);
	if (ret < 0)
		goto err;
	fd = ret;

	file = anon_inode_getfile(name, fops, thr, fd_flags);
	ret = PTR_ERR_OR_ZERO(file);
	if (ret)
		goto err;

	get_task_struct(thr->task);
	wake_up_process(thr->task);
	fd_install(fd, file);
	return fd;
err:
	if (fd >= 0)
		put_unused_fd(fd);
	if (thr->task)
		kthread_stop(thr->task);
	return ret;
}

/* stdio_redirect */

static bool stdio_redirect_has_more_input(struct stdio_redirect *stdio, size_t seen)
{
	return stdio->input.buf.nr > seen || stdio->done;
}

static bool stdio_redirect_has_input(struct stdio_redirect *stdio)
{
	return stdio_redirect_has_more_input(stdio, 0);
}

static bool stdio_redirect_has_output(struct stdio_redirect *stdio)
{
	return stdio->output.buf.nr || stdio->done;
}

#define STDIO_REDIRECT_BUFSIZE		4096

static bool stdio_redirect_has_input_space(struct stdio_redirect *stdio)
{
	return stdio->input.buf.nr < STDIO_REDIRECT_BUFSIZE || stdio->done;
}

static bool stdio_redirect_has_output_space(struct stdio_redirect *stdio)
{
	return stdio->output.buf.nr < STDIO_REDIRECT_BUFSIZE || stdio->done;
}

static void stdio_buf_init(struct stdio_buf *buf)
{
	spin_lock_init(&buf->lock);
	init_waitqueue_head(&buf->wait);
	darray_init(&buf->buf);
}

/* thread_with_stdio */

static void thread_with_stdio_done(struct thread_with_stdio *thr)
{
	thr->thr.done = true;
	thr->stdio.done = true;
	wake_up(&thr->stdio.input.wait);
	wake_up(&thr->stdio.output.wait);
}

static ssize_t thread_with_stdio_read(struct file *file, char __user *ubuf,
				      size_t len, loff_t *ppos)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);
	struct stdio_buf *buf = &thr->stdio.output;
	size_t copied = 0, b;
	int ret = 0;

	if (!(file->f_flags & O_NONBLOCK)) {
		ret = wait_event_interruptible(buf->wait, stdio_redirect_has_output(&thr->stdio));
		if (ret)
			return ret;
	} else if (!stdio_redirect_has_output(&thr->stdio))
		return -EAGAIN;

	while (len && buf->buf.nr) {
		if (fault_in_writeable(ubuf, len) == len) {
			ret = -EFAULT;
			break;
		}

		spin_lock_irq(&buf->lock);
		b = min_t(size_t, len, buf->buf.nr);

		if (b && !copy_to_user_nofault(ubuf, buf->buf.data, b)) {
			ubuf	+= b;
			len	-= b;
			copied	+= b;
			buf->buf.nr -= b;
			memmove(buf->buf.data,
				buf->buf.data + b,
				buf->buf.nr);
		}
		spin_unlock_irq(&buf->lock);
	}

	return copied ?: ret;
}

static int thread_with_stdio_release(struct inode *inode, struct file *file)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);

	thread_with_stdio_done(thr);
	bch2_thread_with_file_exit(&thr->thr);
	darray_exit(&thr->stdio.input.buf);
	darray_exit(&thr->stdio.output.buf);
	thr->ops->exit(thr);
	return 0;
}

static ssize_t thread_with_stdio_write(struct file *file, const char __user *ubuf,
				       size_t len, loff_t *ppos)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);
	struct stdio_buf *buf = &thr->stdio.input;
	size_t copied = 0;
	ssize_t ret = 0;

	while (len) {
		if (thr->thr.done) {
			ret = -EPIPE;
			break;
		}

		size_t b = len - fault_in_readable(ubuf, len);
		if (!b) {
			ret = -EFAULT;
			break;
		}

		spin_lock(&buf->lock);
		size_t makeroom = b;
		if (!buf->waiting_for_line || memchr(buf->buf.data, '\n', buf->buf.nr))
			makeroom = min_t(ssize_t, makeroom,
				   max_t(ssize_t, STDIO_REDIRECT_BUFSIZE - buf->buf.nr,
						  0));
		darray_make_room_gfp(&buf->buf, makeroom, GFP_NOWAIT);

		b = min(len, darray_room(buf->buf));

		if (b && !copy_from_user_nofault(&darray_top(buf->buf), ubuf, b)) {
			buf->buf.nr += b;
			ubuf	+= b;
			len	-= b;
			copied	+= b;
		}
		spin_unlock(&buf->lock);

		if (b) {
			wake_up(&buf->wait);
		} else {
			if ((file->f_flags & O_NONBLOCK)) {
				ret = -EAGAIN;
				break;
			}

			ret = wait_event_interruptible(buf->wait,
					stdio_redirect_has_input_space(&thr->stdio));
			if (ret)
				break;
		}
	}

	return copied ?: ret;
}

static __poll_t thread_with_stdio_poll(struct file *file, struct poll_table_struct *wait)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);

	poll_wait(file, &thr->stdio.output.wait, wait);
	poll_wait(file, &thr->stdio.input.wait, wait);

	__poll_t mask = 0;

	if (stdio_redirect_has_output(&thr->stdio))
		mask |= EPOLLIN;
	if (stdio_redirect_has_input_space(&thr->stdio))
		mask |= EPOLLOUT;
	if (thr->thr.done)
		mask |= EPOLLHUP|EPOLLERR;
	return mask;
}

static __poll_t thread_with_stdout_poll(struct file *file, struct poll_table_struct *wait)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);

	poll_wait(file, &thr->stdio.output.wait, wait);

	__poll_t mask = 0;

	if (stdio_redirect_has_output(&thr->stdio))
		mask |= EPOLLIN;
	if (thr->thr.done)
		mask |= EPOLLHUP|EPOLLERR;
	return mask;
}

static int thread_with_stdio_flush(struct file *file, fl_owner_t id)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);

	return thr->thr.ret;
}

static long thread_with_stdio_ioctl(struct file *file, unsigned int cmd, unsigned long p)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);

	if (thr->ops->unlocked_ioctl)
		return thr->ops->unlocked_ioctl(thr, cmd, p);
	return -ENOTTY;
}

static const struct file_operations thread_with_stdio_fops = {
	.llseek		= no_llseek,
	.read		= thread_with_stdio_read,
	.write		= thread_with_stdio_write,
	.poll		= thread_with_stdio_poll,
	.flush		= thread_with_stdio_flush,
	.release	= thread_with_stdio_release,
	.unlocked_ioctl	= thread_with_stdio_ioctl,
};

static const struct file_operations thread_with_stdout_fops = {
	.llseek		= no_llseek,
	.read		= thread_with_stdio_read,
	.poll		= thread_with_stdout_poll,
	.flush		= thread_with_stdio_flush,
	.release	= thread_with_stdio_release,
	.unlocked_ioctl	= thread_with_stdio_ioctl,
};

static int thread_with_stdio_fn(void *arg)
{
	struct thread_with_stdio *thr = arg;

	thr->thr.ret = thr->ops->fn(thr);

	thread_with_stdio_done(thr);
	return 0;
}

void bch2_thread_with_stdio_init(struct thread_with_stdio *thr,
				 const struct thread_with_stdio_ops *ops)
{
	stdio_buf_init(&thr->stdio.input);
	stdio_buf_init(&thr->stdio.output);
	thr->ops = ops;
}

int __bch2_run_thread_with_stdio(struct thread_with_stdio *thr)
{
	return bch2_run_thread_with_file(&thr->thr, &thread_with_stdio_fops, thread_with_stdio_fn);
}

int bch2_run_thread_with_stdio(struct thread_with_stdio *thr,
			       const struct thread_with_stdio_ops *ops)
{
	bch2_thread_with_stdio_init(thr, ops);

	return __bch2_run_thread_with_stdio(thr);
}

int bch2_run_thread_with_stdout(struct thread_with_stdio *thr,
				const struct thread_with_stdio_ops *ops)
{
	stdio_buf_init(&thr->stdio.input);
	stdio_buf_init(&thr->stdio.output);
	thr->ops = ops;

	return bch2_run_thread_with_file(&thr->thr, &thread_with_stdout_fops, thread_with_stdio_fn);
}
EXPORT_SYMBOL_GPL(bch2_run_thread_with_stdout);

int bch2_stdio_redirect_read(struct stdio_redirect *stdio, char *ubuf, size_t len)
{
	struct stdio_buf *buf = &stdio->input;

	/*
	 * we're waiting on user input (or for the file descriptor to be
	 * closed), don't want a hung task warning:
	 */
	do {
		wait_event_timeout(buf->wait, stdio_redirect_has_input(stdio),
				   sysctl_hung_task_timeout_secs * HZ / 2);
	} while (!stdio_redirect_has_input(stdio));

	if (stdio->done)
		return -1;

	spin_lock(&buf->lock);
	int ret = min(len, buf->buf.nr);
	buf->buf.nr -= ret;
	memcpy(ubuf, buf->buf.data, ret);
	memmove(buf->buf.data,
		buf->buf.data + ret,
		buf->buf.nr);
	spin_unlock(&buf->lock);

	wake_up(&buf->wait);
	return ret;
}

int bch2_stdio_redirect_readline_timeout(struct stdio_redirect *stdio,
					 darray_char *line,
					 unsigned long timeout)
{
	unsigned long until = jiffies + timeout, t;
	struct stdio_buf *buf = &stdio->input;
	size_t seen = 0;
again:
	t = timeout != MAX_SCHEDULE_TIMEOUT
		? max_t(long, until - jiffies, 0)
		: timeout;

	t = min(t, sysctl_hung_task_timeout_secs * HZ / 2);

	wait_event_timeout(buf->wait, stdio_redirect_has_more_input(stdio, seen), t);

	if (stdio->done)
		return -1;

	spin_lock(&buf->lock);
	seen = buf->buf.nr;
	char *n = memchr(buf->buf.data, '\n', seen);

	if (!n && timeout != MAX_SCHEDULE_TIMEOUT && time_after_eq(jiffies, until)) {
		spin_unlock(&buf->lock);
		return -ETIME;
	}

	if (!n) {
		buf->waiting_for_line = true;
		spin_unlock(&buf->lock);
		goto again;
	}

	size_t b = n + 1 - buf->buf.data;
	if (b > line->size) {
		spin_unlock(&buf->lock);
		int ret = darray_resize(line, b);
		if (ret)
			return ret;
		seen = 0;
		goto again;
	}

	buf->buf.nr -= b;
	memcpy(line->data, buf->buf.data, b);
	memmove(buf->buf.data,
		buf->buf.data + b,
		buf->buf.nr);
	line->nr = b;

	buf->waiting_for_line = false;
	spin_unlock(&buf->lock);

	wake_up(&buf->wait);
	return 0;
}

int bch2_stdio_redirect_readline(struct stdio_redirect *stdio, darray_char *line)
{
	return bch2_stdio_redirect_readline_timeout(stdio, line, MAX_SCHEDULE_TIMEOUT);
}

__printf(3, 0)
static ssize_t bch2_darray_vprintf(darray_char *out, gfp_t gfp, const char *fmt, va_list args)
{
	ssize_t ret;

	do {
		va_list args2;
		size_t len;

		va_copy(args2, args);
		len = vsnprintf(out->data + out->nr, darray_room(*out), fmt, args2);
		va_end(args2);

		if (len + 1 <= darray_room(*out)) {
			out->nr += len;
			return len;
		}

		ret = darray_make_room_gfp(out, len + 1, gfp);
	} while (ret == 0);

	return ret;
}

ssize_t bch2_stdio_redirect_vprintf(struct stdio_redirect *stdio, bool nonblocking,
				    const char *fmt, va_list args)
{
	struct stdio_buf *buf = &stdio->output;
	unsigned long flags;
	ssize_t ret;

again:
	spin_lock_irqsave(&buf->lock, flags);
	ret = bch2_darray_vprintf(&buf->buf, GFP_NOWAIT, fmt, args);
	spin_unlock_irqrestore(&buf->lock, flags);

	if (ret < 0) {
		if (nonblocking)
			return -EAGAIN;

		ret = wait_event_interruptible(buf->wait,
				stdio_redirect_has_output_space(stdio));
		if (ret)
			return ret;
		goto again;
	}

	wake_up(&buf->wait);
	return ret;
}

ssize_t bch2_stdio_redirect_printf(struct stdio_redirect *stdio, bool nonblocking,
				const char *fmt, ...)
{
	va_list args;
	ssize_t ret;

	va_start(args, fmt);
	ret = bch2_stdio_redirect_vprintf(stdio, nonblocking, fmt, args);
	va_end(args);

	return ret;
}

#endif /* NO_BCACHEFS_FS */
