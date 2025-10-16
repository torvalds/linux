/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BLK_INTEGRITY_H
#define _LINUX_BLK_INTEGRITY_H

#include <linux/blk-mq.h>
#include <linux/bio-integrity.h>
#include <linux/blk-mq-dma.h>

struct request;

enum blk_integrity_flags {
	BLK_INTEGRITY_NOVERIFY		= 1 << 0,
	BLK_INTEGRITY_NOGENERATE	= 1 << 1,
	BLK_INTEGRITY_DEVICE_CAPABLE	= 1 << 2,
	BLK_INTEGRITY_REF_TAG		= 1 << 3,
	BLK_INTEGRITY_STACKED		= 1 << 4,
};

const char *blk_integrity_profile_name(struct blk_integrity *bi);
bool queue_limits_stack_integrity(struct queue_limits *t,
		struct queue_limits *b);
static inline bool queue_limits_stack_integrity_bdev(struct queue_limits *t,
		struct block_device *bdev)
{
	return queue_limits_stack_integrity(t, &bdev->bd_disk->queue->limits);
}

#ifdef CONFIG_BLK_DEV_INTEGRITY
int blk_rq_map_integrity_sg(struct request *, struct scatterlist *);

static inline bool blk_rq_integrity_dma_unmap(struct request *req,
		struct device *dma_dev, struct dma_iova_state *state,
		size_t mapped_len)
{
	return blk_dma_unmap(req, dma_dev, state, mapped_len,
			bio_integrity(req->bio)->bip_flags & BIP_P2P_DMA);
}

int blk_rq_count_integrity_sg(struct request_queue *, struct bio *);
int blk_rq_integrity_map_user(struct request *rq, void __user *ubuf,
			      ssize_t bytes);
int blk_get_meta_cap(struct block_device *bdev, unsigned int cmd,
		     struct logical_block_metadata_cap __user *argp);
bool blk_rq_integrity_dma_map_iter_start(struct request *req,
		struct device *dma_dev,  struct dma_iova_state *state,
		struct blk_dma_iter *iter);
bool blk_rq_integrity_dma_map_iter_next(struct request *req,
		struct device *dma_dev, struct blk_dma_iter *iter);

static inline bool
blk_integrity_queue_supports_integrity(struct request_queue *q)
{
	return q->limits.integrity.metadata_size;
}

static inline struct blk_integrity *blk_get_integrity(struct gendisk *disk)
{
	if (!blk_integrity_queue_supports_integrity(disk->queue))
		return NULL;
	return &disk->queue->limits.integrity;
}

static inline struct blk_integrity *
bdev_get_integrity(struct block_device *bdev)
{
	return blk_get_integrity(bdev->bd_disk);
}

static inline unsigned short
queue_max_integrity_segments(const struct request_queue *q)
{
	return q->limits.max_integrity_segments;
}

/**
 * bio_integrity_intervals - Return number of integrity intervals for a bio
 * @bi:		blk_integrity profile for device
 * @sectors:	Size of the bio in 512-byte sectors
 *
 * Description: The block layer calculates everything in 512 byte
 * sectors but integrity metadata is done in terms of the data integrity
 * interval size of the storage device.  Convert the block layer sectors
 * to the appropriate number of integrity intervals.
 */
static inline unsigned int bio_integrity_intervals(struct blk_integrity *bi,
						   unsigned int sectors)
{
	return sectors >> (bi->interval_exp - 9);
}

static inline unsigned int bio_integrity_bytes(struct blk_integrity *bi,
					       unsigned int sectors)
{
	return bio_integrity_intervals(bi, sectors) * bi->metadata_size;
}

static inline bool blk_integrity_rq(struct request *rq)
{
	return rq->cmd_flags & REQ_INTEGRITY;
}

/*
 * Return the current bvec that contains the integrity data. bip_iter may be
 * advanced to iterate over the integrity data.
 */
static inline struct bio_vec rq_integrity_vec(struct request *rq)
{
	return mp_bvec_iter_bvec(rq->bio->bi_integrity->bip_vec,
				 rq->bio->bi_integrity->bip_iter);
}
#else /* CONFIG_BLK_DEV_INTEGRITY */
static inline int blk_get_meta_cap(struct block_device *bdev, unsigned int cmd,
				   struct logical_block_metadata_cap __user *argp)
{
	return -ENOIOCTLCMD;
}
static inline int blk_rq_count_integrity_sg(struct request_queue *q,
					    struct bio *b)
{
	return 0;
}
static inline int blk_rq_map_integrity_sg(struct request *q,
					  struct scatterlist *s)
{
	return 0;
}
static inline bool blk_rq_integrity_dma_unmap(struct request *req,
		struct device *dma_dev, struct dma_iova_state *state,
		size_t mapped_len)
{
	return false;
}
static inline int blk_rq_integrity_map_user(struct request *rq,
					    void __user *ubuf,
					    ssize_t bytes)
{
	return -EINVAL;
}
static inline bool blk_rq_integrity_dma_map_iter_start(struct request *req,
		struct device *dma_dev,  struct dma_iova_state *state,
		struct blk_dma_iter *iter)
{
	return false;
}
static inline bool blk_rq_integrity_dma_map_iter_next(struct request *req,
		struct device *dma_dev, struct blk_dma_iter *iter)
{
	return false;
}
static inline struct blk_integrity *bdev_get_integrity(struct block_device *b)
{
	return NULL;
}
static inline struct blk_integrity *blk_get_integrity(struct gendisk *disk)
{
	return NULL;
}
static inline bool
blk_integrity_queue_supports_integrity(struct request_queue *q)
{
	return false;
}
static inline unsigned short
queue_max_integrity_segments(const struct request_queue *q)
{
	return 0;
}

static inline unsigned int bio_integrity_intervals(struct blk_integrity *bi,
						   unsigned int sectors)
{
	return 0;
}

static inline unsigned int bio_integrity_bytes(struct blk_integrity *bi,
					       unsigned int sectors)
{
	return 0;
}
static inline int blk_integrity_rq(struct request *rq)
{
	return 0;
}

static inline struct bio_vec rq_integrity_vec(struct request *rq)
{
	/* the optimizer will remove all calls to this function */
	return (struct bio_vec){ };
}
#endif /* CONFIG_BLK_DEV_INTEGRITY */

#endif /* _LINUX_BLK_INTEGRITY_H */
