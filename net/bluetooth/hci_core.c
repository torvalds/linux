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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kmod.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#ifndef CONFIG_BT_HCI_CORE_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

static void hci_cmd_task(unsigned long arg);
static void hci_rx_task(unsigned long arg);
static void hci_tx_task(unsigned long arg);
static void hci_notify(struct hci_dev *hdev, int event);

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
static struct notifier_block *hci_notifier;

/* ---- HCI notifications ---- */

int hci_register_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&hci_notifier, nb);
}

int hci_unregister_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&hci_notifier, nb);
}

void hci_notify(struct hci_dev *hdev, int event)
{
	notifier_call_chain(&hci_notifier, event, hdev);
}

/* ---- HCI requests ---- */

void hci_req_complete(struct hci_dev *hdev, int result)
{
	BT_DBG("%s result 0x%2.2x", hdev->name, result);

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
		err = -bt_err(hdev->req_result);
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		break;

	default:
		err = -ETIMEDOUT;
		break;
	};

	hdev->req_status = hdev->req_result = 0;

	BT_DBG("%s end: err %d", hdev->name, err);

	return err;
}

static inline int hci_request(struct hci_dev *hdev, void (*req)(struct hci_dev *hdev, unsigned long opt),
				unsigned long opt, __u32 timeout)
{
	int ret;

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
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_RESET, 0, NULL);
}

static void hci_init_req(struct hci_dev *hdev, unsigned long opt)
{
	struct sk_buff *skb;
	__u16 param;

	BT_DBG("%s %ld", hdev->name, opt);

	/* Driver initialization */

	/* Special commands */
	while ((skb = skb_dequeue(&hdev->driver_init))) {
		skb->pkt_type = HCI_COMMAND_PKT;
		skb->dev = (void *) hdev;
		skb_queue_tail(&hdev->cmd_q, skb);
		hci_sched_cmd(hdev);
	}
	skb_queue_purge(&hdev->driver_init);

	/* Mandatory initialization */

	/* Reset */
	if (test_bit(HCI_QUIRK_RESET_ON_INIT, &hdev->quirks))
			hci_send_cmd(hdev, OGF_HOST_CTL, OCF_RESET, 0, NULL);

	/* Read Local Supported Features */
	hci_send_cmd(hdev, OGF_INFO_PARAM, OCF_READ_LOCAL_FEATURES, 0, NULL);

	/* Read Buffer Size (ACL mtu, max pkt, etc.) */
	hci_send_cmd(hdev, OGF_INFO_PARAM, OCF_READ_BUFFER_SIZE, 0, NULL);

#if 0
	/* Host buffer size */
	{
		struct hci_cp_host_buffer_size cp;
		cp.acl_mtu = __cpu_to_le16(HCI_MAX_ACL_SIZE);
		cp.sco_mtu = HCI_MAX_SCO_SIZE;
		cp.acl_max_pkt = __cpu_to_le16(0xffff);
		cp.sco_max_pkt = __cpu_to_le16(0xffff);
		hci_send_cmd(hdev, OGF_HOST_CTL, OCF_HOST_BUFFER_SIZE, sizeof(cp), &cp);
	}
#endif

	/* Read BD Address */
	hci_send_cmd(hdev, OGF_INFO_PARAM, OCF_READ_BD_ADDR, 0, NULL);

	/* Read Voice Setting */
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_READ_VOICE_SETTING, 0, NULL);

	/* Optional initialization */

	/* Clear Event Filters */
	{
		struct hci_cp_set_event_flt cp;
		cp.flt_type  = HCI_FLT_CLEAR_ALL;
		hci_send_cmd(hdev, OGF_HOST_CTL, OCF_SET_EVENT_FLT, sizeof(cp), &cp);
	}

	/* Page timeout ~20 secs */
	param = __cpu_to_le16(0x8000);
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_PG_TIMEOUT, 2, &param);

	/* Connection accept timeout ~20 secs */
	param = __cpu_to_le16(0x7d00);
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_CA_TIMEOUT, 2, &param);
}

static void hci_scan_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 scan = opt;

	BT_DBG("%s %x", hdev->name, scan);

	/* Inquiry and Page scans */
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_SCAN_ENABLE, 1, &scan);
}

static void hci_auth_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 auth = opt;

	BT_DBG("%s %x", hdev->name, auth);

	/* Authentication */
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_AUTH_ENABLE, 1, &auth);
}

static void hci_encrypt_req(struct hci_dev *hdev, unsigned long opt)
{
	__u8 encrypt = opt;

	BT_DBG("%s %x", hdev->name, encrypt);

	/* Authentication */
	hci_send_cmd(hdev, OGF_HOST_CTL, OCF_WRITE_ENCRYPT_MODE, 1, &encrypt);
}

/* Get HCI device by index. 
 * Device is held on return. */
struct hci_dev *hci_dev_get(int index)
{
	struct hci_dev *hdev = NULL;
	struct list_head *p;

	BT_DBG("%d", index);

	if (index < 0)
		return NULL;

	read_lock(&hci_dev_list_lock);
	list_for_each(p, &hci_dev_list) {
		struct hci_dev *d = list_entry(p, struct hci_dev, list);
		if (d->id == index) {
			hdev = hci_dev_hold(d);
			break;
		}
	}
	read_unlock(&hci_dev_list_lock);
	return hdev;
}
EXPORT_SYMBOL(hci_dev_get);

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
	struct inquiry_entry *e;

	BT_DBG("cache %p, %s", cache, batostr(&data->bdaddr));

	if (!(e = hci_inquiry_cache_lookup(hdev, &data->bdaddr))) {
		/* Entry not in the cache. Add new one. */
		if (!(e = kmalloc(sizeof(struct inquiry_entry), GFP_ATOMIC)))
			return;
		memset(e, 0, sizeof(struct inquiry_entry));
		e->next     = cache->list;
		cache->list = e;
	}

	memcpy(&e->data, data, sizeof(*data));
	e->timestamp = jiffies;
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
	hci_send_cmd(hdev, OGF_LINK_CTL, OCF_INQUIRY, sizeof(cp), &cp);
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

	if (!(hdev = hci_dev_get(ir.dev_id)))
		return -ENODEV;

	hci_dev_lock_bh(hdev);
	if (inquiry_cache_age(hdev) > INQUIRY_CACHE_AGE_MAX || 
					inquiry_cache_empty(hdev) ||
					ir.flags & IREQ_CACHE_FLUSH) {
		inquiry_cache_flush(hdev);
		do_inquiry = 1;
	}
	hci_dev_unlock_bh(hdev);

	timeo = ir.length * 2 * HZ;
	if (do_inquiry && (err = hci_request(hdev, hci_inq_req, (unsigned long)&ir, timeo)) < 0)
		goto done;

	/* for unlimited number of responses we will use buffer with 255 entries */
	max_rsp = (ir.num_rsp == 0) ? 255 : ir.num_rsp;

	/* cache_dump can't sleep. Therefore we allocate temp buffer and then
	 * copy it to the user space.
	 */
	if (!(buf = kmalloc(sizeof(struct inquiry_info) * max_rsp, GFP_KERNEL))) {
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

	if (!(hdev = hci_dev_get(dev)))
		return -ENODEV;

	BT_DBG("%s %p", hdev->name, hdev);

	hci_req_lock(hdev);

	if (test_bit(HCI_UP, &hdev->flags)) {
		ret = -EALREADY;
		goto done;
	}

	if (test_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks))
		set_bit(HCI_RAW, &hdev->flags);

	if (hdev->open(hdev)) {
		ret = -EIO;
		goto done;
	}

	if (!test_bit(HCI_RAW, &hdev->flags)) {
		atomic_set(&hdev->cmd_cnt, 1);
		set_bit(HCI_INIT, &hdev->flags);

		//__hci_request(hdev, hci_reset_req, 0, HZ);
		ret = __hci_request(hdev, hci_init_req, 0, HCI_INIT_TIMEOUT);

		clear_bit(HCI_INIT, &hdev->flags);
	}

	if (!ret) {
		hci_dev_hold(hdev);
		set_bit(HCI_UP, &hdev->flags);
		hci_notify(hdev, HCI_DEV_UP);
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
		__hci_request(hdev, hci_reset_req, 0, HZ/4);
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
		kfree_skb(hdev->sent_cmd);
		hdev->sent_cmd = NULL;
	}

	/* After this point our queues are empty
	 * and no tasks are scheduled. */
	hdev->close(hdev);

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

	if (!(hdev = hci_dev_get(dev)))
		return -ENODEV;
	err = hci_dev_do_close(hdev);
	hci_dev_put(hdev);
	return err;
}

int hci_dev_reset(__u16 dev)
{
	struct hci_dev *hdev;
	int ret = 0;

	if (!(hdev = hci_dev_get(dev)))
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
	hdev->acl_cnt = 0; hdev->sco_cnt = 0;

	if (!test_bit(HCI_RAW, &hdev->flags))
		ret = __hci_request(hdev, hci_reset_req, 0, HCI_INIT_TIMEOUT);

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

	if (!(hdev = hci_dev_get(dev)))
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

	if (!(hdev = hci_dev_get(dr.dev_id)))
		return -ENODEV;

	switch (cmd) {
	case HCISETAUTH:
		err = hci_request(hdev, hci_auth_req, dr.dev_opt, HCI_INIT_TIMEOUT);
		break;

	case HCISETENCRYPT:
		if (!lmp_encrypt_capable(hdev)) {
			err = -EOPNOTSUPP;
			break;
		}

		if (!test_bit(HCI_AUTH, &hdev->flags)) {
			/* Auth must be enabled first */
			err = hci_request(hdev, hci_auth_req,
					dr.dev_opt, HCI_INIT_TIMEOUT);
			if (err)
				break;
		}

		err = hci_request(hdev, hci_encrypt_req,
					dr.dev_opt, HCI_INIT_TIMEOUT);
		break;

	case HCISETSCAN:
		err = hci_request(hdev, hci_scan_req, dr.dev_opt, HCI_INIT_TIMEOUT);
		break;

	case HCISETPTYPE:
		hdev->pkt_type = (__u16) dr.dev_opt;
		break;

	case HCISETLINKPOL:
		hdev->link_policy = (__u16) dr.dev_opt;
		break;

	case HCISETLINKMODE:
		hdev->link_mode = ((__u16) dr.dev_opt) & (HCI_LM_MASTER | HCI_LM_ACCEPT);
		break;

	case HCISETACLMTU:
		hdev->acl_mtu  = *((__u16 *)&dr.dev_opt + 1);
		hdev->acl_pkts = *((__u16 *)&dr.dev_opt + 0);
		break;

	case HCISETSCOMTU:
		hdev->sco_mtu  = *((__u16 *)&dr.dev_opt + 1);
		hdev->sco_pkts = *((__u16 *)&dr.dev_opt + 0);
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
	struct hci_dev_list_req *dl;
	struct hci_dev_req *dr;
	struct list_head *p;
	int n = 0, size, err;
	__u16 dev_num;

	if (get_user(dev_num, (__u16 __user *) arg))
		return -EFAULT;

	if (!dev_num || dev_num > (PAGE_SIZE * 2) / sizeof(*dr))
		return -EINVAL;

	size = sizeof(*dl) + dev_num * sizeof(*dr);

	if (!(dl = kmalloc(size, GFP_KERNEL)))
		return -ENOMEM;

	dr = dl->dev_req;

	read_lock_bh(&hci_dev_list_lock);
	list_for_each(p, &hci_dev_list) {
		struct hci_dev *hdev;
		hdev = list_entry(p, struct hci_dev, list);
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

	if (!(hdev = hci_dev_get(di.dev_id)))
		return -ENODEV;

	strcpy(di.name, hdev->name);
	di.bdaddr   = hdev->bdaddr;
	di.type     = hdev->type;
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

/* Alloc HCI device */
struct hci_dev *hci_alloc_dev(void)
{
	struct hci_dev *hdev;

	hdev = kmalloc(sizeof(struct hci_dev), GFP_KERNEL);
	if (!hdev)
		return NULL;

	memset(hdev, 0, sizeof(struct hci_dev));

	skb_queue_head_init(&hdev->driver_init);

	return hdev;
}
EXPORT_SYMBOL(hci_alloc_dev);

/* Free HCI device */
void hci_free_dev(struct hci_dev *hdev)
{
	skb_queue_purge(&hdev->driver_init);

	/* will free via class release */
	class_device_put(&hdev->class_dev);
}
EXPORT_SYMBOL(hci_free_dev);

/* Register HCI device */
int hci_register_dev(struct hci_dev *hdev)
{
	struct list_head *head = &hci_dev_list, *p;
	int id = 0;

	BT_DBG("%p name %s type %d owner %p", hdev, hdev->name, hdev->type, hdev->owner);

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
	hdev->link_mode = (HCI_LM_ACCEPT);

	tasklet_init(&hdev->cmd_task, hci_cmd_task,(unsigned long) hdev);
	tasklet_init(&hdev->rx_task, hci_rx_task, (unsigned long) hdev);
	tasklet_init(&hdev->tx_task, hci_tx_task, (unsigned long) hdev);

	skb_queue_head_init(&hdev->rx_q);
	skb_queue_head_init(&hdev->cmd_q);
	skb_queue_head_init(&hdev->raw_q);

	init_waitqueue_head(&hdev->req_wait_q);
	init_MUTEX(&hdev->req_lock);

	inquiry_cache_init(hdev);

	hci_conn_hash_init(hdev);

	memset(&hdev->stat, 0, sizeof(struct hci_dev_stats));

	atomic_set(&hdev->promisc, 0);

	write_unlock_bh(&hci_dev_list_lock);

	hci_register_sysfs(hdev);

	hci_notify(hdev, HCI_DEV_REG);

	return id;
}
EXPORT_SYMBOL(hci_register_dev);

/* Unregister HCI device */
int hci_unregister_dev(struct hci_dev *hdev)
{
	BT_DBG("%p name %s type %d", hdev, hdev->name, hdev->type);

	hci_unregister_sysfs(hdev);

	write_lock_bh(&hci_dev_list_lock);
	list_del(&hdev->list);
	write_unlock_bh(&hci_dev_list_lock);

	hci_dev_do_close(hdev);

	hci_notify(hdev, HCI_DEV_UNREG);

	__hci_dev_put(hdev);
	return 0;
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

	BT_DBG("%s type %d len %d", hdev->name, skb->pkt_type, skb->len);

	if (atomic_read(&hdev->promisc)) {
		/* Time stamp */
		do_gettimeofday(&skb->stamp);

		hci_send_to_sock(hdev, skb);
	}

	/* Get rid of skb owner, prior to sending to the driver. */
	skb_orphan(skb);

	return hdev->send(skb);
}

/* Send HCI command */
int hci_send_cmd(struct hci_dev *hdev, __u16 ogf, __u16 ocf, __u32 plen, void *param)
{
	int len = HCI_COMMAND_HDR_SIZE + plen;
	struct hci_command_hdr *hdr;
	struct sk_buff *skb;

	BT_DBG("%s ogf 0x%x ocf 0x%x plen %d", hdev->name, ogf, ocf, plen);

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb) {
		BT_ERR("%s Can't allocate memory for HCI command", hdev->name);
		return -ENOMEM;
	}

	hdr = (struct hci_command_hdr *) skb_put(skb, HCI_COMMAND_HDR_SIZE);
	hdr->opcode = __cpu_to_le16(hci_opcode_pack(ogf, ocf));
	hdr->plen   = plen;

	if (plen)
		memcpy(skb_put(skb, plen), param, plen);

	BT_DBG("skb len %d", skb->len);

	skb->pkt_type = HCI_COMMAND_PKT;
	skb->dev = (void *) hdev;
	skb_queue_tail(&hdev->cmd_q, skb);
	hci_sched_cmd(hdev);

	return 0;
}
EXPORT_SYMBOL(hci_send_cmd);

/* Get data from the previously sent command */
void *hci_sent_cmd_data(struct hci_dev *hdev, __u16 ogf, __u16 ocf)
{
	struct hci_command_hdr *hdr;

	if (!hdev->sent_cmd)
		return NULL;

	hdr = (void *) hdev->sent_cmd->data;

	if (hdr->opcode != __cpu_to_le16(hci_opcode_pack(ogf, ocf)))
		return NULL;

	BT_DBG("%s ogf 0x%x ocf 0x%x", hdev->name, ogf, ocf);

	return hdev->sent_cmd->data + HCI_COMMAND_HDR_SIZE;
}

/* Send ACL data */
static void hci_add_acl_hdr(struct sk_buff *skb, __u16 handle, __u16 flags)
{
	struct hci_acl_hdr *hdr;
	int len = skb->len;

	hdr = (struct hci_acl_hdr *) skb_push(skb, HCI_ACL_HDR_SIZE);
	hdr->handle = __cpu_to_le16(hci_handle_pack(handle, flags));
	hdr->dlen   = __cpu_to_le16(len);

	skb->h.raw = (void *) hdr;
}

int hci_send_acl(struct hci_conn *conn, struct sk_buff *skb, __u16 flags)
{
	struct hci_dev *hdev = conn->hdev;
	struct sk_buff *list;

	BT_DBG("%s conn %p flags 0x%x", hdev->name, conn, flags);

	skb->dev = (void *) hdev;
	skb->pkt_type = HCI_ACLDATA_PKT;
	hci_add_acl_hdr(skb, conn->handle, flags | ACL_START);

	if (!(list = skb_shinfo(skb)->frag_list)) {
		/* Non fragmented */
		BT_DBG("%s nonfrag skb %p len %d", hdev->name, skb, skb->len);

		skb_queue_tail(&conn->data_q, skb);
	} else {
		/* Fragmented */
		BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

		skb_shinfo(skb)->frag_list = NULL;

		/* Queue all fragments atomically */
		spin_lock_bh(&conn->data_q.lock);

		__skb_queue_tail(&conn->data_q, skb);
		do {
			skb = list; list = list->next;
			
			skb->dev = (void *) hdev;
			skb->pkt_type = HCI_ACLDATA_PKT;
			hci_add_acl_hdr(skb, conn->handle, flags | ACL_CONT);

			BT_DBG("%s frag %p len %d", hdev->name, skb, skb->len);

			__skb_queue_tail(&conn->data_q, skb);
		} while (list);

		spin_unlock_bh(&conn->data_q.lock);
	}

	hci_sched_tx(hdev);
	return 0;
}
EXPORT_SYMBOL(hci_send_acl);

/* Send SCO data */
int hci_send_sco(struct hci_conn *conn, struct sk_buff *skb)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_sco_hdr hdr;

	BT_DBG("%s len %d", hdev->name, skb->len);

	if (skb->len > hdev->sco_mtu) {
		kfree_skb(skb);
		return -EINVAL;
	}

	hdr.handle = __cpu_to_le16(conn->handle);
	hdr.dlen   = skb->len;

	skb->h.raw = skb_push(skb, HCI_SCO_HDR_SIZE);
	memcpy(skb->h.raw, &hdr, HCI_SCO_HDR_SIZE);

	skb->dev = (void *) hdev;
	skb->pkt_type = HCI_SCODATA_PKT;
	skb_queue_tail(&conn->data_q, skb);
	hci_sched_tx(hdev);
	return 0;
}
EXPORT_SYMBOL(hci_send_sco);

/* ---- HCI TX task (outgoing data) ---- */

/* HCI Connection scheduler */
static inline struct hci_conn *hci_low_sent(struct hci_dev *hdev, __u8 type, int *quote)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn  *conn = NULL;
	int num = 0, min = ~0;
	struct list_head *p;

	/* We don't have to lock device here. Connections are always 
	 * added and removed with TX task disabled. */
	list_for_each(p, &h->list) {
		struct hci_conn *c;
		c = list_entry(p, struct hci_conn, list);

		if (c->type != type || c->state != BT_CONNECTED
				|| skb_queue_empty(&c->data_q))
			continue;
		num++;

		if (c->sent < min) {
			min  = c->sent;
			conn = c;
		}
	}

	if (conn) {
		int cnt = (type == ACL_LINK ? hdev->acl_cnt : hdev->sco_cnt);
		int q = cnt / num;
		*quote = q ? q : 1;
	} else
		*quote = 0;

	BT_DBG("conn %p quote %d", conn, *quote);
	return conn;
}

static inline void hci_acl_tx_to(struct hci_dev *hdev)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct list_head *p;
	struct hci_conn  *c;

	BT_ERR("%s ACL tx timeout", hdev->name);

	/* Kill stalled connections */
	list_for_each(p, &h->list) {
		c = list_entry(p, struct hci_conn, list);
		if (c->type == ACL_LINK && c->sent) {
			BT_ERR("%s killing stalled ACL connection %s",
				hdev->name, batostr(&c->dst));
			hci_acl_disconn(c, 0x13);
		}
	}
}

static inline void hci_sched_acl(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	struct sk_buff *skb;
	int quote;

	BT_DBG("%s", hdev->name);

	if (!test_bit(HCI_RAW, &hdev->flags)) {
		/* ACL tx timeout must be longer than maximum
		 * link supervision timeout (40.9 seconds) */
		if (!hdev->acl_cnt && (jiffies - hdev->acl_last_tx) > (HZ * 45))
			hci_acl_tx_to(hdev);
	}

	while (hdev->acl_cnt && (conn = hci_low_sent(hdev, ACL_LINK, &quote))) {
		while (quote-- && (skb = skb_dequeue(&conn->data_q))) {
			BT_DBG("skb %p len %d", skb, skb->len);
			hci_send_frame(skb);
			hdev->acl_last_tx = jiffies;

			hdev->acl_cnt--;
			conn->sent++;
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

static void hci_tx_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	read_lock(&hci_task_lock);

	BT_DBG("%s acl %d sco %d", hdev->name, hdev->acl_cnt, hdev->sco_cnt);

	/* Schedule queues and send stuff to HCI driver */

	hci_sched_acl(hdev);

	hci_sched_sco(hdev);

	/* Send next queued raw (unknown type) packet */
	while ((skb = skb_dequeue(&hdev->raw_q)))
		hci_send_frame(skb);

	read_unlock(&hci_task_lock);
}

/* ----- HCI RX task (incoming data proccessing) ----- */

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

		/* Send to upper protocol */
		if ((hp = hci_proto[HCI_PROTO_L2CAP]) && hp->recv_acldata) {
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
		if ((hp = hci_proto[HCI_PROTO_SCO]) && hp->recv_scodata) {
			hp->recv_scodata(conn, skb);
			return;
		}
	} else {
		BT_ERR("%s SCO packet for unknown connection handle %d", 
			hdev->name, handle);
	}

	kfree_skb(skb);
}

void hci_rx_task(unsigned long arg)
{
	struct hci_dev *hdev = (struct hci_dev *) arg;
	struct sk_buff *skb;

	BT_DBG("%s", hdev->name);

	read_lock(&hci_task_lock);

	while ((skb = skb_dequeue(&hdev->rx_q))) {
		if (atomic_read(&hdev->promisc)) {
			/* Send copy to the sockets */
			hci_send_to_sock(hdev, skb);
		}

		if (test_bit(HCI_RAW, &hdev->flags)) {
			kfree_skb(skb);
			continue;
		}

		if (test_bit(HCI_INIT, &hdev->flags)) {
			/* Don't process data packets in this states. */
			switch (skb->pkt_type) {
			case HCI_ACLDATA_PKT:
			case HCI_SCODATA_PKT:
				kfree_skb(skb);
				continue;
			};
		}

		/* Process frame */
		switch (skb->pkt_type) {
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

	if (!atomic_read(&hdev->cmd_cnt) && (jiffies - hdev->cmd_last_tx) > HZ) {
		BT_ERR("%s command tx timeout", hdev->name);
		atomic_set(&hdev->cmd_cnt, 1);
	}

	/* Send queued commands */
	if (atomic_read(&hdev->cmd_cnt) && (skb = skb_dequeue(&hdev->cmd_q))) {
		if (hdev->sent_cmd)
			kfree_skb(hdev->sent_cmd);

		if ((hdev->sent_cmd = skb_clone(skb, GFP_ATOMIC))) {
			atomic_dec(&hdev->cmd_cnt);
			hci_send_frame(skb);
			hdev->cmd_last_tx = jiffies;
		} else {
			skb_queue_head(&hdev->cmd_q, skb);
			hci_sched_cmd(hdev);
		}
	}
}
