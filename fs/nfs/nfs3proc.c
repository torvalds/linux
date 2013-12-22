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

#include "iostat.h"
#include "internal.h"

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
		freezable_schedule_timeout_killable_unsafe(NFS_JUKEBOX_RETRY_TIME);
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
	if (task->tk_status == -EJUKEBOX)
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
		struct nfs_fattr *fattr)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_GETATTR],
		.rpc_argp	= fhandle,
		.rpc_resp	= fattr,
	};
	int	status;

	dprintk("NFS call  getattr\n");
	nfs_fattr_init(fattr);
	status = rpc_call_sync(server->client, &msg, 0);
	dprintk("NFS reply getattr: %d\n", status);
	return status;
}

static int
nfs3_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
			struct iattr *sattr)
{
	struct inode *inode = dentry->d_inode;
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
	if (status == 0)
		nfs_setattr_update_inode(inode, sattr);
	dprintk("NFS reply setattr: %d\n", status);
	return status;
}

static int
nfs3_proc_lookup(struct rpc_clnt *clnt, struct inode *dir, struct qstr *name,
		 struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
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

	dprintk("NFS call  lookup %s\n", name->name);
	res.dir_attr = nfs_alloc_fattr();
	if (res.dir_attr == NULL)
		return -ENOMEM;

	nfs_fattr_init(fattr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_refresh_inode(dir, res.dir_attr);
	if (status >= 0 && !(fattr->valid & NFS_ATTR_FATTR)) {
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_GETATTR];
		msg.rpc_argp = fhandle;
		msg.rpc_resp = fattr;
		status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	}
	nfs_free_fattr(res.dir_attr);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

static int nfs3_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs3_accessargs	arg = {
		.fh		= NFS_FH(inode),
	};
	struct nfs3_accessres	res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_ACCESS],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= entry->cred,
	};
	int mode = entry->mask;
	int status = -ENOMEM;

	dprintk("NFS call  access\n");

	if (mode & MAY_READ)
		arg.access |= NFS3_ACCESS_READ;
	if (S_ISDIR(inode->i_mode)) {
		if (mode & MAY_WRITE)
			arg.access |= NFS3_ACCESS_MODIFY | NFS3_ACCESS_EXTEND | NFS3_ACCESS_DELETE;
		if (mode & MAY_EXEC)
			arg.access |= NFS3_ACCESS_LOOKUP;
	} else {
		if (mode & MAY_WRITE)
			arg.access |= NFS3_ACCESS_MODIFY | NFS3_ACCESS_EXTEND;
		if (mode & MAY_EXEC)
			arg.access |= NFS3_ACCESS_EXECUTE;
	}

	res.fattr = nfs_alloc_fattr();
	if (res.fattr == NULL)
		goto out;

	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_refresh_inode(inode, res.fattr);
	if (status == 0) {
		entry->mask = 0;
		if (res.access & NFS3_ACCESS_READ)
			entry->mask |= MAY_READ;
		if (res.access & (NFS3_ACCESS_MODIFY | NFS3_ACCESS_EXTEND | NFS3_ACCESS_DELETE))
			entry->mask |= MAY_WRITE;
		if (res.access & (NFS3_ACCESS_LOOKUP|NFS3_ACCESS_EXECUTE))
			entry->mask |= MAY_EXEC;
	}
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

static int nfs3_do_create(struct inode *dir, struct dentry *dentry, struct nfs3_createdata *data)
{
	int status;

	status = rpc_call_sync(NFS_CLIENT(dir), &data->msg, 0);
	nfs_post_op_update_inode(dir, data->res.dir_attr);
	if (status == 0)
		status = nfs_instantiate(dentry, data->res.fh, data->res.fattr);
	return status;
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
		 int flags, struct nfs_open_context *ctx)
{
	struct nfs3_createdata *data;
	umode_t mode = sattr->ia_mode;
	int status = -ENOMEM;

	dprintk("NFS call  create %s\n", dentry->d_name.name);

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
		data->arg.create.verifier[0] = jiffies;
		data->arg.create.verifier[1] = current->pid;
	}

	sattr->ia_mode &= ~current_umask();

	for (;;) {
		status = nfs3_do_create(dir, dentry, data);

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
				goto out;
		}
		nfs_fattr_init(data->res.dir_attr);
		nfs_fattr_init(data->res.fattr);
	}

	if (status != 0)
		goto out;

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
		nfs_post_op_update_inode(dentry->d_inode, data->res.fattr);
		dprintk("NFS reply setattr (post-create): %d\n", status);
		if (status != 0)
			goto out;
	}
	status = nfs3_proc_set_default_acl(dir, dentry->d_inode, mode);
out:
	nfs3_free_createdata(data);
	dprintk("NFS reply create: %d\n", status);
	return status;
}

static int
nfs3_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs_removeargs arg = {
		.fh = NFS_FH(dir),
		.name.len = name->len,
		.name.name = name->name,
	};
	struct nfs_removeres res;
	struct rpc_message msg = {
		.rpc_proc = &nfs3_procedures[NFS3PROC_REMOVE],
		.rpc_argp = &arg,
		.rpc_resp = &res,
	};
	int status = -ENOMEM;

	dprintk("NFS call  remove %s\n", name->name);
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
nfs3_proc_unlink_setup(struct rpc_message *msg, struct inode *dir)
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
nfs3_proc_rename_setup(struct rpc_message *msg, struct inode *dir)
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
nfs3_proc_rename(struct inode *old_dir, struct qstr *old_name,
		 struct inode *new_dir, struct qstr *new_name)
{
	struct nfs_renameargs	arg = {
		.old_dir	= NFS_FH(old_dir),
		.old_name	= old_name,
		.new_dir	= NFS_FH(new_dir),
		.new_name	= new_name,
	};
	struct nfs_renameres res;
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_RENAME],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int status = -ENOMEM;

	dprintk("NFS call  rename %s -> %s\n", old_name->name, new_name->name);

	res.old_fattr = nfs_alloc_fattr();
	res.new_fattr = nfs_alloc_fattr();
	if (res.old_fattr == NULL || res.new_fattr == NULL)
		goto out;

	status = rpc_call_sync(NFS_CLIENT(old_dir), &msg, 0);
	nfs_post_op_update_inode(old_dir, res.old_fattr);
	nfs_post_op_update_inode(new_dir, res.new_fattr);
out:
	nfs_free_fattr(res.old_fattr);
	nfs_free_fattr(res.new_fattr);
	dprintk("NFS reply rename: %d\n", status);
	return status;
}

static int
nfs3_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
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
nfs3_proc_symlink(struct inode *dir, struct dentry *dentry, struct page *page,
		  unsigned int len, struct iattr *sattr)
{
	struct nfs3_createdata *data;
	int status = -ENOMEM;

	if (len > NFS3_MAXPATHLEN)
		return -ENAMETOOLONG;

	dprintk("NFS call  symlink %s\n", dentry->d_name.name);

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

	status = nfs3_do_create(dir, dentry, data);

	nfs3_free_createdata(data);
out:
	dprintk("NFS reply symlink: %d\n", status);
	return status;
}

static int
nfs3_proc_mkdir(struct inode *dir, struct dentry *dentry, struct iattr *sattr)
{
	struct nfs3_createdata *data;
	umode_t mode = sattr->ia_mode;
	int status = -ENOMEM;

	dprintk("NFS call  mkdir %s\n", dentry->d_name.name);

	sattr->ia_mode &= ~current_umask();

	data = nfs3_alloc_createdata();
	if (data == NULL)
		goto out;

	data->msg.rpc_proc = &nfs3_procedures[NFS3PROC_MKDIR];
	data->arg.mkdir.fh = NFS_FH(dir);
	data->arg.mkdir.name = dentry->d_name.name;
	data->arg.mkdir.len = dentry->d_name.len;
	data->arg.mkdir.sattr = sattr;

	status = nfs3_do_create(dir, dentry, data);
	if (status != 0)
		goto out;

	status = nfs3_proc_set_default_acl(dir, dentry->d_inode, mode);
out:
	nfs3_free_createdata(data);
	dprintk("NFS reply mkdir: %d\n", status);
	return status;
}

static int
nfs3_proc_rmdir(struct inode *dir, struct qstr *name)
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
static int
nfs3_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
		  u64 cookie, struct page **pages, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	__be32			*verf = NFS_I(dir)->cookieverf;
	struct nfs3_readdirargs	arg = {
		.fh		= NFS_FH(dir),
		.cookie		= cookie,
		.verf		= {verf[0], verf[1]},
		.plus		= plus,
		.count		= count,
		.pages		= pages
	};
	struct nfs3_readdirres	res = {
		.verf		= verf,
		.plus		= plus
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READDIR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred
	};
	int status = -ENOMEM;

	if (plus)
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_READDIRPLUS];

	dprintk("NFS call  readdir%s %d\n",
			plus? "plus" : "", (unsigned int) cookie);

	res.dir_attr = nfs_alloc_fattr();
	if (res.dir_attr == NULL)
		goto out;

	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);

	nfs_invalidate_atime(dir);
	nfs_refresh_inode(dir, res.dir_attr);

	nfs_free_fattr(res.dir_attr);
out:
	dprintk("NFS reply readdir%s: %d\n",
			plus? "plus" : "", status);
	return status;
}

static int
nfs3_proc_mknod(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
		dev_t rdev)
{
	struct nfs3_createdata *data;
	umode_t mode = sattr->ia_mode;
	int status = -ENOMEM;

	dprintk("NFS call  mknod %s %u:%u\n", dentry->d_name.name,
			MAJOR(rdev), MINOR(rdev));

	sattr->ia_mode &= ~current_umask();

	data = nfs3_alloc_createdata();
	if (data == NULL)
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
		goto out;
	}

	status = nfs3_do_create(dir, dentry, data);
	if (status != 0)
		goto out;
	status = nfs3_proc_set_default_acl(dir, dentry->d_inode, mode);
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

static int nfs3_read_done(struct rpc_task *task, struct nfs_read_data *data)
{
	if (nfs3_async_handle_jukebox(task, data->inode))
		return -EAGAIN;

	nfs_invalidate_atime(data->inode);
	nfs_refresh_inode(data->inode, &data->fattr);
	return 0;
}

static void nfs3_proc_read_setup(struct nfs_read_data *data, struct rpc_message *msg)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_READ];
}

static void nfs3_proc_read_rpc_prepare(struct rpc_task *task, struct nfs_read_data *data)
{
	rpc_call_start(task);
}

static int nfs3_write_done(struct rpc_task *task, struct nfs_write_data *data)
{
	if (nfs3_async_handle_jukebox(task, data->inode))
		return -EAGAIN;
	if (task->tk_status >= 0)
		nfs_post_op_update_inode_force_wcc(data->inode, data->res.fattr);
	return 0;
}

static void nfs3_proc_write_setup(struct nfs_write_data *data, struct rpc_message *msg)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_WRITE];
}

static void nfs3_proc_write_rpc_prepare(struct rpc_task *task, struct nfs_write_data *data)
{
	rpc_call_start(task);
}

static int nfs3_commit_done(struct rpc_task *task, struct nfs_write_data *data)
{
	if (nfs3_async_handle_jukebox(task, data->inode))
		return -EAGAIN;
	nfs_refresh_inode(data->inode, data->res.fattr);
	return 0;
}

static void nfs3_proc_commit_setup(struct nfs_write_data *data, struct rpc_message *msg)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_COMMIT];
}

static int
nfs3_proc_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	struct inode *inode = filp->f_path.dentry->d_inode;

	return nlmclnt_proc(NFS_SERVER(inode)->nlm_host, cmd, fl);
}

const struct nfs_rpc_ops nfs_v3_clientops = {
	.version	= 3,			/* protocol version */
	.dentry_ops	= &nfs_dentry_operations,
	.dir_inode_ops	= &nfs3_dir_inode_operations,
	.file_inode_ops	= &nfs3_file_inode_operations,
	.file_ops	= &nfs_file_operations,
	.getroot	= nfs3_proc_get_root,
	.getattr	= nfs3_proc_getattr,
	.setattr	= nfs3_proc_setattr,
	.lookup		= nfs3_proc_lookup,
	.access		= nfs3_proc_access,
	.readlink	= nfs3_proc_readlink,
	.create		= nfs3_proc_create,
	.remove		= nfs3_proc_remove,
	.unlink_setup	= nfs3_proc_unlink_setup,
	.unlink_rpc_prepare = nfs3_proc_unlink_rpc_prepare,
	.unlink_done	= nfs3_proc_unlink_done,
	.rename		= nfs3_proc_rename,
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
	.read_setup	= nfs3_proc_read_setup,
	.read_rpc_prepare = nfs3_proc_read_rpc_prepare,
	.read_done	= nfs3_read_done,
	.write_setup	= nfs3_proc_write_setup,
	.write_rpc_prepare = nfs3_proc_write_rpc_prepare,
	.write_done	= nfs3_write_done,
	.commit_setup	= nfs3_proc_commit_setup,
	.commit_done	= nfs3_commit_done,
	.lock		= nfs3_proc_lock,
	.clear_acl_cache = nfs3_forget_cached_acls,
	.close_context	= nfs_close_context,
	.init_client	= nfs_init_client,
};
