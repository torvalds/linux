/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _LINUX_QUOTA_
#define _LINUX_QUOTA_

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/percpu_counter.h>

#include <linux/dqblk_xfs.h>
#include <linux/dqblk_v1.h>
#include <linux/dqblk_v2.h>

#include <linux/atomic.h>
#include <linux/uidgid.h>
#include <linux/projid.h>
#include <uapi/linux/quota.h>

#undef USRQUOTA
#undef GRPQUOTA
#undef PRJQUOTA
enum quota_type {
	USRQUOTA = 0,		/* element used for user quotas */
	GRPQUOTA = 1,		/* element used for group quotas */
	PRJQUOTA = 2,		/* element used for project quotas */
};

/* Masks for quota types when used as a bitmask */
#define QTYPE_MASK_USR (1 << USRQUOTA)
#define QTYPE_MASK_GRP (1 << GRPQUOTA)
#define QTYPE_MASK_PRJ (1 << PRJQUOTA)

typedef __kernel_uid32_t qid_t; /* Type in which we store ids in memory */
typedef long long qsize_t;	/* Type in which we store sizes */

struct kqid {			/* Type in which we store the quota identifier */
	union {
		kuid_t uid;
		kgid_t gid;
		kprojid_t projid;
	};
	enum quota_type type;  /* USRQUOTA (uid) or GRPQUOTA (gid) or PRJQUOTA (projid) */
};

extern bool qid_eq(struct kqid left, struct kqid right);
extern bool qid_lt(struct kqid left, struct kqid right);
extern qid_t from_kqid(struct user_namespace *to, struct kqid qid);
extern qid_t from_kqid_munged(struct user_namespace *to, struct kqid qid);
extern bool qid_valid(struct kqid qid);

/**
 *	make_kqid - Map a user-namespace, type, qid tuple into a kqid.
 *	@from: User namespace that the qid is in
 *	@type: The type of quota
 *	@qid: Quota identifier
 *
 *	Maps a user-namespace, type qid tuple into a kernel internal
 *	kqid, and returns that kqid.
 *
 *	When there is no mapping defined for the user-namespace, type,
 *	qid tuple an invalid kqid is returned.  Callers are expected to
 *	test for and handle handle invalid kqids being returned.
 *	Invalid kqids may be tested for using qid_valid().
 */
static inline struct kqid make_kqid(struct user_namespace *from,
				    enum quota_type type, qid_t qid)
{
	struct kqid kqid;

	kqid.type = type;
	switch (type) {
	case USRQUOTA:
		kqid.uid = make_kuid(from, qid);
		break;
	case GRPQUOTA:
		kqid.gid = make_kgid(from, qid);
		break;
	case PRJQUOTA:
		kqid.projid = make_kprojid(from, qid);
		break;
	default:
		BUG();
	}
	return kqid;
}

/**
 *	make_kqid_invalid - Explicitly make an invalid kqid
 *	@type: The type of quota identifier
 *
 *	Returns an invalid kqid with the specified type.
 */
static inline struct kqid make_kqid_invalid(enum quota_type type)
{
	struct kqid kqid;

	kqid.type = type;
	switch (type) {
	case USRQUOTA:
		kqid.uid = INVALID_UID;
		break;
	case GRPQUOTA:
		kqid.gid = INVALID_GID;
		break;
	case PRJQUOTA:
		kqid.projid = INVALID_PROJID;
		break;
	default:
		BUG();
	}
	return kqid;
}

/**
 *	make_kqid_uid - Make a kqid from a kuid
 *	@uid: The kuid to make the quota identifier from
 */
static inline struct kqid make_kqid_uid(kuid_t uid)
{
	struct kqid kqid;
	kqid.type = USRQUOTA;
	kqid.uid = uid;
	return kqid;
}

/**
 *	make_kqid_gid - Make a kqid from a kgid
 *	@gid: The kgid to make the quota identifier from
 */
static inline struct kqid make_kqid_gid(kgid_t gid)
{
	struct kqid kqid;
	kqid.type = GRPQUOTA;
	kqid.gid = gid;
	return kqid;
}

/**
 *	make_kqid_projid - Make a kqid from a projid
 *	@projid: The kprojid to make the quota identifier from
 */
static inline struct kqid make_kqid_projid(kprojid_t projid)
{
	struct kqid kqid;
	kqid.type = PRJQUOTA;
	kqid.projid = projid;
	return kqid;
}


extern spinlock_t dq_data_lock;

/* Maximal numbers of writes for quota operation (insert/delete/update)
 * (over VFS all formats) */
#define DQUOT_INIT_ALLOC max(V1_INIT_ALLOC, V2_INIT_ALLOC)
#define DQUOT_INIT_REWRITE max(V1_INIT_REWRITE, V2_INIT_REWRITE)
#define DQUOT_DEL_ALLOC max(V1_DEL_ALLOC, V2_DEL_ALLOC)
#define DQUOT_DEL_REWRITE max(V1_DEL_REWRITE, V2_DEL_REWRITE)

/*
 * Data for one user/group kept in memory
 */
struct mem_dqblk {
	qsize_t dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	qsize_t dqb_bsoftlimit;	/* preferred limit on disk blks */
	qsize_t dqb_curspace;	/* current used space */
	qsize_t dqb_rsvspace;   /* current reserved space for delalloc*/
	qsize_t dqb_ihardlimit;	/* absolute limit on allocated inodes */
	qsize_t dqb_isoftlimit;	/* preferred inode limit */
	qsize_t dqb_curinodes;	/* current # allocated inodes */
	time_t dqb_btime;	/* time limit for excessive disk use */
	time_t dqb_itime;	/* time limit for excessive inode use */
};

/*
 * Data for one quotafile kept in memory
 */
struct quota_format_type;

struct mem_dqinfo {
	struct quota_format_type *dqi_format;
	int dqi_fmt_id;		/* Id of the dqi_format - used when turning
				 * quotas on after remount RW */
	struct list_head dqi_dirty_list;	/* List of dirty dquots */
	unsigned long dqi_flags;
	unsigned int dqi_bgrace;
	unsigned int dqi_igrace;
	qsize_t dqi_max_spc_limit;
	qsize_t dqi_max_ino_limit;
	void *dqi_priv;
};

struct super_block;

/* Mask for flags passed to userspace */
#define DQF_GETINFO_MASK (DQF_ROOT_SQUASH | DQF_SYS_FILE)
/* Mask for flags modifiable from userspace */
#define DQF_SETINFO_MASK DQF_ROOT_SQUASH

enum {
	DQF_INFO_DIRTY_B = DQF_PRIVATE,
};
#define DQF_INFO_DIRTY (1 << DQF_INFO_DIRTY_B)	/* Is info dirty? */

extern void mark_info_dirty(struct super_block *sb, int type);
static inline int info_dirty(struct mem_dqinfo *info)
{
	return test_bit(DQF_INFO_DIRTY_B, &info->dqi_flags);
}

enum {
	DQST_LOOKUPS,
	DQST_DROPS,
	DQST_READS,
	DQST_WRITES,
	DQST_CACHE_HITS,
	DQST_ALLOC_DQUOTS,
	DQST_FREE_DQUOTS,
	DQST_SYNCS,
	_DQST_DQSTAT_LAST
};

struct dqstats {
	int stat[_DQST_DQSTAT_LAST];
	struct percpu_counter counter[_DQST_DQSTAT_LAST];
};

extern struct dqstats *dqstats_pcpu;
extern struct dqstats dqstats;

static inline void dqstats_inc(unsigned int type)
{
	percpu_counter_inc(&dqstats.counter[type]);
}

static inline void dqstats_dec(unsigned int type)
{
	percpu_counter_dec(&dqstats.counter[type]);
}

#define DQ_MOD_B	0	/* dquot modified since read */
#define DQ_BLKS_B	1	/* uid/gid has been warned about blk limit */
#define DQ_INODES_B	2	/* uid/gid has been warned about inode limit */
#define DQ_FAKE_B	3	/* no limits only usage */
#define DQ_READ_B	4	/* dquot was read into memory */
#define DQ_ACTIVE_B	5	/* dquot is active (dquot_release not called) */
#define DQ_LASTSET_B	6	/* Following 6 bits (see QIF_) are reserved\
				 * for the mask of entries set via SETQUOTA\
				 * quotactl. They are set under dq_data_lock\
				 * and the quota format handling dquot can\
				 * clear them when it sees fit. */

struct dquot {
	struct hlist_node dq_hash;	/* Hash list in memory */
	struct list_head dq_inuse;	/* List of all quotas */
	struct list_head dq_free;	/* Free list element */
	struct list_head dq_dirty;	/* List of dirty dquots */
	struct mutex dq_lock;		/* dquot IO lock */
	atomic_t dq_count;		/* Use count */
	wait_queue_head_t dq_wait_unused;	/* Wait queue for dquot to become unused */
	struct super_block *dq_sb;	/* superblock this applies to */
	struct kqid dq_id;		/* ID this applies to (uid, gid, projid) */
	loff_t dq_off;			/* Offset of dquot on disk */
	unsigned long dq_flags;		/* See DQ_* */
	struct mem_dqblk dq_dqb;	/* Diskquota usage */
};

/* Operations which must be implemented by each quota format */
struct quota_format_ops {
	int (*check_quota_file)(struct super_block *sb, int type);	/* Detect whether file is in our format */
	int (*read_file_info)(struct super_block *sb, int type);	/* Read main info about file - called on quotaon() */
	int (*write_file_info)(struct super_block *sb, int type);	/* Write main info about file */
	int (*free_file_info)(struct super_block *sb, int type);	/* Called on quotaoff() */
	int (*read_dqblk)(struct dquot *dquot);		/* Read structure for one user */
	int (*commit_dqblk)(struct dquot *dquot);	/* Write structure for one user */
	int (*release_dqblk)(struct dquot *dquot);	/* Called when last reference to dquot is being dropped */
};

/* Operations working with dquots */
struct dquot_operations {
	int (*write_dquot) (struct dquot *);		/* Ordinary dquot write */
	struct dquot *(*alloc_dquot)(struct super_block *, int);	/* Allocate memory for new dquot */
	void (*destroy_dquot)(struct dquot *);		/* Free memory for dquot */
	int (*acquire_dquot) (struct dquot *);		/* Quota is going to be created on disk */
	int (*release_dquot) (struct dquot *);		/* Quota is going to be deleted from disk */
	int (*mark_dirty) (struct dquot *);		/* Dquot is marked dirty */
	int (*write_info) (struct super_block *, int);	/* Write of quota "superblock" */
	/* get reserved quota for delayed alloc, value returned is managed by
	 * quota code only */
	qsize_t *(*get_reserved_space) (struct inode *);
	int (*get_projid) (struct inode *, kprojid_t *);/* Get project ID */
};

struct path;

/* Structure for communicating via ->get_dqblk() & ->set_dqblk() */
struct qc_dqblk {
	int d_fieldmask;	/* mask of fields to change in ->set_dqblk() */
	u64 d_spc_hardlimit;	/* absolute limit on used space */
	u64 d_spc_softlimit;	/* preferred limit on used space */
	u64 d_ino_hardlimit;	/* maximum # allocated inodes */
	u64 d_ino_softlimit;	/* preferred inode limit */
	u64 d_space;		/* Space owned by the user */
	u64 d_ino_count;	/* # inodes owned by the user */
	s64 d_ino_timer;	/* zero if within inode limits */
				/* if not, we refuse service */
	s64 d_spc_timer;	/* similar to above; for space */
	int d_ino_warns;	/* # warnings issued wrt num inodes */
	int d_spc_warns;	/* # warnings issued wrt used space */
	u64 d_rt_spc_hardlimit;	/* absolute limit on realtime space */
	u64 d_rt_spc_softlimit;	/* preferred limit on RT space */
	u64 d_rt_space;		/* realtime space owned */
	s64 d_rt_spc_timer;	/* similar to above; for RT space */
	int d_rt_spc_warns;	/* # warnings issued wrt RT space */
};

/*
 * Field specifiers for ->set_dqblk() in struct qc_dqblk and also for
 * ->set_info() in struct qc_info
 */
#define	QC_INO_SOFT	(1<<0)
#define	QC_INO_HARD	(1<<1)
#define	QC_SPC_SOFT	(1<<2)
#define	QC_SPC_HARD	(1<<3)
#define	QC_RT_SPC_SOFT	(1<<4)
#define	QC_RT_SPC_HARD	(1<<5)
#define QC_LIMIT_MASK (QC_INO_SOFT | QC_INO_HARD | QC_SPC_SOFT | QC_SPC_HARD | \
		       QC_RT_SPC_SOFT | QC_RT_SPC_HARD)
#define	QC_SPC_TIMER	(1<<6)
#define	QC_INO_TIMER	(1<<7)
#define	QC_RT_SPC_TIMER	(1<<8)
#define QC_TIMER_MASK (QC_SPC_TIMER | QC_INO_TIMER | QC_RT_SPC_TIMER)
#define	QC_SPC_WARNS	(1<<9)
#define	QC_INO_WARNS	(1<<10)
#define	QC_RT_SPC_WARNS	(1<<11)
#define QC_WARNS_MASK (QC_SPC_WARNS | QC_INO_WARNS | QC_RT_SPC_WARNS)
#define	QC_SPACE	(1<<12)
#define	QC_INO_COUNT	(1<<13)
#define	QC_RT_SPACE	(1<<14)
#define QC_ACCT_MASK (QC_SPACE | QC_INO_COUNT | QC_RT_SPACE)
#define QC_FLAGS	(1<<15)

#define QCI_SYSFILE		(1 << 0)	/* Quota file is hidden from userspace */
#define QCI_ROOT_SQUASH		(1 << 1)	/* Root squash turned on */
#define QCI_ACCT_ENABLED	(1 << 2)	/* Quota accounting enabled */
#define QCI_LIMITS_ENFORCED	(1 << 3)	/* Quota limits enforced */

/* Structures for communicating via ->get_state */
struct qc_type_state {
	unsigned int flags;		/* Flags QCI_* */
	unsigned int spc_timelimit;	/* Time after which space softlimit is
					 * enforced */
	unsigned int ino_timelimit;	/* Ditto for inode softlimit */
	unsigned int rt_spc_timelimit;	/* Ditto for real-time space */
	unsigned int spc_warnlimit;	/* Limit for number of space warnings */
	unsigned int ino_warnlimit;	/* Ditto for inodes */
	unsigned int rt_spc_warnlimit;	/* Ditto for real-time space */
	unsigned long long ino;		/* Inode number of quota file */
	blkcnt_t blocks;		/* Number of 512-byte blocks in the file */
	blkcnt_t nextents;		/* Number of extents in the file */
};

struct qc_state {
	unsigned int s_incoredqs;	/* Number of dquots in core */
	/*
	 * Per quota type information. The array should really have
	 * max(MAXQUOTAS, XQM_MAXQUOTAS) entries. BUILD_BUG_ON in
	 * quota_getinfo() makes sure XQM_MAXQUOTAS is large enough.  Once VFS
	 * supports project quotas, this can be changed to MAXQUOTAS
	 */
	struct qc_type_state s_state[XQM_MAXQUOTAS];
};

/* Structure for communicating via ->set_info */
struct qc_info {
	int i_fieldmask;	/* mask of fields to change in ->set_info() */
	unsigned int i_flags;		/* Flags QCI_* */
	unsigned int i_spc_timelimit;	/* Time after which space softlimit is
					 * enforced */
	unsigned int i_ino_timelimit;	/* Ditto for inode softlimit */
	unsigned int i_rt_spc_timelimit;/* Ditto for real-time space */
	unsigned int i_spc_warnlimit;	/* Limit for number of space warnings */
	unsigned int i_ino_warnlimit;	/* Limit for number of inode warnings */
	unsigned int i_rt_spc_warnlimit;	/* Ditto for real-time space */
};

/* Operations handling requests from userspace */
struct quotactl_ops {
	int (*quota_on)(struct super_block *, int, int, struct path *);
	int (*quota_off)(struct super_block *, int);
	int (*quota_enable)(struct super_block *, unsigned int);
	int (*quota_disable)(struct super_block *, unsigned int);
	int (*quota_sync)(struct super_block *, int);
	int (*set_info)(struct super_block *, int, struct qc_info *);
	int (*get_dqblk)(struct super_block *, struct kqid, struct qc_dqblk *);
	int (*set_dqblk)(struct super_block *, struct kqid, struct qc_dqblk *);
	int (*get_state)(struct super_block *, struct qc_state *);
	int (*rm_xquota)(struct super_block *, unsigned int);
};

struct quota_format_type {
	int qf_fmt_id;	/* Quota format id */
	const struct quota_format_ops *qf_ops;	/* Operations of format */
	struct module *qf_owner;		/* Module implementing quota format */
	struct quota_format_type *qf_next;
};

/**
 * Quota state flags - they actually come in two flavors - for users and groups.
 *
 * Actual typed flags layout:
 *				USRQUOTA	GRPQUOTA
 *  DQUOT_USAGE_ENABLED		0x0001		0x0002
 *  DQUOT_LIMITS_ENABLED	0x0004		0x0008
 *  DQUOT_SUSPENDED		0x0010		0x0020
 *
 * Following bits are used for non-typed flags:
 *  DQUOT_QUOTA_SYS_FILE	0x0040
 *  DQUOT_NEGATIVE_USAGE	0x0080
 */
enum {
	_DQUOT_USAGE_ENABLED = 0,		/* Track disk usage for users */
	_DQUOT_LIMITS_ENABLED,			/* Enforce quota limits for users */
	_DQUOT_SUSPENDED,			/* User diskquotas are off, but
						 * we have necessary info in
						 * memory to turn them on */
	_DQUOT_STATE_FLAGS
};
#define DQUOT_USAGE_ENABLED	(1 << _DQUOT_USAGE_ENABLED * MAXQUOTAS)
#define DQUOT_LIMITS_ENABLED	(1 << _DQUOT_LIMITS_ENABLED * MAXQUOTAS)
#define DQUOT_SUSPENDED		(1 << _DQUOT_SUSPENDED * MAXQUOTAS)
#define DQUOT_STATE_FLAGS	(DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED | \
				 DQUOT_SUSPENDED)
/* Other quota flags */
#define DQUOT_STATE_LAST	(_DQUOT_STATE_FLAGS * MAXQUOTAS)
#define DQUOT_QUOTA_SYS_FILE	(1 << DQUOT_STATE_LAST)
						/* Quota file is a special
						 * system file and user cannot
						 * touch it. Filesystem is
						 * responsible for setting
						 * S_NOQUOTA, S_NOATIME flags
						 */
#define DQUOT_NEGATIVE_USAGE	(1 << (DQUOT_STATE_LAST + 1))
					       /* Allow negative quota usage */
static inline unsigned int dquot_state_flag(unsigned int flags, int type)
{
	return flags << type;
}

static inline unsigned int dquot_generic_flag(unsigned int flags, int type)
{
	return (flags >> type) & DQUOT_STATE_FLAGS;
}

/* Bitmap of quota types where flag is set in flags */
static __always_inline unsigned dquot_state_types(unsigned flags, unsigned flag)
{
	BUILD_BUG_ON_NOT_POWER_OF_2(flag);
	return (flags / flag) & ((1 << MAXQUOTAS) - 1);
}

#ifdef CONFIG_QUOTA_NETLINK_INTERFACE
extern void quota_send_warning(struct kqid qid, dev_t dev,
			       const char warntype);
#else
static inline void quota_send_warning(struct kqid qid, dev_t dev,
				      const char warntype)
{
	return;
}
#endif /* CONFIG_QUOTA_NETLINK_INTERFACE */

struct quota_info {
	unsigned int flags;			/* Flags for diskquotas on this device */
	struct mutex dqio_mutex;		/* lock device while I/O in progress */
	struct mutex dqonoff_mutex;		/* Serialize quotaon & quotaoff */
	struct inode *files[MAXQUOTAS];		/* inodes of quotafiles */
	struct mem_dqinfo info[MAXQUOTAS];	/* Information for each quota type */
	const struct quota_format_ops *ops[MAXQUOTAS];	/* Operations for each type */
};

int register_quota_format(struct quota_format_type *fmt);
void unregister_quota_format(struct quota_format_type *fmt);

struct quota_module_name {
	int qm_fmt_id;
	char *qm_mod_name;
};

#define INIT_QUOTA_MODULE_NAMES {\
	{QFMT_VFS_OLD, "quota_v1"},\
	{QFMT_VFS_V0, "quota_v2"},\
	{QFMT_VFS_V1, "quota_v2"},\
	{0, NULL}}

#endif /* _QUOTA_ */
