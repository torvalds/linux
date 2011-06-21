#include <linux/in.h>

#include "super.h"
#include "mds_client.h"
#include <linux/ceph/ceph_debug.h>

#include "ioctl.h"


/*
 * ioctls
 */

/*
 * get and set the file layout
 */
static long ceph_ioctl_get_layout(struct file *file, void __user *arg)
{
	struct ceph_inode_info *ci = ceph_inode(file->f_dentry->d_inode);
	struct ceph_ioctl_layout l;
	int err;

	err = ceph_do_getattr(file->f_dentry->d_inode, CEPH_STAT_CAP_LAYOUT);
	if (!err) {
		l.stripe_unit = ceph_file_layout_su(ci->i_layout);
		l.stripe_count = ceph_file_layout_stripe_count(ci->i_layout);
		l.object_size = ceph_file_layout_object_size(ci->i_layout);
		l.data_pool = le32_to_cpu(ci->i_layout.fl_pg_pool);
		l.preferred_osd =
			(s32)le32_to_cpu(ci->i_layout.fl_pg_preferred);
		if (copy_to_user(arg, &l, sizeof(l)))
			return -EFAULT;
	}

	return err;
}

static long ceph_ioctl_set_layout(struct file *file, void __user *arg)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct inode *parent_inode = file->f_dentry->d_parent->d_inode;
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_mds_request *req;
	struct ceph_ioctl_layout l;
	int err, i;

	/* copy and validate */
	if (copy_from_user(&l, arg, sizeof(l)))
		return -EFAULT;

	if ((l.object_size & ~PAGE_MASK) ||
	    (l.stripe_unit & ~PAGE_MASK) ||
	    !l.stripe_unit ||
	    (l.object_size &&
	     (unsigned)l.object_size % (unsigned)l.stripe_unit))
		return -EINVAL;

	/* make sure it's a valid data pool */
	if (l.data_pool > 0) {
		mutex_lock(&mdsc->mutex);
		err = -EINVAL;
		for (i = 0; i < mdsc->mdsmap->m_num_data_pg_pools; i++)
			if (mdsc->mdsmap->m_data_pg_pools[i] == l.data_pool) {
				err = 0;
				break;
			}
		mutex_unlock(&mdsc->mutex);
		if (err)
			return err;
	}

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETLAYOUT,
				       USE_AUTH_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = inode;
	ihold(inode);
	req->r_inode_drop = CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_EXCL;

	req->r_args.setlayout.layout.fl_stripe_unit =
		cpu_to_le32(l.stripe_unit);
	req->r_args.setlayout.layout.fl_stripe_count =
		cpu_to_le32(l.stripe_count);
	req->r_args.setlayout.layout.fl_object_size =
		cpu_to_le32(l.object_size);
	req->r_args.setlayout.layout.fl_pg_pool = cpu_to_le32(l.data_pool);
	req->r_args.setlayout.layout.fl_pg_preferred =
		cpu_to_le32(l.preferred_osd);

	err = ceph_mdsc_do_request(mdsc, parent_inode, req);
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
	struct inode *inode = file->f_dentry->d_inode;
	struct ceph_mds_request *req;
	struct ceph_ioctl_layout l;
	int err, i;
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;

	/* copy and validate */
	if (copy_from_user(&l, arg, sizeof(l)))
		return -EFAULT;

	if ((l.object_size & ~PAGE_MASK) ||
	    (l.stripe_unit & ~PAGE_MASK) ||
	    !l.stripe_unit ||
	    (l.object_size &&
	        (unsigned)l.object_size % (unsigned)l.stripe_unit))
		return -EINVAL;

	/* make sure it's a valid data pool */
	if (l.data_pool > 0) {
		mutex_lock(&mdsc->mutex);
		err = -EINVAL;
		for (i = 0; i < mdsc->mdsmap->m_num_data_pg_pools; i++)
			if (mdsc->mdsmap->m_data_pg_pools[i] == l.data_pool) {
				err = 0;
				break;
			}
		mutex_unlock(&mdsc->mutex);
		if (err)
			return err;
	}

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETDIRLAYOUT,
				       USE_AUTH_MDS);

	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = inode;
	ihold(inode);

	req->r_args.setlayout.layout.fl_stripe_unit =
			cpu_to_le32(l.stripe_unit);
	req->r_args.setlayout.layout.fl_stripe_count =
			cpu_to_le32(l.stripe_count);
	req->r_args.setlayout.layout.fl_object_size =
			cpu_to_le32(l.object_size);
	req->r_args.setlayout.layout.fl_pg_pool =
			cpu_to_le32(l.data_pool);
	req->r_args.setlayout.layout.fl_pg_preferred =
			cpu_to_le32(l.preferred_osd);

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
	struct inode *inode = file->f_dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_osd_client *osdc =
		&ceph_sb_to_client(inode->i_sb)->client->osdc;
	u64 len = 1, olen;
	u64 tmp;
	struct ceph_object_layout ol;
	struct ceph_pg pgid;

	/* copy and validate */
	if (copy_from_user(&dl, arg, sizeof(dl)))
		return -EFAULT;

	down_read(&osdc->map_sem);
	ceph_calc_file_object_mapping(&ci->i_layout, dl.file_offset, &len,
				      &dl.object_no, &dl.object_offset, &olen);
	dl.file_offset -= dl.object_offset;
	dl.object_size = ceph_file_layout_object_size(ci->i_layout);
	dl.block_size = ceph_file_layout_su(ci->i_layout);

	/* block_offset = object_offset % block_size */
	tmp = dl.object_offset;
	dl.block_offset = do_div(tmp, dl.block_size);

	snprintf(dl.object_name, sizeof(dl.object_name), "%llx.%08llx",
		 ceph_ino(inode), dl.object_no);
	ceph_calc_object_layout(&ol, dl.object_name, &ci->i_layout,
				osdc->osdmap);

	pgid = ol.ol_pgid;
	dl.osd = ceph_calc_pg_primary(osdc->osdmap, pgid);
	if (dl.osd >= 0) {
		struct ceph_entity_addr *a =
			ceph_osd_addr(osdc->osdmap, dl.osd);
		if (a)
			memcpy(&dl.osd_addr, &a->in_addr, sizeof(dl.osd_addr));
	} else {
		memset(&dl.osd_addr, 0, sizeof(dl.osd_addr));
	}
	up_read(&osdc->map_sem);

	/* send result back to user */
	if (copy_to_user(arg, &dl, sizeof(dl)))
		return -EFAULT;

	return 0;
}

static long ceph_ioctl_lazyio(struct file *file)
{
	struct ceph_file_info *fi = file->private_data;
	struct inode *inode = file->f_dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);

	if ((fi->fmode & CEPH_FILE_MODE_LAZY) == 0) {
		spin_lock(&inode->i_lock);
		ci->i_nr_by_mode[fi->fmode]--;
		fi->fmode |= CEPH_FILE_MODE_LAZY;
		ci->i_nr_by_mode[fi->fmode]++;
		spin_unlock(&inode->i_lock);
		dout("ioctl_layzio: file %p marked lazy\n", file);

		ceph_check_caps(ci, 0, NULL);
	} else {
		dout("ioctl_layzio: file %p already lazy\n", file);
	}
	return 0;
}

long ceph_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	dout("ioctl file %p cmd %u arg %lu\n", file, cmd, arg);
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
	}

	return -ENOTTY;
}
