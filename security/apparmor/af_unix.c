/*
 * AppArmor security module
 *
 * This file contains AppArmor af_unix fine grained mediation
 *
 * Copyright 2014 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <net/tcp_states.h>

#include "include/af_unix.h"
#include "include/apparmor.h"
#include "include/context.h"
#include "include/file.h"
#include "include/label.h"
#include "include/path.h"
#include "include/policy.h"

static inline struct sock *aa_sock(struct unix_sock *u)
{
	return &u->sk;
}

static inline int unix_fs_perm(const char *op, u32 mask, struct aa_label *label,
			       struct unix_sock *u, int flags)
{
	AA_BUG(!label);
	AA_BUG(!u);
	AA_BUG(!UNIX_FS(aa_sock(u)));

	if (unconfined(label) || !LABEL_MEDIATES(label, AA_CLASS_FILE))
		return 0;

	mask &= NET_FS_PERMS;
	if (!u->path.dentry) {
		struct path_cond cond = { };
		struct aa_perms perms = { };
		struct aa_profile *profile;

		/* socket path has been cleared because it is being shutdown
		 * can only fall back to original sun_path request
		 */
		struct aa_sk_ctx *ctx = SK_CTX(&u->sk);
		if (ctx->path.dentry)
			return aa_path_perm(op, label, &ctx->path, flags, mask,
					    &cond);
		return fn_for_each_confined(label, profile,
			((flags | profile->path_flags) & PATH_MEDIATE_DELETED) ?
				__aa_path_perm(op, profile,
					       u->addr->name->sun_path, mask,
					       &cond, flags, &perms) :
				aa_audit_file(profile, &nullperms, op, mask,
					      u->addr->name->sun_path, NULL,
					      NULL, cond.uid,
					      "Failed name lookup - "
					      "deleted entry", -EACCES));
	} else {
		/* the sunpath may not be valid for this ns so use the path */
		struct path_cond cond = { u->path.dentry->d_inode->i_uid,
					  u->path.dentry->d_inode->i_mode
		};

		return aa_path_perm(op, label, &u->path, flags, mask, &cond);
	}

	return 0;
}

/* passing in state returned by PROFILE_MEDIATES_AF */
static unsigned int match_to_prot(struct aa_profile *profile,
				  unsigned int state, int type, int protocol,
				  const char **info)
{
	__be16 buffer[2];
	buffer[0] = cpu_to_be16(type);
	buffer[1] = cpu_to_be16(protocol);
	state = aa_dfa_match_len(profile->policy.dfa, state, (char *) &buffer,
				 4);
	if (!state)
		*info = "failed type and protocol match";
	return state;
}

static unsigned int match_addr(struct aa_profile *profile, unsigned int state,
			       struct sockaddr_un *addr, int addrlen)
{
	if (addr)
		/* include leading \0 */
		state = aa_dfa_match_len(profile->policy.dfa, state,
					 addr->sun_path,
					 unix_addr_len(addrlen));
	else
		/* anonymous end point */
		state = aa_dfa_match_len(profile->policy.dfa, state, "\x01",
					 1);
	/* todo change to out of band */
	state = aa_dfa_null_transition(profile->policy.dfa, state);
	return state;
}

static unsigned int match_to_local(struct aa_profile *profile,
				   unsigned int state, int type, int protocol,
				   struct sockaddr_un *addr, int addrlen,
				   const char **info)
{
	state = match_to_prot(profile, state, type, protocol, info);
	if (state) {
		state = match_addr(profile, state, addr, addrlen);
		if (state) {
			/* todo: local label matching */
			state = aa_dfa_null_transition(profile->policy.dfa,
						       state);
			if (!state)
				*info = "failed local label match";
		} else
			*info = "failed local address match";
	}

	return state;
}

static unsigned int match_to_sk(struct aa_profile *profile,
				unsigned int state, struct unix_sock *u,
				const char **info)
{
	struct sockaddr_un *addr = NULL;
	int addrlen = 0;

	if (u->addr) {
		addr = u->addr->name;
		addrlen = u->addr->len;
	}

	return match_to_local(profile, state, u->sk.sk_type, u->sk.sk_protocol,
			      addr, addrlen, info);
}

#define CMD_ADDR	1
#define CMD_LISTEN	2
#define CMD_OPT		4

static inline unsigned int match_to_cmd(struct aa_profile *profile,
					unsigned int state, struct unix_sock *u,
					char cmd, const char **info)
{
	state = match_to_sk(profile, state, u, info);
	if (state) {
		state = aa_dfa_match_len(profile->policy.dfa, state, &cmd, 1);
		if (!state)
			*info = "failed cmd selection match";
	}

	return state;
}

static inline unsigned int match_to_peer(struct aa_profile *profile,
					 unsigned int state,
					 struct unix_sock *u,
					 struct sockaddr_un *peer_addr,
					 int peer_addrlen,
					 const char **info)
{
	state = match_to_cmd(profile, state, u, CMD_ADDR, info);
	if (state) {
		state = match_addr(profile, state, peer_addr, peer_addrlen);
		if (!state)
			*info = "failed peer address match";
	}
	return state;
}

static int do_perms(struct aa_profile *profile, unsigned int state, u32 request,
		    struct common_audit_data *sa)
{
	struct aa_perms perms;

	AA_BUG(!profile);

	aa_compute_perms(profile->policy.dfa, state, &perms);
	aa_apply_modes_to_perms(profile, &perms);
	return aa_check_perms(profile, &perms, request, sa,
			      audit_net_cb);
}

static int match_label(struct aa_profile *profile, struct aa_profile *peer,
			      unsigned int state, u32 request,
			      struct common_audit_data *sa)
{
	AA_BUG(!profile);
	AA_BUG(!peer);

	aad(sa)->peer = &peer->label;

	if (state) {
		state = aa_dfa_match(profile->policy.dfa, state,
				     peer->base.hname);
		if (!state)
			aad(sa)->info = "failed peer label match";
	}
	return do_perms(profile, state, request, sa);
}


/* unix sock creation comes before we know if the socket will be an fs
 * socket
 * v6 - semantics are handled by mapping in profile load
 * v7 - semantics require sock create for tasks creating an fs socket.
 */
static int profile_create_perm(struct aa_profile *profile, int family,
			       int type, int protocol)
{
	unsigned int state;
	DEFINE_AUDIT_NET(sa, OP_CREATE, NULL, family, type, protocol);

	AA_BUG(!profile);
	AA_BUG(profile_unconfined(profile));

	if ((state = PROFILE_MEDIATES_AF(profile, AF_UNIX))) {
		state = match_to_prot(profile, state, type, protocol,
				      &aad(&sa)->info);
		return do_perms(profile, state, AA_MAY_CREATE, &sa);
	}

	return aa_profile_af_perm(profile, &sa, AA_MAY_CREATE, family, type);
}

int aa_unix_create_perm(struct aa_label *label, int family, int type,
			int protocol)
{
	struct aa_profile *profile;

	if (unconfined(label))
		return 0;

	return fn_for_each_confined(label, profile,
			profile_create_perm(profile, family, type, protocol));
}


static inline int profile_sk_perm(struct aa_profile *profile, const char *op,
				  u32 request, struct sock *sk)
{
	unsigned int state;
	DEFINE_AUDIT_SK(sa, op, sk);

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(UNIX_FS(sk));
	AA_BUG(profile_unconfined(profile));

	state = PROFILE_MEDIATES_AF(profile, AF_UNIX);
	if (state) {
		state = match_to_sk(profile, state, unix_sk(sk),
				    &aad(&sa)->info);
		return do_perms(profile, state, request, &sa);
	}

	return aa_profile_af_sk_perm(profile, &sa, request, sk);
}

int aa_unix_label_sk_perm(struct aa_label *label, const char *op, u32 request,
			  struct sock *sk)
{
	struct aa_profile *profile;

	return fn_for_each_confined(label, profile,
			profile_sk_perm(profile, op, request, sk));
}

static int unix_label_sock_perm(struct aa_label *label, const char *op, u32 request,
				struct socket *sock)
{
	if (unconfined(label))
		return 0;
	if (UNIX_FS(sock->sk))
		return unix_fs_perm(op, request, label, unix_sk(sock->sk), 0);

	return aa_unix_label_sk_perm(label, op, request, sock->sk);
}

/* revaliation, get/set attr */
int aa_unix_sock_perm(const char *op, u32 request, struct socket *sock)
{
	struct aa_label *label;
	int error;

	label = begin_current_label_crit_section();
	error = unix_label_sock_perm(label, op, request, sock);
	end_current_label_crit_section(label);

	return error;
}

static int profile_bind_perm(struct aa_profile *profile, struct sock *sk,
			     struct sockaddr *addr, int addrlen)
{
	unsigned int state;
	DEFINE_AUDIT_SK(sa, OP_BIND, sk);

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(addr->sa_family != AF_UNIX);
	AA_BUG(profile_unconfined(profile));
	AA_BUG(unix_addr_fs(addr, addrlen));

	state = PROFILE_MEDIATES_AF(profile, AF_UNIX);
	if (state) {
		/* bind for abstract socket */
		aad(&sa)->net.addr = unix_addr(addr);
		aad(&sa)->net.addrlen = addrlen;

		state = match_to_local(profile, state,
				       sk->sk_type, sk->sk_protocol,
				       unix_addr(addr), addrlen,
				       &aad(&sa)->info);
		return do_perms(profile, state, AA_MAY_BIND, &sa);
	}

	return aa_profile_af_sk_perm(profile, &sa, AA_MAY_BIND, sk);
}

int aa_unix_bind_perm(struct socket *sock, struct sockaddr *address,
		      int addrlen)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	 label = begin_current_label_crit_section();
	 /* fs bind is handled by mknod */
	if (!(unconfined(label) || unix_addr_fs(address, addrlen)))
		error = fn_for_each_confined(label, profile,
				profile_bind_perm(profile, sock->sk, address,
						  addrlen));
	end_current_label_crit_section(label);

	return error;
}

int aa_unix_connect_perm(struct socket *sock, struct sockaddr *address,
			 int addrlen)
{
	/* unix connections are covered by the
	 * - unix_stream_connect (stream) and unix_may_send hooks (dgram)
	 * - fs connect is handled by open
	 */
	return 0;
}

static int profile_listen_perm(struct aa_profile *profile, struct sock *sk,
			       int backlog)
{
	unsigned int state;
	DEFINE_AUDIT_SK(sa, OP_LISTEN, sk);

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(UNIX_FS(sk));
	AA_BUG(profile_unconfined(profile));

	state = PROFILE_MEDIATES_AF(profile, AF_UNIX);
	if (state) {
		__be16 b = cpu_to_be16(backlog);

		state = match_to_cmd(profile, state, unix_sk(sk), CMD_LISTEN,
				     &aad(&sa)->info);
		if (state) {
			state = aa_dfa_match_len(profile->policy.dfa, state,
						 (char *) &b, 2);
			if (!state)
				aad(&sa)->info = "failed listen backlog match";
		}
		return do_perms(profile, state, AA_MAY_LISTEN, &sa);
	}

	return aa_profile_af_sk_perm(profile, &sa, AA_MAY_LISTEN, sk);
}

int aa_unix_listen_perm(struct socket *sock, int backlog)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	label = begin_current_label_crit_section();
	if (!(unconfined(label) || UNIX_FS(sock->sk)))
		error = fn_for_each_confined(label, profile,
				profile_listen_perm(profile, sock->sk,
						    backlog));
	end_current_label_crit_section(label);

	return error;
}


static inline int profile_accept_perm(struct aa_profile *profile,
				      struct sock *sk,
				      struct sock *newsk)
{
	unsigned int state;
	DEFINE_AUDIT_SK(sa, OP_ACCEPT, sk);

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(UNIX_FS(sk));
	AA_BUG(profile_unconfined(profile));

	state = PROFILE_MEDIATES_AF(profile, AF_UNIX);
	if (state) {
		state = match_to_sk(profile, state, unix_sk(sk),
				    &aad(&sa)->info);
		return do_perms(profile, state, AA_MAY_ACCEPT, &sa);
	}

	return aa_profile_af_sk_perm(profile, &sa, AA_MAY_ACCEPT, sk);
}

/* ability of sock to connect, not peer address binding */
int aa_unix_accept_perm(struct socket *sock, struct socket *newsock)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	label = begin_current_label_crit_section();
	if (!(unconfined(label) || UNIX_FS(sock->sk)))
		error = fn_for_each_confined(label, profile,
				profile_accept_perm(profile, sock->sk,
						    newsock->sk));
	end_current_label_crit_section(label);

	return error;
}


/* dgram handled by unix_may_sendmsg, right to send on stream done at connect
 * could do per msg unix_stream here
 */
/* sendmsg, recvmsg */
int aa_unix_msg_perm(const char *op, u32 request, struct socket *sock,
		     struct msghdr *msg, int size)
{
	return 0;
}


static int profile_opt_perm(struct aa_profile *profile, const char *op, u32 request,
			    struct sock *sk, int level, int optname)
{
	unsigned int state;
	DEFINE_AUDIT_SK(sa, op, sk);

	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(UNIX_FS(sk));
	AA_BUG(profile_unconfined(profile));

	state = PROFILE_MEDIATES_AF(profile, AF_UNIX);
	if (state) {
		__be16 b = cpu_to_be16(optname);

		state = match_to_cmd(profile, state, unix_sk(sk), CMD_OPT,
				     &aad(&sa)->info);
		if (state) {
			state = aa_dfa_match_len(profile->policy.dfa, state,
						 (char *) &b, 2);
			if (!state)
				aad(&sa)->info = "failed sockopt match";
		}
		return do_perms(profile, state, request, &sa);
	}

	return aa_profile_af_sk_perm(profile, &sa, request, sk);
}

int aa_unix_opt_perm(const char *op, u32 request, struct socket *sock, int level,
		     int optname)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error = 0;

	label = begin_current_label_crit_section();
	if (!(unconfined(label) || UNIX_FS(sock->sk)))
		error = fn_for_each_confined(label, profile,
				profile_opt_perm(profile, op, request,
						 sock->sk, level, optname));
	end_current_label_crit_section(label);

	return error;
}

/* null peer_label is allowed, in which case the peer_sk label is used */
static int profile_peer_perm(struct aa_profile *profile, const char *op, u32 request,
			     struct sock *sk, struct sock *peer_sk,
			     struct aa_label *peer_label,
			     struct common_audit_data *sa)
{
	unsigned int state;

	AA_BUG(!profile);
	AA_BUG(profile_unconfined(profile));
	AA_BUG(!sk);
	AA_BUG(!peer_sk);
	AA_BUG(UNIX_FS(peer_sk));

	state = PROFILE_MEDIATES_AF(profile, AF_UNIX);
	if (state) {
		struct aa_sk_ctx *peer_ctx = SK_CTX(peer_sk);
		struct aa_profile *peerp;
		struct sockaddr_un *addr = NULL;
		int len = 0;
		if (unix_sk(peer_sk)->addr) {
			addr = unix_sk(peer_sk)->addr->name;
			len = unix_sk(peer_sk)->addr->len;
		}
		state = match_to_peer(profile, state, unix_sk(sk),
				      addr, len, &aad(sa)->info);
		if (!peer_label)
			peer_label = peer_ctx->label;
		return fn_for_each_in_ns(peer_label, peerp,
				   match_label(profile, peerp, state, request,
					       sa));
	}

	return aa_profile_af_sk_perm(profile, sa, request, sk);
}

/**
 *
 * Requires: lock held on both @sk and @peer_sk
 */
int aa_unix_peer_perm(struct aa_label *label, const char *op, u32 request,
		      struct sock *sk, struct sock *peer_sk,
		      struct aa_label *peer_label)
{
	struct unix_sock *peeru = unix_sk(peer_sk);
	struct unix_sock *u = unix_sk(sk);

	AA_BUG(!label);
	AA_BUG(!sk);
	AA_BUG(!peer_sk);

	if (UNIX_FS(aa_sock(peeru)))
		return unix_fs_perm(op, request, label, peeru, 0);
	else if (UNIX_FS(aa_sock(u)))
		return unix_fs_perm(op, request, label, u, 0);
	else {
		struct aa_profile *profile;
		DEFINE_AUDIT_SK(sa, op, sk);
		aad(&sa)->net.peer_sk = peer_sk;

		/* TODO: ns!!! */
		if (!net_eq(sock_net(sk), sock_net(peer_sk))) {
			;
		}

		if (unconfined(label))
			return 0;

		return fn_for_each_confined(label, profile,
				profile_peer_perm(profile, op, request, sk,
						  peer_sk, peer_label, &sa));
	}
}


/* from net/unix/af_unix.c */
static void unix_state_double_lock(struct sock *sk1, struct sock *sk2)
{
	if (unlikely(sk1 == sk2) || !sk2) {
		unix_state_lock(sk1);
		return;
	}
	if (sk1 < sk2) {
		unix_state_lock(sk1);
		unix_state_lock_nested(sk2);
	} else {
		unix_state_lock(sk2);
		unix_state_lock_nested(sk1);
	}
}

static void unix_state_double_unlock(struct sock *sk1, struct sock *sk2)
{
	if (unlikely(sk1 == sk2) || !sk2) {
		unix_state_unlock(sk1);
		return;
	}
	unix_state_unlock(sk1);
	unix_state_unlock(sk2);
}

int aa_unix_file_perm(struct aa_label *label, const char *op, u32 request,
		      struct socket *sock)
{
	struct sock *peer_sk = NULL;
	u32 sk_req = request & ~NET_PEER_MASK;
	int error = 0;

	AA_BUG(!label);
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(sock->sk->sk_family != AF_UNIX);

	/* TODO: update sock label with new task label */
	unix_state_lock(sock->sk);
	peer_sk = unix_peer(sock->sk);
	if (peer_sk)
		sock_hold(peer_sk);
	if (!unix_connected(sock) && sk_req) {
		error = unix_label_sock_perm(label, op, sk_req, sock);
		if (!error) {
			// update label
		}
	}
	unix_state_unlock(sock->sk);
	if (!peer_sk)
		return error;

	unix_state_double_lock(sock->sk, peer_sk);
	if (UNIX_FS(sock->sk)) {
		error = unix_fs_perm(op, request, label, unix_sk(sock->sk),
				     PATH_SOCK_COND);
	} else if (UNIX_FS(peer_sk)) {
		error = unix_fs_perm(op, request, label, unix_sk(peer_sk),
				     PATH_SOCK_COND);
	} else {
		struct aa_sk_ctx *pctx = SK_CTX(peer_sk);
		if (sk_req)
			error = aa_unix_label_sk_perm(label, op, sk_req,
						      sock->sk);
		last_error(error,
			xcheck(aa_unix_peer_perm(label, op,
						 MAY_READ | MAY_WRITE,
						 sock->sk, peer_sk, NULL),
			       aa_unix_peer_perm(pctx->label, op,
						 MAY_READ | MAY_WRITE,
						 peer_sk, sock->sk, label)));
	}

	unix_state_double_unlock(sock->sk, peer_sk);
	sock_put(peer_sk);

	return error;
}
