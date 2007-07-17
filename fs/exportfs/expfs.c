
#include <linux/exportfs.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>

#define dprintk(fmt, args...) do{}while(0)


static int get_name(struct dentry *dentry, char *name,
		struct dentry *child);


static struct dentry *exportfs_get_dentry(struct super_block *sb, void *obj)
{
	struct dentry *result = ERR_PTR(-ESTALE);

	if (sb->s_export_op->get_dentry) {
		result = sb->s_export_op->get_dentry(sb, obj);
		if (!result)
			result = ERR_PTR(-ESTALE);
	}

	return result;
}

static int exportfs_get_name(struct dentry *dir, char *name,
		struct dentry *child)
{
	struct export_operations *nop = dir->d_sb->s_export_op;

	if (nop->get_name)
		return nop->get_name(dir, name, child);
	else
		return get_name(dir, name, child);
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

	if (acceptable(context, result))
		return result;

	spin_lock(&dcache_lock);
	list_for_each_entry(dentry, &result->d_inode->i_dentry, d_alias) {
		dget_locked(dentry);
		spin_unlock(&dcache_lock);
		if (toput)
			dput(toput);
		if (dentry != result && acceptable(context, dentry)) {
			dput(result);
			return dentry;
		}
		spin_lock(&dcache_lock);
		toput = dentry;
	}
	spin_unlock(&dcache_lock);

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
	spin_lock(&dentry->d_lock);
	while (!IS_ROOT(dentry) &&
	       (dentry->d_parent->d_flags & DCACHE_DISCONNECTED)) {
		struct dentry *parent = dentry->d_parent;
		dget(parent);
		spin_unlock(&dentry->d_lock);
		dput(dentry);
		dentry = parent;
		spin_lock(&dentry->d_lock);
	}
	spin_unlock(&dentry->d_lock);
	return dentry;
}


/*
 * Make sure target_dir is fully connected to the dentry tree.
 *
 * It may already be, as the flag isn't always updated when connection happens.
 */
static int
reconnect_path(struct super_block *sb, struct dentry *target_dir)
{
	char nbuf[NAME_MAX+1];
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
		} else if (pd == sb->s_root) {
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
			if (sb->s_export_op->get_parent)
				ppd = sb->s_export_op->get_parent(pd);
			mutex_unlock(&pd->d_inode->i_mutex);

			if (IS_ERR(ppd)) {
				err = PTR_ERR(ppd);
				dprintk("%s: get_parent of %ld failed, err %d\n",
					__FUNCTION__, pd->d_inode->i_ino, err);
				dput(pd);
				break;
			}

			dprintk("%s: find name of %lu in %lu\n", __FUNCTION__,
				pd->d_inode->i_ino, ppd->d_inode->i_ino);
			err = exportfs_get_name(ppd, nbuf, pd);
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
			dprintk("%s: found name: %s\n", __FUNCTION__, nbuf);
			mutex_lock(&ppd->d_inode->i_mutex);
			npd = lookup_one_len(nbuf, ppd, strlen(nbuf));
			mutex_unlock(&ppd->d_inode->i_mutex);
			if (IS_ERR(npd)) {
				err = PTR_ERR(npd);
				dprintk("%s: lookup failed: %d\n",
					__FUNCTION__, err);
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
				printk("%s: npd != pd\n", __FUNCTION__);
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

/**
 * find_exported_dentry - helper routine to implement export_operations->decode_fh
 * @sb:		The &super_block identifying the filesystem
 * @obj:	An opaque identifier of the object to be found - passed to
 *		get_inode
 * @parent:	An optional opqaue identifier of the parent of the object.
 * @acceptable:	A function used to test possible &dentries to see if they are
 *		acceptable
 * @context:	A parameter to @acceptable so that it knows on what basis to
 *		judge.
 *
 * find_exported_dentry is the central helper routine to enable file systems
 * to provide the decode_fh() export_operation.  It's main task is to take
 * an &inode, find or create an appropriate &dentry structure, and possibly
 * splice this into the dcache in the correct place.
 *
 * The decode_fh() operation provided by the filesystem should call
 * find_exported_dentry() with the same parameters that it received except
 * that instead of the file handle fragment, pointers to opaque identifiers
 * for the object and optionally its parent are passed.  The default decode_fh
 * routine passes one pointer to the start of the filehandle fragment, and
 * one 8 bytes into the fragment.  It is expected that most filesystems will
 * take this approach, though the offset to the parent identifier may well be
 * different.
 *
 * find_exported_dentry() will call get_dentry to get an dentry pointer from
 * the file system.  If any &dentry in the d_alias list is acceptable, it will
 * be returned.  Otherwise find_exported_dentry() will attempt to splice a new
 * &dentry into the dcache using get_name() and get_parent() to find the
 * appropriate place.
 */

struct dentry *
find_exported_dentry(struct super_block *sb, void *obj, void *parent,
		     int (*acceptable)(void *context, struct dentry *de),
		     void *context)
{
	struct dentry *result, *alias;
	int err = -ESTALE;

	/*
	 * Attempt to find the inode.
	 */
	result = exportfs_get_dentry(sb, obj);
	if (IS_ERR(result))
		return result;

	if (S_ISDIR(result->d_inode->i_mode)) {
		if (!(result->d_flags & DCACHE_DISCONNECTED)) {
			if (acceptable(context, result))
				return result;
			err = -EACCES;
			goto err_result;
		}

		err = reconnect_path(sb, result);
		if (err)
			goto err_result;
	} else {
		struct dentry *target_dir, *nresult;
		char nbuf[NAME_MAX+1];

		alias = find_acceptable_alias(result, acceptable, context);
		if (alias)
			return alias;

		if (parent == NULL)
			goto err_result;

		target_dir = exportfs_get_dentry(sb,parent);
		if (IS_ERR(target_dir)) {
			err = PTR_ERR(target_dir);
			goto err_result;
		}

		err = reconnect_path(sb, target_dir);
		if (err) {
			dput(target_dir);
			goto err_result;
		}

		/*
		 * As we weren't after a directory, have one more step to go.
		 */
		err = exportfs_get_name(target_dir, nbuf, result);
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
		dput(target_dir);
	}

	alias = find_acceptable_alias(result, acceptable, context);
	if (alias)
		return alias;

	/* drat - I just cannot find anything acceptable */
	dput(result);
	/* It might be justifiable to return ESTALE here,
	 * but the filehandle at-least looks reasonable good
	 * and it may just be a permission problem, so returning
	 * -EACCESS is safer
	 */
	return ERR_PTR(-EACCES);

 err_result:
	dput(result);
	return ERR_PTR(err);
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
static int get_name(struct dentry *dentry, char *name,
			struct dentry *child)
{
	struct inode *dir = dentry->d_inode;
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
	file = dentry_open(dget(dentry), NULL, O_RDONLY);
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

		if (error < 0)
			break;

		error = 0;
		if (buffer.found)
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
 * @dentry:  the dentry to encode
 * @fh:      where to store the file handle fragment
 * @max_len: maximum length to store there
 * @connectable: whether to store parent information
 *
 * This default encode_fh function assumes that the 32 inode number
 * is suitable for locating an inode, and that the generation number
 * can be used to check that it is still valid.  It places them in the
 * filehandle fragment where export_decode_fh expects to find them.
 */
static int export_encode_fh(struct dentry *dentry, __u32 *fh, int *max_len,
		   int connectable)
{
	struct inode * inode = dentry->d_inode;
	int len = *max_len;
	int type = 1;
	
	if (len < 2 || (connectable && len < 4))
		return 255;

	len = 2;
	fh[0] = inode->i_ino;
	fh[1] = inode->i_generation;
	if (connectable && !S_ISDIR(inode->i_mode)) {
		struct inode *parent;

		spin_lock(&dentry->d_lock);
		parent = dentry->d_parent->d_inode;
		fh[2] = parent->i_ino;
		fh[3] = parent->i_generation;
		spin_unlock(&dentry->d_lock);
		len = 4;
		type = 2;
	}
	*max_len = len;
	return type;
}


/**
 * export_decode_fh - default export_operations->decode_fh function
 * @sb:  The superblock
 * @fh:  pointer to the file handle fragment
 * @fh_len: length of file handle fragment
 * @acceptable: function for testing acceptability of dentrys
 * @context:   context for @acceptable
 *
 * This is the default decode_fh() function.
 * a fileid_type of 1 indicates that the filehandlefragment
 * just contains an object identifier understood by  get_dentry.
 * a fileid_type of 2 says that there is also a directory
 * identifier 8 bytes in to the filehandlefragement.
 */
static struct dentry *export_decode_fh(struct super_block *sb, __u32 *fh, int fh_len,
			      int fileid_type,
			 int (*acceptable)(void *context, struct dentry *de),
			 void *context)
{
	__u32 parent[2];
	parent[0] = parent[1] = 0;
	if (fh_len < 2 || fileid_type > 2)
		return NULL;
	if (fileid_type == 2) {
		if (fh_len > 2) parent[0] = fh[2];
		if (fh_len > 3) parent[1] = fh[3];
	}
	return find_exported_dentry(sb, fh, parent,
				   acceptable, context);
}

int exportfs_encode_fh(struct dentry *dentry, __u32 *fh, int *max_len,
		int connectable)
{
 	struct export_operations *nop = dentry->d_sb->s_export_op;
	int error;

	if (nop->encode_fh)
		error = nop->encode_fh(dentry, fh, max_len, connectable);
	else
		error = export_encode_fh(dentry, fh, max_len, connectable);

	return error;
}
EXPORT_SYMBOL_GPL(exportfs_encode_fh);

struct dentry *exportfs_decode_fh(struct vfsmount *mnt, __u32 *fh, int fh_len,
		int fileid_type, int (*acceptable)(void *, struct dentry *),
		void *context)
{
	struct export_operations *nop = mnt->mnt_sb->s_export_op;
	struct dentry *result;

	if (nop->decode_fh) {
		result = nop->decode_fh(mnt->mnt_sb, fh, fh_len, fileid_type,
			acceptable, context);
	} else {
		result = export_decode_fh(mnt->mnt_sb, fh, fh_len, fileid_type,
			acceptable, context);
	}

	return result;
}
EXPORT_SYMBOL_GPL(exportfs_decode_fh);

EXPORT_SYMBOL(find_exported_dentry);

MODULE_LICENSE("GPL");
