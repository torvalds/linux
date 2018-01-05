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
