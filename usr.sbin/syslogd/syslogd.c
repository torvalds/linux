/*	$OpenBSD: syslogd.c,v 1.287 2025/06/26 19:10:13 bluhm Exp $	*/

/*
 * Copyright (c) 2014-2021 Alexander Bluhm <bluhm@genua.de>
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

/*
 * Copyright (c) 1983, 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 *  syslogd -- log system messages
 *
 * This program implements a system log. It takes a series of lines.
 * Each line may have a priority, signified as "<n>" as
 * the first characters of the line.  If this is
 * not present, a default priority is used.
 *
 * To kill syslogd, send a signal 15 (terminate).  A signal 1 (hup) will
 * cause it to reread its configuration file.
 *
 * Defined Constants:
 *
 * MAXLINE -- the maximum line length that can be handled.
 * DEFUPRI -- the default priority for user messages
 * DEFSPRI -- the default priority for kernel messages
 *
 * Author: Eric Allman
 * extensive changes by Ralph Campbell
 * more extensive changes by Eric Allman (again)
 * memory buffer logging by Damien Miller
 * IPv6, libevent, syslog over TCP and TLS by Alexander Bluhm
 */

#define MAX_UDPMSG	1180		/* maximum UDP send size */
#define MIN_MEMBUF	(LOG_MAXLINE * 4) /* Minimum memory buffer size */
#define MAX_MEMBUF	(256 * 1024)	/* Maximum memory buffer size */
#define MAX_MEMBUF_NAME	64		/* Max length of membuf log name */
#define MAX_TCPBUF	(256 * 1024)	/* Maximum tcp event buffer size */
#define	MAXSVLINE	120		/* maximum saved line length */
#define FD_RESERVE	5		/* file descriptors not accepted */
#define DEFUPRI		(LOG_USER|LOG_NOTICE)
#define DEFSPRI		(LOG_KERN|LOG_CRIT)
#define TIMERINTVL	30		/* interval for checking flush, mark */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/msgbuf.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tls.h>
#include <unistd.h>
#include <utmp.h>
#include <vis.h>

#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))
#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#define SYSLOG_NAMES
#include <sys/syslog.h>

#include "log.h"
#include "syslogd.h"
#include "evbuffer_tls.h"
#include "parsemsg.h"

char *ConfFile = _PATH_LOGCONF;
const char ctty[] = _PATH_CONSOLE;

#define MAXUNAMES	20	/* maximum number of user names */


/*
 * Flags to logmsg().
 */

#define IGN_CONS	0x001	/* don't print on console */
#define SYNC_FILE	0x002	/* do fsync on file after printing */
#define MARK		0x008	/* this message is a mark */

/*
 * This structure represents the files that will have log
 * copies printed.
 */

struct filed {
	SIMPLEQ_ENTRY(filed) f_next;	/* next in linked list */
	int	f_type;			/* entry type, see below */
	int	f_file;			/* file descriptor */
	time_t	f_time;			/* time this was last written */
	u_char	f_pmask[LOG_NFACILITIES+1];	/* priority mask */
	char	*f_program;		/* program this applies to */
	char	*f_hostname;		/* host this applies to */
	union {
		char	f_uname[MAXUNAMES][UT_NAMESIZE+1];
		struct {
			char	f_loghost[1+4+3+1+NI_MAXHOST+1+NI_MAXSERV];
				/* @proto46://[hostname]:servname\0 */
			struct sockaddr_storage	 f_addr;
			struct buffertls	 f_buftls;
			struct bufferevent	*f_bufev;
			struct event		 f_ev;
			struct tls		*f_ctx;
			char			*f_ipproto;
			char			*f_host;
			char			*f_port;
			int			 f_retrywait;
		} f_forw;		/* forwarding address */
		char	f_fname[PATH_MAX];
		struct {
			char	f_mname[MAX_MEMBUF_NAME];
			struct ringbuf *f_rb;
			int	f_overflow;
			int	f_attached;
			size_t	f_len;
		} f_mb;		/* Memory buffer */
	} f_un;
	char	f_prevline[MAXSVLINE];		/* last message logged */
	char	f_lasttime[33];			/* time of last occurrence */
	char	f_prevhost[HOST_NAME_MAX+1];	/* host from which recd. */
	int	f_prevpri;			/* pri of f_prevline */
	int	f_prevlen;			/* length of f_prevline */
	int	f_prevcount;			/* repetition cnt of prevline */
	unsigned int f_repeatcount;		/* number of "repeated" msgs */
	int	f_quick;			/* abort when matched */
	int	f_dropped;			/* warn, dropped message */
	time_t	f_lasterrtime;			/* last error was reported */
};

/*
 * Intervals at which we flush out "message repeated" messages,
 * in seconds after previous message is logged.  After each flush,
 * we move to the next interval until we reach the largest.
 */
int	repeatinterval[] = { 30, 120, 600 };	/* # of secs before flush */
#define	MAXREPEAT ((sizeof(repeatinterval) / sizeof(repeatinterval[0])) - 1)
#define	REPEATTIME(f)	((f)->f_time + repeatinterval[(f)->f_repeatcount])
#define	BACKOFF(f)	{ if (++(f)->f_repeatcount > MAXREPEAT) \
				(f)->f_repeatcount = MAXREPEAT; \
			}

/* values for f_type */
#define F_UNUSED	0		/* unused entry */
#define F_FILE		1		/* regular file */
#define F_TTY		2		/* terminal */
#define F_CONSOLE	3		/* console terminal */
#define F_FORWUDP	4		/* remote machine via UDP */
#define F_USERS		5		/* list of users */
#define F_WALL		6		/* everyone logged on */
#define F_MEMBUF	7		/* memory buffer */
#define F_PIPE		8		/* pipe to external program */
#define F_FORWTCP	9		/* remote machine via TCP */
#define F_FORWTLS	10		/* remote machine via TLS */

char	*TypeNames[] = {
	"UNUSED",	"FILE",		"TTY",		"CONSOLE",
	"FORWUDP",	"USERS",	"WALL",		"MEMBUF",
	"PIPE",		"FORWTCP",	"FORWTLS",
};

SIMPLEQ_HEAD(filed_list, filed) Files;
struct	filed consfile;

int	Debug;			/* debug flag */
int	Foreground;		/* run in foreground, instead of daemonizing */
char	LocalHostName[HOST_NAME_MAX+1];	/* our hostname */
int	Started = 0;		/* set after privsep */
int	Initialized = 0;	/* set when we have initialized ourselves */

int	MarkInterval = 20 * 60;	/* interval between marks in seconds */
int	MarkSeq = 0;		/* mark sequence number */
int	PrivChild = 0;		/* Exec the privileged parent process */
int	Repeat = 0;		/* 0 msg repeated, 1 in files only, 2 never */
int	SecureMode = 1;		/* when true, speak only unix domain socks */
int	NoDNS = 0;		/* when true, refrain from doing DNS lookups */
int	ZuluTime = 0;		/* display date and time in UTC ISO format */
int	IncludeHostname = 0;	/* include RFC 3164 hostnames when forwarding */
int	Family = PF_UNSPEC;	/* protocol family, may disable IPv4 or IPv6 */

struct	tls *server_ctx;
struct	tls_config *client_config, *server_config;
const char *CAfile = "/etc/ssl/cert.pem"; /* file containing CA certificates */
int	NoVerify = 0;		/* do not verify TLS server x509 certificate */
const char *ClientCertfile = NULL;
const char *ClientKeyfile = NULL;
const char *ServerCAfile = NULL;
int	udpsend_dropped = 0;	/* messages dropped due to UDP not ready */
int	tcpbuf_dropped = 0;	/* count messages dropped from TCP or TLS */
int	file_dropped = 0;	/* messages dropped due to file system full */
int	init_dropped = 0;	/* messages dropped during initialization */

#define CTL_READING_CMD		1
#define CTL_WRITING_REPLY	2
#define CTL_WRITING_CONT_REPLY	3
int	ctl_state = 0;		/* What the control socket is up to */
int	membuf_drop = 0;	/* logs dropped in continuous membuf read */

/*
 * Client protocol NB. all numeric fields in network byte order
 */
#define CTL_VERSION		2

/* Request */
struct	{
	u_int32_t	version;
#define CMD_READ	1	/* Read out log */
#define CMD_READ_CLEAR	2	/* Read and clear log */
#define CMD_CLEAR	3	/* Clear log */
#define CMD_LIST	4	/* List available logs */
#define CMD_FLAGS	5	/* Query flags only */
#define CMD_READ_CONT	6	/* Read out log continuously */
	u_int32_t	cmd;
	u_int32_t	lines;
	char		logname[MAX_MEMBUF_NAME];
}	ctl_cmd;

size_t	ctl_cmd_bytes = 0;	/* number of bytes of ctl_cmd read */

/* Reply */
struct ctl_reply_hdr {
	u_int32_t	version;
#define CTL_HDR_FLAG_OVERFLOW	0x01
	u_int32_t	flags;
	/* Reply text follows, up to MAX_MEMBUF long */
};

#define CTL_HDR_LEN		(sizeof(struct ctl_reply_hdr))
#define CTL_REPLY_MAXSIZE	(CTL_HDR_LEN + MAX_MEMBUF)
#define CTL_REPLY_SIZE		(strlen(reply_text) + CTL_HDR_LEN)

char	*ctl_reply = NULL;	/* Buffer for control connection reply */
char	*reply_text;		/* Start of reply text in buffer */
size_t	ctl_reply_size = 0;	/* Number of bytes used in reply */
size_t	ctl_reply_offset = 0;	/* Number of bytes of reply written so far */

char	*linebuf;
int	 linesize;

int		 fd_ctlconn, fd_udp, fd_udp6, send_udp, send_udp6;
struct event	*ev_ctlaccept, *ev_ctlread, *ev_ctlwrite;

struct peer {
	struct buffertls	 p_buftls;
	struct bufferevent	*p_bufev;
	struct tls		*p_ctx;
	char			*p_peername;
	char			*p_hostname;
	int			 p_fd;
};
char hostname_unknown[] = "???";

void	 klog_readcb(int, short, void *);
void	 udp_readcb(int, short, void *);
void	 unix_readcb(int, short, void *);
int	 reserve_accept4(int, int, struct event *,
    void (*)(int, short, void *), struct sockaddr *, socklen_t *, int);
void	 tcp_acceptcb(int, short, void *);
void	 tls_acceptcb(int, short, void *);
void	 acceptcb(int, short, void *, int);
void	 tls_handshakecb(struct bufferevent *, void *);
int	 octet_counting(struct evbuffer *, char **, int);
int	 non_transparent_framing(struct evbuffer *, char **);
void	 tcp_readcb(struct bufferevent *, void *);
void	 tcp_closecb(struct bufferevent *, short, void *);
int	 tcp_socket(struct filed *);
void	 tcp_dropcb(struct bufferevent *, void *);
void	 tcp_writecb(struct bufferevent *, void *);
void	 tcp_errorcb(struct bufferevent *, short, void *);
void	 tcp_connectcb(int, short, void *);
int	 loghost_resolve(struct filed *);
void	 loghost_retry(struct filed *);
void	 udp_resolvecb(int, short, void *);
int	 tcpbuf_countmsg(struct bufferevent *bufev);
void	 die_signalcb(int, short, void *);
void	 mark_timercb(int, short, void *);
void	 init_signalcb(int, short, void *);
void	 ctlsock_acceptcb(int, short, void *);
void	 ctlconn_readcb(int, short, void *);
void	 ctlconn_writecb(int, short, void *);
void	 ctlconn_logto(char *);
void	 ctlconn_cleanup(void);

struct filed *cfline(char *, char *, char *);
void	cvthname(struct sockaddr *, char *, size_t);
int	decode(const char *, const CODE *);
void	markit(void);
void	fprintlog(struct filed *, int, char *);
void	dropped_warn(int *, const char *);
void	init(void);
void	logevent(int, const char *);
void	logmsg(struct msg *, int, char *);
struct filed *find_dup(struct filed *);
void	printline(char *, char *);
void	printsys(char *);
void	current_time(char *);
void	usage(void);
void	wallmsg(struct filed *, struct iovec *);
int	loghost_parse(char *, char **, char **, char **);
int	getmsgbufsize(void);
void	address_alloc(const char *, const char *, char ***, char ***, int *);
int	socket_bind(const char *, const char *, const char *, int,
    int *, int *);
int	unix_socket(char *, int, mode_t);
void	double_sockbuf(int, int, int);
void	set_sockbuf(int);
void	set_keepalive(int);
void	tailify_replytext(char *, int);

int
main(int argc, char *argv[])
{
	struct timeval	 to;
	struct event	*ev_klog, *ev_sendsys, *ev_udp, *ev_udp6,
			*ev_bind, *ev_listen, *ev_tls, *ev_unix,
			*ev_hup, *ev_int, *ev_quit, *ev_term, *ev_mark;
	sigset_t	 sigmask;
	const char	*errstr;
	char		*p;
	int		 ch, i;
	int		 lockpipe[2] = { -1, -1}, pair[2], nullfd, fd;
	int		 fd_ctlsock, fd_klog, fd_sendsys, *fd_bind, *fd_listen;
	int		*fd_tls, *fd_unix, nunix, nbind, nlisten, ntls;
	char		**path_unix, *path_ctlsock;
	char		**bind_host, **bind_port, **listen_host, **listen_port;
	char		*tls_hostport, **tls_host, **tls_port;

	/* block signal until handler is set up */
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGHUP);
	if (sigprocmask(SIG_SETMASK, &sigmask, NULL) == -1)
		err(1, "sigprocmask block");

	if ((path_unix = malloc(sizeof(*path_unix))) == NULL)
		err(1, "malloc %s", _PATH_LOG);
	path_unix[0] = _PATH_LOG;
	nunix = 1;
	path_ctlsock = NULL;

	bind_host = listen_host = tls_host = NULL;
	bind_port = listen_port = tls_port = NULL;
	tls_hostport = NULL;
	nbind = nlisten = ntls = 0;

	while ((ch = getopt(argc, argv,
	    "46a:C:c:dFf:hK:k:m:nP:p:rS:s:T:U:uVZ")) != -1) {
		switch (ch) {
		case '4':		/* disable IPv6 */
			Family = PF_INET;
			break;
		case '6':		/* disable IPv4 */
			Family = PF_INET6;
			break;
		case 'a':
			if ((path_unix = reallocarray(path_unix, nunix + 1,
			    sizeof(*path_unix))) == NULL)
				err(1, "unix path %s", optarg);
			path_unix[nunix++] = optarg;
			break;
		case 'C':		/* file containing CA certificates */
			CAfile = optarg;
			break;
		case 'c':		/* file containing client certificate */
			ClientCertfile = optarg;
			break;
		case 'd':		/* debug */
			Debug++;
			break;
		case 'F':		/* foreground */
			Foreground = 1;
			break;
		case 'f':		/* configuration file */
			ConfFile = optarg;
			break;
		case 'h':		/* RFC 3164 hostnames */
			IncludeHostname = 1;
			break;
		case 'K':		/* verify client with CA file */
			ServerCAfile = optarg;
			break;
		case 'k':		/* file containing client key */
			ClientKeyfile = optarg;
			break;
		case 'm':		/* mark interval */
			MarkInterval = strtonum(optarg, 0, 365*24*60, &errstr);
			if (errstr)
				errx(1, "mark_interval %s: %s", errstr, optarg);
			MarkInterval *= 60;
			break;
		case 'n':		/* don't do DNS lookups */
			NoDNS = 1;
			break;
		case 'P':		/* used internally, exec the parent */
			PrivChild = strtonum(optarg, 2, INT_MAX, &errstr);
			if (errstr)
				errx(1, "priv child %s: %s", errstr, optarg);
			break;
		case 'p':		/* path */
			path_unix[0] = optarg;
			break;
		case 'r':
			Repeat++;
			break;
		case 'S':		/* allow tls and listen on address */
			if (tls_hostport == NULL)
				tls_hostport = optarg;
			address_alloc("tls", optarg, &tls_host, &tls_port,
			    &ntls);
			break;
		case 's':
			path_ctlsock = optarg;
			break;
		case 'T':		/* allow tcp and listen on address */
			address_alloc("listen", optarg, &listen_host,
			    &listen_port, &nlisten);
			break;
		case 'U':		/* allow udp only from address */
			address_alloc("bind", optarg, &bind_host, &bind_port,
			    &nbind);
			break;
		case 'u':		/* allow udp input port */
			SecureMode = 0;
			break;
		case 'V':		/* do not verify certificates */
			NoVerify = 1;
			break;
		case 'Z':		/* time stamps in UTC ISO format */
			ZuluTime = 1;
			break;
		default:
			usage();
		}
	}
	if (argc != optind)
		usage();

	log_init(Debug, LOG_SYSLOG);
	log_procinit("syslogd");
	if (Debug)
		setvbuf(stdout, NULL, _IOLBF, 0);

	if ((nullfd = open(_PATH_DEVNULL, O_RDWR)) == -1)
		fatal("open %s", _PATH_DEVNULL);
	for (fd = nullfd + 1; fd <= STDERR_FILENO; fd++) {
		if (fcntl(fd, F_GETFL) == -1 && errno == EBADF)
			if (dup2(nullfd, fd) == -1)
				fatal("dup2 null");
	}

	if (PrivChild > 1)
		priv_exec(ConfFile, NoDNS, PrivChild, argc, argv);

	consfile.f_type = F_CONSOLE;
	(void)strlcpy(consfile.f_un.f_fname, ctty,
	    sizeof(consfile.f_un.f_fname));
	consfile.f_file = open(consfile.f_un.f_fname, O_WRONLY|O_NONBLOCK);
	if (consfile.f_file == -1)
		log_warn("open %s", consfile.f_un.f_fname);

	if (gethostname(LocalHostName, sizeof(LocalHostName)) == -1 ||
	    LocalHostName[0] == '\0')
		strlcpy(LocalHostName, "-", sizeof(LocalHostName));
	else if ((p = strchr(LocalHostName, '.')) != NULL)
		*p = '\0';

	/* Reserve space for kernel message buffer plus buffer full message. */
	linesize = getmsgbufsize() + 64;
	if (linesize < LOG_MAXLINE)
		linesize = LOG_MAXLINE;
	linesize++;
	if ((linebuf = malloc(linesize)) == NULL)
		fatal("allocate line buffer");

	if (socket_bind("udp", NULL, "syslog", SecureMode,
	    &fd_udp, &fd_udp6) == -1)
		log_warnx("socket bind * failed");
	if ((fd_bind = reallocarray(NULL, nbind, sizeof(*fd_bind))) == NULL)
		fatal("allocate bind fd");
	for (i = 0; i < nbind; i++) {
		if (socket_bind("udp", bind_host[i], bind_port[i], 0,
		    &fd_bind[i], &fd_bind[i]) == -1)
			log_warnx("socket bind udp failed");
	}
	if ((fd_listen = reallocarray(NULL, nlisten, sizeof(*fd_listen)))
	    == NULL)
		fatal("allocate listen fd");
	for (i = 0; i < nlisten; i++) {
		if (socket_bind("tcp", listen_host[i], listen_port[i], 0,
		    &fd_listen[i], &fd_listen[i]) == -1)
			log_warnx("socket listen tcp failed");
	}
	if ((fd_tls = reallocarray(NULL, ntls, sizeof(*fd_tls))) == NULL)
		fatal("allocate tls fd");
	for (i = 0; i < ntls; i++) {
		if (socket_bind("tls", tls_host[i], tls_port[i], 0,
		    &fd_tls[i], &fd_tls[i]) == -1)
			log_warnx("socket listen tls failed");
	}

	if ((fd_unix = reallocarray(NULL, nunix, sizeof(*fd_unix))) == NULL)
		fatal("allocate unix fd");
	for (i = 0; i < nunix; i++) {
		fd_unix[i] = unix_socket(path_unix[i], SOCK_DGRAM, 0666);
		if (fd_unix[i] == -1) {
			if (i == 0)
				log_warnx("log socket %s failed", path_unix[i]);
			continue;
		}
		double_sockbuf(fd_unix[i], SO_RCVBUF, 0);
	}

	if (socketpair(AF_UNIX, SOCK_DGRAM, PF_UNSPEC, pair) == -1) {
		log_warn("socketpair sendsyslog");
		fd_sendsys = -1;
	} else {
		/*
		 * Avoid to lose messages from sendsyslog(2).  A larger
		 * 1 MB socket buffer compensates bursts.
		 */
		double_sockbuf(pair[0], SO_RCVBUF, 1<<20);
		double_sockbuf(pair[1], SO_SNDBUF, 1<<20);
		fd_sendsys = pair[0];
	}

	fd_ctlsock = fd_ctlconn = -1;
	if (path_ctlsock != NULL) {
		fd_ctlsock = unix_socket(path_ctlsock, SOCK_STREAM, 0600);
		if (fd_ctlsock == -1) {
			log_warnx("control socket %s failed", path_ctlsock);
		} else {
			if (listen(fd_ctlsock, 5) == -1) {
				log_warn("listen control socket");
				close(fd_ctlsock);
				fd_ctlsock = -1;
			}
		}
	}

	if ((fd_klog = open(_PATH_KLOG, O_RDONLY)) == -1) {
		log_warn("open %s", _PATH_KLOG);
	} else if (fd_sendsys != -1) {
		/* Use /dev/klog to register sendsyslog(2) receiver. */
		if (ioctl(fd_klog, LIOCSFD, &pair[1]) == -1)
			log_warn("ioctl klog LIOCSFD sendsyslog");
	}
	if (fd_sendsys != -1)
		close(pair[1]);

	if ((client_config = tls_config_new()) == NULL)
		log_warn("tls_config_new client");
	if (tls_hostport) {
		if ((server_config = tls_config_new()) == NULL)
			log_warn("tls_config_new server");
		if ((server_ctx = tls_server()) == NULL) {
			log_warn("tls_server");
			for (i = 0; i < ntls; i++)
				close(fd_tls[i]);
			free(fd_tls);
			fd_tls = NULL;
			free(tls_host);
			free(tls_port);
			tls_host = tls_port = NULL;
			ntls = 0;
		}
	}

	if (client_config) {
		if (NoVerify) {
			tls_config_insecure_noverifycert(client_config);
			tls_config_insecure_noverifyname(client_config);
		} else {
			if (tls_config_set_ca_file(client_config,
			    CAfile) == -1) {
				log_warnx("load client TLS CA: %s",
				    tls_config_error(client_config));
				/* avoid reading default certs in chroot */
				tls_config_set_ca_mem(client_config, "", 0);
			} else
				log_debug("CAfile %s", CAfile);
		}
		if (ClientCertfile && ClientKeyfile) {
			if (tls_config_set_cert_file(client_config,
			    ClientCertfile) == -1)
				log_warnx("load client TLS cert: %s",
				    tls_config_error(client_config));
			else
				log_debug("ClientCertfile %s", ClientCertfile);

			if (tls_config_set_key_file(client_config,
			    ClientKeyfile) == -1)
				log_warnx("load client TLS key: %s",
				    tls_config_error(client_config));
			else
				log_debug("ClientKeyfile %s", ClientKeyfile);
		} else if (ClientCertfile || ClientKeyfile) {
			log_warnx("options -c and -k must be used together");
		}
		if (tls_config_set_protocols(client_config,
		    TLS_PROTOCOLS_ALL) != 0)
			log_warnx("set client TLS protocols: %s",
			    tls_config_error(client_config));
		if (tls_config_set_ciphers(client_config, "all") != 0)
			log_warnx("set client TLS ciphers: %s",
			    tls_config_error(client_config));
	}
	if (server_config && server_ctx) {
		const char *names[2];

		names[0] = tls_hostport;
		names[1] = tls_host[0];

		for (i = 0; i < 2; i++) {
			if (asprintf(&p, "/etc/ssl/private/%s.key", names[i])
			    == -1)
				continue;
			if (tls_config_set_key_file(server_config, p) == -1) {
				log_warnx("load server TLS key: %s",
				    tls_config_error(server_config));
				free(p);
				continue;
			}
			log_debug("Keyfile %s", p);
			free(p);
			if (asprintf(&p, "/etc/ssl/%s.crt", names[i]) == -1)
				continue;
			if (tls_config_set_cert_file(server_config, p) == -1) {
				log_warnx("load server TLS cert: %s",
				    tls_config_error(server_config));
				free(p);
				continue;
			}
			log_debug("Certfile %s", p);
			free(p);
			break;
		}

		if (ServerCAfile) {
			if (tls_config_set_ca_file(server_config,
			    ServerCAfile) == -1) {
				log_warnx("load server TLS CA: %s",
				    tls_config_error(server_config));
				/* avoid reading default certs in chroot */
				tls_config_set_ca_mem(server_config, "", 0);
			} else
				log_debug("Server CAfile %s", ServerCAfile);
			tls_config_verify_client(server_config);
		}
		if (tls_config_set_protocols(server_config,
		    TLS_PROTOCOLS_ALL) != 0)
			log_warnx("set server TLS protocols: %s",
			    tls_config_error(server_config));
		if (tls_config_set_ciphers(server_config, "compat") != 0)
			log_warnx("Set server TLS ciphers: %s",
			    tls_config_error(server_config));
		if (tls_configure(server_ctx, server_config) != 0) {
			log_warnx("tls_configure server: %s",
			    tls_error(server_ctx));
			tls_free(server_ctx);
			server_ctx = NULL;
			for (i = 0; i < ntls; i++)
				close(fd_tls[i]);
			free(fd_tls);
			fd_tls = NULL;
			free(tls_host);
			free(tls_port);
			tls_host = tls_port = NULL;
			ntls = 0;
		}
	}

	log_debug("off & running....");

	if (!Debug && !Foreground) {
		char c;

		pipe(lockpipe);

		switch(fork()) {
		case -1:
			err(1, "fork");
		case 0:
			setsid();
			close(lockpipe[0]);
			break;
		default:
			close(lockpipe[1]);
			read(lockpipe[0], &c, 1);
			_exit(0);
		}
	}

	/* tuck my process id away */
	if (!Debug) {
		FILE *fp;

		fp = fopen(_PATH_LOGPID, "w");
		if (fp != NULL) {
			fprintf(fp, "%ld\n", (long)getpid());
			(void) fclose(fp);
		}
	}

	/* Privilege separation begins here */
	priv_init(lockpipe[1], nullfd, argc, argv);

	if (pledge("stdio unix inet recvfd", NULL) == -1)
		err(1, "pledge");

	Started = 1;

	/* Process is now unprivileged and inside a chroot */
	if (Debug)
		event_set_log_callback(logevent);
	event_init();

	if ((ev_ctlaccept = malloc(sizeof(struct event))) == NULL ||
	    (ev_ctlread = malloc(sizeof(struct event))) == NULL ||
	    (ev_ctlwrite = malloc(sizeof(struct event))) == NULL ||
	    (ev_klog = malloc(sizeof(struct event))) == NULL ||
	    (ev_sendsys = malloc(sizeof(struct event))) == NULL ||
	    (ev_udp = malloc(sizeof(struct event))) == NULL ||
	    (ev_udp6 = malloc(sizeof(struct event))) == NULL ||
	    (ev_bind = reallocarray(NULL, nbind, sizeof(struct event)))
		== NULL ||
	    (ev_listen = reallocarray(NULL, nlisten, sizeof(struct event)))
		== NULL ||
	    (ev_tls = reallocarray(NULL, ntls, sizeof(struct event)))
		== NULL ||
	    (ev_unix = reallocarray(NULL, nunix, sizeof(struct event)))
		== NULL ||
	    (ev_hup = malloc(sizeof(struct event))) == NULL ||
	    (ev_int = malloc(sizeof(struct event))) == NULL ||
	    (ev_quit = malloc(sizeof(struct event))) == NULL ||
	    (ev_term = malloc(sizeof(struct event))) == NULL ||
	    (ev_mark = malloc(sizeof(struct event))) == NULL)
		err(1, "malloc");

	event_set(ev_ctlaccept, fd_ctlsock, EV_READ|EV_PERSIST,
	    ctlsock_acceptcb, ev_ctlaccept);
	event_set(ev_ctlread, fd_ctlconn, EV_READ|EV_PERSIST,
	    ctlconn_readcb, ev_ctlread);
	event_set(ev_ctlwrite, fd_ctlconn, EV_WRITE|EV_PERSIST,
	    ctlconn_writecb, ev_ctlwrite);
	event_set(ev_klog, fd_klog, EV_READ|EV_PERSIST, klog_readcb, ev_klog);
	event_set(ev_sendsys, fd_sendsys, EV_READ|EV_PERSIST, unix_readcb,
	    ev_sendsys);
	event_set(ev_udp, fd_udp, EV_READ|EV_PERSIST, udp_readcb, ev_udp);
	event_set(ev_udp6, fd_udp6, EV_READ|EV_PERSIST, udp_readcb, ev_udp6);
	for (i = 0; i < nbind; i++)
		event_set(&ev_bind[i], fd_bind[i], EV_READ|EV_PERSIST,
		    udp_readcb, &ev_bind[i]);
	for (i = 0; i < nlisten; i++)
		event_set(&ev_listen[i], fd_listen[i], EV_READ|EV_PERSIST,
		    tcp_acceptcb, &ev_listen[i]);
	for (i = 0; i < ntls; i++)
		event_set(&ev_tls[i], fd_tls[i], EV_READ|EV_PERSIST,
		    tls_acceptcb, &ev_tls[i]);
	for (i = 0; i < nunix; i++)
		event_set(&ev_unix[i], fd_unix[i], EV_READ|EV_PERSIST,
		    unix_readcb, &ev_unix[i]);

	signal_set(ev_hup, SIGHUP, init_signalcb, ev_hup);
	signal_set(ev_int, SIGINT, die_signalcb, ev_int);
	signal_set(ev_quit, SIGQUIT, die_signalcb, ev_quit);
	signal_set(ev_term, SIGTERM, die_signalcb, ev_term);

	evtimer_set(ev_mark, mark_timercb, ev_mark);

	init();

	/* Allocate ctl socket reply buffer if we have a ctl socket */
	if (fd_ctlsock != -1 &&
	    (ctl_reply = malloc(CTL_REPLY_MAXSIZE)) == NULL)
		fatal("allocate control socket reply buffer");
	reply_text = ctl_reply + CTL_HDR_LEN;

	if (!Debug) {
		close(lockpipe[1]);
		dup2(nullfd, STDIN_FILENO);
		dup2(nullfd, STDOUT_FILENO);
		dup2(nullfd, STDERR_FILENO);
	}
	if (nullfd > 2)
		close(nullfd);

	/*
	 * Signal to the priv process that the initial config parsing is done
	 * so that it will reject any future attempts to open more files
	 */
	priv_config_parse_done();

	if (fd_ctlsock != -1)
		event_add(ev_ctlaccept, NULL);
	if (fd_klog != -1)
		event_add(ev_klog, NULL);
	if (fd_sendsys != -1)
		event_add(ev_sendsys, NULL);
	if (!SecureMode) {
		if (fd_udp != -1)
			event_add(ev_udp, NULL);
		if (fd_udp6 != -1)
			event_add(ev_udp6, NULL);
	}
	for (i = 0; i < nbind; i++)
		if (fd_bind[i] != -1)
			event_add(&ev_bind[i], NULL);
	for (i = 0; i < nlisten; i++)
		if (fd_listen[i] != -1)
			event_add(&ev_listen[i], NULL);
	for (i = 0; i < ntls; i++)
		if (fd_tls[i] != -1)
			event_add(&ev_tls[i], NULL);
	for (i = 0; i < nunix; i++)
		if (fd_unix[i] != -1)
			event_add(&ev_unix[i], NULL);

	signal_add(ev_hup, NULL);
	signal_add(ev_term, NULL);
	if (Debug || Foreground) {
		signal_add(ev_int, NULL);
		signal_add(ev_quit, NULL);
	} else {
		(void)signal(SIGINT, SIG_IGN);
		(void)signal(SIGQUIT, SIG_IGN);
	}
	(void)signal(SIGCHLD, SIG_IGN);
	(void)signal(SIGPIPE, SIG_IGN);

	to.tv_sec = TIMERINTVL;
	to.tv_usec = 0;
	evtimer_add(ev_mark, &to);

	log_info(LOG_INFO, "start");
	log_debug("syslogd: started");

	sigemptyset(&sigmask);
	if (sigprocmask(SIG_SETMASK, &sigmask, NULL) == -1)
		err(1, "sigprocmask unblock");

	/* Send message via libc, flushes log stash in kernel. */
	openlog("syslogd", LOG_PID, LOG_SYSLOG);
	syslog(LOG_DEBUG, "running");

	event_dispatch();
	/* NOTREACHED */
	return (0);
}

void
address_alloc(const char *name, const char *address, char ***host,
    char ***port, int *num)
{
	char *p;

	/* do not care about memory leak, argv has to be preserved */
	if ((p = strdup(address)) == NULL)
		err(1, "%s address %s", name, address);
	if ((*host = reallocarray(*host, *num + 1, sizeof(**host))) == NULL)
		err(1, "%s host %s", name, address);
	if ((*port = reallocarray(*port, *num + 1, sizeof(**port))) == NULL)
		err(1, "%s port %s", name, address);
	if (loghost_parse(p, NULL, *host + *num, *port + *num) == -1)
		errx(1, "bad %s address: %s", name, address);
	(*num)++;
}

int
socket_bind(const char *proto, const char *host, const char *port,
    int shutread, int *fd, int *fd6)
{
	struct addrinfo	 hints, *res, *res0;
	char		 hostname[NI_MAXHOST], servname[NI_MAXSERV];
	int		*fdp, error, reuseaddr;

	*fd = *fd6 = -1;
	if (proto == NULL)
		proto = "udp";
	if (port == NULL)
		port = strcmp(proto, "tls") == 0 ? "syslog-tls" : "syslog";

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = Family;
	if (strcmp(proto, "udp") == 0) {
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
	} else {
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
	}
	hints.ai_flags = AI_PASSIVE;

	if ((error = getaddrinfo(host, port, &hints, &res0))) {
		log_warnx("getaddrinfo proto %s, host %s, port %s: %s",
		    proto, host ? host : "*", port, gai_strerror(error));
		return (-1);
	}

	for (res = res0; res; res = res->ai_next) {
		switch (res->ai_family) {
		case AF_INET:
			fdp = fd;
			break;
		case AF_INET6:
			fdp = fd6;
			break;
		default:
			continue;
		}
		if (*fdp >= 0)
			continue;

		if ((*fdp = socket(res->ai_family,
		    res->ai_socktype | SOCK_NONBLOCK, res->ai_protocol)) == -1)
			continue;

		error = getnameinfo(res->ai_addr, res->ai_addrlen, hostname,
		    sizeof(hostname), servname, sizeof(servname),
		    NI_NUMERICHOST | NI_NUMERICSERV |
		    (res->ai_socktype == SOCK_DGRAM ? NI_DGRAM : 0));
		if (error) {
			log_warnx("malformed bind address host \"%s\": %s",
			    host, gai_strerror(error));
			strlcpy(hostname, hostname_unknown, sizeof(hostname));
			strlcpy(servname, hostname_unknown, sizeof(servname));
		}
		if (shutread && shutdown(*fdp, SHUT_RD) == -1) {
			log_warn("shutdown SHUT_RD "
			    "protocol %d, address %s, portnum %s",
			    res->ai_protocol, hostname, servname);
			close(*fdp);
			*fdp = -1;
			continue;
		}
		if (!shutread && res->ai_protocol == IPPROTO_UDP)
			double_sockbuf(*fdp, SO_RCVBUF, 0);
		else if (res->ai_protocol == IPPROTO_TCP) {
			set_sockbuf(*fdp);
			set_keepalive(*fdp);
		}
		reuseaddr = 1;
		if (setsockopt(*fdp, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
		    sizeof(reuseaddr)) == -1) {
			log_warn("setsockopt SO_REUSEADDR "
			    "protocol %d, address %s, portnum %s",
			    res->ai_protocol, hostname, servname);
			close(*fdp);
			*fdp = -1;
			continue;
		}
		if (bind(*fdp, res->ai_addr, res->ai_addrlen) == -1) {
			log_warn("bind protocol %d, address %s, portnum %s",
			    res->ai_protocol, hostname, servname);
			close(*fdp);
			*fdp = -1;
			continue;
		}
		if (!shutread && res->ai_protocol == IPPROTO_TCP &&
		    listen(*fdp, 10) == -1) {
			log_warn("listen protocol %d, address %s, portnum %s",
			    res->ai_protocol, hostname, servname);
			close(*fdp);
			*fdp = -1;
			continue;
		}
	}

	freeaddrinfo(res0);

	if (*fd == -1 && *fd6 == -1)
		return (-1);
	return (0);
}

void
klog_readcb(int fd, short event, void *arg)
{
	struct event		*ev = arg;
	ssize_t			 n;

	n = read(fd, linebuf, linesize - 1);
	if (n > 0) {
		linebuf[n] = '\0';
		printsys(linebuf);
	} else if (n == -1 && errno != EINTR) {
		log_warn("read klog");
		event_del(ev);
	}
}

void
udp_readcb(int fd, short event, void *arg)
{
	struct sockaddr_storage	 sa;
	socklen_t		 salen;
	ssize_t			 n;

	salen = sizeof(sa);
	n = recvfrom(fd, linebuf, LOG_MAXLINE, 0, (struct sockaddr *)&sa,
	    &salen);
	if (n > 0) {
		char	 resolve[NI_MAXHOST];

		linebuf[n] = '\0';
		cvthname((struct sockaddr *)&sa, resolve, sizeof(resolve));
		log_debug("cvthname res: %s", resolve);
		printline(resolve, linebuf);
	} else if (n == -1 && errno != EINTR && errno != EWOULDBLOCK)
		log_warn("recvfrom udp");
}

void
unix_readcb(int fd, short event, void *arg)
{
	struct sockaddr_un	 sa;
	socklen_t		 salen;
	ssize_t			 n;

	salen = sizeof(sa);
	n = recvfrom(fd, linebuf, LOG_MAXLINE, 0, (struct sockaddr *)&sa,
	    &salen);
	if (n > 0) {
		linebuf[n] = '\0';
		printline(LocalHostName, linebuf);
	} else if (n == -1 && errno != EINTR && errno != EWOULDBLOCK)
		log_warn("recvfrom unix");
}

int
reserve_accept4(int lfd, int event, struct event *ev,
    void (*cb)(int, short, void *),
    struct sockaddr *sa, socklen_t *salen, int flags)
{
	struct timeval	 to = { 1, 0 };
	int		 afd;

	if (event & EV_TIMEOUT) {
		log_debug("Listen again");
		/* Enable the listen event, there is no timeout anymore. */
		event_set(ev, lfd, EV_READ|EV_PERSIST, cb, ev);
		event_add(ev, NULL);
		errno = EWOULDBLOCK;
		return (-1);
	}

	if (getdtablecount() + FD_RESERVE >= getdtablesize()) {
		afd = -1;
		errno = EMFILE;
	} else
		afd = accept4(lfd, sa, salen, flags);

	if (afd == -1 && (errno == ENFILE || errno == EMFILE)) {
		log_info(LOG_WARNING, "accept deferred: %s", strerror(errno));
		/*
		 * Disable the listen event and convert it to a timeout.
		 * Pass the listen file descriptor to the callback.
		 */
		event_del(ev);
		event_set(ev, lfd, 0, cb, ev);
		event_add(ev, &to);
		return (-1);
	}

	return (afd);
}

void
tcp_acceptcb(int lfd, short event, void *arg)
{
	acceptcb(lfd, event, arg, 0);
}

void
tls_acceptcb(int lfd, short event, void *arg)
{
	acceptcb(lfd, event, arg, 1);
}

void
acceptcb(int lfd, short event, void *arg, int usetls)
{
	struct event		*ev = arg;
	struct peer		*p;
	struct sockaddr_storage	 ss;
	socklen_t		 sslen;
	char			 hostname[NI_MAXHOST], servname[NI_MAXSERV];
	char			*peername;
	int			 fd, error;

	sslen = sizeof(ss);
	if ((fd = reserve_accept4(lfd, event, ev, tcp_acceptcb,
	    (struct sockaddr *)&ss, &sslen, SOCK_NONBLOCK)) == -1) {
		if (errno != ENFILE && errno != EMFILE &&
		    errno != EINTR && errno != EWOULDBLOCK &&
		    errno != ECONNABORTED)
			log_warn("accept tcp socket");
		return;
	}
	log_debug("Accepting tcp connection");

	error = getnameinfo((struct sockaddr *)&ss, sslen, hostname,
	    sizeof(hostname), servname, sizeof(servname),
	    NI_NUMERICHOST | NI_NUMERICSERV);
	if (error) {
		log_warnx("malformed TCP accept address: %s",
		    gai_strerror(error));
		peername = hostname_unknown;
	} else if (asprintf(&peername, ss.ss_family == AF_INET6 ?
	    "[%s]:%s" : "%s:%s", hostname, servname) == -1) {
		log_warn("allocate hostname \"%s\"", hostname);
		peername = hostname_unknown;
	}
	log_debug("Peer address and port %s", peername);
	if ((p = malloc(sizeof(*p))) == NULL) {
		log_warn("allocate peername \"%s\"", peername);
		if (peername != hostname_unknown)
			free(peername);
		close(fd);
		return;
	}
	p->p_fd = fd;
	p->p_ctx = NULL;
	p->p_peername = NULL;
	if ((p->p_bufev = bufferevent_new(fd,
	    usetls ? tls_handshakecb : tcp_readcb,
	    usetls ? tls_handshakecb : NULL, tcp_closecb, p)) == NULL) {
		log_warn("bufferevent \"%s\"", peername);
		free(p);
		if (peername != hostname_unknown)
			free(peername);
		close(fd);
		return;
	}
	if (usetls) {
		if (tls_accept_socket(server_ctx, &p->p_ctx, fd) == -1) {
			log_warnx("tls_accept_socket \"%s\": %s",
			    peername, tls_error(server_ctx));
			bufferevent_free(p->p_bufev);
			free(p);
			if (peername != hostname_unknown)
				free(peername);
			close(fd);
			return;
		}
		buffertls_set(&p->p_buftls, p->p_bufev, p->p_ctx, fd);
		buffertls_accept(&p->p_buftls, fd);
		log_debug("tcp accept callback: tls context success");
	}
	if (!NoDNS && peername != hostname_unknown &&
	    priv_getnameinfo((struct sockaddr *)&ss, ss.ss_len, hostname,
	    sizeof(hostname)) != 0) {
		log_debug("Host name for accept address (%s) unknown",
		    hostname);
	}
	if (peername == hostname_unknown ||
	    (p->p_hostname = strdup(hostname)) == NULL)
		p->p_hostname = hostname_unknown;
	log_debug("Peer hostname %s", hostname);
	p->p_peername = peername;
	bufferevent_enable(p->p_bufev, EV_READ);

	log_info(LOG_DEBUG, "%s logger \"%s\" accepted",
	    p->p_ctx ? "tls" : "tcp", peername);
}

void
tls_handshakecb(struct bufferevent *bufev, void *arg)
{
	struct peer *p = arg;
	const char *cn;
	char *cntmp;

	log_debug("Completed tls handshake");

	if (tls_peer_cert_provided(p->p_ctx)) {
		if ((cn = tls_peer_cert_common_name(p->p_ctx)) != NULL &&
		    strlen(cn) > 0) {
			if (stravis(&cntmp, cn, VIS_WHITE) == -1)
				log_warn("tls_handshakecb stravis");
			else {
				log_info(LOG_INFO, "%s using hostname \"%s\" "
				    "from certificate", p->p_hostname, cntmp);
				free(p->p_hostname);
				p->p_hostname = cntmp;
			}
		} else
			log_info(LOG_NOTICE,
			    "cannot get hostname from peer certificate");
	}

	bufferevent_setcb(bufev, tcp_readcb, NULL, tcp_closecb, p);
	tcp_readcb(bufev, arg);
}

/*
 * Syslog over TCP  RFC 6587  3.4.1. Octet Counting
 */
int
octet_counting(struct evbuffer *evbuf, char **msg, int drain)
{
	char	*p, *buf, *end;
	int	 len;

	buf = EVBUFFER_DATA(evbuf);
	end = buf + EVBUFFER_LENGTH(evbuf);
	/*
	 * It can be assumed that octet-counting framing is used if a syslog
	 * frame starts with a digit.
	 */
	if (buf >= end || !isdigit((unsigned char)*buf))
		return (-1);
	/*
	 * SYSLOG-FRAME = MSG-LEN SP SYSLOG-MSG
	 * MSG-LEN is the octet count of the SYSLOG-MSG in the SYSLOG-FRAME.
	 * We support up to 5 digits in MSG-LEN, so the maximum is 99999.
	 */
	for (p = buf; p < end && p < buf + 5; p++) {
		if (!isdigit((unsigned char)*p))
			break;
	}
	if (buf >= p || p >= end || *p != ' ')
		return (-1);
	p++;
	/* Using atoi() is safe as buf starts with 1 to 5 digits and a space. */
	len = atoi(buf);
	if (drain)
		log_debugadd(" octet counting %d", len);
	if (p + len > end)
		return (0);
	if (drain)
		evbuffer_drain(evbuf, p - buf);
	if (msg)
		*msg = p;
	return (len);
}

/*
 * Syslog over TCP  RFC 6587  3.4.2. Non-Transparent-Framing
 */
int
non_transparent_framing(struct evbuffer *evbuf, char **msg)
{
	char	*p, *buf, *end;

	buf = EVBUFFER_DATA(evbuf);
	end = buf + EVBUFFER_LENGTH(evbuf);
	/*
	 * The TRAILER has usually been a single character and most often
	 * is ASCII LF (%d10).  However, other characters have also been
	 * seen, with ASCII NUL (%d00) being a prominent example.
	 */
	for (p = buf; p < end; p++) {
		if (*p == '\0' || *p == '\n')
			break;
	}
	if (p + 1 - buf >= INT_MAX)
		return (-1);
	log_debugadd(" non transparent framing");
	if (p >= end)
		return (0);
	/*
	 * Some devices have also been seen to emit a two-character
	 * TRAILER, which is usually CR and LF.
	 */
	if (buf < p && p[0] == '\n' && p[-1] == '\r')
		p[-1] = '\0';
	if (msg)
		*msg = buf;
	return (p + 1 - buf);
}

void
tcp_readcb(struct bufferevent *bufev, void *arg)
{
	struct peer		*p = arg;
	char			*msg;
	int			 len;

	while (EVBUFFER_LENGTH(bufev->input) > 0) {
		log_debugadd("%s logger \"%s\"", p->p_ctx ? "tls" : "tcp",
		    p->p_peername);
		msg = NULL;
		len = octet_counting(bufev->input, &msg, 1);
		if (len < 0)
			len = non_transparent_framing(bufev->input, &msg);
		if (len < 0)
			log_debugadd("unknown method");
		if (msg == NULL) {
			log_debugadd(", incomplete frame");
			break;
		}
		log_debug(", use %d bytes", len);
		if (len > 0 && msg[len-1] == '\n')
			msg[len-1] = '\0';
		if (len == 0 || msg[len-1] != '\0') {
			memcpy(linebuf, msg, MINIMUM(len, LOG_MAXLINE));
			linebuf[MINIMUM(len, LOG_MAXLINE)] = '\0';
			msg = linebuf;
		}
		printline(p->p_hostname, msg);
		evbuffer_drain(bufev->input, len);
	}
	/* Maximum frame has 5 digits, 1 space, MAXLINE chars, 1 new line. */
	if (EVBUFFER_LENGTH(bufev->input) >= 5 + 1 + LOG_MAXLINE + 1) {
		log_debug(", use %zu bytes", EVBUFFER_LENGTH(bufev->input));
		EVBUFFER_DATA(bufev->input)[5 + 1 + LOG_MAXLINE] = '\0';
		printline(p->p_hostname, EVBUFFER_DATA(bufev->input));
		evbuffer_drain(bufev->input, -1);
	} else if (EVBUFFER_LENGTH(bufev->input) > 0)
		log_debug(", buffer %zu bytes", EVBUFFER_LENGTH(bufev->input));
}

void
tcp_closecb(struct bufferevent *bufev, short event, void *arg)
{
	struct peer		*p = arg;

	if (event & EVBUFFER_EOF) {
		log_info(LOG_DEBUG, "%s logger \"%s\" connection close",
		    p->p_ctx ? "tls" : "tcp", p->p_peername);
	} else {
		log_info(LOG_NOTICE, "%s logger \"%s\" connection error: %s",
		    p->p_ctx ? "tls" : "tcp", p->p_peername,
		    p->p_ctx ? tls_error(p->p_ctx) : strerror(errno));
	}

	if (p->p_peername != hostname_unknown)
		free(p->p_peername);
	if (p->p_hostname != hostname_unknown)
		free(p->p_hostname);
	bufferevent_free(p->p_bufev);
	if (p->p_ctx) {
		tls_close(p->p_ctx);
		tls_free(p->p_ctx);
	}
	close(p->p_fd);
	free(p);
}

int
tcp_socket(struct filed *f)
{
	int	 s;

	if ((s = socket(f->f_un.f_forw.f_addr.ss_family,
	    SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)) == -1) {
		log_warn("socket \"%s\"", f->f_un.f_forw.f_loghost);
		return (-1);
	}
	set_sockbuf(s);
	if (connect(s, (struct sockaddr *)&f->f_un.f_forw.f_addr,
	    f->f_un.f_forw.f_addr.ss_len) == -1 && errno != EINPROGRESS) {
		log_warn("connect \"%s\"", f->f_un.f_forw.f_loghost);
		close(s);
		return (-1);
	}
	return (s);
}

void
tcp_dropcb(struct bufferevent *bufev, void *arg)
{
	struct filed	*f = arg;

	/*
	 * Drop data received from the forward log server.
	 */
	log_debug("loghost \"%s\" did send %zu bytes back",
	    f->f_un.f_forw.f_loghost, EVBUFFER_LENGTH(bufev->input));
	evbuffer_drain(bufev->input, -1);
}

void
tcp_writecb(struct bufferevent *bufev, void *arg)
{
	struct filed	*f = arg;
	char		 ebuf[ERRBUFSIZE];

	/*
	 * Successful write, connection to server is good, reset wait time.
	 */
	log_debug("loghost \"%s\" successful write", f->f_un.f_forw.f_loghost);
	f->f_un.f_forw.f_retrywait = 0;

	if (f->f_dropped > 0 &&
	    EVBUFFER_LENGTH(f->f_un.f_forw.f_bufev->output) < MAX_TCPBUF) {
		snprintf(ebuf, sizeof(ebuf), "to loghost \"%s\"",
		    f->f_un.f_forw.f_loghost);
		dropped_warn(&f->f_dropped, ebuf);
	}
}

void
tcp_errorcb(struct bufferevent *bufev, short event, void *arg)
{
	struct filed	*f = arg;
	char		*p, *buf, *end;
	int		 l;
	char		 ebuf[ERRBUFSIZE];

	if (event & EVBUFFER_EOF)
		snprintf(ebuf, sizeof(ebuf), "loghost \"%s\" connection close",
		    f->f_un.f_forw.f_loghost);
	else
		snprintf(ebuf, sizeof(ebuf),
		    "loghost \"%s\" connection error: %s",
		    f->f_un.f_forw.f_loghost, f->f_un.f_forw.f_ctx ?
		    tls_error(f->f_un.f_forw.f_ctx) : strerror(errno));
	log_debug("%s", ebuf);

	/* The SIGHUP handler may also close the socket, so invalidate it. */
	if (f->f_un.f_forw.f_ctx) {
		tls_close(f->f_un.f_forw.f_ctx);
		tls_free(f->f_un.f_forw.f_ctx);
		f->f_un.f_forw.f_ctx = NULL;
	}
	bufferevent_disable(bufev, EV_READ|EV_WRITE);
	close(f->f_file);
	f->f_file = -1;

	/*
	 * The messages in the output buffer may be out of sync.
	 * Check that the buffer starts with "1234 <1234 octets>\n".
	 * Otherwise remove the partial message from the beginning.
	 */
	buf = EVBUFFER_DATA(bufev->output);
	end = buf + EVBUFFER_LENGTH(bufev->output);
	if (buf < end && !((l = octet_counting(bufev->output, &p, 0)) > 0 &&
	    p[l-1] == '\n')) {
		for (p = buf; p < end; p++) {
			if (*p == '\n') {
				evbuffer_drain(bufev->output, p - buf + 1);
				break;
			}
		}
		/* Without '\n' discard everything. */
		if (p == end)
			evbuffer_drain(bufev->output, -1);
		log_debug("loghost \"%s\" dropped partial message",
		    f->f_un.f_forw.f_loghost);
		f->f_dropped++;
	}

	loghost_retry(f);

	/* Log the connection error to the fresh buffer after reconnecting. */
	log_info(LOG_WARNING, "%s", ebuf);
}

void
tcp_connectcb(int fd, short event, void *arg)
{
	struct filed		*f = arg;
	struct bufferevent	*bufev = f->f_un.f_forw.f_bufev;
	int			 s;

	if (f->f_un.f_forw.f_addr.ss_family == AF_UNSPEC) {
		if (loghost_resolve(f) != 0) {
			loghost_retry(f);
			return;
		}
	}

	if ((s = tcp_socket(f)) == -1) {
		loghost_retry(f);
		return;
	}
	log_debug("tcp connect callback: socket success, event %#x", event);
	f->f_file = s;

	bufferevent_setfd(bufev, s);
	bufferevent_setcb(bufev, tcp_dropcb, tcp_writecb, tcp_errorcb, f);
	/*
	 * Although syslog is a write only protocol, enable reading from
	 * the socket to detect connection close and errors.
	 */
	bufferevent_enable(bufev, EV_READ|EV_WRITE);

	if (f->f_type == F_FORWTLS) {
		if ((f->f_un.f_forw.f_ctx = tls_client()) == NULL) {
			log_warn("tls_client \"%s\"", f->f_un.f_forw.f_loghost);
			goto error;
		}
		if (client_config &&
		    tls_configure(f->f_un.f_forw.f_ctx, client_config) == -1) {
			log_warnx("tls_configure \"%s\": %s",
			    f->f_un.f_forw.f_loghost,
			    tls_error(f->f_un.f_forw.f_ctx));
			goto error;
		}
		if (tls_connect_socket(f->f_un.f_forw.f_ctx, s,
		    f->f_un.f_forw.f_host) == -1) {
			log_warnx("tls_connect_socket \"%s\": %s",
			    f->f_un.f_forw.f_loghost,
			    tls_error(f->f_un.f_forw.f_ctx));
			goto error;
		}
		log_debug("tcp connect callback: tls context success");

		buffertls_set(&f->f_un.f_forw.f_buftls, bufev,
		    f->f_un.f_forw.f_ctx, s);
		buffertls_connect(&f->f_un.f_forw.f_buftls, s);
	}

	return;

 error:
	if (f->f_un.f_forw.f_ctx) {
		tls_free(f->f_un.f_forw.f_ctx);
		f->f_un.f_forw.f_ctx = NULL;
	}
	bufferevent_disable(bufev, EV_READ|EV_WRITE);
	close(f->f_file);
	f->f_file = -1;
	loghost_retry(f);
}

int
loghost_resolve(struct filed *f)
{
	char	hostname[NI_MAXHOST];
	int	error;

	error = priv_getaddrinfo(f->f_un.f_forw.f_ipproto,
	    f->f_un.f_forw.f_host, f->f_un.f_forw.f_port,
	    (struct sockaddr *)&f->f_un.f_forw.f_addr,
	    sizeof(f->f_un.f_forw.f_addr));
	if (error) {
		log_warnx("bad hostname \"%s\"", f->f_un.f_forw.f_loghost);
		f->f_un.f_forw.f_addr.ss_family = AF_UNSPEC;
		return (error);
	}

	error = getnameinfo((struct sockaddr *)&f->f_un.f_forw.f_addr,
	    f->f_un.f_forw.f_addr.ss_len, hostname, sizeof(hostname), NULL, 0,
	    NI_NUMERICHOST | NI_NUMERICSERV |
	    (strncmp(f->f_un.f_forw.f_ipproto, "udp", 3) == 0 ? NI_DGRAM : 0));
	if (error) {
		log_warnx("malformed UDP address loghost \"%s\": %s",
		    f->f_un.f_forw.f_loghost, gai_strerror(error));
		strlcpy(hostname, hostname_unknown, sizeof(hostname));
	}

	log_debug("resolved loghost \"%s\" address %s",
	    f->f_un.f_forw.f_loghost, hostname);
	return (0);
}

void
loghost_retry(struct filed *f)
{
	struct timeval		 to;

	if (f->f_un.f_forw.f_retrywait == 0)
		f->f_un.f_forw.f_retrywait = 1;
	else
		f->f_un.f_forw.f_retrywait <<= 1;
	if (f->f_un.f_forw.f_retrywait > 600)
		f->f_un.f_forw.f_retrywait = 600;
	to.tv_sec = f->f_un.f_forw.f_retrywait;
	to.tv_usec = 0;
	evtimer_add(&f->f_un.f_forw.f_ev, &to);

	log_debug("retry loghost \"%s\" wait %d",
	    f->f_un.f_forw.f_loghost, f->f_un.f_forw.f_retrywait);
}

void
udp_resolvecb(int fd, short event, void *arg)
{
	struct filed		*f = arg;

	if (loghost_resolve(f) != 0) {
		loghost_retry(f);
		return;
	}

	switch (f->f_un.f_forw.f_addr.ss_family) {
	case AF_INET:
		f->f_file = fd_udp;
		break;
	case AF_INET6:
		f->f_file = fd_udp6;
		break;
	}
	f->f_un.f_forw.f_retrywait = 0;

	if (f->f_dropped > 0) {
		char ebuf[ERRBUFSIZE];

		snprintf(ebuf, sizeof(ebuf), "to udp loghost \"%s\"",
		    f->f_un.f_forw.f_loghost);
		dropped_warn(&f->f_dropped, ebuf);
	}
}

int
tcpbuf_countmsg(struct bufferevent *bufev)
{
	char	*p, *buf, *end;
	int	 i = 0;

	buf = EVBUFFER_DATA(bufev->output);
	end = buf + EVBUFFER_LENGTH(bufev->output);
	for (p = buf; p < end; p++) {
		if (*p == '\n')
			i++;
	}
	return (i);
}

void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: syslogd [-46dFhnruVZ] [-a path] [-C CAfile]\n"
	    "\t[-c cert_file] [-f config_file] [-K CAfile] [-k key_file]\n"
	    "\t[-m mark_interval] [-p log_socket] [-S listen_address]\n"
	    "\t[-s reporting_socket] [-T listen_address] [-U bind_address]\n");
	exit(1);
}

/*
 * Take a raw input line, decode the message, and print the message
 * on the appropriate log files.
 */
void
printline(char *hname, char *msgstr)
{
	struct msg msg;
	char *p, *q, line[LOG_MAXLINE + 4 + 1];  /* message, encoding, NUL */

	p = msgstr;
	for (q = line; *p && q < &line[LOG_MAXLINE]; p++) {
		if (*p == '\n')
			*q++ = ' ';
		else
			q = vis(q, *p, VIS_NOSLASH, 0);
	}
	line[LOG_MAXLINE] = *q = '\0';

	parsemsg(line, &msg);
	if (msg.m_pri == -1)
		msg.m_pri = DEFUPRI;
	/*
	 * Don't allow users to log kernel messages.
	 * NOTE: since LOG_KERN == 0 this will also match
	 * messages with no facility specified.
	 */
	if (LOG_FAC(msg.m_pri) == LOG_KERN)
		msg.m_pri = LOG_USER | LOG_PRI(msg.m_pri);

	if (msg.m_timestamp[0] == '\0')
		current_time(msg.m_timestamp);

	logmsg(&msg, 0, hname);
}

/*
 * Take a raw input line from /dev/klog, split and format similar to syslog().
 */
void
printsys(char *msgstr)
{
	struct msg msg;
	int c, flags;
	char *lp, *p, *q;
	size_t prilen;
	int l;

	current_time(msg.m_timestamp);
	strlcpy(msg.m_prog, _PATH_UNIX, sizeof(msg.m_prog));
	l = snprintf(msg.m_msg, sizeof(msg.m_msg), "%s: ", _PATH_UNIX);
	if (l < 0 || l >= sizeof(msg.m_msg)) {
		msg.m_msg[0] = '\0';
		l = 0;
	}
	lp = msg.m_msg + l;
	for (p = msgstr; *p != '\0'; ) {
		flags = SYNC_FILE;	/* fsync file after write */
		msg.m_pri = DEFSPRI;
		prilen = parsemsg_priority(p, &msg.m_pri);
		p += prilen;
		if (prilen == 0) {
			/* kernel printf's come out on console */
			flags |= IGN_CONS;
		}
		if (msg.m_pri &~ (LOG_FACMASK|LOG_PRIMASK))
			msg.m_pri = DEFSPRI;

		q = lp;
		while (*p && (c = *p++) != '\n' &&
		    q < &msg.m_msg[sizeof(msg.m_msg) - 4])
			q = vis(q, c, VIS_NOSLASH, 0);

		logmsg(&msg, flags, LocalHostName);
	}
}

void
vlogmsg(int pri, const char *prog, const char *fmt, va_list ap)
{
	struct msg msg;
	int	l;

	msg.m_pri = pri;
	current_time(msg.m_timestamp);
	strlcpy(msg.m_prog, prog, sizeof(msg.m_prog));
	l = snprintf(msg.m_msg, sizeof(msg.m_msg), "%s[%d]: ", prog, getpid());
	if (l < 0 || l >= sizeof(msg.m_msg))
		l = 0;
	l = vsnprintf(msg.m_msg + l, sizeof(msg.m_msg) - l, fmt, ap);
	if (l < 0)
		strlcpy(msg.m_msg, fmt, sizeof(msg.m_msg));

	if (!Started) {
		fprintf(stderr, "%s\n", msg.m_msg);
		init_dropped++;
		return;
	}
	logmsg(&msg, 0, LocalHostName);
}

struct timeval	now;

void
current_time(char *timestamp)
{
	(void)gettimeofday(&now, NULL);

	if (ZuluTime) {
		struct tm *tm;
		size_t l;

		tm = gmtime(&now.tv_sec);
		l = strftime(timestamp, 33, "%FT%T", tm);
		/*
		 * Use only millisecond precision as some time has
		 * passed since syslog(3) was called.
		 */
		snprintf(timestamp + l, 33 - l, ".%03ldZ", now.tv_usec / 1000);
	} else
		strlcpy(timestamp, ctime(&now.tv_sec) + 4, 16);
}

/*
 * Log a message to the appropriate log files, users, etc. based on
 * the priority.
 */
void
logmsg(struct msg *msg, int flags, char *from)
{
	struct filed *f;
	int fac, msglen, prilev;

	(void)gettimeofday(&now, NULL);
	log_debug("logmsg: pri 0%o, flags 0x%x, from %s, prog %s, msg %s",
	    msg->m_pri, flags, from, msg->m_prog, msg->m_msg);

	/* extract facility and priority level */
	if (flags & MARK)
		fac = LOG_NFACILITIES;
	else
		fac = LOG_FAC(msg->m_pri);
	prilev = LOG_PRI(msg->m_pri);

	/* log the message to the particular outputs */
	if (!Initialized) {
		f = &consfile;
		if (f->f_type == F_CONSOLE) {
			strlcpy(f->f_lasttime, msg->m_timestamp,
			    sizeof(f->f_lasttime));
			strlcpy(f->f_prevhost, from,
			    sizeof(f->f_prevhost));
			fprintlog(f, flags, msg->m_msg);
			/* May be set to F_UNUSED, try again next time. */
			f->f_type = F_CONSOLE;
		}
		init_dropped++;
		return;
	}
	/* log the message to the particular outputs */
	msglen = strlen(msg->m_msg);
	SIMPLEQ_FOREACH(f, &Files, f_next) {
		/* skip messages that are incorrect priority */
		if (f->f_pmask[fac] < prilev ||
		    f->f_pmask[fac] == INTERNAL_NOPRI)
			continue;

		/* skip messages with the incorrect program or hostname */
		if (f->f_program && fnmatch(f->f_program, msg->m_prog, 0) != 0)
			continue;
		if (f->f_hostname && fnmatch(f->f_hostname, from, 0) != 0)
			continue;

		if (f->f_type == F_CONSOLE && (flags & IGN_CONS))
			continue;

		/* don't output marks to recently written files */
		if ((flags & MARK) &&
		    (now.tv_sec - f->f_time) < MarkInterval / 2)
			continue;

		/*
		 * suppress duplicate lines to this file
		 */
		if ((Repeat == 0 || (Repeat == 1 &&
		    (f->f_type != F_PIPE && f->f_type != F_FORWUDP &&
		    f->f_type != F_FORWTCP && f->f_type != F_FORWTLS))) &&
		    (flags & MARK) == 0 && msglen == f->f_prevlen &&
		    f->f_dropped == 0 &&
		    !strcmp(msg->m_msg, f->f_prevline) &&
		    !strcmp(from, f->f_prevhost)) {
			strlcpy(f->f_lasttime, msg->m_timestamp,
			    sizeof(f->f_lasttime));
			f->f_prevcount++;
			log_debug("msg repeated %d times, %ld sec of %d",
			    f->f_prevcount, (long)(now.tv_sec - f->f_time),
			    repeatinterval[f->f_repeatcount]);
			/*
			 * If domark would have logged this by now,
			 * flush it now (so we don't hold isolated messages),
			 * but back off so we'll flush less often
			 * in the future.
			 */
			if (now.tv_sec > REPEATTIME(f)) {
				fprintlog(f, flags, (char *)NULL);
				BACKOFF(f);
			}
		} else {
			/* new line, save it */
			if (f->f_prevcount)
				fprintlog(f, 0, (char *)NULL);
			f->f_repeatcount = 0;
			f->f_prevpri = msg->m_pri;
			strlcpy(f->f_lasttime, msg->m_timestamp,
			    sizeof(f->f_lasttime));
			strlcpy(f->f_prevhost, from,
			    sizeof(f->f_prevhost));
			if (msglen < MAXSVLINE) {
				f->f_prevlen = msglen;
				strlcpy(f->f_prevline, msg->m_msg,
				    sizeof(f->f_prevline));
				fprintlog(f, flags, (char *)NULL);
			} else {
				f->f_prevline[0] = 0;
				f->f_prevlen = 0;
				fprintlog(f, flags, msg->m_msg);
			}
		}

		if (f->f_quick)
			break;
	}
}

void
fprintlog(struct filed *f, int flags, char *msg)
{
	struct iovec iov[IOVCNT], *v;
	struct msghdr msghdr;
	int l, retryonce;
	char line[LOG_MAXLINE + 1], pribuf[13], greetings[500], repbuf[80];
	char ebuf[ERRBUFSIZE];

	v = iov;
	switch (f->f_type) {
	case F_FORWUDP:
	case F_FORWTCP:
	case F_FORWTLS:
		l = snprintf(pribuf, sizeof(pribuf), "<%d>", f->f_prevpri);
		if (l < 0)
			l = strlcpy(pribuf, "<13>", sizeof(pribuf));
		if (l >= sizeof(pribuf))
			l = sizeof(pribuf) - 1;
		v->iov_base = pribuf;
		v->iov_len = l;
		break;
	case F_WALL:
		l = snprintf(greetings, sizeof(greetings),
		    "\r\n\7Message from syslogd@%s at %.24s ...\r\n",
		    f->f_prevhost, ctime(&now.tv_sec));
		if (l < 0)
			l = strlcpy(greetings,
			    "\r\n\7Message from syslogd ...\r\n",
			    sizeof(greetings));
		if (l >= sizeof(greetings))
			l = sizeof(greetings) - 1;
		v->iov_base = greetings;
		v->iov_len = l;
		break;
	default:
		v->iov_base = "";
		v->iov_len = 0;
		break;
	}
	v++;

	if (f->f_lasttime[0] != '\0') {
		v->iov_base = f->f_lasttime;
		v->iov_len = strlen(f->f_lasttime);
		v++;
		v->iov_base = " ";
		v->iov_len = 1;
	} else {
		v->iov_base = "";
		v->iov_len = 0;
		v++;
		v->iov_base = "";
		v->iov_len = 0;
	}
	v++;

	switch (f->f_type) {
	case F_FORWUDP:
	case F_FORWTCP:
	case F_FORWTLS:
		if (IncludeHostname) {
			v->iov_base = LocalHostName;
			v->iov_len = strlen(LocalHostName);
			v++;
			v->iov_base = " ";
			v->iov_len = 1;
		} else {
			/* XXX RFC requires to include host name */
			v->iov_base = "";
			v->iov_len = 0;
			v++;
			v->iov_base = "";
			v->iov_len = 0;
		}
		break;
	default:
		if (f->f_prevhost[0] != '\0') {
			v->iov_base = f->f_prevhost;
			v->iov_len = strlen(v->iov_base);
			v++;
			v->iov_base = " ";
			v->iov_len = 1;
		} else {
			v->iov_base = "";
			v->iov_len = 0;
			v++;
			v->iov_base = "";
			v->iov_len = 0;
		}
		break;
	}
	v++;

	if (msg) {
		v->iov_base = msg;
		v->iov_len = strlen(msg);
	} else if (f->f_prevcount > 1) {
		l = snprintf(repbuf, sizeof(repbuf),
		    "last message repeated %d times", f->f_prevcount);
		if (l < 0)
			l = strlcpy(repbuf, "last message repeated",
			    sizeof(repbuf));
		if (l >= sizeof(repbuf))
			l = sizeof(repbuf) - 1;
		v->iov_base = repbuf;
		v->iov_len = l;
	} else {
		v->iov_base = f->f_prevline;
		v->iov_len = f->f_prevlen;
	}
	v++;

	switch (f->f_type) {
	case F_CONSOLE:
	case F_TTY:
	case F_USERS:
	case F_WALL:
		v->iov_base = "\r\n";
		v->iov_len = 2;
		break;
	case F_FILE:
	case F_PIPE:
	case F_FORWTCP:
	case F_FORWTLS:
		v->iov_base = "\n";
		v->iov_len = 1;
		break;
	default:
		v->iov_base = "";
		v->iov_len = 0;
		break;
	}
	v = NULL;

	log_debugadd("Logging to %s", TypeNames[f->f_type]);
	f->f_time = now.tv_sec;

	switch (f->f_type) {
	case F_UNUSED:
		log_debug("");
		break;

	case F_FORWUDP:
		log_debugadd(" %s", f->f_un.f_forw.f_loghost);
		if (f->f_un.f_forw.f_addr.ss_family == AF_UNSPEC) {
			log_debug(" (dropped not resolved)");
			f->f_dropped++;
			break;
		}
		l = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len +
		    iov[3].iov_len + iov[4].iov_len + iov[5].iov_len +
		    iov[6].iov_len;
		if (l > MAX_UDPMSG) {
			l -= MAX_UDPMSG;
			if (iov[5].iov_len > l)
				iov[5].iov_len -= l;
			else
				iov[5].iov_len = 0;
		}
		memset(&msghdr, 0, sizeof(msghdr));
		msghdr.msg_name = &f->f_un.f_forw.f_addr;
		msghdr.msg_namelen = f->f_un.f_forw.f_addr.ss_len;
		msghdr.msg_iov = iov;
		msghdr.msg_iovlen = IOVCNT;
		if (sendmsg(f->f_file, &msghdr, 0) == -1) {
			switch (errno) {
			case EACCES:
			case EADDRNOTAVAIL:
			case EHOSTDOWN:
			case EHOSTUNREACH:
			case ENETDOWN:
			case ENETUNREACH:
			case ENOBUFS:
			case EWOULDBLOCK:
				log_debug(" (dropped send error)");
				f->f_dropped++;
				/* silently dropped */
				break;
			default:
				log_debug(" (dropped permanent send error)");
				f->f_dropped++;
				f->f_type = F_UNUSED;
				snprintf(ebuf, sizeof(ebuf),
				    "to udp loghost \"%s\"",
				    f->f_un.f_forw.f_loghost);
				dropped_warn(&f->f_dropped, ebuf);
				log_warn("loghost \"%s\" disabled, sendmsg",
				    f->f_un.f_forw.f_loghost);
				break;
			}
		} else {
			log_debug("");
			if (f->f_dropped > 0) {
				snprintf(ebuf, sizeof(ebuf),
				    "to udp loghost \"%s\"",
				    f->f_un.f_forw.f_loghost);
				dropped_warn(&f->f_dropped, ebuf);
			}
		}
		break;

	case F_FORWTCP:
	case F_FORWTLS:
		log_debugadd(" %s", f->f_un.f_forw.f_loghost);
		if (EVBUFFER_LENGTH(f->f_un.f_forw.f_bufev->output) >=
		    MAX_TCPBUF) {
			log_debug(" (dropped tcpbuf full)");
			f->f_dropped++;
			break;
		}
		/*
		 * Syslog over TLS  RFC 5425  4.3.  Sending Data
		 * Syslog over TCP  RFC 6587  3.4.1.  Octet Counting
		 * Use an additional '\n' to split messages.  This allows
		 * buffer synchronisation, helps legacy implementations,
		 * and makes line based testing easier.
		 */
		l = evbuffer_add_printf(f->f_un.f_forw.f_bufev->output,
		    "%zu %s%s%s%s%s%s%s", iov[0].iov_len +
		    iov[1].iov_len + iov[2].iov_len +
		    iov[3].iov_len + iov[4].iov_len +
		    iov[5].iov_len + iov[6].iov_len,
		    (char *)iov[0].iov_base,
		    (char *)iov[1].iov_base, (char *)iov[2].iov_base,
		    (char *)iov[3].iov_base, (char *)iov[4].iov_base,
		    (char *)iov[5].iov_base, (char *)iov[6].iov_base);
		if (l < 0) {
			log_debug(" (dropped evbuffer add)");
			f->f_dropped++;
			break;
		}
		bufferevent_enable(f->f_un.f_forw.f_bufev, EV_WRITE);
		log_debug("");
		break;

	case F_CONSOLE:
		if (flags & IGN_CONS) {
			log_debug(" (ignored)");
			break;
		}
		/* FALLTHROUGH */
	case F_TTY:
	case F_FILE:
	case F_PIPE:
		log_debug(" %s", f->f_un.f_fname);
		retryonce = 0;
	again:
		if (writev(f->f_file, iov, IOVCNT) == -1) {
			int e = errno;

			/* allow to recover from file system full */
			if (e == ENOSPC && f->f_type == F_FILE) {
				if (f->f_dropped++ == 0) {
					f->f_type = F_UNUSED;
					errno = e;
					log_warn("write to file \"%s\"",
					    f->f_un.f_fname);
					f->f_type = F_FILE;
				}
				break;
			}

			/* pipe is non-blocking. log and drop message if full */
			if (e == EAGAIN && f->f_type == F_PIPE) {
				if (now.tv_sec - f->f_lasterrtime > 120) {
					f->f_lasterrtime = now.tv_sec;
					log_warn("write to pipe \"%s\"",
					    f->f_un.f_fname);
				}
				break;
			}

			/*
			 * Check for errors on TTY's or program pipes.
			 * Errors happen due to loss of tty or died programs.
			 */
			if (e == EAGAIN) {
				/*
				 * Silently drop messages on blocked write.
				 * This can happen when logging to a locked tty.
				 */
				break;
			}

			(void)close(f->f_file);
			if ((e == EIO || e == EBADF) &&
			    f->f_type != F_FILE && f->f_type != F_PIPE &&
			    !retryonce) {
				f->f_file = priv_open_tty(f->f_un.f_fname);
				retryonce = 1;
				if (f->f_file < 0) {
					f->f_type = F_UNUSED;
					log_warn("priv_open_tty \"%s\"",
					    f->f_un.f_fname);
				} else
					goto again;
			} else if ((e == EPIPE || e == EBADF) &&
			    f->f_type == F_PIPE && !retryonce) {
				f->f_file = priv_open_log(f->f_un.f_fname);
				retryonce = 1;
				if (f->f_file < 0) {
					f->f_type = F_UNUSED;
					log_warn("priv_open_log \"%s\"",
					    f->f_un.f_fname);
				} else
					goto again;
			} else {
				f->f_type = F_UNUSED;
				f->f_file = -1;
				errno = e;
				log_warn("writev \"%s\"", f->f_un.f_fname);
			}
		} else {
			if (flags & SYNC_FILE)
				(void)fsync(f->f_file);
			if (f->f_dropped > 0 && f->f_type == F_FILE) {
				snprintf(ebuf, sizeof(ebuf), "to file \"%s\"",
				    f->f_un.f_fname);
				dropped_warn(&f->f_dropped, ebuf);
			}
		}
		break;

	case F_USERS:
	case F_WALL:
		log_debug("");
		wallmsg(f, iov);
		break;

	case F_MEMBUF:
		log_debug("");
		l = snprintf(line, sizeof(line),
		    "%s%s%s%s%s%s%s", (char *)iov[0].iov_base,
		    (char *)iov[1].iov_base, (char *)iov[2].iov_base,
		    (char *)iov[3].iov_base, (char *)iov[4].iov_base,
		    (char *)iov[5].iov_base, (char *)iov[6].iov_base);
		if (l < 0)
			l = strlcpy(line, iov[5].iov_base, sizeof(line));
		if (ringbuf_append_line(f->f_un.f_mb.f_rb, line) == 1)
			f->f_un.f_mb.f_overflow = 1;
		if (f->f_un.f_mb.f_attached)
			ctlconn_logto(line);
		break;
	}
	f->f_prevcount = 0;
}

/*
 *  WALLMSG -- Write a message to the world at large
 *
 *	Write the specified message to either the entire
 *	world, or a list of approved users.
 */
void
wallmsg(struct filed *f, struct iovec *iov)
{
	struct utmp ut;
	char utline[sizeof(ut.ut_line) + 1];
	static int reenter;			/* avoid calling ourselves */
	FILE *uf;
	int i;

	if (reenter++)
		return;
	if ((uf = priv_open_utmp()) == NULL) {
		log_warn("priv_open_utmp");
		reenter = 0;
		return;
	}
	while (fread(&ut, sizeof(ut), 1, uf) == 1) {
		if (ut.ut_name[0] == '\0')
			continue;
		/* must use strncpy since ut_* may not be NUL terminated */
		strncpy(utline, ut.ut_line, sizeof(utline) - 1);
		utline[sizeof(utline) - 1] = '\0';
		if (f->f_type == F_WALL) {
			ttymsg(utline, iov);
			continue;
		}
		/* should we send the message to this user? */
		for (i = 0; i < MAXUNAMES; i++) {
			if (!f->f_un.f_uname[i][0])
				break;
			if (!strncmp(f->f_un.f_uname[i], ut.ut_name,
			    UT_NAMESIZE)) {
				ttymsg(utline, iov);
				break;
			}
		}
	}
	(void)fclose(uf);
	reenter = 0;
}

/*
 * Return a printable representation of a host address.
 */
void
cvthname(struct sockaddr *f, char *result, size_t res_len)
{
	int error;

	error = getnameinfo(f, f->sa_len, result, res_len, NULL, 0,
	    NI_NUMERICHOST | NI_NUMERICSERV | NI_DGRAM);
	if (error) {
		log_warnx("malformed UDP from address: %s",
		    gai_strerror(error));
		strlcpy(result, hostname_unknown, res_len);
		return;
	}
	log_debug("cvthname(%s)", result);
	if (NoDNS)
		return;

	if (priv_getnameinfo(f, f->sa_len, result, res_len) != 0)
		log_debug("Host name for from address (%s) unknown", result);
}

void
die_signalcb(int signum, short event, void *arg)
{
	die(signum);
}

void
mark_timercb(int unused, short event, void *arg)
{
	struct event		*ev = arg;
	struct timeval		 to;

	markit();

	to.tv_sec = TIMERINTVL;
	to.tv_usec = 0;
	evtimer_add(ev, &to);
}

void
init_signalcb(int signum, short event, void *arg)
{
	init();
	log_info(LOG_INFO, "restart");

	dropped_warn(&udpsend_dropped, "to udp loghost");
	dropped_warn(&tcpbuf_dropped, "to remote loghost");
	dropped_warn(&file_dropped, "to file");
	log_debug("syslogd: restarted");
}

void
logevent(int severity, const char *msg)
{
	log_debug("libevent: [%d] %s", severity, msg);
}

void
dropped_warn(int *count, const char *what)
{
	int dropped;

	if (*count == 0)
		return;

	dropped = *count;
	*count = 0;
	log_info(LOG_WARNING, "dropped %d message%s %s",
	    dropped, dropped == 1 ? "" : "s", what);
}

__dead void
die(int signo)
{
	struct filed *f;

	SIMPLEQ_FOREACH(f, &Files, f_next) {
		/* flush any pending output */
		if (f->f_prevcount)
			fprintlog(f, 0, (char *)NULL);
		if (f->f_type == F_FORWUDP) {
			udpsend_dropped += f->f_dropped;
			f->f_dropped = 0;
		}
		if (f->f_type == F_FORWTLS || f->f_type == F_FORWTCP) {
			tcpbuf_dropped += f->f_dropped +
			    tcpbuf_countmsg(f->f_un.f_forw.f_bufev);
			f->f_dropped = 0;
		}
		if (f->f_type == F_FILE) {
			file_dropped += f->f_dropped;
			f->f_dropped = 0;
		}
	}
	dropped_warn(&init_dropped, "during initialization");
	dropped_warn(&udpsend_dropped, "to udp loghost");
	dropped_warn(&tcpbuf_dropped, "to remote loghost");
	dropped_warn(&file_dropped, "to file");

	if (signo)
		log_info(LOG_ERR, "exiting on signal %d", signo);
	log_debug("syslogd: exited");
	exit(0);
}

/*
 *  INIT -- Initialize syslogd from configuration table
 */
void
init(void)
{
	char progblock[NAME_MAX+1], hostblock[NAME_MAX+1], *cline, *p, *q;
	struct filed_list mb;
	struct filed *f, *m;
	FILE *cf;
	int i;
	size_t s;

	log_debug("init");

	/* If config file has been modified, then just die to restart */
	if (priv_config_modified()) {
		log_debug("config file changed: dying");
		die(0);
	}

	/*
	 *  Close all open log files.
	 */
	Initialized = 0;
	SIMPLEQ_INIT(&mb);
	while (!SIMPLEQ_EMPTY(&Files)) {
		f = SIMPLEQ_FIRST(&Files);
		SIMPLEQ_REMOVE_HEAD(&Files, f_next);
		/* flush any pending output */
		if (f->f_prevcount)
			fprintlog(f, 0, (char *)NULL);

		switch (f->f_type) {
		case F_FORWUDP:
			evtimer_del(&f->f_un.f_forw.f_ev);
			udpsend_dropped += f->f_dropped;
			f->f_dropped = 0;
			free(f->f_un.f_forw.f_ipproto);
			free(f->f_un.f_forw.f_host);
			free(f->f_un.f_forw.f_port);
			break;
		case F_FORWTLS:
			if (f->f_un.f_forw.f_ctx) {
				tls_close(f->f_un.f_forw.f_ctx);
				tls_free(f->f_un.f_forw.f_ctx);
			}
			/* FALLTHROUGH */
		case F_FORWTCP:
			evtimer_del(&f->f_un.f_forw.f_ev);
			tcpbuf_dropped += f->f_dropped;
			if (f->f_un.f_forw.f_bufev) {
				bufferevent_disable(f->f_un.f_forw.f_bufev,
				    EV_READ|EV_WRITE);
				tcpbuf_dropped +=
				     tcpbuf_countmsg(f->f_un.f_forw.f_bufev);
				bufferevent_free(f->f_un.f_forw.f_bufev);
			}
			free(f->f_un.f_forw.f_ipproto);
			free(f->f_un.f_forw.f_host);
			free(f->f_un.f_forw.f_port);
			/* FALLTHROUGH */
		case F_FILE:
			if (f->f_type == F_FILE)
				file_dropped += f->f_dropped;
			f->f_dropped = 0;
			/* FALLTHROUGH */
		case F_TTY:
		case F_CONSOLE:
		case F_PIPE:
			(void)close(f->f_file);
			break;
		}
		free(f->f_program);
		free(f->f_hostname);
		if (f->f_type == F_MEMBUF) {
			f->f_program = NULL;
			f->f_hostname = NULL;
			log_debug("add %p to mb", f);
			SIMPLEQ_INSERT_HEAD(&mb, f, f_next);
		} else
			free(f);
	}
	SIMPLEQ_INIT(&Files);

	/* open the configuration file */
	if ((cf = priv_open_config()) == NULL) {
		log_debug("cannot open %s", ConfFile);
		SIMPLEQ_INSERT_TAIL(&Files,
		    cfline("*.ERR\t/dev/console", "*", "*"), f_next);
		SIMPLEQ_INSERT_TAIL(&Files,
		    cfline("*.PANIC\t*", "*", "*"), f_next);
		Initialized = 1;
		dropped_warn(&init_dropped, "during initialization");
		return;
	}

	/*
	 *  Foreach line in the conf table, open that file.
	 */
	cline = NULL;
	s = 0;
	strlcpy(progblock, "*", sizeof(progblock));
	strlcpy(hostblock, "*", sizeof(hostblock));
	send_udp = send_udp6 = 0;
	while (getline(&cline, &s, cf) != -1) {
		/*
		 * check for end-of-section, comments, strip off trailing
		 * spaces and newline character. !progblock and +hostblock
		 * are treated specially: the following lines apply only to
		 * that program.
		 */
		for (p = cline; isspace((unsigned char)*p); ++p)
			continue;
		if (*p == '\0' || *p == '#')
			continue;
		if (*p == '!' || *p == '+') {
			q = (*p == '!') ? progblock : hostblock;
			p++;
			while (isspace((unsigned char)*p))
				p++;
			if (*p == '\0' || (*p == '*' && (p[1] == '\0' ||
			    isspace((unsigned char)p[1])))) {
				strlcpy(q, "*", NAME_MAX+1);
				continue;
			}
			for (i = 0; i < NAME_MAX; i++) {
				if (*p == '\0' || isspace((unsigned char)*p))
					break;
				*q++ = *p++;
			}
			*q = '\0';
			continue;
		}

		p = cline + strlen(cline);
		while (p > cline)
			if (!isspace((unsigned char)*--p)) {
				p++;
				break;
			}
		*p = '\0';
		f = cfline(cline, progblock, hostblock);
		if (f != NULL)
			SIMPLEQ_INSERT_TAIL(&Files, f, f_next);
	}
	free(cline);
	if (!feof(cf))
		fatal("read config file");

	/* Match and initialize the memory buffers */
	SIMPLEQ_FOREACH(f, &Files, f_next) {
		if (f->f_type != F_MEMBUF)
			continue;
		log_debug("Initialize membuf %s at %p",
		    f->f_un.f_mb.f_mname, f);

		SIMPLEQ_FOREACH(m, &mb, f_next) {
			if (m->f_un.f_mb.f_rb == NULL)
				continue;
			if (strcmp(m->f_un.f_mb.f_mname,
			    f->f_un.f_mb.f_mname) == 0)
				break;
		}
		if (m == NULL) {
			log_debug("Membuf no match");
			f->f_un.f_mb.f_rb = ringbuf_init(f->f_un.f_mb.f_len);
			if (f->f_un.f_mb.f_rb == NULL) {
				f->f_type = F_UNUSED;
				log_warn("allocate membuf");
			}
		} else {
			log_debug("Membuf match f:%p, m:%p", f, m);
			f->f_un = m->f_un;
			m->f_un.f_mb.f_rb = NULL;
		}
	}

	/* make sure remaining buffers are freed */
	while (!SIMPLEQ_EMPTY(&mb)) {
		m = SIMPLEQ_FIRST(&mb);
		SIMPLEQ_REMOVE_HEAD(&mb, f_next);
		if (m->f_un.f_mb.f_rb != NULL) {
			log_warnx("mismatched membuf");
			ringbuf_free(m->f_un.f_mb.f_rb);
		}
		log_debug("Freeing membuf %p", m);

		free(m);
	}

	/* close the configuration file */
	(void)fclose(cf);

	Initialized = 1;
	dropped_warn(&init_dropped, "during initialization");

	if (SecureMode) {
		/*
		 * If generic UDP file descriptors are used neither
		 * for receiving nor for sending, close them.  Then
		 * there is no useless *.514 in netstat.
		 */
		if (fd_udp != -1 && !send_udp) {
			close(fd_udp);
			fd_udp = -1;
		}
		if (fd_udp6 != -1 && !send_udp6) {
			close(fd_udp6);
			fd_udp6 = -1;
		}
	}

	if (Debug) {
		SIMPLEQ_FOREACH(f, &Files, f_next) {
			for (i = 0; i <= LOG_NFACILITIES; i++)
				if (f->f_pmask[i] == INTERNAL_NOPRI)
					printf("X ");
				else
					printf("%d ", f->f_pmask[i]);
			printf("%s: ", TypeNames[f->f_type]);
			switch (f->f_type) {
			case F_FILE:
			case F_TTY:
			case F_CONSOLE:
			case F_PIPE:
				printf("%s", f->f_un.f_fname);
				break;

			case F_FORWUDP:
			case F_FORWTCP:
			case F_FORWTLS:
				printf("%s", f->f_un.f_forw.f_loghost);
				break;

			case F_USERS:
				for (i = 0; i < MAXUNAMES &&
				    *f->f_un.f_uname[i]; i++)
					printf("%s, ", f->f_un.f_uname[i]);
				break;

			case F_MEMBUF:
				printf("%s", f->f_un.f_mb.f_mname);
				break;

			}
			if (f->f_program || f->f_hostname)
				printf(" (%s, %s)",
				    f->f_program ? f->f_program : "*",
				    f->f_hostname ? f->f_hostname : "*");
			printf("\n");
		}
	}
}

#define progmatches(p1, p2) \
	(p1 == p2 || (p1 != NULL && p2 != NULL && strcmp(p1, p2) == 0))

/*
 * Spot a line with a duplicate file, pipe, console, tty, or membuf target.
 */
struct filed *
find_dup(struct filed *f)
{
	struct filed *list;

	SIMPLEQ_FOREACH(list, &Files, f_next) {
		if (list->f_quick || f->f_quick)
			continue;
		switch (list->f_type) {
		case F_FILE:
		case F_TTY:
		case F_CONSOLE:
		case F_PIPE:
			if (strcmp(list->f_un.f_fname, f->f_un.f_fname) == 0 &&
			    progmatches(list->f_program, f->f_program) &&
			    progmatches(list->f_hostname, f->f_hostname)) {
				log_debug("duplicate %s", f->f_un.f_fname);
				return (list);
			}
			break;
		case F_MEMBUF:
			if (strcmp(list->f_un.f_mb.f_mname,
			    f->f_un.f_mb.f_mname) == 0 &&
			    progmatches(list->f_program, f->f_program) &&
			    progmatches(list->f_hostname, f->f_hostname)) {
				log_debug("duplicate membuf %s",
				    f->f_un.f_mb.f_mname);
				return (list);
			}
			break;
		}
	}
	return (NULL);
}

/*
 * Crack a configuration file line
 */
struct filed *
cfline(char *line, char *progblock, char *hostblock)
{
	int i, pri;
	size_t rb_len;
	char *bp, *p, *q, *proto, *host, *port, *ipproto;
	char buf[LOG_MAXLINE];
	struct filed *xf, *f, *d;
	struct timeval to;

	log_debug("cfline(\"%s\", f, \"%s\", \"%s\")",
	    line, progblock, hostblock);

	if ((f = calloc(1, sizeof(*f))) == NULL)
		fatal("allocate struct filed");
	for (i = 0; i <= LOG_NFACILITIES; i++)
		f->f_pmask[i] = INTERNAL_NOPRI;

	/* save program name if any */
	f->f_quick = 0;
	if (*progblock == '!') {
		progblock++;
		f->f_quick = 1;
	}
	if (*hostblock == '+') {
		hostblock++;
		f->f_quick = 1;
	}
	if (strcmp(progblock, "*") != 0)
		f->f_program = strdup(progblock);
	if (strcmp(hostblock, "*") != 0)
		f->f_hostname = strdup(hostblock);

	/* scan through the list of selectors */
	for (p = line; *p && *p != '\t' && *p != ' ';) {

		/* find the end of this facility name list */
		for (q = p; *q && *q != '\t' && *q != ' ' && *q++ != '.'; )
			continue;

		/* collect priority name */
		for (bp = buf; *q && !strchr("\t,; ", *q); )
			*bp++ = *q++;
		*bp = '\0';

		/* skip cruft */
		while (*q && strchr(",;", *q))
			q++;

		/* decode priority name */
		if (*buf == '*')
			pri = LOG_PRIMASK + 1;
		else {
			/* ignore trailing spaces */
			for (i=strlen(buf)-1; i >= 0 && buf[i] == ' '; i--) {
				buf[i]='\0';
			}

			pri = decode(buf, prioritynames);
			if (pri < 0) {
				log_warnx("unknown priority name \"%s\"", buf);
				free(f);
				return (NULL);
			}
		}

		/* scan facilities */
		while (*p && !strchr("\t.; ", *p)) {
			for (bp = buf; *p && !strchr("\t,;. ", *p); )
				*bp++ = *p++;
			*bp = '\0';
			if (*buf == '*')
				for (i = 0; i < LOG_NFACILITIES; i++)
					f->f_pmask[i] = pri;
			else {
				i = decode(buf, facilitynames);
				if (i < 0) {
					log_warnx("unknown facility name "
					    "\"%s\"", buf);
					free(f);
					return (NULL);
				}
				f->f_pmask[i >> 3] = pri;
			}
			while (*p == ',' || *p == ' ')
				p++;
		}

		p = q;
	}

	/* skip to action part */
	while (*p == '\t' || *p == ' ')
		p++;

	switch (*p) {
	case '@':
		if ((strlcpy(f->f_un.f_forw.f_loghost, p,
		    sizeof(f->f_un.f_forw.f_loghost)) >=
		    sizeof(f->f_un.f_forw.f_loghost))) {
			log_warnx("loghost too long \"%s\"", p);
			break;
		}
		if (loghost_parse(++p, &proto, &host, &port) == -1) {
			log_warnx("bad loghost \"%s\"",
			    f->f_un.f_forw.f_loghost);
			break;
		}
		if (proto == NULL)
			proto = "udp";
		if (strcmp(proto, "udp") == 0) {
			if (fd_udp == -1)
				proto = "udp6";
			if (fd_udp6 == -1)
				proto = "udp4";
		}
		ipproto = proto;
		if (strcmp(proto, "udp") == 0) {
			send_udp = send_udp6 = 1;
		} else if (strcmp(proto, "udp4") == 0) {
			send_udp = 1;
			if (fd_udp == -1) {
				log_warnx("no udp4 \"%s\"",
				    f->f_un.f_forw.f_loghost);
				break;
			}
		} else if (strcmp(proto, "udp6") == 0) {
			send_udp6 = 1;
			if (fd_udp6 == -1) {
				log_warnx("no udp6 \"%s\"",
				    f->f_un.f_forw.f_loghost);
				break;
			}
		} else if (strcmp(proto, "tcp") == 0 ||
		    strcmp(proto, "tcp4") == 0 || strcmp(proto, "tcp6") == 0) {
			;
		} else if (strcmp(proto, "tls") == 0) {
			ipproto = "tcp";
		} else if (strcmp(proto, "tls4") == 0) {
			ipproto = "tcp4";
		} else if (strcmp(proto, "tls6") == 0) {
			ipproto = "tcp6";
		} else {
			log_warnx("bad protocol \"%s\"",
			    f->f_un.f_forw.f_loghost);
			break;
		}
		if (strlen(host) >= NI_MAXHOST) {
			log_warnx("host too long \"%s\"",
			    f->f_un.f_forw.f_loghost);
			break;
		}
		if (port == NULL)
			port = strncmp(proto, "tls", 3) == 0 ?
			    "syslog-tls" : "syslog";
		if (strlen(port) >= NI_MAXSERV) {
			log_warnx("port too long \"%s\"",
			    f->f_un.f_forw.f_loghost);
			break;
		}
		f->f_un.f_forw.f_ipproto = strdup(ipproto);
		f->f_un.f_forw.f_host = strdup(host);
		f->f_un.f_forw.f_port = strdup(port);
		if (f->f_un.f_forw.f_ipproto == NULL ||
		    f->f_un.f_forw.f_host == NULL ||
		    f->f_un.f_forw.f_port == NULL) {
			log_warnx("strdup ipproto host port \"%s\"",
			    f->f_un.f_forw.f_loghost);
			free(f->f_un.f_forw.f_ipproto);
			free(f->f_un.f_forw.f_host);
			free(f->f_un.f_forw.f_port);
			break;
		}
		f->f_file = -1;
		loghost_resolve(f);
		if (strncmp(proto, "udp", 3) == 0) {
			evtimer_set(&f->f_un.f_forw.f_ev, udp_resolvecb, f);
			switch (f->f_un.f_forw.f_addr.ss_family) {
			case AF_UNSPEC:
				log_debug("resolve \"%s\" delayed",
				    f->f_un.f_forw.f_loghost);
				to.tv_sec = 0;
				to.tv_usec = 1;
				evtimer_add(&f->f_un.f_forw.f_ev, &to);
				break;
			case AF_INET:
				f->f_file = fd_udp;
				break;
			case AF_INET6:
				f->f_file = fd_udp6;
				break;
			}
			f->f_type = F_FORWUDP;
		} else if (strncmp(ipproto, "tcp", 3) == 0) {
			if ((f->f_un.f_forw.f_bufev = bufferevent_new(-1,
			    tcp_dropcb, tcp_writecb, tcp_errorcb, f)) == NULL) {
				log_warn("bufferevent \"%s\"",
				    f->f_un.f_forw.f_loghost);
				free(f->f_un.f_forw.f_ipproto);
				free(f->f_un.f_forw.f_host);
				free(f->f_un.f_forw.f_port);
				break;
			}
			/*
			 * If we try to connect to a TLS server immediately
			 * syslogd gets an SIGPIPE as the signal handlers have
			 * not been set up.  Delay the connection until the
			 * event loop is started.
			 */
			evtimer_set(&f->f_un.f_forw.f_ev, tcp_connectcb, f);
			to.tv_sec = 0;
			to.tv_usec = 1;
			evtimer_add(&f->f_un.f_forw.f_ev, &to);
			f->f_type = (strncmp(proto, "tls", 3) == 0) ?
			    F_FORWTLS : F_FORWTCP;
		}
		break;

	case '/':
	case '|':
		(void)strlcpy(f->f_un.f_fname, p, sizeof(f->f_un.f_fname));
		d = find_dup(f);
		if (d != NULL) {
			for (i = 0; i <= LOG_NFACILITIES; i++)
				if (f->f_pmask[i] != INTERNAL_NOPRI)
					d->f_pmask[i] = f->f_pmask[i];
			free(f);
			return (NULL);
		}
		if (strcmp(p, ctty) == 0) {
			f->f_file = priv_open_tty(p);
			if (f->f_file < 0)
				log_warn("priv_open_tty \"%s\"", p);
		} else {
			f->f_file = priv_open_log(p);
			if (f->f_file < 0)
				log_warn("priv_open_log \"%s\"", p);
		}
		if (f->f_file < 0) {
			f->f_type = F_UNUSED;
			break;
		}
		if (isatty(f->f_file)) {
			if (strcmp(p, ctty) == 0)
				f->f_type = F_CONSOLE;
			else
				f->f_type = F_TTY;
		} else {
			if (*p == '|')
				f->f_type = F_PIPE;
			else {
				f->f_type = F_FILE;

				/* Clear O_NONBLOCK flag on f->f_file */
				if ((i = fcntl(f->f_file, F_GETFL)) != -1) {
					i &= ~O_NONBLOCK;
					fcntl(f->f_file, F_SETFL, i);
				}
			}
		}
		break;

	case '*':
		f->f_type = F_WALL;
		break;

	case ':':
		f->f_type = F_MEMBUF;

		/* Parse buffer size (in kb) */
		errno = 0;
		rb_len = strtoul(++p, &q, 0);
		if (*p == '\0' || (errno == ERANGE && rb_len == ULONG_MAX) ||
		    *q != ':' || rb_len == 0) {
			f->f_type = F_UNUSED;
			log_warnx("strtoul \"%s\"", p);
			break;
		}
		q++;
		rb_len *= 1024;

		/* Copy buffer name */
		for(i = 0; (size_t)i < sizeof(f->f_un.f_mb.f_mname) - 1; i++) {
			if (!isalnum((unsigned char)q[i]))
				break;
			f->f_un.f_mb.f_mname[i] = q[i];
		}

		/* Make sure buffer name is unique */
		xf = find_dup(f);

		/* Error on missing or non-unique name, or bad buffer length */
		if (i == 0 || rb_len > MAX_MEMBUF || xf != NULL) {
			f->f_type = F_UNUSED;
			log_warnx("find_dup \"%s\"", p);
			break;
		}

		/* Set buffer length */
		rb_len = MAXIMUM(rb_len, MIN_MEMBUF);
		f->f_un.f_mb.f_len = rb_len;
		f->f_un.f_mb.f_overflow = 0;
		f->f_un.f_mb.f_attached = 0;
		break;

	default:
		for (i = 0; i < MAXUNAMES && *p; i++) {
			for (q = p; *q && *q != ','; )
				q++;
			(void)strncpy(f->f_un.f_uname[i], p, UT_NAMESIZE);
			if ((q - p) > UT_NAMESIZE)
				f->f_un.f_uname[i][UT_NAMESIZE] = '\0';
			else
				f->f_un.f_uname[i][q - p] = '\0';
			while (*q == ',' || *q == ' ')
				q++;
			p = q;
		}
		f->f_type = F_USERS;
		break;
	}
	return (f);
}

/*
 * Parse the host and port parts from a loghost string.
 */
int
loghost_parse(char *str, char **proto, char **host, char **port)
{
	char *prefix = NULL;

	if ((*host = strchr(str, ':')) &&
	    (*host)[1] == '/' && (*host)[2] == '/') {
		prefix = str;
		**host = '\0';
		str = *host + 3;
	}
	if (proto)
		*proto = prefix;
	else if (prefix)
		return (-1);

	*host = str;
	if (**host == '[') {
		(*host)++;
		str = strchr(*host, ']');
		if (str == NULL)
			return (-1);
		*str++ = '\0';
	}
	*port = strrchr(str, ':');
	if (*port != NULL)
		*(*port)++ = '\0';

	return (0);
}

/*
 * Retrieve the size of the kernel message buffer, via sysctl.
 */
int
getmsgbufsize(void)
{
	int msgbufsize, mib[2];
	size_t size;

	mib[0] = CTL_KERN;
	mib[1] = KERN_MSGBUFSIZE;
	size = sizeof msgbufsize;
	if (sysctl(mib, 2, &msgbufsize, &size, NULL, 0) == -1) {
		log_debug("couldn't get kern.msgbufsize");
		return (0);
	}
	return (msgbufsize);
}

/*
 *  Decode a symbolic name to a numeric value
 */
int
decode(const char *name, const CODE *codetab)
{
	const CODE *c;
	char *p, buf[40];

	for (p = buf; *name && p < &buf[sizeof(buf) - 1]; p++, name++) {
		if (isupper((unsigned char)*name))
			*p = tolower((unsigned char)*name);
		else
			*p = *name;
	}
	*p = '\0';
	for (c = codetab; c->c_name; c++)
		if (!strcmp(buf, c->c_name))
			return (c->c_val);

	return (-1);
}

void
markit(void)
{
	struct msg msg;
	struct filed *f;

	msg.m_pri = LOG_INFO;
	current_time(msg.m_timestamp);
	msg.m_prog[0] = '\0';
	strlcpy(msg.m_msg, "-- MARK --", sizeof(msg.m_msg));
	MarkSeq += TIMERINTVL;
	if (MarkSeq >= MarkInterval) {
		logmsg(&msg, MARK, LocalHostName);
		MarkSeq = 0;
	}

	SIMPLEQ_FOREACH(f, &Files, f_next) {
		if (f->f_prevcount && now.tv_sec >= REPEATTIME(f)) {
			log_debug("flush %s: repeated %d times, %d sec",
			    TypeNames[f->f_type], f->f_prevcount,
			    repeatinterval[f->f_repeatcount]);
			fprintlog(f, 0, (char *)NULL);
			BACKOFF(f);
		}
	}
}

int
unix_socket(char *path, int type, mode_t mode)
{
	struct sockaddr_un s_un;
	int fd, optval;
	mode_t old_umask;

	memset(&s_un, 0, sizeof(s_un));
	s_un.sun_family = AF_UNIX;
	if (strlcpy(s_un.sun_path, path, sizeof(s_un.sun_path)) >=
	    sizeof(s_un.sun_path)) {
		log_warnx("socket path too long \"%s\"", path);
		return (-1);
	}

	if ((fd = socket(AF_UNIX, type, 0)) == -1) {
		log_warn("socket unix \"%s\"", path);
		return (-1);
	}

	if (Debug) {
		if (connect(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == 0 ||
		    errno == EPROTOTYPE) {
			close(fd);
			errno = EISCONN;
			log_warn("connect unix \"%s\"", path);
			return (-1);
		}
	}

	old_umask = umask(0177);

	unlink(path);
	if (bind(fd, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		log_warn("bind unix \"%s\"", path);
		umask(old_umask);
		close(fd);
		return (-1);
	}

	umask(old_umask);

	if (chmod(path, mode) == -1) {
		log_warn("chmod unix \"%s\"", path);
		close(fd);
		unlink(path);
		return (-1);
	}

	optval = LOG_MAXLINE + PATH_MAX;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval))
	    == -1)
		log_warn("setsockopt unix \"%s\"", path);

	return (fd);
}

/*
 * Increase socket buffer size in small steps to get partial success
 * if we hit a kernel limit.  Allow an optional final step.
 */
void
double_sockbuf(int fd, int optname, int bigsize)
{
	socklen_t len;
	int i, newsize, oldsize = 0;

	len = sizeof(oldsize);
	if (getsockopt(fd, SOL_SOCKET, optname, &oldsize, &len) == -1)
		log_warn("getsockopt bufsize");
	len = sizeof(newsize);
	newsize =  LOG_MAXLINE + 128;  /* data + control */
	/* allow 8 full length messages, that is 66560 bytes */
	for (i = 0; i < 4; i++, newsize *= 2) {
		if (newsize <= oldsize)
			continue;
		if (setsockopt(fd, SOL_SOCKET, optname, &newsize, len) == -1)
			log_warn("setsockopt bufsize %d", newsize);
		else
			oldsize = newsize;
	}
	if (bigsize && bigsize > oldsize) {
		if (setsockopt(fd, SOL_SOCKET, optname, &bigsize, len) == -1)
			log_warn("setsockopt bufsize %d", bigsize);
	}
}

void
set_sockbuf(int fd)
{
	int size = 65536;

	if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == -1)
		log_warn("setsockopt sndbufsize %d", size);
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == -1)
		log_warn("setsockopt rcvbufsize %d", size);
}

void
set_keepalive(int fd)
{
	int val = 1;

	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
		log_warn("setsockopt keepalive %d", val);
}

void
ctlconn_cleanup(void)
{
	struct filed *f;

	close(fd_ctlconn);
	fd_ctlconn = -1;
	event_del(ev_ctlread);
	event_del(ev_ctlwrite);
	event_add(ev_ctlaccept, NULL);

	if (ctl_state == CTL_WRITING_CONT_REPLY)
		SIMPLEQ_FOREACH(f, &Files, f_next)
			if (f->f_type == F_MEMBUF)
				f->f_un.f_mb.f_attached = 0;

	ctl_state = ctl_cmd_bytes = ctl_reply_offset = ctl_reply_size = 0;
}

void
ctlsock_acceptcb(int fd, short event, void *arg)
{
	struct event		*ev = arg;

	if ((fd = reserve_accept4(fd, event, ev, ctlsock_acceptcb,
	    NULL, NULL, SOCK_NONBLOCK)) == -1) {
		if (errno != ENFILE && errno != EMFILE &&
		    errno != EINTR && errno != EWOULDBLOCK &&
		    errno != ECONNABORTED)
			log_warn("accept control socket");
		return;
	}
	log_debug("Accepting control connection");

	if (fd_ctlconn != -1)
		ctlconn_cleanup();

	/* Only one connection at a time */
	event_del(ev);

	fd_ctlconn = fd;
	/* file descriptor has changed, reset event */
	event_set(ev_ctlread, fd_ctlconn, EV_READ|EV_PERSIST,
	    ctlconn_readcb, ev_ctlread);
	event_set(ev_ctlwrite, fd_ctlconn, EV_WRITE|EV_PERSIST,
	    ctlconn_writecb, ev_ctlwrite);
	event_add(ev_ctlread, NULL);
	ctl_state = CTL_READING_CMD;
	ctl_cmd_bytes = 0;
}

static struct filed
*find_membuf_log(const char *name)
{
	struct filed *f;

	SIMPLEQ_FOREACH(f, &Files, f_next) {
		if (f->f_type == F_MEMBUF &&
		    strcmp(f->f_un.f_mb.f_mname, name) == 0)
			break;
	}
	return (f);
}

void
ctlconn_readcb(int fd, short event, void *arg)
{
	struct filed		*f;
	struct ctl_reply_hdr	*reply_hdr = (struct ctl_reply_hdr *)ctl_reply;
	ssize_t			 n;
	u_int32_t		 flags = 0;

	if (ctl_state == CTL_WRITING_REPLY ||
	    ctl_state == CTL_WRITING_CONT_REPLY) {
		/* client has closed the connection */
		ctlconn_cleanup();
		return;
	}

 retry:
	n = read(fd, (char*)&ctl_cmd + ctl_cmd_bytes,
	    sizeof(ctl_cmd) - ctl_cmd_bytes);
	switch (n) {
	case -1:
		if (errno == EINTR)
			goto retry;
		if (errno == EWOULDBLOCK)
			return;
		log_warn("read control socket");
		/* FALLTHROUGH */
	case 0:
		ctlconn_cleanup();
		return;
	default:
		ctl_cmd_bytes += n;
	}
	if (ctl_cmd_bytes < sizeof(ctl_cmd))
		return;

	if (ntohl(ctl_cmd.version) != CTL_VERSION) {
		log_warnx("unknown client protocol version");
		ctlconn_cleanup();
		return;
	}

	/* Ensure that logname is \0 terminated */
	if (memchr(ctl_cmd.logname, '\0', sizeof(ctl_cmd.logname)) == NULL) {
		log_warnx("corrupt control socket command");
		ctlconn_cleanup();
		return;
	}

	*reply_text = '\0';

	ctl_reply_size = ctl_reply_offset = 0;
	memset(reply_hdr, '\0', sizeof(*reply_hdr));

	ctl_cmd.cmd = ntohl(ctl_cmd.cmd);
	log_debug("ctlcmd %x logname \"%s\"", ctl_cmd.cmd, ctl_cmd.logname);

	switch (ctl_cmd.cmd) {
	case CMD_READ:
	case CMD_READ_CLEAR:
	case CMD_READ_CONT:
	case CMD_FLAGS:
		f = find_membuf_log(ctl_cmd.logname);
		if (f == NULL) {
			strlcpy(reply_text, "No such log\n", MAX_MEMBUF);
		} else {
			if (ctl_cmd.cmd != CMD_FLAGS) {
				ringbuf_to_string(reply_text, MAX_MEMBUF,
				    f->f_un.f_mb.f_rb);
			}
			if (f->f_un.f_mb.f_overflow)
				flags |= CTL_HDR_FLAG_OVERFLOW;
			if (ctl_cmd.cmd == CMD_READ_CLEAR) {
				ringbuf_clear(f->f_un.f_mb.f_rb);
				f->f_un.f_mb.f_overflow = 0;
			}
			if (ctl_cmd.cmd == CMD_READ_CONT) {
				f->f_un.f_mb.f_attached = 1;
				tailify_replytext(reply_text,
				    ctl_cmd.lines > 0 ? ctl_cmd.lines : 10);
			} else if (ctl_cmd.lines > 0) {
				tailify_replytext(reply_text, ctl_cmd.lines);
			}
		}
		break;
	case CMD_CLEAR:
		f = find_membuf_log(ctl_cmd.logname);
		if (f == NULL) {
			strlcpy(reply_text, "No such log\n", MAX_MEMBUF);
		} else {
			ringbuf_clear(f->f_un.f_mb.f_rb);
			if (f->f_un.f_mb.f_overflow)
				flags |= CTL_HDR_FLAG_OVERFLOW;
			f->f_un.f_mb.f_overflow = 0;
			strlcpy(reply_text, "Log cleared\n", MAX_MEMBUF);
		}
		break;
	case CMD_LIST:
		SIMPLEQ_FOREACH(f, &Files, f_next) {
			if (f->f_type == F_MEMBUF) {
				strlcat(reply_text, f->f_un.f_mb.f_mname,
				    MAX_MEMBUF);
				if (f->f_un.f_mb.f_overflow) {
					strlcat(reply_text, "*", MAX_MEMBUF);
					flags |= CTL_HDR_FLAG_OVERFLOW;
				}
				strlcat(reply_text, " ", MAX_MEMBUF);
			}
		}
		strlcat(reply_text, "\n", MAX_MEMBUF);
		break;
	default:
		log_warnx("unsupported control socket command");
		ctlconn_cleanup();
		return;
	}
	reply_hdr->version = htonl(CTL_VERSION);
	reply_hdr->flags = htonl(flags);

	ctl_reply_size = CTL_REPLY_SIZE;
	log_debug("ctlcmd reply length %lu", (u_long)ctl_reply_size);

	/* Otherwise, set up to write out reply */
	ctl_state = (ctl_cmd.cmd == CMD_READ_CONT) ?
	    CTL_WRITING_CONT_REPLY : CTL_WRITING_REPLY;

	event_add(ev_ctlwrite, NULL);

	/* another syslogc can kick us out */
	if (ctl_state == CTL_WRITING_CONT_REPLY)
		event_add(ev_ctlaccept, NULL);
}

void
ctlconn_writecb(int fd, short event, void *arg)
{
	struct event		*ev = arg;
	ssize_t			 n;

	if (!(ctl_state == CTL_WRITING_REPLY ||
	    ctl_state == CTL_WRITING_CONT_REPLY)) {
		/* Shouldn't be here! */
		log_warnx("control socket write with bad state");
		ctlconn_cleanup();
		return;
	}

 retry:
	n = write(fd, ctl_reply + ctl_reply_offset,
	    ctl_reply_size - ctl_reply_offset);
	switch (n) {
	case -1:
		if (errno == EINTR)
			goto retry;
		if (errno == EWOULDBLOCK)
			return;
		if (errno != EPIPE)
			log_warn("write control socket");
		/* FALLTHROUGH */
	case 0:
		ctlconn_cleanup();
		return;
	default:
		ctl_reply_offset += n;
	}
	if (ctl_reply_offset < ctl_reply_size)
		return;

	if (ctl_state != CTL_WRITING_CONT_REPLY) {
		ctlconn_cleanup();
		return;
	}

	/*
	 * Make space in the buffer for continuous writes.
	 * Set offset behind reply header to skip it
	 */
	*reply_text = '\0';
	ctl_reply_offset = ctl_reply_size = CTL_REPLY_SIZE;

	/* Now is a good time to report dropped lines */
	if (membuf_drop) {
		strlcat(reply_text, "<ENOBUFS>\n", MAX_MEMBUF);
		ctl_reply_size = CTL_REPLY_SIZE;
		membuf_drop = 0;
	} else {
		/* Nothing left to write */
		event_del(ev);
	}
}

/* Shorten replytext to number of lines */
void
tailify_replytext(char *replytext, int lines)
{
	char *start, *nl;
	int count = 0;
	start = nl = replytext;

	while ((nl = strchr(nl, '\n')) != NULL) {
		nl++;
		if (++count > lines) {
			start = strchr(start, '\n');
			start++;
		}
	}
	if (start != replytext) {
		int len = strlen(start);
		memmove(replytext, start, len);
		*(replytext + len) = '\0';
	}
}

void
ctlconn_logto(char *line)
{
	size_t l;

	if (membuf_drop)
		return;

	l = strlen(line);
	if (l + 2 > (CTL_REPLY_MAXSIZE - ctl_reply_size)) {
		/* remember line drops for later report */
		membuf_drop = 1;
		return;
	}
	memcpy(ctl_reply + ctl_reply_size, line, l);
	memcpy(ctl_reply + ctl_reply_size + l, "\n", 2);
	ctl_reply_size += l + 1;
	event_add(ev_ctlwrite, NULL);
}
