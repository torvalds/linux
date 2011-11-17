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

	nfc_dbg("entry, num_entries %d", ntf->num_entries);

	if (ntf->num_entries > NCI_MAX_NUM_CONN)
		ntf->num_entries = NCI_MAX_NUM_CONN;

	/* update the credits */
	for (i = 0; i < ntf->num_entries; i++) {
		nfc_dbg("entry[%d]: conn_id %d, credits %d", i,
			ntf->conn_entries[i].conn_id,
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

static __u8 *nci_extract_rf_params_nfca_passive_poll(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct rf_tech_specific_params_nfca_poll *nfca_poll;

	nfca_poll = &ntf->rf_tech_specific_params.nfca_poll;

	nfca_poll->sens_res = __le16_to_cpu(*((__u16 *)data));
	data += 2;

	nfca_poll->nfcid1_len = *data++;

	nfc_dbg("sens_res 0x%x, nfcid1_len %d",
		nfca_poll->sens_res,
		nfca_poll->nfcid1_len);

	memcpy(nfca_poll->nfcid1, data, nfca_poll->nfcid1_len);
	data += nfca_poll->nfcid1_len;

	nfca_poll->sel_res_len = *data++;

	if (nfca_poll->sel_res_len != 0)
		nfca_poll->sel_res = *data++;

	nfc_dbg("sel_res_len %d, sel_res 0x%x",
		nfca_poll->sel_res_len,
		nfca_poll->sel_res);

	return data;
}

static int nci_extract_activation_params_iso_dep(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct activation_params_nfca_poll_iso_dep *nfca_poll;

	switch (ntf->activation_rf_tech_and_mode) {
	case NCI_NFC_A_PASSIVE_POLL_MODE:
		nfca_poll = &ntf->activation_params.nfca_poll_iso_dep;
		nfca_poll->rats_res_len = *data++;
		if (nfca_poll->rats_res_len > 0) {
			memcpy(nfca_poll->rats_res,
				data,
				nfca_poll->rats_res_len);
		}
		break;

	default:
		nfc_err("unsupported activation_rf_tech_and_mode 0x%x",
			ntf->activation_rf_tech_and_mode);
		return -EPROTO;
	}

	return 0;
}

static void nci_target_found(struct nci_dev *ndev,
				struct nci_rf_intf_activated_ntf *ntf)
{
	struct nfc_target nfc_tgt;

	if (ntf->rf_protocol == NCI_RF_PROTOCOL_T2T)	/* T2T MifareUL */
		nfc_tgt.supported_protocols = NFC_PROTO_MIFARE_MASK;
	else if (ntf->rf_protocol == NCI_RF_PROTOCOL_ISO_DEP)	/* 4A */
		nfc_tgt.supported_protocols = NFC_PROTO_ISO14443_MASK;
	else
		nfc_tgt.supported_protocols = 0;

	nfc_tgt.sens_res = ntf->rf_tech_specific_params.nfca_poll.sens_res;
	nfc_tgt.sel_res = ntf->rf_tech_specific_params.nfca_poll.sel_res;

	if (!(nfc_tgt.supported_protocols & ndev->poll_prots)) {
		nfc_dbg("the target found does not have the desired protocol");
		return;
	}

	nfc_dbg("new target found,  supported_protocols 0x%x",
		nfc_tgt.supported_protocols);

	ndev->target_available_prots = nfc_tgt.supported_protocols;

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
	ntf.rf_interface_type = *data++;
	ntf.rf_protocol = *data++;
	ntf.activation_rf_tech_and_mode = *data++;
	ntf.rf_tech_specific_params_len = *data++;

	nfc_dbg("rf_discovery_id %d", ntf.rf_discovery_id);
	nfc_dbg("rf_interface_type 0x%x", ntf.rf_interface_type);
	nfc_dbg("rf_protocol 0x%x", ntf.rf_protocol);
	nfc_dbg("activation_rf_tech_and_mode 0x%x",
		ntf.activation_rf_tech_and_mode);
	nfc_dbg("rf_tech_specific_params_len %d",
		ntf.rf_tech_specific_params_len);

	if (ntf.rf_tech_specific_params_len > 0) {
		switch (ntf.activation_rf_tech_and_mode) {
		case NCI_NFC_A_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfca_passive_poll(ndev,
				&ntf, data);
			break;

		default:
			nfc_err("unsupported activation_rf_tech_and_mode 0x%x",
				ntf.activation_rf_tech_and_mode);
			return;
		}
	}

	ntf.data_exch_rf_tech_and_mode = *data++;
	ntf.data_exch_tx_bit_rate = *data++;
	ntf.data_exch_rx_bit_rate = *data++;
	ntf.activation_params_len = *data++;

	nfc_dbg("data_exch_rf_tech_and_mode 0x%x",
		ntf.data_exch_rf_tech_and_mode);
	nfc_dbg("data_exch_tx_bit_rate 0x%x",
		ntf.data_exch_tx_bit_rate);
	nfc_dbg("data_exch_rx_bit_rate 0x%x",
		ntf.data_exch_rx_bit_rate);
	nfc_dbg("activation_params_len %d",
		ntf.activation_params_len);

	if (ntf.activation_params_len > 0) {
		switch (ntf.rf_interface_type) {
		case NCI_RF_INTERFACE_ISO_DEP:
			err = nci_extract_activation_params_iso_dep(ndev,
				&ntf, data);
			break;

		case NCI_RF_INTERFACE_FRAME:
			/* no activation params */
			break;

		default:
			nfc_err("unsupported rf_interface_type 0x%x",
				ntf.rf_interface_type);
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

	nfc_dbg("entry, type 0x%x, reason 0x%x", ntf->type, ntf->reason);

	clear_bit(NCI_POLL_ACTIVE, &ndev->flags);
	ndev->target_active_prot = 0;

	/* drop tx data queue */
	skb_queue_purge(&ndev->tx_q);

	/* drop partial rx data packet */
	if (ndev->rx_data_reassembly) {
		kfree_skb(ndev->rx_data_reassembly);
		ndev->rx_data_reassembly = 0;
	}

	/* set the available credits to initial value */
	atomic_set(&ndev->credits_cnt, ndev->initial_num_credits);

	/* complete the data exchange transaction, if exists */
	if (test_bit(NCI_DATA_EXCHANGE, &ndev->flags))
		nci_data_exchange_complete(ndev, NULL, -EIO);
}

void nci_ntf_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u16 ntf_opcode = nci_opcode(skb->data);

	nfc_dbg("NCI RX: MT=ntf, PBF=%d, GID=0x%x, OID=0x%x, plen=%d",
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

	case NCI_OP_RF_INTF_ACTIVATED_NTF:
		nci_rf_intf_activated_ntf_packet(ndev, skb);
		break;

	case NCI_OP_RF_DEACTIVATE_NTF:
		nci_rf_deactivate_ntf_packet(ndev, skb);
		break;

	default:
		nfc_err("unknown ntf opcode 0x%x", ntf_opcode);
		break;
	}

	kfree_skb(skb);
}
