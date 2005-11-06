/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for Fibre Channel.
 *
 * Version:	@(#)if_fc.h	0.0	11/20/98
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *    Peter De Schrijver, <stud11@cc4.kuleuven.ac.be>
 *	  Vineet Abraham, <vma@iol.unh.edu>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_FC_H
#define _LINUX_IF_FC_H


#define FC_ALEN	6		/* Octets in one ethernet addr	 */
#define FC_HLEN   (sizeof(struct fch_hdr)+sizeof(struct fcllc))
#define FC_ID_LEN 3		/* Octets in a Fibre Channel Address */

/* LLC and SNAP constants */
#define EXTENDED_SAP 0xAA
#define UI_CMD       0x03

/* This is NOT the Fibre Channel frame header. The FC frame header is
 *  constructed in the driver as the Tachyon needs certain fields in
 *  certains positions. So, it can't be generalized here.*/

struct fch_hdr {
	__u8  daddr[FC_ALEN];		/* destination address */
	__u8  saddr[FC_ALEN];		/* source address */
};

/* This is a Fibre Channel LLC structure */
struct fcllc {
	__u8  dsap;			/* destination SAP */
	__u8  ssap;			/* source SAP */
	__u8  llc;			/* LLC control field */
	__u8  protid[3];		/* protocol id */
	__be16 ethertype;		/* ether type field */
};

#endif	/* _LINUX_IF_FC_H */
