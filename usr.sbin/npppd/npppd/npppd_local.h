/*	$OpenBSD: npppd_local.h,v 1.19 2024/07/11 14:05:59 yasuoka Exp $ */

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
#ifndef	_NPPPD_LOCAL_H
#define	_NPPPD_LOCAL_H	1

#ifndef	NPPPD_BUFSZ
/** buffer size */
#define	NPPPD_BUFSZ			BUFSZ
#endif

#include <net/if.h>

#include "npppd_defs.h"

#include "slist.h"
#include "hash.h"
#include "debugutil.h"

#ifdef	USE_NPPPD_RADIUS
#include "radius_req.h"
#endif

#ifdef	USE_NPPPD_L2TP
#include "bytebuf.h"
#include "l2tp.h"
#endif

#ifdef	USE_NPPPD_PPTP
#include "bytebuf.h"
#include "pptp.h"
#endif
#ifdef	USE_NPPPD_PPPOE
#if defined(__NetBSD__)
#include <net/if_ether.h>
#else
#include <netinet/if_ether.h>
#endif
#include "bytebuf.h"
#include "pppoe.h"
#endif
#include "npppd_auth.h"
#include "npppd.h"
#include "npppd_iface.h"

#include "privsep.h"

#include "addr_range.h"
#include "npppd_pool.h"
#include "npppd_ctl.h"

#ifdef	USE_NPPPD_RADIUS
#include "npppd_radius.h"
#endif

/** structure of pool */
struct _npppd_pool {
	/** base of npppd structure */
	npppd		*npppd;
	/** ipcp name */
	char		ipcp_name[NPPPD_GENERIC_NAME_LEN];
	/** size of sockaddr_npppd array */
	int		addrs_size;
	/** pointer indicated to sockaddr_npppd array */
	struct sockaddr_npppd *addrs;
	/** list of addresses dynamically allocated */
	slist 		dyna_addrs;
	unsigned int	/** whether initialized or not */
			initialized:1,
			/** whether in use or not */
			running:1;
};

/** structure for control socket. (control.c) */
struct control_sock {
	const char      *cs_name;
	struct event     cs_ev;
	struct event     cs_evt;
	int              cs_fd;
	int              cs_restricted;
	void            *cs_ctx;
};

/**
 * npppd
 */
struct _npppd {
	/** event handler */
	struct event ev_sigterm, ev_sigint, ev_sighup, ev_sigchld, ev_timer;

	/** interface which concentrates PPP  */
	npppd_iface		iface[NPPPD_MAX_IFACE];

	npppd_pool		*iface_pool[NPPPD_MAX_IFACE];

	/** address pool */
	npppd_pool		pool[NPPPD_MAX_POOL];

	/** radish pool which uses to manage allocated address */
	struct radish_head *rd;

	/** map of username to slist of npppd_ppp */
	hash_table *map_user_ppp;

	/** authentication realms */
	slist realms;

	/** interval time(in seconds) which finalizes authentication realms */
	int auth_finalizer_itvl;

	/** name of configuration file */
	char 	config_file[PATH_MAX];

	/** name of pid file */
	char 	pidpath[PATH_MAX];

	/** process id */
	pid_t	pid;

	/** boot identifier */
	uint32_t boot_id;

#ifdef	USE_NPPPD_L2TP
	/** structure of L2TP daemon */
	l2tpd l2tpd;
#endif
#ifdef	USE_NPPPD_PPTP
	/** structure of PPTP daemon */
	pptpd pptpd;
#endif
#ifdef	USE_NPPPD_PPPOE
	/** structure of PPPOE daemon */
	pppoed pppoed;
#endif
	/** configuration file  */
	struct npppd_conf conf;

	/** the time in seconds which process was started.*/
	uint32_t	secs;

	/** delay time in seconds reload configuration */
	int16_t		delayed_reload;
	/** counter of reload configuration */
	int16_t		reloading_count;

	int		nsession;

	struct ipcpstat_head ipcpstats;

	struct control_sock  ctl_sock;

#ifdef	USE_NPPPD_RADIUS
	struct npppd_radius_dae_listens	raddae_listens;
#endif

	u_int /** whether finalizing or not */
	    finalizing:1,
	    /** whether finalize completed or not */
	    finalized:1,
	    /** npppd stopped itself because of an error. */
	    stop_by_error:1;
};

#define	ppp_iface(ppp)	(&(ppp)->pppd->iface[(ppp)->ifidx])
#define	ppp_ipcp(ppp)	((ppp)->pppd->iface[(ppp)->ifidx].ipcpconf)
#define	ppp_pool(ppp)	((ppp)->pppd->iface_pool[(ppp)->ifidx])

#define	SIN(sa)		((struct sockaddr_in *)(sa))

#define	TIMER_TICK_RUP(interval)			\
	((((interval) % NPPPD_TIMER_TICK_IVAL) == 0)	\
	    ? (interval)				\
	    : (interval) + NPPPD_TIMER_TICK_IVAL	\
		- ((interval) % NPPPD_TIMER_TICK_IVAL))

#endif
