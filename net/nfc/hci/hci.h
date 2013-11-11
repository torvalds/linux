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

#ifndef __LOCAL_HCI_H
#define __LOCAL_HCI_H

#include <net/nfc/hci.h>

struct gate_pipe_map {
	u8 gate;
	u8 pipe;
};

struct hcp_message {
	u8 header;		/* type -cmd,evt,rsp- + instruction */
	u8 data[];
} __packed;

struct hcp_packet {
	u8 header;		/* cbit+pipe */
	struct hcp_message message;
} __packed;

struct hcp_exec_waiter {
	wait_queue_head_t *wq;
	bool exec_complete;
	int exec_result;
	struct sk_buff *result_skb;
};

struct hci_msg {
	struct list_head msg_l;
	struct sk_buff_head msg_frags;
	bool wait_response;
	data_exchange_cb_t cb;
	void *cb_context;
	unsigned long completion_delay;
};

struct hci_create_pipe_params {
	u8 src_gate;
	u8 dest_host;
	u8 dest_gate;
} __packed;

struct hci_create_pipe_resp {
	u8 src_host;
	u8 src_gate;
	u8 dest_host;
	u8 dest_gate;
	u8 pipe;
} __packed;

#define NFC_HCI_FRAGMENT	0x7f

#define HCP_HEADER(type, instr) ((((type) & 0x03) << 6) | ((instr) & 0x3f))
#define HCP_MSG_GET_TYPE(header) ((header & 0xc0) >> 6)
#define HCP_MSG_GET_CMD(header) (header & 0x3f)

int nfc_hci_hcp_message_tx(struct nfc_hci_dev *hdev, u8 pipe,
			   u8 type, u8 instruction,
			   const u8 *payload, size_t payload_len,
			   data_exchange_cb_t cb, void *cb_context,
			   unsigned long completion_delay);

u8 nfc_hci_pipe2gate(struct nfc_hci_dev *hdev, u8 pipe);

void nfc_hci_hcp_message_rx(struct nfc_hci_dev *hdev, u8 pipe, u8 type,
			    u8 instruction, struct sk_buff *skb);

/* HCP headers */
#define NFC_HCI_HCP_PACKET_HEADER_LEN	1
#define NFC_HCI_HCP_MESSAGE_HEADER_LEN	1
#define NFC_HCI_HCP_HEADER_LEN		2

/* HCP types */
#define NFC_HCI_HCP_COMMAND	0x00
#define NFC_HCI_HCP_EVENT	0x01
#define NFC_HCI_HCP_RESPONSE	0x02

/* Generic commands */
#define NFC_HCI_ANY_SET_PARAMETER	0x01
#define NFC_HCI_ANY_GET_PARAMETER	0x02
#define NFC_HCI_ANY_OPEN_PIPE		0x03
#define NFC_HCI_ANY_CLOSE_PIPE		0x04

/* Reader RF commands */
#define NFC_HCI_WR_XCHG_DATA		0x10

/* Admin commands */
#define NFC_HCI_ADM_CREATE_PIPE			0x10
#define NFC_HCI_ADM_DELETE_PIPE			0x11
#define NFC_HCI_ADM_NOTIFY_PIPE_CREATED		0x12
#define NFC_HCI_ADM_NOTIFY_PIPE_DELETED		0x13
#define NFC_HCI_ADM_CLEAR_ALL_PIPE		0x14
#define NFC_HCI_ADM_NOTIFY_ALL_PIPE_CLEARED	0x15

/* Generic responses */
#define NFC_HCI_ANY_OK				0x00
#define NFC_HCI_ANY_E_NOT_CONNECTED		0x01
#define NFC_HCI_ANY_E_CMD_PAR_UNKNOWN		0x02
#define NFC_HCI_ANY_E_NOK			0x03
#define NFC_HCI_ANY_E_PIPES_FULL		0x04
#define NFC_HCI_ANY_E_REG_PAR_UNKNOWN		0x05
#define NFC_HCI_ANY_E_PIPE_NOT_OPENED		0x06
#define NFC_HCI_ANY_E_CMD_NOT_SUPPORTED		0x07
#define NFC_HCI_ANY_E_INHIBITED			0x08
#define NFC_HCI_ANY_E_TIMEOUT			0x09
#define NFC_HCI_ANY_E_REG_ACCESS_DENIED		0x0a
#define NFC_HCI_ANY_E_PIPE_ACCESS_DENIED	0x0b

#endif /* __LOCAL_HCI_H */
