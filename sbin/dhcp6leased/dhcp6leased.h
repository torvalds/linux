/*	$OpenBSD: dhcp6leased.h,v 1.10 2025/09/18 11:49:23 florian Exp $	*/

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

#ifndef nitems
#define	nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define	_PATH_LOCKFILE		"/dev/dhcp6leased.lock"
#define	_PATH_CONF_FILE		"/etc/dhcp6leased.conf"
#define	_PATH_CTRL_SOCKET	"/dev/dhcp6leased.sock"
#define	DHCP6LEASED_USER	"_dhcp6leased"
#define	DHCP6LEASED_RTA_LABEL	"dhcp6leased"
#define	CLIENT_PORT		546
#define	SERVER_PORT		547
#define	_PATH_LEASE		"/var/db/dhcp6leased/"
#define	_PATH_UUID		_PATH_LEASE"uuid"
#define	UUID_SIZE		16
#define UUID_STR_SIZE		sizeof("XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX\n")
#define	DUID_UUID_TYPE		4
#define	XID_SIZE		3
#define	SERVERID_SIZE		130 /* 2 octet type, max 128 octets data */
#define	MAX_IA			32
#define	LEASE_SIZE		4096
#define	LEASE_IA_PD_PREFIX	"ia_pd "
/* MAXDNAME from arpa/namesr.h */
#define	DHCP6LEASED_MAX_DNSSL	1025
#define	MAX_RDNS_COUNT		8 /* max nameserver in a RTM_PROPOSAL */

/* A 1500 bytes packet can hold less than 300 classless static routes */
#define	MAX_DHCP_ROUTES		256

#define	OPENBSD_ENTERPRISENO	30155

/* DHCP message types. */
#define	DHCPSOLICIT		1
#define	DHCPADVERTISE		2
#define	DHCPREQUEST		3
#define	DHCPCONFIRM		4
#define	DHCPRENEW		5
#define	DHCPREBIND		6
#define	DHCPREPLY		7
#define	DHCPRELEASE		8
#define	DHCPDECLINE		9
#define	DHCPRECONFIGURE		10
#define	DHCPINFORMATIONREQUEST	11
#define	DHCPRELAYFORW		12
#define	DHCPRELAYREPL		13

/* DHCP options */
#define	DHO_CLIENTID		1
#define	DHO_SERVERID		2
#define	DHO_ORO			6
#define	DHO_ELAPSED_TIME	8
#define	DHO_STATUS_CODE		13
#define	DHO_RAPID_COMMIT	14
#define	DHO_VENDOR_CLASS	16
#define	DHO_IA_PD		25
#define	DHO_IA_PREFIX		26
#define	DHO_SOL_MAX_RT		82
#define	DHO_INF_MAX_RT		83

/* Status Code Option status codes */
#define	DHCP_STATUS_SUCCESS		0
#define	DHCP_STATUS_UNSPECFAIL		1
#define	DHCP_STATUS_NOADDRSAVAIL	2
#define	DHCP_STATUS_NOBINDING		3
#define	DHCP_STATUS_NOTONLINK		4
#define	DHCP_STATUS_USEMULTICAST	5
#define	DHCP_STATUS_NOPREFIXAVAIL	6

/* Ignore parts of DHCP lease */
#define	IGN_ROUTES	1
#define	IGN_DNS		2

#define	MAX_SERVERS	16	/* max servers that can be ignored per if */

#define	IMSG_DATA_SIZE(imsg)	((imsg).hdr.len - IMSG_HEADER_SIZE)
#define	DHCP_SNAME_LEN		64
#define	DHCP_FILE_LEN		128

struct dhcp_hdr {
	uint8_t		msg_type;	/* Message opcode/type */
	uint8_t		xid[XID_SIZE];	/* Transaction ID */
} __packed;

struct dhcp_option_hdr {
	uint16_t	code;
	uint16_t	len;
} __packed;

struct dhcp_duid {
	uint16_t	type;
	uint8_t		uuid[UUID_SIZE];
} __packed;

struct dhcp_iapd {
	uint32_t	iaid;
	uint32_t	t1;
	uint32_t	t2;
} __packed;

struct dhcp_vendor_class {
	uint32_t	enterprise_number;
	uint16_t	vendor_class_len;
} __packed;

struct dhcp_iaprefix {
	uint32_t	pltime;
	uint32_t	vltime;
	uint8_t		prefix_len;
	struct in6_addr	prefix;
} __packed;

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
	IMSG_CTL_LOG_VERBOSE,
	IMSG_CTL_SHOW_INTERFACE_INFO,
	IMSG_CTL_SEND_REQUEST,
	IMSG_CTL_RELOAD,
	IMSG_CTL_END,
	IMSG_RECONF_CONF,
	IMSG_RECONF_IFACE,
	IMSG_RECONF_IFACE_IA,
	IMSG_RECONF_IFACE_PD,
	IMSG_RECONF_IFACE_IA_END,
	IMSG_RECONF_IFACE_END,
	IMSG_RECONF_END,
	IMSG_SEND_SOLICIT,
	IMSG_SEND_REQUEST,
	IMSG_SEND_RENEW,
	IMSG_SEND_REBIND,
	IMSG_SOCKET_IPC,
	IMSG_OPEN_UDPSOCK,
	IMSG_UDPSOCK,
	IMSG_ROUTESOCK,
	IMSG_UUID,
	IMSG_CONTROLFD,
	IMSG_STARTUP,
	IMSG_UPDATE_IF,
	IMSG_REMOVE_IF,
	IMSG_DHCP,
	IMSG_CONFIGURE_ADDRESS,
	IMSG_DECONFIGURE_ADDRESS,
	IMSG_CONFIGURE_REJECT_ROUTE,
	IMSG_DECONFIGURE_REJECT_ROUTE,
	IMSG_REQUEST_REBOOT,
	IMSG_WRITE_LEASE,
};

struct prefix {
	struct in6_addr	 prefix;
	int		 prefix_len;
	uint32_t	 vltime;
	uint32_t	 pltime;
};

struct ctl_engine_info {
	uint32_t		if_index;
	int			running;
	int			link_state;
	char			state[sizeof("IF_INIT_REBOOT")];
	struct timespec		request_time;
	uint32_t		lease_time;
	uint32_t		t1;
	uint32_t		t2;
	struct prefix		pds[MAX_IA];
};

struct iface_pd_conf {
	SIMPLEQ_ENTRY(iface_pd_conf)	 entry;
	char				 name[IF_NAMESIZE];
	struct in6_addr			 prefix_mask;
	int				 prefix_len;
};

struct iface_ia_conf {
	SIMPLEQ_ENTRY(iface_ia_conf)			 entry;
	SIMPLEQ_HEAD(iface_pd_conf_head, iface_pd_conf)	 iface_pd_list;
	int						 id;
	int						 prefix_len;
};

struct iface_conf {
	SIMPLEQ_ENTRY(iface_conf)		 entry;
	SIMPLEQ_HEAD(iface_ia_conf_head,
	    iface_ia_conf)			 iface_ia_list;
	uint32_t				 ia_count;
	char					 name[IF_NAMESIZE];
};

struct dhcp6leased_conf {
	SIMPLEQ_HEAD(iface_conf_head, iface_conf)	iface_list;
	int						rapid_commit;
};

struct imsg_ifinfo {
	uint32_t		if_index;
	int			rdomain;
	int			running;
	int			link_state;
	struct prefix		pds[MAX_IA];
};

struct imsg_dhcp {
	uint32_t		if_index;
	ssize_t			len;
	uint8_t			packet[1500];
};

struct imsg_req_dhcp {
	uint32_t		 if_index;
	int			 elapsed_time;
	uint8_t			 xid[XID_SIZE];
	int			 serverid_len;
	uint8_t			 serverid[SERVERID_SIZE];
	struct prefix		 pds[MAX_IA];
};

struct imsg_lease_info {
	uint32_t		 if_index;
	struct prefix		 pds[MAX_IA];
};

/* dhcp6leased.c */
void			 imsg_event_add(struct imsgev *);
int			 imsg_compose_event(struct imsgev *, uint16_t, uint32_t,
			     pid_t, int, void *, uint16_t);
void			 config_clear(struct dhcp6leased_conf *);
struct dhcp6leased_conf	*config_new_empty(void);
void			 merge_config(struct dhcp6leased_conf *, struct
			     dhcp6leased_conf *);
const char		*sin6_to_str(struct sockaddr_in6 *);

/* engine.c */
const char		*dhcp_message_type2str(int);

/* frontend.c */
struct iface_conf	*find_iface_conf(struct iface_conf_head *, char *);
int			*changed_ifaces(struct dhcp6leased_conf *, struct
			     dhcp6leased_conf *);
/* printconf.c */
void	print_config(struct dhcp6leased_conf *, int);

/* parse.y */
struct file {
	TAILQ_ENTRY(file)	 entry;
	FILE			*stream;
	char			*name;
	size_t			 ungetpos;
	size_t			 ungetsize;
	u_char			*ungetbuf;
	int			 eof_reached;
	int			 lineno;
	int			 errors;
};

struct dhcp6leased_conf	*parse_config(const char *);
struct file		*pushfile(const char *, int);
int			 popfile(void);
int			 kw_cmp(const void *, const void *);
int			 lgetc(int);
void			 lungetc(int);
int			 findeol(void);

/* parse_lease.y */
void	parse_lease(const char*, struct imsg_ifinfo *);
