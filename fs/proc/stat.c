#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/irqnr.h>
#include <asm/cputime.h>
#include <linux/tick.h>

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif
#ifndef arch_idle_time
#define arch_idle_time(cpu) 0
#endif

static cputime64_t get_idle_time(int cpu)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, NULL);
	cputime64_t idle;

	if (idle_time == -1ULL) {
		/* !NO_HZ so we can rely on cpustat.idle */
		idle = kstat_cpu(cpu).cpustat.idle;
		idle = cputime64_add(idle, arch_idle_time(cpu));
	} else
		idle = usecs_to_cputime64(idle_time);

	return idle;
}

static cputime64_t get_iowait_time(int cpu)
{
	u64 iowait_time = get_cpu_iowait_time_us(cpu, NULL);
	cputime64_t iowait;

	if (iowait_time == -1ULL)
		/* !NO_HZ so we can rely on cpustat.iowait */
		iowait = kstat_cpu(cpu).cpustat.iowait;
	else
		iowait = usecs_to_cputime64(iowait_time);

	return iowait;
}

static int show_stat(struct seq_file *p, void *v)
{
	int i, j;
	unsigned long jif;
	cputime64_t user, nice, system, idle, iowait, irq, softirq, steal;
	cputime64_t guest, guest_nice;
	u64 sum = 0;
	u64 sum_softirq = 0;
	unsigned int per_softirq_sums[NR_SOFTIRQS] = {0};
	struct timespec boottime;

	user = nice = system = idle = iowait =
		irq = softirq = steal = cputime64_zero;
	guest = guest_nice = cputime64_zero;
	getboottime(&boottime);
	jif = boottime.tv_sec;

	for_each_possible_cpu(i) {
		user = cputime64_add(user, kstat_cpu(i).cpustat.user);
		nice = cputime64_add(nice, kstat_cpu(i).cpustat.nice);
		system = cputime64_add(system, kstat_cpu(i).cpustat.system);
		idle = cputime64_add(idle, get_idle_time(i));
		iowait = cputime64_add(iowait, get_iowait_time(i));
		irq = cputime64_add(irq, kstat_cpu(i).cpustat.irq);
		softirq = cputime64_add(softirq, kstat_cpu(i).cpustat.softirq);
		steal = cputime64_add(steal, kstat_cpu(i).cpustat.steal);
		guest = cputime64_add(guest, kstat_cpu(i).cpustat.guest);
		guest_nice = cputime64_add(guest_nice,
			kstat_cpu(i).cpustat.guest_nice);
		sum += kstat_cpu_irqs_sum(i);
		sum += arch_irq_stat_cpu(i);

		for (j = 0; j < NR_SOFTIRQS; j++) {
			unsigned int softirq_stat = kstat_softirqs_cpu(j, i);

			per_softirq_sums[j] += softirq_stat;
			sum_softirq += softirq_stat;
		}
	}
	sum += arch_irq_stat();

	seq_printf(p, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu "
		"%llu\n",
		(unsigned long long)cputime64_to_clock_t(user),
		(unsigned long long)cputime64_to_clock_t(nice),
		(unsigned long long)cputime64_to_clock_t(system),
		(unsigned long long)cputime64_to_clock_t(idle),
		(unsigned long long)cputime64_to_clock_t(iowait),
		(unsigned long long)cputime64_to_clock_t(irq),
		(unsigned long long)cputime64_to_clock_t(softirq),
		(unsigned long long)cputime64_to_clock_t(steal),
		(unsigned long long)cputime64_to_clock_t(guest),
		(unsigned long long)cputime64_to_clock_t(guest_nice));
	for_each_online_cpu(i) {
		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user = kstat_cpu(i).cpustat.user;
		nice = kstat_cpu(i).cpustat.nice;
		system = kstat_cpu(i).cpustat.system;
		idle = get_idle_time(i);
		iowait = get_iowait_time(i);
		irq = kstat_cpu(i).cpustat.irq;
		softirq = kstat_cpu(i).cpustat.softirq;
		steal = kstat_cpu(i).cpustat.steal;
		guest = kstat_cpu(i).cpustat.guest;
		guest_nice = kstat_cpu(i).cpustat.guest_nice;
		seq_printf(p,
			"cpu%d %llu %llu %llu %llu %llu %llu %llu %llu %llu "
			"%llu\n",
			i,
			(unsigned long long)cputime64_to_clock_t(user),
			(unsigned long long)cputime64_to_clock_t(nice),
			(unsigned long long)cputime64_to_clock_t(system),
			(unsigned long long)cputime64_to_clock_t(idle),
			(unsigned long long)cputime64_to_clock_t(iowait),
			(unsigned long long)cputime64_to_clock_t(irq),
			(unsigned long long)cputime64_to_clock_t(softirq),
			(unsigned long long)cputime64_to_clock_t(steal),
			(unsigned long long)cputime64_to_clock_t(guest),
			(unsigned long long)cputime64_to_clock_t(guest_nice));
	}
	seq_printf(p, "intr %llu", (unsigned long long)sum);

	/* sum again ? it could be updated? */
	for_each_irq_nr(j)
		seq_printf(p, " %u", kstat_irqs(j));

	seq_printf(p,
		"\nctxt %llu\n"
		"btime %lu\n"
		"processes %lu\n"
		"procs_running %lu\n"
		"procs_blocked %lu\n",
		nr_context_switches(),
		(unsigned long)jif,
		total_forks,
		nr_running(),
		nr_iowait());

	seq_printf(p, "softirq %llu", (unsigned long long)sum_softirq);

	for (i = 0; i < NR_SOFTIRQS; i++)
		seq_printf(p, " %u", per_softirq_sums[i]);
	seq_putc(p, '\n');

	return 0;
}

static int stat_open(struct inode *inode, struct file *file)
{
	unsigned size = 4096 * (1 + num_possible_cpus() / 32);
	char *buf;
	struct seq_file *m;
	int res;

	/* don't ask for more than the kmalloc() max size */
	if (size > KMALLOC_MAX_SIZE)
		size = KMALLOC_MAX_SIZE;
	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	res = single_open(file, show_stat, NULL);
	if (!res) {
		m = file->private_data;
		m->buf = buf;
		m->size = size;
	} else
		kfree(buf);
	return res;
}

static const struct file_operations proc_stat_operations = {
	.open		= stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_stat_init(void)
{
	proc_create("stat", 0, NULL, &proc_stat_operations);
	return 0;
}
module_init(proc_stat_init);
