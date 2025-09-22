/*	$OpenBSD: httpd.h,v 1.165 2024/10/08 05:28:11 jsg Exp $	*/

/*
 * Copyright (c) 2006 - 2015 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006, 2007 Pierre-Yves Ritschard <pyr@openbsd.org>
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

#ifndef _HTTPD_H
#define _HTTPD_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/time.h>

#include <net/if.h>
#include <netinet/in.h>

#include <stdarg.h>
#include <limits.h>
#include <event.h>
#include <imsg.h>
#include <tls.h>
#include <vis.h>

#include "patterns.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define CONF_FILE		"/etc/httpd.conf"
#define HTTPD_USER		"www"
#define HTTPD_SERVERNAME	"OpenBSD httpd"
#define HTTPD_DOCROOT		"/htdocs"
#define HTTPD_ERRDOCTEMPLATE	"err" /* 3-char name */
#define HTTPD_ERRDOCROOT_MAX	(PATH_MAX - sizeof("000.html"))
#define HTTPD_INDEX		"index.html"
#define HTTPD_FCGI_SOCKET	"/run/slowcgi.sock"
#define HTTPD_LOGROOT		"/logs"
#define HTTPD_ACCESS_LOG	"access.log"
#define HTTPD_ERROR_LOG		"error.log"
#define HTTPD_MAX_ALIAS_IP	16
#define HTTPD_REALM_MAX		255
#define HTTPD_LOCATION_MAX	255
#define HTTPD_DEFAULT_TYPE	{ "bin", "application", "octet-stream", NULL }
#define HTTPD_LOGVIS		VIS_NL|VIS_TAB|VIS_CSTYLE
#define HTTPD_TLS_CERT		"/etc/ssl/server.crt"
#define HTTPD_TLS_KEY		"/etc/ssl/private/server.key"
#define HTTPD_TLS_CONFIG_MAX	511
#define HTTPD_TLS_CIPHERS	"compat"
#define HTTPD_TLS_DHE_PARAMS	"none"
#define HTTPD_TLS_ECDHE_CURVES	"default"
#define HTTPD_FCGI_NAME_MAX	511
#define HTTPD_FCGI_VAL_MAX	511
#define FD_RESERVE		5

#define SERVER_MAX_CLIENTS	1024
#define SERVER_TIMEOUT		600
#define SERVER_REQUESTTIMEOUT	60
#define SERVER_CACHESIZE	-1	/* use default size */
#define SERVER_NUMPROC		3
#define SERVER_MAXHEADERLENGTH	8192
#define SERVER_MAXREQUESTS	100	/* max requests per connection */
#define SERVER_MAXREQUESTBODY	1048576	/* 1M */
#define SERVER_BACKLOG		10
#define SERVER_OUTOF_FD_RETRIES	5
#define SERVER_MAX_PREFETCH	256
#define SERVER_MIN_PREFETCHED	32
#define SERVER_HSTS_DEFAULT_AGE	31536000
#define SERVER_MAX_RANGES	4
#define SERVER_DEF_TLS_LIFETIME	(2 * 3600)
#define SERVER_MIN_TLS_LIFETIME	(60)
#define SERVER_MAX_TLS_LIFETIME	(24 * 3600)

#define MEDIATYPE_NAMEMAX	128	/* file name extension */
#define MEDIATYPE_TYPEMAX	64	/* length of type/subtype */

#define CONFIG_RELOAD		0x00
#define CONFIG_MEDIA		0x01
#define CONFIG_SERVERS		0x02
#define CONFIG_AUTH		0x04
#define CONFIG_ALL		0xff

#define FCGI_CONTENT_SIZE	65535
#define FCGI_DEFAULT_PORT	"9000"

#define PROC_PARENT_SOCK_FILENO	3
#define PROC_MAX_INSTANCES	32

enum httpchunk {
	TOREAD_UNLIMITED		= -1,
	TOREAD_HTTP_HEADER		= -2,
	TOREAD_HTTP_CHUNK_LENGTH	= -3,
	TOREAD_HTTP_CHUNK_TRAILER	= -4,
	TOREAD_HTTP_NONE		= -5,
	TOREAD_HTTP_RANGE		= TOREAD_HTTP_CHUNK_LENGTH
};

#if DEBUG
#define DPRINTF		log_debug
#else
#define DPRINTF(x...)	do {} while(0)
#endif

struct ctl_flags {
	uint8_t		 cf_opts;
	uint32_t	 cf_flags;
	uint8_t		 cf_tls_sid[TLS_MAX_SESSION_ID_LENGTH];
};

TAILQ_HEAD(kvlist, kv);
RB_HEAD(kvtree, kv);

struct kv {
	char			*kv_key;
	char			*kv_value;

	struct kvlist		 kv_children;
	struct kv		*kv_parent;
	TAILQ_ENTRY(kv)		 kv_entry;

	RB_ENTRY(kv)		 kv_node;
};

struct portrange {
	in_port_t		 val[2];
	uint8_t			 op;
};

struct address {
	struct sockaddr_storage	 ss;
	int			 ipproto;
	int			 prefixlen;
	struct portrange	 port;
	char			 ifname[IFNAMSIZ];
	TAILQ_ENTRY(address)	 entry;
};
TAILQ_HEAD(addresslist, address);

/* initially control.h */
struct control_sock {
	const char	*cs_name;
	struct event	 cs_ev;
	struct event	 cs_evt;
	int		 cs_fd;
	int		 cs_restricted;
	void		*cs_env;

	TAILQ_ENTRY(control_sock) cs_entry;
};
TAILQ_HEAD(control_socks, control_sock);

struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	struct privsep_proc	*proc;
	void			*data;
	short			 events;
};

#define IMSG_SIZE_CHECK(imsg, p) do {				\
	if (IMSG_DATA_SIZE(imsg) < sizeof(*p))			\
		fatalx("bad length imsg received");		\
} while (0)
#define IMSG_DATA_SIZE(imsg)	((imsg)->hdr.len - IMSG_HEADER_SIZE)
#define MAX_IMSG_DATA_SIZE	(MAX_IMSGSIZE - IMSG_HEADER_SIZE)

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)	 entry;
	uint8_t			 flags;
	unsigned int		 waiting;
#define CTL_CONN_NOTIFY		 0x01
	struct imsgev		 iev;

};
TAILQ_HEAD(ctl_connlist, ctl_conn);

enum imsg_type {
	IMSG_NONE,
	IMSG_CTL_OK,
	IMSG_CTL_FAIL,
	IMSG_CTL_VERBOSE,
	IMSG_CTL_PROCFD,
	IMSG_CTL_RESET,
	IMSG_CTL_SHUTDOWN,
	IMSG_CTL_RELOAD,
	IMSG_CTL_NOTIFY,
	IMSG_CTL_END,
	IMSG_CTL_START,
	IMSG_CTL_REOPEN,
	IMSG_CFG_SERVER,
	IMSG_CFG_TLS,
	IMSG_CFG_MEDIA,
	IMSG_CFG_AUTH,
	IMSG_CFG_FCGI,
	IMSG_CFG_DONE,
	IMSG_LOG_ACCESS,
	IMSG_LOG_ERROR,
	IMSG_LOG_OPEN,
	IMSG_TLSTICKET_REKEY
};

enum privsep_procid {
	PROC_ALL	= -1,
	PROC_PARENT	= 0,
	PROC_SERVER,
	PROC_LOGGER,
	PROC_MAX
};
extern enum privsep_procid privsep_process;

/* Attach the control socket to the following process */
#define PROC_CONTROL	PROC_LOGGER

struct privsep_pipes {
	int				*pp_pipes[PROC_MAX];
};

struct privsep {
	struct privsep_pipes		*ps_pipes[PROC_MAX];
	struct privsep_pipes		*ps_pp;

	struct imsgev			*ps_ievs[PROC_MAX];
	const char			*ps_title[PROC_MAX];
	uint8_t				 ps_what[PROC_MAX];

	unsigned int			 ps_instances[PROC_MAX];
	unsigned int			 ps_instance;

	struct control_sock		 ps_csock;
	struct control_socks		 ps_rcsocks;

	/* Event and signal handlers */
	struct event			 ps_evsigint;
	struct event			 ps_evsigterm;
	struct event			 ps_evsigchld;
	struct event			 ps_evsighup;
	struct event			 ps_evsigpipe;
	struct event			 ps_evsigusr1;

	int				 ps_noaction;
	struct passwd			*ps_pw;
	struct httpd			*ps_env;
};

struct privsep_proc {
	const char		*p_title;
	enum privsep_procid	 p_id;
	int			(*p_cb)(int, struct privsep_proc *,
				    struct imsg *);
	void			(*p_init)(struct privsep *,
				    struct privsep_proc *);
	const char		*p_chroot;
	struct privsep		*p_ps;
	void			(*p_shutdown)(void);
	struct passwd		*p_pw;
};

struct privsep_fd {
	enum privsep_procid		 pf_procid;
	unsigned int			 pf_instance;
};

enum fcgistate {
	FCGI_READ_HEADER,
	FCGI_READ_CONTENT,
	FCGI_READ_PADDING
};

struct fcgi_data {
	enum fcgistate		 state;
	int			 toread;
	int			 padding_len;
	int			 type;
	int			 chunked;
	int			 end;
	int			 status;
	int			 headersdone;
	int			 headerssent;
};

struct range {
	off_t	start;
	off_t	end;
};

struct range_data {
	struct range		 range[SERVER_MAX_RANGES];
	int			 range_count;
	int			 range_index;
	off_t			 range_toread;

	/* For the Content headers in each part */
	struct media_type	*range_media;
	size_t			 range_total;
};

struct client {
	uint32_t		 clt_id;
	pid_t			 clt_pid;
	void			*clt_srv;
	void			*clt_srv_conf;
	uint32_t		 clt_srv_id;
	struct sockaddr_storage	 clt_srv_ss;
	struct str_match	 clt_srv_match;

	int			 clt_s;
	in_port_t		 clt_port;
	struct sockaddr_storage	 clt_ss;
	struct bufferevent	*clt_bev;
	struct evbuffer		*clt_output;
	struct event		 clt_ev;
	struct http_descriptor	*clt_descreq;
	struct http_descriptor	*clt_descresp;
	int			 clt_sndbufsiz;
	uint64_t		 clt_boundary;

	int			 clt_fd;
	struct tls		*clt_tls_ctx;
	struct bufferevent	*clt_srvbev;
	int			 clt_srvbev_throttled;

	off_t			 clt_toread;
	size_t			 clt_headerlen;
	int			 clt_headersdone;
	unsigned int		 clt_persist;
	unsigned int		 clt_pipelining;
	int			 clt_line;
	int			 clt_done;
	int			 clt_chunk;
	int			 clt_inflight;
	int			 clt_fcgi_count;
	struct range_data	 clt_ranges;
	struct fcgi_data	 clt_fcgi;
	const char		*clt_fcgi_error;
	char			*clt_remote_user;
	struct evbuffer		*clt_srvevb;

	struct evbuffer		*clt_log;
	struct timeval		 clt_timeout;
	struct timeval		 clt_tv_start;
	struct timeval		 clt_tv_last;
	struct event		 clt_inflightevt;

	SPLAY_ENTRY(client)	 clt_nodes;
};
SPLAY_HEAD(client_tree, client);

#define SRVFLAG_INDEX		0x00000001
#define SRVFLAG_NO_INDEX	0x00000002
#define SRVFLAG_AUTO_INDEX	0x00000004
#define SRVFLAG_NO_AUTO_INDEX	0x00000008
#define SRVFLAG_ROOT		0x00000010
#define SRVFLAG_LOCATION	0x00000020
#define SRVFLAG_FCGI		0x00000040
#define SRVFLAG_NO_FCGI		0x00000080
#define SRVFLAG_LOG		0x00000100
#define SRVFLAG_NO_LOG		0x00000200
#define SRVFLAG_ERRDOCS		0x00000400
#define SRVFLAG_SYSLOG		0x00000800
#define SRVFLAG_NO_SYSLOG	0x00001000
#define SRVFLAG_TLS		0x00002000
#define SRVFLAG_ACCESS_LOG	0x00004000
#define SRVFLAG_ERROR_LOG	0x00008000
#define SRVFLAG_AUTH		0x00010000
#define SRVFLAG_NO_AUTH		0x00020000
#define SRVFLAG_BLOCK		0x00040000
#define SRVFLAG_NO_BLOCK	0x00080000
#define SRVFLAG_LOCATION_MATCH	0x00100000
#define SRVFLAG_SERVER_MATCH	0x00200000
#define SRVFLAG_SERVER_HSTS	0x00400000
#define SRVFLAG_DEFAULT_TYPE	0x00800000
#define SRVFLAG_PATH_REWRITE	0x01000000
#define SRVFLAG_NO_PATH_REWRITE	0x02000000
#define SRVFLAG_GZIP_STATIC	0x04000000
#define SRVFLAG_LOCATION_FOUND	0x40000000
#define SRVFLAG_LOCATION_NOT_FOUND 0x80000000

#define SRVFLAG_BITS							\
	"\10\01INDEX\02NO_INDEX\03AUTO_INDEX\04NO_AUTO_INDEX"		\
	"\05ROOT\06LOCATION\07FCGI\10NO_FCGI\11LOG\12NO_LOG\13ERRDOCS"	\
	"\14SYSLOG\15NO_SYSLOG\16TLS\17ACCESS_LOG\20ERROR_LOG"		\
	"\21AUTH\22NO_AUTH\23BLOCK\24NO_BLOCK\25LOCATION_MATCH"		\
	"\26SERVER_MATCH\27SERVER_HSTS\30DEFAULT_TYPE\31PATH\32NO_PATH" \
	"\37LOCATION_FOUND\40LOCATION_NOT_FOUND"

#define TCPFLAG_NODELAY		0x01
#define TCPFLAG_NNODELAY	0x02
#define TCPFLAG_SACK		0x04
#define TCPFLAG_NSACK		0x08
#define TCPFLAG_BUFSIZ		0x10
#define TCPFLAG_IPTTL		0x20
#define TCPFLAG_IPMINTTL	0x40
#define TCPFLAG_NSPLICE		0x80
#define TCPFLAG_DEFAULT		0x00

#define TCPFLAG_BITS						\
	"\10\01NODELAY\02NO_NODELAY\03SACK\04NO_SACK"		\
	"\05SOCKET_BUFFER_SIZE\06IP_TTL\07IP_MINTTL\10NO_SPLICE"

#define HSTSFLAG_SUBDOMAINS	0x01
#define HSTSFLAG_PRELOAD	0x02
#define HSTSFLAG_BITS		"\10\01SUBDOMAINS\02PRELOAD"

#define TLSFLAG_CA		0x01
#define TLSFLAG_CRL		0x02
#define TLSFLAG_OPTIONAL	0x04
#define TLSFLAG_BITS		"\10\01CA\02CRL\03OPTIONAL"

enum log_format {
	LOG_FORMAT_COMMON,
	LOG_FORMAT_COMBINED,
	LOG_FORMAT_CONNECTION,
	LOG_FORMAT_FORWARDED
};

struct log_file {
	char			log_name[PATH_MAX];
	int			log_fd;
	uint32_t		log_id;
	TAILQ_ENTRY(log_file)	log_entry;
};
extern TAILQ_HEAD(log_files, log_file) log_files;

struct media_type {
	char			 media_name[MEDIATYPE_NAMEMAX];
	char			 media_type[MEDIATYPE_TYPEMAX];
	char			 media_subtype[MEDIATYPE_TYPEMAX];
	char			*media_encoding;
	RB_ENTRY(media_type)	 media_entry;
};
RB_HEAD(mediatypes, media_type);

struct auth {
	char			 auth_htpasswd[PATH_MAX];
	uint32_t		 auth_id;
	TAILQ_ENTRY(auth)	 auth_entry;
};
TAILQ_HEAD(serverauth, auth);

struct server_tls_ticket {
	uint32_t	tt_id;
	uint32_t	tt_keyrev;
	unsigned char	tt_key[TLS_TICKET_KEY_SIZE];
};

struct fastcgi_param {
	char			name[HTTPD_FCGI_NAME_MAX];
	char			value[HTTPD_FCGI_VAL_MAX];

	TAILQ_ENTRY(fastcgi_param) entry;
};
TAILQ_HEAD(server_fcgiparams, fastcgi_param);

struct server_config {
	uint32_t		 id;
	uint32_t		 parent_id;
	char			 name[HOST_NAME_MAX+1];
	char			 location[HTTPD_LOCATION_MAX];
	char			 root[PATH_MAX];
	char			 path[PATH_MAX];
	char			 index[PATH_MAX];
	char			 accesslog[PATH_MAX];
	char			 errorlog[PATH_MAX];
	struct media_type	 default_type;

	struct sockaddr_storage	 fastcgi_ss;

	in_port_t		 port;
	struct sockaddr_storage	 ss;
	int			 prefixlen;
	struct timeval		 timeout;
	struct timeval		 requesttimeout;
	uint32_t		 maxrequests;
	size_t			 maxrequestbody;

	uint8_t			*tls_ca;
	char			*tls_ca_file;
	size_t			 tls_ca_len;
	uint8_t			*tls_cert;
	size_t			 tls_cert_len;
	char			*tls_cert_file;
	char			 tls_ciphers[HTTPD_TLS_CONFIG_MAX];
	uint8_t			*tls_crl;
	char			*tls_crl_file;
	size_t			 tls_crl_len;
	char			 tls_dhe_params[HTTPD_TLS_CONFIG_MAX];
	char			 tls_ecdhe_curves[HTTPD_TLS_CONFIG_MAX];
	uint8_t			 tls_flags;
	uint8_t			*tls_key;
	size_t			 tls_key_len;
	char			*tls_key_file;
	uint32_t		 tls_protocols;
	uint8_t			*tls_ocsp_staple;
	size_t			 tls_ocsp_staple_len;
	char			*tls_ocsp_staple_file;
	struct server_tls_ticket tls_ticket_key;
	int			 tls_ticket_lifetime;

	uint32_t		 flags;
	int			 strip;
	uint8_t			 tcpflags;
	int			 tcpbufsiz;
	int			 tcpbacklog;
	uint8_t			 tcpipttl;
	uint8_t			 tcpipminttl;

	enum log_format		 logformat;
	struct log_file		*logaccess;
	struct log_file		*logerror;

	char			 auth_realm[HTTPD_REALM_MAX];
	uint32_t		 auth_id;
	const struct auth	*auth;

	int			 return_code;
	char			*return_uri;
	off_t			 return_uri_len;

	int			 hsts_max_age;
	uint8_t			 hsts_flags;

	struct server_fcgiparams fcgiparams;
	int			 fcgistrip;
	char			 errdocroot[HTTPD_ERRDOCROOT_MAX];

	TAILQ_ENTRY(server_config) entry;
};
TAILQ_HEAD(serverhosts, server_config);

enum tls_config_type {
	TLS_CFG_CA,
	TLS_CFG_CERT,
	TLS_CFG_CRL,
	TLS_CFG_KEY,
	TLS_CFG_OCSP_STAPLE,
};

struct tls_config {
	uint32_t		 id;

	enum tls_config_type	 tls_type;
	size_t			 tls_len;
	size_t			 tls_chunk_len;
	size_t			 tls_chunk_offset;
};

struct server {
	TAILQ_ENTRY(server)	 srv_entry;
	struct server_config	 srv_conf;
	struct serverhosts	 srv_hosts;

	int			 srv_s;
	struct event		 srv_ev;
	struct event		 srv_evt;

	struct tls		 *srv_tls_ctx;
	struct tls_config	 *srv_tls_config;

	struct client_tree	 srv_clients;
};
TAILQ_HEAD(serverlist, server);

struct httpd {
	uint8_t			 sc_opts;
	uint32_t		 sc_flags;
	const char		*sc_conffile;
	struct event		 sc_ev;
	uint16_t		 sc_prefork_server;
	uint16_t		 sc_id;
	int			 sc_paused;
	char			*sc_chroot;
	char			*sc_logdir;

	uint8_t			 sc_tls_sid[TLS_MAX_SESSION_ID_LENGTH];

	struct serverlist	*sc_servers;
	struct mediatypes	*sc_mediatypes;
	struct media_type	 sc_default_type;
	struct serverauth	*sc_auth;

	struct privsep		*sc_ps;
	int			 sc_reload;

	int			 sc_custom_errdocs;
	char			 sc_errdocroot[HTTPD_ERRDOCROOT_MAX];
};

#define HTTPD_OPT_VERBOSE		0x01
#define HTTPD_OPT_NOACTION		0x04

/* control.c */
int	 control_init(struct privsep *, struct control_sock *);
int	 control_listen(struct control_sock *);
void	 control_cleanup(struct control_sock *);
void	 control_dispatch_imsg(int, short, void *);
void	 control_imsg_forward(struct privsep *, struct imsg *);
struct ctl_conn	*
	 control_connbyfd(int);

/* parse.y */
int	 parse_config(const char *, struct httpd *);
int	 load_config(const char *, struct httpd *);
int	 cmdline_symset(char *);

/* server.c */
void	 server(struct privsep *, struct privsep_proc *);
int	 server_tls_cmp(struct server *, struct server *);
int	 server_tls_load_ca(struct server *);
int	 server_tls_load_crl(struct server *);
int	 server_tls_load_keypair(struct server *);
int	 server_tls_load_ocsp(struct server *);
void	 server_generate_ticket_key(struct server_config *);
int	 server_privinit(struct server *);
void	 server_purge(struct server *);
void	 serverconfig_free(struct server_config *);
void	 serverconfig_reset(struct server_config *);
int	 server_socket_af(struct sockaddr_storage *, in_port_t);
in_port_t
	 server_socket_getport(struct sockaddr_storage *);
int	 server_socket_connect(struct sockaddr_storage *, in_port_t,
	    struct server_config *);
void	 server_write(struct bufferevent *, void *);
void	 server_read(struct bufferevent *, void *);
void	 server_error(struct bufferevent *, short, void *);
void	 server_log(struct client *, const char *);
void	 server_sendlog(struct server_config *, int, const char *, ...)
	    __attribute__((__format__ (printf, 3, 4)));
void	 server_close(struct client *, const char *);
void	 server_dump(struct client *, const void *, size_t);
int	 server_client_cmp(struct client *, struct client *);
int	 server_bufferevent_printf(struct client *, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
int	 server_bufferevent_print(struct client *, const char *);
int	 server_bufferevent_write_buffer(struct client *,
	    struct evbuffer *);
int	 server_bufferevent_write_chunk(struct client *,
	    struct evbuffer *, size_t);
int	 server_bufferevent_add(struct event *, int);
int	 server_bufferevent_write(struct client *, void *, size_t);
struct server *
	 server_byaddr(struct sockaddr *, in_port_t);
struct server_config *
	 serverconfig_byid(uint32_t);
int	 server_foreach(int (*)(struct server *,
	    struct server_config *, void *), void *);
struct server *
	 server_match(struct server *, int);

SPLAY_PROTOTYPE(client_tree, client, clt_nodes, server_client_cmp);

/* server_http.c */
void	 server_http_init(struct server *);
void	 server_http(void);
int	 server_httpdesc_init(struct client *);
void	 server_read_http(struct bufferevent *, void *);
void	 server_abort_http(struct client *, unsigned int, const char *);
unsigned int
	 server_httpmethod_byname(const char *);
const char
	*server_httpmethod_byid(unsigned int);
const char
	*server_httperror_byid(unsigned int);
void	 server_read_httpcontent(struct bufferevent *, void *);
void	 server_read_httpchunks(struct bufferevent *, void *);
void	 server_read_httprange(struct bufferevent *, void *);
int	 server_writeheader_http(struct client *clt, struct kv *, void *);
int	 server_headers(struct client *, void *,
	    int (*)(struct client *, struct kv *, void *), void *);
int	 server_writeresponse_http(struct client *);
int	 server_response_http(struct client *, unsigned int,
	    struct media_type *, off_t, time_t);
void	 server_reset_http(struct client *);
void	 server_close_http(struct client *);
int	 server_response(struct httpd *, struct client *);
const char *
	 server_root_strip(const char *, int);
struct server_config *
	 server_getlocation(struct client *, const char *);
int	 server_locationaccesstest(struct server_config *, const char *);
const char *
	 server_http_host(struct sockaddr_storage *, char *, size_t);
char	*server_http_parsehost(char *, char *, size_t, int *);
ssize_t	 server_http_time(time_t, char *, size_t);
int	 server_log_http(struct client *, unsigned int, size_t);

/* server_file.c */
int	 server_file(struct httpd *, struct client *);
void	 server_file_error(struct bufferevent *, short, void *);

/* server_fcgi.c */
int	 server_fcgi(struct httpd *, struct client *);
int	 fcgi_add_stdin(struct client *, struct evbuffer *);

/* httpd.c */
void		 event_again(struct event *, int, short,
		    void (*)(int, short, void *),
		    struct timeval *, struct timeval *, void *);
int		 expand_string(char *, size_t, const char *, const char *);
const char	*url_decode(char *);
char		*url_encode(const char *);
const char	*canonicalize_path(const char *, char *, size_t);
size_t		 path_info(char *);
char		*escape_html(const char *);
void		 socket_rlimit(int);
char		*evbuffer_getline(struct evbuffer *);
char		*get_string(uint8_t *, size_t);
void		*get_data(uint8_t *, size_t);
int		 sockaddr_cmp(struct sockaddr *, struct sockaddr *, int);
struct in6_addr *prefixlen2mask6(uint8_t, uint32_t *);
uint32_t	 prefixlen2mask(uint8_t);
int		 accept_reserve(int, struct sockaddr *, socklen_t *, int,
		    volatile int *);
struct kv	*kv_add(struct kvtree *, char *, char *);
int		 kv_set(struct kv *, char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
int		 kv_setkey(struct kv *, char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
void		 kv_delete(struct kvtree *, struct kv *);
struct kv	*kv_extend(struct kvtree *, struct kv *, char *);
void		 kv_purge(struct kvtree *);
void		 kv_free(struct kv *);
struct kv	*kv_find(struct kvtree *, struct kv *);
int		 kv_cmp(struct kv *, struct kv *);
struct media_type
		*media_add(struct mediatypes *, struct media_type *);
void		 media_delete(struct mediatypes *, struct media_type *);
void		 media_purge(struct mediatypes *);
struct media_type *
		 media_find(struct mediatypes *, const char *);
struct media_type *
		 media_find_config(struct httpd *, struct server_config *,
		    const char *);
int		 media_cmp(struct media_type *, struct media_type *);
RB_PROTOTYPE(kvtree, kv, kv_node, kv_cmp);
RB_PROTOTYPE(mediatypes, media_type, media_entry, media_cmp);
struct auth	*auth_add(struct serverauth *, struct auth *);
struct auth	*auth_byid(struct serverauth *, uint32_t);
void		 auth_free(struct serverauth *, struct auth *);
const char	*print_host(struct sockaddr_storage *, char *, size_t);
const char	*printb_flags(const uint32_t, const char *);
void		 getmonotime(struct timeval *);

extern struct httpd *httpd_env;

/* log.c */
void	log_init(int, int);
void	log_procinit(const char *);
void	log_setverbose(int);
int	log_getverbose(void);
void	log_warn(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_warnx(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_info(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_debug(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	logit(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
void	vlog(int, const char *, va_list)
	    __attribute__((__format__ (printf, 2, 0)));
__dead void fatal(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
__dead void fatalx(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));

/* proc.c */
enum privsep_procid
	    proc_getid(struct privsep_proc *, unsigned int, const char *);
void	 proc_init(struct privsep *, struct privsep_proc *, unsigned int, int,
	    int, char **, enum privsep_procid);
void	 proc_kill(struct privsep *);
void	 proc_connect(struct privsep *);
void	 proc_dispatch(int, short event, void *);
void	 proc_run(struct privsep *, struct privsep_proc *,
	    struct privsep_proc *, unsigned int,
	    void (*)(struct privsep *, struct privsep_proc *, void *), void *);
void	 proc_range(struct privsep *, enum privsep_procid, int *, int *);
int	 proc_compose_imsg(struct privsep *, enum privsep_procid, int,
	    u_int16_t, u_int32_t, int, void *, u_int16_t);
int	 proc_compose(struct privsep *, enum privsep_procid,
	    uint16_t, void *, uint16_t);
int	 proc_composev_imsg(struct privsep *, enum privsep_procid, int,
	    u_int16_t, u_int32_t, int, const struct iovec *, int);
int	 proc_composev(struct privsep *, enum privsep_procid,
	    uint16_t, const struct iovec *, int);
int	 proc_forward_imsg(struct privsep *, struct imsg *,
	    enum privsep_procid, int);
struct imsgbuf *
	 proc_ibuf(struct privsep *, enum privsep_procid, int);
struct imsgev *
	 proc_iev(struct privsep *, enum privsep_procid, int);
int	 proc_flush_imsg(struct privsep *, enum privsep_procid, int);
void	 imsg_event_add(struct imsgev *);
int	 imsg_compose_event(struct imsgev *, uint16_t, uint32_t,
	    pid_t, int, void *, uint16_t);
int	 imsg_composev_event(struct imsgev *, uint16_t, uint32_t,
	    pid_t, int, const struct iovec *, int);

/* config.c */
int	 config_init(struct httpd *);
void	 config_purge(struct httpd *, unsigned int);
int	 config_setreset(struct httpd *, unsigned int);
int	 config_getreset(struct httpd *, struct imsg *);
int	 config_getcfg(struct httpd *, struct imsg *);
int	 config_setserver(struct httpd *, struct server *);
int	 config_setserver_tls(struct httpd *, struct server *);
int	 config_setserver_fcgiparams(struct httpd *, struct server *);
int	 config_getserver(struct httpd *, struct imsg *);
int	 config_getserver_tls(struct httpd *, struct imsg *);
int	 config_getserver_fcgiparams(struct httpd *, struct imsg *);
int	 config_setmedia(struct httpd *, struct media_type *);
int	 config_getmedia(struct httpd *, struct imsg *);
int	 config_setauth(struct httpd *, struct auth *);
int	 config_getauth(struct httpd *, struct imsg *);

/* logger.c */
void	 logger(struct privsep *, struct privsep_proc *);
int	 logger_open_priv(struct imsg *);

#endif /* _HTTPD_H */
