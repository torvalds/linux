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
#include "smb2status.h"

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
			    __u32 desired_access, __u32 create_disposition,
			    __u32 create_options, umode_t mode, struct kvec *in_iov,
			    int *cmds, int num_cmds, struct cifsFileInfo *cfile,
			    __u8 **extbuf, size_t *extbuflen,
			    struct kvec *out_iov, int *out_buftype)
{
	struct smb2_compound_vars *vars = NULL;
	struct kvec *rsp_iov;
	struct smb_rqst *rqst;
	int rc;
	__le16 *utf16_path = NULL;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct cifs_fid fid;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server;
	int num_rqst = 0, i;
	int resp_buftype[MAX_COMPOUND];
	struct smb2_query_info_rsp *qi_rsp = NULL;
	struct cifs_open_info_data *idata;
	int flags = 0;
	__u8 delete_pending[8] = {1, 0, 0, 0, 0, 0, 0, 0};
	unsigned int size[2];
	void *data[2];
	int len;

	vars = kzalloc(sizeof(*vars), GFP_ATOMIC);
	if (vars == NULL)
		return -ENOMEM;
	rqst = &vars->rqst[0];
	rsp_iov = &vars->rsp_iov[0];

	server = cifs_pick_channel(ses);

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

	vars->oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = full_path,
		.desired_access = desired_access,
		.disposition = create_disposition,
		.create_options = cifs_create_options(cifs_sb, create_options),
		.fid = &fid,
		.mode = mode,
		.cifs_sb = cifs_sb,
	};

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

	for (i = 0; i < num_cmds; i++) {
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
				if (!rc) {
					smb2_set_next_command(tcon, &rqst[num_rqst]);
					smb2_set_related(&rqst[num_rqst]);
				}
			}

			if (rc)
				goto finished;
			num_rqst++;
			trace_smb3_query_info_compound_enter(xid, ses->Suid,
							     tcon->tid, full_path);
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
							  (sizeof(struct cifs_sid) * 2), 0, NULL);
			} else {
				rc = SMB2_query_info_init(tcon, server,
							  &rqst[num_rqst],
							  COMPOUND_FID,
							  COMPOUND_FID,
							  SMB_FIND_FILE_POSIX_INFO,
							  SMB2_O_INFO_FILE, 0,
							  sizeof(struct smb311_posix_qinfo *) +
							  (PATH_MAX * 2) +
							  (sizeof(struct cifs_sid) * 2), 0, NULL);
				if (!rc) {
					smb2_set_next_command(tcon, &rqst[num_rqst]);
					smb2_set_related(&rqst[num_rqst]);
				}
			}

			if (rc)
				goto finished;
			num_rqst++;
			trace_smb3_posix_query_info_compound_enter(xid, ses->Suid,
								   tcon->tid, full_path);
			break;
		case SMB2_OP_DELETE:
			trace_smb3_delete_enter(xid, ses->Suid, tcon->tid, full_path);
			break;
		case SMB2_OP_MKDIR:
			/*
			 * Directories are created through parameters in the
			 * SMB2_open() call.
			 */
			trace_smb3_mkdir_enter(xid, ses->Suid, tcon->tid, full_path);
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
			trace_smb3_rmdir_enter(xid, ses->Suid, tcon->tid, full_path);
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
				if (!rc) {
					smb2_set_next_command(tcon, &rqst[num_rqst]);
					smb2_set_related(&rqst[num_rqst]);
				}
			}
			if (rc)
				goto finished;
			num_rqst++;
			trace_smb3_set_eof_enter(xid, ses->Suid, tcon->tid, full_path);
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
				if (!rc) {
					smb2_set_next_command(tcon, &rqst[num_rqst]);
					smb2_set_related(&rqst[num_rqst]);
				}
			}

			if (rc)
				goto finished;
			num_rqst++;
			trace_smb3_set_info_compound_enter(xid, ses->Suid,
							   tcon->tid, full_path);
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
				if (!rc) {
					smb2_set_next_command(tcon, &rqst[num_rqst]);
					smb2_set_related(&rqst[num_rqst]);
				}
			}
			if (rc)
				goto finished;
			num_rqst++;
			trace_smb3_rename_enter(xid, ses->Suid, tcon->tid, full_path);
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
			trace_smb3_hardlink_enter(xid, ses->Suid, tcon->tid, full_path);
			break;
		case SMB2_OP_SET_REPARSE:
			rqst[num_rqst].rq_iov = vars->io_iov;
			rqst[num_rqst].rq_nvec = ARRAY_SIZE(vars->io_iov);

			rc = SMB2_ioctl_init(tcon, server, &rqst[num_rqst],
					     COMPOUND_FID, COMPOUND_FID,
					     FSCTL_SET_REPARSE_POINT,
					     in_iov[i].iov_base,
					     in_iov[i].iov_len, 0);
			if (rc)
				goto finished;
			smb2_set_next_command(tcon, &rqst[num_rqst]);
			smb2_set_related(&rqst[num_rqst++]);
			trace_smb3_set_reparse_compound_enter(xid, ses->Suid,
							      tcon->tid, full_path);
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
		rc = compound_send_recv(xid, ses, server,
					flags, num_rqst - 2,
					&rqst[1], &resp_buftype[1],
					&rsp_iov[1]);
	} else
		rc = compound_send_recv(xid, ses, server,
					flags, num_rqst,
					rqst, resp_buftype,
					rsp_iov);

finished:
	num_rqst = 0;
	SMB2_open_free(&rqst[num_rqst++]);
	if (rc == -EREMCHG) {
		pr_warn_once("server share %s deleted\n", tcon->tree_name);
		tcon->need_reconnect = true;
	}

	for (i = 0; i < num_cmds; i++) {
		switch (cmds[i]) {
		case SMB2_OP_QUERY_INFO:
			idata = in_iov[i].iov_base;
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
				trace_smb3_query_info_compound_err(xid,  ses->Suid,
								   tcon->tid, rc);
			else
				trace_smb3_query_info_compound_done(xid, ses->Suid,
								    tcon->tid);
			break;
		case SMB2_OP_POSIX_QUERY_INFO:
			idata = in_iov[i].iov_base;
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
			if (rc == 0) {
				unsigned int length = le32_to_cpu(qi_rsp->OutputBufferLength);

				if (length > sizeof(idata->posix_fi)) {
					char *base = (char *)rsp_iov[i + 1].iov_base +
						le16_to_cpu(qi_rsp->OutputBufferOffset) +
						sizeof(idata->posix_fi);
					*extbuflen = length - sizeof(idata->posix_fi);
					*extbuf = kmemdup(base, *extbuflen, GFP_KERNEL);
					if (!*extbuf)
						rc = -ENOMEM;
				} else {
					rc = -EINVAL;
				}
			}
			SMB2_query_info_free(&rqst[num_rqst++]);
			if (rc)
				trace_smb3_posix_query_info_compound_err(xid,  ses->Suid,
									 tcon->tid, rc);
			else
				trace_smb3_posix_query_info_compound_done(xid, ses->Suid,
									  tcon->tid);
			break;
		case SMB2_OP_DELETE:
			if (rc)
				trace_smb3_delete_err(xid,  ses->Suid, tcon->tid, rc);
			else
				trace_smb3_delete_done(xid, ses->Suid, tcon->tid);
			break;
		case SMB2_OP_MKDIR:
			if (rc)
				trace_smb3_mkdir_err(xid,  ses->Suid, tcon->tid, rc);
			else
				trace_smb3_mkdir_done(xid, ses->Suid, tcon->tid);
			break;
		case SMB2_OP_HARDLINK:
			if (rc)
				trace_smb3_hardlink_err(xid,  ses->Suid, tcon->tid, rc);
			else
				trace_smb3_hardlink_done(xid, ses->Suid, tcon->tid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_RENAME:
			if (rc)
				trace_smb3_rename_err(xid,  ses->Suid, tcon->tid, rc);
			else
				trace_smb3_rename_done(xid, ses->Suid, tcon->tid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_RMDIR:
			if (rc)
				trace_smb3_rmdir_err(xid,  ses->Suid, tcon->tid, rc);
			else
				trace_smb3_rmdir_done(xid, ses->Suid, tcon->tid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_SET_EOF:
			if (rc)
				trace_smb3_set_eof_err(xid,  ses->Suid, tcon->tid, rc);
			else
				trace_smb3_set_eof_done(xid, ses->Suid, tcon->tid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_SET_INFO:
			if (rc)
				trace_smb3_set_info_compound_err(xid,  ses->Suid,
								 tcon->tid, rc);
			else
				trace_smb3_set_info_compound_done(xid, ses->Suid,
								  tcon->tid);
			SMB2_set_info_free(&rqst[num_rqst++]);
			break;
		case SMB2_OP_SET_REPARSE:
			if (rc) {
				trace_smb3_set_reparse_compound_err(xid,  ses->Suid,
								    tcon->tid, rc);
			} else {
				trace_smb3_set_reparse_compound_done(xid, ses->Suid,
								     tcon->tid);
			}
			SMB2_ioctl_free(&rqst[num_rqst++]);
			break;
		}
	}
	SMB2_close_free(&rqst[num_rqst]);

	if (cfile)
		cifsFileInfo_put(cfile);

	num_cmds += 2;
	if (out_iov && out_buftype) {
		memcpy(out_iov, rsp_iov, num_cmds * sizeof(*out_iov));
		memcpy(out_buftype, resp_buftype,
		       num_cmds * sizeof(*out_buftype));
	} else {
		for (i = 0; i < num_cmds; i++)
			free_rsp_buf(resp_buftype[i], rsp_iov[i].iov_base);
	}
	kfree(vars);
	return rc;
}

static int parse_create_response(struct cifs_open_info_data *data,
				 struct cifs_sb_info *cifs_sb,
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

int smb2_query_path_info(const unsigned int xid,
			 struct cifs_tcon *tcon,
			 struct cifs_sb_info *cifs_sb,
			 const char *full_path,
			 struct cifs_open_info_data *data)
{
	__u32 create_options = 0;
	struct cifsFileInfo *cfile;
	struct cached_fid *cfid = NULL;
	struct smb2_hdr *hdr;
	struct kvec in_iov, out_iov[3] = {};
	int out_buftype[3] = {};
	bool islink;
	int cmd = SMB2_OP_QUERY_INFO;
	int rc, rc2;

	data->adjust_tz = false;
	data->reparse_point = false;

	if (strcmp(full_path, ""))
		rc = -ENOENT;
	else
		rc = open_cached_dir(xid, tcon, full_path, cifs_sb, false, &cfid);
	/* If it is a root and its handle is cached then use it */
	if (!rc) {
		if (cfid->file_all_info_is_valid) {
			memcpy(&data->fi, &cfid->file_all_info, sizeof(data->fi));
		} else {
			rc = SMB2_query_info(xid, tcon, cfid->fid.persistent_fid,
					     cfid->fid.volatile_fid, &data->fi);
		}
		close_cached_dir(cfid);
		return rc;
	}

	in_iov.iov_base = data;
	in_iov.iov_len = sizeof(*data);

	cifs_get_readable_path(tcon, full_path, &cfile);
	rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
			      FILE_READ_ATTRIBUTES, FILE_OPEN,
			      create_options, ACL_NO_MODE, &in_iov,
			      &cmd, 1, cfile, NULL, NULL, out_iov, out_buftype);
	hdr = out_iov[0].iov_base;
	/*
	 * If first iov is unset, then SMB session was dropped or we've got a
	 * cached open file (@cfile).
	 */
	if (!hdr || out_buftype[0] == CIFS_NO_BUFFER)
		goto out;

	switch (rc) {
	case 0:
	case -EOPNOTSUPP:
		rc = parse_create_response(data, cifs_sb, &out_iov[0]);
		if (rc || !data->reparse_point)
			goto out;

		create_options |= OPEN_REPARSE_POINT;
		/* Failed on a symbolic link - query a reparse point info */
		cifs_get_readable_path(tcon, full_path, &cfile);
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
				      FILE_READ_ATTRIBUTES, FILE_OPEN,
				      create_options, ACL_NO_MODE, &in_iov,
				      &cmd, 1, cfile, NULL, NULL, NULL, NULL);
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
	free_rsp_buf(out_buftype[0], out_iov[0].iov_base);
	free_rsp_buf(out_buftype[1], out_iov[1].iov_base);
	free_rsp_buf(out_buftype[2], out_iov[2].iov_base);
	return rc;
}

int smb311_posix_query_path_info(const unsigned int xid,
				 struct cifs_tcon *tcon,
				 struct cifs_sb_info *cifs_sb,
				 const char *full_path,
				 struct cifs_open_info_data *data,
				 struct cifs_sid *owner,
				 struct cifs_sid *group)
{
	int rc;
	__u32 create_options = 0;
	struct cifsFileInfo *cfile;
	struct kvec in_iov, out_iov[3] = {};
	int out_buftype[3] = {};
	__u8 *sidsbuf = NULL;
	__u8 *sidsbuf_end = NULL;
	size_t sidsbuflen = 0;
	size_t owner_len, group_len;
	int cmd = SMB2_OP_POSIX_QUERY_INFO;

	data->adjust_tz = false;
	data->reparse_point = false;

	/*
	 * BB TODO: Add support for using the cached root handle.
	 * Create SMB2_query_posix_info worker function to do non-compounded query
	 * when we already have an open file handle for this. For now this is fast enough
	 * (always using the compounded version).
	 */
	in_iov.iov_base = data;
	in_iov.iov_len = sizeof(*data);

	cifs_get_readable_path(tcon, full_path, &cfile);
	rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
			      FILE_READ_ATTRIBUTES, FILE_OPEN,
			      create_options, ACL_NO_MODE, &in_iov, &cmd, 1,
			      cfile, &sidsbuf, &sidsbuflen, out_iov, out_buftype);
	/*
	 * If first iov is unset, then SMB session was dropped or we've got a
	 * cached open file (@cfile).
	 */
	if (!out_iov[0].iov_base || out_buftype[0] == CIFS_NO_BUFFER)
		goto out;

	switch (rc) {
	case 0:
	case -EOPNOTSUPP:
		/* BB TODO: When support for special files added to Samba re-verify this path */
		rc = parse_create_response(data, cifs_sb, &out_iov[0]);
		if (rc || !data->reparse_point)
			goto out;

		create_options |= OPEN_REPARSE_POINT;
		/* Failed on a symbolic link - query a reparse point info */
		cifs_get_readable_path(tcon, full_path, &cfile);
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
				      FILE_READ_ATTRIBUTES, FILE_OPEN,
				      create_options, ACL_NO_MODE, &in_iov, &cmd, 1,
				      cfile, &sidsbuf, &sidsbuflen, NULL, NULL);
		break;
	}

out:
	if (rc == 0) {
		sidsbuf_end = sidsbuf + sidsbuflen;

		owner_len = posix_info_sid_size(sidsbuf, sidsbuf_end);
		if (owner_len == -1) {
			rc = -EINVAL;
			goto out;
		}
		memcpy(owner, sidsbuf, owner_len);

		group_len = posix_info_sid_size(
			sidsbuf + owner_len, sidsbuf_end);
		if (group_len == -1) {
			rc = -EINVAL;
			goto out;
		}
		memcpy(group, sidsbuf + owner_len, group_len);
	}

	kfree(sidsbuf);
	free_rsp_buf(out_buftype[0], out_iov[0].iov_base);
	free_rsp_buf(out_buftype[1], out_iov[1].iov_base);
	free_rsp_buf(out_buftype[2], out_iov[2].iov_base);
	return rc;
}

int
smb2_mkdir(const unsigned int xid, struct inode *parent_inode, umode_t mode,
	   struct cifs_tcon *tcon, const char *name,
	   struct cifs_sb_info *cifs_sb)
{
	return smb2_compound_op(xid, tcon, cifs_sb, name,
				FILE_WRITE_ATTRIBUTES, FILE_CREATE,
				CREATE_NOT_FILE, mode, NULL,
				&(int){SMB2_OP_MKDIR}, 1,
				NULL, NULL, NULL, NULL, NULL);
}

void
smb2_mkdir_setinfo(struct inode *inode, const char *name,
		   struct cifs_sb_info *cifs_sb, struct cifs_tcon *tcon,
		   const unsigned int xid)
{
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
	tmprc = smb2_compound_op(xid, tcon, cifs_sb, name,
				 FILE_WRITE_ATTRIBUTES, FILE_CREATE,
				 CREATE_NOT_FILE, ACL_NO_MODE, &in_iov,
				 &(int){SMB2_OP_SET_INFO}, 1,
				 cfile, NULL, NULL, NULL, NULL);
	if (tmprc == 0)
		cifs_i->cifsAttrs = dosattrs;
}

int
smb2_rmdir(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	   struct cifs_sb_info *cifs_sb)
{
	drop_cached_dir_by_name(xid, tcon, name, cifs_sb);
	return smb2_compound_op(xid, tcon, cifs_sb, name,
				DELETE, FILE_OPEN, CREATE_NOT_FILE,
				ACL_NO_MODE, NULL, &(int){SMB2_OP_RMDIR}, 1,
				NULL, NULL, NULL, NULL, NULL);
}

int
smb2_unlink(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	    struct cifs_sb_info *cifs_sb)
{
	return smb2_compound_op(xid, tcon, cifs_sb, name, DELETE, FILE_OPEN,
				CREATE_DELETE_ON_CLOSE | OPEN_REPARSE_POINT,
				ACL_NO_MODE, NULL, &(int){SMB2_OP_DELETE}, 1,
				NULL, NULL, NULL, NULL, NULL);
}

static int
smb2_set_path_attr(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *from_name, const char *to_name,
		   struct cifs_sb_info *cifs_sb, __u32 access, int command,
		   struct cifsFileInfo *cfile)
{
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
	rc = smb2_compound_op(xid, tcon, cifs_sb, from_name, access,
			      FILE_OPEN, 0, ACL_NO_MODE, &in_iov,
			      &command, 1, cfile, NULL, NULL, NULL, NULL);
smb2_rename_path:
	kfree(smb2_to_name);
	return rc;
}

int
smb2_rename_path(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *from_name, const char *to_name,
		 struct cifs_sb_info *cifs_sb)
{
	struct cifsFileInfo *cfile;

	drop_cached_dir_by_name(xid, tcon, from_name, cifs_sb);
	cifs_get_writable_path(tcon, from_name, FIND_WR_WITH_DELETE, &cfile);

	return smb2_set_path_attr(xid, tcon, from_name, to_name,
				  cifs_sb, DELETE, SMB2_OP_RENAME, cfile);
}

int
smb2_create_hardlink(const unsigned int xid, struct cifs_tcon *tcon,
		     const char *from_name, const char *to_name,
		     struct cifs_sb_info *cifs_sb)
{
	return smb2_set_path_attr(xid, tcon, from_name, to_name, cifs_sb,
				  FILE_READ_ATTRIBUTES, SMB2_OP_HARDLINK,
				  NULL);
}

int
smb2_set_path_size(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *full_path, __u64 size,
		   struct cifs_sb_info *cifs_sb, bool set_alloc)
{
	struct cifsFileInfo *cfile;
	struct kvec in_iov;
	__le64 eof = cpu_to_le64(size);

	in_iov.iov_base = &eof;
	in_iov.iov_len = sizeof(eof);
	cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);
	return smb2_compound_op(xid, tcon, cifs_sb, full_path,
				FILE_WRITE_DATA, FILE_OPEN,
				0, ACL_NO_MODE, &in_iov,
				&(int){SMB2_OP_SET_EOF}, 1,
				cfile, NULL, NULL, NULL, NULL);
}

int
smb2_set_file_info(struct inode *inode, const char *full_path,
		   FILE_BASIC_INFO *buf, const unsigned int xid)
{
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
	rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
			      FILE_WRITE_ATTRIBUTES, FILE_OPEN,
			      0, ACL_NO_MODE, &in_iov,
			      &(int){SMB2_OP_SET_INFO}, 1, cfile,
			      NULL, NULL, NULL, NULL);
	cifs_put_tlink(tlink);
	return rc;
}

struct inode *smb2_get_reparse_inode(struct cifs_open_info_data *data,
				     struct super_block *sb,
				     const unsigned int xid,
				     struct cifs_tcon *tcon,
				     const char *full_path,
				     struct kvec *iov)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsFileInfo *cfile;
	struct inode *new = NULL;
	struct kvec in_iov[2];
	int cmds[2];
	int da, co, cd;
	int rc;

	da = SYNCHRONIZE | DELETE |
		FILE_READ_ATTRIBUTES |
		FILE_WRITE_ATTRIBUTES;
	co = CREATE_NOT_DIR | OPEN_REPARSE_POINT;
	cd = FILE_CREATE;
	cmds[0] = SMB2_OP_SET_REPARSE;
	in_iov[0] = *iov;
	in_iov[1].iov_base = data;
	in_iov[1].iov_len = sizeof(*data);

	if (tcon->posix_extensions) {
		cmds[1] = SMB2_OP_POSIX_QUERY_INFO;
		cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
				      da, cd, co, ACL_NO_MODE, in_iov,
				      cmds, 2, cfile, NULL, NULL, NULL, NULL);
		if (!rc) {
			rc = smb311_posix_get_inode_info(&new, full_path,
							 data, sb, xid);
		}
	} else {
		cmds[1] = SMB2_OP_QUERY_INFO;
		cifs_get_writable_path(tcon, full_path, FIND_WR_ANY, &cfile);
		rc = smb2_compound_op(xid, tcon, cifs_sb, full_path,
				      da, cd, co, ACL_NO_MODE, in_iov,
				      cmds, 2, cfile, NULL, NULL, NULL, NULL);
		if (!rc) {
			rc = cifs_get_inode_info(&new, full_path,
						 data, sb, xid, NULL);
		}
	}
	return rc ? ERR_PTR(rc) : new;
}
