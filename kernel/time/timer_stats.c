/*
 * kernel/time/timer_stats.c
 *
 * Collect timer usage statistics.
 *
 * Copyright(C) 2006, Red Hat, Inc., Ingo Molnar
 * Copyright(C) 2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * timer_stats is based on timer_top, a similar functionality which was part of
 * Con Kolivas dyntick patch set. It was developed by Daniel Petrini at the
 * Instituto Nokia de Tecnologia - INdT - Manaus. timer_top's design was based
 * on dynamic allocation of the statistics entries and linear search based
 * lookup combined with a global lock, rather than the static array, hash
 * and per-CPU locking which is used by timer_stats. It was written for the
 * pre hrtimer kernel code and therefore did not take hrtimers into account.
 * Nevertheless it provided the base for the timer_stats implementation and
 * was a helpful source of inspiration. Kudos to Daniel and the Nokia folks
 * for this effort.
 *
 * timer_top.c is
 *	Copyright (C) 2005 Instituto Nokia de Tecnologia - INdT - Manaus
 *	Written by Daniel Petrini <d.pensator@gmail.com>
 *	timer_top.c was released under the GNU General Public License version 2
 *
 * We export the addresses and counting of timer functions being called,
 * the pid and cmdline from the owner process if applicable.
 *
 * Start/stop data collection:
 * # echo [1|0] >/proc/timer_stats
 *
 * Display the information collected so far:
 * # cat /proc/timer_stats
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>

#include <asm/uaccess.h>

/*
 * This is our basic unit of interest: a timer expiry event identified
 * by the timer, its start/expire functions and the PID of the task that
 * started the timer. We count the number of times an event happens:
 */
struct entry {
	/*
	 * Hash list:
	 */
	struct entry		*next;

	/*
	 * Hash keys:
	 */
	void			*timer;
	void			*start_func;
	void			*expire_func;
	pid_t			pid;

	/*
	 * Number of timeout events:
	 */
	unsigned long		count;
	unsigned int		timer_flag;

	/*
	 * We save the command-line string to preserve
	 * this information past task exit:
	 */
	char			comm[TASK_COMM_LEN + 1];

} ____cacheline_aligned_in_smp;

/*
 * Spinlock protecting the tables - not taken during lookup:
 */
static DEFINE_RAW_SPINLOCK(table_lock);

/*
 * Per-CPU lookup locks for fast hash lookup:
 */
static DEFINE_PER_CPU(raw_spinlock_t, tstats_lookup_lock);

/*
 * Mutex to serialize state changes with show-stats activities:
 */
static DEFINE_MUTEX(show_mutex);

/*
 * Collection status, active/inactive:
 */
int __read_mostly timer_stats_active;

/*
 * Beginning/end timestamps of measurement:
 */
static ktime_t time_start, time_stop;

/*
 * tstat entry structs only get allocated while collection is
 * active and never freed during that time - this simplifies
 * things quite a bit.
 *
 * They get freed when a new collection period is started.
 */
#define MAX_ENTRIES_BITS	10
#define MAX_ENTRIES		(1UL << MAX_ENTRIES_BITS)

static unsigned long nr_entries;
static struct entry entries[MAX_ENTRIES];

static atomic_t overflow_count;

/*
 * The entries are in a hash-table, for fast lookup:
 */
#define TSTAT_HASH_BITS		(MAX_ENTRIES_BITS - 1)
#define TSTAT_HASH_SIZE		(1UL << TSTAT_HASH_BITS)
#define TSTAT_HASH_MASK		(TSTAT_HASH_SIZE - 1)

#define __tstat_hashfn(entry)						\
	(((unsigned long)(entry)->timer       ^				\
	  (unsigned long)(entry)->start_func  ^				\
	  (unsigned long)(entry)->expire_func ^				\
	  (unsigned long)(entry)->pid		) & TSTAT_HASH_MASK)

#define tstat_hashentry(entry)	(tstat_hash_table + __tstat_hashfn(entry))

static struct entry *tstat_hash_table[TSTAT_HASH_SIZE] __read_mostly;

static void reset_entries(void)
{
	nr_entries = 0;
	memset(entries, 0, sizeof(entries));
	memset(tstat_hash_table, 0, sizeof(tstat_hash_table));
	atomic_set(&overflow_count, 0);
}

static struct entry *alloc_entry(void)
{
	if (nr_entries >= MAX_ENTRIES)
		return NULL;

	return entries + nr_entries++;
}

static int match_entries(struct entry *entry1, struct entry *entry2)
{
	return entry1->timer       == entry2->timer	  &&
	       entry1->start_func  == entry2->start_func  &&
	       entry1->expire_func == entry2->expire_func &&
	       entry1->pid	   == entry2->pid;
}

/*
 * Look up whether an entry matching this item is present
 * in the hash already. Must be called with irqs off and the
 * lookup lock held:
 */
static struct entry *tstat_lookup(struct entry *entry, char *comm)
{
	struct entry **head, *curr, *prev;

	head = tstat_hashentry(entry);
	curr = *head;

	/*
	 * The fastpath is when the entry is already hashed,
	 * we do this with the lookup lock held, but with the
	 * table lock not held:
	 */
	while (curr) {
		if (match_entries(curr, entry))
			return curr;

		curr = curr->next;
	}
	/*
	 * Slowpath: allocate, set up and link a new hash entry:
	 */
	prev = NULL;
	curr = *head;

	raw_spin_lock(&table_lock);
	/*
	 * Make sure we have not raced with another CPU:
	 */
	while (curr) {
		if (match_entries(curr, entry))
			goto out_unlock;

		prev = curr;
		curr = curr->next;
	}

	curr = alloc_entry();
	if (curr) {
		*curr = *entry;
		curr->count = 0;
		curr->next = NULL;
		memcpy(curr->comm, comm, TASK_COMM_LEN);

		smp_mb(); /* Ensure that curr is initialized before insert */

		if (prev)
			prev->next = curr;
		else
			*head = curr;
	}
 out_unlock:
	raw_spin_unlock(&table_lock);

	return curr;
}

/**
 * timer_stats_update_stats - Update the statistics for a timer.
 * @timer:	pointer to either a timer_list or a hrtimer
 * @pid:	the pid of the task which set up the timer
 * @startf:	pointer to the function which did the timer setup
 * @timerf:	pointer to the timer callback function of the timer
 * @comm:	name of the process which set up the timer
 *
 * When the timer is already registered, then the event counter is
 * incremented. Otherwise the timer is registered in a free slot.
 */
void timer_stats_update_stats(void *timer, pid_t pid, void *startf,
			      void *timerf, char *comm,
			      unsigned int timer_flag)
{
	/*
	 * It doesn't matter which lock we take:
	 */
	raw_spinlock_t *lock;
	struct entry *entry, input;
	unsigned long flags;

	if (likely(!timer_stats_active))
		return;

	lock = &per_cpu(tstats_lookup_lock, raw_smp_processor_id());

	input.timer = timer;
	input.start_func = startf;
	input.expire_func = timerf;
	input.pid = pid;
	input.timer_flag = timer_flag;

	raw_spin_lock_irqsave(lock, flags);
	if (!timer_stats_active)
		goto out_unlock;

	entry = tstat_lookup(&input, comm);
	if (likely(entry))
		entry->count++;
	else
		atomic_inc(&overflow_count);

 out_unlock:
	raw_spin_unlock_irqrestore(lock, flags);
}

static void print_name_offset(struct seq_file *m, unsigned long addr)
{
	char symname[KSYM_NAME_LEN];

	if (lookup_symbol_name(addr, symname) < 0)
		seq_printf(m, "<%p>", (void *)addr);
	else
		seq_printf(m, "%s", symname);
}

static int tstats_show(struct seq_file *m, void *v)
{
	struct timespec period;
	struct entry *entry;
	unsigned long ms;
	long events = 0;
	ktime_t time;
	int i;

	mutex_lock(&show_mutex);
	/*
	 * If still active then calculate up to now:
	 */
	if (timer_stats_active)
		time_stop = ktime_get();

	time = ktime_sub(time_stop, time_start);

	period = ktime_to_timespec(time);
	ms = period.tv_nsec / 1000000;

	seq_puts(m, "Timer Stats Version: v0.3\n");
	seq_printf(m, "Sample period: %ld.%03ld s\n", period.tv_sec, ms);
	if (atomic_read(&overflow_count))
		seq_printf(m, "Overflow: %d entries\n", atomic_read(&overflow_count));
	seq_printf(m, "Collection: %s\n", timer_stats_active ? "active" : "inactive");

	for (i = 0; i < nr_entries; i++) {
		entry = entries + i;
		if (entry->timer_flag & TIMER_STATS_FLAG_DEFERRABLE) {
			seq_printf(m, "%4luD, %5d %-16s ",
				entry->count, entry->pid, entry->comm);
		} else {
			seq_printf(m, " %4lu, %5d %-16s ",
				entry->count, entry->pid, entry->comm);
		}

		print_name_offset(m, (unsigned long)entry->start_func);
		seq_puts(m, " (");
		print_name_offset(m, (unsigned long)entry->expire_func);
		seq_puts(m, ")\n");

		events += entry->count;
	}

	ms += period.tv_sec * 1000;
	if (!ms)
		ms = 1;

	if (events && period.tv_sec)
		seq_printf(m, "%ld total events, %ld.%03ld events/sec\n",
			   events, events * 1000 / ms,
			   (events * 1000000 / ms) % 1000);
	else
		seq_printf(m, "%ld total events\n", events);

	mutex_unlock(&show_mutex);

	return 0;
}

/*
 * After a state change, make sure all concurrent lookup/update
 * activities have stopped:
 */
static void sync_access(void)
{
	unsigned long flags;
	int cpu;

	for_each_online_cpu(cpu) {
		raw_spinlock_t *lock = &per_cpu(tstats_lookup_lock, cpu);

		raw_spin_lock_irqsave(lock, flags);
		/* nothing */
		raw_spin_unlock_irqrestore(lock, flags);
	}
}

static ssize_t tstats_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *offs)
{
	char ctl[2];

	if (count != 2 || *offs)
		return -EINVAL;

	if (copy_from_user(ctl, buf, count))
		return -EFAULT;

	mutex_lock(&show_mutex);
	switch (ctl[0]) {
	case '0':
		if (timer_stats_active) {
			timer_stats_active = 0;
			time_stop = ktime_get();
			sync_access();
		}
		break;
	case '1':
		if (!timer_stats_active) {
			reset_entries();
			time_start = ktime_get();
			smp_mb();
			timer_stats_active = 1;
		}
		break;
	default:
		count = -EINVAL;
	}
	mutex_unlock(&show_mutex);

	return count;
}

static int tstats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, tstats_show, NULL);
}

static const struct file_operations tstats_fops = {
	.open		= tstats_open,
	.read		= seq_read,
	.write		= tstats_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void __init init_timer_stats(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		raw_spin_lock_init(&per_cpu(tstats_lookup_lock, cpu));
}

static int __init init_tstats_procfs(void)
{
	struct proc_dir_entry *pe;

	pe = proc_create("timer_stats", 0644, NULL, &tstats_fops);
	if (!pe)
		return -ENOMEM;
	return 0;
}
__initcall(init_tstats_procfs);
