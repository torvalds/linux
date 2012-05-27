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

#ifndef __NFC_SHDLC_H
#define __NFC_SHDLC_H

struct nfc_shdlc;

struct nfc_shdlc_ops {
	int (*open) (struct nfc_shdlc *shdlc);
	void (*close) (struct nfc_shdlc *shdlc);
	int (*hci_ready) (struct nfc_shdlc *shdlc);
	int (*xmit) (struct nfc_shdlc *shdlc, struct sk_buff *skb);
	int (*start_poll) (struct nfc_shdlc *shdlc, u32 protocols);
	int (*target_from_gate) (struct nfc_shdlc *shdlc, u8 gate,
				 struct nfc_target *target);
	int (*complete_target_discovered) (struct nfc_shdlc *shdlc, u8 gate,
					   struct nfc_target *target);
	int (*data_exchange) (struct nfc_shdlc *shdlc,
			      struct nfc_target *target,
			      struct sk_buff *skb, struct sk_buff **res_skb);
	int (*check_presence)(struct nfc_shdlc *shdlc,
			      struct nfc_target *target);
};

enum shdlc_state {
	SHDLC_DISCONNECTED = 0,
	SHDLC_CONNECTING = 1,
	SHDLC_NEGOCIATING = 2,
	SHDLC_CONNECTED = 3
};

struct nfc_shdlc {
	struct mutex state_mutex;
	enum shdlc_state state;
	int hard_fault;

	struct nfc_hci_dev *hdev;

	wait_queue_head_t *connect_wq;
	int connect_tries;
	int connect_result;
	struct timer_list connect_timer;/* aka T3 in spec 10.6.1 */

	u8 w;				/* window size */
	bool srej_support;

	struct timer_list t1_timer;	/* send ack timeout */
	bool t1_active;

	struct timer_list t2_timer;	/* guard/retransmit timeout */
	bool t2_active;

	int ns;				/* next seq num for send */
	int nr;				/* next expected seq num for receive */
	int dnr;			/* oldest sent unacked seq num */

	struct sk_buff_head rcv_q;

	struct sk_buff_head send_q;
	bool rnr;			/* other side is not ready to receive */

	struct sk_buff_head ack_pending_q;

	struct workqueue_struct *sm_wq;
	struct work_struct sm_work;

	struct nfc_shdlc_ops *ops;

	int client_headroom;
	int client_tailroom;

	void *clientdata;
};

void nfc_shdlc_recv_frame(struct nfc_shdlc *shdlc, struct sk_buff *skb);

struct nfc_shdlc *nfc_shdlc_allocate(struct nfc_shdlc_ops *ops,
				     struct nfc_hci_init_data *init_data,
				     u32 protocols,
				     int tx_headroom, int tx_tailroom,
				     int max_link_payload, const char *devname);

void nfc_shdlc_free(struct nfc_shdlc *shdlc);

void nfc_shdlc_set_clientdata(struct nfc_shdlc *shdlc, void *clientdata);
void *nfc_shdlc_get_clientdata(struct nfc_shdlc *shdlc);
struct nfc_hci_dev *nfc_shdlc_get_hci_dev(struct nfc_shdlc *shdlc);

#endif /* __NFC_SHDLC_H */
