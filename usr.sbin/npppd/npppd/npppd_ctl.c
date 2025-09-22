/*	$OpenBSD: npppd_ctl.c,v 1.17 2024/11/21 13:18:38 claudio Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <net/pipex.h>

#include <errno.h>
#include <event.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radish.h"
#include "npppd_local.h"
#include "npppd.h"
#include "log.h"

struct stopped_ppp {
	struct npppd_who          ppp_who;
	TAILQ_ENTRY(stopped_ppp)  entry;
};

struct npppd_ctl {
	u_int                     *started_ppp;
	int                        started_ppp_pos;
	int                        started_ppp_siz;
	TAILQ_HEAD(, stopped_ppp)  stopped_ppps;
	npppd                     *npppd;
	bool                       is_monitoring;
	bool                       responding;
};

static int npppd_ctl_who_walk_rd(struct radish *, void *);
static int npppd_ctl_who0 (struct npppd_ctl *, bool);
static void npppd_who_init (struct npppd_who *, npppd_ppp *);
#ifdef USE_NPPPD_PIPEX
static int npppd_ppp_get_pipex_stat(struct npppd_who *_this, npppd_ppp *ppp);
#endif

struct npppd_ctl *
npppd_ctl_create(npppd *_this)
{
	struct npppd_ctl *ctl;

	if ((ctl = calloc(1, sizeof(struct npppd_ctl))) == NULL)
		return (NULL);
	ctl->npppd = _this;
	TAILQ_INIT(&ctl->stopped_ppps);

	return (ctl);
}

void
npppd_ctl_destroy(struct npppd_ctl *_this)
{
	if (_this != NULL) {
		free(_this->started_ppp);
		free(_this);
	}
}

int
npppd_ctl_who(struct npppd_ctl *_this)
{
	return (npppd_ctl_who0(_this, false));
}

int
npppd_ctl_monitor(struct npppd_ctl *_this)
{
	_this->is_monitoring = true;
	return (0);
}

int
npppd_ctl_who_and_monitor(struct npppd_ctl *_this)
{
	return (npppd_ctl_who0(_this, true));
}

static int
npppd_ctl_who0(struct npppd_ctl *_this, bool is_monitoring)
{
	_this->is_monitoring = is_monitoring;
	_this->responding = true;
	if (rd_walktree(_this->npppd->rd, npppd_ctl_who_walk_rd, _this) != 0)
		return (-1);
	return (0);
}

int
npppd_ctl_add_started_ppp_id(struct npppd_ctl *_this, u_int ppp_id)
{
	int    started_ppp_siz;
	u_int *started_ppp;

	if (!_this->is_monitoring && !_this->responding)
		return (-1);
	if (_this->started_ppp_pos + 1 >= _this->started_ppp_siz) {
		started_ppp_siz = _this->started_ppp_siz + 128;
		started_ppp = reallocarray(_this->started_ppp,
		    started_ppp_siz, sizeof(u_int));
		if (started_ppp == NULL)
			return (-1);
		_this->started_ppp = started_ppp;
		_this->started_ppp_siz = started_ppp_siz;
	}
	_this->started_ppp[_this->started_ppp_pos++] = ppp_id;

	/* reset the event */

	return (0);
}

int
npppd_ctl_add_stopped_ppp(struct npppd_ctl *_this, npppd_ppp *ppp)
{
	struct stopped_ppp *stopped;

	if (!_this->is_monitoring)
		return (-1);
	if ((stopped = malloc(sizeof(struct stopped_ppp))) == NULL) {
		log_warn("malloc() failed in %s()", __func__);
		return (-1);
	}
	npppd_who_init(&stopped->ppp_who, ppp);
	TAILQ_INSERT_TAIL(&_this->stopped_ppps, stopped, entry);

	return (0);
}

static int
npppd_ctl_who_walk_rd(struct radish *rd, void *ctx)
{
	struct npppd_ctl *_this = ctx;
	struct sockaddr_npppd *snp;
	npppd_ppp             *ppp;

	snp = rd->rd_rtent;
	if (snp->snp_type == SNP_PPP) {
		ppp = snp->snp_data_ptr;
		if (npppd_ctl_add_started_ppp_id(_this, ppp->id) != 0)
			return (-1);
	}

	return (0);
}

int
npppd_ctl_disconnect(struct npppd_ctl *_this, u_int *ppp_id, int count)
{
	int        i, n;
	npppd_ppp *ppp;

	for (n = 0, i = 0; i < count; i++) {
		if ((ppp = npppd_get_ppp_by_id(_this->npppd, ppp_id[i]))
		    != NULL) {
			ppp_stop(ppp, NULL);
			n++;
		}
	}

	return (n);
}

int
npppd_ctl_imsg_compose(struct npppd_ctl *_this, struct imsgbuf *ibuf)
{
	int                    i, cnt;
	u_char                 pktbuf[MAX_IMSGSIZE - IMSG_HEADER_SIZE];
	struct npppd_who_list *who_list;
	npppd_ppp             *ppp;
	struct stopped_ppp    *e, *t;

	if (imsgbuf_queuelen(ibuf) > 0)
		return (0);

	cnt = 0;
	if (!TAILQ_EMPTY(&_this->stopped_ppps)) {
		who_list = (struct npppd_who_list *)pktbuf;
		who_list->more_data = 0;
		TAILQ_FOREACH_SAFE(e, &_this->stopped_ppps, entry, t) {
			if (offsetof(struct npppd_who_list, entry[cnt + 1])
			    > sizeof(pktbuf)) {
				who_list->more_data = 1;
				break;
			}
			TAILQ_REMOVE(&_this->stopped_ppps, e, entry);
			memcpy(&who_list->entry[cnt], &e->ppp_who,
			    sizeof(who_list->entry[0]));
			cnt++;
			free(e);
		}
		who_list->entry_count = cnt;
		if (imsg_compose(ibuf, IMSG_PPP_STOP, 0, 0, -1, pktbuf,
		    offsetof(struct npppd_who_list, entry[cnt])) == -1)
			return (-1);

		return (0);
	}
	if (_this->responding || _this->started_ppp_pos > 0) {
		who_list = (struct npppd_who_list *)pktbuf;
		who_list->more_data = 0;
		for (cnt = 0, i = 0; i < _this->started_ppp_pos; i++) {
			if (offsetof(struct npppd_who_list, entry[cnt + 1])
			    > sizeof(pktbuf)) {
				who_list->more_data = 1;
				break;
			}
			if ((ppp = npppd_get_ppp_by_id(_this->npppd,
			    _this->started_ppp[i])) == NULL)
				/* may be disconnected */
				continue;
			npppd_who_init(&who_list->entry[cnt], ppp);
			cnt++;
		}
		who_list->entry_count = cnt;
		if (imsg_compose(ibuf, IMSG_PPP_START, 0, 0, -1, pktbuf,
		    offsetof(struct npppd_who_list, entry[cnt])) == -1)
			return (-1);

		if (_this->started_ppp_pos > i)
			memmove(&_this->started_ppp[0],
			    &_this->started_ppp[i],
			    sizeof(u_int) *
				    (_this->started_ppp_pos - i));
		_this->started_ppp_pos -= i;
		if (who_list->more_data == 0)
			_this->responding = false;
		return (0);
	}

	return (0);
}

static void
npppd_who_init(struct npppd_who *_this, npppd_ppp *ppp)
{
	struct timespec  curr_time;
	npppd_auth_base *realm = ppp->realm;
	npppd_iface     *iface = ppp_iface(ppp);

	strlcpy(_this->username, ppp->username, sizeof(_this->username));
	_this->time = ppp->start_time;
	clock_gettime(CLOCK_MONOTONIC, &curr_time);
	_this->duration_sec = curr_time.tv_sec - ppp->start_monotime;
	strlcpy(_this->tunnel_proto, npppd_ppp_tunnel_protocol_name(
	    ppp->pppd, ppp), sizeof(_this->tunnel_proto));

	_this->tunnel_peer.peer_in4.sin_family = AF_UNSPEC;
	if (((struct sockaddr *)&ppp->phy_info)->sa_len > 0) {
		memcpy(&_this->tunnel_peer, &ppp->phy_info,
		    MINIMUM(sizeof(_this->tunnel_peer),
			((struct sockaddr *)&ppp->phy_info)->sa_len));
	}

	strlcpy(_this->ifname, iface->ifname, sizeof(_this->ifname));
	if (realm == NULL)
		_this->rlmname[0] = '\0';
	else
		strlcpy(_this->rlmname, npppd_auth_get_name(realm),
		    sizeof(_this->rlmname));

	_this->framed_ip_address = ppp->acct_framed_ip_address;
	_this->ipackets = ppp->ipackets;
	_this->opackets = ppp->opackets;
	_this->ierrors = ppp->ierrors;
	_this->oerrors = ppp->oerrors;
	_this->ibytes = ppp->ibytes;
	_this->obytes = ppp->obytes;
	_this->ppp_id = ppp->id;
	_this->mru = ppp->peer_mru;

#ifdef USE_NPPPD_PIPEX
	if (ppp->pipex_enabled != 0) {
		if (npppd_ppp_get_pipex_stat(_this, ppp) != 0) {
			log_warn(
			    "npppd_ppp_get_pipex_stat() failed in %s",
			    __func__);
		}
	}
#endif
}

#ifdef USE_NPPPD_PIPEX
static int
npppd_ppp_get_pipex_stat(struct npppd_who *_this, npppd_ppp *ppp)
{
	npppd_iface                   *iface = ppp_iface(ppp);
	struct pipex_session_stat_req  req;
#ifdef USE_NPPPD_PPPOE
	pppoe_session                 *pppoe;
#endif
#ifdef USE_NPPPD_PPTP
	pptp_call                     *pptp;
#endif
#ifdef USE_NPPPD_L2TP
	l2tp_call                     *l2tp;
#endif

	if (ppp->pipex_enabled == 0)
		return 0;

	memset(&req, 0, sizeof(req));
	switch(ppp->tunnel_type) {
#ifdef	USE_NPPPD_PPPOE
	case NPPPD_TUNNEL_PPPOE:
		pppoe = (pppoe_session *)ppp->phy_context;

		/* PPPOE specific information */
		req.psr_protocol = PIPEX_PROTO_PPPOE;
		req.psr_session_id = pppoe->session_id;
		break;
#endif
#ifdef	USE_NPPPD_PPTP
	case NPPPD_TUNNEL_PPTP:
		pptp = (pptp_call *)ppp->phy_context;

		/* PPTP specific information */
		req.psr_session_id = pptp->id;
		req.psr_protocol = PIPEX_PROTO_PPTP;
		break;
#endif
#ifdef USE_NPPPD_L2TP
	case NPPPD_TUNNEL_L2TP:
		l2tp = (l2tp_call *)ppp->phy_context;

		/* L2TP specific information */
		req.psr_session_id = l2tp->session_id;
		req.psr_protocol = PIPEX_PROTO_L2TP;
		break;
#endif
	default:
		errno = EINVAL;
		return 1;
	}

	/* update statistics in kernel */
	if (ioctl(iface->devf, PIPEXGSTAT, &req) != 0)
		return 1;

	_this->ipackets += req.psr_stat.ipackets;
	_this->opackets += req.psr_stat.opackets;
	_this->ierrors += req.psr_stat.ierrors;
	_this->oerrors += req.psr_stat.oerrors;
	_this->ibytes += req.psr_stat.ibytes;
	_this->obytes += req.psr_stat.obytes;

	return 0;
}
#endif
