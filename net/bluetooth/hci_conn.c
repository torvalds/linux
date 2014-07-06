/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (c) 2000-2001, 2010, Code Aurora Forum. All rights reserved.

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

#include <linux/export.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "smp.h"
#include "a2mp.h"

struct sco_param {
	u16 pkt_type;
	u16 max_latency;
};

static const struct sco_param sco_param_cvsd[] = {
	{ EDR_ESCO_MASK & ~ESCO_2EV3, 0x000a }, /* S3 */
	{ EDR_ESCO_MASK & ~ESCO_2EV3, 0x0007 }, /* S2 */
	{ EDR_ESCO_MASK | ESCO_EV3,   0x0007 }, /* S1 */
	{ EDR_ESCO_MASK | ESCO_HV3,   0xffff }, /* D1 */
	{ EDR_ESCO_MASK | ESCO_HV1,   0xffff }, /* D0 */
};

static const struct sco_param sco_param_wideband[] = {
	{ EDR_ESCO_MASK & ~ESCO_2EV3, 0x000d }, /* T2 */
	{ EDR_ESCO_MASK | ESCO_EV3,   0x0008 }, /* T1 */
};

static void hci_le_create_connection_cancel(struct hci_conn *conn)
{
	hci_send_cmd(conn->hdev, HCI_OP_LE_CREATE_CONN_CANCEL, 0, NULL);
}

static void hci_acl_create_connection(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct inquiry_entry *ie;
	struct hci_cp_create_conn cp;

	BT_DBG("hcon %p", conn);

	conn->state = BT_CONNECT;
	conn->out = true;

	set_bit(HCI_CONN_MASTER, &conn->flags);

	conn->attempt++;

	conn->link_policy = hdev->link_policy;

	memset(&cp, 0, sizeof(cp));
	bacpy(&cp.bdaddr, &conn->dst);
	cp.pscan_rep_mode = 0x02;

	ie = hci_inquiry_cache_lookup(hdev, &conn->dst);
	if (ie) {
		if (inquiry_entry_age(ie) <= INQUIRY_ENTRY_AGE_MAX) {
			cp.pscan_rep_mode = ie->data.pscan_rep_mode;
			cp.pscan_mode     = ie->data.pscan_mode;
			cp.clock_offset   = ie->data.clock_offset |
					    cpu_to_le16(0x8000);
		}

		memcpy(conn->dev_class, ie->data.dev_class, 3);
		if (ie->data.ssp_mode > 0)
			set_bit(HCI_CONN_SSP_ENABLED, &conn->flags);
	}

	cp.pkt_type = cpu_to_le16(conn->pkt_type);
	if (lmp_rswitch_capable(hdev) && !(hdev->link_mode & HCI_LM_MASTER))
		cp.role_switch = 0x01;
	else
		cp.role_switch = 0x00;

	hci_send_cmd(hdev, HCI_OP_CREATE_CONN, sizeof(cp), &cp);
}

static void hci_acl_create_connection_cancel(struct hci_conn *conn)
{
	struct hci_cp_create_conn_cancel cp;

	BT_DBG("hcon %p", conn);

	if (conn->hdev->hci_ver < BLUETOOTH_VER_1_2)
		return;

	bacpy(&cp.bdaddr, &conn->dst);
	hci_send_cmd(conn->hdev, HCI_OP_CREATE_CONN_CANCEL, sizeof(cp), &cp);
}

static void hci_reject_sco(struct hci_conn *conn)
{
	struct hci_cp_reject_sync_conn_req cp;

	cp.reason = HCI_ERROR_REMOTE_USER_TERM;
	bacpy(&cp.bdaddr, &conn->dst);

	hci_send_cmd(conn->hdev, HCI_OP_REJECT_SYNC_CONN_REQ, sizeof(cp), &cp);
}

void hci_disconnect(struct hci_conn *conn, __u8 reason)
{
	struct hci_cp_disconnect cp;

	BT_DBG("hcon %p", conn);

	conn->state = BT_DISCONN;

	cp.handle = cpu_to_le16(conn->handle);
	cp.reason = reason;
	hci_send_cmd(conn->hdev, HCI_OP_DISCONNECT, sizeof(cp), &cp);
}

static void hci_amp_disconn(struct hci_conn *conn)
{
	struct hci_cp_disconn_phy_link cp;

	BT_DBG("hcon %p", conn);

	conn->state = BT_DISCONN;

	cp.phy_handle = HCI_PHY_HANDLE(conn->handle);
	cp.reason = hci_proto_disconn_ind(conn);
	hci_send_cmd(conn->hdev, HCI_OP_DISCONN_PHY_LINK,
		     sizeof(cp), &cp);
}

static void hci_add_sco(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_add_sco cp;

	BT_DBG("hcon %p", conn);

	conn->state = BT_CONNECT;
	conn->out = true;

	conn->attempt++;

	cp.handle   = cpu_to_le16(handle);
	cp.pkt_type = cpu_to_le16(conn->pkt_type);

	hci_send_cmd(hdev, HCI_OP_ADD_SCO, sizeof(cp), &cp);
}

bool hci_setup_sync(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_setup_sync_conn cp;
	const struct sco_param *param;

	BT_DBG("hcon %p", conn);

	conn->state = BT_CONNECT;
	conn->out = true;

	conn->attempt++;

	cp.handle   = cpu_to_le16(handle);

	cp.tx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.rx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.voice_setting  = cpu_to_le16(conn->setting);

	switch (conn->setting & SCO_AIRMODE_MASK) {
	case SCO_AIRMODE_TRANSP:
		if (conn->attempt > ARRAY_SIZE(sco_param_wideband))
			return false;
		cp.retrans_effort = 0x02;
		param = &sco_param_wideband[conn->attempt - 1];
		break;
	case SCO_AIRMODE_CVSD:
		if (conn->attempt > ARRAY_SIZE(sco_param_cvsd))
			return false;
		cp.retrans_effort = 0x01;
		param = &sco_param_cvsd[conn->attempt - 1];
		break;
	default:
		return false;
	}

	cp.pkt_type = __cpu_to_le16(param->pkt_type);
	cp.max_latency = __cpu_to_le16(param->max_latency);

	if (hci_send_cmd(hdev, HCI_OP_SETUP_SYNC_CONN, sizeof(cp), &cp) < 0)
		return false;

	return true;
}

u8 hci_le_conn_update(struct hci_conn *conn, u16 min, u16 max, u16 latency,
		      u16 to_multiplier)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_conn_params *params;
	struct hci_cp_le_conn_update cp;

	hci_dev_lock(hdev);

	params = hci_conn_params_lookup(hdev, &conn->dst, conn->dst_type);
	if (params) {
		params->conn_min_interval = min;
		params->conn_max_interval = max;
		params->conn_latency = latency;
		params->supervision_timeout = to_multiplier;
	}

	hci_dev_unlock(hdev);

	memset(&cp, 0, sizeof(cp));
	cp.handle		= cpu_to_le16(conn->handle);
	cp.conn_interval_min	= cpu_to_le16(min);
	cp.conn_interval_max	= cpu_to_le16(max);
	cp.conn_latency		= cpu_to_le16(latency);
	cp.supervision_timeout	= cpu_to_le16(to_multiplier);
	cp.min_ce_len		= cpu_to_le16(0x0000);
	cp.max_ce_len		= cpu_to_le16(0x0000);

	hci_send_cmd(hdev, HCI_OP_LE_CONN_UPDATE, sizeof(cp), &cp);

	if (params)
		return 0x01;

	return 0x00;
}

void hci_le_start_enc(struct hci_conn *conn, __le16 ediv, __le64 rand,
		      __u8 ltk[16])
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_le_start_enc cp;

	BT_DBG("hcon %p", conn);

	memset(&cp, 0, sizeof(cp));

	cp.handle = cpu_to_le16(conn->handle);
	cp.rand = rand;
	cp.ediv = ediv;
	memcpy(cp.ltk, ltk, sizeof(cp.ltk));

	hci_send_cmd(hdev, HCI_OP_LE_START_ENC, sizeof(cp), &cp);
}

/* Device _must_ be locked */
void hci_sco_setup(struct hci_conn *conn, __u8 status)
{
	struct hci_conn *sco = conn->link;

	if (!sco)
		return;

	BT_DBG("hcon %p", conn);

	if (!status) {
		if (lmp_esco_capable(conn->hdev))
			hci_setup_sync(sco, conn->handle);
		else
			hci_add_sco(sco, conn->handle);
	} else {
		hci_proto_connect_cfm(sco, status);
		hci_conn_del(sco);
	}
}

static void hci_conn_timeout(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn,
					     disc_work.work);
	int refcnt = atomic_read(&conn->refcnt);

	BT_DBG("hcon %p state %s", conn, state_to_string(conn->state));

	WARN_ON(refcnt < 0);

	/* FIXME: It was observed that in pairing failed scenario, refcnt
	 * drops below 0. Probably this is because l2cap_conn_del calls
	 * l2cap_chan_del for each channel, and inside l2cap_chan_del conn is
	 * dropped. After that loop hci_chan_del is called which also drops
	 * conn. For now make sure that ACL is alive if refcnt is higher then 0,
	 * otherwise drop it.
	 */
	if (refcnt > 0)
		return;

	switch (conn->state) {
	case BT_CONNECT:
	case BT_CONNECT2:
		if (conn->out) {
			if (conn->type == ACL_LINK)
				hci_acl_create_connection_cancel(conn);
			else if (conn->type == LE_LINK)
				hci_le_create_connection_cancel(conn);
		} else if (conn->type == SCO_LINK || conn->type == ESCO_LINK) {
			hci_reject_sco(conn);
		}
		break;
	case BT_CONFIG:
	case BT_CONNECTED:
		if (conn->type == AMP_LINK) {
			hci_amp_disconn(conn);
		} else {
			__u8 reason = hci_proto_disconn_ind(conn);

			/* When we are master of an established connection
			 * and it enters the disconnect timeout, then go
			 * ahead and try to read the current clock offset.
			 *
			 * Processing of the result is done within the
			 * event handling and hci_clock_offset_evt function.
			 */
			if (conn->type == ACL_LINK &&
			    test_bit(HCI_CONN_MASTER, &conn->flags)) {
				struct hci_dev *hdev = conn->hdev;
				struct hci_cp_read_clock_offset cp;

				cp.handle = cpu_to_le16(conn->handle);

				hci_send_cmd(hdev, HCI_OP_READ_CLOCK_OFFSET,
					     sizeof(cp), &cp);
			}

			hci_disconnect(conn, reason);
		}
		break;
	default:
		conn->state = BT_CLOSED;
		break;
	}
}

/* Enter sniff mode */
static void hci_conn_idle(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn,
					     idle_work.work);
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("hcon %p mode %d", conn, conn->mode);

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

	if (!test_and_set_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->flags)) {
		struct hci_cp_sniff_mode cp;
		cp.handle       = cpu_to_le16(conn->handle);
		cp.max_interval = cpu_to_le16(hdev->sniff_max_interval);
		cp.min_interval = cpu_to_le16(hdev->sniff_min_interval);
		cp.attempt      = cpu_to_le16(4);
		cp.timeout      = cpu_to_le16(1);
		hci_send_cmd(hdev, HCI_OP_SNIFF_MODE, sizeof(cp), &cp);
	}
}

static void hci_conn_auto_accept(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn,
					     auto_accept_work.work);

	hci_send_cmd(conn->hdev, HCI_OP_USER_CONFIRM_REPLY, sizeof(conn->dst),
		     &conn->dst);
}

static void le_conn_timeout(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn,
					     le_conn_timeout.work);
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("");

	/* We could end up here due to having done directed advertising,
	 * so clean up the state if necessary. This should however only
	 * happen with broken hardware or if low duty cycle was used
	 * (which doesn't have a timeout of its own).
	 */
	if (test_bit(HCI_ADVERTISING, &hdev->dev_flags)) {
		u8 enable = 0x00;
		hci_send_cmd(hdev, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable),
			     &enable);
		hci_le_conn_failed(conn, HCI_ERROR_ADVERTISING_TIMEOUT);
		return;
	}

	hci_le_create_connection_cancel(conn);
}

struct hci_conn *hci_conn_add(struct hci_dev *hdev, int type, bdaddr_t *dst)
{
	struct hci_conn *conn;

	BT_DBG("%s dst %pMR", hdev->name, dst);

	conn = kzalloc(sizeof(struct hci_conn), GFP_KERNEL);
	if (!conn)
		return NULL;

	bacpy(&conn->dst, dst);
	bacpy(&conn->src, &hdev->bdaddr);
	conn->hdev  = hdev;
	conn->type  = type;
	conn->mode  = HCI_CM_ACTIVE;
	conn->state = BT_OPEN;
	conn->auth_type = HCI_AT_GENERAL_BONDING;
	conn->io_capability = hdev->io_capability;
	conn->remote_auth = 0xff;
	conn->key_type = 0xff;
	conn->tx_power = HCI_TX_POWER_INVALID;
	conn->max_tx_power = HCI_TX_POWER_INVALID;

	set_bit(HCI_CONN_POWER_SAVE, &conn->flags);
	conn->disc_timeout = HCI_DISCONN_TIMEOUT;

	switch (type) {
	case ACL_LINK:
		conn->pkt_type = hdev->pkt_type & ACL_PTYPE_MASK;
		break;
	case LE_LINK:
		/* conn->src should reflect the local identity address */
		hci_copy_identity_address(hdev, &conn->src, &conn->src_type);
		break;
	case SCO_LINK:
		if (lmp_esco_capable(hdev))
			conn->pkt_type = (hdev->esco_type & SCO_ESCO_MASK) |
					(hdev->esco_type & EDR_ESCO_MASK);
		else
			conn->pkt_type = hdev->pkt_type & SCO_PTYPE_MASK;
		break;
	case ESCO_LINK:
		conn->pkt_type = hdev->esco_type & ~EDR_ESCO_MASK;
		break;
	}

	skb_queue_head_init(&conn->data_q);

	INIT_LIST_HEAD(&conn->chan_list);

	INIT_DELAYED_WORK(&conn->disc_work, hci_conn_timeout);
	INIT_DELAYED_WORK(&conn->auto_accept_work, hci_conn_auto_accept);
	INIT_DELAYED_WORK(&conn->idle_work, hci_conn_idle);
	INIT_DELAYED_WORK(&conn->le_conn_timeout, le_conn_timeout);

	atomic_set(&conn->refcnt, 0);

	hci_dev_hold(hdev);

	hci_conn_hash_add(hdev, conn);
	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_CONN_ADD);

	hci_conn_init_sysfs(conn);

	return conn;
}

int hci_conn_del(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("%s hcon %p handle %d", hdev->name, conn, conn->handle);

	cancel_delayed_work_sync(&conn->disc_work);
	cancel_delayed_work_sync(&conn->auto_accept_work);
	cancel_delayed_work_sync(&conn->idle_work);

	if (conn->type == ACL_LINK) {
		struct hci_conn *sco = conn->link;
		if (sco)
			sco->link = NULL;

		/* Unacked frames */
		hdev->acl_cnt += conn->sent;
	} else if (conn->type == LE_LINK) {
		cancel_delayed_work_sync(&conn->le_conn_timeout);

		if (hdev->le_pkts)
			hdev->le_cnt += conn->sent;
		else
			hdev->acl_cnt += conn->sent;
	} else {
		struct hci_conn *acl = conn->link;
		if (acl) {
			acl->link = NULL;
			hci_conn_drop(acl);
		}
	}

	hci_chan_list_flush(conn);

	if (conn->amp_mgr)
		amp_mgr_put(conn->amp_mgr);

	hci_conn_hash_del(hdev, conn);
	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_CONN_DEL);

	skb_queue_purge(&conn->data_q);

	hci_conn_del_sysfs(conn);

	hci_dev_put(hdev);

	hci_conn_put(conn);

	return 0;
}

struct hci_dev *hci_get_route(bdaddr_t *dst, bdaddr_t *src)
{
	int use_src = bacmp(src, BDADDR_ANY);
	struct hci_dev *hdev = NULL, *d;

	BT_DBG("%pMR -> %pMR", src, dst);

	read_lock(&hci_dev_list_lock);

	list_for_each_entry(d, &hci_dev_list, list) {
		if (!test_bit(HCI_UP, &d->flags) ||
		    test_bit(HCI_USER_CHANNEL, &d->dev_flags) ||
		    d->dev_type != HCI_BREDR)
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

	read_unlock(&hci_dev_list_lock);
	return hdev;
}
EXPORT_SYMBOL(hci_get_route);

/* This function requires the caller holds hdev->lock */
void hci_le_conn_failed(struct hci_conn *conn, u8 status)
{
	struct hci_dev *hdev = conn->hdev;

	conn->state = BT_CLOSED;

	mgmt_connect_failed(hdev, &conn->dst, conn->type, conn->dst_type,
			    status);

	hci_proto_connect_cfm(conn, status);

	hci_conn_del(conn);

	/* Since we may have temporarily stopped the background scanning in
	 * favor of connection establishment, we should restart it.
	 */
	hci_update_background_scan(hdev);

	/* Re-enable advertising in case this was a failed connection
	 * attempt as a peripheral.
	 */
	mgmt_reenable_advertising(hdev);
}

static void create_le_conn_complete(struct hci_dev *hdev, u8 status)
{
	struct hci_conn *conn;

	if (status == 0)
		return;

	BT_ERR("HCI request failed to create LE connection: status 0x%2.2x",
	       status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_state(hdev, LE_LINK, BT_CONNECT);
	if (!conn)
		goto done;

	hci_le_conn_failed(conn, status);

done:
	hci_dev_unlock(hdev);
}

static void hci_req_add_le_create_conn(struct hci_request *req,
				       struct hci_conn *conn)
{
	struct hci_cp_le_create_conn cp;
	struct hci_dev *hdev = conn->hdev;
	u8 own_addr_type;

	memset(&cp, 0, sizeof(cp));

	/* Update random address, but set require_privacy to false so
	 * that we never connect with an unresolvable address.
	 */
	if (hci_update_random_address(req, false, &own_addr_type))
		return;

	cp.scan_interval = cpu_to_le16(hdev->le_scan_interval);
	cp.scan_window = cpu_to_le16(hdev->le_scan_window);
	bacpy(&cp.peer_addr, &conn->dst);
	cp.peer_addr_type = conn->dst_type;
	cp.own_address_type = own_addr_type;
	cp.conn_interval_min = cpu_to_le16(conn->le_conn_min_interval);
	cp.conn_interval_max = cpu_to_le16(conn->le_conn_max_interval);
	cp.conn_latency = cpu_to_le16(conn->le_conn_latency);
	cp.supervision_timeout = cpu_to_le16(conn->le_supv_timeout);
	cp.min_ce_len = cpu_to_le16(0x0000);
	cp.max_ce_len = cpu_to_le16(0x0000);

	hci_req_add(req, HCI_OP_LE_CREATE_CONN, sizeof(cp), &cp);

	conn->state = BT_CONNECT;
}

static void hci_req_directed_advertising(struct hci_request *req,
					 struct hci_conn *conn)
{
	struct hci_dev *hdev = req->hdev;
	struct hci_cp_le_set_adv_param cp;
	u8 own_addr_type;
	u8 enable;

	enable = 0x00;
	hci_req_add(req, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable), &enable);

	/* Clear the HCI_ADVERTISING bit temporarily so that the
	 * hci_update_random_address knows that it's safe to go ahead
	 * and write a new random address. The flag will be set back on
	 * as soon as the SET_ADV_ENABLE HCI command completes.
	 */
	clear_bit(HCI_ADVERTISING, &hdev->dev_flags);

	/* Set require_privacy to false so that the remote device has a
	 * chance of identifying us.
	 */
	if (hci_update_random_address(req, false, &own_addr_type) < 0)
		return;

	memset(&cp, 0, sizeof(cp));
	cp.type = LE_ADV_DIRECT_IND;
	cp.own_address_type = own_addr_type;
	cp.direct_addr_type = conn->dst_type;
	bacpy(&cp.direct_addr, &conn->dst);
	cp.channel_map = hdev->le_adv_channel_map;

	hci_req_add(req, HCI_OP_LE_SET_ADV_PARAM, sizeof(cp), &cp);

	enable = 0x01;
	hci_req_add(req, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable), &enable);

	conn->state = BT_CONNECT;
}

struct hci_conn *hci_connect_le(struct hci_dev *hdev, bdaddr_t *dst,
				u8 dst_type, u8 sec_level, u8 auth_type,
				u16 conn_timeout)
{
	struct hci_conn_params *params;
	struct hci_conn *conn;
	struct smp_irk *irk;
	struct hci_request req;
	int err;

	/* Some devices send ATT messages as soon as the physical link is
	 * established. To be able to handle these ATT messages, the user-
	 * space first establishes the connection and then starts the pairing
	 * process.
	 *
	 * So if a hci_conn object already exists for the following connection
	 * attempt, we simply update pending_sec_level and auth_type fields
	 * and return the object found.
	 */
	conn = hci_conn_hash_lookup_ba(hdev, LE_LINK, dst);
	if (conn) {
		conn->pending_sec_level = sec_level;
		conn->auth_type = auth_type;
		goto done;
	}

	/* Since the controller supports only one LE connection attempt at a
	 * time, we return -EBUSY if there is any connection attempt running.
	 */
	conn = hci_conn_hash_lookup_state(hdev, LE_LINK, BT_CONNECT);
	if (conn)
		return ERR_PTR(-EBUSY);

	/* When given an identity address with existing identity
	 * resolving key, the connection needs to be established
	 * to a resolvable random address.
	 *
	 * This uses the cached random resolvable address from
	 * a previous scan. When no cached address is available,
	 * try connecting to the identity address instead.
	 *
	 * Storing the resolvable random address is required here
	 * to handle connection failures. The address will later
	 * be resolved back into the original identity address
	 * from the connect request.
	 */
	irk = hci_find_irk_by_addr(hdev, dst, dst_type);
	if (irk && bacmp(&irk->rpa, BDADDR_ANY)) {
		dst = &irk->rpa;
		dst_type = ADDR_LE_DEV_RANDOM;
	}

	conn = hci_conn_add(hdev, LE_LINK, dst);
	if (!conn)
		return ERR_PTR(-ENOMEM);

	conn->dst_type = dst_type;
	conn->sec_level = BT_SECURITY_LOW;
	conn->pending_sec_level = sec_level;
	conn->auth_type = auth_type;
	conn->conn_timeout = conn_timeout;

	hci_req_init(&req, hdev);

	if (test_bit(HCI_ADVERTISING, &hdev->dev_flags)) {
		hci_req_directed_advertising(&req, conn);
		goto create_conn;
	}

	conn->out = true;
	set_bit(HCI_CONN_MASTER, &conn->flags);

	params = hci_conn_params_lookup(hdev, &conn->dst, conn->dst_type);
	if (params) {
		conn->le_conn_min_interval = params->conn_min_interval;
		conn->le_conn_max_interval = params->conn_max_interval;
		conn->le_conn_latency = params->conn_latency;
		conn->le_supv_timeout = params->supervision_timeout;
	} else {
		conn->le_conn_min_interval = hdev->le_conn_min_interval;
		conn->le_conn_max_interval = hdev->le_conn_max_interval;
		conn->le_conn_latency = hdev->le_conn_latency;
		conn->le_supv_timeout = hdev->le_supv_timeout;
	}

	/* If controller is scanning, we stop it since some controllers are
	 * not able to scan and connect at the same time. Also set the
	 * HCI_LE_SCAN_INTERRUPTED flag so that the command complete
	 * handler for scan disabling knows to set the correct discovery
	 * state.
	 */
	if (test_bit(HCI_LE_SCAN, &hdev->dev_flags)) {
		hci_req_add_le_scan_disable(&req);
		set_bit(HCI_LE_SCAN_INTERRUPTED, &hdev->dev_flags);
	}

	hci_req_add_le_create_conn(&req, conn);

create_conn:
	err = hci_req_run(&req, create_le_conn_complete);
	if (err) {
		hci_conn_del(conn);
		return ERR_PTR(err);
	}

done:
	hci_conn_hold(conn);
	return conn;
}

struct hci_conn *hci_connect_acl(struct hci_dev *hdev, bdaddr_t *dst,
				 u8 sec_level, u8 auth_type)
{
	struct hci_conn *acl;

	if (!test_bit(HCI_BREDR_ENABLED, &hdev->dev_flags))
		return ERR_PTR(-ENOTSUPP);

	acl = hci_conn_hash_lookup_ba(hdev, ACL_LINK, dst);
	if (!acl) {
		acl = hci_conn_add(hdev, ACL_LINK, dst);
		if (!acl)
			return ERR_PTR(-ENOMEM);
	}

	hci_conn_hold(acl);

	if (acl->state == BT_OPEN || acl->state == BT_CLOSED) {
		acl->sec_level = BT_SECURITY_LOW;
		acl->pending_sec_level = sec_level;
		acl->auth_type = auth_type;
		hci_acl_create_connection(acl);
	}

	return acl;
}

struct hci_conn *hci_connect_sco(struct hci_dev *hdev, int type, bdaddr_t *dst,
				 __u16 setting)
{
	struct hci_conn *acl;
	struct hci_conn *sco;

	acl = hci_connect_acl(hdev, dst, BT_SECURITY_LOW, HCI_AT_NO_BONDING);
	if (IS_ERR(acl))
		return acl;

	sco = hci_conn_hash_lookup_ba(hdev, type, dst);
	if (!sco) {
		sco = hci_conn_add(hdev, type, dst);
		if (!sco) {
			hci_conn_drop(acl);
			return ERR_PTR(-ENOMEM);
		}
	}

	acl->link = sco;
	sco->link = acl;

	hci_conn_hold(sco);

	sco->setting = setting;

	if (acl->state == BT_CONNECTED &&
	    (sco->state == BT_OPEN || sco->state == BT_CLOSED)) {
		set_bit(HCI_CONN_POWER_SAVE, &acl->flags);
		hci_conn_enter_active_mode(acl, BT_POWER_FORCE_ACTIVE_ON);

		if (test_bit(HCI_CONN_MODE_CHANGE_PEND, &acl->flags)) {
			/* defer SCO setup until mode change completed */
			set_bit(HCI_CONN_SCO_SETUP_PEND, &acl->flags);
			return sco;
		}

		hci_sco_setup(acl, 0x00);
	}

	return sco;
}

/* Check link security requirement */
int hci_conn_check_link_mode(struct hci_conn *conn)
{
	BT_DBG("hcon %p", conn);

	/* In Secure Connections Only mode, it is required that Secure
	 * Connections is used and the link is encrypted with AES-CCM
	 * using a P-256 authenticated combination key.
	 */
	if (test_bit(HCI_SC_ONLY, &conn->hdev->flags)) {
		if (!hci_conn_sc_enabled(conn) ||
		    !test_bit(HCI_CONN_AES_CCM, &conn->flags) ||
		    conn->key_type != HCI_LK_AUTH_COMBINATION_P256)
			return 0;
	}

	if (hci_conn_ssp_enabled(conn) &&
	    !test_bit(HCI_CONN_ENCRYPT, &conn->flags))
		return 0;

	return 1;
}

/* Authenticate remote device */
static int hci_conn_auth(struct hci_conn *conn, __u8 sec_level, __u8 auth_type)
{
	BT_DBG("hcon %p", conn);

	if (conn->pending_sec_level > sec_level)
		sec_level = conn->pending_sec_level;

	if (sec_level > conn->sec_level)
		conn->pending_sec_level = sec_level;
	else if (test_bit(HCI_CONN_AUTH, &conn->flags))
		return 1;

	/* Make sure we preserve an existing MITM requirement*/
	auth_type |= (conn->auth_type & 0x01);

	conn->auth_type = auth_type;

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->flags)) {
		struct hci_cp_auth_requested cp;

		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(conn->hdev, HCI_OP_AUTH_REQUESTED,
			     sizeof(cp), &cp);

		/* If we're already encrypted set the REAUTH_PEND flag,
		 * otherwise set the ENCRYPT_PEND.
		 */
		if (test_bit(HCI_CONN_ENCRYPT, &conn->flags))
			set_bit(HCI_CONN_REAUTH_PEND, &conn->flags);
		else
			set_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags);
	}

	return 0;
}

/* Encrypt the the link */
static void hci_conn_encrypt(struct hci_conn *conn)
{
	BT_DBG("hcon %p", conn);

	if (!test_and_set_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags)) {
		struct hci_cp_set_conn_encrypt cp;
		cp.handle  = cpu_to_le16(conn->handle);
		cp.encrypt = 0x01;
		hci_send_cmd(conn->hdev, HCI_OP_SET_CONN_ENCRYPT, sizeof(cp),
			     &cp);
	}
}

/* Enable security */
int hci_conn_security(struct hci_conn *conn, __u8 sec_level, __u8 auth_type)
{
	BT_DBG("hcon %p", conn);

	if (conn->type == LE_LINK)
		return smp_conn_security(conn, sec_level);

	/* For sdp we don't need the link key. */
	if (sec_level == BT_SECURITY_SDP)
		return 1;

	/* For non 2.1 devices and low security level we don't need the link
	   key. */
	if (sec_level == BT_SECURITY_LOW && !hci_conn_ssp_enabled(conn))
		return 1;

	/* For other security levels we need the link key. */
	if (!test_bit(HCI_CONN_AUTH, &conn->flags))
		goto auth;

	/* An authenticated FIPS approved combination key has sufficient
	 * security for security level 4. */
	if (conn->key_type == HCI_LK_AUTH_COMBINATION_P256 &&
	    sec_level == BT_SECURITY_FIPS)
		goto encrypt;

	/* An authenticated combination key has sufficient security for
	   security level 3. */
	if ((conn->key_type == HCI_LK_AUTH_COMBINATION_P192 ||
	     conn->key_type == HCI_LK_AUTH_COMBINATION_P256) &&
	    sec_level == BT_SECURITY_HIGH)
		goto encrypt;

	/* An unauthenticated combination key has sufficient security for
	   security level 1 and 2. */
	if ((conn->key_type == HCI_LK_UNAUTH_COMBINATION_P192 ||
	     conn->key_type == HCI_LK_UNAUTH_COMBINATION_P256) &&
	    (sec_level == BT_SECURITY_MEDIUM || sec_level == BT_SECURITY_LOW))
		goto encrypt;

	/* A combination key has always sufficient security for the security
	   levels 1 or 2. High security level requires the combination key
	   is generated using maximum PIN code length (16).
	   For pre 2.1 units. */
	if (conn->key_type == HCI_LK_COMBINATION &&
	    (sec_level == BT_SECURITY_MEDIUM || sec_level == BT_SECURITY_LOW ||
	     conn->pin_length == 16))
		goto encrypt;

auth:
	if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags))
		return 0;

	if (!hci_conn_auth(conn, sec_level, auth_type))
		return 0;

encrypt:
	if (test_bit(HCI_CONN_ENCRYPT, &conn->flags))
		return 1;

	hci_conn_encrypt(conn);
	return 0;
}
EXPORT_SYMBOL(hci_conn_security);

/* Check secure link requirement */
int hci_conn_check_secure(struct hci_conn *conn, __u8 sec_level)
{
	BT_DBG("hcon %p", conn);

	/* Accept if non-secure or higher security level is required */
	if (sec_level != BT_SECURITY_HIGH && sec_level != BT_SECURITY_FIPS)
		return 1;

	/* Accept if secure or higher security level is already present */
	if (conn->sec_level == BT_SECURITY_HIGH ||
	    conn->sec_level == BT_SECURITY_FIPS)
		return 1;

	/* Reject not secure link */
	return 0;
}
EXPORT_SYMBOL(hci_conn_check_secure);

/* Change link key */
int hci_conn_change_link_key(struct hci_conn *conn)
{
	BT_DBG("hcon %p", conn);

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->flags)) {
		struct hci_cp_change_conn_link_key cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(conn->hdev, HCI_OP_CHANGE_CONN_LINK_KEY,
			     sizeof(cp), &cp);
	}

	return 0;
}

/* Switch role */
int hci_conn_switch_role(struct hci_conn *conn, __u8 role)
{
	BT_DBG("hcon %p", conn);

	if (!role && test_bit(HCI_CONN_MASTER, &conn->flags))
		return 1;

	if (!test_and_set_bit(HCI_CONN_RSWITCH_PEND, &conn->flags)) {
		struct hci_cp_switch_role cp;
		bacpy(&cp.bdaddr, &conn->dst);
		cp.role = role;
		hci_send_cmd(conn->hdev, HCI_OP_SWITCH_ROLE, sizeof(cp), &cp);
	}

	return 0;
}
EXPORT_SYMBOL(hci_conn_switch_role);

/* Enter active mode */
void hci_conn_enter_active_mode(struct hci_conn *conn, __u8 force_active)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("hcon %p mode %d", conn, conn->mode);

	if (conn->mode != HCI_CM_SNIFF)
		goto timer;

	if (!test_bit(HCI_CONN_POWER_SAVE, &conn->flags) && !force_active)
		goto timer;

	if (!test_and_set_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->flags)) {
		struct hci_cp_exit_sniff_mode cp;
		cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_EXIT_SNIFF_MODE, sizeof(cp), &cp);
	}

timer:
	if (hdev->idle_timeout > 0)
		queue_delayed_work(hdev->workqueue, &conn->idle_work,
				   msecs_to_jiffies(hdev->idle_timeout));
}

/* Drop all connection on the device */
void hci_conn_hash_flush(struct hci_dev *hdev)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn *c, *n;

	BT_DBG("hdev %s", hdev->name);

	list_for_each_entry_safe(c, n, &h->list, list) {
		c->state = BT_CLOSED;

		hci_proto_disconn_cfm(c, HCI_ERROR_LOCAL_HOST_TERM);
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
		hci_acl_create_connection(conn);

	hci_dev_unlock(hdev);
}

static u32 get_link_mode(struct hci_conn *conn)
{
	u32 link_mode = 0;

	if (test_bit(HCI_CONN_MASTER, &conn->flags))
		link_mode |= HCI_LM_MASTER;

	if (test_bit(HCI_CONN_ENCRYPT, &conn->flags))
		link_mode |= HCI_LM_ENCRYPT;

	if (test_bit(HCI_CONN_AUTH, &conn->flags))
		link_mode |= HCI_LM_AUTH;

	if (test_bit(HCI_CONN_SECURE, &conn->flags))
		link_mode |= HCI_LM_SECURE;

	if (test_bit(HCI_CONN_FIPS, &conn->flags))
		link_mode |= HCI_LM_FIPS;

	return link_mode;
}

int hci_get_conn_list(void __user *arg)
{
	struct hci_conn *c;
	struct hci_conn_list_req req, *cl;
	struct hci_conn_info *ci;
	struct hci_dev *hdev;
	int n = 0, size, err;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	if (!req.conn_num || req.conn_num > (PAGE_SIZE * 2) / sizeof(*ci))
		return -EINVAL;

	size = sizeof(req) + req.conn_num * sizeof(*ci);

	cl = kmalloc(size, GFP_KERNEL);
	if (!cl)
		return -ENOMEM;

	hdev = hci_dev_get(req.dev_id);
	if (!hdev) {
		kfree(cl);
		return -ENODEV;
	}

	ci = cl->conn_info;

	hci_dev_lock(hdev);
	list_for_each_entry(c, &hdev->conn_hash.list, list) {
		bacpy(&(ci + n)->bdaddr, &c->dst);
		(ci + n)->handle = c->handle;
		(ci + n)->type  = c->type;
		(ci + n)->out   = c->out;
		(ci + n)->state = c->state;
		(ci + n)->link_mode = get_link_mode(c);
		if (++n >= req.conn_num)
			break;
	}
	hci_dev_unlock(hdev);

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

	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_ba(hdev, req.type, &req.bdaddr);
	if (conn) {
		bacpy(&ci.bdaddr, &conn->dst);
		ci.handle = conn->handle;
		ci.type  = conn->type;
		ci.out   = conn->out;
		ci.state = conn->state;
		ci.link_mode = get_link_mode(conn);
	}
	hci_dev_unlock(hdev);

	if (!conn)
		return -ENOENT;

	return copy_to_user(ptr, &ci, sizeof(ci)) ? -EFAULT : 0;
}

int hci_get_auth_info(struct hci_dev *hdev, void __user *arg)
{
	struct hci_auth_info_req req;
	struct hci_conn *conn;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;

	hci_dev_lock(hdev);
	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &req.bdaddr);
	if (conn)
		req.type = conn->auth_type;
	hci_dev_unlock(hdev);

	if (!conn)
		return -ENOENT;

	return copy_to_user(arg, &req, sizeof(req)) ? -EFAULT : 0;
}

struct hci_chan *hci_chan_create(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_chan *chan;

	BT_DBG("%s hcon %p", hdev->name, conn);

	chan = kzalloc(sizeof(struct hci_chan), GFP_KERNEL);
	if (!chan)
		return NULL;

	chan->conn = conn;
	skb_queue_head_init(&chan->data_q);
	chan->state = BT_CONNECTED;

	list_add_rcu(&chan->list, &conn->chan_list);

	return chan;
}

void hci_chan_del(struct hci_chan *chan)
{
	struct hci_conn *conn = chan->conn;
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("%s hcon %p chan %p", hdev->name, conn, chan);

	list_del_rcu(&chan->list);

	synchronize_rcu();

	hci_conn_drop(conn);

	skb_queue_purge(&chan->data_q);
	kfree(chan);
}

void hci_chan_list_flush(struct hci_conn *conn)
{
	struct hci_chan *chan, *n;

	BT_DBG("hcon %p", conn);

	list_for_each_entry_safe(chan, n, &conn->chan_list, list)
		hci_chan_del(chan);
}

static struct hci_chan *__hci_chan_lookup_handle(struct hci_conn *hcon,
						 __u16 handle)
{
	struct hci_chan *hchan;

	list_for_each_entry(hchan, &hcon->chan_list, list) {
		if (hchan->handle == handle)
			return hchan;
	}

	return NULL;
}

struct hci_chan *hci_chan_lookup_handle(struct hci_dev *hdev, __u16 handle)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn *hcon;
	struct hci_chan *hchan = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(hcon, &h->list, list) {
		hchan = __hci_chan_lookup_handle(hcon, handle);
		if (hchan)
			break;
	}

	rcu_read_unlock();

	return hchan;
}
