/*
 * uprobes-based tracing events
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright (C) IBM Corporation, 2010-2012
 * Author:	Srikar Dronamraju <srikar@linux.vnet.ibm.com>
 */
#define pr_fmt(fmt)	"trace_kprobe: " fmt

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/uprobes.h>
#include <linux/namei.h>
#include <linux/string.h>
#include <linux/rculist.h>

#include "trace_probe.h"

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

/*
 * uprobe event core functions
 */
struct trace_uprobe {
	struct list_head		list;
	struct trace_uprobe_filter	filter;
	struct uprobe_consumer		consumer;
	struct inode			*inode;
	char				*filename;
	unsigned long			offset;
	unsigned long			nhit;
	struct trace_probe		tp;
};

#define SIZEOF_TRACE_UPROBE(n)				\
	(offsetof(struct trace_uprobe, tp.args) +	\
	(sizeof(struct probe_arg) * (n)))

static int register_uprobe_event(struct trace_uprobe *tu);
static int unregister_uprobe_event(struct trace_uprobe *tu);

static DEFINE_MUTEX(uprobe_lock);
static LIST_HEAD(uprobe_list);

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
#define DEFINE_FETCH_stack(type)					\
static void FETCH_FUNC_NAME(stack, type)(struct pt_regs *regs,		\
					 void *offset, void *dest)	\
{									\
	*(type *)dest = (type)get_user_stack_nth(regs,			\
					      ((unsigned long)offset)); \
}
DEFINE_BASIC_FETCH_FUNCS(stack)
/* No string on the stack entry */
#define fetch_stack_string	NULL
#define fetch_stack_string_size	NULL

#define DEFINE_FETCH_memory(type)					\
static void FETCH_FUNC_NAME(memory, type)(struct pt_regs *regs,		\
					  void *addr, void *dest)	\
{									\
	type retval;							\
	void __user *vaddr = (void __force __user *) addr;		\
									\
	if (copy_from_user(&retval, vaddr, sizeof(type)))		\
		*(type *)dest = 0;					\
	else								\
		*(type *) dest = retval;				\
}
DEFINE_BASIC_FETCH_FUNCS(memory)
/*
 * Fetch a null-terminated string. Caller MUST set *(u32 *)dest with max
 * length and relative data location.
 */
static void FETCH_FUNC_NAME(memory, string)(struct pt_regs *regs,
					    void *addr, void *dest)
{
	long ret;
	u32 rloc = *(u32 *)dest;
	int maxlen  = get_rloc_len(rloc);
	u8 *dst = get_rloc_data(dest);
	void __user *src = (void __force __user *) addr;

	if (!maxlen)
		return;

	ret = strncpy_from_user(dst, src, maxlen);
	if (ret == maxlen)
		dst[--ret] = '\0';

	if (ret < 0) {	/* Failed to fetch string */
		((u8 *)get_rloc_data(dest))[0] = '\0';
		*(u32 *)dest = make_data_rloc(0, get_rloc_offs(rloc));
	} else {
		*(u32 *)dest = make_data_rloc(ret, get_rloc_offs(rloc));
	}
}

static void FETCH_FUNC_NAME(memory, string_size)(struct pt_regs *regs,
						 void *addr, void *dest)
{
	int len;
	void __user *vaddr = (void __force __user *) addr;

	len = strnlen_user(vaddr, MAX_STRING_SIZE);

	if (len == 0 || len > MAX_STRING_SIZE)  /* Failed to check length */
		*(u32 *)dest = 0;
	else
		*(u32 *)dest = len;
}

static unsigned long translate_user_vaddr(void *file_offset)
{
	unsigned long base_addr;
	struct uprobe_dispatch_data *udd;

	udd = (void *) current->utask->vaddr;

	base_addr = udd->bp_addr - udd->tu->offset;
	return base_addr + (unsigned long)file_offset;
}

#define DEFINE_FETCH_file_offset(type)					\
static void FETCH_FUNC_NAME(file_offset, type)(struct pt_regs *regs,	\
					       void *offset, void *dest)\
{									\
	void *vaddr = (void *)translate_user_vaddr(offset);		\
									\
	FETCH_FUNC_NAME(memory, type)(regs, vaddr, dest);		\
}
DEFINE_BASIC_FETCH_FUNCS(file_offset)
DEFINE_FETCH_file_offset(string)
DEFINE_FETCH_file_offset(string_size)

/* Fetch type information table */
static const struct fetch_type uprobes_fetch_type_table[] = {
	/* Special types */
	[FETCH_TYPE_STRING] = __ASSIGN_FETCH_TYPE("string", string, string,
					sizeof(u32), 1, "__data_loc char[]"),
	[FETCH_TYPE_STRSIZE] = __ASSIGN_FETCH_TYPE("string_size", u32,
					string_size, sizeof(u32), 0, "u32"),
	/* Basic types */
	ASSIGN_FETCH_TYPE(u8,  u8,  0),
	ASSIGN_FETCH_TYPE(u16, u16, 0),
	ASSIGN_FETCH_TYPE(u32, u32, 0),
	ASSIGN_FETCH_TYPE(u64, u64, 0),
	ASSIGN_FETCH_TYPE(s8,  u8,  1),
	ASSIGN_FETCH_TYPE(s16, u16, 1),
	ASSIGN_FETCH_TYPE(s32, u32, 1),
	ASSIGN_FETCH_TYPE(s64, u64, 1),
	ASSIGN_FETCH_TYPE_ALIAS(x8,  u8,  u8,  0),
	ASSIGN_FETCH_TYPE_ALIAS(x16, u16, u16, 0),
	ASSIGN_FETCH_TYPE_ALIAS(x32, u32, u32, 0),
	ASSIGN_FETCH_TYPE_ALIAS(x64, u64, u64, 0),

	ASSIGN_FETCH_TYPE_END
};

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

/*
 * Allocate new trace_uprobe and initialize it (including uprobes).
 */
static struct trace_uprobe *
alloc_trace_uprobe(const char *group, const char *event, int nargs, bool is_ret)
{
	struct trace_uprobe *tu;

	if (!event || !is_good_name(event))
		return ERR_PTR(-EINVAL);

	if (!group || !is_good_name(group))
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

	INIT_LIST_HEAD(&tu->list);
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

	for (i = 0; i < tu->tp.nr_args; i++)
		traceprobe_free_probe_arg(&tu->tp.args[i]);

	iput(tu->inode);
	kfree(tu->tp.call.class->system);
	kfree(tu->tp.call.name);
	kfree(tu->filename);
	kfree(tu);
}

static struct trace_uprobe *find_probe_event(const char *event, const char *group)
{
	struct trace_uprobe *tu;

	list_for_each_entry(tu, &uprobe_list, list)
		if (strcmp(trace_event_name(&tu->tp.call), event) == 0 &&
		    strcmp(tu->tp.call.class->system, group) == 0)
			return tu;

	return NULL;
}

/* Unregister a trace_uprobe and probe_event: call with locking uprobe_lock */
static int unregister_trace_uprobe(struct trace_uprobe *tu)
{
	int ret;

	ret = unregister_uprobe_event(tu);
	if (ret)
		return ret;

	list_del(&tu->list);
	free_trace_uprobe(tu);
	return 0;
}

/* Register a trace_uprobe and probe_event */
static int register_trace_uprobe(struct trace_uprobe *tu)
{
	struct trace_uprobe *old_tu;
	int ret;

	mutex_lock(&uprobe_lock);

	/* register as an event */
	old_tu = find_probe_event(trace_event_name(&tu->tp.call),
			tu->tp.call.class->system);
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

	list_add_tail(&tu->list, &uprobe_list);

end:
	mutex_unlock(&uprobe_lock);

	return ret;
}

/*
 * Argument syntax:
 *  - Add uprobe: p|r[:[GRP/]EVENT] PATH:OFFSET [FETCHARGS]
 *
 *  - Remove uprobe: -:[GRP/]EVENT
 */
static int create_trace_uprobe(int argc, char **argv)
{
	struct trace_uprobe *tu;
	struct inode *inode;
	char *arg, *event, *group, *filename;
	char buf[MAX_EVENT_NAME_LEN];
	struct path path;
	unsigned long offset;
	bool is_delete, is_return;
	int i, ret;

	inode = NULL;
	ret = 0;
	is_delete = false;
	is_return = false;
	event = NULL;
	group = NULL;

	/* argc must be >= 1 */
	if (argv[0][0] == '-')
		is_delete = true;
	else if (argv[0][0] == 'r')
		is_return = true;
	else if (argv[0][0] != 'p') {
		pr_info("Probe definition must be started with 'p', 'r' or '-'.\n");
		return -EINVAL;
	}

	if (argv[0][1] == ':') {
		event = &argv[0][2];
		arg = strchr(event, '/');

		if (arg) {
			group = event;
			event = arg + 1;
			event[-1] = '\0';

			if (strlen(group) == 0) {
				pr_info("Group name is not specified\n");
				return -EINVAL;
			}
		}
		if (strlen(event) == 0) {
			pr_info("Event name is not specified\n");
			return -EINVAL;
		}
	}
	if (!group)
		group = UPROBE_EVENT_SYSTEM;

	if (is_delete) {
		int ret;

		if (!event) {
			pr_info("Delete command needs an event name.\n");
			return -EINVAL;
		}
		mutex_lock(&uprobe_lock);
		tu = find_probe_event(event, group);

		if (!tu) {
			mutex_unlock(&uprobe_lock);
			pr_info("Event %s/%s doesn't exist.\n", group, event);
			return -ENOENT;
		}
		/* delete an event */
		ret = unregister_trace_uprobe(tu);
		mutex_unlock(&uprobe_lock);
		return ret;
	}

	if (argc < 2) {
		pr_info("Probe point is not specified.\n");
		return -EINVAL;
	}
	/* Find the last occurrence, in case the path contains ':' too. */
	arg = strrchr(argv[1], ':');
	if (!arg) {
		ret = -EINVAL;
		goto fail_address_parse;
	}

	*arg++ = '\0';
	filename = argv[1];
	ret = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (ret)
		goto fail_address_parse;

	inode = igrab(d_real_inode(path.dentry));
	path_put(&path);

	if (!inode || !S_ISREG(inode->i_mode)) {
		ret = -EINVAL;
		goto fail_address_parse;
	}

	ret = kstrtoul(arg, 0, &offset);
	if (ret)
		goto fail_address_parse;

	argc -= 2;
	argv += 2;

	/* setup a probe */
	if (!event) {
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

	tu = alloc_trace_uprobe(group, event, argc, is_return);
	if (IS_ERR(tu)) {
		pr_info("Failed to allocate trace_uprobe.(%d)\n", (int)PTR_ERR(tu));
		ret = PTR_ERR(tu);
		goto fail_address_parse;
	}
	tu->offset = offset;
	tu->inode = inode;
	tu->filename = kstrdup(filename, GFP_KERNEL);

	if (!tu->filename) {
		pr_info("Failed to allocate filename.\n");
		ret = -ENOMEM;
		goto error;
	}

	/* parse arguments */
	ret = 0;
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		struct probe_arg *parg = &tu->tp.args[i];

		/* Increment count for freeing args in error case */
		tu->tp.nr_args++;

		/* Parse argument name */
		arg = strchr(argv[i], '=');
		if (arg) {
			*arg++ = '\0';
			parg->name = kstrdup(argv[i], GFP_KERNEL);
		} else {
			arg = argv[i];
			/* If argument name is omitted, set "argN" */
			snprintf(buf, MAX_EVENT_NAME_LEN, "arg%d", i + 1);
			parg->name = kstrdup(buf, GFP_KERNEL);
		}

		if (!parg->name) {
			pr_info("Failed to allocate argument[%d] name.\n", i);
			ret = -ENOMEM;
			goto error;
		}

		if (!is_good_name(parg->name)) {
			pr_info("Invalid argument[%d] name: %s\n", i, parg->name);
			ret = -EINVAL;
			goto error;
		}

		if (traceprobe_conflict_field_name(parg->name, tu->tp.args, i)) {
			pr_info("Argument[%d] name '%s' conflicts with "
				"another field.\n", i, argv[i]);
			ret = -EINVAL;
			goto error;
		}

		/* Parse fetch argument */
		ret = traceprobe_parse_probe_arg(arg, &tu->tp.size, parg,
						 is_return, false,
						 uprobes_fetch_type_table);
		if (ret) {
			pr_info("Parse error at argument[%d]. (%d)\n", i, ret);
			goto error;
		}
	}

	ret = register_trace_uprobe(tu);
	if (ret)
		goto error;
	return 0;

error:
	free_trace_uprobe(tu);
	return ret;

fail_address_parse:
	iput(inode);

	pr_info("Failed to parse address or file.\n");

	return ret;
}

static int cleanup_all_probes(void)
{
	struct trace_uprobe *tu;
	int ret = 0;

	mutex_lock(&uprobe_lock);
	while (!list_empty(&uprobe_list)) {
		tu = list_entry(uprobe_list.next, struct trace_uprobe, list);
		ret = unregister_trace_uprobe(tu);
		if (ret)
			break;
	}
	mutex_unlock(&uprobe_lock);
	return ret;
}

/* Probes listing interfaces */
static void *probes_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&uprobe_lock);
	return seq_list_start(&uprobe_list, *pos);
}

static void *probes_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &uprobe_list, pos);
}

static void probes_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&uprobe_lock);
}

static int probes_seq_show(struct seq_file *m, void *v)
{
	struct trace_uprobe *tu = v;
	char c = is_ret_probe(tu) ? 'r' : 'p';
	int i;

	seq_printf(m, "%c:%s/%s %s:0x%0*lx", c, tu->tp.call.class->system,
			trace_event_name(&tu->tp.call), tu->filename,
			(int)(sizeof(void *) * 2), tu->offset);

	for (i = 0; i < tu->tp.nr_args; i++)
		seq_printf(m, " %s=%s", tu->tp.args[i].name, tu->tp.args[i].comm);

	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations probes_seq_op = {
	.start	= probes_seq_start,
	.next	= probes_seq_next,
	.stop	= probes_seq_stop,
	.show	= probes_seq_show
};

static int probes_open(struct inode *inode, struct file *file)
{
	int ret;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		ret = cleanup_all_probes();
		if (ret)
			return ret;
	}

	return seq_open(file, &probes_seq_op);
}

static ssize_t probes_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	return trace_parse_run_command(file, buffer, count, ppos, create_trace_uprobe);
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
	struct trace_uprobe *tu = v;

	seq_printf(m, "  %s %-44s %15lu\n", tu->filename,
			trace_event_name(&tu->tp.call), tu->nhit);
	return 0;
}

static const struct seq_operations profile_seq_op = {
	.start	= probes_seq_start,
	.next	= probes_seq_next,
	.stop	= probes_seq_stop,
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
	int i;

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

	for (i = 0; i < tu->tp.nr_args; i++) {
		struct probe_arg *parg = &tu->tp.args[i];

		if (!parg->type->print(s, parg->name, data + parg->offset, entry))
			goto out;
	}

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
	ret = uprobe_register(tu->inode, tu->offset, &tu->consumer);
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
		synchronize_sched();
		kfree(link);

		if (!list_empty(&tu->tp.files))
			return;
	}

	WARN_ON(!uprobe_filter_is_empty(&tu->filter));

	uprobe_unregister(tu->inode, tu->offset, &tu->consumer);
	tu->tp.flags &= file ? ~TP_FLAG_TRACE : ~TP_FLAG_PROFILE;

	uprobe_buffer_disable();
}

static int uprobe_event_define_fields(struct trace_event_call *event_call)
{
	int ret, i, size;
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
	/* Set argument names as fields */
	for (i = 0; i < tu->tp.nr_args; i++) {
		struct probe_arg *parg = &tu->tp.args[i];

		ret = trace_define_field(event_call, parg->type->fmttype,
					 parg->name, size + parg->offset,
					 parg->type->size, parg->type->is_signed,
					 FILTER_OTHER);

		if (ret)
			return ret;
	}
	return 0;
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
	store_trace_args(esize, &tu->tp, regs, ucb->buf, dsize);

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
	store_trace_args(esize, &tu->tp, regs, ucb->buf, dsize);

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

	if (set_print_fmt(&tu->tp, is_ret_probe(tu)) < 0)
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
create_local_trace_uprobe(char *name, unsigned long offs, bool is_return)
{
	struct trace_uprobe *tu;
	struct inode *inode;
	struct path path;
	int ret;

	ret = kern_path(name, LOOKUP_FOLLOW, &path);
	if (ret)
		return ERR_PTR(ret);

	inode = igrab(d_inode(path.dentry));
	path_put(&path);

	if (!inode || !S_ISREG(inode->i_mode)) {
		iput(inode);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * local trace_kprobes are not added to probe_list, so they are never
	 * searched in find_trace_kprobe(). Therefore, there is no concern of
	 * duplicated name "DUMMY_EVENT" here.
	 */
	tu = alloc_trace_uprobe(UPROBE_EVENT_SYSTEM, "DUMMY_EVENT", 0,
				is_return);

	if (IS_ERR(tu)) {
		pr_info("Failed to allocate trace_uprobe.(%d)\n",
			(int)PTR_ERR(tu));
		return ERR_CAST(tu);
	}

	tu->offset = offs;
	tu->inode = inode;
	tu->filename = kstrdup(name, GFP_KERNEL);
	init_trace_event_call(tu, &tu->tp.call);

	if (set_print_fmt(&tu->tp, is_ret_probe(tu)) < 0) {
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
