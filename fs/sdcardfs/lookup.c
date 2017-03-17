/*
 * fs/sdcardfs/lookup.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"
#include "linux/delay.h"

/* The dentry cache is just so we have properly sized dentries */
static struct kmem_cache *sdcardfs_dentry_cachep;

int sdcardfs_init_dentry_cache(void)
{
	sdcardfs_dentry_cachep =
		kmem_cache_create("sdcardfs_dentry",
				  sizeof(struct sdcardfs_dentry_info),
				  0, SLAB_RECLAIM_ACCOUNT, NULL);

	return sdcardfs_dentry_cachep ? 0 : -ENOMEM;
}

void sdcardfs_destroy_dentry_cache(void)
{
	kmem_cache_destroy(sdcardfs_dentry_cachep);
}

void free_dentry_private_data(struct dentry *dentry)
{
	if (!dentry || !dentry->d_fsdata)
		return;
	kmem_cache_free(sdcardfs_dentry_cachep, dentry->d_fsdata);
	dentry->d_fsdata = NULL;
}

/* allocate new dentry private data */
int new_dentry_private_data(struct dentry *dentry)
{
	struct sdcardfs_dentry_info *info = SDCARDFS_D(dentry);

	/* use zalloc to init dentry_info.lower_path */
	info = kmem_cache_zalloc(sdcardfs_dentry_cachep, GFP_ATOMIC);
	if (!info)
		return -ENOMEM;

	spin_lock_init(&info->lock);
	dentry->d_fsdata = info;

	return 0;
}

struct inode_data {
	struct inode *lower_inode;
	userid_t id;
};

static int sdcardfs_inode_test(struct inode *inode, void *candidate_data/*void *candidate_lower_inode*/)
{
	struct inode *current_lower_inode = sdcardfs_lower_inode(inode);
	userid_t current_userid = SDCARDFS_I(inode)->userid;

	if (current_lower_inode == ((struct inode_data *)candidate_data)->lower_inode &&
			current_userid == ((struct inode_data *)candidate_data)->id)
		return 1; /* found a match */
	else
		return 0; /* no match */
}

static int sdcardfs_inode_set(struct inode *inode, void *lower_inode)
{
	/* we do actual inode initialization in sdcardfs_iget */
	return 0;
}

struct inode *sdcardfs_iget(struct super_block *sb, struct inode *lower_inode, userid_t id)
{
	struct sdcardfs_inode_info *info;
	struct inode_data data;
	struct inode *inode; /* the new inode to return */
	int err;

	data.id = id;
	data.lower_inode = lower_inode;
	inode = iget5_locked(sb, /* our superblock */
			     /*
			      * hashval: we use inode number, but we can
			      * also use "(unsigned long)lower_inode"
			      * instead.
			      */
			     lower_inode->i_ino, /* hashval */
			     sdcardfs_inode_test, /* inode comparison function */
			     sdcardfs_inode_set, /* inode init function */
			     &data); /* data passed to test+set fxns */
	if (!inode) {
		err = -EACCES;
		iput(lower_inode);
		return ERR_PTR(err);
	}
	/* if found a cached inode, then just return it */
	if (!(inode->i_state & I_NEW))
		return inode;

	/* initialize new inode */
	info = SDCARDFS_I(inode);

	inode->i_ino = lower_inode->i_ino;
	if (!igrab(lower_inode)) {
		err = -ESTALE;
		return ERR_PTR(err);
	}
	sdcardfs_set_lower_inode(inode, lower_inode);

	inode->i_version++;

	/* use different set of inode ops for symlinks & directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_op = &sdcardfs_dir_iops;
	else if (S_ISLNK(lower_inode->i_mode))
		inode->i_op = &sdcardfs_symlink_iops;
	else
		inode->i_op = &sdcardfs_main_iops;

	/* use different set of file ops for directories */
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_fop = &sdcardfs_dir_fops;
	else
		inode->i_fop = &sdcardfs_main_fops;

	inode->i_mapping->a_ops = &sdcardfs_aops;

	inode->i_atime.tv_sec = 0;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_sec = 0;
	inode->i_ctime.tv_nsec = 0;

	/* properly initialize special inodes */
	if (S_ISBLK(lower_inode->i_mode) || S_ISCHR(lower_inode->i_mode) ||
	    S_ISFIFO(lower_inode->i_mode) || S_ISSOCK(lower_inode->i_mode))
		init_special_inode(inode, lower_inode->i_mode,
				   lower_inode->i_rdev);

	/* all well, copy inode attributes */
	sdcardfs_copy_and_fix_attrs(inode, lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);

	unlock_new_inode(inode);
	return inode;
}

/*
 * Connect a sdcardfs inode dentry/inode with several lower ones.  This is
 * the classic stackable file system "vnode interposition" action.
 *
 * @dentry: sdcardfs's dentry which interposes on lower one
 * @sb: sdcardfs's super_block
 * @lower_path: the lower path (caller does path_get/put)
 */
int sdcardfs_interpose(struct dentry *dentry, struct super_block *sb,
		     struct path *lower_path, userid_t id)
{
	int err = 0;
	struct inode *inode;
	struct inode *lower_inode;
	struct super_block *lower_sb;

	lower_inode = d_inode(lower_path->dentry);
	lower_sb = sdcardfs_lower_super(sb);

	/* check that the lower file system didn't cross a mount point */
	if (lower_inode->i_sb != lower_sb) {
		err = -EXDEV;
		goto out;
	}

	/*
	 * We allocate our new inode below by calling sdcardfs_iget,
	 * which will initialize some of the new inode's fields
	 */

	/* inherit lower inode number for sdcardfs's inode */
	inode = sdcardfs_iget(sb, lower_inode, id);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	d_add(dentry, inode);
	update_derived_permission_lock(dentry);
out:
	return err;
}

struct sdcardfs_name_data {
	struct dir_context ctx;
	const struct qstr *to_find;
	char *name;
	bool found;
};

static int sdcardfs_name_match(struct dir_context *ctx, const char *name,
		int namelen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct sdcardfs_name_data *buf = container_of(ctx, struct sdcardfs_name_data, ctx);
	struct qstr candidate = QSTR_INIT(name, namelen);

	if (qstr_case_eq(buf->to_find, &candidate)) {
		memcpy(buf->name, name, namelen);
		buf->name[namelen] = 0;
		buf->found = true;
		return 1;
	}
	return 0;
}

/*
 * Main driver function for sdcardfs's lookup.
 *
 * Returns: NULL (ok), ERR_PTR if an error occurred.
 * Fills in lower_parent_path with <dentry,mnt> on success.
 */
static struct dentry *__sdcardfs_lookup(struct dentry *dentry,
		unsigned int flags, struct path *lower_parent_path, userid_t id)
{
	int err = 0;
	struct vfsmount *lower_dir_mnt;
	struct dentry *lower_dir_dentry = NULL;
	struct dentry *lower_dentry;
	const struct qstr *name;
	struct path lower_path;
	struct qstr dname;
	struct sdcardfs_sb_info *sbi;

	sbi = SDCARDFS_SB(dentry->d_sb);
	/* must initialize dentry operations */
	d_set_d_op(dentry, &sdcardfs_ci_dops);

	if (IS_ROOT(dentry))
		goto out;

	name = &dentry->d_name;

	/* now start the actual lookup procedure */
	lower_dir_dentry = lower_parent_path->dentry;
	lower_dir_mnt = lower_parent_path->mnt;

	/* Use vfs_path_lookup to check if the dentry exists or not */
	err = vfs_path_lookup(lower_dir_dentry, lower_dir_mnt, name->name, 0,
				&lower_path);
	/* check for other cases */
	if (err == -ENOENT) {
		struct file *file;
		const struct cred *cred = current_cred();

		struct sdcardfs_name_data buffer = {
			.ctx.actor = sdcardfs_name_match,
			.to_find = name,
			.name = __getname(),
			.found = false,
		};

		if (!buffer.name) {
			err = -ENOMEM;
			goto out;
		}
		file = dentry_open(lower_parent_path, O_RDONLY, cred);
		if (IS_ERR(file)) {
			err = PTR_ERR(file);
			goto put_name;
		}
		err = iterate_dir(file, &buffer.ctx);
		fput(file);
		if (err)
			goto put_name;

		if (buffer.found)
			err = vfs_path_lookup(lower_dir_dentry,
						lower_dir_mnt,
						buffer.name, 0,
						&lower_path);
		else
			err = -ENOENT;
put_name:
		__putname(buffer.name);
	}

	/* no error: handle positive dentries */
	if (!err) {
		/* check if the dentry is an obb dentry
		 * if true, the lower_inode must be replaced with
		 * the inode of the graft path
		 */

		if (need_graft_path(dentry)) {

			/* setup_obb_dentry()
			 * The lower_path will be stored to the dentry's orig_path
			 * and the base obbpath will be copyed to the lower_path variable.
			 * if an error returned, there's no change in the lower_path
			 * returns: -ERRNO if error (0: no error)
			 */
			err = setup_obb_dentry(dentry, &lower_path);

			if (err) {
				/* if the sbi->obbpath is not available, we can optionally
				 * setup the lower_path with its orig_path.
				 * but, the current implementation just returns an error
				 * because the sdcard daemon also regards this case as
				 * a lookup fail.
				 */
				pr_info("sdcardfs: base obbpath is not available\n");
				sdcardfs_put_reset_orig_path(dentry);
				goto out;
			}
		}

		sdcardfs_set_lower_path(dentry, &lower_path);
		err = sdcardfs_interpose(dentry, dentry->d_sb, &lower_path, id);
		if (err) /* path_put underlying path on error */
			sdcardfs_put_reset_lower_path(dentry);
		goto out;
	}

	/*
	 * We don't consider ENOENT an error, and we want to return a
	 * negative dentry.
	 */
	if (err && err != -ENOENT)
		goto out;

	/* instatiate a new negative dentry */
	dname.name = name->name;
	dname.len = name->len;
	dname.hash = full_name_hash(dname.name, dname.len);
	lower_dentry = d_lookup(lower_dir_dentry, &dname);
	if (lower_dentry)
		goto setup_lower;

	lower_dentry = d_alloc(lower_dir_dentry, &dname);
	if (!lower_dentry) {
		err = -ENOMEM;
		goto out;
	}
	d_add(lower_dentry, NULL); /* instantiate and hash */

setup_lower:
	lower_path.dentry = lower_dentry;
	lower_path.mnt = mntget(lower_dir_mnt);
	sdcardfs_set_lower_path(dentry, &lower_path);

	/*
	 * If the intent is to create a file, then don't return an error, so
	 * the VFS will continue the process of making this negative dentry
	 * into a positive one.
	 */
	if (flags & (LOOKUP_CREATE|LOOKUP_RENAME_TARGET))
		err = 0;

out:
	return ERR_PTR(err);
}

/*
 * On success:
 * fills dentry object appropriate values and returns NULL.
 * On fail (== error)
 * returns error ptr
 *
 * @dir : Parent inode. It is locked (dir->i_mutex)
 * @dentry : Target dentry to lookup. we should set each of fields.
 *	     (dentry->d_name is initialized already)
 * @nd : nameidata of parent inode
 */
struct dentry *sdcardfs_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags)
{
	struct dentry *ret = NULL, *parent;
	struct path lower_parent_path;
	int err = 0;
	const struct cred *saved_cred = NULL;

	parent = dget_parent(dentry);

	if (!check_caller_access_to_name(d_inode(parent), &dentry->d_name)) {
		ret = ERR_PTR(-EACCES);
		goto out_err;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED_PTR(SDCARDFS_SB(dir->i_sb), saved_cred, SDCARDFS_I(dir));

	sdcardfs_get_lower_path(parent, &lower_parent_path);

	/* allocate dentry private data.  We free it in ->d_release */
	err = new_dentry_private_data(dentry);
	if (err) {
		ret = ERR_PTR(err);
		goto out;
	}

	ret = __sdcardfs_lookup(dentry, flags, &lower_parent_path, SDCARDFS_I(dir)->userid);
	if (IS_ERR(ret))
		goto out;
	if (ret)
		dentry = ret;
	if (d_inode(dentry)) {
		fsstack_copy_attr_times(d_inode(dentry),
					sdcardfs_lower_inode(d_inode(dentry)));
		/* get derived permission */
		get_derived_permission(parent, dentry);
		fixup_tmp_permissions(d_inode(dentry));
		fixup_lower_ownership(dentry, dentry->d_name.name);
	}
	/* update parent directory's atime */
	fsstack_copy_attr_atime(d_inode(parent),
				sdcardfs_lower_inode(d_inode(parent)));

out:
	sdcardfs_put_lower_path(parent, &lower_parent_path);
	REVERT_CRED(saved_cred);
out_err:
	dput(parent);
	return ret;
}
