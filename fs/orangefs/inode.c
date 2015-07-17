/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Linux VFS inode operations.
 */

#include "protocol.h"
#include "pvfs2-kernel.h"
#include "pvfs2-bufmap.h"

static int read_one_page(struct page *page)
{
	void *page_data;
	int ret;
	int max_block;
	ssize_t bytes_read = 0;
	struct inode *inode = page->mapping->host;
	const __u32 blocksize = PAGE_CACHE_SIZE;	/* inode->i_blksize */
	const __u32 blockbits = PAGE_CACHE_SHIFT;	/* inode->i_blkbits */

	gossip_debug(GOSSIP_INODE_DEBUG,
		    "pvfs2_readpage called with page %p\n",
		     page);
	page_data = pvfs2_kmap(page);

	max_block = ((inode->i_size / blocksize) + 1);

	if (page->index < max_block) {
		loff_t blockptr_offset = (((loff_t) page->index) << blockbits);

		bytes_read = pvfs2_inode_read(inode,
					      page_data,
					      blocksize,
					      &blockptr_offset,
					      inode->i_size);
	}
	/* only zero remaining unread portions of the page data */
	if (bytes_read > 0)
		memset(page_data + bytes_read, 0, blocksize - bytes_read);
	else
		memset(page_data, 0, blocksize);
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
	pvfs2_kunmap(page);
	/* unlock the page after the ->readpage() routine completes */
	unlock_page(page);
	return ret;
}

static int pvfs2_readpage(struct file *file, struct page *page)
{
	return read_one_page(page);
}

static int pvfs2_readpages(struct file *file,
			   struct address_space *mapping,
			   struct list_head *pages,
			   unsigned nr_pages)
{
	int page_idx;
	int ret;

	gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_readpages called\n");

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page;

		page = list_entry(pages->prev, struct page, lru);
		list_del(&page->lru);
		if (!add_to_page_cache(page,
				       mapping,
				       page->index,
				       GFP_KERNEL)) {
			ret = read_one_page(page);
			gossip_debug(GOSSIP_INODE_DEBUG,
				"failure adding page to cache, read_one_page returned: %d\n",
				ret);
	      } else {
			page_cache_release(page);
	      }
	}
	BUG_ON(!list_empty(pages));
	return 0;
}

static void pvfs2_invalidatepage(struct page *page,
				 unsigned int offset,
				 unsigned int length)
{
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2_invalidatepage called on page %p "
		     "(offset is %u)\n",
		     page,
		     offset);

	ClearPageUptodate(page);
	ClearPageMappedToDisk(page);
	return;

}

static int pvfs2_releasepage(struct page *page, gfp_t foo)
{
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2_releasepage called on page %p\n",
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
/*
static ssize_t pvfs2_direct_IO(int rw,
			struct kiocb *iocb,
			struct iov_iter *iter,
			loff_t offset)
{
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2_direct_IO: %s\n",
		     iocb->ki_filp->f_path.dentry->d_name.name);

	return -EINVAL;
}
*/

struct backing_dev_info pvfs2_backing_dev_info = {
	.name = "pvfs2",
	.ra_pages = 0,
	.capabilities = BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

/** PVFS2 implementation of address space operations */
const struct address_space_operations pvfs2_address_operations = {
	.readpage = pvfs2_readpage,
	.readpages = pvfs2_readpages,
	.invalidatepage = pvfs2_invalidatepage,
	.releasepage = pvfs2_releasepage,
/*	.direct_IO = pvfs2_direct_IO */
};

static int pvfs2_setattr_size(struct inode *inode, struct iattr *iattr)
{
	struct pvfs2_inode_s *pvfs2_inode = PVFS2_I(inode);
	struct pvfs2_kernel_op_s *new_op;
	loff_t orig_size = i_size_read(inode);
	int ret = -EINVAL;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "%s: %pU: Handle is %pU | fs_id %d | size is %llu\n",
		     __func__,
		     get_khandle_from_ino(inode),
		     &pvfs2_inode->refn.khandle,
		     pvfs2_inode->refn.fs_id,
		     iattr->ia_size);

	truncate_setsize(inode, iattr->ia_size);

	new_op = op_alloc(PVFS2_VFS_OP_TRUNCATE);
	if (!new_op)
		return -ENOMEM;

	new_op->upcall.req.truncate.refn = pvfs2_inode->refn;
	new_op->upcall.req.truncate.size = (__s64) iattr->ia_size;

	ret = service_operation(new_op, __func__,
				get_interruptible_flag(inode));

	/*
	 * the truncate has no downcall members to retrieve, but
	 * the status value tells us if it went through ok or not
	 */
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2: pvfs2_truncate got return value of %d\n",
		     ret);

	op_release(new_op);

	if (ret != 0)
		return ret;

	/*
	 * Only change the c/mtime if we are changing the size or we are
	 * explicitly asked to change it.  This handles the semantic difference
	 * between truncate() and ftruncate() as implemented in the VFS.
	 *
	 * The regular truncate() case without ATTR_CTIME and ATTR_MTIME is a
	 * special case where we need to update the times despite not having
	 * these flags set.  For all other operations the VFS set these flags
	 * explicitly if it wants a timestamp update.
	 */
	if (orig_size != i_size_read(inode) &&
	    !(iattr->ia_valid & (ATTR_CTIME | ATTR_MTIME))) {
		iattr->ia_ctime = iattr->ia_mtime =
			current_fs_time(inode->i_sb);
		iattr->ia_valid |= ATTR_CTIME | ATTR_MTIME;
	}

	return ret;
}

/*
 * Change attributes of an object referenced by dentry.
 */
int pvfs2_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int ret = -EINVAL;
	struct inode *inode = dentry->d_inode;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2_setattr: called on %s\n",
		     dentry->d_name.name);

	ret = inode_change_ok(inode, iattr);
	if (ret)
		goto out;

	if ((iattr->ia_valid & ATTR_SIZE) &&
	    iattr->ia_size != i_size_read(inode)) {
		ret = pvfs2_setattr_size(inode, iattr);
		if (ret)
			goto out;
	}

	setattr_copy(inode, iattr);
	mark_inode_dirty(inode);

	ret = pvfs2_inode_setattr(inode, iattr);
	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2_setattr: inode_setattr returned %d\n",
		     ret);

	if (!ret && (iattr->ia_valid & ATTR_MODE))
		/* change mod on a file that has ACLs */
		ret = posix_acl_chmod(inode, inode->i_mode);

out:
	gossip_debug(GOSSIP_INODE_DEBUG, "pvfs2_setattr: returning %d\n", ret);
	return ret;
}

/*
 * Obtain attributes of an object given a dentry
 */
int pvfs2_getattr(struct vfsmount *mnt,
		  struct dentry *dentry,
		  struct kstat *kstat)
{
	int ret = -ENOENT;
	struct inode *inode = dentry->d_inode;
	struct pvfs2_inode_s *pvfs2_inode = NULL;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2_getattr: called on %s\n",
		     dentry->d_name.name);

	/*
	 * Similar to the above comment, a getattr also expects that all
	 * fields/attributes of the inode would be refreshed. So again, we
	 * dont have too much of a choice but refresh all the attributes.
	 */
	ret = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT);
	if (ret == 0) {
		generic_fillattr(inode, kstat);
		/* override block size reported to stat */
		pvfs2_inode = PVFS2_I(inode);
		kstat->blksize = pvfs2_inode->blksize;
	} else {
		/* assume an I/O error and flag inode as bad */
		gossip_debug(GOSSIP_INODE_DEBUG,
			     "%s:%s:%d calling make bad inode\n",
			     __FILE__,
			     __func__,
			     __LINE__);
		pvfs2_make_bad_inode(inode);
	}
	return ret;
}

/* PVFS2 implementation of VFS inode operations for files */
struct inode_operations pvfs2_file_inode_operations = {
	.get_acl = pvfs2_get_acl,
	.set_acl = pvfs2_set_acl,
	.setattr = pvfs2_setattr,
	.getattr = pvfs2_getattr,
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.listxattr = pvfs2_listxattr,
	.removexattr = generic_removexattr,
};

static int pvfs2_init_iops(struct inode *inode)
{
	inode->i_mapping->a_ops = &pvfs2_address_operations;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &pvfs2_file_inode_operations;
		inode->i_fop = &pvfs2_file_operations;
		inode->i_blkbits = PAGE_CACHE_SHIFT;
		break;
	case S_IFLNK:
		inode->i_op = &pvfs2_symlink_inode_operations;
		break;
	case S_IFDIR:
		inode->i_op = &pvfs2_dir_inode_operations;
		inode->i_fop = &pvfs2_dir_operations;
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
 * Given a PVFS2 object identifier (fsid, handle), convert it into a ino_t type
 * that will be used as a hash-index from where the handle will
 * be searched for in the VFS hash table of inodes.
 */
static inline ino_t pvfs2_handle_hash(struct pvfs2_object_kref *ref)
{
	if (!ref)
		return 0;
	return pvfs2_khandle_to_ino(&(ref->khandle));
}

/*
 * Called to set up an inode from iget5_locked.
 */
static int pvfs2_set_inode(struct inode *inode, void *data)
{
	struct pvfs2_object_kref *ref = (struct pvfs2_object_kref *) data;
	struct pvfs2_inode_s *pvfs2_inode = NULL;

	/* Make sure that we have sane parameters */
	if (!data || !inode)
		return 0;
	pvfs2_inode = PVFS2_I(inode);
	if (!pvfs2_inode)
		return 0;
	pvfs2_inode->refn.fs_id = ref->fs_id;
	pvfs2_inode->refn.khandle = ref->khandle;
	return 0;
}

/*
 * Called to determine if handles match.
 */
static int pvfs2_test_inode(struct inode *inode, void *data)
{
	struct pvfs2_object_kref *ref = (struct pvfs2_object_kref *) data;
	struct pvfs2_inode_s *pvfs2_inode = NULL;

	pvfs2_inode = PVFS2_I(inode);
	return (!PVFS_khandle_cmp(&(pvfs2_inode->refn.khandle), &(ref->khandle))
		&& pvfs2_inode->refn.fs_id == ref->fs_id);
}

/*
 * Front-end to lookup the inode-cache maintained by the VFS using the PVFS2
 * file handle.
 *
 * @sb: the file system super block instance.
 * @ref: The PVFS2 object for which we are trying to locate an inode structure.
 */
struct inode *pvfs2_iget(struct super_block *sb, struct pvfs2_object_kref *ref)
{
	struct inode *inode = NULL;
	unsigned long hash;
	int error;

	hash = pvfs2_handle_hash(ref);
	inode = iget5_locked(sb, hash, pvfs2_test_inode, pvfs2_set_inode, ref);
	if (!inode || !(inode->i_state & I_NEW))
		return inode;

	error = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT);
	if (error) {
		iget_failed(inode);
		return ERR_PTR(error);
	}

	inode->i_ino = hash;	/* needed for stat etc */
	pvfs2_init_iops(inode);
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
struct inode *pvfs2_new_inode(struct super_block *sb, struct inode *dir,
		int mode, dev_t dev, struct pvfs2_object_kref *ref)
{
	unsigned long hash = pvfs2_handle_hash(ref);
	struct inode *inode;
	int error;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "pvfs2_get_custom_inode_common: called\n"
		     "(sb is %p | MAJOR(dev)=%u | MINOR(dev)=%u mode=%o)\n",
		     sb,
		     MAJOR(dev),
		     MINOR(dev),
		     mode);

	inode = new_inode(sb);
	if (!inode)
		return NULL;

	pvfs2_set_inode(inode, ref);
	inode->i_ino = hash;	/* needed for stat etc */

	error = pvfs2_inode_getattr(inode, PVFS_ATTR_SYS_ALL_NOHINT);
	if (error)
		goto out_iput;

	pvfs2_init_iops(inode);

	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_size = PAGE_CACHE_SIZE;
	inode->i_rdev = dev;

	error = insert_inode_locked4(inode, hash, pvfs2_test_inode, ref);
	if (error < 0)
		goto out_iput;

	gossip_debug(GOSSIP_INODE_DEBUG,
		     "Initializing ACL's for inode %pU\n",
		     get_khandle_from_ino(inode));
	pvfs2_init_acl(inode, dir);
	return inode;

out_iput:
	iput(inode);
	return ERR_PTR(error);
}
