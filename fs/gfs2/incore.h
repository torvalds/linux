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
#include <linux/workqueue.h>
#include <linux/dlm.h>
#include <linux/buffer_head.h>

#define DIO_WAIT	0x00000010
#define DIO_METADATA	0x00000020
#define DIO_ALL		0x00000100

struct gfs2_log_operations;
struct gfs2_log_element;
struct gfs2_holder;
struct gfs2_glock;
struct gfs2_quota_data;
struct gfs2_trans;
struct gfs2_ail;
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
	void (*lo_add) (struct gfs2_sbd *sdp, struct gfs2_log_element *le);
	void (*lo_before_commit) (struct gfs2_sbd *sdp);
	void (*lo_after_commit) (struct gfs2_sbd *sdp, struct gfs2_ail *ai);
	void (*lo_before_scan) (struct gfs2_jdesc *jd,
				struct gfs2_log_header_host *head, int pass);
	int (*lo_scan_elements) (struct gfs2_jdesc *jd, unsigned int start,
				 struct gfs2_log_descriptor *ld, __be64 *ptr,
				 int pass);
	void (*lo_after_scan) (struct gfs2_jdesc *jd, int error, int pass);
	const char *lo_name;
};

struct gfs2_log_element {
	struct list_head le_list;
	const struct gfs2_log_operations *le_ops;
};

#define GBF_FULL 1

struct gfs2_bitmap {
	struct buffer_head *bi_bh;
	char *bi_clone;
	unsigned long bi_flags;
	u32 bi_offset;
	u32 bi_start;
	u32 bi_len;
};

struct gfs2_rgrpd {
	struct list_head rd_list;	/* Link with superblock */
	struct list_head rd_list_mru;
	struct gfs2_glock *rd_gl;	/* Glock for this rgrp */
	u64 rd_addr;			/* grp block disk address */
	u64 rd_data0;			/* first data location */
	u32 rd_length;			/* length of rgrp header in fs blocks */
	u32 rd_data;			/* num of data blocks in rgrp */
	u32 rd_bitbytes;		/* number of bytes in data bitmaps */
	u32 rd_free;
	u32 rd_free_clone;
	u32 rd_dinodes;
	u64 rd_igeneration;
	struct gfs2_bitmap *rd_bits;
	struct mutex rd_mutex;
	struct gfs2_log_element rd_le;
	struct gfs2_sbd *rd_sbd;
	unsigned int rd_bh_count;
	u32 rd_last_alloc;
	u32 rd_flags;
#define GFS2_RDF_CHECK		0x10000000 /* check for unlinked inodes */
#define GFS2_RDF_UPTODATE	0x20000000 /* rg is up to date */
#define GFS2_RDF_ERROR		0x40000000 /* error in rg */
#define GFS2_RDF_MASK		0xf0000000 /* mask for internal flags */
};

enum gfs2_state_bits {
	BH_Pinned = BH_PrivateStart,
	BH_Escaped = BH_PrivateStart + 1,
};

BUFFER_FNS(Pinned, pinned)
TAS_BUFFER_FNS(Pinned, pinned)
BUFFER_FNS(Escaped, escaped)
TAS_BUFFER_FNS(Escaped, escaped)

struct gfs2_bufdata {
	struct buffer_head *bd_bh;
	struct gfs2_glock *bd_gl;

	union {
		struct list_head list_tr;
		u64 blkno;
	} u;
#define bd_list_tr u.list_tr
#define bd_blkno u.blkno

	struct gfs2_log_element bd_le;

	struct gfs2_ail *bd_ail;
	struct list_head bd_ail_st_list;
	struct list_head bd_ail_gl_list;
};

/*
 * Internally, we prefix things with gdlm_ and GDLM_ (for gfs-dlm) since a
 * prefix of lock_dlm_ gets awkward.
 */

#define GDLM_STRNAME_BYTES	25
#define GDLM_LVB_SIZE		32

enum {
	DFL_BLOCK_LOCKS		= 0,
};

struct lm_lockname {
	u64 ln_number;
	unsigned int ln_type;
};

#define lm_name_equal(name1, name2) \
        (((name1)->ln_number == (name2)->ln_number) && \
         ((name1)->ln_type == (name2)->ln_type))


struct gfs2_glock_operations {
	void (*go_xmote_th) (struct gfs2_glock *gl);
	int (*go_xmote_bh) (struct gfs2_glock *gl, struct gfs2_holder *gh);
	void (*go_inval) (struct gfs2_glock *gl, int flags);
	int (*go_demote_ok) (const struct gfs2_glock *gl);
	int (*go_lock) (struct gfs2_holder *gh);
	void (*go_unlock) (struct gfs2_holder *gh);
	int (*go_dump)(struct seq_file *seq, const struct gfs2_glock *gl);
	void (*go_callback) (struct gfs2_glock *gl);
	const int go_type;
	const unsigned long go_min_hold_time;
	const unsigned long go_flags;
#define GLOF_ASPACE 1
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
};

struct gfs2_glock {
	struct hlist_node gl_list;
	unsigned long gl_flags;		/* GLF_... */
	struct lm_lockname gl_name;
	atomic_t gl_ref;

	spinlock_t gl_spin;

	unsigned int gl_state;
	unsigned int gl_target;
	unsigned int gl_reply;
	unsigned int gl_hash;
	unsigned int gl_req;
	unsigned int gl_demote_state; /* state requested by remote node */
	unsigned long gl_demote_time; /* time of first demote request */
	struct list_head gl_holders;

	const struct gfs2_glock_operations *gl_ops;
	char gl_strname[GDLM_STRNAME_BYTES];
	struct dlm_lksb gl_lksb;
	char gl_lvb[32];
	unsigned long gl_tchange;
	void *gl_object;

	struct list_head gl_lru;

	struct gfs2_sbd *gl_sbd;

	struct list_head gl_ail_list;
	atomic_t gl_ail_count;
	struct delayed_work gl_work;
	struct work_struct gl_delete;
};

#define GFS2_MIN_LVB_SIZE 32	/* Min size of LVB that gfs2 supports */

struct gfs2_alloc {
	/* Quota stuff */

	struct gfs2_quota_data *al_qd[2*MAXQUOTAS];
	struct gfs2_holder al_qd_ghs[2*MAXQUOTAS];
	unsigned int al_qd_num;

	u32 al_requested; /* Filled in by caller of gfs2_inplace_reserve() */
	u32 al_alloced; /* Filled in by gfs2_alloc_*() */

	/* Filled in by gfs2_inplace_reserve() */

	unsigned int al_line;
	char *al_file;
	struct gfs2_holder al_ri_gh;
	struct gfs2_holder al_rgd_gh;
	struct gfs2_rgrpd *al_rgd;

};

enum {
	GIF_INVALID		= 0,
	GIF_QD_LOCKED		= 1,
	GIF_SW_PAGED		= 3,
};


struct gfs2_inode {
	struct inode i_inode;
	u64 i_no_addr;
	u64 i_no_formal_ino;
	u64 i_generation;
	u64 i_eattr;
	loff_t i_disksize;
	unsigned long i_flags;		/* GIF_... */
	struct gfs2_glock *i_gl; /* Move into i_gh? */
	struct gfs2_holder i_iopen_gh;
	struct gfs2_holder i_gh; /* for prepare/commit_write only */
	struct gfs2_alloc *i_alloc;
	u64 i_goal;	/* goal block for allocations */
	struct rw_semaphore i_rw_mutex;
	struct list_head i_trunc_list;
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
	QDF_USER		= 0,
	QDF_CHANGE		= 1,
	QDF_LOCKED		= 2,
};

struct gfs2_quota_data {
	struct list_head qd_list;
	struct list_head qd_reclaim;

	atomic_t qd_count;

	u32 qd_id;
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
};

struct gfs2_trans {
	unsigned long tr_ip;

	unsigned int tr_blocks;
	unsigned int tr_revokes;
	unsigned int tr_reserved;

	struct gfs2_holder tr_t_gh;

	int tr_touched;

	unsigned int tr_num_buf;
	unsigned int tr_num_buf_new;
	unsigned int tr_num_databuf_new;
	unsigned int tr_num_buf_rm;
	unsigned int tr_num_databuf_rm;
	struct list_head tr_list_buf;

	unsigned int tr_num_revoke;
	unsigned int tr_num_revoke_rm;
};

struct gfs2_ail {
	struct list_head ai_list;

	unsigned int ai_first;
	struct list_head ai_ail1_list;
	struct list_head ai_ail2_list;

	u64 ai_sync_gen;
};

struct gfs2_journal_extent {
	struct list_head extent_list;

	unsigned int lblock; /* First logical block */
	u64 dblock; /* First disk block */
	u64 blocks;
};

struct gfs2_jdesc {
	struct list_head jd_list;
	struct list_head extent_list;
	struct work_struct jd_work;
	struct inode *jd_inode;
	unsigned long jd_flags;
#define JDF_RECOVERY 1
	unsigned int jd_jid;
	unsigned int jd_blocks;
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
	unsigned int ar_ignore_local_fs:1;	/* Ignore optimisations */
	unsigned int ar_localflocks:1;		/* Let the VFS do flock|fcntl */
	unsigned int ar_localcaching:1;		/* Local caching */
	unsigned int ar_debug:1;		/* Oops on errors */
	unsigned int ar_upgrade:1;		/* Upgrade ondisk format */
	unsigned int ar_posix_acl:1;		/* Enable posix acls */
	unsigned int ar_quota:2;		/* off/account/on */
	unsigned int ar_suiddir:1;		/* suiddir support */
	unsigned int ar_data:2;			/* ordered/writeback */
	unsigned int ar_meta:1;			/* mount metafs */
	unsigned int ar_discard:1;		/* discard requests */
	unsigned int ar_errors:2;               /* errors=withdraw | panic */
	unsigned int ar_nobarrier:1;            /* do not send barriers */
	int ar_commit;				/* Commit interval */
	int ar_statfs_quantum;			/* The fast statfs interval */
	int ar_quota_quantum;			/* The quota interval */
	int ar_statfs_percent;			/* The % change to force sync */
};

struct gfs2_tune {
	spinlock_t gt_spin;

	unsigned int gt_logd_secs;

	unsigned int gt_quota_simul_sync; /* Max quotavals to sync at once */
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
	u8 sb_uuid[16];
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
	unsigned int ls_jid;
	unsigned int ls_first;
	unsigned int ls_first_done;
	unsigned int ls_nodir;
	const struct lm_lockops *ls_ops;
	unsigned long ls_flags;
	dlm_lockspace_t *ls_dlm;

	int ls_recover_jid_done;
	int ls_recover_jid_status;
};

struct gfs2_sbd {
	struct super_block *sd_vfs;
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
	struct mutex sd_rindex_mutex;
	struct list_head sd_rindex_list;
	struct list_head sd_rindex_mru_list;
	struct gfs2_rgrpd *sd_rindex_forward;
	unsigned int sd_rgrps;

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
	wait_queue_head_t sd_quota_wait;
	struct list_head sd_trunc_list;
	spinlock_t sd_trunc_lock;

	unsigned int sd_quota_slots;
	unsigned int sd_quota_chunks;
	unsigned char **sd_quota_bitmap;

	u64 sd_quota_sync_gen;

	/* Log stuff */

	spinlock_t sd_log_lock;

	unsigned int sd_log_blks_reserved;
	unsigned int sd_log_commited_buf;
	unsigned int sd_log_commited_databuf;
	int sd_log_commited_revoke;

	atomic_t sd_log_pinned;
	unsigned int sd_log_num_buf;
	unsigned int sd_log_num_revoke;
	unsigned int sd_log_num_rg;
	unsigned int sd_log_num_databuf;

	struct list_head sd_log_le_buf;
	struct list_head sd_log_le_revoke;
	struct list_head sd_log_le_rg;
	struct list_head sd_log_le_databuf;
	struct list_head sd_log_le_ordered;

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
	wait_queue_head_t sd_log_flush_wait;

	unsigned int sd_log_flush_head;
	u64 sd_log_flush_wrapped;

	struct list_head sd_ail1_list;
	struct list_head sd_ail2_list;
	u64 sd_ail_sync_gen;

	/* Replay stuff */

	struct list_head sd_revoke_list;
	unsigned int sd_replay_tail;

	unsigned int sd_found_blocks;
	unsigned int sd_found_revokes;
	unsigned int sd_replayed_blocks;

	/* For quiescing the filesystem */

	struct gfs2_holder sd_freeze_gh;
	struct mutex sd_freeze_lock;
	unsigned int sd_freeze_count;

	char sd_fsname[GFS2_FSNAME_LEN];
	char sd_table_name[GFS2_FSNAME_LEN];
	char sd_proto_name[GFS2_FSNAME_LEN];

	/* Debugging crud */

	unsigned long sd_last_warning;
	struct dentry *debugfs_dir;    /* debugfs directory */
	struct dentry *debugfs_dentry_glocks; /* for debugfs */
};

#endif /* __INCORE_DOT_H__ */

