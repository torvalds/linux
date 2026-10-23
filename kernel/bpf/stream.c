// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <linux/anon_inodes.h>
#include <linux/filter.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/gfp.h>
#include <linux/irq_work.h>
#include <linux/memory.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/refcount.h>

static void bpf_stream_elem_init(struct bpf_stream_elem *elem, int len)
{
	init_llist_node(&elem->node);
	elem->total_len = len;
	elem->consumed_len = 0;
}

static struct bpf_stream_elem *bpf_stream_elem_alloc(int len)
{
	const int max_len = ARRAY_SIZE((struct bpf_bprintf_buffers){}.buf);
	struct bpf_stream_elem *elem;
	size_t alloc_size;

	/*
	 * Length is the payload pushed into the stream, excluding the
	 * trailing NUL of the bprintf buffer. Reject anything that cannot
	 * fit without copying that NUL into the stream element.
	 */
	if (len < 0 || len >= max_len)
		return NULL;

	alloc_size = offsetof(struct bpf_stream_elem, str[len]);
	elem = kmalloc_nolock(alloc_size, __GFP_ZERO, -1);
	if (!elem)
		return NULL;

	bpf_stream_elem_init(elem, len);

	return elem;
}

static int __bpf_stream_push_str(struct llist_head *log, const char *str, int len)
{
	struct bpf_stream_elem *elem = NULL;

	/*
	 * Allocate a bpf_prog_stream_elem and push it to the bpf_prog_stream
	 * log, elements will be popped at once and reversed to print the log.
	 */
	elem = bpf_stream_elem_alloc(len);
	if (!elem)
		return -ENOMEM;

	memcpy(elem->str, str, len);
	llist_add(&elem->node, log);

	return 0;
}

static int bpf_stream_consume_capacity(struct bpf_stream *stream, int len)
{
	if (atomic_read(&stream->capacity) >= BPF_STREAM_MAX_CAPACITY)
		return -ENOSPC;
	if (atomic_add_return(len, &stream->capacity) >= BPF_STREAM_MAX_CAPACITY) {
		atomic_sub(len, &stream->capacity);
		return -ENOSPC;
	}
	return 0;
}

static void bpf_stream_release_capacity(struct bpf_stream *stream, int len)
{
	atomic_sub(len, &stream->capacity);
}

static void bpf_stream_notify(struct irq_work *work)
{
	struct bpf_stream *stream = container_of(work, struct bpf_stream, notify_work);

	/*
	 * Writers run in arbitrary program contexts, including NMI and regions
	 * that already hold wait queue or epoll locks. Wake waiters from
	 * irq_work instead, where taking those locks is safe.
	 */
	wake_up_interruptible_poll(&stream->waitq, EPOLLIN | EPOLLRDNORM);
}

static void bpf_stream_queue_notify(struct bpf_stream *stream)
{
	/*
	 * Record that the work has been used so that teardown only pays for
	 * irq_work_sync(), which may wait for an RCU grace period, when a
	 * callback could actually be in flight.
	 */
	if (!READ_ONCE(stream->notify_used))
		WRITE_ONCE(stream->notify_used, true);
	irq_work_queue(&stream->notify_work);
}

static int bpf_stream_readable_bytes(struct bpf_stream *stream)
{
	return atomic_read_acquire(&stream->readable);
}

static void bpf_stream_publish(struct bpf_stream *stream, int len)
{
	/*
	 * Pairs with atomic_read_acquire() in bpf_stream_readable_bytes().
	 *
	 * Notify only when the stream turns from empty to readable. Readers
	 * block and pollers wait only after finding it empty, and a read never
	 * takes the count below zero, so the publication that makes it positive
	 * is the one they wait for. Publishing into a stream that already holds
	 * data would only raise an interrupt for waiters that are being woken
	 * already, or for nobody at all.
	 */
	if (atomic_add_return_release(len, &stream->readable) == len)
		bpf_stream_queue_notify(stream);
}

static int bpf_stream_push_str(struct bpf_stream *stream, const char *str, int len)
{
	int ret;

	/* Nothing to publish; do not allocate an element for it. */
	if (!len)
		return 0;

	ret = bpf_stream_consume_capacity(stream, len);
	if (ret)
		return ret;

	ret = __bpf_stream_push_str(&stream->log, str, len);
	if (ret)
		bpf_stream_release_capacity(stream, len);
	else
		bpf_stream_publish(stream, len);

	return ret;
}

static struct bpf_stream *bpf_stream_get(enum bpf_stream_id stream_id, struct bpf_prog_aux *aux)
{
	if (stream_id != BPF_STDOUT && stream_id != BPF_STDERR)
		return NULL;
	return aux->stream[stream_id - 1];
}

static void bpf_stream_free_elem(struct bpf_stream_elem *elem)
{
	kfree_nolock(elem);
}

static void bpf_stream_free_list(struct llist_node *list)
{
	struct bpf_stream_elem *elem, *tmp;

	llist_for_each_entry_safe(elem, tmp, list, node)
		bpf_stream_free_elem(elem);
}

static struct llist_node *bpf_stream_backlog_peek(struct bpf_stream *stream)
{
	return stream->backlog_head;
}

static struct llist_node *bpf_stream_backlog_pop(struct bpf_stream *stream)
{
	struct llist_node *node;

	node = stream->backlog_head;
	if (stream->backlog_head == stream->backlog_tail)
		stream->backlog_head = stream->backlog_tail = NULL;
	else
		stream->backlog_head = node->next;
	return node;
}

static void bpf_stream_backlog_fill(struct bpf_stream *stream)
{
	struct llist_node *head, *tail;

	if (llist_empty(&stream->log))
		return;
	tail = llist_del_all(&stream->log);
	if (!tail)
		return;
	head = llist_reverse_order(tail);

	if (!stream->backlog_head) {
		stream->backlog_head = head;
		stream->backlog_tail = tail;
	} else {
		stream->backlog_tail->next = head;
		stream->backlog_tail = tail;
	}

	return;
}

static bool bpf_stream_consume_elem(struct bpf_stream_elem *elem, int *len)
{
	int rem = elem->total_len - elem->consumed_len;
	int used = min(rem, *len);

	elem->consumed_len += used;
	*len -= used;

	return elem->consumed_len == elem->total_len;
}

static int bpf_stream_read(struct bpf_stream *stream, void __user *buf, int len)
{
	int read_len, rem_len, cons_len, ret = 0;
	struct bpf_stream_elem *elem = NULL;
	struct llist_node *node;

	mutex_lock(&stream->lock);
	read_len = min(len, bpf_stream_readable_bytes(stream));
	rem_len = read_len;

	while (rem_len) {
		int pos = read_len - rem_len;
		int chunk, n;
		bool cont;

		node = bpf_stream_backlog_peek(stream);
		if (!node) {
			bpf_stream_backlog_fill(stream);
			node = bpf_stream_backlog_peek(stream);
		}
		if (!node)
			break;
		elem = container_of(node, typeof(*elem), node);

		cons_len = elem->consumed_len;
		cont = bpf_stream_consume_elem(elem, &rem_len) == false;
		chunk = elem->consumed_len - cons_len;

		n = copy_to_user(buf + pos, elem->str + cons_len, chunk);
		if (n) {
			/* Keep any successfully copied bytes; -EFAULT only if none. */
			elem->consumed_len -= n;
			rem_len += n;
			ret = (read_len == rem_len) ? -EFAULT : 0;
			break;
		}

		if (cont)
			continue;
		bpf_stream_backlog_pop(stream);
		bpf_stream_release_capacity(stream, elem->total_len);
		bpf_stream_free_elem(elem);
	}

	atomic_sub(read_len - rem_len, &stream->readable);
	mutex_unlock(&stream->lock);
	return ret ? ret : read_len - rem_len;
}

int bpf_prog_stream_read(struct bpf_prog *prog, enum bpf_stream_id stream_id, void __user *buf, u32 len)
{
	struct bpf_stream *stream;

	stream = bpf_stream_get(stream_id, prog->aux);
	if (!stream)
		return -ENOENT;
	if (len > INT_MAX)
		return -EINVAL;
	return bpf_stream_read(stream, buf, len);
}

static bool bpf_stream_has_data(struct bpf_stream *stream)
{
	return bpf_stream_readable_bytes(stream) > 0;
}

static void bpf_stream_put(struct bpf_stream *stream)
{
	if (refcount_dec_and_test(&stream->refcnt)) {
		struct llist_node *list;

		/* Only a stream that ever queued its work can have a callback in flight. */
		if (READ_ONCE(stream->notify_used))
			irq_work_sync(&stream->notify_work);
		list = llist_del_all(&stream->log);
		bpf_stream_free_list(list);
		bpf_stream_free_list(stream->backlog_head);
		mutex_destroy(&stream->lock);
		kfree(stream);
	}
}

static int bpf_stream_release(struct inode *inode, struct file *file)
{
	bpf_stream_put(file->private_data);
	return 0;
}

static ssize_t bpf_stream_file_read(struct file *file, char __user *buf, size_t len,
				    loff_t *ppos)
{
	struct bpf_stream *stream = file->private_data;
	bool dead;
	int ret;

	if (!len)
		return 0;

	for (;;) {
		/*
		 * Sample teardown state before looking for data. Nothing is
		 * published once the program is gone, so finding the stream
		 * empty after observing dead means EOF. The opposite order could
		 * report EOF while data published just before teardown is still
		 * buffered.
		 */
		dead = smp_load_acquire(&stream->dead);
		ret = bpf_stream_read(stream, buf, len);
		if (ret)
			return ret;
		if (dead)
			return 0;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(stream->waitq,
					       bpf_stream_has_data(stream) ||
					       READ_ONCE(stream->dead));
		if (ret)
			return ret;
	}
}

static __poll_t bpf_stream_poll(struct file *file, struct poll_table_struct *pts)
{
	struct bpf_stream *stream = file->private_data;
	__poll_t events = 0;

	/*
	 * poll_wait() only registers the wait queue callback. Register before
	 * checking persistent state so a concurrent publication or teardown is
	 * observed either by the callback or by the checks below.
	 */
	poll_wait(file, &stream->waitq, pts);
	if (bpf_stream_has_data(stream))
		events |= EPOLLIN | EPOLLRDNORM;
	if (READ_ONCE(stream->dead))
		events |= EPOLLHUP;
	return events;
}

static const struct file_operations bpf_stream_fops = {
	.release = bpf_stream_release,
	.read = bpf_stream_file_read,
	.poll = bpf_stream_poll,
};

int bpf_prog_stream_new_fd(struct bpf_prog *prog, enum bpf_stream_id stream_id, u32 flags)
{
	struct bpf_stream *stream;
	int fd_flags = O_RDONLY | O_CLOEXEC;
	int fd;

	stream = bpf_stream_get(stream_id, prog->aux);
	if (!stream)
		return -ENOENT;
	if (flags & BPF_F_STREAM_NONBLOCK)
		fd_flags |= O_NONBLOCK;

	refcount_inc(&stream->refcnt);
	fd = anon_inode_getfd("bpf-stream", &bpf_stream_fops, stream, fd_flags);
	if (fd < 0)
		bpf_stream_put(stream);
	return fd;
}

__bpf_kfunc_start_defs();

/*
 * Avoid using enum bpf_stream_id so that kfunc users don't have to pull in the
 * enum in headers.
 */
__bpf_kfunc int bpf_stream_vprintk(int stream_id, const char *fmt__str, const void *args,
				   u32 len__sz, struct bpf_prog_aux *aux)
{
	struct bpf_bprintf_data data = {
		.get_bin_args	= true,
		.get_buf	= true,
	};
	u32 fmt_size = strlen(fmt__str) + 1;
	struct bpf_stream *stream;
	u32 data_len = len__sz;
	int ret, num_args;

	stream = bpf_stream_get(stream_id, aux);
	if (!stream)
		return -ENOENT;

	if (data_len & 7 || data_len > MAX_BPRINTF_VARARGS * 8 ||
	    (data_len && !args))
		return -EINVAL;
	num_args = data_len / 8;

	ret = bpf_bprintf_prepare(fmt__str, fmt_size, args, num_args, &data);
	if (ret < 0)
		return ret;

	ret = bstr_printf(data.buf, MAX_BPRINTF_BUF, fmt__str, data.bin_args);
	/* Truncation: reject before capacity charge (not -ENOMEM). */
	if (ret >= MAX_BPRINTF_BUF) {
		bpf_bprintf_cleanup(&data);
		return -E2BIG;
	}
	/* Exclude NULL byte during push. */
	ret = bpf_stream_push_str(stream, data.buf, ret);
	bpf_bprintf_cleanup(&data);

	return ret;
}

/* Directly trigger a stack dump from the program. */
__bpf_kfunc int bpf_stream_print_stack(int stream_id, struct bpf_prog_aux *aux)
{
	struct bpf_stream_stage ss;
	struct bpf_prog *prog;

	/* Make sure the stream ID is valid. */
	if (!bpf_stream_get(stream_id, aux))
		return -ENOENT;

	prog = aux->main_prog_aux->prog;

	bpf_stream_stage(ss, prog, stream_id, ({
		bpf_stream_dump_stack(ss);
	}));

	return 0;
}

__bpf_kfunc_end_defs();

/* Added kfunc to common_btf_ids */

int bpf_prog_stream_init(struct bpf_prog *prog, gfp_t gfp_extra_flags)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(prog->aux->stream); i++) {
		struct bpf_stream *stream;

		/* On failure, bpf_prog_stream_free() releases the streams allocated so far. */
		stream = kzalloc_obj(*stream,
				     bpf_memcg_flags(GFP_KERNEL | gfp_extra_flags));
		if (!stream)
			return -ENOMEM;

		refcount_set(&stream->refcnt, 1);
		init_llist_head(&stream->log);
		mutex_init(&stream->lock);
		init_waitqueue_head(&stream->waitq);
		init_irq_work(&stream->notify_work, bpf_stream_notify);
		prog->aux->stream[i] = stream;
	}
	return 0;
}

void bpf_prog_stream_free(struct bpf_prog *prog)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(prog->aux->stream); i++) {
		struct bpf_stream *stream = prog->aux->stream[i];

		if (!stream)
			continue;
		/*
		 * Pairs with smp_load_acquire() in bpf_stream_file_read(): every
		 * publication precedes the dead flag, so a reader that observes
		 * it also observes all buffered data.
		 */
		smp_store_release(&stream->dead, true);
		wake_up_interruptible_poll(&stream->waitq, EPOLLHUP);
		bpf_stream_put(stream);
		prog->aux->stream[i] = NULL;
	}
}

void bpf_stream_stage_init(struct bpf_stream_stage *ss)
{
	init_llist_head(&ss->log);
	ss->len = 0;
}

void bpf_stream_stage_free(struct bpf_stream_stage *ss)
{
	struct llist_node *node;

	node = llist_del_all(&ss->log);
	bpf_stream_free_list(node);
}

int bpf_stream_stage_printk(struct bpf_stream_stage *ss, const char *fmt, ...)
{
	struct bpf_bprintf_buffers *buf;
	va_list args;
	int len, ret;

	if (bpf_try_get_buffers(&buf))
		return -EBUSY;

	va_start(args, fmt);
	len = vscnprintf(buf->buf, ARRAY_SIZE(buf->buf), fmt, args);
	va_end(args);
	/* Exclude NULL byte during push; skip empty output entirely. */
	ret = len ? __bpf_stream_push_str(&ss->log, buf->buf, len) : 0;
	if (!ret)
		ss->len += len;
	bpf_put_buffers();
	return ret;
}

int bpf_stream_stage_commit(struct bpf_stream_stage *ss, struct bpf_prog *prog,
			    enum bpf_stream_id stream_id)
{
	struct llist_node *list, *head, *tail;
	struct bpf_stream *stream;
	int ret;

	stream = bpf_stream_get(stream_id, prog->aux);
	if (!stream)
		return -EINVAL;

	ret = bpf_stream_consume_capacity(stream, ss->len);
	if (ret)
		return ret;

	list = llist_del_all(&ss->log);
	head = tail = list;

	if (!list)
		return 0;
	while (llist_next(list)) {
		tail = llist_next(list);
		list = tail;
	}
	llist_add_batch(head, tail, &stream->log);
	bpf_stream_publish(stream, ss->len);
	return 0;
}

struct dump_stack_ctx {
	struct bpf_stream_stage *ss;
	int err;
};

static bool dump_stack_cb(void *cookie, u64 ip, u64 sp, u64 bp)
{
	struct dump_stack_ctx *ctxp = cookie;
	const char *file = "", *line = "";
	struct bpf_prog *prog;
	int num, ret;

	rcu_read_lock();
	prog = bpf_prog_ksym_find(ip);
	rcu_read_unlock();
	if (prog) {
		ret = bpf_prog_get_file_line(prog, ip, &file, &line, &num);
		if (ret < 0)
			goto end;
		ctxp->err = bpf_stream_stage_printk(ctxp->ss, "%pS\n  %s @ %s:%d\n",
						    (void *)(long)ip, line, file, num);
		return !ctxp->err;
	}
end:
	ctxp->err = bpf_stream_stage_printk(ctxp->ss, "%pS\n", (void *)(long)ip);
	return !ctxp->err;
}

int bpf_stream_stage_dump_stack(struct bpf_stream_stage *ss)
{
	struct dump_stack_ctx ctx = { .ss = ss };
	int ret;

	ret = bpf_stream_stage_printk(ss, "CPU: %d UID: %d PID: %d Comm: %s\n",
				      raw_smp_processor_id(), __kuid_val(current_real_cred()->euid),
				      current->pid, current->comm);
	if (ret)
		return ret;
	ret = bpf_stream_stage_printk(ss, "Call trace:\n");
	if (ret)
		return ret;
	arch_bpf_stack_walk(dump_stack_cb, &ctx);
	if (ctx.err)
		return ctx.err;
	return bpf_stream_stage_printk(ss, "\n");
}
