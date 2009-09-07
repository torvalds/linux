#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/posix_acl_xattr.h>
#include <linux/nfsacl.h>

#include "internal.h"

#define NFSDBG_FACILITY	NFSDBG_PROC

ssize_t nfs3_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl;
	int pos=0, len=0;

#	define output(s) do {						\
			if (pos + sizeof(s) <= size) {			\
				memcpy(buffer + pos, s, sizeof(s));	\
				pos += sizeof(s);			\
			}						\
			len += sizeof(s);				\
		} while(0)

	acl = nfs3_proc_getacl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
		output("system.posix_acl_access");
		posix_acl_release(acl);
	}

	if (S_ISDIR(inode->i_mode)) {
		acl = nfs3_proc_getacl(inode, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		if (acl) {
			output("system.posix_acl_default");
			posix_acl_release(acl);
		}
	}

#	undef output

	if (!buffer || len <= size)
		return len;
	return -ERANGE;
}

ssize_t nfs3_getxattr(struct dentry *dentry, const char *name,
		void *buffer, size_t size)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl;
	int type, error = 0;

	if (strcmp(name, POSIX_ACL_XATTR_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, POSIX_ACL_XATTR_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	acl = nfs3_proc_getacl(inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	else if (acl) {
		if (type == ACL_TYPE_ACCESS && acl->a_count == 0)
			error = -ENODATA;
		else
			error = posix_acl_to_xattr(acl, buffer, size);
		posix_acl_release(acl);
	} else
		error = -ENODATA;

	return error;
}

int nfs3_setxattr(struct dentry *dentry, const char *name,
	     const void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl;
	int type, error;

	if (strcmp(name, POSIX_ACL_XATTR_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, POSIX_ACL_XATTR_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	acl = posix_acl_from_xattr(value, size);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	error = nfs3_proc_setacl(inode, type, acl);
	posix_acl_release(acl);

	return error;
}

int nfs3_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode = dentry->d_inode;
	int type;

	if (strcmp(name, POSIX_ACL_XATTR_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, POSIX_ACL_XATTR_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	return nfs3_proc_setacl(inode, type, NULL);
}

static void __nfs3_forget_cached_acls(struct nfs_inode *nfsi)
{
	if (!IS_ERR(nfsi->acl_access)) {
		posix_acl_release(nfsi->acl_access);
		nfsi->acl_access = ERR_PTR(-EAGAIN);
	}
	if (!IS_ERR(nfsi->acl_default)) {
		posix_acl_release(nfsi->acl_default);
		nfsi->acl_default = ERR_PTR(-EAGAIN);
	}
}

void nfs3_forget_cached_acls(struct inode *inode)
{
	dprintk("NFS: nfs3_forget_cached_acls(%s/%ld)\n", inode->i_sb->s_id,
		inode->i_ino);
	spin_lock(&inode->i_lock);
	__nfs3_forget_cached_acls(NFS_I(inode));
	spin_unlock(&inode->i_lock);
}

static struct posix_acl *nfs3_get_cached_acl(struct inode *inode, int type)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct posix_acl *acl = ERR_PTR(-EINVAL);

	spin_lock(&inode->i_lock);
	switch(type) {
		case ACL_TYPE_ACCESS:
			acl = nfsi->acl_access;
			break;

		case ACL_TYPE_DEFAULT:
			acl = nfsi->acl_default;
			break;

		default:
			goto out;
	}
	if (IS_ERR(acl))
		acl = ERR_PTR(-EAGAIN);
	else
		acl = posix_acl_dup(acl);
out:
	spin_unlock(&inode->i_lock);
	dprintk("NFS: nfs3_get_cached_acl(%s/%ld, %d) = %p\n", inode->i_sb->s_id,
		inode->i_ino, type, acl);
	return acl;
}

static void nfs3_cache_acls(struct inode *inode, struct posix_acl *acl,
		    struct posix_acl *dfacl)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	dprintk("nfs3_cache_acls(%s/%ld, %p, %p)\n", inode->i_sb->s_id,
		inode->i_ino, acl, dfacl);
	spin_lock(&inode->i_lock);
	__nfs3_forget_cached_acls(NFS_I(inode));
	if (!IS_ERR(acl))
		nfsi->acl_access = posix_acl_dup(acl);
	if (!IS_ERR(dfacl))
		nfsi->acl_default = posix_acl_dup(dfacl);
	spin_unlock(&inode->i_lock);
}

struct posix_acl *nfs3_proc_getacl(struct inode *inode, int type)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_fattr fattr;
	struct page *pages[NFSACL_MAXPAGES] = { };
	struct nfs3_getaclargs args = {
		.fh = NFS_FH(inode),
		/* The xdr layer may allocate pages here. */
		.pages = pages,
	};
	struct nfs3_getaclres res = {
		.fattr =	&fattr,
	};
	struct rpc_message msg = {
		.rpc_argp	= &args,
		.rpc_resp	= &res,
	};
	struct posix_acl *acl;
	int status, count;

	if (!nfs_server_capable(inode, NFS_CAP_ACLS))
		return ERR_PTR(-EOPNOTSUPP);

	status = nfs_revalidate_inode(server, inode);
	if (status < 0)
		return ERR_PTR(status);
	acl = nfs3_get_cached_acl(inode, type);
	if (acl != ERR_PTR(-EAGAIN))
		return acl;
	acl = NULL;

	/*
	 * Only get the access acl when explicitly requested: We don't
	 * need it for access decisions, and only some applications use
	 * it. Applications which request the access acl first are not
	 * penalized from this optimization.
	 */
	if (type == ACL_TYPE_ACCESS)
		args.mask |= NFS_ACLCNT|NFS_ACL;
	if (S_ISDIR(inode->i_mode))
		args.mask |= NFS_DFACLCNT|NFS_DFACL;
	if (args.mask == 0)
		return NULL;

	dprintk("NFS call getacl\n");
	msg.rpc_proc = &server->client_acl->cl_procinfo[ACLPROC3_GETACL];
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(server->client_acl, &msg, 0);
	dprintk("NFS reply getacl: %d\n", status);

	/* pages may have been allocated at the xdr layer. */
	for (count = 0; count < NFSACL_MAXPAGES && args.pages[count]; count++)
		__free_page(args.pages[count]);

	switch (status) {
		case 0:
			status = nfs_refresh_inode(inode, &fattr);
			break;
		case -EPFNOSUPPORT:
		case -EPROTONOSUPPORT:
			dprintk("NFS_V3_ACL extension not supported; disabling\n");
			server->caps &= ~NFS_CAP_ACLS;
		case -ENOTSUPP:
			status = -EOPNOTSUPP;
		default:
			goto getout;
	}
	if ((args.mask & res.mask) != args.mask) {
		status = -EIO;
		goto getout;
	}

	if (res.acl_access != NULL) {
		if (posix_acl_equiv_mode(res.acl_access, NULL) == 0) {
			posix_acl_release(res.acl_access);
			res.acl_access = NULL;
		}
	}
	nfs3_cache_acls(inode,
		(res.mask & NFS_ACL)   ? res.acl_access  : ERR_PTR(-EINVAL),
		(res.mask & NFS_DFACL) ? res.acl_default : ERR_PTR(-EINVAL));

	switch(type) {
		case ACL_TYPE_ACCESS:
			acl = res.acl_access;
			res.acl_access = NULL;
			break;

		case ACL_TYPE_DEFAULT:
			acl = res.acl_default;
			res.acl_default = NULL;
	}

getout:
	posix_acl_release(res.acl_access);
	posix_acl_release(res.acl_default);

	if (status != 0) {
		posix_acl_release(acl);
		acl = ERR_PTR(status);
	}
	return acl;
}

static int nfs3_proc_setacls(struct inode *inode, struct posix_acl *acl,
		  struct posix_acl *dfacl)
{
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_fattr fattr;
	struct page *pages[NFSACL_MAXPAGES];
	struct nfs3_setaclargs args = {
		.inode = inode,
		.mask = NFS_ACL,
		.acl_access = acl,
		.pages = pages,
	};
	struct rpc_message msg = {
		.rpc_argp	= &args,
		.rpc_resp	= &fattr,
	};
	int status;

	status = -EOPNOTSUPP;
	if (!nfs_server_capable(inode, NFS_CAP_ACLS))
		goto out;

	/* We are doing this here, because XDR marshalling can only
	   return -ENOMEM. */
	status = -ENOSPC;
	if (acl != NULL && acl->a_count > NFS_ACL_MAX_ENTRIES)
		goto out;
	if (dfacl != NULL && dfacl->a_count > NFS_ACL_MAX_ENTRIES)
		goto out;
	if (S_ISDIR(inode->i_mode)) {
		args.mask |= NFS_DFACL;
		args.acl_default = dfacl;
		args.len = nfsacl_size(acl, dfacl);
	} else
		args.len = nfsacl_size(acl, NULL);

	if (args.len > NFS_ACL_INLINE_BUFSIZE) {
		unsigned int npages = 1 + ((args.len - 1) >> PAGE_SHIFT);

		status = -ENOMEM;
		do {
			args.pages[args.npages] = alloc_page(GFP_KERNEL);
			if (args.pages[args.npages] == NULL)
				goto out_freepages;
			args.npages++;
		} while (args.npages < npages);
	}

	dprintk("NFS call setacl\n");
	msg.rpc_proc = &server->client_acl->cl_procinfo[ACLPROC3_SETACL];
	nfs_fattr_init(&fattr);
	status = rpc_call_sync(server->client_acl, &msg, 0);
	nfs_access_zap_cache(inode);
	nfs_zap_acl_cache(inode);
	dprintk("NFS reply setacl: %d\n", status);

	switch (status) {
		case 0:
			status = nfs_refresh_inode(inode, &fattr);
			nfs3_cache_acls(inode, acl, dfacl);
			break;
		case -EPFNOSUPPORT:
		case -EPROTONOSUPPORT:
			dprintk("NFS_V3_ACL SETACL RPC not supported"
					"(will not retry)\n");
			server->caps &= ~NFS_CAP_ACLS;
		case -ENOTSUPP:
			status = -EOPNOTSUPP;
	}
out_freepages:
	while (args.npages != 0) {
		args.npages--;
		__free_page(args.pages[args.npages]);
	}
out:
	return status;
}

int nfs3_proc_setacl(struct inode *inode, int type, struct posix_acl *acl)
{
	struct posix_acl *alloc = NULL, *dfacl = NULL;
	int status;

	if (S_ISDIR(inode->i_mode)) {
		switch(type) {
			case ACL_TYPE_ACCESS:
				alloc = dfacl = nfs3_proc_getacl(inode,
						ACL_TYPE_DEFAULT);
				if (IS_ERR(alloc))
					goto fail;
				break;

			case ACL_TYPE_DEFAULT:
				dfacl = acl;
				alloc = acl = nfs3_proc_getacl(inode,
						ACL_TYPE_ACCESS);
				if (IS_ERR(alloc))
					goto fail;
				break;

			default:
				return -EINVAL;
		}
	} else if (type != ACL_TYPE_ACCESS)
			return -EINVAL;

	if (acl == NULL) {
		alloc = acl = posix_acl_from_mode(inode->i_mode, GFP_KERNEL);
		if (IS_ERR(alloc))
			goto fail;
	}
	status = nfs3_proc_setacls(inode, acl, dfacl);
	posix_acl_release(alloc);
	return status;

fail:
	return PTR_ERR(alloc);
}

int nfs3_proc_set_default_acl(struct inode *dir, struct inode *inode,
		mode_t mode)
{
	struct posix_acl *dfacl, *acl;
	int error = 0;

	dfacl = nfs3_proc_getacl(dir, ACL_TYPE_DEFAULT);
	if (IS_ERR(dfacl)) {
		error = PTR_ERR(dfacl);
		return (error == -EOPNOTSUPP) ? 0 : error;
	}
	if (!dfacl)
		return 0;
	acl = posix_acl_clone(dfacl, GFP_KERNEL);
	error = -ENOMEM;
	if (!acl)
		goto out_release_dfacl;
	error = posix_acl_create_masq(acl, &mode);
	if (error < 0)
		goto out_release_acl;
	error = nfs3_proc_setacls(inode, acl, S_ISDIR(inode->i_mode) ?
						      dfacl : NULL);
out_release_acl:
	posix_acl_release(acl);
out_release_dfacl:
	posix_acl_release(dfacl);
	return error;
}
