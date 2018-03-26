#include <linux/module.h>
#include <linux/timer.h>

spinlock_t lock;
static struct timer_list timer;
 
static void foo(struct timer_list *unused)
{
	spin_lock(&lock);

	spin_unlock(&lock);
}

static int so2_locking_init(void)
{
	pr_info("timer init\n");
	timer_setup(&timer, foo, 0);
	mod_timer(&timer, jiffies);

	pr_info("timer setup ok!\n");

	spin_lock_init(&lock);
	spin_lock(&lock);

	/* do work */
	while(1);

	return 0;
}

static void so2_locking_exit(void)
{
	pr_info("locking exit\n");
	del_timer_sync(&timer);
}

MODULE_LICENSE("GPL v2");
module_init(so2_locking_init);
module_exit(so2_locking_exit);
