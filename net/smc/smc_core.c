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
#include <linux/wait.h>
#include <linux/reboot.h>
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

static atomic_t lgr_cnt = ATOMIC_INIT(0); /* number of existing link groups */
static DECLARE_WAIT_QUEUE_HEAD(lgrs_deleted);

static void smc_buf_free(struct smc_link_group *lgr, bool is_rmb,
			 struct smc_buf_desc *buf_desc);
static void __smc_lgr_terminate(struct smc_link_group *lgr, bool soft);

/* return head of link group list and its lock for a given link group */
static inline struct list_head *smc_lgr_list_head(struct smc_link_group *lgr,
						  spinlock_t **lgr_lock)
{
	if (lgr->is_smcd) {
		*lgr_lock = &lgr->smcd->lgr_lock;
		return &lgr->smcd->lgr_list;
	}

	*lgr_lock = &smc_lgr_list.lock;
	return &smc_lgr_list.list;
}

static void smc_lgr_schedule_free_work(struct smc_link_group *lgr)
{
	/* client link group creation always follows the server link group
	 * creation. For client use a somewhat higher removal delay time,
	 * otherwise there is a risk of out-of-sync link groups.
	 */
	if (!lgr->freeing && !lgr->freefast) {
		mod_delayed_work(system_wq, &lgr->free_work,
				 (!lgr->is_smcd && lgr->role == SMC_CLNT) ?
						SMC_LGR_FREE_DELAY_CLNT :
						SMC_LGR_FREE_DELAY_SERV);
	}
}

void smc_lgr_schedule_free_work_fast(struct smc_link_group *lgr)
{
	if (!lgr->freeing && !lgr->freefast) {
		lgr->freefast = 1;
		mod_delayed_work(system_wq, &lgr->free_work,
				 SMC_LGR_FREE_DELAY_FAST);
	}
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
	conn->lgr = NULL;
}

void smc_lgr_cleanup_early(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!lgr)
		return;

	smc_conn_free(conn);
	smc_lgr_forget(lgr);
	smc_lgr_schedule_free_work_fast(lgr);
}

/* Send delete link, either as client to request the initiation
 * of the DELETE LINK sequence from server; or as server to
 * initiate the delete processing. See smc_llc_rx_delete_link().
 */
static int smc_link_send_delete(struct smc_link *lnk, bool orderly)
{
	if (lnk->state == SMC_LNK_ACTIVE &&
	    !smc_llc_send_delete_link(lnk, SMC_LLC_REQ, orderly)) {
		smc_llc_link_deleting(lnk);
		return 0;
	}
	return -ENOTCONN;
}

static void smc_lgr_free(struct smc_link_group *lgr);

static void smc_lgr_free_work(struct work_struct *work)
{
	struct smc_link_group *lgr = container_of(to_delayed_work(work),
						  struct smc_link_group,
						  free_work);
	spinlock_t *lgr_lock;
	struct smc_link *lnk;
	bool conns;

	smc_lgr_list_head(lgr, &lgr_lock);
	spin_lock_bh(lgr_lock);
	if (lgr->freeing) {
		spin_unlock_bh(lgr_lock);
		return;
	}
	read_lock_bh(&lgr->conns_lock);
	conns = RB_EMPTY_ROOT(&lgr->conns_all);
	read_unlock_bh(&lgr->conns_lock);
	if (!conns) { /* number of lgr connections is no longer zero */
		spin_unlock_bh(lgr_lock);
		return;
	}
	list_del_init(&lgr->list); /* remove from smc_lgr_list */

	lnk = &lgr->lnk[SMC_SINGLE_LINK];
	if (!lgr->is_smcd && !lgr->terminating)	{
		/* try to send del link msg, on error free lgr immediately */
		if (lnk->state == SMC_LNK_ACTIVE &&
		    !smc_link_send_delete(lnk, true)) {
			/* reschedule in case we never receive a response */
			smc_lgr_schedule_free_work(lgr);
			spin_unlock_bh(lgr_lock);
			return;
		}
	}
	lgr->freeing = 1; /* this instance does the freeing, no new schedule */
	spin_unlock_bh(lgr_lock);
	cancel_delayed_work(&lgr->free_work);

	if (!lgr->is_smcd && lnk->state != SMC_LNK_INACTIVE)
		smc_llc_link_inactive(lnk);
	if (lgr->is_smcd && !lgr->terminating)
		smc_ism_signal_shutdown(lgr);
	smc_lgr_free(lgr);
}

static void smc_lgr_terminate_work(struct work_struct *work)
{
	struct smc_link_group *lgr = container_of(work, struct smc_link_group,
						  terminate_work);

	__smc_lgr_terminate(lgr, true);
}

/* create a new SMC link group */
static int smc_lgr_create(struct smc_sock *smc, struct smc_init_info *ini)
{
	struct smc_link_group *lgr;
	struct list_head *lgr_list;
	struct smc_link *lnk;
	spinlock_t *lgr_lock;
	u8 rndvec[3];
	int rc = 0;
	int i;

	if (ini->is_smcd && ini->vlan_id) {
		if (smc_ism_get_vlan(ini->ism_dev, ini->vlan_id)) {
			rc = SMC_CLC_DECL_ISMVLANERR;
			goto out;
		}
	}

	lgr = kzalloc(sizeof(*lgr), GFP_KERNEL);
	if (!lgr) {
		rc = SMC_CLC_DECL_MEM;
		goto ism_put_vlan;
	}
	lgr->is_smcd = ini->is_smcd;
	lgr->sync_err = 0;
	lgr->terminating = 0;
	lgr->freefast = 0;
	lgr->freeing = 0;
	lgr->vlan_id = ini->vlan_id;
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
	INIT_WORK(&lgr->terminate_work, smc_lgr_terminate_work);
	lgr->conns_all = RB_ROOT;
	if (ini->is_smcd) {
		/* SMC-D specific settings */
		get_device(&ini->ism_dev->dev);
		lgr->peer_gid = ini->ism_gid;
		lgr->smcd = ini->ism_dev;
		lgr_list = &ini->ism_dev->lgr_list;
		lgr_lock = &lgr->smcd->lgr_lock;
		lgr->peer_shutdown = 0;
		atomic_inc(&ini->ism_dev->lgr_cnt);
	} else {
		/* SMC-R specific settings */
		get_device(&ini->ib_dev->ibdev->dev);
		lgr->role = smc->listen_smc ? SMC_SERV : SMC_CLNT;
		memcpy(lgr->peer_systemid, ini->ib_lcl->id_for_peer,
		       SMC_SYSTEMID_LEN);

		lnk = &lgr->lnk[SMC_SINGLE_LINK];
		/* initialize link */
		lnk->state = SMC_LNK_ACTIVATING;
		lnk->link_id = SMC_SINGLE_LINK;
		lnk->smcibdev = ini->ib_dev;
		lnk->ibport = ini->ib_port;
		lgr_list = &smc_lgr_list.list;
		lgr_lock = &smc_lgr_list.lock;
		lnk->path_mtu =
			ini->ib_dev->pattr[ini->ib_port - 1].active_mtu;
		if (!ini->ib_dev->initialized)
			smc_ib_setup_per_ibdev(ini->ib_dev);
		get_random_bytes(rndvec, sizeof(rndvec));
		lnk->psn_initial = rndvec[0] + (rndvec[1] << 8) +
			(rndvec[2] << 16);
		rc = smc_ib_determine_gid(lnk->smcibdev, lnk->ibport,
					  ini->vlan_id, lnk->gid,
					  &lnk->sgid_index);
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
		atomic_inc(&lgr_cnt);
		atomic_inc(&ini->ib_dev->lnk_cnt);
	}
	smc->conn.lgr = lgr;
	spin_lock_bh(lgr_lock);
	list_add(&lgr->list, lgr_list);
	spin_unlock_bh(lgr_lock);
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
ism_put_vlan:
	if (ini->is_smcd && ini->vlan_id)
		smc_ism_put_vlan(ini->ism_dev, ini->vlan_id);
out:
	if (rc < 0) {
		if (rc == -ENOMEM)
			rc = SMC_CLC_DECL_MEM;
		else
			rc = SMC_CLC_DECL_INTERR;
	}
	return rc;
}

static void smc_buf_unuse(struct smc_connection *conn,
			  struct smc_link_group *lgr)
{
	if (conn->sndbuf_desc)
		conn->sndbuf_desc->used = 0;
	if (conn->rmb_desc) {
		if (!conn->rmb_desc->regerr) {
			if (!lgr->is_smcd && !list_empty(&lgr->list)) {
				/* unregister rmb with peer */
				smc_llc_do_delete_rkey(
						&lgr->lnk[SMC_SINGLE_LINK],
						conn->rmb_desc);
			}
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
		if (!list_empty(&lgr->list))
			smc_ism_unset_conn(conn);
		tasklet_kill(&conn->rx_tsklet);
	} else {
		smc_cdc_tx_dismiss_slots(conn);
	}
	if (!list_empty(&lgr->list)) {
		smc_lgr_unregister_conn(conn);
		smc_buf_unuse(conn, lgr); /* allow buffer reuse */
	}

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
	if (!atomic_dec_return(&lnk->smcibdev->lnk_cnt))
		wake_up(&lnk->smcibdev->lnks_deleted);
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
static void smc_lgr_free(struct smc_link_group *lgr)
{
	smc_lgr_free_bufs(lgr);
	if (lgr->is_smcd) {
		if (!lgr->terminating) {
			smc_ism_put_vlan(lgr->smcd, lgr->vlan_id);
			put_device(&lgr->smcd->dev);
		}
		if (!atomic_dec_return(&lgr->smcd->lgr_cnt))
			wake_up(&lgr->smcd->lgrs_deleted);
	} else {
		smc_link_clear(&lgr->lnk[SMC_SINGLE_LINK]);
		put_device(&lgr->lnk[SMC_SINGLE_LINK].smcibdev->ibdev->dev);
		if (!atomic_dec_return(&lgr_cnt))
			wake_up(&lgrs_deleted);
	}
	kfree(lgr);
}

void smc_lgr_forget(struct smc_link_group *lgr)
{
	struct list_head *lgr_list;
	spinlock_t *lgr_lock;

	lgr_list = smc_lgr_list_head(lgr, &lgr_lock);
	spin_lock_bh(lgr_lock);
	/* do not use this link group for new connections */
	if (!list_empty(lgr_list))
		list_del_init(lgr_list);
	spin_unlock_bh(lgr_lock);
}

static void smcd_unregister_all_dmbs(struct smc_link_group *lgr)
{
	int i;

	for (i = 0; i < SMC_RMBE_SIZES; i++) {
		struct smc_buf_desc *buf_desc;

		list_for_each_entry(buf_desc, &lgr->rmbs[i], list) {
			buf_desc->len += sizeof(struct smcd_cdc_msg);
			smc_ism_unregister_dmb(lgr->smcd, buf_desc);
		}
	}
}

static void smc_sk_wake_ups(struct smc_sock *smc)
{
	smc->sk.sk_write_space(&smc->sk);
	smc->sk.sk_data_ready(&smc->sk);
	smc->sk.sk_state_change(&smc->sk);
}

/* kill a connection */
static void smc_conn_kill(struct smc_connection *conn, bool soft)
{
	struct smc_sock *smc = container_of(conn, struct smc_sock, conn);

	if (conn->lgr->is_smcd && conn->lgr->peer_shutdown)
		conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;
	else
		smc_close_abort(conn);
	conn->killed = 1;
	smc->sk.sk_err = ECONNABORTED;
	smc_sk_wake_ups(smc);
	if (conn->lgr->is_smcd) {
		smc_ism_unset_conn(conn);
		if (soft)
			tasklet_kill(&conn->rx_tsklet);
		else
			tasklet_unlock_wait(&conn->rx_tsklet);
	} else {
		smc_cdc_tx_dismiss_slots(conn);
	}
	smc_lgr_unregister_conn(conn);
	smc_close_active_abort(smc);
}

static void smc_lgr_cleanup(struct smc_link_group *lgr)
{
	if (lgr->is_smcd) {
		smc_ism_signal_shutdown(lgr);
		smcd_unregister_all_dmbs(lgr);
		smc_ism_put_vlan(lgr->smcd, lgr->vlan_id);
		put_device(&lgr->smcd->dev);
	} else {
		struct smc_link *lnk = &lgr->lnk[SMC_SINGLE_LINK];

		if (lnk->state != SMC_LNK_INACTIVE)
			smc_llc_link_inactive(lnk);
	}
}

/* terminate link group
 * @soft: true if link group shutdown can take its time
 *	  false if immediate link group shutdown is required
 */
static void __smc_lgr_terminate(struct smc_link_group *lgr, bool soft)
{
	struct smc_connection *conn;
	struct smc_sock *smc;
	struct rb_node *node;

	if (lgr->terminating)
		return;	/* lgr already terminating */
	if (!soft)
		cancel_delayed_work_sync(&lgr->free_work);
	lgr->terminating = 1;
	if (!lgr->is_smcd)
		smc_llc_link_inactive(&lgr->lnk[SMC_SINGLE_LINK]);

	/* kill remaining link group connections */
	read_lock_bh(&lgr->conns_lock);
	node = rb_first(&lgr->conns_all);
	while (node) {
		read_unlock_bh(&lgr->conns_lock);
		conn = rb_entry(node, struct smc_connection, alert_node);
		smc = container_of(conn, struct smc_sock, conn);
		sock_hold(&smc->sk); /* sock_put below */
		lock_sock(&smc->sk);
		smc_conn_kill(conn, soft);
		release_sock(&smc->sk);
		sock_put(&smc->sk); /* sock_hold above */
		read_lock_bh(&lgr->conns_lock);
		node = rb_first(&lgr->conns_all);
	}
	read_unlock_bh(&lgr->conns_lock);
	smc_lgr_cleanup(lgr);
	if (soft)
		smc_lgr_schedule_free_work_fast(lgr);
	else
		smc_lgr_free(lgr);
}

/* unlink link group and schedule termination */
void smc_lgr_terminate_sched(struct smc_link_group *lgr)
{
	spinlock_t *lgr_lock;

	smc_lgr_list_head(lgr, &lgr_lock);
	spin_lock_bh(lgr_lock);
	if (list_empty(&lgr->list) || lgr->terminating || lgr->freeing) {
		spin_unlock_bh(lgr_lock);
		return;	/* lgr already terminating */
	}
	list_del_init(&lgr->list);
	spin_unlock_bh(lgr_lock);
	schedule_work(&lgr->terminate_work);
}

/* Called when IB port is terminated */
void smc_port_terminate(struct smc_ib_device *smcibdev, u8 ibport)
{
	struct smc_link_group *lgr, *l;
	LIST_HEAD(lgr_free_list);

	spin_lock_bh(&smc_lgr_list.lock);
	list_for_each_entry_safe(lgr, l, &smc_lgr_list.list, list) {
		if (!lgr->is_smcd &&
		    lgr->lnk[SMC_SINGLE_LINK].smcibdev == smcibdev &&
		    lgr->lnk[SMC_SINGLE_LINK].ibport == ibport) {
			list_move(&lgr->list, &lgr_free_list);
			lgr->freeing = 1;
		}
	}
	spin_unlock_bh(&smc_lgr_list.lock);

	list_for_each_entry_safe(lgr, l, &lgr_free_list, list) {
		list_del_init(&lgr->list);
		__smc_lgr_terminate(lgr, false);
	}
}

/* Called when peer lgr shutdown (regularly or abnormally) is received */
void smc_smcd_terminate(struct smcd_dev *dev, u64 peer_gid, unsigned short vlan)
{
	struct smc_link_group *lgr, *l;
	LIST_HEAD(lgr_free_list);

	/* run common cleanup function and build free list */
	spin_lock_bh(&dev->lgr_lock);
	list_for_each_entry_safe(lgr, l, &dev->lgr_list, list) {
		if ((!peer_gid || lgr->peer_gid == peer_gid) &&
		    (vlan == VLAN_VID_MASK || lgr->vlan_id == vlan)) {
			if (peer_gid) /* peer triggered termination */
				lgr->peer_shutdown = 1;
			list_move(&lgr->list, &lgr_free_list);
		}
	}
	spin_unlock_bh(&dev->lgr_lock);

	/* cancel the regular free workers and actually free lgrs */
	list_for_each_entry_safe(lgr, l, &lgr_free_list, list) {
		list_del_init(&lgr->list);
		schedule_work(&lgr->terminate_work);
	}
}

/* Called when an SMCD device is removed or the smc module is unloaded */
void smc_smcd_terminate_all(struct smcd_dev *smcd)
{
	struct smc_link_group *lgr, *lg;
	LIST_HEAD(lgr_free_list);

	spin_lock_bh(&smcd->lgr_lock);
	list_splice_init(&smcd->lgr_list, &lgr_free_list);
	list_for_each_entry(lgr, &lgr_free_list, list)
		lgr->freeing = 1;
	spin_unlock_bh(&smcd->lgr_lock);

	list_for_each_entry_safe(lgr, lg, &lgr_free_list, list) {
		list_del_init(&lgr->list);
		__smc_lgr_terminate(lgr, false);
	}

	if (atomic_read(&smcd->lgr_cnt))
		wait_event(smcd->lgrs_deleted, !atomic_read(&smcd->lgr_cnt));
}

/* Called when an SMCR device is removed or the smc module is unloaded.
 * If smcibdev is given, all SMCR link groups using this device are terminated.
 * If smcibdev is NULL, all SMCR link groups are terminated.
 */
void smc_smcr_terminate_all(struct smc_ib_device *smcibdev)
{
	struct smc_link_group *lgr, *lg;
	LIST_HEAD(lgr_free_list);

	spin_lock_bh(&smc_lgr_list.lock);
	if (!smcibdev) {
		list_splice_init(&smc_lgr_list.list, &lgr_free_list);
		list_for_each_entry(lgr, &lgr_free_list, list)
			lgr->freeing = 1;
	} else {
		list_for_each_entry_safe(lgr, lg, &smc_lgr_list.list, list) {
			if (lgr->lnk[SMC_SINGLE_LINK].smcibdev == smcibdev) {
				list_move(&lgr->list, &lgr_free_list);
				lgr->freeing = 1;
			}
		}
	}
	spin_unlock_bh(&smc_lgr_list.lock);

	list_for_each_entry_safe(lgr, lg, &lgr_free_list, list) {
		list_del_init(&lgr->list);
		__smc_lgr_terminate(lgr, false);
	}

	if (smcibdev) {
		if (atomic_read(&smcibdev->lnk_cnt))
			wait_event(smcibdev->lnks_deleted,
				   !atomic_read(&smcibdev->lnk_cnt));
	} else {
		if (atomic_read(&lgr_cnt))
			wait_event(lgrs_deleted, !atomic_read(&lgr_cnt));
	}
}

/* Determine vlan of internal TCP socket.
 * @vlan_id: address to store the determined vlan id into
 */
int smc_vlan_by_tcpsk(struct socket *clcsock, struct smc_init_info *ini)
{
	struct dst_entry *dst = sk_dst_get(clcsock->sk);
	struct net_device *ndev;
	int i, nest_lvl, rc = 0;

	ini->vlan_id = 0;
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
		ini->vlan_id = vlan_dev_vlan_id(ndev);
		goto out_rel;
	}

	rtnl_lock();
	nest_lvl = ndev->lower_level;
	for (i = 0; i < nest_lvl; i++) {
		struct list_head *lower = &ndev->adj_list.lower;

		if (list_empty(lower))
			break;
		lower = lower->next;
		ndev = (struct net_device *)netdev_lower_get_next(ndev, &lower);
		if (is_vlan_dev(ndev)) {
			ini->vlan_id = vlan_dev_vlan_id(ndev);
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
			   enum smc_lgr_role role, u32 clcqpn)
{
	return !memcmp(lgr->peer_systemid, lcl->id_for_peer,
		       SMC_SYSTEMID_LEN) &&
		!memcmp(lgr->lnk[SMC_SINGLE_LINK].peer_gid, &lcl->gid,
			SMC_GID_SIZE) &&
		!memcmp(lgr->lnk[SMC_SINGLE_LINK].peer_mac, lcl->mac,
			sizeof(lcl->mac)) &&
		lgr->role == role &&
		(lgr->role == SMC_SERV ||
		 lgr->lnk[SMC_SINGLE_LINK].peer_qpn == clcqpn);
}

static bool smcd_lgr_match(struct smc_link_group *lgr,
			   struct smcd_dev *smcismdev, u64 peer_gid)
{
	return lgr->peer_gid == peer_gid && lgr->smcd == smcismdev;
}

/* create a new SMC connection (and a new link group if necessary) */
int smc_conn_create(struct smc_sock *smc, struct smc_init_info *ini)
{
	struct smc_connection *conn = &smc->conn;
	struct list_head *lgr_list;
	struct smc_link_group *lgr;
	enum smc_lgr_role role;
	spinlock_t *lgr_lock;
	int rc = 0;

	lgr_list = ini->is_smcd ? &ini->ism_dev->lgr_list : &smc_lgr_list.list;
	lgr_lock = ini->is_smcd ? &ini->ism_dev->lgr_lock : &smc_lgr_list.lock;
	ini->cln_first_contact = SMC_FIRST_CONTACT;
	role = smc->listen_smc ? SMC_SERV : SMC_CLNT;
	if (role == SMC_CLNT && ini->srv_first_contact)
		/* create new link group as well */
		goto create;

	/* determine if an existing link group can be reused */
	spin_lock_bh(lgr_lock);
	list_for_each_entry(lgr, lgr_list, list) {
		write_lock_bh(&lgr->conns_lock);
		if ((ini->is_smcd ?
		     smcd_lgr_match(lgr, ini->ism_dev, ini->ism_gid) :
		     smcr_lgr_match(lgr, ini->ib_lcl, role, ini->ib_clcqpn)) &&
		    !lgr->sync_err &&
		    lgr->vlan_id == ini->vlan_id &&
		    (role == SMC_CLNT ||
		     lgr->conns_num < SMC_RMBS_PER_LGR_MAX)) {
			/* link group found */
			ini->cln_first_contact = SMC_REUSE_CONTACT;
			conn->lgr = lgr;
			smc_lgr_register_conn(conn); /* add smc conn to lgr */
			if (delayed_work_pending(&lgr->free_work))
				cancel_delayed_work(&lgr->free_work);
			write_unlock_bh(&lgr->conns_lock);
			break;
		}
		write_unlock_bh(&lgr->conns_lock);
	}
	spin_unlock_bh(lgr_lock);

	if (role == SMC_CLNT && !ini->srv_first_contact &&
	    ini->cln_first_contact == SMC_FIRST_CONTACT) {
		/* Server reuses a link group, but Client wants to start
		 * a new one
		 * send out_of_sync decline, reason synchr. error
		 */
		return SMC_CLC_DECL_SYNCERR;
	}

create:
	if (ini->cln_first_contact == SMC_FIRST_CONTACT) {
		rc = smc_lgr_create(smc, ini);
		if (rc)
			goto out;
		lgr = conn->lgr;
		write_lock_bh(&lgr->conns_lock);
		smc_lgr_register_conn(conn); /* add smc conn to lgr */
		write_unlock_bh(&lgr->conns_lock);
	}
	conn->local_tx_ctrl.common.type = SMC_CDC_MSG_TYPE;
	conn->local_tx_ctrl.len = SMC_WR_TX_SIZE;
	conn->urg_state = SMC_URG_READ;
	if (ini->is_smcd) {
		conn->rx_off = sizeof(struct smcd_cdc_msg);
		smcd_cdc_rx_init(conn); /* init tasklet for this conn */
	}
#ifndef KERNEL_HAS_ATOMIC64
	spin_lock_init(&conn->acurs_lock);
#endif

out:
	return rc;
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

#define SMCD_DMBE_SIZES		7 /* 0 -> 16KB, 1 -> 32KB, .. 6 -> 1MB */

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

static void smc_core_going_away(void)
{
	struct smc_ib_device *smcibdev;
	struct smcd_dev *smcd;

	spin_lock(&smc_ib_devices.lock);
	list_for_each_entry(smcibdev, &smc_ib_devices.list, list) {
		int i;

		for (i = 0; i < SMC_MAX_PORTS; i++)
			set_bit(i, smcibdev->ports_going_away);
	}
	spin_unlock(&smc_ib_devices.lock);

	spin_lock(&smcd_dev_list.lock);
	list_for_each_entry(smcd, &smcd_dev_list.list, list) {
		smcd->going_away = 1;
	}
	spin_unlock(&smcd_dev_list.lock);
}

/* Clean up all SMC link groups */
static void smc_lgrs_shutdown(void)
{
	struct smcd_dev *smcd;

	smc_core_going_away();

	smc_smcr_terminate_all(NULL);

	spin_lock(&smcd_dev_list.lock);
	list_for_each_entry(smcd, &smcd_dev_list.list, list)
		smc_smcd_terminate_all(smcd);
	spin_unlock(&smcd_dev_list.lock);
}

static int smc_core_reboot_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	smc_lgrs_shutdown();
	smc_ib_unregister_client();
	return 0;
}

static struct notifier_block smc_reboot_notifier = {
	.notifier_call = smc_core_reboot_event,
};

int __init smc_core_init(void)
{
	return register_reboot_notifier(&smc_reboot_notifier);
}

/* Called (from smc_exit) when module is removed */
void smc_core_exit(void)
{
	unregister_reboot_notifier(&smc_reboot_notifier);
	smc_lgrs_shutdown();
}
