// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * tcp_diag.c	Module for monitoring TCP transport protocols sockets.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>

#include <linux/tcp.h>

#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include <net/inet_timewait_sock.h>
#include <net/netlink.h>
#include <net/tcp.h>

static void tcp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			      void *_info)
{
	struct tcp_info *info = _info;

	if (inet_sk_state_load(sk) == TCP_LISTEN) {
		r->idiag_rqueue = READ_ONCE(sk->sk_ack_backlog);
		r->idiag_wqueue = READ_ONCE(sk->sk_max_ack_backlog);
	} else if (sk->sk_type == SOCK_STREAM) {
		const struct tcp_sock *tp = tcp_sk(sk);

		r->idiag_rqueue = max_t(int, READ_ONCE(tp->rcv_nxt) -
					     READ_ONCE(tp->copied_seq), 0);
		r->idiag_wqueue = READ_ONCE(tp->write_seq) - tp->snd_una;
	}
	if (info)
		tcp_get_info(sk, info);
}

#ifdef CONFIG_TCP_MD5SIG
static void tcp_diag_md5sig_fill(struct tcp_diag_md5sig *info,
				 const struct tcp_md5sig_key *key)
{
	info->tcpm_family = key->family;
	info->tcpm_prefixlen = key->prefixlen;
	info->tcpm_keylen = key->keylen;
	memcpy(info->tcpm_key, key->key, key->keylen);

	if (key->family == AF_INET)
		info->tcpm_addr[0] = key->addr.a4.s_addr;
	#if IS_ENABLED(CONFIG_IPV6)
	else if (key->family == AF_INET6)
		memcpy(&info->tcpm_addr, &key->addr.a6,
		       sizeof(info->tcpm_addr));
	#endif
}

static int tcp_diag_put_md5sig(struct sk_buff *skb,
			       const struct tcp_md5sig_info *md5sig)
{
	const struct tcp_md5sig_key *key;
	struct tcp_diag_md5sig *info;
	struct nlattr *attr;
	int md5sig_count = 0;

	hlist_for_each_entry_rcu(key, &md5sig->head, node)
		md5sig_count++;
	if (md5sig_count == 0)
		return 0;

	attr = nla_reserve(skb, INET_DIAG_MD5SIG,
			   md5sig_count * sizeof(struct tcp_diag_md5sig));
	if (!attr)
		return -EMSGSIZE;

	info = nla_data(attr);
	memset(info, 0, md5sig_count * sizeof(struct tcp_diag_md5sig));
	hlist_for_each_entry_rcu(key, &md5sig->head, node) {
		tcp_diag_md5sig_fill(info++, key);
		if (--md5sig_count == 0)
			break;
	}

	return 0;
}
#endif

static int tcp_diag_put_ulp(struct sk_buff *skb, struct sock *sk,
			    const struct tcp_ulp_ops *ulp_ops, bool net_admin)
{
	struct nlattr *nest;
	int err;

	nest = nla_nest_start_noflag(skb, INET_DIAG_ULP_INFO);
	if (!nest)
		return -EMSGSIZE;

	err = nla_put_string(skb, INET_ULP_INFO_NAME, ulp_ops->name);
	if (err)
		goto nla_failure;

	if (ulp_ops->get_info)
		err = ulp_ops->get_info(sk, skb, net_admin);
	if (err)
		goto nla_failure;

	nla_nest_end(skb, nest);
	return 0;

nla_failure:
	nla_nest_cancel(skb, nest);
	return err;
}

static int tcp_diag_get_aux(struct sock *sk, bool net_admin,
			    struct sk_buff *skb)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	const struct tcp_ulp_ops *ulp_ops;
	int err = 0;

#ifdef CONFIG_TCP_MD5SIG
	if (net_admin) {
		struct tcp_md5sig_info *md5sig;

		rcu_read_lock();
		md5sig = rcu_dereference(tcp_sk(sk)->md5sig_info);
		if (md5sig)
			err = tcp_diag_put_md5sig(skb, md5sig);
		rcu_read_unlock();
		if (err < 0)
			return err;
	}
#endif

	ulp_ops = icsk->icsk_ulp_ops;
	if (ulp_ops) {
		err = tcp_diag_put_ulp(skb, sk, ulp_ops, net_admin);
		if (err < 0)
			return err;
	}

	return 0;
}

static size_t tcp_diag_get_aux_size(struct sock *sk, bool net_admin)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	size_t size = 0;

#ifdef CONFIG_TCP_MD5SIG
	if (net_admin && sk_fullsock(sk)) {
		const struct tcp_md5sig_info *md5sig;
		const struct tcp_md5sig_key *key;
		size_t md5sig_count = 0;

		rcu_read_lock();
		md5sig = rcu_dereference(tcp_sk(sk)->md5sig_info);
		if (md5sig) {
			hlist_for_each_entry_rcu(key, &md5sig->head, node)
				md5sig_count++;
		}
		rcu_read_unlock();
		size += nla_total_size(md5sig_count *
				       sizeof(struct tcp_diag_md5sig));
	}
#endif

	if (sk_fullsock(sk)) {
		const struct tcp_ulp_ops *ulp_ops;

		ulp_ops = icsk->icsk_ulp_ops;
		if (ulp_ops) {
			size += nla_total_size(0) +
				nla_total_size(TCP_ULP_NAME_MAX);
			if (ulp_ops->get_info_size)
				size += ulp_ops->get_info_size(sk, net_admin);
		}
	}

	return size
		+ nla_total_size(sizeof(struct tcp_info))
		+ nla_total_size(sizeof(struct inet_diag_msg))
		+ inet_diag_msg_attrs_size()
		+ nla_total_size(sizeof(struct inet_diag_meminfo))
		+ nla_total_size(SK_MEMINFO_VARS * sizeof(u32))
		+ nla_total_size(TCP_CA_NAME_MAX)
		+ nla_total_size(sizeof(struct tcpvegas_info))
		+ 64;
}

static int tcp_twsk_diag_fill(struct sock *sk,
			      struct sk_buff *skb,
			      struct netlink_callback *cb,
			      u16 nlmsg_flags, bool net_admin)
{
	struct inet_timewait_sock *tw = inet_twsk(sk);
	struct inet_diag_msg *r;
	struct nlmsghdr *nlh;
	long tmo;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid,
			cb->nlh->nlmsg_seq, cb->nlh->nlmsg_type,
			sizeof(*r), nlmsg_flags);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	DEBUG_NET_WARN_ON_ONCE(tw->tw_state != TCP_TIME_WAIT);

	inet_diag_msg_common_fill(r, sk);
	r->idiag_retrans      = 0;

	r->idiag_state	      = READ_ONCE(tw->tw_substate);
	r->idiag_timer	      = 3;
	tmo = tw->tw_timer.expires - jiffies;
	r->idiag_expires      = jiffies_delta_to_msecs(tmo);
	r->idiag_rqueue	      = 0;
	r->idiag_wqueue	      = 0;
	r->idiag_uid	      = 0;
	r->idiag_inode	      = 0;

	if (net_admin && nla_put_u32(skb, INET_DIAG_MARK,
				     tw->tw_mark)) {
		nlmsg_cancel(skb, nlh);
		return -EMSGSIZE;
	}

	nlmsg_end(skb, nlh);
	return 0;
}

static int tcp_req_diag_fill(struct sock *sk, struct sk_buff *skb,
			     struct netlink_callback *cb,
			     u16 nlmsg_flags, bool net_admin)
{
	struct request_sock *reqsk = inet_reqsk(sk);
	struct inet_diag_msg *r;
	struct nlmsghdr *nlh;
	long tmo;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			cb->nlh->nlmsg_type, sizeof(*r), nlmsg_flags);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	inet_diag_msg_common_fill(r, sk);
	r->idiag_state = TCP_SYN_RECV;
	r->idiag_timer = 1;
	r->idiag_retrans = READ_ONCE(reqsk->num_retrans);

	BUILD_BUG_ON(offsetof(struct inet_request_sock, ir_cookie) !=
		     offsetof(struct sock, sk_cookie));

	tmo = READ_ONCE(inet_reqsk(sk)->rsk_timer.expires) - jiffies;
	r->idiag_expires = jiffies_delta_to_msecs(tmo);
	r->idiag_rqueue	= 0;
	r->idiag_wqueue	= 0;
	r->idiag_uid	= 0;
	r->idiag_inode	= 0;

	if (net_admin && nla_put_u32(skb, INET_DIAG_MARK,
				     inet_rsk(reqsk)->ir_mark)) {
		nlmsg_cancel(skb, nlh);
		return -EMSGSIZE;
	}

	nlmsg_end(skb, nlh);
	return 0;
}

static int sk_diag_fill(struct sock *sk, struct sk_buff *skb,
			struct netlink_callback *cb,
			const struct inet_diag_req_v2 *r,
			u16 nlmsg_flags, bool net_admin)
{
	if (sk->sk_state == TCP_TIME_WAIT)
		return tcp_twsk_diag_fill(sk, skb, cb, nlmsg_flags, net_admin);

	if (sk->sk_state == TCP_NEW_SYN_RECV)
		return tcp_req_diag_fill(sk, skb, cb, nlmsg_flags, net_admin);

	return inet_sk_diag_fill(sk, inet_csk(sk), skb, cb, r, nlmsg_flags,
				 net_admin);
}

static void twsk_build_assert(void)
{
	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_family) !=
		     offsetof(struct sock, sk_family));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_num) !=
		     offsetof(struct inet_sock, inet_num));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_dport) !=
		     offsetof(struct inet_sock, inet_dport));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_rcv_saddr) !=
		     offsetof(struct inet_sock, inet_rcv_saddr));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_daddr) !=
		     offsetof(struct inet_sock, inet_daddr));

#if IS_ENABLED(CONFIG_IPV6)
	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_v6_rcv_saddr) !=
		     offsetof(struct sock, sk_v6_rcv_saddr));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_v6_daddr) !=
		     offsetof(struct sock, sk_v6_daddr));
#endif
}

static void tcp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			  const struct inet_diag_req_v2 *r)
{
	bool net_admin = netlink_net_capable(cb->skb, CAP_NET_ADMIN);
	struct inet_diag_dump_data *cb_data = cb->data;
	struct net *net = sock_net(skb->sk);
	u32 idiag_states = r->idiag_states;
	struct inet_hashinfo *hashinfo;
	int i, num, s_i, s_num;
	struct sock *sk;

	hashinfo = net->ipv4.tcp_death_row.hashinfo;
	if (idiag_states & TCPF_SYN_RECV)
		idiag_states |= TCPF_NEW_SYN_RECV;
	s_i = cb->args[1];
	s_num = num = cb->args[2];

	if (cb->args[0] == 0) {
		if (!(idiag_states & TCPF_LISTEN) || r->id.idiag_dport)
			goto skip_listen_ht;

		for (i = s_i; i <= hashinfo->lhash2_mask; i++) {
			struct inet_listen_hashbucket *ilb;
			struct hlist_nulls_node *node;

			num = 0;
			ilb = &hashinfo->lhash2[i];

			if (hlist_nulls_empty(&ilb->nulls_head)) {
				s_num = 0;
				continue;
			}
			spin_lock(&ilb->lock);
			sk_nulls_for_each(sk, node, &ilb->nulls_head) {
				struct inet_sock *inet = inet_sk(sk);

				if (!net_eq(sock_net(sk), net))
					continue;

				if (num < s_num) {
					num++;
					continue;
				}

				if (r->sdiag_family != AF_UNSPEC &&
				    sk->sk_family != r->sdiag_family)
					goto next_listen;

				if (r->id.idiag_sport != inet->inet_sport &&
				    r->id.idiag_sport)
					goto next_listen;

				if (!inet_diag_bc_sk(cb_data, sk))
					goto next_listen;

				if (inet_sk_diag_fill(sk, inet_csk(sk), skb,
						      cb, r, NLM_F_MULTI,
						      net_admin) < 0) {
					spin_unlock(&ilb->lock);
					goto done;
				}

next_listen:
				++num;
			}
			spin_unlock(&ilb->lock);

			s_num = 0;
		}
skip_listen_ht:
		cb->args[0] = 1;
		s_i = num = s_num = 0;
	}

/* Process a maximum of SKARR_SZ sockets at a time when walking hash buckets
 * with bh disabled.
 */
#define SKARR_SZ 16

	/* Dump bound but inactive (not listening, connecting, etc.) sockets */
	if (cb->args[0] == 1) {
		if (!(idiag_states & TCPF_BOUND_INACTIVE))
			goto skip_bind_ht;

		for (i = s_i; i < hashinfo->bhash_size; i++) {
			struct inet_bind_hashbucket *ibb;
			struct inet_bind2_bucket *tb2;
			struct sock *sk_arr[SKARR_SZ];
			int num_arr[SKARR_SZ];
			int idx, accum, res;

resume_bind_walk:
			num = 0;
			accum = 0;
			ibb = &hashinfo->bhash2[i];

			if (hlist_empty(&ibb->chain)) {
				s_num = 0;
				continue;
			}
			spin_lock_bh(&ibb->lock);
			inet_bind_bucket_for_each(tb2, &ibb->chain) {
				if (!net_eq(ib2_net(tb2), net))
					continue;

				sk_for_each_bound(sk, &tb2->owners) {
					struct inet_sock *inet = inet_sk(sk);

					if (num < s_num)
						goto next_bind;

					if (sk->sk_state != TCP_CLOSE ||
					    !inet->inet_num)
						goto next_bind;

					if (r->sdiag_family != AF_UNSPEC &&
					    r->sdiag_family != sk->sk_family)
						goto next_bind;

					if (!inet_diag_bc_sk(cb_data, sk))
						goto next_bind;

					sock_hold(sk);
					num_arr[accum] = num;
					sk_arr[accum] = sk;
					if (++accum == SKARR_SZ)
						goto pause_bind_walk;
next_bind:
					num++;
				}
			}
pause_bind_walk:
			spin_unlock_bh(&ibb->lock);

			res = 0;
			for (idx = 0; idx < accum; idx++) {
				if (res >= 0) {
					res = inet_sk_diag_fill(sk_arr[idx],
								NULL, skb, cb,
								r, NLM_F_MULTI,
								net_admin);
					if (res < 0)
						num = num_arr[idx];
				}
				sock_put(sk_arr[idx]);
			}
			if (res < 0)
				goto done;

			cond_resched();

			if (accum == SKARR_SZ) {
				s_num = num + 1;
				goto resume_bind_walk;
			}

			s_num = 0;
		}
skip_bind_ht:
		cb->args[0] = 2;
		s_i = num = s_num = 0;
	}

	if (!(idiag_states & ~TCPF_LISTEN))
		goto out;

	for (i = s_i; i <= hashinfo->ehash_mask; i++) {
		struct inet_ehash_bucket *head = &hashinfo->ehash[i];
		spinlock_t *lock = inet_ehash_lockp(hashinfo, i);
		struct hlist_nulls_node *node;
		struct sock *sk_arr[SKARR_SZ];
		int num_arr[SKARR_SZ];
		int idx, accum, res;

		if (hlist_nulls_empty(&head->chain))
			continue;

		if (i > s_i)
			s_num = 0;

next_chunk:
		num = 0;
		accum = 0;
		spin_lock_bh(lock);
		sk_nulls_for_each(sk, node, &head->chain) {
			int state;

			if (!net_eq(sock_net(sk), net))
				continue;
			if (num < s_num)
				goto next_normal;
			state = (sk->sk_state == TCP_TIME_WAIT) ?
				READ_ONCE(inet_twsk(sk)->tw_substate) : sk->sk_state;
			if (!(idiag_states & (1 << state)))
				goto next_normal;
			if (r->sdiag_family != AF_UNSPEC &&
			    sk->sk_family != r->sdiag_family)
				goto next_normal;
			if (r->id.idiag_sport != htons(sk->sk_num) &&
			    r->id.idiag_sport)
				goto next_normal;
			if (r->id.idiag_dport != sk->sk_dport &&
			    r->id.idiag_dport)
				goto next_normal;
			twsk_build_assert();

			if (!inet_diag_bc_sk(cb_data, sk))
				goto next_normal;

			if (!refcount_inc_not_zero(&sk->sk_refcnt))
				goto next_normal;

			num_arr[accum] = num;
			sk_arr[accum] = sk;
			if (++accum == SKARR_SZ)
				break;
next_normal:
			++num;
		}
		spin_unlock_bh(lock);

		res = 0;
		for (idx = 0; idx < accum; idx++) {
			if (res >= 0) {
				res = sk_diag_fill(sk_arr[idx], skb, cb, r,
						   NLM_F_MULTI, net_admin);
				if (res < 0)
					num = num_arr[idx];
			}
			sock_gen_put(sk_arr[idx]);
		}
		if (res < 0)
			break;

		cond_resched();

		if (accum == SKARR_SZ) {
			s_num = num + 1;
			goto next_chunk;
		}
	}

done:
	cb->args[1] = i;
	cb->args[2] = num;
out:
	;
}

static struct sock *tcp_diag_find_one_icsk(struct net *net,
					   const struct inet_diag_req_v2 *req)
{
	struct sock *sk;

	rcu_read_lock();
	if (req->sdiag_family == AF_INET) {
		sk = inet_lookup(net, NULL, 0, req->id.idiag_dst[0],
				 req->id.idiag_dport, req->id.idiag_src[0],
				 req->id.idiag_sport, req->id.idiag_if);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (req->sdiag_family == AF_INET6) {
		if (ipv6_addr_v4mapped((struct in6_addr *)req->id.idiag_dst) &&
		    ipv6_addr_v4mapped((struct in6_addr *)req->id.idiag_src))
			sk = inet_lookup(net, NULL, 0, req->id.idiag_dst[3],
					 req->id.idiag_dport, req->id.idiag_src[3],
					 req->id.idiag_sport, req->id.idiag_if);
		else
			sk = inet6_lookup(net, NULL, 0,
					  (struct in6_addr *)req->id.idiag_dst,
					  req->id.idiag_dport,
					  (struct in6_addr *)req->id.idiag_src,
					  req->id.idiag_sport,
					  req->id.idiag_if);
#endif
	} else {
		rcu_read_unlock();
		return ERR_PTR(-EINVAL);
	}
	rcu_read_unlock();
	if (!sk)
		return ERR_PTR(-ENOENT);

	if (sock_diag_check_cookie(sk, req->id.idiag_cookie)) {
		sock_gen_put(sk);
		return ERR_PTR(-ENOENT);
	}

	return sk;
}

static int tcp_diag_dump_one(struct netlink_callback *cb,
			     const struct inet_diag_req_v2 *req)
{
	struct sk_buff *in_skb = cb->skb;
	struct sk_buff *rep;
	struct sock *sk;
	struct net *net;
	bool net_admin;
	int err;

	net = sock_net(in_skb->sk);
	sk = tcp_diag_find_one_icsk(net, req);
	if (IS_ERR(sk))
		return PTR_ERR(sk);

	net_admin = netlink_net_capable(in_skb, CAP_NET_ADMIN);
	rep = nlmsg_new(tcp_diag_get_aux_size(sk, net_admin), GFP_KERNEL);
	if (!rep) {
		err = -ENOMEM;
		goto out;
	}

	err = sk_diag_fill(sk, rep, cb, req, 0, net_admin);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		nlmsg_free(rep);
		goto out;
	}
	err = nlmsg_unicast(net->diag_nlsk, rep, NETLINK_CB(in_skb).portid);

out:
	if (sk)
		sock_gen_put(sk);

	return err;
}

#ifdef CONFIG_INET_DIAG_DESTROY
static int tcp_diag_destroy(struct sk_buff *in_skb,
			    const struct inet_diag_req_v2 *req)
{
	struct net *net = sock_net(in_skb->sk);
	struct sock *sk;
	int err;

	sk = tcp_diag_find_one_icsk(net, req);
	if (IS_ERR(sk))
		return PTR_ERR(sk);

	err = sock_diag_destroy(sk, ECONNABORTED);

	sock_gen_put(sk);

	return err;
}
#endif

static const struct inet_diag_handler tcp_diag_handler = {
	.owner			= THIS_MODULE,
	.dump			= tcp_diag_dump,
	.dump_one		= tcp_diag_dump_one,
	.idiag_get_info		= tcp_diag_get_info,
	.idiag_get_aux		= tcp_diag_get_aux,
	.idiag_type		= IPPROTO_TCP,
	.idiag_info_size	= sizeof(struct tcp_info),
#ifdef CONFIG_INET_DIAG_DESTROY
	.destroy		= tcp_diag_destroy,
#endif
};

static int __init tcp_diag_init(void)
{
	return inet_diag_register(&tcp_diag_handler);
}

static void __exit tcp_diag_exit(void)
{
	inet_diag_unregister(&tcp_diag_handler);
}

module_init(tcp_diag_init);
module_exit(tcp_diag_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP socket monitoring via SOCK_DIAG");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-6 /* AF_INET - IPPROTO_TCP */);
