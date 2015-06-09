/*
 *   fs/cifs/smb2inode.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Pavel Shilovsky (pshilovsky@samba.org),
 *              Steve French (sfrench@us.ibm.com)
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
#include <linux/pagemap.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "fscache.h"
#include "smb2glob.h"
#include "smb2pdu.h"
#include "smb2proto.h"

static int
smb2_open_op_close(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifs_sb_info *cifs_sb, const char *full_path,
		   __u32 desired_access, __u32 create_disposition,
		   __u32 create_options, void *data, int command)
{
	int rc, tmprc = 0;
	__le16 *utf16_path;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_parms oparms;
	struct cifs_fid fid;

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms.tcon = tcon;
	oparms.desired_access = desired_access;
	oparms.disposition = create_disposition;
	oparms.create_options = create_options;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL);
	if (rc) {
		kfree(utf16_path);
		return rc;
	}

	switch (command) {
	case SMB2_OP_DELETE:
		break;
	case SMB2_OP_QUERY_INFO:
		tmprc = SMB2_query_info(xid, tcon, fid.persistent_fid,
					fid.volatile_fid,
					(struct smb2_file_all_info *)data);
		break;
	case SMB2_OP_MKDIR:
		/*
		 * Directories are created through parameters in the
		 * SMB2_open() call.
		 */
		break;
	case SMB2_OP_RENAME:
		tmprc = SMB2_rename(xid, tcon, fid.persistent_fid,
				    fid.volatile_fid, (__le16 *)data);
		break;
	case SMB2_OP_HARDLINK:
		tmprc = SMB2_set_hardlink(xid, tcon, fid.persistent_fid,
					  fid.volatile_fid, (__le16 *)data);
		break;
	case SMB2_OP_SET_EOF:
		tmprc = SMB2_set_eof(xid, tcon, fid.persistent_fid,
				     fid.volatile_fid, current->tgid,
				     (__le64 *)data, false);
		break;
	case SMB2_OP_SET_INFO:
		tmprc = SMB2_set_info(xid, tcon, fid.persistent_fid,
				      fid.volatile_fid,
				      (FILE_BASIC_INFO *)data);
		break;
	default:
		cifs_dbg(VFS, "Invalid command\n");
		break;
	}

	rc = SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
	if (tmprc)
		rc = tmprc;
	kfree(utf16_path);
	return rc;
}

void
move_smb2_info_to_cifs(FILE_ALL_INFO *dst, struct smb2_file_all_info *src)
{
	memcpy(dst, src, (size_t)(&src->CurrentByteOffset) - (size_t)src);
	dst->CurrentByteOffset = src->CurrentByteOffset;
	dst->Mode = src->Mode;
	dst->AlignmentRequirement = src->AlignmentRequirement;
	dst->IndexNumber1 = 0; /* we don't use it */
}

int
smb2_query_path_info(const unsigned int xid, struct cifs_tcon *tcon,
		     struct cifs_sb_info *cifs_sb, const char *full_path,
		     FILE_ALL_INFO *data, bool *adjust_tz, bool *symlink)
{
	int rc;
	struct smb2_file_all_info *smb2_data;

	*adjust_tz = false;
	*symlink = false;

	smb2_data = kzalloc(sizeof(struct smb2_file_all_info) + PATH_MAX * 2,
			    GFP_KERNEL);
	if (smb2_data == NULL)
		return -ENOMEM;

	rc = smb2_open_op_close(xid, tcon, cifs_sb, full_path,
				FILE_READ_ATTRIBUTES, FILE_OPEN, 0,
				smb2_data, SMB2_OP_QUERY_INFO);
	if (rc == -EOPNOTSUPP) {
		*symlink = true;
		/* Failed on a symbolic link - query a reparse point info */
		rc = smb2_open_op_close(xid, tcon, cifs_sb, full_path,
					FILE_READ_ATTRIBUTES, FILE_OPEN,
					OPEN_REPARSE_POINT, smb2_data,
					SMB2_OP_QUERY_INFO);
	}
	if (rc)
		goto out;

	move_smb2_info_to_cifs(data, smb2_data);
out:
	kfree(smb2_data);
	return rc;
}

int
smb2_mkdir(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	   struct cifs_sb_info *cifs_sb)
{
	return smb2_open_op_close(xid, tcon, cifs_sb, name,
				  FILE_WRITE_ATTRIBUTES, FILE_CREATE,
				  CREATE_NOT_FILE, NULL, SMB2_OP_MKDIR);
}

void
smb2_mkdir_setinfo(struct inode *inode, const char *name,
		   struct cifs_sb_info *cifs_sb, struct cifs_tcon *tcon,
		   const unsigned int xid)
{
	FILE_BASIC_INFO data;
	struct cifsInodeInfo *cifs_i;
	u32 dosattrs;
	int tmprc;

	memset(&data, 0, sizeof(data));
	cifs_i = CIFS_I(inode);
	dosattrs = cifs_i->cifsAttrs | ATTR_READONLY;
	data.Attributes = cpu_to_le32(dosattrs);
	tmprc = smb2_open_op_close(xid, tcon, cifs_sb, name,
				   FILE_WRITE_ATTRIBUTES, FILE_CREATE,
				   CREATE_NOT_FILE, &data, SMB2_OP_SET_INFO);
	if (tmprc == 0)
		cifs_i->cifsAttrs = dosattrs;
}

int
smb2_rmdir(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	   struct cifs_sb_info *cifs_sb)
{
	return smb2_open_op_close(xid, tcon, cifs_sb, name, DELETE, FILE_OPEN,
				  CREATE_NOT_FILE | CREATE_DELETE_ON_CLOSE,
				  NULL, SMB2_OP_DELETE);
}

int
smb2_unlink(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	    struct cifs_sb_info *cifs_sb)
{
	return smb2_open_op_close(xid, tcon, cifs_sb, name, DELETE, FILE_OPEN,
				  CREATE_DELETE_ON_CLOSE | OPEN_REPARSE_POINT,
				  NULL, SMB2_OP_DELETE);
}

static int
smb2_set_path_attr(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *from_name, const char *to_name,
		   struct cifs_sb_info *cifs_sb, __u32 access, int command)
{
	__le16 *smb2_to_name = NULL;
	int rc;

	smb2_to_name = cifs_convert_path_to_utf16(to_name, cifs_sb);
	if (smb2_to_name == NULL) {
		rc = -ENOMEM;
		goto smb2_rename_path;
	}

	rc = smb2_open_op_close(xid, tcon, cifs_sb, from_name, access,
				FILE_OPEN, 0, smb2_to_name, command);
smb2_rename_path:
	kfree(smb2_to_name);
	return rc;
}

int
smb2_rename_path(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *from_name, const char *to_name,
		 struct cifs_sb_info *cifs_sb)
{
	return smb2_set_path_attr(xid, tcon, from_name, to_name, cifs_sb,
				  DELETE, SMB2_OP_RENAME);
}

int
smb2_create_hardlink(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *from_name, const char *to_name,
		     struct cifs_sb_info *cifs_sb)
{
	return smb2_set_path_attr(xid, tcon, from_name, to_name, cifs_sb,
				  FILE_READ_ATTRIBUTES, SMB2_OP_HARDLINK);
}

int
smb2_set_path_size(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *full_path, __u64 size,
		   struct cifs_sb_info *cifs_sb, bool set_alloc)
{
	__le64 eof = cpu_to_le64(size);
	return smb2_open_op_close(xid, tcon, cifs_sb, full_path,
				  FILE_WRITE_DATA, FILE_OPEN, 0, &eof,
				  SMB2_OP_SET_EOF);
}

int
smb2_set_file_info(struct inode *inode, const char *full_path,
		   FILE_BASIC_INFO *buf, const unsigned int xid)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink;
	int rc;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	rc = smb2_open_op_close(xid, tlink_tcon(tlink), cifs_sb, full_path,
				FILE_WRITE_ATTRIBUTES, FILE_OPEN, 0, buf,
				SMB2_OP_SET_INFO);
	cifs_put_tlink(tlink);
	return rc;
}
