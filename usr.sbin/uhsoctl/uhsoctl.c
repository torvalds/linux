/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Fredrik Lindberg
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/queue.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_var.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <libutil.h>
#include <time.h>

/*
 * Connection utility to ease connectivity using the raw IP packet interface
 * available on uhso(4) devices.
 */

#define TTY_NAME	"/dev/%s"
#define SYSCTL_TEST	"dev.uhso.%d.%%driver"
#define SYSCTL_LOCATION "dev.uhso.%d.%%location"
#define SYSCTL_PORTS	"dev.uhso.%d.ports"
#define SYSCTL_NETIF	"dev.uhso.%d.netif"
#define SYSCTL_NAME_TTY	"dev.uhso.%d.port.%s.tty"
#define SYSCTL_NAME_DESC "dev.uhso.%d.port.%s.desc"
#define RESOLV_PATH	"/etc/resolv.conf"
#define PIDFILE		"/var/run/uhsoctl.%s.pid"

static const char *network_access_type[] = {
	"GSM",
	"Compact GSM",
	"UMTS",
	"GSM (EGPRS)",
	"HSDPA",
	"HSUPA",
	"HSDPA/HSUPA"
};

static const char *network_reg_status[] = {
	"Not registered",
	"Registered",
	"Searching for network",
	"Network registration denied",
	"Unknown",
	"Registered (roaming)"
};

struct ctx {
	int fd;
	int flags;
#define IPASSIGNED	0x01
#define FLG_NODAEMON	0x02 /* Don't detach from terminal */
#define FLG_DAEMON	0x04 /* Running as daemon */
#define FLG_DELAYED	0x08 /* Fork into background after connect */
#define FLG_NEWDATA	0x10
#define FLG_WATCHDOG	0x20 /* Watchdog enabled */
#define FLG_WDEXP	0x40 /* Watchdog expired */
	const char *ifnam;
	const char *pin; /* device PIN */

	char pidfile[128];
	struct pidfh *pfh;

	time_t watchdog;

	/* PDP context settings */
	int pdp_ctx;
	const char *pdp_apn;
	const char *pdp_user;
	const char *pdp_pwd;

	/* Connection status */
	int con_status;		/* Connected? */
	char *con_apn;		/* Connected APN */
	char *con_oper;		/* Operator name */
	int con_net_stat;	/* Network connection status */
	int con_net_type;	/* Network connection type */

	/* Misc. status */
	int dbm;

	/* IP and nameserver settings */
	struct in_addr ip;
	char **ns;
	const char *resolv_path;
	char *resolv;		/* Old resolv.conf */
	size_t resolv_sz;
};

static int readline_buf(const char *, const char *, char *, size_t);
static int readline(int, char *, size_t);
static void daemonize(struct ctx *);

static int at_cmd_async(int, const char *, ...);

typedef union {
	void *ptr;
	uint32_t int32;
} resp_data;
typedef struct {
	resp_data val[2];	
} resp_arg;
typedef void (*resp_cb)(resp_arg *, const char *, const char *);

typedef void (*async_cb)(void *, const char *);
struct async_handle {
	const char *cmd;
	async_cb func;
};

static void at_async_creg(void *, const char *);
static void at_async_cgreg(void *, const char *);
static void at_async_cops(void *, const char *);
static void at_async_owancall(void *, const char *);
static void at_async_owandata(void *, const char *);
static void at_async_csq(void *, const char *);

static struct async_handle async_cmd[] = {
	{ "+CREG", at_async_creg },
	{ "+CGREG", at_async_cgreg },
	{ "+COPS", at_async_cops },
	{ "+CSQ", at_async_csq },
	{ "_OWANCALL", at_async_owancall },
	{ "_OWANDATA", at_async_owandata },
	{ NULL, NULL }
};

struct timer_entry;
struct timers {
	TAILQ_HEAD(, timer_entry) head;
	int res;
};

typedef void (*tmr_cb)(int, void *);
struct timer_entry {
	TAILQ_ENTRY(timer_entry) next;
	int id;
	int timeout;
	tmr_cb func;
	void *arg;
};


static struct timers timers;
static volatile int running = 1;
static int syslog_open = 0;
static char syslog_title[64];

/* Periodic timer, runs ready timer tasks every tick */
static void
tmr_run(struct timers *tmrs)
{
	struct timer_entry *te, *te2;

	te = TAILQ_FIRST(&tmrs->head);
	if (te == NULL)
		return;

	te->timeout -= tmrs->res;
	while (te->timeout <= 0) {
		te2 = TAILQ_NEXT(te, next);
		TAILQ_REMOVE(&tmrs->head, te, next);
		te->func(te->id, te->arg);
		free(te);
		te = te2;
		if (te == NULL)
			break;
	}
}

/* Add a new timer */
static void
tmr_add(struct timers *tmrs, int id, int timeout, tmr_cb func, void *arg)
{
	struct timer_entry *te, *te2, *te3;

	te = malloc(sizeof(struct timer_entry));
	memset(te, 0, sizeof(struct timer_entry));

	te->timeout = timeout;
	te->func = func;
	te->arg = arg;
	te->id = id;

	te2 = TAILQ_FIRST(&tmrs->head);

	if (TAILQ_EMPTY(&tmrs->head)) {
		TAILQ_INSERT_HEAD(&tmrs->head, te, next);
	} else if (te->timeout < te2->timeout) {
		te2->timeout -= te->timeout;
		TAILQ_INSERT_HEAD(&tmrs->head, te, next);
	} else {
		while (te->timeout >= te2->timeout) {
			te->timeout -= te2->timeout;
			te3 = TAILQ_NEXT(te2, next);
			if (te3 == NULL || te3->timeout > te->timeout)
				break;
			te2 = te3;
		}
		TAILQ_INSERT_AFTER(&tmrs->head, te2, te, next);
	}
}

#define watchdog_enable(ctx) (ctx)->flags |= FLG_WATCHDOG
#define watchdog_disable(ctx) (ctx)->flags &= ~FLG_WATCHDOG

static void
watchdog_reset(struct ctx *ctx, int timeout)
{
	struct timespec tp;

	clock_gettime(CLOCK_MONOTONIC, &tp),
	ctx->watchdog = tp.tv_sec + timeout;

	watchdog_enable(ctx);
}

static void
tmr_creg(int id, void *arg)
{
	struct ctx *ctx = arg;

	at_cmd_async(ctx->fd, "AT+CREG?\r\n");
	watchdog_reset(ctx, 10);
}

static void
tmr_cgreg(int id, void *arg)
{
	struct ctx *ctx = arg;

	at_cmd_async(ctx->fd, "AT+CGREG?\r\n");
	watchdog_reset(ctx, 10);
}

static void
tmr_status(int id, void *arg)
{
	struct ctx *ctx = arg;

	at_cmd_async(ctx->fd, "AT+CSQ\r\n");
	watchdog_reset(ctx, 10);
}

static void
tmr_watchdog(int id, void *arg)
{
	struct ctx *ctx = arg;
	pid_t self;
	struct timespec tp;

	tmr_add(&timers, 1, 5, tmr_watchdog, ctx);

	if (!(ctx->flags & FLG_WATCHDOG))
		return;

	clock_gettime(CLOCK_MONOTONIC, &tp);

	if (tp.tv_sec >= ctx->watchdog) {
#ifdef DEBUG
		fprintf(stderr, "Watchdog expired\n");
#endif
		ctx->flags |= FLG_WDEXP;
		self = getpid();
		kill(self, SIGHUP);	
	}
}

static void
sig_handle(int sig)
{

	switch (sig) {
	case SIGHUP:
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		running = 0;
		break;
	case SIGALRM:
		tmr_run(&timers);
		break;
	}
}

static void
logger(int pri, const char *fmt, ...)
{
	char *buf;
	va_list ap;

	va_start(ap, fmt);
	vasprintf(&buf, fmt, ap);
	if (syslog_open)
		syslog(pri, "%s", buf);
	else {
		switch (pri) {
		case LOG_INFO:
		case LOG_NOTICE:
			printf("%s\n", buf);
			break;
		default:
			fprintf(stderr, "%s: %s\n", getprogname(), buf);
			break;
		}
	}

	free(buf);
	va_end(ap);
}

/* Add/remove IP address from an interface */
static int
ifaddr_ad(unsigned long d, const char *ifnam, struct sockaddr *sa, struct sockaddr *mask)
{
	struct ifaliasreq req;
	int fd, error;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return (-1);

	memset(&req, 0, sizeof(struct ifaliasreq));
	strlcpy(req.ifra_name, ifnam, sizeof(req.ifra_name));
	memcpy(&req.ifra_addr, sa, sa->sa_len);
	memcpy(&req.ifra_mask, mask, mask->sa_len);

	error = ioctl(fd, d, (char *)&req);
	close(fd);
	return (error);
}

#define if_ifup(ifnam) if_setflags(ifnam, IFF_UP)
#define if_ifdown(ifnam) if_setflags(ifnam, -IFF_UP)

static int
if_setflags(const char *ifnam, int flags)
{
	struct ifreq ifr;
	int fd, error;
	unsigned int oflags = 0;

	memset(&ifr, 0, sizeof(struct ifreq));
	strlcpy(ifr.ifr_name, ifnam, sizeof(ifr.ifr_name));

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return (-1);

	error = ioctl(fd, SIOCGIFFLAGS, &ifr);
	if (error == 0) {
		oflags = (ifr.ifr_flags & 0xffff)  | (ifr.ifr_flagshigh << 16);
	}

	if (flags < 0)
		oflags &= ~(-flags);
	else
		oflags |= flags;

	ifr.ifr_flags = oflags & 0xffff;
	ifr.ifr_flagshigh = oflags >> 16;

	error = ioctl(fd, SIOCSIFFLAGS, &ifr);
	if (error != 0)
		warn("ioctl SIOCSIFFLAGS");

	close(fd);
	return (error);
}

static int
ifaddr_add(const char *ifnam, struct sockaddr *sa, struct sockaddr *mask)
{
	int error;

	error = ifaddr_ad(SIOCAIFADDR, ifnam, sa, mask);
	if (error != 0)
		warn("ioctl SIOCAIFADDR");
	return (error);
}

static int
ifaddr_del(const char *ifnam, struct sockaddr *sa, struct sockaddr *mask)
{
	int error;

	error = ifaddr_ad(SIOCDIFADDR, ifnam, sa, mask);
	if (error != 0)
		warn("ioctl SIOCDIFADDR");
	return (error);
}

static int
set_nameservers(struct ctx *ctx, const char *respath, int ns, ...)
{
	int i, n, fd;
	FILE *fp;
	char *p;
	va_list ap;
	struct stat sb;
	char buf[512];
	
	if (ctx->ns != NULL) {
		for (i = 0; ctx->ns[i] != NULL; i++) {
			free(ctx->ns[i]);
		}
		free(ctx->ns);
		ctx->ns = NULL;
	}

	fd = open(respath, O_RDWR | O_CREAT | O_NOFOLLOW, 0666);
	if (fd < 0)
		return (-1);

	if (ns == 0) {
		/* Attempt to restore old resolv.conf */
		if (ctx->resolv != NULL) {
			ftruncate(fd, 0);
			lseek(fd, 0, SEEK_SET);
			write(fd, ctx->resolv, ctx->resolv_sz);
			free(ctx->resolv);
			ctx->resolv = NULL;
			ctx->resolv_sz = 0;
		}
		close(fd);
		return (0);
	}


	ctx->ns = malloc(sizeof(char *) * (ns + 1));
	if (ctx->ns == NULL) {
		close(fd);
		return (-1);
	}

	va_start(ap, ns);
	for (i = 0; i < ns; i++) {
		p = va_arg(ap, char *);
		ctx->ns[i] = strdup(p);
	}
	ctx->ns[i] = NULL;
	va_end(ap);

	/* Attempt to backup the old resolv.conf */
	if (ctx->resolv == NULL) {
		i = fstat(fd, &sb);
		if (i == 0 && sb.st_size != 0) {
			ctx->resolv_sz = sb.st_size;
			ctx->resolv = malloc(sb.st_size);
			if (ctx->resolv != NULL) {
				n = read(fd, ctx->resolv, sb.st_size);
				if (n != sb.st_size) {
					free(ctx->resolv);
					ctx->resolv = NULL;
				}
			}
		}
	}


	ftruncate(fd, 0);
	lseek(fd, 0, SEEK_SET);
	fp = fdopen(fd, "w");

	/*
	 * Write back everything other than nameserver entries to the
	 * new resolv.conf
	 */
	if (ctx->resolv != NULL) {
		p = ctx->resolv;
		while ((i = readline_buf(p, ctx->resolv + ctx->resolv_sz, buf,
		    sizeof(buf))) > 0) {
			p += i;
			if (strncasecmp(buf, "nameserver", 10) == 0)
				continue;
			fprintf(fp, "%s", buf);
		}
	}

	for (i = 0; ctx->ns[i] != NULL; i++) {
		fprintf(fp, "nameserver %s\n", ctx->ns[i]);
	}
	fclose(fp);
	return (0);
}

/* Read a \n-terminated line from buffer */
static int
readline_buf(const char *s, const char *e, char *buf, size_t bufsz)
{
	int pos = 0;
	char *p = buf;

	for (; s < e; s++) {
		*p = *s;
		pos++;
		if (pos >= (bufsz - 1))
			break;
		if (*p++ == '\n')
			break;
	}
	*p = '\0';
	return (pos);
}

/* Read a \n-terminated line from file */
static int
readline(int fd, char *buf, size_t bufsz)
{
	int n = 0, pos = 0;
	char *p = buf;

	for (;;) {
		n = read(fd, p, 1);
		if (n <= 0)
			break;
		pos++;
		if (pos >= (bufsz - 1))
			break;
		if (*p++ == '\n')
			break;
	}
	*p = '\0';
	return (n <= 0 ? n : pos);
}

/*
 * Synchronous AT command
 */
static int
at_cmd(struct ctx *ctx, const char *resp, resp_cb cb, resp_arg *ra, const char *cf, ...)
{
	char buf[512];
	char cmd[64];
	size_t l;
	int n, error, retval = 0;
	va_list ap;
	fd_set set;
	char *p;

	va_start(ap, cf);
	vsnprintf(cmd, sizeof(cmd), cf, ap);
	va_end(ap);

#ifdef DEBUG
	fprintf(stderr, "SYNC_CMD: %s", cmd);
#endif

	l = strlen(cmd);
	n = write(ctx->fd, cmd, l);
	if (n <= 0)
		return (-1);

	if (resp != NULL) {
		l = strlen(resp);
#ifdef DEBUG
		fprintf(stderr, "SYNC_EXP: %s (%zu)\n", resp, l);
#endif
	}

	for (;;) {
		bzero(buf, sizeof(buf));

		FD_ZERO(&set);
		watchdog_reset(ctx, 5);
		do {
			FD_SET(ctx->fd, &set);
			error = select(ctx->fd + 1, &set, NULL, NULL, NULL);
			if (ctx->flags & FLG_WDEXP) {
				watchdog_disable(ctx);
				return (-2);
			}
		} while (error <= 0 && errno == EINTR);
		watchdog_disable(ctx);

		if (error <= 0) {
			retval = -2;
			break;
		}

		n = readline(ctx->fd, buf, sizeof(buf));	
		if (n <= 0) {
			retval = -2;
			break;
		}
		
		if (strcmp(buf, "\r\n") == 0 || strcmp(buf, "\n") == 0)
			continue;

		if ((p = strchr(buf, '\r')) != NULL)
			*p = '\0';
		else if ((p = strchr(buf, '\n')) != NULL)
			*p = '\0';
#ifdef DEBUG
		fprintf(stderr, "SYNC_RESP: %s\n", buf);
#endif

		/* Skip local echo */
		if (strncasecmp(cmd, buf, strlen(buf)) == 0)
			continue;

		if (cb != NULL)
			cb(ra, cmd, buf);

		if (strncmp(buf, "OK", 2) == 0) {
			retval = retval ? retval : 0;
			break;
		} else if (strstr(buf, "ERROR") != NULL) {
			retval = -1;
			break;
		}
		if (resp != NULL)
			retval = strncmp(buf, resp, l);
	}
#ifdef DEBUG
	fprintf(stderr, "SYNC_RETVAL=%d\n", retval);
#endif
	return (retval);
}

static int
at_cmd_async(int fd, const char *cf, ...)
{
	size_t l;
	va_list ap;
	char cmd[64];

	va_start(ap, cf);
	vsnprintf(cmd, sizeof(cmd), cf, ap);
	va_end(ap);

#ifdef DEBUG
	fprintf(stderr, "CMD: %s", cmd);
#endif
	l = strlen(cmd);
	return (write(fd, cmd, l));
}

static void
saveresp(resp_arg *ra, const char *cmd, const char *resp)
{
	char **buf;
	int i = ra->val[1].int32;

#ifdef DEBUG
	fprintf(stderr, "Save '%s'\n", resp);
#endif

	buf = realloc(ra->val[0].ptr, sizeof(char *) * (i + 1));
	if (buf == NULL)
		return;

	buf[i] = strdup(resp);

	ra->val[0].ptr = buf;
	ra->val[1].int32 = i + 1;
}

static void
freeresp(resp_arg *ra)
{
	char **buf;
	int i;

	buf = ra->val[0].ptr;
	for (i = 0; i < ra->val[1].int32; i++) {
		free(buf[i]);
	}
	free(buf);
}

static void
at_async_creg(void *arg, const char *resp)
{
	struct ctx *ctx = arg;
	int n, reg;

	n = sscanf(resp, "+CREG: %*d,%d", &reg);
	if (n != 1) {
		n = sscanf(resp, "+CREG: %d", &reg);
		if (n != 1)
			return;
	}

	if (ctx->con_net_stat != 1 && ctx->con_net_stat != 5) {
		tmr_add(&timers, 1, 1, tmr_creg, ctx);
	}
	else {
		tmr_add(&timers, 1, 30, tmr_creg, ctx);
	}

	if (ctx->con_net_stat == reg)
		return;

	ctx->con_net_stat = reg;
	at_cmd_async(ctx->fd, "AT+COPS?\r\n");
}

static void
at_async_cgreg(void *arg, const char *resp)
{
	struct ctx *ctx = arg;
	int n, reg;

	n = sscanf(resp, "+CGREG: %*d,%d", &reg);
	if (n != 1) {
		n = sscanf(resp, "+CGREG: %d", &reg);
		if (n != 1)
			return;
	}

	if (ctx->con_net_stat != 1 && ctx->con_net_stat != 5) {
		tmr_add(&timers, 1, 1, tmr_cgreg, ctx);
	}
	else {
		tmr_add(&timers, 1, 30, tmr_cgreg, ctx);
	}

	if (ctx->con_net_stat == reg)
		return;

	ctx->con_net_stat = reg;
	at_cmd_async(ctx->fd, "AT+COPS?\r\n");
}


static void
at_async_cops(void *arg, const char *resp)
{
	struct ctx *ctx = arg;
	int n, at;
	char opr[64];

	n = sscanf(resp, "+COPS: %*d,%*d,\"%[^\"]\",%d",
	    opr, &at);
	if (n != 2)
		return;

	if (ctx->con_oper != NULL) {
		if (ctx->con_net_type == at &&
		    strcasecmp(opr, ctx->con_oper) == 0)
			return;
		free(ctx->con_oper);
	}

	ctx->con_oper = strdup(opr);
	ctx->con_net_type = at;

	if (ctx->con_net_stat == 1 || ctx->con_net_stat == 5) {
		logger(LOG_NOTICE, "%s to \"%s\" (%s)",
		    network_reg_status[ctx->con_net_stat],
		    ctx->con_oper, network_access_type[ctx->con_net_type]);
		if (ctx->con_status != 1) {
			at_cmd_async(ctx->fd, "AT_OWANCALL=%d,1,1\r\n",
			    ctx->pdp_ctx);
		}
	}
	else {
		logger(LOG_NOTICE, "%s (%s)",
		    network_reg_status[ctx->con_net_stat],
		    network_access_type[ctx->con_net_type]);
	}
}

/*
 * Signal strength for pretty console output
 *
 * From 3GPP TS 27.007 V8.3.0, Section 8.5
 * 0 = -113 dBm or less
 * 1  = -111 dBm
 * 2...30 = -109...-53 dBm
 * 31 = -51 dBm or greater
 *
 * So, dbm = (rssi * 2) - 113
*/
static void
at_async_csq(void *arg, const char *resp)
{
	struct ctx *ctx = arg;
	int n, rssi;

	n = sscanf(resp, "+CSQ: %d,%*d", &rssi);
	if (n != 1)
		return;
	if (rssi == 99)
		ctx->dbm = 0;
	else {
		ctx->dbm = (rssi * 2) - 113;
		tmr_add(&timers, 1, 15, tmr_status, ctx);
	}

	ctx->flags |= FLG_NEWDATA;
}

static void
at_async_owancall(void *arg, const char *resp)
{
	struct ctx *ctx = arg;
	int n, i;

	n = sscanf(resp, "_OWANCALL: %*d,%d", &i);
	if (n != 1)
		return;

	if (i == ctx->con_status)
		return;

	at_cmd_async(ctx->fd, "AT_OWANDATA=%d\r\n", ctx->pdp_ctx);

	ctx->con_status = i;
	if (ctx->con_status == 1) {
		logger(LOG_NOTICE, "Connected to \"%s\" (%s), %s",
		    ctx->con_oper, ctx->con_apn,
		    network_access_type[ctx->con_net_type]);
	}
	else {
		logger(LOG_NOTICE, "Disconnected from \"%s\" (%s)",
		    ctx->con_oper, ctx->con_apn);
	}
}

static void
at_async_owandata(void *arg, const char *resp)
{
	struct ctx *ctx = arg;
	char ip[40], ns1[40], ns2[40];
	int n, error, rs;
	struct ifaddrs *ifap, *ifa;
	struct sockaddr_in sin, mask;
	struct sockaddr_dl sdl;
	struct {
		struct rt_msghdr rtm;
		char buf[512];
	} r;
	char *cp = r.buf;

	n = sscanf(resp, "_OWANDATA: %*d, %[^,], %*[^,], %[^,], %[^,]",
	    ip, ns1, ns2);
	if (n != 3)
		return;

	/* XXX: AF_INET assumption */

	logger(LOG_NOTICE, "IP address: %s, Nameservers: %s, %s", ip, ns1, ns2);

	sin.sin_len = mask.sin_len = sizeof(struct sockaddr_in);
	memset(&mask.sin_addr.s_addr, 0xff, sizeof(mask.sin_addr.s_addr));
	sin.sin_family = mask.sin_family = AF_INET;

	if (ctx->flags & IPASSIGNED) {
		memcpy(&sin.sin_addr.s_addr, &ctx->ip.s_addr,
		    sizeof(sin.sin_addr.s_addr));
		ifaddr_del(ctx->ifnam, (struct sockaddr *)&sin,
		    (struct sockaddr *)&mask);
	}
	inet_pton(AF_INET, ip, &ctx->ip.s_addr);
	memcpy(&sin.sin_addr.s_addr, &ctx->ip.s_addr,
	    sizeof(sin.sin_addr.s_addr));

	error = ifaddr_add(ctx->ifnam, (struct sockaddr *)&sin,
	    (struct sockaddr *)&mask);
	if (error != 0) {
		logger(LOG_ERR, "failed to set ip-address");
		return;
	}

	if_ifup(ctx->ifnam);

	ctx->flags |= IPASSIGNED;

	set_nameservers(ctx, ctx->resolv_path, 0);
	error = set_nameservers(ctx, ctx->resolv_path, 2, ns1, ns2);
	if (error != 0) {
		logger(LOG_ERR, "failed to set nameservers");
	}

	error = getifaddrs(&ifap);
	if (error != 0) {
		logger(LOG_ERR, "getifaddrs: %s", strerror(errno));
		return;
	}

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		if (strcmp(ctx->ifnam, ifa->ifa_name) == 0) {
			memcpy(&sdl, (struct sockaddr_dl *)ifa->ifa_addr,
			    sizeof(struct sockaddr_dl));
			break;	
		}
	}
	if (ifa == NULL)
		return;

	rs = socket(PF_ROUTE, SOCK_RAW, 0);
	if (rs < 0) {
		logger(LOG_ERR, "socket PF_ROUTE: %s", strerror(errno));
		return;
	}

	memset(&r, 0, sizeof(r));

	r.rtm.rtm_version = RTM_VERSION;
	r.rtm.rtm_type = RTM_ADD;
	r.rtm.rtm_flags = RTF_UP | RTF_STATIC;
	r.rtm.rtm_pid = getpid();
	memset(&sin, 0, sizeof(struct sockaddr_in));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(struct sockaddr_in);

	memcpy(cp, &sin, sin.sin_len);
	cp += SA_SIZE(&sin);
	memcpy(cp, &sdl, sdl.sdl_len);
	cp += SA_SIZE(&sdl);
	memcpy(cp, &sin, sin.sin_len);
	r.rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	r.rtm.rtm_msglen = sizeof(r);

	n = write(rs, &r, r.rtm.rtm_msglen);
	if (n != r.rtm.rtm_msglen) {
		r.rtm.rtm_type = RTM_DELETE;
		n = write(rs, &r, r.rtm.rtm_msglen);
		r.rtm.rtm_type = RTM_ADD;
		n = write(rs, &r, r.rtm.rtm_msglen);
	}

	if (n != r.rtm.rtm_msglen) {
		logger(LOG_ERR, "failed to set default route: %s",
		    strerror(errno));
	}
	close(rs);

	/* Delayed daemonization */
	if ((ctx->flags & FLG_DELAYED) && !(ctx->flags & FLG_NODAEMON))
		daemonize(ctx);
}

static int
at_async(struct ctx *ctx, void *arg)
{
	int n, i;
	size_t l;
	char buf[512];

	watchdog_reset(ctx, 15);

	bzero(buf, sizeof(buf));	
	n = readline(ctx->fd, buf, sizeof(buf));	
	if (n <= 0)
		return (n <= 0 ? -1 : 0);

#ifdef DEBUG
	fprintf(stderr, "AT_ASYNC_RESP: %s", buf);
#endif
	for (i = 0; async_cmd[i].cmd != NULL; i++) {
		l = strlen(async_cmd[i].cmd);
		if (strncmp(buf, async_cmd[i].cmd, l) == 0) {
			async_cmd[i].func(arg, buf);
		}
	}
	return (0);
}

static const char *port_type_list[] = {
	"control", "application", "application2", NULL	
};

/*
 * Attempts to find a list of control tty for the interface
 * FreeBSD attaches USB devices per interface so we have to go through
 * hoops to find which ttys that belong to our network interface.
 */
static char **
get_tty(struct ctx *ctx)
{
	char buf[64], data[128];
	int error, i, usbport, usbport0, list_size = 0;
	char **list = NULL;
	size_t len;
	const char **p, *q;
	
	/*
	 * Look for the network interface first
	 */
	for (i = 0; ; i++) {
		/* Check if we still have uhso nodes to check */
		snprintf(buf, 64, SYSCTL_TEST, i);
		len = 127;
		error = sysctlbyname(buf, data, &len, NULL, 0);
		data[len] = '\0';
#ifdef DEBUG
		fprintf(stderr, "sysctl %s returned(%d): %s\n",
		    buf, error, error == 0 ? data : "FAILED");
#endif
		if (error < 0 || strcasecmp(data, "uhso") != 0)
			return NULL;

		/* Check if this node contains the network interface we want */
		snprintf(buf, 64, SYSCTL_NETIF, i);
		len = 127;
		error = sysctlbyname(buf, data, &len, NULL, 0);
		data[len] = '\0';
#ifdef DEBUG
		fprintf(stderr, "sysctl %s returned(%d): %s\n",
		    buf, error, error == 0 ? data : "FAILED");
#endif
		if (error == 0 && strcasecmp(data, ctx->ifnam) == 0)
			break;
	}

	/* Figure out the USB port location */
	snprintf(buf, 64, SYSCTL_LOCATION, i);
	len = 127;
	error = sysctlbyname(buf, data, &len, NULL, 0);
	data[len] = '\0';
#ifdef DEBUG
	fprintf(stderr, "sysctl %s returned(%d): %s\n",
	    buf, error, error == 0 ? data : "FAILED");
#endif
	if (error != 0)
		return (NULL);

	q = strstr(data, "port=");
	if (q != NULL) {
		error = sscanf(q, " port=%d", &usbport);
		if (error != 1) {
#ifdef DEBUG
			fprintf(stderr, "failed to read usb port location from '%s'\n", data);
#endif
			return (NULL);
		}
	} else {
#ifdef DEBUG
			fprintf(stderr, "failed to parse location '%s'\n", data);
#endif
			return (NULL);
	}
#ifdef DEBUG
	fprintf(stderr, "USB port location=%d\n", usbport);
#endif

	/*
	 * Now go through it all again but only look at those matching the
	 * usb port location we found.
	 */
	for (i = 0; ; i++) {
		snprintf(buf, 64, SYSCTL_LOCATION, i);
		len = 127;
		memset(&data, 0, sizeof(data));
		error = sysctlbyname(buf, data, &len, NULL, 0);
		if (error != 0)
			break;
		data[len] = '\0';
		q = strstr(data, "port=");
		if (q == NULL)
			continue;
		sscanf(q, " port=%d", &usbport0);
		if (usbport != usbport0)
			continue;

		/* Try to add ports */	
		for (p = port_type_list; *p != NULL; p++) {
			snprintf(buf, 64, SYSCTL_NAME_TTY, i, *p);
			len = 127;
			memset(&data, 0, sizeof(data));
			error = sysctlbyname(buf, data, &len, NULL, 0);
			data[len] = '\0';
#ifdef DEBUG
			fprintf(stderr, "sysctl %s returned(%d): %s\n",
			    buf, error, error == 0 ? data : "FAILED");
#endif
			if (error == 0) {
				list = realloc(list, (list_size + 1) * sizeof(char *));
				list[list_size] = malloc(strlen(data) + strlen(TTY_NAME));
		    		sprintf(list[list_size], TTY_NAME, data);
		    		list_size++;
			}
		}
	}
	list = realloc(list, (list_size + 1) * sizeof(char *));
	list[list_size] = NULL;
	return (list);
}

static int
do_connect(struct ctx *ctx, const char *tty)
{
	int i, error, needcfg;
	resp_arg ra;
	struct termios t;
	char **buf;

#ifdef DEBUG
	fprintf(stderr, "Attempting to open %s\n", tty);
#endif

	ctx->fd = open(tty, O_RDWR);
	if (ctx->fd < 0) {
#ifdef DEBUG
		fprintf(stderr, "Failed to open %s\n", tty);
#endif
		return (-1);
	}

	tcgetattr(ctx->fd, &t);
	t.c_oflag = 0;
	t.c_iflag = 0;
	t.c_cflag = CLOCAL | CREAD;
	t.c_lflag = 0;
	tcsetattr(ctx->fd, TCSAFLUSH, &t);

	error = at_cmd(ctx, NULL, NULL, NULL, "AT\r\n");
	if (error == -2) {
		warnx("failed to read from device %s", tty);
		return (-1);
	}

	/* Check for PIN */
	error = at_cmd(ctx, "+CPIN: READY", NULL, NULL, "AT+CPIN?\r\n");
	if (error != 0) {
		ra.val[0].ptr = NULL;
		ra.val[1].int32 = 0;
		error = at_cmd(ctx, "+CME ERROR", saveresp, &ra, "AT+CPIN?\r\n");
		if (ra.val[1].int32 > 0) {
			char *p;

			buf = ra.val[0].ptr;
			if (strstr(buf[0], "+CME ERROR:") != NULL) {
				buf[0] += 12;
				errx(1, "%s", buf[0]);
			}
			freeresp(&ra);
		} else
			freeresp(&ra);

		if (ctx->pin == NULL) {
			errx(1, "device requires PIN");
		}

		error = at_cmd(ctx, NULL, NULL, NULL, "AT+CPIN=\"%s\"\r\n",
		    ctx->pin);
		if (error != 0) {
			errx(1, "wrong PIN");
		}
	}

	/*
	 * Check if a PDP context has been configured and configure one
	 * if needed.
	 */
	ra.val[0].ptr = NULL;
	ra.val[1].int32 = 0;
	error = at_cmd(ctx, "+CGDCONT", saveresp, &ra, "AT+CGDCONT?\r\n");
	buf = ra.val[0].ptr;
	needcfg = 1;
	for (i = 0; i < ra.val[1].int32; i++) {
		char apn[256];
		int cid;
		error = sscanf(buf[i], "+CGDCONT: %d,\"%*[^\"]\",\"%[^\"]\"",
		    &cid, apn);
		if (error != 2) {
			free(buf[i]);
			continue;
		}

		if (cid == ctx->pdp_ctx) {
			ctx->con_apn = strdup(apn);
			if (ctx->pdp_apn != NULL) {
				if (strcmp(apn, ctx->pdp_apn) == 0)
					needcfg = 0;
			}
			else {
				needcfg = 0;
			}
		}
		free(buf[i]);
	}
	free(buf);

	if (needcfg) {
		if (ctx->pdp_apn == NULL)
			errx(1, "device is not configured and no APN given");

		error = at_cmd(ctx, NULL, NULL, NULL,
		   "AT+CGDCONT=%d,,\"%s\"\r\n", ctx->pdp_ctx, ctx->pdp_apn);
		if (error != 0) {
			errx(1, "failed to configure device");
		}
		ctx->con_apn = strdup(ctx->pdp_apn);
	}

	if (ctx->pdp_user != NULL || ctx->pdp_pwd != NULL) {
		at_cmd(ctx, NULL, NULL, NULL,
		    "AT$QCPDPP=%d,1,\"%s\",\"%s\"\r\n", ctx->pdp_ctx,
		    (ctx->pdp_user != NULL) ? ctx->pdp_user : "",
		    (ctx->pdp_pwd != NULL) ? ctx->pdp_pwd : "");
	}

	error = at_cmd(ctx, NULL, NULL, NULL, "AT_OWANCALL=%d,0,0\r\n",
	    ctx->pdp_ctx);
	if (error != 0)
		return (-1);

	at_cmd_async(ctx->fd, "AT+CGREG?\r\n");
	at_cmd_async(ctx->fd, "AT+CREG?\r\n");

	tmr_add(&timers, 1, 5, tmr_status, ctx);
	return (0);
}

static void
do_disconnect(struct ctx *ctx)
{
	struct sockaddr_in sin, mask;

	/* Disconnect */
	at_cmd(ctx, NULL, NULL, NULL, "AT_OWANCALL=%d,0,0\r\n",
	    ctx->pdp_ctx);
	close(ctx->fd);

	/* Remove ip-address from interface */
	if (ctx->flags & IPASSIGNED) {
		sin.sin_len = mask.sin_len = sizeof(struct sockaddr_in);
		memset(&mask.sin_addr.s_addr, 0xff,
		    sizeof(mask.sin_addr.s_addr));
		sin.sin_family = mask.sin_family = AF_INET;
		memcpy(&sin.sin_addr.s_addr, &ctx->ip.s_addr,
		    sizeof(sin.sin_addr.s_addr));
		ifaddr_del(ctx->ifnam, (struct sockaddr *)&sin,
		    (struct sockaddr *)&mask);

		if_ifdown(ctx->ifnam);
		ctx->flags &= ~IPASSIGNED;
	}

	/* Attempt to reset resolv.conf */
	set_nameservers(ctx, ctx->resolv_path, 0);
}

static void
daemonize(struct ctx *ctx)
{
	struct pidfh *pfh;
	pid_t opid;

	snprintf(ctx->pidfile, 127, PIDFILE, ctx->ifnam);

	pfh = pidfile_open(ctx->pidfile, 0600, &opid);
	if (pfh == NULL) {
		warn("Cannot create pidfile %s", ctx->pidfile);
		return;
	}

	if (daemon(0, 0) == -1) {
		warn("Cannot daemonize");
		pidfile_remove(pfh);
		return;
	}
	
	pidfile_write(pfh);
	ctx->pfh = pfh;
	ctx->flags |= FLG_DAEMON;

	snprintf(syslog_title, 63, "%s:%s", getprogname(), ctx->ifnam);
	openlog(syslog_title, LOG_PID, LOG_USER);
	syslog_open = 1;
}

static void
send_disconnect(const char *ifnam)
{
	char pidfile[128];
	FILE *fp;
	pid_t pid;
	int n;

	snprintf(pidfile, 127, PIDFILE, ifnam);
	fp = fopen(pidfile, "r");
	if (fp == NULL) {
		warn("Cannot open %s", pidfile);
		return;
	}

	n = fscanf(fp, "%d", &pid);
	fclose(fp);
	if (n != 1) {
		warnx("unable to read daemon pid");
		return;
	}
#ifdef DEBUG
	fprintf(stderr, "Sending SIGTERM to %d\n", pid);
#endif
	kill(pid, SIGTERM);
}

static void
usage(const char *exec)
{

	printf("usage %s [-b] [-n] [-a apn] [-c cid] [-p pin] [-u username] "
	    "[-k password] [-r resolvpath] [-f tty] interface\n", exec);
	printf("usage %s -d interface\n", exec);
}

enum {
	MODE_CONN,
	MODE_DISC
};

int
main(int argc, char *argv[])
{
	int ch, error, mode;
	const char *ifnam = NULL;
	char *tty = NULL;
	char **p, **tty_list;
	fd_set set;
	struct ctx ctx;
	struct itimerval it;

	TAILQ_INIT(&timers.head);
	timers.res = 1;

	ctx.pdp_ctx = 1;
	ctx.pdp_apn = ctx.pdp_user = ctx.pdp_pwd = NULL;
	ctx.pin = NULL;

	ctx.con_status = 0;
	ctx.con_apn = NULL;
	ctx.con_oper = NULL;
	ctx.con_net_stat = 0;
	ctx.con_net_type = -1;
	ctx.flags = 0;
	ctx.resolv_path = RESOLV_PATH;
	ctx.resolv = NULL;
	ctx.ns = NULL;
	ctx.dbm = 0;

	mode = MODE_CONN;
	ctx.flags |= FLG_DELAYED;

	while ((ch = getopt(argc, argv, "?ha:p:c:u:k:r:f:dbn")) != -1) {
		switch (ch) {
		case 'a':
			ctx.pdp_apn = argv[optind - 1];
			break;
		case 'c':
			ctx.pdp_ctx = strtol(argv[optind - 1], NULL, 10);
			if (ctx.pdp_ctx < 1) {
				warnx("Invalid context ID, defaulting to 1");
				ctx.pdp_ctx = 1;
			}
			break;
		case 'p':
			ctx.pin = argv[optind - 1];
			break;
		case 'u':
			ctx.pdp_user = argv[optind - 1];
			break;
		case 'k':
			ctx.pdp_pwd = argv[optind - 1];
			break;
		case 'r':
			ctx.resolv_path = argv[optind - 1];
			break;
		case 'd':
			mode = MODE_DISC;
			break;
		case 'b':
			ctx.flags &= ~FLG_DELAYED;
			break;
		case 'n':
			ctx.flags |= FLG_NODAEMON;
			break;
		case 'f':
			tty = argv[optind - 1];
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0]);
			exit(EXIT_SUCCESS);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		errx(1, "no interface given");

	ifnam = argv[argc - 1];
	ctx.ifnam = strdup(ifnam);

	switch (mode) {
	case MODE_DISC:
		printf("Disconnecting %s\n", ifnam);
		send_disconnect(ifnam);
		exit(EXIT_SUCCESS);
	default:
		break;
	}

	signal(SIGHUP, sig_handle);
	signal(SIGINT, sig_handle);
	signal(SIGQUIT, sig_handle);
	signal(SIGTERM, sig_handle);
	signal(SIGALRM, sig_handle);

	it.it_interval.tv_sec = 1;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = 1;
	it.it_value.tv_usec = 0;
	error = setitimer(ITIMER_REAL, &it, NULL);
	if (error != 0)
		errx(1, "setitimer");

	tmr_add(&timers, 1, 5, &tmr_watchdog, &ctx);
	watchdog_reset(&ctx, 15);
	
	if (tty != NULL) {
		error = do_connect(&ctx, tty);
		if (error != 0)
			errx(1, "Failed to open %s", tty);
	}
	else {
		tty_list = get_tty(&ctx);
		if (tty_list == NULL)
			errx(1, "%s does not appear to be a uhso device", ifnam);
#ifdef DEBUG
		if (tty_list == NULL) {
			fprintf(stderr, "get_tty returned empty list\n");
		} else {
			fprintf(stderr, "tty list:\n");
			for (p = tty_list; *p != NULL; p++) {
				fprintf(stderr, "\t %s\n", *p);
			}
		}
#endif
		for (p = tty_list; *p != NULL; p++) {
			error = do_connect(&ctx, *p);
			if (error == 0) {
				tty = *p;
				break;
			}
		}
		if (*p == NULL)
			errx(1, "Failed to obtain a control port, "
			    "try specifying one manually");
	}

	if (!(ctx.flags & FLG_DELAYED) && !(ctx.flags & FLG_NODAEMON))
		daemonize(&ctx);


	FD_ZERO(&set);
	FD_SET(ctx.fd, &set);
	for (;;) {

		watchdog_disable(&ctx);
		error = select(ctx.fd + 1, &set, NULL, NULL, NULL);
		if (error <= 0) {
			if (running && errno == EINTR)
				continue;
			if (ctx.flags & FLG_WDEXP) {
				ctx.flags &= ~FLG_WDEXP;
				watchdog_reset(&ctx, 5);
				do_disconnect(&ctx);
				watchdog_reset(&ctx, 15);
				do_connect(&ctx, tty);
				running = 1;
				continue;
			}

			break;
		}

		if (FD_ISSET(ctx.fd, &set)) {
			watchdog_reset(&ctx, 15);
			error = at_async(&ctx, &ctx);
			if (error != 0)
				break;
		}
		FD_SET(ctx.fd, &set);

		if (!(ctx.flags & FLG_DAEMON) && (ctx.flags & IPASSIGNED)) {
			printf("Status: %s (%s)",
			    ctx.con_status ? "connected" : "disconnected",
			    network_access_type[ctx.con_net_type]);
			if (ctx.dbm < 0)
				printf(", signal: %d dBm", ctx.dbm);
			printf("\t\t\t\r");
			fflush(stdout);
		}
	}
	if (!(ctx.flags & FLG_DAEMON) && (ctx.flags & IPASSIGNED))
		printf("\n");

	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGALRM, SIG_IGN);

	do_disconnect(&ctx);

	if (ctx.flags & FLG_DAEMON) {
		pidfile_remove(ctx.pfh);
		if (syslog_open)
			closelog();
	}

	return (0);
}
