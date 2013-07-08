/*
 * Definitions for the NVM Express interface
 * Copyright (c) 2011-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _LINUX_NVME_H
#define _LINUX_NVME_H

#include <uapi/linux/nvme.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/kref.h>

struct nvme_bar {
	__u64			cap;	/* Controller Capabilities */
	__u32			vs;	/* Version */
	__u32			intms;	/* Interrupt Mask Set */
	__u32			intmc;	/* Interrupt Mask Clear */
	__u32			cc;	/* Controller Configuration */
	__u32			rsvd1;	/* Reserved */
	__u32			csts;	/* Controller Status */
	__u32			rsvd2;	/* Reserved */
	__u32			aqa;	/* Admin Queue Attributes */
	__u64			asq;	/* Admin SQ Base Address */
	__u64			acq;	/* Admin CQ Base Address */
};

#define NVME_CAP_MQES(cap)	((cap) & 0xffff)
#define NVME_CAP_TIMEOUT(cap)	(((cap) >> 24) & 0xff)
#define NVME_CAP_STRIDE(cap)	(((cap) >> 32) & 0xf)
#define NVME_CAP_MPSMIN(cap)	(((cap) >> 48) & 0xf)

enum {
	NVME_CC_ENABLE		= 1 << 0,
	NVME_CC_CSS_NVM		= 0 << 4,
	NVME_CC_MPS_SHIFT	= 7,
	NVME_CC_ARB_RR		= 0 << 11,
	NVME_CC_ARB_WRRU	= 1 << 11,
	NVME_CC_ARB_VS		= 7 << 11,
	NVME_CC_SHN_NONE	= 0 << 14,
	NVME_CC_SHN_NORMAL	= 1 << 14,
	NVME_CC_SHN_ABRUPT	= 2 << 14,
	NVME_CC_IOSQES		= 6 << 16,
	NVME_CC_IOCQES		= 4 << 20,
	NVME_CSTS_RDY		= 1 << 0,
	NVME_CSTS_CFS		= 1 << 1,
	NVME_CSTS_SHST_NORMAL	= 0 << 2,
	NVME_CSTS_SHST_OCCUR	= 1 << 2,
	NVME_CSTS_SHST_CMPLT	= 2 << 2,
};

#define NVME_VS(major, minor)	(major << 16 | minor)

#define NVME_IO_TIMEOUT	(5 * HZ)

/*
 * Represents an NVM Express device.  Each nvme_dev is a PCI function.
 */
struct nvme_dev {
	struct list_head node;
	struct nvme_queue **queues;
	u32 __iomem *dbs;
	struct pci_dev *pci_dev;
	struct dma_pool *prp_page_pool;
	struct dma_pool *prp_small_pool;
	int instance;
	int queue_count;
	int db_stride;
	u32 ctrl_config;
	struct msix_entry *entry;
	struct nvme_bar __iomem *bar;
	struct list_head namespaces;
	struct kref kref;
	struct miscdevice miscdev;
	char name[12];
	char serial[20];
	char model[40];
	char firmware_rev[8];
	u32 max_hw_sectors;
	u32 stripe_size;
	u16 oncs;
};

/*
 * An NVM Express namespace is equivalent to a SCSI LUN
 */
struct nvme_ns {
	struct list_head list;

	struct nvme_dev *dev;
	struct request_queue *queue;
	struct gendisk *disk;

	unsigned ns_id;
	int lba_shift;
	int ms;
	u64 mode_select_num_blocks;
	u32 mode_select_block_len;
};

/*
 * The nvme_iod describes the data in an I/O, including the list of PRP
 * entries.  You can't see it in this data structure because C doesn't let
 * me express that.  Use nvme_alloc_iod to ensure there's enough space
 * allocated to store the PRP list.
 */
struct nvme_iod {
	void *private;		/* For the use of the submitter of the I/O */
	int npages;		/* In the PRP list. 0 means small pool in use */
	int offset;		/* Of PRP list */
	int nents;		/* Used in scatterlist */
	int length;		/* Of data, in bytes */
	unsigned long start_time;
	dma_addr_t first_dma;
	struct scatterlist sg[0];
};

static inline u64 nvme_block_nr(struct nvme_ns *ns, sector_t sector)
{
	return (sector >> (ns->lba_shift - 9));
}

/**
 * nvme_free_iod - frees an nvme_iod
 * @dev: The device that the I/O was submitted to
 * @iod: The memory to free
 */
void nvme_free_iod(struct nvme_dev *dev, struct nvme_iod *iod);

int nvme_setup_prps(struct nvme_dev *dev, struct nvme_common_command *cmd,
			struct nvme_iod *iod, int total_len, gfp_t gfp);
struct nvme_iod *nvme_map_user_pages(struct nvme_dev *dev, int write,
				unsigned long addr, unsigned length);
void nvme_unmap_user_pages(struct nvme_dev *dev, int write,
			struct nvme_iod *iod);
struct nvme_queue *get_nvmeq(struct nvme_dev *dev);
void put_nvmeq(struct nvme_queue *nvmeq);
int nvme_submit_sync_cmd(struct nvme_queue *nvmeq, struct nvme_command *cmd,
						u32 *result, unsigned timeout);
int nvme_submit_flush_data(struct nvme_queue *nvmeq, struct nvme_ns *ns);
int nvme_submit_admin_cmd(struct nvme_dev *, struct nvme_command *,
							u32 *result);
int nvme_identify(struct nvme_dev *, unsigned nsid, unsigned cns,
							dma_addr_t dma_addr);
int nvme_get_features(struct nvme_dev *dev, unsigned fid, unsigned nsid,
			dma_addr_t dma_addr, u32 *result);
int nvme_set_features(struct nvme_dev *dev, unsigned fid, unsigned dword11,
			dma_addr_t dma_addr, u32 *result);

struct sg_io_hdr;

int nvme_sg_io(struct nvme_ns *ns, struct sg_io_hdr __user *u_hdr);
int nvme_sg_get_version_num(int __user *ip);

#endif /* _LINUX_NVME_H */
