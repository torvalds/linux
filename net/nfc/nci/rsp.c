/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci_event.c, which was written
 *  by Maxim Krasnyansky.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>

#include "../nfc.h"
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>

/* Handle NCI Response packets */

static void nci_core_reset_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct nci_core_reset_rsp *rsp = (void *) skb->data;

	pr_debug("status 0x%x\n", rsp->status);

	if (rsp->status == NCI_STATUS_OK) {
		ndev->nci_ver = rsp->nci_ver;
		pr_debug("nci_ver 0x%x, config_status 0x%x\n",
			 rsp->nci_ver, rsp->config_status);
	}

	nci_req_complete(ndev, rsp->status);
}

static void nci_core_init_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct nci_core_init_rsp_1 *rsp_1 = (void *) skb->data;
	struct nci_core_init_rsp_2 *rsp_2;

	pr_debug("status 0x%x\n", rsp_1->status);

	if (rsp_1->status != NCI_STATUS_OK)
		goto exit;

	ndev->nfcc_features = __le32_to_cpu(rsp_1->nfcc_features);
	ndev->num_supported_rf_interfaces = rsp_1->num_supported_rf_interfaces;

	if (ndev->num_supported_rf_interfaces >
	    NCI_MAX_SUPPORTED_RF_INTERFACES) {
		ndev->num_supported_rf_interfaces =
			NCI_MAX_SUPPORTED_RF_INTERFACES;
	}

	memcpy(ndev->supported_rf_interfaces,
	       rsp_1->supported_rf_interfaces,
	       ndev->num_supported_rf_interfaces);

	rsp_2 = (void *) (skb->data + 6 + rsp_1->num_supported_rf_interfaces);

	ndev->max_logical_connections = rsp_2->max_logical_connections;
	ndev->max_routing_table_size =
		__le16_to_cpu(rsp_2->max_routing_table_size);
	ndev->max_ctrl_pkt_payload_len =
		rsp_2->max_ctrl_pkt_payload_len;
	ndev->max_size_for_large_params =
		__le16_to_cpu(rsp_2->max_size_for_large_params);
	ndev->manufact_id =
		rsp_2->manufact_id;
	ndev->manufact_specific_info =
		__le32_to_cpu(rsp_2->manufact_specific_info);

	pr_debug("nfcc_features 0x%x\n",
		 ndev->nfcc_features);
	pr_debug("num_supported_rf_interfaces %d\n",
		 ndev->num_supported_rf_interfaces);
	pr_debug("supported_rf_interfaces[0] 0x%x\n",
		 ndev->supported_rf_interfaces[0]);
	pr_debug("supported_rf_interfaces[1] 0x%x\n",
		 ndev->supported_rf_interfaces[1]);
	pr_debug("supported_rf_interfaces[2] 0x%x\n",
		 ndev->supported_rf_interfaces[2]);
	pr_debug("supported_rf_interfaces[3] 0x%x\n",
		 ndev->supported_rf_interfaces[3]);
	pr_debug("max_logical_connections %d\n",
		 ndev->max_logical_connections);
	pr_debug("max_routing_table_size %d\n",
		 ndev->max_routing_table_size);
	pr_debug("max_ctrl_pkt_payload_len %d\n",
		 ndev->max_ctrl_pkt_payload_len);
	pr_debug("max_size_for_large_params %d\n",
		 ndev->max_size_for_large_params);
	pr_debug("manufact_id 0x%x\n",
		 ndev->manufact_id);
	pr_debug("manufact_specific_info 0x%x\n",
		 ndev->manufact_specific_info);

exit:
	nci_req_complete(ndev, rsp_1->status);
}

static void nci_core_set_config_rsp_packet(struct nci_dev *ndev,
					   struct sk_buff *skb)
{
	struct nci_core_set_config_rsp *rsp = (void *) skb->data;

	pr_debug("status 0x%x\n", rsp->status);

	nci_req_complete(ndev, rsp->status);
}

static void nci_rf_disc_map_rsp_packet(struct nci_dev *ndev,
				       struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	pr_debug("status 0x%x\n", status);

	nci_req_complete(ndev, status);
}

static void nci_rf_disc_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct nci_conn_info    *conn_info;
	__u8 status = skb->data[0];

	pr_debug("status 0x%x\n", status);

	if (status == NCI_STATUS_OK) {
		atomic_set(&ndev->state, NCI_DISCOVERY);

		conn_info = ndev->rf_conn_info;
		if (!conn_info) {
			conn_info = devm_kzalloc(&ndev->nfc_dev->dev,
						 sizeof(struct nci_conn_info),
						 GFP_KERNEL);
			if (!conn_info) {
				status = NCI_STATUS_REJECTED;
				goto exit;
			}
			conn_info->conn_id = NCI_STATIC_RF_CONN_ID;
			INIT_LIST_HEAD(&conn_info->list);
			list_add(&conn_info->list, &ndev->conn_info_list);
			ndev->rf_conn_info = conn_info;
		}
	}

exit:
	nci_req_complete(ndev, status);
}

static void nci_rf_disc_select_rsp_packet(struct nci_dev *ndev,
					  struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	pr_debug("status 0x%x\n", status);

	/* Complete the request on intf_activated_ntf or generic_error_ntf */
	if (status != NCI_STATUS_OK)
		nci_req_complete(ndev, status);
}

static void nci_rf_deactivate_rsp_packet(struct nci_dev *ndev,
					 struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	pr_debug("status 0x%x\n", status);

	/* If target was active, complete the request only in deactivate_ntf */
	if ((status != NCI_STATUS_OK) ||
	    (atomic_read(&ndev->state) != NCI_POLL_ACTIVE)) {
		nci_clear_target_list(ndev);
		atomic_set(&ndev->state, NCI_IDLE);
		nci_req_complete(ndev, status);
	}
}

static void nci_nfcee_discover_rsp_packet(struct nci_dev *ndev,
					  struct sk_buff *skb)
{
	struct nci_nfcee_discover_rsp *discover_rsp;

	if (skb->len != 2) {
		nci_req_complete(ndev, NCI_STATUS_NFCEE_PROTOCOL_ERROR);
		return;
	}

	discover_rsp = (struct nci_nfcee_discover_rsp *)skb->data;

	if (discover_rsp->status != NCI_STATUS_OK ||
	    discover_rsp->num_nfcee == 0)
		nci_req_complete(ndev, discover_rsp->status);
}

static void nci_nfcee_mode_set_rsp_packet(struct nci_dev *ndev,
					  struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	pr_debug("status 0x%x\n", status);
	nci_req_complete(ndev, status);
}

static void nci_core_conn_create_rsp_packet(struct nci_dev *ndev,
					    struct sk_buff *skb)
{
	__u8 status = skb->data[0];
	struct nci_conn_info *conn_info = NULL;
	struct nci_core_conn_create_rsp *rsp;

	pr_debug("status 0x%x\n", status);

	if (status == NCI_STATUS_OK) {
		rsp = (struct nci_core_conn_create_rsp *)skb->data;

		conn_info = devm_kzalloc(&ndev->nfc_dev->dev,
					 sizeof(*conn_info), GFP_KERNEL);
		if (!conn_info) {
			status = NCI_STATUS_REJECTED;
			goto exit;
		}

		conn_info->dest_params = devm_kzalloc(&ndev->nfc_dev->dev,
						sizeof(struct dest_spec_params),
						GFP_KERNEL);
		if (!conn_info->dest_params) {
			status = NCI_STATUS_REJECTED;
			goto free_conn_info;
		}

		conn_info->dest_type = ndev->cur_dest_type;
		conn_info->dest_params->id = ndev->cur_params.id;
		conn_info->dest_params->protocol = ndev->cur_params.protocol;
		conn_info->conn_id = rsp->conn_id;

		/* Note: data_exchange_cb and data_exchange_cb_context need to
		 * be specify out of nci_core_conn_create_rsp_packet
		 */

		INIT_LIST_HEAD(&conn_info->list);
		list_add(&conn_info->list, &ndev->conn_info_list);

		if (ndev->cur_params.id == ndev->hci_dev->nfcee_id)
			ndev->hci_dev->conn_info = conn_info;

		conn_info->conn_id = rsp->conn_id;
		conn_info->max_pkt_payload_len = rsp->max_ctrl_pkt_payload_len;
		atomic_set(&conn_info->credits_cnt, rsp->credits_cnt);
	}

free_conn_info:
	if (status == NCI_STATUS_REJECTED)
		devm_kfree(&ndev->nfc_dev->dev, conn_info);
exit:

	nci_req_complete(ndev, status);
}

static void nci_core_conn_close_rsp_packet(struct nci_dev *ndev,
					   struct sk_buff *skb)
{
	struct nci_conn_info *conn_info;
	__u8 status = skb->data[0];

	pr_debug("status 0x%x\n", status);
	if (status == NCI_STATUS_OK) {
		conn_info = nci_get_conn_info_by_conn_id(ndev,
							 ndev->cur_conn_id);
		if (conn_info) {
			list_del(&conn_info->list);
			devm_kfree(&ndev->nfc_dev->dev, conn_info);
		}
	}
	nci_req_complete(ndev, status);
}

void nci_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u16 rsp_opcode = nci_opcode(skb->data);

	/* we got a rsp, stop the cmd timer */
	del_timer(&ndev->cmd_timer);

	pr_debug("NCI RX: MT=rsp, PBF=%d, GID=0x%x, OID=0x%x, plen=%d\n",
		 nci_pbf(skb->data),
		 nci_opcode_gid(rsp_opcode),
		 nci_opcode_oid(rsp_opcode),
		 nci_plen(skb->data));

	/* strip the nci control header */
	skb_pull(skb, NCI_CTRL_HDR_SIZE);

	if (nci_opcode_gid(rsp_opcode) == NCI_GID_PROPRIETARY) {
		if (nci_prop_rsp_packet(ndev, rsp_opcode, skb) == -ENOTSUPP) {
			pr_err("unsupported rsp opcode 0x%x\n",
			       rsp_opcode);
		}

		goto end;
	}

	switch (rsp_opcode) {
	case NCI_OP_CORE_RESET_RSP:
		nci_core_reset_rsp_packet(ndev, skb);
		break;

	case NCI_OP_CORE_INIT_RSP:
		nci_core_init_rsp_packet(ndev, skb);
		break;

	case NCI_OP_CORE_SET_CONFIG_RSP:
		nci_core_set_config_rsp_packet(ndev, skb);
		break;

	case NCI_OP_CORE_CONN_CREATE_RSP:
		nci_core_conn_create_rsp_packet(ndev, skb);
		break;

	case NCI_OP_CORE_CONN_CLOSE_RSP:
		nci_core_conn_close_rsp_packet(ndev, skb);
		break;

	case NCI_OP_RF_DISCOVER_MAP_RSP:
		nci_rf_disc_map_rsp_packet(ndev, skb);
		break;

	case NCI_OP_RF_DISCOVER_RSP:
		nci_rf_disc_rsp_packet(ndev, skb);
		break;

	case NCI_OP_RF_DISCOVER_SELECT_RSP:
		nci_rf_disc_select_rsp_packet(ndev, skb);
		break;

	case NCI_OP_RF_DEACTIVATE_RSP:
		nci_rf_deactivate_rsp_packet(ndev, skb);
		break;

	case NCI_OP_NFCEE_DISCOVER_RSP:
		nci_nfcee_discover_rsp_packet(ndev, skb);
		break;

	case NCI_OP_NFCEE_MODE_SET_RSP:
		nci_nfcee_mode_set_rsp_packet(ndev, skb);
		break;

	default:
		pr_err("unknown rsp opcode 0x%x\n", rsp_opcode);
		break;
	}

	nci_core_rsp_packet(ndev, rsp_opcode, skb);
end:
	kfree_skb(skb);

	/* trigger the next cmd */
	atomic_set(&ndev->cmd_cnt, 1);
	if (!skb_queue_empty(&ndev->cmd_q))
		queue_work(ndev->cmd_wq, &ndev->cmd_work);
}
