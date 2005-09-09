/*
   md_k.h : kernel internal structure of the Linux MD driver
          Copyright (C) 1996-98 Ingo Molnar, Gadi Oxman
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_K_H
#define _MD_K_H

/* and dm-bio-list.h is not under include/linux because.... ??? */
#include "../../../drivers/md/dm-bio-list.h"

#define MD_RESERVED       0UL
#define LINEAR            1UL
#define RAID0             2UL
#define RAID1             3UL
#define RAID5             4UL
#define TRANSLUCENT       5UL
#define HSM               6UL
#define MULTIPATH         7UL
#define RAID6		  8UL
#define	RAID10		  9UL
#define FAULTY		  10UL
#define MAX_PERSONALITY   11UL

#define	LEVEL_MULTIPATH		(-4)
#define	LEVEL_LINEAR		(-1)
#define	LEVEL_FAULTY		(-5)

#define MaxSector (~(sector_t)0)
#define MD_THREAD_NAME_MAX 14

static inline int pers_to_level (int pers)
{
	switch (pers) {
		case FAULTY:		return LEVEL_FAULTY;
		case MULTIPATH:		return LEVEL_MULTIPATH;
		case HSM:		return -3;
		case TRANSLUCENT:	return -2;
		case LINEAR:		return LEVEL_LINEAR;
		case RAID0:		return 0;
		case RAID1:		return 1;
		case RAID5:		return 5;
		case RAID6:		return 6;
		case RAID10:		return 10;
	}
	BUG();
	return MD_RESERVED;
}

static inline int level_to_pers (int level)
{
	switch (level) {
		case LEVEL_FAULTY: return FAULTY;
		case LEVEL_MULTIPATH: return MULTIPATH;
		case -3: return HSM;
		case -2: return TRANSLUCENT;
		case LEVEL_LINEAR: return LINEAR;
		case 0: return RAID0;
		case 1: return RAID1;
		case 4:
		case 5: return RAID5;
		case 6: return RAID6;
		case 10: return RAID10;
	}
	return MD_RESERVED;
}

typedef struct mddev_s mddev_t;
typedef struct mdk_rdev_s mdk_rdev_t;

#define MAX_MD_DEVS  256	/* Max number of md dev */

/*
 * options passed in raidrun:
 */

#define MAX_CHUNK_SIZE (4096*1024)

/*
 * MD's 'extended' device
 */
struct mdk_rdev_s
{
	struct list_head same_set;	/* RAID devices within the same set */

	sector_t size;			/* Device size (in blocks) */
	mddev_t *mddev;			/* RAID array if running */
	unsigned long last_events;	/* IO event timestamp */

	struct block_device *bdev;	/* block device handle */

	struct page	*sb_page;
	int		sb_loaded;
	sector_t	data_offset;	/* start of data in array */
	sector_t	sb_offset;
	int		sb_size;	/* bytes in the superblock */
	int		preferred_minor;	/* autorun support */

	/* A device can be in one of three states based on two flags:
	 * Not working:   faulty==1 in_sync==0
	 * Fully working: faulty==0 in_sync==1
	 * Working, but not
	 * in sync with array
	 *                faulty==0 in_sync==0
	 *
	 * It can never have faulty==1, in_sync==1
	 * This reduces the burden of testing multiple flags in many cases
	 */
	int faulty;			/* if faulty do not issue IO requests */
	int in_sync;			/* device is a full member of the array */

	unsigned long	flags;		/* Should include faulty and in_sync here. */
#define	WriteMostly	4		/* Avoid reading if at all possible */

	int desc_nr;			/* descriptor index in the superblock */
	int raid_disk;			/* role of device in array */
	int saved_raid_disk;		/* role that device used to have in the
					 * array and could again if we did a partial
					 * resync from the bitmap
					 */

	atomic_t	nr_pending;	/* number of pending requests.
					 * only maintained for arrays that
					 * support hot removal
					 */
};

typedef struct mdk_personality_s mdk_personality_t;

struct mddev_s
{
	void				*private;
	mdk_personality_t		*pers;
	dev_t				unit;
	int				md_minor;
	struct list_head 		disks;
	int				sb_dirty;
	int				ro;

	struct gendisk			*gendisk;

	/* Superblock information */
	int				major_version,
					minor_version,
					patch_version;
	int				persistent;
	int				chunk_size;
	time_t				ctime, utime;
	int				level, layout;
	int				raid_disks;
	int				max_disks;
	sector_t			size; /* used size of component devices */
	sector_t			array_size; /* exported array size */
	__u64				events;

	char				uuid[16];

	struct mdk_thread_s		*thread;	/* management thread */
	struct mdk_thread_s		*sync_thread;	/* doing resync or reconstruct */
	sector_t			curr_resync;	/* blocks scheduled */
	unsigned long			resync_mark;	/* a recent timestamp */
	sector_t			resync_mark_cnt;/* blocks written at resync_mark */

	sector_t			resync_max_sectors; /* may be set by personality */
	/* recovery/resync flags 
	 * NEEDED:   we might need to start a resync/recover
	 * RUNNING:  a thread is running, or about to be started
	 * SYNC:     actually doing a resync, not a recovery
	 * ERR:      and IO error was detected - abort the resync/recovery
	 * INTR:     someone requested a (clean) early abort.
	 * DONE:     thread is done and is waiting to be reaped
	 */
#define	MD_RECOVERY_RUNNING	0
#define	MD_RECOVERY_SYNC	1
#define	MD_RECOVERY_ERR		2
#define	MD_RECOVERY_INTR	3
#define	MD_RECOVERY_DONE	4
#define	MD_RECOVERY_NEEDED	5
	unsigned long			recovery;

	int				in_sync;	/* know to not need resync */
	struct semaphore		reconfig_sem;
	atomic_t			active;

	int				changed;	/* true if we might need to reread partition info */
	int				degraded;	/* whether md should consider
							 * adding a spare
							 */

	atomic_t			recovery_active; /* blocks scheduled, but not written */
	wait_queue_head_t		recovery_wait;
	sector_t			recovery_cp;

	spinlock_t			write_lock;
	wait_queue_head_t		sb_wait;	/* for waiting on superblock updates */
	atomic_t			pending_writes;	/* number of active superblock writes */

	unsigned int			safemode;	/* if set, update "clean" superblock
							 * when no writes pending.
							 */ 
	unsigned int			safemode_delay;
	struct timer_list		safemode_timer;
	atomic_t			writes_pending; 
	request_queue_t			*queue;	/* for plugging ... */

	atomic_t                        write_behind; /* outstanding async IO */
	unsigned int                    max_write_behind; /* 0 = sync */

	struct bitmap                   *bitmap; /* the bitmap for the device */
	struct file			*bitmap_file; /* the bitmap file */
	long				bitmap_offset; /* offset from superblock of
							* start of bitmap. May be
							* negative, but not '0'
							*/
	long				default_bitmap_offset; /* this is the offset to use when
								* hot-adding a bitmap.  It should
								* eventually be settable by sysfs.
								*/

	struct list_head		all_mddevs;
};


static inline void rdev_dec_pending(mdk_rdev_t *rdev, mddev_t *mddev)
{
	int faulty = rdev->faulty;
	if (atomic_dec_and_test(&rdev->nr_pending) && faulty)
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
}

static inline void md_sync_acct(struct block_device *bdev, unsigned long nr_sectors)
{
        atomic_add(nr_sectors, &bdev->bd_contains->bd_disk->sync_io);
}

struct mdk_personality_s
{
	char *name;
	struct module *owner;
	int (*make_request)(request_queue_t *q, struct bio *bio);
	int (*run)(mddev_t *mddev);
	int (*stop)(mddev_t *mddev);
	void (*status)(struct seq_file *seq, mddev_t *mddev);
	/* error_handler must set ->faulty and clear ->in_sync
	 * if appropriate, and should abort recovery if needed 
	 */
	void (*error_handler)(mddev_t *mddev, mdk_rdev_t *rdev);
	int (*hot_add_disk) (mddev_t *mddev, mdk_rdev_t *rdev);
	int (*hot_remove_disk) (mddev_t *mddev, int number);
	int (*spare_active) (mddev_t *mddev);
	sector_t (*sync_request)(mddev_t *mddev, sector_t sector_nr, int *skipped, int go_faster);
	int (*resize) (mddev_t *mddev, sector_t sectors);
	int (*reshape) (mddev_t *mddev, int raid_disks);
	int (*reconfig) (mddev_t *mddev, int layout, int chunk_size);
	/* quiesce moves between quiescence states
	 * 0 - fully active
	 * 1 - no new requests allowed
	 * others - reserved
	 */
	void (*quiesce) (mddev_t *mddev, int state);
};


static inline char * mdname (mddev_t * mddev)
{
	return mddev->gendisk ? mddev->gendisk->disk_name : "mdX";
}

extern mdk_rdev_t * find_rdev_nr(mddev_t *mddev, int nr);

/*
 * iterates through some rdev ringlist. It's safe to remove the
 * current 'rdev'. Dont touch 'tmp' though.
 */
#define ITERATE_RDEV_GENERIC(head,rdev,tmp)				\
									\
	for ((tmp) = (head).next;					\
		(rdev) = (list_entry((tmp), mdk_rdev_t, same_set)),	\
			(tmp) = (tmp)->next, (tmp)->prev != &(head)	\
		; )
/*
 * iterates through the 'same array disks' ringlist
 */
#define ITERATE_RDEV(mddev,rdev,tmp)					\
	ITERATE_RDEV_GENERIC((mddev)->disks,rdev,tmp)

/*
 * Iterates through 'pending RAID disks'
 */
#define ITERATE_RDEV_PENDING(rdev,tmp)					\
	ITERATE_RDEV_GENERIC(pending_raid_disks,rdev,tmp)

typedef struct mdk_thread_s {
	void			(*run) (mddev_t *mddev);
	mddev_t			*mddev;
	wait_queue_head_t	wqueue;
	unsigned long           flags;
	struct completion	*event;
	struct task_struct	*tsk;
	unsigned long		timeout;
	const char		*name;
} mdk_thread_t;

#define THREAD_WAKEUP  0

#define __wait_event_lock_irq(wq, condition, lock, cmd) 		\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_UNINTERRUPTIBLE);		\
		if (condition)						\
			break;						\
		spin_unlock_irq(&lock);					\
		cmd;							\
		schedule();						\
		spin_lock_irq(&lock);					\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event_lock_irq(wq, condition, lock, cmd) 			\
do {									\
	if (condition)	 						\
		break;							\
	__wait_event_lock_irq(wq, condition, lock, cmd);		\
} while (0)

#endif

