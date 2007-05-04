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

static inline char *typetostr(int type)
{
	switch (type) {
	case HCI_VIRTUAL:
		return "VIRTUAL";
	case HCI_USB:
		return "USB";
	case HCI_PCCARD:
		return "PCCARD";
	case HCI_UART:
		return "UART";
	case HCI_RS232:
		return "RS232";
	case HCI_PCI:
		return "PCI";
	case HCI_SDIO:
		return "SDIO";
	default:
		return "UNKNOWN";
	}
}

static ssize_t show_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", typetostr(hdev->type));
}

static ssize_t show_address(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	bdaddr_t bdaddr;
	baswap(&bdaddr, &hdev->bdaddr);
	return sprintf(buf, "%s\n", batostr(&bdaddr));
}

static ssize_t show_manufacturer(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", hdev->manufacturer);
}

static ssize_t show_hci_version(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", hdev->hci_ver);
}

static ssize_t show_hci_revision(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", hdev->hci_rev);
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

static DEVICE_ATTR(type, S_IRUGO, show_type, NULL);
static DEVICE_ATTR(address, S_IRUGO, show_address, NULL);
static DEVICE_ATTR(manufacturer, S_IRUGO, show_manufacturer, NULL);
static DEVICE_ATTR(hci_version, S_IRUGO, show_hci_version, NULL);
static DEVICE_ATTR(hci_revision, S_IRUGO, show_hci_revision, NULL);
static DEVICE_ATTR(inquiry_cache, S_IRUGO, show_inquiry_cache, NULL);

static DEVICE_ATTR(idle_timeout, S_IRUGO | S_IWUSR,
				show_idle_timeout, store_idle_timeout);
static DEVICE_ATTR(sniff_max_interval, S_IRUGO | S_IWUSR,
				show_sniff_max_interval, store_sniff_max_interval);
static DEVICE_ATTR(sniff_min_interval, S_IRUGO | S_IWUSR,
				show_sniff_min_interval, store_sniff_min_interval);

static struct device_attribute *bt_attrs[] = {
	&dev_attr_type,
	&dev_attr_address,
	&dev_attr_manufacturer,
	&dev_attr_hci_version,
	&dev_attr_hci_revision,
	&dev_attr_inquiry_cache,
	&dev_attr_idle_timeout,
	&dev_attr_sniff_max_interval,
	&dev_attr_sniff_min_interval,
	NULL
};

static ssize_t show_conn_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_conn *conn = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", conn->type == ACL_LINK ? "ACL" : "SCO");
}

static ssize_t show_conn_address(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_conn *conn = dev_get_drvdata(dev);
	bdaddr_t bdaddr;
	baswap(&bdaddr, &conn->dst);
	return sprintf(buf, "%s\n", batostr(&bdaddr));
}

#define CONN_ATTR(_name,_mode,_show,_store) \
struct device_attribute conn_attr_##_name = __ATTR(_name,_mode,_show,_store)

static CONN_ATTR(type, S_IRUGO, show_conn_type, NULL);
static CONN_ATTR(address, S_IRUGO, show_conn_address, NULL);

static struct device_attribute *conn_attrs[] = {
	&conn_attr_type,
	&conn_attr_address,
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
	void *data = dev_get_drvdata(dev);
	kfree(data);
}

static void add_conn(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn, work);
	int i;

	if (device_add(&conn->dev) < 0) {
		BT_ERR("Failed to register connection device");
		return;
	}

	for (i = 0; conn_attrs[i]; i++)
		if (device_create_file(&conn->dev, conn_attrs[i]) < 0)
			BT_ERR("Failed to create connection attribute");
}

void hci_conn_add_sysfs(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	bdaddr_t *ba = &conn->dst;

	BT_DBG("conn %p", conn);

	conn->dev.bus = &bt_bus;
	conn->dev.parent = &hdev->dev;

	conn->dev.release = bt_release;

	snprintf(conn->dev.bus_id, BUS_ID_SIZE,
			"%s%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X",
			conn->type == ACL_LINK ? "acl" : "sco",
			ba->b[5], ba->b[4], ba->b[3],
			ba->b[2], ba->b[1], ba->b[0]);

	dev_set_drvdata(&conn->dev, conn);

	device_initialize(&conn->dev);

	INIT_WORK(&conn->work, add_conn);

	schedule_work(&conn->work);
}

static void del_conn(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn, work);
	device_del(&conn->dev);
}

void hci_conn_del_sysfs(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	if (!device_is_registered(&conn->dev))
		return;

	INIT_WORK(&conn->work, del_conn);

	schedule_work(&conn->work);
}

int hci_register_sysfs(struct hci_dev *hdev)
{
	struct device *dev = &hdev->dev;
	unsigned int i;
	int err;

	BT_DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	dev->bus = &bt_bus;
	dev->parent = hdev->parent;

	strlcpy(dev->bus_id, hdev->name, BUS_ID_SIZE);

	dev->release = bt_release;

	dev_set_drvdata(dev, hdev);

	err = device_register(dev);
	if (err < 0)
		return err;

	for (i = 0; bt_attrs[i]; i++)
		if (device_create_file(dev, bt_attrs[i]) < 0)
			BT_ERR("Failed to create device attribute");

	if (sysfs_create_link(&bt_class->subsys.kset.kobj,
				&dev->kobj, kobject_name(&dev->kobj)) < 0)
		BT_ERR("Failed to create class symlink");

	return 0;
}

void hci_unregister_sysfs(struct hci_dev *hdev)
{
	BT_DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	sysfs_remove_link(&bt_class->subsys.kset.kobj,
					kobject_name(&hdev->dev.kobj));

	device_del(&hdev->dev);
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

void bt_sysfs_cleanup(void)
{
	class_destroy(bt_class);

	bus_unregister(&bt_bus);

	platform_device_unregister(bt_platform);
}
