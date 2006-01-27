
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/namei.h>

struct export_operations export_op_default;

#define	CALL(ops,fun) ((ops->fun)?(ops->fun):export_op_default.fun)

#define dprintk(fmt, args...) do{}while(0)

static struct dentry *
find_acceptable_alias(struct dentry *result,
		int (*acceptable)(void *context, struct dentry *dentry),
		void *context)
{
	struct dentry *dentry, *toput = NULL;

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
	struct dentry *result = NULL;
	struct dentry *target_dir;
	int err;
	struct export_operations *nops = sb->s_export_op;
	struct dentry *alias;
	int noprogress;
	char nbuf[NAME_MAX+1];

	/*
	 * Attempt to find the inode.
	 */
	result = CALL(sb->s_export_op,get_dentry)(sb,obj);
	err = -ESTALE;
	if (result == NULL)
		goto err_out;
	if (IS_ERR(result)) {
		err = PTR_ERR(result);
		goto err_out;
	}
	if (S_ISDIR(result->d_inode->i_mode) &&
	    (result->d_flags & DCACHE_DISCONNECTED)) {
		/* it is an unconnected directory, we must connect it */
		;
	} else {
		if (acceptable(context, result))
			return result;
		if (S_ISDIR(result->d_inode->i_mode)) {
			/* there is no other dentry, so fail */
			goto err_result;
		}

		alias = find_acceptable_alias(result, acceptable, context);
		if (alias)
			return alias;
	}			

	/* It's a directory, or we are required to confirm the file's
	 * location in the tree based on the parent information
 	 */
	dprintk("find_exported_dentry: need to look harder for %s/%d\n",sb->s_id,*(int*)obj);
	if (S_ISDIR(result->d_inode->i_mode))
		target_dir = dget(result);
	else {
		if (parent == NULL)
			goto err_result;

		target_dir = CALL(sb->s_export_op,get_dentry)(sb,parent);
		if (IS_ERR(target_dir))
			err = PTR_ERR(target_dir);
		if (target_dir == NULL || IS_ERR(target_dir))
			goto err_result;
	}
	/*
	 * Now we need to make sure that target_dir is properly connected.
	 * It may already be, as the flag isn't always updated when connection
	 * happens.
	 * So, we walk up parent links until we find a connected directory,
	 * or we run out of directories.  Then we find the parent, find
	 * the name of the child in that parent, and do a lookup.
	 * This should connect the child into the parent
	 * We then repeat.
	 */

	/* it is possible that a confused file system might not let us complete 
	 * the path to the root.  For example, if get_parent returns a directory
	 * in which we cannot find a name for the child.  While this implies a
	 * very sick filesystem we don't want it to cause knfsd to spin.  Hence
	 * the noprogress counter.  If we go through the loop 10 times (2 is
	 * probably enough) without getting anywhere, we just give up
	 */
	noprogress= 0;
	while (target_dir->d_flags & DCACHE_DISCONNECTED && noprogress++ < 10) {
		struct dentry *pd = target_dir;

		dget(pd);
		spin_lock(&pd->d_lock);
		while (!IS_ROOT(pd) &&
				(pd->d_parent->d_flags&DCACHE_DISCONNECTED)) {
			struct dentry *parent = pd->d_parent;

			dget(parent);
			spin_unlock(&pd->d_lock);
			dput(pd);
			pd = parent;
			spin_lock(&pd->d_lock);
		}
		spin_unlock(&pd->d_lock);

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
			/* we have hit the top of a disconnected path.  Try
			 * to find parent and connect
			 * note: racing with some other process renaming a
			 * directory isn't much of a problem here.  If someone
			 * renames the directory, it will end up properly
			 * connected, which is what we want
			 */
			struct dentry *ppd;
			struct dentry *npd;

			mutex_lock(&pd->d_inode->i_mutex);
			ppd = CALL(nops,get_parent)(pd);
			mutex_unlock(&pd->d_inode->i_mutex);

			if (IS_ERR(ppd)) {
				err = PTR_ERR(ppd);
				dprintk("find_exported_dentry: get_parent of %ld failed, err %d\n",
					pd->d_inode->i_ino, err);
				dput(pd);
				break;
			}
			dprintk("find_exported_dentry: find name of %lu in %lu\n", pd->d_inode->i_ino, ppd->d_inode->i_ino);
			err = CALL(nops,get_name)(ppd, nbuf, pd);
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
			dprintk("find_exported_dentry: found name: %s\n", nbuf);
			mutex_lock(&ppd->d_inode->i_mutex);
			npd = lookup_one_len(nbuf, ppd, strlen(nbuf));
			mutex_unlock(&ppd->d_inode->i_mutex);
			if (IS_ERR(npd)) {
				err = PTR_ERR(npd);
				dprintk("find_exported_dentry: lookup failed: %d\n", err);
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
				printk("find_exported_dentry: npd != pd\n");
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
		goto err_target;
	}
	/* if we weren't after a directory, have one more step to go */
	if (result != target_dir) {
		struct dentry *nresult;
		err = CALL(nops,get_name)(target_dir, nbuf, result);
		if (!err) {
			mutex_lock(&target_dir->d_inode->i_mutex);
			nresult = lookup_one_len(nbuf, target_dir, strlen(nbuf));
			mutex_unlock(&target_dir->d_inode->i_mutex);
			if (!IS_ERR(nresult)) {
				if (nresult->d_inode) {
					dput(result);
					result = nresult;
				} else
					dput(nresult);
			}
		}
	}
	dput(target_dir);
	/* now result is properly connected, it is our best bet */
	if (acceptable(context, result))
		return result;

	alias = find_acceptable_alias(result, acceptable, context);
	if (alias)
		return alias;

	/* drat - I just cannot find anything acceptable */
	dput(result);
	/* It might be justifiable to return ESTALE here,
	 * but the filehandle at-least looks reasonable good
	 * and it just be a permission problem, so returning
	 * -EACCESS is safer
	 */
	return ERR_PTR(-EACCES);

 err_target:
	dput(target_dir);
 err_result:
	dput(result);
 err_out:
	return ERR_PTR(err);
}



static struct dentry *get_parent(struct dentry *child)
{
	/* get_parent cannot be supported generically, the locking
	 * is too icky.
	 * instead, we just return EACCES.  If server reboots or inodes
	 * get flushed, you lose
	 */
	return ERR_PTR(-EACCES);
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
			loff_t pos, ino_t ino, unsigned int d_type)
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


static struct dentry *export_iget(struct super_block *sb, unsigned long ino, __u32 generation)
{

	/* iget isn't really right if the inode is currently unallocated!!
	 * This should really all be done inside each filesystem
	 *
	 * ext2fs' read_inode has been strengthed to return a bad_inode if
	 * the inode had been deleted.
	 *
	 * Currently we don't know the generation for parent directory, so
	 * a generation of 0 means "accept any"
	 */
	struct inode *inode;
	struct dentry *result;
	if (ino == 0)
		return ERR_PTR(-ESTALE);
	inode = iget(sb, ino);
	if (inode == NULL)
		return ERR_PTR(-ENOMEM);
	if (is_bad_inode(inode)
	    || (generation && inode->i_generation != generation)
		) {
		/* we didn't find the right inode.. */
		dprintk("fh_verify: Inode %lu, Bad count: %d %d or version  %u %u\n",
			inode->i_ino,
			inode->i_nlink, atomic_read(&inode->i_count),
			inode->i_generation,
			generation);

		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	/* now to find a dentry.
	 * If possible, get a well-connected one
	 */
	result = d_alloc_anon(inode);
	if (!result) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	return result;
}


static struct dentry *get_object(struct super_block *sb, void *vobjp)
{
	__u32 *objp = vobjp;
	unsigned long ino = objp[0];
	__u32 generation = objp[1];

	return export_iget(sb, ino, generation);
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

struct export_operations export_op_default = {
	.decode_fh	= export_decode_fh,
	.encode_fh	= export_encode_fh,

	.get_name	= get_name,
	.get_parent	= get_parent,
	.get_dentry	= get_object,
};

EXPORT_SYMBOL(export_op_default);
EXPORT_SYMBOL(find_exported_dentry);

MODULE_LICENSE("GPL");
