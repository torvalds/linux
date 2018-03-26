#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kthread.h>

DEFINE_MUTEX(a);
DEFINE_MUTEX(b);

struct task_struct *t1, *t2;

static noinline int thread_a(void *unused)
{
	mutex_lock(&a);
	schedule_timeout(HZ);
	mutex_lock(&b);

	mutex_unlock(&b);
	mutex_unlock(&a);

	return 0;
}

static noinline int thread_b(void *unused)
{
	mutex_lock(&b);
	schedule_timeout(HZ);
	mutex_lock(&a);

	mutex_unlock(&a);
	mutex_unlock(&b);

	return 0;
}


static int so2_locking_init(void)
{
	pr_info("locking_init\n");

	t1 = kthread_run(thread_a, NULL, "thread_a");
	t2 = kthread_run(thread_b, NULL, "thread_b");

	return 0;
}

static void so2_locking_exit(void)
{
	pr_info("locking exit\n");
}

MODULE_LICENSE("GPL v2");
module_init(so2_locking_init);
module_exit(so2_locking_exit);
