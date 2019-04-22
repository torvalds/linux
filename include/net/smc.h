/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for the SMC module (socket related)
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */
#ifndef _SMC_H
#define _SMC_H

#define SMC_MAX_PNETID_LEN	16	/* Max. length of PNET id */

struct smc_hashinfo {
	rwlock_t lock;
	struct hlist_head ht;
};

int smc_hash_sk(struct sock *sk);
void smc_unhash_sk(struct sock *sk);

/* SMCD/ISM device driver interface */
struct smcd_dmb {
	u64 dmb_tok;
	u64 rgid;
	u32 dmb_len;
	u32 sba_idx;
	u32 vlan_valid;
	u32 vlan_id;
	void *cpu_addr;
	dma_addr_t dma_addr;
};

#define ISM_EVENT_DMB	0
#define ISM_EVENT_GID	1
#define ISM_EVENT_SWR	2

struct smcd_event {
	u32 type;
	u32 code;
	u64 tok;
	u64 time;
	u64 info;
};

struct smcd_dev;

struct smcd_ops {
	int (*query_remote_gid)(struct smcd_dev *dev, u64 rgid, u32 vid_valid,
				u32 vid);
	int (*register_dmb)(struct smcd_dev *dev, struct smcd_dmb *dmb);
	int (*unregister_dmb)(struct smcd_dev *dev, struct smcd_dmb *dmb);
	int (*add_vlan_id)(struct smcd_dev *dev, u64 vlan_id);
	int (*del_vlan_id)(struct smcd_dev *dev, u64 vlan_id);
	int (*set_vlan_required)(struct smcd_dev *dev);
	int (*reset_vlan_required)(struct smcd_dev *dev);
	int (*signal_event)(struct smcd_dev *dev, u64 rgid, u32 trigger_irq,
			    u32 event_code, u64 info);
	int (*move_data)(struct smcd_dev *dev, u64 dmb_tok, unsigned int idx,
			 bool sf, unsigned int offset, void *data,
			 unsigned int size);
};

struct smcd_dev {
	const struct smcd_ops *ops;
	struct device dev;
	void *priv;
	u64 local_gid;
	struct list_head list;
	spinlock_t lock;
	struct smc_connection **conn;
	struct list_head vlan;
	struct workqueue_struct *event_wq;
	u8 pnetid[SMC_MAX_PNETID_LEN];
	bool pnetid_by_user;
};

struct smcd_dev *smcd_alloc_dev(struct device *parent, const char *name,
				const struct smcd_ops *ops, int max_dmbs);
int smcd_register_dev(struct smcd_dev *smcd);
void smcd_unregister_dev(struct smcd_dev *smcd);
void smcd_free_dev(struct smcd_dev *smcd);
void smcd_handle_event(struct smcd_dev *dev, struct smcd_event *event);
void smcd_handle_irq(struct smcd_dev *dev, unsigned int bit);
#endif	/* _SMC_H */
