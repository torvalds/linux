/* Bluetooth HCI driver model support. */

#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/platform_device.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#ifndef CONFIG_BT_HCI_CORE_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

static ssize_t show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", hdev->name);
}

static ssize_t show_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", hdev->type);
}

static ssize_t show_address(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	bdaddr_t bdaddr;
	baswap(&bdaddr, &hdev->bdaddr);
	return sprintf(buf, "%s\n", batostr(&bdaddr));
}

static ssize_t show_flags(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "0x%lx\n", hdev->flags);
}

static ssize_t show_inquiry_cache(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
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

static ssize_t show_idle_timeout(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", hdev->idle_timeout);
}

static ssize_t store_idle_timeout(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	char *ptr;
	__u32 val;

	val = simple_strtoul(buf, &ptr, 10);
	if (ptr == buf)
		return -EINVAL;

	if (val != 0 && (val < 500 || val > 3600000))
		return -EINVAL;

	hdev->idle_timeout = val;

	return count;
}

static ssize_t show_sniff_max_interval(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", hdev->sniff_max_interval);
}

static ssize_t store_sniff_max_interval(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	char *ptr;
	__u16 val;

	val = simple_strtoul(buf, &ptr, 10);
	if (ptr == buf)
		return -EINVAL;

	if (val < 0x0002 || val > 0xFFFE || val % 2)
		return -EINVAL;

	if (val < hdev->sniff_min_interval)
		return -EINVAL;

	hdev->sniff_max_interval = val;

	return count;
}

static ssize_t show_sniff_min_interval(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", hdev->sniff_min_interval);
}

static ssize_t store_sniff_min_interval(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	char *ptr;
	__u16 val;

	val = simple_strtoul(buf, &ptr, 10);
	if (ptr == buf)
		return -EINVAL;

	if (val < 0x0002 || val > 0xFFFE || val % 2)
		return -EINVAL;

	if (val > hdev->sniff_max_interval)
		return -EINVAL;

	hdev->sniff_min_interval = val;

	return count;
}

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);
static DEVICE_ATTR(type, S_IRUGO, show_type, NULL);
static DEVICE_ATTR(address, S_IRUGO, show_address, NULL);
static DEVICE_ATTR(flags, S_IRUGO, show_flags, NULL);
static DEVICE_ATTR(inquiry_cache, S_IRUGO, show_inquiry_cache, NULL);

static DEVICE_ATTR(idle_timeout, S_IRUGO | S_IWUSR,
				show_idle_timeout, store_idle_timeout);
static DEVICE_ATTR(sniff_max_interval, S_IRUGO | S_IWUSR,
				show_sniff_max_interval, store_sniff_max_interval);
static DEVICE_ATTR(sniff_min_interval, S_IRUGO | S_IWUSR,
				show_sniff_min_interval, store_sniff_min_interval);

static struct device_attribute *bt_attrs[] = {
	&dev_attr_name,
	&dev_attr_type,
	&dev_attr_address,
	&dev_attr_flags,
	&dev_attr_inquiry_cache,
	&dev_attr_idle_timeout,
	&dev_attr_sniff_max_interval,
	&dev_attr_sniff_min_interval,
	NULL
};

struct class *bt_class = NULL;
EXPORT_SYMBOL_GPL(bt_class);

static struct bus_type bt_bus = {
	.name	= "bluetooth",
};

static struct platform_device *bt_platform;

static void bt_release(struct device *dev)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	kfree(hdev);
}

int hci_register_sysfs(struct hci_dev *hdev)
{
	struct device *dev = &hdev->dev;
	unsigned int i;
	int err;

	BT_DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	dev->class = bt_class;

	if (hdev->parent)
		dev->parent = hdev->parent;
	else
		dev->parent = &bt_platform->dev;

	strlcpy(dev->bus_id, hdev->name, BUS_ID_SIZE);

	dev->release = bt_release;

	dev_set_drvdata(dev, hdev);

	err = device_register(dev);
	if (err < 0)
		return err;

	for (i = 0; bt_attrs[i]; i++)
		device_create_file(dev, bt_attrs[i]);

	return 0;
}

void hci_unregister_sysfs(struct hci_dev *hdev)
{
	struct device *dev = &hdev->dev;

	BT_DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	device_del(dev);
}

int __init bt_sysfs_init(void)
{
	int err;

	bt_platform = platform_device_register_simple("bluetooth", -1, NULL, 0);
	if (IS_ERR(bt_platform))
		return PTR_ERR(bt_platform);

	err = bus_register(&bt_bus);
	if (err < 0) {
		platform_device_unregister(bt_platform);
		return err;
	}

	bt_class = class_create(THIS_MODULE, "bluetooth");
	if (IS_ERR(bt_class)) {
		bus_unregister(&bt_bus);
		platform_device_unregister(bt_platform);
		return PTR_ERR(bt_class);
	}

	return 0;
}

void __exit bt_sysfs_cleanup(void)
{
	class_destroy(bt_class);

	bus_unregister(&bt_bus);

	platform_device_unregister(bt_platform);
}
