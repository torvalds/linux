// SPDX-License-Identifier: GPL-2.0
/* Bluetooth HCI driver model support. */

#include <linux/module.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

static struct class *bt_class;

static void bt_link_release(struct device *dev)
{
	struct hci_conn *conn = to_hci_conn(dev);
	kfree(conn);
}

static const struct device_type bt_link = {
	.name    = "link",
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
		bt_dev_err(hdev, "failed to register connection device");
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

static void bt_host_release(struct device *dev)
{
	struct hci_dev *hdev = to_hci_dev(dev);

	if (hci_dev_test_flag(hdev, HCI_UNREGISTER))
		hci_release_dev(hdev);
	else
		kfree(hdev);
	module_put(THIS_MODULE);
}

static const struct device_type bt_host = {
	.name    = "host",
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
