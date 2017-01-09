/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for SMC Connections, Link Groups and Links
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#ifndef _SMC_CORE_H
#define _SMC_CORE_H

#include <rdma/ib_verbs.h>

#include "smc.h"
#include "smc_ib.h"

#define SMC_RMBS_PER_LGR_MAX	255	/* max. # of RMBs per link group */

struct smc_lgr_list {			/* list of link group definition */
	struct list_head	list;
	spinlock_t		lock;	/* protects list of link groups */
};

extern struct smc_lgr_list	smc_lgr_list; /* list of link groups */

enum smc_lgr_role {		/* possible roles of a link group */
	SMC_CLNT,	/* client */
	SMC_SERV	/* server */
};

struct smc_link {
	struct smc_ib_device	*smcibdev;	/* ib-device */
	u8			ibport;		/* port - values 1 | 2 */
	struct ib_qp		*roce_qp;	/* IB queue pair */
	struct ib_qp_attr	qp_attr;	/* IB queue pair attributes */
	union ib_gid		gid;		/* gid matching used vlan id */
	u32			peer_qpn;	/* QP number of peer */
	enum ib_mtu		path_mtu;	/* used mtu */
	enum ib_mtu		peer_mtu;	/* mtu size of peer */
	u32			psn_initial;	/* QP tx initial packet seqno */
	u32			peer_psn;	/* QP rx initial packet seqno */
	u8			peer_mac[ETH_ALEN];	/* = gid[8:10||13:15] */
	u8			peer_gid[sizeof(union ib_gid)];	/* gid of peer*/
};

/* For now we just allow one parallel link per link group. The SMC protocol
 * allows more (up to 8).
 */
#define SMC_LINKS_PER_LGR_MAX	1
#define SMC_SINGLE_LINK		0

#define SMC_FIRST_CONTACT	1		/* first contact to a peer */
#define SMC_REUSE_CONTACT	0		/* follow-on contact to a peer*/

/* tx/rx buffer list element for sndbufs list and rmbs list of a lgr */
struct smc_buf_desc {
	struct list_head	list;
	u64			dma_addr[SMC_LINKS_PER_LGR_MAX];
						/* mapped address of buffer */
	void			*cpu_addr;	/* virtual address of buffer */
	u32			used;		/* currently used / unused */
};

struct smc_link_group {
	struct list_head	list;
	enum smc_lgr_role	role;		/* client or server */
	__be32			daddr;		/* destination ip address */
	struct smc_link		lnk[SMC_LINKS_PER_LGR_MAX];	/* smc link */
	char			peer_systemid[SMC_SYSTEMID_LEN];
						/* unique system_id of peer */
	struct rb_root		conns_all;	/* connection tree */
	rwlock_t		conns_lock;	/* protects conns_all */
	unsigned int		conns_num;	/* current # of connections */
	unsigned short		vlan_id;	/* vlan id of link group */

	struct list_head	sndbufs[SMC_RMBE_SIZES];/* tx buffers */
	rwlock_t		sndbufs_lock;	/* protects tx buffers */
	struct list_head	rmbs[SMC_RMBE_SIZES];	/* rx buffers */
	rwlock_t		rmbs_lock;	/* protects rx buffers */
	struct delayed_work	free_work;	/* delayed freeing of an lgr */
	bool			sync_err;	/* lgr no longer fits to peer */
};

/* Find the connection associated with the given alert token in the link group.
 * To use rbtrees we have to implement our own search core.
 * Requires @conns_lock
 * @token	alert token to search for
 * @lgr		 link group to search in
 * Returns connection associated with token if found, NULL otherwise.
 */
static inline struct smc_connection *smc_lgr_find_conn(
	u32 token, struct smc_link_group *lgr)
{
	struct smc_connection *res = NULL;
	struct rb_node *node;

	node = lgr->conns_all.rb_node;
	while (node) {
		struct smc_connection *cur = rb_entry(node,
					struct smc_connection, alert_node);

		if (cur->alert_token_local > token) {
			node = node->rb_left;
		} else {
			if (cur->alert_token_local < token) {
				node = node->rb_right;
			} else {
				res = cur;
				break;
			}
		}
	}

	return res;
}

struct smc_sock;
struct smc_clc_msg_accept_confirm;

void smc_lgr_free(struct smc_link_group *lgr);
void smc_lgr_terminate(struct smc_link_group *lgr);
int smc_sndbuf_create(struct smc_sock *smc);
int smc_rmb_create(struct smc_sock *smc);

#endif
