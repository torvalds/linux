// SPDX-License-Identifier: GPL-2.0-only
/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2014 Marvell International Ltd.
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci_event.c, which was written
 *  by Maxim Krasnyansky.
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

static void nci_core_reset_ntf_packet(struct nci_dev *ndev,
				      struct sk_buff *skb)
{
	/* Handle NCI 2.x core reset notification */
	struct nci_core_reset_ntf *ntf = (void *)skb->data;

	ndev->nci_ver = ntf->nci_ver;
	pr_debug("nci_ver 0x%x, config_status 0x%x\n",
		 ntf->nci_ver, ntf->config_status);

	ndev->manufact_id = ntf->manufact_id;
	ndev->manufact_specific_info =
		__le32_to_cpu(ntf->manufact_specific_info);

	nci_req_complete(ndev, NCI_STATUS_OK);
}

static void nci_core_conn_credits_ntf_packet(struct nci_dev *ndev,
					     struct sk_buff *skb)
{
	struct nci_core_conn_credit_ntf *ntf = (void *) skb->data;
	struct nci_conn_info	*conn_info;
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

		conn_info = nci_get_conn_info_by_conn_id(ndev,
							 ntf->conn_entries[i].conn_id);
		if (!conn_info)
			return;

		atomic_add(ntf->conn_entries[i].credits,
			   &conn_info->credits_cnt);
	}

	/* trigger the next tx */
	if (!skb_queue_empty(&ndev->tx_q))
		queue_work(ndev->tx_wq, &ndev->tx_work);
}

static void nci_core_generic_error_ntf_packet(struct nci_dev *ndev,
					      struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	pr_debug("status 0x%x\n", status);

	if (atomic_read(&ndev->state) == NCI_W4_HOST_SELECT) {
		/* Activation failed, so complete the request
		   (the state remains the same) */
		nci_req_complete(ndev, status);
	}
}

static void nci_core_conn_intf_error_ntf_packet(struct nci_dev *ndev,
						struct sk_buff *skb)
{
	struct nci_core_intf_error_ntf *ntf = (void *) skb->data;

	ntf->conn_id = nci_conn_id(&ntf->conn_id);

	pr_debug("status 0x%x, conn_id %d\n", ntf->status, ntf->conn_id);

	/* complete the data exchange transaction, if exists */
	if (test_bit(NCI_DATA_EXCHANGE, &ndev->flags))
		nci_data_exchange_complete(ndev, NULL, ntf->conn_id, -EIO);
}

static __u8 *nci_extract_rf_params_nfca_passive_poll(struct nci_dev *ndev,
			struct rf_tech_specific_params_nfca_poll *nfca_poll,
						     __u8 *data)
{
	nfca_poll->sens_res = __le16_to_cpu(*((__le16 *)data));
	data += 2;

	nfca_poll->nfcid1_len = min_t(__u8, *data++, NFC_NFCID1_MAXSIZE);

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
			struct rf_tech_specific_params_nfcb_poll *nfcb_poll,
						     __u8 *data)
{
	nfcb_poll->sensb_res_len = min_t(__u8, *data++, NFC_SENSB_RES_MAXSIZE);

	pr_debug("sensb_res_len %d\n", nfcb_poll->sensb_res_len);

	memcpy(nfcb_poll->sensb_res, data, nfcb_poll->sensb_res_len);
	data += nfcb_poll->sensb_res_len;

	return data;
}

static __u8 *nci_extract_rf_params_nfcf_passive_poll(struct nci_dev *ndev,
			struct rf_tech_specific_params_nfcf_poll *nfcf_poll,
						     __u8 *data)
{
	nfcf_poll->bit_rate = *data++;
	nfcf_poll->sensf_res_len = min_t(__u8, *data++, NFC_SENSF_RES_MAXSIZE);

	pr_debug("bit_rate %d, sensf_res_len %d\n",
		 nfcf_poll->bit_rate, nfcf_poll->sensf_res_len);

	memcpy(nfcf_poll->sensf_res, data, nfcf_poll->sensf_res_len);
	data += nfcf_poll->sensf_res_len;

	return data;
}

static __u8 *nci_extract_rf_params_nfcv_passive_poll(struct nci_dev *ndev,
			struct rf_tech_specific_params_nfcv_poll *nfcv_poll,
						     __u8 *data)
{
	++data;
	nfcv_poll->dsfid = *data++;
	memcpy(nfcv_poll->uid, data, NFC_ISO15693_UID_MAXSIZE);
	data += NFC_ISO15693_UID_MAXSIZE;
	return data;
}

static __u8 *nci_extract_rf_params_nfcf_passive_listen(struct nci_dev *ndev,
			struct rf_tech_specific_params_nfcf_listen *nfcf_listen,
						     __u8 *data)
{
	nfcf_listen->local_nfcid2_len = min_t(__u8, *data++,
					      NFC_NFCID2_MAXSIZE);
	memcpy(nfcf_listen->local_nfcid2, data, nfcf_listen->local_nfcid2_len);
	data += nfcf_listen->local_nfcid2_len;

	return data;
}

static __u32 nci_get_prop_rf_protocol(struct nci_dev *ndev, __u8 rf_protocol)
{
	if (ndev->ops->get_rfprotocol)
		return ndev->ops->get_rfprotocol(ndev, rf_protocol);
	return 0;
}

static int nci_add_new_protocol(struct nci_dev *ndev,
				struct nfc_target *target,
				__u8 rf_protocol,
				__u8 rf_tech_and_mode,
				void *params)
{
	struct rf_tech_specific_params_nfca_poll *nfca_poll;
	struct rf_tech_specific_params_nfcb_poll *nfcb_poll;
	struct rf_tech_specific_params_nfcf_poll *nfcf_poll;
	struct rf_tech_specific_params_nfcv_poll *nfcv_poll;
	__u32 protocol;

	if (rf_protocol == NCI_RF_PROTOCOL_T1T)
		protocol = NFC_PROTO_JEWEL_MASK;
	else if (rf_protocol == NCI_RF_PROTOCOL_T2T)
		protocol = NFC_PROTO_MIFARE_MASK;
	else if (rf_protocol == NCI_RF_PROTOCOL_ISO_DEP)
		if (rf_tech_and_mode == NCI_NFC_A_PASSIVE_POLL_MODE)
			protocol = NFC_PROTO_ISO14443_MASK;
		else
			protocol = NFC_PROTO_ISO14443_B_MASK;
	else if (rf_protocol == NCI_RF_PROTOCOL_T3T)
		protocol = NFC_PROTO_FELICA_MASK;
	else if (rf_protocol == NCI_RF_PROTOCOL_NFC_DEP)
		protocol = NFC_PROTO_NFC_DEP_MASK;
	else if (rf_protocol == NCI_RF_PROTOCOL_T5T)
		protocol = NFC_PROTO_ISO15693_MASK;
	else
		protocol = nci_get_prop_rf_protocol(ndev, rf_protocol);

	if (!(protocol & ndev->poll_prots)) {
		pr_err("the target found does not have the desired protocol\n");
		return -EPROTO;
	}

	if (rf_tech_and_mode == NCI_NFC_A_PASSIVE_POLL_MODE) {
		nfca_poll = (struct rf_tech_specific_params_nfca_poll *)params;

		target->sens_res = nfca_poll->sens_res;
		target->sel_res = nfca_poll->sel_res;
		target->nfcid1_len = nfca_poll->nfcid1_len;
		if (target->nfcid1_len > 0) {
			memcpy(target->nfcid1, nfca_poll->nfcid1,
			       target->nfcid1_len);
		}
	} else if (rf_tech_and_mode == NCI_NFC_B_PASSIVE_POLL_MODE) {
		nfcb_poll = (struct rf_tech_specific_params_nfcb_poll *)params;

		target->sensb_res_len = nfcb_poll->sensb_res_len;
		if (target->sensb_res_len > 0) {
			memcpy(target->sensb_res, nfcb_poll->sensb_res,
			       target->sensb_res_len);
		}
	} else if (rf_tech_and_mode == NCI_NFC_F_PASSIVE_POLL_MODE) {
		nfcf_poll = (struct rf_tech_specific_params_nfcf_poll *)params;

		target->sensf_res_len = nfcf_poll->sensf_res_len;
		if (target->sensf_res_len > 0) {
			memcpy(target->sensf_res, nfcf_poll->sensf_res,
			       target->sensf_res_len);
		}
	} else if (rf_tech_and_mode == NCI_NFC_V_PASSIVE_POLL_MODE) {
		nfcv_poll = (struct rf_tech_specific_params_nfcv_poll *)params;

		target->is_iso15693 = 1;
		target->iso15693_dsfid = nfcv_poll->dsfid;
		memcpy(target->iso15693_uid, nfcv_poll->uid, NFC_ISO15693_UID_MAXSIZE);
	} else {
		pr_err("unsupported rf_tech_and_mode 0x%x\n", rf_tech_and_mode);
		return -EPROTO;
	}

	target->supported_protocols |= protocol;

	pr_debug("protocol 0x%x\n", protocol);

	return 0;
}

static void nci_add_new_target(struct nci_dev *ndev,
			       struct nci_rf_discover_ntf *ntf)
{
	struct nfc_target *target;
	int i, rc;

	for (i = 0; i < ndev->n_targets; i++) {
		target = &ndev->targets[i];
		if (target->logical_idx == ntf->rf_discovery_id) {
			/* This target already exists, add the new protocol */
			nci_add_new_protocol(ndev, target, ntf->rf_protocol,
					     ntf->rf_tech_and_mode,
					     &ntf->rf_tech_specific_params);
			return;
		}
	}

	/* This is a new target, check if we've enough room */
	if (ndev->n_targets == NCI_MAX_DISCOVERED_TARGETS) {
		pr_debug("not enough room, ignoring new target...\n");
		return;
	}

	target = &ndev->targets[ndev->n_targets];

	rc = nci_add_new_protocol(ndev, target, ntf->rf_protocol,
				  ntf->rf_tech_and_mode,
				  &ntf->rf_tech_specific_params);
	if (!rc) {
		target->logical_idx = ntf->rf_discovery_id;
		ndev->n_targets++;

		pr_debug("logical idx %d, n_targets %d\n", target->logical_idx,
			 ndev->n_targets);
	}
}

void nci_clear_target_list(struct nci_dev *ndev)
{
	memset(ndev->targets, 0,
	       (sizeof(struct nfc_target)*NCI_MAX_DISCOVERED_TARGETS));

	ndev->n_targets = 0;
}

static void nci_rf_discover_ntf_packet(struct nci_dev *ndev,
				       struct sk_buff *skb)
{
	struct nci_rf_discover_ntf ntf;
	__u8 *data = skb->data;
	bool add_target = true;

	ntf.rf_discovery_id = *data++;
	ntf.rf_protocol = *data++;
	ntf.rf_tech_and_mode = *data++;
	ntf.rf_tech_specific_params_len = *data++;

	pr_debug("rf_discovery_id %d\n", ntf.rf_discovery_id);
	pr_debug("rf_protocol 0x%x\n", ntf.rf_protocol);
	pr_debug("rf_tech_and_mode 0x%x\n", ntf.rf_tech_and_mode);
	pr_debug("rf_tech_specific_params_len %d\n",
		 ntf.rf_tech_specific_params_len);

	if (ntf.rf_tech_specific_params_len > 0) {
		switch (ntf.rf_tech_and_mode) {
		case NCI_NFC_A_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfca_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfca_poll), data);
			break;

		case NCI_NFC_B_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcb_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfcb_poll), data);
			break;

		case NCI_NFC_F_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcf_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfcf_poll), data);
			break;

		case NCI_NFC_V_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcv_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfcv_poll), data);
			break;

		default:
			pr_err("unsupported rf_tech_and_mode 0x%x\n",
			       ntf.rf_tech_and_mode);
			data += ntf.rf_tech_specific_params_len;
			add_target = false;
		}
	}

	ntf.ntf_type = *data++;
	pr_debug("ntf_type %d\n", ntf.ntf_type);

	if (add_target == true)
		nci_add_new_target(ndev, &ntf);

	if (ntf.ntf_type == NCI_DISCOVER_NTF_TYPE_MORE) {
		atomic_set(&ndev->state, NCI_W4_ALL_DISCOVERIES);
	} else {
		atomic_set(&ndev->state, NCI_W4_HOST_SELECT);
		nfc_targets_found(ndev->nfc_dev, ndev->targets,
				  ndev->n_targets);
	}
}

static int nci_extract_activation_params_iso_dep(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct activation_params_nfca_poll_iso_dep *nfca_poll;
	struct activation_params_nfcb_poll_iso_dep *nfcb_poll;

	switch (ntf->activation_rf_tech_and_mode) {
	case NCI_NFC_A_PASSIVE_POLL_MODE:
		nfca_poll = &ntf->activation_params.nfca_poll_iso_dep;
		nfca_poll->rats_res_len = min_t(__u8, *data++, 20);
		pr_debug("rats_res_len %d\n", nfca_poll->rats_res_len);
		if (nfca_poll->rats_res_len > 0) {
			memcpy(nfca_poll->rats_res,
			       data, nfca_poll->rats_res_len);
		}
		break;

	case NCI_NFC_B_PASSIVE_POLL_MODE:
		nfcb_poll = &ntf->activation_params.nfcb_poll_iso_dep;
		nfcb_poll->attrib_res_len = min_t(__u8, *data++, 50);
		pr_debug("attrib_res_len %d\n", nfcb_poll->attrib_res_len);
		if (nfcb_poll->attrib_res_len > 0) {
			memcpy(nfcb_poll->attrib_res,
			       data, nfcb_poll->attrib_res_len);
		}
		break;

	default:
		pr_err("unsupported activation_rf_tech_and_mode 0x%x\n",
		       ntf->activation_rf_tech_and_mode);
		return NCI_STATUS_RF_PROTOCOL_ERROR;
	}

	return NCI_STATUS_OK;
}

static int nci_extract_activation_params_nfc_dep(struct nci_dev *ndev,
			struct nci_rf_intf_activated_ntf *ntf, __u8 *data)
{
	struct activation_params_poll_nfc_dep *poll;
	struct activation_params_listen_nfc_dep *listen;

	switch (ntf->activation_rf_tech_and_mode) {
	case NCI_NFC_A_PASSIVE_POLL_MODE:
	case NCI_NFC_F_PASSIVE_POLL_MODE:
		poll = &ntf->activation_params.poll_nfc_dep;
		poll->atr_res_len = min_t(__u8, *data++,
					  NFC_ATR_RES_MAXSIZE - 2);
		pr_debug("atr_res_len %d\n", poll->atr_res_len);
		if (poll->atr_res_len > 0)
			memcpy(poll->atr_res, data, poll->atr_res_len);
		break;

	case NCI_NFC_A_PASSIVE_LISTEN_MODE:
	case NCI_NFC_F_PASSIVE_LISTEN_MODE:
		listen = &ntf->activation_params.listen_nfc_dep;
		listen->atr_req_len = min_t(__u8, *data++,
					    NFC_ATR_REQ_MAXSIZE - 2);
		pr_debug("atr_req_len %d\n", listen->atr_req_len);
		if (listen->atr_req_len > 0)
			memcpy(listen->atr_req, data, listen->atr_req_len);
		break;

	default:
		pr_err("unsupported activation_rf_tech_and_mode 0x%x\n",
		       ntf->activation_rf_tech_and_mode);
		return NCI_STATUS_RF_PROTOCOL_ERROR;
	}

	return NCI_STATUS_OK;
}

static void nci_target_auto_activated(struct nci_dev *ndev,
				      struct nci_rf_intf_activated_ntf *ntf)
{
	struct nfc_target *target;
	int rc;

	target = &ndev->targets[ndev->n_targets];

	rc = nci_add_new_protocol(ndev, target, ntf->rf_protocol,
				  ntf->activation_rf_tech_and_mode,
				  &ntf->rf_tech_specific_params);
	if (rc)
		return;

	target->logical_idx = ntf->rf_discovery_id;
	ndev->n_targets++;

	pr_debug("logical idx %d, n_targets %d\n",
		 target->logical_idx, ndev->n_targets);

	nfc_targets_found(ndev->nfc_dev, ndev->targets, ndev->n_targets);
}

static int nci_store_general_bytes_nfc_dep(struct nci_dev *ndev,
		struct nci_rf_intf_activated_ntf *ntf)
{
	ndev->remote_gb_len = 0;

	if (ntf->activation_params_len <= 0)
		return NCI_STATUS_OK;

	switch (ntf->activation_rf_tech_and_mode) {
	case NCI_NFC_A_PASSIVE_POLL_MODE:
	case NCI_NFC_F_PASSIVE_POLL_MODE:
		ndev->remote_gb_len = min_t(__u8,
			(ntf->activation_params.poll_nfc_dep.atr_res_len
						- NFC_ATR_RES_GT_OFFSET),
			NFC_ATR_RES_GB_MAXSIZE);
		memcpy(ndev->remote_gb,
		       (ntf->activation_params.poll_nfc_dep.atr_res
						+ NFC_ATR_RES_GT_OFFSET),
		       ndev->remote_gb_len);
		break;

	case NCI_NFC_A_PASSIVE_LISTEN_MODE:
	case NCI_NFC_F_PASSIVE_LISTEN_MODE:
		ndev->remote_gb_len = min_t(__u8,
			(ntf->activation_params.listen_nfc_dep.atr_req_len
						- NFC_ATR_REQ_GT_OFFSET),
			NFC_ATR_REQ_GB_MAXSIZE);
		memcpy(ndev->remote_gb,
		       (ntf->activation_params.listen_nfc_dep.atr_req
						+ NFC_ATR_REQ_GT_OFFSET),
		       ndev->remote_gb_len);
		break;

	default:
		pr_err("unsupported activation_rf_tech_and_mode 0x%x\n",
		       ntf->activation_rf_tech_and_mode);
		return NCI_STATUS_RF_PROTOCOL_ERROR;
	}

	return NCI_STATUS_OK;
}

static void nci_rf_intf_activated_ntf_packet(struct nci_dev *ndev,
					     struct sk_buff *skb)
{
	struct nci_conn_info    *conn_info;
	struct nci_rf_intf_activated_ntf ntf;
	__u8 *data = skb->data;
	int err = NCI_STATUS_OK;

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
	pr_debug("initial_num_credits 0x%x\n",
		 ntf.initial_num_credits);
	pr_debug("rf_tech_specific_params_len %d\n",
		 ntf.rf_tech_specific_params_len);

	/* If this contains a value of 0x00 (NFCEE Direct RF
	 * Interface) then all following parameters SHALL contain a
	 * value of 0 and SHALL be ignored.
	 */
	if (ntf.rf_interface == NCI_RF_INTERFACE_NFCEE_DIRECT)
		goto listen;

	if (ntf.rf_tech_specific_params_len > 0) {
		switch (ntf.activation_rf_tech_and_mode) {
		case NCI_NFC_A_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfca_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfca_poll), data);
			break;

		case NCI_NFC_B_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcb_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfcb_poll), data);
			break;

		case NCI_NFC_F_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcf_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfcf_poll), data);
			break;

		case NCI_NFC_V_PASSIVE_POLL_MODE:
			data = nci_extract_rf_params_nfcv_passive_poll(ndev,
				&(ntf.rf_tech_specific_params.nfcv_poll), data);
			break;

		case NCI_NFC_A_PASSIVE_LISTEN_MODE:
			/* no RF technology specific parameters */
			break;

		case NCI_NFC_F_PASSIVE_LISTEN_MODE:
			data = nci_extract_rf_params_nfcf_passive_listen(ndev,
				&(ntf.rf_tech_specific_params.nfcf_listen),
				data);
			break;

		default:
			pr_err("unsupported activation_rf_tech_and_mode 0x%x\n",
			       ntf.activation_rf_tech_and_mode);
			err = NCI_STATUS_RF_PROTOCOL_ERROR;
			goto exit;
		}
	}

	ntf.data_exch_rf_tech_and_mode = *data++;
	ntf.data_exch_tx_bit_rate = *data++;
	ntf.data_exch_rx_bit_rate = *data++;
	ntf.activation_params_len = *data++;

	pr_debug("data_exch_rf_tech_and_mode 0x%x\n",
		 ntf.data_exch_rf_tech_and_mode);
	pr_debug("data_exch_tx_bit_rate 0x%x\n", ntf.data_exch_tx_bit_rate);
	pr_debug("data_exch_rx_bit_rate 0x%x\n", ntf.data_exch_rx_bit_rate);
	pr_debug("activation_params_len %d\n", ntf.activation_params_len);

	if (ntf.activation_params_len > 0) {
		switch (ntf.rf_interface) {
		case NCI_RF_INTERFACE_ISO_DEP:
			err = nci_extract_activation_params_iso_dep(ndev,
								    &ntf, data);
			break;

		case NCI_RF_INTERFACE_NFC_DEP:
			err = nci_extract_activation_params_nfc_dep(ndev,
								    &ntf, data);
			break;

		case NCI_RF_INTERFACE_FRAME:
			/* no activation params */
			break;

		default:
			pr_err("unsupported rf_interface 0x%x\n",
			       ntf.rf_interface);
			err = NCI_STATUS_RF_PROTOCOL_ERROR;
			break;
		}
	}

exit:
	if (err == NCI_STATUS_OK) {
		conn_info = ndev->rf_conn_info;
		if (!conn_info)
			return;

		conn_info->max_pkt_payload_len = ntf.max_data_pkt_payload_size;
		conn_info->initial_num_credits = ntf.initial_num_credits;

		/* set the available credits to initial value */
		atomic_set(&conn_info->credits_cnt,
			   conn_info->initial_num_credits);

		/* store general bytes to be reported later in dep_link_up */
		if (ntf.rf_interface == NCI_RF_INTERFACE_NFC_DEP) {
			err = nci_store_general_bytes_nfc_dep(ndev, &ntf);
			if (err != NCI_STATUS_OK)
				pr_err("unable to store general bytes\n");
		}
	}

	if (!(ntf.activation_rf_tech_and_mode & NCI_RF_TECH_MODE_LISTEN_MASK)) {
		/* Poll mode */
		if (atomic_read(&ndev->state) == NCI_DISCOVERY) {
			/* A single target was found and activated
			 * automatically */
			atomic_set(&ndev->state, NCI_POLL_ACTIVE);
			if (err == NCI_STATUS_OK)
				nci_target_auto_activated(ndev, &ntf);
		} else {	/* ndev->state == NCI_W4_HOST_SELECT */
			/* A selected target was activated, so complete the
			 * request */
			atomic_set(&ndev->state, NCI_POLL_ACTIVE);
			nci_req_complete(ndev, err);
		}
	} else {
listen:
		/* Listen mode */
		atomic_set(&ndev->state, NCI_LISTEN_ACTIVE);
		if (err == NCI_STATUS_OK &&
		    ntf.rf_protocol == NCI_RF_PROTOCOL_NFC_DEP) {
			err = nfc_tm_activated(ndev->nfc_dev,
					       NFC_PROTO_NFC_DEP_MASK,
					       NFC_COMM_PASSIVE,
					       ndev->remote_gb,
					       ndev->remote_gb_len);
			if (err != NCI_STATUS_OK)
				pr_err("error when signaling tm activation\n");
		}
	}
}

static void nci_rf_deactivate_ntf_packet(struct nci_dev *ndev,
					 struct sk_buff *skb)
{
	struct nci_conn_info    *conn_info;
	struct nci_rf_deactivate_ntf *ntf = (void *) skb->data;

	pr_debug("entry, type 0x%x, reason 0x%x\n", ntf->type, ntf->reason);

	conn_info = ndev->rf_conn_info;
	if (!conn_info)
		return;

	/* drop tx data queue */
	skb_queue_purge(&ndev->tx_q);

	/* drop partial rx data packet */
	if (ndev->rx_data_reassembly) {
		kfree_skb(ndev->rx_data_reassembly);
		ndev->rx_data_reassembly = NULL;
	}

	/* complete the data exchange transaction, if exists */
	if (test_bit(NCI_DATA_EXCHANGE, &ndev->flags))
		nci_data_exchange_complete(ndev, NULL, NCI_STATIC_RF_CONN_ID,
					   -EIO);

	switch (ntf->type) {
	case NCI_DEACTIVATE_TYPE_IDLE_MODE:
		nci_clear_target_list(ndev);
		atomic_set(&ndev->state, NCI_IDLE);
		break;
	case NCI_DEACTIVATE_TYPE_SLEEP_MODE:
	case NCI_DEACTIVATE_TYPE_SLEEP_AF_MODE:
		atomic_set(&ndev->state, NCI_W4_HOST_SELECT);
		break;
	case NCI_DEACTIVATE_TYPE_DISCOVERY:
		nci_clear_target_list(ndev);
		atomic_set(&ndev->state, NCI_DISCOVERY);
		break;
	}

	nci_req_complete(ndev, NCI_STATUS_OK);
}

static void nci_nfcee_discover_ntf_packet(struct nci_dev *ndev,
					  struct sk_buff *skb)
{
	u8 status = NCI_STATUS_OK;
	struct nci_nfcee_discover_ntf   *nfcee_ntf =
				(struct nci_nfcee_discover_ntf *)skb->data;

	pr_debug("\n");

	/* NFCForum NCI 9.2.1 HCI Network Specific Handling
	 * If the NFCC supports the HCI Network, it SHALL return one,
	 * and only one, NFCEE_DISCOVER_NTF with a Protocol type of
	 * “HCI Access”, even if the HCI Network contains multiple NFCEEs.
	 */
	ndev->hci_dev->nfcee_id = nfcee_ntf->nfcee_id;
	ndev->cur_params.id = nfcee_ntf->nfcee_id;

	nci_req_complete(ndev, status);
}

static void nci_nfcee_action_ntf_packet(struct nci_dev *ndev,
					struct sk_buff *skb)
{
	pr_debug("\n");
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

	if (nci_opcode_gid(ntf_opcode) == NCI_GID_PROPRIETARY) {
		if (nci_prop_ntf_packet(ndev, ntf_opcode, skb) == -ENOTSUPP) {
			pr_err("unsupported ntf opcode 0x%x\n",
			       ntf_opcode);
		}

		goto end;
	}

	switch (ntf_opcode) {
	case NCI_OP_CORE_RESET_NTF:
		nci_core_reset_ntf_packet(ndev, skb);
		break;

	case NCI_OP_CORE_CONN_CREDITS_NTF:
		nci_core_conn_credits_ntf_packet(ndev, skb);
		break;

	case NCI_OP_CORE_GENERIC_ERROR_NTF:
		nci_core_generic_error_ntf_packet(ndev, skb);
		break;

	case NCI_OP_CORE_INTF_ERROR_NTF:
		nci_core_conn_intf_error_ntf_packet(ndev, skb);
		break;

	case NCI_OP_RF_DISCOVER_NTF:
		nci_rf_discover_ntf_packet(ndev, skb);
		break;

	case NCI_OP_RF_INTF_ACTIVATED_NTF:
		nci_rf_intf_activated_ntf_packet(ndev, skb);
		break;

	case NCI_OP_RF_DEACTIVATE_NTF:
		nci_rf_deactivate_ntf_packet(ndev, skb);
		break;

	case NCI_OP_NFCEE_DISCOVER_NTF:
		nci_nfcee_discover_ntf_packet(ndev, skb);
		break;

	case NCI_OP_RF_NFCEE_ACTION_NTF:
		nci_nfcee_action_ntf_packet(ndev, skb);
		break;

	default:
		pr_err("unknown ntf opcode 0x%x\n", ntf_opcode);
		break;
	}

	nci_core_ntf_packet(ndev, ntf_opcode, skb);
end:
	kfree_skb(skb);
}
