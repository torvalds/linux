/*
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
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

#ifndef __NET_HCI_H
#define __NET_HCI_H

#include <linux/skbuff.h>

#include <net/nfc/nfc.h>

struct nfc_hci_dev;

struct nfc_hci_ops {
	int (*open) (struct nfc_hci_dev *hdev);
	void (*close) (struct nfc_hci_dev *hdev);
	int (*hci_ready) (struct nfc_hci_dev *hdev);
	/*
	 * xmit must always send the complete buffer before
	 * returning. Returned result must be 0 for success
	 * or negative for failure.
	 */
	int (*xmit) (struct nfc_hci_dev *hdev, struct sk_buff *skb);
	int (*start_poll) (struct nfc_hci_dev *hdev,
			   u32 im_protocols, u32 tm_protocols);
	int (*target_from_gate) (struct nfc_hci_dev *hdev, u8 gate,
				 struct nfc_target *target);
	int (*complete_target_discovered) (struct nfc_hci_dev *hdev, u8 gate,
					   struct nfc_target *target);
	int (*data_exchange) (struct nfc_hci_dev *hdev,
			      struct nfc_target *target, struct sk_buff *skb,
			      data_exchange_cb_t cb, void *cb_context);
	int (*check_presence)(struct nfc_hci_dev *hdev,
			      struct nfc_target *target);
};

/* Pipes */
#define NFC_HCI_INVALID_PIPE	0x80
#define NFC_HCI_LINK_MGMT_PIPE	0x00
#define NFC_HCI_ADMIN_PIPE	0x01

struct nfc_hci_gate {
	u8 gate;
	u8 pipe;
};

#define NFC_HCI_MAX_CUSTOM_GATES	50
struct nfc_hci_init_data {
	u8 gate_count;
	struct nfc_hci_gate gates[NFC_HCI_MAX_CUSTOM_GATES];
	char session_id[9];
};

typedef int (*xmit) (struct sk_buff *skb, void *cb_data);

#define NFC_HCI_MAX_GATES		256

struct nfc_hci_dev {
	struct nfc_dev *ndev;

	u32 max_data_link_payload;

	struct mutex msg_tx_mutex;

	struct list_head msg_tx_queue;

	struct work_struct msg_tx_work;

	struct timer_list cmd_timer;
	struct hci_msg *cmd_pending_msg;

	struct sk_buff_head rx_hcp_frags;

	struct work_struct msg_rx_work;

	struct sk_buff_head msg_rx_queue;

	struct nfc_hci_ops *ops;

	struct nfc_llc *llc;

	struct nfc_hci_init_data init_data;

	void *clientdata;

	u8 gate2pipe[NFC_HCI_MAX_GATES];

	u8 sw_romlib;
	u8 sw_patch;
	u8 sw_flashlib_major;
	u8 sw_flashlib_minor;

	u8 hw_derivative;
	u8 hw_version;
	u8 hw_mpw;
	u8 hw_software;
	u8 hw_bsid;

	int async_cb_type;
	data_exchange_cb_t async_cb;
	void *async_cb_context;
};

/* hci device allocation */
struct nfc_hci_dev *nfc_hci_allocate_device(struct nfc_hci_ops *ops,
					    struct nfc_hci_init_data *init_data,
					    u32 protocols,
					    const char *llc_name,
					    int tx_headroom,
					    int tx_tailroom,
					    int max_link_payload);
void nfc_hci_free_device(struct nfc_hci_dev *hdev);

int nfc_hci_register_device(struct nfc_hci_dev *hdev);
void nfc_hci_unregister_device(struct nfc_hci_dev *hdev);

void nfc_hci_set_clientdata(struct nfc_hci_dev *hdev, void *clientdata);
void *nfc_hci_get_clientdata(struct nfc_hci_dev *hdev);

void nfc_hci_driver_failure(struct nfc_hci_dev *hdev, int err);

/* Host IDs */
#define NFC_HCI_HOST_CONTROLLER_ID	0x00
#define NFC_HCI_TERMINAL_HOST_ID	0x01
#define NFC_HCI_UICC_HOST_ID		0x02

/* Host Controller Gates and registry indexes */
#define NFC_HCI_ADMIN_GATE 0x00
#define NFC_HCI_ADMIN_SESSION_IDENTITY	0x01
#define NFC_HCI_ADMIN_MAX_PIPE		0x02
#define NFC_HCI_ADMIN_WHITELIST		0x03
#define NFC_HCI_ADMIN_HOST_LIST		0x04

#define NFC_HCI_LOOPBACK_GATE		0x04

#define NFC_HCI_ID_MGMT_GATE		0x05
#define NFC_HCI_ID_MGMT_VERSION_SW	0x01
#define NFC_HCI_ID_MGMT_VERSION_HW	0x03
#define NFC_HCI_ID_MGMT_VENDOR_NAME	0x04
#define NFC_HCI_ID_MGMT_MODEL_ID	0x05
#define NFC_HCI_ID_MGMT_HCI_VERSION	0x02
#define NFC_HCI_ID_MGMT_GATES_LIST	0x06

#define NFC_HCI_LINK_MGMT_GATE		0x06
#define NFC_HCI_LINK_MGMT_REC_ERROR	0x01

#define NFC_HCI_RF_READER_B_GATE			0x11
#define NFC_HCI_RF_READER_B_PUPI			0x03
#define NFC_HCI_RF_READER_B_APPLICATION_DATA		0x04
#define NFC_HCI_RF_READER_B_AFI				0x02
#define NFC_HCI_RF_READER_B_HIGHER_LAYER_RESPONSE	0x01
#define NFC_HCI_RF_READER_B_HIGHER_LAYER_DATA		0x05

#define NFC_HCI_RF_READER_A_GATE		0x13
#define NFC_HCI_RF_READER_A_UID			0x02
#define NFC_HCI_RF_READER_A_ATQA		0x04
#define NFC_HCI_RF_READER_A_APPLICATION_DATA	0x05
#define NFC_HCI_RF_READER_A_SAK			0x03
#define NFC_HCI_RF_READER_A_FWI_SFGT		0x06
#define NFC_HCI_RF_READER_A_DATARATE_MAX	0x01

#define NFC_HCI_TYPE_A_SEL_PROT(x)		(((x) & 0x60) >> 5)
#define NFC_HCI_TYPE_A_SEL_PROT_MIFARE		0
#define NFC_HCI_TYPE_A_SEL_PROT_ISO14443	1
#define NFC_HCI_TYPE_A_SEL_PROT_DEP		2
#define NFC_HCI_TYPE_A_SEL_PROT_ISO14443_DEP	3

/* Generic events */
#define NFC_HCI_EVT_HCI_END_OF_OPERATION	0x01
#define NFC_HCI_EVT_POST_DATA			0x02
#define NFC_HCI_EVT_HOT_PLUG			0x03

/* Reader RF gates events */
#define NFC_HCI_EVT_READER_REQUESTED	0x10
#define NFC_HCI_EVT_END_OPERATION	0x11

/* Reader Application gate events */
#define NFC_HCI_EVT_TARGET_DISCOVERED	0x10

/* receiving messages from lower layer */
void nfc_hci_resp_received(struct nfc_hci_dev *hdev, u8 result,
			   struct sk_buff *skb);
void nfc_hci_cmd_received(struct nfc_hci_dev *hdev, u8 pipe, u8 cmd,
			  struct sk_buff *skb);
void nfc_hci_event_received(struct nfc_hci_dev *hdev, u8 pipe, u8 event,
			    struct sk_buff *skb);
void nfc_hci_recv_frame(struct nfc_hci_dev *hdev, struct sk_buff *skb);

/* connecting to gates and sending hci instructions */
int nfc_hci_connect_gate(struct nfc_hci_dev *hdev, u8 dest_host, u8 dest_gate,
			 u8 pipe);
int nfc_hci_disconnect_gate(struct nfc_hci_dev *hdev, u8 gate);
int nfc_hci_disconnect_all_gates(struct nfc_hci_dev *hdev);
int nfc_hci_get_param(struct nfc_hci_dev *hdev, u8 gate, u8 idx,
		      struct sk_buff **skb);
int nfc_hci_set_param(struct nfc_hci_dev *hdev, u8 gate, u8 idx,
		      const u8 *param, size_t param_len);
int nfc_hci_send_cmd(struct nfc_hci_dev *hdev, u8 gate, u8 cmd,
		     const u8 *param, size_t param_len, struct sk_buff **skb);
int nfc_hci_send_cmd_async(struct nfc_hci_dev *hdev, u8 gate, u8 cmd,
			   const u8 *param, size_t param_len,
			   data_exchange_cb_t cb, void *cb_context);
int nfc_hci_send_response(struct nfc_hci_dev *hdev, u8 gate, u8 response,
			  const u8 *param, size_t param_len);
int nfc_hci_send_event(struct nfc_hci_dev *hdev, u8 gate, u8 event,
		       const u8 *param, size_t param_len);

#endif /* __NET_HCI_H */
