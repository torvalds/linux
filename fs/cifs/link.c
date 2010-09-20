/*
 *   fs/cifs/link.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
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
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "md5.h"

#define CIFS_MF_SYMLINK_LEN_OFFSET (4+1)
#define CIFS_MF_SYMLINK_MD5_OFFSET (CIFS_MF_SYMLINK_LEN_OFFSET+(4+1))
#define CIFS_MF_SYMLINK_LINK_OFFSET (CIFS_MF_SYMLINK_MD5_OFFSET+(32+1))
#define CIFS_MF_SYMLINK_LINK_MAXLEN (1024)
#define CIFS_MF_SYMLINK_FILE_SIZE \
	(CIFS_MF_SYMLINK_LINK_OFFSET + CIFS_MF_SYMLINK_LINK_MAXLEN)

#define CIFS_MF_SYMLINK_LEN_FORMAT "XSym\n%04u\n"
#define CIFS_MF_SYMLINK_MD5_FORMAT \
	"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n"
#define CIFS_MF_SYMLINK_MD5_ARGS(md5_hash) \
	md5_hash[0],  md5_hash[1],  md5_hash[2],  md5_hash[3], \
	md5_hash[4],  md5_hash[5],  md5_hash[6],  md5_hash[7], \
	md5_hash[8],  md5_hash[9],  md5_hash[10], md5_hash[11],\
	md5_hash[12], md5_hash[13], md5_hash[14], md5_hash[15]

static int
CIFSParseMFSymlink(const u8 *buf,
		   unsigned int buf_len,
		   unsigned int *_link_len,
		   char **_link_str)
{
	int rc;
	unsigned int link_len;
	const char *md5_str1;
	const char *link_str;
	struct MD5Context md5_ctx;
	u8 md5_hash[16];
	char md5_str2[34];

	if (buf_len != CIFS_MF_SYMLINK_FILE_SIZE)
		return -EINVAL;

	md5_str1 = (const char *)&buf[CIFS_MF_SYMLINK_MD5_OFFSET];
	link_str = (const char *)&buf[CIFS_MF_SYMLINK_LINK_OFFSET];

	rc = sscanf(buf, CIFS_MF_SYMLINK_LEN_FORMAT, &link_len);
	if (rc != 1)
		return -EINVAL;

	cifs_MD5_init(&md5_ctx);
	cifs_MD5_update(&md5_ctx, (const u8 *)link_str, link_len);
	cifs_MD5_final(md5_hash, &md5_ctx);

	snprintf(md5_str2, sizeof(md5_str2),
		 CIFS_MF_SYMLINK_MD5_FORMAT,
		 CIFS_MF_SYMLINK_MD5_ARGS(md5_hash));

	if (strncmp(md5_str1, md5_str2, 17) != 0)
		return -EINVAL;

	if (_link_str) {
		*_link_str = kstrndup(link_str, link_len, GFP_KERNEL);
		if (!*_link_str)
			return -ENOMEM;
	}

	*_link_len = link_len;
	return 0;
}

static int
CIFSFormatMFSymlink(u8 *buf, unsigned int buf_len, const char *link_str)
{
	unsigned int link_len;
	unsigned int ofs;
	struct MD5Context md5_ctx;
	u8 md5_hash[16];

	if (buf_len != CIFS_MF_SYMLINK_FILE_SIZE)
		return -EINVAL;

	link_len = strlen(link_str);

	if (link_len > CIFS_MF_SYMLINK_LINK_MAXLEN)
		return -ENAMETOOLONG;

	cifs_MD5_init(&md5_ctx);
	cifs_MD5_update(&md5_ctx, (const u8 *)link_str, link_len);
	cifs_MD5_final(md5_hash, &md5_ctx);

	snprintf(buf, buf_len,
		 CIFS_MF_SYMLINK_LEN_FORMAT CIFS_MF_SYMLINK_MD5_FORMAT,
		 link_len,
		 CIFS_MF_SYMLINK_MD5_ARGS(md5_hash));

	ofs = CIFS_MF_SYMLINK_LINK_OFFSET;
	memcpy(buf + ofs, link_str, link_len);

	ofs += link_len;
	if (ofs < CIFS_MF_SYMLINK_FILE_SIZE) {
		buf[ofs] = '\n';
		ofs++;
	}

	while (ofs < CIFS_MF_SYMLINK_FILE_SIZE) {
		buf[ofs] = ' ';
		ofs++;
	}

	return 0;
}

static int
CIFSCreateMFSymLink(const int xid, struct cifsTconInfo *tcon,
		    const char *fromName, const char *toName,
		    const struct nls_table *nls_codepage, int remap)
{
	int rc;
	int oplock = 0;
	__u16 netfid = 0;
	u8 *buf;
	unsigned int bytes_written = 0;

	buf = kmalloc(CIFS_MF_SYMLINK_FILE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = CIFSFormatMFSymlink(buf, CIFS_MF_SYMLINK_FILE_SIZE, toName);
	if (rc != 0) {
		kfree(buf);
		return rc;
	}

	rc = CIFSSMBOpen(xid, tcon, fromName, FILE_CREATE, GENERIC_WRITE,
			 CREATE_NOT_DIR, &netfid, &oplock, NULL,
			 nls_codepage, remap);
	if (rc != 0) {
		kfree(buf);
		return rc;
	}

	rc = CIFSSMBWrite(xid, tcon, netfid,
			  CIFS_MF_SYMLINK_FILE_SIZE /* length */,
			  0 /* offset */,
			  &bytes_written, buf, NULL, 0);
	CIFSSMBClose(xid, tcon, netfid);
	kfree(buf);
	if (rc != 0)
		return rc;

	if (bytes_written != CIFS_MF_SYMLINK_FILE_SIZE)
		return -EIO;

	return 0;
}

static int
CIFSQueryMFSymLink(const int xid, struct cifsTconInfo *tcon,
		   const unsigned char *searchName, char **symlinkinfo,
		   const struct nls_table *nls_codepage, int remap)
{
	int rc;
	int oplock = 0;
	__u16 netfid = 0;
	u8 *buf;
	char *pbuf;
	unsigned int bytes_read = 0;
	int buf_type = CIFS_NO_BUFFER;
	unsigned int link_len = 0;
	FILE_ALL_INFO file_info;

	rc = CIFSSMBOpen(xid, tcon, searchName, FILE_OPEN, GENERIC_READ,
			 CREATE_NOT_DIR, &netfid, &oplock, &file_info,
			 nls_codepage, remap);
	if (rc != 0)
		return rc;

	if (file_info.EndOfFile != CIFS_MF_SYMLINK_FILE_SIZE) {
		CIFSSMBClose(xid, tcon, netfid);
		/* it's not a symlink */
		return -EINVAL;
	}

	buf = kmalloc(CIFS_MF_SYMLINK_FILE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	pbuf = buf;

	rc = CIFSSMBRead(xid, tcon, netfid,
			 CIFS_MF_SYMLINK_FILE_SIZE /* length */,
			 0 /* offset */,
			 &bytes_read, &pbuf, &buf_type);
	CIFSSMBClose(xid, tcon, netfid);
	if (rc != 0) {
		kfree(buf);
		return rc;
	}

	rc = CIFSParseMFSymlink(buf, bytes_read, &link_len, symlinkinfo);
	kfree(buf);
	if (rc != 0)
		return rc;

	return 0;
}

bool
CIFSCouldBeMFSymlink(const struct cifs_fattr *fattr)
{
	if (!(fattr->cf_mode & S_IFREG))
		/* it's not a symlink */
		return false;

	if (fattr->cf_eof != CIFS_MF_SYMLINK_FILE_SIZE)
		/* it's not a symlink */
		return false;

	return true;
}

int
CIFSCheckMFSymlink(struct cifs_fattr *fattr,
		   const unsigned char *path,
		   struct cifs_sb_info *cifs_sb, int xid)
{
	int rc;
	int oplock = 0;
	__u16 netfid = 0;
	struct cifsTconInfo *pTcon = cifs_sb_tcon(cifs_sb);
	u8 *buf;
	char *pbuf;
	unsigned int bytes_read = 0;
	int buf_type = CIFS_NO_BUFFER;
	unsigned int link_len = 0;
	FILE_ALL_INFO file_info;

	if (!CIFSCouldBeMFSymlink(fattr))
		/* it's not a symlink */
		return 0;

	rc = CIFSSMBOpen(xid, pTcon, path, FILE_OPEN, GENERIC_READ,
			 CREATE_NOT_DIR, &netfid, &oplock, &file_info,
			 cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc != 0)
		return rc;

	if (file_info.EndOfFile != CIFS_MF_SYMLINK_FILE_SIZE) {
		CIFSSMBClose(xid, pTcon, netfid);
		/* it's not a symlink */
		return 0;
	}

	buf = kmalloc(CIFS_MF_SYMLINK_FILE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	pbuf = buf;

	rc = CIFSSMBRead(xid, pTcon, netfid,
			 CIFS_MF_SYMLINK_FILE_SIZE /* length */,
			 0 /* offset */,
			 &bytes_read, &pbuf, &buf_type);
	CIFSSMBClose(xid, pTcon, netfid);
	if (rc != 0) {
		kfree(buf);
		return rc;
	}

	rc = CIFSParseMFSymlink(buf, bytes_read, &link_len, NULL);
	kfree(buf);
	if (rc == -EINVAL)
		/* it's not a symlink */
		return 0;
	if (rc != 0)
		return rc;

	/* it is a symlink */
	fattr->cf_eof = link_len;
	fattr->cf_mode &= ~S_IFMT;
	fattr->cf_mode |= S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;
	fattr->cf_dtype = DT_LNK;
	return 0;
}

int
cifs_hardlink(struct dentry *old_file, struct inode *inode,
	      struct dentry *direntry)
{
	int rc = -EACCES;
	int xid;
	char *fromName = NULL;
	char *toName = NULL;
	struct cifs_sb_info *cifs_sb_target;
	struct cifsTconInfo *pTcon;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cifs_sb_target = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb_tcon(cifs_sb_target);

/* No need to check for cross device links since server will do that
   BB note DFS case in future though (when we may have to check) */

	fromName = build_path_from_dentry(old_file);
	toName = build_path_from_dentry(direntry);
	if ((fromName == NULL) || (toName == NULL)) {
		rc = -ENOMEM;
		goto cifs_hl_exit;
	}

/*	if (cifs_sb_target->tcon->ses->capabilities & CAP_UNIX)*/
	if (pTcon->unix_ext)
		rc = CIFSUnixCreateHardLink(xid, pTcon, fromName, toName,
					    cifs_sb_target->local_nls,
					    cifs_sb_target->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	else {
		rc = CIFSCreateHardLink(xid, pTcon, fromName, toName,
					cifs_sb_target->local_nls,
					cifs_sb_target->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		if ((rc == -EIO) || (rc == -EINVAL))
			rc = -EOPNOTSUPP;
	}

	d_drop(direntry);	/* force new lookup from server of target */

	/* if source file is cached (oplocked) revalidate will not go to server
	   until the file is closed or oplock broken so update nlinks locally */
	if (old_file->d_inode) {
		cifsInode = CIFS_I(old_file->d_inode);
		if (rc == 0) {
			old_file->d_inode->i_nlink++;
/* BB should we make this contingent on superblock flag NOATIME? */
/*			old_file->d_inode->i_ctime = CURRENT_TIME;*/
			/* parent dir timestamps will update from srv
			within a second, would it really be worth it
			to set the parent dir cifs inode time to zero
			to force revalidate (faster) for it too? */
		}
		/* if not oplocked will force revalidate to get info
		   on source file from srv */
		cifsInode->time = 0;

		/* Will update parent dir timestamps from srv within a second.
		   Would it really be worth it to set the parent dir (cifs
		   inode) time field to zero to force revalidate on parent
		   directory faster ie
			CIFS_I(inode)->time = 0;  */
	}

cifs_hl_exit:
	kfree(fromName);
	kfree(toName);
	FreeXid(xid);
	return rc;
}

void *
cifs_follow_link(struct dentry *direntry, struct nameidata *nd)
{
	struct inode *inode = direntry->d_inode;
	int rc = -ENOMEM;
	int xid;
	char *full_path = NULL;
	char *target_path = NULL;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsTconInfo *tcon = cifs_sb_tcon(cifs_sb);

	xid = GetXid();

	/*
	 * For now, we just handle symlinks with unix extensions enabled.
	 * Eventually we should handle NTFS reparse points, and MacOS
	 * symlink support. For instance...
	 *
	 * rc = CIFSSMBQueryReparseLinkInfo(...)
	 *
	 * For now, just return -EACCES when the server doesn't support posix
	 * extensions. Note that we still allow querying symlinks when posix
	 * extensions are manually disabled. We could disable these as well
	 * but there doesn't seem to be any harm in allowing the client to
	 * read them.
	 */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS)
	    && !(tcon->ses->capabilities & CAP_UNIX)) {
		rc = -EACCES;
		goto out;
	}

	full_path = build_path_from_dentry(direntry);
	if (!full_path)
		goto out;

	cFYI(1, "Full path: %s inode = 0x%p", full_path, inode);

	rc = -EACCES;
	/*
	 * First try Minshall+French Symlinks, if configured
	 * and fallback to UNIX Extensions Symlinks.
	 */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS)
		rc = CIFSQueryMFSymLink(xid, tcon, full_path, &target_path,
					cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);

	if ((rc != 0) && (tcon->ses->capabilities & CAP_UNIX))
		rc = CIFSSMBUnixQuerySymLink(xid, tcon, full_path, &target_path,
					     cifs_sb->local_nls);

	kfree(full_path);
out:
	if (rc != 0) {
		kfree(target_path);
		target_path = ERR_PTR(rc);
	}

	FreeXid(xid);
	nd_set_link(nd, target_path);
	return NULL;
}

int
cifs_symlink(struct inode *inode, struct dentry *direntry, const char *symname)
{
	int rc = -EOPNOTSUPP;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb_tcon(cifs_sb);

	full_path = build_path_from_dentry(direntry);

	if (full_path == NULL) {
		rc = -ENOMEM;
		FreeXid(xid);
		return rc;
	}

	cFYI(1, "Full path: %s", full_path);
	cFYI(1, "symname is %s", symname);

	/* BB what if DFS and this volume is on different share? BB */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS)
		rc = CIFSCreateMFSymLink(xid, pTcon, full_path, symname,
					 cifs_sb->local_nls,
					 cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	else if (pTcon->unix_ext)
		rc = CIFSUnixCreateSymLink(xid, pTcon, full_path, symname,
					   cifs_sb->local_nls);
	/* else
	   rc = CIFSCreateReparseSymLink(xid, pTcon, fromName, toName,
					cifs_sb_target->local_nls); */

	if (rc == 0) {
		if (pTcon->unix_ext)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb, xid);
		else
			rc = cifs_get_inode_info(&newinode, full_path, NULL,
						 inode->i_sb, xid, NULL);

		if (rc != 0) {
			cFYI(1, "Create symlink ok, getinodeinfo fail rc = %d",
			      rc);
		} else {
			if (pTcon->nocase)
				direntry->d_op = &cifs_ci_dentry_ops;
			else
				direntry->d_op = &cifs_dentry_ops;
			d_instantiate(direntry, newinode);
		}
	}

	kfree(full_path);
	FreeXid(xid);
	return rc;
}

void cifs_put_link(struct dentry *direntry, struct nameidata *nd, void *cookie)
{
	char *p = nd_get_link(nd);
	if (!IS_ERR(p))
		kfree(p);
}
