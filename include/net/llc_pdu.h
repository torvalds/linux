/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LLC_PDU_H
#define LLC_PDU_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/skbuff.h>

/* Command/response PDU indicator in SSAP field */
#define LLC_PDU_CMD		0
#define LLC_PDU_RSP		1

/* Get PDU type from 2 lowest-order bits of control field first byte */
#define LLC_PDU_TYPE_MASK      0x03
#define LLC_PDU_TYPE_U		3	/* first two bits */

#define LLC_1_PDU_CMD_UI       0x00	/* Type 1 cmds/rsps */

/* Un-numbered PDU format (3 bytes in length) */
struct llc_pdu_un {
	u8 dsap;
	u8 ssap;
	u8 ctrl_1;
} __packed;

static inline struct llc_pdu_un *llc_pdu_un_hdr(struct sk_buff *skb)
{
	return (struct llc_pdu_un *)skb_network_header(skb);
}

/**
 *	llc_pdu_header_init - initializes pdu header
 *	@skb: input skb that header must be set into it.
 *	@ssap: source sap.
 *	@dsap: destination sap.
 *	@cr: command/response bit (%LLC_PDU_CMD or %LLC_PDU_RSP).
 *
 *	This function sets DSAP, SSAP and command/Response bit in LLC header.
 */
static inline void llc_pdu_header_init(struct sk_buff *skb, u8 ssap, u8 dsap,
				       u8 cr)
{
	struct llc_pdu_un *pdu;

	skb_push(skb, sizeof(*pdu));
	skb_reset_network_header(skb);
	pdu = llc_pdu_un_hdr(skb);
	pdu->dsap = dsap;
	pdu->ssap = ssap;
	pdu->ssap |= cr;
}

/**
 *	llc_pdu_init_as_ui_cmd - sets LLC header as UI PDU
 *	@skb: input skb that header must be set into it.
 *
 *	This function sets third byte of LLC header as a UI PDU.
 */
static inline void llc_pdu_init_as_ui_cmd(struct sk_buff *skb)
{
	struct llc_pdu_un *pdu = llc_pdu_un_hdr(skb);

	pdu->ctrl_1  = LLC_PDU_TYPE_U;
	pdu->ctrl_1 |= LLC_1_PDU_CMD_UI;
}

#endif /* LLC_PDU_H */
