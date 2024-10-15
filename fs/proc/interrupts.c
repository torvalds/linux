// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqnr.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

/*
 * /proc/interrupts
 */
static void *int_seq_start(struct seq_file *f, loff_t *pos)
{
	return *pos <= irq_get_nr_irqs() ? pos : NULL;
}

static void *int_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos > irq_get_nr_irqs())
		return NULL;
	return pos;
}

static void int_seq_stop(struct seq_file *f, void *v)
{
	/* Nothing to do */
}

static const struct seq_operations int_seq_ops = {
	.start = int_seq_start,
	.next  = int_seq_next,
	.stop  = int_seq_stop,
	.show  = show_interrupts
};

static int __init proc_interrupts_init(void)
{
	proc_create_seq("interrupts", 0, NULL, &int_seq_ops);
	return 0;
}
fs_initcall(proc_interrupts_init);
