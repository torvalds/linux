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

/* Bluetooth HCI event handling. */

#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "hci_debugfs.h"
#include "a2mp.h"
#include "amp.h"
#include "smp.h"
#include "msft.h"

#define ZERO_KEY "\x00\x00\x00\x00\x00\x00\x00\x00" \
		 "\x00\x00\x00\x00\x00\x00\x00\x00"

/* Handle HCI Event packets */

static void hci_cc_inquiry_cancel(struct hci_dev *hdev, struct sk_buff *skb,
				  u8 *new_status)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	/* It is possible that we receive Inquiry Complete event right
	 * before we receive Inquiry Cancel Command Complete event, in
	 * which case the latter event should have status of Command
	 * Disallowed (0x0c). This should not be treated as error, since
	 * we actually achieve what Inquiry Cancel wants to achieve,
	 * which is to end the last Inquiry session.
	 */
	if (status == 0x0c && !test_bit(HCI_INQUIRY, &hdev->flags)) {
		bt_dev_warn(hdev, "Ignoring error of Inquiry Cancel command");
		status = 0x00;
	}

	*new_status = status;

	if (status)
		return;

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

	hci_conn_check_pending(hdev);
}

static void hci_cc_periodic_inq(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	hci_dev_set_flag(hdev, HCI_PERIODIC_INQ);
}

static void hci_cc_exit_periodic_inq(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	hci_dev_clear_flag(hdev, HCI_PERIODIC_INQ);

	hci_conn_check_pending(hdev);
}

static void hci_cc_remote_name_req_cancel(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	BT_DBG("%s", hdev->name);
}

static void hci_cc_role_discovery(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_role_discovery *rp = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->role = rp->role;

	hci_dev_unlock(hdev);
}

static void hci_cc_read_link_policy(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_link_policy *rp = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->link_policy = __le16_to_cpu(rp->policy);

	hci_dev_unlock(hdev);
}

static void hci_cc_write_link_policy(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_write_link_policy *rp = (void *) skb->data;
	struct hci_conn *conn;
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_LINK_POLICY);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->link_policy = get_unaligned_le16(sent + 2);

	hci_dev_unlock(hdev);
}

static void hci_cc_read_def_link_policy(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_rp_read_def_link_policy *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->link_policy = __le16_to_cpu(rp->policy);
}

static void hci_cc_write_def_link_policy(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_DEF_LINK_POLICY);
	if (!sent)
		return;

	hdev->link_policy = get_unaligned_le16(sent);
}

static void hci_cc_reset(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	clear_bit(HCI_RESET, &hdev->flags);

	if (status)
		return;

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

	hci_bdaddr_list_clear(&hdev->le_white_list);
	hci_bdaddr_list_clear(&hdev->le_resolv_list);
}

static void hci_cc_read_stored_link_key(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_rp_read_stored_link_key *rp = (void *)skb->data;
	struct hci_cp_read_stored_link_key *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_READ_STORED_LINK_KEY);
	if (!sent)
		return;

	if (!rp->status && sent->read_all == 0x01) {
		hdev->stored_max_keys = rp->max_keys;
		hdev->stored_num_keys = rp->num_keys;
	}
}

static void hci_cc_delete_stored_link_key(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_rp_delete_stored_link_key *rp = (void *)skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (rp->num_keys <= hdev->stored_num_keys)
		hdev->stored_num_keys -= rp->num_keys;
	else
		hdev->stored_num_keys = 0;
}

static void hci_cc_write_local_name(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_LOCAL_NAME);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_set_local_name_complete(hdev, sent, status);
	else if (!status)
		memcpy(hdev->dev_name, sent, HCI_MAX_NAME_LENGTH);

	hci_dev_unlock(hdev);
}

static void hci_cc_read_local_name(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_local_name *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG))
		memcpy(hdev->dev_name, rp->name, HCI_MAX_NAME_LENGTH);
}

static void hci_cc_write_auth_enable(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_AUTH_ENABLE);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	if (!status) {
		__u8 param = *((__u8 *) sent);

		if (param == AUTH_ENABLED)
			set_bit(HCI_AUTH, &hdev->flags);
		else
			clear_bit(HCI_AUTH, &hdev->flags);
	}

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_auth_enable_complete(hdev, status);

	hci_dev_unlock(hdev);
}

static void hci_cc_write_encrypt_mode(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	__u8 param;
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_ENCRYPT_MODE);
	if (!sent)
		return;

	param = *((__u8 *) sent);

	if (param)
		set_bit(HCI_ENCRYPT, &hdev->flags);
	else
		clear_bit(HCI_ENCRYPT, &hdev->flags);
}

static void hci_cc_write_scan_enable(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	__u8 param;
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SCAN_ENABLE);
	if (!sent)
		return;

	param = *((__u8 *) sent);

	hci_dev_lock(hdev);

	if (status) {
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
}

static void hci_cc_set_event_filter(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *)skb->data);
	struct hci_cp_set_event_filter *cp;
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_SET_EVENT_FLT);
	if (!sent)
		return;

	cp = (struct hci_cp_set_event_filter *)sent;

	if (cp->flt_type == HCI_FLT_CLEAR_ALL)
		hci_dev_clear_flag(hdev, HCI_EVENT_FILTER_CONFIGURED);
	else
		hci_dev_set_flag(hdev, HCI_EVENT_FILTER_CONFIGURED);
}

static void hci_cc_read_class_of_dev(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_class_of_dev *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	memcpy(hdev->dev_class, rp->dev_class, 3);

	BT_DBG("%s class 0x%.2x%.2x%.2x", hdev->name,
	       hdev->dev_class[2], hdev->dev_class[1], hdev->dev_class[0]);
}

static void hci_cc_write_class_of_dev(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_CLASS_OF_DEV);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	if (status == 0)
		memcpy(hdev->dev_class, sent, 3);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_set_class_of_dev_complete(hdev, sent, status);

	hci_dev_unlock(hdev);
}

static void hci_cc_read_voice_setting(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_voice_setting *rp = (void *) skb->data;
	__u16 setting;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	setting = __le16_to_cpu(rp->voice_setting);

	if (hdev->voice_setting == setting)
		return;

	hdev->voice_setting = setting;

	BT_DBG("%s voice setting 0x%4.4x", hdev->name, setting);

	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);
}

static void hci_cc_write_voice_setting(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	__u16 setting;
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_VOICE_SETTING);
	if (!sent)
		return;

	setting = get_unaligned_le16(sent);

	if (hdev->voice_setting == setting)
		return;

	hdev->voice_setting = setting;

	BT_DBG("%s voice setting 0x%4.4x", hdev->name, setting);

	if (hdev->notify)
		hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);
}

static void hci_cc_read_num_supported_iac(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_rp_read_num_supported_iac *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->num_iac = rp->num_iac;

	BT_DBG("%s num iac %d", hdev->name, hdev->num_iac);
}

static void hci_cc_write_ssp_mode(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	struct hci_cp_write_ssp_mode *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SSP_MODE);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	if (!status) {
		if (sent->mode)
			hdev->features[1][0] |= LMP_HOST_SSP;
		else
			hdev->features[1][0] &= ~LMP_HOST_SSP;
	}

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_ssp_enable_complete(hdev, sent->mode, status);
	else if (!status) {
		if (sent->mode)
			hci_dev_set_flag(hdev, HCI_SSP_ENABLED);
		else
			hci_dev_clear_flag(hdev, HCI_SSP_ENABLED);
	}

	hci_dev_unlock(hdev);
}

static void hci_cc_write_sc_support(struct hci_dev *hdev, struct sk_buff *skb)
{
	u8 status = *((u8 *) skb->data);
	struct hci_cp_write_sc_support *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SC_SUPPORT);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	if (!status) {
		if (sent->support)
			hdev->features[1][0] |= LMP_HOST_SC;
		else
			hdev->features[1][0] &= ~LMP_HOST_SC;
	}

	if (!hci_dev_test_flag(hdev, HCI_MGMT) && !status) {
		if (sent->support)
			hci_dev_set_flag(hdev, HCI_SC_ENABLED);
		else
			hci_dev_clear_flag(hdev, HCI_SC_ENABLED);
	}

	hci_dev_unlock(hdev);
}

static void hci_cc_read_local_version(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_local_version *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG)) {
		hdev->hci_ver = rp->hci_ver;
		hdev->hci_rev = __le16_to_cpu(rp->hci_rev);
		hdev->lmp_ver = rp->lmp_ver;
		hdev->manufacturer = __le16_to_cpu(rp->manufacturer);
		hdev->lmp_subver = __le16_to_cpu(rp->lmp_subver);
	}
}

static void hci_cc_read_local_commands(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	struct hci_rp_read_local_commands *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG))
		memcpy(hdev->commands, rp->commands, sizeof(hdev->commands));
}

static void hci_cc_read_auth_payload_timeout(struct hci_dev *hdev,
					     struct sk_buff *skb)
{
	struct hci_rp_read_auth_payload_to *rp = (void *)skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->auth_payload_timeout = __le16_to_cpu(rp->timeout);

	hci_dev_unlock(hdev);
}

static void hci_cc_write_auth_payload_timeout(struct hci_dev *hdev,
					      struct sk_buff *skb)
{
	struct hci_rp_write_auth_payload_to *rp = (void *)skb->data;
	struct hci_conn *conn;
	void *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_AUTH_PAYLOAD_TO);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->auth_payload_timeout = get_unaligned_le16(sent + 2);

	hci_dev_unlock(hdev);
}

static void hci_cc_read_local_features(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	struct hci_rp_read_local_features *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

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
}

static void hci_cc_read_local_ext_features(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_rp_read_local_ext_features *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (hdev->max_page < rp->max_page)
		hdev->max_page = rp->max_page;

	if (rp->page < HCI_MAX_PAGES)
		memcpy(hdev->features[rp->page], rp->features, 8);
}

static void hci_cc_read_flow_control_mode(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_rp_read_flow_control_mode *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->flow_ctl_mode = rp->mode;
}

static void hci_cc_read_buffer_size(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_buffer_size *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->acl_mtu  = __le16_to_cpu(rp->acl_mtu);
	hdev->sco_mtu  = rp->sco_mtu;
	hdev->acl_pkts = __le16_to_cpu(rp->acl_max_pkt);
	hdev->sco_pkts = __le16_to_cpu(rp->sco_max_pkt);

	if (test_bit(HCI_QUIRK_FIXUP_BUFFER_SIZE, &hdev->quirks)) {
		hdev->sco_mtu  = 64;
		hdev->sco_pkts = 8;
	}

	hdev->acl_cnt = hdev->acl_pkts;
	hdev->sco_cnt = hdev->sco_pkts;

	BT_DBG("%s acl mtu %d:%d sco mtu %d:%d", hdev->name, hdev->acl_mtu,
	       hdev->acl_pkts, hdev->sco_mtu, hdev->sco_pkts);
}

static void hci_cc_read_bd_addr(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_bd_addr *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (test_bit(HCI_INIT, &hdev->flags))
		bacpy(&hdev->bdaddr, &rp->bdaddr);

	if (hci_dev_test_flag(hdev, HCI_SETUP))
		bacpy(&hdev->setup_addr, &rp->bdaddr);
}

static void hci_cc_read_local_pairing_opts(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_rp_read_local_pairing_opts *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (hci_dev_test_flag(hdev, HCI_SETUP) ||
	    hci_dev_test_flag(hdev, HCI_CONFIG)) {
		hdev->pairing_opts = rp->pairing_opts;
		hdev->max_enc_key_size = rp->max_key_size;
	}
}

static void hci_cc_read_page_scan_activity(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_rp_read_page_scan_activity *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (test_bit(HCI_INIT, &hdev->flags)) {
		hdev->page_scan_interval = __le16_to_cpu(rp->interval);
		hdev->page_scan_window = __le16_to_cpu(rp->window);
	}
}

static void hci_cc_write_page_scan_activity(struct hci_dev *hdev,
					    struct sk_buff *skb)
{
	u8 status = *((u8 *) skb->data);
	struct hci_cp_write_page_scan_activity *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_PAGE_SCAN_ACTIVITY);
	if (!sent)
		return;

	hdev->page_scan_interval = __le16_to_cpu(sent->interval);
	hdev->page_scan_window = __le16_to_cpu(sent->window);
}

static void hci_cc_read_page_scan_type(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_rp_read_page_scan_type *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	if (test_bit(HCI_INIT, &hdev->flags))
		hdev->page_scan_type = rp->type;
}

static void hci_cc_write_page_scan_type(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	u8 status = *((u8 *) skb->data);
	u8 *type;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	type = hci_sent_cmd_data(hdev, HCI_OP_WRITE_PAGE_SCAN_TYPE);
	if (type)
		hdev->page_scan_type = *type;
}

static void hci_cc_read_data_block_size(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_rp_read_data_block_size *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->block_mtu = __le16_to_cpu(rp->max_acl_len);
	hdev->block_len = __le16_to_cpu(rp->block_len);
	hdev->num_blocks = __le16_to_cpu(rp->num_blocks);

	hdev->block_cnt = hdev->num_blocks;

	BT_DBG("%s blk mtu %d cnt %d len %d", hdev->name, hdev->block_mtu,
	       hdev->block_cnt, hdev->block_len);
}

static void hci_cc_read_clock(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_clock *rp = (void *) skb->data;
	struct hci_cp_read_clock *cp;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	if (skb->len < sizeof(*rp))
		return;

	if (rp->status)
		return;

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
}

static void hci_cc_read_local_amp_info(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	struct hci_rp_read_local_amp_info *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->amp_status = rp->amp_status;
	hdev->amp_total_bw = __le32_to_cpu(rp->total_bw);
	hdev->amp_max_bw = __le32_to_cpu(rp->max_bw);
	hdev->amp_min_latency = __le32_to_cpu(rp->min_latency);
	hdev->amp_max_pdu = __le32_to_cpu(rp->max_pdu);
	hdev->amp_type = rp->amp_type;
	hdev->amp_pal_cap = __le16_to_cpu(rp->pal_cap);
	hdev->amp_assoc_size = __le16_to_cpu(rp->max_assoc_size);
	hdev->amp_be_flush_to = __le32_to_cpu(rp->be_flush_to);
	hdev->amp_max_flush_to = __le32_to_cpu(rp->max_flush_to);
}

static void hci_cc_read_inq_rsp_tx_power(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_rp_read_inq_rsp_tx_power *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->inq_tx_power = rp->tx_power;
}

static void hci_cc_read_def_err_data_reporting(struct hci_dev *hdev,
					       struct sk_buff *skb)
{
	struct hci_rp_read_def_err_data_reporting *rp = (void *)skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->err_data_reporting = rp->err_data_reporting;
}

static void hci_cc_write_def_err_data_reporting(struct hci_dev *hdev,
						struct sk_buff *skb)
{
	__u8 status = *((__u8 *)skb->data);
	struct hci_cp_write_def_err_data_reporting *cp;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_WRITE_DEF_ERR_DATA_REPORTING);
	if (!cp)
		return;

	hdev->err_data_reporting = cp->err_data_reporting;
}

static void hci_cc_pin_code_reply(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_pin_code_reply *rp = (void *) skb->data;
	struct hci_cp_pin_code_reply *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

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
}

static void hci_cc_pin_code_neg_reply(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_pin_code_neg_reply *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_pin_code_neg_reply_complete(hdev, &rp->bdaddr,
						 rp->status);

	hci_dev_unlock(hdev);
}

static void hci_cc_le_read_buffer_size(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	struct hci_rp_le_read_buffer_size *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->le_mtu = __le16_to_cpu(rp->le_mtu);
	hdev->le_pkts = rp->le_max_pkt;

	hdev->le_cnt = hdev->le_pkts;

	BT_DBG("%s le mtu %d:%d", hdev->name, hdev->le_mtu, hdev->le_pkts);
}

static void hci_cc_le_read_local_features(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_rp_le_read_local_features *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	memcpy(hdev->le_features, rp->features, 8);
}

static void hci_cc_le_read_adv_tx_power(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_rp_le_read_adv_tx_power *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->adv_tx_power = rp->tx_power;
}

static void hci_cc_user_confirm_reply(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_confirm_reply_complete(hdev, &rp->bdaddr, ACL_LINK, 0,
						 rp->status);

	hci_dev_unlock(hdev);
}

static void hci_cc_user_confirm_neg_reply(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_confirm_neg_reply_complete(hdev, &rp->bdaddr,
						     ACL_LINK, 0, rp->status);

	hci_dev_unlock(hdev);
}

static void hci_cc_user_passkey_reply(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_reply_complete(hdev, &rp->bdaddr, ACL_LINK,
						 0, rp->status);

	hci_dev_unlock(hdev);
}

static void hci_cc_user_passkey_neg_reply(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_rp_user_confirm_reply *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	hci_dev_lock(hdev);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_neg_reply_complete(hdev, &rp->bdaddr,
						     ACL_LINK, 0, rp->status);

	hci_dev_unlock(hdev);
}

static void hci_cc_read_local_oob_data(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	struct hci_rp_read_local_oob_data *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);
}

static void hci_cc_read_local_oob_ext_data(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_rp_read_local_oob_ext_data *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);
}

static void hci_cc_le_set_random_addr(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	bdaddr_t *sent;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_RANDOM_ADDR);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	bacpy(&hdev->random_addr, sent);

	hci_dev_unlock(hdev);
}

static void hci_cc_le_set_default_phy(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	struct hci_cp_le_set_default_phy *cp;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_DEFAULT_PHY);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	hdev->le_tx_def_phys = cp->tx_phys;
	hdev->le_rx_def_phys = cp->rx_phys;

	hci_dev_unlock(hdev);
}

static void hci_cc_le_set_adv_set_random_addr(struct hci_dev *hdev,
                                              struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	struct hci_cp_le_set_adv_set_rand_addr *cp;
	struct adv_info *adv_instance;

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADV_SET_RAND_ADDR);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	if (!hdev->cur_adv_instance) {
		/* Store in hdev for instance 0 (Set adv and Directed advs) */
		bacpy(&hdev->random_addr, &cp->bdaddr);
	} else {
		adv_instance = hci_find_adv_instance(hdev,
						     hdev->cur_adv_instance);
		if (adv_instance)
			bacpy(&adv_instance->random_addr, &cp->bdaddr);
	}

	hci_dev_unlock(hdev);
}

static void hci_cc_le_read_transmit_power(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_rp_le_read_transmit_power *rp = (void *)skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->min_le_tx_power = rp->min_le_tx_power;
	hdev->max_le_tx_power = rp->max_le_tx_power;
}

static void hci_cc_le_set_adv_enable(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 *sent, status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADV_ENABLE);
	if (!sent)
		return;

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
}

static void hci_cc_le_set_ext_adv_enable(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_cp_le_set_ext_adv_enable *cp;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_EXT_ADV_ENABLE);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	if (cp->enable) {
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
}

static void hci_cc_le_set_scan_param(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_cp_le_set_scan_param *cp;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_SCAN_PARAM);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	hdev->le_scan_type = cp->type;

	hci_dev_unlock(hdev);
}

static void hci_cc_le_set_ext_scan_param(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_cp_le_set_ext_scan_params *cp;
	__u8 status = *((__u8 *) skb->data);
	struct hci_cp_le_scan_phy_params *phy_param;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_EXT_SCAN_PARAMS);
	if (!cp)
		return;

	phy_param = (void *)cp->data;

	hci_dev_lock(hdev);

	hdev->le_scan_type = phy_param->type;

	hci_dev_unlock(hdev);
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

	if (len > HCI_MAX_AD_LENGTH)
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
		if (hdev->le_scan_type == LE_SCAN_ACTIVE)
			clear_pending_adv_report(hdev);
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
					  d->last_adv_data_len, NULL, 0);
		}

		/* Cancel this timer so that we don't try to disable scanning
		 * when it's already disabled.
		 */
		cancel_delayed_work(&hdev->le_scan_disable);

		hci_dev_clear_flag(hdev, HCI_LE_SCAN);

		/* The HCI_LE_SCAN_INTERRUPTED flag indicates that we
		 * interrupted scanning due to a connect request. Mark
		 * therefore discovery as stopped. If this was not
		 * because of a connect request advertising might have
		 * been disabled because of active scanning, so
		 * re-enable it again if necessary.
		 */
		if (hci_dev_test_and_clear_flag(hdev, HCI_LE_SCAN_INTERRUPTED))
			hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
		else if (!hci_dev_test_flag(hdev, HCI_LE_ADV) &&
			 hdev->discovery.state == DISCOVERY_FINDING)
			hci_req_reenable_advertising(hdev);

		break;

	default:
		bt_dev_err(hdev, "use of reserved LE_Scan_Enable param %d",
			   enable);
		break;
	}

	hci_dev_unlock(hdev);
}

static void hci_cc_le_set_scan_enable(struct hci_dev *hdev,
				      struct sk_buff *skb)
{
	struct hci_cp_le_set_scan_enable *cp;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_SCAN_ENABLE);
	if (!cp)
		return;

	le_set_scan_enable_complete(hdev, cp->enable);
}

static void hci_cc_le_set_ext_scan_enable(struct hci_dev *hdev,
				      struct sk_buff *skb)
{
	struct hci_cp_le_set_ext_scan_enable *cp;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_EXT_SCAN_ENABLE);
	if (!cp)
		return;

	le_set_scan_enable_complete(hdev, cp->enable);
}

static void hci_cc_le_read_num_adv_sets(struct hci_dev *hdev,
				      struct sk_buff *skb)
{
	struct hci_rp_le_read_num_supported_adv_sets *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x No of Adv sets %u", hdev->name, rp->status,
	       rp->num_of_sets);

	if (rp->status)
		return;

	hdev->le_num_of_adv_sets = rp->num_of_sets;
}

static void hci_cc_le_read_white_list_size(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_rp_le_read_white_list_size *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x size %u", hdev->name, rp->status, rp->size);

	if (rp->status)
		return;

	hdev->le_white_list_size = rp->size;
}

static void hci_cc_le_clear_white_list(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	hci_bdaddr_list_clear(&hdev->le_white_list);
}

static void hci_cc_le_add_to_white_list(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_cp_le_add_to_white_list *sent;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_ADD_TO_WHITE_LIST);
	if (!sent)
		return;

	hci_bdaddr_list_add(&hdev->le_white_list, &sent->bdaddr,
			   sent->bdaddr_type);
}

static void hci_cc_le_del_from_white_list(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_cp_le_del_from_white_list *sent;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_DEL_FROM_WHITE_LIST);
	if (!sent)
		return;

	hci_bdaddr_list_del(&hdev->le_white_list, &sent->bdaddr,
			    sent->bdaddr_type);
}

static void hci_cc_le_read_supported_states(struct hci_dev *hdev,
					    struct sk_buff *skb)
{
	struct hci_rp_le_read_supported_states *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	memcpy(hdev->le_states, rp->le_states, 8);
}

static void hci_cc_le_read_def_data_len(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_rp_le_read_def_data_len *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->le_def_tx_len = le16_to_cpu(rp->tx_len);
	hdev->le_def_tx_time = le16_to_cpu(rp->tx_time);
}

static void hci_cc_le_write_def_data_len(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_cp_le_write_def_data_len *sent;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_WRITE_DEF_DATA_LEN);
	if (!sent)
		return;

	hdev->le_def_tx_len = le16_to_cpu(sent->tx_len);
	hdev->le_def_tx_time = le16_to_cpu(sent->tx_time);
}

static void hci_cc_le_add_to_resolv_list(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_cp_le_add_to_resolv_list *sent;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_ADD_TO_RESOLV_LIST);
	if (!sent)
		return;

	hci_bdaddr_list_add_with_irk(&hdev->le_resolv_list, &sent->bdaddr,
				sent->bdaddr_type, sent->peer_irk,
				sent->local_irk);
}

static void hci_cc_le_del_from_resolv_list(struct hci_dev *hdev,
					  struct sk_buff *skb)
{
	struct hci_cp_le_del_from_resolv_list *sent;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_DEL_FROM_RESOLV_LIST);
	if (!sent)
		return;

	hci_bdaddr_list_del_with_irk(&hdev->le_resolv_list, &sent->bdaddr,
			    sent->bdaddr_type);
}

static void hci_cc_le_clear_resolv_list(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	hci_bdaddr_list_clear(&hdev->le_resolv_list);
}

static void hci_cc_le_read_resolv_list_size(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_rp_le_read_resolv_list_size *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x size %u", hdev->name, rp->status, rp->size);

	if (rp->status)
		return;

	hdev->le_resolv_list_size = rp->size;
}

static void hci_cc_le_set_addr_resolution_enable(struct hci_dev *hdev,
						struct sk_buff *skb)
{
	__u8 *sent, status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADDR_RESOLV_ENABLE);
	if (!sent)
		return;

	hci_dev_lock(hdev);

	if (*sent)
		hci_dev_set_flag(hdev, HCI_LL_RPA_RESOLUTION);
	else
		hci_dev_clear_flag(hdev, HCI_LL_RPA_RESOLUTION);

	hci_dev_unlock(hdev);
}

static void hci_cc_le_read_max_data_len(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_rp_le_read_max_data_len *rp = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->le_max_tx_len = le16_to_cpu(rp->tx_len);
	hdev->le_max_tx_time = le16_to_cpu(rp->tx_time);
	hdev->le_max_rx_len = le16_to_cpu(rp->rx_len);
	hdev->le_max_rx_time = le16_to_cpu(rp->rx_time);
}

static void hci_cc_write_le_host_supported(struct hci_dev *hdev,
					   struct sk_buff *skb)
{
	struct hci_cp_write_le_host_supported *sent;
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_LE_HOST_SUPPORTED);
	if (!sent)
		return;

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
}

static void hci_cc_set_adv_param(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_cp_le_set_adv_param *cp;
	u8 status = *((u8 *) skb->data);

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_ADV_PARAM);
	if (!cp)
		return;

	hci_dev_lock(hdev);
	hdev->adv_addr_type = cp->own_address_type;
	hci_dev_unlock(hdev);
}

static void hci_cc_set_ext_adv_param(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_le_set_ext_adv_params *rp = (void *) skb->data;
	struct hci_cp_le_set_ext_adv_params *cp;
	struct adv_info *adv_instance;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_LE_SET_EXT_ADV_PARAMS);
	if (!cp)
		return;

	hci_dev_lock(hdev);
	hdev->adv_addr_type = cp->own_addr_type;
	if (!hdev->cur_adv_instance) {
		/* Store in hdev for instance 0 */
		hdev->adv_tx_power = rp->tx_power;
	} else {
		adv_instance = hci_find_adv_instance(hdev,
						     hdev->cur_adv_instance);
		if (adv_instance)
			adv_instance->tx_power = rp->tx_power;
	}
	/* Update adv data as tx power is known now */
	hci_req_update_adv_data(hdev, hdev->cur_adv_instance);

	hci_dev_unlock(hdev);
}

static void hci_cc_read_rssi(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_rssi *rp = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn)
		conn->rssi = rp->rssi;

	hci_dev_unlock(hdev);
}

static void hci_cc_read_tx_power(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_cp_read_tx_power *sent;
	struct hci_rp_read_tx_power *rp = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, rp->status);

	if (rp->status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_READ_TX_POWER);
	if (!sent)
		return;

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
}

static void hci_cc_write_ssp_debug_mode(struct hci_dev *hdev, struct sk_buff *skb)
{
	u8 status = *((u8 *) skb->data);
	u8 *mode;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status)
		return;

	mode = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SSP_DEBUG_MODE);
	if (mode)
		hdev->ssp_debug_mode = *mode;
}

static void hci_cs_inquiry(struct hci_dev *hdev, __u8 status)
{
	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (status) {
		hci_conn_check_pending(hdev);
		return;
	}

	set_bit(HCI_INQUIRY, &hdev->flags);
}

static void hci_cs_create_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_create_conn *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	cp = hci_sent_cmd_data(hdev, HCI_OP_CREATE_CONN);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);

	BT_DBG("%s bdaddr %pMR hcon %p", hdev->name, &cp->bdaddr, conn);

	if (status) {
		if (conn && conn->state == BT_CONNECT) {
			if (status != 0x0c || conn->attempt > 2) {
				conn->state = BT_CLOSED;
				hci_connect_cfm(conn, status);
				hci_conn_del(conn);
			} else
				conn->state = BT_CONNECT2;
		}
	} else {
		if (!conn) {
			conn = hci_conn_add(hdev, ACL_LINK, &cp->bdaddr,
					    HCI_ROLE_MASTER);
			if (!conn)
				bt_dev_err(hdev, "no memory for new connection");
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_add_sco(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_add_sco *cp;
	struct hci_conn *acl, *sco;
	__u16 handle;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_ADD_SCO);
	if (!cp)
		return;

	handle = __le16_to_cpu(cp->handle);

	BT_DBG("%s handle 0x%4.4x", hdev->name, handle);

	hci_dev_lock(hdev);

	acl = hci_conn_hash_lookup_handle(hdev, handle);
	if (acl) {
		sco = acl->link;
		if (sco) {
			sco->state = BT_CLOSED;

			hci_connect_cfm(sco, status);
			hci_conn_del(sco);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_auth_requested(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_auth_requested *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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
	if (conn &&
	    (conn->state == BT_CONFIG || conn->state == BT_CONNECTED) &&
	    !test_and_set_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags))
		mgmt_device_connected(hdev, conn, 0, name, name_len);

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
	if (name) {
		e->name_state = NAME_KNOWN;
		mgmt_remote_name(hdev, bdaddr, ACL_LINK, 0x00,
				 e->data.rssi, name, name_len);
	} else {
		e->name_state = NAME_NOT_KNOWN;
	}

	if (hci_resolve_next_name(hdev))
		return;

discov_complete:
	hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
}

static void hci_cs_remote_name_req(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_remote_name_req *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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

static void hci_cs_setup_sync_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_setup_sync_conn *cp;
	struct hci_conn *acl, *sco;
	__u16 handle;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SETUP_SYNC_CONN);
	if (!cp)
		return;

	handle = __le16_to_cpu(cp->handle);

	BT_DBG("%s handle 0x%4.4x", hdev->name, handle);

	hci_dev_lock(hdev);

	acl = hci_conn_hash_lookup_handle(hdev, handle);
	if (acl) {
		sco = acl->link;
		if (sco) {
			sco->state = BT_CLOSED;

			hci_connect_cfm(sco, status);
			hci_conn_del(sco);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_sniff_mode(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_sniff_mode *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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
	struct hci_conn *conn;

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_DISCONNECT);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		u8 type = conn->type;

		mgmt_disconnect_failed(hdev, &conn->dst, conn->type,
				       conn->dst_type, status);

		/* If the disconnection failed for any reason, the upper layer
		 * does not retry to disconnect in current implementation.
		 * Hence, we need to do some basic cleanup here and re-enable
		 * advertising if necessary.
		 */
		hci_conn_del(conn);
		if (type == LE_LINK)
			hci_req_reenable_advertising(hdev);
	}

	hci_dev_unlock(hdev);
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

	/* When using controller based address resolution, then the new
	 * address types 0x02 and 0x03 are used. These types need to be
	 * converted back into either public address or random address type
	 */
	if (use_ll_privacy(hdev) &&
	    hci_dev_test_flag(hdev, HCI_LL_RPA_RESOLUTION)) {
		switch (own_address_type) {
		case ADDR_LE_DEV_PUBLIC_RESOLVED:
			own_address_type = ADDR_LE_DEV_PUBLIC;
			break;
		case ADDR_LE_DEV_RANDOM_RESOLVED:
			own_address_type = ADDR_LE_DEV_RANDOM;
			break;
		}
	}

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

	/* We don't want the connection attempt to stick around
	 * indefinitely since LE doesn't have a page timeout concept
	 * like BR/EDR. Set a timer for any connection that doesn't use
	 * the white list for connecting.
	 */
	if (filter_policy == HCI_LE_USE_PEER_ADDR)
		queue_delayed_work(conn->hdev->workqueue,
				   &conn->le_conn_timeout,
				   conn->conn_timeout);
}

static void hci_cs_le_create_conn(struct hci_dev *hdev, u8 status)
{
	struct hci_cp_le_create_conn *cp;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	/* All connection failure handling is taken care of by the
	 * hci_le_conn_failed function which is triggered by the HCI
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

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	/* All connection failure handling is taken care of by the
	 * hci_le_conn_failed function which is triggered by the HCI
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

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

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

static void hci_inquiry_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	struct discovery_state *discov = &hdev->discovery;
	struct inquiry_entry *e;

	BT_DBG("%s status 0x%2.2x", hdev->name, status);

	hci_conn_check_pending(hdev);

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
		    !test_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks))
			hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
		goto unlock;
	}

	e = hci_inquiry_cache_lookup_resolve(hdev, BDADDR_ANY, NAME_NEEDED);
	if (e && hci_resolve_name(hdev, e) == 0) {
		e->name_state = NAME_PENDING;
		hci_discovery_set_state(hdev, DISCOVERY_RESOLVING);
	} else {
		/* When BR/EDR inquiry is active and no LE scanning is in
		 * progress, then change discovery state to indicate completion.
		 *
		 * When running LE scanning and BR/EDR inquiry simultaneously
		 * and the LE scan already finished, then change the discovery
		 * state to indicate completion.
		 */
		if (!hci_dev_test_flag(hdev, HCI_LE_SCAN) ||
		    !test_bit(HCI_QUIRK_SIMULTANEOUS_DISCOVERY, &hdev->quirks))
			hci_discovery_set_state(hdev, DISCOVERY_STOPPED);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_inquiry_result_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct inquiry_data data;
	struct inquiry_info *info = (void *) (skb->data + 1);
	int num_rsp = *((__u8 *) skb->data);

	BT_DBG("%s num_rsp %d", hdev->name, num_rsp);

	if (!num_rsp || skb->len < num_rsp * sizeof(*info) + 1)
		return;

	if (hci_dev_test_flag(hdev, HCI_PERIODIC_INQ))
		return;

	hci_dev_lock(hdev);

	for (; num_rsp; num_rsp--, info++) {
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
				  flags, NULL, 0, NULL, 0);
	}

	hci_dev_unlock(hdev);
}

static void hci_conn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_conn_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
	if (!conn) {
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
		    hci_bdaddr_list_lookup_with_flags(&hdev->whitelist,
						      &ev->bdaddr,
						      BDADDR_BREDR)) {
			conn = hci_conn_add(hdev, ev->link_type, &ev->bdaddr,
					    HCI_ROLE_SLAVE);
			if (!conn) {
				bt_dev_err(hdev, "no memory for new conn");
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

	if (!ev->status) {
		conn->handle = __le16_to_cpu(ev->handle);

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

		/* Get remote features */
		if (conn->type == ACL_LINK) {
			struct hci_cp_read_remote_features cp;
			cp.handle = ev->handle;
			hci_send_cmd(hdev, HCI_OP_READ_REMOTE_FEATURES,
				     sizeof(cp), &cp);

			hci_req_update_scan(hdev);
		}

		/* Set packet type for incoming connection */
		if (!conn->out && hdev->hci_ver < BLUETOOTH_VER_2_0) {
			struct hci_cp_change_conn_ptype cp;
			cp.handle = ev->handle;
			cp.pkt_type = cpu_to_le16(conn->pkt_type);
			hci_send_cmd(hdev, HCI_OP_CHANGE_CONN_PTYPE, sizeof(cp),
				     &cp);
		}
	} else {
		conn->state = BT_CLOSED;
		if (conn->type == ACL_LINK)
			mgmt_connect_failed(hdev, &conn->dst, conn->type,
					    conn->dst_type, ev->status);
	}

	if (conn->type == ACL_LINK)
		hci_sco_setup(conn, ev->status);

	if (ev->status) {
		hci_connect_cfm(conn, ev->status);
		hci_conn_del(conn);
	} else if (ev->link_type == SCO_LINK) {
		switch (conn->setting & SCO_AIRMODE_MASK) {
		case SCO_AIRMODE_CVSD:
			if (hdev->notify)
				hdev->notify(hdev, HCI_NOTIFY_ENABLE_SCO_CVSD);
			break;
		}

		hci_connect_cfm(conn, ev->status);
	}

unlock:
	hci_dev_unlock(hdev);

	hci_conn_check_pending(hdev);
}

static void hci_reject_conn(struct hci_dev *hdev, bdaddr_t *bdaddr)
{
	struct hci_cp_reject_conn_req cp;

	bacpy(&cp.bdaddr, bdaddr);
	cp.reason = HCI_ERROR_REJ_BAD_ADDR;
	hci_send_cmd(hdev, HCI_OP_REJECT_CONN_REQ, sizeof(cp), &cp);
}

static void hci_conn_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_conn_request *ev = (void *) skb->data;
	int mask = hdev->link_mode;
	struct inquiry_entry *ie;
	struct hci_conn *conn;
	__u8 flags = 0;

	BT_DBG("%s bdaddr %pMR type 0x%x", hdev->name, &ev->bdaddr,
	       ev->link_type);

	mask |= hci_proto_connect_ind(hdev, &ev->bdaddr, ev->link_type,
				      &flags);

	if (!(mask & HCI_LM_ACCEPT)) {
		hci_reject_conn(hdev, &ev->bdaddr);
		return;
	}

	if (hci_bdaddr_list_lookup(&hdev->blacklist, &ev->bdaddr,
				   BDADDR_BREDR)) {
		hci_reject_conn(hdev, &ev->bdaddr);
		return;
	}

	/* Require HCI_CONNECTABLE or a whitelist entry to accept the
	 * connection. These features are only touched through mgmt so
	 * only do the checks if HCI_MGMT is set.
	 */
	if (hci_dev_test_flag(hdev, HCI_MGMT) &&
	    !hci_dev_test_flag(hdev, HCI_CONNECTABLE) &&
	    !hci_bdaddr_list_lookup_with_flags(&hdev->whitelist, &ev->bdaddr,
					       BDADDR_BREDR)) {
		hci_reject_conn(hdev, &ev->bdaddr);
		return;
	}

	/* Connection accepted */

	hci_dev_lock(hdev);

	ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
	if (ie)
		memcpy(ie->data.dev_class, ev->dev_class, 3);

	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type,
			&ev->bdaddr);
	if (!conn) {
		conn = hci_conn_add(hdev, ev->link_type, &ev->bdaddr,
				    HCI_ROLE_SLAVE);
		if (!conn) {
			bt_dev_err(hdev, "no memory for new connection");
			hci_dev_unlock(hdev);
			return;
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
			cp.role = 0x00; /* Become master */
		else
			cp.role = 0x01; /* Remain slave */

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

static void hci_disconn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_disconn_complete *ev = (void *) skb->data;
	u8 reason;
	struct hci_conn_params *params;
	struct hci_conn *conn;
	bool mgmt_connected;
	u8 type;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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
		if (test_bit(HCI_CONN_FLUSH_KEY, &conn->flags))
			hci_remove_link_key(hdev, &conn->dst);

		hci_req_update_scan(hdev);
	}

	params = hci_conn_params_lookup(hdev, &conn->dst, conn->dst_type);
	if (params) {
		switch (params->auto_connect) {
		case HCI_AUTO_CONN_LINK_LOSS:
			if (ev->reason != HCI_ERROR_CONNECTION_TIMEOUT)
				break;
			fallthrough;

		case HCI_AUTO_CONN_DIRECT:
		case HCI_AUTO_CONN_ALWAYS:
			list_del_init(&params->action);
			list_add(&params->action, &hdev->pend_le_conns);
			hci_update_background_scan(hdev);
			break;

		default:
			break;
		}
	}

	type = conn->type;

	hci_disconn_cfm(conn, ev->reason);
	hci_conn_del(conn);

	/* The suspend notifier is waiting for all devices to disconnect so
	 * clear the bit from pending tasks and inform the wait queue.
	 */
	if (list_empty(&hdev->conn_hash.list) &&
	    test_and_clear_bit(SUSPEND_DISCONNECTING, hdev->suspend_tasks)) {
		wake_up(&hdev->suspend_wait_q);
	}

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
	if (type == LE_LINK)
		hci_req_reenable_advertising(hdev);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_auth_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_auth_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (!ev->status) {
		clear_bit(HCI_CONN_AUTH_FAILURE, &conn->flags);

		if (!hci_conn_ssp_enabled(conn) &&
		    test_bit(HCI_CONN_REAUTH_PEND, &conn->flags)) {
			bt_dev_info(hdev, "re-auth of legacy device is not possible.");
		} else {
			set_bit(HCI_CONN_AUTH, &conn->flags);
			conn->sec_level = conn->pending_sec_level;
		}
	} else {
		if (ev->status == HCI_ERROR_PIN_OR_KEY_MISSING)
			set_bit(HCI_CONN_AUTH_FAILURE, &conn->flags);

		mgmt_auth_failed(conn, ev->status);
	}

	clear_bit(HCI_CONN_AUTH_PEND, &conn->flags);
	clear_bit(HCI_CONN_REAUTH_PEND, &conn->flags);

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

static void hci_remote_name_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_remote_name *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_conn_check_pending(hdev);

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

static void read_enc_key_size_complete(struct hci_dev *hdev, u8 status,
				       u16 opcode, struct sk_buff *skb)
{
	const struct hci_rp_read_enc_key_size *rp;
	struct hci_conn *conn;
	u16 handle;

	BT_DBG("%s status 0x%02x", hdev->name, status);

	if (!skb || skb->len < sizeof(*rp)) {
		bt_dev_err(hdev, "invalid read key size response");
		return;
	}

	rp = (void *)skb->data;
	handle = le16_to_cpu(rp->handle);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, handle);
	if (!conn)
		goto unlock;

	/* While unexpected, the read_enc_key_size command may fail. The most
	 * secure approach is to then assume the key size is 0 to force a
	 * disconnection.
	 */
	if (rp->status) {
		bt_dev_err(hdev, "failed to read key size for handle %u",
			   handle);
		conn->enc_key_size = 0;
	} else {
		conn->enc_key_size = rp->key_size;
	}

	hci_encrypt_cfm(conn, 0);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_encrypt_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_encrypt_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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
		struct hci_cp_read_enc_key_size cp;
		struct hci_request req;

		/* Only send HCI_Read_Encryption_Key_Size if the
		 * controller really supports it. If it doesn't, assume
		 * the default size (16).
		 */
		if (!(hdev->commands[20] & 0x10)) {
			conn->enc_key_size = HCI_LINK_KEY_SIZE;
			goto notify;
		}

		hci_req_init(&req, hdev);

		cp.handle = cpu_to_le16(conn->handle);
		hci_req_add(&req, HCI_OP_READ_ENC_KEY_SIZE, sizeof(cp), &cp);

		if (hci_req_run_skb(&req, read_enc_key_size_complete)) {
			bt_dev_err(hdev, "sending read key size failed");
			conn->enc_key_size = HCI_LINK_KEY_SIZE;
			goto notify;
		}

		goto unlock;
	}

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
		hci_send_cmd(conn->hdev, HCI_OP_WRITE_AUTH_PAYLOAD_TO,
			     sizeof(cp), &cp);
	}

notify:
	hci_encrypt_cfm(conn, ev->status);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_change_link_key_complete_evt(struct hci_dev *hdev,
					     struct sk_buff *skb)
{
	struct hci_ev_change_link_key_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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

static void hci_remote_features_evt(struct hci_dev *hdev,
				    struct sk_buff *skb)
{
	struct hci_ev_remote_features *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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

	if (!ev->status && !test_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags)) {
		struct hci_cp_remote_name_req cp;
		memset(&cp, 0, sizeof(cp));
		bacpy(&cp.bdaddr, &conn->dst);
		cp.pscan_rep_mode = 0x02;
		hci_send_cmd(hdev, HCI_OP_REMOTE_NAME_REQ, sizeof(cp), &cp);
	} else if (!test_and_set_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags))
		mgmt_device_connected(hdev, conn, 0, NULL, 0);

	if (!hci_outgoing_auth_needed(hdev, conn)) {
		conn->state = BT_CONNECTED;
		hci_connect_cfm(conn, ev->status);
		hci_conn_drop(conn);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_cmd_complete_evt(struct hci_dev *hdev, struct sk_buff *skb,
				 u16 *opcode, u8 *status,
				 hci_req_complete_t *req_complete,
				 hci_req_complete_skb_t *req_complete_skb)
{
	struct hci_ev_cmd_complete *ev = (void *) skb->data;

	*opcode = __le16_to_cpu(ev->opcode);
	*status = skb->data[sizeof(*ev)];

	skb_pull(skb, sizeof(*ev));

	switch (*opcode) {
	case HCI_OP_INQUIRY_CANCEL:
		hci_cc_inquiry_cancel(hdev, skb, status);
		break;

	case HCI_OP_PERIODIC_INQ:
		hci_cc_periodic_inq(hdev, skb);
		break;

	case HCI_OP_EXIT_PERIODIC_INQ:
		hci_cc_exit_periodic_inq(hdev, skb);
		break;

	case HCI_OP_REMOTE_NAME_REQ_CANCEL:
		hci_cc_remote_name_req_cancel(hdev, skb);
		break;

	case HCI_OP_ROLE_DISCOVERY:
		hci_cc_role_discovery(hdev, skb);
		break;

	case HCI_OP_READ_LINK_POLICY:
		hci_cc_read_link_policy(hdev, skb);
		break;

	case HCI_OP_WRITE_LINK_POLICY:
		hci_cc_write_link_policy(hdev, skb);
		break;

	case HCI_OP_READ_DEF_LINK_POLICY:
		hci_cc_read_def_link_policy(hdev, skb);
		break;

	case HCI_OP_WRITE_DEF_LINK_POLICY:
		hci_cc_write_def_link_policy(hdev, skb);
		break;

	case HCI_OP_RESET:
		hci_cc_reset(hdev, skb);
		break;

	case HCI_OP_READ_STORED_LINK_KEY:
		hci_cc_read_stored_link_key(hdev, skb);
		break;

	case HCI_OP_DELETE_STORED_LINK_KEY:
		hci_cc_delete_stored_link_key(hdev, skb);
		break;

	case HCI_OP_WRITE_LOCAL_NAME:
		hci_cc_write_local_name(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_NAME:
		hci_cc_read_local_name(hdev, skb);
		break;

	case HCI_OP_WRITE_AUTH_ENABLE:
		hci_cc_write_auth_enable(hdev, skb);
		break;

	case HCI_OP_WRITE_ENCRYPT_MODE:
		hci_cc_write_encrypt_mode(hdev, skb);
		break;

	case HCI_OP_WRITE_SCAN_ENABLE:
		hci_cc_write_scan_enable(hdev, skb);
		break;

	case HCI_OP_SET_EVENT_FLT:
		hci_cc_set_event_filter(hdev, skb);
		break;

	case HCI_OP_READ_CLASS_OF_DEV:
		hci_cc_read_class_of_dev(hdev, skb);
		break;

	case HCI_OP_WRITE_CLASS_OF_DEV:
		hci_cc_write_class_of_dev(hdev, skb);
		break;

	case HCI_OP_READ_VOICE_SETTING:
		hci_cc_read_voice_setting(hdev, skb);
		break;

	case HCI_OP_WRITE_VOICE_SETTING:
		hci_cc_write_voice_setting(hdev, skb);
		break;

	case HCI_OP_READ_NUM_SUPPORTED_IAC:
		hci_cc_read_num_supported_iac(hdev, skb);
		break;

	case HCI_OP_WRITE_SSP_MODE:
		hci_cc_write_ssp_mode(hdev, skb);
		break;

	case HCI_OP_WRITE_SC_SUPPORT:
		hci_cc_write_sc_support(hdev, skb);
		break;

	case HCI_OP_READ_AUTH_PAYLOAD_TO:
		hci_cc_read_auth_payload_timeout(hdev, skb);
		break;

	case HCI_OP_WRITE_AUTH_PAYLOAD_TO:
		hci_cc_write_auth_payload_timeout(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_VERSION:
		hci_cc_read_local_version(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_COMMANDS:
		hci_cc_read_local_commands(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_FEATURES:
		hci_cc_read_local_features(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_EXT_FEATURES:
		hci_cc_read_local_ext_features(hdev, skb);
		break;

	case HCI_OP_READ_BUFFER_SIZE:
		hci_cc_read_buffer_size(hdev, skb);
		break;

	case HCI_OP_READ_BD_ADDR:
		hci_cc_read_bd_addr(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_PAIRING_OPTS:
		hci_cc_read_local_pairing_opts(hdev, skb);
		break;

	case HCI_OP_READ_PAGE_SCAN_ACTIVITY:
		hci_cc_read_page_scan_activity(hdev, skb);
		break;

	case HCI_OP_WRITE_PAGE_SCAN_ACTIVITY:
		hci_cc_write_page_scan_activity(hdev, skb);
		break;

	case HCI_OP_READ_PAGE_SCAN_TYPE:
		hci_cc_read_page_scan_type(hdev, skb);
		break;

	case HCI_OP_WRITE_PAGE_SCAN_TYPE:
		hci_cc_write_page_scan_type(hdev, skb);
		break;

	case HCI_OP_READ_DATA_BLOCK_SIZE:
		hci_cc_read_data_block_size(hdev, skb);
		break;

	case HCI_OP_READ_FLOW_CONTROL_MODE:
		hci_cc_read_flow_control_mode(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_AMP_INFO:
		hci_cc_read_local_amp_info(hdev, skb);
		break;

	case HCI_OP_READ_CLOCK:
		hci_cc_read_clock(hdev, skb);
		break;

	case HCI_OP_READ_INQ_RSP_TX_POWER:
		hci_cc_read_inq_rsp_tx_power(hdev, skb);
		break;

	case HCI_OP_READ_DEF_ERR_DATA_REPORTING:
		hci_cc_read_def_err_data_reporting(hdev, skb);
		break;

	case HCI_OP_WRITE_DEF_ERR_DATA_REPORTING:
		hci_cc_write_def_err_data_reporting(hdev, skb);
		break;

	case HCI_OP_PIN_CODE_REPLY:
		hci_cc_pin_code_reply(hdev, skb);
		break;

	case HCI_OP_PIN_CODE_NEG_REPLY:
		hci_cc_pin_code_neg_reply(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_OOB_DATA:
		hci_cc_read_local_oob_data(hdev, skb);
		break;

	case HCI_OP_READ_LOCAL_OOB_EXT_DATA:
		hci_cc_read_local_oob_ext_data(hdev, skb);
		break;

	case HCI_OP_LE_READ_BUFFER_SIZE:
		hci_cc_le_read_buffer_size(hdev, skb);
		break;

	case HCI_OP_LE_READ_LOCAL_FEATURES:
		hci_cc_le_read_local_features(hdev, skb);
		break;

	case HCI_OP_LE_READ_ADV_TX_POWER:
		hci_cc_le_read_adv_tx_power(hdev, skb);
		break;

	case HCI_OP_USER_CONFIRM_REPLY:
		hci_cc_user_confirm_reply(hdev, skb);
		break;

	case HCI_OP_USER_CONFIRM_NEG_REPLY:
		hci_cc_user_confirm_neg_reply(hdev, skb);
		break;

	case HCI_OP_USER_PASSKEY_REPLY:
		hci_cc_user_passkey_reply(hdev, skb);
		break;

	case HCI_OP_USER_PASSKEY_NEG_REPLY:
		hci_cc_user_passkey_neg_reply(hdev, skb);
		break;

	case HCI_OP_LE_SET_RANDOM_ADDR:
		hci_cc_le_set_random_addr(hdev, skb);
		break;

	case HCI_OP_LE_SET_ADV_ENABLE:
		hci_cc_le_set_adv_enable(hdev, skb);
		break;

	case HCI_OP_LE_SET_SCAN_PARAM:
		hci_cc_le_set_scan_param(hdev, skb);
		break;

	case HCI_OP_LE_SET_SCAN_ENABLE:
		hci_cc_le_set_scan_enable(hdev, skb);
		break;

	case HCI_OP_LE_READ_WHITE_LIST_SIZE:
		hci_cc_le_read_white_list_size(hdev, skb);
		break;

	case HCI_OP_LE_CLEAR_WHITE_LIST:
		hci_cc_le_clear_white_list(hdev, skb);
		break;

	case HCI_OP_LE_ADD_TO_WHITE_LIST:
		hci_cc_le_add_to_white_list(hdev, skb);
		break;

	case HCI_OP_LE_DEL_FROM_WHITE_LIST:
		hci_cc_le_del_from_white_list(hdev, skb);
		break;

	case HCI_OP_LE_READ_SUPPORTED_STATES:
		hci_cc_le_read_supported_states(hdev, skb);
		break;

	case HCI_OP_LE_READ_DEF_DATA_LEN:
		hci_cc_le_read_def_data_len(hdev, skb);
		break;

	case HCI_OP_LE_WRITE_DEF_DATA_LEN:
		hci_cc_le_write_def_data_len(hdev, skb);
		break;

	case HCI_OP_LE_ADD_TO_RESOLV_LIST:
		hci_cc_le_add_to_resolv_list(hdev, skb);
		break;

	case HCI_OP_LE_DEL_FROM_RESOLV_LIST:
		hci_cc_le_del_from_resolv_list(hdev, skb);
		break;

	case HCI_OP_LE_CLEAR_RESOLV_LIST:
		hci_cc_le_clear_resolv_list(hdev, skb);
		break;

	case HCI_OP_LE_READ_RESOLV_LIST_SIZE:
		hci_cc_le_read_resolv_list_size(hdev, skb);
		break;

	case HCI_OP_LE_SET_ADDR_RESOLV_ENABLE:
		hci_cc_le_set_addr_resolution_enable(hdev, skb);
		break;

	case HCI_OP_LE_READ_MAX_DATA_LEN:
		hci_cc_le_read_max_data_len(hdev, skb);
		break;

	case HCI_OP_WRITE_LE_HOST_SUPPORTED:
		hci_cc_write_le_host_supported(hdev, skb);
		break;

	case HCI_OP_LE_SET_ADV_PARAM:
		hci_cc_set_adv_param(hdev, skb);
		break;

	case HCI_OP_READ_RSSI:
		hci_cc_read_rssi(hdev, skb);
		break;

	case HCI_OP_READ_TX_POWER:
		hci_cc_read_tx_power(hdev, skb);
		break;

	case HCI_OP_WRITE_SSP_DEBUG_MODE:
		hci_cc_write_ssp_debug_mode(hdev, skb);
		break;

	case HCI_OP_LE_SET_EXT_SCAN_PARAMS:
		hci_cc_le_set_ext_scan_param(hdev, skb);
		break;

	case HCI_OP_LE_SET_EXT_SCAN_ENABLE:
		hci_cc_le_set_ext_scan_enable(hdev, skb);
		break;

	case HCI_OP_LE_SET_DEFAULT_PHY:
		hci_cc_le_set_default_phy(hdev, skb);
		break;

	case HCI_OP_LE_READ_NUM_SUPPORTED_ADV_SETS:
		hci_cc_le_read_num_adv_sets(hdev, skb);
		break;

	case HCI_OP_LE_SET_EXT_ADV_PARAMS:
		hci_cc_set_ext_adv_param(hdev, skb);
		break;

	case HCI_OP_LE_SET_EXT_ADV_ENABLE:
		hci_cc_le_set_ext_adv_enable(hdev, skb);
		break;

	case HCI_OP_LE_SET_ADV_SET_RAND_ADDR:
		hci_cc_le_set_adv_set_random_addr(hdev, skb);
		break;

	case HCI_OP_LE_READ_TRANSMIT_POWER:
		hci_cc_le_read_transmit_power(hdev, skb);
		break;

	default:
		BT_DBG("%s opcode 0x%4.4x", hdev->name, *opcode);
		break;
	}

	if (*opcode != HCI_OP_NOP)
		cancel_delayed_work(&hdev->cmd_timer);

	if (ev->ncmd && !test_bit(HCI_RESET, &hdev->flags))
		atomic_set(&hdev->cmd_cnt, 1);

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

static void hci_cmd_status_evt(struct hci_dev *hdev, struct sk_buff *skb,
			       u16 *opcode, u8 *status,
			       hci_req_complete_t *req_complete,
			       hci_req_complete_skb_t *req_complete_skb)
{
	struct hci_ev_cmd_status *ev = (void *) skb->data;

	skb_pull(skb, sizeof(*ev));

	*opcode = __le16_to_cpu(ev->opcode);
	*status = ev->status;

	switch (*opcode) {
	case HCI_OP_INQUIRY:
		hci_cs_inquiry(hdev, ev->status);
		break;

	case HCI_OP_CREATE_CONN:
		hci_cs_create_conn(hdev, ev->status);
		break;

	case HCI_OP_DISCONNECT:
		hci_cs_disconnect(hdev, ev->status);
		break;

	case HCI_OP_ADD_SCO:
		hci_cs_add_sco(hdev, ev->status);
		break;

	case HCI_OP_AUTH_REQUESTED:
		hci_cs_auth_requested(hdev, ev->status);
		break;

	case HCI_OP_SET_CONN_ENCRYPT:
		hci_cs_set_conn_encrypt(hdev, ev->status);
		break;

	case HCI_OP_REMOTE_NAME_REQ:
		hci_cs_remote_name_req(hdev, ev->status);
		break;

	case HCI_OP_READ_REMOTE_FEATURES:
		hci_cs_read_remote_features(hdev, ev->status);
		break;

	case HCI_OP_READ_REMOTE_EXT_FEATURES:
		hci_cs_read_remote_ext_features(hdev, ev->status);
		break;

	case HCI_OP_SETUP_SYNC_CONN:
		hci_cs_setup_sync_conn(hdev, ev->status);
		break;

	case HCI_OP_SNIFF_MODE:
		hci_cs_sniff_mode(hdev, ev->status);
		break;

	case HCI_OP_EXIT_SNIFF_MODE:
		hci_cs_exit_sniff_mode(hdev, ev->status);
		break;

	case HCI_OP_SWITCH_ROLE:
		hci_cs_switch_role(hdev, ev->status);
		break;

	case HCI_OP_LE_CREATE_CONN:
		hci_cs_le_create_conn(hdev, ev->status);
		break;

	case HCI_OP_LE_READ_REMOTE_FEATURES:
		hci_cs_le_read_remote_features(hdev, ev->status);
		break;

	case HCI_OP_LE_START_ENC:
		hci_cs_le_start_enc(hdev, ev->status);
		break;

	case HCI_OP_LE_EXT_CREATE_CONN:
		hci_cs_le_ext_create_conn(hdev, ev->status);
		break;

	default:
		BT_DBG("%s opcode 0x%4.4x", hdev->name, *opcode);
		break;
	}

	if (*opcode != HCI_OP_NOP)
		cancel_delayed_work(&hdev->cmd_timer);

	if (ev->ncmd && !test_bit(HCI_RESET, &hdev->flags))
		atomic_set(&hdev->cmd_cnt, 1);

	/* Indicate request completion if the command failed. Also, if
	 * we're not waiting for a special event and we get a success
	 * command status we should try to flag the request as completed
	 * (since for this kind of commands there will not be a command
	 * complete event).
	 */
	if (ev->status ||
	    (hdev->sent_cmd && !bt_cb(hdev->sent_cmd)->hci.req_event))
		hci_req_cmd_complete(hdev, *opcode, ev->status, req_complete,
				     req_complete_skb);

	if (hci_dev_test_flag(hdev, HCI_CMD_PENDING)) {
		bt_dev_err(hdev,
			   "unexpected event for opcode 0x%4.4x", *opcode);
		return;
	}

	if (atomic_read(&hdev->cmd_cnt) && !skb_queue_empty(&hdev->cmd_q))
		queue_work(hdev->workqueue, &hdev->cmd_work);
}

static void hci_hardware_error_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_hardware_error *ev = (void *) skb->data;

	hdev->hw_error_code = ev->code;

	queue_work(hdev->req_workqueue, &hdev->error_reset);
}

static void hci_role_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_role_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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

static void hci_num_comp_pkts_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_num_comp_pkts *ev = (void *) skb->data;
	int i;

	if (hdev->flow_ctl_mode != HCI_FLOW_CTL_MODE_PACKET_BASED) {
		bt_dev_err(hdev, "wrong event for mode %d", hdev->flow_ctl_mode);
		return;
	}

	if (skb->len < sizeof(*ev) ||
	    skb->len < struct_size(ev, handles, ev->num_hndl)) {
		BT_DBG("%s bad parameters", hdev->name);
		return;
	}

	BT_DBG("%s num_hndl %d", hdev->name, ev->num_hndl);

	for (i = 0; i < ev->num_hndl; i++) {
		struct hci_comp_pkts_info *info = &ev->handles[i];
		struct hci_conn *conn;
		__u16  handle, count;

		handle = __le16_to_cpu(info->handle);
		count  = __le16_to_cpu(info->count);

		conn = hci_conn_hash_lookup_handle(hdev, handle);
		if (!conn)
			continue;

		conn->sent -= count;

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
			hdev->sco_cnt += count;
			if (hdev->sco_cnt > hdev->sco_pkts)
				hdev->sco_cnt = hdev->sco_pkts;
			break;

		default:
			bt_dev_err(hdev, "unknown type %d conn %p",
				   conn->type, conn);
			break;
		}
	}

	queue_work(hdev->workqueue, &hdev->tx_work);
}

static struct hci_conn *__hci_conn_lookup_handle(struct hci_dev *hdev,
						 __u16 handle)
{
	struct hci_chan *chan;

	switch (hdev->dev_type) {
	case HCI_PRIMARY:
		return hci_conn_hash_lookup_handle(hdev, handle);
	case HCI_AMP:
		chan = hci_chan_lookup_handle(hdev, handle);
		if (chan)
			return chan->conn;
		break;
	default:
		bt_dev_err(hdev, "unknown dev_type %d", hdev->dev_type);
		break;
	}

	return NULL;
}

static void hci_num_comp_blocks_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_num_comp_blocks *ev = (void *) skb->data;
	int i;

	if (hdev->flow_ctl_mode != HCI_FLOW_CTL_MODE_BLOCK_BASED) {
		bt_dev_err(hdev, "wrong event for mode %d", hdev->flow_ctl_mode);
		return;
	}

	if (skb->len < sizeof(*ev) ||
	    skb->len < struct_size(ev, handles, ev->num_hndl)) {
		BT_DBG("%s bad parameters", hdev->name);
		return;
	}

	BT_DBG("%s num_blocks %d num_hndl %d", hdev->name, ev->num_blocks,
	       ev->num_hndl);

	for (i = 0; i < ev->num_hndl; i++) {
		struct hci_comp_blocks_info *info = &ev->handles[i];
		struct hci_conn *conn = NULL;
		__u16  handle, block_count;

		handle = __le16_to_cpu(info->handle);
		block_count = __le16_to_cpu(info->blocks);

		conn = __hci_conn_lookup_handle(hdev, handle);
		if (!conn)
			continue;

		conn->sent -= block_count;

		switch (conn->type) {
		case ACL_LINK:
		case AMP_LINK:
			hdev->block_cnt += block_count;
			if (hdev->block_cnt > hdev->num_blocks)
				hdev->block_cnt = hdev->num_blocks;
			break;

		default:
			bt_dev_err(hdev, "unknown type %d conn %p",
				   conn->type, conn);
			break;
		}
	}

	queue_work(hdev->workqueue, &hdev->tx_work);
}

static void hci_mode_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_mode_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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

static void hci_pin_code_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_pin_code_req *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

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

static void hci_link_key_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_link_key_req *ev = (void *) skb->data;
	struct hci_cp_link_key_reply cp;
	struct hci_conn *conn;
	struct link_key *key;

	BT_DBG("%s", hdev->name);

	if (!hci_dev_test_flag(hdev, HCI_MGMT))
		return;

	hci_dev_lock(hdev);

	key = hci_find_link_key(hdev, &ev->bdaddr);
	if (!key) {
		BT_DBG("%s link key not found for %pMR", hdev->name,
		       &ev->bdaddr);
		goto not_found;
	}

	BT_DBG("%s found key type %u for %pMR", hdev->name, key->type,
	       &ev->bdaddr);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn) {
		clear_bit(HCI_CONN_NEW_LINK_KEY, &conn->flags);

		if ((key->type == HCI_LK_UNAUTH_COMBINATION_P192 ||
		     key->type == HCI_LK_UNAUTH_COMBINATION_P256) &&
		    conn->auth_type != 0xff && (conn->auth_type & 0x01)) {
			BT_DBG("%s ignoring unauthenticated key", hdev->name);
			goto not_found;
		}

		if (key->type == HCI_LK_COMBINATION && key->pin_len < 16 &&
		    (conn->pending_sec_level == BT_SECURITY_HIGH ||
		     conn->pending_sec_level == BT_SECURITY_FIPS)) {
			BT_DBG("%s ignoring key unauthenticated for high security",
			       hdev->name);
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

static void hci_link_key_notify_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_link_key_notify *ev = (void *) skb->data;
	struct hci_conn *conn;
	struct link_key *key;
	bool persistent;
	u8 pin_len = 0;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		goto unlock;

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

static void hci_clock_offset_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_clock_offset *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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

static void hci_pkt_type_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_pkt_type_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn && !ev->status)
		conn->pkt_type = __le16_to_cpu(ev->pkt_type);

	hci_dev_unlock(hdev);
}

static void hci_pscan_rep_mode_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_pscan_rep_mode *ev = (void *) skb->data;
	struct inquiry_entry *ie;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
	if (ie) {
		ie->data.pscan_rep_mode = ev->pscan_rep_mode;
		ie->timestamp = jiffies;
	}

	hci_dev_unlock(hdev);
}

static void hci_inquiry_result_with_rssi_evt(struct hci_dev *hdev,
					     struct sk_buff *skb)
{
	struct inquiry_data data;
	int num_rsp = *((__u8 *) skb->data);

	BT_DBG("%s num_rsp %d", hdev->name, num_rsp);

	if (!num_rsp)
		return;

	if (hci_dev_test_flag(hdev, HCI_PERIODIC_INQ))
		return;

	hci_dev_lock(hdev);

	if ((skb->len - 1) / num_rsp != sizeof(struct inquiry_info_with_rssi)) {
		struct inquiry_info_with_rssi_and_pscan_mode *info;
		info = (void *) (skb->data + 1);

		if (skb->len < num_rsp * sizeof(*info) + 1)
			goto unlock;

		for (; num_rsp; num_rsp--, info++) {
			u32 flags;

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
					  flags, NULL, 0, NULL, 0);
		}
	} else {
		struct inquiry_info_with_rssi *info = (void *) (skb->data + 1);

		if (skb->len < num_rsp * sizeof(*info) + 1)
			goto unlock;

		for (; num_rsp; num_rsp--, info++) {
			u32 flags;

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
					  flags, NULL, 0, NULL, 0);
		}
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_remote_ext_features_evt(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_ev_remote_ext_features *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

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
	} else if (!test_and_set_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags))
		mgmt_device_connected(hdev, conn, 0, NULL, 0);

	if (!hci_outgoing_auth_needed(hdev, conn)) {
		conn->state = BT_CONNECTED;
		hci_connect_cfm(conn, ev->status);
		hci_conn_drop(conn);
	}

unlock:
	hci_dev_unlock(hdev);
}

static void hci_sync_conn_complete_evt(struct hci_dev *hdev,
				       struct sk_buff *skb)
{
	struct hci_ev_sync_conn_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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

	switch (ev->status) {
	case 0x00:
		conn->handle = __le16_to_cpu(ev->handle);
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
			if (hci_setup_sync(conn, conn->link->handle))
				goto unlock;
		}
		fallthrough;

	default:
		conn->state = BT_CLOSED;
		break;
	}

	bt_dev_dbg(hdev, "SCO connected with air mode: %02x", ev->air_mode);

	switch (conn->setting & SCO_AIRMODE_MASK) {
	case SCO_AIRMODE_CVSD:
		if (hdev->notify)
			hdev->notify(hdev, HCI_NOTIFY_ENABLE_SCO_CVSD);
		break;
	case SCO_AIRMODE_TRANSP:
		if (hdev->notify)
			hdev->notify(hdev, HCI_NOTIFY_ENABLE_SCO_TRANSP);
		break;
	}

	hci_connect_cfm(conn, ev->status);
	if (ev->status)
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

static void hci_extended_inquiry_result_evt(struct hci_dev *hdev,
					    struct sk_buff *skb)
{
	struct inquiry_data data;
	struct extended_inquiry_info *info = (void *) (skb->data + 1);
	int num_rsp = *((__u8 *) skb->data);
	size_t eir_len;

	BT_DBG("%s num_rsp %d", hdev->name, num_rsp);

	if (!num_rsp || skb->len < num_rsp * sizeof(*info) + 1)
		return;

	if (hci_dev_test_flag(hdev, HCI_PERIODIC_INQ))
		return;

	hci_dev_lock(hdev);

	for (; num_rsp; num_rsp--, info++) {
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
				  flags, info->data, eir_len, NULL, 0);
	}

	hci_dev_unlock(hdev);
}

static void hci_key_refresh_complete_evt(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_ev_key_refresh_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x handle 0x%4.4x", hdev->name, ev->status,
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
		if (!memcmp(data->rand256, ZERO_KEY, 16) ||
		    !memcmp(data->hash256, ZERO_KEY, 16))
			return 0x00;

		return 0x02;
	}

	/* When Secure Connections is not enabled or actually
	 * not supported by the hardware, then check that if
	 * P-192 data values are present.
	 */
	if (!memcmp(data->rand192, ZERO_KEY, 16) ||
	    !memcmp(data->hash192, ZERO_KEY, 16))
		return 0x00;

	return 0x01;
}

static void hci_io_capa_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_io_capa_request *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		goto unlock;

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

static void hci_io_capa_reply_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_io_capa_reply *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
		goto unlock;

	conn->remote_cap = ev->capability;
	conn->remote_auth = ev->authentication;

unlock:
	hci_dev_unlock(hdev);
}

static void hci_user_confirm_request_evt(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_ev_user_confirm_req *ev = (void *) skb->data;
	int loc_mitm, rem_mitm, confirm_hint = 0;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

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
		BT_DBG("Rejecting request: remote device can't provide MITM");
		hci_send_cmd(hdev, HCI_OP_USER_CONFIRM_NEG_REPLY,
			     sizeof(ev->bdaddr), &ev->bdaddr);
		goto unlock;
	}

	/* If no side requires MITM protection; auto-accept */
	if ((!loc_mitm || conn->remote_cap == HCI_IO_NO_INPUT_OUTPUT) &&
	    (!rem_mitm || conn->io_capability == HCI_IO_NO_INPUT_OUTPUT)) {

		/* If we're not the initiators request authorization to
		 * proceed from user space (mgmt_user_confirm with
		 * confirm_hint set to 1). The exception is if neither
		 * side had MITM or if the local IO capability is
		 * NoInputNoOutput, in which case we do auto-accept
		 */
		if (!test_bit(HCI_CONN_AUTH_PEND, &conn->flags) &&
		    conn->io_capability != HCI_IO_NO_INPUT_OUTPUT &&
		    (loc_mitm || rem_mitm)) {
			BT_DBG("Confirming auto-accept as acceptor");
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

static void hci_user_passkey_request_evt(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_ev_user_passkey_req *ev = (void *) skb->data;

	BT_DBG("%s", hdev->name);

	if (hci_dev_test_flag(hdev, HCI_MGMT))
		mgmt_user_passkey_request(hdev, &ev->bdaddr, ACL_LINK, 0);
}

static void hci_user_passkey_notify_evt(struct hci_dev *hdev,
					struct sk_buff *skb)
{
	struct hci_ev_user_passkey_notify *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

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

static void hci_keypress_notify_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_keypress_notify *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

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

static void hci_simple_pair_complete_evt(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_ev_simple_pair_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (!conn)
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

static void hci_remote_host_features_evt(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_ev_remote_host_features *ev = (void *) skb->data;
	struct inquiry_entry *ie;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn)
		memcpy(conn->features[1], ev->features, 8);

	ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
	if (ie)
		ie->data.ssp_mode = (ev->features[0] & LMP_HOST_SSP);

	hci_dev_unlock(hdev);
}

static void hci_remote_oob_data_request_evt(struct hci_dev *hdev,
					    struct sk_buff *skb)
{
	struct hci_ev_remote_oob_data_request *ev = (void *) skb->data;
	struct oob_data *data;

	BT_DBG("%s", hdev->name);

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

#if IS_ENABLED(CONFIG_BT_HS)
static void hci_chan_selected_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_channel_selected *ev = (void *)skb->data;
	struct hci_conn *hcon;

	BT_DBG("%s handle 0x%2.2x", hdev->name, ev->phy_handle);

	skb_pull(skb, sizeof(*ev));

	hcon = hci_conn_hash_lookup_handle(hdev, ev->phy_handle);
	if (!hcon)
		return;

	amp_read_loc_assoc_final_data(hdev, hcon);
}

static void hci_phy_link_complete_evt(struct hci_dev *hdev,
				      struct sk_buff *skb)
{
	struct hci_ev_phy_link_complete *ev = (void *) skb->data;
	struct hci_conn *hcon, *bredr_hcon;

	BT_DBG("%s handle 0x%2.2x status 0x%2.2x", hdev->name, ev->phy_handle,
	       ev->status);

	hci_dev_lock(hdev);

	hcon = hci_conn_hash_lookup_handle(hdev, ev->phy_handle);
	if (!hcon)
		goto unlock;

	if (!hcon->amp_mgr)
		goto unlock;

	if (ev->status) {
		hci_conn_del(hcon);
		goto unlock;
	}

	bredr_hcon = hcon->amp_mgr->l2cap_conn->hcon;

	hcon->state = BT_CONNECTED;
	bacpy(&hcon->dst, &bredr_hcon->dst);

	hci_conn_hold(hcon);
	hcon->disc_timeout = HCI_DISCONN_TIMEOUT;
	hci_conn_drop(hcon);

	hci_debugfs_create_conn(hcon);
	hci_conn_add_sysfs(hcon);

	amp_physical_cfm(bredr_hcon, hcon);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_loglink_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_logical_link_complete *ev = (void *) skb->data;
	struct hci_conn *hcon;
	struct hci_chan *hchan;
	struct amp_mgr *mgr;

	BT_DBG("%s log_handle 0x%4.4x phy_handle 0x%2.2x status 0x%2.2x",
	       hdev->name, le16_to_cpu(ev->handle), ev->phy_handle,
	       ev->status);

	hcon = hci_conn_hash_lookup_handle(hdev, ev->phy_handle);
	if (!hcon)
		return;

	/* Create AMP hchan */
	hchan = hci_chan_create(hcon);
	if (!hchan)
		return;

	hchan->handle = le16_to_cpu(ev->handle);

	BT_DBG("hcon %p mgr %p hchan %p", hcon, hcon->amp_mgr, hchan);

	mgr = hcon->amp_mgr;
	if (mgr && mgr->bredr_chan) {
		struct l2cap_chan *bredr_chan = mgr->bredr_chan;

		l2cap_chan_lock(bredr_chan);

		bredr_chan->conn->mtu = hdev->block_mtu;
		l2cap_logical_cfm(bredr_chan, hchan, 0);
		hci_conn_hold(hcon);

		l2cap_chan_unlock(bredr_chan);
	}
}

static void hci_disconn_loglink_complete_evt(struct hci_dev *hdev,
					     struct sk_buff *skb)
{
	struct hci_ev_disconn_logical_link_complete *ev = (void *) skb->data;
	struct hci_chan *hchan;

	BT_DBG("%s log handle 0x%4.4x status 0x%2.2x", hdev->name,
	       le16_to_cpu(ev->handle), ev->status);

	if (ev->status)
		return;

	hci_dev_lock(hdev);

	hchan = hci_chan_lookup_handle(hdev, le16_to_cpu(ev->handle));
	if (!hchan)
		goto unlock;

	amp_destroy_logical_link(hchan, ev->reason);

unlock:
	hci_dev_unlock(hdev);
}

static void hci_disconn_phylink_complete_evt(struct hci_dev *hdev,
					     struct sk_buff *skb)
{
	struct hci_ev_disconn_phy_link_complete *ev = (void *) skb->data;
	struct hci_conn *hcon;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	if (ev->status)
		return;

	hci_dev_lock(hdev);

	hcon = hci_conn_hash_lookup_handle(hdev, ev->phy_handle);
	if (hcon) {
		hcon->state = BT_CLOSED;
		hci_conn_del(hcon);
	}

	hci_dev_unlock(hdev);
}
#endif

static void le_conn_complete_evt(struct hci_dev *hdev, u8 status,
			bdaddr_t *bdaddr, u8 bdaddr_type, u8 role, u16 handle,
			u16 interval, u16 latency, u16 supervision_timeout)
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

	conn = hci_lookup_le_connect(hdev);
	if (!conn) {
		conn = hci_conn_add(hdev, LE_LINK, bdaddr, role);
		if (!conn) {
			bt_dev_err(hdev, "no memory for new connection");
			goto unlock;
		}

		conn->dst_type = bdaddr_type;

		/* If we didn't have a hci_conn object previously
		 * but we're in master role this must be something
		 * initiated using a white list. Since white list based
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

	if (!conn->out) {
		/* Set the responder (our side) address type based on
		 * the advertising address type.
		 */
		conn->resp_addr_type = hdev->adv_addr_type;
		if (hdev->adv_addr_type == ADDR_LE_DEV_RANDOM) {
			/* In case of ext adv, resp_addr will be updated in
			 * Adv Terminated event.
			 */
			if (!ext_adv_capable(hdev))
				bacpy(&conn->resp_addr, &hdev->random_addr);
		} else {
			bacpy(&conn->resp_addr, &hdev->bdaddr);
		}

		conn->init_addr_type = bdaddr_type;
		bacpy(&conn->init_addr, bdaddr);

		/* For incoming connections, set the default minimum
		 * and maximum connection interval. They will be used
		 * to check if the parameters are in range and if not
		 * trigger the connection update procedure.
		 */
		conn->le_conn_min_interval = hdev->le_conn_min_interval;
		conn->le_conn_max_interval = hdev->le_conn_max_interval;
	}

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

	if (status) {
		hci_le_conn_failed(conn, status);
		goto unlock;
	}

	if (conn->dst_type == ADDR_LE_DEV_PUBLIC)
		addr_type = BDADDR_LE_PUBLIC;
	else
		addr_type = BDADDR_LE_RANDOM;

	/* Drop the connection if the device is blocked */
	if (hci_bdaddr_list_lookup(&hdev->blacklist, &conn->dst, addr_type)) {
		hci_conn_drop(conn);
		goto unlock;
	}

	if (!test_and_set_bit(HCI_CONN_MGMT_CONNECTED, &conn->flags))
		mgmt_device_connected(hdev, conn, 0, NULL, 0);

	conn->sec_level = BT_SECURITY_LOW;
	conn->handle = handle;
	conn->state = BT_CONFIG;

	conn->le_conn_interval = interval;
	conn->le_conn_latency = latency;
	conn->le_supv_timeout = supervision_timeout;

	hci_debugfs_create_conn(conn);
	hci_conn_add_sysfs(conn);

	/* The remote features procedure is defined for master
	 * role only. So only in case of an initiated connection
	 * request the remote features.
	 *
	 * If the local controller supports slave-initiated features
	 * exchange, then requesting the remote features in slave
	 * role is possible. Otherwise just transition into the
	 * connected state without requesting the remote features.
	 */
	if (conn->out ||
	    (hdev->le_features[0] & HCI_LE_SLAVE_FEATURES)) {
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
		list_del_init(&params->action);
		if (params->conn) {
			hci_conn_drop(params->conn);
			hci_conn_put(params->conn);
			params->conn = NULL;
		}
	}

unlock:
	hci_update_background_scan(hdev);
	hci_dev_unlock(hdev);
}

static void hci_le_conn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_le_conn_complete *ev = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	le_conn_complete_evt(hdev, ev->status, &ev->bdaddr, ev->bdaddr_type,
			     ev->role, le16_to_cpu(ev->handle),
			     le16_to_cpu(ev->interval),
			     le16_to_cpu(ev->latency),
			     le16_to_cpu(ev->supervision_timeout));
}

static void hci_le_enh_conn_complete_evt(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	struct hci_ev_le_enh_conn_complete *ev = (void *) skb->data;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	le_conn_complete_evt(hdev, ev->status, &ev->bdaddr, ev->bdaddr_type,
			     ev->role, le16_to_cpu(ev->handle),
			     le16_to_cpu(ev->interval),
			     le16_to_cpu(ev->latency),
			     le16_to_cpu(ev->supervision_timeout));

	if (use_ll_privacy(hdev) &&
	    hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY) &&
	    hci_dev_test_flag(hdev, HCI_LL_RPA_RESOLUTION))
		hci_req_disable_address_resolution(hdev);
}

static void hci_le_ext_adv_term_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_evt_le_ext_adv_set_term *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	if (ev->status)
		return;

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->conn_handle));
	if (conn) {
		struct adv_info *adv_instance;

		if (hdev->adv_addr_type != ADDR_LE_DEV_RANDOM)
			return;

		if (!hdev->cur_adv_instance) {
			bacpy(&conn->resp_addr, &hdev->random_addr);
			return;
		}

		adv_instance = hci_find_adv_instance(hdev, hdev->cur_adv_instance);
		if (adv_instance)
			bacpy(&conn->resp_addr, &adv_instance->random_addr);
	}
}

static void hci_le_conn_update_complete_evt(struct hci_dev *hdev,
					    struct sk_buff *skb)
{
	struct hci_ev_le_conn_update_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

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
					      u8 addr_type, u8 adv_type,
					      bdaddr_t *direct_rpa)
{
	struct hci_conn *conn;
	struct hci_conn_params *params;

	/* If the event is not connectable don't proceed further */
	if (adv_type != LE_ADV_IND && adv_type != LE_ADV_DIRECT_IND)
		return NULL;

	/* Ignore if the device is blocked */
	if (hci_bdaddr_list_lookup(&hdev->blacklist, addr, addr_type))
		return NULL;

	/* Most controller will fail if we try to create new connections
	 * while we have an existing one in slave role.
	 */
	if (hdev->conn_hash.le_num_slave > 0 &&
	    (!test_bit(HCI_QUIRK_VALID_LE_STATES, &hdev->quirks) ||
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
			 * incoming connections from slave devices.
			 */
			if (adv_type != LE_ADV_DIRECT_IND)
				return NULL;
			break;
		case HCI_AUTO_CONN_ALWAYS:
			/* Devices advertising with ADV_IND or ADV_DIRECT_IND
			 * are triggering a connection attempt. This means
			 * that incoming connections from slave device are
			 * accepted and also outgoing connections to slave
			 * devices are established when found.
			 */
			break;
		default:
			return NULL;
		}
	}

	conn = hci_connect_le(hdev, addr, addr_type, BT_SECURITY_LOW,
			      hdev->def_le_autoconnect_timeout, HCI_ROLE_MASTER,
			      direct_rpa);
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
			       u8 direct_addr_type, s8 rssi, u8 *data, u8 len,
			       bool ext_adv)
{
	struct discovery_state *d = &hdev->discovery;
	struct smp_irk *irk;
	struct hci_conn *conn;
	bool match;
	u32 flags;
	u8 *ptr, real_len;

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

	if (!ext_adv && len > HCI_MAX_AD_LENGTH) {
		bt_dev_err_ratelimited(hdev, "legacy adv larger than 31 bytes");
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

	real_len = ptr - data;

	/* Adjust for actual length */
	if (len != real_len) {
		bt_dev_err_ratelimited(hdev, "advertising data len corrected %u -> %u",
				       len, real_len);
		len = real_len;
	}

	/* If the direct address is present, then this report is from
	 * a LE Direct Advertising Report event. In that case it is
	 * important to see if the address is matching the local
	 * controller address.
	 */
	if (direct_addr) {
		/* Only resolvable random addresses are valid for these
		 * kind of reports and others can be ignored.
		 */
		if (!hci_bdaddr_is_rpa(direct_addr, direct_addr_type))
			return;

		/* If the controller is not using resolvable random
		 * addresses, then this report can be ignored.
		 */
		if (!hci_dev_test_flag(hdev, HCI_PRIVACY))
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

	/* Check if we have been requested to connect to this device.
	 *
	 * direct_addr is set only for directed advertising reports (it is NULL
	 * for advertising reports) and is already verified to be RPA above.
	 */
	conn = check_pending_le_conn(hdev, bdaddr, bdaddr_type, type,
								direct_addr);
	if (!ext_adv && conn && type == LE_ADV_IND && len <= HCI_MAX_AD_LENGTH) {
		/* Store report for later inclusion by
		 * mgmt_device_connected
		 */
		memcpy(conn->le_adv_data, data, len);
		conn->le_adv_data_len = len;
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

		if (type == LE_ADV_NONCONN_IND || type == LE_ADV_SCAN_IND)
			flags = MGMT_DEV_FOUND_NOT_CONNECTABLE;
		else
			flags = 0;
		mgmt_device_found(hdev, bdaddr, LE_LINK, bdaddr_type, NULL,
				  rssi, flags, data, len, NULL, 0);
		return;
	}

	/* When receiving non-connectable or scannable undirected
	 * advertising reports, this means that the remote device is
	 * not connectable and then clearly indicate this in the
	 * device found event.
	 *
	 * When receiving a scan response, then there is no way to
	 * know if the remote device is connectable or not. However
	 * since scan responses are merged with a previously seen
	 * advertising report, the flags field from that report
	 * will be used.
	 *
	 * In the really unlikely case that a controller get confused
	 * and just sends a scan response event, then it is marked as
	 * not connectable as well.
	 */
	if (type == LE_ADV_NONCONN_IND || type == LE_ADV_SCAN_IND ||
	    type == LE_ADV_SCAN_RSP)
		flags = MGMT_DEV_FOUND_NOT_CONNECTABLE;
	else
		flags = 0;

	/* If there's nothing pending either store the data from this
	 * event or send an immediate device found event if the data
	 * should not be stored for later.
	 */
	if (!ext_adv &&	!has_pending_adv_report(hdev)) {
		/* If the report will trigger a SCAN_REQ store it for
		 * later merging.
		 */
		if (type == LE_ADV_IND || type == LE_ADV_SCAN_IND) {
			store_pending_adv_report(hdev, bdaddr, bdaddr_type,
						 rssi, flags, data, len);
			return;
		}

		mgmt_device_found(hdev, bdaddr, LE_LINK, bdaddr_type, NULL,
				  rssi, flags, data, len, NULL, 0);
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
					  d->last_adv_data_len, NULL, 0);

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
				  rssi, flags, data, len, NULL, 0);
		return;
	}

	/* If we get here we've got a pending ADV_IND or ADV_SCAN_IND and
	 * the new event is a SCAN_RSP. We can therefore proceed with
	 * sending a merged device found event.
	 */
	mgmt_device_found(hdev, &d->last_adv_addr, LE_LINK,
			  d->last_adv_addr_type, NULL, rssi, d->last_adv_flags,
			  d->last_adv_data, d->last_adv_data_len, data, len);
	clear_pending_adv_report(hdev);
}

static void hci_le_adv_report_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	u8 num_reports = skb->data[0];
	void *ptr = &skb->data[1];

	hci_dev_lock(hdev);

	while (num_reports--) {
		struct hci_ev_le_advertising_info *ev = ptr;
		s8 rssi;

		if (ev->length <= HCI_MAX_AD_LENGTH) {
			rssi = ev->data[ev->length];
			process_adv_report(hdev, ev->evt_type, &ev->bdaddr,
					   ev->bdaddr_type, NULL, 0, rssi,
					   ev->data, ev->length, false);
		} else {
			bt_dev_err(hdev, "Dropping invalid advertising data");
		}

		ptr += sizeof(*ev) + ev->length + 1;
	}

	hci_dev_unlock(hdev);
}

static u8 ext_evt_type_to_legacy(struct hci_dev *hdev, u16 evt_type)
{
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

	if (evt_type == LE_EXT_ADV_NON_CONN_IND ||
	    evt_type & LE_EXT_ADV_DIRECT_IND)
		return LE_ADV_NONCONN_IND;

invalid:
	bt_dev_err_ratelimited(hdev, "Unknown advertising packet type: 0x%02x",
			       evt_type);

	return LE_ADV_INVALID;
}

static void hci_le_ext_adv_report_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	u8 num_reports = skb->data[0];
	void *ptr = &skb->data[1];

	hci_dev_lock(hdev);

	while (num_reports--) {
		struct hci_ev_le_ext_adv_report *ev = ptr;
		u8 legacy_evt_type;
		u16 evt_type;

		evt_type = __le16_to_cpu(ev->evt_type);
		legacy_evt_type = ext_evt_type_to_legacy(hdev, evt_type);
		if (legacy_evt_type != LE_ADV_INVALID) {
			process_adv_report(hdev, legacy_evt_type, &ev->bdaddr,
					   ev->bdaddr_type, NULL, 0, ev->rssi,
					   ev->data, ev->length,
					   !(evt_type & LE_EXT_ADV_LEGACY_PDU));
		}

		ptr += sizeof(*ev) + ev->length;
	}

	hci_dev_unlock(hdev);
}

static void hci_le_remote_feat_complete_evt(struct hci_dev *hdev,
					    struct sk_buff *skb)
{
	struct hci_ev_le_remote_feat_complete *ev = (void *)skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status)
			memcpy(conn->features[0], ev->features, 8);

		if (conn->state == BT_CONFIG) {
			__u8 status;

			/* If the local controller supports slave-initiated
			 * features exchange, but the remote controller does
			 * not, then it is possible that the error code 0x1a
			 * for unsupported remote feature gets returned.
			 *
			 * In this specific case, allow the connection to
			 * transition into connected state and mark it as
			 * successful.
			 */
			if ((hdev->le_features[0] & HCI_LE_SLAVE_FEATURES) &&
			    !conn->out && ev->status == 0x1a)
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

static void hci_le_ltk_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_le_ltk_req *ev = (void *) skb->data;
	struct hci_cp_le_ltk_reply cp;
	struct hci_cp_le_ltk_neg_reply neg;
	struct hci_conn *conn;
	struct smp_ltk *ltk;

	BT_DBG("%s handle 0x%4.4x", hdev->name, __le16_to_cpu(ev->handle));

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

static void hci_le_remote_conn_param_req_evt(struct hci_dev *hdev,
					     struct sk_buff *skb)
{
	struct hci_ev_le_remote_conn_param_req *ev = (void *) skb->data;
	struct hci_cp_le_conn_param_req_reply cp;
	struct hci_conn *hcon;
	u16 handle, min, max, latency, timeout;

	handle = le16_to_cpu(ev->handle);
	min = le16_to_cpu(ev->interval_min);
	max = le16_to_cpu(ev->interval_max);
	latency = le16_to_cpu(ev->latency);
	timeout = le16_to_cpu(ev->timeout);

	hcon = hci_conn_hash_lookup_handle(hdev, handle);
	if (!hcon || hcon->state != BT_CONNECTED)
		return send_conn_param_neg_reply(hdev, handle,
						 HCI_ERROR_UNKNOWN_CONN_ID);

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
		} else{
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

static void hci_le_direct_adv_report_evt(struct hci_dev *hdev,
					 struct sk_buff *skb)
{
	u8 num_reports = skb->data[0];
	struct hci_ev_le_direct_adv_info *ev = (void *)&skb->data[1];

	if (!num_reports || skb->len < num_reports * sizeof(*ev) + 1)
		return;

	hci_dev_lock(hdev);

	for (; num_reports; num_reports--, ev++)
		process_adv_report(hdev, ev->evt_type, &ev->bdaddr,
				   ev->bdaddr_type, &ev->direct_addr,
				   ev->direct_addr_type, ev->rssi, NULL, 0,
				   false);

	hci_dev_unlock(hdev);
}

static void hci_le_phy_update_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_le_phy_update_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%2.2x", hdev->name, ev->status);

	if (!ev->status)
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

static void hci_le_meta_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_le_meta *le_ev = (void *) skb->data;

	skb_pull(skb, sizeof(*le_ev));

	switch (le_ev->subevent) {
	case HCI_EV_LE_CONN_COMPLETE:
		hci_le_conn_complete_evt(hdev, skb);
		break;

	case HCI_EV_LE_CONN_UPDATE_COMPLETE:
		hci_le_conn_update_complete_evt(hdev, skb);
		break;

	case HCI_EV_LE_ADVERTISING_REPORT:
		hci_le_adv_report_evt(hdev, skb);
		break;

	case HCI_EV_LE_REMOTE_FEAT_COMPLETE:
		hci_le_remote_feat_complete_evt(hdev, skb);
		break;

	case HCI_EV_LE_LTK_REQ:
		hci_le_ltk_request_evt(hdev, skb);
		break;

	case HCI_EV_LE_REMOTE_CONN_PARAM_REQ:
		hci_le_remote_conn_param_req_evt(hdev, skb);
		break;

	case HCI_EV_LE_DIRECT_ADV_REPORT:
		hci_le_direct_adv_report_evt(hdev, skb);
		break;

	case HCI_EV_LE_PHY_UPDATE_COMPLETE:
		hci_le_phy_update_evt(hdev, skb);
		break;

	case HCI_EV_LE_EXT_ADV_REPORT:
		hci_le_ext_adv_report_evt(hdev, skb);
		break;

	case HCI_EV_LE_ENHANCED_CONN_COMPLETE:
		hci_le_enh_conn_complete_evt(hdev, skb);
		break;

	case HCI_EV_LE_EXT_ADV_SET_TERM:
		hci_le_ext_adv_term_evt(hdev, skb);
		break;

	default:
		break;
	}
}

static bool hci_get_cmd_complete(struct hci_dev *hdev, u16 opcode,
				 u8 event, struct sk_buff *skb)
{
	struct hci_ev_cmd_complete *ev;
	struct hci_event_hdr *hdr;

	if (!skb)
		return false;

	if (skb->len < sizeof(*hdr)) {
		bt_dev_err(hdev, "too short HCI event");
		return false;
	}

	hdr = (void *) skb->data;
	skb_pull(skb, HCI_EVENT_HDR_SIZE);

	if (event) {
		if (hdr->evt != event)
			return false;
		return true;
	}

	/* Check if request ended in Command Status - no way to retreive
	 * any extra parameters in this case.
	 */
	if (hdr->evt == HCI_EV_CMD_STATUS)
		return false;

	if (hdr->evt != HCI_EV_CMD_COMPLETE) {
		bt_dev_err(hdev, "last event is not cmd complete (0x%2.2x)",
			   hdr->evt);
		return false;
	}

	if (skb->len < sizeof(*ev)) {
		bt_dev_err(hdev, "too short cmd_complete event");
		return false;
	}

	ev = (void *) skb->data;
	skb_pull(skb, sizeof(*ev));

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
	struct hci_ev_le_ext_adv_report *ext_adv;
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
		bacpy(&hdev->wake_addr, &conn_complete->bdaddr);
		hdev->wake_addr_type = BDADDR_BREDR;
	} else if (event == HCI_EV_CONN_COMPLETE) {
		bacpy(&hdev->wake_addr, &conn_request->bdaddr);
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

void hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *) skb->data;
	hci_req_complete_t req_complete = NULL;
	hci_req_complete_skb_t req_complete_skb = NULL;
	struct sk_buff *orig_skb = NULL;
	u8 status = 0, event = hdr->evt, req_evt = 0;
	u16 opcode = HCI_OP_NOP;

	if (!event) {
		bt_dev_warn(hdev, "Received unexpected HCI Event 00000000");
		goto done;
	}

	if (hdev->sent_cmd && bt_cb(hdev->sent_cmd)->hci.req_event == event) {
		struct hci_command_hdr *cmd_hdr = (void *) hdev->sent_cmd->data;
		opcode = __le16_to_cpu(cmd_hdr->opcode);
		hci_req_cmd_complete(hdev, opcode, status, &req_complete,
				     &req_complete_skb);
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

	switch (event) {
	case HCI_EV_INQUIRY_COMPLETE:
		hci_inquiry_complete_evt(hdev, skb);
		break;

	case HCI_EV_INQUIRY_RESULT:
		hci_inquiry_result_evt(hdev, skb);
		break;

	case HCI_EV_CONN_COMPLETE:
		hci_conn_complete_evt(hdev, skb);
		break;

	case HCI_EV_CONN_REQUEST:
		hci_conn_request_evt(hdev, skb);
		break;

	case HCI_EV_DISCONN_COMPLETE:
		hci_disconn_complete_evt(hdev, skb);
		break;

	case HCI_EV_AUTH_COMPLETE:
		hci_auth_complete_evt(hdev, skb);
		break;

	case HCI_EV_REMOTE_NAME:
		hci_remote_name_evt(hdev, skb);
		break;

	case HCI_EV_ENCRYPT_CHANGE:
		hci_encrypt_change_evt(hdev, skb);
		break;

	case HCI_EV_CHANGE_LINK_KEY_COMPLETE:
		hci_change_link_key_complete_evt(hdev, skb);
		break;

	case HCI_EV_REMOTE_FEATURES:
		hci_remote_features_evt(hdev, skb);
		break;

	case HCI_EV_CMD_COMPLETE:
		hci_cmd_complete_evt(hdev, skb, &opcode, &status,
				     &req_complete, &req_complete_skb);
		break;

	case HCI_EV_CMD_STATUS:
		hci_cmd_status_evt(hdev, skb, &opcode, &status, &req_complete,
				   &req_complete_skb);
		break;

	case HCI_EV_HARDWARE_ERROR:
		hci_hardware_error_evt(hdev, skb);
		break;

	case HCI_EV_ROLE_CHANGE:
		hci_role_change_evt(hdev, skb);
		break;

	case HCI_EV_NUM_COMP_PKTS:
		hci_num_comp_pkts_evt(hdev, skb);
		break;

	case HCI_EV_MODE_CHANGE:
		hci_mode_change_evt(hdev, skb);
		break;

	case HCI_EV_PIN_CODE_REQ:
		hci_pin_code_request_evt(hdev, skb);
		break;

	case HCI_EV_LINK_KEY_REQ:
		hci_link_key_request_evt(hdev, skb);
		break;

	case HCI_EV_LINK_KEY_NOTIFY:
		hci_link_key_notify_evt(hdev, skb);
		break;

	case HCI_EV_CLOCK_OFFSET:
		hci_clock_offset_evt(hdev, skb);
		break;

	case HCI_EV_PKT_TYPE_CHANGE:
		hci_pkt_type_change_evt(hdev, skb);
		break;

	case HCI_EV_PSCAN_REP_MODE:
		hci_pscan_rep_mode_evt(hdev, skb);
		break;

	case HCI_EV_INQUIRY_RESULT_WITH_RSSI:
		hci_inquiry_result_with_rssi_evt(hdev, skb);
		break;

	case HCI_EV_REMOTE_EXT_FEATURES:
		hci_remote_ext_features_evt(hdev, skb);
		break;

	case HCI_EV_SYNC_CONN_COMPLETE:
		hci_sync_conn_complete_evt(hdev, skb);
		break;

	case HCI_EV_EXTENDED_INQUIRY_RESULT:
		hci_extended_inquiry_result_evt(hdev, skb);
		break;

	case HCI_EV_KEY_REFRESH_COMPLETE:
		hci_key_refresh_complete_evt(hdev, skb);
		break;

	case HCI_EV_IO_CAPA_REQUEST:
		hci_io_capa_request_evt(hdev, skb);
		break;

	case HCI_EV_IO_CAPA_REPLY:
		hci_io_capa_reply_evt(hdev, skb);
		break;

	case HCI_EV_USER_CONFIRM_REQUEST:
		hci_user_confirm_request_evt(hdev, skb);
		break;

	case HCI_EV_USER_PASSKEY_REQUEST:
		hci_user_passkey_request_evt(hdev, skb);
		break;

	case HCI_EV_USER_PASSKEY_NOTIFY:
		hci_user_passkey_notify_evt(hdev, skb);
		break;

	case HCI_EV_KEYPRESS_NOTIFY:
		hci_keypress_notify_evt(hdev, skb);
		break;

	case HCI_EV_SIMPLE_PAIR_COMPLETE:
		hci_simple_pair_complete_evt(hdev, skb);
		break;

	case HCI_EV_REMOTE_HOST_FEATURES:
		hci_remote_host_features_evt(hdev, skb);
		break;

	case HCI_EV_LE_META:
		hci_le_meta_evt(hdev, skb);
		break;

	case HCI_EV_REMOTE_OOB_DATA_REQUEST:
		hci_remote_oob_data_request_evt(hdev, skb);
		break;

#if IS_ENABLED(CONFIG_BT_HS)
	case HCI_EV_CHANNEL_SELECTED:
		hci_chan_selected_evt(hdev, skb);
		break;

	case HCI_EV_PHY_LINK_COMPLETE:
		hci_phy_link_complete_evt(hdev, skb);
		break;

	case HCI_EV_LOGICAL_LINK_COMPLETE:
		hci_loglink_complete_evt(hdev, skb);
		break;

	case HCI_EV_DISCONN_LOGICAL_LINK_COMPLETE:
		hci_disconn_loglink_complete_evt(hdev, skb);
		break;

	case HCI_EV_DISCONN_PHY_LINK_COMPLETE:
		hci_disconn_phylink_complete_evt(hdev, skb);
		break;
#endif

	case HCI_EV_NUM_COMP_BLOCKS:
		hci_num_comp_blocks_evt(hdev, skb);
		break;

	case HCI_EV_VENDOR:
		msft_vendor_evt(hdev, skb);
		break;

	default:
		BT_DBG("%s event 0x%2.2x", hdev->name, event);
		break;
	}

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
