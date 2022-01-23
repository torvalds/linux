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
#include <linux/debugfs.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "hci_request.h"
#include "smp.h"
#include "a2mp.h"

struct sco_param {
	u16 pkt_type;
	u16 max_latency;
	u8  retrans_effort;
};

static const struct sco_param esco_param_cvsd[] = {
	{ EDR_ESCO_MASK & ~ESCO_2EV3, 0x000a,	0x01 }, /* S3 */
	{ EDR_ESCO_MASK & ~ESCO_2EV3, 0x0007,	0x01 }, /* S2 */
	{ EDR_ESCO_MASK | ESCO_EV3,   0x0007,	0x01 }, /* S1 */
	{ EDR_ESCO_MASK | ESCO_HV3,   0xffff,	0x01 }, /* D1 */
	{ EDR_ESCO_MASK | ESCO_HV1,   0xffff,	0x01 }, /* D0 */
};

static const struct sco_param sco_param_cvsd[] = {
	{ EDR_ESCO_MASK | ESCO_HV3,   0xffff,	0xff }, /* D1 */
	{ EDR_ESCO_MASK | ESCO_HV1,   0xffff,	0xff }, /* D0 */
};

static const struct sco_param esco_param_msbc[] = {
	{ EDR_ESCO_MASK & ~ESCO_2EV3, 0x000d,	0x02 }, /* T2 */
	{ EDR_ESCO_MASK | ESCO_EV3,   0x0008,	0x02 }, /* T1 */
};

/* This function requires the caller holds hdev->lock */
static void hci_connect_le_scan_cleanup(struct hci_conn *conn)
{
	struct hci_conn_params *params;
	struct hci_dev *hdev = conn->hdev;
	struct smp_irk *irk;
	bdaddr_t *bdaddr;
	u8 bdaddr_type;

	bdaddr = &conn->dst;
	bdaddr_type = conn->dst_type;

	/* Check if we need to convert to identity address */
	irk = hci_get_irk(hdev, bdaddr, bdaddr_type);
	if (irk) {
		bdaddr = &irk->bdaddr;
		bdaddr_type = irk->addr_type;
	}

	params = hci_pend_le_action_lookup(&hdev->pend_le_conns, bdaddr,
					   bdaddr_type);
	if (!params || !params->explicit_connect)
		return;

	/* The connection attempt was doing scan for new RPA, and is
	 * in scan phase. If params are not associated with any other
	 * autoconnect action, remove them completely. If they are, just unmark
	 * them as waiting for connection, by clearing explicit_connect field.
	 */
	params->explicit_connect = false;

	list_del_init(&params->action);

	switch (params->auto_connect) {
	case HCI_AUTO_CONN_EXPLICIT:
		hci_conn_params_del(hdev, bdaddr, bdaddr_type);
		/* return instead of break to avoid duplicate scan update */
		return;
	case HCI_AUTO_CONN_DIRECT:
	case HCI_AUTO_CONN_ALWAYS:
		list_add(&params->action, &hdev->pend_le_conns);
		break;
	case HCI_AUTO_CONN_REPORT:
		list_add(&params->action, &hdev->pend_le_reports);
		break;
	default:
		break;
	}

	hci_update_passive_scan(hdev);
}

static void hci_conn_cleanup(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	if (test_bit(HCI_CONN_PARAM_REMOVAL_PEND, &conn->flags))
		hci_conn_params_del(conn->hdev, &conn->dst, conn->dst_type);

	hci_chan_list_flush(conn);

	hci_conn_hash_del(hdev, conn);

	if (conn->type == SCO_LINK || conn->type == ESCO_LINK) {
		switch (conn->setting & SCO_AIRMODE_MASK) {
		case SCO_AIRMODE_CVSD:
		case SCO_AIRMODE_TRANSP:
			if (hdev->notify)
				hdev->notify(hdev, HCI_NOTIFY_DISABLE_SCO);
			break;
		}
	} else {
		if (hdev->notify)
			hdev->notify(hdev, HCI_NOTIFY_CONN_DEL);
	}

	hci_conn_del_sysfs(conn);

	debugfs_remove_recursive(conn->debugfs);

	hci_dev_put(hdev);

	hci_conn_put(conn);
}

static void le_scan_cleanup(struct work_struct *work)
{
	struct hci_conn *conn = container_of(work, struct hci_conn,
					     le_scan_cleanup);
	struct hci_dev *hdev = conn->hdev;
	struct hci_conn *c = NULL;

	BT_DBG("%s hcon %p", hdev->name, conn);

	hci_dev_lock(hdev);

	/* Check that the hci_conn is still around */
	rcu_read_lock();
	list_for_each_entry_rcu(c, &hdev->conn_hash.list, list) {
		if (c == conn)
			break;
	}
	rcu_read_unlock();

	if (c == conn) {
		hci_connect_le_scan_cleanup(conn);
		hci_conn_cleanup(conn);
	}

	hci_dev_unlock(hdev);
	hci_dev_put(hdev);
	hci_conn_put(conn);
}

static void hci_connect_le_scan_remove(struct hci_conn *conn)
{
	BT_DBG("%s hcon %p", conn->hdev->name, conn);

	/* We can't call hci_conn_del/hci_conn_cleanup here since that
	 * could deadlock with another hci_conn_del() call that's holding
	 * hci_dev_lock and doing cancel_delayed_work_sync(&conn->disc_work).
	 * Instead, grab temporary extra references to the hci_dev and
	 * hci_conn and perform the necessary cleanup in a separate work
	 * callback.
	 */

	hci_dev_hold(conn->hdev);
	hci_conn_get(conn);

	/* Even though we hold a reference to the hdev, many other
	 * things might get cleaned up meanwhile, including the hdev's
	 * own workqueue, so we can't use that for scheduling.
	 */
	schedule_work(&conn->le_scan_cleanup);
}

static void hci_acl_create_connection(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct inquiry_entry *ie;
	struct hci_cp_create_conn cp;

	BT_DBG("hcon %p", conn);

	/* Many controllers disallow HCI Create Connection while it is doing
	 * HCI Inquiry. So we cancel the Inquiry first before issuing HCI Create
	 * Connection. This may cause the MGMT discovering state to become false
	 * without user space's request but it is okay since the MGMT Discovery
	 * APIs do not promise that discovery should be done forever. Instead,
	 * the user space monitors the status of MGMT discovering and it may
	 * request for discovery again when this flag becomes false.
	 */
	if (test_bit(HCI_INQUIRY, &hdev->flags)) {
		/* Put this connection to "pending" state so that it will be
		 * executed after the inquiry cancel command complete event.
		 */
		conn->state = BT_CONNECT2;
		hci_send_cmd(hdev, HCI_OP_INQUIRY_CANCEL, 0, NULL);
		return;
	}

	conn->state = BT_CONNECT;
	conn->out = true;
	conn->role = HCI_ROLE_MASTER;

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
	}

	cp.pkt_type = cpu_to_le16(conn->pkt_type);
	if (lmp_rswitch_capable(hdev) && !(hdev->link_mode & HCI_LM_MASTER))
		cp.role_switch = 0x01;
	else
		cp.role_switch = 0x00;

	hci_send_cmd(hdev, HCI_OP_CREATE_CONN, sizeof(cp), &cp);
}

int hci_disconnect(struct hci_conn *conn, __u8 reason)
{
	BT_DBG("hcon %p", conn);

	/* When we are central of an established connection and it enters
	 * the disconnect timeout, then go ahead and try to read the
	 * current clock offset.  Processing of the result is done
	 * within the event handling and hci_clock_offset_evt function.
	 */
	if (conn->type == ACL_LINK && conn->role == HCI_ROLE_MASTER &&
	    (conn->state == BT_CONNECTED || conn->state == BT_CONFIG)) {
		struct hci_dev *hdev = conn->hdev;
		struct hci_cp_read_clock_offset clkoff_cp;

		clkoff_cp.handle = cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_READ_CLOCK_OFFSET, sizeof(clkoff_cp),
			     &clkoff_cp);
	}

	return hci_abort_conn(conn, reason);
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

static bool find_next_esco_param(struct hci_conn *conn,
				 const struct sco_param *esco_param, int size)
{
	for (; conn->attempt <= size; conn->attempt++) {
		if (lmp_esco_2m_capable(conn->link) ||
		    (esco_param[conn->attempt - 1].pkt_type & ESCO_2EV3))
			break;
		BT_DBG("hcon %p skipped attempt %d, eSCO 2M not supported",
		       conn, conn->attempt);
	}

	return conn->attempt <= size;
}

static bool hci_enhanced_setup_sync_conn(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_enhanced_setup_sync_conn cp;
	const struct sco_param *param;

	bt_dev_dbg(hdev, "hcon %p", conn);

	/* for offload use case, codec needs to configured before opening SCO */
	if (conn->codec.data_path)
		hci_req_configure_datapath(hdev, &conn->codec);

	conn->state = BT_CONNECT;
	conn->out = true;

	conn->attempt++;

	memset(&cp, 0x00, sizeof(cp));

	cp.handle   = cpu_to_le16(handle);

	cp.tx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.rx_bandwidth   = cpu_to_le32(0x00001f40);

	switch (conn->codec.id) {
	case BT_CODEC_MSBC:
		if (!find_next_esco_param(conn, esco_param_msbc,
					  ARRAY_SIZE(esco_param_msbc)))
			return false;

		param = &esco_param_msbc[conn->attempt - 1];
		cp.tx_coding_format.id = 0x05;
		cp.rx_coding_format.id = 0x05;
		cp.tx_codec_frame_size = __cpu_to_le16(60);
		cp.rx_codec_frame_size = __cpu_to_le16(60);
		cp.in_bandwidth = __cpu_to_le32(32000);
		cp.out_bandwidth = __cpu_to_le32(32000);
		cp.in_coding_format.id = 0x04;
		cp.out_coding_format.id = 0x04;
		cp.in_coded_data_size = __cpu_to_le16(16);
		cp.out_coded_data_size = __cpu_to_le16(16);
		cp.in_pcm_data_format = 2;
		cp.out_pcm_data_format = 2;
		cp.in_pcm_sample_payload_msb_pos = 0;
		cp.out_pcm_sample_payload_msb_pos = 0;
		cp.in_data_path = conn->codec.data_path;
		cp.out_data_path = conn->codec.data_path;
		cp.in_transport_unit_size = 1;
		cp.out_transport_unit_size = 1;
		break;

	case BT_CODEC_TRANSPARENT:
		if (!find_next_esco_param(conn, esco_param_msbc,
					  ARRAY_SIZE(esco_param_msbc)))
			return false;
		param = &esco_param_msbc[conn->attempt - 1];
		cp.tx_coding_format.id = 0x03;
		cp.rx_coding_format.id = 0x03;
		cp.tx_codec_frame_size = __cpu_to_le16(60);
		cp.rx_codec_frame_size = __cpu_to_le16(60);
		cp.in_bandwidth = __cpu_to_le32(0x1f40);
		cp.out_bandwidth = __cpu_to_le32(0x1f40);
		cp.in_coding_format.id = 0x03;
		cp.out_coding_format.id = 0x03;
		cp.in_coded_data_size = __cpu_to_le16(16);
		cp.out_coded_data_size = __cpu_to_le16(16);
		cp.in_pcm_data_format = 2;
		cp.out_pcm_data_format = 2;
		cp.in_pcm_sample_payload_msb_pos = 0;
		cp.out_pcm_sample_payload_msb_pos = 0;
		cp.in_data_path = conn->codec.data_path;
		cp.out_data_path = conn->codec.data_path;
		cp.in_transport_unit_size = 1;
		cp.out_transport_unit_size = 1;
		break;

	case BT_CODEC_CVSD:
		if (lmp_esco_capable(conn->link)) {
			if (!find_next_esco_param(conn, esco_param_cvsd,
						  ARRAY_SIZE(esco_param_cvsd)))
				return false;
			param = &esco_param_cvsd[conn->attempt - 1];
		} else {
			if (conn->attempt > ARRAY_SIZE(sco_param_cvsd))
				return false;
			param = &sco_param_cvsd[conn->attempt - 1];
		}
		cp.tx_coding_format.id = 2;
		cp.rx_coding_format.id = 2;
		cp.tx_codec_frame_size = __cpu_to_le16(60);
		cp.rx_codec_frame_size = __cpu_to_le16(60);
		cp.in_bandwidth = __cpu_to_le32(16000);
		cp.out_bandwidth = __cpu_to_le32(16000);
		cp.in_coding_format.id = 4;
		cp.out_coding_format.id = 4;
		cp.in_coded_data_size = __cpu_to_le16(16);
		cp.out_coded_data_size = __cpu_to_le16(16);
		cp.in_pcm_data_format = 2;
		cp.out_pcm_data_format = 2;
		cp.in_pcm_sample_payload_msb_pos = 0;
		cp.out_pcm_sample_payload_msb_pos = 0;
		cp.in_data_path = conn->codec.data_path;
		cp.out_data_path = conn->codec.data_path;
		cp.in_transport_unit_size = 16;
		cp.out_transport_unit_size = 16;
		break;
	default:
		return false;
	}

	cp.retrans_effort = param->retrans_effort;
	cp.pkt_type = __cpu_to_le16(param->pkt_type);
	cp.max_latency = __cpu_to_le16(param->max_latency);

	if (hci_send_cmd(hdev, HCI_OP_ENHANCED_SETUP_SYNC_CONN, sizeof(cp), &cp) < 0)
		return false;

	return true;
}

static bool hci_setup_sync_conn(struct hci_conn *conn, __u16 handle)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_setup_sync_conn cp;
	const struct sco_param *param;

	bt_dev_dbg(hdev, "hcon %p", conn);

	conn->state = BT_CONNECT;
	conn->out = true;

	conn->attempt++;

	cp.handle   = cpu_to_le16(handle);

	cp.tx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.rx_bandwidth   = cpu_to_le32(0x00001f40);
	cp.voice_setting  = cpu_to_le16(conn->setting);

	switch (conn->setting & SCO_AIRMODE_MASK) {
	case SCO_AIRMODE_TRANSP:
		if (!find_next_esco_param(conn, esco_param_msbc,
					  ARRAY_SIZE(esco_param_msbc)))
			return false;
		param = &esco_param_msbc[conn->attempt - 1];
		break;
	case SCO_AIRMODE_CVSD:
		if (lmp_esco_capable(conn->link)) {
			if (!find_next_esco_param(conn, esco_param_cvsd,
						  ARRAY_SIZE(esco_param_cvsd)))
				return false;
			param = &esco_param_cvsd[conn->attempt - 1];
		} else {
			if (conn->attempt > ARRAY_SIZE(sco_param_cvsd))
				return false;
			param = &sco_param_cvsd[conn->attempt - 1];
		}
		break;
	default:
		return false;
	}

	cp.retrans_effort = param->retrans_effort;
	cp.pkt_type = __cpu_to_le16(param->pkt_type);
	cp.max_latency = __cpu_to_le16(param->max_latency);

	if (hci_send_cmd(hdev, HCI_OP_SETUP_SYNC_CONN, sizeof(cp), &cp) < 0)
		return false;

	return true;
}

bool hci_setup_sync(struct hci_conn *conn, __u16 handle)
{
	if (enhanced_sco_capable(conn->hdev))
		return hci_enhanced_setup_sync_conn(conn, handle);

	return hci_setup_sync_conn(conn, handle);
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
		      __u8 ltk[16], __u8 key_size)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_le_start_enc cp;

	BT_DBG("hcon %p", conn);

	memset(&cp, 0, sizeof(cp));

	cp.handle = cpu_to_le16(conn->handle);
	cp.rand = rand;
	cp.ediv = ediv;
	memcpy(cp.ltk, ltk, key_size);

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
		hci_connect_cfm(sco, status);
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

	/* LE connections in scanning state need special handling */
	if (conn->state == BT_CONNECT && conn->type == LE_LINK &&
	    test_bit(HCI_CONN_SCANNING, &conn->flags)) {
		hci_connect_le_scan_remove(conn);
		return;
	}

	hci_abort_conn(conn, hci_proto_disconn_ind(conn));
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

static void le_disable_advertising(struct hci_dev *hdev)
{
	if (ext_adv_capable(hdev)) {
		struct hci_cp_le_set_ext_adv_enable cp;

		cp.enable = 0x00;
		cp.num_of_sets = 0x00;

		hci_send_cmd(hdev, HCI_OP_LE_SET_EXT_ADV_ENABLE, sizeof(cp),
			     &cp);
	} else {
		u8 enable = 0x00;
		hci_send_cmd(hdev, HCI_OP_LE_SET_ADV_ENABLE, sizeof(enable),
			     &enable);
	}
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
	if (conn->role == HCI_ROLE_SLAVE) {
		/* Disable LE Advertising */
		le_disable_advertising(hdev);
		hci_le_conn_failed(conn, HCI_ERROR_ADVERTISING_TIMEOUT);
		return;
	}

	hci_abort_conn(conn, HCI_ERROR_REMOTE_USER_TERM);
}

struct hci_conn *hci_conn_add(struct hci_dev *hdev, int type, bdaddr_t *dst,
			      u8 role)
{
	struct hci_conn *conn;

	BT_DBG("%s dst %pMR", hdev->name, dst);

	conn = kzalloc(sizeof(*conn), GFP_KERNEL);
	if (!conn)
		return NULL;

	bacpy(&conn->dst, dst);
	bacpy(&conn->src, &hdev->bdaddr);
	conn->handle = HCI_CONN_HANDLE_UNSET;
	conn->hdev  = hdev;
	conn->type  = type;
	conn->role  = role;
	conn->mode  = HCI_CM_ACTIVE;
	conn->state = BT_OPEN;
	conn->auth_type = HCI_AT_GENERAL_BONDING;
	conn->io_capability = hdev->io_capability;
	conn->remote_auth = 0xff;
	conn->key_type = 0xff;
	conn->rssi = HCI_RSSI_INVALID;
	conn->tx_power = HCI_TX_POWER_INVALID;
	conn->max_tx_power = HCI_TX_POWER_INVALID;

	set_bit(HCI_CONN_POWER_SAVE, &conn->flags);
	conn->disc_timeout = HCI_DISCONN_TIMEOUT;

	/* Set Default Authenticated payload timeout to 30s */
	conn->auth_payload_timeout = DEFAULT_AUTH_PAYLOAD_TIMEOUT;

	if (conn->role == HCI_ROLE_MASTER)
		conn->out = true;

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
	INIT_WORK(&conn->le_scan_cleanup, le_scan_cleanup);

	atomic_set(&conn->refcnt, 0);

	hci_dev_hold(hdev);

	hci_conn_hash_add(hdev, conn);

	/* The SCO and eSCO connections will only be notified when their
	 * setup has been completed. This is different to ACL links which
	 * can be notified right away.
	 */
	if (conn->type != SCO_LINK && conn->type != ESCO_LINK) {
		if (hdev->notify)
			hdev->notify(hdev, HCI_NOTIFY_CONN_ADD);
	}

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
		cancel_delayed_work(&conn->le_conn_timeout);

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

	if (conn->amp_mgr)
		amp_mgr_put(conn->amp_mgr);

	skb_queue_purge(&conn->data_q);

	/* Remove the connection from the list and cleanup its remaining
	 * state. This is a separate function since for some cases like
	 * BT_CONNECT_SCAN we *only* want the cleanup part without the
	 * rest of hci_conn_del.
	 */
	hci_conn_cleanup(conn);

	return 0;
}

struct hci_dev *hci_get_route(bdaddr_t *dst, bdaddr_t *src, uint8_t src_type)
{
	int use_src = bacmp(src, BDADDR_ANY);
	struct hci_dev *hdev = NULL, *d;

	BT_DBG("%pMR -> %pMR", src, dst);

	read_lock(&hci_dev_list_lock);

	list_for_each_entry(d, &hci_dev_list, list) {
		if (!test_bit(HCI_UP, &d->flags) ||
		    hci_dev_test_flag(d, HCI_USER_CHANNEL) ||
		    d->dev_type != HCI_PRIMARY)
			continue;

		/* Simple routing:
		 *   No source address - find interface with bdaddr != dst
		 *   Source address    - find interface with bdaddr == src
		 */

		if (use_src) {
			bdaddr_t id_addr;
			u8 id_addr_type;

			if (src_type == BDADDR_BREDR) {
				if (!lmp_bredr_capable(d))
					continue;
				bacpy(&id_addr, &d->bdaddr);
				id_addr_type = BDADDR_BREDR;
			} else {
				if (!lmp_le_capable(d))
					continue;

				hci_copy_identity_address(d, &id_addr,
							  &id_addr_type);

				/* Convert from HCI to three-value type */
				if (id_addr_type == ADDR_LE_DEV_PUBLIC)
					id_addr_type = BDADDR_LE_PUBLIC;
				else
					id_addr_type = BDADDR_LE_RANDOM;
			}

			if (!bacmp(&id_addr, src) && id_addr_type == src_type) {
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
	struct hci_conn_params *params;

	params = hci_pend_le_action_lookup(&hdev->pend_le_conns, &conn->dst,
					   conn->dst_type);
	if (params && params->conn) {
		hci_conn_drop(params->conn);
		hci_conn_put(params->conn);
		params->conn = NULL;
	}

	conn->state = BT_CLOSED;

	/* If the status indicates successful cancellation of
	 * the attempt (i.e. Unknown Connection Id) there's no point of
	 * notifying failure since we'll go back to keep trying to
	 * connect. The only exception is explicit connect requests
	 * where a timeout + cancel does indicate an actual failure.
	 */
	if (status != HCI_ERROR_UNKNOWN_CONN_ID ||
	    (params && params->explicit_connect))
		mgmt_connect_failed(hdev, &conn->dst, conn->type,
				    conn->dst_type, status);

	hci_connect_cfm(conn, status);

	hci_conn_del(conn);

	/* Since we may have temporarily stopped the background scanning in
	 * favor of connection establishment, we should restart it.
	 */
	hci_update_passive_scan(hdev);

	/* Enable advertising in case this was a failed connection
	 * attempt as a peripheral.
	 */
	hci_enable_advertising(hdev);
}

static void create_le_conn_complete(struct hci_dev *hdev, void *data, int err)
{
	struct hci_conn *conn = data;

	hci_dev_lock(hdev);

	if (!err) {
		hci_connect_le_scan_cleanup(conn);
		goto done;
	}

	bt_dev_err(hdev, "request failed to create LE connection: err %d", err);

	if (!conn)
		goto done;

	hci_le_conn_failed(conn, err);

done:
	hci_dev_unlock(hdev);
}

static int hci_connect_le_sync(struct hci_dev *hdev, void *data)
{
	struct hci_conn *conn = data;

	bt_dev_dbg(hdev, "conn %p", conn);

	return hci_le_create_conn_sync(hdev, conn);
}

struct hci_conn *hci_connect_le(struct hci_dev *hdev, bdaddr_t *dst,
				u8 dst_type, bool dst_resolved, u8 sec_level,
				u16 conn_timeout, u8 role)
{
	struct hci_conn *conn;
	struct smp_irk *irk;
	int err;

	/* Let's make sure that le is enabled.*/
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED)) {
		if (lmp_le_capable(hdev))
			return ERR_PTR(-ECONNREFUSED);

		return ERR_PTR(-EOPNOTSUPP);
	}

	/* Since the controller supports only one LE connection attempt at a
	 * time, we return -EBUSY if there is any connection attempt running.
	 */
	if (hci_lookup_le_connect(hdev))
		return ERR_PTR(-EBUSY);

	/* If there's already a connection object but it's not in
	 * scanning state it means it must already be established, in
	 * which case we can't do anything else except report a failure
	 * to connect.
	 */
	conn = hci_conn_hash_lookup_le(hdev, dst, dst_type);
	if (conn && !test_bit(HCI_CONN_SCANNING, &conn->flags)) {
		return ERR_PTR(-EBUSY);
	}

	/* Check if the destination address has been resolved by the controller
	 * since if it did then the identity address shall be used.
	 */
	if (!dst_resolved) {
		/* When given an identity address with existing identity
		 * resolving key, the connection needs to be established
		 * to a resolvable random address.
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
	}

	if (conn) {
		bacpy(&conn->dst, dst);
	} else {
		conn = hci_conn_add(hdev, LE_LINK, dst, role);
		if (!conn)
			return ERR_PTR(-ENOMEM);
		hci_conn_hold(conn);
		conn->pending_sec_level = sec_level;
	}

	conn->dst_type = dst_type;
	conn->sec_level = BT_SECURITY_LOW;
	conn->conn_timeout = conn_timeout;

	conn->state = BT_CONNECT;
	clear_bit(HCI_CONN_SCANNING, &conn->flags);

	err = hci_cmd_sync_queue(hdev, hci_connect_le_sync, conn,
				 create_le_conn_complete);
	if (err) {
		hci_conn_del(conn);
		return ERR_PTR(err);
	}

	return conn;
}

static bool is_connected(struct hci_dev *hdev, bdaddr_t *addr, u8 type)
{
	struct hci_conn *conn;

	conn = hci_conn_hash_lookup_le(hdev, addr, type);
	if (!conn)
		return false;

	if (conn->state != BT_CONNECTED)
		return false;

	return true;
}

/* This function requires the caller holds hdev->lock */
static int hci_explicit_conn_params_set(struct hci_dev *hdev,
					bdaddr_t *addr, u8 addr_type)
{
	struct hci_conn_params *params;

	if (is_connected(hdev, addr, addr_type))
		return -EISCONN;

	params = hci_conn_params_lookup(hdev, addr, addr_type);
	if (!params) {
		params = hci_conn_params_add(hdev, addr, addr_type);
		if (!params)
			return -ENOMEM;

		/* If we created new params, mark them to be deleted in
		 * hci_connect_le_scan_cleanup. It's different case than
		 * existing disabled params, those will stay after cleanup.
		 */
		params->auto_connect = HCI_AUTO_CONN_EXPLICIT;
	}

	/* We're trying to connect, so make sure params are at pend_le_conns */
	if (params->auto_connect == HCI_AUTO_CONN_DISABLED ||
	    params->auto_connect == HCI_AUTO_CONN_REPORT ||
	    params->auto_connect == HCI_AUTO_CONN_EXPLICIT) {
		list_del_init(&params->action);
		list_add(&params->action, &hdev->pend_le_conns);
	}

	params->explicit_connect = true;

	BT_DBG("addr %pMR (type %u) auto_connect %u", addr, addr_type,
	       params->auto_connect);

	return 0;
}

/* This function requires the caller holds hdev->lock */
struct hci_conn *hci_connect_le_scan(struct hci_dev *hdev, bdaddr_t *dst,
				     u8 dst_type, u8 sec_level,
				     u16 conn_timeout,
				     enum conn_reasons conn_reason)
{
	struct hci_conn *conn;

	/* Let's make sure that le is enabled.*/
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED)) {
		if (lmp_le_capable(hdev))
			return ERR_PTR(-ECONNREFUSED);

		return ERR_PTR(-EOPNOTSUPP);
	}

	/* Some devices send ATT messages as soon as the physical link is
	 * established. To be able to handle these ATT messages, the user-
	 * space first establishes the connection and then starts the pairing
	 * process.
	 *
	 * So if a hci_conn object already exists for the following connection
	 * attempt, we simply update pending_sec_level and auth_type fields
	 * and return the object found.
	 */
	conn = hci_conn_hash_lookup_le(hdev, dst, dst_type);
	if (conn) {
		if (conn->pending_sec_level < sec_level)
			conn->pending_sec_level = sec_level;
		goto done;
	}

	BT_DBG("requesting refresh of dst_addr");

	conn = hci_conn_add(hdev, LE_LINK, dst, HCI_ROLE_MASTER);
	if (!conn)
		return ERR_PTR(-ENOMEM);

	if (hci_explicit_conn_params_set(hdev, dst, dst_type) < 0) {
		hci_conn_del(conn);
		return ERR_PTR(-EBUSY);
	}

	conn->state = BT_CONNECT;
	set_bit(HCI_CONN_SCANNING, &conn->flags);
	conn->dst_type = dst_type;
	conn->sec_level = BT_SECURITY_LOW;
	conn->pending_sec_level = sec_level;
	conn->conn_timeout = conn_timeout;
	conn->conn_reason = conn_reason;

	hci_update_passive_scan(hdev);

done:
	hci_conn_hold(conn);
	return conn;
}

struct hci_conn *hci_connect_acl(struct hci_dev *hdev, bdaddr_t *dst,
				 u8 sec_level, u8 auth_type,
				 enum conn_reasons conn_reason)
{
	struct hci_conn *acl;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED)) {
		if (lmp_bredr_capable(hdev))
			return ERR_PTR(-ECONNREFUSED);

		return ERR_PTR(-EOPNOTSUPP);
	}

	acl = hci_conn_hash_lookup_ba(hdev, ACL_LINK, dst);
	if (!acl) {
		acl = hci_conn_add(hdev, ACL_LINK, dst, HCI_ROLE_MASTER);
		if (!acl)
			return ERR_PTR(-ENOMEM);
	}

	hci_conn_hold(acl);

	acl->conn_reason = conn_reason;
	if (acl->state == BT_OPEN || acl->state == BT_CLOSED) {
		acl->sec_level = BT_SECURITY_LOW;
		acl->pending_sec_level = sec_level;
		acl->auth_type = auth_type;
		hci_acl_create_connection(acl);
	}

	return acl;
}

struct hci_conn *hci_connect_sco(struct hci_dev *hdev, int type, bdaddr_t *dst,
				 __u16 setting, struct bt_codec *codec)
{
	struct hci_conn *acl;
	struct hci_conn *sco;

	acl = hci_connect_acl(hdev, dst, BT_SECURITY_LOW, HCI_AT_NO_BONDING,
			      CONN_REASON_SCO_CONNECT);
	if (IS_ERR(acl))
		return acl;

	sco = hci_conn_hash_lookup_ba(hdev, type, dst);
	if (!sco) {
		sco = hci_conn_add(hdev, type, dst, HCI_ROLE_MASTER);
		if (!sco) {
			hci_conn_drop(acl);
			return ERR_PTR(-ENOMEM);
		}
	}

	acl->link = sco;
	sco->link = acl;

	hci_conn_hold(sco);

	sco->setting = setting;
	sco->codec = *codec;

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
	if (hci_dev_test_flag(conn->hdev, HCI_SC_ONLY)) {
		if (!hci_conn_sc_enabled(conn) ||
		    !test_bit(HCI_CONN_AES_CCM, &conn->flags) ||
		    conn->key_type != HCI_LK_AUTH_COMBINATION_P256)
			return 0;
	}

	 /* AES encryption is required for Level 4:
	  *
	  * BLUETOOTH CORE SPECIFICATION Version 5.2 | Vol 3, Part C
	  * page 1319:
	  *
	  * 128-bit equivalent strength for link and encryption keys
	  * required using FIPS approved algorithms (E0 not allowed,
	  * SAFER+ not allowed, and P-192 not allowed; encryption key
	  * not shortened)
	  */
	if (conn->sec_level == BT_SECURITY_FIPS &&
	    !test_bit(HCI_CONN_AES_CCM, &conn->flags)) {
		bt_dev_err(conn->hdev,
			   "Invalid security: Missing AES-CCM usage");
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

/* Encrypt the link */
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
int hci_conn_security(struct hci_conn *conn, __u8 sec_level, __u8 auth_type,
		      bool initiator)
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

	if (initiator)
		set_bit(HCI_CONN_AUTH_INITIATOR, &conn->flags);

	if (!hci_conn_auth(conn, sec_level, auth_type))
		return 0;

encrypt:
	if (test_bit(HCI_CONN_ENCRYPT, &conn->flags)) {
		/* Ensure that the encryption key size has been read,
		 * otherwise stall the upper layer responses.
		 */
		if (!conn->enc_key_size)
			return 0;

		/* Nothing else needed, all requirements are met */
		return 1;
	}

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

/* Switch role */
int hci_conn_switch_role(struct hci_conn *conn, __u8 role)
{
	BT_DBG("hcon %p", conn);

	if (role == conn->role)
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

		hci_disconn_cfm(c, HCI_ERROR_LOCAL_HOST_TERM);
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

	if (conn->role == HCI_ROLE_MASTER)
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

	if (test_bit(HCI_CONN_DROP, &conn->flags)) {
		BT_DBG("Refusing to create new hci_chan");
		return NULL;
	}

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return NULL;

	chan->conn = hci_conn_get(conn);
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

	/* Prevent new hci_chan's to be created for this hci_conn */
	set_bit(HCI_CONN_DROP, &conn->flags);

	hci_conn_put(conn);

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

u32 hci_conn_get_phy(struct hci_conn *conn)
{
	u32 phys = 0;

	/* BLUETOOTH CORE SPECIFICATION Version 5.2 | Vol 2, Part B page 471:
	 * Table 6.2: Packets defined for synchronous, asynchronous, and
	 * CPB logical transport types.
	 */
	switch (conn->type) {
	case SCO_LINK:
		/* SCO logical transport (1 Mb/s):
		 * HV1, HV2, HV3 and DV.
		 */
		phys |= BT_PHY_BR_1M_1SLOT;

		break;

	case ACL_LINK:
		/* ACL logical transport (1 Mb/s) ptt=0:
		 * DH1, DM3, DH3, DM5 and DH5.
		 */
		phys |= BT_PHY_BR_1M_1SLOT;

		if (conn->pkt_type & (HCI_DM3 | HCI_DH3))
			phys |= BT_PHY_BR_1M_3SLOT;

		if (conn->pkt_type & (HCI_DM5 | HCI_DH5))
			phys |= BT_PHY_BR_1M_5SLOT;

		/* ACL logical transport (2 Mb/s) ptt=1:
		 * 2-DH1, 2-DH3 and 2-DH5.
		 */
		if (!(conn->pkt_type & HCI_2DH1))
			phys |= BT_PHY_EDR_2M_1SLOT;

		if (!(conn->pkt_type & HCI_2DH3))
			phys |= BT_PHY_EDR_2M_3SLOT;

		if (!(conn->pkt_type & HCI_2DH5))
			phys |= BT_PHY_EDR_2M_5SLOT;

		/* ACL logical transport (3 Mb/s) ptt=1:
		 * 3-DH1, 3-DH3 and 3-DH5.
		 */
		if (!(conn->pkt_type & HCI_3DH1))
			phys |= BT_PHY_EDR_3M_1SLOT;

		if (!(conn->pkt_type & HCI_3DH3))
			phys |= BT_PHY_EDR_3M_3SLOT;

		if (!(conn->pkt_type & HCI_3DH5))
			phys |= BT_PHY_EDR_3M_5SLOT;

		break;

	case ESCO_LINK:
		/* eSCO logical transport (1 Mb/s): EV3, EV4 and EV5 */
		phys |= BT_PHY_BR_1M_1SLOT;

		if (!(conn->pkt_type & (ESCO_EV4 | ESCO_EV5)))
			phys |= BT_PHY_BR_1M_3SLOT;

		/* eSCO logical transport (2 Mb/s): 2-EV3, 2-EV5 */
		if (!(conn->pkt_type & ESCO_2EV3))
			phys |= BT_PHY_EDR_2M_1SLOT;

		if (!(conn->pkt_type & ESCO_2EV5))
			phys |= BT_PHY_EDR_2M_3SLOT;

		/* eSCO logical transport (3 Mb/s): 3-EV3, 3-EV5 */
		if (!(conn->pkt_type & ESCO_3EV3))
			phys |= BT_PHY_EDR_3M_1SLOT;

		if (!(conn->pkt_type & ESCO_3EV5))
			phys |= BT_PHY_EDR_3M_3SLOT;

		break;

	case LE_LINK:
		if (conn->le_tx_phy & HCI_LE_SET_PHY_1M)
			phys |= BT_PHY_LE_1M_TX;

		if (conn->le_rx_phy & HCI_LE_SET_PHY_1M)
			phys |= BT_PHY_LE_1M_RX;

		if (conn->le_tx_phy & HCI_LE_SET_PHY_2M)
			phys |= BT_PHY_LE_2M_TX;

		if (conn->le_rx_phy & HCI_LE_SET_PHY_2M)
			phys |= BT_PHY_LE_2M_RX;

		if (conn->le_tx_phy & HCI_LE_SET_PHY_CODED)
			phys |= BT_PHY_LE_CODED_TX;

		if (conn->le_rx_phy & HCI_LE_SET_PHY_CODED)
			phys |= BT_PHY_LE_CODED_RX;

		break;
	}

	return phys;
}
