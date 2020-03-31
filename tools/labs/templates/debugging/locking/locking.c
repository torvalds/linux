#include <linux/module.h>
#include <linux/kthread.h>

static DEFINE_MUTEX(a);
static DEFINE_MUTEX(b);

static noinline int thread_a(void *unused)
{
	mutex_lock(&a); pr_info("%s acquired A\n", __func__);
	mutex_lock(&b);	pr_info("%s acquired B\n", __func__);

	mutex_unlock(&b);
	mutex_unlock(&a);

	return 0;
}

static noinline int thread_b(void *unused)
{
	mutex_lock(&b); pr_info("%s acquired B\n", __func__);
	mutex_lock(&a); pr_info("%s acquired A\n", __func__);

	mutex_unlock(&a);
	mutex_unlock(&b);

	return 0;
}


int init_module(void)
{
	kthread_run(thread_a, NULL, "thread_a");
	kthread_run(thread_b, NULL, "thread_b");

	return 0;
}

void exit_module(void)
{
}

MODULE_LICENSE("GPL v2");
