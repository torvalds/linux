/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2014 Intel Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#include <linux/debugfs.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_debugfs.h"

static int features_show(struct seq_file *f, void *ptr)
{
	struct hci_dev *hdev = f->private;
	u8 p;

	hci_dev_lock(hdev);
	for (p = 0; p < HCI_MAX_PAGES && p <= hdev->max_page; p++) {
		seq_printf(f, "%2u: 0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x "
			   "0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x\n", p,
			   hdev->features[p][0], hdev->features[p][1],
			   hdev->features[p][2], hdev->features[p][3],
			   hdev->features[p][4], hdev->features[p][5],
			   hdev->features[p][6], hdev->features[p][7]);
	}
	if (lmp_le_capable(hdev))
		seq_printf(f, "LE: 0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x "
			   "0x%2.2x 0x%2.2x 0x%2.2x 0x%2.2x\n",
			   hdev->le_features[0], hdev->le_features[1],
			   hdev->le_features[2], hdev->le_features[3],
			   hdev->le_features[4], hdev->le_features[5],
			   hdev->le_features[6], hdev->le_features[7]);
	hci_dev_unlock(hdev);

	return 0;
}

static int features_open(struct inode *inode, struct file *file)
{
	return single_open(file, features_show, inode->i_private);
}

static const struct file_operations features_fops = {
	.open		= features_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int device_list_show(struct seq_file *f, void *ptr)
{
	struct hci_dev *hdev = f->private;
	struct hci_conn_params *p;
	struct bdaddr_list *b;

	hci_dev_lock(hdev);
	list_for_each_entry(b, &hdev->whitelist, list)
		seq_printf(f, "%pMR (type %u)\n", &b->bdaddr, b->bdaddr_type);
	list_for_each_entry(p, &hdev->le_conn_params, list) {
		seq_printf(f, "%pMR (type %u) %u\n", &p->addr, p->addr_type,
			   p->auto_connect);
	}
	hci_dev_unlock(hdev);

	return 0;
}

static int device_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, device_list_show, inode->i_private);
}

static const struct file_operations device_list_fops = {
	.open		= device_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int blacklist_show(struct seq_file *f, void *p)
{
	struct hci_dev *hdev = f->private;
	struct bdaddr_list *b;

	hci_dev_lock(hdev);
	list_for_each_entry(b, &hdev->blacklist, list)
		seq_printf(f, "%pMR (type %u)\n", &b->bdaddr, b->bdaddr_type);
	hci_dev_unlock(hdev);

	return 0;
}

static int blacklist_open(struct inode *inode, struct file *file)
{
	return single_open(file, blacklist_show, inode->i_private);
}

static const struct file_operations blacklist_fops = {
	.open		= blacklist_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int uuids_show(struct seq_file *f, void *p)
{
	struct hci_dev *hdev = f->private;
	struct bt_uuid *uuid;

	hci_dev_lock(hdev);
	list_for_each_entry(uuid, &hdev->uuids, list) {
		u8 i, val[16];

		/* The Bluetooth UUID values are stored in big endian,
		 * but with reversed byte order. So convert them into
		 * the right order for the %pUb modifier.
		 */
		for (i = 0; i < 16; i++)
			val[i] = uuid->uuid[15 - i];

		seq_printf(f, "%pUb\n", val);
	}
	hci_dev_unlock(hdev);

       return 0;
}

static int uuids_open(struct inode *inode, struct file *file)
{
	return single_open(file, uuids_show, inode->i_private);
}

static const struct file_operations uuids_fops = {
	.open		= uuids_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int conn_info_min_age_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val == 0 || val > hdev->conn_info_max_age)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->conn_info_min_age = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int conn_info_min_age_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->conn_info_min_age;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(conn_info_min_age_fops, conn_info_min_age_get,
			conn_info_min_age_set, "%llu\n");

static int conn_info_max_age_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val == 0 || val < hdev->conn_info_min_age)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->conn_info_max_age = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int conn_info_max_age_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->conn_info_max_age;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(conn_info_max_age_fops, conn_info_max_age_get,
			conn_info_max_age_set, "%llu\n");

void hci_debugfs_create_common(struct hci_dev *hdev)
{
	debugfs_create_file("features", 0444, hdev->debugfs, hdev,
			    &features_fops);
	debugfs_create_u16("manufacturer", 0444, hdev->debugfs,
			   &hdev->manufacturer);
	debugfs_create_u8("hci_version", 0444, hdev->debugfs, &hdev->hci_ver);
	debugfs_create_u16("hci_revision", 0444, hdev->debugfs, &hdev->hci_rev);
	debugfs_create_file("device_list", 0444, hdev->debugfs, hdev,
			    &device_list_fops);
	debugfs_create_file("blacklist", 0444, hdev->debugfs, hdev,
			    &blacklist_fops);
	debugfs_create_file("uuids", 0444, hdev->debugfs, hdev, &uuids_fops);

	debugfs_create_file("conn_info_min_age", 0644, hdev->debugfs, hdev,
			    &conn_info_min_age_fops);
	debugfs_create_file("conn_info_max_age", 0644, hdev->debugfs, hdev,
			    &conn_info_max_age_fops);
}

void hci_debugfs_create_bredr(struct hci_dev *hdev)
{
}

void hci_debugfs_create_le(struct hci_dev *hdev)
{
}
