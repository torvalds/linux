#ifndef _UAPI__LINUX_RTNETLINK_H
#define _UAPI__LINUX_RTNETLINK_H

#include <linux/types.h>
#include <linux/netlink.h>
#include <linux/if_link.h>
#include <linux/if_addr.h>
#include <linux/neighbour.h>

/* rtnetlink families. Values up to 127 are reserved for real address
 * families, values above 128 may be used arbitrarily.
 */
#define RTNL_FAMILY_IPMR		128
#define RTNL_FAMILY_IP6MR		129
#define RTNL_FAMILY_MAX			129

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

	RTM_NEWADDRLABEL = 72,
#define RTM_NEWADDRLABEL RTM_NEWADDRLABEL
	RTM_DELADDRLABEL,
#define RTM_DELADDRLABEL RTM_DELADDRLABEL
	RTM_GETADDRLABEL,
#define RTM_GETADDRLABEL RTM_GETADDRLABEL

	RTM_GETDCB = 78,
#define RTM_GETDCB RTM_GETDCB
	RTM_SETDCB,
#define RTM_SETDCB RTM_SETDCB

	RTM_NEWNETCONF = 80,
#define RTM_NEWNETCONF RTM_NEWNETCONF
	RTM_GETNETCONF = 82,
#define RTM_GETNETCONF RTM_GETNETCONF

	RTM_NEWMDB = 84,
#define RTM_NEWMDB RTM_NEWMDB
	RTM_DELMDB = 85,
#define RTM_DELMDB RTM_DELMDB
	RTM_GETMDB = 86,
#define RTM_GETMDB RTM_GETMDB

	RTM_NEWNSID = 88,
#define RTM_NEWNSID RTM_NEWNSID
	RTM_DELNSID = 89,
#define RTM_DELNSID RTM_DELNSID
	RTM_GETNSID = 90,
#define RTM_GETNSID RTM_GETNSID

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

struct rtattr {
	unsigned short	rta_len;
	unsigned short	rta_type;
};

/* Macros to handle rtattributes */

#define RTA_ALIGNTO	4U
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

struct rtmsg {
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

enum {
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
#define RTPROT_DHCP	16      /* DHCP client */
#define RTPROT_MROUTED	17      /* Multicast daemon */
#define RTPROT_BABEL	42      /* Babel daemon */

/* rtm_scope

   Really it is not scope, but sort of distance to the destination.
   NOWHERE are reserved for not existing destinations, HOST is our
   local addresses, LINK are destinations, located on directly attached
   link and UNIVERSE is everywhere in the Universe.

   Intermediate values are also possible f.e. interior routes
   could be assigned a value between UNIVERSE and LINK.
*/

enum rt_scope_t {
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
#define RTM_F_LOOKUP_TABLE	0x1000	/* set rtm_table to FIB lookup result */

/* Reserved table identifiers */

enum rt_class_t {
	RT_TABLE_UNSPEC=0,
/* User defined values */
	RT_TABLE_COMPAT=252,
	RT_TABLE_DEFAULT=253,
	RT_TABLE_MAIN=254,
	RT_TABLE_LOCAL=255,
	RT_TABLE_MAX=0xFFFFFFFF
};


/* Routing message attributes */

enum rtattr_type_t {
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
	RTA_PROTOINFO, /* no longer used */
	RTA_FLOW,
	RTA_CACHEINFO,
	RTA_SESSION, /* no longer used */
	RTA_MP_ALGO, /* no longer used */
	RTA_TABLE,
	RTA_MARK,
	RTA_MFC_STATS,
	RTA_UID,
	RTA_VIA,
	RTA_NEWDST,
	RTA_PREF,
	RTA_ENCAP_TYPE,
	RTA_ENCAP,
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

struct rtnexthop {
	unsigned short		rtnh_len;
	unsigned char		rtnh_flags;
	unsigned char		rtnh_hops;
	int			rtnh_ifindex;
};

/* rtnh_flags */

#define RTNH_F_DEAD		1	/* Nexthop is dead (used by multipath)	*/
#define RTNH_F_PERVASIVE	2	/* Do recursive gateway lookup	*/
#define RTNH_F_ONLINK		4	/* Gateway is forced on link	*/
#define RTNH_F_OFFLOAD		8	/* offloaded route */
#define RTNH_F_LINKDOWN		16	/* carrier-down on nexthop */

#define RTNH_COMPARE_MASK	(RTNH_F_DEAD | RTNH_F_LINKDOWN)

/* Macros to handle hexthops */

#define RTNH_ALIGNTO	4
#define RTNH_ALIGN(len) ( ((len)+RTNH_ALIGNTO-1) & ~(RTNH_ALIGNTO-1) )
#define RTNH_OK(rtnh,len) ((rtnh)->rtnh_len >= sizeof(struct rtnexthop) && \
			   ((int)(rtnh)->rtnh_len) <= (len))
#define RTNH_NEXT(rtnh)	((struct rtnexthop*)(((char*)(rtnh)) + RTNH_ALIGN((rtnh)->rtnh_len)))
#define RTNH_LENGTH(len) (RTNH_ALIGN(sizeof(struct rtnexthop)) + (len))
#define RTNH_SPACE(len)	RTNH_ALIGN(RTNH_LENGTH(len))
#define RTNH_DATA(rtnh)   ((struct rtattr*)(((char*)(rtnh)) + RTNH_LENGTH(0)))

/* RTA_VIA */
struct rtvia {
	__kernel_sa_family_t	rtvia_family;
	__u8			rtvia_addr[0];
};

/* RTM_CACHEINFO */

struct rta_cacheinfo {
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

enum {
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
	RTAX_INITRWND,
#define RTAX_INITRWND RTAX_INITRWND
	RTAX_QUICKACK,
#define RTAX_QUICKACK RTAX_QUICKACK
	RTAX_CC_ALGO,
#define RTAX_CC_ALGO RTAX_CC_ALGO
	__RTAX_MAX
};

#define RTAX_MAX (__RTAX_MAX - 1)

#define RTAX_FEATURE_ECN	(1 << 0)
#define RTAX_FEATURE_SACK	(1 << 1)
#define RTAX_FEATURE_TIMESTAMP	(1 << 2)
#define RTAX_FEATURE_ALLFRAG	(1 << 3)

#define RTAX_FEATURE_MASK	(RTAX_FEATURE_ECN | RTAX_FEATURE_SACK | \
				 RTAX_FEATURE_TIMESTAMP | RTAX_FEATURE_ALLFRAG)

struct rta_session {
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

struct rta_mfc_stats {
	__u64	mfcs_packets;
	__u64	mfcs_bytes;
	__u64	mfcs_wrong_if;
};

/****
 *		General form of address family dependent message.
 ****/

struct rtgenmsg {
	unsigned char		rtgen_family;
};

/*****************************************************************
 *		Link layer specific messages.
 ****/

/* struct ifinfomsg
 * passes link level specific information, not dependent
 * on network protocol.
 */

struct ifinfomsg {
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

struct prefixmsg {
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

struct prefix_cacheinfo {
	__u32	preferred_time;
	__u32	valid_time;
};


/*****************************************************************
 *		Traffic control messages.
 ****/

struct tcmsg {
	unsigned char	tcm_family;
	unsigned char	tcm__pad1;
	unsigned short	tcm__pad2;
	int		tcm_ifindex;
	__u32		tcm_handle;
	__u32		tcm_parent;
	__u32		tcm_info;
};

enum {
	TCA_UNSPEC,
	TCA_KIND,
	TCA_OPTIONS,
	TCA_STATS,
	TCA_XSTATS,
	TCA_RATE,
	TCA_FCNT,
	TCA_STATS2,
	TCA_STAB,
	__TCA_MAX
};

#define TCA_MAX (__TCA_MAX - 1)

#define TCA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct tcmsg))))
#define TCA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct tcmsg))

/********************************************************************
 *		Neighbor Discovery userland options
 ****/

struct nduseroptmsg {
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

enum {
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
	RTNLGRP_PHONET_IFADDR,
#define RTNLGRP_PHONET_IFADDR	RTNLGRP_PHONET_IFADDR
	RTNLGRP_PHONET_ROUTE,
#define RTNLGRP_PHONET_ROUTE	RTNLGRP_PHONET_ROUTE
	RTNLGRP_DCB,
#define RTNLGRP_DCB		RTNLGRP_DCB
	RTNLGRP_IPV4_NETCONF,
#define RTNLGRP_IPV4_NETCONF	RTNLGRP_IPV4_NETCONF
	RTNLGRP_IPV6_NETCONF,
#define RTNLGRP_IPV6_NETCONF	RTNLGRP_IPV6_NETCONF
	RTNLGRP_MDB,
#define RTNLGRP_MDB		RTNLGRP_MDB
	RTNLGRP_MPLS_ROUTE,
#define RTNLGRP_MPLS_ROUTE	RTNLGRP_MPLS_ROUTE
	RTNLGRP_NSID,
#define RTNLGRP_NSID		RTNLGRP_NSID
	__RTNLGRP_MAX
};
#define RTNLGRP_MAX	(__RTNLGRP_MAX - 1)

/* TC action piece */
struct tcamsg {
	unsigned char	tca_family;
	unsigned char	tca__pad1;
	unsigned short	tca__pad2;
};
#define TA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct tcamsg))))
#define TA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct tcamsg))
#define TCA_ACT_TAB 1 /* attr type must be >=1 */	
#define TCAA_MAX 1

/* New extended info filters for IFLA_EXT_MASK */
#define RTEXT_FILTER_VF		(1 << 0)
#define RTEXT_FILTER_BRVLAN	(1 << 1)
#define RTEXT_FILTER_BRVLAN_COMPRESSED	(1 << 2)
#define	RTEXT_FILTER_SKIP_STATS	(1 << 3)

/* End of information exported to user level */



#endif /* _UAPI__LINUX_RTNETLINK_H */
