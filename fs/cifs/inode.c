/*
 *   fs/cifs/inode.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
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
#include <linux/pagemap.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"


static void cifs_set_ops(struct inode *inode, const bool is_dfs_referral)
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
		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			inode->i_fop = &cifs_file_nobrl_ops;
		else { /* not direct, send byte range locks */
			inode->i_fop = &cifs_file_ops;
		}


		/* check if server can support readpages */
		if (cifs_sb->tcon->ses->server->maxBuf <
				PAGE_CACHE_SIZE + MAX_CIFS_HDR_SIZE)
			inode->i_data.a_ops = &cifs_addr_ops_smallbuf;
		else
			inode->i_data.a_ops = &cifs_addr_ops;
		break;
	case S_IFDIR:
#ifdef CONFIG_CIFS_DFS_UPCALL
		if (is_dfs_referral) {
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

/* populate an inode with info from a cifs_fattr struct */
void
cifs_fattr_to_inode(struct inode *inode, struct cifs_fattr *fattr)
{
	struct cifsInodeInfo *cifs_i = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	unsigned long oldtime = cifs_i->time;

	inode->i_atime = fattr->cf_atime;
	inode->i_mtime = fattr->cf_mtime;
	inode->i_ctime = fattr->cf_ctime;
	inode->i_rdev = fattr->cf_rdev;
	inode->i_nlink = fattr->cf_nlink;
	inode->i_uid = fattr->cf_uid;
	inode->i_gid = fattr->cf_gid;

	/* if dynperm is set, don't clobber existing mode */
	if (inode->i_state & I_NEW ||
	    !(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM))
		inode->i_mode = fattr->cf_mode;

	cifs_i->cifsAttrs = fattr->cf_cifsattrs;
	cifs_i->uniqueid = fattr->cf_uniqueid;

	if (fattr->cf_flags & CIFS_FATTR_NEED_REVAL)
		cifs_i->time = 0;
	else
		cifs_i->time = jiffies;

	cFYI(1, ("inode 0x%p old_time=%ld new_time=%ld", inode,
		 oldtime, cifs_i->time));

	cifs_i->delete_pending = fattr->cf_flags & CIFS_FATTR_DELETE_PENDING;

	/*
	 * Can't safely change the file size here if the client is writing to
	 * it due to potential races.
	 */
	spin_lock(&inode->i_lock);
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

	cifs_set_ops(inode, fattr->cf_flags & CIFS_FATTR_DFS_REFERRAL);
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
		cFYI(1, ("unknown type %d", le32_to_cpu(info->Type)));
		break;
	}

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_UID)
		fattr->cf_uid = cifs_sb->mnt_uid;
	else
		fattr->cf_uid = le64_to_cpu(info->Uid);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_GID)
		fattr->cf_gid = cifs_sb->mnt_gid;
	else
		fattr->cf_gid = le64_to_cpu(info->Gid);

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

	cFYI(1, ("creating fake fattr for DFS referral"));

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

int cifs_get_inode_info_unix(struct inode **pinode,
			     const unsigned char *full_path,
			     struct super_block *sb, int xid)
{
	int rc;
	FILE_UNIX_BASIC_INFO find_data;
	struct cifs_fattr fattr;
	struct cifsTconInfo *tcon;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);

	tcon = cifs_sb->tcon;
	cFYI(1, ("Getting info on %s", full_path));

	/* could have done a find first instead but this returns more info */
	rc = CIFSSMBUnixQPathInfo(xid, tcon, full_path, &find_data,
				  cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (!rc) {
		cifs_unix_basic_to_fattr(&fattr, &find_data, cifs_sb);
	} else if (rc == -EREMOTE) {
		cifs_create_dfs_fattr(&fattr, sb);
		rc = 0;
	} else {
		return rc;
	}

	if (*pinode == NULL) {
		/* get new inode */
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
cifs_sfu_type(struct cifs_fattr *fattr, const unsigned char *path,
	      struct cifs_sb_info *cifs_sb, int xid)
{
	int rc;
	int oplock = 0;
	__u16 netfid;
	struct cifsTconInfo *pTcon = cifs_sb->tcon;
	char buf[24];
	unsigned int bytes_read;
	char *pbuf;

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

	rc = CIFSSMBOpen(xid, pTcon, path, FILE_OPEN, GENERIC_READ,
			 CREATE_NOT_DIR, &netfid, &oplock, NULL,
			 cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc == 0) {
		int buf_type = CIFS_NO_BUFFER;
			/* Read header */
		rc = CIFSSMBRead(xid, pTcon, netfid,
				 24 /* length */, 0 /* offset */,
				 &bytes_read, &pbuf, &buf_type);
		if ((rc == 0) && (bytes_read >= 8)) {
			if (memcmp("IntxBLK", pbuf, 8) == 0) {
				cFYI(1, ("Block device"));
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
				cFYI(1, ("Char device"));
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
				cFYI(1, ("Symlink"));
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
		CIFSSMBClose(xid, pTcon, netfid);
	}
	return rc;
}

#define SFBITS_MASK (S_ISVTX | S_ISGID | S_ISUID)  /* SETFILEBITS valid bits */

/*
 * Fetch mode bits as provided by SFU.
 *
 * FIXME: Doesn't this clobber the type bit we got from cifs_sfu_type ?
 */
static int cifs_sfu_mode(struct cifs_fattr *fattr, const unsigned char *path,
			 struct cifs_sb_info *cifs_sb, int xid)
{
#ifdef CONFIG_CIFS_XATTR
	ssize_t rc;
	char ea_value[4];
	__u32 mode;

	rc = CIFSSMBQueryEA(xid, cifs_sb->tcon, path, "SETFILEBITS",
			    ea_value, 4 /* size of buf */, cifs_sb->local_nls,
			    cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc < 0)
		return (int)rc;
	else if (rc > 3) {
		mode = le32_to_cpu(*((__le32 *)ea_value));
		fattr->cf_mode &= ~SFBITS_MASK;
		cFYI(1, ("special bits 0%o org mode 0%o", mode,
			 fattr->cf_mode));
		fattr->cf_mode = (mode & SFBITS_MASK) | fattr->cf_mode;
		cFYI(1, ("special mode bits 0%o", mode));
	}

	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

/* Fill a cifs_fattr struct with info from FILE_ALL_INFO */
static void
cifs_all_info_to_fattr(struct cifs_fattr *fattr, FILE_ALL_INFO *info,
		       struct cifs_sb_info *cifs_sb, bool adjust_tz)
{
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
		fattr->cf_ctime.tv_sec += cifs_sb->tcon->ses->server->timeAdj;
		fattr->cf_mtime.tv_sec += cifs_sb->tcon->ses->server->timeAdj;
	}

	fattr->cf_eof = le64_to_cpu(info->EndOfFile);
	fattr->cf_bytes = le64_to_cpu(info->AllocationSize);

	if (fattr->cf_cifsattrs & ATTR_DIRECTORY) {
		fattr->cf_mode = S_IFDIR | cifs_sb->mnt_dir_mode;
		fattr->cf_dtype = DT_DIR;
	} else {
		fattr->cf_mode = S_IFREG | cifs_sb->mnt_file_mode;
		fattr->cf_dtype = DT_REG;

		/* clear write bits if ATTR_READONLY is set */
		if (fattr->cf_cifsattrs & ATTR_READONLY)
			fattr->cf_mode &= ~(S_IWUGO);
	}

	fattr->cf_nlink = le32_to_cpu(info->NumberOfLinks);

	fattr->cf_uid = cifs_sb->mnt_uid;
	fattr->cf_gid = cifs_sb->mnt_gid;
}

int cifs_get_inode_info(struct inode **pinode,
	const unsigned char *full_path, FILE_ALL_INFO *pfindData,
	struct super_block *sb, int xid, const __u16 *pfid)
{
	int rc = 0, tmprc;
	struct cifsTconInfo *pTcon;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *buf = NULL;
	bool adjustTZ = false;
	struct cifs_fattr fattr;

	pTcon = cifs_sb->tcon;
	cFYI(1, ("Getting info on %s", full_path));

	if ((pfindData == NULL) && (*pinode != NULL)) {
		if (CIFS_I(*pinode)->clientCanCacheRead) {
			cFYI(1, ("No need to revalidate cached inode sizes"));
			return rc;
		}
	}

	/* if file info not passed in then get it from server */
	if (pfindData == NULL) {
		buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;
		pfindData = (FILE_ALL_INFO *)buf;

		/* could do find first instead but this returns more info */
		rc = CIFSSMBQPathInfo(xid, pTcon, full_path, pfindData,
			      0 /* not legacy */,
			      cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
		/* BB optimize code so we do not make the above call
		when server claims no NT SMB support and the above call
		failed at least once - set flag in tcon or mount */
		if ((rc == -EOPNOTSUPP) || (rc == -EINVAL)) {
			rc = SMBQueryInformation(xid, pTcon, full_path,
					pfindData, cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
					  CIFS_MOUNT_MAP_SPECIAL_CHR);
			adjustTZ = true;
		}
	}

	if (!rc) {
		cifs_all_info_to_fattr(&fattr, (FILE_ALL_INFO *) pfindData,
				       cifs_sb, adjustTZ);
	} else if (rc == -EREMOTE) {
		cifs_create_dfs_fattr(&fattr, sb);
		rc = 0;
	} else {
		goto cgii_exit;
	}

	/*
	 * If an inode wasn't passed in, then get the inode number
	 *
	 * Is an i_ino of zero legal? Can we use that to check if the server
	 * supports returning inode numbers?  Are there other sanity checks we
	 * can use to ensure that the server is really filling in that field?
	 *
	 * We can not use the IndexNumber field by default from Windows or
	 * Samba (in ALL_INFO buf) but we can request it explicitly. The SNIA
	 * CIFS spec claims that this value is unique within the scope of a
	 * share, and the windows docs hint that it's actually unique
	 * per-machine.
	 *
	 * There may be higher info levels that work but are there Windows
	 * server or network appliances for which IndexNumber field is not
	 * guaranteed unique?
	 */
	if (*pinode == NULL) {
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
			int rc1 = 0;

			rc1 = CIFSGetSrvInodeNumber(xid, pTcon,
					full_path, &fattr.cf_uniqueid,
					cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			if (rc1 || !fattr.cf_uniqueid) {
				cFYI(1, ("GetSrvInodeNum rc %d", rc1));
				fattr.cf_uniqueid = iunique(sb, ROOT_I);
				cifs_autodisable_serverino(cifs_sb);
			}
		} else {
			fattr.cf_uniqueid = iunique(sb, ROOT_I);
		}
	} else {
		fattr.cf_uniqueid = CIFS_I(*pinode)->uniqueid;
	}

	/* query for SFU type info if supported and needed */
	if (fattr.cf_cifsattrs & ATTR_SYSTEM &&
	    cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) {
		tmprc = cifs_sfu_type(&fattr, full_path, cifs_sb, xid);
		if (tmprc)
			cFYI(1, ("cifs_sfu_type failed: %d", tmprc));
	}

#ifdef CONFIG_CIFS_EXPERIMENTAL
	/* fill in 0777 bits from ACL */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) {
		cFYI(1, ("Getting mode bits from ACL"));
		cifs_acl_to_fattr(cifs_sb, &fattr, *pinode, full_path, pfid);
	}
#endif

	/* fill in remaining high mode bits e.g. SUID, VTX */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL)
		cifs_sfu_mode(&fattr, full_path, cifs_sb, xid);

	if (!*pinode) {
		*pinode = cifs_iget(sb, &fattr);
		if (!*pinode)
			rc = -ENOMEM;
	} else {
		cifs_fattr_to_inode(*pinode, &fattr);
	}

cgii_exit:
	kfree(buf);
	return rc;
}

static const struct inode_operations cifs_ipc_inode_ops = {
	.lookup = cifs_lookup,
};

char *cifs_build_path_to_root(struct cifs_sb_info *cifs_sb)
{
	int pplen = cifs_sb->prepathlen;
	int dfsplen;
	char *full_path = NULL;

	/* if no prefix path, simply set path to the root of share to "" */
	if (pplen == 0) {
		full_path = kmalloc(1, GFP_KERNEL);
		if (full_path)
			full_path[0] = 0;
		return full_path;
	}

	if (cifs_sb->tcon && (cifs_sb->tcon->Flags & SMB_SHARE_IS_IN_DFS))
		dfsplen = strnlen(cifs_sb->tcon->treeName, MAX_TREE_SIZE + 1);
	else
		dfsplen = 0;

	full_path = kmalloc(dfsplen + pplen + 1, GFP_KERNEL);
	if (full_path == NULL)
		return full_path;

	if (dfsplen) {
		strncpy(full_path, cifs_sb->tcon->treeName, dfsplen);
		/* switch slash direction in prepath depending on whether
		 * windows or posix style path names
		 */
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) {
			int i;
			for (i = 0; i < dfsplen; i++) {
				if (full_path[i] == '\\')
					full_path[i] = '/';
			}
		}
	}
	strncpy(full_path + dfsplen, cifs_sb->prepath, pplen);
	full_path[dfsplen + pplen] = 0; /* add trailing null */
	return full_path;
}

static int
cifs_find_inode(struct inode *inode, void *opaque)
{
	struct cifs_fattr *fattr = (struct cifs_fattr *) opaque;

	if (CIFS_I(inode)->uniqueid != fattr->cf_uniqueid)
		return 0;

	return 1;
}

static int
cifs_init_inode(struct inode *inode, void *opaque)
{
	struct cifs_fattr *fattr = (struct cifs_fattr *) opaque;

	CIFS_I(inode)->uniqueid = fattr->cf_uniqueid;
	return 0;
}

/* Given fattrs, get a corresponding inode */
struct inode *
cifs_iget(struct super_block *sb, struct cifs_fattr *fattr)
{
	unsigned long hash;
	struct inode *inode;

	cFYI(1, ("looking for uniqueid=%llu", fattr->cf_uniqueid));

	/* hash down to 32-bits on 32-bit arch */
	hash = cifs_uniqueid_to_ino_t(fattr->cf_uniqueid);

	inode = iget5_locked(sb, hash, cifs_find_inode, cifs_init_inode, fattr);

	/* we have fattrs in hand, update the inode */
	if (inode) {
		cifs_fattr_to_inode(inode, fattr);
		if (sb->s_flags & MS_NOATIME)
			inode->i_flags |= S_NOATIME | S_NOCMTIME;
		if (inode->i_state & I_NEW) {
			inode->i_ino = hash;
			unlock_new_inode(inode);
		}
	}

	return inode;
}

/* gets root inode */
struct inode *cifs_root_iget(struct super_block *sb, unsigned long ino)
{
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct inode *inode = NULL;
	long rc;
	char *full_path;

	cifs_sb = CIFS_SB(sb);
	full_path = cifs_build_path_to_root(cifs_sb);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	xid = GetXid();
	if (cifs_sb->tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, full_path, sb, xid);
	else
		rc = cifs_get_inode_info(&inode, full_path, NULL, sb,
						xid, NULL);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (rc && cifs_sb->tcon->ipc) {
		cFYI(1, ("ipc connection - fake read inode"));
		inode->i_mode |= S_IFDIR;
		inode->i_nlink = 2;
		inode->i_op = &cifs_ipc_inode_ops;
		inode->i_fop = &simple_dir_operations;
		inode->i_uid = cifs_sb->mnt_uid;
		inode->i_gid = cifs_sb->mnt_gid;
	} else if (rc) {
		kfree(full_path);
		_FreeXid(xid);
		iget_failed(inode);
		return ERR_PTR(rc);
	}


	kfree(full_path);
	/* can not call macro FreeXid here since in a void func
	 * TODO: This is no longer true
	 */
	_FreeXid(xid);
	return inode;
}

static int
cifs_set_file_info(struct inode *inode, struct iattr *attrs, int xid,
		    char *full_path, __u32 dosattr)
{
	int rc;
	int oplock = 0;
	__u16 netfid;
	__u32 netpid;
	bool set_time = false;
	struct cifsFileInfo *open_file;
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsTconInfo *pTcon = cifs_sb->tcon;
	FILE_BASIC_INFO	info_buf;

	if (attrs == NULL)
		return -EINVAL;

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
		cFYI(1, ("CIFS - CTIME changed"));
		info_buf.ChangeTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_ctime));
	} else
		info_buf.ChangeTime = 0;

	info_buf.CreationTime = 0;	/* don't change */
	info_buf.Attributes = cpu_to_le32(dosattr);

	/*
	 * If the file is already open for write, just use that fileid
	 */
	open_file = find_writable_file(cifsInode);
	if (open_file) {
		netfid = open_file->netfid;
		netpid = open_file->pid;
		goto set_via_filehandle;
	}

	/*
	 * NT4 apparently returns success on this call, but it doesn't
	 * really work.
	 */
	if (!(pTcon->ses->flags & CIFS_SES_NT4)) {
		rc = CIFSSMBSetPathInfo(xid, pTcon, full_path,
				     &info_buf, cifs_sb->local_nls,
				     cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc == 0) {
			cifsInode->cifsAttrs = dosattr;
			goto out;
		} else if (rc != -EOPNOTSUPP && rc != -EINVAL)
			goto out;
	}

	cFYI(1, ("calling SetFileInfo since SetPathInfo for "
		 "times not supported by this server"));
	rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN,
			 SYNCHRONIZE | FILE_WRITE_ATTRIBUTES,
			 CREATE_NOT_DIR, &netfid, &oplock,
			 NULL, cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (rc != 0) {
		if (rc == -EIO)
			rc = -EINVAL;
		goto out;
	}

	netpid = current->tgid;

set_via_filehandle:
	rc = CIFSSMBSetFileInfo(xid, pTcon, &info_buf, netfid, netpid);
	if (!rc)
		cifsInode->cifsAttrs = dosattr;

	if (open_file == NULL)
		CIFSSMBClose(xid, pTcon, netfid);
	else
		cifsFileInfo_put(open_file);
out:
	return rc;
}

/*
 * open the given file (if it isn't already), set the DELETE_ON_CLOSE bit
 * and rename it to a random name that hopefully won't conflict with
 * anything else.
 */
static int
cifs_rename_pending_delete(char *full_path, struct dentry *dentry, int xid)
{
	int oplock = 0;
	int rc;
	__u16 netfid;
	struct inode *inode = dentry->d_inode;
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsTconInfo *tcon = cifs_sb->tcon;
	__u32 dosattr, origattr;
	FILE_BASIC_INFO *info_buf = NULL;

	rc = CIFSSMBOpen(xid, tcon, full_path, FILE_OPEN,
			 DELETE|FILE_WRITE_ATTRIBUTES, CREATE_NOT_DIR,
			 &netfid, &oplock, NULL, cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
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
		rc = CIFSSMBSetFileInfo(xid, tcon, info_buf, netfid,
					current->tgid);
		/* although we would like to mark the file hidden
 		   if that fails we will still try to rename it */
		if (rc != 0)
			cifsInode->cifsAttrs = dosattr;
		else
			dosattr = origattr; /* since not able to change them */
	}

	/* rename the file */
	rc = CIFSSMBRenameOpenFile(xid, tcon, netfid, NULL, cifs_sb->local_nls,
				   cifs_sb->mnt_cifs_flags &
					    CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc != 0) {
		rc = -ETXTBSY;
		goto undo_setattr;
	}

	/* try to set DELETE_ON_CLOSE */
	if (!cifsInode->delete_pending) {
		rc = CIFSSMBSetFileDisposition(xid, tcon, true, netfid,
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
			rc = -ETXTBSY;
			goto undo_rename;
		}
		cifsInode->delete_pending = true;
	}

out_close:
	CIFSSMBClose(xid, tcon, netfid);
out:
	kfree(info_buf);
	return rc;

	/*
	 * reset everything back to the original state. Don't bother
	 * dealing with errors here since we can't do anything about
	 * them anyway.
	 */
undo_rename:
	CIFSSMBRenameOpenFile(xid, tcon, netfid, dentry->d_name.name,
				cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					    CIFS_MOUNT_MAP_SPECIAL_CHR);
undo_setattr:
	if (dosattr != origattr) {
		info_buf->Attributes = cpu_to_le32(origattr);
		if (!CIFSSMBSetFileInfo(xid, tcon, info_buf, netfid,
					current->tgid))
			cifsInode->cifsAttrs = origattr;
	}

	goto out_close;
}


/*
 * If dentry->d_inode is null (usually meaning the cached dentry
 * is a negative dentry) then we would attempt a standard SMB delete, but
 * if that fails we can not attempt the fall back mechanisms on EACESS
 * but will return the EACESS to the caller.  Note that the VFS does not call
 * unlink on negative dentries currently.
 */
int cifs_unlink(struct inode *dir, struct dentry *dentry)
{
	int rc = 0;
	int xid;
	char *full_path = NULL;
	struct inode *inode = dentry->d_inode;
	struct cifsInodeInfo *cifs_inode;
	struct super_block *sb = dir->i_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsTconInfo *tcon = cifs_sb->tcon;
	struct iattr *attrs = NULL;
	__u32 dosattr = 0, origattr = 0;

	cFYI(1, ("cifs_unlink, dir=0x%p, dentry=0x%p", dir, dentry));

	xid = GetXid();

	/* Unlink can be called from rename so we can not take the
	 * sb->s_vfs_rename_mutex here */
	full_path = build_path_from_dentry(dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		FreeXid(xid);
		return rc;
	}

	if ((tcon->ses->capabilities & CAP_UNIX) &&
		(CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		rc = CIFSPOSIXDelFile(xid, tcon, full_path,
			SMB_POSIX_UNLINK_FILE_TARGET, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
		cFYI(1, ("posix del rc %d", rc));
		if ((rc == 0) || (rc == -ENOENT))
			goto psx_del_no_retry;
	}

retry_std_delete:
	rc = CIFSSMBDelFile(xid, tcon, full_path, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);

psx_del_no_retry:
	if (!rc) {
		if (inode)
			drop_nlink(inode);
	} else if (rc == -ENOENT) {
		d_drop(dentry);
	} else if (rc == -ETXTBSY) {
		rc = cifs_rename_pending_delete(full_path, dentry, xid);
		if (rc == 0)
			drop_nlink(inode);
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

	kfree(full_path);
	kfree(attrs);
	FreeXid(xid);
	return rc;
}

int cifs_mkdir(struct inode *inode, struct dentry *direntry, int mode)
{
	int rc = 0, tmprc;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;
	struct cifs_fattr fattr;

	cFYI(1, ("In cifs_mkdir, mode = 0x%x inode = 0x%p", mode, inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		FreeXid(xid);
		return rc;
	}

	if ((pTcon->ses->capabilities & CAP_UNIX) &&
		(CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(pTcon->fsUnixInfo.Capability))) {
		u32 oplock = 0;
		FILE_UNIX_BASIC_INFO *pInfo =
			kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
		if (pInfo == NULL) {
			rc = -ENOMEM;
			goto mkdir_out;
		}

		mode &= ~current_umask();
		rc = CIFSPOSIXCreate(xid, pTcon, SMB_O_DIRECTORY | SMB_O_CREAT,
				mode, NULL /* netfid */, pInfo, &oplock,
				full_path, cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc == -EOPNOTSUPP) {
			kfree(pInfo);
			goto mkdir_retry_old;
		} else if (rc) {
			cFYI(1, ("posix mkdir returned 0x%x", rc));
			d_drop(direntry);
		} else {
			if (pInfo->Type == cpu_to_le32(-1)) {
				/* no return info, go query for it */
				kfree(pInfo);
				goto mkdir_get_info;
			}
/*BB check (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID ) to see if need
	to set uid/gid */
			inc_nlink(inode);
			if (pTcon->nocase)
				direntry->d_op = &cifs_ci_dentry_ops;
			else
				direntry->d_op = &cifs_dentry_ops;

			cifs_unix_basic_to_fattr(&fattr, pInfo, cifs_sb);
			newinode = cifs_iget(inode->i_sb, &fattr);
			if (!newinode) {
				kfree(pInfo);
				goto mkdir_get_info;
			}

			d_instantiate(direntry, newinode);

#ifdef CONFIG_CIFS_DEBUG2
			cFYI(1, ("instantiated dentry %p %s to inode %p",
				direntry, direntry->d_name.name, newinode));

			if (newinode->i_nlink != 2)
				cFYI(1, ("unexpected number of links %d",
					newinode->i_nlink));
#endif
		}
		kfree(pInfo);
		goto mkdir_out;
	}
mkdir_retry_old:
	/* BB add setting the equivalent of mode via CreateX w/ACLs */
	rc = CIFSSMBMkDir(xid, pTcon, full_path, cifs_sb->local_nls,
			  cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc) {
		cFYI(1, ("cifs_mkdir returned 0x%x", rc));
		d_drop(direntry);
	} else {
mkdir_get_info:
		inc_nlink(inode);
		if (pTcon->unix_ext)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb, xid);
		else
			rc = cifs_get_inode_info(&newinode, full_path, NULL,
						 inode->i_sb, xid, NULL);

		if (pTcon->nocase)
			direntry->d_op = &cifs_ci_dentry_ops;
		else
			direntry->d_op = &cifs_dentry_ops;
		d_instantiate(direntry, newinode);
		 /* setting nlink not necessary except in cases where we
		  * failed to get it from the server or was set bogus */
		if ((direntry->d_inode) && (direntry->d_inode->i_nlink < 2))
				direntry->d_inode->i_nlink = 2;

		mode &= ~current_umask();
		/* must turn on setgid bit if parent dir has it */
		if (inode->i_mode & S_ISGID)
			mode |= S_ISGID;

		if (pTcon->unix_ext) {
			struct cifs_unix_set_info_args args = {
				.mode	= mode,
				.ctime	= NO_CHANGE_64,
				.atime	= NO_CHANGE_64,
				.mtime	= NO_CHANGE_64,
				.device	= 0,
			};
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
				args.uid = (__u64)current_fsuid();
				if (inode->i_mode & S_ISGID)
					args.gid = (__u64)inode->i_gid;
				else
					args.gid = (__u64)current_fsgid();
			} else {
				args.uid = NO_CHANGE_64;
				args.gid = NO_CHANGE_64;
			}
			CIFSSMBUnixSetPathInfo(xid, pTcon, full_path, &args,
					       cifs_sb->local_nls,
					       cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		} else {
			if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) &&
			    (mode & S_IWUGO) == 0) {
				FILE_BASIC_INFO pInfo;
				struct cifsInodeInfo *cifsInode;
				u32 dosattrs;

				memset(&pInfo, 0, sizeof(pInfo));
				cifsInode = CIFS_I(newinode);
				dosattrs = cifsInode->cifsAttrs|ATTR_READONLY;
				pInfo.Attributes = cpu_to_le32(dosattrs);
				tmprc = CIFSSMBSetPathInfo(xid, pTcon,
						full_path, &pInfo,
						cifs_sb->local_nls,
						cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
				if (tmprc == 0)
					cifsInode->cifsAttrs = dosattrs;
			}
			if (direntry->d_inode) {
				if (cifs_sb->mnt_cifs_flags &
				     CIFS_MOUNT_DYNPERM)
					direntry->d_inode->i_mode =
						(mode | S_IFDIR);

				if (cifs_sb->mnt_cifs_flags &
				     CIFS_MOUNT_SET_UID) {
					direntry->d_inode->i_uid =
						current_fsuid();
					if (inode->i_mode & S_ISGID)
						direntry->d_inode->i_gid =
							inode->i_gid;
					else
						direntry->d_inode->i_gid =
							current_fsgid();
				}
			}
		}
	}
mkdir_out:
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int cifs_rmdir(struct inode *inode, struct dentry *direntry)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct cifsInodeInfo *cifsInode;

	cFYI(1, ("cifs_rmdir, inode = 0x%p", inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		FreeXid(xid);
		return rc;
	}

	rc = CIFSSMBRmDir(xid, pTcon, full_path, cifs_sb->local_nls,
			  cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (!rc) {
		drop_nlink(inode);
		spin_lock(&direntry->d_inode->i_lock);
		i_size_write(direntry->d_inode, 0);
		clear_nlink(direntry->d_inode);
		spin_unlock(&direntry->d_inode->i_lock);
	}

	cifsInode = CIFS_I(direntry->d_inode);
	cifsInode->time = 0;	/* force revalidate to go get info when
				   needed */

	cifsInode = CIFS_I(inode);
	cifsInode->time = 0;	/* force revalidate to get parent dir info
				   since cached search results now invalid */

	direntry->d_inode->i_ctime = inode->i_ctime = inode->i_mtime =
		current_fs_time(inode->i_sb);

	kfree(full_path);
	FreeXid(xid);
	return rc;
}

static int
cifs_do_rename(int xid, struct dentry *from_dentry, const char *fromPath,
		struct dentry *to_dentry, const char *toPath)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(from_dentry->d_sb);
	struct cifsTconInfo *pTcon = cifs_sb->tcon;
	__u16 srcfid;
	int oplock, rc;

	/* try path-based rename first */
	rc = CIFSSMBRename(xid, pTcon, fromPath, toPath, cifs_sb->local_nls,
			   cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);

	/*
	 * don't bother with rename by filehandle unless file is busy and
	 * source Note that cross directory moves do not work with
	 * rename by filehandle to various Windows servers.
	 */
	if (rc == 0 || rc != -ETXTBSY)
		return rc;

	/* open the file to be renamed -- we need DELETE perms */
	rc = CIFSSMBOpen(xid, pTcon, fromPath, FILE_OPEN, DELETE,
			 CREATE_NOT_DIR, &srcfid, &oplock, NULL,
			 cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (rc == 0) {
		rc = CIFSSMBRenameOpenFile(xid, pTcon, srcfid,
				(const char *) to_dentry->d_name.name,
				cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);

		CIFSSMBClose(xid, pTcon, srcfid);
	}

	return rc;
}

int cifs_rename(struct inode *source_dir, struct dentry *source_dentry,
	struct inode *target_dir, struct dentry *target_dentry)
{
	char *fromName = NULL;
	char *toName = NULL;
	struct cifs_sb_info *cifs_sb_source;
	struct cifs_sb_info *cifs_sb_target;
	struct cifsTconInfo *tcon;
	FILE_UNIX_BASIC_INFO *info_buf_source = NULL;
	FILE_UNIX_BASIC_INFO *info_buf_target;
	int xid, rc, tmprc;

	cifs_sb_target = CIFS_SB(target_dir->i_sb);
	cifs_sb_source = CIFS_SB(source_dir->i_sb);
	tcon = cifs_sb_source->tcon;

	xid = GetXid();

	/*
	 * BB: this might be allowed if same server, but different share.
	 * Consider adding support for this
	 */
	if (tcon != cifs_sb_target->tcon) {
		rc = -EXDEV;
		goto cifs_rename_exit;
	}

	/*
	 * we already have the rename sem so we do not need to
	 * grab it again here to protect the path integrity
	 */
	fromName = build_path_from_dentry(source_dentry);
	if (fromName == NULL) {
		rc = -ENOMEM;
		goto cifs_rename_exit;
	}

	toName = build_path_from_dentry(target_dentry);
	if (toName == NULL) {
		rc = -ENOMEM;
		goto cifs_rename_exit;
	}

	rc = cifs_do_rename(xid, source_dentry, fromName,
			    target_dentry, toName);

	if (rc == -EEXIST && tcon->unix_ext) {
		/*
		 * Are src and dst hardlinks of same inode? We can
		 * only tell with unix extensions enabled
		 */
		info_buf_source =
			kmalloc(2 * sizeof(FILE_UNIX_BASIC_INFO),
					GFP_KERNEL);
		if (info_buf_source == NULL) {
			rc = -ENOMEM;
			goto cifs_rename_exit;
		}

		info_buf_target = info_buf_source + 1;
		tmprc = CIFSSMBUnixQPathInfo(xid, tcon, fromName,
					info_buf_source,
					cifs_sb_source->local_nls,
					cifs_sb_source->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (tmprc != 0)
			goto unlink_target;

		tmprc = CIFSSMBUnixQPathInfo(xid, tcon,
					toName, info_buf_target,
					cifs_sb_target->local_nls,
					/* remap based on source sb */
					cifs_sb_source->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);

		if (tmprc == 0 && (info_buf_source->UniqueId ==
				   info_buf_target->UniqueId)) {
			/* same file, POSIX says that this is a noop */
			rc = 0;
			goto cifs_rename_exit;
		}
	} /* else ... BB we could add the same check for Windows by
		     checking the UniqueId via FILE_INTERNAL_INFO */

unlink_target:
	/* Try unlinking the target dentry if it's not negative */
	if (target_dentry->d_inode && (rc == -EACCES || rc == -EEXIST)) {
		tmprc = cifs_unlink(target_dir, target_dentry);
		if (tmprc)
			goto cifs_rename_exit;

		rc = cifs_do_rename(xid, source_dentry, fromName,
				    target_dentry, toName);
	}

cifs_rename_exit:
	kfree(info_buf_source);
	kfree(fromName);
	kfree(toName);
	FreeXid(xid);
	return rc;
}

int cifs_revalidate(struct dentry *direntry)
{
	int xid;
	int rc = 0, wbrc = 0;
	char *full_path;
	struct cifs_sb_info *cifs_sb;
	struct cifsInodeInfo *cifsInode;
	loff_t local_size;
	struct timespec local_mtime;
	bool invalidate_inode = false;

	if (direntry->d_inode == NULL)
		return -ENOENT;

	cifsInode = CIFS_I(direntry->d_inode);

	if (cifsInode == NULL)
		return -ENOENT;

	/* no sense revalidating inode info on file that no one can write */
	if (CIFS_I(direntry->d_inode)->clientCanCacheRead)
		return rc;

	xid = GetXid();

	cifs_sb = CIFS_SB(direntry->d_sb);

	/* can not safely grab the rename sem here if rename calls revalidate
	   since that would deadlock */
	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		FreeXid(xid);
		return rc;
	}
	cFYI(1, ("Revalidate: %s inode 0x%p count %d dentry: 0x%p d_time %ld "
		 "jiffies %ld", full_path, direntry->d_inode,
		 direntry->d_inode->i_count.counter, direntry,
		 direntry->d_time, jiffies));

	if (cifsInode->time == 0) {
		/* was set to zero previously to force revalidate */
	} else if (time_before(jiffies, cifsInode->time + HZ) &&
		   lookupCacheEnabled) {
		if ((S_ISREG(direntry->d_inode->i_mode) == 0) ||
		    (direntry->d_inode->i_nlink == 1)) {
			kfree(full_path);
			FreeXid(xid);
			return rc;
		} else {
			cFYI(1, ("Have to revalidate file due to hardlinks"));
		}
	}

	/* save mtime and size */
	local_mtime = direntry->d_inode->i_mtime;
	local_size = direntry->d_inode->i_size;

	if (cifs_sb->tcon->unix_ext) {
		rc = cifs_get_inode_info_unix(&direntry->d_inode, full_path,
					      direntry->d_sb, xid);
		if (rc) {
			cFYI(1, ("error on getting revalidate info %d", rc));
/*			if (rc != -ENOENT)
				rc = 0; */	/* BB should we cache info on
						   certain errors? */
		}
	} else {
		rc = cifs_get_inode_info(&direntry->d_inode, full_path, NULL,
					 direntry->d_sb, xid, NULL);
		if (rc) {
			cFYI(1, ("error on getting revalidate info %d", rc));
/*			if (rc != -ENOENT)
				rc = 0; */	/* BB should we cache info on
						   certain errors? */
		}
	}
	/* should we remap certain errors, access denied?, to zero */

	/* if not oplocked, we invalidate inode pages if mtime or file size
	   had changed on server */

	if (timespec_equal(&local_mtime, &direntry->d_inode->i_mtime) &&
	    (local_size == direntry->d_inode->i_size)) {
		cFYI(1, ("cifs_revalidate - inode unchanged"));
	} else {
		/* file may have changed on server */
		if (cifsInode->clientCanCacheRead) {
			/* no need to invalidate inode pages since we were the
			   only ones who could have modified the file and the
			   server copy is staler than ours */
		} else {
			invalidate_inode = true;
		}
	}

	/* can not grab this sem since kernel filesys locking documentation
	   indicates i_mutex may be taken by the kernel on lookup and rename
	   which could deadlock if we grab the i_mutex here as well */
/*	mutex_lock(&direntry->d_inode->i_mutex);*/
	/* need to write out dirty pages here  */
	if (direntry->d_inode->i_mapping) {
		/* do we need to lock inode until after invalidate completes
		   below? */
		wbrc = filemap_fdatawrite(direntry->d_inode->i_mapping);
		if (wbrc)
			CIFS_I(direntry->d_inode)->write_behind_rc = wbrc;
	}
	if (invalidate_inode) {
	/* shrink_dcache not necessary now that cifs dentry ops
	are exported for negative dentries */
/*		if (S_ISDIR(direntry->d_inode->i_mode))
			shrink_dcache_parent(direntry); */
		if (S_ISREG(direntry->d_inode->i_mode)) {
			if (direntry->d_inode->i_mapping) {
				wbrc = filemap_fdatawait(direntry->d_inode->i_mapping);
				if (wbrc)
					CIFS_I(direntry->d_inode)->write_behind_rc = wbrc;
			}
			/* may eventually have to do this for open files too */
			if (list_empty(&(cifsInode->openFileList))) {
				/* changed on server - flush read ahead pages */
				cFYI(1, ("Invalidating read ahead data on "
					 "closed file"));
				invalidate_remote_inode(direntry->d_inode);
			}
		}
	}
/*	mutex_unlock(&direntry->d_inode->i_mutex); */

	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int cifs_getattr(struct vfsmount *mnt, struct dentry *dentry,
	struct kstat *stat)
{
	int err = cifs_revalidate(dentry);
	if (!err) {
		generic_fillattr(dentry->d_inode, stat);
		stat->blksize = CIFS_MAX_MSGSIZE;
		stat->ino = CIFS_I(dentry->d_inode)->uniqueid;
	}
	return err;
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

static int cifs_vmtruncate(struct inode *inode, loff_t offset)
{
	loff_t oldsize;
	int err;

	spin_lock(&inode->i_lock);
	err = inode_newsize_ok(inode, offset);
	if (err) {
		spin_unlock(&inode->i_lock);
		goto out;
	}

	oldsize = inode->i_size;
	i_size_write(inode, offset);
	spin_unlock(&inode->i_lock);
	truncate_pagecache(inode, oldsize, offset);
	if (inode->i_op->truncate)
		inode->i_op->truncate(inode);
out:
	return err;
}

static int
cifs_set_file_size(struct inode *inode, struct iattr *attrs,
		   int xid, char *full_path)
{
	int rc;
	struct cifsFileInfo *open_file;
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsTconInfo *pTcon = cifs_sb->tcon;

	/*
	 * To avoid spurious oplock breaks from server, in the case of
	 * inodes that we already have open, avoid doing path based
	 * setting of file size if we can do it by handle.
	 * This keeps our caching token (oplock) and avoids timeouts
	 * when the local oplock break takes longer to flush
	 * writebehind data than the SMB timeout for the SetPathInfo
	 * request would allow
	 */
	open_file = find_writable_file(cifsInode);
	if (open_file) {
		__u16 nfid = open_file->netfid;
		__u32 npid = open_file->pid;
		rc = CIFSSMBSetFileSize(xid, pTcon, attrs->ia_size, nfid,
					npid, false);
		cifsFileInfo_put(open_file);
		cFYI(1, ("SetFSize for attrs rc = %d", rc));
		if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
			unsigned int bytes_written;
			rc = CIFSSMBWrite(xid, pTcon, nfid, 0, attrs->ia_size,
					  &bytes_written, NULL, NULL, 1);
			cFYI(1, ("Wrt seteof rc %d", rc));
		}
	} else
		rc = -EINVAL;

	if (rc != 0) {
		/* Set file size by pathname rather than by handle
		   either because no valid, writeable file handle for
		   it was found or because there was an error setting
		   it by handle */
		rc = CIFSSMBSetEOF(xid, pTcon, full_path, attrs->ia_size,
				   false, cifs_sb->local_nls,
				   cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		cFYI(1, ("SetEOF by path (setattrs) rc = %d", rc));
		if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
			__u16 netfid;
			int oplock = 0;

			rc = SMBLegacyOpen(xid, pTcon, full_path,
				FILE_OPEN, GENERIC_WRITE,
				CREATE_NOT_DIR, &netfid, &oplock, NULL,
				cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
			if (rc == 0) {
				unsigned int bytes_written;
				rc = CIFSSMBWrite(xid, pTcon, netfid, 0,
						  attrs->ia_size,
						  &bytes_written, NULL,
						  NULL, 1);
				cFYI(1, ("wrt seteof rc %d", rc));
				CIFSSMBClose(xid, pTcon, netfid);
			}
		}
	}

	if (rc == 0) {
		cifsInode->server_eof = attrs->ia_size;
		rc = cifs_vmtruncate(inode, attrs->ia_size);
		cifs_truncate_page(inode->i_mapping, inode->i_size);
	}

	return rc;
}

static int
cifs_setattr_unix(struct dentry *direntry, struct iattr *attrs)
{
	int rc;
	int xid;
	char *full_path = NULL;
	struct inode *inode = direntry->d_inode;
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsTconInfo *pTcon = cifs_sb->tcon;
	struct cifs_unix_set_info_args *args = NULL;
	struct cifsFileInfo *open_file;

	cFYI(1, ("setattr_unix on file %s attrs->ia_valid=0x%x",
		 direntry->d_name.name, attrs->ia_valid));

	xid = GetXid();

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM) == 0) {
		/* check if we have permission to change attrs */
		rc = inode_change_ok(inode, attrs);
		if (rc < 0)
			goto out;
		else
			rc = 0;
	}

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
	if (rc != 0) {
		cifsInode->write_behind_rc = rc;
		rc = 0;
	}

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
		args->uid = NO_CHANGE_64;

	if (attrs->ia_valid & ATTR_GID)
		args->gid = attrs->ia_gid;
	else
		args->gid = NO_CHANGE_64;

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
	open_file = find_writable_file(cifsInode);
	if (open_file) {
		u16 nfid = open_file->netfid;
		u32 npid = open_file->pid;
		rc = CIFSSMBUnixSetFileInfo(xid, pTcon, args, nfid, npid);
		cifsFileInfo_put(open_file);
	} else {
		rc = CIFSSMBUnixSetPathInfo(xid, pTcon, full_path, args,
				    cifs_sb->local_nls,
				    cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
	}

	if (!rc)
		rc = inode_setattr(inode, attrs);
out:
	kfree(args);
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

static int
cifs_setattr_nounix(struct dentry *direntry, struct iattr *attrs)
{
	int xid;
	struct inode *inode = direntry->d_inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsInodeInfo *cifsInode = CIFS_I(inode);
	char *full_path = NULL;
	int rc = -EACCES;
	__u32 dosattr = 0;
	__u64 mode = NO_CHANGE_64;

	xid = GetXid();

	cFYI(1, ("setattr on file %s attrs->iavalid 0x%x",
		 direntry->d_name.name, attrs->ia_valid));

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM) == 0) {
		/* check if we have permission to change attrs */
		rc = inode_change_ok(inode, attrs);
		if (rc < 0) {
			FreeXid(xid);
			return rc;
		} else
			rc = 0;
	}

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		FreeXid(xid);
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
	if (rc != 0) {
		cifsInode->write_behind_rc = rc;
		rc = 0;
	}

	if (attrs->ia_valid & ATTR_SIZE) {
		rc = cifs_set_file_size(inode, attrs, xid, full_path);
		if (rc != 0)
			goto cifs_setattr_exit;
	}

	/*
	 * Without unix extensions we can't send ownership changes to the
	 * server, so silently ignore them. This is consistent with how
	 * local DOS/Windows filesystems behave (VFAT, NTFS, etc). With
	 * CIFSACL support + proper Windows to Unix idmapping, we may be
	 * able to support this in the future.
	 */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID))
		attrs->ia_valid &= ~(ATTR_UID | ATTR_GID);

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attrs->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
		attrs->ia_valid &= ~ATTR_MODE;

	if (attrs->ia_valid & ATTR_MODE) {
		cFYI(1, ("Mode changed to 0%o", attrs->ia_mode));
		mode = attrs->ia_mode;
	}

	if (attrs->ia_valid & ATTR_MODE) {
		rc = 0;
#ifdef CONFIG_CIFS_EXPERIMENTAL
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL)
			rc = mode_to_acl(inode, full_path, mode);
		else
#endif
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
	if (!rc)
		rc = inode_setattr(inode, attrs);
cifs_setattr_exit:
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_setattr(struct dentry *direntry, struct iattr *attrs)
{
	struct inode *inode = direntry->d_inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsTconInfo *pTcon = cifs_sb->tcon;

	if (pTcon->unix_ext)
		return cifs_setattr_unix(direntry, attrs);

	return cifs_setattr_nounix(direntry, attrs);

	/* BB: add cifs_setattr_legacy for really old servers */
}

#if 0
void cifs_delete_inode(struct inode *inode)
{
	cFYI(1, ("In cifs_delete_inode, inode = 0x%p", inode));
	/* may have to add back in if and when safe distributed caching of
	   directories added e.g. via FindNotify */
}
#endif
