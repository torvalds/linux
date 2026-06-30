/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor inet fine grained mediation
 *
 * Copyright 2024 Canonical Ltd.
 *
 */

#include <net/tcp_states.h>

#include "include/audit.h"
#include "include/af_inet.h"
#include "include/apparmor.h"
#include "include/file.h"
#include "include/label.h"
#include "include/net.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/cred.h"


static inline aa_state_t RULE_MEDIATES_SK(struct aa_ruleset *rules,
					  const struct sock *sk)
{
	return RULE_MEDIATES_NET(rules);
}


enum addr_type {
	ADDR_LOCAL = 0,
	ADDR_LOCAL_PRIV	= 1,
	ADDR_REMOTE = 2,
};

struct match_addr {
	const char *addrp;
	enum addr_type addrtype;
	int len;
	__be16 port;
};

struct stored_match_addr {
	union {
		struct sockaddr addr;
		struct sockaddr_in addr4;
		struct sockaddr_in6 addr6;
	};
	int addrlen;
	struct match_addr maddr;
};

static void set_ad_create(struct apparmor_audit_data *ad,
			 int family, int type, int protocol)
{
	ad->common.u.net->family = family;
	ad->net.type = type;
	ad->net.protocol = protocol;
}

static int set_ad_addr(struct apparmor_audit_data *ad,
		       u16 family, bool source, struct match_addr *maddr)
{
	ad->common.u.net->family = family;

	if (source) {
		ad->common.u.net->sport = maddr->port;
		if (maddr->addrp) {
			if (family == AF_INET)
				/* ad.u.net->v4info.saddr = addr4->sin_addr.s_addr; */
				ad->common.u.net->v4info.saddr = *(__be32 *)maddr->addrp;
			else
				/* ad.u.net->v4info.saddr = addr6->sin6_addr.s6_addr; */
				ad->common.u.net->v6info.saddr = *(struct in6_addr *)maddr->addrp;
		}
	} else {
		ad->common.u.net->dport = maddr->port;
		if (maddr->addrp) {
			if (family == AF_INET)
				/* ad.u.net->v4info.saddr = addr4->sin_addr.s_addr; */
				ad->common.u.net->v4info.daddr = *(__be32 *)maddr->addrp;
			else
				/* ad.u.net->v4info.saddr = addr6->sin6_addr.s6_addr; */
				ad->common.u.net->v6info.daddr = *(struct in6_addr *)maddr->addrp;
		}
	}
	return 0;
}

/* returns 0 on success
* raw_port - if set raw_port (protocol) when SOCK_RAW */
static int map_addr(struct sockaddr *addr, int addrlen, u16 raw_port,
		    enum addr_type addrtype, struct match_addr *maddr,
		    struct apparmor_audit_data *ad)
{
	struct sockaddr_in *addr4 = NULL;
	struct sockaddr_in6 *addr6 = NULL;

	AA_BUG(!addr);
	AA_BUG(!maddr);

	maddr->addrtype = addrtype;
	if (!addr || addrlen < offsetofend(struct sockaddr, sa_family)) {
		maddr->addrp = NULL;
		maddr->port = 0;
		maddr->len = 0;
		return 0;
	}

	/*
	 * its possibly to have sk->sk_family == PF_INET6 and
	 * addr->sa_family == AF_INET. sk_family is used for socket
	 * mediation, sa_family for when we have address ...
	 */
	switch (addr->sa_family) {
	case AF_INET:
		addr4 = (struct sockaddr_in *)addr;
		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		maddr->port = addr4->sin_port;
		maddr->addrp = (char *)&addr4->sin_addr.s_addr;
		maddr->len = 4;
		break;
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)addr;
		if (addrlen < SIN6_LEN_RFC2133)
			return -EINVAL;
		maddr->port = addr6->sin6_port;
		maddr->addrp = (char *)&addr6->sin6_addr.s6_addr;
		maddr->len = 16;
		break;
	default:
		return -EAFNOSUPPORT;
	}
	/* per ip spec, && sk->sk_type == SOCK_RAW*/
	if (raw_port && addrtype != ADDR_REMOTE)
		maddr->port = htons(raw_port);
	if (ad)
		set_ad_addr(ad, addr->sa_family, addrtype != ADDR_REMOTE, maddr);

	return 0;
}

/* -ENOTCONN if not connected */
static int map_sock_addr(struct socket *sock, enum addr_type addrtype,
			 struct stored_match_addr *maddr,
			 struct apparmor_audit_data *ad)
{
	/* do we need early bailout for !family ... */
	maddr->addrlen = sock->ops->getname(sock, (struct sockaddr *) &maddr->addr, addrtype != ADDR_REMOTE ? 0 : 1);
	if (maddr->addrlen == -ENOTCONN) {
		maddr->addrlen = 0;
		return map_addr(NULL, 0, 0, addrtype, &maddr->maddr, ad);
	} else if (maddr->addrlen < 0)
		return maddr->addrlen;
	return map_addr(&maddr->addr, maddr->addrlen, 0, addrtype,
			&maddr->maddr, ad);
}

/* TODO: combine with connect map addr */
/* TODO: raw_port */
static int bind_map_addr(const struct sock *sk, struct sockaddr *addr,
			 int addrlen,
			 struct match_addr *maddr,
			 struct apparmor_audit_data *ad)
{
	struct sockaddr_in *addr4 = NULL;
	struct sockaddr_in6 *addr6 = NULL;
	u16 family;

	AA_BUG(!sk);
	AA_BUG(!addr);
	AA_BUG(!maddr);

	if (addrlen < offsetofend(struct sockaddr, sa_family))
		return -EINVAL;

	maddr->addrtype = ADDR_LOCAL;
	/*
	 * its possibly to have sk->sk_family == PF_INET6 and
	 * addr->sa_family == AF_INET. sk_family is used for socket
	 * mediation, sa_family for when we have address ...
	 */
	family = addr->sa_family;
	switch (addr->sa_family) {
	case AF_UNSPEC:
		if (sk->sk_family == PF_INET6) {
			/* Length check from inet6_bind_sk() */
			if (addrlen < SIN6_LEN_RFC2133)
				return -EINVAL;
			/* Family check from __inet6_bind() */
			return -EAFNOSUPPORT;
		}
		/* see __inet_bind(), we only want to allow
		 * AF_UNSPEC if the address is INADDR_ANY
		 */
		addr4 = (struct sockaddr_in *)addr;
		if (addr4->sin_addr.s_addr != htonl(INADDR_ANY))
			return -EAFNOSUPPORT;
		family = AF_INET;
		fallthrough;
	case AF_INET:
		addr4 = (struct sockaddr_in *)addr;
		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		maddr->port = addr4->sin_port;
		maddr->addrp = (char *)&addr4->sin_addr.s_addr;
		maddr->len = 4;
		break;
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)addr;
		if (addrlen < SIN6_LEN_RFC2133)
			return -EINVAL;
		maddr->port = addr6->sin6_port;
		maddr->addrp = (char *)&addr6->sin6_addr.s6_addr;
		maddr->len = 16;
		break;
	default:
		return -EAFNOSUPPORT;
	}

	if (ad)
		set_ad_addr(ad, family, true, maddr);

	return 0;
}


static inline int profile_sk_perm(struct aa_profile *profile, u32 request,
				  const struct sock *sk,
				  struct match_addr *maddr,
				  struct apparmor_audit_data *ad)
{
	AA_BUG(!profile);
	AA_BUG(!sk);

	return aa_profile_af_sk_perm(profile, ad, request, sk);
}

/* no kernel_t bailout */
static int profile_create_perm(struct aa_profile *profile, int family,
			       int type, int protocol,
			       struct apparmor_audit_data *ad)
{
	AA_BUG(!profile);

	return aa_profile_af_perm(profile, ad, AA_MAY_CREATE, family, type,
				  protocol);
}


/* sendmsg/rcvmsg/connect */
static int profile_remote_perm(struct aa_profile *profile,
			       const struct sock *sk,
			       u32 request, struct match_addr *raddr,
			       struct match_addr *laddr,
			       struct apparmor_audit_data *ad)
{
	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(!raddr);
	AA_BUG(!laddr);
	AA_BUG(sk->sk_family != PF_INET && sk->sk_family != PF_INET6,
	       "family=%d", sk->sk_family);

	return aa_profile_af_sk_perm(profile, ad, request, sk);
}

static int profile_bind_perm(struct aa_profile *profile,
			     const struct sock *sk,
			     struct match_addr *maddr,
			     struct apparmor_audit_data *ad)
{
	return aa_profile_af_sk_perm(profile, ad, AA_MAY_BIND, sk);

}

static int profile_listen_perm(struct aa_profile *profile,
			       const struct sock *sk,
			       struct match_addr *maddr, int backlog,
			       struct apparmor_audit_data *ad)
{
	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(!maddr);
	AA_BUG(sk->sk_family != PF_INET && sk->sk_family != PF_INET6,
	       "family=%d", sk->sk_family);

	return aa_profile_af_sk_perm(profile, ad, AA_MAY_LISTEN, sk);
}

static inline int profile_accept_perm(struct aa_profile *profile,
				      const struct sock *sk,
				      struct match_addr *maddr,
				      const struct sock *newsk,
				      struct apparmor_audit_data *ad)
{
	AA_BUG(!profile);
	AA_BUG(!sk);
	/* AA_BUG(!newsk);  newsk can be null here, since not using atm ... */
	AA_BUG(!maddr);
	AA_BUG(sk->sk_family != PF_INET && sk->sk_family != PF_INET6,
	       "family=%d", sk->sk_family);

	return aa_profile_af_sk_perm(profile, ad, AA_MAY_ACCEPT, sk);
}

/* getopt/setopt */
static int profile_opt_perm(struct aa_profile *profile, u32 request,
			    const struct sock *sk, struct match_addr *maddr,
			    int level, int optname,
			    struct apparmor_audit_data *ad)
{
	AA_BUG(!profile);
	AA_BUG(!sk);
	AA_BUG(!maddr);
	AA_BUG(sk->sk_family != PF_INET && sk->sk_family != PF_INET6,
	       "family=%d", sk->sk_family);

	return aa_profile_af_sk_perm(profile, ad, request, sk);
}

/* ---------------------------------------------------------------------- */

// TODO: cleanup init to use recursion, so we can have N init fns, in 1 macro
// TODO: lift DEFINE_AUDIT out of macro into init fn???

/* no kernel_t bailout */
#define label_sk_has_perm2(CRED, LABEL, SOCKSK, OP, REQUEST, PROFILE, AAD, XXXX, YYYY, CALLBACKFN) \
({								\
	int __EERROR = 0;					\
	if (label_mediates(LABEL, AA_CLASS_NET)) {		\
		struct aa_profile *PROFILE;			\
		DEFINE_AUDIT_SK(AAD, OP, CRED, SOCKSK);		\
		(AAD).subj_cred = (CRED);			\
		(AAD).request = (REQUEST);			\
		__EERROR = (XXXX);				\
		if (__EERROR == 0) {				\
			__EERROR = (YYYY);			\
			if (__EERROR == 0) {			\
				__EERROR = fn_for_each(LABEL, PROFILE,	\
						       (CALLBACKFN));	\
			}						\
		}							\
	}							\
	__EERROR;						\
})

/* no kernel_t bailout */
#define label_sk_has_perm(CRED, LABEL, SOCKSK, OP, REQUEST, PROFILE, AAD, CALLBACKFN) \
	label_sk_has_perm2(CRED, LABEL, SOCKSK, OP, REQUEST, PROFILE, AAD, \
			   0, 0, CALLBACKFN)

/* no kernel_t bailout */
#define label_sk_has_perm1(CRED, LABEL, SOCKSK, OP, REQUEST, PROFILE, AAD, XXXX, CALLBACKFN) \
	label_sk_has_perm2(CRED, LABEL, SOCKSK, OP, REQUEST, PROFILE, AAD, \
			   XXXX, 0, CALLBACKFN)


/* Early bailout for kernel_t - 2 init args before callback */
#define sk_has_perm2(SOCKSK, OP, REQUEST, PROFILE, AAD, XXXXY, YYYYX, CALLBACKFN) \
({									\
	struct aa_label *__label;					\
	struct aa_sk_ctx *__ctx = aa_sock(SOCKSK);			\
	int __ERROR = 0;						\
	bool __needput;						\
	if (rcu_access_pointer(__ctx->label) != kernel_t) {		\
									\
		__label = begin_current_label_crit_section(&__needput);	\
                __ERROR = label_sk_has_perm2(current_cred(), __label, SOCKSK, OP, REQUEST, PROFILE, AAD, XXXXY, YYYYX, CALLBACKFN); \
		end_current_label_crit_section(__label, __needput);	\
	}								\
	__ERROR;							\
})

/* Early bailout for kernel_t - no init args before callback */
#define sk_has_perm(SOCKSK, OP, REQUEST, PROFILE, AAD, CALLBACKFN)	\
	sk_has_perm2(SOCKSK, OP, REQUEST, PROFILE, AAD, 0, 0, CALLBACKFN)


/* Early bailout for kernel_t - 1 init arg before callback */
#define sk_has_perm1(SOCKSK, OP, REQUEST, PROFILE, AAD, XXXXY, CALLBACKFN) \
	sk_has_perm2(SOCKSK, OP, REQUEST, PROFILE, AAD, XXXXY, 0, CALLBACKFN)



/* no kernel_t early bailout */
/* NOTE: already lifted label_mediates into lsm.c */
int aa_inet_create_perm(struct aa_label *label, int family, int type,
			int protocol)
{
	struct aa_profile *profile;
	int error = 0;
	DEFINE_AUDIT_NET(ad, OP_CREATE, current_cred(), NULL, family, type,
			 protocol);

	ad.subj_cred = current_cred();
	set_ad_create(&ad, family, type, protocol);
	error = fn_for_each(label, profile,
			    profile_create_perm(profile, family, type,
						protocol, &ad));

	return error;
}

int aa_inet_bind_perm(struct socket *sock, struct sockaddr *addr,
		      int addrlen)
{
	struct match_addr maddr;

	return sk_has_perm1(sock->sk, OP_BIND, AA_MAY_BIND, profile, ad,
			    bind_map_addr(sock->sk, addr, addrlen, &maddr,
					  &ad),
			    profile_bind_perm(profile, sock->sk, &maddr, &ad));
}

int aa_inet_connect_perm(struct socket *sock, struct sockaddr *addr,
			 int addrlen)
{
	struct stored_match_addr laddr;
	struct match_addr raddr;

	/* disconnect socket */
	if (addrlen < offsetofend(struct sockaddr, sa_family))
		return -EINVAL;
	if (addr->sa_family == AF_UNSPEC)
		return 0;

	/* do we need early bailout for !family ... */
	return sk_has_perm2(sock->sk, OP_CONNECT, AA_MAY_CONNECT, profile, ad,
			    map_sock_addr(sock, ADDR_LOCAL, &laddr, &ad),
			    map_addr(addr, addrlen, 0, ADDR_REMOTE, &raddr,
				     &ad),
			    profile_remote_perm(profile, sock->sk,
						AA_MAY_CONNECT, &raddr,
						&laddr.maddr, &ad));
}

int aa_inet_listen_perm(struct socket *sock, int backlog)
{
	struct stored_match_addr maddr;

	/* do we need early bailout for !family ... */
	return sk_has_perm1(sock->sk, OP_LISTEN, AA_MAY_LISTEN, profile, ad,
			    map_sock_addr(sock, ADDR_LOCAL, &maddr, &ad),
			    profile_listen_perm(profile, sock->sk, &maddr.maddr,
						backlog, &ad));
}

/* ability of sock to connect, not peer address binding */
int aa_inet_accept_perm(struct socket *sock, struct socket *newsock)
{
	struct stored_match_addr maddr;
	int error;

	error = sk_has_perm1(sock->sk, OP_ACCEPT, AA_MAY_ACCEPT, profile, ad,
			     map_sock_addr(sock, ADDR_LOCAL, &maddr, &ad),
			     profile_accept_perm(profile, sock->sk,
						 &maddr.maddr,
						 newsock->sk, &ad));

	/* selinux updates inode - need to investigate this more */
	return error;
}

/* sendmsg, recvmsg. */
int aa_inet_msg_perm(const char *op, u32 request, struct socket *sock,
		     struct msghdr *msg, int size)
{
	struct stored_match_addr laddr;
	struct match_addr raddr;

	/* do we need early bailout for !family ... */
	return sk_has_perm2(sock->sk, op, request, profile, ad,
			    map_sock_addr(sock, ADDR_LOCAL, &laddr, &ad),
			    map_addr(msg->msg_name, msg->msg_namelen, 0,
				     ADDR_REMOTE, &raddr, &ad),
			    profile_remote_perm(profile, sock->sk, request,
						&raddr, &laddr.maddr, &ad));
}

/* getopt, setopt */
int aa_inet_opt_perm(const char *op, u32 request, struct socket *sock,
		     int level, int optname)
{
	struct stored_match_addr maddr;

	return sk_has_perm1(sock->sk, op, request, profile, ad,
			    map_sock_addr(sock, ADDR_LOCAL, &maddr, &ad),
			    profile_opt_perm(profile, request, sock->sk,
					    &maddr.maddr, level, optname, &ad));
}

static int inet_label_sock_perm(const struct cred *cred, struct aa_label *label,
				const char *op, u32 request,
				struct socket *sock)
{
	struct stored_match_addr maddr;

	return label_sk_has_perm1(cred, label, sock->sk, op, request, profile,
				  ad,
			map_sock_addr(sock, ADDR_LOCAL, &maddr, &ad),
			profile_sk_perm(profile, request, sock->sk,
					&maddr.maddr, &ad));
}

/* revalidation, get/set attr/getsockname/peername */
int aa_inet_sock_perm(const char *op, u32 request, struct socket *sock)
{
	struct aa_sk_ctx *ctx = aa_sock(sock->sk);
	struct aa_label *label;
	bool needput;
	int error;

	if (rcu_access_pointer(ctx->label) == kernel_t)
		return 0;

	label = begin_current_label_crit_section(&needput);
	error = inet_label_sock_perm(current_cred(), label, op, request, sock);
	end_current_label_crit_section(label, needput);

	return error;
}

int aa_inet_file_perm(const struct cred *subj_cred, struct aa_label *label,
		      const char *op, u32 request, struct socket *sock)
{
	u32 sk_req = request & ~NET_PEER_MASK;
	struct stored_match_addr laddr;
	const struct sock *sk = sock->sk;
	int error = 0;

	AA_BUG(!label);
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(sk->sk_family != PF_INET && sk->sk_family != PF_INET6,
	       "family=%d", sk->sk_family);

	/* access to the local sock */
	error = label_sk_has_perm1(subj_cred, label, sock->sk, op, request,
				   profile, ad,
			map_sock_addr(sock, ADDR_LOCAL, &laddr, &ad),
			profile_sk_perm(profile, sk_req, sock->sk, &laddr.maddr,
					&ad));

	if (!error) {
		struct stored_match_addr raddr;

		/* TODO: have ad here: instead of in CB so we do have to redo */
		error = map_sock_addr(sock, ADDR_REMOTE, &raddr, NULL);
		if (!error && raddr.maddr.addrp) {
			error = label_sk_has_perm1(subj_cred, label, sock->sk,
						   op, request, profile, ad,
					set_ad_addr(&ad, raddr.addr.sa_family,
						    false, &raddr.maddr),
					profile_remote_perm(profile, sock->sk,
							    request,
							    &raddr.maddr,
							    &laddr.maddr, &ad));
		}
	}

	return error;
}
