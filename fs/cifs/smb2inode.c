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
		   __u32 file_attributes, __u32 create_options,
		   void *data, int command)
{
	int rc, tmprc = 0;
	u64 persistent_fid, volatile_fid;
	__le16 *utf16_path;

	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	rc = SMB2_open(xid, tcon, utf16_path, &persistent_fid, &volatile_fid,
		       desired_access, create_disposition, file_attributes,
		       create_options);
	if (rc) {
		kfree(utf16_path);
		return rc;
	}

	switch (command) {
	case SMB2_OP_DELETE:
		break;
	case SMB2_OP_QUERY_INFO:
		tmprc = SMB2_query_info(xid, tcon, persistent_fid,
					volatile_fid,
					(struct smb2_file_all_info *)data);
		break;
	case SMB2_OP_MKDIR:
		/*
		 * Directories are created through parameters in the
		 * SMB2_open() call.
		 */
		break;
	default:
		cERROR(1, "Invalid command");
		break;
	}

	rc = SMB2_close(xid, tcon, persistent_fid, volatile_fid);
	if (tmprc)
		rc = tmprc;
	kfree(utf16_path);
	return rc;
}

static void
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
		     FILE_ALL_INFO *data, bool *adjust_tz)
{
	int rc;
	struct smb2_file_all_info *smb2_data;

	*adjust_tz = false;

	smb2_data = kzalloc(sizeof(struct smb2_file_all_info) + MAX_NAME * 2,
			    GFP_KERNEL);
	if (smb2_data == NULL)
		return -ENOMEM;

	rc = smb2_open_op_close(xid, tcon, cifs_sb, full_path,
				FILE_READ_ATTRIBUTES, FILE_OPEN, 0, 0,
				smb2_data, SMB2_OP_QUERY_INFO);
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
				  FILE_WRITE_ATTRIBUTES, FILE_CREATE, 0,
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
				   FILE_WRITE_ATTRIBUTES, FILE_CREATE, 0,
				   CREATE_NOT_FILE, &data, SMB2_OP_SET_INFO);
	if (tmprc == 0)
		cifs_i->cifsAttrs = dosattrs;
}

int
smb2_rmdir(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	   struct cifs_sb_info *cifs_sb)
{
	return smb2_open_op_close(xid, tcon, cifs_sb, name, DELETE, FILE_OPEN,
				  0, CREATE_NOT_FILE | CREATE_DELETE_ON_CLOSE,
				  NULL, SMB2_OP_DELETE);
}
