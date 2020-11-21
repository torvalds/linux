#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include "bex.h"

MODULE_DESCRIPTION("BEX misc driver");
MODULE_AUTHOR("Kernel Hacker");
MODULE_LICENSE("GPL");

#define BUF_SIZE 1024

struct bex_misc_device {
	struct miscdevice misc;
	struct bex_device *dev;
	char buf[BUF_SIZE];
};

static int my_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int my_read(struct file *file, char __user *user_buffer,
		   size_t size, loff_t *offset)
{
	struct bex_misc_device *bmd = (struct bex_misc_device *)file->private_data;
	ssize_t len = min(sizeof(bmd->buf) - (ssize_t)*offset, size);

	if (len <= 0)
		return 0;

	if (copy_to_user(user_buffer, bmd->buf + *offset, len))
		return -EFAULT;

	*offset += len;
	return len;
}

static int my_write(struct file *file, const char __user *user_buffer,
		    size_t size, loff_t *offset)
{
	struct bex_misc_device *bmd = (struct bex_misc_device *)file->private_data;
	ssize_t len = min(sizeof(bmd->buf) - (ssize_t)*offset, size);

	if (len <= 0)
		return 0;

	if (copy_from_user(bmd->buf + *offset, user_buffer, len))
		return -EFAULT;

	*offset += len;
	return len;
}

struct file_operations bex_misc_fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.read = my_read,
	.write = my_write,
	.release = my_release,
};

static int bex_misc_count;

int bex_misc_probe(struct bex_device *dev)
{
	struct bex_misc_device *bmd;
	char buf[32];
	int ret;

	dev_info(&dev->dev, "%s: %s %d\n", __func__, dev->type, dev->version);

	/* TODO 6/4: refuse the probe is version > 1 */
	if (dev->version > 1) {
		dev_info(&dev->dev, "unknown version: %d\n", dev->version);
		return -ENODEV;
	}

	bmd = kzalloc(sizeof(*bmd), GFP_KERNEL);
	if (!bmd)
		return -ENOMEM;

	bmd->misc.minor = MISC_DYNAMIC_MINOR;
	snprintf(buf, sizeof(buf), "bex-misc-%d", bex_misc_count++);
	bmd->misc.name = kstrdup(buf, GFP_KERNEL);
	bmd->misc.parent = &dev->dev;
	bmd->misc.fops = &bex_misc_fops;
	bmd->dev = dev;
	dev_set_drvdata(&dev->dev, bmd);

	/* TODO 6/5: register the misc device */
	ret = misc_register(&bmd->misc);
	if (ret) {
		dev_err(&dev->dev, "failed to register misc device: %d\n", ret);
		return ret;
	}

	return 0;
}

void bex_misc_remove(struct bex_device *dev)
{
	struct bex_misc_device *bmd;

	bmd = (struct bex_misc_device *)dev_get_drvdata(&dev->dev);

	/* TODO 6: deregister the misc device */
	misc_deregister(&bmd->misc);
	kfree(bmd);
}

struct bex_driver bex_misc_driver = {
	.type = "misc",
	.probe = bex_misc_probe,
	.remove = bex_misc_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "bex_misc",
	},
};

static int my_init(void)
{
	int err;

	/* TODO 4/5: register the driver */
	err = bex_register_driver(&bex_misc_driver);
	if(err) {
		pr_err("unable to register driver: %d\n", err);
		return err;
	}

	return 0;
}

static void my_exit(void)
{
	/* TODO 4: unregister the driver */
	bex_unregister_driver(&bex_misc_driver);
}

module_init(my_init);
module_exit(my_exit);
