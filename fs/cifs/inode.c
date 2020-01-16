/*
 *   fs/cifs/iyesde.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2010
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
 *   along with this library; if yest, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/freezer.h>
#include <linux/sched/signal.h>
#include <linux/wait_bit.h>

#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifs_unicode.h"
#include "fscache.h"


static void cifs_set_ops(struct iyesde *iyesde)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);

	switch (iyesde->i_mode & S_IFMT) {
	case S_IFREG:
		iyesde->i_op = &cifs_file_iyesde_ops;
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				iyesde->i_fop = &cifs_file_direct_yesbrl_ops;
			else
				iyesde->i_fop = &cifs_file_direct_ops;
		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				iyesde->i_fop = &cifs_file_strict_yesbrl_ops;
			else
				iyesde->i_fop = &cifs_file_strict_ops;
		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			iyesde->i_fop = &cifs_file_yesbrl_ops;
		else { /* yest direct, send byte range locks */
			iyesde->i_fop = &cifs_file_ops;
		}

		/* check if server can support readpages */
		if (cifs_sb_master_tcon(cifs_sb)->ses->server->maxBuf <
				PAGE_SIZE + MAX_CIFS_HDR_SIZE)
			iyesde->i_data.a_ops = &cifs_addr_ops_smallbuf;
		else
			iyesde->i_data.a_ops = &cifs_addr_ops;
		break;
	case S_IFDIR:
#ifdef CONFIG_CIFS_DFS_UPCALL
		if (IS_AUTOMOUNT(iyesde)) {
			iyesde->i_op = &cifs_dfs_referral_iyesde_operations;
		} else {
#else /* NO DFS support, treat as a directory */
		{
#endif
			iyesde->i_op = &cifs_dir_iyesde_ops;
			iyesde->i_fop = &cifs_dir_ops;
		}
		break;
	case S_IFLNK:
		iyesde->i_op = &cifs_symlink_iyesde_ops;
		break;
	default:
		init_special_iyesde(iyesde, iyesde->i_mode, iyesde->i_rdev);
		break;
	}
}

/* check iyesde attributes against fattr. If they don't match, tag the
 * iyesde for cache invalidation
 */
static void
cifs_revalidate_cache(struct iyesde *iyesde, struct cifs_fattr *fattr)
{
	struct cifsIyesdeInfo *cifs_i = CIFS_I(iyesde);

	cifs_dbg(FYI, "%s: revalidating iyesde %llu\n",
		 __func__, cifs_i->uniqueid);

	if (iyesde->i_state & I_NEW) {
		cifs_dbg(FYI, "%s: iyesde %llu is new\n",
			 __func__, cifs_i->uniqueid);
		return;
	}

	/* don't bother with revalidation if we have an oplock */
	if (CIFS_CACHE_READ(cifs_i)) {
		cifs_dbg(FYI, "%s: iyesde %llu is oplocked\n",
			 __func__, cifs_i->uniqueid);
		return;
	}

	 /* revalidate if mtime or size have changed */
	if (timespec64_equal(&iyesde->i_mtime, &fattr->cf_mtime) &&
	    cifs_i->server_eof == fattr->cf_eof) {
		cifs_dbg(FYI, "%s: iyesde %llu is unchanged\n",
			 __func__, cifs_i->uniqueid);
		return;
	}

	cifs_dbg(FYI, "%s: invalidating iyesde %llu mapping\n",
		 __func__, cifs_i->uniqueid);
	set_bit(CIFS_INO_INVALID_MAPPING, &cifs_i->flags);
}

/*
 * copy nlink to the iyesde, unless it wasn't provided.  Provide
 * sane values if we don't have an existing one and yesne was provided
 */
static void
cifs_nlink_fattr_to_iyesde(struct iyesde *iyesde, struct cifs_fattr *fattr)
{
	/*
	 * if we're in a situation where we can't trust what we
	 * got from the server (readdir, some yesn-unix cases)
	 * fake reasonable values
	 */
	if (fattr->cf_flags & CIFS_FATTR_UNKNOWN_NLINK) {
		/* only provide fake values on a new iyesde */
		if (iyesde->i_state & I_NEW) {
			if (fattr->cf_cifsattrs & ATTR_DIRECTORY)
				set_nlink(iyesde, 2);
			else
				set_nlink(iyesde, 1);
		}
		return;
	}

	/* we trust the server, so update it */
	set_nlink(iyesde, fattr->cf_nlink);
}

/* populate an iyesde with info from a cifs_fattr struct */
void
cifs_fattr_to_iyesde(struct iyesde *iyesde, struct cifs_fattr *fattr)
{
	struct cifsIyesdeInfo *cifs_i = CIFS_I(iyesde);
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);

	cifs_revalidate_cache(iyesde, fattr);

	spin_lock(&iyesde->i_lock);
	/* we do yest want atime to be less than mtime, it broke some apps */
	if (timespec64_compare(&fattr->cf_atime, &fattr->cf_mtime) < 0)
		iyesde->i_atime = fattr->cf_mtime;
	else
		iyesde->i_atime = fattr->cf_atime;
	iyesde->i_mtime = fattr->cf_mtime;
	iyesde->i_ctime = fattr->cf_ctime;
	iyesde->i_rdev = fattr->cf_rdev;
	cifs_nlink_fattr_to_iyesde(iyesde, fattr);
	iyesde->i_uid = fattr->cf_uid;
	iyesde->i_gid = fattr->cf_gid;

	/* if dynperm is set, don't clobber existing mode */
	if (iyesde->i_state & I_NEW ||
	    !(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM))
		iyesde->i_mode = fattr->cf_mode;

	cifs_i->cifsAttrs = fattr->cf_cifsattrs;

	if (fattr->cf_flags & CIFS_FATTR_NEED_REVAL)
		cifs_i->time = 0;
	else
		cifs_i->time = jiffies;

	if (fattr->cf_flags & CIFS_FATTR_DELETE_PENDING)
		set_bit(CIFS_INO_DELETE_PENDING, &cifs_i->flags);
	else
		clear_bit(CIFS_INO_DELETE_PENDING, &cifs_i->flags);

	cifs_i->server_eof = fattr->cf_eof;
	/*
	 * Can't safely change the file size here if the client is writing to
	 * it due to potential races.
	 */
	if (is_size_safe_to_change(cifs_i, fattr->cf_eof)) {
		i_size_write(iyesde, fattr->cf_eof);

		/*
		 * i_blocks is yest related to (i_size / i_blksize),
		 * but instead 512 byte (2**9) size is required for
		 * calculating num blocks.
		 */
		iyesde->i_blocks = (512 - 1 + fattr->cf_bytes) >> 9;
	}
	spin_unlock(&iyesde->i_lock);

	if (fattr->cf_flags & CIFS_FATTR_DFS_REFERRAL)
		iyesde->i_flags |= S_AUTOMOUNT;
	if (iyesde->i_state & I_NEW)
		cifs_set_ops(iyesde);
}

void
cifs_fill_uniqueid(struct super_block *sb, struct cifs_fattr *fattr)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)
		return;

	fattr->cf_uniqueid = iunique(sb, ROOT_I);
}

/* Fill a cifs_fattr struct with info from FILE_UNIX_BASIC_INFO. */
void
cifs_unix_basic_to_fattr(struct cifs_fattr *fattr, FILE_UNIX_BASIC_INFO *info,
			 struct cifs_sb_info *cifs_sb)
{
	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_uniqueid = le64_to_cpu(info->UniqueId);
	fattr->cf_bytes = le64_to_cpu(info->NumOfBytes);
	fattr->cf_eof = le64_to_cpu(info->EndOfFile);

	fattr->cf_atime = cifs_NTtimeToUnix(info->LastAccessTime);
	fattr->cf_mtime = cifs_NTtimeToUnix(info->LastModificationTime);
	fattr->cf_ctime = cifs_NTtimeToUnix(info->LastStatusChange);
	/* old POSIX extensions don't get create time */

	fattr->cf_mode = le64_to_cpu(info->Permissions);

	/*
	 * Since we set the iyesde type below we need to mask off
	 * to avoid strange results if bits set above.
	 */
	fattr->cf_mode &= ~S_IFMT;
	switch (le32_to_cpu(info->Type)) {
	case UNIX_FILE:
		fattr->cf_mode |= S_IFREG;
		fattr->cf_dtype = DT_REG;
		break;
	case UNIX_SYMLINK:
		fattr->cf_mode |= S_IFLNK;
		fattr->cf_dtype = DT_LNK;
		break;
	case UNIX_DIR:
		fattr->cf_mode |= S_IFDIR;
		fattr->cf_dtype = DT_DIR;
		break;
	case UNIX_CHARDEV:
		fattr->cf_mode |= S_IFCHR;
		fattr->cf_dtype = DT_CHR;
		fattr->cf_rdev = MKDEV(le64_to_cpu(info->DevMajor),
				       le64_to_cpu(info->DevMiyesr) & MINORMASK);
		break;
	case UNIX_BLOCKDEV:
		fattr->cf_mode |= S_IFBLK;
		fattr->cf_dtype = DT_BLK;
		fattr->cf_rdev = MKDEV(le64_to_cpu(info->DevMajor),
				       le64_to_cpu(info->DevMiyesr) & MINORMASK);
		break;
	case UNIX_FIFO:
		fattr->cf_mode |= S_IFIFO;
		fattr->cf_dtype = DT_FIFO;
		break;
	case UNIX_SOCKET:
		fattr->cf_mode |= S_IFSOCK;
		fattr->cf_dtype = DT_SOCK;
		break;
	default:
		/* safest to call it a file if we do yest kyesw */
		fattr->cf_mode |= S_IFREG;
		fattr->cf_dtype = DT_REG;
		cifs_dbg(FYI, "unkyeswn type %d\n", le32_to_cpu(info->Type));
		break;
	}

	fattr->cf_uid = cifs_sb->mnt_uid;
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_UID)) {
		u64 id = le64_to_cpu(info->Uid);
		if (id < ((uid_t)-1)) {
			kuid_t uid = make_kuid(&init_user_ns, id);
			if (uid_valid(uid))
				fattr->cf_uid = uid;
		}
	}
	
	fattr->cf_gid = cifs_sb->mnt_gid;
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_GID)) {
		u64 id = le64_to_cpu(info->Gid);
		if (id < ((gid_t)-1)) {
			kgid_t gid = make_kgid(&init_user_ns, id);
			if (gid_valid(gid))
				fattr->cf_gid = gid;
		}
	}

	fattr->cf_nlink = le64_to_cpu(info->Nlinks);
}

/*
 * Fill a cifs_fattr struct with fake iyesde info.
 *
 * Needed to setup cifs_fattr data for the directory which is the
 * junction to the new submount (ie to setup the fake directory
 * which represents a DFS referral).
 */
static void
cifs_create_dfs_fattr(struct cifs_fattr *fattr, struct super_block *sb)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);

	cifs_dbg(FYI, "creating fake fattr for DFS referral\n");

	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_mode = S_IFDIR | S_IXUGO | S_IRWXU;
	fattr->cf_uid = cifs_sb->mnt_uid;
	fattr->cf_gid = cifs_sb->mnt_gid;
	ktime_get_real_ts64(&fattr->cf_mtime);
	fattr->cf_mtime = timespec64_trunc(fattr->cf_mtime, sb->s_time_gran);
	fattr->cf_atime = fattr->cf_ctime = fattr->cf_mtime;
	fattr->cf_nlink = 2;
	fattr->cf_flags = CIFS_FATTR_DFS_REFERRAL;
}

static int
cifs_get_file_info_unix(struct file *filp)
{
	int rc;
	unsigned int xid;
	FILE_UNIX_BASIC_INFO find_data;
	struct cifs_fattr fattr;
	struct iyesde *iyesde = file_iyesde(filp);
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);
	struct cifsFileInfo *cfile = filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);

	xid = get_xid();
	rc = CIFSSMBUnixQFileInfo(xid, tcon, cfile->fid.netfid, &find_data);
	if (!rc) {
		cifs_unix_basic_to_fattr(&fattr, &find_data, cifs_sb);
	} else if (rc == -EREMOTE) {
		cifs_create_dfs_fattr(&fattr, iyesde->i_sb);
		rc = 0;
	}

	cifs_fattr_to_iyesde(iyesde, &fattr);
	free_xid(xid);
	return rc;
}

int cifs_get_iyesde_info_unix(struct iyesde **piyesde,
			     const unsigned char *full_path,
			     struct super_block *sb, unsigned int xid)
{
	int rc;
	FILE_UNIX_BASIC_INFO find_data;
	struct cifs_fattr fattr;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);

	cifs_dbg(FYI, "Getting info on %s\n", full_path);

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	/* could have done a find first instead but this returns more info */
	rc = CIFSSMBUnixQPathInfo(xid, tcon, full_path, &find_data,
				  cifs_sb->local_nls, cifs_remap(cifs_sb));
	cifs_put_tlink(tlink);

	if (!rc) {
		cifs_unix_basic_to_fattr(&fattr, &find_data, cifs_sb);
	} else if (rc == -EREMOTE) {
		cifs_create_dfs_fattr(&fattr, sb);
		rc = 0;
	} else {
		return rc;
	}

	/* check for Minshall+French symlinks */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS) {
		int tmprc = check_mf_symlink(xid, tcon, cifs_sb, &fattr,
					     full_path);
		if (tmprc)
			cifs_dbg(FYI, "check_mf_symlink: %d\n", tmprc);
	}

	if (*piyesde == NULL) {
		/* get new iyesde */
		cifs_fill_uniqueid(sb, &fattr);
		*piyesde = cifs_iget(sb, &fattr);
		if (!*piyesde)
			rc = -ENOMEM;
	} else {
		/* we already have iyesde, update it */

		/* if uniqueid is different, return error */
		if (unlikely(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM &&
		    CIFS_I(*piyesde)->uniqueid != fattr.cf_uniqueid)) {
			CIFS_I(*piyesde)->time = 0; /* force reval */
			rc = -ESTALE;
			goto cgiiu_exit;
		}

		/* if filetype is different, return error */
		if (unlikely(((*piyesde)->i_mode & S_IFMT) !=
		    (fattr.cf_mode & S_IFMT))) {
			CIFS_I(*piyesde)->time = 0; /* force reval */
			rc = -ESTALE;
			goto cgiiu_exit;
		}

		cifs_fattr_to_iyesde(*piyesde, &fattr);
	}

cgiiu_exit:
	return rc;
}

static int
cifs_sfu_type(struct cifs_fattr *fattr, const char *path,
	      struct cifs_sb_info *cifs_sb, unsigned int xid)
{
	int rc;
	__u32 oplock;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	struct cifs_io_parms io_parms;
	char buf[24];
	unsigned int bytes_read;
	char *pbuf;
	int buf_type = CIFS_NO_BUFFER;

	pbuf = buf;

	fattr->cf_mode &= ~S_IFMT;

	if (fattr->cf_eof == 0) {
		fattr->cf_mode |= S_IFIFO;
		fattr->cf_dtype = DT_FIFO;
		return 0;
	} else if (fattr->cf_eof < 8) {
		fattr->cf_mode |= S_IFREG;
		fattr->cf_dtype = DT_REG;
		return -EINVAL;	 /* EOPNOTSUPP? */
	}

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	oparms.desired_access = GENERIC_READ;
	oparms.create_options = CREATE_NOT_DIR;
	if (backup_cred(cifs_sb))
		oparms.create_options |= CREATE_OPEN_BACKUP_INTENT;
	oparms.disposition = FILE_OPEN;
	oparms.path = path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	if (tcon->ses->server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;
	rc = tcon->ses->server->ops->open(xid, &oparms, &oplock, NULL);
	if (rc) {
		cifs_dbg(FYI, "check sfu type of %s, open rc = %d\n", path, rc);
		cifs_put_tlink(tlink);
		return rc;
	}

	/* Read header */
	io_parms.netfid = fid.netfid;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.offset = 0;
	io_parms.length = 24;

	rc = tcon->ses->server->ops->sync_read(xid, &fid, &io_parms,
					&bytes_read, &pbuf, &buf_type);
	if ((rc == 0) && (bytes_read >= 8)) {
		if (memcmp("IntxBLK", pbuf, 8) == 0) {
			cifs_dbg(FYI, "Block device\n");
			fattr->cf_mode |= S_IFBLK;
			fattr->cf_dtype = DT_BLK;
			if (bytes_read == 24) {
				/* we have eyesugh to decode dev num */
				__u64 mjr; /* major */
				__u64 mnr; /* miyesr */
				mjr = le64_to_cpu(*(__le64 *)(pbuf+8));
				mnr = le64_to_cpu(*(__le64 *)(pbuf+16));
				fattr->cf_rdev = MKDEV(mjr, mnr);
			}
		} else if (memcmp("IntxCHR", pbuf, 8) == 0) {
			cifs_dbg(FYI, "Char device\n");
			fattr->cf_mode |= S_IFCHR;
			fattr->cf_dtype = DT_CHR;
			if (bytes_read == 24) {
				/* we have eyesugh to decode dev num */
				__u64 mjr; /* major */
				__u64 mnr; /* miyesr */
				mjr = le64_to_cpu(*(__le64 *)(pbuf+8));
				mnr = le64_to_cpu(*(__le64 *)(pbuf+16));
				fattr->cf_rdev = MKDEV(mjr, mnr);
			}
		} else if (memcmp("IntxLNK", pbuf, 7) == 0) {
			cifs_dbg(FYI, "Symlink\n");
			fattr->cf_mode |= S_IFLNK;
			fattr->cf_dtype = DT_LNK;
		} else {
			fattr->cf_mode |= S_IFREG; /* file? */
			fattr->cf_dtype = DT_REG;
			rc = -EOPNOTSUPP;
		}
	} else {
		fattr->cf_mode |= S_IFREG; /* then it is a file */
		fattr->cf_dtype = DT_REG;
		rc = -EOPNOTSUPP; /* or some unkyeswn SFU type */
	}

	tcon->ses->server->ops->close(xid, tcon, &fid);
	cifs_put_tlink(tlink);
	return rc;
}

#define SFBITS_MASK (S_ISVTX | S_ISGID | S_ISUID)  /* SETFILEBITS valid bits */

/*
 * Fetch mode bits as provided by SFU.
 *
 * FIXME: Doesn't this clobber the type bit we got from cifs_sfu_type ?
 */
static int cifs_sfu_mode(struct cifs_fattr *fattr, const unsigned char *path,
			 struct cifs_sb_info *cifs_sb, unsigned int xid)
{
#ifdef CONFIG_CIFS_XATTR
	ssize_t rc;
	char ea_value[4];
	__u32 mode;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	if (tcon->ses->server->ops->query_all_EAs == NULL) {
		cifs_put_tlink(tlink);
		return -EOPNOTSUPP;
	}

	rc = tcon->ses->server->ops->query_all_EAs(xid, tcon, path,
			"SETFILEBITS", ea_value, 4 /* size of buf */,
			cifs_sb);
	cifs_put_tlink(tlink);
	if (rc < 0)
		return (int)rc;
	else if (rc > 3) {
		mode = le32_to_cpu(*((__le32 *)ea_value));
		fattr->cf_mode &= ~SFBITS_MASK;
		cifs_dbg(FYI, "special bits 0%o org mode 0%o\n",
			 mode, fattr->cf_mode);
		fattr->cf_mode = (mode & SFBITS_MASK) | fattr->cf_mode;
		cifs_dbg(FYI, "special mode bits 0%o\n", mode);
	}

	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

/* Fill a cifs_fattr struct with info from FILE_ALL_INFO */
static void
cifs_all_info_to_fattr(struct cifs_fattr *fattr, FILE_ALL_INFO *info,
		       struct super_block *sb, bool adjust_tz,
		       bool symlink)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);

	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_cifsattrs = le32_to_cpu(info->Attributes);
	if (info->DeletePending)
		fattr->cf_flags |= CIFS_FATTR_DELETE_PENDING;

	if (info->LastAccessTime)
		fattr->cf_atime = cifs_NTtimeToUnix(info->LastAccessTime);
	else {
		ktime_get_real_ts64(&fattr->cf_atime);
		fattr->cf_atime = timespec64_trunc(fattr->cf_atime, sb->s_time_gran);
	}

	fattr->cf_ctime = cifs_NTtimeToUnix(info->ChangeTime);
	fattr->cf_mtime = cifs_NTtimeToUnix(info->LastWriteTime);

	if (adjust_tz) {
		fattr->cf_ctime.tv_sec += tcon->ses->server->timeAdj;
		fattr->cf_mtime.tv_sec += tcon->ses->server->timeAdj;
	}

	fattr->cf_eof = le64_to_cpu(info->EndOfFile);
	fattr->cf_bytes = le64_to_cpu(info->AllocationSize);
	fattr->cf_createtime = le64_to_cpu(info->CreationTime);

	fattr->cf_nlink = le32_to_cpu(info->NumberOfLinks);

	if (symlink) {
		fattr->cf_mode = S_IFLNK;
		fattr->cf_dtype = DT_LNK;
	} else if (fattr->cf_cifsattrs & ATTR_DIRECTORY) {
		fattr->cf_mode = S_IFDIR | cifs_sb->mnt_dir_mode;
		fattr->cf_dtype = DT_DIR;
		/*
		 * Server can return wrong NumberOfLinks value for directories
		 * when Unix extensions are disabled - fake it.
		 */
		if (!tcon->unix_ext)
			fattr->cf_flags |= CIFS_FATTR_UNKNOWN_NLINK;
	} else {
		fattr->cf_mode = S_IFREG | cifs_sb->mnt_file_mode;
		fattr->cf_dtype = DT_REG;

		/* clear write bits if ATTR_READONLY is set */
		if (fattr->cf_cifsattrs & ATTR_READONLY)
			fattr->cf_mode &= ~(S_IWUGO);

		/*
		 * Don't accept zero nlink from yesn-unix servers unless
		 * delete is pending.  Instead mark it as unkyeswn.
		 */
		if ((fattr->cf_nlink < 1) && !tcon->unix_ext &&
		    !info->DeletePending) {
			cifs_dbg(1, "bogus file nlink value %u\n",
				fattr->cf_nlink);
			fattr->cf_flags |= CIFS_FATTR_UNKNOWN_NLINK;
		}
	}

	fattr->cf_uid = cifs_sb->mnt_uid;
	fattr->cf_gid = cifs_sb->mnt_gid;
}

static int
cifs_get_file_info(struct file *filp)
{
	int rc;
	unsigned int xid;
	FILE_ALL_INFO find_data;
	struct cifs_fattr fattr;
	struct iyesde *iyesde = file_iyesde(filp);
	struct cifsFileInfo *cfile = filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;

	if (!server->ops->query_file_info)
		return -ENOSYS;

	xid = get_xid();
	rc = server->ops->query_file_info(xid, tcon, &cfile->fid, &find_data);
	switch (rc) {
	case 0:
		cifs_all_info_to_fattr(&fattr, &find_data, iyesde->i_sb, false,
				       false);
		break;
	case -EREMOTE:
		cifs_create_dfs_fattr(&fattr, iyesde->i_sb);
		rc = 0;
		break;
	case -EOPNOTSUPP:
	case -EINVAL:
		/*
		 * FIXME: legacy server -- fall back to path-based call?
		 * for yesw, just skip revalidating and mark iyesde for
		 * immediate reval.
		 */
		rc = 0;
		CIFS_I(iyesde)->time = 0;
	default:
		goto cgfi_exit;
	}

	/*
	 * don't bother with SFU junk here -- just mark iyesde as needing
	 * revalidation.
	 */
	fattr.cf_uniqueid = CIFS_I(iyesde)->uniqueid;
	fattr.cf_flags |= CIFS_FATTR_NEED_REVAL;
	cifs_fattr_to_iyesde(iyesde, &fattr);
cgfi_exit:
	free_xid(xid);
	return rc;
}

/* Simple function to return a 64 bit hash of string.  Rarely called */
static __u64 simple_hashstr(const char *str)
{
	const __u64 hash_mult =  1125899906842597ULL; /* a big eyesugh prime */
	__u64 hash = 0;

	while (*str)
		hash = (hash + (__u64) *str++) * hash_mult;

	return hash;
}

/**
 * cifs_backup_query_path_info - SMB1 fallback code to get iyes
 *
 * Fallback code to get file metadata when we don't have access to
 * @full_path (EACCESS) and have backup creds.
 *
 * @data will be set to search info result buffer
 * @resp_buf will be set to cifs resp buf and needs to be freed with
 * cifs_buf_release() when done with @data.
 */
static int
cifs_backup_query_path_info(int xid,
			    struct cifs_tcon *tcon,
			    struct super_block *sb,
			    const char *full_path,
			    void **resp_buf,
			    FILE_ALL_INFO **data)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_search_info info = {0};
	u16 flags;
	int rc;

	*resp_buf = NULL;
	info.endOfSearch = false;
	if (tcon->unix_ext)
		info.info_level = SMB_FIND_FILE_UNIX;
	else if ((tcon->ses->capabilities &
		  tcon->ses->server->vals->cap_nt_find) == 0)
		info.info_level = SMB_FIND_FILE_INFO_STANDARD;
	else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)
		info.info_level = SMB_FIND_FILE_ID_FULL_DIR_INFO;
	else /* yes srviyes useful for fallback to some netapp */
		info.info_level = SMB_FIND_FILE_DIRECTORY_INFO;

	flags = CIFS_SEARCH_CLOSE_ALWAYS |
		CIFS_SEARCH_CLOSE_AT_END |
		CIFS_SEARCH_BACKUP_SEARCH;

	rc = CIFSFindFirst(xid, tcon, full_path,
			   cifs_sb, NULL, flags, &info, false);
	if (rc)
		return rc;

	*resp_buf = (void *)info.ntwrk_buf_start;
	*data = (FILE_ALL_INFO *)info.srch_entries_start;
	return 0;
}

static void
cifs_set_fattr_iyes(int xid,
		   struct cifs_tcon *tcon,
		   struct super_block *sb,
		   struct iyesde **iyesde,
		   const char *full_path,
		   FILE_ALL_INFO *data,
		   struct cifs_fattr *fattr)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct TCP_Server_Info *server = tcon->ses->server;
	int rc;

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)) {
		if (*iyesde)
			fattr->cf_uniqueid = CIFS_I(*iyesde)->uniqueid;
		else
			fattr->cf_uniqueid = iunique(sb, ROOT_I);
		return;
	}

	/*
	 * If we have an iyesde pass a NULL tcon to ensure we don't
	 * make a round trip to the server. This only works for SMB2+.
	 */
	rc = server->ops->get_srv_inum(xid,
				       *iyesde ? NULL : tcon,
				       cifs_sb, full_path,
				       &fattr->cf_uniqueid,
				       data);
	if (rc) {
		/*
		 * If that fails reuse existing iyes or generate one
		 * and disable server ones
		 */
		if (*iyesde)
			fattr->cf_uniqueid = CIFS_I(*iyesde)->uniqueid;
		else {
			fattr->cf_uniqueid = iunique(sb, ROOT_I);
			cifs_autodisable_serveriyes(cifs_sb);
		}
		return;
	}

	/* If yes errors, check for zero root iyesde (invalid) */
	if (fattr->cf_uniqueid == 0 && strlen(full_path) == 0) {
		cifs_dbg(FYI, "Invalid (0) iyesdenum\n");
		if (*iyesde) {
			/* reuse */
			fattr->cf_uniqueid = CIFS_I(*iyesde)->uniqueid;
		} else {
			/* make an iyes by hashing the UNC */
			fattr->cf_flags |= CIFS_FATTR_FAKE_ROOT_INO;
			fattr->cf_uniqueid = simple_hashstr(tcon->treeName);
		}
	}
}

static inline bool is_iyesde_cache_good(struct iyesde *iyes)
{
	return iyes && CIFS_CACHE_READ(CIFS_I(iyes)) && CIFS_I(iyes)->time != 0;
}

int
cifs_get_iyesde_info(struct iyesde **iyesde,
		    const char *full_path,
		    FILE_ALL_INFO *in_data,
		    struct super_block *sb, int xid,
		    const struct cifs_fid *fid)
{

	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct tcon_link *tlink;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	bool adjust_tz = false;
	struct cifs_fattr fattr = {0};
	bool symlink = false;
	FILE_ALL_INFO *data = in_data;
	FILE_ALL_INFO *tmp_data = NULL;
	void *smb1_backup_rsp_buf = NULL;
	int rc = 0;
	int tmprc = 0;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	/*
	 * 1. Fetch file metadata if yest provided (data)
	 */

	if (!data) {
		if (is_iyesde_cache_good(*iyesde)) {
			cifs_dbg(FYI, "No need to revalidate cached iyesde sizes\n");
			goto out;
		}
		tmp_data = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
		if (!tmp_data) {
			rc = -ENOMEM;
			goto out;
		}
		rc = server->ops->query_path_info(xid, tcon, cifs_sb,
						  full_path, tmp_data,
						  &adjust_tz, &symlink);
		data = tmp_data;
	}

	/*
	 * 2. Convert it to internal cifs metadata (fattr)
	 */

	switch (rc) {
	case 0:
		cifs_all_info_to_fattr(&fattr, data, sb, adjust_tz, symlink);
		break;
	case -EREMOTE:
		/* DFS link, yes metadata available on this server */
		cifs_create_dfs_fattr(&fattr, sb);
		rc = 0;
		break;
	case -EACCES:
		/*
		 * perm errors, try again with backup flags if possible
		 *
		 * For SMB2 and later the backup intent flag
		 * is already sent if needed on open and there
		 * is yes path based FindFirst operation to use
		 * to retry with
		 */
		if (backup_cred(cifs_sb) && is_smb1_server(server)) {
			/* for easier reading */
			FILE_DIRECTORY_INFO *fdi;
			SEARCH_ID_FULL_DIR_INFO *si;

			rc = cifs_backup_query_path_info(xid, tcon, sb,
							 full_path,
							 &smb1_backup_rsp_buf,
							 &data);
			if (rc)
				goto out;

			fdi = (FILE_DIRECTORY_INFO *)data;
			si = (SEARCH_ID_FULL_DIR_INFO *)data;

			cifs_dir_info_to_fattr(&fattr, fdi, cifs_sb);
			fattr.cf_uniqueid = le64_to_cpu(si->UniqueId);
			/* uniqueid set, skip get inum step */
			goto handle_mnt_opt;
		} else {
			/* yesthing we can do, bail out */
			goto out;
		}
		break;
	default:
		cifs_dbg(FYI, "%s: unhandled err rc %d\n", __func__, rc);
		goto out;
	}

	/*
	 * 3. Get or update iyesde number (fattr.cf_uniqueid)
	 */

	cifs_set_fattr_iyes(xid, tcon, sb, iyesde, full_path, data, &fattr);

	/*
	 * 4. Tweak fattr based on mount options
	 */

handle_mnt_opt:
	/* query for SFU type info if supported and needed */
	if (fattr.cf_cifsattrs & ATTR_SYSTEM &&
	    cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) {
		tmprc = cifs_sfu_type(&fattr, full_path, cifs_sb, xid);
		if (tmprc)
			cifs_dbg(FYI, "cifs_sfu_type failed: %d\n", tmprc);
	}

	/* fill in 0777 bits from ACL */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MODE_FROM_SID) {
		rc = cifs_acl_to_fattr(cifs_sb, &fattr, *iyesde, true,
				       full_path, fid);
		if (rc) {
			cifs_dbg(FYI, "%s: Get mode from SID failed. rc=%d\n",
				 __func__, rc);
			goto out;
		}
	} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) {
		rc = cifs_acl_to_fattr(cifs_sb, &fattr, *iyesde, false,
				       full_path, fid);
		if (rc) {
			cifs_dbg(FYI, "%s: Getting ACL failed with error: %d\n",
				 __func__, rc);
			goto out;
		}
	}

	/* fill in remaining high mode bits e.g. SUID, VTX */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL)
		cifs_sfu_mode(&fattr, full_path, cifs_sb, xid);

	/* check for Minshall+French symlinks */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS) {
		tmprc = check_mf_symlink(xid, tcon, cifs_sb, &fattr,
					 full_path);
		if (tmprc)
			cifs_dbg(FYI, "check_mf_symlink: %d\n", tmprc);
	}

	/*
	 * 5. Update iyesde with final fattr data
	 */

	if (!*iyesde) {
		*iyesde = cifs_iget(sb, &fattr);
		if (!*iyesde)
			rc = -ENOMEM;
	} else {
		/* we already have iyesde, update it */

		/* if uniqueid is different, return error */
		if (unlikely(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM &&
		    CIFS_I(*iyesde)->uniqueid != fattr.cf_uniqueid)) {
			CIFS_I(*iyesde)->time = 0; /* force reval */
			rc = -ESTALE;
			goto out;
		}

		/* if filetype is different, return error */
		if (unlikely(((*iyesde)->i_mode & S_IFMT) !=
		    (fattr.cf_mode & S_IFMT))) {
			CIFS_I(*iyesde)->time = 0; /* force reval */
			rc = -ESTALE;
			goto out;
		}

		cifs_fattr_to_iyesde(*iyesde, &fattr);
	}
out:
	cifs_buf_release(smb1_backup_rsp_buf);
	cifs_put_tlink(tlink);
	kfree(tmp_data);
	return rc;
}

static const struct iyesde_operations cifs_ipc_iyesde_ops = {
	.lookup = cifs_lookup,
};

static int
cifs_find_iyesde(struct iyesde *iyesde, void *opaque)
{
	struct cifs_fattr *fattr = (struct cifs_fattr *) opaque;

	/* don't match iyesde with different uniqueid */
	if (CIFS_I(iyesde)->uniqueid != fattr->cf_uniqueid)
		return 0;

	/* use createtime like an i_generation field */
	if (CIFS_I(iyesde)->createtime != fattr->cf_createtime)
		return 0;

	/* don't match iyesde of different type */
	if ((iyesde->i_mode & S_IFMT) != (fattr->cf_mode & S_IFMT))
		return 0;

	/* if it's yest a directory or has yes dentries, then flag it */
	if (S_ISDIR(iyesde->i_mode) && !hlist_empty(&iyesde->i_dentry))
		fattr->cf_flags |= CIFS_FATTR_INO_COLLISION;

	return 1;
}

static int
cifs_init_iyesde(struct iyesde *iyesde, void *opaque)
{
	struct cifs_fattr *fattr = (struct cifs_fattr *) opaque;

	CIFS_I(iyesde)->uniqueid = fattr->cf_uniqueid;
	CIFS_I(iyesde)->createtime = fattr->cf_createtime;
	return 0;
}

/*
 * walk dentry list for an iyesde and report whether it has aliases that
 * are hashed. We use this to determine if a directory iyesde can actually
 * be used.
 */
static bool
iyesde_has_hashed_dentries(struct iyesde *iyesde)
{
	struct dentry *dentry;

	spin_lock(&iyesde->i_lock);
	hlist_for_each_entry(dentry, &iyesde->i_dentry, d_u.d_alias) {
		if (!d_unhashed(dentry) || IS_ROOT(dentry)) {
			spin_unlock(&iyesde->i_lock);
			return true;
		}
	}
	spin_unlock(&iyesde->i_lock);
	return false;
}

/* Given fattrs, get a corresponding iyesde */
struct iyesde *
cifs_iget(struct super_block *sb, struct cifs_fattr *fattr)
{
	unsigned long hash;
	struct iyesde *iyesde;

retry_iget5_locked:
	cifs_dbg(FYI, "looking for uniqueid=%llu\n", fattr->cf_uniqueid);

	/* hash down to 32-bits on 32-bit arch */
	hash = cifs_uniqueid_to_iyes_t(fattr->cf_uniqueid);

	iyesde = iget5_locked(sb, hash, cifs_find_iyesde, cifs_init_iyesde, fattr);
	if (iyesde) {
		/* was there a potentially problematic iyesde collision? */
		if (fattr->cf_flags & CIFS_FATTR_INO_COLLISION) {
			fattr->cf_flags &= ~CIFS_FATTR_INO_COLLISION;

			if (iyesde_has_hashed_dentries(iyesde)) {
				cifs_autodisable_serveriyes(CIFS_SB(sb));
				iput(iyesde);
				fattr->cf_uniqueid = iunique(sb, ROOT_I);
				goto retry_iget5_locked;
			}
		}

		cifs_fattr_to_iyesde(iyesde, fattr);
		if (sb->s_flags & SB_NOATIME)
			iyesde->i_flags |= S_NOATIME | S_NOCMTIME;
		if (iyesde->i_state & I_NEW) {
			iyesde->i_iyes = hash;
#ifdef CONFIG_CIFS_FSCACHE
			/* initialize per-iyesde cache cookie pointer */
			CIFS_I(iyesde)->fscache = NULL;
#endif
			unlock_new_iyesde(iyesde);
		}
	}

	return iyesde;
}

/* gets root iyesde */
struct iyesde *cifs_root_iget(struct super_block *sb)
{
	unsigned int xid;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct iyesde *iyesde = NULL;
	long rc;
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	char *path = NULL;
	int len;

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_USE_PREFIX_PATH)
	    && cifs_sb->prepath) {
		len = strlen(cifs_sb->prepath);
		path = kzalloc(len + 2 /* leading sep + null */, GFP_KERNEL);
		if (path == NULL)
			return ERR_PTR(-ENOMEM);
		path[0] = '/';
		memcpy(path+1, cifs_sb->prepath, len);
	} else {
		path = kstrdup("", GFP_KERNEL);
		if (path == NULL)
			return ERR_PTR(-ENOMEM);
	}

	xid = get_xid();
	if (tcon->unix_ext) {
		rc = cifs_get_iyesde_info_unix(&iyesde, path, sb, xid);
		/* some servers mistakenly claim POSIX support */
		if (rc != -EOPNOTSUPP)
			goto iget_yes_retry;
		cifs_dbg(VFS, "server does yest support POSIX extensions");
		tcon->unix_ext = false;
	}

	convert_delimiter(path, CIFS_DIR_SEP(cifs_sb));
	rc = cifs_get_iyesde_info(&iyesde, path, NULL, sb, xid, NULL);

iget_yes_retry:
	if (!iyesde) {
		iyesde = ERR_PTR(rc);
		goto out;
	}

#ifdef CONFIG_CIFS_FSCACHE
	/* populate tcon->resource_id */
	tcon->resource_id = CIFS_I(iyesde)->uniqueid;
#endif

	if (rc && tcon->pipe) {
		cifs_dbg(FYI, "ipc connection - fake read iyesde\n");
		spin_lock(&iyesde->i_lock);
		iyesde->i_mode |= S_IFDIR;
		set_nlink(iyesde, 2);
		iyesde->i_op = &cifs_ipc_iyesde_ops;
		iyesde->i_fop = &simple_dir_operations;
		iyesde->i_uid = cifs_sb->mnt_uid;
		iyesde->i_gid = cifs_sb->mnt_gid;
		spin_unlock(&iyesde->i_lock);
	} else if (rc) {
		iget_failed(iyesde);
		iyesde = ERR_PTR(rc);
	}

out:
	kfree(path);
	free_xid(xid);
	return iyesde;
}

int
cifs_set_file_info(struct iyesde *iyesde, struct iattr *attrs, unsigned int xid,
		   char *full_path, __u32 dosattr)
{
	bool set_time = false;
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);
	struct TCP_Server_Info *server;
	FILE_BASIC_INFO	info_buf;

	if (attrs == NULL)
		return -EINVAL;

	server = cifs_sb_master_tcon(cifs_sb)->ses->server;
	if (!server->ops->set_file_info)
		return -ENOSYS;

	info_buf.Pad = 0;

	if (attrs->ia_valid & ATTR_ATIME) {
		set_time = true;
		info_buf.LastAccessTime =
			cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_atime));
	} else
		info_buf.LastAccessTime = 0;

	if (attrs->ia_valid & ATTR_MTIME) {
		set_time = true;
		info_buf.LastWriteTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_mtime));
	} else
		info_buf.LastWriteTime = 0;

	/*
	 * Samba throws this field away, but windows may actually use it.
	 * Do yest set ctime unless other time stamps are changed explicitly
	 * (i.e. by utimes()) since we would then have a mix of client and
	 * server times.
	 */
	if (set_time && (attrs->ia_valid & ATTR_CTIME)) {
		cifs_dbg(FYI, "CIFS - CTIME changed\n");
		info_buf.ChangeTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_ctime));
	} else
		info_buf.ChangeTime = 0;

	info_buf.CreationTime = 0;	/* don't change */
	info_buf.Attributes = cpu_to_le32(dosattr);

	return server->ops->set_file_info(iyesde, full_path, &info_buf, xid);
}

/*
 * Open the given file (if it isn't already), set the DELETE_ON_CLOSE bit
 * and rename it to a random name that hopefully won't conflict with
 * anything else.
 */
int
cifs_rename_pending_delete(const char *full_path, struct dentry *dentry,
			   const unsigned int xid)
{
	int oplock = 0;
	int rc;
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct cifsIyesdeInfo *cifsIyesde = CIFS_I(iyesde);
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	__u32 dosattr, origattr;
	FILE_BASIC_INFO *info_buf = NULL;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	/*
	 * We canyest rename the file if the server doesn't support
	 * CAP_INFOLEVEL_PASSTHRU
	 */
	if (!(tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)) {
		rc = -EBUSY;
		goto out;
	}

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	oparms.desired_access = DELETE | FILE_WRITE_ATTRIBUTES;
	oparms.create_options = CREATE_NOT_DIR;
	oparms.disposition = FILE_OPEN;
	oparms.path = full_path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = CIFS_open(xid, &oparms, &oplock, NULL);
	if (rc != 0)
		goto out;

	origattr = cifsIyesde->cifsAttrs;
	if (origattr == 0)
		origattr |= ATTR_NORMAL;

	dosattr = origattr & ~ATTR_READONLY;
	if (dosattr == 0)
		dosattr |= ATTR_NORMAL;
	dosattr |= ATTR_HIDDEN;

	/* set ATTR_HIDDEN and clear ATTR_READONLY, but only if needed */
	if (dosattr != origattr) {
		info_buf = kzalloc(sizeof(*info_buf), GFP_KERNEL);
		if (info_buf == NULL) {
			rc = -ENOMEM;
			goto out_close;
		}
		info_buf->Attributes = cpu_to_le32(dosattr);
		rc = CIFSSMBSetFileInfo(xid, tcon, info_buf, fid.netfid,
					current->tgid);
		/* although we would like to mark the file hidden
 		   if that fails we will still try to rename it */
		if (!rc)
			cifsIyesde->cifsAttrs = dosattr;
		else
			dosattr = origattr; /* since yest able to change them */
	}

	/* rename the file */
	rc = CIFSSMBRenameOpenFile(xid, tcon, fid.netfid, NULL,
				   cifs_sb->local_nls,
				   cifs_remap(cifs_sb));
	if (rc != 0) {
		rc = -EBUSY;
		goto undo_setattr;
	}

	/* try to set DELETE_ON_CLOSE */
	if (!test_bit(CIFS_INO_DELETE_PENDING, &cifsIyesde->flags)) {
		rc = CIFSSMBSetFileDisposition(xid, tcon, true, fid.netfid,
					       current->tgid);
		/*
		 * some samba versions return -ENOENT when we try to set the
		 * file disposition here. Likely a samba bug, but work around
		 * it for yesw. This means that some cifsXXX files may hang
		 * around after they shouldn't.
		 *
		 * BB: remove this hack after more servers have the fix
		 */
		if (rc == -ENOENT)
			rc = 0;
		else if (rc != 0) {
			rc = -EBUSY;
			goto undo_rename;
		}
		set_bit(CIFS_INO_DELETE_PENDING, &cifsIyesde->flags);
	}

out_close:
	CIFSSMBClose(xid, tcon, fid.netfid);
out:
	kfree(info_buf);
	cifs_put_tlink(tlink);
	return rc;

	/*
	 * reset everything back to the original state. Don't bother
	 * dealing with errors here since we can't do anything about
	 * them anyway.
	 */
undo_rename:
	CIFSSMBRenameOpenFile(xid, tcon, fid.netfid, dentry->d_name.name,
				cifs_sb->local_nls, cifs_remap(cifs_sb));
undo_setattr:
	if (dosattr != origattr) {
		info_buf->Attributes = cpu_to_le32(origattr);
		if (!CIFSSMBSetFileInfo(xid, tcon, info_buf, fid.netfid,
					current->tgid))
			cifsIyesde->cifsAttrs = origattr;
	}

	goto out_close;
}

/* copied from fs/nfs/dir.c with small changes */
static void
cifs_drop_nlink(struct iyesde *iyesde)
{
	spin_lock(&iyesde->i_lock);
	if (iyesde->i_nlink > 0)
		drop_nlink(iyesde);
	spin_unlock(&iyesde->i_lock);
}

/*
 * If d_iyesde(dentry) is null (usually meaning the cached dentry
 * is a negative dentry) then we would attempt a standard SMB delete, but
 * if that fails we can yest attempt the fall back mechanisms on EACCES
 * but will return the EACCES to the caller. Note that the VFS does yest call
 * unlink on negative dentries currently.
 */
int cifs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	int rc = 0;
	unsigned int xid;
	char *full_path = NULL;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct cifsIyesdeInfo *cifs_iyesde;
	struct super_block *sb = dir->i_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct iattr *attrs = NULL;
	__u32 dosattr = 0, origattr = 0;

	cifs_dbg(FYI, "cifs_unlink, dir=0x%p, dentry=0x%p\n", dir, dentry);

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	xid = get_xid();

	/* Unlink can be called from rename so we can yest take the
	 * sb->s_vfs_rename_mutex here */
	full_path = build_path_from_dentry(dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto unlink_out;
	}

	if (cap_unix(tcon->ses) && (CIFS_UNIX_POSIX_PATH_OPS_CAP &
				le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		rc = CIFSPOSIXDelFile(xid, tcon, full_path,
			SMB_POSIX_UNLINK_FILE_TARGET, cifs_sb->local_nls,
			cifs_remap(cifs_sb));
		cifs_dbg(FYI, "posix del rc %d\n", rc);
		if ((rc == 0) || (rc == -ENOENT))
			goto psx_del_yes_retry;
	}

retry_std_delete:
	if (!server->ops->unlink) {
		rc = -ENOSYS;
		goto psx_del_yes_retry;
	}

	rc = server->ops->unlink(xid, tcon, full_path, cifs_sb);

psx_del_yes_retry:
	if (!rc) {
		if (iyesde)
			cifs_drop_nlink(iyesde);
	} else if (rc == -ENOENT) {
		d_drop(dentry);
	} else if (rc == -EBUSY) {
		if (server->ops->rename_pending_delete) {
			rc = server->ops->rename_pending_delete(full_path,
								dentry, xid);
			if (rc == 0)
				cifs_drop_nlink(iyesde);
		}
	} else if ((rc == -EACCES) && (dosattr == 0) && iyesde) {
		attrs = kzalloc(sizeof(*attrs), GFP_KERNEL);
		if (attrs == NULL) {
			rc = -ENOMEM;
			goto out_reval;
		}

		/* try to reset dos attributes */
		cifs_iyesde = CIFS_I(iyesde);
		origattr = cifs_iyesde->cifsAttrs;
		if (origattr == 0)
			origattr |= ATTR_NORMAL;
		dosattr = origattr & ~ATTR_READONLY;
		if (dosattr == 0)
			dosattr |= ATTR_NORMAL;
		dosattr |= ATTR_HIDDEN;

		rc = cifs_set_file_info(iyesde, attrs, xid, full_path, dosattr);
		if (rc != 0)
			goto out_reval;

		goto retry_std_delete;
	}

	/* undo the setattr if we errored out and it's needed */
	if (rc != 0 && dosattr != 0)
		cifs_set_file_info(iyesde, attrs, xid, full_path, origattr);

out_reval:
	if (iyesde) {
		cifs_iyesde = CIFS_I(iyesde);
		cifs_iyesde->time = 0;	/* will force revalidate to get info
					   when needed */
		iyesde->i_ctime = current_time(iyesde);
	}
	dir->i_ctime = dir->i_mtime = current_time(dir);
	cifs_iyesde = CIFS_I(dir);
	CIFS_I(dir)->time = 0;	/* force revalidate of dir as well */
unlink_out:
	kfree(full_path);
	kfree(attrs);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

static int
cifs_mkdir_qinfo(struct iyesde *parent, struct dentry *dentry, umode_t mode,
		 const char *full_path, struct cifs_sb_info *cifs_sb,
		 struct cifs_tcon *tcon, const unsigned int xid)
{
	int rc = 0;
	struct iyesde *iyesde = NULL;

	if (tcon->unix_ext)
		rc = cifs_get_iyesde_info_unix(&iyesde, full_path, parent->i_sb,
					      xid);
	else
		rc = cifs_get_iyesde_info(&iyesde, full_path, NULL, parent->i_sb,
					 xid, NULL);

	if (rc)
		return rc;

	/*
	 * setting nlink yest necessary except in cases where we failed to get it
	 * from the server or was set bogus. Also, since this is a brand new
	 * iyesde, yes need to grab the i_lock before setting the i_nlink.
	 */
	if (iyesde->i_nlink < 2)
		set_nlink(iyesde, 2);
	mode &= ~current_umask();
	/* must turn on setgid bit if parent dir has it */
	if (parent->i_mode & S_ISGID)
		mode |= S_ISGID;

	if (tcon->unix_ext) {
		struct cifs_unix_set_info_args args = {
			.mode	= mode,
			.ctime	= NO_CHANGE_64,
			.atime	= NO_CHANGE_64,
			.mtime	= NO_CHANGE_64,
			.device	= 0,
		};
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
			args.uid = current_fsuid();
			if (parent->i_mode & S_ISGID)
				args.gid = parent->i_gid;
			else
				args.gid = current_fsgid();
		} else {
			args.uid = INVALID_UID; /* yes change */
			args.gid = INVALID_GID; /* yes change */
		}
		CIFSSMBUnixSetPathInfo(xid, tcon, full_path, &args,
				       cifs_sb->local_nls,
				       cifs_remap(cifs_sb));
	} else {
		struct TCP_Server_Info *server = tcon->ses->server;
		if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) &&
		    (mode & S_IWUGO) == 0 && server->ops->mkdir_setinfo)
			server->ops->mkdir_setinfo(iyesde, full_path, cifs_sb,
						   tcon, xid);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)
			iyesde->i_mode = (mode | S_IFDIR);

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
			iyesde->i_uid = current_fsuid();
			if (iyesde->i_mode & S_ISGID)
				iyesde->i_gid = parent->i_gid;
			else
				iyesde->i_gid = current_fsgid();
		}
	}
	d_instantiate(dentry, iyesde);
	return rc;
}

static int
cifs_posix_mkdir(struct iyesde *iyesde, struct dentry *dentry, umode_t mode,
		 const char *full_path, struct cifs_sb_info *cifs_sb,
		 struct cifs_tcon *tcon, const unsigned int xid)
{
	int rc = 0;
	u32 oplock = 0;
	FILE_UNIX_BASIC_INFO *info = NULL;
	struct iyesde *newiyesde = NULL;
	struct cifs_fattr fattr;

	info = kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
	if (info == NULL) {
		rc = -ENOMEM;
		goto posix_mkdir_out;
	}

	mode &= ~current_umask();
	rc = CIFSPOSIXCreate(xid, tcon, SMB_O_DIRECTORY | SMB_O_CREAT, mode,
			     NULL /* netfid */, info, &oplock, full_path,
			     cifs_sb->local_nls, cifs_remap(cifs_sb));
	if (rc == -EOPNOTSUPP)
		goto posix_mkdir_out;
	else if (rc) {
		cifs_dbg(FYI, "posix mkdir returned 0x%x\n", rc);
		d_drop(dentry);
		goto posix_mkdir_out;
	}

	if (info->Type == cpu_to_le32(-1))
		/* yes return info, go query for it */
		goto posix_mkdir_get_info;
	/*
	 * BB check (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID ) to see if
	 * need to set uid/gid.
	 */

	cifs_unix_basic_to_fattr(&fattr, info, cifs_sb);
	cifs_fill_uniqueid(iyesde->i_sb, &fattr);
	newiyesde = cifs_iget(iyesde->i_sb, &fattr);
	if (!newiyesde)
		goto posix_mkdir_get_info;

	d_instantiate(dentry, newiyesde);

#ifdef CONFIG_CIFS_DEBUG2
	cifs_dbg(FYI, "instantiated dentry %p %pd to iyesde %p\n",
		 dentry, dentry, newiyesde);

	if (newiyesde->i_nlink != 2)
		cifs_dbg(FYI, "unexpected number of links %d\n",
			 newiyesde->i_nlink);
#endif

posix_mkdir_out:
	kfree(info);
	return rc;
posix_mkdir_get_info:
	rc = cifs_mkdir_qinfo(iyesde, dentry, mode, full_path, cifs_sb, tcon,
			      xid);
	goto posix_mkdir_out;
}

int cifs_mkdir(struct iyesde *iyesde, struct dentry *direntry, umode_t mode)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_sb_info *cifs_sb;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char *full_path;

	cifs_dbg(FYI, "In cifs_mkdir, mode = 0x%hx iyesde = 0x%p\n",
		 mode, iyesde);

	cifs_sb = CIFS_SB(iyesde->i_sb);
	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	xid = get_xid();

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto mkdir_out;
	}

	server = tcon->ses->server;

	if ((server->ops->posix_mkdir) && (tcon->posix_extensions)) {
		rc = server->ops->posix_mkdir(xid, iyesde, mode, tcon, full_path,
					      cifs_sb);
		d_drop(direntry); /* for time being always refresh iyesde info */
		goto mkdir_out;
	}

	if (cap_unix(tcon->ses) && (CIFS_UNIX_POSIX_PATH_OPS_CAP &
				le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		rc = cifs_posix_mkdir(iyesde, direntry, mode, full_path, cifs_sb,
				      tcon, xid);
		if (rc != -EOPNOTSUPP)
			goto mkdir_out;
	}

	if (!server->ops->mkdir) {
		rc = -ENOSYS;
		goto mkdir_out;
	}

	/* BB add setting the equivalent of mode via CreateX w/ACLs */
	rc = server->ops->mkdir(xid, iyesde, mode, tcon, full_path, cifs_sb);
	if (rc) {
		cifs_dbg(FYI, "cifs_mkdir returned 0x%x\n", rc);
		d_drop(direntry);
		goto mkdir_out;
	}

	/* TODO: skip this for smb2/smb3 */
	rc = cifs_mkdir_qinfo(iyesde, direntry, mode, full_path, cifs_sb, tcon,
			      xid);
mkdir_out:
	/*
	 * Force revalidate to get parent dir info when needed since cached
	 * attributes are invalid yesw.
	 */
	CIFS_I(iyesde)->time = 0;
	kfree(full_path);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

int cifs_rmdir(struct iyesde *iyesde, struct dentry *direntry)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_sb_info *cifs_sb;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char *full_path = NULL;
	struct cifsIyesdeInfo *cifsIyesde;

	cifs_dbg(FYI, "cifs_rmdir, iyesde = 0x%p\n", iyesde);

	xid = get_xid();

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto rmdir_exit;
	}

	cifs_sb = CIFS_SB(iyesde->i_sb);
	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		rc = PTR_ERR(tlink);
		goto rmdir_exit;
	}
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	if (!server->ops->rmdir) {
		rc = -ENOSYS;
		cifs_put_tlink(tlink);
		goto rmdir_exit;
	}

	rc = server->ops->rmdir(xid, tcon, full_path, cifs_sb);
	cifs_put_tlink(tlink);

	if (!rc) {
		spin_lock(&d_iyesde(direntry)->i_lock);
		i_size_write(d_iyesde(direntry), 0);
		clear_nlink(d_iyesde(direntry));
		spin_unlock(&d_iyesde(direntry)->i_lock);
	}

	cifsIyesde = CIFS_I(d_iyesde(direntry));
	/* force revalidate to go get info when needed */
	cifsIyesde->time = 0;

	cifsIyesde = CIFS_I(iyesde);
	/*
	 * Force revalidate to get parent dir info when needed since cached
	 * attributes are invalid yesw.
	 */
	cifsIyesde->time = 0;

	d_iyesde(direntry)->i_ctime = iyesde->i_ctime = iyesde->i_mtime =
		current_time(iyesde);

rmdir_exit:
	kfree(full_path);
	free_xid(xid);
	return rc;
}

static int
cifs_do_rename(const unsigned int xid, struct dentry *from_dentry,
	       const char *from_path, struct dentry *to_dentry,
	       const char *to_path)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(from_dentry->d_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifs_fid fid;
	struct cifs_open_parms oparms;
	int oplock, rc;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	if (!server->ops->rename)
		return -ENOSYS;

	/* try path-based rename first */
	rc = server->ops->rename(xid, tcon, from_path, to_path, cifs_sb);

	/*
	 * Don't bother with rename by filehandle unless file is busy and
	 * source. Note that cross directory moves do yest work with
	 * rename by filehandle to various Windows servers.
	 */
	if (rc == 0 || rc != -EBUSY)
		goto do_rename_exit;

	/* Don't fall back to using SMB on SMB 2+ mount */
	if (server->vals->protocol_id != 0)
		goto do_rename_exit;

	/* open-file renames don't work across directories */
	if (to_dentry->d_parent != from_dentry->d_parent)
		goto do_rename_exit;

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	/* open the file to be renamed -- we need DELETE perms */
	oparms.desired_access = DELETE;
	oparms.create_options = CREATE_NOT_DIR;
	oparms.disposition = FILE_OPEN;
	oparms.path = from_path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = CIFS_open(xid, &oparms, &oplock, NULL);
	if (rc == 0) {
		rc = CIFSSMBRenameOpenFile(xid, tcon, fid.netfid,
				(const char *) to_dentry->d_name.name,
				cifs_sb->local_nls, cifs_remap(cifs_sb));
		CIFSSMBClose(xid, tcon, fid.netfid);
	}
do_rename_exit:
	cifs_put_tlink(tlink);
	return rc;
}

int
cifs_rename2(struct iyesde *source_dir, struct dentry *source_dentry,
	     struct iyesde *target_dir, struct dentry *target_dentry,
	     unsigned int flags)
{
	char *from_name = NULL;
	char *to_name = NULL;
	struct cifs_sb_info *cifs_sb;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	FILE_UNIX_BASIC_INFO *info_buf_source = NULL;
	FILE_UNIX_BASIC_INFO *info_buf_target;
	unsigned int xid;
	int rc, tmprc;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	cifs_sb = CIFS_SB(source_dir->i_sb);
	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	xid = get_xid();

	/*
	 * we already have the rename sem so we do yest need to
	 * grab it again here to protect the path integrity
	 */
	from_name = build_path_from_dentry(source_dentry);
	if (from_name == NULL) {
		rc = -ENOMEM;
		goto cifs_rename_exit;
	}

	to_name = build_path_from_dentry(target_dentry);
	if (to_name == NULL) {
		rc = -ENOMEM;
		goto cifs_rename_exit;
	}

	rc = cifs_do_rename(xid, source_dentry, from_name, target_dentry,
			    to_name);

	/*
	 * No-replace is the natural behavior for CIFS, so skip unlink hacks.
	 */
	if (flags & RENAME_NOREPLACE)
		goto cifs_rename_exit;

	if (rc == -EEXIST && tcon->unix_ext) {
		/*
		 * Are src and dst hardlinks of same iyesde? We can only tell
		 * with unix extensions enabled.
		 */
		info_buf_source =
			kmalloc_array(2, sizeof(FILE_UNIX_BASIC_INFO),
					GFP_KERNEL);
		if (info_buf_source == NULL) {
			rc = -ENOMEM;
			goto cifs_rename_exit;
		}

		info_buf_target = info_buf_source + 1;
		tmprc = CIFSSMBUnixQPathInfo(xid, tcon, from_name,
					     info_buf_source,
					     cifs_sb->local_nls,
					     cifs_remap(cifs_sb));
		if (tmprc != 0)
			goto unlink_target;

		tmprc = CIFSSMBUnixQPathInfo(xid, tcon, to_name,
					     info_buf_target,
					     cifs_sb->local_nls,
					     cifs_remap(cifs_sb));

		if (tmprc == 0 && (info_buf_source->UniqueId ==
				   info_buf_target->UniqueId)) {
			/* same file, POSIX says that this is a yesop */
			rc = 0;
			goto cifs_rename_exit;
		}
	}
	/*
	 * else ... BB we could add the same check for Windows by
	 * checking the UniqueId via FILE_INTERNAL_INFO
	 */

unlink_target:
	/* Try unlinking the target dentry if it's yest negative */
	if (d_really_is_positive(target_dentry) && (rc == -EACCES || rc == -EEXIST)) {
		if (d_is_dir(target_dentry))
			tmprc = cifs_rmdir(target_dir, target_dentry);
		else
			tmprc = cifs_unlink(target_dir, target_dentry);
		if (tmprc)
			goto cifs_rename_exit;
		rc = cifs_do_rename(xid, source_dentry, from_name,
				    target_dentry, to_name);
	}

	/* force revalidate to go get info when needed */
	CIFS_I(source_dir)->time = CIFS_I(target_dir)->time = 0;

	source_dir->i_ctime = source_dir->i_mtime = target_dir->i_ctime =
		target_dir->i_mtime = current_time(source_dir);

cifs_rename_exit:
	kfree(info_buf_source);
	kfree(from_name);
	kfree(to_name);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

static bool
cifs_iyesde_needs_reval(struct iyesde *iyesde)
{
	struct cifsIyesdeInfo *cifs_i = CIFS_I(iyesde);
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);

	if (cifs_i->time == 0)
		return true;

	if (CIFS_CACHE_READ(cifs_i))
		return false;

	if (!lookupCacheEnabled)
		return true;

	if (!cifs_sb->actimeo)
		return true;

	if (!time_in_range(jiffies, cifs_i->time,
				cifs_i->time + cifs_sb->actimeo))
		return true;

	/* hardlinked files w/ yesserveriyes get "special" treatment */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) &&
	    S_ISREG(iyesde->i_mode) && iyesde->i_nlink != 1)
		return true;

	return false;
}

/*
 * Zap the cache. Called when invalid_mapping flag is set.
 */
int
cifs_invalidate_mapping(struct iyesde *iyesde)
{
	int rc = 0;

	if (iyesde->i_mapping && iyesde->i_mapping->nrpages != 0) {
		rc = invalidate_iyesde_pages2(iyesde->i_mapping);
		if (rc)
			cifs_dbg(VFS, "%s: could yest invalidate iyesde %p\n",
				 __func__, iyesde);
	}

	cifs_fscache_reset_iyesde_cookie(iyesde);
	return rc;
}

/**
 * cifs_wait_bit_killable - helper for functions that are sleeping on bit locks
 * @word: long word containing the bit lock
 */
static int
cifs_wait_bit_killable(struct wait_bit_key *key, int mode)
{
	freezable_schedule_unsafe();
	if (signal_pending_state(mode, current))
		return -ERESTARTSYS;
	return 0;
}

int
cifs_revalidate_mapping(struct iyesde *iyesde)
{
	int rc;
	unsigned long *flags = &CIFS_I(iyesde)->flags;

	rc = wait_on_bit_lock_action(flags, CIFS_INO_LOCK, cifs_wait_bit_killable,
				     TASK_KILLABLE);
	if (rc)
		return rc;

	if (test_and_clear_bit(CIFS_INO_INVALID_MAPPING, flags)) {
		rc = cifs_invalidate_mapping(iyesde);
		if (rc)
			set_bit(CIFS_INO_INVALID_MAPPING, flags);
	}

	clear_bit_unlock(CIFS_INO_LOCK, flags);
	smp_mb__after_atomic();
	wake_up_bit(flags, CIFS_INO_LOCK);

	return rc;
}

int
cifs_zap_mapping(struct iyesde *iyesde)
{
	set_bit(CIFS_INO_INVALID_MAPPING, &CIFS_I(iyesde)->flags);
	return cifs_revalidate_mapping(iyesde);
}

int cifs_revalidate_file_attr(struct file *filp)
{
	int rc = 0;
	struct iyesde *iyesde = file_iyesde(filp);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *) filp->private_data;

	if (!cifs_iyesde_needs_reval(iyesde))
		return rc;

	if (tlink_tcon(cfile->tlink)->unix_ext)
		rc = cifs_get_file_info_unix(filp);
	else
		rc = cifs_get_file_info(filp);

	return rc;
}

int cifs_revalidate_dentry_attr(struct dentry *dentry)
{
	unsigned int xid;
	int rc = 0;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct super_block *sb = dentry->d_sb;
	char *full_path = NULL;

	if (iyesde == NULL)
		return -ENOENT;

	if (!cifs_iyesde_needs_reval(iyesde))
		return rc;

	xid = get_xid();

	/* can yest safely grab the rename sem here if rename calls revalidate
	   since that would deadlock */
	full_path = build_path_from_dentry(dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	cifs_dbg(FYI, "Update attributes: %s iyesde 0x%p count %d dentry: 0x%p d_time %ld jiffies %ld\n",
		 full_path, iyesde, iyesde->i_count.counter,
		 dentry, cifs_get_time(dentry), jiffies);

	if (cifs_sb_master_tcon(CIFS_SB(sb))->unix_ext)
		rc = cifs_get_iyesde_info_unix(&iyesde, full_path, sb, xid);
	else
		rc = cifs_get_iyesde_info(&iyesde, full_path, NULL, sb,
					 xid, NULL);

out:
	kfree(full_path);
	free_xid(xid);
	return rc;
}

int cifs_revalidate_file(struct file *filp)
{
	int rc;
	struct iyesde *iyesde = file_iyesde(filp);

	rc = cifs_revalidate_file_attr(filp);
	if (rc)
		return rc;

	return cifs_revalidate_mapping(iyesde);
}

/* revalidate a dentry's iyesde attributes */
int cifs_revalidate_dentry(struct dentry *dentry)
{
	int rc;
	struct iyesde *iyesde = d_iyesde(dentry);

	rc = cifs_revalidate_dentry_attr(dentry);
	if (rc)
		return rc;

	return cifs_revalidate_mapping(iyesde);
}

int cifs_getattr(const struct path *path, struct kstat *stat,
		 u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct cifs_sb_info *cifs_sb = CIFS_SB(dentry->d_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct iyesde *iyesde = d_iyesde(dentry);
	int rc;

	/*
	 * We need to be sure that all dirty pages are written and the server
	 * has actual ctime, mtime and file length.
	 */
	if (!CIFS_CACHE_READ(CIFS_I(iyesde)) && iyesde->i_mapping &&
	    iyesde->i_mapping->nrpages != 0) {
		rc = filemap_fdatawait(iyesde->i_mapping);
		if (rc) {
			mapping_set_error(iyesde->i_mapping, rc);
			return rc;
		}
	}

	rc = cifs_revalidate_dentry_attr(dentry);
	if (rc)
		return rc;

	generic_fillattr(iyesde, stat);
	stat->blksize = cifs_sb->bsize;
	stat->iyes = CIFS_I(iyesde)->uniqueid;

	/* old CIFS Unix Extensions doesn't return create time */
	if (CIFS_I(iyesde)->createtime) {
		stat->result_mask |= STATX_BTIME;
		stat->btime =
		      cifs_NTtimeToUnix(cpu_to_le64(CIFS_I(iyesde)->createtime));
	}

	stat->attributes_mask |= (STATX_ATTR_COMPRESSED | STATX_ATTR_ENCRYPTED);
	if (CIFS_I(iyesde)->cifsAttrs & FILE_ATTRIBUTE_COMPRESSED)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (CIFS_I(iyesde)->cifsAttrs & FILE_ATTRIBUTE_ENCRYPTED)
		stat->attributes |= STATX_ATTR_ENCRYPTED;

	/*
	 * If on a multiuser mount without unix extensions or cifsacl being
	 * enabled, and the admin hasn't overridden them, set the ownership
	 * to the fsuid/fsgid of the current process.
	 */
	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER) &&
	    !(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) &&
	    !tcon->unix_ext) {
		if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_UID))
			stat->uid = current_fsuid();
		if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_GID))
			stat->gid = current_fsgid();
	}
	return rc;
}

int cifs_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fei, u64 start,
		u64 len)
{
	struct cifsIyesdeInfo *cifs_i = CIFS_I(iyesde);
	struct cifs_sb_info *cifs_sb = CIFS_SB(cifs_i->vfs_iyesde.i_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifsFileInfo *cfile;
	int rc;

	/*
	 * We need to be sure that all dirty pages are written as they
	 * might fill holes on the server.
	 */
	if (!CIFS_CACHE_READ(CIFS_I(iyesde)) && iyesde->i_mapping &&
	    iyesde->i_mapping->nrpages != 0) {
		rc = filemap_fdatawait(iyesde->i_mapping);
		if (rc) {
			mapping_set_error(iyesde->i_mapping, rc);
			return rc;
		}
	}

	cfile = find_readable_file(cifs_i, false);
	if (cfile == NULL)
		return -EINVAL;

	if (server->ops->fiemap) {
		rc = server->ops->fiemap(tcon, cfile, fei, start, len);
		cifsFileInfo_put(cfile);
		return rc;
	}

	cifsFileInfo_put(cfile);
	return -ENOTSUPP;
}

static int cifs_truncate_page(struct address_space *mapping, loff_t from)
{
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned offset = from & (PAGE_SIZE - 1);
	struct page *page;
	int rc = 0;

	page = grab_cache_page(mapping, index);
	if (!page)
		return -ENOMEM;

	zero_user_segment(page, offset, PAGE_SIZE);
	unlock_page(page);
	put_page(page);
	return rc;
}

static void cifs_setsize(struct iyesde *iyesde, loff_t offset)
{
	struct cifsIyesdeInfo *cifs_i = CIFS_I(iyesde);

	spin_lock(&iyesde->i_lock);
	i_size_write(iyesde, offset);
	spin_unlock(&iyesde->i_lock);

	/* Cached iyesde must be refreshed on truncate */
	cifs_i->time = 0;
	truncate_pagecache(iyesde, offset);
}

static int
cifs_set_file_size(struct iyesde *iyesde, struct iattr *attrs,
		   unsigned int xid, char *full_path)
{
	int rc;
	struct cifsFileInfo *open_file;
	struct cifsIyesdeInfo *cifsIyesde = CIFS_I(iyesde);
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);
	struct tcon_link *tlink = NULL;
	struct cifs_tcon *tcon = NULL;
	struct TCP_Server_Info *server;

	/*
	 * To avoid spurious oplock breaks from server, in the case of
	 * iyesdes that we already have open, avoid doing path based
	 * setting of file size if we can do it by handle.
	 * This keeps our caching token (oplock) and avoids timeouts
	 * when the local oplock break takes longer to flush
	 * writebehind data than the SMB timeout for the SetPathInfo
	 * request would allow
	 */
	open_file = find_writable_file(cifsIyesde, true);
	if (open_file) {
		tcon = tlink_tcon(open_file->tlink);
		server = tcon->ses->server;
		if (server->ops->set_file_size)
			rc = server->ops->set_file_size(xid, tcon, open_file,
							attrs->ia_size, false);
		else
			rc = -ENOSYS;
		cifsFileInfo_put(open_file);
		cifs_dbg(FYI, "SetFSize for attrs rc = %d\n", rc);
	} else
		rc = -EINVAL;

	if (!rc)
		goto set_size_out;

	if (tcon == NULL) {
		tlink = cifs_sb_tlink(cifs_sb);
		if (IS_ERR(tlink))
			return PTR_ERR(tlink);
		tcon = tlink_tcon(tlink);
		server = tcon->ses->server;
	}

	/*
	 * Set file size by pathname rather than by handle either because yes
	 * valid, writeable file handle for it was found or because there was
	 * an error setting it by handle.
	 */
	if (server->ops->set_path_size)
		rc = server->ops->set_path_size(xid, tcon, full_path,
						attrs->ia_size, cifs_sb, false);
	else
		rc = -ENOSYS;
	cifs_dbg(FYI, "SetEOF by path (setattrs) rc = %d\n", rc);

	if (tlink)
		cifs_put_tlink(tlink);

set_size_out:
	if (rc == 0) {
		cifsIyesde->server_eof = attrs->ia_size;
		cifs_setsize(iyesde, attrs->ia_size);
		cifs_truncate_page(iyesde->i_mapping, iyesde->i_size);
	}

	return rc;
}

static int
cifs_setattr_unix(struct dentry *direntry, struct iattr *attrs)
{
	int rc;
	unsigned int xid;
	char *full_path = NULL;
	struct iyesde *iyesde = d_iyesde(direntry);
	struct cifsIyesdeInfo *cifsIyesde = CIFS_I(iyesde);
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *pTcon;
	struct cifs_unix_set_info_args *args = NULL;
	struct cifsFileInfo *open_file;

	cifs_dbg(FYI, "setattr_unix on file %pd attrs->ia_valid=0x%x\n",
		 direntry, attrs->ia_valid);

	xid = get_xid();

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM)
		attrs->ia_valid |= ATTR_FORCE;

	rc = setattr_prepare(direntry, attrs);
	if (rc < 0)
		goto out;

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * Attempt to flush data before changing attributes. We need to do
	 * this for ATTR_SIZE and ATTR_MTIME for sure, and if we change the
	 * ownership or mode then we may also need to do this. Here, we take
	 * the safe way out and just do the flush on all setattr requests. If
	 * the flush returns error, store it to report later and continue.
	 *
	 * BB: This should be smarter. Why bother flushing pages that
	 * will be truncated anyway? Also, should we error out here if
	 * the flush returns error?
	 */
	rc = filemap_write_and_wait(iyesde->i_mapping);
	if (is_interrupt_error(rc)) {
		rc = -ERESTARTSYS;
		goto out;
	}

	mapping_set_error(iyesde->i_mapping, rc);
	rc = 0;

	if (attrs->ia_valid & ATTR_SIZE) {
		rc = cifs_set_file_size(iyesde, attrs, xid, full_path);
		if (rc != 0)
			goto out;
	}

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attrs->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
		attrs->ia_valid &= ~ATTR_MODE;

	args = kmalloc(sizeof(*args), GFP_KERNEL);
	if (args == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/* set up the struct */
	if (attrs->ia_valid & ATTR_MODE)
		args->mode = attrs->ia_mode;
	else
		args->mode = NO_CHANGE_64;

	if (attrs->ia_valid & ATTR_UID)
		args->uid = attrs->ia_uid;
	else
		args->uid = INVALID_UID; /* yes change */

	if (attrs->ia_valid & ATTR_GID)
		args->gid = attrs->ia_gid;
	else
		args->gid = INVALID_GID; /* yes change */

	if (attrs->ia_valid & ATTR_ATIME)
		args->atime = cifs_UnixTimeToNT(attrs->ia_atime);
	else
		args->atime = NO_CHANGE_64;

	if (attrs->ia_valid & ATTR_MTIME)
		args->mtime = cifs_UnixTimeToNT(attrs->ia_mtime);
	else
		args->mtime = NO_CHANGE_64;

	if (attrs->ia_valid & ATTR_CTIME)
		args->ctime = cifs_UnixTimeToNT(attrs->ia_ctime);
	else
		args->ctime = NO_CHANGE_64;

	args->device = 0;
	open_file = find_writable_file(cifsIyesde, true);
	if (open_file) {
		u16 nfid = open_file->fid.netfid;
		u32 npid = open_file->pid;
		pTcon = tlink_tcon(open_file->tlink);
		rc = CIFSSMBUnixSetFileInfo(xid, pTcon, args, nfid, npid);
		cifsFileInfo_put(open_file);
	} else {
		tlink = cifs_sb_tlink(cifs_sb);
		if (IS_ERR(tlink)) {
			rc = PTR_ERR(tlink);
			goto out;
		}
		pTcon = tlink_tcon(tlink);
		rc = CIFSSMBUnixSetPathInfo(xid, pTcon, full_path, args,
				    cifs_sb->local_nls,
				    cifs_remap(cifs_sb));
		cifs_put_tlink(tlink);
	}

	if (rc)
		goto out;

	if ((attrs->ia_valid & ATTR_SIZE) &&
	    attrs->ia_size != i_size_read(iyesde))
		truncate_setsize(iyesde, attrs->ia_size);

	setattr_copy(iyesde, attrs);
	mark_iyesde_dirty(iyesde);

	/* force revalidate when any of these times are set since some
	   of the fs types (eg ext3, fat) do yest have fine eyesugh
	   time granularity to match protocol, and we do yest have a
	   a way (yet) to query the server fs's time granularity (and
	   whether it rounds times down).
	*/
	if (attrs->ia_valid & (ATTR_MTIME | ATTR_CTIME))
		cifsIyesde->time = 0;
out:
	kfree(args);
	kfree(full_path);
	free_xid(xid);
	return rc;
}

static int
cifs_setattr_yesunix(struct dentry *direntry, struct iattr *attrs)
{
	unsigned int xid;
	kuid_t uid = INVALID_UID;
	kgid_t gid = INVALID_GID;
	struct iyesde *iyesde = d_iyesde(direntry);
	struct cifs_sb_info *cifs_sb = CIFS_SB(iyesde->i_sb);
	struct cifsIyesdeInfo *cifsIyesde = CIFS_I(iyesde);
	struct cifsFileInfo *wfile;
	struct cifs_tcon *tcon;
	char *full_path = NULL;
	int rc = -EACCES;
	__u32 dosattr = 0;
	__u64 mode = NO_CHANGE_64;

	xid = get_xid();

	cifs_dbg(FYI, "setattr on file %pd attrs->ia_valid 0x%x\n",
		 direntry, attrs->ia_valid);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM)
		attrs->ia_valid |= ATTR_FORCE;

	rc = setattr_prepare(direntry, attrs);
	if (rc < 0) {
		free_xid(xid);
		return rc;
	}

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		free_xid(xid);
		return rc;
	}

	/*
	 * Attempt to flush data before changing attributes. We need to do
	 * this for ATTR_SIZE and ATTR_MTIME for sure, and if we change the
	 * ownership or mode then we may also need to do this. Here, we take
	 * the safe way out and just do the flush on all setattr requests. If
	 * the flush returns error, store it to report later and continue.
	 *
	 * BB: This should be smarter. Why bother flushing pages that
	 * will be truncated anyway? Also, should we error out here if
	 * the flush returns error?
	 */
	rc = filemap_write_and_wait(iyesde->i_mapping);
	if (is_interrupt_error(rc)) {
		rc = -ERESTARTSYS;
		goto cifs_setattr_exit;
	}

	mapping_set_error(iyesde->i_mapping, rc);
	rc = 0;

	if (attrs->ia_valid & ATTR_MTIME) {
		rc = cifs_get_writable_file(cifsIyesde, false, &wfile);
		if (!rc) {
			tcon = tlink_tcon(wfile->tlink);
			rc = tcon->ses->server->ops->flush(xid, tcon, &wfile->fid);
			cifsFileInfo_put(wfile);
			if (rc)
				goto cifs_setattr_exit;
		} else if (rc != -EBADF)
			goto cifs_setattr_exit;
		else
			rc = 0;
	}

	if (attrs->ia_valid & ATTR_SIZE) {
		rc = cifs_set_file_size(iyesde, attrs, xid, full_path);
		if (rc != 0)
			goto cifs_setattr_exit;
	}

	if (attrs->ia_valid & ATTR_UID)
		uid = attrs->ia_uid;

	if (attrs->ia_valid & ATTR_GID)
		gid = attrs->ia_gid;

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) ||
	    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MODE_FROM_SID)) {
		if (uid_valid(uid) || gid_valid(gid)) {
			rc = id_mode_to_cifs_acl(iyesde, full_path, NO_CHANGE_64,
							uid, gid);
			if (rc) {
				cifs_dbg(FYI, "%s: Setting id failed with error: %d\n",
					 __func__, rc);
				goto cifs_setattr_exit;
			}
		}
	} else
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID))
		attrs->ia_valid &= ~(ATTR_UID | ATTR_GID);

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attrs->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
		attrs->ia_valid &= ~ATTR_MODE;

	if (attrs->ia_valid & ATTR_MODE) {
		mode = attrs->ia_mode;
		rc = 0;
		if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) ||
		    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MODE_FROM_SID)) {
			rc = id_mode_to_cifs_acl(iyesde, full_path, mode,
						INVALID_UID, INVALID_GID);
			if (rc) {
				cifs_dbg(FYI, "%s: Setting ACL failed with error: %d\n",
					 __func__, rc);
				goto cifs_setattr_exit;
			}
		} else
		if (((mode & S_IWUGO) == 0) &&
		    (cifsIyesde->cifsAttrs & ATTR_READONLY) == 0) {

			dosattr = cifsIyesde->cifsAttrs | ATTR_READONLY;

			/* fix up mode if we're yest using dynperm */
			if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM) == 0)
				attrs->ia_mode = iyesde->i_mode & ~S_IWUGO;
		} else if ((mode & S_IWUGO) &&
			   (cifsIyesde->cifsAttrs & ATTR_READONLY)) {

			dosattr = cifsIyesde->cifsAttrs & ~ATTR_READONLY;
			/* Attributes of 0 are igyesred */
			if (dosattr == 0)
				dosattr |= ATTR_NORMAL;

			/* reset local iyesde permissions to yesrmal */
			if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)) {
				attrs->ia_mode &= ~(S_IALLUGO);
				if (S_ISDIR(iyesde->i_mode))
					attrs->ia_mode |=
						cifs_sb->mnt_dir_mode;
				else
					attrs->ia_mode |=
						cifs_sb->mnt_file_mode;
			}
		} else if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)) {
			/* igyesre mode change - ATTR_READONLY hasn't changed */
			attrs->ia_valid &= ~ATTR_MODE;
		}
	}

	if (attrs->ia_valid & (ATTR_MTIME|ATTR_ATIME|ATTR_CTIME) ||
	    ((attrs->ia_valid & ATTR_MODE) && dosattr)) {
		rc = cifs_set_file_info(iyesde, attrs, xid, full_path, dosattr);
		/* BB: check for rc = -EOPNOTSUPP and switch to legacy mode */

		/* Even if error on time set, yes sense failing the call if
		the server would set the time to a reasonable value anyway,
		and this check ensures that we are yest being called from
		sys_utimes in which case we ought to fail the call back to
		the user when the server rejects the call */
		if ((rc) && (attrs->ia_valid &
				(ATTR_MODE | ATTR_GID | ATTR_UID | ATTR_SIZE)))
			rc = 0;
	}

	/* do yest need local check to iyesde_check_ok since the server does
	   that */
	if (rc)
		goto cifs_setattr_exit;

	if ((attrs->ia_valid & ATTR_SIZE) &&
	    attrs->ia_size != i_size_read(iyesde))
		truncate_setsize(iyesde, attrs->ia_size);

	setattr_copy(iyesde, attrs);
	mark_iyesde_dirty(iyesde);

cifs_setattr_exit:
	kfree(full_path);
	free_xid(xid);
	return rc;
}

int
cifs_setattr(struct dentry *direntry, struct iattr *attrs)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(direntry->d_sb);
	struct cifs_tcon *pTcon = cifs_sb_master_tcon(cifs_sb);

	if (pTcon->unix_ext)
		return cifs_setattr_unix(direntry, attrs);

	return cifs_setattr_yesunix(direntry, attrs);

	/* BB: add cifs_setattr_legacy for really old servers */
}

#if 0
void cifs_delete_iyesde(struct iyesde *iyesde)
{
	cifs_dbg(FYI, "In cifs_delete_iyesde, iyesde = 0x%p\n", iyesde);
	/* may have to add back in if and when safe distributed caching of
	   directories added e.g. via FindNotify */
}
#endif
