/*	$OpenBSD: bgpd.h,v 1.519 2025/03/21 01:06:48 jsg Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __BGPD_H__
#define	__BGPD_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <stdint.h>
#include <imsg.h>

#include "monotime.h"

#define	BGP_VERSION			4
#define	RTR_MAX_VERSION			2
#define	RTR_DEFAULT_VERSION		1
#define	BGP_PORT			179
#define	RTR_PORT			323
#define	CONFFILE			"/etc/bgpd.conf"
#define	BGPD_USER			"_bgpd"
#define	PEER_DESCR_LEN			64
#define	REASON_LEN			256	/* includes NUL terminator */
#define	PFTABLE_LEN			32
#define	ROUTELABEL_LEN			32
#define	TCP_MD5_KEY_LEN			80
#define	IPSEC_ENC_KEY_LEN		32
#define	IPSEC_AUTH_KEY_LEN		20
#define	SET_NAME_LEN			128

#define	MAX_PKTSIZE			4096
#define	MAX_EXT_PKTSIZE			65535
#define	MAX_BGPD_IMSGSIZE		(128 * 1024)
#define	MAX_SOCK_BUF			(4 * IBUF_READ_SIZE)
#define	RT_BUF_SIZE			16384
#define	MAX_RTSOCK_BUF			(2 * 1024 * 1024)
#define	MAX_COMM_MATCH			3
#define	MAX_ASPA_SPAS_COUNT		10000
#define	MIN_HOLDTIME			3

#define	BGPD_OPT_VERBOSE		0x0001
#define	BGPD_OPT_VERBOSE2		0x0002
#define	BGPD_OPT_NOACTION		0x0004
#define	BGPD_OPT_FORCE_DEMOTE		0x0008

#define	BGPD_FLAG_REFLECTOR		0x0004
#define	BGPD_FLAG_NEXTHOP_BGP		0x0010
#define	BGPD_FLAG_NEXTHOP_DEFAULT	0x0020
#define	BGPD_FLAG_DECISION_MASK		0x0f00
#define	BGPD_FLAG_DECISION_ROUTEAGE	0x0100
#define	BGPD_FLAG_DECISION_TRANS_AS	0x0200
#define	BGPD_FLAG_DECISION_MED_ALWAYS	0x0400
#define	BGPD_FLAG_DECISION_ALL_PATHS	0x0800
#define	BGPD_FLAG_PERMIT_AS_SET		0x1000

#define	BGPD_LOG_UPDATES		0x0001

#define	SOCKET_NAME			"/var/run/bgpd.sock"

#define	F_BGPD			0x0001
#define	F_BGPD_INSERTED		0x0002
#define	F_CONNECTED		0x0004
#define	F_STATIC		0x0008
#define	F_NEXTHOP		0x0010
#define	F_REJECT		0x0020
#define	F_BLACKHOLE		0x0040
#define	F_MPLS			0x0080
#define	F_LONGER		0x0200
#define	F_SHORTER		0x0400
#define	F_CTL_DETAIL		0x1000		/* only set on requests */
#define	F_CTL_ADJ_IN		0x2000		/* only set on requests */
#define	F_CTL_ADJ_OUT		0x4000		/* only set on requests */
#define	F_CTL_BEST		0x8000
#define	F_CTL_INELIGIBLE	0x10000		/* only set on requests */
#define	F_CTL_LEAKED		0x20000		/* only set on requests */
#define	F_CTL_INVALID		0x40000		/* only set on requests */
#define	F_CTL_OVS_VALID		0x80000
#define	F_CTL_OVS_INVALID	0x100000
#define	F_CTL_OVS_NOTFOUND	0x200000
#define	F_CTL_NEIGHBORS		0x400000	 /* only used by bgpctl */
#define	F_CTL_HAS_PATHID	0x800000	 /* only set on requests */
#define	F_CTL_AVS_VALID		0x1000000
#define	F_CTL_AVS_INVALID	0x2000000
#define	F_CTL_AVS_UNKNOWN	0x4000000
#define	F_CTL_FILTERED		0x8000000	/* only set on requests */
#define	F_CTL_SSV		0x80000000	/* only used by bgpctl */

#define CTASSERT(x)	extern char  _ctassert[(x) ? 1 : -1 ] \
			    __attribute__((__unused__))

/*
 * Note that these numeric assignments differ from the numbers commonly
 * used in route origin validation context.
 */
#define	ROA_NOTFOUND		0x0	/* default */
#define	ROA_INVALID		0x1
#define	ROA_VALID		0x2
#define	ROA_MASK		0x3

#define	ASPA_UNKNOWN		0x00	/* default */
#define	ASPA_INVALID		0x01
#define	ASPA_VALID		0x02
#define	ASPA_MASK		0x03
#define	ASPA_NEVER_KNOWN	0x08	/* unknown and check never needed */

/*
 * Limit the number of messages queued in the session engine.
 * The SE will send an IMSG_XOFF messages to the RDE if the high water mark
 * is reached. The RDE should then throttle this peer or control connection.
 * Once the message queue in the SE drops below the low water mark an
 * IMSG_XON message will be sent and the RDE will produce more messages again.
 */
#define RDE_RUNNER_ROUNDS	100
#define RDE_REAPER_ROUNDS	5000
#define SESS_MSG_HIGH_MARK	2000
#define SESS_MSG_LOW_MARK	500
#define CTL_MSG_HIGH_MARK	500
#define CTL_MSG_LOW_MARK	100

enum bgpd_process {
	PROC_MAIN,
	PROC_SE,
	PROC_RDE,
	PROC_RTR,
};

enum reconf_action {
	RECONF_NONE,
	RECONF_KEEP,
	RECONF_REINIT,
	RECONF_RELOAD,
	RECONF_DELETE
};

/* Address Family Numbers as per RFC 1700 */
#define	AFI_UNSPEC	0
#define	AFI_IPv4	1
#define	AFI_IPv6	2
#define	AFI_L2VPN	25

/* Subsequent Address Family Identifier as per RFC 4760 */
#define	SAFI_NONE		0
#define	SAFI_UNICAST		1
#define	SAFI_MULTICAST		2
#define	SAFI_MPLS		4
#define	SAFI_EVPN		70	/* RFC 7432 */
#define	SAFI_MPLSVPN		128
#define	SAFI_FLOWSPEC		133
#define	SAFI_VPNFLOWSPEC	134

struct aid {
	uint16_t	 afi;
	sa_family_t	 af;
	uint8_t		 safi;
	const char	*name;
};

extern const struct aid aid_vals[];

#define	AID_UNSPEC	0
#define	AID_INET	1
#define	AID_INET6	2
#define	AID_VPN_IPv4	3
#define	AID_VPN_IPv6	4
#define	AID_FLOWSPECv4	5
#define	AID_FLOWSPECv6	6
#define	AID_EVPN	7
#define	AID_MAX		8
#define	AID_MIN		1	/* skip AID_UNSPEC since that is a dummy */

#define AID_VALS	{					\
	/* afi, af, safii, name */				\
	{ AFI_UNSPEC, AF_UNSPEC, SAFI_NONE, "unspec"},		\
	{ AFI_IPv4, AF_INET, SAFI_UNICAST, "IPv4 unicast" },	\
	{ AFI_IPv6, AF_INET6, SAFI_UNICAST, "IPv6 unicast" },	\
	{ AFI_IPv4, AF_INET, SAFI_MPLSVPN, "IPv4 vpn" },	\
	{ AFI_IPv6, AF_INET6, SAFI_MPLSVPN, "IPv6 vpn" },	\
	{ AFI_IPv4, AF_INET, SAFI_FLOWSPEC, "IPv4 flowspec" },	\
	{ AFI_IPv6, AF_INET6, SAFI_FLOWSPEC, "IPv6 flowspec" },	\
	{ AFI_L2VPN, AF_UNSPEC, SAFI_EVPN, "EVPN" },		\
}

#define BGP_MPLS_BOS	0x01
#define ESI_ADDR_LEN	10

#define EVPN_ROUTE_TYPE_2	0x02
#define EVPN_ROUTE_TYPE_3	0x03
#define EVPN_ROUTE_TYPE_5	0x05

struct evpn_addr {
	union {
		struct in_addr	v4;
		struct in6_addr	v6;
	};
	uint32_t	ethtag;
	uint8_t		mac[ETHER_ADDR_LEN];
	uint8_t		esi[ESI_ADDR_LEN];
	uint8_t		aid;
	uint8_t		type;
};

struct bgpd_addr {
	union {
		struct in_addr		v4;
		struct in6_addr		v6;
		struct evpn_addr	evpn;
		/* maximum size for a prefix is 256 bits */
	};		    /* 128-bit address */
	uint64_t	rd;		/* route distinguisher for VPN addrs */
	uint32_t	scope_id;	/* iface scope id for v6 */
	uint8_t		aid;
	uint8_t		labellen;	/* size of the labelstack */
	uint8_t		labelstack[18];	/* max that makes sense */
};

#define	DEFAULT_LISTENER	0x01
#define	LISTENER_LISTENING	0x02

struct listen_addr {
	TAILQ_ENTRY(listen_addr)	entry;
	struct sockaddr_storage		sa;
	int				fd;
	enum reconf_action		reconf;
	socklen_t			sa_len;
	uint8_t				flags;
};

TAILQ_HEAD(listen_addrs, listen_addr);
TAILQ_HEAD(filter_set_head, filter_set);

struct peer;
RB_HEAD(peer_head, peer);

struct l3vpn;
SIMPLEQ_HEAD(l3vpn_head, l3vpn);

struct network;
TAILQ_HEAD(network_head, network);

struct flowspec_config;
RB_HEAD(flowspec_tree, flowspec_config);

struct prefixset;
SIMPLEQ_HEAD(prefixset_head, prefixset);
struct prefixset_item;
RB_HEAD(prefixset_tree, prefixset_item);

struct tentry_v4;
struct tentry_v6;
struct trie_head {
	struct tentry_v4	*root_v4;
	struct tentry_v6	*root_v6;
	int			 match_default_v4;
	int			 match_default_v6;
	size_t			 v4_cnt;
	size_t			 v6_cnt;
};

struct rde_prefixset {
	char				name[SET_NAME_LEN];
	struct trie_head		th;
	SIMPLEQ_ENTRY(rde_prefixset)	entry;
	monotime_t			lastchange;
	int				dirty;
};
SIMPLEQ_HEAD(rde_prefixset_head, rde_prefixset);

struct roa {
	RB_ENTRY(roa)	entry;
	uint8_t		aid;
	uint8_t		prefixlen;
	uint8_t		maxlen;
	uint8_t		pad;
	uint32_t	asnum;
	time_t		expires;
	union {
		struct in_addr	inet;
		struct in6_addr	inet6;
	}		prefix;
};

RB_HEAD(roa_tree, roa);
struct aspa_set;
RB_HEAD(aspa_tree, aspa_set);

struct set_table;
struct as_set;
SIMPLEQ_HEAD(as_set_head, as_set);

struct filter_rule;
TAILQ_HEAD(filter_head, filter_rule);

struct rtr_config;
SIMPLEQ_HEAD(rtr_config_head, rtr_config);

struct bgpd_config {
	struct peer_head			 peers;
	struct l3vpn_head			 l3vpns;
	struct network_head			 networks;
	struct flowspec_tree			 flowspecs;
	struct filter_head			*filters;
	struct listen_addrs			*listen_addrs;
	struct mrt_head				*mrt;
	struct prefixset_head			 prefixsets;
	struct prefixset_head			 originsets;
	struct roa_tree				 roa;
	struct aspa_tree			 aspa;
	struct rde_prefixset_head		 rde_prefixsets;
	struct rde_prefixset_head		 rde_originsets;
	struct as_set_head			 as_sets;
	struct rtr_config_head			 rtrs;
	char					*csock;
	char					*rcsock;
	int					 flags;
	int					 log;
	u_int					 default_tableid;
	uint32_t				 bgpid;
	uint32_t				 clusterid;
	uint32_t				 as;
	uint16_t				 short_as;
	uint16_t				 holdtime;
	uint16_t				 min_holdtime;
	uint16_t				 connectretry;
	uint16_t				 staletime;
	uint8_t					 fib_priority;
	uint8_t					 filtered_in_locrib;
};

extern int cmd_opts;

enum addpath_mode {
	ADDPATH_EVAL_NONE,
	ADDPATH_EVAL_BEST,
	ADDPATH_EVAL_ECMP,
	ADDPATH_EVAL_AS_WIDE,
	ADDPATH_EVAL_ALL,
};

struct addpath_eval {
	enum addpath_mode	mode;
	int			extrapaths;
	int			maxpaths;
};

enum export_type {
	EXPORT_UNSET,
	EXPORT_NONE,
	EXPORT_DEFAULT_ROUTE
};

enum enforce_as {
	ENFORCE_AS_UNDEF,
	ENFORCE_AS_OFF,
	ENFORCE_AS_ON
};

enum role {
	ROLE_NONE,
	ROLE_CUSTOMER,
	ROLE_PROVIDER,
	ROLE_RS,
	ROLE_RS_CLIENT,
	ROLE_PEER,
};

enum auth_method {
	AUTH_NONE,
	AUTH_MD5SIG,
	AUTH_IPSEC_MANUAL_ESP,
	AUTH_IPSEC_MANUAL_AH,
	AUTH_IPSEC_IKE_ESP,
	AUTH_IPSEC_IKE_AH
};

enum auth_alg {
	AUTH_AALG_NONE,
	AUTH_AALG_SHA1HMAC,
	AUTH_AALG_MD5HMAC,
};

enum auth_enc_alg {
	AUTH_EALG_NONE,
	AUTH_EALG_3DESCBC,
	AUTH_EALG_AES,
};

struct auth_config {
	char			md5key[TCP_MD5_KEY_LEN];
	char			auth_key_in[IPSEC_AUTH_KEY_LEN];
	char			auth_key_out[IPSEC_AUTH_KEY_LEN];
	char			enc_key_in[IPSEC_ENC_KEY_LEN];
	char			enc_key_out[IPSEC_ENC_KEY_LEN];
	uint32_t		spi_in;
	uint32_t		spi_out;
	enum auth_method	method;
	enum auth_alg		auth_alg_in;
	enum auth_alg		auth_alg_out;
	enum auth_enc_alg	enc_alg_in;
	enum auth_enc_alg	enc_alg_out;
	uint8_t			md5key_len;
	uint8_t			auth_keylen_in;
	uint8_t			auth_keylen_out;
	uint8_t			enc_keylen_in;
	uint8_t			enc_keylen_out;
};

struct capabilities {
	struct {
		int16_t	timeout;	/* graceful restart timeout */
		int8_t	flags[AID_MAX];	/* graceful restart per AID flags */
		int8_t	restart;	/* graceful restart, RFC 4724 */
		int8_t	grnotification;	/* graceful notification, RFC 8538 */
	}	grestart;
	int8_t	mp[AID_MAX];		/* multiprotocol extensions, RFC 4760 */
	int8_t	add_path[AID_MAX];	/* ADD_PATH, RFC 7911 */
	int8_t	ext_nh[AID_MAX];	/* Ext Nexthop Encoding, RFC 8950 */
	int8_t	refresh;		/* route refresh, RFC 2918 */
	int8_t	as4byte;		/* 4-byte ASnum, RFC 4893 */
	int8_t	enhanced_rr;		/* enhanced route refresh, RFC 7313 */
	int8_t	policy;			/* Open Policy, RFC 9234, 2 = enforce */
	int8_t	ext_msg;		/* Extended Msg, RFC 8654 */
};

enum capa_codes {
	CAPA_NONE = 0,
	CAPA_MP = 1,
	CAPA_REFRESH = 2,
	CAPA_EXT_NEXTHOP = 5,
	CAPA_EXT_MSG = 6,
	CAPA_ROLE = 9,
	CAPA_RESTART = 64,
	CAPA_AS4BYTE = 65,
	CAPA_ADD_PATH = 69,
	CAPA_ENHANCED_RR = 70,
};

/* flags for RFC 4724 - graceful restart */
#define	CAPA_GR_PRESENT		0x01
#define	CAPA_GR_RESTART		0x02
#define	CAPA_GR_FORWARD		0x04
#define	CAPA_GR_RESTARTING	0x08
#define	CAPA_GR_TIMEMASK	0x0fff
#define	CAPA_GR_R_FLAG		0x8000
#define	CAPA_GR_N_FLAG		0x4000
#define	CAPA_GR_F_FLAG		0x80

/* flags for RFC 7911 - enhanced router refresh */
#define	CAPA_AP_RECV		0x01
#define	CAPA_AP_SEND		0x02
#define	CAPA_AP_BIDIR		0x03
#define	CAPA_AP_MASK		0x0f
#define	CAPA_AP_RECV_ENFORCE	0x10	/* internal only */
#define	CAPA_AP_SEND_ENFORCE	0x20	/* internal only */

/* values for RFC 9234 - BGP Open Policy */
#define CAPA_ROLE_PROVIDER	0x00
#define CAPA_ROLE_RS		0x01
#define CAPA_ROLE_RS_CLIENT	0x02
#define CAPA_ROLE_CUSTOMER	0x03
#define CAPA_ROLE_PEER		0x04

struct peer_config {
	struct bgpd_addr	 remote_addr;
	struct bgpd_addr	 local_addr_v4;
	struct bgpd_addr	 local_addr_v6;
	struct capabilities	 capabilities;
	struct addpath_eval	 eval;
	char			 group[PEER_DESCR_LEN];
	char			 descr[PEER_DESCR_LEN];
	char			 reason[REASON_LEN];
	char			 rib[PEER_DESCR_LEN];
	char			 if_depend[IFNAMSIZ];
	char			 demote_group[IFNAMSIZ];
	uint32_t		 id;
	uint32_t		 groupid;
	uint32_t		 remote_as;
	uint32_t		 local_as;
	uint32_t		 max_prefix;
	uint32_t		 max_out_prefix;
	enum export_type	 export_type;
	enum enforce_as		 enforce_as;
	enum enforce_as		 enforce_local_as;
	enum role		 role;
	uint16_t		 max_prefix_restart;
	uint16_t		 max_out_prefix_restart;
	uint16_t		 holdtime;
	uint16_t		 min_holdtime;
	uint16_t		 connectretry;
	uint16_t		 staletime;
	uint16_t		 local_short_as;
	uint16_t		 remote_port;
	uint8_t			 template;
	uint8_t			 remote_masklen;
	uint8_t			 ebgp;		/* 0 = ibgp else ebgp */
	uint8_t			 distance;	/* 1 = direct, >1 = multihop */
	uint8_t			 passive;
	uint8_t			 down;
	uint8_t			 reflector_client;
	uint8_t			 ttlsec;	/* TTL security hack */
	uint8_t			 flags;
};

#define	PEER_ID_NONE		0
#define	PEER_ID_SELF		1
#define	PEER_ID_STATIC_MIN	2	/* exclude self */
#define	PEER_ID_STATIC_MAX	(UINT_MAX / 2)
#define	PEER_ID_DYN_MAX		UINT_MAX

#define PEERFLAG_TRANS_AS	0x01
#define PEERFLAG_LOG_UPDATES	0x02
#define PEERFLAG_EVALUATE_ALL	0x04
#define PEERFLAG_PERMIT_AS_SET	0x08

struct rde_peer_stats {
	uint64_t			 prefix_rcvd_update;
	uint64_t			 prefix_rcvd_withdraw;
	uint64_t			 prefix_rcvd_eor;
	uint64_t			 prefix_sent_update;
	uint64_t			 prefix_sent_withdraw;
	uint64_t			 prefix_sent_eor;
	uint32_t			 prefix_cnt;
	uint32_t			 prefix_out_cnt;
	uint32_t			 pending_update;
	uint32_t			 pending_withdraw;
};

enum network_type {
	NETWORK_DEFAULT,	/* from network statements */
	NETWORK_STATIC,
	NETWORK_CONNECTED,
	NETWORK_RTLABEL,
	NETWORK_MRTCLONE,
	NETWORK_PRIORITY,
	NETWORK_PREFIXSET,
};

struct network_config {
	struct bgpd_addr	 prefix;
	struct filter_set_head	 attrset;
	char			 psname[SET_NAME_LEN];
	uint64_t		 rd;
	enum network_type	 type;
	uint16_t		 rtlabel;
	uint8_t			 prefixlen;
	uint8_t			 priority;
	uint8_t			 old;	/* used for reloading */
};

struct network {
	struct network_config	net;
	TAILQ_ENTRY(network)	entry;
};

struct flowspec {
	uint16_t		len;
	uint8_t			aid;
	uint8_t			flags;
	uint8_t			data[1];
};
#define FLOWSPEC_SIZE	(offsetof(struct flowspec, data))

struct flowspec_config {
	RB_ENTRY(flowspec_config)	 entry;
	struct filter_set_head		 attrset;
	struct flowspec			*flow;
	enum reconf_action		 reconf_action;
};

enum rtr_error {
	NO_ERROR = -1,
	CORRUPT_DATA = 0,
	INTERNAL_ERROR,
	NO_DATA_AVAILABLE,
	INVALID_REQUEST,
	UNSUPP_PROTOCOL_VERS,
	UNSUPP_PDU_TYPE,
	UNK_REC_WDRAWL,
	DUP_REC_RECV,
	UNEXP_PROTOCOL_VERS,
};

struct rtr_config {
	SIMPLEQ_ENTRY(rtr_config)	entry;
	char				descr[PEER_DESCR_LEN];
	struct auth_config		auth;
	struct bgpd_addr		remote_addr;
	struct bgpd_addr		local_addr;
	uint32_t			id;
	uint16_t			remote_port;
	uint8_t				min_version;
};

struct rtr_config_msg {
	char				descr[PEER_DESCR_LEN];
	uint8_t				min_version;
};

struct ctl_show_rtr {
	char			descr[PEER_DESCR_LEN];
	char			state[PEER_DESCR_LEN];
	char			last_sent_msg[REASON_LEN];
	char			last_recv_msg[REASON_LEN];
	struct bgpd_addr	remote_addr;
	struct bgpd_addr	local_addr;
	uint32_t		serial;
	uint32_t		refresh;
	uint32_t		retry;
	uint32_t		expire;
	int			session_id;
	enum rtr_error		last_sent_error;
	enum rtr_error		last_recv_error;
	uint16_t		remote_port;
	uint8_t			version;
	uint8_t			min_version;
};

enum imsg_type {
	IMSG_NONE,
	IMSG_CTL_END,
	IMSG_CTL_RELOAD,
	IMSG_CTL_FIB_COUPLE,
	IMSG_CTL_FIB_DECOUPLE,
	IMSG_CTL_NEIGHBOR_UP,
	IMSG_CTL_NEIGHBOR_DOWN,
	IMSG_CTL_NEIGHBOR_CLEAR,
	IMSG_CTL_NEIGHBOR_RREFRESH,
	IMSG_CTL_NEIGHBOR_DESTROY,
	IMSG_CTL_KROUTE,
	IMSG_CTL_KROUTE_ADDR,
	IMSG_CTL_RESULT,
	IMSG_CTL_SHOW_NEIGHBOR,
	IMSG_CTL_SHOW_NEXTHOP,
	IMSG_CTL_SHOW_INTERFACE,
	IMSG_CTL_SHOW_RIB,
	IMSG_CTL_SHOW_RIB_PREFIX,
	IMSG_CTL_SHOW_RIB_COMMUNITIES,
	IMSG_CTL_SHOW_RIB_ATTR,
	IMSG_CTL_SHOW_NETWORK,
	IMSG_CTL_SHOW_FLOWSPEC,
	IMSG_CTL_SHOW_RIB_MEM,
	IMSG_CTL_SHOW_TERSE,
	IMSG_CTL_SHOW_TIMER,
	IMSG_CTL_LOG_VERBOSE,
	IMSG_CTL_SHOW_FIB_TABLES,
	IMSG_CTL_SHOW_SET,
	IMSG_CTL_SHOW_RTR,
	IMSG_CTL_TERMINATE,
	IMSG_NETWORK_ADD,
	IMSG_NETWORK_ASPATH,
	IMSG_NETWORK_ATTR,
	IMSG_NETWORK_REMOVE,
	IMSG_NETWORK_FLUSH,
	IMSG_NETWORK_DONE,
	IMSG_FLOWSPEC_ADD,
	IMSG_FLOWSPEC_DONE,
	IMSG_FLOWSPEC_REMOVE,
	IMSG_FLOWSPEC_FLUSH,
	IMSG_FILTER_SET,
	IMSG_SOCKET_CONN,
	IMSG_SOCKET_CONN_CTL,
	IMSG_SOCKET_CONN_RTR,
	IMSG_SOCKET_SETUP,
	IMSG_SOCKET_TEARDOWN,
	IMSG_RECONF_CONF,
	IMSG_RECONF_RIB,
	IMSG_RECONF_PEER,
	IMSG_RECONF_PEER_AUTH,
	IMSG_RECONF_FILTER,
	IMSG_RECONF_LISTENER,
	IMSG_RECONF_CTRL,
	IMSG_RECONF_VPN,
	IMSG_RECONF_VPN_EXPORT,
	IMSG_RECONF_VPN_IMPORT,
	IMSG_RECONF_VPN_DONE,
	IMSG_RECONF_PREFIX_SET,
	IMSG_RECONF_PREFIX_SET_ITEM,
	IMSG_RECONF_AS_SET,
	IMSG_RECONF_AS_SET_ITEMS,
	IMSG_RECONF_AS_SET_DONE,
	IMSG_RECONF_ORIGIN_SET,
	IMSG_RECONF_ROA_SET,
	IMSG_RECONF_ROA_ITEM,
	IMSG_RECONF_ASPA,
	IMSG_RECONF_ASPA_TAS,
	IMSG_RECONF_ASPA_DONE,
	IMSG_RECONF_ASPA_PREP,
	IMSG_RECONF_RTR_CONFIG,
	IMSG_RECONF_DRAIN,
	IMSG_RECONF_DONE,
	IMSG_UPDATE,
	IMSG_UPDATE_ERR,
	IMSG_SESSION_ADD,
	IMSG_SESSION_UP,
	IMSG_SESSION_DOWN,
	IMSG_SESSION_DELETE,
	IMSG_SESSION_STALE,
	IMSG_SESSION_NOGRACE,
	IMSG_SESSION_FLUSH,
	IMSG_SESSION_RESTARTED,
	IMSG_SESSION_DEPENDON,
	IMSG_PFKEY_RELOAD,
	IMSG_MRT_OPEN,
	IMSG_MRT_REOPEN,
	IMSG_MRT_CLOSE,
	IMSG_KROUTE_CHANGE,
	IMSG_KROUTE_DELETE,
	IMSG_KROUTE_FLUSH,
	IMSG_NEXTHOP_ADD,
	IMSG_NEXTHOP_REMOVE,
	IMSG_NEXTHOP_UPDATE,
	IMSG_PFTABLE_ADD,
	IMSG_PFTABLE_REMOVE,
	IMSG_PFTABLE_COMMIT,
	IMSG_REFRESH,
	IMSG_DEMOTE,
	IMSG_XON,
	IMSG_XOFF
};

struct demote_msg {
	char		 demote_group[IFNAMSIZ];
	int		 level;
};

enum ctl_results {
	CTL_RES_OK,
	CTL_RES_NOSUCHPEER,
	CTL_RES_DENIED,
	CTL_RES_NOCAP,
	CTL_RES_PARSE_ERROR,
	CTL_RES_PENDING,
	CTL_RES_NOMEM,
	CTL_RES_BADPEER,
	CTL_RES_BADSTATE,
	CTL_RES_NOSUCHRIB,
	CTL_RES_OPNOTSUPP,
};

/* needed for session.h parse prototype */
LIST_HEAD(mrt_head, mrt);

/* error codes and subcodes needed in SE and RDE */
enum err_codes {
	ERR_HEADER = 1,
	ERR_OPEN,
	ERR_UPDATE,
	ERR_HOLDTIMEREXPIRED,
	ERR_FSM,
	ERR_CEASE,
	ERR_RREFRESH,
	ERR_SENDHOLDTIMEREXPIRED,
};

enum suberr_update {
	ERR_UPD_UNSPECIFIC,
	ERR_UPD_ATTRLIST,
	ERR_UPD_UNKNWN_WK_ATTR,
	ERR_UPD_MISSNG_WK_ATTR,
	ERR_UPD_ATTRFLAGS,
	ERR_UPD_ATTRLEN,
	ERR_UPD_ORIGIN,
	ERR_UPD_LOOP,
	ERR_UPD_NEXTHOP,
	ERR_UPD_OPTATTR,
	ERR_UPD_NETWORK,
	ERR_UPD_ASPATH
};

enum suberr_cease {
	ERR_CEASE_MAX_PREFIX = 1,
	ERR_CEASE_ADMIN_DOWN,
	ERR_CEASE_PEER_UNCONF,
	ERR_CEASE_ADMIN_RESET,
	ERR_CEASE_CONN_REJECT,
	ERR_CEASE_OTHER_CHANGE,
	ERR_CEASE_COLLISION,
	ERR_CEASE_RSRC_EXHAUST,
	ERR_CEASE_HARD_RESET,
	ERR_CEASE_MAX_SENT_PREFIX
};

enum suberr_rrefresh {
	ERR_RR_INV_LEN = 1
};

struct kroute;
struct kroute6;
struct knexthop;
struct kredist_node;
RB_HEAD(kroute_tree, kroute);
RB_HEAD(kroute6_tree, kroute6);
RB_HEAD(knexthop_tree, knexthop);
RB_HEAD(kredist_tree, kredist_node);

struct ktable {
	char			 descr[PEER_DESCR_LEN];
	struct kroute_tree	 krt;
	struct kroute6_tree	 krt6;
	struct knexthop_tree	 knt;
	struct kredist_tree	 kredist;
	struct network_head	 krn;
	u_int			 rtableid;
	u_int			 nhtableid; /* rdomain id for nexthop lookup */
	int			 nhrefcnt;  /* refcnt for nexthop table */
	enum reconf_action	 state;
	uint8_t			 fib_conf;  /* configured FIB sync flag */
	uint8_t			 fib_sync;  /* is FIB synced with kernel? */
};

struct kroute_full {
	struct bgpd_addr	prefix;
	struct bgpd_addr	nexthop;
	char			label[ROUTELABEL_LEN];
	uint32_t		mplslabel;
	uint16_t		flags;
	u_short			ifindex;
	uint8_t			prefixlen;
	uint8_t			priority;
};

struct kroute_nexthop {
	struct bgpd_addr	nexthop;
	struct bgpd_addr	gateway;
	struct bgpd_addr	net;
	uint8_t			netlen;
	uint8_t			valid;
	uint8_t			connected;
};

struct session_dependon {
	char			 ifname[IFNAMSIZ];
	uint8_t			 depend_state;	/* for session depend on */
};

struct session_up {
	struct bgpd_addr	local_v4_addr;
	struct bgpd_addr	local_v6_addr;
	struct bgpd_addr	remote_addr;
	struct capabilities	capa;
	uint32_t		remote_bgpid;
	unsigned int		if_scope;
	uint16_t		short_as;
};

struct route_refresh {
	uint8_t			aid;
	uint8_t			subtype;
};
#define ROUTE_REFRESH_REQUEST	0
#define ROUTE_REFRESH_BEGIN_RR	1
#define ROUTE_REFRESH_END_RR	2

struct pftable_msg {
	struct bgpd_addr	addr;
	char			pftable[PFTABLE_LEN];
	uint8_t			len;
};

struct ctl_show_interface {
	char			 ifname[IFNAMSIZ];
	char			 linkstate[32];
	char			 media[32];
	uint64_t		 baudrate;
	u_int			 rdomain;
	uint8_t			 nh_reachable;
	uint8_t			 is_up;
};

struct ctl_show_nexthop {
	struct bgpd_addr		addr;
	struct ctl_show_interface	iface;
	struct kroute_full		kr;
	uint8_t				valid;
	uint8_t				krvalid;
};

struct ctl_show_set {
	char			name[SET_NAME_LEN];
	monotime_t		lastchange;
	size_t			v4_cnt;
	size_t			v6_cnt;
	size_t			as_cnt;
	enum {
		ASNUM_SET,
		PREFIX_SET,
		ORIGIN_SET,
		ROA_SET,
		ASPA_SET,
	}			type;
};

struct ctl_neighbor {
	struct bgpd_addr	addr;
	char			descr[PEER_DESCR_LEN];
	char			reason[REASON_LEN];
	int			show_timers;
	int			is_group;
};

#define	F_PREF_ELIGIBLE	0x001
#define	F_PREF_BEST	0x002
#define	F_PREF_INTERNAL	0x004
#define	F_PREF_ANNOUNCE	0x008
#define	F_PREF_STALE	0x010
#define	F_PREF_INVALID	0x020
#define	F_PREF_PATH_ID	0x040
#define	F_PREF_OTC_LEAK	0x080
#define	F_PREF_ECMP	0x100
#define	F_PREF_AS_WIDE	0x200
#define	F_PREF_FILTERED	0x400

struct ctl_show_rib {
	struct bgpd_addr	true_nexthop;
	struct bgpd_addr	exit_nexthop;
	struct bgpd_addr	prefix;
	struct bgpd_addr	remote_addr;
	char			descr[PEER_DESCR_LEN];
	monotime_t		lastchange;
	uint32_t		remote_id;
	uint32_t		path_id;
	uint32_t		local_pref;
	uint32_t		med;
	uint32_t		weight;
	uint32_t		flags;
	uint8_t			prefixlen;
	uint8_t			origin;
	uint8_t			roa_validation_state;
	uint8_t			aspa_validation_state;
	int8_t			dmetric;
	/* plus an aspath */
};

enum as_spec {
	AS_UNDEF,
	AS_ALL,
	AS_SOURCE,
	AS_TRANSIT,
	AS_PEER,
	AS_EMPTY
};

enum aslen_spec {
	ASLEN_NONE,
	ASLEN_MAX,
	ASLEN_SEQ
};

#define AS_FLAG_NEIGHBORAS	0x01
#define AS_FLAG_AS_SET_NAME	0x02
#define AS_FLAG_AS_SET		0x04

struct filter_as {
	char		 name[SET_NAME_LEN];
	struct as_set	*aset;
	uint32_t	 as_min;
	uint32_t	 as_max;
	enum as_spec	 type;
	uint8_t		 flags;
	uint8_t		 op;
};

struct filter_aslen {
	u_int		aslen;
	enum aslen_spec	type;
};

#define PREFIXSET_FLAG_FILTER	0x01
#define PREFIXSET_FLAG_DIRTY	0x02	/* prefix-set changed at reload */
#define PREFIXSET_FLAG_OPS	0x04	/* indiv. prefixes have prefixlenops */
#define PREFIXSET_FLAG_LONGER	0x08	/* filter all prefixes with or-longer */

struct filter_prefixset {
	int			 flags;
	char			 name[SET_NAME_LEN];
	struct rde_prefixset	*ps;
};

struct filter_originset {
	char			 name[SET_NAME_LEN];
	struct rde_prefixset	*ps;
};

struct filter_vs {
	uint8_t			 validity;
	uint8_t			 is_set;
};

/*
 * Communities are encoded depending on their type. The low byte of flags
 * is the COMMUNITY_TYPE (BASIC, LARGE, EXT). BASIC encoding is just using
 * data1 and data2, LARGE uses all data fields and EXT is also using all
 * data fields. The 4-byte flags fields consists of up to 3 data flags
 * for e.g. COMMUNITY_ANY and the low byte is the community type.
 * If flags is 0 the community struct is unused. If the upper 24bit of
 * flags is 0 a fast compare can be used.
 * The code uses a type cast to uint8_t to access the type.
 */
struct community {
	uint32_t	flags;
	uint32_t	data1;
	uint32_t	data2;
	uint32_t	data3;
};

struct ctl_show_rib_request {
	char			rib[PEER_DESCR_LEN];
	struct ctl_neighbor	neighbor;
	struct bgpd_addr	prefix;
	struct filter_as	as;
	struct community	community;
	uint32_t		flags;
	uint32_t		path_id;
	pid_t			pid;
	enum imsg_type		type;
	uint8_t			validation_state;
	uint8_t			prefixlen;
	uint8_t			aid;
};

struct ctl_kroute_req {
	int			flags;
	sa_family_t		af;
};

enum filter_actions {
	ACTION_NONE,
	ACTION_ALLOW,
	ACTION_DENY
};

enum directions {
	DIR_IN = 1,
	DIR_OUT
};

enum from_spec {
	FROM_ALL,
	FROM_ADDRESS,
	FROM_DESCR,
	FROM_GROUP
};

enum comp_ops {
	OP_NONE,
	OP_RANGE,
	OP_XRANGE,
	OP_EQ,
	OP_NE,
	OP_LE,
	OP_LT,
	OP_GE,
	OP_GT
};

struct filter_peers {
	uint32_t	peerid;
	uint32_t	groupid;
	uint32_t	remote_as;
	uint16_t	ribid;
	uint8_t		ebgp;
	uint8_t		ibgp;
};

/* special community type, keep in sync with the attribute type */
#define	COMMUNITY_TYPE_NONE		0
#define	COMMUNITY_TYPE_BASIC		8
#define	COMMUNITY_TYPE_EXT		16
#define	COMMUNITY_TYPE_LARGE		32

#define	COMMUNITY_ANY			1
#define	COMMUNITY_NEIGHBOR_AS		2
#define	COMMUNITY_LOCAL_AS		3

/* wellknown community definitions */
#define	COMMUNITY_WELLKNOWN		0xffff
#define	COMMUNITY_GRACEFUL_SHUTDOWN	0x0000  /* RFC 8326 */
#define	COMMUNITY_BLACKHOLE		0x029A	/* RFC 7999 */
#define	COMMUNITY_NO_EXPORT		0xff01
#define	COMMUNITY_NO_ADVERTISE		0xff02
#define	COMMUNITY_NO_EXPSUBCONFED	0xff03
#define	COMMUNITY_NO_PEER		0xff04	/* RFC 3765 */

/* extended community definitions */
#define EXT_COMMUNITY_IANA		0x80
#define EXT_COMMUNITY_NON_TRANSITIVE	0x40
#define EXT_COMMUNITY_VALUE		0x3f
/* extended transitive types */
#define EXT_COMMUNITY_TRANS_TWO_AS	0x00	/* 2 octet AS specific */
#define EXT_COMMUNITY_TRANS_IPV4	0x01	/* IPv4 specific */
#define EXT_COMMUNITY_TRANS_FOUR_AS	0x02	/* 4 octet AS specific */
#define EXT_COMMUNITY_TRANS_OPAQUE	0x03	/* opaque ext community */
#define EXT_COMMUNITY_TRANS_EVPN	0x06	/* EVPN RFC 7432 */
/* extended non-transitive types */
#define EXT_COMMUNITY_NON_TRANS_TWO_AS	0x40	/* 2 octet AS specific */
#define EXT_COMMUNITY_NON_TRANS_IPV4	0x41	/* IPv4 specific */
#define EXT_COMMUNITY_NON_TRANS_FOUR_AS	0x42	/* 4 octet AS specific */
#define EXT_COMMUNITY_NON_TRANS_OPAQUE	0x43	/* opaque ext community */
#define EXT_COMMUNITY_UNKNOWN		-1
/* generic transitive types */
#define EXT_COMMUNITY_GEN_TWO_AS	0x80	/* 2 octet AS specific */
#define EXT_COMMUNITY_GEN_IPV4		0x81	/* IPv4 specific */
#define EXT_COMMUNITY_GEN_FOUR_AS	0x82	/* 4 octet AS specific */

/* BGP Origin Validation State Extended Community RFC 8097 */
#define EXT_COMMUNITY_SUBTYPE_OVS	0
#define EXT_COMMUNITY_OVS_VALID		0
#define EXT_COMMUNITY_OVS_NOTFOUND	1
#define EXT_COMMUNITY_OVS_INVALID	2

/* other handy defines */
#define EXT_COMMUNITY_OPAQUE_MAX	0xffffffffffffULL
#define EXT_COMMUNITY_FLAG_VALID	0x01

struct ext_comm_pairs {
	uint8_t		type;
	uint8_t		subtype;
	const char	*subname;
};

#define IANA_EXT_COMMUNITIES	{				\
	{ EXT_COMMUNITY_TRANS_TWO_AS, 0x02, "rt" },		\
	{ EXT_COMMUNITY_TRANS_TWO_AS, 0x03, "soo" },		\
	{ EXT_COMMUNITY_TRANS_TWO_AS, 0x05, "odi" },		\
	{ EXT_COMMUNITY_TRANS_TWO_AS, 0x08, "bdc" },		\
	{ EXT_COMMUNITY_TRANS_TWO_AS, 0x09, "srcas" },		\
	{ EXT_COMMUNITY_TRANS_TWO_AS, 0x0a, "l2vid" },		\
								\
	{ EXT_COMMUNITY_TRANS_FOUR_AS, 0x02, "rt" },		\
	{ EXT_COMMUNITY_TRANS_FOUR_AS, 0x03, "soo" },		\
	{ EXT_COMMUNITY_TRANS_FOUR_AS, 0x05, "odi" },		\
	{ EXT_COMMUNITY_TRANS_FOUR_AS, 0x08, "bdc" },		\
	{ EXT_COMMUNITY_TRANS_FOUR_AS, 0x09, "srcas" },		\
								\
	{ EXT_COMMUNITY_TRANS_IPV4, 0x02, "rt" },		\
	{ EXT_COMMUNITY_TRANS_IPV4, 0x03, "soo" },		\
	{ EXT_COMMUNITY_TRANS_IPV4, 0x05, "odi" },		\
	{ EXT_COMMUNITY_TRANS_IPV4, 0x07, "ori" },		\
	{ EXT_COMMUNITY_TRANS_IPV4, 0x0a, "l2vid" },		\
	{ EXT_COMMUNITY_TRANS_IPV4, 0x0b, "vrfri" },		\
								\
	{ EXT_COMMUNITY_TRANS_OPAQUE, 0x06, "ort" },		\
	{ EXT_COMMUNITY_TRANS_OPAQUE, 0x0c, "encap" },		\
	{ EXT_COMMUNITY_TRANS_OPAQUE, 0x0d, "defgw" },		\
								\
	{ EXT_COMMUNITY_NON_TRANS_OPAQUE, EXT_COMMUNITY_SUBTYPE_OVS, "ovs" }, \
								\
	{ EXT_COMMUNITY_TRANS_EVPN, 0x00, "mac-mob" },		\
	{ EXT_COMMUNITY_TRANS_EVPN, 0x01, "esi-lab" },		\
	{ EXT_COMMUNITY_TRANS_EVPN, 0x02, "esi-rt" },		\
								\
	{ EXT_COMMUNITY_GEN_TWO_AS, 0x06, "flow-rate" },	\
	{ EXT_COMMUNITY_GEN_TWO_AS, 0x0c, "flow-pps" },		\
	{ EXT_COMMUNITY_GEN_TWO_AS, 0x07, "flow-action" },	\
	{ EXT_COMMUNITY_GEN_TWO_AS, 0x08, "flow-rt-redir" },	\
	{ EXT_COMMUNITY_GEN_IPV4,   0x08, "flow-rt-redir" },	\
	{ EXT_COMMUNITY_GEN_FOUR_AS, 0x08, "flow-rt-redir" },	\
	{ EXT_COMMUNITY_GEN_TWO_AS, 0x09, "flow-dscp" },	\
								\
	{ 0 }							\
}

extern const struct ext_comm_pairs iana_ext_comms[];

/* BGP flowspec defines RFC 8955 and 8956 */
#define FLOWSPEC_LEN_LIMIT	0xf0
#define FLOWSPEC_OP_EOL		0x80
#define FLOWSPEC_OP_AND		0x40
#define FLOWSPEC_OP_LEN_MASK	0x30
#define FLOWSPEC_OP_LEN_SHIFT	4
#define FLOWSPEC_OP_LEN(op)					\
	(1 << (((op) & FLOWSPEC_OP_LEN_MASK) >> FLOWSPEC_OP_LEN_SHIFT))
#define FLOWSPEC_OP_NUM_LT	0x04
#define FLOWSPEC_OP_NUM_GT	0x02
#define FLOWSPEC_OP_NUM_EQ	0x01
#define FLOWSPEC_OP_NUM_LE	(FLOWSPEC_OP_NUM_LT | FLOWSPEC_OP_NUM_EQ)
#define FLOWSPEC_OP_NUM_GE	(FLOWSPEC_OP_NUM_GT | FLOWSPEC_OP_NUM_EQ)
#define FLOWSPEC_OP_NUM_NOT	(FLOWSPEC_OP_NUM_GT | FLOWSPEC_OP_NUM_LT)
#define FLOWSPEC_OP_NUM_MASK	0x07
#define FLOWSPEC_OP_BIT_NOT	0x02
#define FLOWSPEC_OP_BIT_MATCH	0x01
#define FLOWSPEC_OP_BIT_MASK	0x03

#define FLOWSPEC_TYPE_MIN		1
#define FLOWSPEC_TYPE_DEST		1
#define FLOWSPEC_TYPE_SOURCE		2
#define FLOWSPEC_TYPE_PROTO		3
#define FLOWSPEC_TYPE_PORT		4
#define FLOWSPEC_TYPE_DST_PORT		5
#define FLOWSPEC_TYPE_SRC_PORT		6
#define FLOWSPEC_TYPE_ICMP_TYPE		7
#define FLOWSPEC_TYPE_ICMP_CODE		8
#define FLOWSPEC_TYPE_TCP_FLAGS		9
#define FLOWSPEC_TYPE_PKT_LEN		10
#define FLOWSPEC_TYPE_DSCP		11
#define FLOWSPEC_TYPE_FRAG		12
#define FLOWSPEC_TYPE_FLOW		13
#define FLOWSPEC_TYPE_MAX		14

#define FLOWSPEC_TCP_FLAG_STRING	"FSRPAUEW"
#define FLOWSPEC_FRAG_STRING4		"DIFL"
#define FLOWSPEC_FRAG_STRING6		" IFL"

struct filter_prefix {
	struct bgpd_addr	addr;
	uint8_t			op;
	uint8_t			len;
	uint8_t			len_min;
	uint8_t			len_max;
};

struct filter_nexthop {
	struct bgpd_addr	addr;
	uint8_t			flags;
#define FILTER_NEXTHOP_ADDR	1
#define FILTER_NEXTHOP_NEIGHBOR	2
};

struct filter_match {
	struct filter_prefix		prefix;
	struct filter_nexthop		nexthop;
	struct filter_as		as;
	struct filter_aslen		aslen;
	struct community		community[MAX_COMM_MATCH];
	struct filter_prefixset		prefixset;
	struct filter_originset		originset;
	struct filter_vs		ovs;
	struct filter_vs		avs;
	int				maxcomm;
	int				maxextcomm;
	int				maxlargecomm;
};

struct filter_rule {
	TAILQ_ENTRY(filter_rule)	entry;
	char				rib[PEER_DESCR_LEN];
	struct filter_peers		peer;
	struct filter_match		match;
	struct filter_set_head		set;
#define RDE_FILTER_SKIP_PEERID		0
#define RDE_FILTER_SKIP_GROUPID		1
#define RDE_FILTER_SKIP_REMOTE_AS	2
#define RDE_FILTER_SKIP_COUNT		3
	struct filter_rule		*skip[RDE_FILTER_SKIP_COUNT];
	enum filter_actions		action;
	enum directions			dir;
	uint8_t				quick;
};

enum action_types {
	ACTION_SET_LOCALPREF,
	ACTION_SET_RELATIVE_LOCALPREF,
	ACTION_SET_MED,
	ACTION_SET_RELATIVE_MED,
	ACTION_SET_WEIGHT,
	ACTION_SET_RELATIVE_WEIGHT,
	ACTION_SET_PREPEND_SELF,
	ACTION_SET_PREPEND_PEER,
	ACTION_SET_AS_OVERRIDE,
	ACTION_SET_NEXTHOP,
	ACTION_SET_NEXTHOP_REF,
	ACTION_SET_NEXTHOP_REJECT,
	ACTION_SET_NEXTHOP_BLACKHOLE,
	ACTION_SET_NEXTHOP_NOMODIFY,
	ACTION_SET_NEXTHOP_SELF,
	ACTION_DEL_COMMUNITY,
	ACTION_SET_COMMUNITY,
	ACTION_PFTABLE,
	ACTION_PFTABLE_ID,
	ACTION_RTLABEL,
	ACTION_RTLABEL_ID,
	ACTION_SET_ORIGIN
};

struct nexthop;
struct filter_set {
	TAILQ_ENTRY(filter_set)		entry;
	union {
		uint8_t				 prepend;
		uint16_t			 id;
		uint32_t			 metric;
		int32_t				 relative;
		struct bgpd_addr		 nexthop;
		struct nexthop			*nh_ref;
		struct community		 community;
		char				 pftable[PFTABLE_LEN];
		char				 rtlabel[ROUTELABEL_LEN];
		uint8_t				 origin;
	}				action;
	enum action_types		type;
};

struct roa_set {
	uint32_t	as;	/* must be first */
	uint32_t	maxlen;	/* change type for better struct layout */
};

struct prefixset_item {
	struct filter_prefix		p;
	RB_ENTRY(prefixset_item)	entry;
};

struct prefixset {
	int				 sflags;
	char				 name[SET_NAME_LEN];
	struct prefixset_tree		 psitems;
	struct roa_tree			 roaitems;
	SIMPLEQ_ENTRY(prefixset)	 entry;
};

struct as_set {
	char				 name[SET_NAME_LEN];
	SIMPLEQ_ENTRY(as_set)		 entry;
	struct set_table		*set;
	monotime_t			 lastchange;
	int				 dirty;
};

struct aspa_set {
	time_t				 expires;
	uint32_t			 as;
	uint32_t			 num;
	uint32_t			 *tas;
	RB_ENTRY(aspa_set)		 entry;
};

struct aspa_prep {
	size_t				datasize;
	uint32_t			entries;
};

struct l3vpn {
	SIMPLEQ_ENTRY(l3vpn)		entry;
	char				descr[PEER_DESCR_LEN];
	char				ifmpe[IFNAMSIZ];
	struct filter_set_head		import;
	struct filter_set_head		export;
	struct network_head		net_l;
	uint64_t			rd;
	u_int				rtableid;
	u_int				label;
	int				flags;
};

struct rde_rib {
	SIMPLEQ_ENTRY(rde_rib)	entry;
	char			name[PEER_DESCR_LEN];
	u_int			rtableid;
	uint16_t		id;
	uint16_t		flags;
};
SIMPLEQ_HEAD(rib_names, rde_rib);
extern struct rib_names ribnames;

/* rde_rib flags */
#define F_RIB_LOCAL		0x0001
#define F_RIB_NOEVALUATE	0x0002
#define F_RIB_NOFIB		0x0004
#define F_RIB_NOFIBSYNC		0x0008

/* 4-byte magic AS number */
#define AS_TRANS	23456
/* AS_NONE for origin validation */
#define AS_NONE		0

struct rde_memstats {
	long long	path_cnt;
	long long	path_refs;
	long long	prefix_cnt;
	long long	rib_cnt;
	long long	pt_cnt[AID_MAX];
	long long	pt_size[AID_MAX];
	long long	nexthop_cnt;
	long long	aspath_cnt;
	long long	aspath_size;
	long long	comm_cnt;
	long long	comm_nmemb;
	long long	comm_size;
	long long	comm_refs;
	long long	attr_cnt;
	long long	attr_refs;
	long long	attr_data;
	long long	attr_dcnt;
	long long	aset_cnt;
	long long	aset_size;
	long long	aset_nmemb;
	long long	pset_cnt;
	long long	pset_size;
};

#define	MRT_FILE_LEN	512
#define	MRT2MC(x)	((struct mrt_config *)(x))

enum mrt_type {
	MRT_NONE,
	MRT_TABLE_DUMP,
	MRT_TABLE_DUMP_MP,
	MRT_TABLE_DUMP_V2,
	MRT_ALL_IN,
	MRT_ALL_OUT,
	MRT_UPDATE_IN,
	MRT_UPDATE_OUT
};

enum mrt_state {
	MRT_STATE_RUNNING,
	MRT_STATE_OPEN,
	MRT_STATE_REOPEN,
	MRT_STATE_REMOVE
};

struct mrt {
	char			rib[PEER_DESCR_LEN];
	LIST_ENTRY(mrt)		entry;
	struct msgbuf		*wbuf;
	uint32_t		peer_id;
	uint32_t		group_id;
	int			fd;
	enum mrt_type		type;
	enum mrt_state		state;
	uint16_t		seqnum;
};

struct mrt_config {
	struct mrt		conf;
	char			name[MRT_FILE_LEN];	/* base file name */
	char			file[MRT_FILE_LEN];	/* actual file name */
	time_t			ReopenTimer;
	int			ReopenTimerInterval;
};

/* prototypes */
/* bgpd.c */
struct pollfd;
void		 send_nexthop_update(struct kroute_nexthop *);
void		 send_imsg_session(int, pid_t, void *, uint16_t);
int		 send_network(int, struct network_config *,
		    struct filter_set_head *);
int		 bgpd_oknexthop(struct kroute_full *);
int		 bgpd_has_bgpnh(void);
void		 set_pollfd(struct pollfd *, struct imsgbuf *);
int		 handle_pollfd(struct pollfd *, struct imsgbuf *);

/* control.c */
int	control_imsg_relay(struct imsg *, struct peer *);

/* config.c */
struct bgpd_config	*new_config(void);
void		copy_config(struct bgpd_config *, struct bgpd_config *);
void		network_free(struct network *);
struct flowspec_config	*flowspec_alloc(uint8_t, int);
void		flowspec_free(struct flowspec_config *);
void		free_l3vpns(struct l3vpn_head *);
void		free_config(struct bgpd_config *);
void		free_prefixsets(struct prefixset_head *);
void		free_rde_prefixsets(struct rde_prefixset_head *);
void		free_prefixtree(struct prefixset_tree *);
void		free_roatree(struct roa_tree *);
void		free_aspa(struct aspa_set *);
void		free_aspatree(struct aspa_tree *);
void		free_rtrs(struct rtr_config_head *);
void		filterlist_free(struct filter_head *);
int		host(const char *, struct bgpd_addr *, uint8_t *);
uint32_t	get_bgpid(void);
void		expand_networks(struct bgpd_config *, struct network_head *);
RB_PROTOTYPE(prefixset_tree, prefixset_item, entry, prefixset_cmp);
RB_PROTOTYPE(roa_tree, roa, entry, roa_cmp);
RB_PROTOTYPE(aspa_tree, aspa_set, entry, aspa_cmp);
RB_PROTOTYPE(flowspec_tree, flowspec_config, entry, flowspec_config_cmp);

/* kroute.c */
int		 kr_init(int *, uint8_t);
int		 kr_default_prio(void);
int		 kr_check_prio(long long);
int		 ktable_update(u_int, char *, int);
void		 ktable_preload(void);
void		 ktable_postload(void);
int		 ktable_exists(u_int, u_int *);
int		 kr_change(u_int, struct kroute_full *);
int		 kr_delete(u_int, struct kroute_full *);
int		 kr_flush(u_int);
void		 kr_shutdown(void);
void		 kr_fib_couple(u_int);
void		 kr_fib_couple_all(void);
void		 kr_fib_decouple(u_int);
void		 kr_fib_decouple_all(void);
void		 kr_fib_prio_set(uint8_t);
int		 kr_dispatch_msg(void);
int		 kr_nexthop_add(uint32_t, struct bgpd_addr *);
void		 kr_nexthop_delete(uint32_t, struct bgpd_addr *);
void		 kr_show_route(struct imsg *);
void		 kr_ifinfo(char *);
void		 kr_net_reload(u_int, uint64_t, struct network_head *);
int		 kr_reload(void);
int		 get_mpe_config(const char *, u_int *, u_int *);
uint8_t		 mask2prefixlen(sa_family_t, struct sockaddr *);

/* log.c */
void		 log_peer_info(const struct peer_config *, const char *, ...)
			__attribute__((__format__ (printf, 2, 3)));
void		 log_peer_warn(const struct peer_config *, const char *, ...)
			__attribute__((__format__ (printf, 2, 3)));
void		 log_peer_warnx(const struct peer_config *, const char *, ...)
			__attribute__((__format__ (printf, 2, 3)));

/* mrt.c */
void		 mrt_write(struct mrt *);
void		 mrt_clean(struct mrt *);
void		 mrt_init(struct imsgbuf *, struct imsgbuf *);
time_t		 mrt_timeout(struct mrt_head *);
void		 mrt_reconfigure(struct mrt_head *);
void		 mrt_handler(struct mrt_head *);
struct mrt	*mrt_get(struct mrt_head *, struct mrt *);
void		 mrt_mergeconfig(struct mrt_head *, struct mrt_head *);

/* name2id.c */
uint16_t	 rtlabel_name2id(const char *);
const char	*rtlabel_id2name(uint16_t);
void		 rtlabel_unref(uint16_t);
uint16_t	 rtlabel_ref(uint16_t);
uint16_t	 pftable_name2id(const char *);
const char	*pftable_id2name(uint16_t);
void		 pftable_unref(uint16_t);
uint16_t	 pftable_ref(uint16_t);

/* parse.y */
int			cmdline_symset(char *);
struct prefixset	*find_prefixset(char *, struct prefixset_head *);
struct bgpd_config	*parse_config(const char *, struct peer_head *,
			    struct rtr_config_head *);

/* pftable.c */
int	pftable_exists(const char *);
int	pftable_add(const char *);
int	pftable_clear_all(void);
int	pftable_addr_add(struct pftable_msg *);
int	pftable_addr_remove(struct pftable_msg *);
int	pftable_commit(void);

/* rde_filter.c */
void	filterset_free(struct filter_set_head *);
int	filterset_cmp(struct filter_set *, struct filter_set *);
void	filterset_move(struct filter_set_head *, struct filter_set_head *);
void	filterset_copy(struct filter_set_head *, struct filter_set_head *);
const char	*filterset_name(enum action_types);

/* rde_sets.c */
struct as_set	*as_sets_lookup(struct as_set_head *, const char *);
struct as_set	*as_sets_new(struct as_set_head *, const char *, size_t,
		    size_t);
void		 as_sets_free(struct as_set_head *);
void		 as_sets_mark_dirty(struct as_set_head *, struct as_set_head *);
int		 as_set_match(const struct as_set *, uint32_t);

struct set_table	*set_new(size_t, size_t);
void			 set_free(struct set_table *);
int			 set_add(struct set_table *, void *, size_t);
void			*set_get(struct set_table *, size_t *);
void			 set_prep(struct set_table *);
void			*set_match(const struct set_table *, uint32_t);
int			 set_equal(const struct set_table *,
			    const struct set_table *);
size_t			 set_nmemb(const struct set_table *);

/* rde_trie.c */
int	trie_add(struct trie_head *, struct bgpd_addr *, uint8_t, uint8_t,
	    uint8_t);
int	trie_roa_add(struct trie_head *, struct roa *);
void	trie_free(struct trie_head *);
int	trie_match(struct trie_head *, struct bgpd_addr *, uint8_t, int);
int	trie_roa_check(struct trie_head *, struct bgpd_addr *, uint8_t,
	    uint32_t);
void	trie_dump(struct trie_head *);
int	trie_equal(struct trie_head *, struct trie_head *);

/* util.c */
const char	*log_addr(const struct bgpd_addr *);
const char	*log_evpnaddr(const struct bgpd_addr *, struct sockaddr *,
		    socklen_t);
const char	*log_in6addr(const struct in6_addr *);
const char	*log_sockaddr(struct sockaddr *, socklen_t);
const char	*log_as(uint32_t);
const char	*log_rd(uint64_t);
const char	*log_ext_subtype(int, uint8_t);
const char	*log_reason(const char *);
const char	*log_aspath_error(int);
const char	*log_roa(struct roa *);
const char	*log_aspa(struct aspa_set *);
const char	*log_rtr_error(enum rtr_error);
const char	*log_policy(enum role);
const char	*log_capability(uint8_t);
int		 aspath_asprint(char **, struct ibuf *);
uint32_t	 aspath_extract(const void *, int);
int		 aspath_verify(struct ibuf *, int, int);
#define		 AS_ERR_LEN	-1
#define		 AS_ERR_TYPE	-2
#define		 AS_ERR_BAD	-3
#define		 AS_ERR_SOFT	-4
struct ibuf	*aspath_inflate(struct ibuf *);
int		 extract_prefix(const u_char *, int, void *, uint8_t, uint8_t);
int		 nlri_get_prefix(struct ibuf *, struct bgpd_addr *, uint8_t *);
int		 nlri_get_prefix6(struct ibuf *, struct bgpd_addr *, uint8_t *);
int		 nlri_get_vpn4(struct ibuf *, struct bgpd_addr *, uint8_t *,
		    int);
int		 nlri_get_vpn6(struct ibuf *, struct bgpd_addr *, uint8_t *,
		    int);
int		 nlri_get_evpn(struct ibuf *, struct bgpd_addr *, uint8_t *);
int		 prefix_compare(const struct bgpd_addr *,
		    const struct bgpd_addr *, int);
void		 inet4applymask(struct in_addr *, const struct in_addr *, int);
void		 inet6applymask(struct in6_addr *, const struct in6_addr *,
		    int);
void		 applymask(struct bgpd_addr *, const struct bgpd_addr *, int);
const char	*aid2str(uint8_t);
int		 aid2afi(uint8_t, uint16_t *, uint8_t *);
int		 afi2aid(uint16_t, uint8_t, uint8_t *);
sa_family_t	 aid2af(uint8_t);
int		 af2aid(sa_family_t, uint8_t, uint8_t *);
struct sockaddr	*addr2sa(const struct bgpd_addr *, uint16_t, socklen_t *);
void		 sa2addr(struct sockaddr *, struct bgpd_addr *, uint16_t *);
const char	*get_baudrate(unsigned long long, char *);

/* flowspec.c */
int	flowspec_valid(const uint8_t *, int, int);
int	flowspec_cmp(const uint8_t *, int, const uint8_t *, int, int);
int	flowspec_get_component(const uint8_t *, int, int, int,
	    const uint8_t **, int *);
int	flowspec_get_addr(const uint8_t *, int, int, int, struct bgpd_addr *,
	    uint8_t *, uint8_t *);
const char	*flowspec_fmt_label(int);
const char	*flowspec_fmt_num_op(const uint8_t *, int, int *);
const char	*flowspec_fmt_bin_op(const uint8_t *, int, int *, const char *);

static const char * const log_procnames[] = {
	"parent",
	"SE",
	"RDE",
	"RTR"
};

/* logmsg.c and needed by bgpctl */
static const char * const statenames[] = {
	"None",
	"Idle",
	"Connect",
	"Active",
	"OpenSent",
	"OpenConfirm",
	"Established"
};

static const char * const msgtypenames[] = {
	"NONE",
	"OPEN",
	"UPDATE",
	"NOTIFICATION",
	"KEEPALIVE",
	"RREFRESH"
};

static const char * const eventnames[] = {
	"None",
	"Start",
	"Stop",
	"Connection opened",
	"Connection closed",
	"Connection open failed",
	"Fatal error",
	"ConnectRetryTimer expired",
	"HoldTimer expired",
	"KeepaliveTimer expired",
	"SendHoldTimer expired",
	"OPEN message received",
	"KEEPALIVE message received",
	"UPDATE message received",
	"NOTIFICATION received",
	"graceful NOTIFICATION received",
};

static const char * const errnames[] = {
	"none",
	"Header error",
	"error in OPEN message",
	"error in UPDATE message",
	"HoldTimer expired",
	"Finite State Machine error",
	"Cease",
	"error in ROUTE-REFRESH message"
};

static const char * const suberr_header_names[] = {
	"none",
	"synchronization error",
	"wrong length",
	"unknown message type"
};

static const char * const suberr_open_names[] = {
	"none",
	"version mismatch",
	"AS unacceptable",
	"BGPID invalid",
	"optional parameter error",
	"authentication error",
	"unacceptable holdtime",
	"unsupported capability",
	NULL,
	NULL,
	NULL,
	"role mismatch",
};

static const char * const suberr_fsm_names[] = {
	"unspecified error",
	"received unexpected message in OpenSent",
	"received unexpected message in OpenConfirm",
	"received unexpected message in Established"
};

static const char * const suberr_update_names[] = {
	"none",
	"attribute list error",
	"unknown well-known attribute",
	"well-known attribute missing",
	"attribute flags error",
	"attribute length wrong",
	"origin unacceptable",
	"loop detected",
	"nexthop unacceptable",
	"optional attribute error",
	"network unacceptable",
	"AS-Path unacceptable"
};

static const char * const suberr_cease_names[] = {
	"none",
	"received max-prefix exceeded",
	"administratively down",
	"peer unconfigured",
	"administrative reset",
	"connection rejected",
	"other config change",
	"collision",
	"resource exhaustion",
	"hard reset",
	"sent max-prefix exceeded"
};

static const char * const suberr_rrefresh_names[] = {
	"none",
	"invalid message length"
};

static const char * const ctl_res_strerror[] = {
	"no error",
	"no such neighbor",
	"permission denied",
	"neighbor does not have this capability",
	"config file has errors, reload failed",
	"previous reload still running",
	"out of memory",
	"not a cloned peer",
	"peer still active, down peer first",
	"no such RIB",
	"operation not supported",
};

static const char * const timernames[] = {
	"None",
	"ConnectRetryTimer",
	"KeepaliveTimer",
	"HoldTimer",
	"SendHoldTimer",
	"IdleHoldTimer",
	"IdleHoldResetTimer",
	"CarpUndemoteTimer",
	"RestartTimer",
	"SessionDownTimer",
	"RTR RefreshTimer",
	"RTR RetryTimer",
	"RTR ExpireTimer",
	"RTR ActiveTimer",
	""
};

#endif /* __BGPD_H__ */
