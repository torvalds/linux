// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		"Ping" sockets
 *
 * Based on ipv4/udp.c code.
 *
 * Authors:	Vasiliy Kulikov / Openwall (for Linux 2.6),
 *		Pavel Kankovsky (for Linux 2.4.32)
 *
 * Pavel gave all rights to bugs to Vasiliy,
 * none of the bugs are Pavel's now.
 */

#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/export.h>
#include <linux/bpf-cgroup.h>
#include <net/sock.h>
#include <net/ping.h>
#include <net/udp.h>
#include <net/route.h>
#include <net/inet_common.h>
#include <net/checksum.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>
#endif

#define ping_portaddr_for_each_entry(__sk, node, list) \
	hlist_nulls_for_each_entry(__sk, node, list, sk_nulls_node)
#define ping_portaddr_for_each_entry_rcu(__sk, node, list) \
	hlist_nulls_for_each_entry_rcu(__sk, node, list, sk_nulls_node)

struct ping_table {
	struct hlist_nulls_head	hash[PING_HTABLE_SIZE];
	spinlock_t		lock;
};

static struct ping_table ping_table;
struct pingv6_ops pingv6_ops;
EXPORT_SYMBOL_GPL(pingv6_ops);

static u16 ping_port_rover;

static inline u32 ping_hashfn(const struct net *net, u32 num, u32 mask)
{
	u32 res = (num + net_hash_mix(net)) & mask;

	pr_debug("hash(%u) = %u\n", num, res);
	return res;
}
EXPORT_SYMBOL_GPL(ping_hash);

static inline struct hlist_nulls_head *ping_hashslot(struct ping_table *table,
					     struct net *net, unsigned int num)
{
	return &table->hash[ping_hashfn(net, num, PING_HTABLE_MASK)];
}

int ping_get_port(struct sock *sk, unsigned short ident)
{
	struct hlist_nulls_node *node;
	struct hlist_nulls_head *hlist;
	struct inet_sock *isk, *isk2;
	struct sock *sk2 = NULL;

	isk = inet_sk(sk);
	spin_lock(&ping_table.lock);
	if (ident == 0) {
		u32 i;
		u16 result = ping_port_rover + 1;

		for (i = 0; i < (1L << 16); i++, result++) {
			if (!result)
				result++; /* avoid zero */
			hlist = ping_hashslot(&ping_table, sock_net(sk),
					    result);
			ping_portaddr_for_each_entry(sk2, node, hlist) {
				isk2 = inet_sk(sk2);

				if (isk2->inet_num == result)
					goto next_port;
			}

			/* found */
			ping_port_rover = ident = result;
			break;
next_port:
			;
		}
		if (i >= (1L << 16))
			goto fail;
	} else {
		hlist = ping_hashslot(&ping_table, sock_net(sk), ident);
		ping_portaddr_for_each_entry(sk2, node, hlist) {
			isk2 = inet_sk(sk2);

			/* BUG? Why is this reuse and not reuseaddr? ping.c
			 * doesn't turn off SO_REUSEADDR, and it doesn't expect
			 * that other ping processes can steal its packets.
			 */
			if ((isk2->inet_num == ident) &&
			    (sk2 != sk) &&
			    (!sk2->sk_reuse || !sk->sk_reuse))
				goto fail;
		}
	}

	pr_debug("found port/ident = %d\n", ident);
	isk->inet_num = ident;
	if (sk_unhashed(sk)) {
		pr_debug("was not hashed\n");
		sock_hold(sk);
		sock_set_flag(sk, SOCK_RCU_FREE);
		hlist_nulls_add_head_rcu(&sk->sk_nulls_node, hlist);
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	}
	spin_unlock(&ping_table.lock);
	return 0;

fail:
	spin_unlock(&ping_table.lock);
	return -EADDRINUSE;
}
EXPORT_SYMBOL_GPL(ping_get_port);

int ping_hash(struct sock *sk)
{
	pr_debug("ping_hash(sk->port=%u)\n", inet_sk(sk)->inet_num);
	BUG(); /* "Please do not press this button again." */

	return 0;
}

void ping_unhash(struct sock *sk)
{
	struct inet_sock *isk = inet_sk(sk);

	pr_debug("ping_unhash(isk=%p,isk->num=%u)\n", isk, isk->inet_num);
	spin_lock(&ping_table.lock);
	if (sk_hashed(sk)) {
		hlist_nulls_del_init_rcu(&sk->sk_nulls_node);
		sock_put(sk);
		isk->inet_num = 0;
		isk->inet_sport = 0;
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	}
	spin_unlock(&ping_table.lock);
}
EXPORT_SYMBOL_GPL(ping_unhash);

/* Called under rcu_read_lock() */
static struct sock *ping_lookup(struct net *net, struct sk_buff *skb, u16 ident)
{
	struct hlist_nulls_head *hslot = ping_hashslot(&ping_table, net, ident);
	struct sock *sk = NULL;
	struct inet_sock *isk;
	struct hlist_nulls_node *hnode;
	int dif, sdif;

	if (skb->protocol == htons(ETH_P_IP)) {
		dif = inet_iif(skb);
		sdif = inet_sdif(skb);
		pr_debug("try to find: num = %d, daddr = %pI4, dif = %d\n",
			 (int)ident, &ip_hdr(skb)->daddr, dif);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		dif = inet6_iif(skb);
		sdif = inet6_sdif(skb);
		pr_debug("try to find: num = %d, daddr = %pI6c, dif = %d\n",
			 (int)ident, &ipv6_hdr(skb)->daddr, dif);
#endif
	} else {
		return NULL;
	}

	ping_portaddr_for_each_entry_rcu(sk, hnode, hslot) {
		isk = inet_sk(sk);

		pr_debug("iterate\n");
		if (isk->inet_num != ident)
			continue;

		if (skb->protocol == htons(ETH_P_IP) &&
		    sk->sk_family == AF_INET) {
			pr_debug("found: %p: num=%d, daddr=%pI4, dif=%d\n", sk,
				 (int) isk->inet_num, &isk->inet_rcv_saddr,
				 sk->sk_bound_dev_if);

			if (isk->inet_rcv_saddr &&
			    isk->inet_rcv_saddr != ip_hdr(skb)->daddr)
				continue;
#if IS_ENABLED(CONFIG_IPV6)
		} else if (skb->protocol == htons(ETH_P_IPV6) &&
			   sk->sk_family == AF_INET6) {

			pr_debug("found: %p: num=%d, daddr=%pI6c, dif=%d\n", sk,
				 (int) isk->inet_num,
				 &sk->sk_v6_rcv_saddr,
				 sk->sk_bound_dev_if);

			if (!ipv6_addr_any(&sk->sk_v6_rcv_saddr) &&
			    !ipv6_addr_equal(&sk->sk_v6_rcv_saddr,
					     &ipv6_hdr(skb)->daddr))
				continue;
#endif
		} else {
			continue;
		}

		if (sk->sk_bound_dev_if && sk->sk_bound_dev_if != dif &&
		    sk->sk_bound_dev_if != sdif)
			continue;

		goto exit;
	}

	sk = NULL;
exit:

	return sk;
}

static void inet_get_ping_group_range_net(struct net *net, kgid_t *low,
					  kgid_t *high)
{
	kgid_t *data = net->ipv4.ping_group_range.range;
	unsigned int seq;

	do {
		seq = read_seqbegin(&net->ipv4.ping_group_range.lock);

		*low = data[0];
		*high = data[1];
	} while (read_seqretry(&net->ipv4.ping_group_range.lock, seq));
}


int ping_init_sock(struct sock *sk)
{
	struct net *net = sock_net(sk);
	kgid_t group = current_egid();
	struct group_info *group_info;
	int i;
	kgid_t low, high;
	int ret = 0;

	if (sk->sk_family == AF_INET6)
		sk->sk_ipv6only = 1;

	inet_get_ping_group_range_net(net, &low, &high);
	if (gid_lte(low, group) && gid_lte(group, high))
		return 0;

	group_info = get_current_groups();
	for (i = 0; i < group_info->ngroups; i++) {
		kgid_t gid = group_info->gid[i];

		if (gid_lte(low, gid) && gid_lte(gid, high))
			goto out_release_group;
	}

	ret = -EACCES;

out_release_group:
	put_group_info(group_info);
	return ret;
}
EXPORT_SYMBOL_GPL(ping_init_sock);

void ping_close(struct sock *sk, long timeout)
{
	pr_debug("ping_close(sk=%p,sk->num=%u)\n",
		 inet_sk(sk), inet_sk(sk)->inet_num);
	pr_debug("isk->refcnt = %d\n", refcount_read(&sk->sk_refcnt));

	sk_common_release(sk);
}
EXPORT_SYMBOL_GPL(ping_close);

static int ping_pre_connect(struct sock *sk, struct sockaddr *uaddr,
			    int addr_len)
{
	/* This check is replicated from __ip4_datagram_connect() and
	 * intended to prevent BPF program called below from accessing bytes
	 * that are out of the bound specified by user in addr_len.
	 */
	if (addr_len < sizeof(struct sockaddr_in))
		return -EINVAL;

	return BPF_CGROUP_RUN_PROG_INET4_CONNECT_LOCK(sk, uaddr);
}

/* Checks the bind address and possibly modifies sk->sk_bound_dev_if. */
static int ping_check_bind_addr(struct sock *sk, struct inet_sock *isk,
				struct sockaddr *uaddr, int addr_len)
{
	struct net *net = sock_net(sk);
	if (sk->sk_family == AF_INET) {
		struct sockaddr_in *addr = (struct sockaddr_in *) uaddr;
		u32 tb_id = RT_TABLE_LOCAL;
		int chk_addr_ret;

		if (addr_len < sizeof(*addr))
			return -EINVAL;

		if (addr->sin_family != AF_INET &&
		    !(addr->sin_family == AF_UNSPEC &&
		      addr->sin_addr.s_addr == htonl(INADDR_ANY)))
			return -EAFNOSUPPORT;

		pr_debug("ping_check_bind_addr(sk=%p,addr=%pI4,port=%d)\n",
			 sk, &addr->sin_addr.s_addr, ntohs(addr->sin_port));

		if (addr->sin_addr.s_addr == htonl(INADDR_ANY))
			return 0;

		tb_id = l3mdev_fib_table_by_index(net, sk->sk_bound_dev_if) ? : tb_id;
		chk_addr_ret = inet_addr_type_table(net, addr->sin_addr.s_addr, tb_id);

		if (chk_addr_ret == RTN_MULTICAST ||
		    chk_addr_ret == RTN_BROADCAST ||
		    (chk_addr_ret != RTN_LOCAL &&
		     !inet_can_nonlocal_bind(net, isk)))
			return -EADDRNOTAVAIL;

#if IS_ENABLED(CONFIG_IPV6)
	} else if (sk->sk_family == AF_INET6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) uaddr;
		int addr_type, scoped, has_addr;
		struct net_device *dev = NULL;

		if (addr_len < sizeof(*addr))
			return -EINVAL;

		if (addr->sin6_family != AF_INET6)
			return -EAFNOSUPPORT;

		pr_debug("ping_check_bind_addr(sk=%p,addr=%pI6c,port=%d)\n",
			 sk, addr->sin6_addr.s6_addr, ntohs(addr->sin6_port));

		addr_type = ipv6_addr_type(&addr->sin6_addr);
		scoped = __ipv6_addr_needs_scope_id(addr_type);
		if ((addr_type != IPV6_ADDR_ANY &&
		     !(addr_type & IPV6_ADDR_UNICAST)) ||
		    (scoped && !addr->sin6_scope_id))
			return -EINVAL;

		rcu_read_lock();
		if (addr->sin6_scope_id) {
			dev = dev_get_by_index_rcu(net, addr->sin6_scope_id);
			if (!dev) {
				rcu_read_unlock();
				return -ENODEV;
			}
		}

		if (!dev && sk->sk_bound_dev_if) {
			dev = dev_get_by_index_rcu(net, sk->sk_bound_dev_if);
			if (!dev) {
				rcu_read_unlock();
				return -ENODEV;
			}
		}
		has_addr = pingv6_ops.ipv6_chk_addr(net, &addr->sin6_addr, dev,
						    scoped);
		rcu_read_unlock();

		if (!(ipv6_can_nonlocal_bind(net, isk) || has_addr ||
		      addr_type == IPV6_ADDR_ANY))
			return -EADDRNOTAVAIL;

		if (scoped)
			sk->sk_bound_dev_if = addr->sin6_scope_id;
#endif
	} else {
		return -EAFNOSUPPORT;
	}
	return 0;
}

static void ping_set_saddr(struct sock *sk, struct sockaddr *saddr)
{
	if (saddr->sa_family == AF_INET) {
		struct inet_sock *isk = inet_sk(sk);
		struct sockaddr_in *addr = (struct sockaddr_in *) saddr;
		isk->inet_rcv_saddr = isk->inet_saddr = addr->sin_addr.s_addr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (saddr->sa_family == AF_INET6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) saddr;
		struct ipv6_pinfo *np = inet6_sk(sk);
		sk->sk_v6_rcv_saddr = np->saddr = addr->sin6_addr;
#endif
	}
}

/*
 * We need our own bind because there are no privileged id's == local ports.
 * Moreover, we don't allow binding to multi- and broadcast addresses.
 */

int ping_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct inet_sock *isk = inet_sk(sk);
	unsigned short snum;
	int err;
	int dif = sk->sk_bound_dev_if;

	err = ping_check_bind_addr(sk, isk, uaddr, addr_len);
	if (err)
		return err;

	lock_sock(sk);

	err = -EINVAL;
	if (isk->inet_num != 0)
		goto out;

	err = -EADDRINUSE;
	snum = ntohs(((struct sockaddr_in *)uaddr)->sin_port);
	if (ping_get_port(sk, snum) != 0) {
		/* Restore possibly modified sk->sk_bound_dev_if by ping_check_bind_addr(). */
		sk->sk_bound_dev_if = dif;
		goto out;
	}
	ping_set_saddr(sk, uaddr);

	pr_debug("after bind(): num = %hu, dif = %d\n",
		 isk->inet_num,
		 sk->sk_bound_dev_if);

	err = 0;
	if (sk->sk_family == AF_INET && isk->inet_rcv_saddr)
		sk->sk_userlocks |= SOCK_BINDADDR_LOCK;
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6 && !ipv6_addr_any(&sk->sk_v6_rcv_saddr))
		sk->sk_userlocks |= SOCK_BINDADDR_LOCK;
#endif

	if (snum)
		sk->sk_userlocks |= SOCK_BINDPORT_LOCK;
	isk->inet_sport = htons(isk->inet_num);
	isk->inet_daddr = 0;
	isk->inet_dport = 0;

#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6)
		memset(&sk->sk_v6_daddr, 0, sizeof(sk->sk_v6_daddr));
#endif

	sk_dst_reset(sk);
out:
	release_sock(sk);
	pr_debug("ping_v4_bind -> %d\n", err);
	return err;
}
EXPORT_SYMBOL_GPL(ping_bind);

/*
 * Is this a supported type of ICMP message?
 */

static inline int ping_supported(int family, int type, int code)
{
	return (family == AF_INET && type == ICMP_ECHO && code == 0) ||
	       (family == AF_INET && type == ICMP_EXT_ECHO && code == 0) ||
	       (family == AF_INET6 && type == ICMPV6_ECHO_REQUEST && code == 0) ||
	       (family == AF_INET6 && type == ICMPV6_EXT_ECHO_REQUEST && code == 0);
}

/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.
 */

void ping_err(struct sk_buff *skb, int offset, u32 info)
{
	int family;
	struct icmphdr *icmph;
	struct inet_sock *inet_sock;
	int type;
	int code;
	struct net *net = dev_net(skb->dev);
	struct sock *sk;
	int harderr;
	int err;

	if (skb->protocol == htons(ETH_P_IP)) {
		family = AF_INET;
		type = icmp_hdr(skb)->type;
		code = icmp_hdr(skb)->code;
		icmph = (struct icmphdr *)(skb->data + offset);
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		family = AF_INET6;
		type = icmp6_hdr(skb)->icmp6_type;
		code = icmp6_hdr(skb)->icmp6_code;
		icmph = (struct icmphdr *) (skb->data + offset);
	} else {
		BUG();
	}

	/* We assume the packet has already been checked by icmp_unreach */

	if (!ping_supported(family, icmph->type, icmph->code))
		return;

	pr_debug("ping_err(proto=0x%x,type=%d,code=%d,id=%04x,seq=%04x)\n",
		 skb->protocol, type, code, ntohs(icmph->un.echo.id),
		 ntohs(icmph->un.echo.sequence));

	sk = ping_lookup(net, skb, ntohs(icmph->un.echo.id));
	if (!sk) {
		pr_debug("no socket, dropping\n");
		return;	/* No socket for error */
	}
	pr_debug("err on socket %p\n", sk);

	err = 0;
	harderr = 0;
	inet_sock = inet_sk(sk);

	if (skb->protocol == htons(ETH_P_IP)) {
		switch (type) {
		default:
		case ICMP_TIME_EXCEEDED:
			err = EHOSTUNREACH;
			break;
		case ICMP_SOURCE_QUENCH:
			/* This is not a real error but ping wants to see it.
			 * Report it with some fake errno.
			 */
			err = EREMOTEIO;
			break;
		case ICMP_PARAMETERPROB:
			err = EPROTO;
			harderr = 1;
			break;
		case ICMP_DEST_UNREACH:
			if (code == ICMP_FRAG_NEEDED) { /* Path MTU discovery */
				ipv4_sk_update_pmtu(skb, sk, info);
				if (inet_sock->pmtudisc != IP_PMTUDISC_DONT) {
					err = EMSGSIZE;
					harderr = 1;
					break;
				}
				goto out;
			}
			err = EHOSTUNREACH;
			if (code <= NR_ICMP_UNREACH) {
				harderr = icmp_err_convert[code].fatal;
				err = icmp_err_convert[code].errno;
			}
			break;
		case ICMP_REDIRECT:
			/* See ICMP_SOURCE_QUENCH */
			ipv4_sk_redirect(skb, sk);
			err = EREMOTEIO;
			break;
		}
#if IS_ENABLED(CONFIG_IPV6)
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		harderr = pingv6_ops.icmpv6_err_convert(type, code, &err);
#endif
	}

	/*
	 *      RFC1122: OK.  Passes ICMP errors back to application, as per
	 *	4.1.3.3.
	 */
	if ((family == AF_INET && !inet_sock->recverr) ||
	    (family == AF_INET6 && !inet6_sk(sk)->recverr)) {
		if (!harderr || sk->sk_state != TCP_ESTABLISHED)
			goto out;
	} else {
		if (family == AF_INET) {
			ip_icmp_error(sk, skb, err, 0 /* no remote port */,
				      info, (u8 *)icmph);
#if IS_ENABLED(CONFIG_IPV6)
		} else if (family == AF_INET6) {
			pingv6_ops.ipv6_icmp_error(sk, skb, err, 0,
						   info, (u8 *)icmph);
#endif
		}
	}
	sk->sk_err = err;
	sk_error_report(sk);
out:
	return;
}
EXPORT_SYMBOL_GPL(ping_err);

/*
 *	Copy and checksum an ICMP Echo packet from user space into a buffer
 *	starting from the payload.
 */

int ping_getfrag(void *from, char *to,
		 int offset, int fraglen, int odd, struct sk_buff *skb)
{
	struct pingfakehdr *pfh = from;

	if (!csum_and_copy_from_iter_full(to, fraglen, &pfh->wcheck,
					  &pfh->msg->msg_iter))
		return -EFAULT;

#if IS_ENABLED(CONFIG_IPV6)
	/* For IPv6, checksum each skb as we go along, as expected by
	 * icmpv6_push_pending_frames. For IPv4, accumulate the checksum in
	 * wcheck, it will be finalized in ping_v4_push_pending_frames.
	 */
	if (pfh->family == AF_INET6) {
		skb->csum = csum_block_add(skb->csum, pfh->wcheck, odd);
		skb->ip_summed = CHECKSUM_NONE;
		pfh->wcheck = 0;
	}
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(ping_getfrag);

static int ping_v4_push_pending_frames(struct sock *sk, struct pingfakehdr *pfh,
				       struct flowi4 *fl4)
{
	struct sk_buff *skb = skb_peek(&sk->sk_write_queue);

	if (!skb)
		return 0;
	pfh->wcheck = csum_partial((char *)&pfh->icmph,
		sizeof(struct icmphdr), pfh->wcheck);
	pfh->icmph.checksum = csum_fold(pfh->wcheck);
	memcpy(icmp_hdr(skb), &pfh->icmph, sizeof(struct icmphdr));
	skb->ip_summed = CHECKSUM_NONE;
	return ip_push_pending_frames(sk, fl4);
}

int ping_common_sendmsg(int family, struct msghdr *msg, size_t len,
			void *user_icmph, size_t icmph_len)
{
	u8 type, code;

	if (len > 0xFFFF)
		return -EMSGSIZE;

	/* Must have at least a full ICMP header. */
	if (len < icmph_len)
		return -EINVAL;

	/*
	 *	Check the flags.
	 */

	/* Mirror BSD error message compatibility */
	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/*
	 *	Fetch the ICMP header provided by the userland.
	 *	iovec is modified! The ICMP header is consumed.
	 */
	if (memcpy_from_msg(user_icmph, msg, icmph_len))
		return -EFAULT;

	if (family == AF_INET) {
		type = ((struct icmphdr *) user_icmph)->type;
		code = ((struct icmphdr *) user_icmph)->code;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (family == AF_INET6) {
		type = ((struct icmp6hdr *) user_icmph)->icmp6_type;
		code = ((struct icmp6hdr *) user_icmph)->icmp6_code;
#endif
	} else {
		BUG();
	}

	if (!ping_supported(family, type, code))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(ping_common_sendmsg);

static int ping_v4_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct net *net = sock_net(sk);
	struct flowi4 fl4;
	struct inet_sock *inet = inet_sk(sk);
	struct ipcm_cookie ipc;
	struct icmphdr user_icmph;
	struct pingfakehdr pfh;
	struct rtable *rt = NULL;
	struct ip_options_data opt_copy;
	int free = 0;
	__be32 saddr, daddr, faddr;
	u8  tos;
	int err;

	pr_debug("ping_v4_sendmsg(sk=%p,sk->num=%u)\n", inet, inet->inet_num);

	err = ping_common_sendmsg(AF_INET, msg, len, &user_icmph,
				  sizeof(user_icmph));
	if (err)
		return err;

	/*
	 *	Get and verify the address.
	 */

	if (msg->msg_name) {
		DECLARE_SOCKADDR(struct sockaddr_in *, usin, msg->msg_name);
		if (msg->msg_namelen < sizeof(*usin))
			return -EINVAL;
		if (usin->sin_family != AF_INET)
			return -EAFNOSUPPORT;
		daddr = usin->sin_addr.s_addr;
		/* no remote port */
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;
		daddr = inet->inet_daddr;
		/* no remote port */
	}

	ipcm_init_sk(&ipc, inet);

	if (msg->msg_controllen) {
		err = ip_cmsg_send(sk, msg, &ipc, false);
		if (unlikely(err)) {
			kfree(ipc.opt);
			return err;
		}
		if (ipc.opt)
			free = 1;
	}
	if (!ipc.opt) {
		struct ip_options_rcu *inet_opt;

		rcu_read_lock();
		inet_opt = rcu_dereference(inet->inet_opt);
		if (inet_opt) {
			memcpy(&opt_copy, inet_opt,
			       sizeof(*inet_opt) + inet_opt->opt.optlen);
			ipc.opt = &opt_copy.opt;
		}
		rcu_read_unlock();
	}

	saddr = ipc.addr;
	ipc.addr = faddr = daddr;

	if (ipc.opt && ipc.opt->opt.srr) {
		if (!daddr) {
			err = -EINVAL;
			goto out_free;
		}
		faddr = ipc.opt->opt.faddr;
	}
	tos = get_rttos(&ipc, inet);
	if (sock_flag(sk, SOCK_LOCALROUTE) ||
	    (msg->msg_flags & MSG_DONTROUTE) ||
	    (ipc.opt && ipc.opt->opt.is_strictroute)) {
		tos |= RTO_ONLINK;
	}

	if (ipv4_is_multicast(daddr)) {
		if (!ipc.oif || netif_index_is_l3_master(sock_net(sk), ipc.oif))
			ipc.oif = inet->mc_index;
		if (!saddr)
			saddr = inet->mc_addr;
	} else if (!ipc.oif)
		ipc.oif = inet->uc_index;

	flowi4_init_output(&fl4, ipc.oif, ipc.sockc.mark, tos,
			   RT_SCOPE_UNIVERSE, sk->sk_protocol,
			   inet_sk_flowi_flags(sk), faddr, saddr, 0, 0,
			   sk->sk_uid);

	fl4.fl4_icmp_type = user_icmph.type;
	fl4.fl4_icmp_code = user_icmph.code;

	security_sk_classify_flow(sk, flowi4_to_flowi_common(&fl4));
	rt = ip_route_output_flow(net, &fl4, sk);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		rt = NULL;
		if (err == -ENETUNREACH)
			IP_INC_STATS(net, IPSTATS_MIB_OUTNOROUTES);
		goto out;
	}

	err = -EACCES;
	if ((rt->rt_flags & RTCF_BROADCAST) &&
	    !sock_flag(sk, SOCK_BROADCAST))
		goto out;

	if (msg->msg_flags & MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	if (!ipc.addr)
		ipc.addr = fl4.daddr;

	lock_sock(sk);

	pfh.icmph.type = user_icmph.type; /* already checked */
	pfh.icmph.code = user_icmph.code; /* ditto */
	pfh.icmph.checksum = 0;
	pfh.icmph.un.echo.id = inet->inet_sport;
	pfh.icmph.un.echo.sequence = user_icmph.un.echo.sequence;
	pfh.msg = msg;
	pfh.wcheck = 0;
	pfh.family = AF_INET;

	err = ip_append_data(sk, &fl4, ping_getfrag, &pfh, len,
			     sizeof(struct icmphdr), &ipc, &rt,
			     msg->msg_flags);
	if (err)
		ip_flush_pending_frames(sk);
	else
		err = ping_v4_push_pending_frames(sk, &pfh, &fl4);
	release_sock(sk);

out:
	ip_rt_put(rt);
out_free:
	if (free)
		kfree(ipc.opt);
	if (!err) {
		icmp_out_count(sock_net(sk), user_icmph.type);
		return len;
	}
	return err;

do_confirm:
	if (msg->msg_flags & MSG_PROBE)
		dst_confirm_neigh(&rt->dst, &fl4.daddr);
	if (!(msg->msg_flags & MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto out;
}

int ping_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int flags,
		 int *addr_len)
{
	struct inet_sock *isk = inet_sk(sk);
	int family = sk->sk_family;
	struct sk_buff *skb;
	int copied, err;

	pr_debug("ping_recvmsg(sk=%p,sk->num=%u)\n", isk, isk->inet_num);

	err = -EOPNOTSUPP;
	if (flags & MSG_OOB)
		goto out;

	if (flags & MSG_ERRQUEUE)
		return inet_recv_error(sk, msg, len, addr_len);

	skb = skb_recv_datagram(sk, flags, &err);
	if (!skb)
		goto out;

	copied = skb->len;
	if (copied > len) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	/* Don't bother checking the checksum */
	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto done;

	sock_recv_timestamp(msg, sk, skb);

	/* Copy the address and add cmsg data. */
	if (family == AF_INET) {
		DECLARE_SOCKADDR(struct sockaddr_in *, sin, msg->msg_name);

		if (sin) {
			sin->sin_family = AF_INET;
			sin->sin_port = 0 /* skb->h.uh->source */;
			sin->sin_addr.s_addr = ip_hdr(skb)->saddr;
			memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
			*addr_len = sizeof(*sin);
		}

		if (isk->cmsg_flags)
			ip_cmsg_recv(msg, skb);

#if IS_ENABLED(CONFIG_IPV6)
	} else if (family == AF_INET6) {
		struct ipv6_pinfo *np = inet6_sk(sk);
		struct ipv6hdr *ip6 = ipv6_hdr(skb);
		DECLARE_SOCKADDR(struct sockaddr_in6 *, sin6, msg->msg_name);

		if (sin6) {
			sin6->sin6_family = AF_INET6;
			sin6->sin6_port = 0;
			sin6->sin6_addr = ip6->saddr;
			sin6->sin6_flowinfo = 0;
			if (np->sndflow)
				sin6->sin6_flowinfo = ip6_flowinfo(ip6);
			sin6->sin6_scope_id =
				ipv6_iface_scope_id(&sin6->sin6_addr,
						    inet6_iif(skb));
			*addr_len = sizeof(*sin6);
		}

		if (inet6_sk(sk)->rxopt.all)
			pingv6_ops.ip6_datagram_recv_common_ctl(sk, msg, skb);
		if (skb->protocol == htons(ETH_P_IPV6) &&
		    inet6_sk(sk)->rxopt.all)
			pingv6_ops.ip6_datagram_recv_specific_ctl(sk, msg, skb);
		else if (skb->protocol == htons(ETH_P_IP) && isk->cmsg_flags)
			ip_cmsg_recv(msg, skb);
#endif
	} else {
		BUG();
	}

	err = copied;

done:
	skb_free_datagram(sk, skb);
out:
	pr_debug("ping_recvmsg -> %d\n", err);
	return err;
}
EXPORT_SYMBOL_GPL(ping_recvmsg);

static enum skb_drop_reason __ping_queue_rcv_skb(struct sock *sk,
						 struct sk_buff *skb)
{
	enum skb_drop_reason reason;

	pr_debug("ping_queue_rcv_skb(sk=%p,sk->num=%d,skb=%p)\n",
		 inet_sk(sk), inet_sk(sk)->inet_num, skb);
	if (sock_queue_rcv_skb_reason(sk, skb, &reason) < 0) {
		kfree_skb_reason(skb, reason);
		pr_debug("ping_queue_rcv_skb -> failed\n");
		return reason;
	}
	return SKB_NOT_DROPPED_YET;
}

int ping_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	return __ping_queue_rcv_skb(sk, skb) ? -1 : 0;
}
EXPORT_SYMBOL_GPL(ping_queue_rcv_skb);


/*
 *	All we need to do is get the socket.
 */

enum skb_drop_reason ping_rcv(struct sk_buff *skb)
{
	enum skb_drop_reason reason = SKB_DROP_REASON_NO_SOCKET;
	struct sock *sk;
	struct net *net = dev_net(skb->dev);
	struct icmphdr *icmph = icmp_hdr(skb);

	/* We assume the packet has already been checked by icmp_rcv */

	pr_debug("ping_rcv(skb=%p,id=%04x,seq=%04x)\n",
		 skb, ntohs(icmph->un.echo.id), ntohs(icmph->un.echo.sequence));

	/* Push ICMP header back */
	skb_push(skb, skb->data - (u8 *)icmph);

	sk = ping_lookup(net, skb, ntohs(icmph->un.echo.id));
	if (sk) {
		struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);

		pr_debug("rcv on socket %p\n", sk);
		if (skb2)
			reason = __ping_queue_rcv_skb(sk, skb2);
		else
			reason = SKB_DROP_REASON_NOMEM;
	}

	if (reason)
		pr_debug("no socket, dropping\n");

	return reason;
}
EXPORT_SYMBOL_GPL(ping_rcv);

struct proto ping_prot = {
	.name =		"PING",
	.owner =	THIS_MODULE,
	.init =		ping_init_sock,
	.close =	ping_close,
	.pre_connect =	ping_pre_connect,
	.connect =	ip4_datagram_connect,
	.disconnect =	__udp_disconnect,
	.setsockopt =	ip_setsockopt,
	.getsockopt =	ip_getsockopt,
	.sendmsg =	ping_v4_sendmsg,
	.recvmsg =	ping_recvmsg,
	.bind =		ping_bind,
	.backlog_rcv =	ping_queue_rcv_skb,
	.release_cb =	ip4_datagram_release_cb,
	.hash =		ping_hash,
	.unhash =	ping_unhash,
	.get_port =	ping_get_port,
	.put_port =	ping_unhash,
	.obj_size =	sizeof(struct inet_sock),
};
EXPORT_SYMBOL(ping_prot);

#ifdef CONFIG_PROC_FS

static struct sock *ping_get_first(struct seq_file *seq, int start)
{
	struct sock *sk;
	struct ping_iter_state *state = seq->private;
	struct net *net = seq_file_net(seq);

	for (state->bucket = start; state->bucket < PING_HTABLE_SIZE;
	     ++state->bucket) {
		struct hlist_nulls_node *node;
		struct hlist_nulls_head *hslot;

		hslot = &ping_table.hash[state->bucket];

		if (hlist_nulls_empty(hslot))
			continue;

		sk_nulls_for_each(sk, node, hslot) {
			if (net_eq(sock_net(sk), net) &&
			    sk->sk_family == state->family)
				goto found;
		}
	}
	sk = NULL;
found:
	return sk;
}

static struct sock *ping_get_next(struct seq_file *seq, struct sock *sk)
{
	struct ping_iter_state *state = seq->private;
	struct net *net = seq_file_net(seq);

	do {
		sk = sk_nulls_next(sk);
	} while (sk && (!net_eq(sock_net(sk), net)));

	if (!sk)
		return ping_get_first(seq, state->bucket + 1);
	return sk;
}

static struct sock *ping_get_idx(struct seq_file *seq, loff_t pos)
{
	struct sock *sk = ping_get_first(seq, 0);

	if (sk)
		while (pos && (sk = ping_get_next(seq, sk)) != NULL)
			--pos;
	return pos ? NULL : sk;
}

void *ping_seq_start(struct seq_file *seq, loff_t *pos, sa_family_t family)
	__acquires(RCU)
{
	struct ping_iter_state *state = seq->private;
	state->bucket = 0;
	state->family = family;

	rcu_read_lock();

	return *pos ? ping_get_idx(seq, *pos-1) : SEQ_START_TOKEN;
}
EXPORT_SYMBOL_GPL(ping_seq_start);

static void *ping_v4_seq_start(struct seq_file *seq, loff_t *pos)
{
	return ping_seq_start(seq, pos, AF_INET);
}

void *ping_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *sk;

	if (v == SEQ_START_TOKEN)
		sk = ping_get_idx(seq, 0);
	else
		sk = ping_get_next(seq, v);

	++*pos;
	return sk;
}
EXPORT_SYMBOL_GPL(ping_seq_next);

void ping_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(ping_seq_stop);

static void ping_v4_format_sock(struct sock *sp, struct seq_file *f,
		int bucket)
{
	struct inet_sock *inet = inet_sk(sp);
	__be32 dest = inet->inet_daddr;
	__be32 src = inet->inet_rcv_saddr;
	__u16 destp = ntohs(inet->inet_dport);
	__u16 srcp = ntohs(inet->inet_sport);

	seq_printf(f, "%5d: %08X:%04X %08X:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5u %8d %lu %d %pK %u",
		bucket, src, srcp, dest, destp, sp->sk_state,
		sk_wmem_alloc_get(sp),
		sk_rmem_alloc_get(sp),
		0, 0L, 0,
		from_kuid_munged(seq_user_ns(f), sock_i_uid(sp)),
		0, sock_i_ino(sp),
		refcount_read(&sp->sk_refcnt), sp,
		atomic_read(&sp->sk_drops));
}

static int ping_v4_seq_show(struct seq_file *seq, void *v)
{
	seq_setwidth(seq, 127);
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "  sl  local_address rem_address   st tx_queue "
			   "rx_queue tr tm->when retrnsmt   uid  timeout "
			   "inode ref pointer drops");
	else {
		struct ping_iter_state *state = seq->private;

		ping_v4_format_sock(v, seq, state->bucket);
	}
	seq_pad(seq, '\n');
	return 0;
}

static const struct seq_operations ping_v4_seq_ops = {
	.start		= ping_v4_seq_start,
	.show		= ping_v4_seq_show,
	.next		= ping_seq_next,
	.stop		= ping_seq_stop,
};

static int __net_init ping_v4_proc_init_net(struct net *net)
{
	if (!proc_create_net("icmp", 0444, net->proc_net, &ping_v4_seq_ops,
			sizeof(struct ping_iter_state)))
		return -ENOMEM;
	return 0;
}

static void __net_exit ping_v4_proc_exit_net(struct net *net)
{
	remove_proc_entry("icmp", net->proc_net);
}

static struct pernet_operations ping_v4_net_ops = {
	.init = ping_v4_proc_init_net,
	.exit = ping_v4_proc_exit_net,
};

int __init ping_proc_init(void)
{
	return register_pernet_subsys(&ping_v4_net_ops);
}

void ping_proc_exit(void)
{
	unregister_pernet_subsys(&ping_v4_net_ops);
}

#endif

void __init ping_init(void)
{
	int i;

	for (i = 0; i < PING_HTABLE_SIZE; i++)
		INIT_HLIST_NULLS_HEAD(&ping_table.hash[i], i);
	spin_lock_init(&ping_table.lock);
}
