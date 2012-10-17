/*
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) "hci: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nfc.h>

#include <net/nfc/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "hci.h"

/* Largest headroom needed for outgoing HCI commands */
#define HCI_CMDS_HEADROOM 1

static int nfc_hci_result_to_errno(u8 result)
{
	switch (result) {
	case NFC_HCI_ANY_OK:
		return 0;
	case NFC_HCI_ANY_E_TIMEOUT:
		return -ETIME;
	default:
		return -1;
	}
}

static void nfc_hci_msg_tx_work(struct work_struct *work)
{
	struct nfc_hci_dev *hdev = container_of(work, struct nfc_hci_dev,
						msg_tx_work);
	struct hci_msg *msg;
	struct sk_buff *skb;
	int r = 0;

	mutex_lock(&hdev->msg_tx_mutex);

	if (hdev->cmd_pending_msg) {
		if (timer_pending(&hdev->cmd_timer) == 0) {
			if (hdev->cmd_pending_msg->cb)
				hdev->cmd_pending_msg->cb(hdev->
							  cmd_pending_msg->
							  cb_context,
							  NULL,
							  -ETIME);
			kfree(hdev->cmd_pending_msg);
			hdev->cmd_pending_msg = NULL;
		} else {
			goto exit;
		}
	}

next_msg:
	if (list_empty(&hdev->msg_tx_queue))
		goto exit;

	msg = list_first_entry(&hdev->msg_tx_queue, struct hci_msg, msg_l);
	list_del(&msg->msg_l);

	pr_debug("msg_tx_queue has a cmd to send\n");
	while ((skb = skb_dequeue(&msg->msg_frags)) != NULL) {
		r = nfc_llc_xmit_from_hci(hdev->llc, skb);
		if (r < 0) {
			kfree_skb(skb);
			skb_queue_purge(&msg->msg_frags);
			if (msg->cb)
				msg->cb(msg->cb_context, NULL, r);
			kfree(msg);
			break;
		}
	}

	if (r)
		goto next_msg;

	if (msg->wait_response == false) {
		kfree(msg);
		goto next_msg;
	}

	hdev->cmd_pending_msg = msg;
	mod_timer(&hdev->cmd_timer, jiffies +
		  msecs_to_jiffies(hdev->cmd_pending_msg->completion_delay));

exit:
	mutex_unlock(&hdev->msg_tx_mutex);
}

static void nfc_hci_msg_rx_work(struct work_struct *work)
{
	struct nfc_hci_dev *hdev = container_of(work, struct nfc_hci_dev,
						msg_rx_work);
	struct sk_buff *skb;
	struct hcp_message *message;
	u8 pipe;
	u8 type;
	u8 instruction;

	while ((skb = skb_dequeue(&hdev->msg_rx_queue)) != NULL) {
		pipe = skb->data[0];
		skb_pull(skb, NFC_HCI_HCP_PACKET_HEADER_LEN);
		message = (struct hcp_message *)skb->data;
		type = HCP_MSG_GET_TYPE(message->header);
		instruction = HCP_MSG_GET_CMD(message->header);
		skb_pull(skb, NFC_HCI_HCP_MESSAGE_HEADER_LEN);

		nfc_hci_hcp_message_rx(hdev, pipe, type, instruction, skb);
	}
}

static void __nfc_hci_cmd_completion(struct nfc_hci_dev *hdev, int err,
				     struct sk_buff *skb)
{
	del_timer_sync(&hdev->cmd_timer);

	if (hdev->cmd_pending_msg->cb)
		hdev->cmd_pending_msg->cb(hdev->cmd_pending_msg->cb_context,
					  skb, err);
	else
		kfree_skb(skb);

	kfree(hdev->cmd_pending_msg);
	hdev->cmd_pending_msg = NULL;

	schedule_work(&hdev->msg_tx_work);
}

void nfc_hci_resp_received(struct nfc_hci_dev *hdev, u8 result,
			   struct sk_buff *skb)
{
	mutex_lock(&hdev->msg_tx_mutex);

	if (hdev->cmd_pending_msg == NULL) {
		kfree_skb(skb);
		goto exit;
	}

	__nfc_hci_cmd_completion(hdev, nfc_hci_result_to_errno(result), skb);

exit:
	mutex_unlock(&hdev->msg_tx_mutex);
}

void nfc_hci_cmd_received(struct nfc_hci_dev *hdev, u8 pipe, u8 cmd,
			  struct sk_buff *skb)
{
	kfree_skb(skb);
}

static u32 nfc_hci_sak_to_protocol(u8 sak)
{
	switch (NFC_HCI_TYPE_A_SEL_PROT(sak)) {
	case NFC_HCI_TYPE_A_SEL_PROT_MIFARE:
		return NFC_PROTO_MIFARE_MASK;
	case NFC_HCI_TYPE_A_SEL_PROT_ISO14443:
		return NFC_PROTO_ISO14443_MASK;
	case NFC_HCI_TYPE_A_SEL_PROT_DEP:
		return NFC_PROTO_NFC_DEP_MASK;
	case NFC_HCI_TYPE_A_SEL_PROT_ISO14443_DEP:
		return NFC_PROTO_ISO14443_MASK | NFC_PROTO_NFC_DEP_MASK;
	default:
		return 0xffffffff;
	}
}

int nfc_hci_target_discovered(struct nfc_hci_dev *hdev, u8 gate)
{
	struct nfc_target *targets;
	struct sk_buff *atqa_skb = NULL;
	struct sk_buff *sak_skb = NULL;
	struct sk_buff *uid_skb = NULL;
	int r;

	pr_debug("from gate %d\n", gate);

	targets = kzalloc(sizeof(struct nfc_target), GFP_KERNEL);
	if (targets == NULL)
		return -ENOMEM;

	switch (gate) {
	case NFC_HCI_RF_READER_A_GATE:
		r = nfc_hci_get_param(hdev, NFC_HCI_RF_READER_A_GATE,
				      NFC_HCI_RF_READER_A_ATQA, &atqa_skb);
		if (r < 0)
			goto exit;

		r = nfc_hci_get_param(hdev, NFC_HCI_RF_READER_A_GATE,
				      NFC_HCI_RF_READER_A_SAK, &sak_skb);
		if (r < 0)
			goto exit;

		if (atqa_skb->len != 2 || sak_skb->len != 1) {
			r = -EPROTO;
			goto exit;
		}

		targets->supported_protocols =
				nfc_hci_sak_to_protocol(sak_skb->data[0]);
		if (targets->supported_protocols == 0xffffffff) {
			r = -EPROTO;
			goto exit;
		}

		targets->sens_res = be16_to_cpu(*(u16 *)atqa_skb->data);
		targets->sel_res = sak_skb->data[0];

		r = nfc_hci_get_param(hdev, NFC_HCI_RF_READER_A_GATE,
				      NFC_HCI_RF_READER_A_UID, &uid_skb);
		if (r < 0)
			goto exit;

		if (uid_skb->len == 0 || uid_skb->len > NFC_NFCID1_MAXSIZE) {
			r = -EPROTO;
			goto exit;
		}

		memcpy(targets->nfcid1, uid_skb->data, uid_skb->len);
		targets->nfcid1_len = uid_skb->len;

		if (hdev->ops->complete_target_discovered) {
			r = hdev->ops->complete_target_discovered(hdev, gate,
								  targets);
			if (r < 0)
				goto exit;
		}
		break;
	case NFC_HCI_RF_READER_B_GATE:
		targets->supported_protocols = NFC_PROTO_ISO14443_B_MASK;
		break;
	default:
		if (hdev->ops->target_from_gate)
			r = hdev->ops->target_from_gate(hdev, gate, targets);
		else
			r = -EPROTO;
		if (r < 0)
			goto exit;

		if (hdev->ops->complete_target_discovered) {
			r = hdev->ops->complete_target_discovered(hdev, gate,
								  targets);
			if (r < 0)
				goto exit;
		}
		break;
	}

	/* if driver set the new gate, we will skip the old one */
	if (targets->hci_reader_gate == 0x00)
		targets->hci_reader_gate = gate;

	r = nfc_targets_found(hdev->ndev, targets, 1);

exit:
	kfree(targets);
	kfree_skb(atqa_skb);
	kfree_skb(sak_skb);
	kfree_skb(uid_skb);

	return r;
}
EXPORT_SYMBOL(nfc_hci_target_discovered);

void nfc_hci_event_received(struct nfc_hci_dev *hdev, u8 pipe, u8 event,
			    struct sk_buff *skb)
{
	int r = 0;

	switch (event) {
	case NFC_HCI_EVT_TARGET_DISCOVERED:
		if (skb->len < 1) {	/* no status data? */
			r = -EPROTO;
			goto exit;
		}

		if (skb->data[0] == 3) {
			/* TODO: Multiple targets in field, none activated
			 * poll is supposedly stopped, but there is no
			 * single target to activate, so nothing to report
			 * up.
			 * if we need to restart poll, we must save the
			 * protocols from the initial poll and reuse here.
			 */
		}

		if (skb->data[0] != 0) {
			r = -EPROTO;
			goto exit;
		}

		r = nfc_hci_target_discovered(hdev,
					      nfc_hci_pipe2gate(hdev, pipe));
		break;
	default:
		if (hdev->ops->event_received) {
			hdev->ops->event_received(hdev,
						nfc_hci_pipe2gate(hdev, pipe),
						event, skb);
			return;
		}

		break;
	}

exit:
	kfree_skb(skb);

	if (r) {
		/* TODO: There was an error dispatching the event,
		 * how to propagate up to nfc core?
		 */
	}
}

static void nfc_hci_cmd_timeout(unsigned long data)
{
	struct nfc_hci_dev *hdev = (struct nfc_hci_dev *)data;

	schedule_work(&hdev->msg_tx_work);
}

static int hci_dev_connect_gates(struct nfc_hci_dev *hdev, u8 gate_count,
				 struct nfc_hci_gate *gates)
{
	int r;
	while (gate_count--) {
		r = nfc_hci_connect_gate(hdev, NFC_HCI_HOST_CONTROLLER_ID,
					 gates->gate, gates->pipe);
		if (r < 0)
			return r;
		gates++;
	}

	return 0;
}

static int hci_dev_session_init(struct nfc_hci_dev *hdev)
{
	struct sk_buff *skb = NULL;
	int r;

	if (hdev->init_data.gates[0].gate != NFC_HCI_ADMIN_GATE)
		return -EPROTO;

	r = nfc_hci_connect_gate(hdev, NFC_HCI_HOST_CONTROLLER_ID,
				 hdev->init_data.gates[0].gate,
				 hdev->init_data.gates[0].pipe);
	if (r < 0)
		goto exit;

	r = nfc_hci_get_param(hdev, NFC_HCI_ADMIN_GATE,
			      NFC_HCI_ADMIN_SESSION_IDENTITY, &skb);
	if (r < 0)
		goto disconnect_all;

	if (skb->len && skb->len == strlen(hdev->init_data.session_id))
		if (memcmp(hdev->init_data.session_id, skb->data,
			   skb->len) == 0) {
			/* TODO ELa: restore gate<->pipe table from
			 * some TBD location.
			 * note: it doesn't seem possible to get the chip
			 * currently open gate/pipe table.
			 * It is only possible to obtain the supported
			 * gate list.
			 */

			/* goto exit
			 * For now, always do a full initialization */
		}

	r = nfc_hci_disconnect_all_gates(hdev);
	if (r < 0)
		goto exit;

	r = hci_dev_connect_gates(hdev, hdev->init_data.gate_count,
				  hdev->init_data.gates);
	if (r < 0)
		goto disconnect_all;

	r = nfc_hci_set_param(hdev, NFC_HCI_ADMIN_GATE,
			      NFC_HCI_ADMIN_SESSION_IDENTITY,
			      hdev->init_data.session_id,
			      strlen(hdev->init_data.session_id));
	if (r == 0)
		goto exit;

disconnect_all:
	nfc_hci_disconnect_all_gates(hdev);

exit:
	kfree_skb(skb);

	return r;
}

static int hci_dev_version(struct nfc_hci_dev *hdev)
{
	int r;
	struct sk_buff *skb;

	r = nfc_hci_get_param(hdev, NFC_HCI_ID_MGMT_GATE,
			      NFC_HCI_ID_MGMT_VERSION_SW, &skb);
	if (r < 0)
		return r;

	if (skb->len != 3) {
		kfree_skb(skb);
		return -EINVAL;
	}

	hdev->sw_romlib = (skb->data[0] & 0xf0) >> 4;
	hdev->sw_patch = skb->data[0] & 0x0f;
	hdev->sw_flashlib_major = skb->data[1];
	hdev->sw_flashlib_minor = skb->data[2];

	kfree_skb(skb);

	r = nfc_hci_get_param(hdev, NFC_HCI_ID_MGMT_GATE,
			      NFC_HCI_ID_MGMT_VERSION_HW, &skb);
	if (r < 0)
		return r;

	if (skb->len != 3) {
		kfree_skb(skb);
		return -EINVAL;
	}

	hdev->hw_derivative = (skb->data[0] & 0xe0) >> 5;
	hdev->hw_version = skb->data[0] & 0x1f;
	hdev->hw_mpw = (skb->data[1] & 0xc0) >> 6;
	hdev->hw_software = skb->data[1] & 0x3f;
	hdev->hw_bsid = skb->data[2];

	kfree_skb(skb);

	pr_info("SOFTWARE INFO:\n");
	pr_info("RomLib         : %d\n", hdev->sw_romlib);
	pr_info("Patch          : %d\n", hdev->sw_patch);
	pr_info("FlashLib Major : %d\n", hdev->sw_flashlib_major);
	pr_info("FlashLib Minor : %d\n", hdev->sw_flashlib_minor);
	pr_info("HARDWARE INFO:\n");
	pr_info("Derivative     : %d\n", hdev->hw_derivative);
	pr_info("HW Version     : %d\n", hdev->hw_version);
	pr_info("#MPW           : %d\n", hdev->hw_mpw);
	pr_info("Software       : %d\n", hdev->hw_software);
	pr_info("BSID Version   : %d\n", hdev->hw_bsid);

	return 0;
}

static int hci_dev_up(struct nfc_dev *nfc_dev)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);
	int r = 0;

	if (hdev->ops->open) {
		r = hdev->ops->open(hdev);
		if (r < 0)
			return r;
	}

	r = nfc_llc_start(hdev->llc);
	if (r < 0)
		goto exit_close;

	r = hci_dev_session_init(hdev);
	if (r < 0)
		goto exit_llc;

	r = nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
			       NFC_HCI_EVT_END_OPERATION, NULL, 0);
	if (r < 0)
		goto exit_llc;

	if (hdev->ops->hci_ready) {
		r = hdev->ops->hci_ready(hdev);
		if (r < 0)
			goto exit_llc;
	}

	r = hci_dev_version(hdev);
	if (r < 0)
		goto exit_llc;

	return 0;

exit_llc:
	nfc_llc_stop(hdev->llc);

exit_close:
	if (hdev->ops->close)
		hdev->ops->close(hdev);

	return r;
}

static int hci_dev_down(struct nfc_dev *nfc_dev)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);

	nfc_llc_stop(hdev->llc);

	if (hdev->ops->close)
		hdev->ops->close(hdev);

	memset(hdev->gate2pipe, NFC_HCI_INVALID_PIPE, sizeof(hdev->gate2pipe));

	return 0;
}

static int hci_start_poll(struct nfc_dev *nfc_dev,
			  u32 im_protocols, u32 tm_protocols)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);

	if (hdev->ops->start_poll)
		return hdev->ops->start_poll(hdev, im_protocols, tm_protocols);
	else
		return nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
					  NFC_HCI_EVT_READER_REQUESTED,
					  NULL, 0);
}

static void hci_stop_poll(struct nfc_dev *nfc_dev)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);

	nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
			   NFC_HCI_EVT_END_OPERATION, NULL, 0);
}

static int hci_dep_link_up(struct nfc_dev *nfc_dev, struct nfc_target *target,
				__u8 comm_mode, __u8 *gb, size_t gb_len)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);

	if (hdev->ops->dep_link_up)
		return hdev->ops->dep_link_up(hdev, target, comm_mode,
						gb, gb_len);

	return 0;
}

static int hci_dep_link_down(struct nfc_dev *nfc_dev)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);

	if (hdev->ops->dep_link_down)
		return hdev->ops->dep_link_down(hdev);

	return 0;
}

static int hci_activate_target(struct nfc_dev *nfc_dev,
			       struct nfc_target *target, u32 protocol)
{
	return 0;
}

static void hci_deactivate_target(struct nfc_dev *nfc_dev,
				  struct nfc_target *target)
{
}

#define HCI_CB_TYPE_TRANSCEIVE 1

static void hci_transceive_cb(void *context, struct sk_buff *skb, int err)
{
	struct nfc_hci_dev *hdev = context;

	switch (hdev->async_cb_type) {
	case HCI_CB_TYPE_TRANSCEIVE:
		/*
		 * TODO: Check RF Error indicator to make sure data is valid.
		 * It seems that HCI cmd can complete without error, but data
		 * can be invalid if an RF error occured? Ignore for now.
		 */
		if (err == 0)
			skb_trim(skb, skb->len - 1); /* RF Err ind */

		hdev->async_cb(hdev->async_cb_context, skb, err);
		break;
	default:
		if (err == 0)
			kfree_skb(skb);
		break;
	}
}

static int hci_transceive(struct nfc_dev *nfc_dev, struct nfc_target *target,
			  struct sk_buff *skb, data_exchange_cb_t cb,
			  void *cb_context)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);
	int r;

	pr_debug("target_idx=%d\n", target->idx);

	switch (target->hci_reader_gate) {
	case NFC_HCI_RF_READER_A_GATE:
	case NFC_HCI_RF_READER_B_GATE:
		if (hdev->ops->im_transceive) {
			r = hdev->ops->im_transceive(hdev, target, skb, cb,
						     cb_context);
			if (r <= 0)	/* handled */
				break;
		}

		*skb_push(skb, 1) = 0;	/* CTR, see spec:10.2.2.1 */

		hdev->async_cb_type = HCI_CB_TYPE_TRANSCEIVE;
		hdev->async_cb = cb;
		hdev->async_cb_context = cb_context;

		r = nfc_hci_send_cmd_async(hdev, target->hci_reader_gate,
					   NFC_HCI_WR_XCHG_DATA, skb->data,
					   skb->len, hci_transceive_cb, hdev);
		break;
	default:
		if (hdev->ops->im_transceive) {
			r = hdev->ops->im_transceive(hdev, target, skb, cb,
						     cb_context);
			if (r == 1)
				r = -ENOTSUPP;
		} else {
			r = -ENOTSUPP;
		}
		break;
	}

	kfree_skb(skb);

	return r;
}

static int hci_tm_send(struct nfc_dev *nfc_dev, struct sk_buff *skb)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);

	if (hdev->ops->tm_send)
		return hdev->ops->tm_send(hdev, skb);
	else
		return -ENOTSUPP;
}

static int hci_check_presence(struct nfc_dev *nfc_dev,
			      struct nfc_target *target)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(nfc_dev);

	if (hdev->ops->check_presence)
		return hdev->ops->check_presence(hdev, target);

	return 0;
}

static void nfc_hci_failure(struct nfc_hci_dev *hdev, int err)
{
	mutex_lock(&hdev->msg_tx_mutex);

	if (hdev->cmd_pending_msg == NULL) {
		nfc_driver_failure(hdev->ndev, err);
		goto exit;
	}

	__nfc_hci_cmd_completion(hdev, err, NULL);

exit:
	mutex_unlock(&hdev->msg_tx_mutex);
}

static void nfc_hci_llc_failure(struct nfc_hci_dev *hdev, int err)
{
	nfc_hci_failure(hdev, err);
}

static void nfc_hci_recv_from_llc(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	struct hcp_packet *packet;
	u8 type;
	u8 instruction;
	struct sk_buff *hcp_skb;
	u8 pipe;
	struct sk_buff *frag_skb;
	int msg_len;

	packet = (struct hcp_packet *)skb->data;
	if ((packet->header & ~NFC_HCI_FRAGMENT) == 0) {
		skb_queue_tail(&hdev->rx_hcp_frags, skb);
		return;
	}

	/* it's the last fragment. Does it need re-aggregation? */
	if (skb_queue_len(&hdev->rx_hcp_frags)) {
		pipe = packet->header & NFC_HCI_FRAGMENT;
		skb_queue_tail(&hdev->rx_hcp_frags, skb);

		msg_len = 0;
		skb_queue_walk(&hdev->rx_hcp_frags, frag_skb) {
			msg_len += (frag_skb->len -
				    NFC_HCI_HCP_PACKET_HEADER_LEN);
		}

		hcp_skb = nfc_alloc_recv_skb(NFC_HCI_HCP_PACKET_HEADER_LEN +
					     msg_len, GFP_KERNEL);
		if (hcp_skb == NULL) {
			nfc_hci_failure(hdev, -ENOMEM);
			return;
		}

		*skb_put(hcp_skb, NFC_HCI_HCP_PACKET_HEADER_LEN) = pipe;

		skb_queue_walk(&hdev->rx_hcp_frags, frag_skb) {
			msg_len = frag_skb->len - NFC_HCI_HCP_PACKET_HEADER_LEN;
			memcpy(skb_put(hcp_skb, msg_len),
			       frag_skb->data + NFC_HCI_HCP_PACKET_HEADER_LEN,
			       msg_len);
		}

		skb_queue_purge(&hdev->rx_hcp_frags);
	} else {
		packet->header &= NFC_HCI_FRAGMENT;
		hcp_skb = skb;
	}

	/* if this is a response, dispatch immediately to
	 * unblock waiting cmd context. Otherwise, enqueue to dispatch
	 * in separate context where handler can also execute command.
	 */
	packet = (struct hcp_packet *)hcp_skb->data;
	type = HCP_MSG_GET_TYPE(packet->message.header);
	if (type == NFC_HCI_HCP_RESPONSE) {
		pipe = packet->header;
		instruction = HCP_MSG_GET_CMD(packet->message.header);
		skb_pull(hcp_skb, NFC_HCI_HCP_PACKET_HEADER_LEN +
			 NFC_HCI_HCP_MESSAGE_HEADER_LEN);
		nfc_hci_hcp_message_rx(hdev, pipe, type, instruction, hcp_skb);
	} else {
		skb_queue_tail(&hdev->msg_rx_queue, hcp_skb);
		schedule_work(&hdev->msg_rx_work);
	}
}

static struct nfc_ops hci_nfc_ops = {
	.dev_up = hci_dev_up,
	.dev_down = hci_dev_down,
	.start_poll = hci_start_poll,
	.stop_poll = hci_stop_poll,
	.dep_link_up = hci_dep_link_up,
	.dep_link_down = hci_dep_link_down,
	.activate_target = hci_activate_target,
	.deactivate_target = hci_deactivate_target,
	.im_transceive = hci_transceive,
	.tm_send = hci_tm_send,
	.check_presence = hci_check_presence,
};

struct nfc_hci_dev *nfc_hci_allocate_device(struct nfc_hci_ops *ops,
					    struct nfc_hci_init_data *init_data,
					    u32 protocols,
					    const char *llc_name,
					    int tx_headroom,
					    int tx_tailroom,
					    int max_link_payload)
{
	struct nfc_hci_dev *hdev;

	if (ops->xmit == NULL)
		return NULL;

	if (protocols == 0)
		return NULL;

	hdev = kzalloc(sizeof(struct nfc_hci_dev), GFP_KERNEL);
	if (hdev == NULL)
		return NULL;

	hdev->llc = nfc_llc_allocate(llc_name, hdev, ops->xmit,
				     nfc_hci_recv_from_llc, tx_headroom,
				     tx_tailroom, nfc_hci_llc_failure);
	if (hdev->llc == NULL) {
		kfree(hdev);
		return NULL;
	}

	hdev->ndev = nfc_allocate_device(&hci_nfc_ops, protocols,
					 tx_headroom + HCI_CMDS_HEADROOM,
					 tx_tailroom);
	if (!hdev->ndev) {
		nfc_llc_free(hdev->llc);
		kfree(hdev);
		return NULL;
	}

	hdev->ops = ops;
	hdev->max_data_link_payload = max_link_payload;
	hdev->init_data = *init_data;

	nfc_set_drvdata(hdev->ndev, hdev);

	memset(hdev->gate2pipe, NFC_HCI_INVALID_PIPE, sizeof(hdev->gate2pipe));

	return hdev;
}
EXPORT_SYMBOL(nfc_hci_allocate_device);

void nfc_hci_free_device(struct nfc_hci_dev *hdev)
{
	nfc_free_device(hdev->ndev);
	nfc_llc_free(hdev->llc);
	kfree(hdev);
}
EXPORT_SYMBOL(nfc_hci_free_device);

int nfc_hci_register_device(struct nfc_hci_dev *hdev)
{
	mutex_init(&hdev->msg_tx_mutex);

	INIT_LIST_HEAD(&hdev->msg_tx_queue);

	INIT_WORK(&hdev->msg_tx_work, nfc_hci_msg_tx_work);

	init_timer(&hdev->cmd_timer);
	hdev->cmd_timer.data = (unsigned long)hdev;
	hdev->cmd_timer.function = nfc_hci_cmd_timeout;

	skb_queue_head_init(&hdev->rx_hcp_frags);

	INIT_WORK(&hdev->msg_rx_work, nfc_hci_msg_rx_work);

	skb_queue_head_init(&hdev->msg_rx_queue);

	return nfc_register_device(hdev->ndev);
}
EXPORT_SYMBOL(nfc_hci_register_device);

void nfc_hci_unregister_device(struct nfc_hci_dev *hdev)
{
	struct hci_msg *msg, *n;

	skb_queue_purge(&hdev->rx_hcp_frags);
	skb_queue_purge(&hdev->msg_rx_queue);

	list_for_each_entry_safe(msg, n, &hdev->msg_tx_queue, msg_l) {
		list_del(&msg->msg_l);
		skb_queue_purge(&msg->msg_frags);
		kfree(msg);
	}

	del_timer_sync(&hdev->cmd_timer);

	nfc_unregister_device(hdev->ndev);

	cancel_work_sync(&hdev->msg_tx_work);
	cancel_work_sync(&hdev->msg_rx_work);
}
EXPORT_SYMBOL(nfc_hci_unregister_device);

void nfc_hci_set_clientdata(struct nfc_hci_dev *hdev, void *clientdata)
{
	hdev->clientdata = clientdata;
}
EXPORT_SYMBOL(nfc_hci_set_clientdata);

void *nfc_hci_get_clientdata(struct nfc_hci_dev *hdev)
{
	return hdev->clientdata;
}
EXPORT_SYMBOL(nfc_hci_get_clientdata);

void nfc_hci_driver_failure(struct nfc_hci_dev *hdev, int err)
{
	nfc_hci_failure(hdev, err);
}
EXPORT_SYMBOL(nfc_hci_driver_failure);

void nfc_hci_recv_frame(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	nfc_llc_rcv_from_drv(hdev->llc, skb);
}
EXPORT_SYMBOL(nfc_hci_recv_frame);

static int __init nfc_hci_init(void)
{
	return nfc_llc_init();
}

static void __exit nfc_hci_exit(void)
{
	nfc_llc_exit();
}

subsys_initcall(nfc_hci_init);
module_exit(nfc_hci_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NFC HCI Core");
