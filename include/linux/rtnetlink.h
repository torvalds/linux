#ifndef __LINUX_RTNETLINK_H
#define __LINUX_RTNETLINK_H

#include <linux/netlink.h>

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
	RTM_GETPREFIX	= 54,
#define RTM_GETPREFIX	RTM_GETPREFIX

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
	__RT_TABLE_MAX
};
#define RT_TABLE_MAX (__RT_TABLE_MAX - 1)



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
	RTA_MP_ALGO,
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


/*********************************************************
 *		Interface address.
 ****/

struct ifaddrmsg
{
	unsigned char	ifa_family;
	unsigned char	ifa_prefixlen;	/* The prefix length		*/
	unsigned char	ifa_flags;	/* Flags			*/
	unsigned char	ifa_scope;	/* See above			*/
	int		ifa_index;	/* Link index			*/
};

enum
{
	IFA_UNSPEC,
	IFA_ADDRESS,
	IFA_LOCAL,
	IFA_LABEL,
	IFA_BROADCAST,
	IFA_ANYCAST,
	IFA_CACHEINFO,
	IFA_MULTICAST,
	__IFA_MAX
};

#define IFA_MAX (__IFA_MAX - 1)

/* ifa_flags */

#define IFA_F_SECONDARY		0x01
#define IFA_F_TEMPORARY		IFA_F_SECONDARY

#define IFA_F_DEPRECATED	0x20
#define IFA_F_TENTATIVE		0x40
#define IFA_F_PERMANENT		0x80

struct ifa_cacheinfo
{
	__u32	ifa_prefered;
	__u32	ifa_valid;
	__u32	cstamp; /* created timestamp, hundredths of seconds */
	__u32	tstamp; /* updated timestamp, hundredths of seconds */
};


#define IFA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ifaddrmsg))))
#define IFA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct ifaddrmsg))

/*
   Important comment:
   IFA_ADDRESS is prefix address, rather than local interface address.
   It makes no difference for normally configured broadcast interfaces,
   but for point-to-point IFA_ADDRESS is DESTINATION address,
   local address is supplied in IFA_LOCAL attribute.
 */

/**************************************************************
 *		Neighbour discovery.
 ****/

struct ndmsg
{
	unsigned char	ndm_family;
	unsigned char	ndm_pad1;
	unsigned short	ndm_pad2;
	int		ndm_ifindex;	/* Link index			*/
	__u16		ndm_state;
	__u8		ndm_flags;
	__u8		ndm_type;
};

enum
{
	NDA_UNSPEC,
	NDA_DST,
	NDA_LLADDR,
	NDA_CACHEINFO,
	NDA_PROBES,
	__NDA_MAX
};

#define NDA_MAX (__NDA_MAX - 1)

#define NDA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ndmsg))))
#define NDA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct ndmsg))

/*
 *	Neighbor Cache Entry Flags
 */

#define NTF_PROXY	0x08	/* == ATF_PUBL */
#define NTF_ROUTER	0x80

/*
 *	Neighbor Cache Entry States.
 */

#define NUD_INCOMPLETE	0x01
#define NUD_REACHABLE	0x02
#define NUD_STALE	0x04
#define NUD_DELAY	0x08
#define NUD_PROBE	0x10
#define NUD_FAILED	0x20

/* Dummy states */
#define NUD_NOARP	0x40
#define NUD_PERMANENT	0x80
#define NUD_NONE	0x00


struct nda_cacheinfo
{
	__u32		ndm_confirmed;
	__u32		ndm_used;
	__u32		ndm_updated;
	__u32		ndm_refcnt;
};


/*****************************************************************
 *		Neighbour tables specific messages.
 *
 * To retrieve the neighbour tables send RTM_GETNEIGHTBL with the
 * NLM_F_DUMP flag set. Every neighbour table configuration is
 * spread over multiple messages to avoid running into message
 * size limits on systems with many interfaces. The first message
 * in the sequence transports all not device specific data such as
 * statistics, configuration, and the default parameter set.
 * This message is followed by 0..n messages carrying device
 * specific parameter sets.
 * Although the ordering should be sufficient, NDTA_NAME can be
 * used to identify sequences. The initial message can be identified
 * by checking for NDTA_CONFIG. The device specific messages do
 * not contain this TLV but have NDTPA_IFINDEX set to the
 * corresponding interface index.
 *
 * To change neighbour table attributes, send RTM_SETNEIGHTBL
 * with NDTA_NAME set. Changeable attribute include NDTA_THRESH[1-3],
 * NDTA_GC_INTERVAL, and all TLVs in NDTA_PARMS unless marked
 * otherwise. Device specific parameter sets can be changed by
 * setting NDTPA_IFINDEX to the interface index of the corresponding
 * device.
 ****/

struct ndt_stats
{
	__u64		ndts_allocs;
	__u64		ndts_destroys;
	__u64		ndts_hash_grows;
	__u64		ndts_res_failed;
	__u64		ndts_lookups;
	__u64		ndts_hits;
	__u64		ndts_rcv_probes_mcast;
	__u64		ndts_rcv_probes_ucast;
	__u64		ndts_periodic_gc_runs;
	__u64		ndts_forced_gc_runs;
};

enum {
	NDTPA_UNSPEC,
	NDTPA_IFINDEX,			/* u32, unchangeable */
	NDTPA_REFCNT,			/* u32, read-only */
	NDTPA_REACHABLE_TIME,		/* u64, read-only, msecs */
	NDTPA_BASE_REACHABLE_TIME,	/* u64, msecs */
	NDTPA_RETRANS_TIME,		/* u64, msecs */
	NDTPA_GC_STALETIME,		/* u64, msecs */
	NDTPA_DELAY_PROBE_TIME,		/* u64, msecs */
	NDTPA_QUEUE_LEN,		/* u32 */
	NDTPA_APP_PROBES,		/* u32 */
	NDTPA_UCAST_PROBES,		/* u32 */
	NDTPA_MCAST_PROBES,		/* u32 */
	NDTPA_ANYCAST_DELAY,		/* u64, msecs */
	NDTPA_PROXY_DELAY,		/* u64, msecs */
	NDTPA_PROXY_QLEN,		/* u32 */
	NDTPA_LOCKTIME,			/* u64, msecs */
	__NDTPA_MAX
};
#define NDTPA_MAX (__NDTPA_MAX - 1)

struct ndtmsg
{
	__u8		ndtm_family;
	__u8		ndtm_pad1;
	__u16		ndtm_pad2;
};

struct ndt_config
{
	__u16		ndtc_key_len;
	__u16		ndtc_entry_size;
	__u32		ndtc_entries;
	__u32		ndtc_last_flush;	/* delta to now in msecs */
	__u32		ndtc_last_rand;		/* delta to now in msecs */
	__u32		ndtc_hash_rnd;
	__u32		ndtc_hash_mask;
	__u32		ndtc_hash_chain_gc;
	__u32		ndtc_proxy_qlen;
};

enum {
	NDTA_UNSPEC,
	NDTA_NAME,			/* char *, unchangeable */
	NDTA_THRESH1,			/* u32 */
	NDTA_THRESH2,			/* u32 */
	NDTA_THRESH3,			/* u32 */
	NDTA_CONFIG,			/* struct ndt_config, read-only */
	NDTA_PARMS,			/* nested TLV NDTPA_* */
	NDTA_STATS,			/* struct ndt_stats, read-only */
	NDTA_GC_INTERVAL,		/* u64, msecs */
	__NDTA_MAX
};
#define NDTA_MAX (__NDTA_MAX - 1)

#define NDTA_RTA(r) ((struct rtattr*)(((char*)(r)) + \
		     NLMSG_ALIGN(sizeof(struct ndtmsg))))
#define NDTA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct ndtmsg))


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

/* The struct should be in sync with struct net_device_stats */
struct rtnl_link_stats
{
	__u32	rx_packets;		/* total packets received	*/
	__u32	tx_packets;		/* total packets transmitted	*/
	__u32	rx_bytes;		/* total bytes received 	*/
	__u32	tx_bytes;		/* total bytes transmitted	*/
	__u32	rx_errors;		/* bad packets received		*/
	__u32	tx_errors;		/* packet transmit problems	*/
	__u32	rx_dropped;		/* no space in linux buffers	*/
	__u32	tx_dropped;		/* no space available in linux	*/
	__u32	multicast;		/* multicast packets received	*/
	__u32	collisions;

	/* detailed rx_errors: */
	__u32	rx_length_errors;
	__u32	rx_over_errors;		/* receiver ring buff overflow	*/
	__u32	rx_crc_errors;		/* recved pkt with crc error	*/
	__u32	rx_frame_errors;	/* recv'd frame alignment error */
	__u32	rx_fifo_errors;		/* recv'r fifo overrun		*/
	__u32	rx_missed_errors;	/* receiver missed packet	*/

	/* detailed tx_errors */
	__u32	tx_aborted_errors;
	__u32	tx_carrier_errors;
	__u32	tx_fifo_errors;
	__u32	tx_heartbeat_errors;
	__u32	tx_window_errors;
	
	/* for cslip etc */
	__u32	rx_compressed;
	__u32	tx_compressed;
};

/* The struct should be in sync with struct ifmap */
struct rtnl_link_ifmap
{
	__u64	mem_start;
	__u64	mem_end;
	__u64	base_addr;
	__u16	irq;
	__u8	dma;
	__u8	port;
};

enum
{
	IFLA_UNSPEC,
	IFLA_ADDRESS,
	IFLA_BROADCAST,
	IFLA_IFNAME,
	IFLA_MTU,
	IFLA_LINK,
	IFLA_QDISC,
	IFLA_STATS,
	IFLA_COST,
#define IFLA_COST IFLA_COST
	IFLA_PRIORITY,
#define IFLA_PRIORITY IFLA_PRIORITY
	IFLA_MASTER,
#define IFLA_MASTER IFLA_MASTER
	IFLA_WIRELESS,		/* Wireless Extension event - see wireless.h */
#define IFLA_WIRELESS IFLA_WIRELESS
	IFLA_PROTINFO,		/* Protocol specific information for a link */
#define IFLA_PROTINFO IFLA_PROTINFO
	IFLA_TXQLEN,
#define IFLA_TXQLEN IFLA_TXQLEN
	IFLA_MAP,
#define IFLA_MAP IFLA_MAP
	IFLA_WEIGHT,
#define IFLA_WEIGHT IFLA_WEIGHT
	IFLA_OPERSTATE,
	IFLA_LINKMODE,
	__IFLA_MAX
};


#define IFLA_MAX (__IFLA_MAX - 1)

#define IFLA_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct ifinfomsg))))
#define IFLA_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct ifinfomsg))

/* ifi_flags.

   IFF_* flags.

   The only change is:
   IFF_LOOPBACK, IFF_BROADCAST and IFF_POINTOPOINT are
   more not changeable by user. They describe link media
   characteristics and set by device driver.

   Comments:
   - Combination IFF_BROADCAST|IFF_POINTOPOINT is invalid
   - If neither of these three flags are set;
     the interface is NBMA.

   - IFF_MULTICAST does not mean anything special:
   multicasts can be used on all not-NBMA links.
   IFF_MULTICAST means that this media uses special encapsulation
   for multicast frames. Apparently, all IFF_POINTOPOINT and
   IFF_BROADCAST devices are able to use multicasts too.
 */

/* IFLA_LINK.
   For usual devices it is equal ifi_index.
   If it is a "virtual interface" (f.e. tunnel), ifi_link
   can point to real physical interface (f.e. for bandwidth calculations),
   or maybe 0, what means, that real media is unknown (usual
   for IPIP tunnels, when route to endpoint is allowed to change)
 */

/* Subtype attributes for IFLA_PROTINFO */
enum
{
	IFLA_INET6_UNSPEC,
	IFLA_INET6_FLAGS,	/* link flags			*/
	IFLA_INET6_CONF,	/* sysctl parameters		*/
	IFLA_INET6_STATS,	/* statistics			*/
	IFLA_INET6_MCAST,	/* MC things. What of them?	*/
	IFLA_INET6_CACHEINFO,	/* time values and max reasm size */
	__IFLA_INET6_MAX
};

#define IFLA_INET6_MAX	(__IFLA_INET6_MAX - 1)

struct ifla_cacheinfo
{
	__u32	max_reasm_len;
	__u32	tstamp;		/* ipv6InterfaceTable updated timestamp */
	__u32	reachable_time;
	__u32	retrans_time;
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
	RTNLGRP_NOP3,
	RTNLGRP_NOP4,
	RTNLGRP_IPV6_PREFIX,
#define RTNLGRP_IPV6_PREFIX	RTNLGRP_IPV6_PREFIX
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

#define rtattr_parse_nested(tb, max, rta) \
	rtattr_parse((tb), (max), RTA_DATA((rta)), RTA_PAYLOAD((rta)))

extern struct sock *rtnl;

struct rtnetlink_link
{
	int (*doit)(struct sk_buff *, struct nlmsghdr*, void *attr);
	int (*dumpit)(struct sk_buff *, struct netlink_callback *cb);
};

extern struct rtnetlink_link * rtnetlink_links[NPROTO];
extern int rtnetlink_send(struct sk_buff *skb, u32 pid, u32 group, int echo);
extern int rtnetlink_put_metrics(struct sk_buff *skb, u32 *metrics);

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
	memset(skb->tail - (RTA_ALIGN(attrlen) - attrlen), 0, \
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
({	struct rtattr *__start = (struct rtattr *) (skb)->tail; \
	RTA_PUT(skb, type, 0, NULL); \
	__start;  })

#define RTA_NEST_END(skb, start) \
({	(start)->rta_len = ((skb)->tail - (unsigned char *) (start)); \
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

#endif /* __KERNEL__ */


#endif	/* __LINUX_RTNETLINK_H */
