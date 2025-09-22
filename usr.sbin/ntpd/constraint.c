/*	$OpenBSD: constraint.c,v 1.60 2024/11/21 13:38:14 claudio Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <imsg.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <tls.h>
#include <pwd.h>
#include <math.h>

#include "ntpd.h"

#define	IMF_FIXDATE	"%a, %d %h %Y %T GMT"
#define	X509_DATE	"%Y-%m-%d %T UTC"

int	 constraint_addr_init(struct constraint *);
void	 constraint_addr_head_clear(struct constraint *);
struct constraint *
	 constraint_byid(u_int32_t);
struct constraint *
	 constraint_byfd(int);
struct constraint *
	 constraint_bypid(pid_t);
int	 constraint_close(u_int32_t);
void	 constraint_update(void);
int	 constraint_cmp(const void *, const void *);

void	 priv_constraint_close(int, int);
void	 priv_constraint_readquery(struct constraint *, struct ntp_addr_msg *,
	    uint8_t **);

struct httpsdate *
	 httpsdate_init(const char *, const char *, const char *,
	    const char *, const u_int8_t *, size_t, int);
void	 httpsdate_free(void *);
int	 httpsdate_request(struct httpsdate *, struct timeval *, int);
void	*httpsdate_query(const char *, const char *, const char *,
	    const char *, const u_int8_t *, size_t,
	    struct timeval *, struct timeval *, int);

char	*tls_readline(struct tls *, size_t *, size_t *, struct timeval *);

u_int constraint_cnt;
extern u_int peer_cnt;
extern struct imsgbuf *ibuf;		/* priv */
extern struct imsgbuf *ibuf_main;	/* chld */

struct httpsdate {
	char			*tls_addr;
	char			*tls_port;
	char			*tls_hostname;
	char			*tls_path;
	char			*tls_request;
	struct tls_config	*tls_config;
	struct tls		*tls_ctx;
	struct tm		 tls_tm;
};

int
constraint_init(struct constraint *cstr)
{
	cstr->state = STATE_NONE;
	cstr->fd = -1;
	cstr->last = getmonotime();
	cstr->constraint = 0;
	cstr->senderrors = 0;

	return (constraint_addr_init(cstr));
}

int
constraint_addr_init(struct constraint *cstr)
{
	struct sockaddr_in	*sa_in;
	struct sockaddr_in6	*sa_in6;
	struct ntp_addr		*h;

	if (cstr->state == STATE_DNS_INPROGRESS)
		return (0);

	if (cstr->addr_head.a == NULL) {
		priv_dns(IMSG_CONSTRAINT_DNS, cstr->addr_head.name, cstr->id);
		cstr->state = STATE_DNS_INPROGRESS;
		return (0);
	}

	h = cstr->addr;
	switch (h->ss.ss_family) {
	case AF_INET:
		sa_in = (struct sockaddr_in *)&h->ss;
		if (ntohs(sa_in->sin_port) == 0)
			sa_in->sin_port = htons(443);
		cstr->state = STATE_DNS_DONE;
		break;
	case AF_INET6:
		sa_in6 = (struct sockaddr_in6 *)&h->ss;
		if (ntohs(sa_in6->sin6_port) == 0)
			sa_in6->sin6_port = htons(443);
		cstr->state = STATE_DNS_DONE;
		break;
	default:
		/* XXX king bula sez it? */
		fatalx("wrong AF in constraint_addr_init");
		/* NOTREACHED */
	}

	return (1);
}

void
constraint_addr_head_clear(struct constraint *cstr)
{
	host_dns_free(cstr->addr_head.a);
	cstr->addr_head.a = NULL;
	cstr->addr = NULL;
}

int
constraint_query(struct constraint *cstr, int synced)
{
	time_t			 now;
	struct ntp_addr_msg	 am;
	struct iovec		 iov[3];
	int			 iov_cnt = 0;

	now = getmonotime();

	switch (cstr->state) {
	case STATE_DNS_DONE:
		/* Proceed and query the time */
		break;
	case STATE_DNS_TEMPFAIL:
		if (now > cstr->last + (cstr->dnstries >= TRIES_AUTO_DNSFAIL ?
		    CONSTRAINT_RETRY_INTERVAL : INTERVAL_AUIO_DNSFAIL)) {
			cstr->dnstries++;
			/* Retry resolving the address */
			constraint_init(cstr);
			return 0;
		}
		return (-1);
	case STATE_QUERY_SENT:
		if (cstr->last + CONSTRAINT_SCAN_TIMEOUT > now) {
			/* The caller should expect a reply */
			return (0);
		}

		/* Timeout, just kill the process to reset it. */
		imsg_compose(ibuf_main, IMSG_CONSTRAINT_KILL,
		    cstr->id, 0, -1, NULL, 0);

		cstr->state = STATE_TIMEOUT;
		return (-1);
	case STATE_INVALID:
		if (cstr->last + CONSTRAINT_SCAN_INTERVAL > now) {
			/* Nothing to do */
			return (-1);
		}

		/* Reset and retry */
		cstr->senderrors = 0;
		constraint_close(cstr->id);
		break;
	case STATE_REPLY_RECEIVED:
	default:
		/* Nothing to do */
		return (-1);
	}

	cstr->last = now;
	cstr->state = STATE_QUERY_SENT;

	memset(&am, 0, sizeof(am));
	memcpy(&am.a, cstr->addr, sizeof(am.a));
	am.synced = synced;

	iov[iov_cnt].iov_base = &am;
	iov[iov_cnt++].iov_len = sizeof(am);
	if (cstr->addr_head.name) {
		am.namelen = strlen(cstr->addr_head.name) + 1;
		iov[iov_cnt].iov_base = cstr->addr_head.name;
		iov[iov_cnt++].iov_len = am.namelen;
	}
	if (cstr->addr_head.path) {
		am.pathlen = strlen(cstr->addr_head.path) + 1;
		iov[iov_cnt].iov_base = cstr->addr_head.path;
		iov[iov_cnt++].iov_len = am.pathlen;
	}

	imsg_composev(ibuf_main, IMSG_CONSTRAINT_QUERY,
	    cstr->id, 0, -1, iov, iov_cnt);

	return (0);
}

void
priv_constraint_msg(u_int32_t id, u_int8_t *data, size_t len, int argc,
    char **argv)
{
	struct ntp_addr_msg	 am;
	struct ntp_addr		*h;
	struct constraint	*cstr;
	int			 pipes[2];

	if ((cstr = constraint_byid(id)) != NULL) {
		log_warnx("IMSG_CONSTRAINT_QUERY repeated for id %d", id);
		return;
	}

	if (len < sizeof(am)) {
		log_warnx("invalid IMSG_CONSTRAINT_QUERY received");
		return;
	}
	memcpy(&am, data, sizeof(am));
	if (len != (sizeof(am) + am.namelen + am.pathlen)) {
		log_warnx("invalid IMSG_CONSTRAINT_QUERY received");
		return;
	}
	/* Additional imsg data is obtained in the unpriv child */

	if ((h = calloc(1, sizeof(*h))) == NULL)
		fatal("calloc ntp_addr");
	memcpy(h, &am.a, sizeof(*h));
	h->next = NULL;

	cstr = new_constraint();
	cstr->id = id;
	cstr->addr = h;
	cstr->addr_head.a = h;
	constraint_add(cstr);
	constraint_cnt++;

	if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, AF_UNSPEC,
	    pipes) == -1)
		fatal("%s pipes", __func__);

	/* Prepare and send constraint data to child. */
	cstr->fd = pipes[0];
	if (imsgbuf_init(&cstr->ibuf, cstr->fd) == -1)
		fatal("imsgbuf_init");
	if (imsg_compose(&cstr->ibuf, IMSG_CONSTRAINT_QUERY, id, 0, -1,
	    data, len) == -1)
		fatal("%s: imsg_compose", __func__);
	/*
	 * Fork child handlers and make sure to do any sensitive work in the
	 * the (unprivileged) child.  The parent should not do any parsing,
	 * certificate loading etc.
	 */
	cstr->pid = start_child(CONSTRAINT_PROC_NAME, pipes[1], argc, argv);

	if (imsgbuf_flush(&cstr->ibuf) == -1)
		fatal("imsgbuf_flush");
}

static int
imsgbuf_read_one(struct imsgbuf *imsgbuf, struct imsg *imsg)
{
	while (1) {
		switch (imsg_get(imsgbuf, imsg)) {
		case -1:
			return (-1);
		case 0:
			break;
		default:
			return (1);
		}

		switch (imsgbuf_read(imsgbuf)) {
		case -1:
			return (-1);
		case 0:
			return (0);
		}
	}
}

void
priv_constraint_readquery(struct constraint *cstr, struct ntp_addr_msg *am,
    uint8_t **data)
{
	struct ntp_addr		*h;
	uint8_t			*dptr;
	struct imsg		 imsg;
	size_t			 mlen;

	/* Read the message our parent left us. */
	switch (imsgbuf_read_one(&cstr->ibuf, &imsg)) {
	case -1:
		fatal("%s: imsgbuf_read_one", __func__);
	case 0:
		fatalx("%s: imsgbuf_read_one: connection closed", __func__);
	}
	if (imsg.hdr.type != IMSG_CONSTRAINT_QUERY)
		fatalx("%s: invalid message type", __func__);

	/*
	 * Copy the message contents just like our father:
	 * priv_constraint_msg().
	 */
	mlen = imsg.hdr.len - IMSG_HEADER_SIZE;
	if (mlen < sizeof(*am))
		fatalx("%s: mlen < sizeof(*am)", __func__);

	memcpy(am, imsg.data, sizeof(*am));
	if (mlen != (sizeof(*am) + am->namelen + am->pathlen))
		fatalx("%s: mlen < sizeof(*am) + am->namelen + am->pathlen",
		    __func__);

	if ((h = calloc(1, sizeof(*h))) == NULL ||
	    (*data = calloc(1, mlen)) == NULL)
		fatal("%s: calloc", __func__);

	memcpy(h, &am->a, sizeof(*h));
	h->next = NULL;

	cstr->id = imsg.hdr.peerid;
	cstr->addr = h;
	cstr->addr_head.a = h;

	dptr = imsg.data;
	memcpy(*data, dptr + sizeof(*am), mlen - sizeof(*am));
	imsg_free(&imsg);
}

void
priv_constraint_child(const char *pw_dir, uid_t pw_uid, gid_t pw_gid)
{
	struct constraint	 cstr;
	struct ntp_addr_msg	 am;
	uint8_t			*data;
	static char		 addr[NI_MAXHOST];
	struct timeval		 rectv, xmttv;
	struct sigaction	 sa;
	void			*ctx;
	struct iovec		 iov[2];
	int			 i;

	log_procinit("constraint");

	if (setpriority(PRIO_PROCESS, 0, 0) == -1)
		log_warn("could not set priority");

	/* load CA certs before chroot() */
	if ((conf->ca = tls_load_file(tls_default_ca_cert_file(),
	    &conf->ca_len, NULL)) == NULL)
		fatalx("failed to load constraint ca");

	if (chroot(pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	if (setgroups(1, &pw_gid) ||
	    setresgid(pw_gid, pw_gid, pw_gid) ||
	    setresuid(pw_uid, pw_uid, pw_uid))
		fatal("can't drop privileges");

	/* Reset all signal handlers */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = SIG_DFL;
	for (i = 1; i < _NSIG; i++)
		sigaction(i, &sa, NULL);

	if (pledge("stdio inet", NULL) == -1)
		fatal("pledge");

	cstr.fd = CONSTRAINT_PASSFD;
	if (imsgbuf_init(&cstr.ibuf, cstr.fd) == -1)
		fatal("imsgbuf_init");
	priv_constraint_readquery(&cstr, &am, &data);

	/*
	 * Get the IP address as name and set the process title accordingly.
	 * This only converts an address into a string and does not trigger
	 * any DNS operation, so it is safe to be called without the dns
	 * pledge.
	 */
	if (getnameinfo((struct sockaddr *)&cstr.addr->ss,
	    SA_LEN((struct sockaddr *)&cstr.addr->ss),
	    addr, sizeof(addr), NULL, 0,
	    NI_NUMERICHOST) != 0)
		fatalx("%s getnameinfo", __func__);

	log_debug("constraint request to %s", addr);
	setproctitle("constraint from %s", addr);
	(void)closefrom(CONSTRAINT_PASSFD + 1);

	/*
	 * Set the close-on-exec flag to prevent leaking the communication
	 * channel to any exec'ed child.  In theory this could never happen,
	 * constraints don't exec children and pledge() prevents it,
	 * but we keep it as a safety belt; especially for portability.
	 */
	if (fcntl(CONSTRAINT_PASSFD, F_SETFD, FD_CLOEXEC) == -1)
		fatal("%s fcntl F_SETFD", __func__);

	/* Get remaining data from imsg in the unpriv child */
	if (am.namelen) {
		if ((cstr.addr_head.name =
		    get_string(data, am.namelen)) == NULL)
			fatalx("invalid IMSG_CONSTRAINT_QUERY name");
		data += am.namelen;
	}
	if (am.pathlen) {
		if ((cstr.addr_head.path =
		    get_string(data, am.pathlen)) == NULL)
			fatalx("invalid IMSG_CONSTRAINT_QUERY path");
	}

	/* Run! */
	if ((ctx = httpsdate_query(addr,
	    CONSTRAINT_PORT, cstr.addr_head.name, cstr.addr_head.path,
	    conf->ca, conf->ca_len, &rectv, &xmttv, am.synced)) == NULL) {
		/* Abort with failure but without warning */
		exit(1);
	}

	iov[0].iov_base = &rectv;
	iov[0].iov_len = sizeof(rectv);
	iov[1].iov_base = &xmttv;
	iov[1].iov_len = sizeof(xmttv);
	imsg_composev(&cstr.ibuf,
	    IMSG_CONSTRAINT_RESULT, 0, 0, -1, iov, 2);
	imsgbuf_flush(&cstr.ibuf);

	/* Tear down the TLS connection after sending the result */
	httpsdate_free(ctx);

	exit(0);
}

void
priv_constraint_check_child(pid_t pid, int status)
{
	struct constraint	*cstr;
	int			 fail, sig;
	char			*signame;

	fail = sig = 0;
	if (WIFSIGNALED(status)) {
		sig = WTERMSIG(status);
	} else if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0)
			fail = 1;
	} else
		fatalx("unexpected cause of SIGCHLD");

	if ((cstr = constraint_bypid(pid)) != NULL) {
		if (sig) {
			if (sig != SIGTERM) {
				signame = strsignal(sig) ?
				    strsignal(sig) : "unknown";
				log_warnx("constraint %s; "
				    "terminated with signal %d (%s)",
				    log_ntp_addr(cstr->addr), sig, signame);
			}
			fail = 1;
		}

		priv_constraint_close(cstr->fd, fail);
	}
}

void
priv_constraint_kill(u_int32_t id)
{
	struct constraint	*cstr;

	if ((cstr = constraint_byid(id)) == NULL) {
		log_warnx("IMSG_CONSTRAINT_KILL for invalid id %d", id);
		return;
	}

	kill(cstr->pid, SIGTERM);
}

struct constraint *
constraint_byid(u_int32_t id)
{
	struct constraint	*cstr;

	TAILQ_FOREACH(cstr, &conf->constraints, entry) {
		if (cstr->id == id)
			return (cstr);
	}

	return (NULL);
}

struct constraint *
constraint_byfd(int fd)
{
	struct constraint	*cstr;

	TAILQ_FOREACH(cstr, &conf->constraints, entry) {
		if (cstr->fd == fd)
			return (cstr);
	}

	return (NULL);
}

struct constraint *
constraint_bypid(pid_t pid)
{
	struct constraint	*cstr;

	TAILQ_FOREACH(cstr, &conf->constraints, entry) {
		if (cstr->pid == pid)
			return (cstr);
	}

	return (NULL);
}

int
constraint_close(u_int32_t id)
{
	struct constraint	*cstr;

	if ((cstr = constraint_byid(id)) == NULL) {
		log_warn("%s: id %d: not found", __func__, id);
		return (0);
	}

	cstr->last = getmonotime();

	if (cstr->addr == NULL || (cstr->addr = cstr->addr->next) == NULL) {
		/* Either a pool or all addresses have been tried */
		cstr->addr = cstr->addr_head.a;
		if (cstr->senderrors)
			cstr->state = STATE_INVALID;
		else if (cstr->state >= STATE_QUERY_SENT)
			cstr->state = STATE_DNS_DONE;

		return (1);
	}

	return (constraint_init(cstr));
}

void
priv_constraint_close(int fd, int fail)
{
	struct constraint	*cstr;
	u_int32_t		 id;

	if ((cstr = constraint_byfd(fd)) == NULL) {
		log_warn("%s: fd %d: not found", __func__, fd);
		return;
	}

	id = cstr->id;
	constraint_remove(cstr);
	constraint_cnt--;

	imsg_compose(ibuf, IMSG_CONSTRAINT_CLOSE, id, 0, -1,
	    &fail, sizeof(fail));
}

void
constraint_add(struct constraint *cstr)
{
	TAILQ_INSERT_TAIL(&conf->constraints, cstr, entry);
}

void
constraint_remove(struct constraint *cstr)
{
	TAILQ_REMOVE(&conf->constraints, cstr, entry);

	imsgbuf_clear(&cstr->ibuf);
	if (cstr->fd != -1)
		close(cstr->fd);
	free(cstr->addr_head.name);
	free(cstr->addr_head.path);
	free(cstr->addr);
	free(cstr);
}

void
constraint_purge(void)
{
	struct constraint	*cstr, *ncstr;

	TAILQ_FOREACH_SAFE(cstr, &conf->constraints, entry, ncstr)
		constraint_remove(cstr);
}

int
priv_constraint_dispatch(struct pollfd *pfd)
{
	struct imsg		 imsg;
	struct constraint	*cstr;
	ssize_t			 n;
	struct timeval		 tv[2];

	if ((cstr = constraint_byfd(pfd->fd)) == NULL)
		return (0);

	if (!(pfd->revents & POLLIN))
		return (0);

	if (imsgbuf_read(&cstr->ibuf) != 1) {
		/* there's a race between SIGCHLD delivery and reading imsg
		   but if we've seen the reply, we're good */
		priv_constraint_close(pfd->fd, cstr->state !=
		    STATE_REPLY_RECEIVED);
		return (1);
	}

	for (;;) {
		if ((n = imsg_get(&cstr->ibuf, &imsg)) == -1) {
			priv_constraint_close(pfd->fd, 1);
			return (1);
		}
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_CONSTRAINT_RESULT:
			 if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(tv))
				fatalx("invalid IMSG_CONSTRAINT received");

			/* state is maintained by child, but we want to
			   remember we've seen the result */
			cstr->state = STATE_REPLY_RECEIVED;
			/* forward imsg to ntp child, don't parse it here */
			imsg_compose(ibuf, imsg.hdr.type,
			    cstr->id, 0, -1, imsg.data, sizeof(tv));
			break;
		default:
			break;
		}
		imsg_free(&imsg);
	}

	return (0);
}

void
constraint_msg_result(u_int32_t id, u_int8_t *data, size_t len)
{
	struct constraint	*cstr;
	struct timeval		 tv[2];
	double			 offset;

	if ((cstr = constraint_byid(id)) == NULL) {
		log_warnx("IMSG_CONSTRAINT_CLOSE with invalid constraint id");
		return;
	}

	if (len != sizeof(tv)) {
		log_warnx("invalid IMSG_CONSTRAINT received");
		return;
	}

	memcpy(tv, data, len);

	offset = gettime_from_timeval(&tv[0]) -
	    gettime_from_timeval(&tv[1]);

	log_info("constraint reply from %s: offset %f",
	    log_ntp_addr(cstr->addr),
	    offset);

	cstr->state = STATE_REPLY_RECEIVED;
	cstr->last = getmonotime();
	cstr->constraint = tv[0].tv_sec;

	constraint_update();
}

void
constraint_msg_close(u_int32_t id, u_int8_t *data, size_t len)
{
	struct constraint	*cstr, *tmp;
	int			 fail, cnt;
	static int		 total_fails;

	if ((cstr = constraint_byid(id)) == NULL) {
		log_warnx("IMSG_CONSTRAINT_CLOSE with invalid constraint id");
		return;
	}

	if (len != sizeof(int)) {
		log_warnx("invalid IMSG_CONSTRAINT_CLOSE received");
		return;
	}

	memcpy(&fail, data, len);

	if (fail) {
		log_debug("no constraint reply from %s"
		    " received in time, next query %ds",
		    log_ntp_addr(cstr->addr),
		    CONSTRAINT_SCAN_INTERVAL);
		
		cnt = 0;
		TAILQ_FOREACH(tmp, &conf->constraints, entry)
			cnt++;
		if (cnt > 0 && ++total_fails >= cnt &&
		    conf->constraint_median == 0) {
			log_warnx("constraints configured but none available");
			total_fails = 0;
		}
	}

	if (fail || cstr->state < STATE_QUERY_SENT) {
		cstr->senderrors++;
		constraint_close(cstr->id);
	}
}

void
constraint_msg_dns(u_int32_t id, u_int8_t *data, size_t len)
{
	struct constraint	*cstr, *ncstr = NULL;
	u_int8_t		*p;
	struct ntp_addr		*h;

	if ((cstr = constraint_byid(id)) == NULL) {
		log_debug("IMSG_CONSTRAINT_DNS with invalid constraint id");
		return;
	}
	if (cstr->addr != NULL) {
		log_warnx("IMSG_CONSTRAINT_DNS but addr != NULL!");
		return;
	}
	if (len == 0) {
		log_debug("%s FAILED", __func__);
		cstr->state = STATE_DNS_TEMPFAIL;
		return;
	}

	if (len % (sizeof(struct sockaddr_storage) + sizeof(int)) != 0)
		fatalx("IMSG_CONSTRAINT_DNS len");

	if (cstr->addr_head.pool) {
		struct constraint *n, *tmp;
		TAILQ_FOREACH_SAFE(n, &conf->constraints, entry, tmp) {
			if (cstr->id == n->id)
				continue;
			if (cstr->addr_head.pool == n->addr_head.pool)
				constraint_remove(n);
		}
	}

	p = data;
	do {
		if ((h = calloc(1, sizeof(*h))) == NULL)
			fatal("calloc ntp_addr");
		memcpy(&h->ss, p, sizeof(h->ss));
		p += sizeof(h->ss);
		len -= sizeof(h->ss);
		memcpy(&h->notauth, p, sizeof(int));
		p += sizeof(int);
		len -= sizeof(int);

		if (ncstr == NULL || cstr->addr_head.pool) {
			ncstr = new_constraint();
			ncstr->addr = h;
			ncstr->addr_head.a = h;
			ncstr->addr_head.name = strdup(cstr->addr_head.name);
			ncstr->addr_head.path = strdup(cstr->addr_head.path);
			if (ncstr->addr_head.name == NULL ||
			    ncstr->addr_head.path == NULL)
				fatal("calloc name");
			ncstr->addr_head.pool = cstr->addr_head.pool;
			ncstr->state = STATE_DNS_DONE;
			constraint_add(ncstr);
			constraint_cnt += constraint_init(ncstr);
		} else {
			h->next = ncstr->addr;
			ncstr->addr = h;
			ncstr->addr_head.a = h;
		}
	} while (len);

	constraint_remove(cstr);
}

int
constraint_cmp(const void *a, const void *b)
{
	time_t at = *(const time_t *)a;
	time_t bt = *(const time_t *)b;
	return at < bt ? -1 : (at > bt ? 1 : 0);
}

void
constraint_update(void)
{
	struct constraint *cstr;
	int	 cnt, i;
	time_t	*values;
	time_t	 now;

	now = getmonotime();

	cnt = 0;
	TAILQ_FOREACH(cstr, &conf->constraints, entry) {
		if (cstr->state != STATE_REPLY_RECEIVED)
			continue;
		cnt++;
	}
	if (cnt == 0)
		return;

	if ((values = calloc(cnt, sizeof(time_t))) == NULL)
		fatal("calloc");

	i = 0;
	TAILQ_FOREACH(cstr, &conf->constraints, entry) {
		if (cstr->state != STATE_REPLY_RECEIVED)
			continue;
		values[i++] = cstr->constraint + (now - cstr->last);
	}

	qsort(values, cnt, sizeof(time_t), constraint_cmp);

	/* calculate median */
	i = cnt / 2;
	if (cnt % 2 == 0)
		conf->constraint_median = (values[i - 1] + values[i]) / 2;
	else
		conf->constraint_median = values[i];

	conf->constraint_last = now;

	free(values);
}

void
constraint_reset(void)
{
	struct constraint *cstr;

	TAILQ_FOREACH(cstr, &conf->constraints, entry) {
		if (cstr->state == STATE_QUERY_SENT)
			continue;
		constraint_close(cstr->id);
		constraint_addr_head_clear(cstr);
		constraint_init(cstr);
	}
	conf->constraint_errors = 0;
}

int
constraint_check(double val)
{
	struct timeval	tv;
	double		diff;
	time_t		now;

	if (conf->constraint_median == 0)
		return (0);

	/* Calculate the constraint with the current offset */
	now = getmonotime();
	tv.tv_sec = conf->constraint_median + (now - conf->constraint_last);
	tv.tv_usec = 0;
	diff = fabs(val - gettime_from_timeval(&tv));

	if (diff > CONSTRAINT_MARGIN) {
		if (conf->constraint_errors++ >
		    (CONSTRAINT_ERROR_MARGIN * peer_cnt)) {
			constraint_reset();
		}

		return (-1);
	}

	return (0);
}

struct httpsdate *
httpsdate_init(const char *addr, const char *port, const char *hostname,
    const char *path, const u_int8_t *ca, size_t ca_len, int synced)
{
	struct httpsdate	*httpsdate = NULL;

	if ((httpsdate = calloc(1, sizeof(*httpsdate))) == NULL)
		goto fail;

	if (hostname == NULL)
		hostname = addr;

	if ((httpsdate->tls_addr = strdup(addr)) == NULL ||
	    (httpsdate->tls_port = strdup(port)) == NULL ||
	    (httpsdate->tls_hostname = strdup(hostname)) == NULL ||
	    (httpsdate->tls_path = strdup(path)) == NULL)
		goto fail;

	if (asprintf(&httpsdate->tls_request,
	    "HEAD %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
	    httpsdate->tls_path, httpsdate->tls_hostname) == -1)
		goto fail;

	if ((httpsdate->tls_config = tls_config_new()) == NULL)
		goto fail;
	if (tls_config_set_ca_mem(httpsdate->tls_config, ca, ca_len) == -1)
		goto fail;

	/*
	 * Due to the fact that we're trying to determine a constraint for time
	 * we do our own certificate validity checking, since the automatic
	 * version is based on our wallclock, which may well be inaccurate...
	 */
	if (!synced) {
		log_debug("constraints: using received time in certificate validation");
		tls_config_insecure_noverifytime(httpsdate->tls_config);
	}

	return (httpsdate);

 fail:
	httpsdate_free(httpsdate);
	return (NULL);
}

void
httpsdate_free(void *arg)
{
	struct httpsdate *httpsdate = arg;
	if (httpsdate == NULL)
		return;
	if (httpsdate->tls_ctx)
		tls_close(httpsdate->tls_ctx);
	tls_free(httpsdate->tls_ctx);
	tls_config_free(httpsdate->tls_config);
	free(httpsdate->tls_addr);
	free(httpsdate->tls_port);
	free(httpsdate->tls_hostname);
	free(httpsdate->tls_path);
	free(httpsdate->tls_request);
	free(httpsdate);
}

int
httpsdate_request(struct httpsdate *httpsdate, struct timeval *when, int synced)
{
	char	 timebuf1[32], timebuf2[32];
	size_t	 outlen = 0, maxlength = CONSTRAINT_MAXHEADERLENGTH, len;
	char	*line, *p, *buf;
	time_t	 httptime, notbefore, notafter;
	struct tm *tm;
	ssize_t	 ret;

	if ((httpsdate->tls_ctx = tls_client()) == NULL)
		goto fail;

	if (tls_configure(httpsdate->tls_ctx, httpsdate->tls_config) == -1)
		goto fail;

	/*
	 * libtls expects an address string, which can also be a DNS name,
	 * but we pass a pre-resolved IP address string in tls_addr so it
	 * does not trigger any DNS operation and is safe to be called
	 * without the dns pledge.
	 */
	if (tls_connect_servername(httpsdate->tls_ctx, httpsdate->tls_addr,
	    httpsdate->tls_port, httpsdate->tls_hostname) == -1) {
		log_debug("tls connect failed: %s (%s): %s",
		    httpsdate->tls_addr, httpsdate->tls_hostname,
		    tls_error(httpsdate->tls_ctx));
		goto fail;
	}

	buf = httpsdate->tls_request;
	len = strlen(httpsdate->tls_request);
	while (len > 0) {
		ret = tls_write(httpsdate->tls_ctx, buf, len);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT)
			continue;
		if (ret == -1) {
			log_warnx("tls write failed: %s (%s): %s",
			    httpsdate->tls_addr, httpsdate->tls_hostname,
			    tls_error(httpsdate->tls_ctx));
			goto fail;
		}
		buf += ret;
		len -= ret;
	}

	while ((line = tls_readline(httpsdate->tls_ctx, &outlen,
	    &maxlength, when)) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';

		if ((p = strchr(line, ' ')) == NULL || *p == '\0')
			goto next;
		*p++ = '\0';
		if (strcasecmp("Date:", line) != 0)
			goto next;

		/*
		 * Expect the date/time format as IMF-fixdate which is
		 * mandated by HTTP/1.1 in the new RFC 7231 and was
		 * preferred by RFC 2616.  Other formats would be RFC 850
		 * or ANSI C's asctime() - the latter doesn't include
		 * the timezone which is required here.
		 */
		if (strptime(p, IMF_FIXDATE,
		    &httpsdate->tls_tm) == NULL) {
			log_warnx("unsupported date format");
			free(line);
			goto fail;
		}

		free(line);
		break;
 next:
		free(line);
	}
	if (httpsdate->tls_tm.tm_year == 0)
		goto fail;

	/* If we are synced, we already checked the certificate validity */
	if (synced)
		return 0;

	/*
	 * Now manually check the validity of the certificate presented in the
	 * TLS handshake, based on the time specified by the server's HTTP Date:
	 * header.
	 */
	notbefore = tls_peer_cert_notbefore(httpsdate->tls_ctx);
	notafter = tls_peer_cert_notafter(httpsdate->tls_ctx);
	if ((httptime = timegm(&httpsdate->tls_tm)) == -1)
		goto fail;
	if (httptime <= notbefore) {
		if ((tm = gmtime(&notbefore)) == NULL)
			goto fail;
		if (strftime(timebuf1, sizeof(timebuf1), X509_DATE, tm) == 0)
			goto fail;
		if (strftime(timebuf2, sizeof(timebuf2), X509_DATE,
		    &httpsdate->tls_tm) == 0)
			goto fail;
		log_warnx("tls certificate not yet valid: %s (%s): "
		    "not before %s, now %s", httpsdate->tls_addr,
		    httpsdate->tls_hostname, timebuf1, timebuf2);
		goto fail;
	}
	if (httptime >= notafter) {
		if ((tm = gmtime(&notafter)) == NULL)
			goto fail;
		if (strftime(timebuf1, sizeof(timebuf1), X509_DATE, tm) == 0)
			goto fail;
		if (strftime(timebuf2, sizeof(timebuf2), X509_DATE,
		    &httpsdate->tls_tm) == 0)
			goto fail;
		log_warnx("tls certificate expired: %s (%s): "
		    "not after %s, now %s", httpsdate->tls_addr,
		    httpsdate->tls_hostname, timebuf1, timebuf2);
		goto fail;
	}

	return (0);

 fail:
	httpsdate_free(httpsdate);
	return (-1);
}

void *
httpsdate_query(const char *addr, const char *port, const char *hostname,
    const char *path, const u_int8_t *ca, size_t ca_len,
    struct timeval *rectv, struct timeval *xmttv, int synced)
{
	struct httpsdate	*httpsdate;
	struct timeval		 when;
	time_t			 t;

	if ((httpsdate = httpsdate_init(addr, port, hostname, path,
	    ca, ca_len, synced)) == NULL)
		return (NULL);

	if (httpsdate_request(httpsdate, &when, synced) == -1)
		return (NULL);

	/* Return parsed date as local time */
	t = timegm(&httpsdate->tls_tm);

	/* Report parsed Date: as "received time" */
	rectv->tv_sec = t;
	rectv->tv_usec = 0;

	/* And add delay as "transmit time" */
	xmttv->tv_sec = when.tv_sec;
	xmttv->tv_usec = when.tv_usec;

	return (httpsdate);
}

/* Based on SSL_readline in ftp/fetch.c */
char *
tls_readline(struct tls *tls, size_t *lenp, size_t *maxlength,
    struct timeval *when)
{
	size_t i, len;
	char *buf, *q, c;
	ssize_t ret;

	len = 128;
	if ((buf = malloc(len)) == NULL)
		fatal("Can't allocate memory for transfer buffer");
	for (i = 0; ; i++) {
		if (i >= len - 1) {
			if ((q = reallocarray(buf, len, 2)) == NULL)
				fatal("Can't expand transfer buffer");
			buf = q;
			len *= 2;
		}
 again:
		ret = tls_read(tls, &c, 1);
		if (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT)
			goto again;
		if (ret == -1) {
			/* SSL read error, ignore */
			free(buf);
			return (NULL);
		}

		if (maxlength != NULL && (*maxlength)-- == 0) {
			log_warnx("maximum length exceeded");
			free(buf);
			return (NULL);
		}

		buf[i] = c;
		if (c == '\n')
			break;
	}
	*lenp = i;
	if (gettimeofday(when, NULL) == -1)
		fatal("gettimeofday");
	return (buf);
}

char *
get_string(u_int8_t *ptr, size_t len)
{
	size_t	 i;

	for (i = 0; i < len; i++)
		if (!(isprint(ptr[i]) || isspace(ptr[i])))
			break;

	return strndup(ptr, i);
}
