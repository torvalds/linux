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

static int inquiry_cache_show(struct seq_file *f, void *p)
{
	struct hci_dev *hdev = f->private;
	struct discovery_state *cache = &hdev->discovery;
	struct inquiry_entry *e;

	hci_dev_lock(hdev);

	list_for_each_entry(e, &cache->all, all) {
		struct inquiry_data *data = &e->data;
		seq_printf(f, "%pMR %d %d %d 0x%.2x%.2x%.2x 0x%.4x %d %d %u\n",
			   &data->bdaddr,
			   data->pscan_rep_mode, data->pscan_period_mode,
			   data->pscan_mode, data->dev_class[2],
			   data->dev_class[1], data->dev_class[0],
			   __le16_to_cpu(data->clock_offset),
			   data->rssi, data->ssp_mode, e->timestamp);
	}

	hci_dev_unlock(hdev);

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

static int link_keys_show(struct seq_file *f, void *ptr)
{
	struct hci_dev *hdev = f->private;
	struct link_key *key;

	rcu_read_lock();
	list_for_each_entry_rcu(key, &hdev->link_keys, list)
		seq_printf(f, "%pMR %u %*phN %u\n", &key->bdaddr, key->type,
			   HCI_LINK_KEY_SIZE, key->val, key->pin_len);
	rcu_read_unlock();

	return 0;
}

static int link_keys_open(struct inode *inode, struct file *file)
{
	return single_open(file, link_keys_show, inode->i_private);
}

static const struct file_operations link_keys_fops = {
	.open		= link_keys_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int dev_class_show(struct seq_file *f, void *ptr)
{
	struct hci_dev *hdev = f->private;

	hci_dev_lock(hdev);
	seq_printf(f, "0x%.2x%.2x%.2x\n", hdev->dev_class[2],
		   hdev->dev_class[1], hdev->dev_class[0]);
	hci_dev_unlock(hdev);

	return 0;
}

static int dev_class_open(struct inode *inode, struct file *file)
{
	return single_open(file, dev_class_show, inode->i_private);
}

static const struct file_operations dev_class_fops = {
	.open		= dev_class_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int voice_setting_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->voice_setting;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(voice_setting_fops, voice_setting_get,
			NULL, "0x%4.4llx\n");

static int auto_accept_delay_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	hdev->auto_accept_delay = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int auto_accept_delay_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->auto_accept_delay;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(auto_accept_delay_fops, auto_accept_delay_get,
			auto_accept_delay_set, "%llu\n");

static ssize_t sc_only_mode_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[3];

	buf[0] = test_bit(HCI_SC_ONLY, &hdev->dev_flags) ? 'Y': 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static const struct file_operations sc_only_mode_fops = {
	.open		= simple_open,
	.read		= sc_only_mode_read,
	.llseek		= default_llseek,
};

static ssize_t force_sc_support_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[3];

	buf[0] = test_bit(HCI_FORCE_SC, &hdev->dbg_flags) ? 'Y': 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t force_sc_support_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[32];
	size_t buf_size = min(count, (sizeof(buf)-1));
	bool enable;

	if (test_bit(HCI_UP, &hdev->flags))
		return -EBUSY;

	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';
	if (strtobool(buf, &enable))
		return -EINVAL;

	if (enable == test_bit(HCI_FORCE_SC, &hdev->dbg_flags))
		return -EALREADY;

	change_bit(HCI_FORCE_SC, &hdev->dbg_flags);

	return count;
}

static const struct file_operations force_sc_support_fops = {
	.open		= simple_open,
	.read		= force_sc_support_read,
	.write		= force_sc_support_write,
	.llseek		= default_llseek,
};

static int idle_timeout_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val != 0 && (val < 500 || val > 3600000))
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->idle_timeout = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int idle_timeout_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->idle_timeout;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(idle_timeout_fops, idle_timeout_get,
			idle_timeout_set, "%llu\n");

static int sniff_min_interval_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val == 0 || val % 2 || val > hdev->sniff_max_interval)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->sniff_min_interval = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int sniff_min_interval_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->sniff_min_interval;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(sniff_min_interval_fops, sniff_min_interval_get,
			sniff_min_interval_set, "%llu\n");

static int sniff_max_interval_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val == 0 || val % 2 || val < hdev->sniff_min_interval)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->sniff_max_interval = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int sniff_max_interval_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->sniff_max_interval;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(sniff_max_interval_fops, sniff_max_interval_get,
			sniff_max_interval_set, "%llu\n");

void hci_debugfs_create_bredr(struct hci_dev *hdev)
{
	debugfs_create_file("inquiry_cache", 0444, hdev->debugfs, hdev,
			    &inquiry_cache_fops);
	debugfs_create_file("link_keys", 0400, hdev->debugfs, hdev,
			    &link_keys_fops);
	debugfs_create_file("dev_class", 0444, hdev->debugfs, hdev,
			    &dev_class_fops);
	debugfs_create_file("voice_setting", 0444, hdev->debugfs, hdev,
			    &voice_setting_fops);

	if (lmp_ssp_capable(hdev)) {
		debugfs_create_file("auto_accept_delay", 0644, hdev->debugfs,
				    hdev, &auto_accept_delay_fops);
		debugfs_create_file("sc_only_mode", 0444, hdev->debugfs,
				    hdev, &sc_only_mode_fops);

		debugfs_create_file("force_sc_support", 0644, hdev->debugfs,
				    hdev, &force_sc_support_fops);
	}

	if (lmp_sniff_capable(hdev)) {
		debugfs_create_file("idle_timeout", 0644, hdev->debugfs,
				    hdev, &idle_timeout_fops);
		debugfs_create_file("sniff_min_interval", 0644, hdev->debugfs,
				    hdev, &sniff_min_interval_fops);
		debugfs_create_file("sniff_max_interval", 0644, hdev->debugfs,
				    hdev, &sniff_max_interval_fops);
	}
}

static int identity_show(struct seq_file *f, void *p)
{
	struct hci_dev *hdev = f->private;
	bdaddr_t addr;
	u8 addr_type;

	hci_dev_lock(hdev);

	hci_copy_identity_address(hdev, &addr, &addr_type);

	seq_printf(f, "%pMR (type %u) %*phN %pMR\n", &addr, addr_type,
		   16, hdev->irk, &hdev->rpa);

	hci_dev_unlock(hdev);

	return 0;
}

static int identity_open(struct inode *inode, struct file *file)
{
	return single_open(file, identity_show, inode->i_private);
}

static const struct file_operations identity_fops = {
	.open		= identity_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rpa_timeout_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	/* Require the RPA timeout to be at least 30 seconds and at most
	 * 24 hours.
	 */
	if (val < 30 || val > (60 * 60 * 24))
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->rpa_timeout = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int rpa_timeout_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->rpa_timeout;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(rpa_timeout_fops, rpa_timeout_get,
			rpa_timeout_set, "%llu\n");

static int random_address_show(struct seq_file *f, void *p)
{
	struct hci_dev *hdev = f->private;

	hci_dev_lock(hdev);
	seq_printf(f, "%pMR\n", &hdev->random_addr);
	hci_dev_unlock(hdev);

	return 0;
}

static int random_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, random_address_show, inode->i_private);
}

static const struct file_operations random_address_fops = {
	.open		= random_address_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int static_address_show(struct seq_file *f, void *p)
{
	struct hci_dev *hdev = f->private;

	hci_dev_lock(hdev);
	seq_printf(f, "%pMR\n", &hdev->static_addr);
	hci_dev_unlock(hdev);

	return 0;
}

static int static_address_open(struct inode *inode, struct file *file)
{
	return single_open(file, static_address_show, inode->i_private);
}

static const struct file_operations static_address_fops = {
	.open		= static_address_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t force_static_address_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[3];

	buf[0] = test_bit(HCI_FORCE_STATIC_ADDR, &hdev->dbg_flags) ? 'Y': 'N';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t force_static_address_write(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct hci_dev *hdev = file->private_data;
	char buf[32];
	size_t buf_size = min(count, (sizeof(buf)-1));
	bool enable;

	if (test_bit(HCI_UP, &hdev->flags))
		return -EBUSY;

	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';
	if (strtobool(buf, &enable))
		return -EINVAL;

	if (enable == test_bit(HCI_FORCE_STATIC_ADDR, &hdev->dbg_flags))
		return -EALREADY;

	change_bit(HCI_FORCE_STATIC_ADDR, &hdev->dbg_flags);

	return count;
}

static const struct file_operations force_static_address_fops = {
	.open		= simple_open,
	.read		= force_static_address_read,
	.write		= force_static_address_write,
	.llseek		= default_llseek,
};

static int white_list_show(struct seq_file *f, void *ptr)
{
	struct hci_dev *hdev = f->private;
	struct bdaddr_list *b;

	hci_dev_lock(hdev);
	list_for_each_entry(b, &hdev->le_white_list, list)
		seq_printf(f, "%pMR (type %u)\n", &b->bdaddr, b->bdaddr_type);
	hci_dev_unlock(hdev);

	return 0;
}

static int white_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, white_list_show, inode->i_private);
}

static const struct file_operations white_list_fops = {
	.open		= white_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int identity_resolving_keys_show(struct seq_file *f, void *ptr)
{
	struct hci_dev *hdev = f->private;
	struct smp_irk *irk;

	rcu_read_lock();
	list_for_each_entry_rcu(irk, &hdev->identity_resolving_keys, list) {
		seq_printf(f, "%pMR (type %u) %*phN %pMR\n",
			   &irk->bdaddr, irk->addr_type,
			   16, irk->val, &irk->rpa);
	}
	rcu_read_unlock();

	return 0;
}

static int identity_resolving_keys_open(struct inode *inode, struct file *file)
{
	return single_open(file, identity_resolving_keys_show,
			   inode->i_private);
}

static const struct file_operations identity_resolving_keys_fops = {
	.open		= identity_resolving_keys_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int long_term_keys_show(struct seq_file *f, void *ptr)
{
	struct hci_dev *hdev = f->private;
	struct smp_ltk *ltk;

	rcu_read_lock();
	list_for_each_entry_rcu(ltk, &hdev->long_term_keys, list)
		seq_printf(f, "%pMR (type %u) %u 0x%02x %u %.4x %.16llx %*phN\n",
			   &ltk->bdaddr, ltk->bdaddr_type, ltk->authenticated,
			   ltk->type, ltk->enc_size, __le16_to_cpu(ltk->ediv),
			   __le64_to_cpu(ltk->rand), 16, ltk->val);
	rcu_read_unlock();

	return 0;
}

static int long_term_keys_open(struct inode *inode, struct file *file)
{
	return single_open(file, long_term_keys_show, inode->i_private);
}

static const struct file_operations long_term_keys_fops = {
	.open		= long_term_keys_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int conn_min_interval_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val < 0x0006 || val > 0x0c80 || val > hdev->le_conn_max_interval)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->le_conn_min_interval = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int conn_min_interval_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->le_conn_min_interval;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(conn_min_interval_fops, conn_min_interval_get,
			conn_min_interval_set, "%llu\n");

static int conn_max_interval_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val < 0x0006 || val > 0x0c80 || val < hdev->le_conn_min_interval)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->le_conn_max_interval = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int conn_max_interval_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->le_conn_max_interval;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(conn_max_interval_fops, conn_max_interval_get,
			conn_max_interval_set, "%llu\n");

static int conn_latency_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val > 0x01f3)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->le_conn_latency = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int conn_latency_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->le_conn_latency;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(conn_latency_fops, conn_latency_get,
			conn_latency_set, "%llu\n");

static int supervision_timeout_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val < 0x000a || val > 0x0c80)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->le_supv_timeout = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int supervision_timeout_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->le_supv_timeout;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(supervision_timeout_fops, supervision_timeout_get,
			supervision_timeout_set, "%llu\n");

static int adv_channel_map_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val < 0x01 || val > 0x07)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->le_adv_channel_map = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int adv_channel_map_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->le_adv_channel_map;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(adv_channel_map_fops, adv_channel_map_get,
			adv_channel_map_set, "%llu\n");

static int adv_min_interval_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val < 0x0020 || val > 0x4000 || val > hdev->le_adv_max_interval)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->le_adv_min_interval = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int adv_min_interval_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->le_adv_min_interval;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(adv_min_interval_fops, adv_min_interval_get,
			adv_min_interval_set, "%llu\n");

static int adv_max_interval_set(void *data, u64 val)
{
	struct hci_dev *hdev = data;

	if (val < 0x0020 || val > 0x4000 || val < hdev->le_adv_min_interval)
		return -EINVAL;

	hci_dev_lock(hdev);
	hdev->le_adv_max_interval = val;
	hci_dev_unlock(hdev);

	return 0;
}

static int adv_max_interval_get(void *data, u64 *val)
{
	struct hci_dev *hdev = data;

	hci_dev_lock(hdev);
	*val = hdev->le_adv_max_interval;
	hci_dev_unlock(hdev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(adv_max_interval_fops, adv_max_interval_get,
			adv_max_interval_set, "%llu\n");

void hci_debugfs_create_le(struct hci_dev *hdev)
{
	debugfs_create_file("identity", 0400, hdev->debugfs, hdev,
			    &identity_fops);
	debugfs_create_file("rpa_timeout", 0644, hdev->debugfs, hdev,
			    &rpa_timeout_fops);
	debugfs_create_file("random_address", 0444, hdev->debugfs, hdev,
			    &random_address_fops);
	debugfs_create_file("static_address", 0444, hdev->debugfs, hdev,
			    &static_address_fops);

	/* For controllers with a public address, provide a debug
	 * option to force the usage of the configured static
	 * address. By default the public address is used.
	 */
	if (bacmp(&hdev->bdaddr, BDADDR_ANY))
		debugfs_create_file("force_static_address", 0644,
				    hdev->debugfs, hdev,
				    &force_static_address_fops);

	debugfs_create_u8("white_list_size", 0444, hdev->debugfs,
			  &hdev->le_white_list_size);
	debugfs_create_file("white_list", 0444, hdev->debugfs, hdev,
			    &white_list_fops);
	debugfs_create_file("identity_resolving_keys", 0400, hdev->debugfs,
			    hdev, &identity_resolving_keys_fops);
	debugfs_create_file("long_term_keys", 0400, hdev->debugfs, hdev,
			    &long_term_keys_fops);
	debugfs_create_file("conn_min_interval", 0644, hdev->debugfs, hdev,
			    &conn_min_interval_fops);
	debugfs_create_file("conn_max_interval", 0644, hdev->debugfs, hdev,
			    &conn_max_interval_fops);
	debugfs_create_file("conn_latency", 0644, hdev->debugfs, hdev,
			    &conn_latency_fops);
	debugfs_create_file("supervision_timeout", 0644, hdev->debugfs, hdev,
			    &supervision_timeout_fops);
	debugfs_create_file("adv_channel_map", 0644, hdev->debugfs, hdev,
			    &adv_channel_map_fops);
	debugfs_create_file("adv_min_interval", 0644, hdev->debugfs, hdev,
			    &adv_min_interval_fops);
	debugfs_create_file("adv_max_interval", 0644, hdev->debugfs, hdev,
			    &adv_max_interval_fops);
	debugfs_create_u16("discov_interleaved_timeout", 0644, hdev->debugfs,
			   &hdev->discov_interleaved_timeout);
}

void hci_debugfs_create_conn(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	char name[6];

	if (IS_ERR_OR_NULL(hdev->debugfs))
		return;

	snprintf(name, sizeof(name), "%u", conn->handle);
	conn->debugfs = debugfs_create_dir(name, hdev->debugfs);
}
