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

/* Bluetooth HCI event handling. */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
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

/* Handle HCI Event packets */

/* Command Complete OGF LINK_CTL  */
static void hci_cc_link_ctl(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	__u8 status;

	BT_DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_INQUIRY_CANCEL:
	case OCF_EXIT_PERIODIC_INQ:
		status = *((__u8 *) skb->data);

		if (status) {
			BT_DBG("%s Inquiry cancel error: status 0x%x", hdev->name, status);
		} else {
			clear_bit(HCI_INQUIRY, &hdev->flags);
			hci_req_complete(hdev, status);
		}
		break;

	default:
		BT_DBG("%s Command complete: ogf LINK_CTL ocf %x", hdev->name, ocf);
		break;
	}
}

/* Command Complete OGF LINK_POLICY  */
static void hci_cc_link_policy(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	struct hci_conn *conn;
	struct hci_rp_role_discovery *rd;
	struct hci_rp_write_link_policy *lp;
	void *sent;

	BT_DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_ROLE_DISCOVERY: 
		rd = (void *) skb->data;

		if (rd->status)
			break;

		hci_dev_lock(hdev);

		conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(rd->handle));
		if (conn) {
			if (rd->role)
				conn->link_mode &= ~HCI_LM_MASTER;
			else
				conn->link_mode |= HCI_LM_MASTER;
		}

		hci_dev_unlock(hdev);
		break;

	case OCF_WRITE_LINK_POLICY:
		sent = hci_sent_cmd_data(hdev, OGF_LINK_POLICY, OCF_WRITE_LINK_POLICY);
		if (!sent)
			break;

		lp = (struct hci_rp_write_link_policy *) skb->data;

		if (lp->status)
			break;

		hci_dev_lock(hdev);

		conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(lp->handle));
		if (conn) {
			__le16 policy = get_unaligned((__le16 *) (sent + 2));
			conn->link_policy = __le16_to_cpu(policy);
		}

		hci_dev_unlock(hdev);
		break;

	default:
		BT_DBG("%s: Command complete: ogf LINK_POLICY ocf %x", 
				hdev->name, ocf);
		break;
	}
}

/* Command Complete OGF HOST_CTL  */
static void hci_cc_host_ctl(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	__u8 status, param;
	__u16 setting;
	struct hci_rp_read_voice_setting *vs;
	void *sent;

	BT_DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_RESET:
		status = *((__u8 *) skb->data);
		hci_req_complete(hdev, status);
		break;

	case OCF_SET_EVENT_FLT:
		status = *((__u8 *) skb->data);
		if (status) {
			BT_DBG("%s SET_EVENT_FLT failed %d", hdev->name, status);
		} else {
			BT_DBG("%s SET_EVENT_FLT succeseful", hdev->name);
		}
		break;

	case OCF_WRITE_AUTH_ENABLE:
		sent = hci_sent_cmd_data(hdev, OGF_HOST_CTL, OCF_WRITE_AUTH_ENABLE);
		if (!sent)
			break;

		status = *((__u8 *) skb->data);
		param  = *((__u8 *) sent);

		if (!status) {
			if (param == AUTH_ENABLED)
				set_bit(HCI_AUTH, &hdev->flags);
			else
				clear_bit(HCI_AUTH, &hdev->flags);
		}
		hci_req_complete(hdev, status);
		break;

	case OCF_WRITE_ENCRYPT_MODE:
		sent = hci_sent_cmd_data(hdev, OGF_HOST_CTL, OCF_WRITE_ENCRYPT_MODE);
		if (!sent)
			break;

		status = *((__u8 *) skb->data);
		param  = *((__u8 *) sent);

		if (!status) {
			if (param)
				set_bit(HCI_ENCRYPT, &hdev->flags);
			else
				clear_bit(HCI_ENCRYPT, &hdev->flags);
		}
		hci_req_complete(hdev, status);
		break;

	case OCF_WRITE_CA_TIMEOUT:
		status = *((__u8 *) skb->data);
		if (status) {
			BT_DBG("%s OCF_WRITE_CA_TIMEOUT failed %d", hdev->name, status);
		} else {
			BT_DBG("%s OCF_WRITE_CA_TIMEOUT succeseful", hdev->name);
		}
		break;

	case OCF_WRITE_PG_TIMEOUT:
		status = *((__u8 *) skb->data);
		if (status) {
			BT_DBG("%s OCF_WRITE_PG_TIMEOUT failed %d", hdev->name, status);
		} else {
			BT_DBG("%s: OCF_WRITE_PG_TIMEOUT succeseful", hdev->name);
		}
		break;

	case OCF_WRITE_SCAN_ENABLE:
		sent = hci_sent_cmd_data(hdev, OGF_HOST_CTL, OCF_WRITE_SCAN_ENABLE);
		if (!sent)
			break;

		status = *((__u8 *) skb->data);
		param  = *((__u8 *) sent);

		BT_DBG("param 0x%x", param);

		if (!status) {
			clear_bit(HCI_PSCAN, &hdev->flags);
			clear_bit(HCI_ISCAN, &hdev->flags);
			if (param & SCAN_INQUIRY) 
				set_bit(HCI_ISCAN, &hdev->flags);

			if (param & SCAN_PAGE) 
				set_bit(HCI_PSCAN, &hdev->flags);
		}
		hci_req_complete(hdev, status);
		break;

	case OCF_READ_VOICE_SETTING:
		vs = (struct hci_rp_read_voice_setting *) skb->data;

		if (vs->status) {
			BT_DBG("%s READ_VOICE_SETTING failed %d", hdev->name, vs->status);
			break;
		}

		setting = __le16_to_cpu(vs->voice_setting);

		if (hdev->voice_setting != setting ) {
			hdev->voice_setting = setting;

			BT_DBG("%s: voice setting 0x%04x", hdev->name, setting);

			if (hdev->notify) {
				tasklet_disable(&hdev->tx_task);
				hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);
				tasklet_enable(&hdev->tx_task);
			}
		}
		break;

	case OCF_WRITE_VOICE_SETTING:
		sent = hci_sent_cmd_data(hdev, OGF_HOST_CTL, OCF_WRITE_VOICE_SETTING);
		if (!sent)
			break;

		status = *((__u8 *) skb->data);
		setting = __le16_to_cpu(get_unaligned((__le16 *) sent));

		if (!status && hdev->voice_setting != setting) {
			hdev->voice_setting = setting;

			BT_DBG("%s: voice setting 0x%04x", hdev->name, setting);

			if (hdev->notify) {
				tasklet_disable(&hdev->tx_task);
				hdev->notify(hdev, HCI_NOTIFY_VOICE_SETTING);
				tasklet_enable(&hdev->tx_task);
			}
		}
		hci_req_complete(hdev, status);
		break;

	case OCF_HOST_BUFFER_SIZE:
		status = *((__u8 *) skb->data);
		if (status) {
			BT_DBG("%s OCF_BUFFER_SIZE failed %d", hdev->name, status);
			hci_req_complete(hdev, status);
		}
		break;

	default:
		BT_DBG("%s Command complete: ogf HOST_CTL ocf %x", hdev->name, ocf);
		break;
	}
}

/* Command Complete OGF INFO_PARAM  */
static void hci_cc_info_param(struct hci_dev *hdev, __u16 ocf, struct sk_buff *skb)
{
	struct hci_rp_read_loc_version *lv;
	struct hci_rp_read_local_features *lf;
	struct hci_rp_read_buffer_size *bs;
	struct hci_rp_read_bd_addr *ba;

	BT_DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_READ_LOCAL_VERSION:
		lv = (struct hci_rp_read_loc_version *) skb->data;

		if (lv->status) {
			BT_DBG("%s READ_LOCAL_VERSION failed %d", hdev->name, lf->status);
			break;
		}

		hdev->hci_ver = lv->hci_ver;
		hdev->hci_rev = btohs(lv->hci_rev);
		hdev->manufacturer = btohs(lv->manufacturer);

		BT_DBG("%s: manufacturer %d hci_ver %d hci_rev %d", hdev->name,
				hdev->manufacturer, hdev->hci_ver, hdev->hci_rev);

		break;

	case OCF_READ_LOCAL_FEATURES:
		lf = (struct hci_rp_read_local_features *) skb->data;

		if (lf->status) {
			BT_DBG("%s READ_LOCAL_FEATURES failed %d", hdev->name, lf->status);
			break;
		}

		memcpy(hdev->features, lf->features, sizeof(hdev->features));

		/* Adjust default settings according to features 
		 * supported by device. */
		if (hdev->features[0] & LMP_3SLOT)
			hdev->pkt_type |= (HCI_DM3 | HCI_DH3);

		if (hdev->features[0] & LMP_5SLOT)
			hdev->pkt_type |= (HCI_DM5 | HCI_DH5);

		if (hdev->features[1] & LMP_HV2)
			hdev->pkt_type |= (HCI_HV2);

		if (hdev->features[1] & LMP_HV3)
			hdev->pkt_type |= (HCI_HV3);

		BT_DBG("%s: features 0x%x 0x%x 0x%x", hdev->name,
				lf->features[0], lf->features[1], lf->features[2]);

		break;

	case OCF_READ_BUFFER_SIZE:
		bs = (struct hci_rp_read_buffer_size *) skb->data;

		if (bs->status) {
			BT_DBG("%s READ_BUFFER_SIZE failed %d", hdev->name, bs->status);
			hci_req_complete(hdev, bs->status);
			break;
		}

		hdev->acl_mtu  = __le16_to_cpu(bs->acl_mtu);
		hdev->sco_mtu  = bs->sco_mtu;
		hdev->acl_pkts = __le16_to_cpu(bs->acl_max_pkt);
		hdev->sco_pkts = __le16_to_cpu(bs->sco_max_pkt);

		if (test_bit(HCI_QUIRK_FIXUP_BUFFER_SIZE, &hdev->quirks)) {
			hdev->sco_mtu  = 64;
			hdev->sco_pkts = 8;
		}

		hdev->acl_cnt = hdev->acl_pkts;
		hdev->sco_cnt = hdev->sco_pkts;

		BT_DBG("%s mtu: acl %d, sco %d max_pkt: acl %d, sco %d", hdev->name,
			hdev->acl_mtu, hdev->sco_mtu, hdev->acl_pkts, hdev->sco_pkts);
		break;

	case OCF_READ_BD_ADDR:
		ba = (struct hci_rp_read_bd_addr *) skb->data;

		if (!ba->status) {
			bacpy(&hdev->bdaddr, &ba->bdaddr);
		} else {
			BT_DBG("%s: READ_BD_ADDR failed %d", hdev->name, ba->status);
		}

		hci_req_complete(hdev, ba->status);
		break;

	default:
		BT_DBG("%s Command complete: ogf INFO_PARAM ocf %x", hdev->name, ocf);
		break;
	}
}

/* Command Status OGF LINK_CTL  */
static inline void hci_cs_create_conn(struct hci_dev *hdev, __u8 status)
{
	struct hci_conn *conn;
	struct hci_cp_create_conn *cp = hci_sent_cmd_data(hdev, OGF_LINK_CTL, OCF_CREATE_CONN);

	if (!cp)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ACL_LINK, &cp->bdaddr);

	BT_DBG("%s status 0x%x bdaddr %s conn %p", hdev->name,
			status, batostr(&cp->bdaddr), conn);

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
				BT_ERR("No memmory for new connection");
		}
	}

	hci_dev_unlock(hdev);
}

static void hci_cs_link_ctl(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	BT_DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_CREATE_CONN:
		hci_cs_create_conn(hdev, status);
		break;

	case OCF_ADD_SCO:
		if (status) {
			struct hci_conn *acl, *sco;
			struct hci_cp_add_sco *cp = hci_sent_cmd_data(hdev, OGF_LINK_CTL, OCF_ADD_SCO);
			__u16 handle;

			if (!cp)
				break;

			handle = __le16_to_cpu(cp->handle);

			BT_DBG("%s Add SCO error: handle %d status 0x%x", hdev->name, handle, status);

			hci_dev_lock(hdev);

			acl = hci_conn_hash_lookup_handle(hdev, handle);
			if (acl && (sco = acl->link)) {
				sco->state = BT_CLOSED;

				hci_proto_connect_cfm(sco, status);
				hci_conn_del(sco);
			}

			hci_dev_unlock(hdev);
		}
		break;

	case OCF_INQUIRY:
		if (status) {
			BT_DBG("%s Inquiry error: status 0x%x", hdev->name, status);
			hci_req_complete(hdev, status);
		} else {
			set_bit(HCI_INQUIRY, &hdev->flags);
		}
		break;

	default:
		BT_DBG("%s Command status: ogf LINK_CTL ocf %x status %d", 
			hdev->name, ocf, status);
		break;
	}
}

/* Command Status OGF LINK_POLICY */
static void hci_cs_link_policy(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	BT_DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	case OCF_SNIFF_MODE:
		if (status) {
			struct hci_conn *conn;
			struct hci_cp_sniff_mode *cp = hci_sent_cmd_data(hdev, OGF_LINK_POLICY, OCF_SNIFF_MODE);

			if (!cp)
				break;

			hci_dev_lock(hdev);

			conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
			if (conn) {
				clear_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend);
			}

			hci_dev_unlock(hdev);
		}
		break;

	case OCF_EXIT_SNIFF_MODE:
		if (status) {
			struct hci_conn *conn;
			struct hci_cp_exit_sniff_mode *cp = hci_sent_cmd_data(hdev, OGF_LINK_POLICY, OCF_EXIT_SNIFF_MODE);

			if (!cp)
				break;

			hci_dev_lock(hdev);

			conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(cp->handle));
			if (conn) {
				clear_bit(HCI_CONN_MODE_CHANGE_PEND, &conn->pend);
			}

			hci_dev_unlock(hdev);
		}
		break;

	default:
		BT_DBG("%s Command status: ogf LINK_POLICY ocf %x", hdev->name, ocf);
		break;
	}
}

/* Command Status OGF HOST_CTL */
static void hci_cs_host_ctl(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	BT_DBG("%s ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	default:
		BT_DBG("%s Command status: ogf HOST_CTL ocf %x", hdev->name, ocf);
		break;
	}
}

/* Command Status OGF INFO_PARAM  */
static void hci_cs_info_param(struct hci_dev *hdev, __u16 ocf, __u8 status)
{
	BT_DBG("%s: hci_cs_info_param: ocf 0x%x", hdev->name, ocf);

	switch (ocf) {
	default:
		BT_DBG("%s Command status: ogf INFO_PARAM ocf %x", hdev->name, ocf);
		break;
	}
}

/* Inquiry Complete */
static inline void hci_inquiry_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	__u8 status = *((__u8 *) skb->data);

	BT_DBG("%s status %d", hdev->name, status);

	clear_bit(HCI_INQUIRY, &hdev->flags);
	hci_req_complete(hdev, status);
}

/* Inquiry Result */
static inline void hci_inquiry_result_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct inquiry_data data;
	struct inquiry_info *info = (struct inquiry_info *) (skb->data + 1);
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
		info++;
		hci_inquiry_cache_update(hdev, &data);
	}

	hci_dev_unlock(hdev);
}

/* Inquiry Result With RSSI */
static inline void hci_inquiry_result_with_rssi_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct inquiry_data data;
	int num_rsp = *((__u8 *) skb->data);

	BT_DBG("%s num_rsp %d", hdev->name, num_rsp);

	if (!num_rsp)
		return;

	hci_dev_lock(hdev);

	if ((skb->len - 1) / num_rsp != sizeof(struct inquiry_info_with_rssi)) {
		struct inquiry_info_with_rssi_and_pscan_mode *info =
			(struct inquiry_info_with_rssi_and_pscan_mode *) (skb->data + 1);

		for (; num_rsp; num_rsp--) {
			bacpy(&data.bdaddr, &info->bdaddr);
			data.pscan_rep_mode	= info->pscan_rep_mode;
			data.pscan_period_mode	= info->pscan_period_mode;
			data.pscan_mode		= info->pscan_mode;
			memcpy(data.dev_class, info->dev_class, 3);
			data.clock_offset	= info->clock_offset;
			data.rssi		= info->rssi;
			info++;
			hci_inquiry_cache_update(hdev, &data);
		}
	} else {
		struct inquiry_info_with_rssi *info =
			(struct inquiry_info_with_rssi *) (skb->data + 1);

		for (; num_rsp; num_rsp--) {
			bacpy(&data.bdaddr, &info->bdaddr);
			data.pscan_rep_mode	= info->pscan_rep_mode;
			data.pscan_period_mode	= info->pscan_period_mode;
			data.pscan_mode		= 0x00;
			memcpy(data.dev_class, info->dev_class, 3);
			data.clock_offset	= info->clock_offset;
			data.rssi		= info->rssi;
			info++;
			hci_inquiry_cache_update(hdev, &data);
		}
	}

	hci_dev_unlock(hdev);
}

/* Extended Inquiry Result */
static inline void hci_extended_inquiry_result_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct inquiry_data data;
	struct extended_inquiry_info *info = (struct extended_inquiry_info *) (skb->data + 1);
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
		info++;
		hci_inquiry_cache_update(hdev, &data);
	}

	hci_dev_unlock(hdev);
}

/* Connect Request */
static inline void hci_conn_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_conn_request *ev = (struct hci_ev_conn_request *) skb->data;
	int mask = hdev->link_mode;

	BT_DBG("%s Connection request: %s type 0x%x", hdev->name,
			batostr(&ev->bdaddr), ev->link_type);

	mask |= hci_proto_connect_ind(hdev, &ev->bdaddr, ev->link_type);

	if (mask & HCI_LM_ACCEPT) {
		/* Connection accepted */
		struct hci_conn *conn;
		struct hci_cp_accept_conn_req cp;

		hci_dev_lock(hdev);
		conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
		if (!conn) {
			if (!(conn = hci_conn_add(hdev, ev->link_type, &ev->bdaddr))) {
				BT_ERR("No memmory for new connection");
				hci_dev_unlock(hdev);
				return;
			}
		}
		memcpy(conn->dev_class, ev->dev_class, 3);
		conn->state = BT_CONNECT;
		hci_dev_unlock(hdev);

		bacpy(&cp.bdaddr, &ev->bdaddr);

		if (lmp_rswitch_capable(hdev) && (mask & HCI_LM_MASTER))
			cp.role = 0x00; /* Become master */
		else
			cp.role = 0x01; /* Remain slave */

		hci_send_cmd(hdev, OGF_LINK_CTL,
				OCF_ACCEPT_CONN_REQ, sizeof(cp), &cp);
	} else {
		/* Connection rejected */
		struct hci_cp_reject_conn_req cp;

		bacpy(&cp.bdaddr, &ev->bdaddr);
		cp.reason = 0x0f;
		hci_send_cmd(hdev, OGF_LINK_CTL,
				OCF_REJECT_CONN_REQ, sizeof(cp), &cp);
	}
}

/* Connect Complete */
static inline void hci_conn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_conn_complete *ev = (struct hci_ev_conn_complete *) skb->data;
	struct hci_conn *conn, *pend;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_ba(hdev, ev->link_type, &ev->bdaddr);
	if (!conn) {
		hci_dev_unlock(hdev);
		return;
	}

	if (!ev->status) {
		conn->handle = __le16_to_cpu(ev->handle);
		conn->state  = BT_CONNECTED;

		if (test_bit(HCI_AUTH, &hdev->flags))
			conn->link_mode |= HCI_LM_AUTH;

		if (test_bit(HCI_ENCRYPT, &hdev->flags))
			conn->link_mode |= HCI_LM_ENCRYPT;

		/* Get remote features */
		if (conn->type == ACL_LINK) {
			struct hci_cp_read_remote_features cp;
			cp.handle = ev->handle;
			hci_send_cmd(hdev, OGF_LINK_CTL,
				OCF_READ_REMOTE_FEATURES, sizeof(cp), &cp);
		}

		/* Set link policy */
		if (conn->type == ACL_LINK && hdev->link_policy) {
			struct hci_cp_write_link_policy cp;
			cp.handle = ev->handle;
			cp.policy = __cpu_to_le16(hdev->link_policy);
			hci_send_cmd(hdev, OGF_LINK_POLICY,
				OCF_WRITE_LINK_POLICY, sizeof(cp), &cp);
		}

		/* Set packet type for incoming connection */
		if (!conn->out) {
			struct hci_cp_change_conn_ptype cp;
			cp.handle = ev->handle;
			cp.pkt_type = (conn->type == ACL_LINK) ? 
				__cpu_to_le16(hdev->pkt_type & ACL_PTYPE_MASK):
				__cpu_to_le16(hdev->pkt_type & SCO_PTYPE_MASK);

			hci_send_cmd(hdev, OGF_LINK_CTL,
				OCF_CHANGE_CONN_PTYPE, sizeof(cp), &cp);
		} else {
			/* Update disconnect timer */
			hci_conn_hold(conn);
			hci_conn_put(conn);
		}
	} else
		conn->state = BT_CLOSED;

	if (conn->type == ACL_LINK) {
		struct hci_conn *sco = conn->link;
		if (sco) {
			if (!ev->status)
				hci_add_sco(sco, conn->handle);
			else {
				hci_proto_connect_cfm(sco, ev->status);
				hci_conn_del(sco);
			}
		}
	}

	hci_proto_connect_cfm(conn, ev->status);
	if (ev->status)
		hci_conn_del(conn);

	pend = hci_conn_hash_lookup_state(hdev, ACL_LINK, BT_CONNECT2);
	if (pend)
		hci_acl_connect(pend);

	hci_dev_unlock(hdev);
}

/* Disconnect Complete */
static inline void hci_disconn_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_disconn_complete *ev = (struct hci_ev_disconn_complete *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	if (ev->status)
		return;

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		conn->state = BT_CLOSED;
		hci_proto_disconn_ind(conn, ev->reason);
		hci_conn_del(conn);
	}

	hci_dev_unlock(hdev);
}

/* Number of completed packets */
static inline void hci_num_comp_pkts_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_num_comp_pkts *ev = (struct hci_ev_num_comp_pkts *) skb->data;
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

		handle = __le16_to_cpu(get_unaligned(ptr++));
		count  = __le16_to_cpu(get_unaligned(ptr++));

		conn = hci_conn_hash_lookup_handle(hdev, handle);
		if (conn) {
			conn->sent -= count;

			if (conn->type == SCO_LINK) {
				if ((hdev->sco_cnt += count) > hdev->sco_pkts)
					hdev->sco_cnt = hdev->sco_pkts;
			} else {
				if ((hdev->acl_cnt += count) > hdev->acl_pkts)
					hdev->acl_cnt = hdev->acl_pkts;
			}
		}
	}
	hci_sched_tx(hdev);

	tasklet_enable(&hdev->tx_task);
}

/* Role Change */
static inline void hci_role_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_role_change *ev = (struct hci_ev_role_change *) skb->data;
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

/* Mode Change */
static inline void hci_mode_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_mode_change *ev = (struct hci_ev_mode_change *) skb->data;
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
	}

	hci_dev_unlock(hdev);
}

/* Authentication Complete */
static inline void hci_auth_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_auth_complete *ev = (struct hci_ev_auth_complete *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status)
			conn->link_mode |= HCI_LM_AUTH;

		clear_bit(HCI_CONN_AUTH_PEND, &conn->pend);

		hci_auth_cfm(conn, ev->status);

		if (test_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend)) {
			if (!ev->status) {
				struct hci_cp_set_conn_encrypt cp;
				cp.handle  = __cpu_to_le16(conn->handle);
				cp.encrypt = 1;
				hci_send_cmd(conn->hdev, OGF_LINK_CTL,
					OCF_SET_CONN_ENCRYPT, sizeof(cp), &cp);
			} else {
				clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend);
				hci_encrypt_cfm(conn, ev->status, 0x00);
			}
		}
	}

	hci_dev_unlock(hdev);
}

/* Encryption Change */
static inline void hci_encrypt_change_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_encrypt_change *ev = (struct hci_ev_encrypt_change *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
		if (!ev->status) {
			if (ev->encrypt)
				conn->link_mode |= HCI_LM_ENCRYPT;
			else
				conn->link_mode &= ~HCI_LM_ENCRYPT;
		}

		clear_bit(HCI_CONN_ENCRYPT_PEND, &conn->pend);

		hci_encrypt_cfm(conn, ev->status, ev->encrypt);
	}

	hci_dev_unlock(hdev);
}

/* Change Connection Link Key Complete */
static inline void hci_change_conn_link_key_complete_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_change_conn_link_key_complete *ev = (struct hci_ev_change_conn_link_key_complete *) skb->data;
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

/* Pin Code Request*/
static inline void hci_pin_code_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
}

/* Link Key Request */
static inline void hci_link_key_request_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
}

/* Link Key Notification */
static inline void hci_link_key_notify_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
}

/* Remote Features */
static inline void hci_remote_features_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_remote_features *ev = (struct hci_ev_remote_features *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn && !ev->status) {
		memcpy(conn->features, ev->features, sizeof(conn->features));
	}

	hci_dev_unlock(hdev);
}

/* Clock Offset */
static inline void hci_clock_offset_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_clock_offset *ev = (struct hci_ev_clock_offset *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn && !ev->status) {
		struct inquiry_entry *ie;

		if ((ie = hci_inquiry_cache_lookup(hdev, &conn->dst))) {
			ie->data.clock_offset = ev->clock_offset;
			ie->timestamp = jiffies;
		}
	}

	hci_dev_unlock(hdev);
}

/* Page Scan Repetition Mode */
static inline void hci_pscan_rep_mode_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_pscan_rep_mode *ev = (struct hci_ev_pscan_rep_mode *) skb->data;
	struct inquiry_entry *ie;

	BT_DBG("%s", hdev->name);

	hci_dev_lock(hdev);

	if ((ie = hci_inquiry_cache_lookup(hdev, &ev->bdaddr))) {
		ie->data.pscan_rep_mode = ev->pscan_rep_mode;
		ie->timestamp = jiffies;
	}

	hci_dev_unlock(hdev);
}

/* Sniff Subrate */
static inline void hci_sniff_subrate_evt(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_ev_sniff_subrate *ev = (struct hci_ev_sniff_subrate *) skb->data;
	struct hci_conn *conn;

	BT_DBG("%s status %d", hdev->name, ev->status);

	hci_dev_lock(hdev);

	conn = hci_conn_hash_lookup_handle(hdev, __le16_to_cpu(ev->handle));
	if (conn) {
	}

	hci_dev_unlock(hdev);
}

void hci_event_packet(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_event_hdr *hdr = (struct hci_event_hdr *) skb->data;
	struct hci_ev_cmd_complete *ec;
	struct hci_ev_cmd_status *cs;
	u16 opcode, ocf, ogf;

	skb_pull(skb, HCI_EVENT_HDR_SIZE);

	BT_DBG("%s evt 0x%x", hdev->name, hdr->evt);

	switch (hdr->evt) {
	case HCI_EV_NUM_COMP_PKTS:
		hci_num_comp_pkts_evt(hdev, skb);
		break;

	case HCI_EV_INQUIRY_COMPLETE:
		hci_inquiry_complete_evt(hdev, skb);
		break;

	case HCI_EV_INQUIRY_RESULT:
		hci_inquiry_result_evt(hdev, skb);
		break;

	case HCI_EV_INQUIRY_RESULT_WITH_RSSI:
		hci_inquiry_result_with_rssi_evt(hdev, skb);
		break;

	case HCI_EV_EXTENDED_INQUIRY_RESULT:
		hci_extended_inquiry_result_evt(hdev, skb);
		break;

	case HCI_EV_CONN_REQUEST:
		hci_conn_request_evt(hdev, skb);
		break;

	case HCI_EV_CONN_COMPLETE:
		hci_conn_complete_evt(hdev, skb);
		break;

	case HCI_EV_DISCONN_COMPLETE:
		hci_disconn_complete_evt(hdev, skb);
		break;

	case HCI_EV_ROLE_CHANGE:
		hci_role_change_evt(hdev, skb);
		break;

	case HCI_EV_MODE_CHANGE:
		hci_mode_change_evt(hdev, skb);
		break;

	case HCI_EV_AUTH_COMPLETE:
		hci_auth_complete_evt(hdev, skb);
		break;

	case HCI_EV_ENCRYPT_CHANGE:
		hci_encrypt_change_evt(hdev, skb);
		break;

	case HCI_EV_CHANGE_CONN_LINK_KEY_COMPLETE:
		hci_change_conn_link_key_complete_evt(hdev, skb);
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

	case HCI_EV_REMOTE_FEATURES:
		hci_remote_features_evt(hdev, skb);
		break;

	case HCI_EV_CLOCK_OFFSET:
		hci_clock_offset_evt(hdev, skb);
		break;

	case HCI_EV_PSCAN_REP_MODE:
		hci_pscan_rep_mode_evt(hdev, skb);
		break;

	case HCI_EV_SNIFF_SUBRATE:
		hci_sniff_subrate_evt(hdev, skb);
		break;

	case HCI_EV_CMD_STATUS:
		cs = (struct hci_ev_cmd_status *) skb->data;
		skb_pull(skb, sizeof(cs));

		opcode = __le16_to_cpu(cs->opcode);
		ogf = hci_opcode_ogf(opcode);
		ocf = hci_opcode_ocf(opcode);

		switch (ogf) {
		case OGF_INFO_PARAM:
			hci_cs_info_param(hdev, ocf, cs->status);
			break;

		case OGF_HOST_CTL:
			hci_cs_host_ctl(hdev, ocf, cs->status);
			break;

		case OGF_LINK_CTL:
			hci_cs_link_ctl(hdev, ocf, cs->status);
			break;

		case OGF_LINK_POLICY:
			hci_cs_link_policy(hdev, ocf, cs->status);
			break;

		default:
			BT_DBG("%s Command Status OGF %x", hdev->name, ogf);
			break;
		}

		if (cs->ncmd) {
			atomic_set(&hdev->cmd_cnt, 1);
			if (!skb_queue_empty(&hdev->cmd_q))
				hci_sched_cmd(hdev);
		}
		break;

	case HCI_EV_CMD_COMPLETE:
		ec = (struct hci_ev_cmd_complete *) skb->data;
		skb_pull(skb, sizeof(*ec));

		opcode = __le16_to_cpu(ec->opcode);
		ogf = hci_opcode_ogf(opcode);
		ocf = hci_opcode_ocf(opcode);

		switch (ogf) {
		case OGF_INFO_PARAM:
			hci_cc_info_param(hdev, ocf, skb);
			break;

		case OGF_HOST_CTL:
			hci_cc_host_ctl(hdev, ocf, skb);
			break;

		case OGF_LINK_CTL:
			hci_cc_link_ctl(hdev, ocf, skb);
			break;

		case OGF_LINK_POLICY:
			hci_cc_link_policy(hdev, ocf, skb);
			break;

		default:
			BT_DBG("%s Command Completed OGF %x", hdev->name, ogf);
			break;
		}

		if (ec->ncmd) {
			atomic_set(&hdev->cmd_cnt, 1);
			if (!skb_queue_empty(&hdev->cmd_q))
				hci_sched_cmd(hdev);
		}
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
