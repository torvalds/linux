// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>
#include <linux/in.h>

#include "super.h"
#include "mds_client.h"
#include "ioctl.h"
#include <linux/ceph/striper.h>
#include <linux/fscrypt.h>

/*
 * ioctls
 */

/*
 * get and set the file layout
 */
static long ceph_ioctl_get_layout(struct file *file, void __user *arg)
{
	struct ceph_inode_info *ci = ceph_inode(file_inode(file));
	struct ceph_ioctl_layout l;
	int err;

	err = ceph_do_getattr(file_inode(file), CEPH_STAT_CAP_LAYOUT, false);
	if (!err) {
		l.stripe_unit = ci->i_layout.stripe_unit;
		l.stripe_count = ci->i_layout.stripe_count;
		l.object_size = ci->i_layout.object_size;
		l.data_pool = ci->i_layout.pool_id;
		l.preferred_osd = -1;
		if (copy_to_user(arg, &l, sizeof(l)))
			return -EFAULT;
	}

	return err;
}

static long __validate_layout(struct ceph_mds_client *mdsc,
			      struct ceph_ioctl_layout *l)
{
	int i, err;

	/* validate striping parameters */
	if ((l->object_size & ~PAGE_MASK) ||
	    (l->stripe_unit & ~PAGE_MASK) ||
	    ((unsigned)l->stripe_unit != 0 &&
	     ((unsigned)l->object_size % (unsigned)l->stripe_unit)))
		return -EINVAL;

	/* make sure it's a valid data pool */
	mutex_lock(&mdsc->mutex);
	err = -EINVAL;
	for (i = 0; i < mdsc->mdsmap->m_num_data_pg_pools; i++)
		if (mdsc->mdsmap->m_data_pg_pools[i] == l->data_pool) {
			err = 0;
			break;
		}
	mutex_unlock(&mdsc->mutex);
	if (err)
		return err;

	return 0;
}

static long ceph_ioctl_set_layout(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct ceph_mds_client *mdsc = ceph_sb_to_fs_client(inode->i_sb)->mdsc;
	struct ceph_mds_request *req;
	struct ceph_ioctl_layout l;
	struct ceph_inode_info *ci = ceph_inode(file_inode(file));
	struct ceph_ioctl_layout nl;
	int err;

	if (copy_from_user(&l, arg, sizeof(l)))
		return -EFAULT;

	/* validate changed params against current layout */
	err = ceph_do_getattr(file_inode(file), CEPH_STAT_CAP_LAYOUT, false);
	if (err)
		return err;

	memset(&nl, 0, sizeof(nl));
	if (l.stripe_count)
		nl.stripe_count = l.stripe_count;
	else
		nl.stripe_count = ci->i_layout.stripe_count;
	if (l.stripe_unit)
		nl.stripe_unit = l.stripe_unit;
	else
		nl.stripe_unit = ci->i_layout.stripe_unit;
	if (l.object_size)
		nl.object_size = l.object_size;
	else
		nl.object_size = ci->i_layout.object_size;
	if (l.data_pool)
		nl.data_pool = l.data_pool;
	else
		nl.data_pool = ci->i_layout.pool_id;

	/* this is obsolete, and always -1 */
	nl.preferred_osd = -1;

	err = __validate_layout(mdsc, &nl);
	if (err)
		return err;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETLAYOUT,
				       USE_AUTH_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = inode;
	ihold(inode);
	req->r_num_caps = 1;

	req->r_inode_drop = CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_EXCL;

	req->r_args.setlayout.layout.fl_stripe_unit =
		cpu_to_le32(l.stripe_unit);
	req->r_args.setlayout.layout.fl_stripe_count =
		cpu_to_le32(l.stripe_count);
	req->r_args.setlayout.layout.fl_object_size =
		cpu_to_le32(l.object_size);
	req->r_args.setlayout.layout.fl_pg_pool = cpu_to_le32(l.data_pool);

	err = ceph_mdsc_do_request(mdsc, NULL, req);
	ceph_mdsc_put_request(req);
	return err;
}

/*
 * Set a layout policy on a directory inode. All items in the tree
 * rooted at this inode will inherit this layout on creation,
 * (It doesn't apply retroactively )
 * unless a subdirectory has its own layout policy.
 */
static long ceph_ioctl_set_layout_policy (struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct ceph_mds_request *req;
	struct ceph_ioctl_layout l;
	int err;
	struct ceph_mds_client *mdsc = ceph_sb_to_fs_client(inode->i_sb)->mdsc;

	/* copy and validate */
	if (copy_from_user(&l, arg, sizeof(l)))
		return -EFAULT;

	err = __validate_layout(mdsc, &l);
	if (err)
		return err;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETDIRLAYOUT,
				       USE_AUTH_MDS);

	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = inode;
	ihold(inode);
	req->r_num_caps = 1;

	req->r_args.setlayout.layout.fl_stripe_unit =
			cpu_to_le32(l.stripe_unit);
	req->r_args.setlayout.layout.fl_stripe_count =
			cpu_to_le32(l.stripe_count);
	req->r_args.setlayout.layout.fl_object_size =
			cpu_to_le32(l.object_size);
	req->r_args.setlayout.layout.fl_pg_pool =
			cpu_to_le32(l.data_pool);

	err = ceph_mdsc_do_request(mdsc, inode, req);
	ceph_mdsc_put_request(req);
	return err;
}

/*
 * Return object name, size/offset information, and location (OSD
 * number, network address) for a given file offset.
 */
static long ceph_ioctl_get_dataloc(struct file *file, void __user *arg)
{
	struct ceph_ioctl_dataloc dl;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_osd_client *osdc =
		&ceph_sb_to_fs_client(inode->i_sb)->client->osdc;
	struct ceph_object_locator oloc;
	CEPH_DEFINE_OID_ONSTACK(oid);
	u32 xlen;
	u64 tmp;
	struct ceph_pg pgid;
	int r;

	/* copy and validate */
	if (copy_from_user(&dl, arg, sizeof(dl)))
		return -EFAULT;

	down_read(&osdc->lock);
	ceph_calc_file_object_mapping(&ci->i_layout, dl.file_offset, 1,
				      &dl.object_no, &dl.object_offset, &xlen);
	dl.file_offset -= dl.object_offset;
	dl.object_size = ci->i_layout.object_size;
	dl.block_size = ci->i_layout.stripe_unit;

	/* block_offset = object_offset % block_size */
	tmp = dl.object_offset;
	dl.block_offset = do_div(tmp, dl.block_size);

	snprintf(dl.object_name, sizeof(dl.object_name), "%llx.%08llx",
		 ceph_ino(inode), dl.object_no);

	oloc.pool = ci->i_layout.pool_id;
	oloc.pool_ns = ceph_try_get_string(ci->i_layout.pool_ns);
	ceph_oid_printf(&oid, "%s", dl.object_name);

	r = ceph_object_locator_to_pg(osdc->osdmap, &oid, &oloc, &pgid);

	ceph_oloc_destroy(&oloc);
	if (r < 0) {
		up_read(&osdc->lock);
		return r;
	}

	dl.osd = ceph_pg_to_acting_primary(osdc->osdmap, &pgid);
	if (dl.osd >= 0) {
		struct ceph_entity_addr *a =
			ceph_osd_addr(osdc->osdmap, dl.osd);
		if (a)
			memcpy(&dl.osd_addr, &a->in_addr, sizeof(dl.osd_addr));
	} else {
		memset(&dl.osd_addr, 0, sizeof(dl.osd_addr));
	}
	up_read(&osdc->lock);

	/* send result back to user */
	if (copy_to_user(arg, &dl, sizeof(dl)))
		return -EFAULT;

	return 0;
}

static long ceph_ioctl_lazyio(struct file *file)
{
	struct ceph_file_info *fi = file->private_data;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_inode_to_fs_client(inode)->mdsc;
	struct ceph_client *cl = mdsc->fsc->client;

	if ((fi->fmode & CEPH_FILE_MODE_LAZY) == 0) {
		spin_lock(&ci->i_ceph_lock);
		fi->fmode |= CEPH_FILE_MODE_LAZY;
		ci->i_nr_by_mode[ffs(CEPH_FILE_MODE_LAZY)]++;
		__ceph_touch_fmode(ci, mdsc, fi->fmode);
		spin_unlock(&ci->i_ceph_lock);
		doutc(cl, "file %p %p %llx.%llx marked lazy\n", file, inode,
		      ceph_vinop(inode));

		ceph_check_caps(ci, 0);
	} else {
		doutc(cl, "file %p %p %llx.%llx already lazy\n", file, inode,
		      ceph_vinop(inode));
	}
	return 0;
}

static long ceph_ioctl_syncio(struct file *file)
{
	struct ceph_file_info *fi = file->private_data;

	fi->flags |= CEPH_F_SYNC;
	return 0;
}

static int vet_mds_for_fscrypt(struct file *file)
{
	int i, ret = -EOPNOTSUPP;
	struct ceph_mds_client	*mdsc = ceph_sb_to_mdsc(file_inode(file)->i_sb);

	mutex_lock(&mdsc->mutex);
	for (i = 0; i < mdsc->max_sessions; i++) {
		struct ceph_mds_session *s = mdsc->sessions[i];

		if (!s)
			continue;
		if (test_bit(CEPHFS_FEATURE_ALTERNATE_NAME, &s->s_features))
			ret = 0;
		break;
	}
	mutex_unlock(&mdsc->mutex);
	return ret;
}

static long ceph_set_encryption_policy(struct file *file, unsigned long arg)
{
	int ret, got = 0;
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);

	/* encrypted directories can't have striped layout */
	if (ci->i_layout.stripe_count > 1)
		return -EINVAL;

	ret = vet_mds_for_fscrypt(file);
	if (ret)
		return ret;

	/*
	 * Ensure we hold these caps so that we _know_ that the rstats check
	 * in the empty_dir check is reliable.
	 */
	ret = ceph_get_caps(file, CEPH_CAP_FILE_SHARED, 0, -1, &got);
	if (ret)
		return ret;

	ret = fscrypt_ioctl_set_policy(file, (const void __user *)arg);
	if (got)
		ceph_put_cap_refs(ci, got);

	return ret;
}

static const char *ceph_ioctl_cmd_name(const unsigned int cmd)
{
	switch (cmd) {
	case CEPH_IOC_GET_LAYOUT:
		return "get_layout";
	case CEPH_IOC_SET_LAYOUT:
		return "set_layout";
	case CEPH_IOC_SET_LAYOUT_POLICY:
		return "set_layout_policy";
	case CEPH_IOC_GET_DATALOC:
		return "get_dataloc";
	case CEPH_IOC_LAZYIO:
		return "lazyio";
	case CEPH_IOC_SYNCIO:
		return "syncio";
	case FS_IOC_SET_ENCRYPTION_POLICY:
		return "set_encryption_policy";
	case FS_IOC_GET_ENCRYPTION_POLICY:
		return "get_encryption_policy";
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
		return "get_encryption_policy_ex";
	case FS_IOC_ADD_ENCRYPTION_KEY:
		return "add_encryption_key";
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
		return "remove_encryption_key";
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
		return "remove_encryption_key_all_users";
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
		return "get_encryption_key_status";
	case FS_IOC_GET_ENCRYPTION_NONCE:
		return "get_encryption_nonce";
	default:
		return "unknown";
	}
}

long ceph_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct ceph_fs_client *fsc = ceph_inode_to_fs_client(inode);
	int ret;

	doutc(fsc->client, "file %p %p %llx.%llx cmd %s arg %lu\n", file,
	      inode, ceph_vinop(inode), ceph_ioctl_cmd_name(cmd), arg);
	switch (cmd) {
	case CEPH_IOC_GET_LAYOUT:
		return ceph_ioctl_get_layout(file, (void __user *)arg);

	case CEPH_IOC_SET_LAYOUT:
		return ceph_ioctl_set_layout(file, (void __user *)arg);

	case CEPH_IOC_SET_LAYOUT_POLICY:
		return ceph_ioctl_set_layout_policy(file, (void __user *)arg);

	case CEPH_IOC_GET_DATALOC:
		return ceph_ioctl_get_dataloc(file, (void __user *)arg);

	case CEPH_IOC_LAZYIO:
		return ceph_ioctl_lazyio(file);

	case CEPH_IOC_SYNCIO:
		return ceph_ioctl_syncio(file);

	case FS_IOC_SET_ENCRYPTION_POLICY:
		return ceph_set_encryption_policy(file, arg);

	case FS_IOC_GET_ENCRYPTION_POLICY:
		ret = vet_mds_for_fscrypt(file);
		if (ret)
			return ret;
		return fscrypt_ioctl_get_policy(file, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
		ret = vet_mds_for_fscrypt(file);
		if (ret)
			return ret;
		return fscrypt_ioctl_get_policy_ex(file, (void __user *)arg);

	case FS_IOC_ADD_ENCRYPTION_KEY:
		ret = vet_mds_for_fscrypt(file);
		if (ret)
			return ret;
		return fscrypt_ioctl_add_key(file, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY:
		return fscrypt_ioctl_remove_key(file, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
		return fscrypt_ioctl_remove_key_all_users(file,
							  (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
		return fscrypt_ioctl_get_key_status(file, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_NONCE:
		ret = vet_mds_for_fscrypt(file);
		if (ret)
			return ret;
		return fscrypt_ioctl_get_nonce(file, (void __user *)arg);
	}

	return -ENOTTY;
}
