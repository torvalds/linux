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

/* Handle HCI Event packets */

static void hci_cc_inquiry_cancel(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (status)
		return;

	clear_bit(HCI_INQUIRY, &hdev->flags);

	hci_req_complete(hdev, status);

	hci_conn_check_pending(hdev);
}

static void hci_cc_exit_periodic_inq(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (status)
		return;

	clear_bit(HCI_INQUIRY, &hdev->flags);

	hci_conn_check_pending(hdev);
}

static void hci_cc_remote_name_req_cancel(struct hci_dev *hdev, struct sk_buff *skb)
{
	BT_DBG("%s", hdev->name);
}

static void hci_cc_role_discovery(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_role_discovery *rp = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rp->handle));
	if (conn) {
		if (rp->role)
			conn->link_mode &= ~HCI_LM_MASTER;
		else
			conn->link_mode |= HCI_LM_MASTER;
	}

	hci_dev_unlock(hdev);
}

static void hci_cc_read_link_policy(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_link_policy *rp = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

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

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

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

static void hci_cc_read_def_link_policy(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_def_link_policy *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->link_policy = __le16_to_cpu(rp->policy);
}

static void hci_cc_write_def_link_policy(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_DEF_LINK_POLICY);
	if (!sent)
		return;

	if (!status)
		hdev->link_policy = get_unaligned_le16(sent);

	hci_req_complete(hdev, status);
}

static void hci_cc_reset(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%x", hdev->name, status);

	hci_req_complete(hdev, status);
}

static void hci_cc_write_local_name(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_LOCAL_NAME);
	if (!sent)
		return;

	memcpy(hdev->dev_name, sent, 248);
}

static void hci_cc_read_local_name(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_local_name *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	memcpy(hdev->dev_name, rp->name, 248);
}

static void hci_cc_write_auth_enable(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_AUTH_ENABLE);
	if (!sent)
		return;

	if (!status) {
		__u8 param = *((__u8 *) sent);

		if (param == AUTH_ENABLED)
			set_bit(HCI_AUTH, &hdev->flags);
		else
			clear_bit(HCI_AUTH, &hdev->flags);
	}

	hci_req_complete(hdev, status);
}

static void hci_cc_write_encrypt_mode(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_ENCRYPT_MODE);
	if (!sent)
		return;

	if (!status) {
		__u8 param = *((__u8 *) sent);

		if (param)
			set_bit(HCI_ENCRYPT, &hdev->flags);
		else
			clear_bit(HCI_ENCRYPT, &hdev->flags);
	}

	hci_req_complete(hdev, status);
}

static void hci_cc_write_scan_enable(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%x", hdev->name, status);

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SCAN_ENABLE);
	if (!sent)
		return;

	if (!status) {
		__u8 param = *((__u8 *) sent);

		clear_bit(HCI_PSCAN, &hdev->flags);
		clear_bit(HCI_ISCAN, &hdev->flags);

		if (param & SCAN_INQUIRY)
			set_bit(HCI_ISCAN, &hdev->flags);

		if (param & SCAN_PAGE)
			set_bit(HCI_PSCAN, &hdev->flags);
	}

	hci_req_complete(hdev, status);
}

static void hci_cc_read_class_of_dev(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_class_of_dev *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

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

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_CLASS_OF_DEV);
	if (!sent)
		return;

	memcpy(hdev->dev_class, sent, 3);
}

static void hci_cc_read_voice_setting(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_voice_setting *rp = (void *) skb->data;
	__u16 setting;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	setting = __le16_to_cpu(rp->voice_setting);

	if (hdev->voice_setting == setting)
		return;

	hdev->voice_setting = setting;

	BT_DBG("%s voice setting 0x%04x", hdev->name, setting);

	if (hdev->notify) {
		tasklet_disable(&hdev->tx_task);
		hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);
		tasklet_enable(&hdev->tx_task);
	}
}

static void hci_cc_write_voice_setting(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	__u16 setting;
	void *sent;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_VOICE_SETTING);
	if (!sent)
		return;

	setting = get_unaligned_le16(sent);

	if (hdev->voice_setting == setting)
		return;

	hdev->voice_setting = setting;

	BT_DBG("%s voice setting 0x%04x", hdev->name, setting);

	if (hdev->notify) {
		tasklet_disable(&hdev->tx_task);
		hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);
		tasklet_enable(&hdev->tx_task);
	}
}

static void hci_cc_host_buffer_size(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status 0x%x", hdev->name, status);

	hci_req_complete(hdev, status);
}

static void hci_cc_read_ssp_mode(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_ssp_mode *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->ssp_mode = rp->mode;
}

static void hci_cc_write_ssp_mode(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);
	void *sent;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (status)
		return;

	sent = hci_sent_cmd_data(hdev, HCI_OP_WRITE_SSP_MODE);
	if (!sent)
		return;

	hdev->ssp_mode = *((__u8 *) sent);
}

static void hci_cc_read_local_version(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_local_version *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	hdev->hci_ver = rp->hci_ver;
	hdev->hci_rev = __le16_to_cpu(rp->hci_rev);
	hdev->manufacturer = __le16_to_cpu(rp->manufacturer);

	BT_DBG("%s manufacturer %d hci ver %d:%d", hdev->name,
					hdev->manufacturer,
					hdev->hci_ver, hdev->hci_rev);
}

static void hci_cc_read_local_commands(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_local_commands *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	memcpy(hdev->commands, rp->commands, sizeof(hdev->commands));
}

static void hci_cc_read_local_features(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_local_features *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (rp->status)
		return;

	memcpy(hdev->features, rp->features, 8);

	/* Adjust default settings according to features
	 * supported by device. */

	if (hdev->features[0] & LMP_3SLOT)
		hdev->pkt_type |= (HCI_DM3 | HCI_DH3);

	if (hdev->features[0] & LMP_5SLOT)
		hdev->pkt_type |= (HCI_DM5 | HCI_DH5);

	if (hdev->features[1] & LMP_HV2) {
		hdev->pkt_type  |= (HCI_HV2);
		hdev->esco_type |= (ESCO_HV2);
	}

	if (hdev->features[1] & LMP_HV3) {
		hdev->pkt_type  |= (HCI_HV3);
		hdev->esco_type |= (ESCO_HV3);
	}

	if (hdev->features[3] & LMP_ESCO)
		hdev->esco_type |= (ESCO_EV3);

	if (hdev->features[4] & LMP_EV4)
		hdev->esco_type |= (ESCO_EV4);

	if (hdev->features[4] & LMP_EV5)
		hdev->esco_type |= (ESCO_EV5);

	if (hdev->features[5] & LMP_EDR_ESCO_2M)
		hdev->esco_type |= (ESCO_2EV3);

	if (hdev->features[5] & LMP_EDR_ESCO_3M)
		hdev->esco_type |= (ESCO_3EV3);

	if (hdev->features[5] & LMP_EDR_3S_ESCO)
		hdev->esco_type |= (ESCO_2EV5 | ESCO_3EV5);

	BT_DBG("%s features 0x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x", hdev->name,
					hdev->features[0], hdev->features[1],
					hdev->features[2], hdev->features[3],
					hdev->features[4], hdev->features[5],
					hdev->features[6], hdev->features[7]);
}

static void hci_cc_read_buffer_size(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_buffer_size *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

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

	BT_DBG("%s acl mtu %d:%d sco mtu %d:%d", hdev->name,
					hdev->acl_mtu, hdev->acl_pkts,
					hdev->sco_mtu, hdev->sco_pkts);
}

static void hci_cc_read_bd_addr(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_rp_read_bd_addr *rp = (void *) skb->data;

	BT_DBG("%s status 0x%x", hdev->name, rp->status);

	if (!rp->status)
		bacpy(&hdev->bdaddr, &rp->bdaddr);

	hci_req_complete(hdev, rp->status);
}

static inline void hci_cs_inquiry(struct hci_dev *hdev, __u8 status)
{
	BT_DBG("%s status 0x%x", hdev->name, status);

	if (status) {
		hci_req_complete(hdev, status);

		hci_conn_check_pending(hdev);
	} else
		set_bit(HCI_INQUIRY, &hdev->flags);
}

static inline void hci_cs_create_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_create_conn *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	cp = hci_sent_cmd_data(hdev, HCI_OP_CREATE_CONN);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);

	BT_DBG("%s bdaddr %s conn %p", hdev->name, batostr(&cp->bdaddr), conn);

	if (status) {
		if (conn && conn->state == BT_CONNECT) {
			if (status != 0x0c || conn->attempt > 2) {
				conn->state = BT_CLOSED;
				hci_proto_connect_cfm(conn, status);
				hci_conn_del(conn);
			} else
				conn->state = BT_CONNECT2;
		}
	} else {
		if (!conn) {
			conn = hci_conn_add(hdev, ACL_LINK, &cp->bdaddr);
			if (conn) {
				conn->out = 1;
				conn->link_mode |= HCI_LM_MASTER;
			} else
				BT_ERR("No memory for new connection");
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_add_sco(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_add_sco *cp;
	struct hci_conn *acl, *sco;
	__u16 handle;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_ADD_SCO);
	if (!cp)
		return;

	handle = __le16_to_cpu(cp->handle);

	BT_DBG("%s handle %d", hdev->name, handle);

	hci_dev_lock(hdev);

	acl = hci_conn_hash_lookup_handle(hdev, handle);
	if (acl && (sco = acl->link)) {
		sco->state = BT_CLOSED;

		hci_proto_connect_cfm(sco, status);
		hci_conn_del(sco);
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_auth_requested(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_auth_requested *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_AUTH_REQUESTED);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_proto_connect_cfm(conn, status);
			hci_conn_put(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_set_conn_encrypt(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_set_conn_encrypt *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SET_CONN_ENCRYPT);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_proto_connect_cfm(conn, status);
			hci_conn_put(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static int hci_outgoing_auth_needed(struct hci_dev *hdev,
						struct hci_conn *conn)
{
	if (conn->state != BT_CONFIG || !conn->out)
		return 0;

	if (conn->sec_level == BT_SECURITY_SDP)
		return 0;

	/* Only request authentication for SSP connections or non-SSP
	 * devices with sec_level HIGH */
	if (!(hdev->ssp_mode > 0 && conn->ssp_mode > 0) &&
					conn->sec_level != BT_SECURITY_HIGH)
		return 0;

	return 1;
}

static void hci_cs_remote_name_req(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_remote_name_req *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	/* If successful wait for the name req complete event before
	 * checking for the need to do authentication */
	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_REMOTE_NAME_REQ);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);
	if (conn && hci_outgoing_auth_needed(hdev, conn)) {
		struct hci_cp_auth_requested cp;
		cp.handle = __cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_AUTH_REQUESTED, sizeof(cp), &cp);
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_read_remote_features(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_read_remote_features *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_READ_REMOTE_FEATURES);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_proto_connect_cfm(conn, status);
			hci_conn_put(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_read_remote_ext_features(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_read_remote_ext_features *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_READ_REMOTE_EXT_FEATURES);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		if (conn->state == BT_CONFIG) {
			hci_proto_connect_cfm(conn, status);
			hci_conn_put(conn);
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_setup_sync_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_setup_sync_conn *cp;
	struct hci_conn *acl, *sco;
	__u16 handle;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SETUP_SYNC_CONN);
	if (!cp)
		return;

	handle = __le16_to_cpu(cp->handle);

	BT_DBG("%s handle %d", hdev->name, handle);

	hci_dev_lock(hdev);

	acl = hci_conn_hash_lookup_handle(hdev, handle);
	if (acl && (sco = acl->link)) {
		sco->state = BT_CLOSED;

		hci_proto_connect_cfm(sco, status);
		hci_conn_del(sco);
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_sniff_mode(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_sniff_mode *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_SNIFF_MODE);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		clear_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend);

		if (test_and_clear_bit(HCI_CONN_SCO_SETUP_PEND, &conn->pend))
			hci_sco_setup(conn, status);
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_exit_sniff_mode(struct hci_dev *hdev, __u8 status)
{
	struct hci_cp_exit_sniff_mode *cp;
	struct hci_conn *conn;

	BT_DBG("%s status 0x%x", hdev->name, status);

	if (!status)
		return;

	cp = hci_sent_cmd_data(hdev, HCI_OP_EXIT_SNIFF_MODE);
	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
	if (conn) {
		clear_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend);

		if (test_and_clear_bit(HCI_CONN_SCO_SETUP_PEND, &conn->pend))
			hci_sco_setup(conn, status);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_inquiry_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status %d", hdev->name, status);

	clear_bit(HCI_INQUIRY, &hdev->flags);

	hci_req_complete(hdev, status);

	hci_conn_check_pending(hdev);
}

static inline void hci_inquiry_result_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct inquiry_data data;
	struct inquiry_info *info = (void *) (skb->data + 1);
	int num_rsp = *((__u8 *) skb->data);

	BT_DBG("%s num_rsp %d", hdev->name, num_rsp);

	if (!num_rsp)
		return;

	hci_dev_lock(hdev);

	for (; num_rsp; num_rsp--) {
		bacpy(&data.bdaddr, &info->bdaddr);
		data.pscan_rep_mode	= info->pscan_rep_mode;
		data.pscan_period_mode	= info->pscan_period_mode;
		data.pscan_mode		= info->pscan_mode;
		memcpy(data.dev_class, info->dev_class, 3);
		data.clock_offset	= info->clock_offset;
		data.rssi		= 0x00;
		data.ssp_mode		= 0x00;
		info++;
		hci_inquiry_cache_update(hdev, &data);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_conn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_conn_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
	if (!conn) {
		if (ev->link_type != SCO_LINK)
			goto unlock;

		conn = hci_conn_hash_lookup_ba(hdev, ESCO_LINK, &ev->bdaddr);
		if (!conn)
			goto unlock;

		conn->type = SCO_LINK;
	}

	if (!ev->status) {
		conn->handle = __le16_to_cpu(ev->handle);

		if (conn->type == ACL_LINK) {
			conn->state = BT_CONFIG;
			hci_conn_hold(conn);
			conn->disc_timeout = HCI_DISCONN_TIMEOUT;
		} else
			conn->state = BT_CONNECTED;

		hci_conn_hold_device(conn);
		hci_conn_add_sysfs(conn);

		if (test_bit(HCI_AUTH, &hdev->flags))
			conn->link_mode |= HCI_LM_AUTH;

		if (test_bit(HCI_ENCRYPT, &hdev->flags))
			conn->link_mode |= HCI_LM_ENCRYPT;

		/* Get remote features */
		if (conn->type == ACL_LINK) {
			struct hci_cp_read_remote_features cp;
			cp.handle = ev->handle;
			hci_send_cmd(hdev, HCI_OP_READ_REMOTE_FEATURES,
							sizeof(cp), &cp);
		}

		/* Set packet type for incoming connection */
		if (!conn->out && hdev->hci_ver < 3) {
			struct hci_cp_change_conn_ptype cp;
			cp.handle = ev->handle;
			cp.pkt_type = cpu_to_le16(conn->pkt_type);
			hci_send_cmd(hdev, HCI_OP_CHANGE_CONN_PTYPE,
							sizeof(cp), &cp);
		}
	} else
		conn->state = BT_CLOSED;

	if (conn->type == ACL_LINK)
		hci_sco_setup(conn, ev->status);

	if (ev->status) {
		hci_proto_connect_cfm(conn, ev->status);
		hci_conn_del(conn);
	} else if (ev->link_type != ACL_LINK)
		hci_proto_connect_cfm(conn, ev->status);

unlock:
	hci_dev_unlock(hdev);

	hci_conn_check_pending(hdev);
}

static inline void hci_conn_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_conn_request *ev = (void *) skb->data;
	int mask = hdev->link_mode;

	BT_DBG("%s bdaddr %s type 0x%x", hdev->name,
					batostr(&ev->bdaddr), ev->link_type);

	mask |= hci_proto_connect_ind(hdev, &ev->bdaddr, ev->link_type);

	if ((mask & HCI_LM_ACCEPT) && !hci_blacklist_lookup(hdev, &ev->bdaddr)) {
		/* Connection accepted */
		struct inquiry_entry *ie;
		struct hci_conn *conn;

		hci_dev_lock(hdev);

		ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
		if (ie)
			memcpy(ie->data.dev_class, ev->dev_class, 3);

		conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
		if (!conn) {
			conn = hci_conn_add(hdev, ev->link_type, &ev->bdaddr);
			if (!conn) {
				BT_ERR("No memory for new connection");
				hci_dev_unlock(hdev);
				return;
			}
		}

		memcpy(conn->dev_class, ev->dev_class, 3);
		conn->state = BT_CONNECT;

		hci_dev_unlock(hdev);

		if (ev->link_type == ACL_LINK || !lmp_esco_capable(hdev)) {
			struct hci_cp_accept_conn_req cp;

			bacpy(&cp.bdaddr, &ev->bdaddr);

			if (lmp_rswitch_capable(hdev) && (mask & HCI_LM_MASTER))
				cp.role = 0x00; /* Become master */
			else
				cp.role = 0x01; /* Remain slave */

			hci_send_cmd(hdev, HCI_OP_ACCEPT_CONN_REQ,
							sizeof(cp), &cp);
		} else {
			struct hci_cp_accept_sync_conn_req cp;

			bacpy(&cp.bdaddr, &ev->bdaddr);
			cp.pkt_type = cpu_to_le16(conn->pkt_type);

			cp.tx_bandwidth   = cpu_to_le32(0x00001f40);
			cp.rx_bandwidth   = cpu_to_le32(0x00001f40);
			cp.max_latency    = cpu_to_le16(0xffff);
			cp.content_format = cpu_to_le16(hdev->voice_setting);
			cp.retrans_effort = 0xff;

			hci_send_cmd(hdev, HCI_OP_ACCEPT_SYNC_CONN_REQ,
							sizeof(cp), &cp);
		}
	} else {
		/* Connection rejected */
		struct hci_cp_reject_conn_req cp;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		cp.reason = 0x0f;
		hci_send_cmd(hdev, HCI_OP_REJECT_CONN_REQ, sizeof(cp), &cp);
	}
}

static inline void hci_disconn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_disconn_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	if (ev->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		conn->state = BT_CLOSED;

		hci_proto_disconn_cfm(conn, ev->reason);
		hci_conn_del(conn);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_auth_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_auth_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status)
			conn->link_mode |= HCI_LM_AUTH;
		else
			conn->sec_level = BT_SECURITY_LOW;

		clear_bit(HCI_CONN_AUTH_PEND, &conn->pend);

		if (conn->state == BT_CONFIG) {
			if (!ev->status && hdev->ssp_mode > 0 &&
							conn->ssp_mode > 0) {
				struct hci_cp_set_conn_encrypt cp;
				cp.handle  = ev->handle;
				cp.encrypt = 0x01;
				hci_send_cmd(hdev, HCI_OP_SET_CONN_ENCRYPT,
							sizeof(cp), &cp);
			} else {
				conn->state = BT_CONNECTED;
				hci_proto_connect_cfm(conn, ev->status);
				hci_conn_put(conn);
			}
		} else {
			hci_auth_cfm(conn, ev->status);

			hci_conn_hold(conn);
			conn->disc_timeout = HCI_DISCONN_TIMEOUT;
			hci_conn_put(conn);
		}

		if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend)) {
			if (!ev->status) {
				struct hci_cp_set_conn_encrypt cp;
				cp.handle  = ev->handle;
				cp.encrypt = 0x01;
				hci_send_cmd(hdev, HCI_OP_SET_CONN_ENCRYPT,
							sizeof(cp), &cp);
			} else {
				clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend);
				hci_encrypt_cfm(conn, ev->status, 0x00);
			}
		}
	}

	hci_dev_unlock(hdev);
}

static inline void hci_remote_name_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_remote_name *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_conn_check_pending(hdev);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn && hci_outgoing_auth_needed(hdev, conn)) {
		struct hci_cp_auth_requested cp;
		cp.handle = __cpu_to_le16(conn->handle);
		hci_send_cmd(hdev, HCI_OP_AUTH_REQUESTED, sizeof(cp), &cp);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_encrypt_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_encrypt_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status) {
			if (ev->encrypt) {
				/* Encryption implies authentication */
				conn->link_mode |= HCI_LM_AUTH;
				conn->link_mode |= HCI_LM_ENCRYPT;
			} else
				conn->link_mode &= ~HCI_LM_ENCRYPT;
		}

		clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend);

		if (conn->state == BT_CONFIG) {
			if (!ev->status)
				conn->state = BT_CONNECTED;

			hci_proto_connect_cfm(conn, ev->status);
			hci_conn_put(conn);
		} else
			hci_encrypt_cfm(conn, ev->status, ev->encrypt);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_change_link_key_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_change_link_key_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status)
			conn->link_mode |= HCI_LM_SECURE;

		clear_bit(HCI_CONN_AUTH_PEND, &conn->pend);

		hci_key_change_cfm(conn, ev->status);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_remote_features_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_remote_features *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (!ev->status)
		memcpy(conn->features, ev->features, 8);

	if (conn->state != BT_CONFIG)
		goto unlock;

	if (!ev->status && lmp_ssp_capable(hdev) && lmp_ssp_capable(conn)) {
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
	}

	if (!hci_outgoing_auth_needed(hdev, conn)) {
		conn->state = BT_CONNECTED;
		hci_proto_connect_cfm(conn, ev->status);
		hci_conn_put(conn);
	}

unlock:
	hci_dev_unlock(hdev);
}

static inline void hci_remote_version_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	BT_DBG("%s", hdev->name);
}

static inline void hci_qos_setup_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	BT_DBG("%s", hdev->name);
}

static inline void hci_cmd_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_cmd_complete *ev = (void *) skb->data;
	__u16 opcode;

	skb_pull(skb, sizeof(*ev));

	opcode = __le16_to_cpu(ev->opcode);

	switch (opcode) {
	case HCI_OP_INQUIRY_CANCEL:
		hci_cc_inquiry_cancel(hdev, skb);
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

	case HCI_OP_HOST_BUFFER_SIZE:
		hci_cc_host_buffer_size(hdev, skb);
		break;

	case HCI_OP_READ_SSP_MODE:
		hci_cc_read_ssp_mode(hdev, skb);
		break;

	case HCI_OP_WRITE_SSP_MODE:
		hci_cc_write_ssp_mode(hdev, skb);
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

	case HCI_OP_READ_BUFFER_SIZE:
		hci_cc_read_buffer_size(hdev, skb);
		break;

	case HCI_OP_READ_BD_ADDR:
		hci_cc_read_bd_addr(hdev, skb);
		break;

	default:
		BT_DBG("%s opcode 0x%x", hdev->name, opcode);
		break;
	}

	if (ev->ncmd) {
		atomic_set(&hdev->cmd_cnt, 1);
		if (!skb_queue_empty(&hdev->cmd_q))
			tasklet_schedule(&hdev->cmd_task);
	}
}

static inline void hci_cmd_status_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_cmd_status *ev = (void *) skb->data;
	__u16 opcode;

	skb_pull(skb, sizeof(*ev));

	opcode = __le16_to_cpu(ev->opcode);

	switch (opcode) {
	case HCI_OP_INQUIRY:
		hci_cs_inquiry(hdev, ev->status);
		break;

	case HCI_OP_CREATE_CONN:
		hci_cs_create_conn(hdev, ev->status);
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

	default:
		BT_DBG("%s opcode 0x%x", hdev->name, opcode);
		break;
	}

	if (ev->ncmd) {
		atomic_set(&hdev->cmd_cnt, 1);
		if (!skb_queue_empty(&hdev->cmd_q))
			tasklet_schedule(&hdev->cmd_task);
	}
}

static inline void hci_role_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_role_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn) {
		if (!ev->status) {
			if (ev->role)
				conn->link_mode &= ~HCI_LM_MASTER;
			else
				conn->link_mode |= HCI_LM_MASTER;
		}

		clear_bit(HCI_CONN_RSWITCH_PEND, &conn->pend);

		hci_role_switch_cfm(conn, ev->status, ev->role);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_num_comp_pkts_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_num_comp_pkts *ev = (void *) skb->data;
	__le16 *ptr;
	int i;

	skb_pull(skb, sizeof(*ev));

	BT_DBG("%s num_hndl %d", hdev->name, ev->num_hndl);

	if (skb->len < ev->num_hndl * 4) {
		BT_DBG("%s bad parameters", hdev->name);
		return;
	}

	tasklet_disable(&hdev->tx_task);

	for (i = 0, ptr = (__le16 *) skb->data; i < ev->num_hndl; i++) {
		struct hci_conn *conn;
		__u16  handle, count;

		handle = get_unaligned_le16(ptr++);
		count  = get_unaligned_le16(ptr++);

		conn = hci_conn_hash_lookup_handle(hdev, handle);
		if (conn) {
			conn->sent -= count;

			if (conn->type == ACL_LINK) {
				if ((hdev->acl_cnt += count) > hdev->acl_pkts)
					hdev->acl_cnt = hdev->acl_pkts;
			} else {
				if ((hdev->sco_cnt += count) > hdev->sco_pkts)
					hdev->sco_cnt = hdev->sco_pkts;
			}
		}
	}

	tasklet_schedule(&hdev->tx_task);

	tasklet_enable(&hdev->tx_task);
}

static inline void hci_mode_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_mode_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		conn->mode = ev->mode;
		conn->interval = __le16_to_cpu(ev->interval);

		if (!test_and_clear_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend)) {
			if (conn->mode == HCI_CM_ACTIVE)
				conn->power_save = 1;
			else
				conn->power_save = 0;
		}

		if (test_and_clear_bit(HCI_CONN_SCO_SETUP_PEND, &conn->pend))
			hci_sco_setup(conn, ev->status);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_pin_code_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_pin_code_req *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn && conn->state == BT_CONNECTED) {
		hci_conn_hold(conn);
		conn->disc_timeout = HCI_PAIRING_TIMEOUT;
		hci_conn_put(conn);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_link_key_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	BT_DBG("%s", hdev->name);
}

static inline void hci_link_key_notify_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_link_key_notify *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn) {
		hci_conn_hold(conn);
		conn->disc_timeout = HCI_DISCONN_TIMEOUT;
		hci_conn_put(conn);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_clock_offset_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_clock_offset *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

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

static inline void hci_pkt_type_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_pkt_type_change *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn && !ev->status)
		conn->pkt_type = __le16_to_cpu(ev->pkt_type);

	hci_dev_unlock(hdev);
}

static inline void hci_pscan_rep_mode_evt(struct hci_dev *hdev, struct sk_buff *skb)
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

static inline void hci_inquiry_result_with_rssi_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct inquiry_data data;
	int num_rsp = *((__u8 *) skb->data);

	BT_DBG("%s num_rsp %d", hdev->name, num_rsp);

	if (!num_rsp)
		return;

	hci_dev_lock(hdev);

	if ((skb->len - 1) / num_rsp != sizeof(struct inquiry_info_with_rssi)) {
		struct inquiry_info_with_rssi_and_pscan_mode *info = (void *) (skb->data + 1);

		for (; num_rsp; num_rsp--) {
			bacpy(&data.bdaddr, &info->bdaddr);
			data.pscan_rep_mode	= info->pscan_rep_mode;
			data.pscan_period_mode	= info->pscan_period_mode;
			data.pscan_mode		= info->pscan_mode;
			memcpy(data.dev_class, info->dev_class, 3);
			data.clock_offset	= info->clock_offset;
			data.rssi		= info->rssi;
			data.ssp_mode		= 0x00;
			info++;
			hci_inquiry_cache_update(hdev, &data);
		}
	} else {
		struct inquiry_info_with_rssi *info = (void *) (skb->data + 1);

		for (; num_rsp; num_rsp--) {
			bacpy(&data.bdaddr, &info->bdaddr);
			data.pscan_rep_mode	= info->pscan_rep_mode;
			data.pscan_period_mode	= info->pscan_period_mode;
			data.pscan_mode		= 0x00;
			memcpy(data.dev_class, info->dev_class, 3);
			data.clock_offset	= info->clock_offset;
			data.rssi		= info->rssi;
			data.ssp_mode		= 0x00;
			info++;
			hci_inquiry_cache_update(hdev, &data);
		}
	}

	hci_dev_unlock(hdev);
}

static inline void hci_remote_ext_features_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_remote_ext_features *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (!conn)
		goto unlock;

	if (!ev->status && ev->page == 0x01) {
		struct inquiry_entry *ie;

		ie = hci_inquiry_cache_lookup(hdev, &conn->dst);
		if (ie)
			ie->data.ssp_mode = (ev->features[0] & 0x01);

		conn->ssp_mode = (ev->features[0] & 0x01);
	}

	if (conn->state != BT_CONFIG)
		goto unlock;

	if (!ev->status) {
		struct hci_cp_remote_name_req cp;
		memset(&cp, 0, sizeof(cp));
		bacpy(&cp.bdaddr, &conn->dst);
		cp.pscan_rep_mode = 0x02;
		hci_send_cmd(hdev, HCI_OP_REMOTE_NAME_REQ, sizeof(cp), &cp);
	}

	if (!hci_outgoing_auth_needed(hdev, conn)) {
		conn->state = BT_CONNECTED;
		hci_proto_connect_cfm(conn, ev->status);
		hci_conn_put(conn);
	}

unlock:
	hci_dev_unlock(hdev);
}

static inline void hci_sync_conn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_sync_conn_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
	if (!conn) {
		if (ev->link_type == ESCO_LINK)
			goto unlock;

		conn = hci_conn_hash_lookup_ba(hdev, ESCO_LINK, &ev->bdaddr);
		if (!conn)
			goto unlock;

		conn->type = SCO_LINK;
	}

	switch (ev->status) {
	case 0x00:
		conn->handle = __le16_to_cpu(ev->handle);
		conn->state  = BT_CONNECTED;

		hci_conn_hold_device(conn);
		hci_conn_add_sysfs(conn);
		break;

	case 0x11:	/* Unsupported Feature or Parameter Value */
	case 0x1c:	/* SCO interval rejected */
	case 0x1a:	/* Unsupported Remote Feature */
	case 0x1f:	/* Unspecified error */
		if (conn->out && conn->attempt < 2) {
			conn->pkt_type = (hdev->esco_type & SCO_ESCO_MASK) |
					(hdev->esco_type & EDR_ESCO_MASK);
			hci_setup_sync(conn, conn->link->handle);
			goto unlock;
		}
		/* fall through */

	default:
		conn->state = BT_CLOSED;
		break;
	}

	hci_proto_connect_cfm(conn, ev->status);
	if (ev->status)
		hci_conn_del(conn);

unlock:
	hci_dev_unlock(hdev);
}

static inline void hci_sync_conn_changed_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	BT_DBG("%s", hdev->name);
}

static inline void hci_sniff_subrate_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_sniff_subrate *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
	}

	hci_dev_unlock(hdev);
}

static inline void hci_extended_inquiry_result_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct inquiry_data data;
	struct extended_inquiry_info *info = (void *) (skb->data + 1);
	int num_rsp = *((__u8 *) skb->data);

	BT_DBG("%s num_rsp %d", hdev->name, num_rsp);

	if (!num_rsp)
		return;

	hci_dev_lock(hdev);

	for (; num_rsp; num_rsp--) {
		bacpy(&data.bdaddr, &info->bdaddr);
		data.pscan_rep_mode     = info->pscan_rep_mode;
		data.pscan_period_mode  = info->pscan_period_mode;
		data.pscan_mode         = 0x00;
		memcpy(data.dev_class, info->dev_class, 3);
		data.clock_offset       = info->clock_offset;
		data.rssi               = info->rssi;
		data.ssp_mode		= 0x01;
		info++;
		hci_inquiry_cache_update(hdev, &data);
	}

	hci_dev_unlock(hdev);
}

static inline void hci_io_capa_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_io_capa_request *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn)
		hci_conn_hold(conn);

	hci_dev_unlock(hdev);
}

static inline void hci_simple_pair_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_simple_pair_complete *ev = (void *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &ev->bdaddr);
	if (conn)
		hci_conn_put(conn);

	hci_dev_unlock(hdev);
}

static inline void hci_remote_host_features_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_remote_host_features *ev = (void *) skb->data;
	struct inquiry_entry *ie;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr);
	if (ie)
		ie->data.ssp_mode = (ev->features[0] & 0x01);

	hci_dev_unlock(hdev);
}

void hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (void *) skb->data;
	__u8 event = hdr->evt;

	skb_pull(skb, HCI_EVENT_HDR_SIZE);

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

	case HCI_EV_REMOTE_VERSION:
		hci_remote_version_evt(hdev, skb);
		break;

	case HCI_EV_QOS_SETUP_COMPLETE:
		hci_qos_setup_complete_evt(hdev, skb);
		break;

	case HCI_EV_CMD_COMPLETE:
		hci_cmd_complete_evt(hdev, skb);
		break;

	case HCI_EV_CMD_STATUS:
		hci_cmd_status_evt(hdev, skb);
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

	case HCI_EV_SYNC_CONN_CHANGED:
		hci_sync_conn_changed_evt(hdev, skb);
		break;

	case HCI_EV_SNIFF_SUBRATE:
		hci_sniff_subrate_evt(hdev, skb);
		break;

	case HCI_EV_EXTENDED_INQUIRY_RESULT:
		hci_extended_inquiry_result_evt(hdev, skb);
		break;

	case HCI_EV_IO_CAPA_REQUEST:
		hci_io_capa_request_evt(hdev, skb);
		break;

	case HCI_EV_SIMPLE_PAIR_COMPLETE:
		hci_simple_pair_complete_evt(hdev, skb);
		break;

	case HCI_EV_REMOTE_HOST_FEATURES:
		hci_remote_host_features_evt(hdev, skb);
		break;

	default:
		BT_DBG("%s event 0x%x", hdev->name, event);
		break;
	}

	kfree_skb(skb);
	hdev->stat.evt_rx++;
}

/* Generate internal stack event */
void hci_si_event(struct hci_dev *hdev, int type, int dlen, void *data)
{
	struct hci_event_hdr *hdr;
	struct hci_ev_stack_internal *ev;
	struct sk_buff *skb;

	skb = bt_skb_alloc(HCI_EVENT_HDR_SIZE + sizeof(*ev) + dlen, GFP_ATOMIC);
	if (!skb)
		return;

	hdr = (void *) skb_put(skb, HCI_EVENT_HDR_SIZE);
	hdr->evt  = HCI_EV_STACK_INTERNAL;
	hdr->plen = sizeof(*ev) + dlen;

	ev  = (void *) skb_put(skb, sizeof(*ev) + dlen);
	ev->type = type;
	memcpy(ev->data, data, dlen);

	bt_cb(skb)->incoming = 1;
	__net_timestamp(skb);

	bt_cb(skb)->pkt_type = HCI_EVENT_PKT;
	skb->dev = (void *) hdev;
	hci_send_to_sock(hdev, skb);
	kfree_skb(skb);
}
