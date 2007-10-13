/*
 *  linux/fs/nfs/nfs3proc.c
 *
 *  Client-side NFSv3 procedures stubs.
 *
 *  Copyright (C) 1997, Olaf Kirch
 */

#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/lockd/bind.h>
#include <linux/nfs_mount.h>

#include "iostat.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_PROC

/* A wrapper to handle the EJUKEBOX error message */
static int
nfs3_rpc_wrapper(struct rpc_clnt *clnt, struct rpc_message *msg, int flags)
{
	sigset_t oldset;
	int res;
	rpc_clnt_sigmask(clnt, &oldset);
	do {
		res = rpc_call_sync(clnt, msg, flags);
		if (res != -EJUKEBOX)
			break;
		schedule_timeout_interruptible(NFS_JUKEBOX_RETRY_TIME);
		res = -ERESTARTSYS;
	} while (!signalled());
	rpc_clnt_sigunmask(clnt, &oldset);
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

	dprintk("%s: call  fsinfo\n", __FUNCTION__);
	nfs_fattr_init(info->fattr);
	status = rpc_call_sync(client, &msg, 0);
	dprintk("%s: reply fsinfo: %d\n", __FUNCTION__, status);
	if (!(info->fattr->valid & NFS_ATTR_FATTR)) {
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_GETATTR];
		msg.rpc_resp = info->fattr;
		status = rpc_call_sync(client, &msg, 0);
		dprintk("%s: reply getattr: %d\n", __FUNCTION__, status);
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
	nfs_fattr_init(fattr);
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	if (status == 0)
		nfs_setattr_update_inode(inode, sattr);
	dprintk("NFS reply setattr: %d\n", status);
	return status;
}

static int
nfs3_proc_lookup(struct inode *dir, struct qstr *name,
		 struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
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
	nfs_fattr_init(&dir_attr);
	nfs_fattr_init(fattr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	if (status >= 0 && !(fattr->valid & NFS_ATTR_FATTR)) {
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_GETATTR];
		msg.rpc_argp = fhandle;
		msg.rpc_resp = fattr;
		status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	}
	dprintk("NFS reply lookup: %d\n", status);
	if (status >= 0)
		status = nfs_refresh_inode(dir, &dir_attr);
	return status;
}

static int nfs3_proc_access(struct inode *inode, struct nfs_access_entry *entry)
{
	struct nfs_fattr	fattr;
	struct nfs3_accessargs	arg = {
		.fh		= NFS_FH(inode),
	};
	struct nfs3_accessres	res = {
		.fattr		= &fattr,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_ACCESS],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= entry->cred,
	};
	int mode = entry->mask;
	int status;

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
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_refresh_inode(inode, &fattr);
	if (status == 0) {
		entry->mask = 0;
		if (res.access & NFS3_ACCESS_READ)
			entry->mask |= MAY_READ;
		if (res.access & (NFS3_ACCESS_MODIFY | NFS3_ACCESS_EXTEND | NFS3_ACCESS_DELETE))
			entry->mask |= MAY_WRITE;
		if (res.access & (NFS3_ACCESS_LOOKUP|NFS3_ACCESS_EXECUTE))
			entry->mask |= MAY_EXEC;
	}
	dprintk("NFS reply access: %d\n", status);
	return status;
}

static int nfs3_proc_readlink(struct inode *inode, struct page *page,
		unsigned int pgbase, unsigned int pglen)
{
	struct nfs_fattr	fattr;
	struct nfs3_readlinkargs args = {
		.fh		= NFS_FH(inode),
		.pgbase		= pgbase,
		.pglen		= pglen,
		.pages		= &page
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READLINK],
		.rpc_argp	= &args,
		.rpc_resp	= &fattr,
	};
	int			status;

	dprintk("NFS call  readlink\n");
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_refresh_inode(inode, &fattr);
	dprintk("NFS reply readlink: %d\n", status);
	return status;
}

/*
 * Create a regular file.
 * For now, we don't implement O_EXCL.
 */
static int
nfs3_proc_create(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
		 int flags, struct nameidata *nd)
{
	struct nfs_fh		fhandle;
	struct nfs_fattr	fattr;
	struct nfs_fattr	dir_attr;
	struct nfs3_createargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= dentry->d_name.name,
		.len		= dentry->d_name.len,
		.sattr		= sattr,
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= &fhandle,
		.fattr		= &fattr
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_CREATE],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	mode_t mode = sattr->ia_mode;
	int status;

	dprintk("NFS call  create %s\n", dentry->d_name.name);
	arg.createmode = NFS3_CREATE_UNCHECKED;
	if (flags & O_EXCL) {
		arg.createmode  = NFS3_CREATE_EXCLUSIVE;
		arg.verifier[0] = jiffies;
		arg.verifier[1] = current->pid;
	}

	sattr->ia_mode &= ~current->fs->umask;

again:
	nfs_fattr_init(&dir_attr);
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_refresh_inode(dir, &dir_attr);

	/* If the server doesn't support the exclusive creation semantics,
	 * try again with simple 'guarded' mode. */
	if (status == -ENOTSUPP) {
		switch (arg.createmode) {
			case NFS3_CREATE_EXCLUSIVE:
				arg.createmode = NFS3_CREATE_GUARDED;
				break;

			case NFS3_CREATE_GUARDED:
				arg.createmode = NFS3_CREATE_UNCHECKED;
				break;

			case NFS3_CREATE_UNCHECKED:
				goto out;
		}
		goto again;
	}

	if (status == 0)
		status = nfs_instantiate(dentry, &fhandle, &fattr);
	if (status != 0)
		goto out;

	/* When we created the file with exclusive semantics, make
	 * sure we set the attributes afterwards. */
	if (arg.createmode == NFS3_CREATE_EXCLUSIVE) {
		dprintk("NFS call  setattr (post-create)\n");

		if (!(sattr->ia_valid & ATTR_ATIME_SET))
			sattr->ia_valid |= ATTR_ATIME;
		if (!(sattr->ia_valid & ATTR_MTIME_SET))
			sattr->ia_valid |= ATTR_MTIME;

		/* Note: we could use a guarded setattr here, but I'm
		 * not sure this buys us anything (and I'd have
		 * to revamp the NFSv3 XDR code) */
		status = nfs3_proc_setattr(dentry, &fattr, sattr);
		nfs_post_op_update_inode(dentry->d_inode, &fattr);
		dprintk("NFS reply setattr (post-create): %d\n", status);
	}
	if (status != 0)
		goto out;
	status = nfs3_proc_set_default_acl(dir, dentry->d_inode, mode);
out:
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
	int			status;

	dprintk("NFS call  remove %s\n", name->name);
	nfs_fattr_init(&res.dir_attr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_post_op_update_inode(dir, &res.dir_attr);
	dprintk("NFS reply remove: %d\n", status);
	return status;
}

static void
nfs3_proc_unlink_setup(struct rpc_message *msg, struct inode *dir)
{
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_REMOVE];
}

static int
nfs3_proc_unlink_done(struct rpc_task *task, struct inode *dir)
{
	struct nfs_removeres *res;
	if (nfs3_async_handle_jukebox(task, dir))
		return 0;
	res = task->tk_msg.rpc_resp;
	nfs_post_op_update_inode(dir, &res->dir_attr);
	return 1;
}

static int
nfs3_proc_rename(struct inode *old_dir, struct qstr *old_name,
		 struct inode *new_dir, struct qstr *new_name)
{
	struct nfs_fattr	old_dir_attr, new_dir_attr;
	struct nfs3_renameargs	arg = {
		.fromfh		= NFS_FH(old_dir),
		.fromname	= old_name->name,
		.fromlen	= old_name->len,
		.tofh		= NFS_FH(new_dir),
		.toname		= new_name->name,
		.tolen		= new_name->len
	};
	struct nfs3_renameres	res = {
		.fromattr	= &old_dir_attr,
		.toattr		= &new_dir_attr
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_RENAME],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int			status;

	dprintk("NFS call  rename %s -> %s\n", old_name->name, new_name->name);
	nfs_fattr_init(&old_dir_attr);
	nfs_fattr_init(&new_dir_attr);
	status = rpc_call_sync(NFS_CLIENT(old_dir), &msg, 0);
	nfs_post_op_update_inode(old_dir, &old_dir_attr);
	nfs_post_op_update_inode(new_dir, &new_dir_attr);
	dprintk("NFS reply rename: %d\n", status);
	return status;
}

static int
nfs3_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs_fattr	dir_attr, fattr;
	struct nfs3_linkargs	arg = {
		.fromfh		= NFS_FH(inode),
		.tofh		= NFS_FH(dir),
		.toname		= name->name,
		.tolen		= name->len
	};
	struct nfs3_linkres	res = {
		.dir_attr	= &dir_attr,
		.fattr		= &fattr
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_LINK],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int			status;

	dprintk("NFS call  link %s\n", name->name);
	nfs_fattr_init(&dir_attr);
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_post_op_update_inode(dir, &dir_attr);
	nfs_post_op_update_inode(inode, &fattr);
	dprintk("NFS reply link: %d\n", status);
	return status;
}

static int
nfs3_proc_symlink(struct inode *dir, struct dentry *dentry, struct page *page,
		  unsigned int len, struct iattr *sattr)
{
	struct nfs_fh fhandle;
	struct nfs_fattr fattr, dir_attr;
	struct nfs3_symlinkargs	arg = {
		.fromfh		= NFS_FH(dir),
		.fromname	= dentry->d_name.name,
		.fromlen	= dentry->d_name.len,
		.pages		= &page,
		.pathlen	= len,
		.sattr		= sattr
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= &fhandle,
		.fattr		= &fattr
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_SYMLINK],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int			status;

	if (len > NFS3_MAXPATHLEN)
		return -ENAMETOOLONG;

	dprintk("NFS call  symlink %s\n", dentry->d_name.name);

	nfs_fattr_init(&dir_attr);
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_post_op_update_inode(dir, &dir_attr);
	if (status != 0)
		goto out;
	status = nfs_instantiate(dentry, &fhandle, &fattr);
out:
	dprintk("NFS reply symlink: %d\n", status);
	return status;
}

static int
nfs3_proc_mkdir(struct inode *dir, struct dentry *dentry, struct iattr *sattr)
{
	struct nfs_fh fhandle;
	struct nfs_fattr fattr, dir_attr;
	struct nfs3_mkdirargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= dentry->d_name.name,
		.len		= dentry->d_name.len,
		.sattr		= sattr
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= &fhandle,
		.fattr		= &fattr
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_MKDIR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	int mode = sattr->ia_mode;
	int status;

	dprintk("NFS call  mkdir %s\n", dentry->d_name.name);

	sattr->ia_mode &= ~current->fs->umask;

	nfs_fattr_init(&dir_attr);
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_post_op_update_inode(dir, &dir_attr);
	if (status != 0)
		goto out;
	status = nfs_instantiate(dentry, &fhandle, &fattr);
	if (status != 0)
		goto out;
	status = nfs3_proc_set_default_acl(dir, dentry->d_inode, mode);
out:
	dprintk("NFS reply mkdir: %d\n", status);
	return status;
}

static int
nfs3_proc_rmdir(struct inode *dir, struct qstr *name)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_RMDIR],
		.rpc_argp	= &arg,
		.rpc_resp	= &dir_attr,
	};
	int			status;

	dprintk("NFS call  rmdir %s\n", name->name);
	nfs_fattr_init(&dir_attr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_post_op_update_inode(dir, &dir_attr);
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
		  u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	struct nfs_fattr	dir_attr;
	__be32			*verf = NFS_COOKIEVERF(dir);
	struct nfs3_readdirargs	arg = {
		.fh		= NFS_FH(dir),
		.cookie		= cookie,
		.verf		= {verf[0], verf[1]},
		.plus		= plus,
		.count		= count,
		.pages		= &page
	};
	struct nfs3_readdirres	res = {
		.dir_attr	= &dir_attr,
		.verf		= verf,
		.plus		= plus
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READDIR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred
	};
	int			status;

	if (plus)
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_READDIRPLUS];

	dprintk("NFS call  readdir%s %d\n",
			plus? "plus" : "", (unsigned int) cookie);

	nfs_fattr_init(&dir_attr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_refresh_inode(dir, &dir_attr);
	dprintk("NFS reply readdir: %d\n", status);
	return status;
}

static int
nfs3_proc_mknod(struct inode *dir, struct dentry *dentry, struct iattr *sattr,
		dev_t rdev)
{
	struct nfs_fh fh;
	struct nfs_fattr fattr, dir_attr;
	struct nfs3_mknodargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= dentry->d_name.name,
		.len		= dentry->d_name.len,
		.sattr		= sattr,
		.rdev		= rdev
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= &fh,
		.fattr		= &fattr
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_MKNOD],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
	};
	mode_t mode = sattr->ia_mode;
	int status;

	switch (sattr->ia_mode & S_IFMT) {
	case S_IFBLK:	arg.type = NF3BLK;  break;
	case S_IFCHR:	arg.type = NF3CHR;  break;
	case S_IFIFO:	arg.type = NF3FIFO; break;
	case S_IFSOCK:	arg.type = NF3SOCK; break;
	default:	return -EINVAL;
	}

	dprintk("NFS call  mknod %s %u:%u\n", dentry->d_name.name,
			MAJOR(rdev), MINOR(rdev));

	sattr->ia_mode &= ~current->fs->umask;

	nfs_fattr_init(&dir_attr);
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_post_op_update_inode(dir, &dir_attr);
	if (status != 0)
		goto out;
	status = nfs_instantiate(dentry, &fh, &fattr);
	if (status != 0)
		goto out;
	status = nfs3_proc_set_default_acl(dir, dentry->d_inode, mode);
out:
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
	dprintk("NFS reply statfs: %d\n", status);
	return status;
}

static int
nfs3_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
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
	status = rpc_call_sync(server->nfs_client->cl_rpcclient, &msg, 0);
	dprintk("NFS reply fsinfo: %d\n", status);
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
	/* Call back common NFS readpage processing */
	if (task->tk_status >= 0)
		nfs_refresh_inode(data->inode, &data->fattr);
	return 0;
}

static void nfs3_proc_read_setup(struct nfs_read_data *data)
{
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READ],
		.rpc_argp	= &data->args,
		.rpc_resp	= &data->res,
		.rpc_cred	= data->cred,
	};

	rpc_call_setup(&data->task, &msg, 0);
}

static int nfs3_write_done(struct rpc_task *task, struct nfs_write_data *data)
{
	if (nfs3_async_handle_jukebox(task, data->inode))
		return -EAGAIN;
	if (task->tk_status >= 0)
		nfs_post_op_update_inode(data->inode, data->res.fattr);
	return 0;
}

static void nfs3_proc_write_setup(struct nfs_write_data *data, int how)
{
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_WRITE],
		.rpc_argp	= &data->args,
		.rpc_resp	= &data->res,
		.rpc_cred	= data->cred,
	};

	data->args.stable = NFS_UNSTABLE;
	if (how & FLUSH_STABLE) {
		data->args.stable = NFS_FILE_SYNC;
		if (NFS_I(data->inode)->ncommit)
			data->args.stable = NFS_DATA_SYNC;
	}

	/* Finalize the task. */
	rpc_call_setup(&data->task, &msg, 0);
}

static int nfs3_commit_done(struct rpc_task *task, struct nfs_write_data *data)
{
	if (nfs3_async_handle_jukebox(task, data->inode))
		return -EAGAIN;
	if (task->tk_status >= 0)
		nfs_post_op_update_inode(data->inode, data->res.fattr);
	return 0;
}

static void nfs3_proc_commit_setup(struct nfs_write_data *data, int how)
{
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_COMMIT],
		.rpc_argp	= &data->args,
		.rpc_resp	= &data->res,
		.rpc_cred	= data->cred,
	};

	rpc_call_setup(&data->task, &msg, 0);
}

static int
nfs3_proc_lock(struct file *filp, int cmd, struct file_lock *fl)
{
	return nlmclnt_proc(filp->f_path.dentry->d_inode, cmd, fl);
}

const struct nfs_rpc_ops nfs_v3_clientops = {
	.version	= 3,			/* protocol version */
	.dentry_ops	= &nfs_dentry_operations,
	.dir_inode_ops	= &nfs3_dir_inode_operations,
	.file_inode_ops	= &nfs3_file_inode_operations,
	.getroot	= nfs3_proc_get_root,
	.getattr	= nfs3_proc_getattr,
	.setattr	= nfs3_proc_setattr,
	.lookup		= nfs3_proc_lookup,
	.access		= nfs3_proc_access,
	.readlink	= nfs3_proc_readlink,
	.create		= nfs3_proc_create,
	.remove		= nfs3_proc_remove,
	.unlink_setup	= nfs3_proc_unlink_setup,
	.unlink_done	= nfs3_proc_unlink_done,
	.rename		= nfs3_proc_rename,
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
	.read_done	= nfs3_read_done,
	.write_setup	= nfs3_proc_write_setup,
	.write_done	= nfs3_write_done,
	.commit_setup	= nfs3_proc_commit_setup,
	.commit_done	= nfs3_commit_done,
	.file_open	= nfs_open,
	.file_release	= nfs_release,
	.lock		= nfs3_proc_lock,
	.clear_acl_cache = nfs3_forget_cached_acls,
};
