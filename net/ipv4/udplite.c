// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  UDPLITE     An implementation of the UDP-Lite protocol (RFC 3828).
 *
 *  Authors:    Gerrit Renker       <gerrit@erg.abdn.ac.uk>
 *
 *  Changes:
 *  Fixes:
 */

#define pr_fmt(fmt) "UDPLite: " fmt

#include <linux/export.h>
#include <linux/proc_fs.h>
#include "udp_impl.h"

struct udp_table 	udplite_table __read_mostly;
EXPORT_SYMBOL(udplite_table);

/* Designate sk as UDP-Lite socket */
static int udplite_sk_init(struct sock *sk)
{
	udp_init_sock(sk);
	udp_sk(sk)->pcflag = UDPLITE_BIT;
	pr_warn_once("UDP-Lite is deprecated and scheduled to be removed in 2025, "
		     "please contact the netdev mailing list\n");
	return 0;
}

static int udplite_rcv(struct sk_buff *skb)
{
	return __udp4_lib_rcv(skb, &udplite_table, IPPROTO_UDPLITE);
}

static int udplite_err(struct sk_buff *skb, u32 info)
{
	return __udp4_lib_err(skb, info, &udplite_table);
}

static const struct net_protocol udplite_protocol = {
	.handler	= udplite_rcv,
	.err_handler	= udplite_err,
	.no_policy	= 1,
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
	.hash		   = udp_lib_hash,
	.unhash		   = udp_lib_unhash,
	.rehash		   = udp_v4_rehash,
	.get_port	   = udp_v4_get_port,

	.memory_allocated  = &udp_memory_allocated,
	.per_cpu_fw_alloc  = &udp_memory_per_cpu_fw_alloc,

	.sysctl_mem	   = sysctl_udp_mem,
	.sysctl_wmem_offset = offsetof(struct net, ipv4.sysctl_udp_wmem_min),
	.sysctl_rmem_offset = offsetof(struct net, ipv4.sysctl_udp_rmem_min),
	.obj_size	   = sizeof(struct udp_sock),
	.h.udp_table	   = &udplite_table,
};
EXPORT_SYMBOL(udplite_prot);

static struct inet_protosw udplite4_protosw = {
	.type		=  SOCK_DGRAM,
	.protocol	=  IPPROTO_UDPLITE,
	.prot		=  &udplite_prot,
	.ops		=  &inet_dgram_ops,
	.flags		=  INET_PROTOSW_PERMANENT,
};

#ifdef CONFIG_PROC_FS
static struct udp_seq_afinfo udplite4_seq_afinfo = {
	.family		= AF_INET,
	.udp_table 	= &udplite_table,
};

static int __net_init udplite4_proc_init_net(struct net *net)
{
	if (!proc_create_net_data("udplite", 0444, net->proc_net, &udp_seq_ops,
			sizeof(struct udp_iter_state), &udplite4_seq_afinfo))
		return -ENOMEM;
	return 0;
}

static void __net_exit udplite4_proc_exit_net(struct net *net)
{
	remove_proc_entry("udplite", net->proc_net);
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
	udp_table_init(&udplite_table, "UDP-Lite");
	if (proto_register(&udplite_prot, 1))
		goto out_register_err;

	if (inet_add_protocol(&udplite_protocol, IPPROTO_UDPLITE) < 0)
		goto out_unregister_proto;

	inet_register_protosw(&udplite4_protosw);

	if (udplite4_proc_init())
		pr_err("%s: Cannot register /proc!\n", __func__);
	return;

out_unregister_proto:
	proto_unregister(&udplite_prot);
out_register_err:
	pr_crit("%s: Cannot add UDP-Lite protocol\n", __func__);
}
