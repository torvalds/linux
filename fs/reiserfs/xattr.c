/*
 * linux/fs/reiserfs/xattr.c
 *
 * Copyright (c) 2002 by Jeff Mahoney, <jeffm@suse.com>
 *
 */

/*
 * In order to implement EA/ACLs in a clean, backwards compatible manner,
 * they are implemented as files in a "private" directory.
 * Each EA is in it's own file, with the directory layout like so (/ is assumed
 * to be relative to fs root). Inside the /.reiserfs_priv/xattrs directory,
 * directories named using the capital-hex form of the objectid and
 * generation number are used. Inside each directory are individual files
 * named with the name of the extended attribute.
 *
 * So, for objectid 12648430, we could have:
 * /.reiserfs_priv/xattrs/C0FFEE.0/system.posix_acl_access
 * /.reiserfs_priv/xattrs/C0FFEE.0/system.posix_acl_default
 * /.reiserfs_priv/xattrs/C0FFEE.0/user.Content-Type
 * .. or similar.
 *
 * The file contents are the text of the EA. The size is known based on the
 * stat data describing the file.
 *
 * In the case of system.posix_acl_access and system.posix_acl_default, since
 * these are special cases for filesystem ACLs, they are interpreted by the
 * kernel, in addition, they are negatively and positively cached and attached
 * to the inode so that unnecessary lookups are avoided.
 */

#include <linux/reiserfs_fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <linux/reiserfs_acl.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <linux/smp_lock.h>
#include <linux/stat.h>
#include <asm/semaphore.h>

#define FL_READONLY 128
#define FL_DIR_SEM_HELD 256
#define PRIVROOT_NAME ".reiserfs_priv"
#define XAROOT_NAME   "xattrs"

static struct reiserfs_xattr_handler *find_xattr_handler_prefix(const char
								*prefix);

static struct dentry *create_xa_root(struct super_block *sb)
{
	struct dentry *privroot = dget(REISERFS_SB(sb)->priv_root);
	struct dentry *xaroot;

	/* This needs to be created at mount-time */
	if (!privroot)
		return ERR_PTR(-EOPNOTSUPP);

	xaroot = lookup_one_len(XAROOT_NAME, privroot, strlen(XAROOT_NAME));
	if (IS_ERR(xaroot)) {
		goto out;
	} else if (!xaroot->d_inode) {
		int err;
		down(&privroot->d_inode->i_sem);
		err =
		    privroot->d_inode->i_op->mkdir(privroot->d_inode, xaroot,
						   0700);
		up(&privroot->d_inode->i_sem);

		if (err) {
			dput(xaroot);
			dput(privroot);
			return ERR_PTR(err);
		}
		REISERFS_SB(sb)->xattr_root = dget(xaroot);
	}

      out:
	dput(privroot);
	return xaroot;
}

/* This will return a dentry, or error, refering to the xa root directory.
 * If the xa root doesn't exist yet, the dentry will be returned without
 * an associated inode. This dentry can be used with ->mkdir to create
 * the xa directory. */
static struct dentry *__get_xa_root(struct super_block *s)
{
	struct dentry *privroot = dget(REISERFS_SB(s)->priv_root);
	struct dentry *xaroot = NULL;

	if (IS_ERR(privroot) || !privroot)
		return privroot;

	xaroot = lookup_one_len(XAROOT_NAME, privroot, strlen(XAROOT_NAME));
	if (IS_ERR(xaroot)) {
		goto out;
	} else if (!xaroot->d_inode) {
		dput(xaroot);
		xaroot = NULL;
		goto out;
	}

	REISERFS_SB(s)->xattr_root = dget(xaroot);

      out:
	dput(privroot);
	return xaroot;
}

/* Returns the dentry (or NULL) referring to the root of the extended
 * attribute directory tree. If it has already been retreived, it is used.
 * Otherwise, we attempt to retreive it from disk. It may also return
 * a pointer-encoded error.
 */
static inline struct dentry *get_xa_root(struct super_block *s)
{
	struct dentry *dentry = dget(REISERFS_SB(s)->xattr_root);

	if (!dentry)
		dentry = __get_xa_root(s);

	return dentry;
}

/* Opens the directory corresponding to the inode's extended attribute store.
 * If flags allow, the tree to the directory may be created. If creation is
 * prohibited, -ENODATA is returned. */
static struct dentry *open_xa_dir(const struct inode *inode, int flags)
{
	struct dentry *xaroot, *xadir;
	char namebuf[17];

	xaroot = get_xa_root(inode->i_sb);
	if (IS_ERR(xaroot)) {
		return xaroot;
	} else if (!xaroot) {
		if (flags == 0 || flags & XATTR_CREATE) {
			xaroot = create_xa_root(inode->i_sb);
			if (IS_ERR(xaroot))
				return xaroot;
		}
		if (!xaroot)
			return ERR_PTR(-ENODATA);
	}

	/* ok, we have xaroot open */

	snprintf(namebuf, sizeof(namebuf), "%X.%X",
		 le32_to_cpu(INODE_PKEY(inode)->k_objectid),
		 inode->i_generation);
	xadir = lookup_one_len(namebuf, xaroot, strlen(namebuf));
	if (IS_ERR(xadir)) {
		dput(xaroot);
		return xadir;
	}

	if (!xadir->d_inode) {
		int err;
		if (flags == 0 || flags & XATTR_CREATE) {
			/* Although there is nothing else trying to create this directory,
			 * another directory with the same hash may be created, so we need
			 * to protect against that */
			err =
			    xaroot->d_inode->i_op->mkdir(xaroot->d_inode, xadir,
							 0700);
			if (err) {
				dput(xaroot);
				dput(xadir);
				return ERR_PTR(err);
			}
		}
		if (!xadir->d_inode) {
			dput(xaroot);
			dput(xadir);
			return ERR_PTR(-ENODATA);
		}
	}

	dput(xaroot);
	return xadir;
}

/* Returns a dentry corresponding to a specific extended attribute file
 * for the inode. If flags allow, the file is created. Otherwise, a
 * valid or negative dentry, or an error is returned. */
static struct dentry *get_xa_file_dentry(const struct inode *inode,
					 const char *name, int flags)
{
	struct dentry *xadir, *xafile;
	int err = 0;

	xadir = open_xa_dir(inode, flags);
	if (IS_ERR(xadir)) {
		return ERR_PTR(PTR_ERR(xadir));
	} else if (xadir && !xadir->d_inode) {
		dput(xadir);
		return ERR_PTR(-ENODATA);
	}

	xafile = lookup_one_len(name, xadir, strlen(name));
	if (IS_ERR(xafile)) {
		dput(xadir);
		return ERR_PTR(PTR_ERR(xafile));
	}

	if (xafile->d_inode) {	/* file exists */
		if (flags & XATTR_CREATE) {
			err = -EEXIST;
			dput(xafile);
			goto out;
		}
	} else if (flags & XATTR_REPLACE || flags & FL_READONLY) {
		goto out;
	} else {
		/* inode->i_sem is down, so nothing else can try to create
		 * the same xattr */
		err = xadir->d_inode->i_op->create(xadir->d_inode, xafile,
						   0700 | S_IFREG, NULL);

		if (err) {
			dput(xafile);
			goto out;
		}
	}

      out:
	dput(xadir);
	if (err)
		xafile = ERR_PTR(err);
	return xafile;
}

/* Opens a file pointer to the attribute associated with inode */
static struct file *open_xa_file(const struct inode *inode, const char *name,
				 int flags)
{
	struct dentry *xafile;
	struct file *fp;

	xafile = get_xa_file_dentry(inode, name, flags);
	if (IS_ERR(xafile))
		return ERR_PTR(PTR_ERR(xafile));
	else if (!xafile->d_inode) {
		dput(xafile);
		return ERR_PTR(-ENODATA);
	}

	fp = dentry_open(xafile, NULL, O_RDWR);
	/* dentry_open dputs the dentry if it fails */

	return fp;
}

/*
 * this is very similar to fs/reiserfs/dir.c:reiserfs_readdir, but
 * we need to drop the path before calling the filldir struct.  That
 * would be a big performance hit to the non-xattr case, so I've copied
 * the whole thing for now. --clm
 *
 * the big difference is that I go backwards through the directory,
 * and don't mess with f->f_pos, but the idea is the same.  Do some
 * action on each and every entry in the directory.
 *
 * we're called with i_sem held, so there are no worries about the directory
 * changing underneath us.
 */
static int __xattr_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct cpu_key pos_key;	/* key of current position in the directory (key of directory entry) */
	INITIALIZE_PATH(path_to_entry);
	struct buffer_head *bh;
	int entry_num;
	struct item_head *ih, tmp_ih;
	int search_res;
	char *local_buf;
	loff_t next_pos;
	char small_buf[32];	/* avoid kmalloc if we can */
	struct reiserfs_de_head *deh;
	int d_reclen;
	char *d_name;
	off_t d_off;
	ino_t d_ino;
	struct reiserfs_dir_entry de;

	/* form key for search the next directory entry using f_pos field of
	   file structure */
	next_pos = max_reiserfs_offset(inode);

	while (1) {
	      research:
		if (next_pos <= DOT_DOT_OFFSET)
			break;
		make_cpu_key(&pos_key, inode, next_pos, TYPE_DIRENTRY, 3);

		search_res =
		    search_by_entry_key(inode->i_sb, &pos_key, &path_to_entry,
					&de);
		if (search_res == IO_ERROR) {
			// FIXME: we could just skip part of directory which could
			// not be read
			pathrelse(&path_to_entry);
			return -EIO;
		}

		if (search_res == NAME_NOT_FOUND)
			de.de_entry_num--;

		set_de_name_and_namelen(&de);
		entry_num = de.de_entry_num;
		deh = &(de.de_deh[entry_num]);

		bh = de.de_bh;
		ih = de.de_ih;

		if (!is_direntry_le_ih(ih)) {
			reiserfs_warning(inode->i_sb, "not direntry %h", ih);
			break;
		}
		copy_item_head(&tmp_ih, ih);

		/* we must have found item, that is item of this directory, */
		RFALSE(COMP_SHORT_KEYS(&(ih->ih_key), &pos_key),
		       "vs-9000: found item %h does not match to dir we readdir %K",
		       ih, &pos_key);

		if (deh_offset(deh) <= DOT_DOT_OFFSET) {
			break;
		}

		/* look for the previous entry in the directory */
		next_pos = deh_offset(deh) - 1;

		if (!de_visible(deh))
			/* it is hidden entry */
			continue;

		d_reclen = entry_length(bh, ih, entry_num);
		d_name = B_I_DEH_ENTRY_FILE_NAME(bh, ih, deh);
		d_off = deh_offset(deh);
		d_ino = deh_objectid(deh);

		if (!d_name[d_reclen - 1])
			d_reclen = strlen(d_name);

		if (d_reclen > REISERFS_MAX_NAME(inode->i_sb->s_blocksize)) {
			/* too big to send back to VFS */
			continue;
		}

		/* Ignore the .reiserfs_priv entry */
		if (reiserfs_xattrs(inode->i_sb) &&
		    !old_format_only(inode->i_sb) &&
		    deh_objectid(deh) ==
		    le32_to_cpu(INODE_PKEY
				(REISERFS_SB(inode->i_sb)->priv_root->d_inode)->
				k_objectid))
			continue;

		if (d_reclen <= 32) {
			local_buf = small_buf;
		} else {
			local_buf =
			    reiserfs_kmalloc(d_reclen, GFP_NOFS, inode->i_sb);
			if (!local_buf) {
				pathrelse(&path_to_entry);
				return -ENOMEM;
			}
			if (item_moved(&tmp_ih, &path_to_entry)) {
				reiserfs_kfree(local_buf, d_reclen,
					       inode->i_sb);

				/* sigh, must retry.  Do this same offset again */
				next_pos = d_off;
				goto research;
			}
		}

		// Note, that we copy name to user space via temporary
		// buffer (local_buf) because filldir will block if
		// user space buffer is swapped out. At that time
		// entry can move to somewhere else
		memcpy(local_buf, d_name, d_reclen);

		/* the filldir function might need to start transactions,
		 * or do who knows what.  Release the path now that we've
		 * copied all the important stuff out of the deh
		 */
		pathrelse(&path_to_entry);

		if (filldir(dirent, local_buf, d_reclen, d_off, d_ino,
			    DT_UNKNOWN) < 0) {
			if (local_buf != small_buf) {
				reiserfs_kfree(local_buf, d_reclen,
					       inode->i_sb);
			}
			goto end;
		}
		if (local_buf != small_buf) {
			reiserfs_kfree(local_buf, d_reclen, inode->i_sb);
		}
	}			/* while */

      end:
	pathrelse(&path_to_entry);
	return 0;
}

/*
 * this could be done with dedicated readdir ops for the xattr files,
 * but I want to get something working asap
 * this is stolen from vfs_readdir
 *
 */
static
int xattr_readdir(struct file *file, filldir_t filler, void *buf)
{
	struct inode *inode = file->f_dentry->d_inode;
	int res = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;
	down(&inode->i_sem);
//        down(&inode->i_zombie);
	res = -ENOENT;
	if (!IS_DEADDIR(inode)) {
		lock_kernel();
		res = __xattr_readdir(file, buf, filler);
		unlock_kernel();
	}
//        up(&inode->i_zombie);
	up(&inode->i_sem);
      out:
	return res;
}

/* Internal operations on file data */
static inline void reiserfs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}

static struct page *reiserfs_get_page(struct inode *dir, unsigned long n)
{
	struct address_space *mapping = dir->i_mapping;
	struct page *page;
	/* We can deadlock if we try to free dentries,
	   and an unlink/rmdir has just occured - GFP_NOFS avoids this */
	mapping->flags = (mapping->flags & ~__GFP_BITS_MASK) | GFP_NOFS;
	page = read_cache_page(mapping, n,
			       (filler_t *) mapping->a_ops->readpage, NULL);
	if (!IS_ERR(page)) {
		wait_on_page_locked(page);
		kmap(page);
		if (!PageUptodate(page))
			goto fail;

		if (PageError(page))
			goto fail;
	}
	return page;

      fail:
	reiserfs_put_page(page);
	return ERR_PTR(-EIO);
}

static inline __u32 xattr_hash(const char *msg, int len)
{
	return csum_partial(msg, len, 0);
}

/* Generic extended attribute operations that can be used by xa plugins */

/*
 * inode->i_sem: down
 */
int
reiserfs_xattr_set(struct inode *inode, const char *name, const void *buffer,
		   size_t buffer_size, int flags)
{
	int err = 0;
	struct file *fp;
	struct page *page;
	char *data;
	struct address_space *mapping;
	size_t file_pos = 0;
	size_t buffer_pos = 0;
	struct inode *xinode;
	struct iattr newattrs;
	__u32 xahash = 0;

	if (IS_RDONLY(inode))
		return -EROFS;

	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		return -EPERM;

	if (get_inode_sd_version(inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	/* Empty xattrs are ok, they're just empty files, no hash */
	if (buffer && buffer_size)
		xahash = xattr_hash(buffer, buffer_size);

      open_file:
	fp = open_xa_file(inode, name, flags);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		goto out;
	}

	xinode = fp->f_dentry->d_inode;
	REISERFS_I(inode)->i_flags |= i_has_xattr_dir;

	/* we need to copy it off.. */
	if (xinode->i_nlink > 1) {
		fput(fp);
		err = reiserfs_xattr_del(inode, name);
		if (err < 0)
			goto out;
		/* We just killed the old one, we're not replacing anymore */
		if (flags & XATTR_REPLACE)
			flags &= ~XATTR_REPLACE;
		goto open_file;
	}

	/* Resize it so we're ok to write there */
	newattrs.ia_size = buffer_size;
	newattrs.ia_valid = ATTR_SIZE | ATTR_CTIME;
	down(&xinode->i_sem);
	err = notify_change(fp->f_dentry, &newattrs);
	if (err)
		goto out_filp;

	mapping = xinode->i_mapping;
	while (buffer_pos < buffer_size || buffer_pos == 0) {
		size_t chunk;
		size_t skip = 0;
		size_t page_offset = (file_pos & (PAGE_CACHE_SIZE - 1));
		if (buffer_size - buffer_pos > PAGE_CACHE_SIZE)
			chunk = PAGE_CACHE_SIZE;
		else
			chunk = buffer_size - buffer_pos;

		page = reiserfs_get_page(xinode, file_pos >> PAGE_CACHE_SHIFT);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out_filp;
		}

		lock_page(page);
		data = page_address(page);

		if (file_pos == 0) {
			struct reiserfs_xattr_header *rxh;
			skip = file_pos = sizeof(struct reiserfs_xattr_header);
			if (chunk + skip > PAGE_CACHE_SIZE)
				chunk = PAGE_CACHE_SIZE - skip;
			rxh = (struct reiserfs_xattr_header *)data;
			rxh->h_magic = cpu_to_le32(REISERFS_XATTR_MAGIC);
			rxh->h_hash = cpu_to_le32(xahash);
		}

		err = mapping->a_ops->prepare_write(fp, page, page_offset,
						    page_offset + chunk + skip);
		if (!err) {
			if (buffer)
				memcpy(data + skip, buffer + buffer_pos, chunk);
			err =
			    mapping->a_ops->commit_write(fp, page, page_offset,
							 page_offset + chunk +
							 skip);
		}
		unlock_page(page);
		reiserfs_put_page(page);
		buffer_pos += chunk;
		file_pos += chunk;
		skip = 0;
		if (err || buffer_size == 0 || !buffer)
			break;
	}

	/* We can't mark the inode dirty if it's not hashed. This is the case
	 * when we're inheriting the default ACL. If we dirty it, the inode
	 * gets marked dirty, but won't (ever) make it onto the dirty list until
	 * it's synced explicitly to clear I_DIRTY. This is bad. */
	if (!hlist_unhashed(&inode->i_hash)) {
		inode->i_ctime = CURRENT_TIME_SEC;
		mark_inode_dirty(inode);
	}

      out_filp:
	up(&xinode->i_sem);
	fput(fp);

      out:
	return err;
}

/*
 * inode->i_sem: down
 */
int
reiserfs_xattr_get(const struct inode *inode, const char *name, void *buffer,
		   size_t buffer_size)
{
	ssize_t err = 0;
	struct file *fp;
	size_t isize;
	size_t file_pos = 0;
	size_t buffer_pos = 0;
	struct page *page;
	struct inode *xinode;
	__u32 hash = 0;

	if (name == NULL)
		return -EINVAL;

	/* We can't have xattrs attached to v1 items since they don't have
	 * generation numbers */
	if (get_inode_sd_version(inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	fp = open_xa_file(inode, name, FL_READONLY);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		goto out;
	}

	xinode = fp->f_dentry->d_inode;
	isize = xinode->i_size;
	REISERFS_I(inode)->i_flags |= i_has_xattr_dir;

	/* Just return the size needed */
	if (buffer == NULL) {
		err = isize - sizeof(struct reiserfs_xattr_header);
		goto out_dput;
	}

	if (buffer_size < isize - sizeof(struct reiserfs_xattr_header)) {
		err = -ERANGE;
		goto out_dput;
	}

	while (file_pos < isize) {
		size_t chunk;
		char *data;
		size_t skip = 0;
		if (isize - file_pos > PAGE_CACHE_SIZE)
			chunk = PAGE_CACHE_SIZE;
		else
			chunk = isize - file_pos;

		page = reiserfs_get_page(xinode, file_pos >> PAGE_CACHE_SHIFT);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto out_dput;
		}

		lock_page(page);
		data = page_address(page);
		if (file_pos == 0) {
			struct reiserfs_xattr_header *rxh =
			    (struct reiserfs_xattr_header *)data;
			skip = file_pos = sizeof(struct reiserfs_xattr_header);
			chunk -= skip;
			/* Magic doesn't match up.. */
			if (rxh->h_magic != cpu_to_le32(REISERFS_XATTR_MAGIC)) {
				unlock_page(page);
				reiserfs_put_page(page);
				reiserfs_warning(inode->i_sb,
						 "Invalid magic for xattr (%s) "
						 "associated with %k", name,
						 INODE_PKEY(inode));
				err = -EIO;
				goto out_dput;
			}
			hash = le32_to_cpu(rxh->h_hash);
		}
		memcpy(buffer + buffer_pos, data + skip, chunk);
		unlock_page(page);
		reiserfs_put_page(page);
		file_pos += chunk;
		buffer_pos += chunk;
		skip = 0;
	}
	err = isize - sizeof(struct reiserfs_xattr_header);

	if (xattr_hash(buffer, isize - sizeof(struct reiserfs_xattr_header)) !=
	    hash) {
		reiserfs_warning(inode->i_sb,
				 "Invalid hash for xattr (%s) associated "
				 "with %k", name, INODE_PKEY(inode));
		err = -EIO;
	}

      out_dput:
	fput(fp);

      out:
	return err;
}

static int
__reiserfs_xattr_del(struct dentry *xadir, const char *name, int namelen)
{
	struct dentry *dentry;
	struct inode *dir = xadir->d_inode;
	int err = 0;

	dentry = lookup_one_len(name, xadir, namelen);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out;
	} else if (!dentry->d_inode) {
		err = -ENODATA;
		goto out_file;
	}

	/* Skip directories.. */
	if (S_ISDIR(dentry->d_inode->i_mode))
		goto out_file;

	if (!is_reiserfs_priv_object(dentry->d_inode)) {
		reiserfs_warning(dir->i_sb, "OID %08x [%.*s/%.*s] doesn't have "
				 "priv flag set [parent is %sset].",
				 le32_to_cpu(INODE_PKEY(dentry->d_inode)->
					     k_objectid), xadir->d_name.len,
				 xadir->d_name.name, namelen, name,
				 is_reiserfs_priv_object(xadir->
							 d_inode) ? "" :
				 "not ");
		dput(dentry);
		return -EIO;
	}

	err = dir->i_op->unlink(dir, dentry);
	if (!err)
		d_delete(dentry);

      out_file:
	dput(dentry);

      out:
	return err;
}

int reiserfs_xattr_del(struct inode *inode, const char *name)
{
	struct dentry *dir;
	int err;

	if (IS_RDONLY(inode))
		return -EROFS;

	dir = open_xa_dir(inode, FL_READONLY);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto out;
	}

	err = __reiserfs_xattr_del(dir, name, strlen(name));
	dput(dir);

	if (!err) {
		inode->i_ctime = CURRENT_TIME_SEC;
		mark_inode_dirty(inode);
	}

      out:
	return err;
}

/* The following are side effects of other operations that aren't explicitly
 * modifying extended attributes. This includes operations such as permissions
 * or ownership changes, object deletions, etc. */

static int
reiserfs_delete_xattrs_filler(void *buf, const char *name, int namelen,
			      loff_t offset, ino_t ino, unsigned int d_type)
{
	struct dentry *xadir = (struct dentry *)buf;

	return __reiserfs_xattr_del(xadir, name, namelen);

}

/* This is called w/ inode->i_sem downed */
int reiserfs_delete_xattrs(struct inode *inode)
{
	struct file *fp;
	struct dentry *dir, *root;
	int err = 0;

	/* Skip out, an xattr has no xattrs associated with it */
	if (is_reiserfs_priv_object(inode) ||
	    get_inode_sd_version(inode) == STAT_DATA_V1 ||
	    !reiserfs_xattrs(inode->i_sb)) {
		return 0;
	}
	reiserfs_read_lock_xattrs(inode->i_sb);
	dir = open_xa_dir(inode, FL_READONLY);
	reiserfs_read_unlock_xattrs(inode->i_sb);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		goto out;
	} else if (!dir->d_inode) {
		dput(dir);
		return 0;
	}

	fp = dentry_open(dir, NULL, O_RDWR);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		/* dentry_open dputs the dentry if it fails */
		goto out;
	}

	lock_kernel();
	err = xattr_readdir(fp, reiserfs_delete_xattrs_filler, dir);
	if (err) {
		unlock_kernel();
		goto out_dir;
	}

	/* Leftovers besides . and .. -- that's not good. */
	if (dir->d_inode->i_nlink <= 2) {
		root = get_xa_root(inode->i_sb);
		reiserfs_write_lock_xattrs(inode->i_sb);
		err = vfs_rmdir(root->d_inode, dir);
		reiserfs_write_unlock_xattrs(inode->i_sb);
		dput(root);
	} else {
		reiserfs_warning(inode->i_sb,
				 "Couldn't remove all entries in directory");
	}
	unlock_kernel();

      out_dir:
	fput(fp);

      out:
	if (!err)
		REISERFS_I(inode)->i_flags =
		    REISERFS_I(inode)->i_flags & ~i_has_xattr_dir;
	return err;
}

struct reiserfs_chown_buf {
	struct inode *inode;
	struct dentry *xadir;
	struct iattr *attrs;
};

/* XXX: If there is a better way to do this, I'd love to hear about it */
static int
reiserfs_chown_xattrs_filler(void *buf, const char *name, int namelen,
			     loff_t offset, ino_t ino, unsigned int d_type)
{
	struct reiserfs_chown_buf *chown_buf = (struct reiserfs_chown_buf *)buf;
	struct dentry *xafile, *xadir = chown_buf->xadir;
	struct iattr *attrs = chown_buf->attrs;
	int err = 0;

	xafile = lookup_one_len(name, xadir, namelen);
	if (IS_ERR(xafile))
		return PTR_ERR(xafile);
	else if (!xafile->d_inode) {
		dput(xafile);
		return -ENODATA;
	}

	if (!S_ISDIR(xafile->d_inode->i_mode))
		err = notify_change(xafile, attrs);
	dput(xafile);

	return err;
}

int reiserfs_chown_xattrs(struct inode *inode, struct iattr *attrs)
{
	struct file *fp;
	struct dentry *dir;
	int err = 0;
	struct reiserfs_chown_buf buf;
	unsigned int ia_valid = attrs->ia_valid;

	/* Skip out, an xattr has no xattrs associated with it */
	if (is_reiserfs_priv_object(inode) ||
	    get_inode_sd_version(inode) == STAT_DATA_V1 ||
	    !reiserfs_xattrs(inode->i_sb)) {
		return 0;
	}
	reiserfs_read_lock_xattrs(inode->i_sb);
	dir = open_xa_dir(inode, FL_READONLY);
	reiserfs_read_unlock_xattrs(inode->i_sb);
	if (IS_ERR(dir)) {
		if (PTR_ERR(dir) != -ENODATA)
			err = PTR_ERR(dir);
		goto out;
	} else if (!dir->d_inode) {
		dput(dir);
		goto out;
	}

	fp = dentry_open(dir, NULL, O_RDWR);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		/* dentry_open dputs the dentry if it fails */
		goto out;
	}

	lock_kernel();

	attrs->ia_valid &= (ATTR_UID | ATTR_GID | ATTR_CTIME);
	buf.xadir = dir;
	buf.attrs = attrs;
	buf.inode = inode;

	err = xattr_readdir(fp, reiserfs_chown_xattrs_filler, &buf);
	if (err) {
		unlock_kernel();
		goto out_dir;
	}

	err = notify_change(dir, attrs);
	unlock_kernel();

      out_dir:
	fput(fp);

      out:
	attrs->ia_valid = ia_valid;
	return err;
}

/* Actual operations that are exported to VFS-land */

/*
 * Inode operation getxattr()
 * Preliminary locking: we down dentry->d_inode->i_sem
 */
ssize_t
reiserfs_getxattr(struct dentry * dentry, const char *name, void *buffer,
		  size_t size)
{
	struct reiserfs_xattr_handler *xah = find_xattr_handler_prefix(name);
	int err;

	if (!xah || !reiserfs_xattrs(dentry->d_sb) ||
	    get_inode_sd_version(dentry->d_inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	reiserfs_read_lock_xattr_i(dentry->d_inode);
	reiserfs_read_lock_xattrs(dentry->d_sb);
	err = xah->get(dentry->d_inode, name, buffer, size);
	reiserfs_read_unlock_xattrs(dentry->d_sb);
	reiserfs_read_unlock_xattr_i(dentry->d_inode);
	return err;
}

/*
 * Inode operation setxattr()
 *
 * dentry->d_inode->i_sem down
 */
int
reiserfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		  size_t size, int flags)
{
	struct reiserfs_xattr_handler *xah = find_xattr_handler_prefix(name);
	int err;
	int lock;

	if (!xah || !reiserfs_xattrs(dentry->d_sb) ||
	    get_inode_sd_version(dentry->d_inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	if (IS_RDONLY(dentry->d_inode))
		return -EROFS;

	if (IS_IMMUTABLE(dentry->d_inode) || IS_APPEND(dentry->d_inode))
		return -EROFS;

	reiserfs_write_lock_xattr_i(dentry->d_inode);
	lock = !has_xattr_dir(dentry->d_inode);
	if (lock)
		reiserfs_write_lock_xattrs(dentry->d_sb);
	else
		reiserfs_read_lock_xattrs(dentry->d_sb);
	err = xah->set(dentry->d_inode, name, value, size, flags);
	if (lock)
		reiserfs_write_unlock_xattrs(dentry->d_sb);
	else
		reiserfs_read_unlock_xattrs(dentry->d_sb);
	reiserfs_write_unlock_xattr_i(dentry->d_inode);
	return err;
}

/*
 * Inode operation removexattr()
 *
 * dentry->d_inode->i_sem down
 */
int reiserfs_removexattr(struct dentry *dentry, const char *name)
{
	int err;
	struct reiserfs_xattr_handler *xah = find_xattr_handler_prefix(name);

	if (!xah || !reiserfs_xattrs(dentry->d_sb) ||
	    get_inode_sd_version(dentry->d_inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	if (IS_RDONLY(dentry->d_inode))
		return -EROFS;

	if (IS_IMMUTABLE(dentry->d_inode) || IS_APPEND(dentry->d_inode))
		return -EPERM;

	reiserfs_write_lock_xattr_i(dentry->d_inode);
	reiserfs_read_lock_xattrs(dentry->d_sb);

	/* Deletion pre-operation */
	if (xah->del) {
		err = xah->del(dentry->d_inode, name);
		if (err)
			goto out;
	}

	err = reiserfs_xattr_del(dentry->d_inode, name);

	dentry->d_inode->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dentry->d_inode);

      out:
	reiserfs_read_unlock_xattrs(dentry->d_sb);
	reiserfs_write_unlock_xattr_i(dentry->d_inode);
	return err;
}

/* This is what filldir will use:
 * r_pos will always contain the amount of space required for the entire
 * list. If r_pos becomes larger than r_size, we need more space and we
 * return an error indicating this. If r_pos is less than r_size, then we've
 * filled the buffer successfully and we return success */
struct reiserfs_listxattr_buf {
	int r_pos;
	int r_size;
	char *r_buf;
	struct inode *r_inode;
};

static int
reiserfs_listxattr_filler(void *buf, const char *name, int namelen,
			  loff_t offset, ino_t ino, unsigned int d_type)
{
	struct reiserfs_listxattr_buf *b = (struct reiserfs_listxattr_buf *)buf;
	int len = 0;
	if (name[0] != '.'
	    || (namelen != 1 && (name[1] != '.' || namelen != 2))) {
		struct reiserfs_xattr_handler *xah =
		    find_xattr_handler_prefix(name);
		if (!xah)
			return 0;	/* Unsupported xattr name, skip it */

		/* We call ->list() twice because the operation isn't required to just
		 * return the name back - we want to make sure we have enough space */
		len += xah->list(b->r_inode, name, namelen, NULL);

		if (len) {
			if (b->r_pos + len + 1 <= b->r_size) {
				char *p = b->r_buf + b->r_pos;
				p += xah->list(b->r_inode, name, namelen, p);
				*p++ = '\0';
			}
			b->r_pos += len + 1;
		}
	}

	return 0;
}

/*
 * Inode operation listxattr()
 *
 * Preliminary locking: we down dentry->d_inode->i_sem
 */
ssize_t reiserfs_listxattr(struct dentry * dentry, char *buffer, size_t size)
{
	struct file *fp;
	struct dentry *dir;
	int err = 0;
	struct reiserfs_listxattr_buf buf;

	if (!dentry->d_inode)
		return -EINVAL;

	if (!reiserfs_xattrs(dentry->d_sb) ||
	    get_inode_sd_version(dentry->d_inode) == STAT_DATA_V1)
		return -EOPNOTSUPP;

	reiserfs_read_lock_xattr_i(dentry->d_inode);
	reiserfs_read_lock_xattrs(dentry->d_sb);
	dir = open_xa_dir(dentry->d_inode, FL_READONLY);
	reiserfs_read_unlock_xattrs(dentry->d_sb);
	if (IS_ERR(dir)) {
		err = PTR_ERR(dir);
		if (err == -ENODATA)
			err = 0;	/* Not an error if there aren't any xattrs */
		goto out;
	}

	fp = dentry_open(dir, NULL, O_RDWR);
	if (IS_ERR(fp)) {
		err = PTR_ERR(fp);
		/* dentry_open dputs the dentry if it fails */
		goto out;
	}

	buf.r_buf = buffer;
	buf.r_size = buffer ? size : 0;
	buf.r_pos = 0;
	buf.r_inode = dentry->d_inode;

	REISERFS_I(dentry->d_inode)->i_flags |= i_has_xattr_dir;

	err = xattr_readdir(fp, reiserfs_listxattr_filler, &buf);
	if (err)
		goto out_dir;

	if (buf.r_pos > buf.r_size && buffer != NULL)
		err = -ERANGE;
	else
		err = buf.r_pos;

      out_dir:
	fput(fp);

      out:
	reiserfs_read_unlock_xattr_i(dentry->d_inode);
	return err;
}

/* This is the implementation for the xattr plugin infrastructure */
static struct list_head xattr_handlers = LIST_HEAD_INIT(xattr_handlers);
static DEFINE_RWLOCK(handler_lock);

static struct reiserfs_xattr_handler *find_xattr_handler_prefix(const char
								*prefix)
{
	struct reiserfs_xattr_handler *xah = NULL;
	struct list_head *p;

	read_lock(&handler_lock);
	list_for_each(p, &xattr_handlers) {
		xah = list_entry(p, struct reiserfs_xattr_handler, handlers);
		if (strncmp(xah->prefix, prefix, strlen(xah->prefix)) == 0)
			break;
		xah = NULL;
	}

	read_unlock(&handler_lock);
	return xah;
}

static void __unregister_handlers(void)
{
	struct reiserfs_xattr_handler *xah;
	struct list_head *p, *tmp;

	list_for_each_safe(p, tmp, &xattr_handlers) {
		xah = list_entry(p, struct reiserfs_xattr_handler, handlers);
		if (xah->exit)
			xah->exit();

		list_del_init(p);
	}
	INIT_LIST_HEAD(&xattr_handlers);
}

int __init reiserfs_xattr_register_handlers(void)
{
	int err = 0;
	struct reiserfs_xattr_handler *xah;
	struct list_head *p;

	write_lock(&handler_lock);

	/* If we're already initialized, nothing to do */
	if (!list_empty(&xattr_handlers)) {
		write_unlock(&handler_lock);
		return 0;
	}

	/* Add the handlers */
	list_add_tail(&user_handler.handlers, &xattr_handlers);
	list_add_tail(&trusted_handler.handlers, &xattr_handlers);
#ifdef CONFIG_REISERFS_FS_SECURITY
	list_add_tail(&security_handler.handlers, &xattr_handlers);
#endif
#ifdef CONFIG_REISERFS_FS_POSIX_ACL
	list_add_tail(&posix_acl_access_handler.handlers, &xattr_handlers);
	list_add_tail(&posix_acl_default_handler.handlers, &xattr_handlers);
#endif

	/* Run initializers, if available */
	list_for_each(p, &xattr_handlers) {
		xah = list_entry(p, struct reiserfs_xattr_handler, handlers);
		if (xah->init) {
			err = xah->init();
			if (err) {
				list_del_init(p);
				break;
			}
		}
	}

	/* Clean up other handlers, if any failed */
	if (err)
		__unregister_handlers();

	write_unlock(&handler_lock);
	return err;
}

void reiserfs_xattr_unregister_handlers(void)
{
	write_lock(&handler_lock);
	__unregister_handlers();
	write_unlock(&handler_lock);
}

/* This will catch lookups from the fs root to .reiserfs_priv */
static int
xattr_lookup_poison(struct dentry *dentry, struct qstr *q1, struct qstr *name)
{
	struct dentry *priv_root = REISERFS_SB(dentry->d_sb)->priv_root;
	if (name->len == priv_root->d_name.len &&
	    name->hash == priv_root->d_name.hash &&
	    !memcmp(name->name, priv_root->d_name.name, name->len)) {
		return -ENOENT;
	} else if (q1->len == name->len &&
		   !memcmp(q1->name, name->name, name->len))
		return 0;
	return 1;
}

static struct dentry_operations xattr_lookup_poison_ops = {
	.d_compare = xattr_lookup_poison,
};

/* We need to take a copy of the mount flags since things like
 * MS_RDONLY don't get set until *after* we're called.
 * mount_flags != mount_options */
int reiserfs_xattr_init(struct super_block *s, int mount_flags)
{
	int err = 0;

	/* We need generation numbers to ensure that the oid mapping is correct
	 * v3.5 filesystems don't have them. */
	if (!old_format_only(s)) {
		set_bit(REISERFS_XATTRS, &(REISERFS_SB(s)->s_mount_opt));
	} else if (reiserfs_xattrs_optional(s)) {
		/* Old format filesystem, but optional xattrs have been enabled
		 * at mount time. Error out. */
		reiserfs_warning(s, "xattrs/ACLs not supported on pre v3.6 "
				 "format filesystem. Failing mount.");
		err = -EOPNOTSUPP;
		goto error;
	} else {
		/* Old format filesystem, but no optional xattrs have been enabled. This
		 * means we silently disable xattrs on the filesystem. */
		clear_bit(REISERFS_XATTRS, &(REISERFS_SB(s)->s_mount_opt));
	}

	/* If we don't have the privroot located yet - go find it */
	if (reiserfs_xattrs(s) && !REISERFS_SB(s)->priv_root) {
		struct dentry *dentry;
		dentry = lookup_one_len(PRIVROOT_NAME, s->s_root,
					strlen(PRIVROOT_NAME));
		if (!IS_ERR(dentry)) {
			if (!(mount_flags & MS_RDONLY) && !dentry->d_inode) {
				struct inode *inode = dentry->d_parent->d_inode;
				down(&inode->i_sem);
				err = inode->i_op->mkdir(inode, dentry, 0700);
				up(&inode->i_sem);
				if (err) {
					dput(dentry);
					dentry = NULL;
				}

				if (dentry && dentry->d_inode)
					reiserfs_warning(s,
							 "Created %s on %s - reserved for "
							 "xattr storage.",
							 PRIVROOT_NAME,
							 reiserfs_bdevname
							 (inode->i_sb));
			} else if (!dentry->d_inode) {
				dput(dentry);
				dentry = NULL;
			}
		} else
			err = PTR_ERR(dentry);

		if (!err && dentry) {
			s->s_root->d_op = &xattr_lookup_poison_ops;
			reiserfs_mark_inode_private(dentry->d_inode);
			REISERFS_SB(s)->priv_root = dentry;
		} else if (!(mount_flags & MS_RDONLY)) {	/* xattrs are unavailable */
			/* If we're read-only it just means that the dir hasn't been
			 * created. Not an error -- just no xattrs on the fs. We'll
			 * check again if we go read-write */
			reiserfs_warning(s, "xattrs/ACLs enabled and couldn't "
					 "find/create .reiserfs_priv. Failing mount.");
			err = -EOPNOTSUPP;
		}
	}

      error:
	/* This is only nonzero if there was an error initializing the xattr
	 * directory or if there is a condition where we don't support them. */
	if (err) {
		clear_bit(REISERFS_XATTRS, &(REISERFS_SB(s)->s_mount_opt));
		clear_bit(REISERFS_XATTRS_USER, &(REISERFS_SB(s)->s_mount_opt));
		clear_bit(REISERFS_POSIXACL, &(REISERFS_SB(s)->s_mount_opt));
	}

	/* The super_block MS_POSIXACL must mirror the (no)acl mount option. */
	s->s_flags = s->s_flags & ~MS_POSIXACL;
	if (reiserfs_posixacl(s))
		s->s_flags |= MS_POSIXACL;

	return err;
}

static int
__reiserfs_permission(struct inode *inode, int mask, struct nameidata *nd,
		      int need_lock)
{
	umode_t mode = inode->i_mode;

	if (mask & MAY_WRITE) {
		/*
		 * Nobody gets write access to a read-only fs.
		 */
		if (IS_RDONLY(inode) &&
		    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			return -EROFS;

		/*
		 * Nobody gets write access to an immutable file.
		 */
		if (IS_IMMUTABLE(inode))
			return -EACCES;
	}

	/* We don't do permission checks on the internal objects.
	 * Permissions are determined by the "owning" object. */
	if (is_reiserfs_priv_object(inode))
		return 0;

	if (current->fsuid == inode->i_uid) {
		mode >>= 6;
#ifdef CONFIG_REISERFS_FS_POSIX_ACL
	} else if (reiserfs_posixacl(inode->i_sb) &&
		   get_inode_sd_version(inode) != STAT_DATA_V1) {
		struct posix_acl *acl;

		/* ACL can't contain additional permissions if
		   the ACL_MASK entry is 0 */
		if (!(mode & S_IRWXG))
			goto check_groups;

		if (need_lock) {
			reiserfs_read_lock_xattr_i(inode);
			reiserfs_read_lock_xattrs(inode->i_sb);
		}
		acl = reiserfs_get_acl(inode, ACL_TYPE_ACCESS);
		if (need_lock) {
			reiserfs_read_unlock_xattrs(inode->i_sb);
			reiserfs_read_unlock_xattr_i(inode);
		}
		if (IS_ERR(acl)) {
			if (PTR_ERR(acl) == -ENODATA)
				goto check_groups;
			return PTR_ERR(acl);
		}

		if (acl) {
			int err = posix_acl_permission(inode, acl, mask);
			posix_acl_release(acl);
			if (err == -EACCES) {
				goto check_capabilities;
			}
			return err;
		} else {
			goto check_groups;
		}
#endif
	} else {
	      check_groups:
		if (in_group_p(inode->i_gid))
			mode >>= 3;
	}

	/*
	 * If the DACs are ok we don't need any capability check.
	 */
	if (((mode & mask & (MAY_READ | MAY_WRITE | MAY_EXEC)) == mask))
		return 0;

      check_capabilities:
	/*
	 * Read/write DACs are always overridable.
	 * Executable DACs are overridable if at least one exec bit is set.
	 */
	if (!(mask & MAY_EXEC) ||
	    (inode->i_mode & S_IXUGO) || S_ISDIR(inode->i_mode))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;

	/*
	 * Searching includes executable on directories, else just read.
	 */
	if (mask == MAY_READ || (S_ISDIR(inode->i_mode) && !(mask & MAY_WRITE)))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;

	return -EACCES;
}

int reiserfs_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	return __reiserfs_permission(inode, mask, nd, 1);
}

int
reiserfs_permission_locked(struct inode *inode, int mask, struct nameidata *nd)
{
	return __reiserfs_permission(inode, mask, nd, 0);
}
