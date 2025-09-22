/*	$OpenBSD: smtpd.h,v 1.689 2025/04/08 17:35:01 op Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <event.h>
#include <imsg.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>

#include "smtpd-defines.h"
#include "smtpd-api.h"
#include "ioev.h"

#define CHECK_IMSG_DATA_SIZE(imsg, expected_sz) do {			\
	if ((imsg)->hdr.len - IMSG_HEADER_SIZE != (expected_sz))	\
		fatalx("smtpd: imsg %d: data size expected %zd got %zd",\
	   	    (imsg)->hdr.type,					\
	   	    (expected_sz), (imsg)->hdr.len - IMSG_HEADER_SIZE);	\
} while (0)

#define CONF_FILE		 "/etc/mail/smtpd.conf"
#define MAILNAME_FILE		 "/etc/mail/mailname"

#define MAX_HOPS_COUNT		 100
#define	DEFAULT_MAX_BODY_SIZE	(35*1024*1024)

#define	EXPAND_BUFFER		 1024

#define SMTPD_QUEUE_EXPIRY	 (4 * 24 * 60 * 60)
#define SMTPD_SOCKET		 "/var/run/smtpd.sock"
#define	SMTPD_NAME		 "OpenSMTPD"
#define	SMTPD_VERSION		 "7.7.0"
#define SMTPD_SESSION_TIMEOUT	 300
#define SMTPD_BACKLOG		 5

#define	PATH_SMTPCTL		"/usr/sbin/smtpctl"

#define PATH_OFFLINE		"/offline"
#define PATH_PURGE		"/purge"
#define PATH_TEMPORARY		"/temporary"

#define	PATH_LIBEXEC		"/usr/local/libexec/smtpd"


/*
 * RFC 5322 defines these characters as valid, some of them are
 * potentially dangerous and need to be escaped.
 */
#define	MAILADDR_ALLOWED       	"!#$%&'*/?^`{|}~+-=_"
#define	MAILADDR_ESCAPE		"!#$%&'*?`{|}~"


#define F_STARTTLS		0x01
#define F_SMTPS			0x02
#define F_SSL		       (F_STARTTLS | F_SMTPS)
#define F_AUTH			0x08
#define	F_STARTTLS_REQUIRE	0x20
#define	F_AUTH_REQUIRE		0x40
#define	F_MASK_SOURCE		0x100
#define	F_TLS_VERIFY		0x200
#define	F_EXT_DSN		0x400
#define	F_RECEIVEDAUTH		0x800
#define	F_MASQUERADE		0x1000
#define	F_FILTERED		0x2000
#define	F_PROXY			0x4000

#define RELAY_TLS_OPPORTUNISTIC	0
#define RELAY_TLS_STARTTLS	1
#define RELAY_TLS_SMTPS		2
#define RELAY_TLS_NO		3

#define RELAY_AUTH		0x08
#define RELAY_LMTP		0x80

#define MTA_EXT_DSN		0x400


#define P_SENDMAIL	0
#define P_NEWALIASES	1
#define P_MAKEMAP	2

struct userinfo {
	char username[SMTPD_VUSERNAME_SIZE];
	char directory[PATH_MAX];
	uid_t uid;
	gid_t gid;
};

struct netaddr {
	struct sockaddr_storage ss;
	int bits;
};

struct relayhost {
	uint16_t flags;
	int tls;
	char hostname[HOST_NAME_MAX+1];
	uint16_t port;
	char authlabel[PATH_MAX];
};

struct credentials {
	char username[LINE_MAX];
	char password[LINE_MAX];
};

struct destination {
	char	name[HOST_NAME_MAX+1];
};

struct source {
	struct sockaddr_storage	addr;
};

struct addrname {
	struct sockaddr_storage	addr;
	char			name[HOST_NAME_MAX+1];
};

union lookup {
	struct expand		*expand;
	struct credentials	 creds;
	struct netaddr		 netaddr;
	struct source		 source;
	struct destination	 domain;
	struct userinfo		 userinfo;
	struct mailaddr		 mailaddr;
	struct addrname		 addrname;
	struct maddrmap		*maddrmap;
	char			 relayhost[LINE_MAX];
};

/*
 * Bump IMSG_VERSION whenever a change is made to enum imsg_type.
 * This will ensure that we can never use a wrong version of smtpctl with smtpd.
 */
#define	IMSG_VERSION		16

enum imsg_type {
	IMSG_NONE,

	IMSG_CTL_OK,
	IMSG_CTL_FAIL,

	IMSG_CTL_GET_DIGEST,
	IMSG_CTL_GET_STATS,
	IMSG_CTL_LIST_MESSAGES,
	IMSG_CTL_LIST_ENVELOPES,
	IMSG_CTL_MTA_SHOW_HOSTS,
	IMSG_CTL_MTA_SHOW_RELAYS,
	IMSG_CTL_MTA_SHOW_ROUTES,
	IMSG_CTL_MTA_SHOW_HOSTSTATS,
	IMSG_CTL_MTA_BLOCK,
	IMSG_CTL_MTA_UNBLOCK,
	IMSG_CTL_MTA_SHOW_BLOCK,
	IMSG_CTL_PAUSE_EVP,
	IMSG_CTL_PAUSE_MDA,
	IMSG_CTL_PAUSE_MTA,
	IMSG_CTL_PAUSE_SMTP,
	IMSG_CTL_PROFILE,
	IMSG_CTL_PROFILE_DISABLE,
	IMSG_CTL_PROFILE_ENABLE,
	IMSG_CTL_RESUME_EVP,
	IMSG_CTL_RESUME_MDA,
	IMSG_CTL_RESUME_MTA,
	IMSG_CTL_RESUME_SMTP,
	IMSG_CTL_RESUME_ROUTE,
	IMSG_CTL_REMOVE,
	IMSG_CTL_SCHEDULE,
	IMSG_CTL_SHOW_STATUS,
	IMSG_CTL_TRACE_DISABLE,
	IMSG_CTL_TRACE_ENABLE,
	IMSG_CTL_UPDATE_TABLE,
	IMSG_CTL_VERBOSE,
	IMSG_CTL_DISCOVER_EVPID,
	IMSG_CTL_DISCOVER_MSGID,

	IMSG_CTL_SMTP_SESSION,

	IMSG_GETADDRINFO,
	IMSG_GETADDRINFO_END,
	IMSG_GETNAMEINFO,
	IMSG_RES_QUERY,

	IMSG_SETUP_KEY,
	IMSG_SETUP_PEER,
	IMSG_SETUP_DONE,

	IMSG_CONF_START,
	IMSG_CONF_END,

	IMSG_STAT_INCREMENT,
	IMSG_STAT_DECREMENT,
	IMSG_STAT_SET,

	IMSG_LKA_AUTHENTICATE,
	IMSG_LKA_OPEN_FORWARD,
	IMSG_LKA_ENVELOPE_SUBMIT,
	IMSG_LKA_ENVELOPE_COMMIT,

	IMSG_QUEUE_DELIVER,
	IMSG_QUEUE_DELIVERY_OK,
	IMSG_QUEUE_DELIVERY_TEMPFAIL,
	IMSG_QUEUE_DELIVERY_PERMFAIL,
	IMSG_QUEUE_DELIVERY_LOOP,
	IMSG_QUEUE_DISCOVER_EVPID,
	IMSG_QUEUE_DISCOVER_MSGID,
	IMSG_QUEUE_ENVELOPE_ACK,
	IMSG_QUEUE_ENVELOPE_COMMIT,
	IMSG_QUEUE_ENVELOPE_REMOVE,
	IMSG_QUEUE_ENVELOPE_SCHEDULE,
	IMSG_QUEUE_ENVELOPE_SUBMIT,
	IMSG_QUEUE_HOLDQ_HOLD,
	IMSG_QUEUE_HOLDQ_RELEASE,
	IMSG_QUEUE_MESSAGE_COMMIT,
	IMSG_QUEUE_MESSAGE_ROLLBACK,
	IMSG_QUEUE_SMTP_SESSION,
	IMSG_QUEUE_TRANSFER,

	IMSG_MDA_DELIVERY_OK,
	IMSG_MDA_DELIVERY_TEMPFAIL,
	IMSG_MDA_DELIVERY_PERMFAIL,
	IMSG_MDA_DELIVERY_LOOP,
	IMSG_MDA_DELIVERY_HOLD,
	IMSG_MDA_DONE,
	IMSG_MDA_FORK,
	IMSG_MDA_HOLDQ_RELEASE,
	IMSG_MDA_LOOKUP_USERINFO,
	IMSG_MDA_KILL,
	IMSG_MDA_OPEN_MESSAGE,

	IMSG_MTA_DELIVERY_OK,
	IMSG_MTA_DELIVERY_TEMPFAIL,
	IMSG_MTA_DELIVERY_PERMFAIL,
	IMSG_MTA_DELIVERY_LOOP,
	IMSG_MTA_DELIVERY_HOLD,
	IMSG_MTA_DNS_HOST,
	IMSG_MTA_DNS_HOST_END,
	IMSG_MTA_DNS_MX,
	IMSG_MTA_DNS_MX_PREFERENCE,
	IMSG_MTA_HOLDQ_RELEASE,
	IMSG_MTA_LOOKUP_CREDENTIALS,
	IMSG_MTA_LOOKUP_SOURCE,
	IMSG_MTA_LOOKUP_HELO,
	IMSG_MTA_LOOKUP_SMARTHOST,
	IMSG_MTA_OPEN_MESSAGE,
	IMSG_MTA_SCHEDULE,

	IMSG_SCHED_ENVELOPE_BOUNCE,
	IMSG_SCHED_ENVELOPE_DELIVER,
	IMSG_SCHED_ENVELOPE_EXPIRE,
	IMSG_SCHED_ENVELOPE_INJECT,
	IMSG_SCHED_ENVELOPE_REMOVE,
	IMSG_SCHED_ENVELOPE_TRANSFER,

	IMSG_SMTP_AUTHENTICATE,
	IMSG_SMTP_MESSAGE_COMMIT,
	IMSG_SMTP_MESSAGE_CREATE,
	IMSG_SMTP_MESSAGE_ROLLBACK,
	IMSG_SMTP_MESSAGE_OPEN,
	IMSG_SMTP_CHECK_SENDER,
	IMSG_SMTP_EXPAND_RCPT,
	IMSG_SMTP_LOOKUP_HELO,

	IMSG_SMTP_REQ_CONNECT,
	IMSG_SMTP_REQ_HELO,
	IMSG_SMTP_REQ_MAIL,
	IMSG_SMTP_REQ_RCPT,
	IMSG_SMTP_REQ_DATA,
	IMSG_SMTP_REQ_EOM,
	IMSG_SMTP_EVENT_RSET,
	IMSG_SMTP_EVENT_COMMIT,
	IMSG_SMTP_EVENT_ROLLBACK,
	IMSG_SMTP_EVENT_DISCONNECT,

	IMSG_LKA_PROCESSOR_FORK,
	IMSG_LKA_PROCESSOR_ERRFD,

	IMSG_REPORT_SMTP_LINK_CONNECT,
	IMSG_REPORT_SMTP_LINK_DISCONNECT,
	IMSG_REPORT_SMTP_LINK_GREETING,
	IMSG_REPORT_SMTP_LINK_IDENTIFY,
	IMSG_REPORT_SMTP_LINK_TLS,
	IMSG_REPORT_SMTP_LINK_AUTH,
	IMSG_REPORT_SMTP_TX_RESET,
	IMSG_REPORT_SMTP_TX_BEGIN,
	IMSG_REPORT_SMTP_TX_MAIL,
	IMSG_REPORT_SMTP_TX_RCPT,
	IMSG_REPORT_SMTP_TX_ENVELOPE,
	IMSG_REPORT_SMTP_TX_DATA,
	IMSG_REPORT_SMTP_TX_COMMIT,
	IMSG_REPORT_SMTP_TX_ROLLBACK,
	IMSG_REPORT_SMTP_PROTOCOL_CLIENT,
	IMSG_REPORT_SMTP_PROTOCOL_SERVER,
	IMSG_REPORT_SMTP_FILTER_RESPONSE,
	IMSG_REPORT_SMTP_TIMEOUT,

	IMSG_FILTER_SMTP_BEGIN,
	IMSG_FILTER_SMTP_END,
	IMSG_FILTER_SMTP_PROTOCOL,
	IMSG_FILTER_SMTP_DATA_BEGIN,
	IMSG_FILTER_SMTP_DATA_END,

	IMSG_CA_RSA_PRIVENC,
	IMSG_CA_RSA_PRIVDEC,
	IMSG_CA_ECDSA_SIGN,
};

enum smtp_proc_type {
	PROC_PARENT = 0,
	PROC_LKA,
	PROC_QUEUE,
	PROC_CONTROL,
	PROC_SCHEDULER,
	PROC_DISPATCHER,
	PROC_CA,
	PROC_PROCESSOR,
	PROC_CLIENT,
};

enum table_type {
	T_NONE		= 0,
	T_DYNAMIC	= 0x01,	/* table with external source	*/
	T_LIST		= 0x02,	/* table holding a list		*/
	T_HASH		= 0x04,	/* table holding a hash table	*/
};

struct table {
	char				 t_name[LINE_MAX];
	enum table_type			 t_type;
	char				 t_config[PATH_MAX];

	unsigned int			 t_services;
	void				*t_handle;
	struct table_backend		*t_backend;
};

struct table_backend {
	const char *name;
	const unsigned int	services;
	int	(*config)(struct table *);
	int	(*add)(struct table *, const char *, const char *);
	void	(*dump)(struct table *);
	int	(*open)(struct table *);
	int	(*update)(struct table *);
	void	(*close)(struct table *);
	int	(*lookup)(struct table *, enum table_service, const char *, char **);
	int	(*fetch)(struct table *, enum table_service, char **);
};


enum bounce_type {
	B_FAILED,
	B_DELAYED,
	B_DELIVERED
};

enum dsn_ret {
	DSN_RETFULL = 1,
	DSN_RETHDRS
};

struct delivery_bounce {
	enum bounce_type	type;
	time_t			delay;
	time_t			ttl;
	enum dsn_ret		dsn_ret;
        int			mta_without_dsn;
};

enum expand_type {
	EXPAND_INVALID,
	EXPAND_USERNAME,
	EXPAND_FILENAME,
	EXPAND_FILTER,
	EXPAND_INCLUDE,
	EXPAND_ADDRESS,
	EXPAND_ERROR,
};

enum filter_phase {
	FILTER_CONNECT,
	FILTER_HELO,
	FILTER_EHLO,
	FILTER_STARTTLS,
	FILTER_AUTH,
	FILTER_MAIL_FROM,
	FILTER_RCPT_TO,
	FILTER_DATA,
	FILTER_DATA_LINE,
	FILTER_RSET,
	FILTER_QUIT,
	FILTER_NOOP,
	FILTER_HELP,
	FILTER_WIZ,
	FILTER_COMMIT,
	FILTER_PHASES_COUNT     /* must be last */
};

struct expandnode {
	RB_ENTRY(expandnode)	entry;
	TAILQ_ENTRY(expandnode)	tq_entry;
	enum expand_type	type;
	int			sameuser;
	int			realuser;
	uid_t			realuser_uid;
	int			forwarded;
	struct rule	       *rule;
	struct expandnode      *parent;
	unsigned int		depth;
	union {
		/*
		 * user field handles both expansion user and system user
		 * so we MUST make it large enough to fit a mailaddr user
		 */
		char		user[SMTPD_MAXLOCALPARTSIZE];
		char		buffer[EXPAND_BUFFER];
		struct mailaddr	mailaddr;
	}			u;
	char		subaddress[SMTPD_SUBADDRESS_SIZE];
};

struct expand {
	RB_HEAD(expandtree, expandnode)	 tree;
	TAILQ_HEAD(xnodes, expandnode)	*queue;
	size_t				 nb_nodes;
	struct rule			*rule;
	struct expandnode		*parent;
};

struct maddrnode {
	TAILQ_ENTRY(maddrnode)		entries;
	struct mailaddr			mailaddr;
};

struct maddrmap {
	TAILQ_HEAD(xmaddr, maddrnode)	queue;
};

#define DSN_SUCCESS 0x01
#define DSN_FAILURE 0x02
#define DSN_DELAY   0x04
#define DSN_NEVER   0x08

#define	DSN_ENVID_LEN	100
#define	DSN_ORCPT_LEN	500

#define	SMTPD_ENVELOPE_VERSION		3
struct envelope {
	TAILQ_ENTRY(envelope)		entry;

	char				dispatcher[HOST_NAME_MAX+1];

	char				tag[SMTPD_TAG_SIZE];

	uint32_t			version;
	uint64_t			id;
	enum envelope_flags		flags;

	char				smtpname[HOST_NAME_MAX+1];
	char				helo[HOST_NAME_MAX+1];
	char				hostname[HOST_NAME_MAX+1];
	char				username[SMTPD_MAXMAILADDRSIZE];
	char				errorline[LINE_MAX];
	struct sockaddr_storage		ss;

	struct mailaddr			sender;
	struct mailaddr			rcpt;
	struct mailaddr			dest;

	char				mda_user[SMTPD_VUSERNAME_SIZE];
	char				mda_subaddress[SMTPD_SUBADDRESS_SIZE];
	char				mda_exec[LINE_MAX];

	enum delivery_type		type;
	union {
		struct delivery_bounce	bounce;
	}				agent;

	uint16_t			retry;
	time_t				creation;
	time_t				ttl;
	time_t				lasttry;
	time_t				nexttry;
	time_t				lastbounce;

	char				dsn_orcpt[DSN_ORCPT_LEN+1];
	char				dsn_envid[DSN_ENVID_LEN+1];
	uint8_t				dsn_notify;
	enum dsn_ret			dsn_ret;

	uint8_t				esc_class;
	uint8_t				esc_code;
};

struct listener {
	uint16_t       		 flags;
	int			 fd;
	struct sockaddr_storage	 ss;
	in_port_t		 port;
	struct timeval		 timeout;
	struct event		 ev;
	char			 filter_name[PATH_MAX];
	char			 pki_name[PATH_MAX];
	char			 ca_name[PATH_MAX];
	char			 tag[SMTPD_TAG_SIZE];
	char			 authtable[LINE_MAX];
	char			 hostname[HOST_NAME_MAX+1];
	char			 hostnametable[PATH_MAX];
	char			 sendertable[PATH_MAX];

	TAILQ_ENTRY(listener)	 entry;

	int			 local;		/* there must be a better way */

	char			*tls_protocols;
	char			*tls_ciphers;
	struct tls		*tls;
	struct pki		**pki;
	int			 pkicount;
};

struct smtpd {
	char				sc_conffile[PATH_MAX];
	size_t				sc_maxsize;

#define SMTPD_OPT_VERBOSE		0x00000001
#define SMTPD_OPT_NOACTION		0x00000002
	uint32_t			sc_opts;

#define SMTPD_EXITING			0x00000001 /* unused */
#define SMTPD_MDA_PAUSED		0x00000002
#define SMTPD_MTA_PAUSED		0x00000004
#define SMTPD_SMTP_PAUSED		0x00000008
#define SMTPD_MDA_BUSY			0x00000010
#define SMTPD_MTA_BUSY			0x00000020
#define SMTPD_BOUNCE_BUSY		0x00000040
#define SMTPD_SMTP_DISABLED		0x00000080
	uint32_t			sc_flags;

#define QUEUE_COMPRESSION      		0x00000001
#define QUEUE_ENCRYPTION      		0x00000002
#define QUEUE_EVPCACHE			0x00000004
	uint32_t			sc_queue_flags;
	char			       *sc_queue_key;
	size_t				sc_queue_evpcache_size;

	size_t				sc_session_max_rcpt;
	size_t				sc_session_max_mails;

	struct dict		       *sc_mda_wrappers;
	size_t				sc_mda_max_session;
	size_t				sc_mda_max_user_session;
	size_t				sc_mda_task_hiwat;
	size_t				sc_mda_task_lowat;
	size_t				sc_mda_task_release;

	size_t				sc_mta_max_deferred;

	size_t				sc_scheduler_max_inflight;
	size_t				sc_scheduler_max_evp_batch_size;
	size_t				sc_scheduler_max_msg_batch_size;
	size_t				sc_scheduler_max_schedule;

	struct dict		       *sc_filter_processes_dict;

	int				sc_ttl;
#define MAX_BOUNCE_WARN			4
	time_t				sc_bounce_warn[MAX_BOUNCE_WARN];
	char				sc_hostname[HOST_NAME_MAX+1];
	struct stat_backend	       *sc_stat;
	struct compress_backend	       *sc_comp;

	time_t					 sc_uptime;

	/* This is a listener for a local socket used by smtp_enqueue(). */
	struct listener                         *sc_sock_listener;

	TAILQ_HEAD(listenerlist, listener)	*sc_listeners;

	TAILQ_HEAD(rulelist, rule)		*sc_rules;


	struct dict				*sc_filters_dict;
	struct dict				*sc_dispatchers;
	struct dispatcher			*sc_dispatcher_bounce;

	struct dict			       *sc_ca_dict;
	struct dict			       *sc_pki_dict;
	struct dict			       *sc_ssl_dict;

	struct dict			       *sc_tables_dict;		/* keyed lookup	*/

	struct dict			       *sc_limits_dict;

	char				       *sc_tls_ciphers;

	char				       *sc_subaddressing_delim;

	char				       *sc_srs_key;
	char				       *sc_srs_key_backup;
	int				        sc_srs_ttl;

	char				       *sc_admd;
};

#define	TRACE_DEBUG	0x0001
#define	TRACE_IMSG	0x0002
#define	TRACE_IO	0x0004
#define	TRACE_SMTP	0x0008
#define	TRACE_FILTERS	0x0010
#define	TRACE_MTA	0x0020
#define	TRACE_BOUNCE	0x0040
#define	TRACE_SCHEDULER	0x0080
#define	TRACE_LOOKUP	0x0100
#define	TRACE_STAT	0x0200
#define	TRACE_RULES	0x0400
#define	TRACE_MPROC	0x0800
#define	TRACE_EXPAND	0x1000
#define	TRACE_TABLES	0x2000
#define	TRACE_QUEUE	0x4000

#define PROFILE_TOSTAT	0x0001
#define PROFILE_IMSG	0x0002
#define PROFILE_QUEUE	0x0004

struct forward_req {
	uint64_t			id;
	uint8_t				status;

	char				user[SMTPD_VUSERNAME_SIZE];
	uid_t				uid;
	gid_t				gid;
	char				directory[PATH_MAX];
};

struct deliver {
	char			dispatcher[EXPAND_BUFFER];

	struct mailaddr		sender;
	struct mailaddr		rcpt;
	struct mailaddr		dest;

	char			mda_subaddress[SMTPD_SUBADDRESS_SIZE];
	char			mda_exec[LINE_MAX];

	struct userinfo		userinfo;
};

struct mta_host {
	SPLAY_ENTRY(mta_host)	 entry;
	struct sockaddr		*sa;
	char			*ptrname;
	int			 refcount;
	size_t			 nconn;
	time_t			 lastconn;
	time_t			 lastptrquery;

#define HOST_IGNORE	0x01
	int			 flags;
};

struct mta_mx {
	TAILQ_ENTRY(mta_mx)	 entry;
	struct mta_host		*host;
	char			*mxname;
	int			 preference;
};

struct mta_domain {
	SPLAY_ENTRY(mta_domain)	 entry;
	char			*name;
	int			 as_host;
	TAILQ_HEAD(, mta_mx)	 mxs;
	int			 mxstatus;
	int			 refcount;
	size_t			 nconn;
	time_t			 lastconn;
	time_t			 lastmxquery;
};

struct mta_source {
	SPLAY_ENTRY(mta_source)	 entry;
	struct sockaddr		*sa;
	int			 refcount;
	size_t			 nconn;
	time_t			 lastconn;
};

struct mta_connector {
	struct mta_source		*source;
	struct mta_relay		*relay;

#define CONNECTOR_ERROR_FAMILY		0x0001
#define CONNECTOR_ERROR_SOURCE		0x0002
#define CONNECTOR_ERROR_MX		0x0004
#define CONNECTOR_ERROR_ROUTE_NET	0x0008
#define CONNECTOR_ERROR_ROUTE_SMTP	0x0010
#define CONNECTOR_ERROR_ROUTE		0x0018
#define CONNECTOR_ERROR_BLOCKED		0x0020
#define CONNECTOR_ERROR			0x00ff

#define CONNECTOR_LIMIT_HOST		0x0100
#define CONNECTOR_LIMIT_ROUTE		0x0200
#define CONNECTOR_LIMIT_SOURCE		0x0400
#define CONNECTOR_LIMIT_RELAY		0x0800
#define CONNECTOR_LIMIT_CONN		0x1000
#define CONNECTOR_LIMIT_DOMAIN		0x2000
#define CONNECTOR_LIMIT			0xff00

#define CONNECTOR_NEW			0x10000
#define CONNECTOR_WAIT			0x20000
	int				 flags;

	int				 refcount;
	size_t				 nconn;
	time_t				 lastconn;
};

struct mta_route {
	SPLAY_ENTRY(mta_route)	 entry;
	uint64_t		 id;
	struct mta_source	*src;
	struct mta_host		*dst;
#define ROUTE_NEW		0x01
#define ROUTE_RUNQ		0x02
#define ROUTE_KEEPALIVE		0x04
#define ROUTE_DISABLED		0xf0
#define ROUTE_DISABLED_NET	0x10
#define ROUTE_DISABLED_SMTP	0x20
	int			 flags;
	int			 nerror;
	int			 penalty;
	int			 refcount;
	size_t			 nconn;
	time_t			 lastconn;
	time_t			 lastdisc;
	time_t			 lastpenalty;
};

struct mta_limits {
	size_t	maxconn_per_host;
	size_t	maxconn_per_route;
	size_t	maxconn_per_source;
	size_t	maxconn_per_connector;
	size_t	maxconn_per_relay;
	size_t	maxconn_per_domain;

	time_t	conndelay_host;
	time_t	conndelay_route;
	time_t	conndelay_source;
	time_t	conndelay_connector;
	time_t	conndelay_relay;
	time_t	conndelay_domain;

	time_t	discdelay_route;

	size_t	max_mail_per_session;
	time_t	sessdelay_transaction;
	time_t	sessdelay_keepalive;

	size_t	max_failures_per_session;

	int	family;

	int	task_hiwat;
	int	task_lowat;
	int	task_release;
};

struct mta_relay {
	SPLAY_ENTRY(mta_relay)	 entry;
	uint64_t		 id;

	struct dispatcher	*dispatcher;
	struct mta_domain	*domain;
	struct mta_limits	*limits;
	int			 tls;
	int			 flags;
	char			*backupname;
	int			 backuppref;
	char			*sourcetable;
	uint16_t		 port;
	char			*pki_name;
	char			*ca_name;
	char			*authtable;
	char			*authlabel;
	char			*helotable;
	char			*heloname;
	char			*secret;
	int			 srs;

	int			 state;
	size_t			 ntask;
	TAILQ_HEAD(, mta_task)	 tasks;

	struct tree		 connectors;
	size_t			 sourceloop;
	time_t			 lastsource;
	time_t			 nextsource;

	int			 fail;
	char			*failstr;

#define RELAY_WAIT_MX		0x01
#define RELAY_WAIT_PREFERENCE	0x02
#define RELAY_WAIT_SECRET	0x04
#define RELAY_WAIT_LIMITS	0x08
#define RELAY_WAIT_SOURCE	0x10
#define RELAY_WAIT_CONNECTOR	0x20
#define RELAY_WAIT_SMARTHOST	0x40
#define RELAY_WAITMASK		0x7f
	int			 status;

	int			 refcount;
	size_t			 nconn;
	size_t			 nconn_ready;
	time_t			 lastconn;
};

struct mta_envelope {
	TAILQ_ENTRY(mta_envelope)	 entry;
	uint64_t			 id;
	uint64_t			 session;
	time_t				 creation;
	char				*smtpname;
	char				*dest;
	char				*rcpt;
	struct mta_task			*task;
	int				 delivery;

	int				 ext;
	char				*dsn_orcpt;
	char				dsn_envid[DSN_ENVID_LEN+1];
	uint8_t				dsn_notify;
	enum dsn_ret			dsn_ret;

	char				 status[LINE_MAX];
};

struct mta_task {
	TAILQ_ENTRY(mta_task)		 entry;
	struct mta_relay		*relay;
	uint32_t			 msgid;
	TAILQ_HEAD(, mta_envelope)	 envelopes;
	char				*sender;
};

struct passwd;

struct queue_backend {
	int	(*init)(struct passwd *, int, const char *);
};

struct compress_backend {
	size_t	(*compress_chunk)(void *, size_t, void *, size_t);
	size_t	(*uncompress_chunk)(void *, size_t, void *, size_t);
	int	(*compress_file)(FILE *, FILE *);
	int	(*uncompress_file)(FILE *, FILE *);
};

/* auth structures */
enum auth_type {
	AUTH_BSD,
	AUTH_PWD,
};

struct auth_backend {
	int	(*authenticate)(char *, char *);
};

struct scheduler_backend {
	int	(*init)(const char *);

	int	(*insert)(struct scheduler_info *);
	size_t	(*commit)(uint32_t);
	size_t	(*rollback)(uint32_t);

	int	(*update)(struct scheduler_info *);
	int	(*delete)(uint64_t);
	int	(*hold)(uint64_t, uint64_t);
	int	(*release)(int, uint64_t, int);

	int	(*batch)(int, int*, size_t*, uint64_t*, int*);

	size_t	(*messages)(uint32_t, uint32_t *, size_t);
	size_t	(*envelopes)(uint64_t, struct evpstate *, size_t);
	int	(*schedule)(uint64_t);
	int	(*remove)(uint64_t);
	int	(*suspend)(uint64_t);
	int	(*resume)(uint64_t);
	int	(*query)(uint64_t);
};

enum stat_type {
	STAT_COUNTER,
	STAT_TIMESTAMP,
	STAT_TIMEVAL,
	STAT_TIMESPEC,
};

struct stat_value {
	enum stat_type	type;
	union stat_v {
		size_t		counter;
		time_t		timestamp;
		struct timeval	tv;
		struct timespec	ts;
	} u;
};

#define	STAT_KEY_SIZE	1024
struct stat_kv {
	void	*iter;
	char	key[STAT_KEY_SIZE];
	struct stat_value	val;
};

struct stat_backend {
	void	(*init)(void);
	void	(*close)(void);
	void	(*increment)(const char *, size_t);
	void	(*decrement)(const char *, size_t);
	void	(*set)(const char *, const struct stat_value *);
	int	(*iter)(void **, char **, struct stat_value *);
};

struct stat_digest {
	time_t			 startup;
	time_t			 timestamp;

	size_t			 clt_connect;
	size_t			 clt_disconnect;

	size_t			 evp_enqueued;
	size_t			 evp_dequeued;

	size_t			 evp_expired;
	size_t			 evp_removed;
	size_t			 evp_bounce;

	size_t			 dlv_ok;
	size_t			 dlv_permfail;
	size_t			 dlv_tempfail;
	size_t			 dlv_loop;
};


struct mproc {
	pid_t		 pid;
	char		*name;
	int		 proc;
	void		(*handler)(struct mproc *, struct imsg *);
	struct imsgbuf	 imsgbuf;

	char		*m_buf;
	size_t		 m_alloc;
	size_t		 m_pos;
	uint32_t	 m_type;
	uint32_t	 m_peerid;
	pid_t		 m_pid;
	int		 m_fd;

	int		 enable;
	short		 events;
	struct event	 ev;
	void		*data;
};

struct msg {
	const uint8_t	*pos;
	const uint8_t	*end;
};

extern enum smtp_proc_type	smtpd_process;

extern int tracing;
extern int foreground_log;
extern int profiling;

extern struct mproc *p_control;
extern struct mproc *p_parent;
extern struct mproc *p_lka;
extern struct mproc *p_queue;
extern struct mproc *p_scheduler;
extern struct mproc *p_dispatcher;
extern struct mproc *p_ca;

extern struct smtpd	*env;
extern void (*imsg_callback)(struct mproc *, struct imsg *);

/* inter-process structures */

struct bounce_req_msg {
	uint64_t		evpid;
	time_t			timestamp;
	struct delivery_bounce	bounce;
};

enum dns_error {
	DNS_OK = 0,
	DNS_RETRY,
	DNS_EINVAL,
	DNS_ENONAME,
	DNS_ENOTFOUND,
	/* RFC 7505 */
	DNS_NULLMX,
};

enum lka_resp_status {
	LKA_OK,
	LKA_TEMPFAIL,
	LKA_PERMFAIL
};

enum filter_type {
	FILTER_TYPE_BUILTIN,
	FILTER_TYPE_PROC,
	FILTER_TYPE_CHAIN,
};

enum filter_subsystem {
	FILTER_SUBSYSTEM_SMTP_IN	= 1<<0,
	FILTER_SUBSYSTEM_SMTP_OUT	= 1<<1,
};

struct filter_proc {
	const char		       *command;
	const char		       *user;
	const char		       *group;
	const char		       *chroot;
	int				errfd;
	enum filter_subsystem		filter_subsystem;
};

struct filter_config {
	char			       *name;
	enum filter_subsystem		filter_subsystem;
	enum filter_type		filter_type;
	enum filter_phase               phase;
	char                           *reject;
	char                           *disconnect;
	char                           *rewrite;
	char                           *report;
	uint8_t				junk;
  	uint8_t				bypass;
	char                           *proc;

	const char		      **chain;
	size_t				chain_size;
	struct dict			chain_procs;

	int8_t				not_fcrdns;
	int8_t				fcrdns;

	int8_t				not_rdns;
	int8_t				rdns;

	int8_t                          not_rdns_table;
	struct table                   *rdns_table;

	int8_t                          not_rdns_regex;
	struct table                   *rdns_regex;

	int8_t                          not_src_table;
	struct table                   *src_table;

	int8_t                          not_src_regex;
	struct table                   *src_regex;

	int8_t                          not_helo_table;
	struct table                   *helo_table;

	int8_t                          not_helo_regex;
	struct table                   *helo_regex;

  	int8_t                          not_auth;
	int8_t				auth;

  	int8_t                          not_auth_table;
	struct table                   *auth_table;

	int8_t                          not_auth_regex;
	struct table                   *auth_regex;

	int8_t                          not_mail_from_table;
	struct table                   *mail_from_table;

	int8_t                          not_mail_from_regex;
	struct table                   *mail_from_regex;

	int8_t                          not_rcpt_to_table;
	struct table                   *rcpt_to_table;

	int8_t                          not_rcpt_to_regex;
	struct table                   *rcpt_to_regex;

};

enum filter_status {
	FILTER_PROCEED,
	FILTER_REPORT,
	FILTER_REWRITE,
	FILTER_REJECT,
	FILTER_DISCONNECT,
	FILTER_JUNK,
};

enum ca_resp_status {
	CA_OK,
	CA_FAIL
};

enum mda_resp_status {
	MDA_OK,
	MDA_TEMPFAIL,
	MDA_PERMFAIL
};

struct msg_walkinfo {
	struct event	 ev;
	uint32_t	 msgid;
	uint32_t	 peerid;
	size_t		 n_evp;
	void		*data;
	int		 done;
};


enum dispatcher_type {
	DISPATCHER_LOCAL,
	DISPATCHER_REMOTE,
	DISPATCHER_BOUNCE,
};

struct dispatcher_local {
	uint8_t is_mbox;	/* only for MBOX */

	uint8_t	expand_only;
	uint8_t	forward_only;

	char	*mda_wrapper;
	char	*command;

	char	*table_alias;
	char	*table_virtual;
	char	*table_userbase;

	char	*user;
};

struct dispatcher_remote {
	char	*helo;
	char	*helo_source;

	char	*source;

	struct tls_config *tls_config;
	char	*ca;
	char	*pki;

	char	*mail_from;

	char	*smarthost;
	int	 smarthost_domain;

	char	*auth;
	int	 tls_required;
	int	 tls_verify;
	char	*tls_protocols;
	char	*tls_ciphers;

	int	 backup;
	char	*backupmx;

	char	*filtername;

	int	 srs;
};

struct dispatcher_bounce {
};

struct dispatcher {
	enum dispatcher_type			type;
	union dispatcher_agent {
		struct dispatcher_local		local;
		struct dispatcher_remote  	remote;
		struct dispatcher_bounce  	bounce;
	} u;

	time_t	ttl;
};

struct rule {
	TAILQ_ENTRY(rule)	r_entry;

	uint8_t	reject;

	int8_t	flag_tag;
	int8_t	flag_from;
	int8_t	flag_for;
	int8_t	flag_from_rdns;
	int8_t	flag_from_socket;

	int8_t	flag_tag_regex;
	int8_t	flag_from_regex;
	int8_t	flag_for_regex;

	int8_t	flag_smtp_helo;
	int8_t	flag_smtp_starttls;
	int8_t	flag_smtp_auth;
	int8_t	flag_smtp_mail_from;
	int8_t	flag_smtp_rcpt_to;

	int8_t	flag_smtp_helo_regex;
	int8_t	flag_smtp_starttls_regex;
	int8_t	flag_smtp_auth_regex;
	int8_t	flag_smtp_mail_from_regex;
	int8_t	flag_smtp_rcpt_to_regex;


	char	*table_tag;
	char	*table_from;
	char	*table_for;

	char	*table_smtp_helo;
	char	*table_smtp_auth;
	char	*table_smtp_mail_from;
	char	*table_smtp_rcpt_to;

	char	*dispatcher;
};


/* aliases.c */
int aliases_get(struct expand *, const char *);
int aliases_virtual_get(struct expand *, const struct mailaddr *);


/* bounce.c */
void bounce_add(uint64_t);
void bounce_fd(int);


/* ca.c */
int	 ca(void);
int	 ca_X509_verify(void *, void *, const char *, const char *, const char **);
void	 ca_imsg(struct mproc *, struct imsg *);
void	 ca_init(void);
void	 ca_engine_init(void);


/* compress_backend.c */
struct compress_backend *compress_backend_lookup(const char *);
size_t	compress_chunk(void *, size_t, void *, size_t);
size_t	uncompress_chunk(void *, size_t, void *, size_t);
int	compress_file(FILE *, FILE *);
int	uncompress_file(FILE *, FILE *);

/* config.c */
#define PURGE_LISTENERS		0x01
#define PURGE_TABLES		0x02
#define PURGE_RULES		0x04
#define PURGE_PKI		0x08
#define PURGE_PKI_KEYS		0x10
#define PURGE_DISPATCHERS	0x20
#define PURGE_EVERYTHING	0xff
struct smtpd *config_default(void);
void purge_config(uint8_t);
void config_process(enum smtp_proc_type);
void config_peer(enum smtp_proc_type);


/* control.c */
int control(void);
int control_create_socket(void);


/* crypto.c */
int	crypto_setup(const char *, size_t);
int	crypto_encrypt_file(FILE *, FILE *);
int	crypto_decrypt_file(FILE *, FILE *);
size_t	crypto_encrypt_buffer(const char *, size_t, char *, size_t);
size_t	crypto_decrypt_buffer(const char *, size_t, char *, size_t);


/* dns.c */
void dns_imsg(struct mproc *, struct imsg *);


/* enqueue.c */
int		 enqueue(int, char **, FILE *);


/* envelope.c */
void envelope_set_errormsg(struct envelope *, char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void envelope_set_esc_class(struct envelope *, enum enhanced_status_class);
void envelope_set_esc_code(struct envelope *, enum enhanced_status_code);
int envelope_load_buffer(struct envelope *, const char *, size_t);
int envelope_dump_buffer(const struct envelope *, char *, size_t);


/* expand.c */
int expand_cmp(struct expandnode *, struct expandnode *);
void expand_insert(struct expand *, struct expandnode *);
struct expandnode *expand_lookup(struct expand *, struct expandnode *);
void expand_clear(struct expand *);
void expand_free(struct expand *);
int expand_line(struct expand *, const char *, int);
int expand_to_text(struct expand *, char *, size_t);
RB_PROTOTYPE(expandtree, expandnode, nodes, expand_cmp);


/* forward.c */
int forwards_get(int, struct expand *);


/* limit.c */
void limit_mta_set_defaults(struct mta_limits *);
int limit_mta_set(struct mta_limits *, const char*, int64_t);


/* lka.c */
int lka(void);


/* lka_proc.c */
int lka_proc_ready(void);
void lka_proc_forked(const char *, uint32_t, int);
void lka_proc_errfd(const char *, int);
struct io *lka_proc_get_io(const char *);


/* lka_report.c */
void lka_report_init(void);
void lka_report_register_hook(const char *, const char *);
void lka_report_smtp_link_connect(const char *, struct timeval *, uint64_t, const char *, int,
    const struct sockaddr_storage *, const struct sockaddr_storage *);
void lka_report_smtp_link_disconnect(const char *, struct timeval *, uint64_t);
void lka_report_smtp_link_greeting(const char *, uint64_t, struct timeval *,
    const char *);
void lka_report_smtp_link_identify(const char *, struct timeval *, uint64_t, const char *, const char *);
void lka_report_smtp_link_tls(const char *, struct timeval *, uint64_t, const char *);
void lka_report_smtp_link_auth(const char *, struct timeval *, uint64_t, const char *, const char *);
void lka_report_smtp_tx_reset(const char *, struct timeval *, uint64_t, uint32_t);
void lka_report_smtp_tx_begin(const char *, struct timeval *, uint64_t, uint32_t);
void lka_report_smtp_tx_mail(const char *, struct timeval *, uint64_t, uint32_t, const char *, int);
void lka_report_smtp_tx_rcpt(const char *, struct timeval *, uint64_t, uint32_t, const char *, int);
void lka_report_smtp_tx_envelope(const char *, struct timeval *, uint64_t, uint32_t, uint64_t);
void lka_report_smtp_tx_commit(const char *, struct timeval *, uint64_t, uint32_t, size_t);
void lka_report_smtp_tx_data(const char *, struct timeval *, uint64_t, uint32_t, int);
void lka_report_smtp_tx_rollback(const char *, struct timeval *, uint64_t, uint32_t);
void lka_report_smtp_protocol_client(const char *, struct timeval *, uint64_t, const char *);
void lka_report_smtp_protocol_server(const char *, struct timeval *, uint64_t, const char *);
void lka_report_smtp_filter_response(const char *, struct timeval *, uint64_t,
    int, int, const char *);
void lka_report_smtp_timeout(const char *, struct timeval *, uint64_t);
void lka_report_filter_report(uint64_t, const char *, int, const char *,
    struct timeval *, const char *);
void lka_report_proc(const char *, const char *);


/* lka_filter.c */
void lka_filter_init(void);
void lka_filter_register_hook(const char *, const char *);
void lka_filter_ready(void);
int lka_filter_proc_in_session(uint64_t, const char *);
void lka_filter_begin(uint64_t, const char *);
void lka_filter_end(uint64_t);
void lka_filter_protocol(uint64_t, enum filter_phase, const char *);
void lka_filter_data_begin(uint64_t);
void lka_filter_data_end(uint64_t);


/* lka_session.c */
void lka_session(uint64_t, struct envelope *);
void lka_session_forward_reply(struct forward_req *, int);


/* log.c */
void vlog(int, const char *, va_list);
void logit(int, const char *, ...) __attribute__((format (printf, 2, 3)));


/* mda.c */
void mda_postfork(void);
void mda_postprivdrop(void);
void mda_imsg(struct mproc *, struct imsg *);


/* mda_mbox.c */
void mda_mbox_init(struct deliver *);
void mda_mbox(struct deliver *);


/* mda_unpriv.c */
void mda_unpriv(struct dispatcher *, struct deliver *, const char *, const char *);


/* mda_variables.c */
ssize_t mda_expand_format(char *, size_t, const struct deliver *,
    const struct userinfo *, const char *);


/* makemap.c */
int makemap(int, int, char **);


/* mailaddr.c */
int mailaddr_line(struct maddrmap *, const char *);
void maddrmap_init(struct maddrmap *);
void maddrmap_insert(struct maddrmap *, struct maddrnode *);
void maddrmap_free(struct maddrmap *);


/* mproc.c */
int mproc_fork(struct mproc *, const char*, char **);
void mproc_init(struct mproc *, int);
void mproc_clear(struct mproc *);
void mproc_enable(struct mproc *);
void mproc_disable(struct mproc *);
void mproc_event_add(struct mproc *);
void m_compose(struct mproc *, uint32_t, uint32_t, pid_t, int, void *, size_t);
void m_composev(struct mproc *, uint32_t, uint32_t, pid_t, int,
    const struct iovec *, int);
void m_forward(struct mproc *, struct imsg *);
void m_create(struct mproc *, uint32_t, uint32_t, pid_t, int);
void m_add(struct mproc *, const void *, size_t);
void m_add_int(struct mproc *, int);
void m_add_u32(struct mproc *, uint32_t);
void m_add_size(struct mproc *, size_t);
void m_add_time(struct mproc *, time_t);
void m_add_timeval(struct mproc *, struct timeval *tv);
void m_add_string(struct mproc *, const char *);
void m_add_data(struct mproc *, const void *, size_t);
void m_add_evpid(struct mproc *, uint64_t);
void m_add_msgid(struct mproc *, uint32_t);
void m_add_id(struct mproc *, uint64_t);
void m_add_sockaddr(struct mproc *, const struct sockaddr *);
void m_add_mailaddr(struct mproc *, const struct mailaddr *);
void m_add_envelope(struct mproc *, const struct envelope *);
void m_add_params(struct mproc *, struct dict *);
void m_close(struct mproc *);
void m_flush(struct mproc *);

void m_msg(struct msg *, struct imsg *);
int  m_is_eom(struct msg *);
void m_end(struct msg *);
void m_get_int(struct msg *, int *);
void m_get_size(struct msg *, size_t *);
void m_get_u32(struct msg *, uint32_t *);
void m_get_time(struct msg *, time_t *);
void m_get_timeval(struct msg *, struct timeval *);
void m_get_string(struct msg *, const char **);
void m_get_data(struct msg *, const void **, size_t *);
void m_get_evpid(struct msg *, uint64_t *);
void m_get_msgid(struct msg *, uint32_t *);
void m_get_id(struct msg *, uint64_t *);
void m_get_sockaddr(struct msg *, struct sockaddr *);
void m_get_mailaddr(struct msg *, struct mailaddr *);
void m_get_envelope(struct msg *, struct envelope *);
void m_get_params(struct msg *, struct dict *);
void m_clear_params(struct dict *);


/* mta.c */
void mta_postfork(void);
void mta_postprivdrop(void);
void mta_imsg(struct mproc *, struct imsg *);
void mta_route_ok(struct mta_relay *, struct mta_route *);
void mta_route_error(struct mta_relay *, struct mta_route *);
void mta_route_down(struct mta_relay *, struct mta_route *);
void mta_route_collect(struct mta_relay *, struct mta_route *);
void mta_source_error(struct mta_relay *, struct mta_route *, const char *);
void mta_delivery_log(struct mta_envelope *, const char *, const char *, int, const char *);
void mta_delivery_notify(struct mta_envelope *);
struct mta_task *mta_route_next_task(struct mta_relay *, struct mta_route *);
const char *mta_host_to_text(struct mta_host *);
const char *mta_relay_to_text(struct mta_relay *);


/* mta_session.c */
void mta_session(struct mta_relay *, struct mta_route *, const char *);
void mta_session_imsg(struct mproc *, struct imsg *);


/* parse.y */
int parse_config(struct smtpd *, const char *, int);
int cmdline_symset(char *);


/* queue.c */
int queue(void);


/* queue_backend.c */
uint32_t queue_generate_msgid(void);
uint64_t queue_generate_evpid(uint32_t);
int queue_init(const char *, int);
int queue_close(void);
int queue_message_create(uint32_t *);
int queue_message_delete(uint32_t);
int queue_message_commit(uint32_t);
int queue_message_fd_r(uint32_t);
int queue_message_fd_rw(uint32_t);
int queue_envelope_create(struct envelope *);
int queue_envelope_delete(uint64_t);
int queue_envelope_load(uint64_t, struct envelope *);
int queue_envelope_update(struct envelope *);
int queue_envelope_walk(struct envelope *);
int queue_message_walk(struct envelope *, uint32_t, int *, void **);


/* report_smtp.c */
void report_smtp_link_connect(const char *, uint64_t, const char *, int,
    const struct sockaddr_storage *, const struct sockaddr_storage *);
void report_smtp_link_disconnect(const char *, uint64_t);
void report_smtp_link_greeting(const char *, uint64_t, const char *);
void report_smtp_link_identify(const char *, uint64_t, const char *, const char *);
void report_smtp_link_tls(const char *, uint64_t, const char *);
void report_smtp_link_auth(const char *, uint64_t, const char *, const char *);
void report_smtp_tx_reset(const char *, uint64_t, uint32_t);
void report_smtp_tx_begin(const char *, uint64_t, uint32_t);
void report_smtp_tx_mail(const char *, uint64_t, uint32_t, const char *, int);
void report_smtp_tx_rcpt(const char *, uint64_t, uint32_t, const char *, int);
void report_smtp_tx_envelope(const char *, uint64_t, uint32_t, uint64_t);
void report_smtp_tx_data(const char *, uint64_t, uint32_t, int);
void report_smtp_tx_commit(const char *, uint64_t, uint32_t, size_t);
void report_smtp_tx_rollback(const char *, uint64_t, uint32_t);
void report_smtp_protocol_client(const char *, uint64_t, const char *);
void report_smtp_protocol_server(const char *, uint64_t, const char *);
void report_smtp_filter_response(const char *, uint64_t, int, int, const char *);
void report_smtp_timeout(const char *, uint64_t);


/* ruleset.c */
struct rule *ruleset_match(const struct envelope *);


/* scheduler.c */
int scheduler(void);


/* scheduler_bakend.c */
struct scheduler_backend *scheduler_backend_lookup(const char *);
void scheduler_info(struct scheduler_info *, struct envelope *);


/* dispatcher.c */
int dispatcher(void);
void dispatcher_imsg(struct mproc *, struct imsg *);


/* resolver.c */
void resolver_getaddrinfo(const char *, const char *, const struct addrinfo *,
    void(*)(void *, int, struct addrinfo*), void *);
void resolver_getnameinfo(const struct sockaddr *, int,
    void(*)(void *, int, const char *, const char *), void *);
void resolver_res_query(const char *, int, int,
    void (*cb)(void *, int, int, int, const void *, int), void *);
void resolver_dispatch_request(struct mproc *, struct imsg *);
void resolver_dispatch_result(struct mproc *, struct imsg *);


/* smtp.c */
void smtp_postfork(void);
void smtp_postprivdrop(void);
void smtp_imsg(struct mproc *, struct imsg *);
void smtp_configure(void);
void smtp_collect(void);


/* smtp_session.c */
int smtp_session(struct listener *, int, const struct sockaddr_storage *,
    const char *, struct io *);
void smtp_session_imsg(struct mproc *, struct imsg *);


/* smtpd.c */
void imsg_dispatch(struct mproc *, struct imsg *);
const char *proc_name(enum smtp_proc_type);
const char *proc_title(enum smtp_proc_type);
const char *imsg_to_str(int);
void log_imsg(int, int, struct imsg *);
int fork_proc_backend(const char *, const char *, const char *, int);


/* srs.c */
const char *srs_encode(const char *, const char *);
const char *srs_decode(const char *);


/* stat_backend.c */
struct stat_backend	*stat_backend_lookup(const char *);
void	stat_increment(const char *, size_t);
void	stat_decrement(const char *, size_t);
void	stat_set(const char *, const struct stat_value *);
struct stat_value *stat_counter(size_t);
struct stat_value *stat_timestamp(time_t);
struct stat_value *stat_timeval(struct timeval *);
struct stat_value *stat_timespec(struct timespec *);


/* table.c */
const char *table_service_name(enum table_service);
int table_service_from_name(const char *);
struct table *table_find(struct smtpd *, const char *);
struct table *table_create(struct smtpd *, const char *, const char *,
    const char *);
int	table_config(struct table *);
int	table_open(struct table *);
int	table_update(struct table *);
void	table_close(struct table *);
void	table_dump(struct table *);
int	table_check_use(struct table *, uint32_t, uint32_t);
int	table_check_type(struct table *, uint32_t);
int	table_check_service(struct table *, uint32_t);
int	table_match(struct table *, enum table_service, const char *);
int	table_lookup(struct table *, enum table_service, const char *,
    union lookup *);
int	table_fetch(struct table *, enum table_service, union lookup *);
void table_destroy(struct smtpd *, struct table *);
void table_add(struct table *, const char *, const char *);
int table_domain_match(const char *, const char *);
int table_netaddr_match(const char *, const char *);
int table_mailaddr_match(const char *, const char *);
int table_regex_match(const char *, const char *);
void	table_open_all(struct smtpd *);
void	table_dump_all(struct smtpd *);
void	table_close_all(struct smtpd *);


/* to.c */
int text_to_netaddr(struct netaddr *, const char *);
int text_to_mailaddr(struct mailaddr *, const char *);
int text_to_relayhost(struct relayhost *, const char *);
int text_to_userinfo(struct userinfo *, const char *);
int text_to_credentials(struct credentials *, const char *);
int text_to_expandnode(struct expandnode *, const char *);
uint64_t text_to_evpid(const char *);
uint32_t text_to_msgid(const char *);
const char *sa_to_text(const struct sockaddr *);
const char *ss_to_text(const struct sockaddr_storage *);
const char *time_to_text(time_t);
const char *duration_to_text(time_t);
const char *rule_to_text(struct rule *);
const char *sockaddr_to_text(const struct sockaddr *);
const char *mailaddr_to_text(const struct mailaddr *);
const char *expandnode_to_text(struct expandnode *);
const char *tls_to_text(struct tls *);


/* util.c */
typedef struct arglist arglist;
struct arglist {
	char	**list;
	uint	  num;
	uint	  nalloc;
};
void addargs(arglist *, char *, ...)
	__attribute__((format(printf, 2, 3)));
int bsnprintf(char *, size_t, const char *, ...)
	__attribute__((format (printf, 3, 4)));
int safe_fclose(FILE *);
int hostname_match(const char *, const char *);
int mailaddr_match(const struct mailaddr *, const struct mailaddr *);
int valid_localpart(const char *);
int valid_domainpart(const char *);
int valid_domainname(const char *);
int valid_smtp_response(const char *);
int valid_xtext(const char *);
int secure_file(int, char *, char *, uid_t, int);
int  lowercase(char *, const char *, size_t);
void xlowercase(char *, const char *, size_t);
int  uppercase(char *, const char *, size_t);
uint64_t generate_uid(void);
int ckdir(const char *, mode_t, uid_t, gid_t, int);
int rmtree(char *, int);
int mvpurge(char *, char *);
int mktmpfile(void);
const char *parse_smtp_response(char *, size_t, char **, int *);
int xasprintf(char **, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void *xmalloc(size_t);
void *xcalloc(size_t, size_t);
char *xstrdup(const char *);
void *xmemdup(const void *, size_t);
char *strip(char *);
int io_xprint(struct io *, const char *);
int io_xprintf(struct io *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
int session_socket_error(int);
int getmailname(char *, size_t);
int base64_encode(unsigned char const *, size_t, char *, size_t);
int base64_decode(char const *, unsigned char *, size_t);
int base64_encode_rfc3548(unsigned char const *, size_t,
		      char *, size_t);

void log_trace_verbose(int);
void log_trace0(const char *, ...)
    __attribute__((format (printf, 1, 2)));
#define log_trace(m, ...)  do { if (tracing & (m)) log_trace0(__VA_ARGS__); } while (0)

int parse_table_line(FILE *, char **, size_t *, int *,
    char **, char **, int *);

/* waitq.c */
int  waitq_wait(void *, void (*)(void *, void *, void *), void *);
void waitq_run(void *, void *);


/* runq.c */
struct runq;

int runq_init(struct runq **, void (*)(struct runq *, void *));
int runq_schedule(struct runq *, time_t, void *);
int runq_schedule_at(struct runq *, time_t, void *);
int runq_cancel(struct runq *, void *);
int runq_pending(struct runq *, void *, time_t *);
