/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (c) 2000-2001, 2010, Code Aurora Forum. All rights reserved.
   Copyright 2023-2024 NXP

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

/* Bluetooth HCI event handling. */

#include <linux/unaligned.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "hci_debugfs.h"
#include "hci_codec.h"
#include "smp.h"
#include "msft.h"
#include "eir.h"

#define ZERO_KEY "\x00\x00\x00\x00\x00\x00\x00\x00" \
		 "\x00\x00\x00\x00\x00\x00\x00\x00"

/* Handle HCI Event packets */

static void *hci_ev_skb_pull(struct hci_dev *hdev, struct sk_buff *skb,
			     u8 ev, size_t len)
{
	void *data;

	data = skb_pull_data(skb, len);
	if (!data)
		bt_dev_err(hdev, "Malformed Event: 0x%2.2x", ev);

	return data;
}

static void *hci_cc_skb_pull(struct hci_dev *hdev, struct sk_buff *skb,
			     u16 op, size_t len)
{
	void *data;

	data = skb_pull_data(skb, len);
	if (!data)
		bt_dev_err(hdev, "Malformed Command Complete: 0x%4.4x", op);

	return data;
}

static void *hci_le_ev_skb_pull(struct hci_dev *hdev, struct sk_buff *skb,
				u8 ev, size_t len)
{
	void *data;

	data = skb_pull_data(skb, len);
	if (!data)
		bt_dev_err(hdev, "Malformed LE Event: 0x%2.2x", ev);

	return data;
}

static u8 hci_cc_inquiry_cancel(struct hci_dev *hdev, void *data,
				struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	/* It is possible that we receive Inquiry Complete event right
	 * before we receive Inquiry Cancel Command Complete event, in
	 * which case the latter event should have status of Command
	 * Disallowed. This should not be treated as error, since
	 * we actually achieve what Inquiry Cancel wants to achieve,
	 * which is to end the last Inquiry session.
	 */
	if (rp->status == HCI_ERROR_COMMAND_DISALLOWED && !test_bit(HCI_INQUIRY, &hdev->flags)) {
		bt_dev_warn(hdev, "Ignoring error of Inquiry Cancel command");
		rp->status = 0x00;
	}

	if (rp->status)
		return rp->status;

	clear_bit(HCI_INQUIRY, &hdev->flags);
	smp_mb__after_atomic(); /* wake_up_bit advises about this barrier */
	wake_up_bit(&hdev->flags, HCI_INQUIRY);

	hci_dev_lock(hdev);
	/* Set discovery state to stopped if we're not doing LE active
	 * scanning.
	 */
	if (!hci_dev_test_flag(hdev, HCI_LE_SCAN) ||
	    hdev->le_scan_type != LE_SCAN_ACTIVE)
		hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_periodic_inq(struct hci_dev *hdev, void *data,
			      struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_set_flag(hdev, HCI_PERIODIC_INQ);

	return rp->status;
}

static u8 hci_cc_exit_periodic_inq(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_clear_flag(hdev, HCI_PERIODIC_INQ);

	return rp->status;
}

static u8 hci_cc_remote_name_req_cancel(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_remote_name_req_cancel *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	return rp->status;
}

static u8 hci_cc_role_discovery(struct hci_dev *hdev, void *data,
				struct sk_buff *skb)
{
	struct hci_rp_role_discovery *rp = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->role = rp->role;

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_link_policy(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_rp_read_link_policy *rp = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->link_policy = __le16_to_cpu(rp->policy);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_write_link_policy(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_rp_write_link_policy *rp = data;
	struct hci_conn *conn;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_LINK_POLICY);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->link_policy = get_unaligned_le16(sent + 2);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_def_link_policy(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_rp_read_def_link_policy *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->link_policy = __le16_to_cpu(rp->policy);

	return rp->status;
}

static u8 hci_cc_write_def_link_policy(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_DEF_LINK_POLICY);
	if (!sent)
		return rp->status;

	hdev->link_policy = get_unaligned_le16(sent);

	return rp->status;
}

static u8 hci_cc_reset(struct hci_dev *hdev, void *data, struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	clear_bit(HCI_RESET, &hdev->flags);

	if (rp->status)
		return rp->status;

	/* Reset all non-persistent flags */
	hci_dev_clear_volatile_flags(hdev);

	hci_discovery_set_state(hdev, DISCOVERY_STOPPED);

	hdev->inq_tx_power = HCI_TX_POWER_INVALID;
	hdev->adv_tx_power = HCI_TX_POWER_INVALID;

	memset(hdev->adv_data, 0, sizeof(hdev->adv_data));
	hdev->adv_data_len = 0;

	memset(hdev->scan_rsp_data, 0, sizeof(hdev->scan_rsp_data));
	hdev->scan_rsp_data_len = 0;

	hdev->le_scan_type = LE_SCAN_PASSIVE;

	hdev->ssp_debug_mode = 0;

	hci_bdaddr_list_clear(&hdev->le_accept_list);
	hci_bdaddr_list_clear(&hdev->le_resolv_list);

	return rp->status;
}

static u8 hci_cc_read_stored_link_key(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_rp_read_stored_link_key *rp = data;
	struct hci_cp_read_stored_link_key *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_READ_STORED_LINK_KEY);
	if (!sent)
		return rp->status;

	if (!rp->status && sent->read_all == 0x01) {
		hdev->stored_max_keys = le16_to_cpu(rp->max_keys);
		hdev->stored_num_keys = le16_to_cpu(rp->num_keys);
	}

	return rp->status;
}

static u8 hci_cc_delete_stored_link_key(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_delete_stored_link_key *rp = data;
	u16 num_keys;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	num_keys = le16_to_cpu(rp->num_keys);

	if (num_keys <= hdev->stored_num_keys)
		hdev->stored_num_keys -= num_keys;
	else
		hdev->stored_num_keys = 0;

	return rp->status;
}

static u8 hci_cc_write_local_name(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_LOCAL_NAME);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_set_local_name_complete(hdev, sent, rp->status);
	else if (!rp->status)
		memcpy(hdev->dev_name, sent, HCI_MAX_NAME_LENGTH);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_local_name(struct hci_dev *hdev, void *data,
				 struct sk_buff *skb)
{
	struct hci_rp_read_local_name *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG))
		memcpy(hdev->dev_name, rp->name, HCI_MAX_NAME_LENGTH);

	return rp->status;
}

static u8 hci_cc_write_auth_enable(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_AUTH_ENABLE);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	if (!rp->status) {
		__u8 param = *((__u8 *) sent);

		if (param == AUTH_ENABLED)
			set_bit(HCI_AUTH, &hdev->flags);
		else
			clear_bit(HCI_AUTH, &hdev->flags);
	}

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_auth_enable_complete(hdev, rp->status);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_write_encrypt_mode(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	__u8 param;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_ENCRYPT_MODE);
	if (!sent)
		return rp->status;

	param = *((__u8 *) sent);

	if (param)
		set_bit(HCI_ENCRYPT, &hdev->flags);
	else
		clear_bit(HCI_ENCRYPT, &hdev->flags);

	return rp->status;
}

static u8 hci_cc_write_scan_enable(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	__u8 param;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SCAN_ENABLE);
	if (!sent)
		return rp->status;

	param = *((__u8 *) sent);

	hci_dev_lock(hdev);

	if (rp->status) {
		hdev->discov_timeout = 0;
		goto done;
	}

	if (param & SCAN_INQUIRY)
		set_bit(HCI_ISCAN, &hdev->flags);
	else
		clear_bit(HCI_ISCAN, &hdev->flags);

	if (param & SCAN_PAGE)
		set_bit(HCI_PSCAN, &hdev->flags);
	else
		clear_bit(HCI_PSCAN, &hdev->flags);

done:
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_set_event_filter(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_set_event_filter *cp;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_SET_EVENT_FLT);
	if (!sent)
		return rp->status;

	cp = (struct hci_cp_set_event_filter *)sent;

	if (cp->flt_type == HCI_FLT_CLEAR_ALL)
		hci_dev_clear_flag(hdev, HCI_EVENT_FILTER_CONFIGURED);
	else
		hci_dev_set_flag(hdev, HCI_EVENT_FILTER_CONFIGURED);

	return rp->status;
}

static u8 hci_cc_read_class_of_dev(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_rp_read_class_of_dev *rp = data;

	if (WARN_ON(!hdev))
		return HCI_ERROR_UNSPECIFIED;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	memcpy(hdev->dev_class, rp->dev_class, 3);

	bt_dev_dbg(hdev, "class 0x%.2x%.2x%.2x", hdev->dev_class[2],
		   hdev->dev_class[1], hdev->dev_class[0]);

	return rp->status;
}

static u8 hci_cc_write_class_of_dev(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_CLASS_OF_DEV);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	if (!rp->status)
		memcpy(hdev->dev_class, sent, 3);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_set_class_of_dev_complete(hdev, sent, rp->status);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_voice_setting(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_rp_read_voice_setting *rp = data;
	__u16 setting;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	setting = __le16_to_cpu(rp->voice_setting);

	if (hdev->voice_setting == setting)
		return rp->status;

	hdev->voice_setting = setting;

	bt_dev_dbg(hdev, "voice setting 0x%4.4x", setting);

	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);

	return rp->status;
}

static u8 hci_cc_write_voice_setting(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	__u16 setting;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_VOICE_SETTING);
	if (!sent)
		return rp->status;

	setting = get_unaligned_le16(sent);

	if (hdev->voice_setting == setting)
		return rp->status;

	hdev->voice_setting = setting;

	bt_dev_dbg(hdev, "voice setting 0x%4.4x", setting);

	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);

	return rp->status;
}

static u8 hci_cc_read_num_supported_iac(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_read_num_supported_iac *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->num_iac = rp->num_iac;

	bt_dev_dbg(hdev, "num iac %d", hdev->num_iac);

	return rp->status;
}

static u8 hci_cc_write_ssp_mode(struct hci_dev *hdev, void *data,
				struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_write_ssp_mode *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SSP_MODE);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	if (!rp->status) {
		if (sent->mode)
			hdev->features[1][0] |= LMP_HOST_SSP;
		else
			hdev->features[1][0] &= ~LMP_HOST_SSP;
	}

	if (!rp->status) {
		if (sent->mode)
			hci_dev_set_flag(hdev, HCI_SSP_ENABLED);
		else
			hci_dev_clear_flag(hdev, HCI_SSP_ENABLED);
	}

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_write_sc_support(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_write_sc_support *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SC_SUPPORT);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	if (!rp->status) {
		if (sent->support)
			hdev->features[1][0] |= LMP_HOST_SC;
		else
			hdev->features[1][0] &= ~LMP_HOST_SC;
	}

	if (!hci_dev_test_flag(hdev, HCI_MGMT) && !rp->status) {
		if (sent->support)
			hci_dev_set_flag(hdev, HCI_SC_ENABLED);
		else
			hci_dev_clear_flag(hdev, HCI_SC_ENABLED);
	}

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_local_version(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_rp_read_local_version *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG)) {
		hdev->hci_ver = rp->hci_ver;
		hdev->hci_rev = __le16_to_cpu(rp->hci_rev);
		hdev->lmp_ver = rp->lmp_ver;
		hdev->manufacturer = __le16_to_cpu(rp->manufacturer);
		hdev->lmp_subver = __le16_to_cpu(rp->lmp_subver);
	}

	return rp->status;
}

static u8 hci_cc_read_enc_key_size(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_rp_read_enc_key_size *rp = data;
	struct hci_conn *conn;
	u16 handle;
	u8 status = rp->status;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	handle = le16_to_cpu(rp->handle);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, handle);
	if (!conn) {
		status = 0xFF;
		goto done;
	}

	/* While unexpected, the read_enc_key_size command may fail. The most
	 * secure approach is to then assume the key size is 0 to force a
	 * disconnection.
	 */
	if (status) {
		bt_dev_err(hdev, "failed to read key size for handle %u",
			   handle);
		conn->enc_key_size = 0;
	} else {
		u8 *key_enc_size = hci_conn_key_enc_size(conn);

		conn->enc_key_size = rp->key_size;
		status = 0;

		/* Attempt to check if the key size is too small or if it has
		 * been downgraded from the last time it was stored as part of
		 * the link_key.
		 */
		if (conn->enc_key_size < hdev->min_enc_key_size ||
		    (key_enc_size && conn->enc_key_size < *key_enc_size)) {
			/* As slave role, the conn->state has been set to
			 * BT_CONNECTED and l2cap conn req might not be received
			 * yet, at this moment the l2cap layer almost does
			 * nothing with the non-zero status.
			 * So we also clear encrypt related bits, and then the
			 * handler of l2cap conn req will get the right secure
			 * state at a later time.
			 */
			status = HCI_ERROR_AUTH_FAILURE;
			clear_bit(HCI_CONN_ENCRYPT, &conn->flags);
			clear_bit(HCI_CONN_AES_CCM, &conn->flags);
		}

		/* Update the key encryption size with the connection one */
		if (key_enc_size && *key_enc_size != conn->enc_key_size)
			*key_enc_size = conn->enc_key_size;
	}

	hci_encrypt_cfm(conn, status);

done:
	hci_dev_unlock(hdev);

	return status;
}

static u8 hci_cc_read_local_commands(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_rp_read_local_commands *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG))
		memcpy(hdev->commands, rp->commands, sizeof(hdev->commands));

	return rp->status;
}

static u8 hci_cc_read_auth_payload_timeout(struct hci_dev *hdev, void *data,
					   struct sk_buff *skb)
{
	struct hci_rp_read_auth_payload_to *rp = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->auth_payload_timeout = __le16_to_cpu(rp->timeout);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_write_auth_payload_timeout(struct hci_dev *hdev, void *data,
					    struct sk_buff *skb)
{
	struct hci_rp_write_auth_payload_to *rp = data;
	struct hci_conn *conn;
	void *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_AUTH_PAYLOAD_TO);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (!conn) {
		rp->status = 0xff;
		goto unlock;
	}

	if (!rp->status)
		conn->auth_payload_timeout = get_unaligned_le16(sent + 2);

unlock:
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_local_features(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_rp_read_local_features *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	memcpy(hdev->features, rp->features, 8);

	/* Adjust default settings according to features
	 * supported by device. */

	if (hdev->features[0][0] & LMP_3SLOT)
		hdev->pkt_type |= (HCI_DM3 | HCI_DH3);

	if (hdev->features[0][0] & LMP_5SLOT)
		hdev->pkt_type |= (HCI_DM5 | HCI_DH5);

	if (hdev->features[0][1] & LMP_HV2) {
		hdev->pkt_type  |= (HCI_HV2);
		hdev->esco_type |= (ESCO_HV2);
	}

	if (hdev->features[0][1] & LMP_HV3) {
		hdev->pkt_type  |= (HCI_HV3);
		hdev->esco_type |= (ESCO_HV3);
	}

	if (lmp_esco_capable(hdev))
		hdev->esco_type |= (ESCO_EV3);

	if (hdev->features[0][4] & LMP_EV4)
		hdev->esco_type |= (ESCO_EV4);

	if (hdev->features[0][4] & LMP_EV5)
		hdev->esco_type |= (ESCO_EV5);

	if (hdev->features[0][5] & LMP_EDR_ESCO_2M)
		hdev->esco_type |= (ESCO_2EV3);

	if (hdev->features[0][5] & LMP_EDR_ESCO_3M)
		hdev->esco_type |= (ESCO_3EV3);

	if (hdev->features[0][5] & LMP_EDR_3S_ESCO)
		hdev->esco_type |= (ESCO_2EV5 | ESCO_3EV5);

	return rp->status;
}

static u8 hci_cc_read_local_ext_features(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_rp_read_local_ext_features *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (hdev->max_page < rp->max_page) {
		if (hci_test_quirk(hdev,
				   HCI_QUIRK_BROKEN_LOCAL_EXT_FEATURES_PAGE_2))
			bt_dev_warn(hdev, "broken local ext features page 2");
		else
			hdev->max_page = rp->max_page;
	}

	if (rp->page < HCI_MAX_PAGES)
		memcpy(hdev->features[rp->page], rp->features, 8);

	return rp->status;
}

static u8 hci_cc_read_buffer_size(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_rp_read_buffer_size *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->acl_mtu  = __le16_to_cpu(rp->acl_mtu);
	hdev->sco_mtu  = rp->sco_mtu;
	hdev->acl_pkts = __le16_to_cpu(rp->acl_max_pkt);
	hdev->sco_pkts = __le16_to_cpu(rp->sco_max_pkt);

	if (hci_test_quirk(hdev, HCI_QUIRK_FIXUP_BUFFER_SIZE)) {
		hdev->sco_mtu  = 64;
		hdev->sco_pkts = 8;
	}

	if (!read_voice_setting_capable(hdev))
		hdev->sco_pkts = 0;

	hdev->acl_cnt = hdev->acl_pkts;
	hdev->sco_cnt = hdev->sco_pkts;

	BT_DBG("%s acl mtu %d:%d sco mtu %d:%d", hdev->name, hdev->acl_mtu,
	       hdev->acl_pkts, hdev->sco_mtu, hdev->sco_pkts);

	if (!hdev->acl_mtu || !hdev->acl_pkts)
		return HCI_ERROR_INVALID_PARAMETERS;

	return rp->status;
}

static u8 hci_cc_read_bd_addr(struct hci_dev *hdev, void *data,
			      struct sk_buff *skb)
{
	struct hci_rp_read_bd_addr *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (test_bit(HCI_INIT, &hdev->flags))
		bacpy(&hdev->bdaddr, &rp->bdaddr);

	if (hci_dev_test_flag(hdev, HCI_SETUP))
		bacpy(&hdev->setup_addr, &rp->bdaddr);

	return rp->status;
}

static u8 hci_cc_read_local_pairing_opts(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_rp_read_local_pairing_opts *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG)) {
		hdev->pairing_opts = rp->pairing_opts;
		hdev->max_enc_key_size = rp->max_key_size;
	}

	return rp->status;
}

static u8 hci_cc_read_page_scan_activity(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_rp_read_page_scan_activity *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (test_bit(HCI_INIT, &hdev->flags)) {
		hdev->page_scan_interval = __le16_to_cpu(rp->interval);
		hdev->page_scan_window = __le16_to_cpu(rp->window);
	}

	return rp->status;
}

static u8 hci_cc_write_page_scan_activity(struct hci_dev *hdev, void *data,
					  struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_write_page_scan_activity *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_PAGE_SCAN_ACTIVITY);
	if (!sent)
		return rp->status;

	hdev->page_scan_interval = __le16_to_cpu(sent->interval);
	hdev->page_scan_window = __le16_to_cpu(sent->window);

	return rp->status;
}

static u8 hci_cc_read_page_scan_type(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_rp_read_page_scan_type *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (test_bit(HCI_INIT, &hdev->flags))
		hdev->page_scan_type = rp->type;

	return rp->status;
}

static u8 hci_cc_write_page_scan_type(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	u8 *type;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	type = hci_sent_cmd_data(hdev, HCI_OP_WRITE_PAGE_SCAN_TYPE);
	if (type)
		hdev->page_scan_type = *type;

	return rp->status;
}

static u8 hci_cc_read_clock(struct hci_dev *hdev, void *data,
			    struct sk_buff *skb)
{
	struct hci_rp_read_clock *rp = data;
	struct hci_cp_read_clock *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_lock(hdev);

	cp = hci_sent_cmd_data(hdev, HCI_OP_READ_CLOCK);
	if (!cp)
		goto unlock;

	if (cp->which == 0x00) {
		hdev->clock = le32_to_cpu(rp->clock);
		goto unlock;
	}

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn) {
		conn->clock = le32_to_cpu(rp->clock);
		conn->clock_accuracy = le16_to_cpu(rp->accuracy);
	}

unlock:
	hci_dev_unlock(hdev);
	return rp->status;
}

static u8 hci_cc_read_inq_rsp_tx_power(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_rp_read_inq_rsp_tx_power *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->inq_tx_power = rp->tx_power;

	return rp->status;
}

static u8 hci_cc_read_def_err_data_reporting(struct hci_dev *hdev, void *data,
					     struct sk_buff *skb)
{
	struct hci_rp_read_def_err_data_reporting *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->err_data_reporting = rp->err_data_reporting;

	return rp->status;
}

static u8 hci_cc_write_def_err_data_reporting(struct hci_dev *hdev, void *data,
					      struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_write_def_err_data_reporting *cp;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_WRITE_DEF_ERR_DATA_REPORTING);
	if (!cp)
		return rp->status;

	hdev->err_data_reporting = cp->err_data_reporting;

	return rp->status;
}

static u8 hci_cc_pin_code_reply(struct hci_dev *hdev, void *data,
				struct sk_buff *skb)
{
	struct hci_rp_pin_code_reply *rp = data;
	struct hci_cp_pin_code_reply *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_pin_code_reply_complete(hdev, &rp->bdaddr, rp->status);

	if (rp->status)
		goto unlock;

	cp = hci_sent_cmd_data(hdev, HCI_OP_PIN_CODE_REPLY);
	if (!cp)
		goto unlock;

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (conn)
		conn->pin_length = cp->pin_len;

unlock:
	hci_dev_unlock(hdev);
	return rp->status;
}

static u8 hci_cc_pin_code_neg_reply(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_rp_pin_code_neg_reply *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_pin_code_neg_reply_complete(hdev, &rp->bdaddr,
						 rp->status);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_read_buffer_size(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_rp_le_read_buffer_size *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->le_mtu = __le16_to_cpu(rp->le_mtu);
	hdev->le_pkts = rp->le_max_pkt;

	hdev->le_cnt = hdev->le_pkts;

	BT_DBG("%s le mtu %d:%d", hdev->name, hdev->le_mtu, hdev->le_pkts);

	if (hdev->le_mtu && hdev->le_mtu < HCI_MIN_LE_MTU)
		return HCI_ERROR_INVALID_PARAMETERS;

	return rp->status;
}

static u8 hci_cc_le_read_local_features(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_le_read_local_features *rp = data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return rp->status;

	memcpy(hdev->le_features, rp->features, 8);

	return rp->status;
}

static u8 hci_cc_le_read_adv_tx_power(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_rp_le_read_adv_tx_power *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->adv_tx_power = rp->tx_power;

	return rp->status;
}

static u8 hci_cc_user_confirm_reply(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_confirm_reply_complete(hdev, &rp->bdaddr, ACL_LINK, 0,
						 rp->status);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_user_confirm_neg_reply(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_confirm_neg_reply_complete(hdev, &rp->bdaddr,
						     ACL_LINK, 0, rp->status);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_user_passkey_reply(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_reply_complete(hdev, &rp->bdaddr, ACL_LINK,
						 0, rp->status);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_user_passkey_neg_reply(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_neg_reply_complete(hdev, &rp->bdaddr,
						     ACL_LINK, 0, rp->status);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_local_oob_data(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_rp_read_local_oob_data *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	return rp->status;
}

static u8 hci_cc_read_local_oob_ext_data(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_rp_read_local_oob_ext_data *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	return rp->status;
}

static u8 hci_cc_le_set_random_addr(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	bdaddr_t *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_RANDOM_ADDR);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	bacpy(&hdev->random_addr, sent);

	if (!bacmp(&hdev->rpa, sent)) {
		hci_dev_clear_flag(hdev, HCI_RPA_EXPIRED);
		queue_delayed_work(hdev->workqueue, &hdev->rpa_expired,
				   secs_to_jiffies(hdev->rpa_timeout));
	}

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_set_default_phy(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_le_set_default_phy *cp;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_DEFAULT_PHY);
	if (!cp)
		return rp->status;

	hci_dev_lock(hdev);

	hdev->le_tx_def_phys = cp->tx_phys;
	hdev->le_rx_def_phys = cp->rx_phys;

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_set_adv_set_random_addr(struct hci_dev *hdev, void *data,
					    struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_le_set_adv_set_rand_addr *cp;
	struct adv_info *adv;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADV_SET_RAND_ADDR);
	/* Update only in case the adv instance since handle 0x00 shall be using
	 * HCI_OP_LE_SET_RANDOM_ADDR since that allows both extended and
	 * non-extended adverting.
	 */
	if (!cp || !cp->handle)
		return rp->status;

	hci_dev_lock(hdev);

	adv = hci_find_adv_instance(hdev, cp->handle);
	if (adv) {
		bacpy(&adv->random_addr, &cp->bdaddr);
		if (!bacmp(&hdev->rpa, &cp->bdaddr)) {
			adv->rpa_expired = false;
			queue_delayed_work(hdev->workqueue,
					   &adv->rpa_expired_cb,
					   secs_to_jiffies(hdev->rpa_timeout));
		}
	}

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_remove_adv_set(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	u8 *instance;
	int err;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	instance = hci_sent_cmd_data(hdev, HCI_OP_LE_REMOVE_ADV_SET);
	if (!instance)
		return rp->status;

	hci_dev_lock(hdev);

	err = hci_remove_adv_instance(hdev, *instance);
	if (!err)
		mgmt_advertising_removed(hci_skb_sk(hdev->sent_cmd), hdev,
					 *instance);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_clear_adv_sets(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct adv_info *adv, *n;
	int err;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	if (!hci_sent_cmd_data(hdev, HCI_OP_LE_CLEAR_ADV_SETS))
		return rp->status;

	hci_dev_lock(hdev);

	list_for_each_entry_safe(adv, n, &hdev->adv_instances, list) {
		u8 instance = adv->instance;

		err = hci_remove_adv_instance(hdev, instance);
		if (!err)
			mgmt_advertising_removed(hci_skb_sk(hdev->sent_cmd),
						 hdev, instance);
	}

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_read_transmit_power(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_le_read_transmit_power *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->min_le_tx_power = rp->min_le_tx_power;
	hdev->max_le_tx_power = rp->max_le_tx_power;

	return rp->status;
}

static u8 hci_cc_le_set_privacy_mode(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_le_set_privacy_mode *cp;
	struct hci_conn_params *params;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_PRIVACY_MODE);
	if (!cp)
		return rp->status;

	hci_dev_lock(hdev);

	params = hci_conn_params_lookup(hdev, &cp->bdaddr, cp->bdaddr_type);
	if (params)
		WRITE_ONCE(params->privacy_mode, cp->mode);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_set_adv_enable(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	__u8 *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADV_ENABLE);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	/* If we're doing connection initiation as peripheral. Set a
	 * timeout in case something goes wrong.
	 */
	if (*sent) {
		struct hci_conn *conn;

		hci_dev_set_flag(hdev, HCI_LE_ADV);

		conn = hci_lookup_le_connect(hdev);
		if (conn)
			queue_delayed_work(hdev->workqueue,
					   &conn->le_conn_timeout,
					   conn->conn_timeout);
	} else {
		hci_dev_clear_flag(hdev, HCI_LE_ADV);
	}

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_set_ext_adv_enable(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_cp_le_set_ext_adv_enable *cp;
	struct hci_cp_ext_adv_set *set;
	struct adv_info *adv = NULL, *n;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_EXT_ADV_ENABLE);
	if (!cp)
		return rp->status;

	set = (void *)cp->data;

	hci_dev_lock(hdev);

	if (cp->num_of_sets)
		adv = hci_find_adv_instance(hdev, set->handle);

	if (cp->enable) {
		struct hci_conn *conn;

		hci_dev_set_flag(hdev, HCI_LE_ADV);

		if (adv)
			adv->enabled = true;
		else if (!set->handle)
			hci_dev_set_flag(hdev, HCI_LE_ADV_0);

		conn = hci_lookup_le_connect(hdev);
		if (conn)
			queue_delayed_work(hdev->workqueue,
					   &conn->le_conn_timeout,
					   conn->conn_timeout);
	} else {
		if (cp->num_of_sets) {
			if (adv)
				adv->enabled = false;
			else if (!set->handle)
				hci_dev_clear_flag(hdev, HCI_LE_ADV_0);

			/* If just one instance was disabled check if there are
			 * any other instance enabled before clearing HCI_LE_ADV
			 */
			list_for_each_entry_safe(adv, n, &hdev->adv_instances,
						 list) {
				if (adv->enabled)
					goto unlock;
			}
		} else {
			/* All instances shall be considered disabled */
			list_for_each_entry_safe(adv, n, &hdev->adv_instances,
						 list)
				adv->enabled = false;
		}

		hci_dev_clear_flag(hdev, HCI_LE_ADV);
	}

unlock:
	hci_dev_unlock(hdev);
	return rp->status;
}

static u8 hci_cc_le_set_scan_param(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_cp_le_set_scan_param *cp;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_SCAN_PARAM);
	if (!cp)
		return rp->status;

	hci_dev_lock(hdev);

	hdev->le_scan_type = cp->type;

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_set_ext_scan_param(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_cp_le_set_ext_scan_params *cp;
	struct hci_ev_status *rp = data;
	struct hci_cp_le_scan_phy_params *phy_param;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_EXT_SCAN_PARAMS);
	if (!cp)
		return rp->status;

	phy_param = (void *)cp->data;

	hci_dev_lock(hdev);

	hdev->le_scan_type = phy_param->type;

	hci_dev_unlock(hdev);

	return rp->status;
}

static bool has_pending_adv_report(struct hci_dev *hdev)
{
	struct discovery_state *d = &hdev->discovery;

	return bacmp(&d->last_adv_addr, BDADDR_ANY);
}

static void clear_pending_adv_report(struct hci_dev *hdev)
{
	struct discovery_state *d = &hdev->discovery;

	bacpy(&d->last_adv_addr, BDADDR_ANY);
	d->last_adv_data_len = 0;
}

static void store_pending_adv_report(struct hci_dev *hdev, bdaddr_t *bdaddr,
				     u8 bdaddr_type, s8 rssi, u32 flags,
				     u8 *data, u8 len)
{
	struct discovery_state *d = &hdev->discovery;

	if (len > max_adv_len(hdev))
		return;

	bacpy(&d->last_adv_addr, bdaddr);
	d->last_adv_addr_type = bdaddr_type;
	d->last_adv_rssi = rssi;
	d->last_adv_flags = flags;
	memcpy(d->last_adv_data, data, len);
	d->last_adv_data_len = len;
}

static void le_set_scan_enable_complete(struct hci_dev *hdev, u8 enable)
{
	hci_dev_lock(hdev);

	switch (enable) {
	case LE_SCAN_ENABLE:
		hci_dev_set_flag(hdev, HCI_LE_SCAN);
		if (hdev->le_scan_type == LE_SCAN_ACTIVE) {
			clear_pending_adv_report(hdev);
			hci_discovery_set_state(hdev, DISCOVERY_FINDING);
		}
		break;

	case LE_SCAN_DISABLE:
		/* We do this here instead of when setting DISCOVERY_STOPPED
		 * since the latter would potentially require waiting for
		 * inquiry to stop too.
		 */
		if (has_pending_adv_report(hdev)) {
			struct discovery_state *d = &hdev->discovery;

			mgmt_device_found(hdev, &d->last_adv_addr, LE_LINK,
					  d->last_adv_addr_type, NULL,
					  d->last_adv_rssi, d->last_adv_flags,
					  d->last_adv_data,
					  d->last_adv_data_len, NULL, 0, 0);
		}

		/* Cancel this timer so that we don't try to disable scanning
		 * when it's already disabled.
		 */
		cancel_delayed_work(&hdev->le_scan_disable);

		hci_dev_clear_flag(hdev, HCI_LE_SCAN);

		/* The HCI_LE_SCAN_INTERRUPTED flag indicates that we
		 * interrupted scanning due to a connect request. Mark
		 * therefore discovery as stopped.
		 */
		if (hci_dev_test_and_clear_flag(hdev, HCI_LE_SCAN_INTERRUPTED))
			hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
		else if (!hci_dev_test_flag(hdev, HCI_LE_ADV) &&
			 hdev->discovery.state == DISCOVERY_FINDING)
			queue_work(hdev->workqueue, &hdev->reenable_adv_work);

		break;

	default:
		bt_dev_err(hdev, "use of reserved LE_Scan_Enable param %d",
			   enable);
		break;
	}

	hci_dev_unlock(hdev);
}

static u8 hci_cc_le_set_scan_enable(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_cp_le_set_scan_enable *cp;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_SCAN_ENABLE);
	if (!cp)
		return rp->status;

	le_set_scan_enable_complete(hdev, cp->enable);

	return rp->status;
}

static u8 hci_cc_le_set_ext_scan_enable(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_cp_le_set_ext_scan_enable *cp;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_EXT_SCAN_ENABLE);
	if (!cp)
		return rp->status;

	le_set_scan_enable_complete(hdev, cp->enable);

	return rp->status;
}

static u8 hci_cc_le_read_num_adv_sets(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_rp_le_read_num_supported_adv_sets *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x No of Adv sets %u", rp->status,
		   rp->num_of_sets);

	if (rp->status)
		return rp->status;

	hdev->le_num_of_adv_sets = rp->num_of_sets;

	return rp->status;
}

static u8 hci_cc_le_read_accept_list_size(struct hci_dev *hdev, void *data,
					  struct sk_buff *skb)
{
	struct hci_rp_le_read_accept_list_size *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x size %u", rp->status, rp->size);

	if (rp->status)
		return rp->status;

	hdev->le_accept_list_size = rp->size;

	return rp->status;
}

static u8 hci_cc_le_clear_accept_list(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_lock(hdev);
	hci_bdaddr_list_clear(&hdev->le_accept_list);
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_add_to_accept_list(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_cp_le_add_to_accept_list *sent;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_ADD_TO_ACCEPT_LIST);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);
	hci_bdaddr_list_add(&hdev->le_accept_list, &sent->bdaddr,
			    sent->bdaddr_type);
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_del_from_accept_list(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_cp_le_del_from_accept_list *sent;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_DEL_FROM_ACCEPT_LIST);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);
	hci_bdaddr_list_del(&hdev->le_accept_list, &sent->bdaddr,
			    sent->bdaddr_type);
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_read_supported_states(struct hci_dev *hdev, void *data,
					  struct sk_buff *skb)
{
	struct hci_rp_le_read_supported_states *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	memcpy(hdev->le_states, rp->le_states, 8);

	return rp->status;
}

static u8 hci_cc_le_read_def_data_len(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_rp_le_read_def_data_len *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->le_def_tx_len = le16_to_cpu(rp->tx_len);
	hdev->le_def_tx_time = le16_to_cpu(rp->tx_time);

	return rp->status;
}

static u8 hci_cc_le_write_def_data_len(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_cp_le_write_def_data_len *sent;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_WRITE_DEF_DATA_LEN);
	if (!sent)
		return rp->status;

	hdev->le_def_tx_len = le16_to_cpu(sent->tx_len);
	hdev->le_def_tx_time = le16_to_cpu(sent->tx_time);

	return rp->status;
}

static u8 hci_cc_le_add_to_resolv_list(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_cp_le_add_to_resolv_list *sent;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_ADD_TO_RESOLV_LIST);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);
	hci_bdaddr_list_add_with_irk(&hdev->le_resolv_list, &sent->bdaddr,
				sent->bdaddr_type, sent->peer_irk,
				sent->local_irk);
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_del_from_resolv_list(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_cp_le_del_from_resolv_list *sent;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_DEL_FROM_RESOLV_LIST);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);
	hci_bdaddr_list_del_with_irk(&hdev->le_resolv_list, &sent->bdaddr,
			    sent->bdaddr_type);
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_clear_resolv_list(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_lock(hdev);
	hci_bdaddr_list_clear(&hdev->le_resolv_list);
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_read_resolv_list_size(struct hci_dev *hdev, void *data,
					  struct sk_buff *skb)
{
	struct hci_rp_le_read_resolv_list_size *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x size %u", rp->status, rp->size);

	if (rp->status)
		return rp->status;

	hdev->le_resolv_list_size = rp->size;

	return rp->status;
}

static u8 hci_cc_le_set_addr_resolution_enable(struct hci_dev *hdev, void *data,
					       struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	__u8 *sent;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADDR_RESOLV_ENABLE);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	if (*sent)
		hci_dev_set_flag(hdev, HCI_LL_RPA_RESOLUTION);
	else
		hci_dev_clear_flag(hdev, HCI_LL_RPA_RESOLUTION);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_read_max_data_len(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_rp_le_read_max_data_len *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->le_max_tx_len = le16_to_cpu(rp->tx_len);
	hdev->le_max_tx_time = le16_to_cpu(rp->tx_time);
	hdev->le_max_rx_len = le16_to_cpu(rp->rx_len);
	hdev->le_max_rx_time = le16_to_cpu(rp->rx_time);

	return rp->status;
}

static u8 hci_cc_write_le_host_supported(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_cp_write_le_host_supported *sent;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_LE_HOST_SUPPORTED);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	if (sent->le) {
		hdev->features[1][0] |= LMP_HOST_LE;
		hci_dev_set_flag(hdev, HCI_LE_ENABLED);
	} else {
		hdev->features[1][0] &= ~LMP_HOST_LE;
		hci_dev_clear_flag(hdev, HCI_LE_ENABLED);
		hci_dev_clear_flag(hdev, HCI_ADVERTISING);
	}

	if (sent->simul)
		hdev->features[1][0] |= LMP_HOST_LE_BREDR;
	else
		hdev->features[1][0] &= ~LMP_HOST_LE_BREDR;

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_set_adv_param(struct hci_dev *hdev, void *data,
			       struct sk_buff *skb)
{
	struct hci_cp_le_set_adv_param *cp;
	struct hci_ev_status *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADV_PARAM);
	if (!cp)
		return rp->status;

	hci_dev_lock(hdev);
	hdev->adv_addr_type = cp->own_address_type;
	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_rssi(struct hci_dev *hdev, void *data,
			   struct sk_buff *skb)
{
	struct hci_rp_read_rssi *rp = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->rssi = rp->rssi;

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_read_tx_power(struct hci_dev *hdev, void *data,
			       struct sk_buff *skb)
{
	struct hci_cp_read_tx_power *sent;
	struct hci_rp_read_tx_power *rp = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	sent = hci_sent_cmd_data(hdev, HCI_OP_READ_TX_POWER);
	if (!sent)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (!conn)
		goto unlock;

	switch (sent->type) {
	case 0x00:
		conn->tx_power = rp->tx_power;
		break;
	case 0x01:
		conn->max_tx_power = rp->tx_power;
		break;
	}

unlock:
	hci_dev_unlock(hdev);
	return rp->status;
}

static u8 hci_cc_write_ssp_debug_mode(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	u8 *mode;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	mode = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SSP_DEBUG_MODE);
	if (mode)
		hdev->ssp_debug_mode = *mode;

	return rp->status;
}

static void hci_cs_inquiry(struct hci_dev *hdev, __u8 status)
{
	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (status)
		return;

	if (hci_sent_cmd_data(hdev, HCI_OP_INQUIRY))
		set_bit(HCI_INQUIRY, &hdev->flags);
}

static void hci_cs_create_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_create_conn *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	cp = hci_sent_cmd_data(hdev, HCI_OP_CREATE_CONN);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);

	bt_dev_dbg(hdev, "bdaddr %pMR hcon %p", &cp->bdaddr, conn);

	if (status) {
		if (conn && conn->state == BT_CONNECT) {
			conn->state = BT_CLOSED;
			hci_connect_cfm(conn, status);
			hci_conn_del(conn);
		}
	} else {
		if (!conn) {
			conn = hci_conn_add_unset(hdev, ACL_LINK, &cp->bdaddr,
						  HCI_ROLE_MASTER);
			if (IS_ERR(conn))
				bt_dev_err(hdev, "connection err: %ld", PTR_ERR(conn));
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_add_sco(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_add_sco *cp;
	struct hci_conn *acl;
	struct hci_link *link;
	__u16 handle;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_ADD_SCO);
	if (!cp)
		return;

	handle = __le16_to_cpu(cp->handle);

	bt_dev_dbg(hdev, "handle 0x%4.4x", handle);

	hci_dev_lock(hdev);

	acl = hci_conn_hash_lookup_handle(hdev, handle);
	if (acl) {
		link = list_first_entry_or_null(&acl->link_list,
						struct hci_link, list);
		if (link && link->conn) {
			link->conn->state = BT_CLOSED;

			hci_connect_cfm(link->conn, status);
			hci_conn_del(link->conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_auth_requested(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_auth_requested *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_AUTH_REQUESTED);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_connect_cfm(conn, status);
			hci_conn_drop(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_set_conn_encrypt(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_set_conn_encrypt *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SET_CONN_ENCRYPT);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_connect_cfm(conn, status);
			hci_conn_drop(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static int hci_outgoing_auth_needed(struct hci_dev *hdev,
				    struct hci_conn *conn)
{
	if (conn->state != BT_CONFIG || !conn->out)
		return 0;

	if (conn->pending_sec_level == BT_SECURITY_SDP)
		return 0;

	/* Only request authentication for SSP connections or non-SSP
	 * devices with sec_level MEDIUM or HIGH or if MITM protection
	 * is requested.
	 */
	if (!hci_conn_ssp_enabled(conn) && !(conn->auth_type & 0x01) &&
	    conn->pending_sec_level != BT_SECURITY_FIPS &&
	    conn->pending_sec_level != BT_SECURITY_HIGH &&
	    conn->pending_sec_level != BT_SECURITY_MEDIUM)
		return 0;

	return 1;
}

static int hci_resolve_name(struct hci_dev *hdev,
				   struct inquiry_entry *e)
{
	struct hci_cp_remote_name_req cp;

	memset(&cp, 0, sizeof(cp));

	bacpy(&cp.bdaddr, &e->data.bdaddr);
	cp.pscan_rep_mode = e->data.pscan_rep_mode;
	cp.pscan_mode = e->data.pscan_mode;
	cp.clock_offset = e->data.clock_offset;

	return hci_send_cmd(hdev, HCI_OP_REMOTE_NAME_REQ, sizeof(cp), &cp);
}

static bool hci_resolve_next_name(struct hci_dev *hdev)
{
	struct discovery_state *discov = &hdev->discovery;
	struct inquiry_entry *e;

	if (list_empty(&discov->resolve))
		return false;

	/* We should stop if we already spent too much time resolving names. */
	if (time_after(jiffies, discov->name_resolve_timeout)) {
		bt_dev_warn_ratelimited(hdev, "Name resolve takes too long.");
		return false;
	}

	e = hci_inquiry_cache_lookup_resolve(hdev, BDADDR_ANY, NAME_NEEDED);
	if (!e)
		return false;

	if (hci_resolve_name(hdev, e) == 0) {
		e->name_state = NAME_PENDING;
		return true;
	}

	return false;
}

static void hci_check_pending_name(struct hci_dev *hdev, struct hci_conn *conn,
				   bdaddr_t *bdaddr, u8 *name, u8 name_len)
{
	struct discovery_state *discov = &hdev->discovery;
	struct inquiry_entry *e;

	/* Update the mgmt connected state if necessary. Be careful with
	 * conn objects that exist but are not (yet) connected however.
	 * Only those in BT_CONFIG or BT_CONNECTED states can be
	 * considered connected.
	 */
	if (conn && (conn->state == BT_CONFIG || conn->state == BT_CONNECTED))
		mgmt_device_connected(hdev, conn, name, name_len);

	if (discov->state == DISCOVERY_STOPPED)
		return;

	if (discov->state == DISCOVERY_STOPPING)
		goto discov_complete;

	if (discov->state != DISCOVERY_RESOLVING)
		return;

	e = hci_inquiry_cache_lookup_resolve(hdev, bdaddr, NAME_PENDING);
	/* If the device was not found in a list of found devices names of which
	 * are pending. there is no need to continue resolving a next name as it
	 * will be done upon receiving another Remote Name Request Complete
	 * Event */
	if (!e)
		return;

	list_del(&e->list);

	e->name_state = name ? NAME_KNOWN : NAME_NOT_KNOWN;
	mgmt_remote_name(hdev, bdaddr, ACL_LINK, 0x00, e->data.rssi,
			 name, name_len);

	if (hci_resolve_next_name(hdev))
		return;

discov_complete:
	hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
}

static void hci_cs_remote_name_req(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_remote_name_req *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	/* If successful wait for the name req complete event before
	 * checking for the need to do authentication */
	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_REMOTE_NAME_REQ);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		hci_check_pending_name(hdev, conn, &cp->bdaddr, NULL, 0);

	if (!conn)
		goto unlock;

	if (!hci_outgoing_auth_needed(hdev, conn))
		goto unlock;

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->flags)) {
		struct hci_cp_auth_requested auth_cp;

		set_bit(HCI_CONN_AUTH_INITIATOR, &conn->flags);

		auth_cp.handle = __cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_AUTH_REQUESTED,
			     sizeof(auth_cp), &auth_cp);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_cs_read_remote_features(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_read_remote_features *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_READ_REMOTE_FEATURES);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_connect_cfm(conn, status);
			hci_conn_drop(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_read_remote_ext_features(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_read_remote_ext_features *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_READ_REMOTE_EXT_FEATURES);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_connect_cfm(conn, status);
			hci_conn_drop(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_setup_sync_conn_status(struct hci_dev *hdev, __u16 handle,
				       __u8 status)
{
	struct hci_conn *acl;
	struct hci_link *link;

	bt_dev_dbg(hdev, "handle 0x%4.4x status 0x%2.2x", handle, status);

	hci_dev_lock(hdev);

	acl = hci_conn_hash_lookup_handle(hdev, handle);
	if (acl) {
		link = list_first_entry_or_null(&acl->link_list,
						struct hci_link, list);
		if (link && link->conn) {
			link->conn->state = BT_CLOSED;

			hci_connect_cfm(link->conn, status);
			hci_conn_del(link->conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_setup_sync_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_setup_sync_conn *cp;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SETUP_SYNC_CONN);
	if (!cp)
		return;

	hci_setup_sync_conn_status(hdev, __le16_to_cpu(cp->handle), status);
}

static void hci_cs_enhanced_setup_sync_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_enhanced_setup_sync_conn *cp;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_ENHANCED_SETUP_SYNC_CONN);
	if (!cp)
		return;

	hci_setup_sync_conn_status(hdev, __le16_to_cpu(cp->handle), status);
}

static void hci_cs_sniff_mode(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_sniff_mode *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SNIFF_MODE);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		clear_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->flags);

		if (test_and_clear_bit(HCI_CONN_SCO_SETUP_PEND, &conn->flags))
			hci_sco_setup(conn, status);
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_exit_sniff_mode(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_exit_sniff_mode *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_EXIT_SNIFF_MODE);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		clear_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->flags);

		if (test_and_clear_bit(HCI_CONN_SCO_SETUP_PEND, &conn->flags))
			hci_sco_setup(conn, status);
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_disconnect(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_disconnect *cp;
	struct hci_conn_params *params;
	struct hci_conn *conn;
	bool mgmt_conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	/* Wait for HCI_EV_DISCONN_COMPLETE if status 0x00 and not suspended
	 * otherwise cleanup the connection immediately.
	 */
	if (!status && !hdev->suspended)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_DISCONNECT);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (!conn)
		goto unlock;

	if (status && status != HCI_ERROR_UNKNOWN_CONN_ID) {
		mgmt_disconnect_failed(hdev, &conn->dst, conn->type,
				       conn->dst_type, status);

		if (conn->type == LE_LINK && conn->role == HCI_ROLE_SLAVE) {
			hdev->cur_adv_instance = conn->adv_instance;
			hci_enable_advertising(hdev);
		}

		/* Inform sockets conn is gone before we delete it */
		hci_disconn_cfm(conn, HCI_ERROR_UNSPECIFIED);

		goto done;
	}

	/* During suspend, mark connection as closed immediately
	 * since we might not receive HCI_EV_DISCONN_COMPLETE
	 */
	if (hdev->suspended)
		conn->state = BT_CLOSED;

	mgmt_conn = test_and_clear_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags);

	if (conn->type == ACL_LINK) {
		if (test_and_clear_bit(HCI_CONN_FLUSH_KEY, &conn->flags))
			hci_remove_link_key(hdev, &conn->dst);
	}

	params = hci_conn_params_lookup(hdev, &conn->dst, conn->dst_type);
	if (params) {
		switch (params->auto_connect) {
		case HCI_AUTO_CONN_LINK_LOSS:
			if (cp->reason != HCI_ERROR_CONNECTION_TIMEOUT)
				break;
			fallthrough;

		case HCI_AUTO_CONN_DIRECT:
		case HCI_AUTO_CONN_ALWAYS:
			hci_pend_le_list_del_init(params);
			hci_pend_le_list_add(params, &hdev->pend_le_conns);
			break;

		default:
			break;
		}
	}

	mgmt_device_disconnected(hdev, &conn->dst, conn->type, conn->dst_type,
				 cp->reason, mgmt_conn);

	hci_disconn_cfm(conn, cp->reason);

done:
	/* If the disconnection failed for any reason, the upper layer
	 * does not retry to disconnect in current implementation.
	 * Hence, we need to do some basic cleanup here and re-enable
	 * advertising if necessary.
	 */
	hci_conn_del(conn);
unlock:
	hci_dev_unlock(hdev);
}

static u8 ev_bdaddr_type(struct hci_dev *hdev, u8 type, bool *resolved)
{
	/* When using controller based address resolution, then the new
	 * address types 0x02 and 0x03 are used. These types need to be
	 * converted back into either public address or random address type
	 */
	switch (type) {
	case ADDR_LE_DEV_PUBLIC_RESOLVED:
		if (resolved)
			*resolved = true;
		return ADDR_LE_DEV_PUBLIC;
	case ADDR_LE_DEV_RANDOM_RESOLVED:
		if (resolved)
			*resolved = true;
		return ADDR_LE_DEV_RANDOM;
	}

	if (resolved)
		*resolved = false;
	return type;
}

static void cs_le_create_conn(struct hci_dev *hdev, bdaddr_t *peer_addr,
			      u8 peer_addr_type, u8 own_address_type,
			      u8 filter_policy)
{
	struct hci_conn *conn;

	conn = hci_conn_hash_lookup_le(hdev, peer_addr,
				       peer_addr_type);
	if (!conn)
		return;

	own_address_type = ev_bdaddr_type(hdev, own_address_type, NULL);

	/* Store the initiator and responder address information which
	 * is needed for SMP. These values will not change during the
	 * lifetime of the connection.
	 */
	conn->init_addr_type = own_address_type;
	if (own_address_type == ADDR_LE_DEV_RANDOM)
		bacpy(&conn->init_addr, &hdev->random_addr);
	else
		bacpy(&conn->init_addr, &hdev->bdaddr);

	conn->resp_addr_type = peer_addr_type;
	bacpy(&conn->resp_addr, peer_addr);
}

static void hci_cs_le_create_conn(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_le_create_conn *cp;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	/* All connection failure handling is taken care of by the
	 * hci_conn_failed function which is triggered by the HCI
	 * request completion callbacks used for connecting.
	 */
	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_CREATE_CONN);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	cs_le_create_conn(hdev, &cp->peer_addr, cp->peer_addr_type,
			  cp->own_address_type, cp->filter_policy);

	hci_dev_unlock(hdev);
}

static void hci_cs_le_ext_create_conn(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_le_ext_create_conn *cp;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	/* All connection failure handling is taken care of by the
	 * hci_conn_failed function which is triggered by the HCI
	 * request completion callbacks used for connecting.
	 */
	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_EXT_CREATE_CONN);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	cs_le_create_conn(hdev, &cp->peer_addr, cp->peer_addr_type,
			  cp->own_addr_type, cp->filter_policy);

	hci_dev_unlock(hdev);
}

static void hci_cs_le_read_remote_features(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_le_read_remote_features *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_READ_REMOTE_FEATURES);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_connect_cfm(conn, status);
			hci_conn_drop(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_le_start_enc(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_le_start_enc *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	hci_dev_lock(hdev);

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_START_ENC);
	if (!cp)
		goto unlock;

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (!conn)
		goto unlock;

	if (conn->state != BT_CONNECTED)
		goto unlock;

	hci_disconnect(conn, HCI_ERROR_AUTH_FAILURE);
	hci_conn_drop(conn);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_cs_switch_role(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_switch_role *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SWITCH_ROLE);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (conn)
		clear_bit(HCI_CONN_RSWITCH_PEND, &conn->flags);

	hci_dev_unlock(hdev);
}

static void hci_inquiry_complete_evt(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_ev_status *ev = data;
	struct discovery_state *discov = &hdev->discovery;
	struct inquiry_entry *e;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	if (!test_and_clear_bit(HCI_INQUIRY, &hdev->flags))
		return;

	smp_mb__after_atomic(); /* wake_up_bit advises about this barrier */
	wake_up_bit(&hdev->flags, HCI_INQUIRY);

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		return;

	hci_dev_lock(hdev);

	if (discov->state != DISCOVERY_FINDING)
		goto unlock;

	if (list_empty(&discov->resolve)) {
		/* When BR/EDR inquiry is active and no LE scanning is in
		 * progress, then change discovery state to indicate completion.
		 *
		 * When running LE scanning and BR/EDR inquiry simultaneously
		 * and the LE scan already finished, then change the discovery
		 * state to indicate completion.
		 */
		if (!hci_dev_test_flag(hdev, HCI_LE_SCAN) ||
		    !hci_test_quirk(hdev, HCI_QUIRK_SIMULTANEOUS_DISCOVERY))
			hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
		goto unlock;
	}

	e = hci_inquiry_cache_lookup_resolve(hdev, BDADDR_ANY, NAME_NEEDED);
	if (e && hci_resolve_name(hdev, e) == 0) {
		e->name_state = NAME_PENDING;
		hci_discovery_set_state(hdev, DISCOVERY_RESOLVING);
		discov->name_resolve_timeout = jiffies + NAME_RESOLVE_DURATION;
	} else {
		/* When BR/EDR inquiry is active and no LE scanning is in
		 * progress, then change discovery state to indicate completion.
		 *
		 * When running LE scanning and BR/EDR inquiry simultaneously
		 * and the LE scan already finished, then change the discovery
		 * state to indicate completion.
		 */
		if (!hci_dev_test_flag(hdev, HCI_LE_SCAN) ||
		    !hci_test_quirk(hdev, HCI_QUIRK_SIMULTANEOUS_DISCOVERY))
			hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_inquiry_result_evt(struct hci_dev *hdev, void *edata,
				   struct sk_buff *skb)
{
	struct hci_ev_inquiry_result *ev = edata;
	struct inquiry_data data;
	int i;

	if (!hci_ev_skb_pull(hdev, skb, HCI_EV_INQUIRY_RESULT,
			     flex_array_size(ev, info, ev->num)))
		return;

	bt_dev_dbg(hdev, "num %d", ev->num);

	if (!ev->num)
		return;

	if (hci_dev_test_flag(hdev, HCI_PERIODIC_INQ))
		return;

	hci_dev_lock(hdev);

	for (i = 0; i < ev->num; i++) {
		struct inquiry_info *info = &ev->info[i];
		u32 flags;

		bacpy(&data.bdaddr, &info->bdaddr);
		data.pscan_rep_mode	= info->pscan_rep_mode;
		data.pscan_period_mode	= info->pscan_period_mode;
		data.pscan_mode		= info->pscan_mode;
		memcpy(data.dev_class, info->dev_class, 3);
		data.clock_offset	= info->clock_offset;
		data.rssi		= HCI_RSSI_INVALID;
		data.ssp_mode		= 0x00;

		flags = hci_inquiry_cache_update(hdev, &data, false);

		mgmt_device_found(hdev, &info->bdaddr, ACL_LINK, 0x00,
				  info->dev_class, HCI_RSSI_INVALID,
				  flags, NULL, 0, NULL, 0, 0);
	}

	hci_dev_unlock(hdev);
}

static int hci_read_enc_key_size(struct hci_dev *hdev, struct hci_conn *conn)
{
	struct hci_cp_read_enc_key_size cp;
	u8 *key_enc_size = hci_conn_key_enc_size(conn);

	if (!read_key_size_capable(hdev)) {
		conn->enc_key_size = HCI_LINK_KEY_SIZE;
		return -EOPNOTSUPP;
	}

	bt_dev_dbg(hdev, "hcon %p", conn);

	memset(&cp, 0, sizeof(cp));
	cp.handle = cpu_to_le16(conn->handle);

	/* If the key enc_size is already known, use it as conn->enc_key_size,
	 * otherwise use hdev->min_enc_key_size so the likes of
	 * l2cap_check_enc_key_size don't fail while waiting for
	 * HCI_OP_READ_ENC_KEY_SIZE response.
	 */
	if (key_enc_size && *key_enc_size)
		conn->enc_key_size = *key_enc_size;
	else
		conn->enc_key_size = hdev->min_enc_key_size;

	return hci_send_cmd(hdev, HCI_OP_READ_ENC_KEY_SIZE, sizeof(cp), &cp);
}

static void hci_conn_complete_evt(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_conn_complete *ev = data;
	struct hci_conn *conn;
	u8 status = ev->status;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	hci_dev_lock(hdev);

	/* Check for existing connection:
	 *
	 * 1. If it doesn't exist then it must be receiver/slave role.
	 * 2. If it does exist confirm that it is connecting/BT_CONNECT in case
	 *    of initiator/master role since there could be a collision where
	 *    either side is attempting to connect or something like a fuzzing
	 *    testing is trying to play tricks to destroy the hcon object before
	 *    it even attempts to connect (e.g. hcon->state == BT_OPEN).
	 */
	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
	if (!conn ||
	    (conn->role == HCI_ROLE_MASTER && conn->state != BT_CONNECT)) {
		/* In case of error status and there is no connection pending
		 * just unlock as there is nothing to cleanup.
		 */
		if (ev->status)
			goto unlock;

		/* Connection may not exist if auto-connected. Check the bredr
		 * allowlist to see if this device is allowed to auto connect.
		 * If link is an ACL type, create a connection class
		 * automatically.
		 *
		 * Auto-connect will only occur if the event filter is
		 * programmed with a given address. Right now, event filter is
		 * only used during suspend.
		 */
		if (ev->link_type == ACL_LINK &&
		    hci_bdaddr_list_lookup_with_flags(&hdev->accept_list,
						      &ev->bdaddr,
						      BDADDR_BREDR)) {
			conn = hci_conn_add_unset(hdev, ev->link_type,
						  &ev->bdaddr, HCI_ROLE_SLAVE);
			if (IS_ERR(conn)) {
				bt_dev_err(hdev, "connection err: %ld", PTR_ERR(conn));
				goto unlock;
			}
		} else {
			if (ev->link_type != SCO_LINK)
				goto unlock;

			conn = hci_conn_hash_lookup_ba(hdev, ESCO_LINK,
						       &ev->bdaddr);
			if (!conn)
				goto unlock;

			conn->type = SCO_LINK;
		}
	}

	/* The HCI_Connection_Complete event is only sent once per connection.
	 * Processing it more than once per connection can corrupt kernel memory.
	 *
	 * As the connection handle is set here for the first time, it indicates
	 * whether the connection is already set up.
	 */
	if (!HCI_CONN_HANDLE_UNSET(conn->handle)) {
		bt_dev_err(hdev, "Ignoring HCI_Connection_Complete for existing connection");
		goto unlock;
	}

	if (!status) {
		status = hci_conn_set_handle(conn, __le16_to_cpu(ev->handle));
		if (status)
			goto done;

		if (conn->type == ACL_LINK) {
			conn->state = BT_CONFIG;
			hci_conn_hold(conn);

			if (!conn->out && !hci_conn_ssp_enabled(conn) &&
			    !hci_find_link_key(hdev, &ev->bdaddr))
				conn->disc_timeout = HCI_PAIRING_TIMEOUT;
			else
				conn->disc_timeout = HCI_DISCONN_TIMEOUT;
		} else
			conn->state = BT_CONNECTED;

		hci_debugfs_create_conn(conn);
		hci_conn_add_sysfs(conn);

		if (test_bit(HCI_AUTH, &hdev->flags))
			set_bit(HCI_CONN_AUTH, &conn->flags);

		if (test_bit(HCI_ENCRYPT, &hdev->flags))
			set_bit(HCI_CONN_ENCRYPT, &conn->flags);

		/* "Link key request" completed ahead of "connect request" completes */
		if (ev->encr_mode == 1 && !test_bit(HCI_CONN_ENCRYPT, &conn->flags) &&
		    ev->link_type == ACL_LINK) {
			struct link_key *key;

			key = hci_find_link_key(hdev, &ev->bdaddr);
			if (key) {
				set_bit(HCI_CONN_ENCRYPT, &conn->flags);
				hci_read_enc_key_size(hdev, conn);
				hci_encrypt_cfm(conn, ev->status);
			}
		}

		/* Get remote features */
		if (conn->type == ACL_LINK) {
			struct hci_cp_read_remote_features cp;
			cp.handle = ev->handle;
			hci_send_cmd(hdev, HCI_OP_READ_REMOTE_FEATURES,
				     sizeof(cp), &cp);

			hci_update_scan(hdev);
		}

		/* Set packet type for incoming connection */
		if (!conn->out && hdev->hci_ver < BLUETOOTH_VER_2_0) {
			struct hci_cp_change_conn_ptype cp;
			cp.handle = ev->handle;
			cp.pkt_type = cpu_to_le16(conn->pkt_type);
			hci_send_cmd(hdev, HCI_OP_CHANGE_CONN_PTYPE, sizeof(cp),
				     &cp);
		}
	}

	if (conn->type == ACL_LINK)
		hci_sco_setup(conn, ev->status);

done:
	if (status) {
		hci_conn_failed(conn, status);
	} else if (ev->link_type == SCO_LINK) {
		switch (conn->setting & SCO_AIRMODE_MASK) {
		case SCO_AIRMODE_CVSD:
			if (hdev->notify)
				hdev->notify(hdev, HCI_NOTIFY_ENABLE_SCO_CVSD);
			break;
		}

		hci_connect_cfm(conn, status);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_reject_conn(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct hci_cp_reject_conn_req cp;

	bacpy(&cp.bdaddr, bdaddr);
	cp.reason = HCI_ERROR_REJ_BAD_ADDR;
	hci_send_cmd(hdev, HCI_OP_REJECT_CONN_REQ, sizeof(cp), &cp);
}

static void hci_conn_request_evt(struct hci_dev *hdev, void *data,
				 struct sk_buff *skb)
{
	struct hci_ev_conn_request *ev = data;
	int mask = hdev->link_mode;
	struct inquiry_entry *ie;
	struct hci_conn *conn;
	__u8 flags = 0;

	bt_dev_dbg(hdev, "bdaddr %pMR type 0x%x", &ev->bdaddr, ev->link_type);

	/* Reject incoming connection from device with same BD ADDR against
	 * CVE-2020-26555
	 */
	if (hdev && !bacmp(&hdev->bdaddr, &ev->bdaddr)) {
		bt_dev_dbg(hdev, "Reject connection with same BD_ADDR %pMR\n",
			   &ev->bdaddr);
		hci_reject_conn(hdev, &ev->bdaddr);
		return;
	}

	mask |= hci_proto_connect_ind(hdev, &ev->bdaddr, ev->link_type,
				      &flags);

	if (!(mask & HCI_LM_ACCEPT)) {
		hci_reject_conn(hdev, &ev->bdaddr);
		return;
	}

	hci_dev_lock(hdev);

	if (hci_bdaddr_list_lookup(&hdev->reject_list, &ev->bdaddr,
				   BDADDR_BREDR)) {
		hci_reject_conn(hdev, &ev->bdaddr);
		goto unlock;
	}

	/* Require HCI_CONNECTABLE or an accept list entry to accept the
	 * connection. These features are only touched through mgmt so
	 * only do the checks if HCI_MGMT is set.
	 */
	if (hci_dev_test_flag(hdev, HCI_MGMT) &&
	    !hci_dev_test_flag(hdev, HCI_CONNECTABLE) &&
	    !hci_bdaddr_list_lookup_with_flags(&hdev->accept_list, &ev->bdaddr,
					       BDADDR_BREDR)) {
		hci_reject_conn(hdev, &ev->bdaddr);
		goto unlock;
	}

	/* Connection accepted */

	ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
	if (ie)
		memcpy(ie->data.dev_class, ev->dev_class, 3);

	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type,
			&ev->bdaddr);
	if (!conn) {
		conn = hci_conn_add_unset(hdev, ev->link_type, &ev->bdaddr,
					  HCI_ROLE_SLAVE);
		if (IS_ERR(conn)) {
			bt_dev_err(hdev, "connection err: %ld", PTR_ERR(conn));
			goto unlock;
		}
	}

	memcpy(conn->dev_class, ev->dev_class, 3);

	hci_dev_unlock(hdev);

	if (ev->link_type == ACL_LINK ||
	    (!(flags & HCI_PROTO_DEFER) && !lmp_esco_capable(hdev))) {
		struct hci_cp_accept_conn_req cp;
		conn->state = BT_CONNECT;

		bacpy(&cp.bdaddr, &ev->bdaddr);

		if (lmp_rswitch_capable(hdev) && (mask & HCI_LM_MASTER))
			cp.role = 0x00; /* Become central */
		else
			cp.role = 0x01; /* Remain peripheral */

		hci_send_cmd(hdev, HCI_OP_ACCEPT_CONN_REQ, sizeof(cp), &cp);
	} else if (!(flags & HCI_PROTO_DEFER)) {
		struct hci_cp_accept_sync_conn_req cp;
		conn->state = BT_CONNECT;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		cp.pkt_type = cpu_to_le16(conn->pkt_type);

		cp.tx_bandwidth   = cpu_to_le32(0x00001f40);
		cp.rx_bandwidth   = cpu_to_le32(0x00001f40);
		cp.max_latency    = cpu_to_le16(0xffff);
		cp.content_format = cpu_to_le16(hdev->voice_setting);
		cp.retrans_effort = 0xff;

		hci_send_cmd(hdev, HCI_OP_ACCEPT_SYNC_CONN_REQ, sizeof(cp),
			     &cp);
	} else {
		conn->state = BT_CONNECT2;
		hci_connect_cfm(conn, 0);
	}

	return;
unlock:
	hci_dev_unlock(hdev);
}

static u8 hci_to_mgmt_reason(u8 err)
{
	switch (err) {
	case HCI_ERROR_CONNECTION_TIMEOUT:
		return MGMT_DEV_DISCONN_TIMEOUT;
	case HCI_ERROR_REMOTE_USER_TERM:
	case HCI_ERROR_REMOTE_LOW_RESOURCES:
	case HCI_ERROR_REMOTE_POWER_OFF:
		return MGMT_DEV_DISCONN_REMOTE;
	case HCI_ERROR_LOCAL_HOST_TERM:
		return MGMT_DEV_DISCONN_LOCAL_HOST;
	default:
		return MGMT_DEV_DISCONN_UNKNOWN;
	}
}

static void hci_disconn_complete_evt(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_ev_disconn_complete *ev = data;
	u8 reason;
	struct hci_conn_params *params;
	struct hci_conn *conn;
	bool mgmt_connected;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (ev->status) {
		mgmt_disconnect_failed(hdev, &conn->dst, conn->type,
				       conn->dst_type, ev->status);
		goto unlock;
	}

	conn->state = BT_CLOSED;

	mgmt_connected = test_and_clear_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags);

	if (test_bit(HCI_CONN_AUTH_FAILURE, &conn->flags))
		reason = MGMT_DEV_DISCONN_AUTH_FAILURE;
	else
		reason = hci_to_mgmt_reason(ev->reason);

	mgmt_device_disconnected(hdev, &conn->dst, conn->type, conn->dst_type,
				reason, mgmt_connected);

	if (conn->type == ACL_LINK) {
		if (test_and_clear_bit(HCI_CONN_FLUSH_KEY, &conn->flags))
			hci_remove_link_key(hdev, &conn->dst);

		hci_update_scan(hdev);
	}

	/* Re-enable passive scanning if disconnected device is marked
	 * as auto-connectable.
	 */
	if (conn->type == LE_LINK) {
		params = hci_conn_params_lookup(hdev, &conn->dst,
						conn->dst_type);
		if (params) {
			switch (params->auto_connect) {
			case HCI_AUTO_CONN_LINK_LOSS:
				if (ev->reason != HCI_ERROR_CONNECTION_TIMEOUT)
					break;
				fallthrough;

			case HCI_AUTO_CONN_DIRECT:
			case HCI_AUTO_CONN_ALWAYS:
				hci_pend_le_list_del_init(params);
				hci_pend_le_list_add(params,
						     &hdev->pend_le_conns);
				hci_update_passive_scan(hdev);
				break;

			default:
				break;
			}
		}
	}

	hci_disconn_cfm(conn, ev->reason);

	/* Re-enable advertising if necessary, since it might
	 * have been disabled by the connection. From the
	 * HCI_LE_Set_Advertise_Enable command description in
	 * the core specification (v4.0):
	 * "The Controller shall continue advertising until the Host
	 * issues an LE_Set_Advertise_Enable command with
	 * Advertising_Enable set to 0x00 (Advertising is disabled)
	 * or until a connection is created or until the Advertising
	 * is timed out due to Directed Advertising."
	 */
	if (conn->type == LE_LINK && conn->role == HCI_ROLE_SLAVE) {
		hdev->cur_adv_instance = conn->adv_instance;
		hci_enable_advertising(hdev);
	}

	hci_conn_del(conn);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_auth_complete_evt(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_auth_complete *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (!ev->status) {
		clear_bit(HCI_CONN_AUTH_FAILURE, &conn->flags);
		set_bit(HCI_CONN_AUTH, &conn->flags);
		conn->sec_level = conn->pending_sec_level;
	} else {
		if (ev->status == HCI_ERROR_PIN_OR_KEY_MISSING)
			set_bit(HCI_CONN_AUTH_FAILURE, &conn->flags);

		mgmt_auth_failed(conn, ev->status);
	}

	clear_bit(HCI_CONN_AUTH_PEND, &conn->flags);

	if (conn->state == BT_CONFIG) {
		if (!ev->status && hci_conn_ssp_enabled(conn)) {
			struct hci_cp_set_conn_encrypt cp;
			cp.handle  = ev->handle;
			cp.encrypt = 0x01;
			hci_send_cmd(hdev, HCI_OP_SET_CONN_ENCRYPT, sizeof(cp),
				     &cp);
		} else {
			conn->state = BT_CONNECTED;
			hci_connect_cfm(conn, ev->status);
			hci_conn_drop(conn);
		}
	} else {
		hci_auth_cfm(conn, ev->status);

		hci_conn_hold(conn);
		conn->disc_timeout = HCI_DISCONN_TIMEOUT;
		hci_conn_drop(conn);
	}

	if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags)) {
		if (!ev->status) {
			struct hci_cp_set_conn_encrypt cp;
			cp.handle  = ev->handle;
			cp.encrypt = 0x01;
			hci_send_cmd(hdev, HCI_OP_SET_CONN_ENCRYPT, sizeof(cp),
				     &cp);
		} else {
			clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags);
			hci_encrypt_cfm(conn, ev->status);
		}
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_remote_name_evt(struct hci_dev *hdev, void *data,
				struct sk_buff *skb)
{
	struct hci_ev_remote_name *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		goto check_auth;

	if (ev->status == 0)
		hci_check_pending_name(hdev, conn, &ev->bdaddr, ev->name,
				       strnlen(ev->name, HCI_MAX_NAME_LENGTH));
	else
		hci_check_pending_name(hdev, conn, &ev->bdaddr, NULL, 0);

check_auth:
	if (!conn)
		goto unlock;

	if (!hci_outgoing_auth_needed(hdev, conn))
		goto unlock;

	if (!test_and_set_bit(HCI_CONN_AUTH_PEND, &conn->flags)) {
		struct hci_cp_auth_requested cp;

		set_bit(HCI_CONN_AUTH_INITIATOR, &conn->flags);

		cp.handle = __cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_AUTH_REQUESTED, sizeof(cp), &cp);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_encrypt_change_evt(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_encrypt_change *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (!ev->status) {
		if (ev->encrypt) {
			/* Encryption implies authentication */
			set_bit(HCI_CONN_AUTH, &conn->flags);
			set_bit(HCI_CONN_ENCRYPT, &conn->flags);
			conn->sec_level = conn->pending_sec_level;

			/* P-256 authentication key implies FIPS */
			if (conn->key_type == HCI_LK_AUTH_COMBINATION_P256)
				set_bit(HCI_CONN_FIPS, &conn->flags);

			if ((conn->type == ACL_LINK && ev->encrypt == 0x02) ||
			    conn->type == LE_LINK)
				set_bit(HCI_CONN_AES_CCM, &conn->flags);
		} else {
			clear_bit(HCI_CONN_ENCRYPT, &conn->flags);
			clear_bit(HCI_CONN_AES_CCM, &conn->flags);
		}
	}

	/* We should disregard the current RPA and generate a new one
	 * whenever the encryption procedure fails.
	 */
	if (ev->status && conn->type == LE_LINK) {
		hci_dev_set_flag(hdev, HCI_RPA_EXPIRED);
		hci_adv_instances_set_rpa_expired(hdev, true);
	}

	clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags);

	/* Check link security requirements are met */
	if (!hci_conn_check_link_mode(conn))
		ev->status = HCI_ERROR_AUTH_FAILURE;

	if (ev->status && conn->state == BT_CONNECTED) {
		if (ev->status == HCI_ERROR_PIN_OR_KEY_MISSING)
			set_bit(HCI_CONN_AUTH_FAILURE, &conn->flags);

		/* Notify upper layers so they can cleanup before
		 * disconnecting.
		 */
		hci_encrypt_cfm(conn, ev->status);
		hci_disconnect(conn, HCI_ERROR_AUTH_FAILURE);
		hci_conn_drop(conn);
		goto unlock;
	}

	/* Try reading the encryption key size for encrypted ACL links */
	if (!ev->status && ev->encrypt && conn->type == ACL_LINK) {
		if (hci_read_enc_key_size(hdev, conn))
			goto notify;

		goto unlock;
	}

	/* We skip the WRITE_AUTH_PAYLOAD_TIMEOUT for ATS2851 based controllers
	 * to avoid unexpected SMP command errors when pairing.
	 */
	if (hci_test_quirk(hdev, HCI_QUIRK_BROKEN_WRITE_AUTH_PAYLOAD_TIMEOUT))
		goto notify;

	/* Set the default Authenticated Payload Timeout after
	 * an LE Link is established. As per Core Spec v5.0, Vol 2, Part B
	 * Section 3.3, the HCI command WRITE_AUTH_PAYLOAD_TIMEOUT should be
	 * sent when the link is active and Encryption is enabled, the conn
	 * type can be either LE or ACL and controller must support LMP Ping.
	 * Ensure for AES-CCM encryption as well.
	 */
	if (test_bit(HCI_CONN_ENCRYPT, &conn->flags) &&
	    test_bit(HCI_CONN_AES_CCM, &conn->flags) &&
	    ((conn->type == ACL_LINK && lmp_ping_capable(hdev)) ||
	     (conn->type == LE_LINK && (hdev->le_features[0] & HCI_LE_PING)))) {
		struct hci_cp_write_auth_payload_to cp;

		cp.handle = cpu_to_le16(conn->handle);
		cp.timeout = cpu_to_le16(hdev->auth_payload_timeout);
		if (hci_send_cmd(conn->hdev, HCI_OP_WRITE_AUTH_PAYLOAD_TO,
				 sizeof(cp), &cp))
			bt_dev_err(hdev, "write auth payload timeout failed");
	}

notify:
	hci_encrypt_cfm(conn, ev->status);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_change_link_key_complete_evt(struct hci_dev *hdev, void *data,
					     struct sk_buff *skb)
{
	struct hci_ev_change_link_key_complete *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status)
			set_bit(HCI_CONN_SECURE, &conn->flags);

		clear_bit(HCI_CONN_AUTH_PEND, &conn->flags);

		hci_key_change_cfm(conn, ev->status);
	}

	hci_dev_unlock(hdev);
}

static void hci_remote_features_evt(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_remote_features *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (!ev->status)
		memcpy(conn->features[0], ev->features, 8);

	if (conn->state != BT_CONFIG)
		goto unlock;

	if (!ev->status && lmp_ext_feat_capable(hdev) &&
	    lmp_ext_feat_capable(conn)) {
		struct hci_cp_read_remote_ext_features cp;
		cp.handle = ev->handle;
		cp.page = 0x01;
		hci_send_cmd(hdev, HCI_OP_READ_REMOTE_EXT_FEATURES,
			     sizeof(cp), &cp);
		goto unlock;
	}

	if (!ev->status) {
		struct hci_cp_remote_name_req cp;
		memset(&cp, 0, sizeof(cp));
		bacpy(&cp.bdaddr, &conn->dst);
		cp.pscan_rep_mode = 0x02;
		hci_send_cmd(hdev, HCI_OP_REMOTE_NAME_REQ, sizeof(cp), &cp);
	} else {
		mgmt_device_connected(hdev, conn, NULL, 0);
	}

	if (!hci_outgoing_auth_needed(hdev, conn)) {
		conn->state = BT_CONNECTED;
		hci_connect_cfm(conn, ev->status);
		hci_conn_drop(conn);
	}

unlock:
	hci_dev_unlock(hdev);
}

static inline void handle_cmd_cnt_and_timer(struct hci_dev *hdev, u8 ncmd)
{
	cancel_delayed_work(&hdev->cmd_timer);

	rcu_read_lock();
	if (!test_bit(HCI_RESET, &hdev->flags)) {
		if (ncmd) {
			cancel_delayed_work(&hdev->ncmd_timer);
			atomic_set(&hdev->cmd_cnt, 1);
		} else {
			if (!hci_dev_test_flag(hdev, HCI_CMD_DRAIN_WORKQUEUE))
				queue_delayed_work(hdev->workqueue, &hdev->ncmd_timer,
						   HCI_NCMD_TIMEOUT);
		}
	}
	rcu_read_unlock();
}

static u8 hci_cc_le_read_buffer_size_v2(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_rp_le_read_buffer_size_v2 *rp = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	hdev->le_mtu   = __le16_to_cpu(rp->acl_mtu);
	hdev->le_pkts  = rp->acl_max_pkt;
	hdev->iso_mtu  = __le16_to_cpu(rp->iso_mtu);
	hdev->iso_pkts = rp->iso_max_pkt;

	hdev->le_cnt  = hdev->le_pkts;
	hdev->iso_cnt = hdev->iso_pkts;

	BT_DBG("%s acl mtu %d:%d iso mtu %d:%d", hdev->name, hdev->acl_mtu,
	       hdev->acl_pkts, hdev->iso_mtu, hdev->iso_pkts);

	if (hdev->le_mtu && hdev->le_mtu < HCI_MIN_LE_MTU)
		return HCI_ERROR_INVALID_PARAMETERS;

	return rp->status;
}

static void hci_unbound_cis_failed(struct hci_dev *hdev, u8 cig, u8 status)
{
	struct hci_conn *conn, *tmp;

	lockdep_assert_held(&hdev->lock);

	list_for_each_entry_safe(conn, tmp, &hdev->conn_hash.list, list) {
		if (conn->type != CIS_LINK ||
		    conn->state == BT_OPEN || conn->iso_qos.ucast.cig != cig)
			continue;

		if (HCI_CONN_HANDLE_UNSET(conn->handle))
			hci_conn_failed(conn, status);
	}
}

static u8 hci_cc_le_set_cig_params(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_rp_le_set_cig_params *rp = data;
	struct hci_cp_le_set_cig_params *cp;
	struct hci_conn *conn;
	u8 status = rp->status;
	bool pending = false;
	int i;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_CIG_PARAMS);
	if (!rp->status && (!cp || rp->num_handles != cp->num_cis ||
			    rp->cig_id != cp->cig_id)) {
		bt_dev_err(hdev, "unexpected Set CIG Parameters response data");
		status = HCI_ERROR_UNSPECIFIED;
	}

	hci_dev_lock(hdev);

	/* BLUETOOTH CORE SPECIFICATION Version 5.4 | Vol 4, Part E page 2554
	 *
	 * If the Status return parameter is non-zero, then the state of the CIG
	 * and its CIS configurations shall not be changed by the command. If
	 * the CIG did not already exist, it shall not be created.
	 */
	if (status) {
		/* Keep current configuration, fail only the unbound CIS */
		hci_unbound_cis_failed(hdev, rp->cig_id, status);
		goto unlock;
	}

	/* BLUETOOTH CORE SPECIFICATION Version 5.3 | Vol 4, Part E page 2553
	 *
	 * If the Status return parameter is zero, then the Controller shall
	 * set the Connection_Handle arrayed return parameter to the connection
	 * handle(s) corresponding to the CIS configurations specified in
	 * the CIS_IDs command parameter, in the same order.
	 */
	for (i = 0; i < rp->num_handles; ++i) {
		conn = hci_conn_hash_lookup_cis(hdev, NULL, 0, rp->cig_id,
						cp->cis[i].cis_id);
		if (!conn || !bacmp(&conn->dst, BDADDR_ANY))
			continue;

		if (conn->state != BT_BOUND && conn->state != BT_CONNECT)
			continue;

		if (hci_conn_set_handle(conn, __le16_to_cpu(rp->handle[i])))
			continue;

		if (conn->state == BT_CONNECT)
			pending = true;
	}

unlock:
	if (pending)
		hci_le_create_cis_pending(hdev);

	hci_dev_unlock(hdev);

	return rp->status;
}

static u8 hci_cc_le_setup_iso_path(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_rp_le_setup_iso_path *rp = data;
	struct hci_cp_le_setup_iso_path *cp;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SETUP_ISO_PATH);
	if (!cp)
		return rp->status;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (!conn)
		goto unlock;

	if (rp->status) {
		hci_connect_cfm(conn, rp->status);
		hci_conn_del(conn);
		goto unlock;
	}

	switch (cp->direction) {
	/* Input (Host to Controller) */
	case 0x00:
		/* Only confirm connection if output only */
		if (conn->iso_qos.ucast.out.sdu && !conn->iso_qos.ucast.in.sdu)
			hci_connect_cfm(conn, rp->status);
		break;
	/* Output (Controller to Host) */
	case 0x01:
		/* Confirm connection since conn->iso_qos is always configured
		 * last.
		 */
		hci_connect_cfm(conn, rp->status);

		/* Notify device connected in case it is a BIG Sync */
		if (!rp->status && test_bit(HCI_CONN_BIG_SYNC, &conn->flags))
			mgmt_device_connected(hdev, conn, NULL, 0);

		break;
	}

unlock:
	hci_dev_unlock(hdev);
	return rp->status;
}

static void hci_cs_le_create_big(struct hci_dev *hdev, u8 status)
{
	bt_dev_dbg(hdev, "status 0x%2.2x", status);
}

static u8 hci_cc_set_per_adv_param(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_le_set_per_adv_params *cp;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_PER_ADV_PARAMS);
	if (!cp)
		return rp->status;

	/* TODO: set the conn state */
	return rp->status;
}

static u8 hci_cc_le_set_per_adv_enable(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_ev_status *rp = data;
	struct hci_cp_le_set_per_adv_enable *cp;
	struct adv_info *adv = NULL, *n;
	u8 per_adv_cnt = 0;

	bt_dev_dbg(hdev, "status 0x%2.2x", rp->status);

	if (rp->status)
		return rp->status;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_PER_ADV_ENABLE);
	if (!cp)
		return rp->status;

	hci_dev_lock(hdev);

	adv = hci_find_adv_instance(hdev, cp->handle);

	if (cp->enable) {
		hci_dev_set_flag(hdev, HCI_LE_PER_ADV);

		if (adv)
			adv->periodic_enabled = true;
	} else {
		if (adv)
			adv->periodic_enabled = false;

		/* If just one instance was disabled check if there are
		 * any other instance enabled before clearing HCI_LE_PER_ADV.
		 * The current periodic adv instance will be marked as
		 * disabled once extended advertising is also disabled.
		 */
		list_for_each_entry_safe(adv, n, &hdev->adv_instances,
					 list) {
			if (adv->periodic && adv->enabled)
				per_adv_cnt++;
		}

		if (per_adv_cnt > 1)
			goto unlock;

		hci_dev_clear_flag(hdev, HCI_LE_PER_ADV);
	}

unlock:
	hci_dev_unlock(hdev);

	return rp->status;
}

#define HCI_CC_VL(_op, _func, _min, _max) \
{ \
	.op = _op, \
	.func = _func, \
	.min_len = _min, \
	.max_len = _max, \
}

#define HCI_CC(_op, _func, _len) \
	HCI_CC_VL(_op, _func, _len, _len)

#define HCI_CC_STATUS(_op, _func) \
	HCI_CC(_op, _func, sizeof(struct hci_ev_status))

static const struct hci_cc {
	u16  op;
	u8 (*func)(struct hci_dev *hdev, void *data, struct sk_buff *skb);
	u16  min_len;
	u16  max_len;
} hci_cc_table[] = {
	HCI_CC_STATUS(HCI_OP_INQUIRY_CANCEL, hci_cc_inquiry_cancel),
	HCI_CC_STATUS(HCI_OP_PERIODIC_INQ, hci_cc_periodic_inq),
	HCI_CC_STATUS(HCI_OP_EXIT_PERIODIC_INQ, hci_cc_exit_periodic_inq),
	HCI_CC(HCI_OP_REMOTE_NAME_REQ_CANCEL, hci_cc_remote_name_req_cancel,
	       sizeof(struct hci_rp_remote_name_req_cancel)),
	HCI_CC(HCI_OP_ROLE_DISCOVERY, hci_cc_role_discovery,
	       sizeof(struct hci_rp_role_discovery)),
	HCI_CC(HCI_OP_READ_LINK_POLICY, hci_cc_read_link_policy,
	       sizeof(struct hci_rp_read_link_policy)),
	HCI_CC(HCI_OP_WRITE_LINK_POLICY, hci_cc_write_link_policy,
	       sizeof(struct hci_rp_write_link_policy)),
	HCI_CC(HCI_OP_READ_DEF_LINK_POLICY, hci_cc_read_def_link_policy,
	       sizeof(struct hci_rp_read_def_link_policy)),
	HCI_CC_STATUS(HCI_OP_WRITE_DEF_LINK_POLICY,
		      hci_cc_write_def_link_policy),
	HCI_CC_STATUS(HCI_OP_RESET, hci_cc_reset),
	HCI_CC(HCI_OP_READ_STORED_LINK_KEY, hci_cc_read_stored_link_key,
	       sizeof(struct hci_rp_read_stored_link_key)),
	HCI_CC(HCI_OP_DELETE_STORED_LINK_KEY, hci_cc_delete_stored_link_key,
	       sizeof(struct hci_rp_delete_stored_link_key)),
	HCI_CC_STATUS(HCI_OP_WRITE_LOCAL_NAME, hci_cc_write_local_name),
	HCI_CC(HCI_OP_READ_LOCAL_NAME, hci_cc_read_local_name,
	       sizeof(struct hci_rp_read_local_name)),
	HCI_CC_STATUS(HCI_OP_WRITE_AUTH_ENABLE, hci_cc_write_auth_enable),
	HCI_CC_STATUS(HCI_OP_WRITE_ENCRYPT_MODE, hci_cc_write_encrypt_mode),
	HCI_CC_STATUS(HCI_OP_WRITE_SCAN_ENABLE, hci_cc_write_scan_enable),
	HCI_CC_STATUS(HCI_OP_SET_EVENT_FLT, hci_cc_set_event_filter),
	HCI_CC(HCI_OP_READ_CLASS_OF_DEV, hci_cc_read_class_of_dev,
	       sizeof(struct hci_rp_read_class_of_dev)),
	HCI_CC_STATUS(HCI_OP_WRITE_CLASS_OF_DEV, hci_cc_write_class_of_dev),
	HCI_CC(HCI_OP_READ_VOICE_SETTING, hci_cc_read_voice_setting,
	       sizeof(struct hci_rp_read_voice_setting)),
	HCI_CC_STATUS(HCI_OP_WRITE_VOICE_SETTING, hci_cc_write_voice_setting),
	HCI_CC(HCI_OP_READ_NUM_SUPPORTED_IAC, hci_cc_read_num_supported_iac,
	       sizeof(struct hci_rp_read_num_supported_iac)),
	HCI_CC_STATUS(HCI_OP_WRITE_SSP_MODE, hci_cc_write_ssp_mode),
	HCI_CC_STATUS(HCI_OP_WRITE_SC_SUPPORT, hci_cc_write_sc_support),
	HCI_CC(HCI_OP_READ_AUTH_PAYLOAD_TO, hci_cc_read_auth_payload_timeout,
	       sizeof(struct hci_rp_read_auth_payload_to)),
	HCI_CC(HCI_OP_WRITE_AUTH_PAYLOAD_TO, hci_cc_write_auth_payload_timeout,
	       sizeof(struct hci_rp_write_auth_payload_to)),
	HCI_CC(HCI_OP_READ_LOCAL_VERSION, hci_cc_read_local_version,
	       sizeof(struct hci_rp_read_local_version)),
	HCI_CC(HCI_OP_READ_LOCAL_COMMANDS, hci_cc_read_local_commands,
	       sizeof(struct hci_rp_read_local_commands)),
	HCI_CC(HCI_OP_READ_LOCAL_FEATURES, hci_cc_read_local_features,
	       sizeof(struct hci_rp_read_local_features)),
	HCI_CC(HCI_OP_READ_LOCAL_EXT_FEATURES, hci_cc_read_local_ext_features,
	       sizeof(struct hci_rp_read_local_ext_features)),
	HCI_CC(HCI_OP_READ_BUFFER_SIZE, hci_cc_read_buffer_size,
	       sizeof(struct hci_rp_read_buffer_size)),
	HCI_CC(HCI_OP_READ_BD_ADDR, hci_cc_read_bd_addr,
	       sizeof(struct hci_rp_read_bd_addr)),
	HCI_CC(HCI_OP_READ_LOCAL_PAIRING_OPTS, hci_cc_read_local_pairing_opts,
	       sizeof(struct hci_rp_read_local_pairing_opts)),
	HCI_CC(HCI_OP_READ_PAGE_SCAN_ACTIVITY, hci_cc_read_page_scan_activity,
	       sizeof(struct hci_rp_read_page_scan_activity)),
	HCI_CC_STATUS(HCI_OP_WRITE_PAGE_SCAN_ACTIVITY,
		      hci_cc_write_page_scan_activity),
	HCI_CC(HCI_OP_READ_PAGE_SCAN_TYPE, hci_cc_read_page_scan_type,
	       sizeof(struct hci_rp_read_page_scan_type)),
	HCI_CC_STATUS(HCI_OP_WRITE_PAGE_SCAN_TYPE, hci_cc_write_page_scan_type),
	HCI_CC(HCI_OP_READ_CLOCK, hci_cc_read_clock,
	       sizeof(struct hci_rp_read_clock)),
	HCI_CC(HCI_OP_READ_ENC_KEY_SIZE, hci_cc_read_enc_key_size,
	       sizeof(struct hci_rp_read_enc_key_size)),
	HCI_CC(HCI_OP_READ_INQ_RSP_TX_POWER, hci_cc_read_inq_rsp_tx_power,
	       sizeof(struct hci_rp_read_inq_rsp_tx_power)),
	HCI_CC(HCI_OP_READ_DEF_ERR_DATA_REPORTING,
	       hci_cc_read_def_err_data_reporting,
	       sizeof(struct hci_rp_read_def_err_data_reporting)),
	HCI_CC_STATUS(HCI_OP_WRITE_DEF_ERR_DATA_REPORTING,
		      hci_cc_write_def_err_data_reporting),
	HCI_CC(HCI_OP_PIN_CODE_REPLY, hci_cc_pin_code_reply,
	       sizeof(struct hci_rp_pin_code_reply)),
	HCI_CC(HCI_OP_PIN_CODE_NEG_REPLY, hci_cc_pin_code_neg_reply,
	       sizeof(struct hci_rp_pin_code_neg_reply)),
	HCI_CC(HCI_OP_READ_LOCAL_OOB_DATA, hci_cc_read_local_oob_data,
	       sizeof(struct hci_rp_read_local_oob_data)),
	HCI_CC(HCI_OP_READ_LOCAL_OOB_EXT_DATA, hci_cc_read_local_oob_ext_data,
	       sizeof(struct hci_rp_read_local_oob_ext_data)),
	HCI_CC(HCI_OP_LE_READ_BUFFER_SIZE, hci_cc_le_read_buffer_size,
	       sizeof(struct hci_rp_le_read_buffer_size)),
	HCI_CC(HCI_OP_LE_READ_LOCAL_FEATURES, hci_cc_le_read_local_features,
	       sizeof(struct hci_rp_le_read_local_features)),
	HCI_CC(HCI_OP_LE_READ_ADV_TX_POWER, hci_cc_le_read_adv_tx_power,
	       sizeof(struct hci_rp_le_read_adv_tx_power)),
	HCI_CC(HCI_OP_USER_CONFIRM_REPLY, hci_cc_user_confirm_reply,
	       sizeof(struct hci_rp_user_confirm_reply)),
	HCI_CC(HCI_OP_USER_CONFIRM_NEG_REPLY, hci_cc_user_confirm_neg_reply,
	       sizeof(struct hci_rp_user_confirm_reply)),
	HCI_CC(HCI_OP_USER_PASSKEY_REPLY, hci_cc_user_passkey_reply,
	       sizeof(struct hci_rp_user_confirm_reply)),
	HCI_CC(HCI_OP_USER_PASSKEY_NEG_REPLY, hci_cc_user_passkey_neg_reply,
	       sizeof(struct hci_rp_user_confirm_reply)),
	HCI_CC_STATUS(HCI_OP_LE_SET_RANDOM_ADDR, hci_cc_le_set_random_addr),
	HCI_CC_STATUS(HCI_OP_LE_SET_ADV_ENABLE, hci_cc_le_set_adv_enable),
	HCI_CC_STATUS(HCI_OP_LE_SET_SCAN_PARAM, hci_cc_le_set_scan_param),
	HCI_CC_STATUS(HCI_OP_LE_SET_SCAN_ENABLE, hci_cc_le_set_scan_enable),
	HCI_CC(HCI_OP_LE_READ_ACCEPT_LIST_SIZE,
	       hci_cc_le_read_accept_list_size,
	       sizeof(struct hci_rp_le_read_accept_list_size)),
	HCI_CC_STATUS(HCI_OP_LE_CLEAR_ACCEPT_LIST, hci_cc_le_clear_accept_list),
	HCI_CC_STATUS(HCI_OP_LE_ADD_TO_ACCEPT_LIST,
		      hci_cc_le_add_to_accept_list),
	HCI_CC_STATUS(HCI_OP_LE_DEL_FROM_ACCEPT_LIST,
		      hci_cc_le_del_from_accept_list),
	HCI_CC(HCI_OP_LE_READ_SUPPORTED_STATES, hci_cc_le_read_supported_states,
	       sizeof(struct hci_rp_le_read_supported_states)),
	HCI_CC(HCI_OP_LE_READ_DEF_DATA_LEN, hci_cc_le_read_def_data_len,
	       sizeof(struct hci_rp_le_read_def_data_len)),
	HCI_CC_STATUS(HCI_OP_LE_WRITE_DEF_DATA_LEN,
		      hci_cc_le_write_def_data_len),
	HCI_CC_STATUS(HCI_OP_LE_ADD_TO_RESOLV_LIST,
		      hci_cc_le_add_to_resolv_list),
	HCI_CC_STATUS(HCI_OP_LE_DEL_FROM_RESOLV_LIST,
		      hci_cc_le_del_from_resolv_list),
	HCI_CC_STATUS(HCI_OP_LE_CLEAR_RESOLV_LIST,
		      hci_cc_le_clear_resolv_list),
	HCI_CC(HCI_OP_LE_READ_RESOLV_LIST_SIZE, hci_cc_le_read_resolv_list_size,
	       sizeof(struct hci_rp_le_read_resolv_list_size)),
	HCI_CC_STATUS(HCI_OP_LE_SET_ADDR_RESOLV_ENABLE,
		      hci_cc_le_set_addr_resolution_enable),
	HCI_CC(HCI_OP_LE_READ_MAX_DATA_LEN, hci_cc_le_read_max_data_len,
	       sizeof(struct hci_rp_le_read_max_data_len)),
	HCI_CC_STATUS(HCI_OP_WRITE_LE_HOST_SUPPORTED,
		      hci_cc_write_le_host_supported),
	HCI_CC_STATUS(HCI_OP_LE_SET_ADV_PARAM, hci_cc_set_adv_param),
	HCI_CC(HCI_OP_READ_RSSI, hci_cc_read_rssi,
	       sizeof(struct hci_rp_read_rssi)),
	HCI_CC(HCI_OP_READ_TX_POWER, hci_cc_read_tx_power,
	       sizeof(struct hci_rp_read_tx_power)),
	HCI_CC_STATUS(HCI_OP_WRITE_SSP_DEBUG_MODE, hci_cc_write_ssp_debug_mode),
	HCI_CC_STATUS(HCI_OP_LE_SET_EXT_SCAN_PARAMS,
		      hci_cc_le_set_ext_scan_param),
	HCI_CC_STATUS(HCI_OP_LE_SET_EXT_SCAN_ENABLE,
		      hci_cc_le_set_ext_scan_enable),
	HCI_CC_STATUS(HCI_OP_LE_SET_DEFAULT_PHY, hci_cc_le_set_default_phy),
	HCI_CC(HCI_OP_LE_READ_NUM_SUPPORTED_ADV_SETS,
	       hci_cc_le_read_num_adv_sets,
	       sizeof(struct hci_rp_le_read_num_supported_adv_sets)),
	HCI_CC_STATUS(HCI_OP_LE_SET_EXT_ADV_ENABLE,
		      hci_cc_le_set_ext_adv_enable),
	HCI_CC_STATUS(HCI_OP_LE_SET_ADV_SET_RAND_ADDR,
		      hci_cc_le_set_adv_set_random_addr),
	HCI_CC_STATUS(HCI_OP_LE_REMOVE_ADV_SET, hci_cc_le_remove_adv_set),
	HCI_CC_STATUS(HCI_OP_LE_CLEAR_ADV_SETS, hci_cc_le_clear_adv_sets),
	HCI_CC_STATUS(HCI_OP_LE_SET_PER_ADV_PARAMS, hci_cc_set_per_adv_param),
	HCI_CC_STATUS(HCI_OP_LE_SET_PER_ADV_ENABLE,
		      hci_cc_le_set_per_adv_enable),
	HCI_CC(HCI_OP_LE_READ_TRANSMIT_POWER, hci_cc_le_read_transmit_power,
	       sizeof(struct hci_rp_le_read_transmit_power)),
	HCI_CC_STATUS(HCI_OP_LE_SET_PRIVACY_MODE, hci_cc_le_set_privacy_mode),
	HCI_CC(HCI_OP_LE_READ_BUFFER_SIZE_V2, hci_cc_le_read_buffer_size_v2,
	       sizeof(struct hci_rp_le_read_buffer_size_v2)),
	HCI_CC_VL(HCI_OP_LE_SET_CIG_PARAMS, hci_cc_le_set_cig_params,
		  sizeof(struct hci_rp_le_set_cig_params), HCI_MAX_EVENT_SIZE),
	HCI_CC(HCI_OP_LE_SETUP_ISO_PATH, hci_cc_le_setup_iso_path,
	       sizeof(struct hci_rp_le_setup_iso_path)),
};

static u8 hci_cc_func(struct hci_dev *hdev, const struct hci_cc *cc,
		      struct sk_buff *skb)
{
	void *data;

	if (skb->len < cc->min_len) {
		bt_dev_err(hdev, "unexpected cc 0x%4.4x length: %u < %u",
			   cc->op, skb->len, cc->min_len);
		return HCI_ERROR_UNSPECIFIED;
	}

	/* Just warn if the length is over max_len size it still be possible to
	 * partially parse the cc so leave to callback to decide if that is
	 * acceptable.
	 */
	if (skb->len > cc->max_len)
		bt_dev_warn(hdev, "unexpected cc 0x%4.4x length: %u > %u",
			    cc->op, skb->len, cc->max_len);

	data = hci_cc_skb_pull(hdev, skb, cc->op, cc->min_len);
	if (!data)
		return HCI_ERROR_UNSPECIFIED;

	return cc->func(hdev, data, skb);
}

static void hci_cmd_complete_evt(struct hci_dev *hdev, void *data,
				 struct sk_buff *skb, u16 *opcode, u8 *status,
				 hci_req_complete_t *req_complete,
				 hci_req_complete_skb_t *req_complete_skb)
{
	struct hci_ev_cmd_complete *ev = data;
	int i;

	*opcode = __le16_to_cpu(ev->opcode);

	bt_dev_dbg(hdev, "opcode 0x%4.4x", *opcode);

	for (i = 0; i < ARRAY_SIZE(hci_cc_table); i++) {
		if (hci_cc_table[i].op == *opcode) {
			*status = hci_cc_func(hdev, &hci_cc_table[i], skb);
			break;
		}
	}

	if (i == ARRAY_SIZE(hci_cc_table)) {
		if (!skb->len) {
			bt_dev_err(hdev, "Unexpected cc 0x%4.4x with no status",
				   *opcode);
			*status = HCI_ERROR_UNSPECIFIED;
			return;
		}

		/* Unknown opcode, assume byte 0 contains the status, so
		 * that e.g. __hci_cmd_sync() properly returns errors
		 * for vendor specific commands send by HCI drivers.
		 * If a vendor doesn't actually follow this convention we may
		 * need to introduce a vendor CC table in order to properly set
		 * the status.
		 */
		*status = skb->data[0];
	}

	handle_cmd_cnt_and_timer(hdev, ev->ncmd);

	hci_req_cmd_complete(hdev, *opcode, *status, req_complete,
			     req_complete_skb);

	if (hci_dev_test_flag(hdev, HCI_CMD_PENDING)) {
		bt_dev_err(hdev,
			   "unexpected event for opcode 0x%4.4x", *opcode);
		return;
	}

	if (atomic_read(&hdev->cmd_cnt) && !skb_queue_empty(&hdev->cmd_q))
		queue_work(hdev->workqueue, &hdev->cmd_work);
}

static void hci_cs_le_create_cis(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_le_create_cis *cp;
	bool pending = false;
	int i;

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_CREATE_CIS);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	/* Remove connection if command failed */
	for (i = 0; i < cp->num_cis; i++) {
		struct hci_conn *conn;
		u16 handle;

		handle = __le16_to_cpu(cp->cis[i].cis_handle);

		conn = hci_conn_hash_lookup_handle(hdev, handle);
		if (conn) {
			if (test_and_clear_bit(HCI_CONN_CREATE_CIS,
					       &conn->flags))
				pending = true;
			conn->state = BT_CLOSED;
			hci_connect_cfm(conn, status);
			hci_conn_del(conn);
		}
	}
	cp->num_cis = 0;

	if (pending)
		hci_le_create_cis_pending(hdev);

	hci_dev_unlock(hdev);
}

#define HCI_CS(_op, _func) \
{ \
	.op = _op, \
	.func = _func, \
}

static const struct hci_cs {
	u16  op;
	void (*func)(struct hci_dev *hdev, __u8 status);
} hci_cs_table[] = {
	HCI_CS(HCI_OP_INQUIRY, hci_cs_inquiry),
	HCI_CS(HCI_OP_CREATE_CONN, hci_cs_create_conn),
	HCI_CS(HCI_OP_DISCONNECT, hci_cs_disconnect),
	HCI_CS(HCI_OP_ADD_SCO, hci_cs_add_sco),
	HCI_CS(HCI_OP_AUTH_REQUESTED, hci_cs_auth_requested),
	HCI_CS(HCI_OP_SET_CONN_ENCRYPT, hci_cs_set_conn_encrypt),
	HCI_CS(HCI_OP_REMOTE_NAME_REQ, hci_cs_remote_name_req),
	HCI_CS(HCI_OP_READ_REMOTE_FEATURES, hci_cs_read_remote_features),
	HCI_CS(HCI_OP_READ_REMOTE_EXT_FEATURES,
	       hci_cs_read_remote_ext_features),
	HCI_CS(HCI_OP_SETUP_SYNC_CONN, hci_cs_setup_sync_conn),
	HCI_CS(HCI_OP_ENHANCED_SETUP_SYNC_CONN,
	       hci_cs_enhanced_setup_sync_conn),
	HCI_CS(HCI_OP_SNIFF_MODE, hci_cs_sniff_mode),
	HCI_CS(HCI_OP_EXIT_SNIFF_MODE, hci_cs_exit_sniff_mode),
	HCI_CS(HCI_OP_SWITCH_ROLE, hci_cs_switch_role),
	HCI_CS(HCI_OP_LE_CREATE_CONN, hci_cs_le_create_conn),
	HCI_CS(HCI_OP_LE_READ_REMOTE_FEATURES, hci_cs_le_read_remote_features),
	HCI_CS(HCI_OP_LE_START_ENC, hci_cs_le_start_enc),
	HCI_CS(HCI_OP_LE_EXT_CREATE_CONN, hci_cs_le_ext_create_conn),
	HCI_CS(HCI_OP_LE_CREATE_CIS, hci_cs_le_create_cis),
	HCI_CS(HCI_OP_LE_CREATE_BIG, hci_cs_le_create_big),
};

static void hci_cmd_status_evt(struct hci_dev *hdev, void *data,
			       struct sk_buff *skb, u16 *opcode, u8 *status,
			       hci_req_complete_t *req_complete,
			       hci_req_complete_skb_t *req_complete_skb)
{
	struct hci_ev_cmd_status *ev = data;
	int i;

	*opcode = __le16_to_cpu(ev->opcode);
	*status = ev->status;

	bt_dev_dbg(hdev, "opcode 0x%4.4x", *opcode);

	for (i = 0; i < ARRAY_SIZE(hci_cs_table); i++) {
		if (hci_cs_table[i].op == *opcode) {
			hci_cs_table[i].func(hdev, ev->status);
			break;
		}
	}

	handle_cmd_cnt_and_timer(hdev, ev->ncmd);

	/* Indicate request completion if the command failed. Also, if
	 * we're not waiting for a special event and we get a success
	 * command status we should try to flag the request as completed
	 * (since for this kind of commands there will not be a command
	 * complete event).
	 */
	if (ev->status || (hdev->req_skb && !hci_skb_event(hdev->req_skb))) {
		hci_req_cmd_complete(hdev, *opcode, ev->status, req_complete,
				     req_complete_skb);
		if (hci_dev_test_flag(hdev, HCI_CMD_PENDING)) {
			bt_dev_err(hdev, "unexpected event for opcode 0x%4.4x",
				   *opcode);
			return;
		}
	}

	if (atomic_read(&hdev->cmd_cnt) && !skb_queue_empty(&hdev->cmd_q))
		queue_work(hdev->workqueue, &hdev->cmd_work);
}

static void hci_hardware_error_evt(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_hardware_error *ev = data;

	bt_dev_dbg(hdev, "code 0x%2.2x", ev->code);

	hdev->hw_error_code = ev->code;

	queue_work(hdev->req_workqueue, &hdev->error_reset);
}

static void hci_role_change_evt(struct hci_dev *hdev, void *data,
				struct sk_buff *skb)
{
	struct hci_ev_role_change *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn) {
		if (!ev->status)
			conn->role = ev->role;

		clear_bit(HCI_CONN_RSWITCH_PEND, &conn->flags);

		hci_role_switch_cfm(conn, ev->status, ev->role);
	}

	hci_dev_unlock(hdev);
}

static void hci_num_comp_pkts_evt(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_num_comp_pkts *ev = data;
	int i;

	if (!hci_ev_skb_pull(hdev, skb, HCI_EV_NUM_COMP_PKTS,
			     flex_array_size(ev, handles, ev->num)))
		return;

	bt_dev_dbg(hdev, "num %d", ev->num);

	hci_dev_lock(hdev);

	for (i = 0; i < ev->num; i++) {
		struct hci_comp_pkts_info *info = &ev->handles[i];
		struct hci_conn *conn;
		__u16  handle, count;
		unsigned int i;

		handle = __le16_to_cpu(info->handle);
		count  = __le16_to_cpu(info->count);

		conn = hci_conn_hash_lookup_handle(hdev, handle);
		if (!conn)
			continue;

		/* Check if there is really enough packets outstanding before
		 * attempting to decrease the sent counter otherwise it could
		 * underflow..
		 */
		if (conn->sent >= count) {
			conn->sent -= count;
		} else {
			bt_dev_warn(hdev, "hcon %p sent %u < count %u",
				    conn, conn->sent, count);
			conn->sent = 0;
		}

		for (i = 0; i < count; ++i)
			hci_conn_tx_dequeue(conn);

		switch (conn->type) {
		case ACL_LINK:
			hdev->acl_cnt += count;
			if (hdev->acl_cnt > hdev->acl_pkts)
				hdev->acl_cnt = hdev->acl_pkts;
			break;

		case LE_LINK:
			if (hdev->le_pkts) {
				hdev->le_cnt += count;
				if (hdev->le_cnt > hdev->le_pkts)
					hdev->le_cnt = hdev->le_pkts;
			} else {
				hdev->acl_cnt += count;
				if (hdev->acl_cnt > hdev->acl_pkts)
					hdev->acl_cnt = hdev->acl_pkts;
			}
			break;

		case SCO_LINK:
		case ESCO_LINK:
			hdev->sco_cnt += count;
			if (hdev->sco_cnt > hdev->sco_pkts)
				hdev->sco_cnt = hdev->sco_pkts;

			break;

		case CIS_LINK:
		case BIS_LINK:
		case PA_LINK:
			hdev->iso_cnt += count;
			if (hdev->iso_cnt > hdev->iso_pkts)
				hdev->iso_cnt = hdev->iso_pkts;
			break;

		default:
			bt_dev_err(hdev, "unknown type %d conn %p",
				   conn->type, conn);
			break;
		}
	}

	queue_work(hdev->workqueue, &hdev->tx_work);

	hci_dev_unlock(hdev);
}

static void hci_mode_change_evt(struct hci_dev *hdev, void *data,
				struct sk_buff *skb)
{
	struct hci_ev_mode_change *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		conn->mode = ev->mode;

		if (!test_and_clear_bit(HCI_CONN_MODE_CHANGE_PEND,
					&conn->flags)) {
			if (conn->mode == HCI_CM_ACTIVE)
				set_bit(HCI_CONN_POWER_SAVE, &conn->flags);
			else
				clear_bit(HCI_CONN_POWER_SAVE, &conn->flags);
		}

		if (test_and_clear_bit(HCI_CONN_SCO_SETUP_PEND, &conn->flags))
			hci_sco_setup(conn, ev->status);
	}

	hci_dev_unlock(hdev);
}

static void hci_pin_code_request_evt(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_ev_pin_code_req *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		goto unlock;

	if (conn->state == BT_CONNECTED) {
		hci_conn_hold(conn);
		conn->disc_timeout = HCI_PAIRING_TIMEOUT;
		hci_conn_drop(conn);
	}

	if (!hci_dev_test_flag(hdev, HCI_BONDABLE) &&
	    !test_bit(HCI_CONN_AUTH_INITIATOR, &conn->flags)) {
		hci_send_cmd(hdev, HCI_OP_PIN_CODE_NEG_REPLY,
			     sizeof(ev->bdaddr), &ev->bdaddr);
	} else if (hci_dev_test_flag(hdev, HCI_MGMT)) {
		u8 secure;

		if (conn->pending_sec_level == BT_SECURITY_HIGH)
			secure = 1;
		else
			secure = 0;

		mgmt_pin_code_request(hdev, &ev->bdaddr, secure);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void conn_set_key(struct hci_conn *conn, u8 key_type, u8 pin_len)
{
	if (key_type == HCI_LK_CHANGED_COMBINATION)
		return;

	conn->pin_length = pin_len;
	conn->key_type = key_type;

	switch (key_type) {
	case HCI_LK_LOCAL_UNIT:
	case HCI_LK_REMOTE_UNIT:
	case HCI_LK_DEBUG_COMBINATION:
		return;
	case HCI_LK_COMBINATION:
		if (pin_len == 16)
			conn->pending_sec_level = BT_SECURITY_HIGH;
		else
			conn->pending_sec_level = BT_SECURITY_MEDIUM;
		break;
	case HCI_LK_UNAUTH_COMBINATION_P192:
	case HCI_LK_UNAUTH_COMBINATION_P256:
		conn->pending_sec_level = BT_SECURITY_MEDIUM;
		break;
	case HCI_LK_AUTH_COMBINATION_P192:
		conn->pending_sec_level = BT_SECURITY_HIGH;
		break;
	case HCI_LK_AUTH_COMBINATION_P256:
		conn->pending_sec_level = BT_SECURITY_FIPS;
		break;
	}
}

static void hci_link_key_request_evt(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_ev_link_key_req *ev = data;
	struct hci_cp_link_key_reply cp;
	struct hci_conn *conn;
	struct link_key *key;

	bt_dev_dbg(hdev, "");

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		return;

	hci_dev_lock(hdev);

	key = hci_find_link_key(hdev, &ev->bdaddr);
	if (!key) {
		bt_dev_dbg(hdev, "link key not found for %pMR", &ev->bdaddr);
		goto not_found;
	}

	bt_dev_dbg(hdev, "found key type %u for %pMR", key->type, &ev->bdaddr);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn) {
		clear_bit(HCI_CONN_NEW_LINK_KEY, &conn->flags);

		if ((key->type == HCI_LK_UNAUTH_COMBINATION_P192 ||
		     key->type == HCI_LK_UNAUTH_COMBINATION_P256) &&
		    conn->auth_type != 0xff && (conn->auth_type & 0x01)) {
			bt_dev_dbg(hdev, "ignoring unauthenticated key");
			goto not_found;
		}

		if (key->type == HCI_LK_COMBINATION && key->pin_len < 16 &&
		    (conn->pending_sec_level == BT_SECURITY_HIGH ||
		     conn->pending_sec_level == BT_SECURITY_FIPS)) {
			bt_dev_dbg(hdev, "ignoring key unauthenticated for high security");
			goto not_found;
		}

		conn_set_key(conn, key->type, key->pin_len);
	}

	bacpy(&cp.bdaddr, &ev->bdaddr);
	memcpy(cp.link_key, key->val, HCI_LINK_KEY_SIZE);

	hci_send_cmd(hdev, HCI_OP_LINK_KEY_REPLY, sizeof(cp), &cp);

	hci_dev_unlock(hdev);

	return;

not_found:
	hci_send_cmd(hdev, HCI_OP_LINK_KEY_NEG_REPLY, 6, &ev->bdaddr);
	hci_dev_unlock(hdev);
}

static void hci_link_key_notify_evt(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_link_key_notify *ev = data;
	struct hci_conn *conn;
	struct link_key *key;
	bool persistent;
	u8 pin_len = 0;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		goto unlock;

	/* Ignore NULL link key against CVE-2020-26555 */
	if (!crypto_memneq(ev->link_key, ZERO_KEY, HCI_LINK_KEY_SIZE)) {
		bt_dev_dbg(hdev, "Ignore NULL link key (ZERO KEY) for %pMR",
			   &ev->bdaddr);
		hci_disconnect(conn, HCI_ERROR_AUTH_FAILURE);
		hci_conn_drop(conn);
		goto unlock;
	}

	hci_conn_hold(conn);
	conn->disc_timeout = HCI_DISCONN_TIMEOUT;
	hci_conn_drop(conn);

	set_bit(HCI_CONN_NEW_LINK_KEY, &conn->flags);
	conn_set_key(conn, ev->key_type, conn->pin_length);

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		goto unlock;

	key = hci_add_link_key(hdev, conn, &ev->bdaddr, ev->link_key,
			        ev->key_type, pin_len, &persistent);
	if (!key)
		goto unlock;

	/* Update connection information since adding the key will have
	 * fixed up the type in the case of changed combination keys.
	 */
	if (ev->key_type == HCI_LK_CHANGED_COMBINATION)
		conn_set_key(conn, key->type, key->pin_len);

	mgmt_new_link_key(hdev, key, persistent);

	/* Keep debug keys around only if the HCI_KEEP_DEBUG_KEYS flag
	 * is set. If it's not set simply remove the key from the kernel
	 * list (we've still notified user space about it but with
	 * store_hint being 0).
	 */
	if (key->type == HCI_LK_DEBUG_COMBINATION &&
	    !hci_dev_test_flag(hdev, HCI_KEEP_DEBUG_KEYS)) {
		list_del_rcu(&key->list);
		kfree_rcu(key, rcu);
		goto unlock;
	}

	if (persistent)
		clear_bit(HCI_CONN_FLUSH_KEY, &conn->flags);
	else
		set_bit(HCI_CONN_FLUSH_KEY, &conn->flags);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_clock_offset_evt(struct hci_dev *hdev, void *data,
				 struct sk_buff *skb)
{
	struct hci_ev_clock_offset *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn && !ev->status) {
		struct inquiry_entry *ie;

		ie = hci_inquiry_cache_lookup(hdev, &conn->dst);
		if (ie) {
			ie->data.clock_offset = ev->clock_offset;
			ie->timestamp = jiffies;
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_pkt_type_change_evt(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_pkt_type_change *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn && !ev->status)
		conn->pkt_type = __le16_to_cpu(ev->pkt_type);

	hci_dev_unlock(hdev);
}

static void hci_pscan_rep_mode_evt(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_pscan_rep_mode *ev = data;
	struct inquiry_entry *ie;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
	if (ie) {
		ie->data.pscan_rep_mode = ev->pscan_rep_mode;
		ie->timestamp = jiffies;
	}

	hci_dev_unlock(hdev);
}

static void hci_inquiry_result_with_rssi_evt(struct hci_dev *hdev, void *edata,
					     struct sk_buff *skb)
{
	struct hci_ev_inquiry_result_rssi *ev = edata;
	struct inquiry_data data;
	int i;

	bt_dev_dbg(hdev, "num_rsp %d", ev->num);

	if (!ev->num)
		return;

	if (hci_dev_test_flag(hdev, HCI_PERIODIC_INQ))
		return;

	hci_dev_lock(hdev);

	if (skb->len == array_size(ev->num,
				   sizeof(struct inquiry_info_rssi_pscan))) {
		struct inquiry_info_rssi_pscan *info;

		for (i = 0; i < ev->num; i++) {
			u32 flags;

			info = hci_ev_skb_pull(hdev, skb,
					       HCI_EV_INQUIRY_RESULT_WITH_RSSI,
					       sizeof(*info));
			if (!info) {
				bt_dev_err(hdev, "Malformed HCI Event: 0x%2.2x",
					   HCI_EV_INQUIRY_RESULT_WITH_RSSI);
				goto unlock;
			}

			bacpy(&data.bdaddr, &info->bdaddr);
			data.pscan_rep_mode	= info->pscan_rep_mode;
			data.pscan_period_mode	= info->pscan_period_mode;
			data.pscan_mode		= info->pscan_mode;
			memcpy(data.dev_class, info->dev_class, 3);
			data.clock_offset	= info->clock_offset;
			data.rssi		= info->rssi;
			data.ssp_mode		= 0x00;

			flags = hci_inquiry_cache_update(hdev, &data, false);

			mgmt_device_found(hdev, &info->bdaddr, ACL_LINK, 0x00,
					  info->dev_class, info->rssi,
					  flags, NULL, 0, NULL, 0, 0);
		}
	} else if (skb->len == array_size(ev->num,
					  sizeof(struct inquiry_info_rssi))) {
		struct inquiry_info_rssi *info;

		for (i = 0; i < ev->num; i++) {
			u32 flags;

			info = hci_ev_skb_pull(hdev, skb,
					       HCI_EV_INQUIRY_RESULT_WITH_RSSI,
					       sizeof(*info));
			if (!info) {
				bt_dev_err(hdev, "Malformed HCI Event: 0x%2.2x",
					   HCI_EV_INQUIRY_RESULT_WITH_RSSI);
				goto unlock;
			}

			bacpy(&data.bdaddr, &info->bdaddr);
			data.pscan_rep_mode	= info->pscan_rep_mode;
			data.pscan_period_mode	= info->pscan_period_mode;
			data.pscan_mode		= 0x00;
			memcpy(data.dev_class, info->dev_class, 3);
			data.clock_offset	= info->clock_offset;
			data.rssi		= info->rssi;
			data.ssp_mode		= 0x00;

			flags = hci_inquiry_cache_update(hdev, &data, false);

			mgmt_device_found(hdev, &info->bdaddr, ACL_LINK, 0x00,
					  info->dev_class, info->rssi,
					  flags, NULL, 0, NULL, 0, 0);
		}
	} else {
		bt_dev_err(hdev, "Malformed HCI Event: 0x%2.2x",
			   HCI_EV_INQUIRY_RESULT_WITH_RSSI);
	}
unlock:
	hci_dev_unlock(hdev);
}

static void hci_remote_ext_features_evt(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_ev_remote_ext_features *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (ev->page < HCI_MAX_PAGES)
		memcpy(conn->features[ev->page], ev->features, 8);

	if (!ev->status && ev->page == 0x01) {
		struct inquiry_entry *ie;

		ie = hci_inquiry_cache_lookup(hdev, &conn->dst);
		if (ie)
			ie->data.ssp_mode = (ev->features[0] & LMP_HOST_SSP);

		if (ev->features[0] & LMP_HOST_SSP) {
			set_bit(HCI_CONN_SSP_ENABLED, &conn->flags);
		} else {
			/* It is mandatory by the Bluetooth specification that
			 * Extended Inquiry Results are only used when Secure
			 * Simple Pairing is enabled, but some devices violate
			 * this.
			 *
			 * To make these devices work, the internal SSP
			 * enabled flag needs to be cleared if the remote host
			 * features do not indicate SSP support */
			clear_bit(HCI_CONN_SSP_ENABLED, &conn->flags);
		}

		if (ev->features[0] & LMP_HOST_SC)
			set_bit(HCI_CONN_SC_ENABLED, &conn->flags);
	}

	if (conn->state != BT_CONFIG)
		goto unlock;

	if (!ev->status && !test_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags)) {
		struct hci_cp_remote_name_req cp;
		memset(&cp, 0, sizeof(cp));
		bacpy(&cp.bdaddr, &conn->dst);
		cp.pscan_rep_mode = 0x02;
		hci_send_cmd(hdev, HCI_OP_REMOTE_NAME_REQ, sizeof(cp), &cp);
	} else {
		mgmt_device_connected(hdev, conn, NULL, 0);
	}

	if (!hci_outgoing_auth_needed(hdev, conn)) {
		conn->state = BT_CONNECTED;
		hci_connect_cfm(conn, ev->status);
		hci_conn_drop(conn);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_sync_conn_complete_evt(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_ev_sync_conn_complete *ev = data;
	struct hci_conn *conn;
	u8 status = ev->status;

	switch (ev->link_type) {
	case SCO_LINK:
	case ESCO_LINK:
		break;
	default:
		/* As per Core 5.3 Vol 4 Part E 7.7.35 (p.2219), Link_Type
		 * for HCI_Synchronous_Connection_Complete is limited to
		 * either SCO or eSCO
		 */
		bt_dev_err(hdev, "Ignoring connect complete event for invalid link type");
		return;
	}

	bt_dev_dbg(hdev, "status 0x%2.2x", status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
	if (!conn) {
		if (ev->link_type == ESCO_LINK)
			goto unlock;

		/* When the link type in the event indicates SCO connection
		 * and lookup of the connection object fails, then check
		 * if an eSCO connection object exists.
		 *
		 * The core limits the synchronous connections to either
		 * SCO or eSCO. The eSCO connection is preferred and tried
		 * to be setup first and until successfully established,
		 * the link type will be hinted as eSCO.
		 */
		conn = hci_conn_hash_lookup_ba(hdev, ESCO_LINK, &ev->bdaddr);
		if (!conn)
			goto unlock;
	}

	/* The HCI_Synchronous_Connection_Complete event is only sent once per connection.
	 * Processing it more than once per connection can corrupt kernel memory.
	 *
	 * As the connection handle is set here for the first time, it indicates
	 * whether the connection is already set up.
	 */
	if (!HCI_CONN_HANDLE_UNSET(conn->handle)) {
		bt_dev_err(hdev, "Ignoring HCI_Sync_Conn_Complete event for existing connection");
		goto unlock;
	}

	switch (status) {
	case 0x00:
		status = hci_conn_set_handle(conn, __le16_to_cpu(ev->handle));
		if (status) {
			conn->state = BT_CLOSED;
			break;
		}

		conn->state  = BT_CONNECTED;
		conn->type   = ev->link_type;

		hci_debugfs_create_conn(conn);
		hci_conn_add_sysfs(conn);
		break;

	case 0x10:	/* Connection Accept Timeout */
	case 0x0d:	/* Connection Rejected due to Limited Resources */
	case 0x11:	/* Unsupported Feature or Parameter Value */
	case 0x1c:	/* SCO interval rejected */
	case 0x1a:	/* Unsupported Remote Feature */
	case 0x1e:	/* Invalid LMP Parameters */
	case 0x1f:	/* Unspecified error */
	case 0x20:	/* Unsupported LMP Parameter value */
		if (conn->out) {
			conn->pkt_type = (hdev->esco_type & SCO_ESCO_MASK) |
					(hdev->esco_type & EDR_ESCO_MASK);
			if (hci_setup_sync(conn, conn->parent->handle))
				goto unlock;
		}
		fallthrough;

	default:
		conn->state = BT_CLOSED;
		break;
	}

	bt_dev_dbg(hdev, "SCO connected with air mode: %02x", ev->air_mode);
	/* Notify only in case of SCO over HCI transport data path which
	 * is zero and non-zero value shall be non-HCI transport data path
	 */
	if (conn->codec.data_path == 0 && hdev->notify) {
		switch (ev->air_mode) {
		case 0x02:
			hdev->notify(hdev, HCI_NOTIFY_ENABLE_SCO_CVSD);
			break;
		case 0x03:
			hdev->notify(hdev, HCI_NOTIFY_ENABLE_SCO_TRANSP);
			break;
		}
	}

	hci_connect_cfm(conn, status);
	if (status)
		hci_conn_del(conn);

unlock:
	hci_dev_unlock(hdev);
}

static inline size_t eir_get_length(u8 *eir, size_t eir_len)
{
	size_t parsed = 0;

	while (parsed < eir_len) {
		u8 field_len = eir[0];

		if (field_len == 0)
			return parsed;

		parsed += field_len + 1;
		eir += field_len + 1;
	}

	return eir_len;
}

static void hci_extended_inquiry_result_evt(struct hci_dev *hdev, void *edata,
					    struct sk_buff *skb)
{
	struct hci_ev_ext_inquiry_result *ev = edata;
	struct inquiry_data data;
	size_t eir_len;
	int i;

	if (!hci_ev_skb_pull(hdev, skb, HCI_EV_EXTENDED_INQUIRY_RESULT,
			     flex_array_size(ev, info, ev->num)))
		return;

	bt_dev_dbg(hdev, "num %d", ev->num);

	if (!ev->num)
		return;

	if (hci_dev_test_flag(hdev, HCI_PERIODIC_INQ))
		return;

	hci_dev_lock(hdev);

	for (i = 0; i < ev->num; i++) {
		struct extended_inquiry_info *info = &ev->info[i];
		u32 flags;
		bool name_known;

		bacpy(&data.bdaddr, &info->bdaddr);
		data.pscan_rep_mode	= info->pscan_rep_mode;
		data.pscan_period_mode	= info->pscan_period_mode;
		data.pscan_mode		= 0x00;
		memcpy(data.dev_class, info->dev_class, 3);
		data.clock_offset	= info->clock_offset;
		data.rssi		= info->rssi;
		data.ssp_mode		= 0x01;

		if (hci_dev_test_flag(hdev, HCI_MGMT))
			name_known = eir_get_data(info->data,
						  sizeof(info->data),
						  EIR_NAME_COMPLETE, NULL);
		else
			name_known = true;

		flags = hci_inquiry_cache_update(hdev, &data, name_known);

		eir_len = eir_get_length(info->data, sizeof(info->data));

		mgmt_device_found(hdev, &info->bdaddr, ACL_LINK, 0x00,
				  info->dev_class, info->rssi,
				  flags, info->data, eir_len, NULL, 0, 0);
	}

	hci_dev_unlock(hdev);
}

static void hci_key_refresh_complete_evt(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_ev_key_refresh_complete *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x handle 0x%4.4x", ev->status,
		   __le16_to_cpu(ev->handle));

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	/* For BR/EDR the necessary steps are taken through the
	 * auth_complete event.
	 */
	if (conn->type != LE_LINK)
		goto unlock;

	if (!ev->status)
		conn->sec_level = conn->pending_sec_level;

	clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->flags);

	if (ev->status && conn->state == BT_CONNECTED) {
		hci_disconnect(conn, HCI_ERROR_AUTH_FAILURE);
		hci_conn_drop(conn);
		goto unlock;
	}

	if (conn->state == BT_CONFIG) {
		if (!ev->status)
			conn->state = BT_CONNECTED;

		hci_connect_cfm(conn, ev->status);
		hci_conn_drop(conn);
	} else {
		hci_auth_cfm(conn, ev->status);

		hci_conn_hold(conn);
		conn->disc_timeout = HCI_DISCONN_TIMEOUT;
		hci_conn_drop(conn);
	}

unlock:
	hci_dev_unlock(hdev);
}

static u8 hci_get_auth_req(struct hci_conn *conn)
{
	/* If remote requests no-bonding follow that lead */
	if (conn->remote_auth == HCI_AT_NO_BONDING ||
	    conn->remote_auth == HCI_AT_NO_BONDING_MITM)
		return conn->remote_auth | (conn->auth_type & 0x01);

	/* If both remote and local have enough IO capabilities, require
	 * MITM protection
	 */
	if (conn->remote_cap != HCI_IO_NO_INPUT_OUTPUT &&
	    conn->io_capability != HCI_IO_NO_INPUT_OUTPUT)
		return conn->remote_auth | 0x01;

	/* No MITM protection possible so ignore remote requirement */
	return (conn->remote_auth & ~0x01) | (conn->auth_type & 0x01);
}

static u8 bredr_oob_data_present(struct hci_conn *conn)
{
	struct hci_dev *hdev = conn->hdev;
	struct oob_data *data;

	data = hci_find_remote_oob_data(hdev, &conn->dst, BDADDR_BREDR);
	if (!data)
		return 0x00;

	if (bredr_sc_enabled(hdev)) {
		/* When Secure Connections is enabled, then just
		 * return the present value stored with the OOB
		 * data. The stored value contains the right present
		 * information. However it can only be trusted when
		 * not in Secure Connection Only mode.
		 */
		if (!hci_dev_test_flag(hdev, HCI_SC_ONLY))
			return data->present;

		/* When Secure Connections Only mode is enabled, then
		 * the P-256 values are required. If they are not
		 * available, then do not declare that OOB data is
		 * present.
		 */
		if (!crypto_memneq(data->rand256, ZERO_KEY, 16) ||
		    !crypto_memneq(data->hash256, ZERO_KEY, 16))
			return 0x00;

		return 0x02;
	}

	/* When Secure Connections is not enabled or actually
	 * not supported by the hardware, then check that if
	 * P-192 data values are present.
	 */
	if (!crypto_memneq(data->rand192, ZERO_KEY, 16) ||
	    !crypto_memneq(data->hash192, ZERO_KEY, 16))
		return 0x00;

	return 0x01;
}

static void hci_io_capa_request_evt(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_io_capa_request *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn || !hci_dev_test_flag(hdev, HCI_SSP_ENABLED))
		goto unlock;

	/* Assume remote supports SSP since it has triggered this event */
	set_bit(HCI_CONN_SSP_ENABLED, &conn->flags);

	hci_conn_hold(conn);

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		goto unlock;

	/* Allow pairing if we're pairable, the initiators of the
	 * pairing or if the remote is not requesting bonding.
	 */
	if (hci_dev_test_flag(hdev, HCI_BONDABLE) ||
	    test_bit(HCI_CONN_AUTH_INITIATOR, &conn->flags) ||
	    (conn->remote_auth & ~0x01) == HCI_AT_NO_BONDING) {
		struct hci_cp_io_capability_reply cp;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		/* Change the IO capability from KeyboardDisplay
		 * to DisplayYesNo as it is not supported by BT spec. */
		cp.capability = (conn->io_capability == 0x04) ?
				HCI_IO_DISPLAY_YESNO : conn->io_capability;

		/* If we are initiators, there is no remote information yet */
		if (conn->remote_auth == 0xff) {
			/* Request MITM protection if our IO caps allow it
			 * except for the no-bonding case.
			 */
			if (conn->io_capability != HCI_IO_NO_INPUT_OUTPUT &&
			    conn->auth_type != HCI_AT_NO_BONDING)
				conn->auth_type |= 0x01;
		} else {
			conn->auth_type = hci_get_auth_req(conn);
		}

		/* If we're not bondable, force one of the non-bondable
		 * authentication requirement values.
		 */
		if (!hci_dev_test_flag(hdev, HCI_BONDABLE))
			conn->auth_type &= HCI_AT_NO_BONDING_MITM;

		cp.authentication = conn->auth_type;
		cp.oob_data = bredr_oob_data_present(conn);

		hci_send_cmd(hdev, HCI_OP_IO_CAPABILITY_REPLY,
			     sizeof(cp), &cp);
	} else {
		struct hci_cp_io_capability_neg_reply cp;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		cp.reason = HCI_ERROR_PAIRING_NOT_ALLOWED;

		hci_send_cmd(hdev, HCI_OP_IO_CAPABILITY_NEG_REPLY,
			     sizeof(cp), &cp);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_io_capa_reply_evt(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_io_capa_reply *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		goto unlock;

	conn->remote_cap = ev->capability;
	conn->remote_auth = ev->authentication;

unlock:
	hci_dev_unlock(hdev);
}

static void hci_user_confirm_request_evt(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_ev_user_confirm_req *ev = data;
	int loc_mitm, rem_mitm, confirm_hint = 0;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		goto unlock;

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		goto unlock;

	loc_mitm = (conn->auth_type & 0x01);
	rem_mitm = (conn->remote_auth & 0x01);

	/* If we require MITM but the remote device can't provide that
	 * (it has NoInputNoOutput) then reject the confirmation
	 * request. We check the security level here since it doesn't
	 * necessarily match conn->auth_type.
	 */
	if (conn->pending_sec_level > BT_SECURITY_MEDIUM &&
	    conn->remote_cap == HCI_IO_NO_INPUT_OUTPUT) {
		bt_dev_dbg(hdev, "Rejecting request: remote device can't provide MITM");
		hci_send_cmd(hdev, HCI_OP_USER_CONFIRM_NEG_REPLY,
			     sizeof(ev->bdaddr), &ev->bdaddr);
		goto unlock;
	}

	/* If no side requires MITM protection; use JUST_CFM method */
	if ((!loc_mitm || conn->remote_cap == HCI_IO_NO_INPUT_OUTPUT) &&
	    (!rem_mitm || conn->io_capability == HCI_IO_NO_INPUT_OUTPUT)) {

		/* If we're not the initiator of request authorization and the
		 * local IO capability is not NoInputNoOutput, use JUST_WORKS
		 * method (mgmt_user_confirm with confirm_hint set to 1).
		 */
		if (!test_bit(HCI_CONN_AUTH_PEND, &conn->flags) &&
		    conn->io_capability != HCI_IO_NO_INPUT_OUTPUT) {
			bt_dev_dbg(hdev, "Confirming auto-accept as acceptor");
			confirm_hint = 1;
			goto confirm;
		}

		/* If there already exists link key in local host, leave the
		 * decision to user space since the remote device could be
		 * legitimate or malicious.
		 */
		if (hci_find_link_key(hdev, &ev->bdaddr)) {
			bt_dev_dbg(hdev, "Local host already has link key");
			confirm_hint = 1;
			goto confirm;
		}

		BT_DBG("Auto-accept of user confirmation with %ums delay",
		       hdev->auto_accept_delay);

		if (hdev->auto_accept_delay > 0) {
			int delay = msecs_to_jiffies(hdev->auto_accept_delay);
			queue_delayed_work(conn->hdev->workqueue,
					   &conn->auto_accept_work, delay);
			goto unlock;
		}

		hci_send_cmd(hdev, HCI_OP_USER_CONFIRM_REPLY,
			     sizeof(ev->bdaddr), &ev->bdaddr);
		goto unlock;
	}

confirm:
	mgmt_user_confirm_request(hdev, &ev->bdaddr, ACL_LINK, 0,
				  le32_to_cpu(ev->passkey), confirm_hint);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_user_passkey_request_evt(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_ev_user_passkey_req *ev = data;

	bt_dev_dbg(hdev, "");

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_request(hdev, &ev->bdaddr, ACL_LINK, 0);
}

static void hci_user_passkey_notify_evt(struct hci_dev *hdev, void *data,
					struct sk_buff *skb)
{
	struct hci_ev_user_passkey_notify *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		return;

	conn->passkey_notify = __le32_to_cpu(ev->passkey);
	conn->passkey_entered = 0;

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_notify(hdev, &conn->dst, conn->type,
					 conn->dst_type, conn->passkey_notify,
					 conn->passkey_entered);
}

static void hci_keypress_notify_evt(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_keypress_notify *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		return;

	switch (ev->type) {
	case HCI_KEYPRESS_STARTED:
		conn->passkey_entered = 0;
		return;

	case HCI_KEYPRESS_ENTERED:
		conn->passkey_entered++;
		break;

	case HCI_KEYPRESS_ERASED:
		conn->passkey_entered--;
		break;

	case HCI_KEYPRESS_CLEARED:
		conn->passkey_entered = 0;
		break;

	case HCI_KEYPRESS_COMPLETED:
		return;
	}

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_notify(hdev, &conn->dst, conn->type,
					 conn->dst_type, conn->passkey_notify,
					 conn->passkey_entered);
}

static void hci_simple_pair_complete_evt(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_ev_simple_pair_complete *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn || !hci_conn_ssp_enabled(conn))
		goto unlock;

	/* Reset the authentication requirement to unknown */
	conn->remote_auth = 0xff;

	/* To avoid duplicate auth_failed events to user space we check
	 * the HCI_CONN_AUTH_PEND flag which will be set if we
	 * initiated the authentication. A traditional auth_complete
	 * event gets always produced as initiator and is also mapped to
	 * the mgmt_auth_failed event */
	if (!test_bit(HCI_CONN_AUTH_PEND, &conn->flags) && ev->status)
		mgmt_auth_failed(conn, ev->status);

	hci_conn_drop(conn);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_remote_host_features_evt(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_ev_remote_host_features *ev = data;
	struct inquiry_entry *ie;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn)
		memcpy(conn->features[1], ev->features, 8);

	ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
	if (ie)
		ie->data.ssp_mode = (ev->features[0] & LMP_HOST_SSP);

	hci_dev_unlock(hdev);
}

static void hci_remote_oob_data_request_evt(struct hci_dev *hdev, void *edata,
					    struct sk_buff *skb)
{
	struct hci_ev_remote_oob_data_request *ev = edata;
	struct oob_data *data;

	bt_dev_dbg(hdev, "");

	hci_dev_lock(hdev);

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		goto unlock;

	data = hci_find_remote_oob_data(hdev, &ev->bdaddr, BDADDR_BREDR);
	if (!data) {
		struct hci_cp_remote_oob_data_neg_reply cp;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		hci_send_cmd(hdev, HCI_OP_REMOTE_OOB_DATA_NEG_REPLY,
			     sizeof(cp), &cp);
		goto unlock;
	}

	if (bredr_sc_enabled(hdev)) {
		struct hci_cp_remote_oob_ext_data_reply cp;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		if (hci_dev_test_flag(hdev, HCI_SC_ONLY)) {
			memset(cp.hash192, 0, sizeof(cp.hash192));
			memset(cp.rand192, 0, sizeof(cp.rand192));
		} else {
			memcpy(cp.hash192, data->hash192, sizeof(cp.hash192));
			memcpy(cp.rand192, data->rand192, sizeof(cp.rand192));
		}
		memcpy(cp.hash256, data->hash256, sizeof(cp.hash256));
		memcpy(cp.rand256, data->rand256, sizeof(cp.rand256));

		hci_send_cmd(hdev, HCI_OP_REMOTE_OOB_EXT_DATA_REPLY,
			     sizeof(cp), &cp);
	} else {
		struct hci_cp_remote_oob_data_reply cp;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		memcpy(cp.hash, data->hash192, sizeof(cp.hash));
		memcpy(cp.rand, data->rand192, sizeof(cp.rand));

		hci_send_cmd(hdev, HCI_OP_REMOTE_OOB_DATA_REPLY,
			     sizeof(cp), &cp);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void le_conn_update_addr(struct hci_conn *conn, bdaddr_t *bdaddr,
				u8 bdaddr_type, bdaddr_t *local_rpa)
{
	if (conn->out) {
		conn->dst_type = bdaddr_type;
		conn->resp_addr_type = bdaddr_type;
		bacpy(&conn->resp_addr, bdaddr);

		/* Check if the controller has set a Local RPA then it must be
		 * used instead or hdev->rpa.
		 */
		if (local_rpa && bacmp(local_rpa, BDADDR_ANY)) {
			conn->init_addr_type = ADDR_LE_DEV_RANDOM;
			bacpy(&conn->init_addr, local_rpa);
		} else if (hci_dev_test_flag(conn->hdev, HCI_PRIVACY)) {
			conn->init_addr_type = ADDR_LE_DEV_RANDOM;
			bacpy(&conn->init_addr, &conn->hdev->rpa);
		} else {
			hci_copy_identity_address(conn->hdev, &conn->init_addr,
						  &conn->init_addr_type);
		}
	} else {
		conn->resp_addr_type = conn->hdev->adv_addr_type;
		/* Check if the controller has set a Local RPA then it must be
		 * used instead or hdev->rpa.
		 */
		if (local_rpa && bacmp(local_rpa, BDADDR_ANY)) {
			conn->resp_addr_type = ADDR_LE_DEV_RANDOM;
			bacpy(&conn->resp_addr, local_rpa);
		} else if (conn->hdev->adv_addr_type == ADDR_LE_DEV_RANDOM) {
			/* In case of ext adv, resp_addr will be updated in
			 * Adv Terminated event.
			 */
			if (!ext_adv_capable(conn->hdev))
				bacpy(&conn->resp_addr,
				      &conn->hdev->random_addr);
		} else {
			bacpy(&conn->resp_addr, &conn->hdev->bdaddr);
		}

		conn->init_addr_type = bdaddr_type;
		bacpy(&conn->init_addr, bdaddr);

		/* For incoming connections, set the default minimum
		 * and maximum connection interval. They will be used
		 * to check if the parameters are in range and if not
		 * trigger the connection update procedure.
		 */
		conn->le_conn_min_interval = conn->hdev->le_conn_min_interval;
		conn->le_conn_max_interval = conn->hdev->le_conn_max_interval;
	}
}

static void le_conn_complete_evt(struct hci_dev *hdev, u8 status,
				 bdaddr_t *bdaddr, u8 bdaddr_type,
				 bdaddr_t *local_rpa, u8 role, u16 handle,
				 u16 interval, u16 latency,
				 u16 supervision_timeout)
{
	struct hci_conn_params *params;
	struct hci_conn *conn;
	struct smp_irk *irk;
	u8 addr_type;

	hci_dev_lock(hdev);

	/* All controllers implicitly stop advertising in the event of a
	 * connection, so ensure that the state bit is cleared.
	 */
	hci_dev_clear_flag(hdev, HCI_LE_ADV);

	/* Check for existing connection:
	 *
	 * 1. If it doesn't exist then use the role to create a new object.
	 * 2. If it does exist confirm that it is connecting/BT_CONNECT in case
	 *    of initiator/master role since there could be a collision where
	 *    either side is attempting to connect or something like a fuzzing
	 *    testing is trying to play tricks to destroy the hcon object before
	 *    it even attempts to connect (e.g. hcon->state == BT_OPEN).
	 */
	conn = hci_conn_hash_lookup_role(hdev, LE_LINK, role, bdaddr);
	if (!conn ||
	    (conn->role == HCI_ROLE_MASTER && conn->state != BT_CONNECT)) {
		/* In case of error status and there is no connection pending
		 * just unlock as there is nothing to cleanup.
		 */
		if (status)
			goto unlock;

		conn = hci_conn_add_unset(hdev, LE_LINK, bdaddr, role);
		if (IS_ERR(conn)) {
			bt_dev_err(hdev, "connection err: %ld", PTR_ERR(conn));
			goto unlock;
		}

		conn->dst_type = bdaddr_type;

		/* If we didn't have a hci_conn object previously
		 * but we're in central role this must be something
		 * initiated using an accept list. Since accept list based
		 * connections are not "first class citizens" we don't
		 * have full tracking of them. Therefore, we go ahead
		 * with a "best effort" approach of determining the
		 * initiator address based on the HCI_PRIVACY flag.
		 */
		if (conn->out) {
			conn->resp_addr_type = bdaddr_type;
			bacpy(&conn->resp_addr, bdaddr);
			if (hci_dev_test_flag(hdev, HCI_PRIVACY)) {
				conn->init_addr_type = ADDR_LE_DEV_RANDOM;
				bacpy(&conn->init_addr, &hdev->rpa);
			} else {
				hci_copy_identity_address(hdev,
							  &conn->init_addr,
							  &conn->init_addr_type);
			}
		}
	} else {
		cancel_delayed_work(&conn->le_conn_timeout);
	}

	/* The HCI_LE_Connection_Complete event is only sent once per connection.
	 * Processing it more than once per connection can corrupt kernel memory.
	 *
	 * As the connection handle is set here for the first time, it indicates
	 * whether the connection is already set up.
	 */
	if (!HCI_CONN_HANDLE_UNSET(conn->handle)) {
		bt_dev_err(hdev, "Ignoring HCI_Connection_Complete for existing connection");
		goto unlock;
	}

	le_conn_update_addr(conn, bdaddr, bdaddr_type, local_rpa);

	/* Lookup the identity address from the stored connection
	 * address and address type.
	 *
	 * When establishing connections to an identity address, the
	 * connection procedure will store the resolvable random
	 * address first. Now if it can be converted back into the
	 * identity address, start using the identity address from
	 * now on.
	 */
	irk = hci_get_irk(hdev, &conn->dst, conn->dst_type);
	if (irk) {
		bacpy(&conn->dst, &irk->bdaddr);
		conn->dst_type = irk->addr_type;
	}

	conn->dst_type = ev_bdaddr_type(hdev, conn->dst_type, NULL);

	/* All connection failure handling is taken care of by the
	 * hci_conn_failed function which is triggered by the HCI
	 * request completion callbacks used for connecting.
	 */
	if (status || hci_conn_set_handle(conn, handle))
		goto unlock;

	/* Drop the connection if it has been aborted */
	if (test_bit(HCI_CONN_CANCEL, &conn->flags)) {
		hci_conn_drop(conn);
		goto unlock;
	}

	if (conn->dst_type == ADDR_LE_DEV_PUBLIC)
		addr_type = BDADDR_LE_PUBLIC;
	else
		addr_type = BDADDR_LE_RANDOM;

	/* Drop the connection if the device is blocked */
	if (hci_bdaddr_list_lookup(&hdev->reject_list, &conn->dst, addr_type)) {
		hci_conn_drop(conn);
		goto unlock;
	}

	mgmt_device_connected(hdev, conn, NULL, 0);

	conn->sec_level = BT_SECURITY_LOW;
	conn->state = BT_CONFIG;

	/* Store current advertising instance as connection advertising instance
	 * when software rotation is in use so it can be re-enabled when
	 * disconnected.
	 */
	if (!ext_adv_capable(hdev))
		conn->adv_instance = hdev->cur_adv_instance;

	conn->le_conn_interval = interval;
	conn->le_conn_latency = latency;
	conn->le_supv_timeout = supervision_timeout;

	hci_debugfs_create_conn(conn);
	hci_conn_add_sysfs(conn);

	/* The remote features procedure is defined for central
	 * role only. So only in case of an initiated connection
	 * request the remote features.
	 *
	 * If the local controller supports peripheral-initiated features
	 * exchange, then requesting the remote features in peripheral
	 * role is possible. Otherwise just transition into the
	 * connected state without requesting the remote features.
	 */
	if (conn->out ||
	    (hdev->le_features[0] & HCI_LE_PERIPHERAL_FEATURES)) {
		struct hci_cp_le_read_remote_features cp;

		cp.handle = __cpu_to_le16(conn->handle);

		hci_send_cmd(hdev, HCI_OP_LE_READ_REMOTE_FEATURES,
			     sizeof(cp), &cp);

		hci_conn_hold(conn);
	} else {
		conn->state = BT_CONNECTED;
		hci_connect_cfm(conn, status);
	}

	params = hci_pend_le_action_lookup(&hdev->pend_le_conns, &conn->dst,
					   conn->dst_type);
	if (params) {
		hci_pend_le_list_del_init(params);
		if (params->conn) {
			hci_conn_drop(params->conn);
			hci_conn_put(params->conn);
			params->conn = NULL;
		}
	}

unlock:
	hci_update_passive_scan(hdev);
	hci_dev_unlock(hdev);
}

static void hci_le_conn_complete_evt(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_ev_le_conn_complete *ev = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	le_conn_complete_evt(hdev, ev->status, &ev->bdaddr, ev->bdaddr_type,
			     NULL, ev->role, le16_to_cpu(ev->handle),
			     le16_to_cpu(ev->interval),
			     le16_to_cpu(ev->latency),
			     le16_to_cpu(ev->supervision_timeout));
}

static void hci_le_enh_conn_complete_evt(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_ev_le_enh_conn_complete *ev = data;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	le_conn_complete_evt(hdev, ev->status, &ev->bdaddr, ev->bdaddr_type,
			     &ev->local_rpa, ev->role, le16_to_cpu(ev->handle),
			     le16_to_cpu(ev->interval),
			     le16_to_cpu(ev->latency),
			     le16_to_cpu(ev->supervision_timeout));
}

static void hci_le_pa_sync_lost_evt(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_ev_le_pa_sync_lost *ev = data;
	u16 handle = le16_to_cpu(ev->handle);
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "sync handle 0x%4.4x", handle);

	hci_dev_lock(hdev);

	/* Delete the pa sync connection */
	conn = hci_conn_hash_lookup_pa_sync_handle(hdev, handle);
	if (conn) {
		clear_bit(HCI_CONN_BIG_SYNC, &conn->flags);
		clear_bit(HCI_CONN_PA_SYNC, &conn->flags);
		hci_disconn_cfm(conn, HCI_ERROR_REMOTE_USER_TERM);
		hci_conn_del(conn);
	}

	hci_dev_unlock(hdev);
}

static void hci_le_ext_adv_term_evt(struct hci_dev *hdev, void *data,
				    struct sk_buff *skb)
{
	struct hci_evt_le_ext_adv_set_term *ev = data;
	struct hci_conn *conn;
	struct adv_info *adv, *n;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	/* The Bluetooth Core 5.3 specification clearly states that this event
	 * shall not be sent when the Host disables the advertising set. So in
	 * case of HCI_ERROR_CANCELLED_BY_HOST, just ignore the event.
	 *
	 * When the Host disables an advertising set, all cleanup is done via
	 * its command callback and not needed to be duplicated here.
	 */
	if (ev->status == HCI_ERROR_CANCELLED_BY_HOST) {
		bt_dev_warn_ratelimited(hdev, "Unexpected advertising set terminated event");
		return;
	}

	hci_dev_lock(hdev);

	adv = hci_find_adv_instance(hdev, ev->handle);

	if (ev->status) {
		if (!adv)
			goto unlock;

		/* Remove advertising as it has been terminated */
		hci_remove_adv_instance(hdev, ev->handle);
		mgmt_advertising_removed(NULL, hdev, ev->handle);

		list_for_each_entry_safe(adv, n, &hdev->adv_instances, list) {
			if (adv->enabled)
				goto unlock;
		}

		/* We are no longer advertising, clear HCI_LE_ADV */
		hci_dev_clear_flag(hdev, HCI_LE_ADV);
		goto unlock;
	}

	if (adv)
		adv->enabled = false;

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->conn_handle));
	if (conn) {
		/* Store handle in the connection so the correct advertising
		 * instance can be re-enabled when disconnected.
		 */
		conn->adv_instance = ev->handle;

		if (hdev->adv_addr_type != ADDR_LE_DEV_RANDOM ||
		    bacmp(&conn->resp_addr, BDADDR_ANY))
			goto unlock;

		if (!ev->handle) {
			bacpy(&conn->resp_addr, &hdev->random_addr);
			goto unlock;
		}

		if (adv)
			bacpy(&conn->resp_addr, &adv->random_addr);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_le_conn_update_complete_evt(struct hci_dev *hdev, void *data,
					    struct sk_buff *skb)
{
	struct hci_ev_le_conn_update_complete *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	if (ev->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		conn->le_conn_interval = le16_to_cpu(ev->interval);
		conn->le_conn_latency = le16_to_cpu(ev->latency);
		conn->le_supv_timeout = le16_to_cpu(ev->supervision_timeout);
	}

	hci_dev_unlock(hdev);
}

/* This function requires the caller holds hdev->lock */
static struct hci_conn *check_pending_le_conn(struct hci_dev *hdev,
					      bdaddr_t *addr,
					      u8 addr_type, bool addr_resolved,
					      u8 adv_type, u8 phy, u8 sec_phy)
{
	struct hci_conn *conn;
	struct hci_conn_params *params;

	/* If the event is not connectable don't proceed further */
	if (adv_type != LE_ADV_IND && adv_type != LE_ADV_DIRECT_IND)
		return NULL;

	/* Ignore if the device is blocked or hdev is suspended */
	if (hci_bdaddr_list_lookup(&hdev->reject_list, addr, addr_type) ||
	    hdev->suspended)
		return NULL;

	/* Most controller will fail if we try to create new connections
	 * while we have an existing one in peripheral role.
	 */
	if (hdev->conn_hash.le_num_peripheral > 0 &&
	    (hci_test_quirk(hdev, HCI_QUIRK_BROKEN_LE_STATES) ||
	     !(hdev->le_states[3] & 0x10)))
		return NULL;

	/* If we're not connectable only connect devices that we have in
	 * our pend_le_conns list.
	 */
	params = hci_pend_le_action_lookup(&hdev->pend_le_conns, addr,
					   addr_type);
	if (!params)
		return NULL;

	if (!params->explicit_connect) {
		switch (params->auto_connect) {
		case HCI_AUTO_CONN_DIRECT:
			/* Only devices advertising with ADV_DIRECT_IND are
			 * triggering a connection attempt. This is allowing
			 * incoming connections from peripheral devices.
			 */
			if (adv_type != LE_ADV_DIRECT_IND)
				return NULL;
			break;
		case HCI_AUTO_CONN_ALWAYS:
			/* Devices advertising with ADV_IND or ADV_DIRECT_IND
			 * are triggering a connection attempt. This means
			 * that incoming connections from peripheral device are
			 * accepted and also outgoing connections to peripheral
			 * devices are established when found.
			 */
			break;
		default:
			return NULL;
		}
	}

	conn = hci_connect_le(hdev, addr, addr_type, addr_resolved,
			      BT_SECURITY_LOW, hdev->def_le_autoconnect_timeout,
			      HCI_ROLE_MASTER, phy, sec_phy);
	if (!IS_ERR(conn)) {
		/* If HCI_AUTO_CONN_EXPLICIT is set, conn is already owned
		 * by higher layer that tried to connect, if no then
		 * store the pointer since we don't really have any
		 * other owner of the object besides the params that
		 * triggered it. This way we can abort the connection if
		 * the parameters get removed and keep the reference
		 * count consistent once the connection is established.
		 */

		if (!params->explicit_connect)
			params->conn = hci_conn_get(conn);

		return conn;
	}

	switch (PTR_ERR(conn)) {
	case -EBUSY:
		/* If hci_connect() returns -EBUSY it means there is already
		 * an LE connection attempt going on. Since controllers don't
		 * support more than one connection attempt at the time, we
		 * don't consider this an error case.
		 */
		break;
	default:
		BT_DBG("Failed to connect: err %ld", PTR_ERR(conn));
		return NULL;
	}

	return NULL;
}

static void process_adv_report(struct hci_dev *hdev, u8 type, bdaddr_t *bdaddr,
			       u8 bdaddr_type, bdaddr_t *direct_addr,
			       u8 direct_addr_type, u8 phy, u8 sec_phy, s8 rssi,
			       u8 *data, u8 len, bool ext_adv, bool ctl_time,
			       u64 instant)
{
	struct discovery_state *d = &hdev->discovery;
	struct smp_irk *irk;
	struct hci_conn *conn;
	bool match, bdaddr_resolved;
	u32 flags;
	u8 *ptr;

	switch (type) {
	case LE_ADV_IND:
	case LE_ADV_DIRECT_IND:
	case LE_ADV_SCAN_IND:
	case LE_ADV_NONCONN_IND:
	case LE_ADV_SCAN_RSP:
		break;
	default:
		bt_dev_err_ratelimited(hdev, "unknown advertising packet "
				       "type: 0x%02x", type);
		return;
	}

	if (len > max_adv_len(hdev)) {
		bt_dev_err_ratelimited(hdev,
				       "adv larger than maximum supported");
		return;
	}

	/* Find the end of the data in case the report contains padded zero
	 * bytes at the end causing an invalid length value.
	 *
	 * When data is NULL, len is 0 so there is no need for extra ptr
	 * check as 'ptr < data + 0' is already false in such case.
	 */
	for (ptr = data; ptr < data + len && *ptr; ptr += *ptr + 1) {
		if (ptr + 1 + *ptr > data + len)
			break;
	}

	/* Adjust for actual length. This handles the case when remote
	 * device is advertising with incorrect data length.
	 */
	len = ptr - data;

	/* If the direct address is present, then this report is from
	 * a LE Direct Advertising Report event. In that case it is
	 * important to see if the address is matching the local
	 * controller address.
	 *
	 * If local privacy is not enable the controller shall not be
	 * generating such event since according to its documentation it is only
	 * valid for filter_policy 0x02 and 0x03, but the fact that it did
	 * generate LE Direct Advertising Report means it is probably broken and
	 * won't generate any other event which can potentially break
	 * auto-connect logic so in case local privacy is not enable this
	 * ignores the direct_addr so it works as a regular report.
	 */
	if (!hci_dev_test_flag(hdev, HCI_MESH) && direct_addr &&
	    hci_dev_test_flag(hdev, HCI_PRIVACY)) {
		direct_addr_type = ev_bdaddr_type(hdev, direct_addr_type,
						  &bdaddr_resolved);

		/* Only resolvable random addresses are valid for these
		 * kind of reports and others can be ignored.
		 */
		if (!hci_bdaddr_is_rpa(direct_addr, direct_addr_type))
			return;

		/* If the local IRK of the controller does not match
		 * with the resolvable random address provided, then
		 * this report can be ignored.
		 */
		if (!smp_irk_matches(hdev, hdev->irk, direct_addr))
			return;
	}

	/* Check if we need to convert to identity address */
	irk = hci_get_irk(hdev, bdaddr, bdaddr_type);
	if (irk) {
		bdaddr = &irk->bdaddr;
		bdaddr_type = irk->addr_type;
	}

	bdaddr_type = ev_bdaddr_type(hdev, bdaddr_type, &bdaddr_resolved);

	/* Check if we have been requested to connect to this device.
	 *
	 * direct_addr is set only for directed advertising reports (it is NULL
	 * for advertising reports) and is already verified to be RPA above.
	 */
	conn = check_pending_le_conn(hdev, bdaddr, bdaddr_type, bdaddr_resolved,
				     type, phy, sec_phy);
	if (!ext_adv && conn && type == LE_ADV_IND &&
	    len <= max_adv_len(hdev)) {
		/* Store report for later inclusion by
		 * mgmt_device_connected
		 */
		memcpy(conn->le_adv_data, data, len);
		conn->le_adv_data_len = len;
	}

	if (type == LE_ADV_NONCONN_IND || type == LE_ADV_SCAN_IND)
		flags = MGMT_DEV_FOUND_NOT_CONNECTABLE;
	else
		flags = 0;

	/* All scan results should be sent up for Mesh systems */
	if (hci_dev_test_flag(hdev, HCI_MESH)) {
		mgmt_device_found(hdev, bdaddr, LE_LINK, bdaddr_type, NULL,
				  rssi, flags, data, len, NULL, 0, instant);
		return;
	}

	/* Passive scanning shouldn't trigger any device found events,
	 * except for devices marked as CONN_REPORT for which we do send
	 * device found events, or advertisement monitoring requested.
	 */
	if (hdev->le_scan_type == LE_SCAN_PASSIVE) {
		if (type == LE_ADV_DIRECT_IND)
			return;

		if (!hci_pend_le_action_lookup(&hdev->pend_le_reports,
					       bdaddr, bdaddr_type) &&
		    idr_is_empty(&hdev->adv_monitors_idr))
			return;

		mgmt_device_found(hdev, bdaddr, LE_LINK, bdaddr_type, NULL,
				  rssi, flags, data, len, NULL, 0, 0);
		return;
	}

	/* When receiving a scan response, then there is no way to
	 * know if the remote device is connectable or not. However
	 * since scan responses are merged with a previously seen
	 * advertising report, the flags field from that report
	 * will be used.
	 *
	 * In the unlikely case that a controller just sends a scan
	 * response event that doesn't match the pending report, then
	 * it is marked as a standalone SCAN_RSP.
	 */
	if (type == LE_ADV_SCAN_RSP)
		flags = MGMT_DEV_FOUND_SCAN_RSP;

	/* If there's nothing pending either store the data from this
	 * event or send an immediate device found event if the data
	 * should not be stored for later.
	 */
	if (!has_pending_adv_report(hdev)) {
		/* If the report will trigger a SCAN_REQ store it for
		 * later merging.
		 */
		if (!ext_adv && (type == LE_ADV_IND ||
				 type == LE_ADV_SCAN_IND)) {
			store_pending_adv_report(hdev, bdaddr, bdaddr_type,
						 rssi, flags, data, len);
			return;
		}

		mgmt_device_found(hdev, bdaddr, LE_LINK, bdaddr_type, NULL,
				  rssi, flags, data, len, NULL, 0, 0);
		return;
	}

	/* Check if the pending report is for the same device as the new one */
	match = (!bacmp(bdaddr, &d->last_adv_addr) &&
		 bdaddr_type == d->last_adv_addr_type);

	/* If the pending data doesn't match this report or this isn't a
	 * scan response (e.g. we got a duplicate ADV_IND) then force
	 * sending of the pending data.
	 */
	if (type != LE_ADV_SCAN_RSP || !match) {
		/* Send out whatever is in the cache, but skip duplicates */
		if (!match)
			mgmt_device_found(hdev, &d->last_adv_addr, LE_LINK,
					  d->last_adv_addr_type, NULL,
					  d->last_adv_rssi, d->last_adv_flags,
					  d->last_adv_data,
					  d->last_adv_data_len, NULL, 0, 0);

		/* If the new report will trigger a SCAN_REQ store it for
		 * later merging.
		 */
		if (!ext_adv && (type == LE_ADV_IND ||
				 type == LE_ADV_SCAN_IND)) {
			store_pending_adv_report(hdev, bdaddr, bdaddr_type,
						 rssi, flags, data, len);
			return;
		}

		/* The advertising reports cannot be merged, so clear
		 * the pending report and send out a device found event.
		 */
		clear_pending_adv_report(hdev);
		mgmt_device_found(hdev, bdaddr, LE_LINK, bdaddr_type, NULL,
				  rssi, flags, data, len, NULL, 0, 0);
		return;
	}

	/* If we get here we've got a pending ADV_IND or ADV_SCAN_IND and
	 * the new event is a SCAN_RSP. We can therefore proceed with
	 * sending a merged device found event.
	 */
	mgmt_device_found(hdev, &d->last_adv_addr, LE_LINK,
			  d->last_adv_addr_type, NULL, rssi, d->last_adv_flags,
			  d->last_adv_data, d->last_adv_data_len, data, len, 0);
	clear_pending_adv_report(hdev);
}

static void hci_le_adv_report_evt(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_le_advertising_report *ev = data;
	u64 instant = jiffies;

	if (!ev->num)
		return;

	hci_dev_lock(hdev);

	while (ev->num--) {
		struct hci_ev_le_advertising_info *info;
		s8 rssi;

		info = hci_le_ev_skb_pull(hdev, skb,
					  HCI_EV_LE_ADVERTISING_REPORT,
					  sizeof(*info));
		if (!info)
			break;

		if (!hci_le_ev_skb_pull(hdev, skb, HCI_EV_LE_ADVERTISING_REPORT,
					info->length + 1))
			break;

		if (info->length <= max_adv_len(hdev)) {
			rssi = info->data[info->length];
			process_adv_report(hdev, info->type, &info->bdaddr,
					   info->bdaddr_type, NULL, 0,
					   HCI_ADV_PHY_1M, 0, rssi,
					   info->data, info->length, false,
					   false, instant);
		} else {
			bt_dev_err(hdev, "Dropping invalid advertising data");
		}
	}

	hci_dev_unlock(hdev);
}

static u8 ext_evt_type_to_legacy(struct hci_dev *hdev, u16 evt_type)
{
	u16 pdu_type = evt_type & ~LE_EXT_ADV_DATA_STATUS_MASK;

	if (!pdu_type)
		return LE_ADV_NONCONN_IND;

	if (evt_type & LE_EXT_ADV_LEGACY_PDU) {
		switch (evt_type) {
		case LE_LEGACY_ADV_IND:
			return LE_ADV_IND;
		case LE_LEGACY_ADV_DIRECT_IND:
			return LE_ADV_DIRECT_IND;
		case LE_LEGACY_ADV_SCAN_IND:
			return LE_ADV_SCAN_IND;
		case LE_LEGACY_NONCONN_IND:
			return LE_ADV_NONCONN_IND;
		case LE_LEGACY_SCAN_RSP_ADV:
		case LE_LEGACY_SCAN_RSP_ADV_SCAN:
			return LE_ADV_SCAN_RSP;
		}

		goto invalid;
	}

	if (evt_type & LE_EXT_ADV_CONN_IND) {
		if (evt_type & LE_EXT_ADV_DIRECT_IND)
			return LE_ADV_DIRECT_IND;

		return LE_ADV_IND;
	}

	if (evt_type & LE_EXT_ADV_SCAN_RSP)
		return LE_ADV_SCAN_RSP;

	if (evt_type & LE_EXT_ADV_SCAN_IND)
		return LE_ADV_SCAN_IND;

	if (evt_type & LE_EXT_ADV_DIRECT_IND)
		return LE_ADV_NONCONN_IND;

invalid:
	bt_dev_err_ratelimited(hdev, "Unknown advertising packet type: 0x%02x",
			       evt_type);

	return LE_ADV_INVALID;
}

static void hci_le_ext_adv_report_evt(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_ev_le_ext_adv_report *ev = data;
	u64 instant = jiffies;

	if (!ev->num)
		return;

	hci_dev_lock(hdev);

	while (ev->num--) {
		struct hci_ev_le_ext_adv_info *info;
		u8 legacy_evt_type;
		u16 evt_type;

		info = hci_le_ev_skb_pull(hdev, skb, HCI_EV_LE_EXT_ADV_REPORT,
					  sizeof(*info));
		if (!info)
			break;

		if (!hci_le_ev_skb_pull(hdev, skb, HCI_EV_LE_EXT_ADV_REPORT,
					info->length))
			break;

		evt_type = __le16_to_cpu(info->type) & LE_EXT_ADV_EVT_TYPE_MASK;
		legacy_evt_type = ext_evt_type_to_legacy(hdev, evt_type);

		if (hci_test_quirk(hdev,
				   HCI_QUIRK_FIXUP_LE_EXT_ADV_REPORT_PHY)) {
			info->primary_phy &= 0x1f;
			info->secondary_phy &= 0x1f;
		}

		/* Check if PA Sync is pending and if the hci_conn SID has not
		 * been set update it.
		 */
		if (hci_dev_test_flag(hdev, HCI_PA_SYNC)) {
			struct hci_conn *conn;

			conn = hci_conn_hash_lookup_create_pa_sync(hdev);
			if (conn && conn->sid == HCI_SID_INVALID)
				conn->sid = info->sid;
		}

		if (legacy_evt_type != LE_ADV_INVALID) {
			process_adv_report(hdev, legacy_evt_type, &info->bdaddr,
					   info->bdaddr_type, NULL, 0,
					   info->primary_phy,
					   info->secondary_phy,
					   info->rssi, info->data, info->length,
					   !(evt_type & LE_EXT_ADV_LEGACY_PDU),
					   false, instant);
		}
	}

	hci_dev_unlock(hdev);
}

static int hci_le_pa_term_sync(struct hci_dev *hdev, __le16 handle)
{
	struct hci_cp_le_pa_term_sync cp;

	memset(&cp, 0, sizeof(cp));
	cp.handle = handle;

	return hci_send_cmd(hdev, HCI_OP_LE_PA_TERM_SYNC, sizeof(cp), &cp);
}

static void hci_le_pa_sync_established_evt(struct hci_dev *hdev, void *data,
					   struct sk_buff *skb)
{
	struct hci_ev_le_pa_sync_established *ev = data;
	int mask = hdev->link_mode;
	__u8 flags = 0;
	struct hci_conn *pa_sync, *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	hci_dev_clear_flag(hdev, HCI_PA_SYNC);

	conn = hci_conn_hash_lookup_create_pa_sync(hdev);
	if (!conn) {
		bt_dev_err(hdev,
			   "Unable to find connection for dst %pMR sid 0x%2.2x",
			   &ev->bdaddr, ev->sid);
		goto unlock;
	}

	clear_bit(HCI_CONN_CREATE_PA_SYNC, &conn->flags);

	conn->sync_handle = le16_to_cpu(ev->handle);
	conn->sid = HCI_SID_INVALID;

	mask |= hci_proto_connect_ind(hdev, &ev->bdaddr, PA_LINK,
				      &flags);
	if (!(mask & HCI_LM_ACCEPT)) {
		hci_le_pa_term_sync(hdev, ev->handle);
		goto unlock;
	}

	if (!(flags & HCI_PROTO_DEFER))
		goto unlock;

	/* Add connection to indicate PA sync event */
	pa_sync = hci_conn_add_unset(hdev, PA_LINK, BDADDR_ANY,
				     HCI_ROLE_SLAVE);

	if (IS_ERR(pa_sync))
		goto unlock;

	pa_sync->sync_handle = le16_to_cpu(ev->handle);

	if (ev->status) {
		set_bit(HCI_CONN_PA_SYNC_FAILED, &pa_sync->flags);

		/* Notify iso layer */
		hci_connect_cfm(pa_sync, ev->status);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_le_per_adv_report_evt(struct hci_dev *hdev, void *data,
				      struct sk_buff *skb)
{
	struct hci_ev_le_per_adv_report *ev = data;
	int mask = hdev->link_mode;
	__u8 flags = 0;
	struct hci_conn *pa_sync;

	bt_dev_dbg(hdev, "sync_handle 0x%4.4x", le16_to_cpu(ev->sync_handle));

	hci_dev_lock(hdev);

	mask |= hci_proto_connect_ind(hdev, BDADDR_ANY, PA_LINK, &flags);
	if (!(mask & HCI_LM_ACCEPT))
		goto unlock;

	if (!(flags & HCI_PROTO_DEFER))
		goto unlock;

	pa_sync = hci_conn_hash_lookup_pa_sync_handle
			(hdev,
			le16_to_cpu(ev->sync_handle));

	if (!pa_sync)
		goto unlock;

	if (ev->data_status == LE_PA_DATA_COMPLETE &&
	    !test_and_set_bit(HCI_CONN_PA_SYNC, &pa_sync->flags)) {
		/* Notify iso layer */
		hci_connect_cfm(pa_sync, 0);

		/* Notify MGMT layer */
		mgmt_device_connected(hdev, pa_sync, NULL, 0);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_le_remote_feat_complete_evt(struct hci_dev *hdev, void *data,
					    struct sk_buff *skb)
{
	struct hci_ev_le_remote_feat_complete *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status)
			memcpy(conn->features[0], ev->features, 8);

		if (conn->state == BT_CONFIG) {
			__u8 status;

			/* If the local controller supports peripheral-initiated
			 * features exchange, but the remote controller does
			 * not, then it is possible that the error code 0x1a
			 * for unsupported remote feature gets returned.
			 *
			 * In this specific case, allow the connection to
			 * transition into connected state and mark it as
			 * successful.
			 */
			if (!conn->out && ev->status == HCI_ERROR_UNSUPPORTED_REMOTE_FEATURE &&
			    (hdev->le_features[0] & HCI_LE_PERIPHERAL_FEATURES))
				status = 0x00;
			else
				status = ev->status;

			conn->state = BT_CONNECTED;
			hci_connect_cfm(conn, status);
			hci_conn_drop(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_le_ltk_request_evt(struct hci_dev *hdev, void *data,
				   struct sk_buff *skb)
{
	struct hci_ev_le_ltk_req *ev = data;
	struct hci_cp_le_ltk_reply cp;
	struct hci_cp_le_ltk_neg_reply neg;
	struct hci_conn *conn;
	struct smp_ltk *ltk;

	bt_dev_dbg(hdev, "handle 0x%4.4x", __le16_to_cpu(ev->handle));

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn == NULL)
		goto not_found;

	ltk = hci_find_ltk(hdev, &conn->dst, conn->dst_type, conn->role);
	if (!ltk)
		goto not_found;

	if (smp_ltk_is_sc(ltk)) {
		/* With SC both EDiv and Rand are set to zero */
		if (ev->ediv || ev->rand)
			goto not_found;
	} else {
		/* For non-SC keys check that EDiv and Rand match */
		if (ev->ediv != ltk->ediv || ev->rand != ltk->rand)
			goto not_found;
	}

	memcpy(cp.ltk, ltk->val, ltk->enc_size);
	memset(cp.ltk + ltk->enc_size, 0, sizeof(cp.ltk) - ltk->enc_size);
	cp.handle = cpu_to_le16(conn->handle);

	conn->pending_sec_level = smp_ltk_sec_level(ltk);

	conn->enc_key_size = ltk->enc_size;

	hci_send_cmd(hdev, HCI_OP_LE_LTK_REPLY, sizeof(cp), &cp);

	/* Ref. Bluetooth Core SPEC pages 1975 and 2004. STK is a
	 * temporary key used to encrypt a connection following
	 * pairing. It is used during the Encrypted Session Setup to
	 * distribute the keys. Later, security can be re-established
	 * using a distributed LTK.
	 */
	if (ltk->type == SMP_STK) {
		set_bit(HCI_CONN_STK_ENCRYPT, &conn->flags);
		list_del_rcu(&ltk->list);
		kfree_rcu(ltk, rcu);
	} else {
		clear_bit(HCI_CONN_STK_ENCRYPT, &conn->flags);
	}

	hci_dev_unlock(hdev);

	return;

not_found:
	neg.handle = ev->handle;
	hci_send_cmd(hdev, HCI_OP_LE_LTK_NEG_REPLY, sizeof(neg), &neg);
	hci_dev_unlock(hdev);
}

static void send_conn_param_neg_reply(struct hci_dev *hdev, u16 handle,
				      u8 reason)
{
	struct hci_cp_le_conn_param_req_neg_reply cp;

	cp.handle = cpu_to_le16(handle);
	cp.reason = reason;

	hci_send_cmd(hdev, HCI_OP_LE_CONN_PARAM_REQ_NEG_REPLY, sizeof(cp),
		     &cp);
}

static void hci_le_remote_conn_param_req_evt(struct hci_dev *hdev, void *data,
					     struct sk_buff *skb)
{
	struct hci_ev_le_remote_conn_param_req *ev = data;
	struct hci_cp_le_conn_param_req_reply cp;
	struct hci_conn *hcon;
	u16 handle, min, max, latency, timeout;

	bt_dev_dbg(hdev, "handle 0x%4.4x", __le16_to_cpu(ev->handle));

	handle = le16_to_cpu(ev->handle);
	min = le16_to_cpu(ev->interval_min);
	max = le16_to_cpu(ev->interval_max);
	latency = le16_to_cpu(ev->latency);
	timeout = le16_to_cpu(ev->timeout);

	hcon = hci_conn_hash_lookup_handle(hdev, handle);
	if (!hcon || hcon->state != BT_CONNECTED)
		return send_conn_param_neg_reply(hdev, handle,
						 HCI_ERROR_UNKNOWN_CONN_ID);

	if (max > hcon->le_conn_max_interval)
		return send_conn_param_neg_reply(hdev, handle,
						 HCI_ERROR_INVALID_LL_PARAMS);

	if (hci_check_conn_params(min, max, latency, timeout))
		return send_conn_param_neg_reply(hdev, handle,
						 HCI_ERROR_INVALID_LL_PARAMS);

	if (hcon->role == HCI_ROLE_MASTER) {
		struct hci_conn_params *params;
		u8 store_hint;

		hci_dev_lock(hdev);

		params = hci_conn_params_lookup(hdev, &hcon->dst,
						hcon->dst_type);
		if (params) {
			params->conn_min_interval = min;
			params->conn_max_interval = max;
			params->conn_latency = latency;
			params->supervision_timeout = timeout;
			store_hint = 0x01;
		} else {
			store_hint = 0x00;
		}

		hci_dev_unlock(hdev);

		mgmt_new_conn_param(hdev, &hcon->dst, hcon->dst_type,
				    store_hint, min, max, latency, timeout);
	}

	cp.handle = ev->handle;
	cp.interval_min = ev->interval_min;
	cp.interval_max = ev->interval_max;
	cp.latency = ev->latency;
	cp.timeout = ev->timeout;
	cp.min_ce_len = 0;
	cp.max_ce_len = 0;

	hci_send_cmd(hdev, HCI_OP_LE_CONN_PARAM_REQ_REPLY, sizeof(cp), &cp);
}

static void hci_le_direct_adv_report_evt(struct hci_dev *hdev, void *data,
					 struct sk_buff *skb)
{
	struct hci_ev_le_direct_adv_report *ev = data;
	u64 instant = jiffies;
	int i;

	if (!hci_le_ev_skb_pull(hdev, skb, HCI_EV_LE_DIRECT_ADV_REPORT,
				flex_array_size(ev, info, ev->num)))
		return;

	if (!ev->num)
		return;

	hci_dev_lock(hdev);

	for (i = 0; i < ev->num; i++) {
		struct hci_ev_le_direct_adv_info *info = &ev->info[i];

		process_adv_report(hdev, info->type, &info->bdaddr,
				   info->bdaddr_type, &info->direct_addr,
				   info->direct_addr_type, HCI_ADV_PHY_1M, 0,
				   info->rssi, NULL, 0, false, false, instant);
	}

	hci_dev_unlock(hdev);
}

static void hci_le_phy_update_evt(struct hci_dev *hdev, void *data,
				  struct sk_buff *skb)
{
	struct hci_ev_le_phy_update_complete *ev = data;
	struct hci_conn *conn;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	if (ev->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	conn->le_tx_phy = ev->tx_phy;
	conn->le_rx_phy = ev->rx_phy;

unlock:
	hci_dev_unlock(hdev);
}

static void hci_le_cis_established_evt(struct hci_dev *hdev, void *data,
				       struct sk_buff *skb)
{
	struct hci_evt_le_cis_established *ev = data;
	struct hci_conn *conn;
	struct bt_iso_qos *qos;
	bool pending = false;
	u16 handle = __le16_to_cpu(ev->handle);
	u32 c_sdu_interval, p_sdu_interval;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, handle);
	if (!conn) {
		bt_dev_err(hdev,
			   "Unable to find connection with handle 0x%4.4x",
			   handle);
		goto unlock;
	}

	if (conn->type != CIS_LINK) {
		bt_dev_err(hdev,
			   "Invalid connection link type handle 0x%4.4x",
			   handle);
		goto unlock;
	}

	qos = &conn->iso_qos;

	pending = test_and_clear_bit(HCI_CONN_CREATE_CIS, &conn->flags);

	/* BLUETOOTH CORE SPECIFICATION Version 5.4 | Vol 6, Part G
	 * page 3075:
	 * Transport_Latency_C_To_P = CIG_Sync_Delay + (FT_C_To_P) ×
	 * ISO_Interval + SDU_Interval_C_To_P
	 * ...
	 * SDU_Interval = (CIG_Sync_Delay + (FT) x ISO_Interval) -
	 *					Transport_Latency
	 */
	c_sdu_interval = (get_unaligned_le24(ev->cig_sync_delay) +
			 (ev->c_ft * le16_to_cpu(ev->interval) * 1250)) -
			get_unaligned_le24(ev->c_latency);
	p_sdu_interval = (get_unaligned_le24(ev->cig_sync_delay) +
			 (ev->p_ft * le16_to_cpu(ev->interval) * 1250)) -
			get_unaligned_le24(ev->p_latency);

	switch (conn->role) {
	case HCI_ROLE_SLAVE:
		qos->ucast.in.interval = c_sdu_interval;
		qos->ucast.out.interval = p_sdu_interval;
		/* Convert Transport Latency (us) to Latency (msec) */
		qos->ucast.in.latency =
			DIV_ROUND_CLOSEST(get_unaligned_le24(ev->c_latency),
					  1000);
		qos->ucast.out.latency =
			DIV_ROUND_CLOSEST(get_unaligned_le24(ev->p_latency),
					  1000);
		qos->ucast.in.sdu = ev->c_bn ? le16_to_cpu(ev->c_mtu) : 0;
		qos->ucast.out.sdu = ev->p_bn ? le16_to_cpu(ev->p_mtu) : 0;
		qos->ucast.in.phy = ev->c_phy;
		qos->ucast.out.phy = ev->p_phy;
		break;
	case HCI_ROLE_MASTER:
		qos->ucast.in.interval = p_sdu_interval;
		qos->ucast.out.interval = c_sdu_interval;
		/* Convert Transport Latency (us) to Latency (msec) */
		qos->ucast.out.latency =
			DIV_ROUND_CLOSEST(get_unaligned_le24(ev->c_latency),
					  1000);
		qos->ucast.in.latency =
			DIV_ROUND_CLOSEST(get_unaligned_le24(ev->p_latency),
					  1000);
		qos->ucast.out.sdu = ev->c_bn ? le16_to_cpu(ev->c_mtu) : 0;
		qos->ucast.in.sdu = ev->p_bn ? le16_to_cpu(ev->p_mtu) : 0;
		qos->ucast.out.phy = ev->c_phy;
		qos->ucast.in.phy = ev->p_phy;
		break;
	}

	if (!ev->status) {
		conn->state = BT_CONNECTED;
		hci_debugfs_create_conn(conn);
		hci_conn_add_sysfs(conn);
		hci_iso_setup_path(conn);
		goto unlock;
	}

	conn->state = BT_CLOSED;
	hci_connect_cfm(conn, ev->status);
	hci_conn_del(conn);

unlock:
	if (pending)
		hci_le_create_cis_pending(hdev);

	hci_dev_unlock(hdev);
}

static void hci_le_reject_cis(struct hci_dev *hdev, __le16 handle)
{
	struct hci_cp_le_reject_cis cp;

	memset(&cp, 0, sizeof(cp));
	cp.handle = handle;
	cp.reason = HCI_ERROR_REJ_BAD_ADDR;
	hci_send_cmd(hdev, HCI_OP_LE_REJECT_CIS, sizeof(cp), &cp);
}

static void hci_le_accept_cis(struct hci_dev *hdev, __le16 handle)
{
	struct hci_cp_le_accept_cis cp;

	memset(&cp, 0, sizeof(cp));
	cp.handle = handle;
	hci_send_cmd(hdev, HCI_OP_LE_ACCEPT_CIS, sizeof(cp), &cp);
}

static void hci_le_cis_req_evt(struct hci_dev *hdev, void *data,
			       struct sk_buff *skb)
{
	struct hci_evt_le_cis_req *ev = data;
	u16 acl_handle, cis_handle;
	struct hci_conn *acl, *cis;
	int mask;
	__u8 flags = 0;

	acl_handle = __le16_to_cpu(ev->acl_handle);
	cis_handle = __le16_to_cpu(ev->cis_handle);

	bt_dev_dbg(hdev, "acl 0x%4.4x handle 0x%4.4x cig 0x%2.2x cis 0x%2.2x",
		   acl_handle, cis_handle, ev->cig_id, ev->cis_id);

	hci_dev_lock(hdev);

	acl = hci_conn_hash_lookup_handle(hdev, acl_handle);
	if (!acl)
		goto unlock;

	mask = hci_proto_connect_ind(hdev, &acl->dst, CIS_LINK, &flags);
	if (!(mask & HCI_LM_ACCEPT)) {
		hci_le_reject_cis(hdev, ev->cis_handle);
		goto unlock;
	}

	cis = hci_conn_hash_lookup_handle(hdev, cis_handle);
	if (!cis) {
		cis = hci_conn_add(hdev, CIS_LINK, &acl->dst,
				   HCI_ROLE_SLAVE, cis_handle);
		if (IS_ERR(cis)) {
			hci_le_reject_cis(hdev, ev->cis_handle);
			goto unlock;
		}
	}

	cis->iso_qos.ucast.cig = ev->cig_id;
	cis->iso_qos.ucast.cis = ev->cis_id;

	if (!(flags & HCI_PROTO_DEFER)) {
		hci_le_accept_cis(hdev, ev->cis_handle);
	} else {
		cis->state = BT_CONNECT2;
		hci_connect_cfm(cis, 0);
	}

unlock:
	hci_dev_unlock(hdev);
}

static int hci_iso_term_big_sync(struct hci_dev *hdev, void *data)
{
	u8 handle = PTR_UINT(data);

	return hci_le_terminate_big_sync(hdev, handle,
					 HCI_ERROR_LOCAL_HOST_TERM);
}

static void hci_le_create_big_complete_evt(struct hci_dev *hdev, void *data,
					   struct sk_buff *skb)
{
	struct hci_evt_le_create_big_complete *ev = data;
	struct hci_conn *conn;
	__u8 i = 0;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	if (!hci_le_ev_skb_pull(hdev, skb, HCI_EVT_LE_CREATE_BIG_COMPLETE,
				flex_array_size(ev, bis_handle, ev->num_bis)))
		return;

	hci_dev_lock(hdev);

	/* Connect all BISes that are bound to the BIG */
	while ((conn = hci_conn_hash_lookup_big_state(hdev, ev->handle,
						      BT_BOUND,
						      HCI_ROLE_MASTER))) {
		if (ev->status) {
			hci_connect_cfm(conn, ev->status);
			hci_conn_del(conn);
			continue;
		}

		if (hci_conn_set_handle(conn,
					__le16_to_cpu(ev->bis_handle[i++])))
			continue;

		conn->state = BT_CONNECTED;
		set_bit(HCI_CONN_BIG_CREATED, &conn->flags);
		hci_debugfs_create_conn(conn);
		hci_conn_add_sysfs(conn);
		hci_iso_setup_path(conn);
	}

	if (!ev->status && !i)
		/* If no BISes have been connected for the BIG,
		 * terminate. This is in case all bound connections
		 * have been closed before the BIG creation
		 * has completed.
		 */
		hci_cmd_sync_queue(hdev, hci_iso_term_big_sync,
				   UINT_PTR(ev->handle), NULL);

	hci_dev_unlock(hdev);
}

static void hci_le_big_sync_established_evt(struct hci_dev *hdev, void *data,
					    struct sk_buff *skb)
{
	struct hci_evt_le_big_sync_established *ev = data;
	struct hci_conn *bis, *conn;
	int i;

	bt_dev_dbg(hdev, "status 0x%2.2x", ev->status);

	if (!hci_le_ev_skb_pull(hdev, skb, HCI_EVT_LE_BIG_SYNC_ESTABLISHED,
				flex_array_size(ev, bis, ev->num_bis)))
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_big_sync_pend(hdev, ev->handle,
						  ev->num_bis);
	if (!conn) {
		bt_dev_err(hdev,
			   "Unable to find connection for big 0x%2.2x",
			   ev->handle);
		goto unlock;
	}

	clear_bit(HCI_CONN_CREATE_BIG_SYNC, &conn->flags);

	conn->num_bis = 0;
	memset(conn->bis, 0, sizeof(conn->num_bis));

	for (i = 0; i < ev->num_bis; i++) {
		u16 handle = le16_to_cpu(ev->bis[i]);
		__le32 interval;

		bis = hci_conn_hash_lookup_handle(hdev, handle);
		if (!bis) {
			if (handle > HCI_CONN_HANDLE_MAX) {
				bt_dev_dbg(hdev, "ignore too large handle %u", handle);
				continue;
			}
			bis = hci_conn_add(hdev, BIS_LINK, BDADDR_ANY,
					   HCI_ROLE_SLAVE, handle);
			if (IS_ERR(bis))
				continue;
		}

		if (ev->status != 0x42)
			/* Mark PA sync as established */
			set_bit(HCI_CONN_PA_SYNC, &bis->flags);

		bis->sync_handle = conn->sync_handle;
		bis->iso_qos.bcast.big = ev->handle;
		memset(&interval, 0, sizeof(interval));
		memcpy(&interval, ev->latency, sizeof(ev->latency));
		bis->iso_qos.bcast.in.interval = le32_to_cpu(interval);
		/* Convert ISO Interval (1.25 ms slots) to latency (ms) */
		bis->iso_qos.bcast.in.latency = le16_to_cpu(ev->interval) * 125 / 100;
		bis->iso_qos.bcast.in.sdu = le16_to_cpu(ev->max_pdu);

		if (!ev->status) {
			bis->state = BT_CONNECTED;
			set_bit(HCI_CONN_BIG_SYNC, &bis->flags);
			hci_debugfs_create_conn(bis);
			hci_conn_add_sysfs(bis);
			hci_iso_setup_path(bis);
		}
	}

	/* In case BIG sync failed, notify each failed connection to
	 * the user after all hci connections have been added
	 */
	if (ev->status)
		for (i = 0; i < ev->num_bis; i++) {
			u16 handle = le16_to_cpu(ev->bis[i]);

			bis = hci_conn_hash_lookup_handle(hdev, handle);
			if (!bis)
				continue;

			set_bit(HCI_CONN_BIG_SYNC_FAILED, &bis->flags);
			hci_connect_cfm(bis, ev->status);
		}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_le_big_sync_lost_evt(struct hci_dev *hdev, void *data,
				     struct sk_buff *skb)
{
	struct hci_evt_le_big_sync_lost *ev = data;
	struct hci_conn *bis;
	bool mgmt_conn = false;

	bt_dev_dbg(hdev, "big handle 0x%2.2x", ev->handle);

	hci_dev_lock(hdev);

	/* Delete each bis connection */
	while ((bis = hci_conn_hash_lookup_big_state(hdev, ev->handle,
						     BT_CONNECTED,
						     HCI_ROLE_SLAVE))) {
		if (!mgmt_conn) {
			mgmt_conn = test_and_clear_bit(HCI_CONN_MGMT_CONNECTED,
						       &bis->flags);
			mgmt_device_disconnected(hdev, &bis->dst, bis->type,
						 bis->dst_type, ev->reason,
						 mgmt_conn);
		}

		clear_bit(HCI_CONN_BIG_SYNC, &bis->flags);
		hci_disconn_cfm(bis, ev->reason);
		hci_conn_del(bis);
	}

	hci_dev_unlock(hdev);
}

static void hci_le_big_info_adv_report_evt(struct hci_dev *hdev, void *data,
					   struct sk_buff *skb)
{
	struct hci_evt_le_big_info_adv_report *ev = data;
	int mask = hdev->link_mode;
	__u8 flags = 0;
	struct hci_conn *pa_sync;

	bt_dev_dbg(hdev, "sync_handle 0x%4.4x", le16_to_cpu(ev->sync_handle));

	hci_dev_lock(hdev);

	mask |= hci_proto_connect_ind(hdev, BDADDR_ANY, BIS_LINK, &flags);
	if (!(mask & HCI_LM_ACCEPT))
		goto unlock;

	if (!(flags & HCI_PROTO_DEFER))
		goto unlock;

	pa_sync = hci_conn_hash_lookup_pa_sync_handle
			(hdev,
			le16_to_cpu(ev->sync_handle));

	if (!pa_sync)
		goto unlock;

	pa_sync->iso_qos.bcast.encryption = ev->encryption;

	/* Notify iso layer */
	hci_connect_cfm(pa_sync, 0);

unlock:
	hci_dev_unlock(hdev);
}

#define HCI_LE_EV_VL(_op, _func, _min_len, _max_len) \
[_op] = { \
	.func = _func, \
	.min_len = _min_len, \
	.max_len = _max_len, \
}

#define HCI_LE_EV(_op, _func, _len) \
	HCI_LE_EV_VL(_op, _func, _len, _len)

#define HCI_LE_EV_STATUS(_op, _func) \
	HCI_LE_EV(_op, _func, sizeof(struct hci_ev_status))

/* Entries in this table shall have their position according to the subevent
 * opcode they handle so the use of the macros above is recommend since it does
 * attempt to initialize at its proper index using Designated Initializers that
 * way events without a callback function can be omitted.
 */
static const struct hci_le_ev {
	void (*func)(struct hci_dev *hdev, void *data, struct sk_buff *skb);
	u16  min_len;
	u16  max_len;
} hci_le_ev_table[U8_MAX + 1] = {
	/* [0x01 = HCI_EV_LE_CONN_COMPLETE] */
	HCI_LE_EV(HCI_EV_LE_CONN_COMPLETE, hci_le_conn_complete_evt,
		  sizeof(struct hci_ev_le_conn_complete)),
	/* [0x02 = HCI_EV_LE_ADVERTISING_REPORT] */
	HCI_LE_EV_VL(HCI_EV_LE_ADVERTISING_REPORT, hci_le_adv_report_evt,
		     sizeof(struct hci_ev_le_advertising_report),
		     HCI_MAX_EVENT_SIZE),
	/* [0x03 = HCI_EV_LE_CONN_UPDATE_COMPLETE] */
	HCI_LE_EV(HCI_EV_LE_CONN_UPDATE_COMPLETE,
		  hci_le_conn_update_complete_evt,
		  sizeof(struct hci_ev_le_conn_update_complete)),
	/* [0x04 = HCI_EV_LE_REMOTE_FEAT_COMPLETE] */
	HCI_LE_EV(HCI_EV_LE_REMOTE_FEAT_COMPLETE,
		  hci_le_remote_feat_complete_evt,
		  sizeof(struct hci_ev_le_remote_feat_complete)),
	/* [0x05 = HCI_EV_LE_LTK_REQ] */
	HCI_LE_EV(HCI_EV_LE_LTK_REQ, hci_le_ltk_request_evt,
		  sizeof(struct hci_ev_le_ltk_req)),
	/* [0x06 = HCI_EV_LE_REMOTE_CONN_PARAM_REQ] */
	HCI_LE_EV(HCI_EV_LE_REMOTE_CONN_PARAM_REQ,
		  hci_le_remote_conn_param_req_evt,
		  sizeof(struct hci_ev_le_remote_conn_param_req)),
	/* [0x0a = HCI_EV_LE_ENHANCED_CONN_COMPLETE] */
	HCI_LE_EV(HCI_EV_LE_ENHANCED_CONN_COMPLETE,
		  hci_le_enh_conn_complete_evt,
		  sizeof(struct hci_ev_le_enh_conn_complete)),
	/* [0x0b = HCI_EV_LE_DIRECT_ADV_REPORT] */
	HCI_LE_EV_VL(HCI_EV_LE_DIRECT_ADV_REPORT, hci_le_direct_adv_report_evt,
		     sizeof(struct hci_ev_le_direct_adv_report),
		     HCI_MAX_EVENT_SIZE),
	/* [0x0c = HCI_EV_LE_PHY_UPDATE_COMPLETE] */
	HCI_LE_EV(HCI_EV_LE_PHY_UPDATE_COMPLETE, hci_le_phy_update_evt,
		  sizeof(struct hci_ev_le_phy_update_complete)),
	/* [0x0d = HCI_EV_LE_EXT_ADV_REPORT] */
	HCI_LE_EV_VL(HCI_EV_LE_EXT_ADV_REPORT, hci_le_ext_adv_report_evt,
		     sizeof(struct hci_ev_le_ext_adv_report),
		     HCI_MAX_EVENT_SIZE),
	/* [0x0e = HCI_EV_LE_PA_SYNC_ESTABLISHED] */
	HCI_LE_EV(HCI_EV_LE_PA_SYNC_ESTABLISHED,
		  hci_le_pa_sync_established_evt,
		  sizeof(struct hci_ev_le_pa_sync_established)),
	/* [0x0f = HCI_EV_LE_PER_ADV_REPORT] */
	HCI_LE_EV_VL(HCI_EV_LE_PER_ADV_REPORT,
				 hci_le_per_adv_report_evt,
				 sizeof(struct hci_ev_le_per_adv_report),
				 HCI_MAX_EVENT_SIZE),
	/* [0x10 = HCI_EV_LE_PA_SYNC_LOST] */
	HCI_LE_EV(HCI_EV_LE_PA_SYNC_LOST, hci_le_pa_sync_lost_evt,
		  sizeof(struct hci_ev_le_pa_sync_lost)),
	/* [0x12 = HCI_EV_LE_EXT_ADV_SET_TERM] */
	HCI_LE_EV(HCI_EV_LE_EXT_ADV_SET_TERM, hci_le_ext_adv_term_evt,
		  sizeof(struct hci_evt_le_ext_adv_set_term)),
	/* [0x19 = HCI_EVT_LE_CIS_ESTABLISHED] */
	HCI_LE_EV(HCI_EVT_LE_CIS_ESTABLISHED, hci_le_cis_established_evt,
		  sizeof(struct hci_evt_le_cis_established)),
	/* [0x1a = HCI_EVT_LE_CIS_REQ] */
	HCI_LE_EV(HCI_EVT_LE_CIS_REQ, hci_le_cis_req_evt,
		  sizeof(struct hci_evt_le_cis_req)),
	/* [0x1b = HCI_EVT_LE_CREATE_BIG_COMPLETE] */
	HCI_LE_EV_VL(HCI_EVT_LE_CREATE_BIG_COMPLETE,
		     hci_le_create_big_complete_evt,
		     sizeof(struct hci_evt_le_create_big_complete),
		     HCI_MAX_EVENT_SIZE),
	/* [0x1d = HCI_EV_LE_BIG_SYNC_ESTABLISHED] */
	HCI_LE_EV_VL(HCI_EVT_LE_BIG_SYNC_ESTABLISHED,
		     hci_le_big_sync_established_evt,
		     sizeof(struct hci_evt_le_big_sync_established),
		     HCI_MAX_EVENT_SIZE),
	/* [0x1e = HCI_EVT_LE_BIG_SYNC_LOST] */
	HCI_LE_EV_VL(HCI_EVT_LE_BIG_SYNC_LOST,
		     hci_le_big_sync_lost_evt,
		     sizeof(struct hci_evt_le_big_sync_lost),
		     HCI_MAX_EVENT_SIZE),
	/* [0x22 = HCI_EVT_LE_BIG_INFO_ADV_REPORT] */
	HCI_LE_EV_VL(HCI_EVT_LE_BIG_INFO_ADV_REPORT,
		     hci_le_big_info_adv_report_evt,
		     sizeof(struct hci_evt_le_big_info_adv_report),
		     HCI_MAX_EVENT_SIZE),
};

static void hci_le_meta_evt(struct hci_dev *hdev, void *data,
			    struct sk_buff *skb, u16 *opcode, u8 *status,
			    hci_req_complete_t *req_complete,
			    hci_req_complete_skb_t *req_complete_skb)
{
	struct hci_ev_le_meta *ev = data;
	const struct hci_le_ev *subev;

	bt_dev_dbg(hdev, "subevent 0x%2.2x", ev->subevent);

	/* Only match event if command OGF is for LE */
	if (hdev->req_skb &&
	   (hci_opcode_ogf(hci_skb_opcode(hdev->req_skb)) == 0x08 ||
	    hci_skb_opcode(hdev->req_skb) == HCI_OP_NOP) &&
	    hci_skb_event(hdev->req_skb) == ev->subevent) {
		*opcode = hci_skb_opcode(hdev->req_skb);
		hci_req_cmd_complete(hdev, *opcode, 0x00, req_complete,
				     req_complete_skb);
	}

	subev = &hci_le_ev_table[ev->subevent];
	if (!subev->func)
		return;

	if (skb->len < subev->min_len) {
		bt_dev_err(hdev, "unexpected subevent 0x%2.2x length: %u < %u",
			   ev->subevent, skb->len, subev->min_len);
		return;
	}

	/* Just warn if the length is over max_len size it still be
	 * possible to partially parse the event so leave to callback to
	 * decide if that is acceptable.
	 */
	if (skb->len > subev->max_len)
		bt_dev_warn(hdev, "unexpected subevent 0x%2.2x length: %u > %u",
			    ev->subevent, skb->len, subev->max_len);
	data = hci_le_ev_skb_pull(hdev, skb, ev->subevent, subev->min_len);
	if (!data)
		return;

	subev->func(hdev, data, skb);
}

static bool hci_get_cmd_complete(struct hci_dev *hdev, u16 opcode,
				 u8 event, struct sk_buff *skb)
{
	struct hci_ev_cmd_complete *ev;
	struct hci_event_hdr *hdr;

	if (!skb)
		return false;

	hdr = hci_ev_skb_pull(hdev, skb, event, sizeof(*hdr));
	if (!hdr)
		return false;

	if (event) {
		if (hdr->evt != event)
			return false;
		return true;
	}

	/* Check if request ended in Command Status - no way to retrieve
	 * any extra parameters in this case.
	 */
	if (hdr->evt == HCI_EV_CMD_STATUS)
		return false;

	if (hdr->evt != HCI_EV_CMD_COMPLETE) {
		bt_dev_err(hdev, "last event is not cmd complete (0x%2.2x)",
			   hdr->evt);
		return false;
	}

	ev = hci_cc_skb_pull(hdev, skb, opcode, sizeof(*ev));
	if (!ev)
		return false;

	if (opcode != __le16_to_cpu(ev->opcode)) {
		BT_DBG("opcode doesn't match (0x%2.2x != 0x%2.2x)", opcode,
		       __le16_to_cpu(ev->opcode));
		return false;
	}

	return true;
}

static void hci_store_wake_reason(struct hci_dev *hdev, u8 event,
				  struct sk_buff *skb)
{
	struct hci_ev_le_advertising_info *adv;
	struct hci_ev_le_direct_adv_info *direct_adv;
	struct hci_ev_le_ext_adv_info *ext_adv;
	const struct hci_ev_conn_complete *conn_complete = (void *)skb->data;
	const struct hci_ev_conn_request *conn_request = (void *)skb->data;

	hci_dev_lock(hdev);

	/* If we are currently suspended and this is the first BT event seen,
	 * save the wake reason associated with the event.
	 */
	if (!hdev->suspended || hdev->wake_reason)
		goto unlock;

	/* Default to remote wake. Values for wake_reason are documented in the
	 * Bluez mgmt api docs.
	 */
	hdev->wake_reason = MGMT_WAKE_REASON_REMOTE_WAKE;

	/* Once configured for remote wakeup, we should only wake up for
	 * reconnections. It's useful to see which device is waking us up so
	 * keep track of the bdaddr of the connection event that woke us up.
	 */
	if (event == HCI_EV_CONN_REQUEST) {
		bacpy(&hdev->wake_addr, &conn_request->bdaddr);
		hdev->wake_addr_type = BDADDR_BREDR;
	} else if (event == HCI_EV_CONN_COMPLETE) {
		bacpy(&hdev->wake_addr, &conn_complete->bdaddr);
		hdev->wake_addr_type = BDADDR_BREDR;
	} else if (event == HCI_EV_LE_META) {
		struct hci_ev_le_meta *le_ev = (void *)skb->data;
		u8 subevent = le_ev->subevent;
		u8 *ptr = &skb->data[sizeof(*le_ev)];
		u8 num_reports = *ptr;

		if ((subevent == HCI_EV_LE_ADVERTISING_REPORT ||
		     subevent == HCI_EV_LE_DIRECT_ADV_REPORT ||
		     subevent == HCI_EV_LE_EXT_ADV_REPORT) &&
		    num_reports) {
			adv = (void *)(ptr + 1);
			direct_adv = (void *)(ptr + 1);
			ext_adv = (void *)(ptr + 1);

			switch (subevent) {
			case HCI_EV_LE_ADVERTISING_REPORT:
				bacpy(&hdev->wake_addr, &adv->bdaddr);
				hdev->wake_addr_type = adv->bdaddr_type;
				break;
			case HCI_EV_LE_DIRECT_ADV_REPORT:
				bacpy(&hdev->wake_addr, &direct_adv->bdaddr);
				hdev->wake_addr_type = direct_adv->bdaddr_type;
				break;
			case HCI_EV_LE_EXT_ADV_REPORT:
				bacpy(&hdev->wake_addr, &ext_adv->bdaddr);
				hdev->wake_addr_type = ext_adv->bdaddr_type;
				break;
			}
		}
	} else {
		hdev->wake_reason = MGMT_WAKE_REASON_UNEXPECTED;
	}

unlock:
	hci_dev_unlock(hdev);
}

#define HCI_EV_VL(_op, _func, _min_len, _max_len) \
[_op] = { \
	.req = false, \
	.func = _func, \
	.min_len = _min_len, \
	.max_len = _max_len, \
}

#define HCI_EV(_op, _func, _len) \
	HCI_EV_VL(_op, _func, _len, _len)

#define HCI_EV_STATUS(_op, _func) \
	HCI_EV(_op, _func, sizeof(struct hci_ev_status))

#define HCI_EV_REQ_VL(_op, _func, _min_len, _max_len) \
[_op] = { \
	.req = true, \
	.func_req = _func, \
	.min_len = _min_len, \
	.max_len = _max_len, \
}

#define HCI_EV_REQ(_op, _func, _len) \
	HCI_EV_REQ_VL(_op, _func, _len, _len)

/* Entries in this table shall have their position according to the event opcode
 * they handle so the use of the macros above is recommend since it does attempt
 * to initialize at its proper index using Designated Initializers that way
 * events without a callback function don't have entered.
 */
static const struct hci_ev {
	bool req;
	union {
		void (*func)(struct hci_dev *hdev, void *data,
			     struct sk_buff *skb);
		void (*func_req)(struct hci_dev *hdev, void *data,
				 struct sk_buff *skb, u16 *opcode, u8 *status,
				 hci_req_complete_t *req_complete,
				 hci_req_complete_skb_t *req_complete_skb);
	};
	u16  min_len;
	u16  max_len;
} hci_ev_table[U8_MAX + 1] = {
	/* [0x01 = HCI_EV_INQUIRY_COMPLETE] */
	HCI_EV_STATUS(HCI_EV_INQUIRY_COMPLETE, hci_inquiry_complete_evt),
	/* [0x02 = HCI_EV_INQUIRY_RESULT] */
	HCI_EV_VL(HCI_EV_INQUIRY_RESULT, hci_inquiry_result_evt,
		  sizeof(struct hci_ev_inquiry_result), HCI_MAX_EVENT_SIZE),
	/* [0x03 = HCI_EV_CONN_COMPLETE] */
	HCI_EV(HCI_EV_CONN_COMPLETE, hci_conn_complete_evt,
	       sizeof(struct hci_ev_conn_complete)),
	/* [0x04 = HCI_EV_CONN_REQUEST] */
	HCI_EV(HCI_EV_CONN_REQUEST, hci_conn_request_evt,
	       sizeof(struct hci_ev_conn_request)),
	/* [0x05 = HCI_EV_DISCONN_COMPLETE] */
	HCI_EV(HCI_EV_DISCONN_COMPLETE, hci_disconn_complete_evt,
	       sizeof(struct hci_ev_disconn_complete)),
	/* [0x06 = HCI_EV_AUTH_COMPLETE] */
	HCI_EV(HCI_EV_AUTH_COMPLETE, hci_auth_complete_evt,
	       sizeof(struct hci_ev_auth_complete)),
	/* [0x07 = HCI_EV_REMOTE_NAME] */
	HCI_EV(HCI_EV_REMOTE_NAME, hci_remote_name_evt,
	       sizeof(struct hci_ev_remote_name)),
	/* [0x08 = HCI_EV_ENCRYPT_CHANGE] */
	HCI_EV(HCI_EV_ENCRYPT_CHANGE, hci_encrypt_change_evt,
	       sizeof(struct hci_ev_encrypt_change)),
	/* [0x09 = HCI_EV_CHANGE_LINK_KEY_COMPLETE] */
	HCI_EV(HCI_EV_CHANGE_LINK_KEY_COMPLETE,
	       hci_change_link_key_complete_evt,
	       sizeof(struct hci_ev_change_link_key_complete)),
	/* [0x0b = HCI_EV_REMOTE_FEATURES] */
	HCI_EV(HCI_EV_REMOTE_FEATURES, hci_remote_features_evt,
	       sizeof(struct hci_ev_remote_features)),
	/* [0x0e = HCI_EV_CMD_COMPLETE] */
	HCI_EV_REQ_VL(HCI_EV_CMD_COMPLETE, hci_cmd_complete_evt,
		      sizeof(struct hci_ev_cmd_complete), HCI_MAX_EVENT_SIZE),
	/* [0x0f = HCI_EV_CMD_STATUS] */
	HCI_EV_REQ(HCI_EV_CMD_STATUS, hci_cmd_status_evt,
		   sizeof(struct hci_ev_cmd_status)),
	/* [0x10 = HCI_EV_CMD_STATUS] */
	HCI_EV(HCI_EV_HARDWARE_ERROR, hci_hardware_error_evt,
	       sizeof(struct hci_ev_hardware_error)),
	/* [0x12 = HCI_EV_ROLE_CHANGE] */
	HCI_EV(HCI_EV_ROLE_CHANGE, hci_role_change_evt,
	       sizeof(struct hci_ev_role_change)),
	/* [0x13 = HCI_EV_NUM_COMP_PKTS] */
	HCI_EV_VL(HCI_EV_NUM_COMP_PKTS, hci_num_comp_pkts_evt,
		  sizeof(struct hci_ev_num_comp_pkts), HCI_MAX_EVENT_SIZE),
	/* [0x14 = HCI_EV_MODE_CHANGE] */
	HCI_EV(HCI_EV_MODE_CHANGE, hci_mode_change_evt,
	       sizeof(struct hci_ev_mode_change)),
	/* [0x16 = HCI_EV_PIN_CODE_REQ] */
	HCI_EV(HCI_EV_PIN_CODE_REQ, hci_pin_code_request_evt,
	       sizeof(struct hci_ev_pin_code_req)),
	/* [0x17 = HCI_EV_LINK_KEY_REQ] */
	HCI_EV(HCI_EV_LINK_KEY_REQ, hci_link_key_request_evt,
	       sizeof(struct hci_ev_link_key_req)),
	/* [0x18 = HCI_EV_LINK_KEY_NOTIFY] */
	HCI_EV(HCI_EV_LINK_KEY_NOTIFY, hci_link_key_notify_evt,
	       sizeof(struct hci_ev_link_key_notify)),
	/* [0x1c = HCI_EV_CLOCK_OFFSET] */
	HCI_EV(HCI_EV_CLOCK_OFFSET, hci_clock_offset_evt,
	       sizeof(struct hci_ev_clock_offset)),
	/* [0x1d = HCI_EV_PKT_TYPE_CHANGE] */
	HCI_EV(HCI_EV_PKT_TYPE_CHANGE, hci_pkt_type_change_evt,
	       sizeof(struct hci_ev_pkt_type_change)),
	/* [0x20 = HCI_EV_PSCAN_REP_MODE] */
	HCI_EV(HCI_EV_PSCAN_REP_MODE, hci_pscan_rep_mode_evt,
	       sizeof(struct hci_ev_pscan_rep_mode)),
	/* [0x22 = HCI_EV_INQUIRY_RESULT_WITH_RSSI] */
	HCI_EV_VL(HCI_EV_INQUIRY_RESULT_WITH_RSSI,
		  hci_inquiry_result_with_rssi_evt,
		  sizeof(struct hci_ev_inquiry_result_rssi),
		  HCI_MAX_EVENT_SIZE),
	/* [0x23 = HCI_EV_REMOTE_EXT_FEATURES] */
	HCI_EV(HCI_EV_REMOTE_EXT_FEATURES, hci_remote_ext_features_evt,
	       sizeof(struct hci_ev_remote_ext_features)),
	/* [0x2c = HCI_EV_SYNC_CONN_COMPLETE] */
	HCI_EV(HCI_EV_SYNC_CONN_COMPLETE, hci_sync_conn_complete_evt,
	       sizeof(struct hci_ev_sync_conn_complete)),
	/* [0x2f = HCI_EV_EXTENDED_INQUIRY_RESULT] */
	HCI_EV_VL(HCI_EV_EXTENDED_INQUIRY_RESULT,
		  hci_extended_inquiry_result_evt,
		  sizeof(struct hci_ev_ext_inquiry_result), HCI_MAX_EVENT_SIZE),
	/* [0x30 = HCI_EV_KEY_REFRESH_COMPLETE] */
	HCI_EV(HCI_EV_KEY_REFRESH_COMPLETE, hci_key_refresh_complete_evt,
	       sizeof(struct hci_ev_key_refresh_complete)),
	/* [0x31 = HCI_EV_IO_CAPA_REQUEST] */
	HCI_EV(HCI_EV_IO_CAPA_REQUEST, hci_io_capa_request_evt,
	       sizeof(struct hci_ev_io_capa_request)),
	/* [0x32 = HCI_EV_IO_CAPA_REPLY] */
	HCI_EV(HCI_EV_IO_CAPA_REPLY, hci_io_capa_reply_evt,
	       sizeof(struct hci_ev_io_capa_reply)),
	/* [0x33 = HCI_EV_USER_CONFIRM_REQUEST] */
	HCI_EV(HCI_EV_USER_CONFIRM_REQUEST, hci_user_confirm_request_evt,
	       sizeof(struct hci_ev_user_confirm_req)),
	/* [0x34 = HCI_EV_USER_PASSKEY_REQUEST] */
	HCI_EV(HCI_EV_USER_PASSKEY_REQUEST, hci_user_passkey_request_evt,
	       sizeof(struct hci_ev_user_passkey_req)),
	/* [0x35 = HCI_EV_REMOTE_OOB_DATA_REQUEST] */
	HCI_EV(HCI_EV_REMOTE_OOB_DATA_REQUEST, hci_remote_oob_data_request_evt,
	       sizeof(struct hci_ev_remote_oob_data_request)),
	/* [0x36 = HCI_EV_SIMPLE_PAIR_COMPLETE] */
	HCI_EV(HCI_EV_SIMPLE_PAIR_COMPLETE, hci_simple_pair_complete_evt,
	       sizeof(struct hci_ev_simple_pair_complete)),
	/* [0x3b = HCI_EV_USER_PASSKEY_NOTIFY] */
	HCI_EV(HCI_EV_USER_PASSKEY_NOTIFY, hci_user_passkey_notify_evt,
	       sizeof(struct hci_ev_user_passkey_notify)),
	/* [0x3c = HCI_EV_KEYPRESS_NOTIFY] */
	HCI_EV(HCI_EV_KEYPRESS_NOTIFY, hci_keypress_notify_evt,
	       sizeof(struct hci_ev_keypress_notify)),
	/* [0x3d = HCI_EV_REMOTE_HOST_FEATURES] */
	HCI_EV(HCI_EV_REMOTE_HOST_FEATURES, hci_remote_host_features_evt,
	       sizeof(struct hci_ev_remote_host_features)),
	/* [0x3e = HCI_EV_LE_META] */
	HCI_EV_REQ_VL(HCI_EV_LE_META, hci_le_meta_evt,
		      sizeof(struct hci_ev_le_meta), HCI_MAX_EVENT_SIZE),
	/* [0xff = HCI_EV_VENDOR] */
	HCI_EV_VL(HCI_EV_VENDOR, msft_vendor_evt, 0, HCI_MAX_EVENT_SIZE),
};

static void hci_event_func(struct hci_dev *hdev, u8 event, struct sk_buff *skb,
			   u16 *opcode, u8 *status,
			   hci_req_complete_t *req_complete,
			   hci_req_complete_skb_t *req_complete_skb)
{
	const struct hci_ev *ev = &hci_ev_table[event];
	void *data;

	if (!ev->func)
		return;

	if (skb->len < ev->min_len) {
		bt_dev_err(hdev, "unexpected event 0x%2.2x length: %u < %u",
			   event, skb->len, ev->min_len);
		return;
	}

	/* Just warn if the length is over max_len size it still be
	 * possible to partially parse the event so leave to callback to
	 * decide if that is acceptable.
	 */
	if (skb->len > ev->max_len)
		bt_dev_warn_ratelimited(hdev,
					"unexpected event 0x%2.2x length: %u > %u",
					event, skb->len, ev->max_len);

	data = hci_ev_skb_pull(hdev, skb, event, ev->min_len);
	if (!data)
		return;

	if (ev->req)
		ev->func_req(hdev, data, skb, opcode, status, req_complete,
			     req_complete_skb);
	else
		ev->func(hdev, data, skb);
}

void hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *) skb->data;
	hci_req_complete_t req_complete = NULL;
	hci_req_complete_skb_t req_complete_skb = NULL;
	struct sk_buff *orig_skb = NULL;
	u8 status = 0, event, req_evt = 0;
	u16 opcode = HCI_OP_NOP;

	if (skb->len < sizeof(*hdr)) {
		bt_dev_err(hdev, "Malformed HCI Event");
		goto done;
	}

	hci_dev_lock(hdev);
	kfree_skb(hdev->recv_event);
	hdev->recv_event = skb_clone(skb, GFP_KERNEL);
	hci_dev_unlock(hdev);

	event = hdr->evt;
	if (!event) {
		bt_dev_warn(hdev, "Received unexpected HCI Event 0x%2.2x",
			    event);
		goto done;
	}

	/* Only match event if command OGF is not for LE */
	if (hdev->req_skb &&
	    hci_opcode_ogf(hci_skb_opcode(hdev->req_skb)) != 0x08 &&
	    hci_skb_event(hdev->req_skb) == event) {
		hci_req_cmd_complete(hdev, hci_skb_opcode(hdev->req_skb),
				     status, &req_complete, &req_complete_skb);
		req_evt = event;
	}

	/* If it looks like we might end up having to call
	 * req_complete_skb, store a pristine copy of the skb since the
	 * various handlers may modify the original one through
	 * skb_pull() calls, etc.
	 */
	if (req_complete_skb || event == HCI_EV_CMD_STATUS ||
	    event == HCI_EV_CMD_COMPLETE)
		orig_skb = skb_clone(skb, GFP_KERNEL);

	skb_pull(skb, HCI_EVENT_HDR_SIZE);

	/* Store wake reason if we're suspended */
	hci_store_wake_reason(hdev, event, skb);

	bt_dev_dbg(hdev, "event 0x%2.2x", event);

	hci_event_func(hdev, event, skb, &opcode, &status, &req_complete,
		       &req_complete_skb);

	if (req_complete) {
		req_complete(hdev, status, opcode);
	} else if (req_complete_skb) {
		if (!hci_get_cmd_complete(hdev, opcode, req_evt, orig_skb)) {
			kfree_skb(orig_skb);
			orig_skb = NULL;
		}
		req_complete_skb(hdev, status, opcode, orig_skb);
	}

done:
	kfree_skb(orig_skb);
	kfree_skb(skb);
	hdev->stat.evt_rx++;
}
