/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the Token-Ring IEEE 802.5 interface.
 *
 * Version:	@(#)if_tr.h	0.0	07/11/94
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *		Peter De Schrijver, <stud11@cc4.kuleuven.ac.be>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_TR_H
#define _LINUX_IF_TR_H

#include <asm/byteorder.h>	/* For __be16 */

/* IEEE 802.5 Token-Ring magic constants.  The frame sizes omit the preamble
   and FCS/CRC (frame check sequence). */
#define TR_ALEN		6		/* Octets in one token-ring addr */
#define TR_HLEN 	(sizeof(struct trh_hdr)+sizeof(struct trllc))
#define AC		0x10
#define LLC_FRAME 	0x40

/* LLC and SNAP constants */
#define EXTENDED_SAP 	0xAA
#define UI_CMD       	0x03

/* This is an Token-Ring frame header. */
struct trh_hdr {
	__u8  ac;			/* access control field */
	__u8  fc;			/* frame control field */
	__u8  daddr[TR_ALEN];		/* destination address */
	__u8  saddr[TR_ALEN];		/* source address */
	__be16 rcf;			/* route control field */
	__be16 rseg[8];			/* routing registers */
};

#ifdef __KERNEL__
#include <linux/skbuff.h>

static inline struct trh_hdr *tr_hdr(const struct sk_buff *skb)
{
	return (struct trh_hdr *)skb_mac_header(skb);
}
#endif

/* This is an Token-Ring LLC structure */
struct trllc {
	__u8  dsap;			/* destination SAP */
	__u8  ssap;			/* source SAP */
	__u8  llc;			/* LLC control field */
	__u8  protid[3];		/* protocol id */
	__be16 ethertype;		/* ether type field */
};

/* Token-Ring statistics collection data. */
struct tr_statistics {
	unsigned long rx_packets;       /* total packets received	*/
	unsigned long tx_packets;	/* total packets transmitted	*/
	unsigned long rx_bytes;		/* total bytes received   	*/
	unsigned long tx_bytes;		/* total bytes transmitted	*/
	unsigned long rx_errors;	/* bad packets received		*/
	unsigned long tx_errors;	/* packet transmit problems	*/
	unsigned long rx_dropped;	/* no space in linux buffers	*/
	unsigned long tx_dropped;	/* no space available in linux	*/
	unsigned long multicast;	/* multicast packets received	*/
	unsigned long transmit_collision;

	/* detailed Token-Ring errors. See IBM Token-Ring Network
	   Architecture for more info */

	unsigned long line_errors;
	unsigned long internal_errors;
	unsigned long burst_errors;
	unsigned long A_C_errors;
	unsigned long abort_delimiters;
	unsigned long lost_frames;
	unsigned long recv_congest_count;
	unsigned long frame_copied_errors;
	unsigned long frequency_errors;
	unsigned long token_errors;
	unsigned long dummy1;
};

/* source routing stuff */
#define TR_RII 			0x80
#define TR_RCF_DIR_BIT 		0x80
#define TR_RCF_LEN_MASK 	0x1f00
#define TR_RCF_BROADCAST 	0x8000	/* all-routes broadcast */
#define TR_RCF_LIMITED_BROADCAST 0xC000	/* single-route broadcast */
#define TR_RCF_FRAME2K 		0x20
#define TR_RCF_BROADCAST_MASK 	0xC000
#define TR_MAXRIFLEN 		18

#endif	/* _LINUX_IF_TR_H */
