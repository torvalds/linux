// SPDX-License-Identifier: GPL-2.0
/*
 * quota.c - CephFS quota
 *
 * Copyright (C) 2017-2018 SUSE
 */

#include <linux/statfs.h>

#include "super.h"
#include "mds_client.h"

void ceph_adjust_quota_realms_count(struct inode *inode, bool inc)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);
	if (inc)
		atomic64_inc(&mdsc->quotarealms_count);
	else
		atomic64_dec(&mdsc->quotarealms_count);
}

static inline bool ceph_has_realms_with_quotas(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(sb);
	struct inode *root = d_inode(sb->s_root);

	if (atomic64_read(&mdsc->quotarealms_count) > 0)
		return true;
	/* if root is the real CephFS root, we don't have quota realms */
	if (root && ceph_ino(root) == CEPH_INO_ROOT)
		return false;
	/* otherwise, we can't know for sure */
	return true;
}

void ceph_handle_quota(struct ceph_mds_client *mdsc,
		       struct ceph_mds_session *session,
		       struct ceph_msg *msg)
{
	struct super_block *sb = mdsc->fsc->sb;
	struct ceph_mds_quota *h = msg->front.iov_base;
	struct ceph_vino vino;
	struct inode *inode;
	struct ceph_inode_info *ci;

	if (msg->front.iov_len < sizeof(*h)) {
		pr_err("%s corrupt message mds%d len %d\n", __func__,
		       session->s_mds, (int)msg->front.iov_len);
		ceph_msg_dump(msg);
		return;
	}

	/* increment msg sequence number */
	mutex_lock(&session->s_mutex);
	inc_session_sequence(session);
	mutex_unlock(&session->s_mutex);

	/* lookup inode */
	vino.ino = le64_to_cpu(h->ino);
	vino.snap = CEPH_NOSNAP;
	inode = ceph_find_inode(sb, vino);
	if (!inode) {
		pr_warn("Failed to find inode %llu\n", vino.ino);
		return;
	}
	ci = ceph_inode(inode);

	spin_lock(&ci->i_ceph_lock);
	ci->i_rbytes = le64_to_cpu(h->rbytes);
	ci->i_rfiles = le64_to_cpu(h->rfiles);
	ci->i_rsubdirs = le64_to_cpu(h->rsubdirs);
	__ceph_update_quota(ci, le64_to_cpu(h->max_bytes),
		            le64_to_cpu(h->max_files));
	spin_unlock(&ci->i_ceph_lock);

	iput(inode);
}

static struct ceph_quotarealm_inode *
find_quotarealm_inode(struct ceph_mds_client *mdsc, u64 ino)
{
	struct ceph_quotarealm_inode *qri = NULL;
	struct rb_node **node, *parent = NULL;

	mutex_lock(&mdsc->quotarealms_inodes_mutex);
	node = &(mdsc->quotarealms_inodes.rb_node);
	while (*node) {
		parent = *node;
		qri = container_of(*node, struct ceph_quotarealm_inode, node);

		if (ino < qri->ino)
			node = &((*node)->rb_left);
		else if (ino > qri->ino)
			node = &((*node)->rb_right);
		else
			break;
	}
	if (!qri || (qri->ino != ino)) {
		/* Not found, create a new one and insert it */
		qri = kmalloc(sizeof(*qri), GFP_KERNEL);
		if (qri) {
			qri->ino = ino;
			qri->inode = NULL;
			qri->timeout = 0;
			mutex_init(&qri->mutex);
			rb_link_node(&qri->node, parent, node);
			rb_insert_color(&qri->node, &mdsc->quotarealms_inodes);
		} else
			pr_warn("Failed to alloc quotarealms_inode\n");
	}
	mutex_unlock(&mdsc->quotarealms_inodes_mutex);

	return qri;
}

/*
 * This function will try to lookup a realm inode which isn't visible in the
 * filesystem mountpoint.  A list of these kind of inodes (not visible) is
 * maintained in the mdsc and freed only when the filesystem is umounted.
 *
 * Note that these inodes are kept in this list even if the lookup fails, which
 * allows to prevent useless lookup requests.
 */
static struct inode *lookup_quotarealm_inode(struct ceph_mds_client *mdsc,
					     struct super_block *sb,
					     struct ceph_snap_realm *realm)
{
	struct ceph_quotarealm_inode *qri;
	struct inode *in;

	qri = find_quotarealm_inode(mdsc, realm->ino);
	if (!qri)
		return NULL;

	mutex_lock(&qri->mutex);
	if (qri->inode && ceph_is_any_caps(qri->inode)) {
		/* A request has already returned the inode */
		mutex_unlock(&qri->mutex);
		return qri->inode;
	}
	/* Check if this inode lookup has failed recently */
	if (qri->timeout &&
	    time_before_eq(jiffies, qri->timeout)) {
		mutex_unlock(&qri->mutex);
		return NULL;
	}
	if (qri->inode) {
		/* get caps */
		int ret = __ceph_do_getattr(qri->inode, NULL,
					    CEPH_STAT_CAP_INODE, true);
		if (ret >= 0)
			in = qri->inode;
		else
			in = ERR_PTR(ret);
	}  else {
		in = ceph_lookup_inode(sb, realm->ino);
	}

	if (IS_ERR(in)) {
		dout("Can't lookup inode %llx (err: %ld)\n",
		     realm->ino, PTR_ERR(in));
		qri->timeout = jiffies + msecs_to_jiffies(60 * 1000); /* XXX */
	} else {
		qri->timeout = 0;
		qri->inode = in;
	}
	mutex_unlock(&qri->mutex);

	return in;
}

void ceph_cleanup_quotarealms_inodes(struct ceph_mds_client *mdsc)
{
	struct ceph_quotarealm_inode *qri;
	struct rb_node *node;

	/*
	 * It should now be safe to clean quotarealms_inode tree without holding
	 * mdsc->quotarealms_inodes_mutex...
	 */
	mutex_lock(&mdsc->quotarealms_inodes_mutex);
	while (!RB_EMPTY_ROOT(&mdsc->quotarealms_inodes)) {
		node = rb_first(&mdsc->quotarealms_inodes);
		qri = rb_entry(node, struct ceph_quotarealm_inode, node);
		rb_erase(node, &mdsc->quotarealms_inodes);
		iput(qri->inode);
		kfree(qri);
	}
	mutex_unlock(&mdsc->quotarealms_inodes_mutex);
}

/*
 * This function walks through the snaprealm for an inode and returns the
 * ceph_snap_realm for the first snaprealm that has quotas set (either max_files
 * or max_bytes).  If the root is reached, return the root ceph_snap_realm
 * instead.
 *
 * Note that the caller is responsible for calling ceph_put_snap_realm() on the
 * returned realm.
 *
 * Callers of this function need to hold mdsc->snap_rwsem.  However, if there's
 * a need to do an inode lookup, this rwsem will be temporarily dropped.  Hence
 * the 'retry' argument: if rwsem needs to be dropped and 'retry' is 'false'
 * this function will return -EAGAIN; otherwise, the snaprealms walk-through
 * will be restarted.
 */
static struct ceph_snap_realm *get_quota_realm(struct ceph_mds_client *mdsc,
					       struct inode *inode, bool retry)
{
	struct ceph_inode_info *ci = NULL;
	struct ceph_snap_realm *realm, *next;
	struct inode *in;
	bool has_quota;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return NULL;

restart:
	realm = ceph_inode(inode)->i_snap_realm;
	if (realm)
		ceph_get_snap_realm(mdsc, realm);
	else
		pr_err_ratelimited("get_quota_realm: ino (%llx.%llx) "
				   "null i_snap_realm\n", ceph_vinop(inode));
	while (realm) {
		bool has_inode;

		spin_lock(&realm->inodes_with_caps_lock);
		has_inode = realm->inode;
		in = has_inode ? igrab(realm->inode) : NULL;
		spin_unlock(&realm->inodes_with_caps_lock);
		if (has_inode && !in)
			break;
		if (!in) {
			up_read(&mdsc->snap_rwsem);
			in = lookup_quotarealm_inode(mdsc, inode->i_sb, realm);
			down_read(&mdsc->snap_rwsem);
			if (IS_ERR_OR_NULL(in))
				break;
			ceph_put_snap_realm(mdsc, realm);
			if (!retry)
				return ERR_PTR(-EAGAIN);
			goto restart;
		}

		ci = ceph_inode(in);
		has_quota = __ceph_has_any_quota(ci);
		iput(in);

		next = realm->parent;
		if (has_quota || !next)
		       return realm;

		ceph_get_snap_realm(mdsc, next);
		ceph_put_snap_realm(mdsc, realm);
		realm = next;
	}
	if (realm)
		ceph_put_snap_realm(mdsc, realm);

	return NULL;
}

bool ceph_quota_is_same_realm(struct inode *old, struct inode *new)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(old->i_sb);
	struct ceph_snap_realm *old_realm, *new_realm;
	bool is_same;

restart:
	/*
	 * We need to lookup 2 quota realms atomically, i.e. with snap_rwsem.
	 * However, get_quota_realm may drop it temporarily.  By setting the
	 * 'retry' parameter to 'false', we'll get -EAGAIN if the rwsem was
	 * dropped and we can then restart the whole operation.
	 */
	down_read(&mdsc->snap_rwsem);
	old_realm = get_quota_realm(mdsc, old, true);
	new_realm = get_quota_realm(mdsc, new, false);
	if (PTR_ERR(new_realm) == -EAGAIN) {
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
 * is exceeded or if the root inode is reached.
 */
static bool check_quota_exceeded(struct inode *inode, enum quota_check_op op,
				 loff_t delta)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);
	struct ceph_inode_info *ci;
	struct ceph_snap_realm *realm, *next;
	struct inode *in;
	u64 max, rvalue;
	bool exceeded = false;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return false;

	down_read(&mdsc->snap_rwsem);
restart:
	realm = ceph_inode(inode)->i_snap_realm;
	if (realm)
		ceph_get_snap_realm(mdsc, realm);
	else
		pr_err_ratelimited("check_quota_exceeded: ino (%llx.%llx) "
				   "null i_snap_realm\n", ceph_vinop(inode));
	while (realm) {
		bool has_inode;

		spin_lock(&realm->inodes_with_caps_lock);
		has_inode = realm->inode;
		in = has_inode ? igrab(realm->inode) : NULL;
		spin_unlock(&realm->inodes_with_caps_lock);
		if (has_inode && !in)
			break;
		if (!in) {
			up_read(&mdsc->snap_rwsem);
			in = lookup_quotarealm_inode(mdsc, inode->i_sb, realm);
			down_read(&mdsc->snap_rwsem);
			if (IS_ERR_OR_NULL(in))
				break;
			ceph_put_snap_realm(mdsc, realm);
			goto restart;
		}
		ci = ceph_inode(in);
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
			pr_warn("Invalid quota check op (%d)\n", op);
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
 * @inode:	directory where a new file is being created
 *
 * This functions returns true is max_files quota allows a new file to be
 * created.  It is necessary to walk through the snaprealm hierarchy (until the
 * FS root) to check all realms with quotas set.
 */
bool ceph_quota_is_max_files_exceeded(struct inode *inode)
{
	if (!ceph_has_realms_with_quotas(inode))
		return false;

	WARN_ON(!S_ISDIR(inode->i_mode));

	return check_quota_exceeded(inode, QUOTA_CHECK_MAX_FILES_OP, 1);
}

/*
 * ceph_quota_is_max_bytes_exceeded - check if we can write to a file
 * @inode:	inode being written
 * @newsize:	new size if write succeeds
 *
 * This functions returns true is max_bytes quota allows a file size to reach
 * @newsize; it returns false otherwise.
 */
bool ceph_quota_is_max_bytes_exceeded(struct inode *inode, loff_t newsize)
{
	loff_t size = i_size_read(inode);

	if (!ceph_has_realms_with_quotas(inode))
		return false;

	/* return immediately if we're decreasing file size */
	if (newsize <= size)
		return false;

	return check_quota_exceeded(inode, QUOTA_CHECK_MAX_BYTES_OP, (newsize - size));
}

/*
 * ceph_quota_is_max_bytes_approaching - check if we're reaching max_bytes
 * @inode:	inode being written
 * @newsize:	new size if write succeeds
 *
 * This function returns true if the new file size @newsize will be consuming
 * more than 1/16th of the available quota space; it returns false otherwise.
 */
bool ceph_quota_is_max_bytes_approaching(struct inode *inode, loff_t newsize)
{
	loff_t size = ceph_inode(inode)->i_reported_size;

	if (!ceph_has_realms_with_quotas(inode))
		return false;

	/* return immediately if we're decreasing file size */
	if (newsize <= size)
		return false;

	return check_quota_exceeded(inode, QUOTA_CHECK_MAX_BYTES_APPROACHING_OP,
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
	struct ceph_inode_info *ci;
	struct ceph_snap_realm *realm;
	struct inode *in;
	u64 total = 0, used, free;
	bool is_updated = false;

	down_read(&mdsc->snap_rwsem);
	realm = get_quota_realm(mdsc, d_inode(fsc->sb->s_root), true);
	up_read(&mdsc->snap_rwsem);
	if (!realm)
		return false;

	spin_lock(&realm->inodes_with_caps_lock);
	in = realm->inode ? igrab(realm->inode) : NULL;
	spin_unlock(&realm->inodes_with_caps_lock);
	if (in) {
		ci = ceph_inode(in);
		spin_lock(&ci->i_ceph_lock);
		if (ci->i_max_bytes) {
			total = ci->i_max_bytes >> CEPH_BLOCK_SHIFT;
			used = ci->i_rbytes >> CEPH_BLOCK_SHIFT;
			/* It is possible for a quota to be exceeded.
			 * Report 'zero' in that case
			 */
			free = total > used ? total - used : 0;
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

