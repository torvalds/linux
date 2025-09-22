/*	$OpenBSD: npppd_pool.c,v 1.11 2022/08/29 02:58:13 jsg Exp $ */

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
/**@file */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/route.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <stdio.h>
#include <time.h>
#include <event.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdarg.h>
#include <netdb.h>

#include "slist.h"
#include "debugutil.h"
#include "addr_range.h"
#include "radish.h"
#include "npppd_local.h"
#include "npppd_pool.h"
#include "npppd_subr.h"
#include "net_utils.h"

#ifdef	NPPPD_POOL_DEBUG
#define	NPPPD_POOL_DBG(x)	npppd_pool_log x
#define	NPPPD_POOL_ASSERT(cond)					\
	if (!(cond)) {						\
	    fprintf(stderr,					\
		"\nASSERT(" #cond ") failed on %s() at %s:%d.\n"\
		, __func__, __FILE__, __LINE__);		\
	    abort(); 						\
	}
#else
#define	NPPPD_POOL_ASSERT(cond)
#define	NPPPD_POOL_DBG(x)
#endif
#define	A(v) ((0xff000000 & (v)) >> 24), ((0x00ff0000 & (v)) >> 16),	\
	    ((0x0000ff00 & (v)) >> 8), (0x000000ff & (v))
#define	SA(sin4)	((struct sockaddr *)(sin4))

#define SHUFLLE_MARK 0xffffffffL
static int  npppd_pool_log(npppd_pool *, int, const char *, ...) __printflike(3, 4);
static int  is_valid_host_address (uint32_t);
static int  npppd_pool_regist_radish(npppd_pool *, struct in_addr_range *,
    struct sockaddr_npppd *, int );


/***********************************************************************
 * npppd_pool object management
 ***********************************************************************/
/** Initialize npppd_poll. */
int
npppd_pool_init(npppd_pool *_this, npppd *base, const char *name)
{
	memset(_this, 0, sizeof(npppd_pool));

	strlcpy(_this->ipcp_name, name, sizeof(_this->ipcp_name));
	_this->npppd = base;
	slist_init(&_this->dyna_addrs);

	_this->initialized = 1;

	return 0;
}

/** Start to use npppd_pool. */
int
npppd_pool_start(npppd_pool *_this)
{
	return 0;	/* nothing to do */
}

/** Finalize npppd_poll. */
void
npppd_pool_uninit(npppd_pool *_this)
{
	_this->initialized = 0;

	slist_fini(&_this->dyna_addrs);
	free(_this->addrs);
	_this->addrs = NULL;
	_this->addrs_size = 0;
	_this->npppd = NULL;
}

/** Reload configuration. */
int
npppd_pool_reload(npppd_pool *_this)
{
	int i, count, addrs_size;
	struct sockaddr_npppd *addrs;
	struct in_addr_range *pool, *dyna_pool, *range;
	char buf0[BUFSIZ], buf1[BUFSIZ];
	struct ipcpconf *ipcp;

	addrs = NULL;
	pool = NULL;
	dyna_pool = NULL;
	buf0[0] = '\0';

	TAILQ_FOREACH(ipcp, &_this->npppd->conf.ipcpconfs, entry) {
		if (strcmp(ipcp->name, _this->ipcp_name) == 0) {
			dyna_pool = ipcp->dynamic_pool;
			pool = ipcp->static_pool;
		}
	}

	addrs_size = 0;
	for (range = dyna_pool; range != NULL; range = range->next)
		addrs_size++;
	for (range = pool; range != NULL; range = range->next)
		addrs_size++;

	if ((addrs = calloc(addrs_size + 1, sizeof(struct sockaddr_npppd)))
	    == NULL) {
		/* addr_size + 1 because of avoiding calloc(0). */
		npppd_pool_log(_this, LOG_WARNING,
		    "calloc() failed in %s: %m", __func__);
		goto fail;
	}

	/* Register dynamic pool address with RADISH. */
	count = 0;
	for (i = 0, range = dyna_pool; range != NULL; range = range->next, i++){
		if (npppd_pool_regist_radish(_this, range, &addrs[count], 1))
			goto fail;
		if (count == 0)
			strlcat(buf0, "dyn_pool=[", sizeof(buf0));
		else
			strlcat(buf0, ",", sizeof(buf0));
		snprintf(buf1, sizeof(buf1), "%d.%d.%d.%d/%d",
		    A(range->addr), netmask2prefixlen(range->mask));
		strlcat(buf0, buf1, sizeof(buf0));
		count++;
	}
	if (i > 0)
		strlcat(buf0, "] ", sizeof(buf0));

	/* Register static pool address with RADISH. */
	for (i = 0, range = pool; range != NULL; range = range->next, i++) {
		if (npppd_pool_regist_radish(_this, range, &addrs[count], 0))
			goto fail;
		if (i == 0)
			strlcat(buf0, "pool=[", sizeof(buf0));
		else
			strlcat(buf0, ",", sizeof(buf0));
		snprintf(buf1, sizeof(buf1), "%d.%d.%d.%d/%d",
		    A(range->addr), netmask2prefixlen(range->mask));
		strlcat(buf0, buf1, sizeof(buf0));
		count++;
	}
	if (i > 0)
		strlcat(buf0, "]", sizeof(buf0));

	npppd_pool_log(_this, LOG_INFO, "%s", buf0);

	count = 0;
	slist_add(&_this->dyna_addrs, (void *)SHUFLLE_MARK);
	for (range = dyna_pool; range != NULL; range = range->next) {
		if (count >= NPPPD_MAX_POOLED_ADDRS)
			break;
		for (i = 0; i <= ~(range->mask); i++) {
			if (!is_valid_host_address(range->addr + i))
				continue;
			if (count >= NPPPD_MAX_POOLED_ADDRS)
				break;
			slist_add(&_this->dyna_addrs,
			    (void *)(uintptr_t)(range->addr + i));
			count++;
		}
	}
	free(_this->addrs);
	_this->addrs = addrs;
	_this->addrs_size = addrs_size;

	return 0;
fail:
	free(addrs);

	return 1;
}

static int
npppd_pool_regist_radish(npppd_pool *_this, struct in_addr_range *range,
    struct sockaddr_npppd *snp, int is_dynamic)
{
	int rval;
	struct sockaddr_in sin4a, sin4b;
	struct sockaddr_npppd *snp0;
	npppd_pool *npool0;

	memset(&sin4a, 0, sizeof(sin4a));
	memset(&sin4b, 0, sizeof(sin4b));
	sin4a.sin_len = sin4b.sin_len = sizeof(sin4a);
	sin4a.sin_family = sin4b.sin_family = AF_INET;
	sin4a.sin_addr.s_addr = htonl(range->addr);
	sin4b.sin_addr.s_addr = htonl(range->mask);

	snp->snp_len = sizeof(struct sockaddr_npppd);
	snp->snp_family = AF_INET;
	snp->snp_addr.s_addr = htonl(range->addr);
	snp->snp_mask.s_addr = htonl(range->mask);
	snp->snp_data_ptr = _this;
	if (is_dynamic)
		snp->snp_type = SNP_DYN_POOL;
	else
		snp->snp_type = SNP_POOL;

	if ((snp0 = rd_lookup(SA(&sin4a), SA(&sin4b),
	    _this->npppd->rd)) != NULL) {
		/*
		 * Immediately after the radish tree is initialized,
		 * assuming that it has only POOL entry.
		 */
		NPPPD_POOL_ASSERT(snp0->snp_type != SNP_PPP);
		npool0 = snp0->snp_data_ptr;

		if (!is_dynamic && npool0 == _this)
			/* Already registered as dynamic pool address. */
			return 0;

		npppd_pool_log(_this, LOG_WARNING,
		    "%d.%d.%d.%d/%d is already defined as '%s'(%s)",
		    A(range->addr), netmask2prefixlen(range->mask),
		    npool0->ipcp_name, (snp0->snp_type == SNP_POOL)
			? "static" : "dynamic");
		goto fail;
	}
	if ((rval = rd_insert(SA(&sin4a), SA(&sin4b), _this->npppd->rd,
	    snp)) != 0) {
		errno = rval;
		npppd_pool_log(_this, LOG_WARNING,
		    "rd_insert(%d.%d.%d.%d/%d) failed: %m",
		    A(range->addr), netmask2prefixlen(range->mask));
		goto fail;
	}

	return 0;
fail:
	return 1;

}

/***********************************************************************
 * API
 ***********************************************************************/
/** Assign dynamic pool address. */
uint32_t
npppd_pool_get_dynamic(npppd_pool *_this, npppd_ppp *ppp)
{
	int shuffle_cnt;
	uintptr_t result = 0;
	struct sockaddr_npppd *snp;
	npppd_ppp *ppp0;

	shuffle_cnt = 0;
	slist_itr_first(&_this->dyna_addrs);
	while (slist_length(&_this->dyna_addrs) > 1 &&
	    slist_itr_has_next(&_this->dyna_addrs)) {
		result = (uintptr_t)slist_itr_next(&_this->dyna_addrs);
		if (result == 0)
			break;
		/* shuffle */
		if ((uint32_t)result == SHUFLLE_MARK) {
			/*
			 * When the free list is empty, SHUFLLE_MARK is
			 * retrieved twice sequentially.  This means there is
			 * no address to use.
			 */
			if (shuffle_cnt++ > 0) {
				result = 0;
				break;
			}
			NPPPD_POOL_DBG((_this, LOG_DEBUG, "shuffle"));
			slist_itr_remove(&_this->dyna_addrs);
			slist_shuffle(&_this->dyna_addrs);
			slist_add(&_this->dyna_addrs, (void *)result);
			slist_itr_first(&_this->dyna_addrs);
			continue;
		}
		slist_itr_remove(&_this->dyna_addrs);

		switch (npppd_pool_get_assignability(_this, (uint32_t)result,
		    0xffffffffL, &snp)) {
		case ADDRESS_OK:
			/* only succeed here */
			return (uint32_t)result;
		default:
			/*
			 * Used as a interface address
			 */
			continue;
		case ADDRESS_BUSY:
			/*
			 * Used by the previous configuration.
			 */
			NPPPD_POOL_ASSERT(snp != NULL);
			NPPPD_POOL_ASSERT(snp->snp_type == SNP_PPP);
			ppp0 = snp->snp_data_ptr;
			ppp0->assigned_pool = _this;
			ppp0->assign_dynapool = 1;	/* need to return */
			continue;
		}
		break;
	}
	return (uint32_t)0;
}

static inline int
npppd_is_ifcace_ip4addr(npppd *_this, uint32_t ip4addr)
{
	int i;

	for (i = 0; i < countof(_this->iface); i++) {
		if (npppd_iface_ip_is_ready(&_this->iface[i]) &&
		    _this->iface[i].ip4addr.s_addr == ip4addr)
			return 1;
	}

	return 0;
}

/** Assign IP address. */
int
npppd_pool_assign_ip(npppd_pool *_this, npppd_ppp *ppp)
{
	int rval;
	uint32_t ip4;
	void *rtent;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_len = sizeof(struct sockaddr_in)
	}, mask = {
		.sin_family = AF_INET,
		.sin_len = sizeof(struct sockaddr_in),
	};
	struct sockaddr_npppd *snp;

	ip4 = ntohl(ppp->ppp_framed_ip_address.s_addr);

	/* If the address contains dynamic pool address list, delete it. */
	slist_itr_first(&_this->dyna_addrs);
	while (slist_itr_has_next(&_this->dyna_addrs)) {
		if ((uintptr_t)slist_itr_next(&_this->dyna_addrs) != ip4)
			continue;
		slist_itr_remove(&_this->dyna_addrs);
		break;
	}

	addr.sin_addr = ppp->ppp_framed_ip_address;
	mask.sin_addr = ppp->ppp_framed_ip_netmask;
	addr.sin_addr.s_addr &= mask.sin_addr.s_addr;

	if (rd_delete(SA(&addr), SA(&mask), _this->npppd->rd, &rtent) == 0) {
		snp = rtent;
		/* It has duplicate address entry. change from pool to PPP. */
		NPPPD_POOL_ASSERT(snp != NULL);
		NPPPD_POOL_ASSERT(snp->snp_type != SNP_PPP);
		ppp->snp.snp_next = snp;
		NPPPD_POOL_DBG((_this, DEBUG_LEVEL_2,
		    "pool %s/32 => %s(ppp=%d)",
		    inet_ntoa(ppp->ppp_framed_ip_address), ppp->username,
		    ppp->id));
	}
	NPPPD_POOL_DBG((_this, LOG_DEBUG, "rd_insert(%s) %s",
	    inet_ntoa(addr.sin_addr), ppp->username));
	if ((rval = rd_insert((struct sockaddr *)&addr,
	    (struct sockaddr *)&mask, _this->npppd->rd, &ppp->snp)) != 0) {
		errno = rval;
		log_printf(LOG_INFO, "rd_insert(%s) failed: %m",
		    inet_ntoa(ppp->ppp_framed_ip_address));
		return 1;
	}

	return 0;
}

/** Release IP address. */
void
npppd_pool_release_ip(npppd_pool *_this, npppd_ppp *ppp)
{
	void *item;
	int rval;
	struct sockaddr_npppd *snp;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_len = sizeof(struct sockaddr_in)
	}, mask = {
		.sin_family = AF_INET,
		.sin_len = sizeof(struct sockaddr_in),
	};

	/*
	 * `_this' may be NULL.  It was gone because of a configuration change.
	 */
	if (!ppp_ip_assigned(ppp))
		return;

	addr.sin_addr = ppp->ppp_framed_ip_address;
	mask.sin_addr = ppp->ppp_framed_ip_netmask;
	addr.sin_addr.s_addr &= mask.sin_addr.s_addr;

	if ((rval = rd_delete((struct sockaddr *)&addr,
	    (struct sockaddr *)&mask, ppp->pppd->rd, &item)) != 0) {
		errno = rval;
		log_printf(LOG_INFO, "Unexpected error: "
		    "rd_delete(%s) failed: %m",
		    inet_ntoa(ppp->ppp_framed_ip_address));
	}
	snp = item;

	if (_this != NULL && ppp->assign_dynapool != 0) {
		NPPPD_POOL_ASSERT(_this == ppp->assigned_pool);
		/* return to dynamic address pool list */
		slist_add(&((npppd_pool *)ppp->assigned_pool)->dyna_addrs,
		    (void *)(uintptr_t)ntohl(
			    ppp->ppp_framed_ip_address.s_addr));
	}

	if (snp != NULL && snp->snp_next != NULL) {
		/*
		 * The radish entry is registered as a list.  Insert the next
		 * of the list to the radish tree.
		 */
		if (rd_insert(SA(&addr), SA(&mask), ppp->pppd->rd,
		    snp->snp_next) != 0) {
			log_printf(LOG_INFO, "Unexpected error: "
			    "rd_insert(%s) failed: %m",
			    inet_ntoa(ppp->ppp_framed_ip_address));
		}
		NPPPD_POOL_DBG((_this, DEBUG_LEVEL_2,
		    "pool %s/%d <= %s(ppp=%d)",
		    inet_ntoa(ppp->ppp_framed_ip_address),
		    netmask2prefixlen(ntohl(ppp->ppp_framed_ip_netmask.s_addr)),
		    ppp->username, ppp->id));
		snp->snp_next = NULL;
	}
}

/**
 * Check if specified address is assignable.
 * @return {@link ::#ADDRESS_OK} or {@link ::#ADDRESS_RESERVED} or
 * {@link ::#ADDRESS_BUSY} or {@link ::#ADDRESS_INVALID}  or
 * {@link ::#ADDRESS_OUT_OF_POOL}
 */
int
npppd_pool_get_assignability(npppd_pool *_this, uint32_t ip4addr,
    uint32_t ip4mask, struct sockaddr_npppd **psnp)
{
	struct radish *radish;
	struct sockaddr_in sin4;
	struct sockaddr_npppd *snp;

	NPPPD_POOL_ASSERT(ip4mask != 0);
	NPPPD_POOL_DBG((_this, LOG_DEBUG, "%s(%08x,%08x)", __func__, ip4addr,
	    ip4mask));

	if (netmask2prefixlen(htonl(ip4mask)) == 32) {
		if (!is_valid_host_address(ip4addr))
			return ADDRESS_INVALID;
	}

	memset(&sin4, 0, sizeof(sin4));

	sin4.sin_len = sizeof(sin4);
	sin4.sin_family = AF_INET;
	sin4.sin_addr.s_addr = htonl(ip4addr);

	if (npppd_is_ifcace_ip4addr(_this->npppd, sin4.sin_addr.s_addr))
		return ADDRESS_RESERVED;
		/* Not to assign interface address */

	if (rd_match(SA(&sin4), _this->npppd->rd, &radish)) {
		do {
			snp = radish->rd_rtent;
			if (snp->snp_type == SNP_POOL ||
			    snp->snp_type == SNP_DYN_POOL) {
				if (psnp != NULL)
					*psnp = snp;
				if (snp->snp_data_ptr == _this)
					return  ADDRESS_OK;
				else
					return ADDRESS_RESERVED;
			}
			if (snp->snp_type == SNP_PPP) {
				if (psnp != NULL)
					*psnp = snp;
				return ADDRESS_BUSY;
			}
		} while (rd_match_next(SA(&sin4), _this->npppd->rd, &radish,
		    radish));
	}

	return ADDRESS_OUT_OF_POOL;
}
/***********************************************************************
 * miscellaneous functions
 ***********************************************************************/
/**
 * Check if valid host address.
 * <pre>
 * There are some issues that it uses host address as broadcast address
 * in natural mask, so it is not correct.
 * The issue is as follows:
 * (1) BSDs treat the following packet as it is not forwarded and
 *     is received as the packet to myself.
 * (2) The issue that Windows can't use L2TP/IPsec when Windows is assigned
 *     IP address .255.</pre>
 */
static int
is_valid_host_address(uint32_t addr)
{
	if (IN_CLASSA(addr))
		return ((IN_CLASSA_HOST & addr) == 0 ||
		    (IN_CLASSA_HOST & addr) == IN_CLASSA_HOST)? 0 : 1;
	if (IN_CLASSB(addr))
		return ((IN_CLASSB_HOST & addr) == 0 ||
		    (IN_CLASSB_HOST & addr) == IN_CLASSB_HOST)? 0 : 1;
	if (IN_CLASSC(addr))
		return ((IN_CLASSC_HOST & addr) == 0 ||
		    (IN_CLASSC_HOST & addr) == IN_CLASSC_HOST)? 0 : 1;

	return 0;
}

/** Record log that begins the label based this instance. */
static int
npppd_pool_log(npppd_pool *_this, int prio, const char *fmt, ...)
{
	int status;
	char logbuf[BUFSIZ];
	va_list ap;

	/*
	 * npppd_pool_release_ip is called as _this == NULL,
	 * so it can't NPPPD_POOL_ASSERT(_this != NULL).
	 */
	va_start(ap, fmt);
	snprintf(logbuf, sizeof(logbuf), "ipcp=%s pool %s",
	    (_this == NULL)? "null" : _this->ipcp_name, fmt);
	status = vlog_printf(prio, logbuf, ap);
	va_end(ap);

	return status;
}
