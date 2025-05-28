/*
 * llc_s_ac.c - actions performed during sap state transition.
 *
 * Description :
 *   Functions in this module are implementation of sap component actions.
 *   Details of actions can be found in IEEE-802.2 standard document.
 *   All functions have one sap and one event as input argument. All of
 *   them return 0 On success and 1 otherwise.
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 *		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <linux/netdevice.h>
#include <net/llc.h>
#include <net/llc_pdu.h>
#include <net/llc_s_ac.h>
#include <net/llc_s_ev.h>
#include <net/llc_sap.h>
#include <net/sock.h>

/**
 *	llc_sap_action_unitdata_ind - forward UI PDU to network layer
 *	@sap: SAP
 *	@skb: the event to forward
 *
 *	Received a UI PDU from MAC layer; forward to network layer as a
 *	UNITDATA INDICATION; verify our event is the kind we expect
 */
int llc_sap_action_unitdata_ind(struct llc_sap *sap, struct sk_buff *skb)
{
	llc_sap_rtn_pdu(sap, skb);
	return 0;
}

static int llc_prepare_and_xmit(struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);
	struct sk_buff *nskb;
	int rc;

	rc = llc_mac_hdr_init(skb, ev->saddr.mac, ev->daddr.mac);
	if (rc)
		return rc;

	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb)
		return -ENOMEM;

	if (skb->sk)
		skb_set_owner_w(nskb, skb->sk);

	return dev_queue_xmit(nskb);
}

/**
 *	llc_sap_action_send_ui - sends UI PDU resp to UNITDATA REQ to MAC layer
 *	@sap: SAP
 *	@skb: the event to send
 *
 *	Sends a UI PDU to the MAC layer in response to a UNITDATA REQUEST
 *	primitive from the network layer. Verifies event is a primitive type of
 *	event. Verify the primitive is a UNITDATA REQUEST.
 */
int llc_sap_action_send_ui(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	llc_pdu_header_init(skb, LLC_PDU_TYPE_U, ev->saddr.lsap,
			    ev->daddr.lsap, LLC_PDU_CMD);
	llc_pdu_init_as_ui_cmd(skb);

	return llc_prepare_and_xmit(skb);
}

/**
 *	llc_sap_action_send_xid_c - send XID PDU as response to XID REQ
 *	@sap: SAP
 *	@skb: the event to send
 *
 *	Send a XID command PDU to MAC layer in response to a XID REQUEST
 *	primitive from the network layer. Verify event is a primitive type
 *	event. Verify the primitive is a XID REQUEST.
 */
int llc_sap_action_send_xid_c(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	llc_pdu_header_init(skb, LLC_PDU_TYPE_U_XID, ev->saddr.lsap,
			    ev->daddr.lsap, LLC_PDU_CMD);
	llc_pdu_init_as_xid_cmd(skb, LLC_XID_NULL_CLASS_2, 0);

	return llc_prepare_and_xmit(skb);
}

/**
 *	llc_sap_action_send_xid_r - send XID PDU resp to MAC for received XID
 *	@sap: SAP
 *	@skb: the event to send
 *
 *	Send XID response PDU to MAC in response to an earlier received XID
 *	command PDU. Verify event is a PDU type event
 */
int llc_sap_action_send_xid_r(struct llc_sap *sap, struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], mac_sa[ETH_ALEN], dsap;
	int rc = 1;
	struct sk_buff *nskb;

	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_da(skb, mac_sa);
	llc_pdu_decode_ssap(skb, &dsap);
	nskb = llc_alloc_frame(NULL, skb->dev, LLC_PDU_TYPE_U,
			       sizeof(struct llc_xid_info));
	if (!nskb)
		goto out;
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap, dsap,
			    LLC_PDU_RSP);
	llc_pdu_init_as_xid_rsp(nskb, LLC_XID_NULL_CLASS_2, 0);
	rc = llc_mac_hdr_init(nskb, mac_sa, mac_da);
	if (likely(!rc))
		rc = dev_queue_xmit(nskb);
out:
	return rc;
}

/**
 *	llc_sap_action_send_test_c - send TEST PDU to MAC in resp to TEST REQ
 *	@sap: SAP
 *	@skb: the event to send
 *
 *	Send a TEST command PDU to the MAC layer in response to a TEST REQUEST
 *	primitive from the network layer. Verify event is a primitive type
 *	event; verify the primitive is a TEST REQUEST.
 */
int llc_sap_action_send_test_c(struct llc_sap *sap, struct sk_buff *skb)
{
	struct llc_sap_state_ev *ev = llc_sap_ev(skb);

	llc_pdu_header_init(skb, LLC_PDU_TYPE_U, ev->saddr.lsap,
			    ev->daddr.lsap, LLC_PDU_CMD);
	llc_pdu_init_as_test_cmd(skb);

	return llc_prepare_and_xmit(skb);
}

int llc_sap_action_send_test_r(struct llc_sap *sap, struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], mac_sa[ETH_ALEN], dsap;
	struct sk_buff *nskb;
	int rc = 1;
	u32 data_size;

	if (skb->mac_len < ETH_HLEN)
		return 1;

	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_da(skb, mac_sa);
	llc_pdu_decode_ssap(skb, &dsap);

	/* The test request command is type U (llc_len = 3) */
	data_size = ntohs(eth_hdr(skb)->h_proto) - 3;
	nskb = llc_alloc_frame(NULL, skb->dev, LLC_PDU_TYPE_U, data_size);
	if (!nskb)
		goto out;
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, sap->laddr.lsap, dsap,
			    LLC_PDU_RSP);
	llc_pdu_init_as_test_rsp(nskb, skb);
	rc = llc_mac_hdr_init(nskb, mac_sa, mac_da);
	if (likely(!rc))
		rc = dev_queue_xmit(nskb);
out:
	return rc;
}

/**
 *	llc_sap_action_report_status - report data link status to layer mgmt
 *	@sap: SAP
 *	@skb: the event to send
 *
 *	Report data link status to layer management. Verify our event is the
 *	kind we expect.
 */
int llc_sap_action_report_status(struct llc_sap *sap, struct sk_buff *skb)
{
	return 0;
}

/**
 *	llc_sap_action_xid_ind - send XID PDU resp to net layer via XID IND
 *	@sap: SAP
 *	@skb: the event to send
 *
 *	Send a XID response PDU to the network layer via a XID INDICATION
 *	primitive.
 */
int llc_sap_action_xid_ind(struct llc_sap *sap, struct sk_buff *skb)
{
	llc_sap_rtn_pdu(sap, skb);
	return 0;
}

/**
 *	llc_sap_action_test_ind - send TEST PDU to net layer via TEST IND
 *	@sap: SAP
 *	@skb: the event to send
 *
 *	Send a TEST response PDU to the network layer via a TEST INDICATION
 *	primitive. Verify our event is a PDU type event.
 */
int llc_sap_action_test_ind(struct llc_sap *sap, struct sk_buff *skb)
{
	llc_sap_rtn_pdu(sap, skb);
	return 0;
}
