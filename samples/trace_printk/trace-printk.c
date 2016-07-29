#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/irq_work.h>

/* Must not be static to force gcc to consider these non constant */
char *trace_printk_test_global_str =
	"This is a dynamic string that will use trace_puts\n";

char *trace_printk_test_global_str_irq =
	"(irq) This is a dynamic string that will use trace_puts\n";

char *trace_printk_test_global_str_fmt =
	"%sThis is a %s that will use trace_printk\n";

static struct irq_work irqwork;

static void trace_printk_irq_work(struct irq_work *work)
{
	trace_printk("(irq) This is a static string that will use trace_bputs\n");
	trace_printk(trace_printk_test_global_str_irq);

	trace_printk("(irq) This is a %s that will use trace_bprintk()\n",
		     "static string");

	trace_printk(trace_printk_test_global_str_fmt,
		     "(irq) ", "dynamic string");
}

static int __init trace_printk_init(void)
{
	init_irq_work(&irqwork, trace_printk_irq_work);

	trace_printk("This is a static string that will use trace_bputs\n");
	trace_printk(trace_printk_test_global_str);

	/* Kick off printing in irq context */
	irq_work_queue(&irqwork);

	trace_printk("This is a %s that will use trace_bprintk()\n",
		     "static string");

	trace_printk(trace_printk_test_global_str_fmt, "", "dynamic string");

	return 0;
}

static void __exit trace_printk_exit(void)
{
}

module_init(trace_printk_init);
module_exit(trace_printk_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("trace-printk");
MODULE_LICENSE("GPL");
