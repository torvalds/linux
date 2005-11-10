/* Bluetooth HCI driver model support. */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#ifndef CONFIG_BT_HCI_CORE_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

static ssize_t show_name(struct class_device *cdev, char *buf)
{
	struct hci_dev *hdev = class_get_devdata(cdev);
	return sprintf(buf, "%s\n", hdev->name);
}

static ssize_t show_type(struct class_device *cdev, char *buf)
{
	struct hci_dev *hdev = class_get_devdata(cdev);
	return sprintf(buf, "%d\n", hdev->type);
}

static ssize_t show_address(struct class_device *cdev, char *buf)
{
	struct hci_dev *hdev = class_get_devdata(cdev);
	bdaddr_t bdaddr;
	baswap(&bdaddr, &hdev->bdaddr);
	return sprintf(buf, "%s\n", batostr(&bdaddr));
}

static ssize_t show_flags(struct class_device *cdev, char *buf)
{
	struct hci_dev *hdev = class_get_devdata(cdev);
	return sprintf(buf, "0x%lx\n", hdev->flags);
}

static ssize_t show_inquiry_cache(struct class_device *cdev, char *buf)
{
	struct hci_dev *hdev = class_get_devdata(cdev);
	struct inquiry_cache *cache = &hdev->inq_cache;
	struct inquiry_entry *e;
	int n = 0;

	hci_dev_lock_bh(hdev);

	for (e = cache->list; e; e = e->next) {
		struct inquiry_data *data = &e->data;
		bdaddr_t bdaddr;
		baswap(&bdaddr, &data->bdaddr);
		n += sprintf(buf + n, "%s %d %d %d 0x%.2x%.2x%.2x 0x%.4x %d %u\n",
				batostr(&bdaddr),
				data->pscan_rep_mode, data->pscan_period_mode, data->pscan_mode,
				data->dev_class[2], data->dev_class[1], data->dev_class[0],
				__le16_to_cpu(data->clock_offset), data->rssi, e->timestamp);
	}

	hci_dev_unlock_bh(hdev);
	return n;
}

static CLASS_DEVICE_ATTR(name, S_IRUGO, show_name, NULL);
static CLASS_DEVICE_ATTR(type, S_IRUGO, show_type, NULL);
static CLASS_DEVICE_ATTR(address, S_IRUGO, show_address, NULL);
static CLASS_DEVICE_ATTR(flags, S_IRUGO, show_flags, NULL);
static CLASS_DEVICE_ATTR(inquiry_cache, S_IRUGO, show_inquiry_cache, NULL);

static struct class_device_attribute *bt_attrs[] = {
	&class_device_attr_name,
	&class_device_attr_type,
	&class_device_attr_address,
	&class_device_attr_flags,
	&class_device_attr_inquiry_cache,
	NULL
};

#ifdef CONFIG_HOTPLUG
static int bt_hotplug(struct class_device *cdev, char **envp, int num_envp, char *buf, int size)
{
	struct hci_dev *hdev = class_get_devdata(cdev);
	int n, i = 0;

	envp[i++] = buf;
	n = snprintf(buf, size, "INTERFACE=%s", hdev->name) + 1;
	buf += n;
	size -= n;

	if ((size <= 0) || (i >= num_envp))
		return -ENOMEM;

	envp[i] = NULL;
	return 0;
}
#endif

static void bt_release(struct class_device *cdev)
{
	struct hci_dev *hdev = class_get_devdata(cdev);

	kfree(hdev);
}

struct class bt_class = {
	.name		= "bluetooth",
	.release	= bt_release,
#ifdef CONFIG_HOTPLUG
	.hotplug	= bt_hotplug,
#endif
};

EXPORT_SYMBOL_GPL(bt_class);

int hci_register_sysfs(struct hci_dev *hdev)
{
	struct class_device *cdev = &hdev->class_dev;
	unsigned int i;
	int err;

	BT_DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	cdev->class = &bt_class;
	class_set_devdata(cdev, hdev);

	strlcpy(cdev->class_id, hdev->name, BUS_ID_SIZE);
	err = class_device_register(cdev);
	if (err < 0)
		return err;

	for (i = 0; bt_attrs[i]; i++)
		class_device_create_file(cdev, bt_attrs[i]);

	return 0;
}

void hci_unregister_sysfs(struct hci_dev *hdev)
{
	struct class_device * cdev = &hdev->class_dev;

	BT_DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	class_device_del(cdev);
}

int __init bt_sysfs_init(void)
{
	return class_register(&bt_class);
}

void __exit bt_sysfs_cleanup(void)
{
	class_unregister(&bt_class);
}
