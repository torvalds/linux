/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_TFTP_H
#define _NF_CONNTRACK_TFTP_H

#define TFTP_PORT 69

#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/netfilter/nf_conntrack_expect.h>

struct tftphdr {
	__be16 opcode;
};

#define TFTP_OPCODE_READ	1
#define TFTP_OPCODE_WRITE	2
#define TFTP_OPCODE_DATA	3
#define TFTP_OPCODE_ACK		4
#define TFTP_OPCODE_ERROR	5

extern unsigned int (*nf_nat_tftp_hook)(struct sk_buff *skb,
				        enum ip_conntrack_info ctinfo,
				        struct nf_conntrack_expect *exp);

#endif /* _NF_CONNTRACK_TFTP_H */
