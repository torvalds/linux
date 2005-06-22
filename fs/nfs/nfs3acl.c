#include <linux/fs.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/xattr_acl.h>
#include <linux/nfsacl.h>

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

	if (strcmp(name, XATTR_NAME_ACL_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, XATTR_NAME_ACL_DEFAULT) == 0)
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

	if (strcmp(name, XATTR_NAME_ACL_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, XATTR_NAME_ACL_DEFAULT) == 0)
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

	if (strcmp(name, XATTR_NAME_ACL_ACCESS) == 0)
		type = ACL_TYPE_ACCESS;
	else if (strcmp(name, XATTR_NAME_ACL_DEFAULT) == 0)
		type = ACL_TYPE_DEFAULT;
	else
		return -EOPNOTSUPP;

	return nfs3_proc_setacl(inode, type, NULL);
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
	struct posix_acl *acl = NULL;
	int status, count;

	if (!nfs_server_capable(inode, NFS_CAP_ACLS))
		return ERR_PTR(-EOPNOTSUPP);

	switch (type) {
		case ACL_TYPE_ACCESS:
			args.mask = NFS_ACLCNT|NFS_ACL;
			break;

		case ACL_TYPE_DEFAULT:
			if (!S_ISDIR(inode->i_mode))
				return NULL;
			args.mask = NFS_DFACLCNT|NFS_DFACL;
			break;

		default:
			return ERR_PTR(-EINVAL);
	}

	dprintk("NFS call getacl\n");
	status = rpc_call(server->client_acl, ACLPROC3_GETACL,
			  &args, &res, 0);
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
	struct page *pages[NFSACL_MAXPAGES] = { };
	struct nfs3_setaclargs args = {
		.inode = inode,
		.mask = NFS_ACL,
		.acl_access = acl,
		.pages = pages,
	};
	int status, count;

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
	}

	dprintk("NFS call setacl\n");
	nfs_begin_data_update(inode);
	status = rpc_call(server->client_acl, ACLPROC3_SETACL,
			  &args, &fattr, 0);
	NFS_FLAGS(inode) |= NFS_INO_INVALID_ACCESS;
	nfs_end_data_update(inode);
	dprintk("NFS reply setacl: %d\n", status);

	/* pages may have been allocated at the xdr layer. */
	for (count = 0; count < NFSACL_MAXPAGES && args.pages[count]; count++)
		__free_page(args.pages[count]);

	switch (status) {
		case 0:
			status = nfs_refresh_inode(inode, &fattr);
			break;
		case -EPFNOSUPPORT:
		case -EPROTONOSUPPORT:
			dprintk("NFS_V3_ACL SETACL RPC not supported"
					"(will not retry)\n");
			server->caps &= ~NFS_CAP_ACLS;
		case -ENOTSUPP:
			status = -EOPNOTSUPP;
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
