// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Pavel Shilovsky (pshilovsky@samba.org),
 *              Steve French (sfrench@us.ibm.com)
 *
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
#include "cached_dir.h"
#include "../common/smb2status.h"

static struct reparse_data_buffer *reparse_buf_ptr(struct kvec *iov)
{
	struct reparse_data_buffer *buf;
	struct smb2_ioctl_rsp *io = iov->iov_base;
	u32 off, count, len;

	count = le32_to_cpu(io->OutputCount);
	off = le32_to_cpu(io->OutputOffset);
	if (check_add_overflow(off, count, &len) || len > iov->iov_len)
		return ERR_PTR(-EIO);

	buf = (struct reparse_data_buffer *)((u8 *)io + off);
	len = sizeof(*buf);
	if (count < len || count < le16_to_cpu(buf->ReparseDataLength) + len)
		return ERR_PTR(-EIO);
	return buf;
}

static inline __u32 file_create_options(struct dentry *dentry)
{
	struct cifsInodeInfo *ci;

	if (dentry) {
		ci = CIFS_I(d_inode(dentry));
		if (ci->cifsAttrs & ATTR_REPARSE)
			return OPEN_REPARSE_POINT;
	}
	return 0;
}

/* Parse owner and group from SMB3.1.1 POSIX query info */
static int parse_posix_sids(struct cifs_open_info_data *data,
			    struct kvec *rsp_iov)
{
	struct smb2_query_info_rsp *qi = rsp_iov->iov_base;
	unsigned int out_len = le32_to_cpu(qi->OutputBufferLength);
	unsigned int qi_len = sizeof(data->posix_fi);
	int owner_len, group_len;
	u8 *sidsbuf, *sidsbuf_end;

	if (out_len <= qi_len)
		return -EINVAL;

	sidsbuf = (u8 *)qi + le16_to_cpu(qi->OutputBufferOffset) + qi_len;
	sidsbuf_end = sidsbuf + out_len - qi_len;

	owner_len = posix_info_sid_size(sidsbuf, sidsbuf_end);
	if (owner_len == -1)
		return -EINVAL;

	memcpy(&data->posix_owner, sidsbuf, owner_len);
	group_len = posix_info_sid_size(sidsbuf + owner_len, sidsbuf_end);
	if (group_len == -1)
		return -EINVAL;

	memcpy(&data->posix_group, sidsbuf + owner_len, group_len);
	return 0;
}

struct wsl_query_ea {
	__le32	next;
	__u8	name_len;
	__u8	name[SMB2_WSL_XATTR_NAME_LEN + 1];
} __packed;

#define NEXT_OFF cpu_to_le32(sizeof(struct wsl_query_ea))

static const struct wsl_query_ea wsl_query_eas[] = {
	{ .next = NEXT_OFF, .name_len = SMB2_WSL_XATTR_NAME_LEN, .name = SMB2_WSL_XATTR_UID, },
	{ .next = NEXT_OFF, .name_len = SMB2_WSL_XATTR_NAME_LEN, .name = SMB2_WSL_XATTR_GID, },
	{ .next = NEXT_OFF, .name_len = SMB2_WSL_XATTR_NAME_LEN, .name = SMB2_WSL_XATTR_MODE, },
	{ .next = 0,        .name_len = SMB2_WSL_XATTR_NAME_LEN, .name = SMB2_WSL_XATTR_DEV, },
};

static int check_wsl_eas(struct kvec *rsp_iov)
{
	struct smb2_file_full_ea_info *ea;
	struct smb2_query_info_rsp *rsp = rsp_iov->iov_base;
	unsigned long addr;
	u32 outlen, next;
	u16 vlen;
	u8 nlen;
	u8 *end;

	outlen = le32_to_cpu(rsp->OutputBufferLength);
	if (outlen < SMB2_WSL_MIN_QUERY_EA_RESP_SIZE ||
	    outlen > SMB2_WSL_MAX_QUERY_EA_RESP_SIZE)
		return -EINVAL;

	ea = (void *)((u8 *)rsp_iov->iov_base +
		      le16_to_cpu(rsp->OutputBufferOffset));
	end = (u8 *)rsp_iov->iov_base + rsp_iov->iov_len;
	for (;;) {
		if ((u8 *)ea > end - sizeof(*ea))
			return -EINVAL;

		nlen = ea->ea_name_length;
		vlen = le16_to_cpu(ea->ea_value_length);
		if (nlen != SMB2_WSL_XATTR_NAME_LEN ||
		    (u8 *)ea + nlen + 1 + vlen > end)
			return -EINVAL;

		switch (vlen) {
		case 4:
			if (strncmp(ea->ea_data, SMB2_WSL_XATTR_UID, nlen) &&
			    strncmp(ea->ea_data, SMB2_WSL_XATTR_GID, nlen) &&
			    strncmp(ea->ea_data, SMB2_WSL_XATTR_MODE, nlen))
				return -EINVAL;
			break;
		case 8:
			if (strncmp(ea->ea_data, SMB2_WSL_XATTR_DEV, nlen))
				return -EINVAL;
			break;
		case 0:
			if (!strncmp(ea->ea_data, SMB2_WSL_XATTR_UID, nlen) ||
			    !strncmp(ea->ea_data, SMB2_WSL_XATTR_GID, nlen) ||
			    !strncmp(ea->ea_data, SMB2_WSL_XATTR_MODE, nlen) ||
			    !strncmp(ea->ea_data, SMB2_WSL_XATTR_DEV, nlen))
				break;
			fallthrough;
		default:
			return -EINVAL;
		}

		next = le32_to_cpu(ea->next_entry_offset);
		if (!next)
			break;
		if (!IS_ALIGNED(next, 4) ||
		    check_add_overflow((unsigned long)ea, next, &addr))
			return -EINVAL;
		ea = (void *)addr;
	}
	return 0;
}

/*
 * note: If cfile is passed, the reference to it is dropped here.
 * So make sure that you do not reuse cfile after return from this func.
 *
 * If passing @out_iov and @out_buftype, ensure to make them both large enough
 * (>= 3) to hold all compounded responses.  Caller is also responsible for
 * freeing them up with free_rsp_buf().
 */
static int smb2_compound_op(const unsigned int xid, struct cifs_tcon *tcon,
			    struct cifs_sb_info *cifs_sb, const char *full_path,
			    struct cifs_open_parms *oparms, struct kvec *in_iov,
			    int *cmds, int num_cmds, struct cifsFileInfo *cfile,
			    struct kvec *out_iov, int *out_buftype, struct dentry *dentry)
{

	struct smb2_create_rsp *create_rsp = NULL;
	struct smb2_query_info_rsp *qi_rsp = NULL;
	struct smb2_compound_vars *vars = NULL;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_open_info_data *idata;
	struct cifs_ses *ses = tcon->ses;
	struct reparse_data_buffer *rbuf;
	struct TCP_Server_Info *server;
	int resp_buftype[MAX_COMPOUND];
	int retries = 0, cur_sleep = 1;
	__u8 delete_pending[8] = {1,};
	struct kvec *rsp_iov, *iov;
	struct inode *inode = NULL;
	__le16 *utf16_path = NULL;
	struct smb_rqst *rqst;
	unsigned int size[2];
	struct cifs_fid fid;
	int num_rqst = 0, i;
	unsigned int len;
	int tmp_rc, rc;
	int flags = 0;
	void *data[2];

replay_again:
	/* reinitialize for possible replay */
	flags = 0;
	oplock = SMB2_OPLOCK_LEVEL_NONE;
	num_rqst = 0;
	server = cifs_pick_channel(ses);

	vars = kzalloc(sizeof(*vars), GFP_ATOMIC);
	if (vars == NULL)
		return -ENOMEM;
	rqst = &vars->rqst[0];
	rsp_iov = &vars->rsp_iov[0];

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	for (i = 0; i < ARRAY_SIZE(resp_buftype); i++)
		resp_buftype[i] = CIFS_NO_BUFFER;

	/* We already have a handle so we can skip the open */
	if (cfile)
		goto after_open;

	/* Open */
	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path) {
		rc = -ENOMEM;
		goto finished;
	}

	/* if there is an existing lease, reuse it */

	/*
	 * note: files with hardlinks cause unexpected behaviour. As per MS-SMB2,
	 * lease keys are associated with the filepath. We are maintaining lease keys
	 * with the inode on the client. If the file has hardlinks, it is possible
	 * that the lease for a file be reused for an operation on its hardlink or
	 * vice versa.
	 * As a workaround, send request using an existing lease key and if the server
	 * returns STATUS_INVALID_PARAMETER, which maps to EINVAL, send the request
	 * again without the lease.
	 */
	if (dentry) {
		inode = d_inode(dentry);
		if (CIFS_I(inode)->lease_granted && server->ops->get_lease_key) {
			oplock = SMB2_OPLOCK_LEVEL_LEASE;
			server->ops->get_lease_key(inode, &fid);
		}
	}

	vars->oparms = *oparms;
	vars->oparms.fid = &fid;

	rqst[num_rqst].rq_iov = &vars->open_iov[0];
	rqst[num_rqst].rq_nvec = SMB2_CREATE_IOV_SIZE;
	rc = SMB2_open_init(tcon, server,
			    &rqst[num_rqst], &oplock, &vars->oparms,
			    utf16_path);
	kfree(utf16_path);
	if (rc)
		goto finished;

	smb2_set_next_command(tcon, &rqst[num_rqst]);
 after_open:
	num_rqst++;
	rc = 0;

	i = 0;

	/* Skip the leading explicit OPEN operation */
	if (num_cmds > 0 && cmds[0] == SMB2_OP_OPEN_QUERY)
		i++;

	for (; i < num_cmds; i++) {
		/* Operation */
		switch (cmds[i]) {
		case SMB2_OP_QUERY_INFO:
			rqst[num_rqst].rq_iov = &vars->qi_iov;
			rqst[num_rqst].rq_nvec = 1;

			if (cfile) {
				rc = SMB2_query_info_init(tcon, server,
							  &rqst[num_rqst],
							  cfile->fid.persistent_fid,
							  cfile->fid.volatile_fid,
							  FILE_ALL_INFORMATION,
							  SMB2_O_INFO_FILE, 0,
							  sizeof(struct smb2_file_all_info) +
							  PATH_MAX * 2, 0, NULL);
			} else {
				rc = SMB2_query_info_init(tcon, server,
							  &rqst[num_rqst],
							  COMPOUND_FID,
							  COMPOUND_FID,
							  FILE_ALL_INFORMATION,
							  SMB2_O_INFO_FILE, 0,
							  sizeof(struct smb2_file_all_info) +
							  PATH_MAX * 2, 0, NULL);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_query_info_compound_enter(xid, tcon->tid,
							     ses->Suid, full_path);
			break;
		case SMB2_OP_POSIX_QUERY_INFO:
			rqst[num_rqst].rq_iov = &vars->qi_iov;
			rqst[num_rqst].rq_nvec = 1;

			if (cfile) {
				/* TBD: fix following to allow for longer SIDs */
				rc = SMB2_query_info_init(tcon, server,
							  &rqst[num_rqst],
							  cfile->fid.persistent_fid,
							  cfile->fid.volatile_fid,
							  SMB_FIND_FILE_POSIX_INFO,
							  SMB2_O_INFO_FILE, 0,
							  sizeof(struct smb311_posix_qinfo *) +
							  (PATH_MAX * 2) +
							  (sizeof(struct smb_sid) * 2), 0, NULL);
			} else {
				rc = SMB2_query_info_init(tcon, server,
							  &rqst[num_rqst],
							  COMPOUND_FID,
							  COMPOUND_FID,
							  SMB_FIND_FILE_POSIX_INFO,
							  SMB2_O_INFO_FILE, 0,
							  sizeof(struct smb311_posix_qinfo *) +
							  (PATH_MAX * 2) +
							  (sizeof(struct smb_sid) * 2), 0, NULL);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_posix_query_info_compound_enter(xid, tcon->tid,
								   ses->Suid, full_path);
			break;
		case SMB2_OP_DELETE:
			trace_smb3_delete_enter(xid, tcon->tid, ses->Suid, full_path);
			break;
		case SMB2_OP_MKDIR:
			/*
			 * Directories are created through parameters in the
			 * SMB2_open() call.
			 */
			trace_smb3_mkdir_enter(xid, tcon->tid, ses->Suid, full_path);
			break;
		case SMB2_OP_RMDIR:
			rqst[num_rqst].rq_iov = &vars->si_iov[0];
			rqst[num_rqst].rq_nvec = 1;

			size[0] = 1; /* sizeof __u8 See MS-FSCC section 2.4.11 */
			data[0] = &delete_pending[0];

			rc = SMB2_set_info_init(tcon, server,
						&rqst[num_rqst], COMPOUND_FID,
						COMPOUND_FID, current->tgid,
						FILE_DISPOSITION_INFORMATION,
						SMB2_O_INFO_FILE, 0, data, size);
			if (rc)
				goto finished;
			smb2_set_next_command(tcon, &rqst[num_rqst]);
			smb2_set_related(&rqst[num_rqst++]);
			trace_smb3_rmdir_enter(xid, tcon->tid, ses->Suid, full_path);
			break;
		case SMB2_OP_SET_EOF:
			rqst[num_rqst].rq_iov = &vars->si_iov[0];
			rqst[num_rqst].rq_nvec = 1;

			size[0] = in_iov[i].iov_len;
			data[0] = in_iov[i].iov_base;

			if (cfile) {
				rc = SMB2_set_info_init(tcon, server,
							&rqst[num_rqst],
							cfile->fid.persistent_fid,
							cfile->fid.volatile_fid,
							current->tgid,
							FILE_END_OF_FILE_INFORMATION,
							SMB2_O_INFO_FILE, 0,
							data, size);
			} else {
				rc = SMB2_set_info_init(tcon, server,
							&rqst[num_rqst],
							COMPOUND_FID,
							COMPOUND_FID,
							current->tgid,
							FILE_END_OF_FILE_INFORMATION,
							SMB2_O_INFO_FILE, 0,
							data, size);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_set_eof_enter(xid, tcon->tid, ses->Suid, full_path);
			break;
		case SMB2_OP_SET_INFO:
			rqst[num_rqst].rq_iov = &vars->si_iov[0];
			rqst[num_rqst].rq_nvec = 1;

			size[0] = in_iov[i].iov_len;
			data[0] = in_iov[i].iov_base;

			if (cfile) {
				rc = SMB2_set_info_init(tcon, server,
							&rqst[num_rqst],
							cfile->fid.persistent_fid,
							cfile->fid.volatile_fid, current->tgid,
							FILE_BASIC_INFORMATION,
							SMB2_O_INFO_FILE, 0, data, size);
			} else {
				rc = SMB2_set_info_init(tcon, server,
							&rqst[num_rqst],
							COMPOUND_FID,
							COMPOUND_FID, current->tgid,
							FILE_BASIC_INFORMATION,
							SMB2_O_INFO_FILE, 0, data, size);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_set_info_compound_enter(xid, tcon->tid,
							   ses->Suid, full_path);
			break;
		case SMB2_OP_RENAME:
			rqst[num_rqst].rq_iov = &vars->si_iov[0];
			rqst[num_rqst].rq_nvec = 2;

			len = in_iov[i].iov_len;

			vars->rename_info.ReplaceIfExists = 1;
			vars->rename_info.RootDirectory = 0;
			vars->rename_info.FileNameLength = cpu_to_le32(len);

			size[0] = sizeof(struct smb2_file_rename_info);
			data[0] = &vars->rename_info;

			size[1] = len + 2 /* null */;
			data[1] = in_iov[i].iov_base;

			if (cfile) {
				rc = SMB2_set_info_init(tcon, server,
							&rqst[num_rqst],
							cfile->fid.persistent_fid,
							cfile->fid.volatile_fid,
							current->tgid, FILE_RENAME_INFORMATION,
							SMB2_O_INFO_FILE, 0, data, size);
			} else {
				rc = SMB2_set_info_init(tcon, server,
							&rqst[num_rqst],
							COMPOUND_FID, COMPOUND_FID,
							current->tgid, FILE_RENAME_INFORMATION,
							SMB2_O_INFO_FILE, 0, data, size);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_rename_enter(xid, tcon->tid, ses->Suid, full_path);
			break;
		case SMB2_OP_HARDLINK:
			rqst[num_rqst].rq_iov = &vars->si_iov[0];
			rqst[num_rqst].rq_nvec = 2;

			len = in_iov[i].iov_len;

			vars->link_info.ReplaceIfExists = 0;
			vars->link_info.RootDirectory = 0;
			vars->link_info.FileNameLength = cpu_to_le32(len);

			size[0] = sizeof(struct smb2_file_link_info);
			data[0] = &vars->link_info;

			size[1] = len + 2 /* null */;
			data[1] = in_iov[i].iov_base;

			rc = SMB2_set_info_init(tcon, server,
						&rqst[num_rqst], COMPOUND_FID,
						COMPOUND_FID, current->tgid,
						FILE_LINK_INFORMATION,
						SMB2_O_INFO_FILE, 0, data, size);
			if (rc)
				goto finished;
			smb2_set_next_command(tcon, &rqst[num_rqst]);
			smb2_set_related(&rqst[num_rqst++]);
			trace_smb3_hardlink_enter(xid, tcon->tid, ses->Suid, full_path);
			break;
		case SMB2_OP_SET_REPARSE:
			rqst[num_rqst].rq_iov = vars->io_iov;
			rqst[num_rqst].rq_nvec = ARRAY_SIZE(vars->io_iov);

			if (cfile) {
				rc = SMB2_ioctl_init(tcon, server, &rqst[num_rqst],
						     cfile->fid.persistent_fid,
						     cfile->fid.volatile_fid,
						     FSCTL_SET_REPARSE_POINT,
						     in_iov[i].iov_base,
						     in_iov[i].iov_len, 0);
			} else {
				rc = SMB2_ioctl_init(tcon, server, &rqst[num_rqst],
						     COMPOUND_FID, COMPOUND_FID,
						     FSCTL_SET_REPARSE_POINT,
						     in_iov[i].iov_base,
						     in_iov[i].iov_len, 0);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_set_reparse_compound_enter(xid, tcon->tid,
							      ses->Suid, full_path);
			break;
		case SMB2_OP_GET_REPARSE:
			rqst[num_rqst].rq_iov = vars->io_iov;
			rqst[num_rqst].rq_nvec = ARRAY_SIZE(vars->io_iov);

			if (cfile) {
				rc = SMB2_ioctl_init(tcon, server, &rqst[num_rqst],
						     cfile->fid.persistent_fid,
						     cfile->fid.volatile_fid,
						     FSCTL_GET_REPARSE_POINT,
						     NULL, 0, CIFSMaxBufSize);
			} else {
				rc = SMB2_ioctl_init(tcon, server, &rqst[num_rqst],
						     COMPOUND_FID, COMPOUND_FID,
						     FSCTL_GET_REPARSE_POINT,
						     NULL, 0, CIFSMaxBufSize);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_get_reparse_compound_enter(xid, tcon->tid,
							      ses->Suid, full_path);
			break;
		case SMB2_OP_QUERY_WSL_EA:
			rqst[num_rqst].rq_iov = &vars->ea_iov;
			rqst[num_rqst].rq_nvec = 1;

			if (cfile) {
				rc = SMB2_query_info_init(tcon, server,
							  &rqst[num_rqst],
							  cfile->fid.persistent_fid,
							  cfile->fid.volatile_fid,
							  FILE_FULL_EA_INFORMATION,
							  SMB2_O_INFO_FILE, 0,
							  SMB2_WSL_MAX_QUERY_EA_RESP_SIZE,
							  sizeof(wsl_query_eas),
							  (void *)wsl_query_eas);
			} else {
				rc = SMB2_query_info_init(tcon, server,
							  &rqst[num_rqst],
							  COMPOUND_FID,
							  COMPOUND_FID,
							  FILE_FULL_EA_INFORMATION,
							  SMB2_O_INFO_FILE, 0,
							  SMB2_WSL_MAX_QUERY_EA_RESP_SIZE,
							  sizeof(wsl_query_eas),
							  (void *)wsl_query_eas);
			}
			if (!rc && (!cfile || num_rqst > 1)) {
				smb2_set_next_command(tcon, &rqst[num_rqst]);
				smb2_set_related(&rqst[num_rqst]);
			} else if (rc) {
				goto finished;
			}
			num_rqst++;
			trace_smb3_query_wsl_ea_compound_enter(xid, tcon->tid,
							       ses->Suid, full_path);
			break;
		default:
			cifs_dbg(VFS, "Invalid command\n");
			rc = -EINVAL;
		}
	}
	if (rc)
		goto finished;

	/* We already have a handle so we can skip the close */
	if (cfile)
		goto after_close;
	/* Close */
	flags |= CIFS_CP_CREATE_CLOSE_OP;
	rqst[num_rqst].rq_iov = &vars->close_iov;
	rqst[num_rqst].rq_nvec = 1;
	rc = SMB2_close_init(tcon, server,
			     &rqst[num_rqst], COMPOUND_FID,
			     COMPOUND_FID, false);
	smb2_set_related(&rqst[num_rqst]);
	if (rc)
		goto finished;
 after_close:
	num_rqst++;

	if (cfile) {
		if (retries)
			for (i = 1; i < num_rqst - 2; i++)
				smb2_set_replay(server, &rqst[i]);

		rc = compound_send_recv(xid, ses, server,
					flags, num_rqst - 2,
					&rqst[1], &resp_buftype[1],
					&rsp_iov[1]);
	} else {
		if (retries)
			for (i = 0; i < num_rqst; i++)
				smb2_set_replay(server, &rqst[i]);

		rc = compound_send_recv(xid, ses, server,
					flags, num_rqst,
					rqst, resp_buftype,
					rsp_iov);
	}

finished:
	num_rqst = 0;
	SMB2_open_free(&rqst[num_rqst++]);
	if (rc == -EREMCHG) {
		pr_warn_once("server share %s deleted\n", tcon->tree_name);
		tcon->need_reconnect = true;
	}

	tmp_rc = rc;

	if (rc == 0 && num_cmds > 0 && cmds[0] == SMB2_OP_OPEN_QUERY) {
		create_rsp = rsp_iov[0].iov_base;
		idata = in_iov[0].iov_base;
		idata->fi.CreationTime = create_rsp->CreationTime;
		idata->fi.LastAccessTime = create_rsp->LastAccessTime;
		idata->fi.LastWriteTime = create_rsp->LastWriteTime;
		idata->fi.ChangeTime = create_rsp->ChangeTime;
		idata->fi.Attributes = create_rsp->FileAttributes;
		idata->fi.AllocationSize = create_rsp->AllocationSize;
		idata->fi.EndOfFile = create_rsp->EndofFile;
		if (le32_to_cpu(idata->fi.NumberOfLinks) == 0)
			idata->fi.NumberOfLinks = cpu_to_le32(1); /* dummy value */
		idata->fi.DeletePending = 0;
		idata->fi.Directory = !!(le32_to_cpu(create_rsp->FileAttributes) & ATTR_DIRECTORY);

		/* smb2_parse_contexts() fills idata->fi.IndexNumber */
		rc = smb2_parse_contexts(server, &rsp_iov[0], &oparms->fid->epoch,
					 oparms->fid->lease_key, &oplock, &idata->fi, NULL);
		if (rc)
			cifs_dbg(VFS, "rc: %d parsing context of compound op\n", rc);
	}

	for (i = 0; i < num_cmds; i++) {
		char *buf = rsp_iov[i + i].iov_base;

		if (buf && resp_buftype[i + 1] != CIFS_NO_BUFFER)
			rc = server->ops->map_error(buf, false);
		else
			rc = tmp_rc;
		switch (cmds[i]) {
		case SMB2_OP_QUERY_INFO:
			idata = in_iov[i].iov_base;
			idata->contains_posix_file_info = false;
			if (rc == 0 && cfile && cfile->symlink_target) {
				idata->symlink_target = kstrdup(cfile->symlink_target, GFP_KERNEL);
				if (!idata->symlink_target)
					rc = -ENOMEM;
			}
			if (rc == 0) {
				qi_rsp = (struct smb2_query_info_rsp *)
					rsp_iov[i + 1].iov_base;
				rc = smb2_validate_and_copy_iov(
					le16_to_cpu(qi_rsp->OutputBufferOffset),
					le32_to_cpu(qi_rsp->OutputBufferLength),
					&rsp_iov[i + 1], sizeof(idata->fi), (char *)&idata->fi);
			}
			SMB2_query_info_free(&rqst[num_rqst++]);
			if (rc)
				trace_smb3_query_info_compound_err(xid,  tcon->tid,
								   ses->Suid, rc);
			else
				trace_smb3_query_info_compound_done(xid, tcon->tid,
								    ses->Suid);
			break;
		case SMB2_OP_POSIX_QUERY_INFO:
			idata = in_iov[i].iov_base;
			idata->contains_posix_file_info = true;
			if (rc == 0 && cfile && cfile->symlink_target) {
				idata->symlink_target = kstrdup(cfile->symlink_target, GFP_KERNEL);
				if (!idata->symlink_target)
					rc = -ENOMEM;
			}
			if (rc == 0) {
				qi_rsp = (struct smb2_query_info_rsp *)
					rsp_iov[i + 1].iov_base;
				rc = smb2_validate_and_copy_iov(
					le16_to_cpu(qi_rsp->OutputBufferOffset),
					le32_to_cpu(qi_rsp->OutputBufferLength),
					&rsp_iov[i + 1], sizeof(idata->posix_fi) /* add SIDs */,
					(char *)&idata->posix_fi);
			}
			if (rc == 0)
				rc = parse_posix_sids(idata, &rsp_iov[i + 1]);

			SMB2_query_info_free(&rqst[num_rqst++]);
			if (rc)
				trace_smb3_posix_query_info_compound_err(xid,  tcon->tid,
									 ses->Suid, rc);
			else
				trace_smb3_posix_query_info_compound_done(xid, tcon->tid,
									  ses->Suid);
			break;
		case SMB2_OP_DELETE:
			if (rc)
				trace_smb3_delete_err(xid, tcon->tid, ses->Suid, rc);
			else {
				/*
				 * If dentry (hence, inode) is NULL, lease break is going to
				 * take care of degrading leases on handles for deleted files.
				 */
				if (inode)
					cifs_mark_open_handles_for_deleted_file(inode, full_path);
				trace_smb3_delete_done(xid, tcon->tid, ses->Suid);
			}
			break;
		case SMB2_OP_MKDIR:
			if (rc)
				trace_smb3_mkdir_err(xid, tcon->tid, ses->Suid, rc);
			else
				trace_smb3_mkdir_done(xid, tcon->tid, ses->Suid);
			break;
		case SMB2_OP_HARDLINK:
			if (rc)
				trace_smb3_hardlink_err(xid,  tcon->tid, ses->Suid, rc);
			else
				trace_smb3_hardlink_done(xid, tcon->tid, ses->Suid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_RENAME:
			if (rc)
				trace_smb3_rename_err(xid, tcon->tid, ses->Suid, rc);
			else
				trace_smb3_rename_done(xid, tcon->tid, ses->Suid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_RMDIR:
			if (rc)
				trace_smb3_rmdir_err(xid, tcon->tid, ses->Suid, rc);
			else
				trace_smb3_rmdir_done(xid, tcon->tid, ses->Suid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_SET_EOF:
			if (rc)
				trace_smb3_set_eof_err(xid, tcon->tid, ses->Suid, rc);
			else
				trace_smb3_set_eof_done(xid, tcon->tid, ses->Suid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_SET_INFO:
			if (rc)
				trace_smb3_set_info_compound_err(xid,  tcon->tid,
								 ses->Suid, rc);
			else
				trace_smb3_set_info_compound_done(xid, tcon->tid,
								  ses->Suid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_SET_REPARSE:
			if (rc) {
				trace_smb3_set_reparse_compound_err(xid, tcon->tid,
								    ses->Suid, rc);
			} else {
				trace_smb3_set_reparse_compound_done(xid, tcon->tid,
								     ses->Suid);
			}
			SMB2_ioctl_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_GET_REPARSE:
			if (!rc) {
				iov = &rsp_iov[i + 1];
				idata = in_iov[i].iov_base;
				idata->reparse.io.iov = *iov;
				idata->reparse.io.buftype = resp_buftype[i + 1];
				idata->contains_posix_file_info = false; /* BB VERIFY */
				rbuf = reparse_buf_ptr(iov);
				if (IS_ERR(rbuf)) {
					rc = PTR_ERR(rbuf);
					trace_smb3_get_reparse_compound_err(xid, tcon->tid,
									    ses->Suid, rc);
				} else {
					idata->reparse.tag = le32_to_cpu(rbuf->ReparseTag);
					trace_smb3_get_reparse_compound_done(xid, tcon->tid,
									     ses->Suid);
				}
				memset(iov, 0, sizeof(*iov));
				resp_buftype[i + 1] = CIFS_NO_BUFFER;
			} else {
				trace_smb3_get_reparse_compound_err(xid, tcon->tid,
								    ses->Suid, rc);
			}
			SMB2_ioctl_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_QUERY_WSL_EA:
			if (!rc) {
				idata = in_iov[i].iov_base;
				idata->contains_posix_file_info = false;
				qi_rsp = rsp_iov[i + 1].iov_base;
				data[0] = (u8 *)qi_rsp + le16_to_cpu(qi_rsp->OutputBufferOffset);
				size[0] = le32_to_cpu(qi_rsp->OutputBufferLength);
				rc = check_wsl_eas(&rsp_iov[i + 1]);
				if (!rc) {
					memcpy(idata->wsl.eas, data[0], size[0]);
					idata->wsl.eas_len = size[0];
				}
			}
			if (!rc) {
				trace_smb3_query_wsl_ea_compound_done(xid, tcon->tid,
								      ses->Suid);
			} else {
				trace_smb3_query_wsl_ea_compound_err(xid, tcon->tid,
								     ses->Suid, rc);
			}
			SMB2_query_info_free(&rqst[num_rqst++]);
			break;
		}
	}
	SMB2_close_free(&rqst[num_rqst]);
	rc = tmp_rc;

	num_cmds += 2;
	if (out_iov && out_buftype) {
		memcpy(out_iov, rsp_iov, num_cmds * sizeof(*out_iov));
		memcpy(out_buftype, resp_buftype,
		       num_cmds * sizeof(*out_buftype));
	} else {
		for (i = 0; i < num_cmds; i++)
			free_rsp_buf(resp_buftype[i], rsp_iov[i].iov_base);
	}
	num_cmds -= 2; /* correct num_cmds as there could be a retry */
	kfree(vars);

	if (is_replayable_error(rc) &&
	    smb2_should_replay(tcon, &retries, &cur_sleep))
		goto replay_again;

	if (cfile)
		cifsFileInfo_put(cfile);

	return rc;
}

static int parse_create_response(struct cifs_open_info_data *data,
				 struct cifs_sb_info *cifs_sb,
				 const char *full_path,
				 const struct kvec *iov)
{
	struct smb2_create_rsp *rsp = iov->iov_base;
	bool reparse_point = false;
	u32 tag = 0;
	int rc = 0;

	switch (rsp->hdr.Status) {
	case STATUS_IO_REPARSE_TAG_NOT_HANDLED:
		reparse_point = true;
		break;
	case STATUS_STOPPED_ON_SYMLINK:
		rc = smb2_parse_symlink_response(cifs_sb, iov,
						 full_path,
						 &data->symlink_target);
		if (rc)
			return rc;
		tag = IO_REPARSE_TAG_SYMLINK;
		reparse_point = true;
		break;
	case STATUS_SUCCESS:
		reparse_point = !!(rsp->Flags & SMB2_CREATE_FLAG_REPARSEPOINT);
		break;
	}
	data->reparse_point = reparse_point;
	data->reparse.tag = tag;
	return rc;
}

/* Check only if SMB2_OP_QUERY_WSL_EA command failed in the compound chain */
static bool ea_unsupported(int *cmds, int num_cmds,
			   struct kvec *out_iov, int *out_buftype)
{
	int i;

	if (cmds[num_cmds - 1] != SMB2_OP_QUERY_WSL_EA)
		return false;

	for (i = 1; i < num_cmds - 1; i++) {
		struct smb2_hdr *hdr = out_iov[i].iov_base;

		if (out_buftype[i] == CIFS_NO_BUFFER || !hdr ||
		    hdr->Status != STATUS_SUCCESS)
			return false;
	}
	return true;
}

static inline void free_rsp_iov(struct kvec *iovs, int *buftype, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		free_rsp_buf(buftype[i], iovs[i].iov_base);
		memset(&iovs[i], 0, sizeof(*iovs));
		buftype[i] = CIFS_NO_BUFFER;
	}
}

int smb2_query_path_info(const unsigned int xid,
			 struct cifs_tcon *tcon,
			 struct cifs_sb_info *cifs_sb,
			 const char *full_path,
			 struct cifs_open_info_data *data)
{
	struct kvec in_iov[3], out_iov[5] = {};
	struct cached_fid *cfid = NULL;
	struct cifs_open_parms oparms;
	struct cifsFileInfo *cfile;
	__u32 create_options = 0;
	int out_buftype[5] = {};
	struct smb2_hdr *hdr;
	int num_cmds = 0;
	int cmds[3];
	bool islink;
	int rc, rc2;

	data->adjust_tz = false;
	data->reparse_point = false;

	/*
	 * BB TODO: Add support for using cached root handle in SMB3.1.1 POSIX.
	 * Create SMB2_query_posix_info worker function to do non-compounded
	 * query when we already have an open file handle for this. For now this
	 * is fast enough (always using the compounded version).
	 */
	if (!tcon->posix_extensions) {
		if (*full_path) {
			rc = -ENOENT;
		} else {
			rc = open_cached_dir(xid, tcon, full_path,
					     cifs_sb, false, &cfid);
		}
		/* If it is a root and its handle is cached then use it */
		if (!rc) {
			if (cfid->file_all_info_is_valid) {
				memcpy(&data->fi, &cfid->file_all_info,
				       sizeof(data->fi));
			} else {
				rc = SMB2_query_info(xid, tcon,
						     cfid->fid.persistent_fid,
						     cfid->fid.volatile_fid,
						     &data->fi);
			}
			close_cached_dir(cfid);
			return rc;
		}
		cmds[num_cmds++] = SMB2_OP_QUERY_INFO;
	} else {
		cmds[num_cmds++] = SMB2_OP_POSIX_QUERY_INFO;
	}

	in_iov[0].iov_base = data;
	in_iov[0].iov_len = sizeof(*data);
	in_iov[1] = in_iov[0];
	in_iov[2] = in_iov[0];

	cifs_get_readable_path(tcon, full_path, &cfile);
	oparms = CIFS_OPARMS(cifs_sb, tcon, full_path, FILE_READ_ATTRIBUTES,
			     FILE_OPEN, create_options, ACL_NO_MODE);
	rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
			      &oparms, in_iov, cmds, num_cmds,
			      cfile, out_iov, out_buftype, NULL);
	hdr = out_iov[0].iov_base;
	/*
	 * If first iov is unset, then SMB session was dropped or we've got a
	 * cached open file (@cfile).
	 */
	if (!hdr || out_buftype[0] == CIFS_NO_BUFFER)
		goto out;

	switch (rc) {
	case 0:
		rc = parse_create_response(data, cifs_sb, full_path, &out_iov[0]);
		break;
	case -EACCES:
		/*
		 * If SMB2_OP_QUERY_INFO (called when POSIX extensions are not used) failed with
		 * STATUS_ACCESS_DENIED then it means that caller does not have permission to
		 * open the path with FILE_READ_ATTRIBUTES access and therefore cannot issue
		 * SMB2_OP_QUERY_INFO command.
		 *
		 * There is an alternative way how to query limited information about path but still
		 * suitable for stat() syscall. SMB2 OPEN/CREATE operation returns in its successful
		 * response subset of query information.
		 *
		 * So try to open the path without FILE_READ_ATTRIBUTES but with MAXIMUM_ALLOWED
		 * access which will grant the maximum possible access to the file and the response
		 * will contain required query information for stat() syscall.
		 */

		if (tcon->posix_extensions)
			break;

		num_cmds = 1;
		cmds[0] = SMB2_OP_OPEN_QUERY;
		in_iov[0].iov_base = data;
		in_iov[0].iov_len = sizeof(*data);
		oparms = CIFS_OPARMS(cifs_sb, tcon, full_path, MAXIMUM_ALLOWED,
				     FILE_OPEN, create_options, ACL_NO_MODE);
		free_rsp_iov(out_iov, out_buftype, ARRAY_SIZE(out_iov));
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
				      &oparms, in_iov, cmds, num_cmds,
				      cfile, out_iov, out_buftype, NULL);

		hdr = out_iov[0].iov_base;
		if (!hdr || out_buftype[0] == CIFS_NO_BUFFER)
			goto out;

		if (!rc)
			rc = parse_create_response(data, cifs_sb, full_path, &out_iov[0]);
		break;
	case -EOPNOTSUPP:
		/*
		 * BB TODO: When support for special files added to Samba
		 * re-verify this path.
		 */
		rc = parse_create_response(data, cifs_sb, full_path, &out_iov[0]);
		if (rc || !data->reparse_point)
			goto out;

		/*
		 * Skip SMB2_OP_GET_REPARSE if symlink already parsed in create
		 * response.
		 */
		if (data->reparse.tag != IO_REPARSE_TAG_SYMLINK)
			cmds[num_cmds++] = SMB2_OP_GET_REPARSE;
		if (!tcon->posix_extensions)
			cmds[num_cmds++] = SMB2_OP_QUERY_WSL_EA;

		oparms = CIFS_OPARMS(cifs_sb, tcon, full_path,
				     FILE_READ_ATTRIBUTES |
				     FILE_READ_EA | SYNCHRONIZE,
				     FILE_OPEN, create_options |
				     OPEN_REPARSE_POINT, ACL_NO_MODE);
		cifs_get_readable_path(tcon, full_path, &cfile);
		free_rsp_iov(out_iov, out_buftype, ARRAY_SIZE(out_iov));
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
				      &oparms, in_iov, cmds, num_cmds,
				      cfile, out_iov, out_buftype, NULL);
		if (rc && ea_unsupported(cmds, num_cmds,
					 out_iov, out_buftype)) {
			if (data->reparse.tag != IO_REPARSE_TAG_LX_BLK &&
			    data->reparse.tag != IO_REPARSE_TAG_LX_CHR)
				rc = 0;
			else
				rc = -EOPNOTSUPP;
		}

		if (data->reparse.tag == IO_REPARSE_TAG_SYMLINK && !rc) {
			bool directory = le32_to_cpu(data->fi.Attributes) & ATTR_DIRECTORY;
			rc = smb2_fix_symlink_target_type(&data->symlink_target, directory, cifs_sb);
		}
		break;
	case -EREMOTE:
		break;
	default:
		if (hdr->Status != STATUS_OBJECT_NAME_INVALID)
			break;
		rc2 = cifs_inval_name_dfs_link_error(xid, tcon, cifs_sb,
						     full_path, &islink);
		if (rc2) {
			rc = rc2;
			goto out;
		}
		if (islink)
			rc = -EREMOTE;
	}

out:
	free_rsp_iov(out_iov, out_buftype, ARRAY_SIZE(out_iov));
	return rc;
}

int
smb2_mkdir(const unsigned int xid, struct inode *parent_inode, umode_t mode,
	   struct cifs_tcon *tcon, const char *name,
	   struct cifs_sb_info *cifs_sb)
{
	struct cifs_open_parms oparms;

	oparms = CIFS_OPARMS(cifs_sb, tcon, name, FILE_WRITE_ATTRIBUTES,
			     FILE_CREATE, CREATE_NOT_FILE, mode);
	return smb2_compound_op(xid, tcon, cifs_sb,
				name, &oparms, NULL,
				&(int){SMB2_OP_MKDIR}, 1,
				NULL, NULL, NULL, NULL);
}

void
smb2_mkdir_setinfo(struct inode *inode, const char *name,
		   struct cifs_sb_info *cifs_sb, struct cifs_tcon *tcon,
		   const unsigned int xid)
{
	struct cifs_open_parms oparms;
	FILE_BASIC_INFO data = {};
	struct cifsInodeInfo *cifs_i;
	struct cifsFileInfo *cfile;
	struct kvec in_iov;
	u32 dosattrs;
	int tmprc;

	in_iov.iov_base = &data;
	in_iov.iov_len = sizeof(data);
	cifs_i = CIFS_I(inode);
	dosattrs = cifs_i->cifsAttrs | ATTR_READONLY;
	data.Attributes = cpu_to_le32(dosattrs);
	cifs_get_writable_path(tcon, name, FIND_WR_ANY, &cfile);
	oparms = CIFS_OPARMS(cifs_sb, tcon, name, FILE_WRITE_ATTRIBUTES,
			     FILE_CREATE, CREATE_NOT_FILE, ACL_NO_MODE);
	tmprc = smb2_compound_op(xid, tcon, cifs_sb, name,
				 &oparms, &in_iov,
				 &(int){SMB2_OP_SET_INFO}, 1,
				 cfile, NULL, NULL, NULL);
	if (tmprc == 0)
		cifs_i->cifsAttrs = dosattrs;
}

int
smb2_rmdir(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	   struct cifs_sb_info *cifs_sb)
{
	struct cifs_open_parms oparms;

	drop_cached_dir_by_name(xid, tcon, name, cifs_sb);
	oparms = CIFS_OPARMS(cifs_sb, tcon, name, DELETE,
			     FILE_OPEN, CREATE_NOT_FILE, ACL_NO_MODE);
	return smb2_compound_op(xid, tcon, cifs_sb,
				name, &oparms, NULL,
				&(int){SMB2_OP_RMDIR}, 1,
				NULL, NULL, NULL, NULL);
}

int
smb2_unlink(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	    struct cifs_sb_info *cifs_sb, struct dentry *dentry)
{
	struct cifs_open_parms oparms;

	oparms = CIFS_OPARMS(cifs_sb, tcon, name,
			     DELETE, FILE_OPEN,
			     CREATE_DELETE_ON_CLOSE | OPEN_REPARSE_POINT,
			     ACL_NO_MODE);
	int rc = smb2_compound_op(xid, tcon, cifs_sb, name, &oparms,
				  NULL, &(int){SMB2_OP_DELETE}, 1,
				  NULL, NULL, NULL, dentry);
	if (rc == -EINVAL) {
		cifs_dbg(FYI, "invalid lease key, resending request without lease");
		rc = smb2_compound_op(xid, tcon, cifs_sb, name, &oparms,
				      NULL, &(int){SMB2_OP_DELETE}, 1,
				      NULL, NULL, NULL, NULL);
	}
	return rc;
}

static int smb2_set_path_attr(const unsigned int xid, struct cifs_tcon *tcon,
			      const char *from_name, const char *to_name,
			      struct cifs_sb_info *cifs_sb,
			      __u32 create_options, __u32 access,
			      int command, struct cifsFileInfo *cfile,
				  struct dentry *dentry)
{
	struct cifs_open_parms oparms;
	struct kvec in_iov;
	__le16 *smb2_to_name = NULL;
	int rc;

	smb2_to_name = cifs_convert_path_to_utf16(to_name, cifs_sb);
	if (smb2_to_name == NULL) {
		rc = -ENOMEM;
		goto smb2_rename_path;
	}
	in_iov.iov_base = smb2_to_name;
	in_iov.iov_len = 2 * UniStrnlen((wchar_t *)smb2_to_name, PATH_MAX);
	oparms = CIFS_OPARMS(cifs_sb, tcon, from_name, access, FILE_OPEN,
			     create_options, ACL_NO_MODE);
	rc = smb2_compound_op(xid, tcon, cifs_sb, from_name,
			      &oparms, &in_iov, &command, 1,
			      cfile, NULL, NULL, dentry);
smb2_rename_path:
	kfree(smb2_to_name);
	return rc;
}

int smb2_rename_path(const unsigned int xid,
		     struct cifs_tcon *tcon,
		     struct dentry *source_dentry,
		     const char *from_name, const char *to_name,
		     struct cifs_sb_info *cifs_sb)
{
	struct cifsFileInfo *cfile;
	__u32 co = file_create_options(source_dentry);

	drop_cached_dir_by_name(xid, tcon, from_name, cifs_sb);
	cifs_get_writable_path(tcon, from_name, FIND_WR_WITH_DELETE, &cfile);

	int rc = smb2_set_path_attr(xid, tcon, from_name, to_name, cifs_sb,
				  co, DELETE, SMB2_OP_RENAME, cfile, source_dentry);
	if (rc == -EINVAL) {
		cifs_dbg(FYI, "invalid lease key, resending request without lease");
		cifs_get_writable_path(tcon, from_name,
				       FIND_WR_WITH_DELETE, &cfile);
		rc = smb2_set_path_attr(xid, tcon, from_name, to_name, cifs_sb,
				  co, DELETE, SMB2_OP_RENAME, cfile, NULL);
	}
	return rc;
}

int smb2_create_hardlink(const unsigned int xid,
			 struct cifs_tcon *tcon,
			 struct dentry *source_dentry,
			 const char *from_name, const char *to_name,
			 struct cifs_sb_info *cifs_sb)
{
	__u32 co = file_create_options(source_dentry);

	return smb2_set_path_attr(xid, tcon, from_name, to_name,
				  cifs_sb, co, FILE_READ_ATTRIBUTES,
				  SMB2_OP_HARDLINK, NULL, NULL);
}

int
smb2_set_path_size(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *full_path, __u64 size,
		   struct cifs_sb_info *cifs_sb, bool set_alloc,
		   struct dentry *dentry)
{
	struct cifs_open_parms oparms;
	struct cifsFileInfo *cfile;
	struct kvec in_iov;
	__le64 eof = cpu_to_le64(size);
	int rc;

	in_iov.iov_base = &eof;
	in_iov.iov_len = sizeof(eof);
	cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);

	oparms = CIFS_OPARMS(cifs_sb, tcon, full_path, FILE_WRITE_DATA,
			     FILE_OPEN, 0, ACL_NO_MODE);
	rc = smb2_compound_op(xid, tcon, cifs_sb,
			      full_path, &oparms, &in_iov,
			      &(int){SMB2_OP_SET_EOF}, 1,
			      cfile, NULL, NULL, dentry);
	if (rc == -EINVAL) {
		cifs_dbg(FYI, "invalid lease key, resending request without lease");
		cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);
		rc = smb2_compound_op(xid, tcon, cifs_sb,
				      full_path, &oparms, &in_iov,
				      &(int){SMB2_OP_SET_EOF}, 1,
				      cfile, NULL, NULL, NULL);
	}
	return rc;
}

int
smb2_set_file_info(struct inode *inode, const char *full_path,
		   FILE_BASIC_INFO *buf, const unsigned int xid)
{
	struct cifs_open_parms oparms;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *cfile;
	struct kvec in_iov = { .iov_base = buf, .iov_len = sizeof(*buf), };
	int rc;

	if ((buf->CreationTime == 0) && (buf->LastAccessTime == 0) &&
	    (buf->LastWriteTime == 0) && (buf->ChangeTime == 0) &&
	    (buf->Attributes == 0))
		return 0; /* would be a no op, no sense sending this */

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);
	oparms = CIFS_OPARMS(cifs_sb, tcon, full_path, FILE_WRITE_ATTRIBUTES,
			     FILE_OPEN, 0, ACL_NO_MODE);
	rc = smb2_compound_op(xid, tcon, cifs_sb,
			      full_path, &oparms, &in_iov,
			      &(int){SMB2_OP_SET_INFO}, 1,
			      cfile, NULL, NULL, NULL);
	cifs_put_tlink(tlink);
	return rc;
}

struct inode *smb2_get_reparse_inode(struct cifs_open_info_data *data,
				     struct super_block *sb,
				     const unsigned int xid,
				     struct cifs_tcon *tcon,
				     const char *full_path,
				     bool directory,
				     struct kvec *reparse_iov,
				     struct kvec *xattr_iov)
{
	struct cifs_open_parms oparms;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsFileInfo *cfile;
	struct inode *new = NULL;
	int out_buftype[4] = {};
	struct kvec out_iov[4] = {};
	struct kvec in_iov[2];
	int cmds[2];
	int rc;
	int i;

	/*
	 * If server filesystem does not support reparse points then do not
	 * attempt to create reparse point. This will prevent creating unusable
	 * empty object on the server.
	 */
	if (!(le32_to_cpu(tcon->fsAttrInfo.Attributes) & FILE_SUPPORTS_REPARSE_POINTS))
		if (!tcon->posix_extensions)
			return ERR_PTR(-EOPNOTSUPP);

	oparms = CIFS_OPARMS(cifs_sb, tcon, full_path,
			     SYNCHRONIZE | DELETE |
			     FILE_READ_ATTRIBUTES |
			     FILE_WRITE_ATTRIBUTES,
			     FILE_CREATE,
			     (directory ? CREATE_NOT_FILE : CREATE_NOT_DIR) | OPEN_REPARSE_POINT,
			     ACL_NO_MODE);
	if (xattr_iov)
		oparms.ea_cctx = xattr_iov;

	cmds[0] = SMB2_OP_SET_REPARSE;
	in_iov[0] = *reparse_iov;
	in_iov[1].iov_base = data;
	in_iov[1].iov_len = sizeof(*data);

	if (tcon->posix_extensions) {
		cmds[1] = SMB2_OP_POSIX_QUERY_INFO;
		cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path, &oparms,
				      in_iov, cmds, 2, cfile, out_iov, out_buftype, NULL);
		if (!rc) {
			rc = smb311_posix_get_inode_info(&new, full_path,
							 data, sb, xid);
		}
	} else {
		cmds[1] = SMB2_OP_QUERY_INFO;
		cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path, &oparms,
				      in_iov, cmds, 2, cfile, out_iov, out_buftype, NULL);
		if (!rc) {
			rc = cifs_get_inode_info(&new, full_path,
						 data, sb, xid, NULL);
		}
	}


	/*
	 * If CREATE was successful but SMB2_OP_SET_REPARSE failed then
	 * remove the intermediate object created by CREATE. Otherwise
	 * empty object stay on the server when reparse call failed.
	 */
	if (rc &&
	    out_iov[0].iov_base != NULL && out_buftype[0] != CIFS_NO_BUFFER &&
	    ((struct smb2_hdr *)out_iov[0].iov_base)->Status == STATUS_SUCCESS &&
	    (out_iov[1].iov_base == NULL || out_buftype[1] == CIFS_NO_BUFFER ||
	     ((struct smb2_hdr *)out_iov[1].iov_base)->Status != STATUS_SUCCESS))
		smb2_unlink(xid, tcon, full_path, cifs_sb, NULL);

	for (i = 0; i < ARRAY_SIZE(out_buftype); i++)
		free_rsp_buf(out_buftype[i], out_iov[i].iov_base);

	return rc ? ERR_PTR(rc) : new;
}

int smb2_query_reparse_point(const unsigned int xid,
			     struct cifs_tcon *tcon,
			     struct cifs_sb_info *cifs_sb,
			     const char *full_path,
			     u32 *tag, struct kvec *rsp,
			     int *rsp_buftype)
{
	struct cifs_open_parms oparms;
	struct cifs_open_info_data data = {};
	struct cifsFileInfo *cfile;
	struct kvec in_iov = { .iov_base = &data, .iov_len = sizeof(data), };
	int rc;

	cifs_dbg(FYI, "%s: path: %s\n", __func__, full_path);

	cifs_get_readable_path(tcon, full_path, &cfile);
	oparms = CIFS_OPARMS(cifs_sb, tcon, full_path,
			     FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE,
			     FILE_OPEN, OPEN_REPARSE_POINT, ACL_NO_MODE);
	rc = smb2_compound_op(xid, tcon, cifs_sb,
			      full_path, &oparms, &in_iov,
			      &(int){SMB2_OP_GET_REPARSE}, 1,
			      cfile, NULL, NULL, NULL);
	if (rc)
		goto out;

	*tag = data.reparse.tag;
	*rsp = data.reparse.io.iov;
	*rsp_buftype = data.reparse.io.buftype;
	memset(&data.reparse.io.iov, 0, sizeof(data.reparse.io.iov));
	data.reparse.io.buftype = CIFS_NO_BUFFER;
out:
	cifs_free_open_info(&data);
	return rc;
}
