/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#ifndef __UTIL_DOT_H__
#define __UTIL_DOT_H__

#ifdef pr_fmt
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#endif

#include <linux/mempool.h>

#include "incore.h"

#define fs_emerg(fs, fmt, ...)						\
	pr_emerg("fsid=%s: " fmt, (fs)->sd_fsname, ##__VA_ARGS__)
#define fs_warn(fs, fmt, ...)						\
	pr_warn("fsid=%s: " fmt, (fs)->sd_fsname, ##__VA_ARGS__)
#define fs_err(fs, fmt, ...)						\
	pr_err("fsid=%s: " fmt, (fs)->sd_fsname, ##__VA_ARGS__)
#define fs_info(fs, fmt, ...)						\
	pr_info("fsid=%s: " fmt, (fs)->sd_fsname, ##__VA_ARGS__)

void gfs2_assert_i(struct gfs2_sbd *sdp);

#define gfs2_assert(sdp, assertion) \
do { \
	if (unlikely(!(assertion))) { \
		gfs2_assert_i(sdp); \
		BUG(); \
        } \
} while (0)


void gfs2_assert_withdraw_i(struct gfs2_sbd *sdp, char *assertion,
			    const char *function, char *file, unsigned int line,
			    bool delayed);

#define gfs2_assert_withdraw(sdp, assertion) \
	({ \
		bool _bool = (assertion); \
		if (unlikely(!_bool)) \
			gfs2_assert_withdraw_i((sdp), #assertion, \
					__func__, __FILE__, __LINE__, false); \
		!_bool; \
	})

#define gfs2_assert_withdraw_delayed(sdp, assertion) \
	({ \
		bool _bool = (assertion); \
		if (unlikely(!_bool)) \
			gfs2_assert_withdraw_i((sdp), #assertion, \
					__func__, __FILE__, __LINE__, true); \
		!_bool; \
	})

void gfs2_assert_warn_i(struct gfs2_sbd *sdp, char *assertion,
			const char *function, char *file, unsigned int line);

#define gfs2_assert_warn(sdp, assertion) \
	({ \
		bool _bool = (assertion); \
		if (unlikely(!_bool)) \
			gfs2_assert_warn_i((sdp), #assertion, \
					__func__, __FILE__, __LINE__); \
		!_bool; \
	})

void gfs2_consist_i(struct gfs2_sbd *sdp,
		    const char *function, char *file, unsigned int line);

#define gfs2_consist(sdp) \
gfs2_consist_i((sdp), __func__, __FILE__, __LINE__)


void gfs2_consist_inode_i(struct gfs2_inode *ip,
			  const char *function, char *file, unsigned int line);

#define gfs2_consist_inode(ip) \
gfs2_consist_inode_i((ip), __func__, __FILE__, __LINE__)


void gfs2_consist_rgrpd_i(struct gfs2_rgrpd *rgd,
			  const char *function, char *file, unsigned int line);

#define gfs2_consist_rgrpd(rgd) \
gfs2_consist_rgrpd_i((rgd), __func__, __FILE__, __LINE__)


void gfs2_meta_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
			const char *function,
			char *file, unsigned int line);

static inline int gfs2_meta_check(struct gfs2_sbd *sdp,
				    struct buffer_head *bh)
{
	struct gfs2_meta_header *mh = (struct gfs2_meta_header *)bh->b_data;
	u32 magic = be32_to_cpu(mh->mh_magic);
	if (unlikely(magic != GFS2_MAGIC)) {
		fs_err(sdp, "Magic number missing at %llu\n",
		       (unsigned long long)bh->b_blocknr);
		return -EIO;
	}
	return 0;
}

void gfs2_metatype_check_ii(struct gfs2_sbd *sdp, struct buffer_head *bh,
			    u16 type, u16 t,
			    const char *function,
			    char *file, unsigned int line);

static inline int gfs2_metatype_check_i(struct gfs2_sbd *sdp,
					struct buffer_head *bh,
					u16 type,
					const char *function,
					char *file, unsigned int line)
{
	struct gfs2_meta_header *mh = (struct gfs2_meta_header *)bh->b_data;
	u32 magic = be32_to_cpu(mh->mh_magic);
	u16 t = be32_to_cpu(mh->mh_type);
	if (unlikely(magic != GFS2_MAGIC)) {
		gfs2_meta_check_ii(sdp, bh, function,
				   file, line);
		return -EIO;
	}
        if (unlikely(t != type)) {
		gfs2_metatype_check_ii(sdp, bh, type, t, function,
				       file, line);
		return -EIO;
	}
	return 0;
}

#define gfs2_metatype_check(sdp, bh, type) \
gfs2_metatype_check_i((sdp), (bh), (type), __func__, __FILE__, __LINE__)

static inline void gfs2_metatype_set(struct buffer_head *bh, u16 type,
				     u16 format)
{
	struct gfs2_meta_header *mh;
	mh = (struct gfs2_meta_header *)bh->b_data;
	mh->mh_type = cpu_to_be32(type);
	mh->mh_format = cpu_to_be32(format);
}


void gfs2_io_error_i(struct gfs2_sbd *sdp, const char *function,
		     char *file, unsigned int line);

int check_journal_clean(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd,
		        bool verbose);
int gfs2_freeze_lock_shared(struct gfs2_sbd *sdp);
void gfs2_freeze_unlock(struct gfs2_sbd *sdp);

#define gfs2_io_error(sdp) \
gfs2_io_error_i((sdp), __func__, __FILE__, __LINE__)


void gfs2_io_error_bh_i(struct gfs2_sbd *sdp, struct buffer_head *bh,
			const char *function, char *file, unsigned int line,
			bool withdraw);

#define gfs2_io_error_bh_wd(sdp, bh) \
gfs2_io_error_bh_i((sdp), (bh), __func__, __FILE__, __LINE__, true)

#define gfs2_io_error_bh(sdp, bh) \
gfs2_io_error_bh_i((sdp), (bh), __func__, __FILE__, __LINE__, false)


extern struct kmem_cache *gfs2_glock_cachep;
extern struct kmem_cache *gfs2_glock_aspace_cachep;
extern struct kmem_cache *gfs2_inode_cachep;
extern struct kmem_cache *gfs2_bufdata_cachep;
extern struct kmem_cache *gfs2_rgrpd_cachep;
extern struct kmem_cache *gfs2_quotad_cachep;
extern struct kmem_cache *gfs2_qadata_cachep;
extern struct kmem_cache *gfs2_trans_cachep;
extern mempool_t *gfs2_page_pool;
extern struct workqueue_struct *gfs2_control_wq;

static inline unsigned int gfs2_tune_get_i(struct gfs2_tune *gt,
					   unsigned int *p)
{
	unsigned int x;
	spin_lock(&gt->gt_spin);
	x = *p;
	spin_unlock(&gt->gt_spin);
	return x;
}

/**
 * gfs2_withdraw_delayed - withdraw as soon as possible without deadlocks
 * @sdp: the superblock
 */
static inline void gfs2_withdraw_delayed(struct gfs2_sbd *sdp)
{
	set_bit(SDF_WITHDRAWING, &sdp->sd_flags);
}

/**
 * gfs2_withdrawing_or_withdrawn - test whether the file system is withdrawing
 *                                 or withdrawn
 * @sdp: the superblock
 */
static inline bool gfs2_withdrawing_or_withdrawn(struct gfs2_sbd *sdp)
{
	return unlikely(test_bit(SDF_WITHDRAWN, &sdp->sd_flags) ||
			test_bit(SDF_WITHDRAWING, &sdp->sd_flags));
}

/**
 * gfs2_withdrawing - check if a withdraw is pending
 * @sdp: the superblock
 */
static inline bool gfs2_withdrawing(struct gfs2_sbd *sdp)
{
	return unlikely(test_bit(SDF_WITHDRAWING, &sdp->sd_flags) &&
			!test_bit(SDF_WITHDRAWN, &sdp->sd_flags));
}

static inline bool gfs2_withdraw_in_prog(struct gfs2_sbd *sdp)
{
	return unlikely(test_bit(SDF_WITHDRAW_IN_PROG, &sdp->sd_flags));
}

#define gfs2_tune_get(sdp, field) \
gfs2_tune_get_i(&(sdp)->sd_tune, &(sdp)->sd_tune.field)

__printf(2, 3)
void gfs2_lm(struct gfs2_sbd *sdp, const char *fmt, ...);
void gfs2_withdraw(struct gfs2_sbd *sdp);

#endif /* __UTIL_DOT_H__ */
