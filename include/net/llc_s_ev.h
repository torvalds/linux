#ifndef LLC_S_EV_H
#define LLC_S_EV_H
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

#include <linux/skbuff.h>

/* Defines SAP component events */
/* Types of events (possible values in 'ev->type') */
#define LLC_SAP_EV_TYPE_SIMPLE		1
#define LLC_SAP_EV_TYPE_CONDITION	2
#define LLC_SAP_EV_TYPE_PRIM		3
#define LLC_SAP_EV_TYPE_PDU		4   /* command/response PDU */
#define LLC_SAP_EV_TYPE_ACK_TMR		5
#define LLC_SAP_EV_TYPE_RPT_STATUS	6

#define LLC_SAP_EV_ACTIVATION_REQ	 1
#define LLC_SAP_EV_RX_UI		 2
#define LLC_SAP_EV_UNITDATA_REQ		 3
#define LLC_SAP_EV_XID_REQ		 4
#define LLC_SAP_EV_RX_XID_C		 5
#define LLC_SAP_EV_RX_XID_R		 6
#define LLC_SAP_EV_TEST_REQ		 7
#define LLC_SAP_EV_RX_TEST_C		 8
#define LLC_SAP_EV_RX_TEST_R		 9
#define LLC_SAP_EV_DEACTIVATION_REQ	10

struct llc_sap_state_ev {
	u8		prim;
	u8		prim_type;
	u8		type;
	u8		reason;
	u8		ind_cfm_flag;
	struct llc_addr saddr;
	struct llc_addr daddr;
};

static __inline__ struct llc_sap_state_ev *llc_sap_ev(struct sk_buff *skb)
{
	return (struct llc_sap_state_ev *)skb->cb;
}

struct llc_sap;

typedef int (*llc_sap_ev_t)(struct llc_sap *sap, struct sk_buff *skb);

int llc_sap_ev_activation_req(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_rx_ui(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_unitdata_req(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_xid_req(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_rx_xid_c(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_rx_xid_r(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_test_req(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_rx_test_c(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_rx_test_r(struct llc_sap *sap, struct sk_buff *skb);
int llc_sap_ev_deactivation_req(struct llc_sap *sap, struct sk_buff *skb);
#endif /* LLC_S_EV_H */
