/*
 *	fs/nfsctl.c
 *
 *	This should eventually move to userland.
 *
 */
#include <linux/types.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/syscall.h>
#include <linux/linkage.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>

/*
 * open a file on nfsd fs
 */

static struct file *do_open(char *name, int flags)
{
	struct nameidata nd;
	int error;

	nd.mnt = do_kern_mount("nfsd", 0, "nfsd", NULL);

	if (IS_ERR(nd.mnt))
		return (struct file *)nd.mnt;

	nd.dentry = dget(nd.mnt->mnt_root);
	nd.last_type = LAST_ROOT;
	nd.flags = 0;
	nd.depth = 0;

	error = path_walk(name, &nd);
	if (error)
		return ERR_PTR(error);

	if (flags == O_RDWR)
		error = may_open(&nd,MAY_READ|MAY_WRITE,FMODE_READ|FMODE_WRITE);
	else
		error = may_open(&nd, MAY_WRITE, FMODE_WRITE);

	if (!error)
		return dentry_open(nd.dentry, nd.mnt, flags);

	path_release(&nd);
	return ERR_PTR(error);
}

static struct {
	char *name; int wsize; int rsize;
} map[] = {
	[NFSCTL_SVC] = {
		.name	= ".svc",
		.wsize	= sizeof(struct nfsctl_svc)
	},
	[NFSCTL_ADDCLIENT] = {
		.name	= ".add",
		.wsize	= sizeof(struct nfsctl_client)
	},
	[NFSCTL_DELCLIENT] = {
		.name	= ".del",
		.wsize	= sizeof(struct nfsctl_client)
	},
	[NFSCTL_EXPORT] = {
		.name	= ".export",
		.wsize	= sizeof(struct nfsctl_export)
	},
	[NFSCTL_UNEXPORT] = {
		.name	= ".unexport",
		.wsize	= sizeof(struct nfsctl_export)
	},
	[NFSCTL_GETFD] = {
		.name	= ".getfd",
		.wsize	= sizeof(struct nfsctl_fdparm),
		.rsize	= NFS_FHSIZE
	},
	[NFSCTL_GETFS] = {
		.name	= ".getfs",
		.wsize	= sizeof(struct nfsctl_fsparm),
		.rsize	= sizeof(struct knfsd_fh)
	},
};

long
asmlinkage sys_nfsservctl(int cmd, struct nfsctl_arg __user *arg, void __user *res)
{
	struct file *file;
	void __user *p = &arg->u;
	int version;
	int err;

	if (copy_from_user(&version, &arg->ca_version, sizeof(int)))
		return -EFAULT;

	if (version != NFSCTL_VERSION)
		return -EINVAL;

	if (cmd < 0 || cmd >= ARRAY_SIZE(map) || !map[cmd].name)
		return -EINVAL;

	file = do_open(map[cmd].name, map[cmd].rsize ? O_RDWR : O_WRONLY);	
	if (IS_ERR(file))
		return PTR_ERR(file);
	err = file->f_op->write(file, p, map[cmd].wsize, &file->f_pos);
	if (err >= 0 && map[cmd].rsize)
		err = file->f_op->read(file, res, map[cmd].rsize, &file->f_pos);
	if (err >= 0)
		err = 0;
	fput(file);
	return err;
}
