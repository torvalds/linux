// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * hung_task_tests.c - Sample code for testing hung tasks with mutex,
 * semaphore, etc.
 *
 * Usage: Load this module and read `<debugfs>/hung_task/mutex`,
 *        `<debugfs>/hung_task/semaphore`, etc., with 2 or more processes.
 *
 * This is for testing kernel hung_task error messages with various locking
 * mechanisms (e.g., mutex, semaphore, etc.). Note that this may freeze
 * your system or cause a panic. Use only for testing purposes.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>

#define HUNG_TASK_DIR		"hung_task"
#define HUNG_TASK_MUTEX_FILE	"mutex"
#define HUNG_TASK_SEM_FILE	"semaphore"
#define SLEEP_SECOND		256

static const char dummy_string[] = "This is a dummy string.";
static DEFINE_MUTEX(dummy_mutex);
static DEFINE_SEMAPHORE(dummy_sem, 1);
static struct dentry *hung_task_dir;

/* Mutex-based read function */
static ssize_t read_dummy_mutex(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	/* Second task waits on mutex, entering uninterruptible sleep */
	guard(mutex)(&dummy_mutex);

	/* First task sleeps here, interruptible */
	msleep_interruptible(SLEEP_SECOND * 1000);

	return simple_read_from_buffer(user_buf, count, ppos, dummy_string,
				       sizeof(dummy_string));
}

/* Semaphore-based read function */
static ssize_t read_dummy_semaphore(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	/* Second task waits on semaphore, entering uninterruptible sleep */
	down(&dummy_sem);

	/* First task sleeps here, interruptible */
	msleep_interruptible(SLEEP_SECOND * 1000);

	up(&dummy_sem);

	return simple_read_from_buffer(user_buf, count, ppos, dummy_string,
				       sizeof(dummy_string));
}

/* File operations for mutex */
static const struct file_operations hung_task_mutex_fops = {
	.read = read_dummy_mutex,
};

/* File operations for semaphore */
static const struct file_operations hung_task_sem_fops = {
	.read = read_dummy_semaphore,
};

static int __init hung_task_tests_init(void)
{
	hung_task_dir = debugfs_create_dir(HUNG_TASK_DIR, NULL);
	if (IS_ERR(hung_task_dir))
		return PTR_ERR(hung_task_dir);

	/* Create debugfs files for mutex and semaphore tests */
	debugfs_create_file(HUNG_TASK_MUTEX_FILE, 0400, hung_task_dir, NULL,
			    &hung_task_mutex_fops);
	debugfs_create_file(HUNG_TASK_SEM_FILE, 0400, hung_task_dir, NULL,
			    &hung_task_sem_fops);

	return 0;
}

static void __exit hung_task_tests_exit(void)
{
	debugfs_remove_recursive(hung_task_dir);
}

module_init(hung_task_tests_init);
module_exit(hung_task_tests_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Masami Hiramatsu <mhiramat@kernel.org>");
MODULE_AUTHOR("Zi Li <amaindex@outlook.com>");
MODULE_DESCRIPTION("Simple sleep under lock files for testing hung task");
