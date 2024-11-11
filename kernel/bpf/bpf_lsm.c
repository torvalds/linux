// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020 Google LLC.
 */

#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/binfmts.h>
#include <linux/lsm_hooks.h>
#include <linux/bpf_lsm.h>
#include <linux/kallsyms.h>
#include <net/bpf_sk_storage.h>
#include <linux/bpf_local_storage.h>
#include <linux/btf_ids.h>
#include <linux/ima.h>
#include <linux/bpf-cgroup.h>

/* For every LSM hook that allows attachment of BPF programs, declare a nop
 * function where a BPF program can be attached.
 */
#define LSM_HOOK(RET, DEFAULT, NAME, ...)	\
noinline RET bpf_lsm_##NAME(__VA_ARGS__)	\
{						\
	return DEFAULT;				\
}

#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK

#define LSM_HOOK(RET, DEFAULT, NAME, ...) BTF_ID(func, bpf_lsm_##NAME)
BTF_SET_START(bpf_lsm_hooks)
#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK
BTF_SET_END(bpf_lsm_hooks)

BTF_SET_START(bpf_lsm_disabled_hooks)
BTF_ID(func, bpf_lsm_vm_enough_memory)
BTF_ID(func, bpf_lsm_inode_need_killpriv)
BTF_ID(func, bpf_lsm_inode_getsecurity)
BTF_ID(func, bpf_lsm_inode_listsecurity)
BTF_ID(func, bpf_lsm_inode_copy_up_xattr)
BTF_ID(func, bpf_lsm_getselfattr)
BTF_ID(func, bpf_lsm_getprocattr)
BTF_ID(func, bpf_lsm_setprocattr)
#ifdef CONFIG_KEYS
BTF_ID(func, bpf_lsm_key_getsecurity)
#endif
#ifdef CONFIG_AUDIT
BTF_ID(func, bpf_lsm_audit_rule_match)
#endif
BTF_ID(func, bpf_lsm_ismaclabel)
BTF_SET_END(bpf_lsm_disabled_hooks)

/* List of LSM hooks that should operate on 'current' cgroup regardless
 * of function signature.
 */
BTF_SET_START(bpf_lsm_current_hooks)
/* operate on freshly allocated sk without any cgroup association */
#ifdef CONFIG_SECURITY_NETWORK
BTF_ID(func, bpf_lsm_sk_alloc_security)
BTF_ID(func, bpf_lsm_sk_free_security)
#endif
BTF_SET_END(bpf_lsm_current_hooks)

/* List of LSM hooks that trigger while the socket is properly locked.
 */
BTF_SET_START(bpf_lsm_locked_sockopt_hooks)
#ifdef CONFIG_SECURITY_NETWORK
BTF_ID(func, bpf_lsm_sock_graft)
BTF_ID(func, bpf_lsm_inet_csk_clone)
BTF_ID(func, bpf_lsm_inet_conn_established)
#endif
BTF_SET_END(bpf_lsm_locked_sockopt_hooks)

/* List of LSM hooks that trigger while the socket is _not_ locked,
 * but it's ok to call bpf_{g,s}etsockopt because the socket is still
 * in the early init phase.
 */
BTF_SET_START(bpf_lsm_unlocked_sockopt_hooks)
#ifdef CONFIG_SECURITY_NETWORK
BTF_ID(func, bpf_lsm_socket_post_create)
BTF_ID(func, bpf_lsm_socket_socketpair)
#endif
BTF_SET_END(bpf_lsm_unlocked_sockopt_hooks)

#ifdef CONFIG_CGROUP_BPF
void bpf_lsm_find_cgroup_shim(const struct bpf_prog *prog,
			     bpf_func_t *bpf_func)
{
	const struct btf_param *args __maybe_unused;

	if (btf_type_vlen(prog->aux->attach_func_proto) < 1 ||
	    btf_id_set_contains(&bpf_lsm_current_hooks,
				prog->aux->attach_btf_id)) {
		*bpf_func = __cgroup_bpf_run_lsm_current;
		return;
	}

#ifdef CONFIG_NET
	args = btf_params(prog->aux->attach_func_proto);

	if (args[0].type == btf_sock_ids[BTF_SOCK_TYPE_SOCKET])
		*bpf_func = __cgroup_bpf_run_lsm_socket;
	else if (args[0].type == btf_sock_ids[BTF_SOCK_TYPE_SOCK])
		*bpf_func = __cgroup_bpf_run_lsm_sock;
	else
#endif
		*bpf_func = __cgroup_bpf_run_lsm_current;
}
#endif

int bpf_lsm_verify_prog(struct bpf_verifier_log *vlog,
			const struct bpf_prog *prog)
{
	u32 btf_id = prog->aux->attach_btf_id;
	const char *func_name = prog->aux->attach_func_name;

	if (!prog->gpl_compatible) {
		bpf_log(vlog,
			"LSM programs must have a GPL compatible license\n");
		return -EINVAL;
	}

	if (btf_id_set_contains(&bpf_lsm_disabled_hooks, btf_id)) {
		bpf_log(vlog, "attach_btf_id %u points to disabled hook %s\n",
			btf_id, func_name);
		return -EINVAL;
	}

	if (!btf_id_set_contains(&bpf_lsm_hooks, btf_id)) {
		bpf_log(vlog, "attach_btf_id %u points to wrong type name %s\n",
			btf_id, func_name);
		return -EINVAL;
	}

	return 0;
}

/* Mask for all the currently supported BPRM option flags */
#define BPF_F_BRPM_OPTS_MASK	BPF_F_BPRM_SECUREEXEC

BPF_CALL_2(bpf_bprm_opts_set, struct linux_binprm *, bprm, u64, flags)
{
	if (flags & ~BPF_F_BRPM_OPTS_MASK)
		return -EINVAL;

	bprm->secureexec = (flags & BPF_F_BPRM_SECUREEXEC);
	return 0;
}

BTF_ID_LIST_SINGLE(bpf_bprm_opts_set_btf_ids, struct, linux_binprm)

static const struct bpf_func_proto bpf_bprm_opts_set_proto = {
	.func		= bpf_bprm_opts_set,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &bpf_bprm_opts_set_btf_ids[0],
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_ima_inode_hash, struct inode *, inode, void *, dst, u32, size)
{
	return ima_inode_hash(inode, dst, size);
}

static bool bpf_ima_inode_hash_allowed(const struct bpf_prog *prog)
{
	return bpf_lsm_is_sleepable_hook(prog->aux->attach_btf_id);
}

BTF_ID_LIST_SINGLE(bpf_ima_inode_hash_btf_ids, struct, inode)

static const struct bpf_func_proto bpf_ima_inode_hash_proto = {
	.func		= bpf_ima_inode_hash,
	.gpl_only	= false,
	.might_sleep	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &bpf_ima_inode_hash_btf_ids[0],
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.allowed	= bpf_ima_inode_hash_allowed,
};

BPF_CALL_3(bpf_ima_file_hash, struct file *, file, void *, dst, u32, size)
{
	return ima_file_hash(file, dst, size);
}

BTF_ID_LIST_SINGLE(bpf_ima_file_hash_btf_ids, struct, file)

static const struct bpf_func_proto bpf_ima_file_hash_proto = {
	.func		= bpf_ima_file_hash,
	.gpl_only	= false,
	.might_sleep	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &bpf_ima_file_hash_btf_ids[0],
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.allowed	= bpf_ima_inode_hash_allowed,
};

BPF_CALL_1(bpf_get_attach_cookie, void *, ctx)
{
	struct bpf_trace_run_ctx *run_ctx;

	run_ctx = container_of(current->bpf_ctx, struct bpf_trace_run_ctx, run_ctx);
	return run_ctx->bpf_cookie;
}

static const struct bpf_func_proto bpf_get_attach_cookie_proto = {
	.func		= bpf_get_attach_cookie,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

static const struct bpf_func_proto *
bpf_lsm_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	const struct bpf_func_proto *func_proto;

	if (prog->expected_attach_type == BPF_LSM_CGROUP) {
		func_proto = cgroup_common_func_proto(func_id, prog);
		if (func_proto)
			return func_proto;
	}

	switch (func_id) {
	case BPF_FUNC_inode_storage_get:
		return &bpf_inode_storage_get_proto;
	case BPF_FUNC_inode_storage_delete:
		return &bpf_inode_storage_delete_proto;
#ifdef CONFIG_NET
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
#endif /* CONFIG_NET */
	case BPF_FUNC_spin_lock:
		return &bpf_spin_lock_proto;
	case BPF_FUNC_spin_unlock:
		return &bpf_spin_unlock_proto;
	case BPF_FUNC_bprm_opts_set:
		return &bpf_bprm_opts_set_proto;
	case BPF_FUNC_ima_inode_hash:
		return &bpf_ima_inode_hash_proto;
	case BPF_FUNC_ima_file_hash:
		return &bpf_ima_file_hash_proto;
	case BPF_FUNC_get_attach_cookie:
		return bpf_prog_has_trampoline(prog) ? &bpf_get_attach_cookie_proto : NULL;
#ifdef CONFIG_NET
	case BPF_FUNC_setsockopt:
		if (prog->expected_attach_type != BPF_LSM_CGROUP)
			return NULL;
		if (btf_id_set_contains(&bpf_lsm_locked_sockopt_hooks,
					prog->aux->attach_btf_id))
			return &bpf_sk_setsockopt_proto;
		if (btf_id_set_contains(&bpf_lsm_unlocked_sockopt_hooks,
					prog->aux->attach_btf_id))
			return &bpf_unlocked_sk_setsockopt_proto;
		return NULL;
	case BPF_FUNC_getsockopt:
		if (prog->expected_attach_type != BPF_LSM_CGROUP)
			return NULL;
		if (btf_id_set_contains(&bpf_lsm_locked_sockopt_hooks,
					prog->aux->attach_btf_id))
			return &bpf_sk_getsockopt_proto;
		if (btf_id_set_contains(&bpf_lsm_unlocked_sockopt_hooks,
					prog->aux->attach_btf_id))
			return &bpf_unlocked_sk_getsockopt_proto;
		return NULL;
#endif
	default:
		return tracing_prog_func_proto(func_id, prog);
	}
}

/* The set of hooks which are called without pagefaults disabled and are allowed
 * to "sleep" and thus can be used for sleepable BPF programs.
 */
BTF_SET_START(sleepable_lsm_hooks)
BTF_ID(func, bpf_lsm_bpf)
BTF_ID(func, bpf_lsm_bpf_map)
BTF_ID(func, bpf_lsm_bpf_map_create)
BTF_ID(func, bpf_lsm_bpf_map_free)
BTF_ID(func, bpf_lsm_bpf_prog)
BTF_ID(func, bpf_lsm_bpf_prog_load)
BTF_ID(func, bpf_lsm_bpf_prog_free)
BTF_ID(func, bpf_lsm_bpf_token_create)
BTF_ID(func, bpf_lsm_bpf_token_free)
BTF_ID(func, bpf_lsm_bpf_token_cmd)
BTF_ID(func, bpf_lsm_bpf_token_capable)
BTF_ID(func, bpf_lsm_bprm_check_security)
BTF_ID(func, bpf_lsm_bprm_committed_creds)
BTF_ID(func, bpf_lsm_bprm_committing_creds)
BTF_ID(func, bpf_lsm_bprm_creds_for_exec)
BTF_ID(func, bpf_lsm_bprm_creds_from_file)
BTF_ID(func, bpf_lsm_capget)
BTF_ID(func, bpf_lsm_capset)
BTF_ID(func, bpf_lsm_cred_prepare)
BTF_ID(func, bpf_lsm_file_ioctl)
BTF_ID(func, bpf_lsm_file_lock)
BTF_ID(func, bpf_lsm_file_open)
BTF_ID(func, bpf_lsm_file_post_open)
BTF_ID(func, bpf_lsm_file_receive)

BTF_ID(func, bpf_lsm_inode_create)
BTF_ID(func, bpf_lsm_inode_free_security)
BTF_ID(func, bpf_lsm_inode_getattr)
BTF_ID(func, bpf_lsm_inode_getxattr)
BTF_ID(func, bpf_lsm_inode_mknod)
BTF_ID(func, bpf_lsm_inode_need_killpriv)
BTF_ID(func, bpf_lsm_inode_post_setxattr)
BTF_ID(func, bpf_lsm_inode_readlink)
BTF_ID(func, bpf_lsm_inode_rename)
BTF_ID(func, bpf_lsm_inode_rmdir)
BTF_ID(func, bpf_lsm_inode_setattr)
BTF_ID(func, bpf_lsm_inode_setxattr)
BTF_ID(func, bpf_lsm_inode_symlink)
BTF_ID(func, bpf_lsm_inode_unlink)
BTF_ID(func, bpf_lsm_kernel_module_request)
BTF_ID(func, bpf_lsm_kernel_read_file)
BTF_ID(func, bpf_lsm_kernfs_init_security)

#ifdef CONFIG_SECURITY_PATH
BTF_ID(func, bpf_lsm_path_unlink)
BTF_ID(func, bpf_lsm_path_mkdir)
BTF_ID(func, bpf_lsm_path_rmdir)
BTF_ID(func, bpf_lsm_path_truncate)
BTF_ID(func, bpf_lsm_path_symlink)
BTF_ID(func, bpf_lsm_path_link)
BTF_ID(func, bpf_lsm_path_rename)
BTF_ID(func, bpf_lsm_path_chmod)
BTF_ID(func, bpf_lsm_path_chown)
#endif /* CONFIG_SECURITY_PATH */

BTF_ID(func, bpf_lsm_mmap_file)
BTF_ID(func, bpf_lsm_netlink_send)
BTF_ID(func, bpf_lsm_path_notify)
BTF_ID(func, bpf_lsm_release_secctx)
BTF_ID(func, bpf_lsm_sb_alloc_security)
BTF_ID(func, bpf_lsm_sb_eat_lsm_opts)
BTF_ID(func, bpf_lsm_sb_kern_mount)
BTF_ID(func, bpf_lsm_sb_mount)
BTF_ID(func, bpf_lsm_sb_remount)
BTF_ID(func, bpf_lsm_sb_set_mnt_opts)
BTF_ID(func, bpf_lsm_sb_show_options)
BTF_ID(func, bpf_lsm_sb_statfs)
BTF_ID(func, bpf_lsm_sb_umount)
BTF_ID(func, bpf_lsm_settime)

#ifdef CONFIG_SECURITY_NETWORK
BTF_ID(func, bpf_lsm_inet_conn_established)

BTF_ID(func, bpf_lsm_socket_accept)
BTF_ID(func, bpf_lsm_socket_bind)
BTF_ID(func, bpf_lsm_socket_connect)
BTF_ID(func, bpf_lsm_socket_create)
BTF_ID(func, bpf_lsm_socket_getpeername)
BTF_ID(func, bpf_lsm_socket_getpeersec_dgram)
BTF_ID(func, bpf_lsm_socket_getsockname)
BTF_ID(func, bpf_lsm_socket_getsockopt)
BTF_ID(func, bpf_lsm_socket_listen)
BTF_ID(func, bpf_lsm_socket_post_create)
BTF_ID(func, bpf_lsm_socket_recvmsg)
BTF_ID(func, bpf_lsm_socket_sendmsg)
BTF_ID(func, bpf_lsm_socket_shutdown)
BTF_ID(func, bpf_lsm_socket_socketpair)
#endif /* CONFIG_SECURITY_NETWORK */

BTF_ID(func, bpf_lsm_syslog)
BTF_ID(func, bpf_lsm_task_alloc)
BTF_ID(func, bpf_lsm_current_getsecid_subj)
BTF_ID(func, bpf_lsm_task_getsecid_obj)
BTF_ID(func, bpf_lsm_task_prctl)
BTF_ID(func, bpf_lsm_task_setscheduler)
BTF_ID(func, bpf_lsm_task_to_inode)
BTF_ID(func, bpf_lsm_userns_create)
BTF_SET_END(sleepable_lsm_hooks)

BTF_SET_START(untrusted_lsm_hooks)
BTF_ID(func, bpf_lsm_bpf_map_free)
BTF_ID(func, bpf_lsm_bpf_prog_free)
BTF_ID(func, bpf_lsm_file_alloc_security)
BTF_ID(func, bpf_lsm_file_free_security)
#ifdef CONFIG_SECURITY_NETWORK
BTF_ID(func, bpf_lsm_sk_alloc_security)
BTF_ID(func, bpf_lsm_sk_free_security)
#endif /* CONFIG_SECURITY_NETWORK */
BTF_ID(func, bpf_lsm_task_free)
BTF_SET_END(untrusted_lsm_hooks)

bool bpf_lsm_is_sleepable_hook(u32 btf_id)
{
	return btf_id_set_contains(&sleepable_lsm_hooks, btf_id);
}

bool bpf_lsm_is_trusted(const struct bpf_prog *prog)
{
	return !btf_id_set_contains(&untrusted_lsm_hooks, prog->aux->attach_btf_id);
}

const struct bpf_prog_ops lsm_prog_ops = {
};

const struct bpf_verifier_ops lsm_verifier_ops = {
	.get_func_proto = bpf_lsm_func_proto,
	.is_valid_access = btf_ctx_access,
};

/* hooks return 0 or 1 */
BTF_SET_START(bool_lsm_hooks)
#ifdef CONFIG_SECURITY_NETWORK_XFRM
BTF_ID(func, bpf_lsm_xfrm_state_pol_flow_match)
#endif
#ifdef CONFIG_AUDIT
BTF_ID(func, bpf_lsm_audit_rule_known)
#endif
BTF_ID(func, bpf_lsm_inode_xattr_skipcap)
BTF_SET_END(bool_lsm_hooks)

int bpf_lsm_get_retval_range(const struct bpf_prog *prog,
			     struct bpf_retval_range *retval_range)
{
	/* no return value range for void hooks */
	if (!prog->aux->attach_func_proto->type)
		return -EINVAL;

	if (btf_id_set_contains(&bool_lsm_hooks, prog->aux->attach_btf_id)) {
		retval_range->minval = 0;
		retval_range->maxval = 1;
	} else {
		/* All other available LSM hooks, except task_prctl, return 0
		 * on success and negative error code on failure.
		 * To keep things simple, we only allow bpf progs to return 0
		 * or negative errno for task_prctl too.
		 */
		retval_range->minval = -MAX_ERRNO;
		retval_range->maxval = 0;
	}
	return 0;
}
