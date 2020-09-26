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
#include "smc_tx.h"
#include "smc_rx.h"
#include "smc_close.h"

static DEFINE_MUTEX(smc_server_lgr_pending);	/* serialize link group
						 * creation on server
						 */
static DEFINE_MUTEX(smc_client_lgr_pending);	/* serialize link group
						 * creation on client
						 */

struct workqueue_struct	*smc_hs_wq;	/* wq for handshake work */
struct workqueue_struct	*smc_close_wq;	/* wq for close work */

static void smc_tcp_listen_work(struct work_struct *);
static void smc_connect_work(struct work_struct *);

static void smc_set_keepalive(struct sock *sk, int val)
{
	struct smc_sock *smc = smc_sk(sk);

	smc->clcsock->sk->sk_prot->keepalive(smc->clcsock->sk, val);
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
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock_bh(&h->lock);

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

struct proto smc_proto = {
	.name		= "SMC",
	.owner		= THIS_MODULE,
	.keepalive	= smc_set_keepalive,
	.hash		= smc_hash_sk,
	.unhash		= smc_unhash_sk,
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
	.obj_size	= sizeof(struct smc_sock),
	.h.smc_hash	= &smc_v6_hashinfo,
	.slab_flags	= SLAB_TYPESAFE_BY_RCU,
};
EXPORT_SYMBOL_GPL(smc_proto6);

static void smc_restore_fallback_changes(struct smc_sock *smc)
{
	if (smc->clcsock->file) { /* non-accepted sockets have no file yet */
		smc->clcsock->file->private_data = smc->sk.sk_socket;
		smc->clcsock->file = NULL;
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
		if (sk->sk_state != SMC_LISTEN && sk->sk_state != SMC_INIT)
			sock_put(sk); /* passive closing */
		if (sk->sk_state == SMC_LISTEN) {
			/* wake up clcsock accept */
			rc = kernel_sock_shutdown(smc->clcsock, SHUT_RDWR);
		}
		sk->sk_state = SMC_CLOSED;
		sk->sk_state_change(sk);
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
	int rc = 0;

	if (!sk)
		goto out;

	sock_hold(sk); /* sock_put below */
	smc = smc_sk(sk);

	/* cleanup for a dangling non-blocking connect */
	if (smc->connect_nonblock && sk->sk_state == SMC_INIT)
		tcp_abort(smc->clcsock->sk, ECONNABORTED);
	flush_work(&smc->connect_work);

	if (sk->sk_state == SMC_LISTEN)
		/* smc_close_non_accepted() is called and acquires
		 * sock lock for child sockets again
		 */
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);
	else
		lock_sock(sk);

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
	 * parallel smcr_link_reg_rmb()
	 */
	mutex_lock(&lgr->llc_conf_mutex);
	for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
		if (!smc_link_active(&lgr->lnk[i]))
			continue;
		rc = smcr_link_reg_rmb(&lgr->lnk[i], rmb_desc);
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

	if (smcr_link_reg_rmb(link, smc->conn.rmb_desc))
		return SMC_CLC_DECL_ERR_REGRMB;

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
}

static void smc_link_save_peer_info(struct smc_link *link,
				    struct smc_clc_msg_accept_confirm *clc)
{
	link->peer_qpn = ntoh24(clc->r0.qpn);
	memcpy(link->peer_gid, clc->r0.lcl.gid, SMC_GID_SIZE);
	memcpy(link->peer_mac, clc->r0.lcl.mac, sizeof(link->peer_mac));
	link->peer_psn = ntoh24(clc->r0.psn);
	link->peer_mtu = clc->r0.qp_mtu;
}

static void smc_switch_to_fallback(struct smc_sock *smc)
{
	smc->use_fallback = true;
	if (smc->sk.sk_socket && smc->sk.sk_socket->file) {
		smc->clcsock->file = smc->sk.sk_socket->file;
		smc->clcsock->file->private_data = smc->clcsock;
		smc->clcsock->wq.fasync_list =
			smc->sk.sk_socket->wq.fasync_list;
	}
}

/* fall back during connect */
static int smc_connect_fallback(struct smc_sock *smc, int reason_code)
{
	smc_switch_to_fallback(smc);
	smc->fallback_rsn = reason_code;
	smc_copy_sock_settings_to_clc(smc);
	smc->connect_nonblock = 0;
	if (smc->sk.sk_state == SMC_INIT)
		smc->sk.sk_state = SMC_ACTIVE;
	return 0;
}

/* decline and fall back during connect */
static int smc_connect_decline_fallback(struct smc_sock *smc, int reason_code)
{
	int rc;

	if (reason_code < 0) { /* error, fallback is not possible */
		if (smc->sk.sk_state == SMC_INIT)
			sock_put(&smc->sk); /* passive closing */
		return reason_code;
	}
	if (reason_code != SMC_CLC_DECL_PEERDECL) {
		rc = smc_clc_send_decline(smc, reason_code);
		if (rc < 0) {
			if (smc->sk.sk_state == SMC_INIT)
				sock_put(&smc->sk); /* passive closing */
			return rc;
		}
	}
	return smc_connect_fallback(smc, reason_code);
}

/* abort connecting */
static int smc_connect_abort(struct smc_sock *smc, int reason_code,
			     int local_first)
{
	bool is_smcd = smc->conn.lgr->is_smcd;

	if (local_first)
		smc_lgr_cleanup_early(&smc->conn);
	else
		smc_conn_free(&smc->conn);
	if (is_smcd)
		/* there is only one lgr role for SMC-D; use server lock */
		mutex_unlock(&smc_server_lgr_pending);
	else
		mutex_unlock(&smc_client_lgr_pending);

	smc->connect_nonblock = 0;
	return reason_code;
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
	if (!ini->ib_dev)
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
	return 0;
}

/* Check for VLAN ID and register it on ISM device just for CLC handshake */
static int smc_connect_ism_vlan_setup(struct smc_sock *smc,
				      struct smc_init_info *ini)
{
	if (ini->vlan_id && smc_ism_get_vlan(ini->ism_dev[0], ini->vlan_id))
		return SMC_CLC_DECL_ISMVLANERR;
	return 0;
}

/* cleanup temporary VLAN ID registration used for CLC handshake. If ISM is
 * used, the VLAN ID will be registered again during the connection setup.
 */
static int smc_connect_ism_vlan_cleanup(struct smc_sock *smc, bool is_smcd,
					struct smc_init_info *ini)
{
	if (!is_smcd)
		return 0;
	if (ini->vlan_id && smc_ism_put_vlan(ini->ism_dev[0], ini->vlan_id))
		return SMC_CLC_DECL_CNFERR;
	return 0;
}

/* CLC handshake during connect */
static int smc_connect_clc(struct smc_sock *smc, int smc_type,
			   struct smc_clc_msg_accept_confirm *aclc,
			   struct smc_init_info *ini)
{
	int rc = 0;

	/* do inband token exchange */
	rc = smc_clc_send_proposal(smc, smc_type, ini);
	if (rc)
		return rc;
	/* receive SMC Accept CLC message */
	return smc_clc_wait_msg(smc, aclc, sizeof(*aclc), SMC_CLC_ACCEPT,
				CLC_WAIT_TIME);
}

/* setup for RDMA connection of client */
static int smc_connect_rdma(struct smc_sock *smc,
			    struct smc_clc_msg_accept_confirm *aclc,
			    struct smc_init_info *ini)
{
	int i, reason_code = 0;
	struct smc_link *link;

	ini->is_smcd = false;
	ini->ib_lcl = &aclc->r0.lcl;
	ini->ib_clcqpn = ntoh24(aclc->r0.qpn);
	ini->first_contact_peer = aclc->hdr.typev2 & SMC_FIRST_CONTACT_MASK;

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
			    !memcmp(l->peer_mac, &aclc->r0.lcl.mac,
				    sizeof(l->peer_mac))) {
				link = l;
				break;
			}
		}
		if (!link)
			return smc_connect_abort(smc, SMC_CLC_DECL_NOSRVLINK,
						 ini->first_contact_local);
		smc->conn.lnk = link;
	}

	/* create send buffer and rmb */
	if (smc_buf_create(smc, false))
		return smc_connect_abort(smc, SMC_CLC_DECL_MEM,
					 ini->first_contact_local);

	if (ini->first_contact_local)
		smc_link_save_peer_info(link, aclc);

	if (smc_rmb_rtoken_handling(&smc->conn, link, aclc))
		return smc_connect_abort(smc, SMC_CLC_DECL_ERR_RTOK,
					 ini->first_contact_local);

	smc_close_init(smc);
	smc_rx_init(smc);

	if (ini->first_contact_local) {
		if (smc_ib_ready_link(link))
			return smc_connect_abort(smc, SMC_CLC_DECL_ERR_RDYLNK,
						 ini->first_contact_local);
	} else {
		if (smcr_lgr_reg_rmbs(link, smc->conn.rmb_desc))
			return smc_connect_abort(smc, SMC_CLC_DECL_ERR_REGRMB,
						 ini->first_contact_local);
	}
	smc_rmb_sync_sg_for_device(&smc->conn);

	reason_code = smc_clc_send_confirm(smc);
	if (reason_code)
		return smc_connect_abort(smc, reason_code,
					 ini->first_contact_local);

	smc_tx_init(smc);

	if (ini->first_contact_local) {
		/* QP confirmation over RoCE fabric */
		smc_llc_flow_initiate(link->lgr, SMC_LLC_FLOW_ADD_LINK);
		reason_code = smcr_clnt_conf_first_link(smc);
		smc_llc_flow_stop(link->lgr, &link->lgr->llc_flow_lcl);
		if (reason_code)
			return smc_connect_abort(smc, reason_code,
						 ini->first_contact_local);
	}
	mutex_unlock(&smc_client_lgr_pending);

	smc_copy_sock_settings_to_clc(smc);
	smc->connect_nonblock = 0;
	if (smc->sk.sk_state == SMC_INIT)
		smc->sk.sk_state = SMC_ACTIVE;

	return 0;
}

/* setup for ISM connection of client */
static int smc_connect_ism(struct smc_sock *smc,
			   struct smc_clc_msg_accept_confirm *aclc,
			   struct smc_init_info *ini)
{
	int rc = 0;

	ini->is_smcd = true;
	ini->ism_peer_gid[0] = aclc->d0.gid;
	ini->first_contact_peer = aclc->hdr.typev2 & SMC_FIRST_CONTACT_MASK;

	/* there is only one lgr role for SMC-D; use server lock */
	mutex_lock(&smc_server_lgr_pending);
	rc = smc_conn_create(smc, ini);
	if (rc) {
		mutex_unlock(&smc_server_lgr_pending);
		return rc;
	}

	/* Create send and receive buffers */
	rc = smc_buf_create(smc, true);
	if (rc)
		return smc_connect_abort(smc, (rc == -ENOSPC) ?
					      SMC_CLC_DECL_MAX_DMB :
					      SMC_CLC_DECL_MEM,
					 ini->first_contact_local);

	smc_conn_save_peer_info(smc, aclc);
	smc_close_init(smc);
	smc_rx_init(smc);
	smc_tx_init(smc);

	rc = smc_clc_send_confirm(smc);
	if (rc)
		return smc_connect_abort(smc, rc, ini->first_contact_local);
	mutex_unlock(&smc_server_lgr_pending);

	smc_copy_sock_settings_to_clc(smc);
	smc->connect_nonblock = 0;
	if (smc->sk.sk_state == SMC_INIT)
		smc->sk.sk_state = SMC_ACTIVE;

	return 0;
}

/* perform steps before actually connecting */
static int __smc_connect(struct smc_sock *smc)
{
	bool ism_supported = false, rdma_supported = false;
	struct smc_clc_msg_accept_confirm aclc;
	struct smc_init_info *ini = NULL;
	int smc_type;
	int rc = 0;

	if (smc->use_fallback)
		return smc_connect_fallback(smc, smc->fallback_rsn);

	/* if peer has not signalled SMC-capability, fall back */
	if (!tcp_sk(smc->clcsock->sk)->syn_smc)
		return smc_connect_fallback(smc, SMC_CLC_DECL_PEERNOSMC);

	/* IPSec connections opt out of SMC-R optimizations */
	if (using_ipsec(smc))
		return smc_connect_decline_fallback(smc, SMC_CLC_DECL_IPSEC);

	ini = kzalloc(sizeof(*ini), GFP_KERNEL);
	if (!ini)
		return smc_connect_decline_fallback(smc, SMC_CLC_DECL_MEM);

	/* get vlan id from IP device */
	if (smc_vlan_by_tcpsk(smc->clcsock, ini)) {
		kfree(ini);
		return smc_connect_decline_fallback(smc,
						    SMC_CLC_DECL_GETVLANERR);
	}

	/* check if there is an ism device available */
	if (!smc_find_ism_device(smc, ini) &&
	    !smc_connect_ism_vlan_setup(smc, ini)) {
		/* ISM is supported for this connection */
		ism_supported = true;
		smc_type = SMC_TYPE_D;
	}

	/* check if there is a rdma device available */
	if (!smc_find_rdma_device(smc, ini)) {
		/* RDMA is supported for this connection */
		rdma_supported = true;
		if (ism_supported)
			smc_type = SMC_TYPE_B; /* both */
		else
			smc_type = SMC_TYPE_R; /* only RDMA */
	}

	/* if neither ISM nor RDMA are supported, fallback */
	if (!rdma_supported && !ism_supported) {
		kfree(ini);
		return smc_connect_decline_fallback(smc, SMC_CLC_DECL_NOSMCDEV);
	}

	/* perform CLC handshake */
	rc = smc_connect_clc(smc, smc_type, &aclc, ini);
	if (rc) {
		smc_connect_ism_vlan_cleanup(smc, ism_supported, ini);
		kfree(ini);
		return smc_connect_decline_fallback(smc, rc);
	}

	/* depending on previous steps, connect using rdma or ism */
	if (rdma_supported && aclc.hdr.typev1 == SMC_TYPE_R)
		rc = smc_connect_rdma(smc, &aclc, ini);
	else if (ism_supported && aclc.hdr.typev1 == SMC_TYPE_D)
		rc = smc_connect_ism(smc, &aclc, ini);
	else
		rc = SMC_CLC_DECL_MODEUNSUPP;
	if (rc) {
		smc_connect_ism_vlan_cleanup(smc, ism_supported, ini);
		kfree(ini);
		return smc_connect_decline_fallback(smc, rc);
	}

	smc_connect_ism_vlan_cleanup(smc, ism_supported, ini);
	kfree(ini);
	return 0;
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
					(TCPF_SYN_SENT | TCP_SYN_RECV)) {
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
	switch (sk->sk_state) {
	default:
		goto out;
	case SMC_ACTIVE:
		rc = -EISCONN;
		goto out;
	case SMC_INIT:
		rc = 0;
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

	sock_hold(&smc->sk); /* sock put in passive closing */
	if (smc->use_fallback)
		goto out;
	if (flags & O_NONBLOCK) {
		if (queue_work(smc_hs_wq, &smc->connect_work))
			smc->connect_nonblock = 1;
		rc = -EINPROGRESS;
	} else {
		rc = __smc_connect(smc);
		if (rc < 0)
			goto out;
		else
			rc = 0; /* success cases including fallback */
	}

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

	if (smcr_link_reg_rmb(link, smc->conn.rmb_desc))
		return SMC_CLC_DECL_ERR_REGRMB;

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
	smc_llc_srv_add_link(link);
	return 0;
}

/* listen worker: finish */
static void smc_listen_out(struct smc_sock *new_smc)
{
	struct smc_sock *lsmc = new_smc->listen_smc;
	struct sock *newsmcsk = &new_smc->sk;

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

	sk_refcnt_debug_inc(newsmcsk);
	if (newsmcsk->sk_state == SMC_INIT)
		newsmcsk->sk_state = SMC_ACTIVE;

	smc_listen_out(new_smc);
}

/* listen worker: finish in error state */
static void smc_listen_out_err(struct smc_sock *new_smc)
{
	struct sock *newsmcsk = &new_smc->sk;

	if (newsmcsk->sk_state == SMC_INIT)
		sock_put(&new_smc->sk); /* passive closing */
	newsmcsk->sk_state = SMC_CLOSED;

	smc_listen_out(new_smc);
}

/* listen worker: decline and fall back if possible */
static void smc_listen_decline(struct smc_sock *new_smc, int reason_code,
			       bool local_first)
{
	/* RDMA setup failed, switch back to TCP */
	if (local_first)
		smc_lgr_cleanup_early(&new_smc->conn);
	else
		smc_conn_free(&new_smc->conn);
	if (reason_code < 0) { /* error, no fallback possible */
		smc_listen_out_err(new_smc);
		return;
	}
	smc_switch_to_fallback(new_smc);
	new_smc->fallback_rsn = reason_code;
	if (reason_code && reason_code != SMC_CLC_DECL_PEERDECL) {
		if (smc_clc_send_decline(new_smc, reason_code) < 0) {
			smc_listen_out_err(new_smc);
			return;
		}
	}
	smc_listen_out_connected(new_smc);
}

/* listen worker: check prefixes */
static int smc_listen_prfx_check(struct smc_sock *new_smc,
				 struct smc_clc_msg_proposal *pclc)
{
	struct smc_clc_msg_proposal_prefix *pclc_prfx;
	struct socket *newclcsock = new_smc->clcsock;

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
		if (ini->first_contact_local)
			smc_lgr_cleanup_early(&new_smc->conn);
		else
			smc_conn_free(&new_smc->conn);
		return (rc == -ENOSPC) ? SMC_CLC_DECL_MAX_DMB :
					 SMC_CLC_DECL_MEM;
	}

	return 0;
}

static void smc_find_ism_device_serv(struct smc_sock *new_smc,
				     struct smc_clc_msg_proposal *pclc,
				     struct smc_init_info *ini)
{
	struct smc_clc_msg_smcd *pclc_smcd = smc_get_clc_msg_smcd(pclc);

	if (!smcd_indicated(pclc->hdr.typev1))
		goto not_found;
	ini->is_smcd = true; /* prepare ISM check */
	ini->ism_peer_gid[0] = pclc_smcd->gid;
	if (smc_find_ism_device(new_smc, ini))
		goto not_found;
	if (!smc_listen_ism_init(new_smc, ini))
		return;		/* ISM device found */

not_found:
	ini->ism_dev[0] = NULL;
	ini->is_smcd = false;
}

/* listen worker: register buffers */
static int smc_listen_rdma_reg(struct smc_sock *new_smc, bool local_first)
{
	struct smc_connection *conn = &new_smc->conn;

	if (!local_first) {
		if (smcr_lgr_reg_rmbs(conn->lnk, conn->rmb_desc))
			return SMC_CLC_DECL_ERR_REGRMB;
	}
	smc_rmb_sync_sg_for_device(&new_smc->conn);

	return 0;
}

static int smc_find_rdma_device_serv(struct smc_sock *new_smc,
				     struct smc_clc_msg_proposal *pclc,
				     struct smc_init_info *ini)
{
	int rc;

	if (!smcr_indicated(pclc->hdr.typev1))
		return SMC_CLC_DECL_NOSMCDEV;

	/* prepare RDMA check */
	ini->ib_lcl = &pclc->lcl;
	rc = smc_find_rdma_device(new_smc, ini);
	if (rc) {
		/* no RDMA device found */
		if (pclc->hdr.typev1 == SMC_TYPE_B)
			/* neither ISM nor RDMA device found */
			rc = SMC_CLC_DECL_NOSMCDEV;
		return rc;
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
	/* check if ISM is available */
	smc_find_ism_device_serv(new_smc, pclc, ini);
	if (ini->is_smcd)
		return 0;
	if (pclc->hdr.typev1 == SMC_TYPE_D)
		return SMC_CLC_DECL_NOSMCDDEV; /* skip RDMA and decline */

	/* check if RDMA is available */
	return smc_find_rdma_device_serv(new_smc, pclc, ini);
}

/* listen worker: finish RDMA setup */
static int smc_listen_rdma_finish(struct smc_sock *new_smc,
				  struct smc_clc_msg_accept_confirm *cclc,
				  bool local_first)
{
	struct smc_link *link = new_smc->conn.lnk;
	int reason_code = 0;

	if (local_first)
		smc_link_save_peer_info(link, cclc);

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
	struct smc_clc_msg_accept_confirm cclc;
	struct smc_clc_msg_proposal_area *buf;
	struct smc_clc_msg_proposal *pclc;
	struct smc_init_info *ini = NULL;
	int rc = 0;

	if (new_smc->listen_smc->sk.sk_state != SMC_LISTEN)
		return smc_listen_out_err(new_smc);

	if (new_smc->use_fallback) {
		smc_listen_out_connected(new_smc);
		return;
	}

	/* check if peer is smc capable */
	if (!tcp_sk(newclcsock->sk)->syn_smc) {
		smc_switch_to_fallback(new_smc);
		new_smc->fallback_rsn = SMC_CLC_DECL_PEERNOSMC;
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

	/* IPSec connections opt out of SMC-R optimizations */
	if (using_ipsec(new_smc)) {
		rc = SMC_CLC_DECL_IPSEC;
		goto out_decl;
	}

	/* check for matching IP prefix and subnet length */
	rc = smc_listen_prfx_check(new_smc, pclc);
	if (rc)
		goto out_decl;

	ini = kzalloc(sizeof(*ini), GFP_KERNEL);
	if (!ini) {
		rc = SMC_CLC_DECL_MEM;
		goto out_decl;
	}

	/* get vlan id from IP device */
	if (smc_vlan_by_tcpsk(new_smc->clcsock, ini)) {
		rc = SMC_CLC_DECL_GETVLANERR;
		goto out_decl;
	}

	mutex_lock(&smc_server_lgr_pending);
	smc_close_init(new_smc);
	smc_rx_init(new_smc);
	smc_tx_init(new_smc);

	/* determine ISM or RoCE device used for connection */
	rc = smc_listen_find_device(new_smc, pclc, ini);
	if (rc)
		goto out_unlock;

	/* send SMC Accept CLC message */
	rc = smc_clc_send_accept(new_smc, ini->first_contact_local);
	if (rc)
		goto out_unlock;

	/* SMC-D does not need this lock any more */
	if (ini->is_smcd)
		mutex_unlock(&smc_server_lgr_pending);

	/* receive SMC Confirm CLC message */
	rc = smc_clc_wait_msg(new_smc, &cclc, sizeof(cclc),
			      SMC_CLC_CONFIRM, CLC_WAIT_TIME);
	if (rc) {
		if (!ini->is_smcd)
			goto out_unlock;
		goto out_decl;
	}

	/* finish worker */
	if (!ini->is_smcd) {
		rc = smc_listen_rdma_finish(new_smc, &cclc,
					    ini->first_contact_local);
		if (rc)
			goto out_unlock;
		mutex_unlock(&smc_server_lgr_pending);
	}
	smc_conn_save_peer_info(new_smc, &cclc);
	smc_listen_out_connected(new_smc);
	goto out_free;

out_unlock:
	mutex_unlock(&smc_server_lgr_pending);
out_decl:
	smc_listen_decline(new_smc, rc, ini ? ini->first_contact_local : 0);
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

	lsmc = (struct smc_sock *)
	       ((uintptr_t)listen_clcsock->sk_user_data & ~SK_USER_DATA_NOCOPY);
	if (!lsmc)
		return;
	lsmc->clcsk_data_ready(listen_clcsock);
	if (lsmc->sk.sk_state == SMC_LISTEN) {
		sock_hold(&lsmc->sk); /* sock_put in smc_tcp_listen_work() */
		if (!queue_work(smc_hs_wq, &lsmc->tcp_listen_work))
			sock_put(&lsmc->sk);
	}
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
	    smc->connect_nonblock)
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
	smc->clcsk_data_ready = smc->clcsock->sk->sk_data_ready;
	smc->clcsock->sk->sk_data_ready = smc_clcsock_data_ready;
	smc->clcsock->sk->sk_user_data =
		(void *)((uintptr_t)smc | SK_USER_DATA_NOCOPY);
	rc = kernel_listen(smc->clcsock, backlog);
	if (rc)
		goto out;
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
			smc_switch_to_fallback(smc);
			smc->fallback_rsn = SMC_CLC_DECL_OPTUNSUPP;
		} else {
			rc = -EINVAL;
			goto out;
		}
	}

	if (smc->use_fallback)
		rc = smc->clcsock->ops->sendmsg(smc->clcsock, msg, len);
	else
		rc = smc_tx_sendmsg(smc, msg, len);
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
	struct smc_sock *smc;
	int rc = -EINVAL;
	int rc1 = 0;

	smc = smc_sk(sk);

	if ((how < SHUT_RD) || (how > SHUT_RDWR))
		return rc;

	lock_sock(sk);

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
		if (sk->sk_shutdown == SHUTDOWN_MASK)
			sk->sk_state = SMC_CLOSED;
		goto out;
	}
	switch (how) {
	case SHUT_RDWR:		/* shutdown in both directions */
		rc = smc_close_active(smc);
		break;
	case SHUT_WR:
		rc = smc_close_shutdown_write(smc);
		break;
	case SHUT_RD:
		rc = 0;
		/* nothing more to do because peer is not involved */
		break;
	}
	if (smc->clcsock)
		rc1 = kernel_sock_shutdown(smc->clcsock, how);
	/* map sock_shutdown_cmd constants to sk_shutdown value range */
	sk->sk_shutdown |= how + 1;

out:
	release_sock(sk);
	return rc ? rc : rc1;
}

static int smc_setsockopt(struct socket *sock, int level, int optname,
			  sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int val, rc;

	smc = smc_sk(sk);

	/* generic setsockopts reaching us here always apply to the
	 * CLC socket
	 */
	if (unlikely(!smc->clcsock->ops->setsockopt))
		rc = -EOPNOTSUPP;
	else
		rc = smc->clcsock->ops->setsockopt(smc->clcsock, level, optname,
						   optval, optlen);
	if (smc->clcsock->sk->sk_err) {
		sk->sk_err = smc->clcsock->sk->sk_err;
		sk->sk_error_report(sk);
	}

	if (optlen < sizeof(int))
		return -EINVAL;
	if (copy_from_sockptr(&val, optval, sizeof(int)))
		return -EFAULT;

	lock_sock(sk);
	if (rc || smc->use_fallback)
		goto out;
	switch (optname) {
	case TCP_ULP:
	case TCP_FASTOPEN:
	case TCP_FASTOPEN_CONNECT:
	case TCP_FASTOPEN_KEY:
	case TCP_FASTOPEN_NO_COOKIE:
		/* option not supported by SMC */
		if (sk->sk_state == SMC_INIT && !smc->connect_nonblock) {
			smc_switch_to_fallback(smc);
			smc->fallback_rsn = SMC_CLC_DECL_OPTUNSUPP;
		} else {
			rc = -EINVAL;
		}
		break;
	case TCP_NODELAY:
		if (sk->sk_state != SMC_INIT &&
		    sk->sk_state != SMC_LISTEN &&
		    sk->sk_state != SMC_CLOSED) {
			if (val)
				mod_delayed_work(smc->conn.lgr->tx_wq,
						 &smc->conn.tx_work, 0);
		}
		break;
	case TCP_CORK:
		if (sk->sk_state != SMC_INIT &&
		    sk->sk_state != SMC_LISTEN &&
		    sk->sk_state != SMC_CLOSED) {
			if (!val)
				mod_delayed_work(smc->conn.lgr->tx_wq,
						 &smc->conn.tx_work, 0);
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

	smc = smc_sk(sock->sk);
	/* socket options apply to the CLC socket */
	if (unlikely(!smc->clcsock->ops->getsockopt))
		return -EOPNOTSUPP;
	return smc->clcsock->ops->getsockopt(smc->clcsock, level, optname,
					     optval, optlen);
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
	if (smc->use_fallback)
		rc = kernel_sendpage(smc->clcsock, page, offset,
				     size, flags);
	else
		rc = sock_no_sendpage(sock, page, offset, size, flags);

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

static int smc_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
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
	sk = smc_sock_alloc(net, sock, protocol);
	if (!sk)
		goto out;

	/* create internal TCP socket for CLC handshake and fallback */
	smc = smc_sk(sk);
	smc->use_fallback = false; /* assume rdma capability first */
	smc->fallback_rsn = 0;
	rc = sock_create_kern(net, family, SOCK_STREAM, IPPROTO_TCP,
			      &smc->clcsock);
	if (rc) {
		sk_common_release(sk);
		goto out;
	}
	smc->sk.sk_sndbuf = max(smc->clcsock->sk->sk_sndbuf, SMC_BUF_MIN_SIZE);
	smc->sk.sk_rcvbuf = max(smc->clcsock->sk->sk_rcvbuf, SMC_BUF_MIN_SIZE);

out:
	return rc;
}

static const struct net_proto_family smc_sock_family_ops = {
	.family	= PF_SMC,
	.owner	= THIS_MODULE,
	.create	= smc_create,
};

unsigned int smc_net_id;

static __net_init int smc_net_init(struct net *net)
{
	return smc_pnet_net_init(net);
}

static void __net_exit smc_net_exit(struct net *net)
{
	smc_pnet_net_exit(net);
}

static struct pernet_operations smc_net_ops = {
	.init = smc_net_init,
	.exit = smc_net_exit,
	.id   = &smc_net_id,
	.size = sizeof(struct smc_net),
};

static int __init smc_init(void)
{
	int rc;

	rc = register_pernet_subsys(&smc_net_ops);
	if (rc)
		return rc;

	rc = smc_pnet_init();
	if (rc)
		goto out_pernet_subsys;

	rc = -ENOMEM;
	smc_hs_wq = alloc_workqueue("smc_hs_wq", 0, 0);
	if (!smc_hs_wq)
		goto out_pnet;

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

	static_branch_enable(&tcp_have_smc);
	return 0;

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
out_pnet:
	smc_pnet_exit();
out_pernet_subsys:
	unregister_pernet_subsys(&smc_net_ops);

	return rc;
}

static void __exit smc_exit(void)
{
	static_branch_disable(&tcp_have_smc);
	sock_unregister(PF_SMC);
	smc_core_exit();
	smc_ib_unregister_client();
	destroy_workqueue(smc_close_wq);
	destroy_workqueue(smc_hs_wq);
	proto_unregister(&smc_proto6);
	proto_unregister(&smc_proto);
	smc_pnet_exit();
	unregister_pernet_subsys(&smc_net_ops);
	rcu_barrier();
}

module_init(smc_init);
module_exit(smc_exit);

MODULE_AUTHOR("Ursula Braun <ubraun@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("smc socket address family");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_SMC);
