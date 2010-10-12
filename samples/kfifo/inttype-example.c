/*
 * Sample kfifo int type implementation
 *
 * Copyright (C) 2010 Stefani Seibold <stefani@seibold.net>
 *
 * Released under the GPL version 2 only.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>

/*
 * This module shows how to create a int type fifo.
 */

/* fifo size in elements (ints) */
#define FIFO_SIZE	32

/* name of the proc entry */
#define	PROC_FIFO	"int-fifo"

/* lock for procfs read access */
static DEFINE_MUTEX(read_lock);

/* lock for procfs write access */
static DEFINE_MUTEX(write_lock);

/*
 * define DYNAMIC in this example for a dynamically allocated fifo.
 *
 * Otherwise the fifo storage will be a part of the fifo structure.
 */
#if 0
#define DYNAMIC
#endif

#ifdef DYNAMIC
static DECLARE_KFIFO_PTR(test, int);
#else
static DEFINE_KFIFO(test, int, FIFO_SIZE);
#endif

static int __init testfunc(void)
{
	int		buf[6];
	int		i;
	unsigned int	ret;

	printk(KERN_INFO "int fifo test start\n");

	/* put values into the fifo */
	for (i = 0; i != 10; i++)
		kfifo_put(&test, &i);

	/* show the number of used elements */
	printk(KERN_INFO "fifo len: %u\n", kfifo_len(&test));

	/* get max of 2 elements from the fifo */
	ret = kfifo_out(&test, buf, 2);
	printk(KERN_INFO "ret: %d\n", ret);
	/* and put it back to the end of the fifo */
	ret = kfifo_in(&test, buf, ret);
	printk(KERN_INFO "ret: %d\n", ret);

	for (i = 20; i != 30; i++)
		kfifo_put(&test, &i);

	printk(KERN_INFO "queue len: %u\n", kfifo_len(&test));

	/* show the first value without removing from the fifo */
	if (kfifo_peek(&test, &i))
		printk(KERN_INFO "%d\n", i);

	/* print out all values in the fifo */
	while (kfifo_get(&test, &i))
		printk("%d ", i);
	printk("\n");

	return 0;
}

static ssize_t fifo_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret;
	unsigned int copied;

	if (mutex_lock_interruptible(&write_lock))
		return -ERESTARTSYS;

	ret = kfifo_from_user(&test, buf, count, &copied);

	mutex_unlock(&write_lock);

	return ret ? ret : copied;
}

static ssize_t fifo_read(struct file *file, char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret;
	unsigned int copied;

	if (mutex_lock_interruptible(&read_lock))
		return -ERESTARTSYS;

	ret = kfifo_to_user(&test, buf, count, &copied);

	mutex_unlock(&read_lock);

	return ret ? ret : copied;
}

static const struct file_operations fifo_fops = {
	.owner		= THIS_MODULE,
	.read		= fifo_read,
	.write		= fifo_write,
};

static int __init example_init(void)
{
#ifdef DYNAMIC
	int ret;

	ret = kfifo_alloc(&test, FIFO_SIZE, GFP_KERNEL);
	if (ret) {
		printk(KERN_ERR "error kfifo_alloc\n");
		return ret;
	}
#endif
	testfunc();

	if (proc_create(PROC_FIFO, 0, NULL, &fifo_fops) == NULL) {
#ifdef DYNAMIC
		kfifo_free(&test);
#endif
		return -ENOMEM;
	}
	return 0;
}

static void __exit example_exit(void)
{
	remove_proc_entry(PROC_FIFO, NULL);
#ifdef DYNAMIC
	kfifo_free(&test);
#endif
}

module_init(example_init);
module_exit(example_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stefani Seibold <stefani@seibold.net>");
