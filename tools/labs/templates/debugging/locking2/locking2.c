#include <linux/module.h>
#include <linux/timer.h>

static DEFINE_SPINLOCK(lock);

static void timerfn(struct timer_list *unused)
{
	pr_info("%s acquiring lock\n", __func__);
	spin_lock(&lock);   pr_info("%s acquired lock\n", __func__);
	spin_unlock(&lock); pr_info("%s released lock\n", __func__);
}

static DEFINE_TIMER(timer, timerfn);

int init_module(void)
{
	mod_timer(&timer, jiffies);

	pr_info("%s acquiring lock\n", __func__);
	spin_lock(&lock);   pr_info("%s acquired lock\n", __func__);
	spin_unlock(&lock); pr_info("%s released lock\n", __func__);
	return 0;
}

void exit_module(void)
{
	del_timer_sync(&timer);
}

MODULE_LICENSE("GPL v2");
