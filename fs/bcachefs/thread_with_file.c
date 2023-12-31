// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "printbuf.h"
#include "thread_with_file.h"

#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/poll.h>

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

	fd_install(fd, file);
	get_task_struct(thr->task);
	wake_up_process(thr->task);
	return fd;
err:
	if (fd >= 0)
		put_unused_fd(fd);
	if (thr->task)
		kthread_stop(thr->task);
	return ret;
}

static inline bool thread_with_stdio_has_output(struct thread_with_stdio *thr)
{
	return thr->stdio.output_buf.pos ||
		thr->output2.nr ||
		thr->thr.done;
}

static ssize_t thread_with_stdio_read(struct file *file, char __user *buf,
				      size_t len, loff_t *ppos)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);
	size_t copied = 0, b;
	int ret = 0;

	if ((file->f_flags & O_NONBLOCK) &&
	    !thread_with_stdio_has_output(thr))
		return -EAGAIN;

	ret = wait_event_interruptible(thr->stdio.output_wait,
		thread_with_stdio_has_output(thr));
	if (ret)
		return ret;

	if (thr->thr.done)
		return 0;

	while (len) {
		ret = darray_make_room(&thr->output2, thr->stdio.output_buf.pos);
		if (ret)
			break;

		spin_lock_irq(&thr->stdio.output_lock);
		b = min_t(size_t, darray_room(thr->output2), thr->stdio.output_buf.pos);

		memcpy(&darray_top(thr->output2), thr->stdio.output_buf.buf, b);
		memmove(thr->stdio.output_buf.buf,
			thr->stdio.output_buf.buf + b,
			thr->stdio.output_buf.pos - b);

		thr->output2.nr += b;
		thr->stdio.output_buf.pos -= b;
		spin_unlock_irq(&thr->stdio.output_lock);

		b = min(len, thr->output2.nr);
		if (!b)
			break;

		b -= copy_to_user(buf, thr->output2.data, b);
		if (!b) {
			ret = -EFAULT;
			break;
		}

		copied	+= b;
		buf	+= b;
		len	-= b;

		memmove(thr->output2.data,
			thr->output2.data + b,
			thr->output2.nr - b);
		thr->output2.nr -= b;
	}

	return copied ?: ret;
}

static int thread_with_stdio_release(struct inode *inode, struct file *file)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);

	bch2_thread_with_file_exit(&thr->thr);
	printbuf_exit(&thr->stdio.input_buf);
	printbuf_exit(&thr->stdio.output_buf);
	darray_exit(&thr->output2);
	thr->exit(thr);
	return 0;
}

#define WRITE_BUFFER		4096

static inline bool thread_with_stdio_has_input_space(struct thread_with_stdio *thr)
{
	return thr->stdio.input_buf.pos < WRITE_BUFFER || thr->thr.done;
}

static ssize_t thread_with_stdio_write(struct file *file, const char __user *ubuf,
				       size_t len, loff_t *ppos)
{
	struct thread_with_stdio *thr =
		container_of(file->private_data, struct thread_with_stdio, thr);
	struct printbuf *buf = &thr->stdio.input_buf;
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

		spin_lock(&thr->stdio.input_lock);
		if (buf->pos < WRITE_BUFFER)
			bch2_printbuf_make_room(buf, min(b, WRITE_BUFFER - buf->pos));
		b = min(len, printbuf_remaining_size(buf));

		if (b && !copy_from_user_nofault(&buf->buf[buf->pos], ubuf, b)) {
			ubuf += b;
			len -= b;
			copied += b;
			buf->pos += b;
		}
		spin_unlock(&thr->stdio.input_lock);

		if (b) {
			wake_up(&thr->stdio.input_wait);
		} else {
			if ((file->f_flags & O_NONBLOCK)) {
				ret = -EAGAIN;
				break;
			}

			ret = wait_event_interruptible(thr->stdio.input_wait,
					thread_with_stdio_has_input_space(thr));
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

	poll_wait(file, &thr->stdio.output_wait, wait);
	poll_wait(file, &thr->stdio.input_wait, wait);

	__poll_t mask = 0;

	if (thread_with_stdio_has_output(thr))
		mask |= EPOLLIN;
	if (thread_with_stdio_has_input_space(thr))
		mask |= EPOLLOUT;
	if (thr->thr.done)
		mask |= EPOLLHUP|EPOLLERR;
	return mask;
}

static const struct file_operations thread_with_stdio_fops = {
	.release	= thread_with_stdio_release,
	.read		= thread_with_stdio_read,
	.write		= thread_with_stdio_write,
	.poll		= thread_with_stdio_poll,
	.llseek		= no_llseek,
};

int bch2_run_thread_with_stdio(struct thread_with_stdio *thr,
			       void (*exit)(struct thread_with_stdio *),
			       int (*fn)(void *))
{
	thr->stdio.input_buf = PRINTBUF;
	thr->stdio.input_buf.atomic++;
	spin_lock_init(&thr->stdio.input_lock);
	init_waitqueue_head(&thr->stdio.input_wait);

	thr->stdio.output_buf = PRINTBUF;
	thr->stdio.output_buf.atomic++;
	spin_lock_init(&thr->stdio.output_lock);
	init_waitqueue_head(&thr->stdio.output_wait);

	darray_init(&thr->output2);
	thr->exit = exit;

	return bch2_run_thread_with_file(&thr->thr, &thread_with_stdio_fops, fn);
}

int bch2_stdio_redirect_read(struct stdio_redirect *stdio, char *buf, size_t len)
{
	wait_event(stdio->input_wait,
		   stdio->input_buf.pos || stdio->done);

	if (stdio->done)
		return -1;

	spin_lock(&stdio->input_lock);
	int ret = min(len, stdio->input_buf.pos);
	stdio->input_buf.pos -= ret;
	memcpy(buf, stdio->input_buf.buf, ret);
	memmove(stdio->input_buf.buf,
		stdio->input_buf.buf + ret,
		stdio->input_buf.pos);
	spin_unlock(&stdio->input_lock);

	wake_up(&stdio->input_wait);
	return ret;
}

int bch2_stdio_redirect_readline(struct stdio_redirect *stdio, char *buf, size_t len)
{
	wait_event(stdio->input_wait,
		   stdio->input_buf.pos || stdio->done);

	if (stdio->done)
		return -1;

	spin_lock(&stdio->input_lock);
	int ret = min(len, stdio->input_buf.pos);
	char *n = memchr(stdio->input_buf.buf, '\n', ret);
	if (n)
		ret = min(ret, n + 1 - stdio->input_buf.buf);
	stdio->input_buf.pos -= ret;
	memcpy(buf, stdio->input_buf.buf, ret);
	memmove(stdio->input_buf.buf,
		stdio->input_buf.buf + ret,
		stdio->input_buf.pos);
	spin_unlock(&stdio->input_lock);

	wake_up(&stdio->input_wait);
	return ret;
}
