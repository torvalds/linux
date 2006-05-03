/* $Id: jffs2_fs_sb.h,v 1.54 2005/09/21 13:37:34 dedekind Exp $ */

#ifndef _JFFS2_FS_SB
#define _JFFS2_FS_SB

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <asm/semaphore.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/rwsem.h>

#define JFFS2_SB_FLAG_RO 1
#define JFFS2_SB_FLAG_SCANNING 2 /* Flash scanning is in progress */
#define JFFS2_SB_FLAG_BUILDING 4 /* File system building is in progress */

struct jffs2_inodirty;

/* A struct for the overall file system control.  Pointers to
   jffs2_sb_info structs are named `c' in the source code.
   Nee jffs_control
*/
struct jffs2_sb_info {
	struct mtd_info *mtd;

	uint32_t highest_ino;
	uint32_t checked_ino;

	unsigned int flags;

	struct task_struct *gc_task;	/* GC task struct */
	struct completion gc_thread_start; /* GC thread start completion */
	struct completion gc_thread_exit; /* GC thread exit completion port */

	struct semaphore alloc_sem;	/* Used to protect all the following
					   fields, and also to protect against
					   out-of-order writing of nodes. And GC. */
	uint32_t cleanmarker_size;	/* Size of an _inline_ CLEANMARKER
					 (i.e. zero for OOB CLEANMARKER */

	uint32_t flash_size;
	uint32_t used_size;
	uint32_t dirty_size;
	uint32_t wasted_size;
	uint32_t free_size;
	uint32_t erasing_size;
	uint32_t bad_size;
	uint32_t sector_size;
	uint32_t unchecked_size;

	uint32_t nr_free_blocks;
	uint32_t nr_erasing_blocks;

	/* Number of free blocks there must be before we... */
	uint8_t resv_blocks_write;	/* ... allow a normal filesystem write */
	uint8_t resv_blocks_deletion;	/* ... allow a normal filesystem deletion */
	uint8_t resv_blocks_gctrigger;	/* ... wake up the GC thread */
	uint8_t resv_blocks_gcbad;	/* ... pick a block from the bad_list to GC */
	uint8_t resv_blocks_gcmerge;	/* ... merge pages when garbage collecting */

	uint32_t nospc_dirty_size;

	uint32_t nr_blocks;
	struct jffs2_eraseblock *blocks;	/* The whole array of blocks. Used for getting blocks
						 * from the offset (blocks[ofs / sector_size]) */
	struct jffs2_eraseblock *nextblock;	/* The block we're currently filling */

	struct jffs2_eraseblock *gcblock;	/* The block we're currently garbage-collecting */

	struct list_head clean_list;		/* Blocks 100% full of clean data */
	struct list_head very_dirty_list;	/* Blocks with lots of dirty space */
	struct list_head dirty_list;		/* Blocks with some dirty space */
	struct list_head erasable_list;		/* Blocks which are completely dirty, and need erasing */
	struct list_head erasable_pending_wbuf_list;	/* Blocks which need erasing but only after the current wbuf is flushed */
	struct list_head erasing_list;		/* Blocks which are currently erasing */
	struct list_head erase_pending_list;	/* Blocks which need erasing now */
	struct list_head erase_complete_list;	/* Blocks which are erased and need the clean marker written to them */
	struct list_head free_list;		/* Blocks which are free and ready to be used */
	struct list_head bad_list;		/* Bad blocks. */
	struct list_head bad_used_list;		/* Bad blocks with valid data in. */

	spinlock_t erase_completion_lock;	/* Protect free_list and erasing_list
						   against erase completion handler */
	wait_queue_head_t erase_wait;		/* For waiting for erases to complete */

	wait_queue_head_t inocache_wq;
	struct jffs2_inode_cache **inocache_list;
	spinlock_t inocache_lock;

	/* Sem to allow jffs2_garbage_collect_deletion_dirent to
	   drop the erase_completion_lock while it's holding a pointer
	   to an obsoleted node. I don't like this. Alternatives welcomed. */
	struct semaphore erase_free_sem;

	uint32_t wbuf_pagesize; /* 0 for NOR and other flashes with no wbuf */

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	/* Write-behind buffer for NAND flash */
	unsigned char *wbuf;
	uint32_t wbuf_ofs;
	uint32_t wbuf_len;
	struct jffs2_inodirty *wbuf_inodes;

	struct rw_semaphore wbuf_sem;	/* Protects the write buffer */

	/* Information about out-of-band area usage... */
	struct nand_oobinfo *oobinfo;
	uint32_t badblock_pos;
	uint32_t fsdata_pos;
	uint32_t fsdata_len;
#endif

	struct jffs2_summary *summary;		/* Summary information */

	/* OS-private pointer for getting back to master superblock info */
	void *os_priv;
};

#endif /* _JFFS2_FB_SB */
