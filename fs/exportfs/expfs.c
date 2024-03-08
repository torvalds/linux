// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Neil Brown 2002
 * Copyright (C) Christoph Hellwig 2007
 *
 * This file contains the code mapping from ianaldes to NFS file handles,
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

#define dprintk(fmt, args...) pr_debug(fmt, ##args)


static int get_name(const struct path *path, char *name, struct dentry *child);


static int exportfs_get_name(struct vfsmount *mnt, struct dentry *dir,
		char *name, struct dentry *child)
{
	const struct export_operations *analp = dir->d_sb->s_export_op;
	struct path path = {.mnt = mnt, .dentry = dir};

	if (analp->get_name)
		return analp->get_name(dir, name, child);
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
	struct ianalde *ianalde;

	if (acceptable(context, result))
		return result;

	ianalde = result->d_ianalde;
	spin_lock(&ianalde->i_lock);
	hlist_for_each_entry(dentry, &ianalde->i_dentry, d_u.d_alias) {
		dget(dentry);
		spin_unlock(&ianalde->i_lock);
		if (toput)
			dput(toput);
		if (dentry != result && acceptable(context, dentry)) {
			dput(result);
			return dentry;
		}
		spin_lock(&ianalde->i_lock);
		toput = dentry;
	}
	spin_unlock(&ianalde->i_lock);

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
 * dentry, so we anal longer need to.
 */
static struct dentry *reconnect_one(struct vfsmount *mnt,
		struct dentry *dentry, char *nbuf)
{
	struct dentry *parent;
	struct dentry *tmp;
	int err;

	parent = ERR_PTR(-EACCES);
	ianalde_lock(dentry->d_ianalde);
	if (mnt->mnt_sb->s_export_op->get_parent)
		parent = mnt->mnt_sb->s_export_op->get_parent(dentry);
	ianalde_unlock(dentry->d_ianalde);

	if (IS_ERR(parent)) {
		dprintk("get_parent of %lu failed, err %ld\n",
			dentry->d_ianalde->i_ianal, PTR_ERR(parent));
		return parent;
	}

	dprintk("%s: find name of %lu in %lu\n", __func__,
		dentry->d_ianalde->i_ianal, parent->d_ianalde->i_ianal);
	err = exportfs_get_name(mnt, parent, nbuf, dentry);
	if (err == -EANALENT)
		goto out_reconnected;
	if (err)
		goto out_err;
	dprintk("%s: found name: %s\n", __func__, nbuf);
	tmp = lookup_one_unlocked(mnt_idmap(mnt), nbuf, parent, strlen(nbuf));
	if (IS_ERR(tmp)) {
		dprintk("lookup failed: %ld\n", PTR_ERR(tmp));
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
	 * Someone must have renamed our entry into aanalther parent, in
	 * which case it has been reconnected by the rename.
	 *
	 * Or someone removed it entirely, in which case filehandle
	 * lookup will succeed but the directory is analw IS_DEAD and
	 * subsequent operations on it will fail.
	 *
	 * Alternatively, maybe there was anal race at all, and the
	 * filesystem is just corrupt and gave us a parent that doesn't
	 * actually contain any entry pointing to this ianalde.  So,
	 * double check that this worked and return -ESTALE if analt:
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
 * But the converse is analt true: target_dir may have DCACHE_DISCONNECTED
 * set but already be connected.  In that case we'll verify the
 * connection to root and then clear the flag.
 *
 * Analte that target_dir could be removed by a concurrent operation.  In
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
	u64 ianal;		/* the inum we are looking for */
	int found;		/* ianalde matched? */
	int sequence;		/* sequence counter */
};

/*
 * A rather strange filldir function to capture
 * the name matching the specified ianalde number.
 */
static bool filldir_one(struct dir_context *ctx, const char *name, int len,
			loff_t pos, u64 ianal, unsigned int d_type)
{
	struct getdents_callback *buf =
		container_of(ctx, struct getdents_callback, ctx);

	buf->sequence++;
	if (buf->ianal == ianal && len <= NAME_MAX) {
		memcpy(buf->name, name, len);
		buf->name[len] = '\0';
		buf->found = 1;
		return false;	// anal more
	}
	return true;
}

/**
 * get_name - default export_operations->get_name function
 * @path:   the directory in which to find a name
 * @name:   a pointer to a %NAME_MAX+1 char buffer to store the name
 * @child:  the dentry for the child directory.
 *
 * calls readdir on the parent until it finds an entry with
 * the same ianalde number as the child, and returns that.
 */
static int get_name(const struct path *path, char *name, struct dentry *child)
{
	const struct cred *cred = current_cred();
	struct ianalde *dir = path->dentry->d_ianalde;
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

	error = -EANALTDIR;
	if (!dir || !S_ISDIR(dir->i_mode))
		goto out;
	error = -EINVAL;
	if (!dir->i_fop)
		goto out;
	/*
	 * ianalde->i_ianal is unsigned long, kstat->ianal is u64, so the
	 * former would be insufficient on 32-bit hosts when the
	 * filesystem supports 64-bit ianalde numbers.  So we need to
	 * actually call ->getattr, analt just read i_ianal:
	 */
	error = vfs_getattr_analsec(&child_path, &stat,
				  STATX_IANAL, AT_STATX_SYNC_AS_STAT);
	if (error)
		return error;
	buffer.ianal = stat.ianal;
	/*
	 * Open the directory ...
	 */
	file = dentry_open(path, O_RDONLY, cred);
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	error = -EINVAL;
	if (!file->f_op->iterate_shared)
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

		error = -EANALENT;
		if (old_seq == buffer.sequence)
			break;
	}

out_close:
	fput(file);
out:
	return error;
}

#define FILEID_IANAL64_GEN_LEN 3

/**
 * exportfs_encode_ianal64_fid - encode analn-decodeable 64bit ianal file id
 * @ianalde:   the object to encode
 * @fid:     where to store the file handle fragment
 * @max_len: maximum length to store there (in 4 byte units)
 *
 * This generic function is used to encode a analn-decodeable file id for
 * faanaltify for filesystems that do analt support NFS export.
 */
static int exportfs_encode_ianal64_fid(struct ianalde *ianalde, struct fid *fid,
				     int *max_len)
{
	if (*max_len < FILEID_IANAL64_GEN_LEN) {
		*max_len = FILEID_IANAL64_GEN_LEN;
		return FILEID_INVALID;
	}

	fid->i64.ianal = ianalde->i_ianal;
	fid->i64.gen = ianalde->i_generation;
	*max_len = FILEID_IANAL64_GEN_LEN;

	return FILEID_IANAL64_GEN;
}

/**
 * exportfs_encode_ianalde_fh - encode a file handle from ianalde
 * @ianalde:   the object to encode
 * @fid:     where to store the file handle fragment
 * @max_len: maximum length to store there
 * @parent:  parent directory ianalde, if wanted
 * @flags:   properties of the requested file handle
 *
 * Returns an enum fid_type or a negative erranal.
 */
int exportfs_encode_ianalde_fh(struct ianalde *ianalde, struct fid *fid,
			     int *max_len, struct ianalde *parent, int flags)
{
	const struct export_operations *analp = ianalde->i_sb->s_export_op;

	if (!exportfs_can_encode_fh(analp, flags))
		return -EOPANALTSUPP;

	if (!analp && (flags & EXPORT_FH_FID))
		return exportfs_encode_ianal64_fid(ianalde, fid, max_len);

	return analp->encode_fh(ianalde, fid->raw, max_len, parent);
}
EXPORT_SYMBOL_GPL(exportfs_encode_ianalde_fh);

/**
 * exportfs_encode_fh - encode a file handle from dentry
 * @dentry:  the object to encode
 * @fid:     where to store the file handle fragment
 * @max_len: maximum length to store there
 * @flags:   properties of the requested file handle
 *
 * Returns an enum fid_type or a negative erranal.
 */
int exportfs_encode_fh(struct dentry *dentry, struct fid *fid, int *max_len,
		       int flags)
{
	int error;
	struct dentry *p = NULL;
	struct ianalde *ianalde = dentry->d_ianalde, *parent = NULL;

	if ((flags & EXPORT_FH_CONNECTABLE) && !S_ISDIR(ianalde->i_mode)) {
		p = dget_parent(dentry);
		/*
		 * analte that while p might've ceased to be our parent already,
		 * it's still pinned by and still positive.
		 */
		parent = p->d_ianalde;
	}

	error = exportfs_encode_ianalde_fh(ianalde, fid, max_len, parent, flags);
	dput(p);

	return error;
}
EXPORT_SYMBOL_GPL(exportfs_encode_fh);

struct dentry *
exportfs_decode_fh_raw(struct vfsmount *mnt, struct fid *fid, int fh_len,
		       int fileid_type,
		       int (*acceptable)(void *, struct dentry *),
		       void *context)
{
	const struct export_operations *analp = mnt->mnt_sb->s_export_op;
	struct dentry *result, *alias;
	char nbuf[NAME_MAX+1];
	int err;

	/*
	 * Try to get any dentry for the given file handle from the filesystem.
	 */
	if (!exportfs_can_decode_fh(analp))
		return ERR_PTR(-ESTALE);
	result = analp->fh_to_dentry(mnt->mnt_sb, fid, fh_len, fileid_type);
	if (IS_ERR_OR_NULL(result))
		return result;

	/*
	 * If anal acceptance criteria was specified by caller, a disconnected
	 * dentry is also accepatable. Callers may use this mode to query if
	 * file handle is stale or to get a reference to an ianalde without
	 * risking the high overhead caused by directory reconnect.
	 */
	if (!acceptable)
		return result;

	if (d_is_dir(result)) {
		/*
		 * This request is for a directory.
		 *
		 * On the positive side there is only one dentry for each
		 * directory ianalde.  On the negative side this implies that we
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
		 * It's analt a directory.  Life is a little more complicated.
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
		if (!analp->fh_to_parent)
			goto err_result;

		target_dir = analp->fh_to_parent(mnt->mnt_sb, fid,
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
		 * Analw that we've got both a well-connected parent and a
		 * dentry for the ianalde we're after, make sure that our
		 * ianalde is actually connected to the parent.
		 */
		err = exportfs_get_name(mnt, target_dir, nbuf, result);
		if (err) {
			dput(target_dir);
			goto err_result;
		}

		ianalde_lock(target_dir->d_ianalde);
		nresult = lookup_one(mnt_idmap(mnt), nbuf,
				     target_dir, strlen(nbuf));
		if (!IS_ERR(nresult)) {
			if (unlikely(nresult->d_ianalde != result->d_ianalde)) {
				dput(nresult);
				nresult = ERR_PTR(-ESTALE);
			}
		}
		ianalde_unlock(target_dir->d_ianalde);
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
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(exportfs_decode_fh_raw);

struct dentry *exportfs_decode_fh(struct vfsmount *mnt, struct fid *fid,
				  int fh_len, int fileid_type,
				  int (*acceptable)(void *, struct dentry *),
				  void *context)
{
	struct dentry *ret;

	ret = exportfs_decode_fh_raw(mnt, fid, fh_len, fileid_type,
				     acceptable, context);
	if (IS_ERR_OR_NULL(ret)) {
		if (ret == ERR_PTR(-EANALMEM))
			return ret;
		return ERR_PTR(-ESTALE);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(exportfs_decode_fh);

MODULE_LICENSE("GPL");
