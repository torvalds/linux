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
smb2_open_file(const unsigned int xid, struct cifs_open_parms *oparms,
	       __u32 *oplock, FILE_ALL_INFO *buf)
{
	int rc;
	__le16 *smb2_path;
	struct smb2_file_all_info *smb2_data = NULL;
	__u8 smb2_oplock;
	struct cifs_fid *fid = oparms->fid;
	struct network_resiliency_req nr_ioctl_req;

	smb2_path = cifs_convert_path_to_utf16(oparms->path, oparms->cifs_sb);
	if (smb2_path == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	smb2_data = kzalloc(sizeof(struct smb2_file_all_info) + PATH_MAX * 2,
			    GFP_KERNEL);
	if (smb2_data == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	oparms->desired_access |= FILE_READ_ATTRIBUTES;
	smb2_oplock = SMB2_OPLOCK_LEVEL_BATCH;

	rc = SMB2_open(xid, oparms, smb2_path, &smb2_oplock, smb2_data, NULL,
		       NULL);
	if (rc)
		goto out;


	 if (oparms->tcon->use_resilient) {
		/* default timeout is 0, servers pick default (120 seconds) */
		nr_ioctl_req.Timeout =
			cpu_to_le32(oparms->tcon->handle_timeout);
		nr_ioctl_req.Reserved = 0;
		rc = SMB2_ioctl(xid, oparms->tcon, fid->persistent_fid,
			fid->volatile_fid, FSCTL_LMR_REQUEST_RESILIENCY,
			true /* is_fsctl */,
			(char *)&nr_ioctl_req, sizeof(nr_ioctl_req),
			CIFSMaxBufSize, NULL, NULL /* no return info */);
		if (rc == -EOPNOTSUPP) {
			cifs_dbg(VFS,
			     "resiliency not supported by server, disabling\n");
			oparms->tcon->use_resilient = false;
		} else if (rc)
			cifs_dbg(FYI, "error %d setting resiliency\n", rc);

		rc = 0;
	}

	if (buf) {
		/* open response does not have IndexNumber field - get it */
		rc = SMB2_get_srv_num(xid, oparms->tcon, fid->persistent_fid,
				      fid->volatile_fid,
				      &smb2_data->IndexNumber);
		if (rc) {
			/* let get_inode_info disable server inode numbers */
			smb2_data->IndexNumber = 0;
			rc = 0;
		}
		move_smb2_info_to_cifs(buf, smb2_data);
	}

	*oplock = smb2_oplock;
out:
	kfree(smb2_data);
	kfree(smb2_path);
	return rc;
}

int
smb2_unlock_range(struct cifsFileInfo *cfile, struct file_lock *flock,
		  const unsigned int xid)
{
	int rc = 0, stored_rc;
	unsigned int max_num, num = 0, max_buf;
	struct smb2_lock_element *buf, *cur;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifsLockInfo *li, *tmp;
	__u64 length = 1 + flock->fl_end - flock->fl_start;
	struct list_head tmp_llist;

	INIT_LIST_HEAD(&tmp_llist);

	/*
	 * Accessing maxBuf is racy with cifs_reconnect - need to store value
	 * and check it before using.
	 */
	max_buf = tcon->ses->server->maxBuf;
	if (max_buf < sizeof(struct smb2_lock_element))
		return -EINVAL;

	BUILD_BUG_ON(sizeof(struct smb2_lock_element) > PAGE_SIZE);
	max_buf = min_t(unsigned int, max_buf, PAGE_SIZE);
	max_num = max_buf / sizeof(struct smb2_lock_element);
	buf = kcalloc(max_num, sizeof(struct smb2_lock_element), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	cur = buf;

	down_write(&cinode->lock_sem);
	list_for_each_entry_safe(li, tmp, &cfile->llist->locks, llist) {
		if (flock->fl_start > li->offset ||
		    (flock->fl_start + length) <
		    (li->offset + li->length))
			continue;
		if (current->tgid != li->pid)
			continue;
		if (cinode->can_cache_brlcks) {
			/*
			 * We can cache brlock requests - simply remove a lock
			 * from the file's list.
			 */
			list_del(&li->llist);
			cifs_del_lock_waiters(li);
			kfree(li);
			continue;
		}
		cur->Length = cpu_to_le64(li->length);
		cur->Offset = cpu_to_le64(li->offset);
		cur->Flags = cpu_to_le32(SMB2_LOCKFLAG_UNLOCK);
		/*
		 * We need to save a lock here to let us add it again to the
		 * file's list if the unlock range request fails on the server.
		 */
		list_move(&li->llist, &tmp_llist);
		if (++num == max_num) {
			stored_rc = smb2_lockv(xid, tcon,
					       cfile->fid.persistent_fid,
					       cfile->fid.volatile_fid,
					       current->tgid, num, buf);
			if (stored_rc) {
				/*
				 * We failed on the unlock range request - add
				 * all locks from the tmp list to the head of
				 * the file's list.
				 */
				cifs_move_llist(&tmp_llist,
						&cfile->llist->locks);
				rc = stored_rc;
			} else
				/*
				 * The unlock range request succeed - free the
				 * tmp list.
				 */
				cifs_free_llist(&tmp_llist);
			cur = buf;
			num = 0;
		} else
			cur++;
	}
	if (num) {
		stored_rc = smb2_lockv(xid, tcon, cfile->fid.persistent_fid,
				       cfile->fid.volatile_fid, current->tgid,
				       num, buf);
		if (stored_rc) {
			cifs_move_llist(&tmp_llist, &cfile->llist->locks);
			rc = stored_rc;
		} else
			cifs_free_llist(&tmp_llist);
	}
	up_write(&cinode->lock_sem);

	kfree(buf);
	return rc;
}

static int
smb2_push_mand_fdlocks(struct cifs_fid_locks *fdlocks, const unsigned int xid,
		       struct smb2_lock_element *buf, unsigned int max_num)
{
	int rc = 0, stored_rc;
	struct cifsFileInfo *cfile = fdlocks->cfile;
	struct cifsLockInfo *li;
	unsigned int num = 0;
	struct smb2_lock_element *cur = buf;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);

	list_for_each_entry(li, &fdlocks->locks, llist) {
		cur->Length = cpu_to_le64(li->length);
		cur->Offset = cpu_to_le64(li->offset);
		cur->Flags = cpu_to_le32(li->type |
						SMB2_LOCKFLAG_FAIL_IMMEDIATELY);
		if (++num == max_num) {
			stored_rc = smb2_lockv(xid, tcon,
					       cfile->fid.persistent_fid,
					       cfile->fid.volatile_fid,
					       current->tgid, num, buf);
			if (stored_rc)
				rc = stored_rc;
			cur = buf;
			num = 0;
		} else
			cur++;
	}
	if (num) {
		stored_rc = smb2_lockv(xid, tcon,
				       cfile->fid.persistent_fid,
				       cfile->fid.volatile_fid,
				       current->tgid, num, buf);
		if (stored_rc)
			rc = stored_rc;
	}

	return rc;
}

int
smb2_push_mandatory_locks(struct cifsFileInfo *cfile)
{
	int rc = 0, stored_rc;
	unsigned int xid;
	unsigned int max_num, max_buf;
	struct smb2_lock_element *buf;
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifs_fid_locks *fdlocks;

	xid = get_xid();

	/*
	 * Accessing maxBuf is racy with cifs_reconnect - need to store value
	 * and check it for zero before using.
	 */
	max_buf = tlink_tcon(cfile->tlink)->ses->server->maxBuf;
	if (max_buf < sizeof(struct smb2_lock_element)) {
		free_xid(xid);
		return -EINVAL;
	}

	BUILD_BUG_ON(sizeof(struct smb2_lock_element) > PAGE_SIZE);
	max_buf = min_t(unsigned int, max_buf, PAGE_SIZE);
	max_num = max_buf / sizeof(struct smb2_lock_element);
	buf = kcalloc(max_num, sizeof(struct smb2_lock_element), GFP_KERNEL);
	if (!buf) {
		free_xid(xid);
		return -ENOMEM;
	}

	list_for_each_entry(fdlocks, &cinode->llist, llist) {
		stored_rc = smb2_push_mand_fdlocks(fdlocks, xid, buf, max_num);
		if (stored_rc)
			rc = stored_rc;
	}

	kfree(buf);
	free_xid(xid);
	return rc;
}
