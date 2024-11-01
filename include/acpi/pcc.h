/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCC (Platform Communications Channel) methods
 */

#ifndef _PCC_H
#define _PCC_H

#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>

struct pcc_mbox_chan {
	struct mbox_chan *mchan;
	u64 shmem_base_addr;
	u64 shmem_size;
	u32 latency;
	u32 max_access_rate;
	u16 min_turnaround_time;
};

#define MAX_PCC_SUBSPACES	256
#ifdef CONFIG_PCC
extern struct pcc_mbox_chan *
pcc_mbox_request_channel(struct mbox_client *cl, int subspace_id);
extern void pcc_mbox_free_channel(struct pcc_mbox_chan *chan);
#else
static inline struct pcc_mbox_chan *
pcc_mbox_request_channel(struct mbox_client *cl, int subspace_id)
{
	return ERR_PTR(-ENODEV);
}
static inline void pcc_mbox_free_channel(struct pcc_mbox_chan *chan) { }
#endif

#endif /* _PCC_H */
