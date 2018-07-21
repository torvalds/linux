/*
 * Functions to manage eBPF programs attached to cgroups
 *
 * Copyright (c) 2016 Daniel Mack
 *
 * This file is subject to the terms and conditions of version 2 of the GNU
 * General Public License.  See the file COPYING in the main directory of the
 * Linux distribution for more details.
 */

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/cgroup.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/bpf-cgroup.h>
#include <net/sock.h>

DEFINE_STATIC_KEY_FALSE(cgroup_bpf_enabled_key);
EXPORT_SYMBOL(cgroup_bpf_enabled_key);

/**
 * cgroup_bpf_put() - put references of all bpf programs
 * @cgrp: the cgroup to modify
 */
void cgroup_bpf_put(struct cgroup *cgrp)
{
	unsigned int type;

	for (type = 0; type < ARRAY_SIZE(cgrp->bpf.progs); type++) {
		struct list_head *progs = &cgrp->bpf.progs[type];
		struct bpf_prog_list *pl, *tmp;

		list_for_each_entry_safe(pl, tmp, progs, node) {
			list_del(&pl->node);
			bpf_prog_put(pl->prog);
			kfree(pl);
			static_branch_dec(&cgroup_bpf_enabled_key);
		}
		bpf_prog_array_free(cgrp->bpf.effective[type]);
	}
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
				   struct bpf_prog_array __rcu **array)
{
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
		if (cnt == 0 || (p->bpf.flags[type] & BPF_F_ALLOW_MULTI))
			list_for_each_entry(pl,
					    &p->bpf.progs[type], node) {
				if (!pl->prog)
					continue;
				progs->progs[cnt++] = pl->prog;
			}
		p = cgroup_parent(p);
	} while (p);

	rcu_assign_pointer(*array, progs);
	return 0;
}

static void activate_effective_progs(struct cgroup *cgrp,
				     enum bpf_attach_type type,
				     struct bpf_prog_array __rcu *array)
{
	struct bpf_prog_array __rcu *old_array;

	old_array = xchg(&cgrp->bpf.effective[type], array);
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
	struct bpf_prog_array __rcu *arrays[NR] = {};
	int i;

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
	return -ENOMEM;
}

#define BPF_CGROUP_MAX_PROGS 64

/**
 * __cgroup_bpf_attach() - Attach the program to a cgroup, and
 *                         propagate the change to descendants
 * @cgrp: The cgroup which descendants to traverse
 * @prog: A program to attach
 * @type: Type of attach operation
 *
 * Must be called with cgroup_mutex held.
 */
int __cgroup_bpf_attach(struct cgroup *cgrp, struct bpf_prog *prog,
			enum bpf_attach_type type, u32 flags)
{
	struct list_head *progs = &cgrp->bpf.progs[type];
	struct bpf_prog *old_prog = NULL;
	struct cgroup_subsys_state *css;
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

	if (flags & BPF_F_ALLOW_MULTI) {
		list_for_each_entry(pl, progs, node)
			if (pl->prog == prog)
				/* disallow attaching the same prog twice */
				return -EINVAL;

		pl = kmalloc(sizeof(*pl), GFP_KERNEL);
		if (!pl)
			return -ENOMEM;
		pl_was_allocated = true;
		pl->prog = prog;
		list_add_tail(&pl->node, progs);
	} else {
		if (list_empty(progs)) {
			pl = kmalloc(sizeof(*pl), GFP_KERNEL);
			if (!pl)
				return -ENOMEM;
			pl_was_allocated = true;
			list_add_tail(&pl->node, progs);
		} else {
			pl = list_first_entry(progs, typeof(*pl), node);
			old_prog = pl->prog;
			pl_was_allocated = false;
		}
		pl->prog = prog;
	}

	cgrp->bpf.flags[type] = flags;

	/* allocate and recompute effective prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		err = compute_effective_progs(desc, type, &desc->bpf.inactive);
		if (err)
			goto cleanup;
	}

	/* all allocations were successful. Activate all prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		activate_effective_progs(desc, type, desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	static_branch_inc(&cgroup_bpf_enabled_key);
	if (old_prog) {
		bpf_prog_put(old_prog);
		static_branch_dec(&cgroup_bpf_enabled_key);
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

	/* and cleanup the prog list */
	pl->prog = old_prog;
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
			enum bpf_attach_type type, u32 unused_flags)
{
	struct list_head *progs = &cgrp->bpf.progs[type];
	u32 flags = cgrp->bpf.flags[type];
	struct bpf_prog *old_prog = NULL;
	struct cgroup_subsys_state *css;
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

	/* allocate and recompute effective prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		err = compute_effective_progs(desc, type, &desc->bpf.inactive);
		if (err)
			goto cleanup;
	}

	/* all allocations were successful. Activate all prog arrays */
	css_for_each_descendant_pre(css, &cgrp->self) {
		struct cgroup *desc = container_of(css, struct cgroup, self);

		activate_effective_progs(desc, type, desc->bpf.inactive);
		desc->bpf.inactive = NULL;
	}

	/* now can actually delete it from this cgroup list */
	list_del(&pl->node);
	kfree(pl);
	if (list_empty(progs))
		/* last program was detached, reset flags to zero */
		cgrp->bpf.flags[type] = 0;

	bpf_prog_put(old_prog);
	static_branch_dec(&cgroup_bpf_enabled_key);
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
	int cnt, ret = 0, i;

	if (attr->query.query_flags & BPF_F_QUERY_EFFECTIVE)
		cnt = bpf_prog_array_length(cgrp->bpf.effective[type]);
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
		return bpf_prog_array_copy_to_user(cgrp->bpf.effective[type],
						   prog_ids, cnt);
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
 * This function will return %-EPERM if any if an attached program was found
 * and if it returned != 1 during execution. In all other cases, 0 is returned.
 */
int __cgroup_bpf_run_filter_skb(struct sock *sk,
				struct sk_buff *skb,
				enum bpf_attach_type type)
{
	unsigned int offset = skb->data - skb_network_header(skb);
	struct sock *save_sk;
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
	ret = BPF_PROG_RUN_ARRAY(cgrp->bpf.effective[type], skb,
				 bpf_prog_run_save_cb);
	__skb_pull(skb, offset);
	skb->sk = save_sk;
	return ret == 1 ? 0 : -EPERM;
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
cgroup_dev_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_trace_printk:
		if (capable(CAP_SYS_ADMIN))
			return bpf_get_trace_printk_proto();
	default:
		return NULL;
	}
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
