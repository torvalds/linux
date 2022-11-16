/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Block data types and constants.  Directly include this file only to
 * break include dependency loop.
 */
#ifndef __LINUX_BLK_TYPES_H
#define __LINUX_BLK_TYPES_H

#include <linux/types.h>
#include <linux/bvec.h>
#include <linux/device.h>
#include <linux/ktime.h>

struct bio_set;
struct bio;
struct bio_integrity_payload;
struct page;
struct io_context;
struct cgroup_subsys_state;
typedef void (bio_end_io_t) (struct bio *);
struct bio_crypt_ctx;

/*
 * The basic unit of block I/O is a sector. It is used in a number of contexts
 * in Linux (blk, bio, genhd). The size of one sector is 512 = 2**9
 * bytes. Variables of type sector_t represent an offset or size that is a
 * multiple of 512 bytes. Hence these two constants.
 */
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

#define PAGE_SECTORS_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define PAGE_SECTORS		(1 << PAGE_SECTORS_SHIFT)
#define SECTOR_MASK		(PAGE_SECTORS - 1)

struct block_device {
	sector_t		bd_start_sect;
	sector_t		bd_nr_sectors;
	struct disk_stats __percpu *bd_stats;
	unsigned long		bd_stamp;
	bool			bd_read_only;	/* read-only policy */
	dev_t			bd_dev;
	atomic_t		bd_openers;
	struct inode *		bd_inode;	/* will die */
	struct super_block *	bd_super;
	void *			bd_claiming;
	struct device		bd_device;
	void *			bd_holder;
	int			bd_holders;
	bool			bd_write_holder;
	struct kobject		*bd_holder_dir;
	u8			bd_partno;
	spinlock_t		bd_size_lock; /* for bd_inode->i_size updates */
	struct gendisk *	bd_disk;
	struct request_queue *	bd_queue;

	/* The counter of freeze processes */
	int			bd_fsfreeze_count;
	/* Mutex for freeze */
	struct mutex		bd_fsfreeze_mutex;
	struct super_block	*bd_fsfreeze_sb;

	struct partition_meta_info *bd_meta_info;
#ifdef CONFIG_FAIL_MAKE_REQUEST
	bool			bd_make_it_fail;
#endif
} __randomize_layout;

#define bdev_whole(_bdev) \
	((_bdev)->bd_disk->part0)

#define dev_to_bdev(device) \
	container_of((device), struct block_device, bd_device)

#define bdev_kobj(_bdev) \
	(&((_bdev)->bd_device.kobj))

/*
 * Block error status values.  See block/blk-core:blk_errors for the details.
 * Alpha cannot write a byte atomically, so we need to use 32-bit value.
 */
#if defined(CONFIG_ALPHA) && !defined(__alpha_bwx__)
typedef u32 __bitwise blk_status_t;
typedef u32 blk_short_t;
#else
typedef u8 __bitwise blk_status_t;
typedef u16 blk_short_t;
#endif
#define	BLK_STS_OK 0
#define BLK_STS_NOTSUPP		((__force blk_status_t)1)
#define BLK_STS_TIMEOUT		((__force blk_status_t)2)
#define BLK_STS_NOSPC		((__force blk_status_t)3)
#define BLK_STS_TRANSPORT	((__force blk_status_t)4)
#define BLK_STS_TARGET		((__force blk_status_t)5)
#define BLK_STS_NEXUS		((__force blk_status_t)6)
#define BLK_STS_MEDIUM		((__force blk_status_t)7)
#define BLK_STS_PROTECTION	((__force blk_status_t)8)
#define BLK_STS_RESOURCE	((__force blk_status_t)9)
#define BLK_STS_IOERR		((__force blk_status_t)10)

/* hack for device mapper, don't use elsewhere: */
#define BLK_STS_DM_REQUEUE    ((__force blk_status_t)11)

/*
 * BLK_STS_AGAIN should only be returned if RQF_NOWAIT is set
 * and the bio would block (cf bio_wouldblock_error())
 */
#define BLK_STS_AGAIN		((__force blk_status_t)12)

/*
 * BLK_STS_DEV_RESOURCE is returned from the driver to the block layer if
 * device related resources are unavailable, but the driver can guarantee
 * that the queue will be rerun in the future once resources become
 * available again. This is typically the case for device specific
 * resources that are consumed for IO. If the driver fails allocating these
 * resources, we know that inflight (or pending) IO will free these
 * resource upon completion.
 *
 * This is different from BLK_STS_RESOURCE in that it explicitly references
 * a device specific resource. For resources of wider scope, allocation
 * failure can happen without having pending IO. This means that we can't
 * rely on request completions freeing these resources, as IO may not be in
 * flight. Examples of that are kernel memory allocations, DMA mappings, or
 * any other system wide resources.
 */
#define BLK_STS_DEV_RESOURCE	((__force blk_status_t)13)

/*
 * BLK_STS_ZONE_RESOURCE is returned from the driver to the block layer if zone
 * related resources are unavailable, but the driver can guarantee the queue
 * will be rerun in the future once the resources become available again.
 *
 * This is different from BLK_STS_DEV_RESOURCE in that it explicitly references
 * a zone specific resource and IO to a different zone on the same device could
 * still be served. Examples of that are zones that are write-locked, but a read
 * to the same zone could be served.
 */
#define BLK_STS_ZONE_RESOURCE	((__force blk_status_t)14)

/*
 * BLK_STS_ZONE_OPEN_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently open. The same command should be successful if resubmitted
 * after the number of open zones decreases below the device's limits, which is
 * reported in the request_queue's max_open_zones.
 */
#define BLK_STS_ZONE_OPEN_RESOURCE	((__force blk_status_t)15)

/*
 * BLK_STS_ZONE_ACTIVE_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently active. The same command should be successful if resubmitted
 * after the number of active zones decreases below the device's limits, which
 * is reported in the request_queue's max_active_zones.
 */
#define BLK_STS_ZONE_ACTIVE_RESOURCE	((__force blk_status_t)16)

/*
 * BLK_STS_OFFLINE is returned from the driver when the target device is offline
 * or is being taken offline. This could help differentiate the case where a
 * device is intentionally being shut down from a real I/O error.
 */
#define BLK_STS_OFFLINE		((__force blk_status_t)17)

/**
 * blk_path_error - returns true if error may be path related
 * @error: status the request was completed with
 *
 * Description:
 *     This classifies block error status into non-retryable errors and ones
 *     that may be successful if retried on a failover path.
 *
 * Return:
 *     %false - retrying failover path will not help
 *     %true  - may succeed if retried
 */
static inline bool blk_path_error(blk_status_t error)
{
	switch (error) {
	case BLK_STS_NOTSUPP:
	case BLK_STS_NOSPC:
	case BLK_STS_TARGET:
	case BLK_STS_NEXUS:
	case BLK_STS_MEDIUM:
	case BLK_STS_PROTECTION:
		return false;
	}

	/* Anything else could be a path failure, so should be retried */
	return true;
}

/*
 * From most significant bit:
 * 1 bit: reserved for other usage, see below
 * 12 bits: original size of bio
 * 51 bits: issue time of bio
 */
#define BIO_ISSUE_RES_BITS      1
#define BIO_ISSUE_SIZE_BITS     12
#define BIO_ISSUE_RES_SHIFT     (64 - BIO_ISSUE_RES_BITS)
#define BIO_ISSUE_SIZE_SHIFT    (BIO_ISSUE_RES_SHIFT - BIO_ISSUE_SIZE_BITS)
#define BIO_ISSUE_TIME_MASK     ((1ULL << BIO_ISSUE_SIZE_SHIFT) - 1)
#define BIO_ISSUE_SIZE_MASK     \
	(((1ULL << BIO_ISSUE_SIZE_BITS) - 1) << BIO_ISSUE_SIZE_SHIFT)
#define BIO_ISSUE_RES_MASK      (~((1ULL << BIO_ISSUE_RES_SHIFT) - 1))

/* Reserved bit for blk-throtl */
#define BIO_ISSUE_THROTL_SKIP_LATENCY (1ULL << 63)

struct bio_issue {
	u64 value;
};

static inline u64 __bio_issue_time(u64 time)
{
	return time & BIO_ISSUE_TIME_MASK;
}

static inline u64 bio_issue_time(struct bio_issue *issue)
{
	return __bio_issue_time(issue->value);
}

static inline sector_t bio_issue_size(struct bio_issue *issue)
{
	return ((issue->value & BIO_ISSUE_SIZE_MASK) >> BIO_ISSUE_SIZE_SHIFT);
}

static inline void bio_issue_init(struct bio_issue *issue,
				       sector_t size)
{
	size &= (1ULL << BIO_ISSUE_SIZE_BITS) - 1;
	issue->value = ((issue->value & BIO_ISSUE_RES_MASK) |
			(ktime_get_ns() & BIO_ISSUE_TIME_MASK) |
			((u64)size << BIO_ISSUE_SIZE_SHIFT));
}

typedef __u32 __bitwise blk_opf_t;

typedef unsigned int blk_qc_t;
#define BLK_QC_T_NONE		-1U

/*
 * main unit of I/O for the block layer and lower layers (ie drivers and
 * stacking drivers)
 */
struct bio {
	struct bio		*bi_next;	/* request queue link */
	struct block_device	*bi_bdev;
	blk_opf_t		bi_opf;		/* bottom bits REQ_OP, top bits
						 * req_flags.
						 */
	unsigned short		bi_flags;	/* BIO_* below */
	unsigned short		bi_ioprio;
	blk_status_t		bi_status;
	atomic_t		__bi_remaining;

	struct bvec_iter	bi_iter;

	blk_qc_t		bi_cookie;
	bio_end_io_t		*bi_end_io;
	void			*bi_private;
#ifdef CONFIG_BLK_CGROUP
	/*
	 * Represents the association of the css and request_queue for the bio.
	 * If a bio goes direct to device, it will not have a blkg as it will
	 * not have a request_queue associated with it.  The reference is put
	 * on release of the bio.
	 */
	struct blkcg_gq		*bi_blkg;
	struct bio_issue	bi_issue;
#ifdef CONFIG_BLK_CGROUP_IOCOST
	u64			bi_iocost_cost;
#endif
#endif

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	struct bio_crypt_ctx	*bi_crypt_context;
#if IS_ENABLED(CONFIG_DM_DEFAULT_KEY)
	bool			bi_skip_dm_default_key;
#endif
#endif

	union {
#if defined(CONFIG_BLK_DEV_INTEGRITY)
		struct bio_integrity_payload *bi_integrity; /* data integrity */
#endif
	};

	unsigned short		bi_vcnt;	/* how many bio_vec's */

	/*
	 * Everything starting with bi_max_vecs will be preserved by bio_reset()
	 */

	unsigned short		bi_max_vecs;	/* max bvl_vecs we can hold */

	atomic_t		__bi_cnt;	/* pin count */

	struct bio_vec		*bi_io_vec;	/* the actual vec list */

	struct bio_set		*bi_pool;

	/*
	 * We can inline a number of vecs at the end of the bio, to avoid
	 * double allocations for a small number of bio_vecs. This member
	 * MUST obviously be kept at the very end of the bio.
	 */
	struct bio_vec		bi_inline_vecs[];
};

#define BIO_RESET_BYTES		offsetof(struct bio, bi_max_vecs)
#define BIO_MAX_SECTORS		(UINT_MAX >> SECTOR_SHIFT)

/*
 * bio flags
 */
enum {
	BIO_NO_PAGE_REF,	/* don't put release vec pages */
	BIO_CLONED,		/* doesn't own data */
	BIO_BOUNCED,		/* bio is a bounce bio */
	BIO_QUIET,		/* Make BIO Quiet */
	BIO_CHAIN,		/* chained bio, ->bi_remaining in effect */
	BIO_REFFED,		/* bio has elevated ->bi_cnt */
	BIO_BPS_THROTTLED,	/* This bio has already been subjected to
				 * throttling rules. Don't do it again. */
	BIO_TRACE_COMPLETION,	/* bio_endio() should trace the final completion
				 * of this bio. */
	BIO_CGROUP_ACCT,	/* has been accounted to a cgroup */
	BIO_QOS_THROTTLED,	/* bio went through rq_qos throttle path */
	BIO_QOS_MERGED,		/* but went through rq_qos merge path */
	BIO_REMAPPED,
	BIO_ZONE_WRITE_LOCKED,	/* Owns a zoned device zone write lock */
	BIO_FLAG_LAST
};

typedef __u32 __bitwise blk_mq_req_flags_t;

#define REQ_OP_BITS	8
#define REQ_OP_MASK	(__force blk_opf_t)((1 << REQ_OP_BITS) - 1)
#define REQ_FLAG_BITS	24

/**
 * enum req_op - Operations common to the bio and request structures.
 * We use 8 bits for encoding the operation, and the remaining 24 for flags.
 *
 * The least significant bit of the operation number indicates the data
 * transfer direction:
 *
 *   - if the least significant bit is set transfers are TO the device
 *   - if the least significant bit is not set transfers are FROM the device
 *
 * If a operation does not transfer data the least significant bit has no
 * meaning.
 */
enum req_op {
	/* read sectors from the device */
	REQ_OP_READ		= (__force blk_opf_t)0,
	/* write sectors to the device */
	REQ_OP_WRITE		= (__force blk_opf_t)1,
	/* flush the volatile write cache */
	REQ_OP_FLUSH		= (__force blk_opf_t)2,
	/* discard sectors */
	REQ_OP_DISCARD		= (__force blk_opf_t)3,
	/* securely erase sectors */
	REQ_OP_SECURE_ERASE	= (__force blk_opf_t)5,
	/* write the zero filled sector many times */
	REQ_OP_WRITE_ZEROES	= (__force blk_opf_t)9,
	/* Open a zone */
	REQ_OP_ZONE_OPEN	= (__force blk_opf_t)10,
	/* Close a zone */
	REQ_OP_ZONE_CLOSE	= (__force blk_opf_t)11,
	/* Transition a zone to full */
	REQ_OP_ZONE_FINISH	= (__force blk_opf_t)12,
	/* write data at the current zone write pointer */
	REQ_OP_ZONE_APPEND	= (__force blk_opf_t)13,
	/* reset a zone write pointer */
	REQ_OP_ZONE_RESET	= (__force blk_opf_t)15,
	/* reset all the zone present on the device */
	REQ_OP_ZONE_RESET_ALL	= (__force blk_opf_t)17,

	/* Driver private requests */
	REQ_OP_DRV_IN		= (__force blk_opf_t)34,
	REQ_OP_DRV_OUT		= (__force blk_opf_t)35,

	REQ_OP_LAST		= (__force blk_opf_t)36,
};

enum req_flag_bits {
	__REQ_FAILFAST_DEV =	/* no driver retries of device errors */
		REQ_OP_BITS,
	__REQ_FAILFAST_TRANSPORT, /* no driver retries of transport errors */
	__REQ_FAILFAST_DRIVER,	/* no driver retries of driver errors */
	__REQ_SYNC,		/* request is sync (sync write or read) */
	__REQ_META,		/* metadata io request */
	__REQ_PRIO,		/* boost priority in cfq */
	__REQ_NOMERGE,		/* don't touch this for merging */
	__REQ_IDLE,		/* anticipate more IO after this one */
	__REQ_INTEGRITY,	/* I/O includes block integrity payload */
	__REQ_FUA,		/* forced unit access */
	__REQ_PREFLUSH,		/* request for cache flush */
	__REQ_RAHEAD,		/* read ahead, can fail anytime */
	__REQ_BACKGROUND,	/* background IO */
	__REQ_NOWAIT,           /* Don't wait if request will block */
	/*
	 * When a shared kthread needs to issue a bio for a cgroup, doing
	 * so synchronously can lead to priority inversions as the kthread
	 * can be trapped waiting for that cgroup.  CGROUP_PUNT flag makes
	 * submit_bio() punt the actual issuing to a dedicated per-blkcg
	 * work item to avoid such priority inversions.
	 */
	__REQ_CGROUP_PUNT,
	__REQ_POLLED,		/* caller polls for completion using bio_poll */
	__REQ_ALLOC_CACHE,	/* allocate IO from cache if available */
	__REQ_SWAP,		/* swap I/O */
	__REQ_DRV,		/* for driver use */

	/*
	 * Command specific flags, keep last:
	 */
	/* for REQ_OP_WRITE_ZEROES: */
	__REQ_NOUNMAP,		/* do not free blocks when zeroing */

	__REQ_NR_BITS,		/* stops here */
};

#define REQ_FAILFAST_DEV	\
			(__force blk_opf_t)(1ULL << __REQ_FAILFAST_DEV)
#define REQ_FAILFAST_TRANSPORT	\
			(__force blk_opf_t)(1ULL << __REQ_FAILFAST_TRANSPORT)
#define REQ_FAILFAST_DRIVER	\
			(__force blk_opf_t)(1ULL << __REQ_FAILFAST_DRIVER)
#define REQ_SYNC	(__force blk_opf_t)(1ULL << __REQ_SYNC)
#define REQ_META	(__force blk_opf_t)(1ULL << __REQ_META)
#define REQ_PRIO	(__force blk_opf_t)(1ULL << __REQ_PRIO)
#define REQ_NOMERGE	(__force blk_opf_t)(1ULL << __REQ_NOMERGE)
#define REQ_IDLE	(__force blk_opf_t)(1ULL << __REQ_IDLE)
#define REQ_INTEGRITY	(__force blk_opf_t)(1ULL << __REQ_INTEGRITY)
#define REQ_FUA		(__force blk_opf_t)(1ULL << __REQ_FUA)
#define REQ_PREFLUSH	(__force blk_opf_t)(1ULL << __REQ_PREFLUSH)
#define REQ_RAHEAD	(__force blk_opf_t)(1ULL << __REQ_RAHEAD)
#define REQ_BACKGROUND	(__force blk_opf_t)(1ULL << __REQ_BACKGROUND)
#define REQ_NOWAIT	(__force blk_opf_t)(1ULL << __REQ_NOWAIT)
#define REQ_CGROUP_PUNT	(__force blk_opf_t)(1ULL << __REQ_CGROUP_PUNT)

#define REQ_NOUNMAP	(__force blk_opf_t)(1ULL << __REQ_NOUNMAP)
#define REQ_POLLED	(__force blk_opf_t)(1ULL << __REQ_POLLED)
#define REQ_ALLOC_CACHE	(__force blk_opf_t)(1ULL << __REQ_ALLOC_CACHE)

#define REQ_DRV		(__force blk_opf_t)(1ULL << __REQ_DRV)
#define REQ_SWAP	(__force blk_opf_t)(1ULL << __REQ_SWAP)

#define REQ_FAILFAST_MASK \
	(REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT | REQ_FAILFAST_DRIVER)

#define REQ_NOMERGE_FLAGS \
	(REQ_NOMERGE | REQ_PREFLUSH | REQ_FUA)

enum stat_group {
	STAT_READ,
	STAT_WRITE,
	STAT_DISCARD,
	STAT_FLUSH,

	NR_STAT_GROUPS
};

static inline enum req_op bio_op(const struct bio *bio)
{
	return bio->bi_opf & REQ_OP_MASK;
}

/* obsolete, don't use in new code */
static inline void bio_set_op_attrs(struct bio *bio, enum req_op op,
				    blk_opf_t op_flags)
{
	bio->bi_opf = op | op_flags;
}

static inline bool op_is_write(blk_opf_t op)
{
	return !!(op & (__force blk_opf_t)1);
}

/*
 * Check if the bio or request is one that needs special treatment in the
 * flush state machine.
 */
static inline bool op_is_flush(blk_opf_t op)
{
	return op & (REQ_FUA | REQ_PREFLUSH);
}

/*
 * Reads are always treated as synchronous, as are requests with the FUA or
 * PREFLUSH flag.  Other operations may be marked as synchronous using the
 * REQ_SYNC flag.
 */
static inline bool op_is_sync(blk_opf_t op)
{
	return (op & REQ_OP_MASK) == REQ_OP_READ ||
		(op & (REQ_SYNC | REQ_FUA | REQ_PREFLUSH));
}

static inline bool op_is_discard(blk_opf_t op)
{
	return (op & REQ_OP_MASK) == REQ_OP_DISCARD;
}

/*
 * Check if a bio or request operation is a zone management operation, with
 * the exception of REQ_OP_ZONE_RESET_ALL which is treated as a special case
 * due to its different handling in the block layer and device response in
 * case of command failure.
 */
static inline bool op_is_zone_mgmt(enum req_op op)
{
	switch (op & REQ_OP_MASK) {
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_OPEN:
	case REQ_OP_ZONE_CLOSE:
	case REQ_OP_ZONE_FINISH:
		return true;
	default:
		return false;
	}
}

static inline int op_stat_group(enum req_op op)
{
	if (op_is_discard(op))
		return STAT_DISCARD;
	return op_is_write(op);
}

struct blk_rq_stat {
	u64 mean;
	u64 min;
	u64 max;
	u32 nr_samples;
	u64 batch;
};

#endif /* __LINUX_BLK_TYPES_H */
