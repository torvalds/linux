// SPDX-License-Identifier: GPL-2.0
/*
 * uprobes-based tracing events
 *
 * Copyright (C) IBM Corporation, 2010-2012
 * Author:	Srikar Dronamraju <srikar@linux.vnet.ibm.com>
 */
#define pr_fmt(fmt)	"trace_uprobe: " fmt

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/uprobes.h>
#include <linux/namei.h>
#include <linux/string.h>
#include <linux/rculist.h>

#include "trace_dynevent.h"
#include "trace_probe.h"
#include "trace_probe_tmpl.h"

#define UPROBE_EVENT_SYSTEM	"uprobes"

struct uprobe_trace_entry_head {
	struct trace_entry	ent;
	unsigned long		vaddr[];
};

#define SIZEOF_TRACE_ENTRY(is_return)			\
	(sizeof(struct uprobe_trace_entry_head) +	\
	 sizeof(unsigned long) * (is_return ? 2 : 1))

#define DATAOF_TRACE_ENTRY(entry, is_return)		\
	((void*)(entry) + SIZEOF_TRACE_ENTRY(is_return))

struct trace_uprobe_filter {
	rwlock_t		rwlock;
	int			nr_systemwide;
	struct list_head	perf_events;
};

static int trace_uprobe_create(int argc, const char **argv);
static int trace_uprobe_show(struct seq_file *m, struct dyn_event *ev);
static int trace_uprobe_release(struct dyn_event *ev);
static bool trace_uprobe_is_busy(struct dyn_event *ev);
static bool trace_uprobe_match(const char *system, const char *event,
			       struct dyn_event *ev);

static struct dyn_event_operations trace_uprobe_ops = {
	.create = trace_uprobe_create,
	.show = trace_uprobe_show,
	.is_busy = trace_uprobe_is_busy,
	.free = trace_uprobe_release,
	.match = trace_uprobe_match,
};

/*
 * uprobe event core functions
 */
struct trace_uprobe {
	struct dyn_event		devent;
	struct trace_uprobe_filter	filter;
	struct uprobe_consumer		consumer;
	struct path			path;
	struct inode			*inode;
	char				*filename;
	unsigned long			offset;
	unsigned long			ref_ctr_offset;
	unsigned long			nhit;
	struct trace_probe		tp;
};

static bool is_trace_uprobe(struct dyn_event *ev)
{
	return ev->ops == &trace_uprobe_ops;
}

static struct trace_uprobe *to_trace_uprobe(struct dyn_event *ev)
{
	return container_of(ev, struct trace_uprobe, devent);
}

/**
 * for_each_trace_uprobe - iterate over the trace_uprobe list
 * @pos:	the struct trace_uprobe * for each entry
 * @dpos:	the struct dyn_event * to use as a loop cursor
 */
#define for_each_trace_uprobe(pos, dpos)	\
	for_each_dyn_event(dpos)		\
		if (is_trace_uprobe(dpos) && (pos = to_trace_uprobe(dpos)))

#define SIZEOF_TRACE_UPROBE(n)				\
	(offsetof(struct trace_uprobe, tp.args) +	\
	(sizeof(struct probe_arg) * (n)))

static int register_uprobe_event(struct trace_uprobe *tu);
static int unregister_uprobe_event(struct trace_uprobe *tu);

struct uprobe_dispatch_data {
	struct trace_uprobe	*tu;
	unsigned long		bp_addr;
};

static int uprobe_dispatcher(struct uprobe_consumer *con, struct pt_regs *regs);
static int uretprobe_dispatcher(struct uprobe_consumer *con,
				unsigned long func, struct pt_regs *regs);

#ifdef CONFIG_STACK_GROWSUP
static unsigned long adjust_stack_addr(unsigned long addr, unsigned int n)
{
	return addr - (n * sizeof(long));
}
#else
static unsigned long adjust_stack_addr(unsigned long addr, unsigned int n)
{
	return addr + (n * sizeof(long));
}
#endif

static unsigned long get_user_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long ret;
	unsigned long addr = user_stack_pointer(regs);

	addr = adjust_stack_addr(addr, n);

	if (copy_from_user(&ret, (void __force __user *) addr, sizeof(ret)))
		return 0;

	return ret;
}

/*
 * Uprobes-specific fetch functions
 */
static nokprobe_inline int
probe_mem_read(void *dest, void *src, size_t size)
{
	void __user *vaddr = (void __force __user *)src;

	return copy_from_user(dest, vaddr, size) ? -EFAULT : 0;
}
/*
 * Fetch a null-terminated string. Caller MUST set *(u32 *)dest with max
 * length and relative data location.
 */
static nokprobe_inline int
fetch_store_string(unsigned long addr, void *dest, void *base)
{
	long ret;
	u32 loc = *(u32 *)dest;
	int maxlen  = get_loc_len(loc);
	u8 *dst = get_loc_data(dest, base);
	void __user *src = (void __force __user *) addr;

	if (unlikely(!maxlen))
		return -ENOMEM;

	ret = strncpy_from_user(dst, src, maxlen);
	if (ret >= 0) {
		if (ret == maxlen)
			dst[ret - 1] = '\0';
		else
			/*
			 * Include the terminating null byte. In this case it
			 * was copied by strncpy_from_user but not accounted
			 * for in ret.
			 */
			ret++;
		*(u32 *)dest = make_data_loc(ret, (void *)dst - base);
	}

	return ret;
}

/* Return the length of string -- including null terminal byte */
static nokprobe_inline int
fetch_store_strlen(unsigned long addr)
{
	int len;
	void __user *vaddr = (void __force __user *) addr;

	len = strnlen_user(vaddr, MAX_STRING_SIZE);

	return (len > MAX_STRING_SIZE) ? 0 : len;
}

static unsigned long translate_user_vaddr(unsigned long file_offset)
{
	unsigned long base_addr;
	struct uprobe_dispatch_data *udd;

	udd = (void *) current->utask->vaddr;

	base_addr = udd->bp_addr - udd->tu->offset;
	return base_addr + file_offset;
}

/* Note that we don't verify it, since the code does not come from user space */
static int
process_fetch_insn(struct fetch_insn *code, struct pt_regs *regs, void *dest,
		   void *base)
{
	unsigned long val;

	/* 1st stage: get value from context */
	switch (code->op) {
	case FETCH_OP_REG:
		val = regs_get_register(regs, code->param);
		break;
	case FETCH_OP_STACK:
		val = get_user_stack_nth(regs, code->param);
		break;
	case FETCH_OP_STACKP:
		val = user_stack_pointer(regs);
		break;
	case FETCH_OP_RETVAL:
		val = regs_return_value(regs);
		break;
	case FETCH_OP_IMM:
		val = code->immediate;
		break;
	case FETCH_OP_FOFFS:
		val = translate_user_vaddr(code->immediate);
		break;
	default:
		return -EILSEQ;
	}
	code++;

	return process_fetch_insn_bottom(code, val, dest, base);
}
NOKPROBE_SYMBOL(process_fetch_insn)

static inline void init_trace_uprobe_filter(struct trace_uprobe_filter *filter)
{
	rwlock_init(&filter->rwlock);
	filter->nr_systemwide = 0;
	INIT_LIST_HEAD(&filter->perf_events);
}

static inline bool uprobe_filter_is_empty(struct trace_uprobe_filter *filter)
{
	return !filter->nr_systemwide && list_empty(&filter->perf_events);
}

static inline bool is_ret_probe(struct trace_uprobe *tu)
{
	return tu->consumer.ret_handler != NULL;
}

static bool trace_uprobe_is_busy(struct dyn_event *ev)
{
	struct trace_uprobe *tu = to_trace_uprobe(ev);

	return trace_probe_is_enabled(&tu->tp);
}

static bool trace_uprobe_match(const char *system, const char *event,
			       struct dyn_event *ev)
{
	struct trace_uprobe *tu = to_trace_uprobe(ev);

	return strcmp(trace_event_name(&tu->tp.call), event) == 0 &&
		(!system || strcmp(tu->tp.call.class->system, system) == 0);
}

/*
 * Allocate new trace_uprobe and initialize it (including uprobes).
 */
static struct trace_uprobe *
alloc_trace_uprobe(const char *group, const char *event, int nargs, bool is_ret)
{
	struct trace_uprobe *tu;

	if (!event || !group)
		return ERR_PTR(-EINVAL);

	tu = kzalloc(SIZEOF_TRACE_UPROBE(nargs), GFP_KERNEL);
	if (!tu)
		return ERR_PTR(-ENOMEM);

	tu->tp.call.class = &tu->tp.class;
	tu->tp.call.name = kstrdup(event, GFP_KERNEL);
	if (!tu->tp.call.name)
		goto error;

	tu->tp.class.system = kstrdup(group, GFP_KERNEL);
	if (!tu->tp.class.system)
		goto error;

	dyn_event_init(&tu->devent, &trace_uprobe_ops);
	INIT_LIST_HEAD(&tu->tp.files);
	tu->consumer.handler = uprobe_dispatcher;
	if (is_ret)
		tu->consumer.ret_handler = uretprobe_dispatcher;
	init_trace_uprobe_filter(&tu->filter);
	return tu;

error:
	kfree(tu->tp.call.name);
	kfree(tu);

	return ERR_PTR(-ENOMEM);
}

static void free_trace_uprobe(struct trace_uprobe *tu)
{
	int i;

	if (!tu)
		return;

	for (i = 0; i < tu->tp.nr_args; i++)
		traceprobe_free_probe_arg(&tu->tp.args[i]);

	path_put(&tu->path);
	kfree(tu->tp.call.class->system);
	kfree(tu->tp.call.name);
	kfree(tu->filename);
	kfree(tu);
}

static struct trace_uprobe *find_probe_event(const char *event, const char *group)
{
	struct dyn_event *pos;
	struct trace_uprobe *tu;

	for_each_trace_uprobe(tu, pos)
		if (strcmp(trace_event_name(&tu->tp.call), event) == 0 &&
		    strcmp(tu->tp.call.class->system, group) == 0)
			return tu;

	return NULL;
}

/* Unregister a trace_uprobe and probe_event */
static int unregister_trace_uprobe(struct trace_uprobe *tu)
{
	int ret;

	ret = unregister_uprobe_event(tu);
	if (ret)
		return ret;

	dyn_event_remove(&tu->devent);
	free_trace_uprobe(tu);
	return 0;
}

/*
 * Uprobe with multiple reference counter is not allowed. i.e.
 * If inode and offset matches, reference counter offset *must*
 * match as well. Though, there is one exception: If user is
 * replacing old trace_uprobe with new one(same group/event),
 * then we allow same uprobe with new reference counter as far
 * as the new one does not conflict with any other existing
 * ones.
 */
static struct trace_uprobe *find_old_trace_uprobe(struct trace_uprobe *new)
{
	struct dyn_event *pos;
	struct trace_uprobe *tmp, *old = NULL;
	struct inode *new_inode = d_real_inode(new->path.dentry);

	old = find_probe_event(trace_event_name(&new->tp.call),
				new->tp.call.class->system);

	for_each_trace_uprobe(tmp, pos) {
		if ((old ? old != tmp : true) &&
		    new_inode == d_real_inode(tmp->path.dentry) &&
		    new->offset == tmp->offset &&
		    new->ref_ctr_offset != tmp->ref_ctr_offset) {
			pr_warn("Reference counter offset mismatch.");
			return ERR_PTR(-EINVAL);
		}
	}
	return old;
}

/* Register a trace_uprobe and probe_event */
static int register_trace_uprobe(struct trace_uprobe *tu)
{
	struct trace_uprobe *old_tu;
	int ret;

	mutex_lock(&event_mutex);

	/* register as an event */
	old_tu = find_old_trace_uprobe(tu);
	if (IS_ERR(old_tu)) {
		ret = PTR_ERR(old_tu);
		goto end;
	}

	if (old_tu) {
		/* delete old event */
		ret = unregister_trace_uprobe(old_tu);
		if (ret)
			goto end;
	}

	ret = register_uprobe_event(tu);
	if (ret) {
		pr_warn("Failed to register probe event(%d)\n", ret);
		goto end;
	}

	dyn_event_add(&tu->devent);

end:
	mutex_unlock(&event_mutex);

	return ret;
}

/*
 * Argument syntax:
 *  - Add uprobe: p|r[:[GRP/]EVENT] PATH:OFFSET [FETCHARGS]
 *
 *  - Remove uprobe: -:[GRP/]EVENT
 */
static int trace_uprobe_create(int argc, const char **argv)
{
	struct trace_uprobe *tu;
	const char *event = NULL, *group = UPROBE_EVENT_SYSTEM;
	char *arg, *filename, *rctr, *rctr_end, *tmp;
	char buf[MAX_EVENT_NAME_LEN];
	struct path path;
	unsigned long offset, ref_ctr_offset;
	bool is_return = false;
	int i, ret;

	ret = 0;
	ref_ctr_offset = 0;

	/* argc must be >= 1 */
	if (argv[0][0] == 'r')
		is_return = true;
	else if (argv[0][0] != 'p' || argc < 2)
		return -ECANCELED;

	if (argv[0][1] == ':')
		event = &argv[0][2];

	if (!strchr(argv[1], '/'))
		return -ECANCELED;

	filename = kstrdup(argv[1], GFP_KERNEL);
	if (!filename)
		return -ENOMEM;

	/* Find the last occurrence, in case the path contains ':' too. */
	arg = strrchr(filename, ':');
	if (!arg || !isdigit(arg[1])) {
		kfree(filename);
		return -ECANCELED;
	}

	trace_probe_log_init("trace_uprobe", argc, argv);
	trace_probe_log_set_index(1);	/* filename is the 2nd argument */

	*arg++ = '\0';
	ret = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (ret) {
		trace_probe_log_err(0, FILE_NOT_FOUND);
		kfree(filename);
		trace_probe_log_clear();
		return ret;
	}
	if (!d_is_reg(path.dentry)) {
		trace_probe_log_err(0, NO_REGULAR_FILE);
		ret = -EINVAL;
		goto fail_address_parse;
	}

	/* Parse reference counter offset if specified. */
	rctr = strchr(arg, '(');
	if (rctr) {
		rctr_end = strchr(rctr, ')');
		if (!rctr_end) {
			ret = -EINVAL;
			rctr_end = rctr + strlen(rctr);
			trace_probe_log_err(rctr_end - filename,
					    REFCNT_OPEN_BRACE);
			goto fail_address_parse;
		} else if (rctr_end[1] != '\0') {
			ret = -EINVAL;
			trace_probe_log_err(rctr_end + 1 - filename,
					    BAD_REFCNT_SUFFIX);
			goto fail_address_parse;
		}

		*rctr++ = '\0';
		*rctr_end = '\0';
		ret = kstrtoul(rctr, 0, &ref_ctr_offset);
		if (ret) {
			trace_probe_log_err(rctr - filename, BAD_REFCNT);
			goto fail_address_parse;
		}
	}

	/* Parse uprobe offset. */
	ret = kstrtoul(arg, 0, &offset);
	if (ret) {
		trace_probe_log_err(arg - filename, BAD_UPROBE_OFFS);
		goto fail_address_parse;
	}

	/* setup a probe */
	trace_probe_log_set_index(0);
	if (event) {
		ret = traceprobe_parse_event_name(&event, &group, buf,
						  event - argv[0]);
		if (ret)
			goto fail_address_parse;
	} else {
		char *tail;
		char *ptr;

		tail = kstrdup(kbasename(filename), GFP_KERNEL);
		if (!tail) {
			ret = -ENOMEM;
			goto fail_address_parse;
		}

		ptr = strpbrk(tail, ".-_");
		if (ptr)
			*ptr = '\0';

		snprintf(buf, MAX_EVENT_NAME_LEN, "%c_%s_0x%lx", 'p', tail, offset);
		event = buf;
		kfree(tail);
	}

	argc -= 2;
	argv += 2;

	tu = alloc_trace_uprobe(group, event, argc, is_return);
	if (IS_ERR(tu)) {
		ret = PTR_ERR(tu);
		/* This must return -ENOMEM otherwise there is a bug */
		WARN_ON_ONCE(ret != -ENOMEM);
		goto fail_address_parse;
	}
	tu->offset = offset;
	tu->ref_ctr_offset = ref_ctr_offset;
	tu->path = path;
	tu->filename = filename;

	/* parse arguments */
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		tmp = kstrdup(argv[i], GFP_KERNEL);
		if (!tmp) {
			ret = -ENOMEM;
			goto error;
		}

		trace_probe_log_set_index(i + 2);
		ret = traceprobe_parse_probe_arg(&tu->tp, i, tmp,
					is_return ? TPARG_FL_RETURN : 0);
		kfree(tmp);
		if (ret)
			goto error;
	}

	ret = register_trace_uprobe(tu);
	if (!ret)
		goto out;

error:
	free_trace_uprobe(tu);
out:
	trace_probe_log_clear();
	return ret;

fail_address_parse:
	trace_probe_log_clear();
	path_put(&path);
	kfree(filename);

	return ret;
}

static int create_or_delete_trace_uprobe(int argc, char **argv)
{
	int ret;

	if (argv[0][0] == '-')
		return dyn_event_release(argc, argv, &trace_uprobe_ops);

	ret = trace_uprobe_create(argc, (const char **)argv);
	return ret == -ECANCELED ? -EINVAL : ret;
}

static int trace_uprobe_release(struct dyn_event *ev)
{
	struct trace_uprobe *tu = to_trace_uprobe(ev);

	return unregister_trace_uprobe(tu);
}

/* Probes listing interfaces */
static int trace_uprobe_show(struct seq_file *m, struct dyn_event *ev)
{
	struct trace_uprobe *tu = to_trace_uprobe(ev);
	char c = is_ret_probe(tu) ? 'r' : 'p';
	int i;

	seq_printf(m, "%c:%s/%s %s:0x%0*lx", c, tu->tp.call.class->system,
			trace_event_name(&tu->tp.call), tu->filename,
			(int)(sizeof(void *) * 2), tu->offset);

	if (tu->ref_ctr_offset)
		seq_printf(m, "(0x%lx)", tu->ref_ctr_offset);

	for (i = 0; i < tu->tp.nr_args; i++)
		seq_printf(m, " %s=%s", tu->tp.args[i].name, tu->tp.args[i].comm);

	seq_putc(m, '\n');
	return 0;
}

static int probes_seq_show(struct seq_file *m, void *v)
{
	struct dyn_event *ev = v;

	if (!is_trace_uprobe(ev))
		return 0;

	return trace_uprobe_show(m, ev);
}

static const struct seq_operations probes_seq_op = {
	.start  = dyn_event_seq_start,
	.next   = dyn_event_seq_next,
	.stop   = dyn_event_seq_stop,
	.show   = probes_seq_show
};

static int probes_open(struct inode *inode, struct file *file)
{
	int ret;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		ret = dyn_events_release_all(&trace_uprobe_ops);
		if (ret)
			return ret;
	}

	return seq_open(file, &probes_seq_op);
}

static ssize_t probes_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	return trace_parse_run_command(file, buffer, count, ppos,
					create_or_delete_trace_uprobe);
}

static const struct file_operations uprobe_events_ops = {
	.owner		= THIS_MODULE,
	.open		= probes_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.write		= probes_write,
};

/* Probes profiling interfaces */
static int probes_profile_seq_show(struct seq_file *m, void *v)
{
	struct dyn_event *ev = v;
	struct trace_uprobe *tu;

	if (!is_trace_uprobe(ev))
		return 0;

	tu = to_trace_uprobe(ev);
	seq_printf(m, "  %s %-44s %15lu\n", tu->filename,
			trace_event_name(&tu->tp.call), tu->nhit);
	return 0;
}

static const struct seq_operations profile_seq_op = {
	.start  = dyn_event_seq_start,
	.next   = dyn_event_seq_next,
	.stop   = dyn_event_seq_stop,
	.show	= probes_profile_seq_show
};

static int profile_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &profile_seq_op);
}

static const struct file_operations uprobe_profile_ops = {
	.owner		= THIS_MODULE,
	.open		= profile_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

struct uprobe_cpu_buffer {
	struct mutex mutex;
	void *buf;
};
static struct uprobe_cpu_buffer __percpu *uprobe_cpu_buffer;
static int uprobe_buffer_refcnt;

static int uprobe_buffer_init(void)
{
	int cpu, err_cpu;

	uprobe_cpu_buffer = alloc_percpu(struct uprobe_cpu_buffer);
	if (uprobe_cpu_buffer == NULL)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		struct page *p = alloc_pages_node(cpu_to_node(cpu),
						  GFP_KERNEL, 0);
		if (p == NULL) {
			err_cpu = cpu;
			goto err;
		}
		per_cpu_ptr(uprobe_cpu_buffer, cpu)->buf = page_address(p);
		mutex_init(&per_cpu_ptr(uprobe_cpu_buffer, cpu)->mutex);
	}

	return 0;

err:
	for_each_possible_cpu(cpu) {
		if (cpu == err_cpu)
			break;
		free_page((unsigned long)per_cpu_ptr(uprobe_cpu_buffer, cpu)->buf);
	}

	free_percpu(uprobe_cpu_buffer);
	return -ENOMEM;
}

static int uprobe_buffer_enable(void)
{
	int ret = 0;

	BUG_ON(!mutex_is_locked(&event_mutex));

	if (uprobe_buffer_refcnt++ == 0) {
		ret = uprobe_buffer_init();
		if (ret < 0)
			uprobe_buffer_refcnt--;
	}

	return ret;
}

static void uprobe_buffer_disable(void)
{
	int cpu;

	BUG_ON(!mutex_is_locked(&event_mutex));

	if (--uprobe_buffer_refcnt == 0) {
		for_each_possible_cpu(cpu)
			free_page((unsigned long)per_cpu_ptr(uprobe_cpu_buffer,
							     cpu)->buf);

		free_percpu(uprobe_cpu_buffer);
		uprobe_cpu_buffer = NULL;
	}
}

static struct uprobe_cpu_buffer *uprobe_buffer_get(void)
{
	struct uprobe_cpu_buffer *ucb;
	int cpu;

	cpu = raw_smp_processor_id();
	ucb = per_cpu_ptr(uprobe_cpu_buffer, cpu);

	/*
	 * Use per-cpu buffers for fastest access, but we might migrate
	 * so the mutex makes sure we have sole access to it.
	 */
	mutex_lock(&ucb->mutex);

	return ucb;
}

static void uprobe_buffer_put(struct uprobe_cpu_buffer *ucb)
{
	mutex_unlock(&ucb->mutex);
}

static void __uprobe_trace_func(struct trace_uprobe *tu,
				unsigned long func, struct pt_regs *regs,
				struct uprobe_cpu_buffer *ucb, int dsize,
				struct trace_event_file *trace_file)
{
	struct uprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	void *data;
	int size, esize;
	struct trace_event_call *call = &tu->tp.call;

	WARN_ON(call != trace_file->event_call);

	if (WARN_ON_ONCE(tu->tp.size + dsize > PAGE_SIZE))
		return;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	esize = SIZEOF_TRACE_ENTRY(is_ret_probe(tu));
	size = esize + tu->tp.size + dsize;
	event = trace_event_buffer_lock_reserve(&buffer, trace_file,
						call->event.type, size, 0, 0);
	if (!event)
		return;

	entry = ring_buffer_event_data(event);
	if (is_ret_probe(tu)) {
		entry->vaddr[0] = func;
		entry->vaddr[1] = instruction_pointer(regs);
		data = DATAOF_TRACE_ENTRY(entry, true);
	} else {
		entry->vaddr[0] = instruction_pointer(regs);
		data = DATAOF_TRACE_ENTRY(entry, false);
	}

	memcpy(data, ucb->buf, tu->tp.size + dsize);

	event_trigger_unlock_commit(trace_file, buffer, event, entry, 0, 0);
}

/* uprobe handler */
static int uprobe_trace_func(struct trace_uprobe *tu, struct pt_regs *regs,
			     struct uprobe_cpu_buffer *ucb, int dsize)
{
	struct event_file_link *link;

	if (is_ret_probe(tu))
		return 0;

	rcu_read_lock();
	list_for_each_entry_rcu(link, &tu->tp.files, list)
		__uprobe_trace_func(tu, 0, regs, ucb, dsize, link->file);
	rcu_read_unlock();

	return 0;
}

static void uretprobe_trace_func(struct trace_uprobe *tu, unsigned long func,
				 struct pt_regs *regs,
				 struct uprobe_cpu_buffer *ucb, int dsize)
{
	struct event_file_link *link;

	rcu_read_lock();
	list_for_each_entry_rcu(link, &tu->tp.files, list)
		__uprobe_trace_func(tu, func, regs, ucb, dsize, link->file);
	rcu_read_unlock();
}

/* Event entry printers */
static enum print_line_t
print_uprobe_event(struct trace_iterator *iter, int flags, struct trace_event *event)
{
	struct uprobe_trace_entry_head *entry;
	struct trace_seq *s = &iter->seq;
	struct trace_uprobe *tu;
	u8 *data;

	entry = (struct uprobe_trace_entry_head *)iter->ent;
	tu = container_of(event, struct trace_uprobe, tp.call.event);

	if (is_ret_probe(tu)) {
		trace_seq_printf(s, "%s: (0x%lx <- 0x%lx)",
				 trace_event_name(&tu->tp.call),
				 entry->vaddr[1], entry->vaddr[0]);
		data = DATAOF_TRACE_ENTRY(entry, true);
	} else {
		trace_seq_printf(s, "%s: (0x%lx)",
				 trace_event_name(&tu->tp.call),
				 entry->vaddr[0]);
		data = DATAOF_TRACE_ENTRY(entry, false);
	}

	if (print_probe_args(s, tu->tp.args, tu->tp.nr_args, data, entry) < 0)
		goto out;

	trace_seq_putc(s, '\n');

 out:
	return trace_handle_return(s);
}

typedef bool (*filter_func_t)(struct uprobe_consumer *self,
				enum uprobe_filter_ctx ctx,
				struct mm_struct *mm);

static int
probe_event_enable(struct trace_uprobe *tu, struct trace_event_file *file,
		   filter_func_t filter)
{
	bool enabled = trace_probe_is_enabled(&tu->tp);
	struct event_file_link *link = NULL;
	int ret;

	if (file) {
		if (tu->tp.flags & TP_FLAG_PROFILE)
			return -EINTR;

		link = kmalloc(sizeof(*link), GFP_KERNEL);
		if (!link)
			return -ENOMEM;

		link->file = file;
		list_add_tail_rcu(&link->list, &tu->tp.files);

		tu->tp.flags |= TP_FLAG_TRACE;
	} else {
		if (tu->tp.flags & TP_FLAG_TRACE)
			return -EINTR;

		tu->tp.flags |= TP_FLAG_PROFILE;
	}

	WARN_ON(!uprobe_filter_is_empty(&tu->filter));

	if (enabled)
		return 0;

	ret = uprobe_buffer_enable();
	if (ret)
		goto err_flags;

	tu->consumer.filter = filter;
	tu->inode = d_real_inode(tu->path.dentry);
	if (tu->ref_ctr_offset) {
		ret = uprobe_register_refctr(tu->inode, tu->offset,
				tu->ref_ctr_offset, &tu->consumer);
	} else {
		ret = uprobe_register(tu->inode, tu->offset, &tu->consumer);
	}

	if (ret)
		goto err_buffer;

	return 0;

 err_buffer:
	uprobe_buffer_disable();

 err_flags:
	if (file) {
		list_del(&link->list);
		kfree(link);
		tu->tp.flags &= ~TP_FLAG_TRACE;
	} else {
		tu->tp.flags &= ~TP_FLAG_PROFILE;
	}
	return ret;
}

static void
probe_event_disable(struct trace_uprobe *tu, struct trace_event_file *file)
{
	if (!trace_probe_is_enabled(&tu->tp))
		return;

	if (file) {
		struct event_file_link *link;

		link = find_event_file_link(&tu->tp, file);
		if (!link)
			return;

		list_del_rcu(&link->list);
		/* synchronize with u{,ret}probe_trace_func */
		synchronize_rcu();
		kfree(link);

		if (!list_empty(&tu->tp.files))
			return;
	}

	WARN_ON(!uprobe_filter_is_empty(&tu->filter));

	uprobe_unregister(tu->inode, tu->offset, &tu->consumer);
	tu->inode = NULL;
	tu->tp.flags &= file ? ~TP_FLAG_TRACE : ~TP_FLAG_PROFILE;

	uprobe_buffer_disable();
}

static int uprobe_event_define_fields(struct trace_event_call *event_call)
{
	int ret, size;
	struct uprobe_trace_entry_head field;
	struct trace_uprobe *tu = event_call->data;

	if (is_ret_probe(tu)) {
		DEFINE_FIELD(unsigned long, vaddr[0], FIELD_STRING_FUNC, 0);
		DEFINE_FIELD(unsigned long, vaddr[1], FIELD_STRING_RETIP, 0);
		size = SIZEOF_TRACE_ENTRY(true);
	} else {
		DEFINE_FIELD(unsigned long, vaddr[0], FIELD_STRING_IP, 0);
		size = SIZEOF_TRACE_ENTRY(false);
	}

	return traceprobe_define_arg_fields(event_call, size, &tu->tp);
}

#ifdef CONFIG_PERF_EVENTS
static bool
__uprobe_perf_filter(struct trace_uprobe_filter *filter, struct mm_struct *mm)
{
	struct perf_event *event;

	if (filter->nr_systemwide)
		return true;

	list_for_each_entry(event, &filter->perf_events, hw.tp_list) {
		if (event->hw.target->mm == mm)
			return true;
	}

	return false;
}

static inline bool
uprobe_filter_event(struct trace_uprobe *tu, struct perf_event *event)
{
	return __uprobe_perf_filter(&tu->filter, event->hw.target->mm);
}

static int uprobe_perf_close(struct trace_uprobe *tu, struct perf_event *event)
{
	bool done;

	write_lock(&tu->filter.rwlock);
	if (event->hw.target) {
		list_del(&event->hw.tp_list);
		done = tu->filter.nr_systemwide ||
			(event->hw.target->flags & PF_EXITING) ||
			uprobe_filter_event(tu, event);
	} else {
		tu->filter.nr_systemwide--;
		done = tu->filter.nr_systemwide;
	}
	write_unlock(&tu->filter.rwlock);

	if (!done)
		return uprobe_apply(tu->inode, tu->offset, &tu->consumer, false);

	return 0;
}

static int uprobe_perf_open(struct trace_uprobe *tu, struct perf_event *event)
{
	bool done;
	int err;

	write_lock(&tu->filter.rwlock);
	if (event->hw.target) {
		/*
		 * event->parent != NULL means copy_process(), we can avoid
		 * uprobe_apply(). current->mm must be probed and we can rely
		 * on dup_mmap() which preserves the already installed bp's.
		 *
		 * attr.enable_on_exec means that exec/mmap will install the
		 * breakpoints we need.
		 */
		done = tu->filter.nr_systemwide ||
			event->parent || event->attr.enable_on_exec ||
			uprobe_filter_event(tu, event);
		list_add(&event->hw.tp_list, &tu->filter.perf_events);
	} else {
		done = tu->filter.nr_systemwide;
		tu->filter.nr_systemwide++;
	}
	write_unlock(&tu->filter.rwlock);

	err = 0;
	if (!done) {
		err = uprobe_apply(tu->inode, tu->offset, &tu->consumer, true);
		if (err)
			uprobe_perf_close(tu, event);
	}
	return err;
}

static bool uprobe_perf_filter(struct uprobe_consumer *uc,
				enum uprobe_filter_ctx ctx, struct mm_struct *mm)
{
	struct trace_uprobe *tu;
	int ret;

	tu = container_of(uc, struct trace_uprobe, consumer);
	read_lock(&tu->filter.rwlock);
	ret = __uprobe_perf_filter(&tu->filter, mm);
	read_unlock(&tu->filter.rwlock);

	return ret;
}

static void __uprobe_perf_func(struct trace_uprobe *tu,
			       unsigned long func, struct pt_regs *regs,
			       struct uprobe_cpu_buffer *ucb, int dsize)
{
	struct trace_event_call *call = &tu->tp.call;
	struct uprobe_trace_entry_head *entry;
	struct hlist_head *head;
	void *data;
	int size, esize;
	int rctx;

	if (bpf_prog_array_valid(call) && !trace_call_bpf(call, regs))
		return;

	esize = SIZEOF_TRACE_ENTRY(is_ret_probe(tu));

	size = esize + tu->tp.size + dsize;
	size = ALIGN(size + sizeof(u32), sizeof(u64)) - sizeof(u32);
	if (WARN_ONCE(size > PERF_MAX_TRACE_SIZE, "profile buffer not large enough"))
		return;

	preempt_disable();
	head = this_cpu_ptr(call->perf_events);
	if (hlist_empty(head))
		goto out;

	entry = perf_trace_buf_alloc(size, NULL, &rctx);
	if (!entry)
		goto out;

	if (is_ret_probe(tu)) {
		entry->vaddr[0] = func;
		entry->vaddr[1] = instruction_pointer(regs);
		data = DATAOF_TRACE_ENTRY(entry, true);
	} else {
		entry->vaddr[0] = instruction_pointer(regs);
		data = DATAOF_TRACE_ENTRY(entry, false);
	}

	memcpy(data, ucb->buf, tu->tp.size + dsize);

	if (size - esize > tu->tp.size + dsize) {
		int len = tu->tp.size + dsize;

		memset(data + len, 0, size - esize - len);
	}

	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
 out:
	preempt_enable();
}

/* uprobe profile handler */
static int uprobe_perf_func(struct trace_uprobe *tu, struct pt_regs *regs,
			    struct uprobe_cpu_buffer *ucb, int dsize)
{
	if (!uprobe_perf_filter(&tu->consumer, 0, current->mm))
		return UPROBE_HANDLER_REMOVE;

	if (!is_ret_probe(tu))
		__uprobe_perf_func(tu, 0, regs, ucb, dsize);
	return 0;
}

static void uretprobe_perf_func(struct trace_uprobe *tu, unsigned long func,
				struct pt_regs *regs,
				struct uprobe_cpu_buffer *ucb, int dsize)
{
	__uprobe_perf_func(tu, func, regs, ucb, dsize);
}

int bpf_get_uprobe_info(const struct perf_event *event, u32 *fd_type,
			const char **filename, u64 *probe_offset,
			bool perf_type_tracepoint)
{
	const char *pevent = trace_event_name(event->tp_event);
	const char *group = event->tp_event->class->system;
	struct trace_uprobe *tu;

	if (perf_type_tracepoint)
		tu = find_probe_event(pevent, group);
	else
		tu = event->tp_event->data;
	if (!tu)
		return -EINVAL;

	*fd_type = is_ret_probe(tu) ? BPF_FD_TYPE_URETPROBE
				    : BPF_FD_TYPE_UPROBE;
	*filename = tu->filename;
	*probe_offset = tu->offset;
	return 0;
}
#endif	/* CONFIG_PERF_EVENTS */

static int
trace_uprobe_register(struct trace_event_call *event, enum trace_reg type,
		      void *data)
{
	struct trace_uprobe *tu = event->data;
	struct trace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return probe_event_enable(tu, file, NULL);

	case TRACE_REG_UNREGISTER:
		probe_event_disable(tu, file);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return probe_event_enable(tu, NULL, uprobe_perf_filter);

	case TRACE_REG_PERF_UNREGISTER:
		probe_event_disable(tu, NULL);
		return 0;

	case TRACE_REG_PERF_OPEN:
		return uprobe_perf_open(tu, data);

	case TRACE_REG_PERF_CLOSE:
		return uprobe_perf_close(tu, data);

#endif
	default:
		return 0;
	}
	return 0;
}

static int uprobe_dispatcher(struct uprobe_consumer *con, struct pt_regs *regs)
{
	struct trace_uprobe *tu;
	struct uprobe_dispatch_data udd;
	struct uprobe_cpu_buffer *ucb;
	int dsize, esize;
	int ret = 0;


	tu = container_of(con, struct trace_uprobe, consumer);
	tu->nhit++;

	udd.tu = tu;
	udd.bp_addr = instruction_pointer(regs);

	current->utask->vaddr = (unsigned long) &udd;

	if (WARN_ON_ONCE(!uprobe_cpu_buffer))
		return 0;

	dsize = __get_data_size(&tu->tp, regs);
	esize = SIZEOF_TRACE_ENTRY(is_ret_probe(tu));

	ucb = uprobe_buffer_get();
	store_trace_args(ucb->buf, &tu->tp, regs, esize, dsize);

	if (tu->tp.flags & TP_FLAG_TRACE)
		ret |= uprobe_trace_func(tu, regs, ucb, dsize);

#ifdef CONFIG_PERF_EVENTS
	if (tu->tp.flags & TP_FLAG_PROFILE)
		ret |= uprobe_perf_func(tu, regs, ucb, dsize);
#endif
	uprobe_buffer_put(ucb);
	return ret;
}

static int uretprobe_dispatcher(struct uprobe_consumer *con,
				unsigned long func, struct pt_regs *regs)
{
	struct trace_uprobe *tu;
	struct uprobe_dispatch_data udd;
	struct uprobe_cpu_buffer *ucb;
	int dsize, esize;

	tu = container_of(con, struct trace_uprobe, consumer);

	udd.tu = tu;
	udd.bp_addr = func;

	current->utask->vaddr = (unsigned long) &udd;

	if (WARN_ON_ONCE(!uprobe_cpu_buffer))
		return 0;

	dsize = __get_data_size(&tu->tp, regs);
	esize = SIZEOF_TRACE_ENTRY(is_ret_probe(tu));

	ucb = uprobe_buffer_get();
	store_trace_args(ucb->buf, &tu->tp, regs, esize, dsize);

	if (tu->tp.flags & TP_FLAG_TRACE)
		uretprobe_trace_func(tu, func, regs, ucb, dsize);

#ifdef CONFIG_PERF_EVENTS
	if (tu->tp.flags & TP_FLAG_PROFILE)
		uretprobe_perf_func(tu, func, regs, ucb, dsize);
#endif
	uprobe_buffer_put(ucb);
	return 0;
}

static struct trace_event_functions uprobe_funcs = {
	.trace		= print_uprobe_event
};

static inline void init_trace_event_call(struct trace_uprobe *tu,
					 struct trace_event_call *call)
{
	INIT_LIST_HEAD(&call->class->fields);
	call->event.funcs = &uprobe_funcs;
	call->class->define_fields = uprobe_event_define_fields;

	call->flags = TRACE_EVENT_FL_UPROBE;
	call->class->reg = trace_uprobe_register;
	call->data = tu;
}

static int register_uprobe_event(struct trace_uprobe *tu)
{
	struct trace_event_call *call = &tu->tp.call;
	int ret = 0;

	init_trace_event_call(tu, call);

	if (traceprobe_set_print_fmt(&tu->tp, is_ret_probe(tu)) < 0)
		return -ENOMEM;

	ret = register_trace_event(&call->event);
	if (!ret) {
		kfree(call->print_fmt);
		return -ENODEV;
	}

	ret = trace_add_event_call(call);

	if (ret) {
		pr_info("Failed to register uprobe event: %s\n",
			trace_event_name(call));
		kfree(call->print_fmt);
		unregister_trace_event(&call->event);
	}

	return ret;
}

static int unregister_uprobe_event(struct trace_uprobe *tu)
{
	int ret;

	/* tu->event is unregistered in trace_remove_event_call() */
	ret = trace_remove_event_call(&tu->tp.call);
	if (ret)
		return ret;
	kfree(tu->tp.call.print_fmt);
	tu->tp.call.print_fmt = NULL;
	return 0;
}

#ifdef CONFIG_PERF_EVENTS
struct trace_event_call *
create_local_trace_uprobe(char *name, unsigned long offs,
			  unsigned long ref_ctr_offset, bool is_return)
{
	struct trace_uprobe *tu;
	struct path path;
	int ret;

	ret = kern_path(name, LOOKUP_FOLLOW, &path);
	if (ret)
		return ERR_PTR(ret);

	if (!d_is_reg(path.dentry)) {
		path_put(&path);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * local trace_kprobes are not added to dyn_event, so they are never
	 * searched in find_trace_kprobe(). Therefore, there is no concern of
	 * duplicated name "DUMMY_EVENT" here.
	 */
	tu = alloc_trace_uprobe(UPROBE_EVENT_SYSTEM, "DUMMY_EVENT", 0,
				is_return);

	if (IS_ERR(tu)) {
		pr_info("Failed to allocate trace_uprobe.(%d)\n",
			(int)PTR_ERR(tu));
		path_put(&path);
		return ERR_CAST(tu);
	}

	tu->offset = offs;
	tu->path = path;
	tu->ref_ctr_offset = ref_ctr_offset;
	tu->filename = kstrdup(name, GFP_KERNEL);
	init_trace_event_call(tu, &tu->tp.call);

	if (traceprobe_set_print_fmt(&tu->tp, is_ret_probe(tu)) < 0) {
		ret = -ENOMEM;
		goto error;
	}

	return &tu->tp.call;
error:
	free_trace_uprobe(tu);
	return ERR_PTR(ret);
}

void destroy_local_trace_uprobe(struct trace_event_call *event_call)
{
	struct trace_uprobe *tu;

	tu = container_of(event_call, struct trace_uprobe, tp.call);

	kfree(tu->tp.call.print_fmt);
	tu->tp.call.print_fmt = NULL;

	free_trace_uprobe(tu);
}
#endif /* CONFIG_PERF_EVENTS */

/* Make a trace interface for controling probe points */
static __init int init_uprobe_trace(void)
{
	struct dentry *d_tracer;
	int ret;

	ret = dyn_event_register(&trace_uprobe_ops);
	if (ret)
		return ret;

	d_tracer = tracing_init_dentry();
	if (IS_ERR(d_tracer))
		return 0;

	trace_create_file("uprobe_events", 0644, d_tracer,
				    NULL, &uprobe_events_ops);
	/* Profile interface */
	trace_create_file("uprobe_profile", 0444, d_tracer,
				    NULL, &uprobe_profile_ops);
	return 0;
}

fs_initcall(init_uprobe_trace);
