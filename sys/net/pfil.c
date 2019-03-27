/*	$FreeBSD$ */
/*	$NetBSD: pfil.c,v 1.20 2001/11/12 23:49:46 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2019 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 1996 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/epoch.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/ucred.h>
#include <sys/jail.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/pfil.h>

static MALLOC_DEFINE(M_PFIL, "pfil", "pfil(9) packet filter hooks");

static int pfil_ioctl(struct cdev *, u_long, caddr_t, int, struct thread *);
static struct cdevsw pfil_cdevsw = {
	.d_ioctl =	pfil_ioctl,
	.d_name =	PFILDEV,
	.d_version =	D_VERSION,
};
static struct cdev *pfil_dev;

static struct mtx pfil_lock;
MTX_SYSINIT(pfil_mtxinit, &pfil_lock, "pfil(9) lock", MTX_DEF);
#define	PFIL_LOCK()	mtx_lock(&pfil_lock)
#define	PFIL_UNLOCK()	mtx_unlock(&pfil_lock)
#define	PFIL_LOCK_ASSERT()	mtx_assert(&pfil_lock, MA_OWNED)

#define	PFIL_EPOCH		net_epoch_preempt
#define	PFIL_EPOCH_ENTER(et)	epoch_enter_preempt(net_epoch_preempt, &(et))
#define	PFIL_EPOCH_EXIT(et)	epoch_exit_preempt(net_epoch_preempt, &(et))

struct pfil_hook {
	pfil_func_t	 hook_func;
	void		*hook_ruleset;
	int		 hook_flags;
	int		 hook_links;
	enum pfil_types	 hook_type;
	const char	*hook_modname;
	const char	*hook_rulname;
	LIST_ENTRY(pfil_hook) hook_list;
};

struct pfil_link {
	CK_STAILQ_ENTRY(pfil_link) link_chain;
	pfil_func_t		 link_func;
	void			*link_ruleset;
	int			 link_flags;
	struct pfil_hook	*link_hook;
	struct epoch_context	 link_epoch_ctx;
};

typedef CK_STAILQ_HEAD(pfil_chain, pfil_link)	pfil_chain_t;
struct pfil_head {
	int		 head_nhooksin;
	int		 head_nhooksout;
	pfil_chain_t	 head_in;
	pfil_chain_t	 head_out;
	int		 head_flags;
	enum pfil_types	 head_type;
	LIST_ENTRY(pfil_head) head_list;
	const char	*head_name;
};

LIST_HEAD(pfilheadhead, pfil_head);
VNET_DEFINE_STATIC(struct pfilheadhead, pfil_head_list) =
    LIST_HEAD_INITIALIZER(pfil_head_list);
#define	V_pfil_head_list	VNET(pfil_head_list)

LIST_HEAD(pfilhookhead, pfil_hook);
VNET_DEFINE_STATIC(struct pfilhookhead, pfil_hook_list) =
    LIST_HEAD_INITIALIZER(pfil_hook_list);
#define	V_pfil_hook_list	VNET(pfil_hook_list)

static struct pfil_link *pfil_link_remove(pfil_chain_t *, pfil_hook_t );
static void pfil_link_free(epoch_context_t);

int
pfil_realloc(pfil_packet_t *p, int flags, struct ifnet *ifp)
{
	struct mbuf *m;

	MPASS(flags & PFIL_MEMPTR);

	if ((m = m_devget(p->mem, PFIL_LENGTH(flags), 0, ifp, NULL)) == NULL)
		return (ENOMEM);
	*p = pfil_packet_align(*p);
	*p->m = m;

	return (0);
}

static __noinline int
pfil_fake_mbuf(pfil_func_t func, pfil_packet_t *p, struct ifnet *ifp, int flags,
    void *ruleset, struct inpcb *inp)
{
	struct mbuf m, *mp;
	pfil_return_t rv;

	(void)m_init(&m, M_NOWAIT, MT_DATA, M_NOFREE | M_PKTHDR);
	m_extadd(&m, p->mem, PFIL_LENGTH(flags), NULL, NULL, NULL, 0,
	    EXT_RXRING);
	m.m_len = m.m_pkthdr.len = PFIL_LENGTH(flags);
	mp = &m;
	flags &= ~(PFIL_MEMPTR | PFIL_LENMASK);

	rv = func(&mp, ifp, flags, ruleset, inp);
	if (rv == PFIL_PASS && mp != &m) {
		/*
		 * Firewalls that need pfil_fake_mbuf() most likely don't
		 * know they need return PFIL_REALLOCED.
		 */
		rv = PFIL_REALLOCED;
		*p = pfil_packet_align(*p);
		*p->m = mp;
	}

	return (rv);
}

/*
 * pfil_run_hooks() runs the specified packet filter hook chain.
 */
int
pfil_run_hooks(struct pfil_head *head, pfil_packet_t p, struct ifnet *ifp,
    int flags, struct inpcb *inp)
{
	struct epoch_tracker et;
	pfil_chain_t *pch;
	struct pfil_link *link;
	pfil_return_t rv;
	bool realloc = false;

	if (PFIL_DIR(flags) == PFIL_IN)
		pch = &head->head_in;
	else if (__predict_true(PFIL_DIR(flags) == PFIL_OUT))
		pch = &head->head_out;
	else
		panic("%s: bogus flags %d", __func__, flags);

	rv = PFIL_PASS;
	PFIL_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(link, pch, link_chain) {
		if ((flags & PFIL_MEMPTR) && !(link->link_flags & PFIL_MEMPTR))
			rv = pfil_fake_mbuf(link->link_func, &p, ifp, flags,
			    link->link_ruleset, inp);
		else
			rv = (*link->link_func)(p, ifp, flags,
			    link->link_ruleset, inp);
		if (rv == PFIL_DROPPED || rv == PFIL_CONSUMED)
			break;
		else if (rv == PFIL_REALLOCED) {
			flags &= ~(PFIL_MEMPTR | PFIL_LENMASK);
			realloc = true;
		}
	}
	PFIL_EPOCH_EXIT(et);
	if (realloc && rv == PFIL_PASS)
		rv = PFIL_REALLOCED;
	return (rv);
}

/*
 * pfil_head_register() registers a pfil_head with the packet filter hook
 * mechanism.
 */
pfil_head_t
pfil_head_register(struct pfil_head_args *pa)
{
	struct pfil_head *head, *list;

	MPASS(pa->pa_version == PFIL_VERSION);

	head = malloc(sizeof(struct pfil_head), M_PFIL, M_WAITOK);

	head->head_nhooksin = head->head_nhooksout = 0;
	head->head_flags = pa->pa_flags;
	head->head_type = pa->pa_type;
	head->head_name = pa->pa_headname;
	CK_STAILQ_INIT(&head->head_in);
	CK_STAILQ_INIT(&head->head_out);

	PFIL_LOCK();
	LIST_FOREACH(list, &V_pfil_head_list, head_list)
		if (strcmp(pa->pa_headname, list->head_name) == 0) {
			printf("pfil: duplicate head \"%s\"\n",
			    pa->pa_headname);
		}
	LIST_INSERT_HEAD(&V_pfil_head_list, head, head_list);
	PFIL_UNLOCK();

	return (head);
}

/*
 * pfil_head_unregister() removes a pfil_head from the packet filter hook
 * mechanism.  The producer of the hook promises that all outstanding
 * invocations of the hook have completed before it unregisters the hook.
 */
void
pfil_head_unregister(pfil_head_t ph)
{
	struct pfil_link *link, *next;

	PFIL_LOCK();
	LIST_REMOVE(ph, head_list);

	CK_STAILQ_FOREACH_SAFE(link, &ph->head_in, link_chain, next) {
		link->link_hook->hook_links--;
		free(link, M_PFIL);
	}
	CK_STAILQ_FOREACH_SAFE(link, &ph->head_out, link_chain, next) {
		link->link_hook->hook_links--;
		free(link, M_PFIL);
	}
	PFIL_UNLOCK();
}

pfil_hook_t
pfil_add_hook(struct pfil_hook_args *pa)
{
	struct pfil_hook *hook, *list;

	MPASS(pa->pa_version == PFIL_VERSION);

	hook = malloc(sizeof(struct pfil_hook), M_PFIL, M_WAITOK | M_ZERO);
	hook->hook_func = pa->pa_func;
	hook->hook_ruleset = pa->pa_ruleset;
	hook->hook_flags = pa->pa_flags;
	hook->hook_type = pa->pa_type;
	hook->hook_modname = pa->pa_modname;
	hook->hook_rulname = pa->pa_rulname;

	PFIL_LOCK();
	LIST_FOREACH(list, &V_pfil_hook_list, hook_list)
		if (strcmp(pa->pa_modname, list->hook_modname) == 0 &&
		    strcmp(pa->pa_rulname, list->hook_rulname) == 0) {
			printf("pfil: duplicate hook \"%s:%s\"\n",
			    pa->pa_modname, pa->pa_rulname);
		}
	LIST_INSERT_HEAD(&V_pfil_hook_list, hook, hook_list);
	PFIL_UNLOCK();

	return (hook);
}

static int
pfil_unlink(struct pfil_link_args *pa, pfil_head_t head, pfil_hook_t hook)
{
	struct pfil_link *in, *out;

	PFIL_LOCK_ASSERT();

	if (pa->pa_flags & PFIL_IN) {
		in = pfil_link_remove(&head->head_in, hook);
		if (in != NULL) {
			head->head_nhooksin--;
			hook->hook_links--;
		}
	} else
		in = NULL;
	if (pa->pa_flags & PFIL_OUT) {
		out = pfil_link_remove(&head->head_out, hook);
		if (out != NULL) {
			head->head_nhooksout--;
			hook->hook_links--;
		}
	} else
		out = NULL;
	PFIL_UNLOCK();

	if (in != NULL)
		epoch_call(PFIL_EPOCH, &in->link_epoch_ctx, pfil_link_free);
	if (out != NULL)
		epoch_call(PFIL_EPOCH, &out->link_epoch_ctx, pfil_link_free);

	if (in == NULL && out == NULL)
		return (ENOENT);
	else
		return (0);
}

int
pfil_link(struct pfil_link_args *pa)
{
	struct pfil_link *in, *out, *link;
	struct pfil_head *head;
	struct pfil_hook *hook;
	int error;

	MPASS(pa->pa_version == PFIL_VERSION);

	if ((pa->pa_flags & (PFIL_IN | PFIL_UNLINK)) == PFIL_IN)
		in = malloc(sizeof(*in), M_PFIL, M_WAITOK | M_ZERO);
	else
		in = NULL;
	if ((pa->pa_flags & (PFIL_OUT | PFIL_UNLINK)) == PFIL_OUT)
		out = malloc(sizeof(*out), M_PFIL, M_WAITOK | M_ZERO);
	else
		out = NULL;

	PFIL_LOCK();
	if (pa->pa_flags & PFIL_HEADPTR)
		head = pa->pa_head;
	else
		LIST_FOREACH(head, &V_pfil_head_list, head_list)
			if (strcmp(pa->pa_headname, head->head_name) == 0)
				break;
	if (pa->pa_flags & PFIL_HOOKPTR)
		hook = pa->pa_hook;
	else
		LIST_FOREACH(hook, &V_pfil_hook_list, hook_list)
			if (strcmp(pa->pa_modname, hook->hook_modname) == 0 &&
			    strcmp(pa->pa_rulname, hook->hook_rulname) == 0)
				break;
	if (head == NULL || hook == NULL) {
		error = ENOENT;
		goto fail;
	}

	if (pa->pa_flags & PFIL_UNLINK)
		return (pfil_unlink(pa, head, hook));

	if (head->head_type != hook->hook_type ||
	    ((hook->hook_flags & pa->pa_flags) & ~head->head_flags)) {
		error = EINVAL;
		goto fail;
	}

	if (pa->pa_flags & PFIL_IN)
		CK_STAILQ_FOREACH(link, &head->head_in, link_chain)
			if (link->link_hook == hook) {
				error = EEXIST;
				goto fail;
			}
	if (pa->pa_flags & PFIL_OUT)
		CK_STAILQ_FOREACH(link, &head->head_out, link_chain)
			if (link->link_hook == hook) {
				error = EEXIST;
				goto fail;
			}

	if (pa->pa_flags & PFIL_IN) {
		in->link_hook = hook;
		in->link_func = hook->hook_func;
		in->link_flags = hook->hook_flags;
		in->link_ruleset = hook->hook_ruleset;
		if (pa->pa_flags & PFIL_APPEND)
			CK_STAILQ_INSERT_TAIL(&head->head_in, in, link_chain);
		else
			CK_STAILQ_INSERT_HEAD(&head->head_in, in, link_chain);
		hook->hook_links++;
		head->head_nhooksin++;
	}
	if (pa->pa_flags & PFIL_OUT) {
		out->link_hook = hook;
		out->link_func = hook->hook_func;
		out->link_flags = hook->hook_flags;
		out->link_ruleset = hook->hook_ruleset;
		if (pa->pa_flags & PFIL_APPEND)
			CK_STAILQ_INSERT_HEAD(&head->head_out, out, link_chain);
		else
			CK_STAILQ_INSERT_TAIL(&head->head_out, out, link_chain);
		hook->hook_links++;
		head->head_nhooksout++;
	}
	PFIL_UNLOCK();

	return (0);

fail:
	PFIL_UNLOCK();
	free(in, M_PFIL);
	free(out, M_PFIL);
	return (error);
}

static void
pfil_link_free(epoch_context_t ctx)
{
	struct pfil_link *link;

	link = __containerof(ctx, struct pfil_link, link_epoch_ctx);
	free(link, M_PFIL);
}

/*
 * pfil_remove_hook removes a filter from all filtering points.
 */
void
pfil_remove_hook(pfil_hook_t hook)
{
	struct pfil_head *head;
	struct pfil_link *in, *out;

	PFIL_LOCK();
	LIST_FOREACH(head, &V_pfil_head_list, head_list) {
retry:
		in = pfil_link_remove(&head->head_in, hook);
		if (in != NULL) {
			head->head_nhooksin--;
			hook->hook_links--;
			epoch_call(PFIL_EPOCH, &in->link_epoch_ctx,
			    pfil_link_free);
		}
		out = pfil_link_remove(&head->head_out, hook);
		if (out != NULL) {
			head->head_nhooksout--;
			hook->hook_links--;
			epoch_call(PFIL_EPOCH, &out->link_epoch_ctx,
			    pfil_link_free);
		}
		if (in != NULL || out != NULL)
			/* What if some stupid admin put same filter twice? */
			goto retry;
	}
	LIST_REMOVE(hook, hook_list);
	PFIL_UNLOCK();
	MPASS(hook->hook_links == 0);
	free(hook, M_PFIL);
}

/*
 * Internal: Remove a pfil hook from a hook chain.
 */
static struct pfil_link *
pfil_link_remove(pfil_chain_t *chain, pfil_hook_t hook)
{
	struct pfil_link *link;

	PFIL_LOCK_ASSERT();

	CK_STAILQ_FOREACH(link, chain, link_chain)
		if (link->link_hook == hook) {
			CK_STAILQ_REMOVE(chain, link, pfil_link, link_chain);
			return (link);
		}

	return (NULL);
}

static void
pfil_init(const void *unused __unused)
{
	struct make_dev_args args;
	int error;

	make_dev_args_init(&args);
	args.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	args.mda_devsw = &pfil_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_WHEEL;
	args.mda_mode = 0600;
	error = make_dev_s(&args, &pfil_dev, PFILDEV);
	KASSERT(error == 0, ("%s: failed to create dev: %d", __func__, error));
}
/*
 * Make sure the pfil bits are first before any possible subsystem which
 * might piggyback on the SI_SUB_PROTO_PFIL.
 */
SYSINIT(pfil_init, SI_SUB_PROTO_PFIL, SI_ORDER_FIRST, pfil_init, NULL);

/*
 * User control interface.
 */
static int pfilioc_listheads(struct pfilioc_list *);
static int pfilioc_listhooks(struct pfilioc_list *);
static int pfilioc_link(struct pfilioc_link *);

static int
pfil_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags,
    struct thread *td)
{
	int error;

	CURVNET_SET(TD_TO_VNET(td));
	error = 0;
	switch (cmd) {
	case PFILIOC_LISTHEADS:
		error = pfilioc_listheads((struct pfilioc_list *)addr);
		break;
	case PFILIOC_LISTHOOKS:
		error = pfilioc_listhooks((struct pfilioc_list *)addr);
		break;
	case PFILIOC_LINK:
		error = pfilioc_link((struct pfilioc_link *)addr);
		break;
	default:
		error = EINVAL;
		break;
	}
	CURVNET_RESTORE();
	return (error);
}

static int
pfilioc_listheads(struct pfilioc_list *req)
{
	struct pfil_head *head;
	struct pfil_link *link;
	struct pfilioc_head *iohead;
	struct pfilioc_hook *iohook;
	u_int nheads, nhooks, hd, hk;
	int error;

	PFIL_LOCK();
restart:
	nheads = nhooks = 0;
	LIST_FOREACH(head, &V_pfil_head_list, head_list) {
		nheads++;
		nhooks += head->head_nhooksin + head->head_nhooksout;
	}
	PFIL_UNLOCK();

	if (req->pio_nheads < nheads || req->pio_nhooks < nhooks) {
		req->pio_nheads = nheads;
		req->pio_nhooks = nhooks;
		return (0);
	}

	iohead = malloc(sizeof(*iohead) * nheads, M_TEMP, M_WAITOK);
	iohook = malloc(sizeof(*iohook) * nhooks, M_TEMP, M_WAITOK);

	hd = hk = 0;
	PFIL_LOCK();
	LIST_FOREACH(head, &V_pfil_head_list, head_list) {
		if (hd + 1 > nheads ||
		    hk + head->head_nhooksin + head->head_nhooksout > nhooks) {
			/* Configuration changed during malloc(). */
			free(iohead, M_TEMP);
			free(iohook, M_TEMP);
			goto restart;
		}
		strlcpy(iohead[hd].pio_name, head->head_name,
			sizeof(iohead[0].pio_name));
		iohead[hd].pio_nhooksin = head->head_nhooksin;
		iohead[hd].pio_nhooksout = head->head_nhooksout;
		iohead[hd].pio_type = head->head_type;
		CK_STAILQ_FOREACH(link, &head->head_in, link_chain) {
			strlcpy(iohook[hk].pio_module,
			    link->link_hook->hook_modname,
			    sizeof(iohook[0].pio_module));
			strlcpy(iohook[hk].pio_ruleset,
			    link->link_hook->hook_rulname,
			    sizeof(iohook[0].pio_ruleset));
			hk++;
		}
		CK_STAILQ_FOREACH(link, &head->head_out, link_chain) {
			strlcpy(iohook[hk].pio_module,
			    link->link_hook->hook_modname,
			    sizeof(iohook[0].pio_module));
			strlcpy(iohook[hk].pio_ruleset,
			    link->link_hook->hook_rulname,
			    sizeof(iohook[0].pio_ruleset));
			hk++;
		}
		hd++;
	}
	PFIL_UNLOCK();

	error = copyout(iohead, req->pio_heads,
	    sizeof(*iohead) * min(hd, req->pio_nheads));
	if (error == 0)
		error = copyout(iohook, req->pio_hooks,
		    sizeof(*iohook) * min(req->pio_nhooks, hk));

	req->pio_nheads = hd;
	req->pio_nhooks = hk;

	free(iohead, M_TEMP);
	free(iohook, M_TEMP);

	return (error);
}

static int
pfilioc_listhooks(struct pfilioc_list *req)
{
	struct pfil_hook *hook;
	struct pfilioc_hook *iohook;
	u_int nhooks, hk;
	int error;

	PFIL_LOCK();
restart:
	nhooks = 0;
	LIST_FOREACH(hook, &V_pfil_hook_list, hook_list)
		nhooks++;
	PFIL_UNLOCK();

	if (req->pio_nhooks < nhooks) {
		req->pio_nhooks = nhooks;
		return (0);
	}

	iohook = malloc(sizeof(*iohook) * nhooks, M_TEMP, M_WAITOK);

	hk = 0;
	PFIL_LOCK();
	LIST_FOREACH(hook, &V_pfil_hook_list, hook_list) {
		if (hk + 1 > nhooks) {
			/* Configuration changed during malloc(). */
			free(iohook, M_TEMP);
			goto restart;
		}
		strlcpy(iohook[hk].pio_module, hook->hook_modname,
		    sizeof(iohook[0].pio_module));
		strlcpy(iohook[hk].pio_ruleset, hook->hook_rulname,
		    sizeof(iohook[0].pio_ruleset));
		iohook[hk].pio_type = hook->hook_type;
		iohook[hk].pio_flags = hook->hook_flags;
		hk++;
	}
	PFIL_UNLOCK();

	error = copyout(iohook, req->pio_hooks,
	    sizeof(*iohook) * min(req->pio_nhooks, hk));
	req->pio_nhooks = hk;
	free(iohook, M_TEMP);

	return (error);
}

static int
pfilioc_link(struct pfilioc_link *req)
{
	struct pfil_link_args args;

	if (req->pio_flags & ~(PFIL_IN | PFIL_OUT | PFIL_UNLINK | PFIL_APPEND))
		return (EINVAL);

	args.pa_version = PFIL_VERSION;
	args.pa_flags = req->pio_flags;
	args.pa_headname = req->pio_name;
	args.pa_modname = req->pio_module;
	args.pa_rulname = req->pio_ruleset;

	return (pfil_link(&args));
}
