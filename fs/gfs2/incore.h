/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __INCORE_DOT_H__
#define __INCORE_DOT_H__

#include <linux/fs.h>

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

typedef void (*gfs2_glop_bh_t) (struct gfs2_glock *gl, unsigned int ret);

/*
 * Structure of operations that are associated with each
 * type of element in the log.
 */

struct gfs2_log_operations {
	void (*lo_add) (struct gfs2_sbd *sdp, struct gfs2_log_element *le);
	void (*lo_incore_commit) (struct gfs2_sbd *sdp, struct gfs2_trans *tr);
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

struct gfs2_bitmap {
	struct buffer_head *bi_bh;
	char *bi_clone;
	u32 bi_offset;
	u32 bi_start;
	u32 bi_len;
};

struct gfs2_rgrpd {
	struct list_head rd_list;	/* Link with superblock */
	struct list_head rd_list_mru;
	struct list_head rd_recent;	/* Recently used rgrps */
	struct gfs2_glock *rd_gl;	/* Glock for this rgrp */
	struct gfs2_rindex_host rd_ri;
	struct gfs2_rgrp_host rd_rg;
	u64 rd_rg_vn;
	struct gfs2_bitmap *rd_bits;
	unsigned int rd_bh_count;
	struct mutex rd_mutex;
	u32 rd_free_clone;
	struct gfs2_log_element rd_le;
	u32 rd_last_alloc_data;
	u32 rd_last_alloc_meta;
	struct gfs2_sbd *rd_sbd;
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

	struct list_head bd_list_tr;
	struct gfs2_log_element bd_le;

	struct gfs2_ail *bd_ail;
	struct list_head bd_ail_st_list;
	struct list_head bd_ail_gl_list;
};

struct gfs2_glock_operations {
	void (*go_xmote_th) (struct gfs2_glock *gl, unsigned int state, int flags);
	void (*go_xmote_bh) (struct gfs2_glock *gl);
	void (*go_drop_th) (struct gfs2_glock *gl);
	void (*go_drop_bh) (struct gfs2_glock *gl);
	void (*go_sync) (struct gfs2_glock *gl);
	void (*go_inval) (struct gfs2_glock *gl, int flags);
	int (*go_demote_ok) (struct gfs2_glock *gl);
	int (*go_lock) (struct gfs2_holder *gh);
	void (*go_unlock) (struct gfs2_holder *gh);
	void (*go_callback) (struct gfs2_glock *gl, unsigned int state);
	void (*go_greedy) (struct gfs2_glock *gl);
	const int go_type;
};

enum {
	/* Actions */
	HIF_MUTEX		= 0,
	HIF_PROMOTE		= 1,
	HIF_DEMOTE		= 2,
	HIF_GREEDY		= 3,

	/* States */
	HIF_ALLOCED		= 4,
	HIF_DEALLOC		= 5,
	HIF_HOLDER		= 6,
	HIF_FIRST		= 7,
	HIF_ABORTED		= 9,
};

struct gfs2_holder {
	struct list_head gh_list;

	struct gfs2_glock *gh_gl;
	struct task_struct *gh_owner;
	unsigned int gh_state;
	unsigned gh_flags;

	int gh_error;
	unsigned long gh_iflags;
	struct completion gh_wait;
	unsigned long gh_ip;
};

enum {
	GLF_LOCK		= 1,
	GLF_STICKY		= 2,
	GLF_PREFETCH		= 3,
	GLF_DIRTY		= 5,
	GLF_SKIP_WAITERS2	= 6,
	GLF_GREEDY		= 7,
};

struct gfs2_glock {
	struct hlist_node gl_list;
	unsigned long gl_flags;		/* GLF_... */
	struct lm_lockname gl_name;
	atomic_t gl_ref;

	spinlock_t gl_spin;

	unsigned int gl_state;
	unsigned int gl_hash;
	struct task_struct *gl_owner;
	unsigned long gl_ip;
	struct list_head gl_holders;
	struct list_head gl_waiters1;	/* HIF_MUTEX */
	struct list_head gl_waiters2;	/* HIF_DEMOTE, HIF_GREEDY */
	struct list_head gl_waiters3;	/* HIF_PROMOTE */

	const struct gfs2_glock_operations *gl_ops;

	struct gfs2_holder *gl_req_gh;
	gfs2_glop_bh_t gl_req_bh;

	void *gl_lock;
	char *gl_lvb;
	atomic_t gl_lvb_count;

	u64 gl_vn;
	unsigned long gl_stamp;
	void *gl_object;

	struct list_head gl_reclaim;

	struct gfs2_sbd *gl_sbd;

	struct inode *gl_aspace;
	struct gfs2_log_element gl_le;
	struct list_head gl_ail_list;
	atomic_t gl_ail_count;
};

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
	GIF_PAGED		= 2,
	GIF_SW_PAGED		= 3,
};

struct gfs2_inode {
	struct inode i_inode;
	struct gfs2_inum_host i_num;

	unsigned long i_flags;		/* GIF_... */

	struct gfs2_dinode_host i_di; /* To be replaced by ref to block */

	struct gfs2_glock *i_gl; /* Move into i_gh? */
	struct gfs2_holder i_iopen_gh;
	struct gfs2_holder i_gh; /* for prepare/commit_write only */
	struct gfs2_alloc i_alloc;
	u64 i_last_rg_alloc;

	spinlock_t i_spin;
	struct rw_semaphore i_rw_mutex;
	unsigned int i_greedy;
	unsigned long i_last_pfault;

	struct buffer_head *i_cache[GFS2_MAX_META_HEIGHT];
};

/*
 * Since i_inode is the first element of struct gfs2_inode,
 * this is effectively a cast.
 */
static inline struct gfs2_inode *GFS2_I(struct inode *inode)
{
	return container_of(inode, struct gfs2_inode, i_inode);
}

/* To be removed? */
static inline struct gfs2_sbd *GFS2_SB(struct inode *inode)
{
	return inode->i_sb->s_fs_info;
}

enum {
	GFF_DID_DIRECT_ALLOC	= 0,
	GFF_EXLOCK = 1,
};

struct gfs2_file {
	unsigned long f_flags;		/* GFF_... */
	struct mutex f_fl_mutex;
	struct gfs2_holder f_fl_gh;
};

struct gfs2_revoke {
	struct gfs2_log_element rv_le;
	u64 rv_blkno;
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

struct gfs2_quota_lvb {
        __be32 qb_magic;
        u32 __pad;
        __be64 qb_limit;      /* Hard limit of # blocks to alloc */
        __be64 qb_warn;       /* Warn user when alloc is above this # */
        __be64 qb_value;       /* Current # blocks allocated */
};

struct gfs2_quota_data {
	struct list_head qd_list;
	unsigned int qd_count;

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
	unsigned long qd_last_touched;
};

struct gfs2_log_buf {
	struct list_head lb_list;
	struct buffer_head *lb_bh;
	struct buffer_head *lb_real;
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
	unsigned int tr_num_buf_rm;
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

struct gfs2_jdesc {
	struct list_head jd_list;

	struct inode *jd_inode;
	unsigned int jd_jid;
	int jd_dirty;

	unsigned int jd_blocks;
};

#define GFS2_GLOCKD_DEFAULT	1
#define GFS2_GLOCKD_MAX		16

#define GFS2_QUOTA_DEFAULT	GFS2_QUOTA_OFF
#define GFS2_QUOTA_OFF		0
#define GFS2_QUOTA_ACCOUNT	1
#define GFS2_QUOTA_ON		2

#define GFS2_DATA_DEFAULT	GFS2_DATA_ORDERED
#define GFS2_DATA_WRITEBACK	1
#define GFS2_DATA_ORDERED	2

struct gfs2_args {
	char ar_lockproto[GFS2_LOCKNAME_LEN]; /* Name of the Lock Protocol */
	char ar_locktable[GFS2_LOCKNAME_LEN]; /* Name of the Lock Table */
	char ar_hostdata[GFS2_LOCKNAME_LEN]; /* Host specific data */
	int ar_spectator; /* Don't get a journal because we're always RO */
	int ar_ignore_local_fs; /* Don't optimize even if local_fs is 1 */
	int ar_localflocks; /* Let the VFS do flock|fcntl locks for us */
	int ar_localcaching; /* Local-style caching (dangerous on multihost) */
	int ar_debug; /* Oops on errors instead of trying to be graceful */
	int ar_upgrade; /* Upgrade ondisk/multihost format */
	unsigned int ar_num_glockd; /* Number of glockd threads */
	int ar_posix_acl; /* Enable posix acls */
	int ar_quota; /* off/account/on */
	int ar_suiddir; /* suiddir support */
	int ar_data; /* ordered/writeback */
};

struct gfs2_tune {
	spinlock_t gt_spin;

	unsigned int gt_ilimit;
	unsigned int gt_ilimit_tries;
	unsigned int gt_ilimit_min;
	unsigned int gt_demote_secs; /* Cache retention for unheld glock */
	unsigned int gt_incore_log_blocks;
	unsigned int gt_log_flush_secs;
	unsigned int gt_jindex_refresh_secs; /* Check for new journal index */

	unsigned int gt_scand_secs;
	unsigned int gt_recoverd_secs;
	unsigned int gt_logd_secs;
	unsigned int gt_quotad_secs;

	unsigned int gt_quota_simul_sync; /* Max quotavals to sync at once */
	unsigned int gt_quota_warn_period; /* Secs between quota warn msgs */
	unsigned int gt_quota_scale_num; /* Numerator */
	unsigned int gt_quota_scale_den; /* Denominator */
	unsigned int gt_quota_cache_secs;
	unsigned int gt_quota_quantum; /* Secs between syncs to quota file */
	unsigned int gt_atime_quantum; /* Min secs between atime updates */
	unsigned int gt_new_files_jdata;
	unsigned int gt_new_files_directio;
	unsigned int gt_max_atomic_write; /* Split big writes into this size */
	unsigned int gt_max_readahead; /* Max bytes to read-ahead from disk */
	unsigned int gt_lockdump_size;
	unsigned int gt_stall_secs; /* Detects trouble! */
	unsigned int gt_complain_secs;
	unsigned int gt_reclaim_limit; /* Max num of glocks in reclaim list */
	unsigned int gt_entries_per_readdir;
	unsigned int gt_prefetch_secs; /* Usage window for prefetched glocks */
	unsigned int gt_greedy_default;
	unsigned int gt_greedy_quantum;
	unsigned int gt_greedy_max;
	unsigned int gt_statfs_quantum;
	unsigned int gt_statfs_slow;
};

enum {
	SDF_JOURNAL_CHECKED	= 0,
	SDF_JOURNAL_LIVE	= 1,
	SDF_SHUTDOWN		= 2,
	SDF_NOATIME		= 3,
};

#define GFS2_FSNAME_LEN		256

struct gfs2_sbd {
	struct super_block *sd_vfs;
	struct super_block *sd_vfs_meta;
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
	u64 sd_heightsize[GFS2_MAX_META_HEIGHT];
	u32 sd_max_jheight; /* Max height of journaled file's meta tree */
	u64 sd_jheightsize[GFS2_MAX_META_HEIGHT];

	struct gfs2_args sd_args;	/* Mount arguments */
	struct gfs2_tune sd_tune;	/* Filesystem tuning structure */

	/* Lock Stuff */

	struct lm_lockstruct sd_lockstruct;
	struct list_head sd_reclaim_list;
	spinlock_t sd_reclaim_lock;
	wait_queue_head_t sd_reclaim_wq;
	atomic_t sd_reclaim_count;
	struct gfs2_holder sd_live_gh;
	struct gfs2_glock *sd_rename_gl;
	struct gfs2_glock *sd_trans_gl;

	/* Inode Stuff */

	struct inode *sd_master_dir;
	struct inode *sd_jindex;
	struct inode *sd_inum_inode;
	struct inode *sd_statfs_inode;
	struct inode *sd_ir_inode;
	struct inode *sd_sc_inode;
	struct inode *sd_qc_inode;
	struct inode *sd_rindex;
	struct inode *sd_quota_inode;

	/* Inum stuff */

	struct mutex sd_inum_mutex;

	/* StatFS stuff */

	spinlock_t sd_statfs_spin;
	struct mutex sd_statfs_mutex;
	struct gfs2_statfs_change_host sd_statfs_master;
	struct gfs2_statfs_change_host sd_statfs_local;
	unsigned long sd_statfs_sync_time;

	/* Resource group stuff */

	u64 sd_rindex_vn;
	spinlock_t sd_rindex_spin;
	struct mutex sd_rindex_mutex;
	struct list_head sd_rindex_list;
	struct list_head sd_rindex_mru_list;
	struct list_head sd_rindex_recent_list;
	struct gfs2_rgrpd *sd_rindex_forward;
	unsigned int sd_rgrps;

	/* Journal index stuff */

	struct list_head sd_jindex_list;
	spinlock_t sd_jindex_spin;
	struct mutex sd_jindex_mutex;
	unsigned int sd_journals;
	unsigned long sd_jindex_refresh_time;

	struct gfs2_jdesc *sd_jdesc;
	struct gfs2_holder sd_journal_gh;
	struct gfs2_holder sd_jinode_gh;

	struct gfs2_holder sd_ir_gh;
	struct gfs2_holder sd_sc_gh;
	struct gfs2_holder sd_qc_gh;

	/* Daemon stuff */

	struct task_struct *sd_scand_process;
	struct task_struct *sd_recoverd_process;
	struct task_struct *sd_logd_process;
	struct task_struct *sd_quotad_process;
	struct task_struct *sd_glockd_process[GFS2_GLOCKD_MAX];
	unsigned int sd_glockd_num;

	/* Quota stuff */

	struct list_head sd_quota_list;
	atomic_t sd_quota_count;
	spinlock_t sd_quota_spin;
	struct mutex sd_quota_mutex;

	unsigned int sd_quota_slots;
	unsigned int sd_quota_chunks;
	unsigned char **sd_quota_bitmap;

	u64 sd_quota_sync_gen;
	unsigned long sd_quota_sync_time;

	/* Log stuff */

	spinlock_t sd_log_lock;

	unsigned int sd_log_blks_reserved;
	unsigned int sd_log_commited_buf;
	unsigned int sd_log_commited_revoke;

	unsigned int sd_log_num_gl;
	unsigned int sd_log_num_buf;
	unsigned int sd_log_num_revoke;
	unsigned int sd_log_num_rg;
	unsigned int sd_log_num_databuf;
	unsigned int sd_log_num_jdata;
	unsigned int sd_log_num_hdrs;

	struct list_head sd_log_le_gl;
	struct list_head sd_log_le_buf;
	struct list_head sd_log_le_revoke;
	struct list_head sd_log_le_rg;
	struct list_head sd_log_le_databuf;

	unsigned int sd_log_blks_free;
	struct mutex sd_log_reserve_mutex;

	u64 sd_log_sequence;
	unsigned int sd_log_head;
	unsigned int sd_log_tail;
	int sd_log_idle;

	unsigned long sd_log_flush_time;
	struct rw_semaphore sd_log_flush_lock;
	struct list_head sd_log_flush_list;

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

	/* Counters */

	atomic_t sd_glock_count;
	atomic_t sd_glock_held_count;
	atomic_t sd_inode_count;
	atomic_t sd_reclaimed;

	char sd_fsname[GFS2_FSNAME_LEN];
	char sd_table_name[GFS2_FSNAME_LEN];
	char sd_proto_name[GFS2_FSNAME_LEN];

	/* Debugging crud */

	unsigned long sd_last_warning;
	struct vfsmount *sd_gfs2mnt;
};

#endif /* __INCORE_DOT_H__ */

