/*
 *  UDPLITEv6   An implementation of the UDP-Lite protocol over IPv6.
 *              See also net/ipv4/udplite.c
 *
 *  Version:    $Id: udplite.c,v 1.9 2006/10/19 08:28:10 gerrit Exp $
 *
 *  Authors:    Gerrit Renker       <gerrit@erg.abdn.ac.uk>
 *
 *  Changes:
 *  Fixes:
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include "udp_impl.h"

DEFINE_SNMP_STAT(struct udp_mib, udplite_stats_in6) __read_mostly;

static int udplitev6_rcv(struct sk_buff **pskb)
{
	return __udp6_lib_rcv(pskb, udplite_hash, IPPROTO_UDPLITE);
}

static void udplitev6_err(struct sk_buff *skb,
			  struct inet6_skb_parm *opt,
			  int type, int code, int offset, __be32 info)
{
	return __udp6_lib_err(skb, opt, type, code, offset, info, udplite_hash);
}

static struct inet6_protocol udplitev6_protocol = {
	.handler	=	udplitev6_rcv,
	.err_handler	=	udplitev6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

static int udplite_v6_get_port(struct sock *sk, unsigned short snum)
{
	return udplite_get_port(sk, snum, ipv6_rcv_saddr_equal);
}

struct proto udplitev6_prot = {
	.name		   = "UDPLITEv6",
	.owner		   = THIS_MODULE,
	.close		   = udp_lib_close,
	.connect	   = ip6_datagram_connect,
	.disconnect	   = udp_disconnect,
	.ioctl		   = udp_ioctl,
	.init		   = udplite_sk_init,
	.destroy	   = udpv6_destroy_sock,
	.setsockopt	   = udpv6_setsockopt,
	.getsockopt	   = udpv6_getsockopt,
	.sendmsg	   = udpv6_sendmsg,
	.recvmsg	   = udpv6_recvmsg,
	.backlog_rcv	   = udpv6_queue_rcv_skb,
	.hash		   = udp_lib_hash,
	.unhash		   = udp_lib_unhash,
	.get_port	   = udplite_v6_get_port,
	.obj_size	   = sizeof(struct udp6_sock),
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_udpv6_setsockopt,
	.compat_getsockopt = compat_udpv6_getsockopt,
#endif
};

static struct inet_protosw udplite6_protosw = {
	.type		= SOCK_DGRAM,
	.protocol	= IPPROTO_UDPLITE,
	.prot		= &udplitev6_prot,
	.ops		= &inet6_dgram_ops,
	.capability	= -1,
	.no_check	= 0,
	.flags		= INET_PROTOSW_PERMANENT,
};

void __init udplitev6_init(void)
{
	if (inet6_add_protocol(&udplitev6_protocol, IPPROTO_UDPLITE) < 0)
		printk(KERN_ERR "%s: Could not register.\n", __FUNCTION__);

	inet6_register_protosw(&udplite6_protosw);
}

#ifdef CONFIG_PROC_FS
static struct file_operations udplite6_seq_fops;
static struct udp_seq_afinfo udplite6_seq_afinfo = {
	.owner		= THIS_MODULE,
	.name		= "udplite6",
	.family		= AF_INET6,
	.hashtable	= udplite_hash,
	.seq_show	= udp6_seq_show,
	.seq_fops	= &udplite6_seq_fops,
};

int __init udplite6_proc_init(void)
{
	return udp_proc_register(&udplite6_seq_afinfo);
}

void udplite6_proc_exit(void)
{
	udp_proc_unregister(&udplite6_seq_afinfo);
}
#endif
