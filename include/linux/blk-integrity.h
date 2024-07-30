/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BLK_INTEGRITY_H
#define _LINUX_BLK_INTEGRITY_H

#include <linux/blk-mq.h>
#include <linux/bio-integrity.h>

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
int blk_rq_map_integrity_sg(struct request_queue *, struct bio *,
				   struct scatterlist *);
int blk_rq_count_integrity_sg(struct request_queue *, struct bio *);

static inline bool
blk_integrity_queue_supports_integrity(struct request_queue *q)
{
	return q->limits.integrity.tuple_size;
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
	return bio_integrity_intervals(bi, sectors) * bi->tuple_size;
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
static inline int blk_rq_count_integrity_sg(struct request_queue *q,
					    struct bio *b)
{
	return 0;
}
static inline int blk_rq_map_integrity_sg(struct request_queue *q,
					  struct bio *b,
					  struct scatterlist *s)
{
	return 0;
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
