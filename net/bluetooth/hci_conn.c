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

/* Bluetooth HCI connection handling. */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
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

void hci_acl_connect(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct inquiry_entry *ie;
	struct hci_cp_create_conn cp;

	BT_DBG("%p", conn);

	conn->state = BT_CONNECT;
	conn->out   = 1;
	conn->link_mode = HCI_LM_MASTER;

	conn->attempt++;

	memset(&cp, 0, sizeof(cp));
	bacpy(&cp.bdaddr, &conn->dst);
	cp.pscan_rep_mode = 0x02;

	if ((ie = hci_inquiry_cache_lookup(hdev, &conn->dst)) &&
			inquiry_entry_age(ie) <= INQUIRY_ENTRY_AGE_MAX) {
		cp.pscan_rep_mode = ie->data.pscan_rep_mode;
		cp.pscan_mode     = ie->data.pscan_mode;
		cp.clock_offset   = ie->data.clock_offset | cpu_to_le16(0x8000);
		memcpy(conn->dev_class, ie->data.dev_class, 3);
	}

	cp.pkt_type = cpu_to_le16(hdev->pkt_type & ACL_PTYPE_MASK);
	if (lmp_rswitch_capable(hdev) && !(hdev->link_mode & HCI_LM_MASTER))
		cp.role_switch = 0x01;
	else
		cp.role_switch = 0x00;

	hci_send_cmd(hdev, HCI_OP_CREATE_CONN, sizeof(cp), &cp);
}

static void hci_acl_connect_cancel(struct hci_conn *conn)
{
	struct hci_cp_create_conn_cancel cp;

	BT_DBG("%p", conn);

	if (conn->hdev->hci_ver < 2)
		return;

	bacpy(&cp.bdaddr, &conn->dst);
	hci_send_cmd(conn->hdev, HCI_OP_CREATE_CONN_CANCEL, sizeof(cp), &cp);
}

void hci_acl_disconn(struct hci_conn *conn, __u8 reason)
{
	struct hci_cp_disconnect cp;

	BT_DBG("%p", conn);

	conn->state = BT_DISCONN;

	cp.handle = cpu_to_le16(conn->handle);
	cp.reason = reason;
	hci_send_cmd(conn->hdev, HCI_OP_DISCONNECT, sizeof(cp), &cp);
}

void hci_add_sco(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_add_sco cp;

	BT_DBG("%p", conn);

	conn->state = BT_CONNECT;
	conn->out = 1;

	cp.handle   = cpu_to_le16(handle);
	cp.pkt_type = cpu_to_le16(hdev->pkt_type & SCO_PTYPE_MASK);

	hci_send_cmd(hdev, HCI_OP_ADD_SCO, sizeof(cp), &cp);
}

void hci_setup_sync(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_setup_sync_conn cp;

	BT_DBG("%p", conn);

	conn->state = BT_CONNECT;
	conn->out = 1;

	cp.handle   = cpu_to_le16(handle);
	cp.pkt_type = cpu_to_le16(hdev->esco_type);

	cp.tx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.rx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.max_latency    = cpu_to_le16(0xffff);
	cp.voice_setting  = cpu_to_le16(hdev->voice_setting);
	cp.retrans_effort = 0xff;

	hci_send_cmd(hdev, HCI_OP_SETUP_SYNC_CONN, sizeof(cp), &cp);
}

static void hci_conn_timeout(unsigned long arg)
{
	struct hci_conn *conn = (void *) arg;
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p state %d", conn, conn->state);

	if (atomic_read(&conn->refcnt))
		return;

	hci_dev_lock(hdev);

	switch (conn->state) {
	case BT_CONNECT:
		if (conn->type == ACL_LINK)
			hci_acl_connect_cancel(conn);
		else
			hci_acl_disconn(conn, 0x13);
		break;
	case BT_CONNECTED:
		hci_acl_disconn(conn, 0x13);
		break;
	default:
		conn->state = BT_CLOSED;
		break;
	}

	hci_dev_unlock(hdev);
}

static void hci_conn_idle(unsigned long arg)
{
	struct hci_conn *conn = (void *) arg;

	BT_DBG("conn %p mode %d", conn, conn->mode);

	hci_conn_enter_sniff_mode(conn);
}

struct hci_conn *hci_conn_add(struct hci_dev *hdev, int type, bdaddr_t *dst)
{
	struct hci_conn *conn;

	BT_DBG("%s dst %s", hdev->name, batostr(dst));

	conn = kzalloc(sizeof(struct hci_conn), GFP_ATOMIC);
	if (!conn)
		return NULL;

	bacpy(&conn->dst, dst);
	conn->hdev   = hdev;
	conn->type   = type;
	conn->mode   = HCI_CM_ACTIVE;
	conn->state  = BT_OPEN;

	conn->power_save = 1;

	skb_queue_head_init(&conn->data_q);

	setup_timer(&conn->disc_timer, hci_conn_timeout, (unsigned long)conn);
	setup_timer(&conn->idle_timer, hci_conn_idle, (unsigned long)conn);

	atomic_set(&conn->refcnt, 0);

	hci_dev_hold(hdev);

	tasklet_disable(&hdev->tx_task);

	hci_conn_hash_add(hdev, conn);
	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_CONN_ADD);

	hci_conn_add_sysfs(conn);

	tasklet_enable(&hdev->tx_task);

	return conn;
}

int hci_conn_del(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("%s conn %p handle %d", hdev->name, conn, conn->handle);

	del_timer(&conn->idle_timer);

	del_timer(&conn->disc_timer);

	if (conn->type == ACL_LINK) {
		struct hci_conn *sco = conn->link;
		if (sco)
			sco->link = NULL;

		/* Unacked frames */
		hdev->acl_cnt += conn->sent;
	} else {
		struct hci_conn *acl = conn->link;
		if (acl) {
			acl->link = NULL;
			hci_conn_put(acl);
		}
	}

	tasklet_disable(&hdev->tx_task);
	hci_conn_hash_del(hdev, conn);
	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_CONN_DEL);
	tasklet_enable(&hdev->tx_task);
	skb_queue_purge(&conn->data_q);
	hci_conn_del_sysfs(conn);

	return 0;
}

struct hci_dev *hci_get_route(bdaddr_t *dst, bdaddr_t *src)
{
	int use_src = bacmp(src, BDADDR_ANY);
	struct hci_dev *hdev = NULL;
	struct list_head *p;

	BT_DBG("%s -> %s", batostr(src), batostr(dst));

	read_lock_bh(&hci_dev_list_lock);

	list_for_each(p, &hci_dev_list) {
		struct hci_dev *d = list_entry(p, struct hci_dev, list);

		if (!test_bit(HCI_UP, &d->flags) || test_bit(HCI_RAW, &d->flags))
			continue;

		/* Simple routing:
		 *   No source address - find interface with bdaddr != dst
		 *   Source address    - find interface with bdaddr == src
		 */

		if (use_src) {
			if (!bacmp(&d->bdaddr, src)) {
				hdev = d; break;
			}
		} else {
			if (bacmp(&d->bdaddr, dst)) {
				hdev = d; break;
			}
		}
	}

	if (hdev)
		hdev = hci_dev_hold(hdev);

	read_unlock_bh(&hci_dev_list_lock);
	return hdev;
}
EXPORT_SYMBOL(hci_get_route);

/* Create SCO or ACL connection.
 * Device _must_ be locked */
struct hci_conn *hci_connect(struct hci_dev *hdev, int type, bdaddr_t *dst)
{
	struct hci_conn *acl;
	struct hci_conn *sco;

	BT_DBG("%s dst %s", hdev->name, batostr(dst));

	if (!(acl = hci_conn_hash_lookup_ba(hdev, ACL_LINK, dst))) {
		if (!(acl = hci_conn_add(hdev, ACL_LINK, dst)))
			return NULL;
	}

	hci_conn_hold(acl);

	if (acl->state == BT_OPEN || acl->state == BT_CLOSED)
		hci_acl_connect(acl);

	if (type == ACL_LINK)
		return acl;

	if (!(sco = hci_conn_hash_lookup_ba(hdev, type, dst))) {
		if (!(sco = hci_conn_add(hdev, type, dst))) {
			hci_conn_put(acl);
			return NULL;
		}
	}

	acl->link = sco;
	sco->link = acl;

	hci_conn_hold(sco);

	if (acl->state == BT_CONNECTED &&
			(sco->state == BT_OPEN || sco->state == BT_CLOSED)) {
		if (lmp_esco_capable(hdev))
			hci_setup_sync(sco, acl->handle);
		else
			hci_add_sco(sco, acl->handle);
	}

	return sco;
}
EXPORT_SYMBOL(hci_connect);

/* Authenticate remote device */
int hci_conn_auth(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	if (conn->link_mode & HCI_LM_AUTH)
		return 1;

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->pend)) {
		struct hci_cp_auth_requested cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(conn->hdev, HCI_OP_AUTH_REQUESTED, sizeof(cp), &cp);
	}
	return 0;
}
EXPORT_SYMBOL(hci_conn_auth);

/* Enable encryption */
int hci_conn_encrypt(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	if (conn->link_mode & HCI_LM_ENCRYPT)
		return 1;

	if (test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend))
		return 0;

	if (hci_conn_auth(conn)) {
		struct hci_cp_set_conn_encrypt cp;
		cp.handle  = cpu_to_le16(conn->handle);
		cp.encrypt = 1;
		hci_send_cmd(conn->hdev, HCI_OP_SET_CONN_ENCRYPT, sizeof(cp), &cp);
	}
	return 0;
}
EXPORT_SYMBOL(hci_conn_encrypt);

/* Change link key */
int hci_conn_change_link_key(struct hci_conn *conn)
{
	BT_DBG("conn %p", conn);

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->pend)) {
		struct hci_cp_change_conn_link_key cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(conn->hdev, HCI_OP_CHANGE_CONN_LINK_KEY, sizeof(cp), &cp);
	}
	return 0;
}
EXPORT_SYMBOL(hci_conn_change_link_key);

/* Switch role */
int hci_conn_switch_role(struct hci_conn *conn, uint8_t role)
{
	BT_DBG("conn %p", conn);

	if (!role && conn->link_mode & HCI_LM_MASTER)
		return 1;

	if (!test_and_set_bit(HCI_CONN_RSWITCH_PEND, &conn->pend)) {
		struct hci_cp_switch_role cp;
		bacpy(&cp.bdaddr, &conn->dst);
		cp.role = role;
		hci_send_cmd(conn->hdev, HCI_OP_SWITCH_ROLE, sizeof(cp), &cp);
	}
	return 0;
}
EXPORT_SYMBOL(hci_conn_switch_role);

/* Enter active mode */
void hci_conn_enter_active_mode(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p mode %d", conn, conn->mode);

	if (test_bit(HCI_RAW, &hdev->flags))
		return;

	if (conn->mode != HCI_CM_SNIFF || !conn->power_save)
		goto timer;

	if (!test_and_set_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend)) {
		struct hci_cp_exit_sniff_mode cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_EXIT_SNIFF_MODE, sizeof(cp), &cp);
	}

timer:
	if (hdev->idle_timeout > 0)
		mod_timer(&conn->idle_timer,
			jiffies + msecs_to_jiffies(hdev->idle_timeout));
}

/* Enter sniff mode */
void hci_conn_enter_sniff_mode(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("conn %p mode %d", conn, conn->mode);

	if (test_bit(HCI_RAW, &hdev->flags))
		return;

	if (!lmp_sniff_capable(hdev) || !lmp_sniff_capable(conn))
		return;

	if (conn->mode != HCI_CM_ACTIVE || !(conn->link_policy & HCI_LP_SNIFF))
		return;

	if (lmp_sniffsubr_capable(hdev) && lmp_sniffsubr_capable(conn)) {
		struct hci_cp_sniff_subrate cp;
		cp.handle             = cpu_to_le16(conn->handle);
		cp.max_latency        = cpu_to_le16(0);
		cp.min_remote_timeout = cpu_to_le16(0);
		cp.min_local_timeout  = cpu_to_le16(0);
		hci_send_cmd(hdev, HCI_OP_SNIFF_SUBRATE, sizeof(cp), &cp);
	}

	if (!test_and_set_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend)) {
		struct hci_cp_sniff_mode cp;
		cp.handle       = cpu_to_le16(conn->handle);
		cp.max_interval = cpu_to_le16(hdev->sniff_max_interval);
		cp.min_interval = cpu_to_le16(hdev->sniff_min_interval);
		cp.attempt      = cpu_to_le16(4);
		cp.timeout      = cpu_to_le16(1);
		hci_send_cmd(hdev, HCI_OP_SNIFF_MODE, sizeof(cp), &cp);
	}
}

/* Drop all connection on the device */
void hci_conn_hash_flush(struct hci_dev *hdev)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct list_head *p;

	BT_DBG("hdev %s", hdev->name);

	p = h->list.next;
	while (p != &h->list) {
		struct hci_conn *c;

		c = list_entry(p, struct hci_conn, list);
		p = p->next;

		c->state = BT_CLOSED;

		hci_proto_disconn_ind(c, 0x16);
		hci_conn_del(c);
	}
}

/* Check pending connect attempts */
void hci_conn_check_pending(struct hci_dev *hdev)
{
	struct hci_conn *conn;

	BT_DBG("hdev %s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_state(hdev, ACL_LINK, BT_CONNECT2);
	if (conn)
		hci_acl_connect(conn);

	hci_dev_unlock(hdev);
}

int hci_get_conn_list(void __user *arg)
{
	struct hci_conn_list_req req, *cl;
	struct hci_conn_info *ci;
	struct hci_dev *hdev;
	struct list_head *p;
	int n = 0, size, err;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	if (!req.conn_num || req.conn_num > (PAGE_SIZE * 2) / sizeof(*ci))
		return -EINVAL;

	size = sizeof(req) + req.conn_num * sizeof(*ci);

	if (!(cl = kmalloc(size, GFP_KERNEL)))
		return -ENOMEM;

	if (!(hdev = hci_dev_get(req.dev_id))) {
		kfree(cl);
		return -ENODEV;
	}

	ci = cl->conn_info;

	hci_dev_lock_bh(hdev);
	list_for_each(p, &hdev->conn_hash.list) {
		register struct hci_conn *c;
		c = list_entry(p, struct hci_conn, list);

		bacpy(&(ci + n)->bdaddr, &c->dst);
		(ci + n)->handle = c->handle;
		(ci + n)->type  = c->type;
		(ci + n)->out   = c->out;
		(ci + n)->state = c->state;
		(ci + n)->link_mode = c->link_mode;
		if (++n >= req.conn_num)
			break;
	}
	hci_dev_unlock_bh(hdev);

	cl->dev_id = hdev->id;
	cl->conn_num = n;
	size = sizeof(req) + n * sizeof(*ci);

	hci_dev_put(hdev);

	err = copy_to_user(arg, cl, size);
	kfree(cl);

	return err ? -EFAULT : 0;
}

int hci_get_conn_info(struct hci_dev *hdev, void __user *arg)
{
	struct hci_conn_info_req req;
	struct hci_conn_info ci;
	struct hci_conn *conn;
	char __user *ptr = arg + sizeof(req);

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	hci_dev_lock_bh(hdev);
	conn = hci_conn_hash_lookup_ba(hdev, req.type, &req.bdaddr);
	if (conn) {
		bacpy(&ci.bdaddr, &conn->dst);
		ci.handle = conn->handle;
		ci.type  = conn->type;
		ci.out   = conn->out;
		ci.state = conn->state;
		ci.link_mode = conn->link_mode;
	}
	hci_dev_unlock_bh(hdev);

	if (!conn)
		return -ENOENT;

	return copy_to_user(ptr, &ci, sizeof(ci)) ? -EFAULT : 0;
}
