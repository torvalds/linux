/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the ICMP module.
 *
 * Version:	@(#)icmp.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ICMP_H
#define	_ICMP_H

#include <linux/icmp.h>

#include <net/inet_sock.h>
#include <net/snmp.h>

struct icmp_err {
  int		errno;
  unsigned int	fatal:1;
};

extern const struct icmp_err icmp_err_convert[];
#define ICMP_INC_STATS(net, field)	SNMP_INC_STATS((net)->mib.icmp_statistics, field)
#define __ICMP_INC_STATS(net, field)	__SNMP_INC_STATS((net)->mib.icmp_statistics, field)
#define ICMPMSGOUT_INC_STATS(net, field)	SNMP_INC_STATS_ATOMIC_LONG((net)->mib.icmpmsg_statistics, field+256)
#define ICMPMSGIN_INC_STATS(net, field)		SNMP_INC_STATS_ATOMIC_LONG((net)->mib.icmpmsg_statistics, field)

struct dst_entry;
struct net_proto_family;
struct sk_buff;
struct net;

void icmp_send(struct sk_buff *skb_in, int type, int code, __be32 info);
int icmp_rcv(struct sk_buff *skb);
void icmp_err(struct sk_buff *skb, u32 info);
int icmp_init(void);
void icmp_out_count(struct net *net, unsigned char type);

#endif	/* _ICMP_H */
