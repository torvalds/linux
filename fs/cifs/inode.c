/*
 *   fs/cifs/inode.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2007
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

int cifs_get_inode_info_unix(struct inode **pinode,
	const unsigned char *search_path, struct super_block *sb, int xid)
{
	int rc = 0;
	FILE_UNIX_BASIC_INFO findData;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *tmp_path;

	pTcon = cifs_sb->tcon;
	cFYI(1, ("Getting info on %s", search_path));
	/* could have done a find first instead but this returns more info */
	rc = CIFSSMBUnixQPathInfo(xid, pTcon, search_path, &findData,
				  cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
/*	dump_mem("\nUnixQPathInfo return data", &findData,
		 sizeof(findData)); */
	if (rc) {
		if (rc == -EREMOTE) {
			tmp_path =
			    kmalloc(strnlen(pTcon->treeName,
					    MAX_TREE_SIZE + 1) +
				    strnlen(search_path, MAX_PATHCONF) + 1,
				    GFP_KERNEL);
			if (tmp_path == NULL) {
				return -ENOMEM;
			}
			/* have to skip first of the double backslash of
			   UNC name */
			strncpy(tmp_path, pTcon->treeName, MAX_TREE_SIZE);
			strncat(tmp_path, search_path, MAX_PATHCONF);
			rc = connect_to_dfs_path(xid, pTcon->ses,
						 /* treename + */ tmp_path,
						 cifs_sb->local_nls,
						 cifs_sb->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
			kfree(tmp_path);

			/* BB fix up inode etc. */
		} else if (rc) {
			return rc;
		}
	} else {
		struct cifsInodeInfo *cifsInfo;
		__u32 type = le32_to_cpu(findData.Type);
		__u64 num_of_bytes = le64_to_cpu(findData.NumOfBytes);
		__u64 end_of_file = le64_to_cpu(findData.EndOfFile);

		/* get new inode */
		if (*pinode == NULL) {
			*pinode = new_inode(sb);
			if (*pinode == NULL)
				return -ENOMEM;
			/* Is an i_ino of zero legal? */
			/* Are there sanity checks we can use to ensure that
			   the server is really filling in that field? */
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
				(*pinode)->i_ino =
					(unsigned long)findData.UniqueId;
			} /* note ino incremented to unique num in new_inode */
			if (sb->s_flags & MS_NOATIME)
				(*pinode)->i_flags |= S_NOATIME | S_NOCMTIME;

			insert_inode_hash(*pinode);
		}

		inode = *pinode;
		cifsInfo = CIFS_I(inode);

		cFYI(1, ("Old time %ld", cifsInfo->time));
		cifsInfo->time = jiffies;
		cFYI(1, ("New time %ld", cifsInfo->time));
		/* this is ok to set on every inode revalidate */
		atomic_set(&cifsInfo->inUse, 1);

		inode->i_atime =
		    cifs_NTtimeToUnix(le64_to_cpu(findData.LastAccessTime));
		inode->i_mtime =
		    cifs_NTtimeToUnix(le64_to_cpu
				(findData.LastModificationTime));
		inode->i_ctime =
		    cifs_NTtimeToUnix(le64_to_cpu(findData.LastStatusChange));
		inode->i_mode = le64_to_cpu(findData.Permissions);
		/* since we set the inode type below we need to mask off
		   to avoid strange results if bits set above */
		inode->i_mode &= ~S_IFMT;
		if (type == UNIX_FILE) {
			inode->i_mode |= S_IFREG;
		} else if (type == UNIX_SYMLINK) {
			inode->i_mode |= S_IFLNK;
		} else if (type == UNIX_DIR) {
			inode->i_mode |= S_IFDIR;
		} else if (type == UNIX_CHARDEV) {
			inode->i_mode |= S_IFCHR;
			inode->i_rdev = MKDEV(le64_to_cpu(findData.DevMajor),
				le64_to_cpu(findData.DevMinor) & MINORMASK);
		} else if (type == UNIX_BLOCKDEV) {
			inode->i_mode |= S_IFBLK;
			inode->i_rdev = MKDEV(le64_to_cpu(findData.DevMajor),
				le64_to_cpu(findData.DevMinor) & MINORMASK);
		} else if (type == UNIX_FIFO) {
			inode->i_mode |= S_IFIFO;
		} else if (type == UNIX_SOCKET) {
			inode->i_mode |= S_IFSOCK;
		} else {
			/* safest to call it a file if we do not know */
			inode->i_mode |= S_IFREG;
			cFYI(1, ("unknown type %d", type));
		}

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_UID)
			inode->i_uid = cifs_sb->mnt_uid;
		else
			inode->i_uid = le64_to_cpu(findData.Uid);

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_GID)
			inode->i_gid = cifs_sb->mnt_gid;
		else
			inode->i_gid = le64_to_cpu(findData.Gid);

		inode->i_nlink = le64_to_cpu(findData.Nlinks);

		spin_lock(&inode->i_lock);
		if (is_size_safe_to_change(cifsInfo, end_of_file)) {
		/* can not safely change the file size here if the
		   client is writing to it due to potential races */
			i_size_write(inode, end_of_file);

		/* blksize needs to be multiple of two. So safer to default to
		blksize and blkbits set in superblock so 2**blkbits and blksize
		will match rather than setting to:
		(pTcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFE00;*/

		/* This seems incredibly stupid but it turns out that i_blocks
		   is not related to (i_size / i_blksize), instead 512 byte size
		   is required for calculating num blocks */

		/* 512 bytes (2**9) is the fake blocksize that must be used */
		/* for this calculation */
			inode->i_blocks = (512 - 1 + num_of_bytes) >> 9;
		}
		spin_unlock(&inode->i_lock);

		if (num_of_bytes < end_of_file)
			cFYI(1, ("allocation size less than end of file"));
		cFYI(1, ("Size %ld and blocks %llu",
			(unsigned long) inode->i_size,
			(unsigned long long)inode->i_blocks));
		if (S_ISREG(inode->i_mode)) {
			cFYI(1, ("File inode"));
			inode->i_op = &cifs_file_inode_ops;
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
				if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
					inode->i_fop =
						&cifs_file_direct_nobrl_ops;
				else
					inode->i_fop = &cifs_file_direct_ops;
			} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				inode->i_fop = &cifs_file_nobrl_ops;
			else /* not direct, send byte range locks */
				inode->i_fop = &cifs_file_ops;

			/* check if server can support readpages */
			if (pTcon->ses->server->maxBuf <
			    PAGE_CACHE_SIZE + MAX_CIFS_HDR_SIZE)
				inode->i_data.a_ops = &cifs_addr_ops_smallbuf;
			else
				inode->i_data.a_ops = &cifs_addr_ops;
		} else if (S_ISDIR(inode->i_mode)) {
			cFYI(1, ("Directory inode"));
			inode->i_op = &cifs_dir_inode_ops;
			inode->i_fop = &cifs_dir_ops;
		} else if (S_ISLNK(inode->i_mode)) {
			cFYI(1, ("Symbolic Link inode"));
			inode->i_op = &cifs_symlink_inode_ops;
		/* tmp_inode->i_fop = */ /* do not need to set to anything */
		} else {
			cFYI(1, ("Init special inode"));
			init_special_inode(inode, inode->i_mode,
					   inode->i_rdev);
		}
	}
	return rc;
}

static int decode_sfu_inode(struct inode *inode, __u64 size,
			    const unsigned char *path,
			    struct cifs_sb_info *cifs_sb, int xid)
{
	int rc;
	int oplock = FALSE;
	__u16 netfid;
	struct cifsTconInfo *pTcon = cifs_sb->tcon;
	char buf[24];
	unsigned int bytes_read;
	char *pbuf;

	pbuf = buf;

	if (size == 0) {
		inode->i_mode |= S_IFIFO;
		return 0;
	} else if (size < 8) {
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
		rc = CIFSSMBRead(xid, pTcon,
				 netfid,
				 24 /* length */, 0 /* offset */,
				 &bytes_read, &pbuf, &buf_type);
		if ((rc == 0) && (bytes_read >= 8)) {
			if (memcmp("IntxBLK", pbuf, 8) == 0) {
				cFYI(1, ("Block device"));
				inode->i_mode |= S_IFBLK;
				if (bytes_read == 24) {
					/* we have enough to decode dev num */
					__u64 mjr; /* major */
					__u64 mnr; /* minor */
					mjr = le64_to_cpu(*(__le64 *)(pbuf+8));
					mnr = le64_to_cpu(*(__le64 *)(pbuf+16));
					inode->i_rdev = MKDEV(mjr, mnr);
				}
			} else if (memcmp("IntxCHR", pbuf, 8) == 0) {
				cFYI(1, ("Char device"));
				inode->i_mode |= S_IFCHR;
				if (bytes_read == 24) {
					/* we have enough to decode dev num */
					__u64 mjr; /* major */
					__u64 mnr; /* minor */
					mjr = le64_to_cpu(*(__le64 *)(pbuf+8));
					mnr = le64_to_cpu(*(__le64 *)(pbuf+16));
					inode->i_rdev = MKDEV(mjr, mnr);
				}
			} else if (memcmp("IntxLNK", pbuf, 7) == 0) {
				cFYI(1, ("Symlink"));
				inode->i_mode |= S_IFLNK;
			} else {
				inode->i_mode |= S_IFREG; /* file? */
				rc = -EOPNOTSUPP;
			}
		} else {
			inode->i_mode |= S_IFREG; /* then it is a file */
			rc = -EOPNOTSUPP; /* or some unknown SFU type */
		}
		CIFSSMBClose(xid, pTcon, netfid);
	}
	return rc;
}

#define SFBITS_MASK (S_ISVTX | S_ISGID | S_ISUID)  /* SETFILEBITS valid bits */

static int get_sfu_mode(struct inode *inode,
			const unsigned char *path,
			struct cifs_sb_info *cifs_sb, int xid)
{
#ifdef CONFIG_CIFS_XATTR
	ssize_t rc;
	char ea_value[4];
	__u32 mode;

	rc = CIFSSMBQueryEA(xid, cifs_sb->tcon, path, "SETFILEBITS",
			ea_value, 4 /* size of buf */, cifs_sb->local_nls,
		cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc < 0)
		return (int)rc;
	else if (rc > 3) {
		mode = le32_to_cpu(*((__le32 *)ea_value));
		inode->i_mode &= ~SFBITS_MASK;
		cFYI(1, ("special bits 0%o org mode 0%o", mode, inode->i_mode));
		inode->i_mode = (mode &  SFBITS_MASK) | inode->i_mode;
		cFYI(1, ("special mode bits 0%o", mode));
		return 0;
	} else {
		return 0;
	}
#else
	return -EOPNOTSUPP;
#endif
}

int cifs_get_inode_info(struct inode **pinode,
	const unsigned char *search_path, FILE_ALL_INFO *pfindData,
	struct super_block *sb, int xid)
{
	int rc = 0;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	char *tmp_path;
	char *buf = NULL;
	int adjustTZ = FALSE;

	pTcon = cifs_sb->tcon;
	cFYI(1, ("Getting info on %s", search_path));

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
		rc = CIFSSMBQPathInfo(xid, pTcon, search_path, pfindData,
			      0 /* not legacy */,
			      cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
		/* BB optimize code so we do not make the above call
		when server claims no NT SMB support and the above call
		failed at least once - set flag in tcon or mount */
		if ((rc == -EOPNOTSUPP) || (rc == -EINVAL)) {
			rc = SMBQueryInformation(xid, pTcon, search_path,
					pfindData, cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
					  CIFS_MOUNT_MAP_SPECIAL_CHR);
			adjustTZ = TRUE;
		}
	}
	/* dump_mem("\nQPathInfo return data",&findData, sizeof(findData)); */
	if (rc) {
		if (rc == -EREMOTE) {
			tmp_path =
			    kmalloc(strnlen
				    (pTcon->treeName,
				     MAX_TREE_SIZE + 1) +
				    strnlen(search_path, MAX_PATHCONF) + 1,
				    GFP_KERNEL);
			if (tmp_path == NULL) {
				kfree(buf);
				return -ENOMEM;
			}

			strncpy(tmp_path, pTcon->treeName, MAX_TREE_SIZE);
			strncat(tmp_path, search_path, MAX_PATHCONF);
			rc = connect_to_dfs_path(xid, pTcon->ses,
						 /* treename + */ tmp_path,
						 cifs_sb->local_nls,
						 cifs_sb->mnt_cifs_flags &
						   CIFS_MOUNT_MAP_SPECIAL_CHR);
			kfree(tmp_path);
			/* BB fix up inode etc. */
		} else if (rc) {
			kfree(buf);
			return rc;
		}
	} else {
		struct cifsInodeInfo *cifsInfo;
		__u32 attr = le32_to_cpu(pfindData->Attributes);

		/* get new inode */
		if (*pinode == NULL) {
			*pinode = new_inode(sb);
			if (*pinode == NULL) {
				kfree(buf);
				return -ENOMEM;
			}
			/* Is an i_ino of zero legal? Can we use that to check
			   if the server supports returning inode numbers?  Are
			   there other sanity checks we can use to ensure that
			   the server is really filling in that field? */

			/* We can not use the IndexNumber field by default from
			   Windows or Samba (in ALL_INFO buf) but we can request
			   it explicitly.  It may not be unique presumably if
			   the server has multiple devices mounted under one
			   share */

			/* There may be higher info levels that work but are
			   there Windows server or network appliances for which
			   IndexNumber field is not guaranteed unique? */

			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
				int rc1 = 0;
				__u64 inode_num;

				rc1 = CIFSGetSrvInodeNumber(xid, pTcon,
					search_path, &inode_num,
					cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
				if (rc1) {
					cFYI(1, ("GetSrvInodeNum rc %d", rc1));
					/* BB EOPNOSUPP disable SERVER_INUM? */
				} else /* do we need cast or hash to ino? */
					(*pinode)->i_ino = inode_num;
			} /* else ino incremented to unique num in new_inode*/
			if (sb->s_flags & MS_NOATIME)
				(*pinode)->i_flags |= S_NOATIME | S_NOCMTIME;
			insert_inode_hash(*pinode);
		}
		inode = *pinode;
		cifsInfo = CIFS_I(inode);
		cifsInfo->cifsAttrs = attr;
		cFYI(1, ("Old time %ld", cifsInfo->time));
		cifsInfo->time = jiffies;
		cFYI(1, ("New time %ld", cifsInfo->time));

		/* blksize needs to be multiple of two. So safer to default to
		blksize and blkbits set in superblock so 2**blkbits and blksize
		will match rather than setting to:
		(pTcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE) & 0xFFFFFE00;*/

		/* Linux can not store file creation time so ignore it */
		if (pfindData->LastAccessTime)
			inode->i_atime = cifs_NTtimeToUnix
				(le64_to_cpu(pfindData->LastAccessTime));
		else /* do not need to use current_fs_time - time not stored */
			inode->i_atime = CURRENT_TIME;
		inode->i_mtime =
		    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastWriteTime));
		inode->i_ctime =
		    cifs_NTtimeToUnix(le64_to_cpu(pfindData->ChangeTime));
		cFYI(0, ("Attributes came in as 0x%x", attr));
		if (adjustTZ && (pTcon->ses) && (pTcon->ses->server)) {
			inode->i_ctime.tv_sec += pTcon->ses->server->timeAdj;
			inode->i_mtime.tv_sec += pTcon->ses->server->timeAdj;
		}

		/* set default mode. will override for dirs below */
		if (atomic_read(&cifsInfo->inUse) == 0)
			/* new inode, can safely set these fields */
			inode->i_mode = cifs_sb->mnt_file_mode;
		else /* since we set the inode type below we need to mask off
		     to avoid strange results if type changes and both
		     get orred in */
			inode->i_mode &= ~S_IFMT;
/*		if (attr & ATTR_REPARSE)  */
		/* We no longer handle these as symlinks because we could not
		   follow them due to the absolute path with drive letter */
		if (attr & ATTR_DIRECTORY) {
		/* override default perms since we do not do byte range locking
		   on dirs */
			inode->i_mode = cifs_sb->mnt_dir_mode;
			inode->i_mode |= S_IFDIR;
		} else if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) &&
			   (cifsInfo->cifsAttrs & ATTR_SYSTEM) &&
			   /* No need to le64 convert size of zero */
			   (pfindData->EndOfFile == 0)) {
			inode->i_mode = cifs_sb->mnt_file_mode;
			inode->i_mode |= S_IFIFO;
/* BB Finish for SFU style symlinks and devices */
		} else if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) &&
			   (cifsInfo->cifsAttrs & ATTR_SYSTEM)) {
			if (decode_sfu_inode(inode,
					 le64_to_cpu(pfindData->EndOfFile),
					 search_path,
					 cifs_sb, xid)) {
				cFYI(1, ("Unrecognized sfu inode type"));
			}
			cFYI(1, ("sfu mode 0%o", inode->i_mode));
		} else {
			inode->i_mode |= S_IFREG;
			/* treat the dos attribute of read-only as read-only
			   mode e.g. 555 */
			if (cifsInfo->cifsAttrs & ATTR_READONLY)
				inode->i_mode &= ~(S_IWUGO);
			else if ((inode->i_mode & S_IWUGO) == 0)
				/* the ATTR_READONLY flag may have been	*/
				/* changed on server -- set any w bits	*/
				/* allowed by mnt_file_mode		*/
				inode->i_mode |= (S_IWUGO &
						  cifs_sb->mnt_file_mode);
		/* BB add code here -
		   validate if device or weird share or device type? */
		}

		spin_lock(&inode->i_lock);
		if (is_size_safe_to_change(cifsInfo, le64_to_cpu(pfindData->EndOfFile))) {
			/* can not safely shrink the file size here if the
			   client is writing to it due to potential races */
			i_size_write(inode, le64_to_cpu(pfindData->EndOfFile));

			/* 512 bytes (2**9) is the fake blocksize that must be
			   used for this calculation */
			inode->i_blocks = (512 - 1 + le64_to_cpu(
					   pfindData->AllocationSize)) >> 9;
		}
		spin_unlock(&inode->i_lock);

		inode->i_nlink = le32_to_cpu(pfindData->NumberOfLinks);

		/* BB fill in uid and gid here? with help from winbind?
		   or retrieve from NTFS stream extended attribute */
#ifdef CONFIG_CIFS_EXPERIMENTAL
		/* fill in 0777 bits from ACL */
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) {
			cFYI(1, ("Getting mode bits from ACL"));
			acl_to_uid_mode(inode, search_path);
		}
#endif
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) {
			/* fill in remaining high mode bits e.g. SUID, VTX */
			get_sfu_mode(inode, search_path, cifs_sb, xid);
		} else if (atomic_read(&cifsInfo->inUse) == 0) {
			inode->i_uid = cifs_sb->mnt_uid;
			inode->i_gid = cifs_sb->mnt_gid;
			/* set so we do not keep refreshing these fields with
			   bad data after user has changed them in memory */
			atomic_set(&cifsInfo->inUse, 1);
		}

		if (S_ISREG(inode->i_mode)) {
			cFYI(1, ("File inode"));
			inode->i_op = &cifs_file_inode_ops;
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
				if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
					inode->i_fop =
						&cifs_file_direct_nobrl_ops;
				else
					inode->i_fop = &cifs_file_direct_ops;
			} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				inode->i_fop = &cifs_file_nobrl_ops;
			else /* not direct, send byte range locks */
				inode->i_fop = &cifs_file_ops;

			if (pTcon->ses->server->maxBuf <
			     PAGE_CACHE_SIZE + MAX_CIFS_HDR_SIZE)
				inode->i_data.a_ops = &cifs_addr_ops_smallbuf;
			else
				inode->i_data.a_ops = &cifs_addr_ops;
		} else if (S_ISDIR(inode->i_mode)) {
			cFYI(1, ("Directory inode"));
			inode->i_op = &cifs_dir_inode_ops;
			inode->i_fop = &cifs_dir_ops;
		} else if (S_ISLNK(inode->i_mode)) {
			cFYI(1, ("Symbolic Link inode"));
			inode->i_op = &cifs_symlink_inode_ops;
		} else {
			init_special_inode(inode, inode->i_mode,
					   inode->i_rdev);
		}
	}
	kfree(buf);
	return rc;
}

static const struct inode_operations cifs_ipc_inode_ops = {
	.lookup = cifs_lookup,
};

/* gets root inode */
void cifs_read_inode(struct inode *inode)
{
	int xid, rc;
	struct cifs_sb_info *cifs_sb;

	cifs_sb = CIFS_SB(inode->i_sb);
	xid = GetXid();

	if (cifs_sb->tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, "", inode->i_sb, xid);
	else
		rc = cifs_get_inode_info(&inode, "", NULL, inode->i_sb, xid);
	if (rc && cifs_sb->tcon->ipc) {
		cFYI(1, ("ipc connection - fake read inode"));
		inode->i_mode |= S_IFDIR;
		inode->i_nlink = 2;
		inode->i_op = &cifs_ipc_inode_ops;
		inode->i_fop = &simple_dir_operations;
		inode->i_uid = cifs_sb->mnt_uid;
		inode->i_gid = cifs_sb->mnt_gid;
	}

	/* can not call macro FreeXid here since in a void func */
	_FreeXid(xid);
}

int cifs_unlink(struct inode *inode, struct dentry *direntry)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct cifsInodeInfo *cifsInode;
	FILE_BASIC_INFO *pinfo_buf;

	cFYI(1, ("cifs_unlink, inode = 0x%p", inode));

	xid = GetXid();

	if (inode)
		cifs_sb = CIFS_SB(inode->i_sb);
	else
		cifs_sb = CIFS_SB(direntry->d_sb);
	pTcon = cifs_sb->tcon;

	/* Unlink can be called from rename so we can not grab the sem here
	   since we deadlock otherwise */
/*	mutex_lock(&direntry->d_sb->s_vfs_rename_mutex);*/
	full_path = build_path_from_dentry(direntry);
/*	mutex_unlock(&direntry->d_sb->s_vfs_rename_mutex);*/
	if (full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}

	if ((pTcon->ses->capabilities & CAP_UNIX) &&
		(CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(pTcon->fsUnixInfo.Capability))) {
		rc = CIFSPOSIXDelFile(xid, pTcon, full_path,
			SMB_POSIX_UNLINK_FILE_TARGET, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
		cFYI(1, ("posix del rc %d", rc));
		if ((rc == 0) || (rc == -ENOENT))
			goto psx_del_no_retry;
	}

	rc = CIFSSMBDelFile(xid, pTcon, full_path, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
psx_del_no_retry:
	if (!rc) {
		if (direntry->d_inode)
			drop_nlink(direntry->d_inode);
	} else if (rc == -ENOENT) {
		d_drop(direntry);
	} else if (rc == -ETXTBSY) {
		int oplock = FALSE;
		__u16 netfid;

		rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN, DELETE,
				 CREATE_NOT_DIR | CREATE_DELETE_ON_CLOSE,
				 &netfid, &oplock, NULL, cifs_sb->local_nls,
				 cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc == 0) {
			CIFSSMBRenameOpenFile(xid, pTcon, netfid, NULL,
					      cifs_sb->local_nls,
					      cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			CIFSSMBClose(xid, pTcon, netfid);
			if (direntry->d_inode)
				drop_nlink(direntry->d_inode);
		}
	} else if (rc == -EACCES) {
		/* try only if r/o attribute set in local lookup data? */
		pinfo_buf = kzalloc(sizeof(FILE_BASIC_INFO), GFP_KERNEL);
		if (pinfo_buf) {
			/* ATTRS set to normal clears r/o bit */
			pinfo_buf->Attributes = cpu_to_le32(ATTR_NORMAL);
			if (!(pTcon->ses->flags & CIFS_SES_NT4))
				rc = CIFSSMBSetTimes(xid, pTcon, full_path,
						     pinfo_buf,
						     cifs_sb->local_nls,
						     cifs_sb->mnt_cifs_flags &
							CIFS_MOUNT_MAP_SPECIAL_CHR);
			else
				rc = -EOPNOTSUPP;

			if (rc == -EOPNOTSUPP) {
				int oplock = FALSE;
				__u16 netfid;
			/*	rc = CIFSSMBSetAttrLegacy(xid, pTcon,
							  full_path,
							  (__u16)ATTR_NORMAL,
							  cifs_sb->local_nls);
			   For some strange reason it seems that NT4 eats the
			   old setattr call without actually setting the
			   attributes so on to the third attempted workaround
			   */

			/* BB could scan to see if we already have it open
			   and pass in pid of opener to function */
				rc = CIFSSMBOpen(xid, pTcon, full_path,
						 FILE_OPEN, SYNCHRONIZE |
						 FILE_WRITE_ATTRIBUTES, 0,
						 &netfid, &oplock, NULL,
						 cifs_sb->local_nls,
						 cifs_sb->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
				if (rc == 0) {
					rc = CIFSSMBSetFileTimes(xid, pTcon,
								 pinfo_buf,
								 netfid);
					CIFSSMBClose(xid, pTcon, netfid);
				}
			}
			kfree(pinfo_buf);
		}
		if (rc == 0) {
			rc = CIFSSMBDelFile(xid, pTcon, full_path,
					    cifs_sb->local_nls,
					    cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			if (!rc) {
				if (direntry->d_inode)
					drop_nlink(direntry->d_inode);
			} else if (rc == -ETXTBSY) {
				int oplock = FALSE;
				__u16 netfid;

				rc = CIFSSMBOpen(xid, pTcon, full_path,
						 FILE_OPEN, DELETE,
						 CREATE_NOT_DIR |
						 CREATE_DELETE_ON_CLOSE,
						 &netfid, &oplock, NULL,
						 cifs_sb->local_nls,
						 cifs_sb->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
				if (rc == 0) {
					CIFSSMBRenameOpenFile(xid, pTcon,
						netfid, NULL,
						cifs_sb->local_nls,
						cifs_sb->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
					CIFSSMBClose(xid, pTcon, netfid);
					if (direntry->d_inode)
						drop_nlink(direntry->d_inode);
				}
			/* BB if rc = -ETXTBUSY goto the rename logic BB */
			}
		}
	}
	if (direntry->d_inode) {
		cifsInode = CIFS_I(direntry->d_inode);
		cifsInode->time = 0;	/* will force revalidate to get info
					   when needed */
		direntry->d_inode->i_ctime = current_fs_time(inode->i_sb);
	}
	if (inode) {
		inode->i_ctime = inode->i_mtime = current_fs_time(inode->i_sb);
		cifsInode = CIFS_I(inode);
		cifsInode->time = 0;	/* force revalidate of dir as well */
	}

	kfree(full_path);
	FreeXid(xid);
	return rc;
}

static void posix_fill_in_inode(struct inode *tmp_inode,
	FILE_UNIX_BASIC_INFO *pData, int *pobject_type, int isNewInode)
{
	loff_t local_size;
	struct timespec local_mtime;

	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(tmp_inode->i_sb);

	__u32 type = le32_to_cpu(pData->Type);
	__u64 num_of_bytes = le64_to_cpu(pData->NumOfBytes);
	__u64 end_of_file = le64_to_cpu(pData->EndOfFile);
	cifsInfo->time = jiffies;
	atomic_inc(&cifsInfo->inUse);

	/* save mtime and size */
	local_mtime = tmp_inode->i_mtime;
	local_size  = tmp_inode->i_size;

	tmp_inode->i_atime =
	    cifs_NTtimeToUnix(le64_to_cpu(pData->LastAccessTime));
	tmp_inode->i_mtime =
	    cifs_NTtimeToUnix(le64_to_cpu(pData->LastModificationTime));
	tmp_inode->i_ctime =
	    cifs_NTtimeToUnix(le64_to_cpu(pData->LastStatusChange));

	tmp_inode->i_mode = le64_to_cpu(pData->Permissions);
	/* since we set the inode type below we need to mask off type
	   to avoid strange results if bits above were corrupt */
	tmp_inode->i_mode &= ~S_IFMT;
	if (type == UNIX_FILE) {
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
	} else if (type == UNIX_SYMLINK) {
		*pobject_type = DT_LNK;
		tmp_inode->i_mode |= S_IFLNK;
	} else if (type == UNIX_DIR) {
		*pobject_type = DT_DIR;
		tmp_inode->i_mode |= S_IFDIR;
	} else if (type == UNIX_CHARDEV) {
		*pobject_type = DT_CHR;
		tmp_inode->i_mode |= S_IFCHR;
		tmp_inode->i_rdev = MKDEV(le64_to_cpu(pData->DevMajor),
				le64_to_cpu(pData->DevMinor) & MINORMASK);
	} else if (type == UNIX_BLOCKDEV) {
		*pobject_type = DT_BLK;
		tmp_inode->i_mode |= S_IFBLK;
		tmp_inode->i_rdev = MKDEV(le64_to_cpu(pData->DevMajor),
				le64_to_cpu(pData->DevMinor) & MINORMASK);
	} else if (type == UNIX_FIFO) {
		*pobject_type = DT_FIFO;
		tmp_inode->i_mode |= S_IFIFO;
	} else if (type == UNIX_SOCKET) {
		*pobject_type = DT_SOCK;
		tmp_inode->i_mode |= S_IFSOCK;
	} else {
		/* safest to just call it a file */
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
		cFYI(1, ("unknown inode type %d", type));
	}

#ifdef CONFIG_CIFS_DEBUG2
	cFYI(1, ("object type: %d", type));
#endif
	tmp_inode->i_uid = le64_to_cpu(pData->Uid);
	tmp_inode->i_gid = le64_to_cpu(pData->Gid);
	tmp_inode->i_nlink = le64_to_cpu(pData->Nlinks);

	spin_lock(&tmp_inode->i_lock);
	if (is_size_safe_to_change(cifsInfo, end_of_file)) {
		/* can not safely change the file size here if the
		client is writing to it due to potential races */
		i_size_write(tmp_inode, end_of_file);

	/* 512 bytes (2**9) is the fake blocksize that must be used */
	/* for this calculation, not the real blocksize */
		tmp_inode->i_blocks = (512 - 1 + num_of_bytes) >> 9;
	}
	spin_unlock(&tmp_inode->i_lock);

	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, ("File inode"));
		tmp_inode->i_op = &cifs_file_inode_ops;

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				tmp_inode->i_fop = &cifs_file_direct_nobrl_ops;
			else
				tmp_inode->i_fop = &cifs_file_direct_ops;

		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			tmp_inode->i_fop = &cifs_file_nobrl_ops;
		else
			tmp_inode->i_fop = &cifs_file_ops;

		if ((cifs_sb->tcon) && (cifs_sb->tcon->ses) &&
		   (cifs_sb->tcon->ses->server->maxBuf <
			PAGE_CACHE_SIZE + MAX_CIFS_HDR_SIZE))
			tmp_inode->i_data.a_ops = &cifs_addr_ops_smallbuf;
		else
			tmp_inode->i_data.a_ops = &cifs_addr_ops;

		if (isNewInode)
			return; /* No sense invalidating pages for new inode
				   since we we have not started caching
				   readahead file data yet */

		if (timespec_equal(&tmp_inode->i_mtime, &local_mtime) &&
			(local_size == tmp_inode->i_size)) {
			cFYI(1, ("inode exists but unchanged"));
		} else {
			/* file may have changed on server */
			cFYI(1, ("invalidate inode, readdir detected change"));
			invalidate_remote_inode(tmp_inode);
		}
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, ("Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, ("Symbolic Link inode"));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
/* tmp_inode->i_fop = *//* do not need to set to anything */
	} else {
		cFYI(1, ("Special inode"));
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   tmp_inode->i_rdev);
	}
}

int cifs_mkdir(struct inode *inode, struct dentry *direntry, int mode)
{
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;

	cFYI(1, ("In cifs_mkdir, mode = 0x%x inode = 0x%p", mode, inode));

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}

	if ((pTcon->ses->capabilities & CAP_UNIX) &&
		(CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(pTcon->fsUnixInfo.Capability))) {
		u32 oplock = 0;
		FILE_UNIX_BASIC_INFO * pInfo =
			kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
		if (pInfo == NULL) {
			rc = -ENOMEM;
			goto mkdir_out;
		}

		mode &= ~current->fs->umask;
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
			int obj_type;
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

			newinode = new_inode(inode->i_sb);
			if (newinode == NULL) {
				kfree(pInfo);
				goto mkdir_get_info;
			}
			/* Is an i_ino of zero legal? */
			/* Are there sanity checks we can use to ensure that
			   the server is really filling in that field? */
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
				newinode->i_ino =
					(unsigned long)pInfo->UniqueId;
			} /* note ino incremented to unique num in new_inode */
			if (inode->i_sb->s_flags & MS_NOATIME)
				newinode->i_flags |= S_NOATIME | S_NOCMTIME;
			newinode->i_nlink = 2;

			insert_inode_hash(newinode);
			d_instantiate(direntry, newinode);

			/* we already checked in POSIXCreate whether
			   frame was long enough */
			posix_fill_in_inode(direntry->d_inode,
					pInfo, &obj_type, 1 /* NewInode */);
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
						 inode->i_sb, xid);

		if (pTcon->nocase)
			direntry->d_op = &cifs_ci_dentry_ops;
		else
			direntry->d_op = &cifs_dentry_ops;
		d_instantiate(direntry, newinode);
		 /* setting nlink not necessary except in cases where we
		  * failed to get it from the server or was set bogus */
		if ((direntry->d_inode) && (direntry->d_inode->i_nlink < 2))
				direntry->d_inode->i_nlink = 2;
		if (pTcon->unix_ext) {
			mode &= ~current->fs->umask;
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
				CIFSSMBUnixSetPerms(xid, pTcon, full_path,
						    mode,
						    (__u64)current->fsuid,
						    (__u64)current->fsgid,
						    0 /* dev_t */,
						    cifs_sb->local_nls,
						    cifs_sb->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
			} else {
				CIFSSMBUnixSetPerms(xid, pTcon, full_path,
						    mode, (__u64)-1,
						    (__u64)-1, 0 /* dev_t */,
						    cifs_sb->local_nls,
						    cifs_sb->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
			}
		} else {
			/* BB to be implemented via Windows secrty descriptors
			   eg CIFSSMBWinSetPerms(xid, pTcon, full_path, mode,
						 -1, -1, local_nls); */
			if (direntry->d_inode) {
				direntry->d_inode->i_mode = mode;
				direntry->d_inode->i_mode |= S_IFDIR;
				if (cifs_sb->mnt_cifs_flags &
				     CIFS_MOUNT_SET_UID) {
					direntry->d_inode->i_uid =
						current->fsuid;
					direntry->d_inode->i_gid =
						current->fsgid;
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
		FreeXid(xid);
		return -ENOMEM;
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
	direntry->d_inode->i_ctime = inode->i_ctime = inode->i_mtime =
		current_fs_time(inode->i_sb);

	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int cifs_rename(struct inode *source_inode, struct dentry *source_direntry,
	struct inode *target_inode, struct dentry *target_direntry)
{
	char *fromName;
	char *toName;
	struct cifs_sb_info *cifs_sb_source;
	struct cifs_sb_info *cifs_sb_target;
	struct cifsTconInfo *pTcon;
	int xid;
	int rc = 0;

	xid = GetXid();

	cifs_sb_target = CIFS_SB(target_inode->i_sb);
	cifs_sb_source = CIFS_SB(source_inode->i_sb);
	pTcon = cifs_sb_source->tcon;

	if (pTcon != cifs_sb_target->tcon) {
		FreeXid(xid);
		return -EXDEV;	/* BB actually could be allowed if same server,
				   but different share.
				   Might eventually add support for this */
	}

	/* we already  have the rename sem so we do not need to grab it again
	   here to protect the path integrity */
	fromName = build_path_from_dentry(source_direntry);
	toName = build_path_from_dentry(target_direntry);
	if ((fromName == NULL) || (toName == NULL)) {
		rc = -ENOMEM;
		goto cifs_rename_exit;
	}

	rc = CIFSSMBRename(xid, pTcon, fromName, toName,
			   cifs_sb_source->local_nls,
			   cifs_sb_source->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc == -EEXIST) {
		/* check if they are the same file because rename of hardlinked
		   files is a noop */
		FILE_UNIX_BASIC_INFO *info_buf_source;
		FILE_UNIX_BASIC_INFO *info_buf_target;

		info_buf_source =
			kmalloc(2 * sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
		if (info_buf_source != NULL) {
			info_buf_target = info_buf_source + 1;
			if (pTcon->unix_ext)
				rc = CIFSSMBUnixQPathInfo(xid, pTcon, fromName,
					info_buf_source,
					cifs_sb_source->local_nls,
					cifs_sb_source->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			/* else rc is still EEXIST so will fall through to
			   unlink the target and retry rename */
			if (rc == 0) {
				rc = CIFSSMBUnixQPathInfo(xid, pTcon, toName,
						info_buf_target,
						cifs_sb_target->local_nls,
						/* remap based on source sb */
						cifs_sb_source->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
			}
			if ((rc == 0) &&
			    (info_buf_source->UniqueId ==
			     info_buf_target->UniqueId)) {
			/* do not rename since the files are hardlinked which
			   is a noop */
			} else {
			/* we either can not tell the files are hardlinked
			   (as with Windows servers) or files are not
			   hardlinked so delete the target manually before
			   renaming to follow POSIX rather than Windows
			   semantics */
				cifs_unlink(target_inode, target_direntry);
				rc = CIFSSMBRename(xid, pTcon, fromName,
						   toName,
						   cifs_sb_source->local_nls,
						   cifs_sb_source->mnt_cifs_flags
						   & CIFS_MOUNT_MAP_SPECIAL_CHR);
			}
			kfree(info_buf_source);
		} /* if we can not get memory just leave rc as EEXIST */
	}

	if (rc) {
		cFYI(1, ("rename rc %d", rc));
	}

	if ((rc == -EIO) || (rc == -EEXIST)) {
		int oplock = FALSE;
		__u16 netfid;

		/* BB FIXME Is Generic Read correct for rename? */
		/* if renaming directory - we should not say CREATE_NOT_DIR,
		   need to test renaming open directory, also GENERIC_READ
		   might not right be right access to request */
		rc = CIFSSMBOpen(xid, pTcon, fromName, FILE_OPEN, GENERIC_READ,
				 CREATE_NOT_DIR, &netfid, &oplock, NULL,
				 cifs_sb_source->local_nls,
				 cifs_sb_source->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc == 0) {
			rc = CIFSSMBRenameOpenFile(xid, pTcon, netfid, toName,
					      cifs_sb_source->local_nls,
					      cifs_sb_source->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			CIFSSMBClose(xid, pTcon, netfid);
		}
	}

cifs_rename_exit:
	kfree(fromName);
	kfree(toName);
	FreeXid(xid);
	return rc;
}

int cifs_revalidate(struct dentry *direntry)
{
	int xid;
	int rc = 0;
	char *full_path;
	struct cifs_sb_info *cifs_sb;
	struct cifsInodeInfo *cifsInode;
	loff_t local_size;
	struct timespec local_mtime;
	int invalidate_inode = FALSE;

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
		FreeXid(xid);
		return -ENOMEM;
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
					 direntry->d_sb, xid);
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
			invalidate_inode = TRUE;
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
		filemap_fdatawrite(direntry->d_inode->i_mapping);
	}
	if (invalidate_inode) {
	/* shrink_dcache not necessary now that cifs dentry ops
	are exported for negative dentries */
/*		if (S_ISDIR(direntry->d_inode->i_mode))
			shrink_dcache_parent(direntry); */
		if (S_ISREG(direntry->d_inode->i_mode)) {
			if (direntry->d_inode->i_mapping)
				filemap_fdatawait(direntry->d_inode->i_mapping);
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

	zero_user_page(page, offset, PAGE_CACHE_SIZE - offset, KM_USER0);
	unlock_page(page);
	page_cache_release(page);
	return rc;
}

static int cifs_vmtruncate(struct inode *inode, loff_t offset)
{
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	spin_lock(&inode->i_lock);
	if (inode->i_size < offset)
		goto do_expand;
	/*
	 * truncation of in-use swapfiles is disallowed - it would cause
	 * subsequent swapout to scribble on the now-freed blocks.
	 */
	if (IS_SWAPFILE(inode)) {
		spin_unlock(&inode->i_lock);
		goto out_busy;
	}
	i_size_write(inode, offset);
	spin_unlock(&inode->i_lock);
	/*
	 * unmap_mapping_range is called twice, first simply for efficiency
	 * so that truncate_inode_pages does fewer single-page unmaps. However
	 * after this first call, and before truncate_inode_pages finishes,
	 * it is possible for private pages to be COWed, which remain after
	 * truncate_inode_pages finishes, hence the second unmap_mapping_range
	 * call must be made for correctness.
	 */
	unmap_mapping_range(mapping, offset + PAGE_SIZE - 1, 0, 1);
	truncate_inode_pages(mapping, offset);
	unmap_mapping_range(mapping, offset + PAGE_SIZE - 1, 0, 1);
	goto out_truncate;

do_expand:
	limit = current->signal->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY && offset > limit) {
		spin_unlock(&inode->i_lock);
		goto out_sig;
	}
	if (offset > inode->i_sb->s_maxbytes) {
		spin_unlock(&inode->i_lock);
		goto out_big;
	}
	i_size_write(inode, offset);
	spin_unlock(&inode->i_lock);
out_truncate:
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out_big:
	return -EFBIG;
out_busy:
	return -ETXTBSY;
}

int cifs_setattr(struct dentry *direntry, struct iattr *attrs)
{
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	int rc = -EACCES;
	struct cifsFileInfo *open_file = NULL;
	FILE_BASIC_INFO time_buf;
	int set_time = FALSE;
	int set_dosattr = FALSE;
	__u64 mode = 0xFFFFFFFFFFFFFFFFULL;
	__u64 uid = 0xFFFFFFFFFFFFFFFFULL;
	__u64 gid = 0xFFFFFFFFFFFFFFFFULL;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cFYI(1, ("setattr on file %s attrs->iavalid 0x%x",
		 direntry->d_name.name, attrs->ia_valid));

	cifs_sb = CIFS_SB(direntry->d_inode->i_sb);
	pTcon = cifs_sb->tcon;

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM) == 0) {
		/* check if we have permission to change attrs */
		rc = inode_change_ok(direntry->d_inode, attrs);
		if (rc < 0) {
			FreeXid(xid);
			return rc;
		} else
			rc = 0;
	}

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}
	cifsInode = CIFS_I(direntry->d_inode);

	/* BB check if we need to refresh inode from server now ? BB */

	/* need to flush data before changing file size on server */
	filemap_write_and_wait(direntry->d_inode->i_mapping);

	if (attrs->ia_valid & ATTR_SIZE) {
		/* To avoid spurious oplock breaks from server, in the case of
		   inodes that we already have open, avoid doing path based
		   setting of file size if we can do it by handle.
		   This keeps our caching token (oplock) and avoids timeouts
		   when the local oplock break takes longer to flush
		   writebehind data than the SMB timeout for the SetPathInfo
		   request would allow */

		open_file = find_writable_file(cifsInode);
		if (open_file) {
			__u16 nfid = open_file->netfid;
			__u32 npid = open_file->pid;
			rc = CIFSSMBSetFileSize(xid, pTcon, attrs->ia_size,
						nfid, npid, FALSE);
			atomic_dec(&open_file->wrtPending);
			cFYI(1, ("SetFSize for attrs rc = %d", rc));
			if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
				unsigned int bytes_written;
				rc = CIFSSMBWrite(xid, pTcon,
						  nfid, 0, attrs->ia_size,
						  &bytes_written, NULL, NULL,
						  1 /* 45 seconds */);
				cFYI(1, ("Wrt seteof rc %d", rc));
			}
		} else
			rc = -EINVAL;

		if (rc != 0) {
			/* Set file size by pathname rather than by handle
			   either because no valid, writeable file handle for
			   it was found or because there was an error setting
			   it by handle */
			rc = CIFSSMBSetEOF(xid, pTcon, full_path,
					   attrs->ia_size, FALSE,
					   cifs_sb->local_nls,
					   cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			cFYI(1, ("SetEOF by path (setattrs) rc = %d", rc));
			if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
				__u16 netfid;
				int oplock = FALSE;

				rc = SMBLegacyOpen(xid, pTcon, full_path,
					FILE_OPEN,
					SYNCHRONIZE | FILE_WRITE_ATTRIBUTES,
					CREATE_NOT_DIR, &netfid, &oplock,
					NULL, cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
				if (rc == 0) {
					unsigned int bytes_written;
					rc = CIFSSMBWrite(xid, pTcon,
							netfid, 0,
							attrs->ia_size,
							&bytes_written, NULL,
							NULL, 1 /* 45 sec */);
					cFYI(1, ("wrt seteof rc %d", rc));
					CIFSSMBClose(xid, pTcon, netfid);
				}

			}
		}

		/* Server is ok setting allocation size implicitly - no need
		   to call:
		CIFSSMBSetEOF(xid, pTcon, full_path, attrs->ia_size, TRUE,
			 cifs_sb->local_nls);
		   */

		if (rc == 0) {
			rc = cifs_vmtruncate(direntry->d_inode, attrs->ia_size);
			cifs_truncate_page(direntry->d_inode->i_mapping,
					   direntry->d_inode->i_size);
		} else
			goto cifs_setattr_exit;
	}
	if (attrs->ia_valid & ATTR_UID) {
		cFYI(1, ("UID changed to %d", attrs->ia_uid));
		uid = attrs->ia_uid;
	}
	if (attrs->ia_valid & ATTR_GID) {
		cFYI(1, ("GID changed to %d", attrs->ia_gid));
		gid = attrs->ia_gid;
	}

	time_buf.Attributes = 0;

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attrs->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
		attrs->ia_valid &= ~ATTR_MODE;

	if (attrs->ia_valid & ATTR_MODE) {
		cFYI(1, ("Mode changed to 0x%x", attrs->ia_mode));
		mode = attrs->ia_mode;
	}

	if ((pTcon->unix_ext)
	    && (attrs->ia_valid & (ATTR_MODE | ATTR_GID | ATTR_UID)))
		rc = CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode, uid, gid,
					 0 /* dev_t */, cifs_sb->local_nls,
					 cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	else if (attrs->ia_valid & ATTR_MODE) {
		rc = 0;
		if ((mode & S_IWUGO) == 0) /* not writeable */ {
			if ((cifsInode->cifsAttrs & ATTR_READONLY) == 0) {
				set_dosattr = TRUE;
				time_buf.Attributes =
					cpu_to_le32(cifsInode->cifsAttrs |
						    ATTR_READONLY);
			}
		} else if (cifsInode->cifsAttrs & ATTR_READONLY) {
			/* If file is readonly on server, we would
			not be able to write to it - so if any write
			bit is enabled for user or group or other we
			need to at least try to remove r/o dos attr */
			set_dosattr = TRUE;
			time_buf.Attributes = cpu_to_le32(cifsInode->cifsAttrs &
					    (~ATTR_READONLY));
			/* Windows ignores set to zero */
			if (time_buf.Attributes == 0)
				time_buf.Attributes |= cpu_to_le32(ATTR_NORMAL);
		}
		/* BB to be implemented -
		   via Windows security descriptors or streams */
		/* CIFSSMBWinSetPerms(xid, pTcon, full_path, mode, uid, gid,
				      cifs_sb->local_nls); */
	}

	if (attrs->ia_valid & ATTR_ATIME) {
		set_time = TRUE;
		time_buf.LastAccessTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_atime));
	} else
		time_buf.LastAccessTime = 0;

	if (attrs->ia_valid & ATTR_MTIME) {
		set_time = TRUE;
		time_buf.LastWriteTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_mtime));
	} else
		time_buf.LastWriteTime = 0;
	/* Do not set ctime explicitly unless other time
	   stamps are changed explicitly (i.e. by utime()
	   since we would then have a mix of client and
	   server times */

	if (set_time && (attrs->ia_valid & ATTR_CTIME)) {
		set_time = TRUE;
		/* Although Samba throws this field away
		it may be useful to Windows - but we do
		not want to set ctime unless some other
		timestamp is changing */
		cFYI(1, ("CIFS - CTIME changed"));
		time_buf.ChangeTime =
		    cpu_to_le64(cifs_UnixTimeToNT(attrs->ia_ctime));
	} else
		time_buf.ChangeTime = 0;

	if (set_time || set_dosattr) {
		time_buf.CreationTime = 0;	/* do not change */
		/* In the future we should experiment - try setting timestamps
		   via Handle (SetFileInfo) instead of by path */
		if (!(pTcon->ses->flags & CIFS_SES_NT4))
			rc = CIFSSMBSetTimes(xid, pTcon, full_path, &time_buf,
					     cifs_sb->local_nls,
					     cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		else
			rc = -EOPNOTSUPP;

		if (rc == -EOPNOTSUPP) {
			int oplock = FALSE;
			__u16 netfid;

			cFYI(1, ("calling SetFileInfo since SetPathInfo for "
				 "times not supported by this server"));
			/* BB we could scan to see if we already have it open
			   and pass in pid of opener to function */
			rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN,
					 SYNCHRONIZE | FILE_WRITE_ATTRIBUTES,
					 CREATE_NOT_DIR, &netfid, &oplock,
					 NULL, cifs_sb->local_nls,
					 cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			if (rc == 0) {
				rc = CIFSSMBSetFileTimes(xid, pTcon, &time_buf,
							 netfid);
				CIFSSMBClose(xid, pTcon, netfid);
			} else {
			/* BB For even older servers we could convert time_buf
			   into old DOS style which uses two second
			   granularity */

			/* rc = CIFSSMBSetTimesLegacy(xid, pTcon, full_path,
					&time_buf, cifs_sb->local_nls); */
			}
		}
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
		rc = inode_setattr(direntry->d_inode, attrs);
cifs_setattr_exit:
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

#if 0
void cifs_delete_inode(struct inode *inode)
{
	cFYI(1, ("In cifs_delete_inode, inode = 0x%p", inode));
	/* may have to add back in if and when safe distributed caching of
	   directories added e.g. via FindNotify */
}
#endif
