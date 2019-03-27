/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Karels at Berkeley Software Design, Inc.
 *
 * Quite extensively rewritten by Poul-Henning Kamp of the FreeBSD
 * project, to make these variables more userfriendly.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_sysctl.c	8.4 (Berkeley) 4/14/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/fail.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rmlock.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/sysproto.h>
#include <sys/uio.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <net/vnet.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>

static MALLOC_DEFINE(M_SYSCTL, "sysctl", "sysctl internal magic");
static MALLOC_DEFINE(M_SYSCTLOID, "sysctloid", "sysctl dynamic oids");
static MALLOC_DEFINE(M_SYSCTLTMP, "sysctltmp", "sysctl temp output buffer");

/*
 * The sysctllock protects the MIB tree.  It also protects sysctl
 * contexts used with dynamic sysctls.  The sysctl_register_oid() and
 * sysctl_unregister_oid() routines require the sysctllock to already
 * be held, so the sysctl_wlock() and sysctl_wunlock() routines are
 * provided for the few places in the kernel which need to use that
 * API rather than using the dynamic API.  Use of the dynamic API is
 * strongly encouraged for most code.
 *
 * The sysctlmemlock is used to limit the amount of user memory wired for
 * sysctl requests.  This is implemented by serializing any userland
 * sysctl requests larger than a single page via an exclusive lock.
 */
static struct rmlock sysctllock;
static struct sx __exclusive_cache_line sysctlmemlock;

#define	SYSCTL_WLOCK()		rm_wlock(&sysctllock)
#define	SYSCTL_WUNLOCK()	rm_wunlock(&sysctllock)
#define	SYSCTL_RLOCK(tracker)	rm_rlock(&sysctllock, (tracker))
#define	SYSCTL_RUNLOCK(tracker)	rm_runlock(&sysctllock, (tracker))
#define	SYSCTL_WLOCKED()	rm_wowned(&sysctllock)
#define	SYSCTL_ASSERT_LOCKED()	rm_assert(&sysctllock, RA_LOCKED)
#define	SYSCTL_ASSERT_WLOCKED()	rm_assert(&sysctllock, RA_WLOCKED)
#define	SYSCTL_ASSERT_RLOCKED()	rm_assert(&sysctllock, RA_RLOCKED)
#define	SYSCTL_INIT()		rm_init_flags(&sysctllock, "sysctl lock", \
				    RM_SLEEPABLE)
#define	SYSCTL_SLEEP(ch, wmesg, timo)					\
				rm_sleep(ch, &sysctllock, 0, wmesg, timo)

static int sysctl_root(SYSCTL_HANDLER_ARGS);

/* Root list */
struct sysctl_oid_list sysctl__children = SLIST_HEAD_INITIALIZER(&sysctl__children);

static int	sysctl_remove_oid_locked(struct sysctl_oid *oidp, int del,
		    int recurse);
static int	sysctl_old_kernel(struct sysctl_req *, const void *, size_t);
static int	sysctl_new_kernel(struct sysctl_req *, void *, size_t);

static struct sysctl_oid *
sysctl_find_oidname(const char *name, struct sysctl_oid_list *list)
{
	struct sysctl_oid *oidp;

	SYSCTL_ASSERT_LOCKED();
	SLIST_FOREACH(oidp, list, oid_link) {
		if (strcmp(oidp->oid_name, name) == 0) {
			return (oidp);
		}
	}
	return (NULL);
}

/*
 * Initialization of the MIB tree.
 *
 * Order by number in each list.
 */
void
sysctl_wlock(void)
{

	SYSCTL_WLOCK();
}

void
sysctl_wunlock(void)
{

	SYSCTL_WUNLOCK();
}

static int
sysctl_root_handler_locked(struct sysctl_oid *oid, void *arg1, intmax_t arg2,
    struct sysctl_req *req, struct rm_priotracker *tracker)
{
	int error;

	if (oid->oid_kind & CTLFLAG_DYN)
		atomic_add_int(&oid->oid_running, 1);

	if (tracker != NULL)
		SYSCTL_RUNLOCK(tracker);
	else
		SYSCTL_WUNLOCK();

	if (!(oid->oid_kind & CTLFLAG_MPSAFE))
		mtx_lock(&Giant);
	error = oid->oid_handler(oid, arg1, arg2, req);
	if (!(oid->oid_kind & CTLFLAG_MPSAFE))
		mtx_unlock(&Giant);

	KFAIL_POINT_ERROR(_debug_fail_point, sysctl_running, error);

	if (tracker != NULL)
		SYSCTL_RLOCK(tracker);
	else
		SYSCTL_WLOCK();

	if (oid->oid_kind & CTLFLAG_DYN) {
		if (atomic_fetchadd_int(&oid->oid_running, -1) == 1 &&
		    (oid->oid_kind & CTLFLAG_DYING) != 0)
			wakeup(&oid->oid_running);
	}

	return (error);
}

static void
sysctl_load_tunable_by_oid_locked(struct sysctl_oid *oidp)
{
	struct sysctl_req req;
	struct sysctl_oid *curr;
	char *penv = NULL;
	char path[96];
	ssize_t rem = sizeof(path);
	ssize_t len;
	uint8_t data[512] __aligned(sizeof(uint64_t));
	int size;
	int error;

	path[--rem] = 0;

	for (curr = oidp; curr != NULL; curr = SYSCTL_PARENT(curr)) {
		len = strlen(curr->oid_name);
		rem -= len;
		if (curr != oidp)
			rem -= 1;
		if (rem < 0) {
			printf("OID path exceeds %d bytes\n", (int)sizeof(path));
			return;
		}
		memcpy(path + rem, curr->oid_name, len);
		if (curr != oidp)
			path[rem + len] = '.';
	}

	memset(&req, 0, sizeof(req));

	req.td = curthread;
	req.oldfunc = sysctl_old_kernel;
	req.newfunc = sysctl_new_kernel;
	req.lock = REQ_UNWIRED;

	switch (oidp->oid_kind & CTLTYPE) {
	case CTLTYPE_INT:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(int), GETENV_SIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_UINT:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(int), GETENV_UNSIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_LONG:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(long), GETENV_SIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_ULONG:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(long), GETENV_UNSIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_S8:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(int8_t), GETENV_SIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_S16:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(int16_t), GETENV_SIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_S32:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(int32_t), GETENV_SIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_S64:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(int64_t), GETENV_SIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_U8:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(uint8_t), GETENV_UNSIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_U16:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(uint16_t), GETENV_UNSIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_U32:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(uint32_t), GETENV_UNSIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_U64:
		if (getenv_array(path + rem, data, sizeof(data), &size,
		    sizeof(uint64_t), GETENV_UNSIGNED) == 0)
			return;
		req.newlen = size;
		req.newptr = data;
		break;
	case CTLTYPE_STRING:
		penv = kern_getenv(path + rem);
		if (penv == NULL)
			return;
		req.newlen = strlen(penv);
		req.newptr = penv;
		break;
	default:
		return;
	}
	error = sysctl_root_handler_locked(oidp, oidp->oid_arg1,
	    oidp->oid_arg2, &req, NULL);
	if (error != 0)
		printf("Setting sysctl %s failed: %d\n", path + rem, error);
	if (penv != NULL)
		freeenv(penv);
}

static int
sbuf_printf_drain(void *arg __unused, const char *data, int len)
{

	return (printf("%.*s", len, data));
}

/*
 * Locate the path to a given oid.  Returns the length of the resulting path,
 * or -1 if the oid was not found.  nodes must have room for CTL_MAXNAME
 * elements and be NULL initialized.
 */
static int
sysctl_search_oid(struct sysctl_oid **nodes, struct sysctl_oid *needle)
{
	int indx;

	SYSCTL_ASSERT_LOCKED();
	indx = 0;
	while (indx < CTL_MAXNAME && indx >= 0) {
		if (nodes[indx] == NULL && indx == 0)
			nodes[indx] = SLIST_FIRST(&sysctl__children);
		else if (nodes[indx] == NULL)
			nodes[indx] = SLIST_FIRST(&nodes[indx - 1]->oid_children);
		else
			nodes[indx] = SLIST_NEXT(nodes[indx], oid_link);

		if (nodes[indx] == needle)
			return (indx + 1);

		if (nodes[indx] == NULL) {
			indx--;
			continue;
		}

		if ((nodes[indx]->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
			indx++;
			continue;
		}
	}
	return (-1);
}

static void
sysctl_warn_reuse(const char *func, struct sysctl_oid *leaf)
{
	struct sysctl_oid *nodes[CTL_MAXNAME];
	char buf[128];
	struct sbuf sb;
	int rc, i;

	(void)sbuf_new(&sb, buf, sizeof(buf), SBUF_FIXEDLEN | SBUF_INCLUDENUL);
	sbuf_set_drain(&sb, sbuf_printf_drain, NULL);

	sbuf_printf(&sb, "%s: can't re-use a leaf (", __func__);

	memset(nodes, 0, sizeof(nodes));
	rc = sysctl_search_oid(nodes, leaf);
	if (rc > 0) {
		for (i = 0; i < rc; i++)
			sbuf_printf(&sb, "%s%.*s", nodes[i]->oid_name,
			    i != (rc - 1), ".");
	} else {
		sbuf_printf(&sb, "%s", leaf->oid_name);
	}
	sbuf_printf(&sb, ")!\n");

	(void)sbuf_finish(&sb);
}

#ifdef SYSCTL_DEBUG
static int
sysctl_reuse_test(SYSCTL_HANDLER_ARGS)
{
	struct rm_priotracker tracker;

	SYSCTL_RLOCK(&tracker);
	sysctl_warn_reuse(__func__, oidp);
	SYSCTL_RUNLOCK(&tracker);
	return (0);
}
SYSCTL_PROC(_sysctl, 0, reuse_test, CTLTYPE_STRING|CTLFLAG_RD|CTLFLAG_MPSAFE,
	0, 0, sysctl_reuse_test, "-", "");
#endif

void
sysctl_register_oid(struct sysctl_oid *oidp)
{
	struct sysctl_oid_list *parent = oidp->oid_parent;
	struct sysctl_oid *p;
	struct sysctl_oid *q;
	int oid_number;
	int timeout = 2;

	/*
	 * First check if another oid with the same name already
	 * exists in the parent's list.
	 */
	SYSCTL_ASSERT_WLOCKED();
	p = sysctl_find_oidname(oidp->oid_name, parent);
	if (p != NULL) {
		if ((p->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
			p->oid_refcnt++;
			return;
		} else {
			sysctl_warn_reuse(__func__, p);
			return;
		}
	}
	/* get current OID number */
	oid_number = oidp->oid_number;

#if (OID_AUTO >= 0)
#error "OID_AUTO is expected to be a negative value"
#endif	
	/*
	 * Any negative OID number qualifies as OID_AUTO. Valid OID
	 * numbers should always be positive.
	 *
	 * NOTE: DO NOT change the starting value here, change it in
	 * <sys/sysctl.h>, and make sure it is at least 256 to
	 * accommodate e.g. net.inet.raw as a static sysctl node.
	 */
	if (oid_number < 0) {
		static int newoid;

		/*
		 * By decrementing the next OID number we spend less
		 * time inserting the OIDs into a sorted list.
		 */
		if (--newoid < CTL_AUTO_START)
			newoid = 0x7fffffff;

		oid_number = newoid;
	}

	/*
	 * Insert the OID into the parent's list sorted by OID number.
	 */
retry:
	q = NULL;
	SLIST_FOREACH(p, parent, oid_link) {
		/* check if the current OID number is in use */
		if (oid_number == p->oid_number) {
			/* get the next valid OID number */
			if (oid_number < CTL_AUTO_START ||
			    oid_number == 0x7fffffff) {
				/* wraparound - restart */
				oid_number = CTL_AUTO_START;
				/* don't loop forever */
				if (!timeout--)
					panic("sysctl: Out of OID numbers\n");
				goto retry;
			} else {
				oid_number++;
			}
		} else if (oid_number < p->oid_number)
			break;
		q = p;
	}
	/* check for non-auto OID number collision */
	if (oidp->oid_number >= 0 && oidp->oid_number < CTL_AUTO_START &&
	    oid_number >= CTL_AUTO_START) {
		printf("sysctl: OID number(%d) is already in use for '%s'\n",
		    oidp->oid_number, oidp->oid_name);
	}
	/* update the OID number, if any */
	oidp->oid_number = oid_number;
	if (q != NULL)
		SLIST_INSERT_AFTER(q, oidp, oid_link);
	else
		SLIST_INSERT_HEAD(parent, oidp, oid_link);

	if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE &&
#ifdef VIMAGE
	    (oidp->oid_kind & CTLFLAG_VNET) == 0 &&
#endif
	    (oidp->oid_kind & CTLFLAG_TUN) != 0 &&
	    (oidp->oid_kind & CTLFLAG_NOFETCH) == 0) {
		/* only fetch value once */
		oidp->oid_kind |= CTLFLAG_NOFETCH;
		/* try to fetch value from kernel environment */
		sysctl_load_tunable_by_oid_locked(oidp);
	}
}

void
sysctl_register_disabled_oid(struct sysctl_oid *oidp)
{

	/*
	 * Mark the leaf as dormant if it's not to be immediately enabled.
	 * We do not disable nodes as they can be shared between modules
	 * and it is always safe to access a node.
	 */
	KASSERT((oidp->oid_kind & CTLFLAG_DORMANT) == 0,
	    ("internal flag is set in oid_kind"));
	if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE)
		oidp->oid_kind |= CTLFLAG_DORMANT;
	sysctl_register_oid(oidp);
}

void
sysctl_enable_oid(struct sysctl_oid *oidp)
{

	SYSCTL_ASSERT_WLOCKED();
	if ((oidp->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
		KASSERT((oidp->oid_kind & CTLFLAG_DORMANT) == 0,
		    ("sysctl node is marked as dormant"));
		return;
	}
	KASSERT((oidp->oid_kind & CTLFLAG_DORMANT) != 0,
	    ("enabling already enabled sysctl oid"));
	oidp->oid_kind &= ~CTLFLAG_DORMANT;
}

void
sysctl_unregister_oid(struct sysctl_oid *oidp)
{
	struct sysctl_oid *p;
	int error;

	SYSCTL_ASSERT_WLOCKED();
	if (oidp->oid_number == OID_AUTO) {
		error = EINVAL;
	} else {
		error = ENOENT;
		SLIST_FOREACH(p, oidp->oid_parent, oid_link) {
			if (p == oidp) {
				SLIST_REMOVE(oidp->oid_parent, oidp,
				    sysctl_oid, oid_link);
				error = 0;
				break;
			}
		}
	}

	/* 
	 * This can happen when a module fails to register and is
	 * being unloaded afterwards.  It should not be a panic()
	 * for normal use.
	 */
	if (error) {
		printf("%s: failed(%d) to unregister sysctl(%s)\n",
		    __func__, error, oidp->oid_name);
	}
}

/* Initialize a new context to keep track of dynamically added sysctls. */
int
sysctl_ctx_init(struct sysctl_ctx_list *c)
{

	if (c == NULL) {
		return (EINVAL);
	}

	/*
	 * No locking here, the caller is responsible for not adding
	 * new nodes to a context until after this function has
	 * returned.
	 */
	TAILQ_INIT(c);
	return (0);
}

/* Free the context, and destroy all dynamic oids registered in this context */
int
sysctl_ctx_free(struct sysctl_ctx_list *clist)
{
	struct sysctl_ctx_entry *e, *e1;
	int error;

	error = 0;
	/*
	 * First perform a "dry run" to check if it's ok to remove oids.
	 * XXX FIXME
	 * XXX This algorithm is a hack. But I don't know any
	 * XXX better solution for now...
	 */
	SYSCTL_WLOCK();
	TAILQ_FOREACH(e, clist, link) {
		error = sysctl_remove_oid_locked(e->entry, 0, 0);
		if (error)
			break;
	}
	/*
	 * Restore deregistered entries, either from the end,
	 * or from the place where error occurred.
	 * e contains the entry that was not unregistered
	 */
	if (error)
		e1 = TAILQ_PREV(e, sysctl_ctx_list, link);
	else
		e1 = TAILQ_LAST(clist, sysctl_ctx_list);
	while (e1 != NULL) {
		sysctl_register_oid(e1->entry);
		e1 = TAILQ_PREV(e1, sysctl_ctx_list, link);
	}
	if (error) {
		SYSCTL_WUNLOCK();
		return(EBUSY);
	}
	/* Now really delete the entries */
	e = TAILQ_FIRST(clist);
	while (e != NULL) {
		e1 = TAILQ_NEXT(e, link);
		error = sysctl_remove_oid_locked(e->entry, 1, 0);
		if (error)
			panic("sysctl_remove_oid: corrupt tree, entry: %s",
			    e->entry->oid_name);
		free(e, M_SYSCTLOID);
		e = e1;
	}
	SYSCTL_WUNLOCK();
	return (error);
}

/* Add an entry to the context */
struct sysctl_ctx_entry *
sysctl_ctx_entry_add(struct sysctl_ctx_list *clist, struct sysctl_oid *oidp)
{
	struct sysctl_ctx_entry *e;

	SYSCTL_ASSERT_WLOCKED();
	if (clist == NULL || oidp == NULL)
		return(NULL);
	e = malloc(sizeof(struct sysctl_ctx_entry), M_SYSCTLOID, M_WAITOK);
	e->entry = oidp;
	TAILQ_INSERT_HEAD(clist, e, link);
	return (e);
}

/* Find an entry in the context */
struct sysctl_ctx_entry *
sysctl_ctx_entry_find(struct sysctl_ctx_list *clist, struct sysctl_oid *oidp)
{
	struct sysctl_ctx_entry *e;

	SYSCTL_ASSERT_WLOCKED();
	if (clist == NULL || oidp == NULL)
		return(NULL);
	TAILQ_FOREACH(e, clist, link) {
		if(e->entry == oidp)
			return(e);
	}
	return (e);
}

/*
 * Delete an entry from the context.
 * NOTE: this function doesn't free oidp! You have to remove it
 * with sysctl_remove_oid().
 */
int
sysctl_ctx_entry_del(struct sysctl_ctx_list *clist, struct sysctl_oid *oidp)
{
	struct sysctl_ctx_entry *e;

	if (clist == NULL || oidp == NULL)
		return (EINVAL);
	SYSCTL_WLOCK();
	e = sysctl_ctx_entry_find(clist, oidp);
	if (e != NULL) {
		TAILQ_REMOVE(clist, e, link);
		SYSCTL_WUNLOCK();
		free(e, M_SYSCTLOID);
		return (0);
	} else {
		SYSCTL_WUNLOCK();
		return (ENOENT);
	}
}

/*
 * Remove dynamically created sysctl trees.
 * oidp - top of the tree to be removed
 * del - if 0 - just deregister, otherwise free up entries as well
 * recurse - if != 0 traverse the subtree to be deleted
 */
int
sysctl_remove_oid(struct sysctl_oid *oidp, int del, int recurse)
{
	int error;

	SYSCTL_WLOCK();
	error = sysctl_remove_oid_locked(oidp, del, recurse);
	SYSCTL_WUNLOCK();
	return (error);
}

int
sysctl_remove_name(struct sysctl_oid *parent, const char *name,
    int del, int recurse)
{
	struct sysctl_oid *p, *tmp;
	int error;

	error = ENOENT;
	SYSCTL_WLOCK();
	SLIST_FOREACH_SAFE(p, SYSCTL_CHILDREN(parent), oid_link, tmp) {
		if (strcmp(p->oid_name, name) == 0) {
			error = sysctl_remove_oid_locked(p, del, recurse);
			break;
		}
	}
	SYSCTL_WUNLOCK();

	return (error);
}


static int
sysctl_remove_oid_locked(struct sysctl_oid *oidp, int del, int recurse)
{
	struct sysctl_oid *p, *tmp;
	int error;

	SYSCTL_ASSERT_WLOCKED();
	if (oidp == NULL)
		return(EINVAL);
	if ((oidp->oid_kind & CTLFLAG_DYN) == 0) {
		printf("Warning: can't remove non-dynamic nodes (%s)!\n",
		    oidp->oid_name);
		return (EINVAL);
	}
	/*
	 * WARNING: normal method to do this should be through
	 * sysctl_ctx_free(). Use recursing as the last resort
	 * method to purge your sysctl tree of leftovers...
	 * However, if some other code still references these nodes,
	 * it will panic.
	 */
	if ((oidp->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
		if (oidp->oid_refcnt == 1) {
			SLIST_FOREACH_SAFE(p,
			    SYSCTL_CHILDREN(oidp), oid_link, tmp) {
				if (!recurse) {
					printf("Warning: failed attempt to "
					    "remove oid %s with child %s\n",
					    oidp->oid_name, p->oid_name);
					return (ENOTEMPTY);
				}
				error = sysctl_remove_oid_locked(p, del,
				    recurse);
				if (error)
					return (error);
			}
		}
	}
	if (oidp->oid_refcnt > 1 ) {
		oidp->oid_refcnt--;
	} else {
		if (oidp->oid_refcnt == 0) {
			printf("Warning: bad oid_refcnt=%u (%s)!\n",
				oidp->oid_refcnt, oidp->oid_name);
			return (EINVAL);
		}
		sysctl_unregister_oid(oidp);
		if (del) {
			/*
			 * Wait for all threads running the handler to drain.
			 * This preserves the previous behavior when the
			 * sysctl lock was held across a handler invocation,
			 * and is necessary for module unload correctness.
			 */
			while (oidp->oid_running > 0) {
				oidp->oid_kind |= CTLFLAG_DYING;
				SYSCTL_SLEEP(&oidp->oid_running, "oidrm", 0);
			}
			if (oidp->oid_descr)
				free(__DECONST(char *, oidp->oid_descr),
				    M_SYSCTLOID);
			if (oidp->oid_label)
				free(__DECONST(char *, oidp->oid_label),
				    M_SYSCTLOID);
			free(__DECONST(char *, oidp->oid_name), M_SYSCTLOID);
			free(oidp, M_SYSCTLOID);
		}
	}
	return (0);
}
/*
 * Create new sysctls at run time.
 * clist may point to a valid context initialized with sysctl_ctx_init().
 */
struct sysctl_oid *
sysctl_add_oid(struct sysctl_ctx_list *clist, struct sysctl_oid_list *parent,
	int number, const char *name, int kind, void *arg1, intmax_t arg2,
	int (*handler)(SYSCTL_HANDLER_ARGS), const char *fmt, const char *descr,
	const char *label)
{
	struct sysctl_oid *oidp;

	/* You have to hook up somewhere.. */
	if (parent == NULL)
		return(NULL);
	/* Check if the node already exists, otherwise create it */
	SYSCTL_WLOCK();
	oidp = sysctl_find_oidname(name, parent);
	if (oidp != NULL) {
		if ((oidp->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
			oidp->oid_refcnt++;
			/* Update the context */
			if (clist != NULL)
				sysctl_ctx_entry_add(clist, oidp);
			SYSCTL_WUNLOCK();
			return (oidp);
		} else {
			sysctl_warn_reuse(__func__, oidp);
			SYSCTL_WUNLOCK();
			return (NULL);
		}
	}
	oidp = malloc(sizeof(struct sysctl_oid), M_SYSCTLOID, M_WAITOK|M_ZERO);
	oidp->oid_parent = parent;
	SLIST_INIT(&oidp->oid_children);
	oidp->oid_number = number;
	oidp->oid_refcnt = 1;
	oidp->oid_name = strdup(name, M_SYSCTLOID);
	oidp->oid_handler = handler;
	oidp->oid_kind = CTLFLAG_DYN | kind;
	oidp->oid_arg1 = arg1;
	oidp->oid_arg2 = arg2;
	oidp->oid_fmt = fmt;
	if (descr != NULL)
		oidp->oid_descr = strdup(descr, M_SYSCTLOID);
	if (label != NULL)
		oidp->oid_label = strdup(label, M_SYSCTLOID);
	/* Update the context, if used */
	if (clist != NULL)
		sysctl_ctx_entry_add(clist, oidp);
	/* Register this oid */
	sysctl_register_oid(oidp);
	SYSCTL_WUNLOCK();
	return (oidp);
}

/*
 * Rename an existing oid.
 */
void
sysctl_rename_oid(struct sysctl_oid *oidp, const char *name)
{
	char *newname;
	char *oldname;

	newname = strdup(name, M_SYSCTLOID);
	SYSCTL_WLOCK();
	oldname = __DECONST(char *, oidp->oid_name);
	oidp->oid_name = newname;
	SYSCTL_WUNLOCK();
	free(oldname, M_SYSCTLOID);
}

/*
 * Reparent an existing oid.
 */
int
sysctl_move_oid(struct sysctl_oid *oid, struct sysctl_oid_list *parent)
{
	struct sysctl_oid *oidp;

	SYSCTL_WLOCK();
	if (oid->oid_parent == parent) {
		SYSCTL_WUNLOCK();
		return (0);
	}
	oidp = sysctl_find_oidname(oid->oid_name, parent);
	if (oidp != NULL) {
		SYSCTL_WUNLOCK();
		return (EEXIST);
	}
	sysctl_unregister_oid(oid);
	oid->oid_parent = parent;
	oid->oid_number = OID_AUTO;
	sysctl_register_oid(oid);
	SYSCTL_WUNLOCK();
	return (0);
}

/*
 * Register the kernel's oids on startup.
 */
SET_DECLARE(sysctl_set, struct sysctl_oid);

static void
sysctl_register_all(void *arg)
{
	struct sysctl_oid **oidp;

	sx_init(&sysctlmemlock, "sysctl mem");
	SYSCTL_INIT();
	SYSCTL_WLOCK();
	SET_FOREACH(oidp, sysctl_set)
		sysctl_register_oid(*oidp);
	SYSCTL_WUNLOCK();
}
SYSINIT(sysctl, SI_SUB_KMEM, SI_ORDER_FIRST, sysctl_register_all, NULL);

/*
 * "Staff-functions"
 *
 * These functions implement a presently undocumented interface 
 * used by the sysctl program to walk the tree, and get the type
 * so it can print the value.
 * This interface is under work and consideration, and should probably
 * be killed with a big axe by the first person who can find the time.
 * (be aware though, that the proper interface isn't as obvious as it
 * may seem, there are various conflicting requirements.
 *
 * {0,0}	printf the entire MIB-tree.
 * {0,1,...}	return the name of the "..." OID.
 * {0,2,...}	return the next OID.
 * {0,3}	return the OID of the name in "new"
 * {0,4,...}	return the kind & format info for the "..." OID.
 * {0,5,...}	return the description of the "..." OID.
 * {0,6,...}	return the aggregation label of the "..." OID.
 */

#ifdef SYSCTL_DEBUG
static void
sysctl_sysctl_debug_dump_node(struct sysctl_oid_list *l, int i)
{
	int k;
	struct sysctl_oid *oidp;

	SYSCTL_ASSERT_LOCKED();
	SLIST_FOREACH(oidp, l, oid_link) {

		for (k=0; k<i; k++)
			printf(" ");

		printf("%d %s ", oidp->oid_number, oidp->oid_name);

		printf("%c%c",
			oidp->oid_kind & CTLFLAG_RD ? 'R':' ',
			oidp->oid_kind & CTLFLAG_WR ? 'W':' ');

		if (oidp->oid_handler)
			printf(" *Handler");

		switch (oidp->oid_kind & CTLTYPE) {
			case CTLTYPE_NODE:
				printf(" Node\n");
				if (!oidp->oid_handler) {
					sysctl_sysctl_debug_dump_node(
					    SYSCTL_CHILDREN(oidp), i + 2);
				}
				break;
			case CTLTYPE_INT:    printf(" Int\n"); break;
			case CTLTYPE_UINT:   printf(" u_int\n"); break;
			case CTLTYPE_LONG:   printf(" Long\n"); break;
			case CTLTYPE_ULONG:  printf(" u_long\n"); break;
			case CTLTYPE_STRING: printf(" String\n"); break;
			case CTLTYPE_S8:     printf(" int8_t\n"); break;
			case CTLTYPE_S16:    printf(" int16_t\n"); break;
			case CTLTYPE_S32:    printf(" int32_t\n"); break;
			case CTLTYPE_S64:    printf(" int64_t\n"); break;
			case CTLTYPE_U8:     printf(" uint8_t\n"); break;
			case CTLTYPE_U16:    printf(" uint16_t\n"); break;
			case CTLTYPE_U32:    printf(" uint32_t\n"); break;
			case CTLTYPE_U64:    printf(" uint64_t\n"); break;
			case CTLTYPE_OPAQUE: printf(" Opaque/struct\n"); break;
			default:	     printf("\n");
		}

	}
}

static int
sysctl_sysctl_debug(SYSCTL_HANDLER_ARGS)
{
	struct rm_priotracker tracker;
	int error;

	error = priv_check(req->td, PRIV_SYSCTL_DEBUG);
	if (error)
		return (error);
	SYSCTL_RLOCK(&tracker);
	sysctl_sysctl_debug_dump_node(&sysctl__children, 0);
	SYSCTL_RUNLOCK(&tracker);
	return (ENOENT);
}

SYSCTL_PROC(_sysctl, 0, debug, CTLTYPE_STRING|CTLFLAG_RD|CTLFLAG_MPSAFE,
	0, 0, sysctl_sysctl_debug, "-", "");
#endif

static int
sysctl_sysctl_name(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *) arg1;
	u_int namelen = arg2;
	int error = 0;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *lsp = &sysctl__children, *lsp2;
	struct rm_priotracker tracker;
	char buf[10];

	SYSCTL_RLOCK(&tracker);
	while (namelen) {
		if (!lsp) {
			snprintf(buf,sizeof(buf),"%d",*name);
			if (req->oldidx)
				error = SYSCTL_OUT(req, ".", 1);
			if (!error)
				error = SYSCTL_OUT(req, buf, strlen(buf));
			if (error)
				goto out;
			namelen--;
			name++;
			continue;
		}
		lsp2 = NULL;
		SLIST_FOREACH(oid, lsp, oid_link) {
			if (oid->oid_number != *name)
				continue;

			if (req->oldidx)
				error = SYSCTL_OUT(req, ".", 1);
			if (!error)
				error = SYSCTL_OUT(req, oid->oid_name,
					strlen(oid->oid_name));
			if (error)
				goto out;

			namelen--;
			name++;

			if ((oid->oid_kind & CTLTYPE) != CTLTYPE_NODE) 
				break;

			if (oid->oid_handler)
				break;

			lsp2 = SYSCTL_CHILDREN(oid);
			break;
		}
		lsp = lsp2;
	}
	error = SYSCTL_OUT(req, "", 1);
 out:
	SYSCTL_RUNLOCK(&tracker);
	return (error);
}

/*
 * XXXRW/JA: Shouldn't return name data for nodes that we don't permit in
 * capability mode.
 */
static SYSCTL_NODE(_sysctl, 1, name, CTLFLAG_RD | CTLFLAG_MPSAFE | CTLFLAG_CAPRD,
    sysctl_sysctl_name, "");

static int
sysctl_sysctl_next_ls(struct sysctl_oid_list *lsp, int *name, u_int namelen, 
	int *next, int *len, int level, struct sysctl_oid **oidpp)
{
	struct sysctl_oid *oidp;

	SYSCTL_ASSERT_LOCKED();
	*len = level;
	SLIST_FOREACH(oidp, lsp, oid_link) {
		*next = oidp->oid_number;
		*oidpp = oidp;

		if ((oidp->oid_kind & (CTLFLAG_SKIP | CTLFLAG_DORMANT)) != 0)
			continue;

		if (!namelen) {
			if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE) 
				return (0);
			if (oidp->oid_handler) 
				/* We really should call the handler here...*/
				return (0);
			lsp = SYSCTL_CHILDREN(oidp);
			if (!sysctl_sysctl_next_ls(lsp, 0, 0, next+1, 
				len, level+1, oidpp))
				return (0);
			goto emptynode;
		}

		if (oidp->oid_number < *name)
			continue;

		if (oidp->oid_number > *name) {
			if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE)
				return (0);
			if (oidp->oid_handler)
				return (0);
			lsp = SYSCTL_CHILDREN(oidp);
			if (!sysctl_sysctl_next_ls(lsp, name+1, namelen-1, 
				next+1, len, level+1, oidpp))
				return (0);
			goto next;
		}
		if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE)
			continue;

		if (oidp->oid_handler)
			continue;

		lsp = SYSCTL_CHILDREN(oidp);
		if (!sysctl_sysctl_next_ls(lsp, name+1, namelen-1, next+1, 
			len, level+1, oidpp))
			return (0);
	next:
		namelen = 1;
	emptynode:
		*len = level;
	}
	return (1);
}

static int
sysctl_sysctl_next(SYSCTL_HANDLER_ARGS)
{
	int *name = (int *) arg1;
	u_int namelen = arg2;
	int i, j, error;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *lsp = &sysctl__children;
	struct rm_priotracker tracker;
	int newoid[CTL_MAXNAME];

	SYSCTL_RLOCK(&tracker);
	i = sysctl_sysctl_next_ls(lsp, name, namelen, newoid, &j, 1, &oid);
	SYSCTL_RUNLOCK(&tracker);
	if (i)
		return (ENOENT);
	error = SYSCTL_OUT(req, newoid, j * sizeof (int));
	return (error);
}

/*
 * XXXRW/JA: Shouldn't return next data for nodes that we don't permit in
 * capability mode.
 */
static SYSCTL_NODE(_sysctl, 2, next, CTLFLAG_RD | CTLFLAG_MPSAFE | CTLFLAG_CAPRD,
    sysctl_sysctl_next, "");

static int
name2oid(char *name, int *oid, int *len, struct sysctl_oid **oidpp)
{
	struct sysctl_oid *oidp;
	struct sysctl_oid_list *lsp = &sysctl__children;
	char *p;

	SYSCTL_ASSERT_LOCKED();

	for (*len = 0; *len < CTL_MAXNAME;) {
		p = strsep(&name, ".");

		oidp = SLIST_FIRST(lsp);
		for (;; oidp = SLIST_NEXT(oidp, oid_link)) {
			if (oidp == NULL)
				return (ENOENT);
			if (strcmp(p, oidp->oid_name) == 0)
				break;
		}
		*oid++ = oidp->oid_number;
		(*len)++;

		if (name == NULL || *name == '\0') {
			if (oidpp)
				*oidpp = oidp;
			return (0);
		}

		if ((oidp->oid_kind & CTLTYPE) != CTLTYPE_NODE)
			break;

		if (oidp->oid_handler)
			break;

		lsp = SYSCTL_CHILDREN(oidp);
	}
	return (ENOENT);
}

static int
sysctl_sysctl_name2oid(SYSCTL_HANDLER_ARGS)
{
	char *p;
	int error, oid[CTL_MAXNAME], len = 0;
	struct sysctl_oid *op = NULL;
	struct rm_priotracker tracker;
	char buf[32];

	if (!req->newlen) 
		return (ENOENT);
	if (req->newlen >= MAXPATHLEN)	/* XXX arbitrary, undocumented */
		return (ENAMETOOLONG);

	p = buf;
	if (req->newlen >= sizeof(buf))
		p = malloc(req->newlen+1, M_SYSCTL, M_WAITOK);

	error = SYSCTL_IN(req, p, req->newlen);
	if (error) {
		if (p != buf)
			free(p, M_SYSCTL);
		return (error);
	}

	p [req->newlen] = '\0';

	SYSCTL_RLOCK(&tracker);
	error = name2oid(p, oid, &len, &op);
	SYSCTL_RUNLOCK(&tracker);

	if (p != buf)
		free(p, M_SYSCTL);

	if (error)
		return (error);

	error = SYSCTL_OUT(req, oid, len * sizeof *oid);
	return (error);
}

/*
 * XXXRW/JA: Shouldn't return name2oid data for nodes that we don't permit in
 * capability mode.
 */
SYSCTL_PROC(_sysctl, 3, name2oid,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_ANYBODY | CTLFLAG_MPSAFE
    | CTLFLAG_CAPRW, 0, 0, sysctl_sysctl_name2oid, "I", "");

static int
sysctl_sysctl_oidfmt(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_oid *oid;
	struct rm_priotracker tracker;
	int error;

	SYSCTL_RLOCK(&tracker);
	error = sysctl_find_oid(arg1, arg2, &oid, NULL, req);
	if (error)
		goto out;

	if (oid->oid_fmt == NULL) {
		error = ENOENT;
		goto out;
	}
	error = SYSCTL_OUT(req, &oid->oid_kind, sizeof(oid->oid_kind));
	if (error)
		goto out;
	error = SYSCTL_OUT(req, oid->oid_fmt, strlen(oid->oid_fmt) + 1);
 out:
	SYSCTL_RUNLOCK(&tracker);
	return (error);
}


static SYSCTL_NODE(_sysctl, 4, oidfmt, CTLFLAG_RD|CTLFLAG_MPSAFE|CTLFLAG_CAPRD,
    sysctl_sysctl_oidfmt, "");

static int
sysctl_sysctl_oiddescr(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_oid *oid;
	struct rm_priotracker tracker;
	int error;

	SYSCTL_RLOCK(&tracker);
	error = sysctl_find_oid(arg1, arg2, &oid, NULL, req);
	if (error)
		goto out;

	if (oid->oid_descr == NULL) {
		error = ENOENT;
		goto out;
	}
	error = SYSCTL_OUT(req, oid->oid_descr, strlen(oid->oid_descr) + 1);
 out:
	SYSCTL_RUNLOCK(&tracker);
	return (error);
}

static SYSCTL_NODE(_sysctl, 5, oiddescr, CTLFLAG_RD|CTLFLAG_MPSAFE|CTLFLAG_CAPRD,
    sysctl_sysctl_oiddescr, "");

static int
sysctl_sysctl_oidlabel(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_oid *oid;
	struct rm_priotracker tracker;
	int error;

	SYSCTL_RLOCK(&tracker);
	error = sysctl_find_oid(arg1, arg2, &oid, NULL, req);
	if (error)
		goto out;

	if (oid->oid_label == NULL) {
		error = ENOENT;
		goto out;
	}
	error = SYSCTL_OUT(req, oid->oid_label, strlen(oid->oid_label) + 1);
 out:
	SYSCTL_RUNLOCK(&tracker);
	return (error);
}

static SYSCTL_NODE(_sysctl, 6, oidlabel,
    CTLFLAG_RD | CTLFLAG_MPSAFE | CTLFLAG_CAPRD, sysctl_sysctl_oidlabel, "");

/*
 * Default "handler" functions.
 */

/*
 * Handle a bool.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */

int
sysctl_handle_bool(SYSCTL_HANDLER_ARGS)
{
	uint8_t temp;
	int error;

	/*
	 * Attempt to get a coherent snapshot by making a copy of the data.
	 */
	if (arg1)
		temp = *(bool *)arg1 ? 1 : 0;
	else
		temp = arg2 ? 1 : 0;

	error = SYSCTL_OUT(req, &temp, sizeof(temp));
	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
	else {
		error = SYSCTL_IN(req, &temp, sizeof(temp));
		if (!error)
			*(bool *)arg1 = temp ? 1 : 0;
	}
	return (error);
}

/*
 * Handle an int8_t, signed or unsigned.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */

int
sysctl_handle_8(SYSCTL_HANDLER_ARGS)
{
	int8_t tmpout;
	int error = 0;

	/*
	 * Attempt to get a coherent snapshot by making a copy of the data.
	 */
	if (arg1)
		tmpout = *(int8_t *)arg1;
	else
		tmpout = arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(tmpout));

	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(tmpout));
	return (error);
}

/*
 * Handle an int16_t, signed or unsigned.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */

int
sysctl_handle_16(SYSCTL_HANDLER_ARGS)
{
	int16_t tmpout;
	int error = 0;

	/*
	 * Attempt to get a coherent snapshot by making a copy of the data.
	 */
	if (arg1)
		tmpout = *(int16_t *)arg1;
	else
		tmpout = arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(tmpout));

	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(tmpout));
	return (error);
}

/*
 * Handle an int32_t, signed or unsigned.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */

int
sysctl_handle_32(SYSCTL_HANDLER_ARGS)
{
	int32_t tmpout;
	int error = 0;

	/*
	 * Attempt to get a coherent snapshot by making a copy of the data.
	 */
	if (arg1)
		tmpout = *(int32_t *)arg1;
	else
		tmpout = arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(tmpout));

	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(tmpout));
	return (error);
}

/*
 * Handle an int, signed or unsigned.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */

int
sysctl_handle_int(SYSCTL_HANDLER_ARGS)
{
	int tmpout, error = 0;

	/*
	 * Attempt to get a coherent snapshot by making a copy of the data.
	 */
	if (arg1)
		tmpout = *(int *)arg1;
	else
		tmpout = arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(int));

	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(int));
	return (error);
}

/*
 * Based on on sysctl_handle_int() convert milliseconds into ticks.
 * Note: this is used by TCP.
 */

int
sysctl_msec_to_ticks(SYSCTL_HANDLER_ARGS)
{
	int error, s, tt;

	tt = *(int *)arg1;
	s = (int)((int64_t)tt * 1000 / hz);

	error = sysctl_handle_int(oidp, &s, 0, req);
	if (error || !req->newptr)
		return (error);

	tt = (int)((int64_t)s * hz / 1000);
	if (tt < 1)
		return (EINVAL);

	*(int *)arg1 = tt;
	return (0);
}


/*
 * Handle a long, signed or unsigned.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */

int
sysctl_handle_long(SYSCTL_HANDLER_ARGS)
{
	int error = 0;
	long tmplong;
#ifdef SCTL_MASK32
	int tmpint;
#endif

	/*
	 * Attempt to get a coherent snapshot by making a copy of the data.
	 */
	if (arg1)
		tmplong = *(long *)arg1;
	else
		tmplong = arg2;
#ifdef SCTL_MASK32
	if (req->flags & SCTL_MASK32) {
		tmpint = tmplong;
		error = SYSCTL_OUT(req, &tmpint, sizeof(int));
	} else
#endif
		error = SYSCTL_OUT(req, &tmplong, sizeof(long));

	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
#ifdef SCTL_MASK32
	else if (req->flags & SCTL_MASK32) {
		error = SYSCTL_IN(req, &tmpint, sizeof(int));
		*(long *)arg1 = (long)tmpint;
	}
#endif
	else
		error = SYSCTL_IN(req, arg1, sizeof(long));
	return (error);
}

/*
 * Handle a 64 bit int, signed or unsigned.
 * Two cases:
 *     a variable:  point arg1 at it.
 *     a constant:  pass it in arg2.
 */
int
sysctl_handle_64(SYSCTL_HANDLER_ARGS)
{
	int error = 0;
	uint64_t tmpout;

	/*
	 * Attempt to get a coherent snapshot by making a copy of the data.
	 */
	if (arg1)
		tmpout = *(uint64_t *)arg1;
	else
		tmpout = arg2;
	error = SYSCTL_OUT(req, &tmpout, sizeof(uint64_t));

	if (error || !req->newptr)
		return (error);

	if (!arg1)
		error = EPERM;
	else
		error = SYSCTL_IN(req, arg1, sizeof(uint64_t));
	return (error);
}

/*
 * Handle our generic '\0' terminated 'C' string.
 * Two cases:
 * 	a variable string:  point arg1 at it, arg2 is max length.
 * 	a constant string:  point arg1 at it, arg2 is zero.
 */

int
sysctl_handle_string(SYSCTL_HANDLER_ARGS)
{
	size_t outlen;
	int error = 0, ro_string = 0;

	/*
	 * A zero-length buffer indicates a fixed size read-only
	 * string:
	 */
	if (arg2 == 0) {
		arg2 = strlen((char *)arg1) + 1;
		ro_string = 1;
	}

	if (req->oldptr != NULL) {
		char *tmparg;

		if (ro_string) {
			tmparg = arg1;
		} else {
			/* try to make a coherent snapshot of the string */
			tmparg = malloc(arg2, M_SYSCTLTMP, M_WAITOK);
			memcpy(tmparg, arg1, arg2);
		}

		outlen = strnlen(tmparg, arg2 - 1) + 1;
		error = SYSCTL_OUT(req, tmparg, outlen);

		if (!ro_string)
			free(tmparg, M_SYSCTLTMP);
	} else {
		outlen = strnlen((char *)arg1, arg2 - 1) + 1;
		error = SYSCTL_OUT(req, NULL, outlen);
	}
	if (error || !req->newptr)
		return (error);

	if ((req->newlen - req->newidx) >= arg2) {
		error = EINVAL;
	} else {
		arg2 = (req->newlen - req->newidx);
		error = SYSCTL_IN(req, arg1, arg2);
		((char *)arg1)[arg2] = '\0';
	}
	return (error);
}

/*
 * Handle any kind of opaque data.
 * arg1 points to it, arg2 is the size.
 */

int
sysctl_handle_opaque(SYSCTL_HANDLER_ARGS)
{
	int error, tries;
	u_int generation;
	struct sysctl_req req2;

	/*
	 * Attempt to get a coherent snapshot, by using the thread
	 * pre-emption counter updated from within mi_switch() to
	 * determine if we were pre-empted during a bcopy() or
	 * copyout(). Make 3 attempts at doing this before giving up.
	 * If we encounter an error, stop immediately.
	 */
	tries = 0;
	req2 = *req;
retry:
	generation = curthread->td_generation;
	error = SYSCTL_OUT(req, arg1, arg2);
	if (error)
		return (error);
	tries++;
	if (generation != curthread->td_generation && tries < 3) {
		*req = req2;
		goto retry;
	}

	error = SYSCTL_IN(req, arg1, arg2);

	return (error);
}

/*
 * Based on on sysctl_handle_int() convert microseconds to a sbintime.
 */
int
sysctl_usec_to_sbintime(SYSCTL_HANDLER_ARGS)
{
	int error;
	int64_t tt;
	sbintime_t sb;

	tt = *(int64_t *)arg1;
	sb = sbttous(tt);

	error = sysctl_handle_64(oidp, &sb, 0, req);
	if (error || !req->newptr)
		return (error);

	tt = ustosbt(sb);
	*(int64_t *)arg1 = tt;

	return (0);
}

/*
 * Based on on sysctl_handle_int() convert milliseconds to a sbintime.
 */
int
sysctl_msec_to_sbintime(SYSCTL_HANDLER_ARGS)
{
	int error;
	int64_t tt;
	sbintime_t sb;

	tt = *(int64_t *)arg1;
	sb = sbttoms(tt);

	error = sysctl_handle_64(oidp, &sb, 0, req);
	if (error || !req->newptr)
		return (error);

	tt = mstosbt(sb);
	*(int64_t *)arg1 = tt;

	return (0);
}


/*
 * Transfer functions to/from kernel space.
 * XXX: rather untested at this point
 */
static int
sysctl_old_kernel(struct sysctl_req *req, const void *p, size_t l)
{
	size_t i = 0;

	if (req->oldptr) {
		i = l;
		if (req->oldlen <= req->oldidx)
			i = 0;
		else
			if (i > req->oldlen - req->oldidx)
				i = req->oldlen - req->oldidx;
		if (i > 0)
			bcopy(p, (char *)req->oldptr + req->oldidx, i);
	}
	req->oldidx += l;
	if (req->oldptr && i != l)
		return (ENOMEM);
	return (0);
}

static int
sysctl_new_kernel(struct sysctl_req *req, void *p, size_t l)
{
	if (!req->newptr)
		return (0);
	if (req->newlen - req->newidx < l)
		return (EINVAL);
	bcopy((const char *)req->newptr + req->newidx, p, l);
	req->newidx += l;
	return (0);
}

int
kernel_sysctl(struct thread *td, int *name, u_int namelen, void *old,
    size_t *oldlenp, void *new, size_t newlen, size_t *retval, int flags)
{
	int error = 0;
	struct sysctl_req req;

	bzero(&req, sizeof req);

	req.td = td;
	req.flags = flags;

	if (oldlenp) {
		req.oldlen = *oldlenp;
	}
	req.validlen = req.oldlen;

	if (old) {
		req.oldptr= old;
	}

	if (new != NULL) {
		req.newlen = newlen;
		req.newptr = new;
	}

	req.oldfunc = sysctl_old_kernel;
	req.newfunc = sysctl_new_kernel;
	req.lock = REQ_UNWIRED;

	error = sysctl_root(0, name, namelen, &req);

	if (req.lock == REQ_WIRED && req.validlen > 0)
		vsunlock(req.oldptr, req.validlen);

	if (error && error != ENOMEM)
		return (error);

	if (retval) {
		if (req.oldptr && req.oldidx > req.validlen)
			*retval = req.validlen;
		else
			*retval = req.oldidx;
	}
	return (error);
}

int
kernel_sysctlbyname(struct thread *td, char *name, void *old, size_t *oldlenp,
    void *new, size_t newlen, size_t *retval, int flags)
{
        int oid[CTL_MAXNAME];
        size_t oidlen, plen;
	int error;

	oid[0] = 0;		/* sysctl internal magic */
	oid[1] = 3;		/* name2oid */
	oidlen = sizeof(oid);

	error = kernel_sysctl(td, oid, 2, oid, &oidlen,
	    (void *)name, strlen(name), &plen, flags);
	if (error)
		return (error);

	error = kernel_sysctl(td, oid, plen / sizeof(int), old, oldlenp,
	    new, newlen, retval, flags);
	return (error);
}

/*
 * Transfer function to/from user space.
 */
static int
sysctl_old_user(struct sysctl_req *req, const void *p, size_t l)
{
	size_t i, len, origidx;
	int error;

	origidx = req->oldidx;
	req->oldidx += l;
	if (req->oldptr == NULL)
		return (0);
	/*
	 * If we have not wired the user supplied buffer and we are currently
	 * holding locks, drop a witness warning, as it's possible that
	 * write operations to the user page can sleep.
	 */
	if (req->lock != REQ_WIRED)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "sysctl_old_user()");
	i = l;
	len = req->validlen;
	if (len <= origidx)
		i = 0;
	else {
		if (i > len - origidx)
			i = len - origidx;
		if (req->lock == REQ_WIRED) {
			error = copyout_nofault(p, (char *)req->oldptr +
			    origidx, i);
		} else
			error = copyout(p, (char *)req->oldptr + origidx, i);
		if (error != 0)
			return (error);
	}
	if (i < l)
		return (ENOMEM);
	return (0);
}

static int
sysctl_new_user(struct sysctl_req *req, void *p, size_t l)
{
	int error;

	if (!req->newptr)
		return (0);
	if (req->newlen - req->newidx < l)
		return (EINVAL);
	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "sysctl_new_user()");
	error = copyin((const char *)req->newptr + req->newidx, p, l);
	req->newidx += l;
	return (error);
}

/*
 * Wire the user space destination buffer.  If set to a value greater than
 * zero, the len parameter limits the maximum amount of wired memory.
 */
int
sysctl_wire_old_buffer(struct sysctl_req *req, size_t len)
{
	int ret;
	size_t wiredlen;

	wiredlen = (len > 0 && len < req->oldlen) ? len : req->oldlen;
	ret = 0;
	if (req->lock != REQ_WIRED && req->oldptr &&
	    req->oldfunc == sysctl_old_user) {
		if (wiredlen != 0) {
			ret = vslock(req->oldptr, wiredlen);
			if (ret != 0) {
				if (ret != ENOMEM)
					return (ret);
				wiredlen = 0;
			}
		}
		req->lock = REQ_WIRED;
		req->validlen = wiredlen;
	}
	return (0);
}

int
sysctl_find_oid(int *name, u_int namelen, struct sysctl_oid **noid,
    int *nindx, struct sysctl_req *req)
{
	struct sysctl_oid_list *lsp;
	struct sysctl_oid *oid;
	int indx;

	SYSCTL_ASSERT_LOCKED();
	lsp = &sysctl__children;
	indx = 0;
	while (indx < CTL_MAXNAME) {
		SLIST_FOREACH(oid, lsp, oid_link) {
			if (oid->oid_number == name[indx])
				break;
		}
		if (oid == NULL)
			return (ENOENT);

		indx++;
		if ((oid->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
			if (oid->oid_handler != NULL || indx == namelen) {
				*noid = oid;
				if (nindx != NULL)
					*nindx = indx;
				KASSERT((oid->oid_kind & CTLFLAG_DYING) == 0,
				    ("%s found DYING node %p", __func__, oid));
				return (0);
			}
			lsp = SYSCTL_CHILDREN(oid);
		} else if (indx == namelen) {
			if ((oid->oid_kind & CTLFLAG_DORMANT) != 0)
				return (ENOENT);
			*noid = oid;
			if (nindx != NULL)
				*nindx = indx;
			KASSERT((oid->oid_kind & CTLFLAG_DYING) == 0,
			    ("%s found DYING node %p", __func__, oid));
			return (0);
		} else {
			return (ENOTDIR);
		}
	}
	return (ENOENT);
}

/*
 * Traverse our tree, and find the right node, execute whatever it points
 * to, and return the resulting error code.
 */

static int
sysctl_root(SYSCTL_HANDLER_ARGS)
{
	struct sysctl_oid *oid;
	struct rm_priotracker tracker;
	int error, indx, lvl;

	SYSCTL_RLOCK(&tracker);

	error = sysctl_find_oid(arg1, arg2, &oid, &indx, req);
	if (error)
		goto out;

	if ((oid->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
		/*
		 * You can't call a sysctl when it's a node, but has
		 * no handler.  Inform the user that it's a node.
		 * The indx may or may not be the same as namelen.
		 */
		if (oid->oid_handler == NULL) {
			error = EISDIR;
			goto out;
		}
	}

	/* Is this sysctl writable? */
	if (req->newptr && !(oid->oid_kind & CTLFLAG_WR)) {
		error = EPERM;
		goto out;
	}

	KASSERT(req->td != NULL, ("sysctl_root(): req->td == NULL"));

#ifdef CAPABILITY_MODE
	/*
	 * If the process is in capability mode, then don't permit reading or
	 * writing unless specifically granted for the node.
	 */
	if (IN_CAPABILITY_MODE(req->td)) {
		if ((req->oldptr && !(oid->oid_kind & CTLFLAG_CAPRD)) ||
		    (req->newptr && !(oid->oid_kind & CTLFLAG_CAPWR))) {
			error = EPERM;
			goto out;
		}
	}
#endif

	/* Is this sysctl sensitive to securelevels? */
	if (req->newptr && (oid->oid_kind & CTLFLAG_SECURE)) {
		lvl = (oid->oid_kind & CTLMASK_SECURE) >> CTLSHIFT_SECURE;
		error = securelevel_gt(req->td->td_ucred, lvl);
		if (error)
			goto out;
	}

	/* Is this sysctl writable by only privileged users? */
	if (req->newptr && !(oid->oid_kind & CTLFLAG_ANYBODY)) {
		int priv;

		if (oid->oid_kind & CTLFLAG_PRISON)
			priv = PRIV_SYSCTL_WRITEJAIL;
#ifdef VIMAGE
		else if ((oid->oid_kind & CTLFLAG_VNET) &&
		     prison_owns_vnet(req->td->td_ucred))
			priv = PRIV_SYSCTL_WRITEJAIL;
#endif
		else
			priv = PRIV_SYSCTL_WRITE;
		error = priv_check(req->td, priv);
		if (error)
			goto out;
	}

	if (!oid->oid_handler) {
		error = EINVAL;
		goto out;
	}

	if ((oid->oid_kind & CTLTYPE) == CTLTYPE_NODE) {
		arg1 = (int *)arg1 + indx;
		arg2 -= indx;
	} else {
		arg1 = oid->oid_arg1;
		arg2 = oid->oid_arg2;
	}
#ifdef MAC
	error = mac_system_check_sysctl(req->td->td_ucred, oid, arg1, arg2,
	    req);
	if (error != 0)
		goto out;
#endif
#ifdef VIMAGE
	if ((oid->oid_kind & CTLFLAG_VNET) && arg1 != NULL)
		arg1 = (void *)(curvnet->vnet_data_base + (uintptr_t)arg1);
#endif
	error = sysctl_root_handler_locked(oid, arg1, arg2, req, &tracker);

out:
	SYSCTL_RUNLOCK(&tracker);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct sysctl_args {
	int	*name;
	u_int	namelen;
	void	*old;
	size_t	*oldlenp;
	void	*new;
	size_t	newlen;
};
#endif
int
sys___sysctl(struct thread *td, struct sysctl_args *uap)
{
	int error, i, name[CTL_MAXNAME];
	size_t j;

	if (uap->namelen > CTL_MAXNAME || uap->namelen < 2)
		return (EINVAL);

 	error = copyin(uap->name, &name, uap->namelen * sizeof(int));
 	if (error)
		return (error);

	error = userland_sysctl(td, name, uap->namelen,
		uap->old, uap->oldlenp, 0,
		uap->new, uap->newlen, &j, 0);
	if (error && error != ENOMEM)
		return (error);
	if (uap->oldlenp) {
		i = copyout(&j, uap->oldlenp, sizeof(j));
		if (i)
			return (i);
	}
	return (error);
}

/*
 * This is used from various compatibility syscalls too.  That's why name
 * must be in kernel space.
 */
int
userland_sysctl(struct thread *td, int *name, u_int namelen, void *old,
    size_t *oldlenp, int inkernel, const void *new, size_t newlen,
    size_t *retval, int flags)
{
	int error = 0, memlocked;
	struct sysctl_req req;

	bzero(&req, sizeof req);

	req.td = td;
	req.flags = flags;

	if (oldlenp) {
		if (inkernel) {
			req.oldlen = *oldlenp;
		} else {
			error = copyin(oldlenp, &req.oldlen, sizeof(*oldlenp));
			if (error)
				return (error);
		}
	}
	req.validlen = req.oldlen;
	req.oldptr = old;

	if (new != NULL) {
		req.newlen = newlen;
		req.newptr = new;
	}

	req.oldfunc = sysctl_old_user;
	req.newfunc = sysctl_new_user;
	req.lock = REQ_UNWIRED;

#ifdef KTRACE
	if (KTRPOINT(curthread, KTR_SYSCTL))
		ktrsysctl(name, namelen);
#endif
	memlocked = 0;
	if (req.oldptr && req.oldlen > 4 * PAGE_SIZE) {
		memlocked = 1;
		sx_xlock(&sysctlmemlock);
	}
	CURVNET_SET(TD_TO_VNET(td));

	for (;;) {
		req.oldidx = 0;
		req.newidx = 0;
		error = sysctl_root(0, name, namelen, &req);
		if (error != EAGAIN)
			break;
		kern_yield(PRI_USER);
	}

	CURVNET_RESTORE();

	if (req.lock == REQ_WIRED && req.validlen > 0)
		vsunlock(req.oldptr, req.validlen);
	if (memlocked)
		sx_xunlock(&sysctlmemlock);

	if (error && error != ENOMEM)
		return (error);

	if (retval) {
		if (req.oldptr && req.oldidx > req.validlen)
			*retval = req.validlen;
		else
			*retval = req.oldidx;
	}
	return (error);
}

/*
 * Drain into a sysctl struct.  The user buffer should be wired if a page
 * fault would cause issue.
 */
static int
sbuf_sysctl_drain(void *arg, const char *data, int len)
{
	struct sysctl_req *req = arg;
	int error;

	error = SYSCTL_OUT(req, data, len);
	KASSERT(error >= 0, ("Got unexpected negative value %d", error));
	return (error == 0 ? len : -error);
}

struct sbuf *
sbuf_new_for_sysctl(struct sbuf *s, char *buf, int length,
    struct sysctl_req *req)
{

	/* Supply a default buffer size if none given. */
	if (buf == NULL && length == 0)
		length = 64;
	s = sbuf_new(s, buf, length, SBUF_FIXEDLEN | SBUF_INCLUDENUL);
	sbuf_set_drain(s, sbuf_sysctl_drain, req);
	return (s);
}
