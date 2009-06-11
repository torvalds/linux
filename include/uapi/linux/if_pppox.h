/***************************************************************************
 * Linux PPP over X - Generic PPP transport layer sockets
 * Linux PPP over Ethernet (PPPoE) Socket Implementation (RFC 2516) 
 *
 * This file supplies definitions required by the PPP over Ethernet driver
 * (pppox.c).  All version information wrt this file is located in pppox.c
 *
 * License:
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#ifndef _UAPI__LINUX_IF_PPPOX_H
#define _UAPI__LINUX_IF_PPPOX_H


#include <linux/types.h>
#include <asm/byteorder.h>

#include <linux/socket.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_pppol2tp.h>
#include <linux/if_pppolac.h>
#include <linux/if_pppopns.h>
#include <linux/in.h>
#include <linux/in6.h>

/* For user-space programs to pick up these definitions
 * which they wouldn't get otherwise without defining __KERNEL__
 */
#ifndef AF_PPPOX
#define AF_PPPOX	24
#define PF_PPPOX	AF_PPPOX
#endif /* !(AF_PPPOX) */

/************************************************************************ 
 * PPPoE addressing definition 
 */ 
typedef __be16 sid_t;
struct pppoe_addr {
	sid_t         sid;                    /* Session identifier */
	unsigned char remote[ETH_ALEN];       /* Remote address */
	char          dev[IFNAMSIZ];          /* Local device to use */
}; 
 
/************************************************************************ 
 * PPTP addressing definition
 */
struct pptp_addr {
	__u16		call_id;
	struct in_addr	sin_addr;
};

/************************************************************************
 * Protocols supported by AF_PPPOX
 */
#define PX_PROTO_OE    0 /* Currently just PPPoE */
#define PX_PROTO_OL2TP 1 /* Now L2TP also */
#define PX_PROTO_PPTP  2
#define PX_PROTO_OLAC  3
#define PX_PROTO_OPNS  4
#define PX_MAX_PROTO   5

struct sockaddr_pppox {
	__kernel_sa_family_t sa_family;       /* address family, AF_PPPOX */
	unsigned int    sa_protocol;          /* protocol identifier */
	union {
		struct pppoe_addr  pppoe;
		struct pptp_addr   pptp;
	} sa_addr;
} __packed;

/* The use of the above union isn't viable because the size of this
 * struct must stay fixed over time -- applications use sizeof(struct
 * sockaddr_pppox) to fill it. We use a protocol specific sockaddr
 * type instead.
 */
struct sockaddr_pppol2tp {
	__kernel_sa_family_t sa_family; /* address family, AF_PPPOX */
	unsigned int    sa_protocol;    /* protocol identifier */
	struct pppol2tp_addr pppol2tp;
} __packed;

struct sockaddr_pppol2tpin6 {
	__kernel_sa_family_t sa_family; /* address family, AF_PPPOX */
	unsigned int    sa_protocol;    /* protocol identifier */
	struct pppol2tpin6_addr pppol2tp;
} __packed;

/* The L2TPv3 protocol changes tunnel and session ids from 16 to 32
 * bits. So we need a different sockaddr structure.
 */
struct sockaddr_pppol2tpv3 {
	__kernel_sa_family_t sa_family; /* address family, AF_PPPOX */
	unsigned int    sa_protocol;    /* protocol identifier */
	struct pppol2tpv3_addr pppol2tp;
} __packed;

struct sockaddr_pppol2tpv3in6 {
	__kernel_sa_family_t sa_family; /* address family, AF_PPPOX */
	unsigned int    sa_protocol;    /* protocol identifier */
	struct pppol2tpv3in6_addr pppol2tp;
} __packed;

/*********************************************************************
 *
 * ioctl interface for defining forwarding of connections
 *
 ********************************************************************/

#define PPPOEIOCSFWD	_IOW(0xB1 ,0, size_t)
#define PPPOEIOCDFWD	_IO(0xB1 ,1)
/*#define PPPOEIOCGFWD	_IOWR(0xB1,2, size_t)*/

/* Codes to identify message types */
#define PADI_CODE	0x09
#define PADO_CODE	0x07
#define PADR_CODE	0x19
#define PADS_CODE	0x65
#define PADT_CODE	0xa7
struct pppoe_tag {
	__be16 tag_type;
	__be16 tag_len;
	char tag_data[0];
} __attribute__ ((packed));

/* Tag identifiers */
#define PTT_EOL		__cpu_to_be16(0x0000)
#define PTT_SRV_NAME	__cpu_to_be16(0x0101)
#define PTT_AC_NAME	__cpu_to_be16(0x0102)
#define PTT_HOST_UNIQ	__cpu_to_be16(0x0103)
#define PTT_AC_COOKIE	__cpu_to_be16(0x0104)
#define PTT_VENDOR 	__cpu_to_be16(0x0105)
#define PTT_RELAY_SID	__cpu_to_be16(0x0110)
#define PTT_SRV_ERR     __cpu_to_be16(0x0201)
#define PTT_SYS_ERR  	__cpu_to_be16(0x0202)
#define PTT_GEN_ERR  	__cpu_to_be16(0x0203)

struct pppoe_hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 type : 4;
	__u8 ver : 4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8 ver : 4;
	__u8 type : 4;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	__u8 code;
	__be16 sid;
	__be16 length;
	struct pppoe_tag tag[0];
} __packed;

/* Length of entire PPPoE + PPP header */
#define PPPOE_SES_HLEN	8


#endif /* _UAPI__LINUX_IF_PPPOX_H */
