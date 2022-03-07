/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PTP 1588 support
 *
 * This file implements a BPF that recognizes PTP event messages.
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */

#ifndef _PTP_CLASSIFY_H_
#define _PTP_CLASSIFY_H_

#include <linux/ip.h>
#include <linux/skbuff.h>

#define PTP_CLASS_NONE  0x00 /* not a PTP event message */
#define PTP_CLASS_V1    0x01 /* protocol version 1 */
#define PTP_CLASS_V2    0x02 /* protocol version 2 */
#define PTP_CLASS_VMASK 0x0f /* max protocol version is 15 */
#define PTP_CLASS_IPV4  0x10 /* event in an IPV4 UDP packet */
#define PTP_CLASS_IPV6  0x20 /* event in an IPV6 UDP packet */
#define PTP_CLASS_L2    0x40 /* event in a L2 packet */
#define PTP_CLASS_PMASK	0x70 /* mask for the packet type field */
#define PTP_CLASS_VLAN	0x80 /* event in a VLAN tagged packet */

#define PTP_CLASS_V1_IPV4 (PTP_CLASS_V1 | PTP_CLASS_IPV4)
#define PTP_CLASS_V1_IPV6 (PTP_CLASS_V1 | PTP_CLASS_IPV6) /* probably DNE */
#define PTP_CLASS_V2_IPV4 (PTP_CLASS_V2 | PTP_CLASS_IPV4)
#define PTP_CLASS_V2_IPV6 (PTP_CLASS_V2 | PTP_CLASS_IPV6)
#define PTP_CLASS_V2_L2   (PTP_CLASS_V2 | PTP_CLASS_L2)
#define PTP_CLASS_V2_VLAN (PTP_CLASS_V2 | PTP_CLASS_VLAN)
#define PTP_CLASS_L4      (PTP_CLASS_IPV4 | PTP_CLASS_IPV6)

#define PTP_MSGTYPE_SYNC        0x0
#define PTP_MSGTYPE_DELAY_REQ   0x1
#define PTP_MSGTYPE_PDELAY_REQ  0x2
#define PTP_MSGTYPE_PDELAY_RESP 0x3

#define PTP_EV_PORT 319
#define PTP_GEN_PORT 320
#define PTP_GEN_BIT 0x08 /* indicates general message, if set in message type */

#define OFF_PTP_SOURCE_UUID	22 /* PTPv1 only */
#define OFF_PTP_SEQUENCE_ID	30

/* Below defines should actually be removed at some point in time. */
#define IP6_HLEN	40
#define UDP_HLEN	8
#define OFF_IHL		14
#define IPV4_HLEN(data) (((struct iphdr *)(data + OFF_IHL))->ihl << 2)

struct clock_identity {
	u8 id[8];
} __packed;

struct port_identity {
	struct clock_identity	clock_identity;
	__be16			port_number;
} __packed;

struct ptp_header {
	u8			tsmt;  /* transportSpecific | messageType */
	u8			ver;   /* reserved          | versionPTP  */
	__be16			message_length;
	u8			domain_number;
	u8			reserved1;
	u8			flag_field[2];
	__be64			correction;
	__be32			reserved2;
	struct port_identity	source_port_identity;
	__be16			sequence_id;
	u8			control;
	u8			log_message_interval;
} __packed;

#if defined(CONFIG_NET_PTP_CLASSIFY)
/**
 * ptp_classify_raw - classify a PTP packet
 * @skb: buffer
 *
 * Runs a minimal BPF dissector to classify a network packet to
 * determine the PTP class. In case the skb does not contain any
 * PTP protocol data, PTP_CLASS_NONE will be returned, otherwise
 * PTP_CLASS_V1_IPV{4,6}, PTP_CLASS_V2_IPV{4,6} or
 * PTP_CLASS_V2_{L2,VLAN}, depending on the packet content.
 */
unsigned int ptp_classify_raw(const struct sk_buff *skb);

/**
 * ptp_parse_header - Get pointer to the PTP v2 header
 * @skb: packet buffer
 * @type: type of the packet (see ptp_classify_raw())
 *
 * This function takes care of the VLAN, UDP, IPv4 and IPv6 headers. The length
 * is checked.
 *
 * Note, internally skb_mac_header() is used. Make sure that the @skb is
 * initialized accordingly.
 *
 * Return: Pointer to the ptp v2 header or NULL if not found
 */
struct ptp_header *ptp_parse_header(struct sk_buff *skb, unsigned int type);

/**
 * ptp_get_msgtype - Extract ptp message type from given header
 * @hdr: ptp header
 * @type: type of the packet (see ptp_classify_raw())
 *
 * This function returns the message type for a given ptp header. It takes care
 * of the different ptp header versions (v1 or v2).
 *
 * Return: The message type
 */
static inline u8 ptp_get_msgtype(const struct ptp_header *hdr,
				 unsigned int type)
{
	u8 msgtype;

	if (unlikely(type & PTP_CLASS_V1)) {
		/* msg type is located at the control field for ptp v1 */
		msgtype = hdr->control;
	} else {
		msgtype = hdr->tsmt & 0x0f;
	}

	return msgtype;
}

/**
 * ptp_msg_is_sync - Evaluates whether the given skb is a PTP Sync message
 * @skb: packet buffer
 * @type: type of the packet (see ptp_classify_raw())
 *
 * This function evaluates whether the given skb is a PTP Sync message.
 *
 * Return: true if sync message, false otherwise
 */
bool ptp_msg_is_sync(struct sk_buff *skb, unsigned int type);

void __init ptp_classifier_init(void);
#else
static inline void ptp_classifier_init(void)
{
}
static inline unsigned int ptp_classify_raw(struct sk_buff *skb)
{
	return PTP_CLASS_NONE;
}
static inline struct ptp_header *ptp_parse_header(struct sk_buff *skb,
						  unsigned int type)
{
	return NULL;
}
static inline u8 ptp_get_msgtype(const struct ptp_header *hdr,
				 unsigned int type)
{
	/* The return is meaningless. The stub function would not be
	 * executed since no available header from ptp_parse_header.
	 */
	return PTP_MSGTYPE_SYNC;
}
static inline bool ptp_msg_is_sync(struct sk_buff *skb, unsigned int type)
{
	return false;
}
#endif
#endif /* _PTP_CLASSIFY_H_ */
