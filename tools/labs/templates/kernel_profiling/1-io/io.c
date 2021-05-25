#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched/task.h>

#define MY_MAJOR		42
#define MY_MINOR		0
#define MODULE_NAME		"deferred"

#define TIMER_TYPE_NONE		-1
#define TIMER_TYPE_SET		0
#define TIMER_TYPE_ALLOC	1
#define TIMER_TYPE_MON		2

MODULE_DESCRIPTION("Generate disruptive interrupts");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

struct timer_list timer;

static void timer_handler(struct timer_list *tl)
{
	unsigned long deadline = jiffies + HZ;

	while (jiffies < deadline) {
		(void)0;
	}
	mod_timer(&timer, jiffies + HZ);
}

static int deferred_init(void)
{
	int err;

	pr_info("[deferred_init] Init module\n");

	timer_setup(&timer, timer_handler, 0);
	mod_timer(&timer, jiffies + 5 * HZ);

	return 0;
}

static void deferred_exit(void)
{
	struct mon_proc *p, *n;

	pr_info("[deferred_exit] Exit module\n" );

	del_timer_sync(&timer);
}

module_init(deferred_init);
module_exit(deferred_exit);
