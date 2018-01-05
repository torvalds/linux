// SPDX-License-Identifier: GPL-2.0
/*
 * quota.c - CephFS quota
 *
 * Copyright (C) 2017-2018 SUSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "super.h"
#include "mds_client.h"

static inline bool ceph_has_quota(struct ceph_inode_info *ci)
{
	return (ci && (ci->i_max_files || ci->i_max_bytes));
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

	if (msg->front.iov_len != sizeof(*h)) {
		pr_err("%s corrupt message mds%d len %d\n", __func__,
		       session->s_mds, (int)msg->front.iov_len);
		ceph_msg_dump(msg);
		return;
	}

	/* increment msg sequence number */
	mutex_lock(&session->s_mutex);
	session->s_seq++;
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
	ci->i_max_bytes = le64_to_cpu(h->max_bytes);
	ci->i_max_files = le64_to_cpu(h->max_files);
	spin_unlock(&ci->i_ceph_lock);

	iput(inode);
}

/*
 * This function walks through the snaprealm for an inode and returns the
 * ceph_snap_realm for the first snaprealm that has quotas set (either max_files
 * or max_bytes).  If the root is reached, return the root ceph_snap_realm
 * instead.
 *
 * Note that the caller is responsible for calling ceph_put_snap_realm() on the
 * returned realm.
 */
static struct ceph_snap_realm *get_quota_realm(struct ceph_mds_client *mdsc,
					       struct inode *inode)
{
	struct ceph_inode_info *ci = NULL;
	struct ceph_snap_realm *realm, *next;
	struct ceph_vino vino;
	struct inode *in;

	realm = ceph_inode(inode)->i_snap_realm;
	ceph_get_snap_realm(mdsc, realm);
	while (realm) {
		vino.ino = realm->ino;
		vino.snap = CEPH_NOSNAP;
		in = ceph_find_inode(inode->i_sb, vino);
		if (!in) {
			pr_warn("Failed to find inode for %llu\n", vino.ino);
			break;
		}
		ci = ceph_inode(in);
		if (ceph_has_quota(ci) || (ci->i_vino.ino == CEPH_INO_ROOT)) {
			iput(in);
			return realm;
		}
		iput(in);
		next = realm->parent;
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
	struct ceph_mds_client *mdsc = ceph_inode_to_client(old)->mdsc;
	struct ceph_snap_realm *old_realm, *new_realm;
	bool is_same;

	down_read(&mdsc->snap_rwsem);
	old_realm = get_quota_realm(mdsc, old);
	new_realm = get_quota_realm(mdsc, new);
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
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	struct ceph_inode_info *ci;
	struct ceph_snap_realm *realm, *next;
	struct ceph_vino vino;
	struct inode *in;
	u64 max, rvalue;
	bool is_root;
	bool exceeded = false;

	down_read(&mdsc->snap_rwsem);
	realm = ceph_inode(inode)->i_snap_realm;
	ceph_get_snap_realm(mdsc, realm);
	while (realm) {
		vino.ino = realm->ino;
		vino.snap = CEPH_NOSNAP;
		in = ceph_find_inode(inode->i_sb, vino);
		if (!in) {
			pr_warn("Failed to find inode for %llu\n", vino.ino);
			break;
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
		is_root = (ci->i_vino.ino == CEPH_INO_ROOT);
		spin_unlock(&ci->i_ceph_lock);
		switch (op) {
		case QUOTA_CHECK_MAX_FILES_OP:
			exceeded = (max && (rvalue >= max));
			break;
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

		if (is_root || exceeded)
			break;
		next = realm->parent;
		ceph_get_snap_realm(mdsc, next);
		ceph_put_snap_realm(mdsc, realm);
		realm = next;
	}
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
	WARN_ON(!S_ISDIR(inode->i_mode));

	return check_quota_exceeded(inode, QUOTA_CHECK_MAX_FILES_OP, 0);
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

	/* return immediately if we're decreasing file size */
	if (newsize <= size)
		return false;

	return check_quota_exceeded(inode, QUOTA_CHECK_MAX_BYTES_APPROACHING_OP,
				    (newsize - size));
}
