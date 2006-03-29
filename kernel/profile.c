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
 *	William Irwin, Oracle, July 2004
 *  Amortized hit count accounting via per-cpu open-addressed hashtables
 *	to resolve timer interrupt livelocks, William Irwin, Oracle, 2004
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/profile.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/profile.h>
#include <linux/highmem.h>
#include <linux/mutex.h>
#include <asm/sections.h>
#include <asm/semaphore.h>

struct profile_hit {
	u32 pc, hits;
};
#define PROFILE_GRPSHIFT	3
#define PROFILE_GRPSZ		(1 << PROFILE_GRPSHIFT)
#define NR_PROFILE_HIT		(PAGE_SIZE/sizeof(struct profile_hit))
#define NR_PROFILE_GRP		(NR_PROFILE_HIT/PROFILE_GRPSZ)

/* Oprofile timer tick hook */
int (*timer_hook)(struct pt_regs *) __read_mostly;

static atomic_t *prof_buffer;
static unsigned long prof_len, prof_shift;
static int prof_on __read_mostly;
static cpumask_t prof_cpu_mask = CPU_MASK_ALL;
#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct profile_hit *[2], cpu_profile_hits);
static DEFINE_PER_CPU(int, cpu_profile_flip);
static DEFINE_MUTEX(profile_flip_mutex);
#endif /* CONFIG_SMP */

static int __init profile_setup(char * str)
{
	static char __initdata schedstr[] = "schedule";
	int par;

	if (!strncmp(str, schedstr, strlen(schedstr))) {
		prof_on = SCHED_PROFILING;
		if (str[strlen(schedstr)] == ',')
			str += strlen(schedstr) + 1;
		if (get_option(&str, &par))
			prof_shift = par;
		printk(KERN_INFO
			"kernel schedule profiling enabled (shift: %ld)\n",
			prof_shift);
	} else if (get_option(&str, &par)) {
		prof_shift = par;
		prof_on = CPU_PROFILING;
		printk(KERN_INFO "kernel profiling enabled (shift: %ld)\n",
			prof_shift);
	}
	return 1;
}
__setup("profile=", profile_setup);


void __init profile_init(void)
{
	if (!prof_on) 
		return;
 
	/* only text is profiled */
	prof_len = (_etext - _stext) >> prof_shift;
	prof_buffer = alloc_bootmem(prof_len*sizeof(atomic_t));
}

/* Profile event notifications */
 
#ifdef CONFIG_PROFILING
 
static BLOCKING_NOTIFIER_HEAD(task_exit_notifier);
static ATOMIC_NOTIFIER_HEAD(task_free_notifier);
static BLOCKING_NOTIFIER_HEAD(munmap_notifier);
 
void profile_task_exit(struct task_struct * task)
{
	blocking_notifier_call_chain(&task_exit_notifier, 0, task);
}
 
int profile_handoff_task(struct task_struct * task)
{
	int ret;
	ret = atomic_notifier_call_chain(&task_free_notifier, 0, task);
	return (ret == NOTIFY_OK) ? 1 : 0;
}

void profile_munmap(unsigned long addr)
{
	blocking_notifier_call_chain(&munmap_notifier, 0, (void *)addr);
}

int task_handoff_register(struct notifier_block * n)
{
	return atomic_notifier_chain_register(&task_free_notifier, n);
}

int task_handoff_unregister(struct notifier_block * n)
{
	return atomic_notifier_chain_unregister(&task_free_notifier, n);
}

int profile_event_register(enum profile_type type, struct notifier_block * n)
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

 
int profile_event_unregister(enum profile_type type, struct notifier_block * n)
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

int register_timer_hook(int (*hook)(struct pt_regs *))
{
	if (timer_hook)
		return -EBUSY;
	timer_hook = hook;
	return 0;
}

void unregister_timer_hook(int (*hook)(struct pt_regs *))
{
	WARN_ON(hook != timer_hook);
	timer_hook = NULL;
	/* make sure all CPUs see the NULL hook */
	synchronize_sched();  /* Allow ongoing interrupts to complete. */
}

EXPORT_SYMBOL_GPL(register_timer_hook);
EXPORT_SYMBOL_GPL(unregister_timer_hook);
EXPORT_SYMBOL_GPL(task_handoff_register);
EXPORT_SYMBOL_GPL(task_handoff_unregister);

#endif /* CONFIG_PROFILING */

EXPORT_SYMBOL_GPL(profile_event_register);
EXPORT_SYMBOL_GPL(profile_event_unregister);

#ifdef CONFIG_SMP
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
 * SCHED_PROFILING profile_hit() may be called from process context).
 * The hash function is meant to be lightweight as opposed to strong,
 * and was vaguely inspired by ppc64 firmware-supported inverted
 * pagetable hash functions, but uses a full hashtable full of finite
 * collision chains, not just pairs of them.
 *
 * -- wli
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
	on_each_cpu(__profile_flip_buffers, NULL, 0, 1);
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
	on_each_cpu(__profile_flip_buffers, NULL, 0, 1);
	for_each_online_cpu(cpu) {
		struct profile_hit *hits = per_cpu(cpu_profile_hits, cpu)[i];
		memset(hits, 0, NR_PROFILE_HIT*sizeof(struct profile_hit));
	}
	mutex_unlock(&profile_flip_mutex);
}

void profile_hit(int type, void *__pc)
{
	unsigned long primary, secondary, flags, pc = (unsigned long)__pc;
	int i, j, cpu;
	struct profile_hit *hits;

	if (prof_on != type || !prof_buffer)
		return;
	pc = min((pc - (unsigned long)_stext) >> prof_shift, prof_len - 1);
	i = primary = (pc & (NR_PROFILE_GRP - 1)) << PROFILE_GRPSHIFT;
	secondary = (~(pc << 1) & (NR_PROFILE_GRP - 1)) << PROFILE_GRPSHIFT;
	cpu = get_cpu();
	hits = per_cpu(cpu_profile_hits, cpu)[per_cpu(cpu_profile_flip, cpu)];
	if (!hits) {
		put_cpu();
		return;
	}
	local_irq_save(flags);
	do {
		for (j = 0; j < PROFILE_GRPSZ; ++j) {
			if (hits[i + j].pc == pc) {
				hits[i + j].hits++;
				goto out;
			} else if (!hits[i + j].hits) {
				hits[i + j].pc = pc;
				hits[i + j].hits = 1;
				goto out;
			}
		}
		i = (i + secondary) & (NR_PROFILE_HIT - 1);
	} while (i != primary);
	atomic_inc(&prof_buffer[pc]);
	for (i = 0; i < NR_PROFILE_HIT; ++i) {
		atomic_add(hits[i].hits, &prof_buffer[hits[i].pc]);
		hits[i].pc = hits[i].hits = 0;
	}
out:
	local_irq_restore(flags);
	put_cpu();
}

#ifdef CONFIG_HOTPLUG_CPU
static int __devinit profile_cpu_callback(struct notifier_block *info,
					unsigned long action, void *__cpu)
{
	int node, cpu = (unsigned long)__cpu;
	struct page *page;

	switch (action) {
	case CPU_UP_PREPARE:
		node = cpu_to_node(cpu);
		per_cpu(cpu_profile_flip, cpu) = 0;
		if (!per_cpu(cpu_profile_hits, cpu)[1]) {
			page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
			if (!page)
				return NOTIFY_BAD;
			per_cpu(cpu_profile_hits, cpu)[1] = page_address(page);
		}
		if (!per_cpu(cpu_profile_hits, cpu)[0]) {
			page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
			if (!page)
				goto out_free;
			per_cpu(cpu_profile_hits, cpu)[0] = page_address(page);
		}
		break;
	out_free:
		page = virt_to_page(per_cpu(cpu_profile_hits, cpu)[1]);
		per_cpu(cpu_profile_hits, cpu)[1] = NULL;
		__free_page(page);
		return NOTIFY_BAD;
	case CPU_ONLINE:
		cpu_set(cpu, prof_cpu_mask);
		break;
	case CPU_UP_CANCELED:
	case CPU_DEAD:
		cpu_clear(cpu, prof_cpu_mask);
		if (per_cpu(cpu_profile_hits, cpu)[0]) {
			page = virt_to_page(per_cpu(cpu_profile_hits, cpu)[0]);
			per_cpu(cpu_profile_hits, cpu)[0] = NULL;
			__free_page(page);
		}
		if (per_cpu(cpu_profile_hits, cpu)[1]) {
			page = virt_to_page(per_cpu(cpu_profile_hits, cpu)[1]);
			per_cpu(cpu_profile_hits, cpu)[1] = NULL;
			__free_page(page);
		}
		break;
	}
	return NOTIFY_OK;
}
#endif /* CONFIG_HOTPLUG_CPU */
#else /* !CONFIG_SMP */
#define profile_flip_buffers()		do { } while (0)
#define profile_discard_flip_buffers()	do { } while (0)

void profile_hit(int type, void *__pc)
{
	unsigned long pc;

	if (prof_on != type || !prof_buffer)
		return;
	pc = ((unsigned long)__pc - (unsigned long)_stext) >> prof_shift;
	atomic_inc(&prof_buffer[min(pc, prof_len - 1)]);
}
#endif /* !CONFIG_SMP */

void profile_tick(int type, struct pt_regs *regs)
{
	if (type == CPU_PROFILING && timer_hook)
		timer_hook(regs);
	if (!user_mode(regs) && cpu_isset(smp_processor_id(), prof_cpu_mask))
		profile_hit(type, (void *)profile_pc(regs));
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>

static int prof_cpu_mask_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len = cpumask_scnprintf(page, count, *(cpumask_t *)data);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int prof_cpu_mask_write_proc (struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	cpumask_t *mask = (cpumask_t *)data;
	unsigned long full_count = count, err;
	cpumask_t new_value;

	err = cpumask_parse(buffer, count, new_value);
	if (err)
		return err;

	*mask = new_value;
	return full_count;
}

void create_prof_cpu_mask(struct proc_dir_entry *root_irq_dir)
{
	struct proc_dir_entry *entry;

	/* create /proc/irq/prof_cpu_mask */
	if (!(entry = create_proc_entry("prof_cpu_mask", 0600, root_irq_dir)))
		return;
	entry->nlink = 1;
	entry->data = (void *)&prof_cpu_mask;
	entry->read_proc = prof_cpu_mask_read_proc;
	entry->write_proc = prof_cpu_mask_write_proc;
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
	char * pnt;
	unsigned int sample_step = 1 << prof_shift;

	profile_flip_buffers();
	if (p >= (prof_len+1)*sizeof(unsigned int))
		return 0;
	if (count > (prof_len+1)*sizeof(unsigned int) - p)
		count = (prof_len+1)*sizeof(unsigned int) - p;
	read = 0;

	while (p < sizeof(unsigned int) && count > 0) {
		put_user(*((char *)(&sample_step)+p),buf);
		buf++; p++; count--; read++;
	}
	pnt = (char *)prof_buffer + p - sizeof(atomic_t);
	if (copy_to_user(buf,(void *)pnt,count))
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
	extern int setup_profiling_timer (unsigned int multiplier);

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

static struct file_operations proc_profile_operations = {
	.read		= read_profile,
	.write		= write_profile,
};

#ifdef CONFIG_SMP
static void __init profile_nop(void *unused)
{
}

static int __init create_hash_tables(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		int node = cpu_to_node(cpu);
		struct page *page;

		page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
		if (!page)
			goto out_cleanup;
		per_cpu(cpu_profile_hits, cpu)[1]
				= (struct profile_hit *)page_address(page);
		page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO, 0);
		if (!page)
			goto out_cleanup;
		per_cpu(cpu_profile_hits, cpu)[0]
				= (struct profile_hit *)page_address(page);
	}
	return 0;
out_cleanup:
	prof_on = 0;
	smp_mb();
	on_each_cpu(profile_nop, NULL, 0, 1);
	for_each_online_cpu(cpu) {
		struct page *page;

		if (per_cpu(cpu_profile_hits, cpu)[0]) {
			page = virt_to_page(per_cpu(cpu_profile_hits, cpu)[0]);
			per_cpu(cpu_profile_hits, cpu)[0] = NULL;
			__free_page(page);
		}
		if (per_cpu(cpu_profile_hits, cpu)[1]) {
			page = virt_to_page(per_cpu(cpu_profile_hits, cpu)[1]);
			per_cpu(cpu_profile_hits, cpu)[1] = NULL;
			__free_page(page);
		}
	}
	return -1;
}
#else
#define create_hash_tables()			({ 0; })
#endif

static int __init create_proc_profile(void)
{
	struct proc_dir_entry *entry;

	if (!prof_on)
		return 0;
	if (create_hash_tables())
		return -1;
	if (!(entry = create_proc_entry("profile", S_IWUSR | S_IRUGO, NULL)))
		return 0;
	entry->proc_fops = &proc_profile_operations;
	entry->size = (1+prof_len) * sizeof(atomic_t);
	hotcpu_notifier(profile_cpu_callback, 0);
	return 0;
}
module_init(create_proc_profile);
#endif /* CONFIG_PROC_FS */
