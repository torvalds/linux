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

/* Unless we gain unexpected popularity, this limit should hold for a while */
#define MAX_CLIENTS		8
#define ISM_NR_DMBS		1920

struct ism_dev {
	spinlock_t lock; /* protects the ism device */
	spinlock_t cmd_lock; /* serializes cmds */
	struct list_head list;
	struct dibs_dev *dibs;
	struct pci_dev *pdev;

	struct ism_sba *sba;
	dma_addr_t sba_dma_addr;
	DECLARE_BITMAP(sba_bitmap, ISM_NR_DMBS);
	void *priv[MAX_CLIENTS];

	struct ism_eq *ieq;
	dma_addr_t ieq_dma_addr;

	int ieq_idx;

	struct ism_client *subs[MAX_CLIENTS];
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
	void (*handle_event)(struct ism_dev *dev, struct ism_event *event);
	/* Private area - don't touch! */
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

const struct smcd_ops *ism_get_smcd_ops(void);

#endif	/* _ISM_H */
