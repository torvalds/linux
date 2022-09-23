// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  AF_SMC protocol family socket handler keeping the AF_INET sock address type
 *  applies to SOCK_STREAM sockets only
 *  offers an alternative communication option for TCP-protocol sockets
 *  applicable with RoCE-cards only
 *
 *  Initial restrictions:
 *    - support for alternate links postponed
 *
 *  Copyright IBM Corp. 2016, 2018
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 *              based on prototype from Frank Blaschka
 */

#define KMSG_COMPONENT "smc"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/socket.h>
#include <linux/workqueue.h>
#include <linux/in.h>
#include <linux/sched/signal.h>
#include <linux/if_vlan.h>
#include <linux/rcupdate_wait.h>
#include <linux/ctype.h>

#include <net/sock.h>
#include <net/tcp.h>
#include <net/smc.h>
#include <asm/ioctls.h>

#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include "smc_netns.h"

#include "smc.h"
#include "smc_clc.h"
#include "smc_llc.h"
#include "smc_cdc.h"
#include "smc_core.h"
#include "smc_ib.h"
#include "smc_ism.h"
#include "smc_pnet.h"
#include "smc_netlink.h"
#include "smc_tx.h"
#include "smc_rx.h"
#include "smc_close.h"
#include "smc_stats.h"
#include "smc_tracepoint.h"
#include "smc_sysctl.h"

static DEFINE_MUTEX(smc_server_lgr_pending);	/* serialize link group
						 * creation on server
						 */
static DEFINE_MUTEX(smc_client_lgr_pending);	/* serialize link group
						 * creation on client
						 */

static struct workqueue_struct	*smc_tcp_ls_wq;	/* wq for tcp listen work */
struct workqueue_struct	*smc_hs_wq;	/* wq for handshake work */
struct workqueue_struct	*smc_close_wq;	/* wq for close work */

static void smc_tcp_listen_work(struct work_struct *);
static void smc_connect_work(struct work_struct *);

int smc_nl_dump_hs_limitation(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct smc_nl_dmp_ctx *cb_ctx = smc_nl_dmp_ctx(cb);
	void *hdr;

	if (cb_ctx->pos[0])
		goto out;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &smc_gen_nl_family, NLM_F_MULTI,
			  SMC_NETLINK_DUMP_HS_LIMITATION);
	if (!hdr)
		return -ENOMEM;

	if (nla_put_u8(skb, SMC_NLA_HS_LIMITATION_ENABLED,
		       sock_net(skb->sk)->smc.limit_smc_hs))
		goto err;

	genlmsg_end(skb, hdr);
	cb_ctx->pos[0] = 1;
out:
	return skb->len;
err:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

int smc_nl_enable_hs_limitation(struct sk_buff *skb, struct genl_info *info)
{
	sock_net(skb->sk)->smc.limit_smc_hs = true;
	return 0;
}

int smc_nl_disable_hs_limitation(struct sk_buff *skb, struct genl_info *info)
{
	sock_net(skb->sk)->smc.limit_smc_hs = false;
	return 0;
}

static void smc_set_keepalive(struct sock *sk, int val)
{
	struct smc_sock *smc = smc_sk(sk);

	smc->clcsock->sk->sk_prot->keepalive(smc->clcsock->sk, val);
}

static struct sock *smc_tcp_syn_recv_sock(const struct sock *sk,
					  struct sk_buff *skb,
					  struct request_sock *req,
					  struct dst_entry *dst,
					  struct request_sock *req_unhash,
					  bool *own_req)
{
	struct smc_sock *smc;
	struct sock *child;

	smc = smc_clcsock_user_data(sk);

	if (READ_ONCE(sk->sk_ack_backlog) + atomic_read(&smc->queued_smc_hs) >
				sk->sk_max_ack_backlog)
		goto drop;

	if (sk_acceptq_is_full(&smc->sk)) {
		NET_INC_STATS(sock_net(sk), LINUX_MIB_LISTENOVERFLOWS);
		goto drop;
	}

	/* passthrough to original syn recv sock fct */
	child = smc->ori_af_ops->syn_recv_sock(sk, skb, req, dst, req_unhash,
					       own_req);
	/* child must not inherit smc or its ops */
	if (child) {
		rcu_assign_sk_user_data(child, NULL);

		/* v4-mapped sockets don't inherit parent ops. Don't restore. */
		if (inet_csk(child)->icsk_af_ops == inet_csk(sk)->icsk_af_ops)
			inet_csk(child)->icsk_af_ops = smc->ori_af_ops;
	}
	return child;

drop:
	dst_release(dst);
	tcp_listendrop(sk);
	return NULL;
}

static bool smc_hs_congested(const struct sock *sk)
{
	const struct smc_sock *smc;

	smc = smc_clcsock_user_data(sk);

	if (!smc)
		return true;

	if (workqueue_congested(WORK_CPU_UNBOUND, smc_hs_wq))
		return true;

	return false;
}

static struct smc_hashinfo smc_v4_hashinfo = {
	.lock = __RW_LOCK_UNLOCKED(smc_v4_hashinfo.lock),
};

static struct smc_hashinfo smc_v6_hashinfo = {
	.lock = __RW_LOCK_UNLOCKED(smc_v6_hashinfo.lock),
};

int smc_hash_sk(struct sock *sk)
{
	struct smc_hashinfo *h = sk->sk_prot->h.smc_hash;
	struct hlist_head *head;

	head = &h->ht;

	write_lock_bh(&h->lock);
	sk_add_node(sk, head);
	write_unlock_bh(&h->lock);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(smc_hash_sk);

void smc_unhash_sk(struct sock *sk)
{
	struct smc_hashinfo *h = sk->sk_prot->h.smc_hash;

	write_lock_bh(&h->lock);
	if (sk_del_node_init(sk))
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	write_unlock_bh(&h->lock);
}
EXPORT_SYMBOL_GPL(smc_unhash_sk);

/* This will be called before user really release sock_lock. So do the
 * work which we didn't do because of user hold the sock_lock in the
 * BH context
 */
static void smc_release_cb(struct sock *sk)
{
	struct smc_sock *smc = smc_sk(sk);

	if (smc->conn.tx_in_release_sock) {
		smc_tx_pending(&smc->conn);
		smc->conn.tx_in_release_sock = false;
	}
}

struct proto smc_proto = {
	.name		= "SMC",
	.owner		= THIS_MODULE,
	.keepalive	= smc_set_keepalive,
	.hash		= smc_hash_sk,
	.unhash		= smc_unhash_sk,
	.release_cb	= smc_release_cb,
	.obj_size	= sizeof(struct smc_sock),
	.h.smc_hash	= &smc_v4_hashinfo,
	.slab_flags	= SLAB_TYPESAFE_BY_RCU,
};
EXPORT_SYMBOL_GPL(smc_proto);

struct proto smc_proto6 = {
	.name		= "SMC6",
	.owner		= THIS_MODULE,
	.keepalive	= smc_set_keepalive,
	.hash		= smc_hash_sk,
	.unhash		= smc_unhash_sk,
	.release_cb	= smc_release_cb,
	.obj_size	= sizeof(struct smc_sock),
	.h.smc_hash	= &smc_v6_hashinfo,
	.slab_flags	= SLAB_TYPESAFE_BY_RCU,
};
EXPORT_SYMBOL_GPL(smc_proto6);

static void smc_fback_restore_callbacks(struct smc_sock *smc)
{
	struct sock *clcsk = smc->clcsock->sk;

	write_lock_bh(&clcsk->sk_callback_lock);
	clcsk->sk_user_data = NULL;

	smc_clcsock_restore_cb(&clcsk->sk_state_change, &smc->clcsk_state_change);
	smc_clcsock_restore_cb(&clcsk->sk_data_ready, &smc->clcsk_data_ready);
	smc_clcsock_restore_cb(&clcsk->sk_write_space, &smc->clcsk_write_space);
	smc_clcsock_restore_cb(&clcsk->sk_error_report, &smc->clcsk_error_report);

	write_unlock_bh(&clcsk->sk_callback_lock);
}

static void smc_restore_fallback_changes(struct smc_sock *smc)
{
	if (smc->clcsock->file) { /* non-accepted sockets have no file yet */
		smc->clcsock->file->private_data = smc->sk.sk_socket;
		smc->clcsock->file = NULL;
		smc_fback_restore_callbacks(smc);
	}
}

static int __smc_release(struct smc_sock *smc)
{
	struct sock *sk = &smc->sk;
	int rc = 0;

	if (!smc->use_fallback) {
		rc = smc_close_active(smc);
		sock_set_flag(sk, SOCK_DEAD);
		sk->sk_shutdown |= SHUTDOWN_MASK;
	} else {
		if (sk->sk_state != SMC_CLOSED) {
			if (sk->sk_state != SMC_LISTEN &&
			    sk->sk_state != SMC_INIT)
				sock_put(sk); /* passive closing */
			if (sk->sk_state == SMC_LISTEN) {
				/* wake up clcsock accept */
				rc = kernel_sock_shutdown(smc->clcsock,
							  SHUT_RDWR);
			}
			sk->sk_state = SMC_CLOSED;
			sk->sk_state_change(sk);
		}
		smc_restore_fallback_changes(smc);
	}

	sk->sk_prot->unhash(sk);

	if (sk->sk_state == SMC_CLOSED) {
		if (smc->clcsock) {
			release_sock(sk);
			smc_clcsock_release(smc);
			lock_sock(sk);
		}
		if (!smc->use_fallback)
			smc_conn_free(&smc->conn);
	}

	return rc;
}

static int smc_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int old_state, rc = 0;

	if (!sk)
		goto out;

	sock_hold(sk); /* sock_put below */
	smc = smc_sk(sk);

	old_state = sk->sk_state;

	/* cleanup for a dangling non-blocking connect */
	if (smc->connect_nonblock && old_state == SMC_INIT)
		tcp_abort(smc->clcsock->sk, ECONNABORTED);

	if (cancel_work_sync(&smc->connect_work))
		sock_put(&smc->sk); /* sock_hold in smc_connect for passive closing */

	if (sk->sk_state == SMC_LISTEN)
		/* smc_close_non_accepted() is called and acquires
		 * sock lock for child sockets again
		 */
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);
	else
		lock_sock(sk);

	if (old_state == SMC_INIT && sk->sk_state == SMC_ACTIVE &&
	    !smc->use_fallback)
		smc_close_active_abort(smc);

	rc = __smc_release(smc);

	/* detach socket */
	sock_orphan(sk);
	sock->sk = NULL;
	release_sock(sk);

	sock_put(sk); /* sock_hold above */
	sock_put(sk); /* final sock_put */
out:
	return rc;
}

static void smc_destruct(struct sock *sk)
{
	if (sk->sk_state != SMC_CLOSED)
		return;
	if (!sock_flag(sk, SOCK_DEAD))
		return;

	sk_refcnt_debug_dec(sk);
}

static struct sock *smc_sock_alloc(struct net *net, struct socket *sock,
				   int protocol)
{
	struct smc_sock *smc;
	struct proto *prot;
	struct sock *sk;

	prot = (protocol == SMCPROTO_SMC6) ? &smc_proto6 : &smc_proto;
	sk = sk_alloc(net, PF_SMC, GFP_KERNEL, prot, 0);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk); /* sets sk_refcnt to 1 */
	sk->sk_state = SMC_INIT;
	sk->sk_destruct = smc_destruct;
	sk->sk_protocol = protocol;
	smc = smc_sk(sk);
	INIT_WORK(&smc->tcp_listen_work, smc_tcp_listen_work);
	INIT_WORK(&smc->connect_work, smc_connect_work);
	INIT_DELAYED_WORK(&smc->conn.tx_work, smc_tx_work);
	INIT_LIST_HEAD(&smc->accept_q);
	spin_lock_init(&smc->accept_q_lock);
	spin_lock_init(&smc->conn.send_lock);
	sk->sk_prot->hash(sk);
	sk_refcnt_debug_inc(sk);
	mutex_init(&smc->clcsock_release_lock);
	smc_init_saved_callbacks(smc);

	return sk;
}

static int smc_bind(struct socket *sock, struct sockaddr *uaddr,
		    int addr_len)
{
	struct sockaddr_in *addr = (struct sockaddr_in *)uaddr;
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc;

	smc = smc_sk(sk);

	/* replicate tests from inet_bind(), to be safe wrt. future changes */
	rc = -EINVAL;
	if (addr_len < sizeof(struct sockaddr_in))
		goto out;

	rc = -EAFNOSUPPORT;
	if (addr->sin_family != AF_INET &&
	    addr->sin_family != AF_INET6 &&
	    addr->sin_family != AF_UNSPEC)
		goto out;
	/* accept AF_UNSPEC (mapped to AF_INET) only if s_addr is INADDR_ANY */
	if (addr->sin_family == AF_UNSPEC &&
	    addr->sin_addr.s_addr != htonl(INADDR_ANY))
		goto out;

	lock_sock(sk);

	/* Check if socket is already active */
	rc = -EINVAL;
	if (sk->sk_state != SMC_INIT || smc->connect_nonblock)
		goto out_rel;

	smc->clcsock->sk->sk_reuse = sk->sk_reuse;
	rc = kernel_bind(smc->clcsock, uaddr, addr_len);

out_rel:
	release_sock(sk);
out:
	return rc;
}

static void smc_copy_sock_settings(struct sock *nsk, struct sock *osk,
				   unsigned long mask)
{
	/* options we don't get control via setsockopt for */
	nsk->sk_type = osk->sk_type;
	nsk->sk_sndbuf = osk->sk_sndbuf;
	nsk->sk_rcvbuf = osk->sk_rcvbuf;
	nsk->sk_sndtimeo = osk->sk_sndtimeo;
	nsk->sk_rcvtimeo = osk->sk_rcvtimeo;
	nsk->sk_mark = osk->sk_mark;
	nsk->sk_priority = osk->sk_priority;
	nsk->sk_rcvlowat = osk->sk_rcvlowat;
	nsk->sk_bound_dev_if = osk->sk_bound_dev_if;
	nsk->sk_err = osk->sk_err;

	nsk->sk_flags &= ~mask;
	nsk->sk_flags |= osk->sk_flags & mask;
}

#define SK_FLAGS_SMC_TO_CLC ((1UL << SOCK_URGINLINE) | \
			     (1UL << SOCK_KEEPOPEN) | \
			     (1UL << SOCK_LINGER) | \
			     (1UL << SOCK_BROADCAST) | \
			     (1UL << SOCK_TIMESTAMP) | \
			     (1UL << SOCK_DBG) | \
			     (1UL << SOCK_RCVTSTAMP) | \
			     (1UL << SOCK_RCVTSTAMPNS) | \
			     (1UL << SOCK_LOCALROUTE) | \
			     (1UL << SOCK_TIMESTAMPING_RX_SOFTWARE) | \
			     (1UL << SOCK_RXQ_OVFL) | \
			     (1UL << SOCK_WIFI_STATUS) | \
			     (1UL << SOCK_NOFCS) | \
			     (1UL << SOCK_FILTER_LOCKED) | \
			     (1UL << SOCK_TSTAMP_NEW))
/* copy only relevant settings and flags of SOL_SOCKET level from smc to
 * clc socket (since smc is not called for these options from net/core)
 */
static void smc_copy_sock_settings_to_clc(struct smc_sock *smc)
{
	smc_copy_sock_settings(smc->clcsock->sk, &smc->sk, SK_FLAGS_SMC_TO_CLC);
}

#define SK_FLAGS_CLC_TO_SMC ((1UL << SOCK_URGINLINE) | \
			     (1UL << SOCK_KEEPOPEN) | \
			     (1UL << SOCK_LINGER) | \
			     (1UL << SOCK_DBG))
/* copy only settings and flags relevant for smc from clc to smc socket */
static void smc_copy_sock_settings_to_smc(struct smc_sock *smc)
{
	smc_copy_sock_settings(&smc->sk, smc->clcsock->sk, SK_FLAGS_CLC_TO_SMC);
}

/* register the new vzalloced sndbuf on all links */
static int smcr_lgr_reg_sndbufs(struct smc_link *link,
				struct smc_buf_desc *snd_desc)
{
	struct smc_link_group *lgr = link->lgr;
	int i, rc = 0;

	if (!snd_desc->is_vm)
		return -EINVAL;

	/* protect against parallel smcr_link_reg_buf() */
	mutex_lock(&lgr->llc_conf_mutex);
	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		if (!smc_link_active(&lgr->lnk[i]))
			continue;
		rc = smcr_link_reg_buf(&lgr->lnk[i], snd_desc);
		if (rc)
			break;
	}
	mutex_unlock(&lgr->llc_conf_mutex);
	return rc;
}

/* register the new rmb on all links */
static int smcr_lgr_reg_rmbs(struct smc_link *link,
			     struct smc_buf_desc *rmb_desc)
{
	struct smc_link_group *lgr = link->lgr;
	int i, rc = 0;

	rc = smc_llc_flow_initiate(lgr, SMC_LLC_FLOW_RKEY);
	if (rc)
		return rc;
	/* protect against parallel smc_llc_cli_rkey_exchange() and
	 * parallel smcr_link_reg_buf()
	 */
	mutex_lock(&lgr->llc_conf_mutex);
	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		if (!smc_link_active(&lgr->lnk[i]))
			continue;
		rc = smcr_link_reg_buf(&lgr->lnk[i], rmb_desc);
		if (rc)
			goto out;
	}

	/* exchange confirm_rkey msg with peer */
	rc = smc_llc_do_confirm_rkey(link, rmb_desc);
	if (rc) {
		rc = -EFAULT;
		goto out;
	}
	rmb_desc->is_conf_rkey = true;
out:
	mutex_unlock(&lgr->llc_conf_mutex);
	smc_llc_flow_stop(lgr, &lgr->llc_flow_lcl);
	return rc;
}

static int smcr_clnt_conf_first_link(struct smc_sock *smc)
{
	struct smc_link *link = smc->conn.lnk;
	struct smc_llc_qentry *qentry;
	int rc;

	/* receive CONFIRM LINK request from server over RoCE fabric */
	qentry = smc_llc_wait(link->lgr, NULL, SMC_LLC_WAIT_TIME,
			      SMC_LLC_CONFIRM_LINK);
	if (!qentry) {
		struct smc_clc_msg_decline dclc;

		rc = smc_clc_wait_msg(smc, &dclc, sizeof(dclc),
				      SMC_CLC_DECLINE, CLC_WAIT_TIME_SHORT);
		return rc == -EAGAIN ? SMC_CLC_DECL_TIMEOUT_CL : rc;
	}
	smc_llc_save_peer_uid(qentry);
	rc = smc_llc_eval_conf_link(qentry, SMC_LLC_REQ);
	smc_llc_flow_qentry_del(&link->lgr->llc_flow_lcl);
	if (rc)
		return SMC_CLC_DECL_RMBE_EC;

	rc = smc_ib_modify_qp_rts(link);
	if (rc)
		return SMC_CLC_DECL_ERR_RDYLNK;

	smc_wr_remember_qp_attr(link);

	/* reg the sndbuf if it was vzalloced */
	if (smc->conn.sndbuf_desc->is_vm) {
		if (smcr_link_reg_buf(link, smc->conn.sndbuf_desc))
			return SMC_CLC_DECL_ERR_REGBUF;
	}

	/* reg the rmb */
	if (smcr_link_reg_buf(link, smc->conn.rmb_desc))
		return SMC_CLC_DECL_ERR_REGBUF;

	/* confirm_rkey is implicit on 1st contact */
	smc->conn.rmb_desc->is_conf_rkey = true;

	/* send CONFIRM LINK response over RoCE fabric */
	rc = smc_llc_send_confirm_link(link, SMC_LLC_RESP);
	if (rc < 0)
		return SMC_CLC_DECL_TIMEOUT_CL;

	smc_llc_link_active(link);
	smcr_lgr_set_type(link->lgr, SMC_LGR_SINGLE);

	/* optional 2nd link, receive ADD LINK request from server */
	qentry = smc_llc_wait(link->lgr, NULL, SMC_LLC_WAIT_TIME,
			      SMC_LLC_ADD_LINK);
	if (!qentry) {
		struct smc_clc_msg_decline dclc;

		rc = smc_clc_wait_msg(smc, &dclc, sizeof(dclc),
				      SMC_CLC_DECLINE, CLC_WAIT_TIME_SHORT);
		if (rc == -EAGAIN)
			rc = 0; /* no DECLINE received, go with one link */
		return rc;
	}
	smc_llc_flow_qentry_clr(&link->lgr->llc_flow_lcl);
	smc_llc_cli_add_link(link, qentry);
	return 0;
}

static bool smc_isascii(char *hostname)
{
	int i;

	for (i = 0; i < SMC_MAX_HOSTNAME_LEN; i++)
		if (!isascii(hostname[i]))
			return false;
	return true;
}

static void smc_conn_save_peer_info_fce(struct smc_sock *smc,
					struct smc_clc_msg_accept_confirm *clc)
{
	struct smc_clc_msg_accept_confirm_v2 *clc_v2 =
		(struct smc_clc_msg_accept_confirm_v2 *)clc;
	struct smc_clc_first_contact_ext *fce;
	int clc_v2_len;

	if (clc->hdr.version == SMC_V1 ||
	    !(clc->hdr.typev2 & SMC_FIRST_CONTACT_MASK))
		return;

	if (smc->conn.lgr->is_smcd) {
		memcpy(smc->conn.lgr->negotiated_eid, clc_v2->d1.eid,
		       SMC_MAX_EID_LEN);
		clc_v2_len = offsetofend(struct smc_clc_msg_accept_confirm_v2,
					 d1);
	} else {
		memcpy(smc->conn.lgr->negotiated_eid, clc_v2->r1.eid,
		       SMC_MAX_EID_LEN);
		clc_v2_len = offsetofend(struct smc_clc_msg_accept_confirm_v2,
					 r1);
	}
	fce = (struct smc_clc_first_contact_ext *)(((u8 *)clc_v2) + clc_v2_len);
	smc->conn.lgr->peer_os = fce->os_type;
	smc->conn.lgr->peer_smc_release = fce->release;
	if (smc_isascii(fce->hostname))
		memcpy(smc->conn.lgr->peer_hostname, fce->hostname,
		       SMC_MAX_HOSTNAME_LEN);
}

static void smcr_conn_save_peer_info(struct smc_sock *smc,
				     struct smc_clc_msg_accept_confirm *clc)
{
	int bufsize = smc_uncompress_bufsize(clc->r0.rmbe_size);

	smc->conn.peer_rmbe_idx = clc->r0.rmbe_idx;
	smc->conn.local_tx_ctrl.token = ntohl(clc->r0.rmbe_alert_token);
	smc->conn.peer_rmbe_size = bufsize;
	atomic_set(&smc->conn.peer_rmbe_space, smc->conn.peer_rmbe_size);
	smc->conn.tx_off = bufsize * (smc->conn.peer_rmbe_idx - 1);
}

static void smcd_conn_save_peer_info(struct smc_sock *smc,
				     struct smc_clc_msg_accept_confirm *clc)
{
	int bufsize = smc_uncompress_bufsize(clc->d0.dmbe_size);

	smc->conn.peer_rmbe_idx = clc->d0.dmbe_idx;
	smc->conn.peer_token = clc->d0.token;
	/* msg header takes up space in the buffer */
	smc->conn.peer_rmbe_size = bufsize - sizeof(struct smcd_cdc_msg);
	atomic_set(&smc->conn.peer_rmbe_space, smc->conn.peer_rmbe_size);
	smc->conn.tx_off = bufsize * smc->conn.peer_rmbe_idx;
}

static void smc_conn_save_peer_info(struct smc_sock *smc,
				    struct smc_clc_msg_accept_confirm *clc)
{
	if (smc->conn.lgr->is_smcd)
		smcd_conn_save_peer_info(smc, clc);
	else
		smcr_conn_save_peer_info(smc, clc);
	smc_conn_save_peer_info_fce(smc, clc);
}

static void smc_link_save_peer_info(struct smc_link *link,
				    struct smc_clc_msg_accept_confirm *clc,
				    struct smc_init_info *ini)
{
	link->peer_qpn = ntoh24(clc->r0.qpn);
	memcpy(link->peer_gid, ini->peer_gid, SMC_GID_SIZE);
	memcpy(link->peer_mac, ini->peer_mac, sizeof(link->peer_mac));
	link->peer_psn = ntoh24(clc->r0.psn);
	link->peer_mtu = clc->r0.qp_mtu;
}

static void smc_stat_inc_fback_rsn_cnt(struct smc_sock *smc,
				       struct smc_stats_fback *fback_arr)
{
	int cnt;

	for (cnt = 0; cnt < SMC_MAX_FBACK_RSN_CNT; cnt++) {
		if (fback_arr[cnt].fback_code == smc->fallback_rsn) {
			fback_arr[cnt].count++;
			break;
		}
		if (!fback_arr[cnt].fback_code) {
			fback_arr[cnt].fback_code = smc->fallback_rsn;
			fback_arr[cnt].count++;
			break;
		}
	}
}

static void smc_stat_fallback(struct smc_sock *smc)
{
	struct net *net = sock_net(&smc->sk);

	mutex_lock(&net->smc.mutex_fback_rsn);
	if (smc->listen_smc) {
		smc_stat_inc_fback_rsn_cnt(smc, net->smc.fback_rsn->srv);
		net->smc.fback_rsn->srv_fback_cnt++;
	} else {
		smc_stat_inc_fback_rsn_cnt(smc, net->smc.fback_rsn->clnt);
		net->smc.fback_rsn->clnt_fback_cnt++;
	}
	mutex_unlock(&net->smc.mutex_fback_rsn);
}

/* must be called under rcu read lock */
static void smc_fback_wakeup_waitqueue(struct smc_sock *smc, void *key)
{
	struct socket_wq *wq;
	__poll_t flags;

	wq = rcu_dereference(smc->sk.sk_wq);
	if (!skwq_has_sleeper(wq))
		return;

	/* wake up smc sk->sk_wq */
	if (!key) {
		/* sk_state_change */
		wake_up_interruptible_all(&wq->wait);
	} else {
		flags = key_to_poll(key);
		if (flags & (EPOLLIN | EPOLLOUT))
			/* sk_data_ready or sk_write_space */
			wake_up_interruptible_sync_poll(&wq->wait, flags);
		else if (flags & EPOLLERR)
			/* sk_error_report */
			wake_up_interruptible_poll(&wq->wait, flags);
	}
}

static int smc_fback_mark_woken(wait_queue_entry_t *wait,
				unsigned int mode, int sync, void *key)
{
	struct smc_mark_woken *mark =
		container_of(wait, struct smc_mark_woken, wait_entry);

	mark->woken = true;
	mark->key = key;
	return 0;
}

static void smc_fback_forward_wakeup(struct smc_sock *smc, struct sock *clcsk,
				     void (*clcsock_callback)(struct sock *sk))
{
	struct smc_mark_woken mark = { .woken = false };
	struct socket_wq *wq;

	init_waitqueue_func_entry(&mark.wait_entry,
				  smc_fback_mark_woken);
	rcu_read_lock();
	wq = rcu_dereference(clcsk->sk_wq);
	if (!wq)
		goto out;
	add_wait_queue(sk_sleep(clcsk), &mark.wait_entry);
	clcsock_callback(clcsk);
	remove_wait_queue(sk_sleep(clcsk), &mark.wait_entry);

	if (mark.woken)
		smc_fback_wakeup_waitqueue(smc, mark.key);
out:
	rcu_read_unlock();
}

static void smc_fback_state_change(struct sock *clcsk)
{
	struct smc_sock *smc;

	read_lock_bh(&clcsk->sk_callback_lock);
	smc = smc_clcsock_user_data(clcsk);
	if (smc)
		smc_fback_forward_wakeup(smc, clcsk,
					 smc->clcsk_state_change);
	read_unlock_bh(&clcsk->sk_callback_lock);
}

static void smc_fback_data_ready(struct sock *clcsk)
{
	struct smc_sock *smc;

	read_lock_bh(&clcsk->sk_callback_lock);
	smc = smc_clcsock_user_data(clcsk);
	if (smc)
		smc_fback_forward_wakeup(smc, clcsk,
					 smc->clcsk_data_ready);
	read_unlock_bh(&clcsk->sk_callback_lock);
}

static void smc_fback_write_space(struct sock *clcsk)
{
	struct smc_sock *smc;

	read_lock_bh(&clcsk->sk_callback_lock);
	smc = smc_clcsock_user_data(clcsk);
	if (smc)
		smc_fback_forward_wakeup(smc, clcsk,
					 smc->clcsk_write_space);
	read_unlock_bh(&clcsk->sk_callback_lock);
}

static void smc_fback_error_report(struct sock *clcsk)
{
	struct smc_sock *smc;

	read_lock_bh(&clcsk->sk_callback_lock);
	smc = smc_clcsock_user_data(clcsk);
	if (smc)
		smc_fback_forward_wakeup(smc, clcsk,
					 smc->clcsk_error_report);
	read_unlock_bh(&clcsk->sk_callback_lock);
}

static void smc_fback_replace_callbacks(struct smc_sock *smc)
{
	struct sock *clcsk = smc->clcsock->sk;

	write_lock_bh(&clcsk->sk_callback_lock);
	clcsk->sk_user_data = (void *)((uintptr_t)smc | SK_USER_DATA_NOCOPY);

	smc_clcsock_replace_cb(&clcsk->sk_state_change, smc_fback_state_change,
			       &smc->clcsk_state_change);
	smc_clcsock_replace_cb(&clcsk->sk_data_ready, smc_fback_data_ready,
			       &smc->clcsk_data_ready);
	smc_clcsock_replace_cb(&clcsk->sk_write_space, smc_fback_write_space,
			       &smc->clcsk_write_space);
	smc_clcsock_replace_cb(&clcsk->sk_error_report, smc_fback_error_report,
			       &smc->clcsk_error_report);

	write_unlock_bh(&clcsk->sk_callback_lock);
}

static int smc_switch_to_fallback(struct smc_sock *smc, int reason_code)
{
	int rc = 0;

	mutex_lock(&smc->clcsock_release_lock);
	if (!smc->clcsock) {
		rc = -EBADF;
		goto out;
	}

	smc->use_fallback = true;
	smc->fallback_rsn = reason_code;
	smc_stat_fallback(smc);
	trace_smc_switch_to_fallback(smc, reason_code);
	if (smc->sk.sk_socket && smc->sk.sk_socket->file) {
		smc->clcsock->file = smc->sk.sk_socket->file;
		smc->clcsock->file->private_data = smc->clcsock;
		smc->clcsock->wq.fasync_list =
			smc->sk.sk_socket->wq.fasync_list;

		/* There might be some wait entries remaining
		 * in smc sk->sk_wq and they should be woken up
		 * as clcsock's wait queue is woken up.
		 */
		smc_fback_replace_callbacks(smc);
	}
out:
	mutex_unlock(&smc->clcsock_release_lock);
	return rc;
}

/* fall back during connect */
static int smc_connect_fallback(struct smc_sock *smc, int reason_code)
{
	struct net *net = sock_net(&smc->sk);
	int rc = 0;

	rc = smc_switch_to_fallback(smc, reason_code);
	if (rc) { /* fallback fails */
		this_cpu_inc(net->smc.smc_stats->clnt_hshake_err_cnt);
		if (smc->sk.sk_state == SMC_INIT)
			sock_put(&smc->sk); /* passive closing */
		return rc;
	}
	smc_copy_sock_settings_to_clc(smc);
	smc->connect_nonblock = 0;
	if (smc->sk.sk_state == SMC_INIT)
		smc->sk.sk_state = SMC_ACTIVE;
	return 0;
}

/* decline and fall back during connect */
static int smc_connect_decline_fallback(struct smc_sock *smc, int reason_code,
					u8 version)
{
	struct net *net = sock_net(&smc->sk);
	int rc;

	if (reason_code < 0) { /* error, fallback is not possible */
		this_cpu_inc(net->smc.smc_stats->clnt_hshake_err_cnt);
		if (smc->sk.sk_state == SMC_INIT)
			sock_put(&smc->sk); /* passive closing */
		return reason_code;
	}
	if (reason_code != SMC_CLC_DECL_PEERDECL) {
		rc = smc_clc_send_decline(smc, reason_code, version);
		if (rc < 0) {
			this_cpu_inc(net->smc.smc_stats->clnt_hshake_err_cnt);
			if (smc->sk.sk_state == SMC_INIT)
				sock_put(&smc->sk); /* passive closing */
			return rc;
		}
	}
	return smc_connect_fallback(smc, reason_code);
}

static void smc_conn_abort(struct smc_sock *smc, int local_first)
{
	struct smc_connection *conn = &smc->conn;
	struct smc_link_group *lgr = conn->lgr;
	bool lgr_valid = false;

	if (smc_conn_lgr_valid(conn))
		lgr_valid = true;

	smc_conn_free(conn);
	if (local_first && lgr_valid)
		smc_lgr_cleanup_early(lgr);
}

/* check if there is a rdma device available for this connection. */
/* called for connect and listen */
static int smc_find_rdma_device(struct smc_sock *smc, struct smc_init_info *ini)
{
	/* PNET table look up: search active ib_device and port
	 * within same PNETID that also contains the ethernet device
	 * used for the internal TCP socket
	 */
	smc_pnet_find_roce_resource(smc->clcsock->sk, ini);
	if (!ini->check_smcrv2 && !ini->ib_dev)
		return SMC_CLC_DECL_NOSMCRDEV;
	if (ini->check_smcrv2 && !ini->smcrv2.ib_dev_v2)
		return SMC_CLC_DECL_NOSMCRDEV;
	return 0;
}

/* check if there is an ISM device available for this connection. */
/* called for connect and listen */
static int smc_find_ism_device(struct smc_sock *smc, struct smc_init_info *ini)
{
	/* Find ISM device with same PNETID as connecting interface  */
	smc_pnet_find_ism_resource(smc->clcsock->sk, ini);
	if (!ini->ism_dev[0])
		return SMC_CLC_DECL_NOSMCDDEV;
	else
		ini->ism_chid[0] = smc_ism_get_chid(ini->ism_dev[0]);
	return 0;
}

/* is chid unique for the ism devices that are already determined? */
static bool smc_find_ism_v2_is_unique_chid(u16 chid, struct smc_init_info *ini,
					   int cnt)
{
	int i = (!ini->ism_dev[0]) ? 1 : 0;

	for (; i < cnt; i++)
		if (ini->ism_chid[i] == chid)
			return false;
	return true;
}

/* determine possible V2 ISM devices (either without PNETID or with PNETID plus
 * PNETID matching net_device)
 */
static int smc_find_ism_v2_device_clnt(struct smc_sock *smc,
				       struct smc_init_info *ini)
{
	int rc = SMC_CLC_DECL_NOSMCDDEV;
	struct smcd_dev *smcd;
	int i = 1;
	u16 chid;

	if (smcd_indicated(ini->smc_type_v1))
		rc = 0;		/* already initialized for V1 */
	mutex_lock(&smcd_dev_list.mutex);
	list_for_each_entry(smcd, &smcd_dev_list.list, list) {
		if (smcd->going_away || smcd == ini->ism_dev[0])
			continue;
		chid = smc_ism_get_chid(smcd);
		if (!smc_find_ism_v2_is_unique_chid(chid, ini, i))
			continue;
		if (!smc_pnet_is_pnetid_set(smcd->pnetid) ||
		    smc_pnet_is_ndev_pnetid(sock_net(&smc->sk), smcd->pnetid)) {
			ini->ism_dev[i] = smcd;
			ini->ism_chid[i] = chid;
			ini->is_smcd = true;
			rc = 0;
			i++;
			if (i > SMC_MAX_ISM_DEVS)
				break;
		}
	}
	mutex_unlock(&smcd_dev_list.mutex);
	ini->ism_offered_cnt = i - 1;
	if (!ini->ism_dev[0] && !ini->ism_dev[1])
		ini->smcd_version = 0;

	return rc;
}

/* Check for VLAN ID and register it on ISM device just for CLC handshake */
static int smc_connect_ism_vlan_setup(struct smc_sock *smc,
				      struct smc_init_info *ini)
{
	if (ini->vlan_id && smc_ism_get_vlan(ini->ism_dev[0], ini->vlan_id))
		return SMC_CLC_DECL_ISMVLANERR;
	return 0;
}

static int smc_find_proposal_devices(struct smc_sock *smc,
				     struct smc_init_info *ini)
{
	int rc = 0;

	/* check if there is an ism device available */
	if (!(ini->smcd_version & SMC_V1) ||
	    smc_find_ism_device(smc, ini) ||
	    smc_connect_ism_vlan_setup(smc, ini))
		ini->smcd_version &= ~SMC_V1;
	/* else ISM V1 is supported for this connection */

	/* check if there is an rdma device available */
	if (!(ini->smcr_version & SMC_V1) ||
	    smc_find_rdma_device(smc, ini))
		ini->smcr_version &= ~SMC_V1;
	/* else RDMA is supported for this connection */

	ini->smc_type_v1 = smc_indicated_type(ini->smcd_version & SMC_V1,
					      ini->smcr_version & SMC_V1);

	/* check if there is an ism v2 device available */
	if (!(ini->smcd_version & SMC_V2) ||
	    !smc_ism_is_v2_capable() ||
	    smc_find_ism_v2_device_clnt(smc, ini))
		ini->smcd_version &= ~SMC_V2;

	/* check if there is an rdma v2 device available */
	ini->check_smcrv2 = true;
	ini->smcrv2.saddr = smc->clcsock->sk->sk_rcv_saddr;
	if (!(ini->smcr_version & SMC_V2) ||
	    smc->clcsock->sk->sk_family != AF_INET ||
	    !smc_clc_ueid_count() ||
	    smc_find_rdma_device(smc, ini))
		ini->smcr_version &= ~SMC_V2;
	ini->check_smcrv2 = false;

	ini->smc_type_v2 = smc_indicated_type(ini->smcd_version & SMC_V2,
					      ini->smcr_version & SMC_V2);

	/* if neither ISM nor RDMA are supported, fallback */
	if (ini->smc_type_v1 == SMC_TYPE_N && ini->smc_type_v2 == SMC_TYPE_N)
		rc = SMC_CLC_DECL_NOSMCDEV;

	return rc;
}

/* cleanup temporary VLAN ID registration used for CLC handshake. If ISM is
 * used, the VLAN ID will be registered again during the connection setup.
 */
static int smc_connect_ism_vlan_cleanup(struct smc_sock *smc,
					struct smc_init_info *ini)
{
	if (!smcd_indicated(ini->smc_type_v1))
		return 0;
	if (ini->vlan_id && smc_ism_put_vlan(ini->ism_dev[0], ini->vlan_id))
		return SMC_CLC_DECL_CNFERR;
	return 0;
}

#define SMC_CLC_MAX_ACCEPT_LEN \
	(sizeof(struct smc_clc_msg_accept_confirm_v2) + \
	 sizeof(struct smc_clc_first_contact_ext) + \
	 sizeof(struct smc_clc_msg_trail))

/* CLC handshake during connect */
static int smc_connect_clc(struct smc_sock *smc,
			   struct smc_clc_msg_accept_confirm_v2 *aclc2,
			   struct smc_init_info *ini)
{
	int rc = 0;

	/* do inband token exchange */
	rc = smc_clc_send_proposal(smc, ini);
	if (rc)
		return rc;
	/* receive SMC Accept CLC message */
	return smc_clc_wait_msg(smc, aclc2, SMC_CLC_MAX_ACCEPT_LEN,
				SMC_CLC_ACCEPT, CLC_WAIT_TIME);
}

void smc_fill_gid_list(struct smc_link_group *lgr,
		       struct smc_gidlist *gidlist,
		       struct smc_ib_device *known_dev, u8 *known_gid)
{
	struct smc_init_info *alt_ini = NULL;

	memset(gidlist, 0, sizeof(*gidlist));
	memcpy(gidlist->list[gidlist->len++], known_gid, SMC_GID_SIZE);

	alt_ini = kzalloc(sizeof(*alt_ini), GFP_KERNEL);
	if (!alt_ini)
		goto out;

	alt_ini->vlan_id = lgr->vlan_id;
	alt_ini->check_smcrv2 = true;
	alt_ini->smcrv2.saddr = lgr->saddr;
	smc_pnet_find_alt_roce(lgr, alt_ini, known_dev);

	if (!alt_ini->smcrv2.ib_dev_v2)
		goto out;

	memcpy(gidlist->list[gidlist->len++], alt_ini->smcrv2.ib_gid_v2,
	       SMC_GID_SIZE);

out:
	kfree(alt_ini);
}

static int smc_connect_rdma_v2_prepare(struct smc_sock *smc,
				       struct smc_clc_msg_accept_confirm *aclc,
				       struct smc_init_info *ini)
{
	struct smc_clc_msg_accept_confirm_v2 *clc_v2 =
		(struct smc_clc_msg_accept_confirm_v2 *)aclc;
	struct smc_clc_first_contact_ext *fce =
		(struct smc_clc_first_contact_ext *)
			(((u8 *)clc_v2) + sizeof(*clc_v2));

	if (!ini->first_contact_peer || aclc->hdr.version == SMC_V1)
		return 0;

	if (fce->v2_direct) {
		memcpy(ini->smcrv2.nexthop_mac, &aclc->r0.lcl.mac, ETH_ALEN);
		ini->smcrv2.uses_gateway = false;
	} else {
		if (smc_ib_find_route(smc->clcsock->sk->sk_rcv_saddr,
				      smc_ib_gid_to_ipv4(aclc->r0.lcl.gid),
				      ini->smcrv2.nexthop_mac,
				      &ini->smcrv2.uses_gateway))
			return SMC_CLC_DECL_NOROUTE;
		if (!ini->smcrv2.uses_gateway) {
			/* mismatch: peer claims indirect, but its direct */
			return SMC_CLC_DECL_NOINDIRECT;
		}
	}
	return 0;
}

/* setup for RDMA connection of client */
static int smc_connect_rdma(struct smc_sock *smc,
			    struct smc_clc_msg_accept_confirm *aclc,
			    struct smc_init_info *ini)
{
	int i, reason_code = 0;
	struct smc_link *link;
	u8 *eid = NULL;

	ini->is_smcd = false;
	ini->ib_clcqpn = ntoh24(aclc->r0.qpn);
	ini->first_contact_peer = aclc->hdr.typev2 & SMC_FIRST_CONTACT_MASK;
	memcpy(ini->peer_systemid, aclc->r0.lcl.id_for_peer, SMC_SYSTEMID_LEN);
	memcpy(ini->peer_gid, aclc->r0.lcl.gid, SMC_GID_SIZE);
	memcpy(ini->peer_mac, aclc->r0.lcl.mac, ETH_ALEN);

	reason_code = smc_connect_rdma_v2_prepare(smc, aclc, ini);
	if (reason_code)
		return reason_code;

	mutex_lock(&smc_client_lgr_pending);
	reason_code = smc_conn_create(smc, ini);
	if (reason_code) {
		mutex_unlock(&smc_client_lgr_pending);
		return reason_code;
	}

	smc_conn_save_peer_info(smc, aclc);

	if (ini->first_contact_local) {
		link = smc->conn.lnk;
	} else {
		/* set link that was assigned by server */
		link = NULL;
		for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
			struct smc_link *l = &smc->conn.lgr->lnk[i];

			if (l->peer_qpn == ntoh24(aclc->r0.qpn) &&
			    !memcmp(l->peer_gid, &aclc->r0.lcl.gid,
				    SMC_GID_SIZE) &&
			    (aclc->hdr.version > SMC_V1 ||
			     !memcmp(l->peer_mac, &aclc->r0.lcl.mac,
				     sizeof(l->peer_mac)))) {
				link = l;
				break;
			}
		}
		if (!link) {
			reason_code = SMC_CLC_DECL_NOSRVLINK;
			goto connect_abort;
		}
		smc_switch_link_and_count(&smc->conn, link);
	}

	/* create send buffer and rmb */
	if (smc_buf_create(smc, false)) {
		reason_code = SMC_CLC_DECL_MEM;
		goto connect_abort;
	}

	if (ini->first_contact_local)
		smc_link_save_peer_info(link, aclc, ini);

	if (smc_rmb_rtoken_handling(&smc->conn, link, aclc)) {
		reason_code = SMC_CLC_DECL_ERR_RTOK;
		goto connect_abort;
	}

	smc_close_init(smc);
	smc_rx_init(smc);

	if (ini->first_contact_local) {
		if (smc_ib_ready_link(link)) {
			reason_code = SMC_CLC_DECL_ERR_RDYLNK;
			goto connect_abort;
		}
	} else {
		/* reg sendbufs if they were vzalloced */
		if (smc->conn.sndbuf_desc->is_vm) {
			if (smcr_lgr_reg_sndbufs(link, smc->conn.sndbuf_desc)) {
				reason_code = SMC_CLC_DECL_ERR_REGBUF;
				goto connect_abort;
			}
		}
		if (smcr_lgr_reg_rmbs(link, smc->conn.rmb_desc)) {
			reason_code = SMC_CLC_DECL_ERR_REGBUF;
			goto connect_abort;
		}
	}

	if (aclc->hdr.version > SMC_V1) {
		struct smc_clc_msg_accept_confirm_v2 *clc_v2 =
			(struct smc_clc_msg_accept_confirm_v2 *)aclc;

		eid = clc_v2->r1.eid;
		if (ini->first_contact_local)
			smc_fill_gid_list(link->lgr, &ini->smcrv2.gidlist,
					  link->smcibdev, link->gid);
	}

	reason_code = smc_clc_send_confirm(smc, ini->first_contact_local,
					   aclc->hdr.version, eid, ini);
	if (reason_code)
		goto connect_abort;

	smc_tx_init(smc);

	if (ini->first_contact_local) {
		/* QP confirmation over RoCE fabric */
		smc_llc_flow_initiate(link->lgr, SMC_LLC_FLOW_ADD_LINK);
		reason_code = smcr_clnt_conf_first_link(smc);
		smc_llc_flow_stop(link->lgr, &link->lgr->llc_flow_lcl);
		if (reason_code)
			goto connect_abort;
	}
	mutex_unlock(&smc_client_lgr_pending);

	smc_copy_sock_settings_to_clc(smc);
	smc->connect_nonblock = 0;
	if (smc->sk.sk_state == SMC_INIT)
		smc->sk.sk_state = SMC_ACTIVE;

	return 0;
connect_abort:
	smc_conn_abort(smc, ini->first_contact_local);
	mutex_unlock(&smc_client_lgr_pending);
	smc->connect_nonblock = 0;

	return reason_code;
}

/* The server has chosen one of the proposed ISM devices for the communication.
 * Determine from the CHID of the received CLC ACCEPT the ISM device chosen.
 */
static int
smc_v2_determine_accepted_chid(struct smc_clc_msg_accept_confirm_v2 *aclc,
			       struct smc_init_info *ini)
{
	int i;

	for (i = 0; i < ini->ism_offered_cnt + 1; i++) {
		if (ini->ism_chid[i] == ntohs(aclc->d1.chid)) {
			ini->ism_selected = i;
			return 0;
		}
	}

	return -EPROTO;
}

/* setup for ISM connection of client */
static int smc_connect_ism(struct smc_sock *smc,
			   struct smc_clc_msg_accept_confirm *aclc,
			   struct smc_init_info *ini)
{
	u8 *eid = NULL;
	int rc = 0;

	ini->is_smcd = true;
	ini->first_contact_peer = aclc->hdr.typev2 & SMC_FIRST_CONTACT_MASK;

	if (aclc->hdr.version == SMC_V2) {
		struct smc_clc_msg_accept_confirm_v2 *aclc_v2 =
			(struct smc_clc_msg_accept_confirm_v2 *)aclc;

		rc = smc_v2_determine_accepted_chid(aclc_v2, ini);
		if (rc)
			return rc;
	}
	ini->ism_peer_gid[ini->ism_selected] = aclc->d0.gid;

	/* there is only one lgr role for SMC-D; use server lock */
	mutex_lock(&smc_server_lgr_pending);
	rc = smc_conn_create(smc, ini);
	if (rc) {
		mutex_unlock(&smc_server_lgr_pending);
		return rc;
	}

	/* Create send and receive buffers */
	rc = smc_buf_create(smc, true);
	if (rc) {
		rc = (rc == -ENOSPC) ? SMC_CLC_DECL_MAX_DMB : SMC_CLC_DECL_MEM;
		goto connect_abort;
	}

	smc_conn_save_peer_info(smc, aclc);
	smc_close_init(smc);
	smc_rx_init(smc);
	smc_tx_init(smc);

	if (aclc->hdr.version > SMC_V1) {
		struct smc_clc_msg_accept_confirm_v2 *clc_v2 =
			(struct smc_clc_msg_accept_confirm_v2 *)aclc;

		eid = clc_v2->d1.eid;
	}

	rc = smc_clc_send_confirm(smc, ini->first_contact_local,
				  aclc->hdr.version, eid, NULL);
	if (rc)
		goto connect_abort;
	mutex_unlock(&smc_server_lgr_pending);

	smc_copy_sock_settings_to_clc(smc);
	smc->connect_nonblock = 0;
	if (smc->sk.sk_state == SMC_INIT)
		smc->sk.sk_state = SMC_ACTIVE;

	return 0;
connect_abort:
	smc_conn_abort(smc, ini->first_contact_local);
	mutex_unlock(&smc_server_lgr_pending);
	smc->connect_nonblock = 0;

	return rc;
}

/* check if received accept type and version matches a proposed one */
static int smc_connect_check_aclc(struct smc_init_info *ini,
				  struct smc_clc_msg_accept_confirm *aclc)
{
	if (aclc->hdr.typev1 != SMC_TYPE_R &&
	    aclc->hdr.typev1 != SMC_TYPE_D)
		return SMC_CLC_DECL_MODEUNSUPP;

	if (aclc->hdr.version >= SMC_V2) {
		if ((aclc->hdr.typev1 == SMC_TYPE_R &&
		     !smcr_indicated(ini->smc_type_v2)) ||
		    (aclc->hdr.typev1 == SMC_TYPE_D &&
		     !smcd_indicated(ini->smc_type_v2)))
			return SMC_CLC_DECL_MODEUNSUPP;
	} else {
		if ((aclc->hdr.typev1 == SMC_TYPE_R &&
		     !smcr_indicated(ini->smc_type_v1)) ||
		    (aclc->hdr.typev1 == SMC_TYPE_D &&
		     !smcd_indicated(ini->smc_type_v1)))
			return SMC_CLC_DECL_MODEUNSUPP;
	}

	return 0;
}

/* perform steps before actually connecting */
static int __smc_connect(struct smc_sock *smc)
{
	u8 version = smc_ism_is_v2_capable() ? SMC_V2 : SMC_V1;
	struct smc_clc_msg_accept_confirm_v2 *aclc2;
	struct smc_clc_msg_accept_confirm *aclc;
	struct smc_init_info *ini = NULL;
	u8 *buf = NULL;
	int rc = 0;

	if (smc->use_fallback)
		return smc_connect_fallback(smc, smc->fallback_rsn);

	/* if peer has not signalled SMC-capability, fall back */
	if (!tcp_sk(smc->clcsock->sk)->syn_smc)
		return smc_connect_fallback(smc, SMC_CLC_DECL_PEERNOSMC);

	/* IPSec connections opt out of SMC optimizations */
	if (using_ipsec(smc))
		return smc_connect_decline_fallback(smc, SMC_CLC_DECL_IPSEC,
						    version);

	ini = kzalloc(sizeof(*ini), GFP_KERNEL);
	if (!ini)
		return smc_connect_decline_fallback(smc, SMC_CLC_DECL_MEM,
						    version);

	ini->smcd_version = SMC_V1 | SMC_V2;
	ini->smcr_version = SMC_V1 | SMC_V2;
	ini->smc_type_v1 = SMC_TYPE_B;
	ini->smc_type_v2 = SMC_TYPE_B;

	/* get vlan id from IP device */
	if (smc_vlan_by_tcpsk(smc->clcsock, ini)) {
		ini->smcd_version &= ~SMC_V1;
		ini->smcr_version = 0;
		ini->smc_type_v1 = SMC_TYPE_N;
		if (!ini->smcd_version) {
			rc = SMC_CLC_DECL_GETVLANERR;
			goto fallback;
		}
	}

	rc = smc_find_proposal_devices(smc, ini);
	if (rc)
		goto fallback;

	buf = kzalloc(SMC_CLC_MAX_ACCEPT_LEN, GFP_KERNEL);
	if (!buf) {
		rc = SMC_CLC_DECL_MEM;
		goto fallback;
	}
	aclc2 = (struct smc_clc_msg_accept_confirm_v2 *)buf;
	aclc = (struct smc_clc_msg_accept_confirm *)aclc2;

	/* perform CLC handshake */
	rc = smc_connect_clc(smc, aclc2, ini);
	if (rc) {
		/* -EAGAIN on timeout, see tcp_recvmsg() */
		if (rc == -EAGAIN) {
			rc = -ETIMEDOUT;
			smc->sk.sk_err = ETIMEDOUT;
		}
		goto vlan_cleanup;
	}

	/* check if smc modes and versions of CLC proposal and accept match */
	rc = smc_connect_check_aclc(ini, aclc);
	version = aclc->hdr.version == SMC_V1 ? SMC_V1 : SMC_V2;
	if (rc)
		goto vlan_cleanup;

	/* depending on previous steps, connect using rdma or ism */
	if (aclc->hdr.typev1 == SMC_TYPE_R) {
		ini->smcr_version = version;
		rc = smc_connect_rdma(smc, aclc, ini);
	} else if (aclc->hdr.typev1 == SMC_TYPE_D) {
		ini->smcd_version = version;
		rc = smc_connect_ism(smc, aclc, ini);
	}
	if (rc)
		goto vlan_cleanup;

	SMC_STAT_CLNT_SUCC_INC(sock_net(smc->clcsock->sk), aclc);
	smc_connect_ism_vlan_cleanup(smc, ini);
	kfree(buf);
	kfree(ini);
	return 0;

vlan_cleanup:
	smc_connect_ism_vlan_cleanup(smc, ini);
	kfree(buf);
fallback:
	kfree(ini);
	return smc_connect_decline_fallback(smc, rc, version);
}

static void smc_connect_work(struct work_struct *work)
{
	struct smc_sock *smc = container_of(work, struct smc_sock,
					    connect_work);
	long timeo = smc->sk.sk_sndtimeo;
	int rc = 0;

	if (!timeo)
		timeo = MAX_SCHEDULE_TIMEOUT;
	lock_sock(smc->clcsock->sk);
	if (smc->clcsock->sk->sk_err) {
		smc->sk.sk_err = smc->clcsock->sk->sk_err;
	} else if ((1 << smc->clcsock->sk->sk_state) &
					(TCPF_SYN_SENT | TCPF_SYN_RECV)) {
		rc = sk_stream_wait_connect(smc->clcsock->sk, &timeo);
		if ((rc == -EPIPE) &&
		    ((1 << smc->clcsock->sk->sk_state) &
					(TCPF_ESTABLISHED | TCPF_CLOSE_WAIT)))
			rc = 0;
	}
	release_sock(smc->clcsock->sk);
	lock_sock(&smc->sk);
	if (rc != 0 || smc->sk.sk_err) {
		smc->sk.sk_state = SMC_CLOSED;
		if (rc == -EPIPE || rc == -EAGAIN)
			smc->sk.sk_err = EPIPE;
		else if (rc == -ECONNREFUSED)
			smc->sk.sk_err = ECONNREFUSED;
		else if (signal_pending(current))
			smc->sk.sk_err = -sock_intr_errno(timeo);
		sock_put(&smc->sk); /* passive closing */
		goto out;
	}

	rc = __smc_connect(smc);
	if (rc < 0)
		smc->sk.sk_err = -rc;

out:
	if (!sock_flag(&smc->sk, SOCK_DEAD)) {
		if (smc->sk.sk_err) {
			smc->sk.sk_state_change(&smc->sk);
		} else { /* allow polling before and after fallback decision */
			smc->clcsock->sk->sk_write_space(smc->clcsock->sk);
			smc->sk.sk_write_space(&smc->sk);
		}
	}
	release_sock(&smc->sk);
}

static int smc_connect(struct socket *sock, struct sockaddr *addr,
		       int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = -EINVAL;

	smc = smc_sk(sk);

	/* separate smc parameter checking to be safe */
	if (alen < sizeof(addr->sa_family))
		goto out_err;
	if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6)
		goto out_err;

	lock_sock(sk);
	switch (sock->state) {
	default:
		rc = -EINVAL;
		goto out;
	case SS_CONNECTED:
		rc = sk->sk_state == SMC_ACTIVE ? -EISCONN : -EINVAL;
		goto out;
	case SS_CONNECTING:
		if (sk->sk_state == SMC_ACTIVE)
			goto connected;
		break;
	case SS_UNCONNECTED:
		sock->state = SS_CONNECTING;
		break;
	}

	switch (sk->sk_state) {
	default:
		goto out;
	case SMC_CLOSED:
		rc = sock_error(sk) ? : -ECONNABORTED;
		sock->state = SS_UNCONNECTED;
		goto out;
	case SMC_ACTIVE:
		rc = -EISCONN;
		goto out;
	case SMC_INIT:
		break;
	}

	smc_copy_sock_settings_to_clc(smc);
	tcp_sk(smc->clcsock->sk)->syn_smc = 1;
	if (smc->connect_nonblock) {
		rc = -EALREADY;
		goto out;
	}
	rc = kernel_connect(smc->clcsock, addr, alen, flags);
	if (rc && rc != -EINPROGRESS)
		goto out;

	if (smc->use_fallback) {
		sock->state = rc ? SS_CONNECTING : SS_CONNECTED;
		goto out;
	}
	sock_hold(&smc->sk); /* sock put in passive closing */
	if (flags & O_NONBLOCK) {
		if (queue_work(smc_hs_wq, &smc->connect_work))
			smc->connect_nonblock = 1;
		rc = -EINPROGRESS;
		goto out;
	} else {
		rc = __smc_connect(smc);
		if (rc < 0)
			goto out;
	}

connected:
	rc = 0;
	sock->state = SS_CONNECTED;
out:
	release_sock(sk);
out_err:
	return rc;
}

static int smc_clcsock_accept(struct smc_sock *lsmc, struct smc_sock **new_smc)
{
	struct socket *new_clcsock = NULL;
	struct sock *lsk = &lsmc->sk;
	struct sock *new_sk;
	int rc = -EINVAL;

	release_sock(lsk);
	new_sk = smc_sock_alloc(sock_net(lsk), NULL, lsk->sk_protocol);
	if (!new_sk) {
		rc = -ENOMEM;
		lsk->sk_err = ENOMEM;
		*new_smc = NULL;
		lock_sock(lsk);
		goto out;
	}
	*new_smc = smc_sk(new_sk);

	mutex_lock(&lsmc->clcsock_release_lock);
	if (lsmc->clcsock)
		rc = kernel_accept(lsmc->clcsock, &new_clcsock, SOCK_NONBLOCK);
	mutex_unlock(&lsmc->clcsock_release_lock);
	lock_sock(lsk);
	if  (rc < 0 && rc != -EAGAIN)
		lsk->sk_err = -rc;
	if (rc < 0 || lsk->sk_state == SMC_CLOSED) {
		new_sk->sk_prot->unhash(new_sk);
		if (new_clcsock)
			sock_release(new_clcsock);
		new_sk->sk_state = SMC_CLOSED;
		sock_set_flag(new_sk, SOCK_DEAD);
		sock_put(new_sk); /* final */
		*new_smc = NULL;
		goto out;
	}

	/* new clcsock has inherited the smc listen-specific sk_data_ready
	 * function; switch it back to the original sk_data_ready function
	 */
	new_clcsock->sk->sk_data_ready = lsmc->clcsk_data_ready;

	/* if new clcsock has also inherited the fallback-specific callback
	 * functions, switch them back to the original ones.
	 */
	if (lsmc->use_fallback) {
		if (lsmc->clcsk_state_change)
			new_clcsock->sk->sk_state_change = lsmc->clcsk_state_change;
		if (lsmc->clcsk_write_space)
			new_clcsock->sk->sk_write_space = lsmc->clcsk_write_space;
		if (lsmc->clcsk_error_report)
			new_clcsock->sk->sk_error_report = lsmc->clcsk_error_report;
	}

	(*new_smc)->clcsock = new_clcsock;
out:
	return rc;
}

/* add a just created sock to the accept queue of the listen sock as
 * candidate for a following socket accept call from user space
 */
static void smc_accept_enqueue(struct sock *parent, struct sock *sk)
{
	struct smc_sock *par = smc_sk(parent);

	sock_hold(sk); /* sock_put in smc_accept_unlink () */
	spin_lock(&par->accept_q_lock);
	list_add_tail(&smc_sk(sk)->accept_q, &par->accept_q);
	spin_unlock(&par->accept_q_lock);
	sk_acceptq_added(parent);
}

/* remove a socket from the accept queue of its parental listening socket */
static void smc_accept_unlink(struct sock *sk)
{
	struct smc_sock *par = smc_sk(sk)->listen_smc;

	spin_lock(&par->accept_q_lock);
	list_del_init(&smc_sk(sk)->accept_q);
	spin_unlock(&par->accept_q_lock);
	sk_acceptq_removed(&smc_sk(sk)->listen_smc->sk);
	sock_put(sk); /* sock_hold in smc_accept_enqueue */
}

/* remove a sock from the accept queue to bind it to a new socket created
 * for a socket accept call from user space
 */
struct sock *smc_accept_dequeue(struct sock *parent,
				struct socket *new_sock)
{
	struct smc_sock *isk, *n;
	struct sock *new_sk;

	list_for_each_entry_safe(isk, n, &smc_sk(parent)->accept_q, accept_q) {
		new_sk = (struct sock *)isk;

		smc_accept_unlink(new_sk);
		if (new_sk->sk_state == SMC_CLOSED) {
			new_sk->sk_prot->unhash(new_sk);
			if (isk->clcsock) {
				sock_release(isk->clcsock);
				isk->clcsock = NULL;
			}
			sock_put(new_sk); /* final */
			continue;
		}
		if (new_sock) {
			sock_graft(new_sk, new_sock);
			new_sock->state = SS_CONNECTED;
			if (isk->use_fallback) {
				smc_sk(new_sk)->clcsock->file = new_sock->file;
				isk->clcsock->file->private_data = isk->clcsock;
			}
		}
		return new_sk;
	}
	return NULL;
}

/* clean up for a created but never accepted sock */
void smc_close_non_accepted(struct sock *sk)
{
	struct smc_sock *smc = smc_sk(sk);

	sock_hold(sk); /* sock_put below */
	lock_sock(sk);
	if (!sk->sk_lingertime)
		/* wait for peer closing */
		sk->sk_lingertime = SMC_MAX_STREAM_WAIT_TIMEOUT;
	__smc_release(smc);
	release_sock(sk);
	sock_put(sk); /* sock_hold above */
	sock_put(sk); /* final sock_put */
}

static int smcr_serv_conf_first_link(struct smc_sock *smc)
{
	struct smc_link *link = smc->conn.lnk;
	struct smc_llc_qentry *qentry;
	int rc;

	/* reg the sndbuf if it was vzalloced*/
	if (smc->conn.sndbuf_desc->is_vm) {
		if (smcr_link_reg_buf(link, smc->conn.sndbuf_desc))
			return SMC_CLC_DECL_ERR_REGBUF;
	}

	/* reg the rmb */
	if (smcr_link_reg_buf(link, smc->conn.rmb_desc))
		return SMC_CLC_DECL_ERR_REGBUF;

	/* send CONFIRM LINK request to client over the RoCE fabric */
	rc = smc_llc_send_confirm_link(link, SMC_LLC_REQ);
	if (rc < 0)
		return SMC_CLC_DECL_TIMEOUT_CL;

	/* receive CONFIRM LINK response from client over the RoCE fabric */
	qentry = smc_llc_wait(link->lgr, link, SMC_LLC_WAIT_TIME,
			      SMC_LLC_CONFIRM_LINK);
	if (!qentry) {
		struct smc_clc_msg_decline dclc;

		rc = smc_clc_wait_msg(smc, &dclc, sizeof(dclc),
				      SMC_CLC_DECLINE, CLC_WAIT_TIME_SHORT);
		return rc == -EAGAIN ? SMC_CLC_DECL_TIMEOUT_CL : rc;
	}
	smc_llc_save_peer_uid(qentry);
	rc = smc_llc_eval_conf_link(qentry, SMC_LLC_RESP);
	smc_llc_flow_qentry_del(&link->lgr->llc_flow_lcl);
	if (rc)
		return SMC_CLC_DECL_RMBE_EC;

	/* confirm_rkey is implicit on 1st contact */
	smc->conn.rmb_desc->is_conf_rkey = true;

	smc_llc_link_active(link);
	smcr_lgr_set_type(link->lgr, SMC_LGR_SINGLE);

	/* initial contact - try to establish second link */
	smc_llc_srv_add_link(link, NULL);
	return 0;
}

/* listen worker: finish */
static void smc_listen_out(struct smc_sock *new_smc)
{
	struct smc_sock *lsmc = new_smc->listen_smc;
	struct sock *newsmcsk = &new_smc->sk;

	if (tcp_sk(new_smc->clcsock->sk)->syn_smc)
		atomic_dec(&lsmc->queued_smc_hs);

	if (lsmc->sk.sk_state == SMC_LISTEN) {
		lock_sock_nested(&lsmc->sk, SINGLE_DEPTH_NESTING);
		smc_accept_enqueue(&lsmc->sk, newsmcsk);
		release_sock(&lsmc->sk);
	} else { /* no longer listening */
		smc_close_non_accepted(newsmcsk);
	}

	/* Wake up accept */
	lsmc->sk.sk_data_ready(&lsmc->sk);
	sock_put(&lsmc->sk); /* sock_hold in smc_tcp_listen_work */
}

/* listen worker: finish in state connected */
static void smc_listen_out_connected(struct smc_sock *new_smc)
{
	struct sock *newsmcsk = &new_smc->sk;

	if (newsmcsk->sk_state == SMC_INIT)
		newsmcsk->sk_state = SMC_ACTIVE;

	smc_listen_out(new_smc);
}

/* listen worker: finish in error state */
static void smc_listen_out_err(struct smc_sock *new_smc)
{
	struct sock *newsmcsk = &new_smc->sk;
	struct net *net = sock_net(newsmcsk);

	this_cpu_inc(net->smc.smc_stats->srv_hshake_err_cnt);
	if (newsmcsk->sk_state == SMC_INIT)
		sock_put(&new_smc->sk); /* passive closing */
	newsmcsk->sk_state = SMC_CLOSED;

	smc_listen_out(new_smc);
}

/* listen worker: decline and fall back if possible */
static void smc_listen_decline(struct smc_sock *new_smc, int reason_code,
			       int local_first, u8 version)
{
	/* RDMA setup failed, switch back to TCP */
	smc_conn_abort(new_smc, local_first);
	if (reason_code < 0 ||
	    smc_switch_to_fallback(new_smc, reason_code)) {
		/* error, no fallback possible */
		smc_listen_out_err(new_smc);
		return;
	}
	if (reason_code && reason_code != SMC_CLC_DECL_PEERDECL) {
		if (smc_clc_send_decline(new_smc, reason_code, version) < 0) {
			smc_listen_out_err(new_smc);
			return;
		}
	}
	smc_listen_out_connected(new_smc);
}

/* listen worker: version checking */
static int smc_listen_v2_check(struct smc_sock *new_smc,
			       struct smc_clc_msg_proposal *pclc,
			       struct smc_init_info *ini)
{
	struct smc_clc_smcd_v2_extension *pclc_smcd_v2_ext;
	struct smc_clc_v2_extension *pclc_v2_ext;
	int rc = SMC_CLC_DECL_PEERNOSMC;

	ini->smc_type_v1 = pclc->hdr.typev1;
	ini->smc_type_v2 = pclc->hdr.typev2;
	ini->smcd_version = smcd_indicated(ini->smc_type_v1) ? SMC_V1 : 0;
	ini->smcr_version = smcr_indicated(ini->smc_type_v1) ? SMC_V1 : 0;
	if (pclc->hdr.version > SMC_V1) {
		if (smcd_indicated(ini->smc_type_v2))
			ini->smcd_version |= SMC_V2;
		if (smcr_indicated(ini->smc_type_v2))
			ini->smcr_version |= SMC_V2;
	}
	if (!(ini->smcd_version & SMC_V2) && !(ini->smcr_version & SMC_V2)) {
		rc = SMC_CLC_DECL_PEERNOSMC;
		goto out;
	}
	pclc_v2_ext = smc_get_clc_v2_ext(pclc);
	if (!pclc_v2_ext) {
		ini->smcd_version &= ~SMC_V2;
		ini->smcr_version &= ~SMC_V2;
		rc = SMC_CLC_DECL_NOV2EXT;
		goto out;
	}
	pclc_smcd_v2_ext = smc_get_clc_smcd_v2_ext(pclc_v2_ext);
	if (ini->smcd_version & SMC_V2) {
		if (!smc_ism_is_v2_capable()) {
			ini->smcd_version &= ~SMC_V2;
			rc = SMC_CLC_DECL_NOISM2SUPP;
		} else if (!pclc_smcd_v2_ext) {
			ini->smcd_version &= ~SMC_V2;
			rc = SMC_CLC_DECL_NOV2DEXT;
		} else if (!pclc_v2_ext->hdr.eid_cnt &&
			   !pclc_v2_ext->hdr.flag.seid) {
			ini->smcd_version &= ~SMC_V2;
			rc = SMC_CLC_DECL_NOUEID;
		}
	}
	if (ini->smcr_version & SMC_V2) {
		if (!pclc_v2_ext->hdr.eid_cnt) {
			ini->smcr_version &= ~SMC_V2;
			rc = SMC_CLC_DECL_NOUEID;
		}
	}

out:
	if (!ini->smcd_version && !ini->smcr_version)
		return rc;

	return 0;
}

/* listen worker: check prefixes */
static int smc_listen_prfx_check(struct smc_sock *new_smc,
				 struct smc_clc_msg_proposal *pclc)
{
	struct smc_clc_msg_proposal_prefix *pclc_prfx;
	struct socket *newclcsock = new_smc->clcsock;

	if (pclc->hdr.typev1 == SMC_TYPE_N)
		return 0;
	pclc_prfx = smc_clc_proposal_get_prefix(pclc);
	if (smc_clc_prfx_match(newclcsock, pclc_prfx))
		return SMC_CLC_DECL_DIFFPREFIX;

	return 0;
}

/* listen worker: initialize connection and buffers */
static int smc_listen_rdma_init(struct smc_sock *new_smc,
				struct smc_init_info *ini)
{
	int rc;

	/* allocate connection / link group */
	rc = smc_conn_create(new_smc, ini);
	if (rc)
		return rc;

	/* create send buffer and rmb */
	if (smc_buf_create(new_smc, false))
		return SMC_CLC_DECL_MEM;

	return 0;
}

/* listen worker: initialize connection and buffers for SMC-D */
static int smc_listen_ism_init(struct smc_sock *new_smc,
			       struct smc_init_info *ini)
{
	int rc;

	rc = smc_conn_create(new_smc, ini);
	if (rc)
		return rc;

	/* Create send and receive buffers */
	rc = smc_buf_create(new_smc, true);
	if (rc) {
		smc_conn_abort(new_smc, ini->first_contact_local);
		return (rc == -ENOSPC) ? SMC_CLC_DECL_MAX_DMB :
					 SMC_CLC_DECL_MEM;
	}

	return 0;
}

static bool smc_is_already_selected(struct smcd_dev *smcd,
				    struct smc_init_info *ini,
				    int matches)
{
	int i;

	for (i = 0; i < matches; i++)
		if (smcd == ini->ism_dev[i])
			return true;

	return false;
}

/* check for ISM devices matching proposed ISM devices */
static void smc_check_ism_v2_match(struct smc_init_info *ini,
				   u16 proposed_chid, u64 proposed_gid,
				   unsigned int *matches)
{
	struct smcd_dev *smcd;

	list_for_each_entry(smcd, &smcd_dev_list.list, list) {
		if (smcd->going_away)
			continue;
		if (smc_is_already_selected(smcd, ini, *matches))
			continue;
		if (smc_ism_get_chid(smcd) == proposed_chid &&
		    !smc_ism_cantalk(proposed_gid, ISM_RESERVED_VLANID, smcd)) {
			ini->ism_peer_gid[*matches] = proposed_gid;
			ini->ism_dev[*matches] = smcd;
			(*matches)++;
			break;
		}
	}
}

static void smc_find_ism_store_rc(u32 rc, struct smc_init_info *ini)
{
	if (!ini->rc)
		ini->rc = rc;
}

static void smc_find_ism_v2_device_serv(struct smc_sock *new_smc,
					struct smc_clc_msg_proposal *pclc,
					struct smc_init_info *ini)
{
	struct smc_clc_smcd_v2_extension *smcd_v2_ext;
	struct smc_clc_v2_extension *smc_v2_ext;
	struct smc_clc_msg_smcd *pclc_smcd;
	unsigned int matches = 0;
	u8 smcd_version;
	u8 *eid = NULL;
	int i, rc;

	if (!(ini->smcd_version & SMC_V2) || !smcd_indicated(ini->smc_type_v2))
		goto not_found;

	pclc_smcd = smc_get_clc_msg_smcd(pclc);
	smc_v2_ext = smc_get_clc_v2_ext(pclc);
	smcd_v2_ext = smc_get_clc_smcd_v2_ext(smc_v2_ext);

	mutex_lock(&smcd_dev_list.mutex);
	if (pclc_smcd->ism.chid)
		/* check for ISM device matching proposed native ISM device */
		smc_check_ism_v2_match(ini, ntohs(pclc_smcd->ism.chid),
				       ntohll(pclc_smcd->ism.gid), &matches);
	for (i = 1; i <= smc_v2_ext->hdr.ism_gid_cnt; i++) {
		/* check for ISM devices matching proposed non-native ISM
		 * devices
		 */
		smc_check_ism_v2_match(ini,
				       ntohs(smcd_v2_ext->gidchid[i - 1].chid),
				       ntohll(smcd_v2_ext->gidchid[i - 1].gid),
				       &matches);
	}
	mutex_unlock(&smcd_dev_list.mutex);

	if (!ini->ism_dev[0]) {
		smc_find_ism_store_rc(SMC_CLC_DECL_NOSMCD2DEV, ini);
		goto not_found;
	}

	smc_ism_get_system_eid(&eid);
	if (!smc_clc_match_eid(ini->negotiated_eid, smc_v2_ext,
			       smcd_v2_ext->system_eid, eid))
		goto not_found;

	/* separate - outside the smcd_dev_list.lock */
	smcd_version = ini->smcd_version;
	for (i = 0; i < matches; i++) {
		ini->smcd_version = SMC_V2;
		ini->is_smcd = true;
		ini->ism_selected = i;
		rc = smc_listen_ism_init(new_smc, ini);
		if (rc) {
			smc_find_ism_store_rc(rc, ini);
			/* try next active ISM device */
			continue;
		}
		return; /* matching and usable V2 ISM device found */
	}
	/* no V2 ISM device could be initialized */
	ini->smcd_version = smcd_version;	/* restore original value */
	ini->negotiated_eid[0] = 0;

not_found:
	ini->smcd_version &= ~SMC_V2;
	ini->ism_dev[0] = NULL;
	ini->is_smcd = false;
}

static void smc_find_ism_v1_device_serv(struct smc_sock *new_smc,
					struct smc_clc_msg_proposal *pclc,
					struct smc_init_info *ini)
{
	struct smc_clc_msg_smcd *pclc_smcd = smc_get_clc_msg_smcd(pclc);
	int rc = 0;

	/* check if ISM V1 is available */
	if (!(ini->smcd_version & SMC_V1) || !smcd_indicated(ini->smc_type_v1))
		goto not_found;
	ini->is_smcd = true; /* prepare ISM check */
	ini->ism_peer_gid[0] = ntohll(pclc_smcd->ism.gid);
	rc = smc_find_ism_device(new_smc, ini);
	if (rc)
		goto not_found;
	ini->ism_selected = 0;
	rc = smc_listen_ism_init(new_smc, ini);
	if (!rc)
		return;		/* V1 ISM device found */

not_found:
	smc_find_ism_store_rc(rc, ini);
	ini->smcd_version &= ~SMC_V1;
	ini->ism_dev[0] = NULL;
	ini->is_smcd = false;
}

/* listen worker: register buffers */
static int smc_listen_rdma_reg(struct smc_sock *new_smc, bool local_first)
{
	struct smc_connection *conn = &new_smc->conn;

	if (!local_first) {
		/* reg sendbufs if they were vzalloced */
		if (conn->sndbuf_desc->is_vm) {
			if (smcr_lgr_reg_sndbufs(conn->lnk,
						 conn->sndbuf_desc))
				return SMC_CLC_DECL_ERR_REGBUF;
		}
		if (smcr_lgr_reg_rmbs(conn->lnk, conn->rmb_desc))
			return SMC_CLC_DECL_ERR_REGBUF;
	}

	return 0;
}

static void smc_find_rdma_v2_device_serv(struct smc_sock *new_smc,
					 struct smc_clc_msg_proposal *pclc,
					 struct smc_init_info *ini)
{
	struct smc_clc_v2_extension *smc_v2_ext;
	u8 smcr_version;
	int rc;

	if (!(ini->smcr_version & SMC_V2) || !smcr_indicated(ini->smc_type_v2))
		goto not_found;

	smc_v2_ext = smc_get_clc_v2_ext(pclc);
	if (!smc_clc_match_eid(ini->negotiated_eid, smc_v2_ext, NULL, NULL))
		goto not_found;

	/* prepare RDMA check */
	memcpy(ini->peer_systemid, pclc->lcl.id_for_peer, SMC_SYSTEMID_LEN);
	memcpy(ini->peer_gid, smc_v2_ext->roce, SMC_GID_SIZE);
	memcpy(ini->peer_mac, pclc->lcl.mac, ETH_ALEN);
	ini->check_smcrv2 = true;
	ini->smcrv2.clc_sk = new_smc->clcsock->sk;
	ini->smcrv2.saddr = new_smc->clcsock->sk->sk_rcv_saddr;
	ini->smcrv2.daddr = smc_ib_gid_to_ipv4(smc_v2_ext->roce);
	rc = smc_find_rdma_device(new_smc, ini);
	if (rc) {
		smc_find_ism_store_rc(rc, ini);
		goto not_found;
	}
	if (!ini->smcrv2.uses_gateway)
		memcpy(ini->smcrv2.nexthop_mac, pclc->lcl.mac, ETH_ALEN);

	smcr_version = ini->smcr_version;
	ini->smcr_version = SMC_V2;
	rc = smc_listen_rdma_init(new_smc, ini);
	if (!rc)
		rc = smc_listen_rdma_reg(new_smc, ini->first_contact_local);
	if (!rc)
		return;
	ini->smcr_version = smcr_version;
	smc_find_ism_store_rc(rc, ini);

not_found:
	ini->smcr_version &= ~SMC_V2;
	ini->smcrv2.ib_dev_v2 = NULL;
	ini->check_smcrv2 = false;
}

static int smc_find_rdma_v1_device_serv(struct smc_sock *new_smc,
					struct smc_clc_msg_proposal *pclc,
					struct smc_init_info *ini)
{
	int rc;

	if (!(ini->smcr_version & SMC_V1) || !smcr_indicated(ini->smc_type_v1))
		return SMC_CLC_DECL_NOSMCDEV;

	/* prepare RDMA check */
	memcpy(ini->peer_systemid, pclc->lcl.id_for_peer, SMC_SYSTEMID_LEN);
	memcpy(ini->peer_gid, pclc->lcl.gid, SMC_GID_SIZE);
	memcpy(ini->peer_mac, pclc->lcl.mac, ETH_ALEN);
	rc = smc_find_rdma_device(new_smc, ini);
	if (rc) {
		/* no RDMA device found */
		return SMC_CLC_DECL_NOSMCDEV;
	}
	rc = smc_listen_rdma_init(new_smc, ini);
	if (rc)
		return rc;
	return smc_listen_rdma_reg(new_smc, ini->first_contact_local);
}

/* determine the local device matching to proposal */
static int smc_listen_find_device(struct smc_sock *new_smc,
				  struct smc_clc_msg_proposal *pclc,
				  struct smc_init_info *ini)
{
	int prfx_rc;

	/* check for ISM device matching V2 proposed device */
	smc_find_ism_v2_device_serv(new_smc, pclc, ini);
	if (ini->ism_dev[0])
		return 0;

	/* check for matching IP prefix and subnet length (V1) */
	prfx_rc = smc_listen_prfx_check(new_smc, pclc);
	if (prfx_rc)
		smc_find_ism_store_rc(prfx_rc, ini);

	/* get vlan id from IP device */
	if (smc_vlan_by_tcpsk(new_smc->clcsock, ini))
		return ini->rc ?: SMC_CLC_DECL_GETVLANERR;

	/* check for ISM device matching V1 proposed device */
	if (!prfx_rc)
		smc_find_ism_v1_device_serv(new_smc, pclc, ini);
	if (ini->ism_dev[0])
		return 0;

	if (!smcr_indicated(pclc->hdr.typev1) &&
	    !smcr_indicated(pclc->hdr.typev2))
		/* skip RDMA and decline */
		return ini->rc ?: SMC_CLC_DECL_NOSMCDDEV;

	/* check if RDMA V2 is available */
	smc_find_rdma_v2_device_serv(new_smc, pclc, ini);
	if (ini->smcrv2.ib_dev_v2)
		return 0;

	/* check if RDMA V1 is available */
	if (!prfx_rc) {
		int rc;

		rc = smc_find_rdma_v1_device_serv(new_smc, pclc, ini);
		smc_find_ism_store_rc(rc, ini);
		return (!rc) ? 0 : ini->rc;
	}
	return SMC_CLC_DECL_NOSMCDEV;
}

/* listen worker: finish RDMA setup */
static int smc_listen_rdma_finish(struct smc_sock *new_smc,
				  struct smc_clc_msg_accept_confirm *cclc,
				  bool local_first,
				  struct smc_init_info *ini)
{
	struct smc_link *link = new_smc->conn.lnk;
	int reason_code = 0;

	if (local_first)
		smc_link_save_peer_info(link, cclc, ini);

	if (smc_rmb_rtoken_handling(&new_smc->conn, link, cclc))
		return SMC_CLC_DECL_ERR_RTOK;

	if (local_first) {
		if (smc_ib_ready_link(link))
			return SMC_CLC_DECL_ERR_RDYLNK;
		/* QP confirmation over RoCE fabric */
		smc_llc_flow_initiate(link->lgr, SMC_LLC_FLOW_ADD_LINK);
		reason_code = smcr_serv_conf_first_link(new_smc);
		smc_llc_flow_stop(link->lgr, &link->lgr->llc_flow_lcl);
	}
	return reason_code;
}

/* setup for connection of server */
static void smc_listen_work(struct work_struct *work)
{
	struct smc_sock *new_smc = container_of(work, struct smc_sock,
						smc_listen_work);
	struct socket *newclcsock = new_smc->clcsock;
	struct smc_clc_msg_accept_confirm *cclc;
	struct smc_clc_msg_proposal_area *buf;
	struct smc_clc_msg_proposal *pclc;
	struct smc_init_info *ini = NULL;
	u8 proposal_version = SMC_V1;
	u8 accept_version;
	int rc = 0;

	if (new_smc->listen_smc->sk.sk_state != SMC_LISTEN)
		return smc_listen_out_err(new_smc);

	if (new_smc->use_fallback) {
		smc_listen_out_connected(new_smc);
		return;
	}

	/* check if peer is smc capable */
	if (!tcp_sk(newclcsock->sk)->syn_smc) {
		rc = smc_switch_to_fallback(new_smc, SMC_CLC_DECL_PEERNOSMC);
		if (rc)
			smc_listen_out_err(new_smc);
		else
			smc_listen_out_connected(new_smc);
		return;
	}

	/* do inband token exchange -
	 * wait for and receive SMC Proposal CLC message
	 */
	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf) {
		rc = SMC_CLC_DECL_MEM;
		goto out_decl;
	}
	pclc = (struct smc_clc_msg_proposal *)buf;
	rc = smc_clc_wait_msg(new_smc, pclc, sizeof(*buf),
			      SMC_CLC_PROPOSAL, CLC_WAIT_TIME);
	if (rc)
		goto out_decl;

	if (pclc->hdr.version > SMC_V1)
		proposal_version = SMC_V2;

	/* IPSec connections opt out of SMC optimizations */
	if (using_ipsec(new_smc)) {
		rc = SMC_CLC_DECL_IPSEC;
		goto out_decl;
	}

	ini = kzalloc(sizeof(*ini), GFP_KERNEL);
	if (!ini) {
		rc = SMC_CLC_DECL_MEM;
		goto out_decl;
	}

	/* initial version checking */
	rc = smc_listen_v2_check(new_smc, pclc, ini);
	if (rc)
		goto out_decl;

	mutex_lock(&smc_server_lgr_pending);
	smc_close_init(new_smc);
	smc_rx_init(new_smc);
	smc_tx_init(new_smc);

	/* determine ISM or RoCE device used for connection */
	rc = smc_listen_find_device(new_smc, pclc, ini);
	if (rc)
		goto out_unlock;

	/* send SMC Accept CLC message */
	accept_version = ini->is_smcd ? ini->smcd_version : ini->smcr_version;
	rc = smc_clc_send_accept(new_smc, ini->first_contact_local,
				 accept_version, ini->negotiated_eid);
	if (rc)
		goto out_unlock;

	/* SMC-D does not need this lock any more */
	if (ini->is_smcd)
		mutex_unlock(&smc_server_lgr_pending);

	/* receive SMC Confirm CLC message */
	memset(buf, 0, sizeof(*buf));
	cclc = (struct smc_clc_msg_accept_confirm *)buf;
	rc = smc_clc_wait_msg(new_smc, cclc, sizeof(*buf),
			      SMC_CLC_CONFIRM, CLC_WAIT_TIME);
	if (rc) {
		if (!ini->is_smcd)
			goto out_unlock;
		goto out_decl;
	}

	/* finish worker */
	if (!ini->is_smcd) {
		rc = smc_listen_rdma_finish(new_smc, cclc,
					    ini->first_contact_local, ini);
		if (rc)
			goto out_unlock;
		mutex_unlock(&smc_server_lgr_pending);
	}
	smc_conn_save_peer_info(new_smc, cclc);
	smc_listen_out_connected(new_smc);
	SMC_STAT_SERV_SUCC_INC(sock_net(newclcsock->sk), ini);
	goto out_free;

out_unlock:
	mutex_unlock(&smc_server_lgr_pending);
out_decl:
	smc_listen_decline(new_smc, rc, ini ? ini->first_contact_local : 0,
			   proposal_version);
out_free:
	kfree(ini);
	kfree(buf);
}

static void smc_tcp_listen_work(struct work_struct *work)
{
	struct smc_sock *lsmc = container_of(work, struct smc_sock,
					     tcp_listen_work);
	struct sock *lsk = &lsmc->sk;
	struct smc_sock *new_smc;
	int rc = 0;

	lock_sock(lsk);
	while (lsk->sk_state == SMC_LISTEN) {
		rc = smc_clcsock_accept(lsmc, &new_smc);
		if (rc) /* clcsock accept queue empty or error */
			goto out;
		if (!new_smc)
			continue;

		if (tcp_sk(new_smc->clcsock->sk)->syn_smc)
			atomic_inc(&lsmc->queued_smc_hs);

		new_smc->listen_smc = lsmc;
		new_smc->use_fallback = lsmc->use_fallback;
		new_smc->fallback_rsn = lsmc->fallback_rsn;
		sock_hold(lsk); /* sock_put in smc_listen_work */
		INIT_WORK(&new_smc->smc_listen_work, smc_listen_work);
		smc_copy_sock_settings_to_smc(new_smc);
		new_smc->sk.sk_sndbuf = lsmc->sk.sk_sndbuf;
		new_smc->sk.sk_rcvbuf = lsmc->sk.sk_rcvbuf;
		sock_hold(&new_smc->sk); /* sock_put in passive closing */
		if (!queue_work(smc_hs_wq, &new_smc->smc_listen_work))
			sock_put(&new_smc->sk);
	}

out:
	release_sock(lsk);
	sock_put(&lsmc->sk); /* sock_hold in smc_clcsock_data_ready() */
}

static void smc_clcsock_data_ready(struct sock *listen_clcsock)
{
	struct smc_sock *lsmc;

	read_lock_bh(&listen_clcsock->sk_callback_lock);
	lsmc = smc_clcsock_user_data(listen_clcsock);
	if (!lsmc)
		goto out;
	lsmc->clcsk_data_ready(listen_clcsock);
	if (lsmc->sk.sk_state == SMC_LISTEN) {
		sock_hold(&lsmc->sk); /* sock_put in smc_tcp_listen_work() */
		if (!queue_work(smc_tcp_ls_wq, &lsmc->tcp_listen_work))
			sock_put(&lsmc->sk);
	}
out:
	read_unlock_bh(&listen_clcsock->sk_callback_lock);
}

static int smc_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc;

	smc = smc_sk(sk);
	lock_sock(sk);

	rc = -EINVAL;
	if ((sk->sk_state != SMC_INIT && sk->sk_state != SMC_LISTEN) ||
	    smc->connect_nonblock || sock->state != SS_UNCONNECTED)
		goto out;

	rc = 0;
	if (sk->sk_state == SMC_LISTEN) {
		sk->sk_max_ack_backlog = backlog;
		goto out;
	}
	/* some socket options are handled in core, so we could not apply
	 * them to the clc socket -- copy smc socket options to clc socket
	 */
	smc_copy_sock_settings_to_clc(smc);
	if (!smc->use_fallback)
		tcp_sk(smc->clcsock->sk)->syn_smc = 1;

	/* save original sk_data_ready function and establish
	 * smc-specific sk_data_ready function
	 */
	write_lock_bh(&smc->clcsock->sk->sk_callback_lock);
	smc->clcsock->sk->sk_user_data =
		(void *)((uintptr_t)smc | SK_USER_DATA_NOCOPY);
	smc_clcsock_replace_cb(&smc->clcsock->sk->sk_data_ready,
			       smc_clcsock_data_ready, &smc->clcsk_data_ready);
	write_unlock_bh(&smc->clcsock->sk->sk_callback_lock);

	/* save original ops */
	smc->ori_af_ops = inet_csk(smc->clcsock->sk)->icsk_af_ops;

	smc->af_ops = *smc->ori_af_ops;
	smc->af_ops.syn_recv_sock = smc_tcp_syn_recv_sock;

	inet_csk(smc->clcsock->sk)->icsk_af_ops = &smc->af_ops;

	if (smc->limit_smc_hs)
		tcp_sk(smc->clcsock->sk)->smc_hs_congested = smc_hs_congested;

	rc = kernel_listen(smc->clcsock, backlog);
	if (rc) {
		write_lock_bh(&smc->clcsock->sk->sk_callback_lock);
		smc_clcsock_restore_cb(&smc->clcsock->sk->sk_data_ready,
				       &smc->clcsk_data_ready);
		smc->clcsock->sk->sk_user_data = NULL;
		write_unlock_bh(&smc->clcsock->sk->sk_callback_lock);
		goto out;
	}
	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;
	sk->sk_state = SMC_LISTEN;

out:
	release_sock(sk);
	return rc;
}

static int smc_accept(struct socket *sock, struct socket *new_sock,
		      int flags, bool kern)
{
	struct sock *sk = sock->sk, *nsk;
	DECLARE_WAITQUEUE(wait, current);
	struct smc_sock *lsmc;
	long timeo;
	int rc = 0;

	lsmc = smc_sk(sk);
	sock_hold(sk); /* sock_put below */
	lock_sock(sk);

	if (lsmc->sk.sk_state != SMC_LISTEN) {
		rc = -EINVAL;
		release_sock(sk);
		goto out;
	}

	/* Wait for an incoming connection */
	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);
	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	while (!(nsk = smc_accept_dequeue(sk, new_sock))) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!timeo) {
			rc = -EAGAIN;
			break;
		}
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		/* wakeup by sk_data_ready in smc_listen_work() */
		sched_annotate_sleep();
		lock_sock(sk);
		if (signal_pending(current)) {
			rc = sock_intr_errno(timeo);
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);

	if (!rc)
		rc = sock_error(nsk);
	release_sock(sk);
	if (rc)
		goto out;

	if (lsmc->sockopt_defer_accept && !(flags & O_NONBLOCK)) {
		/* wait till data arrives on the socket */
		timeo = msecs_to_jiffies(lsmc->sockopt_defer_accept *
								MSEC_PER_SEC);
		if (smc_sk(nsk)->use_fallback) {
			struct sock *clcsk = smc_sk(nsk)->clcsock->sk;

			lock_sock(clcsk);
			if (skb_queue_empty(&clcsk->sk_receive_queue))
				sk_wait_data(clcsk, &timeo, NULL);
			release_sock(clcsk);
		} else if (!atomic_read(&smc_sk(nsk)->conn.bytes_to_rcv)) {
			lock_sock(nsk);
			smc_rx_wait(smc_sk(nsk), &timeo, smc_rx_data_available);
			release_sock(nsk);
		}
	}

out:
	sock_put(sk); /* sock_hold above */
	return rc;
}

static int smc_getname(struct socket *sock, struct sockaddr *addr,
		       int peer)
{
	struct smc_sock *smc;

	if (peer && (sock->sk->sk_state != SMC_ACTIVE) &&
	    (sock->sk->sk_state != SMC_APPCLOSEWAIT1))
		return -ENOTCONN;

	smc = smc_sk(sock->sk);

	return smc->clcsock->ops->getname(smc->clcsock, addr, peer);
}

static int smc_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = -EPIPE;

	smc = smc_sk(sk);
	lock_sock(sk);
	if ((sk->sk_state != SMC_ACTIVE) &&
	    (sk->sk_state != SMC_APPCLOSEWAIT1) &&
	    (sk->sk_state != SMC_INIT))
		goto out;

	if (msg->msg_flags & MSG_FASTOPEN) {
		if (sk->sk_state == SMC_INIT && !smc->connect_nonblock) {
			rc = smc_switch_to_fallback(smc, SMC_CLC_DECL_OPTUNSUPP);
			if (rc)
				goto out;
		} else {
			rc = -EINVAL;
			goto out;
		}
	}

	if (smc->use_fallback) {
		rc = smc->clcsock->ops->sendmsg(smc->clcsock, msg, len);
	} else {
		rc = smc_tx_sendmsg(smc, msg, len);
		SMC_STAT_TX_PAYLOAD(smc, len, rc);
	}
out:
	release_sock(sk);
	return rc;
}

static int smc_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		       int flags)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = -ENOTCONN;

	smc = smc_sk(sk);
	lock_sock(sk);
	if (sk->sk_state == SMC_CLOSED && (sk->sk_shutdown & RCV_SHUTDOWN)) {
		/* socket was connected before, no more data to read */
		rc = 0;
		goto out;
	}
	if ((sk->sk_state == SMC_INIT) ||
	    (sk->sk_state == SMC_LISTEN) ||
	    (sk->sk_state == SMC_CLOSED))
		goto out;

	if (sk->sk_state == SMC_PEERFINCLOSEWAIT) {
		rc = 0;
		goto out;
	}

	if (smc->use_fallback) {
		rc = smc->clcsock->ops->recvmsg(smc->clcsock, msg, len, flags);
	} else {
		msg->msg_namelen = 0;
		rc = smc_rx_recvmsg(smc, msg, NULL, len, flags);
		SMC_STAT_RX_PAYLOAD(smc, rc, rc);
	}

out:
	release_sock(sk);
	return rc;
}

static __poll_t smc_accept_poll(struct sock *parent)
{
	struct smc_sock *isk = smc_sk(parent);
	__poll_t mask = 0;

	spin_lock(&isk->accept_q_lock);
	if (!list_empty(&isk->accept_q))
		mask = EPOLLIN | EPOLLRDNORM;
	spin_unlock(&isk->accept_q_lock);

	return mask;
}

static __poll_t smc_poll(struct file *file, struct socket *sock,
			     poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	__poll_t mask = 0;

	if (!sk)
		return EPOLLNVAL;

	smc = smc_sk(sock->sk);
	if (smc->use_fallback) {
		/* delegate to CLC child sock */
		mask = smc->clcsock->ops->poll(file, smc->clcsock, wait);
		sk->sk_err = smc->clcsock->sk->sk_err;
	} else {
		if (sk->sk_state != SMC_CLOSED)
			sock_poll_wait(file, sock, wait);
		if (sk->sk_err)
			mask |= EPOLLERR;
		if ((sk->sk_shutdown == SHUTDOWN_MASK) ||
		    (sk->sk_state == SMC_CLOSED))
			mask |= EPOLLHUP;
		if (sk->sk_state == SMC_LISTEN) {
			/* woken up by sk_data_ready in smc_listen_work() */
			mask |= smc_accept_poll(sk);
		} else if (smc->use_fallback) { /* as result of connect_work()*/
			mask |= smc->clcsock->ops->poll(file, smc->clcsock,
							   wait);
			sk->sk_err = smc->clcsock->sk->sk_err;
		} else {
			if ((sk->sk_state != SMC_INIT &&
			     atomic_read(&smc->conn.sndbuf_space)) ||
			    sk->sk_shutdown & SEND_SHUTDOWN) {
				mask |= EPOLLOUT | EPOLLWRNORM;
			} else {
				sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);
				set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
			}
			if (atomic_read(&smc->conn.bytes_to_rcv))
				mask |= EPOLLIN | EPOLLRDNORM;
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				mask |= EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;
			if (sk->sk_state == SMC_APPCLOSEWAIT1)
				mask |= EPOLLIN;
			if (smc->conn.urg_state == SMC_URG_VALID)
				mask |= EPOLLPRI;
		}
	}

	return mask;
}

static int smc_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	bool do_shutdown = true;
	struct smc_sock *smc;
	int rc = -EINVAL;
	int old_state;
	int rc1 = 0;

	smc = smc_sk(sk);

	if ((how < SHUT_RD) || (how > SHUT_RDWR))
		return rc;

	lock_sock(sk);

	if (sock->state == SS_CONNECTING) {
		if (sk->sk_state == SMC_ACTIVE)
			sock->state = SS_CONNECTED;
		else if (sk->sk_state == SMC_PEERCLOSEWAIT1 ||
			 sk->sk_state == SMC_PEERCLOSEWAIT2 ||
			 sk->sk_state == SMC_APPCLOSEWAIT1 ||
			 sk->sk_state == SMC_APPCLOSEWAIT2 ||
			 sk->sk_state == SMC_APPFINCLOSEWAIT)
			sock->state = SS_DISCONNECTING;
	}

	rc = -ENOTCONN;
	if ((sk->sk_state != SMC_ACTIVE) &&
	    (sk->sk_state != SMC_PEERCLOSEWAIT1) &&
	    (sk->sk_state != SMC_PEERCLOSEWAIT2) &&
	    (sk->sk_state != SMC_APPCLOSEWAIT1) &&
	    (sk->sk_state != SMC_APPCLOSEWAIT2) &&
	    (sk->sk_state != SMC_APPFINCLOSEWAIT))
		goto out;
	if (smc->use_fallback) {
		rc = kernel_sock_shutdown(smc->clcsock, how);
		sk->sk_shutdown = smc->clcsock->sk->sk_shutdown;
		if (sk->sk_shutdown == SHUTDOWN_MASK) {
			sk->sk_state = SMC_CLOSED;
			sk->sk_socket->state = SS_UNCONNECTED;
			sock_put(sk);
		}
		goto out;
	}
	switch (how) {
	case SHUT_RDWR:		/* shutdown in both directions */
		old_state = sk->sk_state;
		rc = smc_close_active(smc);
		if (old_state == SMC_ACTIVE &&
		    sk->sk_state == SMC_PEERCLOSEWAIT1)
			do_shutdown = false;
		break;
	case SHUT_WR:
		rc = smc_close_shutdown_write(smc);
		break;
	case SHUT_RD:
		rc = 0;
		/* nothing more to do because peer is not involved */
		break;
	}
	if (do_shutdown && smc->clcsock)
		rc1 = kernel_sock_shutdown(smc->clcsock, how);
	/* map sock_shutdown_cmd constants to sk_shutdown value range */
	sk->sk_shutdown |= how + 1;

	if (sk->sk_state == SMC_CLOSED)
		sock->state = SS_UNCONNECTED;
	else
		sock->state = SS_DISCONNECTING;
out:
	release_sock(sk);
	return rc ? rc : rc1;
}

static int __smc_getsockopt(struct socket *sock, int level, int optname,
			    char __user *optval, int __user *optlen)
{
	struct smc_sock *smc;
	int val, len;

	smc = smc_sk(sock->sk);

	if (get_user(len, optlen))
		return -EFAULT;

	len = min_t(int, len, sizeof(int));

	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case SMC_LIMIT_HS:
		val = smc->limit_smc_hs;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

static int __smc_setsockopt(struct socket *sock, int level, int optname,
			    sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int val, rc;

	smc = smc_sk(sk);

	lock_sock(sk);
	switch (optname) {
	case SMC_LIMIT_HS:
		if (optlen < sizeof(int)) {
			rc = -EINVAL;
			break;
		}
		if (copy_from_sockptr(&val, optval, sizeof(int))) {
			rc = -EFAULT;
			break;
		}

		smc->limit_smc_hs = !!val;
		rc = 0;
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}
	release_sock(sk);

	return rc;
}

static int smc_setsockopt(struct socket *sock, int level, int optname,
			  sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int val, rc;

	if (level == SOL_TCP && optname == TCP_ULP)
		return -EOPNOTSUPP;
	else if (level == SOL_SMC)
		return __smc_setsockopt(sock, level, optname, optval, optlen);

	smc = smc_sk(sk);

	/* generic setsockopts reaching us here always apply to the
	 * CLC socket
	 */
	mutex_lock(&smc->clcsock_release_lock);
	if (!smc->clcsock) {
		mutex_unlock(&smc->clcsock_release_lock);
		return -EBADF;
	}
	if (unlikely(!smc->clcsock->ops->setsockopt))
		rc = -EOPNOTSUPP;
	else
		rc = smc->clcsock->ops->setsockopt(smc->clcsock, level, optname,
						   optval, optlen);
	if (smc->clcsock->sk->sk_err) {
		sk->sk_err = smc->clcsock->sk->sk_err;
		sk_error_report(sk);
	}
	mutex_unlock(&smc->clcsock_release_lock);

	if (optlen < sizeof(int))
		return -EINVAL;
	if (copy_from_sockptr(&val, optval, sizeof(int)))
		return -EFAULT;

	lock_sock(sk);
	if (rc || smc->use_fallback)
		goto out;
	switch (optname) {
	case TCP_FASTOPEN:
	case TCP_FASTOPEN_CONNECT:
	case TCP_FASTOPEN_KEY:
	case TCP_FASTOPEN_NO_COOKIE:
		/* option not supported by SMC */
		if (sk->sk_state == SMC_INIT && !smc->connect_nonblock) {
			rc = smc_switch_to_fallback(smc, SMC_CLC_DECL_OPTUNSUPP);
		} else {
			rc = -EINVAL;
		}
		break;
	case TCP_NODELAY:
		if (sk->sk_state != SMC_INIT &&
		    sk->sk_state != SMC_LISTEN &&
		    sk->sk_state != SMC_CLOSED) {
			if (val) {
				SMC_STAT_INC(smc, ndly_cnt);
				smc_tx_pending(&smc->conn);
				cancel_delayed_work(&smc->conn.tx_work);
			}
		}
		break;
	case TCP_CORK:
		if (sk->sk_state != SMC_INIT &&
		    sk->sk_state != SMC_LISTEN &&
		    sk->sk_state != SMC_CLOSED) {
			if (!val) {
				SMC_STAT_INC(smc, cork_cnt);
				smc_tx_pending(&smc->conn);
				cancel_delayed_work(&smc->conn.tx_work);
			}
		}
		break;
	case TCP_DEFER_ACCEPT:
		smc->sockopt_defer_accept = val;
		break;
	default:
		break;
	}
out:
	release_sock(sk);

	return rc;
}

static int smc_getsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct smc_sock *smc;
	int rc;

	if (level == SOL_SMC)
		return __smc_getsockopt(sock, level, optname, optval, optlen);

	smc = smc_sk(sock->sk);
	mutex_lock(&smc->clcsock_release_lock);
	if (!smc->clcsock) {
		mutex_unlock(&smc->clcsock_release_lock);
		return -EBADF;
	}
	/* socket options apply to the CLC socket */
	if (unlikely(!smc->clcsock->ops->getsockopt)) {
		mutex_unlock(&smc->clcsock_release_lock);
		return -EOPNOTSUPP;
	}
	rc = smc->clcsock->ops->getsockopt(smc->clcsock, level, optname,
					   optval, optlen);
	mutex_unlock(&smc->clcsock_release_lock);
	return rc;
}

static int smc_ioctl(struct socket *sock, unsigned int cmd,
		     unsigned long arg)
{
	union smc_host_cursor cons, urg;
	struct smc_connection *conn;
	struct smc_sock *smc;
	int answ;

	smc = smc_sk(sock->sk);
	conn = &smc->conn;
	lock_sock(&smc->sk);
	if (smc->use_fallback) {
		if (!smc->clcsock) {
			release_sock(&smc->sk);
			return -EBADF;
		}
		answ = smc->clcsock->ops->ioctl(smc->clcsock, cmd, arg);
		release_sock(&smc->sk);
		return answ;
	}
	switch (cmd) {
	case SIOCINQ: /* same as FIONREAD */
		if (smc->sk.sk_state == SMC_LISTEN) {
			release_sock(&smc->sk);
			return -EINVAL;
		}
		if (smc->sk.sk_state == SMC_INIT ||
		    smc->sk.sk_state == SMC_CLOSED)
			answ = 0;
		else
			answ = atomic_read(&smc->conn.bytes_to_rcv);
		break;
	case SIOCOUTQ:
		/* output queue size (not send + not acked) */
		if (smc->sk.sk_state == SMC_LISTEN) {
			release_sock(&smc->sk);
			return -EINVAL;
		}
		if (smc->sk.sk_state == SMC_INIT ||
		    smc->sk.sk_state == SMC_CLOSED)
			answ = 0;
		else
			answ = smc->conn.sndbuf_desc->len -
					atomic_read(&smc->conn.sndbuf_space);
		break;
	case SIOCOUTQNSD:
		/* output queue size (not send only) */
		if (smc->sk.sk_state == SMC_LISTEN) {
			release_sock(&smc->sk);
			return -EINVAL;
		}
		if (smc->sk.sk_state == SMC_INIT ||
		    smc->sk.sk_state == SMC_CLOSED)
			answ = 0;
		else
			answ = smc_tx_prepared_sends(&smc->conn);
		break;
	case SIOCATMARK:
		if (smc->sk.sk_state == SMC_LISTEN) {
			release_sock(&smc->sk);
			return -EINVAL;
		}
		if (smc->sk.sk_state == SMC_INIT ||
		    smc->sk.sk_state == SMC_CLOSED) {
			answ = 0;
		} else {
			smc_curs_copy(&cons, &conn->local_tx_ctrl.cons, conn);
			smc_curs_copy(&urg, &conn->urg_curs, conn);
			answ = smc_curs_diff(conn->rmb_desc->len,
					     &cons, &urg) == 1;
		}
		break;
	default:
		release_sock(&smc->sk);
		return -ENOIOCTLCMD;
	}
	release_sock(&smc->sk);

	return put_user(answ, (int __user *)arg);
}

static ssize_t smc_sendpage(struct socket *sock, struct page *page,
			    int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = -EPIPE;

	smc = smc_sk(sk);
	lock_sock(sk);
	if (sk->sk_state != SMC_ACTIVE) {
		release_sock(sk);
		goto out;
	}
	release_sock(sk);
	if (smc->use_fallback) {
		rc = kernel_sendpage(smc->clcsock, page, offset,
				     size, flags);
	} else {
		lock_sock(sk);
		rc = smc_tx_sendpage(smc, page, offset, size, flags);
		release_sock(sk);
		SMC_STAT_INC(smc, sendpage_cnt);
	}

out:
	return rc;
}

/* Map the affected portions of the rmbe into an spd, note the number of bytes
 * to splice in conn->splice_pending, and press 'go'. Delays consumer cursor
 * updates till whenever a respective page has been fully processed.
 * Note that subsequent recv() calls have to wait till all splice() processing
 * completed.
 */
static ssize_t smc_splice_read(struct socket *sock, loff_t *ppos,
			       struct pipe_inode_info *pipe, size_t len,
			       unsigned int flags)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = -ENOTCONN;

	smc = smc_sk(sk);
	lock_sock(sk);
	if (sk->sk_state == SMC_CLOSED && (sk->sk_shutdown & RCV_SHUTDOWN)) {
		/* socket was connected before, no more data to read */
		rc = 0;
		goto out;
	}
	if (sk->sk_state == SMC_INIT ||
	    sk->sk_state == SMC_LISTEN ||
	    sk->sk_state == SMC_CLOSED)
		goto out;

	if (sk->sk_state == SMC_PEERFINCLOSEWAIT) {
		rc = 0;
		goto out;
	}

	if (smc->use_fallback) {
		rc = smc->clcsock->ops->splice_read(smc->clcsock, ppos,
						    pipe, len, flags);
	} else {
		if (*ppos) {
			rc = -ESPIPE;
			goto out;
		}
		if (flags & SPLICE_F_NONBLOCK)
			flags = MSG_DONTWAIT;
		else
			flags = 0;
		SMC_STAT_INC(smc, splice_cnt);
		rc = smc_rx_recvmsg(smc, NULL, pipe, len, flags);
	}
out:
	release_sock(sk);

	return rc;
}

/* must look like tcp */
static const struct proto_ops smc_sock_ops = {
	.family		= PF_SMC,
	.owner		= THIS_MODULE,
	.release	= smc_release,
	.bind		= smc_bind,
	.connect	= smc_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= smc_accept,
	.getname	= smc_getname,
	.poll		= smc_poll,
	.ioctl		= smc_ioctl,
	.listen		= smc_listen,
	.shutdown	= smc_shutdown,
	.setsockopt	= smc_setsockopt,
	.getsockopt	= smc_getsockopt,
	.sendmsg	= smc_sendmsg,
	.recvmsg	= smc_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= smc_sendpage,
	.splice_read	= smc_splice_read,
};

static int __smc_create(struct net *net, struct socket *sock, int protocol,
			int kern, struct socket *clcsock)
{
	int family = (protocol == SMCPROTO_SMC6) ? PF_INET6 : PF_INET;
	struct smc_sock *smc;
	struct sock *sk;
	int rc;

	rc = -ESOCKTNOSUPPORT;
	if (sock->type != SOCK_STREAM)
		goto out;

	rc = -EPROTONOSUPPORT;
	if (protocol != SMCPROTO_SMC && protocol != SMCPROTO_SMC6)
		goto out;

	rc = -ENOBUFS;
	sock->ops = &smc_sock_ops;
	sock->state = SS_UNCONNECTED;
	sk = smc_sock_alloc(net, sock, protocol);
	if (!sk)
		goto out;

	/* create internal TCP socket for CLC handshake and fallback */
	smc = smc_sk(sk);
	smc->use_fallback = false; /* assume rdma capability first */
	smc->fallback_rsn = 0;

	/* default behavior from limit_smc_hs in every net namespace */
	smc->limit_smc_hs = net->smc.limit_smc_hs;

	rc = 0;
	if (!clcsock) {
		rc = sock_create_kern(net, family, SOCK_STREAM, IPPROTO_TCP,
				      &smc->clcsock);
		if (rc) {
			sk_common_release(sk);
			goto out;
		}
	} else {
		smc->clcsock = clcsock;
	}

	smc->sk.sk_sndbuf = max(smc->clcsock->sk->sk_sndbuf, SMC_BUF_MIN_SIZE);
	smc->sk.sk_rcvbuf = max(smc->clcsock->sk->sk_rcvbuf, SMC_BUF_MIN_SIZE);

out:
	return rc;
}

static int smc_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	return __smc_create(net, sock, protocol, kern, NULL);
}

static const struct net_proto_family smc_sock_family_ops = {
	.family	= PF_SMC,
	.owner	= THIS_MODULE,
	.create	= smc_create,
};

static int smc_ulp_init(struct sock *sk)
{
	struct socket *tcp = sk->sk_socket;
	struct net *net = sock_net(sk);
	struct socket *smcsock;
	int protocol, ret;

	/* only TCP can be replaced */
	if (tcp->type != SOCK_STREAM || sk->sk_protocol != IPPROTO_TCP ||
	    (sk->sk_family != AF_INET && sk->sk_family != AF_INET6))
		return -ESOCKTNOSUPPORT;
	/* don't handle wq now */
	if (tcp->state != SS_UNCONNECTED || !tcp->file || tcp->wq.fasync_list)
		return -ENOTCONN;

	if (sk->sk_family == AF_INET)
		protocol = SMCPROTO_SMC;
	else
		protocol = SMCPROTO_SMC6;

	smcsock = sock_alloc();
	if (!smcsock)
		return -ENFILE;

	smcsock->type = SOCK_STREAM;
	__module_get(THIS_MODULE); /* tried in __tcp_ulp_find_autoload */
	ret = __smc_create(net, smcsock, protocol, 1, tcp);
	if (ret) {
		sock_release(smcsock); /* module_put() which ops won't be NULL */
		return ret;
	}

	/* replace tcp socket to smc */
	smcsock->file = tcp->file;
	smcsock->file->private_data = smcsock;
	smcsock->file->f_inode = SOCK_INODE(smcsock); /* replace inode when sock_close */
	smcsock->file->f_path.dentry->d_inode = SOCK_INODE(smcsock); /* dput() in __fput */
	tcp->file = NULL;

	return ret;
}

static void smc_ulp_clone(const struct request_sock *req, struct sock *newsk,
			  const gfp_t priority)
{
	struct inet_connection_sock *icsk = inet_csk(newsk);

	/* don't inherit ulp ops to child when listen */
	icsk->icsk_ulp_ops = NULL;
}

static struct tcp_ulp_ops smc_ulp_ops __read_mostly = {
	.name		= "smc",
	.owner		= THIS_MODULE,
	.init		= smc_ulp_init,
	.clone		= smc_ulp_clone,
};

unsigned int smc_net_id;

static __net_init int smc_net_init(struct net *net)
{
	int rc;

	rc = smc_sysctl_net_init(net);
	if (rc)
		return rc;
	return smc_pnet_net_init(net);
}

static void __net_exit smc_net_exit(struct net *net)
{
	smc_sysctl_net_exit(net);
	smc_pnet_net_exit(net);
}

static __net_init int smc_net_stat_init(struct net *net)
{
	return smc_stats_init(net);
}

static void __net_exit smc_net_stat_exit(struct net *net)
{
	smc_stats_exit(net);
}

static struct pernet_operations smc_net_ops = {
	.init = smc_net_init,
	.exit = smc_net_exit,
	.id   = &smc_net_id,
	.size = sizeof(struct smc_net),
};

static struct pernet_operations smc_net_stat_ops = {
	.init = smc_net_stat_init,
	.exit = smc_net_stat_exit,
};

static int __init smc_init(void)
{
	int rc;

	rc = register_pernet_subsys(&smc_net_ops);
	if (rc)
		return rc;

	rc = register_pernet_subsys(&smc_net_stat_ops);
	if (rc)
		return rc;

	smc_ism_init();
	smc_clc_init();

	rc = smc_nl_init();
	if (rc)
		goto out_pernet_subsys;

	rc = smc_pnet_init();
	if (rc)
		goto out_nl;

	rc = -ENOMEM;

	smc_tcp_ls_wq = alloc_workqueue("smc_tcp_ls_wq", 0, 0);
	if (!smc_tcp_ls_wq)
		goto out_pnet;

	smc_hs_wq = alloc_workqueue("smc_hs_wq", 0, 0);
	if (!smc_hs_wq)
		goto out_alloc_tcp_ls_wq;

	smc_close_wq = alloc_workqueue("smc_close_wq", 0, 0);
	if (!smc_close_wq)
		goto out_alloc_hs_wq;

	rc = smc_core_init();
	if (rc) {
		pr_err("%s: smc_core_init fails with %d\n", __func__, rc);
		goto out_alloc_wqs;
	}

	rc = smc_llc_init();
	if (rc) {
		pr_err("%s: smc_llc_init fails with %d\n", __func__, rc);
		goto out_core;
	}

	rc = smc_cdc_init();
	if (rc) {
		pr_err("%s: smc_cdc_init fails with %d\n", __func__, rc);
		goto out_core;
	}

	rc = proto_register(&smc_proto, 1);
	if (rc) {
		pr_err("%s: proto_register(v4) fails with %d\n", __func__, rc);
		goto out_core;
	}

	rc = proto_register(&smc_proto6, 1);
	if (rc) {
		pr_err("%s: proto_register(v6) fails with %d\n", __func__, rc);
		goto out_proto;
	}

	rc = sock_register(&smc_sock_family_ops);
	if (rc) {
		pr_err("%s: sock_register fails with %d\n", __func__, rc);
		goto out_proto6;
	}
	INIT_HLIST_HEAD(&smc_v4_hashinfo.ht);
	INIT_HLIST_HEAD(&smc_v6_hashinfo.ht);

	rc = smc_ib_register_client();
	if (rc) {
		pr_err("%s: ib_register fails with %d\n", __func__, rc);
		goto out_sock;
	}

	rc = tcp_register_ulp(&smc_ulp_ops);
	if (rc) {
		pr_err("%s: tcp_ulp_register fails with %d\n", __func__, rc);
		goto out_ib;
	}

	static_branch_enable(&tcp_have_smc);
	return 0;

out_ib:
	smc_ib_unregister_client();
out_sock:
	sock_unregister(PF_SMC);
out_proto6:
	proto_unregister(&smc_proto6);
out_proto:
	proto_unregister(&smc_proto);
out_core:
	smc_core_exit();
out_alloc_wqs:
	destroy_workqueue(smc_close_wq);
out_alloc_hs_wq:
	destroy_workqueue(smc_hs_wq);
out_alloc_tcp_ls_wq:
	destroy_workqueue(smc_tcp_ls_wq);
out_pnet:
	smc_pnet_exit();
out_nl:
	smc_nl_exit();
out_pernet_subsys:
	unregister_pernet_subsys(&smc_net_ops);

	return rc;
}

static void __exit smc_exit(void)
{
	static_branch_disable(&tcp_have_smc);
	tcp_unregister_ulp(&smc_ulp_ops);
	sock_unregister(PF_SMC);
	smc_core_exit();
	smc_ib_unregister_client();
	destroy_workqueue(smc_close_wq);
	destroy_workqueue(smc_tcp_ls_wq);
	destroy_workqueue(smc_hs_wq);
	proto_unregister(&smc_proto6);
	proto_unregister(&smc_proto);
	smc_pnet_exit();
	smc_nl_exit();
	smc_clc_exit();
	unregister_pernet_subsys(&smc_net_stat_ops);
	unregister_pernet_subsys(&smc_net_ops);
	rcu_barrier();
}

module_init(smc_init);
module_exit(smc_exit);

MODULE_AUTHOR("Ursula Braun <ubraun@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("smc socket address family");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_SMC);
MODULE_ALIAS_TCP_ULP("smc");
MODULE_ALIAS_GENL_FAMILY(SMC_GENL_FAMILY_NAME);
