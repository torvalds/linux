/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (c) 2000-2001, 2010, Code Aurora Forum. All rights reserved.
   Copyright 2023 NXP

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
#include <net/bluetooth/iso.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "smp.h"
#include "a2mp.h"
#include "eir.h"

struct sco_param {
	u16 pkt_type;
	u16 max_latency;
	u8  retrans_effort;
};

struct conn_handle_t {
	struct hci_conn *conn;
	__u16 handle;
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
static void hci_connect_le_scan_cleanup(struct hci_conn *conn, u8 status)
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
	if (!params)
		return;

	if (params->conn) {
		hci_conn_drop(params->conn);
		hci_conn_put(params->conn);
		params->conn = NULL;
	}

	if (!params->explicit_connect)
		return;

	/* If the status indicates successful cancellation of
	 * the attempt (i.e. Unknown Connection Id) there's no point of
	 * notifying failure since we'll go back to keep trying to
	 * connect. The only exception is explicit connect requests
	 * where a timeout + cancel does indicate an actual failure.
	 */
	if (status && status != HCI_ERROR_UNKNOWN_CONN_ID)
		mgmt_connect_failed(hdev, &conn->dst, conn->type,
				    conn->dst_type, status);

	/* The connection attempt was doing scan for new RPA, and is
	 * in scan phase. If params are not associated with any other
	 * autoconnect action, remove them completely. If they are, just unmark
	 * them as waiting for connection, by clearing explicit_connect field.
	 */
	params->explicit_connect = false;

	hci_pend_le_list_del_init(params);

	switch (params->auto_connect) {
	case HCI_AUTO_CONN_EXPLICIT:
		hci_conn_params_del(hdev, bdaddr, bdaddr_type);
		/* return instead of break to avoid duplicate scan update */
		return;
	case HCI_AUTO_CONN_DIRECT:
	case HCI_AUTO_CONN_ALWAYS:
		hci_pend_le_list_add(params, &hdev->pend_le_conns);
		break;
	case HCI_AUTO_CONN_REPORT:
		hci_pend_le_list_add(params, &hdev->pend_le_reports);
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

	if (test_and_clear_bit(HCI_CONN_FLUSH_KEY, &conn->flags))
		hci_remove_link_key(hdev, &conn->dst);

	hci_chan_list_flush(conn);

	hci_conn_hash_del(hdev, conn);

	if (conn->cleanup)
		conn->cleanup(conn);

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
	if (!conn->parent)
		return false;

	for (; conn->attempt <= size; conn->attempt++) {
		if (lmp_esco_2m_capable(conn->parent) ||
		    (esco_param[conn->attempt - 1].pkt_type & ESCO_2EV3))
			break;
		BT_DBG("hcon %p skipped attempt %d, eSCO 2M not supported",
		       conn, conn->attempt);
	}

	return conn->attempt <= size;
}

static int configure_datapath_sync(struct hci_dev *hdev, struct bt_codec *codec)
{
	int err;
	__u8 vnd_len, *vnd_data = NULL;
	struct hci_op_configure_data_path *cmd = NULL;

	err = hdev->get_codec_config_data(hdev, ESCO_LINK, codec, &vnd_len,
					  &vnd_data);
	if (err < 0)
		goto error;

	cmd = kzalloc(sizeof(*cmd) + vnd_len, GFP_KERNEL);
	if (!cmd) {
		err = -ENOMEM;
		goto error;
	}

	err = hdev->get_data_path_id(hdev, &cmd->data_path_id);
	if (err < 0)
		goto error;

	cmd->vnd_len = vnd_len;
	memcpy(cmd->vnd_data, vnd_data, vnd_len);

	cmd->direction = 0x00;
	__hci_cmd_sync_status(hdev, HCI_CONFIGURE_DATA_PATH,
			      sizeof(*cmd) + vnd_len, cmd, HCI_CMD_TIMEOUT);

	cmd->direction = 0x01;
	err = __hci_cmd_sync_status(hdev, HCI_CONFIGURE_DATA_PATH,
				    sizeof(*cmd) + vnd_len, cmd,
				    HCI_CMD_TIMEOUT);
error:

	kfree(cmd);
	kfree(vnd_data);
	return err;
}

static int hci_enhanced_setup_sync(struct hci_dev *hdev, void *data)
{
	struct conn_handle_t *conn_handle = data;
	struct hci_conn *conn = conn_handle->conn;
	__u16 handle = conn_handle->handle;
	struct hci_cp_enhanced_setup_sync_conn cp;
	const struct sco_param *param;

	kfree(conn_handle);

	bt_dev_dbg(hdev, "hcon %p", conn);

	/* for offload use case, codec needs to configured before opening SCO */
	if (conn->codec.data_path)
		configure_datapath_sync(hdev, &conn->codec);

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
			return -EINVAL;

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
		if (conn->parent && lmp_esco_capable(conn->parent)) {
			if (!find_next_esco_param(conn, esco_param_cvsd,
						  ARRAY_SIZE(esco_param_cvsd)))
				return -EINVAL;
			param = &esco_param_cvsd[conn->attempt - 1];
		} else {
			if (conn->attempt > ARRAY_SIZE(sco_param_cvsd))
				return -EINVAL;
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
		return -EINVAL;
	}

	cp.retrans_effort = param->retrans_effort;
	cp.pkt_type = __cpu_to_le16(param->pkt_type);
	cp.max_latency = __cpu_to_le16(param->max_latency);

	if (hci_send_cmd(hdev, HCI_OP_ENHANCED_SETUP_SYNC_CONN, sizeof(cp), &cp) < 0)
		return -EIO;

	return 0;
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
		if (conn->parent && lmp_esco_capable(conn->parent)) {
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
	int result;
	struct conn_handle_t *conn_handle;

	if (enhanced_sync_conn_capable(conn->hdev)) {
		conn_handle = kzalloc(sizeof(*conn_handle), GFP_KERNEL);

		if (!conn_handle)
			return false;

		conn_handle->conn = conn;
		conn_handle->handle = handle;
		result = hci_cmd_sync_queue(conn->hdev, hci_enhanced_setup_sync,
					    conn_handle, NULL);
		if (result < 0)
			kfree(conn_handle);

		return result == 0;
	}

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
	struct hci_link *link;

	link = list_first_entry_or_null(&conn->link_list, struct hci_link, list);
	if (!link || !link->conn)
		return;

	BT_DBG("hcon %p", conn);

	if (!status) {
		if (lmp_esco_capable(conn->hdev))
			hci_setup_sync(link->conn, conn->handle);
		else
			hci_add_sco(link->conn, conn->handle);
	} else {
		hci_connect_cfm(link->conn, status);
		hci_conn_del(link->conn);
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
		hci_dev_lock(hdev);
		hci_conn_failed(conn, HCI_ERROR_ADVERTISING_TIMEOUT);
		hci_dev_unlock(hdev);
		return;
	}

	hci_abort_conn(conn, HCI_ERROR_REMOTE_USER_TERM);
}

struct iso_cig_params {
	struct hci_cp_le_set_cig_params cp;
	struct hci_cis_params cis[0x1f];
};

struct iso_list_data {
	union {
		u8  cig;
		u8  big;
	};
	union {
		u8  cis;
		u8  bis;
		u16 sync_handle;
	};
	int count;
	bool big_term;
	bool big_sync_term;
};

static void bis_list(struct hci_conn *conn, void *data)
{
	struct iso_list_data *d = data;

	/* Skip if not broadcast/ANY address */
	if (bacmp(&conn->dst, BDADDR_ANY))
		return;

	if (d->big != conn->iso_qos.bcast.big || d->bis == BT_ISO_QOS_BIS_UNSET ||
	    d->bis != conn->iso_qos.bcast.bis)
		return;

	d->count++;
}

static int terminate_big_sync(struct hci_dev *hdev, void *data)
{
	struct iso_list_data *d = data;

	bt_dev_dbg(hdev, "big 0x%2.2x bis 0x%2.2x", d->big, d->bis);

	hci_remove_ext_adv_instance_sync(hdev, d->bis, NULL);

	/* Only terminate BIG if it has been created */
	if (!d->big_term)
		return 0;

	return hci_le_terminate_big_sync(hdev, d->big,
					 HCI_ERROR_LOCAL_HOST_TERM);
}

static void terminate_big_destroy(struct hci_dev *hdev, void *data, int err)
{
	kfree(data);
}

static int hci_le_terminate_big(struct hci_dev *hdev, struct hci_conn *conn)
{
	struct iso_list_data *d;
	int ret;

	bt_dev_dbg(hdev, "big 0x%2.2x bis 0x%2.2x", conn->iso_qos.bcast.big,
		   conn->iso_qos.bcast.bis);

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->big = conn->iso_qos.bcast.big;
	d->bis = conn->iso_qos.bcast.bis;
	d->big_term = test_and_clear_bit(HCI_CONN_BIG_CREATED, &conn->flags);

	ret = hci_cmd_sync_queue(hdev, terminate_big_sync, d,
				 terminate_big_destroy);
	if (ret)
		kfree(d);

	return ret;
}

static int big_terminate_sync(struct hci_dev *hdev, void *data)
{
	struct iso_list_data *d = data;

	bt_dev_dbg(hdev, "big 0x%2.2x sync_handle 0x%4.4x", d->big,
		   d->sync_handle);

	if (d->big_sync_term)
		hci_le_big_terminate_sync(hdev, d->big);

	return hci_le_pa_terminate_sync(hdev, d->sync_handle);
}

static int hci_le_big_terminate(struct hci_dev *hdev, u8 big, struct hci_conn *conn)
{
	struct iso_list_data *d;
	int ret;

	bt_dev_dbg(hdev, "big 0x%2.2x sync_handle 0x%4.4x", big, conn->sync_handle);

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->big = big;
	d->sync_handle = conn->sync_handle;
	d->big_sync_term = test_and_clear_bit(HCI_CONN_BIG_SYNC, &conn->flags);

	ret = hci_cmd_sync_queue(hdev, big_terminate_sync, d,
				 terminate_big_destroy);
	if (ret)
		kfree(d);

	return ret;
}

/* Cleanup BIS connection
 *
 * Detects if there any BIS left connected in a BIG
 * broadcaster: Remove advertising instance and terminate BIG.
 * broadcaster receiver: Teminate BIG sync and terminate PA sync.
 */
static void bis_cleanup(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_conn *bis;

	bt_dev_dbg(hdev, "conn %p", conn);

	if (conn->role == HCI_ROLE_MASTER) {
		if (!test_and_clear_bit(HCI_CONN_PER_ADV, &conn->flags))
			return;

		/* Check if ISO connection is a BIS and terminate advertising
		 * set and BIG if there are no other connections using it.
		 */
		bis = hci_conn_hash_lookup_big(hdev, conn->iso_qos.bcast.big);
		if (bis)
			return;

		hci_le_terminate_big(hdev, conn);
	} else {
		bis = hci_conn_hash_lookup_big_any_dst(hdev,
						       conn->iso_qos.bcast.big);

		if (bis)
			return;

		hci_le_big_terminate(hdev, conn->iso_qos.bcast.big,
				     conn);
	}
}

static int remove_cig_sync(struct hci_dev *hdev, void *data)
{
	u8 handle = PTR_ERR(data);

	return hci_le_remove_cig_sync(hdev, handle);
}

static int hci_le_remove_cig(struct hci_dev *hdev, u8 handle)
{
	bt_dev_dbg(hdev, "handle 0x%2.2x", handle);

	return hci_cmd_sync_queue(hdev, remove_cig_sync, ERR_PTR(handle), NULL);
}

static void find_cis(struct hci_conn *conn, void *data)
{
	struct iso_list_data *d = data;

	/* Ignore broadcast or if CIG don't match */
	if (!bacmp(&conn->dst, BDADDR_ANY) || d->cig != conn->iso_qos.ucast.cig)
		return;

	d->count++;
}

/* Cleanup CIS connection:
 *
 * Detects if there any CIS left connected in a CIG and remove it.
 */
static void cis_cleanup(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct iso_list_data d;

	if (conn->iso_qos.ucast.cig == BT_ISO_QOS_CIG_UNSET)
		return;

	memset(&d, 0, sizeof(d));
	d.cig = conn->iso_qos.ucast.cig;

	/* Check if ISO connection is a CIS and remove CIG if there are
	 * no other connections using it.
	 */
	hci_conn_hash_list_state(hdev, find_cis, ISO_LINK, BT_BOUND, &d);
	hci_conn_hash_list_state(hdev, find_cis, ISO_LINK, BT_CONNECT, &d);
	hci_conn_hash_list_state(hdev, find_cis, ISO_LINK, BT_CONNECTED, &d);
	if (d.count)
		return;

	hci_le_remove_cig(hdev, conn->iso_qos.ucast.cig);
}

static u16 hci_conn_hash_alloc_unset(struct hci_dev *hdev)
{
	struct hci_conn_hash *h = &hdev->conn_hash;
	struct hci_conn  *c;
	u16 handle = HCI_CONN_HANDLE_MAX + 1;

	rcu_read_lock();

	list_for_each_entry_rcu(c, &h->list, list) {
		/* Find the first unused handle */
		if (handle == 0xffff || c->handle != handle)
			break;
		handle++;
	}
	rcu_read_unlock();

	return handle;
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
	conn->handle = hci_conn_hash_alloc_unset(hdev);
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
	case ISO_LINK:
		/* conn->src should reflect the local identity address */
		hci_copy_identity_address(hdev, &conn->src, &conn->src_type);

		/* set proper cleanup function */
		if (!bacmp(dst, BDADDR_ANY))
			conn->cleanup = bis_cleanup;
		else if (conn->role == HCI_ROLE_MASTER)
			conn->cleanup = cis_cleanup;

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
	INIT_LIST_HEAD(&conn->link_list);

	INIT_DELAYED_WORK(&conn->disc_work, hci_conn_timeout);
	INIT_DELAYED_WORK(&conn->auto_accept_work, hci_conn_auto_accept);
	INIT_DELAYED_WORK(&conn->idle_work, hci_conn_idle);
	INIT_DELAYED_WORK(&conn->le_conn_timeout, le_conn_timeout);

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

static void hci_conn_unlink(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	bt_dev_dbg(hdev, "hcon %p", conn);

	if (!conn->parent) {
		struct hci_link *link, *t;

		list_for_each_entry_safe(link, t, &conn->link_list, list) {
			struct hci_conn *child = link->conn;

			hci_conn_unlink(child);

			/* If hdev is down it means
			 * hci_dev_close_sync/hci_conn_hash_flush is in progress
			 * and links don't need to be cleanup as all connections
			 * would be cleanup.
			 */
			if (!test_bit(HCI_UP, &hdev->flags))
				continue;

			/* Due to race, SCO connection might be not established
			 * yet at this point. Delete it now, otherwise it is
			 * possible for it to be stuck and can't be deleted.
			 */
			if ((child->type == SCO_LINK ||
			     child->type == ESCO_LINK) &&
			    HCI_CONN_HANDLE_UNSET(child->handle))
				hci_conn_del(child);
		}

		return;
	}

	if (!conn->link)
		return;

	list_del_rcu(&conn->link->list);
	synchronize_rcu();

	hci_conn_drop(conn->parent);
	hci_conn_put(conn->parent);
	conn->parent = NULL;

	kfree(conn->link);
	conn->link = NULL;
}

void hci_conn_del(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;

	BT_DBG("%s hcon %p handle %d", hdev->name, conn, conn->handle);

	hci_conn_unlink(conn);

	cancel_delayed_work_sync(&conn->disc_work);
	cancel_delayed_work_sync(&conn->auto_accept_work);
	cancel_delayed_work_sync(&conn->idle_work);

	if (conn->type == ACL_LINK) {
		/* Unacked frames */
		hdev->acl_cnt += conn->sent;
	} else if (conn->type == LE_LINK) {
		cancel_delayed_work(&conn->le_conn_timeout);

		if (hdev->le_pkts)
			hdev->le_cnt += conn->sent;
		else
			hdev->acl_cnt += conn->sent;
	} else {
		/* Unacked ISO frames */
		if (conn->type == ISO_LINK) {
			if (hdev->iso_pkts)
				hdev->iso_cnt += conn->sent;
			else if (hdev->le_pkts)
				hdev->le_cnt += conn->sent;
			else
				hdev->acl_cnt += conn->sent;
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
static void hci_le_conn_failed(struct hci_conn *conn, u8 status)
{
	struct hci_dev *hdev = conn->hdev;

	hci_connect_le_scan_cleanup(conn, status);

	/* Enable advertising in case this was a failed connection
	 * attempt as a peripheral.
	 */
	hci_enable_advertising(hdev);
}

/* This function requires the caller holds hdev->lock */
void hci_conn_failed(struct hci_conn *conn, u8 status)
{
	struct hci_dev *hdev = conn->hdev;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	switch (conn->type) {
	case LE_LINK:
		hci_le_conn_failed(conn, status);
		break;
	case ACL_LINK:
		mgmt_connect_failed(hdev, &conn->dst, conn->type,
				    conn->dst_type, status);
		break;
	}

	conn->state = BT_CLOSED;
	hci_connect_cfm(conn, status);
	hci_conn_del(conn);
}

/* This function requires the caller holds hdev->lock */
u8 hci_conn_set_handle(struct hci_conn *conn, u16 handle)
{
	struct hci_dev *hdev = conn->hdev;

	bt_dev_dbg(hdev, "hcon %p handle 0x%4.4x", conn, handle);

	if (conn->handle == handle)
		return 0;

	if (handle > HCI_CONN_HANDLE_MAX) {
		bt_dev_err(hdev, "Invalid handle: 0x%4.4x > 0x%4.4x",
			   handle, HCI_CONN_HANDLE_MAX);
		return HCI_ERROR_INVALID_PARAMETERS;
	}

	/* If abort_reason has been sent it means the connection is being
	 * aborted and the handle shall not be changed.
	 */
	if (conn->abort_reason)
		return conn->abort_reason;

	conn->handle = handle;

	return 0;
}

static void create_le_conn_complete(struct hci_dev *hdev, void *data, int err)
{
	struct hci_conn *conn;
	u16 handle = PTR_ERR(data);

	conn = hci_conn_hash_lookup_handle(hdev, handle);
	if (!conn)
		return;

	bt_dev_dbg(hdev, "err %d", err);

	hci_dev_lock(hdev);

	if (!err) {
		hci_connect_le_scan_cleanup(conn, 0x00);
		goto done;
	}

	/* Check if connection is still pending */
	if (conn != hci_lookup_le_connect(hdev))
		goto done;

	/* Flush to make sure we send create conn cancel command if needed */
	flush_delayed_work(&conn->le_conn_timeout);
	hci_conn_failed(conn, bt_status(err));

done:
	hci_dev_unlock(hdev);
}

static int hci_connect_le_sync(struct hci_dev *hdev, void *data)
{
	struct hci_conn *conn;
	u16 handle = PTR_ERR(data);

	conn = hci_conn_hash_lookup_handle(hdev, handle);
	if (!conn)
		return 0;

	bt_dev_dbg(hdev, "conn %p", conn);

	conn->state = BT_CONNECT;

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

	clear_bit(HCI_CONN_SCANNING, &conn->flags);

	err = hci_cmd_sync_queue(hdev, hci_connect_le_sync,
				 ERR_PTR(conn->handle),
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
		hci_pend_le_list_del_init(params);
		hci_pend_le_list_add(params, &hdev->pend_le_conns);
	}

	params->explicit_connect = true;

	BT_DBG("addr %pMR (type %u) auto_connect %u", addr, addr_type,
	       params->auto_connect);

	return 0;
}

static int qos_set_big(struct hci_dev *hdev, struct bt_iso_qos *qos)
{
	struct hci_conn *conn;
	u8  big;

	/* Allocate a BIG if not set */
	if (qos->bcast.big == BT_ISO_QOS_BIG_UNSET) {
		for (big = 0x00; big < 0xef; big++) {

			conn = hci_conn_hash_lookup_big(hdev, big);
			if (!conn)
				break;
		}

		if (big == 0xef)
			return -EADDRNOTAVAIL;

		/* Update BIG */
		qos->bcast.big = big;
	}

	return 0;
}

static int qos_set_bis(struct hci_dev *hdev, struct bt_iso_qos *qos)
{
	struct hci_conn *conn;
	u8  bis;

	/* Allocate BIS if not set */
	if (qos->bcast.bis == BT_ISO_QOS_BIS_UNSET) {
		/* Find an unused adv set to advertise BIS, skip instance 0x00
		 * since it is reserved as general purpose set.
		 */
		for (bis = 0x01; bis < hdev->le_num_of_adv_sets;
		     bis++) {

			conn = hci_conn_hash_lookup_bis(hdev, BDADDR_ANY, bis);
			if (!conn)
				break;
		}

		if (bis == hdev->le_num_of_adv_sets)
			return -EADDRNOTAVAIL;

		/* Update BIS */
		qos->bcast.bis = bis;
	}

	return 0;
}

/* This function requires the caller holds hdev->lock */
static struct hci_conn *hci_add_bis(struct hci_dev *hdev, bdaddr_t *dst,
				    struct bt_iso_qos *qos, __u8 base_len,
				    __u8 *base)
{
	struct hci_conn *conn;
	int err;

	/* Let's make sure that le is enabled.*/
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED)) {
		if (lmp_le_capable(hdev))
			return ERR_PTR(-ECONNREFUSED);
		return ERR_PTR(-EOPNOTSUPP);
	}

	err = qos_set_big(hdev, qos);
	if (err)
		return ERR_PTR(err);

	err = qos_set_bis(hdev, qos);
	if (err)
		return ERR_PTR(err);

	/* Check if the LE Create BIG command has already been sent */
	conn = hci_conn_hash_lookup_per_adv_bis(hdev, dst, qos->bcast.big,
						qos->bcast.big);
	if (conn)
		return ERR_PTR(-EADDRINUSE);

	/* Check BIS settings against other bound BISes, since all
	 * BISes in a BIG must have the same value for all parameters
	 */
	conn = hci_conn_hash_lookup_big(hdev, qos->bcast.big);

	if (conn && (memcmp(qos, &conn->iso_qos, sizeof(*qos)) ||
		     base_len != conn->le_per_adv_data_len ||
		     memcmp(conn->le_per_adv_data, base, base_len)))
		return ERR_PTR(-EADDRINUSE);

	conn = hci_conn_add(hdev, ISO_LINK, dst, HCI_ROLE_MASTER);
	if (!conn)
		return ERR_PTR(-ENOMEM);

	conn->state = BT_CONNECT;

	hci_conn_hold(conn);
	return conn;
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

static struct hci_link *hci_conn_link(struct hci_conn *parent,
				      struct hci_conn *conn)
{
	struct hci_dev *hdev = parent->hdev;
	struct hci_link *link;

	bt_dev_dbg(hdev, "parent %p hcon %p", parent, conn);

	if (conn->link)
		return conn->link;

	if (conn->parent)
		return NULL;

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return NULL;

	link->conn = hci_conn_hold(conn);
	conn->link = link;
	conn->parent = hci_conn_get(parent);

	/* Use list_add_tail_rcu append to the list */
	list_add_tail_rcu(&link->list, &parent->link_list);

	return link;
}

struct hci_conn *hci_connect_sco(struct hci_dev *hdev, int type, bdaddr_t *dst,
				 __u16 setting, struct bt_codec *codec)
{
	struct hci_conn *acl;
	struct hci_conn *sco;
	struct hci_link *link;

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

	link = hci_conn_link(acl, sco);
	if (!link) {
		hci_conn_drop(acl);
		hci_conn_drop(sco);
		return ERR_PTR(-ENOLINK);
	}

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

static int hci_le_create_big(struct hci_conn *conn, struct bt_iso_qos *qos)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_le_create_big cp;
	struct iso_list_data data;

	memset(&cp, 0, sizeof(cp));

	data.big = qos->bcast.big;
	data.bis = qos->bcast.bis;
	data.count = 0;

	/* Create a BIS for each bound connection */
	hci_conn_hash_list_state(hdev, bis_list, ISO_LINK,
				 BT_BOUND, &data);

	cp.handle = qos->bcast.big;
	cp.adv_handle = qos->bcast.bis;
	cp.num_bis  = data.count;
	hci_cpu_to_le24(qos->bcast.out.interval, cp.bis.sdu_interval);
	cp.bis.sdu = cpu_to_le16(qos->bcast.out.sdu);
	cp.bis.latency =  cpu_to_le16(qos->bcast.out.latency);
	cp.bis.rtn  = qos->bcast.out.rtn;
	cp.bis.phy  = qos->bcast.out.phy;
	cp.bis.packing = qos->bcast.packing;
	cp.bis.framing = qos->bcast.framing;
	cp.bis.encryption = qos->bcast.encryption;
	memcpy(cp.bis.bcode, qos->bcast.bcode, sizeof(cp.bis.bcode));

	return hci_send_cmd(hdev, HCI_OP_LE_CREATE_BIG, sizeof(cp), &cp);
}

static int set_cig_params_sync(struct hci_dev *hdev, void *data)
{
	u8 cig_id = PTR_ERR(data);
	struct hci_conn *conn;
	struct bt_iso_qos *qos;
	struct iso_cig_params pdu;
	u8 cis_id;

	conn = hci_conn_hash_lookup_cig(hdev, cig_id);
	if (!conn)
		return 0;

	memset(&pdu, 0, sizeof(pdu));

	qos = &conn->iso_qos;
	pdu.cp.cig_id = cig_id;
	hci_cpu_to_le24(qos->ucast.out.interval, pdu.cp.c_interval);
	hci_cpu_to_le24(qos->ucast.in.interval, pdu.cp.p_interval);
	pdu.cp.sca = qos->ucast.sca;
	pdu.cp.packing = qos->ucast.packing;
	pdu.cp.framing = qos->ucast.framing;
	pdu.cp.c_latency = cpu_to_le16(qos->ucast.out.latency);
	pdu.cp.p_latency = cpu_to_le16(qos->ucast.in.latency);

	/* Reprogram all CIS(s) with the same CIG, valid range are:
	 * num_cis: 0x00 to 0x1F
	 * cis_id: 0x00 to 0xEF
	 */
	for (cis_id = 0x00; cis_id < 0xf0 &&
	     pdu.cp.num_cis < ARRAY_SIZE(pdu.cis); cis_id++) {
		struct hci_cis_params *cis;

		conn = hci_conn_hash_lookup_cis(hdev, NULL, 0, cig_id, cis_id);
		if (!conn)
			continue;

		qos = &conn->iso_qos;

		cis = &pdu.cis[pdu.cp.num_cis++];
		cis->cis_id = cis_id;
		cis->c_sdu  = cpu_to_le16(conn->iso_qos.ucast.out.sdu);
		cis->p_sdu  = cpu_to_le16(conn->iso_qos.ucast.in.sdu);
		cis->c_phy  = qos->ucast.out.phy ? qos->ucast.out.phy :
			      qos->ucast.in.phy;
		cis->p_phy  = qos->ucast.in.phy ? qos->ucast.in.phy :
			      qos->ucast.out.phy;
		cis->c_rtn  = qos->ucast.out.rtn;
		cis->p_rtn  = qos->ucast.in.rtn;
	}

	if (!pdu.cp.num_cis)
		return 0;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_CIG_PARAMS,
				     sizeof(pdu.cp) +
				     pdu.cp.num_cis * sizeof(pdu.cis[0]), &pdu,
				     HCI_CMD_TIMEOUT);
}

static bool hci_le_set_cig_params(struct hci_conn *conn, struct bt_iso_qos *qos)
{
	struct hci_dev *hdev = conn->hdev;
	struct iso_list_data data;

	memset(&data, 0, sizeof(data));

	/* Allocate first still reconfigurable CIG if not set */
	if (qos->ucast.cig == BT_ISO_QOS_CIG_UNSET) {
		for (data.cig = 0x00; data.cig < 0xf0; data.cig++) {
			data.count = 0;

			hci_conn_hash_list_state(hdev, find_cis, ISO_LINK,
						 BT_CONNECT, &data);
			if (data.count)
				continue;

			hci_conn_hash_list_state(hdev, find_cis, ISO_LINK,
						 BT_CONNECTED, &data);
			if (!data.count)
				break;
		}

		if (data.cig == 0xf0)
			return false;

		/* Update CIG */
		qos->ucast.cig = data.cig;
	}

	if (qos->ucast.cis != BT_ISO_QOS_CIS_UNSET) {
		if (hci_conn_hash_lookup_cis(hdev, NULL, 0, qos->ucast.cig,
					     qos->ucast.cis))
			return false;
		goto done;
	}

	/* Allocate first available CIS if not set */
	for (data.cig = qos->ucast.cig, data.cis = 0x00; data.cis < 0xf0;
	     data.cis++) {
		if (!hci_conn_hash_lookup_cis(hdev, NULL, 0, data.cig,
					      data.cis)) {
			/* Update CIS */
			qos->ucast.cis = data.cis;
			break;
		}
	}

	if (qos->ucast.cis == BT_ISO_QOS_CIS_UNSET)
		return false;

done:
	if (hci_cmd_sync_queue(hdev, set_cig_params_sync,
			       ERR_PTR(qos->ucast.cig), NULL) < 0)
		return false;

	return true;
}

struct hci_conn *hci_bind_cis(struct hci_dev *hdev, bdaddr_t *dst,
			      __u8 dst_type, struct bt_iso_qos *qos)
{
	struct hci_conn *cis;

	cis = hci_conn_hash_lookup_cis(hdev, dst, dst_type, qos->ucast.cig,
				       qos->ucast.cis);
	if (!cis) {
		cis = hci_conn_add(hdev, ISO_LINK, dst, HCI_ROLE_MASTER);
		if (!cis)
			return ERR_PTR(-ENOMEM);
		cis->cleanup = cis_cleanup;
		cis->dst_type = dst_type;
	}

	if (cis->state == BT_CONNECTED)
		return cis;

	/* Check if CIS has been set and the settings matches */
	if (cis->state == BT_BOUND &&
	    !memcmp(&cis->iso_qos, qos, sizeof(*qos)))
		return cis;

	/* Update LINK PHYs according to QoS preference */
	cis->le_tx_phy = qos->ucast.out.phy;
	cis->le_rx_phy = qos->ucast.in.phy;

	/* If output interval is not set use the input interval as it cannot be
	 * 0x000000.
	 */
	if (!qos->ucast.out.interval)
		qos->ucast.out.interval = qos->ucast.in.interval;

	/* If input interval is not set use the output interval as it cannot be
	 * 0x000000.
	 */
	if (!qos->ucast.in.interval)
		qos->ucast.in.interval = qos->ucast.out.interval;

	/* If output latency is not set use the input latency as it cannot be
	 * 0x0000.
	 */
	if (!qos->ucast.out.latency)
		qos->ucast.out.latency = qos->ucast.in.latency;

	/* If input latency is not set use the output latency as it cannot be
	 * 0x0000.
	 */
	if (!qos->ucast.in.latency)
		qos->ucast.in.latency = qos->ucast.out.latency;

	if (!hci_le_set_cig_params(cis, qos)) {
		hci_conn_drop(cis);
		return ERR_PTR(-EINVAL);
	}

	hci_conn_hold(cis);

	cis->iso_qos = *qos;
	cis->state = BT_BOUND;

	return cis;
}

bool hci_iso_setup_path(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct hci_cp_le_setup_iso_path cmd;

	memset(&cmd, 0, sizeof(cmd));

	if (conn->iso_qos.ucast.out.sdu) {
		cmd.handle = cpu_to_le16(conn->handle);
		cmd.direction = 0x00; /* Input (Host to Controller) */
		cmd.path = 0x00; /* HCI path if enabled */
		cmd.codec = 0x03; /* Transparent Data */

		if (hci_send_cmd(hdev, HCI_OP_LE_SETUP_ISO_PATH, sizeof(cmd),
				 &cmd) < 0)
			return false;
	}

	if (conn->iso_qos.ucast.in.sdu) {
		cmd.handle = cpu_to_le16(conn->handle);
		cmd.direction = 0x01; /* Output (Controller to Host) */
		cmd.path = 0x00; /* HCI path if enabled */
		cmd.codec = 0x03; /* Transparent Data */

		if (hci_send_cmd(hdev, HCI_OP_LE_SETUP_ISO_PATH, sizeof(cmd),
				 &cmd) < 0)
			return false;
	}

	return true;
}

int hci_conn_check_create_cis(struct hci_conn *conn)
{
	if (conn->type != ISO_LINK || !bacmp(&conn->dst, BDADDR_ANY))
		return -EINVAL;

	if (!conn->parent || conn->parent->state != BT_CONNECTED ||
	    conn->state != BT_CONNECT || HCI_CONN_HANDLE_UNSET(conn->handle))
		return 1;

	return 0;
}

static int hci_create_cis_sync(struct hci_dev *hdev, void *data)
{
	return hci_le_create_cis_sync(hdev);
}

int hci_le_create_cis_pending(struct hci_dev *hdev)
{
	struct hci_conn *conn;
	bool pending = false;

	rcu_read_lock();

	list_for_each_entry_rcu(conn, &hdev->conn_hash.list, list) {
		if (test_bit(HCI_CONN_CREATE_CIS, &conn->flags)) {
			rcu_read_unlock();
			return -EBUSY;
		}

		if (!hci_conn_check_create_cis(conn))
			pending = true;
	}

	rcu_read_unlock();

	if (!pending)
		return 0;

	/* Queue Create CIS */
	return hci_cmd_sync_queue(hdev, hci_create_cis_sync, NULL, NULL);
}

static void hci_iso_qos_setup(struct hci_dev *hdev, struct hci_conn *conn,
			      struct bt_iso_io_qos *qos, __u8 phy)
{
	/* Only set MTU if PHY is enabled */
	if (!qos->sdu && qos->phy) {
		if (hdev->iso_mtu > 0)
			qos->sdu = hdev->iso_mtu;
		else if (hdev->le_mtu > 0)
			qos->sdu = hdev->le_mtu;
		else
			qos->sdu = hdev->acl_mtu;
	}

	/* Use the same PHY as ACL if set to any */
	if (qos->phy == BT_ISO_PHY_ANY)
		qos->phy = phy;

	/* Use LE ACL connection interval if not set */
	if (!qos->interval)
		/* ACL interval unit in 1.25 ms to us */
		qos->interval = conn->le_conn_interval * 1250;

	/* Use LE ACL connection latency if not set */
	if (!qos->latency)
		qos->latency = conn->le_conn_latency;
}

static int create_big_sync(struct hci_dev *hdev, void *data)
{
	struct hci_conn *conn = data;
	struct bt_iso_qos *qos = &conn->iso_qos;
	u16 interval, sync_interval = 0;
	u32 flags = 0;
	int err;

	if (qos->bcast.out.phy == 0x02)
		flags |= MGMT_ADV_FLAG_SEC_2M;

	/* Align intervals */
	interval = (qos->bcast.out.interval / 1250) * qos->bcast.sync_factor;

	if (qos->bcast.bis)
		sync_interval = interval * 4;

	err = hci_start_per_adv_sync(hdev, qos->bcast.bis, conn->le_per_adv_data_len,
				     conn->le_per_adv_data, flags, interval,
				     interval, sync_interval);
	if (err)
		return err;

	return hci_le_create_big(conn, &conn->iso_qos);
}

static void create_pa_complete(struct hci_dev *hdev, void *data, int err)
{
	struct hci_cp_le_pa_create_sync *cp = data;

	bt_dev_dbg(hdev, "");

	if (err)
		bt_dev_err(hdev, "Unable to create PA: %d", err);

	kfree(cp);
}

static int create_pa_sync(struct hci_dev *hdev, void *data)
{
	struct hci_cp_le_pa_create_sync *cp = data;
	int err;

	err = __hci_cmd_sync_status(hdev, HCI_OP_LE_PA_CREATE_SYNC,
				    sizeof(*cp), cp, HCI_CMD_TIMEOUT);
	if (err) {
		hci_dev_clear_flag(hdev, HCI_PA_SYNC);
		return err;
	}

	return hci_update_passive_scan_sync(hdev);
}

int hci_pa_create_sync(struct hci_dev *hdev, bdaddr_t *dst, __u8 dst_type,
		       __u8 sid, struct bt_iso_qos *qos)
{
	struct hci_cp_le_pa_create_sync *cp;

	if (hci_dev_test_and_set_flag(hdev, HCI_PA_SYNC))
		return -EBUSY;

	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp) {
		hci_dev_clear_flag(hdev, HCI_PA_SYNC);
		return -ENOMEM;
	}

	cp->options = qos->bcast.options;
	cp->sid = sid;
	cp->addr_type = dst_type;
	bacpy(&cp->addr, dst);
	cp->skip = cpu_to_le16(qos->bcast.skip);
	cp->sync_timeout = cpu_to_le16(qos->bcast.sync_timeout);
	cp->sync_cte_type = qos->bcast.sync_cte_type;

	/* Queue start pa_create_sync and scan */
	return hci_cmd_sync_queue(hdev, create_pa_sync, cp, create_pa_complete);
}

int hci_le_big_create_sync(struct hci_dev *hdev, struct bt_iso_qos *qos,
			   __u16 sync_handle, __u8 num_bis, __u8 bis[])
{
	struct _packed {
		struct hci_cp_le_big_create_sync cp;
		__u8  bis[0x11];
	} pdu;
	int err;

	if (num_bis > sizeof(pdu.bis))
		return -EINVAL;

	err = qos_set_big(hdev, qos);
	if (err)
		return err;

	memset(&pdu, 0, sizeof(pdu));
	pdu.cp.handle = qos->bcast.big;
	pdu.cp.sync_handle = cpu_to_le16(sync_handle);
	pdu.cp.encryption = qos->bcast.encryption;
	memcpy(pdu.cp.bcode, qos->bcast.bcode, sizeof(pdu.cp.bcode));
	pdu.cp.mse = qos->bcast.mse;
	pdu.cp.timeout = cpu_to_le16(qos->bcast.timeout);
	pdu.cp.num_bis = num_bis;
	memcpy(pdu.bis, bis, num_bis);

	return hci_send_cmd(hdev, HCI_OP_LE_BIG_CREATE_SYNC,
			    sizeof(pdu.cp) + num_bis, &pdu);
}

static void create_big_complete(struct hci_dev *hdev, void *data, int err)
{
	struct hci_conn *conn = data;

	bt_dev_dbg(hdev, "conn %p", conn);

	if (err) {
		bt_dev_err(hdev, "Unable to create BIG: %d", err);
		hci_connect_cfm(conn, err);
		hci_conn_del(conn);
	}
}

struct hci_conn *hci_bind_bis(struct hci_dev *hdev, bdaddr_t *dst,
			      struct bt_iso_qos *qos,
			      __u8 base_len, __u8 *base)
{
	struct hci_conn *conn;
	__u8 eir[HCI_MAX_PER_AD_LENGTH];

	if (base_len && base)
		base_len = eir_append_service_data(eir, 0,  0x1851,
						   base, base_len);

	/* We need hci_conn object using the BDADDR_ANY as dst */
	conn = hci_add_bis(hdev, dst, qos, base_len, eir);
	if (IS_ERR(conn))
		return conn;

	/* Update LINK PHYs according to QoS preference */
	conn->le_tx_phy = qos->bcast.out.phy;
	conn->le_tx_phy = qos->bcast.out.phy;

	/* Add Basic Announcement into Peridic Adv Data if BASE is set */
	if (base_len && base) {
		memcpy(conn->le_per_adv_data,  eir, sizeof(eir));
		conn->le_per_adv_data_len = base_len;
	}

	hci_iso_qos_setup(hdev, conn, &qos->bcast.out,
			  conn->le_tx_phy ? conn->le_tx_phy :
			  hdev->le_tx_def_phys);

	conn->iso_qos = *qos;
	conn->state = BT_BOUND;

	return conn;
}

static void bis_mark_per_adv(struct hci_conn *conn, void *data)
{
	struct iso_list_data *d = data;

	/* Skip if not broadcast/ANY address */
	if (bacmp(&conn->dst, BDADDR_ANY))
		return;

	if (d->big != conn->iso_qos.bcast.big ||
	    d->bis == BT_ISO_QOS_BIS_UNSET ||
	    d->bis != conn->iso_qos.bcast.bis)
		return;

	set_bit(HCI_CONN_PER_ADV, &conn->flags);
}

struct hci_conn *hci_connect_bis(struct hci_dev *hdev, bdaddr_t *dst,
				 __u8 dst_type, struct bt_iso_qos *qos,
				 __u8 base_len, __u8 *base)
{
	struct hci_conn *conn;
	int err;
	struct iso_list_data data;

	conn = hci_bind_bis(hdev, dst, qos, base_len, base);
	if (IS_ERR(conn))
		return conn;

	data.big = qos->bcast.big;
	data.bis = qos->bcast.bis;

	/* Set HCI_CONN_PER_ADV for all bound connections, to mark that
	 * the start periodic advertising and create BIG commands have
	 * been queued
	 */
	hci_conn_hash_list_state(hdev, bis_mark_per_adv, ISO_LINK,
				 BT_BOUND, &data);

	/* Queue start periodic advertising and create BIG */
	err = hci_cmd_sync_queue(hdev, create_big_sync, conn,
				 create_big_complete);
	if (err < 0) {
		hci_conn_drop(conn);
		return ERR_PTR(err);
	}

	return conn;
}

struct hci_conn *hci_connect_cis(struct hci_dev *hdev, bdaddr_t *dst,
				 __u8 dst_type, struct bt_iso_qos *qos)
{
	struct hci_conn *le;
	struct hci_conn *cis;
	struct hci_link *link;

	if (hci_dev_test_flag(hdev, HCI_ADVERTISING))
		le = hci_connect_le(hdev, dst, dst_type, false,
				    BT_SECURITY_LOW,
				    HCI_LE_CONN_TIMEOUT,
				    HCI_ROLE_SLAVE);
	else
		le = hci_connect_le_scan(hdev, dst, dst_type,
					 BT_SECURITY_LOW,
					 HCI_LE_CONN_TIMEOUT,
					 CONN_REASON_ISO_CONNECT);
	if (IS_ERR(le))
		return le;

	hci_iso_qos_setup(hdev, le, &qos->ucast.out,
			  le->le_tx_phy ? le->le_tx_phy : hdev->le_tx_def_phys);
	hci_iso_qos_setup(hdev, le, &qos->ucast.in,
			  le->le_rx_phy ? le->le_rx_phy : hdev->le_rx_def_phys);

	cis = hci_bind_cis(hdev, dst, dst_type, qos);
	if (IS_ERR(cis)) {
		hci_conn_drop(le);
		return cis;
	}

	link = hci_conn_link(le, cis);
	if (!link) {
		hci_conn_drop(le);
		hci_conn_drop(cis);
		return ERR_PTR(-ENOLINK);
	}

	/* Link takes the refcount */
	hci_conn_drop(cis);

	cis->state = BT_CONNECT;

	hci_le_create_cis_pending(hdev);

	return cis;
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
	struct list_head *head = &hdev->conn_hash.list;
	struct hci_conn *conn;

	BT_DBG("hdev %s", hdev->name);

	/* We should not traverse the list here, because hci_conn_del
	 * can remove extra links, which may cause the list traversal
	 * to hit items that have already been released.
	 */
	while ((conn = list_first_entry_or_null(head,
						struct hci_conn,
						list)) != NULL) {
		conn->state = BT_CLOSED;
		hci_disconn_cfm(conn, HCI_ERROR_LOCAL_HOST_TERM);
		hci_conn_del(conn);
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

static int abort_conn_sync(struct hci_dev *hdev, void *data)
{
	struct hci_conn *conn;
	u16 handle = PTR_ERR(data);

	conn = hci_conn_hash_lookup_handle(hdev, handle);
	if (!conn)
		return 0;

	return hci_abort_conn_sync(hdev, conn, conn->abort_reason);
}

int hci_abort_conn(struct hci_conn *conn, u8 reason)
{
	struct hci_dev *hdev = conn->hdev;

	/* If abort_reason has already been set it means the connection is
	 * already being aborted so don't attempt to overwrite it.
	 */
	if (conn->abort_reason)
		return 0;

	bt_dev_dbg(hdev, "handle 0x%2.2x reason 0x%2.2x", conn->handle, reason);

	conn->abort_reason = reason;

	/* If the connection is pending check the command opcode since that
	 * might be blocking on hci_cmd_sync_work while waiting its respective
	 * event so we need to hci_cmd_sync_cancel to cancel it.
	 *
	 * hci_connect_le serializes the connection attempts so only one
	 * connection can be in BT_CONNECT at time.
	 */
	if (conn->state == BT_CONNECT && hdev->req_status == HCI_REQ_PEND) {
		switch (hci_skb_event(hdev->sent_cmd)) {
		case HCI_EV_LE_CONN_COMPLETE:
		case HCI_EV_LE_ENHANCED_CONN_COMPLETE:
		case HCI_EVT_LE_CIS_ESTABLISHED:
			hci_cmd_sync_cancel(hdev, -ECANCELED);
			break;
		}
	}

	return hci_cmd_sync_queue(hdev, abort_conn_sync, ERR_PTR(conn->handle),
				  NULL);
}
