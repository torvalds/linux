/*
 * This module provides an interface to trigger and test firmware loading.
 *
 * It is designed to be used for basic evaluation of the firmware loading
 * subsystem (for example when validating firmware verification). It lacks
 * any extra dependencies, and will not normally be loaded by the system
 * unless explicitly requested by name.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static DEFINE_MUTEX(test_fw_mutex);
static const struct firmware *test_firmware;

static ssize_t test_fw_misc_read(struct file *f, char __user *buf,
				 size_t size, loff_t *offset)
{
	ssize_t rc = 0;

	mutex_lock(&test_fw_mutex);
	if (test_firmware)
		rc = simple_read_from_buffer(buf, size, offset,
					     test_firmware->data,
					     test_firmware->size);
	mutex_unlock(&test_fw_mutex);
	return rc;
}

static const struct file_operations test_fw_fops = {
	.owner          = THIS_MODULE,
	.read           = test_fw_misc_read,
};

static ssize_t trigger_request_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int rc;
	char *name;

	name = kstrndup(buf, count, GFP_KERNEL);
	if (!name)
		return -ENOSPC;

	pr_info("loading '%s'\n", name);

	mutex_lock(&test_fw_mutex);
	release_firmware(test_firmware);
	test_firmware = NULL;
	rc = request_firmware(&test_firmware, name, dev);
	if (rc) {
		pr_info("load of '%s' failed: %d\n", name, rc);
		goto out;
	}
	pr_info("loaded: %zu\n", test_firmware->size);
	rc = count;

out:
	mutex_unlock(&test_fw_mutex);

	kfree(name);

	return rc;
}
static DEVICE_ATTR_WO(trigger_request);

static DECLARE_COMPLETION(async_fw_done);

static void trigger_async_request_cb(const struct firmware *fw, void *context)
{
	test_firmware = fw;
	complete(&async_fw_done);
}

static ssize_t trigger_async_request_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	int rc;
	char *name;

	name = kstrndup(buf, count, GFP_KERNEL);
	if (!name)
		return -ENOSPC;

	pr_info("loading '%s'\n", name);

	mutex_lock(&test_fw_mutex);
	release_firmware(test_firmware);
	test_firmware = NULL;
	rc = request_firmware_nowait(THIS_MODULE, 1, name, dev, GFP_KERNEL,
				     NULL, trigger_async_request_cb);
	if (rc) {
		pr_info("async load of '%s' failed: %d\n", name, rc);
		kfree(name);
		goto out;
	}
	/* Free 'name' ASAP, to test for race conditions */
	kfree(name);

	wait_for_completion(&async_fw_done);

	if (test_firmware) {
		pr_info("loaded: %zu\n", test_firmware->size);
		rc = count;
	} else {
		pr_err("failed to async load firmware\n");
		rc = -ENODEV;
	}

out:
	mutex_unlock(&test_fw_mutex);

	return rc;
}
static DEVICE_ATTR_WO(trigger_async_request);

static ssize_t trigger_custom_fallback_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	int rc;
	char *name;

	name = kstrndup(buf, count, GFP_KERNEL);
	if (!name)
		return -ENOSPC;

	pr_info("loading '%s' using custom fallback mechanism\n", name);

	mutex_lock(&test_fw_mutex);
	release_firmware(test_firmware);
	test_firmware = NULL;
	rc = request_firmware_nowait(THIS_MODULE, FW_ACTION_NOHOTPLUG, name,
				     dev, GFP_KERNEL, NULL,
				     trigger_async_request_cb);
	if (rc) {
		pr_info("async load of '%s' failed: %d\n", name, rc);
		kfree(name);
		goto out;
	}
	/* Free 'name' ASAP, to test for race conditions */
	kfree(name);

	wait_for_completion(&async_fw_done);

	if (test_firmware) {
		pr_info("loaded: %zu\n", test_firmware->size);
		rc = count;
	} else {
		pr_err("failed to async load firmware\n");
		rc = -ENODEV;
	}

out:
	mutex_unlock(&test_fw_mutex);

	return rc;
}
static DEVICE_ATTR_WO(trigger_custom_fallback);

#define TEST_FW_DEV_ATTR(name)          &dev_attr_##name.attr

static struct attribute *test_dev_attrs[] = {
	TEST_FW_DEV_ATTR(trigger_request),
	TEST_FW_DEV_ATTR(trigger_async_request),
	TEST_FW_DEV_ATTR(trigger_custom_fallback),
	NULL,
};

ATTRIBUTE_GROUPS(test_dev);

static struct miscdevice test_fw_misc_device = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = "test_firmware",
	.fops           = &test_fw_fops,
	.groups 	= test_dev_groups,
};

static int __init test_firmware_init(void)
{
	int rc;

	rc = misc_register(&test_fw_misc_device);
	if (rc) {
		pr_err("could not register misc device: %d\n", rc);
		return rc;
	}

	pr_warn("interface ready\n");

	return 0;
}

module_init(test_firmware_init);

static void __exit test_firmware_exit(void)
{
	release_firmware(test_firmware);
	misc_deregister(&test_fw_misc_device);
	pr_warn("removed interface\n");
}

module_exit(test_firmware_exit);

MODULE_AUTHOR("Kees Cook <keescook@chromium.org>");
MODULE_LICENSE("GPL");
