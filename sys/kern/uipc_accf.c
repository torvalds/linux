/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Paycounter, Inc.
 * Copyright (c) 2005 Robert N. M. Watson
 * Author: Alfred Perlstein <alfred@paycounter.com>, <alfred@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define ACCEPT_FILTER_MOD

#include "opt_param.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/domain.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/protosw.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/queue.h>

static struct mtx accept_filter_mtx;
MTX_SYSINIT(accept_filter, &accept_filter_mtx, "accept_filter_mtx",
	MTX_DEF);
#define	ACCEPT_FILTER_LOCK()	mtx_lock(&accept_filter_mtx)
#define	ACCEPT_FILTER_UNLOCK()	mtx_unlock(&accept_filter_mtx)

static SLIST_HEAD(, accept_filter) accept_filtlsthd =
	SLIST_HEAD_INITIALIZER(accept_filtlsthd);

MALLOC_DEFINE(M_ACCF, "accf", "accept filter data");

static int unloadable = 0;

SYSCTL_NODE(_net, OID_AUTO, accf, CTLFLAG_RW, 0, "Accept filters");
SYSCTL_INT(_net_accf, OID_AUTO, unloadable, CTLFLAG_RW, &unloadable, 0,
	"Allow unload of accept filters (not recommended)");

/*
 * Must be passed a malloc'd structure so we don't explode if the kld is
 * unloaded, we leak the struct on deallocation to deal with this, but if a
 * filter is loaded with the same name as a leaked one we re-use the entry.
 */
int
accept_filt_add(struct accept_filter *filt)
{
	struct accept_filter *p;

	ACCEPT_FILTER_LOCK();
	SLIST_FOREACH(p, &accept_filtlsthd, accf_next)
		if (strcmp(p->accf_name, filt->accf_name) == 0)  {
			if (p->accf_callback != NULL) {
				ACCEPT_FILTER_UNLOCK();
				return (EEXIST);
			} else {
				p->accf_callback = filt->accf_callback;
				ACCEPT_FILTER_UNLOCK();
				free(filt, M_ACCF);
				return (0);
			}
		}
				
	if (p == NULL)
		SLIST_INSERT_HEAD(&accept_filtlsthd, filt, accf_next);
	ACCEPT_FILTER_UNLOCK();
	return (0);
}

int
accept_filt_del(char *name)
{
	struct accept_filter *p;

	p = accept_filt_get(name);
	if (p == NULL)
		return (ENOENT);

	p->accf_callback = NULL;
	return (0);
}

struct accept_filter *
accept_filt_get(char *name)
{
	struct accept_filter *p;

	ACCEPT_FILTER_LOCK();
	SLIST_FOREACH(p, &accept_filtlsthd, accf_next)
		if (strcmp(p->accf_name, name) == 0)
			break;
	ACCEPT_FILTER_UNLOCK();

	return (p);
}

int
accept_filt_generic_mod_event(module_t mod, int event, void *data)
{
	struct accept_filter *p;
	struct accept_filter *accfp = (struct accept_filter *) data;
	int error;

	switch (event) {
	case MOD_LOAD:
		p = malloc(sizeof(*p), M_ACCF, M_WAITOK);
		bcopy(accfp, p, sizeof(*p));
		error = accept_filt_add(p);
		break;

	case MOD_UNLOAD:
		/*
		 * Do not support unloading yet. we don't keep track of
		 * refcounts and unloading an accept filter callback and then
		 * having it called is a bad thing.  A simple fix would be to
		 * track the refcount in the struct accept_filter.
		 */
		if (unloadable != 0) {
			error = accept_filt_del(accfp->accf_name);
		} else
			error = EOPNOTSUPP;
		break;

	case MOD_SHUTDOWN:
		error = 0;
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

int
accept_filt_getopt(struct socket *so, struct sockopt *sopt)
{
	struct accept_filter_arg *afap;
	int error;

	error = 0;
	afap = malloc(sizeof(*afap), M_TEMP, M_WAITOK | M_ZERO);
	SOCK_LOCK(so);
	if ((so->so_options & SO_ACCEPTCONN) == 0) {
		error = EINVAL;
		goto out;
	}
	if (so->sol_accept_filter == NULL) {
		error = EINVAL;
		goto out;
	}
	strcpy(afap->af_name, so->sol_accept_filter->accf_name);
	if (so->sol_accept_filter_str != NULL)
		strcpy(afap->af_arg, so->sol_accept_filter_str);
out:
	SOCK_UNLOCK(so);
	if (error == 0)
		error = sooptcopyout(sopt, afap, sizeof(*afap));
	free(afap, M_TEMP);
	return (error);
}

int
accept_filt_setopt(struct socket *so, struct sockopt *sopt)
{
	struct accept_filter_arg *afap;
	struct accept_filter *afp;
	char *accept_filter_str = NULL;
	void *accept_filter_arg = NULL;
	int error;

	/*
	 * Handle the simple delete case first.
	 */
	if (sopt == NULL || sopt->sopt_val == NULL) {
		struct socket *sp, *sp1;
		int wakeup;

		SOCK_LOCK(so);
		if ((so->so_options & SO_ACCEPTCONN) == 0) {
			SOCK_UNLOCK(so);
			return (EINVAL);
		}
		if (so->sol_accept_filter == NULL) {
			SOCK_UNLOCK(so);
			return (0);
		}
		if (so->sol_accept_filter->accf_destroy != NULL)
			so->sol_accept_filter->accf_destroy(so);
		if (so->sol_accept_filter_str != NULL)
			free(so->sol_accept_filter_str, M_ACCF);
		so->sol_accept_filter = NULL;
		so->sol_accept_filter_arg = NULL;
		so->sol_accept_filter_str = NULL;
		so->so_options &= ~SO_ACCEPTFILTER;

		/*
		 * Move from incomplete queue to complete only those
		 * connections, that are blocked by us.
		 */
		wakeup = 0;
		TAILQ_FOREACH_SAFE(sp, &so->sol_incomp, so_list, sp1) {
			SOCK_LOCK(sp);
			if (sp->so_options & SO_ACCEPTFILTER) {
				TAILQ_REMOVE(&so->sol_incomp, sp, so_list);
				TAILQ_INSERT_TAIL(&so->sol_comp, sp, so_list);
				sp->so_qstate = SQ_COMP;
				sp->so_options &= ~SO_ACCEPTFILTER;
				so->sol_incqlen--;
				so->sol_qlen++;
				wakeup = 1;
			}
			SOCK_UNLOCK(sp);
		}
		if (wakeup)
			solisten_wakeup(so);  /* unlocks */
		else
			SOLISTEN_UNLOCK(so);
		return (0);
	}

	/*
	 * Pre-allocate any memory we may need later to avoid blocking at
	 * untimely moments.  This does not optimize for invalid arguments.
	 */
	afap = malloc(sizeof(*afap), M_TEMP, M_WAITOK);
	error = sooptcopyin(sopt, afap, sizeof *afap, sizeof *afap);
	afap->af_name[sizeof(afap->af_name)-1] = '\0';
	afap->af_arg[sizeof(afap->af_arg)-1] = '\0';
	if (error) {
		free(afap, M_TEMP);
		return (error);
	}
	afp = accept_filt_get(afap->af_name);
	if (afp == NULL) {
		free(afap, M_TEMP);
		return (ENOENT);
	}
	if (afp->accf_create != NULL && afap->af_name[0] != '\0') {
		size_t len = strlen(afap->af_name) + 1;
		accept_filter_str = malloc(len, M_ACCF, M_WAITOK);
		strcpy(accept_filter_str, afap->af_name);
	}

	/*
	 * Require a listen socket; don't try to replace an existing filter
	 * without first removing it.
	 */
	SOCK_LOCK(so);
	if ((so->so_options & SO_ACCEPTCONN) == 0 ||
	    so->sol_accept_filter != NULL) {
		error = EINVAL;
		goto out;
	}

	/*
	 * Invoke the accf_create() method of the filter if required.  The
	 * socket mutex is held over this call, so create methods for filters
	 * can't block.
	 */
	if (afp->accf_create != NULL) {
		accept_filter_arg = afp->accf_create(so, afap->af_arg);
		if (accept_filter_arg == NULL) {
			error = EINVAL;
			goto out;
		}
	}
	so->sol_accept_filter = afp;
	so->sol_accept_filter_arg = accept_filter_arg;
	so->sol_accept_filter_str = accept_filter_str;
	so->so_options |= SO_ACCEPTFILTER;
out:
	SOCK_UNLOCK(so);
	if (accept_filter_str != NULL)
		free(accept_filter_str, M_ACCF);
	free(afap, M_TEMP);
	return (error);
}
