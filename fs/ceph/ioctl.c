#include <linux/ceph/ceph_debug.h>
#include <linux/in.h>

#include "super.h"
#include "mds_client.h"
#include "ioctl.h"


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

	err = ceph_do_getattr(file_inode(file), CEPH_STAT_CAP_LAYOUT);
	if (!err) {
		l.stripe_unit = ceph_file_layout_su(ci->i_layout);
		l.stripe_count = ceph_file_layout_stripe_count(ci->i_layout);
		l.object_size = ceph_file_layout_object_size(ci->i_layout);
		l.data_pool = le32_to_cpu(ci->i_layout.fl_pg_pool);
		l.preferred_osd = (s32)-1;
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
	    (l->stripe_unit != 0 &&
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
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_mds_request *req;
	struct ceph_ioctl_layout l;
	struct ceph_inode_info *ci = ceph_inode(file_inode(file));
	struct ceph_ioctl_layout nl;
	int err;

	if (copy_from_user(&l, arg, sizeof(l)))
		return -EFAULT;

	/* validate changed params against current layout */
	err = ceph_do_getattr(file_inode(file), CEPH_STAT_CAP_LAYOUT);
	if (err)
		return err;

	memset(&nl, 0, sizeof(nl));
	if (l.stripe_count)
		nl.stripe_count = l.stripe_count;
	else
		nl.stripe_count = ceph_file_layout_stripe_count(ci->i_layout);
	if (l.stripe_unit)
		nl.stripe_unit = l.stripe_unit;
	else
		nl.stripe_unit = ceph_file_layout_su(ci->i_layout);
	if (l.object_size)
		nl.object_size = l.object_size;
	else
		nl.object_size = ceph_file_layout_object_size(ci->i_layout);
	if (l.data_pool)
		nl.data_pool = l.data_pool;
	else
		nl.data_pool = ceph_file_layout_pg_pool(ci->i_layout);

	/* this is obsolete, and always -1 */
	nl.preferred_osd = le64_to_cpu(-1);

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
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;

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
		&ceph_sb_to_client(inode->i_sb)->client->osdc;
	struct ceph_object_locator oloc;
	struct ceph_object_id oid;
	u64 len = 1, olen;
	u64 tmp;
	struct ceph_pg pgid;
	int r;

	/* copy and validate */
	if (copy_from_user(&dl, arg, sizeof(dl)))
		return -EFAULT;

	down_read(&osdc->map_sem);
	r = ceph_calc_file_object_mapping(&ci->i_layout, dl.file_offset, len,
					  &dl.object_no, &dl.object_offset,
					  &olen);
	if (r < 0) {
		up_read(&osdc->map_sem);
		return -EIO;
	}
	dl.file_offset -= dl.object_offset;
	dl.object_size = ceph_file_layout_object_size(ci->i_layout);
	dl.block_size = ceph_file_layout_su(ci->i_layout);

	/* block_offset = object_offset % block_size */
	tmp = dl.object_offset;
	dl.block_offset = do_div(tmp, dl.block_size);

	snprintf(dl.object_name, sizeof(dl.object_name), "%llx.%08llx",
		 ceph_ino(inode), dl.object_no);

	oloc.pool = ceph_file_layout_pg_pool(ci->i_layout);
	ceph_oid_set_name(&oid, dl.object_name);

	r = ceph_oloc_oid_to_pg(osdc->osdmap, &oloc, &oid, &pgid);
	if (r < 0) {
		up_read(&osdc->map_sem);
		return r;
	}

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
	struct inode *inode = file_inode(file);
	struct ceph_inode_info *ci = ceph_inode(inode);

	if ((fi->fmode & CEPH_FILE_MODE_LAZY) == 0) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_nr_by_mode[fi->fmode]--;
		fi->fmode |= CEPH_FILE_MODE_LAZY;
		ci->i_nr_by_mode[fi->fmode]++;
		spin_unlock(&ci->i_ceph_lock);
		dout("ioctl_layzio: file %p marked lazy\n", file);

		ceph_check_caps(ci, 0, NULL);
	} else {
		dout("ioctl_layzio: file %p already lazy\n", file);
	}
	return 0;
}

static long ceph_ioctl_syncio(struct file *file)
{
	struct ceph_file_info *fi = file->private_data;

	fi->flags |= CEPH_F_SYNC;
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

	case CEPH_IOC_SYNCIO:
		return ceph_ioctl_syncio(file);
	}

	return -ENOTTY;
}
