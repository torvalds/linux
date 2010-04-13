/* Bluetooth HCI driver model support. */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

static struct class *bt_class;

struct dentry *bt_debugfs = NULL;
EXPORT_SYMBOL_GPL(bt_debugfs);

static struct workqueue_struct *bt_workq;

static inline char *link_typetostr(int type)
{
	switch (type) {
	case ACL_LINK:
		return "ACL";
	case SCO_LINK:
		return "SCO";
	case ESCO_LINK:
		return "eSCO";
	default:
		return "UNKNOWN";
	}
}

static ssize_t show_link_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_conn *conn = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", link_typetostr(conn->type));
}

static ssize_t show_link_address(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_conn *conn = dev_get_drvdata(dev);
	bdaddr_t bdaddr;
	baswap(&bdaddr, &conn->dst);
	return sprintf(buf, "%s\n", batostr(&bdaddr));
}

static ssize_t show_link_features(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_conn *conn = dev_get_drvdata(dev);

	return sprintf(buf, "0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
				conn->features[0], conn->features[1],
				conn->features[2], conn->features[3],
				conn->features[4], conn->features[5],
				conn->features[6], conn->features[7]);
}

#define LINK_ATTR(_name,_mode,_show,_store) \
struct device_attribute link_attr_##_name = __ATTR(_name,_mode,_show,_store)

static LINK_ATTR(type, S_IRUGO, show_link_type, NULL);
static LINK_ATTR(address, S_IRUGO, show_link_address, NULL);
static LINK_ATTR(features, S_IRUGO, show_link_features, NULL);

static struct attribute *bt_link_attrs[] = {
	&link_attr_type.attr,
	&link_attr_address.attr,
	&link_attr_features.attr,
	NULL
};

static struct attribute_group bt_link_group = {
	.attrs = bt_link_attrs,
};

static const struct attribute_group *bt_link_groups[] = {
	&bt_link_group,
	NULL
};

static void bt_link_release(struct device *dev)
{
	void *data = dev_get_drvdata(dev);
	kfree(data);
}

static struct device_type bt_link = {
	.name    = "link",
	.groups  = bt_link_groups,
	.release = bt_link_release,
};

static void add_conn(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn, work_add);
	struct hci_dev *hdev = conn->hdev;

	dev_set_name(&conn->dev, "%s:%d", hdev->name, conn->handle);

	dev_set_drvdata(&conn->dev, conn);

	if (device_add(&conn->dev) < 0) {
		BT_ERR("Failed to register connection device");
		return;
	}

	hci_dev_hold(hdev);
}

/*
 * The rfcomm tty device will possibly retain even when conn
 * is down, and sysfs doesn't support move zombie device,
 * so we should move the device before conn device is destroyed.
 */
static int __match_tty(struct device *dev, void *data)
{
	return !strncmp(dev_name(dev), "rfcomm", 6);
}

static void del_conn(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn, work_del);
	struct hci_dev *hdev = conn->hdev;

	if (!device_is_registered(&conn->dev))
		return;

	while (1) {
		struct device *dev;

		dev = device_find_child(&conn->dev, NULL, __match_tty);
		if (!dev)
			break;
		device_move(dev, NULL, DPM_ORDER_DEV_LAST);
		put_device(dev);
	}

	device_del(&conn->dev);
	put_device(&conn->dev);

	hci_dev_put(hdev);
}

void hci_conn_init_sysfs(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p", conn);

	conn->dev.type = &bt_link;
	conn->dev.class = bt_class;
	conn->dev.parent = &hdev->dev;

	device_initialize(&conn->dev);

	INIT_WORK(&conn->work_add, add_conn);
	INIT_WORK(&conn->work_del, del_conn);
}

void hci_conn_add_sysfs(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	queue_work(bt_workq, &conn->work_add);
}

void hci_conn_del_sysfs(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	queue_work(bt_workq, &conn->work_del);
}

static inline char *host_bustostr(int bus)
{
	switch (bus) {
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

static inline char *host_typetostr(int type)
{
	switch (type) {
	case HCI_BREDR:
		return "BR/EDR";
	case HCI_80211:
		return "802.11";
	default:
		return "UNKNOWN";
	}
}

static ssize_t show_bus(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", host_bustostr(hdev->bus));
}

static ssize_t show_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", host_typetostr(hdev->dev_type));
}

static ssize_t show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	char name[249];
	int i;

	for (i = 0; i < 248; i++)
		name[i] = hdev->dev_name[i];

	name[248] = '\0';
	return sprintf(buf, "%s\n", name);
}

static ssize_t show_class(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	return sprintf(buf, "0x%.2x%.2x%.2x\n",
			hdev->dev_class[2], hdev->dev_class[1], hdev->dev_class[0]);
}

static ssize_t show_address(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);
	bdaddr_t bdaddr;
	baswap(&bdaddr, &hdev->bdaddr);
	return sprintf(buf, "%s\n", batostr(&bdaddr));
}

static ssize_t show_features(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
				hdev->features[0], hdev->features[1],
				hdev->features[2], hdev->features[3],
				hdev->features[4], hdev->features[5],
				hdev->features[6], hdev->features[7]);
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

static DEVICE_ATTR(bus, S_IRUGO, show_bus, NULL);
static DEVICE_ATTR(type, S_IRUGO, show_type, NULL);
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);
static DEVICE_ATTR(class, S_IRUGO, show_class, NULL);
static DEVICE_ATTR(address, S_IRUGO, show_address, NULL);
static DEVICE_ATTR(features, S_IRUGO, show_features, NULL);
static DEVICE_ATTR(manufacturer, S_IRUGO, show_manufacturer, NULL);
static DEVICE_ATTR(hci_version, S_IRUGO, show_hci_version, NULL);
static DEVICE_ATTR(hci_revision, S_IRUGO, show_hci_revision, NULL);

static DEVICE_ATTR(idle_timeout, S_IRUGO | S_IWUSR,
				show_idle_timeout, store_idle_timeout);
static DEVICE_ATTR(sniff_max_interval, S_IRUGO | S_IWUSR,
				show_sniff_max_interval, store_sniff_max_interval);
static DEVICE_ATTR(sniff_min_interval, S_IRUGO | S_IWUSR,
				show_sniff_min_interval, store_sniff_min_interval);

static struct attribute *bt_host_attrs[] = {
	&dev_attr_bus.attr,
	&dev_attr_type.attr,
	&dev_attr_name.attr,
	&dev_attr_class.attr,
	&dev_attr_address.attr,
	&dev_attr_features.attr,
	&dev_attr_manufacturer.attr,
	&dev_attr_hci_version.attr,
	&dev_attr_hci_revision.attr,
	&dev_attr_idle_timeout.attr,
	&dev_attr_sniff_max_interval.attr,
	&dev_attr_sniff_min_interval.attr,
	NULL
};

static struct attribute_group bt_host_group = {
	.attrs = bt_host_attrs,
};

static const struct attribute_group *bt_host_groups[] = {
	&bt_host_group,
	NULL
};

static void bt_host_release(struct device *dev)
{
	void *data = dev_get_drvdata(dev);
	kfree(data);
}

static struct device_type bt_host = {
	.name    = "host",
	.groups  = bt_host_groups,
	.release = bt_host_release,
};

static int inquiry_cache_show(struct seq_file *f, void *p)
{
	struct hci_dev *hdev = f->private;
	struct inquiry_cache *cache = &hdev->inq_cache;
	struct inquiry_entry *e;

	hci_dev_lock_bh(hdev);

	for (e = cache->list; e; e = e->next) {
		struct inquiry_data *data = &e->data;
		bdaddr_t bdaddr;
		baswap(&bdaddr, &data->bdaddr);
		seq_printf(f, "%s %d %d %d 0x%.2x%.2x%.2x 0x%.4x %d %d %u\n",
			   batostr(&bdaddr),
			   data->pscan_rep_mode, data->pscan_period_mode,
			   data->pscan_mode, data->dev_class[2],
			   data->dev_class[1], data->dev_class[0],
			   __le16_to_cpu(data->clock_offset),
			   data->rssi, data->ssp_mode, e->timestamp);
	}

	hci_dev_unlock_bh(hdev);

	return 0;
}

static int inquiry_cache_open(struct inode *inode, struct file *file)
{
	return single_open(file, inquiry_cache_show, inode->i_private);
}

static const struct file_operations inquiry_cache_fops = {
	.open		= inquiry_cache_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int hci_register_sysfs(struct hci_dev *hdev)
{
	struct device *dev = &hdev->dev;
	int err;

	BT_DBG("%p name %s bus %d", hdev, hdev->name, hdev->bus);

	dev->type = &bt_host;
	dev->class = bt_class;
	dev->parent = hdev->parent;

	dev_set_name(dev, "%s", hdev->name);

	dev_set_drvdata(dev, hdev);

	err = device_register(dev);
	if (err < 0)
		return err;

	if (!bt_debugfs)
		return 0;

	hdev->debugfs = debugfs_create_dir(hdev->name, bt_debugfs);
	if (!hdev->debugfs)
		return 0;

	debugfs_create_file("inquiry_cache", 0444, hdev->debugfs,
						hdev, &inquiry_cache_fops);

	return 0;
}

void hci_unregister_sysfs(struct hci_dev *hdev)
{
	BT_DBG("%p name %s bus %d", hdev, hdev->name, hdev->bus);

	debugfs_remove_recursive(hdev->debugfs);

	device_del(&hdev->dev);
}

int __init bt_sysfs_init(void)
{
	bt_workq = create_singlethread_workqueue("bluetooth");
	if (!bt_workq)
		return -ENOMEM;

	bt_debugfs = debugfs_create_dir("bluetooth", NULL);

	bt_class = class_create(THIS_MODULE, "bluetooth");
	if (IS_ERR(bt_class)) {
		destroy_workqueue(bt_workq);
		return PTR_ERR(bt_class);
	}

	return 0;
}

void bt_sysfs_cleanup(void)
{
	class_destroy(bt_class);

	debugfs_remove_recursive(bt_debugfs);

	destroy_workqueue(bt_workq);
}
