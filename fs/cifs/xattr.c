/*
 *   fs/cifs/xattr.c
 *
 *   Copyright (c) International Business Machines  Corp., 2003
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/posix_acl_xattr.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

#define MAX_EA_VALUE_SIZE 65535
#define CIFS_XATTR_DOS_ATTRIB "user.DosAttrib"
#define CIFS_XATTR_USER_PREFIX "user."
#define CIFS_XATTR_SYSTEM_PREFIX "system."
#define CIFS_XATTR_OS2_PREFIX "os2."
#define CIFS_XATTR_SECURITY_PREFIX ".security"
#define CIFS_XATTR_TRUSTED_PREFIX "trusted."
#define XATTR_TRUSTED_PREFIX_LEN  8
#define XATTR_SECURITY_PREFIX_LEN 9
/* BB need to add server (Samba e.g) support for security and trusted prefix */
  


int cifs_removexattr(struct dentry * direntry, const char * ea_name)
{
	int rc = -EOPNOTSUPP;
#ifdef CONFIG_CIFS_XATTR
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct super_block * sb;
	char * full_path;
                                                                                     
	if(direntry == NULL)
		return -EIO;
	if(direntry->d_inode == NULL)
		return -EIO;
	sb = direntry->d_inode->i_sb;
	if(sb == NULL)
		return -EIO;
	xid = GetXid();
                                                                                     
	cifs_sb = CIFS_SB(sb);
	pTcon = cifs_sb->tcon;
                                                                                     
	down(&sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(direntry);
	up(&sb->s_vfs_rename_sem);
	if(full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}
	if(ea_name == NULL) {
		cFYI(1,("Null xattr names not supported"));
	} else if(strncmp(ea_name,CIFS_XATTR_USER_PREFIX,5)
		&& (strncmp(ea_name,CIFS_XATTR_OS2_PREFIX,4))) {
		cFYI(1,("illegal xattr namespace %s (only user namespace supported)",ea_name));
		/* BB what if no namespace prefix? */
		/* Should we just pass them to server, except for
		system and perhaps security prefixes? */
	} else {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
			goto remove_ea_exit;

		ea_name+=5; /* skip past user. prefix */
		rc = CIFSSMBSetEA(xid,pTcon,full_path,ea_name,NULL,
			(__u16)0, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	}
remove_ea_exit:
	if (full_path)
		kfree(full_path);
	FreeXid(xid);
#endif
	return rc;
}

int cifs_setxattr(struct dentry * direntry, const char * ea_name,
        const void * ea_value, size_t value_size, int flags)
{
	int rc = -EOPNOTSUPP;
#ifdef CONFIG_CIFS_XATTR
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct super_block * sb;
	char * full_path;

	if(direntry == NULL)
		return -EIO;
	if(direntry->d_inode == NULL)
		return -EIO;
	sb = direntry->d_inode->i_sb;
	if(sb == NULL)
		return -EIO;
	xid = GetXid();

	cifs_sb = CIFS_SB(sb);
	pTcon = cifs_sb->tcon;

	down(&sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(direntry);
	up(&sb->s_vfs_rename_sem);
	if(full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}
	/* return dos attributes as pseudo xattr */
	/* return alt name if available as pseudo attr */

	/* if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to 
		returns as xattrs */
	if(value_size > MAX_EA_VALUE_SIZE) {
		cFYI(1,("size of EA value too large"));
		if(full_path)
			kfree(full_path);
		FreeXid(xid);
		return -EOPNOTSUPP;
	}

	if(ea_name == NULL) {
		cFYI(1,("Null xattr names not supported"));
	} else if(strncmp(ea_name,CIFS_XATTR_USER_PREFIX,5) == 0) {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
			goto set_ea_exit;
		if(strncmp(ea_name,CIFS_XATTR_DOS_ATTRIB,14) == 0) {
			cFYI(1,("attempt to set cifs inode metadata"));
		}
		ea_name += 5; /* skip past user. prefix */
		rc = CIFSSMBSetEA(xid,pTcon,full_path,ea_name,ea_value,
			(__u16)value_size, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	} else if(strncmp(ea_name, CIFS_XATTR_OS2_PREFIX,4) == 0) {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
			goto set_ea_exit;

		ea_name += 4; /* skip past os2. prefix */
		rc = CIFSSMBSetEA(xid,pTcon,full_path,ea_name,ea_value,
			(__u16)value_size, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	} else {
		int temp; 
		temp = strncmp(ea_name,POSIX_ACL_XATTR_ACCESS,
			strlen(POSIX_ACL_XATTR_ACCESS));
		if (temp == 0) {
#ifdef CONFIG_CIFS_POSIX
			if(sb->s_flags & MS_POSIXACL)
				rc = CIFSSMBSetPosixACL(xid, pTcon,full_path,
					ea_value, (const int)value_size, 
					ACL_TYPE_ACCESS,cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags & 
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			cFYI(1,("set POSIX ACL rc %d",rc));
#else
			cFYI(1,("set POSIX ACL not supported"));
#endif
		} else if(strncmp(ea_name,POSIX_ACL_XATTR_DEFAULT,strlen(POSIX_ACL_XATTR_DEFAULT)) == 0) {
#ifdef CONFIG_CIFS_POSIX
			if(sb->s_flags & MS_POSIXACL)
				rc = CIFSSMBSetPosixACL(xid, pTcon,full_path,
					ea_value, (const int)value_size, 
					ACL_TYPE_DEFAULT, cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags & 
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			cFYI(1,("set POSIX default ACL rc %d",rc));
#else
			cFYI(1,("set default POSIX ACL not supported"));
#endif
		} else {
			cFYI(1,("illegal xattr request %s (only user namespace supported)",ea_name));
		  /* BB what if no namespace prefix? */
		  /* Should we just pass them to server, except for 
		  system and perhaps security prefixes? */
		}
	}

set_ea_exit:
	if (full_path)
		kfree(full_path);
	FreeXid(xid);
#endif
	return rc;
}

ssize_t cifs_getxattr(struct dentry * direntry, const char * ea_name,
         void * ea_value, size_t buf_size)
{
	ssize_t rc = -EOPNOTSUPP;
#ifdef CONFIG_CIFS_XATTR
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct super_block * sb;
	char * full_path;

	if(direntry == NULL)
		return -EIO;
	if(direntry->d_inode == NULL)
		return -EIO;
	sb = direntry->d_inode->i_sb;
	if(sb == NULL)
		return -EIO;

	xid = GetXid();

	cifs_sb = CIFS_SB(sb);
	pTcon = cifs_sb->tcon;

	down(&sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(direntry);
	up(&sb->s_vfs_rename_sem);
	if(full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}
	/* return dos attributes as pseudo xattr */
	/* return alt name if available as pseudo attr */
	if(ea_name == NULL) {
		cFYI(1,("Null xattr names not supported"));
	} else if(strncmp(ea_name,CIFS_XATTR_USER_PREFIX,5) == 0) {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
			goto get_ea_exit;

		if(strncmp(ea_name,CIFS_XATTR_DOS_ATTRIB,14) == 0) {
			cFYI(1,("attempt to query cifs inode metadata"));
			/* revalidate/getattr then populate from inode */
		} /* BB add else when above is implemented */
		ea_name += 5; /* skip past user. prefix */
		rc = CIFSSMBQueryEA(xid,pTcon,full_path,ea_name,ea_value,
			buf_size, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	} else if(strncmp(ea_name, CIFS_XATTR_OS2_PREFIX,4) == 0) {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_XATTR)
			goto get_ea_exit;

		ea_name += 4; /* skip past os2. prefix */
		rc = CIFSSMBQueryEA(xid,pTcon,full_path,ea_name,ea_value,
			buf_size, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	} else if(strncmp(ea_name,POSIX_ACL_XATTR_ACCESS,strlen(POSIX_ACL_XATTR_ACCESS)) == 0) {
#ifdef CONFIG_CIFS_POSIX
		if(sb->s_flags & MS_POSIXACL)
			rc = CIFSSMBGetPosixACL(xid, pTcon, full_path,
				ea_value, buf_size, ACL_TYPE_ACCESS, 
				cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags & 
					CIFS_MOUNT_MAP_SPECIAL_CHR);
#else 
		cFYI(1,("query POSIX ACL not supported yet"));
#endif /* CONFIG_CIFS_POSIX */
	} else if(strncmp(ea_name,POSIX_ACL_XATTR_DEFAULT,strlen(POSIX_ACL_XATTR_DEFAULT)) == 0) {
#ifdef CONFIG_CIFS_POSIX
		if(sb->s_flags & MS_POSIXACL)
			rc = CIFSSMBGetPosixACL(xid, pTcon, full_path,
				ea_value, buf_size, ACL_TYPE_DEFAULT, 
				cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags & 
					CIFS_MOUNT_MAP_SPECIAL_CHR);
#else 
		cFYI(1,("query POSIX default ACL not supported yet"));
#endif
	} else if(strncmp(ea_name,
		  CIFS_XATTR_TRUSTED_PREFIX,XATTR_TRUSTED_PREFIX_LEN) == 0) {
		cFYI(1,("Trusted xattr namespace not supported yet"));
	} else if(strncmp(ea_name,
		  CIFS_XATTR_SECURITY_PREFIX,XATTR_SECURITY_PREFIX_LEN) == 0) {
		cFYI(1,("Security xattr namespace not supported yet"));
	} else {
		cFYI(1,("illegal xattr name request %s (only user namespace supported)",ea_name));
	}

	/* We could add an additional check for streams ie 
	    if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to 
		returns as xattrs */

	if(rc == -EINVAL)
		rc = -EOPNOTSUPP; 

get_ea_exit:
	if (full_path)
		kfree(full_path);
	FreeXid(xid);
#endif
	return rc;
}

ssize_t cifs_listxattr(struct dentry * direntry, char * data, size_t buf_size)
{
	ssize_t rc = -EOPNOTSUPP;
#ifdef CONFIG_CIFS_XATTR
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct super_block * sb;
	char * full_path;

	if(direntry == NULL)
		return -EIO;
	if(direntry->d_inode == NULL)
		return -EIO;
	sb = direntry->d_inode->i_sb;
	if(sb == NULL)
		return -EIO;
	xid = GetXid();

	cifs_sb = CIFS_SB(sb);
	pTcon = cifs_sb->tcon;

	down(&sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(direntry);
	up(&sb->s_vfs_rename_sem);
	if(full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}
	/* return dos attributes as pseudo xattr */
	/* return alt name if available as pseudo attr */

	/* if proc/fs/cifs/streamstoxattr is set then
		search server for EAs or streams to 
		returns as xattrs */
	rc = CIFSSMBQAllEAs(xid,pTcon,full_path,data,buf_size,
				cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags & 
					CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
#endif
	return rc;
}
