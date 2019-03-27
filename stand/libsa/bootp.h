/*	$NetBSD: bootp.h,v 1.4 1997/09/06 13:55:57 drochner Exp $	*/

/*
 * Bootstrap Protocol (BOOTP).  RFC951 and RFC1048.
 *
 * This file specifies the "implementation-independent" BOOTP protocol
 * information which is common to both client and server.
 *
 * Copyright 1988 by Carnegie Mellon.
 *
 * Permission to use, copy, modify, and distribute this program for any
 * purpose and without fee is hereby granted, provided that this copyright
 * and permission notice appear on all copies and supporting documentation,
 * the name of Carnegie Mellon not be used in advertising or publicity
 * pertaining to distribution of the program without specific prior
 * permission, and notice be given in supporting documentation that copying
 * and distribution is by permission of Carnegie Mellon and Stanford
 * University.  Carnegie Mellon makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * $FreeBSD$
 */

#ifndef _BOOTP_H_
#define _BOOTP_H_

struct bootp {
	unsigned char	bp_op;		/* packet opcode type */
	unsigned char	bp_htype;	/* hardware addr type */
	unsigned char	bp_hlen;	/* hardware addr length */
	unsigned char	bp_hops;	/* gateway hops */
	unsigned int	bp_xid;		/* transaction ID */
	unsigned short	bp_secs;	/* seconds since boot began */
	unsigned short	bp_flags;
	struct in_addr	bp_ciaddr;	/* client IP address */
	struct in_addr	bp_yiaddr;	/* 'your' IP address */
	struct in_addr	bp_siaddr;	/* server IP address */
	struct in_addr	bp_giaddr;	/* gateway IP address */
	unsigned char	bp_chaddr[16];	/* client hardware address */
	unsigned char	bp_sname[64];	/* server host name */
	char		bp_file[128];	/* boot file name */
#ifdef SUPPORT_DHCP
#define BOOTP_VENDSIZE 312
#else
#define BOOTP_VENDSIZE 64
#endif
	unsigned char	bp_vend[BOOTP_VENDSIZE];	/* vendor-specific area */
};

/*
 * UDP port numbers, server and client.
 */
#define	IPPORT_BOOTPS		67
#define	IPPORT_BOOTPC		68

#define BOOTREPLY		2
#define BOOTREQUEST		1


/*
 * Vendor magic cookie (v_magic) for CMU
 */
#define VM_CMU		"CMU"

/*
 * Vendor magic cookie (v_magic) for RFC1048
 */
#define VM_RFC1048	{ 99, 130, 83, 99 }



/*
 * RFC1048 tag values used to specify what information is being supplied in
 * the vendor field of the packet.
 */

#define TAG_PAD			((unsigned char)   0)
#define TAG_SUBNET_MASK		((unsigned char)   1)
#define TAG_TIME_OFFSET		((unsigned char)   2)
#define TAG_GATEWAY		((unsigned char)   3)
#define TAG_TIME_SERVER		((unsigned char)   4)
#define TAG_NAME_SERVER		((unsigned char)   5)
#define TAG_DOMAIN_SERVER	((unsigned char)   6)
#define TAG_LOG_SERVER		((unsigned char)   7)
#define TAG_COOKIE_SERVER	((unsigned char)   8)
#define TAG_LPR_SERVER		((unsigned char)   9)
#define TAG_IMPRESS_SERVER	((unsigned char)  10)
#define TAG_RLP_SERVER		((unsigned char)  11)
#define TAG_HOSTNAME		((unsigned char)  12)
#define TAG_BOOTSIZE		((unsigned char)  13)
#define TAG_DUMPFILE		((unsigned char)  14)
#define TAG_DOMAINNAME		((unsigned char)  15)
#define TAG_SWAPSERVER		((unsigned char)  16)
#define TAG_ROOTPATH		((unsigned char)  17)
#define TAG_INTF_MTU		((unsigned char)  26)

#ifdef SUPPORT_DHCP
#define TAG_REQ_ADDR		((unsigned char)  50)
#define TAG_LEASETIME		((unsigned char)  51)
#define TAG_OVERLOAD		((unsigned char)  52)
#define TAG_DHCP_MSGTYPE	((unsigned char)  53)
#define TAG_SERVERID		((unsigned char)  54)
#define TAG_PARAM_REQ		((unsigned char)  55)
#define TAG_MSG			((unsigned char)  56)
#define TAG_MAXSIZE		((unsigned char)  57)
#define TAG_T1			((unsigned char)  58)
#define TAG_T2			((unsigned char)  59)
#define TAG_CLASSID		((unsigned char)  60)
#define TAG_CLIENTID		((unsigned char)  61)
#define TAG_USER_CLASS		((unsigned char)  77)
#endif

#define TAG_END			((unsigned char) 255)

#ifdef SUPPORT_DHCP
#define DHCPDISCOVER 1
#define DHCPOFFER 2
#define DHCPREQUEST 3
#define DHCPDECLINE 4
#define DHCPACK 5
#define DHCPNAK 6
#define DHCPRELEASE 7
#endif

/*
 * "vendor" data permitted for CMU bootp clients.
 */

struct cmu_vend {
	unsigned char	v_magic[4];	/* magic number */
	unsigned int	v_flags;	/* flags/opcodes, etc. */
	struct in_addr	v_smask;	/* Subnet mask */
	struct in_addr	v_dgate;	/* Default gateway */
	struct in_addr	v_dns1, v_dns2; /* Domain name servers */
	struct in_addr	v_ins1, v_ins2; /* IEN-116 name servers */
	struct in_addr	v_ts1, v_ts2;	/* Time servers */
	unsigned char	v_unused[25];	/* currently unused */
};


/* v_flags values */
#define VF_SMASK	1	/* Subnet mask field contains valid data */

/* cached bootp response/dhcp ack */
extern struct bootp *bootp_response;
extern size_t bootp_response_size;

#endif /* _BOOTP_H_ */
