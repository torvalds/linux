// SPDX-License-Identifier: GPL-2.0-only
/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *  This is the HCI over NCI implementation, as specified in the 10.2
 *  section of the NCI 1.1 specification.
 *
 *  Copyright (C) 2014  STMicroelectronics SAS. All rights reserved.
 */

#include <linux/skbuff.h>

#include "../nfc.h"
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>
#include <linux/nfc.h>
#include <linux/kcov.h>

struct nci_data {
	u8 conn_id;
	u8 pipe;
	u8 cmd;
	const u8 *data;
	u32 data_len;
} __packed;

struct nci_hci_create_pipe_params {
	u8 src_gate;
	u8 dest_host;
	u8 dest_gate;
} __packed;

struct nci_hci_create_pipe_resp {
	u8 src_host;
	u8 src_gate;
	u8 dest_host;
	u8 dest_gate;
	u8 pipe;
} __packed;

struct nci_hci_delete_pipe_noti {
	u8 pipe;
} __packed;

struct nci_hci_all_pipe_cleared_noti {
	u8 host;
} __packed;

struct nci_hcp_message {
	u8 header;      /* type -cmd,evt,rsp- + instruction */
	u8 data[];
} __packed;

struct nci_hcp_packet {
	u8 header;      /* cbit+pipe */
	struct nci_hcp_message message;
} __packed;

#define NCI_HCI_ANY_SET_PARAMETER  0x01
#define NCI_HCI_ANY_GET_PARAMETER  0x02
#define NCI_HCI_ANY_CLOSE_PIPE     0x04
#define NCI_HCI_ADM_CLEAR_ALL_PIPE 0x14

#define NCI_HFP_NO_CHAINING        0x80

#define NCI_NFCEE_ID_HCI                0x80

#define NCI_EVT_HOT_PLUG           0x03

#define NCI_HCI_ADMIN_PARAM_SESSION_IDENTITY       0x01
#define NCI_HCI_ADM_CREATE_PIPE			0x10
#define NCI_HCI_ADM_DELETE_PIPE			0x11

/* HCP headers */
#define NCI_HCI_HCP_PACKET_HEADER_LEN      1
#define NCI_HCI_HCP_MESSAGE_HEADER_LEN     1
#define NCI_HCI_HCP_HEADER_LEN             2

/* HCP types */
#define NCI_HCI_HCP_COMMAND        0x00
#define NCI_HCI_HCP_EVENT          0x01
#define NCI_HCI_HCP_RESPONSE       0x02

#define NCI_HCI_ADM_NOTIFY_PIPE_CREATED     0x12
#define NCI_HCI_ADM_NOTIFY_PIPE_DELETED     0x13
#define NCI_HCI_ADM_NOTIFY_ALL_PIPE_CLEARED 0x15

#define NCI_HCI_FRAGMENT           0x7f
#define NCI_HCP_HEADER(type, instr) ((((type) & 0x03) << 6) |\
				      ((instr) & 0x3f))

#define NCI_HCP_MSG_GET_TYPE(header) ((header & 0xc0) >> 6)
#define NCI_HCP_MSG_GET_CMD(header)  (header & 0x3f)
#define NCI_HCP_MSG_GET_PIPE(header) (header & 0x7f)

static int nci_hci_result_to_errno(u8 result)
{
	switch (result) {
	case NCI_HCI_ANY_OK:
		return 0;
	case NCI_HCI_ANY_E_REG_PAR_UNKNOWN:
		return -EOPNOTSUPP;
	case NCI_HCI_ANY_E_TIMEOUT:
		return -ETIME;
	default:
		return -1;
	}
}

/* HCI core */
static void nci_hci_reset_pipes(struct nci_hci_dev *hdev)
{
	int i;

	for (i = 0; i < NCI_HCI_MAX_PIPES; i++) {
		hdev->pipes[i].gate = NCI_HCI_INVALID_GATE;
		hdev->pipes[i].host = NCI_HCI_INVALID_HOST;
	}
	memset(hdev->gate2pipe, NCI_HCI_INVALID_PIPE, sizeof(hdev->gate2pipe));
}

static void nci_hci_reset_pipes_per_host(struct nci_dev *ndev, u8 host)
{
	int i;

	for (i = 0; i < NCI_HCI_MAX_PIPES; i++) {
		if (ndev->hci_dev->pipes[i].host == host) {
			ndev->hci_dev->pipes[i].gate = NCI_HCI_INVALID_GATE;
			ndev->hci_dev->pipes[i].host = NCI_HCI_INVALID_HOST;
		}
	}
}

/* Fragment HCI data over NCI packet.
 * NFC Forum NCI 10.2.2 Data Exchange:
 * The payload of the Data Packets sent on the Logical Connection SHALL be
 * valid HCP packets, as defined within [ETSI_102622]. Each Data Packet SHALL
 * contain a single HCP packet. NCI Segmentation and Reassembly SHALL NOT be
 * applied to Data Messages in either direction. The HCI fragmentation mechanism
 * is used if required.
 */
static int nci_hci_send_data(struct nci_dev *ndev, u8 pipe,
			     const u8 data_type, const u8 *data,
			     size_t data_len)
{
	const struct nci_conn_info *conn_info;
	struct sk_buff *skb;
	int len, i, r;
	u8 cb = pipe;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		return -EPROTO;

	i = 0;
	skb = nci_skb_alloc(ndev, conn_info->max_pkt_payload_len +
			    NCI_DATA_HDR_SIZE, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, NCI_DATA_HDR_SIZE + 2);
	*(u8 *)skb_push(skb, 1) = data_type;

	do {
		/* If last packet add NCI_HFP_NO_CHAINING */
		if (i + conn_info->max_pkt_payload_len -
		    (skb->len + 1) >= data_len) {
			cb |= NCI_HFP_NO_CHAINING;
			len = data_len - i;
		} else {
			len = conn_info->max_pkt_payload_len - skb->len - 1;
		}

		*(u8 *)skb_push(skb, 1) = cb;

		if (len > 0)
			skb_put_data(skb, data + i, len);

		r = nci_send_data(ndev, conn_info->conn_id, skb);
		if (r < 0)
			return r;

		i += len;

		if (i < data_len) {
			skb = nci_skb_alloc(ndev,
					    conn_info->max_pkt_payload_len +
					    NCI_DATA_HDR_SIZE, GFP_ATOMIC);
			if (!skb)
				return -ENOMEM;

			skb_reserve(skb, NCI_DATA_HDR_SIZE + 1);
		}
	} while (i < data_len);

	return i;
}

static void nci_hci_send_data_req(struct nci_dev *ndev, const void *opt)
{
	const struct nci_data *data = opt;

	nci_hci_send_data(ndev, data->pipe, data->cmd,
			  data->data, data->data_len);
}

int nci_hci_send_event(struct nci_dev *ndev, u8 gate, u8 event,
		       const u8 *param, size_t param_len)
{
	u8 pipe = ndev->hci_dev->gate2pipe[gate];

	if (pipe == NCI_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	return nci_hci_send_data(ndev, pipe,
			NCI_HCP_HEADER(NCI_HCI_HCP_EVENT, event),
			param, param_len);
}
EXPORT_SYMBOL(nci_hci_send_event);

int nci_hci_send_cmd(struct nci_dev *ndev, u8 gate, u8 cmd,
		     const u8 *param, size_t param_len,
		     struct sk_buff **skb)
{
	const struct nci_hcp_message *message;
	const struct nci_conn_info *conn_info;
	struct nci_data data;
	int r;
	u8 pipe = ndev->hci_dev->gate2pipe[gate];

	if (pipe == NCI_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		return -EPROTO;

	data.conn_id = conn_info->conn_id;
	data.pipe = pipe;
	data.cmd = NCI_HCP_HEADER(NCI_HCI_HCP_COMMAND, cmd);
	data.data = param;
	data.data_len = param_len;

	r = nci_request(ndev, nci_hci_send_data_req, &data,
			msecs_to_jiffies(NCI_DATA_TIMEOUT));
	if (r == NCI_STATUS_OK) {
		message = (struct nci_hcp_message *)conn_info->rx_skb->data;
		r = nci_hci_result_to_errno(
			NCI_HCP_MSG_GET_CMD(message->header));
		skb_pull(conn_info->rx_skb, NCI_HCI_HCP_MESSAGE_HEADER_LEN);

		if (!r && skb)
			*skb = conn_info->rx_skb;
	}

	return r;
}
EXPORT_SYMBOL(nci_hci_send_cmd);

int nci_hci_clear_all_pipes(struct nci_dev *ndev)
{
	int r;

	r = nci_hci_send_cmd(ndev, NCI_HCI_ADMIN_GATE,
			     NCI_HCI_ADM_CLEAR_ALL_PIPE, NULL, 0, NULL);
	if (r < 0)
		return r;

	nci_hci_reset_pipes(ndev->hci_dev);
	return r;
}
EXPORT_SYMBOL(nci_hci_clear_all_pipes);

static void nci_hci_event_received(struct nci_dev *ndev, u8 pipe,
				   u8 event, struct sk_buff *skb)
{
	if (ndev->ops->hci_event_received)
		ndev->ops->hci_event_received(ndev, pipe, event, skb);
}

static void nci_hci_cmd_received(struct nci_dev *ndev, u8 pipe,
				 u8 cmd, struct sk_buff *skb)
{
	u8 gate = ndev->hci_dev->pipes[pipe].gate;
	u8 status = NCI_HCI_ANY_OK | ~NCI_HCI_FRAGMENT;
	u8 dest_gate, new_pipe;
	struct nci_hci_create_pipe_resp *create_info;
	struct nci_hci_delete_pipe_noti *delete_info;
	struct nci_hci_all_pipe_cleared_noti *cleared_info;

	pr_debug("from gate %x pipe %x cmd %x\n", gate, pipe, cmd);

	switch (cmd) {
	case NCI_HCI_ADM_NOTIFY_PIPE_CREATED:
		if (skb->len != 5) {
			status = NCI_HCI_ANY_E_NOK;
			goto exit;
		}
		create_info = (struct nci_hci_create_pipe_resp *)skb->data;
		dest_gate = create_info->dest_gate;
		new_pipe = create_info->pipe;
		if (new_pipe >= NCI_HCI_MAX_PIPES) {
			status = NCI_HCI_ANY_E_NOK;
			goto exit;
		}

		/* Save the new created pipe and bind with local gate,
		 * the description for skb->data[3] is destination gate id
		 * but since we received this cmd from host controller, we
		 * are the destination and it is our local gate
		 */
		ndev->hci_dev->gate2pipe[dest_gate] = new_pipe;
		ndev->hci_dev->pipes[new_pipe].gate = dest_gate;
		ndev->hci_dev->pipes[new_pipe].host =
						create_info->src_host;
		break;
	case NCI_HCI_ANY_OPEN_PIPE:
		/* If the pipe is not created report an error */
		if (gate == NCI_HCI_INVALID_GATE) {
			status = NCI_HCI_ANY_E_NOK;
			goto exit;
		}
		break;
	case NCI_HCI_ADM_NOTIFY_PIPE_DELETED:
		if (skb->len != 1) {
			status = NCI_HCI_ANY_E_NOK;
			goto exit;
		}
		delete_info = (struct nci_hci_delete_pipe_noti *)skb->data;
		if (delete_info->pipe >= NCI_HCI_MAX_PIPES) {
			status = NCI_HCI_ANY_E_NOK;
			goto exit;
		}

		ndev->hci_dev->pipes[delete_info->pipe].gate =
						NCI_HCI_INVALID_GATE;
		ndev->hci_dev->pipes[delete_info->pipe].host =
						NCI_HCI_INVALID_HOST;
		break;
	case NCI_HCI_ADM_NOTIFY_ALL_PIPE_CLEARED:
		if (skb->len != 1) {
			status = NCI_HCI_ANY_E_NOK;
			goto exit;
		}

		cleared_info =
			(struct nci_hci_all_pipe_cleared_noti *)skb->data;
		nci_hci_reset_pipes_per_host(ndev, cleared_info->host);
		break;
	default:
		pr_debug("Discarded unknown cmd %x to gate %x\n", cmd, gate);
		break;
	}

	if (ndev->ops->hci_cmd_received)
		ndev->ops->hci_cmd_received(ndev, pipe, cmd, skb);

exit:
	nci_hci_send_data(ndev, pipe, status, NULL, 0);

	kfree_skb(skb);
}

static void nci_hci_resp_received(struct nci_dev *ndev, u8 pipe,
				  struct sk_buff *skb)
{
	struct nci_conn_info *conn_info;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		goto exit;

	conn_info->rx_skb = skb;

exit:
	nci_req_complete(ndev, NCI_STATUS_OK);
}

/* Receive hcp message for pipe, with type and cmd.
 * skb contains optional message data only.
 */
static void nci_hci_hcp_message_rx(struct nci_dev *ndev, u8 pipe,
				   u8 type, u8 instruction, struct sk_buff *skb)
{
	switch (type) {
	case NCI_HCI_HCP_RESPONSE:
		nci_hci_resp_received(ndev, pipe, skb);
		break;
	case NCI_HCI_HCP_COMMAND:
		nci_hci_cmd_received(ndev, pipe, instruction, skb);
		break;
	case NCI_HCI_HCP_EVENT:
		nci_hci_event_received(ndev, pipe, instruction, skb);
		break;
	default:
		pr_err("UNKNOWN MSG Type %d, instruction=%d\n",
		       type, instruction);
		kfree_skb(skb);
		break;
	}

	nci_req_complete(ndev, NCI_STATUS_OK);
}

static void nci_hci_msg_rx_work(struct work_struct *work)
{
	struct nci_hci_dev *hdev =
		container_of(work, struct nci_hci_dev, msg_rx_work);
	struct sk_buff *skb;
	const struct nci_hcp_message *message;
	u8 pipe, type, instruction;

	for (; (skb = skb_dequeue(&hdev->msg_rx_queue)); kcov_remote_stop()) {
		kcov_remote_start_common(skb_get_kcov_handle(skb));
		pipe = NCI_HCP_MSG_GET_PIPE(skb->data[0]);
		skb_pull(skb, NCI_HCI_HCP_PACKET_HEADER_LEN);
		message = (struct nci_hcp_message *)skb->data;
		type = NCI_HCP_MSG_GET_TYPE(message->header);
		instruction = NCI_HCP_MSG_GET_CMD(message->header);
		skb_pull(skb, NCI_HCI_HCP_MESSAGE_HEADER_LEN);

		nci_hci_hcp_message_rx(hdev->ndev, pipe,
				       type, instruction, skb);
	}
}

void nci_hci_data_received_cb(void *context,
			      struct sk_buff *skb, int err)
{
	struct nci_dev *ndev = (struct nci_dev *)context;
	struct nci_hcp_packet *packet;
	u8 pipe, type;
	struct sk_buff *hcp_skb;
	struct sk_buff *frag_skb;
	int msg_len;

	if (err) {
		nci_req_complete(ndev, err);
		return;
	}

	packet = (struct nci_hcp_packet *)skb->data;
	if ((packet->header & ~NCI_HCI_FRAGMENT) == 0) {
		skb_queue_tail(&ndev->hci_dev->rx_hcp_frags, skb);
		return;
	}

	/* it's the last fragment. Does it need re-aggregation? */
	if (skb_queue_len(&ndev->hci_dev->rx_hcp_frags)) {
		pipe = NCI_HCP_MSG_GET_PIPE(packet->header);
		skb_queue_tail(&ndev->hci_dev->rx_hcp_frags, skb);

		msg_len = 0;
		skb_queue_walk(&ndev->hci_dev->rx_hcp_frags, frag_skb) {
			msg_len += (frag_skb->len -
				    NCI_HCI_HCP_PACKET_HEADER_LEN);
		}

		hcp_skb = nfc_alloc_recv_skb(NCI_HCI_HCP_PACKET_HEADER_LEN +
					     msg_len, GFP_KERNEL);
		if (!hcp_skb) {
			nci_req_complete(ndev, -ENOMEM);
			return;
		}

		skb_put_u8(hcp_skb, pipe);

		skb_queue_walk(&ndev->hci_dev->rx_hcp_frags, frag_skb) {
			msg_len = frag_skb->len - NCI_HCI_HCP_PACKET_HEADER_LEN;
			skb_put_data(hcp_skb,
				     frag_skb->data + NCI_HCI_HCP_PACKET_HEADER_LEN,
				     msg_len);
		}

		skb_queue_purge(&ndev->hci_dev->rx_hcp_frags);
	} else {
		packet->header &= NCI_HCI_FRAGMENT;
		hcp_skb = skb;
	}

	/* if this is a response, dispatch immediately to
	 * unblock waiting cmd context. Otherwise, enqueue to dispatch
	 * in separate context where handler can also execute command.
	 */
	packet = (struct nci_hcp_packet *)hcp_skb->data;
	type = NCI_HCP_MSG_GET_TYPE(packet->message.header);
	if (type == NCI_HCI_HCP_RESPONSE) {
		pipe = NCI_HCP_MSG_GET_PIPE(packet->header);
		skb_pull(hcp_skb, NCI_HCI_HCP_PACKET_HEADER_LEN);
		nci_hci_hcp_message_rx(ndev, pipe, type,
				       NCI_STATUS_OK, hcp_skb);
	} else {
		skb_queue_tail(&ndev->hci_dev->msg_rx_queue, hcp_skb);
		schedule_work(&ndev->hci_dev->msg_rx_work);
	}
}

int nci_hci_open_pipe(struct nci_dev *ndev, u8 pipe)
{
	struct nci_data data;
	const struct nci_conn_info *conn_info;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		return -EPROTO;

	data.conn_id = conn_info->conn_id;
	data.pipe = pipe;
	data.cmd = NCI_HCP_HEADER(NCI_HCI_HCP_COMMAND,
				       NCI_HCI_ANY_OPEN_PIPE);
	data.data = NULL;
	data.data_len = 0;

	return nci_request(ndev, nci_hci_send_data_req, &data,
			   msecs_to_jiffies(NCI_DATA_TIMEOUT));
}
EXPORT_SYMBOL(nci_hci_open_pipe);

static u8 nci_hci_create_pipe(struct nci_dev *ndev, u8 dest_host,
			      u8 dest_gate, int *result)
{
	u8 pipe;
	struct sk_buff *skb;
	struct nci_hci_create_pipe_params params;
	const struct nci_hci_create_pipe_resp *resp;

	pr_debug("gate=%d\n", dest_gate);

	params.src_gate = NCI_HCI_ADMIN_GATE;
	params.dest_host = dest_host;
	params.dest_gate = dest_gate;

	*result = nci_hci_send_cmd(ndev, NCI_HCI_ADMIN_GATE,
				   NCI_HCI_ADM_CREATE_PIPE,
				   (u8 *)&params, sizeof(params), &skb);
	if (*result < 0)
		return NCI_HCI_INVALID_PIPE;

	resp = (struct nci_hci_create_pipe_resp *)skb->data;
	pipe = resp->pipe;
	kfree_skb(skb);

	pr_debug("pipe created=%d\n", pipe);

	if (pipe >= NCI_HCI_MAX_PIPES)
		pipe = NCI_HCI_INVALID_PIPE;
	return pipe;
}

static int nci_hci_delete_pipe(struct nci_dev *ndev, u8 pipe)
{
	return nci_hci_send_cmd(ndev, NCI_HCI_ADMIN_GATE,
				NCI_HCI_ADM_DELETE_PIPE, &pipe, 1, NULL);
}

int nci_hci_set_param(struct nci_dev *ndev, u8 gate, u8 idx,
		      const u8 *param, size_t param_len)
{
	const struct nci_hcp_message *message;
	const struct nci_conn_info *conn_info;
	struct nci_data data;
	int r;
	u8 *tmp;
	u8 pipe = ndev->hci_dev->gate2pipe[gate];

	pr_debug("idx=%d to gate %d\n", idx, gate);

	if (pipe == NCI_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		return -EPROTO;

	tmp = kmalloc(1 + param_len, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	*tmp = idx;
	memcpy(tmp + 1, param, param_len);

	data.conn_id = conn_info->conn_id;
	data.pipe = pipe;
	data.cmd = NCI_HCP_HEADER(NCI_HCI_HCP_COMMAND,
				       NCI_HCI_ANY_SET_PARAMETER);
	data.data = tmp;
	data.data_len = param_len + 1;

	r = nci_request(ndev, nci_hci_send_data_req, &data,
			msecs_to_jiffies(NCI_DATA_TIMEOUT));
	if (r == NCI_STATUS_OK) {
		message = (struct nci_hcp_message *)conn_info->rx_skb->data;
		r = nci_hci_result_to_errno(
			NCI_HCP_MSG_GET_CMD(message->header));
		skb_pull(conn_info->rx_skb, NCI_HCI_HCP_MESSAGE_HEADER_LEN);
	}

	kfree(tmp);
	return r;
}
EXPORT_SYMBOL(nci_hci_set_param);

int nci_hci_get_param(struct nci_dev *ndev, u8 gate, u8 idx,
		      struct sk_buff **skb)
{
	const struct nci_hcp_message *message;
	const struct nci_conn_info *conn_info;
	struct nci_data data;
	int r;
	u8 pipe = ndev->hci_dev->gate2pipe[gate];

	pr_debug("idx=%d to gate %d\n", idx, gate);

	if (pipe == NCI_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		return -EPROTO;

	data.conn_id = conn_info->conn_id;
	data.pipe = pipe;
	data.cmd = NCI_HCP_HEADER(NCI_HCI_HCP_COMMAND,
				  NCI_HCI_ANY_GET_PARAMETER);
	data.data = &idx;
	data.data_len = 1;

	r = nci_request(ndev, nci_hci_send_data_req, &data,
			msecs_to_jiffies(NCI_DATA_TIMEOUT));

	if (r == NCI_STATUS_OK) {
		message = (struct nci_hcp_message *)conn_info->rx_skb->data;
		r = nci_hci_result_to_errno(
			NCI_HCP_MSG_GET_CMD(message->header));
		skb_pull(conn_info->rx_skb, NCI_HCI_HCP_MESSAGE_HEADER_LEN);

		if (!r && skb)
			*skb = conn_info->rx_skb;
	}

	return r;
}
EXPORT_SYMBOL(nci_hci_get_param);

int nci_hci_connect_gate(struct nci_dev *ndev,
			 u8 dest_host, u8 dest_gate, u8 pipe)
{
	bool pipe_created = false;
	int r;

	if (pipe == NCI_HCI_DO_NOT_OPEN_PIPE)
		return 0;

	if (ndev->hci_dev->gate2pipe[dest_gate] != NCI_HCI_INVALID_PIPE)
		return -EADDRINUSE;

	if (pipe != NCI_HCI_INVALID_PIPE)
		goto open_pipe;

	switch (dest_gate) {
	case NCI_HCI_LINK_MGMT_GATE:
		pipe = NCI_HCI_LINK_MGMT_PIPE;
	break;
	case NCI_HCI_ADMIN_GATE:
		pipe = NCI_HCI_ADMIN_PIPE;
	break;
	default:
		pipe = nci_hci_create_pipe(ndev, dest_host, dest_gate, &r);
		if (pipe == NCI_HCI_INVALID_PIPE)
			return r;
		pipe_created = true;
		break;
	}

open_pipe:
	r = nci_hci_open_pipe(ndev, pipe);
	if (r < 0) {
		if (pipe_created) {
			if (nci_hci_delete_pipe(ndev, pipe) < 0) {
				/* TODO: Cannot clean by deleting pipe...
				 * -> inconsistent state
				 */
			}
		}
		return r;
	}

	ndev->hci_dev->pipes[pipe].gate = dest_gate;
	ndev->hci_dev->pipes[pipe].host = dest_host;
	ndev->hci_dev->gate2pipe[dest_gate] = pipe;

	return 0;
}
EXPORT_SYMBOL(nci_hci_connect_gate);

static int nci_hci_dev_connect_gates(struct nci_dev *ndev,
				     u8 gate_count,
				     const struct nci_hci_gate *gates)
{
	int r;

	while (gate_count--) {
		r = nci_hci_connect_gate(ndev, gates->dest_host,
					 gates->gate, gates->pipe);
		if (r < 0)
			return r;
		gates++;
	}

	return 0;
}

int nci_hci_dev_session_init(struct nci_dev *ndev)
{
	struct nci_conn_info *conn_info;
	struct sk_buff *skb;
	int r;

	ndev->hci_dev->count_pipes = 0;
	ndev->hci_dev->expected_pipes = 0;

	conn_info = ndev->hci_dev->conn_info;
	if (!conn_info)
		return -EPROTO;

	conn_info->data_exchange_cb = nci_hci_data_received_cb;
	conn_info->data_exchange_cb_context = ndev;

	nci_hci_reset_pipes(ndev->hci_dev);

	if (ndev->hci_dev->init_data.gates[0].gate != NCI_HCI_ADMIN_GATE)
		return -EPROTO;

	r = nci_hci_connect_gate(ndev,
				 ndev->hci_dev->init_data.gates[0].dest_host,
				 ndev->hci_dev->init_data.gates[0].gate,
				 ndev->hci_dev->init_data.gates[0].pipe);
	if (r < 0)
		return r;

	r = nci_hci_get_param(ndev, NCI_HCI_ADMIN_GATE,
			      NCI_HCI_ADMIN_PARAM_SESSION_IDENTITY, &skb);
	if (r < 0)
		return r;

	if (skb->len &&
	    skb->len == strlen(ndev->hci_dev->init_data.session_id) &&
	    !memcmp(ndev->hci_dev->init_data.session_id, skb->data, skb->len) &&
	    ndev->ops->hci_load_session) {
		/* Restore gate<->pipe table from some proprietary location. */
		r = ndev->ops->hci_load_session(ndev);
	} else {
		r = nci_hci_clear_all_pipes(ndev);
		if (r < 0)
			goto exit;

		r = nci_hci_dev_connect_gates(ndev,
					      ndev->hci_dev->init_data.gate_count,
					      ndev->hci_dev->init_data.gates);
		if (r < 0)
			goto exit;

		r = nci_hci_set_param(ndev, NCI_HCI_ADMIN_GATE,
				      NCI_HCI_ADMIN_PARAM_SESSION_IDENTITY,
				      ndev->hci_dev->init_data.session_id,
				      strlen(ndev->hci_dev->init_data.session_id));
	}

exit:
	kfree_skb(skb);

	return r;
}
EXPORT_SYMBOL(nci_hci_dev_session_init);

struct nci_hci_dev *nci_hci_allocate(struct nci_dev *ndev)
{
	struct nci_hci_dev *hdev;

	hdev = kzalloc(sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return NULL;

	skb_queue_head_init(&hdev->rx_hcp_frags);
	INIT_WORK(&hdev->msg_rx_work, nci_hci_msg_rx_work);
	skb_queue_head_init(&hdev->msg_rx_queue);
	hdev->ndev = ndev;

	return hdev;
}

void nci_hci_deallocate(struct nci_dev *ndev)
{
	kfree(ndev->hci_dev);
}
