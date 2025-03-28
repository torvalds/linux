// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/nfs/nfs3proc.c
 *
 *  Client-side NFSv3 procedures stubs.
 *
 *  Copyright (C) 1997, Olaf Kirch
 */

#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/slab.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/lockd/bind.h>
#include <linux/nfs_mount.h>
#include <linux/freezer.h>
#include <linux/xattr.h>

#include "iostat.h"
#include "internal.h"
#include "nfs3_fs.h"

#define NFSDBG_FACILITY		NFSDBG_PROC

/* A wrapper to handle the EJUKEBOX error messages */
static int
nfs3_rpc_wrapper(struct rpc_clnt *clnt, struct rpc_message *msg, int flags)
{
	int res;
	do {
		res = rpc_call_sync(clnt, msg, flags);
		if (res != -EJUKEBOX)
			break;
		__set_current_state(TASK_KILLABLE|TASK_FREEZABLE_UNSAFE);
		schedule_timeout(NFS_JUKEBOX_RETRY_TIME);
		res = -ERESTARTSYS;
	} while (!fatal_signal_pending(current));
	return res;
}

#define rpc_call_sync(clnt, msg, flags)	nfs3_rpc_wrapper(clnt, msg, flags)

static int
nfs3_async_handle_jukebox(struct rpc_task *task, struct inode *inode)
{
	if (task->tk_status != -EJUKEBOX)
		return 0;
	nfs_inc_stats(inode, NFSIOS_DELAY);
	task->tk_status = 0;
	rpc_restart_call(task);
	rpc_delay(task, NFS_JUKEBOX_RETRY_TIME);
	return 1;
}

static int
do_proc_get_root(struct rpc_clnt *client, struct nfs_fh *fhandle,
		 struct nfs_fsinfo *info)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_FSINFO],
		.rpc_argp	= fhandle,
		.rpc_resp	= info,
	};
	int	status;

	dprintk("%s: call  fsinfo\n", __func__);
	nfs_fattr_init(info->fattr);
	status = rpc_call_sync(client, &msg, 0);
	dprintk("%s: reply fsinfo: %d\n", __func__, status);
	if (status == 0 && !(info->fattr->valid & NFS_ATTR_FATTR)) {
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_GETATTR];
		msg.rpc_resp = info->fattr;
		status = rpc_call_sync(client, &msg, 0);
		dprintk("%s: reply getattr: %d\n", __func__, status);
	}
	return status;
}

/*
 * Bare-bones access to getattr: this is for nfs_get_root/nfs_get_sb
 */
static int
nfs3_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_fsinfo *info)
{
	int	status;

	status = do_proc_get_root(server->client, fhandle, info);
	if (status && server->nfs_client->cl_rpcclient != server->client)
		status = do_proc_get_root(server->nfs_client->cl_rpcclient, fhandle, info);
	return status;
}

/*
 * One function for each procedure in the NFS protocol.
 */
static int
nfs3_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
		struct nfs_fattr *fattr, struct inode *inode)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_GETATTR],
		.rpc_argp	= fhandle,
		.rpc_resp	= fattr,
	};
	int	status;
	unsigned short task_flags = 0;

	/* Is this is an attribute revalidation, subject to softreval? */
	if (inode && (server->flags & NFS_MOUNT_SOFTREVAL))
		task_flags |= RPC_TASK_TIMEOUT;

	dprintk("NFS call  getattr\n");
	nfs_fattr_init(fattr);
	status = rpc_call_sync(server->client, &msg, task_flags);
	dprintk("NFS reply getattr: %d\n", status);
	return status;
}

static int
nfs3_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
			struct iattr *sattr)
{
	struct inode *inode = d_inode(dentry);
	struct nfs3_sattrargs	arg = {
		.fh		= NFS_FH(inode),
		.sattr		= sattr,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_SETATTR],
		.rpc_argp	= &arg,
		.rpc_resp	= fattr,
	};
	int	status;

	dprintk("NFS call  setattr\n");
	if (sattr->ia_valid & ATTR_FILE)
		msg.rpc_cred = nfs_file_cred(sattr->ia_file);
	nfs_fattr_init(fattr);
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	if (status == 0) {
		nfs_setattr_update_inode(inode, sattr, fattr);
		if (NFS_I(inode)->cache_validity & NFS_INO_INVALID_ACL)
			nfs_zap_acl_cache(inode);
	}
	dprintk("NFS reply setattr: %d\n", status);
	return status;
}

static int
__nfs3_proc_lookup(struct inode *dir, const char *name, size_t len,
		   struct nfs_fh *fhandle, struct nfs_fattr *fattr,
		   unsigned short task_flags)
{
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name,
		.len		= len
	};
	struct nfs3_diropres	res = {
		.fh		= fhandle,
		.fattr		= fattr
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_LOOKUP],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int			status;

	res.dir_attr = nfs_alloc_fattr();
	if (res.dir_attr == NULL)
		return -ENOMEM;

	nfs_fattr_init(fattr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, task_flags);
	nfs_refresh_inode(dir, res.dir_attr);
	if (status >= 0 && !(fattr->valid & NFS_ATTR_FATTR)) {
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_GETATTR];
		msg.rpc_argp = fhandle;
		msg.rpc_resp = fattr;
		status = rpc_call_sync(NFS_CLIENT(dir), &msg, task_flags);
	}
	nfs_free_fattr(res.dir_attr);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

static int
nfs3_proc_lookup(struct inode *dir, struct dentry *dentry, const struct qstr *name,
		 struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	unsigned short task_flags = 0;

	/* Is this is an attribute revalidation, subject to softreval? */
	if (nfs_lookup_is_soft_revalidate(dentry))
		task_flags |= RPC_TASK_TIMEOUT;

	dprintk("NFS call  lookup %pd2\n", dentry);
	return __nfs3_proc_lookup(dir, name->name, name->len, fhandle, fattr,
				  task_flags);
}

static int nfs3_proc_lookupp(struct inode *inode, struct nfs_fh *fhandle,
			     struct nfs_fattr *fattr)
{
	const char dotdot[] = "..";
	const size_t len = strlen(dotdot);
	unsigned short task_flags = 0;

	if (NFS_SERVER(inode)->flags & NFS_MOUNT_SOFTREVAL)
		task_flags |= RPC_TASK_TIMEOUT;

	return __nfs3_proc_lookup(inode, dotdot, len, fhandle, fattr,
				  task_flags);
}

static int nfs3_proc_access(struct inode *inode, struct nfs_access_entry *entry,
			    const struct cred *cred)
{
	struct nfs3_accessargs	arg = {
		.fh		= NFS_FH(inode),
		.access		= entry->mask,
	};
	struct nfs3_accessres	res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_ACCESS],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred,
	};
	int status = -ENOMEM;

	dprintk("NFS call  access\n");
	res.fattr = nfs_alloc_fattr();
	if (res.fattr == NULL)
		goto out;

	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_refresh_inode(inode, res.fattr);
	if (status == 0)
		nfs_access_set_mask(entry, res.access);
	nfs_free_fattr(res.fattr);
out:
	dprintk("NFS reply access: %d\n", status);
	return status;
}

static int nfs3_proc_readlink(struct inode *inode, struct page *page,
		unsigned int pgbase, unsigned int pglen)
{
	struct nfs_fattr	*fattr;
	struct nfs3_readlinkargs args = {
		.fh		= NFS_FH(inode),
		.pgbase		= pgbase,
		.pglen		= pglen,
		.pages		= &page
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READLINK],
		.rpc_argp	= &args,
	};
	int status = -ENOMEM;

	dprintk("NFS call  readlink\n");
	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		goto out;
	msg.rpc_resp = fattr;

	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_refresh_inode(inode, fattr);
	nfs_free_fattr(fattr);
out:
	dprintk("NFS reply readlink: %d\n", status);
	return status;
}

struct nfs3_createdata {
	struct rpc_message msg;
	union {
		struct nfs3_createargs create;
		struct nfs3_mkdirargs mkdir;
		struct nfs3_symlinkargs symlink;
		struct nfs3_mknodargs mknod;
	} arg;
	struct nfs3_diropres res;
	struct nfs_fh fh;
	struct nfs_fattr fattr;
	struct nfs_fattr dir_attr;
};

static struct nfs3_createdata *nfs3_alloc_createdata(void)
{
	struct nfs3_createdata *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data != NULL) {
		data->msg.rpc_argp = &data->arg;
		data->msg.rpc_resp = &data->res;
		data->res.fh = &data->fh;
		data->res.fattr = &data->fattr;
		data->res.dir_attr = &data->dir_attr;
		nfs_fattr_init(data->res.fattr);
		nfs_fattr_init(data->res.dir_attr);
	}
	return data;
}

static struct dentry *
nfs3_do_create(struct inode *dir, struct dentry *dentry, struct nfs3_createdata *data)
{
	int status;

	status = rpc_call_sync(NFS_CLIENT(dir), &data->msg, 0);
	nfs_post_op_update_inode(dir, data->res.dir_attr);
	if (status != 0)
		return ERR_PTR(status);

	return nfs_add_or_obtain(dentry, data->res.fh, data->res.fattr);
}

static void nfs3_free_createdata(struct nfs3_createdata *data)
{
	kfree(data);
}

/*
 * Create a regular file.
 */
static int
nfs3_proc_create(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
		 int flags)
{
	struct posix_acl *default_acl, *acl;
	struct nfs3_createdata *data;
	struct dentry *d_alias;
	int status = -ENOMEM;

	dprintk("NFS call  create %pd\n", dentry);

	data = nfs3_alloc_createdata();
	if (data == NULL)
		goto out;

	data->msg.rpc_proc = &nfs3_procedures[NFS3PROC_CREATE];
	data->arg.create.fh = NFS_FH(dir);
	data->arg.create.name = dentry->d_name.name;
	data->arg.create.len = dentry->d_name.len;
	data->arg.create.sattr = sattr;

	data->arg.create.createmode = NFS3_CREATE_UNCHECKED;
	if (flags & O_EXCL) {
		data->arg.create.createmode  = NFS3_CREATE_EXCLUSIVE;
		data->arg.create.verifier[0] = cpu_to_be32(jiffies);
		data->arg.create.verifier[1] = cpu_to_be32(current->pid);
	}

	status = posix_acl_create(dir, &sattr->ia_mode, &default_acl, &acl);
	if (status)
		goto out;

	for (;;) {
		d_alias = nfs3_do_create(dir, dentry, data);
		status = PTR_ERR_OR_ZERO(d_alias);

		if (status != -ENOTSUPP)
			break;
		/* If the server doesn't support the exclusive creation
		 * semantics, try again with simple 'guarded' mode. */
		switch (data->arg.create.createmode) {
			case NFS3_CREATE_EXCLUSIVE:
				data->arg.create.createmode = NFS3_CREATE_GUARDED;
				break;

			case NFS3_CREATE_GUARDED:
				data->arg.create.createmode = NFS3_CREATE_UNCHECKED;
				break;

			case NFS3_CREATE_UNCHECKED:
				goto out_release_acls;
		}
		nfs_fattr_init(data->res.dir_attr);
		nfs_fattr_init(data->res.fattr);
	}

	if (status != 0)
		goto out_release_acls;

	if (d_alias)
		dentry = d_alias;

	/* When we created the file with exclusive semantics, make
	 * sure we set the attributes afterwards. */
	if (data->arg.create.createmode == NFS3_CREATE_EXCLUSIVE) {
		dprintk("NFS call  setattr (post-create)\n");

		if (!(sattr->ia_valid & ATTR_ATIME_SET))
			sattr->ia_valid |= ATTR_ATIME;
		if (!(sattr->ia_valid & ATTR_MTIME_SET))
			sattr->ia_valid |= ATTR_MTIME;

		/* Note: we could use a guarded setattr here, but I'm
		 * not sure this buys us anything (and I'd have
		 * to revamp the NFSv3 XDR code) */
		status = nfs3_proc_setattr(dentry, data->res.fattr, sattr);
		nfs_post_op_update_inode(d_inode(dentry), data->res.fattr);
		dprintk("NFS reply setattr (post-create): %d\n", status);
		if (status != 0)
			goto out_dput;
	}

	status = nfs3_proc_setacls(d_inode(dentry), acl, default_acl);

out_dput:
	dput(d_alias);
out_release_acls:
	posix_acl_release(acl);
	posix_acl_release(default_acl);
out:
	nfs3_free_createdata(data);
	dprintk("NFS reply create: %d\n", status);
	return status;
}

static int
nfs3_proc_remove(struct inode *dir, struct dentry *dentry)
{
	struct nfs_removeargs arg = {
		.fh = NFS_FH(dir),
		.name = dentry->d_name,
	};
	struct nfs_removeres res;
	struct rpc_message msg = {
		.rpc_proc = &nfs3_procedures[NFS3PROC_REMOVE],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int status = -ENOMEM;

	dprintk("NFS call  remove %pd2\n", dentry);
	res.dir_attr = nfs_alloc_fattr();
	if (res.dir_attr == NULL)
		goto out;

	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_post_op_update_inode(dir, res.dir_attr);
	nfs_free_fattr(res.dir_attr);
out:
	dprintk("NFS reply remove: %d\n", status);
	return status;
}

static void
nfs3_proc_unlink_setup(struct rpc_message *msg,
		struct dentry *dentry,
		struct inode *inode)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_REMOVE];
}

static void nfs3_proc_unlink_rpc_prepare(struct rpc_task *task, struct nfs_unlinkdata *data)
{
	rpc_call_start(task);
}

static int
nfs3_proc_unlink_done(struct rpc_task *task, struct inode *dir)
{
	struct nfs_removeres *res;
	if (nfs3_async_handle_jukebox(task, dir))
		return 0;
	res = task->tk_msg.rpc_resp;
	nfs_post_op_update_inode(dir, res->dir_attr);
	return 1;
}

static void
nfs3_proc_rename_setup(struct rpc_message *msg,
		struct dentry *old_dentry,
		struct dentry *new_dentry)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_RENAME];
}

static void nfs3_proc_rename_rpc_prepare(struct rpc_task *task, struct nfs_renamedata *data)
{
	rpc_call_start(task);
}

static int
nfs3_proc_rename_done(struct rpc_task *task, struct inode *old_dir,
		      struct inode *new_dir)
{
	struct nfs_renameres *res;

	if (nfs3_async_handle_jukebox(task, old_dir))
		return 0;
	res = task->tk_msg.rpc_resp;

	nfs_post_op_update_inode(old_dir, res->old_fattr);
	nfs_post_op_update_inode(new_dir, res->new_fattr);
	return 1;
}

static int
nfs3_proc_link(struct inode *inode, struct inode *dir, const struct qstr *name)
{
	struct nfs3_linkargs	arg = {
		.fromfh		= NFS_FH(inode),
		.tofh		= NFS_FH(dir),
		.toname		= name->name,
		.tolen		= name->len
	};
	struct nfs3_linkres	res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_LINK],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int status = -ENOMEM;

	dprintk("NFS call  link %s\n", name->name);
	res.fattr = nfs_alloc_fattr();
	res.dir_attr = nfs_alloc_fattr();
	if (res.fattr == NULL || res.dir_attr == NULL)
		goto out;

	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_post_op_update_inode(dir, res.dir_attr);
	nfs_post_op_update_inode(inode, res.fattr);
out:
	nfs_free_fattr(res.dir_attr);
	nfs_free_fattr(res.fattr);
	dprintk("NFS reply link: %d\n", status);
	return status;
}

static int
nfs3_proc_symlink(struct inode *dir, struct dentry *dentry, struct folio *folio,
		  unsigned int len, struct iattr *sattr)
{
	struct page *page = &folio->page;
	struct nfs3_createdata *data;
	struct dentry *d_alias;
	int status = -ENOMEM;

	if (len > NFS3_MAXPATHLEN)
		return -ENAMETOOLONG;

	dprintk("NFS call  symlink %pd\n", dentry);

	data = nfs3_alloc_createdata();
	if (data == NULL)
		goto out;
	data->msg.rpc_proc = &nfs3_procedures[NFS3PROC_SYMLINK];
	data->arg.symlink.fromfh = NFS_FH(dir);
	data->arg.symlink.fromname = dentry->d_name.name;
	data->arg.symlink.fromlen = dentry->d_name.len;
	data->arg.symlink.pages = &page;
	data->arg.symlink.pathlen = len;
	data->arg.symlink.sattr = sattr;

	d_alias = nfs3_do_create(dir, dentry, data);
	status = PTR_ERR_OR_ZERO(d_alias);

	if (status == 0)
		dput(d_alias);

	nfs3_free_createdata(data);
out:
	dprintk("NFS reply symlink: %d\n", status);
	return status;
}

static struct dentry *
nfs3_proc_mkdir(struct inode *dir, struct dentry *dentry, struct iattr *sattr)
{
	struct posix_acl *default_acl, *acl;
	struct nfs3_createdata *data;
	struct dentry *ret = ERR_PTR(-ENOMEM);
	int status;

	dprintk("NFS call  mkdir %pd\n", dentry);

	data = nfs3_alloc_createdata();
	if (data == NULL)
		goto out;

	ret = ERR_PTR(posix_acl_create(dir, &sattr->ia_mode,
				       &default_acl, &acl));
	if (IS_ERR(ret))
		goto out;

	data->msg.rpc_proc = &nfs3_procedures[NFS3PROC_MKDIR];
	data->arg.mkdir.fh = NFS_FH(dir);
	data->arg.mkdir.name = dentry->d_name.name;
	data->arg.mkdir.len = dentry->d_name.len;
	data->arg.mkdir.sattr = sattr;

	ret = nfs3_do_create(dir, dentry, data);

	if (IS_ERR(ret))
		goto out_release_acls;

	if (ret)
		dentry = ret;

	status = nfs3_proc_setacls(d_inode(dentry), acl, default_acl);
	if (status) {
		dput(ret);
		ret = ERR_PTR(status);
	}

out_release_acls:
	posix_acl_release(acl);
	posix_acl_release(default_acl);
out:
	nfs3_free_createdata(data);
	dprintk("NFS reply mkdir: %d\n", PTR_ERR_OR_ZERO(ret));
	return ret;
}

static int
nfs3_proc_rmdir(struct inode *dir, const struct qstr *name)
{
	struct nfs_fattr	*dir_attr;
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_RMDIR],
		.rpc_argp	= &arg,
	};
	int status = -ENOMEM;

	dprintk("NFS call  rmdir %s\n", name->name);
	dir_attr = nfs_alloc_fattr();
	if (dir_attr == NULL)
		goto out;

	msg.rpc_resp = dir_attr;
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_post_op_update_inode(dir, dir_attr);
	nfs_free_fattr(dir_attr);
out:
	dprintk("NFS reply rmdir: %d\n", status);
	return status;
}

/*
 * The READDIR implementation is somewhat hackish - we pass the user buffer
 * to the encode function, which installs it in the receive iovec.
 * The decode function itself doesn't perform any decoding, it just makes
 * sure the reply is syntactically correct.
 *
 * Also note that this implementation handles both plain readdir and
 * readdirplus.
 */
static int nfs3_proc_readdir(struct nfs_readdir_arg *nr_arg,
			     struct nfs_readdir_res *nr_res)
{
	struct inode		*dir = d_inode(nr_arg->dentry);
	struct nfs3_readdirargs	arg = {
		.fh		= NFS_FH(dir),
		.cookie		= nr_arg->cookie,
		.plus		= nr_arg->plus,
		.count		= nr_arg->page_len,
		.pages		= nr_arg->pages
	};
	struct nfs3_readdirres	res = {
		.verf		= nr_res->verf,
		.plus		= nr_arg->plus,
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READDIR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= nr_arg->cred,
	};
	int status = -ENOMEM;

	if (nr_arg->plus)
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_READDIRPLUS];
	if (arg.cookie)
		memcpy(arg.verf, nr_arg->verf, sizeof(arg.verf));

	dprintk("NFS call  readdir%s %llu\n", nr_arg->plus ? "plus" : "",
		(unsigned long long)nr_arg->cookie);

	res.dir_attr = nfs_alloc_fattr();
	if (res.dir_attr == NULL)
		goto out;

	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);

	nfs_invalidate_atime(dir);
	nfs_refresh_inode(dir, res.dir_attr);

	nfs_free_fattr(res.dir_attr);
out:
	dprintk("NFS reply readdir%s: %d\n", nr_arg->plus ? "plus" : "",
		status);
	return status;
}

static int
nfs3_proc_mknod(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
		dev_t rdev)
{
	struct posix_acl *default_acl, *acl;
	struct nfs3_createdata *data;
	struct dentry *d_alias;
	int status = -ENOMEM;

	dprintk("NFS call  mknod %pd %u:%u\n", dentry,
			MAJOR(rdev), MINOR(rdev));

	data = nfs3_alloc_createdata();
	if (data == NULL)
		goto out;

	status = posix_acl_create(dir, &sattr->ia_mode, &default_acl, &acl);
	if (status)
		goto out;

	data->msg.rpc_proc = &nfs3_procedures[NFS3PROC_MKNOD];
	data->arg.mknod.fh = NFS_FH(dir);
	data->arg.mknod.name = dentry->d_name.name;
	data->arg.mknod.len = dentry->d_name.len;
	data->arg.mknod.sattr = sattr;
	data->arg.mknod.rdev = rdev;

	switch (sattr->ia_mode & S_IFMT) {
	case S_IFBLK:
		data->arg.mknod.type = NF3BLK;
		break;
	case S_IFCHR:
		data->arg.mknod.type = NF3CHR;
		break;
	case S_IFIFO:
		data->arg.mknod.type = NF3FIFO;
		break;
	case S_IFSOCK:
		data->arg.mknod.type = NF3SOCK;
		break;
	default:
		status = -EINVAL;
		goto out_release_acls;
	}

	d_alias = nfs3_do_create(dir, dentry, data);
	status = PTR_ERR_OR_ZERO(d_alias);
	if (status != 0)
		goto out_release_acls;

	if (d_alias)
		dentry = d_alias;

	status = nfs3_proc_setacls(d_inode(dentry), acl, default_acl);

	dput(d_alias);
out_release_acls:
	posix_acl_release(acl);
	posix_acl_release(default_acl);
out:
	nfs3_free_createdata(data);
	dprintk("NFS reply mknod: %d\n", status);
	return status;
}

static int
nfs3_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsstat *stat)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_FSSTAT],
		.rpc_argp	= fhandle,
		.rpc_resp	= stat,
	};
	int	status;

	dprintk("NFS call  fsstat\n");
	nfs_fattr_init(stat->fattr);
	status = rpc_call_sync(server->client, &msg, 0);
	dprintk("NFS reply fsstat: %d\n", status);
	return status;
}

static int
do_proc_fsinfo(struct rpc_clnt *client, struct nfs_fh *fhandle,
		 struct nfs_fsinfo *info)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_FSINFO],
		.rpc_argp	= fhandle,
		.rpc_resp	= info,
	};
	int	status;

	dprintk("NFS call  fsinfo\n");
	nfs_fattr_init(info->fattr);
	status = rpc_call_sync(client, &msg, 0);
	dprintk("NFS reply fsinfo: %d\n", status);
	return status;
}

/*
 * Bare-bones access to fsinfo: this is for nfs_get_root/nfs_get_sb via
 * nfs_create_server
 */
static int
nfs3_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_fsinfo *info)
{
	int	status;

	status = do_proc_fsinfo(server->client, fhandle, info);
	if (status && server->nfs_client->cl_rpcclient != server->client)
		status = do_proc_fsinfo(server->nfs_client->cl_rpcclient, fhandle, info);
	return status;
}

static int
nfs3_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_pathconf *info)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_PATHCONF],
		.rpc_argp	= fhandle,
		.rpc_resp	= info,
	};
	int	status;

	dprintk("NFS call  pathconf\n");
	nfs_fattr_init(info->fattr);
	status = rpc_call_sync(server->client, &msg, 0);
	dprintk("NFS reply pathconf: %d\n", status);
	return status;
}

#if IS_ENABLED(CONFIG_NFS_LOCALIO)

static unsigned nfs3_localio_probe_throttle __read_mostly = 0;
module_param(nfs3_localio_probe_throttle, uint, 0644);
MODULE_PARM_DESC(nfs3_localio_probe_throttle,
		 "Probe for NFSv3 LOCALIO every N IO requests. Must be power-of-2, defaults to 0 (probing disabled).");

static void nfs3_localio_probe(struct nfs_server *server)
{
	struct nfs_client *clp = server->nfs_client;

	/* Throttled to reduce nfs_local_probe_async() frequency */
	if (!nfs3_localio_probe_throttle || nfs_server_is_local(clp))
		return;

	/*
	 * Try (re)enabling LOCALIO if isn't enabled -- admin deems
	 * it worthwhile to periodically check if LOCALIO possible by
	 * setting the 'nfs3_localio_probe_throttle' module parameter.
	 *
	 * This is useful if LOCALIO was previously enabled, but was
	 * disabled due to server restart, and IO has successfully
	 * completed in terms of normal RPC.
	 */
	if ((clp->cl_uuid.nfs3_localio_probe_count++ &
	     (nfs3_localio_probe_throttle - 1)) == 0) {
		if (!nfs_server_is_local(clp))
			nfs_local_probe_async(clp);
	}
}

#else
static void nfs3_localio_probe(struct nfs_server *server) {}
#endif

static int nfs3_read_done(struct rpc_task *task, struct nfs_pgio_header *hdr)
{
	struct inode *inode = hdr->inode;
	struct nfs_server *server = NFS_SERVER(inode);

	if (hdr->pgio_done_cb != NULL)
		return hdr->pgio_done_cb(task, hdr);

	if (nfs3_async_handle_jukebox(task, inode))
		return -EAGAIN;

	if (task->tk_status >= 0) {
		if (!server->read_hdrsize)
			cmpxchg(&server->read_hdrsize, 0, hdr->res.replen);
		nfs3_localio_probe(server);
	}

	nfs_invalidate_atime(inode);
	nfs_refresh_inode(inode, &hdr->fattr);
	return 0;
}

static void nfs3_proc_read_setup(struct nfs_pgio_header *hdr,
				 struct rpc_message *msg)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_READ];
	hdr->args.replen = NFS_SERVER(hdr->inode)->read_hdrsize;
}

static int nfs3_proc_pgio_rpc_prepare(struct rpc_task *task,
				      struct nfs_pgio_header *hdr)
{
	rpc_call_start(task);
	return 0;
}

static int nfs3_write_done(struct rpc_task *task, struct nfs_pgio_header *hdr)
{
	struct inode *inode = hdr->inode;

	if (hdr->pgio_done_cb != NULL)
		return hdr->pgio_done_cb(task, hdr);

	if (nfs3_async_handle_jukebox(task, inode))
		return -EAGAIN;
	if (task->tk_status >= 0) {
		nfs_writeback_update_inode(hdr);
		nfs3_localio_probe(NFS_SERVER(inode));
	}
	return 0;
}

static void nfs3_proc_write_setup(struct nfs_pgio_header *hdr,
				  struct rpc_message *msg,
				  struct rpc_clnt **clnt)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_WRITE];
}

static void nfs3_proc_commit_rpc_prepare(struct rpc_task *task, struct nfs_commit_data *data)
{
	rpc_call_start(task);
}

static int nfs3_commit_done(struct rpc_task *task, struct nfs_commit_data *data)
{
	if (data->commit_done_cb != NULL)
		return data->commit_done_cb(task, data);

	if (nfs3_async_handle_jukebox(task, data->inode))
		return -EAGAIN;
	nfs_refresh_inode(data->inode, data->res.fattr);
	return 0;
}

static void nfs3_proc_commit_setup(struct nfs_commit_data *data, struct rpc_message *msg,
				   struct rpc_clnt **clnt)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_COMMIT];
}

static void nfs3_nlm_alloc_call(void *data)
{
	struct nfs_lock_context *l_ctx = data;
	if (l_ctx && test_bit(NFS_CONTEXT_UNLOCK, &l_ctx->open_context->flags)) {
		get_nfs_open_context(l_ctx->open_context);
		nfs_get_lock_context(l_ctx->open_context);
	}
}

static bool nfs3_nlm_unlock_prepare(struct rpc_task *task, void *data)
{
	struct nfs_lock_context *l_ctx = data;
	if (l_ctx && test_bit(NFS_CONTEXT_UNLOCK, &l_ctx->open_context->flags))
		return nfs_async_iocounter_wait(task, l_ctx);
	return false;

}

static void nfs3_nlm_release_call(void *data)
{
	struct nfs_lock_context *l_ctx = data;
	struct nfs_open_context *ctx;
	if (l_ctx && test_bit(NFS_CONTEXT_UNLOCK, &l_ctx->open_context->flags)) {
		ctx = l_ctx->open_context;
		nfs_put_lock_context(l_ctx);
		put_nfs_open_context(ctx);
	}
}

static const struct nlmclnt_operations nlmclnt_fl_close_lock_ops = {
	.nlmclnt_alloc_call = nfs3_nlm_alloc_call,
	.nlmclnt_unlock_prepare = nfs3_nlm_unlock_prepare,
	.nlmclnt_release_call = nfs3_nlm_release_call,
};

static int
nfs3_proc_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = file_inode(filp);
	struct nfs_lock_context *l_ctx = NULL;
	struct nfs_open_context *ctx = nfs_file_open_context(filp);
	int status;

	if (fl->c.flc_flags & FL_CLOSE) {
		l_ctx = nfs_get_lock_context(ctx);
		if (IS_ERR(l_ctx))
			l_ctx = NULL;
		else
			set_bit(NFS_CONTEXT_UNLOCK, &ctx->flags);
	}

	status = nlmclnt_proc(NFS_SERVER(inode)->nlm_host, cmd, fl, l_ctx);

	if (l_ctx)
		nfs_put_lock_context(l_ctx);

	return status;
}

static int nfs3_have_delegation(struct inode *inode, fmode_t type, int flags)
{
	return 0;
}

static int nfs3_return_delegation(struct inode *inode)
{
	if (S_ISREG(inode->i_mode))
		nfs_wb_all(inode);
	return 0;
}

static const struct inode_operations nfs3_dir_inode_operations = {
	.create		= nfs_create,
	.atomic_open	= nfs_atomic_open_v23,
	.lookup		= nfs_lookup,
	.link		= nfs_link,
	.unlink		= nfs_unlink,
	.symlink	= nfs_symlink,
	.mkdir		= nfs_mkdir,
	.rmdir		= nfs_rmdir,
	.mknod		= nfs_mknod,
	.rename		= nfs_rename,
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
#ifdef CONFIG_NFS_V3_ACL
	.listxattr	= nfs3_listxattr,
	.get_inode_acl	= nfs3_get_acl,
	.set_acl	= nfs3_set_acl,
#endif
};

static const struct inode_operations nfs3_file_inode_operations = {
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
#ifdef CONFIG_NFS_V3_ACL
	.listxattr	= nfs3_listxattr,
	.get_inode_acl	= nfs3_get_acl,
	.set_acl	= nfs3_set_acl,
#endif
};

const struct nfs_rpc_ops nfs_v3_clientops = {
	.version	= 3,			/* protocol version */
	.dentry_ops	= &nfs_dentry_operations,
	.dir_inode_ops	= &nfs3_dir_inode_operations,
	.file_inode_ops	= &nfs3_file_inode_operations,
	.file_ops	= &nfs_file_operations,
	.nlmclnt_ops	= &nlmclnt_fl_close_lock_ops,
	.getroot	= nfs3_proc_get_root,
	.submount	= nfs_submount,
	.try_get_tree	= nfs_try_get_tree,
	.getattr	= nfs3_proc_getattr,
	.setattr	= nfs3_proc_setattr,
	.lookup		= nfs3_proc_lookup,
	.lookupp	= nfs3_proc_lookupp,
	.access		= nfs3_proc_access,
	.readlink	= nfs3_proc_readlink,
	.create		= nfs3_proc_create,
	.remove		= nfs3_proc_remove,
	.unlink_setup	= nfs3_proc_unlink_setup,
	.unlink_rpc_prepare = nfs3_proc_unlink_rpc_prepare,
	.unlink_done	= nfs3_proc_unlink_done,
	.rename_setup	= nfs3_proc_rename_setup,
	.rename_rpc_prepare = nfs3_proc_rename_rpc_prepare,
	.rename_done	= nfs3_proc_rename_done,
	.link		= nfs3_proc_link,
	.symlink	= nfs3_proc_symlink,
	.mkdir		= nfs3_proc_mkdir,
	.rmdir		= nfs3_proc_rmdir,
	.readdir	= nfs3_proc_readdir,
	.mknod		= nfs3_proc_mknod,
	.statfs		= nfs3_proc_statfs,
	.fsinfo		= nfs3_proc_fsinfo,
	.pathconf	= nfs3_proc_pathconf,
	.decode_dirent	= nfs3_decode_dirent,
	.pgio_rpc_prepare = nfs3_proc_pgio_rpc_prepare,
	.read_setup	= nfs3_proc_read_setup,
	.read_done	= nfs3_read_done,
	.write_setup	= nfs3_proc_write_setup,
	.write_done	= nfs3_write_done,
	.commit_setup	= nfs3_proc_commit_setup,
	.commit_rpc_prepare = nfs3_proc_commit_rpc_prepare,
	.commit_done	= nfs3_commit_done,
	.lock		= nfs3_proc_lock,
	.clear_acl_cache = forget_all_cached_acls,
	.close_context	= nfs_close_context,
	.have_delegation = nfs3_have_delegation,
	.return_delegation = nfs3_return_delegation,
	.alloc_client	= nfs_alloc_client,
	.init_client	= nfs_init_client,
	.free_client	= nfs_free_client,
	.create_server	= nfs3_create_server,
	.clone_server	= nfs3_clone_server,
};
