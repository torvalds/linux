/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GTP_H_
#define _GTP_H_

#include <linux/netdevice.h>
#include <linux/types.h>
#include <net/rtnetlink.h>

/* General GTP protocol related definitions. */

#define GTP0_PORT	3386
#define GTP1U_PORT	2152

/* GTP messages types */
#define GTP_ECHO_REQ	1	/* Echo Request */
#define GTP_ECHO_RSP	2	/* Echo Response */
#define GTP_TPDU	255

#define GTPIE_RECOVERY	14

struct gtp0_header {	/* According to GSM TS 09.60. */
	__u8	flags;
	__u8	type;
	__be16	length;
	__be16	seq;
	__be16	flow;
	__u8	number;
	__u8	spare[3];
	__be64	tid;
} __attribute__ ((packed));

struct gtp1_header {	/* According to 3GPP TS 29.060. */
	__u8	flags;
	__u8	type;
	__be16	length;
	__be32	tid;
} __attribute__ ((packed));

struct gtp1_header_long {	/* According to 3GPP TS 29.060. */
	__u8	flags;
	__u8	type;
	__be16	length;
	__be32	tid;
	__be16	seq;
	__u8	npdu;
	__u8	next;
} __packed;

/* GTP Information Element */
struct gtp_ie {
	__u8	tag;
	__u8	val;
} __packed;

struct gtp0_packet {
	struct gtp0_header gtp0_h;
	struct gtp_ie ie;
} __packed;

struct gtp1u_packet {
	struct gtp1_header_long gtp1u_h;
	struct gtp_ie ie;
} __packed;

struct gtp_pdu_session_info {	/* According to 3GPP TS 38.415. */
	u8	pdu_type;
	u8	qfi;
};

static inline bool netif_is_gtp(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
		!strcmp(dev->rtnl_link_ops->kind, "gtp");
}

#define GTP1_F_NPDU	0x01
#define GTP1_F_SEQ	0x02
#define GTP1_F_EXTHDR	0x04
#define GTP1_F_MASK	0x07

struct gtp_ext_hdr {
	__u8	len;
	__u8	data[];
};

#endif
