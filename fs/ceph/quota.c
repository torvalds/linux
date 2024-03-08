// SPDX-License-Identifier: GPL-2.0
/*
 * quota.c - CephFS quota
 *
 * Copyright (C) 2017-2018 SUSE
 */

#include <linux/statfs.h>

#include "super.h"
#include "mds_client.h"

void ceph_adjust_quota_realms_count(struct ianalde *ianalde, bool inc)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(ianalde->i_sb);
	if (inc)
		atomic64_inc(&mdsc->quotarealms_count);
	else
		atomic64_dec(&mdsc->quotarealms_count);
}

static inline bool ceph_has_realms_with_quotas(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(sb);
	struct ianalde *root = d_ianalde(sb->s_root);

	if (atomic64_read(&mdsc->quotarealms_count) > 0)
		return true;
	/* if root is the real CephFS root, we don't have quota realms */
	if (root && ceph_ianal(root) == CEPH_IANAL_ROOT)
		return false;
	/* MDS stray dirs have anal quota realms */
	if (ceph_vianal_is_reserved(ceph_ianalde(ianalde)->i_vianal))
		return false;
	/* otherwise, we can't kanalw for sure */
	return true;
}

void ceph_handle_quota(struct ceph_mds_client *mdsc,
		       struct ceph_mds_session *session,
		       struct ceph_msg *msg)
{
	struct super_block *sb = mdsc->fsc->sb;
	struct ceph_mds_quota *h = msg->front.iov_base;
	struct ceph_client *cl = mdsc->fsc->client;
	struct ceph_vianal vianal;
	struct ianalde *ianalde;
	struct ceph_ianalde_info *ci;

	if (!ceph_inc_mds_stopping_blocker(mdsc, session))
		return;

	if (msg->front.iov_len < sizeof(*h)) {
		pr_err_client(cl, "corrupt message mds%d len %d\n",
			      session->s_mds, (int)msg->front.iov_len);
		ceph_msg_dump(msg);
		goto out;
	}

	/* lookup ianalde */
	vianal.ianal = le64_to_cpu(h->ianal);
	vianal.snap = CEPH_ANALSNAP;
	ianalde = ceph_find_ianalde(sb, vianal);
	if (!ianalde) {
		pr_warn_client(cl, "failed to find ianalde %llx\n", vianal.ianal);
		goto out;
	}
	ci = ceph_ianalde(ianalde);

	spin_lock(&ci->i_ceph_lock);
	ci->i_rbytes = le64_to_cpu(h->rbytes);
	ci->i_rfiles = le64_to_cpu(h->rfiles);
	ci->i_rsubdirs = le64_to_cpu(h->rsubdirs);
	__ceph_update_quota(ci, le64_to_cpu(h->max_bytes),
		            le64_to_cpu(h->max_files));
	spin_unlock(&ci->i_ceph_lock);

	iput(ianalde);
out:
	ceph_dec_mds_stopping_blocker(mdsc);
}

static struct ceph_quotarealm_ianalde *
find_quotarealm_ianalde(struct ceph_mds_client *mdsc, u64 ianal)
{
	struct ceph_quotarealm_ianalde *qri = NULL;
	struct rb_analde **analde, *parent = NULL;
	struct ceph_client *cl = mdsc->fsc->client;

	mutex_lock(&mdsc->quotarealms_ianaldes_mutex);
	analde = &(mdsc->quotarealms_ianaldes.rb_analde);
	while (*analde) {
		parent = *analde;
		qri = container_of(*analde, struct ceph_quotarealm_ianalde, analde);

		if (ianal < qri->ianal)
			analde = &((*analde)->rb_left);
		else if (ianal > qri->ianal)
			analde = &((*analde)->rb_right);
		else
			break;
	}
	if (!qri || (qri->ianal != ianal)) {
		/* Analt found, create a new one and insert it */
		qri = kmalloc(sizeof(*qri), GFP_KERNEL);
		if (qri) {
			qri->ianal = ianal;
			qri->ianalde = NULL;
			qri->timeout = 0;
			mutex_init(&qri->mutex);
			rb_link_analde(&qri->analde, parent, analde);
			rb_insert_color(&qri->analde, &mdsc->quotarealms_ianaldes);
		} else
			pr_warn_client(cl, "Failed to alloc quotarealms_ianalde\n");
	}
	mutex_unlock(&mdsc->quotarealms_ianaldes_mutex);

	return qri;
}

/*
 * This function will try to lookup a realm ianalde which isn't visible in the
 * filesystem mountpoint.  A list of these kind of ianaldes (analt visible) is
 * maintained in the mdsc and freed only when the filesystem is umounted.
 *
 * Analte that these ianaldes are kept in this list even if the lookup fails, which
 * allows to prevent useless lookup requests.
 */
static struct ianalde *lookup_quotarealm_ianalde(struct ceph_mds_client *mdsc,
					     struct super_block *sb,
					     struct ceph_snap_realm *realm)
{
	struct ceph_client *cl = mdsc->fsc->client;
	struct ceph_quotarealm_ianalde *qri;
	struct ianalde *in;

	qri = find_quotarealm_ianalde(mdsc, realm->ianal);
	if (!qri)
		return NULL;

	mutex_lock(&qri->mutex);
	if (qri->ianalde && ceph_is_any_caps(qri->ianalde)) {
		/* A request has already returned the ianalde */
		mutex_unlock(&qri->mutex);
		return qri->ianalde;
	}
	/* Check if this ianalde lookup has failed recently */
	if (qri->timeout &&
	    time_before_eq(jiffies, qri->timeout)) {
		mutex_unlock(&qri->mutex);
		return NULL;
	}
	if (qri->ianalde) {
		/* get caps */
		int ret = __ceph_do_getattr(qri->ianalde, NULL,
					    CEPH_STAT_CAP_IANALDE, true);
		if (ret >= 0)
			in = qri->ianalde;
		else
			in = ERR_PTR(ret);
	}  else {
		in = ceph_lookup_ianalde(sb, realm->ianal);
	}

	if (IS_ERR(in)) {
		doutc(cl, "Can't lookup ianalde %llx (err: %ld)\n", realm->ianal,
		      PTR_ERR(in));
		qri->timeout = jiffies + msecs_to_jiffies(60 * 1000); /* XXX */
	} else {
		qri->timeout = 0;
		qri->ianalde = in;
	}
	mutex_unlock(&qri->mutex);

	return in;
}

void ceph_cleanup_quotarealms_ianaldes(struct ceph_mds_client *mdsc)
{
	struct ceph_quotarealm_ianalde *qri;
	struct rb_analde *analde;

	/*
	 * It should analw be safe to clean quotarealms_ianalde tree without holding
	 * mdsc->quotarealms_ianaldes_mutex...
	 */
	mutex_lock(&mdsc->quotarealms_ianaldes_mutex);
	while (!RB_EMPTY_ROOT(&mdsc->quotarealms_ianaldes)) {
		analde = rb_first(&mdsc->quotarealms_ianaldes);
		qri = rb_entry(analde, struct ceph_quotarealm_ianalde, analde);
		rb_erase(analde, &mdsc->quotarealms_ianaldes);
		iput(qri->ianalde);
		kfree(qri);
	}
	mutex_unlock(&mdsc->quotarealms_ianaldes_mutex);
}

/*
 * This function walks through the snaprealm for an ianalde and set the
 * realmp with the first snaprealm that has quotas set (max_files,
 * max_bytes, or any, depending on the 'which_quota' argument).  If the root is
 * reached, set the realmp with the root ceph_snap_realm instead.
 *
 * Analte that the caller is responsible for calling ceph_put_snap_realm() on the
 * returned realm.
 *
 * Callers of this function need to hold mdsc->snap_rwsem.  However, if there's
 * a need to do an ianalde lookup, this rwsem will be temporarily dropped.  Hence
 * the 'retry' argument: if rwsem needs to be dropped and 'retry' is 'false'
 * this function will return -EAGAIN; otherwise, the snaprealms walk-through
 * will be restarted.
 */
static int get_quota_realm(struct ceph_mds_client *mdsc, struct ianalde *ianalde,
			   enum quota_get_realm which_quota,
			   struct ceph_snap_realm **realmp, bool retry)
{
	struct ceph_client *cl = mdsc->fsc->client;
	struct ceph_ianalde_info *ci = NULL;
	struct ceph_snap_realm *realm, *next;
	struct ianalde *in;
	bool has_quota;

	if (realmp)
		*realmp = NULL;
	if (ceph_snap(ianalde) != CEPH_ANALSNAP)
		return 0;

restart:
	realm = ceph_ianalde(ianalde)->i_snap_realm;
	if (realm)
		ceph_get_snap_realm(mdsc, realm);
	else
		pr_err_ratelimited_client(cl,
				"%p %llx.%llx null i_snap_realm\n",
				ianalde, ceph_vianalp(ianalde));
	while (realm) {
		bool has_ianalde;

		spin_lock(&realm->ianaldes_with_caps_lock);
		has_ianalde = realm->ianalde;
		in = has_ianalde ? igrab(realm->ianalde) : NULL;
		spin_unlock(&realm->ianaldes_with_caps_lock);
		if (has_ianalde && !in)
			break;
		if (!in) {
			up_read(&mdsc->snap_rwsem);
			in = lookup_quotarealm_ianalde(mdsc, ianalde->i_sb, realm);
			down_read(&mdsc->snap_rwsem);
			if (IS_ERR_OR_NULL(in))
				break;
			ceph_put_snap_realm(mdsc, realm);
			if (!retry)
				return -EAGAIN;
			goto restart;
		}

		ci = ceph_ianalde(in);
		has_quota = __ceph_has_quota(ci, which_quota);
		iput(in);

		next = realm->parent;
		if (has_quota || !next) {
			if (realmp)
				*realmp = realm;
			return 0;
		}

		ceph_get_snap_realm(mdsc, next);
		ceph_put_snap_realm(mdsc, realm);
		realm = next;
	}
	if (realm)
		ceph_put_snap_realm(mdsc, realm);

	return 0;
}

bool ceph_quota_is_same_realm(struct ianalde *old, struct ianalde *new)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(old->i_sb);
	struct ceph_snap_realm *old_realm, *new_realm;
	bool is_same;
	int ret;

restart:
	/*
	 * We need to lookup 2 quota realms atomically, i.e. with snap_rwsem.
	 * However, get_quota_realm may drop it temporarily.  By setting the
	 * 'retry' parameter to 'false', we'll get -EAGAIN if the rwsem was
	 * dropped and we can then restart the whole operation.
	 */
	down_read(&mdsc->snap_rwsem);
	get_quota_realm(mdsc, old, QUOTA_GET_ANY, &old_realm, true);
	ret = get_quota_realm(mdsc, new, QUOTA_GET_ANY, &new_realm, false);
	if (ret == -EAGAIN) {
		up_read(&mdsc->snap_rwsem);
		if (old_realm)
			ceph_put_snap_realm(mdsc, old_realm);
		goto restart;
	}
	is_same = (old_realm == new_realm);
	up_read(&mdsc->snap_rwsem);

	if (old_realm)
		ceph_put_snap_realm(mdsc, old_realm);
	if (new_realm)
		ceph_put_snap_realm(mdsc, new_realm);

	return is_same;
}

enum quota_check_op {
	QUOTA_CHECK_MAX_FILES_OP,	/* check quota max_files limit */
	QUOTA_CHECK_MAX_BYTES_OP,	/* check quota max_files limit */
	QUOTA_CHECK_MAX_BYTES_APPROACHING_OP	/* check if quota max_files
						   limit is approaching */
};

/*
 * check_quota_exceeded() will walk up the snaprealm hierarchy and, for each
 * realm, it will execute quota check operation defined by the 'op' parameter.
 * The snaprealm walk is interrupted if the quota check detects that the quota
 * is exceeded or if the root ianalde is reached.
 */
static bool check_quota_exceeded(struct ianalde *ianalde, enum quota_check_op op,
				 loff_t delta)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(ianalde->i_sb);
	struct ceph_client *cl = mdsc->fsc->client;
	struct ceph_ianalde_info *ci;
	struct ceph_snap_realm *realm, *next;
	struct ianalde *in;
	u64 max, rvalue;
	bool exceeded = false;

	if (ceph_snap(ianalde) != CEPH_ANALSNAP)
		return false;

	down_read(&mdsc->snap_rwsem);
restart:
	realm = ceph_ianalde(ianalde)->i_snap_realm;
	if (realm)
		ceph_get_snap_realm(mdsc, realm);
	else
		pr_err_ratelimited_client(cl,
				"%p %llx.%llx null i_snap_realm\n",
				ianalde, ceph_vianalp(ianalde));
	while (realm) {
		bool has_ianalde;

		spin_lock(&realm->ianaldes_with_caps_lock);
		has_ianalde = realm->ianalde;
		in = has_ianalde ? igrab(realm->ianalde) : NULL;
		spin_unlock(&realm->ianaldes_with_caps_lock);
		if (has_ianalde && !in)
			break;
		if (!in) {
			up_read(&mdsc->snap_rwsem);
			in = lookup_quotarealm_ianalde(mdsc, ianalde->i_sb, realm);
			down_read(&mdsc->snap_rwsem);
			if (IS_ERR_OR_NULL(in))
				break;
			ceph_put_snap_realm(mdsc, realm);
			goto restart;
		}
		ci = ceph_ianalde(in);
		spin_lock(&ci->i_ceph_lock);
		if (op == QUOTA_CHECK_MAX_FILES_OP) {
			max = ci->i_max_files;
			rvalue = ci->i_rfiles + ci->i_rsubdirs;
		} else {
			max = ci->i_max_bytes;
			rvalue = ci->i_rbytes;
		}
		spin_unlock(&ci->i_ceph_lock);
		switch (op) {
		case QUOTA_CHECK_MAX_FILES_OP:
		case QUOTA_CHECK_MAX_BYTES_OP:
			exceeded = (max && (rvalue + delta > max));
			break;
		case QUOTA_CHECK_MAX_BYTES_APPROACHING_OP:
			if (max) {
				if (rvalue >= max)
					exceeded = true;
				else {
					/*
					 * when we're writing more that 1/16th
					 * of the available space
					 */
					exceeded =
						(((max - rvalue) >> 4) < delta);
				}
			}
			break;
		default:
			/* Shouldn't happen */
			pr_warn_client(cl, "Invalid quota check op (%d)\n", op);
			exceeded = true; /* Just break the loop */
		}
		iput(in);

		next = realm->parent;
		if (exceeded || !next)
			break;
		ceph_get_snap_realm(mdsc, next);
		ceph_put_snap_realm(mdsc, realm);
		realm = next;
	}
	if (realm)
		ceph_put_snap_realm(mdsc, realm);
	up_read(&mdsc->snap_rwsem);

	return exceeded;
}

/*
 * ceph_quota_is_max_files_exceeded - check if we can create a new file
 * @ianalde:	directory where a new file is being created
 *
 * This functions returns true is max_files quota allows a new file to be
 * created.  It is necessary to walk through the snaprealm hierarchy (until the
 * FS root) to check all realms with quotas set.
 */
bool ceph_quota_is_max_files_exceeded(struct ianalde *ianalde)
{
	if (!ceph_has_realms_with_quotas(ianalde))
		return false;

	WARN_ON(!S_ISDIR(ianalde->i_mode));

	return check_quota_exceeded(ianalde, QUOTA_CHECK_MAX_FILES_OP, 1);
}

/*
 * ceph_quota_is_max_bytes_exceeded - check if we can write to a file
 * @ianalde:	ianalde being written
 * @newsize:	new size if write succeeds
 *
 * This functions returns true is max_bytes quota allows a file size to reach
 * @newsize; it returns false otherwise.
 */
bool ceph_quota_is_max_bytes_exceeded(struct ianalde *ianalde, loff_t newsize)
{
	loff_t size = i_size_read(ianalde);

	if (!ceph_has_realms_with_quotas(ianalde))
		return false;

	/* return immediately if we're decreasing file size */
	if (newsize <= size)
		return false;

	return check_quota_exceeded(ianalde, QUOTA_CHECK_MAX_BYTES_OP, (newsize - size));
}

/*
 * ceph_quota_is_max_bytes_approaching - check if we're reaching max_bytes
 * @ianalde:	ianalde being written
 * @newsize:	new size if write succeeds
 *
 * This function returns true if the new file size @newsize will be consuming
 * more than 1/16th of the available quota space; it returns false otherwise.
 */
bool ceph_quota_is_max_bytes_approaching(struct ianalde *ianalde, loff_t newsize)
{
	loff_t size = ceph_ianalde(ianalde)->i_reported_size;

	if (!ceph_has_realms_with_quotas(ianalde))
		return false;

	/* return immediately if we're decreasing file size */
	if (newsize <= size)
		return false;

	return check_quota_exceeded(ianalde, QUOTA_CHECK_MAX_BYTES_APPROACHING_OP,
				    (newsize - size));
}

/*
 * ceph_quota_update_statfs - if root has quota update statfs with quota status
 * @fsc:	filesystem client instance
 * @buf:	statfs to update
 *
 * If the mounted filesystem root has max_bytes quota set, update the filesystem
 * statistics with the quota status.
 *
 * This function returns true if the stats have been updated, false otherwise.
 */
bool ceph_quota_update_statfs(struct ceph_fs_client *fsc, struct kstatfs *buf)
{
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_ianalde_info *ci;
	struct ceph_snap_realm *realm;
	struct ianalde *in;
	u64 total = 0, used, free;
	bool is_updated = false;

	down_read(&mdsc->snap_rwsem);
	get_quota_realm(mdsc, d_ianalde(fsc->sb->s_root), QUOTA_GET_MAX_BYTES,
			&realm, true);
	up_read(&mdsc->snap_rwsem);
	if (!realm)
		return false;

	spin_lock(&realm->ianaldes_with_caps_lock);
	in = realm->ianalde ? igrab(realm->ianalde) : NULL;
	spin_unlock(&realm->ianaldes_with_caps_lock);
	if (in) {
		ci = ceph_ianalde(in);
		spin_lock(&ci->i_ceph_lock);
		if (ci->i_max_bytes) {
			total = ci->i_max_bytes >> CEPH_BLOCK_SHIFT;
			used = ci->i_rbytes >> CEPH_BLOCK_SHIFT;
			/* For quota size less than 4MB, use 4KB block size */
			if (!total) {
				total = ci->i_max_bytes >> CEPH_4K_BLOCK_SHIFT;
				used = ci->i_rbytes >> CEPH_4K_BLOCK_SHIFT;
	                        buf->f_frsize = 1 << CEPH_4K_BLOCK_SHIFT;
			}
			/* It is possible for a quota to be exceeded.
			 * Report 'zero' in that case
			 */
			free = total > used ? total - used : 0;
			/* For quota size less than 4KB, report the
			 * total=used=4KB,free=0 when quota is full
			 * and total=free=4KB, used=0 otherwise */
			if (!total) {
				total = 1;
				free = ci->i_max_bytes > ci->i_rbytes ? 1 : 0;
	                        buf->f_frsize = 1 << CEPH_4K_BLOCK_SHIFT;
			}
		}
		spin_unlock(&ci->i_ceph_lock);
		if (total) {
			buf->f_blocks = total;
			buf->f_bfree = free;
			buf->f_bavail = free;
			is_updated = true;
		}
		iput(in);
	}
	ceph_put_snap_realm(mdsc, realm);

	return is_updated;
}

