/*	$OpenBSD: spamd.c,v 1.163 2024/05/09 08:35:03 florian Exp $	*/

/*
 * Copyright (c) 2015 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2002-2007 Bob Beck.  All rights reserved.
 * Copyright (c) 2002 Theo de Raadt.  All rights reserved.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <tls.h>

#include <netdb.h>

#include "sdl.h"
#include "grey.h"
#include "sync.h"

struct con {
	struct pollfd *pfd;
	int state;
	int laststate;
	int af;
	int il;
	struct sockaddr_storage ss;
	void *ia;
	char addr[32];
	char caddr[32];
	char helo[MAX_MAIL], mail[MAX_MAIL], rcpt[MAX_MAIL];
	struct sdlist **blacklists;
	struct tls *cctx;

	/*
	 * we will do stuttering by changing these to time_t's of
	 * now + n, and only advancing when the time is in the past/now
	 */
	time_t r;
	time_t w;
	time_t s;

	char ibuf[8192];
	char *ip;
	char rend[5];	/* any chars in here causes input termination */

	char *obuf;
	char *lists;
	size_t osize;
	char *op;
	int ol;
	int data_lines;
	int data_body;
	int stutter;
	int badcmd;
	int sr;
	int tlsaction;
} *con;

#define	SPAMD_TLS_ACT_NONE		0
#define	SPAMD_TLS_ACT_READ_POLLIN	1
#define	SPAMD_TLS_ACT_READ_POLLOUT	2
#define	SPAMD_TLS_ACT_WRITE_POLLIN	3
#define	SPAMD_TLS_ACT_WRITE_POLLOUT	4

#define	SPAMD_USER			"_spamd"

void     usage(void);
char    *grow_obuf(struct con *, int);
int      parse_configline(char *);
void     parse_configs(void);
void     do_config(void);
int      append_error_string (struct con *, size_t, char *, int, void *);
void     doreply(struct con *);
void     setlog(char *, size_t, char *);
void     initcon(struct con *, int, struct sockaddr *);
void     closecon(struct con *);
int      match(const char *, const char *);
void     nextstate(struct con *);
void     handler(struct con *);
void     handlew(struct con *, int one);
char    *loglists(struct con *);
void     getcaddr(struct con *);
void     gethelo(char *, size_t, char *);
int      read_configline(FILE *);
void	 spamd_tls_init(void);
void	 check_spamd_db(void);
void	 blackcheck(int);

char hostname[HOST_NAME_MAX+1];
struct syslog_data sdata = SYSLOG_DATA_INIT;
char *nreply = "450";
char *spamd = "spamd IP-based SPAM blocker";
int greyback[2];
int greypipe[2];
int trappipe[2];
FILE *grey;
FILE *trapcfg;
time_t passtime = PASSTIME;
time_t greyexp = GREYEXP;
time_t whiteexp = WHITEEXP;
time_t trapexp = TRAPEXP;
struct passwd *pw;
pid_t jail_pid = -1;
u_short cfg_port;
u_short sync_port;
struct tls_config *tlscfg;
struct tls *tlsctx;
char 	*tlskeyfile = NULL;
char 	*tlscertfile = NULL;

extern struct sdlist *blacklists;
extern int pfdev;
extern char *low_prio_mx_ip;

time_t slowdowntill;

int conffd = -1;
int trapfd = -1;
char *cb;
size_t cbs, cbu;

time_t t;

#define MAXCON 800
int maxfiles;
int maxcon = MAXCON;
int maxblack = MAXCON;
int blackcount;
int clients;
int debug;
int greylist = 1;
int grey_stutter = 10;
int verbose;
int stutter = 1;
int window;
int syncrecv;
int syncsend;
#define MAXTIME 400

#define MAXIMUM(a,b) (((a)>(b))?(a):(b))

void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-45bdv] [-B maxblack] [-C file] [-c maxcon] "
	    "[-G passtime:greyexp:whiteexp]\n"
	    "\t[-h hostname] [-K file] [-l address] [-M address] [-n name]\n"
	    "\t[-p port] [-S secs] [-s secs] "
	    "[-w window] [-Y synctarget] [-y synclisten]\n",
	    __progname);

	exit(1);
}

char *
grow_obuf(struct con *cp, int off)
{
	char *tmp;

	tmp = realloc(cp->obuf, cp->osize + 8192);
	if (tmp == NULL) {
		free(cp->obuf);
		cp->obuf = NULL;
		cp->osize = 0;
		return (NULL);
	} else {
		cp->osize += 8192;
		cp->obuf = tmp;
		return (cp->obuf + off);
	}
}

int
parse_configline(char *line)
{
	char *cp, prev, *name, *msg, *tmp;
	char **v4 = NULL, **v6 = NULL;
	const char *errstr;
	u_int nv4 = 0, nv6 = 0;
	int mdone = 0;
	sa_family_t af;

	name = line;

	for (cp = name; *cp && *cp != ';'; cp++)
		;
	if (*cp != ';')
		goto parse_error;
	*cp++ = '\0';
	if (!*cp) {
		sdl_del(name);
		return (0);
	}
	msg = cp;
	if (*cp++ != '"')
		goto parse_error;
	prev = '\0';
	for (; !mdone; cp++) {
		switch (*cp) {
		case '\\':
			if (!prev)
				prev = *cp;
			else
				prev = '\0';
			break;
		case '"':
			if (prev != '\\') {
				cp++;
				if (*cp == ';') {
					mdone = 1;
					*cp = '\0';
				} else {
					if (debug > 0)
						printf("bad message: %s\n", msg);
					goto parse_error;
				}
			}
			break;
		case '\0':
			if (debug > 0)
				printf("bad message: %s\n", msg);
			goto parse_error;
		default:
			prev = '\0';
			break;
		}
	}

	while ((tmp = strsep(&cp, ";")) != NULL) {
		char **av;
		u_int au, ac;

		if (*tmp == '\0')
			continue;

		if (strncmp(tmp, "inet", 4) != 0)
			goto parse_error;
		switch (tmp[4]) {
		case '\0':
			af = AF_INET;
			break;
		case '6':
			if (tmp[5] == '\0') {
				af = AF_INET6;
				break;
			}
			/* FALLTHROUGH */
		default:
			if (debug > 0)
				printf("unsupported address family: %s\n", tmp);
			goto parse_error;
		}

		tmp = strsep(&cp, ";");
		if (tmp == NULL) {
			if (debug > 0)
				printf("missing address count\n");
			goto parse_error;
		}
		ac = strtonum(tmp, 0, UINT_MAX, &errstr);
		if (errstr != NULL) {
			if (debug > 0)
				printf("count \"%s\" is %s\n", tmp, errstr);
			goto parse_error;
		}

		av = reallocarray(NULL, ac, sizeof(char *));
		for (au = 0; au < ac; au++) {
			tmp = strsep(&cp, ";");
			if (tmp == NULL) {
				if (debug > 0)
					printf("expected %u addrs, got %u\n",
					    ac, au + 1);
				free(av);
				goto parse_error;
			}
			if (*tmp == '\0')
				continue;
			av[au] = tmp;
		}
		if (af == AF_INET) {
			if (v4 != NULL) {
				if (debug > 0)
					printf("duplicate inet\n");
				goto parse_error;
			}
			v4 = av;
			nv4 = ac;
		} else {
			if (v6 != NULL) {
				if (debug > 0)
					printf("duplicate inet6\n");
				goto parse_error;
			}
			v6 = av;
			nv6 = ac;
		}
	}
	if (nv4 == 0 && nv6 == 0) {
		if (debug > 0)
			printf("no addresses\n");
		goto parse_error;
	}
	sdl_add(name, msg, v4, nv4, v6, nv6);
	free(v4);
	free(v6);
	return (0);

parse_error:
	if (debug > 0)
		printf("bogus config line - need 'tag;message;af;count;a/m;a/m;a/m...'\n");
	free(v4);
	free(v6);
	return (-1);
}

void
parse_configs(void)
{
	char *start, *end;
	size_t i;

	/* We always leave an extra byte for the NUL. */
	cb[cbu++] = '\0';

	start = cb;
	end = start;
	for (i = 0; i < cbu; i++) {
		if (*end == '\n') {
			*end = '\0';
			if (end > start + 1)
				parse_configline(start);
			start = ++end;
		} else
			++end;
	}
	if (end > start + 1)
		parse_configline(start);
}

void
do_config(void)
{
	int n;

	if (debug > 0)
		printf("got configuration connection\n");

	/* Leave an extra byte for the terminating NUL. */
	if (cbu + 1 >= cbs) {
		char *tmp;

		tmp = realloc(cb, cbs + (1024 * 1024));
		if (tmp == NULL) {
			if (debug > 0)
				warn("realloc");
			goto configdone;
		}
		cbs += 1024 * 1024;
		cb = tmp;
	}

	n = read(conffd, cb + cbu, cbs - cbu);
	if (debug > 0)
		printf("read %d config bytes\n", n);
	if (n == 0) {
		if (cbu != 0)
			parse_configs();
		goto configdone;
	} else if (n == -1) {
		if (debug > 0)
			warn("read");
		goto configdone;
	} else
		cbu += n;
	return;

configdone:
	free(cb);
	cb = NULL;
	cbs = 0;
	cbu = 0;
	close(conffd);
	conffd = -1;
	slowdowntill = 0;
}

int
read_configline(FILE *config)
{
	char *buf;
	size_t len;

	if ((buf = fgetln(config, &len))) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else
			return (-1);	/* all valid lines end in \n */
		parse_configline(buf);
	} else {
		syslog_r(LOG_DEBUG, &sdata, "read_configline: fgetln (%m)");
		return (-1);
	}
	return (0);
}

void
spamd_tls_init(void)
{
	if (tlskeyfile == NULL && tlscertfile == NULL)
		return;
	if (tlskeyfile == NULL || tlscertfile == NULL)
		errx(1, "need key and certificate for TLS");

	if ((tlscfg = tls_config_new()) == NULL)
		errx(1, "failed to get tls config");
	if ((tlsctx = tls_server()) == NULL)
		errx(1, "failed to get tls server");

	if (tls_config_set_protocols(tlscfg, TLS_PROTOCOLS_ALL) != 0)
		errx(1, "failed to set tls protocols");

	/* might need user-specified ciphers, tls_config_set_ciphers */
	if (tls_config_set_ciphers(tlscfg, "all") != 0)
		errx(1, "failed to set tls ciphers");

	if (tls_config_set_cert_file(tlscfg, tlscertfile) == -1)
		errx(1, "unable to set TLS certificate file %s", tlscertfile);
	if (tls_config_set_key_file(tlscfg, tlskeyfile) == -1)
		errx(1, "unable to set TLS key file %s", tlskeyfile);
	if (tls_configure(tlsctx, tlscfg) != 0)
		errx(1, "failed to configure TLS - %s", tls_error(tlsctx));

	/* set hostname to cert's CN unless explicitly given? */
}

int
append_error_string(struct con *cp, size_t off, char *fmt, int af, void *ia)
{
	char sav = '\0';
	static int lastcont = 0;
	char *c = cp->obuf + off;
	char *s = fmt;
	size_t len = cp->osize - off;
	int i = 0;

	if (off == 0)
		lastcont = 0;

	if (lastcont != 0)
		cp->obuf[lastcont] = '-';
	snprintf(c, len, "%s ", nreply);
	i += strlen(c);
	lastcont = off + i - 1;
	if (*s == '"')
		s++;
	while (*s) {
		/*
		 * Make sure we at minimum, have room to add a
		 * format code (4 bytes), and a v6 address(39 bytes)
		 * and a byte saved in sav.
		 */
		if (i >= len - 46) {
			c = grow_obuf(cp, off);
			if (c == NULL)
				return (-1);
			len = cp->osize - (off + i);
		}

		if (c[i-1] == '\n') {
			if (lastcont != 0)
				cp->obuf[lastcont] = '-';
			snprintf(c + i, len, "%s ", nreply);
			i += strlen(c);
			lastcont = off + i - 1;
		}

		switch (*s) {
		case '\\':
		case '%':
			if (!sav)
				sav = *s;
			else {
				c[i++] = sav;
				sav = '\0';
				c[i] = '\0';
			}
			break;
		case '"':
		case 'A':
		case 'n':
			if (*(s+1) == '\0') {
				break;
			}
			if (sav == '\\' && *s == 'n') {
				c[i++] = '\n';
				sav = '\0';
				c[i] = '\0';
				break;
			} else if (sav == '\\' && *s == '"') {
				c[i++] = '"';
				sav = '\0';
				c[i] = '\0';
				break;
			} else if (sav == '%' && *s == 'A') {
				inet_ntop(af, ia, c + i, (len - i));
				i += strlen(c + i);
				sav = '\0';
				break;
			}
			/* FALLTHROUGH */
		default:
			if (sav)
				c[i++] = sav;
			c[i++] = *s;
			sav = '\0';
			c[i] = '\0';
			break;
		}
		s++;
	}
	return (i);
}

char *
loglists(struct con *cp)
{
	static char matchlists[80];
	struct sdlist **matches;
	int s = sizeof(matchlists) - 4;

	matchlists[0] = '\0';
	matches = cp->blacklists;
	if (matches == NULL)
		return (NULL);
	for (; *matches; matches++) {

		/* don't report an insane amount of lists in the logs.
		 * just truncate and indicate with ...
		 */
		if (strlen(matchlists) + strlen(matches[0]->tag) + 1 >= s)
			strlcat(matchlists, " ...", sizeof(matchlists));
		else {
			strlcat(matchlists, " ", s);
			strlcat(matchlists, matches[0]->tag, s);
		}
	}
	return matchlists;
}

void
doreply(struct con *cp)
{
	struct sdlist **matches;
	int off = 0;

	matches = cp->blacklists;
	if (matches == NULL)
		goto nomatch;
	for (; *matches; matches++) {
		int used = 0;
		int left = cp->osize - off;

		used = append_error_string(cp, off, matches[0]->string,
		    cp->af, cp->ia);
		if (used == -1)
			goto bad;
		off += used;
		left -= used;
		if (cp->obuf[off - 1] != '\n') {
			if (left < 1) {
				if (grow_obuf(cp, off) == NULL)
					goto bad;
			}
			cp->obuf[off++] = '\n';
			cp->obuf[off] = '\0';
		}
	}
	return;
nomatch:
	/* No match. give generic reply */
	free(cp->obuf);
	if (cp->blacklists != NULL)
		cp->osize = asprintf(&cp->obuf,
		    "%s-Sorry %s\n"
		    "%s-You are trying to send mail from an address "
		    "listed by one\n"
		    "%s or more IP-based registries as being a SPAM source.\n",
		    nreply, cp->addr, nreply, nreply);
	else
		cp->osize = asprintf(&cp->obuf,
		    "451 Temporary failure, please try again later.\r\n");
	if (cp->osize == -1)
		cp->obuf = NULL;
	cp->osize++; /* size includes the NUL (also changes -1 to 0) */
	return;
bad:
	if (cp->obuf != NULL) {
		free(cp->obuf);
		cp->obuf = NULL;
		cp->osize = 0;
	}
}

void
setlog(char *p, size_t len, char *f)
{
	char *s;

	s = strsep(&f, ":");
	if (!f)
		return;
	while (*f == ' ' || *f == '\t')
		f++;
	s = strsep(&f, " \t");
	if (s == NULL)
		return;
	strlcpy(p, s, len);
	s = strsep(&p, " \t\n\r");
	if (s == NULL)
		return;
	s = strsep(&p, " \t\n\r");
	if (s)
		*s = '\0';
}

/*
 * Get address client connected to, by doing a getsockname call.
 * Must not be used with a NAT'ed connection (use divert-to instead of rdr-to).
 */
void
getcaddr(struct con *cp)
{
	struct sockaddr_storage original_destination;
	struct sockaddr *odp = (struct sockaddr *) &original_destination;
	socklen_t len = sizeof(struct sockaddr_storage);
	int error;

	cp->caddr[0] = '\0';
	if (getsockname(cp->pfd->fd, odp, &len) == -1)
		return;
	error = getnameinfo(odp, odp->sa_len, cp->caddr, sizeof(cp->caddr),
	    NULL, 0, NI_NUMERICHOST);
	if (error)
		cp->caddr[0] = '\0';
}

void
gethelo(char *p, size_t len, char *f)
{
	char *s;

	/* skip HELO/EHLO */
	f+=4;
	/* skip whitespace */
	while (*f == ' ' || *f == '\t')
		f++;
	s = strsep(&f, " \t");
	if (s == NULL)
		return;
	strlcpy(p, s, len);
	s = strsep(&p, " \t\n\r");
	if (s == NULL)
		return;
	s = strsep(&p, " \t\n\r");
	if (s)
		*s = '\0';
}

void
initcon(struct con *cp, int fd, struct sockaddr *sa)
{
	struct pollfd *pfd = cp->pfd;
	char ctimebuf[26];
	time_t tt;
	int error;

	if (sa->sa_family != AF_INET)
		errx(1, "not supported yet");

	time(&tt);
	free(cp->obuf);
	free(cp->blacklists);
	free(cp->lists);
	memset(cp, 0, sizeof(*cp));
	if (grow_obuf(cp, 0) == NULL)
		err(1, "malloc");
	cp->pfd = pfd;
	cp->pfd->fd = fd;
	memcpy(&cp->ss, sa, sa->sa_len);
	cp->af = sa->sa_family;
	cp->ia = &((struct sockaddr_in *)&cp->ss)->sin_addr;
	cp->blacklists = sdl_lookup(blacklists, cp->af, cp->ia);
	cp->stutter = (greylist && !grey_stutter && cp->blacklists == NULL) ?
	    0 : stutter;
	error = getnameinfo(sa, sa->sa_len, cp->addr, sizeof(cp->addr), NULL, 0,
	    NI_NUMERICHOST);
	if (error)
		strlcpy(cp->addr, "<unknown>", sizeof(cp->addr));
	memset(ctimebuf, 0, sizeof(ctimebuf));
	ctime_r(&t, ctimebuf);
	ctimebuf[sizeof(ctimebuf) - 2] = '\0'; /* nuke newline */
	snprintf(cp->obuf, cp->osize, "220 %s ESMTP %s; %s\r\n",
	    hostname, spamd, ctimebuf);
	cp->op = cp->obuf;
	cp->ol = strlen(cp->op);
	cp->w = tt + cp->stutter;
	cp->s = tt;
	strlcpy(cp->rend, "\n", sizeof cp->rend);
	clients++;
	if (cp->blacklists != NULL) {
		blackcount++;
		if (greylist && blackcount > maxblack)
			cp->stutter = 0;
		cp->lists = strdup(loglists(cp));
		if (cp->lists == NULL)
			err(1, "malloc");
	}
	else
		cp->lists = NULL;
}

void
closecon(struct con *cp)
{
	time_t tt;

	if (cp->cctx) {
		tls_close(cp->cctx);
		tls_free(cp->cctx);
	}
	close(cp->pfd->fd);
	cp->pfd->fd = -1;

	slowdowntill = 0;

	time(&tt);
	syslog_r(LOG_INFO, &sdata, "%s: disconnected after %lld seconds.%s%s",
	    cp->addr, (long long)(tt - cp->s),
	    ((cp->lists == NULL) ? "" : " lists:"),
	    ((cp->lists == NULL) ? "": cp->lists));
	if (debug > 0)
		printf("%s connected for %lld seconds.\n", cp->addr,
		    (long long)(tt - cp->s));
	free(cp->lists);
	cp->lists = NULL;
	if (cp->blacklists != NULL) {
		blackcount--;
		free(cp->blacklists);
		cp->blacklists = NULL;
	}
	if (cp->obuf != NULL) {
		free(cp->obuf);
		cp->obuf = NULL;
		cp->osize = 0;
	}
	clients--;
}

int
match(const char *s1, const char *s2)
{
	return (strncasecmp(s1, s2, strlen(s2)) == 0);
}

void
nextstate(struct con *cp)
{
	if (match(cp->ibuf, "QUIT") && cp->state < 99) {
		snprintf(cp->obuf, cp->osize, "221 %s\r\n", hostname);
		cp->op = cp->obuf;
		cp->ol = strlen(cp->op);
		cp->w = t + cp->stutter;
		cp->laststate = cp->state;
		cp->state = 99;
		return;
	}

	if (match(cp->ibuf, "RSET") && cp->state > 2 && cp->state < 50) {
		snprintf(cp->obuf, cp->osize,
		    "250 Ok to start over.\r\n");
		cp->op = cp->obuf;
		cp->ol = strlen(cp->op);
		cp->w = t + cp->stutter;
		cp->laststate = cp->state;
		cp->state = 2;
		return;
	}
	switch (cp->state) {
	case 0:
	tlsinitdone:
		/* banner sent; wait for input */
		cp->ip = cp->ibuf;
		cp->il = sizeof(cp->ibuf) - 1;
		cp->laststate = cp->state;
		cp->state = 1;
		cp->r = t;
		break;
	case 1:
		/* received input: parse, and select next state */
		if (match(cp->ibuf, "HELO") ||
		    match(cp->ibuf, "EHLO")) {
			int nextstate = 2;
			cp->helo[0] = '\0';
			gethelo(cp->helo, sizeof cp->helo, cp->ibuf);
			if (cp->helo[0] == '\0') {
				nextstate = 0;
				snprintf(cp->obuf, cp->osize,
				    "501 helo requires domain name.\r\n");
			} else {
				if (cp->cctx == NULL && tlsctx != NULL &&
				    cp->blacklists == NULL &&
				    match(cp->ibuf, "EHLO")) {
					snprintf(cp->obuf, cp->osize,
					    "250-%s\r\n"
					    "250-8BITMIME\r\n"
					    "250-SMTPUTF8\r\n"
					    "250 STARTTLS\r\n",
					    hostname);
					nextstate = 7;
				} else {
					snprintf(cp->obuf, cp->osize,
					    "250 Hello, spam sender. Pleased "
					    "to be wasting your time.\r\n");
				}
			}
			cp->op = cp->obuf;
			cp->ol = strlen(cp->op);
			cp->laststate = cp->state;
			cp->state = nextstate;
			cp->w = t + cp->stutter;
			break;
		}
		goto mail;
	case 2:
		/* sent 250 Hello, wait for input */
		cp->ip = cp->ibuf;
		cp->il = sizeof(cp->ibuf) - 1;
		cp->laststate = cp->state;
		cp->state = 3;
		cp->r = t;
		break;
	case 3:
	mail:
		if (match(cp->ibuf, "MAIL")) {
			setlog(cp->mail, sizeof cp->mail, cp->ibuf);
			snprintf(cp->obuf, cp->osize,
			    "250 You are about to try to deliver spam. "
			    "Your time will be spent, for nothing.\r\n");
			cp->op = cp->obuf;
			cp->ol = strlen(cp->op);
			cp->laststate = cp->state;
			cp->state = 4;
			cp->w = t + cp->stutter;
			break;
		}
		goto rcpt;
	case 4:
		/* sent 250 Sender ok */
		cp->ip = cp->ibuf;
		cp->il = sizeof(cp->ibuf) - 1;
		cp->laststate = cp->state;
		cp->state = 5;
		cp->r = t;
		break;
	case 5:
	rcpt:
		if (match(cp->ibuf, "RCPT")) {
			setlog(cp->rcpt, sizeof(cp->rcpt), cp->ibuf);
			snprintf(cp->obuf, cp->osize,
			    "250 This is hurting you more than it is "
			    "hurting me.\r\n");
			cp->op = cp->obuf;
			cp->ol = strlen(cp->op);
			cp->laststate = cp->state;
			cp->state = 6;
			cp->w = t + cp->stutter;

			if (cp->mail[0] && cp->rcpt[0]) {
				if (verbose)
					syslog_r(LOG_INFO, &sdata,
					    "(%s) %s: %s -> %s",
					    cp->blacklists ? "BLACK" : "GREY",
					    cp->addr, cp->mail,
					    cp->rcpt);
				if (debug)
					fprintf(stderr, "(%s) %s: %s -> %s\n",
					    cp->blacklists ? "BLACK" : "GREY",
					    cp->addr, cp->mail, cp->rcpt);
				if (greylist && cp->blacklists == NULL) {
					/* send this info to the greylister */
					getcaddr(cp);
					fprintf(grey,
					    "CO:%s\nHE:%s\nIP:%s\nFR:%s\nTO:%s\n",
					    cp->caddr, cp->helo, cp->addr,
					    cp->mail, cp->rcpt);
					fflush(grey);
				}
			}
			break;
		}
		goto spam;
	case 6:
		/* sent 250 blah */
		cp->ip = cp->ibuf;
		cp->il = sizeof(cp->ibuf) - 1;
		cp->laststate = cp->state;
		cp->state = 5;
		cp->r = t;
		break;
	case 7:
		/* sent 250 STARTTLS, wait for input */
		cp->ip = cp->ibuf;
		cp->il = sizeof(cp->ibuf) - 1;
		cp->laststate = cp->state;
		cp->state = 8;
		cp->r = t;
		break;
	case 8:
		if (tlsctx != NULL && cp->blacklists == NULL &&
		    cp->cctx == NULL && match(cp->ibuf, "STARTTLS")) {
			snprintf(cp->obuf, cp->osize,
			    "220 glad you want to burn more CPU cycles on "
			    "your spam\r\n");
			cp->op = cp->obuf;
			cp->ol = strlen(cp->op);
			cp->laststate = cp->state;
			cp->state = 9;
			cp->w = t + cp->stutter;
			break;
		}
		goto mail;
	case 9:
		if (tls_accept_socket(tlsctx, &cp->cctx, cp->pfd->fd) == -1) {
			snprintf(cp->obuf, cp->osize,
			    "500 STARTTLS failed\r\n");
			cp->op = cp->obuf;
			cp->ol = strlen(cp->op);
			cp->laststate = cp->state;
			cp->state = 98;
			goto done;
		}
		goto tlsinitdone;

	case 50:
	spam:
		if (match(cp->ibuf, "DATA")) {
			snprintf(cp->obuf, cp->osize,
			    "354 Enter spam, end with \".\" on a line by "
			    "itself\r\n");
			cp->state = 60;
			if (window && setsockopt(cp->pfd->fd, SOL_SOCKET,
			    SO_RCVBUF, &window, sizeof(window)) == -1) {
				syslog_r(LOG_DEBUG, &sdata,"setsockopt: %m");
				/* don't fail if this doesn't work. */
			}
			cp->ip = cp->ibuf;
			cp->il = sizeof(cp->ibuf) - 1;
			cp->op = cp->obuf;
			cp->ol = strlen(cp->op);
			cp->w = t + cp->stutter;
			if (greylist && cp->blacklists == NULL) {
				cp->laststate = cp->state;
				cp->state = 98;
				goto done;
			}
		} else {
			if (match(cp->ibuf, "NOOP"))
				snprintf(cp->obuf, cp->osize,
				    "250 2.0.0 OK I did nothing\r\n");
			else {
				snprintf(cp->obuf, cp->osize,
				    "500 5.5.1 Command unrecognized\r\n");
				cp->badcmd++;
				if (cp->badcmd > 20) {
					cp->laststate = cp->state;
					cp->state = 98;
					goto done;
				}
			}
			cp->state = cp->laststate;
			cp->ip = cp->ibuf;
			cp->il = sizeof(cp->ibuf) - 1;
			cp->op = cp->obuf;
			cp->ol = strlen(cp->op);
			cp->w = t + cp->stutter;
		}
		break;
	case 60:
		/* sent 354 blah */
		cp->ip = cp->ibuf;
		cp->il = sizeof(cp->ibuf) - 1;
		cp->laststate = cp->state;
		cp->state = 70;
		cp->r = t;
		break;
	case 70: {
		char *p, *q;

		for (p = q = cp->ibuf; q <= cp->ip; ++q)
			if (*q == '\n' || q == cp->ip) {
				*q = 0;
				if (q > p && q[-1] == '\r')
					q[-1] = 0;
				if (!strcmp(p, ".") ||
				    (cp->data_body && ++cp->data_lines >= 10)) {
					cp->laststate = cp->state;
					cp->state = 98;
					goto done;
				}
				if (!cp->data_body && !*p)
					cp->data_body = 1;
				if (verbose && cp->data_body && *p)
					syslog_r(LOG_DEBUG, &sdata, "%s: "
					    "Body: %s", cp->addr, p);
				else if (verbose && (match(p, "FROM:") ||
				    match(p, "TO:") || match(p, "SUBJECT:")))
					syslog_r(LOG_INFO, &sdata, "%s: %s",
					    cp->addr, p);
				p = ++q;
			}
		cp->ip = cp->ibuf;
		cp->il = sizeof(cp->ibuf) - 1;
		cp->r = t;
		break;
	}
	case 98:
	done:
		doreply(cp);
		cp->op = cp->obuf;
		cp->ol = strlen(cp->op);
		cp->w = t + cp->stutter;
		cp->laststate = cp->state;
		cp->state = 99;
		break;
	case 99:
		closecon(cp);
		break;
	default:
		errx(1, "illegal state %d", cp->state);
		break;
	}
}

void
handler(struct con *cp)
{
	int end = 0;
	ssize_t n;

	if (cp->r || cp->tlsaction != SPAMD_TLS_ACT_NONE) {
		if (cp->cctx) {
			cp->tlsaction = SPAMD_TLS_ACT_NONE;
			n = tls_read(cp->cctx, cp->ip, cp->il);
			if (n == TLS_WANT_POLLIN)
				cp->tlsaction = SPAMD_TLS_ACT_READ_POLLIN;
			if (n == TLS_WANT_POLLOUT)
				cp->tlsaction = SPAMD_TLS_ACT_READ_POLLOUT;
			if (cp->tlsaction != SPAMD_TLS_ACT_NONE)
				return;
		} else
			n = read(cp->pfd->fd, cp->ip, cp->il);

		if (n == 0)
			closecon(cp);
		else if (n == -1) {
			if (errno == EAGAIN)
				return;
			if (debug > 0)
				warn("read");
			closecon(cp);
		} else {
			cp->ip[n] = '\0';
			if (cp->rend[0])
				if (strpbrk(cp->ip, cp->rend))
					end = 1;
			cp->ip += n;
			cp->il -= n;
		}
	}
	if (end || cp->il == 0) {
		while (cp->ip > cp->ibuf &&
		    (cp->ip[-1] == '\r' || cp->ip[-1] == '\n'))
			cp->ip--;
		*cp->ip = '\0';
		cp->r = 0;
		nextstate(cp);
	}
}

void
handlew(struct con *cp, int one)
{
	ssize_t n;

	/* kill stutter on greylisted connections after initial delay */
	if (cp->stutter && greylist && cp->blacklists == NULL &&
	    (t - cp->s) > grey_stutter)
		cp->stutter=0;

	if (cp->w || cp->tlsaction != SPAMD_TLS_ACT_NONE) {
		if (*cp->op == '\n' && !cp->sr) {
			/* insert \r before \n */
			if (cp->cctx) {
				cp->tlsaction = SPAMD_TLS_ACT_NONE;
				n = tls_write(cp->cctx, "\r", 1);
				if (n == TLS_WANT_POLLIN)
					cp->tlsaction =
					    SPAMD_TLS_ACT_WRITE_POLLIN;
				if (n == TLS_WANT_POLLOUT)
					cp->tlsaction =
					    SPAMD_TLS_ACT_WRITE_POLLOUT;
				if (cp->tlsaction != SPAMD_TLS_ACT_NONE)
					return;
			} else
				n = write(cp->pfd->fd, "\r", 1);

			if (n == 0) {
				closecon(cp);
				goto handled;
			} else if (n == -1) {
				if (errno == EAGAIN)
					return;
				if (debug > 0 && errno != EPIPE)
					warn("write");
				closecon(cp);
				goto handled;
			}
		}
		if (*cp->op == '\r')
			cp->sr = 1;
		else
			cp->sr = 0;
		if (cp->cctx) {
			cp->tlsaction = SPAMD_TLS_ACT_NONE;
			n = tls_write(cp->cctx, cp->op, cp->ol);
			if (n == TLS_WANT_POLLIN)
				cp->tlsaction = SPAMD_TLS_ACT_WRITE_POLLIN;
			if (n == TLS_WANT_POLLOUT)
				cp->tlsaction = SPAMD_TLS_ACT_WRITE_POLLOUT;
			if (cp->tlsaction != SPAMD_TLS_ACT_NONE)
				return;
		} else
			n = write(cp->pfd->fd, cp->op,
			   (one && cp->stutter) ? 1 : cp->ol);

		if (n == 0)
			closecon(cp);
		else if (n == -1) {
			if (errno == EAGAIN)
				return;
			if (debug > 0 && errno != EPIPE)
				warn("write");
			closecon(cp);
		} else {
			cp->op += n;
			cp->ol -= n;
		}
	}
handled:
	cp->w = t + cp->stutter;
	if (cp->ol == 0) {
		cp->w = 0;
		nextstate(cp);
	}
}

static int
get_maxfiles(void)
{
	int mib[2], maxfiles;
	size_t len;

	mib[0] = CTL_KERN;
	mib[1] = KERN_MAXFILES;
	len = sizeof(maxfiles);
	if (sysctl(mib, 2, &maxfiles, &len, NULL, 0) == -1)
		return(MAXCON);
	if ((maxfiles - 200) < 10)
		errx(1, "kern.maxfiles is only %d, can not continue\n",
		    maxfiles);
	else
		return(maxfiles - 200);
}

/* Symbolic indexes for pfd[] below */
#define PFD_SMTPLISTEN	0
#define PFD_CONFLISTEN	1
#define PFD_SYNCFD	2
#define PFD_CONFFD	3
#define PFD_TRAPFD	4
#define PFD_GREYBACK	5
#define PFD_FIRSTCON	6

int
main(int argc, char *argv[])
{
	struct pollfd *pfd;
	struct sockaddr_in sin;
	struct sockaddr_in lin;
	int ch, smtplisten, conflisten, syncfd = -1, i, one = 1;
	u_short port;
	long long passt, greyt, whitet;
	struct servent *ent;
	struct rlimit rlp;
	char *bind_address = NULL;
	const char *errstr;
	char *sync_iface = NULL;
	char *sync_baddr = NULL;
	struct addrinfo hints, *res;
	char *addr;
	char portstr[6];
	int error;

	tzset();
	openlog_r("spamd", LOG_PID | LOG_NDELAY, LOG_DAEMON, &sdata);

	if ((ent = getservbyname("spamd", "tcp")) == NULL)
		errx(1, "Can't find service \"spamd\" in /etc/services");
	port = ntohs(ent->s_port);
	if ((ent = getservbyname("spamd-cfg", "tcp")) == NULL)
		errx(1, "Can't find service \"spamd-cfg\" in /etc/services");
	cfg_port = ntohs(ent->s_port);
	if ((ent = getservbyname("spamd-sync", "udp")) == NULL)
		errx(1, "Can't find service \"spamd-sync\" in /etc/services");
	sync_port = ntohs(ent->s_port);

	if (gethostname(hostname, sizeof hostname) == -1)
		err(1, "gethostname");
	maxfiles = get_maxfiles();
	if (maxcon > maxfiles)
		maxcon = maxfiles;
	if (maxblack > maxfiles)
		maxblack = maxfiles;
	while ((ch =
	    getopt(argc, argv, "45l:c:B:p:bdG:h:s:S:M:n:vw:y:Y:C:K:")) != -1) {
		switch (ch) {
		case '4':
			nreply = "450";
			break;
		case '5':
			nreply = "550";
			break;
		case 'l':
			bind_address = optarg;
			break;
		case 'B':
			maxblack = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-B %s: %s", optarg, errstr);
			break;
		case 'c':
			maxcon = strtonum(optarg, 1, maxfiles, &errstr);
			if (errstr) {
				fprintf(stderr, "-c %s: %s\n", optarg, errstr);
				usage();
			}
			break;
		case 'p':
			port = strtonum(optarg, 1, USHRT_MAX, &errstr);
			if (errstr)
				errx(1, "-p %s: %s", optarg, errstr);
			break;
		case 'd':
			debug = 1;
			break;
		case 'b':
			greylist = 0;
			break;
		case 'G':
			if (sscanf(optarg, "%lld:%lld:%lld", &passt, &greyt,
			    &whitet) != 3)
				usage();
			passtime = passt;
			greyexp = greyt;
			whiteexp = whitet;
			/* convert to seconds from minutes */
			passtime *= 60;
			/* convert to seconds from hours */
			whiteexp *= (60 * 60);
			/* convert to seconds from hours */
			greyexp *= (60 * 60);
			break;
		case 'h':
			memset(hostname, 0, sizeof(hostname));
			if (strlcpy(hostname, optarg, sizeof(hostname)) >=
			    sizeof(hostname))
				errx(1, "-h arg too long");
			break;
		case 's':
			stutter = strtonum(optarg, 0, 10, &errstr);
			if (errstr)
				usage();
			break;
		case 'S':
			grey_stutter = strtonum(optarg, 0, 90, &errstr);
			if (errstr)
				usage();
			break;
		case 'M':
			low_prio_mx_ip = optarg;
			break;
		case 'n':
			spamd = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			window = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "-w %s: %s", optarg, errstr);
			break;
		case 'Y':
			if (sync_addhost(optarg, sync_port) != 0)
				sync_iface = optarg;
			syncsend++;
			break;
		case 'y':
			sync_baddr = optarg;
			syncrecv++;
			break;
		case 'C':
			tlscertfile = optarg;
			break;
		case 'K':
			tlskeyfile = optarg;
			break;
		default:
			usage();
			break;
		}
	}

	setproctitle("[priv]%s%s",
	    greylist ? " (greylist)" : "",
	    (syncrecv || syncsend) ? " (sync)" : "");

	if (syncsend || syncrecv) {
		syncfd = sync_init(sync_iface, sync_baddr, sync_port);
		if (syncfd == -1)
			err(1, "sync init");
	}

	if (geteuid())
		errx(1, "need root privileges");

	if ((pw = getpwnam(SPAMD_USER)) == NULL)
		errx(1, "no such user %s", SPAMD_USER);

	if (!greylist) {
		maxblack = maxcon;
	} else if (maxblack > maxcon)
		usage();

	spamd_tls_init();

	rlp.rlim_cur = rlp.rlim_max = maxcon + 15;
	if (setrlimit(RLIMIT_NOFILE, &rlp) == -1)
		err(1, "setrlimit");

	pfd = reallocarray(NULL, PFD_FIRSTCON + maxcon, sizeof(*pfd));
	if (pfd == NULL)
		err(1, "reallocarray");

	con = calloc(maxcon, sizeof(*con));
	if (con == NULL)
		err(1, "calloc");

	con->obuf = malloc(8192);

	if (con->obuf == NULL)
		err(1, "malloc");
	con->osize = 8192;

	for (i = 0; i < maxcon; i++) {
		con[i].pfd = &pfd[PFD_FIRSTCON + i];
		con[i].pfd->fd = -1;
	}

	signal(SIGPIPE, SIG_IGN);

	smtplisten = socket(AF_INET, SOCK_STREAM, 0);
	if (smtplisten == -1)
		err(1, "socket");

	if (setsockopt(smtplisten, SOL_SOCKET, SO_REUSEADDR, &one,
	    sizeof(one)) == -1)
		return (-1);

	conflisten = socket(AF_INET, SOCK_STREAM, 0);
	if (conflisten == -1)
		err(1, "socket");

	if (setsockopt(conflisten, SOL_SOCKET, SO_REUSEADDR, &one,
	    sizeof(one)) == -1)
		return (-1);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	addr = bind_address;
	snprintf(portstr, sizeof(portstr), "%hu", port);

	if ((error = getaddrinfo(addr, portstr, &hints, &res)) != 0) {
		errx(1, "getaddrinfo: %s", gai_strerror(error));
	}

	if (bind(smtplisten, res->ai_addr, res->ai_addrlen) == -1) {
		freeaddrinfo(res);
		err(1, "bind");
	}
	freeaddrinfo(res);

	memset(&lin, 0, sizeof sin);
	lin.sin_len = sizeof(sin);
	lin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	lin.sin_family = AF_INET;
	lin.sin_port = htons(cfg_port);

	if (bind(conflisten, (struct sockaddr *)&lin, sizeof lin) == -1)
		err(1, "bind local");

	if (debug == 0) {
		if (daemon(1, 1) == -1)
			err(1, "daemon");
	}

	if (greylist) {
		pfdev = open("/dev/pf", O_RDWR);
		if (pfdev == -1) {
			syslog_r(LOG_ERR, &sdata, "open /dev/pf: %m");
			exit(1);
		}

		check_spamd_db();

		maxblack = (maxblack >= maxcon) ? maxcon - 100 : maxblack;
		if (maxblack < 0)
			maxblack = 0;

		/* open pipe to talk to greylister */
		if (socketpair(AF_UNIX, SOCK_DGRAM, 0, greyback) == -1) {
			syslog(LOG_ERR, "socketpair (%m)");
			exit(1);
		}
		if (pipe(greypipe) == -1) {
			syslog(LOG_ERR, "pipe (%m)");
			exit(1);
		}
		/* open pipe to receive spamtrap configs */
		if (pipe(trappipe) == -1) {
			syslog(LOG_ERR, "pipe (%m)");
			exit(1);
		}
		jail_pid = fork();
		switch (jail_pid) {
		case -1:
			syslog(LOG_ERR, "fork (%m)");
			exit(1);
		case 0:
			/* child - continue */
			signal(SIGPIPE, SIG_IGN);
			grey = fdopen(greypipe[1], "w");
			if (grey == NULL) {
				syslog(LOG_ERR, "fdopen (%m)");
				_exit(1);
			}
			close(greyback[0]);
			close(greypipe[0]);
			trapfd = trappipe[0];
			trapcfg = fdopen(trappipe[0], "r");
			if (trapcfg == NULL) {
				syslog(LOG_ERR, "fdopen (%m)");
				_exit(1);
			}
			close(trappipe[1]);

			if (setgroups(1, &pw->pw_gid) ||
			    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
			    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
				err(1, "failed to drop privs");

			goto jail;
		}
		/* parent - run greylister */
		close(greyback[1]);
		grey = fdopen(greypipe[0], "r");
		if (grey == NULL) {
			syslog(LOG_ERR, "fdopen (%m)");
			exit(1);
		}
		close(greypipe[1]);
		trapcfg = fdopen(trappipe[1], "w");
		if (trapcfg == NULL) {
			syslog(LOG_ERR, "fdopen (%m)");
			exit(1);
		}
		close(trappipe[0]);
		return (greywatcher());
	}

jail:
	if (pledge("stdio inet", NULL) == -1)
		err(1, "pledge");

	if (listen(smtplisten, 10) == -1)
		err(1, "listen");

	if (listen(conflisten, 10) == -1)
		err(1, "listen");

	if (debug != 0)
		printf("listening for incoming connections.\n");
	syslog_r(LOG_WARNING, &sdata, "listening for incoming connections.");

	/* We always check for trap and sync events if configured. */
	if (trapfd != -1) {
		pfd[PFD_TRAPFD].fd = trapfd;
		pfd[PFD_TRAPFD].events = POLLIN;
	} else {
		pfd[PFD_TRAPFD].fd = -1;
		pfd[PFD_TRAPFD].events = 0;
	}
	if (syncrecv) {
		pfd[PFD_SYNCFD].fd = syncfd;
		pfd[PFD_SYNCFD].events = POLLIN;
	} else {
		pfd[PFD_SYNCFD].fd = -1;
		pfd[PFD_SYNCFD].events = 0;
	}
	if (greylist) {
		pfd[PFD_GREYBACK].fd = greyback[1];
		pfd[PFD_GREYBACK].events = POLLIN;
	} else {
		pfd[PFD_GREYBACK].fd = -1;
		pfd[PFD_GREYBACK].events = 0;
	}

	/* events and pfd entries for con[] are filled in below. */
	pfd[PFD_SMTPLISTEN].fd = smtplisten;
	pfd[PFD_CONFLISTEN].fd = conflisten;

	while (1) {
		int numcon = 0, n, timeout, writers;

		time(&t);

		writers = 0;
		for (i = 0; i < maxcon; i++) {
			if (con[i].pfd->fd == -1)
				continue;
			con[i].pfd->events = 0;
			if (con[i].r) {
				if (con[i].r + MAXTIME <= t) {
					closecon(&con[i]);
					continue;
				}
				con[i].pfd->events |= POLLIN;
			}
			if (con[i].w) {
				if (con[i].w + MAXTIME <= t) {
					closecon(&con[i]);
					continue;
				}
				if (con[i].w <= t)
					con[i].pfd->events |= POLLOUT;
				writers = 1;
			}
			if (con[i].tlsaction == SPAMD_TLS_ACT_READ_POLLIN ||
			    con[i].tlsaction == SPAMD_TLS_ACT_WRITE_POLLIN)
				con[i].pfd->events = POLLIN;
			if (con[i].tlsaction == SPAMD_TLS_ACT_READ_POLLOUT ||
			    con[i].tlsaction == SPAMD_TLS_ACT_WRITE_POLLOUT)
				con[i].pfd->events = POLLOUT;
			if (i + 1 > numcon)
				numcon = i + 1;
		}
		pfd[PFD_SMTPLISTEN].events = 0;
		pfd[PFD_CONFLISTEN].events = 0;
		pfd[PFD_CONFFD].events = 0;
		pfd[PFD_CONFFD].fd = conffd;
		if (slowdowntill == 0) {
			pfd[PFD_SMTPLISTEN].events = POLLIN;

			/* only one active config conn at a time */
			if (conffd == -1)
				pfd[PFD_CONFLISTEN].events = POLLIN;
			else
				pfd[PFD_CONFFD].events = POLLIN;
		}

		/* If we are not listening, wake up at least once a second */
		if (writers == 0 && slowdowntill == 0)
			timeout = INFTIM;
		else
			timeout = 1000;

		n = poll(pfd, PFD_FIRSTCON + numcon, timeout);
		if (n == -1) {
			if (errno != EINTR)
				err(1, "poll");
			continue;
		}

		/* Check if we can speed up accept() calls */
		if (slowdowntill && slowdowntill > t)
			slowdowntill = 0;

		for (i = 0; i < maxcon; i++) {
			if (con[i].pfd->fd == -1)
				continue;
			if (pfd[PFD_FIRSTCON + i].revents & POLLHUP) {
				closecon(&con[i]);
				continue;
			}
			if (pfd[PFD_FIRSTCON + i].revents & POLLIN) {
				if (con[i].tlsaction ==
				    SPAMD_TLS_ACT_WRITE_POLLIN)
					handlew(&con[i], clients + 5 < maxcon);
				else
					handler(&con[i]);
			}
			if (pfd[PFD_FIRSTCON + i].revents & POLLOUT) {
				if (con[i].tlsaction ==
				    SPAMD_TLS_ACT_READ_POLLOUT)
					handler(&con[i]);
				else
					handlew(&con[i], clients + 5 < maxcon);
			}
		}
		if (pfd[PFD_SMTPLISTEN].revents & (POLLIN|POLLHUP)) {
			socklen_t sinlen;
			int s2;

			sinlen = sizeof(sin);
			s2 = accept4(smtplisten, (struct sockaddr *)&sin, &sinlen,
			    SOCK_NONBLOCK);
			if (s2 == -1) {
				switch (errno) {
				case EINTR:
				case ECONNABORTED:
					break;
				case EMFILE:
				case ENFILE:
					slowdowntill = time(NULL) + 1;
					break;
				default:
					errx(1, "accept");
				}
			} else {
				/* Check if we hit the chosen fd limit */
				for (i = 0; i < maxcon; i++)
					if (con[i].pfd->fd == -1)
						break;
				if (i == maxcon) {
					close(s2);
					slowdowntill = 0;
				} else {
					initcon(&con[i], s2,
					    (struct sockaddr *)&sin);
					syslog_r(LOG_INFO, &sdata,
					    "%s: connected (%d/%d)%s%s",
					    con[i].addr, clients, blackcount,
					    ((con[i].lists == NULL) ? "" :
					    ", lists:"),
					    ((con[i].lists == NULL) ? "":
					    con[i].lists));
				}
			}
		}
		if (pfd[PFD_CONFLISTEN].revents & (POLLIN|POLLHUP)) {
			socklen_t sinlen;

			sinlen = sizeof(lin);
			conffd = accept(conflisten, (struct sockaddr *)&lin,
			    &sinlen);
			if (conffd == -1) {
				switch (errno) {
				case EINTR:
				case ECONNABORTED:
					break;
				case EMFILE:
				case ENFILE:
					slowdowntill = time(NULL) + 1;
					break;
				default:
					errx(1, "accept");
				}
			} else if (ntohs(lin.sin_port) >= IPPORT_RESERVED) {
				close(conffd);
				conffd = -1;
				slowdowntill = 0;
			}
		} else if (pfd[PFD_CONFFD].revents & (POLLIN|POLLHUP))
			do_config();
		if (pfd[PFD_TRAPFD].revents & (POLLIN|POLLHUP))
			read_configline(trapcfg);
		if (pfd[PFD_SYNCFD].revents & (POLLIN|POLLHUP))
			sync_recv();
		if (pfd[PFD_GREYBACK].revents & (POLLIN|POLLHUP))
			blackcheck(greyback[1]);
	}
	exit(1);
}

void
blackcheck(int fd)
{
	struct sockaddr_storage ss;
	ssize_t nread;
	void *ia;
	char ch;

	/* Read sockaddr from greylister and look it up in the blacklists. */
	nread = recv(fd, &ss, sizeof(ss), 0);
	if (nread == -1) {
		syslog(LOG_ERR, "%s: recv: %m", __func__);
		return;
	}
	if (nread != sizeof(struct sockaddr_in) &&
	    nread != sizeof(struct sockaddr_in6)) {
		syslog(LOG_ERR, "%s: invalid size %zd", __func__, nread);
		return;
	}
	if (ss.ss_family == AF_INET) {
		ia = &((struct sockaddr_in *)&ss)->sin_addr;
	} else if (ss.ss_family == AF_INET6) {
		ia = &((struct sockaddr_in6 *)&ss)->sin6_addr;
	} else {
		syslog(LOG_ERR, "%s: bad family %d", __func__, ss.ss_family);
		return;
	}
	ch = sdl_check(blacklists, ss.ss_family, ia) ? '1' : '0';

	/* Send '1' for match or '0' for no match. */
	if (send(fd, &ch, sizeof(ch), 0) == -1) {
		syslog(LOG_ERR, "%s: send: %m", __func__);
		return;
	}
}
