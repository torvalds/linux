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

#include "smc.h"
#include "smc_clc.h"
#include "smc_core.h"
#include "smc_ib.h"

#define SMC_LGR_FREE_DELAY	(600 * HZ)

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

/* Unregister connection and trigger lgr freeing if applicable
 */
static void smc_lgr_unregister_conn(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;
	int reduced = 0;

	write_lock_bh(&lgr->conns_lock);
	if (conn->alert_token_local) {
		reduced = 1;
		__smc_lgr_unregister_conn(conn);
	}
	write_unlock_bh(&lgr->conns_lock);
	if (reduced && !lgr->conns_num)
		schedule_delayed_work(&lgr->free_work, SMC_LGR_FREE_DELAY);
}

static void smc_lgr_free_work(struct work_struct *work)
{
	struct smc_link_group *lgr = container_of(to_delayed_work(work),
						  struct smc_link_group,
						  free_work);
	bool conns;

	spin_lock_bh(&smc_lgr_list.lock);
	read_lock_bh(&lgr->conns_lock);
	conns = RB_EMPTY_ROOT(&lgr->conns_all);
	read_unlock_bh(&lgr->conns_lock);
	if (!conns) { /* number of lgr connections is no longer zero */
		spin_unlock_bh(&smc_lgr_list.lock);
		return;
	}
	list_del_init(&lgr->list); /* remove from smc_lgr_list */
	spin_unlock_bh(&smc_lgr_list.lock);
	smc_lgr_free(lgr);
}

/* create a new SMC link group */
static int smc_lgr_create(struct smc_sock *smc, __be32 peer_in_addr,
			  struct smc_ib_device *smcibdev, u8 ibport,
			  char *peer_systemid, unsigned short vlan_id)
{
	struct smc_link_group *lgr;
	struct smc_link *lnk;
	u8 rndvec[3];
	int rc = 0;

	lgr = kzalloc(sizeof(*lgr), GFP_KERNEL);
	if (!lgr) {
		rc = -ENOMEM;
		goto out;
	}
	lgr->role = smc->listen_smc ? SMC_SERV : SMC_CLNT;
	lgr->sync_err = false;
	lgr->daddr = peer_in_addr;
	memcpy(lgr->peer_systemid, peer_systemid, SMC_SYSTEMID_LEN);
	lgr->vlan_id = vlan_id;
	INIT_DELAYED_WORK(&lgr->free_work, smc_lgr_free_work);
	lgr->conns_all = RB_ROOT;

	lnk = &lgr->lnk[SMC_SINGLE_LINK];
	/* initialize link */
	lnk->smcibdev = smcibdev;
	lnk->ibport = ibport;
	lnk->path_mtu = smcibdev->pattr[ibport - 1].active_mtu;
	get_random_bytes(rndvec, sizeof(rndvec));
	lnk->psn_initial = rndvec[0] + (rndvec[1] << 8) + (rndvec[2] << 16);

	smc->conn.lgr = lgr;
	rwlock_init(&lgr->conns_lock);
	spin_lock_bh(&smc_lgr_list.lock);
	list_add(&lgr->list, &smc_lgr_list.list);
	spin_unlock_bh(&smc_lgr_list.lock);
out:
	return rc;
}

/* remove a finished connection from its link group */
void smc_conn_free(struct smc_connection *conn)
{
	struct smc_link_group *lgr = conn->lgr;

	if (!lgr)
		return;
	smc_lgr_unregister_conn(conn);
}

static void smc_link_clear(struct smc_link *lnk)
{
	lnk->peer_qpn = 0;
}

/* remove a link group */
void smc_lgr_free(struct smc_link_group *lgr)
{
	smc_link_clear(&lgr->lnk[SMC_SINGLE_LINK]);
	kfree(lgr);
}

/* terminate linkgroup abnormally */
void smc_lgr_terminate(struct smc_link_group *lgr)
{
	struct smc_connection *conn;
	struct rb_node *node;

	spin_lock_bh(&smc_lgr_list.lock);
	if (list_empty(&lgr->list)) {
		/* termination already triggered */
		spin_unlock_bh(&smc_lgr_list.lock);
		return;
	}
	/* do not use this link group for new connections */
	list_del_init(&lgr->list);
	spin_unlock_bh(&smc_lgr_list.lock);

	write_lock_bh(&lgr->conns_lock);
	node = rb_first(&lgr->conns_all);
	while (node) {
		conn = rb_entry(node, struct smc_connection, alert_node);
		__smc_lgr_unregister_conn(conn);
		node = rb_first(&lgr->conns_all);
	}
	write_unlock_bh(&lgr->conns_lock);
	schedule_delayed_work(&lgr->free_work, SMC_LGR_FREE_DELAY);
}

/* Determine vlan of internal TCP socket.
 * @vlan_id: address to store the determined vlan id into
 */
static int smc_vlan_by_tcpsk(struct socket *clcsock, unsigned short *vlan_id)
{
	struct dst_entry *dst = sk_dst_get(clcsock->sk);
	int rc = 0;

	*vlan_id = 0;
	if (!dst) {
		rc = -ENOTCONN;
		goto out;
	}
	if (!dst->dev) {
		rc = -ENODEV;
		goto out_rel;
	}

	if (is_vlan_dev(dst->dev))
		*vlan_id = vlan_dev_vlan_id(dst->dev);

out_rel:
	dst_release(dst);
out:
	return rc;
}

/* determine the link gid matching the vlan id of the link group */
static int smc_link_determine_gid(struct smc_link_group *lgr)
{
	struct smc_link *lnk = &lgr->lnk[SMC_SINGLE_LINK];
	struct ib_gid_attr gattr;
	union ib_gid gid;
	int i;

	if (!lgr->vlan_id) {
		lnk->gid = lnk->smcibdev->gid[lnk->ibport - 1];
		return 0;
	}

	for (i = 0; i < lnk->smcibdev->pattr[lnk->ibport - 1].gid_tbl_len;
	     i++) {
		if (ib_query_gid(lnk->smcibdev->ibdev, lnk->ibport, i, &gid,
				 &gattr))
			continue;
		if (gattr.ndev &&
		    (vlan_dev_vlan_id(gattr.ndev) == lgr->vlan_id)) {
			lnk->gid = gid;
			return 0;
		}
	}
	return -ENODEV;
}

/* create a new SMC connection (and a new link group if necessary) */
int smc_conn_create(struct smc_sock *smc, __be32 peer_in_addr,
		    struct smc_ib_device *smcibdev, u8 ibport,
		    struct smc_clc_msg_local *lcl, int srv_first_contact)
{
	struct smc_connection *conn = &smc->conn;
	struct smc_link_group *lgr;
	unsigned short vlan_id;
	enum smc_lgr_role role;
	int local_contact = SMC_FIRST_CONTACT;
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
		if (!memcmp(lgr->peer_systemid, lcl->id_for_peer,
			    SMC_SYSTEMID_LEN) &&
		    !memcmp(lgr->lnk[SMC_SINGLE_LINK].peer_gid, &lcl->gid,
			    SMC_GID_SIZE) &&
		    !memcmp(lgr->lnk[SMC_SINGLE_LINK].peer_mac, lcl->mac,
			    sizeof(lcl->mac)) &&
		    !lgr->sync_err &&
		    (lgr->role == role) &&
		    (lgr->vlan_id == vlan_id)) {
			/* link group found */
			local_contact = SMC_REUSE_CONTACT;
			conn->lgr = lgr;
			smc_lgr_register_conn(conn); /* add smc conn to lgr */
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
		rc = smc_lgr_create(smc, peer_in_addr, smcibdev, ibport,
				    lcl->id_for_peer, vlan_id);
		if (rc)
			goto out;
		smc_lgr_register_conn(conn); /* add smc conn to lgr */
		rc = smc_link_determine_gid(conn->lgr);
	}

out:
	return rc ? rc : local_contact;
}
