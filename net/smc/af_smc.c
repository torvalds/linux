/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  AF_SMC protocol family socket handler keeping the AF_INET sock address type
 *  applies to SOCK_STREAM sockets only
 *  offers an alternative communication option for TCP-protocol sockets
 *  applicable with RoCE-cards only
 *
 *  Initial restrictions:
 *    - non-blocking connect postponed
 *    - IPv6 support postponed
 *    - support for alternate links postponed
 *    - partial support for non-blocking sockets only
 *    - support for urgent data postponed
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 *              based on prototype from Frank Blaschka
 */

#define KMSG_COMPONENT "smc"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/socket.h>
#include <linux/inetdevice.h>
#include <linux/workqueue.h>
#include <linux/in.h>
#include <linux/sched/signal.h>

#include <net/sock.h>
#include <net/tcp.h>
#include <net/smc.h>

#include "smc.h"
#include "smc_clc.h"
#include "smc_llc.h"
#include "smc_cdc.h"
#include "smc_core.h"
#include "smc_ib.h"
#include "smc_pnet.h"
#include "smc_tx.h"
#include "smc_rx.h"
#include "smc_close.h"

static DEFINE_MUTEX(smc_create_lgr_pending);	/* serialize link group
						 * creation
						 */

struct smc_lgr_list smc_lgr_list = {		/* established link groups */
	.lock = __SPIN_LOCK_UNLOCKED(smc_lgr_list.lock),
	.list = LIST_HEAD_INIT(smc_lgr_list.list),
};

static void smc_tcp_listen_work(struct work_struct *);

static void smc_set_keepalive(struct sock *sk, int val)
{
	struct smc_sock *smc = smc_sk(sk);

	smc->clcsock->sk->sk_prot->keepalive(smc->clcsock->sk, val);
}

static struct smc_hashinfo smc_v4_hashinfo = {
	.lock = __RW_LOCK_UNLOCKED(smc_v4_hashinfo.lock),
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

static int smc_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = 0;

	if (!sk)
		goto out;

	smc = smc_sk(sk);
	sock_hold(sk);
	if (sk->sk_state == SMC_LISTEN)
		/* smc_close_non_accepted() is called and acquires
		 * sock lock for child sockets again
		 */
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);
	else
		lock_sock(sk);

	if (smc->use_fallback) {
		sk->sk_state = SMC_CLOSED;
		sk->sk_state_change(sk);
	} else {
		rc = smc_close_active(smc);
		sock_set_flag(sk, SOCK_DEAD);
		sk->sk_shutdown |= SHUTDOWN_MASK;
	}
	if (smc->clcsock) {
		sock_release(smc->clcsock);
		smc->clcsock = NULL;
	}

	/* detach socket */
	sock_orphan(sk);
	sock->sk = NULL;
	if (smc->use_fallback) {
		schedule_delayed_work(&smc->sock_put_work, TCP_TIMEWAIT_LEN);
	} else if (sk->sk_state == SMC_CLOSED) {
		smc_conn_free(&smc->conn);
		schedule_delayed_work(&smc->sock_put_work,
				      SMC_CLOSE_SOCK_PUT_DELAY);
	}
	release_sock(sk);

	sock_put(sk);
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

static struct sock *smc_sock_alloc(struct net *net, struct socket *sock)
{
	struct smc_sock *smc;
	struct sock *sk;

	sk = sk_alloc(net, PF_SMC, GFP_KERNEL, &smc_proto, 0);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk); /* sets sk_refcnt to 1 */
	sk->sk_state = SMC_INIT;
	sk->sk_destruct = smc_destruct;
	sk->sk_protocol = SMCPROTO_SMC;
	smc = smc_sk(sk);
	INIT_WORK(&smc->tcp_listen_work, smc_tcp_listen_work);
	INIT_LIST_HEAD(&smc->accept_q);
	spin_lock_init(&smc->accept_q_lock);
	INIT_DELAYED_WORK(&smc->sock_put_work, smc_close_sock_put_work);
	sk->sk_prot->hash(sk);
	sk_refcnt_debug_inc(sk);

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
	/* accept AF_UNSPEC (mapped to AF_INET) only if s_addr is INADDR_ANY */
	if ((addr->sin_family != AF_INET) &&
	    ((addr->sin_family != AF_UNSPEC) ||
	     (addr->sin_addr.s_addr != htonl(INADDR_ANY))))
		goto out;

	lock_sock(sk);

	/* Check if socket is already active */
	rc = -EINVAL;
	if (sk->sk_state != SMC_INIT)
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
			     (1UL << SOCK_FILTER_LOCKED))
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

/* determine subnet and mask of internal TCP socket */
int smc_netinfo_by_tcpsk(struct socket *clcsock,
			 __be32 *subnet, u8 *prefix_len)
{
	struct dst_entry *dst = sk_dst_get(clcsock->sk);
	struct in_device *in_dev;
	struct sockaddr_in addr;
	int rc = -ENOENT;
	int len;

	if (!dst) {
		rc = -ENOTCONN;
		goto out;
	}
	if (!dst->dev) {
		rc = -ENODEV;
		goto out_rel;
	}

	/* get address to which the internal TCP socket is bound */
	kernel_getsockname(clcsock, (struct sockaddr *)&addr, &len);
	/* analyze IPv4 specific data of net_device belonging to TCP socket */
	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dst->dev);
	for_ifa(in_dev) {
		if (!inet_ifa_match(addr.sin_addr.s_addr, ifa))
			continue;
		*prefix_len = inet_mask_len(ifa->ifa_mask);
		*subnet = ifa->ifa_address & ifa->ifa_mask;
		rc = 0;
		break;
	} endfor_ifa(in_dev);
	rcu_read_unlock();

out_rel:
	dst_release(dst);
out:
	return rc;
}

static int smc_clnt_conf_first_link(struct smc_sock *smc, union ib_gid *gid)
{
	struct smc_link_group *lgr = smc->conn.lgr;
	struct smc_link *link;
	int rest;
	int rc;

	link = &lgr->lnk[SMC_SINGLE_LINK];
	/* receive CONFIRM LINK request from server over RoCE fabric */
	rest = wait_for_completion_interruptible_timeout(
		&link->llc_confirm,
		SMC_LLC_WAIT_FIRST_TIME);
	if (rest <= 0) {
		struct smc_clc_msg_decline dclc;

		rc = smc_clc_wait_msg(smc, &dclc, sizeof(dclc),
				      SMC_CLC_DECLINE);
		return rc;
	}

	rc = smc_ib_modify_qp_rts(link);
	if (rc)
		return SMC_CLC_DECL_INTERR;

	smc_wr_remember_qp_attr(link);

	rc = smc_wr_reg_send(link,
			     smc->conn.rmb_desc->mr_rx[SMC_SINGLE_LINK]);
	if (rc)
		return SMC_CLC_DECL_INTERR;

	/* send CONFIRM LINK response over RoCE fabric */
	rc = smc_llc_send_confirm_link(link,
				       link->smcibdev->mac[link->ibport - 1],
				       gid, SMC_LLC_RESP);
	if (rc < 0)
		return SMC_CLC_DECL_TCL;

	return rc;
}

static void smc_conn_save_peer_info(struct smc_sock *smc,
				    struct smc_clc_msg_accept_confirm *clc)
{
	smc->conn.peer_conn_idx = clc->conn_idx;
	smc->conn.local_tx_ctrl.token = ntohl(clc->rmbe_alert_token);
	smc->conn.peer_rmbe_size = smc_uncompress_bufsize(clc->rmbe_size);
	atomic_set(&smc->conn.peer_rmbe_space, smc->conn.peer_rmbe_size);
}

static void smc_link_save_peer_info(struct smc_link *link,
				    struct smc_clc_msg_accept_confirm *clc)
{
	link->peer_qpn = ntoh24(clc->qpn);
	memcpy(link->peer_gid, clc->lcl.gid, SMC_GID_SIZE);
	memcpy(link->peer_mac, clc->lcl.mac, sizeof(link->peer_mac));
	link->peer_psn = ntoh24(clc->psn);
	link->peer_mtu = clc->qp_mtu;
}

/* setup for RDMA connection of client */
static int smc_connect_rdma(struct smc_sock *smc)
{
	struct sockaddr_in *inaddr = (struct sockaddr_in *)smc->addr;
	struct smc_clc_msg_accept_confirm aclc;
	int local_contact = SMC_FIRST_CONTACT;
	struct smc_ib_device *smcibdev;
	struct smc_link *link;
	u8 srv_first_contact;
	int reason_code = 0;
	int rc = 0;
	u8 ibport;

	if (!tcp_sk(smc->clcsock->sk)->syn_smc) {
		/* peer has not signalled SMC-capability */
		smc->use_fallback = true;
		goto out_connected;
	}

	/* IPSec connections opt out of SMC-R optimizations */
	if (using_ipsec(smc)) {
		reason_code = SMC_CLC_DECL_IPSEC;
		goto decline_rdma;
	}

	/* PNET table look up: search active ib_device and port
	 * within same PNETID that also contains the ethernet device
	 * used for the internal TCP socket
	 */
	smc_pnet_find_roce_resource(smc->clcsock->sk, &smcibdev, &ibport);
	if (!smcibdev) {
		reason_code = SMC_CLC_DECL_CNFERR; /* configuration error */
		goto decline_rdma;
	}

	/* do inband token exchange */
	reason_code = smc_clc_send_proposal(smc, smcibdev, ibport);
	if (reason_code < 0) {
		rc = reason_code;
		goto out_err;
	}
	if (reason_code > 0) /* configuration error */
		goto decline_rdma;
	/* receive SMC Accept CLC message */
	reason_code = smc_clc_wait_msg(smc, &aclc, sizeof(aclc),
				       SMC_CLC_ACCEPT);
	if (reason_code < 0) {
		rc = reason_code;
		goto out_err;
	}
	if (reason_code > 0)
		goto decline_rdma;

	srv_first_contact = aclc.hdr.flag;
	mutex_lock(&smc_create_lgr_pending);
	local_contact = smc_conn_create(smc, inaddr->sin_addr.s_addr, smcibdev,
					ibport, &aclc.lcl, srv_first_contact);
	if (local_contact < 0) {
		rc = local_contact;
		if (rc == -ENOMEM)
			reason_code = SMC_CLC_DECL_MEM;/* insufficient memory*/
		else if (rc == -ENOLINK)
			reason_code = SMC_CLC_DECL_SYNCERR; /* synchr. error */
		goto decline_rdma_unlock;
	}
	link = &smc->conn.lgr->lnk[SMC_SINGLE_LINK];

	smc_conn_save_peer_info(smc, &aclc);

	/* create send buffer and rmb */
	rc = smc_buf_create(smc);
	if (rc) {
		reason_code = SMC_CLC_DECL_MEM;
		goto decline_rdma_unlock;
	}

	if (local_contact == SMC_FIRST_CONTACT)
		smc_link_save_peer_info(link, &aclc);

	rc = smc_rmb_rtoken_handling(&smc->conn, &aclc);
	if (rc) {
		reason_code = SMC_CLC_DECL_INTERR;
		goto decline_rdma_unlock;
	}

	smc_close_init(smc);
	smc_rx_init(smc);

	if (local_contact == SMC_FIRST_CONTACT) {
		rc = smc_ib_ready_link(link);
		if (rc) {
			reason_code = SMC_CLC_DECL_INTERR;
			goto decline_rdma_unlock;
		}
	} else {
		struct smc_buf_desc *buf_desc = smc->conn.rmb_desc;

		if (!buf_desc->reused) {
			/* register memory region for new rmb */
			rc = smc_wr_reg_send(link,
					     buf_desc->mr_rx[SMC_SINGLE_LINK]);
			if (rc) {
				reason_code = SMC_CLC_DECL_INTERR;
				goto decline_rdma_unlock;
			}
		}
	}
	smc_rmb_sync_sg_for_device(&smc->conn);

	rc = smc_clc_send_confirm(smc);
	if (rc)
		goto out_err_unlock;

	if (local_contact == SMC_FIRST_CONTACT) {
		/* QP confirmation over RoCE fabric */
		reason_code = smc_clnt_conf_first_link(
			smc, &smcibdev->gid[ibport - 1]);
		if (reason_code < 0) {
			rc = reason_code;
			goto out_err_unlock;
		}
		if (reason_code > 0)
			goto decline_rdma_unlock;
	}

	mutex_unlock(&smc_create_lgr_pending);
	smc_tx_init(smc);

out_connected:
	smc_copy_sock_settings_to_clc(smc);
	if (smc->sk.sk_state == SMC_INIT)
		smc->sk.sk_state = SMC_ACTIVE;

	return rc ? rc : local_contact;

decline_rdma_unlock:
	mutex_unlock(&smc_create_lgr_pending);
	smc_conn_free(&smc->conn);
decline_rdma:
	/* RDMA setup failed, switch back to TCP */
	smc->use_fallback = true;
	if (reason_code && (reason_code != SMC_CLC_DECL_REPLY)) {
		rc = smc_clc_send_decline(smc, reason_code);
		if (rc < sizeof(struct smc_clc_msg_decline))
			goto out_err;
	}
	goto out_connected;

out_err_unlock:
	mutex_unlock(&smc_create_lgr_pending);
	smc_conn_free(&smc->conn);
out_err:
	return rc;
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
	if (addr->sa_family != AF_INET)
		goto out_err;
	smc->addr = addr;	/* needed for nonblocking connect */

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
	rc = kernel_connect(smc->clcsock, addr, alen, flags);
	if (rc)
		goto out;

	/* setup RDMA connection */
	rc = smc_connect_rdma(smc);
	if (rc < 0)
		goto out;
	else
		rc = 0; /* success cases including fallback */

out:
	release_sock(sk);
out_err:
	return rc;
}

static int smc_clcsock_accept(struct smc_sock *lsmc, struct smc_sock **new_smc)
{
	struct sock *sk = &lsmc->sk;
	struct socket *new_clcsock;
	struct sock *new_sk;
	int rc;

	release_sock(&lsmc->sk);
	new_sk = smc_sock_alloc(sock_net(sk), NULL);
	if (!new_sk) {
		rc = -ENOMEM;
		lsmc->sk.sk_err = ENOMEM;
		*new_smc = NULL;
		lock_sock(&lsmc->sk);
		goto out;
	}
	*new_smc = smc_sk(new_sk);

	rc = kernel_accept(lsmc->clcsock, &new_clcsock, 0);
	lock_sock(&lsmc->sk);
	if  (rc < 0) {
		lsmc->sk.sk_err = -rc;
		new_sk->sk_state = SMC_CLOSED;
		sock_set_flag(new_sk, SOCK_DEAD);
		sk->sk_prot->unhash(new_sk);
		sock_put(new_sk);
		*new_smc = NULL;
		goto out;
	}
	if (lsmc->sk.sk_state == SMC_CLOSED) {
		if (new_clcsock)
			sock_release(new_clcsock);
		new_sk->sk_state = SMC_CLOSED;
		sock_set_flag(new_sk, SOCK_DEAD);
		sk->sk_prot->unhash(new_sk);
		sock_put(new_sk);
		*new_smc = NULL;
		goto out;
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

	sock_hold(sk);
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
	sock_put(sk);
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
			sock_put(new_sk);
			continue;
		}
		if (new_sock)
			sock_graft(new_sk, new_sock);
		return new_sk;
	}
	return NULL;
}

/* clean up for a created but never accepted sock */
void smc_close_non_accepted(struct sock *sk)
{
	struct smc_sock *smc = smc_sk(sk);

	sock_hold(sk);
	lock_sock(sk);
	if (!sk->sk_lingertime)
		/* wait for peer closing */
		sk->sk_lingertime = SMC_MAX_STREAM_WAIT_TIMEOUT;
	if (smc->use_fallback) {
		sk->sk_state = SMC_CLOSED;
	} else {
		smc_close_active(smc);
		sock_set_flag(sk, SOCK_DEAD);
		sk->sk_shutdown |= SHUTDOWN_MASK;
	}
	if (smc->clcsock) {
		struct socket *tcp;

		tcp = smc->clcsock;
		smc->clcsock = NULL;
		sock_release(tcp);
	}
	if (smc->use_fallback) {
		schedule_delayed_work(&smc->sock_put_work, TCP_TIMEWAIT_LEN);
	} else if (sk->sk_state == SMC_CLOSED) {
		smc_conn_free(&smc->conn);
		schedule_delayed_work(&smc->sock_put_work,
				      SMC_CLOSE_SOCK_PUT_DELAY);
	}
	release_sock(sk);
	sock_put(sk);
}

static int smc_serv_conf_first_link(struct smc_sock *smc)
{
	struct smc_link_group *lgr = smc->conn.lgr;
	struct smc_link *link;
	int rest;
	int rc;

	link = &lgr->lnk[SMC_SINGLE_LINK];

	rc = smc_wr_reg_send(link,
			     smc->conn.rmb_desc->mr_rx[SMC_SINGLE_LINK]);
	if (rc)
		return SMC_CLC_DECL_INTERR;

	/* send CONFIRM LINK request to client over the RoCE fabric */
	rc = smc_llc_send_confirm_link(link,
				       link->smcibdev->mac[link->ibport - 1],
				       &link->smcibdev->gid[link->ibport - 1],
				       SMC_LLC_REQ);
	if (rc < 0)
		return SMC_CLC_DECL_TCL;

	/* receive CONFIRM LINK response from client over the RoCE fabric */
	rest = wait_for_completion_interruptible_timeout(
		&link->llc_confirm_resp,
		SMC_LLC_WAIT_FIRST_TIME);
	if (rest <= 0) {
		struct smc_clc_msg_decline dclc;

		rc = smc_clc_wait_msg(smc, &dclc, sizeof(dclc),
				      SMC_CLC_DECLINE);
	}

	return rc;
}

/* setup for RDMA connection of server */
static void smc_listen_work(struct work_struct *work)
{
	struct smc_sock *new_smc = container_of(work, struct smc_sock,
						smc_listen_work);
	struct socket *newclcsock = new_smc->clcsock;
	struct smc_sock *lsmc = new_smc->listen_smc;
	struct smc_clc_msg_accept_confirm cclc;
	int local_contact = SMC_REUSE_CONTACT;
	struct sock *newsmcsk = &new_smc->sk;
	struct smc_clc_msg_proposal pclc;
	struct smc_ib_device *smcibdev;
	struct sockaddr_in peeraddr;
	struct smc_link *link;
	int reason_code = 0;
	int rc = 0, len;
	__be32 subnet;
	u8 prefix_len;
	u8 ibport;

	/* check if peer is smc capable */
	if (!tcp_sk(newclcsock->sk)->syn_smc) {
		new_smc->use_fallback = true;
		goto out_connected;
	}

	/* do inband token exchange -
	 *wait for and receive SMC Proposal CLC message
	 */
	reason_code = smc_clc_wait_msg(new_smc, &pclc, sizeof(pclc),
				       SMC_CLC_PROPOSAL);
	if (reason_code < 0)
		goto out_err;
	if (reason_code > 0)
		goto decline_rdma;

	/* IPSec connections opt out of SMC-R optimizations */
	if (using_ipsec(new_smc)) {
		reason_code = SMC_CLC_DECL_IPSEC;
		goto decline_rdma;
	}

	/* PNET table look up: search active ib_device and port
	 * within same PNETID that also contains the ethernet device
	 * used for the internal TCP socket
	 */
	smc_pnet_find_roce_resource(newclcsock->sk, &smcibdev, &ibport);
	if (!smcibdev) {
		reason_code = SMC_CLC_DECL_CNFERR; /* configuration error */
		goto decline_rdma;
	}

	/* determine subnet and mask from internal TCP socket */
	rc = smc_netinfo_by_tcpsk(newclcsock, &subnet, &prefix_len);
	if (rc) {
		reason_code = SMC_CLC_DECL_CNFERR; /* configuration error */
		goto decline_rdma;
	}
	if ((pclc.outgoing_subnet != subnet) ||
	    (pclc.prefix_len != prefix_len)) {
		reason_code = SMC_CLC_DECL_CNFERR; /* configuration error */
		goto decline_rdma;
	}

	/* get address of the peer connected to the internal TCP socket */
	kernel_getpeername(newclcsock, (struct sockaddr *)&peeraddr, &len);

	/* allocate connection / link group */
	mutex_lock(&smc_create_lgr_pending);
	local_contact = smc_conn_create(new_smc, peeraddr.sin_addr.s_addr,
					smcibdev, ibport, &pclc.lcl, 0);
	if (local_contact < 0) {
		rc = local_contact;
		if (rc == -ENOMEM)
			reason_code = SMC_CLC_DECL_MEM;/* insufficient memory*/
		goto decline_rdma_unlock;
	}
	link = &new_smc->conn.lgr->lnk[SMC_SINGLE_LINK];

	/* create send buffer and rmb */
	rc = smc_buf_create(new_smc);
	if (rc) {
		reason_code = SMC_CLC_DECL_MEM;
		goto decline_rdma_unlock;
	}

	smc_close_init(new_smc);
	smc_rx_init(new_smc);

	if (local_contact != SMC_FIRST_CONTACT) {
		struct smc_buf_desc *buf_desc = new_smc->conn.rmb_desc;

		if (!buf_desc->reused) {
			/* register memory region for new rmb */
			rc = smc_wr_reg_send(link,
					     buf_desc->mr_rx[SMC_SINGLE_LINK]);
			if (rc) {
				reason_code = SMC_CLC_DECL_INTERR;
				goto decline_rdma_unlock;
			}
		}
	}
	smc_rmb_sync_sg_for_device(&new_smc->conn);

	rc = smc_clc_send_accept(new_smc, local_contact);
	if (rc)
		goto out_err_unlock;

	/* receive SMC Confirm CLC message */
	reason_code = smc_clc_wait_msg(new_smc, &cclc, sizeof(cclc),
				       SMC_CLC_CONFIRM);
	if (reason_code < 0)
		goto out_err_unlock;
	if (reason_code > 0)
		goto decline_rdma_unlock;
	smc_conn_save_peer_info(new_smc, &cclc);
	if (local_contact == SMC_FIRST_CONTACT)
		smc_link_save_peer_info(link, &cclc);

	rc = smc_rmb_rtoken_handling(&new_smc->conn, &cclc);
	if (rc) {
		reason_code = SMC_CLC_DECL_INTERR;
		goto decline_rdma_unlock;
	}

	if (local_contact == SMC_FIRST_CONTACT) {
		rc = smc_ib_ready_link(link);
		if (rc) {
			reason_code = SMC_CLC_DECL_INTERR;
			goto decline_rdma_unlock;
		}
		/* QP confirmation over RoCE fabric */
		reason_code = smc_serv_conf_first_link(new_smc);
		if (reason_code < 0) {
			/* peer is not aware of a problem */
			rc = reason_code;
			goto out_err_unlock;
		}
		if (reason_code > 0)
			goto decline_rdma_unlock;
	}

	smc_tx_init(new_smc);
	mutex_unlock(&smc_create_lgr_pending);

out_connected:
	sk_refcnt_debug_inc(newsmcsk);
	if (newsmcsk->sk_state == SMC_INIT)
		newsmcsk->sk_state = SMC_ACTIVE;
enqueue:
	lock_sock_nested(&lsmc->sk, SINGLE_DEPTH_NESTING);
	if (lsmc->sk.sk_state == SMC_LISTEN) {
		smc_accept_enqueue(&lsmc->sk, newsmcsk);
	} else { /* no longer listening */
		smc_close_non_accepted(newsmcsk);
	}
	release_sock(&lsmc->sk);

	/* Wake up accept */
	lsmc->sk.sk_data_ready(&lsmc->sk);
	sock_put(&lsmc->sk); /* sock_hold in smc_tcp_listen_work */
	return;

decline_rdma_unlock:
	mutex_unlock(&smc_create_lgr_pending);
decline_rdma:
	/* RDMA setup failed, switch back to TCP */
	smc_conn_free(&new_smc->conn);
	new_smc->use_fallback = true;
	if (reason_code && (reason_code != SMC_CLC_DECL_REPLY)) {
		rc = smc_clc_send_decline(new_smc, reason_code);
		if (rc < sizeof(struct smc_clc_msg_decline))
			goto out_err;
	}
	goto out_connected;

out_err_unlock:
	mutex_unlock(&smc_create_lgr_pending);
out_err:
	newsmcsk->sk_state = SMC_CLOSED;
	smc_conn_free(&new_smc->conn);
	goto enqueue; /* queue new sock with sk_err set */
}

static void smc_tcp_listen_work(struct work_struct *work)
{
	struct smc_sock *lsmc = container_of(work, struct smc_sock,
					     tcp_listen_work);
	struct smc_sock *new_smc;
	int rc = 0;

	lock_sock(&lsmc->sk);
	while (lsmc->sk.sk_state == SMC_LISTEN) {
		rc = smc_clcsock_accept(lsmc, &new_smc);
		if (rc)
			goto out;
		if (!new_smc)
			continue;

		new_smc->listen_smc = lsmc;
		new_smc->use_fallback = false; /* assume rdma capability first*/
		sock_hold(&lsmc->sk); /* sock_put in smc_listen_work */
		INIT_WORK(&new_smc->smc_listen_work, smc_listen_work);
		smc_copy_sock_settings_to_smc(new_smc);
		schedule_work(&new_smc->smc_listen_work);
	}

out:
	release_sock(&lsmc->sk);
	lsmc->sk.sk_data_ready(&lsmc->sk); /* no more listening, wake accept */
}

static int smc_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc;

	smc = smc_sk(sk);
	lock_sock(sk);

	rc = -EINVAL;
	if ((sk->sk_state != SMC_INIT) && (sk->sk_state != SMC_LISTEN))
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
	tcp_sk(smc->clcsock->sk)->syn_smc = 1;

	rc = kernel_listen(smc->clcsock, backlog);
	if (rc)
		goto out;
	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;
	sk->sk_state = SMC_LISTEN;
	INIT_WORK(&smc->tcp_listen_work, smc_tcp_listen_work);
	schedule_work(&smc->tcp_listen_work);

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
	lock_sock(sk);

	if (lsmc->sk.sk_state != SMC_LISTEN) {
		rc = -EINVAL;
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

out:
	release_sock(sk);
	return rc;
}

static int smc_getname(struct socket *sock, struct sockaddr *addr,
		       int *len, int peer)
{
	struct smc_sock *smc;

	if (peer && (sock->sk->sk_state != SMC_ACTIVE) &&
	    (sock->sk->sk_state != SMC_APPCLOSEWAIT1))
		return -ENOTCONN;

	smc = smc_sk(sock->sk);

	return smc->clcsock->ops->getname(smc->clcsock, addr, len, peer);
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
	if ((sk->sk_state == SMC_INIT) ||
	    (sk->sk_state == SMC_LISTEN) ||
	    (sk->sk_state == SMC_CLOSED))
		goto out;

	if (sk->sk_state == SMC_PEERFINCLOSEWAIT) {
		rc = 0;
		goto out;
	}

	if (smc->use_fallback)
		rc = smc->clcsock->ops->recvmsg(smc->clcsock, msg, len, flags);
	else
		rc = smc_rx_recvmsg(smc, msg, len, flags);

out:
	release_sock(sk);
	return rc;
}

static unsigned int smc_accept_poll(struct sock *parent)
{
	struct smc_sock *isk;
	struct sock *sk;

	lock_sock(parent);
	list_for_each_entry(isk, &smc_sk(parent)->accept_q, accept_q) {
		sk = (struct sock *)isk;

		if (sk->sk_state == SMC_ACTIVE) {
			release_sock(parent);
			return POLLIN | POLLRDNORM;
		}
	}
	release_sock(parent);

	return 0;
}

static unsigned int smc_poll(struct file *file, struct socket *sock,
			     poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask = 0;
	struct smc_sock *smc;
	int rc;

	smc = smc_sk(sock->sk);
	if ((sk->sk_state == SMC_INIT) || smc->use_fallback) {
		/* delegate to CLC child sock */
		mask = smc->clcsock->ops->poll(file, smc->clcsock, wait);
		/* if non-blocking connect finished ... */
		lock_sock(sk);
		if ((sk->sk_state == SMC_INIT) && (mask & POLLOUT)) {
			sk->sk_err = smc->clcsock->sk->sk_err;
			if (sk->sk_err) {
				mask |= POLLERR;
			} else {
				rc = smc_connect_rdma(smc);
				if (rc < 0)
					mask |= POLLERR;
				else
					/* success cases including fallback */
					mask |= POLLOUT | POLLWRNORM;
			}
		}
		release_sock(sk);
	} else {
		sock_poll_wait(file, sk_sleep(sk), wait);
		if (sk->sk_state == SMC_LISTEN)
			/* woken up by sk_data_ready in smc_listen_work() */
			mask |= smc_accept_poll(sk);
		if (sk->sk_err)
			mask |= POLLERR;
		if (atomic_read(&smc->conn.sndbuf_space) ||
		    (sk->sk_shutdown & SEND_SHUTDOWN)) {
			mask |= POLLOUT | POLLWRNORM;
		} else {
			sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);
			set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		}
		if (atomic_read(&smc->conn.bytes_to_rcv))
			mask |= POLLIN | POLLRDNORM;
		if ((sk->sk_shutdown == SHUTDOWN_MASK) ||
		    (sk->sk_state == SMC_CLOSED))
			mask |= POLLHUP;
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			mask |= POLLIN | POLLRDNORM | POLLRDHUP;
		if (sk->sk_state == SMC_APPCLOSEWAIT1)
			mask |= POLLIN;

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
	if ((sk->sk_state != SMC_LISTEN) &&
	    (sk->sk_state != SMC_ACTIVE) &&
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
		if (sk->sk_state == SMC_LISTEN)
			rc = smc_close_active(smc);
		else
			rc = 0;
			/* nothing more to do because peer is not involved */
		break;
	}
	rc1 = kernel_sock_shutdown(smc->clcsock, how);
	/* map sock_shutdown_cmd constants to sk_shutdown value range */
	sk->sk_shutdown |= how + 1;

out:
	release_sock(sk);
	return rc ? rc : rc1;
}

static int smc_setsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;

	smc = smc_sk(sk);

	/* generic setsockopts reaching us here always apply to the
	 * CLC socket
	 */
	return smc->clcsock->ops->setsockopt(smc->clcsock, level, optname,
					     optval, optlen);
}

static int smc_getsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct smc_sock *smc;

	smc = smc_sk(sock->sk);
	/* socket options apply to the CLC socket */
	return smc->clcsock->ops->getsockopt(smc->clcsock, level, optname,
					     optval, optlen);
}

static int smc_ioctl(struct socket *sock, unsigned int cmd,
		     unsigned long arg)
{
	struct smc_sock *smc;

	smc = smc_sk(sock->sk);
	if (smc->use_fallback)
		return smc->clcsock->ops->ioctl(smc->clcsock, cmd, arg);
	else
		return sock_no_ioctl(sock, cmd, arg);
}

static ssize_t smc_sendpage(struct socket *sock, struct page *page,
			    int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = -EPIPE;

	smc = smc_sk(sk);
	lock_sock(sk);
	if (sk->sk_state != SMC_ACTIVE)
		goto out;
	if (smc->use_fallback)
		rc = kernel_sendpage(smc->clcsock, page, offset,
				     size, flags);
	else
		rc = sock_no_sendpage(sock, page, offset, size, flags);

out:
	release_sock(sk);
	return rc;
}

static ssize_t smc_splice_read(struct socket *sock, loff_t *ppos,
			       struct pipe_inode_info *pipe, size_t len,
				    unsigned int flags)
{
	struct sock *sk = sock->sk;
	struct smc_sock *smc;
	int rc = -ENOTCONN;

	smc = smc_sk(sk);
	lock_sock(sk);
	if ((sk->sk_state != SMC_ACTIVE) && (sk->sk_state != SMC_CLOSED))
		goto out;
	if (smc->use_fallback) {
		rc = smc->clcsock->ops->splice_read(smc->clcsock, ppos,
						    pipe, len, flags);
	} else {
		rc = -EOPNOTSUPP;
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
	struct smc_sock *smc;
	struct sock *sk;
	int rc;

	rc = -ESOCKTNOSUPPORT;
	if (sock->type != SOCK_STREAM)
		goto out;

	rc = -EPROTONOSUPPORT;
	if ((protocol != IPPROTO_IP) && (protocol != IPPROTO_TCP))
		goto out;

	rc = -ENOBUFS;
	sock->ops = &smc_sock_ops;
	sk = smc_sock_alloc(net, sock);
	if (!sk)
		goto out;

	/* create internal TCP socket for CLC handshake and fallback */
	smc = smc_sk(sk);
	smc->use_fallback = false; /* assume rdma capability first */
	rc = sock_create_kern(net, PF_INET, SOCK_STREAM,
			      IPPROTO_TCP, &smc->clcsock);
	if (rc)
		sk_common_release(sk);
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

static int __init smc_init(void)
{
	int rc;

	rc = smc_pnet_init();
	if (rc)
		return rc;

	rc = smc_llc_init();
	if (rc) {
		pr_err("%s: smc_llc_init fails with %d\n", __func__, rc);
		goto out_pnet;
	}

	rc = smc_cdc_init();
	if (rc) {
		pr_err("%s: smc_cdc_init fails with %d\n", __func__, rc);
		goto out_pnet;
	}

	rc = proto_register(&smc_proto, 1);
	if (rc) {
		pr_err("%s: proto_register fails with %d\n", __func__, rc);
		goto out_pnet;
	}

	rc = sock_register(&smc_sock_family_ops);
	if (rc) {
		pr_err("%s: sock_register fails with %d\n", __func__, rc);
		goto out_proto;
	}
	INIT_HLIST_HEAD(&smc_v4_hashinfo.ht);

	rc = smc_ib_register_client();
	if (rc) {
		pr_err("%s: ib_register fails with %d\n", __func__, rc);
		goto out_sock;
	}

	static_branch_enable(&tcp_have_smc);
	return 0;

out_sock:
	sock_unregister(PF_SMC);
out_proto:
	proto_unregister(&smc_proto);
out_pnet:
	smc_pnet_exit();
	return rc;
}

static void __exit smc_exit(void)
{
	struct smc_link_group *lgr, *lg;
	LIST_HEAD(lgr_freeing_list);

	spin_lock_bh(&smc_lgr_list.lock);
	if (!list_empty(&smc_lgr_list.list))
		list_splice_init(&smc_lgr_list.list, &lgr_freeing_list);
	spin_unlock_bh(&smc_lgr_list.lock);
	list_for_each_entry_safe(lgr, lg, &lgr_freeing_list, list) {
		list_del_init(&lgr->list);
		smc_lgr_free(lgr); /* free link group */
	}
	static_branch_disable(&tcp_have_smc);
	smc_ib_unregister_client();
	sock_unregister(PF_SMC);
	proto_unregister(&smc_proto);
	smc_pnet_exit();
}

module_init(smc_init);
module_exit(smc_exit);

MODULE_AUTHOR("Ursula Braun <ubraun@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("smc socket address family");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_SMC);
