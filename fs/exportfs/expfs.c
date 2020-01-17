// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Neil Brown 2002
 * Copyright (C) Christoph Hellwig 2007
 *
 * This file contains the code mapping from iyesdes to NFS file handles,
 * and for mapping back from file handles to dentries.
 *
 * For details on why we do all the strange and hairy things in here
 * take a look at Documentation/filesystems/nfs/exporting.rst.
 */
#include <linux/exportfs.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/cred.h>

#define dprintk(fmt, args...) do{}while(0)


static int get_name(const struct path *path, char *name, struct dentry *child);


static int exportfs_get_name(struct vfsmount *mnt, struct dentry *dir,
		char *name, struct dentry *child)
{
	const struct export_operations *yesp = dir->d_sb->s_export_op;
	struct path path = {.mnt = mnt, .dentry = dir};

	if (yesp->get_name)
		return yesp->get_name(dir, name, child);
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
	struct iyesde *iyesde;

	if (acceptable(context, result))
		return result;

	iyesde = result->d_iyesde;
	spin_lock(&iyesde->i_lock);
	hlist_for_each_entry(dentry, &iyesde->i_dentry, d_u.d_alias) {
		dget(dentry);
		spin_unlock(&iyesde->i_lock);
		if (toput)
			dput(toput);
		if (dentry != result && acceptable(context, dentry)) {
			dput(result);
			return dentry;
		}
		spin_lock(&iyesde->i_lock);
		toput = dentry;
	}
	spin_unlock(&iyesde->i_lock);

	if (toput)
		dput(toput);
	return NULL;
}

static bool dentry_connected(struct dentry *dentry)
{
	dget(dentry);
	while (dentry->d_flags & DCACHE_DISCONNECTED) {
		struct dentry *parent = dget_parent(dentry);

		dput(dentry);
		if (dentry == parent) {
			dput(parent);
			return false;
		}
		dentry = parent;
	}
	dput(dentry);
	return true;
}

static void clear_disconnected(struct dentry *dentry)
{
	dget(dentry);
	while (dentry->d_flags & DCACHE_DISCONNECTED) {
		struct dentry *parent = dget_parent(dentry);

		WARN_ON_ONCE(IS_ROOT(dentry));

		spin_lock(&dentry->d_lock);
		dentry->d_flags &= ~DCACHE_DISCONNECTED;
		spin_unlock(&dentry->d_lock);

		dput(dentry);
		dentry = parent;
	}
	dput(dentry);
}

/*
 * Reconnect a directory dentry with its parent.
 *
 * This can return a dentry, or NULL, or an error.
 *
 * In the first case the returned dentry is the parent of the given
 * dentry, and may itself need to be reconnected to its parent.
 *
 * In the NULL case, a concurrent VFS operation has either renamed or
 * removed this directory.  The concurrent operation has reconnected our
 * dentry, so we yes longer need to.
 */
static struct dentry *reconnect_one(struct vfsmount *mnt,
		struct dentry *dentry, char *nbuf)
{
	struct dentry *parent;
	struct dentry *tmp;
	int err;

	parent = ERR_PTR(-EACCES);
	iyesde_lock(dentry->d_iyesde);
	if (mnt->mnt_sb->s_export_op->get_parent)
		parent = mnt->mnt_sb->s_export_op->get_parent(dentry);
	iyesde_unlock(dentry->d_iyesde);

	if (IS_ERR(parent)) {
		dprintk("%s: get_parent of %ld failed, err %d\n",
			__func__, dentry->d_iyesde->i_iyes, PTR_ERR(parent));
		return parent;
	}

	dprintk("%s: find name of %lu in %lu\n", __func__,
		dentry->d_iyesde->i_iyes, parent->d_iyesde->i_iyes);
	err = exportfs_get_name(mnt, parent, nbuf, dentry);
	if (err == -ENOENT)
		goto out_reconnected;
	if (err)
		goto out_err;
	dprintk("%s: found name: %s\n", __func__, nbuf);
	tmp = lookup_one_len_unlocked(nbuf, parent, strlen(nbuf));
	if (IS_ERR(tmp)) {
		dprintk("%s: lookup failed: %d\n", __func__, PTR_ERR(tmp));
		err = PTR_ERR(tmp);
		goto out_err;
	}
	if (tmp != dentry) {
		/*
		 * Somebody has renamed it since exportfs_get_name();
		 * great, since it could've only been renamed if it
		 * got looked up and thus connected, and it would
		 * remain connected afterwards.  We are done.
		 */
		dput(tmp);
		goto out_reconnected;
	}
	dput(tmp);
	if (IS_ROOT(dentry)) {
		err = -ESTALE;
		goto out_err;
	}
	return parent;

out_err:
	dput(parent);
	return ERR_PTR(err);
out_reconnected:
	dput(parent);
	/*
	 * Someone must have renamed our entry into ayesther parent, in
	 * which case it has been reconnected by the rename.
	 *
	 * Or someone removed it entirely, in which case filehandle
	 * lookup will succeed but the directory is yesw IS_DEAD and
	 * subsequent operations on it will fail.
	 *
	 * Alternatively, maybe there was yes race at all, and the
	 * filesystem is just corrupt and gave us a parent that doesn't
	 * actually contain any entry pointing to this iyesde.  So,
	 * double check that this worked and return -ESTALE if yest:
	 */
	if (!dentry_connected(dentry))
		return ERR_PTR(-ESTALE);
	return NULL;
}

/*
 * Make sure target_dir is fully connected to the dentry tree.
 *
 * On successful return, DCACHE_DISCONNECTED will be cleared on
 * target_dir, and target_dir->d_parent->...->d_parent will reach the
 * root of the filesystem.
 *
 * Whenever DCACHE_DISCONNECTED is unset, target_dir is fully connected.
 * But the converse is yest true: target_dir may have DCACHE_DISCONNECTED
 * set but already be connected.  In that case we'll verify the
 * connection to root and then clear the flag.
 *
 * Note that target_dir could be removed by a concurrent operation.  In
 * that case reconnect_path may still succeed with target_dir fully
 * connected, but further operations using the filehandle will fail when
 * necessary (due to S_DEAD being set on the directory).
 */
static int
reconnect_path(struct vfsmount *mnt, struct dentry *target_dir, char *nbuf)
{
	struct dentry *dentry, *parent;

	dentry = dget(target_dir);

	while (dentry->d_flags & DCACHE_DISCONNECTED) {
		BUG_ON(dentry == mnt->mnt_sb->s_root);

		if (IS_ROOT(dentry))
			parent = reconnect_one(mnt, dentry, nbuf);
		else
			parent = dget_parent(dentry);

		if (!parent)
			break;
		dput(dentry);
		if (IS_ERR(parent))
			return PTR_ERR(parent);
		dentry = parent;
	}
	dput(dentry);
	clear_disconnected(target_dir);
	return 0;
}

struct getdents_callback {
	struct dir_context ctx;
	char *name;		/* name that was found. It already points to a
				   buffer NAME_MAX+1 is size */
	u64 iyes;		/* the inum we are looking for */
	int found;		/* iyesde matched? */
	int sequence;		/* sequence counter */
};

/*
 * A rather strange filldir function to capture
 * the name matching the specified iyesde number.
 */
static int filldir_one(struct dir_context *ctx, const char *name, int len,
			loff_t pos, u64 iyes, unsigned int d_type)
{
	struct getdents_callback *buf =
		container_of(ctx, struct getdents_callback, ctx);
	int result = 0;

	buf->sequence++;
	if (buf->iyes == iyes && len <= NAME_MAX) {
		memcpy(buf->name, name, len);
		buf->name[len] = '\0';
		buf->found = 1;
		result = -1;
	}
	return result;
}

/**
 * get_name - default export_operations->get_name function
 * @path:   the directory in which to find a name
 * @name:   a pointer to a %NAME_MAX+1 char buffer to store the name
 * @child:  the dentry for the child directory.
 *
 * calls readdir on the parent until it finds an entry with
 * the same iyesde number as the child, and returns that.
 */
static int get_name(const struct path *path, char *name, struct dentry *child)
{
	const struct cred *cred = current_cred();
	struct iyesde *dir = path->dentry->d_iyesde;
	int error;
	struct file *file;
	struct kstat stat;
	struct path child_path = {
		.mnt = path->mnt,
		.dentry = child,
	};
	struct getdents_callback buffer = {
		.ctx.actor = filldir_one,
		.name = name,
	};

	error = -ENOTDIR;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;
	error = -EINVAL;
	if (!dir->i_fop)
		goto out;
	/*
	 * iyesde->i_iyes is unsigned long, kstat->iyes is u64, so the
	 * former would be insufficient on 32-bit hosts when the
	 * filesystem supports 64-bit iyesde numbers.  So we need to
	 * actually call ->getattr, yest just read i_iyes:
	 */
	error = vfs_getattr_yessec(&child_path, &stat,
				  STATX_INO, AT_STATX_SYNC_AS_STAT);
	if (error)
		return error;
	buffer.iyes = stat.iyes;
	/*
	 * Open the directory ...
	 */
	file = dentry_open(path, O_RDONLY, cred);
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	error = -EINVAL;
	if (!file->f_op->iterate && !file->f_op->iterate_shared)
		goto out_close;

	buffer.sequence = 0;
	while (1) {
		int old_seq = buffer.sequence;

		error = iterate_dir(file, &buffer.ctx);
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
 * @iyesde:   the object to encode
 * @fid:     where to store the file handle fragment
 * @max_len: maximum length to store there
 * @parent:  parent directory iyesde, if wanted
 *
 * This default encode_fh function assumes that the 32 iyesde number
 * is suitable for locating an iyesde, and that the generation number
 * can be used to check that it is still valid.  It places them in the
 * filehandle fragment where export_decode_fh expects to find them.
 */
static int export_encode_fh(struct iyesde *iyesde, struct fid *fid,
		int *max_len, struct iyesde *parent)
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
	fid->i32.iyes = iyesde->i_iyes;
	fid->i32.gen = iyesde->i_generation;
	if (parent) {
		fid->i32.parent_iyes = parent->i_iyes;
		fid->i32.parent_gen = parent->i_generation;
		len = 4;
		type = FILEID_INO32_GEN_PARENT;
	}
	*max_len = len;
	return type;
}

int exportfs_encode_iyesde_fh(struct iyesde *iyesde, struct fid *fid,
			     int *max_len, struct iyesde *parent)
{
	const struct export_operations *yesp = iyesde->i_sb->s_export_op;

	if (yesp && yesp->encode_fh)
		return yesp->encode_fh(iyesde, fid->raw, max_len, parent);

	return export_encode_fh(iyesde, fid, max_len, parent);
}
EXPORT_SYMBOL_GPL(exportfs_encode_iyesde_fh);

int exportfs_encode_fh(struct dentry *dentry, struct fid *fid, int *max_len,
		int connectable)
{
	int error;
	struct dentry *p = NULL;
	struct iyesde *iyesde = dentry->d_iyesde, *parent = NULL;

	if (connectable && !S_ISDIR(iyesde->i_mode)) {
		p = dget_parent(dentry);
		/*
		 * yeste that while p might've ceased to be our parent already,
		 * it's still pinned by and still positive.
		 */
		parent = p->d_iyesde;
	}

	error = exportfs_encode_iyesde_fh(iyesde, fid, max_len, parent);
	dput(p);

	return error;
}
EXPORT_SYMBOL_GPL(exportfs_encode_fh);

struct dentry *exportfs_decode_fh(struct vfsmount *mnt, struct fid *fid,
		int fh_len, int fileid_type,
		int (*acceptable)(void *, struct dentry *), void *context)
{
	const struct export_operations *yesp = mnt->mnt_sb->s_export_op;
	struct dentry *result, *alias;
	char nbuf[NAME_MAX+1];
	int err;

	/*
	 * Try to get any dentry for the given file handle from the filesystem.
	 */
	if (!yesp || !yesp->fh_to_dentry)
		return ERR_PTR(-ESTALE);
	result = yesp->fh_to_dentry(mnt->mnt_sb, fid, fh_len, fileid_type);
	if (PTR_ERR(result) == -ENOMEM)
		return ERR_CAST(result);
	if (IS_ERR_OR_NULL(result))
		return ERR_PTR(-ESTALE);

	/*
	 * If yes acceptance criteria was specified by caller, a disconnected
	 * dentry is also accepatable. Callers may use this mode to query if
	 * file handle is stale or to get a reference to an iyesde without
	 * risking the high overhead caused by directory reconnect.
	 */
	if (!acceptable)
		return result;

	if (d_is_dir(result)) {
		/*
		 * This request is for a directory.
		 *
		 * On the positive side there is only one dentry for each
		 * directory iyesde.  On the negative side this implies that we
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
		 * It's yest a directory.  Life is a little more complicated.
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
		if (!yesp->fh_to_parent)
			goto err_result;

		target_dir = yesp->fh_to_parent(mnt->mnt_sb, fid,
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
		 * dentry for the iyesde we're after, make sure that our
		 * iyesde is actually connected to the parent.
		 */
		err = exportfs_get_name(mnt, target_dir, nbuf, result);
		if (err) {
			dput(target_dir);
			goto err_result;
		}

		iyesde_lock(target_dir->d_iyesde);
		nresult = lookup_one_len(nbuf, target_dir, strlen(nbuf));
		if (!IS_ERR(nresult)) {
			if (unlikely(nresult->d_iyesde != result->d_iyesde)) {
				dput(nresult);
				nresult = ERR_PTR(-ESTALE);
			}
		}
		iyesde_unlock(target_dir->d_iyesde);
		/*
		 * At this point we are done with the parent, but it's pinned
		 * by the child dentry anyway.
		 */
		dput(target_dir);

		if (IS_ERR(nresult)) {
			err = PTR_ERR(nresult);
			goto err_result;
		}
		dput(result);
		result = nresult;

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
	if (err != -ENOMEM)
		err = -ESTALE;
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(exportfs_decode_fh);

MODULE_LICENSE("GPL");
