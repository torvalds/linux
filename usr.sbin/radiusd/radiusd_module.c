/*	$OpenBSD: radiusd_module.c,v 1.26 2024/11/21 13:43:10 claudio Exp $	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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

/* radiusd_module.c -- helper functions for radiusd modules */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <imsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <pwd.h>

#include "radiusd.h"
#include "radiusd_module.h"
#include "imsg_subr.h"

static void	(*module_config_set) (void *, const char *, int,
		    char * const *) = NULL;
static void	(*module_start_module) (void *) = NULL;
static void	(*module_stop_module) (void *) = NULL;
static void	(*module_userpass) (void *, u_int, const char *, const char *)
		    = NULL;
static void	(*module_access_request) (void *, u_int, const u_char *,
		    size_t) = NULL;
static void	(*module_next_response) (void *, u_int, const u_char *,
		    size_t) = NULL;
static void	(*module_request_decoration) (void *, u_int, const u_char *,
		    size_t) = NULL;
static void	(*module_response_decoration) (void *, u_int, const u_char *,
		    size_t, const u_char *, size_t) = NULL;
static void	(*module_accounting_request) (void *, u_int, const u_char *,
		    size_t) = NULL;
static void	(*module_dispatch_control) (void *, struct imsg *) = NULL;

struct module_base {
	void			*ctx;
	struct imsgbuf		 ibuf;
	bool			 priv_dropped;

	/* Buffer for receiving the RADIUS packet */
	u_char			*radpkt;
	int			 radpktsiz;
	int			 radpktoff;
	u_char			*radpkt2;
	int			 radpkt2siz;	/* allocated size */
	int			 radpkt2len;	/* actual size */

#ifdef USE_LIBEVENT
	struct module_imsgbuf	*module_imsgbuf;
	bool			 writeready;
	bool			 stopped;
	bool			 ev_onhandler;
	struct event		 ev;
#endif
};

static int	 module_common_radpkt(struct module_base *, uint32_t, u_int,
		    const u_char *, size_t);
static int	 module_recv_imsg(struct module_base *);
static int	 module_imsg_handler(struct module_base *, struct imsg *);
#ifdef USE_LIBEVENT
static void	 module_on_event(int, short, void *);
#endif
static void	 module_reset_event(struct module_base *);

struct module_base *
module_create(int sock, void *ctx, struct module_handlers *handler)
{
	struct module_base	*base;

	if ((base = calloc(1, sizeof(struct module_base))) == NULL)
		return (NULL);

	if (imsgbuf_init(&base->ibuf, sock) == -1) {
		free(base);
		return (NULL);
	}
	base->ctx = ctx;

	module_userpass = handler->userpass;
	module_access_request = handler->access_request;
	module_next_response = handler->next_response;
	module_config_set = handler->config_set;
	module_request_decoration = handler->request_decoration;
	module_response_decoration = handler->response_decoration;
	module_accounting_request = handler->accounting_request;
	module_start_module = handler->start;
	module_stop_module = handler->stop;
	module_dispatch_control = handler->dispatch_control;

	return (base);
}

void
module_start(struct module_base *base)
{
#ifdef USE_LIBEVENT
	int	 ival;

	if ((ival = fcntl(base->ibuf.fd, F_GETFL)) == -1)
		err(1, "Failed to F_GETFL");
	if (fcntl(base->ibuf.fd, F_SETFL, ival | O_NONBLOCK) == -1)
		err(1, "Failed to setup NONBLOCK");
	event_set(&base->ev, base->ibuf.fd, EV_READ, module_on_event, base);
	event_add(&base->ev, NULL);
#endif
}

int
module_run(struct module_base *base)
{
	int	 ret;

	ret = module_recv_imsg(base);
	if (ret == 0)
		imsgbuf_flush(&base->ibuf);

	return (ret);
}

void
module_destroy(struct module_base *base)
{
	if (base != NULL) {
		free(base->radpkt);
		free(base->radpkt2);
		imsgbuf_clear(&base->ibuf);
	}
	free(base);
}

void
module_load(struct module_base *base)
{
	struct radiusd_module_load_arg	 load;

	memset(&load, 0, sizeof(load));
	if (module_userpass != NULL)
		load.cap |= RADIUSD_MODULE_CAP_USERPASS;
	if (module_access_request != NULL)
		load.cap |= RADIUSD_MODULE_CAP_ACCSREQ;
	if (module_next_response != NULL)
		load.cap |= RADIUSD_MODULE_CAP_NEXTRES;
	if (module_request_decoration != NULL)
		load.cap |= RADIUSD_MODULE_CAP_REQDECO;
	if (module_response_decoration != NULL)
		load.cap |= RADIUSD_MODULE_CAP_RESDECO;
	if (module_accounting_request != NULL)
		load.cap |= RADIUSD_MODULE_CAP_ACCTREQ;
	if (module_dispatch_control != NULL)
		load.cap |= RADIUSD_MODULE_CAP_CONTROL;
	imsg_compose(&base->ibuf, IMSG_RADIUSD_MODULE_LOAD, 0, 0, -1, &load,
	    sizeof(load));
	imsgbuf_flush(&base->ibuf);
}

void
module_drop_privilege(struct module_base *base, int nochroot)
{
	struct passwd	*pw;

	tzset();

	/* Drop the privilege */
	if ((pw = getpwnam(RADIUSD_USER)) == NULL)
		goto on_fail;
	if (nochroot == 0 && chroot(pw->pw_dir) == -1)
		goto on_fail;
	if (chdir("/") == -1)
		goto on_fail;
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		goto on_fail;
	base->priv_dropped = true;

on_fail:
	return;
}

int
module_notify_secret(struct module_base *base, const char *secret)
{
	int		 ret;

	ret = imsg_compose(&base->ibuf, IMSG_RADIUSD_MODULE_NOTIFY_SECRET,
	    0, 0, -1, secret, strlen(secret) + 1);
	module_reset_event(base);

	return (ret);
}

int
module_send_message(struct module_base *base, uint32_t cmd, const char *fmt,
    ...)
{
	char	*msg;
	va_list	 ap;
	int	 ret;

	if (fmt == NULL)
		ret = imsg_compose(&base->ibuf, cmd, 0, 0, -1, NULL, 0);
	else {
		va_start(ap, fmt);
		vasprintf(&msg, fmt, ap);
		va_end(ap);
		if (msg == NULL)
			return (-1);
		ret = imsg_compose(&base->ibuf, cmd, 0, 0, -1, msg,
		    strlen(msg) + 1);
		free(msg);
	}
	module_reset_event(base);

	return (ret);
}

int
module_userpass_ok(struct module_base *base, u_int q_id, const char *msg)
{
	int		 ret;
	struct iovec	 iov[2];

	iov[0].iov_base = &q_id;
	iov[0].iov_len = sizeof(q_id);
	iov[1].iov_base = (char *)msg;
	iov[1].iov_len = strlen(msg) + 1;
	ret = imsg_composev(&base->ibuf, IMSG_RADIUSD_MODULE_USERPASS_OK,
	    0, 0, -1, iov, 2);
	module_reset_event(base);

	return (ret);
}

int
module_userpass_fail(struct module_base *base, u_int q_id, const char *msg)
{
	int		 ret;
	struct iovec	 iov[2];

	iov[0].iov_base = &q_id;
	iov[0].iov_len = sizeof(q_id);
	iov[1].iov_base = (char *)msg;
	iov[1].iov_len = strlen(msg) + 1;
	ret = imsg_composev(&base->ibuf, IMSG_RADIUSD_MODULE_USERPASS_FAIL,
	    0, 0, -1, iov, 2);
	module_reset_event(base);

	return (ret);
}

int
module_accsreq_answer(struct module_base *base, u_int q_id, const u_char *pkt,
    size_t pktlen)
{
	return (module_common_radpkt(base, IMSG_RADIUSD_MODULE_ACCSREQ_ANSWER,
	    q_id, pkt, pktlen));
}

int
module_accsreq_next(struct module_base *base, u_int q_id, const u_char *pkt,
    size_t pktlen)
{
	return (module_common_radpkt(base, IMSG_RADIUSD_MODULE_ACCSREQ_NEXT,
	    q_id, pkt, pktlen));
}

int
module_accsreq_aborted(struct module_base *base, u_int q_id)
{
	int	 ret;

	ret = imsg_compose(&base->ibuf, IMSG_RADIUSD_MODULE_ACCSREQ_ABORTED,
	    0, 0, -1, &q_id, sizeof(u_int));
	module_reset_event(base);

	return (ret);
}

int
module_reqdeco_done(struct module_base *base, u_int q_id, const u_char *pkt,
    size_t pktlen)
{
	return (module_common_radpkt(base, IMSG_RADIUSD_MODULE_REQDECO_DONE,
	    q_id, pkt, pktlen));
}

int
module_resdeco_done(struct module_base *base, u_int q_id, const u_char *pkt,
    size_t pktlen)
{
	return (module_common_radpkt(base, IMSG_RADIUSD_MODULE_RESDECO_DONE,
	    q_id, pkt, pktlen));
}

static int
module_common_radpkt(struct module_base *base, uint32_t imsg_type, u_int q_id,
    const u_char *pkt, size_t pktlen)
{
	int		 ret = 0, off = 0, len, siz;
	struct iovec	 iov[2];
	struct radiusd_module_radpkt_arg	 ans;

	len = pktlen;
	ans.q_id = q_id;
	ans.pktlen = pktlen;
	ans.final = false;

	while (!ans.final) {
		siz = MAX_IMSGSIZE - sizeof(ans);
		if (len - off <= siz) {
			ans.final = true;
			siz = len - off;
		}
		iov[0].iov_base = &ans;
		iov[0].iov_len = sizeof(ans);
		if (siz > 0) {
			iov[1].iov_base = (u_char *)pkt + off;
			iov[1].iov_len = siz;
		}
		ret = imsg_composev(&base->ibuf, imsg_type, 0, 0, -1, iov,
		    (siz > 0)? 2 : 1);
		if (ret == -1)
			break;
		off += siz;
	}
	module_reset_event(base);

	return (ret);
}

static int
module_recv_imsg(struct module_base *base)
{
	ssize_t		 n;
	struct imsg	 imsg;

	if ((n = imsgbuf_read(&base->ibuf)) != 1) {
		if (n == -1)
			syslog(LOG_ERR, "%s: imsgbuf_read(): %m", __func__);
		module_stop(base);
		return (-1);
	}
	for (;;) {
		if ((n = imsg_get(&base->ibuf, &imsg)) == -1) {
			syslog(LOG_ERR, "%s: imsg_get(): %m", __func__);
			module_stop(base);
			return (-1);
		}
		if (n == 0)
			break;
		module_imsg_handler(base, &imsg);
		imsg_free(&imsg);
	}
	module_reset_event(base);

	return (0);
}

static int
module_imsg_handler(struct module_base *base, struct imsg *imsg)
{
	ssize_t	 datalen;

	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;
	switch (imsg->hdr.type) {
	case IMSG_RADIUSD_MODULE_SET_CONFIG:
	    {
		struct radiusd_module_set_arg	 *arg;
		struct radiusd_module_object	 *val;
		u_int				  i;
		size_t				  off;
		char				**argv;

		arg = (struct radiusd_module_set_arg *)imsg->data;
		off = sizeof(struct radiusd_module_set_arg);

		if ((argv = calloc(sizeof(const char *), arg->nparamval))
		    == NULL) {
			module_send_message(base, IMSG_NG,
			    "Out of memory: %s", strerror(errno));
			break;
		}
		for (i = 0; i < arg->nparamval; i++) {
			if (datalen - off <
			    sizeof(struct radiusd_module_object))
				break;
			val = (struct radiusd_module_object *)
			    ((caddr_t)imsg->data + off);
			if (datalen - off < val->size)
				break;
			argv[i] = (char *)(val + 1);
			off += val->size;
		}
		if (i >= arg->nparamval)
			module_config_set(base->ctx, arg->paramname,
			    arg->nparamval, argv);
		else
			module_send_message(base, IMSG_NG,
			    "Internal protocol error");
		free(argv);

		break;
	    }
	case IMSG_RADIUSD_MODULE_START:
		if (module_start_module != NULL) {
			module_start_module(base->ctx);
			if (!base->priv_dropped) {
				syslog(LOG_ERR, "Module tried to start with "
				    "root privileges");
				abort();
			}
		} else {
			if (!base->priv_dropped) {
				syslog(LOG_ERR, "Module tried to start with "
				    "root privileges");
				abort();
			}
			module_send_message(base, IMSG_OK, NULL);
		}
		break;
	case IMSG_RADIUSD_MODULE_STOP:
		module_stop(base);
		break;
	case IMSG_RADIUSD_MODULE_USERPASS:
	    {
		struct radiusd_module_userpass_arg *userpass;

		if (module_userpass == NULL) {
			syslog(LOG_ERR, "Received USERPASS message, but "
			    "module doesn't support");
			break;
		}
		if (datalen <
		    (ssize_t)sizeof(struct radiusd_module_userpass_arg)) {
			syslog(LOG_ERR, "Received USERPASS message, but "
			    "length is wrong");
			break;
		}
		userpass = (struct radiusd_module_userpass_arg *)imsg->data;
		module_userpass(base->ctx, userpass->q_id, userpass->user,
		    (userpass->has_pass)? userpass->pass : NULL);
		explicit_bzero(userpass,
		    sizeof(struct radiusd_module_userpass_arg));
		break;
	    }
	case IMSG_RADIUSD_MODULE_ACCSREQ:
	case IMSG_RADIUSD_MODULE_NEXTRES:
	case IMSG_RADIUSD_MODULE_REQDECO:
	case IMSG_RADIUSD_MODULE_RESDECO0_REQ:
	case IMSG_RADIUSD_MODULE_RESDECO:
	case IMSG_RADIUSD_MODULE_ACCTREQ:
	    {
		struct radiusd_module_radpkt_arg	*accessreq;
		int					 chunklen;
		const char				*typestr;

		if (imsg->hdr.type == IMSG_RADIUSD_MODULE_ACCSREQ) {
			if (module_access_request == NULL) {
				syslog(LOG_ERR, "Received ACCSREQ message, but "
				    "module doesn't support");
				break;
			}
			typestr = "ACCSREQ";
		} else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_NEXTRES) {
			if (module_next_response == NULL) {
				syslog(LOG_ERR, "Received NEXTRES message, but "
				    "module doesn't support");
				break;
			}
			typestr = "NEXTRES";
		} else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_ACCTREQ) {
			if (module_accounting_request == NULL) {
				syslog(LOG_ERR, "Received ACCTREQ message, but "
				    "module doesn't support");
				break;
			}
			typestr = "ACCTREQ";
		} else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_REQDECO) {
			if (module_request_decoration == NULL) {
				syslog(LOG_ERR, "Received REQDECO message, but "
				    "module doesn't support");
				break;
			}
			typestr = "REQDECO";
		} else {
			if (module_response_decoration == NULL) {
				syslog(LOG_ERR, "Received RESDECO message, but "
				    "module doesn't support");
				break;
			}
			if (imsg->hdr.type == IMSG_RADIUSD_MODULE_RESDECO0_REQ)
				typestr = "RESDECO0_REQ";
			else
				typestr = "RESDECO";
		}

		if (datalen <
		    (ssize_t)sizeof(struct radiusd_module_radpkt_arg)) {
			syslog(LOG_ERR, "Received %s message, but "
			    "length is wrong", typestr);
			break;
		}
		accessreq = (struct radiusd_module_radpkt_arg *)imsg->data;
		if (base->radpktsiz < accessreq->pktlen) {
			u_char *nradpkt;
			if ((nradpkt = realloc(base->radpkt,
			    accessreq->pktlen)) == NULL) {
				syslog(LOG_ERR, "Could not handle received "
				    "%s message: %m", typestr);
				base->radpktoff = 0;
				goto accsreq_out;
			}
			base->radpkt = nradpkt;
			base->radpktsiz = accessreq->pktlen;
		}
		chunklen = datalen - sizeof(struct radiusd_module_radpkt_arg);
		if (chunklen > base->radpktsiz - base->radpktoff){
			syslog(LOG_ERR,
			    "Could not handle received %s message: "
			    "received length is too big", typestr);
			base->radpktoff = 0;
			goto accsreq_out;
		}
		memcpy(base->radpkt + base->radpktoff,
		    (caddr_t)(accessreq + 1), chunklen);
		base->radpktoff += chunklen;
		if (!accessreq->final)
			goto accsreq_out;
		if (base->radpktoff != accessreq->pktlen) {
			syslog(LOG_ERR,
			    "Could not handle received %s "
			    "message: length is mismatch", typestr);
			base->radpktoff = 0;
			goto accsreq_out;
		}
		if (imsg->hdr.type == IMSG_RADIUSD_MODULE_ACCSREQ)
			module_access_request(base->ctx, accessreq->q_id,
			    base->radpkt, base->radpktoff);
		else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_NEXTRES)
			module_next_response(base->ctx, accessreq->q_id,
			    base->radpkt, base->radpktoff);
		else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_REQDECO)
			module_request_decoration(base->ctx, accessreq->q_id,
			    base->radpkt, base->radpktoff);
		else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_RESDECO0_REQ) {
			/* preserve request */
			if (base->radpktoff > base->radpkt2siz) {
				u_char *nradpkt;
				if ((nradpkt = realloc(base->radpkt2,
				    base->radpktoff)) == NULL) {
					syslog(LOG_ERR, "Could not handle "
					    "received %s message: %m", typestr);
					base->radpktoff = 0;
					goto accsreq_out;
				}
				base->radpkt2 = nradpkt;
				base->radpkt2siz = base->radpktoff;
			}
			memcpy(base->radpkt2, base->radpkt, base->radpktoff);
			base->radpkt2len = base->radpktoff;
		} else if (imsg->hdr.type == IMSG_RADIUSD_MODULE_RESDECO) {
			module_response_decoration(base->ctx, accessreq->q_id,
			    base->radpkt2, base->radpkt2len, base->radpkt,
			    base->radpktoff);
			base->radpkt2len = 0;
		} else
			module_accounting_request(base->ctx, accessreq->q_id,
			    base->radpkt, base->radpktoff);
		base->radpktoff = 0;
 accsreq_out:
		break;
	    }
	case IMSG_RADIUSD_MODULE_CTRL_UNBIND:
		goto forward_msg;
		break;
	default:
		if (imsg->hdr.type >= IMSG_RADIUSD_MODULE_MIN) {
 forward_msg:
			if (module_dispatch_control == NULL) {
				const char msg[] =
				    "the module doesn't handle any controls";
				imsg_compose(&base->ibuf, IMSG_NG,
				    imsg->hdr.peerid, 0, -1, msg, sizeof(msg));
			} else
				module_dispatch_control(base->ctx, imsg);
		}
	}

	return (0);
}

void
module_stop(struct module_base *base)
{
	if (module_stop_module != NULL)
		module_stop_module(base->ctx);
#ifdef USE_LIBEVENT
	event_del(&base->ev);
	base->stopped = true;
#endif
	close(base->ibuf.fd);
}

#ifdef USE_LIBEVENT
static void
module_on_event(int fd, short evmask, void *ctx)
{
	struct module_base	*base = ctx;
	int			 ret;

	base->ev_onhandler = true;
	if (evmask & EV_WRITE) {
		base->writeready = true;
		if (imsgbuf_write(&base->ibuf) == -1) {
			syslog(LOG_ERR, "%s: imsgbuf_write: %m", __func__);
			module_stop(base);
			return;
		}
		base->writeready = false;
	}
	if (evmask & EV_READ) {
		ret = module_recv_imsg(base);
		if (ret < 0)
			return;
	}
	base->ev_onhandler = false;
	module_reset_event(base);
	return;
}
#endif

static void
module_reset_event(struct module_base *base)
{
#ifdef USE_LIBEVENT
	short		 evmask = 0;
	struct timeval	*tvp = NULL, tv = { 0, 0 };

	if (base->ev_onhandler)
		return;
	if (base->stopped)
		return;
	event_del(&base->ev);

	evmask |= EV_READ;
	if (imsgbuf_queuelen(&base->ibuf) > 0) {
		if (!base->writeready)
			evmask |= EV_WRITE;
		else
			tvp = &tv;	/* fire immediately */
	}
	event_set(&base->ev, base->ibuf.fd, evmask, module_on_event, base);
	if (event_add(&base->ev, tvp) == -1)
		syslog(LOG_ERR, "event_add() failed in %s()", __func__);
#endif
}

int
module_imsg_compose(struct module_base *base, uint32_t type, uint32_t id,
    pid_t pid, int fd, const void *data, size_t datalen)
{
	int	 ret;

	if ((ret = imsg_compose(&base->ibuf, type, id, pid, fd, data, datalen))
	    != -1)
		module_reset_event(base);

	return (ret);
}

int
module_imsg_composev(struct module_base *base, uint32_t type, uint32_t id,
    pid_t pid, int fd, const struct iovec *iov, int iovcnt)
{
	int	 ret;

	if ((ret = imsg_composev(&base->ibuf, type, id, pid, fd, iov, iovcnt))
	    != -1)
		module_reset_event(base);

	return (ret);
}
