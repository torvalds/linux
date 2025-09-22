/*	$OpenBSD: npppd_config.c,v 1.15 2024/07/11 14:05:59 yasuoka Exp $ */

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
/* $Id: npppd_config.c,v 1.15 2024/07/11 14:05:59 yasuoka Exp $ */
/*@file
 * This file provides functions which operates configuration and so on.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <netinet/ip.h>
#include <net/route.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <time.h>
#include <event.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>

#include "addr_range.h"
#include "debugutil.h"
#include "npppd_subr.h"
#include "npppd_local.h"
#include "npppd_auth.h"
#include "npppd_iface.h"
#include "radish.h"

#include "pathnames.h"

#ifdef NPPPD_CONFIG_DEBUG
#define NPPPD_CONFIG_DBG(x) 	log_printf x
#define NPPPD_CONFIG_ASSERT(x) ASSERT(x)
#else
#define NPPPD_CONFIG_DBG(x)
#define NPPPD_CONFIG_ASSERT(x)
#endif

static int              npppd_pool_load(npppd *);
static int              npppd_auth_realm_reload (npppd *);
static npppd_auth_base *realm_list_remove (slist *, const char *);

int
npppd_config_check(const char *path)
{
	struct npppd_conf  conf;

	npppd_conf_init(&conf);
	return npppd_conf_parse(&conf, path);
}

/***********************************************************************
 * Reading the configuration. This is the export function which
 * aggregates functions to read from each part.
 ***********************************************************************/
/**
 * reload the configuration file.
 * @param   _this   pointer indicated to npppd
 * @returns A 0 is returned if succeeds, otherwise non 0 is returned
 *	    in case of configuration error.
 */
int
npppd_reload_config(npppd *_this)
{
	int                retval = -1;
	struct npppd_conf  conf;

	npppd_conf_init(&conf);
	if (npppd_conf_parse(&conf, _this->config_file) != 0) {
		log_printf(LOG_ERR, "Load configuration from='%s' failed",
		    _this->config_file);
		retval = -1;
		goto fail;
	}

	_this->conf = conf;

	retval = 0;
	log_printf(LOG_NOTICE, "Load configuration from='%s' successfully.",
	    _this->config_file);

	/* FALLTHROUGH */
fail:

	return retval;
}

/** reload the configuration for each module */
int
npppd_modules_reload(npppd *_this)
{
	int  rval;

	rval = 0;
	if (npppd_pool_load(_this) != 0)
		return -1;

	npppd_auth_realm_reload(_this);
#ifdef USE_NPPPD_L2TP
	rval |= l2tpd_reload(&_this->l2tpd, &_this->conf.l2tp_confs);
#endif
#ifdef USE_NPPPD_PPTP
	rval |= pptpd_reload(&_this->pptpd, &_this->conf.pptp_confs);
#endif
#ifdef USE_NPPPD_PPPOE
	rval |= pppoed_reload(&_this->pppoed, &_this->conf.pppoe_confs);
#endif
#ifdef USE_NPPPD_RADIUS
	npppd_radius_dae_init(_this);
#endif

	return rval;
}

/***********************************************************************
 * reload the configuration on each part
 ***********************************************************************/
/** load the configuration for IP address pool */
static int
npppd_pool_load(npppd *_this)
{
	int n, i, j;
	npppd_pool pool0[NPPPD_MAX_IFACE];
	struct radish_head *rd_curr, *rd_new;
	struct ipcpconf *ipcp;

	rd_curr = _this->rd;
	rd_new = NULL;

	n = 0;
	if (!rd_inithead((void *)&rd_new, 0x41,
	    sizeof(struct sockaddr_npppd),
	    offsetof(struct sockaddr_npppd, snp_addr),
	    sizeof(struct in_addr), sockaddr_npppd_match)) {
		goto fail;
	}
	_this->rd = rd_new;

	TAILQ_FOREACH(ipcp, &_this->conf.ipcpconfs, entry) {
		if (n >= countof(_this->pool)) {
			log_printf(LOG_WARNING, "number of the pool reached "
			    "limit=%d",(int)countof(_this->pool));
			break;
		}
		if (npppd_pool_init(&pool0[n], _this, ipcp->name) != 0) {
			log_printf(LOG_WARNING, "Failed to initialize "
			    "npppd_pool '%s': %m", ipcp->name);
			goto fail;
		}
		if (npppd_pool_reload(&pool0[n]) != 0)
			goto fail;
		n++;
	}
	for (; n < countof(pool0); n++)
		pool0[n].initialized = 0;

	_this->rd = rd_curr;	/* backup */
	if (npppd_set_radish(_this, rd_new) != 0)
		goto fail;

	for (i = 0; i < countof(_this->pool); i++) {
		if (_this->pool[i].initialized != 0)
			npppd_pool_uninit(&_this->pool[i]);
		if (pool0[i].initialized == 0)
			continue;
		_this->pool[i] = pool0[i];
		/* swap references */
		for (j = 0; j < _this->pool[i].addrs_size; j++) {
			if (_this->pool[i].initialized == 0)
				continue;
			_this->pool[i].addrs[j].snp_data_ptr = &_this->pool[i];
		}
	}
	log_printf(LOG_INFO, "Loading pool config successfully.");

	return 0;
fail:
	/* rollback */
	for (i = 0; i < n; i++) {
		if (pool0[i].initialized != 0)
			npppd_pool_uninit(&pool0[i]);
	}

	if (rd_curr != NULL)
		_this->rd = rd_curr;

	if (rd_new != NULL) {
		rd_walktree(rd_new,
		    (int (*)(struct radish *, void *))rd_unlink,
		    rd_new->rdh_top);
		free(rd_new);
	}
	log_printf(LOG_NOTICE, "Loading pool config failed");

	return 1;
}

/* authentication realm */
static int
npppd_auth_realm_reload(npppd *_this)
{
	int              rval;
	slist            realms0, nrealms;
	struct authconf *auth;
	npppd_auth_base *auth_base;

	rval = 0;
	slist_init(&realms0);
	slist_init(&nrealms);

	if (slist_add_all(&realms0, &_this->realms) != 0) {
		log_printf(LOG_WARNING, "slist_add_all() failed in %s(): %m",
		__func__);
		goto fail;
	}

	TAILQ_FOREACH(auth, &_this->conf.authconfs, entry) {
#ifndef USE_NPPPD_RADIUS
		if (auth->auth_type == NPPPD_AUTH_TYPE_RADIUS)  {
			log_printf(LOG_WARNING, "radius support is not "
			    "enabled by compile time.");
			continue;
		}
#endif
		auth_base = realm_list_remove(&realms0, auth->name);
		if (auth_base != NULL &&
		    npppd_auth_get_type(auth_base) != auth->auth_type) {
			/*
			 * The type of authentication has been changed in the
			 * same label name.
			 */
			slist_add(&realms0, auth_base);
			auth_base = NULL;
		}

		if (auth_base == NULL) {
			/* create newly */
			if ((auth_base = npppd_auth_create(auth->auth_type,
			    auth->name, _this)) == NULL) {
				log_printf(LOG_WARNING, "npppd_auth_create() "
				    "failed in %s(): %m", __func__);
				goto fail;
			}
		}
		slist_add(&nrealms, auth_base);
	}
	if (slist_set_size(&_this->realms, slist_length(&nrealms)) != 0) {
		log_printf(LOG_WARNING, "slist_set_size() failed in %s(): %m",
		    __func__);
		goto fail;
	}

	slist_itr_first(&realms0);
	while (slist_itr_has_next(&realms0)) {
		auth_base = slist_itr_next(&realms0);
		if (npppd_auth_is_disposing(auth_base))
			continue;
		npppd_auth_dispose(auth_base);
	}

	slist_itr_first(&nrealms);
	while (slist_itr_has_next(&nrealms)) {
		auth_base = slist_itr_next(&nrealms);
		rval |= npppd_auth_reload(auth_base);
	}
	slist_remove_all(&_this->realms);
	(void)slist_add_all(&_this->realms, &nrealms);
	(void)slist_add_all(&_this->realms, &realms0);

	slist_fini(&realms0);
	slist_fini(&nrealms);

	return rval;
fail:

	slist_itr_first(&nrealms);
	while (slist_itr_has_next(&nrealms)) {
		auth_base = slist_itr_next(&nrealms);
		npppd_auth_destroy(auth_base);
	}
	slist_fini(&realms0);
	slist_fini(&nrealms);

	return 1;
}

static npppd_auth_base *
realm_list_remove(slist *list0, const char *label)
{
	npppd_auth_base *base;

	for (slist_itr_first(list0); slist_itr_has_next(list0); ) {
		base = slist_itr_next(list0);
		if (npppd_auth_is_disposing(base))
			continue;
		if (strcmp(npppd_auth_get_name(base), label) == 0)
			return slist_itr_remove(list0);
	}

	return NULL;
}

/** load the interface configuration */
int
npppd_ifaces_load_config(npppd *_this)
{
	int           i;
	struct iface *iface;
	npppd_iface  *niface;

	for (i = 0; i < countof(_this->iface); i++) {
		if (_this->iface[i].initialized == 0)
			continue;
		TAILQ_FOREACH(iface, &_this->conf.ifaces, entry) {
			if (strcmp(_this->iface[i].ifname, iface->name) == 0)
				break;
		}
		if (iface == NULL) {
			npppd_iface_stop(&_this->iface[i]);
			npppd_iface_fini(&_this->iface[i]);
		}
	}
	TAILQ_FOREACH(iface, &_this->conf.ifaces, entry) {
		/* find the existing entry or first free entry */
		niface = NULL;
		for (i = 0; i < countof(_this->iface); i++) {
			if (_this->iface[i].initialized == 0) {
				if (niface == NULL)
					niface = &_this->iface[i];
				continue;
			}
			if (strcmp(_this->iface[i].ifname, iface->name) == 0) {
				niface = &_this->iface[i];
				break;
			}
		}
		if (niface == NULL) {
			log_printf(LOG_WARNING,
			    "number of the interface reached limit=%d",
			    (int)countof(_this->iface));
			break;
		}
		if (niface->initialized == 0)
			npppd_iface_init(_this, niface, iface);
		else
			npppd_iface_reinit(niface, iface);
	}

	return 0;
}
