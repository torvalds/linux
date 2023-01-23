/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Internal Shared Memory
 *
 *  Definitions for the ISM module
 *
 *  Copyright IBM Corp. 2022
 */
#ifndef _ISM_H
#define _ISM_H

#include <linux/workqueue.h>

struct ism_dmb {
	u64 dmb_tok;
	u64 rgid;
	u32 dmb_len;
	u32 sba_idx;
	u32 vlan_valid;
	u32 vlan_id;
	void *cpu_addr;
	dma_addr_t dma_addr;
};

/* Unless we gain unexpected popularity, this limit should hold for a while */
#define MAX_CLIENTS		8
#define ISM_NR_DMBS		1920

struct ism_dev {
	spinlock_t lock; /* protects the ism device */
	struct list_head list;
	struct pci_dev *pdev;
	struct smcd_dev *smcd;

	struct ism_sba *sba;
	dma_addr_t sba_dma_addr;
	DECLARE_BITMAP(sba_bitmap, ISM_NR_DMBS);
	u8 *sba_client_arr;	/* entries are indices into 'clients' array */
	void *priv[MAX_CLIENTS];

	struct ism_eq *ieq;
	dma_addr_t ieq_dma_addr;

	struct device dev;
	u64 local_gid;
	int ieq_idx;

	atomic_t free_clients_cnt;
	atomic_t add_dev_cnt;
	wait_queue_head_t waitq;
};

struct ism_event {
	u32 type;
	u32 code;
	u64 tok;
	u64 time;
	u64 info;
};

struct ism_client {
	const char *name;
	void (*add)(struct ism_dev *dev);
	void (*remove)(struct ism_dev *dev);
	void (*handle_event)(struct ism_dev *dev, struct ism_event *event);
	/* Parameter dmbemask contains a bit vector with updated DMBEs, if sent
	 * via ism_move_data(). Callback function must handle all active bits
	 * indicated by dmbemask.
	 */
	void (*handle_irq)(struct ism_dev *dev, unsigned int bit, u16 dmbemask);
	/* Private area - don't touch! */
	struct work_struct remove_work;
	struct work_struct add_work;
	struct ism_dev *tgt_ism;
	u8 id;
};

int ism_register_client(struct ism_client *client);
int  ism_unregister_client(struct ism_client *client);
static inline void *ism_get_priv(struct ism_dev *dev,
				 struct ism_client *client) {
	return dev->priv[client->id];
}

static inline void ism_set_priv(struct ism_dev *dev, struct ism_client *client,
				void *priv) {
	dev->priv[client->id] = priv;
}

int  ism_register_dmb(struct ism_dev *dev, struct ism_dmb *dmb,
		      struct ism_client *client);
int  ism_unregister_dmb(struct ism_dev *dev, struct ism_dmb *dmb);
int  ism_move(struct ism_dev *dev, u64 dmb_tok, unsigned int idx, bool sf,
	      unsigned int offset, void *data, unsigned int size);
u8  *ism_get_seid(void);

#endif	/* _ISM_H */
