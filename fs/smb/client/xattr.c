// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2003, 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#include <linux/fs.h>
#include <linux/posix_acl_xattr.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "cifs_ioctl.h"

#define MAX_EA_VALUE_SIZE CIFSMaxBufSize
#define CIFS_XATTR_CIFS_ACL "system.cifs_acl" /* DACL only */
#define CIFS_XATTR_CIFS_NTSD "system.cifs_ntsd" /* owner plus DACL */
#define CIFS_XATTR_CIFS_NTSD_FULL "system.cifs_ntsd_full" /* owner/DACL/SACL */
#define CIFS_XATTR_ATTRIB "cifs.dosattrib"  /* full name: user.cifs.dosattrib */
#define CIFS_XATTR_CREATETIME "cifs.creationtime"  /* user.cifs.creationtime */
/*
 * Although these three are just aliases for the above, need to move away from
 * confusing users and using the 20+ year old term 'cifs' when it is no longer
 * secure, replaced by SMB2 (then even more highly secure SMB3) many years ago
 */
#define SMB3_XATTR_CIFS_ACL "system.smb3_acl" /* DACL only */
#define SMB3_XATTR_CIFS_NTSD "system.smb3_ntsd" /* owner plus DACL */
#define SMB3_XATTR_CIFS_NTSD_FULL "system.smb3_ntsd_full" /* owner/DACL/SACL */
#define SMB3_XATTR_ATTRIB "smb3.dosattrib"  /* full name: user.smb3.dosattrib */
#define SMB3_XATTR_CREATETIME "smb3.creationtime"  /* user.smb3.creationtime */
/* BB need to add server (Samba e.g) support for security and trusted prefix */

enum { XATTR_USER, XATTR_CIFS_ACL, XATTR_ACL_ACCESS, XATTR_ACL_DEFAULT,
	XATTR_CIFS_NTSD, XATTR_CIFS_NTSD_FULL };

static int cifs_attrib_set(unsigned int xid, struct cifs_tcon *pTcon,
			   struct inode *inode, const char *full_path,
			   const void *value, size_t size)
{
	ssize_t rc = -EOPNOTSUPP;
	__u32 *pattrib = (__u32 *)value;
	__u32 attrib;
	FILE_BASIC_INFO info_buf;

	if ((value == NULL) || (size != sizeof(__u32)))
		return -ERANGE;

	memset(&info_buf, 0, sizeof(info_buf));
	attrib = *pattrib;
	info_buf.Attributes = cpu_to_le32(attrib);
	if (pTcon->ses->server->ops->set_file_info)
		rc = pTcon->ses->server->ops->set_file_info(inode, full_path,
				&info_buf, xid);
	if (rc == 0)
		CIFS_I(inode)->cifsAttrs = attrib;

	return rc;
}

static int cifs_creation_time_set(unsigned int xid, struct cifs_tcon *pTcon,
				  struct inode *inode, const char *full_path,
				  const void *value, size_t size)
{
	ssize_t rc = -EOPNOTSUPP;
	__u64 *pcreation_time = (__u64 *)value;
	__u64 creation_time;
	FILE_BASIC_INFO info_buf;

	if ((value == NULL) || (size != sizeof(__u64)))
		return -ERANGE;

	memset(&info_buf, 0, sizeof(info_buf));
	creation_time = *pcreation_time;
	info_buf.CreationTime = cpu_to_le64(creation_time);
	if (pTcon->ses->server->ops->set_file_info)
		rc = pTcon->ses->server->ops->set_file_info(inode, full_path,
				&info_buf, xid);
	if (rc == 0)
		CIFS_I(inode)->createtime = creation_time;

	return rc;
}

static int cifs_xattr_set(const struct xattr_handler *handler,
			  struct mnt_idmap *idmap,
			  struct dentry *dentry, struct inode *inode,
			  const char *name, const void *value,
			  size_t size, int flags)
{
	int rc = -EOPNOTSUPP;
	unsigned int xid;
	struct super_block *sb = dentry->d_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct tcon_link *tlink;
	struct cifs_tcon *pTcon;
	const char *full_path;
	void *page;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	pTcon = tlink_tcon(tlink);

	xid = get_xid();
	page = alloc_dentry_path();

	full_path = build_path_from_dentry(dentry, page);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		goto out;
	}
	/* return dos attributes as pseudo xattr */
	/* return alt name if available as pseudo attr */

	/* if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to
		returns as xattrs */
	if (size > MAX_EA_VALUE_SIZE) {
		cifs_dbg(FYI, "size of EA value too large\n");
		rc = -EOPNOTSUPP;
		goto out;
	}

	switch (handler->flags) {
	case XATTR_USER:
		cifs_dbg(FYI, "%s:setting user xattr %s\n", __func__, name);
		if ((strcmp(name, CIFS_XATTR_ATTRIB) == 0) ||
		    (strcmp(name, SMB3_XATTR_ATTRIB) == 0)) {
			rc = cifs_attrib_set(xid, pTcon, inode, full_path,
					value, size);
			if (rc == 0) /* force revalidate of the inode */
				CIFS_I(inode)->time = 0;
			break;
		} else if ((strcmp(name, CIFS_XATTR_CREATETIME) == 0) ||
			   (strcmp(name, SMB3_XATTR_CREATETIME) == 0)) {
			rc = cifs_creation_time_set(xid, pTcon, inode,
					full_path, value, size);
			if (rc == 0) /* force revalidate of the inode */
				CIFS_I(inode)->time = 0;
			break;
		}

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
			goto out;

		if (pTcon->ses->server->ops->set_EA) {
			rc = pTcon->ses->server->ops->set_EA(xid, pTcon,
				full_path, name, value, (__u16)size,
				cifs_sb->local_nls, cifs_sb);
			if (rc == 0)
				inode_set_ctime_current(inode);
		}
		break;

	case XATTR_CIFS_ACL:
	case XATTR_CIFS_NTSD:
	case XATTR_CIFS_NTSD_FULL: {
		struct smb_ntsd *pacl;

		if (!value)
			goto out;
		pacl = kmalloc(size, GFP_KERNEL);
		if (!pacl) {
			rc = -ENOMEM;
		} else {
			memcpy(pacl, value, size);
			if (pTcon->ses->server->ops->set_acl) {
				int aclflags = 0;
				rc = 0;

				switch (handler->flags) {
				case XATTR_CIFS_NTSD_FULL:
					aclflags = (CIFS_ACL_OWNER |
						    CIFS_ACL_GROUP |
						    CIFS_ACL_DACL |
						    CIFS_ACL_SACL);
					break;
				case XATTR_CIFS_NTSD:
					aclflags = (CIFS_ACL_OWNER |
						    CIFS_ACL_GROUP |
						    CIFS_ACL_DACL);
					break;
				case XATTR_CIFS_ACL:
				default:
					aclflags = CIFS_ACL_DACL;
				}

				rc = pTcon->ses->server->ops->set_acl(pacl,
					size, inode, full_path, aclflags);
			} else {
				rc = -EOPNOTSUPP;
			}
			if (rc == 0) /* force revalidate of the inode */
				CIFS_I(inode)->time = 0;
			kfree(pacl);
		}
		break;
	}
	}

out:
	free_dentry_path(page);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

static int cifs_attrib_get(struct dentry *dentry,
			   struct inode *inode, void *value,
			   size_t size)
{
	ssize_t rc;
	__u32 *pattribute;

	rc = cifs_revalidate_dentry_attr(dentry);

	if (rc)
		return rc;

	if ((value == NULL) || (size == 0))
		return sizeof(__u32);
	else if (size < sizeof(__u32))
		return -ERANGE;

	/* return dos attributes as pseudo xattr */
	pattribute = (__u32 *)value;
	*pattribute = CIFS_I(inode)->cifsAttrs;

	return sizeof(__u32);
}

static int cifs_creation_time_get(struct dentry *dentry, struct inode *inode,
				  void *value, size_t size)
{
	ssize_t rc;
	__u64 *pcreatetime;

	rc = cifs_revalidate_dentry_attr(dentry);
	if (rc)
		return rc;

	if ((value == NULL) || (size == 0))
		return sizeof(__u64);
	else if (size < sizeof(__u64))
		return -ERANGE;

	/* return dos attributes as pseudo xattr */
	pcreatetime = (__u64 *)value;
	*pcreatetime = CIFS_I(inode)->createtime;
	return sizeof(__u64);
}


static int cifs_xattr_get(const struct xattr_handler *handler,
			  struct dentry *dentry, struct inode *inode,
			  const char *name, void *value, size_t size)
{
	ssize_t rc = -EOPNOTSUPP;
	unsigned int xid;
	struct super_block *sb = dentry->d_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct tcon_link *tlink;
	struct cifs_tcon *pTcon;
	const char *full_path;
	void *page;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	pTcon = tlink_tcon(tlink);

	xid = get_xid();
	page = alloc_dentry_path();

	full_path = build_path_from_dentry(dentry, page);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		goto out;
	}

	/* return alt name if available as pseudo attr */
	switch (handler->flags) {
	case XATTR_USER:
		cifs_dbg(FYI, "%s:querying user xattr %s\n", __func__, name);
		if ((strcmp(name, CIFS_XATTR_ATTRIB) == 0) ||
		    (strcmp(name, SMB3_XATTR_ATTRIB) == 0)) {
			rc = cifs_attrib_get(dentry, inode, value, size);
			break;
		} else if ((strcmp(name, CIFS_XATTR_CREATETIME) == 0) ||
		    (strcmp(name, SMB3_XATTR_CREATETIME) == 0)) {
			rc = cifs_creation_time_get(dentry, inode, value, size);
			break;
		}

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
			goto out;

		if (pTcon->ses->server->ops->query_all_EAs)
			rc = pTcon->ses->server->ops->query_all_EAs(xid, pTcon,
				full_path, name, value, size, cifs_sb);
		break;

	case XATTR_CIFS_ACL:
	case XATTR_CIFS_NTSD:
	case XATTR_CIFS_NTSD_FULL: {
		/*
		 * fetch owner, DACL, and SACL if asked for full descriptor,
		 * fetch owner and DACL otherwise
		 */
		u32 acllen, extra_info;
		struct smb_ntsd *pacl;

		if (pTcon->ses->server->ops->get_acl == NULL)
			goto out; /* rc already EOPNOTSUPP */

		switch (handler->flags) {
		case XATTR_CIFS_NTSD_FULL:
			extra_info = OWNER_SECINFO | GROUP_SECINFO | DACL_SECINFO | SACL_SECINFO;
			break;
		case XATTR_CIFS_NTSD:
			extra_info = OWNER_SECINFO | GROUP_SECINFO | DACL_SECINFO;
			break;
		case XATTR_CIFS_ACL:
		default:
			extra_info = DACL_SECINFO;
			break;
		}
		pacl = pTcon->ses->server->ops->get_acl(cifs_sb,
				inode, full_path, &acllen, extra_info);
		if (IS_ERR(pacl)) {
			rc = PTR_ERR(pacl);
			cifs_dbg(VFS, "%s: error %zd getting sec desc\n",
				 __func__, rc);
		} else {
			if (value) {
				if (acllen > size)
					acllen = -ERANGE;
				else
					memcpy(value, pacl, acllen);
			}
			rc = acllen;
			kfree(pacl);
		}
		break;
	}
	}

	/* We could add an additional check for streams ie
	    if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to
		returns as xattrs */

	if (rc == -EINVAL)
		rc = -EOPNOTSUPP;

out:
	free_dentry_path(page);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

ssize_t cifs_listxattr(struct dentry *direntry, char *data, size_t buf_size)
{
	ssize_t rc = -EOPNOTSUPP;
	unsigned int xid;
	struct cifs_sb_info *cifs_sb = CIFS_SB(direntry->d_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *pTcon;
	const char *full_path;
	void *page;

	if (unlikely(cifs_forced_shutdown(cifs_sb)))
		return -EIO;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
		return -EOPNOTSUPP;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	pTcon = tlink_tcon(tlink);

	xid = get_xid();
	page = alloc_dentry_path();

	full_path = build_path_from_dentry(direntry, page);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		goto list_ea_exit;
	}
	/* return dos attributes as pseudo xattr */
	/* return alt name if available as pseudo attr */

	/* if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to
		returns as xattrs */

	if (pTcon->ses->server->ops->query_all_EAs)
		rc = pTcon->ses->server->ops->query_all_EAs(xid, pTcon,
				full_path, NULL, data, buf_size, cifs_sb);
list_ea_exit:
	free_dentry_path(page);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

static const struct xattr_handler cifs_user_xattr_handler = {
	.prefix = XATTR_USER_PREFIX,
	.flags = XATTR_USER,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

/* os2.* attributes are treated like user.* attributes */
static const struct xattr_handler cifs_os2_xattr_handler = {
	.prefix = XATTR_OS2_PREFIX,
	.flags = XATTR_USER,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

static const struct xattr_handler cifs_cifs_acl_xattr_handler = {
	.name = CIFS_XATTR_CIFS_ACL,
	.flags = XATTR_CIFS_ACL,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

/*
 * Although this is just an alias for the above, need to move away from
 * confusing users and using the 20 year old term 'cifs' when it is no
 * longer secure and was replaced by SMB2/SMB3 a long time ago, and
 * SMB3 and later are highly secure.
 */
static const struct xattr_handler smb3_acl_xattr_handler = {
	.name = SMB3_XATTR_CIFS_ACL,
	.flags = XATTR_CIFS_ACL,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

static const struct xattr_handler cifs_cifs_ntsd_xattr_handler = {
	.name = CIFS_XATTR_CIFS_NTSD,
	.flags = XATTR_CIFS_NTSD,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

/*
 * Although this is just an alias for the above, need to move away from
 * confusing users and using the 20 year old term 'cifs' when it is no
 * longer secure and was replaced by SMB2/SMB3 a long time ago, and
 * SMB3 and later are highly secure.
 */
static const struct xattr_handler smb3_ntsd_xattr_handler = {
	.name = SMB3_XATTR_CIFS_NTSD,
	.flags = XATTR_CIFS_NTSD,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

static const struct xattr_handler cifs_cifs_ntsd_full_xattr_handler = {
	.name = CIFS_XATTR_CIFS_NTSD_FULL,
	.flags = XATTR_CIFS_NTSD_FULL,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

/*
 * Although this is just an alias for the above, need to move away from
 * confusing users and using the 20 year old term 'cifs' when it is no
 * longer secure and was replaced by SMB2/SMB3 a long time ago, and
 * SMB3 and later are highly secure.
 */
static const struct xattr_handler smb3_ntsd_full_xattr_handler = {
	.name = SMB3_XATTR_CIFS_NTSD_FULL,
	.flags = XATTR_CIFS_NTSD_FULL,
	.get = cifs_xattr_get,
	.set = cifs_xattr_set,
};

const struct xattr_handler * const cifs_xattr_handlers[] = {
	&cifs_user_xattr_handler,
	&cifs_os2_xattr_handler,
	&cifs_cifs_acl_xattr_handler,
	&smb3_acl_xattr_handler, /* alias for above since avoiding "cifs" */
	&cifs_cifs_ntsd_xattr_handler,
	&smb3_ntsd_xattr_handler, /* alias for above since avoiding "cifs" */
	&cifs_cifs_ntsd_full_xattr_handler,
	&smb3_ntsd_full_xattr_handler, /* alias for above since avoiding "cifs" */
	NULL
};
