// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Basic Transport Functions exploiting Infiniband API
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/socket.h>
#include <linux/if_vlan.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>

#include "smc.h"
#include "smc_clc.h"
#include "smc_core.h"
#include "smc_ib.h"
#include "smc_wr.h"
#include "smc_llc.h"
#include "smc_cdc.h"
#include "smc_close.h"
#include "smc_ism.h"

#define SMC_LGR_NUM_INCR		256
#define SMC_LGR_FREE_DELAY_SERV		(600 * HZ)
#define SMC_LGR_FREE_DELAY_CLNT		(SMC_LGR_FREE_DELAY_SERV + 10 * HZ)
#define SMC_LGR_FREE_DELAY_FAST		(8 * HZ)

static struct smc_lgr_list smc_lgr_list = {	/* established link groups */
	.lock = __SPIN_LOCK_UNLOCKED(smc_lgr_list.lock),
	.list = LIST_HEAD_INIT(smc_lgr_list.list),
	.num = 0,
};

static void smc_buf_free(struct smc_link_group *lgr, bool is_rmb,
			 struct smc_buf_desc *buf_desc);

static void smc_lgr_schedule_free_work(struct smc_link_group *lgr)
{
	/* client link group creation always follows the server link group
	 * creation. For client use a somewhat higher removal delay time,
	 * otherwise there is a risk of out-of-sync link groups.
	 */
	mod_delayed_work(system_wq, &lgr->free_work,
			 (!lgr->is_smcd && lgr->role == SMC_CLNT) ?
			 SMC_LGR_FREE_DELAY_CLNT : SMC_LGR_FREE_DELAY_SERV);
}

void smc_lgr_schedule_free_work_fast(struct smc_link_group *lgr)
{
	mod_delayed_work(system_wq, &lgr->free_work, SMC_LGR_FREE_DELAY_FAST);
}

/* Register connection's alert token in our lookup structure.
 * To use rbtrees we have to implement our own insert core.
 * Requires @conns_lock
 * @smc		connection to register
 * Returns 0 on success, != otherwise.
 */
static void smc_lgr_add_alert_token(struct smc_connection *conn)
{
	struct rb_node **link, *parent = NULL;
	u32 token = conn->alert_token_local;

	link = &conn->lgr->conns_all.rb_node;
	while (*link) {
		struct smc_connection *cur = rb_entry(*link,
					struct smc_connection, alert_node);

		parent = *link;
		if (cur->alert_token_local > token)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}
	/* Put the new node there */
	rb_link_node(&conn->alert_node, parent, link);
	rb_insert_color(&conn->alert_node, &conn->lgr->conns_all);
}

/* Register connection in link group by assigning an alert token
 * registered in a search tree.
 * Requires @conns_lock
 * Note that '0' is a reserved value and not assigned.
 */
static void smc_lgr_register_conn(struct smc_connection *conn)
{
	struct smc_sock *smc = container_of(conn, struct smc_sock, conn);
	static atomic_t nexttoken = ATOMIC_INIT(0);

	/* find a new alert_token_local value not yet used by some connection
	 * in this link group
	 */
	sock_hold(&smc->sk); /* sock_put in smc_lgr_unregister_conn() */
	while (!conn->alert_token_local) {
		conn->alert_token_local = atomic_inc_return(&nexttoken);
		if (smc_lgr_find_conn(conn->alert_token_local, conn->lgr))
			conn->alert_token_local = 0;
	}
	smc_lgr_add_alert_token(conn);
	conn->lgr->conns_num++;
}

/* Unregister connection and reset the alert token of the given connection<
 */
static void __smc_lgr_unregister_conn(struct smc_connection *conn)
{
	struct smc_sock *smc = container_of(conn, struct smc_sock, conn);
	struct smc_link_group *lgr = conn->lgr;

	rb_erase(&conn->alert_node, &lgr->conns_all);
	lgr->conns_num--;
	conn->alert_token_local = 0;
	conn->lgr = NULL;
	sock_put(&smc->sk); /* sock_hold in smc_lgr_register_conn() */
}

/* Unregister connection from lgr
 */
static void smc_lgr_unregister_conn(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!lgr)
		return;
	write_lock_bh(&lgr->conns_lock);
	if (conn->alert_token_local) {
		__smc_lgr_unregister_conn(conn);
	}
	write_unlock_bh(&lgr->conns_lock);
}

/* Send delete link, either as client to request the initiation
 * of the DELETE LINK sequence from server; or as server to
 * initiate the delete processing. See smc_llc_rx_delete_link().
 */
static int smc_link_send_delete(struct smc_link *lnk)
{
	if (lnk->state == SMC_LNK_ACTIVE &&
	    !smc_llc_send_delete_link(lnk, SMC_LLC_REQ, true)) {
		smc_llc_link_deleting(lnk);
		return 0;
	}
	return -ENOTCONN;
}

static void smc_lgr_free_work(struct work_struct *work)
{
	struct smc_link_group *lgr = container_of(to_delayed_work(work),
						  struct smc_link_group,
						  free_work);
	bool conns;

	spin_lock_bh(&smc_lgr_list.lock);
	if (list_empty(&lgr->list))
		goto free;
	read_lock_bh(&lgr->conns_lock);
	conns = RB_EMPTY_ROOT(&lgr->conns_all);
	read_unlock_bh(&lgr->conns_lock);
	if (!conns) { /* number of lgr connections is no longer zero */
		spin_unlock_bh(&smc_lgr_list.lock);
		return;
	}
	list_del_init(&lgr->list); /* remove from smc_lgr_list */
free:
	spin_unlock_bh(&smc_lgr_list.lock);

	if (!lgr->is_smcd && !lgr->terminating)	{
		/* try to send del link msg, on error free lgr immediately */
		if (!smc_link_send_delete(&lgr->lnk[SMC_SINGLE_LINK])) {
			/* reschedule in case we never receive a response */
			smc_lgr_schedule_free_work(lgr);
			return;
		}
	}

	if (!delayed_work_pending(&lgr->free_work)) {
		struct smc_link *lnk = &lgr->lnk[SMC_SINGLE_LINK];

		if (!lgr->is_smcd && lnk->state != SMC_LNK_INACTIVE)
			smc_llc_link_inactive(lnk);
		smc_lgr_free(lgr);
	}
}

/* create a new SMC link group */
static int smc_lgr_create(struct smc_sock *smc, bool is_smcd,
			  struct smc_ib_device *smcibdev, u8 ibport,
			  char *peer_systemid, unsigned short vlan_id,
			  struct smcd_dev *smcismdev, u64 peer_gid)
{
	struct smc_link_group *lgr;
	struct smc_link *lnk;
	u8 rndvec[3];
	int rc = 0;
	int i;

	if (is_smcd && vlan_id) {
		rc = smc_ism_get_vlan(smcismdev, vlan_id);
		if (rc)
			goto out;
	}

	lgr = kzalloc(sizeof(*lgr), GFP_KERNEL);
	if (!lgr) {
		rc = -ENOMEM;
		goto out;
	}
	lgr->is_smcd = is_smcd;
	lgr->sync_err = 0;
	lgr->vlan_id = vlan_id;
	rwlock_init(&lgr->sndbufs_lock);
	rwlock_init(&lgr->rmbs_lock);
	rwlock_init(&lgr->conns_lock);
	for (i = 0; i < SMC_RMBE_SIZES; i++) {
		INIT_LIST_HEAD(&lgr->sndbufs[i]);
		INIT_LIST_HEAD(&lgr->rmbs[i]);
	}
	smc_lgr_list.num += SMC_LGR_NUM_INCR;
	memcpy(&lgr->id, (u8 *)&smc_lgr_list.num, SMC_LGR_ID_SIZE);
	INIT_DELAYED_WORK(&lgr->free_work, smc_lgr_free_work);
	lgr->conns_all = RB_ROOT;
	if (is_smcd) {
		/* SMC-D specific settings */
		lgr->peer_gid = peer_gid;
		lgr->smcd = smcismdev;
	} else {
		/* SMC-R specific settings */
		lgr->role = smc->listen_smc ? SMC_SERV : SMC_CLNT;
		memcpy(lgr->peer_systemid, peer_systemid, SMC_SYSTEMID_LEN);

		lnk = &lgr->lnk[SMC_SINGLE_LINK];
		/* initialize link */
		lnk->state = SMC_LNK_ACTIVATING;
		lnk->link_id = SMC_SINGLE_LINK;
		lnk->smcibdev = smcibdev;
		lnk->ibport = ibport;
		lnk->path_mtu = smcibdev->pattr[ibport - 1].active_mtu;
		if (!smcibdev->initialized)
			smc_ib_setup_per_ibdev(smcibdev);
		get_random_bytes(rndvec, sizeof(rndvec));
		lnk->psn_initial = rndvec[0] + (rndvec[1] << 8) +
			(rndvec[2] << 16);
		rc = smc_ib_determine_gid(lnk->smcibdev, lnk->ibport,
					  vlan_id, lnk->gid, &lnk->sgid_index);
		if (rc)
			goto free_lgr;
		rc = smc_llc_link_init(lnk);
		if (rc)
			goto free_lgr;
		rc = smc_wr_alloc_link_mem(lnk);
		if (rc)
			goto clear_llc_lnk;
		rc = smc_ib_create_protection_domain(lnk);
		if (rc)
			goto free_link_mem;
		rc = smc_ib_create_queue_pair(lnk);
		if (rc)
			goto dealloc_pd;
		rc = smc_wr_create_link(lnk);
		if (rc)
			goto destroy_qp;
	}
	smc->conn.lgr = lgr;
	spin_lock_bh(&smc_lgr_list.lock);
	list_add(&lgr->list, &smc_lgr_list.list);
	spin_unlock_bh(&smc_lgr_list.lock);
	return 0;

destroy_qp:
	smc_ib_destroy_queue_pair(lnk);
dealloc_pd:
	smc_ib_dealloc_protection_domain(lnk);
free_link_mem:
	smc_wr_free_link_mem(lnk);
clear_llc_lnk:
	smc_llc_link_clear(lnk);
free_lgr:
	kfree(lgr);
out:
	return rc;
}

static void smc_buf_unuse(struct smc_connection *conn,
			  struct smc_link_group *lgr)
{
	if (conn->sndbuf_desc)
		conn->sndbuf_desc->used = 0;
	if (conn->rmb_desc) {
		if (!conn->rmb_desc->regerr) {
			conn->rmb_desc->reused = 1;
			conn->rmb_desc->used = 0;
		} else {
			/* buf registration failed, reuse not possible */
			write_lock_bh(&lgr->rmbs_lock);
			list_del(&conn->rmb_desc->list);
			write_unlock_bh(&lgr->rmbs_lock);

			smc_buf_free(lgr, true, conn->rmb_desc);
		}
	}
}

/* remove a finished connection from its link group */
void smc_conn_free(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!lgr)
		return;
	if (lgr->is_smcd) {
		smc_ism_unset_conn(conn);
		tasklet_kill(&conn->rx_tsklet);
	} else {
		smc_cdc_tx_dismiss_slots(conn);
	}
	smc_lgr_unregister_conn(conn);		/* unsets conn->lgr */
	smc_buf_unuse(conn, lgr);		/* allow buffer reuse */

	if (!lgr->conns_num)
		smc_lgr_schedule_free_work(lgr);
}

static void smc_link_clear(struct smc_link *lnk)
{
	lnk->peer_qpn = 0;
	smc_llc_link_clear(lnk);
	smc_ib_modify_qp_reset(lnk);
	smc_wr_free_link(lnk);
	smc_ib_destroy_queue_pair(lnk);
	smc_ib_dealloc_protection_domain(lnk);
	smc_wr_free_link_mem(lnk);
}

static void smcr_buf_free(struct smc_link_group *lgr, bool is_rmb,
			  struct smc_buf_desc *buf_desc)
{
	struct smc_link *lnk = &lgr->lnk[SMC_SINGLE_LINK];

	if (is_rmb) {
		if (buf_desc->mr_rx[SMC_SINGLE_LINK])
			smc_ib_put_memory_region(
					buf_desc->mr_rx[SMC_SINGLE_LINK]);
		smc_ib_buf_unmap_sg(lnk->smcibdev, buf_desc,
				    DMA_FROM_DEVICE);
	} else {
		smc_ib_buf_unmap_sg(lnk->smcibdev, buf_desc,
				    DMA_TO_DEVICE);
	}
	sg_free_table(&buf_desc->sgt[SMC_SINGLE_LINK]);
	if (buf_desc->pages)
		__free_pages(buf_desc->pages, buf_desc->order);
	kfree(buf_desc);
}

static void smcd_buf_free(struct smc_link_group *lgr, bool is_dmb,
			  struct smc_buf_desc *buf_desc)
{
	if (is_dmb) {
		/* restore original buf len */
		buf_desc->len += sizeof(struct smcd_cdc_msg);
		smc_ism_unregister_dmb(lgr->smcd, buf_desc);
	} else {
		kfree(buf_desc->cpu_addr);
	}
	kfree(buf_desc);
}

static void smc_buf_free(struct smc_link_group *lgr, bool is_rmb,
			 struct smc_buf_desc *buf_desc)
{
	if (lgr->is_smcd)
		smcd_buf_free(lgr, is_rmb, buf_desc);
	else
		smcr_buf_free(lgr, is_rmb, buf_desc);
}

static void __smc_lgr_free_bufs(struct smc_link_group *lgr, bool is_rmb)
{
	struct smc_buf_desc *buf_desc, *bf_desc;
	struct list_head *buf_list;
	int i;

	for (i = 0; i < SMC_RMBE_SIZES; i++) {
		if (is_rmb)
			buf_list = &lgr->rmbs[i];
		else
			buf_list = &lgr->sndbufs[i];
		list_for_each_entry_safe(buf_desc, bf_desc, buf_list,
					 list) {
			list_del(&buf_desc->list);
			smc_buf_free(lgr, is_rmb, buf_desc);
		}
	}
}

static void smc_lgr_free_bufs(struct smc_link_group *lgr)
{
	/* free send buffers */
	__smc_lgr_free_bufs(lgr, false);
	/* free rmbs */
	__smc_lgr_free_bufs(lgr, true);
}

/* remove a link group */
void smc_lgr_free(struct smc_link_group *lgr)
{
	smc_lgr_free_bufs(lgr);
	if (lgr->is_smcd)
		smc_ism_put_vlan(lgr->smcd, lgr->vlan_id);
	else
		smc_link_clear(&lgr->lnk[SMC_SINGLE_LINK]);
	kfree(lgr);
}

void smc_lgr_forget(struct smc_link_group *lgr)
{
	spin_lock_bh(&smc_lgr_list.lock);
	/* do not use this link group for new connections */
	if (!list_empty(&lgr->list))
		list_del_init(&lgr->list);
	spin_unlock_bh(&smc_lgr_list.lock);
}

/* terminate linkgroup abnormally */
static void __smc_lgr_terminate(struct smc_link_group *lgr)
{
	struct smc_connection *conn;
	struct smc_sock *smc;
	struct rb_node *node;

	if (lgr->terminating)
		return;	/* lgr already terminating */
	lgr->terminating = 1;
	if (!list_empty(&lgr->list)) /* forget lgr */
		list_del_init(&lgr->list);
	if (!lgr->is_smcd)
		smc_llc_link_inactive(&lgr->lnk[SMC_SINGLE_LINK]);

	write_lock_bh(&lgr->conns_lock);
	node = rb_first(&lgr->conns_all);
	while (node) {
		conn = rb_entry(node, struct smc_connection, alert_node);
		smc = container_of(conn, struct smc_sock, conn);
		sock_hold(&smc->sk); /* sock_put in close work */
		conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;
		__smc_lgr_unregister_conn(conn);
		write_unlock_bh(&lgr->conns_lock);
		if (!schedule_work(&conn->close_work))
			sock_put(&smc->sk);
		write_lock_bh(&lgr->conns_lock);
		node = rb_first(&lgr->conns_all);
	}
	write_unlock_bh(&lgr->conns_lock);
	if (!lgr->is_smcd)
		wake_up(&lgr->lnk[SMC_SINGLE_LINK].wr_reg_wait);
	smc_lgr_schedule_free_work(lgr);
}

void smc_lgr_terminate(struct smc_link_group *lgr)
{
	spin_lock_bh(&smc_lgr_list.lock);
	__smc_lgr_terminate(lgr);
	spin_unlock_bh(&smc_lgr_list.lock);
}

/* Called when IB port is terminated */
void smc_port_terminate(struct smc_ib_device *smcibdev, u8 ibport)
{
	struct smc_link_group *lgr, *l;

	spin_lock_bh(&smc_lgr_list.lock);
	list_for_each_entry_safe(lgr, l, &smc_lgr_list.list, list) {
		if (!lgr->is_smcd &&
		    lgr->lnk[SMC_SINGLE_LINK].smcibdev == smcibdev &&
		    lgr->lnk[SMC_SINGLE_LINK].ibport == ibport)
			__smc_lgr_terminate(lgr);
	}
	spin_unlock_bh(&smc_lgr_list.lock);
}

/* Called when SMC-D device is terminated or peer is lost */
void smc_smcd_terminate(struct smcd_dev *dev, u64 peer_gid)
{
	struct smc_link_group *lgr, *l;
	LIST_HEAD(lgr_free_list);

	/* run common cleanup function and build free list */
	spin_lock_bh(&smc_lgr_list.lock);
	list_for_each_entry_safe(lgr, l, &smc_lgr_list.list, list) {
		if (lgr->is_smcd && lgr->smcd == dev &&
		    (!peer_gid || lgr->peer_gid == peer_gid) &&
		    !list_empty(&lgr->list)) {
			__smc_lgr_terminate(lgr);
			list_move(&lgr->list, &lgr_free_list);
		}
	}
	spin_unlock_bh(&smc_lgr_list.lock);

	/* cancel the regular free workers and actually free lgrs */
	list_for_each_entry_safe(lgr, l, &lgr_free_list, list) {
		list_del_init(&lgr->list);
		cancel_delayed_work_sync(&lgr->free_work);
		smc_lgr_free(lgr);
	}
}

/* Determine vlan of internal TCP socket.
 * @vlan_id: address to store the determined vlan id into
 */
int smc_vlan_by_tcpsk(struct socket *clcsock, unsigned short *vlan_id)
{
	struct dst_entry *dst = sk_dst_get(clcsock->sk);
	struct net_device *ndev;
	int i, nest_lvl, rc = 0;

	*vlan_id = 0;
	if (!dst) {
		rc = -ENOTCONN;
		goto out;
	}
	if (!dst->dev) {
		rc = -ENODEV;
		goto out_rel;
	}

	ndev = dst->dev;
	if (is_vlan_dev(ndev)) {
		*vlan_id = vlan_dev_vlan_id(ndev);
		goto out_rel;
	}

	rtnl_lock();
	nest_lvl = dev_get_nest_level(ndev);
	for (i = 0; i < nest_lvl; i++) {
		struct list_head *lower = &ndev->adj_list.lower;

		if (list_empty(lower))
			break;
		lower = lower->next;
		ndev = (struct net_device *)netdev_lower_get_next(ndev, &lower);
		if (is_vlan_dev(ndev)) {
			*vlan_id = vlan_dev_vlan_id(ndev);
			break;
		}
	}
	rtnl_unlock();

out_rel:
	dst_release(dst);
out:
	return rc;
}

static bool smcr_lgr_match(struct smc_link_group *lgr,
			   struct smc_clc_msg_local *lcl,
			   enum smc_lgr_role role)
{
	return !memcmp(lgr->peer_systemid, lcl->id_for_peer,
		       SMC_SYSTEMID_LEN) &&
		!memcmp(lgr->lnk[SMC_SINGLE_LINK].peer_gid, &lcl->gid,
			SMC_GID_SIZE) &&
		!memcmp(lgr->lnk[SMC_SINGLE_LINK].peer_mac, lcl->mac,
			sizeof(lcl->mac)) &&
		lgr->role == role;
}

static bool smcd_lgr_match(struct smc_link_group *lgr,
			   struct smcd_dev *smcismdev, u64 peer_gid)
{
	return lgr->peer_gid == peer_gid && lgr->smcd == smcismdev;
}

/* create a new SMC connection (and a new link group if necessary) */
int smc_conn_create(struct smc_sock *smc, bool is_smcd, int srv_first_contact,
		    struct smc_ib_device *smcibdev, u8 ibport,
		    struct smc_clc_msg_local *lcl, struct smcd_dev *smcd,
		    u64 peer_gid)
{
	struct smc_connection *conn = &smc->conn;
	int local_contact = SMC_FIRST_CONTACT;
	struct smc_link_group *lgr;
	unsigned short vlan_id;
	enum smc_lgr_role role;
	int rc = 0;

	role = smc->listen_smc ? SMC_SERV : SMC_CLNT;
	rc = smc_vlan_by_tcpsk(smc->clcsock, &vlan_id);
	if (rc)
		return rc;

	if ((role == SMC_CLNT) && srv_first_contact)
		/* create new link group as well */
		goto create;

	/* determine if an existing link group can be reused */
	spin_lock_bh(&smc_lgr_list.lock);
	list_for_each_entry(lgr, &smc_lgr_list.list, list) {
		write_lock_bh(&lgr->conns_lock);
		if ((is_smcd ? smcd_lgr_match(lgr, smcd, peer_gid) :
		     smcr_lgr_match(lgr, lcl, role)) &&
		    !lgr->sync_err &&
		    lgr->vlan_id == vlan_id &&
		    (role == SMC_CLNT ||
		     lgr->conns_num < SMC_RMBS_PER_LGR_MAX)) {
			/* link group found */
			local_contact = SMC_REUSE_CONTACT;
			conn->lgr = lgr;
			smc_lgr_register_conn(conn); /* add smc conn to lgr */
			if (delayed_work_pending(&lgr->free_work))
				cancel_delayed_work(&lgr->free_work);
			write_unlock_bh(&lgr->conns_lock);
			break;
		}
		write_unlock_bh(&lgr->conns_lock);
	}
	spin_unlock_bh(&smc_lgr_list.lock);

	if (role == SMC_CLNT && !srv_first_contact &&
	    (local_contact == SMC_FIRST_CONTACT)) {
		/* Server reuses a link group, but Client wants to start
		 * a new one
		 * send out_of_sync decline, reason synchr. error
		 */
		return -ENOLINK;
	}

create:
	if (local_contact == SMC_FIRST_CONTACT) {
		rc = smc_lgr_create(smc, is_smcd, smcibdev, ibport,
				    lcl->id_for_peer, vlan_id, smcd, peer_gid);
		if (rc)
			goto out;
		smc_lgr_register_conn(conn); /* add smc conn to lgr */
	}
	conn->local_tx_ctrl.common.type = SMC_CDC_MSG_TYPE;
	conn->local_tx_ctrl.len = SMC_WR_TX_SIZE;
	conn->urg_state = SMC_URG_READ;
	if (is_smcd) {
		conn->rx_off = sizeof(struct smcd_cdc_msg);
		smcd_cdc_rx_init(conn); /* init tasklet for this conn */
	}
#ifndef KERNEL_HAS_ATOMIC64
	spin_lock_init(&conn->acurs_lock);
#endif

out:
	return rc ? rc : local_contact;
}

/* convert the RMB size into the compressed notation - minimum 16K.
 * In contrast to plain ilog2, this rounds towards the next power of 2,
 * so the socket application gets at least its desired sndbuf / rcvbuf size.
 */
static u8 smc_compress_bufsize(int size)
{
	u8 compressed;

	if (size <= SMC_BUF_MIN_SIZE)
		return 0;

	size = (size - 1) >> 14;
	compressed = ilog2(size) + 1;
	if (compressed >= SMC_RMBE_SIZES)
		compressed = SMC_RMBE_SIZES - 1;
	return compressed;
}

/* convert the RMB size from compressed notation into integer */
int smc_uncompress_bufsize(u8 compressed)
{
	u32 size;

	size = 0x00000001 << (((int)compressed) + 14);
	return (int)size;
}

/* try to reuse a sndbuf or rmb description slot for a certain
 * buffer size; if not available, return NULL
 */
static struct smc_buf_desc *smc_buf_get_slot(int compressed_bufsize,
					     rwlock_t *lock,
					     struct list_head *buf_list)
{
	struct smc_buf_desc *buf_slot;

	read_lock_bh(lock);
	list_for_each_entry(buf_slot, buf_list, list) {
		if (cmpxchg(&buf_slot->used, 0, 1) == 0) {
			read_unlock_bh(lock);
			return buf_slot;
		}
	}
	read_unlock_bh(lock);
	return NULL;
}

/* one of the conditions for announcing a receiver's current window size is
 * that it "results in a minimum increase in the window size of 10% of the
 * receive buffer space" [RFC7609]
 */
static inline int smc_rmb_wnd_update_limit(int rmbe_size)
{
	return min_t(int, rmbe_size / 10, SOCK_MIN_SNDBUF / 2);
}

static struct smc_buf_desc *smcr_new_buf_create(struct smc_link_group *lgr,
						bool is_rmb, int bufsize)
{
	struct smc_buf_desc *buf_desc;
	struct smc_link *lnk;
	int rc;

	/* try to alloc a new buffer */
	buf_desc = kzalloc(sizeof(*buf_desc), GFP_KERNEL);
	if (!buf_desc)
		return ERR_PTR(-ENOMEM);

	buf_desc->order = get_order(bufsize);
	buf_desc->pages = alloc_pages(GFP_KERNEL | __GFP_NOWARN |
				      __GFP_NOMEMALLOC | __GFP_COMP |
				      __GFP_NORETRY | __GFP_ZERO,
				      buf_desc->order);
	if (!buf_desc->pages) {
		kfree(buf_desc);
		return ERR_PTR(-EAGAIN);
	}
	buf_desc->cpu_addr = (void *)page_address(buf_desc->pages);

	/* build the sg table from the pages */
	lnk = &lgr->lnk[SMC_SINGLE_LINK];
	rc = sg_alloc_table(&buf_desc->sgt[SMC_SINGLE_LINK], 1,
			    GFP_KERNEL);
	if (rc) {
		smc_buf_free(lgr, is_rmb, buf_desc);
		return ERR_PTR(rc);
	}
	sg_set_buf(buf_desc->sgt[SMC_SINGLE_LINK].sgl,
		   buf_desc->cpu_addr, bufsize);

	/* map sg table to DMA address */
	rc = smc_ib_buf_map_sg(lnk->smcibdev, buf_desc,
			       is_rmb ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
	/* SMC protocol depends on mapping to one DMA address only */
	if (rc != 1)  {
		smc_buf_free(lgr, is_rmb, buf_desc);
		return ERR_PTR(-EAGAIN);
	}

	/* create a new memory region for the RMB */
	if (is_rmb) {
		rc = smc_ib_get_memory_region(lnk->roce_pd,
					      IB_ACCESS_REMOTE_WRITE |
					      IB_ACCESS_LOCAL_WRITE,
					      buf_desc);
		if (rc) {
			smc_buf_free(lgr, is_rmb, buf_desc);
			return ERR_PTR(rc);
		}
	}

	buf_desc->len = bufsize;
	return buf_desc;
}

#define SMCD_DMBE_SIZES		6 /* 0 -> 16KB, 1 -> 32KB, .. 6 -> 1MB */

static struct smc_buf_desc *smcd_new_buf_create(struct smc_link_group *lgr,
						bool is_dmb, int bufsize)
{
	struct smc_buf_desc *buf_desc;
	int rc;

	if (smc_compress_bufsize(bufsize) > SMCD_DMBE_SIZES)
		return ERR_PTR(-EAGAIN);

	/* try to alloc a new DMB */
	buf_desc = kzalloc(sizeof(*buf_desc), GFP_KERNEL);
	if (!buf_desc)
		return ERR_PTR(-ENOMEM);
	if (is_dmb) {
		rc = smc_ism_register_dmb(lgr, bufsize, buf_desc);
		if (rc) {
			kfree(buf_desc);
			return ERR_PTR(-EAGAIN);
		}
		buf_desc->pages = virt_to_page(buf_desc->cpu_addr);
		/* CDC header stored in buf. So, pretend it was smaller */
		buf_desc->len = bufsize - sizeof(struct smcd_cdc_msg);
	} else {
		buf_desc->cpu_addr = kzalloc(bufsize, GFP_KERNEL |
					     __GFP_NOWARN | __GFP_NORETRY |
					     __GFP_NOMEMALLOC);
		if (!buf_desc->cpu_addr) {
			kfree(buf_desc);
			return ERR_PTR(-EAGAIN);
		}
		buf_desc->len = bufsize;
	}
	return buf_desc;
}

static int __smc_buf_create(struct smc_sock *smc, bool is_smcd, bool is_rmb)
{
	struct smc_buf_desc *buf_desc = ERR_PTR(-ENOMEM);
	struct smc_connection *conn = &smc->conn;
	struct smc_link_group *lgr = conn->lgr;
	struct list_head *buf_list;
	int bufsize, bufsize_short;
	int sk_buf_size;
	rwlock_t *lock;

	if (is_rmb)
		/* use socket recv buffer size (w/o overhead) as start value */
		sk_buf_size = smc->sk.sk_rcvbuf / 2;
	else
		/* use socket send buffer size (w/o overhead) as start value */
		sk_buf_size = smc->sk.sk_sndbuf / 2;

	for (bufsize_short = smc_compress_bufsize(sk_buf_size);
	     bufsize_short >= 0; bufsize_short--) {

		if (is_rmb) {
			lock = &lgr->rmbs_lock;
			buf_list = &lgr->rmbs[bufsize_short];
		} else {
			lock = &lgr->sndbufs_lock;
			buf_list = &lgr->sndbufs[bufsize_short];
		}
		bufsize = smc_uncompress_bufsize(bufsize_short);
		if ((1 << get_order(bufsize)) > SG_MAX_SINGLE_ALLOC)
			continue;

		/* check for reusable slot in the link group */
		buf_desc = smc_buf_get_slot(bufsize_short, lock, buf_list);
		if (buf_desc) {
			memset(buf_desc->cpu_addr, 0, bufsize);
			break; /* found reusable slot */
		}

		if (is_smcd)
			buf_desc = smcd_new_buf_create(lgr, is_rmb, bufsize);
		else
			buf_desc = smcr_new_buf_create(lgr, is_rmb, bufsize);

		if (PTR_ERR(buf_desc) == -ENOMEM)
			break;
		if (IS_ERR(buf_desc))
			continue;

		buf_desc->used = 1;
		write_lock_bh(lock);
		list_add(&buf_desc->list, buf_list);
		write_unlock_bh(lock);
		break; /* found */
	}

	if (IS_ERR(buf_desc))
		return -ENOMEM;

	if (is_rmb) {
		conn->rmb_desc = buf_desc;
		conn->rmbe_size_short = bufsize_short;
		smc->sk.sk_rcvbuf = bufsize * 2;
		atomic_set(&conn->bytes_to_rcv, 0);
		conn->rmbe_update_limit =
			smc_rmb_wnd_update_limit(buf_desc->len);
		if (is_smcd)
			smc_ism_set_conn(conn); /* map RMB/smcd_dev to conn */
	} else {
		conn->sndbuf_desc = buf_desc;
		smc->sk.sk_sndbuf = bufsize * 2;
		atomic_set(&conn->sndbuf_space, bufsize);
	}
	return 0;
}

void smc_sndbuf_sync_sg_for_cpu(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!conn->lgr || conn->lgr->is_smcd)
		return;
	smc_ib_sync_sg_for_cpu(lgr->lnk[SMC_SINGLE_LINK].smcibdev,
			       conn->sndbuf_desc, DMA_TO_DEVICE);
}

void smc_sndbuf_sync_sg_for_device(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!conn->lgr || conn->lgr->is_smcd)
		return;
	smc_ib_sync_sg_for_device(lgr->lnk[SMC_SINGLE_LINK].smcibdev,
				  conn->sndbuf_desc, DMA_TO_DEVICE);
}

void smc_rmb_sync_sg_for_cpu(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!conn->lgr || conn->lgr->is_smcd)
		return;
	smc_ib_sync_sg_for_cpu(lgr->lnk[SMC_SINGLE_LINK].smcibdev,
			       conn->rmb_desc, DMA_FROM_DEVICE);
}

void smc_rmb_sync_sg_for_device(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!conn->lgr || conn->lgr->is_smcd)
		return;
	smc_ib_sync_sg_for_device(lgr->lnk[SMC_SINGLE_LINK].smcibdev,
				  conn->rmb_desc, DMA_FROM_DEVICE);
}

/* create the send and receive buffer for an SMC socket;
 * receive buffers are called RMBs;
 * (even though the SMC protocol allows more than one RMB-element per RMB,
 * the Linux implementation uses just one RMB-element per RMB, i.e. uses an
 * extra RMB for every connection in a link group
 */
int smc_buf_create(struct smc_sock *smc, bool is_smcd)
{
	int rc;

	/* create send buffer */
	rc = __smc_buf_create(smc, is_smcd, false);
	if (rc)
		return rc;
	/* create rmb */
	rc = __smc_buf_create(smc, is_smcd, true);
	if (rc)
		smc_buf_free(smc->conn.lgr, false, smc->conn.sndbuf_desc);
	return rc;
}

static inline int smc_rmb_reserve_rtoken_idx(struct smc_link_group *lgr)
{
	int i;

	for_each_clear_bit(i, lgr->rtokens_used_mask, SMC_RMBS_PER_LGR_MAX) {
		if (!test_and_set_bit(i, lgr->rtokens_used_mask))
			return i;
	}
	return -ENOSPC;
}

/* add a new rtoken from peer */
int smc_rtoken_add(struct smc_link_group *lgr, __be64 nw_vaddr, __be32 nw_rkey)
{
	u64 dma_addr = be64_to_cpu(nw_vaddr);
	u32 rkey = ntohl(nw_rkey);
	int i;

	for (i = 0; i < SMC_RMBS_PER_LGR_MAX; i++) {
		if ((lgr->rtokens[i][SMC_SINGLE_LINK].rkey == rkey) &&
		    (lgr->rtokens[i][SMC_SINGLE_LINK].dma_addr == dma_addr) &&
		    test_bit(i, lgr->rtokens_used_mask)) {
			/* already in list */
			return i;
		}
	}
	i = smc_rmb_reserve_rtoken_idx(lgr);
	if (i < 0)
		return i;
	lgr->rtokens[i][SMC_SINGLE_LINK].rkey = rkey;
	lgr->rtokens[i][SMC_SINGLE_LINK].dma_addr = dma_addr;
	return i;
}

/* delete an rtoken */
int smc_rtoken_delete(struct smc_link_group *lgr, __be32 nw_rkey)
{
	u32 rkey = ntohl(nw_rkey);
	int i;

	for (i = 0; i < SMC_RMBS_PER_LGR_MAX; i++) {
		if (lgr->rtokens[i][SMC_SINGLE_LINK].rkey == rkey &&
		    test_bit(i, lgr->rtokens_used_mask)) {
			lgr->rtokens[i][SMC_SINGLE_LINK].rkey = 0;
			lgr->rtokens[i][SMC_SINGLE_LINK].dma_addr = 0;

			clear_bit(i, lgr->rtokens_used_mask);
			return 0;
		}
	}
	return -ENOENT;
}

/* save rkey and dma_addr received from peer during clc handshake */
int smc_rmb_rtoken_handling(struct smc_connection *conn,
			    struct smc_clc_msg_accept_confirm *clc)
{
	conn->rtoken_idx = smc_rtoken_add(conn->lgr, clc->rmb_dma_addr,
					  clc->rmb_rkey);
	if (conn->rtoken_idx < 0)
		return conn->rtoken_idx;
	return 0;
}

/* Called (from smc_exit) when module is removed */
void smc_core_exit(void)
{
	struct smc_link_group *lgr, *lg;
	LIST_HEAD(lgr_freeing_list);

	spin_lock_bh(&smc_lgr_list.lock);
	if (!list_empty(&smc_lgr_list.list))
		list_splice_init(&smc_lgr_list.list, &lgr_freeing_list);
	spin_unlock_bh(&smc_lgr_list.lock);
	list_for_each_entry_safe(lgr, lg, &lgr_freeing_list, list) {
		list_del_init(&lgr->list);
		if (!lgr->is_smcd) {
			struct smc_link *lnk = &lgr->lnk[SMC_SINGLE_LINK];

			if (lnk->state == SMC_LNK_ACTIVE)
				smc_llc_send_delete_link(lnk, SMC_LLC_REQ,
							 false);
			smc_llc_link_inactive(lnk);
		}
		cancel_delayed_work_sync(&lgr->free_work);
		smc_lgr_free(lgr); /* free link group */
	}
}
