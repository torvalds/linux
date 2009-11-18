/*
 *  UDPLITE     An implementation of the UDP-Lite protocol (RFC 3828).
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

struct udp_table 	udplite_table;
EXPORT_SYMBOL(udplite_table);

static int udplite_rcv(struct sk_buff *skb)
{
	return __udp4_lib_rcv(skb, &udplite_table, IPPROTO_UDPLITE);
}

static void udplite_err(struct sk_buff *skb, u32 info)
{
	__udp4_lib_err(skb, info, &udplite_table);
}

static const struct net_protocol udplite_protocol = {
	.handler	= udplite_rcv,
	.err_handler	= udplite_err,
	.no_policy	= 1,
	.netns_ok	= 1,
};

struct proto 	udplite_prot = {
	.name		   = "UDP-Lite",
	.owner		   = THIS_MODULE,
	.close		   = udp_lib_close,
	.connect	   = ip4_datagram_connect,
	.disconnect	   = udp_disconnect,
	.ioctl		   = udp_ioctl,
	.init		   = udplite_sk_init,
	.destroy	   = udp_destroy_sock,
	.setsockopt	   = udp_setsockopt,
	.getsockopt	   = udp_getsockopt,
	.sendmsg	   = udp_sendmsg,
	.recvmsg	   = udp_recvmsg,
	.sendpage	   = udp_sendpage,
	.backlog_rcv	   = udp_queue_rcv_skb,
	.hash		   = udp_lib_hash,
	.unhash		   = udp_lib_unhash,
	.get_port	   = udp_v4_get_port,
	.obj_size	   = sizeof(struct udp_sock),
	.slab_flags	   = SLAB_DESTROY_BY_RCU,
	.h.udp_table	   = &udplite_table,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_udp_setsockopt,
	.compat_getsockopt = compat_udp_getsockopt,
#endif
};

static struct inet_protosw udplite4_protosw = {
	.type		=  SOCK_DGRAM,
	.protocol	=  IPPROTO_UDPLITE,
	.prot		=  &udplite_prot,
	.ops		=  &inet_dgram_ops,
	.capability	= -1,
	.no_check	=  0,		/* must checksum (RFC 3828) */
	.flags		=  INET_PROTOSW_PERMANENT,
};

#ifdef CONFIG_PROC_FS
static struct udp_seq_afinfo udplite4_seq_afinfo = {
	.name		= "udplite",
	.family		= AF_INET,
	.udp_table 	= &udplite_table,
	.seq_fops	= {
		.owner	=	THIS_MODULE,
	},
	.seq_ops	= {
		.show		= udp4_seq_show,
	},
};

static int udplite4_proc_init_net(struct net *net)
{
	return udp_proc_register(net, &udplite4_seq_afinfo);
}

static void udplite4_proc_exit_net(struct net *net)
{
	udp_proc_unregister(net, &udplite4_seq_afinfo);
}

static struct pernet_operations udplite4_net_ops = {
	.init = udplite4_proc_init_net,
	.exit = udplite4_proc_exit_net,
};

static __init int udplite4_proc_init(void)
{
	return register_pernet_subsys(&udplite4_net_ops);
}
#else
static inline int udplite4_proc_init(void)
{
	return 0;
}
#endif

void __init udplite4_register(void)
{
	udp_table_init(&udplite_table);
	if (proto_register(&udplite_prot, 1))
		goto out_register_err;

	if (inet_add_protocol(&udplite_protocol, IPPROTO_UDPLITE) < 0)
		goto out_unregister_proto;

	inet_register_protosw(&udplite4_protosw);

	if (udplite4_proc_init())
		printk(KERN_ERR "%s: Cannot register /proc!\n", __func__);
	return;

out_unregister_proto:
	proto_unregister(&udplite_prot);
out_register_err:
	printk(KERN_CRIT "%s: Cannot add UDP-Lite protocol.\n", __func__);
}

EXPORT_SYMBOL(udplite_prot);
