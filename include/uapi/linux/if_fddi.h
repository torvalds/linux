/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the ANSI FDDI interface.
 *
 * Version:	@(#)if_fddi.h	1.0.2	Sep 29 2004
 *
 * Author:	Lawrence V. Stefani, <stefani@lkg.dec.com>
 *
 *		if_fddi.h is based on previous if_ether.h and if_tr.h work by
 *			Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *			Donald Becker, <becker@super.org>
 *			Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *			Steve Whitehouse, <gw7rrm@eeshack3.swan.ac.uk>
 *			Peter De Schrijver, <stud11@cc4.kuleuven.ac.be>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI_LINUX_IF_FDDI_H
#define _UAPI_LINUX_IF_FDDI_H

#include <linux/types.h>

/*
 *  Define max and min legal sizes.  The frame sizes do not include
 *  4 byte FCS/CRC (frame check sequence).
 */
#define FDDI_K_ALEN		6	/* Octets in one FDDI address */
#define FDDI_K_8022_HLEN	16	/* Total octets in 802.2 header */
#define FDDI_K_SNAP_HLEN	21	/* Total octets in 802.2 SNAP header */
#define FDDI_K_8022_ZLEN	16	/* Min octets in 802.2 frame sans
					   FCS */
#define FDDI_K_SNAP_ZLEN	21	/* Min octets in 802.2 SNAP frame sans
					   FCS */
#define FDDI_K_8022_DLEN	4475	/* Max octets in 802.2 payload */
#define FDDI_K_SNAP_DLEN	4470	/* Max octets in 802.2 SNAP payload */
#define FDDI_K_LLC_ZLEN		13	/* Min octets in LLC frame sans FCS */
#define FDDI_K_LLC_LEN		4491	/* Max octets in LLC frame sans FCS */
#define FDDI_K_OUI_LEN		3	/* Octets in OUI in 802.2 SNAP
					   header */

/* Define FDDI Frame Control (FC) Byte values */
#define FDDI_FC_K_VOID			0x00
#define FDDI_FC_K_NON_RESTRICTED_TOKEN	0x80
#define FDDI_FC_K_RESTRICTED_TOKEN	0xC0
#define FDDI_FC_K_SMT_MIN		0x41
#define FDDI_FC_K_SMT_MAX		0x4F
#define FDDI_FC_K_MAC_MIN		0xC1
#define FDDI_FC_K_MAC_MAX		0xCF
#define FDDI_FC_K_ASYNC_LLC_MIN		0x50
#define FDDI_FC_K_ASYNC_LLC_DEF		0x54
#define FDDI_FC_K_ASYNC_LLC_MAX		0x5F
#define FDDI_FC_K_SYNC_LLC_MIN		0xD0
#define FDDI_FC_K_SYNC_LLC_MAX		0xD7
#define FDDI_FC_K_IMPLEMENTOR_MIN	0x60
#define FDDI_FC_K_IMPLEMENTOR_MAX	0x6F
#define FDDI_FC_K_RESERVED_MIN		0x70
#define FDDI_FC_K_RESERVED_MAX		0x7F

/* Define LLC and SNAP constants */
#define FDDI_EXTENDED_SAP		0xAA
#define FDDI_UI_CMD			0x03

/* Define 802.2 Type 1 header */
struct fddi_8022_1_hdr {
	__u8	dsap;			/* destination service access point */
	__u8	ssap;			/* source service access point */
	__u8	ctrl;			/* control byte #1 */
} __attribute__((packed));

/* Define 802.2 Type 2 header */
struct fddi_8022_2_hdr {
	__u8	dsap;			/* destination service access point */
	__u8	ssap;			/* source service access point */
	__u8	ctrl_1;			/* control byte #1 */
	__u8	ctrl_2;			/* control byte #2 */
} __attribute__((packed));

/* Define 802.2 SNAP header */
struct fddi_snap_hdr {
	__u8	dsap;			/* always 0xAA */
	__u8	ssap;			/* always 0xAA */
	__u8	ctrl;			/* always 0x03 */
	__u8	oui[FDDI_K_OUI_LEN];	/* organizational universal id */
	__be16	ethertype;		/* packet type ID field */
} __attribute__((packed));

/* Define FDDI LLC frame header */
struct fddihdr {
	__u8	fc;			/* frame control */
	__u8	daddr[FDDI_K_ALEN];	/* destination address */
	__u8	saddr[FDDI_K_ALEN];	/* source address */
	union {
		struct fddi_8022_1_hdr	llc_8022_1;
		struct fddi_8022_2_hdr	llc_8022_2;
		struct fddi_snap_hdr	llc_snap;
	} hdr;
} __attribute__((packed));


#endif /* _UAPI_LINUX_IF_FDDI_H */
