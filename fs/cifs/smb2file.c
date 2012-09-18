/*
 *   fs/cifs/smb2file.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *   Author(s): Steve French (sfrench@us.ibm.com),
 *              Pavel Shilovsky ((pshilovsky@samba.org) 2012
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
#include "smb2proto.h"

int
smb2_open_file(const unsigned int xid, struct cifs_tcon *tcon, const char *path,
	       int disposition, int desired_access, int create_options,
	       struct cifs_fid *fid, __u32 *oplock, FILE_ALL_INFO *buf,
	       struct cifs_sb_info *cifs_sb)
{
	int rc;
	__le16 *smb2_path;
	struct smb2_file_all_info *smb2_data = NULL;

	smb2_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (smb2_path == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	smb2_data = kzalloc(sizeof(struct smb2_file_all_info) + MAX_NAME * 2,
			    GFP_KERNEL);
	if (smb2_data == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	desired_access |= FILE_READ_ATTRIBUTES;

	rc = SMB2_open(xid, tcon, smb2_path, &fid->persistent_fid,
		       &fid->volatile_fid, desired_access, disposition,
		       0, 0, smb2_data);
	if (rc)
		goto out;

	if (buf) {
		/* open response does not have IndexNumber field - get it */
		rc = SMB2_get_srv_num(xid, tcon, fid->persistent_fid,
				      fid->volatile_fid,
				      &smb2_data->IndexNumber);
		if (rc) {
			/* let get_inode_info disable server inode numbers */
			smb2_data->IndexNumber = 0;
			rc = 0;
		}
		move_smb2_info_to_cifs(buf, smb2_data);
	}

out:
	*oplock = 0;
	kfree(smb2_data);
	kfree(smb2_path);
	return rc;
}
