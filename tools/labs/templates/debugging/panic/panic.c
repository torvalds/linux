#include <linux/module.h>
#include <linux/timer.h>

static struct timer_list panic_timer;

static void do_panic(struct timer_list *unused)
{
	*(int*)0x42 = 'a';
}

static int so2_panic_init(void)
{
	pr_info("panic_init\n");

	timer_setup(&panic_timer,  do_panic, 0);
	mod_timer(&panic_timer, jiffies + 2 * HZ);

	return 0;
}

static void so2_panic_exit(void)
{
	pr_info("panic exit\n");
	del_timer_sync(&panic_timer);
}

MODULE_LICENSE("GPL v2");
module_init(so2_panic_init);
module_exit(so2_panic_exit);
