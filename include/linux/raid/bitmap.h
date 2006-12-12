/*
 * bitmap.h: Copyright (C) Peter T. Breuer (ptb@ot.uc3m.es) 2003
 *
 * additions: Copyright (C) 2003-2004, Paul Clements, SteelEye Technology, Inc.
 */
#ifndef BITMAP_H
#define BITMAP_H 1

#define BITMAP_MAJOR_LO 3
/* version 4 insists the bitmap is in little-endian order
 * with version 3, it is host-endian which is non-portable
 */
#define BITMAP_MAJOR_HI 4
#define	BITMAP_MAJOR_HOSTENDIAN 3

#define BITMAP_MINOR 39

/*
 * in-memory bitmap:
 *
 * Use 16 bit block counters to track pending writes to each "chunk".
 * The 2 high order bits are special-purpose, the first is a flag indicating
 * whether a resync is needed.  The second is a flag indicating whether a
 * resync is active.
 * This means that the counter is actually 14 bits:
 *
 * +--------+--------+------------------------------------------------+
 * | resync | resync |               counter                          |
 * | needed | active |                                                |
 * |  (0-1) |  (0-1) |              (0-16383)                         |
 * +--------+--------+------------------------------------------------+
 *
 * The "resync needed" bit is set when:
 *    a '1' bit is read from storage at startup.
 *    a write request fails on some drives
 *    a resync is aborted on a chunk with 'resync active' set
 * It is cleared (and resync-active set) when a resync starts across all drives
 * of the chunk.
 *
 *
 * The "resync active" bit is set when:
 *    a resync is started on all drives, and resync_needed is set.
 *       resync_needed will be cleared (as long as resync_active wasn't already set).
 * It is cleared when a resync completes.
 *
 * The counter counts pending write requests, plus the on-disk bit.
 * When the counter is '1' and the resync bits are clear, the on-disk
 * bit can be cleared aswell, thus setting the counter to 0.
 * When we set a bit, or in the counter (to start a write), if the fields is
 * 0, we first set the disk bit and set the counter to 1.
 *
 * If the counter is 0, the on-disk bit is clear and the stipe is clean
 * Anything that dirties the stipe pushes the counter to 2 (at least)
 * and sets the on-disk bit (lazily).
 * If a periodic sweep find the counter at 2, it is decremented to 1.
 * If the sweep find the counter at 1, the on-disk bit is cleared and the
 * counter goes to zero.
 *
 * Also, we'll hijack the "map" pointer itself and use it as two 16 bit block
 * counters as a fallback when "page" memory cannot be allocated:
 *
 * Normal case (page memory allocated):
 *
 *     page pointer (32-bit)
 *
 *     [ ] ------+
 *               |
 *               +-------> [   ][   ]..[   ] (4096 byte page == 2048 counters)
 *                          c1   c2    c2048
 *
 * Hijacked case (page memory allocation failed):
 *
 *     hijacked page pointer (32-bit)
 *
 *     [		  ][		  ] (no page memory allocated)
 *      counter #1 (16-bit) counter #2 (16-bit)
 *
 */

#ifdef __KERNEL__

#define PAGE_BITS (PAGE_SIZE << 3)
#define PAGE_BIT_SHIFT (PAGE_SHIFT + 3)

typedef __u16 bitmap_counter_t;
#define COUNTER_BITS 16
#define COUNTER_BIT_SHIFT 4
#define COUNTER_BYTE_RATIO (COUNTER_BITS / 8)
#define COUNTER_BYTE_SHIFT (COUNTER_BIT_SHIFT - 3)

#define NEEDED_MASK ((bitmap_counter_t) (1 << (COUNTER_BITS - 1)))
#define RESYNC_MASK ((bitmap_counter_t) (1 << (COUNTER_BITS - 2)))
#define COUNTER_MAX ((bitmap_counter_t) RESYNC_MASK - 1)
#define NEEDED(x) (((bitmap_counter_t) x) & NEEDED_MASK)
#define RESYNC(x) (((bitmap_counter_t) x) & RESYNC_MASK)
#define COUNTER(x) (((bitmap_counter_t) x) & COUNTER_MAX)

/* how many counters per page? */
#define PAGE_COUNTER_RATIO (PAGE_BITS / COUNTER_BITS)
/* same, except a shift value for more efficient bitops */
#define PAGE_COUNTER_SHIFT (PAGE_BIT_SHIFT - COUNTER_BIT_SHIFT)
/* same, except a mask value for more efficient bitops */
#define PAGE_COUNTER_MASK  (PAGE_COUNTER_RATIO - 1)

#define BITMAP_BLOCK_SIZE 512
#define BITMAP_BLOCK_SHIFT 9

/* how many blocks per chunk? (this is variable) */
#define CHUNK_BLOCK_RATIO(bitmap) ((bitmap)->chunksize >> BITMAP_BLOCK_SHIFT)
#define CHUNK_BLOCK_SHIFT(bitmap) ((bitmap)->chunkshift - BITMAP_BLOCK_SHIFT)
#define CHUNK_BLOCK_MASK(bitmap) (CHUNK_BLOCK_RATIO(bitmap) - 1)

/* when hijacked, the counters and bits represent even larger "chunks" */
/* there will be 1024 chunks represented by each counter in the page pointers */
#define PAGEPTR_BLOCK_RATIO(bitmap) \
			(CHUNK_BLOCK_RATIO(bitmap) << PAGE_COUNTER_SHIFT >> 1)
#define PAGEPTR_BLOCK_SHIFT(bitmap) \
			(CHUNK_BLOCK_SHIFT(bitmap) + PAGE_COUNTER_SHIFT - 1)
#define PAGEPTR_BLOCK_MASK(bitmap) (PAGEPTR_BLOCK_RATIO(bitmap) - 1)

/*
 * on-disk bitmap:
 *
 * Use one bit per "chunk" (block set). We do the disk I/O on the bitmap
 * file a page at a time. There's a superblock at the start of the file.
 */

/* map chunks (bits) to file pages - offset by the size of the superblock */
#define CHUNK_BIT_OFFSET(chunk) ((chunk) + (sizeof(bitmap_super_t) << 3))

#endif

/*
 * bitmap structures:
 */

#define BITMAP_MAGIC 0x6d746962

/* use these for bitmap->flags and bitmap->sb->state bit-fields */
enum bitmap_state {
	BITMAP_ACTIVE = 0x001, /* the bitmap is in use */
	BITMAP_STALE  = 0x002,  /* the bitmap file is out of date or had -EIO */
	BITMAP_WRITE_ERROR = 0x004, /* A write error has occurred */
	BITMAP_HOSTENDIAN = 0x8000,
};

/* the superblock at the front of the bitmap file -- little endian */
typedef struct bitmap_super_s {
	__le32 magic;        /*  0  BITMAP_MAGIC */
	__le32 version;      /*  4  the bitmap major for now, could change... */
	__u8  uuid[16];      /*  8  128 bit uuid - must match md device uuid */
	__le64 events;       /* 24  event counter for the bitmap (1)*/
	__le64 events_cleared;/*32  event counter when last bit cleared (2) */
	__le64 sync_size;    /* 40  the size of the md device's sync range(3) */
	__le32 state;        /* 48  bitmap state information */
	__le32 chunksize;    /* 52  the bitmap chunk size in bytes */
	__le32 daemon_sleep; /* 56  seconds between disk flushes */
	__le32 write_behind; /* 60  number of outstanding write-behind writes */

	__u8  pad[256 - 64]; /* set to zero */
} bitmap_super_t;

/* notes:
 * (1) This event counter is updated before the eventcounter in the md superblock
 *    When a bitmap is loaded, it is only accepted if this event counter is equal
 *    to, or one greater than, the event counter in the superblock.
 * (2) This event counter is updated when the other one is *if*and*only*if* the
 *    array is not degraded.  As bits are not cleared when the array is degraded,
 *    this represents the last time that any bits were cleared.
 *    If a device is being added that has an event count with this value or
 *    higher, it is accepted as conforming to the bitmap.
 * (3)This is the number of sectors represented by the bitmap, and is the range that
 *    resync happens across.  For raid1 and raid5/6 it is the size of individual
 *    devices.  For raid10 it is the size of the array.
 */

#ifdef __KERNEL__

/* the in-memory bitmap is represented by bitmap_pages */
struct bitmap_page {
	/*
	 * map points to the actual memory page
	 */
	char *map;
	/*
	 * in emergencies (when map cannot be alloced), hijack the map
	 * pointer and use it as two counters itself
	 */
	unsigned int hijacked:1;
	/*
	 * count of dirty bits on the page
	 */
	unsigned int  count:31;
};

/* keep track of bitmap file pages that have pending writes on them */
struct page_list {
	struct list_head list;
	struct page *page;
};

/* the main bitmap structure - one per mddev */
struct bitmap {
	struct bitmap_page *bp;
	unsigned long pages; /* total number of pages in the bitmap */
	unsigned long missing_pages; /* number of pages not yet allocated */

	mddev_t *mddev; /* the md device that the bitmap is for */

	int counter_bits; /* how many bits per block counter */

	/* bitmap chunksize -- how much data does each bit represent? */
	unsigned long chunksize;
	unsigned long chunkshift; /* chunksize = 2^chunkshift (for bitops) */
	unsigned long chunks; /* total number of data chunks for the array */

	/* We hold a count on the chunk currently being synced, and drop
	 * it when the last block is started.  If the resync is aborted
	 * midway, we need to be able to drop that count, so we remember
	 * the counted chunk..
	 */
	unsigned long syncchunk;

	__u64	events_cleared;

	/* bitmap spinlock */
	spinlock_t lock;

	long offset; /* offset from superblock if file is NULL */
	struct file *file; /* backing disk file */
	struct page *sb_page; /* cached copy of the bitmap file superblock */
	struct page **filemap; /* list of cache pages for the file */
	unsigned long *filemap_attr; /* attributes associated w/ filemap pages */
	unsigned long file_pages; /* number of pages in the file */

	unsigned long flags;

	unsigned long max_write_behind; /* write-behind mode */
	atomic_t behind_writes;

	/*
	 * the bitmap daemon - periodically wakes up and sweeps the bitmap
	 * file, cleaning up bits and flushing out pages to disk as necessary
	 */
	unsigned long daemon_lastrun; /* jiffies of last run */
	unsigned long daemon_sleep; /* how many seconds between updates? */

	atomic_t pending_writes; /* pending writes to the bitmap file */
	wait_queue_head_t write_wait;

};

/* the bitmap API */

/* these are used only by md/bitmap */
int  bitmap_create(mddev_t *mddev);
void bitmap_flush(mddev_t *mddev);
void bitmap_destroy(mddev_t *mddev);
int  bitmap_active(struct bitmap *bitmap);

char *file_path(struct file *file, char *buf, int count);
void bitmap_print_sb(struct bitmap *bitmap);
int bitmap_update_sb(struct bitmap *bitmap);

int  bitmap_setallbits(struct bitmap *bitmap);
void bitmap_write_all(struct bitmap *bitmap);

void bitmap_dirty_bits(struct bitmap *bitmap, unsigned long s, unsigned long e);

/* these are exported */
int bitmap_startwrite(struct bitmap *bitmap, sector_t offset,
			unsigned long sectors, int behind);
void bitmap_endwrite(struct bitmap *bitmap, sector_t offset,
			unsigned long sectors, int success, int behind);
int bitmap_start_sync(struct bitmap *bitmap, sector_t offset, int *blocks, int degraded);
void bitmap_end_sync(struct bitmap *bitmap, sector_t offset, int *blocks, int aborted);
void bitmap_close_sync(struct bitmap *bitmap);

int bitmap_unplug(struct bitmap *bitmap);
int bitmap_daemon_work(struct bitmap *bitmap);
#endif

#endif
