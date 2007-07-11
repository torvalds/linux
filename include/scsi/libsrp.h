#ifndef __LIBSRP_H__
#define __LIBSRP_H__

#include <linux/list.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/srp.h>

enum iue_flags {
	V_DIOVER,
	V_WRITE,
	V_LINKED,
	V_FLYING,
};

struct srp_buf {
	dma_addr_t dma;
	void *buf;
};

struct srp_queue {
	void *pool;
	void *items;
	struct kfifo *queue;
	spinlock_t lock;
};

struct srp_target {
	struct Scsi_Host *shost;
	struct device *dev;

	spinlock_t lock;
	struct list_head cmd_queue;

	size_t srp_iu_size;
	struct srp_queue iu_queue;
	size_t rx_ring_size;
	struct srp_buf **rx_ring;

	void *ldata;
};

struct iu_entry {
	struct srp_target *target;

	struct list_head ilist;
	dma_addr_t remote_token;
	unsigned long flags;

	struct srp_buf *sbuf;
};

typedef int (srp_rdma_t)(struct scsi_cmnd *, struct scatterlist *, int,
			 struct srp_direct_buf *, int,
			 enum dma_data_direction, unsigned int);
extern int srp_target_alloc(struct srp_target *, struct device *, size_t, size_t);
extern void srp_target_free(struct srp_target *);

extern struct iu_entry *srp_iu_get(struct srp_target *);
extern void srp_iu_put(struct iu_entry *);

extern int srp_cmd_queue(struct Scsi_Host *, struct srp_cmd *, void *, u64, u64);
extern int srp_transfer_data(struct scsi_cmnd *, struct srp_cmd *,
			     srp_rdma_t, int, int);


static inline struct srp_target *host_to_srp_target(struct Scsi_Host *host)
{
	return (struct srp_target *) host->hostdata;
}

static inline int srp_cmd_direction(struct srp_cmd *cmd)
{
	return (cmd->buf_fmt >> 4) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
}

#endif
