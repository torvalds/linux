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
enum quota_type {
	USRQUOTA = 0,		/* element used for user quotas */
	GRPQUOTA = 1,		/* element used for group quotas */
	PRJQUOTA = 2,		/* element used for project quotas */
};

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
	qsize_t dqi_maxblimit;
	qsize_t dqi_maxilimit;
	void *dqi_priv;
};

struct super_block;

#define DQF_MASK 0xffff		/* Mask for format specific flags */
#define DQF_GETINFO_MASK 0x1ffff	/* Mask for flags passed to userspace */
#define DQF_SETINFO_MASK 0xffff		/* Mask for flags modifiable from userspace */
#define DQF_SYS_FILE_B		16
#define DQF_SYS_FILE (1 << DQF_SYS_FILE_B)	/* Quota file stored as system file */
#define DQF_INFO_DIRTY_B	31
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
};

struct path;

/* Operations handling requests from userspace */
struct quotactl_ops {
	int (*quota_on)(struct super_block *, int, int, struct path *);
	int (*quota_on_meta)(struct super_block *, int, int);
	int (*quota_off)(struct super_block *, int);
	int (*quota_sync)(struct super_block *, int);
	int (*get_info)(struct super_block *, int, struct if_dqinfo *);
	int (*set_info)(struct super_block *, int, struct if_dqinfo *);
	int (*get_dqblk)(struct super_block *, struct kqid, struct fs_disk_quota *);
	int (*set_dqblk)(struct super_block *, struct kqid, struct fs_disk_quota *);
	int (*get_xstate)(struct super_block *, struct fs_quota_stat *);
	int (*set_xstate)(struct super_block *, unsigned int, int);
	int (*get_xstatev)(struct super_block *, struct fs_quota_statv *);
	int (*rm_xquota)(struct super_block *, unsigned int);
};

struct quota_format_type {
	int qf_fmt_id;	/* Quota format id */
	const struct quota_format_ops *qf_ops;	/* Operations of format */
	struct module *qf_owner;		/* Module implementing quota format */
	struct quota_format_type *qf_next;
};

/* Quota state flags - they actually come in two flavors - for users and groups */
enum {
	_DQUOT_USAGE_ENABLED = 0,		/* Track disk usage for users */
	_DQUOT_LIMITS_ENABLED,			/* Enforce quota limits for users */
	_DQUOT_SUSPENDED,			/* User diskquotas are off, but
						 * we have necessary info in
						 * memory to turn them on */
	_DQUOT_STATE_FLAGS
};
#define DQUOT_USAGE_ENABLED	(1 << _DQUOT_USAGE_ENABLED)
#define DQUOT_LIMITS_ENABLED	(1 << _DQUOT_LIMITS_ENABLED)
#define DQUOT_SUSPENDED		(1 << _DQUOT_SUSPENDED)
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
	return flags << _DQUOT_STATE_FLAGS * type;
}

static inline unsigned int dquot_generic_flag(unsigned int flags, int type)
{
	return (flags >> _DQUOT_STATE_FLAGS * type) & DQUOT_STATE_FLAGS;
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
