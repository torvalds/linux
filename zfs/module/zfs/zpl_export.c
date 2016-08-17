/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011 Gunnar Beutner
 * Copyright (c) 2012 Cyril Plisko. All rights reserved.
 */


#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>


static int
#ifdef HAVE_ENCODE_FH_WITH_INODE
zpl_encode_fh(struct inode *ip, __u32 *fh, int *max_len, struct inode *parent)
{
#else
zpl_encode_fh(struct dentry *dentry, __u32 *fh, int *max_len, int connectable)
{
	struct inode *ip = dentry->d_inode;
#endif /* HAVE_ENCODE_FH_WITH_INODE */
	fstrans_cookie_t cookie;
	fid_t *fid = (fid_t *)fh;
	int len_bytes, rc;

	len_bytes = *max_len * sizeof (__u32);

	if (len_bytes < offsetof(fid_t, fid_data))
		return (255);

	fid->fid_len = len_bytes - offsetof(fid_t, fid_data);
	cookie = spl_fstrans_mark();

	if (zfsctl_is_node(ip))
		rc = zfsctl_fid(ip, fid);
	else
		rc = zfs_fid(ip, fid);

	spl_fstrans_unmark(cookie);
	len_bytes = offsetof(fid_t, fid_data) + fid->fid_len;
	*max_len = roundup(len_bytes, sizeof (__u32)) / sizeof (__u32);

	return (rc == 0 ? FILEID_INO32_GEN : 255);
}

static struct dentry *
zpl_dentry_obtain_alias(struct inode *ip)
{
	struct dentry *result;

#ifdef HAVE_D_OBTAIN_ALIAS
	result = d_obtain_alias(ip);
#else
	result = d_alloc_anon(ip);

	if (result == NULL) {
		iput(ip);
		result = ERR_PTR(-ENOMEM);
	}
#endif /* HAVE_D_OBTAIN_ALIAS */

	return (result);
}

static struct dentry *
zpl_fh_to_dentry(struct super_block *sb, struct fid *fh,
    int fh_len, int fh_type)
{
	fid_t *fid = (fid_t *)fh;
	fstrans_cookie_t cookie;
	struct inode *ip;
	int len_bytes, rc;

	len_bytes = fh_len * sizeof (__u32);

	if (fh_type != FILEID_INO32_GEN ||
	    len_bytes < offsetof(fid_t, fid_data) ||
	    len_bytes < offsetof(fid_t, fid_data) + fid->fid_len)
		return (ERR_PTR(-EINVAL));

	cookie = spl_fstrans_mark();
	rc = zfs_vget(sb, &ip, fid);
	spl_fstrans_unmark(cookie);

	if (rc) {
		/*
		 * If we see ENOENT it might mean that an NFSv4 * client
		 * is using a cached inode value in a file handle and
		 * that the sought after file has had its inode changed
		 * by a third party.  So change the error to ESTALE
		 * which will trigger a full lookup by the client and
		 * will find the new filename/inode pair if it still
		 * exists.
		 */
		if (rc == ENOENT)
			rc = ESTALE;

		return (ERR_PTR(-rc));
	}

	ASSERT((ip != NULL) && !IS_ERR(ip));

	return (zpl_dentry_obtain_alias(ip));
}

static struct dentry *
zpl_get_parent(struct dentry *child)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	struct inode *ip;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_lookup(child->d_inode, "..", &ip, 0, cr, NULL, NULL);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	if (error)
		return (ERR_PTR(error));

	return (zpl_dentry_obtain_alias(ip));
}

#ifdef HAVE_COMMIT_METADATA
static int
zpl_commit_metadata(struct inode *inode)
{
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	if (zfsctl_is_node(inode))
		return (0);

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_fsync(inode, 0, cr);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}
#endif /* HAVE_COMMIT_METADATA */

const struct export_operations zpl_export_operations = {
	.encode_fh		= zpl_encode_fh,
	.fh_to_dentry		= zpl_fh_to_dentry,
	.get_parent		= zpl_get_parent,
#ifdef HAVE_COMMIT_METADATA
	.commit_metadata	= zpl_commit_metadata,
#endif /* HAVE_COMMIT_METADATA */
};
