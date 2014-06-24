/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __INCORE_DOT_H__
#define __INCORE_DOT_H__

#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/workqueue.h>
#include <linux/dlm.h>
#include <linux/buffer_head.h>
#include <linux/rcupdate.h>
#include <linux/rculist_bl.h>
#include <linux/completion.h>
#include <linux/rbtree.h>
#include <linux/ktime.h>
#include <linux/percpu.h>
#include <linux/lockref.h>

#define DIO_WAIT	0x00000010
#define DIO_METADATA	0x00000020

struct gfs2_log_operations;
struct gfs2_bufdata;
struct gfs2_holder;
struct gfs2_glock;
struct gfs2_quota_data;
struct gfs2_trans;
struct gfs2_jdesc;
struct gfs2_sbd;
struct lm_lockops;

typedef void (*gfs2_glop_bh_t) (struct gfs2_glock *gl, unsigned int ret);

struct gfs2_log_header_host {
	u64 lh_sequence;	/* Sequence number of this transaction */
	u32 lh_flags;		/* GFS2_LOG_HEAD_... */
	u32 lh_tail;		/* Block number of log tail */
	u32 lh_blkno;
	u32 lh_hash;
};

/*
 * Structure of operations that are associated with each
 * type of element in the log.
 */

struct gfs2_log_operations {
	void (*lo_before_commit) (struct gfs2_sbd *sdp, struct gfs2_trans *tr);
	void (*lo_after_commit) (struct gfs2_sbd *sdp, struct gfs2_trans *tr);
	void (*lo_before_scan) (struct gfs2_jdesc *jd,
				struct gfs2_log_header_host *head, int pass);
	int (*lo_scan_elements) (struct gfs2_jdesc *jd, unsigned int start,
				 struct gfs2_log_descriptor *ld, __be64 *ptr,
				 int pass);
	void (*lo_after_scan) (struct gfs2_jdesc *jd, int error, int pass);
	const char *lo_name;
};

#define GBF_FULL 1

struct gfs2_bitmap {
	struct buffer_head *bi_bh;
	char *bi_clone;
	unsigned long bi_flags;
	u32 bi_offset;
	u32 bi_start;
	u32 bi_len;
	u32 bi_blocks;
};

struct gfs2_rgrpd {
	struct rb_node rd_node;		/* Link with superblock */
	struct gfs2_glock *rd_gl;	/* Glock for this rgrp */
	u64 rd_addr;			/* grp block disk address */
	u64 rd_data0;			/* first data location */
	u32 rd_length;			/* length of rgrp header in fs blocks */
	u32 rd_data;			/* num of data blocks in rgrp */
	u32 rd_bitbytes;		/* number of bytes in data bitmaps */
	u32 rd_free;
	u32 rd_reserved;                /* number of blocks reserved */
	u32 rd_free_clone;
	u32 rd_dinodes;
	u64 rd_igeneration;
	struct gfs2_bitmap *rd_bits;
	struct gfs2_sbd *rd_sbd;
	struct gfs2_rgrp_lvb *rd_rgl;
	u32 rd_last_alloc;
	u32 rd_flags;
	u32 rd_extfail_pt;		/* extent failure point */
#define GFS2_RDF_CHECK		0x10000000 /* check for unlinked inodes */
#define GFS2_RDF_UPTODATE	0x20000000 /* rg is up to date */
#define GFS2_RDF_ERROR		0x40000000 /* error in rg */
#define GFS2_RDF_MASK		0xf0000000 /* mask for internal flags */
	spinlock_t rd_rsspin;           /* protects reservation related vars */
	struct rb_root rd_rstree;       /* multi-block reservation tree */
};

struct gfs2_rbm {
	struct gfs2_rgrpd *rgd;
	u32 offset;		/* The offset is bitmap relative */
	int bii;		/* Bitmap index */
};

static inline struct gfs2_bitmap *rbm_bi(const struct gfs2_rbm *rbm)
{
	return rbm->rgd->rd_bits + rbm->bii;
}

static inline u64 gfs2_rbm_to_block(const struct gfs2_rbm *rbm)
{
	return rbm->rgd->rd_data0 + (rbm_bi(rbm)->bi_start * GFS2_NBBY) +
		rbm->offset;
}

static inline bool gfs2_rbm_eq(const struct gfs2_rbm *rbm1,
			       const struct gfs2_rbm *rbm2)
{
	return (rbm1->rgd == rbm2->rgd) && (rbm1->bii == rbm2->bii) &&
	       (rbm1->offset == rbm2->offset);
}

enum gfs2_state_bits {
	BH_Pinned = BH_PrivateStart,
	BH_Escaped = BH_PrivateStart + 1,
	BH_Zeronew = BH_PrivateStart + 2,
};

BUFFER_FNS(Pinned, pinned)
TAS_BUFFER_FNS(Pinned, pinned)
BUFFER_FNS(Escaped, escaped)
TAS_BUFFER_FNS(Escaped, escaped)
BUFFER_FNS(Zeronew, zeronew)
TAS_BUFFER_FNS(Zeronew, zeronew)

struct gfs2_bufdata {
	struct buffer_head *bd_bh;
	struct gfs2_glock *bd_gl;
	u64 bd_blkno;

	struct list_head bd_list;
	const struct gfs2_log_operations *bd_ops;

	struct gfs2_trans *bd_tr;
	struct list_head bd_ail_st_list;
	struct list_head bd_ail_gl_list;
};

/*
 * Internally, we prefix things with gdlm_ and GDLM_ (for gfs-dlm) since a
 * prefix of lock_dlm_ gets awkward.
 */

#define GDLM_STRNAME_BYTES	25
#define GDLM_LVB_SIZE		32

/*
 * ls_recover_flags:
 *
 * DFL_BLOCK_LOCKS: dlm is in recovery and will grant locks that had been
 * held by failed nodes whose journals need recovery.  Those locks should
 * only be used for journal recovery until the journal recovery is done.
 * This is set by the dlm recover_prep callback and cleared by the
 * gfs2_control thread when journal recovery is complete.  To avoid
 * races between recover_prep setting and gfs2_control clearing, recover_spin
 * is held while changing this bit and reading/writing recover_block
 * and recover_start.
 *
 * DFL_NO_DLM_OPS: dlm lockspace ops/callbacks are not being used.
 *
 * DFL_FIRST_MOUNT: this node is the first to mount this fs and is doing
 * recovery of all journals before allowing other nodes to mount the fs.
 * This is cleared when FIRST_MOUNT_DONE is set.
 *
 * DFL_FIRST_MOUNT_DONE: this node was the first mounter, and has finished
 * recovery of all journals, and now allows other nodes to mount the fs.
 *
 * DFL_MOUNT_DONE: gdlm_mount has completed successfully and cleared
 * BLOCK_LOCKS for the first time.  The gfs2_control thread should now
 * control clearing BLOCK_LOCKS for further recoveries.
 *
 * DFL_UNMOUNT: gdlm_unmount sets to keep sdp off gfs2_control_wq.
 *
 * DFL_DLM_RECOVERY: set while dlm is in recovery, between recover_prep()
 * and recover_done(), i.e. set while recover_block == recover_start.
 */

enum {
	DFL_BLOCK_LOCKS		= 0,
	DFL_NO_DLM_OPS		= 1,
	DFL_FIRST_MOUNT		= 2,
	DFL_FIRST_MOUNT_DONE	= 3,
	DFL_MOUNT_DONE		= 4,
	DFL_UNMOUNT		= 5,
	DFL_DLM_RECOVERY	= 6,
};

struct lm_lockname {
	u64 ln_number;
	unsigned int ln_type;
};

#define lm_name_equal(name1, name2) \
        (((name1)->ln_number == (name2)->ln_number) && \
         ((name1)->ln_type == (name2)->ln_type))


struct gfs2_glock_operations {
	void (*go_sync) (struct gfs2_glock *gl);
	int (*go_xmote_bh) (struct gfs2_glock *gl, struct gfs2_holder *gh);
	void (*go_inval) (struct gfs2_glock *gl, int flags);
	int (*go_demote_ok) (const struct gfs2_glock *gl);
	int (*go_lock) (struct gfs2_holder *gh);
	void (*go_unlock) (struct gfs2_holder *gh);
	void (*go_dump)(struct seq_file *seq, const struct gfs2_glock *gl);
	void (*go_callback)(struct gfs2_glock *gl, bool remote);
	const int go_type;
	const unsigned long go_flags;
#define GLOF_ASPACE 1
#define GLOF_LVB    2
};

enum {
	GFS2_LKS_SRTT = 0,	/* Non blocking smoothed round trip time */
	GFS2_LKS_SRTTVAR = 1,	/* Non blocking smoothed variance */
	GFS2_LKS_SRTTB = 2,	/* Blocking smoothed round trip time */
	GFS2_LKS_SRTTVARB = 3,	/* Blocking smoothed variance */
	GFS2_LKS_SIRT = 4,	/* Smoothed Inter-request time */
	GFS2_LKS_SIRTVAR = 5,	/* Smoothed Inter-request variance */
	GFS2_LKS_DCOUNT = 6,	/* Count of dlm requests */
	GFS2_LKS_QCOUNT = 7,	/* Count of gfs2_holder queues */
	GFS2_NR_LKSTATS
};

struct gfs2_lkstats {
	s64 stats[GFS2_NR_LKSTATS];
};

enum {
	/* States */
	HIF_HOLDER		= 6,  /* Set for gh that "holds" the glock */
	HIF_FIRST		= 7,
	HIF_WAIT		= 10,
};

struct gfs2_holder {
	struct list_head gh_list;

	struct gfs2_glock *gh_gl;
	struct pid *gh_owner_pid;
	unsigned int gh_state;
	unsigned gh_flags;

	int gh_error;
	unsigned long gh_iflags; /* HIF_... */
	unsigned long gh_ip;
};

/* Resource group multi-block reservation, in order of appearance:

   Step 1. Function prepares to write, allocates a mb, sets the size hint.
   Step 2. User calls inplace_reserve to target an rgrp, sets the rgrp info
   Step 3. Function get_local_rgrp locks the rgrp, determines which bits to use
   Step 4. Bits are assigned from the rgrp based on either the reservation
           or wherever it can.
*/

struct gfs2_blkreserv {
	/* components used during write (step 1): */
	atomic_t rs_sizehint;         /* hint of the write size */

	struct gfs2_holder rs_rgd_gh; /* Filled in by get_local_rgrp */
	struct rb_node rs_node;       /* link to other block reservations */
	struct gfs2_rbm rs_rbm;       /* Start of reservation */
	u32 rs_free;                  /* how many blocks are still free */
	u64 rs_inum;                  /* Inode number for reservation */

	/* ancillary quota stuff */
	struct gfs2_quota_data *rs_qa_qd[2 * MAXQUOTAS];
	struct gfs2_holder rs_qa_qd_ghs[2 * MAXQUOTAS];
	unsigned int rs_qa_qd_num;
};

/*
 * Allocation parameters
 * @target: The number of blocks we'd ideally like to allocate
 * @aflags: The flags (e.g. Orlov flag)
 *
 * The intent is to gradually expand this structure over time in
 * order to give more information, e.g. alignment, min extent size
 * to the allocation code.
 */
struct gfs2_alloc_parms {
	u32 target;
	u32 aflags;
};

enum {
	GLF_LOCK			= 1,
	GLF_DEMOTE			= 3,
	GLF_PENDING_DEMOTE		= 4,
	GLF_DEMOTE_IN_PROGRESS		= 5,
	GLF_DIRTY			= 6,
	GLF_LFLUSH			= 7,
	GLF_INVALIDATE_IN_PROGRESS	= 8,
	GLF_REPLY_PENDING		= 9,
	GLF_INITIAL			= 10,
	GLF_FROZEN			= 11,
	GLF_QUEUED			= 12,
	GLF_LRU				= 13,
	GLF_OBJECT			= 14, /* Used only for tracing */
	GLF_BLOCKING			= 15,
};

struct gfs2_glock {
	struct hlist_bl_node gl_list;
	struct gfs2_sbd *gl_sbd;
	unsigned long gl_flags;		/* GLF_... */
	struct lm_lockname gl_name;

	struct lockref gl_lockref;
#define gl_spin gl_lockref.lock

	/* State fields protected by gl_spin */
	unsigned int gl_state:2,	/* Current state */
		     gl_target:2,	/* Target state */
		     gl_demote_state:2,	/* State requested by remote node */
		     gl_req:2,		/* State in last dlm request */
		     gl_reply:8;	/* Last reply from the dlm */

	unsigned int gl_hash;
	unsigned long gl_demote_time; /* time of first demote request */
	long gl_hold_time;
	struct list_head gl_holders;

	const struct gfs2_glock_operations *gl_ops;
	ktime_t gl_dstamp;
	struct gfs2_lkstats gl_stats;
	struct dlm_lksb gl_lksb;
	unsigned long gl_tchange;
	void *gl_object;

	struct list_head gl_lru;
	struct list_head gl_ail_list;
	atomic_t gl_ail_count;
	atomic_t gl_revokes;
	struct delayed_work gl_work;
	union {
		/* For inode and iopen glocks only */
		struct work_struct gl_delete;
		/* For rgrp glocks only */
		struct {
			loff_t start;
			loff_t end;
		} gl_vm;
	};
	struct rcu_head gl_rcu;
};

#define GFS2_MIN_LVB_SIZE 32	/* Min size of LVB that gfs2 supports */

enum {
	GIF_INVALID		= 0,
	GIF_QD_LOCKED		= 1,
	GIF_ALLOC_FAILED	= 2,
	GIF_SW_PAGED		= 3,
	GIF_ORDERED		= 4,
	GIF_FREE_VFS_INODE      = 5,
};

struct gfs2_inode {
	struct inode i_inode;
	u64 i_no_addr;
	u64 i_no_formal_ino;
	u64 i_generation;
	u64 i_eattr;
	unsigned long i_flags;		/* GIF_... */
	struct gfs2_glock *i_gl; /* Move into i_gh? */
	struct gfs2_holder i_iopen_gh;
	struct gfs2_holder i_gh; /* for prepare/commit_write only */
	struct gfs2_blkreserv *i_res; /* rgrp multi-block reservation */
	struct gfs2_rgrpd *i_rgd;
	u64 i_goal;	/* goal block for allocations */
	struct rw_semaphore i_rw_mutex;
	struct list_head i_ordered;
	struct list_head i_trunc_list;
	__be64 *i_hash_cache;
	u32 i_entries;
	u32 i_diskflags;
	u8 i_height;
	u8 i_depth;
};

/*
 * Since i_inode is the first element of struct gfs2_inode,
 * this is effectively a cast.
 */
static inline struct gfs2_inode *GFS2_I(struct inode *inode)
{
	return container_of(inode, struct gfs2_inode, i_inode);
}

static inline struct gfs2_sbd *GFS2_SB(const struct inode *inode)
{
	return inode->i_sb->s_fs_info;
}

struct gfs2_file {
	struct mutex f_fl_mutex;
	struct gfs2_holder f_fl_gh;
};

struct gfs2_revoke_replay {
	struct list_head rr_list;
	u64 rr_blkno;
	unsigned int rr_where;
};

enum {
	QDF_CHANGE		= 1,
	QDF_LOCKED		= 2,
	QDF_REFRESH		= 3,
};

struct gfs2_quota_data {
	struct hlist_bl_node qd_hlist;
	struct list_head qd_list;
	struct kqid qd_id;
	struct gfs2_sbd *qd_sbd;
	struct lockref qd_lockref;
	struct list_head qd_lru;
	unsigned qd_hash;

	unsigned long qd_flags;		/* QDF_... */

	s64 qd_change;
	s64 qd_change_sync;

	unsigned int qd_slot;
	unsigned int qd_slot_count;

	struct buffer_head *qd_bh;
	struct gfs2_quota_change *qd_bh_qc;
	unsigned int qd_bh_count;

	struct gfs2_glock *qd_gl;
	struct gfs2_quota_lvb qd_qb;

	u64 qd_sync_gen;
	unsigned long qd_last_warn;
	struct rcu_head qd_rcu;
};

struct gfs2_trans {
	unsigned long tr_ip;

	unsigned int tr_blocks;
	unsigned int tr_revokes;
	unsigned int tr_reserved;
	unsigned int tr_touched:1;
	unsigned int tr_attached:1;

	struct gfs2_holder tr_t_gh;


	unsigned int tr_num_buf_new;
	unsigned int tr_num_databuf_new;
	unsigned int tr_num_buf_rm;
	unsigned int tr_num_databuf_rm;
	unsigned int tr_num_revoke;
	unsigned int tr_num_revoke_rm;

	struct list_head tr_list;
	struct list_head tr_databuf;
	struct list_head tr_buf;

	unsigned int tr_first;
	struct list_head tr_ail1_list;
	struct list_head tr_ail2_list;
};

struct gfs2_journal_extent {
	struct list_head list;

	unsigned int lblock; /* First logical block */
	u64 dblock; /* First disk block */
	u64 blocks;
};

struct gfs2_jdesc {
	struct list_head jd_list;
	struct list_head extent_list;
	unsigned int nr_extents;
	struct work_struct jd_work;
	struct inode *jd_inode;
	unsigned long jd_flags;
#define JDF_RECOVERY 1
	unsigned int jd_jid;
	unsigned int jd_blocks;
	int jd_recover_error;
	/* Replay stuff */

	unsigned int jd_found_blocks;
	unsigned int jd_found_revokes;
	unsigned int jd_replayed_blocks;

	struct list_head jd_revoke_list;
	unsigned int jd_replay_tail;

};

struct gfs2_statfs_change_host {
	s64 sc_total;
	s64 sc_free;
	s64 sc_dinodes;
};

#define GFS2_QUOTA_DEFAULT	GFS2_QUOTA_OFF
#define GFS2_QUOTA_OFF		0
#define GFS2_QUOTA_ACCOUNT	1
#define GFS2_QUOTA_ON		2

#define GFS2_DATA_DEFAULT	GFS2_DATA_ORDERED
#define GFS2_DATA_WRITEBACK	1
#define GFS2_DATA_ORDERED	2

#define GFS2_ERRORS_DEFAULT     GFS2_ERRORS_WITHDRAW
#define GFS2_ERRORS_WITHDRAW    0
#define GFS2_ERRORS_CONTINUE    1 /* place holder for future feature */
#define GFS2_ERRORS_RO          2 /* place holder for future feature */
#define GFS2_ERRORS_PANIC       3

struct gfs2_args {
	char ar_lockproto[GFS2_LOCKNAME_LEN];	/* Name of the Lock Protocol */
	char ar_locktable[GFS2_LOCKNAME_LEN];	/* Name of the Lock Table */
	char ar_hostdata[GFS2_LOCKNAME_LEN];	/* Host specific data */
	unsigned int ar_spectator:1;		/* Don't get a journal */
	unsigned int ar_localflocks:1;		/* Let the VFS do flock|fcntl */
	unsigned int ar_debug:1;		/* Oops on errors */
	unsigned int ar_posix_acl:1;		/* Enable posix acls */
	unsigned int ar_quota:2;		/* off/account/on */
	unsigned int ar_suiddir:1;		/* suiddir support */
	unsigned int ar_data:2;			/* ordered/writeback */
	unsigned int ar_meta:1;			/* mount metafs */
	unsigned int ar_discard:1;		/* discard requests */
	unsigned int ar_errors:2;               /* errors=withdraw | panic */
	unsigned int ar_nobarrier:1;            /* do not send barriers */
	unsigned int ar_rgrplvb:1;		/* use lvbs for rgrp info */
	int ar_commit;				/* Commit interval */
	int ar_statfs_quantum;			/* The fast statfs interval */
	int ar_quota_quantum;			/* The quota interval */
	int ar_statfs_percent;			/* The % change to force sync */
};

struct gfs2_tune {
	spinlock_t gt_spin;

	unsigned int gt_logd_secs;

	unsigned int gt_quota_warn_period; /* Secs between quota warn msgs */
	unsigned int gt_quota_scale_num; /* Numerator */
	unsigned int gt_quota_scale_den; /* Denominator */
	unsigned int gt_quota_quantum; /* Secs between syncs to quota file */
	unsigned int gt_new_files_jdata;
	unsigned int gt_max_readahead; /* Max bytes to read-ahead from disk */
	unsigned int gt_complain_secs;
	unsigned int gt_statfs_quantum;
	unsigned int gt_statfs_slow;
};

enum {
	SDF_JOURNAL_CHECKED	= 0,
	SDF_JOURNAL_LIVE	= 1,
	SDF_SHUTDOWN		= 2,
	SDF_NOBARRIERS		= 3,
	SDF_NORECOVERY		= 4,
	SDF_DEMOTE		= 5,
	SDF_NOJOURNALID		= 6,
	SDF_RORECOVERY		= 7, /* read only recovery */
	SDF_SKIP_DLM_UNLOCK	= 8,
};

#define GFS2_FSNAME_LEN		256

struct gfs2_inum_host {
	u64 no_formal_ino;
	u64 no_addr;
};

struct gfs2_sb_host {
	u32 sb_magic;
	u32 sb_type;
	u32 sb_format;

	u32 sb_fs_format;
	u32 sb_multihost_format;
	u32 sb_bsize;
	u32 sb_bsize_shift;

	struct gfs2_inum_host sb_master_dir;
	struct gfs2_inum_host sb_root_dir;

	char sb_lockproto[GFS2_LOCKNAME_LEN];
	char sb_locktable[GFS2_LOCKNAME_LEN];
};

/*
 * lm_mount() return values
 *
 * ls_jid - the journal ID this node should use
 * ls_first - this node is the first to mount the file system
 * ls_lockspace - lock module's context for this file system
 * ls_ops - lock module's functions
 */

struct lm_lockstruct {
	int ls_jid;
	unsigned int ls_first;
	const struct lm_lockops *ls_ops;
	dlm_lockspace_t *ls_dlm;

	int ls_recover_jid_done;   /* These two are deprecated, */
	int ls_recover_jid_status; /* used previously by gfs_controld */

	struct dlm_lksb ls_mounted_lksb; /* mounted_lock */
	struct dlm_lksb ls_control_lksb; /* control_lock */
	char ls_control_lvb[GDLM_LVB_SIZE]; /* control_lock lvb */
	struct completion ls_sync_wait; /* {control,mounted}_{lock,unlock} */
	char *ls_lvb_bits;

	spinlock_t ls_recover_spin; /* protects following fields */
	unsigned long ls_recover_flags; /* DFL_ */
	uint32_t ls_recover_mount; /* gen in first recover_done cb */
	uint32_t ls_recover_start; /* gen in last recover_done cb */
	uint32_t ls_recover_block; /* copy recover_start in last recover_prep */
	uint32_t ls_recover_size; /* size of recover_submit, recover_result */
	uint32_t *ls_recover_submit; /* gen in last recover_slot cb per jid */
	uint32_t *ls_recover_result; /* result of last jid recovery */
};

struct gfs2_pcpu_lkstats {
	/* One struct for each glock type */
	struct gfs2_lkstats lkstats[10];
};

struct gfs2_sbd {
	struct super_block *sd_vfs;
	struct gfs2_pcpu_lkstats __percpu *sd_lkstats;
	struct kobject sd_kobj;
	unsigned long sd_flags;	/* SDF_... */
	struct gfs2_sb_host sd_sb;

	/* Constants computed on mount */

	u32 sd_fsb2bb;
	u32 sd_fsb2bb_shift;
	u32 sd_diptrs;	/* Number of pointers in a dinode */
	u32 sd_inptrs;	/* Number of pointers in a indirect block */
	u32 sd_jbsize;	/* Size of a journaled data block */
	u32 sd_hash_bsize;	/* sizeof(exhash block) */
	u32 sd_hash_bsize_shift;
	u32 sd_hash_ptrs;	/* Number of pointers in a hash block */
	u32 sd_qc_per_block;
	u32 sd_blocks_per_bitmap;
	u32 sd_max_dirres;	/* Max blocks needed to add a directory entry */
	u32 sd_max_height;	/* Max height of a file's metadata tree */
	u64 sd_heightsize[GFS2_MAX_META_HEIGHT + 1];
	u32 sd_max_jheight; /* Max height of journaled file's meta tree */
	u64 sd_jheightsize[GFS2_MAX_META_HEIGHT + 1];

	struct gfs2_args sd_args;	/* Mount arguments */
	struct gfs2_tune sd_tune;	/* Filesystem tuning structure */

	/* Lock Stuff */

	struct lm_lockstruct sd_lockstruct;
	struct gfs2_holder sd_live_gh;
	struct gfs2_glock *sd_rename_gl;
	struct gfs2_glock *sd_trans_gl;
	wait_queue_head_t sd_glock_wait;
	atomic_t sd_glock_disposal;
	struct completion sd_locking_init;
	struct completion sd_wdack;
	struct delayed_work sd_control_work;

	/* Inode Stuff */

	struct dentry *sd_master_dir;
	struct dentry *sd_root_dir;

	struct inode *sd_jindex;
	struct inode *sd_statfs_inode;
	struct inode *sd_sc_inode;
	struct inode *sd_qc_inode;
	struct inode *sd_rindex;
	struct inode *sd_quota_inode;

	/* StatFS stuff */

	spinlock_t sd_statfs_spin;
	struct gfs2_statfs_change_host sd_statfs_master;
	struct gfs2_statfs_change_host sd_statfs_local;
	int sd_statfs_force_sync;

	/* Resource group stuff */

	int sd_rindex_uptodate;
	spinlock_t sd_rindex_spin;
	struct rb_root sd_rindex_tree;
	unsigned int sd_rgrps;
	unsigned int sd_max_rg_data;

	/* Journal index stuff */

	struct list_head sd_jindex_list;
	spinlock_t sd_jindex_spin;
	struct mutex sd_jindex_mutex;
	unsigned int sd_journals;

	struct gfs2_jdesc *sd_jdesc;
	struct gfs2_holder sd_journal_gh;
	struct gfs2_holder sd_jinode_gh;

	struct gfs2_holder sd_sc_gh;
	struct gfs2_holder sd_qc_gh;

	/* Daemon stuff */

	struct task_struct *sd_logd_process;
	struct task_struct *sd_quotad_process;

	/* Quota stuff */

	struct list_head sd_quota_list;
	atomic_t sd_quota_count;
	struct mutex sd_quota_mutex;
	struct mutex sd_quota_sync_mutex;
	wait_queue_head_t sd_quota_wait;
	struct list_head sd_trunc_list;
	spinlock_t sd_trunc_lock;

	unsigned int sd_quota_slots;
	unsigned long *sd_quota_bitmap;
	spinlock_t sd_bitmap_lock;

	u64 sd_quota_sync_gen;

	/* Log stuff */

	struct address_space sd_aspace;

	spinlock_t sd_log_lock;

	struct gfs2_trans *sd_log_tr;
	unsigned int sd_log_blks_reserved;
	int sd_log_commited_revoke;

	atomic_t sd_log_pinned;
	unsigned int sd_log_num_revoke;

	struct list_head sd_log_le_revoke;
	struct list_head sd_log_le_ordered;
	spinlock_t sd_ordered_lock;

	atomic_t sd_log_thresh1;
	atomic_t sd_log_thresh2;
	atomic_t sd_log_blks_free;
	wait_queue_head_t sd_log_waitq;
	wait_queue_head_t sd_logd_waitq;

	u64 sd_log_sequence;
	unsigned int sd_log_head;
	unsigned int sd_log_tail;
	int sd_log_idle;

	struct rw_semaphore sd_log_flush_lock;
	atomic_t sd_log_in_flight;
	struct bio *sd_log_bio;
	wait_queue_head_t sd_log_flush_wait;
	int sd_log_error;

	unsigned int sd_log_flush_head;
	u64 sd_log_flush_wrapped;

	spinlock_t sd_ail_lock;
	struct list_head sd_ail1_list;
	struct list_head sd_ail2_list;

	/* For quiescing the filesystem */
	struct gfs2_holder sd_freeze_gh;

	char sd_fsname[GFS2_FSNAME_LEN];
	char sd_table_name[GFS2_FSNAME_LEN];
	char sd_proto_name[GFS2_FSNAME_LEN];

	/* Debugging crud */

	unsigned long sd_last_warning;
	struct dentry *debugfs_dir;    /* debugfs directory */
	struct dentry *debugfs_dentry_glocks;
	struct dentry *debugfs_dentry_glstats;
	struct dentry *debugfs_dentry_sbstats;
};

static inline void gfs2_glstats_inc(struct gfs2_glock *gl, int which)
{
	gl->gl_stats.stats[which]++;
}

static inline void gfs2_sbstats_inc(const struct gfs2_glock *gl, int which)
{
	const struct gfs2_sbd *sdp = gl->gl_sbd;
	preempt_disable();
	this_cpu_ptr(sdp->sd_lkstats)->lkstats[gl->gl_name.ln_type].stats[which]++;
	preempt_enable();
}

#endif /* __INCORE_DOT_H__ */

