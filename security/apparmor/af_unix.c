// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor af_unix fine grained mediation
 *
 * Copyright 2023 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/fs.h>
#include <net/tcp_states.h>

#include "include/audit.h"
#include "include/af_unix.h"
#include "include/apparmor.h"
#include "include/file.h"
#include "include/label.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/cred.h"


static inline struct sock *aa_unix_sk(struct unix_sock *u)
{
	return &u->sk;
}

static int unix_fs_perm(const char *op, u32 mask, const struct cred *subj_cred,
			struct aa_label *label, const struct path *path)
{
	AA_BUG(!label);
	AA_BUG(!path);

	if (unconfined(label) || !label_mediates(label, AA_CLASS_FILE))
		return 0;

	mask &= NET_FS_PERMS;
	/* if !u->path.dentry socket is being shutdown - implicit delegation
	 * until obj delegation is supported
	 */
	if (path->dentry) {
		/* the sunpath may not be valid for this ns so use the path */
		struct inode *inode = path->dentry->d_inode;
		vfsuid_t vfsuid = i_uid_into_vfsuid(mnt_idmap(path->mnt), inode);
		struct path_cond cond = {
			.uid = vfsuid_into_kuid(vfsuid),
			.mode = inode->i_mode,
		};

		return aa_path_perm(op, subj_cred, label, path,
				    PATH_SOCK_COND, mask, &cond);
	} /* else implicitly delegated */

	return 0;
}

/* match_addr special constants */
#define ABSTRACT_ADDR "\x00"		/* abstract socket addr */
#define ANONYMOUS_ADDR "\x01"		/* anonymous endpoint, no addr */
#define DISCONNECTED_ADDR "\x02"	/* addr is another namespace */
#define SHUTDOWN_ADDR "\x03"		/* path addr is shutdown and cleared */
#define FS_ADDR "/"			/* path addr in fs */

static aa_state_t match_addr(struct aa_dfa *dfa, aa_state_t state,
			     struct sockaddr_un *addr, int addrlen)
{
	if (addr)
		/* include leading \0 */
		state = aa_dfa_match_len(dfa, state, addr->sun_path,
					 unix_addr_len(addrlen));
	else
		state = aa_dfa_match_len(dfa, state, ANONYMOUS_ADDR, 1);
	/* todo: could change to out of band for cleaner separation */
	state = aa_dfa_null_transition(dfa, state);

	return state;
}

static aa_state_t match_to_local(struct aa_policydb *policy,
				 aa_state_t state, u32 request,
				 int type, int protocol,
				 struct sockaddr_un *addr, int addrlen,
				 struct aa_perms **p,
				 const char **info)
{
	state = aa_match_to_prot(policy, state, request, PF_UNIX, type,
				 protocol, NULL, info);
	if (state) {
		state = match_addr(policy->dfa, state, addr, addrlen);
		if (state) {
			/* todo: local label matching */
			state = aa_dfa_null_transition(policy->dfa, state);
			if (!state)
				*info = "failed local label match";
		} else {
			*info = "failed local address match";
		}
	}

	return state;
}

struct sockaddr_un *aa_sunaddr(const struct unix_sock *u, int *addrlen)
{
	struct unix_address *addr;

	/* memory barrier is sufficient see note in net/unix/af_unix.c */
	addr = smp_load_acquire(&u->addr);
	if (addr) {
		*addrlen = addr->len;
		return addr->name;
	}
	*addrlen = 0;
	return NULL;
}

static aa_state_t match_to_sk(struct aa_policydb *policy,
			      aa_state_t state, u32 request,
			      struct unix_sock *u, struct aa_perms **p,
			      const char **info)
{
	int addrlen;
	struct sockaddr_un *addr = aa_sunaddr(u, &addrlen);

	return match_to_local(policy, state, request, u->sk.sk_type,
			      u->sk.sk_protocol, addr, addrlen, p, info);
}

#define CMD_ADDR	1
#define CMD_LISTEN	2
#define CMD_OPT		4

static aa_state_t match_to_cmd(struct aa_policydb *policy, aa_state_t state,
			       u32 request, struct unix_sock *u,
			       char cmd, struct aa_perms **p,
			       const char **info)
{
	AA_BUG(!p);

	state = match_to_sk(policy, state, request, u, p, info);
	if (state && !*p) {
		state = aa_dfa_match_len(policy->dfa, state, &cmd, 1);
		if (!state)
			*info = "failed cmd selection match";
	}

	return state;
}

static aa_state_t match_to_peer(struct aa_policydb *policy, aa_state_t state,
				u32 request, struct unix_sock *u,
				struct sockaddr_un *peer_addr, int peer_addrlen,
				struct aa_perms **p, const char **info)
{
	AA_BUG(!p);

	state = match_to_cmd(policy, state, request, u, CMD_ADDR, p, info);
	if (state && !*p) {
		state = match_addr(policy->dfa, state, peer_addr, peer_addrlen);
		if (!state)
			*info = "failed peer address match";
	}

	return state;
}

static aa_state_t match_label(struct aa_profile *profile,
			      struct aa_ruleset *rule, aa_state_t state,
			      u32 request, struct aa_profile *peer,
			      struct aa_perms *p,
			      struct apparmor_audit_data *ad)
{
	AA_BUG(!profile);
	AA_BUG(!peer);

	ad->peer = &peer->label;

	if (state && !p) {
		state = aa_dfa_match(rule->policy->dfa, state,
				     peer->base.hname);
		if (!state)
			ad->info = "failed peer label match";

	}

	return aa_do_perms(profile, rule->policy, state, request, p, ad);
}


/* unix sock creation comes before we know if the socket will be an fs
 * socket
 * v6 - semantics are handled by mapping in profile load
 * v7 - semantics require sock create for tasks creating an fs socket.
 * v8 - same as v7
 */
static int profile_create_perm(struct aa_profile *profile, int family,
			       int type, int protocol,
			       struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	aa_state_t state;

	AA_BUG(!profile);
	AA_BUG(profile_unconfined(profile));

	state = RULE_MEDIATES_v9NET(rules);
	if (state) {
		state = aa_match_to_prot(rules->policy, state, AA_MAY_CREATE,
					 PF_UNIX, type, protocol, NULL,
					 &ad->info);

		return aa_do_perms(profile, rules->policy, state, AA_MAY_CREATE,
				   NULL, ad);
	}

	return aa_profile_af_perm(profile, ad, AA_MAY_CREATE, family, type,
				  protocol);
}

static int profile_sk_perm(struct aa_profile *profile,
			   struct apparmor_audit_data *ad,
			   u32 request, struct sock *sk, const struct path *path)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	struct aa_perms *p = NULL;
	aa_state_t state;

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(profile_unconfined(profile));

	state = RULE_MEDIATES_v9NET(rules);
	if (state) {
		if (is_unix_fs(sk))
			return unix_fs_perm(ad->op, request, ad->subj_cred,
					    &profile->label,
					    &unix_sk(sk)->path);

		state = match_to_sk(rules->policy, state, request, unix_sk(sk),
				    &p, &ad->info);

		return aa_do_perms(profile, rules->policy, state, request, p,
				   ad);
	}

	return aa_profile_af_sk_perm(profile, ad, request, sk);
}

static int profile_bind_perm(struct aa_profile *profile, struct sock *sk,
			     struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	struct aa_perms *p = NULL;
	aa_state_t state;

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(!ad);
	AA_BUG(profile_unconfined(profile));

	state = RULE_MEDIATES_v9NET(rules);
	if (state) {
		if (is_unix_addr_fs(ad->net.addr, ad->net.addrlen))
			/* under v7-9 fs hook handles bind */
			return 0;
		/* bind for abstract socket */
		state = match_to_local(rules->policy, state, AA_MAY_BIND,
				       sk->sk_type, sk->sk_protocol,
				       unix_addr(ad->net.addr),
				       ad->net.addrlen,
				       &p, &ad->info);

		return aa_do_perms(profile, rules->policy, state, AA_MAY_BIND,
				   p, ad);
	}

	return aa_profile_af_sk_perm(profile, ad, AA_MAY_BIND, sk);
}

static int profile_listen_perm(struct aa_profile *profile, struct sock *sk,
			       int backlog, struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	struct aa_perms *p = NULL;
	aa_state_t state;

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(!ad);
	AA_BUG(profile_unconfined(profile));

	state = RULE_MEDIATES_v9NET(rules);
	if (state) {
		__be16 b = cpu_to_be16(backlog);

		if (is_unix_fs(sk))
			return unix_fs_perm(ad->op, AA_MAY_LISTEN,
					    ad->subj_cred, &profile->label,
					    &unix_sk(sk)->path);

		state = match_to_cmd(rules->policy, state, AA_MAY_LISTEN,
				     unix_sk(sk), CMD_LISTEN, &p, &ad->info);
		if (state && !p) {
			state = aa_dfa_match_len(rules->policy->dfa, state,
						 (char *) &b, 2);
			if (!state)
				ad->info = "failed listen backlog match";
		}
		return aa_do_perms(profile, rules->policy, state, AA_MAY_LISTEN,
				   p, ad);
	}

	return aa_profile_af_sk_perm(profile, ad, AA_MAY_LISTEN, sk);
}

static int profile_accept_perm(struct aa_profile *profile,
			       struct sock *sk,
			       struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	struct aa_perms *p = NULL;
	aa_state_t state;

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(!ad);
	AA_BUG(profile_unconfined(profile));

	state = RULE_MEDIATES_v9NET(rules);
	if (state) {
		if (is_unix_fs(sk))
			return unix_fs_perm(ad->op, AA_MAY_ACCEPT,
					    ad->subj_cred, &profile->label,
					    &unix_sk(sk)->path);

		state = match_to_sk(rules->policy, state, AA_MAY_ACCEPT,
				    unix_sk(sk), &p, &ad->info);

		return aa_do_perms(profile, rules->policy, state, AA_MAY_ACCEPT,
				   p, ad);
	}

	return aa_profile_af_sk_perm(profile, ad, AA_MAY_ACCEPT, sk);
}

static int profile_opt_perm(struct aa_profile *profile, u32 request,
			    struct sock *sk, int optname,
			    struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	struct aa_perms *p = NULL;
	aa_state_t state;

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(!ad);
	AA_BUG(profile_unconfined(profile));

	state = RULE_MEDIATES_v9NET(rules);
	if (state) {
		__be16 b = cpu_to_be16(optname);
		if (is_unix_fs(sk))
			return unix_fs_perm(ad->op, request,
					    ad->subj_cred, &profile->label,
					    &unix_sk(sk)->path);

		state = match_to_cmd(rules->policy, state, request, unix_sk(sk),
				     CMD_OPT, &p, &ad->info);
		if (state && !p) {
			state = aa_dfa_match_len(rules->policy->dfa, state,
						 (char *) &b, 2);
			if (!state)
				ad->info = "failed sockopt match";
		}
		return aa_do_perms(profile, rules->policy, state, request, p,
				   ad);
	}

	return aa_profile_af_sk_perm(profile, ad, request, sk);
}

/* null peer_label is allowed, in which case the peer_sk label is used */
static int profile_peer_perm(struct aa_profile *profile, u32 request,
			     struct sock *sk, const struct path *path,
			     struct sockaddr_un *peer_addr,
			     int peer_addrlen, const struct path *peer_path,
			     struct aa_label *peer_label,
			     struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	struct aa_perms *p = NULL;
	aa_state_t state;

	AA_BUG(!profile);
	AA_BUG(profile_unconfined(profile));
	AA_BUG(!sk);
	AA_BUG(!peer_label);
	AA_BUG(!ad);

	state = RULE_MEDIATES_v9NET(rules);
	if (state) {
		struct aa_profile *peerp;

		if (peer_path)
			return unix_fs_perm(ad->op, request, ad->subj_cred,
					    &profile->label, peer_path);
		else if (path)
			return unix_fs_perm(ad->op, request, ad->subj_cred,
					    &profile->label, path);
		state = match_to_peer(rules->policy, state, request,
				      unix_sk(sk),
				      peer_addr, peer_addrlen, &p, &ad->info);

		return fn_for_each_in_ns(peer_label, peerp,
				match_label(profile, rules, state, request,
					    peerp, p, ad));
	}

	return aa_profile_af_sk_perm(profile, ad, request, sk);
}

/* -------------------------------- */

int aa_unix_create_perm(struct aa_label *label, int family, int type,
			int protocol)
{
	if (!unconfined(label)) {
		struct aa_profile *profile;
		DEFINE_AUDIT_NET(ad, OP_CREATE, current_cred(), NULL, family,
				 type, protocol);

		return fn_for_each_confined(label, profile,
				profile_create_perm(profile, family, type,
						    protocol, &ad));
	}

	return 0;
}

static int aa_unix_label_sk_perm(const struct cred *subj_cred,
				 struct aa_label *label,
				 const char *op, u32 request, struct sock *sk,
				 const struct path *path)
{
	if (!unconfined(label)) {
		struct aa_profile *profile;
		DEFINE_AUDIT_SK(ad, op, subj_cred, sk);

		return fn_for_each_confined(label, profile,
				profile_sk_perm(profile, &ad, request, sk,
						path));
	}
	return 0;
}

/* revalidation, get/set attr, shutdown */
int aa_unix_sock_perm(const char *op, u32 request, struct socket *sock)
{
	struct aa_label *label;
	int error;

	label = begin_current_label_crit_section();
	error = aa_unix_label_sk_perm(current_cred(), label, op,
				      request, sock->sk,
				      is_unix_fs(sock->sk) ? &unix_sk(sock->sk)->path : NULL);
	end_current_label_crit_section(label);

	return error;
}

static int valid_addr(struct sockaddr *addr, int addr_len)
{
	struct sockaddr_un *sunaddr = unix_addr(addr);

	/* addr_len == offsetof(struct sockaddr_un, sun_path) is autobind */
	if (addr_len < offsetof(struct sockaddr_un, sun_path) ||
	    addr_len > sizeof(*sunaddr))
		return -EINVAL;
	return 0;
}

int aa_unix_bind_perm(struct socket *sock, struct sockaddr *addr,
		      int addrlen)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	error = valid_addr(addr, addrlen);
	if (error)
		return error;

	label = begin_current_label_crit_section();
	/* fs bind is handled by mknod */
	if (!unconfined(label)) {
		DEFINE_AUDIT_SK(ad, OP_BIND, current_cred(), sock->sk);

		ad.net.addr = unix_addr(addr);
		ad.net.addrlen = addrlen;

		error = fn_for_each_confined(label, profile,
				profile_bind_perm(profile, sock->sk, &ad));
	}
	end_current_label_crit_section(label);

	return error;
}

/*
 * unix connections are covered by the
 * - unix_stream_connect (stream) and unix_may_send hooks (dgram)
 * - fs connect is handled by open
 * This is just here to document this is not needed for af_unix
 *
int aa_unix_connect_perm(struct socket *sock, struct sockaddr *address,
			 int addrlen)
{
	return 0;
}
*/

int aa_unix_listen_perm(struct socket *sock, int backlog)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	label = begin_current_label_crit_section();
	if (!unconfined(label)) {
		DEFINE_AUDIT_SK(ad, OP_LISTEN, current_cred(), sock->sk);

		error = fn_for_each_confined(label, profile,
				profile_listen_perm(profile, sock->sk,
						    backlog, &ad));
	}
	end_current_label_crit_section(label);

	return error;
}


/* ability of sock to connect, not peer address binding */
int aa_unix_accept_perm(struct socket *sock, struct socket *newsock)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	label = begin_current_label_crit_section();
	if (!unconfined(label)) {
		DEFINE_AUDIT_SK(ad, OP_ACCEPT, current_cred(), sock->sk);

		error = fn_for_each_confined(label, profile,
				profile_accept_perm(profile, sock->sk, &ad));
	}
	end_current_label_crit_section(label);

	return error;
}


/*
 * dgram handled by unix_may_sendmsg, right to send on stream done at connect
 * could do per msg unix_stream here, but connect + socket transfer is
 * sufficient. This is just here to document this is not needed for af_unix
 *
 * sendmsg, recvmsg
int aa_unix_msg_perm(const char *op, u32 request, struct socket *sock,
		     struct msghdr *msg, int size)
{
	return 0;
}
*/

int aa_unix_opt_perm(const char *op, u32 request, struct socket *sock,
		     int level, int optname)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	label = begin_current_label_crit_section();
	if (!unconfined(label)) {
		DEFINE_AUDIT_SK(ad, op, current_cred(), sock->sk);

		error = fn_for_each_confined(label, profile,
				profile_opt_perm(profile, request, sock->sk,
						 optname, &ad));
	}
	end_current_label_crit_section(label);

	return error;
}

static int unix_peer_perm(const struct cred *subj_cred,
			  struct aa_label *label, const char *op, u32 request,
			  struct sock *sk, const struct path *path,
			  struct sockaddr_un *peer_addr, int peer_addrlen,
			  const struct path *peer_path, struct aa_label *peer_label)
{
	struct aa_profile *profile;
	DEFINE_AUDIT_SK(ad, op, subj_cred, sk);

	ad.net.peer.addr = peer_addr;
	ad.net.peer.addrlen = peer_addrlen;

	return fn_for_each_confined(label, profile,
			profile_peer_perm(profile, request, sk, path,
					  peer_addr, peer_addrlen, peer_path,
					  peer_label, &ad));
}

/**
 *
 * Requires: lock held on both @sk and @peer_sk
 *           called by unix_stream_connect, unix_may_send
 */
int aa_unix_peer_perm(const struct cred *subj_cred,
		      struct aa_label *label, const char *op, u32 request,
		      struct sock *sk, struct sock *peer_sk,
		      struct aa_label *peer_label)
{
	struct unix_sock *peeru = unix_sk(peer_sk);
	struct unix_sock *u = unix_sk(sk);
	int plen;
	struct sockaddr_un *paddr = aa_sunaddr(unix_sk(peer_sk), &plen);

	AA_BUG(!label);
	AA_BUG(!sk);
	AA_BUG(!peer_sk);
	AA_BUG(!peer_label);

	return unix_peer_perm(subj_cred, label, op, request, sk,
			      is_unix_fs(sk) ? &u->path : NULL,
			      paddr, plen,
			      is_unix_fs(peer_sk) ? &peeru->path : NULL,
			      peer_label);
}

/* sk_plabel for comparison only */
static void update_sk_ctx(struct sock *sk, struct aa_label *label,
			  struct aa_label *plabel)
{
	struct aa_label *l, *old;
	struct aa_sk_ctx *ctx = aa_sock(sk);
	bool update_sk;

	rcu_read_lock();
	update_sk = (plabel &&
		     (plabel != rcu_access_pointer(ctx->peer_lastupdate) ||
		      !aa_label_is_subset(plabel, rcu_dereference(ctx->peer)))) ||
	  !__aa_subj_label_is_cached(label, rcu_dereference(ctx->label));
	rcu_read_unlock();
	if (!update_sk)
		return;

	spin_lock(&unix_sk(sk)->lock);
	old = rcu_dereference_protected(ctx->label,
					lockdep_is_held(&unix_sk(sk)->lock));
	l = aa_label_merge(old, label, GFP_ATOMIC);
	if (l) {
		if (l != old) {
			rcu_assign_pointer(ctx->label, l);
			aa_put_label(old);
		} else
			aa_put_label(l);
	}
	if (plabel && rcu_access_pointer(ctx->peer_lastupdate) != plabel) {
		old = rcu_dereference_protected(ctx->peer, lockdep_is_held(&unix_sk(sk)->lock));

		if (old == plabel) {
			rcu_assign_pointer(ctx->peer_lastupdate, plabel);
		} else if (aa_label_is_subset(plabel, old)) {
			rcu_assign_pointer(ctx->peer_lastupdate, plabel);
			rcu_assign_pointer(ctx->peer, aa_get_label(plabel));
			aa_put_label(old);
		} /* else race or a subset - don't update */
	}
	spin_unlock(&unix_sk(sk)->lock);
}

static void update_peer_ctx(struct sock *sk, struct aa_sk_ctx *ctx,
			    struct aa_label *label)
{
	struct aa_label *l, *old;

	spin_lock(&unix_sk(sk)->lock);
	old = rcu_dereference_protected(ctx->peer,
					lockdep_is_held(&unix_sk(sk)->lock));
	l = aa_label_merge(old, label, GFP_ATOMIC);
	if (l) {
		if (l != old) {
			rcu_assign_pointer(ctx->peer, l);
			aa_put_label(old);
		} else
			aa_put_label(l);
	}
	spin_unlock(&unix_sk(sk)->lock);
}

/* This fn is only checked if something has changed in the security
 * boundaries. Otherwise cached info off file is sufficient
 */
int aa_unix_file_perm(const struct cred *subj_cred, struct aa_label *label,
		      const char *op, u32 request, struct file *file)
{
	struct socket *sock = (struct socket *) file->private_data;
	struct sockaddr_un *addr, *peer_addr;
	int addrlen, peer_addrlen;
	struct aa_label *plabel = NULL;
	struct sock *peer_sk = NULL;
	u32 sk_req = request & ~NET_PEER_MASK;
	struct path path;
	bool is_sk_fs;
	int error = 0;

	AA_BUG(!label);
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(sock->sk->sk_family != PF_UNIX);

	/* investigate only using lock via unix_peer_get()
	 * addr only needs the memory barrier, but need to investigate
	 * path
	 */
	unix_state_lock(sock->sk);
	peer_sk = unix_peer(sock->sk);
	if (peer_sk)
		sock_hold(peer_sk);

	is_sk_fs = is_unix_fs(sock->sk);
	addr = aa_sunaddr(unix_sk(sock->sk), &addrlen);
	path = unix_sk(sock->sk)->path;
	unix_state_unlock(sock->sk);

	if (is_sk_fs && peer_sk)
		sk_req = request;
	if (sk_req) {
			error = aa_unix_label_sk_perm(subj_cred, label, op,
						      sk_req, sock->sk,
						      is_sk_fs ? &path : NULL);
	}
	if (!peer_sk)
		goto out;

	peer_addr = aa_sunaddr(unix_sk(peer_sk), &peer_addrlen);

	struct path peer_path;

	peer_path = unix_sk(peer_sk)->path;
	if (!is_sk_fs && is_unix_fs(peer_sk)) {
		last_error(error,
			   unix_fs_perm(op, request, subj_cred, label,
					is_unix_fs(peer_sk) ? &peer_path : NULL));
	} else if (!is_sk_fs) {
		struct aa_label *plabel;
		struct aa_sk_ctx *pctx = aa_sock(peer_sk);

		rcu_read_lock();
		plabel = aa_get_label_rcu(&pctx->label);
		rcu_read_unlock();
		/* no fs check of aa_unix_peer_perm because conditions above
		 * ensure they will never be done
		 */
		last_error(error,
			xcheck(unix_peer_perm(subj_cred, label, op,
					      MAY_READ | MAY_WRITE, sock->sk,
					      is_sk_fs ? &path : NULL,
					      peer_addr, peer_addrlen,
					      is_unix_fs(peer_sk) ?
							&peer_path : NULL,
					      plabel),
			       unix_peer_perm(file->f_cred, plabel, op,
					      MAY_READ | MAY_WRITE, peer_sk,
					      is_unix_fs(peer_sk) ?
							&peer_path : NULL,
					      addr, addrlen,
					      is_sk_fs ? &path : NULL,
					      label)));
		if (!error && !__aa_subj_label_is_cached(plabel, label))
			update_peer_ctx(peer_sk, pctx, label);
	}
	sock_put(peer_sk);

out:

	/* update peer cache to latest successful perm check */
	if (error == 0)
		update_sk_ctx(sock->sk, label, plabel);
	aa_put_label(plabel);

	return error;
}

