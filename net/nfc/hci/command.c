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
#include <linux/sched.h>
#include <linux/module.h>

#include <net/nfc/hci.h>

#include "hci.h"

static int nfc_hci_execute_cmd_async(struct nfc_hci_dev *hdev, u8 pipe, u8 cmd,
			       const u8 *param, size_t param_len,
			       data_exchange_cb_t cb, void *cb_context)
{
	pr_debug("exec cmd async through pipe=%d, cmd=%d, plen=%zd\n", pipe,
		 cmd, param_len);

	/* TODO: Define hci cmd execution delay. Should it be the same
	 * for all commands?
	 */
	return nfc_hci_hcp_message_tx(hdev, pipe, NFC_HCI_HCP_COMMAND, cmd,
				      param, param_len, cb, cb_context, 3000);
}

/*
 * HCI command execution completion callback.
 * err will be a standard linux error (may be converted from HCI response)
 * skb contains the response data and must be disposed, or may be NULL if
 * an error occured
 */
static void nfc_hci_execute_cb(void *context, struct sk_buff *skb, int err)
{
	struct hcp_exec_waiter *hcp_ew = (struct hcp_exec_waiter *)context;

	pr_debug("HCI Cmd completed with result=%d\n", err);

	hcp_ew->exec_result = err;
	if (hcp_ew->exec_result == 0)
		hcp_ew->result_skb = skb;
	else
		kfree_skb(skb);
	hcp_ew->exec_complete = true;

	wake_up(hcp_ew->wq);
}

static int nfc_hci_execute_cmd(struct nfc_hci_dev *hdev, u8 pipe, u8 cmd,
			       const u8 *param, size_t param_len,
			       struct sk_buff **skb)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(ew_wq);
	struct hcp_exec_waiter hcp_ew;
	hcp_ew.wq = &ew_wq;
	hcp_ew.exec_complete = false;
	hcp_ew.result_skb = NULL;

	pr_debug("exec cmd sync through pipe=%d, cmd=%d, plen=%zd\n", pipe,
		 cmd, param_len);

	/* TODO: Define hci cmd execution delay. Should it be the same
	 * for all commands?
	 */
	hcp_ew.exec_result = nfc_hci_hcp_message_tx(hdev, pipe,
						    NFC_HCI_HCP_COMMAND, cmd,
						    param, param_len,
						    nfc_hci_execute_cb, &hcp_ew,
						    3000);
	if (hcp_ew.exec_result < 0)
		return hcp_ew.exec_result;

	wait_event(ew_wq, hcp_ew.exec_complete == true);

	if (hcp_ew.exec_result == 0) {
		if (skb)
			*skb = hcp_ew.result_skb;
		else
			kfree_skb(hcp_ew.result_skb);
	}

	return hcp_ew.exec_result;
}

int nfc_hci_send_event(struct nfc_hci_dev *hdev, u8 gate, u8 event,
		       const u8 *param, size_t param_len)
{
	u8 pipe;

	pr_debug("%d to gate %d\n", event, gate);

	pipe = hdev->gate2pipe[gate];
	if (pipe == NFC_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	return nfc_hci_hcp_message_tx(hdev, pipe, NFC_HCI_HCP_EVENT, event,
				      param, param_len, NULL, NULL, 0);
}
EXPORT_SYMBOL(nfc_hci_send_event);

int nfc_hci_send_response(struct nfc_hci_dev *hdev, u8 gate, u8 response,
			  const u8 *param, size_t param_len)
{
	u8 pipe;

	pr_debug("\n");

	pipe = hdev->gate2pipe[gate];
	if (pipe == NFC_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	return nfc_hci_hcp_message_tx(hdev, pipe, NFC_HCI_HCP_RESPONSE,
				      response, param, param_len, NULL, NULL,
				      0);
}
EXPORT_SYMBOL(nfc_hci_send_response);

/*
 * Execute an hci command sent to gate.
 * skb will contain response data if success. skb can be NULL if you are not
 * interested by the response.
 */
int nfc_hci_send_cmd(struct nfc_hci_dev *hdev, u8 gate, u8 cmd,
		     const u8 *param, size_t param_len, struct sk_buff **skb)
{
	u8 pipe;

	pr_debug("\n");

	pipe = hdev->gate2pipe[gate];
	if (pipe == NFC_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	return nfc_hci_execute_cmd(hdev, pipe, cmd, param, param_len, skb);
}
EXPORT_SYMBOL(nfc_hci_send_cmd);

int nfc_hci_send_cmd_async(struct nfc_hci_dev *hdev, u8 gate, u8 cmd,
			   const u8 *param, size_t param_len,
			   data_exchange_cb_t cb, void *cb_context)
{
	u8 pipe;

	pr_debug("\n");

	pipe = hdev->gate2pipe[gate];
	if (pipe == NFC_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	return nfc_hci_execute_cmd_async(hdev, pipe, cmd, param, param_len,
					 cb, cb_context);
}
EXPORT_SYMBOL(nfc_hci_send_cmd_async);

int nfc_hci_set_param(struct nfc_hci_dev *hdev, u8 gate, u8 idx,
		      const u8 *param, size_t param_len)
{
	int r;
	u8 *tmp;

	/* TODO ELa: reg idx must be inserted before param, but we don't want
	 * to ask the caller to do it to keep a simpler API.
	 * For now, just create a new temporary param buffer. This is far from
	 * optimal though, and the plan is to modify APIs to pass idx down to
	 * nfc_hci_hcp_message_tx where the frame is actually built, thereby
	 * eliminating the need for the temp allocation-copy here.
	 */

	pr_debug("idx=%d to gate %d\n", idx, gate);

	tmp = kmalloc(1 + param_len, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	*tmp = idx;
	memcpy(tmp + 1, param, param_len);

	r = nfc_hci_send_cmd(hdev, gate, NFC_HCI_ANY_SET_PARAMETER,
			     tmp, param_len + 1, NULL);

	kfree(tmp);

	return r;
}
EXPORT_SYMBOL(nfc_hci_set_param);

int nfc_hci_get_param(struct nfc_hci_dev *hdev, u8 gate, u8 idx,
		      struct sk_buff **skb)
{
	pr_debug("gate=%d regidx=%d\n", gate, idx);

	return nfc_hci_send_cmd(hdev, gate, NFC_HCI_ANY_GET_PARAMETER,
				&idx, 1, skb);
}
EXPORT_SYMBOL(nfc_hci_get_param);

static int nfc_hci_open_pipe(struct nfc_hci_dev *hdev, u8 pipe)
{
	struct sk_buff *skb;
	int r;

	pr_debug("pipe=%d\n", pipe);

	r = nfc_hci_execute_cmd(hdev, pipe, NFC_HCI_ANY_OPEN_PIPE,
				NULL, 0, &skb);
	if (r == 0) {
		/* dest host other than host controller will send
		 * number of pipes already open on this gate before
		 * execution. The number can be found in skb->data[0]
		 */
		kfree_skb(skb);
	}

	return r;
}

static int nfc_hci_close_pipe(struct nfc_hci_dev *hdev, u8 pipe)
{
	pr_debug("\n");

	return nfc_hci_execute_cmd(hdev, pipe, NFC_HCI_ANY_CLOSE_PIPE,
				   NULL, 0, NULL);
}

static u8 nfc_hci_create_pipe(struct nfc_hci_dev *hdev, u8 dest_host,
			      u8 dest_gate, int *result)
{
	struct sk_buff *skb;
	struct hci_create_pipe_params params;
	struct hci_create_pipe_resp *resp;
	u8 pipe;

	pr_debug("gate=%d\n", dest_gate);

	params.src_gate = NFC_HCI_ADMIN_GATE;
	params.dest_host = dest_host;
	params.dest_gate = dest_gate;

	*result = nfc_hci_execute_cmd(hdev, NFC_HCI_ADMIN_PIPE,
				      NFC_HCI_ADM_CREATE_PIPE,
				      (u8 *) &params, sizeof(params), &skb);
	if (*result < 0)
		return NFC_HCI_INVALID_PIPE;

	resp = (struct hci_create_pipe_resp *)skb->data;
	pipe = resp->pipe;
	kfree_skb(skb);

	pr_debug("pipe created=%d\n", pipe);

	return pipe;
}

static int nfc_hci_delete_pipe(struct nfc_hci_dev *hdev, u8 pipe)
{
	pr_debug("\n");

	return nfc_hci_execute_cmd(hdev, NFC_HCI_ADMIN_PIPE,
				   NFC_HCI_ADM_DELETE_PIPE, &pipe, 1, NULL);
}

static int nfc_hci_clear_all_pipes(struct nfc_hci_dev *hdev)
{
	u8 param[2];

	/* TODO: Find out what the identity reference data is
	 * and fill param with it. HCI spec 6.1.3.5 */

	pr_debug("\n");

	return nfc_hci_execute_cmd(hdev, NFC_HCI_ADMIN_PIPE,
				   NFC_HCI_ADM_CLEAR_ALL_PIPE, param, 2, NULL);
}

int nfc_hci_disconnect_gate(struct nfc_hci_dev *hdev, u8 gate)
{
	int r;
	u8 pipe = hdev->gate2pipe[gate];

	pr_debug("\n");

	if (pipe == NFC_HCI_INVALID_PIPE)
		return -EADDRNOTAVAIL;

	r = nfc_hci_close_pipe(hdev, pipe);
	if (r < 0)
		return r;

	if (pipe != NFC_HCI_LINK_MGMT_PIPE && pipe != NFC_HCI_ADMIN_PIPE) {
		r = nfc_hci_delete_pipe(hdev, pipe);
		if (r < 0)
			return r;
	}

	hdev->gate2pipe[gate] = NFC_HCI_INVALID_PIPE;

	return 0;
}
EXPORT_SYMBOL(nfc_hci_disconnect_gate);

int nfc_hci_disconnect_all_gates(struct nfc_hci_dev *hdev)
{
	int r;

	pr_debug("\n");

	r = nfc_hci_clear_all_pipes(hdev);
	if (r < 0)
		return r;

	memset(hdev->gate2pipe, NFC_HCI_INVALID_PIPE, sizeof(hdev->gate2pipe));

	return 0;
}
EXPORT_SYMBOL(nfc_hci_disconnect_all_gates);

int nfc_hci_connect_gate(struct nfc_hci_dev *hdev, u8 dest_host, u8 dest_gate,
			 u8 pipe)
{
	bool pipe_created = false;
	int r;

	pr_debug("\n");

	if (hdev->gate2pipe[dest_gate] != NFC_HCI_INVALID_PIPE)
		return -EADDRINUSE;

	if (pipe != NFC_HCI_INVALID_PIPE)
		goto pipe_is_open;

	switch (dest_gate) {
	case NFC_HCI_LINK_MGMT_GATE:
		pipe = NFC_HCI_LINK_MGMT_PIPE;
		break;
	case NFC_HCI_ADMIN_GATE:
		pipe = NFC_HCI_ADMIN_PIPE;
		break;
	default:
		pipe = nfc_hci_create_pipe(hdev, dest_host, dest_gate, &r);
		if (pipe == NFC_HCI_INVALID_PIPE)
			return r;
		pipe_created = true;
		break;
	}

	r = nfc_hci_open_pipe(hdev, pipe);
	if (r < 0) {
		if (pipe_created)
			if (nfc_hci_delete_pipe(hdev, pipe) < 0) {
				/* TODO: Cannot clean by deleting pipe...
				 * -> inconsistent state */
			}
		return r;
	}

pipe_is_open:
	hdev->gate2pipe[dest_gate] = pipe;

	return 0;
}
EXPORT_SYMBOL(nfc_hci_connect_gate);
