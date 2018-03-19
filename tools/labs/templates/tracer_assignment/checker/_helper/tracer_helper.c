#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <asm/ioctl.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>

#include "helper.h"


#define HELPER_MINOR 30

/* number of paralel kernel threads */
static wait_queue_head_t wq[MCOUNT];

/*XXX: this looks like a poor design, please refactor */
static int thread_prepared[MCOUNT] = {0, };
static int thread_running[MCOUNT] = {0, };
static int thread_should_stop[MCOUNT] = {0, };
static struct task_struct *t[MCOUNT] = {NULL, };

static struct test_params tp[MCOUNT];
static int tcount;

void do_work(void)
{
	int i, j;
	int a = 0;

	for (i = 0; i < 1000; i++)
		for (j = 0; j < 1000; j++)
			a = i * j;
}

int thread_fn(void *data)
{
	int i;

	void *k_addr[MCOUNT];
	struct semaphore sem;
	struct mutex lock;

	struct test_params *tp;

	tp = (struct test_params *)data;

	thread_prepared[tp->idx] = 1;
	wake_up_interruptible(&wq[tp->idx]);

	wait_event_interruptible(wq[tp->idx], thread_running[tp->idx] == 1);

	for (i = 0; i < tp->kcalls; i++)
		k_addr[i] = kmalloc(tp->alloc[i], GFP_KERNEL);

	/*XXX: do proper cleanup, avoid memory leaks */
	for (i = 0; i < tp->kcalls; i++)
		if (tp->free[i] && k_addr[i])
			kfree(k_addr[i]);

	for (i = 0; i < tp->sched; i++)
		schedule();

	/* ***: use tp->down for down_interruptible */
	sema_init(&sem, 1);
	for (i = 0; i < tp->up; i++) {
		up(&sem);
		do_work();
		down_interruptible(&sem);
	}
	/* ***: use to->unlock for mutex_unlock */
	mutex_init(&lock);
	for (i = 0; i < tp->lock; i++) {
		mutex_lock(&lock);
		do_work();
		mutex_unlock(&lock);
	}

	wait_event_interruptible(wq[tp->idx], thread_should_stop[tp->idx] == 1);

	/* reset state machine */
	thread_prepared[tp->idx] = 0;
	thread_running[tp->idx] = 0;
	thread_should_stop[tp->idx] = 0;

	return 0;
}
static int helper_open(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	pr_info("tracer-helper: open\n");
#endif
	return 0;
}

static int helper_release(struct inode *inode, struct file *file)
{
#ifdef DEBUG
	pr_info("tracer-helper: close\n");
#endif
	return 0;
}

static long helper_ioctl(struct file *filp, unsigned int cmd,
			 unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case PREPARE_TEST:
		if (copy_from_user(&tp[tcount], (struct test_param *)arg,
			sizeof(tp[tcount]))) {
			pr_info("Error copy to user\n");
			return -EFAULT;
		}
		t[tp[tcount].idx] = kthread_run(thread_fn, &tp[tcount], "%s",
			tp[tcount].thread_name);
		if (!t[tp[tcount].idx]) {
			pr_info("Could not create thread!\n");
			return -ENOMEM;
		}

		ret = t[tp[tcount].idx]->pid;
		wait_event_interruptible(wq[tp[tcount].idx],
			thread_prepared[tp[tcount].idx] == 1);
		tcount++;
		break;
	case START_TEST:
#if 0
		pr_info("%s: start test for idx %lu\n", __func__, arg);
#endif
		thread_running[arg] = 1;
		wake_up_interruptible(&wq[arg]);
		break;
	case STOP_TEST:
#if 0
		pr_info("%s: stop test for idx %lu\n", __func__, arg);
#endif
		thread_should_stop[arg] = 1;
		wake_up_interruptible(&wq[arg]);
		kthread_stop(t[arg]);
		break;
	default:
		break;
	}

	return ret;
}

static const struct file_operations tracer_fops = {
	.open		= helper_open,
	.unlocked_ioctl	= helper_ioctl,
	.release	= helper_release,
};

static struct miscdevice helper_dev = {
	.minor	= HELPER_MINOR,
	.name	= "helper",
	.fops	= &tracer_fops,
};

static int __init tracer_helper_init(void)
{
	int rc, i;

	rc = misc_register(&helper_dev);
	if (rc < 0) {
		pr_err("misc_register: fail\n");
		return rc;
	}

	for (i = 0; i < MCOUNT; i++)
		init_waitqueue_head(&wq[i]);
#ifdef DEBUG
	pr_info("tracer-helper: init\n");
#endif
	return 0;
}

static void __exit tracer_helper_exit(void)
{
	misc_deregister(&helper_dev);
#ifdef DEBUG
	pr_info("tracer-helper: exit\n");
#endif
}

MODULE_AUTHOR("Daniel Baluta");
MODULE_LICENSE("GPL");

module_init(tracer_helper_init);
module_exit(tracer_helper_exit);
