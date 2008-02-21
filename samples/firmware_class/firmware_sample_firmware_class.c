/*
 * firmware_sample_firmware_class.c -
 *
 * Copyright (c) 2003 Manuel Estrada Sainz
 *
 * NOTE: This is just a probe of concept, if you think that your driver would
 * be well served by this mechanism please contact me first.
 *
 * DON'T USE THIS CODE AS IS
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/firmware.h>


MODULE_AUTHOR("Manuel Estrada Sainz");
MODULE_DESCRIPTION("Hackish sample for using firmware class directly");
MODULE_LICENSE("GPL");

static inline struct class_device *to_class_dev(struct kobject *obj)
{
	return container_of(obj, struct class_device, kobj);
}

static inline
struct class_device_attribute *to_class_dev_attr(struct attribute *_attr)
{
	return container_of(_attr, struct class_device_attribute, attr);
}

struct firmware_priv {
	char fw_id[FIRMWARE_NAME_MAX];
	s32 loading:2;
	u32 abort:1;
};

static ssize_t firmware_loading_show(struct class_device *class_dev, char *buf)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	return sprintf(buf, "%d\n", fw_priv->loading);
}

static ssize_t firmware_loading_store(struct class_device *class_dev,
				      const char *buf, size_t count)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	int prev_loading = fw_priv->loading;

	fw_priv->loading = simple_strtol(buf, NULL, 10);

	switch (fw_priv->loading) {
	case -1:
		/* abort load an panic */
		break;
	case 1:
		/* setup load */
		break;
	case 0:
		if (prev_loading == 1) {
			/* finish load and get the device back to working
			 * state */
		}
		break;
	}

	return count;
}
static CLASS_DEVICE_ATTR(loading, 0644,
			 firmware_loading_show, firmware_loading_store);

static ssize_t firmware_data_read(struct kobject *kobj,
				  struct bin_attribute *bin_attr,
				  char *buffer, loff_t offset, size_t count)
{
	struct class_device *class_dev = to_class_dev(kobj);
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);

	/* read from the devices firmware memory */

	return count;
}
static ssize_t firmware_data_write(struct kobject *kobj,
				   struct bin_attribute *bin_attr,
				   char *buffer, loff_t offset, size_t count)
{
	struct class_device *class_dev = to_class_dev(kobj);
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);

	/* write to the devices firmware memory */

	return count;
}
static struct bin_attribute firmware_attr_data = {
	.attr = {.name = "data", .mode = 0644},
	.size = 0,
	.read = firmware_data_read,
	.write = firmware_data_write,
};
static int fw_setup_class_device(struct class_device *class_dev,
				 const char *fw_name,
				 struct device *device)
{
	int retval;
	struct firmware_priv *fw_priv;

	fw_priv = kzalloc(sizeof(struct firmware_priv),	GFP_KERNEL);
	if (!fw_priv) {
		retval = -ENOMEM;
		goto out;
	}

	memset(class_dev, 0, sizeof(*class_dev));

	strncpy(fw_priv->fw_id, fw_name, FIRMWARE_NAME_MAX);
	fw_priv->fw_id[FIRMWARE_NAME_MAX-1] = '\0';

	strncpy(class_dev->class_id, device->bus_id, BUS_ID_SIZE);
	class_dev->class_id[BUS_ID_SIZE-1] = '\0';
	class_dev->dev = device;

	class_dev->class = &firmware_class,
	class_set_devdata(class_dev, fw_priv);
	retval = class_device_register(class_dev);
	if (retval) {
		printk(KERN_ERR "%s: class_device_register failed\n",
		       __func__);
		goto error_free_fw_priv;
	}

	retval = sysfs_create_bin_file(&class_dev->kobj, &firmware_attr_data);
	if (retval) {
		printk(KERN_ERR "%s: sysfs_create_bin_file failed\n",
		       __func__);
		goto error_unreg_class_dev;
	}

	retval = class_device_create_file(class_dev,
					  &class_device_attr_loading);
	if (retval) {
		printk(KERN_ERR "%s: class_device_create_file failed\n",
		       __func__);
		goto error_remove_data;
	}

	goto out;

error_remove_data:
	sysfs_remove_bin_file(&class_dev->kobj, &firmware_attr_data);
error_unreg_class_dev:
	class_device_unregister(class_dev);
error_free_fw_priv:
	kfree(fw_priv);
out:
	return retval;
}
static void fw_remove_class_device(struct class_device *class_dev)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);

	class_device_remove_file(class_dev, &class_device_attr_loading);
	sysfs_remove_bin_file(&class_dev->kobj, &firmware_attr_data);
	class_device_unregister(class_dev);
}

static struct class_device *class_dev;

static struct device my_device = {
	.bus_id    = "my_dev0",
};

static int __init firmware_sample_init(void)
{
	int error;

	device_initialize(&my_device);
	class_dev = kmalloc(sizeof(struct class_device), GFP_KERNEL);
	if (!class_dev)
		return -ENOMEM;

	error = fw_setup_class_device(class_dev, "my_firmware_image",
				      &my_device);
	if (error) {
		kfree(class_dev);
		return error;
	}
	return 0;

}
static void __exit firmware_sample_exit(void)
{
	struct firmware_priv *fw_priv = class_get_devdata(class_dev);
	fw_remove_class_device(class_dev);
	kfree(fw_priv);
	kfree(class_dev);
}

module_init(firmware_sample_init);
module_exit(firmware_sample_exit);
