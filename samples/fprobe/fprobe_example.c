// SPDX-License-Identifier: GPL-2.0-only
/*
 * Here's a sample kernel module showing the use of fprobe to dump a
 * stack trace and selected registers when kernel_clone() is called.
 *
 * For more information on theory of operation of kprobes, see
 * Documentation/trace/kprobes.rst
 *
 * You will see the trace data in /var/log/messages and on the console
 * whenever kernel_clone() is invoked to create a new process.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fprobe.h>
#include <linux/sched/debug.h>
#include <linux/slab.h>

#define BACKTRACE_DEPTH 16
#define MAX_SYMBOL_LEN 4096
static struct fprobe sample_probe;
static unsigned long nhit;

static char symbol[MAX_SYMBOL_LEN] = "kernel_clone";
module_param_string(symbol, symbol, sizeof(symbol), 0644);
MODULE_PARM_DESC(symbol, "Probed symbol(s), given by comma separated symbols or a wildcard pattern.");

static char nosymbol[MAX_SYMBOL_LEN] = "";
module_param_string(nosymbol, nosymbol, sizeof(nosymbol), 0644);
MODULE_PARM_DESC(nosymbol, "Not-probed symbols, given by a wildcard pattern.");

static bool stackdump = true;
module_param(stackdump, bool, 0644);
MODULE_PARM_DESC(stackdump, "Enable stackdump.");

static bool use_trace = false;
module_param(use_trace, bool, 0644);
MODULE_PARM_DESC(use_trace, "Use trace_printk instead of printk. This is only for debugging.");

static void show_backtrace(void)
{
	unsigned long stacks[BACKTRACE_DEPTH];
	unsigned int len;

	len = stack_trace_save(stacks, BACKTRACE_DEPTH, 2);
	stack_trace_print(stacks, len, 24);
}

static int sample_entry_handler(struct fprobe *fp, unsigned long ip,
				struct pt_regs *regs, void *data)
{
	if (use_trace)
		/*
		 * This is just an example, no kernel code should call
		 * trace_printk() except when actively debugging.
		 */
		trace_printk("Enter <%pS> ip = 0x%p\n", (void *)ip, (void *)ip);
	else
		pr_info("Enter <%pS> ip = 0x%p\n", (void *)ip, (void *)ip);
	nhit++;
	if (stackdump)
		show_backtrace();
	return 0;
}

static void sample_exit_handler(struct fprobe *fp, unsigned long ip, struct pt_regs *regs,
				void *data)
{
	unsigned long rip = instruction_pointer(regs);

	if (use_trace)
		/*
		 * This is just an example, no kernel code should call
		 * trace_printk() except when actively debugging.
		 */
		trace_printk("Return from <%pS> ip = 0x%p to rip = 0x%p (%pS)\n",
			(void *)ip, (void *)ip, (void *)rip, (void *)rip);
	else
		pr_info("Return from <%pS> ip = 0x%p to rip = 0x%p (%pS)\n",
			(void *)ip, (void *)ip, (void *)rip, (void *)rip);
	nhit++;
	if (stackdump)
		show_backtrace();
}

static int __init fprobe_init(void)
{
	char *p, *symbuf = NULL;
	const char **syms;
	int ret, count, i;

	sample_probe.entry_handler = sample_entry_handler;
	sample_probe.exit_handler = sample_exit_handler;

	if (strchr(symbol, '*')) {
		/* filter based fprobe */
		ret = register_fprobe(&sample_probe, symbol,
				      nosymbol[0] == '\0' ? NULL : nosymbol);
		goto out;
	} else if (!strchr(symbol, ',')) {
		symbuf = symbol;
		ret = register_fprobe_syms(&sample_probe, (const char **)&symbuf, 1);
		goto out;
	}

	/* Comma separated symbols */
	symbuf = kstrdup(symbol, GFP_KERNEL);
	if (!symbuf)
		return -ENOMEM;
	p = symbuf;
	count = 1;
	while ((p = strchr(++p, ',')) != NULL)
		count++;

	pr_info("%d symbols found\n", count);

	syms = kcalloc(count, sizeof(char *), GFP_KERNEL);
	if (!syms) {
		kfree(symbuf);
		return -ENOMEM;
	}

	p = symbuf;
	for (i = 0; i < count; i++)
		syms[i] = strsep(&p, ",");

	ret = register_fprobe_syms(&sample_probe, syms, count);
	kfree(syms);
	kfree(symbuf);
out:
	if (ret < 0)
		pr_err("register_fprobe failed, returned %d\n", ret);
	else
		pr_info("Planted fprobe at %s\n", symbol);

	return ret;
}

static void __exit fprobe_exit(void)
{
	unregister_fprobe(&sample_probe);

	pr_info("fprobe at %s unregistered. %ld times hit, %ld times missed\n",
		symbol, nhit, sample_probe.nmissed);
}

module_init(fprobe_init)
module_exit(fprobe_exit)
MODULE_LICENSE("GPL");
