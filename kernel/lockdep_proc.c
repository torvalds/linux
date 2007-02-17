/*
 * kernel/lockdep_proc.c
 *
 * Runtime locking correctness validator
 *
 * Started by Ingo Molnar:
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Code for /proc/lockdep and /proc/lockdep_stats:
 *
 */
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/debug_locks.h>

#include "lockdep_internals.h"

static void *l_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct lock_class *class = v;

	(*pos)++;

	if (class->lock_entry.next != &all_lock_classes)
		class = list_entry(class->lock_entry.next, struct lock_class,
				  lock_entry);
	else
		class = NULL;
	m->private = class;

	return class;
}

static void *l_start(struct seq_file *m, loff_t *pos)
{
	struct lock_class *class = m->private;

	if (&class->lock_entry == all_lock_classes.next)
		seq_printf(m, "all lock classes:\n");

	return class;
}

static void l_stop(struct seq_file *m, void *v)
{
}

static unsigned long count_forward_deps(struct lock_class *class)
{
	struct lock_list *entry;
	unsigned long ret = 1;

	/*
	 * Recurse this class's dependency list:
	 */
	list_for_each_entry(entry, &class->locks_after, entry)
		ret += count_forward_deps(entry->class);

	return ret;
}

static unsigned long count_backward_deps(struct lock_class *class)
{
	struct lock_list *entry;
	unsigned long ret = 1;

	/*
	 * Recurse this class's dependency list:
	 */
	list_for_each_entry(entry, &class->locks_before, entry)
		ret += count_backward_deps(entry->class);

	return ret;
}

static void print_name(struct seq_file *m, struct lock_class *class)
{
	char str[128];
	const char *name = class->name;

	if (!name) {
		name = __get_key_name(class->key, str);
		seq_printf(m, "%s", name);
	} else{
		seq_printf(m, "%s", name);
		if (class->name_version > 1)
			seq_printf(m, "#%d", class->name_version);
		if (class->subclass)
			seq_printf(m, "/%d", class->subclass);
	}
}

static int l_show(struct seq_file *m, void *v)
{
	unsigned long nr_forward_deps, nr_backward_deps;
	struct lock_class *class = m->private;
	struct lock_list *entry;
	char c1, c2, c3, c4;

	seq_printf(m, "%p", class->key);
#ifdef CONFIG_DEBUG_LOCKDEP
	seq_printf(m, " OPS:%8ld", class->ops);
#endif
	nr_forward_deps = count_forward_deps(class);
	seq_printf(m, " FD:%5ld", nr_forward_deps);

	nr_backward_deps = count_backward_deps(class);
	seq_printf(m, " BD:%5ld", nr_backward_deps);

	get_usage_chars(class, &c1, &c2, &c3, &c4);
	seq_printf(m, " %c%c%c%c", c1, c2, c3, c4);

	seq_printf(m, ": ");
	print_name(m, class);
	seq_puts(m, "\n");

	list_for_each_entry(entry, &class->locks_after, entry) {
		if (entry->distance == 1) {
			seq_printf(m, " -> [%p] ", entry->class);
			print_name(m, entry->class);
			seq_puts(m, "\n");
		}
	}
	seq_puts(m, "\n");

	return 0;
}

static const struct seq_operations lockdep_ops = {
	.start	= l_start,
	.next	= l_next,
	.stop	= l_stop,
	.show	= l_show,
};

static int lockdep_open(struct inode *inode, struct file *file)
{
	int res = seq_open(file, &lockdep_ops);
	if (!res) {
		struct seq_file *m = file->private_data;

		if (!list_empty(&all_lock_classes))
			m->private = list_entry(all_lock_classes.next,
					struct lock_class, lock_entry);
		else
			m->private = NULL;
	}
	return res;
}

static const struct file_operations proc_lockdep_operations = {
	.open		= lockdep_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void lockdep_stats_debug_show(struct seq_file *m)
{
#ifdef CONFIG_DEBUG_LOCKDEP
	unsigned int hi1 = debug_atomic_read(&hardirqs_on_events),
		     hi2 = debug_atomic_read(&hardirqs_off_events),
		     hr1 = debug_atomic_read(&redundant_hardirqs_on),
		     hr2 = debug_atomic_read(&redundant_hardirqs_off),
		     si1 = debug_atomic_read(&softirqs_on_events),
		     si2 = debug_atomic_read(&softirqs_off_events),
		     sr1 = debug_atomic_read(&redundant_softirqs_on),
		     sr2 = debug_atomic_read(&redundant_softirqs_off);

	seq_printf(m, " chain lookup misses:           %11u\n",
		debug_atomic_read(&chain_lookup_misses));
	seq_printf(m, " chain lookup hits:             %11u\n",
		debug_atomic_read(&chain_lookup_hits));
	seq_printf(m, " cyclic checks:                 %11u\n",
		debug_atomic_read(&nr_cyclic_checks));
	seq_printf(m, " cyclic-check recursions:       %11u\n",
		debug_atomic_read(&nr_cyclic_check_recursions));
	seq_printf(m, " find-mask forwards checks:     %11u\n",
		debug_atomic_read(&nr_find_usage_forwards_checks));
	seq_printf(m, " find-mask forwards recursions: %11u\n",
		debug_atomic_read(&nr_find_usage_forwards_recursions));
	seq_printf(m, " find-mask backwards checks:    %11u\n",
		debug_atomic_read(&nr_find_usage_backwards_checks));
	seq_printf(m, " find-mask backwards recursions:%11u\n",
		debug_atomic_read(&nr_find_usage_backwards_recursions));

	seq_printf(m, " hardirq on events:             %11u\n", hi1);
	seq_printf(m, " hardirq off events:            %11u\n", hi2);
	seq_printf(m, " redundant hardirq ons:         %11u\n", hr1);
	seq_printf(m, " redundant hardirq offs:        %11u\n", hr2);
	seq_printf(m, " softirq on events:             %11u\n", si1);
	seq_printf(m, " softirq off events:            %11u\n", si2);
	seq_printf(m, " redundant softirq ons:         %11u\n", sr1);
	seq_printf(m, " redundant softirq offs:        %11u\n", sr2);
#endif
}

static int lockdep_stats_show(struct seq_file *m, void *v)
{
	struct lock_class *class;
	unsigned long nr_unused = 0, nr_uncategorized = 0,
		      nr_irq_safe = 0, nr_irq_unsafe = 0,
		      nr_softirq_safe = 0, nr_softirq_unsafe = 0,
		      nr_hardirq_safe = 0, nr_hardirq_unsafe = 0,
		      nr_irq_read_safe = 0, nr_irq_read_unsafe = 0,
		      nr_softirq_read_safe = 0, nr_softirq_read_unsafe = 0,
		      nr_hardirq_read_safe = 0, nr_hardirq_read_unsafe = 0,
		      sum_forward_deps = 0, factor = 0;

	list_for_each_entry(class, &all_lock_classes, lock_entry) {

		if (class->usage_mask == 0)
			nr_unused++;
		if (class->usage_mask == LOCKF_USED)
			nr_uncategorized++;
		if (class->usage_mask & LOCKF_USED_IN_IRQ)
			nr_irq_safe++;
		if (class->usage_mask & LOCKF_ENABLED_IRQS)
			nr_irq_unsafe++;
		if (class->usage_mask & LOCKF_USED_IN_SOFTIRQ)
			nr_softirq_safe++;
		if (class->usage_mask & LOCKF_ENABLED_SOFTIRQS)
			nr_softirq_unsafe++;
		if (class->usage_mask & LOCKF_USED_IN_HARDIRQ)
			nr_hardirq_safe++;
		if (class->usage_mask & LOCKF_ENABLED_HARDIRQS)
			nr_hardirq_unsafe++;
		if (class->usage_mask & LOCKF_USED_IN_IRQ_READ)
			nr_irq_read_safe++;
		if (class->usage_mask & LOCKF_ENABLED_IRQS_READ)
			nr_irq_read_unsafe++;
		if (class->usage_mask & LOCKF_USED_IN_SOFTIRQ_READ)
			nr_softirq_read_safe++;
		if (class->usage_mask & LOCKF_ENABLED_SOFTIRQS_READ)
			nr_softirq_read_unsafe++;
		if (class->usage_mask & LOCKF_USED_IN_HARDIRQ_READ)
			nr_hardirq_read_safe++;
		if (class->usage_mask & LOCKF_ENABLED_HARDIRQS_READ)
			nr_hardirq_read_unsafe++;

		sum_forward_deps += count_forward_deps(class);
	}
#ifdef CONFIG_DEBUG_LOCKDEP
	DEBUG_LOCKS_WARN_ON(debug_atomic_read(&nr_unused_locks) != nr_unused);
#endif
	seq_printf(m, " lock-classes:                  %11lu [max: %lu]\n",
			nr_lock_classes, MAX_LOCKDEP_KEYS);
	seq_printf(m, " direct dependencies:           %11lu [max: %lu]\n",
			nr_list_entries, MAX_LOCKDEP_ENTRIES);
	seq_printf(m, " indirect dependencies:         %11lu\n",
			sum_forward_deps);

	/*
	 * Total number of dependencies:
	 *
	 * All irq-safe locks may nest inside irq-unsafe locks,
	 * plus all the other known dependencies:
	 */
	seq_printf(m, " all direct dependencies:       %11lu\n",
			nr_irq_unsafe * nr_irq_safe +
			nr_hardirq_unsafe * nr_hardirq_safe +
			nr_list_entries);

	/*
	 * Estimated factor between direct and indirect
	 * dependencies:
	 */
	if (nr_list_entries)
		factor = sum_forward_deps / nr_list_entries;

	seq_printf(m, " dependency chains:             %11lu [max: %lu]\n",
			nr_lock_chains, MAX_LOCKDEP_CHAINS);

#ifdef CONFIG_TRACE_IRQFLAGS
	seq_printf(m, " in-hardirq chains:             %11u\n",
			nr_hardirq_chains);
	seq_printf(m, " in-softirq chains:             %11u\n",
			nr_softirq_chains);
#endif
	seq_printf(m, " in-process chains:             %11u\n",
			nr_process_chains);
	seq_printf(m, " stack-trace entries:           %11lu [max: %lu]\n",
			nr_stack_trace_entries, MAX_STACK_TRACE_ENTRIES);
	seq_printf(m, " combined max dependencies:     %11u\n",
			(nr_hardirq_chains + 1) *
			(nr_softirq_chains + 1) *
			(nr_process_chains + 1)
	);
	seq_printf(m, " hardirq-safe locks:            %11lu\n",
			nr_hardirq_safe);
	seq_printf(m, " hardirq-unsafe locks:          %11lu\n",
			nr_hardirq_unsafe);
	seq_printf(m, " softirq-safe locks:            %11lu\n",
			nr_softirq_safe);
	seq_printf(m, " softirq-unsafe locks:          %11lu\n",
			nr_softirq_unsafe);
	seq_printf(m, " irq-safe locks:                %11lu\n",
			nr_irq_safe);
	seq_printf(m, " irq-unsafe locks:              %11lu\n",
			nr_irq_unsafe);

	seq_printf(m, " hardirq-read-safe locks:       %11lu\n",
			nr_hardirq_read_safe);
	seq_printf(m, " hardirq-read-unsafe locks:     %11lu\n",
			nr_hardirq_read_unsafe);
	seq_printf(m, " softirq-read-safe locks:       %11lu\n",
			nr_softirq_read_safe);
	seq_printf(m, " softirq-read-unsafe locks:     %11lu\n",
			nr_softirq_read_unsafe);
	seq_printf(m, " irq-read-safe locks:           %11lu\n",
			nr_irq_read_safe);
	seq_printf(m, " irq-read-unsafe locks:         %11lu\n",
			nr_irq_read_unsafe);

	seq_printf(m, " uncategorized locks:           %11lu\n",
			nr_uncategorized);
	seq_printf(m, " unused locks:                  %11lu\n",
			nr_unused);
	seq_printf(m, " max locking depth:             %11u\n",
			max_lockdep_depth);
	seq_printf(m, " max recursion depth:           %11u\n",
			max_recursion_depth);
	lockdep_stats_debug_show(m);
	seq_printf(m, " debug_locks:                   %11u\n",
			debug_locks);

	return 0;
}

static int lockdep_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, lockdep_stats_show, NULL);
}

static const struct file_operations proc_lockdep_stats_operations = {
	.open		= lockdep_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init lockdep_proc_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry("lockdep", S_IRUSR, NULL);
	if (entry)
		entry->proc_fops = &proc_lockdep_operations;

	entry = create_proc_entry("lockdep_stats", S_IRUSR, NULL);
	if (entry)
		entry->proc_fops = &proc_lockdep_stats_operations;

	return 0;
}

__initcall(lockdep_proc_init);

