/*
 * trace_ksym.c - Kernel Symbol Tracer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2009
 */

#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include "trace_output.h"
#include "trace.h"

#include <linux/hw_breakpoint.h>
#include <asm/hw_breakpoint.h>

#include <asm/atomic.h>

#define KSYM_TRACER_OP_LEN 3 /* rw- */

struct trace_ksym {
	struct perf_event	**ksym_hbp;
	struct perf_event_attr	attr;
#ifdef CONFIG_PROFILE_KSYM_TRACER
	atomic64_t		counter;
#endif
	struct hlist_node	ksym_hlist;
};

static struct trace_array *ksym_trace_array;

static unsigned int ksym_tracing_enabled;

static HLIST_HEAD(ksym_filter_head);

static DEFINE_MUTEX(ksym_tracer_mutex);

#ifdef CONFIG_PROFILE_KSYM_TRACER

#define MAX_UL_INT 0xffffffff

void ksym_collect_stats(unsigned long hbp_hit_addr)
{
	struct hlist_node *node;
	struct trace_ksym *entry;

	rcu_read_lock();
	hlist_for_each_entry_rcu(entry, node, &ksym_filter_head, ksym_hlist) {
		if (entry->attr.bp_addr == hbp_hit_addr) {
			atomic64_inc(&entry->counter);
			break;
		}
	}
	rcu_read_unlock();
}
#endif /* CONFIG_PROFILE_KSYM_TRACER */

void ksym_hbp_handler(struct perf_event *hbp, int nmi,
		      struct perf_sample_data *data,
		      struct pt_regs *regs)
{
	struct ring_buffer_event *event;
	struct ksym_trace_entry *entry;
	struct ring_buffer *buffer;
	int pc;

	if (!ksym_tracing_enabled)
		return;

	buffer = ksym_trace_array->buffer;

	pc = preempt_count();

	event = trace_buffer_lock_reserve(buffer, TRACE_KSYM,
							sizeof(*entry), 0, pc);
	if (!event)
		return;

	entry		= ring_buffer_event_data(event);
	entry->ip	= instruction_pointer(regs);
	entry->type	= hw_breakpoint_type(hbp);
	entry->addr	= hw_breakpoint_addr(hbp);
	strlcpy(entry->cmd, current->comm, TASK_COMM_LEN);

#ifdef CONFIG_PROFILE_KSYM_TRACER
	ksym_collect_stats(hw_breakpoint_addr(hbp));
#endif /* CONFIG_PROFILE_KSYM_TRACER */

	trace_buffer_unlock_commit(buffer, event, 0, pc);
}

/* Valid access types are represented as
 *
 * rw- : Set Read/Write Access Breakpoint
 * -w- : Set Write Access Breakpoint
 * --- : Clear Breakpoints
 * --x : Set Execution Break points (Not available yet)
 *
 */
static int ksym_trace_get_access_type(char *str)
{
	int access = 0;

	if (str[0] == 'r')
		access |= HW_BREAKPOINT_R;

	if (str[1] == 'w')
		access |= HW_BREAKPOINT_W;

	if (str[2] == 'x')
		access |= HW_BREAKPOINT_X;

	switch (access) {
	case HW_BREAKPOINT_R:
	case HW_BREAKPOINT_W:
	case HW_BREAKPOINT_W | HW_BREAKPOINT_R:
		return access;
	default:
		return -EINVAL;
	}
}

/*
 * There can be several possible malformed requests and we attempt to capture
 * all of them. We enumerate some of the rules
 * 1. We will not allow kernel symbols with ':' since it is used as a delimiter.
 *    i.e. multiple ':' symbols disallowed. Possible uses are of the form
 *    <module>:<ksym_name>:<op>.
 * 2. No delimiter symbol ':' in the input string
 * 3. Spurious operator symbols or symbols not in their respective positions
 * 4. <ksym_name>:--- i.e. clear breakpoint request when ksym_name not in file
 * 5. Kernel symbol not a part of /proc/kallsyms
 * 6. Duplicate requests
 */
static int parse_ksym_trace_str(char *input_string, char **ksymname,
							unsigned long *addr)
{
	int ret;

	*ksymname = strsep(&input_string, ":");
	*addr = kallsyms_lookup_name(*ksymname);

	/* Check for malformed request: (2), (1) and (5) */
	if ((!input_string) ||
	    (strlen(input_string) != KSYM_TRACER_OP_LEN) ||
	    (*addr == 0))
		return -EINVAL;;

	ret = ksym_trace_get_access_type(input_string);

	return ret;
}

int process_new_ksym_entry(char *ksymname, int op, unsigned long addr)
{
	struct trace_ksym *entry;
	int ret = -ENOMEM;

	entry = kzalloc(sizeof(struct trace_ksym), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	hw_breakpoint_init(&entry->attr);

	entry->attr.bp_type = op;
	entry->attr.bp_addr = addr;
	entry->attr.bp_len = HW_BREAKPOINT_LEN_4;

	entry->ksym_hbp = register_wide_hw_breakpoint(&entry->attr,
					ksym_hbp_handler);

	if (IS_ERR(entry->ksym_hbp)) {
		ret = PTR_ERR(entry->ksym_hbp);
		if (ret == -ENOSPC) {
			printk(KERN_ERR "ksym_tracer: Maximum limit reached."
			" No new requests for tracing can be accepted now.\n");
		} else {
			printk(KERN_INFO "ksym_tracer request failed. Try again"
					 " later!!\n");
		}
		goto err;
	}

	hlist_add_head_rcu(&(entry->ksym_hlist), &ksym_filter_head);

	return 0;

err:
	kfree(entry);

	return ret;
}

static ssize_t ksym_trace_filter_read(struct file *filp, char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct trace_ksym *entry;
	struct hlist_node *node;
	struct trace_seq *s;
	ssize_t cnt = 0;
	int ret;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	trace_seq_init(s);

	mutex_lock(&ksym_tracer_mutex);

	hlist_for_each_entry(entry, node, &ksym_filter_head, ksym_hlist) {
		ret = trace_seq_printf(s, "%pS:",
				(void *)(unsigned long)entry->attr.bp_addr);
		if (entry->attr.bp_type == HW_BREAKPOINT_R)
			ret = trace_seq_puts(s, "r--\n");
		else if (entry->attr.bp_type == HW_BREAKPOINT_W)
			ret = trace_seq_puts(s, "-w-\n");
		else if (entry->attr.bp_type == (HW_BREAKPOINT_W | HW_BREAKPOINT_R))
			ret = trace_seq_puts(s, "rw-\n");
		WARN_ON_ONCE(!ret);
	}

	cnt = simple_read_from_buffer(ubuf, count, ppos, s->buffer, s->len);

	mutex_unlock(&ksym_tracer_mutex);

	kfree(s);

	return cnt;
}

static void __ksym_trace_reset(void)
{
	struct trace_ksym *entry;
	struct hlist_node *node, *node1;

	mutex_lock(&ksym_tracer_mutex);
	hlist_for_each_entry_safe(entry, node, node1, &ksym_filter_head,
								ksym_hlist) {
		unregister_wide_hw_breakpoint(entry->ksym_hbp);
		hlist_del_rcu(&(entry->ksym_hlist));
		synchronize_rcu();
		kfree(entry);
	}
	mutex_unlock(&ksym_tracer_mutex);
}

static ssize_t ksym_trace_filter_write(struct file *file,
					const char __user *buffer,
						size_t count, loff_t *ppos)
{
	struct trace_ksym *entry;
	struct hlist_node *node;
	char *buf, *input_string, *ksymname = NULL;
	unsigned long ksym_addr = 0;
	int ret, op, changed = 0;

	buf = kzalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(buf, buffer, count))
		goto out;

	buf[count] = '\0';
	input_string = strstrip(buf);

	/*
	 * Clear all breakpoints if:
	 * 1: echo > ksym_trace_filter
	 * 2: echo 0 > ksym_trace_filter
	 * 3: echo "*:---" > ksym_trace_filter
	 */
	if (!input_string[0] || !strcmp(input_string, "0") ||
	    !strcmp(input_string, "*:---")) {
		__ksym_trace_reset();
		ret = 0;
		goto out;
	}

	ret = op = parse_ksym_trace_str(input_string, &ksymname, &ksym_addr);
	if (ret < 0)
		goto out;

	mutex_lock(&ksym_tracer_mutex);

	ret = -EINVAL;
	hlist_for_each_entry(entry, node, &ksym_filter_head, ksym_hlist) {
		if (entry->attr.bp_addr == ksym_addr) {
			/* Check for malformed request: (6) */
			if (entry->attr.bp_type != op)
				changed = 1;
			else
				goto out_unlock;
			break;
		}
	}
	if (changed) {
		unregister_wide_hw_breakpoint(entry->ksym_hbp);
		entry->attr.bp_type = op;
		ret = 0;
		if (op > 0) {
			entry->ksym_hbp =
				register_wide_hw_breakpoint(&entry->attr,
					ksym_hbp_handler);
			if (IS_ERR(entry->ksym_hbp))
				ret = PTR_ERR(entry->ksym_hbp);
			else
				goto out_unlock;
		}
		/* Error or "symbol:---" case: drop it */
		hlist_del_rcu(&(entry->ksym_hlist));
		synchronize_rcu();
		kfree(entry);
		goto out_unlock;
	} else {
		/* Check for malformed request: (4) */
		if (op)
			ret = process_new_ksym_entry(ksymname, op, ksym_addr);
	}
out_unlock:
	mutex_unlock(&ksym_tracer_mutex);
out:
	kfree(buf);
	return !ret ? count : ret;
}

static const struct file_operations ksym_tracing_fops = {
	.open		= tracing_open_generic,
	.read		= ksym_trace_filter_read,
	.write		= ksym_trace_filter_write,
};

static void ksym_trace_reset(struct trace_array *tr)
{
	ksym_tracing_enabled = 0;
	__ksym_trace_reset();
}

static int ksym_trace_init(struct trace_array *tr)
{
	int cpu, ret = 0;

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);
	ksym_tracing_enabled = 1;
	ksym_trace_array = tr;

	return ret;
}

static void ksym_trace_print_header(struct seq_file *m)
{
	seq_puts(m,
		 "#       TASK-PID   CPU#      Symbol                    "
		 "Type    Function\n");
	seq_puts(m,
		 "#          |        |          |                       "
		 " |         |\n");
}

static enum print_line_t ksym_trace_output(struct trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	struct trace_seq *s = &iter->seq;
	struct ksym_trace_entry *field;
	char str[KSYM_SYMBOL_LEN];
	int ret;

	if (entry->type != TRACE_KSYM)
		return TRACE_TYPE_UNHANDLED;

	trace_assign_type(field, entry);

	ret = trace_seq_printf(s, "%11s-%-5d [%03d] %pS", field->cmd,
				entry->pid, iter->cpu, (char *)field->addr);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	switch (field->type) {
	case HW_BREAKPOINT_R:
		ret = trace_seq_printf(s, " R  ");
		break;
	case HW_BREAKPOINT_W:
		ret = trace_seq_printf(s, " W  ");
		break;
	case HW_BREAKPOINT_R | HW_BREAKPOINT_W:
		ret = trace_seq_printf(s, " RW ");
		break;
	default:
		return TRACE_TYPE_PARTIAL_LINE;
	}

	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	sprint_symbol(str, field->ip);
	ret = trace_seq_printf(s, "%s\n", str);
	if (!ret)
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

struct tracer ksym_tracer __read_mostly =
{
	.name		= "ksym_tracer",
	.init		= ksym_trace_init,
	.reset		= ksym_trace_reset,
#ifdef CONFIG_FTRACE_SELFTEST
	.selftest	= trace_selftest_startup_ksym,
#endif
	.print_header   = ksym_trace_print_header,
	.print_line	= ksym_trace_output
};

#ifdef CONFIG_PROFILE_KSYM_TRACER
static int ksym_profile_show(struct seq_file *m, void *v)
{
	struct hlist_node *node;
	struct trace_ksym *entry;
	int access_type = 0;
	char fn_name[KSYM_NAME_LEN];

	seq_puts(m, "  Access Type ");
	seq_puts(m, "  Symbol                                       Counter\n");
	seq_puts(m, "  ----------- ");
	seq_puts(m, "  ------                                       -------\n");

	rcu_read_lock();
	hlist_for_each_entry_rcu(entry, node, &ksym_filter_head, ksym_hlist) {

		access_type = entry->attr.bp_type;

		switch (access_type) {
		case HW_BREAKPOINT_R:
			seq_puts(m, "  R           ");
			break;
		case HW_BREAKPOINT_W:
			seq_puts(m, "  W           ");
			break;
		case HW_BREAKPOINT_R | HW_BREAKPOINT_W:
			seq_puts(m, "  RW          ");
			break;
		default:
			seq_puts(m, "  NA          ");
		}

		if (lookup_symbol_name(entry->attr.bp_addr, fn_name) >= 0)
			seq_printf(m, "  %-36s", fn_name);
		else
			seq_printf(m, "  %-36s", "<NA>");
		seq_printf(m, " %15llu\n",
			   (unsigned long long)atomic64_read(&entry->counter));
	}
	rcu_read_unlock();

	return 0;
}

static int ksym_profile_open(struct inode *node, struct file *file)
{
	return single_open(file, ksym_profile_show, NULL);
}

static const struct file_operations ksym_profile_fops = {
	.open		= ksym_profile_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif /* CONFIG_PROFILE_KSYM_TRACER */

__init static int init_ksym_trace(void)
{
	struct dentry *d_tracer;

	d_tracer = tracing_init_dentry();

	trace_create_file("ksym_trace_filter", 0644, d_tracer,
			  NULL, &ksym_tracing_fops);

#ifdef CONFIG_PROFILE_KSYM_TRACER
	trace_create_file("ksym_profile", 0444, d_tracer,
			  NULL, &ksym_profile_fops);
#endif

	return register_tracer(&ksym_tracer);
}
device_initcall(init_ksym_trace);
