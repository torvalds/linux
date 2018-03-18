/*
 * Character device drivers lab
 *
 * All tasks
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "../include/so2_cdev.h"

MODULE_DESCRIPTION("SO2 character device");
MODULE_AUTHOR("SO2");
MODULE_LICENSE("GPL");

#define LOG_LEVEL	KERN_DEBUG

#define MY_MAJOR		42
#define MY_MINOR		0
#define NUM_MINORS		1
#define MODULE_NAME		"so2_cdev"
#define MESSAGE			"hello\n"
#define IOCTL_MESSAGE		"Hello ioctl"

#ifndef BUFSIZ
#define BUFSIZ		4096
#endif


struct so2_device_data {
	/* TODO 2/1: add cdev member */
	struct cdev cdev;
	/* TODO 4/2: add buffer with BUFSIZ elements */
	char buffer[BUFSIZ];
	size_t size;
	/* TODO 7/2: extra members for home */
	wait_queue_head_t wq;
	int flag;
	/* TODO 3/1: add atomic_t access variable to keep track if file is opened */
	atomic_t access;
};

struct so2_device_data devs[NUM_MINORS];

static int so2_cdev_open(struct inode *inode, struct file *file)
{
	struct so2_device_data *data;

	/* TODO 3/1: inode->i_cdev contains our cdev struct, use container_of to obtain a pointer to so2_device_data */
	data = container_of(inode->i_cdev, struct so2_device_data, cdev);

	file->private_data = data;

	/* TODO 3/2: return immediately if access is != 0, use atomic_cmpxchg */
	if (atomic_cmpxchg(&data->access, 0, 1) != 0)
		return -EBUSY;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(10);

	return 0;
}

static int
so2_cdev_release(struct inode *inode, struct file *file)
{
#ifndef EXTRA
	struct so2_device_data *data =
		(struct so2_device_data *) file->private_data;

	/* TODO 3/1: reset access variable to 0, use atomic_set */
	atomic_set(&data->access, 0);
#endif
	return 0;
}

static ssize_t
so2_cdev_read(struct file *file,
		char __user *user_buffer,
		size_t size, loff_t *offset)
{
	struct so2_device_data *data =
		(struct so2_device_data *) file->private_data;
	size_t to_read;

#ifdef EXTRA
	/* TODO 7/6: extra tasks for home */
	if (!data->size) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(data->wq, data->size != 0))
			return -ERESTARTSYS;
	}
#endif

	/* TODO 4/4: Copy data->buffer to user_buffer, use copy_to_user */
	to_read = (size > data->size - *offset) ? (data->size - *offset) : size;
	if (copy_to_user(user_buffer, data->buffer + *offset, to_read) != 0)
		return -EFAULT;
	*offset += to_read;

	return to_read;
}

static ssize_t
so2_cdev_write(struct file *file,
		const char __user *user_buffer,
		size_t size, loff_t *offset)
{
	struct so2_device_data *data =
		(struct so2_device_data *) file->private_data;


	/* TODO 5/5: copy user_buffer to data->buffer, use copy_from_user */
	size = (*offset + size > BUFSIZ) ? (BUFSIZ - *offset) : size;
	if (copy_from_user(data->buffer + *offset, user_buffer, size) != 0)
		return -EFAULT;
	*offset += size;
	data->size = *offset;
	/* TODO 7/3: extra tasks for home */
#ifdef EXTRA
	wake_up_interruptible(&data->wq);
#endif

	return size;
}

static long
so2_cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct so2_device_data *data =
		(struct so2_device_data *) file->private_data;
	int ret = 0;
	int remains;

	switch (cmd) {
	/* TODO 6/3: if cmd = MY_IOCTL_PRINT, display IOCTL_MESSAGE */
	case MY_IOCTL_PRINT:
		printk(LOG_LEVEL "%s\n", IOCTL_MESSAGE);
		break;
	/* TODO 7/19: extra tasks, for home */
	case MY_IOCTL_DOWN:
		data->flag = 0;
		ret = wait_event_interruptible(data->wq, data->flag != 0);
		break;
	case MY_IOCTL_UP:
		data->flag = 1;
		wake_up_interruptible(&data->wq);
		break;
	case MY_IOCTL_SET_BUFFER:
		remains = copy_from_user(data->buffer, (char __user *)arg,
				BUFFER_SIZE);
		if (remains)
			ret = -EFAULT;
		data->size = BUFFER_SIZE - remains;
		break;
	case MY_IOCTL_GET_BUFFER:
		if (copy_to_user((char __user *)arg, data->buffer, data->size))
			ret = -EFAULT;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct file_operations so2_fops = {
	.owner = THIS_MODULE,
/* TODO 2/4: add open, release, read, write functions */
	.open = so2_cdev_open,
	.release = so2_cdev_release,
	.read = so2_cdev_read,
	.write = so2_cdev_write,
	.unlocked_ioctl = so2_cdev_ioctl,
};

static int so2_cdev_init(void)
{
	int err;
	int i;

        /* TODO 1/6: register char device region for MY_MAJOR and NUM_MINORS starting at MY_MINOR */
	err = register_chrdev_region(MKDEV(MY_MAJOR, MY_MINOR),
			NUM_MINORS, MODULE_NAME);
	if (err != 0) {
		pr_info("register_chrdev_region");
		return err;
	}

	for (i = 0; i < NUM_MINORS; i++) {
#ifdef EXTRA
		/* TODO 7/2: extra tasks, for home */
		devs[i].size = 0;
		memset(devs[i].buffer, 0, sizeof(devs[i].buffer));
#else
		/*TODO 4/2: initialize buffer with MESSAGE string */
		memcpy(devs[i].buffer, MESSAGE, sizeof(MESSAGE));
		devs[i].size = sizeof(MESSAGE);
#endif
		/* TODO 7/2: extra tasks for home */
		init_waitqueue_head(&devs[i].wq);
		devs[i].flag = 0;
		/* TODO 3/1: set access variable to 0, use atomic_set */
		atomic_set(&devs[i].access, 0);
		/* TODO 2/2: init and add cdev to kernel core */
		cdev_init(&devs[i].cdev, &so2_fops);
		cdev_add(&devs[i].cdev, MKDEV(MY_MAJOR, i), 1);
	}

	return 0;
}

static void so2_cdev_exit(void)
{
	int i;

	for (i = 0; i < NUM_MINORS; i++) {
		/* TODO 2/1: delete cdev from kernel core */
		cdev_del(&devs[i].cdev);
	}

	/* TODO 1/1: unregister char device region, for MY_MAJOR and NUM_MINORS starting at MY_MINOR */
	unregister_chrdev_region(MKDEV(MY_MAJOR, MY_MINOR), NUM_MINORS);
}

module_init(so2_cdev_init);
module_exit(so2_cdev_exit);
