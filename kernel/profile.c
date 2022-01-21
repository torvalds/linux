// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/kernel/profile.c
 *  Simple profiling. Manages a direct-mapped profile hit count buffer,
 *  with configurable resolution, support for restricting the cpus on
 *  which profiling is done, and switching between cpu time and
 *  schedule() calls via kernel command line parameters passed at boot.
 *
 *  Scheduler profiling support, Arjan van de Ven and Ingo Molnar,
 *	Red Hat, July 2004
 *  Consolidation of architecture support code for profiling,
 *	Nadia Yvette Chambers, Oracle, July 2004
 *  Amortized hit count accounting via per-cpu open-addressed hashtables
 *	to resolve timer interrupt livelocks, Nadia Yvette Chambers,
 *	Oracle, 2004
 */

#include <linux/export.h>
#include <linux/profile.h>
#include <linux/memblock.h>
#include <linux/notifier.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched/stat.h>

#include <asm/sections.h>
#include <asm/irq_regs.h>
#include <asm/ptrace.h>

struct profile_hit {
	u32 pc, hits;
};
#define PROFILE_GRPSHIFT	3
#define PROFILE_GRPSZ		(1 << PROFILE_GRPSHIFT)
#define NR_PROFILE_HIT		(PAGE_SIZE/sizeof(struct profile_hit))
#define NR_PROFILE_GRP		(NR_PROFILE_HIT/PROFILE_GRPSZ)

static atomic_t *prof_buffer;
static unsigned long prof_len;
static unsigned short int prof_shift;

int prof_on __read_mostly;
EXPORT_SYMBOL_GPL(prof_on);

static cpumask_var_t prof_cpu_mask;
#if defined(CONFIG_SMP) && defined(CONFIG_PROC_FS)
static DEFINE_PER_CPU(struct profile_hit *[2], cpu_profile_hits);
static DEFINE_PER_CPU(int, cpu_profile_flip);
static DEFINE_MUTEX(profile_flip_mutex);
#endif /* CONFIG_SMP */

int profile_setup(char *str)
{
	static const char schedstr[] = "schedule";
	static const char sleepstr[] = "sleep";
	static const char kvmstr[] = "kvm";
	int par;

	if (!strncmp(str, sleepstr, strlen(sleepstr))) {
#ifdef CONFIG_SCHEDSTATS
		force_schedstat_enabled();
		prof_on = SLEEP_PROFILING;
		if (str[strlen(sleepstr)] == ',')
			str += strlen(sleepstr) + 1;
		if (get_option(&str, &par))
			prof_shift = clamp(par, 0, BITS_PER_LONG - 1);
		pr_info("kernel sleep profiling enabled (shift: %u)\n",
			prof_shift);
#else
		pr_warn("kernel sleep profiling requires CONFIG_SCHEDSTATS\n");
#endif /* CONFIG_SCHEDSTATS */
	} else if (!strncmp(str, schedstr, strlen(schedstr))) {
		prof_on = SCHED_PROFILING;
		if (str[strlen(schedstr)] == ',')
			str += strlen(schedstr) + 1;
		if (get_option(&str, &par))
			prof_shift = clamp(par, 0, BITS_PER_LONG - 1);
		pr_info("kernel schedule profiling enabled (shift: %u)\n",
			prof_shift);
	} else if (!strncmp(str, kvmstr, strlen(kvmstr))) {
		prof_on = KVM_PROFILING;
		if (str[strlen(kvmstr)] == ',')
			str += strlen(kvmstr) + 1;
		if (get_option(&str, &par))
			prof_shift = clamp(par, 0, BITS_PER_LONG - 1);
		pr_info("kernel KVM profiling enabled (shift: %u)\n",
			prof_shift);
	} else if (get_option(&str, &par)) {
		prof_shift = clamp(par, 0, BITS_PER_LONG - 1);
		prof_on = CPU_PROFILING;
		pr_info("kernel profiling enabled (shift: %u)\n",
			prof_shift);
	}
	return 1;
}
__setup("profile=", profile_setup);


int __ref profile_init(void)
{
	int buffer_bytes;
	if (!prof_on)
		return 0;

	/* only text is profiled */
	prof_len = (_etext - _stext) >> prof_shift;
	buffer_bytes = prof_len*sizeof(atomic_t);

	if (!alloc_cpumask_var(&prof_cpu_mask, GFP_KERNEL))
		return -ENOMEM;

	cpumask_copy(prof_cpu_mask, cpu_possible_mask);

	prof_buffer = kzalloc(buffer_bytes, GFP_KERNEL|__GFP_NOWARN);
	if (prof_buffer)
		return 0;

	prof_buffer = alloc_pages_exact(buffer_bytes,
					GFP_KERNEL|__GFP_ZERO|__GFP_NOWARN);
	if (prof_buffer)
		return 0;

	prof_buffer = vzalloc(buffer_bytes);
	if (prof_buffer)
		return 0;

	free_cpumask_var(prof_cpu_mask);
	return -ENOMEM;
}

/* Profile event notifications */

static BLOCKING_NOTIFIER_HEAD(task_exit_notifier);
static ATOMIC_NOTIFIER_HEAD(task_free_notifier);
static BLOCKING_NOTIFIER_HEAD(munmap_notifier);

void profile_task_exit(struct task_struct *task)
{
	blocking_notifier_call_chain(&task_exit_notifier, 0, task);
}

int profile_handoff_task(struct task_struct *task)
{
	int ret;
	ret = atomic_notifier_call_chain(&task_free_notifier, 0, task);
	return (ret == NOTIFY_OK) ? 1 : 0;
}

void profile_munmap(unsigned long addr)
{
	blocking_notifier_call_chain(&munmap_notifier, 0, (void *)addr);
}

int task_handoff_register(struct notifier_block *n)
{
	return atomic_notifier_chain_register(&task_free_notifier, n);
}
EXPORT_SYMBOL_GPL(task_handoff_register);

int task_handoff_unregister(struct notifier_block *n)
{
	return atomic_notifier_chain_unregister(&task_free_notifier, n);
}
EXPORT_SYMBOL_GPL(task_handoff_unregister);

int profile_event_register(enum profile_type type, struct notifier_block *n)
{
	int err = -EINVAL;

	switch (type) {
	case PROFILE_TASK_EXIT:
		err = blocking_notifier_chain_register(
				&task_exit_notifier, n);
		break;
	case PROFILE_MUNMAP:
		err = blocking_notifier_chain_register(
				&munmap_notifier, n);
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(profile_event_register);

int profile_event_unregister(enum profile_type type, struct notifier_block *n)
{
	int err = -EINVAL;

	switch (type) {
	case PROFILE_TASK_EXIT:
		err = blocking_notifier_chain_unregister(
				&task_exit_notifier, n);
		break;
	case PROFILE_MUNMAP:
		err = blocking_notifier_chain_unregister(
				&munmap_notifier, n);
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(profile_event_unregister);

#if defined(CONFIG_SMP) && defined(CONFIG_PROC_FS)
/*
 * Each cpu has a pair of open-addressed hashtables for pending
 * profile hits. read_profile() IPI's all cpus to request them
 * to flip buffers and flushes their contents to prof_buffer itself.
 * Flip requests are serialized by the profile_flip_mutex. The sole
 * use of having a second hashtable is for avoiding cacheline
 * contention that would otherwise happen during flushes of pending
 * profile hits required for the accuracy of reported profile hits
 * and so resurrect the interrupt livelock issue.
 *
 * The open-addressed hashtables are indexed by profile buffer slot
 * and hold the number of pending hits to that profile buffer slot on
 * a cpu in an entry. When the hashtable overflows, all pending hits
 * are accounted to their corresponding profile buffer slots with
 * atomic_add() and the hashtable emptied. As numerous pending hits
 * may be accounted to a profile buffer slot in a hashtable entry,
 * this amortizes a number of atomic profile buffer increments likely
 * to be far larger than the number of entries in the hashtable,
 * particularly given that the number of distinct profile buffer
 * positions to which hits are accounted during short intervals (e.g.
 * several seconds) is usually very small. Exclusion from buffer
 * flipping is provided by interrupt disablement (note that for
 * SCHED_PROFILING or SLEEP_PROFILING profile_hit() may be called from
 * process context).
 * The hash function is meant to be lightweight as opposed to strong,
 * and was vaguely inspired by ppc64 firmware-supported inverted
 * pagetable hash functions, but uses a full hashtable full of finite
 * collision chains, not just pairs of them.
 *
 * -- nyc
 */
static void __profile_flip_buffers(void *unused)
{
	int cpu = smp_processor_id();

	per_cpu(cpu_profile_flip, cpu) = !per_cpu(cpu_profile_flip, cpu);
}

static void profile_flip_buffers(void)
{
	int i, j, cpu;

	mutex_lock(&profile_flip_mutex);
	j = per_cpu(cpu_profile_flip, get_cpu());
	put_cpu();
	on_each_cpu(__profile_flip_buffers, NULL, 1);
	for_each_online_cpu(cpu) {
		struct profile_hit *hits = per_cpu(cpu_profile_hits, cpu)[j];
		for (i = 0; i < NR_PROFILE_HIT; ++i) {
			if (!hits[i].hits) {
				if (hits[i].pc)
					hits[i].pc = 0;
				continue;
			}
			atomic_add(hits[i].hits, &prof_buffer[hits[i].pc]);
			hits[i].hits = hits[i].pc = 0;
		}
	}
	mutex_unlock(&profile_flip_mutex);
}

static void profile_discard_flip_buffers(void)
{
	int i, cpu;

	mutex_lock(&profile_flip_mutex);
	i = per_cpu(cpu_profile_flip, get_cpu());
	put_cpu();
	on_each_cpu(__profile_flip_buffers, NULL, 1);
	for_each_online_cpu(cpu) {
		struct profile_hit *hits = per_cpu(cpu_profile_hits, cpu)[i];
		memset(hits, 0, NR_PROFILE_HIT*sizeof(struct profile_hit));
	}
	mutex_unlock(&profile_flip_mutex);
}

static void do_profile_hits(int type, void *__pc, unsigned int nr_hits)
{
	unsigned long primary, secondary, flags, pc = (unsigned long)__pc;
	int i, j, cpu;
	struct profile_hit *hits;

	pc = min((pc - (unsigned long)_stext) >> prof_shift, prof_len - 1);
	i = primary = (pc & (NR_PROFILE_GRP - 1)) << PROFILE_GRPSHIFT;
	secondary = (~(pc << 1) & (NR_PROFILE_GRP - 1)) << PROFILE_GRPSHIFT;
	cpu = get_cpu();
	hits = per_cpu(cpu_profile_hits, cpu)[per_cpu(cpu_profile_flip, cpu)];
	if (!hits) {
		put_cpu();
		return;
	}
	/*
	 * We buffer the global profiler buffer into a per-CPU
	 * queue and thus reduce the number of global (and possibly
	 * NUMA-alien) accesses. The write-queue is self-coalescing:
	 */
	local_irq_save(flags);
	do {
		for (j = 0; j < PROFILE_GRPSZ; ++j) {
			if (hits[i + j].pc == pc) {
				hits[i + j].hits += nr_hits;
				goto out;
			} else if (!hits[i + j].hits) {
				hits[i + j].pc = pc;
				hits[i + j].hits = nr_hits;
				goto out;
			}
		}
		i = (i + secondary) & (NR_PROFILE_HIT - 1);
	} while (i != primary);

	/*
	 * Add the current hit(s) and flush the write-queue out
	 * to the global buffer:
	 */
	atomic_add(nr_hits, &prof_buffer[pc]);
	for (i = 0; i < NR_PROFILE_HIT; ++i) {
		atomic_add(hits[i].hits, &prof_buffer[hits[i].pc]);
		hits[i].pc = hits[i].hits = 0;
	}
out:
	local_irq_restore(flags);
	put_cpu();
}

static int profile_dead_cpu(unsigned int cpu)
{
	struct page *page;
	int i;

	if (cpumask_available(prof_cpu_mask))
		cpumask_clear_cpu(cpu, prof_cpu_mask);

	for (i = 0; i < 2; i++) {
		if (per_cpu(cpu_profile_hits, cpu)[i]) {
			page = virt_to_page(per_cpu(cpu_profile_hits, cpu)[i]);
			per_cpu(cpu_profile_hits, cpu)[i] = NULL;
			__free_page(page);
		}
	}
	return 0;
}

static int profile_prepare_cpu(unsigned int cpu)
{
	int i, node = cpu_to_mem(cpu);
	struct page *page;

	per_cpu(cpu_profile_flip, cpu) = 0;

	for (i = 0; i < 2; i++) {
		if (per_cpu(cpu_profile_hits, cpu)[i])
			continue;

		page = __alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
		if (!page) {
			profile_dead_cpu(cpu);
			return -ENOMEM;
		}
		per_cpu(cpu_profile_hits, cpu)[i] = page_address(page);

	}
	return 0;
}

static int profile_online_cpu(unsigned int cpu)
{
	if (cpumask_available(prof_cpu_mask))
		cpumask_set_cpu(cpu, prof_cpu_mask);

	return 0;
}

#else /* !CONFIG_SMP */
#define profile_flip_buffers()		do { } while (0)
#define profile_discard_flip_buffers()	do { } while (0)

static void do_profile_hits(int type, void *__pc, unsigned int nr_hits)
{
	unsigned long pc;
	pc = ((unsigned long)__pc - (unsigned long)_stext) >> prof_shift;
	atomic_add(nr_hits, &prof_buffer[min(pc, prof_len - 1)]);
}
#endif /* !CONFIG_SMP */

void profile_hits(int type, void *__pc, unsigned int nr_hits)
{
	if (prof_on != type || !prof_buffer)
		return;
	do_profile_hits(type, __pc, nr_hits);
}
EXPORT_SYMBOL_GPL(profile_hits);

void profile_tick(int type)
{
	struct pt_regs *regs = get_irq_regs();

	if (!user_mode(regs) && cpumask_available(prof_cpu_mask) &&
	    cpumask_test_cpu(smp_processor_id(), prof_cpu_mask))
		profile_hit(type, (void *)profile_pc(regs));
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

static int prof_cpu_mask_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%*pb\n", cpumask_pr_args(prof_cpu_mask));
	return 0;
}

static int prof_cpu_mask_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, prof_cpu_mask_proc_show, NULL);
}

static ssize_t prof_cpu_mask_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *pos)
{
	cpumask_var_t new_value;
	int err;

	if (!alloc_cpumask_var(&new_value, GFP_KERNEL))
		return -ENOMEM;

	err = cpumask_parse_user(buffer, count, new_value);
	if (!err) {
		cpumask_copy(prof_cpu_mask, new_value);
		err = count;
	}
	free_cpumask_var(new_value);
	return err;
}

static const struct proc_ops prof_cpu_mask_proc_ops = {
	.proc_open	= prof_cpu_mask_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= prof_cpu_mask_proc_write,
};

void create_prof_cpu_mask(void)
{
	/* create /proc/irq/prof_cpu_mask */
	proc_create("irq/prof_cpu_mask", 0600, NULL, &prof_cpu_mask_proc_ops);
}

/*
 * This function accesses profiling information. The returned data is
 * binary: the sampling step and the actual contents of the profile
 * buffer. Use of the program readprofile is recommended in order to
 * get meaningful info out of these data.
 */
static ssize_t
read_profile(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t read;
	char *pnt;
	unsigned long sample_step = 1UL << prof_shift;

	profile_flip_buffers();
	if (p >= (prof_len+1)*sizeof(unsigned int))
		return 0;
	if (count > (prof_len+1)*sizeof(unsigned int) - p)
		count = (prof_len+1)*sizeof(unsigned int) - p;
	read = 0;

	while (p < sizeof(unsigned int) && count > 0) {
		if (put_user(*((char *)(&sample_step)+p), buf))
			return -EFAULT;
		buf++; p++; count--; read++;
	}
	pnt = (char *)prof_buffer + p - sizeof(atomic_t);
	if (copy_to_user(buf, (void *)pnt, count))
		return -EFAULT;
	read += count;
	*ppos += read;
	return read;
}

/*
 * Writing to /proc/profile resets the counters
 *
 * Writing a 'profiling multiplier' value into it also re-sets the profiling
 * interrupt frequency, on architectures that support this.
 */
static ssize_t write_profile(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
#ifdef CONFIG_SMP
	extern int setup_profiling_timer(unsigned int multiplier);

	if (count == sizeof(int)) {
		unsigned int multiplier;

		if (copy_from_user(&multiplier, buf, sizeof(int)))
			return -EFAULT;

		if (setup_profiling_timer(multiplier))
			return -EINVAL;
	}
#endif
	profile_discard_flip_buffers();
	memset(prof_buffer, 0, prof_len * sizeof(atomic_t));
	return count;
}

static const struct proc_ops profile_proc_ops = {
	.proc_read	= read_profile,
	.proc_write	= write_profile,
	.proc_lseek	= default_llseek,
};

int __ref create_proc_profile(void)
{
	struct proc_dir_entry *entry;
#ifdef CONFIG_SMP
	enum cpuhp_state online_state;
#endif

	int err = 0;

	if (!prof_on)
		return 0;
#ifdef CONFIG_SMP
	err = cpuhp_setup_state(CPUHP_PROFILE_PREPARE, "PROFILE_PREPARE",
				profile_prepare_cpu, profile_dead_cpu);
	if (err)
		return err;

	err = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "AP_PROFILE_ONLINE",
				profile_online_cpu, NULL);
	if (err < 0)
		goto err_state_prep;
	online_state = err;
	err = 0;
#endif
	entry = proc_create("profile", S_IWUSR | S_IRUGO,
			    NULL, &profile_proc_ops);
	if (!entry)
		goto err_state_onl;
	proc_set_size(entry, (1 + prof_len) * sizeof(atomic_t));

	return err;
err_state_onl:
#ifdef CONFIG_SMP
	cpuhp_remove_state(online_state);
err_state_prep:
	cpuhp_remove_state(CPUHP_PROFILE_PREPARE);
#endif
	return err;
}
subsys_initcall(create_proc_profile);
#endif /* CONFIG_PROC_FS */
