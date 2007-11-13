#ifndef __LINUX_RTNETLINK_H
#define __LINUX_RTNETLINK_H

#include <linux/netlink.h>
#include <linux/if_link.h>
#include <linux/if_addr.h>
#include <linux/neighbour.h>

/****
 *		Routing/neighbour discovery messages.
 ****/

/* Types of messages */

enum {
	RTM_BASE	= 16,
#define RTM_BASE	RTM_BASE

	RTM_NEWLINK	= 16,
#define RTM_NEWLINK	RTM_NEWLINK
	RTM_DELLINK,
#define RTM_DELLINK	RTM_DELLINK
	RTM_GETLINK,
#define RTM_GETLINK	RTM_GETLINK
	RTM_SETLINK,
#define RTM_SETLINK	RTM_SETLINK

	RTM_NEWADDR	= 20,
#define RTM_NEWADDR	RTM_NEWADDR
	RTM_DELADDR,
#define RTM_DELADDR	RTM_DELADDR
	RTM_GETADDR,
#define RTM_GETADDR	RTM_GETADDR

	RTM_NEWROUTE	= 24,
#define RTM_NEWROUTE	RTM_NEWROUTE
	RTM_DELROUTE,
#define RTM_DELROUTE	RTM_DELROUTE
	RTM_GETROUTE,
#define RTM_GETROUTE	RTM_GETROUTE

	RTM_NEWNEIGH	= 28,
#define RTM_NEWNEIGH	RTM_NEWNEIGH
	RTM_DELNEIGH,
#define RTM_DELNEIGH	RTM_DELNEIGH
	RTM_GETNEIGH,
#define RTM_GETNEIGH	RTM_GETNEIGH

	RTM_NEWRULE	= 32,
#define RTM_NEWRULE	RTM_NEWRULE
	RTM_DELRULE,
#define RTM_DELRULE	RTM_DELRULE
	RTM_GETRULE,
#define RTM_GETRULE	RTM_GETRULE

	RTM_NEWQDISC	= 36,
#define RTM_NEWQDISC	RTM_NEWQDISC
	RTM_DELQDISC,
#define RTM_DELQDISC	RTM_DELQDISC
	RTM_GETQDISC,
#define RTM_GETQDISC	RTM_GETQDISC

	RTM_NEWTCLASS	= 40,
#define RTM_NEWTCLASS	RTM_NEWTCLASS
	RTM_DELTCLASS,
#define RTM_DELTCLASS	RTM_DELTCLASS
	RTM_GETTCLASS,
#define RTM_GETTCLASS	RTM_GETTCLASS

	RTM_NEWTFILTER	= 44,
#define RTM_NEWTFILTER	RTM_NEWTFILTER
	RTM_DELTFILTER,
#define RTM_DELTFILTER	RTM_DELTFILTER
	RTM_GETTFILTER,
#define RTM_GETTFILTER	RTM_GETTFILTER

	RTM_NEWACTION	= 48,
#define RTM_NEWACTION   RTM_NEWACTION
	RTM_DELACTION,
#define RTM_DELACTION   RTM_DELACTION
	RTM_GETACTION,
#define RTM_GETACTION   RTM_GETACTION

	RTM_NEWPREFIX	= 52,
#define RTM_NEWPREFIX	RTM_NEWPREFIX

	RTM_GETMULTICAST = 58,
#define RTM_GETMULTICAST RTM_GETMULTICAST

	RTM_GETANYCAST	= 62,
#define RTM_GETANYCAST	RTM_GETANYCAST

	RTM_NEWNEIGHTBL	= 64,
#define RTM_NEWNEIGHTBL	RTM_NEWNEIGHTBL
	RTM_GETNEIGHTBL	= 66,
#define RTM_GETNEIGHTBL	RTM_GETNEIGHTBL
	RTM_SETNEIGHTBL,
#define RTM_SETNEIGHTBL	RTM_SETNEIGHTBL

	RTM_NEWNDUSEROPT = 68,
#define RTM_NEWNDUSEROPT RTM_NEWNDUSEROPT

	__RTM_MAX,
#define RTM_MAX		(((__RTM_MAX + 3) & ~3) - 1)
};

#define RTM_NR_MSGTYPES	(RTM_MAX + 1 - RTM_BASE)
#define RTM_NR_FAMILIES	(RTM_NR_MSGTYPES >> 2)
#define RTM_FAM(cmd)	(((cmd) - RTM_BASE) >> 2)

/* 
   Generic structure for encapsulation of optional route information.
   It is reminiscent of sockaddr, but with sa_family replaced
   with attribute type.
 */

struct rtattr
{
	unsigned short	rta_len;
	unsigned short	rta_type;
};

/* Macros to handle rtattributes */

#define RTA_ALIGNTO	4
#define RTA_ALIGN(len) ( ((len)+RTA_ALIGNTO-1) & ~(RTA_ALIGNTO-1) )
#define RTA_OK(rta,len) ((len) >= (int)sizeof(struct rtattr) && \
			 (rta)->rta_len >= sizeof(struct rtattr) && \
			 (rta)->rta_len <= (len))
#define RTA_NEXT(rta,attrlen)	((attrlen) -= RTA_ALIGN((rta)->rta_len), \
				 (struct rtattr*)(((char*)(rta)) + RTA_ALIGN((rta)->rta_len)))
#define RTA_LENGTH(len)	(RTA_ALIGN(sizeof(struct rtattr)) + (len))
#define RTA_SPACE(len)	RTA_ALIGN(RTA_LENGTH(len))
#define RTA_DATA(rta)   ((void*)(((char*)(rta)) + RTA_LENGTH(0)))
#define RTA_PAYLOAD(rta) ((int)((rta)->rta_len) - RTA_LENGTH(0))




/******************************************************************************
 *		Definitions used in routing table administration.
 ****/

struct rtmsg
{
	unsigned char		rtm_family;
	unsigned char		rtm_dst_len;
	unsigned char		rtm_src_len;
	unsigned char		rtm_tos;

	unsigned char		rtm_table;	/* Routing table id */
	unsigned char		rtm_protocol;	/* Routing protocol; see below	*/
	unsigned char		rtm_scope;	/* See below */	
	unsigned char		rtm_type;	/* See below	*/

	unsigned		rtm_flags;
};

/* rtm_type */

enum
{
	RTN_UNSPEC,
	RTN_UNICAST,		/* Gateway or direct route	*/
	RTN_LOCAL,		/* Accept locally		*/
	RTN_BROADCAST,		/* Accept locally as broadcast,
				   send as broadcast */
	RTN_ANYCAST,		/* Accept locally as broadcast,
				   but send as unicast */
	RTN_MULTICAST,		/* Multicast route		*/
	RTN_BLACKHOLE,		/* Drop				*/
	RTN_UNREACHABLE,	/* Destination is unreachable   */
	RTN_PROHIBIT,		/* Administratively prohibited	*/
	RTN_THROW,		/* Not in this table		*/
	RTN_NAT,		/* Translate this address	*/
	RTN_XRESOLVE,		/* Use external resolver	*/
	__RTN_MAX
};

#define RTN_MAX (__RTN_MAX - 1)


/* rtm_protocol */

#define RTPROT_UNSPEC	0
#define RTPROT_REDIRECT	1	/* Route installed by ICMP redirects;
				   not used by current IPv4 */
#define RTPROT_KERNEL	2	/* Route installed by kernel		*/
#define RTPROT_BOOT	3	/* Route installed during boot		*/
#define RTPROT_STATIC	4	/* Route installed by administrator	*/

/* Values of protocol >= RTPROT_STATIC are not interpreted by kernel;
   they are just passed from user and back as is.
   It will be used by hypothetical multiple routing daemons.
   Note that protocol values should be standardized in order to
   avoid conflicts.
 */

#define RTPROT_GATED	8	/* Apparently, GateD */
#define RTPROT_RA	9	/* RDISC/ND router advertisements */
#define RTPROT_MRT	10	/* Merit MRT */
#define RTPROT_ZEBRA	11	/* Zebra */
#define RTPROT_BIRD	12	/* BIRD */
#define RTPROT_DNROUTED	13	/* DECnet routing daemon */
#define RTPROT_XORP	14	/* XORP */
#define RTPROT_NTK	15	/* Netsukuku */

/* rtm_scope

   Really it is not scope, but sort of distance to the destination.
   NOWHERE are reserved for not existing destinations, HOST is our
   local addresses, LINK are destinations, located on directly attached
   link and UNIVERSE is everywhere in the Universe.

   Intermediate values are also possible f.e. interior routes
   could be assigned a value between UNIVERSE and LINK.
*/

enum rt_scope_t
{
	RT_SCOPE_UNIVERSE=0,
/* User defined values  */
	RT_SCOPE_SITE=200,
	RT_SCOPE_LINK=253,
	RT_SCOPE_HOST=254,
	RT_SCOPE_NOWHERE=255
};

/* rtm_flags */

#define RTM_F_NOTIFY		0x100	/* Notify user of route change	*/
#define RTM_F_CLONED		0x200	/* This route is cloned		*/
#define RTM_F_EQUALIZE		0x400	/* Multipath equalizer: NI	*/
#define RTM_F_PREFIX		0x800	/* Prefix addresses		*/

/* Reserved table identifiers */

enum rt_class_t
{
	RT_TABLE_UNSPEC=0,
/* User defined values */
	RT_TABLE_DEFAULT=253,
	RT_TABLE_MAIN=254,
	RT_TABLE_LOCAL=255,
	RT_TABLE_MAX=0xFFFFFFFF
};


/* Routing message attributes */

enum rtattr_type_t
{
	RTA_UNSPEC,
	RTA_DST,
	RTA_SRC,
	RTA_IIF,
	RTA_OIF,
	RTA_GATEWAY,
	RTA_PRIORITY,
	RTA_PREFSRC,
	RTA_METRICS,
	RTA_MULTIPATH,
	RTA_PROTOINFO,
	RTA_FLOW,
	RTA_CACHEINFO,
	RTA_SESSION,
	RTA_MP_ALGO, /* no longer used */
	RTA_TABLE,
	__RTA_MAX
};

#define RTA_MAX (__RTA_MAX - 1)

#define RTM_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct rtmsg))))
#define RTM_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct rtmsg))

/* RTM_MULTIPATH --- array of struct rtnexthop.
 *
 * "struct rtnexthop" describes all necessary nexthop information,
 * i.e. parameters of path to a destination via this nexthop.
 *
 * At the moment it is impossible to set different prefsrc, mtu, window
 * and rtt for different paths from multipath.
 */

struct rtnexthop
{
	unsigned short		rtnh_len;
	unsigned char		rtnh_flags;
	unsigned char		rtnh_hops;
	int			rtnh_ifindex;
};

/* rtnh_flags */

#define RTNH_F_DEAD		1	/* Nexthop is dead (used by multipath)	*/
#define RTNH_F_PERVASIVE	2	/* Do recursive gateway lookup	*/
#define RTNH_F_ONLINK		4	/* Gateway is forced on link	*/

/* Macros to handle hexthops */

#define RTNH_ALIGNTO	4
#define RTNH_ALIGN(len) ( ((len)+RTNH_ALIGNTO-1) & ~(RTNH_ALIGNTO-1) )
#define RTNH_OK(rtnh,len) ((rtnh)->rtnh_len >= sizeof(struct rtnexthop) && \
			   ((int)(rtnh)->rtnh_len) <= (len))
#define RTNH_NEXT(rtnh)	((struct rtnexthop*)(((char*)(rtnh)) + RTNH_ALIGN((rtnh)->rtnh_len)))
#define RTNH_LENGTH(len) (RTNH_ALIGN(sizeof(struct rtnexthop)) + (len))
#define RTNH_SPACE(len)	RTNH_ALIGN(RTNH_LENGTH(len))
#define RTNH_DATA(rtnh)   ((struct rtattr*)(((char*)(rtnh)) + RTNH_LENGTH(0)))

/* RTM_CACHEINFO */

struct rta_cacheinfo
{
	__u32	rta_clntref;
	__u32	rta_lastuse;
	__s32	rta_expires;
	__u32	rta_error;
	__u32	rta_used;

#define RTNETLINK_HAVE_PEERINFO 1
	__u32	rta_id;
	__u32	rta_ts;
	__u32	rta_tsage;
};

/* RTM_METRICS --- array of struct rtattr with types of RTAX_* */

enum
{
	RTAX_UNSPEC,
#define RTAX_UNSPEC RTAX_UNSPEC
	RTAX_LOCK,
#define RTAX_LOCK RTAX_LOCK
	RTAX_MTU,
#define RTAX_MTU RTAX_MTU
	RTAX_WINDOW,
#define RTAX_WINDOW RTAX_WINDOW
	RTAX_RTT,
#define RTAX_RTT RTAX_RTT
	RTAX_RTTVAR,
#define RTAX_RTTVAR RTAX_RTTVAR
	RTAX_SSTHRESH,
#define RTAX_SSTHRESH RTAX_SSTHRESH
	RTAX_CWND,
#define RTAX_CWND RTAX_CWND
	RTAX_ADVMSS,
#define RTAX_ADVMSS RTAX_ADVMSS
	RTAX_REORDERING,
#define RTAX_REORDERING RTAX_REORDERING
	RTAX_HOPLIMIT,
#define RTAX_HOPLIMIT RTAX_HOPLIMIT
	RTAX_INITCWND,
#define RTAX_INITCWND RTAX_INITCWND
	RTAX_FEATURES,
#define RTAX_FEATURES RTAX_FEATURES
	RTAX_RTO_MIN,
#define RTAX_RTO_MIN RTAX_RTO_MIN
	__RTAX_MAX
};

#define RTAX_MAX (__RTAX_MAX - 1)

#define RTAX_FEATURE_ECN	0x00000001
#define RTAX_FEATURE_SACK	0x00000002
#define RTAX_FEATURE_TIMESTAMP	0x00000004
#define RTAX_FEATURE_ALLFRAG	0x00000008

struct rta_session
{
	__u8	proto;
	__u8	pad1;
	__u16	pad2;

	union {
		struct {
			__u16	sport;
			__u16	dport;
		} ports;

		struct {
			__u8	type;
			__u8	code;
			__u16	ident;
		} icmpt;

		__u32		spi;
	} u;
};

/****
 *		General form of address family dependent message.
 ****/

struct rtgenmsg
{
	unsigned char		rtgen_family;
};

/*****************************************************************
 *		Link layer specific messages.
 ****/

/* struct ifinfomsg
 * passes link level specific information, not dependent
 * on network protocol.
 */

struct ifinfomsg
{
	unsigned char	ifi_family;
	unsigned char	__ifi_pad;
	unsigned short	ifi_type;		/* ARPHRD_* */
	int		ifi_index;		/* Link index	*/
	unsigned	ifi_flags;		/* IFF_* flags	*/
	unsigned	ifi_change;		/* IFF_* change mask */
};

/********************************************************************
 *		prefix information 
 ****/

struct prefixmsg
{
	unsigned char	prefix_family;
	unsigned char	prefix_pad1;
	unsigned short	prefix_pad2;
	int		prefix_ifindex;
	unsigned char	prefix_type;
	unsigned char	prefix_len;
	unsigned char	prefix_flags;
	unsigned char	prefix_pad3;
};

enum 
{
	PREFIX_UNSPEC,
	PREFIX_ADDRESS,
	PREFIX_CACHEINFO,
	__PREFIX_MAX
};

#define PREFIX_MAX	(__PREFIX_MAX - 1)

struct prefix_cacheinfo
{
	__u32	preferred_time;
	__u32	valid_time;
};


/*****************************************************************
 *		Traffic control messages.
 ****/

struct tcmsg
{
	unsigned char	tcm_family;
	unsigned char	tcm__pad1;
	unsigned short	tcm__pad2;
	int		tcm_ifindex;
	__u32		tcm_handle;
	__u32		tcm_parent;
	__u32		tcm_info;
};

enum
{
	TCA_UNSPEC,
	TCA_KIND,
	TCA_OPTIONS,
	TCA_STATS,
	TCA_XSTATS,
	TCA_RATE,
	TCA_FCNT,
	TCA_STATS2,
	__TCA_MAX
};

#define TCA_MAX (__TCA_MAX - 1)

#define TCA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct tcmsg))))
#define TCA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct tcmsg))

/********************************************************************
 *		Neighbor Discovery userland options
 ****/

struct nduseroptmsg
{
	unsigned char	nduseropt_family;
	unsigned char	nduseropt_pad1;
	unsigned short	nduseropt_opts_len;	/* Total length of options */
	int		nduseropt_ifindex;
	__u8		nduseropt_icmp_type;
	__u8		nduseropt_icmp_code;
	unsigned short	nduseropt_pad2;
	unsigned int	nduseropt_pad3;
	/* Followed by one or more ND options */
};

enum
{
	NDUSEROPT_UNSPEC,
	NDUSEROPT_SRCADDR,
	__NDUSEROPT_MAX
};

#define NDUSEROPT_MAX	(__NDUSEROPT_MAX - 1)

#ifndef __KERNEL__
/* RTnetlink multicast groups - backwards compatibility for userspace */
#define RTMGRP_LINK		1
#define RTMGRP_NOTIFY		2
#define RTMGRP_NEIGH		4
#define RTMGRP_TC		8

#define RTMGRP_IPV4_IFADDR	0x10
#define RTMGRP_IPV4_MROUTE	0x20
#define RTMGRP_IPV4_ROUTE	0x40
#define RTMGRP_IPV4_RULE	0x80

#define RTMGRP_IPV6_IFADDR	0x100
#define RTMGRP_IPV6_MROUTE	0x200
#define RTMGRP_IPV6_ROUTE	0x400
#define RTMGRP_IPV6_IFINFO	0x800

#define RTMGRP_DECnet_IFADDR    0x1000
#define RTMGRP_DECnet_ROUTE     0x4000

#define RTMGRP_IPV6_PREFIX	0x20000
#endif

/* RTnetlink multicast groups */
enum rtnetlink_groups {
	RTNLGRP_NONE,
#define RTNLGRP_NONE		RTNLGRP_NONE
	RTNLGRP_LINK,
#define RTNLGRP_LINK		RTNLGRP_LINK
	RTNLGRP_NOTIFY,
#define RTNLGRP_NOTIFY		RTNLGRP_NOTIFY
	RTNLGRP_NEIGH,
#define RTNLGRP_NEIGH		RTNLGRP_NEIGH
	RTNLGRP_TC,
#define RTNLGRP_TC		RTNLGRP_TC
	RTNLGRP_IPV4_IFADDR,
#define RTNLGRP_IPV4_IFADDR	RTNLGRP_IPV4_IFADDR
	RTNLGRP_IPV4_MROUTE,
#define	RTNLGRP_IPV4_MROUTE	RTNLGRP_IPV4_MROUTE
	RTNLGRP_IPV4_ROUTE,
#define RTNLGRP_IPV4_ROUTE	RTNLGRP_IPV4_ROUTE
	RTNLGRP_IPV4_RULE,
#define RTNLGRP_IPV4_RULE	RTNLGRP_IPV4_RULE
	RTNLGRP_IPV6_IFADDR,
#define RTNLGRP_IPV6_IFADDR	RTNLGRP_IPV6_IFADDR
	RTNLGRP_IPV6_MROUTE,
#define RTNLGRP_IPV6_MROUTE	RTNLGRP_IPV6_MROUTE
	RTNLGRP_IPV6_ROUTE,
#define RTNLGRP_IPV6_ROUTE	RTNLGRP_IPV6_ROUTE
	RTNLGRP_IPV6_IFINFO,
#define RTNLGRP_IPV6_IFINFO	RTNLGRP_IPV6_IFINFO
	RTNLGRP_DECnet_IFADDR,
#define RTNLGRP_DECnet_IFADDR	RTNLGRP_DECnet_IFADDR
	RTNLGRP_NOP2,
	RTNLGRP_DECnet_ROUTE,
#define RTNLGRP_DECnet_ROUTE	RTNLGRP_DECnet_ROUTE
	RTNLGRP_DECnet_RULE,
#define RTNLGRP_DECnet_RULE	RTNLGRP_DECnet_RULE
	RTNLGRP_NOP4,
	RTNLGRP_IPV6_PREFIX,
#define RTNLGRP_IPV6_PREFIX	RTNLGRP_IPV6_PREFIX
	RTNLGRP_IPV6_RULE,
#define RTNLGRP_IPV6_RULE	RTNLGRP_IPV6_RULE
	RTNLGRP_ND_USEROPT,
#define RTNLGRP_ND_USEROPT	RTNLGRP_ND_USEROPT
	__RTNLGRP_MAX
};
#define RTNLGRP_MAX	(__RTNLGRP_MAX - 1)

/* TC action piece */
struct tcamsg
{
	unsigned char	tca_family;
	unsigned char	tca__pad1;
	unsigned short	tca__pad2;
};
#define TA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct tcamsg))))
#define TA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct tcamsg))
#define TCA_ACT_TAB 1 /* attr type must be >=1 */	
#define TCAA_MAX 1

/* End of information exported to user level */

#ifdef __KERNEL__

#include <linux/mutex.h>

extern size_t rtattr_strlcpy(char *dest, const struct rtattr *rta, size_t size);
static __inline__ int rtattr_strcmp(const struct rtattr *rta, const char *str)
{
	int len = strlen(str) + 1;
	return len > rta->rta_len || memcmp(RTA_DATA(rta), str, len);
}

extern int rtattr_parse(struct rtattr *tb[], int maxattr, struct rtattr *rta, int len);
extern int __rtattr_parse_nested_compat(struct rtattr *tb[], int maxattr,
				        struct rtattr *rta, int len);

#define rtattr_parse_nested(tb, max, rta) \
	rtattr_parse((tb), (max), RTA_DATA((rta)), RTA_PAYLOAD((rta)))

#define rtattr_parse_nested_compat(tb, max, rta, data, len) \
({	data = RTA_PAYLOAD(rta) >= len ? RTA_DATA(rta) : NULL; \
	__rtattr_parse_nested_compat(tb, max, rta, len); })

extern int rtnetlink_send(struct sk_buff *skb, u32 pid, u32 group, int echo);
extern int rtnl_unicast(struct sk_buff *skb, u32 pid);
extern int rtnl_notify(struct sk_buff *skb, u32 pid, u32 group,
		       struct nlmsghdr *nlh, gfp_t flags);
extern void rtnl_set_sk_err(u32 group, int error);
extern int rtnetlink_put_metrics(struct sk_buff *skb, u32 *metrics);
extern int rtnl_put_cacheinfo(struct sk_buff *skb, struct dst_entry *dst,
			      u32 id, u32 ts, u32 tsage, long expires,
			      u32 error);

extern void __rta_fill(struct sk_buff *skb, int attrtype, int attrlen, const void *data);

#define RTA_PUT(skb, attrtype, attrlen, data) \
({	if (unlikely(skb_tailroom(skb) < (int)RTA_SPACE(attrlen))) \
		 goto rtattr_failure; \
   	__rta_fill(skb, attrtype, attrlen, data); }) 

#define RTA_APPEND(skb, attrlen, data) \
({	if (unlikely(skb_tailroom(skb) < (int)(attrlen))) \
		goto rtattr_failure; \
	memcpy(skb_put(skb, attrlen), data, attrlen); })

#define RTA_PUT_NOHDR(skb, attrlen, data) \
({	RTA_APPEND(skb, RTA_ALIGN(attrlen), data); \
	memset(skb_tail_pointer(skb) - (RTA_ALIGN(attrlen) - attrlen), 0, \
	       RTA_ALIGN(attrlen) - attrlen); })

#define RTA_PUT_U8(skb, attrtype, value) \
({	u8 _tmp = (value); \
	RTA_PUT(skb, attrtype, sizeof(u8), &_tmp); })

#define RTA_PUT_U16(skb, attrtype, value) \
({	u16 _tmp = (value); \
	RTA_PUT(skb, attrtype, sizeof(u16), &_tmp); })

#define RTA_PUT_U32(skb, attrtype, value) \
({	u32 _tmp = (value); \
	RTA_PUT(skb, attrtype, sizeof(u32), &_tmp); })

#define RTA_PUT_U64(skb, attrtype, value) \
({	u64 _tmp = (value); \
	RTA_PUT(skb, attrtype, sizeof(u64), &_tmp); })

#define RTA_PUT_SECS(skb, attrtype, value) \
	RTA_PUT_U64(skb, attrtype, (value) / HZ)

#define RTA_PUT_MSECS(skb, attrtype, value) \
	RTA_PUT_U64(skb, attrtype, jiffies_to_msecs(value))

#define RTA_PUT_STRING(skb, attrtype, value) \
	RTA_PUT(skb, attrtype, strlen(value) + 1, value)

#define RTA_PUT_FLAG(skb, attrtype) \
	RTA_PUT(skb, attrtype, 0, NULL);

#define RTA_NEST(skb, type) \
({	struct rtattr *__start = (struct rtattr *)skb_tail_pointer(skb); \
	RTA_PUT(skb, type, 0, NULL); \
	__start;  })

#define RTA_NEST_END(skb, start) \
({	(start)->rta_len = skb_tail_pointer(skb) - (unsigned char *)(start); \
	(skb)->len; })

#define RTA_NEST_COMPAT(skb, type, attrlen, data) \
({	struct rtattr *__start = (struct rtattr *)skb_tail_pointer(skb); \
	RTA_PUT(skb, type, attrlen, data); \
	RTA_NEST(skb, type); \
	__start; })

#define RTA_NEST_COMPAT_END(skb, start) \
({	struct rtattr *__nest = (void *)(start) + NLMSG_ALIGN((start)->rta_len); \
	(start)->rta_len = skb_tail_pointer(skb) - (unsigned char *)(start); \
	RTA_NEST_END(skb, __nest); \
	(skb)->len; })

#define RTA_NEST_CANCEL(skb, start) \
({	if (start) \
		skb_trim(skb, (unsigned char *) (start) - (skb)->data); \
	-1; })

#define RTA_GET_U8(rta) \
({	if (!rta || RTA_PAYLOAD(rta) < sizeof(u8)) \
		goto rtattr_failure; \
	*(u8 *) RTA_DATA(rta); })

#define RTA_GET_U16(rta) \
({	if (!rta || RTA_PAYLOAD(rta) < sizeof(u16)) \
		goto rtattr_failure; \
	*(u16 *) RTA_DATA(rta); })

#define RTA_GET_U32(rta) \
({	if (!rta || RTA_PAYLOAD(rta) < sizeof(u32)) \
		goto rtattr_failure; \
	*(u32 *) RTA_DATA(rta); })

#define RTA_GET_U64(rta) \
({	u64 _tmp; \
	if (!rta || RTA_PAYLOAD(rta) < sizeof(u64)) \
		goto rtattr_failure; \
	memcpy(&_tmp, RTA_DATA(rta), sizeof(_tmp)); \
	_tmp; })

#define RTA_GET_FLAG(rta) (!!(rta))

#define RTA_GET_SECS(rta) ((unsigned long) RTA_GET_U64(rta) * HZ)
#define RTA_GET_MSECS(rta) (msecs_to_jiffies((unsigned long) RTA_GET_U64(rta)))
		
static inline struct rtattr *
__rta_reserve(struct sk_buff *skb, int attrtype, int attrlen)
{
	struct rtattr *rta;
	int size = RTA_LENGTH(attrlen);

	rta = (struct rtattr*)skb_put(skb, RTA_ALIGN(size));
	rta->rta_type = attrtype;
	rta->rta_len = size;
	memset(RTA_DATA(rta) + attrlen, 0, RTA_ALIGN(size) - size);
	return rta;
}

#define __RTA_PUT(skb, attrtype, attrlen) \
({ 	if (unlikely(skb_tailroom(skb) < (int)RTA_SPACE(attrlen))) \
		goto rtattr_failure; \
   	__rta_reserve(skb, attrtype, attrlen); })

extern void rtmsg_ifinfo(int type, struct net_device *dev, unsigned change);

/* RTNL is used as a global lock for all changes to network configuration  */
extern void rtnl_lock(void);
extern void rtnl_unlock(void);
extern int rtnl_trylock(void);

extern void rtnetlink_init(void);
extern void __rtnl_unlock(void);

#define ASSERT_RTNL() do { \
	if (unlikely(rtnl_trylock())) { \
		rtnl_unlock(); \
		printk(KERN_ERR "RTNL: assertion failed at %s (%d)\n", \
		       __FILE__,  __LINE__); \
		dump_stack(); \
	} \
} while(0)

#define BUG_TRAP(x) do { \
	if (unlikely(!(x))) { \
		printk(KERN_ERR "KERNEL: assertion (%s) failed at %s (%d)\n", \
			#x,  __FILE__ , __LINE__); \
	} \
} while(0)

static inline u32 rtm_get_table(struct rtattr **rta, u8 table)
{
	return RTA_GET_U32(rta[RTA_TABLE-1]);
rtattr_failure:
	return table;
}

#endif /* __KERNEL__ */


#endif	/* __LINUX_RTNETLINK_H */
