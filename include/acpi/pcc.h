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
	void __iomem *shmem;
	u64 shmem_size;
	u32 latency;
	u32 max_access_rate;
	u16 min_turnaround_time;

	/* Set to true to indicate that the mailbox should manage
	 * writing the dat to the shared buffer. This differs from
	 * the case where the drivesr are writing to the buffer and
	 * using send_data only to  ring the doorbell.  If this flag
	 * is set, then the void * data parameter of send_data must
	 * point to a kernel-memory buffer formatted in accordance with
	 * the PCC specification.
	 *
	 * The active buffer management will include reading the
	 * notify_on_completion flag, and will then
	 * call mbox_chan_txdone when the acknowledgment interrupt is
	 * received.
	 */
	bool manage_writes;

	/* Optional callback that allows the driver
	 * to allocate the memory used for receiving
	 * messages.  The return value is the location
	 * inside the buffer where the mailbox should write the data.
	 */
	void *(*rx_alloc)(struct mbox_client *cl,  int size);
};

struct pcc_header {
	u32 signature;
	u32 flags;
	u32 length;
	u32 command;
};

/* Generic Communications Channel Shared Memory Region */
#define PCC_SIGNATURE			0x50434300
/* Generic Communications Channel Command Field */
#define PCC_CMD_GENERATE_DB_INTR	BIT(15)
/* Generic Communications Channel Status Field */
#define PCC_STATUS_CMD_COMPLETE		BIT(0)
#define PCC_STATUS_SCI_DOORBELL		BIT(1)
#define PCC_STATUS_ERROR		BIT(2)
#define PCC_STATUS_PLATFORM_NOTIFY	BIT(3)
/* Initiator Responder Communications Channel Flags */
#define PCC_CMD_COMPLETION_NOTIFY	BIT(0)

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
