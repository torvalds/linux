// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/percpu.h>
#include <linux/refcount.h>
#include <linux/gfp.h>
#include <linux/memory.h>
#include <linux/local_lock.h>
#include <linux/mutex.h>

/*
 * Simple per-CPU NMI-safe bump allocation mechanism, backed by the NMI-safe
 * try_alloc_pages()/free_pages_nolock() primitives. We allocate a page and
 * stash it in a local per-CPU variable, and bump allocate from the page
 * whenever items need to be printed to a stream. Each page holds a global
 * atomic refcount in its first 4 bytes, and then records of variable length
 * that describe the printed messages. Once the global refcount has dropped to
 * zero, it is a signal to free the page back to the kernel's page allocator,
 * given all the individual records in it have been consumed.
 *
 * It is possible the same page is used to serve allocations across different
 * programs, which may be consumed at different times individually, hence
 * maintaining a reference count per-page is critical for correct lifetime
 * tracking.
 *
 * The bpf_stream_page code will be replaced to use kmalloc_nolock() once it
 * lands.
 */
struct bpf_stream_page {
	refcount_t ref;
	u32 consumed;
	char buf[];
};

/* Available room to add data to a refcounted page. */
#define BPF_STREAM_PAGE_SZ (PAGE_SIZE - offsetofend(struct bpf_stream_page, consumed))

static DEFINE_PER_CPU(local_trylock_t, stream_local_lock) = INIT_LOCAL_TRYLOCK(stream_local_lock);
static DEFINE_PER_CPU(struct bpf_stream_page *, stream_pcpu_page);

static bool bpf_stream_page_local_lock(unsigned long *flags)
{
	return local_trylock_irqsave(&stream_local_lock, *flags);
}

static void bpf_stream_page_local_unlock(unsigned long *flags)
{
	local_unlock_irqrestore(&stream_local_lock, *flags);
}

static void bpf_stream_page_free(struct bpf_stream_page *stream_page)
{
	struct page *p;

	if (!stream_page)
		return;
	p = virt_to_page(stream_page);
	free_pages_nolock(p, 0);
}

static void bpf_stream_page_get(struct bpf_stream_page *stream_page)
{
	refcount_inc(&stream_page->ref);
}

static void bpf_stream_page_put(struct bpf_stream_page *stream_page)
{
	if (refcount_dec_and_test(&stream_page->ref))
		bpf_stream_page_free(stream_page);
}

static void bpf_stream_page_init(struct bpf_stream_page *stream_page)
{
	refcount_set(&stream_page->ref, 1);
	stream_page->consumed = 0;
}

static struct bpf_stream_page *bpf_stream_page_replace(void)
{
	struct bpf_stream_page *stream_page, *old_stream_page;
	struct page *page;

	page = alloc_pages_nolock(NUMA_NO_NODE, 0);
	if (!page)
		return NULL;
	stream_page = page_address(page);
	bpf_stream_page_init(stream_page);

	old_stream_page = this_cpu_read(stream_pcpu_page);
	if (old_stream_page)
		bpf_stream_page_put(old_stream_page);
	this_cpu_write(stream_pcpu_page, stream_page);
	return stream_page;
}

static int bpf_stream_page_check_room(struct bpf_stream_page *stream_page, int len)
{
	int min = offsetof(struct bpf_stream_elem, str[0]);
	int consumed = stream_page->consumed;
	int total = BPF_STREAM_PAGE_SZ;
	int rem = max(0, total - consumed - min);

	/* Let's give room of at least 8 bytes. */
	WARN_ON_ONCE(rem % 8 != 0);
	rem = rem < 8 ? 0 : rem;
	return min(len, rem);
}

static void bpf_stream_elem_init(struct bpf_stream_elem *elem, int len)
{
	init_llist_node(&elem->node);
	elem->total_len = len;
	elem->consumed_len = 0;
}

static struct bpf_stream_page *bpf_stream_page_from_elem(struct bpf_stream_elem *elem)
{
	unsigned long addr = (unsigned long)elem;

	return (struct bpf_stream_page *)PAGE_ALIGN_DOWN(addr);
}

static struct bpf_stream_elem *bpf_stream_page_push_elem(struct bpf_stream_page *stream_page, int len)
{
	u32 consumed = stream_page->consumed;

	stream_page->consumed += round_up(offsetof(struct bpf_stream_elem, str[len]), 8);
	return (struct bpf_stream_elem *)&stream_page->buf[consumed];
}

static struct bpf_stream_elem *bpf_stream_page_reserve_elem(int len)
{
	struct bpf_stream_elem *elem = NULL;
	struct bpf_stream_page *page;
	int room = 0;

	page = this_cpu_read(stream_pcpu_page);
	if (!page)
		page = bpf_stream_page_replace();
	if (!page)
		return NULL;

	room = bpf_stream_page_check_room(page, len);
	if (room != len)
		page = bpf_stream_page_replace();
	if (!page)
		return NULL;
	bpf_stream_page_get(page);
	room = bpf_stream_page_check_room(page, len);
	WARN_ON_ONCE(room != len);

	elem = bpf_stream_page_push_elem(page, room);
	bpf_stream_elem_init(elem, room);
	return elem;
}

static struct bpf_stream_elem *bpf_stream_elem_alloc(int len)
{
	const int max_len = ARRAY_SIZE((struct bpf_bprintf_buffers){}.buf);
	struct bpf_stream_elem *elem;
	unsigned long flags;

	BUILD_BUG_ON(max_len > BPF_STREAM_PAGE_SZ);
	/*
	 * Length denotes the amount of data to be written as part of stream element,
	 * thus includes '\0' byte. We're capped by how much bpf_bprintf_buffers can
	 * accomodate, therefore deny allocations that won't fit into them.
	 */
	if (len < 0 || len > max_len)
		return NULL;

	if (!bpf_stream_page_local_lock(&flags))
		return NULL;
	elem = bpf_stream_page_reserve_elem(len);
	bpf_stream_page_local_unlock(&flags);
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

static void bpf_stream_release_capacity(struct bpf_stream *stream, struct bpf_stream_elem *elem)
{
	int len = elem->total_len;

	atomic_sub(len, &stream->capacity);
}

static int bpf_stream_push_str(struct bpf_stream *stream, const char *str, int len)
{
	int ret = bpf_stream_consume_capacity(stream, len);

	return ret ?: __bpf_stream_push_str(&stream->log, str, len);
}

static struct bpf_stream *bpf_stream_get(enum bpf_stream_id stream_id, struct bpf_prog_aux *aux)
{
	if (stream_id != BPF_STDOUT && stream_id != BPF_STDERR)
		return NULL;
	return &aux->stream[stream_id - 1];
}

static void bpf_stream_free_elem(struct bpf_stream_elem *elem)
{
	struct bpf_stream_page *p;

	p = bpf_stream_page_from_elem(elem);
	bpf_stream_page_put(p);
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
	int rem_len = len, cons_len, ret = 0;
	struct bpf_stream_elem *elem = NULL;
	struct llist_node *node;

	mutex_lock(&stream->lock);

	while (rem_len) {
		int pos = len - rem_len;
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

		ret = copy_to_user(buf + pos, elem->str + cons_len,
				   elem->consumed_len - cons_len);
		/* Restore in case of error. */
		if (ret) {
			ret = -EFAULT;
			elem->consumed_len = cons_len;
			break;
		}

		if (cont)
			continue;
		bpf_stream_backlog_pop(stream);
		bpf_stream_release_capacity(stream, elem);
		bpf_stream_free_elem(elem);
	}

	mutex_unlock(&stream->lock);
	return ret ? ret : len - rem_len;
}

int bpf_prog_stream_read(struct bpf_prog *prog, enum bpf_stream_id stream_id, void __user *buf, int len)
{
	struct bpf_stream *stream;

	stream = bpf_stream_get(stream_id, prog->aux);
	if (!stream)
		return -ENOENT;
	return bpf_stream_read(stream, buf, len);
}

__bpf_kfunc_start_defs();

/*
 * Avoid using enum bpf_stream_id so that kfunc users don't have to pull in the
 * enum in headers.
 */
__bpf_kfunc int bpf_stream_vprintk(int stream_id, const char *fmt__str, const void *args, u32 len__sz, void *aux__prog)
{
	struct bpf_bprintf_data data = {
		.get_bin_args	= true,
		.get_buf	= true,
	};
	struct bpf_prog_aux *aux = aux__prog;
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
	/* Exclude NULL byte during push. */
	ret = bpf_stream_push_str(stream, data.buf, ret);
	bpf_bprintf_cleanup(&data);

	return ret;
}

__bpf_kfunc_end_defs();

/* Added kfunc to common_btf_ids */

void bpf_prog_stream_init(struct bpf_prog *prog)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(prog->aux->stream); i++) {
		atomic_set(&prog->aux->stream[i].capacity, 0);
		init_llist_head(&prog->aux->stream[i].log);
		mutex_init(&prog->aux->stream[i].lock);
		prog->aux->stream[i].backlog_head = NULL;
		prog->aux->stream[i].backlog_tail = NULL;
	}
}

void bpf_prog_stream_free(struct bpf_prog *prog)
{
	struct llist_node *list;
	int i;

	for (i = 0; i < ARRAY_SIZE(prog->aux->stream); i++) {
		list = llist_del_all(&prog->aux->stream[i].log);
		bpf_stream_free_list(list);
		bpf_stream_free_list(prog->aux->stream[i].backlog_head);
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
	int ret;

	if (bpf_try_get_buffers(&buf))
		return -EBUSY;

	va_start(args, fmt);
	ret = vsnprintf(buf->buf, ARRAY_SIZE(buf->buf), fmt, args);
	va_end(args);
	ss->len += ret;
	/* Exclude NULL byte during push. */
	ret = __bpf_stream_push_str(&ss->log, buf->buf, ret);
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
