/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_trans.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_ioctl32.h"

#include <linux/dcache.h>
#include <linux/smp_lock.h>

static struct vm_operations_struct xfs_file_vm_ops;
#ifdef CONFIG_XFS_DMAPI
static struct vm_operations_struct xfs_dmapi_file_vm_ops;
#endif

STATIC inline ssize_t
__xfs_file_read(
	struct kiocb		*iocb,
	char			__user *buf,
	int			ioflags,
	size_t			count,
	loff_t			pos)
{
	struct iovec		iov = {buf, count};
	struct file		*file = iocb->ki_filp;
	vnode_t			*vp = vn_from_inode(file->f_dentry->d_inode);
	ssize_t			rval;

	BUG_ON(iocb->ki_pos != pos);

	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;
	VOP_READ(vp, iocb, &iov, 1, &iocb->ki_pos, ioflags, NULL, rval);
	return rval;
}

STATIC ssize_t
xfs_file_aio_read(
	struct kiocb		*iocb,
	char			__user *buf,
	size_t			count,
	loff_t			pos)
{
	return __xfs_file_read(iocb, buf, IO_ISAIO, count, pos);
}

STATIC ssize_t
xfs_file_aio_read_invis(
	struct kiocb		*iocb,
	char			__user *buf,
	size_t			count,
	loff_t			pos)
{
	return __xfs_file_read(iocb, buf, IO_ISAIO|IO_INVIS, count, pos);
}

STATIC inline ssize_t
__xfs_file_write(
	struct kiocb	*iocb,
	const char	__user *buf,
	int		ioflags,
	size_t		count,
	loff_t		pos)
{
	struct iovec	iov = {(void __user *)buf, count};
	struct file	*file = iocb->ki_filp;
	struct inode	*inode = file->f_mapping->host;
	vnode_t		*vp = vn_from_inode(inode);
	ssize_t		rval;

	BUG_ON(iocb->ki_pos != pos);
	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;

	VOP_WRITE(vp, iocb, &iov, 1, &iocb->ki_pos, ioflags, NULL, rval);
	return rval;
}

STATIC ssize_t
xfs_file_aio_write(
	struct kiocb		*iocb,
	const char		__user *buf,
	size_t			count,
	loff_t			pos)
{
	return __xfs_file_write(iocb, buf, IO_ISAIO, count, pos);
}

STATIC ssize_t
xfs_file_aio_write_invis(
	struct kiocb		*iocb,
	const char		__user *buf,
	size_t			count,
	loff_t			pos)
{
	return __xfs_file_write(iocb, buf, IO_ISAIO|IO_INVIS, count, pos);
}

STATIC inline ssize_t
__xfs_file_readv(
	struct file		*file,
	const struct iovec 	*iov,
	int			ioflags,
	unsigned long		nr_segs,
	loff_t			*ppos)
{
	struct inode	*inode = file->f_mapping->host;
	vnode_t		*vp = vn_from_inode(inode);
	struct kiocb	kiocb;
	ssize_t		rval;

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = *ppos;

	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;
	VOP_READ(vp, &kiocb, iov, nr_segs, &kiocb.ki_pos, ioflags, NULL, rval);

	*ppos = kiocb.ki_pos;
	return rval;
}

STATIC ssize_t
xfs_file_readv(
	struct file		*file,
	const struct iovec 	*iov,
	unsigned long		nr_segs,
	loff_t			*ppos)
{
	return __xfs_file_readv(file, iov, 0, nr_segs, ppos);
}

STATIC ssize_t
xfs_file_readv_invis(
	struct file		*file,
	const struct iovec 	*iov,
	unsigned long		nr_segs,
	loff_t			*ppos)
{
	return __xfs_file_readv(file, iov, IO_INVIS, nr_segs, ppos);
}

STATIC inline ssize_t
__xfs_file_writev(
	struct file		*file,
	const struct iovec 	*iov,
	int			ioflags,
	unsigned long		nr_segs,
	loff_t			*ppos)
{
	struct inode	*inode = file->f_mapping->host;
	vnode_t		*vp = vn_from_inode(inode);
	struct kiocb	kiocb;
	ssize_t		rval;

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = *ppos;
	if (unlikely(file->f_flags & O_DIRECT))
		ioflags |= IO_ISDIRECT;

	VOP_WRITE(vp, &kiocb, iov, nr_segs, &kiocb.ki_pos, ioflags, NULL, rval);

	*ppos = kiocb.ki_pos;
	return rval;
}

STATIC ssize_t
xfs_file_writev(
	struct file		*file,
	const struct iovec 	*iov,
	unsigned long		nr_segs,
	loff_t			*ppos)
{
	return __xfs_file_writev(file, iov, 0, nr_segs, ppos);
}

STATIC ssize_t
xfs_file_writev_invis(
	struct file		*file,
	const struct iovec 	*iov,
	unsigned long		nr_segs,
	loff_t			*ppos)
{
	return __xfs_file_writev(file, iov, IO_INVIS, nr_segs, ppos);
}

STATIC ssize_t
xfs_file_sendfile(
	struct file		*filp,
	loff_t			*pos,
	size_t			count,
	read_actor_t		actor,
	void			*target)
{
	vnode_t			*vp = vn_from_inode(filp->f_dentry->d_inode);
	ssize_t			rval;

	VOP_SENDFILE(vp, filp, pos, 0, count, actor, target, NULL, rval);
	return rval;
}

STATIC ssize_t
xfs_file_sendfile_invis(
	struct file		*filp,
	loff_t			*pos,
	size_t			count,
	read_actor_t		actor,
	void			*target)
{
	vnode_t			*vp = vn_from_inode(filp->f_dentry->d_inode);
	ssize_t			rval;

	VOP_SENDFILE(vp, filp, pos, IO_INVIS, count, actor, target, NULL, rval);
	return rval;
}

STATIC ssize_t
xfs_file_splice_read(
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			len,
	unsigned int		flags)
{
	vnode_t			*vp = vn_from_inode(infilp->f_dentry->d_inode);
	ssize_t			rval;

	VOP_SPLICE_READ(vp, infilp, ppos, pipe, len, flags, 0, NULL, rval);
	return rval;
}

STATIC ssize_t
xfs_file_splice_read_invis(
	struct file		*infilp,
	loff_t			*ppos,
	struct pipe_inode_info	*pipe,
	size_t			len,
	unsigned int		flags)
{
	vnode_t			*vp = vn_from_inode(infilp->f_dentry->d_inode);
	ssize_t			rval;

	VOP_SPLICE_READ(vp, infilp, ppos, pipe, len, flags, IO_INVIS, NULL, rval);
	return rval;
}

STATIC ssize_t
xfs_file_splice_write(
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			len,
	unsigned int		flags)
{
	vnode_t			*vp = vn_from_inode(outfilp->f_dentry->d_inode);
	ssize_t			rval;

	VOP_SPLICE_WRITE(vp, pipe, outfilp, ppos, len, flags, 0, NULL, rval);
	return rval;
}

STATIC ssize_t
xfs_file_splice_write_invis(
	struct pipe_inode_info	*pipe,
	struct file		*outfilp,
	loff_t			*ppos,
	size_t			len,
	unsigned int		flags)
{
	vnode_t			*vp = vn_from_inode(outfilp->f_dentry->d_inode);
	ssize_t			rval;

	VOP_SPLICE_WRITE(vp, pipe, outfilp, ppos, len, flags, IO_INVIS, NULL, rval);
	return rval;
}

STATIC int
xfs_file_open(
	struct inode	*inode,
	struct file	*filp)
{
	vnode_t		*vp = vn_from_inode(inode);
	int		error;

	if (!(filp->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
		return -EFBIG;
	VOP_OPEN(vp, NULL, error);
	return -error;
}

STATIC int
xfs_file_release(
	struct inode	*inode,
	struct file	*filp)
{
	vnode_t		*vp = vn_from_inode(inode);
	int		error = 0;

	if (vp)
		VOP_RELEASE(vp, error);
	return -error;
}

STATIC int
xfs_file_fsync(
	struct file	*filp,
	struct dentry	*dentry,
	int		datasync)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = vn_from_inode(inode);
	int		error;
	int		flags = FSYNC_WAIT;

	if (datasync)
		flags |= FSYNC_DATA;
	VOP_FSYNC(vp, flags, NULL, (xfs_off_t)0, (xfs_off_t)-1, error);
	return -error;
}

#ifdef CONFIG_XFS_DMAPI
STATIC struct page *
xfs_vm_nopage(
	struct vm_area_struct	*area,
	unsigned long		address,
	int			*type)
{
	struct inode	*inode = area->vm_file->f_dentry->d_inode;
	vnode_t		*vp = vn_from_inode(inode);

	ASSERT_ALWAYS(vp->v_vfsp->vfs_flag & VFS_DMI);
	if (XFS_SEND_MMAP(XFS_VFSTOM(vp->v_vfsp), area, 0))
		return NULL;
	return filemap_nopage(area, address, type);
}
#endif /* CONFIG_XFS_DMAPI */

STATIC int
xfs_file_readdir(
	struct file	*filp,
	void		*dirent,
	filldir_t	filldir)
{
	int		error = 0;
	vnode_t		*vp = vn_from_inode(filp->f_dentry->d_inode);
	uio_t		uio;
	iovec_t		iov;
	int		eof = 0;
	caddr_t		read_buf;
	int		namelen, size = 0;
	size_t		rlen = PAGE_CACHE_SIZE;
	xfs_off_t	start_offset, curr_offset;
	xfs_dirent_t	*dbp = NULL;

	/* Try fairly hard to get memory */
	do {
		if ((read_buf = (caddr_t)kmalloc(rlen, GFP_KERNEL)))
			break;
		rlen >>= 1;
	} while (rlen >= 1024);

	if (read_buf == NULL)
		return -ENOMEM;

	uio.uio_iov = &iov;
	uio.uio_segflg = UIO_SYSSPACE;
	curr_offset = filp->f_pos;
	if (filp->f_pos != 0x7fffffff)
		uio.uio_offset = filp->f_pos;
	else
		uio.uio_offset = 0xffffffff;

	while (!eof) {
		uio.uio_resid = iov.iov_len = rlen;
		iov.iov_base = read_buf;
		uio.uio_iovcnt = 1;

		start_offset = uio.uio_offset;

		VOP_READDIR(vp, &uio, NULL, &eof, error);
		if ((uio.uio_offset == start_offset) || error) {
			size = 0;
			break;
		}

		size = rlen - uio.uio_resid;
		dbp = (xfs_dirent_t *)read_buf;
		while (size > 0) {
			namelen = strlen(dbp->d_name);

			if (filldir(dirent, dbp->d_name, namelen,
					(loff_t) curr_offset & 0x7fffffff,
					(ino_t) dbp->d_ino,
					DT_UNKNOWN)) {
				goto done;
			}
			size -= dbp->d_reclen;
			curr_offset = (loff_t)dbp->d_off /* & 0x7fffffff */;
			dbp = (xfs_dirent_t *)((char *)dbp + dbp->d_reclen);
		}
	}
done:
	if (!error) {
		if (size == 0)
			filp->f_pos = uio.uio_offset & 0x7fffffff;
		else if (dbp)
			filp->f_pos = curr_offset;
	}

	kfree(read_buf);
	return -error;
}

STATIC int
xfs_file_mmap(
	struct file	*filp,
	struct vm_area_struct *vma)
{
	vma->vm_ops = &xfs_file_vm_ops;

#ifdef CONFIG_XFS_DMAPI
	if (vn_from_inode(filp->f_dentry->d_inode)->v_vfsp->vfs_flag & VFS_DMI)
		vma->vm_ops = &xfs_dmapi_file_vm_ops;
#endif /* CONFIG_XFS_DMAPI */

	file_accessed(filp);
	return 0;
}


STATIC long
xfs_file_ioctl(
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	arg)
{
	int		error;
	struct inode	*inode = filp->f_dentry->d_inode;
	vnode_t		*vp = vn_from_inode(inode);

	VOP_IOCTL(vp, inode, filp, 0, cmd, (void __user *)arg, error);
	VMODIFY(vp);

	/* NOTE:  some of the ioctl's return positive #'s as a
	 *	  byte count indicating success, such as
	 *	  readlink_by_handle.  So we don't "sign flip"
	 *	  like most other routines.  This means true
	 *	  errors need to be returned as a negative value.
	 */
	return error;
}

STATIC long
xfs_file_ioctl_invis(
	struct file	*filp,
	unsigned int	cmd,
	unsigned long	arg)
{
	struct inode	*inode = filp->f_dentry->d_inode;
	vnode_t		*vp = vn_from_inode(inode);
	int		error;

	VOP_IOCTL(vp, inode, filp, IO_INVIS, cmd, (void __user *)arg, error);
	VMODIFY(vp);

	/* NOTE:  some of the ioctl's return positive #'s as a
	 *	  byte count indicating success, such as
	 *	  readlink_by_handle.  So we don't "sign flip"
	 *	  like most other routines.  This means true
	 *	  errors need to be returned as a negative value.
	 */
	return error;
}

#ifdef CONFIG_XFS_DMAPI
#ifdef HAVE_VMOP_MPROTECT
STATIC int
xfs_vm_mprotect(
	struct vm_area_struct *vma,
	unsigned int	newflags)
{
	vnode_t		*vp = vn_from_inode(vma->vm_file->f_dentry->d_inode);
	int		error = 0;

	if (vp->v_vfsp->vfs_flag & VFS_DMI) {
		if ((vma->vm_flags & VM_MAYSHARE) &&
		    (newflags & VM_WRITE) && !(vma->vm_flags & VM_WRITE)) {
			xfs_mount_t	*mp = XFS_VFSTOM(vp->v_vfsp);

			error = XFS_SEND_MMAP(mp, vma, VM_WRITE);
		    }
	}
	return error;
}
#endif /* HAVE_VMOP_MPROTECT */
#endif /* CONFIG_XFS_DMAPI */

#ifdef HAVE_FOP_OPEN_EXEC
/* If the user is attempting to execute a file that is offline then
 * we have to trigger a DMAPI READ event before the file is marked as busy
 * otherwise the invisible I/O will not be able to write to the file to bring
 * it back online.
 */
STATIC int
xfs_file_open_exec(
	struct inode	*inode)
{
	vnode_t		*vp = vn_from_inode(inode);
	xfs_mount_t	*mp = XFS_VFSTOM(vp->v_vfsp);
	int		error = 0;
	xfs_inode_t	*ip;

	if (vp->v_vfsp->vfs_flag & VFS_DMI) {
		ip = xfs_vtoi(vp);
		if (!ip) {
			error = -EINVAL;
			goto open_exec_out;
		}
		if (DM_EVENT_ENABLED(vp->v_vfsp, ip, DM_EVENT_READ)) {
			error = -XFS_SEND_DATA(mp, DM_EVENT_READ, vp,
					       0, 0, 0, NULL);
		}
	}
open_exec_out:
	return error;
}
#endif /* HAVE_FOP_OPEN_EXEC */

const struct file_operations xfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.readv		= xfs_file_readv,
	.writev		= xfs_file_writev,
	.aio_read	= xfs_file_aio_read,
	.aio_write	= xfs_file_aio_write,
	.sendfile	= xfs_file_sendfile,
	.splice_read	= xfs_file_splice_read,
	.splice_write	= xfs_file_splice_write,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.mmap		= xfs_file_mmap,
	.open		= xfs_file_open,
	.release	= xfs_file_release,
	.fsync		= xfs_file_fsync,
#ifdef HAVE_FOP_OPEN_EXEC
	.open_exec	= xfs_file_open_exec,
#endif
};

const struct file_operations xfs_invis_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.readv		= xfs_file_readv_invis,
	.writev		= xfs_file_writev_invis,
	.aio_read	= xfs_file_aio_read_invis,
	.aio_write	= xfs_file_aio_write_invis,
	.sendfile	= xfs_file_sendfile_invis,
	.splice_read	= xfs_file_splice_read_invis,
	.splice_write	= xfs_file_splice_write_invis,
	.unlocked_ioctl	= xfs_file_ioctl_invis,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_invis_ioctl,
#endif
	.mmap		= xfs_file_mmap,
	.open		= xfs_file_open,
	.release	= xfs_file_release,
	.fsync		= xfs_file_fsync,
};


const struct file_operations xfs_dir_file_operations = {
	.read		= generic_read_dir,
	.readdir	= xfs_file_readdir,
	.unlocked_ioctl	= xfs_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= xfs_file_compat_ioctl,
#endif
	.fsync		= xfs_file_fsync,
};

static struct vm_operations_struct xfs_file_vm_ops = {
	.nopage		= filemap_nopage,
	.populate	= filemap_populate,
};

#ifdef CONFIG_XFS_DMAPI
static struct vm_operations_struct xfs_dmapi_file_vm_ops = {
	.nopage		= xfs_vm_nopage,
	.populate	= filemap_populate,
#ifdef HAVE_VMOP_MPROTECT
	.mprotect	= xfs_vm_mprotect,
#endif
};
#endif /* CONFIG_XFS_DMAPI */
