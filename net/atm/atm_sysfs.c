/* ATM driver model support. */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/atmdev.h>
#include "common.h"
#include "resources.h"

#define to_atm_dev(cldev) container_of(cldev, struct atm_dev, class_dev)

static ssize_t show_type(struct class_device *cdev, char *buf)
{
	struct atm_dev *adev = to_atm_dev(cdev);
	return sprintf(buf, "%s\n", adev->type);
}

static ssize_t show_address(struct class_device *cdev, char *buf)
{
	char *pos = buf;
	struct atm_dev *adev = to_atm_dev(cdev);
	int i;

	for (i = 0; i < (ESI_LEN - 1); i++)
		pos += sprintf(pos, "%02x:", adev->esi[i]);
	pos += sprintf(pos, "%02x\n", adev->esi[i]);

	return pos - buf;
}

static ssize_t show_atmaddress(struct class_device *cdev, char *buf)
{
	unsigned long flags;
	char *pos = buf;
	struct atm_dev *adev = to_atm_dev(cdev);
	struct atm_dev_addr *aaddr;
	int bin[] = { 1, 2, 10, 6, 1 }, *fmt = bin;
	int i, j;

	spin_lock_irqsave(&adev->lock, flags);
	list_for_each_entry(aaddr, &adev->local, entry) {
		for(i = 0, j = 0; i < ATM_ESA_LEN; ++i, ++j) {
			if (j == *fmt) {
				pos += sprintf(pos, ".");
				++fmt;
				j = 0;
			}
			pos += sprintf(pos, "%02x", aaddr->addr.sas_addr.prv[i]);
		}
		pos += sprintf(pos, "\n");
	}
	spin_unlock_irqrestore(&adev->lock, flags);

	return pos - buf;
}

static ssize_t show_carrier(struct class_device *cdev, char *buf)
{
	char *pos = buf;
	struct atm_dev *adev = to_atm_dev(cdev);

	pos += sprintf(pos, "%d\n",
		       adev->signal == ATM_PHY_SIG_LOST ? 0 : 1);

	return pos - buf;
}

static ssize_t show_link_rate(struct class_device *cdev, char *buf)
{
	char *pos = buf;
	struct atm_dev *adev = to_atm_dev(cdev);
	int link_rate;

	/* show the link rate, not the data rate */
	switch (adev->link_rate) {
		case ATM_OC3_PCR:
			link_rate = 155520000;
			break;
		case ATM_OC12_PCR:
			link_rate = 622080000;
			break;
		case ATM_25_PCR:
			link_rate = 25600000;
			break;
		default:
			link_rate = adev->link_rate * 8 * 53;
	}
	pos += sprintf(pos, "%d\n", link_rate);

	return pos - buf;
}

static CLASS_DEVICE_ATTR(address, S_IRUGO, show_address, NULL);
static CLASS_DEVICE_ATTR(atmaddress, S_IRUGO, show_atmaddress, NULL);
static CLASS_DEVICE_ATTR(carrier, S_IRUGO, show_carrier, NULL);
static CLASS_DEVICE_ATTR(type, S_IRUGO, show_type, NULL);
static CLASS_DEVICE_ATTR(link_rate, S_IRUGO, show_link_rate, NULL);

static struct class_device_attribute *atm_attrs[] = {
	&class_device_attr_atmaddress,
	&class_device_attr_address,
	&class_device_attr_carrier,
	&class_device_attr_type,
	&class_device_attr_link_rate,
	NULL
};

static int atm_uevent(struct class_device *cdev, char **envp, int num_envp, char *buf, int size)
{
	struct atm_dev *adev;
	int i = 0, len = 0;

	if (!cdev)
		return -ENODEV;

	adev = to_atm_dev(cdev);
	if (!adev)
		return -ENODEV;

	if (add_uevent_var(envp, num_envp, &i, buf, size, &len,
			   "NAME=%s%d", adev->type, adev->number))
		return -ENOMEM;

	envp[i] = NULL;
	return 0;
}

static void atm_release(struct class_device *cdev)
{
	struct atm_dev *adev = to_atm_dev(cdev);

	kfree(adev);
}

static struct class atm_class = {
	.name		= "atm",
	.release	= atm_release,
	.uevent		= atm_uevent,
};

int atm_register_sysfs(struct atm_dev *adev)
{
	struct class_device *cdev = &adev->class_dev;
	int i, j, err;

	cdev->class = &atm_class;
	class_set_devdata(cdev, adev);

	snprintf(cdev->class_id, BUS_ID_SIZE, "%s%d", adev->type, adev->number);
	err = class_device_register(cdev);
	if (err < 0)
		return err;

	for (i = 0; atm_attrs[i]; i++) {
		err = class_device_create_file(cdev, atm_attrs[i]);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	for (j = 0; j < i; j++)
		class_device_remove_file(cdev, atm_attrs[j]);
	class_device_del(cdev);
	return err;
}

void atm_unregister_sysfs(struct atm_dev *adev)
{
	struct class_device *cdev = &adev->class_dev;

	class_device_del(cdev);
}

int __init atm_sysfs_init(void)
{
	return class_register(&atm_class);
}

void __exit atm_sysfs_exit(void)
{
	class_unregister(&atm_class);
}
