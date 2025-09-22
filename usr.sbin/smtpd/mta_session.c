/*	$OpenBSD: mta_session.c,v 1.152 2024/09/03 18:27:04 op Exp $	*/

/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
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

#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tls.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

#define MAX_TRYBEFOREDISABLE	10

#define MTA_HIWAT		65535

enum mta_state {
	MTA_INIT,
	MTA_BANNER,
	MTA_EHLO,
	MTA_HELO,
	MTA_LHLO,
	MTA_STARTTLS,
	MTA_AUTH,
	MTA_AUTH_PLAIN,
	MTA_AUTH_LOGIN,
	MTA_AUTH_LOGIN_USER,
	MTA_AUTH_LOGIN_PASS,
	MTA_READY,
	MTA_MAIL,
	MTA_RCPT,
	MTA_DATA,
	MTA_BODY,
	MTA_EOM,
	MTA_LMTP_EOM,
	MTA_RSET,
	MTA_QUIT,
};

#define MTA_FORCE_ANYSSL	0x0001
#define MTA_FORCE_SMTPS		0x0002
#define MTA_FORCE_TLS     	0x0004
#define MTA_FORCE_PLAIN		0x0008
#define MTA_WANT_SECURE		0x0010
#define MTA_DOWNGRADE_PLAIN    	0x0080

#define MTA_TLS			0x0100
#define MTA_TLS_VERIFIED	0x0200

#define MTA_FREE		0x0400
#define MTA_LMTP		0x0800
#define MTA_WAIT		0x1000
#define MTA_HANGON		0x2000
#define MTA_RECONN		0x4000

#define MTA_EXT_STARTTLS	0x01
#define MTA_EXT_PIPELINING	0x02
#define MTA_EXT_AUTH		0x04
#define MTA_EXT_AUTH_PLAIN     	0x08
#define MTA_EXT_AUTH_LOGIN     	0x10
#define MTA_EXT_SIZE     	0x20

struct mta_session {
	uint64_t		 id;
	struct mta_relay	*relay;
	struct mta_route	*route;
	char			*helo;
	char			*mxname;

	char			*username;

	int			 flags;

	int			 attempt;
	int			 use_smtps;
	int			 use_starttls;
	int			 use_smtp_tls;
	int			 ready;

	struct event		 ev;
	struct io		*io;
	int			 ext;

	size_t			 ext_size;

	size_t			 msgtried;
	size_t			 msgcount;
	size_t			 rcptcount;
	int			 hangon;

	enum mta_state		 state;
	struct mta_task		*task;
	struct mta_envelope	*currevp;
	FILE			*datafp;
	size_t			 datalen;

	size_t			 failures;

	char			 replybuf[2048];
};

static void mta_session_init(void);
static void mta_start(int fd, short ev, void *arg);
static void mta_io(struct io *, int, void *);
static void mta_free(struct mta_session *);
static void mta_getnameinfo_cb(void *, int, const char *, const char *);
static void mta_on_ptr(void *, void *, void *);
static void mta_on_timeout(struct runq *, void *);
static void mta_connect(struct mta_session *);
static void mta_enter_state(struct mta_session *, int);
static void mta_flush_task(struct mta_session *, int, const char *, size_t, int);
static void mta_error(struct mta_session *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
static void mta_send(struct mta_session *, char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
static ssize_t mta_queue_data(struct mta_session *);
static void mta_response(struct mta_session *, char *);
static const char * mta_strstate(int);
static void mta_tls_init(struct mta_session *);
static void mta_tls_started(struct mta_session *);
static struct mta_session *mta_tree_pop(struct tree *, uint64_t);
static const char * dsn_strret(enum dsn_ret);
static const char * dsn_strnotify(uint8_t);

void mta_hoststat_update(const char *, const char *);
void mta_hoststat_reschedule(const char *);
void mta_hoststat_cache(const char *, uint64_t);
void mta_hoststat_uncache(const char *, uint64_t);


static void mta_filter_begin(struct mta_session *);
static void mta_filter_end(struct mta_session *);
static void mta_connected(struct mta_session *);
static void mta_disconnected(struct mta_session *);

static void mta_report_link_connect(struct mta_session *, const char *, int,
    const struct sockaddr_storage *,
    const struct sockaddr_storage *);
static void mta_report_link_greeting(struct mta_session *, const char *);
static void mta_report_link_identify(struct mta_session *, const char *, const char *);
static void mta_report_link_tls(struct mta_session *, const char *);
static void mta_report_link_disconnect(struct mta_session *);
static void mta_report_link_auth(struct mta_session *, const char *, const char *);
static void mta_report_tx_reset(struct mta_session *, uint32_t);
static void mta_report_tx_begin(struct mta_session *, uint32_t);
static void mta_report_tx_mail(struct mta_session *, uint32_t, const char *, int);
static void mta_report_tx_rcpt(struct mta_session *, uint32_t, const char *, int);
static void mta_report_tx_envelope(struct mta_session *, uint32_t, uint64_t);
static void mta_report_tx_data(struct mta_session *, uint32_t, int);
static void mta_report_tx_commit(struct mta_session *, uint32_t, size_t);
static void mta_report_tx_rollback(struct mta_session *, uint32_t);
static void mta_report_protocol_client(struct mta_session *, const char *);
static void mta_report_protocol_server(struct mta_session *, const char *);
#if 0
static void mta_report_filter_response(struct mta_session *, int, int, const char *);
#endif
static void mta_report_timeout(struct mta_session *);


static struct tree wait_helo;
static struct tree wait_ptr;
static struct tree wait_fd;
static struct tree wait_tls_init;
static struct tree wait_tls_verify;

static struct runq *hangon;

#define	SESSION_FILTERED(s) \
	((s)->relay->dispatcher->u.remote.filtername)

static void
mta_session_init(void)
{
	static int init = 0;

	if (!init) {
		tree_init(&wait_helo);
		tree_init(&wait_ptr);
		tree_init(&wait_fd);
		tree_init(&wait_tls_init);
		tree_init(&wait_tls_verify);
		runq_init(&hangon, mta_on_timeout);
		init = 1;
	}
}

void
mta_session(struct mta_relay *relay, struct mta_route *route, const char *mxname)
{
	struct mta_session	*s;
	struct timeval		 tv;

	mta_session_init();

	s = xcalloc(1, sizeof *s);
	s->id = generate_uid();
	s->relay = relay;
	s->route = route;
	s->mxname = xstrdup(mxname);

	mta_filter_begin(s);

	if (relay->flags & RELAY_LMTP)
		s->flags |= MTA_LMTP;
	switch (relay->tls) {
	case RELAY_TLS_SMTPS:
		s->flags |= MTA_FORCE_SMTPS;
		s->flags |= MTA_WANT_SECURE;
		break;
	case RELAY_TLS_STARTTLS:
		s->flags |= MTA_FORCE_TLS;
		s->flags |= MTA_WANT_SECURE;
		break;
	case RELAY_TLS_OPPORTUNISTIC:
		/* do not force anything, try tls then smtp */
		break;
	case RELAY_TLS_NO:
		s->flags |= MTA_FORCE_PLAIN;
		break;
	default:
		fatalx("bad value for relay->tls: %d", relay->tls);
	}

	log_debug("debug: mta: %p: spawned for relay %s", s,
	    mta_relay_to_text(relay));
	stat_increment("mta.session", 1);

	if (route->dst->ptrname || route->dst->lastptrquery) {
		/* We want to delay the connection since to always notify
		 * the relay asynchronously.
		 */
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		evtimer_set(&s->ev, mta_start, s);
		evtimer_add(&s->ev, &tv);
	} else if (waitq_wait(&route->dst->ptrname, mta_on_ptr, s)) {
		resolver_getnameinfo(s->route->dst->sa, NI_NUMERICSERV,
		    mta_getnameinfo_cb, s);
	}
}

void
mta_session_imsg(struct mproc *p, struct imsg *imsg)
{
	struct mta_session	*s;
	struct msg		 m;
	uint64_t		 reqid;
	const char		*name;
	int			 status, fd;
	struct stat		 sb;
	
	switch (imsg->hdr.type) {

	case IMSG_MTA_OPEN_MESSAGE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);

		fd = imsg_get_fd(imsg);
		s = mta_tree_pop(&wait_fd, reqid);
		if (s == NULL) {
			if (fd != -1)
				close(fd);
			return;
		}

		if (fd == -1) {
			log_debug("debug: mta: failed to obtain msg fd");
			mta_flush_task(s, IMSG_MTA_DELIVERY_TEMPFAIL,
			    "Could not get message fd", 0, 0);
			mta_enter_state(s, MTA_READY);
			return;
		}

		if ((s->ext & MTA_EXT_SIZE) && s->ext_size != 0) {
			if (fstat(fd, &sb) == -1) {
				log_debug("debug: mta: failed to stat msg fd");
				mta_flush_task(s, IMSG_MTA_DELIVERY_TEMPFAIL,
				    "Could not stat message fd", 0, 0);
				mta_enter_state(s, MTA_READY);
				close(fd);
				return;
			}
			if (sb.st_size > (off_t)s->ext_size) {
				log_debug("debug: mta: message too large for peer");
				mta_flush_task(s, IMSG_MTA_DELIVERY_PERMFAIL,
				    "message too large for peer", 0, 0);
				mta_enter_state(s, MTA_READY);
				close(fd);
				return;
			}
		}
		
		s->datafp = fdopen(fd, "r");
		if (s->datafp == NULL)
			fatal("mta: fdopen");

		mta_enter_state(s, MTA_MAIL);
		return;

	case IMSG_MTA_LOOKUP_HELO:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &status);
		if (status == LKA_OK)
			m_get_string(&m, &name);
		m_end(&m);

		s = mta_tree_pop(&wait_helo, reqid);
		if (s == NULL)
			return;

		if (status == LKA_OK) {
			s->helo = xstrdup(name);
			mta_connect(s);
		} else {
			mta_source_error(s->relay, s->route,
			    "Failed to retrieve helo string");
			mta_free(s);
		}
		return;

	default:
		fatalx("mta_session_imsg: unexpected %s imsg",
		    imsg_to_str(imsg->hdr.type));
	}
}

static struct mta_session *
mta_tree_pop(struct tree *wait, uint64_t reqid)
{
	struct mta_session *s;

	s = tree_xpop(wait, reqid);
	if (s->flags & MTA_FREE) {
		log_debug("debug: mta: %p: zombie session", s);
		mta_free(s);
		return (NULL);
	}
	s->flags &= ~MTA_WAIT;

	return (s);
}

static void
mta_free(struct mta_session *s)
{
	struct mta_relay *relay;
	struct mta_route *route;

	log_debug("debug: mta: %p: session done", s);

	mta_disconnected(s);

	if (s->ready)
		s->relay->nconn_ready -= 1;

	if (s->flags & MTA_HANGON) {
		log_debug("debug: mta: %p: cancelling hangon timer", s);
		runq_cancel(hangon, s);
	}

	if (s->io)
		io_free(s->io);

	if (s->task)
		fatalx("current task should have been deleted already");
	if (s->datafp) {
		fclose(s->datafp);
		s->datalen = 0;
	}
	free(s->helo);

	relay = s->relay;
	route = s->route;
	free(s->username);
	free(s->mxname);
	free(s);
	stat_decrement("mta.session", 1);
	mta_route_collect(relay, route);
}

static void
mta_getnameinfo_cb(void *arg, int gaierrno, const char *host, const char *serv)
{
	struct mta_session *s = arg;
	struct mta_host *h;

	h = s->route->dst;
	h->lastptrquery = time(NULL);
	if (host)
		h->ptrname = xstrdup(host);
	waitq_run(&h->ptrname, h->ptrname);
}

static void
mta_on_timeout(struct runq *runq, void *arg)
{
	struct mta_session *s = arg;

	log_debug("mta: timeout for session hangon");

	s->flags &= ~MTA_HANGON;
	s->hangon++;

	mta_enter_state(s, MTA_READY);
}

static void
mta_on_ptr(void *tag, void *arg, void *data)
{
	struct mta_session *s = arg;

	mta_connect(s);
}

static void
mta_start(int fd, short ev, void *arg)
{
	struct mta_session *s = arg;

	mta_connect(s);
}

static void
mta_connect(struct mta_session *s)
{
	struct sockaddr_storage	 ss;
	struct sockaddr		*sa;
	int			 portno;
	const char		*schema;

	if (s->helo == NULL) {
		if (s->relay->helotable && s->route->src->sa) {
			m_create(p_lka, IMSG_MTA_LOOKUP_HELO, 0, 0, -1);
			m_add_id(p_lka, s->id);
			m_add_string(p_lka, s->relay->helotable);
			m_add_sockaddr(p_lka, s->route->src->sa);
			m_close(p_lka);
			tree_xset(&wait_helo, s->id, s);
			s->flags |= MTA_WAIT;
			return;
		}
		else if (s->relay->heloname)
			s->helo = xstrdup(s->relay->heloname);
		else
			s->helo = xstrdup(env->sc_hostname);
	}

	if (s->io) {
		io_free(s->io);
		s->io = NULL;
	}

	s->use_smtps = s->use_starttls = s->use_smtp_tls = 0;

	switch (s->attempt) {
	case 0:
		if (s->flags & MTA_FORCE_SMTPS)
			s->use_smtps = 1;	/* smtps */
		else if (s->flags & (MTA_FORCE_TLS|MTA_FORCE_ANYSSL))
			s->use_starttls = 1;	/* tls, tls+smtps */
		else if (!(s->flags & MTA_FORCE_PLAIN))
			s->use_smtp_tls = 1;
		break;
	case 1:
		if (s->flags & MTA_FORCE_ANYSSL) {
			s->use_smtps = 1;	/* tls+smtps */
			break;
		}
		else if (s->flags & MTA_DOWNGRADE_PLAIN) {
			/* smtp, with tls failure */
			break;
		}
	default:
		mta_free(s);
		return;
	}
	portno = s->use_smtps ? 465 : 25;

	/* Override with relay-specified port */
	if (s->relay->port)
		portno = s->relay->port;

	memmove(&ss, s->route->dst->sa, s->route->dst->sa->sa_len);
	sa = (struct sockaddr *)&ss;

	if (sa->sa_family == AF_INET)
		((struct sockaddr_in *)sa)->sin_port = htons(portno);
	else if (sa->sa_family == AF_INET6)
		((struct sockaddr_in6 *)sa)->sin6_port = htons(portno);

	s->attempt += 1;
	if (s->use_smtp_tls)
		schema = "smtp://";
	else if (s->use_starttls)
		schema = "smtp+tls://";
	else if (s->use_smtps)
		schema = "smtps://";
	else if (s->flags & MTA_LMTP)
		schema = "lmtp://";
	else
		schema = "smtp+notls://";

	log_info("%016"PRIx64" mta "
	    "connecting address=%s%s:%d host=%s",
	    s->id, schema, sa_to_text(s->route->dst->sa),
	    portno, s->route->dst->ptrname);

	mta_enter_state(s, MTA_INIT);
	s->io = io_new();
	io_set_callback(s->io, mta_io, s);
	io_set_timeout(s->io, 300000);
	if (io_connect(s->io, sa, s->route->src->sa) == -1) {
		/*
		 * This error is most likely a "no route",
		 * so there is no need to try again.
		 */
		log_debug("debug: mta: io_connect failed: %s", io_error(s->io));
		if (errno == EADDRNOTAVAIL)
			mta_source_error(s->relay, s->route, io_error(s->io));
		else
			mta_error(s, "Connection failed: %s", io_error(s->io));
		mta_free(s);
	}
}

static void
mta_enter_state(struct mta_session *s, int newstate)
{
	struct mta_envelope	 *e;
	size_t			 envid_sz;
	int			 oldstate;
	ssize_t			 q;
	char			 ibuf[LINE_MAX];
	char			 obuf[LINE_MAX];
	int			 offset;
	const char     		*srs_sender;

again:
	oldstate = s->state;

	log_trace(TRACE_MTA, "mta: %p: %s -> %s", s,
	    mta_strstate(oldstate),
	    mta_strstate(newstate));

	s->state = newstate;

	memset(s->replybuf, 0, sizeof s->replybuf);

	/* don't try this at home! */
#define mta_enter_state(_s, _st) do { newstate = _st; goto again; } while (0)

	switch (s->state) {
	case MTA_INIT:
	case MTA_BANNER:
		break;

	case MTA_EHLO:
		s->ext = 0;
		mta_send(s, "EHLO %s", s->helo);
		mta_report_link_identify(s, "EHLO", s->helo);
		break;

	case MTA_HELO:
		s->ext = 0;
		mta_send(s, "HELO %s", s->helo);
		mta_report_link_identify(s, "HELO", s->helo);
		break;

	case MTA_LHLO:
		s->ext = 0;
		mta_send(s, "LHLO %s", s->helo);
		mta_report_link_identify(s, "LHLO", s->helo);
		break;

	case MTA_STARTTLS:
		if (s->flags & MTA_DOWNGRADE_PLAIN)
			mta_enter_state(s, MTA_AUTH);
		if (s->flags & MTA_TLS) /* already started */
			mta_enter_state(s, MTA_AUTH);
		else if ((s->ext & MTA_EXT_STARTTLS) == 0) {
			if (s->flags & MTA_FORCE_TLS || s->flags & MTA_WANT_SECURE) {
				mta_error(s, "TLS required but not supported by remote host");
				s->flags |= MTA_RECONN;
			}
			else
				/* server doesn't support starttls, do not use it */
				mta_enter_state(s, MTA_AUTH);
		}
		else
			mta_send(s, "STARTTLS");
		break;

	case MTA_AUTH:
		if (s->relay->secret && s->flags & MTA_TLS) {
			if (s->ext & MTA_EXT_AUTH) {
				if (s->ext & MTA_EXT_AUTH_PLAIN) {
					mta_enter_state(s, MTA_AUTH_PLAIN);
					break;
				}
				if (s->ext & MTA_EXT_AUTH_LOGIN) {
					mta_enter_state(s, MTA_AUTH_LOGIN);
					break;
				}
				log_debug("debug: mta: %p: no supported AUTH method on session", s);
				mta_error(s, "no supported AUTH method");
			}
			else {
				log_debug("debug: mta: %p: AUTH not advertised on session", s);
				mta_error(s, "AUTH not advertised");
			}
		}
		else if (s->relay->secret) {
			log_debug("debug: mta: %p: not using AUTH on non-TLS "
			    "session", s);
			mta_error(s, "Refuse to AUTH over insecure channel");
			mta_connect(s);
		} else {
			mta_enter_state(s, MTA_READY);
		}
		break;

	case MTA_AUTH_PLAIN:
		memset(ibuf, 0, sizeof ibuf);
		if (base64_decode(s->relay->secret, (unsigned char *)ibuf,
				  sizeof(ibuf)-1) == -1) {
			log_debug("debug: mta: %p: credentials too large on session", s);
			mta_error(s, "Credentials too large");
			break;
		}
		s->username = xstrdup(ibuf+1);
		mta_send(s, "AUTH PLAIN %s", s->relay->secret);
		break;

	case MTA_AUTH_LOGIN:
		mta_send(s, "AUTH LOGIN");
		break;

	case MTA_AUTH_LOGIN_USER:
		memset(ibuf, 0, sizeof ibuf);
		if (base64_decode(s->relay->secret, (unsigned char *)ibuf,
				  sizeof(ibuf)-1) == -1) {
			log_debug("debug: mta: %p: credentials too large on session", s);
			mta_error(s, "Credentials too large");
			break;
		}
		s->username = xstrdup(ibuf+1);

		memset(obuf, 0, sizeof obuf);
		base64_encode((unsigned char *)ibuf + 1, strlen(ibuf + 1), obuf, sizeof obuf);
		mta_send(s, "%s", obuf);

		memset(ibuf, 0, sizeof ibuf);
		memset(obuf, 0, sizeof obuf);
		break;

	case MTA_AUTH_LOGIN_PASS:
		memset(ibuf, 0, sizeof ibuf);
		if (base64_decode(s->relay->secret, (unsigned char *)ibuf,
				  sizeof(ibuf)-1) == -1) {
			log_debug("debug: mta: %p: credentials too large on session", s);
			mta_error(s, "Credentials too large");
			break;
		}

		offset = strlen(ibuf+1)+2;
		memset(obuf, 0, sizeof obuf);
		base64_encode((unsigned char *)ibuf + offset, strlen(ibuf + offset), obuf, sizeof obuf);
		mta_send(s, "%s", obuf);

		memset(ibuf, 0, sizeof ibuf);
		memset(obuf, 0, sizeof obuf);
		break;

	case MTA_READY:
		/* Ready to send a new mail */
		if (s->ready == 0) {
			s->ready = 1;
			s->relay->nconn_ready += 1;
			mta_route_ok(s->relay, s->route);
		}

		if (s->msgtried >= MAX_TRYBEFOREDISABLE) {
			log_info("%016"PRIx64" mta host-rejects-all-mails",
			    s->id);
			mta_route_down(s->relay, s->route);
			mta_enter_state(s, MTA_QUIT);
			break;
		}

		if (s->msgcount >= s->relay->limits->max_mail_per_session) {
			log_debug("debug: mta: "
			    "%p: cannot send more message to relay %s", s,
			    mta_relay_to_text(s->relay));
			mta_enter_state(s, MTA_QUIT);
			break;
		}

		/*
		 * When downgrading from opportunistic TLS, clear flag and
		 * possibly reuse the same task (forbidden in other cases).
		 */
		if (s->flags & MTA_DOWNGRADE_PLAIN)
			s->flags &= ~MTA_DOWNGRADE_PLAIN;
		else if (s->task)
			fatalx("task should be NULL at this point");

		if (s->task == NULL)
			s->task = mta_route_next_task(s->relay, s->route);
		if (s->task == NULL) {
			log_debug("debug: mta: %p: no task for relay %s",
			    s, mta_relay_to_text(s->relay));

			if (s->relay->nconn > 1 ||
			    s->hangon >= s->relay->limits->sessdelay_keepalive) {
				mta_enter_state(s, MTA_QUIT);
				break;
			}

			log_debug("mta: debug: last connection: hanging on for %llds",
			    (long long)(s->relay->limits->sessdelay_keepalive -
			    s->hangon));
			s->flags |= MTA_HANGON;
			runq_schedule(hangon, 1, s);
			break;
		}

		log_debug("debug: mta: %p: handling next task for relay %s", s,
			    mta_relay_to_text(s->relay));

		stat_increment("mta.task.running", 1);

		m_create(p_queue, IMSG_MTA_OPEN_MESSAGE, 0, 0, -1);
		m_add_id(p_queue, s->id);
		m_add_msgid(p_queue, s->task->msgid);
		m_close(p_queue);

		tree_xset(&wait_fd, s->id, s);
		s->flags |= MTA_WAIT;
		break;

	case MTA_MAIL:
		s->currevp = TAILQ_FIRST(&s->task->envelopes);

		e = s->currevp;
		s->hangon = 0;
		s->msgtried++;
		envid_sz = strlen(e->dsn_envid);

		/* SRS-encode if requested for the relay action, AND we're not
		 * bouncing, AND we have an RCPT which means we are forwarded,
		 * AND the RCPT has a '@' just for sanity check (will always).
		 */
		if (env->sc_srs_key != NULL &&
		    s->relay->srs &&
		    strchr(s->task->sender, '@') &&
		    e->rcpt &&
		    strchr(e->rcpt, '@')) {
			/* encode and replace task sender with new SRS-sender */
			srs_sender = srs_encode(s->task->sender,
			    strchr(e->rcpt, '@') + 1);
			if (srs_sender) {
				free(s->task->sender);
				s->task->sender = xstrdup(srs_sender);
			}
		}

		if (s->ext & MTA_EXT_DSN) {
			mta_send(s, "MAIL FROM:<%s>%s%s%s%s",
			    s->task->sender,
			    e->dsn_ret ? " RET=" : "",
			    e->dsn_ret ? dsn_strret(e->dsn_ret) : "",
			    envid_sz ? " ENVID=" : "",
			    envid_sz ? e->dsn_envid : "");
		} else
			mta_send(s, "MAIL FROM:<%s>", s->task->sender);
		break;

	case MTA_RCPT:
		if (s->currevp == NULL)
			s->currevp = TAILQ_FIRST(&s->task->envelopes);

		e = s->currevp;
		if (s->ext & MTA_EXT_DSN) {
			mta_send(s, "RCPT TO:<%s>%s%s%s%s",
			    e->dest,
			    e->dsn_notify ? " NOTIFY=" : "",
			    e->dsn_notify ? dsn_strnotify(e->dsn_notify) : "",
			    e->dsn_orcpt ? " ORCPT=" : "",
			    e->dsn_orcpt ? e->dsn_orcpt : "");
		} else
			mta_send(s, "RCPT TO:<%s>", e->dest);

		mta_report_tx_envelope(s, s->task->msgid, e->id);
		s->rcptcount++;
		break;

	case MTA_DATA:
		fseek(s->datafp, 0, SEEK_SET);
		mta_send(s, "DATA");
		break;

	case MTA_BODY:
		if (s->datafp == NULL) {
			log_trace(TRACE_MTA, "mta: %p: end-of-file", s);
			mta_enter_state(s, MTA_EOM);
			break;
		}

		if ((q = mta_queue_data(s)) == -1) {
			s->flags |= MTA_FREE;
			break;
		}
		if (q == 0) {
			mta_enter_state(s, MTA_BODY);
			break;
		}

		log_trace(TRACE_MTA, "mta: %p: >>> [...%zd bytes...]", s, q);
		break;

	case MTA_EOM:
		mta_send(s, ".");
		break;

	case MTA_LMTP_EOM:
		/* LMTP reports status of each delivery, so enable read */
		io_set_read(s->io);
		break;

	case MTA_RSET:
		if (s->datafp) {
			fclose(s->datafp);
			s->datafp = NULL;
			s->datalen = 0;
		}
		mta_send(s, "RSET");
		break;

	case MTA_QUIT:
		mta_send(s, "QUIT");
		break;

	default:
		fatalx("mta_enter_state: unknown state");
	}
#undef mta_enter_state
}

/*
 * Handle a response to an SMTP command
 */
static void
mta_response(struct mta_session *s, char *line)
{
	struct mta_envelope	*e;
	struct sockaddr_storage	 ss;
	struct sockaddr		*sa;
	const char		*domain;
	char			*pbuf;
	socklen_t		 sa_len;
	char			 buf[LINE_MAX];
	int			 delivery;

	switch (s->state) {

	case MTA_BANNER:
		if (line[0] != '2') {
			mta_error(s, "BANNER rejected: %s", line);
			s->flags |= MTA_FREE;
			return;
		}

		pbuf = "";
		if (strlen(line) > 4) {
			(void)strlcpy(buf, line + 4, sizeof buf);
			if ((pbuf = strchr(buf, ' ')))
				*pbuf = '\0';
			pbuf = valid_domainpart(buf) ? buf : "";
		}
		mta_report_link_greeting(s, pbuf);

		if (s->flags & MTA_LMTP)
			mta_enter_state(s, MTA_LHLO);
		else
			mta_enter_state(s, MTA_EHLO);
		break;

	case MTA_EHLO:
		if (line[0] != '2') {
			/* rejected at ehlo state */
			if ((s->relay->flags & RELAY_AUTH) ||
			    (s->flags & MTA_WANT_SECURE)) {
				mta_error(s, "EHLO rejected: %s", line);
				s->flags |= MTA_FREE;
				return;
			}
			mta_enter_state(s, MTA_HELO);
			return;
		}
		if (!(s->flags & MTA_FORCE_PLAIN))
			mta_enter_state(s, MTA_STARTTLS);
		else
			mta_enter_state(s, MTA_READY);
		break;

	case MTA_HELO:
		if (line[0] != '2') {
			mta_error(s, "HELO rejected: %s", line);
			s->flags |= MTA_FREE;
			return;
		}
		mta_enter_state(s, MTA_READY);
		break;

	case MTA_LHLO:
		if (line[0] != '2') {
			mta_error(s, "LHLO rejected: %s", line);
			s->flags |= MTA_FREE;
			return;
		}
		mta_enter_state(s, MTA_READY);
		break;

	case MTA_STARTTLS:
		if (line[0] != '2') {
			if (!(s->flags & MTA_WANT_SECURE)) {
				mta_enter_state(s, MTA_AUTH);
				return;
			}
			/* XXX mark that the MX doesn't support STARTTLS */
			mta_error(s, "STARTTLS rejected: %s", line);
			s->flags |= MTA_FREE;
			return;
		}

		mta_tls_init(s);
		break;

	case MTA_AUTH_PLAIN:
		if (line[0] != '2') {
			mta_error(s, "AUTH rejected: %s", line);
			mta_report_link_auth(s, s->username, "fail");
			s->flags |= MTA_FREE;
			return;
		}
		mta_report_link_auth(s, s->username, "pass");
		mta_enter_state(s, MTA_READY);
		break;

	case MTA_AUTH_LOGIN:
		if (strncmp(line, "334 ", 4) != 0) {
			mta_error(s, "AUTH rejected: %s", line);
			mta_report_link_auth(s, s->username, "fail");
			s->flags |= MTA_FREE;
			return;
		}
		mta_enter_state(s, MTA_AUTH_LOGIN_USER);
		break;

	case MTA_AUTH_LOGIN_USER:
		if (strncmp(line, "334 ", 4) != 0) {
			mta_error(s, "AUTH rejected: %s", line);
			mta_report_link_auth(s, s->username, "fail");
			s->flags |= MTA_FREE;
			return;
		}
		mta_enter_state(s, MTA_AUTH_LOGIN_PASS);
		break;

	case MTA_AUTH_LOGIN_PASS:
		if (line[0] != '2') {
			mta_error(s, "AUTH rejected: %s", line);
			mta_report_link_auth(s, s->username, "fail");
			s->flags |= MTA_FREE;
			return;
		}
		mta_report_link_auth(s, s->username, "pass");
		mta_enter_state(s, MTA_READY);
		break;

	case MTA_MAIL:
		if (line[0] != '2') {
			if (line[0] == '5')
				delivery = IMSG_MTA_DELIVERY_PERMFAIL;
			else
				delivery = IMSG_MTA_DELIVERY_TEMPFAIL;

			mta_flush_task(s, delivery, line, 0, 0);
			mta_enter_state(s, MTA_RSET);
			return;
		}
		mta_report_tx_begin(s, s->task->msgid);
		mta_report_tx_mail(s, s->task->msgid, s->task->sender, 1);
		mta_enter_state(s, MTA_RCPT);
		break;

	case MTA_RCPT:
		e = s->currevp;

		/* remove envelope from hosttat cache if there */
		if ((domain = strchr(e->dest, '@')) != NULL) {
			domain++;
			mta_hoststat_uncache(domain, e->id);
		}

		s->currevp = TAILQ_NEXT(s->currevp, entry);
		if (line[0] == '2') {
			s->failures = 0;
			/*
			 * this host is up, reschedule envelopes that
			 * were cached for reschedule.
			 */
			if (domain)
				mta_hoststat_reschedule(domain);
		}
		else {
			mta_report_tx_rollback(s, s->task->msgid);
			mta_report_tx_reset(s, s->task->msgid);
			if (line[0] == '5')
				delivery = IMSG_MTA_DELIVERY_PERMFAIL;
			else
				delivery = IMSG_MTA_DELIVERY_TEMPFAIL;
			s->failures++;

			/* remove failed envelope from task list */
			TAILQ_REMOVE(&s->task->envelopes, e, entry);
			stat_decrement("mta.envelope", 1);

			/* log right away */
			(void)snprintf(buf, sizeof(buf), "%s",
			    mta_host_to_text(s->route->dst));

			e->session = s->id;
			/* XXX */
			/*
			 * getsockname() can only fail with ENOBUFS here
			 * best effort, don't log source ...
			 */
			sa_len = sizeof(ss);
			sa = (struct sockaddr *)&ss;
			if (getsockname(io_fileno(s->io), sa, &sa_len) == -1)
				mta_delivery_log(e, NULL, buf, delivery, line);
			else
				mta_delivery_log(e, sa_to_text(sa),
				    buf, delivery, line);

			if (domain)
				mta_hoststat_update(domain, e->status);
			mta_delivery_notify(e);

			if (s->relay->limits->max_failures_per_session &&
			    s->failures == s->relay->limits->max_failures_per_session) {
					mta_flush_task(s, IMSG_MTA_DELIVERY_TEMPFAIL,
					    "Too many consecutive errors, closing connection", 0, 1);
					mta_enter_state(s, MTA_QUIT);
					break;
				}

			/*
			 * if no more envelopes, flush failed queue
			 */
			if (TAILQ_EMPTY(&s->task->envelopes)) {
				mta_flush_task(s, IMSG_MTA_DELIVERY_OK,
				    "No envelope", 0, 0);
				mta_enter_state(s, MTA_RSET);
				break;
			}
		}

		switch (line[0]) {
		case '2':
			mta_report_tx_rcpt(s,
			    s->task->msgid, e->dest, 1);
			break;
		case '4':
			mta_report_tx_rcpt(s,
			    s->task->msgid, e->dest, -1);
			break;
		case '5':
			mta_report_tx_rcpt(s,
			    s->task->msgid, e->dest, 0);
			break;
		}

		if (s->currevp == NULL)
			mta_enter_state(s, MTA_DATA);
		else
			mta_enter_state(s, MTA_RCPT);
		break;

	case MTA_DATA:
		if (line[0] == '2' || line[0] == '3') {
			mta_report_tx_data(s, s->task->msgid, 1);
			mta_enter_state(s, MTA_BODY);
			break;
		}

		if (line[0] == '5')
			delivery = IMSG_MTA_DELIVERY_PERMFAIL;
		else
			delivery = IMSG_MTA_DELIVERY_TEMPFAIL;
		mta_report_tx_data(s, s->task->msgid,
		    delivery == IMSG_MTA_DELIVERY_TEMPFAIL ? -1 : 0);
		mta_report_tx_rollback(s, s->task->msgid);
		mta_report_tx_reset(s, s->task->msgid);
		mta_flush_task(s, delivery, line, 0, 0);
		mta_enter_state(s, MTA_RSET);
		break;

	case MTA_LMTP_EOM:
	case MTA_EOM:
		if (line[0] == '2') {
			delivery = IMSG_MTA_DELIVERY_OK;
			s->msgtried = 0;
			s->msgcount++;
		}
		else if (line[0] == '5')
			delivery = IMSG_MTA_DELIVERY_PERMFAIL;
		else
			delivery = IMSG_MTA_DELIVERY_TEMPFAIL;
		if (delivery != IMSG_MTA_DELIVERY_OK) {
			mta_report_tx_rollback(s, s->task->msgid);
			mta_report_tx_reset(s, s->task->msgid);
		}
		else {
			mta_report_tx_commit(s, s->task->msgid, s->datalen);
			mta_report_tx_reset(s, s->task->msgid);
		}
		mta_flush_task(s, delivery, line, (s->flags & MTA_LMTP) ? 1 : 0, 0);
		if (s->task) {
			s->rcptcount--;
			mta_enter_state(s, MTA_LMTP_EOM);
		} else {
			s->rcptcount = 0;
			if (s->relay->limits->sessdelay_transaction) {
				log_debug("debug: mta: waiting for %llds before next transaction",
				    (long long)s->relay->limits->sessdelay_transaction);
				s->hangon = s->relay->limits->sessdelay_transaction -1;
				s->flags |= MTA_HANGON;
				runq_schedule(hangon,
				    s->relay->limits->sessdelay_transaction, s);
			}
			else
				mta_enter_state(s, MTA_READY);
		}
		break;

	case MTA_RSET:
		s->rcptcount = 0;

		if (s->task) {
			mta_report_tx_rollback(s, s->task->msgid);
			mta_report_tx_reset(s, s->task->msgid);
		}
		if (s->relay->limits->sessdelay_transaction) {
			log_debug("debug: mta: waiting for %llds after reset",
			    (long long)s->relay->limits->sessdelay_transaction);
			s->hangon = s->relay->limits->sessdelay_transaction -1;
			s->flags |= MTA_HANGON;
			runq_schedule(hangon,
			    s->relay->limits->sessdelay_transaction, s);
		}
		else
			mta_enter_state(s, MTA_READY);
		break;

	default:
		fatalx("mta_response() bad state");
	}
}

static void
mta_io(struct io *io, int evt, void *arg)
{
	struct mta_session	*s = arg;
	char			*line, *msg, *p;
	size_t			 len;
	const char		*error;
	int			 cont;

	log_trace(TRACE_IO, "mta: %p: %s %s", s, io_strevent(evt),
	    io_strio(io));

	switch (evt) {

	case IO_CONNECTED:
		mta_connected(s);

		if (s->use_smtps) {
			io_set_write(io);
			mta_tls_init(s);
			if (s->flags & MTA_FREE)
				mta_free(s);
		}
		else {
			mta_enter_state(s, MTA_BANNER);
			io_set_read(io);
		}
		break;

	case IO_TLSREADY:
		log_info("%016"PRIx64" mta tls ciphers=%s",
		    s->id, tls_to_text(io_tls(s->io)));
		s->flags |= MTA_TLS;
		if (s->relay->dispatcher->u.remote.tls_verify)
			s->flags |= MTA_TLS_VERIFIED;

		mta_tls_started(s);
		mta_report_link_tls(s,
		    tls_to_text(io_tls(s->io)));
		break;

	case IO_DATAIN:
	    nextline:
		line = io_getline(s->io, &len);
		if (line == NULL) {
			if (io_datalen(s->io) >= LINE_MAX) {
				mta_error(s, "Input too long");
				mta_free(s);
			}
			return;
		}

		/* Strip trailing '\r' */
		if (len && line[len - 1] == '\r')
			line[--len] = '\0';

		log_trace(TRACE_MTA, "mta: %p: <<< %s", s, line);
		mta_report_protocol_server(s, line);

		if ((error = parse_smtp_response(line, len, &msg, &cont))) {
			mta_error(s, "Bad response: %s", error);
			mta_free(s);
			return;
		}

		/* read extensions */
		if (s->state == MTA_EHLO) {
			if (strcmp(msg, "STARTTLS") == 0)
				s->ext |= MTA_EXT_STARTTLS;
			else if (strncmp(msg, "AUTH ", 5) == 0) {
                                s->ext |= MTA_EXT_AUTH;
                                if ((p = strstr(msg, " PLAIN")) &&
				    (*(p+6) == '\0' || *(p+6) == ' '))
                                        s->ext |= MTA_EXT_AUTH_PLAIN;
                                if ((p = strstr(msg, " LOGIN")) &&
				    (*(p+6) == '\0' || *(p+6) == ' '))
                                        s->ext |= MTA_EXT_AUTH_LOGIN;
			}
			else if (strcmp(msg, "PIPELINING") == 0)
				s->ext |= MTA_EXT_PIPELINING;
			else if (strcmp(msg, "DSN") == 0)
				s->ext |= MTA_EXT_DSN;
			else if (strncmp(msg, "SIZE ", 5) == 0) {
				s->ext_size = strtonum(msg+5, 0, UINT32_MAX, &error);
				if (error == NULL)
					s->ext |= MTA_EXT_SIZE;
			}
		}

		/* continuation reply, we parse out the repeating statuses and ESC */
		if (cont) {
			if (s->replybuf[0] == '\0')
				(void)strlcat(s->replybuf, line, sizeof s->replybuf);
			else if (len > 4) {
				p = line + 4;
				if (isdigit((unsigned char)p[0]) && p[1] == '.' &&
				    isdigit((unsigned char)p[2]) && p[3] == '.' &&
				    isdigit((unsigned char)p[4]) && isspace((unsigned char)p[5]))
					p += 5;
				(void)strlcat(s->replybuf, p, sizeof s->replybuf);
			}
			goto nextline;
		}

		/* last line of a reply, check if we're on a continuation to parse out status and ESC.
		 * if we overflow reply buffer or are not on continuation, log entire last line.
		 */
		if (s->replybuf[0] == '\0')
			(void)strlcat(s->replybuf, line, sizeof s->replybuf);
		else if (len > 4) {
			p = line + 4;
			if (isdigit((unsigned char)p[0]) && p[1] == '.' &&
			    isdigit((unsigned char)p[2]) && p[3] == '.' &&
			    isdigit((unsigned char)p[4]) && isspace((unsigned char)p[5]))
				p += 5;
			if (strlcat(s->replybuf, p, sizeof s->replybuf) >= sizeof s->replybuf)
				(void)strlcpy(s->replybuf, line, sizeof s->replybuf);
		}

		if (s->state == MTA_QUIT) {
			log_info("%016"PRIx64" mta disconnected reason=quit messages=%zu",
			    s->id, s->msgcount);
			mta_free(s);
			return;
		}
		io_set_write(io);
		mta_response(s, s->replybuf);
		if (s->flags & MTA_FREE) {
			mta_free(s);
			return;
		}
		if (s->flags & MTA_RECONN) {
			s->flags &= ~MTA_RECONN;
			mta_connect(s);
			return;
		}

		if (io_datalen(s->io)) {
			log_debug("debug: mta: remaining data in input buffer");
			mta_error(s, "Remote host sent too much data");
			if (s->flags & MTA_WAIT)
				s->flags |= MTA_FREE;
			else
				mta_free(s);
		}
		break;

	case IO_LOWAT:
		if (s->state == MTA_BODY) {
			mta_enter_state(s, MTA_BODY);
			if (s->flags & MTA_FREE) {
				mta_free(s);
				return;
			}
		}

		if (io_queued(s->io) == 0)
			io_set_read(io);
		break;

	case IO_TIMEOUT:
		log_debug("debug: mta: %p: connection timeout", s);
		mta_error(s, "Connection timeout");
		mta_report_timeout(s);
		if (!s->ready)
			mta_connect(s);
		else
			mta_free(s);
		break;

	case IO_ERROR:
		log_debug("debug: mta: %p: IO error: %s", s, io_error(io));

		if (s->state == MTA_STARTTLS && s->use_smtp_tls) {
			/* error in non-strict SSL negotiation, downgrade to plain */
			log_info("smtp-out: Error on session %016"PRIx64
			    ": opportunistic TLS failed, "
			    "downgrading to plain", s->id);
			s->flags &= ~MTA_TLS;
			s->flags |= MTA_DOWNGRADE_PLAIN;
			mta_connect(s);
			break;
		}

		mta_error(s, "IO Error: %s", io_error(io));
		mta_free(s);
		break;

	case IO_DISCONNECTED:
		log_debug("debug: mta: %p: disconnected in state %s",
		    s, mta_strstate(s->state));
		mta_error(s, "Connection closed unexpectedly");
		if (!s->ready)
			mta_connect(s);
		else
			mta_free(s);
		break;

	default:
		fatalx("mta_io() bad event");
	}
}

static void
mta_send(struct mta_session *s, char *fmt, ...)
{
	va_list  ap;
	char	*p;
	int	 len;

	va_start(ap, fmt);
	if ((len = vasprintf(&p, fmt, ap)) == -1)
		fatal("mta: vasprintf");
	va_end(ap);

	log_trace(TRACE_MTA, "mta: %p: >>> %s", s, p);

	if (strncasecmp(p, "AUTH PLAIN ", 11) == 0)
		mta_report_protocol_client(s, "AUTH PLAIN ********");
	else if (s->state == MTA_AUTH_LOGIN_USER || s->state == MTA_AUTH_LOGIN_PASS)
		mta_report_protocol_client(s, "********");
	else
		mta_report_protocol_client(s, p);

	io_xprintf(s->io, "%s\r\n", p);

	free(p);
}

/*
 * Queue some data into the input buffer
 */
static ssize_t
mta_queue_data(struct mta_session *s)
{
	char	*ln = NULL;
	size_t	 sz = 0, q;
	ssize_t	 len;

	q = io_queued(s->io);

	while (io_queued(s->io) < MTA_HIWAT) {
		if ((len = getline(&ln, &sz, s->datafp)) == -1)
			break;
		if (ln[len - 1] == '\n')
			ln[len - 1] = '\0';
		s->datalen += io_xprintf(s->io, "%s%s\r\n", *ln == '.' ? "." : "", ln);
	}

	free(ln);
	if (ferror(s->datafp)) {
		mta_flush_task(s, IMSG_MTA_DELIVERY_TEMPFAIL,
		    "Error reading content file", 0, 0);
		return (-1);
	}

	if (feof(s->datafp)) {
		fclose(s->datafp);
		s->datafp = NULL;
	}

	return (io_queued(s->io) - q);
}

static void
mta_flush_task(struct mta_session *s, int delivery, const char *error, size_t count,
	int cache)
{
	struct mta_envelope	*e;
	char			 relay[LINE_MAX];
	size_t			 n;
	struct sockaddr_storage	 ss;
	struct sockaddr		*sa;
	socklen_t		 sa_len;
	const char		*domain;

	(void)snprintf(relay, sizeof relay, "%s", mta_host_to_text(s->route->dst));
	n = 0;
	while ((e = TAILQ_FIRST(&s->task->envelopes))) {

		if (count && n == count) {
			stat_decrement("mta.envelope", n);
			return;
		}

		TAILQ_REMOVE(&s->task->envelopes, e, entry);

		/* we're about to log, associate session to envelope */
		e->session = s->id;
		e->ext = s->ext;

		/* XXX */
		/*
		 * getsockname() can only fail with ENOBUFS here
		 * best effort, don't log source ...
		 */
		sa = (struct sockaddr *)&ss;
		sa_len = sizeof(ss);
		if (getsockname(io_fileno(s->io), sa, &sa_len) == -1)
			mta_delivery_log(e, NULL, relay, delivery, error);
		else
			mta_delivery_log(e, sa_to_text(sa),
			    relay, delivery, error);

		mta_delivery_notify(e);

		domain = strchr(e->dest, '@');
		if (domain) {
			domain++;
			mta_hoststat_update(domain, error);
			if (cache)
				mta_hoststat_cache(domain, e->id);
		}

		n++;
	}

	free(s->task->sender);
	free(s->task);
	s->task = NULL;

	if (s->datafp) {
		fclose(s->datafp);
		s->datafp = NULL;
	}

	stat_decrement("mta.envelope", n);
	stat_decrement("mta.task.running", 1);
	stat_decrement("mta.task", 1);
}

static void
mta_error(struct mta_session *s, const char *fmt, ...)
{
	va_list  ap;
	char	*error;
	int	 len;

	va_start(ap, fmt);
	if ((len = vasprintf(&error, fmt, ap)) == -1)
		fatal("mta: vasprintf");
	va_end(ap);

	if (s->msgcount)
		log_info("smtp-out: Error on session %016"PRIx64
		    " after %zu message%s sent: %s", s->id, s->msgcount,
		    (s->msgcount > 1) ? "s" : "", error);
	else
		log_info("%016"PRIx64" mta error reason=%s",
		    s->id, error);

	/*
	 * If not connected yet, and the error is not local, just ignore it
	 * and try to reconnect.
	 */
	if (s->state == MTA_INIT &&
	    (errno == ETIMEDOUT || errno == ECONNREFUSED)) {
		log_debug("debug: mta: not reporting route error yet");
		free(error);
		return;
	}

	mta_route_error(s->relay, s->route);

	if (s->task)
		mta_flush_task(s, IMSG_MTA_DELIVERY_TEMPFAIL, error, 0, 0);

	free(error);
}

static void
mta_tls_init(struct mta_session *s)
{
	struct dispatcher_remote *remote;
	struct tls *tls;

	if ((tls = tls_client()) == NULL) {
		log_info("%016"PRIx64" mta closing reason=tls-failure", s->id);
		s->flags |= MTA_FREE;
		return;
	}

	remote = &s->relay->dispatcher->u.remote;
	if ((s->flags & MTA_WANT_SECURE) && !remote->tls_required) {
		/* If TLS not explicitly configured, use implicit config. */
		remote->tls_required = 1;
		remote->tls_verify = 1;
		tls_config_verify(remote->tls_config);
	}
	if (tls_configure(tls, remote->tls_config) == -1) {
		log_info("%016"PRIx64" mta closing reason=tls-failure", s->id);
		tls_free(tls);
		s->flags |= MTA_FREE;
		return;
	}

	if (io_connect_tls(s->io, tls, s->mxname) == -1) {
		log_info("%016"PRIx64" mta closing reason=tls-connect-failed", s->id);
		tls_free(tls);
		s->flags |= MTA_FREE;
	}
}

static void
mta_tls_started(struct mta_session *s)
{
	if (tls_peer_cert_provided(io_tls(s->io))) {
		log_info("%016"PRIx64" mta "
		    "cert-check result=\"%s\" fingerprint=\"%s\"",
		    s->id,
		    (s->flags & MTA_TLS_VERIFIED) ? "valid" : "unverified",
		    tls_peer_cert_hash(io_tls(s->io)));
	}
	else {
		log_info("%016"PRIx64" smtp "
		    "cert-check result=\"no certificate presented\"",
		    s->id);
	}

	if (s->use_smtps) {
		mta_enter_state(s, MTA_BANNER);
		io_set_read(s->io);
	}
	else
		mta_enter_state(s, MTA_EHLO);
}

static const char *
dsn_strret(enum dsn_ret ret)
{
	if (ret == DSN_RETHDRS)
		return "HDRS";
	else if (ret == DSN_RETFULL)
		return "FULL";
	else {
		log_debug("mta: invalid ret %d", ret);
		return "???";
	}
}

static const char *
dsn_strnotify(uint8_t arg)
{
	static char	buf[32];
	size_t		sz;

	buf[0] = '\0';
	if (arg & DSN_SUCCESS)
		(void)strlcat(buf, "SUCCESS,", sizeof(buf));

	if (arg & DSN_FAILURE)
		(void)strlcat(buf, "FAILURE,", sizeof(buf));

	if (arg & DSN_DELAY)
		(void)strlcat(buf, "DELAY,", sizeof(buf));

	if (arg & DSN_NEVER)
		(void)strlcat(buf, "NEVER,", sizeof(buf));

	/* trim trailing comma */
	sz = strlen(buf);
	if (sz)
		buf[sz - 1] = '\0';

	return (buf);
}

#define CASE(x) case x : return #x

static const char *
mta_strstate(int state)
{
	switch (state) {
	CASE(MTA_INIT);
	CASE(MTA_BANNER);
	CASE(MTA_EHLO);
	CASE(MTA_HELO);
	CASE(MTA_STARTTLS);
	CASE(MTA_AUTH);
	CASE(MTA_AUTH_PLAIN);
	CASE(MTA_AUTH_LOGIN);
	CASE(MTA_AUTH_LOGIN_USER);
	CASE(MTA_AUTH_LOGIN_PASS);
	CASE(MTA_READY);
	CASE(MTA_MAIL);
	CASE(MTA_RCPT);
	CASE(MTA_DATA);
	CASE(MTA_BODY);
	CASE(MTA_EOM);
	CASE(MTA_LMTP_EOM);
	CASE(MTA_RSET);
	CASE(MTA_QUIT);
	default:
		return "MTA_???";
	}
}

static void
mta_filter_begin(struct mta_session *s)
{
	if (!SESSION_FILTERED(s))
		return;

	m_create(p_lka, IMSG_FILTER_SMTP_BEGIN, 0, 0, -1);
	m_add_id(p_lka, s->id);
	m_add_string(p_lka, s->relay->dispatcher->u.remote.filtername);
	m_close(p_lka);
}

static void
mta_filter_end(struct mta_session *s)
{
	if (!SESSION_FILTERED(s))
		return;

	m_create(p_lka, IMSG_FILTER_SMTP_END, 0, 0, -1);
	m_add_id(p_lka, s->id);
	m_close(p_lka);
}

static void
mta_connected(struct mta_session *s)
{
	struct sockaddr_storage sa_src;
	struct sockaddr_storage sa_dest;
	int sa_len;

	log_info("%016"PRIx64" mta connected", s->id);

	sa_len = sizeof sa_src;
	if (getsockname(io_fileno(s->io),
	    (struct sockaddr *)&sa_src, &sa_len) == -1)
		bzero(&sa_src, sizeof sa_src);
	sa_len = sizeof sa_dest;
	if (getpeername(io_fileno(s->io),
	    (struct sockaddr *)&sa_dest, &sa_len) == -1)
		bzero(&sa_dest, sizeof sa_dest);

	mta_report_link_connect(s,
	    s->route->dst->ptrname, 1,
	    &sa_src,
	    &sa_dest);
}

static void
mta_disconnected(struct mta_session *s)
{
	mta_report_link_disconnect(s);
	mta_filter_end(s);
}


static void
mta_report_link_connect(struct mta_session *s, const char *rdns, int fcrdns,
    const struct sockaddr_storage *ss_src,
    const struct sockaddr_storage *ss_dest)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_connect("smtp-out", s->id, rdns, fcrdns, ss_src, ss_dest);
}

static void
mta_report_link_greeting(struct mta_session *s,
    const char *domain)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_greeting("smtp-out", s->id, domain);
}

static void
mta_report_link_identify(struct mta_session *s, const char *method, const char *identity)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_identify("smtp-out", s->id, method, identity);
}

static void
mta_report_link_tls(struct mta_session *s, const char *ssl)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_tls("smtp-out", s->id, ssl);
}

static void
mta_report_link_disconnect(struct mta_session *s)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_disconnect("smtp-out", s->id);
}

static void
mta_report_link_auth(struct mta_session *s, const char *user, const char *result)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_auth("smtp-out", s->id, user, result);
}

static void
mta_report_tx_reset(struct mta_session *s, uint32_t msgid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_reset("smtp-out", s->id, msgid);
}

static void
mta_report_tx_begin(struct mta_session *s, uint32_t msgid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_begin("smtp-out", s->id, msgid);
}

static void
mta_report_tx_mail(struct mta_session *s, uint32_t msgid, const char *address, int ok)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_mail("smtp-out", s->id, msgid, address, ok);
}

static void
mta_report_tx_rcpt(struct mta_session *s, uint32_t msgid, const char *address, int ok)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_rcpt("smtp-out", s->id, msgid, address, ok);
}

static void
mta_report_tx_envelope(struct mta_session *s, uint32_t msgid, uint64_t evpid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_envelope("smtp-out", s->id, msgid, evpid);
}

static void
mta_report_tx_data(struct mta_session *s, uint32_t msgid, int ok)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_data("smtp-out", s->id, msgid, ok);
}

static void
mta_report_tx_commit(struct mta_session *s, uint32_t msgid, size_t msgsz)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_commit("smtp-out", s->id, msgid, msgsz);
}

static void
mta_report_tx_rollback(struct mta_session *s, uint32_t msgid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_rollback("smtp-out", s->id, msgid);
}

static void
mta_report_protocol_client(struct mta_session *s, const char *command)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_protocol_client("smtp-out", s->id, command);
}

static void
mta_report_protocol_server(struct mta_session *s, const char *response)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_protocol_server("smtp-out", s->id, response);
}

#if 0
static void
mta_report_filter_response(struct mta_session *s, int phase, int response, const char *param)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_filter_response("smtp-out", s->id, phase, response, param);
}
#endif

static void
mta_report_timeout(struct mta_session *s)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_timeout("smtp-out", s->id);
}
