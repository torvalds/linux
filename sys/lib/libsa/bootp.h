/*	$OpenBSD: bootp.h,v 1.5 2003/08/11 06:23:09 deraadt Exp $	*/
/*	$NetBSD: bootp.h,v 1.3 1996/09/26 23:22:01 cgd Exp $	*/

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
 */

#define BP_CHADDR_LEN	16
#define BP_SNAME_LEN	64
#define BP_FILE_LEN	128
#define BP_VEND_LEN	64
#define BP_MINPKTSZ	300     /* to check sizeof(struct bootp) */


struct bootp {
	u_char		bp_op;			/* packet opcode type */
	u_char		bp_htype;		/* hardware addr type */
	u_char		bp_hlen;		/* hardware addr length */
	u_char		bp_hops;		/* gateway hops */
	u_int		bp_xid;			/* transaction ID */
	u_short		bp_secs;		/* seconds since boot began */
	u_short		bp_flags;		/* RFC1532 broadcast, etc. */
	struct in_addr	bp_ciaddr;		/* client IP address */
	struct in_addr	bp_yiaddr;		/* 'your' IP address */
	struct in_addr	bp_siaddr;		/* server IP address */
	struct in_addr	bp_giaddr;		/* gateway IP address */
	u_char		bp_chaddr[BP_CHADDR_LEN];/* client hardware address */
	u_char		bp_sname[BP_SNAME_LEN];	/* server host name */
	u_char		bp_file[BP_FILE_LEN];	/* boot file name */
	u_char		bp_vend[BP_VEND_LEN];	/* vendor-specific area */
};

/*
 * UDP port numbers, server and client.
 */
#define	IPPORT_BOOTPS		67
#define	IPPORT_BOOTPC		68

#define BOOTREPLY		2
#define BOOTREQUEST		1

/*
 * Hardware types from Assigned Numbers RFC.
 */
#define HTYPE_ETHERNET		1
#define HTYPE_EXP_ETHERNET	2
#define HTYPE_AX25		3
#define HTYPE_PRONET		4
#define HTYPE_CHAOS		5
#define HTYPE_IEEE802		6
#define HTYPE_ARCNET		7

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
#define TAG_EXTEN_FILE		((unsigned char)  18)
#define TAG_NIS_DOMAIN		((unsigned char)  40)
#define TAG_NIS_SERVER		((unsigned char)  41)
#define TAG_NTP_SERVER		((unsigned char)  42)
#define TAG_MAX_MSGSZ		((unsigned char)  57)
#define TAG_END			((unsigned char) 255)



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

void	bootp(int);
