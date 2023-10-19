/* SPDX-License-Identifier: GPL-1.0+ WITH Linux-syscall-note */
/*
 * IEEE 802.2 User Interface SAPs for Linux, data structures and indicators.
 *
 * Copyright (c) 2001 by Jay Schulist <jschlst@samba.org>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#ifndef _UAPI__LINUX_LLC_H
#define _UAPI__LINUX_LLC_H

#include <linux/socket.h>
#include <linux/if.h> 		/* For IFHWADDRLEN. */

#define __LLC_SOCK_SIZE__ 16	/* sizeof(sockaddr_llc), word align. */
struct sockaddr_llc {
	__kernel_sa_family_t sllc_family; /* AF_LLC */
	__kernel_sa_family_t sllc_arphrd; /* ARPHRD_ETHER */
	unsigned char   sllc_test;
	unsigned char   sllc_xid;
	unsigned char	sllc_ua;	/* UA data, only for SOCK_STREAM. */
	unsigned char   sllc_sap;
	unsigned char   sllc_mac[IFHWADDRLEN];
	unsigned char   __pad[__LLC_SOCK_SIZE__ -
			      sizeof(__kernel_sa_family_t) * 2 -
			      sizeof(unsigned char) * 4 - IFHWADDRLEN];
};

/* sockopt definitions. */
enum llc_sockopts {
	LLC_OPT_UNKNOWN = 0,
	LLC_OPT_RETRY,		/* max retrans attempts. */
	LLC_OPT_SIZE,		/* max PDU size (octets). */
	LLC_OPT_ACK_TMR_EXP,	/* ack expire time (secs). */
	LLC_OPT_P_TMR_EXP,	/* pf cycle expire time (secs). */
	LLC_OPT_REJ_TMR_EXP,	/* rej sent expire time (secs). */
	LLC_OPT_BUSY_TMR_EXP,	/* busy state expire time (secs). */
	LLC_OPT_TX_WIN,		/* tx window size. */
	LLC_OPT_RX_WIN,		/* rx window size. */
	LLC_OPT_PKTINFO,	/* ancillary packet information. */
	LLC_OPT_MAX
};

#define LLC_OPT_MAX_RETRY	 100
#define LLC_OPT_MAX_SIZE	4196
#define LLC_OPT_MAX_WIN		 127
#define LLC_OPT_MAX_ACK_TMR_EXP	  60
#define LLC_OPT_MAX_P_TMR_EXP	  60
#define LLC_OPT_MAX_REJ_TMR_EXP	  60
#define LLC_OPT_MAX_BUSY_TMR_EXP  60

/* LLC SAP types. */
#define LLC_SAP_NULL	0x00		/* NULL SAP. 			*/
#define LLC_SAP_LLC	0x02		/* LLC Sublayer Management. 	*/
#define LLC_SAP_SNA	0x04		/* SNA Path Control. 		*/
#define LLC_SAP_PNM	0x0E		/* Proway Network Management.	*/	
#define LLC_SAP_IP	0x06		/* TCP/IP. 			*/
#define LLC_SAP_BSPAN	0x42		/* Bridge Spanning Tree Proto	*/
#define LLC_SAP_MMS	0x4E		/* Manufacturing Message Srv.	*/
#define LLC_SAP_8208	0x7E		/* ISO 8208			*/
#define LLC_SAP_3COM	0x80		/* 3COM. 			*/
#define LLC_SAP_PRO	0x8E		/* Proway Active Station List	*/
#define LLC_SAP_SNAP	0xAA		/* SNAP. 			*/
#define LLC_SAP_BANYAN	0xBC		/* Banyan. 			*/
#define LLC_SAP_IPX	0xE0		/* IPX/SPX. 			*/
#define LLC_SAP_NETBEUI	0xF0		/* NetBEUI. 			*/
#define LLC_SAP_LANMGR	0xF4		/* LanManager. 			*/
#define LLC_SAP_IMPL	0xF8		/* IMPL				*/
#define LLC_SAP_DISC	0xFC		/* Discovery			*/
#define LLC_SAP_OSI	0xFE		/* OSI Network Layers. 		*/
#define LLC_SAP_LAR	0xDC		/* LAN Address Resolution 	*/
#define LLC_SAP_RM	0xD4		/* Resource Management 		*/
#define LLC_SAP_GLOBAL	0xFF		/* Global SAP. 			*/

struct llc_pktinfo {
	int lpi_ifindex;
	unsigned char lpi_sap;
	unsigned char lpi_mac[IFHWADDRLEN];
};

#endif /* _UAPI__LINUX_LLC_H */
