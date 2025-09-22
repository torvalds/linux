/*	$OpenBSD: session.c,v 1.526 2025/08/21 15:15:25 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004, 2005 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2017 Peter van Dijk <peter.van.dijk@powerdns.com>
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

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <limits.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

#define PFD_PIPE_MAIN		0
#define PFD_PIPE_ROUTE		1
#define PFD_PIPE_ROUTE_CTL	2
#define PFD_SOCK_CTL		3
#define PFD_SOCK_RCTL		4
#define PFD_LISTENERS_START	5

#define MAX_TIMEOUT		240
#define PAUSEACCEPT_TIMEOUT	1

void	session_sighdlr(int);
int	setup_listeners(u_int *);
void	init_peer(struct peer *, struct bgpd_config *);
int	session_setup_socket(struct peer *);
void	session_accept(int);
void	session_graceful_stop(struct peer *);
void	session_dispatch_imsg(struct imsgbuf *, int, u_int *);
void	imsg_rde(int, uint32_t, void *, uint16_t);
void	merge_peers(struct bgpd_config *, struct bgpd_config *);

void	session_template_clone(struct peer *, struct sockaddr *,
	    uint32_t, uint32_t);
int	session_match_mask(struct peer *, struct bgpd_addr *);

static struct bgpd_config	*conf, *nconf;
static struct imsgbuf		*ibuf_rde;
static struct imsgbuf		*ibuf_rde_ctl;
static struct imsgbuf		*ibuf_main;

struct bgpd_sysdep	 sysdep;
volatile sig_atomic_t	 session_quit;
int			 pending_reconf;
int			 csock = -1, rcsock = -1;
u_int			 peer_cnt;

struct mrt_head		 mrthead;
monotime_t		 pauseaccept;

static inline int
peer_compare(const struct peer *a, const struct peer *b)
{
	return a->conf.id - b->conf.id;
}

RB_GENERATE(peer_head, peer, entry, peer_compare);

void
session_sighdlr(int sig)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		session_quit = 1;
		break;
	}
}

int
setup_listeners(u_int *la_cnt)
{
	int			 ttl = 255;
	struct listen_addr	*la;
	u_int			 cnt = 0;

	TAILQ_FOREACH(la, conf->listen_addrs, entry) {
		la->reconf = RECONF_NONE;
		cnt++;

		if (la->flags & LISTENER_LISTENING)
			continue;

		if (la->fd == -1) {
			log_warn("cannot establish listener on %s: invalid fd",
			    log_sockaddr((struct sockaddr *)&la->sa,
			    la->sa_len));
			continue;
		}

		if (tcp_md5_prep_listener(la, &conf->peers) == -1)
			fatal("tcp_md5_prep_listener");

		/* set ttl to 255 so that ttl-security works */
		if (la->sa.ss_family == AF_INET && setsockopt(la->fd,
		    IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == -1) {
			log_warn("setup_listeners setsockopt TTL");
			continue;
		}
		if (la->sa.ss_family == AF_INET6 && setsockopt(la->fd,
		    IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl)) == -1) {
			log_warn("setup_listeners setsockopt hoplimit");
			continue;
		}

		if (listen(la->fd, MAX_BACKLOG)) {
			close(la->fd);
			fatal("listen");
		}

		la->flags |= LISTENER_LISTENING;

		log_info("listening on %s",
		    log_sockaddr((struct sockaddr *)&la->sa, la->sa_len));
	}

	*la_cnt = cnt;

	return (0);
}

void
session_main(int debug, int verbose)
{
	unsigned int		 i, j, idx_peers, idx_listeners, idx_mrts;
	u_int			 pfd_elms = 0, peer_l_elms = 0, mrt_l_elms = 0;
	u_int			 listener_cnt, ctl_cnt, mrt_cnt;
	u_int			 new_cnt;
	struct passwd		*pw;
	struct peer		*p, **peer_l = NULL, *next;
	struct mrt		*m, *xm, **mrt_l = NULL;
	struct pollfd		*pfd = NULL;
	struct listen_addr	*la;
	void			*newp;
	monotime_t		 now, timeout;
	short			 events;

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	log_procinit(log_procnames[PROC_SE]);

	if ((pw = getpwnam(BGPD_USER)) == NULL)
		fatal(NULL);

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("session engine");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio inet recvfd", NULL) == -1)
		fatal("pledge");

	signal(SIGTERM, session_sighdlr);
	signal(SIGINT, session_sighdlr);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);

	if ((ibuf_main = malloc(sizeof(struct imsgbuf))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(ibuf_main, 3) == -1 ||
	    imsgbuf_set_maxsize(ibuf_main, MAX_BGPD_IMSGSIZE) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(ibuf_main);

	LIST_INIT(&mrthead);
	listener_cnt = 0;
	peer_cnt = 0;
	ctl_cnt = 0;

	conf = new_config();
	log_info("session engine ready");

	while (session_quit == 0) {
		/* check for peers to be initialized or deleted */
		if (!pending_reconf) {
			RB_FOREACH_SAFE(p, peer_head, &conf->peers, next) {
				/* new peer that needs init? */
				if (p->state == STATE_NONE)
					init_peer(p, conf);

				/* deletion due? */
				if (p->reconf_action == RECONF_DELETE) {
					if (p->demoted)
						session_demote(p, -1);
					p->conf.demote_group[0] = 0;
					session_stop(p, ERR_CEASE_PEER_UNCONF,
					    NULL);
					timer_remove_all(&p->timers);
					tcp_md5_del_listener(conf, p);
					imsg_rde(IMSG_SESSION_DELETE,
					    p->conf.id, NULL, 0);
					msgbuf_free(p->wbuf);
					RB_REMOVE(peer_head, &conf->peers, p);
					log_peer_warnx(&p->conf, "removed");
					free(p);
					peer_cnt--;
					continue;
				}
				p->reconf_action = RECONF_NONE;
			}
		}

		if (peer_cnt > peer_l_elms) {
			if ((newp = reallocarray(peer_l, peer_cnt,
			    sizeof(struct peer *))) == NULL) {
				/* panic for now */
				log_warn("could not resize peer_l from %u -> %u"
				    " entries", peer_l_elms, peer_cnt);
				fatalx("exiting");
			}
			peer_l = newp;
			peer_l_elms = peer_cnt;
		}

		mrt_cnt = 0;
		LIST_FOREACH_SAFE(m, &mrthead, entry, xm) {
			if (m->state == MRT_STATE_REMOVE) {
				mrt_clean(m);
				LIST_REMOVE(m, entry);
				free(m);
				continue;
			}
			if (msgbuf_queuelen(m->wbuf) > 0)
				mrt_cnt++;
		}

		if (mrt_cnt > mrt_l_elms) {
			if ((newp = reallocarray(mrt_l, mrt_cnt,
			    sizeof(struct mrt *))) == NULL) {
				/* panic for now */
				log_warn("could not resize mrt_l from %u -> %u"
				    " entries", mrt_l_elms, mrt_cnt);
				fatalx("exiting");
			}
			mrt_l = newp;
			mrt_l_elms = mrt_cnt;
		}

		new_cnt = PFD_LISTENERS_START + listener_cnt + peer_cnt +
		    ctl_cnt + mrt_cnt;
		if (new_cnt > pfd_elms) {
			if ((newp = reallocarray(pfd, new_cnt,
			    sizeof(struct pollfd))) == NULL) {
				/* panic for now */
				log_warn("could not resize pfd from %u -> %u"
				    " entries", pfd_elms, new_cnt);
				fatalx("exiting");
			}
			pfd = newp;
			pfd_elms = new_cnt;
		}

		memset(pfd, 0, sizeof(struct pollfd) * pfd_elms);

		set_pollfd(&pfd[PFD_PIPE_MAIN], ibuf_main);
		set_pollfd(&pfd[PFD_PIPE_ROUTE], ibuf_rde);
		set_pollfd(&pfd[PFD_PIPE_ROUTE_CTL], ibuf_rde_ctl);

		if (!monotime_valid(pauseaccept)) {
			pfd[PFD_SOCK_CTL].fd = csock;
			pfd[PFD_SOCK_CTL].events = POLLIN;
			pfd[PFD_SOCK_RCTL].fd = rcsock;
			pfd[PFD_SOCK_RCTL].events = POLLIN;
		} else {
			pfd[PFD_SOCK_CTL].fd = -1;
			pfd[PFD_SOCK_RCTL].fd = -1;
		}

		i = PFD_LISTENERS_START;
		TAILQ_FOREACH(la, conf->listen_addrs, entry) {
			if (!monotime_valid(pauseaccept)) {
				pfd[i].fd = la->fd;
				pfd[i].events = POLLIN;
			} else
				pfd[i].fd = -1;
			i++;
		}
		idx_listeners = i;
		now = getmonotime();
		timeout = monotime_add(now, monotime_from_sec(MAX_TIMEOUT));

		RB_FOREACH(p, peer_head, &conf->peers) {
			monotime_t nextaction;
			struct timer *pt;

			/* check timers */
			if ((pt = timer_nextisdue(&p->timers, now)) != NULL) {
				switch (pt->type) {
				case Timer_Hold:
					bgp_fsm(p, EVNT_TIMER_HOLDTIME, NULL);
					break;
				case Timer_SendHold:
					bgp_fsm(p, EVNT_TIMER_SENDHOLD, NULL);
					break;
				case Timer_ConnectRetry:
					bgp_fsm(p, EVNT_TIMER_CONNRETRY, NULL);
					break;
				case Timer_Keepalive:
					bgp_fsm(p, EVNT_TIMER_KEEPALIVE, NULL);
					break;
				case Timer_IdleHold:
					bgp_fsm(p, EVNT_START, NULL);
					break;
				case Timer_IdleHoldReset:
					p->IdleHoldTime =
					    INTERVAL_IDLE_HOLD_INITIAL;
					p->errcnt = 0;
					timer_stop(&p->timers,
					    Timer_IdleHoldReset);
					break;
				case Timer_CarpUndemote:
					timer_stop(&p->timers,
					    Timer_CarpUndemote);
					if (p->demoted &&
					    p->state == STATE_ESTABLISHED)
						session_demote(p, -1);
					break;
				case Timer_RestartTimeout:
					timer_stop(&p->timers,
					    Timer_RestartTimeout);
					session_graceful_stop(p);
					break;
				case Timer_SessionDown:
					timer_stop(&p->timers,
					    Timer_SessionDown);

					imsg_rde(IMSG_SESSION_DELETE,
					    p->conf.id, NULL, 0);
					p->rdesession = 0;

					/* finally delete this cloned peer */
					if (p->template)
						p->reconf_action =
						    RECONF_DELETE;
					break;
				default:
					fatalx("King Bula lost in time");
				}
			}
			nextaction = timer_nextduein(&p->timers);
			if (monotime_valid(nextaction)) {
				if (monotime_cmp(nextaction, timeout) < 0)
					timeout = nextaction;
			}

			/* check if peer needs throttling or not */
			if (!p->throttled &&
			    msgbuf_queuelen(p->wbuf) > SESS_MSG_HIGH_MARK) {
				imsg_rde(IMSG_XOFF, p->conf.id, NULL, 0);
				p->throttled = 1;
			}
			if (p->throttled &&
			    msgbuf_queuelen(p->wbuf) < SESS_MSG_LOW_MARK) {
				imsg_rde(IMSG_XON, p->conf.id, NULL, 0);
				p->throttled = 0;
			}

			/* are we waiting for a write? */
			events = POLLIN;
			if (msgbuf_queuelen(p->wbuf) > 0 ||
			    p->state == STATE_CONNECT)
				events |= POLLOUT;
			/* is there still work to do? */
			if (p->rpending)
				timeout = monotime_clear();

			/* poll events */
			if (p->fd != -1 && events != 0) {
				pfd[i].fd = p->fd;
				pfd[i].events = events;
				peer_l[i - idx_listeners] = p;
				i++;
			}
		}

		idx_peers = i;

		LIST_FOREACH(m, &mrthead, entry)
			if (msgbuf_queuelen(m->wbuf) > 0) {
				pfd[i].fd = m->fd;
				pfd[i].events = POLLOUT;
				mrt_l[i - idx_peers] = m;
				i++;
			}

		idx_mrts = i;

		i += control_fill_pfds(pfd + i, pfd_elms -i);

		if (i > pfd_elms)
			fatalx("poll pfd overflow");

		if (monotime_valid(pauseaccept) &&
		    monotime_cmp(timeout, pauseaccept) > 0)
			timeout = pauseaccept;

		timeout = monotime_sub(timeout, getmonotime());
		if (!monotime_valid(timeout))
			timeout = monotime_clear();

		if (poll(pfd, i, monotime_to_msec(timeout)) == -1) {
			if (errno == EINTR)
				continue;
			fatal("poll error");
		}

		/*
		 * If we previously saw fd exhaustion, we stop accept()
		 * for 1 second to throttle the accept() loop.
		 */
		if (monotime_valid(pauseaccept) &&
		    monotime_cmp(getmonotime(), pauseaccept) > 0)
			pauseaccept = monotime_clear();

		if (handle_pollfd(&pfd[PFD_PIPE_MAIN], ibuf_main) == -1) {
			log_warnx("SE: Lost connection to parent");
			session_quit = 1;
			continue;
		} else
			session_dispatch_imsg(ibuf_main, PFD_PIPE_MAIN,
			    &listener_cnt);

		if (handle_pollfd(&pfd[PFD_PIPE_ROUTE], ibuf_rde) == -1) {
			log_warnx("SE: Lost connection to RDE");
			imsgbuf_clear(ibuf_rde);
			free(ibuf_rde);
			ibuf_rde = NULL;
		} else
			session_dispatch_imsg(ibuf_rde, PFD_PIPE_ROUTE,
			    &listener_cnt);

		if (handle_pollfd(&pfd[PFD_PIPE_ROUTE_CTL], ibuf_rde_ctl) ==
		    -1) {
			log_warnx("SE: Lost connection to RDE control");
			imsgbuf_clear(ibuf_rde_ctl);
			free(ibuf_rde_ctl);
			ibuf_rde_ctl = NULL;
		} else
			session_dispatch_imsg(ibuf_rde_ctl, PFD_PIPE_ROUTE_CTL,
			    &listener_cnt);

		if (pfd[PFD_SOCK_CTL].revents & POLLIN)
			ctl_cnt += control_accept(csock, 0);

		if (pfd[PFD_SOCK_RCTL].revents & POLLIN)
			ctl_cnt += control_accept(rcsock, 1);

		for (j = PFD_LISTENERS_START; j < idx_listeners; j++)
			if (pfd[j].revents & POLLIN)
				session_accept(pfd[j].fd);

		for (; j < idx_peers; j++)
			session_dispatch_msg(&pfd[j],
			    peer_l[j - idx_listeners]);

		RB_FOREACH(p, peer_head, &conf->peers)
			session_process_msg(p);

		for (; j < idx_mrts; j++)
			if (pfd[j].revents & POLLOUT)
				mrt_write(mrt_l[j - idx_peers]);

		for (; j < i; j++)
			ctl_cnt -= control_dispatch_msg(&pfd[j], &conf->peers);
	}

	RB_FOREACH_SAFE(p, peer_head, &conf->peers, next) {
		session_stop(p, ERR_CEASE_ADMIN_DOWN, "bgpd shutting down");
		timer_remove_all(&p->timers);
		tcp_md5_del_listener(conf, p);
		RB_REMOVE(peer_head, &conf->peers, p);
		free(p);
	}

	while ((m = LIST_FIRST(&mrthead)) != NULL) {
		mrt_clean(m);
		LIST_REMOVE(m, entry);
		free(m);
	}

	free_config(conf);
	free(peer_l);
	free(mrt_l);
	free(pfd);

	/* close pipes */
	if (ibuf_rde) {
		imsgbuf_write(ibuf_rde);
		imsgbuf_clear(ibuf_rde);
		close(ibuf_rde->fd);
		free(ibuf_rde);
	}
	if (ibuf_rde_ctl) {
		imsgbuf_clear(ibuf_rde_ctl);
		close(ibuf_rde_ctl->fd);
		free(ibuf_rde_ctl);
	}
	imsgbuf_write(ibuf_main);
	imsgbuf_clear(ibuf_main);
	close(ibuf_main->fd);
	free(ibuf_main);

	control_shutdown(csock);
	control_shutdown(rcsock);
	log_info("session engine exiting");
	exit(0);
}

void
init_peer(struct peer *p, struct bgpd_config *c)
{
	TAILQ_INIT(&p->timers);
	p->fd = -1;
	if (p->wbuf != NULL)
		fatalx("%s: msgbuf already set", __func__);
	if ((p->wbuf = msgbuf_new_reader(MSGSIZE_HEADER, parse_header, p)) ==
	    NULL)
		fatal(NULL);

	if (p->conf.if_depend[0])
		imsg_compose(ibuf_main, IMSG_SESSION_DEPENDON, 0, 0, -1,
		    p->conf.if_depend, sizeof(p->conf.if_depend));
	else
		p->depend_ok = 1;

	/* apply holdtime and min_holdtime settings */
	if (p->conf.holdtime == 0)
		p->conf.holdtime = c->holdtime;
	if (p->conf.min_holdtime == 0)
		p->conf.min_holdtime = c->min_holdtime;
	if (p->conf.connectretry == 0)
		p->conf.connectretry = c->connectretry;
	p->local_bgpid = c->bgpid;

	peer_cnt++;

	change_state(p, STATE_IDLE, EVNT_NONE);
	if (p->conf.down)
		timer_stop(&p->timers, Timer_IdleHold); /* no autostart */
	else
		timer_set(&p->timers, Timer_IdleHold, SESSION_CLEAR_DELAY);

	p->stats.last_updown = getmonotime();

	/*
	 * on startup, demote if requested.
	 * do not handle new peers. they must reach ESTABLISHED beforehand.
	 * peers added at runtime have reconf_action set to RECONF_REINIT.
	 */
	if (p->reconf_action != RECONF_REINIT && p->conf.demote_group[0])
		session_demote(p, +1);
}

int
session_dispatch_msg(struct pollfd *pfd, struct peer *p)
{
	socklen_t	len;
	int		error;

	if (p->state == STATE_CONNECT) {
		if (pfd->revents & POLLOUT) {
			if (pfd->revents & POLLIN) {
				/* error occurred */
				len = sizeof(error);
				if (getsockopt(pfd->fd, SOL_SOCKET, SO_ERROR,
				    &error, &len) == -1 || error) {
					if (error)
						errno = error;
					if (errno != p->lasterr) {
						log_peer_warn(&p->conf,
						    "socket error");
						p->lasterr = errno;
					}
					bgp_fsm(p, EVNT_CON_OPENFAIL, NULL);
					return (1);
				}
			}
			bgp_fsm(p, EVNT_CON_OPEN, NULL);
			return (1);
		}
		if (pfd->revents & POLLHUP) {
			bgp_fsm(p, EVNT_CON_OPENFAIL, NULL);
			return (1);
		}
		if (pfd->revents & (POLLERR|POLLNVAL)) {
			bgp_fsm(p, EVNT_CON_FATAL, NULL);
			return (1);
		}
		return (0);
	}

	if (pfd->revents & POLLHUP) {
		bgp_fsm(p, EVNT_CON_CLOSED, NULL);
		return (1);
	}
	if (pfd->revents & (POLLERR|POLLNVAL)) {
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return (1);
	}

	if (pfd->revents & POLLOUT && msgbuf_queuelen(p->wbuf) > 0) {
		if (ibuf_write(p->fd, p->wbuf) == -1) {
			if (errno == EPIPE)
				log_peer_warnx(&p->conf, "Connection closed");
			else
				log_peer_warn(&p->conf, "write error");
			bgp_fsm(p, EVNT_CON_FATAL, NULL);
			return (1);
		}
		p->stats.last_write = getmonotime();
		start_timer_sendholdtime(p);
		if (!(pfd->revents & POLLIN))
			return (1);
	}

	if (p->fd != -1 && pfd->revents & POLLIN) {
		switch (ibuf_read(p->fd, p->wbuf)) {
		case -1:
			if (p->state == STATE_IDLE)
				/* error already handled before */
				return (1);
			log_peer_warn(&p->conf, "read error");
			bgp_fsm(p, EVNT_CON_FATAL, NULL);
			return (1);
		case 0:
			bgp_fsm(p, EVNT_CON_CLOSED, NULL);
			return (1);
		}
		p->stats.last_read = getmonotime();
		return (1);
	}
	return (0);
}

void
session_accept(int listenfd)
{
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_storage	 cliaddr;
	struct peer		*p = NULL;

	len = sizeof(cliaddr);
	if ((connfd = accept4(listenfd,
	    (struct sockaddr *)&cliaddr, &len,
	    SOCK_CLOEXEC | SOCK_NONBLOCK)) == -1) {
		if (errno == ENFILE || errno == EMFILE)
			pauseaccept = monotime_add(getmonotime(),
			    monotime_from_sec(PAUSEACCEPT_TIMEOUT));
		else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("accept");
		return;
	}

	p = getpeerbyip(conf, (struct sockaddr *)&cliaddr);

	if (p != NULL && p->state == STATE_IDLE && p->errcnt < 2) {
		if (timer_running(&p->timers, Timer_IdleHold, NULL)) {
			/* fast reconnect after clear */
			p->passive = 1;
			bgp_fsm(p, EVNT_START, NULL);
		}
	}

	if (p != NULL &&
	    (p->state == STATE_CONNECT || p->state == STATE_ACTIVE)) {
		if (p->fd != -1) {
			if (p->state == STATE_CONNECT)
				session_close(p);
			else {
				close(connfd);
				return;
			}
		}

open:
		if (p->auth_conf.method != AUTH_NONE && sysdep.no_pfkey) {
			log_peer_warnx(&p->conf,
			    "ipsec or md5sig configured but not available");
			close(connfd);
			return;
		}

		if (tcp_md5_check(connfd, &p->auth_conf) == -1) {
			log_peer_warn(&p->conf, "check md5sig");
			close(connfd);
			return;
		}
		p->fd = connfd;
		if (session_setup_socket(p)) {
			close(connfd);
			return;
		}
		bgp_fsm(p, EVNT_CON_OPEN, NULL);
		return;
	} else if (p != NULL && p->state == STATE_ESTABLISHED &&
	    p->capa.neg.grestart.restart == 2) {
		/* first do the graceful restart dance */
		change_state(p, STATE_CONNECT, EVNT_CON_CLOSED);
		/* then do part of the open dance */
		goto open;
	} else {
		log_conn_attempt(p, (struct sockaddr *)&cliaddr, len);
		close(connfd);
	}
}

int
session_connect(struct peer *peer)
{
	struct sockaddr		*sa;
	struct bgpd_addr	*bind_addr;
	socklen_t		 sa_len;

	/*
	 * we do not need the overcomplicated collision detection RFC 1771
	 * describes; we simply make sure there is only ever one concurrent
	 * tcp connection per peer.
	 */
	if (peer->fd != -1)
		return (-1);

	if ((peer->fd = socket(aid2af(peer->conf.remote_addr.aid),
	    SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_TCP)) == -1) {
		log_peer_warn(&peer->conf, "session_connect socket");
		bgp_fsm(peer, EVNT_CON_OPENFAIL, NULL);
		return (-1);
	}

	if (peer->auth_conf.method != AUTH_NONE && sysdep.no_pfkey) {
		log_peer_warnx(&peer->conf,
		    "ipsec or md5sig configured but not available");
		bgp_fsm(peer, EVNT_CON_OPENFAIL, NULL);
		return (-1);
	}

	if (tcp_md5_set(peer->fd, &peer->auth_conf,
	    &peer->conf.remote_addr) == -1)
		log_peer_warn(&peer->conf, "setting md5sig");

	/* if local-address is set we need to bind() */
	bind_addr = session_localaddr(peer);
	if ((sa = addr2sa(bind_addr, 0, &sa_len)) != NULL) {
		if (bind(peer->fd, sa, sa_len) == -1) {
			log_peer_warn(&peer->conf, "session_connect bind");
			bgp_fsm(peer, EVNT_CON_OPENFAIL, NULL);
			return (-1);
		}
	}

	if (session_setup_socket(peer)) {
		bgp_fsm(peer, EVNT_CON_OPENFAIL, NULL);
		return (-1);
	}

	sa = addr2sa(&peer->conf.remote_addr, peer->conf.remote_port, &sa_len);
	if (connect(peer->fd, sa, sa_len) == -1) {
		if (errno == EINPROGRESS)
			return (0);

		if (errno != peer->lasterr)
			log_peer_warn(&peer->conf, "connect");
		peer->lasterr = errno;
		bgp_fsm(peer, EVNT_CON_OPENFAIL, NULL);
		return (-1);
	}

	bgp_fsm(peer, EVNT_CON_OPEN, NULL);

	return (0);
}

int
session_setup_socket(struct peer *p)
{
	int	ttl = p->conf.distance;
	int	pre = IPTOS_PREC_INTERNETCONTROL;
	int	nodelay = 1;
	int	bsize;

	switch (p->conf.remote_addr.aid) {
	case AID_INET:
		/* set precedence, see RFC 1771 appendix 5 */
		if (setsockopt(p->fd, IPPROTO_IP, IP_TOS, &pre, sizeof(pre)) ==
		    -1) {
			log_peer_warn(&p->conf,
			    "session_setup_socket setsockopt TOS");
			return (-1);
		}

		if (p->conf.ebgp) {
			/*
			 * set TTL to foreign router's distance
			 * 1=direct n=multihop with ttlsec, we always use 255
			 */
			if (p->conf.ttlsec) {
				ttl = 256 - p->conf.distance;
				if (setsockopt(p->fd, IPPROTO_IP, IP_MINTTL,
				    &ttl, sizeof(ttl)) == -1) {
					log_peer_warn(&p->conf,
					    "session_setup_socket: "
					    "setsockopt MINTTL");
					return (-1);
				}
				ttl = 255;
			}

			if (setsockopt(p->fd, IPPROTO_IP, IP_TTL, &ttl,
			    sizeof(ttl)) == -1) {
				log_peer_warn(&p->conf,
				    "session_setup_socket setsockopt TTL");
				return (-1);
			}
		}
		break;
	case AID_INET6:
		if (setsockopt(p->fd, IPPROTO_IPV6, IPV6_TCLASS, &pre,
		    sizeof(pre)) == -1) {
			log_peer_warn(&p->conf, "session_setup_socket "
			    "setsockopt TCLASS");
			return (-1);
		}

		if (p->conf.ebgp) {
			/*
			 * set hoplimit to foreign router's distance
			 * 1=direct n=multihop with ttlsec, we always use 255
			 */
			if (p->conf.ttlsec) {
				ttl = 256 - p->conf.distance;
				if (setsockopt(p->fd, IPPROTO_IPV6,
				    IPV6_MINHOPCOUNT, &ttl, sizeof(ttl))
				    == -1) {
					log_peer_warn(&p->conf,
					    "session_setup_socket: "
					    "setsockopt MINHOPCOUNT");
					return (-1);
				}
				ttl = 255;
			}
			if (setsockopt(p->fd, IPPROTO_IPV6, IPV6_UNICAST_HOPS,
			    &ttl, sizeof(ttl)) == -1) {
				log_peer_warn(&p->conf,
				    "session_setup_socket setsockopt hoplimit");
				return (-1);
			}
		}
		break;
	}

	/* set TCP_NODELAY */
	if (setsockopt(p->fd, IPPROTO_TCP, TCP_NODELAY, &nodelay,
	    sizeof(nodelay)) == -1) {
		log_peer_warn(&p->conf,
		    "session_setup_socket setsockopt TCP_NODELAY");
		return (-1);
	}

	/* limit bufsize. no biggie if it fails */
	bsize = 65535;
	setsockopt(p->fd, SOL_SOCKET, SO_RCVBUF, &bsize, sizeof(bsize));
	setsockopt(p->fd, SOL_SOCKET, SO_SNDBUF, &bsize, sizeof(bsize));

	return (0);
}

void
session_close(struct peer *peer)
{
	if (peer->fd != -1) {
		close(peer->fd);
		pauseaccept = monotime_clear();
	}
	peer->fd = -1;
}

/*
 * compare the bgpd_addr with the sockaddr by converting the latter into
 * a bgpd_addr. Return true if the two are equal, including any scope
 */
static int
sa_equal(struct bgpd_addr *ba, struct sockaddr *b)
{
	struct bgpd_addr bb;

	sa2addr(b, &bb, NULL);
	return (memcmp(ba, &bb, sizeof(*ba)) == 0);
}

void
get_alternate_addr(struct bgpd_addr *local, struct bgpd_addr *remote,
    struct bgpd_addr *alt, unsigned int *scope)
{
	struct ifaddrs	*ifap, *ifa, *match;
	int connected = 0;
	u_int8_t plen;

	if (getifaddrs(&ifap) == -1)
		fatal("getifaddrs");

	for (match = ifap; match != NULL; match = match->ifa_next) {
		if (match->ifa_addr == NULL)
			continue;
		if (match->ifa_addr->sa_family != AF_INET &&
		    match->ifa_addr->sa_family != AF_INET6)
			continue;
		if (sa_equal(local, match->ifa_addr)) {
			if (remote->aid == AID_INET6 &&
			    IN6_IS_ADDR_LINKLOCAL(&remote->v6)) {
				/* IPv6 LLA are by definition connected */
				connected = 1;
			} else if (match->ifa_flags & IFF_POINTOPOINT &&
			    match->ifa_dstaddr != NULL) {
				if (sa_equal(remote, match->ifa_dstaddr))
					connected = 1;
			} else if (match->ifa_netmask != NULL) {
				plen = mask2prefixlen(
				    match->ifa_addr->sa_family,
				    match->ifa_netmask);
				if (prefix_compare(local, remote, plen) == 0)
					connected = 1;
			}
			break;
		}
	}

	if (match == NULL) {
		log_warnx("%s: local address not found", __func__);
		return;
	}
	if (connected)
		*scope = if_nametoindex(match->ifa_name);
	else
		*scope = 0;

	switch (local->aid) {
	case AID_INET6:
		for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr != NULL &&
			    ifa->ifa_addr->sa_family == AF_INET &&
			    strcmp(ifa->ifa_name, match->ifa_name) == 0) {
				sa2addr(ifa->ifa_addr, alt, NULL);
				break;
			}
		}
		break;
	case AID_INET:
		for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr != NULL &&
			    ifa->ifa_addr->sa_family == AF_INET6 &&
			    strcmp(ifa->ifa_name, match->ifa_name) == 0) {
				struct sockaddr_in6 *s =
				    (struct sockaddr_in6 *)ifa->ifa_addr;

				/* only accept global scope addresses */
				if (IN6_IS_ADDR_LINKLOCAL(&s->sin6_addr) ||
				    IN6_IS_ADDR_SITELOCAL(&s->sin6_addr))
					continue;
				sa2addr(ifa->ifa_addr, alt, NULL);
				break;
			}
		}
		break;
	default:
		log_warnx("%s: unsupported address family %s", __func__,
		    aid2str(local->aid));
		break;
	}

	freeifaddrs(ifap);
}

void
session_handle_update(struct peer *peer, struct ibuf *msg)
{
	/* pass the message verbatim to the rde. */
	imsg_rde(IMSG_UPDATE, peer->conf.id, ibuf_data(msg), ibuf_size(msg));
}

void
session_handle_rrefresh(struct peer *peer, struct route_refresh *rr)
{
	imsg_rde(IMSG_REFRESH, peer->conf.id, rr, sizeof(*rr));
}

void
session_graceful_restart(struct peer *p)
{
	uint8_t	i;
	uint16_t staletime = conf->staletime;

	if (p->conf.staletime)
		staletime = p->conf.staletime;

	/* RFC 8538: enforce configurable upper bound of the stale timer */
	if (staletime > p->capa.neg.grestart.timeout)
		staletime = p->capa.neg.grestart.timeout;
	timer_set(&p->timers, Timer_RestartTimeout, staletime);

	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.neg.grestart.flags[i] & CAPA_GR_PRESENT) {
			imsg_rde(IMSG_SESSION_STALE, p->conf.id,
			    &i, sizeof(i));
			log_peer_warnx(&p->conf,
			    "graceful restart of %s, keeping routes",
			    aid2str(i));
			p->capa.neg.grestart.flags[i] |= CAPA_GR_RESTARTING;
		} else if (p->capa.neg.mp[i]) {
			imsg_rde(IMSG_SESSION_NOGRACE, p->conf.id,
			    &i, sizeof(i));
			log_peer_warnx(&p->conf,
			    "graceful restart of %s, flushing routes",
			    aid2str(i));
		}
	}
}

void
session_graceful_stop(struct peer *p)
{
	uint8_t	i;

	for (i = AID_MIN; i < AID_MAX; i++) {
		/*
		 * Only flush if the peer is restarting and the timeout fired.
		 * In all other cases the session was already flushed when the
		 * session went down or when the new open message was parsed.
		 */
		if (p->capa.neg.grestart.flags[i] & CAPA_GR_RESTARTING)
			session_graceful_flush(p, i, "time-out");
		p->capa.neg.grestart.flags[i] &= ~CAPA_GR_RESTARTING;
	}
}

void
session_graceful_flush(struct peer *p, uint8_t aid, const char *why)
{
	log_peer_warnx(&p->conf, "graceful restart of %s, %s, flushing",
	    aid2str(aid), why);
	imsg_rde(IMSG_SESSION_FLUSH, p->conf.id, &aid, sizeof(aid));
}

void
session_mrt_dump_state(struct peer *p)
{
	struct mrt		*mrt;

	LIST_FOREACH(mrt, &mrthead, entry) {
		if (mrt->type != MRT_ALL_IN && mrt->type != MRT_ALL_OUT)
			continue;
		if ((mrt->peer_id == 0 && mrt->group_id == 0) ||
		    mrt->peer_id == p->conf.id || (mrt->group_id != 0 &&
		    mrt->group_id == p->conf.groupid))
			mrt_dump_state(mrt, p);
	}
}

void
session_mrt_dump_bgp_msg(struct peer *p, struct ibuf *msg,
     enum msg_type msgtype, enum directions dir)
{
	struct mrt		*mrt;

	LIST_FOREACH(mrt, &mrthead, entry) {
		if (dir == DIR_IN) {
			if (mrt->type != MRT_ALL_IN &&
			    (mrt->type != MRT_UPDATE_IN ||
			    msgtype != BGP_UPDATE))
				continue;
		} else {
			if (mrt->type != MRT_ALL_OUT &&
			    (mrt->type != MRT_UPDATE_OUT ||
			    msgtype != BGP_UPDATE))
				continue;
		}
		if ((mrt->peer_id == 0 && mrt->group_id == 0) ||
		    mrt->peer_id == p->conf.id || (mrt->group_id != 0 &&
		    mrt->group_id == p->conf.groupid))
			mrt_dump_bgp_msg(mrt, msg, p, msgtype);
	}
}

static int
la_cmp(struct listen_addr *a, struct listen_addr *b)
{
	struct sockaddr_in	*in_a, *in_b;
	struct sockaddr_in6	*in6_a, *in6_b;

	if (a->sa.ss_family != b->sa.ss_family)
		return (1);

	switch (a->sa.ss_family) {
	case AF_INET:
		in_a = (struct sockaddr_in *)&a->sa;
		in_b = (struct sockaddr_in *)&b->sa;
		if (in_a->sin_addr.s_addr != in_b->sin_addr.s_addr)
			return (1);
		if (in_a->sin_port != in_b->sin_port)
			return (1);
		break;
	case AF_INET6:
		in6_a = (struct sockaddr_in6 *)&a->sa;
		in6_b = (struct sockaddr_in6 *)&b->sa;
		if (memcmp(&in6_a->sin6_addr, &in6_b->sin6_addr,
		    sizeof(struct in6_addr)))
			return (1);
		if (in6_a->sin6_port != in6_b->sin6_port)
			return (1);
		break;
	default:
		fatal("king bula sez: unknown address family");
		/* NOTREACHED */
	}

	return (0);
}

void
session_dispatch_imsg(struct imsgbuf *imsgbuf, int idx, u_int *listener_cnt)
{
	struct imsg		 imsg;
	struct ibuf		 ibuf;
	struct mrt		 xmrt;
	struct route_refresh	 rr;
	struct mrt		*mrt;
	struct imsgbuf		*i;
	struct peer		*p;
	struct listen_addr	*la, *next, nla;
	struct session_dependon	 sdon;
	struct bgpd_config	 tconf;
	uint32_t		 peerid;
	int			 n, fd, depend_ok, restricted;
	uint16_t		 t;
	uint8_t			 aid, errcode, subcode;

	while (imsgbuf) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			fatal("session_dispatch_imsg: imsg_get error");

		if (n == 0)
			break;

		peerid = imsg_get_id(&imsg);
		switch (imsg_get_type(&imsg)) {
		case IMSG_SOCKET_CONN:
		case IMSG_SOCKET_CONN_CTL:
			if (idx != PFD_PIPE_MAIN)
				fatalx("reconf request not from parent");
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("expected to receive imsg fd to "
				    "RDE but didn't receive any");
				break;
			}
			if ((i = malloc(sizeof(struct imsgbuf))) == NULL)
				fatal(NULL);
			if (imsgbuf_init(i, fd) == -1 ||
			    imsgbuf_set_maxsize(i, MAX_BGPD_IMSGSIZE) == -1)
				fatal(NULL);
			if (imsg_get_type(&imsg) == IMSG_SOCKET_CONN) {
				if (ibuf_rde) {
					log_warnx("Unexpected imsg connection "
					    "to RDE received");
					imsgbuf_clear(ibuf_rde);
					free(ibuf_rde);
				}
				ibuf_rde = i;
			} else {
				if (ibuf_rde_ctl) {
					log_warnx("Unexpected imsg ctl "
					    "connection to RDE received");
					imsgbuf_clear(ibuf_rde_ctl);
					free(ibuf_rde_ctl);
				}
				ibuf_rde_ctl = i;
			}
			break;
		case IMSG_RECONF_CONF:
			if (idx != PFD_PIPE_MAIN)
				fatalx("reconf request not from parent");
			if (imsg_get_data(&imsg, &tconf, sizeof(tconf)) == -1)
				fatal("imsg_get_data");

			nconf = new_config();
			copy_config(nconf, &tconf);
			pending_reconf = 1;
			break;
		case IMSG_RECONF_PEER:
			if (idx != PFD_PIPE_MAIN)
				fatalx("reconf request not from parent");
			if ((p = calloc(1, sizeof(struct peer))) == NULL)
				fatal("new_peer");
			if (imsg_get_data(&imsg, &p->conf, sizeof(p->conf)) ==
			    -1)
				fatal("imsg_get_data");
			p->state = p->prev_state = STATE_NONE;
			p->reconf_action = RECONF_REINIT;
			if (RB_INSERT(peer_head, &nconf->peers, p) != NULL)
				fatalx("%s: peer tree is corrupt", __func__);
			break;
		case IMSG_RECONF_PEER_AUTH:
			if (idx != PFD_PIPE_MAIN)
				fatalx("reconf request not from parent");
			if ((p = getpeerbyid(nconf, peerid)) == NULL) {
				log_warnx("%s: no such peer: id=%u",
				    "IMSG_RECONF_PEER_AUTH", peerid);
				break;
			}
			if (pfkey_recv_conf(p, &imsg) == -1)
				fatal("pfkey_recv_conf");
			break;
		case IMSG_RECONF_LISTENER:
			if (idx != PFD_PIPE_MAIN)
				fatalx("reconf request not from parent");
			if (nconf == NULL)
				fatalx("IMSG_RECONF_LISTENER but no config");
			if (imsg_get_data(&imsg, &nla, sizeof(nla)) == -1)
				fatal("imsg_get_data");
			TAILQ_FOREACH(la, conf->listen_addrs, entry)
				if (!la_cmp(la, &nla))
					break;

			if (la == NULL) {
				if (nla.reconf != RECONF_REINIT)
					fatalx("king bula sez: "
					    "expected REINIT");

				if ((nla.fd = imsg_get_fd(&imsg)) == -1)
					log_warnx("expected to receive fd for "
					    "%s but didn't receive any",
					    log_sockaddr((struct sockaddr *)
					    &nla.sa, nla.sa_len));

				la = calloc(1, sizeof(struct listen_addr));
				if (la == NULL)
					fatal(NULL);
				memcpy(&la->sa, &nla.sa, sizeof(la->sa));
				la->flags = nla.flags;
				la->fd = nla.fd;
				la->reconf = RECONF_REINIT;
				TAILQ_INSERT_TAIL(nconf->listen_addrs, la,
				    entry);
			} else {
				if (nla.reconf != RECONF_KEEP)
					fatalx("king bula sez: expected KEEP");
				la->reconf = RECONF_KEEP;
			}

			break;
		case IMSG_RECONF_CTRL:
			if (idx != PFD_PIPE_MAIN)
				fatalx("reconf request not from parent");

			if (imsg_get_data(&imsg, &restricted,
			    sizeof(restricted)) == -1)
				fatal("imsg_get_data");
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("expected to receive fd for control "
				    "socket but didn't receive any");
				break;
			}
			if (restricted) {
				control_shutdown(rcsock);
				rcsock = fd;
			} else {
				control_shutdown(csock);
				csock = fd;
			}
			break;
		case IMSG_RECONF_DRAIN:
			switch (idx) {
			case PFD_PIPE_ROUTE:
				if (nconf != NULL)
					fatalx("got unexpected %s from RDE",
					    "IMSG_RECONF_DONE");
				imsg_compose(ibuf_main, IMSG_RECONF_DONE, 0, 0,
				    -1, NULL, 0);
				break;
			case PFD_PIPE_MAIN:
				if (nconf == NULL)
					fatalx("got unexpected %s from parent",
					    "IMSG_RECONF_DONE");
				imsg_compose(ibuf_main, IMSG_RECONF_DRAIN, 0, 0,
				    -1, NULL, 0);
				break;
			default:
				fatalx("reconf request not from parent or RDE");
			}
			break;
		case IMSG_RECONF_DONE:
			if (idx != PFD_PIPE_MAIN)
				fatalx("reconf request not from parent");
			if (nconf == NULL)
				fatalx("got IMSG_RECONF_DONE but no config");
			copy_config(conf, nconf);
			merge_peers(conf, nconf);

			/* delete old listeners */
			TAILQ_FOREACH_SAFE(la, conf->listen_addrs, entry,
			    next) {
				if (la->reconf == RECONF_NONE) {
					log_info("not listening on %s any more",
					    log_sockaddr((struct sockaddr *)
					    &la->sa, la->sa_len));
					TAILQ_REMOVE(conf->listen_addrs, la,
					    entry);
					close(la->fd);
					free(la);
				}
			}

			/* add new listeners */
			TAILQ_CONCAT(conf->listen_addrs, nconf->listen_addrs,
			    entry);

			setup_listeners(listener_cnt);
			free_config(nconf);
			nconf = NULL;
			pending_reconf = 0;
			log_info("SE reconfigured");
			/*
			 * IMSG_RECONF_DONE is sent when the RDE drained
			 * the peer config sent in merge_peers().
			 */
			break;
		case IMSG_SESSION_DEPENDON:
			if (idx != PFD_PIPE_MAIN)
				fatalx("IFINFO message not from parent");
			if (imsg_get_data(&imsg, &sdon, sizeof(sdon)) == -1)
				fatalx("DEPENDON imsg with wrong len");
			depend_ok = sdon.depend_state;

			RB_FOREACH(p, peer_head, &conf->peers)
				if (!strcmp(p->conf.if_depend, sdon.ifname)) {
					if (depend_ok && !p->depend_ok) {
						p->depend_ok = depend_ok;
						bgp_fsm(p, EVNT_START, NULL);
					} else if (!depend_ok && p->depend_ok) {
						p->depend_ok = depend_ok;
						session_stop(p,
						    ERR_CEASE_OTHER_CHANGE,
						    NULL);
					}
				}
			break;
		case IMSG_MRT_OPEN:
		case IMSG_MRT_REOPEN:
			if (idx != PFD_PIPE_MAIN)
				fatalx("mrt request not from parent");
			if (imsg_get_data(&imsg, &xmrt, sizeof(xmrt)) == -1) {
				log_warnx("mrt open, wrong imsg len");
				break;
			}

			if ((xmrt.fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("expected to receive fd for mrt dump "
				    "but didn't receive any");
				break;
			}

			mrt = mrt_get(&mrthead, &xmrt);
			if (mrt == NULL) {
				/* new dump */
				mrt = calloc(1, sizeof(struct mrt));
				if (mrt == NULL)
					fatal("session_dispatch_imsg");
				memcpy(mrt, &xmrt, sizeof(struct mrt));
				if ((mrt->wbuf = msgbuf_new()) == NULL)
					fatal("session_dispatch_imsg");
				LIST_INSERT_HEAD(&mrthead, mrt, entry);
			} else {
				/* old dump reopened */
				close(mrt->fd);
			}
			mrt->fd = xmrt.fd;
			break;
		case IMSG_MRT_CLOSE:
			if (idx != PFD_PIPE_MAIN)
				fatalx("mrt request not from parent");
			if (imsg_get_data(&imsg, &xmrt, sizeof(xmrt)) == -1) {
				log_warnx("mrt close, wrong imsg len");
				break;
			}

			mrt = mrt_get(&mrthead, &xmrt);
			if (mrt != NULL)
				mrt_done(mrt);
			break;
		case IMSG_CTL_KROUTE:
		case IMSG_CTL_KROUTE_ADDR:
		case IMSG_CTL_SHOW_NEXTHOP:
		case IMSG_CTL_SHOW_INTERFACE:
		case IMSG_CTL_SHOW_FIB_TABLES:
		case IMSG_CTL_SHOW_RTR:
		case IMSG_CTL_SHOW_TIMER:
			if (idx != PFD_PIPE_MAIN)
				fatalx("ctl kroute request not from parent");
			if (control_imsg_relay(&imsg, NULL) == -1)
				log_warn("control_imsg_relay");
			break;
		case IMSG_CTL_SHOW_NEIGHBOR:
			if (idx != PFD_PIPE_ROUTE_CTL)
				fatalx("ctl rib request not from RDE");
			if ((p = getpeerbyid(conf, peerid)) == NULL) {
				log_warnx("%s: no such peer: id=%u",
				    "IMSG_CTL_SHOW_NEIGHBOR", peerid);
				break;
			}
			if (control_imsg_relay(&imsg, p) == -1)
				log_warn("control_imsg_relay");
			break;
		case IMSG_CTL_SHOW_RIB:
		case IMSG_CTL_SHOW_RIB_PREFIX:
		case IMSG_CTL_SHOW_RIB_COMMUNITIES:
		case IMSG_CTL_SHOW_RIB_ATTR:
		case IMSG_CTL_SHOW_RIB_MEM:
		case IMSG_CTL_SHOW_NETWORK:
		case IMSG_CTL_SHOW_FLOWSPEC:
		case IMSG_CTL_SHOW_SET:
			if (idx != PFD_PIPE_ROUTE_CTL)
				fatalx("ctl rib request not from RDE");
			if (control_imsg_relay(&imsg, NULL) == -1)
				log_warn("control_imsg_relay");
			break;
		case IMSG_CTL_END:
		case IMSG_CTL_RESULT:
			if (control_imsg_relay(&imsg, NULL) == -1)
				log_warn("control_imsg_relay");
			break;
		case IMSG_UPDATE:
			if (idx != PFD_PIPE_ROUTE)
				fatalx("update request not from RDE");
			if ((p = getpeerbyid(conf, peerid)) == NULL) {
				log_warnx("%s: no such peer: id=%u",
				    "IMSG_UPDATE", peerid);
				break;
			}
			if (imsg_get_ibuf(&imsg, &ibuf) == -1)
				log_warn("RDE sent invalid update");
			else
				session_update(p, &ibuf);
			break;
		case IMSG_UPDATE_ERR:
			if (idx != PFD_PIPE_ROUTE)
				fatalx("update request not from RDE");
			if ((p = getpeerbyid(conf, peerid)) == NULL) {
				log_warnx("%s: no such peer: id=%u",
				    "IMSG_UPDATE_ERR", peerid);
				break;
			}
			if (imsg_get_ibuf(&imsg, &ibuf) == -1 ||
			    ibuf_get_n8(&ibuf, &errcode) == -1 ||
			    ibuf_get_n8(&ibuf, &subcode) == -1) {
				log_warnx("RDE sent invalid notification");
				break;
			}

			session_notification(p, errcode, subcode, &ibuf);
			switch (errcode) {
			case ERR_CEASE:
				switch (subcode) {
				case ERR_CEASE_MAX_PREFIX:
				case ERR_CEASE_MAX_SENT_PREFIX:
					t = p->conf.max_out_prefix_restart;
					if (subcode == ERR_CEASE_MAX_PREFIX)
						t = p->conf.max_prefix_restart;

					bgp_fsm(p, EVNT_STOP, NULL);
					if (t)
						timer_set(&p->timers,
						    Timer_IdleHold, 60 * t);
					break;
				default:
					bgp_fsm(p, EVNT_CON_FATAL, NULL);
					break;
				}
				break;
			default:
				bgp_fsm(p, EVNT_CON_FATAL, NULL);
				break;
			}
			break;
		case IMSG_REFRESH:
			if (idx != PFD_PIPE_ROUTE)
				fatalx("route refresh request not from RDE");
			if ((p = getpeerbyid(conf, peerid)) == NULL) {
				log_warnx("%s: no such peer: id=%u",
				    "IMSG_REFRESH", peerid);
				break;
			}
			if (imsg_get_data(&imsg, &rr, sizeof(rr)) == -1) {
				log_warnx("RDE sent invalid refresh msg");
				break;
			}
			if (rr.aid < AID_MIN || rr.aid >= AID_MAX)
				fatalx("IMSG_REFRESH: bad AID");
			session_rrefresh(p, rr.aid, rr.subtype);
			break;
		case IMSG_SESSION_RESTARTED:
			if (idx != PFD_PIPE_ROUTE)
				fatalx("session restart not from RDE");
			if ((p = getpeerbyid(conf, peerid)) == NULL) {
				log_warnx("%s: no such peer: id=%u",
				    "IMSG_SESSION_RESTARTED", peerid);
				break;
			}
			if (imsg_get_data(&imsg, &aid, sizeof(aid)) == -1) {
				log_warnx("RDE sent invalid restart msg");
				break;
			}
			if (aid < AID_MIN || aid >= AID_MAX)
				fatalx("IMSG_SESSION_RESTARTED: bad AID");
			if (p->capa.neg.grestart.flags[aid] &
			    CAPA_GR_RESTARTING) {
				log_peer_warnx(&p->conf,
				    "graceful restart of %s finished",
				    aid2str(aid));
				p->capa.neg.grestart.flags[aid] &=
				    ~CAPA_GR_RESTARTING;
				timer_stop(&p->timers, Timer_RestartTimeout);

				/* signal back to RDE to cleanup stale routes */
				imsg_rde(IMSG_SESSION_RESTARTED,
				    peerid, &aid, sizeof(aid));
			}
			break;
		default:
			break;
		}
		imsg_free(&imsg);
	}
}

struct peer *
getpeerbydesc(struct bgpd_config *c, const char *descr)
{
	struct peer	*p, *res = NULL;
	int		 match = 0;

	RB_FOREACH(p, peer_head, &c->peers)
		if (!strcmp(p->conf.descr, descr)) {
			res = p;
			match++;
		}

	if (match > 1)
		log_info("neighbor description \"%s\" not unique, request "
		    "aborted", descr);

	if (match == 1)
		return (res);
	else
		return (NULL);
}

struct peer *
getpeerbyip(struct bgpd_config *c, struct sockaddr *ip)
{
	struct bgpd_addr addr;
	struct peer	*p, *newpeer, *loose = NULL;
	uint32_t	 id;

	sa2addr(ip, &addr, NULL);

	/* we might want a more effective way to find peers by IP */
	RB_FOREACH(p, peer_head, &c->peers)
		if (!p->conf.template &&
		    !memcmp(&addr, &p->conf.remote_addr, sizeof(addr)))
			return (p);

	/* try template matching */
	RB_FOREACH(p, peer_head, &c->peers)
		if (p->conf.template &&
		    p->conf.remote_addr.aid == addr.aid &&
		    session_match_mask(p, &addr))
			if (loose == NULL || loose->conf.remote_masklen <
			    p->conf.remote_masklen)
				loose = p;

	if (loose != NULL) {
		/* clone */
		if ((newpeer = malloc(sizeof(struct peer))) == NULL)
			fatal(NULL);
		memcpy(newpeer, loose, sizeof(struct peer));
		for (id = PEER_ID_DYN_MAX; id > PEER_ID_STATIC_MAX; id--) {
			if (getpeerbyid(c, id) == NULL)	/* we found a free id */
				break;
		}
		newpeer->template = loose;
		session_template_clone(newpeer, ip, id, 0);
		newpeer->state = newpeer->prev_state = STATE_NONE;
		newpeer->reconf_action = RECONF_KEEP;
		newpeer->rpending = 0;
		newpeer->wbuf = NULL;
		init_peer(newpeer, c);
		/* start delete timer, it is stopped when session goes up. */
		timer_set(&newpeer->timers, Timer_SessionDown,
		    INTERVAL_SESSION_DOWN);
		bgp_fsm(newpeer, EVNT_START, NULL);
		if (RB_INSERT(peer_head, &c->peers, newpeer) != NULL)
			fatalx("%s: peer tree is corrupt", __func__);
		return (newpeer);
	}

	return (NULL);
}

struct peer *
getpeerbyid(struct bgpd_config *c, uint32_t peerid)
{
	static struct peer lookup;

	lookup.conf.id = peerid;

	return RB_FIND(peer_head, &c->peers, &lookup);
}

int
peer_matched(struct peer *p, struct ctl_neighbor *n)
{
	char *s;

	if (n && n->addr.aid) {
		if (memcmp(&p->conf.remote_addr, &n->addr,
		    sizeof(p->conf.remote_addr)))
			return 0;
	} else if (n && n->descr[0]) {
		s = n->is_group ? p->conf.group : p->conf.descr;
		/* cannot trust n->descr to be properly terminated */
		if (strncmp(s, n->descr, sizeof(n->descr)))
			return 0;
	}
	return 1;
}

void
session_template_clone(struct peer *p, struct sockaddr *ip, uint32_t id,
    uint32_t as)
{
	struct bgpd_addr	remote_addr;

	if (ip)
		sa2addr(ip, &remote_addr, NULL);
	else
		memcpy(&remote_addr, &p->conf.remote_addr, sizeof(remote_addr));

	memcpy(&p->conf, &p->template->conf, sizeof(struct peer_config));

	p->conf.id = id;

	if (as) {
		p->conf.remote_as = as;
		p->conf.ebgp = (p->conf.remote_as != p->conf.local_as);
		if (!p->conf.ebgp)
			/* force enforce_as off for iBGP sessions */
			p->conf.enforce_as = ENFORCE_AS_OFF;
	}

	memcpy(&p->conf.remote_addr, &remote_addr, sizeof(remote_addr));
	switch (p->conf.remote_addr.aid) {
	case AID_INET:
		p->conf.remote_masklen = 32;
		break;
	case AID_INET6:
		p->conf.remote_masklen = 128;
		break;
	}
	p->conf.template = 0;
}

int
session_match_mask(struct peer *p, struct bgpd_addr *a)
{
	struct bgpd_addr masked;

	applymask(&masked, a, p->conf.remote_masklen);
	if (memcmp(&masked, &p->conf.remote_addr, sizeof(masked)) == 0)
		return (1);
	return (0);
}

void
session_down(struct peer *peer)
{
	memset(&peer->capa.neg, 0, sizeof(peer->capa.neg));
	peer->stats.last_updown = getmonotime();

	timer_set(&peer->timers, Timer_SessionDown, INTERVAL_SESSION_DOWN);

	/*
	 * session_down is called in the exit code path so check
	 * if the RDE is still around, if not there is no need to
	 * send the message.
	 */
	if (ibuf_rde == NULL)
		return;
	imsg_rde(IMSG_SESSION_DOWN, peer->conf.id, NULL, 0);
}

void
session_up(struct peer *p)
{
	struct session_up	 sup;

	/* clear last errors, now that the session is up */
	p->stats.last_sent_errcode = 0;
	p->stats.last_sent_suberr = 0;
	p->stats.last_rcvd_errcode = 0;
	p->stats.last_rcvd_suberr = 0;
	memset(p->stats.last_reason, 0, sizeof(p->stats.last_reason));

	timer_stop(&p->timers, Timer_SessionDown);

	if (!p->rdesession) {
		/* inform rde about new peer */
		imsg_rde(IMSG_SESSION_ADD, p->conf.id,
		    &p->conf, sizeof(p->conf));
		p->rdesession = 1;
	}

	if (p->local.aid == AID_INET) {
		sup.local_v4_addr = p->local;
		sup.local_v6_addr = p->local_alt;
	} else {
		sup.local_v6_addr = p->local;
		sup.local_v4_addr = p->local_alt;
	}
	sup.remote_addr = p->remote;
	sup.if_scope = p->if_scope;

	sup.remote_bgpid = p->remote_bgpid;
	sup.short_as = p->short_as;
	memcpy(&sup.capa, &p->capa.neg, sizeof(sup.capa));
	p->stats.last_updown = getmonotime();
	imsg_rde(IMSG_SESSION_UP, p->conf.id, &sup, sizeof(sup));
}

int
imsg_ctl_parent(struct imsg *imsg)
{
	return imsg_forward(ibuf_main, imsg);
}

int
imsg_ctl_rde(struct imsg *imsg)
{
	if (ibuf_rde_ctl == NULL)
		return (0);
	/*
	 * Use control socket to talk to RDE to bypass the queue of the
	 * regular imsg socket.
	 */
	return imsg_forward(ibuf_rde_ctl, imsg);
}

int
imsg_ctl_rde_msg(int type, uint32_t peerid, pid_t pid)
{
	if (ibuf_rde_ctl == NULL)
		return (0);

	/*
	 * Use control socket to talk to RDE to bypass the queue of the
	 * regular imsg socket.
	 */
	return imsg_compose(ibuf_rde_ctl, type, peerid, pid, -1, NULL, 0);
}

void
imsg_rde(int type, uint32_t peerid, void *data, uint16_t datalen)
{
	if (ibuf_rde == NULL)
		return;
	if (imsg_compose(ibuf_rde, type, peerid, 0, -1, data, datalen) == -1)
		fatal("imsg_compose");
}

void
session_demote(struct peer *p, int level)
{
	struct demote_msg	msg;

	strlcpy(msg.demote_group, p->conf.demote_group,
	    sizeof(msg.demote_group));
	msg.level = level;
	if (imsg_compose(ibuf_main, IMSG_DEMOTE, p->conf.id, 0, -1,
	    &msg, sizeof(msg)) == -1)
		fatal("imsg_compose");

	p->demoted += level;
}

void
session_md5_reload(struct peer *p)
{
	if (!p->template)
		if (imsg_compose(ibuf_main, IMSG_PFKEY_RELOAD,
		    p->conf.id, 0, -1, NULL, 0) == -1)
			fatalx("imsg_compose error");
}

void
session_stop(struct peer *peer, uint8_t subcode, const char *reason)
{
	struct ibuf *ibuf;

	if (reason != NULL)
		strlcpy(peer->conf.reason, reason, sizeof(peer->conf.reason));

	ibuf = ibuf_dynamic(0, REASON_LEN);

	if ((subcode == ERR_CEASE_ADMIN_DOWN ||
	    subcode == ERR_CEASE_ADMIN_RESET) &&
	    reason != NULL && *reason != '\0' &&
	    ibuf != NULL) {
		if (ibuf_add_n8(ibuf, strlen(reason)) == -1 ||
		    ibuf_add(ibuf, reason, strlen(reason))) {
			log_peer_warnx(&peer->conf,
			    "trying to send overly long shutdown reason");
			ibuf_free(ibuf);
			ibuf = NULL;
		}
	}
	switch (peer->state) {
	case STATE_OPENSENT:
	case STATE_OPENCONFIRM:
	case STATE_ESTABLISHED:
		session_notification(peer, ERR_CEASE, subcode, ibuf);
		break;
	default:
		/* session not open, no need to send notification */
		if (subcode >= sizeof(suberr_cease_names) / sizeof(char *) ||
		    suberr_cease_names[subcode] == NULL)
			log_peer_warnx(&peer->conf, "session stop: %s, "
			    "unknown subcode %u", errnames[ERR_CEASE], subcode);
		else
			log_peer_warnx(&peer->conf, "session stop: %s, %s",
			    errnames[ERR_CEASE], suberr_cease_names[subcode]);
		break;
	}
	ibuf_free(ibuf);
	bgp_fsm(peer, EVNT_STOP, NULL);
}

struct bgpd_addr *
session_localaddr(struct peer *p)
{
	switch (p->conf.remote_addr.aid) {
	case AID_INET:
		return &p->conf.local_addr_v4;
	case AID_INET6:
		return &p->conf.local_addr_v6;
	}
	fatalx("Unknown AID in %s", __func__);
}

void
merge_peers(struct bgpd_config *c, struct bgpd_config *nc)
{
	struct peer *p, *np, *next;

	RB_FOREACH(p, peer_head, &c->peers) {
		/* templates are handled specially */
		if (p->template != NULL)
			continue;
		np = getpeerbyid(nc, p->conf.id);
		if (np == NULL) {
			p->reconf_action = RECONF_DELETE;
			continue;
		}

		/* peer no longer uses TCP MD5SIG so deconfigure */
		if (p->auth_conf.method == AUTH_MD5SIG &&
		    np->auth_conf.method != AUTH_MD5SIG)
			tcp_md5_del_listener(c, p);
		else if (np->auth_conf.method == AUTH_MD5SIG)
			tcp_md5_add_listener(c, np);

		memcpy(&p->conf, &np->conf, sizeof(p->conf));
		memcpy(&p->auth_conf, &np->auth_conf, sizeof(p->auth_conf));
		RB_REMOVE(peer_head, &nc->peers, np);
		free(np);

		p->reconf_action = RECONF_KEEP;

		/* reapply holdtime and min_holdtime settings */
		if (p->conf.holdtime == 0)
			p->conf.holdtime = nc->holdtime;
		if (p->conf.min_holdtime == 0)
			p->conf.min_holdtime = nc->min_holdtime;
		if (p->conf.connectretry == 0)
			p->conf.connectretry = nc->connectretry;
		p->local_bgpid = nc->bgpid;

		/* had demotion, is demoted, demote removed? */
		if (p->demoted && !p->conf.demote_group[0])
			session_demote(p, -1);

		/* if session is not open then refresh pfkey data */
		if (p->state < STATE_OPENSENT && !p->template)
			imsg_compose(ibuf_main, IMSG_PFKEY_RELOAD,
			    p->conf.id, 0, -1, NULL, 0);

		/*
		 * If the session is established or the SessionDown timer is
		 * running sync with the RDE
		 */
		if (p->rdesession)
			imsg_rde(IMSG_SESSION_ADD, p->conf.id,
			    &p->conf, sizeof(struct peer_config));

		/* apply the config to all clones of a template */
		if (p->conf.template) {
			struct peer *xp;
			RB_FOREACH(xp, peer_head, &c->peers) {
				if (xp->template != p)
					continue;
				session_template_clone(xp, NULL, xp->conf.id,
				    xp->conf.remote_as);

				if (p->rdesession)
					imsg_rde(IMSG_SESSION_ADD,
					    xp->conf.id, &xp->conf,
					    sizeof(xp->conf));
			}
		}
	}

	imsg_rde(IMSG_RECONF_DRAIN, 0, NULL, 0);

	/* pfkeys of new peers already loaded by the parent process */
	RB_FOREACH_SAFE(np, peer_head, &nc->peers, next) {
		RB_REMOVE(peer_head, &nc->peers, np);
		if (RB_INSERT(peer_head, &c->peers, np) != NULL)
			fatalx("%s: peer tree is corrupt", __func__);
		if (np->auth_conf.method == AUTH_MD5SIG)
			tcp_md5_add_listener(c, np);
	}
}
