#ifndef LLC_IF_H
#define LLC_IF_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
/* Defines LLC interface to network layer */
/* Available primitives */
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/llc.h>
#include <linux/etherdevice.h>
#include <net/llc.h>

#define LLC_DATAUNIT_PRIM	1
#define LLC_CONN_PRIM		2
#define LLC_DATA_PRIM		3
#define LLC_DISC_PRIM		4
#define LLC_RESET_PRIM		5
#define LLC_FLOWCONTROL_PRIM	6 /* Not supported at this time */
#define LLC_DISABLE_PRIM	7
#define LLC_XID_PRIM		8
#define LLC_TEST_PRIM		9
#define LLC_SAP_ACTIVATION     10
#define LLC_SAP_DEACTIVATION   11

#define LLC_NBR_PRIMITIVES     11

#define LLC_IND			1
#define LLC_CONFIRM		2

/* Primitive type */
#define LLC_PRIM_TYPE_REQ	1
#define LLC_PRIM_TYPE_IND	2
#define LLC_PRIM_TYPE_RESP	3
#define LLC_PRIM_TYPE_CONFIRM	4

/* Reset reasons, remote entity or local LLC */
#define LLC_RESET_REASON_REMOTE	1
#define LLC_RESET_REASON_LOCAL	2

/* Disconnect reasons */
#define LLC_DISC_REASON_RX_DM_RSP_PDU	0
#define LLC_DISC_REASON_RX_DISC_CMD_PDU	1
#define LLC_DISC_REASON_ACK_TMR_EXP	2

/* Confirm reasons */
#define LLC_STATUS_CONN		0 /* connect confirm & reset confirm */
#define LLC_STATUS_DISC		1 /* connect confirm & reset confirm */
#define LLC_STATUS_FAILED	2 /* connect confirm & reset confirm */
#define LLC_STATUS_IMPOSSIBLE	3 /* connect confirm */
#define LLC_STATUS_RECEIVED	4 /* data conn */
#define LLC_STATUS_REMOTE_BUSY	5 /* data conn */
#define LLC_STATUS_REFUSE	6 /* data conn */
#define LLC_STATUS_CONFLICT	7 /* disconnect conn */
#define LLC_STATUS_RESET_DONE	8 /*  */

extern int llc_establish_connection(struct sock *sk, u8 *lmac,
				    u8 *dmac, u8 dsap);
extern int llc_build_and_send_pkt(struct sock *sk, struct sk_buff *skb);
extern int llc_send_disc(struct sock *sk);
#endif /* LLC_IF_H */
