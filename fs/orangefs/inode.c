// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS inode operations.
 */

#include <linux/bvec.h>
#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

static int read_one_page(struct page *page)
{
	int ret;
	int max_block;
	ssize_t bytes_read = 0;
	struct inode *inode = page->mapping->host;
	const __u32 blocksize = PAGE_SIZE;
	const __u32 blockbits = PAGE_SHIFT;
	struct iov_iter to;
	struct bio_vec bv = {.bv_page = page, .bv_len = PAGE_SIZE};

	iov_iter_bvec(&to, READ, &bv, 1, PAGE_SIZE);

	gossip_debug(GOSSIP_INODE_DEBUG,
		    "orangefs_readpage called with page %p\n",
		     page);

	max_block = ((inode->i_size / blocksize) + 1);

	if (page->index < max_block) {
		loff_t blockptr_offset = (((loff_t) page->index) << blockbits);

		bytes_read = orangefs_inode_read(inode,
						 &to,
						 &blockptr_offset,
						 inode->i_size);
	}
	/* this will only zero remaining unread portions of the page data */
	iov_iter_zero(~0U, &to);
	/* takes care of potential aliasing */
	flush_dcache_page(page);
	if (bytes_read < 0) {
		ret = bytes_read;
		SetPageError(page);
	} else {
		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
		ret = 0;
	}
	/* unlock the page after the ->readpage() routine completes */
	unlock_page(page);
	return ret;
}

static int orangefs_readpage(struct file *file, struct page *page)
{
	return read_one_page(page);
}

static int orangefs_readpages(struct file *file,
			   struct address_space *mapping,
			   struct list_head *pages,
			   unsigned nr_pages)
{
	int page_idx;
	int ret;

	gossip_debug(GOSSIP_INODE_DEBUG, "orangefs_readpages called\n");

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page;

		page = lru_to_page(pages);
		list_del(&page->lru);
		if (!add_to_page_cache(page,
				       mapping,
				       page->index,
				       readahead_gfp_mask(mapping))) {
			ret = read_one_page(page);
			gossip_debug(GOSSIP_INODE_DEBUG,
				"failure adding page to cache, read_one_page returned: %d\n",
				ret);
	      } else {
			put_page(page);
	      }
	}
	BUG_ON(!list_empty(pages));
	return 0;
}

static void orangefs_invalidatepage(struct page *page,
				 unsigned int offset,
				 unsigned int length)
{
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "orangefs_invalidatepage called on page %p "
		     "(offset is %u)\n",
		     page,
		     offset);

	ClearPageUptodate(page);
	ClearPageMappedToDisk(page);
	return;

}

static int orangefs_releasepage(struct page *page, gfp_t foo)
{
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "orangefs_releasepage called on page %p\n",
		     page);
	return 0;
}

/*
 * Having a direct_IO entry point in the address_space_operations
 * struct causes the kernel to allows us to use O_DIRECT on
 * open. Nothing will ever call this thing, but in the future we
 * will need to be able to use O_DIRECT on open in order to support
 * AIO. Modeled after NFS, they do this too.
 */

static ssize_t orangefs_direct_IO(struct kiocb *iocb,
				  struct iov_iter *iter)
{
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "orangefs_direct_IO: %pD\n",
		     iocb->ki_filp);

	return -EINVAL;
}

/** ORANGEFS2 implementation of address space operations */
static const struct address_space_operations orangefs_address_operations = {
	.readpage = orangefs_readpage,
	.readpages = orangefs_readpages,
	.invalidatepage = orangefs_invalidatepage,
	.releasepage = orangefs_releasepage,
	.direct_IO = orangefs_direct_IO,
};

static int orangefs_setattr_size(struct inode *inode, struct iattr *iattr)
{
	struct orangefs_inode_s *orangefs_inode = ORANGEFS_I(inode);
	struct orangefs_kernel_op_s *new_op;
	loff_t orig_size;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "%s: %pU: Handle is %pU | fs_id %d | size is %llu\n",
		     __func__,
		     get_khandle_from_ino(inode),
		     &orangefs_inode->refn.khandle,
		     orangefs_inode->refn.fs_id,
		     iattr->ia_size);

	/* Ensure that we have a up to date size, so we know if it changed. */
	ret = orangefs_inode_getattr(inode, 0, 1, STATX_SIZE);
	if (ret == -ESTALE)
		ret = -EIO;
	if (ret) {
		gossip_err("%s: orangefs_inode_getattr failed, ret:%d:.\n",
		    __func__, ret);
		return ret;
	}
	orig_size = i_size_read(inode);

	truncate_setsize(inode, iattr->ia_size);

	new_op = op_alloc(ORANGEFS_VFS_OP_TRUNCATE);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.truncate.refn = orangefs_inode->refn;
	new_op->upcall.req.truncate.size = (__s64) iattr->ia_size;

	ret = service_operation(new_op,
		__func__,
		get_interruptible_flag(inode));

	/*
	 * the truncate has no downcall members to retrieve, but
	 * the status value tells us if it went through ok or not
	 */
	gossip_debug(GOSSIP_INODE_DEBUG, "%s: ret:%d:\n", __func__, ret);

	op_release(new_op);

	if (ret != 0)
		return ret;

	if (orig_size != i_size_read(inode))
		iattr->ia_valid |= ATTR_CTIME | ATTR_MTIME;

	return ret;
}

/*
 * Change attributes of an object referenced by dentry.
 */
int orangefs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int ret = -EINVAL;
	struct inode *inode = dentry->d_inode;

	gossip_debug(GOSSIP_INODE_DEBUG,
		"%s: called on %pd\n",
		__func__,
		dentry);

	ret = setattr_prepare(dentry, iattr);
	if (ret)
		goto out;

	if (iattr->ia_valid & ATTR_SIZE) {
		ret = orangefs_setattr_size(inode, iattr);
		if (ret)
			goto out;
	}

	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);

	ret = orangefs_inode_setattr(inode, iattr);
	gossip_debug(GOSSIP_INODE_DEBUG,
		"%s: orangefs_inode_setattr returned %d\n",
		__func__,
		ret);

	if (!ret && (iattr->ia_valid & ATTR_MODE))
		/* change mod on a file that has ACLs */
		ret = posix_acl_chmod(inode, inode->i_mode);

out:
	gossip_debug(GOSSIP_INODE_DEBUG, "%s: ret:%d:\n", __func__, ret);
	return ret;
}

/*
 * Obtain attributes of an object given a dentry
 */
int orangefs_getattr(const struct path *path, struct kstat *stat,
		     u32 request_mask, unsigned int flags)
{
	int ret = -ENOENT;
	struct inode *inode = path->dentry->d_inode;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "orangefs_getattr: called on %pd\n",
		     path->dentry);

	ret = orangefs_inode_getattr(inode, 0, 0, request_mask);
	if (ret == 0) {
		generic_fillattr(inode, stat);

		/* override block size reported to stat */
		if (!(request_mask & STATX_SIZE))
			stat->result_mask &= ~STATX_SIZE;

		stat->attributes_mask = STATX_ATTR_IMMUTABLE |
		    STATX_ATTR_APPEND;
		if (inode->i_flags & S_IMMUTABLE)
			stat->attributes |= STATX_ATTR_IMMUTABLE;
		if (inode->i_flags & S_APPEND)
			stat->attributes |= STATX_ATTR_APPEND;
	}
	return ret;
}

int orangefs_permission(struct inode *inode, int mask)
{
	int ret;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	gossip_debug(GOSSIP_INODE_DEBUG, "%s: refreshing\n", __func__);

	/* Make sure the permission (and other common attrs) are up to date. */
	ret = orangefs_inode_getattr(inode, 0, 0, STATX_MODE);
	if (ret < 0)
		return ret;

	return generic_permission(inode, mask);
}

int orangefs_update_time(struct inode *inode, struct timespec64 *time, int flags)
{
	struct iattr iattr;
	gossip_debug(GOSSIP_INODE_DEBUG, "orangefs_update_time: %pU\n",
	    get_khandle_from_ino(inode));
	generic_update_time(inode, time, flags);
	memset(&iattr, 0, sizeof iattr);
        if (flags & S_ATIME)
		iattr.ia_valid |= ATTR_ATIME;
	if (flags & S_CTIME)
		iattr.ia_valid |= ATTR_CTIME;
	if (flags & S_MTIME)
		iattr.ia_valid |= ATTR_MTIME;
	return orangefs_inode_setattr(inode, &iattr);
}

/* ORANGEFS2 implementation of VFS inode operations for files */
static const struct inode_operations orangefs_file_inode_operations = {
	.get_acl = orangefs_get_acl,
	.set_acl = orangefs_set_acl,
	.setattr = orangefs_setattr,
	.getattr = orangefs_getattr,
	.listxattr = orangefs_listxattr,
	.permission = orangefs_permission,
	.update_time = orangefs_update_time,
};

static int orangefs_init_iops(struct inode *inode)
{
	inode->i_mapping->a_ops = &orangefs_address_operations;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &orangefs_file_inode_operations;
		inode->i_fop = &orangefs_file_operations;
		break;
	case S_IFLNK:
		inode->i_op = &orangefs_symlink_inode_operations;
		break;
	case S_IFDIR:
		inode->i_op = &orangefs_dir_inode_operations;
		inode->i_fop = &orangefs_dir_operations;
		break;
	default:
		gossip_debug(GOSSIP_INODE_DEBUG,
			     "%s: unsupported mode\n",
			     __func__);
		return -EINVAL;
	}

	return 0;
}

/*
 * Given an ORANGEFS object identifier (fsid, handle), convert it into
 * a ino_t type that will be used as a hash-index from where the handle will
 * be searched for in the VFS hash table of inodes.
 */
static inline ino_t orangefs_handle_hash(struct orangefs_object_kref *ref)
{
	if (!ref)
		return 0;
	return orangefs_khandle_to_ino(&(ref->khandle));
}

/*
 * Called to set up an inode from iget5_locked.
 */
static int orangefs_set_inode(struct inode *inode, void *data)
{
	struct orangefs_object_kref *ref = (struct orangefs_object_kref *) data;
	ORANGEFS_I(inode)->refn.fs_id = ref->fs_id;
	ORANGEFS_I(inode)->refn.khandle = ref->khandle;
	return 0;
}

/*
 * Called to determine if handles match.
 */
static int orangefs_test_inode(struct inode *inode, void *data)
{
	struct orangefs_object_kref *ref = (struct orangefs_object_kref *) data;
	struct orangefs_inode_s *orangefs_inode = NULL;

	orangefs_inode = ORANGEFS_I(inode);
	/* test handles and fs_ids... */
	return (!ORANGEFS_khandle_cmp(&(orangefs_inode->refn.khandle),
				&(ref->khandle)) &&
			orangefs_inode->refn.fs_id == ref->fs_id);
}

/*
 * Front-end to lookup the inode-cache maintained by the VFS using the ORANGEFS
 * file handle.
 *
 * @sb: the file system super block instance.
 * @ref: The ORANGEFS object for which we are trying to locate an inode.
 */
struct inode *orangefs_iget(struct super_block *sb,
		struct orangefs_object_kref *ref)
{
	struct inode *inode = NULL;
	unsigned long hash;
	int error;

	hash = orangefs_handle_hash(ref);
	inode = iget5_locked(sb,
			hash,
			orangefs_test_inode,
			orangefs_set_inode,
			ref);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (!(inode->i_state & I_NEW))
		return inode;

	error = orangefs_inode_getattr(inode, 1, 1, STATX_ALL);
	if (error) {
		iget_failed(inode);
		return ERR_PTR(error);
	}

	inode->i_ino = hash;	/* needed for stat etc */
	orangefs_init_iops(inode);
	unlock_new_inode(inode);

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "iget handle %pU, fsid %d hash %ld i_ino %lu\n",
		     &ref->khandle,
		     ref->fs_id,
		     hash,
		     inode->i_ino);

	return inode;
}

/*
 * Allocate an inode for a newly created file and insert it into the inode hash.
 */
struct inode *orangefs_new_inode(struct super_block *sb, struct inode *dir,
		int mode, dev_t dev, struct orangefs_object_kref *ref)
{
	unsigned long hash = orangefs_handle_hash(ref);
	struct inode *inode;
	int error;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "%s:(sb is %p | MAJOR(dev)=%u | MINOR(dev)=%u mode=%o)\n",
		     __func__,
		     sb,
		     MAJOR(dev),
		     MINOR(dev),
		     mode);

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	orangefs_set_inode(inode, ref);
	inode->i_ino = hash;	/* needed for stat etc */

	error = orangefs_inode_getattr(inode, 1, 1, STATX_ALL);
	if (error)
		goto out_iput;

	orangefs_init_iops(inode);

	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_size = PAGE_SIZE;
	inode->i_rdev = dev;

	error = insert_inode_locked4(inode, hash, orangefs_test_inode, ref);
	if (error < 0)
		goto out_iput;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "Initializing ACL's for inode %pU\n",
		     get_khandle_from_ino(inode));
	orangefs_init_acl(inode, dir);
	return inode;

out_iput:
	iput(inode);
	return ERR_PTR(error);
}
