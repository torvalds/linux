/*	$OpenBSD: dhcpleased.h,v 1.18 2025/09/18 11:37:01 florian Exp $	*/

/*
 * Copyright (c) 2017, 2021 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
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

#define	_PATH_LOCKFILE		"/dev/dhcpleased.lock"
#define	_PATH_CONF_FILE		"/etc/dhcpleased.conf"
#define	_PATH_DHCPLEASED_SOCKET	"/dev/dhcpleased.sock"
#define	DHCPLEASED_USER		"_dhcp"
#define	DHCPLEASED_RTA_LABEL	"dhcpleased"
#define	SERVER_PORT		67
#define	CLIENT_PORT		68
#define	_PATH_LEASE		"/var/db/dhcpleased/"
#define	LEASE_VERSION		"version: 2"
#define	LEASE_IP_PREFIX		"ip: "
#define	LEASE_NEXTSERVER_PREFIX	"next-server: "
#define	LEASE_BOOTFILE_PREFIX	"filename: "
#define	LEASE_HOSTNAME_PREFIX	"host-name: "
#define	LEASE_DOMAIN_PREFIX	"domain-name: "
#define	LEASE_SIZE		4096
/* MAXDNAME from arpa/namesr.h */
#define	DHCPLEASED_MAX_DNSSL	1025
#define	MAX_RDNS_COUNT		8 /* max nameserver in a RTM_PROPOSAL */

/* A 1500 bytes packet can hold less than 300 classless static routes */
#define	MAX_DHCP_ROUTES		256

#define	DHCP_COOKIE		{99, 130, 83, 99}

/* Possible values for hardware type (htype) field. */
#define	HTYPE_NONE		0
#define	HTYPE_ETHER		1
#define	HTYPE_IPSEC_TUNNEL	31

/* DHCP op code */
#define	DHCP_BOOTREQUEST		1
#define	DHCP_BOOTREPLY			2

/* DHCP Option codes: */
#define	DHO_PAD				0
#define	DHO_SUBNET_MASK			1
#define	DHO_TIME_OFFSET			2
#define	DHO_ROUTERS			3
#define	DHO_TIME_SERVERS		4
#define	DHO_NAME_SERVERS		5
#define	DHO_DOMAIN_NAME_SERVERS		6
#define	DHO_LOG_SERVERS			7
#define	DHO_COOKIE_SERVERS		8
#define	DHO_LPR_SERVERS			9
#define	DHO_IMPRESS_SERVERS		10
#define	DHO_RESOURCE_LOCATION_SERVERS	11
#define	DHO_HOST_NAME			12
#define	DHO_BOOT_SIZE			13
#define	DHO_MERIT_DUMP			14
#define	DHO_DOMAIN_NAME			15
#define	DHO_SWAP_SERVER			16
#define	DHO_ROOT_PATH			17
#define	DHO_EXTENSIONS_PATH		18
#define	DHO_IP_FORWARDING		19
#define	DHO_NON_LOCAL_SOURCE_ROUTING	20
#define	DHO_POLICY_FILTER		21
#define	DHO_MAX_DGRAM_REASSEMBLY	22
#define	DHO_DEFAULT_IP_TTL		23
#define	DHO_PATH_MTU_AGING_TIMEOUT	24
#define	DHO_PATH_MTU_PLATEAU_TABLE	25
#define	DHO_INTERFACE_MTU		26
#define	DHO_ALL_SUBNETS_LOCAL		27
#define	DHO_BROADCAST_ADDRESS		28
#define	DHO_PERFORM_MASK_DISCOVERY	29
#define	DHO_MASK_SUPPLIER		30
#define	DHO_ROUTER_DISCOVERY		31
#define	DHO_ROUTER_SOLICITATION_ADDRESS	32
#define	DHO_STATIC_ROUTES		33
#define	DHO_TRAILER_ENCAPSULATION	34
#define	DHO_ARP_CACHE_TIMEOUT		35
#define	DHO_IEEE802_3_ENCAPSULATION	36
#define	DHO_DEFAULT_TCP_TTL		37
#define	DHO_TCP_KEEPALIVE_INTERVAL	38
#define	DHO_TCP_KEEPALIVE_GARBAGE	39
#define	DHO_NIS_DOMAIN			40
#define	DHO_NIS_SERVERS			41
#define	DHO_NTP_SERVERS			42
#define	DHO_VENDOR_ENCAPSULATED_OPTIONS	43
#define	DHO_NETBIOS_NAME_SERVERS	44
#define	DHO_NETBIOS_DD_SERVER		45
#define	DHO_NETBIOS_NODE_TYPE		46
#define	DHO_NETBIOS_SCOPE		47
#define	DHO_FONT_SERVERS		48
#define	DHO_X_DISPLAY_MANAGER		49
#define	DHO_DHCP_REQUESTED_ADDRESS	50
#define	DHO_DHCP_LEASE_TIME		51
#define	DHO_DHCP_OPTION_OVERLOAD	52
#define	DHO_DHCP_MESSAGE_TYPE		53
#define	DHO_DHCP_SERVER_IDENTIFIER	54
#define	DHO_DHCP_PARAMETER_REQUEST_LIST	55
#define	DHO_DHCP_MESSAGE		56
#define	DHO_DHCP_MAX_MESSAGE_SIZE	57
#define	DHO_DHCP_RENEWAL_TIME		58
#define	DHO_DHCP_REBINDING_TIME		59
#define	DHO_DHCP_CLASS_IDENTIFIER	60
#define	DHO_DHCP_CLIENT_IDENTIFIER	61
#define	DHO_NISPLUS_DOMAIN		64
#define	DHO_NISPLUS_SERVERS		65
#define	DHO_TFTP_SERVER			66
#define	DHO_BOOTFILE_NAME		67
#define	DHO_MOBILE_IP_HOME_AGENT	68
#define	DHO_SMTP_SERVER			69
#define	DHO_POP_SERVER			70
#define	DHO_NNTP_SERVER			71
#define	DHO_WWW_SERVER			72
#define	DHO_FINGER_SERVER		73
#define	DHO_IRC_SERVER			74
#define	DHO_STREETTALK_SERVER		75
#define	DHO_STREETTALK_DIRECTORY_ASSISTANCE_SERVER	76
#define	DHO_DHCP_USER_CLASS_ID		77
#define	DHO_RELAY_AGENT_INFORMATION	82
#define	DHO_NDS_SERVERS			85
#define	DHO_NDS_TREE_NAME		86
#define	DHO_NDS_CONTEXT			87
#define	DHO_IPV6_ONLY_PREFERRED		108
#define	DHO_DOMAIN_SEARCH		119
#define	DHO_CLASSLESS_STATIC_ROUTES	121
#define	DHO_TFTP_CONFIG_FILE		144
#define	DHO_VOIP_CONFIGURATION_SERVER	150
#define	DHO_CLASSLESS_MS_STATIC_ROUTES	249
#define	DHO_AUTOPROXY_SCRIPT		252
#define	DHO_END				255
#define	DHO_COUNT			256	/* # of DHCP options */

/* DHCP message types. */
#define	DHCPDISCOVER	1
#define	DHCPOFFER	2
#define	DHCPREQUEST	3
#define	DHCPDECLINE	4
#define	DHCPACK		5
#define	DHCPNAK		6
#define	DHCPRELEASE	7
#define	DHCPINFORM	8

/* Ignore parts of DHCP lease */
#define	IGN_ROUTES	1
#define	IGN_DNS		2

#define	MAX_SERVERS	16	/* max servers that can be ignored per if */

#define	DHCP_SNAME_LEN		64
#define	DHCP_FILE_LEN		128

struct dhcp_hdr {
	uint8_t		op;	/* Message opcode/type */
	uint8_t		htype;	/* Hardware addr type (see net/if_types.h) */
	uint8_t		hlen;	/* Hardware addr length */
	uint8_t		hops;	/* Number of relay agent hops from client */
	uint32_t	xid;	/* Transaction ID */
	uint16_t	secs;	/* Seconds since client started looking */
	uint16_t	flags;	/* Flag bits */
	struct in_addr	ciaddr;	/* Client IP address (if already in use) */
	struct in_addr	yiaddr;	/* Client IP address */
	struct in_addr	siaddr;	/* IP address of next server to talk to */
	struct in_addr	giaddr;	/* DHCP relay agent IP address */
	uint8_t		chaddr[16];		/* Client hardware address */
	char		sname[DHCP_SNAME_LEN];	/* Server name */
	char		file[DHCP_FILE_LEN];	/* Boot filename */
};

struct imsgev {
	struct imsgbuf	 ibuf;
	void		(*handler)(int, short, void *);
	struct event	 ev;
	short		 events;
};

struct dhcp_route {
	struct in_addr		 dst;
	struct in_addr		 mask;
	struct in_addr		 gw;
};

enum imsg_type {
	IMSG_NONE,
#ifndef	SMALL
	IMSG_CTL_LOG_VERBOSE,
	IMSG_CTL_SHOW_INTERFACE_INFO,
	IMSG_CTL_SEND_REQUEST,
	IMSG_CTL_RELOAD,
	IMSG_CTL_END,
	IMSG_RECONF_CONF,
	IMSG_RECONF_IFACE,
	IMSG_RECONF_VC_ID,
	IMSG_RECONF_C_ID,
	IMSG_RECONF_H_NAME,
	IMSG_RECONF_END,
#endif	/* SMALL */
	IMSG_SEND_DISCOVER,
	IMSG_SEND_REQUEST,
	IMSG_SOCKET_IPC,
	IMSG_OPEN_BPFSOCK,
	IMSG_BPFSOCK,
	IMSG_UDPSOCK,
	IMSG_CLOSE_UDPSOCK,
	IMSG_ROUTESOCK,
	IMSG_CONTROLFD,
	IMSG_STARTUP,
	IMSG_UPDATE_IF,
	IMSG_REMOVE_IF,
	IMSG_DHCP,
	IMSG_CONFIGURE_INTERFACE,
	IMSG_DECONFIGURE_INTERFACE,
	IMSG_PROPOSE_RDNS,
	IMSG_WITHDRAW_RDNS,
	IMSG_WITHDRAW_ROUTES,
	IMSG_REPROPOSE_RDNS,
	IMSG_REQUEST_REBOOT,
};

#ifndef	SMALL
struct ctl_engine_info {
	uint32_t		if_index;
	int			running;
	int			link_state;
	char			state[sizeof("IF_INIT_REBOOT")];
	struct timespec		request_time;
	struct in_addr		server_identifier;
	struct in_addr		dhcp_server; /* for unicast */
	struct in_addr		requested_ip;
	struct in_addr		mask;
	struct dhcp_route	routes[MAX_DHCP_ROUTES];
	int			routes_len;
	struct in_addr		nameservers[MAX_RDNS_COUNT];
	uint32_t		lease_time;
	uint32_t		renewal_time;
	uint32_t		rebinding_time;
};

struct iface_conf {
	SIMPLEQ_ENTRY(iface_conf)	 entry;
	char				 name[IF_NAMESIZE];
	uint8_t				*vc_id;
	size_t				 vc_id_len;
	uint8_t				*c_id;
	size_t				 c_id_len;
	char				*h_name;
	int				 ignore;
	struct in_addr			 ignore_servers[MAX_SERVERS];
	int				 ignore_servers_len;
	int				 prefer_ipv6;
};

struct dhcpleased_conf {
	SIMPLEQ_HEAD(iface_conf_head, iface_conf)	iface_list;
};

#endif	/* SMALL */

struct imsg_ifinfo {
	uint32_t		if_index;
	char			if_name[IF_NAMESIZE];
	int			rdomain;
	int			running;
	int			link_state;
	struct ether_addr	hw_address;
	char			lease[LEASE_SIZE];
};

struct imsg_propose_rdns {
	uint32_t		if_index;
	int			rdomain;
	int			rdns_count;
	struct in_addr		rdns[MAX_RDNS_COUNT];
};

struct imsg_dhcp {
	uint32_t		if_index;
	ssize_t			len;
	uint16_t		csumflags;
	uint8_t			packet[1500];
};

struct imsg_req_dhcp {
	uint32_t		if_index;
	uint32_t		xid;
	struct in_addr		ciaddr;
	struct in_addr		requested_ip;
	struct in_addr		server_identifier;
	struct in_addr		dhcp_server;
};

/* dhcpleased.c */
void			 imsg_event_add(struct imsgev *);
int			 imsg_compose_event(struct imsgev *, uint16_t, uint32_t,
			     pid_t, int, void *, uint16_t);
int			 imsg_forward_event(struct imsgev *, struct imsg *);
#ifndef	SMALL
void			 config_clear(struct dhcpleased_conf *);
struct dhcpleased_conf	*config_new_empty(void);
void			 merge_config(struct dhcpleased_conf *, struct
			     dhcpleased_conf *);
const char	*sin_to_str(struct sockaddr_in *);
const char	*i2s(uint32_t);

/* frontend.c */
struct iface_conf	*find_iface_conf(struct iface_conf_head *, char *);
int			*changed_ifaces(struct dhcpleased_conf *, struct
			     dhcpleased_conf *);

/* printconf.c */
void	print_config(struct dhcpleased_conf *);

/* parse.y */
struct dhcpleased_conf	*parse_config(const char *);
int			 cmdline_symset(char *);
#else
#define	sin_to_str(x)	""
#define	i2s(x)		""
#endif	/* SMALL */

