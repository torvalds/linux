// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */
#include <crypto/md5.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include "cifsfs.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "smb2proto.h"
#include "smb2glob.h"
#include "cifs_ioctl.h"
#include "fs_context.h"
#include "reparse.h"

/*
 * M-F Symlink Functions - Begin
 */

#define CIFS_MF_SYMLINK_LEN_OFFSET (4+1)
#define CIFS_MF_SYMLINK_MD5_OFFSET (CIFS_MF_SYMLINK_LEN_OFFSET+(4+1))
#define CIFS_MF_SYMLINK_LINK_OFFSET (CIFS_MF_SYMLINK_MD5_OFFSET+(32+1))
#define CIFS_MF_SYMLINK_LINK_MAXLEN (1024)
#define CIFS_MF_SYMLINK_FILE_SIZE \
	(CIFS_MF_SYMLINK_LINK_OFFSET + CIFS_MF_SYMLINK_LINK_MAXLEN)

#define CIFS_MF_SYMLINK_LEN_FORMAT "XSym\n%04u\n"
#define CIFS_MF_SYMLINK_MD5_FORMAT "%16phN\n"
#define CIFS_MF_SYMLINK_MD5_ARGS(md5_hash) md5_hash

static int
parse_mf_symlink(const u8 *buf, unsigned int buf_len, unsigned int *_link_len,
		 char **_link_str)
{
	int rc;
	unsigned int link_len;
	const char *md5_str1;
	const char *link_str;
	u8 md5_hash[16];
	char md5_str2[34];

	if (buf_len != CIFS_MF_SYMLINK_FILE_SIZE)
		return -EINVAL;

	md5_str1 = (const char *)&buf[CIFS_MF_SYMLINK_MD5_OFFSET];
	link_str = (const char *)&buf[CIFS_MF_SYMLINK_LINK_OFFSET];

	rc = sscanf(buf, CIFS_MF_SYMLINK_LEN_FORMAT, &link_len);
	if (rc != 1)
		return -EINVAL;

	if (link_len > CIFS_MF_SYMLINK_LINK_MAXLEN)
		return -EINVAL;

	md5(link_str, link_len, md5_hash);

	scnprintf(md5_str2, sizeof(md5_str2),
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
format_mf_symlink(u8 *buf, unsigned int buf_len, const char *link_str)
{
	unsigned int link_len;
	unsigned int ofs;
	u8 md5_hash[16];

	if (buf_len != CIFS_MF_SYMLINK_FILE_SIZE)
		return -EINVAL;

	link_len = strlen(link_str);

	if (link_len > CIFS_MF_SYMLINK_LINK_MAXLEN)
		return -ENAMETOOLONG;

	md5(link_str, link_len, md5_hash);

	scnprintf(buf, buf_len,
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

bool
couldbe_mf_symlink(const struct cifs_fattr *fattr)
{
	if (!S_ISREG(fattr->cf_mode))
		/* it's not a symlink */
		return false;

	if (fattr->cf_eof != CIFS_MF_SYMLINK_FILE_SIZE)
		/* it's not a symlink */
		return false;

	return true;
}

static int
create_mf_symlink(const unsigned int xid, struct cifs_tcon *tcon,
		  struct cifs_sb_info *cifs_sb, const char *fromName,
		  const char *toName)
{
	int rc;
	u8 *buf;
	unsigned int bytes_written = 0;

	buf = kmalloc(CIFS_MF_SYMLINK_FILE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = format_mf_symlink(buf, CIFS_MF_SYMLINK_FILE_SIZE, toName);
	if (rc)
		goto out;

	if (tcon->ses->server->ops->create_mf_symlink)
		rc = tcon->ses->server->ops->create_mf_symlink(xid, tcon,
					cifs_sb, fromName, buf, &bytes_written);
	else
		rc = -EOPNOTSUPP;

	if (rc)
		goto out;

	if (bytes_written != CIFS_MF_SYMLINK_FILE_SIZE)
		rc = smb_EIO2(smb_eio_trace_symlink_file_size,
			      bytes_written, CIFS_MF_SYMLINK_FILE_SIZE);
out:
	kfree(buf);
	return rc;
}

int cifs_check_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb,
			  const char *full_path, struct inode *inode,
			  struct cifs_fattr *fattr)
{
	struct TCP_Server_Info *server = tcon->ses->server;
	char *buf __free(kfree) = NULL;
	unsigned int bytes_read = 0;
	unsigned int link_len = 0;
	char *symlink = NULL;
	int rc = 0;

	if (!couldbe_mf_symlink(fattr))
		goto out;

	buf = kmalloc(CIFS_MF_SYMLINK_FILE_SIZE, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}

	if (server->ops->query_mf_symlink) {
		rc = server->ops->query_mf_symlink(xid, tcon,
						   cifs_sb, full_path,
						   inode, buf, &bytes_read);
	} else {
		rc = -ENOSYS;
	}

	if (rc)
		goto out;

	if (bytes_read == 0) /* not a symlink */
		goto out;

	rc = parse_mf_symlink(buf, bytes_read, &link_len, &symlink);
	if (rc == -EINVAL) {
		/* it's not a symlink */
		rc = 0;
		goto out;
	}

	if (rc != 0)
		goto out;

	/* it is a symlink */
	fattr->cf_eof = link_len;
	fattr->cf_mode &= ~S_IFMT;
	fattr->cf_mode |= S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;
	fattr->cf_dtype = DT_LNK;
	fattr->cf_symlink_target = symlink;
out:
	cifs_dbg(FYI, "%s: rc = %d\n", __func__, rc);
	return rc == -ENOMEM ? rc : 0;
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
int cifs_query_mf_symlink(const unsigned int xid,
			  struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb,
			  const char *full_path,
			  struct inode *inode, char *pbuf,
			  unsigned int *pbytes_read)
{
	int rc;
	int oplock = 0;
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	struct cifs_io_parms io_parms = {0};
	int buf_type = CIFS_NO_BUFFER;
	struct cifs_open_info_data query_data = {};

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.cifs_sb = cifs_sb,
		.desired_access = GENERIC_READ,
		.create_options = cifs_create_options(cifs_sb, CREATE_NOT_DIR),
		.disposition = FILE_OPEN,
		.path = full_path,
		.fid = &fid,
	};

	rc = tcon->ses->server->ops->open(xid, &oparms, &oplock, &query_data);
	if (rc)
		return rc;

	if (query_data.fi.EndOfFile != cpu_to_le64(CIFS_MF_SYMLINK_FILE_SIZE)) {
		rc = -ENOENT;
		/* it's not a symlink */
		goto out;
	}

	io_parms.netfid = fid.netfid;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.offset = 0;
	io_parms.length = CIFS_MF_SYMLINK_FILE_SIZE;

	rc = CIFSSMBRead(xid, &io_parms, pbytes_read, &pbuf, &buf_type);
out:
	CIFSSMBClose(xid, tcon, fid.netfid);
	return rc;
}

int
cifs_create_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
		       struct cifs_sb_info *cifs_sb, const unsigned char *path,
		       char *pbuf, unsigned int *pbytes_written)
{
	int rc;
	int oplock = 0;
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	struct cifs_io_parms io_parms = {0};

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.cifs_sb = cifs_sb,
		.desired_access = GENERIC_WRITE,
		.create_options = cifs_create_options(cifs_sb, CREATE_NOT_DIR),
		.disposition = FILE_CREATE,
		.path = path,
		.fid = &fid,
	};

	rc = tcon->ses->server->ops->open(xid, &oparms, &oplock, NULL);
	if (rc)
		return rc;

	io_parms.netfid = fid.netfid;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.offset = 0;
	io_parms.length = CIFS_MF_SYMLINK_FILE_SIZE;

	rc = CIFSSMBWrite(xid, &io_parms, pbytes_written, pbuf);
	CIFSSMBClose(xid, tcon, fid.netfid);
	return rc;
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

int smb2_query_mf_symlink(const unsigned int xid,
			  struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb,
			  const char *full_path,
			  struct inode *inode, char *pbuf,
			  unsigned int *pbytes_read)
{
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec read_iov[2], close_iov;
	struct cifsFileInfo *cfile = NULL;
	struct smb2_read_rsp *read_rsp;
	struct TCP_Server_Info *server;
	int retries = 0, cur_sleep = 0;
	struct cifs_io_parms io_parms;
	struct smb2_read_req *req;
	struct smb_rqst rqst[3];
	unsigned int total_len;
	struct kvec rsp_iov[3];
	struct cifs_fid fid;
	int rsp_buftype[3];
	struct kvec *riov;
	int num_rqst;
	__u8 oplock;
	int flags;
	u32 dlen;
	int rc;

	cifs_get_readable_path(tcon, full_path, &cfile);
	*pbytes_read	= 0;
again:
	flags		= 0;
	num_rqst	= 0;
	req		= NULL;
	oplock		= SMB2_OPLOCK_LEVEL_NONE;
	server		= cifs_pick_channel(tcon->ses);

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	for (int i = 0; i < ARRAY_SIZE(rsp_buftype); i++)
		rsp_buftype[i] = CIFS_NO_BUFFER;

	memset(rqst, 0, sizeof(rqst));
	memset(rsp_iov, 0, sizeof(rsp_iov));

	if (!cfile) {
		__le16 *utf16_path __free(kfree) = NULL;
		struct cifs_open_parms oparms;

		utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
		if (!utf16_path) {
			rc = -ENOMEM;
			goto out;
		}

		if (inode && CIFS_I(inode)->lease_granted &&
		    server->ops->get_lease_key) {
			oplock = SMB2_OPLOCK_LEVEL_LEASE;
			server->ops->get_lease_key(inode, &fid);
		}

		oparms = CIFS_OPARMS(cifs_sb, tcon, full_path, GENERIC_READ,
				     FILE_OPEN, CREATE_NOT_DIR, ACL_NO_MODE);
		oparms.fid = &fid;

		memset(open_iov, 0, sizeof(open_iov));
		rqst[num_rqst].rq_iov = open_iov;
		rqst[num_rqst].rq_nvec = SMB2_CREATE_IOV_SIZE;

		rc = SMB2_open_init(tcon, server, &rqst[num_rqst],
				    &oplock, &oparms, utf16_path);
		if (rc)
			goto out;
		smb2_set_next_command(tcon, &rqst[num_rqst++]);
	}

	io_parms = (struct cifs_io_parms) {
		.pid			= current->tgid,
		.tcon			= tcon,
		.length			= CIFS_MF_SYMLINK_FILE_SIZE,
		.server			= server,
		.persistent_fid		= COMP_PID(cfile),
		.volatile_fid		= COMP_VID(cfile),
	};

	rc = smb2_new_read_req((void **)&req, &total_len,
			       &io_parms, NULL, 0, 0);
	if (rc)
		goto out;

	read_iov[0].iov_base = req;
	read_iov[0].iov_len = total_len;
	rqst[num_rqst].rq_iov = read_iov;
	rqst[num_rqst].rq_nvec = 1;
	if (!cfile) {
		smb2_set_next_command(tcon, &rqst[num_rqst]);
		smb2_set_related(&rqst[num_rqst]);
	}
	num_rqst++;

	if (!cfile) {
		flags |= CIFS_CP_CREATE_CLOSE_OP;
		memset(&close_iov, 0, sizeof(close_iov));
		rqst[num_rqst].rq_iov = &close_iov;
		rqst[num_rqst].rq_nvec = 1;
		rc = SMB2_close_init(tcon, server, &rqst[num_rqst],
				     COMPOUND_FID, COMPOUND_FID, false);
		smb2_set_related(&rqst[num_rqst++]);
		if (rc)
			goto out;
	}

	if (retries) {
		if (cur_sleep)
			msleep(cur_sleep);
		for (int i = 0; i < num_rqst; i++)
			smb2_set_replay(server, &rqst[i]);
	}

	rc = compound_send_recv(xid, tcon->ses, server, flags,
				num_rqst, rqst, rsp_buftype, rsp_iov);
	if (rc) {
		if (rc != -ENODATA) {
			if (rc != -ENOENT)
				cifs_stats_fail_inc(tcon, SMB2_READ_HE);
			cifs_dbg(FYI, "%s: failed to read file: %d\n",
				 __func__, rc);
			trace_smb3_read_err(0, 0, xid, req->PersistentFileId,
					    tcon->tid, tcon->ses->Suid,
					    io_parms.offset, io_parms.length,
					    rc);
		} else {
			trace_smb3_read_done(0, 0, xid, req->PersistentFileId,
					     tcon->tid, tcon->ses->Suid,
					     io_parms.offset, 0);
		}
		rc = rc == -ENODATA ? 0 : rc;
		goto out;
	}
	trace_smb3_read_done(0, 0, xid, req->PersistentFileId,
			     tcon->tid, tcon->ses->Suid,
			     io_parms.offset, io_parms.length);

	riov = &rsp_iov[!cfile ? 1 : 0];
	read_rsp = riov->iov_base;
	dlen = le32_to_cpu(read_rsp->DataLength);
	if (dlen > CIFS_MAX_MSGSIZE || dlen > io_parms.length) {
		cifs_dbg(FYI, "%s: bad length %u for count %u\n",
			 __func__, dlen, io_parms.length);
		rc = smb_EIO2(smb_eio_trace_read_overlarge,
			      dlen, io_parms.length);
	} else if (dlen != CIFS_MF_SYMLINK_FILE_SIZE) {
		rc = -ENOENT;
	} else {
		rc = smb2_validate_iov(read_rsp->DataOffset, dlen, riov,
				       CIFS_MF_SYMLINK_FILE_SIZE);
	}

	if (!rc) {
		memcpy(pbuf, (u8 *)read_rsp + read_rsp->DataOffset, dlen);
		*pbytes_read = dlen;
	}

out:
	if (!cfile) {
		SMB2_open_free(&rqst[0]);
		SMB2_close_free(&rqst[2]);
	}
	cifs_small_buf_release(req);
	for (int i = 0; i < ARRAY_SIZE(rsp_buftype); i++)
		free_rsp_buf(rsp_buftype[i], rsp_iov[i].iov_base);

	if (is_replayable_error(rc) &&
	    smb2_should_replay(tcon, &retries, &cur_sleep))
		goto again;

	if (cfile)
		cifsFileInfo_put(cfile);
	return rc;
}

int
smb2_create_mf_symlink(unsigned int xid, struct cifs_tcon *tcon,
		       struct cifs_sb_info *cifs_sb, const unsigned char *path,
		       char *pbuf, unsigned int *pbytes_written)
{
	int rc;
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	struct cifs_io_parms io_parms = {0};
	__le16 *utf16_path;
	__u8 oplock = SMB2_OPLOCK_LEVEL_NONE;
	struct kvec iov[2];

	cifs_dbg(FYI, "%s: path: %s\n", __func__, path);

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.cifs_sb = cifs_sb,
		.path = path,
		.desired_access = GENERIC_WRITE,
		.create_options = cifs_create_options(cifs_sb, CREATE_NOT_DIR),
		.disposition = FILE_CREATE,
		.fid = &fid,
		.mode = 0644,
	};

	rc = SMB2_open(xid, &oparms, utf16_path, &oplock, NULL, NULL,
		       NULL, NULL);
	if (rc) {
		kfree(utf16_path);
		return rc;
	}

	io_parms.netfid = fid.netfid;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.offset = 0;
	io_parms.length = CIFS_MF_SYMLINK_FILE_SIZE;
	io_parms.persistent_fid = fid.persistent_fid;
	io_parms.volatile_fid = fid.volatile_fid;

	/* iov[0] is reserved for smb header */
	iov[1].iov_base = pbuf;
	iov[1].iov_len = CIFS_MF_SYMLINK_FILE_SIZE;

	rc = SMB2_write(xid, &io_parms, pbytes_written, iov, 1);

	/* Make sure we wrote all of the symlink data */
	if ((rc == 0) && (*pbytes_written != CIFS_MF_SYMLINK_FILE_SIZE))
		rc = smb_EIO2(smb_eio_trace_short_symlink_write,
			      *pbytes_written, CIFS_MF_SYMLINK_FILE_SIZE);

	SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);

	kfree(utf16_path);
	return rc;
}

/*
 * M-F Symlink Functions - End
 */

int
cifs_hardlink(struct dentry *old_file, struct inode *inode,
	      struct dentry *direntry)
{
	int rc = -EACCES;
	unsigned int xid;
	const char *from_name, *to_name;
	void *page1, *page2;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifsInodeInfo *cifsInode;

	if (unlikely(cifs_forced_shutdown(cifs_sb)))
		return smb_EIO(smb_eio_trace_forced_shutdown);

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	xid = get_xid();
	page1 = alloc_dentry_path();
	page2 = alloc_dentry_path();

	from_name = build_path_from_dentry(old_file, page1);
	if (IS_ERR(from_name)) {
		rc = PTR_ERR(from_name);
		goto cifs_hl_exit;
	}
	to_name = build_path_from_dentry(direntry, page2);
	if (IS_ERR(to_name)) {
		rc = PTR_ERR(to_name);
		goto cifs_hl_exit;
	}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if (tcon->unix_ext)
		rc = CIFSUnixCreateHardLink(xid, tcon, from_name, to_name,
					    cifs_sb->local_nls,
					    cifs_remap(cifs_sb));
	else {
#else
	{
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
		server = tcon->ses->server;
		if (!server->ops->create_hardlink) {
			rc = -ENOSYS;
			goto cifs_hl_exit;
		}
		rc = server->ops->create_hardlink(xid, tcon, old_file,
						  from_name, to_name, cifs_sb);
		if ((rc == -EIO) || (rc == -EINVAL))
			rc = -EOPNOTSUPP;
	}

	d_drop(direntry);	/* force new lookup from server of target */

	/*
	 * if source file is cached (oplocked) revalidate will not go to server
	 * until the file is closed or oplock broken so update nlinks locally
	 */
	if (d_really_is_positive(old_file)) {
		cifsInode = CIFS_I(d_inode(old_file));
		if (rc == 0) {
			clear_bit(CIFS_INO_TMPFILE, &cifsInode->flags);
			spin_lock(&d_inode(old_file)->i_lock);
			inc_nlink(d_inode(old_file));
			spin_unlock(&d_inode(old_file)->i_lock);

			/*
			 * parent dir timestamps will update from srv within a
			 * second, would it really be worth it to set the parent
			 * dir cifs inode time to zero to force revalidate
			 * (faster) for it too?
			 */
		}
		/*
		 * if not oplocked will force revalidate to get info on source
		 * file from srv.  Note Samba server prior to 4.2 has bug -
		 * not updating src file ctime on hardlinks but Windows servers
		 * handle it properly
		 */
		cifsInode->time = 0;

		/*
		 * Will update parent dir timestamps from srv within a second.
		 * Would it really be worth it to set the parent dir (cifs
		 * inode) time field to zero to force revalidate on parent
		 * directory faster ie
		 *
		 * CIFS_I(inode)->time = 0;
		 */
	}

cifs_hl_exit:
	free_dentry_path(page1);
	free_dentry_path(page2);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

int
cifs_symlink(const struct mnt_idmap *idmap, struct inode *inode,
	     struct dentry *direntry, const char *symname)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode);
	struct inode *newinode = NULL;
	struct tcon_link *tlink;
	struct cifs_tcon *pTcon;
	const char *full_path;
	int rc = -EOPNOTSUPP;
	unsigned int sbflags;
	unsigned int xid;
	void *page;

	if (unlikely(cifs_forced_shutdown(cifs_sb)))
		return smb_EIO(smb_eio_trace_forced_shutdown);

	page = alloc_dentry_path();
	if (!page)
		return -ENOMEM;

	xid = get_xid();

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		rc = PTR_ERR(tlink);
		/* BB could be clearer if skipped put_tlink on error here, but harmless */
		goto symlink_exit;
	}
	pTcon = tlink_tcon(tlink);

	full_path = build_path_from_dentry(direntry, page);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		goto symlink_exit;
	}

	cifs_dbg(FYI, "Full path: %s\n", full_path);
	cifs_dbg(FYI, "symname is %s\n", symname);

	/* BB what if DFS and this volume is on different share? BB */
	sbflags = cifs_sb_flags(cifs_sb);
	rc = -EOPNOTSUPP;
	switch (cifs_symlink_type(cifs_sb)) {
	case CIFS_SYMLINK_TYPE_UNIX:
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
		if (pTcon->unix_ext) {
			rc = CIFSUnixCreateSymLink(xid, pTcon, full_path,
						   symname,
						   cifs_sb->local_nls,
						   cifs_remap(cifs_sb));
		}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
		break;

	case CIFS_SYMLINK_TYPE_MFSYMLINKS:
		if (sbflags & CIFS_MOUNT_MF_SYMLINKS) {
			rc = create_mf_symlink(xid, pTcon, cifs_sb,
					       full_path, symname);
		}
		break;

	case CIFS_SYMLINK_TYPE_SFU:
		if (sbflags & CIFS_MOUNT_UNX_EMUL) {
			rc = __cifs_sfu_make_node(xid, inode, direntry, pTcon,
						  full_path, S_IFLNK,
						  0, symname);
		}
		break;

	case CIFS_SYMLINK_TYPE_NATIVE:
	case CIFS_SYMLINK_TYPE_NFS:
	case CIFS_SYMLINK_TYPE_WSL:
		if (CIFS_REPARSE_SUPPORT(pTcon)) {
			rc = create_reparse_symlink(xid, inode, direntry, pTcon,
						    full_path, symname);
			goto symlink_exit;
		}
		break;
	default:
		break;
	}

	if (rc == 0) {
		if (pTcon->posix_extensions) {
			rc = smb311_posix_get_inode_info(&newinode, full_path,
							 NULL, inode->i_sb, xid);
		} else if (pTcon->unix_ext) {
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb, xid);
		} else {
			rc = cifs_get_inode_info(&newinode, full_path, NULL,
						 inode->i_sb, xid, NULL);
		}

		if (rc != 0) {
			cifs_dbg(FYI, "Create symlink ok, getinodeinfo fail rc = %d\n",
				 rc);
		} else {
			d_instantiate(direntry, newinode);
		}
	}
symlink_exit:
	free_dentry_path(page);
	cifs_put_tlink(tlink);
	free_xid(xid);
	return rc;
}
