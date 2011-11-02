/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

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

#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/kmod.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/rfkill.h>
#include <linux/timer.h>
#include <linux/crypto.h>
#include <net/sock.h>

#include <asm/system.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#define AUTO_OFF_TIMEOUT 2000

static void hci_cmd_task(unsigned long arg);
static void hci_rx_task(unsigned long arg);
static void hci_tx_task(unsigned long arg);

static DEFINE_RWLOCK(hci_task_lock);

/* HCI device list */
LIST_HEAD(hci_dev_list);
DEFINE_RWLOCK(hci_dev_list_lock);

/* HCI callback list */
LIST_HEAD(hci_cb_list);
DEFINE_RWLOCK(hci_cb_list_lock);

/* HCI protocols */
#define HCI_MAX_PROTO	2
struct hci_proto *hci_proto[HCI_MAX_PROTO];

/* HCI notifiers list */
static ATOMIC_NOTIFIER_HEAD(hci_notifier);

/* ---- HCI notifications ---- */

int hci_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&hci_notifier, nb);
}

int hci_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&hci_notifier, nb);
}

static void hci_notify(struct hci_dev *hdev, int event)
{
	atomic_notifier_call_chain(&hci_notifier, event, hdev);
}

/* ---- HCI requests ---- */

void hci_req_complete(struct hci_dev *hdev, __u16 cmd, int result)
{
	BT_DBG("%s command 0x%04x result 0x%2.2x", hdev->name, cmd, result);

	/* If this is the init phase check if the completed command matches
	 * the last init command, and if not just return.
	 */
	if (test_bit(HCI_INIT, &hdev->flags) && hdev->init_last_cmd != cmd)
		return;

	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = result;
		hdev->req_status = HCI_REQ_DONE;
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

static void hci_req_cancel(struct hci_dev *hdev, int err)
{
	BT_DBG("%s err 0x%2.2x", hdev->name, err);

	if (hdev->req_status == HCI_REQ_PEND) {
		hdev->req_result = err;
		hdev->req_status = HCI_REQ_CANCELED;
		wake_up_interruptible(&hdev->req_wait_q);
	}
}

/* Execute request and wait for completion. */
static int __hci_request(struct hci_dev *hdev, void (*req)(struct hci_dev *hdev, unsigned long opt),
					unsigned long opt, __u32 timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	BT_DBG("%s start", hdev->name);

	hdev->req_status = HCI_REQ_PEND;

	add_wait_queue(&hdev->req_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	req(hdev, opt);
	schedule_timeout(timeout);

	remove_wait_queue(&hdev->req_wait_q, &wait);

	if (signal_pending(current))
		return -EINTR;

	switch (hdev->req_status) {
	case HCI_REQ_DONE:
		err = -bt_to_errno(hdev->req_result);
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		break;

	default:
		err = -ETIMEDOUT;
		break;
	}

	hdev->req_status = hdev->req_result = 0;

	BT_DBG("%s end: err %d", hdev->name, err);

	return err;
}

static inline int hci_request(struct hci_dev *hdev, void (*req)(struct hci_dev *hdev, unsigned long opt),
					unsigned long opt, __u32 timeout)
{
	int ret;

	if (!test_bit(HCI_UP, &hdev->flags))
		return -ENETDOWN;

	/* Serialize all requests */
	hci_req_lock(hdev);
	ret = __hci_request(hdev, req, opt, timeout);
	hci_req_unlock(hdev);

	return ret;
}

static void hci_reset_req(struct hci_dev *hdev, unsigned long opt)
{
	BT_DBG("%s %ld", hdev->name, opt);

	/* Reset device */
	set_bit(HCI_RESET, &hdev->flags);
	hci_send_cmd(hdev, HCI_OP_RESET, 0, NULL);
}

static void hci_init_req(struct hci_dev *hdev, unsigned long opt)
{
	struct hci_cp_delete_stored_link_key cp;
	struct sk_buff *skb;
	__le16 param;
	__u8 flt_type;

	BT_DBG("%s %ld", hdev->name, opt);

	/* Driver initialization */

	/* Special commands */
	while ((skb = skb_dequeue(&hdev->driver_init))) {
		bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;
		skb->dev = (void *) hdev;

		skb_queue_tail(&hdev->cmd_q, skb);
		tasklet_schedule(&hdev->cmd_task);
	}
	skb_queue_purge(&hdev->driver_init);

	/* Mandatory initialization */

	/* Reset */
	if (!test_bit(HCI_QUIRK_NO_RESET, &hdev->quirks)) {
			set_bit(HCI_RESET, &hdev->flags);
			hci_send_cmd(hdev, HCI_OP_RESET, 0, NULL);
	}

	/* Read Local Supported Features */
	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_FEATURES, 0, NULL);

	/* Read Local Version */
	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL);

	/* Read Buffer Size (ACL mtu, max pkt, etc.) */
	hci_send_cmd(hdev, HCI_OP_READ_BUFFER_SIZE, 0, NULL);

#if 0
	/* Host buffer size */
	{
		struct hci_cp_host_buffer_size cp;
		cp.acl_mtu = cpu_to_le16(HCI_MAX_ACL_SIZE);
		cp.sco_mtu = HCI_MAX_SCO_SIZE;
		cp.acl_max_pkt = cpu_to_le16(0xffff);
		cp.sco_max_pkt = cpu_to_le16(0xffff);
		hci_send_cmd(hdev, HCI_OP_HOST_BUFFER_SIZE, sizeof(cp), &cp);
	}
#endif

	/* Read BD Address */
	hci_send_cmd(hdev, HCI_OP_READ_BD_ADDR, 0, NULL);

	/* Read Class of Device */
	hci_send_cmd(hdev, HCI_OP_READ_CLASS_OF_DEV, 0, NULL);

	/* Read Local Name */
	hci_send_cmd(hdev, HCI_OP_READ_LOCAL_NAME, 0, NULL);

	/* Read Voice Setting */
	hci_send_cmd(hdev, HCI_OP_READ_VOICE_SETTING, 0, NULL);

	/* Optional initialization */

	/* Clear Event Filters */
	flt_type = HCI_FLT_CLEAR_ALL;
	hci_send_cmd(hdev, HCI_OP_SET_EVENT_FLT, 1, &flt_type);

	/* Connection accept timeout ~20 secs */
	param = cpu_to_le16(0x7d00);
	hci_send_cmd(hdev, HCI_OP_WRITE_CA_TIMEOUT, 2, &param);

	bacpy(&cp.bdaddr, BDADDR_ANY);
	cp.delete_all = 1;
	hci_send_cmd(hdev, HCI_OP_DELETE_STORED_LINK_KEY, sizeof(cp), &cp);
}

static void hci_le_init_req(struct hci_dev *hdev, unsigned long opt)
{
	BT_DBG("%s", hdev->name);

	/* Read LE buffer size */
	hci_send_cmd(hdev, HCI_OP_LE_READ_BUFFER_SIZE, 0, NULL);
}

static void hci_scan_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 scan = opt;

	BT_DBG("%s %x", hdev->name, scan);

	/* Inquiry and Page scans */
	hci_send_cmd(hdev, HCI_OP_WRITE_SCAN_ENABLE, 1, &scan);
}

static void hci_auth_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 auth = opt;

	BT_DBG("%s %x", hdev->name, auth);

	/* Authentication */
	hci_send_cmd(hdev, HCI_OP_WRITE_AUTH_ENABLE, 1, &auth);
}

static void hci_encrypt_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 encrypt = opt;

	BT_DBG("%s %x", hdev->name, encrypt);

	/* Encryption */
	hci_send_cmd(hdev, HCI_OP_WRITE_ENCRYPT_MODE, 1, &encrypt);
}

static void hci_linkpol_req(struct hci_dev *hdev, unsigned long opt)
{
	__le16 policy = cpu_to_le16(opt);

	BT_DBG("%s %x", hdev->name, policy);

	/* Default link policy */
	hci_send_cmd(hdev, HCI_OP_WRITE_DEF_LINK_POLICY, 2, &policy);
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
static void inquiry_cache_flush(struct hci_dev *hdev)
{
	struct inquiry_cache *cache = &hdev->inq_cache;
	struct inquiry_entry *next  = cache->list, *e;

	BT_DBG("cache %p", cache);

	cache->list = NULL;
	while ((e = next)) {
		next = e->next;
		kfree(e);
	}
}

struct inquiry_entry *hci_inquiry_cache_lookup(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct inquiry_cache *cache = &hdev->inq_cache;
	struct inquiry_entry *e;

	BT_DBG("cache %p, %s", cache, batostr(bdaddr));

	for (e = cache->list; e; e = e->next)
		if (!bacmp(&e->data.bdaddr, bdaddr))
			break;
	return e;
}

void hci_inquiry_cache_update(struct hci_dev *hdev, struct inquiry_data *data)
{
	struct inquiry_cache *cache = &hdev->inq_cache;
	struct inquiry_entry *ie;

	BT_DBG("cache %p, %s", cache, batostr(&data->bdaddr));

	ie = hci_inquiry_cache_lookup(hdev, &data->bdaddr);
	if (!ie) {
		/* Entry not in the cache. Add new one. */
		ie = kzalloc(sizeof(struct inquiry_entry), GFP_ATOMIC);
		if (!ie)
			return;

		ie->next = cache->list;
		cache->list = ie;
	}

	memcpy(&ie->data, data, sizeof(*data));
	ie->timestamp = jiffies;
	cache->timestamp = jiffies;
}

static int inquiry_cache_dump(struct hci_dev *hdev, int num, __u8 *buf)
{
	struct inquiry_cache *cache = &hdev->inq_cache;
	struct inquiry_info *info = (struct inquiry_info *) buf;
	struct inquiry_entry *e;
	int copied = 0;

	for (e = cache->list; e && copied < num; e = e->next, copied++) {
		struct inquiry_data *data = &e->data;
		bacpy(&info->bdaddr, &data->bdaddr);
		info->pscan_rep_mode	= data->pscan_rep_mode;
		info->pscan_period_mode	= data->pscan_period_mode;
		info->pscan_mode	= data->pscan_mode;
		memcpy(info->dev_class, data->dev_class, 3);
		info->clock_offset	= data->clock_offset;
		info++;
	}

	BT_DBG("cache %p, copied %d", cache, copied);
	return copied;
}

static void hci_inq_req(struct hci_dev *hdev, unsigned long opt)
{
	struct hci_inquiry_req *ir = (struct hci_inquiry_req *) opt;
	struct hci_cp_inquiry cp;

	BT_DBG("%s", hdev->name);

	if (test_bit(HCI_INQUIRY, &hdev->flags))
		return;

	/* Start Inquiry */
	memcpy(&cp.lap, &ir->lap, 3);
	cp.length  = ir->length;
	cp.num_rsp = ir->num_rsp;
	hci_send_cmd(hdev, HCI_OP_INQUIRY, sizeof(cp), &cp);
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

	hci_dev_lock_bh(hdev);
	if (inquiry_cache_age(hdev) > INQUIRY_CACHE_AGE_MAX ||
				inquiry_cache_empty(hdev) ||
				ir.flags & IREQ_CACHE_FLUSH) {
		inquiry_cache_flush(hdev);
		do_inquiry = 1;
	}
	hci_dev_unlock_bh(hdev);

	timeo = ir.length * msecs_to_jiffies(2000);

	if (do_inquiry) {
		err = hci_request(hdev, hci_inq_req, (unsigned long)&ir, timeo);
		if (err < 0)
			goto done;
	}

	/* for unlimited number of responses we will use buffer with 255 entries */
	max_rsp = (ir.num_rsp == 0) ? 255 : ir.num_rsp;

	/* cache_dump can't sleep. Therefore we allocate temp buffer and then
	 * copy it to the user space.
	 */
	buf = kmalloc(sizeof(struct inquiry_info) * max_rsp, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto done;
	}

	hci_dev_lock_bh(hdev);
	ir.num_rsp = inquiry_cache_dump(hdev, max_rsp, buf);
	hci_dev_unlock_bh(hdev);

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

/* ---- HCI ioctl helpers ---- */

int hci_dev_open(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	BT_DBG("%s %p", hdev->name, hdev);

	hci_req_lock(hdev);

	if (hdev->rfkill && rfkill_blocked(hdev->rfkill)) {
		ret = -ERFKILL;
		goto done;
	}

	if (test_bit(HCI_UP, &hdev->flags)) {
		ret = -EALREADY;
		goto done;
	}

	if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
		set_bit(HCI_RAW, &hdev->flags);

	/* Treat all non BR/EDR controllers as raw devices for now */
	if (hdev->dev_type != HCI_BREDR)
		set_bit(HCI_RAW, &hdev->flags);

	if (hdev->open(hdev)) {
		ret = -EIO;
		goto done;
	}

	if (!test_bit(HCI_RAW, &hdev->flags)) {
		atomic_set(&hdev->cmd_cnt, 1);
		set_bit(HCI_INIT, &hdev->flags);
		hdev->init_last_cmd = 0;

		ret = __hci_request(hdev, hci_init_req, 0,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));

		if (lmp_host_le_capable(hdev))
			ret = __hci_request(hdev, hci_le_init_req, 0,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));

		clear_bit(HCI_INIT, &hdev->flags);
	}

	if (!ret) {
		hci_dev_hold(hdev);
		set_bit(HCI_UP, &hdev->flags);
		hci_notify(hdev, HCI_DEV_UP);
		if (!test_bit(HCI_SETUP, &hdev->flags))
			mgmt_powered(hdev->id, 1);
	} else {
		/* Init failed, cleanup */
		tasklet_kill(&hdev->rx_task);
		tasklet_kill(&hdev->tx_task);
		tasklet_kill(&hdev->cmd_task);

		skb_queue_purge(&hdev->cmd_q);
		skb_queue_purge(&hdev->rx_q);

		if (hdev->flush)
			hdev->flush(hdev);

		if (hdev->sent_cmd) {
			kfree_skb(hdev->sent_cmd);
			hdev->sent_cmd = NULL;
		}

		hdev->close(hdev);
		hdev->flags = 0;
	}

done:
	hci_req_unlock(hdev);
	hci_dev_put(hdev);
	return ret;
}

static int hci_dev_do_close(struct hci_dev *hdev)
{
	BT_DBG("%s %p", hdev->name, hdev);

	hci_req_cancel(hdev, ENODEV);
	hci_req_lock(hdev);

	if (!test_and_clear_bit(HCI_UP, &hdev->flags)) {
		del_timer_sync(&hdev->cmd_timer);
		hci_req_unlock(hdev);
		return 0;
	}

	/* Kill RX and TX tasks */
	tasklet_kill(&hdev->rx_task);
	tasklet_kill(&hdev->tx_task);

	hci_dev_lock_bh(hdev);
	inquiry_cache_flush(hdev);
	hci_conn_hash_flush(hdev);
	hci_dev_unlock_bh(hdev);

	hci_notify(hdev, HCI_DEV_DOWN);

	if (hdev->flush)
		hdev->flush(hdev);

	/* Reset device */
	skb_queue_purge(&hdev->cmd_q);
	atomic_set(&hdev->cmd_cnt, 1);
	if (!test_bit(HCI_RAW, &hdev->flags)) {
		set_bit(HCI_INIT, &hdev->flags);
		__hci_request(hdev, hci_reset_req, 0,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));
		clear_bit(HCI_INIT, &hdev->flags);
	}

	/* Kill cmd task */
	tasklet_kill(&hdev->cmd_task);

	/* Drop queues */
	skb_queue_purge(&hdev->rx_q);
	skb_queue_purge(&hdev->cmd_q);
	skb_queue_purge(&hdev->raw_q);

	/* Drop last sent command */
	if (hdev->sent_cmd) {
		del_timer_sync(&hdev->cmd_timer);
		kfree_skb(hdev->sent_cmd);
		hdev->sent_cmd = NULL;
	}

	/* After this point our queues are empty
	 * and no tasks are scheduled. */
	hdev->close(hdev);

	mgmt_powered(hdev->id, 0);

	/* Clear flags */
	hdev->flags = 0;

	hci_req_unlock(hdev);

	hci_dev_put(hdev);
	return 0;
}

int hci_dev_close(__u16 dev)
{
	struct hci_dev *hdev;
	int err;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;
	err = hci_dev_do_close(hdev);
	hci_dev_put(hdev);
	return err;
}

int hci_dev_reset(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	hci_req_lock(hdev);
	tasklet_disable(&hdev->tx_task);

	if (!test_bit(HCI_UP, &hdev->flags))
		goto done;

	/* Drop queues */
	skb_queue_purge(&hdev->rx_q);
	skb_queue_purge(&hdev->cmd_q);

	hci_dev_lock_bh(hdev);
	inquiry_cache_flush(hdev);
	hci_conn_hash_flush(hdev);
	hci_dev_unlock_bh(hdev);

	if (hdev->flush)
		hdev->flush(hdev);

	atomic_set(&hdev->cmd_cnt, 1);
	hdev->acl_cnt = 0; hdev->sco_cnt = 0; hdev->le_cnt = 0;

	if (!test_bit(HCI_RAW, &hdev->flags))
		ret = __hci_request(hdev, hci_reset_req, 0,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));

done:
	tasklet_enable(&hdev->tx_task);
	hci_req_unlock(hdev);
	hci_dev_put(hdev);
	return ret;
}

int hci_dev_reset_stat(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	hdev = hci_dev_get(dev);
	if (!hdev)
		return -ENODEV;

	memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));

	hci_dev_put(hdev);

	return ret;
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

	switch (cmd) {
	case HCISETAUTH:
		err = hci_request(hdev, hci_auth_req, dr.dev_opt,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));
		break;

	case HCISETENCRYPT:
		if (!lmp_encrypt_capable(hdev)) {
			err = -EOPNOTSUPP;
			break;
		}

		if (!test_bit(HCI_AUTH, &hdev->flags)) {
			/* Auth must be enabled first */
			err = hci_request(hdev, hci_auth_req, dr.dev_opt,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));
			if (err)
				break;
		}

		err = hci_request(hdev, hci_encrypt_req, dr.dev_opt,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));
		break;

	case HCISETSCAN:
		err = hci_request(hdev, hci_scan_req, dr.dev_opt,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));
		break;

	case HCISETLINKPOL:
		err = hci_request(hdev, hci_linkpol_req, dr.dev_opt,
					msecs_to_jiffies(HCI_INIT_TIMEOUT));
		break;

	case HCISETLINKMODE:
		hdev->link_mode = ((__u16) dr.dev_opt) &
					(HCI_LM_MASTER | HCI_LM_ACCEPT);
		break;

	case HCISETPTYPE:
		hdev->pkt_type = (__u16) dr.dev_opt;
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

	read_lock_bh(&hci_dev_list_lock);
	list_for_each_entry(hdev, &hci_dev_list, list) {
		hci_del_off_timer(hdev);

		if (!test_bit(HCI_MGMT, &hdev->flags))
			set_bit(HCI_PAIRABLE, &hdev->flags);

		(dr + n)->dev_id  = hdev->id;
		(dr + n)->dev_opt = hdev->flags;

		if (++n >= dev_num)
			break;
	}
	read_unlock_bh(&hci_dev_list_lock);

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
	int err = 0;

	if (copy_from_user(&di, arg, sizeof(di)))
		return -EFAULT;

	hdev = hci_dev_get(di.dev_id);
	if (!hdev)
		return -ENODEV;

	hci_del_off_timer(hdev);

	if (!test_bit(HCI_MGMT, &hdev->flags))
		set_bit(HCI_PAIRABLE, &hdev->flags);

	strcpy(di.name, hdev->name);
	di.bdaddr   = hdev->bdaddr;
	di.type     = (hdev->bus & 0x0f) | (hdev->dev_type << 4);
	di.flags    = hdev->flags;
	di.pkt_type = hdev->pkt_type;
	di.acl_mtu  = hdev->acl_mtu;
	di.acl_pkts = hdev->acl_pkts;
	di.sco_mtu  = hdev->sco_mtu;
	di.sco_pkts = hdev->sco_pkts;
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

	if (!blocked)
		return 0;

	hci_dev_do_close(hdev);

	return 0;
}

static const struct rfkill_ops hci_rfkill_ops = {
	.set_block = hci_rfkill_set_block,
};

/* Alloc HCI device */
struct hci_dev *hci_alloc_dev(void)
{
	struct hci_dev *hdev;

	hdev = kzalloc(sizeof(struct hci_dev), GFP_KERNEL);
	if (!hdev)
		return NULL;

	hci_init_sysfs(hdev);
	skb_queue_head_init(&hdev->driver_init);

	return hdev;
}
EXPORT_SYMBOL(hci_alloc_dev);

/* Free HCI device */
void hci_free_dev(struct hci_dev *hdev)
{
	skb_queue_purge(&hdev->driver_init);

	/* will free via device release */
	put_device(&hdev->dev);
}
EXPORT_SYMBOL(hci_free_dev);

static void hci_power_on(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, power_on);

	BT_DBG("%s", hdev->name);

	if (hci_dev_open(hdev->id) < 0)
		return;

	if (test_bit(HCI_AUTO_OFF, &hdev->flags))
		mod_timer(&hdev->off_timer,
				jiffies + msecs_to_jiffies(AUTO_OFF_TIMEOUT));

	if (test_and_clear_bit(HCI_SETUP, &hdev->flags))
		mgmt_index_added(hdev->id);
}

static void hci_power_off(struct work_struct *work)
{
	struct hci_dev *hdev = container_of(work, struct hci_dev, power_off);

	BT_DBG("%s", hdev->name);

	hci_dev_close(hdev->id);
}

static void hci_auto_off(unsigned long data)
{
	struct hci_dev *hdev = (struct hci_dev *) data;

	BT_DBG("%s", hdev->name);

	clear_bit(HCI_AUTO_OFF, &hdev->flags);

	queue_work(hdev->workqueue, &hdev->power_off);
}

void hci_del_off_timer(struct hci_dev *hdev)
{
	BT_DBG("%s", hdev->name);

	clear_bit(HCI_AUTO_OFF, &hdev->flags);
	del_timer(&hdev->off_timer);
}

int hci_uuids_clear(struct hci_dev *hdev)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &hdev->uuids) {
		struct bt_uuid *uuid;

		uuid = list_entry(p, struct bt_uuid, list);

		list_del(p);
		kfree(uuid);
	}

	return 0;
}

int hci_link_keys_clear(struct hci_dev *hdev)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &hdev->link_keys) {
		struct link_key *key;

		key = list_entry(p, struct link_key, list);

		list_del(p);
		kfree(key);
	}

	return 0;
}

struct link_key *hci_find_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct link_key *k;

	list_for_each_entry(k, &hdev->link_keys, list)
		if (bacmp(bdaddr, &k->bdaddr) == 0)
			return k;

	return NULL;
}

static int hci_persistent_key(struct hci_dev *hdev, struct hci_conn *conn,
						u8 key_type, u8 old_key_type)
{
	/* Legacy key */
	if (key_type < 0x03)
		return 1;

	/* Debug keys are insecure so don't store them persistently */
	if (key_type == HCI_LK_DEBUG_COMBINATION)
		return 0;

	/* Changed combination key and there's no previous one */
	if (key_type == HCI_LK_CHANGED_COMBINATION && old_key_type == 0xff)
		return 0;

	/* Security mode 3 case */
	if (!conn)
		return 1;

	/* Neither local nor remote side had no-bonding as requirement */
	if (conn->auth_type > 0x01 && conn->remote_auth > 0x01)
		return 1;

	/* Local side had dedicated bonding as requirement */
	if (conn->auth_type == 0x02 || conn->auth_type == 0x03)
		return 1;

	/* Remote side had dedicated bonding as requirement */
	if (conn->remote_auth == 0x02 || conn->remote_auth == 0x03)
		return 1;

	/* If none of the above criteria match, then don't store the key
	 * persistently */
	return 0;
}

struct link_key *hci_find_ltk(struct hci_dev *hdev, __le16 ediv, u8 rand[8])
{
	struct link_key *k;

	list_for_each_entry(k, &hdev->link_keys, list) {
		struct key_master_id *id;

		if (k->type != HCI_LK_SMP_LTK)
			continue;

		if (k->dlen != sizeof(*id))
			continue;

		id = (void *) &k->data;
		if (id->ediv == ediv &&
				(memcmp(rand, id->rand, sizeof(id->rand)) == 0))
			return k;
	}

	return NULL;
}
EXPORT_SYMBOL(hci_find_ltk);

struct link_key *hci_find_link_key_type(struct hci_dev *hdev,
					bdaddr_t *bdaddr, u8 type)
{
	struct link_key *k;

	list_for_each_entry(k, &hdev->link_keys, list)
		if (k->type == type && bacmp(bdaddr, &k->bdaddr) == 0)
			return k;

	return NULL;
}
EXPORT_SYMBOL(hci_find_link_key_type);

int hci_add_link_key(struct hci_dev *hdev, struct hci_conn *conn, int new_key,
				bdaddr_t *bdaddr, u8 *val, u8 type, u8 pin_len)
{
	struct link_key *key, *old_key;
	u8 old_key_type, persistent;

	old_key = hci_find_link_key(hdev, bdaddr);
	if (old_key) {
		old_key_type = old_key->type;
		key = old_key;
	} else {
		old_key_type = conn ? conn->key_type : 0xff;
		key = kzalloc(sizeof(*key), GFP_ATOMIC);
		if (!key)
			return -ENOMEM;
		list_add(&key->list, &hdev->link_keys);
	}

	BT_DBG("%s key for %s type %u", hdev->name, batostr(bdaddr), type);

	/* Some buggy controller combinations generate a changed
	 * combination key for legacy pairing even when there's no
	 * previous key */
	if (type == HCI_LK_CHANGED_COMBINATION &&
					(!conn || conn->remote_auth == 0xff) &&
					old_key_type == 0xff) {
		type = HCI_LK_COMBINATION;
		if (conn)
			conn->key_type = type;
	}

	bacpy(&key->bdaddr, bdaddr);
	memcpy(key->val, val, 16);
	key->pin_len = pin_len;

	if (type == HCI_LK_CHANGED_COMBINATION)
		key->type = old_key_type;
	else
		key->type = type;

	if (!new_key)
		return 0;

	persistent = hci_persistent_key(hdev, conn, type, old_key_type);

	mgmt_new_key(hdev->id, key, persistent);

	if (!persistent) {
		list_del(&key->list);
		kfree(key);
	}

	return 0;
}

int hci_add_ltk(struct hci_dev *hdev, int new_key, bdaddr_t *bdaddr,
			u8 key_size, __le16 ediv, u8 rand[8], u8 ltk[16])
{
	struct link_key *key, *old_key;
	struct key_master_id *id;
	u8 old_key_type;

	BT_DBG("%s addr %s", hdev->name, batostr(bdaddr));

	old_key = hci_find_link_key_type(hdev, bdaddr, HCI_LK_SMP_LTK);
	if (old_key) {
		key = old_key;
		old_key_type = old_key->type;
	} else {
		key = kzalloc(sizeof(*key) + sizeof(*id), GFP_ATOMIC);
		if (!key)
			return -ENOMEM;
		list_add(&key->list, &hdev->link_keys);
		old_key_type = 0xff;
	}

	key->dlen = sizeof(*id);

	bacpy(&key->bdaddr, bdaddr);
	memcpy(key->val, ltk, sizeof(key->val));
	key->type = HCI_LK_SMP_LTK;
	key->pin_len = key_size;

	id = (void *) &key->data;
	id->ediv = ediv;
	memcpy(id->rand, rand, sizeof(id->rand));

	if (new_key)
		mgmt_new_key(hdev->id, key, old_key_type);

	return 0;
}

int hci_remove_link_key(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct link_key *key;

	key = hci_find_link_key(hdev, bdaddr);
	if (!key)
		return -ENOENT;

	BT_DBG("%s removing %s", hdev->name, batostr(bdaddr));

	list_del(&key->list);
	kfree(key);

	return 0;
}

/* HCI command timer function */
static void hci_cmd_timer(unsigned long arg)
{
	struct hci_dev *hdev = (void *) arg;

	BT_ERR("%s command tx timeout", hdev->name);
	atomic_set(&hdev->cmd_cnt, 1);
	tasklet_schedule(&hdev->cmd_task);
}

struct oob_data *hci_find_remote_oob_data(struct hci_dev *hdev,
							bdaddr_t *bdaddr)
{
	struct oob_data *data;

	list_for_each_entry(data, &hdev->remote_oob_data, list)
		if (bacmp(bdaddr, &data->bdaddr) == 0)
			return data;

	return NULL;
}

int hci_remove_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct oob_data *data;

	data = hci_find_remote_oob_data(hdev, bdaddr);
	if (!data)
		return -ENOENT;

	BT_DBG("%s removing %s", hdev->name, batostr(bdaddr));

	list_del(&data->list);
	kfree(data);

	return 0;
}

int hci_remote_oob_data_clear(struct hci_dev *hdev)
{
	struct oob_data *data, *n;

	list_for_each_entry_safe(data, n, &hdev->remote_oob_data, list) {
		list_del(&data->list);
		kfree(data);
	}

	return 0;
}

int hci_add_remote_oob_data(struct hci_dev *hdev, bdaddr_t *bdaddr, u8 *hash,
								u8 *randomizer)
{
	struct oob_data *data;

	data = hci_find_remote_oob_data(hdev, bdaddr);

	if (!data) {
		data = kmalloc(sizeof(*data), GFP_ATOMIC);
		if (!data)
			return -ENOMEM;

		bacpy(&data->bdaddr, bdaddr);
		list_add(&data->list, &hdev->remote_oob_data);
	}

	memcpy(data->hash, hash, sizeof(data->hash));
	memcpy(data->randomizer, randomizer, sizeof(data->randomizer));

	BT_DBG("%s for %s", hdev->name, batostr(bdaddr));

	return 0;
}

struct bdaddr_list *hci_blacklist_lookup(struct hci_dev *hdev,
						bdaddr_t *bdaddr)
{
	struct bdaddr_list *b;

	list_for_each_entry(b, &hdev->blacklist, list)
		if (bacmp(bdaddr, &b->bdaddr) == 0)
			return b;

	return NULL;
}

int hci_blacklist_clear(struct hci_dev *hdev)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &hdev->blacklist) {
		struct bdaddr_list *b;

		b = list_entry(p, struct bdaddr_list, list);

		list_del(p);
		kfree(b);
	}

	return 0;
}

int hci_blacklist_add(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct bdaddr_list *entry;

	if (bacmp(bdaddr, BDADDR_ANY) == 0)
		return -EBADF;

	if (hci_blacklist_lookup(hdev, bdaddr))
		return -EEXIST;

	entry = kzalloc(sizeof(struct bdaddr_list), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	bacpy(&entry->bdaddr, bdaddr);

	list_add(&entry->list, &hdev->blacklist);

	return mgmt_device_blocked(hdev->id, bdaddr);
}

int hci_blacklist_del(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct bdaddr_list *entry;

	if (bacmp(bdaddr, BDADDR_ANY) == 0) {
		return hci_blacklist_clear(hdev);
	}

	entry = hci_blacklist_lookup(hdev, bdaddr);
	if (!entry) {
		return -ENOENT;
	}

	list_del(&entry->list);
	kfree(entry);

	return mgmt_device_unblocked(hdev->id, bdaddr);
}

static void hci_clear_adv_cache(unsigned long arg)
{
	struct hci_dev *hdev = (void *) arg;

	hci_dev_lock(hdev);

	hci_adv_entries_clear(hdev);

	hci_dev_unlock(hdev);
}

int hci_adv_entries_clear(struct hci_dev *hdev)
{
	struct adv_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &hdev->adv_entries, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	BT_DBG("%s adv cache cleared", hdev->name);

	return 0;
}

struct adv_entry *hci_find_adv_entry(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct adv_entry *entry;

	list_for_each_entry(entry, &hdev->adv_entries, list)
		if (bacmp(bdaddr, &entry->bdaddr) == 0)
			return entry;

	return NULL;
}

static inline int is_connectable_adv(u8 evt_type)
{
	if (evt_type == ADV_IND || evt_type == ADV_DIRECT_IND)
		return 1;

	return 0;
}

int hci_add_adv_entry(struct hci_dev *hdev,
					struct hci_ev_le_advertising_info *ev)
{
	struct adv_entry *entry;

	if (!is_connectable_adv(ev->evt_type))
		return -EINVAL;

	/* Only new entries should be added to adv_entries. So, if
	 * bdaddr was found, don't add it. */
	if (hci_find_adv_entry(hdev, &ev->bdaddr))
		return 0;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	bacpy(&entry->bdaddr, &ev->bdaddr);
	entry->bdaddr_type = ev->bdaddr_type;

	list_add(&entry->list, &hdev->adv_entries);

	BT_DBG("%s adv entry added: address %s type %u", hdev->name,
				batostr(&entry->bdaddr), entry->bdaddr_type);

	return 0;
}

/* Register HCI device */
int hci_register_dev(struct hci_dev *hdev)
{
	struct list_head *head = &hci_dev_list, *p;
	int i, id = 0, error;

	BT_DBG("%p name %s bus %d owner %p", hdev, hdev->name,
						hdev->bus, hdev->owner);

	if (!hdev->open || !hdev->close || !hdev->destruct)
		return -EINVAL;

	write_lock_bh(&hci_dev_list_lock);

	/* Find first available device id */
	list_for_each(p, &hci_dev_list) {
		if (list_entry(p, struct hci_dev, list)->id != id)
			break;
		head = p; id++;
	}

	sprintf(hdev->name, "hci%d", id);
	hdev->id = id;
	list_add(&hdev->list, head);

	atomic_set(&hdev->refcnt, 1);
	spin_lock_init(&hdev->lock);

	hdev->flags = 0;
	hdev->pkt_type  = (HCI_DM1 | HCI_DH1 | HCI_HV1);
	hdev->esco_type = (ESCO_HV1);
	hdev->link_mode = (HCI_LM_ACCEPT);
	hdev->io_capability = 0x03; /* No Input No Output */

	hdev->idle_timeout = 0;
	hdev->sniff_max_interval = 800;
	hdev->sniff_min_interval = 80;

	tasklet_init(&hdev->cmd_task, hci_cmd_task, (unsigned long) hdev);
	tasklet_init(&hdev->rx_task, hci_rx_task, (unsigned long) hdev);
	tasklet_init(&hdev->tx_task, hci_tx_task, (unsigned long) hdev);

	skb_queue_head_init(&hdev->rx_q);
	skb_queue_head_init(&hdev->cmd_q);
	skb_queue_head_init(&hdev->raw_q);

	setup_timer(&hdev->cmd_timer, hci_cmd_timer, (unsigned long) hdev);

	for (i = 0; i < NUM_REASSEMBLY; i++)
		hdev->reassembly[i] = NULL;

	init_waitqueue_head(&hdev->req_wait_q);
	mutex_init(&hdev->req_lock);

	inquiry_cache_init(hdev);

	hci_conn_hash_init(hdev);

	INIT_LIST_HEAD(&hdev->blacklist);

	INIT_LIST_HEAD(&hdev->uuids);

	INIT_LIST_HEAD(&hdev->link_keys);

	INIT_LIST_HEAD(&hdev->remote_oob_data);

	INIT_LIST_HEAD(&hdev->adv_entries);
	setup_timer(&hdev->adv_timer, hci_clear_adv_cache,
						(unsigned long) hdev);

	INIT_WORK(&hdev->power_on, hci_power_on);
	INIT_WORK(&hdev->power_off, hci_power_off);
	setup_timer(&hdev->off_timer, hci_auto_off, (unsigned long) hdev);

	memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));

	atomic_set(&hdev->promisc, 0);

	write_unlock_bh(&hci_dev_list_lock);

	hdev->workqueue = create_singlethread_workqueue(hdev->name);
	if (!hdev->workqueue) {
		error = -ENOMEM;
		goto err;
	}

	error = hci_add_sysfs(hdev);
	if (error < 0)
		goto err_wqueue;

	hdev->rfkill = rfkill_alloc(hdev->name, &hdev->dev,
				RFKILL_TYPE_BLUETOOTH, &hci_rfkill_ops, hdev);
	if (hdev->rfkill) {
		if (rfkill_register(hdev->rfkill) < 0) {
			rfkill_destroy(hdev->rfkill);
			hdev->rfkill = NULL;
		}
	}

	set_bit(HCI_AUTO_OFF, &hdev->flags);
	set_bit(HCI_SETUP, &hdev->flags);
	queue_work(hdev->workqueue, &hdev->power_on);

	hci_notify(hdev, HCI_DEV_REG);

	return id;

err_wqueue:
	destroy_workqueue(hdev->workqueue);
err:
	write_lock_bh(&hci_dev_list_lock);
	list_del(&hdev->list);
	write_unlock_bh(&hci_dev_list_lock);

	return error;
}
EXPORT_SYMBOL(hci_register_dev);

/* Unregister HCI device */
void hci_unregister_dev(struct hci_dev *hdev)
{
	int i;

	BT_DBG("%p name %s bus %d", hdev, hdev->name, hdev->bus);

	write_lock_bh(&hci_dev_list_lock);
	list_del(&hdev->list);
	write_unlock_bh(&hci_dev_list_lock);

	hci_dev_do_close(hdev);

	for (i = 0; i < NUM_REASSEMBLY; i++)
		kfree_skb(hdev->reassembly[i]);

	if (!test_bit(HCI_INIT, &hdev->flags) &&
					!test_bit(HCI_SETUP, &hdev->flags))
		mgmt_index_removed(hdev->id);

	hci_notify(hdev, HCI_DEV_UNREG);

	if (hdev->rfkill) {
		rfkill_unregister(hdev->rfkill);
		rfkill_destroy(hdev->rfkill);
	}

	hci_del_sysfs(hdev);

	hci_del_off_timer(hdev);
	del_timer(&hdev->adv_timer);

	destroy_workqueue(hdev->workqueue);

	hci_dev_lock_bh(hdev);
	hci_blacklist_clear(hdev);
	hci_uuids_clear(hdev);
	hci_link_keys_clear(hdev);
	hci_remote_oob_data_clear(hdev);
	hci_adv_entries_clear(hdev);
	hci_dev_unlock_bh(hdev);

	__hci_dev_put(hdev);
}
EXPORT_SYMBOL(hci_unregister_dev);

/* Suspend HCI device */
int hci_suspend_dev(struct hci_dev *hdev)
{
	hci_notify(hdev, HCI_DEV_SUSPEND);
	return 0;
}
EXPORT_SYMBOL(hci_suspend_dev);

/* Resume HCI device */
int hci_resume_dev(struct hci_dev *hdev)
{
	hci_notify(hdev, HCI_DEV_RESUME);
	return 0;
}
EXPORT_SYMBOL(hci_resume_dev);

/* Receive frame from HCI drivers */
int hci_recv_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	if (!hdev || (!test_bit(HCI_UP, &hdev->flags)
				&& !test_bit(HCI_INIT, &hdev->flags))) {
		kfree_skb(skb);
		return -ENXIO;
	}

	/* Incomming skb */
	bt_cb(skb)->incoming = 1;

	/* Time stamp */
	__net_timestamp(skb);

	/* Queue frame for rx task */
	skb_queue_tail(&hdev->rx_q, skb);
	tasklet_schedule(&hdev->rx_task);

	return 0;
}
EXPORT_SYMBOL(hci_recv_frame);

static int hci_reassembly(struct hci_dev *hdev, int type, void *data,
						  int count, __u8 index)
{
	int len = 0;
	int hlen = 0;
	int remain = count;
	struct sk_buff *skb;
	struct bt_skb_cb *scb;

	if ((type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT) ||
				index >= NUM_REASSEMBLY)
		return -EILSEQ;

	skb = hdev->reassembly[index];

	if (!skb) {
		switch (type) {
		case HCI_ACLDATA_PKT:
			len = HCI_MAX_FRAME_SIZE;
			hlen = HCI_ACL_HDR_SIZE;
			break;
		case HCI_EVENT_PKT:
			len = HCI_MAX_EVENT_SIZE;
			hlen = HCI_EVENT_HDR_SIZE;
			break;
		case HCI_SCODATA_PKT:
			len = HCI_MAX_SCO_SIZE;
			hlen = HCI_SCO_HDR_SIZE;
			break;
		}

		skb = bt_skb_alloc(len, GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;

		scb = (void *) skb->cb;
		scb->expect = hlen;
		scb->pkt_type = type;

		skb->dev = (void *) hdev;
		hdev->reassembly[index] = skb;
	}

	while (count) {
		scb = (void *) skb->cb;
		len = min(scb->expect, (__u16)count);

		memcpy(skb_put(skb, len), data, len);

		count -= len;
		data += len;
		scb->expect -= len;
		remain = count;

		switch (type) {
		case HCI_EVENT_PKT:
			if (skb->len == HCI_EVENT_HDR_SIZE) {
				struct hci_event_hdr *h = hci_event_hdr(skb);
				scb->expect = h->plen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;

		case HCI_ACLDATA_PKT:
			if (skb->len  == HCI_ACL_HDR_SIZE) {
				struct hci_acl_hdr *h = hci_acl_hdr(skb);
				scb->expect = __le16_to_cpu(h->dlen);

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;

		case HCI_SCODATA_PKT:
			if (skb->len == HCI_SCO_HDR_SIZE) {
				struct hci_sco_hdr *h = hci_sco_hdr(skb);
				scb->expect = h->dlen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					hdev->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;
		}

		if (scb->expect == 0) {
			/* Complete frame */

			bt_cb(skb)->pkt_type = type;
			hci_recv_frame(skb);

			hdev->reassembly[index] = NULL;
			return remain;
		}
	}

	return remain;
}

int hci_recv_fragment(struct hci_dev *hdev, int type, void *data, int count)
{
	int rem = 0;

	if (type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT)
		return -EILSEQ;

	while (count) {
		rem = hci_reassembly(hdev, type, data, count, type - 1);
		if (rem < 0)
			return rem;

		data += (count - rem);
		count = rem;
	}

	return rem;
}
EXPORT_SYMBOL(hci_recv_fragment);

#define STREAM_REASSEMBLY 0

int hci_recv_stream_fragment(struct hci_dev *hdev, void *data, int count)
{
	int type;
	int rem = 0;

	while (count) {
		struct sk_buff *skb = hdev->reassembly[STREAM_REASSEMBLY];

		if (!skb) {
			struct { char type; } *pkt;

			/* Start of the frame */
			pkt = data;
			type = pkt->type;

			data++;
			count--;
		} else
			type = bt_cb(skb)->pkt_type;

		rem = hci_reassembly(hdev, type, data, count,
							STREAM_REASSEMBLY);
		if (rem < 0)
			return rem;

		data += (count - rem);
		count = rem;
	}

	return rem;
}
EXPORT_SYMBOL(hci_recv_stream_fragment);

/* ---- Interface to upper protocols ---- */

/* Register/Unregister protocols.
 * hci_task_lock is used to ensure that no tasks are running. */
int hci_register_proto(struct hci_proto *hp)
{
	int err = 0;

	BT_DBG("%p name %s id %d", hp, hp->name, hp->id);

	if (hp->id >= HCI_MAX_PROTO)
		return -EINVAL;

	write_lock_bh(&hci_task_lock);

	if (!hci_proto[hp->id])
		hci_proto[hp->id] = hp;
	else
		err = -EEXIST;

	write_unlock_bh(&hci_task_lock);

	return err;
}
EXPORT_SYMBOL(hci_register_proto);

int hci_unregister_proto(struct hci_proto *hp)
{
	int err = 0;

	BT_DBG("%p name %s id %d", hp, hp->name, hp->id);

	if (hp->id >= HCI_MAX_PROTO)
		return -EINVAL;

	write_lock_bh(&hci_task_lock);

	if (hci_proto[hp->id])
		hci_proto[hp->id] = NULL;
	else
		err = -ENOENT;

	write_unlock_bh(&hci_task_lock);

	return err;
}
EXPORT_SYMBOL(hci_unregister_proto);

int hci_register_cb(struct hci_cb *cb)
{
	BT_DBG("%p name %s", cb, cb->name);

	write_lock_bh(&hci_cb_list_lock);
	list_add(&cb->list, &hci_cb_list);
	write_unlock_bh(&hci_cb_list_lock);

	return 0;
}
EXPORT_SYMBOL(hci_register_cb);

int hci_unregister_cb(struct hci_cb *cb)
{
	BT_DBG("%p name %s", cb, cb->name);

	write_lock_bh(&hci_cb_list_lock);
	list_del(&cb->list);
	write_unlock_bh(&hci_cb_list_lock);

	return 0;
}
EXPORT_SYMBOL(hci_unregister_cb);

static int hci_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	if (!hdev) {
		kfree_skb(skb);
		return -ENODEV;
	}

	BT_DBG("%s type %d len %d", hdev->name, bt_cb(skb)->pkt_type, skb->len);

	if (atomic_read(&hdev->promisc)) {
		/* Time stamp */
		__net_timestamp(skb);

		hci_send_to_sock(hdev, skb, NULL);
	}

	/* Get rid of skb owner, prior to sending to the driver. */
	skb_orphan(skb);

	return hdev->send(skb);
}

/* Send HCI command */
int hci_send_cmd(struct hci_dev *hdev, __u16 opcode, __u32 plen, void *param)
{
	int len = HCI_COMMAND_HDR_SIZE + plen;
	struct hci_command_hdr *hdr;
	struct sk_buff *skb;

	BT_DBG("%s opcode 0x%x plen %d", hdev->name, opcode, plen);

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("%s no memory for command", hdev->name);
		return -ENOMEM;
	}

	hdr = (struct hci_command_hdr *) skb_put(skb, HCI_COMMAND_HDR_SIZE);
	hdr->opcode = cpu_to_le16(opcode);
	hdr->plen   = plen;

	if (plen)
		memcpy(skb_put(skb, plen), param, plen);

	BT_DBG("skb len %d", skb->len);

	bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;
	skb->dev = (void *) hdev;

	if (test_bit(HCI_INIT, &hdev->flags))
		hdev->init_last_cmd = opcode;

	skb_queue_tail(&hdev->cmd_q, skb);
	tasklet_schedule(&hdev->cmd_task);

	return 0;
}

/* Get data from the previously sent command */
void *hci_sent_cmd_data(struct hci_dev *hdev, __u16 opcode)
{
	struct hci_command_hdr *hdr;

	if (!hdev->sent_cmd)
		return NULL;

	hdr = (void *) hdev->sent_cmd->data;

	if (hdr->opcode != cpu_to_le16(opcode))
		return NULL;

	BT_DBG("%s opcode 0x%x", hdev->name, opcode);

	return hdev->sent_cmd->data + HCI_COMMAND_HDR_SIZE;
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

static void hci_queue_acl(struct hci_conn *conn, struct sk_buff_head *queue,
				struct sk_buff *skb, __u16 flags)
{
	struct hci_dev *hdev = conn->hdev;
	struct sk_buff *list;

	list = skb_shinfo(skb)->frag_list;
	if (!list) {
		/* Non fragmented */
		BT_DBG("%s nonfrag skb %p len %d", hdev->name, skb, skb->len);

		skb_queue_tail(queue, skb);
	} else {
		/* Fragmented */
		BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

		skb_shinfo(skb)->frag_list = NULL;

		/* Queue all fragments atomically */
		spin_lock_bh(&queue->lock);

		__skb_queue_tail(queue, skb);

		flags &= ~ACL_START;
		flags |= ACL_CONT;
		do {
			skb = list; list = list->next;

			skb->dev = (void *) hdev;
			bt_cb(skb)->pkt_type = HCI_ACLDATA_PKT;
			hci_add_acl_hdr(skb, conn->handle, flags);

			BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

			__skb_queue_tail(queue, skb);
		} while (list);

		spin_unlock_bh(&queue->lock);
	}
}

void hci_send_acl(struct hci_chan *chan, struct sk_buff *skb, __u16 flags)
{
	struct hci_conn *conn = chan->conn;
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("%s chan %p flags 0x%x", hdev->name, chan, flags);

	skb->dev = (void *) hdev;
	bt_cb(skb)->pkt_type = HCI_ACLDATA_PKT;
	hci_add_acl_hdr(skb, conn->handle, flags);

	hci_queue_acl(conn, &chan->data_q, skb, flags);

	tasklet_schedule(&hdev->tx_task);
}
EXPORT_SYMBOL(hci_send_acl);

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

	skb->dev = (void *) hdev;
	bt_cb(skb)->pkt_type = HCI_SCODATA_PKT;

	skb_queue_tail(&conn->data_q, skb);
	tasklet_schedule(&hdev->tx_task);
}
EXPORT_SYMBOL(hci_send_sco);

/* ---- HCI TX task (outgoing data) ---- */

/* HCI Connection scheduler */
static inline struct hci_conn *hci_low_sent(struct hci_dev *hdev, __u8 type, int *quote)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn *conn = NULL, *c;
	int num = 0, min = ~0;

	/* We don't have to lock device here. Connections are always
	 * added and removed with TX task disabled. */
	list_for_each_entry(c, &h->list, list) {
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

	if (conn) {
		int cnt, q;

		switch (conn->type) {
		case ACL_LINK:
			cnt = hdev->acl_cnt;
			break;
		case SCO_LINK:
		case ESCO_LINK:
			cnt = hdev->sco_cnt;
			break;
		case LE_LINK:
			cnt = hdev->le_mtu ? hdev->le_cnt : hdev->acl_cnt;
			break;
		default:
			cnt = 0;
			BT_ERR("Unknown link type");
		}

		q = cnt / num;
		*quote = q ? q : 1;
	} else
		*quote = 0;

	BT_DBG("conn %p quote %d", conn, *quote);
	return conn;
}

static inline void hci_link_tx_to(struct hci_dev *hdev, __u8 type)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn *c;

	BT_ERR("%s link tx timeout", hdev->name);

	/* Kill stalled connections */
	list_for_each_entry(c, &h->list, list) {
		if (c->type == type && c->sent) {
			BT_ERR("%s killing stalled connection %s",
				hdev->name, batostr(&c->dst));
			hci_acl_disconn(c, 0x13);
		}
	}
}

static inline struct hci_chan *hci_chan_sent(struct hci_dev *hdev, __u8 type,
						int *quote)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_chan *chan = NULL;
	int num = 0, min = ~0, cur_prio = 0;
	struct hci_conn *conn;
	int cnt, q, conn_num = 0;

	BT_DBG("%s", hdev->name);

	list_for_each_entry(conn, &h->list, list) {
		struct hci_chan_hash *ch;
		struct hci_chan *tmp;

		if (conn->type != type)
			continue;

		if (conn->state != BT_CONNECTED && conn->state != BT_CONFIG)
			continue;

		conn_num++;

		ch = &conn->chan_hash;

		list_for_each_entry(tmp, &ch->list, list) {
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

	if (!chan)
		return NULL;

	switch (chan->conn->type) {
	case ACL_LINK:
		cnt = hdev->acl_cnt;
		break;
	case SCO_LINK:
	case ESCO_LINK:
		cnt = hdev->sco_cnt;
		break;
	case LE_LINK:
		cnt = hdev->le_mtu ? hdev->le_cnt : hdev->acl_cnt;
		break;
	default:
		cnt = 0;
		BT_ERR("Unknown link type");
	}

	q = cnt / num;
	*quote = q ? q : 1;
	BT_DBG("chan %p quote %d", chan, *quote);
	return chan;
}

static inline void hci_sched_acl(struct hci_dev *hdev)
{
	struct hci_chan *chan;
	struct sk_buff *skb;
	int quote;
	unsigned int cnt;

	BT_DBG("%s", hdev->name);

	if (!hci_conn_num(hdev, ACL_LINK))
		return;

	if (!test_bit(HCI_RAW, &hdev->flags)) {
		/* ACL tx timeout must be longer than maximum
		 * link supervision timeout (40.9 seconds) */
		if (!hdev->acl_cnt && time_after(jiffies, hdev->acl_last_tx + HZ * 45))
			hci_link_tx_to(hdev, ACL_LINK);
	}

	cnt = hdev->acl_cnt;

	while (hdev->acl_cnt &&
			(chan = hci_chan_sent(hdev, ACL_LINK, &quote))) {
		while (quote-- && (skb = skb_dequeue(&chan->data_q))) {
			BT_DBG("chan %p skb %p len %d priority %u", chan, skb,
					skb->len, skb->priority);

			hci_conn_enter_active_mode(chan->conn,
						bt_cb(skb)->force_active);

			hci_send_frame(skb);
			hdev->acl_last_tx = jiffies;

			hdev->acl_cnt--;
			chan->sent++;
			chan->conn->sent++;
		}
	}
}

/* Schedule SCO */
static inline void hci_sched_sco(struct hci_dev *hdev)
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
			hci_send_frame(skb);

			conn->sent++;
			if (conn->sent == ~0)
				conn->sent = 0;
		}
	}
}

static inline void hci_sched_esco(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	struct sk_buff *skb;
	int quote;

	BT_DBG("%s", hdev->name);

	if (!hci_conn_num(hdev, ESCO_LINK))
		return;

	while (hdev->sco_cnt && (conn = hci_low_sent(hdev, ESCO_LINK, &quote))) {
		while (quote-- && (skb = skb_dequeue(&conn->data_q))) {
			BT_DBG("skb %p len %d", skb, skb->len);
			hci_send_frame(skb);

			conn->sent++;
			if (conn->sent == ~0)
				conn->sent = 0;
		}
	}
}

static inline void hci_sched_le(struct hci_dev *hdev)
{
	struct hci_chan *chan;
	struct sk_buff *skb;
	int quote, cnt;

	BT_DBG("%s", hdev->name);

	if (!hci_conn_num(hdev, LE_LINK))
		return;

	if (!test_bit(HCI_RAW, &hdev->flags)) {
		/* LE tx timeout must be longer than maximum
		 * link supervision timeout (40.9 seconds) */
		if (!hdev->le_cnt && hdev->le_pkts &&
				time_after(jiffies, hdev->le_last_tx + HZ * 45))
			hci_link_tx_to(hdev, LE_LINK);
	}

	cnt = hdev->le_pkts ? hdev->le_cnt : hdev->acl_cnt;
	while (cnt && (chan = hci_chan_sent(hdev, LE_LINK, &quote))) {
		while (quote-- && (skb = skb_dequeue(&chan->data_q))) {
			BT_DBG("chan %p skb %p len %d priority %u", chan, skb,
					skb->len, skb->priority);

			hci_send_frame(skb);
			hdev->le_last_tx = jiffies;

			cnt--;
			chan->sent++;
			chan->conn->sent++;
		}
	}

	if (hdev->le_pkts)
		hdev->le_cnt = cnt;
	else
		hdev->acl_cnt = cnt;
}

static void hci_tx_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	read_lock(&hci_task_lock);

	BT_DBG("%s acl %d sco %d le %d", hdev->name, hdev->acl_cnt,
		hdev->sco_cnt, hdev->le_cnt);

	/* Schedule queues and send stuff to HCI driver */

	hci_sched_acl(hdev);

	hci_sched_sco(hdev);

	hci_sched_esco(hdev);

	hci_sched_le(hdev);

	/* Send next queued raw (unknown type) packet */
	while ((skb = skb_dequeue(&hdev->raw_q)))
		hci_send_frame(skb);

	read_unlock(&hci_task_lock);
}

/* ----- HCI RX task (incoming data processing) ----- */

/* ACL data packet */
static inline void hci_acldata_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_acl_hdr *hdr = (void *) skb->data;
	struct hci_conn *conn;
	__u16 handle, flags;

	skb_pull(skb, HCI_ACL_HDR_SIZE);

	handle = __le16_to_cpu(hdr->handle);
	flags  = hci_flags(handle);
	handle = hci_handle(handle);

	BT_DBG("%s len %d handle 0x%x flags 0x%x", hdev->name, skb->len, handle, flags);

	hdev->stat.acl_rx++;

	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_handle(hdev, handle);
	hci_dev_unlock(hdev);

	if (conn) {
		register struct hci_proto *hp;

		hci_conn_enter_active_mode(conn, bt_cb(skb)->force_active);

		/* Send to upper protocol */
		hp = hci_proto[HCI_PROTO_L2CAP];
		if (hp && hp->recv_acldata) {
			hp->recv_acldata(conn, skb, flags);
			return;
		}
	} else {
		BT_ERR("%s ACL packet for unknown connection handle %d",
			hdev->name, handle);
	}

	kfree_skb(skb);
}

/* SCO data packet */
static inline void hci_scodata_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_sco_hdr *hdr = (void *) skb->data;
	struct hci_conn *conn;
	__u16 handle;

	skb_pull(skb, HCI_SCO_HDR_SIZE);

	handle = __le16_to_cpu(hdr->handle);

	BT_DBG("%s len %d handle 0x%x", hdev->name, skb->len, handle);

	hdev->stat.sco_rx++;

	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_handle(hdev, handle);
	hci_dev_unlock(hdev);

	if (conn) {
		register struct hci_proto *hp;

		/* Send to upper protocol */
		hp = hci_proto[HCI_PROTO_SCO];
		if (hp && hp->recv_scodata) {
			hp->recv_scodata(conn, skb);
			return;
		}
	} else {
		BT_ERR("%s SCO packet for unknown connection handle %d",
			hdev->name, handle);
	}

	kfree_skb(skb);
}

static void hci_rx_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	BT_DBG("%s", hdev->name);

	read_lock(&hci_task_lock);

	while ((skb = skb_dequeue(&hdev->rx_q))) {
		if (atomic_read(&hdev->promisc)) {
			/* Send copy to the sockets */
			hci_send_to_sock(hdev, skb, NULL);
		}

		if (test_bit(HCI_RAW, &hdev->flags)) {
			kfree_skb(skb);
			continue;
		}

		if (test_bit(HCI_INIT, &hdev->flags)) {
			/* Don't process data packets in this states. */
			switch (bt_cb(skb)->pkt_type) {
			case HCI_ACLDATA_PKT:
			case HCI_SCODATA_PKT:
				kfree_skb(skb);
				continue;
			}
		}

		/* Process frame */
		switch (bt_cb(skb)->pkt_type) {
		case HCI_EVENT_PKT:
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

		default:
			kfree_skb(skb);
			break;
		}
	}

	read_unlock(&hci_task_lock);
}

static void hci_cmd_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	BT_DBG("%s cmd %d", hdev->name, atomic_read(&hdev->cmd_cnt));

	/* Send queued commands */
	if (atomic_read(&hdev->cmd_cnt)) {
		skb = skb_dequeue(&hdev->cmd_q);
		if (!skb)
			return;

		kfree_skb(hdev->sent_cmd);

		hdev->sent_cmd = skb_clone(skb, GFP_ATOMIC);
		if (hdev->sent_cmd) {
			atomic_dec(&hdev->cmd_cnt);
			hci_send_frame(skb);
			if (test_bit(HCI_RESET, &hdev->flags))
				del_timer(&hdev->cmd_timer);
			else
				mod_timer(&hdev->cmd_timer,
				  jiffies + msecs_to_jiffies(HCI_CMD_TIMEOUT));
		} else {
			skb_queue_head(&hdev->cmd_q, skb);
			tasklet_schedule(&hdev->cmd_task);
		}
	}
}
