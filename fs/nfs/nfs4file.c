// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/nfs/file.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/falloc.h>
#include <linux/mount.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_ssc.h>
#include "delegation.h"
#include "internal.h"
#include "iostat.h"
#include "fscache.h"
#include "pnfs.h"

#include "nfstrace.h"

#ifdef CONFIG_NFS_V4_2
#include "nfs42.h"
#endif

#define NFSDBG_FACILITY		NFSDBG_FILE

static int
nfs4_file_open(struct inode *inode, struct file *filp)
{
	struct nfs_open_context *ctx;
	struct dentry *dentry = file_dentry(filp);
	struct dentry *parent = NULL;
	struct inode *dir;
	unsigned openflags = filp->f_flags;
	struct iattr attr;
	int err;

	/*
	 * If no cached dentry exists or if it's negative, NFSv4 handled the
	 * opens in ->lookup() or ->create().
	 *
	 * We only get this far for a cached positive dentry.  We skipped
	 * revalidation, so handle it here by dropping the dentry and returning
	 * -EOPENSTALE.  The VFS will retry the lookup/create/open.
	 */

	dprintk("NFS: open file(%pd2)\n", dentry);

	err = nfs_check_flags(openflags);
	if (err)
		return err;

	if ((openflags & O_ACCMODE) == 3)
		return nfs_open(inode, filp);

	/* We can't create new files here */
	openflags &= ~(O_CREAT|O_EXCL);

	parent = dget_parent(dentry);
	dir = d_inode(parent);

	ctx = alloc_nfs_open_context(file_dentry(filp), filp->f_mode, filp);
	err = PTR_ERR(ctx);
	if (IS_ERR(ctx))
		goto out;

	attr.ia_valid = ATTR_OPEN;
	if (openflags & O_TRUNC) {
		attr.ia_valid |= ATTR_SIZE;
		attr.ia_size = 0;
		filemap_write_and_wait(inode->i_mapping);
	}

	inode = NFS_PROTO(dir)->open_context(dir, ctx, openflags, &attr, NULL);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		switch (err) {
		default:
			goto out_put_ctx;
		case -ENOENT:
		case -ESTALE:
		case -EISDIR:
		case -ENOTDIR:
		case -ELOOP:
			goto out_drop;
		}
	}
	if (inode != d_inode(dentry))
		goto out_drop;

	nfs_file_set_open_context(filp, ctx);
	nfs_fscache_open_file(inode, filp);
	err = 0;

out_put_ctx:
	put_nfs_open_context(ctx);
out:
	dput(parent);
	return err;

out_drop:
	d_drop(dentry);
	err = -EOPENSTALE;
	goto out_put_ctx;
}

/*
 * Flush all dirty pages, and check for write errors.
 */
static int
nfs4_file_flush(struct file *file, fl_owner_t id)
{
	struct inode	*inode = file_inode(file);
	errseq_t since;

	dprintk("NFS: flush(%pD2)\n", file);

	nfs_inc_stats(inode, NFSIOS_VFSFLUSH);
	if ((file->f_mode & FMODE_WRITE) == 0)
		return 0;

	/*
	 * If we're holding a write delegation, then check if we're required
	 * to flush the i/o on close. If not, then just start the i/o now.
	 */
	if (!nfs4_delegation_flush_on_close(inode))
		return filemap_fdatawrite(file->f_mapping);

	/* Flush writes to the server and return any errors */
	since = filemap_sample_wb_err(file->f_mapping);
	nfs_wb_all(inode);
	return filemap_check_wb_err(file->f_mapping, since);
}

#ifdef CONFIG_NFS_V4_2
static ssize_t __nfs4_copy_file_range(struct file *file_in, loff_t pos_in,
				      struct file *file_out, loff_t pos_out,
				      size_t count, unsigned int flags)
{
	struct nfs42_copy_notify_res *cn_resp = NULL;
	struct nl4_server *nss = NULL;
	nfs4_stateid *cnrs = NULL;
	ssize_t ret;
	bool sync = false;

	/* Only offload copy if superblock is the same */
	if (file_in->f_op != &nfs4_file_operations)
		return -EXDEV;
	if (!nfs_server_capable(file_inode(file_out), NFS_CAP_COPY) ||
	    !nfs_server_capable(file_inode(file_in), NFS_CAP_COPY))
		return -EOPNOTSUPP;
	if (file_inode(file_in) == file_inode(file_out))
		return -EOPNOTSUPP;
	/* if the copy size if smaller than 2 RPC payloads, make it
	 * synchronous
	 */
	if (count <= 2 * NFS_SERVER(file_inode(file_in))->rsize)
		sync = true;
retry:
	if (!nfs42_files_from_same_server(file_in, file_out)) {
		/* for inter copy, if copy size if smaller than 12 RPC
		 * payloads, fallback to traditional copy. There are
		 * 14 RPCs during an NFSv4.x mount between source/dest
		 * servers.
		 */
		if (sync ||
			count <= 14 * NFS_SERVER(file_inode(file_in))->rsize)
			return -EOPNOTSUPP;
		cn_resp = kzalloc(sizeof(struct nfs42_copy_notify_res),
				GFP_NOFS);
		if (unlikely(cn_resp == NULL))
			return -ENOMEM;

		ret = nfs42_proc_copy_notify(file_in, file_out, cn_resp);
		if (ret) {
			ret = -EOPNOTSUPP;
			goto out;
		}
		nss = &cn_resp->cnr_src;
		cnrs = &cn_resp->cnr_stateid;
	}
	ret = nfs42_proc_copy(file_in, pos_in, file_out, pos_out, count,
				nss, cnrs, sync);
out:
	if (!nfs42_files_from_same_server(file_in, file_out))
		kfree(cn_resp);
	if (ret == -EAGAIN)
		goto retry;
	return ret;
}

static ssize_t nfs4_copy_file_range(struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out,
				    size_t count, unsigned int flags)
{
	ssize_t ret;

	ret = __nfs4_copy_file_range(file_in, pos_in, file_out, pos_out, count,
				     flags);
	if (ret == -EOPNOTSUPP || ret == -EXDEV)
		ret = generic_copy_file_range(file_in, pos_in, file_out,
					      pos_out, count, flags);
	return ret;
}

static loff_t nfs4_file_llseek(struct file *filep, loff_t offset, int whence)
{
	loff_t ret;

	switch (whence) {
	case SEEK_HOLE:
	case SEEK_DATA:
		ret = nfs42_proc_llseek(filep, offset, whence);
		if (ret != -ENOTSUPP)
			return ret;
		fallthrough;
	default:
		return nfs_file_llseek(filep, offset, whence);
	}
}

static long nfs42_fallocate(struct file *filep, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(filep);
	long ret;

	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	if ((mode != 0) && (mode != (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE)))
		return -EOPNOTSUPP;

	ret = inode_newsize_ok(inode, offset + len);
	if (ret < 0)
		return ret;

	if (mode & FALLOC_FL_PUNCH_HOLE)
		return nfs42_proc_deallocate(filep, offset, len);
	return nfs42_proc_allocate(filep, offset, len);
}

static loff_t nfs42_remap_file_range(struct file *src_file, loff_t src_off,
		struct file *dst_file, loff_t dst_off, loff_t count,
		unsigned int remap_flags)
{
	struct inode *dst_inode = file_inode(dst_file);
	struct nfs_server *server = NFS_SERVER(dst_inode);
	struct inode *src_inode = file_inode(src_file);
	unsigned int bs = server->clone_blksize;
	bool same_inode = false;
	int ret;

	/* NFS does not support deduplication. */
	if (remap_flags & REMAP_FILE_DEDUP)
		return -EOPNOTSUPP;

	if (remap_flags & ~REMAP_FILE_ADVISORY)
		return -EINVAL;

	if (IS_SWAPFILE(dst_inode) || IS_SWAPFILE(src_inode))
		return -ETXTBSY;

	/* check alignment w.r.t. clone_blksize */
	ret = -EINVAL;
	if (bs) {
		if (!IS_ALIGNED(src_off, bs) || !IS_ALIGNED(dst_off, bs))
			goto out;
		if (!IS_ALIGNED(count, bs) && i_size_read(src_inode) != (src_off + count))
			goto out;
	}

	if (src_inode == dst_inode)
		same_inode = true;

	/* XXX: do we lock at all? what if server needs CB_RECALL_LAYOUT? */
	if (same_inode) {
		inode_lock(src_inode);
	} else if (dst_inode < src_inode) {
		inode_lock_nested(dst_inode, I_MUTEX_PARENT);
		inode_lock_nested(src_inode, I_MUTEX_CHILD);
	} else {
		inode_lock_nested(src_inode, I_MUTEX_PARENT);
		inode_lock_nested(dst_inode, I_MUTEX_CHILD);
	}

	/* flush all pending writes on both src and dst so that server
	 * has the latest data */
	ret = nfs_sync_inode(src_inode);
	if (ret)
		goto out_unlock;
	ret = nfs_sync_inode(dst_inode);
	if (ret)
		goto out_unlock;

	ret = nfs42_proc_clone(src_file, dst_file, src_off, dst_off, count);

	/* truncate inode page cache of the dst range so that future reads can fetch
	 * new data from server */
	if (!ret)
		truncate_inode_pages_range(&dst_inode->i_data, dst_off, dst_off + count - 1);

out_unlock:
	if (same_inode) {
		inode_unlock(src_inode);
	} else if (dst_inode < src_inode) {
		inode_unlock(src_inode);
		inode_unlock(dst_inode);
	} else {
		inode_unlock(dst_inode);
		inode_unlock(src_inode);
	}
out:
	return ret < 0 ? ret : count;
}

static int read_name_gen = 1;
#define SSC_READ_NAME_BODY "ssc_read_%d"

static struct file *__nfs42_ssc_open(struct vfsmount *ss_mnt,
		struct nfs_fh *src_fh, nfs4_stateid *stateid)
{
	struct nfs_fattr fattr;
	struct file *filep, *res;
	struct nfs_server *server;
	struct inode *r_ino = NULL;
	struct nfs_open_context *ctx;
	struct nfs4_state_owner *sp;
	char *read_name = NULL;
	int len, status = 0;

	server = NFS_SERVER(ss_mnt->mnt_root->d_inode);

	nfs_fattr_init(&fattr);

	status = nfs4_proc_getattr(server, src_fh, &fattr, NULL, NULL);
	if (status < 0) {
		res = ERR_PTR(status);
		goto out;
	}

	res = ERR_PTR(-ENOMEM);
	len = strlen(SSC_READ_NAME_BODY) + 16;
	read_name = kzalloc(len, GFP_NOFS);
	if (read_name == NULL)
		goto out;
	snprintf(read_name, len, SSC_READ_NAME_BODY, read_name_gen++);

	r_ino = nfs_fhget(ss_mnt->mnt_root->d_inode->i_sb, src_fh, &fattr,
			NULL);
	if (IS_ERR(r_ino)) {
		res = ERR_CAST(r_ino);
		goto out_free_name;
	}

	filep = alloc_file_pseudo(r_ino, ss_mnt, read_name, FMODE_READ,
				     r_ino->i_fop);
	if (IS_ERR(filep)) {
		res = ERR_CAST(filep);
		goto out_free_name;
	}
	filep->f_mode |= FMODE_READ;

	ctx = alloc_nfs_open_context(filep->f_path.dentry, filep->f_mode,
					filep);
	if (IS_ERR(ctx)) {
		res = ERR_CAST(ctx);
		goto out_filep;
	}

	res = ERR_PTR(-EINVAL);
	sp = nfs4_get_state_owner(server, ctx->cred, GFP_KERNEL);
	if (sp == NULL)
		goto out_ctx;

	ctx->state = nfs4_get_open_state(r_ino, sp);
	if (ctx->state == NULL)
		goto out_stateowner;

	set_bit(NFS_SRV_SSC_COPY_STATE, &ctx->state->flags);
	set_bit(NFS_OPEN_STATE, &ctx->state->flags);
	memcpy(&ctx->state->open_stateid.other, &stateid->other,
	       NFS4_STATEID_OTHER_SIZE);
	update_open_stateid(ctx->state, stateid, NULL, filep->f_mode);

	nfs_file_set_open_context(filep, ctx);
	put_nfs_open_context(ctx);

	file_ra_state_init(&filep->f_ra, filep->f_mapping->host->i_mapping);
	res = filep;
out_free_name:
	kfree(read_name);
out:
	return res;
out_stateowner:
	nfs4_put_state_owner(sp);
out_ctx:
	put_nfs_open_context(ctx);
out_filep:
	fput(filep);
	goto out_free_name;
}

static void __nfs42_ssc_close(struct file *filep)
{
	struct nfs_open_context *ctx = nfs_file_open_context(filep);

	ctx->state->flags = 0;
}

static const struct nfs4_ssc_client_ops nfs4_ssc_clnt_ops_tbl = {
	.sco_open = __nfs42_ssc_open,
	.sco_close = __nfs42_ssc_close,
};

/**
 * nfs42_ssc_register_ops - Wrapper to register NFS_V4 ops in nfs_common
 *
 * Return values:
 *   None
 */
void nfs42_ssc_register_ops(void)
{
	nfs42_ssc_register(&nfs4_ssc_clnt_ops_tbl);
}

/**
 * nfs42_ssc_unregister_ops - wrapper to un-register NFS_V4 ops in nfs_common
 *
 * Return values:
 *   None.
 */
void nfs42_ssc_unregister_ops(void)
{
	nfs42_ssc_unregister(&nfs4_ssc_clnt_ops_tbl);
}
#endif /* CONFIG_NFS_V4_2 */

const struct file_operations nfs4_file_operations = {
	.read_iter	= nfs_file_read,
	.write_iter	= nfs_file_write,
	.mmap		= nfs_file_mmap,
	.open		= nfs4_file_open,
	.flush		= nfs4_file_flush,
	.release	= nfs_file_release,
	.fsync		= nfs_file_fsync,
	.lock		= nfs_lock,
	.flock		= nfs_flock,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.check_flags	= nfs_check_flags,
	.setlease	= simple_nosetlease,
#ifdef CONFIG_NFS_V4_2
	.copy_file_range = nfs4_copy_file_range,
	.llseek		= nfs4_file_llseek,
	.fallocate	= nfs42_fallocate,
	.remap_file_range = nfs42_remap_file_range,
#else
	.llseek		= nfs_file_llseek,
#endif
};
