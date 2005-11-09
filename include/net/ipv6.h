/*
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	$Id: ipv6.h,v 1.1 2002/05/20 15:13:07 jgrimm Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_IPV6_H
#define _NET_IPV6_H

#include <linux/ipv6.h>
#include <linux/hardirq.h>
#include <net/ndisc.h>
#include <net/flow.h>
#include <net/snmp.h>

#define SIN6_LEN_RFC2133	24

#define IPV6_MAXPLEN		65535

/*
 *	NextHeader field of IPv6 header
 */

#define NEXTHDR_HOP		0	/* Hop-by-hop option header. */
#define NEXTHDR_TCP		6	/* TCP segment. */
#define NEXTHDR_UDP		17	/* UDP message. */
#define NEXTHDR_IPV6		41	/* IPv6 in IPv6 */
#define NEXTHDR_ROUTING		43	/* Routing header. */
#define NEXTHDR_FRAGMENT	44	/* Fragmentation/reassembly header. */
#define NEXTHDR_ESP		50	/* Encapsulating security payload. */
#define NEXTHDR_AUTH		51	/* Authentication header. */
#define NEXTHDR_ICMP		58	/* ICMP for IPv6. */
#define NEXTHDR_NONE		59	/* No next header */
#define NEXTHDR_DEST		60	/* Destination options header. */

#define NEXTHDR_MAX		255



#define IPV6_DEFAULT_HOPLIMIT   64
#define IPV6_DEFAULT_MCASTHOPS	1

/*
 *	Addr type
 *	
 *	type	-	unicast | multicast
 *	scope	-	local	| site	    | global
 *	v4	-	compat
 *	v4mapped
 *	any
 *	loopback
 */

#define IPV6_ADDR_ANY		0x0000U

#define IPV6_ADDR_UNICAST      	0x0001U	
#define IPV6_ADDR_MULTICAST    	0x0002U	

#define IPV6_ADDR_LOOPBACK	0x0010U
#define IPV6_ADDR_LINKLOCAL	0x0020U
#define IPV6_ADDR_SITELOCAL	0x0040U

#define IPV6_ADDR_COMPATv4	0x0080U

#define IPV6_ADDR_SCOPE_MASK	0x00f0U

#define IPV6_ADDR_MAPPED	0x1000U
#define IPV6_ADDR_RESERVED	0x2000U	/* reserved address space */

/*
 *	Addr scopes
 */
#ifdef __KERNEL__
#define IPV6_ADDR_MC_SCOPE(a)	\
	((a)->s6_addr[1] & 0x0f)	/* nonstandard */
#define __IPV6_ADDR_SCOPE_INVALID	-1
#endif
#define IPV6_ADDR_SCOPE_NODELOCAL	0x01
#define IPV6_ADDR_SCOPE_LINKLOCAL	0x02
#define IPV6_ADDR_SCOPE_SITELOCAL	0x05
#define IPV6_ADDR_SCOPE_ORGLOCAL	0x08
#define IPV6_ADDR_SCOPE_GLOBAL		0x0e

/*
 *	fragmentation header
 */

struct frag_hdr {
	unsigned char	nexthdr;
	unsigned char	reserved;	
	unsigned short	frag_off;
	__u32		identification;
};

#define	IP6_MF	0x0001

#ifdef __KERNEL__

#include <linux/config.h>
#include <net/sock.h>

/* sysctls */
extern int sysctl_ipv6_bindv6only;
extern int sysctl_mld_max_msf;

/* MIBs */
DECLARE_SNMP_STAT(struct ipstats_mib, ipv6_statistics);
#define IP6_INC_STATS(field)		SNMP_INC_STATS(ipv6_statistics, field)
#define IP6_INC_STATS_BH(field)		SNMP_INC_STATS_BH(ipv6_statistics, field)
#define IP6_INC_STATS_USER(field) 	SNMP_INC_STATS_USER(ipv6_statistics, field)
DECLARE_SNMP_STAT(struct icmpv6_mib, icmpv6_statistics);
#define ICMP6_INC_STATS(idev, field)		({			\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		SNMP_INC_STATS(idev->stats.icmpv6, field); 		\
	SNMP_INC_STATS(icmpv6_statistics, field);			\
})
#define ICMP6_INC_STATS_BH(idev, field)		({			\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		SNMP_INC_STATS_BH((_idev)->stats.icmpv6, field);	\
	SNMP_INC_STATS_BH(icmpv6_statistics, field);			\
})
#define ICMP6_INC_STATS_USER(idev, field) 	({			\
	struct inet6_dev *_idev = (idev);				\
	if (likely(_idev != NULL))					\
		SNMP_INC_STATS_USER(_idev->stats.icmpv6, field);	\
	SNMP_INC_STATS_USER(icmpv6_statistics, field);			\
})
#define ICMP6_INC_STATS_OFFSET_BH(idev, field, offset)	({			\
	struct inet6_dev *_idev = idev;						\
	__typeof__(offset) _offset = (offset);					\
	if (likely(_idev != NULL))						\
		SNMP_INC_STATS_OFFSET_BH(_idev->stats.icmpv6, field, _offset);	\
	SNMP_INC_STATS_OFFSET_BH(icmpv6_statistics, field, _offset);    	\
})
DECLARE_SNMP_STAT(struct udp_mib, udp_stats_in6);
#define UDP6_INC_STATS(field)		SNMP_INC_STATS(udp_stats_in6, field)
#define UDP6_INC_STATS_BH(field)	SNMP_INC_STATS_BH(udp_stats_in6, field)
#define UDP6_INC_STATS_USER(field) 	SNMP_INC_STATS_USER(udp_stats_in6, field)

int snmp6_register_dev(struct inet6_dev *idev);
int snmp6_unregister_dev(struct inet6_dev *idev);
int snmp6_alloc_dev(struct inet6_dev *idev);
int snmp6_free_dev(struct inet6_dev *idev);
int snmp6_mib_init(void *ptr[2], size_t mibsize, size_t mibalign);
void snmp6_mib_free(void *ptr[2]);

struct ip6_ra_chain
{
	struct ip6_ra_chain	*next;
	struct sock		*sk;
	int			sel;
	void			(*destructor)(struct sock *);
};

extern struct ip6_ra_chain	*ip6_ra_chain;
extern rwlock_t ip6_ra_lock;

/*
   This structure is prepared by protocol, when parsing
   ancillary data and passed to IPv6.
 */

struct ipv6_txoptions
{
	/* Length of this structure */
	int			tot_len;

	/* length of extension headers   */

	__u16			opt_flen;	/* after fragment hdr */
	__u16			opt_nflen;	/* before fragment hdr */

	struct ipv6_opt_hdr	*hopopt;
	struct ipv6_opt_hdr	*dst0opt;
	struct ipv6_rt_hdr	*srcrt;	/* Routing Header */
	struct ipv6_opt_hdr	*dst1opt;

	/* Option buffer, as read by IPV6_PKTOPTIONS, starts here. */
};

struct ip6_flowlabel
{
	struct ip6_flowlabel	*next;
	u32			label;
	struct in6_addr		dst;
	struct ipv6_txoptions	*opt;
	atomic_t		users;
	unsigned long		linger;
	u8			share;
	u32			owner;
	unsigned long		lastuse;
	unsigned long		expires;
};

#define IPV6_FLOWINFO_MASK	__constant_htonl(0x0FFFFFFF)
#define IPV6_FLOWLABEL_MASK	__constant_htonl(0x000FFFFF)

struct ipv6_fl_socklist
{
	struct ipv6_fl_socklist	*next;
	struct ip6_flowlabel	*fl;
};

extern struct ip6_flowlabel	*fl6_sock_lookup(struct sock *sk, u32 label);
extern struct ipv6_txoptions	*fl6_merge_options(struct ipv6_txoptions * opt_space,
						   struct ip6_flowlabel * fl,
						   struct ipv6_txoptions * fopt);
extern void			fl6_free_socklist(struct sock *sk);
extern int			ipv6_flowlabel_opt(struct sock *sk, char __user *optval, int optlen);
extern void			ip6_flowlabel_init(void);
extern void			ip6_flowlabel_cleanup(void);

static inline void fl6_sock_release(struct ip6_flowlabel *fl)
{
	if (fl)
		atomic_dec(&fl->users);
}

extern int 			ip6_ra_control(struct sock *sk, int sel,
					       void (*destructor)(struct sock *));


extern int			ipv6_parse_hopopts(struct sk_buff *skb, int);

extern struct ipv6_txoptions *  ipv6_dup_options(struct sock *sk, struct ipv6_txoptions *opt);
extern struct ipv6_txoptions *	ipv6_renew_options(struct sock *sk, struct ipv6_txoptions *opt,
						   int newtype,
						   struct ipv6_opt_hdr __user *newopt,
						   int newoptlen);

extern int ip6_frag_nqueues;
extern atomic_t ip6_frag_mem;

#define IPV6_FRAG_TIMEOUT	(60*HZ)		/* 60 seconds */

/*
 *	Function prototype for build_xmit
 */

typedef int		(*inet_getfrag_t) (const void *data,
					   struct in6_addr *addr,
					   char *,
					   unsigned int, unsigned int);

extern int __ipv6_addr_type(const struct in6_addr *addr);
static inline int ipv6_addr_type(const struct in6_addr *addr)
{
	return __ipv6_addr_type(addr) & 0xffff;
}

static inline int ipv6_addr_scope(const struct in6_addr *addr)
{
	return __ipv6_addr_type(addr) & IPV6_ADDR_SCOPE_MASK;
}

static inline int __ipv6_addr_src_scope(int type)
{
	return (type == IPV6_ADDR_ANY ? __IPV6_ADDR_SCOPE_INVALID : (type >> 16));
}

static inline int ipv6_addr_src_scope(const struct in6_addr *addr)
{
	return __ipv6_addr_src_scope(__ipv6_addr_type(addr));
}

static inline int ipv6_addr_cmp(const struct in6_addr *a1, const struct in6_addr *a2)
{
	return memcmp((const void *) a1, (const void *) a2, sizeof(struct in6_addr));
}

static inline void ipv6_addr_copy(struct in6_addr *a1, const struct in6_addr *a2)
{
	memcpy((void *) a1, (const void *) a2, sizeof(struct in6_addr));
}

static inline void ipv6_addr_prefix(struct in6_addr *pfx, 
				    const struct in6_addr *addr,
				    int plen)
{
	/* caller must guarantee 0 <= plen <= 128 */
	int o = plen >> 3,
	    b = plen & 0x7;

	memcpy(pfx->s6_addr, addr, o);
	if (b != 0) {
		pfx->s6_addr[o] = addr->s6_addr[o] & (0xff00 >> b);
		o++;
	}
	if (o < 16)
		memset(pfx->s6_addr + o, 0, 16 - o);
}

#ifndef __HAVE_ARCH_ADDR_SET
static inline void ipv6_addr_set(struct in6_addr *addr, 
				     __u32 w1, __u32 w2,
				     __u32 w3, __u32 w4)
{
	addr->s6_addr32[0] = w1;
	addr->s6_addr32[1] = w2;
	addr->s6_addr32[2] = w3;
	addr->s6_addr32[3] = w4;
}
#endif

static inline int ipv6_addr_equal(const struct in6_addr *a1,
				  const struct in6_addr *a2)
{
	return (a1->s6_addr32[0] == a2->s6_addr32[0] &&
		a1->s6_addr32[1] == a2->s6_addr32[1] &&
		a1->s6_addr32[2] == a2->s6_addr32[2] &&
		a1->s6_addr32[3] == a2->s6_addr32[3]);
}

static inline int __ipv6_prefix_equal(const u32 *a1, const u32 *a2,
				      unsigned int prefixlen)
{
	unsigned pdw, pbi;

	/* check complete u32 in prefix */
	pdw = prefixlen >> 5;
	if (pdw && memcmp(a1, a2, pdw << 2))
		return 0;

	/* check incomplete u32 in prefix */
	pbi = prefixlen & 0x1f;
	if (pbi && ((a1[pdw] ^ a2[pdw]) & htonl((0xffffffff) << (32 - pbi))))
		return 0;

	return 1;
}

static inline int ipv6_prefix_equal(const struct in6_addr *a1,
				    const struct in6_addr *a2,
				    unsigned int prefixlen)
{
	return __ipv6_prefix_equal(a1->s6_addr32, a2->s6_addr32,
				   prefixlen);
}

static inline int ipv6_addr_any(const struct in6_addr *a)
{
	return ((a->s6_addr32[0] | a->s6_addr32[1] | 
		 a->s6_addr32[2] | a->s6_addr32[3] ) == 0); 
}

/*
 * find the first different bit between two addresses
 * length of address must be a multiple of 32bits
 */
static inline int __ipv6_addr_diff(const void *token1, const void *token2, int addrlen)
{
	const __u32 *a1 = token1, *a2 = token2;
	int i;

	addrlen >>= 2;

	for (i = 0; i < addrlen; i++) {
		__u32 xb = a1[i] ^ a2[i];
		if (xb) {
			int j = 31;

			xb = ntohl(xb);
			while ((xb & (1 << j)) == 0)
				j--;

			return (i * 32 + 31 - j);
		}
	}

	/*
	 *	we should *never* get to this point since that 
	 *	would mean the addrs are equal
	 *
	 *	However, we do get to it 8) And exacly, when
	 *	addresses are equal 8)
	 *
	 *	ip route add 1111::/128 via ...
	 *	ip route add 1111::/64 via ...
	 *	and we are here.
	 *
	 *	Ideally, this function should stop comparison
	 *	at prefix length. It does not, but it is still OK,
	 *	if returned value is greater than prefix length.
	 *					--ANK (980803)
	 */
	return (addrlen << 5);
}

static inline int ipv6_addr_diff(const struct in6_addr *a1, const struct in6_addr *a2)
{
	return __ipv6_addr_diff(a1, a2, sizeof(struct in6_addr));
}

/*
 *	Prototypes exported by ipv6
 */

/*
 *	rcv function (called from netdevice level)
 */

extern int			ipv6_rcv(struct sk_buff *skb, 
					 struct net_device *dev, 
					 struct packet_type *pt,
					 struct net_device *orig_dev);

/*
 *	upper-layer output functions
 */
extern int			ip6_xmit(struct sock *sk,
					 struct sk_buff *skb,
					 struct flowi *fl,
					 struct ipv6_txoptions *opt,
					 int ipfragok);

extern int			ip6_nd_hdr(struct sock *sk,
					   struct sk_buff *skb,
					   struct net_device *dev,
					   struct in6_addr *saddr,
					   struct in6_addr *daddr,
					   int proto, int len);

extern int			ip6_find_1stfragopt(struct sk_buff *skb, u8 **nexthdr);

extern int			ip6_append_data(struct sock *sk,
						int getfrag(void *from, char *to, int offset, int len, int odd, struct sk_buff *skb),
		    				void *from,
						int length,
						int transhdrlen,
		      				int hlimit,
		      				int tclass,
						struct ipv6_txoptions *opt,
						struct flowi *fl,
						struct rt6_info *rt,
						unsigned int flags);

extern int			ip6_push_pending_frames(struct sock *sk);

extern void			ip6_flush_pending_frames(struct sock *sk);

extern int			ip6_dst_lookup(struct sock *sk,
					       struct dst_entry **dst,
					       struct flowi *fl);

/*
 *	skb processing functions
 */

extern int			ip6_output(struct sk_buff *skb);
extern int			ip6_forward(struct sk_buff *skb);
extern int			ip6_input(struct sk_buff *skb);
extern int			ip6_mc_input(struct sk_buff *skb);

/*
 *	Extension header (options) processing
 */

extern u8 *			ipv6_build_nfrag_opts(struct sk_buff *skb,
						      u8 *prev_hdr,
						      struct ipv6_txoptions *opt,
						      struct in6_addr *daddr,
						      u32 jumbolen);
extern u8 *			ipv6_build_frag_opts(struct sk_buff *skb,
						     u8 *prev_hdr,
						     struct ipv6_txoptions *opt);
extern void 			ipv6_push_nfrag_opts(struct sk_buff *skb,
						     struct ipv6_txoptions *opt,
						     u8 *proto,
						     struct in6_addr **daddr_p);
extern void			ipv6_push_frag_opts(struct sk_buff *skb,
						    struct ipv6_txoptions *opt,
						    u8 *proto);

extern int			ipv6_skip_exthdr(const struct sk_buff *, int start,
					         u8 *nexthdrp);

extern int 			ipv6_ext_hdr(u8 nexthdr);

extern struct ipv6_txoptions *	ipv6_invert_rthdr(struct sock *sk,
						  struct ipv6_rt_hdr *hdr);


/*
 *	socket options (ipv6_sockglue.c)
 */

extern int			ipv6_setsockopt(struct sock *sk, int level, 
						int optname,
						char __user *optval, 
						int optlen);
extern int			ipv6_getsockopt(struct sock *sk, int level, 
						int optname,
						char __user *optval, 
						int __user *optlen);

extern void			ipv6_packet_init(void);

extern void			ipv6_packet_cleanup(void);

extern int			ip6_datagram_connect(struct sock *sk, 
						     struct sockaddr *addr, int addr_len);

extern int 			ipv6_recv_error(struct sock *sk, struct msghdr *msg, int len);
extern void			ipv6_icmp_error(struct sock *sk, struct sk_buff *skb, int err, u16 port,
						u32 info, u8 *payload);
extern void			ipv6_local_error(struct sock *sk, int err, struct flowi *fl, u32 info);

extern int inet6_release(struct socket *sock);
extern int inet6_bind(struct socket *sock, struct sockaddr *uaddr, 
		      int addr_len);
extern int inet6_getname(struct socket *sock, struct sockaddr *uaddr,
			 int *uaddr_len, int peer);
extern int inet6_ioctl(struct socket *sock, unsigned int cmd, 
		       unsigned long arg);

/*
 * reassembly.c
 */
extern int sysctl_ip6frag_high_thresh;
extern int sysctl_ip6frag_low_thresh;
extern int sysctl_ip6frag_time;
extern int sysctl_ip6frag_secret_interval;

extern struct proto_ops inet6_stream_ops;
extern struct proto_ops inet6_dgram_ops;

extern int ip6_mc_source(int add, int omode, struct sock *sk,
			 struct group_source_req *pgsr);
extern int ip6_mc_msfilter(struct sock *sk, struct group_filter *gsf);
extern int ip6_mc_msfget(struct sock *sk, struct group_filter *gsf,
			 struct group_filter __user *optval,
			 int __user *optlen);

#ifdef CONFIG_PROC_FS
extern int  ac6_proc_init(void);
extern void ac6_proc_exit(void);
extern int  raw6_proc_init(void);
extern void raw6_proc_exit(void);
extern int  tcp6_proc_init(void);
extern void tcp6_proc_exit(void);
extern int  udp6_proc_init(void);
extern void udp6_proc_exit(void);
extern int  ipv6_misc_proc_init(void);
extern void ipv6_misc_proc_exit(void);

extern struct rt6_statistics rt6_stats;
#endif

#ifdef CONFIG_SYSCTL
extern ctl_table ipv6_route_table[];
extern ctl_table ipv6_icmp_table[];

extern void ipv6_sysctl_register(void);
extern void ipv6_sysctl_unregister(void);
#endif

#endif /* __KERNEL__ */
#endif /* _NET_IPV6_H */
