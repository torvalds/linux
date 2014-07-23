/*
 *   fs/cifs/inode.c
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
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/freezer.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "fscache.h"


static void cifs_set_ops(struct inode *inode)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &cifs_file_inode_ops;
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				inode->i_fop = &cifs_file_direct_nobrl_ops;
			else
				inode->i_fop = &cifs_file_direct_ops;
		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				inode->i_fop = &cifs_file_strict_nobrl_ops;
			else
				inode->i_fop = &cifs_file_strict_ops;
		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			inode->i_fop = &cifs_file_nobrl_ops;
		else { /* not direct, send byte range locks */
			inode->i_fop = &cifs_file_ops;
		}

		/* check if server can support readpages */
		if (cifs_sb_master_tcon(cifs_sb)->ses->server->maxBuf <
				PAGE_CACHE_SIZE + MAX_CIFS_HDR_SIZE)
			inode->i_data.a_ops = &cifs_addr_ops_smallbuf;
		else
			inode->i_data.a_ops = &cifs_addr_ops;
		break;
	case S_IFDIR:
#ifdef CONFIG_CIFS_DFS_UPCALL
		if (IS_AUTOMOUNT(inode)) {
			inode->i_op = &cifs_dfs_referral_inode_operations;
		} else {
#else /* NO DFS support, treat as a directory */
		{
#endif
			inode->i_op = &cifs_dir_inode_ops;
			inode->i_fop = &cifs_dir_ops;
		}
		break;
	case S_IFLNK:
		inode->i_op = &cifs_symlink_inode_ops;
		break;
	default:
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		break;
	}
}

/* check inode attributes against fattr. If they don't match, tag the
 * inode for cache invalidation
 */
static void
cifs_revalidate_cache(struct inode *inode, struct cifs_fattr *fattr)
{
	struct cifsInodeInfo *cifs_i = CIFS_I(inode);

	cifs_dbg(FYI, "%s: revalidating inode %llu\n",
		 __func__, cifs_i->uniqueid);

	if (inode->i_state & I_NEW) {
		cifs_dbg(FYI, "%s: inode %llu is new\n",
			 __func__, cifs_i->uniqueid);
		return;
	}

	/* don't bother with revalidation if we have an oplock */
	if (CIFS_CACHE_READ(cifs_i)) {
		cifs_dbg(FYI, "%s: inode %llu is oplocked\n",
			 __func__, cifs_i->uniqueid);
		return;
	}

	 /* revalidate if mtime or size have changed */
	if (timespec_equal(&inode->i_mtime, &fattr->cf_mtime) &&
	    cifs_i->server_eof == fattr->cf_eof) {
		cifs_dbg(FYI, "%s: inode %llu is unchanged\n",
			 __func__, cifs_i->uniqueid);
		return;
	}

	cifs_dbg(FYI, "%s: invalidating inode %llu mapping\n",
		 __func__, cifs_i->uniqueid);
	set_bit(CIFS_INO_INVALID_MAPPING, &cifs_i->flags);
}

/*
 * copy nlink to the inode, unless it wasn't provided.  Provide
 * sane values if we don't have an existing one and none was provided
 */
static void
cifs_nlink_fattr_to_inode(struct inode *inode, struct cifs_fattr *fattr)
{
	/*
	 * if we're in a situation where we can't trust what we
	 * got from the server (readdir, some non-unix cases)
	 * fake reasonable values
	 */
	if (fattr->cf_flags & CIFS_FATTR_UNKNOWN_NLINK) {
		/* only provide fake values on a new inode */
		if (inode->i_state & I_NEW) {
			if (fattr->cf_cifsattrs & ATTR_DIRECTORY)
				set_nlink(inode, 2);
			else
				set_nlink(inode, 1);
		}
		return;
	}

	/* we trust the server, so update it */
	set_nlink(inode, fattr->cf_nlink);
}

/* populate an inode with info from a cifs_fattr struct */
void
cifs_fattr_to_inode(struct inode *inode, struct cifs_fattr *fattr)
{
	struct cifsInodeInfo *cifs_i = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	cifs_revalidate_cache(inode, fattr);

	spin_lock(&inode->i_lock);
	inode->i_atime = fattr->cf_atime;
	inode->i_mtime = fattr->cf_mtime;
	inode->i_ctime = fattr->cf_ctime;
	inode->i_rdev = fattr->cf_rdev;
	cifs_nlink_fattr_to_inode(inode, fattr);
	inode->i_uid = fattr->cf_uid;
	inode->i_gid = fattr->cf_gid;

	/* if dynperm is set, don't clobber existing mode */
	if (inode->i_state & I_NEW ||
	    !(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM))
		inode->i_mode = fattr->cf_mode;

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
		i_size_write(inode, fattr->cf_eof);

		/*
		 * i_blocks is not related to (i_size / i_blksize),
		 * but instead 512 byte (2**9) size is required for
		 * calculating num blocks.
		 */
		inode->i_blocks = (512 - 1 + fattr->cf_bytes) >> 9;
	}
	spin_unlock(&inode->i_lock);

	if (fattr->cf_flags & CIFS_FATTR_DFS_REFERRAL)
		inode->i_flags |= S_AUTOMOUNT;
	if (inode->i_state & I_NEW)
		cifs_set_ops(inode);
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
	fattr->cf_mode = le64_to_cpu(info->Permissions);

	/*
	 * Since we set the inode type below we need to mask off
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
				       le64_to_cpu(info->DevMinor) & MINORMASK);
		break;
	case UNIX_BLOCKDEV:
		fattr->cf_mode |= S_IFBLK;
		fattr->cf_dtype = DT_BLK;
		fattr->cf_rdev = MKDEV(le64_to_cpu(info->DevMajor),
				       le64_to_cpu(info->DevMinor) & MINORMASK);
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
		/* safest to call it a file if we do not know */
		fattr->cf_mode |= S_IFREG;
		fattr->cf_dtype = DT_REG;
		cifs_dbg(FYI, "unknown type %d\n", le32_to_cpu(info->Type));
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
 * Fill a cifs_fattr struct with fake inode info.
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
	fattr->cf_atime = CURRENT_TIME;
	fattr->cf_ctime = CURRENT_TIME;
	fattr->cf_mtime = CURRENT_TIME;
	fattr->cf_nlink = 2;
	fattr->cf_flags |= CIFS_FATTR_DFS_REFERRAL;
}

static int
cifs_get_file_info_unix(struct file *filp)
{
	int rc;
	unsigned int xid;
	FILE_UNIX_BASIC_INFO find_data;
	struct cifs_fattr fattr;
	struct inode *inode = file_inode(filp);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsFileInfo *cfile = filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);

	xid = get_xid();
	rc = CIFSSMBUnixQFileInfo(xid, tcon, cfile->fid.netfid, &find_data);
	if (!rc) {
		cifs_unix_basic_to_fattr(&fattr, &find_data, cifs_sb);
	} else if (rc == -EREMOTE) {
		cifs_create_dfs_fattr(&fattr, inode->i_sb);
		rc = 0;
	}

	cifs_fattr_to_inode(inode, &fattr);
	free_xid(xid);
	return rc;
}

int cifs_get_inode_info_unix(struct inode **pinode,
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
				  cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
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

	if (*pinode == NULL) {
		/* get new inode */
		cifs_fill_uniqueid(sb, &fattr);
		*pinode = cifs_iget(sb, &fattr);
		if (!*pinode)
			rc = -ENOMEM;
	} else {
		/* we already have inode, update it */
		cifs_fattr_to_inode(*pinode, &fattr);
	}

	return rc;
}

static int
cifs_sfu_type(struct cifs_fattr *fattr, const char *path,
	      struct cifs_sb_info *cifs_sb, unsigned int xid)
{
	int rc;
	int oplock = 0;
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
	oparms.disposition = FILE_OPEN;
	oparms.path = path;
	oparms.fid = &fid;
	oparms.reconnect = false;

	rc = CIFS_open(xid, &oparms, &oplock, NULL);
	if (rc) {
		cifs_put_tlink(tlink);
		return rc;
	}

	/* Read header */
	io_parms.netfid = fid.netfid;
	io_parms.pid = current->tgid;
	io_parms.tcon = tcon;
	io_parms.offset = 0;
	io_parms.length = 24;

	rc = CIFSSMBRead(xid, &io_parms, &bytes_read, &pbuf, &buf_type);
	if ((rc == 0) && (bytes_read >= 8)) {
		if (memcmp("IntxBLK", pbuf, 8) == 0) {
			cifs_dbg(FYI, "Block device\n");
			fattr->cf_mode |= S_IFBLK;
			fattr->cf_dtype = DT_BLK;
			if (bytes_read == 24) {
				/* we have enough to decode dev num */
				__u64 mjr; /* major */
				__u64 mnr; /* minor */
				mjr = le64_to_cpu(*(__le64 *)(pbuf+8));
				mnr = le64_to_cpu(*(__le64 *)(pbuf+16));
				fattr->cf_rdev = MKDEV(mjr, mnr);
			}
		} else if (memcmp("IntxCHR", pbuf, 8) == 0) {
			cifs_dbg(FYI, "Char device\n");
			fattr->cf_mode |= S_IFCHR;
			fattr->cf_dtype = DT_CHR;
			if (bytes_read == 24) {
				/* we have enough to decode dev num */
				__u64 mjr; /* major */
				__u64 mnr; /* minor */
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
		rc = -EOPNOTSUPP; /* or some unknown SFU type */
	}
	CIFSSMBClose(xid, tcon, fid.netfid);
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
			cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
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
		       struct cifs_sb_info *cifs_sb, bool adjust_tz,
		       bool symlink)
{
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);

	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_cifsattrs = le32_to_cpu(info->Attributes);
	if (info->DeletePending)
		fattr->cf_flags |= CIFS_FATTR_DELETE_PENDING;

	if (info->LastAccessTime)
		fattr->cf_atime = cifs_NTtimeToUnix(info->LastAccessTime);
	else
		fattr->cf_atime = CURRENT_TIME;

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
		 * Don't accept zero nlink from non-unix servers unless
		 * delete is pending.  Instead mark it as unknown.
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
	struct inode *inode = file_inode(filp);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsFileInfo *cfile = filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;

	if (!server->ops->query_file_info)
		return -ENOSYS;

	xid = get_xid();
	rc = server->ops->query_file_info(xid, tcon, &cfile->fid, &find_data);
	switch (rc) {
	case 0:
		cifs_all_info_to_fattr(&fattr, &find_data, cifs_sb, false,
				       false);
		break;
	case -EREMOTE:
		cifs_create_dfs_fattr(&fattr, inode->i_sb);
		rc = 0;
		break;
	case -EOPNOTSUPP:
	case -EINVAL:
		/*
		 * FIXME: legacy server -- fall back to path-based call?
		 * for now, just skip revalidating and mark inode for
		 * immediate reval.
		 */
		rc = 0;
		CIFS_I(inode)->time = 0;
	default:
		goto cgfi_exit;
	}

	/*
	 * don't bother with SFU junk here -- just mark inode as needing
	 * revalidation.
	 */
	fattr.cf_uniqueid = CIFS_I(inode)->uniqueid;
	fattr.cf_flags |= CIFS_FATTR_NEED_REVAL;
	cifs_fattr_to_inode(inode, &fattr);
cgfi_exit:
	free_xid(xid);
	return rc;
}

int
cifs_get_inode_info(struct inode **inode, const char *full_path,
		    FILE_ALL_INFO *data, struct super_block *sb, int xid,
		    const struct cifs_fid *fid)
{
	bool validinum = false;
	__u16 srchflgs;
	int rc = 0, tmprc = ENOSYS;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct tcon_link *tlink;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *buf = NULL;
	bool adjust_tz = false;
	struct cifs_fattr fattr;
	struct cifs_search_info *srchinf = NULL;
	bool symlink = false;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	cifs_dbg(FYI, "Getting info on %s\n", full_path);

	if ((data == NULL) && (*inode != NULL)) {
		if (CIFS_CACHE_READ(CIFS_I(*inode))) {
			cifs_dbg(FYI, "No need to revalidate cached inode sizes\n");
			goto cgii_exit;
		}
	}

	/* if inode info is not passed, get it from server */
	if (data == NULL) {
		if (!server->ops->query_path_info) {
			rc = -ENOSYS;
			goto cgii_exit;
		}
		buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
		if (buf == NULL) {
			rc = -ENOMEM;
			goto cgii_exit;
		}
		data = (FILE_ALL_INFO *)buf;
		rc = server->ops->query_path_info(xid, tcon, cifs_sb, full_path,
						  data, &adjust_tz, &symlink);
	}

	if (!rc) {
		cifs_all_info_to_fattr(&fattr, data, cifs_sb, adjust_tz,
				       symlink);
	} else if (rc == -EREMOTE) {
		cifs_create_dfs_fattr(&fattr, sb);
		rc = 0;
	} else if (rc == -EACCES && backup_cred(cifs_sb)) {
			srchinf = kzalloc(sizeof(struct cifs_search_info),
						GFP_KERNEL);
			if (srchinf == NULL) {
				rc = -ENOMEM;
				goto cgii_exit;
			}

			srchinf->endOfSearch = false;
			srchinf->info_level = SMB_FIND_FILE_ID_FULL_DIR_INFO;

			srchflgs = CIFS_SEARCH_CLOSE_ALWAYS |
					CIFS_SEARCH_CLOSE_AT_END |
					CIFS_SEARCH_BACKUP_SEARCH;

			rc = CIFSFindFirst(xid, tcon, full_path,
				cifs_sb, NULL, srchflgs, srchinf, false);
			if (!rc) {
				data =
				(FILE_ALL_INFO *)srchinf->srch_entries_start;

				cifs_dir_info_to_fattr(&fattr,
				(FILE_DIRECTORY_INFO *)data, cifs_sb);
				fattr.cf_uniqueid = le64_to_cpu(
				((SEARCH_ID_FULL_DIR_INFO *)data)->UniqueId);
				validinum = true;

				cifs_buf_release(srchinf->ntwrk_buf_start);
			}
			kfree(srchinf);
	} else
		goto cgii_exit;

	/*
	 * If an inode wasn't passed in, then get the inode number
	 *
	 * Is an i_ino of zero legal? Can we use that to check if the server
	 * supports returning inode numbers?  Are there other sanity checks we
	 * can use to ensure that the server is really filling in that field?
	 */
	if (*inode == NULL) {
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
			if (validinum == false) {
				if (server->ops->get_srv_inum)
					tmprc = server->ops->get_srv_inum(xid,
						tcon, cifs_sb, full_path,
						&fattr.cf_uniqueid, data);
				if (tmprc) {
					cifs_dbg(FYI, "GetSrvInodeNum rc %d\n",
						 tmprc);
					fattr.cf_uniqueid = iunique(sb, ROOT_I);
					cifs_autodisable_serverino(cifs_sb);
				}
			}
		} else
			fattr.cf_uniqueid = iunique(sb, ROOT_I);
	} else
		fattr.cf_uniqueid = CIFS_I(*inode)->uniqueid;

	/* query for SFU type info if supported and needed */
	if (fattr.cf_cifsattrs & ATTR_SYSTEM &&
	    cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) {
		tmprc = cifs_sfu_type(&fattr, full_path, cifs_sb, xid);
		if (tmprc)
			cifs_dbg(FYI, "cifs_sfu_type failed: %d\n", tmprc);
	}

#ifdef CONFIG_CIFS_ACL
	/* fill in 0777 bits from ACL */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) {
		rc = cifs_acl_to_fattr(cifs_sb, &fattr, *inode, full_path, fid);
		if (rc) {
			cifs_dbg(FYI, "%s: Getting ACL failed with error: %d\n",
				 __func__, rc);
			goto cgii_exit;
		}
	}
#endif /* CONFIG_CIFS_ACL */

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

	if (!*inode) {
		*inode = cifs_iget(sb, &fattr);
		if (!*inode)
			rc = -ENOMEM;
	} else {
		cifs_fattr_to_inode(*inode, &fattr);
	}

cgii_exit:
	kfree(buf);
	cifs_put_tlink(tlink);
	return rc;
}

static const struct inode_operations cifs_ipc_inode_ops = {
	.lookup = cifs_lookup,
};

static int
cifs_find_inode(struct inode *inode, void *opaque)
{
	struct cifs_fattr *fattr = (struct cifs_fattr *) opaque;

	/* don't match inode with different uniqueid */
	if (CIFS_I(inode)->uniqueid != fattr->cf_uniqueid)
		return 0;

	/* use createtime like an i_generation field */
	if (CIFS_I(inode)->createtime != fattr->cf_createtime)
		return 0;

	/* don't match inode of different type */
	if ((inode->i_mode & S_IFMT) != (fattr->cf_mode & S_IFMT))
		return 0;

	/* if it's not a directory or has no dentries, then flag it */
	if (S_ISDIR(inode->i_mode) && !hlist_empty(&inode->i_dentry))
		fattr->cf_flags |= CIFS_FATTR_INO_COLLISION;

	return 1;
}

static int
cifs_init_inode(struct inode *inode, void *opaque)
{
	struct cifs_fattr *fattr = (struct cifs_fattr *) opaque;

	CIFS_I(inode)->uniqueid = fattr->cf_uniqueid;
	CIFS_I(inode)->createtime = fattr->cf_createtime;
	return 0;
}

/*
 * walk dentry list for an inode and report whether it has aliases that
 * are hashed. We use this to determine if a directory inode can actually
 * be used.
 */
static bool
inode_has_hashed_dentries(struct inode *inode)
{
	struct dentry *dentry;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, &inode->i_dentry, d_alias) {
		if (!d_unhashed(dentry) || IS_ROOT(dentry)) {
			spin_unlock(&inode->i_lock);
			return true;
		}
	}
	spin_unlock(&inode->i_lock);
	return false;
}

/* Given fattrs, get a corresponding inode */
struct inode *
cifs_iget(struct super_block *sb, struct cifs_fattr *fattr)
{
	unsigned long hash;
	struct inode *inode;

retry_iget5_locked:
	cifs_dbg(FYI, "looking for uniqueid=%llu\n", fattr->cf_uniqueid);

	/* hash down to 32-bits on 32-bit arch */
	hash = cifs_uniqueid_to_ino_t(fattr->cf_uniqueid);

	inode = iget5_locked(sb, hash, cifs_find_inode, cifs_init_inode, fattr);
	if (inode) {
		/* was there a potentially problematic inode collision? */
		if (fattr->cf_flags & CIFS_FATTR_INO_COLLISION) {
			fattr->cf_flags &= ~CIFS_FATTR_INO_COLLISION;

			if (inode_has_hashed_dentries(inode)) {
				cifs_autodisable_serverino(CIFS_SB(sb));
				iput(inode);
				fattr->cf_uniqueid = iunique(sb, ROOT_I);
				goto retry_iget5_locked;
			}
		}

		cifs_fattr_to_inode(inode, fattr);
		if (sb->s_flags & MS_NOATIME)
			inode->i_flags |= S_NOATIME | S_NOCMTIME;
		if (inode->i_state & I_NEW) {
			inode->i_ino = hash;
			if (S_ISREG(inode->i_mode))
				inode->i_data.backing_dev_info = sb->s_bdi;
#ifdef CONFIG_CIFS_FSCACHE
			/* initialize per-inode cache cookie pointer */
			CIFS_I(inode)->fscache = NULL;
#endif
			unlock_new_inode(inode);
		}
	}

	return inode;
}

/* gets root inode */
struct inode *cifs_root_iget(struct super_block *sb)
{
	unsigned int xid;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct inode *inode = NULL;
	long rc;
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);

	xid = get_xid();
	if (tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, "", sb, xid);
	else
		rc = cifs_get_inode_info(&inode, "", NULL, sb, xid, NULL);

	if (!inode) {
		inode = ERR_PTR(rc);
		goto out;
	}

#ifdef CONFIG_CIFS_FSCACHE
	/* populate tcon->resource_id */
	tcon->resource_id = CIFS_I(inode)->uniqueid;
#endif

	if (rc && tcon->ipc) {
		cifs_dbg(FYI, "ipc connection - fake read inode\n");
		spin_lock(&inode->i_lock);
		inode->i_mode |= S_IFDIR;
		set_nlink(inode, 2);
		inode->i_op = &cifs_ipc_inode_ops;
		inode->i_fop = &simple_dir_operations;
		inode->i_uid = cifs_sb->mnt_uid;
		inode->i_gid = cifs_sb->mnt_gid;
		spin_unlock(&inode->i_lock);
	} else if (rc) {
		iget_failed(inode);
		inode = ERR_PTR(rc);
	}

out:
	/* can not call macro free_xid here since in a void func
	 * TODO: This is no longer true
	 */
	_free_xid(xid);
	return inode;
}

int
cifs_set_file_info(struct inode *inode, struct iattr *attrs, unsigned int xid,
		   char *full_path, __u32 dosattr)
{
	bool set_time = false;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct TCP_Server_Info *server;
	FILE_BASIC_INFO	info_buf;

	if (attrs == NULL)
		return -EINVAL;

	server = cifs_sb_master_tcon(cifs_sb)->ses->server;
	if (!server->ops->set_file_info)
		return -ENOSYS;

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
	 * Do not set ctime unless other time stamps are changed explicitly
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

	return server->ops->set_file_info(inode, full_path, &info_buf, xid);
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
	struct inode *inode = dentry->d_inode;
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	__u32 dosattr, origattr;
	FILE_BASIC_INFO *info_buf = NULL;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);
	tcon = tlink_tcon(tlink);

	/*
	 * We cannot rename the file if the server doesn't support
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

	origattr = cifsInode->cifsAttrs;
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
			cifsInode->cifsAttrs = dosattr;
		else
			dosattr = origattr; /* since not able to change them */
	}

	/* rename the file */
	rc = CIFSSMBRenameOpenFile(xid, tcon, fid.netfid, NULL,
				   cifs_sb->local_nls,
				   cifs_sb->mnt_cifs_flags &
					    CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc != 0) {
		rc = -EBUSY;
		goto undo_setattr;
	}

	/* try to set DELETE_ON_CLOSE */
	if (!test_bit(CIFS_INO_DELETE_PENDING, &cifsInode->flags)) {
		rc = CIFSSMBSetFileDisposition(xid, tcon, true, fid.netfid,
					       current->tgid);
		/*
		 * some samba versions return -ENOENT when we try to set the
		 * file disposition here. Likely a samba bug, but work around
		 * it for now. This means that some cifsXXX files may hang
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
		set_bit(CIFS_INO_DELETE_PENDING, &cifsInode->flags);
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
				cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					    CIFS_MOUNT_MAP_SPECIAL_CHR);
undo_setattr:
	if (dosattr != origattr) {
		info_buf->Attributes = cpu_to_le32(origattr);
		if (!CIFSSMBSetFileInfo(xid, tcon, info_buf, fid.netfid,
					current->tgid))
			cifsInode->cifsAttrs = origattr;
	}

	goto out_close;
}

/* copied from fs/nfs/dir.c with small changes */
static void
cifs_drop_nlink(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	if (inode->i_nlink > 0)
		drop_nlink(inode);
	spin_unlock(&inode->i_lock);
}

/*
 * If dentry->d_inode is null (usually meaning the cached dentry
 * is a negative dentry) then we would attempt a standard SMB delete, but
 * if that fails we can not attempt the fall back mechanisms on EACCESS
 * but will return the EACCESS to the caller. Note that the VFS does not call
 * unlink on negative dentries currently.
 */
int cifs_unlink(struct inode *dir, struct dentry *dentry)
{
	int rc = 0;
	unsigned int xid;
	char *full_path = NULL;
	struct inode *inode = dentry->d_inode;
	struct cifsInodeInfo *cifs_inode;
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

	/* Unlink can be called from rename so we can not take the
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
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
		cifs_dbg(FYI, "posix del rc %d\n", rc);
		if ((rc == 0) || (rc == -ENOENT))
			goto psx_del_no_retry;
	}

retry_std_delete:
	if (!server->ops->unlink) {
		rc = -ENOSYS;
		goto psx_del_no_retry;
	}

	rc = server->ops->unlink(xid, tcon, full_path, cifs_sb);

psx_del_no_retry:
	if (!rc) {
		if (inode)
			cifs_drop_nlink(inode);
	} else if (rc == -ENOENT) {
		d_drop(dentry);
	} else if (rc == -EBUSY) {
		if (server->ops->rename_pending_delete) {
			rc = server->ops->rename_pending_delete(full_path,
								dentry, xid);
			if (rc == 0)
				cifs_drop_nlink(inode);
		}
	} else if ((rc == -EACCES) && (dosattr == 0) && inode) {
		attrs = kzalloc(sizeof(*attrs), GFP_KERNEL);
		if (attrs == NULL) {
			rc = -ENOMEM;
			goto out_reval;
		}

		/* try to reset dos attributes */
		cifs_inode = CIFS_I(inode);
		origattr = cifs_inode->cifsAttrs;
		if (origattr == 0)
			origattr |= ATTR_NORMAL;
		dosattr = origattr & ~ATTR_READONLY;
		if (dosattr == 0)
			dosattr |= ATTR_NORMAL;
		dosattr |= ATTR_HIDDEN;

		rc = cifs_set_file_info(inode, attrs, xid, full_path, dosattr);
		if (rc != 0)
			goto out_reval;

		goto retry_std_delete;
	}

	/* undo the setattr if we errored out and it's needed */
	if (rc != 0 && dosattr != 0)
		cifs_set_file_info(inode, attrs, xid, full_path, origattr);

out_reval:
	if (inode) {
		cifs_inode = CIFS_I(inode);
		cifs_inode->time = 0;	/* will force revalidate to get info
					   when needed */
		inode->i_ctime = current_fs_time(sb);
	}
	dir->i_ctime = dir->i_mtime = current_fs_time(sb);
	cifs_inode = CIFS_I(dir);
	CIFS_I(dir)->time = 0;	/* force revalidate of dir as well */
unlink_out:
	kfree(full_path);
	kfree(attrs);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

static int
cifs_mkdir_qinfo(struct inode *parent, struct dentry *dentry, umode_t mode,
		 const char *full_path, struct cifs_sb_info *cifs_sb,
		 struct cifs_tcon *tcon, const unsigned int xid)
{
	int rc = 0;
	struct inode *inode = NULL;

	if (tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, full_path, parent->i_sb,
					      xid);
	else
		rc = cifs_get_inode_info(&inode, full_path, NULL, parent->i_sb,
					 xid, NULL);

	if (rc)
		return rc;

	/*
	 * setting nlink not necessary except in cases where we failed to get it
	 * from the server or was set bogus. Also, since this is a brand new
	 * inode, no need to grab the i_lock before setting the i_nlink.
	 */
	if (inode->i_nlink < 2)
		set_nlink(inode, 2);
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
			args.uid = INVALID_UID; /* no change */
			args.gid = INVALID_GID; /* no change */
		}
		CIFSSMBUnixSetPathInfo(xid, tcon, full_path, &args,
				       cifs_sb->local_nls,
				       cifs_sb->mnt_cifs_flags &
				       CIFS_MOUNT_MAP_SPECIAL_CHR);
	} else {
		struct TCP_Server_Info *server = tcon->ses->server;
		if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) &&
		    (mode & S_IWUGO) == 0 && server->ops->mkdir_setinfo)
			server->ops->mkdir_setinfo(inode, full_path, cifs_sb,
						   tcon, xid);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)
			inode->i_mode = (mode | S_IFDIR);

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
			inode->i_uid = current_fsuid();
			if (inode->i_mode & S_ISGID)
				inode->i_gid = parent->i_gid;
			else
				inode->i_gid = current_fsgid();
		}
	}
	d_instantiate(dentry, inode);
	return rc;
}

static int
cifs_posix_mkdir(struct inode *inode, struct dentry *dentry, umode_t mode,
		 const char *full_path, struct cifs_sb_info *cifs_sb,
		 struct cifs_tcon *tcon, const unsigned int xid)
{
	int rc = 0;
	u32 oplock = 0;
	FILE_UNIX_BASIC_INFO *info = NULL;
	struct inode *newinode = NULL;
	struct cifs_fattr fattr;

	info = kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
	if (info == NULL) {
		rc = -ENOMEM;
		goto posix_mkdir_out;
	}

	mode &= ~current_umask();
	rc = CIFSPOSIXCreate(xid, tcon, SMB_O_DIRECTORY | SMB_O_CREAT, mode,
			     NULL /* netfid */, info, &oplock, full_path,
			     cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
			     CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc == -EOPNOTSUPP)
		goto posix_mkdir_out;
	else if (rc) {
		cifs_dbg(FYI, "posix mkdir returned 0x%x\n", rc);
		d_drop(dentry);
		goto posix_mkdir_out;
	}

	if (info->Type == cpu_to_le32(-1))
		/* no return info, go query for it */
		goto posix_mkdir_get_info;
	/*
	 * BB check (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID ) to see if
	 * need to set uid/gid.
	 */

	cifs_unix_basic_to_fattr(&fattr, info, cifs_sb);
	cifs_fill_uniqueid(inode->i_sb, &fattr);
	newinode = cifs_iget(inode->i_sb, &fattr);
	if (!newinode)
		goto posix_mkdir_get_info;

	d_instantiate(dentry, newinode);

#ifdef CONFIG_CIFS_DEBUG2
	cifs_dbg(FYI, "instantiated dentry %p %s to inode %p\n",
		 dentry, dentry->d_name.name, newinode);

	if (newinode->i_nlink != 2)
		cifs_dbg(FYI, "unexpected number of links %d\n",
			 newinode->i_nlink);
#endif

posix_mkdir_out:
	kfree(info);
	return rc;
posix_mkdir_get_info:
	rc = cifs_mkdir_qinfo(inode, dentry, mode, full_path, cifs_sb, tcon,
			      xid);
	goto posix_mkdir_out;
}

int cifs_mkdir(struct inode *inode, struct dentry *direntry, umode_t mode)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_sb_info *cifs_sb;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char *full_path;

	cifs_dbg(FYI, "In cifs_mkdir, mode = 0x%hx inode = 0x%p\n",
		 mode, inode);

	cifs_sb = CIFS_SB(inode->i_sb);
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

	if (cap_unix(tcon->ses) && (CIFS_UNIX_POSIX_PATH_OPS_CAP &
				le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		rc = cifs_posix_mkdir(inode, direntry, mode, full_path, cifs_sb,
				      tcon, xid);
		if (rc != -EOPNOTSUPP)
			goto mkdir_out;
	}

	server = tcon->ses->server;

	if (!server->ops->mkdir) {
		rc = -ENOSYS;
		goto mkdir_out;
	}

	/* BB add setting the equivalent of mode via CreateX w/ACLs */
	rc = server->ops->mkdir(xid, tcon, full_path, cifs_sb);
	if (rc) {
		cifs_dbg(FYI, "cifs_mkdir returned 0x%x\n", rc);
		d_drop(direntry);
		goto mkdir_out;
	}

	rc = cifs_mkdir_qinfo(inode, direntry, mode, full_path, cifs_sb, tcon,
			      xid);
mkdir_out:
	/*
	 * Force revalidate to get parent dir info when needed since cached
	 * attributes are invalid now.
	 */
	CIFS_I(inode)->time = 0;
	kfree(full_path);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

int cifs_rmdir(struct inode *inode, struct dentry *direntry)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_sb_info *cifs_sb;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char *full_path = NULL;
	struct cifsInodeInfo *cifsInode;

	cifs_dbg(FYI, "cifs_rmdir, inode = 0x%p\n", inode);

	xid = get_xid();

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto rmdir_exit;
	}

	cifs_sb = CIFS_SB(inode->i_sb);
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
		spin_lock(&direntry->d_inode->i_lock);
		i_size_write(direntry->d_inode, 0);
		clear_nlink(direntry->d_inode);
		spin_unlock(&direntry->d_inode->i_lock);
	}

	cifsInode = CIFS_I(direntry->d_inode);
	/* force revalidate to go get info when needed */
	cifsInode->time = 0;

	cifsInode = CIFS_I(inode);
	/*
	 * Force revalidate to get parent dir info when needed since cached
	 * attributes are invalid now.
	 */
	cifsInode->time = 0;

	direntry->d_inode->i_ctime = inode->i_ctime = inode->i_mtime =
		current_fs_time(inode->i_sb);

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
	 * source. Note that cross directory moves do not work with
	 * rename by filehandle to various Windows servers.
	 */
	if (rc == 0 || rc != -EBUSY)
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
				cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		CIFSSMBClose(xid, tcon, fid.netfid);
	}
do_rename_exit:
	cifs_put_tlink(tlink);
	return rc;
}

int
cifs_rename2(struct inode *source_dir, struct dentry *source_dentry,
	     struct inode *target_dir, struct dentry *target_dentry,
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
	 * we already have the rename sem so we do not need to
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
		 * Are src and dst hardlinks of same inode? We can only tell
		 * with unix extensions enabled.
		 */
		info_buf_source =
			kmalloc(2 * sizeof(FILE_UNIX_BASIC_INFO),
					GFP_KERNEL);
		if (info_buf_source == NULL) {
			rc = -ENOMEM;
			goto cifs_rename_exit;
		}

		info_buf_target = info_buf_source + 1;
		tmprc = CIFSSMBUnixQPathInfo(xid, tcon, from_name,
					     info_buf_source,
					     cifs_sb->local_nls,
					     cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (tmprc != 0)
			goto unlink_target;

		tmprc = CIFSSMBUnixQPathInfo(xid, tcon, to_name,
					     info_buf_target,
					     cifs_sb->local_nls,
					     cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);

		if (tmprc == 0 && (info_buf_source->UniqueId ==
				   info_buf_target->UniqueId)) {
			/* same file, POSIX says that this is a noop */
			rc = 0;
			goto cifs_rename_exit;
		}
	}
	/*
	 * else ... BB we could add the same check for Windows by
	 * checking the UniqueId via FILE_INTERNAL_INFO
	 */

unlink_target:
	/* Try unlinking the target dentry if it's not negative */
	if (target_dentry->d_inode && (rc == -EACCES || rc == -EEXIST)) {
		tmprc = cifs_unlink(target_dir, target_dentry);
		if (tmprc)
			goto cifs_rename_exit;
		rc = cifs_do_rename(xid, source_dentry, from_name,
				    target_dentry, to_name);
	}

cifs_rename_exit:
	kfree(info_buf_source);
	kfree(from_name);
	kfree(to_name);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

static bool
cifs_inode_needs_reval(struct inode *inode)
{
	struct cifsInodeInfo *cifs_i = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	if (CIFS_CACHE_READ(cifs_i))
		return false;

	if (!lookupCacheEnabled)
		return true;

	if (cifs_i->time == 0)
		return true;

	if (!cifs_sb->actimeo)
		return true;

	if (!time_in_range(jiffies, cifs_i->time,
				cifs_i->time + cifs_sb->actimeo))
		return true;

	/* hardlinked files w/ noserverino get "special" treatment */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) &&
	    S_ISREG(inode->i_mode) && inode->i_nlink != 1)
		return true;

	return false;
}

/*
 * Zap the cache. Called when invalid_mapping flag is set.
 */
int
cifs_invalidate_mapping(struct inode *inode)
{
	int rc = 0;

	if (inode->i_mapping && inode->i_mapping->nrpages != 0) {
		rc = invalidate_inode_pages2(inode->i_mapping);
		if (rc)
			cifs_dbg(VFS, "%s: could not invalidate inode %p\n",
				 __func__, inode);
	}

	cifs_fscache_reset_inode_cookie(inode);
	return rc;
}

/**
 * cifs_wait_bit_killable - helper for functions that are sleeping on bit locks
 * @word: long word containing the bit lock
 */
static int
cifs_wait_bit_killable(void *word)
{
	if (fatal_signal_pending(current))
		return -ERESTARTSYS;
	freezable_schedule_unsafe();
	return 0;
}

int
cifs_revalidate_mapping(struct inode *inode)
{
	int rc;
	unsigned long *flags = &CIFS_I(inode)->flags;

	rc = wait_on_bit_lock(flags, CIFS_INO_LOCK, cifs_wait_bit_killable,
				TASK_KILLABLE);
	if (rc)
		return rc;

	if (test_and_clear_bit(CIFS_INO_INVALID_MAPPING, flags)) {
		rc = cifs_invalidate_mapping(inode);
		if (rc)
			set_bit(CIFS_INO_INVALID_MAPPING, flags);
	}

	clear_bit_unlock(CIFS_INO_LOCK, flags);
	smp_mb__after_atomic();
	wake_up_bit(flags, CIFS_INO_LOCK);

	return rc;
}

int
cifs_zap_mapping(struct inode *inode)
{
	set_bit(CIFS_INO_INVALID_MAPPING, &CIFS_I(inode)->flags);
	return cifs_revalidate_mapping(inode);
}

int cifs_revalidate_file_attr(struct file *filp)
{
	int rc = 0;
	struct inode *inode = file_inode(filp);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *) filp->private_data;

	if (!cifs_inode_needs_reval(inode))
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
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dentry->d_sb;
	char *full_path = NULL;

	if (inode == NULL)
		return -ENOENT;

	if (!cifs_inode_needs_reval(inode))
		return rc;

	xid = get_xid();

	/* can not safely grab the rename sem here if rename calls revalidate
	   since that would deadlock */
	full_path = build_path_from_dentry(dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	cifs_dbg(FYI, "Update attributes: %s inode 0x%p count %d dentry: 0x%p d_time %ld jiffies %ld\n",
		 full_path, inode, inode->i_count.counter,
		 dentry, dentry->d_time, jiffies);

	if (cifs_sb_master_tcon(CIFS_SB(sb))->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, full_path, sb, xid);
	else
		rc = cifs_get_inode_info(&inode, full_path, NULL, sb,
					 xid, NULL);

out:
	kfree(full_path);
	free_xid(xid);
	return rc;
}

int cifs_revalidate_file(struct file *filp)
{
	int rc;
	struct inode *inode = file_inode(filp);

	rc = cifs_revalidate_file_attr(filp);
	if (rc)
		return rc;

	return cifs_revalidate_mapping(inode);
}

/* revalidate a dentry's inode attributes */
int cifs_revalidate_dentry(struct dentry *dentry)
{
	int rc;
	struct inode *inode = dentry->d_inode;

	rc = cifs_revalidate_dentry_attr(dentry);
	if (rc)
		return rc;

	return cifs_revalidate_mapping(inode);
}

int cifs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(dentry->d_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct inode *inode = dentry->d_inode;
	int rc;

	/*
	 * We need to be sure that all dirty pages are written and the server
	 * has actual ctime, mtime and file length.
	 */
	if (!CIFS_CACHE_READ(CIFS_I(inode)) && inode->i_mapping &&
	    inode->i_mapping->nrpages != 0) {
		rc = filemap_fdatawait(inode->i_mapping);
		if (rc) {
			mapping_set_error(inode->i_mapping, rc);
			return rc;
		}
	}

	rc = cifs_revalidate_dentry_attr(dentry);
	if (rc)
		return rc;

	generic_fillattr(inode, stat);
	stat->blksize = CIFS_MAX_MSGSIZE;
	stat->ino = CIFS_I(inode)->uniqueid;

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

static int cifs_truncate_page(struct address_space *mapping, loff_t from)
{
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE - 1);
	struct page *page;
	int rc = 0;

	page = grab_cache_page(mapping, index);
	if (!page)
		return -ENOMEM;

	zero_user_segment(page, offset, PAGE_CACHE_SIZE);
	unlock_page(page);
	page_cache_release(page);
	return rc;
}

static void cifs_setsize(struct inode *inode, loff_t offset)
{
	spin_lock(&inode->i_lock);
	i_size_write(inode, offset);
	spin_unlock(&inode->i_lock);

	truncate_pagecache(inode, offset);
}

static int
cifs_set_file_size(struct inode *inode, struct iattr *attrs,
		   unsigned int xid, char *full_path)
{
	int rc;
	struct cifsFileInfo *open_file;
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink = NULL;
	struct cifs_tcon *tcon = NULL;
	struct TCP_Server_Info *server;
	struct cifs_io_parms io_parms;

	/*
	 * To avoid spurious oplock breaks from server, in the case of
	 * inodes that we already have open, avoid doing path based
	 * setting of file size if we can do it by handle.
	 * This keeps our caching token (oplock) and avoids timeouts
	 * when the local oplock break takes longer to flush
	 * writebehind data than the SMB timeout for the SetPathInfo
	 * request would allow
	 */
	open_file = find_writable_file(cifsInode, true);
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
		if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
			unsigned int bytes_written;

			io_parms.netfid = open_file->fid.netfid;
			io_parms.pid = open_file->pid;
			io_parms.tcon = tcon;
			io_parms.offset = 0;
			io_parms.length = attrs->ia_size;
			rc = CIFSSMBWrite(xid, &io_parms, &bytes_written,
					  NULL, NULL, 1);
			cifs_dbg(FYI, "Wrt seteof rc %d\n", rc);
		}
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
	 * Set file size by pathname rather than by handle either because no
	 * valid, writeable file handle for it was found or because there was
	 * an error setting it by handle.
	 */
	if (server->ops->set_path_size)
		rc = server->ops->set_path_size(xid, tcon, full_path,
						attrs->ia_size, cifs_sb, false);
	else
		rc = -ENOSYS;
	cifs_dbg(FYI, "SetEOF by path (setattrs) rc = %d\n", rc);
	if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
		__u16 netfid;
		int oplock = 0;

		rc = SMBLegacyOpen(xid, tcon, full_path, FILE_OPEN,
				   GENERIC_WRITE, CREATE_NOT_DIR, &netfid,
				   &oplock, NULL, cifs_sb->local_nls,
				   cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc == 0) {
			unsigned int bytes_written;

			io_parms.netfid = netfid;
			io_parms.pid = current->tgid;
			io_parms.tcon = tcon;
			io_parms.offset = 0;
			io_parms.length = attrs->ia_size;
			rc = CIFSSMBWrite(xid, &io_parms, &bytes_written, NULL,
					  NULL,  1);
			cifs_dbg(FYI, "wrt seteof rc %d\n", rc);
			CIFSSMBClose(xid, tcon, netfid);
		}
	}
	if (tlink)
		cifs_put_tlink(tlink);

set_size_out:
	if (rc == 0) {
		cifsInode->server_eof = attrs->ia_size;
		cifs_setsize(inode, attrs->ia_size);
		cifs_truncate_page(inode->i_mapping, inode->i_size);
	}

	return rc;
}

static int
cifs_setattr_unix(struct dentry *direntry, struct iattr *attrs)
{
	int rc;
	unsigned int xid;
	char *full_path = NULL;
	struct inode *inode = direntry->d_inode;
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct tcon_link *tlink;
	struct cifs_tcon *pTcon;
	struct cifs_unix_set_info_args *args = NULL;
	struct cifsFileInfo *open_file;

	cifs_dbg(FYI, "setattr_unix on file %s attrs->ia_valid=0x%x\n",
		 direntry->d_name.name, attrs->ia_valid);

	xid = get_xid();

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM)
		attrs->ia_valid |= ATTR_FORCE;

	rc = inode_change_ok(inode, attrs);
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
	rc = filemap_write_and_wait(inode->i_mapping);
	mapping_set_error(inode->i_mapping, rc);
	rc = 0;

	if (attrs->ia_valid & ATTR_SIZE) {
		rc = cifs_set_file_size(inode, attrs, xid, full_path);
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
		args->uid = INVALID_UID; /* no change */

	if (attrs->ia_valid & ATTR_GID)
		args->gid = attrs->ia_gid;
	else
		args->gid = INVALID_GID; /* no change */

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
	open_file = find_writable_file(cifsInode, true);
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
				    cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		cifs_put_tlink(tlink);
	}

	if (rc)
		goto out;

	if ((attrs->ia_valid & ATTR_SIZE) &&
	    attrs->ia_size != i_size_read(inode))
		truncate_setsize(inode, attrs->ia_size);

	setattr_copy(inode, attrs);
	mark_inode_dirty(inode);

	/* force revalidate when any of these times are set since some
	   of the fs types (eg ext3, fat) do not have fine enough
	   time granularity to match protocol, and we do not have a
	   a way (yet) to query the server fs's time granularity (and
	   whether it rounds times down).
	*/
	if (attrs->ia_valid & (ATTR_MTIME | ATTR_CTIME))
		cifsInode->time = 0;
out:
	kfree(args);
	kfree(full_path);
	free_xid(xid);
	return rc;
}

static int
cifs_setattr_nounix(struct dentry *direntry, struct iattr *attrs)
{
	unsigned int xid;
	kuid_t uid = INVALID_UID;
	kgid_t gid = INVALID_GID;
	struct inode *inode = direntry->d_inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	char *full_path = NULL;
	int rc = -EACCES;
	__u32 dosattr = 0;
	__u64 mode = NO_CHANGE_64;

	xid = get_xid();

	cifs_dbg(FYI, "setattr on file %s attrs->iavalid 0x%x\n",
		 direntry->d_name.name, attrs->ia_valid);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM)
		attrs->ia_valid |= ATTR_FORCE;

	rc = inode_change_ok(inode, attrs);
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
	rc = filemap_write_and_wait(inode->i_mapping);
	mapping_set_error(inode->i_mapping, rc);
	rc = 0;

	if (attrs->ia_valid & ATTR_SIZE) {
		rc = cifs_set_file_size(inode, attrs, xid, full_path);
		if (rc != 0)
			goto cifs_setattr_exit;
	}

	if (attrs->ia_valid & ATTR_UID)
		uid = attrs->ia_uid;

	if (attrs->ia_valid & ATTR_GID)
		gid = attrs->ia_gid;

#ifdef CONFIG_CIFS_ACL
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) {
		if (uid_valid(uid) || gid_valid(gid)) {
			rc = id_mode_to_cifs_acl(inode, full_path, NO_CHANGE_64,
							uid, gid);
			if (rc) {
				cifs_dbg(FYI, "%s: Setting id failed with error: %d\n",
					 __func__, rc);
				goto cifs_setattr_exit;
			}
		}
	} else
#endif /* CONFIG_CIFS_ACL */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID))
		attrs->ia_valid &= ~(ATTR_UID | ATTR_GID);

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attrs->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
		attrs->ia_valid &= ~ATTR_MODE;

	if (attrs->ia_valid & ATTR_MODE) {
		mode = attrs->ia_mode;
		rc = 0;
#ifdef CONFIG_CIFS_ACL
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) {
			rc = id_mode_to_cifs_acl(inode, full_path, mode,
						INVALID_UID, INVALID_GID);
			if (rc) {
				cifs_dbg(FYI, "%s: Setting ACL failed with error: %d\n",
					 __func__, rc);
				goto cifs_setattr_exit;
			}
		} else
#endif /* CONFIG_CIFS_ACL */
		if (((mode & S_IWUGO) == 0) &&
		    (cifsInode->cifsAttrs & ATTR_READONLY) == 0) {

			dosattr = cifsInode->cifsAttrs | ATTR_READONLY;

			/* fix up mode if we're not using dynperm */
			if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM) == 0)
				attrs->ia_mode = inode->i_mode & ~S_IWUGO;
		} else if ((mode & S_IWUGO) &&
			   (cifsInode->cifsAttrs & ATTR_READONLY)) {

			dosattr = cifsInode->cifsAttrs & ~ATTR_READONLY;
			/* Attributes of 0 are ignored */
			if (dosattr == 0)
				dosattr |= ATTR_NORMAL;

			/* reset local inode permissions to normal */
			if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)) {
				attrs->ia_mode &= ~(S_IALLUGO);
				if (S_ISDIR(inode->i_mode))
					attrs->ia_mode |=
						cifs_sb->mnt_dir_mode;
				else
					attrs->ia_mode |=
						cifs_sb->mnt_file_mode;
			}
		} else if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)) {
			/* ignore mode change - ATTR_READONLY hasn't changed */
			attrs->ia_valid &= ~ATTR_MODE;
		}
	}

	if (attrs->ia_valid & (ATTR_MTIME|ATTR_ATIME|ATTR_CTIME) ||
	    ((attrs->ia_valid & ATTR_MODE) && dosattr)) {
		rc = cifs_set_file_info(inode, attrs, xid, full_path, dosattr);
		/* BB: check for rc = -EOPNOTSUPP and switch to legacy mode */

		/* Even if error on time set, no sense failing the call if
		the server would set the time to a reasonable value anyway,
		and this check ensures that we are not being called from
		sys_utimes in which case we ought to fail the call back to
		the user when the server rejects the call */
		if ((rc) && (attrs->ia_valid &
				(ATTR_MODE | ATTR_GID | ATTR_UID | ATTR_SIZE)))
			rc = 0;
	}

	/* do not need local check to inode_check_ok since the server does
	   that */
	if (rc)
		goto cifs_setattr_exit;

	if ((attrs->ia_valid & ATTR_SIZE) &&
	    attrs->ia_size != i_size_read(inode))
		truncate_setsize(inode, attrs->ia_size);

	setattr_copy(inode, attrs);
	mark_inode_dirty(inode);

cifs_setattr_exit:
	kfree(full_path);
	free_xid(xid);
	return rc;
}

int
cifs_setattr(struct dentry *direntry, struct iattr *attrs)
{
	struct inode *inode = direntry->d_inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_tcon *pTcon = cifs_sb_master_tcon(cifs_sb);

	if (pTcon->unix_ext)
		return cifs_setattr_unix(direntry, attrs);

	return cifs_setattr_nounix(direntry, attrs);

	/* BB: add cifs_setattr_legacy for really old servers */
}

#if 0
void cifs_delete_inode(struct inode *inode)
{
	cifs_dbg(FYI, "In cifs_delete_inode, inode = 0x%p\n", inode);
	/* may have to add back in if and when safe distributed caching of
	   directories added e.g. via FindNotify */
}
#endif
