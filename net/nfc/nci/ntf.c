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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include <linux/nfc.h>

/* Handle NCI Notification packets */

static void nci_core_conn_credits_ntf_packet(struct nci_dev *ndev,
						struct sk_buff *skb)
{
	struct nci_core_conn_credit_ntf *ntf = (void *) skb->data;
	int i;

	pr_debug("num_entries %d\n", ntf->num_entries);

	if (ntf->num_entries > NCI_MAX_NUM_CONN)
		ntf->num_entries = NCI_MAX_NUM_CONN;

	/* update the credits */
	for (i = 0; i < ntf->num_entries; i++) {
		ntf->conn_entries[i].conn_id =
			nci_conn_id(&ntf->conn_entries[i].conn_id);

		pr_debug("entry[%d]: conn_id %d, credits %d\n",
			 i, ntf->conn_entries[i].conn_id,
			 ntf->conn_entries[i].credits);

		if (ntf->conn_entries[i].conn_id == NCI_STATIC_RF_CONN_ID) {
			/* found static rf connection */
			atomic_add(ntf->conn_entries[i].credits,
				&ndev->credits_cnt);
		}
	}

	/* trigger the next tx */
	if (!skb_queue_empty(&ndev->tx_q))
		queue_work(ndev->tx_wq, &ndev->tx_work);
}

static void nci_core_conn_intf_error_ntf_packet(struct nci_dev *ndev,
						struct sk_buff *skb)
{
	struct nci_core_intf_error_ntf *ntf = (void *) skb->data;

	ntf->conn_id = nci_conn_id(&ntf->conn_id);

	pr_debug("status 0x%x, conn_id %d\n", ntf->status, ntf->conn_id);

	/* complete the data exchange transaction, if exists */
	if (test_bit(NCI_DATA_EXCHANGE, &ndev->flags))
		nci_data_exchange_complete(ndev, NULL, -EIO);
}

static __u8 *nci_extract_rf_params_nfca_passive_poll(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct rf_tech_specific_params_nfca_poll *nfca_poll;

	nfca_poll = &ntf->rf_tech_specific_params.nfca_poll;

	nfca_poll->sens_res = __le16_to_cpu(*((__u16 *)data));
	data += 2;

	nfca_poll->nfcid1_len = *data++;

	pr_debug("sens_res 0x%x, nfcid1_len %d\n",
		 nfca_poll->sens_res, nfca_poll->nfcid1_len);

	memcpy(nfca_poll->nfcid1, data, nfca_poll->nfcid1_len);
	data += nfca_poll->nfcid1_len;

	nfca_poll->sel_res_len = *data++;

	if (nfca_poll->sel_res_len != 0)
		nfca_poll->sel_res = *data++;

	pr_debug("sel_res_len %d, sel_res 0x%x\n",
		 nfca_poll->sel_res_len,
		 nfca_poll->sel_res);

	return data;
}

static __u8 *nci_extract_rf_params_nfcb_passive_poll(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct rf_tech_specific_params_nfcb_poll *nfcb_poll;

	nfcb_poll = &ntf->rf_tech_specific_params.nfcb_poll;

	nfcb_poll->sensb_res_len = *data++;

	pr_debug("sensb_res_len %d\n", nfcb_poll->sensb_res_len);

	memcpy(nfcb_poll->sensb_res, data, nfcb_poll->sensb_res_len);
	data += nfcb_poll->sensb_res_len;

	return data;
}

static __u8 *nci_extract_rf_params_nfcf_passive_poll(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct rf_tech_specific_params_nfcf_poll *nfcf_poll;

	nfcf_poll = &ntf->rf_tech_specific_params.nfcf_poll;

	nfcf_poll->bit_rate = *data++;
	nfcf_poll->sensf_res_len = *data++;

	pr_debug("bit_rate %d, sensf_res_len %d\n",
		nfcf_poll->bit_rate, nfcf_poll->sensf_res_len);

	memcpy(nfcf_poll->sensf_res, data, nfcf_poll->sensf_res_len);
	data += nfcf_poll->sensf_res_len;

	return data;
}

static int nci_extract_activation_params_iso_dep(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct activation_params_nfca_poll_iso_dep *nfca_poll;
	struct activation_params_nfcb_poll_iso_dep *nfcb_poll;

	switch (ntf->activation_rf_tech_and_mode) {
	case NCI_NFC_A_PASSIVE_POLL_MODE:
		nfca_poll = &ntf->activation_params.nfca_poll_iso_dep;
		nfca_poll->rats_res_len = *data++;
		pr_debug("rats_res_len %d\n", nfca_poll->rats_res_len);
		if (nfca_poll->rats_res_len > 0) {
			memcpy(nfca_poll->rats_res,
				data,
				nfca_poll->rats_res_len);
		}
		break;

	case NCI_NFC_B_PASSIVE_POLL_MODE:
		nfcb_poll = &ntf->activation_params.nfcb_poll_iso_dep;
		nfcb_poll->attrib_res_len = *data++;
		pr_debug("attrib_res_len %d\n",
			nfcb_poll->attrib_res_len);
		if (nfcb_poll->attrib_res_len > 0) {
			memcpy(nfcb_poll->attrib_res,
				data,
				nfcb_poll->attrib_res_len);
		}
		break;

	default:
		pr_err("unsupported activation_rf_tech_and_mode 0x%x\n",
		       ntf->activation_rf_tech_and_mode);
		return -EPROTO;
	}

	return 0;
}

static void nci_target_found(struct nci_dev *ndev,
				struct nci_rf_intf_activated_ntf *ntf)
{
	struct nfc_target nfc_tgt;

	memset(&nfc_tgt, 0, sizeof(nfc_tgt));

	if (ntf->rf_protocol == NCI_RF_PROTOCOL_T2T)
		nfc_tgt.supported_protocols = NFC_PROTO_MIFARE_MASK;
	else if (ntf->rf_protocol == NCI_RF_PROTOCOL_ISO_DEP)
		nfc_tgt.supported_protocols = NFC_PROTO_ISO14443_MASK;
	else if (ntf->rf_protocol == NCI_RF_PROTOCOL_T3T)
		nfc_tgt.supported_protocols = NFC_PROTO_FELICA_MASK;

	if (!(nfc_tgt.supported_protocols & ndev->poll_prots)) {
		pr_debug("the target found does not have the desired protocol\n");
		return;
	}

	pr_debug("new target found,  supported_protocols 0x%x\n",
		 nfc_tgt.supported_protocols);

	if (ntf->activation_rf_tech_and_mode == NCI_NFC_A_PASSIVE_POLL_MODE) {
		nfc_tgt.sens_res =
			ntf->rf_tech_specific_params.nfca_poll.sens_res;
		nfc_tgt.sel_res =
			ntf->rf_tech_specific_params.nfca_poll.sel_res;
		nfc_tgt.nfcid1_len =
			ntf->rf_tech_specific_params.nfca_poll.nfcid1_len;
		if (nfc_tgt.nfcid1_len > 0) {
			memcpy(nfc_tgt.nfcid1,
				ntf->rf_tech_specific_params.nfca_poll.nfcid1,
				nfc_tgt.nfcid1_len);
		}
	} else if (ntf->activation_rf_tech_and_mode ==
						NCI_NFC_B_PASSIVE_POLL_MODE) {
		nfc_tgt.sensb_res_len =
			ntf->rf_tech_specific_params.nfcb_poll.sensb_res_len;
		if (nfc_tgt.sensb_res_len > 0) {
			memcpy(nfc_tgt.sensb_res,
			       ntf->rf_tech_specific_params.nfcb_poll.sensb_res,
			       nfc_tgt.sensb_res_len);
		}
	} else if (ntf->activation_rf_tech_and_mode ==
						NCI_NFC_F_PASSIVE_POLL_MODE) {
		nfc_tgt.sensf_res_len =
			ntf->rf_tech_specific_params.nfcf_poll.sensf_res_len;
		if (nfc_tgt.sensf_res_len > 0) {
			memcpy(nfc_tgt.sensf_res,
			       ntf->rf_tech_specific_params.nfcf_poll.sensf_res,
			       nfc_tgt.sensf_res_len);
		}
	}

	ndev->target_available_prots = nfc_tgt.supported_protocols;
	ndev->max_data_pkt_payload_size = ntf->max_data_pkt_payload_size;
	ndev->initial_num_credits = ntf->initial_num_credits;

	/* set the available credits to initial value */
	atomic_set(&ndev->credits_cnt, ndev->initial_num_credits);

	nfc_targets_found(ndev->nfc_dev, &nfc_tgt, 1);
}

static void nci_rf_intf_activated_ntf_packet(struct nci_dev *ndev,
						struct sk_buff *skb)
{
	struct nci_rf_intf_activated_ntf ntf;
	__u8 *data = skb->data;
	int err = 0;

	clear_bit(NCI_DISCOVERY, &ndev->flags);
	set_bit(NCI_POLL_ACTIVE, &ndev->flags);

	ntf.rf_discovery_id = *data++;
	ntf.rf_interface = *data++;
	ntf.rf_protocol = *data++;
	ntf.activation_rf_tech_and_mode = *data++;
	ntf.max_data_pkt_payload_size = *data++;
	ntf.initial_num_credits = *data++;
	ntf.rf_tech_specific_params_len = *data++;

	pr_debug("rf_discovery_id %d\n", ntf.rf_discovery_id);
	pr_debug("rf_interface 0x%x\n", ntf.rf_interface);
	pr_debug("rf_protocol 0x%x\n", ntf.rf_protocol);
	pr_debug("activation_rf_tech_and_mode 0x%x\n",
		 ntf.activation_rf_tech_and_mode);
	pr_debug("max_data_pkt_payload_size 0x%x\n",
		 ntf.max_data_pkt_payload_size);
	pr_debug("initial_num_credits 0x%x\n", ntf.initial_num_credits);
	pr_debug("rf_tech_specific_params_len %d\n",
		 ntf.rf_tech_specific_params_len);

	if (ntf.rf_tech_specific_params_len > 0) {
		switch (ntf.activation_rf_tech_and_mode) {
		case NCI_NFC_A_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfca_passive_poll(ndev,
				&ntf, data);
			break;

		case NCI_NFC_B_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcb_passive_poll(ndev,
				&ntf, data);
			break;

		case NCI_NFC_F_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcf_passive_poll(ndev,
				&ntf, data);
			break;

		default:
			pr_err("unsupported activation_rf_tech_and_mode 0x%x\n",
			       ntf.activation_rf_tech_and_mode);
			return;
		}
	}

	ntf.data_exch_rf_tech_and_mode = *data++;
	ntf.data_exch_tx_bit_rate = *data++;
	ntf.data_exch_rx_bit_rate = *data++;
	ntf.activation_params_len = *data++;

	pr_debug("data_exch_rf_tech_and_mode 0x%x\n",
		 ntf.data_exch_rf_tech_and_mode);
	pr_debug("data_exch_tx_bit_rate 0x%x\n",
		 ntf.data_exch_tx_bit_rate);
	pr_debug("data_exch_rx_bit_rate 0x%x\n",
		 ntf.data_exch_rx_bit_rate);
	pr_debug("activation_params_len %d\n",
		 ntf.activation_params_len);

	if (ntf.activation_params_len > 0) {
		switch (ntf.rf_interface) {
		case NCI_RF_INTERFACE_ISO_DEP:
			err = nci_extract_activation_params_iso_dep(ndev,
				&ntf, data);
			break;

		case NCI_RF_INTERFACE_FRAME:
			/* no activation params */
			break;

		default:
			pr_err("unsupported rf_interface 0x%x\n",
			       ntf.rf_interface);
			return;
		}
	}

	if (!err)
		nci_target_found(ndev, &ntf);
}

static void nci_rf_deactivate_ntf_packet(struct nci_dev *ndev,
					struct sk_buff *skb)
{
	struct nci_rf_deactivate_ntf *ntf = (void *) skb->data;

	pr_debug("entry, type 0x%x, reason 0x%x\n", ntf->type, ntf->reason);

	clear_bit(NCI_POLL_ACTIVE, &ndev->flags);
	ndev->target_active_prot = 0;

	/* drop tx data queue */
	skb_queue_purge(&ndev->tx_q);

	/* drop partial rx data packet */
	if (ndev->rx_data_reassembly) {
		kfree_skb(ndev->rx_data_reassembly);
		ndev->rx_data_reassembly = 0;
	}

	/* complete the data exchange transaction, if exists */
	if (test_bit(NCI_DATA_EXCHANGE, &ndev->flags))
		nci_data_exchange_complete(ndev, NULL, -EIO);

	nci_req_complete(ndev, NCI_STATUS_OK);
}

void nci_ntf_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u16 ntf_opcode = nci_opcode(skb->data);

	pr_debug("NCI RX: MT=ntf, PBF=%d, GID=0x%x, OID=0x%x, plen=%d\n",
		 nci_pbf(skb->data),
		 nci_opcode_gid(ntf_opcode),
		 nci_opcode_oid(ntf_opcode),
		 nci_plen(skb->data));

	/* strip the nci control header */
	skb_pull(skb, NCI_CTRL_HDR_SIZE);

	switch (ntf_opcode) {
	case NCI_OP_CORE_CONN_CREDITS_NTF:
		nci_core_conn_credits_ntf_packet(ndev, skb);
		break;

	case NCI_OP_CORE_INTF_ERROR_NTF:
		nci_core_conn_intf_error_ntf_packet(ndev, skb);
		break;

	case NCI_OP_RF_INTF_ACTIVATED_NTF:
		nci_rf_intf_activated_ntf_packet(ndev, skb);
		break;

	case NCI_OP_RF_DEACTIVATE_NTF:
		nci_rf_deactivate_ntf_packet(ndev, skb);
		break;

	default:
		pr_err("unknown ntf opcode 0x%x\n", ntf_opcode);
		break;
	}

	kfree_skb(skb);
}
