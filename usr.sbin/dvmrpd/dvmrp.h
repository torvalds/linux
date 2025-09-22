/*	$OpenBSD: dvmrp.h,v 1.3 2009/03/14 15:32:55 michele Exp $ */

/*
 * Copyright (c) 2005, 2006 Esben Norby <norby@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* DVMRP protocol definitions */

#ifndef _DVMRP_H_
#define _DVMRP_H_

/* misc */
#define IPPROTO_DVMRP			2
#define AllDVMRPRouters			"224.0.0.4"

#define DEFAULT_PROBE_INTERVAL		10
#define NBR_TMOUT			35
#define MIN_FLASH_UPDATE_INTERVAL	5
#define ROUTE_REPORT_INTERVAL		60
#define ROUTE_EXPIRATION_TIME		140
#define ROUTE_HOLD_DOWN			2 * ROUTE_REPORT_INTERVAL
#define MAX_PRUNE_LIFETIME		2 * 3600	/* 2 hours */
#define PRUNE_RETRANS_TIME		3		/* with exp. back off */
#define GRAFT_RETRANS_TIME		5		/* with exp. back off */

#define DEFAULT_CACHE_LIFETIME		300

#define DEFAULT_METRIC			1
#define MIN_METRIC			1
#define MAX_METRIC			31
#define INFINITY_METRIC			31

#define LAST_MASK			0x80		/* route reports */
#define METRIC_MASK			~LAST_MASK	/* route reports */

#define DVMRP_MAJOR_VERSION		3
#define DVMRP_MINOR_VERSION		255

/* DVMRP packet types */
#define PKT_TYPE_DVMRP			0x13

#define DVMRP_CODE_PROBE		0x01
#define DVMRP_CODE_REPORT		0x02
#define DVMRP_CODE_ASK_NBRS		0x03	/* obsolete */
#define DVMRP_CODE_NBRS			0x04	/* obsolete */
#define DVMRP_CODE_ASK_NBRS2		0x05
#define DVMRP_CODE_NBRS2		0x06
#define DVMRP_CODE_PRUNE		0x07
#define DVMRP_CODE_GRAFT		0x08
#define DVMRP_CODE_GRAFT_ACK		0x09

/* DVMRP command types */
#define DVMRP_CMD_NULL			0
#define DVMRP_CMD_AF_INDICATOR		2
#define DVMRP_CMD_SUBNETMASK		3
#define DVMRP_CMD_METRIC		4
#define DVMRP_CMD_FLAGS0		5
#define DVMRP_CMD_INFINITY		6
#define DVMRP_CMD_DEST_ADDR		7
#define DVMRP_CMD_REQ_DEST_ADDR		8
#define DVMRP_CMD_NON_MEM_REPORT	9
#define DVMRP_CMD_NON_MEM_CANCEL	10

/* DVMRP capabilities */
#define DVMRP_CAP_LEAF			0x01
#define DVMRP_CAP_PRUNE			0x02
#define DVMRP_CAP_GENID			0x04
#define DVMRP_CAP_MTRACE		0x08
#define DVMRP_CAP_SNMP			0x10
#define DVMRP_CAP_NETMASK		0x20
#define DVMRP_CAP_DEFAULT		(DVMRP_CAP_PRUNE | DVMRP_CAP_GENID | \
					    DVMRP_CAP_MTRACE)

/* DVMRP header */
struct dvmrp_hdr {
	u_int8_t		type;
	u_int8_t		code;
	u_int16_t		chksum;
	u_int8_t		dummy;
	u_int8_t		capabilities;
	u_int8_t		minor_version;
	u_int8_t		major_version;
};

/* Prune header */
struct prune_hdr {
	u_int32_t		src_host_addr;
	u_int32_t		group_addr;
	u_int32_t		lifetime;
	u_int32_t		src_netmask;
};
#define	PRUNE_MIN_LEN			12

/* Graft and Graft Ack header */
struct graft_hdr {
	u_int32_t		src_host_addr;
	u_int32_t		group_addr;
	u_int32_t		src_netmask;
};

struct igmpmsg {
	u_int32_t unused1;
	u_int32_t unused2;
	u_int8_t  im_msgtype;		/* what type of message */
#define	IGMPMSG_NOCACHE		1	/* no MFC in the kernel		    */
#define	IGMPMSG_WRONGVIF	2	/* packet came from wrong interface */
#define	IGMPMSG_WHOLEPKT	3	/* PIM pkt for user level encap.    */
#define	IGMPMSG_BW_UPCALL	4	/* BW monitoring upcall		    */
	u_int8_t  im_mbz;		/* must be zero */
	u_int8_t  im_vif;		/* vif rec'd on */
	u_int8_t  unused3;
	struct	  in_addr im_src, im_dst;
};

#endif /* !_DVMRP_H_ */
