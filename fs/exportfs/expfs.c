/*
 * Copyright (C) Neil Brown 2002
 * Copyright (C) Christoph Hellwig 2007
 *
 * This file contains the code mapping from inodes to NFS file handles,
 * and for mapping back from file handles to dentries.
 *
 * For details on why we do all the strange and hairy things in here
 * take a look at Documentation/filesystems/nfs/Exporting.
 */
#include <linux/exportfs.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/sched.h>

#define dprintk(fmt, args...) do{}while(0)


static int get_name(const struct path *path, char *name, struct dentry *child);


static int exportfs_get_name(struct vfsmount *mnt, struct dentry *dir,
		char *name, struct dentry *child)
{
	const struct export_operations *nop = dir->d_sb->s_export_op;
	struct path path = {.mnt = mnt, .dentry = dir};

	if (nop->get_name)
		return nop->get_name(dir, name, child);
	else
		return get_name(&path, name, child);
}

/*
 * Check if the dentry or any of it's aliases is acceptable.
 */
static struct dentry *
find_acceptable_alias(struct dentry *result,
		int (*acceptable)(void *context, struct dentry *dentry),
		void *context)
{
	struct dentry *dentry, *toput = NULL;
	struct inode *inode;
	struct hlist_node *p;

	if (acceptable(context, result))
		return result;

	inode = result->d_inode;
	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, p, &inode->i_dentry, d_alias) {
		dget(dentry);
		spin_unlock(&inode->i_lock);
		if (toput)
			dput(toput);
		if (dentry != result && acceptable(context, dentry)) {
			dput(result);
			return dentry;
		}
		spin_lock(&inode->i_lock);
		toput = dentry;
	}
	spin_unlock(&inode->i_lock);

	if (toput)
		dput(toput);
	return NULL;
}

/*
 * Find root of a disconnected subtree and return a reference to it.
 */
static struct dentry *
find_disconnected_root(struct dentry *dentry)
{
	dget(dentry);
	while (!IS_ROOT(dentry)) {
		struct dentry *parent = dget_parent(dentry);

		if (!(parent->d_flags & DCACHE_DISCONNECTED)) {
			dput(parent);
			break;
		}

		dput(dentry);
		dentry = parent;
	}
	return dentry;
}

/*
 * Make sure target_dir is fully connected to the dentry tree.
 *
 * It may already be, as the flag isn't always updated when connection happens.
 */
static int
reconnect_path(struct vfsmount *mnt, struct dentry *target_dir, char *nbuf)
{
	int noprogress = 0;
	int err = -ESTALE;

	/*
	 * It is possible that a confused file system might not let us complete
	 * the path to the root.  For example, if get_parent returns a directory
	 * in which we cannot find a name for the child.  While this implies a
	 * very sick filesystem we don't want it to cause knfsd to spin.  Hence
	 * the noprogress counter.  If we go through the loop 10 times (2 is
	 * probably enough) without getting anywhere, we just give up
	 */
	while (target_dir->d_flags & DCACHE_DISCONNECTED && noprogress++ < 10) {
		struct dentry *pd = find_disconnected_root(target_dir);

		if (!IS_ROOT(pd)) {
			/* must have found a connected parent - great */
			spin_lock(&pd->d_lock);
			pd->d_flags &= ~DCACHE_DISCONNECTED;
			spin_unlock(&pd->d_lock);
			noprogress = 0;
		} else if (pd == mnt->mnt_sb->s_root) {
			printk(KERN_ERR "export: Eeek filesystem root is not connected, impossible\n");
			spin_lock(&pd->d_lock);
			pd->d_flags &= ~DCACHE_DISCONNECTED;
			spin_unlock(&pd->d_lock);
			noprogress = 0;
		} else {
			/*
			 * We have hit the top of a disconnected path, try to
			 * find parent and connect.
			 *
			 * Racing with some other process renaming a directory
			 * isn't much of a problem here.  If someone renames
			 * the directory, it will end up properly connected,
			 * which is what we want
			 *
			 * Getting the parent can't be supported generically,
			 * the locking is too icky.
			 *
			 * Instead we just return EACCES.  If server reboots
			 * or inodes get flushed, you lose
			 */
			struct dentry *ppd = ERR_PTR(-EACCES);
			struct dentry *npd;

			mutex_lock(&pd->d_inode->i_mutex);
			if (mnt->mnt_sb->s_export_op->get_parent)
				ppd = mnt->mnt_sb->s_export_op->get_parent(pd);
			mutex_unlock(&pd->d_inode->i_mutex);

			if (IS_ERR(ppd)) {
				err = PTR_ERR(ppd);
				dprintk("%s: get_parent of %ld failed, err %d\n",
					__func__, pd->d_inode->i_ino, err);
				dput(pd);
				break;
			}

			dprintk("%s: find name of %lu in %lu\n", __func__,
				pd->d_inode->i_ino, ppd->d_inode->i_ino);
			err = exportfs_get_name(mnt, ppd, nbuf, pd);
			if (err) {
				dput(ppd);
				dput(pd);
				if (err == -ENOENT)
					/* some race between get_parent and
					 * get_name?  just try again
					 */
					continue;
				break;
			}
			dprintk("%s: found name: %s\n", __func__, nbuf);
			mutex_lock(&ppd->d_inode->i_mutex);
			npd = lookup_one_len(nbuf, ppd, strlen(nbuf));
			mutex_unlock(&ppd->d_inode->i_mutex);
			if (IS_ERR(npd)) {
				err = PTR_ERR(npd);
				dprintk("%s: lookup failed: %d\n",
					__func__, err);
				dput(ppd);
				dput(pd);
				break;
			}
			/* we didn't really want npd, we really wanted
			 * a side-effect of the lookup.
			 * hopefully, npd == pd, though it isn't really
			 * a problem if it isn't
			 */
			if (npd == pd)
				noprogress = 0;
			else
				printk("%s: npd != pd\n", __func__);
			dput(npd);
			dput(ppd);
			if (IS_ROOT(pd)) {
				/* something went wrong, we have to give up */
				dput(pd);
				break;
			}
		}
		dput(pd);
	}

	if (target_dir->d_flags & DCACHE_DISCONNECTED) {
		/* something went wrong - oh-well */
		if (!err)
			err = -ESTALE;
		return err;
	}

	return 0;
}

struct getdents_callback {
	char *name;		/* name that was found. It already points to a
				   buffer NAME_MAX+1 is size */
	unsigned long ino;	/* the inum we are looking for */
	int found;		/* inode matched? */
	int sequence;		/* sequence counter */
};

/*
 * A rather strange filldir function to capture
 * the name matching the specified inode number.
 */
static int filldir_one(void * __buf, const char * name, int len,
			loff_t pos, u64 ino, unsigned int d_type)
{
	struct getdents_callback *buf = __buf;
	int result = 0;

	buf->sequence++;
	if (buf->ino == ino) {
		memcpy(buf->name, name, len);
		buf->name[len] = '\0';
		buf->found = 1;
		result = -1;
	}
	return result;
}

/**
 * get_name - default export_operations->get_name function
 * @dentry: the directory in which to find a name
 * @name:   a pointer to a %NAME_MAX+1 char buffer to store the name
 * @child:  the dentry for the child directory.
 *
 * calls readdir on the parent until it finds an entry with
 * the same inode number as the child, and returns that.
 */
static int get_name(const struct path *path, char *name, struct dentry *child)
{
	const struct cred *cred = current_cred();
	struct inode *dir = path->dentry->d_inode;
	int error;
	struct file *file;
	struct getdents_callback buffer;

	error = -ENOTDIR;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;
	error = -EINVAL;
	if (!dir->i_fop)
		goto out;
	/*
	 * Open the directory ...
	 */
	file = dentry_open(path, O_RDONLY, cred);
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	error = -EINVAL;
	if (!file->f_op->readdir)
		goto out_close;

	buffer.name = name;
	buffer.ino = child->d_inode->i_ino;
	buffer.found = 0;
	buffer.sequence = 0;
	while (1) {
		int old_seq = buffer.sequence;

		error = vfs_readdir(file, filldir_one, &buffer);
		if (buffer.found) {
			error = 0;
			break;
		}

		if (error < 0)
			break;

		error = -ENOENT;
		if (old_seq == buffer.sequence)
			break;
	}

out_close:
	fput(file);
out:
	return error;
}

/**
 * export_encode_fh - default export_operations->encode_fh function
 * @inode:   the object to encode
 * @fh:      where to store the file handle fragment
 * @max_len: maximum length to store there
 * @parent:  parent directory inode, if wanted
 *
 * This default encode_fh function assumes that the 32 inode number
 * is suitable for locating an inode, and that the generation number
 * can be used to check that it is still valid.  It places them in the
 * filehandle fragment where export_decode_fh expects to find them.
 */
static int export_encode_fh(struct inode *inode, struct fid *fid,
		int *max_len, struct inode *parent)
{
	int len = *max_len;
	int type = FILEID_INO32_GEN;

	if (parent && (len < 4)) {
		*max_len = 4;
		return FILEID_INVALID;
	} else if (len < 2) {
		*max_len = 2;
		return FILEID_INVALID;
	}

	len = 2;
	fid->i32.ino = inode->i_ino;
	fid->i32.gen = inode->i_generation;
	if (parent) {
		fid->i32.parent_ino = parent->i_ino;
		fid->i32.parent_gen = parent->i_generation;
		len = 4;
		type = FILEID_INO32_GEN_PARENT;
	}
	*max_len = len;
	return type;
}

int exportfs_encode_fh(struct dentry *dentry, struct fid *fid, int *max_len,
		int connectable)
{
	const struct export_operations *nop = dentry->d_sb->s_export_op;
	int error;
	struct dentry *p = NULL;
	struct inode *inode = dentry->d_inode, *parent = NULL;

	if (connectable && !S_ISDIR(inode->i_mode)) {
		p = dget_parent(dentry);
		/*
		 * note that while p might've ceased to be our parent already,
		 * it's still pinned by and still positive.
		 */
		parent = p->d_inode;
	}
	if (nop->encode_fh)
		error = nop->encode_fh(inode, fid->raw, max_len, parent);
	else
		error = export_encode_fh(inode, fid, max_len, parent);
	dput(p);

	return error;
}
EXPORT_SYMBOL_GPL(exportfs_encode_fh);

struct dentry *exportfs_decode_fh(struct vfsmount *mnt, struct fid *fid,
		int fh_len, int fileid_type,
		int (*acceptable)(void *, struct dentry *), void *context)
{
	const struct export_operations *nop = mnt->mnt_sb->s_export_op;
	struct dentry *result, *alias;
	char nbuf[NAME_MAX+1];
	int err;

	/*
	 * Try to get any dentry for the given file handle from the filesystem.
	 */
	if (!nop || !nop->fh_to_dentry)
		return ERR_PTR(-ESTALE);
	result = nop->fh_to_dentry(mnt->mnt_sb, fid, fh_len, fileid_type);
	if (!result)
		result = ERR_PTR(-ESTALE);
	if (IS_ERR(result))
		return result;

	if (S_ISDIR(result->d_inode->i_mode)) {
		/*
		 * This request is for a directory.
		 *
		 * On the positive side there is only one dentry for each
		 * directory inode.  On the negative side this implies that we
		 * to ensure our dentry is connected all the way up to the
		 * filesystem root.
		 */
		if (result->d_flags & DCACHE_DISCONNECTED) {
			err = reconnect_path(mnt, result, nbuf);
			if (err)
				goto err_result;
		}

		if (!acceptable(context, result)) {
			err = -EACCES;
			goto err_result;
		}

		return result;
	} else {
		/*
		 * It's not a directory.  Life is a little more complicated.
		 */
		struct dentry *target_dir, *nresult;

		/*
		 * See if either the dentry we just got from the filesystem
		 * or any alias for it is acceptable.  This is always true
		 * if this filesystem is exported without the subtreecheck
		 * option.  If the filesystem is exported with the subtree
		 * check option there's a fair chance we need to look at
		 * the parent directory in the file handle and make sure
		 * it's connected to the filesystem root.
		 */
		alias = find_acceptable_alias(result, acceptable, context);
		if (alias)
			return alias;

		/*
		 * Try to extract a dentry for the parent directory from the
		 * file handle.  If this fails we'll have to give up.
		 */
		err = -ESTALE;
		if (!nop->fh_to_parent)
			goto err_result;

		target_dir = nop->fh_to_parent(mnt->mnt_sb, fid,
				fh_len, fileid_type);
		if (!target_dir)
			goto err_result;
		err = PTR_ERR(target_dir);
		if (IS_ERR(target_dir))
			goto err_result;

		/*
		 * And as usual we need to make sure the parent directory is
		 * connected to the filesystem root.  The VFS really doesn't
		 * like disconnected directories..
		 */
		err = reconnect_path(mnt, target_dir, nbuf);
		if (err) {
			dput(target_dir);
			goto err_result;
		}

		/*
		 * Now that we've got both a well-connected parent and a
		 * dentry for the inode we're after, make sure that our
		 * inode is actually connected to the parent.
		 */
		err = exportfs_get_name(mnt, target_dir, nbuf, result);
		if (!err) {
			mutex_lock(&target_dir->d_inode->i_mutex);
			nresult = lookup_one_len(nbuf, target_dir,
						 strlen(nbuf));
			mutex_unlock(&target_dir->d_inode->i_mutex);
			if (!IS_ERR(nresult)) {
				if (nresult->d_inode) {
					dput(result);
					result = nresult;
				} else
					dput(nresult);
			}
		}

		/*
		 * At this point we are done with the parent, but it's pinned
		 * by the child dentry anyway.
		 */
		dput(target_dir);

		/*
		 * And finally make sure the dentry is actually acceptable
		 * to NFSD.
		 */
		alias = find_acceptable_alias(result, acceptable, context);
		if (!alias) {
			err = -EACCES;
			goto err_result;
		}

		return alias;
	}

 err_result:
	dput(result);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(exportfs_decode_fh);

MODULE_LICENSE("GPL");
