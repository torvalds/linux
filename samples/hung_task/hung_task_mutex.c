// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * hung_task_mutex.c - Sample code which causes hung task by mutex
 *
 * Usage: load this module and read `<debugfs>/hung_task/mutex`
 *        by 2 or more processes.
 *
 * This is for testing kernel hung_task error message.
 * Note that this will make your system freeze and maybe
 * cause panic. So do not use this except for the test.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>

#define HUNG_TASK_DIR   "hung_task"
#define HUNG_TASK_FILE  "mutex"
#define SLEEP_SECOND 256

static const char dummy_string[] = "This is a dummy string.";
static DEFINE_MUTEX(dummy_mutex);
static struct dentry *hung_task_dir;

static ssize_t read_dummy(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	/* If the second task waits on the lock, it is uninterruptible sleep. */
	guard(mutex)(&dummy_mutex);

	/* When the first task sleep here, it is interruptible. */
	msleep_interruptible(SLEEP_SECOND * 1000);

	return simple_read_from_buffer(user_buf, count, ppos,
				dummy_string, sizeof(dummy_string));
}

static const struct file_operations hung_task_fops = {
	.read = read_dummy,
};

static int __init hung_task_sample_init(void)
{
	hung_task_dir = debugfs_create_dir(HUNG_TASK_DIR, NULL);
	if (IS_ERR(hung_task_dir))
		return PTR_ERR(hung_task_dir);

	debugfs_create_file(HUNG_TASK_FILE, 0400, hung_task_dir,
			    NULL, &hung_task_fops);

	return 0;
}

static void __exit hung_task_sample_exit(void)
{
	debugfs_remove_recursive(hung_task_dir);
}

module_init(hung_task_sample_init);
module_exit(hung_task_sample_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Masami Hiramatsu");
MODULE_DESCRIPTION("Simple sleep under mutex file for testing hung task");
