// SPDX-License-Identifier: GPL-2.0-only
/*
 * Functions to manage eBPF programs attached to cgroups
 *
 * Copyright (c) 2016 Daniel Mack
 */

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/cgroup.h>
#include <linux/filter.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/string.h>
#include <linux/bpf.h>
#include <linux/bpf-cgroup.h>
#include <net/sock.h>
#include <net/bpf_sk_storage.h>

#include "../cgroup/cgroup-internal.h"

DEFINE_STATIC_KEY_FALSE(cgroup_bpf_enabled_key);
EXPORT_SYMBOL(cgroup_bpf_enabled_key);

void cgroup_bpf_offline(struct cgroup *cgrp)
{
	cgroup_get(cgrp);
	percpu_ref_kill(&cgrp->bpf.refcnt);
}

/**
 * cgroup_bpf_release() - put references of all bpf programs and
 *                        release all cgroup bpf data
 * @work: work structure embedded into the cgroup to modify
 */
static void cgroup_bpf_release(struct work_struct *work)
{
	struct cgroup *p, *cgrp = container_of(work, struct cgroup,
					       bpf.release_work);
	enum bpf_cgroup_storage_type stype;
	struct bpf_prog_array *old_array;
	unsigned int type;

	mutex_lock(&cgroup_mutex);

	for (type = 0; type < ARRAY_SIZE(cgrp->bpf.progs); type++) {
		struct list_head *progs = &cgrp->bpf.progs[type];
		struct bpf_prog_list *pl, *tmp;

		list_for_each_entry_safe(pl, tmp, progs, node) {
			list_del(&pl->node);
			bpf_prog_put(pl->prog);
			for_each_cgroup_storage_type(stype) {
				bpf_cgroup_storage_unlink(pl->storage[stype]);
				bpf_cgroup_storage_free(pl->storage[stype]);
			}
			kfree(pl);
			static_branch_dec(&cgroup_bpf_enabled_key);
		}
		old_array = rcu_dereference_protected(
				cgrp->bpf.effective[type],
				lockdep_is_held(&cgroup_mutex));
		bpf_prog_array_free(old_array);
	}

	mutex_unlock(&cgroup_mutex);

	for (p = cgroup_parent(cgrp); p; p = cgroup_parent(p))
		cgroup_bpf_put(p);

	percpu_ref_exit(&cgrp->bpf.refcnt);
	cgroup_put(cgrp);
}

/**
 * cgroup_bpf_release_fn() - callback used to schedule releasing
 *                           of bpf cgroup data
 * @ref: percpu ref counter structure
 */
static void cgroup_bpf_release_fn(struct percpu_ref *ref)
{
	struct cgroup *cgrp = container_of(ref, struct cgroup, bpf.refcnt);

	INIT_WORK(&cgrp->bpf.release_work, cgroup_bpf_release);
	queue_work(system_wq, &cgrp->bpf.release_work);
}

/* count number of elements in the list.
 * it's slow but the list cannot be long
 */
static u32 prog_list_length(struct list_head *head)
{
	struct bpf_prog_list *pl;
	u32 cnt = 0;

	list_for_each_entry(pl, head, node) {
		if (!pl->prog)
			continue;
		cnt++;
	}
	return cnt;
}

/* if parent has non-overridable prog attached,
 * disallow attaching new programs to the descendent cgroup.
 * if parent has overridable or multi-prog, allow attaching
 */
static bool hierarchy_allows_attach(struct cgroup *cgrp,
				    enum bpf_attach_type type,
				    u32 new_flags)
{
	struct cgroup *p;

	p = cgroup_parent(cgrp);
	if (!p)
		return true;
	do {
		u32 flags = p->bpf.flags[type];
		u32 cnt;

		if (flags & BPF_F_ALLOW_MULTI)
			return true;
		cnt = prog_list_length(&p->bpf.progs[type]);
		WARN_ON_ONCE(cnt > 1);
		if (cnt == 1)
			return !!(flags & BPF_F_ALLOW_OVERRIDE);
		p = cgroup_parent(p);
	} while (p);
	return true;
}

/* compute a chain of effective programs for a given cgroup:
 * start from the list of programs in this cgroup and add
 * all parent programs.
 * Note that parent's F_ALLOW_OVERRIDE-type program is yielding
 * to programs in this cgroup
 */
static int compute_effective_progs(struct cgroup *cgrp,
				   enum bpf_attach_type type,
				   struct bpf_prog_array **array)
{
	enum bpf_cgroup_storage_type stype;
	struct bpf_prog_array *progs;
	struct bpf_prog_list *pl;
	struct cgroup *p = cgrp;
	int cnt = 0;

	/* count number of effective programs by walking parents */
	do {
		if (cnt == 0 || (p->bpf.flags[type] & BPF_F_ALLOW_MULTI))
			cnt += prog_list_length(&p->bpf.progs[type]);
		p = cgroup_parent(p);
	} while (p);

	progs = bpf_prog_array_alloc(cnt, GFP_KERNEL);
	if (!progs)
		return -ENOMEM;

	/* populate the array with effective progs */
	cnt = 0;
	p = cgrp;
	do {
		if (cnt > 0 && !(p->bpf.flags[type] & BPF_F_ALLOW_MULTI))
			continue;

		list_for_each_entry(pl, &p->bpf.progs[type], node) {
			if (!pl->prog)
				continue;

			progs->items[cnt].prog = pl->prog;
			for_each_cgroup_storage_type(stype)
				progs->items[cnt].cgroup_storage[stype] =
					pl->storage[stype];
			cnt++;
		}
	} while ((p = cgroup_parent(p)));

	*array = progs;
	return 0;
}

static void activate_effective_progs(struct cgroup *cgrp,
				     enum bpf_attach_type type,
				     struct bpf_prog_array *old_array)
{
	old_array = rcu_replace_pointer(cgrp->bpf.effective[type], old_array,
					lockdep_is_held(&cgroup_mutex));
	/* free prog array after grace period, since __cgroup_bpf_run_*()
	 * might be still walking the array
	 */
	bpf_prog_array_free(old_array);
}

/**
 * cgroup_bpf_inherit() - inherit effective programs from parent
 * @cgrp: the cgroup to modify
 */
int cgroup_bpf_inherit(struct cgroup *cgrp)
{
/* has to use marco instead of const int, since compiler thinks
 * that array below is variable length
 */
#define	NR ARRAY_SIZE(cgrp->bpf.effective)
	struct bpf_prog_array *arrays[NR] = {};
	struct cgroup *p;
	int ret, i;

	ret = percpu_ref_init(&cgrp->bpf.refcnt, cgroup_bpf_release_fn, 0,
			      GFP_KERNEL);
	if (ret)
		return ret;

	for (p = cgroup_parent(cgrp); p; p = cgroup_parent(p))
		cgroup_bpf_get(p);

	for (i = 0; i < NR; i++)
		INIT_LIST_HEAD(&cgrp->bpf.progs[i]);

	for (i = 0; i < NR; i++)
		if (compute_effective_progs(cgrp, i, &arrays[i]))
			goto cleanup;

	for (i = 0; i < NR; i++)
		activate_effective_progs(cgrp, i, arrays[i]);

	return 0;
cleanup:
	for (i = 0; i < NR; i++)
		bpf_prog_array_free(arrays[i]);

	percpu_ref_exit(&cgrp->bpf.refcnt);

	return -ENOMEM;
}

static int update_effective_progs(struct cgroup *cgrp,
				  enum bpf_attach_type type)
{
	struct cgroup_subsys_state *css;
	int err;

	/* allocate and recompute effective prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		if (percpu_ref_is_zero(&desc->bpf.refcnt))
			continue;

		err = compute_effective_progs(desc, type, &desc->bpf.inactive);
		if (err)
			goto cleanup;
	}

	/* all allocations were successful. Activate all prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		if (percpu_ref_is_zero(&desc->bpf.refcnt)) {
			if (unlikely(desc->bpf.inactive)) {
				bpf_prog_array_free(desc->bpf.inactive);
				desc->bpf.inactive = NULL;
			}
			continue;
		}

		activate_effective_progs(desc, type, desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	return 0;

cleanup:
	/* oom while computing effective. Free all computed effective arrays
	 * since they were not activated
	 */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		bpf_prog_array_free(desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	return err;
}

#define BPF_CGROUP_MAX_PROGS 64

/**
 * __cgroup_bpf_attach() - Attach the program to a cgroup, and
 *                         propagate the change to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @prog: A program to attach
 * @type: Type of attach operation
 * @flags: Option flags
 *
 * Must be called with cgroup_mutex held.
 */
int __cgroup_bpf_attach(struct cgroup *cgrp, struct bpf_prog *prog,
			enum bpf_attach_type type, u32 flags)
{
	struct list_head *progs = &cgrp->bpf.progs[type];
	struct bpf_prog *old_prog = NULL;
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE],
		*old_storage[MAX_BPF_CGROUP_STORAGE_TYPE] = {NULL};
	enum bpf_cgroup_storage_type stype;
	struct bpf_prog_list *pl;
	bool pl_was_allocated;
	int err;

	if ((flags & BPF_F_ALLOW_OVERRIDE) && (flags & BPF_F_ALLOW_MULTI))
		/* invalid combination */
		return -EINVAL;

	if (!hierarchy_allows_attach(cgrp, type, flags))
		return -EPERM;

	if (!list_empty(progs) && cgrp->bpf.flags[type] != flags)
		/* Disallow attaching non-overridable on top
		 * of existing overridable in this cgroup.
		 * Disallow attaching multi-prog if overridable or none
		 */
		return -EPERM;

	if (prog_list_length(progs) >= BPF_CGROUP_MAX_PROGS)
		return -E2BIG;

	for_each_cgroup_storage_type(stype) {
		storage[stype] = bpf_cgroup_storage_alloc(prog, stype);
		if (IS_ERR(storage[stype])) {
			storage[stype] = NULL;
			for_each_cgroup_storage_type(stype)
				bpf_cgroup_storage_free(storage[stype]);
			return -ENOMEM;
		}
	}

	if (flags & BPF_F_ALLOW_MULTI) {
		list_for_each_entry(pl, progs, node) {
			if (pl->prog == prog) {
				/* disallow attaching the same prog twice */
				for_each_cgroup_storage_type(stype)
					bpf_cgroup_storage_free(storage[stype]);
				return -EINVAL;
			}
		}

		pl = kmalloc(sizeof(*pl), GFP_KERNEL);
		if (!pl) {
			for_each_cgroup_storage_type(stype)
				bpf_cgroup_storage_free(storage[stype]);
			return -ENOMEM;
		}

		pl_was_allocated = true;
		pl->prog = prog;
		for_each_cgroup_storage_type(stype)
			pl->storage[stype] = storage[stype];
		list_add_tail(&pl->node, progs);
	} else {
		if (list_empty(progs)) {
			pl = kmalloc(sizeof(*pl), GFP_KERNEL);
			if (!pl) {
				for_each_cgroup_storage_type(stype)
					bpf_cgroup_storage_free(storage[stype]);
				return -ENOMEM;
			}
			pl_was_allocated = true;
			list_add_tail(&pl->node, progs);
		} else {
			pl = list_first_entry(progs, typeof(*pl), node);
			old_prog = pl->prog;
			for_each_cgroup_storage_type(stype) {
				old_storage[stype] = pl->storage[stype];
				bpf_cgroup_storage_unlink(old_storage[stype]);
			}
			pl_was_allocated = false;
		}
		pl->prog = prog;
		for_each_cgroup_storage_type(stype)
			pl->storage[stype] = storage[stype];
	}

	cgrp->bpf.flags[type] = flags;

	err = update_effective_progs(cgrp, type);
	if (err)
		goto cleanup;

	static_branch_inc(&cgroup_bpf_enabled_key);
	for_each_cgroup_storage_type(stype) {
		if (!old_storage[stype])
			continue;
		bpf_cgroup_storage_free(old_storage[stype]);
	}
	if (old_prog) {
		bpf_prog_put(old_prog);
		static_branch_dec(&cgroup_bpf_enabled_key);
	}
	for_each_cgroup_storage_type(stype)
		bpf_cgroup_storage_link(storage[stype], cgrp, type);
	return 0;

cleanup:
	/* and cleanup the prog list */
	pl->prog = old_prog;
	for_each_cgroup_storage_type(stype) {
		bpf_cgroup_storage_free(pl->storage[stype]);
		pl->storage[stype] = old_storage[stype];
		bpf_cgroup_storage_link(old_storage[stype], cgrp, type);
	}
	if (pl_was_allocated) {
		list_del(&pl->node);
		kfree(pl);
	}
	return err;
}

/**
 * __cgroup_bpf_detach() - Detach the program from a cgroup, and
 *                         propagate the change to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @prog: A program to detach or NULL
 * @type: Type of detach operation
 *
 * Must be called with cgroup_mutex held.
 */
int __cgroup_bpf_detach(struct cgroup *cgrp, struct bpf_prog *prog,
			enum bpf_attach_type type)
{
	struct list_head *progs = &cgrp->bpf.progs[type];
	enum bpf_cgroup_storage_type stype;
	u32 flags = cgrp->bpf.flags[type];
	struct bpf_prog *old_prog = NULL;
	struct bpf_prog_list *pl;
	int err;

	if (flags & BPF_F_ALLOW_MULTI) {
		if (!prog)
			/* to detach MULTI prog the user has to specify valid FD
			 * of the program to be detached
			 */
			return -EINVAL;
	} else {
		if (list_empty(progs))
			/* report error when trying to detach and nothing is attached */
			return -ENOENT;
	}

	if (flags & BPF_F_ALLOW_MULTI) {
		/* find the prog and detach it */
		list_for_each_entry(pl, progs, node) {
			if (pl->prog != prog)
				continue;
			old_prog = prog;
			/* mark it deleted, so it's ignored while
			 * recomputing effective
			 */
			pl->prog = NULL;
			break;
		}
		if (!old_prog)
			return -ENOENT;
	} else {
		/* to maintain backward compatibility NONE and OVERRIDE cgroups
		 * allow detaching with invalid FD (prog==NULL)
		 */
		pl = list_first_entry(progs, typeof(*pl), node);
		old_prog = pl->prog;
		pl->prog = NULL;
	}

	err = update_effective_progs(cgrp, type);
	if (err)
		goto cleanup;

	/* now can actually delete it from this cgroup list */
	list_del(&pl->node);
	for_each_cgroup_storage_type(stype) {
		bpf_cgroup_storage_unlink(pl->storage[stype]);
		bpf_cgroup_storage_free(pl->storage[stype]);
	}
	kfree(pl);
	if (list_empty(progs))
		/* last program was detached, reset flags to zero */
		cgrp->bpf.flags[type] = 0;

	bpf_prog_put(old_prog);
	static_branch_dec(&cgroup_bpf_enabled_key);
	return 0;

cleanup:
	/* and restore back old_prog */
	pl->prog = old_prog;
	return err;
}

/* Must be called with cgroup_mutex held to avoid races. */
int __cgroup_bpf_query(struct cgroup *cgrp, const union bpf_attr *attr,
		       union bpf_attr __user *uattr)
{
	__u32 __user *prog_ids = u64_to_user_ptr(attr->query.prog_ids);
	enum bpf_attach_type type = attr->query.attach_type;
	struct list_head *progs = &cgrp->bpf.progs[type];
	u32 flags = cgrp->bpf.flags[type];
	struct bpf_prog_array *effective;
	int cnt, ret = 0, i;

	effective = rcu_dereference_protected(cgrp->bpf.effective[type],
					      lockdep_is_held(&cgroup_mutex));

	if (attr->query.query_flags & BPF_F_QUERY_EFFECTIVE)
		cnt = bpf_prog_array_length(effective);
	else
		cnt = prog_list_length(progs);

	if (copy_to_user(&uattr->query.attach_flags, &flags, sizeof(flags)))
		return -EFAULT;
	if (copy_to_user(&uattr->query.prog_cnt, &cnt, sizeof(cnt)))
		return -EFAULT;
	if (attr->query.prog_cnt == 0 || !prog_ids || !cnt)
		/* return early if user requested only program count + flags */
		return 0;
	if (attr->query.prog_cnt < cnt) {
		cnt = attr->query.prog_cnt;
		ret = -ENOSPC;
	}

	if (attr->query.query_flags & BPF_F_QUERY_EFFECTIVE) {
		return bpf_prog_array_copy_to_user(effective, prog_ids, cnt);
	} else {
		struct bpf_prog_list *pl;
		u32 id;

		i = 0;
		list_for_each_entry(pl, progs, node) {
			id = pl->prog->aux->id;
			if (copy_to_user(prog_ids + i, &id, sizeof(id)))
				return -EFAULT;
			if (++i == cnt)
				break;
		}
	}
	return ret;
}

int cgroup_bpf_prog_attach(const union bpf_attr *attr,
			   enum bpf_prog_type ptype, struct bpf_prog *prog)
{
	struct cgroup *cgrp;
	int ret;

	cgrp = cgroup_get_from_fd(attr->target_fd);
	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	ret = cgroup_bpf_attach(cgrp, prog, attr->attach_type,
				attr->attach_flags);
	cgroup_put(cgrp);
	return ret;
}

int cgroup_bpf_prog_detach(const union bpf_attr *attr, enum bpf_prog_type ptype)
{
	struct bpf_prog *prog;
	struct cgroup *cgrp;
	int ret;

	cgrp = cgroup_get_from_fd(attr->target_fd);
	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	prog = bpf_prog_get_type(attr->attach_bpf_fd, ptype);
	if (IS_ERR(prog))
		prog = NULL;

	ret = cgroup_bpf_detach(cgrp, prog, attr->attach_type, 0);
	if (prog)
		bpf_prog_put(prog);

	cgroup_put(cgrp);
	return ret;
}

int cgroup_bpf_prog_query(const union bpf_attr *attr,
			  union bpf_attr __user *uattr)
{
	struct cgroup *cgrp;
	int ret;

	cgrp = cgroup_get_from_fd(attr->query.target_fd);
	if (IS_ERR(cgrp))
		return PTR_ERR(cgrp);

	ret = cgroup_bpf_query(cgrp, attr, uattr);

	cgroup_put(cgrp);
	return ret;
}

/**
 * __cgroup_bpf_run_filter_skb() - Run a program for packet filtering
 * @sk: The socket sending or receiving traffic
 * @skb: The skb that is being sent or received
 * @type: The type of program to be exectuted
 *
 * If no socket is passed, or the socket is not of type INET or INET6,
 * this function does nothing and returns 0.
 *
 * The program type passed in via @type must be suitable for network
 * filtering. No further check is performed to assert that.
 *
 * For egress packets, this function can return:
 *   NET_XMIT_SUCCESS    (0)	- continue with packet output
 *   NET_XMIT_DROP       (1)	- drop packet and notify TCP to call cwr
 *   NET_XMIT_CN         (2)	- continue with packet output and notify TCP
 *				  to call cwr
 *   -EPERM			- drop packet
 *
 * For ingress packets, this function will return -EPERM if any
 * attached program was found and if it returned != 1 during execution.
 * Otherwise 0 is returned.
 */
int __cgroup_bpf_run_filter_skb(struct sock *sk,
				struct sk_buff *skb,
				enum bpf_attach_type type)
{
	unsigned int offset = skb->data - skb_network_header(skb);
	struct sock *save_sk;
	void *saved_data_end;
	struct cgroup *cgrp;
	int ret;

	if (!sk || !sk_fullsock(sk))
		return 0;

	if (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)
		return 0;

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	save_sk = skb->sk;
	skb->sk = sk;
	__skb_push(skb, offset);

	/* compute pointers for the bpf prog */
	bpf_compute_and_save_data_end(skb, &saved_data_end);

	if (type == BPF_CGROUP_INET_EGRESS) {
		ret = BPF_PROG_CGROUP_INET_EGRESS_RUN_ARRAY(
			cgrp->bpf.effective[type], skb, __bpf_prog_run_save_cb);
	} else {
		ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], skb,
					  __bpf_prog_run_save_cb);
		ret = (ret == 1 ? 0 : -EPERM);
	}
	bpf_restore_data_end(skb, saved_data_end);
	__skb_pull(skb, offset);
	skb->sk = save_sk;

	return ret;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_skb);

/**
 * __cgroup_bpf_run_filter_sk() - Run a program on a sock
 * @sk: sock structure to manipulate
 * @type: The type of program to be exectuted
 *
 * socket is passed is expected to be of type INET or INET6.
 *
 * The program type passed in via @type must be suitable for sock
 * filtering. No further check is performed to assert that.
 *
 * This function will return %-EPERM if any if an attached program was found
 * and if it returned != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_sk(struct sock *sk,
			       enum bpf_attach_type type)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	int ret;

	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], sk, BPF_PROG_RUN);
	return ret == 1 ? 0 : -EPERM;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sk);

/**
 * __cgroup_bpf_run_filter_sock_addr() - Run a program on a sock and
 *                                       provided by user sockaddr
 * @sk: sock struct that will use sockaddr
 * @uaddr: sockaddr struct provided by user
 * @type: The type of program to be exectuted
 * @t_ctx: Pointer to attach type specific context
 *
 * socket is expected to be of type INET or INET6.
 *
 * This function will return %-EPERM if an attached program is found and
 * returned value != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_sock_addr(struct sock *sk,
				      struct sockaddr *uaddr,
				      enum bpf_attach_type type,
				      void *t_ctx)
{
	struct bpf_sock_addr_kern ctx = {
		.sk = sk,
		.uaddr = uaddr,
		.t_ctx = t_ctx,
	};
	struct sockaddr_storage unspec;
	struct cgroup *cgrp;
	int ret;

	/* Check socket family since not all sockets represent network
	 * endpoint (e.g. AF_UNIX).
	 */
	if (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)
		return 0;

	if (!ctx.uaddr) {
		memset(&unspec, 0, sizeof(unspec));
		ctx.uaddr = (struct sockaddr *)&unspec;
	}

	cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], &ctx, BPF_PROG_RUN);

	return ret == 1 ? 0 : -EPERM;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sock_addr);

/**
 * __cgroup_bpf_run_filter_sock_ops() - Run a program on a sock
 * @sk: socket to get cgroup from
 * @sock_ops: bpf_sock_ops_kern struct to pass to program. Contains
 * sk with connection information (IP addresses, etc.) May not contain
 * cgroup info if it is a req sock.
 * @type: The type of program to be exectuted
 *
 * socket passed is expected to be of type INET or INET6.
 *
 * The program type passed in via @type must be suitable for sock_ops
 * filtering. No further check is performed to assert that.
 *
 * This function will return %-EPERM if any if an attached program was found
 * and if it returned != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_sock_ops(struct sock *sk,
				     struct bpf_sock_ops_kern *sock_ops,
				     enum bpf_attach_type type)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	int ret;

	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], sock_ops,
				 BPF_PROG_RUN);
	return ret == 1 ? 0 : -EPERM;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sock_ops);

int __cgroup_bpf_check_dev_permission(short dev_type, u32 major, u32 minor,
				      short access, enum bpf_attach_type type)
{
	struct cgroup *cgrp;
	struct bpf_cgroup_dev_ctx ctx = {
		.access_type = (access << 16) | dev_type,
		.major = major,
		.minor = minor,
	};
	int allow = 1;

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	allow = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], &ctx,
				   BPF_PROG_RUN);
	rcu_read_unlock();

	return !allow;
}
EXPORT_SYMBOL(__cgroup_bpf_check_dev_permission);

static const struct bpf_func_proto *
cgroup_base_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_map_push_elem:
		return &bpf_map_push_elem_proto;
	case BPF_FUNC_map_pop_elem:
		return &bpf_map_pop_elem_proto;
	case BPF_FUNC_map_peek_elem:
		return &bpf_map_peek_elem_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_local_storage:
		return &bpf_get_local_storage_proto;
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
	case BPF_FUNC_trace_printk:
		if (capable(CAP_SYS_ADMIN))
			return bpf_get_trace_printk_proto();
		/* fall through */
	default:
		return NULL;
	}
}

static const struct bpf_func_proto *
cgroup_dev_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return cgroup_base_func_proto(func_id, prog);
}

static bool cgroup_dev_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (type == BPF_WRITE)
		return false;

	if (off < 0 || off + size > sizeof(struct bpf_cgroup_dev_ctx))
		return false;
	/* The verifier guarantees that size > 0. */
	if (off % size != 0)
		return false;

	switch (off) {
	case bpf_ctx_range(struct bpf_cgroup_dev_ctx, access_type):
		bpf_ctx_record_field_size(info, size_default);
		if (!bpf_ctx_narrow_access_ok(off, size, size_default))
			return false;
		break;
	default:
		if (size != size_default)
			return false;
	}

	return true;
}

const struct bpf_prog_ops cg_dev_prog_ops = {
};

const struct bpf_verifier_ops cg_dev_verifier_ops = {
	.get_func_proto		= cgroup_dev_func_proto,
	.is_valid_access	= cgroup_dev_is_valid_access,
};

/**
 * __cgroup_bpf_run_filter_sysctl - Run a program on sysctl
 *
 * @head: sysctl table header
 * @table: sysctl table
 * @write: sysctl is being read (= 0) or written (= 1)
 * @buf: pointer to buffer passed by user space
 * @pcount: value-result argument: value is size of buffer pointed to by @buf,
 *	result is size of @new_buf if program set new value, initial value
 *	otherwise
 * @ppos: value-result argument: value is position at which read from or write
 *	to sysctl is happening, result is new position if program overrode it,
 *	initial value otherwise
 * @new_buf: pointer to pointer to new buffer that will be allocated if program
 *	overrides new value provided by user space on sysctl write
 *	NOTE: it's caller responsibility to free *new_buf if it was set
 * @type: type of program to be executed
 *
 * Program is run when sysctl is being accessed, either read or written, and
 * can allow or deny such access.
 *
 * This function will return %-EPERM if an attached program is found and
 * returned value != 1 during execution. In all other cases 0 is returned.
 */
int __cgroup_bpf_run_filter_sysctl(struct ctl_table_header *head,
				   struct ctl_table *table, int write,
				   void __user *buf, size_t *pcount,
				   loff_t *ppos, void **new_buf,
				   enum bpf_attach_type type)
{
	struct bpf_sysctl_kern ctx = {
		.head = head,
		.table = table,
		.write = write,
		.ppos = ppos,
		.cur_val = NULL,
		.cur_len = PAGE_SIZE,
		.new_val = NULL,
		.new_len = 0,
		.new_updated = 0,
	};
	struct cgroup *cgrp;
	int ret;

	ctx.cur_val = kmalloc_track_caller(ctx.cur_len, GFP_KERNEL);
	if (ctx.cur_val) {
		mm_segment_t old_fs;
		loff_t pos = 0;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		if (table->proc_handler(table, 0, (void __user *)ctx.cur_val,
					&ctx.cur_len, &pos)) {
			/* Let BPF program decide how to proceed. */
			ctx.cur_len = 0;
		}
		set_fs(old_fs);
	} else {
		/* Let BPF program decide how to proceed. */
		ctx.cur_len = 0;
	}

	if (write && buf && *pcount) {
		/* BPF program should be able to override new value with a
		 * buffer bigger than provided by user.
		 */
		ctx.new_val = kmalloc_track_caller(PAGE_SIZE, GFP_KERNEL);
		ctx.new_len = min_t(size_t, PAGE_SIZE, *pcount);
		if (!ctx.new_val ||
		    copy_from_user(ctx.new_val, buf, ctx.new_len))
			/* Let BPF program decide how to proceed. */
			ctx.new_len = 0;
	}

	rcu_read_lock();
	cgrp = task_dfl_cgroup(current);
	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], &ctx, BPF_PROG_RUN);
	rcu_read_unlock();

	kfree(ctx.cur_val);

	if (ret == 1 && ctx.new_updated) {
		*new_buf = ctx.new_val;
		*pcount = ctx.new_len;
	} else {
		kfree(ctx.new_val);
	}

	return ret == 1 ? 0 : -EPERM;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_sysctl);

#ifdef CONFIG_NET
static bool __cgroup_bpf_prog_array_is_empty(struct cgroup *cgrp,
					     enum bpf_attach_type attach_type)
{
	struct bpf_prog_array *prog_array;
	bool empty;

	rcu_read_lock();
	prog_array = rcu_dereference(cgrp->bpf.effective[attach_type]);
	empty = bpf_prog_array_is_empty(prog_array);
	rcu_read_unlock();

	return empty;
}

static int sockopt_alloc_buf(struct bpf_sockopt_kern *ctx, int max_optlen)
{
	if (unlikely(max_optlen > PAGE_SIZE) || max_optlen < 0)
		return -EINVAL;

	ctx->optval = kzalloc(max_optlen, GFP_USER);
	if (!ctx->optval)
		return -ENOMEM;

	ctx->optval_end = ctx->optval + max_optlen;

	return 0;
}

static void sockopt_free_buf(struct bpf_sockopt_kern *ctx)
{
	kfree(ctx->optval);
}

int __cgroup_bpf_run_filter_setsockopt(struct sock *sk, int *level,
				       int *optname, char __user *optval,
				       int *optlen, char **kernel_optval)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	struct bpf_sockopt_kern ctx = {
		.sk = sk,
		.level = *level,
		.optname = *optname,
	};
	int ret, max_optlen;

	/* Opportunistic check to see whether we have any BPF program
	 * attached to the hook so we don't waste time allocating
	 * memory and locking the socket.
	 */
	if (!cgroup_bpf_enabled ||
	    __cgroup_bpf_prog_array_is_empty(cgrp, BPF_CGROUP_SETSOCKOPT))
		return 0;

	/* Allocate a bit more than the initial user buffer for
	 * BPF program. The canonical use case is overriding
	 * TCP_CONGESTION(nv) to TCP_CONGESTION(cubic).
	 */
	max_optlen = max_t(int, 16, *optlen);

	ret = sockopt_alloc_buf(&ctx, max_optlen);
	if (ret)
		return ret;

	ctx.optlen = *optlen;

	if (copy_from_user(ctx.optval, optval, *optlen) != 0) {
		ret = -EFAULT;
		goto out;
	}

	lock_sock(sk);
	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[BPF_CGROUP_SETSOCKOPT],
				 &ctx, BPF_PROG_RUN);
	release_sock(sk);

	if (!ret) {
		ret = -EPERM;
		goto out;
	}

	if (ctx.optlen == -1) {
		/* optlen set to -1, bypass kernel */
		ret = 1;
	} else if (ctx.optlen > max_optlen || ctx.optlen < -1) {
		/* optlen is out of bounds */
		ret = -EFAULT;
	} else {
		/* optlen within bounds, run kernel handler */
		ret = 0;

		/* export any potential modifications */
		*level = ctx.level;
		*optname = ctx.optname;
		*optlen = ctx.optlen;
		*kernel_optval = ctx.optval;
	}

out:
	if (ret)
		sockopt_free_buf(&ctx);
	return ret;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_setsockopt);

int __cgroup_bpf_run_filter_getsockopt(struct sock *sk, int level,
				       int optname, char __user *optval,
				       int __user *optlen, int max_optlen,
				       int retval)
{
	struct cgroup *cgrp = sock_cgroup_ptr(&sk->sk_cgrp_data);
	struct bpf_sockopt_kern ctx = {
		.sk = sk,
		.level = level,
		.optname = optname,
		.retval = retval,
	};
	int ret;

	/* Opportunistic check to see whether we have any BPF program
	 * attached to the hook so we don't waste time allocating
	 * memory and locking the socket.
	 */
	if (!cgroup_bpf_enabled ||
	    __cgroup_bpf_prog_array_is_empty(cgrp, BPF_CGROUP_GETSOCKOPT))
		return retval;

	ret = sockopt_alloc_buf(&ctx, max_optlen);
	if (ret)
		return ret;

	ctx.optlen = max_optlen;

	if (!retval) {
		/* If kernel getsockopt finished successfully,
		 * copy whatever was returned to the user back
		 * into our temporary buffer. Set optlen to the
		 * one that kernel returned as well to let
		 * BPF programs inspect the value.
		 */

		if (get_user(ctx.optlen, optlen)) {
			ret = -EFAULT;
			goto out;
		}

		if (ctx.optlen > max_optlen)
			ctx.optlen = max_optlen;

		if (copy_from_user(ctx.optval, optval, ctx.optlen) != 0) {
			ret = -EFAULT;
			goto out;
		}
	}

	lock_sock(sk);
	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[BPF_CGROUP_GETSOCKOPT],
				 &ctx, BPF_PROG_RUN);
	release_sock(sk);

	if (!ret) {
		ret = -EPERM;
		goto out;
	}

	if (ctx.optlen > max_optlen) {
		ret = -EFAULT;
		goto out;
	}

	/* BPF programs only allowed to set retval to 0, not some
	 * arbitrary value.
	 */
	if (ctx.retval != 0 && ctx.retval != retval) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_to_user(optval, ctx.optval, ctx.optlen) ||
	    put_user(ctx.optlen, optlen)) {
		ret = -EFAULT;
		goto out;
	}

	ret = ctx.retval;

out:
	sockopt_free_buf(&ctx);
	return ret;
}
EXPORT_SYMBOL(__cgroup_bpf_run_filter_getsockopt);
#endif

static ssize_t sysctl_cpy_dir(const struct ctl_dir *dir, char **bufp,
			      size_t *lenp)
{
	ssize_t tmp_ret = 0, ret;

	if (dir->header.parent) {
		tmp_ret = sysctl_cpy_dir(dir->header.parent, bufp, lenp);
		if (tmp_ret < 0)
			return tmp_ret;
	}

	ret = strscpy(*bufp, dir->header.ctl_table[0].procname, *lenp);
	if (ret < 0)
		return ret;
	*bufp += ret;
	*lenp -= ret;
	ret += tmp_ret;

	/* Avoid leading slash. */
	if (!ret)
		return ret;

	tmp_ret = strscpy(*bufp, "/", *lenp);
	if (tmp_ret < 0)
		return tmp_ret;
	*bufp += tmp_ret;
	*lenp -= tmp_ret;

	return ret + tmp_ret;
}

BPF_CALL_4(bpf_sysctl_get_name, struct bpf_sysctl_kern *, ctx, char *, buf,
	   size_t, buf_len, u64, flags)
{
	ssize_t tmp_ret = 0, ret;

	if (!buf)
		return -EINVAL;

	if (!(flags & BPF_F_SYSCTL_BASE_NAME)) {
		if (!ctx->head)
			return -EINVAL;
		tmp_ret = sysctl_cpy_dir(ctx->head->parent, &buf, &buf_len);
		if (tmp_ret < 0)
			return tmp_ret;
	}

	ret = strscpy(buf, ctx->table->procname, buf_len);

	return ret < 0 ? ret : tmp_ret + ret;
}

static const struct bpf_func_proto bpf_sysctl_get_name_proto = {
	.func		= bpf_sysctl_get_name,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

static int copy_sysctl_value(char *dst, size_t dst_len, char *src,
			     size_t src_len)
{
	if (!dst)
		return -EINVAL;

	if (!dst_len)
		return -E2BIG;

	if (!src || !src_len) {
		memset(dst, 0, dst_len);
		return -EINVAL;
	}

	memcpy(dst, src, min(dst_len, src_len));

	if (dst_len > src_len) {
		memset(dst + src_len, '\0', dst_len - src_len);
		return src_len;
	}

	dst[dst_len - 1] = '\0';

	return -E2BIG;
}

BPF_CALL_3(bpf_sysctl_get_current_value, struct bpf_sysctl_kern *, ctx,
	   char *, buf, size_t, buf_len)
{
	return copy_sysctl_value(buf, buf_len, ctx->cur_val, ctx->cur_len);
}

static const struct bpf_func_proto bpf_sysctl_get_current_value_proto = {
	.func		= bpf_sysctl_get_current_value,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

BPF_CALL_3(bpf_sysctl_get_new_value, struct bpf_sysctl_kern *, ctx, char *, buf,
	   size_t, buf_len)
{
	if (!ctx->write) {
		if (buf && buf_len)
			memset(buf, '\0', buf_len);
		return -EINVAL;
	}
	return copy_sysctl_value(buf, buf_len, ctx->new_val, ctx->new_len);
}

static const struct bpf_func_proto bpf_sysctl_get_new_value_proto = {
	.func		= bpf_sysctl_get_new_value,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

BPF_CALL_3(bpf_sysctl_set_new_value, struct bpf_sysctl_kern *, ctx,
	   const char *, buf, size_t, buf_len)
{
	if (!ctx->write || !ctx->new_val || !ctx->new_len || !buf || !buf_len)
		return -EINVAL;

	if (buf_len > PAGE_SIZE - 1)
		return -E2BIG;

	memcpy(ctx->new_val, buf, buf_len);
	ctx->new_len = buf_len;
	ctx->new_updated = 1;

	return 0;
}

static const struct bpf_func_proto bpf_sysctl_set_new_value_proto = {
	.func		= bpf_sysctl_set_new_value,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
};

static const struct bpf_func_proto *
sysctl_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_strtol:
		return &bpf_strtol_proto;
	case BPF_FUNC_strtoul:
		return &bpf_strtoul_proto;
	case BPF_FUNC_sysctl_get_name:
		return &bpf_sysctl_get_name_proto;
	case BPF_FUNC_sysctl_get_current_value:
		return &bpf_sysctl_get_current_value_proto;
	case BPF_FUNC_sysctl_get_new_value:
		return &bpf_sysctl_get_new_value_proto;
	case BPF_FUNC_sysctl_set_new_value:
		return &bpf_sysctl_set_new_value_proto;
	default:
		return cgroup_base_func_proto(func_id, prog);
	}
}

static bool sysctl_is_valid_access(int off, int size, enum bpf_access_type type,
				   const struct bpf_prog *prog,
				   struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off + size > sizeof(struct bpf_sysctl) || off % size)
		return false;

	switch (off) {
	case bpf_ctx_range(struct bpf_sysctl, write):
		if (type != BPF_READ)
			return false;
		bpf_ctx_record_field_size(info, size_default);
		return bpf_ctx_narrow_access_ok(off, size, size_default);
	case bpf_ctx_range(struct bpf_sysctl, file_pos):
		if (type == BPF_READ) {
			bpf_ctx_record_field_size(info, size_default);
			return bpf_ctx_narrow_access_ok(off, size, size_default);
		} else {
			return size == size_default;
		}
	default:
		return false;
	}
}

static u32 sysctl_convert_ctx_access(enum bpf_access_type type,
				     const struct bpf_insn *si,
				     struct bpf_insn *insn_buf,
				     struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;
	u32 read_size;

	switch (si->off) {
	case offsetof(struct bpf_sysctl, write):
		*insn++ = BPF_LDX_MEM(
			BPF_SIZE(si->code), si->dst_reg, si->src_reg,
			bpf_target_off(struct bpf_sysctl_kern, write,
				       sizeof_field(struct bpf_sysctl_kern,
						    write),
				       target_size));
		break;
	case offsetof(struct bpf_sysctl, file_pos):
		/* ppos is a pointer so it should be accessed via indirect
		 * loads and stores. Also for stores additional temporary
		 * register is used since neither src_reg nor dst_reg can be
		 * overridden.
		 */
		if (type == BPF_WRITE) {
			int treg = BPF_REG_9;

			if (si->src_reg == treg || si->dst_reg == treg)
				--treg;
			if (si->src_reg == treg || si->dst_reg == treg)
				--treg;
			*insn++ = BPF_STX_MEM(
				BPF_DW, si->dst_reg, treg,
				offsetof(struct bpf_sysctl_kern, tmp_reg));
			*insn++ = BPF_LDX_MEM(
				BPF_FIELD_SIZEOF(struct bpf_sysctl_kern, ppos),
				treg, si->dst_reg,
				offsetof(struct bpf_sysctl_kern, ppos));
			*insn++ = BPF_STX_MEM(
				BPF_SIZEOF(u32), treg, si->src_reg,
				bpf_ctx_narrow_access_offset(
					0, sizeof(u32), sizeof(loff_t)));
			*insn++ = BPF_LDX_MEM(
				BPF_DW, treg, si->dst_reg,
				offsetof(struct bpf_sysctl_kern, tmp_reg));
		} else {
			*insn++ = BPF_LDX_MEM(
				BPF_FIELD_SIZEOF(struct bpf_sysctl_kern, ppos),
				si->dst_reg, si->src_reg,
				offsetof(struct bpf_sysctl_kern, ppos));
			read_size = bpf_size_to_bytes(BPF_SIZE(si->code));
			*insn++ = BPF_LDX_MEM(
				BPF_SIZE(si->code), si->dst_reg, si->dst_reg,
				bpf_ctx_narrow_access_offset(
					0, read_size, sizeof(loff_t)));
		}
		*target_size = sizeof(u32);
		break;
	}

	return insn - insn_buf;
}

const struct bpf_verifier_ops cg_sysctl_verifier_ops = {
	.get_func_proto		= sysctl_func_proto,
	.is_valid_access	= sysctl_is_valid_access,
	.convert_ctx_access	= sysctl_convert_ctx_access,
};

const struct bpf_prog_ops cg_sysctl_prog_ops = {
};

static const struct bpf_func_proto *
cg_sockopt_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
#ifdef CONFIG_NET
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
#endif
#ifdef CONFIG_INET
	case BPF_FUNC_tcp_sock:
		return &bpf_tcp_sock_proto;
#endif
	default:
		return cgroup_base_func_proto(func_id, prog);
	}
}

static bool cg_sockopt_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	const int size_default = sizeof(__u32);

	if (off < 0 || off >= sizeof(struct bpf_sockopt))
		return false;

	if (off % size != 0)
		return false;

	if (type == BPF_WRITE) {
		switch (off) {
		case offsetof(struct bpf_sockopt, retval):
			if (size != size_default)
				return false;
			return prog->expected_attach_type ==
				BPF_CGROUP_GETSOCKOPT;
		case offsetof(struct bpf_sockopt, optname):
			/* fallthrough */
		case offsetof(struct bpf_sockopt, level):
			if (size != size_default)
				return false;
			return prog->expected_attach_type ==
				BPF_CGROUP_SETSOCKOPT;
		case offsetof(struct bpf_sockopt, optlen):
			return size == size_default;
		default:
			return false;
		}
	}

	switch (off) {
	case offsetof(struct bpf_sockopt, sk):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_SOCKET;
		break;
	case offsetof(struct bpf_sockopt, optval):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_PACKET;
		break;
	case offsetof(struct bpf_sockopt, optval_end):
		if (size != sizeof(__u64))
			return false;
		info->reg_type = PTR_TO_PACKET_END;
		break;
	case offsetof(struct bpf_sockopt, retval):
		if (size != size_default)
			return false;
		return prog->expected_attach_type == BPF_CGROUP_GETSOCKOPT;
	default:
		if (size != size_default)
			return false;
		break;
	}
	return true;
}

#define CG_SOCKOPT_ACCESS_FIELD(T, F)					\
	T(BPF_FIELD_SIZEOF(struct bpf_sockopt_kern, F),			\
	  si->dst_reg, si->src_reg,					\
	  offsetof(struct bpf_sockopt_kern, F))

static u32 cg_sockopt_convert_ctx_access(enum bpf_access_type type,
					 const struct bpf_insn *si,
					 struct bpf_insn *insn_buf,
					 struct bpf_prog *prog,
					 u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct bpf_sockopt, sk):
		*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, sk);
		break;
	case offsetof(struct bpf_sockopt, level):
		if (type == BPF_WRITE)
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_STX_MEM, level);
		else
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, level);
		break;
	case offsetof(struct bpf_sockopt, optname):
		if (type == BPF_WRITE)
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_STX_MEM, optname);
		else
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optname);
		break;
	case offsetof(struct bpf_sockopt, optlen):
		if (type == BPF_WRITE)
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_STX_MEM, optlen);
		else
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optlen);
		break;
	case offsetof(struct bpf_sockopt, retval):
		if (type == BPF_WRITE)
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_STX_MEM, retval);
		else
			*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, retval);
		break;
	case offsetof(struct bpf_sockopt, optval):
		*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optval);
		break;
	case offsetof(struct bpf_sockopt, optval_end):
		*insn++ = CG_SOCKOPT_ACCESS_FIELD(BPF_LDX_MEM, optval_end);
		break;
	}

	return insn - insn_buf;
}

static int cg_sockopt_get_prologue(struct bpf_insn *insn_buf,
				   bool direct_write,
				   const struct bpf_prog *prog)
{
	/* Nothing to do for sockopt argument. The data is kzalloc'ated.
	 */
	return 0;
}

const struct bpf_verifier_ops cg_sockopt_verifier_ops = {
	.get_func_proto		= cg_sockopt_func_proto,
	.is_valid_access	= cg_sockopt_is_valid_access,
	.convert_ctx_access	= cg_sockopt_convert_ctx_access,
	.gen_prologue		= cg_sockopt_get_prologue,
};

const struct bpf_prog_ops cg_sockopt_prog_ops = {
};
