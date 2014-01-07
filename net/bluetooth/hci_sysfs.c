/* Bluetooth HCI driver model support. */

#include <linux/module.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

static struct class *bt_class;

static inline char *link_typetostr(int type)
{
	switch (type) {
	case ACL_LINK:
		return "ACL";
	case SCO_LINK:
		return "SCO";
	case ESCO_LINK:
		return "eSCO";
	case LE_LINK:
		return "LE";
	default:
		return "UNKNOWN";
	}
}

static ssize_t show_link_type(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct hci_conn *conn = to_hci_conn(dev);
	return sprintf(buf, "%s\n", link_typetostr(conn->type));
}

static ssize_t show_link_address(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct hci_conn *conn = to_hci_conn(dev);
	return sprintf(buf, "%pMR\n", &conn->dst);
}

#define LINK_ATTR(_name, _mode, _show, _store) \
struct device_attribute link_attr_##_name = __ATTR(_name, _mode, _show, _store)

static LINK_ATTR(type, S_IRUGO, show_link_type, NULL);
static LINK_ATTR(address, S_IRUGO, show_link_address, NULL);

static struct attribute *bt_link_attrs[] = {
	&link_attr_type.attr,
	&link_attr_address.attr,
	NULL
};

ATTRIBUTE_GROUPS(bt_link);

static void bt_link_release(struct device *dev)
{
	struct hci_conn *conn = to_hci_conn(dev);
	kfree(conn);
}

static struct device_type bt_link = {
	.name    = "link",
	.groups  = bt_link_groups,
	.release = bt_link_release,
};

/*
 * The rfcomm tty device will possibly retain even when conn
 * is down, and sysfs doesn't support move zombie device,
 * so we should move the device before conn device is destroyed.
 */
static int __match_tty(struct device *dev, void *data)
{
	return !strncmp(dev_name(dev), "rfcomm", 6);
}

void hci_conn_init_sysfs(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p", conn);

	conn->dev.type = &bt_link;
	conn->dev.class = bt_class;
	conn->dev.parent = &hdev->dev;

	device_initialize(&conn->dev);
}

void hci_conn_add_sysfs(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p", conn);

	dev_set_name(&conn->dev, "%s:%d", hdev->name, conn->handle);

	if (device_add(&conn->dev) < 0) {
		BT_ERR("Failed to register connection device");
		return;
	}

	hci_dev_hold(hdev);
}

void hci_conn_del_sysfs(struct hci_conn *conn)
{
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

	hci_dev_put(hdev);
}

static inline char *host_typetostr(int type)
{
	switch (type) {
	case HCI_BREDR:
		return "BR/EDR";
	case HCI_AMP:
		return "AMP";
	default:
		return "UNKNOWN";
	}
}

static ssize_t show_type(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = to_hci_dev(dev);
	return sprintf(buf, "%s\n", host_typetostr(hdev->dev_type));
}

static ssize_t show_name(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = to_hci_dev(dev);
	char name[HCI_MAX_NAME_LENGTH + 1];
	int i;

	for (i = 0; i < HCI_MAX_NAME_LENGTH; i++)
		name[i] = hdev->dev_name[i];

	name[HCI_MAX_NAME_LENGTH] = '\0';
	return sprintf(buf, "%s\n", name);
}

static ssize_t show_address(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct hci_dev *hdev = to_hci_dev(dev);
	return sprintf(buf, "%pMR\n", &hdev->bdaddr);
}

static DEVICE_ATTR(type, S_IRUGO, show_type, NULL);
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);
static DEVICE_ATTR(address, S_IRUGO, show_address, NULL);

static struct attribute *bt_host_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_name.attr,
	&dev_attr_address.attr,
	NULL
};

ATTRIBUTE_GROUPS(bt_host);

static void bt_host_release(struct device *dev)
{
	struct hci_dev *hdev = to_hci_dev(dev);
	kfree(hdev);
	module_put(THIS_MODULE);
}

static struct device_type bt_host = {
	.name    = "host",
	.groups  = bt_host_groups,
	.release = bt_host_release,
};

void hci_init_sysfs(struct hci_dev *hdev)
{
	struct device *dev = &hdev->dev;

	dev->type = &bt_host;
	dev->class = bt_class;

	__module_get(THIS_MODULE);
	device_initialize(dev);
}

int __init bt_sysfs_init(void)
{
	bt_class = class_create(THIS_MODULE, "bluetooth");

	return PTR_ERR_OR_ZERO(bt_class);
}

void bt_sysfs_cleanup(void)
{
	class_destroy(bt_class);
}
