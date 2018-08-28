/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ADDRCONF_H
#define _ADDRCONF_H

#define MAX_RTR_SOLICITATIONS		-1		/* unlimited */
#define RTR_SOLICITATION_INTERVAL	(4*HZ)
#define RTR_SOLICITATION_MAX_INTERVAL	(3600*HZ)	/* 1 hour */

#define MIN_VALID_LIFETIME		(2*3600)	/* 2 hours */

#define TEMP_VALID_LIFETIME		(7*86400)
#define TEMP_PREFERRED_LIFETIME		(86400)
#define REGEN_MAX_RETRY			(3)
#define MAX_DESYNC_FACTOR		(600)

#define ADDR_CHECK_FREQUENCY		(120*HZ)

#define IPV6_MAX_ADDRESSES		16

#define ADDRCONF_TIMER_FUZZ_MINUS	(HZ > 50 ? HZ / 50 : 1)
#define ADDRCONF_TIMER_FUZZ		(HZ / 4)
#define ADDRCONF_TIMER_FUZZ_MAX		(HZ)

#define ADDRCONF_NOTIFY_PRIORITY	0

#include <linux/in.h>
#include <linux/in6.h>

struct prefix_info {
	__u8			type;
	__u8			length;
	__u8			prefix_len;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8			onlink : 1,
			 	autoconf : 1,
				reserved : 6;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			reserved : 6,
				autoconf : 1,
				onlink : 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__be32			valid;
	__be32			prefered;
	__be32			reserved2;

	struct in6_addr		prefix;
};

#include <linux/netdevice.h>
#include <net/if_inet6.h>
#include <net/ipv6.h>

struct in6_validator_info {
	struct in6_addr		i6vi_addr;
	struct inet6_dev	*i6vi_dev;
	struct netlink_ext_ack	*extack;
};

struct ifa6_config {
	const struct in6_addr	*pfx;
	unsigned int		plen;

	const struct in6_addr	*peer_pfx;

	u32			rt_priority;
	u32			ifa_flags;
	u32			preferred_lft;
	u32			valid_lft;
	u16			scope;
};

int addrconf_init(void);
void addrconf_cleanup(void);

int addrconf_add_ifaddr(struct net *net, void __user *arg);
int addrconf_del_ifaddr(struct net *net, void __user *arg);
int addrconf_set_dstaddr(struct net *net, void __user *arg);

int ipv6_chk_addr(struct net *net, const struct in6_addr *addr,
		  const struct net_device *dev, int strict);
int ipv6_chk_addr_and_flags(struct net *net, const struct in6_addr *addr,
			    const struct net_device *dev, bool skip_dev_check,
			    int strict, u32 banned_flags);

#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
int ipv6_chk_home_addr(struct net *net, const struct in6_addr *addr);
#endif

bool ipv6_chk_custom_prefix(const struct in6_addr *addr,
				   const unsigned int prefix_len,
				   struct net_device *dev);

int ipv6_chk_prefix(const struct in6_addr *addr, struct net_device *dev);

struct inet6_ifaddr *ipv6_get_ifaddr(struct net *net,
				     const struct in6_addr *addr,
				     struct net_device *dev, int strict);

int ipv6_dev_get_saddr(struct net *net, const struct net_device *dev,
		       const struct in6_addr *daddr, unsigned int srcprefs,
		       struct in6_addr *saddr);
int __ipv6_get_lladdr(struct inet6_dev *idev, struct in6_addr *addr,
		      u32 banned_flags);
int ipv6_get_lladdr(struct net_device *dev, struct in6_addr *addr,
		    u32 banned_flags);
bool inet_rcv_saddr_equal(const struct sock *sk, const struct sock *sk2,
			  bool match_wildcard);
bool inet_rcv_saddr_any(const struct sock *sk);
void addrconf_join_solict(struct net_device *dev, const struct in6_addr *addr);
void addrconf_leave_solict(struct inet6_dev *idev, const struct in6_addr *addr);

void addrconf_add_linklocal(struct inet6_dev *idev,
			    const struct in6_addr *addr, u32 flags);

int addrconf_prefix_rcv_add_addr(struct net *net, struct net_device *dev,
				 const struct prefix_info *pinfo,
				 struct inet6_dev *in6_dev,
				 const struct in6_addr *addr, int addr_type,
				 u32 addr_flags, bool sllao, bool tokenized,
				 __u32 valid_lft, u32 prefered_lft);

static inline void addrconf_addr_eui48_base(u8 *eui, const char *const addr)
{
	memcpy(eui, addr, 3);
	eui[3] = 0xFF;
	eui[4] = 0xFE;
	memcpy(eui + 5, addr + 3, 3);
}

static inline void addrconf_addr_eui48(u8 *eui, const char *const addr)
{
	addrconf_addr_eui48_base(eui, addr);
	eui[0] ^= 2;
}

static inline int addrconf_ifid_eui48(u8 *eui, struct net_device *dev)
{
	if (dev->addr_len != ETH_ALEN)
		return -1;

	/*
	 * The zSeries OSA network cards can be shared among various
	 * OS instances, but the OSA cards have only one MAC address.
	 * This leads to duplicate address conflicts in conjunction
	 * with IPv6 if more than one instance uses the same card.
	 *
	 * The driver for these cards can deliver a unique 16-bit
	 * identifier for each instance sharing the same card.  It is
	 * placed instead of 0xFFFE in the interface identifier.  The
	 * "u" bit of the interface identifier is not inverted in this
	 * case.  Hence the resulting interface identifier has local
	 * scope according to RFC2373.
	 */

	addrconf_addr_eui48_base(eui, dev->dev_addr);

	if (dev->dev_id) {
		eui[3] = (dev->dev_id >> 8) & 0xFF;
		eui[4] = dev->dev_id & 0xFF;
	} else {
		eui[0] ^= 2;
	}

	return 0;
}

static inline unsigned long addrconf_timeout_fixup(u32 timeout,
						   unsigned int unit)
{
	if (timeout == 0xffffffff)
		return ~0UL;

	/*
	 * Avoid arithmetic overflow.
	 * Assuming unit is constant and non-zero, this "if" statement
	 * will go away on 64bit archs.
	 */
	if (0xfffffffe > LONG_MAX / unit && timeout > LONG_MAX / unit)
		return LONG_MAX / unit;

	return timeout;
}

static inline int addrconf_finite_timeout(unsigned long timeout)
{
	return ~timeout;
}

/*
 *	IPv6 Address Label subsystem (addrlabel.c)
 */
int ipv6_addr_label_init(void);
void ipv6_addr_label_cleanup(void);
int ipv6_addr_label_rtnl_register(void);
u32 ipv6_addr_label(struct net *net, const struct in6_addr *addr,
		    int type, int ifindex);

/*
 *	multicast prototypes (mcast.c)
 */
int ipv6_sock_mc_join(struct sock *sk, int ifindex,
		      const struct in6_addr *addr);
int ipv6_sock_mc_drop(struct sock *sk, int ifindex,
		      const struct in6_addr *addr);
void __ipv6_sock_mc_close(struct sock *sk);
void ipv6_sock_mc_close(struct sock *sk);
bool inet6_mc_check(struct sock *sk, const struct in6_addr *mc_addr,
		    const struct in6_addr *src_addr);

int ipv6_dev_mc_inc(struct net_device *dev, const struct in6_addr *addr);
int __ipv6_dev_mc_dec(struct inet6_dev *idev, const struct in6_addr *addr);
int ipv6_dev_mc_dec(struct net_device *dev, const struct in6_addr *addr);
void ipv6_mc_up(struct inet6_dev *idev);
void ipv6_mc_down(struct inet6_dev *idev);
void ipv6_mc_unmap(struct inet6_dev *idev);
void ipv6_mc_remap(struct inet6_dev *idev);
void ipv6_mc_init_dev(struct inet6_dev *idev);
void ipv6_mc_destroy_dev(struct inet6_dev *idev);
int ipv6_mc_check_mld(struct sk_buff *skb, struct sk_buff **skb_trimmed);
void addrconf_dad_failure(struct sk_buff *skb, struct inet6_ifaddr *ifp);

bool ipv6_chk_mcast_addr(struct net_device *dev, const struct in6_addr *group,
			 const struct in6_addr *src_addr);

void ipv6_mc_dad_complete(struct inet6_dev *idev);

/* A stub used by vxlan module. This is ugly, ideally these
 * symbols should be built into the core kernel.
 */
struct ipv6_stub {
	int (*ipv6_sock_mc_join)(struct sock *sk, int ifindex,
				 const struct in6_addr *addr);
	int (*ipv6_sock_mc_drop)(struct sock *sk, int ifindex,
				 const struct in6_addr *addr);
	int (*ipv6_dst_lookup)(struct net *net, struct sock *sk,
			       struct dst_entry **dst, struct flowi6 *fl6);

	struct fib6_table *(*fib6_get_table)(struct net *net, u32 id);
	struct fib6_info *(*fib6_lookup)(struct net *net, int oif,
					 struct flowi6 *fl6, int flags);
	struct fib6_info *(*fib6_table_lookup)(struct net *net,
					      struct fib6_table *table,
					      int oif, struct flowi6 *fl6,
					      int flags);
	struct fib6_info *(*fib6_multipath_select)(const struct net *net,
						   struct fib6_info *f6i,
						   struct flowi6 *fl6, int oif,
						   const struct sk_buff *skb,
						   int strict);
	u32 (*ip6_mtu_from_fib6)(struct fib6_info *f6i, struct in6_addr *daddr,
				 struct in6_addr *saddr);

	void (*udpv6_encap_enable)(void);
	void (*ndisc_send_na)(struct net_device *dev, const struct in6_addr *daddr,
			      const struct in6_addr *solicited_addr,
			      bool router, bool solicited, bool override, bool inc_opt);
	struct neigh_table *nd_tbl;
};
extern const struct ipv6_stub *ipv6_stub __read_mostly;

/* A stub used by bpf helpers. Similarly ugly as ipv6_stub */
struct ipv6_bpf_stub {
	int (*inet6_bind)(struct sock *sk, struct sockaddr *uaddr, int addr_len,
			  bool force_bind_address_no_port, bool with_lock);
};
extern const struct ipv6_bpf_stub *ipv6_bpf_stub __read_mostly;

/*
 * identify MLD packets for MLD filter exceptions
 */
static inline bool ipv6_is_mld(struct sk_buff *skb, int nexthdr, int offset)
{
	struct icmp6hdr *hdr;

	if (nexthdr != IPPROTO_ICMPV6 ||
	    !pskb_network_may_pull(skb, offset + sizeof(struct icmp6hdr)))
		return false;

	hdr = (struct icmp6hdr *)(skb_network_header(skb) + offset);

	switch (hdr->icmp6_type) {
	case ICMPV6_MGM_QUERY:
	case ICMPV6_MGM_REPORT:
	case ICMPV6_MGM_REDUCTION:
	case ICMPV6_MLD2_REPORT:
		return true;
	default:
		break;
	}
	return false;
}

void addrconf_prefix_rcv(struct net_device *dev,
			 u8 *opt, int len, bool sllao);

/*
 *	anycast prototypes (anycast.c)
 */
int ipv6_sock_ac_join(struct sock *sk, int ifindex,
		      const struct in6_addr *addr);
int ipv6_sock_ac_drop(struct sock *sk, int ifindex,
		      const struct in6_addr *addr);
void ipv6_sock_ac_close(struct sock *sk);

int __ipv6_dev_ac_inc(struct inet6_dev *idev, const struct in6_addr *addr);
int __ipv6_dev_ac_dec(struct inet6_dev *idev, const struct in6_addr *addr);
void ipv6_ac_destroy_dev(struct inet6_dev *idev);
bool ipv6_chk_acast_addr(struct net *net, struct net_device *dev,
			 const struct in6_addr *addr);
bool ipv6_chk_acast_addr_src(struct net *net, struct net_device *dev,
			     const struct in6_addr *addr);

/* Device notifier */
int register_inet6addr_notifier(struct notifier_block *nb);
int unregister_inet6addr_notifier(struct notifier_block *nb);
int inet6addr_notifier_call_chain(unsigned long val, void *v);

int register_inet6addr_validator_notifier(struct notifier_block *nb);
int unregister_inet6addr_validator_notifier(struct notifier_block *nb);
int inet6addr_validator_notifier_call_chain(unsigned long val, void *v);

void inet6_netconf_notify_devconf(struct net *net, int event, int type,
				  int ifindex, struct ipv6_devconf *devconf);

/**
 * __in6_dev_get - get inet6_dev pointer from netdevice
 * @dev: network device
 *
 * Caller must hold rcu_read_lock or RTNL, because this function
 * does not take a reference on the inet6_dev.
 */
static inline struct inet6_dev *__in6_dev_get(const struct net_device *dev)
{
	return rcu_dereference_rtnl(dev->ip6_ptr);
}

/**
 * __in6_dev_get_safely - get inet6_dev pointer from netdevice
 * @dev: network device
 *
 * This is a safer version of __in6_dev_get
 */
static inline struct inet6_dev *__in6_dev_get_safely(const struct net_device *dev)
{
	if (likely(dev))
		return rcu_dereference_rtnl(dev->ip6_ptr);
	else
		return NULL;
}

/**
 * in6_dev_get - get inet6_dev pointer from netdevice
 * @dev: network device
 *
 * This version can be used in any context, and takes a reference
 * on the inet6_dev. Callers must use in6_dev_put() later to
 * release this reference.
 */
static inline struct inet6_dev *in6_dev_get(const struct net_device *dev)
{
	struct inet6_dev *idev;

	rcu_read_lock();
	idev = rcu_dereference(dev->ip6_ptr);
	if (idev)
		refcount_inc(&idev->refcnt);
	rcu_read_unlock();
	return idev;
}

static inline struct neigh_parms *__in6_dev_nd_parms_get_rcu(const struct net_device *dev)
{
	struct inet6_dev *idev = __in6_dev_get(dev);

	return idev ? idev->nd_parms : NULL;
}

void in6_dev_finish_destroy(struct inet6_dev *idev);

static inline void in6_dev_put(struct inet6_dev *idev)
{
	if (refcount_dec_and_test(&idev->refcnt))
		in6_dev_finish_destroy(idev);
}

static inline void in6_dev_put_clear(struct inet6_dev **pidev)
{
	struct inet6_dev *idev = *pidev;

	if (idev) {
		in6_dev_put(idev);
		*pidev = NULL;
	}
}

static inline void __in6_dev_put(struct inet6_dev *idev)
{
	refcount_dec(&idev->refcnt);
}

static inline void in6_dev_hold(struct inet6_dev *idev)
{
	refcount_inc(&idev->refcnt);
}

void inet6_ifa_finish_destroy(struct inet6_ifaddr *ifp);

static inline void in6_ifa_put(struct inet6_ifaddr *ifp)
{
	if (refcount_dec_and_test(&ifp->refcnt))
		inet6_ifa_finish_destroy(ifp);
}

static inline void __in6_ifa_put(struct inet6_ifaddr *ifp)
{
	refcount_dec(&ifp->refcnt);
}

static inline void in6_ifa_hold(struct inet6_ifaddr *ifp)
{
	refcount_inc(&ifp->refcnt);
}


/*
 *	compute link-local solicited-node multicast address
 */

static inline void addrconf_addr_solict_mult(const struct in6_addr *addr,
					     struct in6_addr *solicited)
{
	ipv6_addr_set(solicited,
		      htonl(0xFF020000), 0,
		      htonl(0x1),
		      htonl(0xFF000000) | addr->s6_addr32[3]);
}

static inline bool ipv6_addr_is_ll_all_nodes(const struct in6_addr *addr)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	__be64 *p = (__be64 *)addr;
	return ((p[0] ^ cpu_to_be64(0xff02000000000000UL)) | (p[1] ^ cpu_to_be64(1))) == 0UL;
#else
	return ((addr->s6_addr32[0] ^ htonl(0xff020000)) |
		addr->s6_addr32[1] | addr->s6_addr32[2] |
		(addr->s6_addr32[3] ^ htonl(0x00000001))) == 0;
#endif
}

static inline bool ipv6_addr_is_ll_all_routers(const struct in6_addr *addr)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	__be64 *p = (__be64 *)addr;
	return ((p[0] ^ cpu_to_be64(0xff02000000000000UL)) | (p[1] ^ cpu_to_be64(2))) == 0UL;
#else
	return ((addr->s6_addr32[0] ^ htonl(0xff020000)) |
		addr->s6_addr32[1] | addr->s6_addr32[2] |
		(addr->s6_addr32[3] ^ htonl(0x00000002))) == 0;
#endif
}

static inline bool ipv6_addr_is_isatap(const struct in6_addr *addr)
{
	return (addr->s6_addr32[2] | htonl(0x02000000)) == htonl(0x02005EFE);
}

static inline bool ipv6_addr_is_solict_mult(const struct in6_addr *addr)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	__be64 *p = (__be64 *)addr;
	return ((p[0] ^ cpu_to_be64(0xff02000000000000UL)) |
		((p[1] ^ cpu_to_be64(0x00000001ff000000UL)) &
		 cpu_to_be64(0xffffffffff000000UL))) == 0UL;
#else
	return ((addr->s6_addr32[0] ^ htonl(0xff020000)) |
		addr->s6_addr32[1] |
		(addr->s6_addr32[2] ^ htonl(0x00000001)) |
		(addr->s6_addr[12] ^ 0xff)) == 0;
#endif
}

#ifdef CONFIG_PROC_FS
int if6_proc_init(void);
void if6_proc_exit(void);
#endif

#endif
