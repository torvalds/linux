/* SPDX-License-Identifier: GPL-2.0 */
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

#include <linux/atomic.h>
#include <linux/smc.h>
#include <linux/pci.h>
#include <rdma/ib_verbs.h>
#include <net/genetlink.h>

#include "smc.h"
#include "smc_ib.h"

#define SMC_RMBS_PER_LGR_MAX	255	/* max. # of RMBs per link group */

struct smc_lgr_list {			/* list of link group definition */
	struct list_head	list;
	spinlock_t		lock;	/* protects list of link groups */
	u32			num;	/* unique link group number */
};

enum smc_lgr_role {		/* possible roles of a link group */
	SMC_CLNT,	/* client */
	SMC_SERV	/* server */
};

enum smc_link_state {			/* possible states of a link */
	SMC_LNK_UNUSED,		/* link is unused */
	SMC_LNK_INACTIVE,	/* link is inactive */
	SMC_LNK_ACTIVATING,	/* link is being activated */
	SMC_LNK_ACTIVE,		/* link is active */
};

#define SMC_WR_BUF_SIZE		48	/* size of work request buffer */
#define SMC_WR_BUF_V2_SIZE	8192	/* size of v2 work request buffer */

struct smc_wr_buf {
	u8	raw[SMC_WR_BUF_SIZE];
};

struct smc_wr_v2_buf {
	u8	raw[SMC_WR_BUF_V2_SIZE];
};

#define SMC_WR_REG_MR_WAIT_TIME	(5 * HZ)/* wait time for ib_wr_reg_mr result */

enum smc_wr_reg_state {
	POSTED,		/* ib_wr_reg_mr request posted */
	CONFIRMED,	/* ib_wr_reg_mr response: successful */
	FAILED		/* ib_wr_reg_mr response: failure */
};

struct smc_rdma_sge {				/* sges for RDMA writes */
	struct ib_sge		wr_tx_rdma_sge[SMC_IB_MAX_SEND_SGE];
};

#define SMC_MAX_RDMA_WRITES	2		/* max. # of RDMA writes per
						 * message send
						 */

struct smc_rdma_sges {				/* sges per message send */
	struct smc_rdma_sge	tx_rdma_sge[SMC_MAX_RDMA_WRITES];
};

struct smc_rdma_wr {				/* work requests per message
						 * send
						 */
	struct ib_rdma_wr	wr_tx_rdma[SMC_MAX_RDMA_WRITES];
};

#define SMC_LGR_ID_SIZE		4

struct smc_link {
	struct smc_ib_device	*smcibdev;	/* ib-device */
	u8			ibport;		/* port - values 1 | 2 */
	struct ib_pd		*roce_pd;	/* IB protection domain,
						 * unique for every RoCE QP
						 */
	struct ib_qp		*roce_qp;	/* IB queue pair */
	struct ib_qp_attr	qp_attr;	/* IB queue pair attributes */

	struct smc_wr_buf	*wr_tx_bufs;	/* WR send payload buffers */
	struct ib_send_wr	*wr_tx_ibs;	/* WR send meta data */
	struct ib_sge		*wr_tx_sges;	/* WR send gather meta data */
	struct smc_rdma_sges	*wr_tx_rdma_sges;/*RDMA WRITE gather meta data*/
	struct smc_rdma_wr	*wr_tx_rdmas;	/* WR RDMA WRITE */
	struct smc_wr_tx_pend	*wr_tx_pends;	/* WR send waiting for CQE */
	struct completion	*wr_tx_compl;	/* WR send CQE completion */
	/* above four vectors have wr_tx_cnt elements and use the same index */
	struct ib_send_wr	*wr_tx_v2_ib;	/* WR send v2 meta data */
	struct ib_sge		*wr_tx_v2_sge;	/* WR send v2 gather meta data*/
	struct smc_wr_tx_pend	*wr_tx_v2_pend;	/* WR send v2 waiting for CQE */
	dma_addr_t		wr_tx_dma_addr;	/* DMA address of wr_tx_bufs */
	dma_addr_t		wr_tx_v2_dma_addr; /* DMA address of v2 tx buf*/
	atomic_long_t		wr_tx_id;	/* seq # of last sent WR */
	unsigned long		*wr_tx_mask;	/* bit mask of used indexes */
	u32			wr_tx_cnt;	/* number of WR send buffers */
	wait_queue_head_t	wr_tx_wait;	/* wait for free WR send buf */
	struct {
		struct percpu_ref	wr_tx_refs;
	} ____cacheline_aligned_in_smp;
	struct completion	tx_ref_comp;

	struct smc_wr_buf	*wr_rx_bufs;	/* WR recv payload buffers */
	struct ib_recv_wr	*wr_rx_ibs;	/* WR recv meta data */
	struct ib_sge		*wr_rx_sges;	/* WR recv scatter meta data */
	/* above three vectors have wr_rx_cnt elements and use the same index */
	dma_addr_t		wr_rx_dma_addr;	/* DMA address of wr_rx_bufs */
	dma_addr_t		wr_rx_v2_dma_addr; /* DMA address of v2 rx buf*/
	u64			wr_rx_id;	/* seq # of last recv WR */
	u64			wr_rx_id_compl; /* seq # of last completed WR */
	u32			wr_rx_cnt;	/* number of WR recv buffers */
	unsigned long		wr_rx_tstamp;	/* jiffies when last buf rx */
	wait_queue_head_t       wr_rx_empty_wait; /* wait for RQ empty */

	struct ib_reg_wr	wr_reg;		/* WR register memory region */
	wait_queue_head_t	wr_reg_wait;	/* wait for wr_reg result */
	struct {
		struct percpu_ref	wr_reg_refs;
	} ____cacheline_aligned_in_smp;
	struct completion	reg_ref_comp;
	enum smc_wr_reg_state	wr_reg_state;	/* state of wr_reg request */

	u8			gid[SMC_GID_SIZE];/* gid matching used vlan id*/
	u8			sgid_index;	/* gid index for vlan id      */
	u32			peer_qpn;	/* QP number of peer */
	enum ib_mtu		path_mtu;	/* used mtu */
	enum ib_mtu		peer_mtu;	/* mtu size of peer */
	u32			psn_initial;	/* QP tx initial packet seqno */
	u32			peer_psn;	/* QP rx initial packet seqno */
	u8			peer_mac[ETH_ALEN];	/* = gid[8:10||13:15] */
	u8			peer_gid[SMC_GID_SIZE];	/* gid of peer*/
	u8			link_id;	/* unique # within link group */
	u8			link_uid[SMC_LGR_ID_SIZE]; /* unique lnk id */
	u8			peer_link_uid[SMC_LGR_ID_SIZE]; /* peer uid */
	u8			link_idx;	/* index in lgr link array */
	u8			link_is_asym;	/* is link asymmetric? */
	u8			clearing : 1;	/* link is being cleared */
	refcount_t		refcnt;		/* link reference count */
	struct smc_link_group	*lgr;		/* parent link group */
	struct work_struct	link_down_wrk;	/* wrk to bring link down */
	char			ibname[IB_DEVICE_NAME_MAX]; /* ib device name */
	int			ndev_ifidx; /* network device ifindex */

	enum smc_link_state	state;		/* state of link */
	struct delayed_work	llc_testlink_wrk; /* testlink worker */
	struct completion	llc_testlink_resp; /* wait for rx of testlink */
	int			llc_testlink_time; /* testlink interval */
	atomic_t		conn_cnt; /* connections on this link */
};

/* For now we just allow one parallel link per link group. The SMC protocol
 * allows more (up to 8).
 */
#define SMC_LINKS_PER_LGR_MAX	3
#define SMC_SINGLE_LINK		0

/* tx/rx buffer list element for sndbufs list and rmbs list of a lgr */
struct smc_buf_desc {
	struct list_head	list;
	void			*cpu_addr;	/* virtual address of buffer */
	struct page		*pages;
	int			len;		/* length of buffer */
	u32			used;		/* currently used / unused */
	union {
		struct { /* SMC-R */
			struct sg_table	sgt[SMC_LINKS_PER_LGR_MAX];
					/* virtual buffer */
			struct ib_mr	*mr[SMC_LINKS_PER_LGR_MAX];
					/* memory region: for rmb and
					 * vzalloced sndbuf
					 * incl. rkey provided to peer
					 * and lkey provided to local
					 */
			u32		order;	/* allocation order */

			u8		is_conf_rkey;
					/* confirm_rkey done */
			u8		is_reg_mr[SMC_LINKS_PER_LGR_MAX];
					/* mem region registered */
			u8		is_map_ib[SMC_LINKS_PER_LGR_MAX];
					/* mem region mapped to lnk */
			u8		is_dma_need_sync;
			u8		is_reg_err;
					/* buffer registration err */
			u8		is_vm;
					/* virtually contiguous */
		};
		struct { /* SMC-D */
			unsigned short	sba_idx;
					/* SBA index number */
			u64		token;
					/* DMB token number */
			dma_addr_t	dma_addr;
					/* DMA address */
		};
	};
};

struct smc_rtoken {				/* address/key of remote RMB */
	u64			dma_addr;
	u32			rkey;
};

#define SMC_BUF_MIN_SIZE	16384	/* minimum size of an RMB */
#define SMC_RMBE_SIZES		16	/* number of distinct RMBE sizes */
/* theoretically, the RFC states that largest size would be 512K,
 * i.e. compressed 5 and thus 6 sizes (0..5), despite
 * struct smc_clc_msg_accept_confirm.rmbe_size being a 4 bit value (0..15)
 */

struct smcd_dev;

enum smc_lgr_type {				/* redundancy state of lgr */
	SMC_LGR_NONE,			/* no active links, lgr to be deleted */
	SMC_LGR_SINGLE,			/* 1 active RNIC on each peer */
	SMC_LGR_SYMMETRIC,		/* 2 active RNICs on each peer */
	SMC_LGR_ASYMMETRIC_PEER,	/* local has 2, peer 1 active RNICs */
	SMC_LGR_ASYMMETRIC_LOCAL,	/* local has 1, peer 2 active RNICs */
};

enum smcr_buf_type {		/* types of SMC-R sndbufs and RMBs */
	SMCR_PHYS_CONT_BUFS	= 0,
	SMCR_VIRT_CONT_BUFS	= 1,
	SMCR_MIXED_BUFS		= 2,
};

enum smc_llc_flowtype {
	SMC_LLC_FLOW_NONE	= 0,
	SMC_LLC_FLOW_ADD_LINK	= 2,
	SMC_LLC_FLOW_DEL_LINK	= 4,
	SMC_LLC_FLOW_REQ_ADD_LINK = 5,
	SMC_LLC_FLOW_RKEY	= 6,
};

struct smc_llc_qentry;

struct smc_llc_flow {
	enum smc_llc_flowtype type;
	struct smc_llc_qentry *qentry;
};

struct smc_link_group {
	struct list_head	list;
	struct rb_root		conns_all;	/* connection tree */
	rwlock_t		conns_lock;	/* protects conns_all */
	unsigned int		conns_num;	/* current # of connections */
	unsigned short		vlan_id;	/* vlan id of link group */

	struct list_head	sndbufs[SMC_RMBE_SIZES];/* tx buffers */
	struct rw_semaphore	sndbufs_lock;	/* protects tx buffers */
	struct list_head	rmbs[SMC_RMBE_SIZES];	/* rx buffers */
	struct rw_semaphore	rmbs_lock;	/* protects rx buffers */

	u8			id[SMC_LGR_ID_SIZE];	/* unique lgr id */
	struct delayed_work	free_work;	/* delayed freeing of an lgr */
	struct work_struct	terminate_work;	/* abnormal lgr termination */
	struct workqueue_struct	*tx_wq;		/* wq for conn. tx workers */
	u8			sync_err : 1;	/* lgr no longer fits to peer */
	u8			terminating : 1;/* lgr is terminating */
	u8			freeing : 1;	/* lgr is being freed */

	refcount_t		refcnt;		/* lgr reference count */
	bool			is_smcd;	/* SMC-R or SMC-D */
	u8			smc_version;
	u8			negotiated_eid[SMC_MAX_EID_LEN];
	u8			peer_os;	/* peer operating system */
	u8			peer_smc_release;
	u8			peer_hostname[SMC_MAX_HOSTNAME_LEN];
	union {
		struct { /* SMC-R */
			enum smc_lgr_role	role;
						/* client or server */
			struct smc_link		lnk[SMC_LINKS_PER_LGR_MAX];
						/* smc link */
			struct smc_wr_v2_buf	*wr_rx_buf_v2;
						/* WR v2 recv payload buffer */
			struct smc_wr_v2_buf	*wr_tx_buf_v2;
						/* WR v2 send payload buffer */
			char			peer_systemid[SMC_SYSTEMID_LEN];
						/* unique system_id of peer */
			struct smc_rtoken	rtokens[SMC_RMBS_PER_LGR_MAX]
						[SMC_LINKS_PER_LGR_MAX];
						/* remote addr/key pairs */
			DECLARE_BITMAP(rtokens_used_mask, SMC_RMBS_PER_LGR_MAX);
						/* used rtoken elements */
			u8			next_link_id;
			enum smc_lgr_type	type;
			enum smcr_buf_type	buf_type;
						/* redundancy state */
			u8			pnet_id[SMC_MAX_PNETID_LEN + 1];
						/* pnet id of this lgr */
			struct list_head	llc_event_q;
						/* queue for llc events */
			spinlock_t		llc_event_q_lock;
						/* protects llc_event_q */
			struct rw_semaphore	llc_conf_mutex;
						/* protects lgr reconfig. */
			struct work_struct	llc_add_link_work;
			struct work_struct	llc_del_link_work;
			struct work_struct	llc_event_work;
						/* llc event worker */
			wait_queue_head_t	llc_flow_waiter;
						/* w4 next llc event */
			wait_queue_head_t	llc_msg_waiter;
						/* w4 next llc msg */
			struct smc_llc_flow	llc_flow_lcl;
						/* llc local control field */
			struct smc_llc_flow	llc_flow_rmt;
						/* llc remote control field */
			struct smc_llc_qentry	*delayed_event;
						/* arrived when flow active */
			spinlock_t		llc_flow_lock;
						/* protects llc flow */
			int			llc_testlink_time;
						/* link keep alive time */
			u32			llc_termination_rsn;
						/* rsn code for termination */
			u8			nexthop_mac[ETH_ALEN];
			u8			uses_gateway;
			__be32			saddr;
						/* net namespace */
			struct net		*net;
		};
		struct { /* SMC-D */
			u64			peer_gid;
						/* Peer GID (remote) */
			struct smcd_dev		*smcd;
						/* ISM device for VLAN reg. */
			u8			peer_shutdown : 1;
						/* peer triggered shutdownn */
		};
	};
};

struct smc_clc_msg_local;

#define GID_LIST_SIZE	2

struct smc_gidlist {
	u8			len;
	u8			list[GID_LIST_SIZE][SMC_GID_SIZE];
};

struct smc_init_info_smcrv2 {
	/* Input fields */
	__be32			saddr;
	struct sock		*clc_sk;
	__be32			daddr;

	/* Output fields when saddr is set */
	struct smc_ib_device	*ib_dev_v2;
	u8			ib_port_v2;
	u8			ib_gid_v2[SMC_GID_SIZE];

	/* Additional output fields when clc_sk and daddr is set as well */
	u8			uses_gateway;
	u8			nexthop_mac[ETH_ALEN];

	struct smc_gidlist	gidlist;
};

struct smc_init_info {
	u8			is_smcd;
	u8			smc_type_v1;
	u8			smc_type_v2;
	u8			first_contact_peer;
	u8			first_contact_local;
	unsigned short		vlan_id;
	u32			rc;
	u8			negotiated_eid[SMC_MAX_EID_LEN];
	/* SMC-R */
	u8			smcr_version;
	u8			check_smcrv2;
	u8			peer_gid[SMC_GID_SIZE];
	u8			peer_mac[ETH_ALEN];
	u8			peer_systemid[SMC_SYSTEMID_LEN];
	struct smc_ib_device	*ib_dev;
	u8			ib_gid[SMC_GID_SIZE];
	u8			ib_port;
	u32			ib_clcqpn;
	struct smc_init_info_smcrv2 smcrv2;
	/* SMC-D */
	u64			ism_peer_gid[SMC_MAX_ISM_DEVS + 1];
	struct smcd_dev		*ism_dev[SMC_MAX_ISM_DEVS + 1];
	u16			ism_chid[SMC_MAX_ISM_DEVS + 1];
	u8			ism_offered_cnt; /* # of ISM devices offered */
	u8			ism_selected;    /* index of selected ISM dev*/
	u8			smcd_version;
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

static inline bool smc_conn_lgr_valid(struct smc_connection *conn)
{
	return conn->lgr && conn->alert_token_local;
}

/*
 * Returns true if the specified link is usable.
 *
 * usable means the link is ready to receive RDMA messages, map memory
 * on the link, etc. This doesn't ensure we are able to send RDMA messages
 * on this link, if sending RDMA messages is needed, use smc_link_sendable()
 */
static inline bool smc_link_usable(struct smc_link *lnk)
{
	if (lnk->state == SMC_LNK_UNUSED || lnk->state == SMC_LNK_INACTIVE)
		return false;
	return true;
}

/*
 * Returns true if the specified link is ready to receive AND send RDMA
 * messages.
 *
 * For the client side in first contact, the underlying QP may still in
 * RESET or RTR when the link state is ACTIVATING, checks in smc_link_usable()
 * is not strong enough. For those places that need to send any CDC or LLC
 * messages, use smc_link_sendable(), otherwise, use smc_link_usable() instead
 */
static inline bool smc_link_sendable(struct smc_link *lnk)
{
	return smc_link_usable(lnk) &&
		lnk->qp_attr.cur_qp_state == IB_QPS_RTS;
}

static inline bool smc_link_active(struct smc_link *lnk)
{
	return lnk->state == SMC_LNK_ACTIVE;
}

static inline void smc_gid_be16_convert(__u8 *buf, u8 *gid_raw)
{
	sprintf(buf, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
		be16_to_cpu(((__be16 *)gid_raw)[0]),
		be16_to_cpu(((__be16 *)gid_raw)[1]),
		be16_to_cpu(((__be16 *)gid_raw)[2]),
		be16_to_cpu(((__be16 *)gid_raw)[3]),
		be16_to_cpu(((__be16 *)gid_raw)[4]),
		be16_to_cpu(((__be16 *)gid_raw)[5]),
		be16_to_cpu(((__be16 *)gid_raw)[6]),
		be16_to_cpu(((__be16 *)gid_raw)[7]));
}

struct smc_pci_dev {
	__u32		pci_fid;
	__u16		pci_pchid;
	__u16		pci_vendor;
	__u16		pci_device;
	__u8		pci_id[SMC_PCI_ID_STR_LEN];
};

static inline void smc_set_pci_values(struct pci_dev *pci_dev,
				      struct smc_pci_dev *smc_dev)
{
	smc_dev->pci_vendor = pci_dev->vendor;
	smc_dev->pci_device = pci_dev->device;
	snprintf(smc_dev->pci_id, sizeof(smc_dev->pci_id), "%s",
		 pci_name(pci_dev));
#if IS_ENABLED(CONFIG_S390)
	{ /* Set s390 specific PCI information */
	struct zpci_dev *zdev;

	zdev = to_zpci(pci_dev);
	smc_dev->pci_fid = zdev->fid;
	smc_dev->pci_pchid = zdev->pchid;
	}
#endif
}

struct smc_sock;
struct smc_clc_msg_accept_confirm;

void smc_lgr_cleanup_early(struct smc_link_group *lgr);
void smc_lgr_terminate_sched(struct smc_link_group *lgr);
void smc_lgr_hold(struct smc_link_group *lgr);
void smc_lgr_put(struct smc_link_group *lgr);
void smcr_port_add(struct smc_ib_device *smcibdev, u8 ibport);
void smcr_port_err(struct smc_ib_device *smcibdev, u8 ibport);
void smc_smcd_terminate(struct smcd_dev *dev, u64 peer_gid,
			unsigned short vlan);
void smc_smcd_terminate_all(struct smcd_dev *dev);
void smc_smcr_terminate_all(struct smc_ib_device *smcibdev);
int smc_buf_create(struct smc_sock *smc, bool is_smcd);
int smc_uncompress_bufsize(u8 compressed);
int smc_rmb_rtoken_handling(struct smc_connection *conn, struct smc_link *link,
			    struct smc_clc_msg_accept_confirm *clc);
int smc_rtoken_add(struct smc_link *lnk, __be64 nw_vaddr, __be32 nw_rkey);
int smc_rtoken_delete(struct smc_link *lnk, __be32 nw_rkey);
void smc_rtoken_set(struct smc_link_group *lgr, int link_idx, int link_idx_new,
		    __be32 nw_rkey_known, __be64 nw_vaddr, __be32 nw_rkey);
void smc_rtoken_set2(struct smc_link_group *lgr, int rtok_idx, int link_id,
		     __be64 nw_vaddr, __be32 nw_rkey);
void smc_sndbuf_sync_sg_for_device(struct smc_connection *conn);
void smc_rmb_sync_sg_for_cpu(struct smc_connection *conn);
int smc_vlan_by_tcpsk(struct socket *clcsock, struct smc_init_info *ini);

void smc_conn_free(struct smc_connection *conn);
int smc_conn_create(struct smc_sock *smc, struct smc_init_info *ini);
int smc_core_init(void);
void smc_core_exit(void);

int smcr_link_init(struct smc_link_group *lgr, struct smc_link *lnk,
		   u8 link_idx, struct smc_init_info *ini);
void smcr_link_clear(struct smc_link *lnk, bool log);
void smcr_link_hold(struct smc_link *lnk);
void smcr_link_put(struct smc_link *lnk);
void smc_switch_link_and_count(struct smc_connection *conn,
			       struct smc_link *to_lnk);
int smcr_buf_map_lgr(struct smc_link *lnk);
int smcr_buf_reg_lgr(struct smc_link *lnk);
void smcr_lgr_set_type(struct smc_link_group *lgr, enum smc_lgr_type new_type);
void smcr_lgr_set_type_asym(struct smc_link_group *lgr,
			    enum smc_lgr_type new_type, int asym_lnk_idx);
int smcr_link_reg_buf(struct smc_link *link, struct smc_buf_desc *rmb_desc);
struct smc_link *smc_switch_conns(struct smc_link_group *lgr,
				  struct smc_link *from_lnk, bool is_dev_err);
void smcr_link_down_cond(struct smc_link *lnk);
void smcr_link_down_cond_sched(struct smc_link *lnk);
int smc_nl_get_sys_info(struct sk_buff *skb, struct netlink_callback *cb);
int smcr_nl_get_lgr(struct sk_buff *skb, struct netlink_callback *cb);
int smcr_nl_get_link(struct sk_buff *skb, struct netlink_callback *cb);
int smcd_nl_get_lgr(struct sk_buff *skb, struct netlink_callback *cb);

static inline struct smc_link_group *smc_get_lgr(struct smc_link *link)
{
	return link->lgr;
}
#endif
