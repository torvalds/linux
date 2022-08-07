// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   vfs operations that deal with io control
 *
 *   Copyright (C) International Business Machines  Corp., 2005,2013
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifsfs.h"
#include "cifs_ioctl.h"
#include "smb2proto.h"
#include "smb2glob.h"
#include <linux/btrfs.h>

static long cifs_ioctl_query_info(unsigned int xid, struct file *filep,
				  unsigned long p)
{
	struct inode *inode = file_inode(filep);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct dentry *dentry = filep->f_path.dentry;
	const unsigned char *path;
	void *page = alloc_dentry_path();
	__le16 *utf16_path = NULL, root_path;
	int rc = 0;

	path = build_path_from_dentry(dentry, page);
	if (IS_ERR(path)) {
		free_dentry_path(page);
		return PTR_ERR(path);
	}

	cifs_dbg(FYI, "%s %s\n", __func__, path);

	if (!path[0]) {
		root_path = 0;
		utf16_path = &root_path;
	} else {
		utf16_path = cifs_convert_path_to_utf16(path + 1, cifs_sb);
		if (!utf16_path) {
			rc = -ENOMEM;
			goto ici_exit;
		}
	}

	if (tcon->ses->server->ops->ioctl_query_info)
		rc = tcon->ses->server->ops->ioctl_query_info(
				xid, tcon, cifs_sb, utf16_path,
				filep->private_data ? 0 : 1, p);
	else
		rc = -EOPNOTSUPP;

 ici_exit:
	if (utf16_path != &root_path)
		kfree(utf16_path);
	free_dentry_path(page);
	return rc;
}

static long cifs_ioctl_copychunk(unsigned int xid, struct file *dst_file,
			unsigned long srcfd)
{
	int rc;
	struct fd src_file;
	struct inode *src_inode;

	cifs_dbg(FYI, "ioctl copychunk range\n");
	/* the destination must be opened for writing */
	if (!(dst_file->f_mode & FMODE_WRITE)) {
		cifs_dbg(FYI, "file target not open for write\n");
		return -EINVAL;
	}

	/* check if target volume is readonly and take reference */
	rc = mnt_want_write_file(dst_file);
	if (rc) {
		cifs_dbg(FYI, "mnt_want_write failed with rc %d\n", rc);
		return rc;
	}

	src_file = fdget(srcfd);
	if (!src_file.file) {
		rc = -EBADF;
		goto out_drop_write;
	}

	if (src_file.file->f_op->unlocked_ioctl != cifs_ioctl) {
		rc = -EBADF;
		cifs_dbg(VFS, "src file seems to be from a different filesystem type\n");
		goto out_fput;
	}

	src_inode = file_inode(src_file.file);
	rc = -EINVAL;
	if (S_ISDIR(src_inode->i_mode))
		goto out_fput;

	rc = cifs_file_copychunk_range(xid, src_file.file, 0, dst_file, 0,
					src_inode->i_size, 0);
	if (rc > 0)
		rc = 0;
out_fput:
	fdput(src_file);
out_drop_write:
	mnt_drop_write_file(dst_file);
	return rc;
}

static long smb_mnt_get_fsinfo(unsigned int xid, struct cifs_tcon *tcon,
				void __user *arg)
{
	int rc = 0;
	struct smb_mnt_fs_info *fsinf;

	fsinf = kzalloc(sizeof(struct smb_mnt_fs_info), GFP_KERNEL);
	if (fsinf == NULL)
		return -ENOMEM;

	fsinf->version = 1;
	fsinf->protocol_id = tcon->ses->server->vals->protocol_id;
	fsinf->device_characteristics =
			le32_to_cpu(tcon->fsDevInfo.DeviceCharacteristics);
	fsinf->device_type = le32_to_cpu(tcon->fsDevInfo.DeviceType);
	fsinf->fs_attributes = le32_to_cpu(tcon->fsAttrInfo.Attributes);
	fsinf->max_path_component =
		le32_to_cpu(tcon->fsAttrInfo.MaxPathNameComponentLength);
	fsinf->vol_serial_number = tcon->vol_serial_number;
	fsinf->vol_create_time = le64_to_cpu(tcon->vol_create_time);
	fsinf->share_flags = tcon->share_flags;
	fsinf->share_caps = le32_to_cpu(tcon->capabilities);
	fsinf->sector_flags = tcon->ss_flags;
	fsinf->optimal_sector_size = tcon->perf_sector_size;
	fsinf->max_bytes_chunk = tcon->max_bytes_chunk;
	fsinf->maximal_access = tcon->maximal_access;
	fsinf->cifs_posix_caps = le64_to_cpu(tcon->fsUnixInfo.Capability);

	if (copy_to_user(arg, fsinf, sizeof(struct smb_mnt_fs_info)))
		rc = -EFAULT;

	kfree(fsinf);
	return rc;
}

static int cifs_shutdown(struct super_block *sb, unsigned long arg)
{
	struct cifs_sb_info *sbi = CIFS_SB(sb);
	__u32 flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(flags, (__u32 __user *)arg))
		return -EFAULT;

	if (flags > CIFS_GOING_FLAGS_NOLOGFLUSH)
		return -EINVAL;

	if (cifs_forced_shutdown(sbi))
		return 0;

	cifs_dbg(VFS, "shut down requested (%d)", flags);
/*	trace_cifs_shutdown(sb, flags);*/

	/*
	 * see:
	 *   https://man7.org/linux/man-pages/man2/ioctl_xfs_goingdown.2.html
	 * for more information and description of original intent of the flags
	 */
	switch (flags) {
	/*
	 * We could add support later for default flag which requires:
	 *     "Flush all dirty data and metadata to disk"
	 * would need to call syncfs or equivalent to flush page cache for
	 * the mount and then issue fsync to server (if nostrictsync not set)
	 */
	case CIFS_GOING_FLAGS_DEFAULT:
		cifs_dbg(FYI, "shutdown with default flag not supported\n");
		return -EINVAL;
	/*
	 * FLAGS_LOGFLUSH is easy since it asks to write out metadata (not
	 * data) but metadata writes are not cached on the client, so can treat
	 * it similarly to NOLOGFLUSH
	 */
	case CIFS_GOING_FLAGS_LOGFLUSH:
	case CIFS_GOING_FLAGS_NOLOGFLUSH:
		sbi->mnt_cifs_flags |= CIFS_MOUNT_SHUTDOWN;
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

static int cifs_dump_full_key(struct cifs_tcon *tcon, struct smb3_full_key_debug_info __user *in)
{
	struct smb3_full_key_debug_info out;
	struct cifs_ses *ses;
	int rc = 0;
	bool found = false;
	u8 __user *end;

	if (!smb3_encryption_required(tcon)) {
		rc = -EOPNOTSUPP;
		goto out;
	}

	/* copy user input into our output buffer */
	if (copy_from_user(&out, in, sizeof(out))) {
		rc = -EINVAL;
		goto out;
	}

	if (!out.session_id) {
		/* if ses id is 0, use current user session */
		ses = tcon->ses;
	} else {
		/* otherwise if a session id is given, look for it in all our sessions */
		struct cifs_ses *ses_it = NULL;
		struct TCP_Server_Info *server_it = NULL;

		spin_lock(&cifs_tcp_ses_lock);
		list_for_each_entry(server_it, &cifs_tcp_ses_list, tcp_ses_list) {
			list_for_each_entry(ses_it, &server_it->smb_ses_list, smb_ses_list) {
				if (ses_it->Suid == out.session_id) {
					ses = ses_it;
					/*
					 * since we are using the session outside the crit
					 * section, we need to make sure it won't be released
					 * so increment its refcount
					 */
					ses->ses_count++;
					found = true;
					goto search_end;
				}
			}
		}
search_end:
		spin_unlock(&cifs_tcp_ses_lock);
		if (!found) {
			rc = -ENOENT;
			goto out;
		}
	}

	switch (ses->server->cipher_type) {
	case SMB2_ENCRYPTION_AES128_CCM:
	case SMB2_ENCRYPTION_AES128_GCM:
		out.session_key_length = CIFS_SESS_KEY_SIZE;
		out.server_in_key_length = out.server_out_key_length = SMB3_GCM128_CRYPTKEY_SIZE;
		break;
	case SMB2_ENCRYPTION_AES256_CCM:
	case SMB2_ENCRYPTION_AES256_GCM:
		out.session_key_length = CIFS_SESS_KEY_SIZE;
		out.server_in_key_length = out.server_out_key_length = SMB3_GCM256_CRYPTKEY_SIZE;
		break;
	default:
		rc = -EOPNOTSUPP;
		goto out;
	}

	/* check if user buffer is big enough to store all the keys */
	if (out.in_size < sizeof(out) + out.session_key_length + out.server_in_key_length
	    + out.server_out_key_length) {
		rc = -ENOBUFS;
		goto out;
	}

	out.session_id = ses->Suid;
	out.cipher_type = le16_to_cpu(ses->server->cipher_type);

	/* overwrite user input with our output */
	if (copy_to_user(in, &out, sizeof(out))) {
		rc = -EINVAL;
		goto out;
	}

	/* append all the keys at the end of the user buffer */
	end = in->data;
	if (copy_to_user(end, ses->auth_key.response, out.session_key_length)) {
		rc = -EINVAL;
		goto out;
	}
	end += out.session_key_length;

	if (copy_to_user(end, ses->smb3encryptionkey, out.server_in_key_length)) {
		rc = -EINVAL;
		goto out;
	}
	end += out.server_in_key_length;

	if (copy_to_user(end, ses->smb3decryptionkey, out.server_out_key_length)) {
		rc = -EINVAL;
		goto out;
	}

out:
	if (found)
		cifs_put_smb_ses(ses);
	return rc;
}

long cifs_ioctl(struct file *filep, unsigned int command, unsigned long arg)
{
	struct inode *inode = file_inode(filep);
	struct smb3_key_debug_info pkey_inf;
	int rc = -ENOTTY; /* strange error - but the precedent */
	unsigned int xid;
	struct cifsFileInfo *pSMBFile = filep->private_data;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	struct cifs_sb_info *cifs_sb;
	__u64	ExtAttrBits = 0;
	__u64   caps;

	xid = get_xid();

	cifs_dbg(FYI, "cifs ioctl 0x%x\n", command);
	switch (command) {
		case FS_IOC_GETFLAGS:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			caps = le64_to_cpu(tcon->fsUnixInfo.Capability);
#ifdef CONFIG_CIFS_POSIX
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
			if (CIFS_UNIX_EXTATTR_CAP & caps) {
				__u64	ExtAttrMask = 0;
				rc = CIFSGetExtAttr(xid, tcon,
						    pSMBFile->fid.netfid,
						    &ExtAttrBits, &ExtAttrMask);
				if (rc == 0)
					rc = put_user(ExtAttrBits &
						FS_FL_USER_VISIBLE,
						(int __user *)arg);
				if (rc != EOPNOTSUPP)
					break;
			}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
#endif /* CONFIG_CIFS_POSIX */
			rc = 0;
			if (CIFS_I(inode)->cifsAttrs & ATTR_COMPRESSED) {
				/* add in the compressed bit */
				ExtAttrBits = FS_COMPR_FL;
				rc = put_user(ExtAttrBits & FS_FL_USER_VISIBLE,
					      (int __user *)arg);
			}
			break;
		case FS_IOC_SETFLAGS:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			/* caps = le64_to_cpu(tcon->fsUnixInfo.Capability); */

			if (get_user(ExtAttrBits, (int __user *)arg)) {
				rc = -EFAULT;
				break;
			}

			/*
			 * if (CIFS_UNIX_EXTATTR_CAP & caps)
			 *	rc = CIFSSetExtAttr(xid, tcon,
			 *		       pSMBFile->fid.netfid,
			 *		       extAttrBits,
			 *		       &ExtAttrMask);
			 * if (rc != EOPNOTSUPP)
			 *	break;
			 */

			/* Currently only flag we can set is compressed flag */
			if ((ExtAttrBits & FS_COMPR_FL) == 0)
				break;

			/* Try to set compress flag */
			if (tcon->ses->server->ops->set_compression) {
				rc = tcon->ses->server->ops->set_compression(
							xid, tcon, pSMBFile);
				cifs_dbg(FYI, "set compress flag rc %d\n", rc);
			}
			break;
		case CIFS_IOC_COPYCHUNK_FILE:
			rc = cifs_ioctl_copychunk(xid, filep, arg);
			break;
		case CIFS_QUERY_INFO:
			rc = cifs_ioctl_query_info(xid, filep, arg);
			break;
		case CIFS_IOC_SET_INTEGRITY:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			if (tcon->ses->server->ops->set_integrity)
				rc = tcon->ses->server->ops->set_integrity(xid,
						tcon, pSMBFile);
			else
				rc = -EOPNOTSUPP;
			break;
		case CIFS_IOC_GET_MNT_INFO:
			if (pSMBFile == NULL)
				break;
			tcon = tlink_tcon(pSMBFile->tlink);
			rc = smb_mnt_get_fsinfo(xid, tcon, (void __user *)arg);
			break;
		case CIFS_ENUMERATE_SNAPSHOTS:
			if (pSMBFile == NULL)
				break;
			if (arg == 0) {
				rc = -EINVAL;
				goto cifs_ioc_exit;
			}
			tcon = tlink_tcon(pSMBFile->tlink);
			if (tcon->ses->server->ops->enum_snapshots)
				rc = tcon->ses->server->ops->enum_snapshots(xid, tcon,
						pSMBFile, (void __user *)arg);
			else
				rc = -EOPNOTSUPP;
			break;
		case CIFS_DUMP_KEY:
			/*
			 * Dump encryption keys. This is an old ioctl that only
			 * handles AES-128-{CCM,GCM}.
			 */
			if (pSMBFile == NULL)
				break;
			if (!capable(CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}

			tcon = tlink_tcon(pSMBFile->tlink);
			if (!smb3_encryption_required(tcon)) {
				rc = -EOPNOTSUPP;
				break;
			}
			pkey_inf.cipher_type =
				le16_to_cpu(tcon->ses->server->cipher_type);
			pkey_inf.Suid = tcon->ses->Suid;
			memcpy(pkey_inf.auth_key, tcon->ses->auth_key.response,
					16 /* SMB2_NTLMV2_SESSKEY_SIZE */);
			memcpy(pkey_inf.smb3decryptionkey,
			      tcon->ses->smb3decryptionkey, SMB3_SIGN_KEY_SIZE);
			memcpy(pkey_inf.smb3encryptionkey,
			      tcon->ses->smb3encryptionkey, SMB3_SIGN_KEY_SIZE);
			if (copy_to_user((void __user *)arg, &pkey_inf,
					sizeof(struct smb3_key_debug_info)))
				rc = -EFAULT;
			else
				rc = 0;
			break;
		case CIFS_DUMP_FULL_KEY:
			/*
			 * Dump encryption keys (handles any key sizes)
			 */
			if (pSMBFile == NULL)
				break;
			if (!capable(CAP_SYS_ADMIN)) {
				rc = -EACCES;
				break;
			}
			tcon = tlink_tcon(pSMBFile->tlink);
			rc = cifs_dump_full_key(tcon, (void __user *)arg);
			break;
		case CIFS_IOC_NOTIFY:
			if (!S_ISDIR(inode->i_mode)) {
				/* Notify can only be done on directories */
				rc = -EOPNOTSUPP;
				break;
			}
			cifs_sb = CIFS_SB(inode->i_sb);
			tlink = cifs_sb_tlink(cifs_sb);
			if (IS_ERR(tlink)) {
				rc = PTR_ERR(tlink);
				break;
			}
			tcon = tlink_tcon(tlink);
			if (tcon && tcon->ses->server->ops->notify) {
				rc = tcon->ses->server->ops->notify(xid,
						filep, (void __user *)arg);
				cifs_dbg(FYI, "ioctl notify rc %d\n", rc);
			} else
				rc = -EOPNOTSUPP;
			cifs_put_tlink(tlink);
			break;
		case CIFS_IOC_SHUTDOWN:
			rc = cifs_shutdown(inode->i_sb, arg);
			break;
		default:
			cifs_dbg(FYI, "unsupported ioctl\n");
			break;
	}
cifs_ioc_exit:
	free_xid(xid);
	return rc;
}
