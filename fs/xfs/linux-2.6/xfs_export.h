/*
 * Copyright (c) 2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_EXPORT_H__
#define __XFS_EXPORT_H__

/*
 * Common defines for code related to exporting XFS filesystems over NFS.
 *
 * The NFS fileid goes out on the wire as an array of
 * 32bit unsigned ints in host order.  There are 5 possible
 * formats.
 *
 * (1)	fileid_type=0x00
 *	(no fileid data; handled by the generic code)
 *
 * (2)	fileid_type=0x01
 *	inode-num
 *	generation
 *
 * (3)	fileid_type=0x02
 *	inode-num
 *	generation
 *	parent-inode-num
 *	parent-generation
 *
 * (4)	fileid_type=0x81
 *	inode-num-lo32
 *	inode-num-hi32
 *	generation
 *
 * (5)	fileid_type=0x82
 *	inode-num-lo32
 *	inode-num-hi32
 *	generation
 *	parent-inode-num-lo32
 *	parent-inode-num-hi32
 *	parent-generation
 *
 * Note, the NFS filehandle also includes an fsid portion which
 * may have an inode number in it.  That number is hardcoded to
 * 32bits and there is no way for XFS to intercept it.  In
 * practice this means when exporting an XFS filesystem with 64bit
 * inodes you should either export the mountpoint (rather than
 * a subdirectory) or use the "fsid" export option.
 */

/* This flag goes on the wire.  Don't play with it. */
#define XFS_FILEID_TYPE_64FLAG	0x80	/* NFS fileid has 64bit inodes */

/* Calculate the length in u32 units of the fileid data */
static inline int
xfs_fileid_length(int hasparent, int is64)
{
	return hasparent ? (is64 ? 6 : 4) : (is64 ? 3 : 2);
}

/*
 * Decode encoded inode information (either for the inode itself
 * or the parent) into an xfs_fid2_t structure.  Advances and
 * returns the new data pointer
 */
static inline __u32 *
xfs_fileid_decode_fid2(__u32 *p, xfs_fid2_t *fid, int is64)
{
	fid->fid_len = sizeof(xfs_fid2_t) - sizeof(fid->fid_len);
	fid->fid_pad = 0;
	fid->fid_ino = *p++;
#if XFS_BIG_INUMS
	if (is64)
		fid->fid_ino |= (((__u64)(*p++)) << 32);
#endif
	fid->fid_gen = *p++;
	return p;
}

/*
 * Encode inode information (either for the inode itself or the
 * parent) into a fileid buffer.  Advances and returns the new
 * data pointer.
 */
static inline __u32 *
xfs_fileid_encode_inode(__u32 *p, struct inode *inode, int is64)
{
	*p++ = (__u32)inode->i_ino;
#if XFS_BIG_INUMS
	if (is64)
		*p++ = (__u32)(inode->i_ino >> 32);
#endif
	*p++ = inode->i_generation;
	return p;
}

#endif	/* __XFS_EXPORT_H__ */
