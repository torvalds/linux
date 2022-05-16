/*
 * llc_station.c - station component of LLC
 *
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <net/llc.h>
#include <net/llc_sap.h>
#include <net/llc_conn.h>
#include <net/llc_c_ac.h>
#include <net/llc_s_ac.h>
#include <net/llc_c_ev.h>
#include <net/llc_c_st.h>
#include <net/llc_s_ev.h>
#include <net/llc_s_st.h>
#include <net/llc_pdu.h>

static int llc_stat_ev_rx_null_dsap_xid_c(struct sk_buff *skb)
{
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) &&			/* command PDU */
	       LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_CMD(pdu) == LLC_1_PDU_CMD_XID &&
	       !pdu->dsap;				/* NULL DSAP value */
}

static int llc_stat_ev_rx_null_dsap_test_c(struct sk_buff *skb)
{
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	return LLC_PDU_IS_CMD(pdu) &&			/* command PDU */
	       LLC_PDU_TYPE_IS_U(pdu) &&		/* U type PDU */
	       LLC_U_PDU_CMD(pdu) == LLC_1_PDU_CMD_TEST &&
	       !pdu->dsap;				/* NULL DSAP */
}

static int llc_station_ac_send_xid_r(struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], dsap;
	int rc = 1;
	struct sk_buff *nskb = llc_alloc_frame(NULL, skb->dev, LLC_PDU_TYPE_U,
					       sizeof(struct llc_xid_info));

	if (!nskb)
		goto out;
	rc = 0;
	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_ssap(skb, &dsap);
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, dsap, LLC_PDU_RSP);
	llc_pdu_init_as_xid_rsp(nskb, LLC_XID_NULL_CLASS_2, 127);
	rc = llc_mac_hdr_init(nskb, skb->dev->dev_addr, mac_da);
	if (unlikely(rc))
		goto free;
	dev_queue_xmit(nskb);
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

static int llc_station_ac_send_test_r(struct sk_buff *skb)
{
	u8 mac_da[ETH_ALEN], dsap;
	int rc = 1;
	u32 data_size;
	struct sk_buff *nskb;

	/* The test request command is type U (llc_len = 3) */
	data_size = ntohs(eth_hdr(skb)->h_proto) - 3;
	nskb = llc_alloc_frame(NULL, skb->dev, LLC_PDU_TYPE_U, data_size);

	if (!nskb)
		goto out;
	rc = 0;
	llc_pdu_decode_sa(skb, mac_da);
	llc_pdu_decode_ssap(skb, &dsap);
	llc_pdu_header_init(nskb, LLC_PDU_TYPE_U, 0, dsap, LLC_PDU_RSP);
	llc_pdu_init_as_test_rsp(nskb, skb);
	rc = llc_mac_hdr_init(nskb, skb->dev->dev_addr, mac_da);
	if (unlikely(rc))
		goto free;
	dev_queue_xmit(nskb);
out:
	return rc;
free:
	kfree_skb(nskb);
	goto out;
}

/**
 *	llc_station_rcv - send received pdu to the station state machine
 *	@skb: received frame.
 *
 *	Sends data unit to station state machine.
 */
static void llc_station_rcv(struct sk_buff *skb)
{
	if (llc_stat_ev_rx_null_dsap_xid_c(skb))
		llc_station_ac_send_xid_r(skb);
	else if (llc_stat_ev_rx_null_dsap_test_c(skb))
		llc_station_ac_send_test_r(skb);
	kfree_skb(skb);
}

void __init llc_station_init(void)
{
	llc_set_station_handler(llc_station_rcv);
}

void llc_station_exit(void)
{
	llc_set_station_handler(NULL);
}
