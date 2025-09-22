/*	$OpenBSD: frontend.c,v 1.4 2024/11/21 13:34:51 claudio Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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

#include <sys/tree.h>

#include <errno.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "lpd.h"

#include "log.h"
#include "proc.h"

static void frontend_shutdown(void);
static void frontend_listen(struct listener *);
static void frontend_pause(struct listener *);
static void frontend_resume(struct listener *);
static void frontend_accept(int, short, void *);
static void frontend_dispatch_priv(struct imsgproc *, struct imsg *, void *);
static void frontend_dispatch_engine(struct imsgproc *, struct imsg *, void *);

struct conn {
	SPLAY_ENTRY(conn)	 entry;
	struct listener		*listener;
	uint32_t		 id;
};

static int conn_cmp(struct conn *, struct conn *);

SPLAY_HEAD(conntree, conn);
SPLAY_PROTOTYPE(conntree, conn, entry, conn_cmp);

static struct conntree conns;
static struct lpd_conf *tmpconf;

static int
conn_cmp(struct conn *a, struct conn *b)
{
	if (a->id < b->id)
		return (-1);
	if (a->id > b->id)
		return (1);
	return (0);
}

SPLAY_GENERATE(conntree, conn, entry, conn_cmp);

void
frontend(int debug, int verbose)
{
	struct passwd *pw;

	/* Early initialisation. */
	log_init(debug, LOG_LPR);
	log_setverbose(verbose);
	log_procinit("frontend");
	setproctitle("frontend");

	SPLAY_INIT(&conns);
	lpr_init();

	/* Drop privileges. */
	if ((pw = getpwnam(LPD_USER)) == NULL)
		fatal("%s: getpwnam: %s", __func__, LPD_USER);

	if (chroot(_PATH_VAREMPTY) == -1)
		fatal("%s: chroot", __func__);
	if (chdir("/") == -1)
		fatal("%s: chdir", __func__);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("%s: cannot drop privileges", __func__);

	if (pledge("stdio unix inet recvfd sendfd", NULL) == -1)
		fatal("%s: pledge", __func__);

	event_init();

	signal(SIGPIPE, SIG_IGN);

	/* Setup parent imsg socket. */
	p_priv = proc_attach(PROC_PRIV, 3);
	if (p_priv == NULL)
		fatal("%s: proc_attach", __func__);
	proc_setcallback(p_priv, frontend_dispatch_priv, NULL);
	proc_enable(p_priv);

	event_dispatch();

	frontend_shutdown();
}

void
frontend_conn_closed(uint32_t connid)
{
	struct listener *l;
	struct conn key, *conn;

	key.id = connid;
	conn = SPLAY_FIND(conntree, &conns, &key);
	if (conn == NULL)
		fatalx("%s: %08x unknown connid", __func__, connid);

	l = conn->listener;

	if (log_getverbose() > LOGLEVEL_CONN)
		log_debug("%08x close %s", conn->id,
		    log_fmt_proto(l->proto));

	SPLAY_REMOVE(conntree, &conns, conn);
	free(conn);

	if (l->pause)
		frontend_resume(l);
}

static void
frontend_shutdown()
{
	struct listener *l;

	TAILQ_FOREACH(l, &env->listeners, entry)
		close(l->sock);

	log_debug("exiting");

	exit(0);
}

static void
frontend_listen(struct listener *l)
{
	if (log_getverbose() > LOGLEVEL_CONN)
		log_debug("listen %s %s", log_fmt_proto(l->proto),
		    log_fmt_sockaddr((struct sockaddr*)&l->ss));

	if (listen(l->sock, 5) == -1)
		fatal("%s: listen", __func__);

	frontend_resume(l);
}

static void
frontend_pause(struct listener *l)
{
	struct timeval tv;

	event_del(&l->ev);

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	evtimer_set(&l->ev, frontend_accept, l);
	evtimer_add(&l->ev, &tv);
	l->pause = 1;
}

static void
frontend_resume(struct listener *l)
{
	if (l->pause) {
		evtimer_del(&l->ev);
		l->pause = 0;
	}
	event_set(&l->ev, l->sock, EV_READ | EV_PERSIST, frontend_accept, l);
	event_add(&l->ev, NULL);
}

static void
frontend_accept(int sock, short ev, void *arg)
{
	struct listener *l = arg;
	struct sockaddr_storage ss;
	struct sockaddr *sa;
	struct conn *conn;
	socklen_t len;

	if (l->pause) {
		l->pause = 0;
		frontend_resume(l);
		return;
	}

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL)
		log_warn("%s: calloc", __func__);

	sa = (struct sockaddr *)&ss;
	len = sizeof(ss);
	sock = accept4(sock, sa, &len, SOCK_NONBLOCK);
	if (sock == -1) {
		if (errno == ENFILE || errno == EMFILE)
			frontend_pause(l);
		else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("%s: accept4", __func__);
		free(conn);
		return;
	}

	if (conn == NULL) {
		close(sock);
		return;
	}

	while (conn->id == 0 || SPLAY_FIND(conntree, &conns, conn))
		conn->id = arc4random();
	SPLAY_INSERT(conntree, &conns, conn);
	conn->listener = l;

	if (log_getverbose() > LOGLEVEL_CONN)
		log_debug("%08x accept %s %s", conn->id,
		    log_fmt_proto(conn->listener->proto),
		    log_fmt_sockaddr((struct sockaddr*)&ss));

	switch (l->proto) {
	case PROTO_LPR:
		lpr_conn(conn->id, l, sock, sa);
		break;
	default:
		fatalx("%s: unexpected protocol %d", __func__, l->proto);
	}
}

static void
frontend_dispatch_priv(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	struct listener *l;
	int fd;

	if (imsg == NULL) {
		log_debug("%s: imsg connection lost", __func__);
		event_loopexit(NULL);
		return;
	}

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	case IMSG_SOCK_ENGINE:
		if ((fd = imsg_get_fd(imsg)) == -1)
			fatalx("%s: engine socket not received", __func__);
		m_end(proc);
		p_engine = proc_attach(PROC_ENGINE, fd);
		proc_setcallback(p_engine, frontend_dispatch_engine, NULL);
		proc_enable(p_engine);
		break;

	case IMSG_CONF_START:
		m_end(proc);
		if ((tmpconf = calloc(1, sizeof(*tmpconf))) == NULL)
			fatal("%s: calloc", __func__);
		TAILQ_INIT(&tmpconf->listeners);
		break;

	case IMSG_CONF_LISTENER:
		if ((fd = imsg_get_fd(imsg)) == -1)
			fatalx("%s: listener socket not received", __func__);
		if ((l = calloc(1, sizeof(*l))) == NULL)
			fatal("%s: calloc", __func__);
		m_get_int(proc, &l->proto);
		m_get_sockaddr(proc, (struct sockaddr *)&l->ss);
		m_end(proc);
		l->sock = fd;
		TAILQ_INSERT_TAIL(&tmpconf->listeners, l, entry);
		break;

	case IMSG_CONF_END:
		m_end(proc);
		TAILQ_FOREACH(l, &tmpconf->listeners, entry)
			frontend_listen(l);
		env = tmpconf;
		break;

	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
frontend_dispatch_engine(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	if (imsg == NULL) {
		log_debug("%s: imsg connection lost", __func__);
		event_loopexit(NULL);
		return;
	}

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	case IMSG_GETADDRINFO:
	case IMSG_GETADDRINFO_END:
	case IMSG_GETNAMEINFO:
		resolver_dispatch_result(proc, imsg);
		break;

	case IMSG_LPR_ALLOWEDHOST:
	case IMSG_LPR_DISPLAYQ:
	case IMSG_LPR_RECVJOB:
	case IMSG_LPR_RECVJOB_CF:
	case IMSG_LPR_RECVJOB_DF:
	case IMSG_LPR_RMJOB:
		lpr_dispatch_engine(proc, imsg);
		break;

	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}
