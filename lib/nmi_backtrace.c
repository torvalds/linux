/*
 *  NMI backtrace support
 *
 * Gratuitously copied from arch/x86/kernel/apic/hw_nmi.c by Russell King,
 * with the following header:
 *
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 */
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/seq_buf.h>

#ifdef arch_trigger_all_cpu_backtrace
/* For reliability, we're prepared to waste bits here. */
static DECLARE_BITMAP(backtrace_mask, NR_CPUS) __read_mostly;
static cpumask_t printtrace_mask;

#define NMI_BUF_SIZE		4096

struct nmi_seq_buf {
	unsigned char		buffer[NMI_BUF_SIZE];
	struct seq_buf		seq;
};

/* Safe printing in NMI context */
static DEFINE_PER_CPU(struct nmi_seq_buf, nmi_print_seq);

/* "in progress" flag of arch_trigger_all_cpu_backtrace */
static unsigned long backtrace_flag;

static void print_seq_line(struct nmi_seq_buf *s, int start, int end)
{
	const char *buf = s->buffer + start;

	printk("%.*s", (end - start) + 1, buf);
}

/*
 * When raise() is called it will be is passed a pointer to the
 * backtrace_mask. Architectures that call nmi_cpu_backtrace()
 * directly from their raise() functions may rely on the mask
 * they are passed being updated as a side effect of this call.
 */
void nmi_trigger_all_cpu_backtrace(bool include_self,
				   void (*raise)(cpumask_t *mask))
{
	struct nmi_seq_buf *s;
	int i, cpu, this_cpu = get_cpu();

	if (test_and_set_bit(0, &backtrace_flag)) {
		/*
		 * If there is already a trigger_all_cpu_backtrace() in progress
		 * (backtrace_flag == 1), don't output double cpu dump infos.
		 */
		put_cpu();
		return;
	}

	cpumask_copy(to_cpumask(backtrace_mask), cpu_online_mask);
	if (!include_self)
		cpumask_clear_cpu(this_cpu, to_cpumask(backtrace_mask));

	cpumask_copy(&printtrace_mask, to_cpumask(backtrace_mask));

	/*
	 * Set up per_cpu seq_buf buffers that the NMIs running on the other
	 * CPUs will write to.
	 */
	for_each_cpu(cpu, to_cpumask(backtrace_mask)) {
		s = &per_cpu(nmi_print_seq, cpu);
		seq_buf_init(&s->seq, s->buffer, NMI_BUF_SIZE);
	}

	if (!cpumask_empty(to_cpumask(backtrace_mask))) {
		pr_info("Sending NMI to %s CPUs:\n",
			(include_self ? "all" : "other"));
		raise(to_cpumask(backtrace_mask));
	}

	/* Wait for up to 10 seconds for all CPUs to do the backtrace */
	for (i = 0; i < 10 * 1000; i++) {
		if (cpumask_empty(to_cpumask(backtrace_mask)))
			break;
		mdelay(1);
		touch_softlockup_watchdog();
	}

	/*
	 * Now that all the NMIs have triggered, we can dump out their
	 * back traces safely to the console.
	 */
	for_each_cpu(cpu, &printtrace_mask) {
		int len, last_i = 0;

		s = &per_cpu(nmi_print_seq, cpu);
		len = seq_buf_used(&s->seq);
		if (!len)
			continue;

		/* Print line by line. */
		for (i = 0; i < len; i++) {
			if (s->buffer[i] == '\n') {
				print_seq_line(s, last_i, i);
				last_i = i + 1;
			}
		}
		/* Check if there was a partial line. */
		if (last_i < len) {
			print_seq_line(s, last_i, len - 1);
			pr_cont("\n");
		}
	}

	clear_bit(0, &backtrace_flag);
	smp_mb__after_atomic();
	put_cpu();
}

/*
 * It is not safe to call printk() directly from NMI handlers.
 * It may be fine if the NMI detected a lock up and we have no choice
 * but to do so, but doing a NMI on all other CPUs to get a back trace
 * can be done with a sysrq-l. We don't want that to lock up, which
 * can happen if the NMI interrupts a printk in progress.
 *
 * Instead, we redirect the vprintk() to this nmi_vprintk() that writes
 * the content into a per cpu seq_buf buffer. Then when the NMIs are
 * all done, we can safely dump the contents of the seq_buf to a printk()
 * from a non NMI context.
 */
static int nmi_vprintk(const char *fmt, va_list args)
{
	struct nmi_seq_buf *s = this_cpu_ptr(&nmi_print_seq);
	unsigned int len = seq_buf_used(&s->seq);

	seq_buf_vprintf(&s->seq, fmt, args);
	return seq_buf_used(&s->seq) - len;
}

bool nmi_cpu_backtrace(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	if (cpumask_test_cpu(cpu, to_cpumask(backtrace_mask))) {
		printk_func_t printk_func_save = this_cpu_read(printk_func);

		/* Replace printk to write into the NMI seq */
		this_cpu_write(printk_func, nmi_vprintk);
		pr_warn("NMI backtrace for cpu %d\n", cpu);
		if (regs)
			show_regs(regs);
		else
			dump_stack();
		this_cpu_write(printk_func, printk_func_save);

		cpumask_clear_cpu(cpu, to_cpumask(backtrace_mask));
		return true;
	}

	return false;
}
NOKPROBE_SYMBOL(nmi_cpu_backtrace);
#endif
