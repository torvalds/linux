/*	$OpenBSD: control.c,v 1.135 2025/03/10 14:11:38 claudio Exp $ */

/*
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

TAILQ_HEAD(ctl_conns, ctl_conn) ctl_conns = TAILQ_HEAD_INITIALIZER(ctl_conns);

#define	CONTROL_BACKLOG	5

struct ctl_conn	*control_connbyfd(int);
struct ctl_conn	*control_connbypid(pid_t);
int		 control_close(struct ctl_conn *);
void		 control_result(struct ctl_conn *, u_int);

int
control_check(char *path)
{
	struct sockaddr_un	 sa_un;
	int			 fd;

	memset(&sa_un, 0, sizeof(sa_un));
	sa_un.sun_family = AF_UNIX;
	strlcpy(sa_un.sun_path, path, sizeof(sa_un.sun_path));

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) == -1) {
		log_warn("%s: socket", __func__);
		return (-1);
	}

	if (connect(fd, (struct sockaddr *)&sa_un, sizeof(sa_un)) == 0) {
		log_warnx("control socket %s already in use", path);
		close(fd);
		return (-1);
	}

	close(fd);

	return (0);
}

int
control_init(int restricted, char *path)
{
	struct sockaddr_un	 sa_un;
	int			 fd;
	mode_t			 old_umask, mode;

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    0)) == -1) {
		log_warn("control_init: socket");
		return (-1);
	}

	memset(&sa_un, 0, sizeof(sa_un));
	sa_un.sun_family = AF_UNIX;
	if (strlcpy(sa_un.sun_path, path, sizeof(sa_un.sun_path)) >=
	    sizeof(sa_un.sun_path)) {
		log_warn("control_init: socket name too long");
		close(fd);
		return (-1);
	}

	if (unlink(path) == -1)
		if (errno != ENOENT) {
			log_warn("control_init: unlink %s", path);
			close(fd);
			return (-1);
		}

	if (restricted) {
		old_umask = umask(S_IXUSR|S_IXGRP|S_IXOTH);
		mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
	} else {
		old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
		mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP;
	}

	if (bind(fd, (struct sockaddr *)&sa_un, sizeof(sa_un)) == -1) {
		log_warn("control_init: bind: %s", path);
		close(fd);
		umask(old_umask);
		return (-1);
	}

	umask(old_umask);

	if (chmod(path, mode) == -1) {
		log_warn("control_init: chmod: %s", path);
		close(fd);
		unlink(path);
		return (-1);
	}

	return (fd);
}

int
control_listen(int fd)
{
	if (fd != -1 && listen(fd, CONTROL_BACKLOG) == -1) {
		log_warn("control_listen: listen");
		return (-1);
	}

	return (0);
}

void
control_shutdown(int fd)
{
	close(fd);
}

size_t
control_fill_pfds(struct pollfd *pfd, size_t size)
{
	struct ctl_conn	*ctl_conn;
	size_t i = 0;

	TAILQ_FOREACH(ctl_conn, &ctl_conns, entry) {
		pfd[i].fd = ctl_conn->imsgbuf.fd;
		pfd[i].events = POLLIN;
		if (imsgbuf_queuelen(&ctl_conn->imsgbuf) > 0)
			pfd[i].events |= POLLOUT;
		i++;
	}
	return i;
}

unsigned int
control_accept(int listenfd, int restricted)
{
	int			 connfd;
	socklen_t		 len;
	struct sockaddr_un	 sa_un;
	struct ctl_conn		*ctl_conn;

	len = sizeof(sa_un);
	if ((connfd = accept4(listenfd,
	    (struct sockaddr *)&sa_un, &len,
	    SOCK_NONBLOCK | SOCK_CLOEXEC)) == -1) {
		if (errno == ENFILE || errno == EMFILE) {
			pauseaccept = getmonotime();
			return (0);
		} else if (errno != EWOULDBLOCK && errno != EINTR &&
		    errno != ECONNABORTED)
			log_warn("control_accept: accept");
		return (0);
	}

	if ((ctl_conn = calloc(1, sizeof(struct ctl_conn))) == NULL) {
		log_warn("control_accept");
		close(connfd);
		return (0);
	}

	if (imsgbuf_init(&ctl_conn->imsgbuf, connfd) == -1 ||
	    imsgbuf_set_maxsize(&ctl_conn->imsgbuf, MAX_BGPD_IMSGSIZE) == -1) {
		log_warn("control_accept");
		imsgbuf_clear(&ctl_conn->imsgbuf);
		close(connfd);
		free(ctl_conn);
		return (0);
	}
	ctl_conn->restricted = restricted;

	TAILQ_INSERT_TAIL(&ctl_conns, ctl_conn, entry);

	return (1);
}

struct ctl_conn *
control_connbyfd(int fd)
{
	struct ctl_conn	*c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (c->imsgbuf.fd == fd)
			break;
	}

	return (c);
}

struct ctl_conn *
control_connbypid(pid_t pid)
{
	struct ctl_conn	*c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (c->imsgbuf.pid == pid)
			break;
	}

	return (c);
}

int
control_close(struct ctl_conn *c)
{
	if (c->terminate && c->imsgbuf.pid)
		imsg_ctl_rde_msg(IMSG_CTL_TERMINATE, 0, c->imsgbuf.pid);

	imsgbuf_clear(&c->imsgbuf);
	TAILQ_REMOVE(&ctl_conns, c, entry);

	close(c->imsgbuf.fd);
	free(c);
	pauseaccept = monotime_clear();
	return (1);
}

int
control_dispatch_msg(struct pollfd *pfd, struct peer_head *peers)
{
	struct imsg		 imsg;
	struct ctl_neighbor	 neighbor;
	struct ctl_show_rib_request	ribreq;
	struct ctl_conn		*c;
	struct peer		*p;
	ssize_t			 n;
	uint32_t		 type;
	pid_t			 pid;
	int			 verbose, matched;

	if ((c = control_connbyfd(pfd->fd)) == NULL) {
		log_warn("control_dispatch_msg: fd %d: not found", pfd->fd);
		return (0);
	}

	if (pfd->revents & POLLOUT) {
		if (imsgbuf_write(&c->imsgbuf) == -1)
			return control_close(c);
		if (c->throttled &&
		    imsgbuf_queuelen(&c->imsgbuf) < CTL_MSG_LOW_MARK) {
			if (imsg_ctl_rde_msg(IMSG_XON, 0, c->imsgbuf.pid) != -1)
				c->throttled = 0;
		}
	}

	if (!(pfd->revents & POLLIN))
		return (0);

	if (imsgbuf_read(&c->imsgbuf) != 1)
		return control_close(c);

	for (;;) {
		if ((n = imsg_get(&c->imsgbuf, &imsg)) == -1)
			return control_close(c);

		if (n == 0)
			break;

		type = imsg_get_type(&imsg);
		pid = imsg_get_pid(&imsg);
		if (c->restricted) {
			switch (type) {
			case IMSG_CTL_SHOW_NEIGHBOR:
			case IMSG_CTL_SHOW_NEXTHOP:
			case IMSG_CTL_SHOW_INTERFACE:
			case IMSG_CTL_SHOW_RIB_MEM:
			case IMSG_CTL_SHOW_TERSE:
			case IMSG_CTL_SHOW_TIMER:
			case IMSG_CTL_SHOW_NETWORK:
			case IMSG_CTL_SHOW_FLOWSPEC:
			case IMSG_CTL_SHOW_RIB:
			case IMSG_CTL_SHOW_RIB_PREFIX:
			case IMSG_CTL_SHOW_SET:
			case IMSG_CTL_SHOW_RTR:
				break;
			default:
				/* clear imsg type to prevent processing */
				type = IMSG_NONE;
				control_result(c, CTL_RES_DENIED);
				break;
			}
		}

		/*
		 * TODO: this is wrong and shoud work the other way around.
		 * The imsg.hdr.pid is from the remote end and should not
		 * be trusted.
		 */
		c->imsgbuf.pid = pid;
		switch (type) {
		case IMSG_NONE:
			/* message was filtered out, nothing to do */
			break;
		case IMSG_CTL_FIB_COUPLE:
		case IMSG_CTL_FIB_DECOUPLE:
			imsg_ctl_parent(&imsg);
			break;
		case IMSG_CTL_SHOW_TERSE:
			RB_FOREACH(p, peer_head, peers)
				imsg_compose(&c->imsgbuf,
				    IMSG_CTL_SHOW_NEIGHBOR, 0, 0, -1,
				    p, sizeof(struct peer));
			imsg_compose(&c->imsgbuf, IMSG_CTL_END, 0, 0, -1,
			    NULL, 0);
			break;
		case IMSG_CTL_SHOW_NEIGHBOR:
			if (imsg_get_data(&imsg, &neighbor,
			    sizeof(neighbor)) == -1)
				memset(&neighbor, 0, sizeof(neighbor));

			matched = 0;
			RB_FOREACH(p, peer_head, peers) {
				if (!peer_matched(p, &neighbor))
					continue;

				matched = 1;
				if (!neighbor.show_timers) {
					imsg_ctl_rde_msg(type,
					    p->conf.id, pid);
				} else {
					u_int			 i;
					monotime_t		 d;
					struct ctl_timer	 ct;

					imsg_compose(&c->imsgbuf,
					    IMSG_CTL_SHOW_NEIGHBOR,
					    0, 0, -1, p, sizeof(*p));
					for (i = 1; i < Timer_Max; i++) {
						if (!timer_running(&p->timers,
						    i, &d))
							continue;
						ct.type = i;
						ct.val = d;
						imsg_compose(&c->imsgbuf,
						    IMSG_CTL_SHOW_TIMER,
						    0, 0, -1, &ct, sizeof(ct));
					}
				}
			}
			if (!matched && !RB_EMPTY(peers)) {
				control_result(c, CTL_RES_NOSUCHPEER);
			} else if (!neighbor.show_timers) {
				imsg_ctl_rde_msg(IMSG_CTL_END, 0, pid);
			} else {
				imsg_compose(&c->imsgbuf, IMSG_CTL_END, 0, 0,
				    -1, NULL, 0);
			}
			break;
		case IMSG_CTL_NEIGHBOR_UP:
		case IMSG_CTL_NEIGHBOR_DOWN:
		case IMSG_CTL_NEIGHBOR_CLEAR:
		case IMSG_CTL_NEIGHBOR_RREFRESH:
		case IMSG_CTL_NEIGHBOR_DESTROY:
			if (imsg_get_data(&imsg, &neighbor,
			    sizeof(neighbor)) == -1) {
				log_warnx("got IMSG_CTL_NEIGHBOR_ with "
				    "wrong length");
				break;
			}

			matched = 0;
			RB_FOREACH(p, peer_head, peers) {
				if (!peer_matched(p, &neighbor))
					continue;

				matched = 1;

				switch (type) {
				case IMSG_CTL_NEIGHBOR_UP:
					bgp_fsm(p, EVNT_START, NULL);
					p->conf.down = 0;
					p->conf.reason[0] = '\0';
					p->IdleHoldTime =
					    INTERVAL_IDLE_HOLD_INITIAL;
					p->errcnt = 0;
					control_result(c, CTL_RES_OK);
					break;
				case IMSG_CTL_NEIGHBOR_DOWN:
					neighbor.reason[
					    sizeof(neighbor.reason) - 1] = '\0';
					p->conf.down = 1;
					session_stop(p, ERR_CEASE_ADMIN_DOWN,
					    neighbor.reason);
					control_result(c, CTL_RES_OK);
					break;
				case IMSG_CTL_NEIGHBOR_CLEAR:
					neighbor.reason[
					    sizeof(neighbor.reason) - 1] = '\0';
					p->IdleHoldTime =
					    INTERVAL_IDLE_HOLD_INITIAL;
					p->errcnt = 0;
					if (!p->conf.down) {
						session_stop(p,
						    ERR_CEASE_ADMIN_RESET,
						    neighbor.reason);
						timer_set(&p->timers,
						    Timer_IdleHold,
						    SESSION_CLEAR_DELAY);
					} else {
						session_stop(p,
						    ERR_CEASE_ADMIN_DOWN,
						    neighbor.reason);
					}
					control_result(c, CTL_RES_OK);
					break;
				case IMSG_CTL_NEIGHBOR_RREFRESH:
					if (session_neighbor_rrefresh(p))
						control_result(c,
						    CTL_RES_NOCAP);
					else
						control_result(c, CTL_RES_OK);
					break;
				case IMSG_CTL_NEIGHBOR_DESTROY:
					if (!p->template)
						control_result(c,
						    CTL_RES_BADPEER);
					else if (p->state != STATE_IDLE)
						control_result(c,
						    CTL_RES_BADSTATE);
					else {
						/*
						 * Mark as deleted, will be
						 * collected on next poll loop.
						 */
						p->reconf_action =
						    RECONF_DELETE;
						control_result(c, CTL_RES_OK);
					}
					break;
				default:
					fatal("king bula wants more humppa");
				}
			}
			if (!matched)
				control_result(c, CTL_RES_NOSUCHPEER);
			break;
		case IMSG_CTL_RELOAD:
		case IMSG_CTL_SHOW_INTERFACE:
		case IMSG_CTL_SHOW_FIB_TABLES:
		case IMSG_CTL_SHOW_RTR:
			imsg_ctl_parent(&imsg);
			break;
		case IMSG_CTL_KROUTE:
		case IMSG_CTL_KROUTE_ADDR:
		case IMSG_CTL_SHOW_NEXTHOP:
			imsg_ctl_parent(&imsg);
			break;
		case IMSG_CTL_SHOW_RIB:
		case IMSG_CTL_SHOW_RIB_PREFIX:
			if (imsg_get_data(&imsg, &ribreq, sizeof(ribreq)) ==
			    -1) {
				log_warnx("got IMSG_CTL_SHOW_RIB with "
				    "wrong length");
				break;
			}

			/* check if at least one neighbor exists */
			RB_FOREACH(p, peer_head, peers)
				if (peer_matched(p, &ribreq.neighbor))
					break;
			if (p == NULL && !RB_EMPTY(peers)) {
				control_result(c, CTL_RES_NOSUCHPEER);
				break;
			}

			if (type == IMSG_CTL_SHOW_RIB_PREFIX &&
			    ribreq.prefix.aid == AID_UNSPEC) {
				/* malformed request, must specify af */
				control_result(c, CTL_RES_PARSE_ERROR);
				break;
			}

			c->terminate = 1;
			imsg_ctl_rde(&imsg);
			break;
		case IMSG_CTL_SHOW_NETWORK:
		case IMSG_CTL_SHOW_FLOWSPEC:
			c->terminate = 1;
			/* FALLTHROUGH */
		case IMSG_CTL_SHOW_RIB_MEM:
		case IMSG_CTL_SHOW_SET:
			imsg_ctl_rde(&imsg);
			break;
		case IMSG_NETWORK_ADD:
		case IMSG_NETWORK_ASPATH:
		case IMSG_NETWORK_ATTR:
		case IMSG_NETWORK_REMOVE:
		case IMSG_NETWORK_FLUSH:
		case IMSG_NETWORK_DONE:
		case IMSG_FLOWSPEC_ADD:
		case IMSG_FLOWSPEC_REMOVE:
		case IMSG_FLOWSPEC_DONE:
		case IMSG_FLOWSPEC_FLUSH:
		case IMSG_FILTER_SET:
			imsg_ctl_rde(&imsg);
			break;
		case IMSG_CTL_LOG_VERBOSE:
			if (imsg_get_data(&imsg, &verbose, sizeof(verbose)) ==
			    -1)
				break;

			/* forward to other processes */
			imsg_ctl_parent(&imsg);
			imsg_ctl_rde(&imsg);
			log_setverbose(verbose);
			break;
		default:
			break;
		}
		imsg_free(&imsg);
	}

	return (0);
}

int
control_imsg_relay(struct imsg *imsg, struct peer *p)
{
	struct ctl_conn	*c;
	uint32_t type;
	pid_t pid;

	type = imsg_get_type(imsg);
	pid = imsg_get_pid(imsg);

	/* not an error if the connection got closed */
	if ((c = control_connbypid(pid)) == NULL)
		return (0);

	/* special handling for peers since only the stats are sent from RDE */
	if (type == IMSG_CTL_SHOW_NEIGHBOR) {
		struct rde_peer_stats stats;
		struct peer peer;

		if (p == NULL) {
			errno = EINVAL;
			return (-1);
		}
		if (imsg_get_data(imsg, &stats, sizeof(stats)) == -1)
			return (-1);

		peer = *p;
		explicit_bzero(&peer.auth_conf, sizeof(peer.auth_conf));
		peer.auth_conf.method = p->auth_conf.method;
		peer.stats.prefix_cnt = stats.prefix_cnt;
		peer.stats.prefix_out_cnt = stats.prefix_out_cnt;
		peer.stats.prefix_rcvd_update = stats.prefix_rcvd_update;
		peer.stats.prefix_rcvd_withdraw = stats.prefix_rcvd_withdraw;
		peer.stats.prefix_rcvd_eor = stats.prefix_rcvd_eor;
		peer.stats.prefix_sent_update = stats.prefix_sent_update;
		peer.stats.prefix_sent_withdraw = stats.prefix_sent_withdraw;
		peer.stats.prefix_sent_eor = stats.prefix_sent_eor;
		peer.stats.pending_update = stats.pending_update;
		peer.stats.pending_withdraw = stats.pending_withdraw;
		peer.stats.msg_queue_len = msgbuf_queuelen(p->wbuf);

		return imsg_compose(&c->imsgbuf, type, 0, pid, -1,
		    &peer, sizeof(peer));
	}

	/* if command finished no need to send exit message */
	if (type == IMSG_CTL_END || type == IMSG_CTL_RESULT)
		c->terminate = 0;

	if (!c->throttled &&
	    imsgbuf_queuelen(&c->imsgbuf) > CTL_MSG_HIGH_MARK) {
		if (imsg_ctl_rde_msg(IMSG_XOFF, 0, pid) != -1)
			c->throttled = 1;
	}

	return imsg_forward(&c->imsgbuf, imsg);
}

void
control_result(struct ctl_conn *c, u_int code)
{
	imsg_compose(&c->imsgbuf, IMSG_CTL_RESULT, 0, c->imsgbuf.pid, -1,
	    &code, sizeof(code));
}
