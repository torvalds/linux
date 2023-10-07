// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	PF_INET6 socket protocol family
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Adapted from linux/net/ipv4/af_inet.c
 *
 *	Fixes:
 *	piggy, Karl Knutson	:	Socket protocol table
 *	Hideaki YOSHIFUJI	:	sin6_scope_id support
 *	Arnaldo Melo		:	check proc_net_create return, cleanups
 */

#define pr_fmt(fmt) "IPv6: " fmt

#include <linux/module.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmpv6.h>
#include <linux/netfilter_ipv6.h>

#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/udplite.h>
#include <net/tcp.h>
#include <net/ping.h>
#include <net/protocol.h>
#include <net/inet_common.h>
#include <net/route.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ipv6_stubs.h>
#include <net/ndisc.h>
#ifdef CONFIG_IPV6_TUNNEL
#include <net/ip6_tunnel.h>
#endif
#include <net/calipso.h>
#include <net/seg6.h>
#include <net/rpl.h>
#include <net/compat.h>
#include <net/xfrm.h>
#include <net/ioam6.h>
#include <net/rawv6.h>

#include <linux/uaccess.h>
#include <linux/mroute6.h>

#include "ip6_offload.h"

MODULE_AUTHOR("Cast of dozens");
MODULE_DESCRIPTION("IPv6 protocol stack for Linux");
MODULE_LICENSE("GPL");

/* The inetsw6 table contains everything that inet6_create needs to
 * build a new socket.
 */
static struct list_head inetsw6[SOCK_MAX];
static DEFINE_SPINLOCK(inetsw6_lock);

struct ipv6_params ipv6_defaults = {
	.disable_ipv6 = 0,
	.autoconf = 1,
};

static int disable_ipv6_mod;

module_param_named(disable, disable_ipv6_mod, int, 0444);
MODULE_PARM_DESC(disable, "Disable IPv6 module such that it is non-functional");

module_param_named(disable_ipv6, ipv6_defaults.disable_ipv6, int, 0444);
MODULE_PARM_DESC(disable_ipv6, "Disable IPv6 on all interfaces");

module_param_named(autoconf, ipv6_defaults.autoconf, int, 0444);
MODULE_PARM_DESC(autoconf, "Enable IPv6 address autoconfiguration on all interfaces");

bool ipv6_mod_enabled(void)
{
	return disable_ipv6_mod == 0;
}
EXPORT_SYMBOL_GPL(ipv6_mod_enabled);

static struct ipv6_pinfo *inet6_sk_generic(struct sock *sk)
{
	const int offset = sk->sk_prot->ipv6_pinfo_offset;

	return (struct ipv6_pinfo *)(((u8 *)sk) + offset);
}

void inet6_sock_destruct(struct sock *sk)
{
	inet6_cleanup_sock(sk);
	inet_sock_destruct(sk);
}
EXPORT_SYMBOL_GPL(inet6_sock_destruct);

static int inet6_create(struct net *net, struct socket *sock, int protocol,
			int kern)
{
	struct inet_sock *inet;
	struct ipv6_pinfo *np;
	struct sock *sk;
	struct inet_protosw *answer;
	struct proto *answer_prot;
	unsigned char answer_flags;
	int try_loading_module = 0;
	int err;

	if (protocol < 0 || protocol >= IPPROTO_MAX)
		return -EINVAL;

	/* Look for the requested type/protocol pair. */
lookup_protocol:
	err = -ESOCKTNOSUPPORT;
	rcu_read_lock();
	list_for_each_entry_rcu(answer, &inetsw6[sock->type], list) {

		err = 0;
		/* Check the non-wild match. */
		if (protocol == answer->protocol) {
			if (protocol != IPPROTO_IP)
				break;
		} else {
			/* Check for the two wild cases. */
			if (IPPROTO_IP == protocol) {
				protocol = answer->protocol;
				break;
			}
			if (IPPROTO_IP == answer->protocol)
				break;
		}
		err = -EPROTONOSUPPORT;
	}

	if (err) {
		if (try_loading_module < 2) {
			rcu_read_unlock();
			/*
			 * Be more specific, e.g. net-pf-10-proto-132-type-1
			 * (net-pf-PF_INET6-proto-IPPROTO_SCTP-type-SOCK_STREAM)
			 */
			if (++try_loading_module == 1)
				request_module("net-pf-%d-proto-%d-type-%d",
						PF_INET6, protocol, sock->type);
			/*
			 * Fall back to generic, e.g. net-pf-10-proto-132
			 * (net-pf-PF_INET6-proto-IPPROTO_SCTP)
			 */
			else
				request_module("net-pf-%d-proto-%d",
						PF_INET6, protocol);
			goto lookup_protocol;
		} else
			goto out_rcu_unlock;
	}

	err = -EPERM;
	if (sock->type == SOCK_RAW && !kern &&
	    !ns_capable(net->user_ns, CAP_NET_RAW))
		goto out_rcu_unlock;

	sock->ops = answer->ops;
	answer_prot = answer->prot;
	answer_flags = answer->flags;
	rcu_read_unlock();

	WARN_ON(!answer_prot->slab);

	err = -ENOBUFS;
	sk = sk_alloc(net, PF_INET6, GFP_KERNEL, answer_prot, kern);
	if (!sk)
		goto out;

	sock_init_data(sock, sk);

	err = 0;
	if (INET_PROTOSW_REUSE & answer_flags)
		sk->sk_reuse = SK_CAN_REUSE;

	if (INET_PROTOSW_ICSK & answer_flags)
		inet_init_csk_locks(sk);

	inet = inet_sk(sk);
	inet_assign_bit(IS_ICSK, sk, INET_PROTOSW_ICSK & answer_flags);

	if (SOCK_RAW == sock->type) {
		inet->inet_num = protocol;
		if (IPPROTO_RAW == protocol)
			inet_set_bit(HDRINCL, sk);
	}

	sk->sk_destruct		= inet6_sock_destruct;
	sk->sk_family		= PF_INET6;
	sk->sk_protocol		= protocol;

	sk->sk_backlog_rcv	= answer->prot->backlog_rcv;

	inet_sk(sk)->pinet6 = np = inet6_sk_generic(sk);
	np->hop_limit	= -1;
	np->mcast_hops	= IPV6_DEFAULT_MCASTHOPS;
	np->mc_loop	= 1;
	np->mc_all	= 1;
	np->pmtudisc	= IPV6_PMTUDISC_WANT;
	np->repflow	= net->ipv6.sysctl.flowlabel_reflect & FLOWLABEL_REFLECT_ESTABLISHED;
	sk->sk_ipv6only	= net->ipv6.sysctl.bindv6only;
	sk->sk_txrehash = READ_ONCE(net->core.sysctl_txrehash);

	/* Init the ipv4 part of the socket since we can have sockets
	 * using v6 API for ipv4.
	 */
	inet->uc_ttl	= -1;

	inet_set_bit(MC_LOOP, sk);
	inet->mc_ttl	= 1;
	inet->mc_index	= 0;
	RCU_INIT_POINTER(inet->mc_list, NULL);
	inet->rcv_tos	= 0;

	if (READ_ONCE(net->ipv4.sysctl_ip_no_pmtu_disc))
		inet->pmtudisc = IP_PMTUDISC_DONT;
	else
		inet->pmtudisc = IP_PMTUDISC_WANT;

	if (inet->inet_num) {
		/* It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically shares.
		 */
		inet->inet_sport = htons(inet->inet_num);
		err = sk->sk_prot->hash(sk);
		if (err) {
			sk_common_release(sk);
			goto out;
		}
	}
	if (sk->sk_prot->init) {
		err = sk->sk_prot->init(sk);
		if (err) {
			sk_common_release(sk);
			goto out;
		}
	}

	if (!kern) {
		err = BPF_CGROUP_RUN_PROG_INET_SOCK(sk);
		if (err) {
			sk_common_release(sk);
			goto out;
		}
	}
out:
	return err;
out_rcu_unlock:
	rcu_read_unlock();
	goto out;
}

static int __inet6_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len,
			u32 flags)
{
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *)uaddr;
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct net *net = sock_net(sk);
	__be32 v4addr = 0;
	unsigned short snum;
	bool saved_ipv6only;
	int addr_type = 0;
	int err = 0;

	if (addr->sin6_family != AF_INET6)
		return -EAFNOSUPPORT;

	addr_type = ipv6_addr_type(&addr->sin6_addr);
	if ((addr_type & IPV6_ADDR_MULTICAST) && sk->sk_type == SOCK_STREAM)
		return -EINVAL;

	snum = ntohs(addr->sin6_port);
	if (!(flags & BIND_NO_CAP_NET_BIND_SERVICE) &&
	    snum && inet_port_requires_bind_service(net, snum) &&
	    !ns_capable(net->user_ns, CAP_NET_BIND_SERVICE))
		return -EACCES;

	if (flags & BIND_WITH_LOCK)
		lock_sock(sk);

	/* Check these errors (active socket, double bind). */
	if (sk->sk_state != TCP_CLOSE || inet->inet_num) {
		err = -EINVAL;
		goto out;
	}

	/* Check if the address belongs to the host. */
	if (addr_type == IPV6_ADDR_MAPPED) {
		struct net_device *dev = NULL;
		int chk_addr_ret;

		/* Binding to v4-mapped address on a v6-only socket
		 * makes no sense
		 */
		if (ipv6_only_sock(sk)) {
			err = -EINVAL;
			goto out;
		}

		rcu_read_lock();
		if (sk->sk_bound_dev_if) {
			dev = dev_get_by_index_rcu(net, sk->sk_bound_dev_if);
			if (!dev) {
				err = -ENODEV;
				goto out_unlock;
			}
		}

		/* Reproduce AF_INET checks to make the bindings consistent */
		v4addr = addr->sin6_addr.s6_addr32[3];
		chk_addr_ret = inet_addr_type_dev_table(net, dev, v4addr);
		rcu_read_unlock();

		if (!inet_addr_valid_or_nonlocal(net, inet, v4addr,
						 chk_addr_ret)) {
			err = -EADDRNOTAVAIL;
			goto out;
		}
	} else {
		if (addr_type != IPV6_ADDR_ANY) {
			struct net_device *dev = NULL;

			rcu_read_lock();
			if (__ipv6_addr_needs_scope_id(addr_type)) {
				if (addr_len >= sizeof(struct sockaddr_in6) &&
				    addr->sin6_scope_id) {
					/* Override any existing binding, if another one
					 * is supplied by user.
					 */
					sk->sk_bound_dev_if = addr->sin6_scope_id;
				}

				/* Binding to link-local address requires an interface */
				if (!sk->sk_bound_dev_if) {
					err = -EINVAL;
					goto out_unlock;
				}
			}

			if (sk->sk_bound_dev_if) {
				dev = dev_get_by_index_rcu(net, sk->sk_bound_dev_if);
				if (!dev) {
					err = -ENODEV;
					goto out_unlock;
				}
			}

			/* ipv4 addr of the socket is invalid.  Only the
			 * unspecified and mapped address have a v4 equivalent.
			 */
			v4addr = LOOPBACK4_IPV6;
			if (!(addr_type & IPV6_ADDR_MULTICAST))	{
				if (!ipv6_can_nonlocal_bind(net, inet) &&
				    !ipv6_chk_addr(net, &addr->sin6_addr,
						   dev, 0)) {
					err = -EADDRNOTAVAIL;
					goto out_unlock;
				}
			}
			rcu_read_unlock();
		}
	}

	inet->inet_rcv_saddr = v4addr;
	inet->inet_saddr = v4addr;

	sk->sk_v6_rcv_saddr = addr->sin6_addr;

	if (!(addr_type & IPV6_ADDR_MULTICAST))
		np->saddr = addr->sin6_addr;

	saved_ipv6only = sk->sk_ipv6only;
	if (addr_type != IPV6_ADDR_ANY && addr_type != IPV6_ADDR_MAPPED)
		sk->sk_ipv6only = 1;

	/* Make sure we are allowed to bind here. */
	if (snum || !(inet_test_bit(BIND_ADDRESS_NO_PORT, sk) ||
		      (flags & BIND_FORCE_ADDRESS_NO_PORT))) {
		err = sk->sk_prot->get_port(sk, snum);
		if (err) {
			sk->sk_ipv6only = saved_ipv6only;
			inet_reset_saddr(sk);
			goto out;
		}
		if (!(flags & BIND_FROM_BPF)) {
			err = BPF_CGROUP_RUN_PROG_INET6_POST_BIND(sk);
			if (err) {
				sk->sk_ipv6only = saved_ipv6only;
				inet_reset_saddr(sk);
				if (sk->sk_prot->put_port)
					sk->sk_prot->put_port(sk);
				goto out;
			}
		}
	}

	if (addr_type != IPV6_ADDR_ANY)
		sk->sk_userlocks |= SOCK_BINDADDR_LOCK;
	if (snum)
		sk->sk_userlocks |= SOCK_BINDPORT_LOCK;
	inet->inet_sport = htons(inet->inet_num);
	inet->inet_dport = 0;
	inet->inet_daddr = 0;
out:
	if (flags & BIND_WITH_LOCK)
		release_sock(sk);
	return err;
out_unlock:
	rcu_read_unlock();
	goto out;
}

int inet6_bind_sk(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	u32 flags = BIND_WITH_LOCK;
	const struct proto *prot;
	int err = 0;

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	prot = READ_ONCE(sk->sk_prot);
	/* If the socket has its own bind function then use it. */
	if (prot->bind)
		return prot->bind(sk, uaddr, addr_len);

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	/* BPF prog is run before any checks are done so that if the prog
	 * changes context in a wrong way it will be caught.
	 */
	err = BPF_CGROUP_RUN_PROG_INET_BIND_LOCK(sk, uaddr, &addr_len,
						 CGROUP_INET6_BIND, &flags);
	if (err)
		return err;

	return __inet6_bind(sk, uaddr, addr_len, flags);
}

/* bind for INET6 API */
int inet6_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	return inet6_bind_sk(sock->sk, uaddr, addr_len);
}
EXPORT_SYMBOL(inet6_bind);

int inet6_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (!sk)
		return -EINVAL;

	/* Free mc lists */
	ipv6_sock_mc_close(sk);

	/* Free ac lists */
	ipv6_sock_ac_close(sk);

	return inet_release(sock);
}
EXPORT_SYMBOL(inet6_release);

void inet6_cleanup_sock(struct sock *sk)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *skb;
	struct ipv6_txoptions *opt;

	/* Release rx options */

	skb = xchg(&np->pktoptions, NULL);
	kfree_skb(skb);

	skb = xchg(&np->rxpmtu, NULL);
	kfree_skb(skb);

	/* Free flowlabels */
	fl6_free_socklist(sk);

	/* Free tx options */

	opt = xchg((__force struct ipv6_txoptions **)&np->opt, NULL);
	if (opt) {
		atomic_sub(opt->tot_len, &sk->sk_omem_alloc);
		txopt_put(opt);
	}
}
EXPORT_SYMBOL_GPL(inet6_cleanup_sock);

/*
 *	This does both peername and sockname.
 */
int inet6_getname(struct socket *sock, struct sockaddr *uaddr,
		  int peer)
{
	struct sockaddr_in6 *sin = (struct sockaddr_in6 *)uaddr;
	int sin_addr_len = sizeof(*sin);
	struct sock *sk = sock->sk;
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);

	sin->sin6_family = AF_INET6;
	sin->sin6_flowinfo = 0;
	sin->sin6_scope_id = 0;
	lock_sock(sk);
	if (peer) {
		if (!inet->inet_dport ||
		    (((1 << sk->sk_state) & (TCPF_CLOSE | TCPF_SYN_SENT)) &&
		    peer == 1)) {
			release_sock(sk);
			return -ENOTCONN;
		}
		sin->sin6_port = inet->inet_dport;
		sin->sin6_addr = sk->sk_v6_daddr;
		if (np->sndflow)
			sin->sin6_flowinfo = np->flow_label;
		BPF_CGROUP_RUN_SA_PROG(sk, (struct sockaddr *)sin, &sin_addr_len,
				       CGROUP_INET6_GETPEERNAME);
	} else {
		if (ipv6_addr_any(&sk->sk_v6_rcv_saddr))
			sin->sin6_addr = np->saddr;
		else
			sin->sin6_addr = sk->sk_v6_rcv_saddr;
		sin->sin6_port = inet->inet_sport;
		BPF_CGROUP_RUN_SA_PROG(sk, (struct sockaddr *)sin, &sin_addr_len,
				       CGROUP_INET6_GETSOCKNAME);
	}
	sin->sin6_scope_id = ipv6_iface_scope_id(&sin->sin6_addr,
						 sk->sk_bound_dev_if);
	release_sock(sk);
	return sin_addr_len;
}
EXPORT_SYMBOL(inet6_getname);

int inet6_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);
	const struct proto *prot;

	switch (cmd) {
	case SIOCADDRT:
	case SIOCDELRT: {
		struct in6_rtmsg rtmsg;

		if (copy_from_user(&rtmsg, argp, sizeof(rtmsg)))
			return -EFAULT;
		return ipv6_route_ioctl(net, cmd, &rtmsg);
	}
	case SIOCSIFADDR:
		return addrconf_add_ifaddr(net, argp);
	case SIOCDIFADDR:
		return addrconf_del_ifaddr(net, argp);
	case SIOCSIFDSTADDR:
		return addrconf_set_dstaddr(net, argp);
	default:
		/* IPV6_ADDRFORM can change sk->sk_prot under us. */
		prot = READ_ONCE(sk->sk_prot);
		if (!prot->ioctl)
			return -ENOIOCTLCMD;
		return sk_ioctl(sk, cmd, (void __user *)arg);
	}
	/*NOTREACHED*/
	return 0;
}
EXPORT_SYMBOL(inet6_ioctl);

#ifdef CONFIG_COMPAT
struct compat_in6_rtmsg {
	struct in6_addr		rtmsg_dst;
	struct in6_addr		rtmsg_src;
	struct in6_addr		rtmsg_gateway;
	u32			rtmsg_type;
	u16			rtmsg_dst_len;
	u16			rtmsg_src_len;
	u32			rtmsg_metric;
	u32			rtmsg_info;
	u32			rtmsg_flags;
	s32			rtmsg_ifindex;
};

static int inet6_compat_routing_ioctl(struct sock *sk, unsigned int cmd,
		struct compat_in6_rtmsg __user *ur)
{
	struct in6_rtmsg rt;

	if (copy_from_user(&rt.rtmsg_dst, &ur->rtmsg_dst,
			3 * sizeof(struct in6_addr)) ||
	    get_user(rt.rtmsg_type, &ur->rtmsg_type) ||
	    get_user(rt.rtmsg_dst_len, &ur->rtmsg_dst_len) ||
	    get_user(rt.rtmsg_src_len, &ur->rtmsg_src_len) ||
	    get_user(rt.rtmsg_metric, &ur->rtmsg_metric) ||
	    get_user(rt.rtmsg_info, &ur->rtmsg_info) ||
	    get_user(rt.rtmsg_flags, &ur->rtmsg_flags) ||
	    get_user(rt.rtmsg_ifindex, &ur->rtmsg_ifindex))
		return -EFAULT;


	return ipv6_route_ioctl(sock_net(sk), cmd, &rt);
}

int inet6_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	void __user *argp = compat_ptr(arg);
	struct sock *sk = sock->sk;

	switch (cmd) {
	case SIOCADDRT:
	case SIOCDELRT:
		return inet6_compat_routing_ioctl(sk, cmd, argp);
	default:
		return -ENOIOCTLCMD;
	}
}
EXPORT_SYMBOL_GPL(inet6_compat_ioctl);
#endif /* CONFIG_COMPAT */

INDIRECT_CALLABLE_DECLARE(int udpv6_sendmsg(struct sock *, struct msghdr *,
					    size_t));
int inet6_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;
	const struct proto *prot;

	if (unlikely(inet_send_prepare(sk)))
		return -EAGAIN;

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	prot = READ_ONCE(sk->sk_prot);
	return INDIRECT_CALL_2(prot->sendmsg, tcp_sendmsg, udpv6_sendmsg,
			       sk, msg, size);
}

INDIRECT_CALLABLE_DECLARE(int udpv6_recvmsg(struct sock *, struct msghdr *,
					    size_t, int, int *));
int inet6_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		  int flags)
{
	struct sock *sk = sock->sk;
	const struct proto *prot;
	int addr_len = 0;
	int err;

	if (likely(!(flags & MSG_ERRQUEUE)))
		sock_rps_record_flow(sk);

	/* IPV6_ADDRFORM can change sk->sk_prot under us. */
	prot = READ_ONCE(sk->sk_prot);
	err = INDIRECT_CALL_2(prot->recvmsg, tcp_recvmsg, udpv6_recvmsg,
			      sk, msg, size, flags, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}

const struct proto_ops inet6_stream_ops = {
	.family		   = PF_INET6,
	.owner		   = THIS_MODULE,
	.release	   = inet6_release,
	.bind		   = inet6_bind,
	.connect	   = inet_stream_connect,	/* ok		*/
	.socketpair	   = sock_no_socketpair,	/* a do nothing	*/
	.accept		   = inet_accept,		/* ok		*/
	.getname	   = inet6_getname,
	.poll		   = tcp_poll,			/* ok		*/
	.ioctl		   = inet6_ioctl,		/* must change  */
	.gettstamp	   = sock_gettstamp,
	.listen		   = inet_listen,		/* ok		*/
	.shutdown	   = inet_shutdown,		/* ok		*/
	.setsockopt	   = sock_common_setsockopt,	/* ok		*/
	.getsockopt	   = sock_common_getsockopt,	/* ok		*/
	.sendmsg	   = inet6_sendmsg,		/* retpoline's sake */
	.recvmsg	   = inet6_recvmsg,		/* retpoline's sake */
#ifdef CONFIG_MMU
	.mmap		   = tcp_mmap,
#endif
	.splice_eof	   = inet_splice_eof,
	.sendmsg_locked    = tcp_sendmsg_locked,
	.splice_read	   = tcp_splice_read,
	.read_sock	   = tcp_read_sock,
	.read_skb	   = tcp_read_skb,
	.peek_len	   = tcp_peek_len,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = inet6_compat_ioctl,
#endif
	.set_rcvlowat	   = tcp_set_rcvlowat,
};

const struct proto_ops inet6_dgram_ops = {
	.family		   = PF_INET6,
	.owner		   = THIS_MODULE,
	.release	   = inet6_release,
	.bind		   = inet6_bind,
	.connect	   = inet_dgram_connect,	/* ok		*/
	.socketpair	   = sock_no_socketpair,	/* a do nothing	*/
	.accept		   = sock_no_accept,		/* a do nothing	*/
	.getname	   = inet6_getname,
	.poll		   = udp_poll,			/* ok		*/
	.ioctl		   = inet6_ioctl,		/* must change  */
	.gettstamp	   = sock_gettstamp,
	.listen		   = sock_no_listen,		/* ok		*/
	.shutdown	   = inet_shutdown,		/* ok		*/
	.setsockopt	   = sock_common_setsockopt,	/* ok		*/
	.getsockopt	   = sock_common_getsockopt,	/* ok		*/
	.sendmsg	   = inet6_sendmsg,		/* retpoline's sake */
	.recvmsg	   = inet6_recvmsg,		/* retpoline's sake */
	.read_skb	   = udp_read_skb,
	.mmap		   = sock_no_mmap,
	.set_peek_off	   = sk_set_peek_off,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = inet6_compat_ioctl,
#endif
};

static const struct net_proto_family inet6_family_ops = {
	.family = PF_INET6,
	.create = inet6_create,
	.owner	= THIS_MODULE,
};

int inet6_register_protosw(struct inet_protosw *p)
{
	struct list_head *lh;
	struct inet_protosw *answer;
	struct list_head *last_perm;
	int protocol = p->protocol;
	int ret;

	spin_lock_bh(&inetsw6_lock);

	ret = -EINVAL;
	if (p->type >= SOCK_MAX)
		goto out_illegal;

	/* If we are trying to override a permanent protocol, bail. */
	answer = NULL;
	ret = -EPERM;
	last_perm = &inetsw6[p->type];
	list_for_each(lh, &inetsw6[p->type]) {
		answer = list_entry(lh, struct inet_protosw, list);

		/* Check only the non-wild match. */
		if (INET_PROTOSW_PERMANENT & answer->flags) {
			if (protocol == answer->protocol)
				break;
			last_perm = lh;
		}

		answer = NULL;
	}
	if (answer)
		goto out_permanent;

	/* Add the new entry after the last permanent entry if any, so that
	 * the new entry does not override a permanent entry when matched with
	 * a wild-card protocol. But it is allowed to override any existing
	 * non-permanent entry.  This means that when we remove this entry, the
	 * system automatically returns to the old behavior.
	 */
	list_add_rcu(&p->list, last_perm);
	ret = 0;
out:
	spin_unlock_bh(&inetsw6_lock);
	return ret;

out_permanent:
	pr_err("Attempt to override permanent protocol %d\n", protocol);
	goto out;

out_illegal:
	pr_err("Ignoring attempt to register invalid socket type %d\n",
	       p->type);
	goto out;
}
EXPORT_SYMBOL(inet6_register_protosw);

void
inet6_unregister_protosw(struct inet_protosw *p)
{
	if (INET_PROTOSW_PERMANENT & p->flags) {
		pr_err("Attempt to unregister permanent protocol %d\n",
		       p->protocol);
	} else {
		spin_lock_bh(&inetsw6_lock);
		list_del_rcu(&p->list);
		spin_unlock_bh(&inetsw6_lock);

		synchronize_net();
	}
}
EXPORT_SYMBOL(inet6_unregister_protosw);

int inet6_sk_rebuild_header(struct sock *sk)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct dst_entry *dst;

	dst = __sk_dst_check(sk, np->dst_cookie);

	if (!dst) {
		struct inet_sock *inet = inet_sk(sk);
		struct in6_addr *final_p, final;
		struct flowi6 fl6;

		memset(&fl6, 0, sizeof(fl6));
		fl6.flowi6_proto = sk->sk_protocol;
		fl6.daddr = sk->sk_v6_daddr;
		fl6.saddr = np->saddr;
		fl6.flowlabel = np->flow_label;
		fl6.flowi6_oif = sk->sk_bound_dev_if;
		fl6.flowi6_mark = sk->sk_mark;
		fl6.fl6_dport = inet->inet_dport;
		fl6.fl6_sport = inet->inet_sport;
		fl6.flowi6_uid = sk->sk_uid;
		security_sk_classify_flow(sk, flowi6_to_flowi_common(&fl6));

		rcu_read_lock();
		final_p = fl6_update_dst(&fl6, rcu_dereference(np->opt),
					 &final);
		rcu_read_unlock();

		dst = ip6_dst_lookup_flow(sock_net(sk), sk, &fl6, final_p);
		if (IS_ERR(dst)) {
			sk->sk_route_caps = 0;
			WRITE_ONCE(sk->sk_err_soft, -PTR_ERR(dst));
			return PTR_ERR(dst);
		}

		ip6_dst_store(sk, dst, NULL, NULL);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(inet6_sk_rebuild_header);

bool ipv6_opt_accepted(const struct sock *sk, const struct sk_buff *skb,
		       const struct inet6_skb_parm *opt)
{
	const struct ipv6_pinfo *np = inet6_sk(sk);

	if (np->rxopt.all) {
		if (((opt->flags & IP6SKB_HOPBYHOP) &&
		     (np->rxopt.bits.hopopts || np->rxopt.bits.ohopopts)) ||
		    (ip6_flowinfo((struct ipv6hdr *) skb_network_header(skb)) &&
		     np->rxopt.bits.rxflow) ||
		    (opt->srcrt && (np->rxopt.bits.srcrt ||
		     np->rxopt.bits.osrcrt)) ||
		    ((opt->dst1 || opt->dst0) &&
		     (np->rxopt.bits.dstopts || np->rxopt.bits.odstopts)))
			return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(ipv6_opt_accepted);

static struct packet_type ipv6_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_IPV6),
	.func = ipv6_rcv,
	.list_func = ipv6_list_rcv,
};

static int __init ipv6_packet_init(void)
{
	dev_add_pack(&ipv6_packet_type);
	return 0;
}

static void ipv6_packet_cleanup(void)
{
	dev_remove_pack(&ipv6_packet_type);
}

static int __net_init ipv6_init_mibs(struct net *net)
{
	int i;

	net->mib.udp_stats_in6 = alloc_percpu(struct udp_mib);
	if (!net->mib.udp_stats_in6)
		return -ENOMEM;
	net->mib.udplite_stats_in6 = alloc_percpu(struct udp_mib);
	if (!net->mib.udplite_stats_in6)
		goto err_udplite_mib;
	net->mib.ipv6_statistics = alloc_percpu(struct ipstats_mib);
	if (!net->mib.ipv6_statistics)
		goto err_ip_mib;

	for_each_possible_cpu(i) {
		struct ipstats_mib *af_inet6_stats;
		af_inet6_stats = per_cpu_ptr(net->mib.ipv6_statistics, i);
		u64_stats_init(&af_inet6_stats->syncp);
	}


	net->mib.icmpv6_statistics = alloc_percpu(struct icmpv6_mib);
	if (!net->mib.icmpv6_statistics)
		goto err_icmp_mib;
	net->mib.icmpv6msg_statistics = kzalloc(sizeof(struct icmpv6msg_mib),
						GFP_KERNEL);
	if (!net->mib.icmpv6msg_statistics)
		goto err_icmpmsg_mib;
	return 0;

err_icmpmsg_mib:
	free_percpu(net->mib.icmpv6_statistics);
err_icmp_mib:
	free_percpu(net->mib.ipv6_statistics);
err_ip_mib:
	free_percpu(net->mib.udplite_stats_in6);
err_udplite_mib:
	free_percpu(net->mib.udp_stats_in6);
	return -ENOMEM;
}

static void ipv6_cleanup_mibs(struct net *net)
{
	free_percpu(net->mib.udp_stats_in6);
	free_percpu(net->mib.udplite_stats_in6);
	free_percpu(net->mib.ipv6_statistics);
	free_percpu(net->mib.icmpv6_statistics);
	kfree(net->mib.icmpv6msg_statistics);
}

static int __net_init inet6_net_init(struct net *net)
{
	int err = 0;

	net->ipv6.sysctl.bindv6only = 0;
	net->ipv6.sysctl.icmpv6_time = 1*HZ;
	net->ipv6.sysctl.icmpv6_echo_ignore_all = 0;
	net->ipv6.sysctl.icmpv6_echo_ignore_multicast = 0;
	net->ipv6.sysctl.icmpv6_echo_ignore_anycast = 0;
	net->ipv6.sysctl.icmpv6_error_anycast_as_unicast = 0;

	/* By default, rate limit error messages.
	 * Except for pmtu discovery, it would break it.
	 * proc_do_large_bitmap needs pointer to the bitmap.
	 */
	bitmap_set(net->ipv6.sysctl.icmpv6_ratemask, 0, ICMPV6_ERRMSG_MAX + 1);
	bitmap_clear(net->ipv6.sysctl.icmpv6_ratemask, ICMPV6_PKT_TOOBIG, 1);
	net->ipv6.sysctl.icmpv6_ratemask_ptr = net->ipv6.sysctl.icmpv6_ratemask;

	net->ipv6.sysctl.flowlabel_consistency = 1;
	net->ipv6.sysctl.auto_flowlabels = IP6_DEFAULT_AUTO_FLOW_LABELS;
	net->ipv6.sysctl.idgen_retries = 3;
	net->ipv6.sysctl.idgen_delay = 1 * HZ;
	net->ipv6.sysctl.flowlabel_state_ranges = 0;
	net->ipv6.sysctl.max_dst_opts_cnt = IP6_DEFAULT_MAX_DST_OPTS_CNT;
	net->ipv6.sysctl.max_hbh_opts_cnt = IP6_DEFAULT_MAX_HBH_OPTS_CNT;
	net->ipv6.sysctl.max_dst_opts_len = IP6_DEFAULT_MAX_DST_OPTS_LEN;
	net->ipv6.sysctl.max_hbh_opts_len = IP6_DEFAULT_MAX_HBH_OPTS_LEN;
	net->ipv6.sysctl.fib_notify_on_flag_change = 0;
	atomic_set(&net->ipv6.fib6_sernum, 1);

	net->ipv6.sysctl.ioam6_id = IOAM6_DEFAULT_ID;
	net->ipv6.sysctl.ioam6_id_wide = IOAM6_DEFAULT_ID_WIDE;

	err = ipv6_init_mibs(net);
	if (err)
		return err;
#ifdef CONFIG_PROC_FS
	err = udp6_proc_init(net);
	if (err)
		goto out;
	err = tcp6_proc_init(net);
	if (err)
		goto proc_tcp6_fail;
	err = ac6_proc_init(net);
	if (err)
		goto proc_ac6_fail;
#endif
	return err;

#ifdef CONFIG_PROC_FS
proc_ac6_fail:
	tcp6_proc_exit(net);
proc_tcp6_fail:
	udp6_proc_exit(net);
out:
	ipv6_cleanup_mibs(net);
	return err;
#endif
}

static void __net_exit inet6_net_exit(struct net *net)
{
#ifdef CONFIG_PROC_FS
	udp6_proc_exit(net);
	tcp6_proc_exit(net);
	ac6_proc_exit(net);
#endif
	ipv6_cleanup_mibs(net);
}

static struct pernet_operations inet6_net_ops = {
	.init = inet6_net_init,
	.exit = inet6_net_exit,
};

static int ipv6_route_input(struct sk_buff *skb)
{
	ip6_route_input(skb);
	return skb_dst(skb)->error;
}

static const struct ipv6_stub ipv6_stub_impl = {
	.ipv6_sock_mc_join = ipv6_sock_mc_join,
	.ipv6_sock_mc_drop = ipv6_sock_mc_drop,
	.ipv6_dst_lookup_flow = ip6_dst_lookup_flow,
	.ipv6_route_input  = ipv6_route_input,
	.fib6_get_table	   = fib6_get_table,
	.fib6_table_lookup = fib6_table_lookup,
	.fib6_lookup       = fib6_lookup,
	.fib6_select_path  = fib6_select_path,
	.ip6_mtu_from_fib6 = ip6_mtu_from_fib6,
	.fib6_nh_init	   = fib6_nh_init,
	.fib6_nh_release   = fib6_nh_release,
	.fib6_nh_release_dsts = fib6_nh_release_dsts,
	.fib6_update_sernum = fib6_update_sernum_stub,
	.fib6_rt_update	   = fib6_rt_update,
	.ip6_del_rt	   = ip6_del_rt,
	.udpv6_encap_enable = udpv6_encap_enable,
	.ndisc_send_na = ndisc_send_na,
#if IS_ENABLED(CONFIG_XFRM)
	.xfrm6_local_rxpmtu = xfrm6_local_rxpmtu,
	.xfrm6_udp_encap_rcv = xfrm6_udp_encap_rcv,
	.xfrm6_rcv_encap = xfrm6_rcv_encap,
#endif
	.nd_tbl	= &nd_tbl,
	.ipv6_fragment = ip6_fragment,
	.ipv6_dev_find = ipv6_dev_find,
};

static const struct ipv6_bpf_stub ipv6_bpf_stub_impl = {
	.inet6_bind = __inet6_bind,
	.udp6_lib_lookup = __udp6_lib_lookup,
	.ipv6_setsockopt = do_ipv6_setsockopt,
	.ipv6_getsockopt = do_ipv6_getsockopt,
	.ipv6_dev_get_saddr = ipv6_dev_get_saddr,
};

static int __init inet6_init(void)
{
	struct list_head *r;
	int err = 0;

	sock_skb_cb_check_size(sizeof(struct inet6_skb_parm));

	/* Register the socket-side information for inet6_create.  */
	for (r = &inetsw6[0]; r < &inetsw6[SOCK_MAX]; ++r)
		INIT_LIST_HEAD(r);

	raw_hashinfo_init(&raw_v6_hashinfo);

	if (disable_ipv6_mod) {
		pr_info("Loaded, but administratively disabled, reboot required to enable\n");
		goto out;
	}

	err = proto_register(&tcpv6_prot, 1);
	if (err)
		goto out;

	err = proto_register(&udpv6_prot, 1);
	if (err)
		goto out_unregister_tcp_proto;

	err = proto_register(&udplitev6_prot, 1);
	if (err)
		goto out_unregister_udp_proto;

	err = proto_register(&rawv6_prot, 1);
	if (err)
		goto out_unregister_udplite_proto;

	err = proto_register(&pingv6_prot, 1);
	if (err)
		goto out_unregister_raw_proto;

	/* We MUST register RAW sockets before we create the ICMP6,
	 * IGMP6, or NDISC control sockets.
	 */
	err = rawv6_init();
	if (err)
		goto out_unregister_ping_proto;

	/* Register the family here so that the init calls below will
	 * be able to create sockets. (?? is this dangerous ??)
	 */
	err = sock_register(&inet6_family_ops);
	if (err)
		goto out_sock_register_fail;

	/*
	 *	ipngwg API draft makes clear that the correct semantics
	 *	for TCP and UDP is to consider one TCP and UDP instance
	 *	in a host available by both INET and INET6 APIs and
	 *	able to communicate via both network protocols.
	 */

	err = register_pernet_subsys(&inet6_net_ops);
	if (err)
		goto register_pernet_fail;
	err = ip6_mr_init();
	if (err)
		goto ipmr_fail;
	err = icmpv6_init();
	if (err)
		goto icmp_fail;
	err = ndisc_init();
	if (err)
		goto ndisc_fail;
	err = igmp6_init();
	if (err)
		goto igmp_fail;

	err = ipv6_netfilter_init();
	if (err)
		goto netfilter_fail;
	/* Create /proc/foo6 entries. */
#ifdef CONFIG_PROC_FS
	err = -ENOMEM;
	if (raw6_proc_init())
		goto proc_raw6_fail;
	if (udplite6_proc_init())
		goto proc_udplite6_fail;
	if (ipv6_misc_proc_init())
		goto proc_misc6_fail;
	if (if6_proc_init())
		goto proc_if6_fail;
#endif
	err = ip6_route_init();
	if (err)
		goto ip6_route_fail;
	err = ndisc_late_init();
	if (err)
		goto ndisc_late_fail;
	err = ip6_flowlabel_init();
	if (err)
		goto ip6_flowlabel_fail;
	err = ipv6_anycast_init();
	if (err)
		goto ipv6_anycast_fail;
	err = addrconf_init();
	if (err)
		goto addrconf_fail;

	/* Init v6 extension headers. */
	err = ipv6_exthdrs_init();
	if (err)
		goto ipv6_exthdrs_fail;

	err = ipv6_frag_init();
	if (err)
		goto ipv6_frag_fail;

	/* Init v6 transport protocols. */
	err = udpv6_init();
	if (err)
		goto udpv6_fail;

	err = udplitev6_init();
	if (err)
		goto udplitev6_fail;

	err = udpv6_offload_init();
	if (err)
		goto udpv6_offload_fail;

	err = tcpv6_init();
	if (err)
		goto tcpv6_fail;

	err = ipv6_packet_init();
	if (err)
		goto ipv6_packet_fail;

	err = pingv6_init();
	if (err)
		goto pingv6_fail;

	err = calipso_init();
	if (err)
		goto calipso_fail;

	err = seg6_init();
	if (err)
		goto seg6_fail;

	err = rpl_init();
	if (err)
		goto rpl_fail;

	err = ioam6_init();
	if (err)
		goto ioam6_fail;

	err = igmp6_late_init();
	if (err)
		goto igmp6_late_err;

#ifdef CONFIG_SYSCTL
	err = ipv6_sysctl_register();
	if (err)
		goto sysctl_fail;
#endif

	/* ensure that ipv6 stubs are visible only after ipv6 is ready */
	wmb();
	ipv6_stub = &ipv6_stub_impl;
	ipv6_bpf_stub = &ipv6_bpf_stub_impl;
out:
	return err;

#ifdef CONFIG_SYSCTL
sysctl_fail:
	igmp6_late_cleanup();
#endif
igmp6_late_err:
	ioam6_exit();
ioam6_fail:
	rpl_exit();
rpl_fail:
	seg6_exit();
seg6_fail:
	calipso_exit();
calipso_fail:
	pingv6_exit();
pingv6_fail:
	ipv6_packet_cleanup();
ipv6_packet_fail:
	tcpv6_exit();
tcpv6_fail:
	udpv6_offload_exit();
udpv6_offload_fail:
	udplitev6_exit();
udplitev6_fail:
	udpv6_exit();
udpv6_fail:
	ipv6_frag_exit();
ipv6_frag_fail:
	ipv6_exthdrs_exit();
ipv6_exthdrs_fail:
	addrconf_cleanup();
addrconf_fail:
	ipv6_anycast_cleanup();
ipv6_anycast_fail:
	ip6_flowlabel_cleanup();
ip6_flowlabel_fail:
	ndisc_late_cleanup();
ndisc_late_fail:
	ip6_route_cleanup();
ip6_route_fail:
#ifdef CONFIG_PROC_FS
	if6_proc_exit();
proc_if6_fail:
	ipv6_misc_proc_exit();
proc_misc6_fail:
	udplite6_proc_exit();
proc_udplite6_fail:
	raw6_proc_exit();
proc_raw6_fail:
#endif
	ipv6_netfilter_fini();
netfilter_fail:
	igmp6_cleanup();
igmp_fail:
	ndisc_cleanup();
ndisc_fail:
	icmpv6_cleanup();
icmp_fail:
	ip6_mr_cleanup();
ipmr_fail:
	unregister_pernet_subsys(&inet6_net_ops);
register_pernet_fail:
	sock_unregister(PF_INET6);
	rtnl_unregister_all(PF_INET6);
out_sock_register_fail:
	rawv6_exit();
out_unregister_ping_proto:
	proto_unregister(&pingv6_prot);
out_unregister_raw_proto:
	proto_unregister(&rawv6_prot);
out_unregister_udplite_proto:
	proto_unregister(&udplitev6_prot);
out_unregister_udp_proto:
	proto_unregister(&udpv6_prot);
out_unregister_tcp_proto:
	proto_unregister(&tcpv6_prot);
	goto out;
}
module_init(inet6_init);

MODULE_ALIAS_NETPROTO(PF_INET6);
