// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *   Author(s): Steve French (sfrench@us.ibm.com),
 *              Pavel Shilovsky ((pshilovsky@samba.org) 2012
 *
 */
#include <linux/fs.h>
#include <linux/filelock.h>
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
#include "smb2status.h"

static struct smb2_symlink_err_rsp *symlink_data(const struct kvec *iov)
{
	struct smb2_err_rsp *err = iov->iov_base;
	struct smb2_symlink_err_rsp *sym = ERR_PTR(-EINVAL);
	u32 len;

	if (err->ErrorContextCount) {
		struct smb2_error_context_rsp *p, *end;

		len = (u32)err->ErrorContextCount * (offsetof(struct smb2_error_context_rsp,
							      ErrorContextData) +
						     sizeof(struct smb2_symlink_err_rsp));
		if (le32_to_cpu(err->ByteCount) < len || iov->iov_len < len + sizeof(*err) + 1)
			return ERR_PTR(-EINVAL);

		p = (struct smb2_error_context_rsp *)err->ErrorData;
		end = (struct smb2_error_context_rsp *)((u8 *)err + iov->iov_len);
		do {
			if (le32_to_cpu(p->ErrorId) == SMB2_ERROR_ID_DEFAULT) {
				sym = (struct smb2_symlink_err_rsp *)&p->ErrorContextData;
				break;
			}
			cifs_dbg(FYI, "%s: skipping unhandled error context: 0x%x\n",
				 __func__, le32_to_cpu(p->ErrorId));

			len = ALIGN(le32_to_cpu(p->ErrorDataLength), 8);
			p = (struct smb2_error_context_rsp *)((u8 *)&p->ErrorContextData + len);
		} while (p < end);
	} else if (le32_to_cpu(err->ByteCount) >= sizeof(*sym) &&
		   iov->iov_len >= SMB2_SYMLINK_STRUCT_SIZE) {
		sym = (struct smb2_symlink_err_rsp *)err->ErrorData;
	}

	if (!IS_ERR(sym) && (le32_to_cpu(sym->SymLinkErrorTag) != SYMLINK_ERROR_TAG ||
			     le32_to_cpu(sym->ReparseTag) != IO_REPARSE_TAG_SYMLINK))
		sym = ERR_PTR(-EINVAL);

	return sym;
}

int smb2_parse_symlink_response(struct cifs_sb_info *cifs_sb, const struct kvec *iov, char **path)
{
	struct smb2_symlink_err_rsp *sym;
	unsigned int sub_offs, sub_len;
	unsigned int print_offs, print_len;
	char *s;

	if (!cifs_sb || !iov || !iov->iov_base || !iov->iov_len || !path)
		return -EINVAL;

	sym = symlink_data(iov);
	if (IS_ERR(sym))
		return PTR_ERR(sym);

	sub_len = le16_to_cpu(sym->SubstituteNameLength);
	sub_offs = le16_to_cpu(sym->SubstituteNameOffset);
	print_len = le16_to_cpu(sym->PrintNameLength);
	print_offs = le16_to_cpu(sym->PrintNameOffset);

	if (iov->iov_len < SMB2_SYMLINK_STRUCT_SIZE + sub_offs + sub_len ||
	    iov->iov_len < SMB2_SYMLINK_STRUCT_SIZE + print_offs + print_len)
		return -EINVAL;

	s = cifs_strndup_from_utf16((char *)sym->PathBuffer + sub_offs, sub_len, true,
				    cifs_sb->local_nls);
	if (!s)
		return -ENOMEM;
	convert_delimiter(s, '/');
	cifs_dbg(FYI, "%s: symlink target: %s\n", __func__, s);

	*path = s;
	return 0;
}

int smb2_open_file(const unsigned int xid, struct cifs_open_parms *oparms, __u32 *oplock, void *buf)
{
	int rc;
	__le16 *smb2_path;
	__u8 smb2_oplock;
	struct cifs_open_info_data *data = buf;
	struct smb2_file_all_info file_info = {};
	struct smb2_file_all_info *smb2_data = data ? &file_info : NULL;
	struct kvec err_iov = {};
	int err_buftype = CIFS_NO_BUFFER;
	struct cifs_fid *fid = oparms->fid;
	struct network_resiliency_req nr_ioctl_req;

	smb2_path = cifs_convert_path_to_utf16(oparms->path, oparms->cifs_sb);
	if (smb2_path == NULL)
		return -ENOMEM;

	oparms->desired_access |= FILE_READ_ATTRIBUTES;
	smb2_oplock = SMB2_OPLOCK_LEVEL_BATCH;

	rc = SMB2_open(xid, oparms, smb2_path, &smb2_oplock, smb2_data, NULL, &err_iov,
		       &err_buftype);
	if (rc && data) {
		struct smb2_hdr *hdr = err_iov.iov_base;

		if (unlikely(!err_iov.iov_base || err_buftype == CIFS_NO_BUFFER))
			goto out;
		if (hdr->Status == STATUS_STOPPED_ON_SYMLINK) {
			rc = smb2_parse_symlink_response(oparms->cifs_sb, &err_iov,
							 &data->symlink_target);
			if (!rc) {
				memset(smb2_data, 0, sizeof(*smb2_data));
				oparms->create_options |= OPEN_REPARSE_POINT;
				rc = SMB2_open(xid, oparms, smb2_path, &smb2_oplock, smb2_data,
					       NULL, NULL, NULL);
				oparms->create_options &= ~OPEN_REPARSE_POINT;
			}
		}
	}

	if (rc)
		goto out;

	if (oparms->tcon->use_resilient) {
		/* default timeout is 0, servers pick default (120 seconds) */
		nr_ioctl_req.Timeout =
			cpu_to_le32(oparms->tcon->handle_timeout);
		nr_ioctl_req.Reserved = 0;
		rc = SMB2_ioctl(xid, oparms->tcon, fid->persistent_fid,
			fid->volatile_fid, FSCTL_LMR_REQUEST_RESILIENCY,
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

	if (smb2_data) {
		/* if open response does not have IndexNumber field - get it */
		if (smb2_data->IndexNumber == 0) {
			rc = SMB2_get_srv_num(xid, oparms->tcon,
				      fid->persistent_fid,
				      fid->volatile_fid,
				      &smb2_data->IndexNumber);
			if (rc) {
				/*
				 * let get_inode_info disable server inode
				 * numbers
				 */
				smb2_data->IndexNumber = 0;
				rc = 0;
			}
		}
		memcpy(&data->fi, smb2_data, sizeof(data->fi));
	}

	*oplock = smb2_oplock;
out:
	free_rsp_buf(err_buftype, err_iov.iov_base);
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

	cifs_down_write(&cinode->lock_sem);
	list_for_each_entry_safe(li, tmp, &cfile->llist->locks, llist) {
		if (flock->fl_start > li->offset ||
		    (flock->fl_start + length) <
		    (li->offset + li->length))
			continue;
		if (current->tgid != li->pid)
			/*
			 * flock and OFD lock are associated with an open
			 * file description, not the process.
			 */
			if (!(flock->c.flc_flags & (FL_FLOCK | FL_OFDLCK)))
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
