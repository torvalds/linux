/*	$OpenBSD: npppd.c,v 1.57 2024/11/21 13:18:38 claudio Exp $ */

/*-
 * Copyright (c) 2005-2008,2009 Internet Initiative Japan Inc.
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
/**@file
 * Next pppd(nppd). This file provides a npppd daemon process and operations
 * for npppd instance.
 * @author	Yasuoka Masahiko
 * $Id: npppd.c,v 1.57 2024/11/21 13:18:38 claudio Exp $
 */
#include "version.h"
#include <sys/param.h>	/* ALIGNED_POINTER */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/route.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#include <event.h>
#include <errno.h>
#include <err.h>
#include <pwd.h>

#include "pathnames.h"
#include "debugutil.h"
#include "addr_range.h"
#include "npppd_subr.h"
#include "npppd_local.h"
#include "npppd_auth.h"
#include "radish.h"
#include "net_utils.h"
#include "time_utils.h"

#include "l2tp_local.h"	/* XXX sa_cookie */

#ifdef USE_NPPPD_ARP
#include "npppd_arp.h"
#endif

#ifdef USE_NPPPD_PIPEX
#ifdef USE_NPPPD_PPPOE
#include "pppoe_local.h"
#endif /* USE_NPPPD_PPPOE */
#include "psm-opt.h"
#include <sys/ioctl.h>
#include <net/pipex.h>
#endif /* USE_NPPPD_PIPEX */

#include "accept.h"
#include "log.h"

static npppd s_npppd;	/* singleton */

static void         npppd_reload0 (npppd *);
static void         npppd_update_pool_reference (npppd *);
static int          npppd_rd_walktree_delete(struct radish_head *);
static __dead void  usage (void);
static void         npppd_stop_really (npppd *);
static uint32_t     str_hash(const void *, int);
static void         npppd_on_sighup (int, short, void *);
static void         npppd_on_sigterm (int, short, void *);
static void         npppd_on_sigint (int, short, void *);
static void         npppd_on_sigchld (int, short, void *);
static void         npppd_reset_timer(npppd *);
static void         npppd_timer(int, short, void *);
static void         npppd_auth_finalizer_periodic(npppd *);
static int          rd2slist_walk (struct radish *, void *);
static int          rd2slist (struct radish_head *, slist *);
static int          npppd_get_all_users (npppd *, slist *);
static struct ipcpstat
                   *npppd_get_ipcp_stat(struct ipcpstat_head *, const char *);
static void         npppd_destroy_ipcp_stats(struct ipcpstat_head *);
static void         npppd_ipcp_stats_reload(npppd *);

#ifndef	NO_ROUTE_FOR_POOLED_ADDRESS
static struct in_addr loop;	/* initialize at npppd_init() */
#endif
static uint32_t        str_hash(const void *, int);

#ifdef USE_NPPPD_PIPEX
static void pipex_periodic(npppd *);
#endif /* USE_NPPPD_PIPEX */

#ifdef NPPPD_DEBUG
#define NPPPD_DBG(x) 	log_printf x
#define NPPPD_ASSERT(x) ASSERT(x)
#else
#define NPPPD_DBG(x)
#define NPPPD_ASSERT(x)
#endif

/***********************************************************************
 * Daemon process
 ***********************************************************************/
int        main (int, char *[]);
int        debugsyslog = 0;	/* used by log.c */

int
main(int argc, char *argv[])
{
	int            ch, stop_by_error, runasdaemon = 1, nflag = 0;
	const char    *npppd_conf0 = DEFAULT_NPPPD_CONF;
	struct passwd *pw;

	while ((ch = getopt(argc, argv, "nf:d")) != -1) {
		switch (ch) {
		case 'n':
			nflag = 1;
			break;
		case 'f':
			npppd_conf0 = optarg;
			break;
		case 'd':
			debuglevel++;
			runasdaemon = 0;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0)
		usage();
	if (nflag) {
		debuglevel++;
		runasdaemon = 0;
	}

	/* for log.c */
	log_init(debuglevel);
	if (debuglevel > 0) {
		/* for ../common/debugutil.c */
		debug_set_debugfp(stderr);
		debug_use_syslog(0);
	}
	if (runasdaemon)
		daemon(0, 0);

	/* check for root privileges */
	if (geteuid())
		errx(1, "need root privileges");
	/* check for npppd user */
	if (getpwnam(NPPPD_USER) == NULL)
		errx(1, "unknown user %s", NPPPD_USER);

	if (privsep_init() != 0)
		err(1, "cannot drop privileges");

	if (nflag) {
		if (npppd_config_check(npppd_conf0) == 0) {
			fprintf(stderr, "configuration OK\n");
			exit(EXIT_SUCCESS);
		}
		exit(EXIT_FAILURE);
	}
	if (npppd_init(&s_npppd, npppd_conf0) != 0)
		exit(EXIT_FAILURE);

	if ((pw = getpwnam(NPPPD_USER)) == NULL)
		err(EXIT_FAILURE, "gwpwnam");
	if (chroot(pw->pw_dir) == -1)
		err(EXIT_FAILURE, "chroot");
	if (chdir("/") == -1)
		err(EXIT_FAILURE, "chdir(\"/\")");
        if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		err(EXIT_FAILURE, "cannot drop privileges");
	/* privileges is dropped */

	npppd_start(&s_npppd);
	stop_by_error = s_npppd.stop_by_error;
	npppd_fini(&s_npppd);
	privsep_fini();
	log_printf(LOG_NOTICE, "Terminate npppd.");

	exit((!stop_by_error)? EXIT_SUCCESS : EXIT_FAILURE);
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: npppd [-dn] [-f config_file]\n");
	exit(1);
}

/** Returns the singleton npppd instance */
npppd *
npppd_get_npppd()
{
	return &s_npppd;
}

/***********************************************************************
 * Operations to npppd itself (initialize/finalize/start/stop)
 ***********************************************************************/
 /** Initialize the npppd */
int
npppd_init(npppd *_this, const char *config_file)
{
	int		 i, status = -1, value;
	const char	*pidpath0;
	FILE		*pidfp = NULL;
	struct tunnconf	*tunn;
	struct ipcpconf *ipcpconf;
	struct ipcpstat *ipcpstat;
	int		 mib[] = { CTL_NET, PF_PIPEX, PIPEXCTL_ENABLE };
	size_t		 size;

	memset(_this, 0, sizeof(npppd));
#ifndef	NO_ROUTE_FOR_POOLED_ADDRESS
	loop.s_addr = htonl(INADDR_LOOPBACK);
#endif

	NPPPD_ASSERT(config_file != NULL);

	pidpath0 = NULL;
	_this->pid = getpid();
	slist_init(&_this->realms);
	npppd_conf_init(&_this->conf);
	TAILQ_INIT(&_this->raddae_listens);

	log_printf(LOG_NOTICE, "Starting npppd pid=%u version=%s",
	    _this->pid, VERSION);
#if defined(BUILD_DATE) && defined(BUILD_TIME)
	log_printf(LOG_INFO, "Build %s %s ", BUILD_DATE, BUILD_TIME);
#endif
	if (get_nanotime() == INT64_MIN) {
		log_printf(LOG_ERR, "get_nanotime() failed: %m");
		return 1;
	}

	if (realpath(config_file, _this->config_file) == NULL) {
		log_printf(LOG_ERR, "realpath(%s,) failed in %s(): %m",
		    config_file, __func__);
		return 1;
	}
	/* we assume 4.4 compatible realpath().  See realpath(3) on BSD. */
	NPPPD_ASSERT(_this->config_file[0] == '/');

	_this->boot_id = arc4random();

#ifdef	USE_NPPPD_L2TP
	if (l2tpd_init(&_this->l2tpd) != 0)
		return (-1);
#endif
#ifdef	USE_NPPPD_PPTP
	if (pptpd_init(&_this->pptpd) != 0)
		return (-1);
#endif
#ifdef	USE_NPPPD_PPPOE
	if (pppoed_init(&_this->pppoed) != 0)
		return (-1);
#endif
	LIST_INIT(&_this->ipcpstats);

	/* load configuration */
	if ((status = npppd_reload_config(_this)) != 0)
		return status;

	TAILQ_FOREACH(tunn, &_this->conf.tunnconfs, entry) {
		if (tunn->pipex) {
			size = sizeof(value);
			if (!sysctl(mib, nitems(mib), &value, &size, NULL, 0)
			    && value == 0)
				log_printf(LOG_WARNING,
					"pipex(4) is disabled by sysctl");
			break;
		}
	}

	if ((_this->map_user_ppp = hash_create(
	    (int (*) (const void *, const void *))strcmp, str_hash,
	    NPPPD_USER_HASH_SIZ)) == NULL) {
		log_printf(LOG_ERR, "hash_create() failed in %s(): %m",
		    __func__);
		return -1;
	}

	if (npppd_ifaces_load_config(_this) != 0) {
		return -1;
	}

	TAILQ_FOREACH(ipcpconf, &_this->conf.ipcpconfs, entry) {
		ipcpstat = malloc(sizeof(*ipcpstat));
		if (ipcpstat == NULL) {
			log_printf(LOG_ERR, "initializing ipcp_stats failed : %m");
			npppd_destroy_ipcp_stats(&_this->ipcpstats);
			return -1;
		}
		memset(ipcpstat, 0, sizeof(*ipcpstat));
		strlcpy(ipcpstat->name, ipcpconf->name, sizeof(ipcpstat->name));
		LIST_INSERT_HEAD(&_this->ipcpstats, ipcpstat, entry);
	}

	pidpath0 = DEFAULT_NPPPD_PIDFILE;

	/* initialize event(3) */
	event_init();
	_this->ctl_sock.cs_name = NPPPD_SOCKET;
	_this->ctl_sock.cs_ctx = _this;
	if (control_init(&_this->ctl_sock) == -1) {
		log_printf(LOG_ERR, "control_init() failed %s(): %m",
		    __func__);
		return (-1);
	}
	if (control_listen(&_this->ctl_sock) == -1) {
		log_printf(LOG_ERR, "control_listen() failed %s(): %m",
		    __func__);
		return (-1);
	}
	accept_init();

	/* ignore signals */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGURG, SIG_IGN);

	/* set signal handlers */
	signal_set(&_this->ev_sigterm, SIGTERM, npppd_on_sigterm, _this);
	signal_set(&_this->ev_sigint, SIGINT, npppd_on_sigint, _this);
	signal_set(&_this->ev_sighup, SIGHUP, npppd_on_sighup, _this);
	signal_set(&_this->ev_sigchld, SIGCHLD, npppd_on_sigchld, _this);
	signal_add(&_this->ev_sigterm, NULL);
	signal_add(&_this->ev_sigint, NULL);
	signal_add(&_this->ev_sighup, NULL);
	signal_add(&_this->ev_sigchld, NULL);

	evtimer_set(&_this->ev_timer, npppd_timer, _this);

	/* start tun(4) or pppac(4) */
	status = 0;
	for (i = 0; i < countof(_this->iface); i++) {
		if (_this->iface[i].initialized != 0)
			status |= npppd_iface_start(&_this->iface[i]);
	}
	if (status != 0)
		return -1;

	/*
	 * If the npppd can start(open) interfaces successfully, it can
	 * act as only one npppd process on the system and overwrite the pid
	 * file.
	 */
	if ((pidfp = fopen(pidpath0, "w+")) == NULL) {
		log_printf(LOG_ERR, "fopen(%s,w+) failed in %s(): %m",
		    pidpath0, __func__);
		return -1;
	}
	strlcpy(_this->pidpath, pidpath0, sizeof(_this->pidpath));
	fprintf(pidfp, "%u\n", _this->pid);
	fclose(pidfp);
	pidfp = NULL;
#ifdef USE_NPPPD_ARP
	arp_set_strictintfnetwork(npppd_config_str_equali(_this, "arpd.strictintfnetwork", "true", ARPD_STRICTINTFNETWORK_DEFAULT));
	if (npppd_config_str_equali(_this, "arpd.enabled", "true", ARPD_DEFAULT) == 1)
        	arp_sock_init();
#endif
	if ((status = npppd_modules_reload(_this)) != 0)
		return status;

	npppd_update_pool_reference(_this);

	return 0;
}

/** start the npppd */
void
npppd_start(npppd *_this)
{
	int rval = 0;

	npppd_reset_timer(_this);
	while ((rval = event_loop(EVLOOP_ONCE)) == 0) {
		if (_this->finalized != 0)
			break;
	}
	if (rval != 0) {
		log_printf(LOG_CRIT, "event_loop() failed: %m");
		abort();
	}
}

/** stop the npppd */
void
npppd_stop(npppd *_this)
{
	int i;
#ifdef	USE_NPPPD_L2TP
	l2tpd_stop(&_this->l2tpd);
#endif
#ifdef	USE_NPPPD_PPTP
	pptpd_stop(&_this->pptpd);
#endif
#ifdef	USE_NPPPD_PPPOE
	pppoed_stop(&_this->pppoed);
#endif
#ifdef USE_NPPPD_ARP
        arp_sock_fini();
#endif
	close(_this->ctl_sock.cs_fd);
	control_cleanup(&_this->ctl_sock);

	for (i = countof(_this->iface) - 1; i >= 0; i--) {
		if (_this->iface[i].initialized != 0)
			npppd_iface_stop(&_this->iface[i]);
	}
	npppd_set_radish(_this, NULL);

	_this->finalizing = 1;
	npppd_reset_timer(_this);

#ifdef USE_NPPPD_RADIUS
	npppd_radius_dae_fini(_this);
#endif
}

static void
npppd_stop_really(npppd *_this)
{
	int i;
#if defined(USE_NPPPD_L2TP) || defined(USE_NPPPD_PPTP)
	int wait_again;

	wait_again = 0;

#ifdef	USE_NPPPD_L2TP
	if (!l2tpd_is_stopped(&_this->l2tpd))
		wait_again |= 1;
#endif
#ifdef	USE_NPPPD_PPTP
	if (!pptpd_is_stopped(&_this->pptpd))
		wait_again |= 1;
#endif
	if (wait_again != 0) {
		npppd_reset_timer(_this);
		return;
	}
#endif
	for (i = countof(_this->iface) - 1; i >= 0; i--) {
		npppd_iface_fini(&_this->iface[i]);
	}
	_this->finalized = 1;
}

/** finalize the npppd */
void
npppd_fini(npppd *_this)
{
	int i;
	npppd_auth_base *auth_base;

#ifdef USE_NPPPD_L2TP
	l2tpd_uninit(&_this->l2tpd);
#endif
#ifdef USE_NPPPD_PPTP
	pptpd_uninit(&_this->pptpd);
#endif
#ifdef USE_NPPPD_PPPOE
	pppoed_uninit(&_this->pppoed);
#endif
	for (slist_itr_first(&_this->realms);
	    slist_itr_has_next(&_this->realms);) {
		auth_base = slist_itr_next(&_this->realms);
		npppd_auth_destroy(auth_base);
	}
	for (i = countof(_this->iface) - 1; i >= 0; i--) {
		if (_this->iface[i].initialized != 0)
			npppd_iface_fini(&_this->iface[i]);
	}

	for (i = countof(_this->pool) - 1; i >= 0; i--) {
		if (_this->pool[i].initialized != 0)
			npppd_pool_uninit(&_this->pool[i]);
	}

	npppd_destroy_ipcp_stats(&_this->ipcpstats);

	signal_del(&_this->ev_sigterm);
	signal_del(&_this->ev_sigint);
	signal_del(&_this->ev_sighup);
	signal_del(&_this->ev_sigchld);

	npppd_conf_fini(&_this->conf);

	slist_fini(&_this->realms);

	if (_this->map_user_ppp != NULL)
		hash_free(_this->map_user_ppp);
}

/***********************************************************************
 * Timer related functions
 ***********************************************************************/
static void
npppd_reset_timer(npppd *_this)
{
	struct timeval tv;

	if (_this->finalizing != 0) {
		/* we can use the timer exclusively on finalizing */
		tv.tv_usec = 500000;
		tv.tv_sec = 0;
		evtimer_add(&_this->ev_timer, &tv);
	} else {
		tv.tv_usec = 0;
		tv.tv_sec = NPPPD_TIMER_TICK_IVAL;
		evtimer_add(&_this->ev_timer, &tv);
	}
}

static void
npppd_timer(int fd, short evtype, void *ctx)
{
	npppd *_this;

	_this = ctx;
	if (_this->finalizing != 0) {
		npppd_stop_really(_this); /* The timer has been reset */
		return;	/* we can use the timer exclusively on finalizing */
	}
	_this->secs += NPPPD_TIMER_TICK_IVAL;
	if (_this->reloading_count > 0) {
		_this->reloading_count -= NPPPD_TIMER_TICK_IVAL;
		if (_this->reloading_count <= 0) {
			npppd_reload0(_this);
			_this->reloading_count = 0;
		}
	} else {
		if ((_this->secs % TIMER_TICK_RUP(
			    NPPPD_AUTH_REALM_FINALIZER_INTERVAL)) == 0)
			npppd_auth_finalizer_periodic(_this);
	}

#ifdef USE_NPPPD_PPPOE
	if (pppoed_need_polling(&_this->pppoed))
		pppoed_reload_listeners(&_this->pppoed);
#endif
#ifdef USE_NPPPD_PIPEX
	pipex_periodic(_this);
#endif

	npppd_reset_timer(_this);
}

int
npppd_reset_routing_table(npppd *_this, int pool_only)
{
#ifndef	NO_ROUTE_FOR_POOLED_ADDRESS
	slist rtlist0;

	if (_this->iface[0].using_pppx) 
		return 0;

	slist_init(&rtlist0);
	if (rd2slist(_this->rd, &rtlist0) != 0)
		return 1;

	for (slist_itr_first(&rtlist0); slist_itr_has_next(&rtlist0); ) {
		struct radish *rd;
		struct sockaddr_npppd *snp;
		npppd_ppp *ppp;
		int is_first;

		rd = slist_itr_next(&rtlist0);
		snp = rd->rd_rtent;

		is_first = 1;
		for (snp = rd->rd_rtent; snp != NULL; snp = snp->snp_next) {
			switch (snp->snp_type) {
			case SNP_POOL:
			case SNP_DYN_POOL:
				if (is_first)
					in_route_add(&snp->snp_addr,
					    &snp->snp_mask, &loop,
					    LOOPBACK_IFNAME, RTF_BLACKHOLE, 0);
				break;

			case SNP_PPP:
				if (pool_only)
					break;
				ppp = snp->snp_data_ptr;
				if (ppp->ppp_framed_ip_netmask.s_addr
				    == 0xffffffffL) {
					in_host_route_add(&ppp->
					    ppp_framed_ip_address,
					    &ppp_iface(ppp)->ip4addr,
					    ppp_iface(ppp)->ifname,
					    MRU_IPMTU(ppp->peer_mru));
				} else {
					in_route_add(&ppp->
					    ppp_framed_ip_address,
					    &ppp->ppp_framed_ip_netmask,
					    &ppp_iface(ppp)->ip4addr,
					    ppp_iface(ppp)->ifname, 0,
					    MRU_IPMTU(ppp->peer_mru));
				}
				break;
			}
			is_first = 0;
		}

	}

	slist_fini(&rtlist0);
#endif
	return 0;
}

/***********************************************************************
 * Other npppd related functions.
 ***********************************************************************/
/**
 * Get the user's password.  Return 0 on success.
 *
 * @param	username    Username who acquires password
 * @param	password    A pointer to a buffer space to store the password.
 *			    Use NULL when you need to know only the length of
 *			    the password.
 * @param	plpassword  A pointer to the length of the password parameter.
 *			    This function uses this parameter value and stores
 *			    the required length value pointed to by this
 *			    parameter.  Use NULL when use don't need to know
 *			    the password and its length.
 * @return	If the function succeeds, 0 is returned.  The function returns
 *		1 if the username is unknown, returns 2 if the password buffer
 *		length is not enough.  It returns negative value for other
 *		errors.
 */
int
npppd_get_user_password(npppd *_this, npppd_ppp *ppp,
    const char *username, char *password, int *plpassword)
{
	char buf0[MAX_USERNAME_LENGTH];

	NPPPD_ASSERT(ppp->realm != NULL);
	return npppd_auth_get_user_password(ppp->realm,
	    npppd_auth_username_for_auth(ppp->realm, username, buf0), password,
	    plpassword);
}

/** Get the Framed-IP-Address attribute of the user */
struct in_addr *
npppd_get_user_framed_ip_address(npppd *_this, npppd_ppp *ppp,
    const char *username)
{

	if (ppp->peer_auth == 0) {
		ppp->realm_framed_ip_address.s_addr = 0;
		goto do_default;
	}
	NPPPD_ASSERT(ppp->realm != NULL);

	if (ppp->realm_framed_ip_address.s_addr != 0)
		return &ppp->realm_framed_ip_address;

	/* assign by the authentication realm */
	if (npppd_auth_get_framed_ip(ppp->realm, username,
	    &ppp->realm_framed_ip_address,
		    &ppp->realm_framed_ip_netmask) != 0)
		ppp->realm_framed_ip_address.s_addr = 0;

do_default:
	/* Use USER_SELECT if the realm doesn't specify the ip address */
	if (ppp->realm_framed_ip_address.s_addr == 0)
		ppp->realm_framed_ip_address.s_addr = INADDR_USER_SELECT;


	if (ppp->realm_framed_ip_address.s_addr == INADDR_USER_SELECT) {
		/* Use NAS_SELECT if USER_SELECT is not allowed by the config */
		if (!ppp_ipcp(ppp)->allow_user_select)
			ppp->realm_framed_ip_address.s_addr = INADDR_NAS_SELECT;
	}
	NPPPD_DBG((LOG_DEBUG, "%s() = %s", __func__,
	    inet_ntoa(ppp->realm_framed_ip_address)));

	return &ppp->realm_framed_ip_address;
}

/** XXX */
int
npppd_check_calling_number(npppd *_this, npppd_ppp *ppp)
{
	struct tunnconf *conf;
	int              lnumber, rval;
	char             number[NPPPD_PHONE_NUMBER_LEN + 1];

	conf = ppp_get_tunnconf(ppp);
	if (conf->callnum_check != 0) {
		lnumber = sizeof(number);
		if ((rval = npppd_auth_get_calling_number(ppp->realm,
		    ppp->username, number, &lnumber)) == 0)
			return
			    (strcmp(number, ppp->calling_number) == 0)? 1 : 0;
		if ((conf->callnum_check & NPPPD_CALLNUM_CHECK_STRICT) != 0)
			return 0;
	}

	return 1;
}

/**
 * This function finds a {@link npppd_ppp} instance that is assigned the
 * specified ip address and returns it
 * @param ipaddr	IP Address(Specify in network byte order)
 */
npppd_ppp *
npppd_get_ppp_by_ip(npppd *_this, struct in_addr ipaddr)
{
	struct sockaddr_npppd *snp;
	struct radish *rdp;
	struct sockaddr_in npppd_get_ppp_by_ip_sin4;

	npppd_get_ppp_by_ip_sin4.sin_family = AF_INET;
	npppd_get_ppp_by_ip_sin4.sin_len = sizeof(struct sockaddr_in);
	npppd_get_ppp_by_ip_sin4.sin_addr = ipaddr;
	if (_this->rd == NULL)
		return NULL;	/* no radix tree on startup */
	if (rd_match((struct sockaddr *)&npppd_get_ppp_by_ip_sin4, _this->rd,
	    &rdp)) {
		snp = rdp->rd_rtent;
		if (snp->snp_type == SNP_PPP)
			return snp->snp_data_ptr;
	}
	return NULL;
}

/**
 * This function finds {@link npppd_ppp} instances that are authenticated
 * as the specified username and returns them as a {@link slist} list.
 * @param username	PPP Username.
 * @return	{@link slist} that contains the {@link npppd_ppp} instances.
 * NULL may be returned if no instance has been found.
 */
slist *
npppd_get_ppp_by_user(npppd *_this, const char *username)
{
	hash_link *hl;

	if ((hl = hash_lookup(_this->map_user_ppp, username)) != NULL)
		return hl->item;

	return NULL;
}

/**
 * This function finds a {@link npppd_ppp} instance that matches the specified
 * ppp id and returns it.
 * @param	id	{@link npppd_ppp#id ppp's id}
 * @return	This function returns the pointer if the instance which has
 *		specified ID is found, otherwise it returns NULL.
 */
npppd_ppp *
npppd_get_ppp_by_id(npppd *_this, u_int ppp_id)
{
	slist users;
	npppd_ppp *ppp0, *ppp;

	NPPPD_ASSERT(_this != NULL);

	ppp = NULL;
	slist_init(&users);
	if (npppd_get_all_users(_this, &users) != 0) {
		log_printf(LOG_WARNING,
		    "npppd_get_all_users() failed in %s()", __func__);
	} else {
		/* FIXME: This linear search eats CPU. */
		for (slist_itr_first(&users); slist_itr_has_next(&users); ) {
			ppp0 = slist_itr_next(&users);
			if (ppp0->id == ppp_id) {
				ppp = ppp0;
				break;
			}
		}
	}
	slist_fini(&users);

	return ppp;
}

static struct ipcpstat *
npppd_get_ipcp_stat(struct ipcpstat_head *head , const char *ipcp_name)
{
	struct ipcpstat *ipcpstat = NULL;

	LIST_FOREACH(ipcpstat, head, entry) {
		if (strncmp(ipcpstat->name, ipcp_name,
		    sizeof(ipcpstat->name)) == 0)
			return ipcpstat;
	}

	return NULL;
}

static void
npppd_destroy_ipcp_stats(struct ipcpstat_head *head)
{
	struct ipcpstat	*ipcpstat, *tipcpstat;
	npppd_ppp	*ppp, *tppp;

	LIST_FOREACH_SAFE(ipcpstat, head, entry, tipcpstat) {
		LIST_FOREACH_SAFE(ppp, &ipcpstat->ppp, ipcpstat_entry, tppp) {
			ppp->ipcpstat = NULL;
			LIST_REMOVE(ppp, ipcpstat_entry);
		}
		free(ipcpstat);
	}
}

static void
npppd_ipcp_stats_reload(npppd *_this)
{
	struct ipcpstat		*ipcpstat, *tipcpstat;
	struct ipcpconf		*ipcpconf;
	struct ipcpstat_head	 destroy_list;

	LIST_INIT(&destroy_list);
	LIST_FOREACH_SAFE(ipcpstat, &_this->ipcpstats, entry, tipcpstat) {
		LIST_REMOVE(ipcpstat, entry);
		LIST_INSERT_HEAD(&destroy_list, ipcpstat, entry);
	}

	TAILQ_FOREACH(ipcpconf, &_this->conf.ipcpconfs, entry) {
		ipcpstat = npppd_get_ipcp_stat(&destroy_list, ipcpconf->name);
		if (ipcpstat != NULL) {
			LIST_REMOVE(ipcpstat, entry);
			LIST_INSERT_HEAD(&_this->ipcpstats, ipcpstat, entry);
			continue;
		}

		ipcpstat = malloc(sizeof(*ipcpstat));
		if (ipcpstat == NULL) {
			log_printf(LOG_ERR, "initializing ipcp_stats failed : %m");
			continue;
		}
		memset(ipcpstat, 0, sizeof(*ipcpstat));
		strlcpy(ipcpstat->name, ipcpconf->name, sizeof(ipcpconf->name));
		LIST_INSERT_HEAD(&_this->ipcpstats, ipcpstat, entry);
	}
	npppd_destroy_ipcp_stats(&destroy_list);
}

/**
 * Checks whether the user reaches the maximum session limit
 * (user_max_serssion).
 * @return	This function returns 1(true) if the user does not reach the
 *		limit, otherwise it returns 0(false).
 */
int
npppd_check_user_max_session(npppd *_this, npppd_ppp *ppp)
{
	int global_count, realm_count;
	npppd_ppp *ppp1;
	slist *uppp;

	/* user_max_session == 0 means unlimit */
	if (_this->conf.user_max_session == 0 &&
	    npppd_auth_user_session_unlimited(ppp->realm))
		return 1;

	global_count = realm_count = 0;
	if ((uppp = npppd_get_ppp_by_user(_this, ppp->username)) != NULL) {
		for (slist_itr_first(uppp); slist_itr_has_next(uppp); ) {
			ppp1 = slist_itr_next(uppp);
			if (ppp->realm == ppp1->realm)
				realm_count++;
			global_count++;
		}
	}

	if (npppd_check_auth_user_max_session(ppp->realm, realm_count)) {
		ppp_log(ppp, LOG_WARNING,
		    "user %s exceeds user-max-session limit per auth",
		    ppp->username);
		return 0;
	} else if (_this->conf.user_max_session != 0 &&
	    _this->conf.user_max_session <= global_count) {
		ppp_log(ppp, LOG_WARNING,
		    "user %s exceeds user-max-session limit", ppp->username);
		return 0;
	} else
		return 1;
}

/***********************************************************************
 * Network I/O ralated functions.
 ***********************************************************************/
/**
 * Call this function to output packets to the network(tun).  This function
 * currently assumes the packet is a IPv4 datagram.
 */
void
npppd_network_output(npppd *_this, npppd_ppp *ppp, int proto, u_char *pktp,
    int lpktp)
{
	struct ip *pip;
	int lbuf;
	u_char buf[256];	/* enough size for TCP/IP header */

	NPPPD_ASSERT(ppp != NULL);

	if (!ppp_ip_assigned(ppp))
		return;

	if (lpktp < sizeof(struct ip)) {
		ppp_log(ppp, LOG_DEBUG, "Received IP packet is too small");
		return;
	}
	lbuf = MINIMUM(lpktp, sizeof(buf));
	if (!ALIGNED_POINTER(pktp, struct ip)) {
		memcpy(buf, pktp, lbuf);
		pip = (struct ip *)buf;
	} else {
		pip = (struct ip *)pktp;
	}

	if (ppp->ingress_filter != 0 &&
	    (pip->ip_src.s_addr & ppp->ppp_framed_ip_netmask.s_addr)
		    != (ppp->ppp_framed_ip_address.s_addr &
			ppp->ppp_framed_ip_netmask.s_addr)) {
		char logbuf[80];
		strlcpy(logbuf, inet_ntoa(pip->ip_dst), sizeof(logbuf));
		ppp_log(ppp, LOG_INFO,
		    "Drop packet by ingress filter.  %s => %s",
		    inet_ntoa(pip->ip_src), logbuf);

		return;
	}
	if (ppp->timeout_sec > 0 && !ip_is_idle_packet(pip, lbuf))
		ppp_reset_idle_timeout(ppp);

#ifndef NO_ADJUST_MSS
	if (ppp->adjust_mss) {
		if (lpktp == lbuf) {
			/*
			 * We can assume the packet length is less than
			 * sizeof(buf).
			 */
			if (!ALIGNED_POINTER(pktp, struct ip))
				pktp = buf;
			adjust_tcp_mss(pktp, lpktp, MRU_IPMTU(ppp->peer_mru));
		}
	}
#endif
	npppd_iface_write(ppp_iface(ppp), ppp, proto, pktp, lpktp);
}

#ifdef USE_NPPPD_PIPEX
/***********************************************************************
 * PIPEX related functions
 ***********************************************************************/
static void
pipex_setup_common(npppd_ppp *ppp, struct pipex_session_req *req)
{
	memset(req, 0, sizeof(*req));
	if (psm_opt_is_accepted(&ppp->lcp, acfc))
		req->pr_ppp_flags |= PIPEX_PPP_ACFC_ENABLED;
	if (psm_peer_opt_is_accepted(&ppp->lcp, acfc))
		req->pr_ppp_flags |= PIPEX_PPP_ACFC_ACCEPTED;

	if (psm_peer_opt_is_accepted(&ppp->lcp, pfc))
		req->pr_ppp_flags |= PIPEX_PPP_PFC_ACCEPTED;
	if (psm_opt_is_accepted(&ppp->lcp, pfc))
		req->pr_ppp_flags |= PIPEX_PPP_PFC_ENABLED;

	if (ppp->has_acf != 0)
		req->pr_ppp_flags |= PIPEX_PPP_HAS_ACF;

	if (ppp->adjust_mss != 0)
		req->pr_ppp_flags |= PIPEX_PPP_ADJUST_TCPMSS;
	if (ppp->ingress_filter != 0)
		req->pr_ppp_flags |= PIPEX_PPP_INGRESS_FILTER;

	req->pr_ip_srcaddr = ppp->pppd->iface[0].ip4addr;
	req->pr_ip_address = ppp->ppp_framed_ip_address;
	req->pr_ip_netmask = ppp->ppp_framed_ip_netmask;
	req->pr_peer_mru = ppp->peer_mru;
	req->pr_ppp_id = ppp->id;

	req->pr_timeout_sec = ppp->timeout_sec;

#ifdef USE_NPPPD_MPPE
	req->pr_ccp_id = ppp->ccp.fsm.id;
	if (ppp->mppe.send.keybits > 0) {
		memcpy(req->pr_mppe_send.master_key,
		    ppp->mppe.send.master_key,
		    sizeof(req->pr_mppe_send.master_key));
		req->pr_mppe_send.stateless = ppp->mppe.send.stateless;
		req->pr_mppe_send.keylenbits = ppp->mppe.send.keybits;
		req->pr_ppp_flags |= PIPEX_PPP_MPPE_ENABLED;
	}
	if (ppp->mppe.recv.keybits > 0) {
		memcpy(req->pr_mppe_recv.master_key,
		    ppp->mppe.recv.master_key,
		    sizeof(req->pr_mppe_recv.master_key));
		req->pr_mppe_recv.stateless = ppp->mppe.recv.stateless;
		req->pr_mppe_recv.keylenbits = ppp->mppe.recv.keybits;
		req->pr_ppp_flags |= PIPEX_PPP_MPPE_ACCEPTED;
	}
	if (ppp->mppe.required)
		req->pr_ppp_flags |= PIPEX_PPP_MPPE_REQUIRED;
#endif /* USE_NPPPD_MPPE */
}

/** Enable PIPEX of the {@link npppd_ppp ppp} */
int
npppd_ppp_pipex_enable(npppd *_this, npppd_ppp *ppp)
{
	struct pipex_session_req req;
#ifdef	USE_NPPPD_PPPOE
	pppoe_session *pppoe;
#endif
#ifdef	USE_NPPPD_PPTP
	pptp_call *call;
#endif
#ifdef	USE_NPPPD_L2TP
	l2tp_call *l2tp;
	l2tp_ctrl *l2tpctrl;
#endif
	int error;

	NPPPD_ASSERT(ppp != NULL);
	NPPPD_ASSERT(ppp->phy_context != NULL);
	NPPPD_ASSERT(ppp->use_pipex != 0);

	pipex_setup_common(ppp, &req);

	switch (ppp->tunnel_type) {
#ifdef USE_NPPPD_PPPOE
	case NPPPD_TUNNEL_PPPOE:
	    {
		struct sockaddr *sa;
		struct ether_header *eh;
		pppoe = (pppoe_session *)ppp->phy_context;

		/* PPPoE specific information */
		req.pr_protocol = PIPEX_PROTO_PPPOE;
		req.pr_session_id = pppoe->session_id;
		req.pr_peer_session_id = 0;
		strlcpy(req.pr_proto.pppoe.over_ifname,
		    pppoe_session_listen_ifname(pppoe),
		    sizeof(req.pr_proto.pppoe.over_ifname));

		sa = (struct sockaddr *)&req.pr_peer_address;
		sa->sa_family = AF_UNSPEC;
		sa->sa_len = sizeof(struct sockaddr);

		eh = (struct ether_header *)sa->sa_data;
		eh->ether_type = htons(ETHERTYPE_PPPOE);
		memcpy(eh->ether_dhost, pppoe->ether_addr, ETHER_ADDR_LEN);
		memset(eh->ether_shost, 0, ETHER_ADDR_LEN);

		break;
	    }
#endif
#ifdef USE_NPPPD_PPTP
	case NPPPD_TUNNEL_PPTP:
		call = (pptp_call *)ppp->phy_context;

		/* PPTP specific information */
		req.pr_session_id = call->id;
		req.pr_protocol = PIPEX_PROTO_PPTP;

		req.pr_peer_session_id = call->peers_call_id;
		req.pr_proto.pptp.snd_nxt = call->snd_nxt;
		req.pr_proto.pptp.snd_una = call->snd_una;
		req.pr_proto.pptp.rcv_nxt = call->rcv_nxt;
		req.pr_proto.pptp.rcv_acked = call->rcv_acked;
		req.pr_proto.pptp.winsz = call->winsz;
		req.pr_proto.pptp.maxwinsz = call->maxwinsz;
		req.pr_proto.pptp.peer_maxwinsz = call->peers_maxwinsz;

		NPPPD_ASSERT(call->ctrl->peer.ss_family == AF_INET);
		NPPPD_ASSERT(call->ctrl->our.ss_family == AF_INET);

		memcpy(&req.pr_peer_address, &call->ctrl->peer,
		    call->ctrl->peer.ss_len);
		memcpy(&req.pr_local_address, &call->ctrl->our,
		    call->ctrl->our.ss_len);
		break;
#endif
#ifdef USE_NPPPD_L2TP
	case NPPPD_TUNNEL_L2TP:
		l2tp = (l2tp_call *)ppp->phy_context;
		l2tpctrl = l2tp->ctrl;

		/* L2TPv2 specific context */
		/* Session KEYS */
		req.pr_protocol = PIPEX_PROTO_L2TP;
		req.pr_proto.l2tp.tunnel_id = l2tpctrl->tunnel_id;
		req.pr_proto.l2tp.peer_tunnel_id = l2tpctrl->peer_tunnel_id;
		req.pr_session_id = l2tp->session_id;
		req.pr_peer_session_id = l2tp->peer_session_id;

		if (l2tpctrl->data_use_seq)
			req.pr_proto.l2tp.option_flags |=
			    PIPEX_L2TP_USE_SEQUENCING;

		/* transmission control contexts */
		req.pr_proto.l2tp.ns_nxt = l2tp->snd_nxt;
		req.pr_proto.l2tp.nr_nxt = l2tp->rcv_nxt;

		memcpy(&req.pr_peer_address, &l2tpctrl->peer,
		    l2tpctrl->peer.ss_len);
		memcpy(&req.pr_local_address, &l2tpctrl->sock,
		    l2tpctrl->sock.ss_len);
#ifdef USE_SA_COOKIE
		if (l2tpctrl->sa_cookie != NULL) {
			req.pr_proto.l2tp.ipsecflowinfo =
			    ((struct in_ipsec_sa_cookie *)l2tpctrl->sa_cookie)
				    ->ipsecflow;
		}
#endif
		break;
#endif
	default:
		return 1;
	}

	if ((error = ioctl(_this->iface[ppp->ifidx].devf, PIPEXASESSION, &req))
	    != 0) {
		if (errno == ENXIO)	/* pipex is disabled on runtime */
			error = 0;
		ppp->pipex_enabled = 0;
		return error;
	}

	if (_this->iface[ppp->ifidx].using_pppx) {
		struct pipex_session_descr_req descr_req;

		descr_req.pdr_protocol = req.pr_protocol;
		descr_req.pdr_session_id = req.pr_session_id;
		memset(descr_req.pdr_descr, 0, sizeof(descr_req.pdr_descr));
		strlcpy(descr_req.pdr_descr, ppp->username, sizeof(descr_req.pdr_descr));
		error = ioctl(_this->iface[ppp->ifidx].devf, PIPEXSIFDESCR, &descr_req);
		if (error != 0) {
			log_printf(LOG_WARNING, "PIPEXSIFDESCR(%s) failed: %d\n", ppp->username, error);
		}
	}

	ppp->pipex_enabled = 1;
	if (ppp->timeout_sec > 0) {
		/* Stop the npppd's idle-timer.  We use pipex's idle-timer */
		ppp->timeout_sec = 0;
		ppp_reset_idle_timeout(ppp);
	}

	return error;
}

/** Disable PIPEX of the {@link npppd_ppp ppp} */
int
npppd_ppp_pipex_disable(npppd *_this, npppd_ppp *ppp)
{
	struct pipex_session_close_req req;
#ifdef USE_NPPPD_PPPOE
	pppoe_session *pppoe;
#endif
#ifdef USE_NPPPD_PPTP
	pptp_call *call;
#endif
#ifdef USE_NPPPD_L2TP
	l2tp_call *l2tp;
#endif
	int error;

	if (ppp->pipex_started == 0)
		return 0;	/* not started */

	bzero(&req, sizeof(req));
	switch(ppp->tunnel_type) {
#ifdef USE_NPPPD_PPPOE
	case NPPPD_TUNNEL_PPPOE:
		pppoe = (pppoe_session *)ppp->phy_context;

		/* PPPoE specific information */
		req.pcr_protocol = PIPEX_PROTO_PPPOE;
		req.pcr_session_id = pppoe->session_id;
		break;
#endif
#ifdef USE_NPPPD_PPTP
	case NPPPD_TUNNEL_PPTP:
		call = (pptp_call *)ppp->phy_context;

		/* PPTP specific information */
		req.pcr_session_id = call->id;
		req.pcr_protocol = PIPEX_PROTO_PPTP;
		break;
#endif
#ifdef USE_NPPPD_L2TP
	case NPPPD_TUNNEL_L2TP:
		l2tp = (l2tp_call *)ppp->phy_context;

		/* L2TP specific context */
		req.pcr_session_id = l2tp->session_id;
		req.pcr_protocol = PIPEX_PROTO_L2TP;
		break;
#endif
	default:
		return 1;
	}

	error = ioctl(_this->iface[ppp->ifidx].devf, PIPEXDSESSION, &req);
	if (error == 0) {
		ppp->ipackets += req.pcr_stat.ipackets;
		ppp->opackets += req.pcr_stat.opackets;
		ppp->ierrors += req.pcr_stat.ierrors;
		ppp->oerrors += req.pcr_stat.oerrors;
		ppp->ibytes += req.pcr_stat.ibytes;
		ppp->obytes += req.pcr_stat.obytes;
		ppp->pipex_enabled = 0;
	}

	return error;
}

static void
pipex_periodic(npppd *_this)
{
	struct pipex_session_list_req  req;
	npppd_ppp                     *ppp;
	int                            i, devf, error;
	u_int                          ppp_id;
	slist                          dlist, users;

	slist_init(&dlist);
	slist_init(&users);

	devf = -1;
	for (i = 0; i < nitems(_this->iface); i++) {
		if (_this->iface[i].initialized != 0) {
			devf = _this->iface[i].devf;
			break;
		}
	}
	if (devf >= 0) {
		do {
			error = ioctl(devf, PIPEXGCLOSED, &req);
			if (error) {
				if (errno != ENXIO)
					log_printf(LOG_WARNING,
					    "PIPEXGCLOSED failed: %m");
				break;
			}
			for (i = 0; i < req.plr_ppp_id_count; i++) {
				ppp_id = req.plr_ppp_id[i];
				slist_add(&dlist, (void *)(uintptr_t)ppp_id);
			}
		} while (req.plr_flags & PIPEX_LISTREQ_MORE);
	}

	if (slist_length(&dlist) <= 0)
		goto pipex_done;
	if (npppd_get_all_users(_this, &users) != 0) {
		log_printf(LOG_WARNING,
		    "npppd_get_all_users() failed in %s()", __func__);
		slist_fini(&users);
		goto pipex_done;
	}

	/* Disconnect request */
	slist_itr_first(&dlist);
	while (slist_itr_has_next(&dlist)) {
		/* FIXME: Linear search by PPP Id eats CPU */
		ppp_id = (uintptr_t)slist_itr_next(&dlist);
		slist_itr_first(&users);
		ppp = NULL;
		while (slist_itr_has_next(&users)) {
			ppp =  slist_itr_next(&users);
			if (ppp_id == ppp->id) {
				/* found */
				slist_itr_remove(&users);
				break;
			}
			ppp = NULL;
		}
		if (ppp == NULL) {
			log_printf(LOG_WARNING,
			    "kernel requested a ppp down, but it's not found.  "
			    "ppp=%d", ppp_id);
			continue;
		}
		ppp_log(ppp, LOG_INFO, "Stop requested by the kernel");
		/* TODO: PIPEX doesn't return the disconnect reason */
#ifdef USE_NPPPD_RADIUS
		ppp_set_radius_terminate_cause(ppp,
		    RADIUS_TERMNATE_CAUSE_IDLE_TIMEOUT);
#endif
		ppp_stop(ppp, NULL);
	}
pipex_done:
	slist_fini(&users);
	slist_fini(&dlist);
}
#endif /* USE_NPPPD_PIPEX */

/***********************************************************************
 * IP address assignment related functions
 ***********************************************************************/
/** Prepare to use IP */
int
npppd_prepare_ip(npppd *_this, npppd_ppp *ppp)
{

	if (ppp_ipcp(ppp) == NULL)
		return 1;

	npppd_get_user_framed_ip_address(_this, ppp, ppp->username);

	if (npppd_iface_ip_is_ready(ppp_iface(ppp)))
		ppp->ipcp.ip4_our = ppp_iface(ppp)->ip4addr;
	else if (npppd_iface_ip_is_ready(&_this->iface[0]))
		ppp->ipcp.ip4_our = _this->iface[0].ip4addr;
	else
		return -1;
	ppp->ipcp.dns_pri = ppp_ipcp(ppp)->dns_servers[0];
	ppp->ipcp.dns_sec = ppp_ipcp(ppp)->dns_servers[1];
	ppp->ipcp.nbns_pri = ppp_ipcp(ppp)->nbns_servers[0];
	ppp->ipcp.nbns_sec = ppp_ipcp(ppp)->nbns_servers[1];

	return 0;
}

/** Notify stop using IP to npppd and release the resources. */
void
npppd_release_ip(npppd *_this, npppd_ppp *ppp)
{

	if (!ppp_ip_assigned(ppp))
		return;

	npppd_set_ip_enabled(_this, ppp, 0);
	npppd_pool_release_ip(ppp->assigned_pool, ppp);
	ppp->assigned_pool = NULL;
	ppp->ppp_framed_ip_address.s_addr = 0;
}

/**
 * Change IP enableness.  When the enableness is change, npppd will operate
 * the route entry.
 */
void
npppd_set_ip_enabled(npppd *_this, npppd_ppp *ppp, int enabled)
{
	int was_enabled, found;
	slist *u;
	hash_link *hl;
	npppd_ppp *ppp1;

	NPPPD_ASSERT(ppp_ip_assigned(ppp));
	NPPPD_DBG((LOG_DEBUG,
	    "npppd_set_ip_enabled(%s/%s, %s)", ppp->username,
		inet_ntoa(ppp->ppp_framed_ip_address),
		(enabled)?"true" : "false"));

	/*
	 * Don't do anything if the enableness is not change.  Changing route
	 * makes many programs will wake up and do heavy operations, it causes
	 * system overload, so we refrain useless changing route.
	 */
	enabled = (enabled)? 1 : 0;
	was_enabled = (ppp->assigned_ip4_enabled != 0)? 1 : 0;
	if (enabled == was_enabled)
		return;

	ppp->assigned_ip4_enabled = enabled;
	if (enabled) {
		if (ppp->username[0] != '\0') {
			if ((u = npppd_get_ppp_by_user(_this, ppp->username))
			    == NULL) {
				if ((u = malloc(sizeof(slist))) == NULL) {
					ppp_log(ppp, LOG_ERR,
					    "Out of memory on %s: %m",
					    __func__);
				} else {
					slist_init(u);
					slist_set_size(u, 4);
					hash_insert(_this->map_user_ppp,
					    ppp->username, u);
					NPPPD_DBG((LOG_DEBUG,
					    "hash_insert(user->ppp, %s)",
					    ppp->username));
				}
			}
			if (u != NULL)	/* above malloc() may failed */
				slist_add(u, ppp);
		}

#ifndef	NO_ROUTE_FOR_POOLED_ADDRESS
		if (_this->iface[ppp->ifidx].using_pppx == 0) {
			if (ppp->snp.snp_next != NULL)
				/*
				 * There is a blackhole route that has same
				 * address/mask.
				 */
				in_route_delete(&ppp->ppp_framed_ip_address,
				    &ppp->ppp_framed_ip_netmask, &loop,
				    RTF_BLACKHOLE);
			/* See the comment for MRU_IPMTU() on ppp.h */
			if (ppp->ppp_framed_ip_netmask.s_addr == 0xffffffffL) {
				in_host_route_add(&ppp->ppp_framed_ip_address,
				    &ppp_iface(ppp)->ip4addr,
				    ppp_iface(ppp)->ifname,
				    MRU_IPMTU(ppp->peer_mru));
			} else {
				in_route_add(&ppp->ppp_framed_ip_address,
				    &ppp->ppp_framed_ip_netmask,
				    &ppp_iface(ppp)->ip4addr,
				    ppp_iface(ppp)->ifname, 0,
				    MRU_IPMTU(ppp->peer_mru));
			}
		}
#endif
	} else {
#ifndef	NO_ROUTE_FOR_POOLED_ADDRESS
		if (_this->iface[ppp->ifidx].using_pppx == 0) {
			if (ppp->ppp_framed_ip_netmask.s_addr == 0xffffffffL) {
				in_host_route_delete(&ppp->ppp_framed_ip_address,
				    &ppp_iface(ppp)->ip4addr);
			} else {
				in_route_delete(&ppp->ppp_framed_ip_address,
				    &ppp->ppp_framed_ip_netmask,
				    &ppp_iface(ppp)->ip4addr, 0);
			}
			if (ppp->snp.snp_next != NULL)
				/*
				 * There is a blackhole route that has same
				 * address/mask.
				 */
				in_route_add(&ppp->snp.snp_addr,
				    &ppp->snp.snp_mask, &loop, LOOPBACK_IFNAME,
				    RTF_BLACKHOLE, 0);
		}
#endif
		if (ppp->username[0] != '\0') {
			hl = hash_lookup(_this->map_user_ppp, ppp->username);
			NPPPD_ASSERT(hl != NULL);
			if (hl == NULL) {
				ppp_log(ppp, LOG_ERR,
				    "Unexpected error: cannot find user(%s) "
				    "from user database", ppp->username);
				return;
			}
			found = 0;
			u = hl->item;
			for (slist_itr_first(u); slist_itr_has_next(u);) {
				ppp1 = slist_itr_next(u);
				if (ppp1 == ppp) {
					slist_itr_remove(u);
					found++;
					break;
				}
			}
			if (found == 0) {
				ppp_log(ppp, LOG_ERR,
				    "Unexpected error: PPP instance is "
				    "not found in the user's list.");
			}
			NPPPD_ASSERT(found != 0);
			if (slist_length(u) <= 0) {
				/* The last PPP */
				NPPPD_DBG((LOG_DEBUG,
				    "hash_delete(user->ppp, %s)",
				    ppp->username));
				if (hash_delete(_this->map_user_ppp,
				    ppp->username, 0) != 0) {
					ppp_log(ppp, LOG_ERR,
					    "Unexpected error: cannot delete "
					    "user(%s) from user database",
					    ppp->username);
				}
				slist_fini(u);
				free(u);
			} else {
				/* Replace the reference. */
				ppp1 = slist_get(u, 0);
				hl->key = ppp1->username;
			}
		}
	}
}

/**
 * Assign the IP address.  Returning "struct in_addr" is stored IP address
 * in network byte order.
 * @param req_ip4	IP address request to assign.  If the address is used
 * already, this function will return fail.
 */
int
npppd_assign_ip_addr(npppd *_this, npppd_ppp *ppp, uint32_t req_ip4)
{
	uint32_t ip4, ip4mask;
	int dyna, rval, fallback_dyna;
	const char *reason = "out of the pool";
	struct sockaddr_npppd *snp;
	npppd_pool *pool;
	npppd_auth_base *realm;

	NPPPD_DBG((LOG_DEBUG, "%s() assigned=%s", __func__,
	    (ppp_ip_assigned(ppp))? "true" : "false"));
	if (ppp_ip_assigned(ppp))
		return 0;

	ip4 = INADDR_ANY;
	ip4mask = 0xffffffffL;
	realm = ppp->realm;
	dyna = 0;
	fallback_dyna = 0;
	pool = NULL;

	if (ppp->realm_framed_ip_address.s_addr == INADDR_USER_SELECT) {
		if (req_ip4 == INADDR_ANY)
			dyna = 1;
	} else if (ppp->realm_framed_ip_address.s_addr == INADDR_NAS_SELECT) {
		dyna = 1;
	} else {
		NPPPD_ASSERT(realm != NULL);
		fallback_dyna = 1;
		req_ip4 = ntohl(ppp->realm_framed_ip_address.s_addr);
		ip4mask = ntohl(ppp->realm_framed_ip_netmask.s_addr);
	}
	if (!dyna) {
		/*
		 * Realm requires the fixed IP address, but the address
		 * doesn't belong any address pool.  Fallback to dynamic
		 * assignment.
		 */
		pool = ppp_pool(ppp);
		rval = npppd_pool_get_assignability(pool, req_ip4, ip4mask,
		    &snp);
		switch (rval) {
		case ADDRESS_OK:
			if (snp->snp_type == SNP_POOL) {
				/*
				 * Fixed address pool can be used only if the
				 * realm specified to use it.
				 */
				if (ppp->realm_framed_ip_address
				    .s_addr != INADDR_USER_SELECT)
					ip4 = req_ip4;
				break;
			}
			ppp->assign_dynapool = 1;
			ip4 = req_ip4;
			break;
		case ADDRESS_RESERVED:
			reason = "reserved";
			break;
		case ADDRESS_OUT_OF_POOL:
			reason = "out of the pool";
			break;
		case ADDRESS_BUSY:
			fallback_dyna = 0;
			reason = "busy";
			break;
		default:
		case ADDRESS_INVALID:
			fallback_dyna = 0;
			reason = "invalid";
			break;
		}
#define	IP_4OCT(v) ((0xff000000 & (v)) >> 24), ((0x00ff0000 & (v)) >> 16),\
	    ((0x0000ff00 & (v)) >> 8), (0x000000ff & (v))
		if (ip4 == 0) {
			ppp_log(ppp, LOG_NOTICE,
			    "Requested IP address (%d.%d.%d.%d)/%d "
			    "is %s", IP_4OCT(req_ip4),
			    netmask2prefixlen(ip4mask), reason);
			if (fallback_dyna)
				goto dyna_assign;
			return 1;
		}
		ppp->assigned_pool = pool;

		ppp->ppp_framed_ip_address.s_addr = htonl(ip4);
		ppp->ppp_framed_ip_netmask.s_addr = htonl(ip4mask);
		ppp->acct_framed_ip_address = ppp->ppp_framed_ip_address;
	} else {
dyna_assign:
		pool = ppp_pool(ppp);
		ip4 = npppd_pool_get_dynamic(pool, ppp);
		if (ip4 == 0) {
			ppp_log(ppp, LOG_NOTICE,
			    "No free address in the pool.");
			return 1;
		}
		ppp->assigned_pool = pool;
		ppp->assign_dynapool = 1;
		ppp->ppp_framed_ip_address.s_addr = htonl(ip4);
		ppp->ppp_framed_ip_netmask.s_addr = htonl(0xffffffffL);
		ppp->acct_framed_ip_address = ppp->ppp_framed_ip_address;
	}

	return npppd_pool_assign_ip(ppp->assigned_pool, ppp);
}

static void *
rtlist_remove(slist *prtlist, struct radish *radish)
{
	struct radish *r;

	slist_itr_first(prtlist);
	while (slist_itr_has_next(prtlist)) {
		r = slist_itr_next(prtlist);
		if (!sockaddr_npppd_match(radish->rd_route, r->rd_route) ||
		    !sockaddr_npppd_match(radish->rd_mask, r->rd_mask))
			continue;

		return slist_itr_remove(prtlist);
	}

	return NULL;
}

/** Set {@link ::npppd#rd the only radish of npppd} */
int
npppd_set_radish(npppd *_this, void *radish_head)
{
	int rval, delppp0, count;
	struct sockaddr_npppd *snp;
	struct radish *radish, *r;
	slist rtlist0, rtlist1, delppp;
	npppd_ppp *ppp;
	void *dummy;

	slist_init(&rtlist0);
	slist_init(&rtlist1);
	slist_init(&delppp);

	if (radish_head != NULL) {
		if (rd2slist(radish_head, &rtlist1) != 0) {
			log_printf(LOG_WARNING, "rd2slist failed: %m");
			goto fail;
		}
	}
	if (_this->rd != NULL) {
		if (rd2slist(_this->rd, &rtlist0) != 0) {
			log_printf(LOG_WARNING, "rd2slist failed: %m");
			goto fail;
		}
	}
	if (_this->rd != NULL && radish_head != NULL) {
		for (slist_itr_first(&rtlist0); slist_itr_has_next(&rtlist0);) {
			radish = slist_itr_next(&rtlist0);
			snp = radish->rd_rtent;
		    /*
		     * replace the pool address
		     */
			if (snp->snp_type == SNP_POOL ||
			    snp->snp_type == SNP_DYN_POOL) {
				if (rd_lookup(radish->rd_route, radish->rd_mask,
					    radish_head) == NULL)
					continue;
				/* don't add */
				rtlist_remove(&rtlist1, radish);
				/* don't delete */
				slist_itr_remove(&rtlist0);
				continue;
			}
		    /*
		     * handle the active PPP sessions.
		     */
			NPPPD_ASSERT(snp->snp_type == SNP_PPP);
			ppp =  snp->snp_data_ptr;

			/* Don't delete the route of active PPP session */
			slist_itr_remove(&rtlist0);

			/* clear information about old pool configuration */
			snp->snp_next = NULL;

			delppp0 = 0;
			if (!rd_match((struct sockaddr *)snp, radish_head, &r)){
				/*
				 * If the address doesn't belong the new pools,
				 * add the PPP session to the deletion list.
				 */
				slist_add(&delppp, snp->snp_data_ptr);
				delppp0 = 1;
			} else {
				NPPPD_ASSERT(
				    ((struct sockaddr_npppd *)r->rd_rtent)
					->snp_type == SNP_POOL ||
				    ((struct sockaddr_npppd *)r->rd_rtent)
					->snp_type == SNP_DYN_POOL);
				/*
				 * If there is a pool entry that has same
				 * address/mask, then make the RADISH entry a
				 * list.  Set SNP_PPP as the first in the list,
				 * set current entry in snp->snp_next and
				 * delete it.
				 */
				if (sockaddr_npppd_match(
					    radish->rd_route, r->rd_route) &&
				    sockaddr_npppd_match(
					    radish->rd_mask, r->rd_mask)) {
					/*
					 * Releasing it, so remove it from the
					 * new routing list.
					 */
					rtlist_remove(&rtlist1, radish);
					/* set as snp_snp_next */
					snp->snp_next = r->rd_rtent;
					rval = rd_delete(r->rd_route,
					    r->rd_mask, radish_head, &dummy);
					NPPPD_ASSERT(rval == 0);
				}
			}
			/* Register to the new radish */
			rval = rd_insert(radish->rd_route, radish->rd_mask,
			    radish_head, snp);
			if (rval != 0) {
				errno = rval;
				ppp_log(((npppd_ppp *)snp->snp_data_ptr),
				    LOG_ERR,
				    "Fatal error on %s, cannot continue "
				    "this ppp session: %m", __func__);
				if (!delppp0)
					slist_add(&delppp, snp->snp_data_ptr);
			}
		}
	}
	count = 0;
#ifndef	NO_ROUTE_FOR_POOLED_ADDRESS
	if (_this->iface[0].using_pppx == 0) {
		for (slist_itr_first(&rtlist0); slist_itr_has_next(&rtlist0);) {
			radish = slist_itr_next(&rtlist0);
			in_route_delete(&SIN(radish->rd_route)->sin_addr,
			    &SIN(radish->rd_mask)->sin_addr, &loop,
			    RTF_BLACKHOLE);
			count++;
		}
		if (count > 0)
			log_printf(LOG_INFO,
			    "Deleted %d routes for old pool addresses", count);

		count = 0;
		for (slist_itr_first(&rtlist1); slist_itr_has_next(&rtlist1);) {
			radish = slist_itr_next(&rtlist1);
			in_route_add(&(SIN(radish->rd_route)->sin_addr),
			    &SIN(radish->rd_mask)->sin_addr, &loop,
			    LOOPBACK_IFNAME, RTF_BLACKHOLE, 0);
			count++;
		}
		if (count > 0)
			log_printf(LOG_INFO,
				    "Added %d routes for new pool addresses",
				    count);
	}
#endif
	slist_fini(&rtlist0);
	slist_fini(&rtlist1);

	if (_this->rd != NULL) {
		npppd_rd_walktree_delete(_this->rd);
		_this->rd = NULL;
	}
	if (radish_head == NULL)
		npppd_get_all_users(_this, &delppp);
	_this->rd = radish_head;

	for (slist_itr_first(&delppp); slist_itr_has_next(&delppp);) {
		ppp = slist_itr_next(&delppp);
                ppp_log(ppp, LOG_NOTICE,
                    "stop.  IP address of this ppp is out of the pool.: %s",
                    inet_ntoa(ppp->ppp_framed_ip_address));
		ppp_stop(ppp, NULL);
	}
	slist_fini(&delppp);

	return 0;
fail:
	slist_fini(&rtlist0);
	slist_fini(&rtlist1);
	slist_fini(&delppp);

	return 1;
}

/**
 * This function stores all users to {@link slist} and returns them.
 * References to {@link ::npppd_ppp} will be stored in users.
 */
static int
npppd_get_all_users(npppd *_this, slist *users)
{
	int rval;
	struct radish *rd;
	struct sockaddr_npppd *snp;
	slist list;

	NPPPD_ASSERT(_this != NULL);

	slist_init(&list);
	if (_this->rd == NULL)
		return 0;
	rval = rd2slist(_this->rd, &list);
	if (rval != 0)
		return rval;

	for (slist_itr_first(&list); slist_itr_has_next(&list);) {
		rd = slist_itr_next(&list);
		snp = rd->rd_rtent;
		if (snp->snp_type == SNP_PPP) {
			if (slist_add(users, snp->snp_data_ptr) == NULL) {
				log_printf(LOG_ERR,
				    "slist_add() failed in %s: %m", __func__);
				goto fail;
			}
		}
	}
	slist_fini(&list);

	return 0;
fail:
	slist_fini(&list);

	return 1;
}

static int
rd2slist_walk(struct radish *rd, void *list0)
{
	slist *list = list0;
	void *r;

	r = slist_add(list, rd);
	if (r == NULL)
		return -1;
	return 0;
}
static int
rd2slist(struct radish_head *h, slist *list)
{
	return rd_walktree(h, rd2slist_walk, list);
}

static void
npppd_reload0(npppd *_this)
{
	int  i;

	npppd_reload_config(_this);
#ifdef USE_NPPPD_ARP
	arp_set_strictintfnetwork(npppd_config_str_equali(_this, "arpd.strictintfnetwork", "true", ARPD_STRICTINTFNETWORK_DEFAULT));
	if (npppd_config_str_equali(_this, "arpd.enabled", "true", ARPD_DEFAULT) == 1)
        	arp_sock_init();
	else
		arp_sock_fini();
#endif
	npppd_modules_reload(_this);
	npppd_ifaces_load_config(_this);
	npppd_update_pool_reference(_this);
	npppd_auth_finalizer_periodic(_this);
	npppd_ipcp_stats_reload(_this);

	for (i = 0; i < countof(_this->iface); i++) {
		if (_this->iface[i].initialized != 0 &&
		    _this->iface[i].started == 0)
			npppd_iface_start(&_this->iface[i]);
	}
}

static void
npppd_update_pool_reference(npppd *_this)
{
	int  i, j;
	/* update iface to pool reference */
	for (i = 0; i < countof(_this->iface_pool); i++) {
		_this->iface_pool[i] = NULL;
		if (_this->iface[i].initialized == 0)
			continue;
		if (_this->iface[i].ipcpconf == NULL)
			continue;	/* no IPCP for this interface */

		for (j = 0; j < countof(_this->pool); j++) {
			if (_this->pool[j].initialized == 0)
				continue;
			if (strcmp(_this->iface[i].ipcpconf->name,
			    _this->pool[j].ipcp_name) == 0) {
				/* found the ipcp that has the pool */
				_this->iface_pool[i] = &_this->pool[j];
				break;
			}
		}
	}
}

/***********************************************************************
 * Signal handlers
 ***********************************************************************/
static void
npppd_on_sighup(int fd, short ev_type, void *ctx)
{
	npppd *_this;

	_this = ctx;
#ifndef	NO_DELAYED_RELOAD
	if (_this->delayed_reload > 0)
		_this->reloading_count = _this->delayed_reload;
	else
#endif
		npppd_reload0(_this);
}

static void
npppd_on_sigterm(int fd, short ev_type, void *ctx)
{
	npppd *_this;

	_this = ctx;
	npppd_stop(_this);
}

static void
npppd_on_sigint(int fd, short ev_type, void *ctx)
{
	npppd *_this;

	_this = ctx;
	npppd_stop(_this);
}

static void
npppd_on_sigchld(int fd, short ev_type, void *ctx)
{
	int status;
	pid_t wpid;
	npppd *_this;

	_this = ctx;
	wpid = privsep_priv_pid();
	if (wait4(wpid, &status, WNOHANG, NULL) == wpid) {
		if (WIFSIGNALED(status))
			log_printf(LOG_WARNING,
			    "privileged process exits abnormally.  signal=%d",
			    WTERMSIG(status));
		else
			log_printf(LOG_WARNING,
			    "privileged process exits abnormally.  status=%d",
			    WEXITSTATUS(status));
		_this->stop_by_error = 1;
		npppd_stop(_this);
	}
}
/***********************************************************************
 * Miscellaneous functions
 ***********************************************************************/
static uint32_t
str_hash(const void *ptr, int sz)
{
	uint32_t hash = 0;
	int i, len;
	const char *str;

	str = ptr;
	len = strlen(str);
	for (i = 0; i < len; i++)
		hash = hash*0x1F + str[i];
	hash = (hash << 16) ^ (hash & 0xffff);

	return hash % sz;
}

/**
 * Select a authentication realm that is for given {@link ::npppd_ppp PPP}.
 * Return 0 on success.
 */
int
npppd_ppp_bind_realm(npppd *_this, npppd_ppp *ppp, const char *username, int
    eap_required)
{
	struct confbind *bind;
	npppd_auth_base *realm = NULL, *realm0 = NULL, *realm1 = NULL;
	char             buf1[MAX_USERNAME_LENGTH];
	int              lsuffix, lusername, lmax;

	NPPPD_ASSERT(_this != NULL);
	NPPPD_ASSERT(ppp != NULL);
	NPPPD_ASSERT(username != NULL);

	/*
	 * If the PPP suffix is the longest, and the length of the suffix is
	 * same, select the first one.
	 */
	lusername = strlen(username);
	lmax = -1;
	realm = NULL;

	TAILQ_FOREACH(bind, &_this->conf.confbinds, entry) {
		if (strcmp(bind->tunnconf->name, ppp->phy_label) != 0)
			continue;

		realm0 = NULL;
		slist_itr_first(&_this->realms);
		while (slist_itr_has_next(&_this->realms)) {
			realm1 = slist_itr_next(&_this->realms);
			if (!npppd_auth_is_usable(realm1))
				continue;
			if (eap_required && !npppd_auth_is_eap_capable(realm1))
				continue;
			if (strcmp(npppd_auth_get_name(realm1),
			    bind->authconf->name) == 0) {
				realm0 = realm1;
				break;
			}
		}
		if (realm0 == NULL)
			continue;

		lsuffix = strlen(npppd_auth_get_suffix(realm0));
		if (lsuffix > lmax &&
		    (lsuffix == 0 ||
			(lsuffix < lusername && strcmp(username + lusername
				- lsuffix, npppd_auth_get_suffix(realm0))
				== 0))) {
			lmax = lsuffix;
			realm = realm0;
		}
	}

	if (realm == NULL) {
		log_printf(LOG_INFO, "user='%s' could not bind any realms",
		    username);
		return 1;
	}
	NPPPD_DBG((LOG_DEBUG, "bind realm %s", npppd_auth_get_name(realm)));

	if (npppd_auth_get_type(realm) == NPPPD_AUTH_TYPE_LOCAL)
		/* hook the auto reload */
		npppd_auth_get_user_password(realm,
		    npppd_auth_username_for_auth(realm1, username, buf1), NULL,
			NULL);
	ppp->realm = realm;

	return 0;
}

/** Is assigned realm a LOCAL authentication? */
int
npppd_ppp_is_realm_local(npppd *_this, npppd_ppp *ppp)
{
	NPPPD_ASSERT(_this != NULL);
	NPPPD_ASSERT(ppp != NULL);

	if (ppp->realm == NULL)
		return 0;

	return (npppd_auth_get_type(ppp->realm) == NPPPD_AUTH_TYPE_LOCAL)
	    ? 1 : 0;
}

/** Is assigned realm a RADIUS authentication? */
int
npppd_ppp_is_realm_radius(npppd *_this, npppd_ppp *ppp)
{
	NPPPD_ASSERT(_this != NULL);
	NPPPD_ASSERT(ppp != NULL);

	if (ppp->realm == NULL)
		return 0;

	return (npppd_auth_get_type(ppp->realm) == NPPPD_AUTH_TYPE_RADIUS)
	    ? 1 : 0;
}

/** Is assigned realm usable? */
int
npppd_ppp_is_realm_ready(npppd *_this, npppd_ppp *ppp)
{
	if (ppp->realm == NULL)
		return 0;

	return npppd_auth_is_ready(ppp->realm);
}

/** Return the name of assigned realm */
const char *
npppd_ppp_get_realm_name(npppd *_this, npppd_ppp *ppp)
{
	if (ppp->realm == NULL)
		return "(none)";
	return npppd_auth_get_name(ppp->realm);
}

/** Return the interface name that bound given {@link ::npppd_ppp PPP} */
const char *
npppd_ppp_get_iface_name(npppd *_this, npppd_ppp *ppp)
{
	if (ppp == NULL || ppp->ifidx < 0)
		return "(not binding)";
	return ppp_iface(ppp)->ifname;
}

/** Is the interface usable? */
int
npppd_ppp_iface_is_ready(npppd *_this, npppd_ppp *ppp)
{
	return (npppd_iface_ip_is_ready(ppp_iface(ppp)) &&
	    ppp_ipcp(ppp) != NULL)? 1 : 0;
}

/** Select a suitable interface for {@link :npppd_ppp PPP} and bind them  */
int
npppd_ppp_bind_iface(npppd *_this, npppd_ppp *ppp)
{
	int              i, ifidx;
	struct confbind *bind;
	struct ipcpstat *ipcpstat;

	NPPPD_ASSERT(_this != NULL);
	NPPPD_ASSERT(ppp != NULL);

	if (ppp->ifidx >= 0)
		return 0;

	TAILQ_FOREACH(bind, &_this->conf.confbinds, entry) {
		if (strcmp(bind->tunnconf->name, ppp->phy_label) != 0)
			continue;
		if (ppp->realm == NULL) {
			if (bind->authconf == NULL)
				break;
		} else if (strcmp(bind->authconf->name,
		    npppd_auth_get_name(ppp->realm)) == 0)
			break;
	}
	if (bind == NULL)
		return 1;

	/* Search a interface */
	ifidx = -1;
	for (i = 0; i < countof(_this->iface); i++) {
		if (_this->iface[i].initialized == 0)
			continue;
		if (strcmp(_this->iface[i].ifname, bind->iface->name) == 0)
			ifidx = i;
	}
	if (ifidx < 0)
		return 1;

	ppp->ifidx = ifidx;
	NPPPD_ASSERT(ppp_ipcp(ppp) != NULL);
	ipcpstat = npppd_get_ipcp_stat(&_this->ipcpstats, ppp_ipcp(ppp)->name);
	if (ipcpstat == NULL) {
		ppp_log(ppp, LOG_WARNING, "Unknown IPCP %s",
		    ppp_ipcp(ppp)->name);
		ppp->ifidx = -1; /* unbind interface */
		return 1;
	}
	if (ppp_ipcp(ppp)->max_session > 0 &&
	    ipcpstat->nsession >= ppp_ipcp(ppp)->max_session) {
		ppp_log(ppp, LOG_WARNING,
		    "Number of sessions per IPCP reaches out of the limit=%d",
		    ppp_ipcp(ppp)->max_session);
		ppp->ifidx = -1; /* unbind interface */
		return 1;
	}

	if (_this->conf.max_session > 0 &&
	    _this->nsession >= _this->conf.max_session) {
		ppp_log(ppp, LOG_WARNING,
		    "Number of sessions reaches out of the limit=%d",
		    _this->conf.max_session);
		ppp->ifidx = -1; /* unbind interface */
		return 1;
	}
	_this->nsession++;

	LIST_INSERT_HEAD(&ipcpstat->ppp, ppp, ipcpstat_entry);
	ppp->ipcpstat = ipcpstat;
	ipcpstat->nsession++;

	return 0;
}

/** Unbind the interface from the {@link ::npppd_ppp PPP} */
void
npppd_ppp_unbind_iface(npppd *_this, npppd_ppp *ppp)
{
	if (ppp->ifidx >= 0) {
		_this->nsession--;
		if (ppp->ipcpstat!= NULL) {
			ppp->ipcpstat->nsession--;
			LIST_REMOVE(ppp, ipcpstat_entry);
		}
	}
	ppp->ifidx = -1;
}

static int
npppd_rd_walktree_delete(struct radish_head *rh)
{
	void *dummy;
	struct radish *rd;
	slist list;

	slist_init(&list);
	if (rd2slist(rh, &list) != 0)
		return 1;
	for (slist_itr_first(&list); slist_itr_has_next(&list);) {
		rd = slist_itr_next(&list);
		rd_delete(rd->rd_route, rd->rd_mask, rh, &dummy);
	}
	slist_fini(&list);

	free(rh);

	return 0;
}

#ifdef USE_NPPPD_RADIUS
/**
 * Return radius_req_setting for the given {@link ::npppd_ppp PPP}.
 * @return return NULL if no usable RADIUS setting.
 */
void *
npppd_get_radius_auth_setting(npppd *_this, npppd_ppp *ppp)
{
	NPPPD_ASSERT(_this != NULL);
	NPPPD_ASSERT(ppp != NULL);

	if (ppp->realm == NULL)
		return NULL;
	if (!npppd_ppp_is_realm_radius(_this, ppp))
		return NULL;

	return npppd_auth_radius_get_radius_auth_setting(ppp->realm);
}
#endif

/** Finalize authentication realm */
static void
npppd_auth_finalizer_periodic(npppd *_this)
{
	int ndisposing = 0, refcnt;
	slist users;
	npppd_auth_base *auth_base;
	npppd_ppp *ppp;

	/*
	 * For the realms with disposing flag, if the realm has assigned PPPs,
	 * disconnect them.  If all PPPs are disconnected then free the realm.
	 */
	NPPPD_DBG((DEBUG_LEVEL_2, "%s() called", __func__));
	slist_itr_first(&_this->realms);
	while (slist_itr_has_next(&_this->realms)) {
		auth_base = slist_itr_next(&_this->realms);
		if (!npppd_auth_is_disposing(auth_base))
			continue;
		refcnt = 0;
		if (ndisposing++ == 0) {
			slist_init(&users);
			if (npppd_get_all_users(_this, &users) != 0) {
				log_printf(LOG_WARNING,
				    "npppd_get_all_users() failed in %s(): %m",
				    __func__);
				break;
			}
		}
		slist_itr_first(&users);
		while (slist_itr_has_next(&users)) {
			ppp = slist_itr_next(&users);
			if (ppp->realm == auth_base) {
				refcnt++;
				ppp_stop(ppp, NULL);
				ppp_log(ppp, LOG_INFO,
				    "Stop request by npppd.  Binding "
				    "authentication realm is disposing.  "
				    "realm=%s", npppd_auth_get_name(auth_base));
				slist_itr_remove(&users);
			}
		}
		if (refcnt == 0) {
			npppd_auth_destroy(auth_base);
			slist_itr_remove(&_this->realms);
		}
	}
	if (ndisposing > 0)
		slist_fini(&users);
}

/** compare sockaddr_npppd.  return 0 if matches */
int
sockaddr_npppd_match(void *a0, void *b0)
{
	struct sockaddr_npppd *a, *b;

	a = a0;
	b = b0;

	return (a->snp_addr.s_addr == b->snp_addr.s_addr)? 1 : 0;
}

/**
 * This function stores the username for authentication to the space specified
 * by username_buffer and returns it.  username_buffer must have space more
 * than MAX_USERNAME_LENGTH.
 */
const char *
npppd_ppp_get_username_for_auth(npppd *_this, npppd_ppp *ppp,
    const char *username, char *username_buffer)
{
	NPPPD_ASSERT(_this != NULL);
	NPPPD_ASSERT(ppp != NULL);
	NPPPD_ASSERT(ppp->realm != NULL);

	return npppd_auth_username_for_auth(ppp->realm, username,
	    username_buffer);
}

const char *
npppd_tunnel_protocol_name(int tunn_protocol)
{
	switch (tunn_protocol) {
	case NPPPD_TUNNEL_NONE:
		return "None";
	case NPPPD_TUNNEL_L2TP:
		return "L2TP";
	case NPPPD_TUNNEL_PPTP:
		return "PPTP";
	case NPPPD_TUNNEL_PPPOE:
		return "PPPoE";
	case NPPPD_TUNNEL_SSTP:
		return "SSTP";
	}

	return "Error";
}

const char *
npppd_ppp_tunnel_protocol_name(npppd *_this, npppd_ppp *ppp)
{
	return npppd_tunnel_protocol_name(ppp->tunnel_type);
}

struct tunnconf *
npppd_get_tunnconf(npppd *_this, const char *name)
{
	struct tunnconf *conf;

	TAILQ_FOREACH(conf, &_this->conf.tunnconfs, entry) {
		if (strcmp(conf->name, name) == 0)
			return conf;
	}

	return NULL;
}

void
npppd_on_ppp_start(npppd *_this, npppd_ppp *ppp)
{
	struct ctl_conn  *c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (npppd_ctl_add_started_ppp_id(c->ctx, ppp->id) == 0) {
			npppd_ctl_imsg_compose(c->ctx, &c->iev.ibuf);
			imsg_event_add(&c->iev);
		}
	}
}

void
npppd_on_ppp_stop(npppd *_this, npppd_ppp *ppp)
{
	struct ctl_conn  *c;

	TAILQ_FOREACH(c, &ctl_conns, entry) {
		if (npppd_ctl_add_stopped_ppp(c->ctx, ppp) == 0) {
			npppd_ctl_imsg_compose(c->ctx, &c->iev.ibuf);
			imsg_event_add(&c->iev);
		}
	}
}

void
imsg_event_add(struct imsgev *iev)
{
	iev->events = EV_READ;
	if (imsgbuf_queuelen(&iev->ibuf) > 0)
		iev->events |= EV_WRITE;

	event_del(&iev->ev);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev->data);
	event_add(&iev->ev, NULL);
}
