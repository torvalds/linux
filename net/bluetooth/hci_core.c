/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright (C) 2011 ProFUSION Embedded Systems

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

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

/* Bluetooth HCI core. */

#include <linux/export.h>
#include <linux/rfkill.h>
#include <linux/debugfs.h>
#include <linux/crypto.h>
#include <linux/kcov.h>
#include <linux/property.h>
#include <linux/suspend.h>
#include <linux/wait.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "hci_debugfs.h"
#include "smp.h"
#include "leds.h"
#include "msft.h"
#include "aosp.h"
#include "hci_codec.h"

static void hci_rx_work(struct work_struct *work);
static void hci_cmd_work(struct work_struct *work);
static void hci_tx_work(struct work_struct *work);

/* HCI device list */
LIST_HEAD(hci_dev_list);
DEFINE_RWLOCK(hci_dev_list_lock);

/* HCI callback list */
LIST_HEAD(hci_cb_list);
DEFINE_MUTEX(hci_cb_list_lock);

/* HCI ID Numbering */
static DEFINE_IDA(hci_index_ida);

static int hci_scan_req(struct hci_request *req, unsigned long opt)
{
	__u8 scan = opt;

	BT_DBG("%s %x", req->hdev->name, scan);

	/* Inquiry and Page scans */
	hci_req_add(req, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
	return 0;
}

static int hci_auth_req(struct hci_request *req, unsigned long opt)
{
	__u8 auth = opt;

	BT_DBG("%s %x", req->hdev->name, auth);

	/* Authentication */
	hci_req_add(req, HCI_OP_WRITE_AUTH_ENABLE, 1, &auth);
	return 0;
}

static int hci_encrypt_req(struct hci_request *req, unsigned long opt)
{
	__u8 encrypt = opt;

	BT_DBG("%s %x", req->hdev->name, encrypt);

	/* Encryption */
	hci_req_add(req, HCI_OP_WRITE_ENCRYPT_MODE, 1, &encrypt);
	return 0;
}

static int hci_linkpol_req(struct hci_request *req, unsigned long opt)
{
	__le16 policy = cpu_to_le16(opt);

	BT_DBG("%s %x", req->hdev->name, policy);

	/* Default link policy */
	hci_req_add(req, HCI_OP_WRITE_DEF_LINK_POLICY, 2, &policy);
	return 0;
}

/* Get HCI device by index.
 * Device is held on return. */
struct hci_dev *hci_dev_get(int index)
{
	struct hci_dev *hdev = NULL, *d;

	BT_DBG("%d", index);

	if (index < 0)
		return NULL;

	read_lock(&hci_dev_list_lock);
	list_for_each_entry(d, &hci_dev_list, list) {
		if (d->id == index) {
			hdev = hci_dev_hold(d);
			break;
		}
	}
	read_unlock(&hci_dev_list_lock);
	return hdev;
}

/* ---- Inquiry support ---- */

bool hci_discovery_active(struct hci_dev *hdev)
{
	struct discovery_state *discov = &hdev->discovery;

	switch (discov->state) {
	case DISCOVERY_FINDING:
	case DISCOVERY_RESOLVING:
		return true;

	default:
		return false;
	}
}

void hci_discovery_set_state(struct hci_dev *hdev, int state)
{
	int old_state = hdev->discovery.state;

	BT_DBG("%s state %u -> %u", hdev->name, hdev->discovery.state, state);

	if (old_state == state)
		return;

	hdev->discovery.state = state;

	switch (state) {
	case DISCOVERY_STOPPED:
		hci_update_passive_scan(hdev);

		if (old_state != DISCOVERY_STARTING)
			mgmt_discovering(hdev, 0);
		break;
	case DISCOVERY_STARTING:
		break;
	case DISCOVERY_FINDING:
		mgmt_discovering(hdev, 1);
		break;
	case DISCOVERY_RESOLVING:
		break;
	case DISCOVERY_STOPPING:
		break;
	}
}

void hci_inquiry_cache_flush(struct hci_dev *hdev)
{
	struct discovery_state *cache = &hdev->discovery;
	struct inquiry_entry *p, *n;

	list_for_each_entry_safe(p, n, &cache->all, all) {
		list_del(&p->all);
		kfree(p);
	}

	INIT_LIST_HEAD(&cache->unknown);
	INIT_LIST_HEAD(&cache->resolve);
}

struct inquiry_entry *hci_inquiry_cache_lookup(struct hci_dev *hdev,
					       bdaddr_t *bdaddr)
{
	struct discovery_state *cache = &hdev->discovery;
	struct inquiry_entry *e;

	BT_DBG("cache %p, %pMR", cache, bdaddr);

	list_for_each_entry(e, &cache->all, all) {
		if (!bacmp(&e->data.bdaddr, bdaddr))
			return e;
	}

	return NULL;
}

struct inquiry_entry *hci_inquiry_cache_lookup_unknown(struct hci_dev *hdev,
						       bdaddr_t *bdaddr)
{
	struct discovery_state *cache = &hdev->discovery;
	struct inquiry_entry *e;

	BT_DBG("cache %p, %pMR", cache, bdaddr);

	list_for_each_entry(e, &cache->unknown, list) {
		if (!bacmp(&e->data.bdaddr, bdaddr))
			return e;
	}

	return NULL;
}

struct inquiry_entry *hci_inquiry_cache_lookup_resolve(struct hci_dev *hdev,
						       bdaddr_t *bdaddr,
						       int state)
{
	struct discovery_state *cache = &hdev->discovery;
	struct inquiry_entry *e;

	BT_DBG("cache %p bdaddr %pMR state %d", cache, bdaddr, state);

	list_for_each_entry(e, &cache->resolve, list) {
		if (!bacmp(bdaddr, BDADDR_ANY) && e->name_state == state)
			return e;
		if (!bacmp(&e->data.bdaddr, bdaddr))
			return e;
	}

	return NULL;
}

void hci_inquiry_cache_update_resolve(struct hci_dev *hdev,
				      struct inquiry_entry *ie)
{
	struct discovery_state *cache = &hdev->discovery;
	struct list_head *pos = &cache->resolve;
	struct inquiry_entry *p;

	list_del(&ie->list);

	list_for_each_entry(p, &cache->resolve, list) {
		if (p->name_state != NAME_PENDING &&
		    abs(p->data.rssi) >= abs(ie->data.rssi))
			break;
		pos = &p->list;
	}

	list_add(&ie->list, pos);
}

u32 hci_inquiry_cache_update(struct hci_dev *hdev, struct inquiry_data *data,
			     bool name_known)
{
	struct discovery_state *cache = &hdev->discovery;
	struct inquiry_entry *ie;
	u32 flags = 0;

	BT_DBG("cache %p, %pMR", cache, &data->bdaddr);

	hci_remove_remote_oob_data(hdev, &data->bdaddr, BDADDR_BREDR);

	if (!data->ssp_mode)
		flags |= MGMT_DEV_FOUND_LEGACY_PAIRING;

	ie = hci_inquiry_cache_lookup(hdev, &data->bdaddr);
	if (ie) {
		if (!ie->data.ssp_mode)
			flags |= MGMT_DEV_FOUND_LEGACY_PAIRING;

		if (ie->name_state == NAME_NEEDED &&
		    data->rssi != ie->data.rssi) {
			ie->data.rssi = data->rssi;
			hci_inquiry_cache_update_resolve(hdev, ie);
		}

		goto update;
	}

	/* Entry not in the cache. Add new one. */
	ie = kzalloc(sizeof(*ie), GFP_KERNEL);
	if (!ie) {
		flags |= MGMT_DEV_FOUND_CONFIRM_NAME;
		goto done;
	}

	list_add(&ie->all, &cache->all);

	if (name_known) {
		ie->name_state = NAME_KNOWN;
	} else {
		ie->name_state = NAME_NOT_KNOWN;
		list_add(&ie->list, &cache->unknown);
	}

update:
	if (name_known && ie->name_state != NAME_KNOWN &&
	    ie->name_state != NAME_PENDING) {
		ie->name_state = NAME_KNOWN;
		list_del(&ie->list);
	}

	memcpy(&ie->data, data, sizeof(*data));
	ie->timestamp = jiffies;
	cache->timestamp = jiffies;

	if (ie->name_state == NAME_NOT_KNOWN)
		flags |= MGMT_DEV_FOUND_CONFIRM_NAME;

done:
	return flags;
}

static int inquiry_cache_dump(struct hci_dev *hdev, int num, __u8 *buf)
{
	struct discovery_state *cache = &hdev->discovery;
	struct inquiry_info *info = (struct inquiry_info *) buf;
	struct inquiry_entry *e;
	int copied = 0;

	list_for_each_entry(e, &cache->all, all) {
		struct inquiry_data *data = &e->data;

		if (copied >= num)
			break;

		bacpy(&info->bdaddr, &data->bdaddr);
		info->pscan_rep_mode	= data->pscan_rep_mode;
		info->pscan_period_mode	= data->pscan_period_mode;
		info->pscan_mode	= data->pscan_mode;
		memcpy(info->dev_class, data->dev_class, 3);
		info->clock_offset	= data->clock_offset;

		info++;
		copied++;
	}

	BT_DBG("cache %p, copied %d", cache, copied);
	return copied;
}

static int hci_inq_req(struct hci_request *req, unsigned long opt)
{
	struct hci_inquiry_req *ir = (struct hci_inquiry_req *) opt;
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_inquiry cp;

	BT_DBG("%s", hdev->name);

	if (test_bit(HCI_INQUIRY, &hdev->flags))
		return 0;

	/* Start Inquiry */
	memcpy(&cp.lap, &ir->lap, 3);
	cp.length  = ir->length;
	cp.num_rsp = ir->num_rsp;
	hci_req_add(req, HCI_OP_INQUIRY, sizeof(cp), &cp);

	return 0;
}

int hci_inquiry(void __user *arg)
{
	__u8 __user *ptr = arg;
	struct hci_inquiry_req ir;
	struct hci_dev *hdev;
	int err = 0, do_inquiry = 0, max_rsp;
	long timeo;
	__u8 *buf;

	if (copy_from_user(&ir, ptr, sizeof(ir)))
		return -EFAULT;

	hdev = hci_dev_get(ir.dev_id);
	if (!hdev)
		return -ENODEV;

	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
		err = -EBUSY;
		goto done;
	}

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED)) {
		err = -EOPNOTSUPP;
		goto done;
	}

	if (hdev->dev_type != HCI_PRIMARY) {
		err = -EOPNOTSUPP;
		goto done;
	}

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED)) {
		err = -EOPNOTSUPP;
		goto done;
	}

	/* Restrict maximum inquiry length to 60 seconds */
	if (ir.length > 60) {
		err = -EINVAL;
		goto done;
	}

	hci_dev_lock(hdev);
	if (inquiry_cache_age(hdev) > INQUIRY_CACHE_AGE_MAX ||
	    inquiry_cache_empty(hdev) || ir.flags & IREQ_CACHE_FLUSH) {
		hci_inquiry_cache_flush(hdev);
		do_inquiry = 1;
	}
	hci_dev_unlock(hdev);

	timeo = ir.length * msecs_to_jiffies(2000);

	if (do_inquiry) {
		err = hci_req_sync(hdev, hci_inq_req, (unsigned long) &ir,
				   timeo, NULL);
		if (err < 0)
			goto done;

		/* Wait until Inquiry procedure finishes (HCI_INQUIRY flag is
		 * cleared). If it is interrupted by a signal, return -EINTR.
		 */
		if (wait_on_bit(&hdev->flags, HCI_INQUIRY,
				TASK_INTERRUPTIBLE)) {
			err = -EINTR;
			goto done;
		}
	}

	/* for unlimited number of responses we will use buffer with
	 * 255 entries
	 */
	max_rsp = (ir.num_rsp == 0) ? 255 : ir.num_rsp;

	/* cache_dump can't sleep. Therefore we allocate temp buffer and then
	 * copy it to the user space.
	 */
	buf = kmalloc_array(max_rsp, sizeof(struct inquiry_info), GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto done;
	}

	hci_dev_lock(hdev);
	ir.num_rsp = inquiry_cache_dump(hdev, max_rsp, buf);
	hci_dev_unlock(hdev);

	BT_DBG("num_rsp %d", ir.num_rsp);

	if (!copy_to_user(ptr, &ir, sizeof(ir))) {
		ptr += sizeof(ir);
		if (copy_to_user(ptr, buf, sizeof(struct inquiry_info) *
				 ir.num_rsp))
			err = -EFAULT;
	} else
		err = -EFAULT;

	kfree(buf);

done:
	hci_dev_put(hdev);
	return err;
}

static int hci_dev_do_open(struct hci_dev *hdev)
{
	int ret = 0;

	BT_DBG("%s %p", hdev->name, hdev);

	hci_req_sync_lock(hdev);

	ret = hci_dev_open_sync(hdev);

	hci_req_sync_unlock(hdev);
	return ret;
}

/* ---- HCI ioctl helpers ---- */

int hci_dev_open(__u16 dev)
{
	struct hci_dev *hdev;
	int err;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	/* Devices that are marked as unconfigured can only be powered
	 * up as user channel. Trying to bring them up as normal devices
	 * will result into a failure. Only user channel operation is
	 * possible.
	 *
	 * When this function is called for a user channel, the flag
	 * HCI_USER_CHANNEL will be set first before attempting to
	 * open the device.
	 */
	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED) &&
	    !hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
		err = -EOPNOTSUPP;
		goto done;
	}

	/* We need to ensure that no other power on/off work is pending
	 * before proceeding to call hci_dev_do_open. This is
	 * particularly important if the setup procedure has not yet
	 * completed.
	 */
	if (hci_dev_test_and_clear_flag(hdev, HCI_AUTO_OFF))
		cancel_delayed_work(&hdev->power_off);

	/* After this call it is guaranteed that the setup procedure
	 * has finished. This means that error conditions like RFKILL
	 * or no valid public or static random address apply.
	 */
	flush_workqueue(hdev->req_workqueue);

	/* For controllers not using the management interface and that
	 * are brought up using legacy ioctl, set the HCI_BONDABLE bit
	 * so that pairing works for them. Once the management interface
	 * is in use this bit will be cleared again and userspace has
	 * to explicitly enable it.
	 */
	if (!hci_dev_test_flag(hdev, HCI_USER_CHANNEL) &&
	    !hci_dev_test_flag(hdev, HCI_MGMT))
		hci_dev_set_flag(hdev, HCI_BONDABLE);

	err = hci_dev_do_open(hdev);

done:
	hci_dev_put(hdev);
	return err;
}

int hci_dev_do_close(struct hci_dev *hdev)
{
	int err;

	BT_DBG("%s %p", hdev->name, hdev);

	hci_req_sync_lock(hdev);

	err = hci_dev_close_sync(hdev);

	hci_req_sync_unlock(hdev);

	return err;
}

int hci_dev_close(__u16 dev)
{
	struct hci_dev *hdev;
	int err;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
		err = -EBUSY;
		goto done;
	}

	cancel_work_sync(&hdev->power_on);
	if (hci_dev_test_and_clear_flag(hdev, HCI_AUTO_OFF))
		cancel_delayed_work(&hdev->power_off);

	err = hci_dev_do_close(hdev);

done:
	hci_dev_put(hdev);
	return err;
}

static int hci_dev_do_reset(struct hci_dev *hdev)
{
	int ret;

	BT_DBG("%s %p", hdev->name, hdev);

	hci_req_sync_lock(hdev);

	/* Drop queues */
	skb_queue_purge(&hdev->rx_q);
	skb_queue_purge(&hdev->cmd_q);

	/* Cancel these to avoid queueing non-chained pending work */
	hci_dev_set_flag(hdev, HCI_CMD_DRAIN_WORKQUEUE);
	/* Wait for
	 *
	 *    if (!hci_dev_test_flag(hdev, HCI_CMD_DRAIN_WORKQUEUE))
	 *        queue_delayed_work(&hdev->{cmd,ncmd}_timer)
	 *
	 * inside RCU section to see the flag or complete scheduling.
	 */
	synchronize_rcu();
	/* Explicitly cancel works in case scheduled after setting the flag. */
	cancel_delayed_work(&hdev->cmd_timer);
	cancel_delayed_work(&hdev->ncmd_timer);

	/* Avoid potential lockdep warnings from the *_flush() calls by
	 * ensuring the workqueue is empty up front.
	 */
	drain_workqueue(hdev->workqueue);

	hci_dev_lock(hdev);
	hci_inquiry_cache_flush(hdev);
	hci_conn_hash_flush(hdev);
	hci_dev_unlock(hdev);

	if (hdev->flush)
		hdev->flush(hdev);

	hci_dev_clear_flag(hdev, HCI_CMD_DRAIN_WORKQUEUE);

	atomic_set(&hdev->cmd_cnt, 1);
	hdev->acl_cnt = 0;
	hdev->sco_cnt = 0;
	hdev->le_cnt = 0;
	hdev->iso_cnt = 0;

	ret = hci_reset_sync(hdev);

	hci_req_sync_unlock(hdev);
	return ret;
}

int hci_dev_reset(__u16 dev)
{
	struct hci_dev *hdev;
	int err;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	if (!test_bit(HCI_UP, &hdev->flags)) {
		err = -ENETDOWN;
		goto done;
	}

	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
		err = -EBUSY;
		goto done;
	}

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED)) {
		err = -EOPNOTSUPP;
		goto done;
	}

	err = hci_dev_do_reset(hdev);

done:
	hci_dev_put(hdev);
	return err;
}

int hci_dev_reset_stat(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
		ret = -EBUSY;
		goto done;
	}

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED)) {
		ret = -EOPNOTSUPP;
		goto done;
	}

	memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));

done:
	hci_dev_put(hdev);
	return ret;
}

static void hci_update_passive_scan_state(struct hci_dev *hdev, u8 scan)
{
	bool conn_changed, discov_changed;

	BT_DBG("%s scan 0x%02x", hdev->name, scan);

	if ((scan & SCAN_PAGE))
		conn_changed = !hci_dev_test_and_set_flag(hdev,
							  HCI_CONNECTABLE);
	else
		conn_changed = hci_dev_test_and_clear_flag(hdev,
							   HCI_CONNECTABLE);

	if ((scan & SCAN_INQUIRY)) {
		discov_changed = !hci_dev_test_and_set_flag(hdev,
							    HCI_DISCOVERABLE);
	} else {
		hci_dev_clear_flag(hdev, HCI_LIMITED_DISCOVERABLE);
		discov_changed = hci_dev_test_and_clear_flag(hdev,
							     HCI_DISCOVERABLE);
	}

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		return;

	if (conn_changed || discov_changed) {
		/* In case this was disabled through mgmt */
		hci_dev_set_flag(hdev, HCI_BREDR_ENABLED);

		if (hci_dev_test_flag(hdev, HCI_LE_ENABLED))
			hci_update_adv_data(hdev, hdev->cur_adv_instance);

		mgmt_new_settings(hdev);
	}
}

int hci_dev_cmd(unsigned int cmd, void __user *arg)
{
	struct hci_dev *hdev;
	struct hci_dev_req dr;
	int err = 0;

	if (copy_from_user(&dr, arg, sizeof(dr)))
		return -EFAULT;

	hdev = hci_dev_get(dr.dev_id);
	if (!hdev)
		return -ENODEV;

	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
		err = -EBUSY;
		goto done;
	}

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED)) {
		err = -EOPNOTSUPP;
		goto done;
	}

	if (hdev->dev_type != HCI_PRIMARY) {
		err = -EOPNOTSUPP;
		goto done;
	}

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED)) {
		err = -EOPNOTSUPP;
		goto done;
	}

	switch (cmd) {
	case HCISETAUTH:
		err = hci_req_sync(hdev, hci_auth_req, dr.dev_opt,
				   HCI_INIT_TIMEOUT, NULL);
		break;

	case HCISETENCRYPT:
		if (!lmp_encrypt_capable(hdev)) {
			err = -EOPNOTSUPP;
			break;
		}

		if (!test_bit(HCI_AUTH, &hdev->flags)) {
			/* Auth must be enabled first */
			err = hci_req_sync(hdev, hci_auth_req, dr.dev_opt,
					   HCI_INIT_TIMEOUT, NULL);
			if (err)
				break;
		}

		err = hci_req_sync(hdev, hci_encrypt_req, dr.dev_opt,
				   HCI_INIT_TIMEOUT, NULL);
		break;

	case HCISETSCAN:
		err = hci_req_sync(hdev, hci_scan_req, dr.dev_opt,
				   HCI_INIT_TIMEOUT, NULL);

		/* Ensure that the connectable and discoverable states
		 * get correctly modified as this was a non-mgmt change.
		 */
		if (!err)
			hci_update_passive_scan_state(hdev, dr.dev_opt);
		break;

	case HCISETLINKPOL:
		err = hci_req_sync(hdev, hci_linkpol_req, dr.dev_opt,
				   HCI_INIT_TIMEOUT, NULL);
		break;

	case HCISETLINKMODE:
		hdev->link_mode = ((__u16) dr.dev_opt) &
					(HCI_LM_MASTER | HCI_LM_ACCEPT);
		break;

	case HCISETPTYPE:
		if (hdev->pkt_type == (__u16) dr.dev_opt)
			break;

		hdev->pkt_type = (__u16) dr.dev_opt;
		mgmt_phy_configuration_changed(hdev, NULL);
		break;

	case HCISETACLMTU:
		hdev->acl_mtu  = *((__u16 *) &dr.dev_opt + 1);
		hdev->acl_pkts = *((__u16 *) &dr.dev_opt + 0);
		break;

	case HCISETSCOMTU:
		hdev->sco_mtu  = *((__u16 *) &dr.dev_opt + 1);
		hdev->sco_pkts = *((__u16 *) &dr.dev_opt + 0);
		break;

	default:
		err = -EINVAL;
		break;
	}

done:
	hci_dev_put(hdev);
	return err;
}

int hci_get_dev_list(void __user *arg)
{
	struct hci_dev *hdev;
	struct hci_dev_list_req *dl;
	struct hci_dev_req *dr;
	int n = 0, size, err;
	__u16 dev_num;

	if (get_user(dev_num, (__u16 __user *) arg))
		return -EFAULT;

	if (!dev_num || dev_num > (PAGE_SIZE * 2) / sizeof(*dr))
		return -EINVAL;

	size = sizeof(*dl) + dev_num * sizeof(*dr);

	dl = kzalloc(size, GFP_KERNEL);
	if (!dl)
		return -ENOMEM;

	dr = dl->dev_req;

	read_lock(&hci_dev_list_lock);
	list_for_each_entry(hdev, &hci_dev_list, list) {
		unsigned long flags = hdev->flags;

		/* When the auto-off is configured it means the transport
		 * is running, but in that case still indicate that the
		 * device is actually down.
		 */
		if (hci_dev_test_flag(hdev, HCI_AUTO_OFF))
			flags &= ~BIT(HCI_UP);

		(dr + n)->dev_id  = hdev->id;
		(dr + n)->dev_opt = flags;

		if (++n >= dev_num)
			break;
	}
	read_unlock(&hci_dev_list_lock);

	dl->dev_num = n;
	size = sizeof(*dl) + n * sizeof(*dr);

	err = copy_to_user(arg, dl, size);
	kfree(dl);

	return err ? -EFAULT : 0;
}

int hci_get_dev_info(void __user *arg)
{
	struct hci_dev *hdev;
	struct hci_dev_info di;
	unsigned long flags;
	int err = 0;

	if (copy_from_user(&di, arg, sizeof(di)))
		return -EFAULT;

	hdev = hci_dev_get(di.dev_id);
	if (!hdev)
		return -ENODEV;

	/* When the auto-off is configured it means the transport
	 * is running, but in that case still indicate that the
	 * device is actually down.
	 */
	if (hci_dev_test_flag(hdev, HCI_AUTO_OFF))
		flags = hdev->flags & ~BIT(HCI_UP);
	else
		flags = hdev->flags;

	strcpy(di.name, hdev->name);
	di.bdaddr   = hdev->bdaddr;
	di.type     = (hdev->bus & 0x0f) | ((hdev->dev_type & 0x03) << 4);
	di.flags    = flags;
	di.pkt_type = hdev->pkt_type;
	if (lmp_bredr_capable(hdev)) {
		di.acl_mtu  = hdev->acl_mtu;
		di.acl_pkts = hdev->acl_pkts;
		di.sco_mtu  = hdev->sco_mtu;
		di.sco_pkts = hdev->sco_pkts;
	} else {
		di.acl_mtu  = hdev->le_mtu;
		di.acl_pkts = hdev->le_pkts;
		di.sco_mtu  = 0;
		di.sco_pkts = 0;
	}
	di.link_policy = hdev->link_policy;
	di.link_mode   = hdev->link_mode;

	memcpy(&di.stat, &hdev->stat, sizeof(di.stat));
	memcpy(&di.features, &hdev->features, sizeof(di.features));

	if (copy_to_user(arg, &di, sizeof(di)))
		err = -EFAULT;

	hci_dev_put(hdev);

	return err;
}

/* ---- Interface to HCI drivers ---- */

static int hci_rfkill_set_block(void *data, bool blocked)
{
	struct hci_dev *hdev = data;

	BT_DBG("%p name %s blocked %d", hdev, hdev->name, blocked);

	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL))
		return -EBUSY;

	if (blocked) {
		hci_dev_set_flag(hdev, HCI_RFKILLED);
		if (!hci_dev_test_flag(hdev, HCI_SETUP) &&
		    !hci_dev_test_flag(hdev, HCI_CONFIG))
			hci_dev_do_close(hdev);
	} else {
		hci_dev_clear_flag(hdev, HCI_RFKILLED);
	}

	return 0;
}

static const struct rfkill_ops hci_rfkill_ops = {
	.set_block = hci_rfkill_set_block,
};

static void hci_power_on(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, power_on);
	int err;

	BT_DBG("%s", hdev->name);

	if (test_bit(HCI_UP, &hdev->flags) &&
	    hci_dev_test_flag(hdev, HCI_MGMT) &&
	    hci_dev_test_and_clear_flag(hdev, HCI_AUTO_OFF)) {
		cancel_delayed_work(&hdev->power_off);
		err = hci_powered_update_sync(hdev);
		mgmt_power_on(hdev, err);
		return;
	}

	err = hci_dev_do_open(hdev);
	if (err < 0) {
		hci_dev_lock(hdev);
		mgmt_set_powered_failed(hdev, err);
		hci_dev_unlock(hdev);
		return;
	}

	/* During the HCI setup phase, a few error conditions are
	 * ignored and they need to be checked now. If they are still
	 * valid, it is important to turn the device back off.
	 */
	if (hci_dev_test_flag(hdev, HCI_RFKILLED) ||
	    hci_dev_test_flag(hdev, HCI_UNCONFIGURED) ||
	    (hdev->dev_type == HCI_PRIMARY &&
	     !bacmp(&hdev->bdaddr, BDADDR_ANY) &&
	     !bacmp(&hdev->static_addr, BDADDR_ANY))) {
		hci_dev_clear_flag(hdev, HCI_AUTO_OFF);
		hci_dev_do_close(hdev);
	} else if (hci_dev_test_flag(hdev, HCI_AUTO_OFF)) {
		queue_delayed_work(hdev->req_workqueue, &hdev->power_off,
				   HCI_AUTO_OFF_TIMEOUT);
	}

	if (hci_dev_test_and_clear_flag(hdev, HCI_SETUP)) {
		/* For unconfigured devices, set the HCI_RAW flag
		 * so that userspace can easily identify them.
		 */
		if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED))
			set_bit(HCI_RAW, &hdev->flags);

		/* For fully configured devices, this will send
		 * the Index Added event. For unconfigured devices,
		 * it will send Unconfigued Index Added event.
		 *
		 * Devices with HCI_QUIRK_RAW_DEVICE are ignored
		 * and no event will be send.
		 */
		mgmt_index_added(hdev);
	} else if (hci_dev_test_and_clear_flag(hdev, HCI_CONFIG)) {
		/* When the controller is now configured, then it
		 * is important to clear the HCI_RAW flag.
		 */
		if (!hci_dev_test_flag(hdev, HCI_UNCONFIGURED))
			clear_bit(HCI_RAW, &hdev->flags);

		/* Powering on the controller with HCI_CONFIG set only
		 * happens with the transition from unconfigured to
		 * configured. This will send the Index Added event.
		 */
		mgmt_index_added(hdev);
	}
}

static void hci_power_off(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    power_off.work);

	BT_DBG("%s", hdev->name);

	hci_dev_do_close(hdev);
}

static void hci_error_reset(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, error_reset);

	BT_DBG("%s", hdev->name);

	if (hdev->hw_error)
		hdev->hw_error(hdev, hdev->hw_error_code);
	else
		bt_dev_err(hdev, "hardware error 0x%2.2x", hdev->hw_error_code);

	if (hci_dev_do_close(hdev))
		return;

	hci_dev_do_open(hdev);
}

void hci_uuids_clear(struct hci_dev *hdev)
{
	struct bt_uuid *uuid, *tmp;

	list_for_each_entry_safe(uuid, tmp, &hdev->uuids, list) {
		list_del(&uuid->list);
		kfree(uuid);
	}
}

void hci_link_keys_clear(struct hci_dev *hdev)
{
	struct link_key *key;

	list_for_each_entry(key, &hdev->link_keys, list) {
		list_del_rcu(&key->list);
		kfree_rcu(key, rcu);
	}
}

void hci_smp_ltks_clear(struct hci_dev *hdev)
{
	struct smp_ltk *k;

	list_for_each_entry(k, &hdev->long_term_keys, list) {
		list_del_rcu(&k->list);
		kfree_rcu(k, rcu);
	}
}

void hci_smp_irks_clear(struct hci_dev *hdev)
{
	struct smp_irk *k;

	list_for_each_entry(k, &hdev->identity_resolving_keys, list) {
		list_del_rcu(&k->list);
		kfree_rcu(k, rcu);
	}
}

void hci_blocked_keys_clear(struct hci_dev *hdev)
{
	struct blocked_key *b;

	list_for_each_entry(b, &hdev->blocked_keys, list) {
		list_del_rcu(&b->list);
		kfree_rcu(b, rcu);
	}
}

bool hci_is_blocked_key(struct hci_dev *hdev, u8 type, u8 val[16])
{
	bool blocked = false;
	struct blocked_key *b;

	rcu_read_lock();
	list_for_each_entry_rcu(b, &hdev->blocked_keys, list) {
		if (b->type == type && !memcmp(b->val, val, sizeof(b->val))) {
			blocked = true;
			break;
		}
	}

	rcu_read_unlock();
	return blocked;
}

struct link_key *hci_find_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct link_key *k;

	rcu_read_lock();
	list_for_each_entry_rcu(k, &hdev->link_keys, list) {
		if (bacmp(bdaddr, &k->bdaddr) == 0) {
			rcu_read_unlock();

			if (hci_is_blocked_key(hdev,
					       HCI_BLOCKED_KEY_TYPE_LINKKEY,
					       k->val)) {
				bt_dev_warn_ratelimited(hdev,
							"Link key blocked for %pMR",
							&k->bdaddr);
				return NULL;
			}

			return k;
		}
	}
	rcu_read_unlock();

	return NULL;
}

static bool hci_persistent_key(struct hci_dev *hdev, struct hci_conn *conn,
			       u8 key_type, u8 old_key_type)
{
	/* Legacy key */
	if (key_type < 0x03)
		return true;

	/* Debug keys are insecure so don't store them persistently */
	if (key_type == HCI_LK_DEBUG_COMBINATION)
		return false;

	/* Changed combination key and there's no previous one */
	if (key_type == HCI_LK_CHANGED_COMBINATION && old_key_type == 0xff)
		return false;

	/* Security mode 3 case */
	if (!conn)
		return true;

	/* BR/EDR key derived using SC from an LE link */
	if (conn->type == LE_LINK)
		return true;

	/* Neither local nor remote side had no-bonding as requirement */
	if (conn->auth_type > 0x01 && conn->remote_auth > 0x01)
		return true;

	/* Local side had dedicated bonding as requirement */
	if (conn->auth_type == 0x02 || conn->auth_type == 0x03)
		return true;

	/* Remote side had dedicated bonding as requirement */
	if (conn->remote_auth == 0x02 || conn->remote_auth == 0x03)
		return true;

	/* If none of the above criteria match, then don't store the key
	 * persistently */
	return false;
}

static u8 ltk_role(u8 type)
{
	if (type == SMP_LTK)
		return HCI_ROLE_MASTER;

	return HCI_ROLE_SLAVE;
}

struct smp_ltk *hci_find_ltk(struct hci_dev *hdev, bdaddr_t *bdaddr,
			     u8 addr_type, u8 role)
{
	struct smp_ltk *k;

	rcu_read_lock();
	list_for_each_entry_rcu(k, &hdev->long_term_keys, list) {
		if (addr_type != k->bdaddr_type || bacmp(bdaddr, &k->bdaddr))
			continue;

		if (smp_ltk_is_sc(k) || ltk_role(k->type) == role) {
			rcu_read_unlock();

			if (hci_is_blocked_key(hdev, HCI_BLOCKED_KEY_TYPE_LTK,
					       k->val)) {
				bt_dev_warn_ratelimited(hdev,
							"LTK blocked for %pMR",
							&k->bdaddr);
				return NULL;
			}

			return k;
		}
	}
	rcu_read_unlock();

	return NULL;
}

struct smp_irk *hci_find_irk_by_rpa(struct hci_dev *hdev, bdaddr_t *rpa)
{
	struct smp_irk *irk_to_return = NULL;
	struct smp_irk *irk;

	rcu_read_lock();
	list_for_each_entry_rcu(irk, &hdev->identity_resolving_keys, list) {
		if (!bacmp(&irk->rpa, rpa)) {
			irk_to_return = irk;
			goto done;
		}
	}

	list_for_each_entry_rcu(irk, &hdev->identity_resolving_keys, list) {
		if (smp_irk_matches(hdev, irk->val, rpa)) {
			bacpy(&irk->rpa, rpa);
			irk_to_return = irk;
			goto done;
		}
	}

done:
	if (irk_to_return && hci_is_blocked_key(hdev, HCI_BLOCKED_KEY_TYPE_IRK,
						irk_to_return->val)) {
		bt_dev_warn_ratelimited(hdev, "Identity key blocked for %pMR",
					&irk_to_return->bdaddr);
		irk_to_return = NULL;
	}

	rcu_read_unlock();

	return irk_to_return;
}

struct smp_irk *hci_find_irk_by_addr(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 addr_type)
{
	struct smp_irk *irk_to_return = NULL;
	struct smp_irk *irk;

	/* Identity Address must be public or static random */
	if (addr_type == ADDR_LE_DEV_RANDOM && (bdaddr->b[5] & 0xc0) != 0xc0)
		return NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(irk, &hdev->identity_resolving_keys, list) {
		if (addr_type == irk->addr_type &&
		    bacmp(bdaddr, &irk->bdaddr) == 0) {
			irk_to_return = irk;
			goto done;
		}
	}

done:

	if (irk_to_return && hci_is_blocked_key(hdev, HCI_BLOCKED_KEY_TYPE_IRK,
						irk_to_return->val)) {
		bt_dev_warn_ratelimited(hdev, "Identity key blocked for %pMR",
					&irk_to_return->bdaddr);
		irk_to_return = NULL;
	}

	rcu_read_unlock();

	return irk_to_return;
}

struct link_key *hci_add_link_key(struct hci_dev *hdev, struct hci_conn *conn,
				  bdaddr_t *bdaddr, u8 *val, u8 type,
				  u8 pin_len, bool *persistent)
{
	struct link_key *key, *old_key;
	u8 old_key_type;

	old_key = hci_find_link_key(hdev, bdaddr);
	if (old_key) {
		old_key_type = old_key->type;
		key = old_key;
	} else {
		old_key_type = conn ? conn->key_type : 0xff;
		key = kzalloc(sizeof(*key), GFP_KERNEL);
		if (!key)
			return NULL;
		list_add_rcu(&key->list, &hdev->link_keys);
	}

	BT_DBG("%s key for %pMR type %u", hdev->name, bdaddr, type);

	/* Some buggy controller combinations generate a changed
	 * combination key for legacy pairing even when there's no
	 * previous key */
	if (type == HCI_LK_CHANGED_COMBINATION &&
	    (!conn || conn->remote_auth == 0xff) && old_key_type == 0xff) {
		type = HCI_LK_COMBINATION;
		if (conn)
			conn->key_type = type;
	}

	bacpy(&key->bdaddr, bdaddr);
	memcpy(key->val, val, HCI_LINK_KEY_SIZE);
	key->pin_len = pin_len;

	if (type == HCI_LK_CHANGED_COMBINATION)
		key->type = old_key_type;
	else
		key->type = type;

	if (persistent)
		*persistent = hci_persistent_key(hdev, conn, type,
						 old_key_type);

	return key;
}

struct smp_ltk *hci_add_ltk(struct hci_dev *hdev, bdaddr_t *bdaddr,
			    u8 addr_type, u8 type, u8 authenticated,
			    u8 tk[16], u8 enc_size, __le16 ediv, __le64 rand)
{
	struct smp_ltk *key, *old_key;
	u8 role = ltk_role(type);

	old_key = hci_find_ltk(hdev, bdaddr, addr_type, role);
	if (old_key)
		key = old_key;
	else {
		key = kzalloc(sizeof(*key), GFP_KERNEL);
		if (!key)
			return NULL;
		list_add_rcu(&key->list, &hdev->long_term_keys);
	}

	bacpy(&key->bdaddr, bdaddr);
	key->bdaddr_type = addr_type;
	memcpy(key->val, tk, sizeof(key->val));
	key->authenticated = authenticated;
	key->ediv = ediv;
	key->rand = rand;
	key->enc_size = enc_size;
	key->type = type;

	return key;
}

struct smp_irk *hci_add_irk(struct hci_dev *hdev, bdaddr_t *bdaddr,
			    u8 addr_type, u8 val[16], bdaddr_t *rpa)
{
	struct smp_irk *irk;

	irk = hci_find_irk_by_addr(hdev, bdaddr, addr_type);
	if (!irk) {
		irk = kzalloc(sizeof(*irk), GFP_KERNEL);
		if (!irk)
			return NULL;

		bacpy(&irk->bdaddr, bdaddr);
		irk->addr_type = addr_type;

		list_add_rcu(&irk->list, &hdev->identity_resolving_keys);
	}

	memcpy(irk->val, val, 16);
	bacpy(&irk->rpa, rpa);

	return irk;
}

int hci_remove_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct link_key *key;

	key = hci_find_link_key(hdev, bdaddr);
	if (!key)
		return -ENOENT;

	BT_DBG("%s removing %pMR", hdev->name, bdaddr);

	list_del_rcu(&key->list);
	kfree_rcu(key, rcu);

	return 0;
}

int hci_remove_ltk(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 bdaddr_type)
{
	struct smp_ltk *k;
	int removed = 0;

	list_for_each_entry_rcu(k, &hdev->long_term_keys, list) {
		if (bacmp(bdaddr, &k->bdaddr) || k->bdaddr_type != bdaddr_type)
			continue;

		BT_DBG("%s removing %pMR", hdev->name, bdaddr);

		list_del_rcu(&k->list);
		kfree_rcu(k, rcu);
		removed++;
	}

	return removed ? 0 : -ENOENT;
}

void hci_remove_irk(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 addr_type)
{
	struct smp_irk *k;

	list_for_each_entry_rcu(k, &hdev->identity_resolving_keys, list) {
		if (bacmp(bdaddr, &k->bdaddr) || k->addr_type != addr_type)
			continue;

		BT_DBG("%s removing %pMR", hdev->name, bdaddr);

		list_del_rcu(&k->list);
		kfree_rcu(k, rcu);
	}
}

bool hci_bdaddr_is_paired(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 type)
{
	struct smp_ltk *k;
	struct smp_irk *irk;
	u8 addr_type;

	if (type == BDADDR_BREDR) {
		if (hci_find_link_key(hdev, bdaddr))
			return true;
		return false;
	}

	/* Convert to HCI addr type which struct smp_ltk uses */
	if (type == BDADDR_LE_PUBLIC)
		addr_type = ADDR_LE_DEV_PUBLIC;
	else
		addr_type = ADDR_LE_DEV_RANDOM;

	irk = hci_get_irk(hdev, bdaddr, addr_type);
	if (irk) {
		bdaddr = &irk->bdaddr;
		addr_type = irk->addr_type;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(k, &hdev->long_term_keys, list) {
		if (k->bdaddr_type == addr_type && !bacmp(bdaddr, &k->bdaddr)) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();

	return false;
}

/* HCI command timer function */
static void hci_cmd_timeout(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    cmd_timer.work);

	if (hdev->sent_cmd) {
		struct hci_command_hdr *sent = (void *) hdev->sent_cmd->data;
		u16 opcode = __le16_to_cpu(sent->opcode);

		bt_dev_err(hdev, "command 0x%4.4x tx timeout", opcode);
	} else {
		bt_dev_err(hdev, "command tx timeout");
	}

	if (hdev->cmd_timeout)
		hdev->cmd_timeout(hdev);

	atomic_set(&hdev->cmd_cnt, 1);
	queue_work(hdev->workqueue, &hdev->cmd_work);
}

/* HCI ncmd timer function */
static void hci_ncmd_timeout(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev,
					    ncmd_timer.work);

	bt_dev_err(hdev, "Controller not accepting commands anymore: ncmd = 0");

	/* During HCI_INIT phase no events can be injected if the ncmd timer
	 * triggers since the procedure has its own timeout handling.
	 */
	if (test_bit(HCI_INIT, &hdev->flags))
		return;

	/* This is an irrecoverable state, inject hardware error event */
	hci_reset_dev(hdev);
}

struct oob_data *hci_find_remote_oob_data(struct hci_dev *hdev,
					  bdaddr_t *bdaddr, u8 bdaddr_type)
{
	struct oob_data *data;

	list_for_each_entry(data, &hdev->remote_oob_data, list) {
		if (bacmp(bdaddr, &data->bdaddr) != 0)
			continue;
		if (data->bdaddr_type != bdaddr_type)
			continue;
		return data;
	}

	return NULL;
}

int hci_remove_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr,
			       u8 bdaddr_type)
{
	struct oob_data *data;

	data = hci_find_remote_oob_data(hdev, bdaddr, bdaddr_type);
	if (!data)
		return -ENOENT;

	BT_DBG("%s removing %pMR (%u)", hdev->name, bdaddr, bdaddr_type);

	list_del(&data->list);
	kfree(data);

	return 0;
}

void hci_remote_oob_data_clear(struct hci_dev *hdev)
{
	struct oob_data *data, *n;

	list_for_each_entry_safe(data, n, &hdev->remote_oob_data, list) {
		list_del(&data->list);
		kfree(data);
	}
}

int hci_add_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr,
			    u8 bdaddr_type, u8 *hash192, u8 *rand192,
			    u8 *hash256, u8 *rand256)
{
	struct oob_data *data;

	data = hci_find_remote_oob_data(hdev, bdaddr, bdaddr_type);
	if (!data) {
		data = kmalloc(sizeof(*data), GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		bacpy(&data->bdaddr, bdaddr);
		data->bdaddr_type = bdaddr_type;
		list_add(&data->list, &hdev->remote_oob_data);
	}

	if (hash192 && rand192) {
		memcpy(data->hash192, hash192, sizeof(data->hash192));
		memcpy(data->rand192, rand192, sizeof(data->rand192));
		if (hash256 && rand256)
			data->present = 0x03;
	} else {
		memset(data->hash192, 0, sizeof(data->hash192));
		memset(data->rand192, 0, sizeof(data->rand192));
		if (hash256 && rand256)
			data->present = 0x02;
		else
			data->present = 0x00;
	}

	if (hash256 && rand256) {
		memcpy(data->hash256, hash256, sizeof(data->hash256));
		memcpy(data->rand256, rand256, sizeof(data->rand256));
	} else {
		memset(data->hash256, 0, sizeof(data->hash256));
		memset(data->rand256, 0, sizeof(data->rand256));
		if (hash192 && rand192)
			data->present = 0x01;
	}

	BT_DBG("%s for %pMR", hdev->name, bdaddr);

	return 0;
}

/* This function requires the caller holds hdev->lock */
struct adv_info *hci_find_adv_instance(struct hci_dev *hdev, u8 instance)
{
	struct adv_info *adv_instance;

	list_for_each_entry(adv_instance, &hdev->adv_instances, list) {
		if (adv_instance->instance == instance)
			return adv_instance;
	}

	return NULL;
}

/* This function requires the caller holds hdev->lock */
struct adv_info *hci_get_next_instance(struct hci_dev *hdev, u8 instance)
{
	struct adv_info *cur_instance;

	cur_instance = hci_find_adv_instance(hdev, instance);
	if (!cur_instance)
		return NULL;

	if (cur_instance == list_last_entry(&hdev->adv_instances,
					    struct adv_info, list))
		return list_first_entry(&hdev->adv_instances,
						 struct adv_info, list);
	else
		return list_next_entry(cur_instance, list);
}

/* This function requires the caller holds hdev->lock */
int hci_remove_adv_instance(struct hci_dev *hdev, u8 instance)
{
	struct adv_info *adv_instance;

	adv_instance = hci_find_adv_instance(hdev, instance);
	if (!adv_instance)
		return -ENOENT;

	BT_DBG("%s removing %dMR", hdev->name, instance);

	if (hdev->cur_adv_instance == instance) {
		if (hdev->adv_instance_timeout) {
			cancel_delayed_work(&hdev->adv_instance_expire);
			hdev->adv_instance_timeout = 0;
		}
		hdev->cur_adv_instance = 0x00;
	}

	cancel_delayed_work_sync(&adv_instance->rpa_expired_cb);

	list_del(&adv_instance->list);
	kfree(adv_instance);

	hdev->adv_instance_cnt--;

	return 0;
}

void hci_adv_instances_set_rpa_expired(struct hci_dev *hdev, bool rpa_expired)
{
	struct adv_info *adv_instance, *n;

	list_for_each_entry_safe(adv_instance, n, &hdev->adv_instances, list)
		adv_instance->rpa_expired = rpa_expired;
}

/* This function requires the caller holds hdev->lock */
void hci_adv_instances_clear(struct hci_dev *hdev)
{
	struct adv_info *adv_instance, *n;

	if (hdev->adv_instance_timeout) {
		cancel_delayed_work(&hdev->adv_instance_expire);
		hdev->adv_instance_timeout = 0;
	}

	list_for_each_entry_safe(adv_instance, n, &hdev->adv_instances, list) {
		cancel_delayed_work_sync(&adv_instance->rpa_expired_cb);
		list_del(&adv_instance->list);
		kfree(adv_instance);
	}

	hdev->adv_instance_cnt = 0;
	hdev->cur_adv_instance = 0x00;
}

static void adv_instance_rpa_expired(struct work_struct *work)
{
	struct adv_info *adv_instance = container_of(work, struct adv_info,
						     rpa_expired_cb.work);

	BT_DBG("");

	adv_instance->rpa_expired = true;
}

/* This function requires the caller holds hdev->lock */
struct adv_info *hci_add_adv_instance(struct hci_dev *hdev, u8 instance,
				      u32 flags, u16 adv_data_len, u8 *adv_data,
				      u16 scan_rsp_len, u8 *scan_rsp_data,
				      u16 timeout, u16 duration, s8 tx_power,
				      u32 min_interval, u32 max_interval,
				      u8 mesh_handle)
{
	struct adv_info *adv;

	adv = hci_find_adv_instance(hdev, instance);
	if (adv) {
		memset(adv->adv_data, 0, sizeof(adv->adv_data));
		memset(adv->scan_rsp_data, 0, sizeof(adv->scan_rsp_data));
		memset(adv->per_adv_data, 0, sizeof(adv->per_adv_data));
	} else {
		if (hdev->adv_instance_cnt >= hdev->le_num_of_adv_sets ||
		    instance < 1 || instance > hdev->le_num_of_adv_sets + 1)
			return ERR_PTR(-EOVERFLOW);

		adv = kzalloc(sizeof(*adv), GFP_KERNEL);
		if (!adv)
			return ERR_PTR(-ENOMEM);

		adv->pending = true;
		adv->instance = instance;
		list_add(&adv->list, &hdev->adv_instances);
		hdev->adv_instance_cnt++;
	}

	adv->flags = flags;
	adv->min_interval = min_interval;
	adv->max_interval = max_interval;
	adv->tx_power = tx_power;
	/* Defining a mesh_handle changes the timing units to ms,
	 * rather than seconds, and ties the instance to the requested
	 * mesh_tx queue.
	 */
	adv->mesh = mesh_handle;

	hci_set_adv_instance_data(hdev, instance, adv_data_len, adv_data,
				  scan_rsp_len, scan_rsp_data);

	adv->timeout = timeout;
	adv->remaining_time = timeout;

	if (duration == 0)
		adv->duration = hdev->def_multi_adv_rotation_duration;
	else
		adv->duration = duration;

	INIT_DELAYED_WORK(&adv->rpa_expired_cb, adv_instance_rpa_expired);

	BT_DBG("%s for %dMR", hdev->name, instance);

	return adv;
}

/* This function requires the caller holds hdev->lock */
struct adv_info *hci_add_per_instance(struct hci_dev *hdev, u8 instance,
				      u32 flags, u8 data_len, u8 *data,
				      u32 min_interval, u32 max_interval)
{
	struct adv_info *adv;

	adv = hci_add_adv_instance(hdev, instance, flags, 0, NULL, 0, NULL,
				   0, 0, HCI_ADV_TX_POWER_NO_PREFERENCE,
				   min_interval, max_interval, 0);
	if (IS_ERR(adv))
		return adv;

	adv->periodic = true;
	adv->per_adv_data_len = data_len;

	if (data)
		memcpy(adv->per_adv_data, data, data_len);

	return adv;
}

/* This function requires the caller holds hdev->lock */
int hci_set_adv_instance_data(struct hci_dev *hdev, u8 instance,
			      u16 adv_data_len, u8 *adv_data,
			      u16 scan_rsp_len, u8 *scan_rsp_data)
{
	struct adv_info *adv;

	adv = hci_find_adv_instance(hdev, instance);

	/* If advertisement doesn't exist, we can't modify its data */
	if (!adv)
		return -ENOENT;

	if (adv_data_len && ADV_DATA_CMP(adv, adv_data, adv_data_len)) {
		memset(adv->adv_data, 0, sizeof(adv->adv_data));
		memcpy(adv->adv_data, adv_data, adv_data_len);
		adv->adv_data_len = adv_data_len;
		adv->adv_data_changed = true;
	}

	if (scan_rsp_len && SCAN_RSP_CMP(adv, scan_rsp_data, scan_rsp_len)) {
		memset(adv->scan_rsp_data, 0, sizeof(adv->scan_rsp_data));
		memcpy(adv->scan_rsp_data, scan_rsp_data, scan_rsp_len);
		adv->scan_rsp_len = scan_rsp_len;
		adv->scan_rsp_changed = true;
	}

	/* Mark as changed if there are flags which would affect it */
	if (((adv->flags & MGMT_ADV_FLAG_APPEARANCE) && hdev->appearance) ||
	    adv->flags & MGMT_ADV_FLAG_LOCAL_NAME)
		adv->scan_rsp_changed = true;

	return 0;
}

/* This function requires the caller holds hdev->lock */
u32 hci_adv_instance_flags(struct hci_dev *hdev, u8 instance)
{
	u32 flags;
	struct adv_info *adv;

	if (instance == 0x00) {
		/* Instance 0 always manages the "Tx Power" and "Flags"
		 * fields
		 */
		flags = MGMT_ADV_FLAG_TX_POWER | MGMT_ADV_FLAG_MANAGED_FLAGS;

		/* For instance 0, the HCI_ADVERTISING_CONNECTABLE setting
		 * corresponds to the "connectable" instance flag.
		 */
		if (hci_dev_test_flag(hdev, HCI_ADVERTISING_CONNECTABLE))
			flags |= MGMT_ADV_FLAG_CONNECTABLE;

		if (hci_dev_test_flag(hdev, HCI_LIMITED_DISCOVERABLE))
			flags |= MGMT_ADV_FLAG_LIMITED_DISCOV;
		else if (hci_dev_test_flag(hdev, HCI_DISCOVERABLE))
			flags |= MGMT_ADV_FLAG_DISCOV;

		return flags;
	}

	adv = hci_find_adv_instance(hdev, instance);

	/* Return 0 when we got an invalid instance identifier. */
	if (!adv)
		return 0;

	return adv->flags;
}

bool hci_adv_instance_is_scannable(struct hci_dev *hdev, u8 instance)
{
	struct adv_info *adv;

	/* Instance 0x00 always set local name */
	if (instance == 0x00)
		return true;

	adv = hci_find_adv_instance(hdev, instance);
	if (!adv)
		return false;

	if (adv->flags & MGMT_ADV_FLAG_APPEARANCE ||
	    adv->flags & MGMT_ADV_FLAG_LOCAL_NAME)
		return true;

	return adv->scan_rsp_len ? true : false;
}

/* This function requires the caller holds hdev->lock */
void hci_adv_monitors_clear(struct hci_dev *hdev)
{
	struct adv_monitor *monitor;
	int handle;

	idr_for_each_entry(&hdev->adv_monitors_idr, monitor, handle)
		hci_free_adv_monitor(hdev, monitor);

	idr_destroy(&hdev->adv_monitors_idr);
}

/* Frees the monitor structure and do some bookkeepings.
 * This function requires the caller holds hdev->lock.
 */
void hci_free_adv_monitor(struct hci_dev *hdev, struct adv_monitor *monitor)
{
	struct adv_pattern *pattern;
	struct adv_pattern *tmp;

	if (!monitor)
		return;

	list_for_each_entry_safe(pattern, tmp, &monitor->patterns, list) {
		list_del(&pattern->list);
		kfree(pattern);
	}

	if (monitor->handle)
		idr_remove(&hdev->adv_monitors_idr, monitor->handle);

	if (monitor->state != ADV_MONITOR_STATE_NOT_REGISTERED) {
		hdev->adv_monitors_cnt--;
		mgmt_adv_monitor_removed(hdev, monitor->handle);
	}

	kfree(monitor);
}

/* Assigns handle to a monitor, and if offloading is supported and power is on,
 * also attempts to forward the request to the controller.
 * This function requires the caller holds hci_req_sync_lock.
 */
int hci_add_adv_monitor(struct hci_dev *hdev, struct adv_monitor *monitor)
{
	int min, max, handle;
	int status = 0;

	if (!monitor)
		return -EINVAL;

	hci_dev_lock(hdev);

	min = HCI_MIN_ADV_MONITOR_HANDLE;
	max = HCI_MIN_ADV_MONITOR_HANDLE + HCI_MAX_ADV_MONITOR_NUM_HANDLES;
	handle = idr_alloc(&hdev->adv_monitors_idr, monitor, min, max,
			   GFP_KERNEL);

	hci_dev_unlock(hdev);

	if (handle < 0)
		return handle;

	monitor->handle = handle;

	if (!hdev_is_powered(hdev))
		return status;

	switch (hci_get_adv_monitor_offload_ext(hdev)) {
	case HCI_ADV_MONITOR_EXT_NONE:
		bt_dev_dbg(hdev, "%s add monitor %d status %d", hdev->name,
			   monitor->handle, status);
		/* Message was not forwarded to controller - not an error */
		break;

	case HCI_ADV_MONITOR_EXT_MSFT:
		status = msft_add_monitor_pattern(hdev, monitor);
		bt_dev_dbg(hdev, "%s add monitor %d msft status %d", hdev->name,
			   monitor->handle, status);
		break;
	}

	return status;
}

/* Attempts to tell the controller and free the monitor. If somehow the
 * controller doesn't have a corresponding handle, remove anyway.
 * This function requires the caller holds hci_req_sync_lock.
 */
static int hci_remove_adv_monitor(struct hci_dev *hdev,
				  struct adv_monitor *monitor)
{
	int status = 0;

	switch (hci_get_adv_monitor_offload_ext(hdev)) {
	case HCI_ADV_MONITOR_EXT_NONE: /* also goes here when powered off */
		bt_dev_dbg(hdev, "%s remove monitor %d status %d", hdev->name,
			   monitor->handle, status);
		goto free_monitor;

	case HCI_ADV_MONITOR_EXT_MSFT:
		status = msft_remove_monitor(hdev, monitor);
		bt_dev_dbg(hdev, "%s remove monitor %d msft status %d",
			   hdev->name, monitor->handle, status);
		break;
	}

	/* In case no matching handle registered, just free the monitor */
	if (status == -ENOENT)
		goto free_monitor;

	return status;

free_monitor:
	if (status == -ENOENT)
		bt_dev_warn(hdev, "Removing monitor with no matching handle %d",
			    monitor->handle);
	hci_free_adv_monitor(hdev, monitor);

	return status;
}

/* This function requires the caller holds hci_req_sync_lock */
int hci_remove_single_adv_monitor(struct hci_dev *hdev, u16 handle)
{
	struct adv_monitor *monitor = idr_find(&hdev->adv_monitors_idr, handle);

	if (!monitor)
		return -EINVAL;

	return hci_remove_adv_monitor(hdev, monitor);
}

/* This function requires the caller holds hci_req_sync_lock */
int hci_remove_all_adv_monitor(struct hci_dev *hdev)
{
	struct adv_monitor *monitor;
	int idr_next_id = 0;
	int status = 0;

	while (1) {
		monitor = idr_get_next(&hdev->adv_monitors_idr, &idr_next_id);
		if (!monitor)
			break;

		status = hci_remove_adv_monitor(hdev, monitor);
		if (status)
			return status;

		idr_next_id++;
	}

	return status;
}

/* This function requires the caller holds hdev->lock */
bool hci_is_adv_monitoring(struct hci_dev *hdev)
{
	return !idr_is_empty(&hdev->adv_monitors_idr);
}

int hci_get_adv_monitor_offload_ext(struct hci_dev *hdev)
{
	if (msft_monitor_supported(hdev))
		return HCI_ADV_MONITOR_EXT_MSFT;

	return HCI_ADV_MONITOR_EXT_NONE;
}

struct bdaddr_list *hci_bdaddr_list_lookup(struct list_head *bdaddr_list,
					 bdaddr_t *bdaddr, u8 type)
{
	struct bdaddr_list *b;

	list_for_each_entry(b, bdaddr_list, list) {
		if (!bacmp(&b->bdaddr, bdaddr) && b->bdaddr_type == type)
			return b;
	}

	return NULL;
}

struct bdaddr_list_with_irk *hci_bdaddr_list_lookup_with_irk(
				struct list_head *bdaddr_list, bdaddr_t *bdaddr,
				u8 type)
{
	struct bdaddr_list_with_irk *b;

	list_for_each_entry(b, bdaddr_list, list) {
		if (!bacmp(&b->bdaddr, bdaddr) && b->bdaddr_type == type)
			return b;
	}

	return NULL;
}

struct bdaddr_list_with_flags *
hci_bdaddr_list_lookup_with_flags(struct list_head *bdaddr_list,
				  bdaddr_t *bdaddr, u8 type)
{
	struct bdaddr_list_with_flags *b;

	list_for_each_entry(b, bdaddr_list, list) {
		if (!bacmp(&b->bdaddr, bdaddr) && b->bdaddr_type == type)
			return b;
	}

	return NULL;
}

void hci_bdaddr_list_clear(struct list_head *bdaddr_list)
{
	struct bdaddr_list *b, *n;

	list_for_each_entry_safe(b, n, bdaddr_list, list) {
		list_del(&b->list);
		kfree(b);
	}
}

int hci_bdaddr_list_add(struct list_head *list, bdaddr_t *bdaddr, u8 type)
{
	struct bdaddr_list *entry;

	if (!bacmp(bdaddr, BDADDR_ANY))
		return -EBADF;

	if (hci_bdaddr_list_lookup(list, bdaddr, type))
		return -EEXIST;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	bacpy(&entry->bdaddr, bdaddr);
	entry->bdaddr_type = type;

	list_add(&entry->list, list);

	return 0;
}

int hci_bdaddr_list_add_with_irk(struct list_head *list, bdaddr_t *bdaddr,
					u8 type, u8 *peer_irk, u8 *local_irk)
{
	struct bdaddr_list_with_irk *entry;

	if (!bacmp(bdaddr, BDADDR_ANY))
		return -EBADF;

	if (hci_bdaddr_list_lookup(list, bdaddr, type))
		return -EEXIST;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	bacpy(&entry->bdaddr, bdaddr);
	entry->bdaddr_type = type;

	if (peer_irk)
		memcpy(entry->peer_irk, peer_irk, 16);

	if (local_irk)
		memcpy(entry->local_irk, local_irk, 16);

	list_add(&entry->list, list);

	return 0;
}

int hci_bdaddr_list_add_with_flags(struct list_head *list, bdaddr_t *bdaddr,
				   u8 type, u32 flags)
{
	struct bdaddr_list_with_flags *entry;

	if (!bacmp(bdaddr, BDADDR_ANY))
		return -EBADF;

	if (hci_bdaddr_list_lookup(list, bdaddr, type))
		return -EEXIST;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	bacpy(&entry->bdaddr, bdaddr);
	entry->bdaddr_type = type;
	entry->flags = flags;

	list_add(&entry->list, list);

	return 0;
}

int hci_bdaddr_list_del(struct list_head *list, bdaddr_t *bdaddr, u8 type)
{
	struct bdaddr_list *entry;

	if (!bacmp(bdaddr, BDADDR_ANY)) {
		hci_bdaddr_list_clear(list);
		return 0;
	}

	entry = hci_bdaddr_list_lookup(list, bdaddr, type);
	if (!entry)
		return -ENOENT;

	list_del(&entry->list);
	kfree(entry);

	return 0;
}

int hci_bdaddr_list_del_with_irk(struct list_head *list, bdaddr_t *bdaddr,
							u8 type)
{
	struct bdaddr_list_with_irk *entry;

	if (!bacmp(bdaddr, BDADDR_ANY)) {
		hci_bdaddr_list_clear(list);
		return 0;
	}

	entry = hci_bdaddr_list_lookup_with_irk(list, bdaddr, type);
	if (!entry)
		return -ENOENT;

	list_del(&entry->list);
	kfree(entry);

	return 0;
}

int hci_bdaddr_list_del_with_flags(struct list_head *list, bdaddr_t *bdaddr,
				   u8 type)
{
	struct bdaddr_list_with_flags *entry;

	if (!bacmp(bdaddr, BDADDR_ANY)) {
		hci_bdaddr_list_clear(list);
		return 0;
	}

	entry = hci_bdaddr_list_lookup_with_flags(list, bdaddr, type);
	if (!entry)
		return -ENOENT;

	list_del(&entry->list);
	kfree(entry);

	return 0;
}

/* This function requires the caller holds hdev->lock */
struct hci_conn_params *hci_conn_params_lookup(struct hci_dev *hdev,
					       bdaddr_t *addr, u8 addr_type)
{
	struct hci_conn_params *params;

	list_for_each_entry(params, &hdev->le_conn_params, list) {
		if (bacmp(&params->addr, addr) == 0 &&
		    params->addr_type == addr_type) {
			return params;
		}
	}

	return NULL;
}

/* This function requires the caller holds hdev->lock */
struct hci_conn_params *hci_pend_le_action_lookup(struct list_head *list,
						  bdaddr_t *addr, u8 addr_type)
{
	struct hci_conn_params *param;

	list_for_each_entry(param, list, action) {
		if (bacmp(&param->addr, addr) == 0 &&
		    param->addr_type == addr_type)
			return param;
	}

	return NULL;
}

/* This function requires the caller holds hdev->lock */
struct hci_conn_params *hci_conn_params_add(struct hci_dev *hdev,
					    bdaddr_t *addr, u8 addr_type)
{
	struct hci_conn_params *params;

	params = hci_conn_params_lookup(hdev, addr, addr_type);
	if (params)
		return params;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params) {
		bt_dev_err(hdev, "out of memory");
		return NULL;
	}

	bacpy(&params->addr, addr);
	params->addr_type = addr_type;

	list_add(&params->list, &hdev->le_conn_params);
	INIT_LIST_HEAD(&params->action);

	params->conn_min_interval = hdev->le_conn_min_interval;
	params->conn_max_interval = hdev->le_conn_max_interval;
	params->conn_latency = hdev->le_conn_latency;
	params->supervision_timeout = hdev->le_supv_timeout;
	params->auto_connect = HCI_AUTO_CONN_DISABLED;

	BT_DBG("addr %pMR (type %u)", addr, addr_type);

	return params;
}

static void hci_conn_params_free(struct hci_conn_params *params)
{
	if (params->conn) {
		hci_conn_drop(params->conn);
		hci_conn_put(params->conn);
	}

	list_del(&params->action);
	list_del(&params->list);
	kfree(params);
}

/* This function requires the caller holds hdev->lock */
void hci_conn_params_del(struct hci_dev *hdev, bdaddr_t *addr, u8 addr_type)
{
	struct hci_conn_params *params;

	params = hci_conn_params_lookup(hdev, addr, addr_type);
	if (!params)
		return;

	hci_conn_params_free(params);

	hci_update_passive_scan(hdev);

	BT_DBG("addr %pMR (type %u)", addr, addr_type);
}

/* This function requires the caller holds hdev->lock */
void hci_conn_params_clear_disabled(struct hci_dev *hdev)
{
	struct hci_conn_params *params, *tmp;

	list_for_each_entry_safe(params, tmp, &hdev->le_conn_params, list) {
		if (params->auto_connect != HCI_AUTO_CONN_DISABLED)
			continue;

		/* If trying to establish one time connection to disabled
		 * device, leave the params, but mark them as just once.
		 */
		if (params->explicit_connect) {
			params->auto_connect = HCI_AUTO_CONN_EXPLICIT;
			continue;
		}

		list_del(&params->list);
		kfree(params);
	}

	BT_DBG("All LE disabled connection parameters were removed");
}

/* This function requires the caller holds hdev->lock */
static void hci_conn_params_clear_all(struct hci_dev *hdev)
{
	struct hci_conn_params *params, *tmp;

	list_for_each_entry_safe(params, tmp, &hdev->le_conn_params, list)
		hci_conn_params_free(params);

	BT_DBG("All LE connection parameters were removed");
}

/* Copy the Identity Address of the controller.
 *
 * If the controller has a public BD_ADDR, then by default use that one.
 * If this is a LE only controller without a public address, default to
 * the static random address.
 *
 * For debugging purposes it is possible to force controllers with a
 * public address to use the static random address instead.
 *
 * In case BR/EDR has been disabled on a dual-mode controller and
 * userspace has configured a static address, then that address
 * becomes the identity address instead of the public BR/EDR address.
 */
void hci_copy_identity_address(struct hci_dev *hdev, bdaddr_t *bdaddr,
			       u8 *bdaddr_type)
{
	if (hci_dev_test_flag(hdev, HCI_FORCE_STATIC_ADDR) ||
	    !bacmp(&hdev->bdaddr, BDADDR_ANY) ||
	    (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) &&
	     bacmp(&hdev->static_addr, BDADDR_ANY))) {
		bacpy(bdaddr, &hdev->static_addr);
		*bdaddr_type = ADDR_LE_DEV_RANDOM;
	} else {
		bacpy(bdaddr, &hdev->bdaddr);
		*bdaddr_type = ADDR_LE_DEV_PUBLIC;
	}
}

static void hci_clear_wake_reason(struct hci_dev *hdev)
{
	hci_dev_lock(hdev);

	hdev->wake_reason = 0;
	bacpy(&hdev->wake_addr, BDADDR_ANY);
	hdev->wake_addr_type = 0;

	hci_dev_unlock(hdev);
}

static int hci_suspend_notifier(struct notifier_block *nb, unsigned long action,
				void *data)
{
	struct hci_dev *hdev =
		container_of(nb, struct hci_dev, suspend_notifier);
	int ret = 0;

	/* Userspace has full control of this device. Do nothing. */
	if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL))
		return NOTIFY_DONE;

	if (action == PM_SUSPEND_PREPARE)
		ret = hci_suspend_dev(hdev);
	else if (action == PM_POST_SUSPEND)
		ret = hci_resume_dev(hdev);

	if (ret)
		bt_dev_err(hdev, "Suspend notifier action (%lu) failed: %d",
			   action, ret);

	return NOTIFY_DONE;
}

/* Alloc HCI device */
struct hci_dev *hci_alloc_dev_priv(int sizeof_priv)
{
	struct hci_dev *hdev;
	unsigned int alloc_size;

	alloc_size = sizeof(*hdev);
	if (sizeof_priv) {
		/* Fixme: May need ALIGN-ment? */
		alloc_size += sizeof_priv;
	}

	hdev = kzalloc(alloc_size, GFP_KERNEL);
	if (!hdev)
		return NULL;

	hdev->pkt_type  = (HCI_DM1 | HCI_DH1 | HCI_HV1);
	hdev->esco_type = (ESCO_HV1);
	hdev->link_mode = (HCI_LM_ACCEPT);
	hdev->num_iac = 0x01;		/* One IAC support is mandatory */
	hdev->io_capability = 0x03;	/* No Input No Output */
	hdev->manufacturer = 0xffff;	/* Default to internal use */
	hdev->inq_tx_power = HCI_TX_POWER_INVALID;
	hdev->adv_tx_power = HCI_TX_POWER_INVALID;
	hdev->adv_instance_cnt = 0;
	hdev->cur_adv_instance = 0x00;
	hdev->adv_instance_timeout = 0;

	hdev->advmon_allowlist_duration = 300;
	hdev->advmon_no_filter_duration = 500;
	hdev->enable_advmon_interleave_scan = 0x00;	/* Default to disable */

	hdev->sniff_max_interval = 800;
	hdev->sniff_min_interval = 80;

	hdev->le_adv_channel_map = 0x07;
	hdev->le_adv_min_interval = 0x0800;
	hdev->le_adv_max_interval = 0x0800;
	hdev->le_scan_interval = 0x0060;
	hdev->le_scan_window = 0x0030;
	hdev->le_scan_int_suspend = 0x0400;
	hdev->le_scan_window_suspend = 0x0012;
	hdev->le_scan_int_discovery = DISCOV_LE_SCAN_INT;
	hdev->le_scan_window_discovery = DISCOV_LE_SCAN_WIN;
	hdev->le_scan_int_adv_monitor = 0x0060;
	hdev->le_scan_window_adv_monitor = 0x0030;
	hdev->le_scan_int_connect = 0x0060;
	hdev->le_scan_window_connect = 0x0060;
	hdev->le_conn_min_interval = 0x0018;
	hdev->le_conn_max_interval = 0x0028;
	hdev->le_conn_latency = 0x0000;
	hdev->le_supv_timeout = 0x002a;
	hdev->le_def_tx_len = 0x001b;
	hdev->le_def_tx_time = 0x0148;
	hdev->le_max_tx_len = 0x001b;
	hdev->le_max_tx_time = 0x0148;
	hdev->le_max_rx_len = 0x001b;
	hdev->le_max_rx_time = 0x0148;
	hdev->le_max_key_size = SMP_MAX_ENC_KEY_SIZE;
	hdev->le_min_key_size = SMP_MIN_ENC_KEY_SIZE;
	hdev->le_tx_def_phys = HCI_LE_SET_PHY_1M;
	hdev->le_rx_def_phys = HCI_LE_SET_PHY_1M;
	hdev->le_num_of_adv_sets = HCI_MAX_ADV_INSTANCES;
	hdev->def_multi_adv_rotation_duration = HCI_DEFAULT_ADV_DURATION;
	hdev->def_le_autoconnect_timeout = HCI_LE_AUTOCONN_TIMEOUT;
	hdev->min_le_tx_power = HCI_TX_POWER_INVALID;
	hdev->max_le_tx_power = HCI_TX_POWER_INVALID;

	hdev->rpa_timeout = HCI_DEFAULT_RPA_TIMEOUT;
	hdev->discov_interleaved_timeout = DISCOV_INTERLEAVED_TIMEOUT;
	hdev->conn_info_min_age = DEFAULT_CONN_INFO_MIN_AGE;
	hdev->conn_info_max_age = DEFAULT_CONN_INFO_MAX_AGE;
	hdev->auth_payload_timeout = DEFAULT_AUTH_PAYLOAD_TIMEOUT;
	hdev->min_enc_key_size = HCI_MIN_ENC_KEY_SIZE;

	/* default 1.28 sec page scan */
	hdev->def_page_scan_type = PAGE_SCAN_TYPE_STANDARD;
	hdev->def_page_scan_int = 0x0800;
	hdev->def_page_scan_window = 0x0012;

	mutex_init(&hdev->lock);
	mutex_init(&hdev->req_lock);

	INIT_LIST_HEAD(&hdev->mesh_pending);
	INIT_LIST_HEAD(&hdev->mgmt_pending);
	INIT_LIST_HEAD(&hdev->reject_list);
	INIT_LIST_HEAD(&hdev->accept_list);
	INIT_LIST_HEAD(&hdev->uuids);
	INIT_LIST_HEAD(&hdev->link_keys);
	INIT_LIST_HEAD(&hdev->long_term_keys);
	INIT_LIST_HEAD(&hdev->identity_resolving_keys);
	INIT_LIST_HEAD(&hdev->remote_oob_data);
	INIT_LIST_HEAD(&hdev->le_accept_list);
	INIT_LIST_HEAD(&hdev->le_resolv_list);
	INIT_LIST_HEAD(&hdev->le_conn_params);
	INIT_LIST_HEAD(&hdev->pend_le_conns);
	INIT_LIST_HEAD(&hdev->pend_le_reports);
	INIT_LIST_HEAD(&hdev->conn_hash.list);
	INIT_LIST_HEAD(&hdev->adv_instances);
	INIT_LIST_HEAD(&hdev->blocked_keys);
	INIT_LIST_HEAD(&hdev->monitored_devices);

	INIT_LIST_HEAD(&hdev->local_codecs);
	INIT_WORK(&hdev->rx_work, hci_rx_work);
	INIT_WORK(&hdev->cmd_work, hci_cmd_work);
	INIT_WORK(&hdev->tx_work, hci_tx_work);
	INIT_WORK(&hdev->power_on, hci_power_on);
	INIT_WORK(&hdev->error_reset, hci_error_reset);

	hci_cmd_sync_init(hdev);

	INIT_DELAYED_WORK(&hdev->power_off, hci_power_off);

	skb_queue_head_init(&hdev->rx_q);
	skb_queue_head_init(&hdev->cmd_q);
	skb_queue_head_init(&hdev->raw_q);

	init_waitqueue_head(&hdev->req_wait_q);

	INIT_DELAYED_WORK(&hdev->cmd_timer, hci_cmd_timeout);
	INIT_DELAYED_WORK(&hdev->ncmd_timer, hci_ncmd_timeout);

	hci_request_setup(hdev);

	hci_init_sysfs(hdev);
	discovery_init(hdev);

	return hdev;
}
EXPORT_SYMBOL(hci_alloc_dev_priv);

/* Free HCI device */
void hci_free_dev(struct hci_dev *hdev)
{
	/* will free via device release */
	put_device(&hdev->dev);
}
EXPORT_SYMBOL(hci_free_dev);

/* Register HCI device */
int hci_register_dev(struct hci_dev *hdev)
{
	int id, error;

	if (!hdev->open || !hdev->close || !hdev->send)
		return -EINVAL;

	/* Do not allow HCI_AMP devices to register at index 0,
	 * so the index can be used as the AMP controller ID.
	 */
	switch (hdev->dev_type) {
	case HCI_PRIMARY:
		id = ida_simple_get(&hci_index_ida, 0, HCI_MAX_ID, GFP_KERNEL);
		break;
	case HCI_AMP:
		id = ida_simple_get(&hci_index_ida, 1, HCI_MAX_ID, GFP_KERNEL);
		break;
	default:
		return -EINVAL;
	}

	if (id < 0)
		return id;

	snprintf(hdev->name, sizeof(hdev->name), "hci%d", id);
	hdev->id = id;

	BT_DBG("%p name %s bus %d", hdev, hdev->name, hdev->bus);

	hdev->workqueue = alloc_ordered_workqueue("%s", WQ_HIGHPRI, hdev->name);
	if (!hdev->workqueue) {
		error = -ENOMEM;
		goto err;
	}

	hdev->req_workqueue = alloc_ordered_workqueue("%s", WQ_HIGHPRI,
						      hdev->name);
	if (!hdev->req_workqueue) {
		destroy_workqueue(hdev->workqueue);
		error = -ENOMEM;
		goto err;
	}

	if (!IS_ERR_OR_NULL(bt_debugfs))
		hdev->debugfs = debugfs_create_dir(hdev->name, bt_debugfs);

	dev_set_name(&hdev->dev, "%s", hdev->name);

	error = device_add(&hdev->dev);
	if (error < 0)
		goto err_wqueue;

	hci_leds_init(hdev);

	hdev->rfkill = rfkill_alloc(hdev->name, &hdev->dev,
				    RFKILL_TYPE_BLUETOOTH, &hci_rfkill_ops,
				    hdev);
	if (hdev->rfkill) {
		if (rfkill_register(hdev->rfkill) < 0) {
			rfkill_destroy(hdev->rfkill);
			hdev->rfkill = NULL;
		}
	}

	if (hdev->rfkill && rfkill_blocked(hdev->rfkill))
		hci_dev_set_flag(hdev, HCI_RFKILLED);

	hci_dev_set_flag(hdev, HCI_SETUP);
	hci_dev_set_flag(hdev, HCI_AUTO_OFF);

	if (hdev->dev_type == HCI_PRIMARY) {
		/* Assume BR/EDR support until proven otherwise (such as
		 * through reading supported features during init.
		 */
		hci_dev_set_flag(hdev, HCI_BREDR_ENABLED);
	}

	write_lock(&hci_dev_list_lock);
	list_add(&hdev->list, &hci_dev_list);
	write_unlock(&hci_dev_list_lock);

	/* Devices that are marked for raw-only usage are unconfigured
	 * and should not be included in normal operation.
	 */
	if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
		hci_dev_set_flag(hdev, HCI_UNCONFIGURED);

	/* Mark Remote Wakeup connection flag as supported if driver has wakeup
	 * callback.
	 */
	if (hdev->wakeup)
		hdev->conn_flags |= HCI_CONN_FLAG_REMOTE_WAKEUP;

	hci_sock_dev_event(hdev, HCI_DEV_REG);
	hci_dev_hold(hdev);

	error = hci_register_suspend_notifier(hdev);
	if (error)
		BT_WARN("register suspend notifier failed error:%d\n", error);

	queue_work(hdev->req_workqueue, &hdev->power_on);

	idr_init(&hdev->adv_monitors_idr);
	msft_register(hdev);

	return id;

err_wqueue:
	debugfs_remove_recursive(hdev->debugfs);
	destroy_workqueue(hdev->workqueue);
	destroy_workqueue(hdev->req_workqueue);
err:
	ida_simple_remove(&hci_index_ida, hdev->id);

	return error;
}
EXPORT_SYMBOL(hci_register_dev);

/* Unregister HCI device */
void hci_unregister_dev(struct hci_dev *hdev)
{
	BT_DBG("%p name %s bus %d", hdev, hdev->name, hdev->bus);

	mutex_lock(&hdev->unregister_lock);
	hci_dev_set_flag(hdev, HCI_UNREGISTER);
	mutex_unlock(&hdev->unregister_lock);

	write_lock(&hci_dev_list_lock);
	list_del(&hdev->list);
	write_unlock(&hci_dev_list_lock);

	cancel_work_sync(&hdev->power_on);

	hci_cmd_sync_clear(hdev);

	hci_unregister_suspend_notifier(hdev);

	msft_unregister(hdev);

	hci_dev_do_close(hdev);

	if (!test_bit(HCI_INIT, &hdev->flags) &&
	    !hci_dev_test_flag(hdev, HCI_SETUP) &&
	    !hci_dev_test_flag(hdev, HCI_CONFIG)) {
		hci_dev_lock(hdev);
		mgmt_index_removed(hdev);
		hci_dev_unlock(hdev);
	}

	/* mgmt_index_removed should take care of emptying the
	 * pending list */
	BUG_ON(!list_empty(&hdev->mgmt_pending));

	hci_sock_dev_event(hdev, HCI_DEV_UNREG);

	if (hdev->rfkill) {
		rfkill_unregister(hdev->rfkill);
		rfkill_destroy(hdev->rfkill);
	}

	device_del(&hdev->dev);
	/* Actual cleanup is deferred until hci_release_dev(). */
	hci_dev_put(hdev);
}
EXPORT_SYMBOL(hci_unregister_dev);

/* Release HCI device */
void hci_release_dev(struct hci_dev *hdev)
{
	debugfs_remove_recursive(hdev->debugfs);
	kfree_const(hdev->hw_info);
	kfree_const(hdev->fw_info);

	destroy_workqueue(hdev->workqueue);
	destroy_workqueue(hdev->req_workqueue);

	hci_dev_lock(hdev);
	hci_bdaddr_list_clear(&hdev->reject_list);
	hci_bdaddr_list_clear(&hdev->accept_list);
	hci_uuids_clear(hdev);
	hci_link_keys_clear(hdev);
	hci_smp_ltks_clear(hdev);
	hci_smp_irks_clear(hdev);
	hci_remote_oob_data_clear(hdev);
	hci_adv_instances_clear(hdev);
	hci_adv_monitors_clear(hdev);
	hci_bdaddr_list_clear(&hdev->le_accept_list);
	hci_bdaddr_list_clear(&hdev->le_resolv_list);
	hci_conn_params_clear_all(hdev);
	hci_discovery_filter_clear(hdev);
	hci_blocked_keys_clear(hdev);
	hci_dev_unlock(hdev);

	ida_simple_remove(&hci_index_ida, hdev->id);
	kfree_skb(hdev->sent_cmd);
	kfree_skb(hdev->recv_event);
	kfree(hdev);
}
EXPORT_SYMBOL(hci_release_dev);

int hci_register_suspend_notifier(struct hci_dev *hdev)
{
	int ret = 0;

	if (!hdev->suspend_notifier.notifier_call &&
	    !test_bit(HCI_QUIRK_NO_SUSPEND_NOTIFIER, &hdev->quirks)) {
		hdev->suspend_notifier.notifier_call = hci_suspend_notifier;
		ret = register_pm_notifier(&hdev->suspend_notifier);
	}

	return ret;
}

int hci_unregister_suspend_notifier(struct hci_dev *hdev)
{
	int ret = 0;

	if (hdev->suspend_notifier.notifier_call) {
		ret = unregister_pm_notifier(&hdev->suspend_notifier);
		if (!ret)
			hdev->suspend_notifier.notifier_call = NULL;
	}

	return ret;
}

/* Suspend HCI device */
int hci_suspend_dev(struct hci_dev *hdev)
{
	int ret;

	bt_dev_dbg(hdev, "");

	/* Suspend should only act on when powered. */
	if (!hdev_is_powered(hdev) ||
	    hci_dev_test_flag(hdev, HCI_UNREGISTER))
		return 0;

	/* If powering down don't attempt to suspend */
	if (mgmt_powering_down(hdev))
		return 0;

	hci_req_sync_lock(hdev);
	ret = hci_suspend_sync(hdev);
	hci_req_sync_unlock(hdev);

	hci_clear_wake_reason(hdev);
	mgmt_suspending(hdev, hdev->suspend_state);

	hci_sock_dev_event(hdev, HCI_DEV_SUSPEND);
	return ret;
}
EXPORT_SYMBOL(hci_suspend_dev);

/* Resume HCI device */
int hci_resume_dev(struct hci_dev *hdev)
{
	int ret;

	bt_dev_dbg(hdev, "");

	/* Resume should only act on when powered. */
	if (!hdev_is_powered(hdev) ||
	    hci_dev_test_flag(hdev, HCI_UNREGISTER))
		return 0;

	/* If powering down don't attempt to resume */
	if (mgmt_powering_down(hdev))
		return 0;

	hci_req_sync_lock(hdev);
	ret = hci_resume_sync(hdev);
	hci_req_sync_unlock(hdev);

	mgmt_resuming(hdev, hdev->wake_reason, &hdev->wake_addr,
		      hdev->wake_addr_type);

	hci_sock_dev_event(hdev, HCI_DEV_RESUME);
	return ret;
}
EXPORT_SYMBOL(hci_resume_dev);

/* Reset HCI device */
int hci_reset_dev(struct hci_dev *hdev)
{
	static const u8 hw_err[] = { HCI_EV_HARDWARE_ERROR, 0x01, 0x00 };
	struct sk_buff *skb;

	skb = bt_skb_alloc(3, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;
	skb_put_data(skb, hw_err, 3);

	bt_dev_err(hdev, "Injecting HCI hardware error event");

	/* Send Hardware Error to upper stack */
	return hci_recv_frame(hdev, skb);
}
EXPORT_SYMBOL(hci_reset_dev);

/* Receive frame from HCI drivers */
int hci_recv_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	if (!hdev || (!test_bit(HCI_UP, &hdev->flags)
		      && !test_bit(HCI_INIT, &hdev->flags))) {
		kfree_skb(skb);
		return -ENXIO;
	}

	switch (hci_skb_pkt_type(skb)) {
	case HCI_EVENT_PKT:
		break;
	case HCI_ACLDATA_PKT:
		/* Detect if ISO packet has been sent as ACL */
		if (hci_conn_num(hdev, ISO_LINK)) {
			__u16 handle = __le16_to_cpu(hci_acl_hdr(skb)->handle);
			__u8 type;

			type = hci_conn_lookup_type(hdev, hci_handle(handle));
			if (type == ISO_LINK)
				hci_skb_pkt_type(skb) = HCI_ISODATA_PKT;
		}
		break;
	case HCI_SCODATA_PKT:
		break;
	case HCI_ISODATA_PKT:
		break;
	default:
		kfree_skb(skb);
		return -EINVAL;
	}

	/* Incoming skb */
	bt_cb(skb)->incoming = 1;

	/* Time stamp */
	__net_timestamp(skb);

	skb_queue_tail(&hdev->rx_q, skb);
	queue_work(hdev->workqueue, &hdev->rx_work);

	return 0;
}
EXPORT_SYMBOL(hci_recv_frame);

/* Receive diagnostic message from HCI drivers */
int hci_recv_diag(struct hci_dev *hdev, struct sk_buff *skb)
{
	/* Mark as diagnostic packet */
	hci_skb_pkt_type(skb) = HCI_DIAG_PKT;

	/* Time stamp */
	__net_timestamp(skb);

	skb_queue_tail(&hdev->rx_q, skb);
	queue_work(hdev->workqueue, &hdev->rx_work);

	return 0;
}
EXPORT_SYMBOL(hci_recv_diag);

void hci_set_hw_info(struct hci_dev *hdev, const char *fmt, ...)
{
	va_list vargs;

	va_start(vargs, fmt);
	kfree_const(hdev->hw_info);
	hdev->hw_info = kvasprintf_const(GFP_KERNEL, fmt, vargs);
	va_end(vargs);
}
EXPORT_SYMBOL(hci_set_hw_info);

void hci_set_fw_info(struct hci_dev *hdev, const char *fmt, ...)
{
	va_list vargs;

	va_start(vargs, fmt);
	kfree_const(hdev->fw_info);
	hdev->fw_info = kvasprintf_const(GFP_KERNEL, fmt, vargs);
	va_end(vargs);
}
EXPORT_SYMBOL(hci_set_fw_info);

/* ---- Interface to upper protocols ---- */

int hci_register_cb(struct hci_cb *cb)
{
	BT_DBG("%p name %s", cb, cb->name);

	mutex_lock(&hci_cb_list_lock);
	list_add_tail(&cb->list, &hci_cb_list);
	mutex_unlock(&hci_cb_list_lock);

	return 0;
}
EXPORT_SYMBOL(hci_register_cb);

int hci_unregister_cb(struct hci_cb *cb)
{
	BT_DBG("%p name %s", cb, cb->name);

	mutex_lock(&hci_cb_list_lock);
	list_del(&cb->list);
	mutex_unlock(&hci_cb_list_lock);

	return 0;
}
EXPORT_SYMBOL(hci_unregister_cb);

static int hci_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	int err;

	BT_DBG("%s type %d len %d", hdev->name, hci_skb_pkt_type(skb),
	       skb->len);

	/* Time stamp */
	__net_timestamp(skb);

	/* Send copy to monitor */
	hci_send_to_monitor(hdev, skb);

	if (atomic_read(&hdev->promisc)) {
		/* Send copy to the sockets */
		hci_send_to_sock(hdev, skb);
	}

	/* Get rid of skb owner, prior to sending to the driver. */
	skb_orphan(skb);

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	err = hdev->send(hdev, skb);
	if (err < 0) {
		bt_dev_err(hdev, "sending frame failed (%d)", err);
		kfree_skb(skb);
		return err;
	}

	return 0;
}

/* Send HCI command */
int hci_send_cmd(struct hci_dev *hdev, __u16 opcode, __u32 plen,
		 const void *param)
{
	struct sk_buff *skb;

	BT_DBG("%s opcode 0x%4.4x plen %d", hdev->name, opcode, plen);

	skb = hci_prepare_cmd(hdev, opcode, plen, param);
	if (!skb) {
		bt_dev_err(hdev, "no memory for command");
		return -ENOMEM;
	}

	/* Stand-alone HCI commands must be flagged as
	 * single-command requests.
	 */
	bt_cb(skb)->hci.req_flags |= HCI_REQ_START;

	skb_queue_tail(&hdev->cmd_q, skb);
	queue_work(hdev->workqueue, &hdev->cmd_work);

	return 0;
}

int __hci_cmd_send(struct hci_dev *hdev, u16 opcode, u32 plen,
		   const void *param)
{
	struct sk_buff *skb;

	if (hci_opcode_ogf(opcode) != 0x3f) {
		/* A controller receiving a command shall respond with either
		 * a Command Status Event or a Command Complete Event.
		 * Therefore, all standard HCI commands must be sent via the
		 * standard API, using hci_send_cmd or hci_cmd_sync helpers.
		 * Some vendors do not comply with this rule for vendor-specific
		 * commands and do not return any event. We want to support
		 * unresponded commands for such cases only.
		 */
		bt_dev_err(hdev, "unresponded command not supported");
		return -EINVAL;
	}

	skb = hci_prepare_cmd(hdev, opcode, plen, param);
	if (!skb) {
		bt_dev_err(hdev, "no memory for command (opcode 0x%4.4x)",
			   opcode);
		return -ENOMEM;
	}

	hci_send_frame(hdev, skb);

	return 0;
}
EXPORT_SYMBOL(__hci_cmd_send);

/* Get data from the previously sent command */
void *hci_sent_cmd_data(struct hci_dev *hdev, __u16 opcode)
{
	struct hci_command_hdr *hdr;

	if (!hdev->sent_cmd)
		return NULL;

	hdr = (void *) hdev->sent_cmd->data;

	if (hdr->opcode != cpu_to_le16(opcode))
		return NULL;

	BT_DBG("%s opcode 0x%4.4x", hdev->name, opcode);

	return hdev->sent_cmd->data + HCI_COMMAND_HDR_SIZE;
}

/* Get data from last received event */
void *hci_recv_event_data(struct hci_dev *hdev, __u8 event)
{
	struct hci_event_hdr *hdr;
	int offset;

	if (!hdev->recv_event)
		return NULL;

	hdr = (void *)hdev->recv_event->data;
	offset = sizeof(*hdr);

	if (hdr->evt != event) {
		/* In case of LE metaevent check the subevent match */
		if (hdr->evt == HCI_EV_LE_META) {
			struct hci_ev_le_meta *ev;

			ev = (void *)hdev->recv_event->data + offset;
			offset += sizeof(*ev);
			if (ev->subevent == event)
				goto found;
		}
		return NULL;
	}

found:
	bt_dev_dbg(hdev, "event 0x%2.2x", event);

	return hdev->recv_event->data + offset;
}

/* Send ACL data */
static void hci_add_acl_hdr(struct sk_buff *skb, __u16 handle, __u16 flags)
{
	struct hci_acl_hdr *hdr;
	int len = skb->len;

	skb_push(skb, HCI_ACL_HDR_SIZE);
	skb_reset_transport_header(skb);
	hdr = (struct hci_acl_hdr *)skb_transport_header(skb);
	hdr->handle = cpu_to_le16(hci_handle_pack(handle, flags));
	hdr->dlen   = cpu_to_le16(len);
}

static void hci_queue_acl(struct hci_chan *chan, struct sk_buff_head *queue,
			  struct sk_buff *skb, __u16 flags)
{
	struct hci_conn *conn = chan->conn;
	struct hci_dev *hdev = conn->hdev;
	struct sk_buff *list;

	skb->len = skb_headlen(skb);
	skb->data_len = 0;

	hci_skb_pkt_type(skb) = HCI_ACLDATA_PKT;

	switch (hdev->dev_type) {
	case HCI_PRIMARY:
		hci_add_acl_hdr(skb, conn->handle, flags);
		break;
	case HCI_AMP:
		hci_add_acl_hdr(skb, chan->handle, flags);
		break;
	default:
		bt_dev_err(hdev, "unknown dev_type %d", hdev->dev_type);
		return;
	}

	list = skb_shinfo(skb)->frag_list;
	if (!list) {
		/* Non fragmented */
		BT_DBG("%s nonfrag skb %p len %d", hdev->name, skb, skb->len);

		skb_queue_tail(queue, skb);
	} else {
		/* Fragmented */
		BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

		skb_shinfo(skb)->frag_list = NULL;

		/* Queue all fragments atomically. We need to use spin_lock_bh
		 * here because of 6LoWPAN links, as there this function is
		 * called from softirq and using normal spin lock could cause
		 * deadlocks.
		 */
		spin_lock_bh(&queue->lock);

		__skb_queue_tail(queue, skb);

		flags &= ~ACL_START;
		flags |= ACL_CONT;
		do {
			skb = list; list = list->next;

			hci_skb_pkt_type(skb) = HCI_ACLDATA_PKT;
			hci_add_acl_hdr(skb, conn->handle, flags);

			BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

			__skb_queue_tail(queue, skb);
		} while (list);

		spin_unlock_bh(&queue->lock);
	}
}

void hci_send_acl(struct hci_chan *chan, struct sk_buff *skb, __u16 flags)
{
	struct hci_dev *hdev = chan->conn->hdev;

	BT_DBG("%s chan %p flags 0x%4.4x", hdev->name, chan, flags);

	hci_queue_acl(chan, &chan->data_q, skb, flags);

	queue_work(hdev->workqueue, &hdev->tx_work);
}

/* Send SCO data */
void hci_send_sco(struct hci_conn *conn, struct sk_buff *skb)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_sco_hdr hdr;

	BT_DBG("%s len %d", hdev->name, skb->len);

	hdr.handle = cpu_to_le16(conn->handle);
	hdr.dlen   = skb->len;

	skb_push(skb, HCI_SCO_HDR_SIZE);
	skb_reset_transport_header(skb);
	memcpy(skb_transport_header(skb), &hdr, HCI_SCO_HDR_SIZE);

	hci_skb_pkt_type(skb) = HCI_SCODATA_PKT;

	skb_queue_tail(&conn->data_q, skb);
	queue_work(hdev->workqueue, &hdev->tx_work);
}

/* Send ISO data */
static void hci_add_iso_hdr(struct sk_buff *skb, __u16 handle, __u8 flags)
{
	struct hci_iso_hdr *hdr;
	int len = skb->len;

	skb_push(skb, HCI_ISO_HDR_SIZE);
	skb_reset_transport_header(skb);
	hdr = (struct hci_iso_hdr *)skb_transport_header(skb);
	hdr->handle = cpu_to_le16(hci_handle_pack(handle, flags));
	hdr->dlen   = cpu_to_le16(len);
}

static void hci_queue_iso(struct hci_conn *conn, struct sk_buff_head *queue,
			  struct sk_buff *skb)
{
	struct hci_dev *hdev = conn->hdev;
	struct sk_buff *list;
	__u16 flags;

	skb->len = skb_headlen(skb);
	skb->data_len = 0;

	hci_skb_pkt_type(skb) = HCI_ISODATA_PKT;

	list = skb_shinfo(skb)->frag_list;

	flags = hci_iso_flags_pack(list ? ISO_START : ISO_SINGLE, 0x00);
	hci_add_iso_hdr(skb, conn->handle, flags);

	if (!list) {
		/* Non fragmented */
		BT_DBG("%s nonfrag skb %p len %d", hdev->name, skb, skb->len);

		skb_queue_tail(queue, skb);
	} else {
		/* Fragmented */
		BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

		skb_shinfo(skb)->frag_list = NULL;

		__skb_queue_tail(queue, skb);

		do {
			skb = list; list = list->next;

			hci_skb_pkt_type(skb) = HCI_ISODATA_PKT;
			flags = hci_iso_flags_pack(list ? ISO_CONT : ISO_END,
						   0x00);
			hci_add_iso_hdr(skb, conn->handle, flags);

			BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

			__skb_queue_tail(queue, skb);
		} while (list);
	}
}

void hci_send_iso(struct hci_conn *conn, struct sk_buff *skb)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("%s len %d", hdev->name, skb->len);

	hci_queue_iso(conn, &conn->data_q, skb);

	queue_work(hdev->workqueue, &hdev->tx_work);
}

/* ---- HCI TX task (outgoing data) ---- */

/* HCI Connection scheduler */
static inline void hci_quote_sent(struct hci_conn *conn, int num, int *quote)
{
	struct hci_dev *hdev;
	int cnt, q;

	if (!conn) {
		*quote = 0;
		return;
	}

	hdev = conn->hdev;

	switch (conn->type) {
	case ACL_LINK:
		cnt = hdev->acl_cnt;
		break;
	case AMP_LINK:
		cnt = hdev->block_cnt;
		break;
	case SCO_LINK:
	case ESCO_LINK:
		cnt = hdev->sco_cnt;
		break;
	case LE_LINK:
		cnt = hdev->le_mtu ? hdev->le_cnt : hdev->acl_cnt;
		break;
	case ISO_LINK:
		cnt = hdev->iso_mtu ? hdev->iso_cnt :
			hdev->le_mtu ? hdev->le_cnt : hdev->acl_cnt;
		break;
	default:
		cnt = 0;
		bt_dev_err(hdev, "unknown link type %d", conn->type);
	}

	q = cnt / num;
	*quote = q ? q : 1;
}

static struct hci_conn *hci_low_sent(struct hci_dev *hdev, __u8 type,
				     int *quote)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn *conn = NULL, *c;
	unsigned int num = 0, min = ~0;

	/* We don't have to lock device here. Connections are always
	 * added and removed with TX task disabled. */

	rcu_read_lock();

	list_for_each_entry_rcu(c, &h->list, list) {
		if (c->type != type || skb_queue_empty(&c->data_q))
			continue;

		if (c->state != BT_CONNECTED && c->state != BT_CONFIG)
			continue;

		num++;

		if (c->sent < min) {
			min  = c->sent;
			conn = c;
		}

		if (hci_conn_num(hdev, type) == num)
			break;
	}

	rcu_read_unlock();

	hci_quote_sent(conn, num, quote);

	BT_DBG("conn %p quote %d", conn, *quote);
	return conn;
}

static void hci_link_tx_to(struct hci_dev *hdev, __u8 type)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn *c;

	bt_dev_err(hdev, "link tx timeout");

	rcu_read_lock();

	/* Kill stalled connections */
	list_for_each_entry_rcu(c, &h->list, list) {
		if (c->type == type && c->sent) {
			bt_dev_err(hdev, "killing stalled connection %pMR",
				   &c->dst);
			hci_disconnect(c, HCI_ERROR_REMOTE_USER_TERM);
		}
	}

	rcu_read_unlock();
}

static struct hci_chan *hci_chan_sent(struct hci_dev *hdev, __u8 type,
				      int *quote)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_chan *chan = NULL;
	unsigned int num = 0, min = ~0, cur_prio = 0;
	struct hci_conn *conn;
	int conn_num = 0;

	BT_DBG("%s", hdev->name);

	rcu_read_lock();

	list_for_each_entry_rcu(conn, &h->list, list) {
		struct hci_chan *tmp;

		if (conn->type != type)
			continue;

		if (conn->state != BT_CONNECTED && conn->state != BT_CONFIG)
			continue;

		conn_num++;

		list_for_each_entry_rcu(tmp, &conn->chan_list, list) {
			struct sk_buff *skb;

			if (skb_queue_empty(&tmp->data_q))
				continue;

			skb = skb_peek(&tmp->data_q);
			if (skb->priority < cur_prio)
				continue;

			if (skb->priority > cur_prio) {
				num = 0;
				min = ~0;
				cur_prio = skb->priority;
			}

			num++;

			if (conn->sent < min) {
				min  = conn->sent;
				chan = tmp;
			}
		}

		if (hci_conn_num(hdev, type) == conn_num)
			break;
	}

	rcu_read_unlock();

	if (!chan)
		return NULL;

	hci_quote_sent(chan->conn, num, quote);

	BT_DBG("chan %p quote %d", chan, *quote);
	return chan;
}

static void hci_prio_recalculate(struct hci_dev *hdev, __u8 type)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn *conn;
	int num = 0;

	BT_DBG("%s", hdev->name);

	rcu_read_lock();

	list_for_each_entry_rcu(conn, &h->list, list) {
		struct hci_chan *chan;

		if (conn->type != type)
			continue;

		if (conn->state != BT_CONNECTED && conn->state != BT_CONFIG)
			continue;

		num++;

		list_for_each_entry_rcu(chan, &conn->chan_list, list) {
			struct sk_buff *skb;

			if (chan->sent) {
				chan->sent = 0;
				continue;
			}

			if (skb_queue_empty(&chan->data_q))
				continue;

			skb = skb_peek(&chan->data_q);
			if (skb->priority >= HCI_PRIO_MAX - 1)
				continue;

			skb->priority = HCI_PRIO_MAX - 1;

			BT_DBG("chan %p skb %p promoted to %d", chan, skb,
			       skb->priority);
		}

		if (hci_conn_num(hdev, type) == num)
			break;
	}

	rcu_read_unlock();

}

static inline int __get_blocks(struct hci_dev *hdev, struct sk_buff *skb)
{
	/* Calculate count of blocks used by this packet */
	return DIV_ROUND_UP(skb->len - HCI_ACL_HDR_SIZE, hdev->block_len);
}

static void __check_timeout(struct hci_dev *hdev, unsigned int cnt, u8 type)
{
	unsigned long last_tx;

	if (hci_dev_test_flag(hdev, HCI_UNCONFIGURED))
		return;

	switch (type) {
	case LE_LINK:
		last_tx = hdev->le_last_tx;
		break;
	default:
		last_tx = hdev->acl_last_tx;
		break;
	}

	/* tx timeout must be longer than maximum link supervision timeout
	 * (40.9 seconds)
	 */
	if (!cnt && time_after(jiffies, last_tx + HCI_ACL_TX_TIMEOUT))
		hci_link_tx_to(hdev, type);
}

/* Schedule SCO */
static void hci_sched_sco(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	struct sk_buff *skb;
	int quote;

	BT_DBG("%s", hdev->name);

	if (!hci_conn_num(hdev, SCO_LINK))
		return;

	while (hdev->sco_cnt && (conn = hci_low_sent(hdev, SCO_LINK, &quote))) {
		while (quote-- && (skb = skb_dequeue(&conn->data_q))) {
			BT_DBG("skb %p len %d", skb, skb->len);
			hci_send_frame(hdev, skb);

			conn->sent++;
			if (conn->sent == ~0)
				conn->sent = 0;
		}
	}
}

static void hci_sched_esco(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	struct sk_buff *skb;
	int quote;

	BT_DBG("%s", hdev->name);

	if (!hci_conn_num(hdev, ESCO_LINK))
		return;

	while (hdev->sco_cnt && (conn = hci_low_sent(hdev, ESCO_LINK,
						     &quote))) {
		while (quote-- && (skb = skb_dequeue(&conn->data_q))) {
			BT_DBG("skb %p len %d", skb, skb->len);
			hci_send_frame(hdev, skb);

			conn->sent++;
			if (conn->sent == ~0)
				conn->sent = 0;
		}
	}
}

static void hci_sched_acl_pkt(struct hci_dev *hdev)
{
	unsigned int cnt = hdev->acl_cnt;
	struct hci_chan *chan;
	struct sk_buff *skb;
	int quote;

	__check_timeout(hdev, cnt, ACL_LINK);

	while (hdev->acl_cnt &&
	       (chan = hci_chan_sent(hdev, ACL_LINK, &quote))) {
		u32 priority = (skb_peek(&chan->data_q))->priority;
		while (quote-- && (skb = skb_peek(&chan->data_q))) {
			BT_DBG("chan %p skb %p len %d priority %u", chan, skb,
			       skb->len, skb->priority);

			/* Stop if priority has changed */
			if (skb->priority < priority)
				break;

			skb = skb_dequeue(&chan->data_q);

			hci_conn_enter_active_mode(chan->conn,
						   bt_cb(skb)->force_active);

			hci_send_frame(hdev, skb);
			hdev->acl_last_tx = jiffies;

			hdev->acl_cnt--;
			chan->sent++;
			chan->conn->sent++;

			/* Send pending SCO packets right away */
			hci_sched_sco(hdev);
			hci_sched_esco(hdev);
		}
	}

	if (cnt != hdev->acl_cnt)
		hci_prio_recalculate(hdev, ACL_LINK);
}

static void hci_sched_acl_blk(struct hci_dev *hdev)
{
	unsigned int cnt = hdev->block_cnt;
	struct hci_chan *chan;
	struct sk_buff *skb;
	int quote;
	u8 type;

	BT_DBG("%s", hdev->name);

	if (hdev->dev_type == HCI_AMP)
		type = AMP_LINK;
	else
		type = ACL_LINK;

	__check_timeout(hdev, cnt, type);

	while (hdev->block_cnt > 0 &&
	       (chan = hci_chan_sent(hdev, type, &quote))) {
		u32 priority = (skb_peek(&chan->data_q))->priority;
		while (quote > 0 && (skb = skb_peek(&chan->data_q))) {
			int blocks;

			BT_DBG("chan %p skb %p len %d priority %u", chan, skb,
			       skb->len, skb->priority);

			/* Stop if priority has changed */
			if (skb->priority < priority)
				break;

			skb = skb_dequeue(&chan->data_q);

			blocks = __get_blocks(hdev, skb);
			if (blocks > hdev->block_cnt)
				return;

			hci_conn_enter_active_mode(chan->conn,
						   bt_cb(skb)->force_active);

			hci_send_frame(hdev, skb);
			hdev->acl_last_tx = jiffies;

			hdev->block_cnt -= blocks;
			quote -= blocks;

			chan->sent += blocks;
			chan->conn->sent += blocks;
		}
	}

	if (cnt != hdev->block_cnt)
		hci_prio_recalculate(hdev, type);
}

static void hci_sched_acl(struct hci_dev *hdev)
{
	BT_DBG("%s", hdev->name);

	/* No ACL link over BR/EDR controller */
	if (!hci_conn_num(hdev, ACL_LINK) && hdev->dev_type == HCI_PRIMARY)
		return;

	/* No AMP link over AMP controller */
	if (!hci_conn_num(hdev, AMP_LINK) && hdev->dev_type == HCI_AMP)
		return;

	switch (hdev->flow_ctl_mode) {
	case HCI_FLOW_CTL_MODE_PACKET_BASED:
		hci_sched_acl_pkt(hdev);
		break;

	case HCI_FLOW_CTL_MODE_BLOCK_BASED:
		hci_sched_acl_blk(hdev);
		break;
	}
}

static void hci_sched_le(struct hci_dev *hdev)
{
	struct hci_chan *chan;
	struct sk_buff *skb;
	int quote, cnt, tmp;

	BT_DBG("%s", hdev->name);

	if (!hci_conn_num(hdev, LE_LINK))
		return;

	cnt = hdev->le_pkts ? hdev->le_cnt : hdev->acl_cnt;

	__check_timeout(hdev, cnt, LE_LINK);

	tmp = cnt;
	while (cnt && (chan = hci_chan_sent(hdev, LE_LINK, &quote))) {
		u32 priority = (skb_peek(&chan->data_q))->priority;
		while (quote-- && (skb = skb_peek(&chan->data_q))) {
			BT_DBG("chan %p skb %p len %d priority %u", chan, skb,
			       skb->len, skb->priority);

			/* Stop if priority has changed */
			if (skb->priority < priority)
				break;

			skb = skb_dequeue(&chan->data_q);

			hci_send_frame(hdev, skb);
			hdev->le_last_tx = jiffies;

			cnt--;
			chan->sent++;
			chan->conn->sent++;

			/* Send pending SCO packets right away */
			hci_sched_sco(hdev);
			hci_sched_esco(hdev);
		}
	}

	if (hdev->le_pkts)
		hdev->le_cnt = cnt;
	else
		hdev->acl_cnt = cnt;

	if (cnt != tmp)
		hci_prio_recalculate(hdev, LE_LINK);
}

/* Schedule CIS */
static void hci_sched_iso(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	struct sk_buff *skb;
	int quote, *cnt;

	BT_DBG("%s", hdev->name);

	if (!hci_conn_num(hdev, ISO_LINK))
		return;

	cnt = hdev->iso_pkts ? &hdev->iso_cnt :
		hdev->le_pkts ? &hdev->le_cnt : &hdev->acl_cnt;
	while (*cnt && (conn = hci_low_sent(hdev, ISO_LINK, &quote))) {
		while (quote-- && (skb = skb_dequeue(&conn->data_q))) {
			BT_DBG("skb %p len %d", skb, skb->len);
			hci_send_frame(hdev, skb);

			conn->sent++;
			if (conn->sent == ~0)
				conn->sent = 0;
			(*cnt)--;
		}
	}
}

static void hci_tx_work(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, tx_work);
	struct sk_buff *skb;

	BT_DBG("%s acl %d sco %d le %d iso %d", hdev->name, hdev->acl_cnt,
	       hdev->sco_cnt, hdev->le_cnt, hdev->iso_cnt);

	if (!hci_dev_test_flag(hdev, HCI_USER_CHANNEL)) {
		/* Schedule queues and send stuff to HCI driver */
		hci_sched_sco(hdev);
		hci_sched_esco(hdev);
		hci_sched_iso(hdev);
		hci_sched_acl(hdev);
		hci_sched_le(hdev);
	}

	/* Send next queued raw (unknown type) packet */
	while ((skb = skb_dequeue(&hdev->raw_q)))
		hci_send_frame(hdev, skb);
}

/* ----- HCI RX task (incoming data processing) ----- */

/* ACL data packet */
static void hci_acldata_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_acl_hdr *hdr = (void *) skb->data;
	struct hci_conn *conn;
	__u16 handle, flags;

	skb_pull(skb, HCI_ACL_HDR_SIZE);

	handle = __le16_to_cpu(hdr->handle);
	flags  = hci_flags(handle);
	handle = hci_handle(handle);

	BT_DBG("%s len %d handle 0x%4.4x flags 0x%4.4x", hdev->name, skb->len,
	       handle, flags);

	hdev->stat.acl_rx++;

	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_handle(hdev, handle);
	hci_dev_unlock(hdev);

	if (conn) {
		hci_conn_enter_active_mode(conn, BT_POWER_FORCE_ACTIVE_OFF);

		/* Send to upper protocol */
		l2cap_recv_acldata(conn, skb, flags);
		return;
	} else {
		bt_dev_err(hdev, "ACL packet for unknown connection handle %d",
			   handle);
	}

	kfree_skb(skb);
}

/* SCO data packet */
static void hci_scodata_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_sco_hdr *hdr = (void *) skb->data;
	struct hci_conn *conn;
	__u16 handle, flags;

	skb_pull(skb, HCI_SCO_HDR_SIZE);

	handle = __le16_to_cpu(hdr->handle);
	flags  = hci_flags(handle);
	handle = hci_handle(handle);

	BT_DBG("%s len %d handle 0x%4.4x flags 0x%4.4x", hdev->name, skb->len,
	       handle, flags);

	hdev->stat.sco_rx++;

	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_handle(hdev, handle);
	hci_dev_unlock(hdev);

	if (conn) {
		/* Send to upper protocol */
		bt_cb(skb)->sco.pkt_status = flags & 0x03;
		sco_recv_scodata(conn, skb);
		return;
	} else {
		bt_dev_err_ratelimited(hdev, "SCO packet for unknown connection handle %d",
				       handle);
	}

	kfree_skb(skb);
}

static void hci_isodata_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_iso_hdr *hdr;
	struct hci_conn *conn;
	__u16 handle, flags;

	hdr = skb_pull_data(skb, sizeof(*hdr));
	if (!hdr) {
		bt_dev_err(hdev, "ISO packet too small");
		goto drop;
	}

	handle = __le16_to_cpu(hdr->handle);
	flags  = hci_flags(handle);
	handle = hci_handle(handle);

	bt_dev_dbg(hdev, "len %d handle 0x%4.4x flags 0x%4.4x", skb->len,
		   handle, flags);

	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_handle(hdev, handle);
	hci_dev_unlock(hdev);

	if (!conn) {
		bt_dev_err(hdev, "ISO packet for unknown connection handle %d",
			   handle);
		goto drop;
	}

	/* Send to upper protocol */
	iso_recv(conn, skb, flags);
	return;

drop:
	kfree_skb(skb);
}

static bool hci_req_is_complete(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = skb_peek(&hdev->cmd_q);
	if (!skb)
		return true;

	return (bt_cb(skb)->hci.req_flags & HCI_REQ_START);
}

static void hci_resend_last(struct hci_dev *hdev)
{
	struct hci_command_hdr *sent;
	struct sk_buff *skb;
	u16 opcode;

	if (!hdev->sent_cmd)
		return;

	sent = (void *) hdev->sent_cmd->data;
	opcode = __le16_to_cpu(sent->opcode);
	if (opcode == HCI_OP_RESET)
		return;

	skb = skb_clone(hdev->sent_cmd, GFP_KERNEL);
	if (!skb)
		return;

	skb_queue_head(&hdev->cmd_q, skb);
	queue_work(hdev->workqueue, &hdev->cmd_work);
}

void hci_req_cmd_complete(struct hci_dev *hdev, u16 opcode, u8 status,
			  hci_req_complete_t *req_complete,
			  hci_req_complete_skb_t *req_complete_skb)
{
	struct sk_buff *skb;
	unsigned long flags;

	BT_DBG("opcode 0x%04x status 0x%02x", opcode, status);

	/* If the completed command doesn't match the last one that was
	 * sent we need to do special handling of it.
	 */
	if (!hci_sent_cmd_data(hdev, opcode)) {
		/* Some CSR based controllers generate a spontaneous
		 * reset complete event during init and any pending
		 * command will never be completed. In such a case we
		 * need to resend whatever was the last sent
		 * command.
		 */
		if (test_bit(HCI_INIT, &hdev->flags) && opcode == HCI_OP_RESET)
			hci_resend_last(hdev);

		return;
	}

	/* If we reach this point this event matches the last command sent */
	hci_dev_clear_flag(hdev, HCI_CMD_PENDING);

	/* If the command succeeded and there's still more commands in
	 * this request the request is not yet complete.
	 */
	if (!status && !hci_req_is_complete(hdev))
		return;

	/* If this was the last command in a request the complete
	 * callback would be found in hdev->sent_cmd instead of the
	 * command queue (hdev->cmd_q).
	 */
	if (bt_cb(hdev->sent_cmd)->hci.req_flags & HCI_REQ_SKB) {
		*req_complete_skb = bt_cb(hdev->sent_cmd)->hci.req_complete_skb;
		return;
	}

	if (bt_cb(hdev->sent_cmd)->hci.req_complete) {
		*req_complete = bt_cb(hdev->sent_cmd)->hci.req_complete;
		return;
	}

	/* Remove all pending commands belonging to this request */
	spin_lock_irqsave(&hdev->cmd_q.lock, flags);
	while ((skb = __skb_dequeue(&hdev->cmd_q))) {
		if (bt_cb(skb)->hci.req_flags & HCI_REQ_START) {
			__skb_queue_head(&hdev->cmd_q, skb);
			break;
		}

		if (bt_cb(skb)->hci.req_flags & HCI_REQ_SKB)
			*req_complete_skb = bt_cb(skb)->hci.req_complete_skb;
		else
			*req_complete = bt_cb(skb)->hci.req_complete;
		dev_kfree_skb_irq(skb);
	}
	spin_unlock_irqrestore(&hdev->cmd_q.lock, flags);
}

static void hci_rx_work(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, rx_work);
	struct sk_buff *skb;

	BT_DBG("%s", hdev->name);

	/* The kcov_remote functions used for collecting packet parsing
	 * coverage information from this background thread and associate
	 * the coverage with the syscall's thread which originally injected
	 * the packet. This helps fuzzing the kernel.
	 */
	for (; (skb = skb_dequeue(&hdev->rx_q)); kcov_remote_stop()) {
		kcov_remote_start_common(skb_get_kcov_handle(skb));

		/* Send copy to monitor */
		hci_send_to_monitor(hdev, skb);

		if (atomic_read(&hdev->promisc)) {
			/* Send copy to the sockets */
			hci_send_to_sock(hdev, skb);
		}

		/* If the device has been opened in HCI_USER_CHANNEL,
		 * the userspace has exclusive access to device.
		 * When device is HCI_INIT, we still need to process
		 * the data packets to the driver in order
		 * to complete its setup().
		 */
		if (hci_dev_test_flag(hdev, HCI_USER_CHANNEL) &&
		    !test_bit(HCI_INIT, &hdev->flags)) {
			kfree_skb(skb);
			continue;
		}

		if (test_bit(HCI_INIT, &hdev->flags)) {
			/* Don't process data packets in this states. */
			switch (hci_skb_pkt_type(skb)) {
			case HCI_ACLDATA_PKT:
			case HCI_SCODATA_PKT:
			case HCI_ISODATA_PKT:
				kfree_skb(skb);
				continue;
			}
		}

		/* Process frame */
		switch (hci_skb_pkt_type(skb)) {
		case HCI_EVENT_PKT:
			BT_DBG("%s Event packet", hdev->name);
			hci_event_packet(hdev, skb);
			break;

		case HCI_ACLDATA_PKT:
			BT_DBG("%s ACL data packet", hdev->name);
			hci_acldata_packet(hdev, skb);
			break;

		case HCI_SCODATA_PKT:
			BT_DBG("%s SCO data packet", hdev->name);
			hci_scodata_packet(hdev, skb);
			break;

		case HCI_ISODATA_PKT:
			BT_DBG("%s ISO data packet", hdev->name);
			hci_isodata_packet(hdev, skb);
			break;

		default:
			kfree_skb(skb);
			break;
		}
	}
}

static void hci_cmd_work(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, cmd_work);
	struct sk_buff *skb;

	BT_DBG("%s cmd_cnt %d cmd queued %d", hdev->name,
	       atomic_read(&hdev->cmd_cnt), skb_queue_len(&hdev->cmd_q));

	/* Send queued commands */
	if (atomic_read(&hdev->cmd_cnt)) {
		skb = skb_dequeue(&hdev->cmd_q);
		if (!skb)
			return;

		kfree_skb(hdev->sent_cmd);

		hdev->sent_cmd = skb_clone(skb, GFP_KERNEL);
		if (hdev->sent_cmd) {
			int res;
			if (hci_req_status_pend(hdev))
				hci_dev_set_flag(hdev, HCI_CMD_PENDING);
			atomic_dec(&hdev->cmd_cnt);

			res = hci_send_frame(hdev, skb);
			if (res < 0)
				__hci_cmd_sync_cancel(hdev, -res);

			rcu_read_lock();
			if (test_bit(HCI_RESET, &hdev->flags) ||
			    hci_dev_test_flag(hdev, HCI_CMD_DRAIN_WORKQUEUE))
				cancel_delayed_work(&hdev->cmd_timer);
			else
				queue_delayed_work(hdev->workqueue, &hdev->cmd_timer,
						   HCI_CMD_TIMEOUT);
			rcu_read_unlock();
		} else {
			skb_queue_head(&hdev->cmd_q, skb);
			queue_work(hdev->workqueue, &hdev->cmd_work);
		}
	}
}
