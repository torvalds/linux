/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
 * Copyright (c) 2008, 2009 Reinoud Zandijk
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Original definitions written by Koji Sato <koji@osrg.net>
 *                    and Ryusuke Konishi <ryusuke@osrg.net>
 * From: NetBSD: nandfs_fs.h,v 1.1 2009/07/18 16:31:42 reinoud
 *
 * $FreeBSD$
 */

#ifndef _NANDFS_FS_H
#define _NANDFS_FS_H

#include <sys/uuid.h>

#define	MNINDIR(fsdev)	((fsdev)->nd_blocksize / sizeof(nandfs_daddr_t))

/*
 * Inode structure. There are a few dedicated inode numbers that are
 * defined here first.
 */
#define	NANDFS_WHT_INO		1	/* Whiteout ino			*/
#define	NANDFS_ROOT_INO		2	/* Root file inode		*/
#define	NANDFS_DAT_INO		3	/* DAT file			*/
#define	NANDFS_CPFILE_INO	4	/* checkpoint file		*/
#define	NANDFS_SUFILE_INO	5	/* segment usage file		*/
#define	NANDFS_IFILE_INO	6	/* ifile			*/
#define	NANDFS_GC_INO		7	/* Cleanerd node		*/
#define	NANDFS_ATIME_INO	8	/* Atime file (reserved)	*/
#define	NANDFS_XATTR_INO	9	/* Xattribute file (reserved)	*/
#define	NANDFS_SKETCH_INO	10	/* Sketch file (obsolete)	*/
#define	NANDFS_USER_INO		11	/* First user's file inode number */

#define	NANDFS_SYS_NODE(ino) \
	(((ino) >= NANDFS_DAT_INO) && ((ino) <= NANDFS_GC_INO))

#define	NANDFS_NDADDR		12	/* Direct addresses in inode. */
#define	NANDFS_NIADDR		3	/* Indirect addresses in inode. */

typedef	int64_t		nandfs_daddr_t;
typedef	int64_t		nandfs_lbn_t;

struct nandfs_inode {
	uint64_t	i_blocks;	/* 0: size in device blocks		*/
	uint64_t	i_size;		/* 8: size in bytes			*/
	uint64_t	i_ctime;	/* 16: creation time in seconds		*/
	uint64_t	i_mtime;	/* 24: modification time in seconds part*/
	uint32_t	i_ctime_nsec;	/* 32: creation time nanoseconds part	*/
	uint32_t	i_mtime_nsec;	/* 36: modification time in nanoseconds	*/
	uint32_t	i_uid;		/* 40: user id				*/
	uint32_t	i_gid;		/* 44: group id				*/
	uint16_t	i_mode;		/* 48: file mode			*/
	uint16_t	i_links_count;	/* 50: number of references to the inode*/
	uint32_t	i_flags;	/* 52: NANDFS_*_FL flags		*/
	nandfs_daddr_t	i_special;	/* 56: special				*/
	nandfs_daddr_t	i_db[NANDFS_NDADDR]; /* 64: Direct disk blocks.		*/
	nandfs_daddr_t	i_ib[NANDFS_NIADDR]; /* 160: Indirect disk blocks.	*/
	uint64_t	i_xattr;	/* 184: reserved for extended attributes*/
	uint32_t	i_generation;	/* 192: file generation for NFS		*/
	uint32_t	i_pad[15];	/* 196: make it 64 bits aligned		*/
};

#ifdef _KERNEL
CTASSERT(sizeof(struct nandfs_inode) == 256);
#endif

/*
 * Each checkpoint/snapshot has a super root.
 *
 * The super root holds the inodes of the three system files: `dat', `cp' and
 * 'su' files. All other FS state is defined by those.
 *
 * It is CRC checksum'ed and time stamped.
 */

struct nandfs_super_root {
	uint32_t	sr_sum;		/* check-sum				*/
	uint16_t	sr_bytes;	/* byte count of this structure		*/
	uint16_t	sr_flags;	/* reserved for flags			*/
	uint64_t	sr_nongc_ctime;	/* timestamp, not for cleaner(?)	*/
	struct nandfs_inode sr_dat;	/* DAT, virt->phys translation inode	*/
	struct nandfs_inode sr_cpfile;	/* CP, checkpoints inode		*/
	struct nandfs_inode sr_sufile;	/* SU, segment usage inode		*/
};

#define	NANDFS_SR_MDT_OFFSET(inode_size, i)			\
	((uint32_t)&((struct nandfs_super_root *)0)->sr_dat +	\
	(inode_size) * (i))

#define	NANDFS_SR_DAT_OFFSET(inode_size)	NANDFS_SR_MDT_OFFSET(inode_size, 0)
#define	NANDFS_SR_CPFILE_OFFSET(inode_size)	NANDFS_SR_MDT_OFFSET(inode_size, 1)
#define	NANDFS_SR_SUFILE_OFFSET(inode_size)	NANDFS_SR_MDT_OFFSET(inode_size, 2)
#define	NANDFS_SR_BYTES			(sizeof(struct nandfs_super_root))

/*
 * The superblock describes the basic structure and mount history. It also
 * records some sizes of structures found on the disc for sanity checks.
 *
 * The superblock is stored at two places: NANDFS_SB_OFFSET_BYTES and
 * NANDFS_SB2_OFFSET_BYTES.
 */

/* File system states stored on media in superblock's sbp->s_state */
#define	NANDFS_VALID_FS		0x0001	/* cleanly unmounted and all is ok  */
#define	NANDFS_ERROR_FS		0x0002	/* there were errors detected, fsck */
#define	NANDFS_RESIZE_FS	0x0004	/* resize required, XXX unknown flag*/
#define	NANDFS_MOUNT_STATE_BITS	"\20\1VALID_FS\2ERROR_FS\3RESIZE_FS"

/*
 * Brief description of control structures:
 *
 * NANDFS_NFSAREAS first blocks contain fsdata and some amount of super blocks.
 * Simple round-robin policy is used in order to choose which block will
 * contain new super block.
 *
 * Simple case with 2 blocks:
 * 1: fsdata sblock1 [sblock3 [sblock5 ..]]
 * 2: fsdata sblock2 [sblock4 [sblock6 ..]]
 */
struct nandfs_fsdata {
	uint16_t	f_magic;
	uint16_t	f_bytes;

	uint32_t	f_sum;		/* checksum of fsdata		*/
	uint32_t	f_rev_level;	/* major disk format revision	*/

	uint64_t	f_ctime;	/* creation time (execution time
					   of newfs)			*/
	/* Block size represented as: blocksize = 1 << (f_log_block_size + 10)	*/
	uint32_t	f_log_block_size;

	uint16_t	f_inode_size;		/* size of an inode		*/
	uint16_t	f_dat_entry_size;	/* size of a dat entry		*/
	uint16_t	f_checkpoint_size;	/* size of a checkpoint		*/
	uint16_t	f_segment_usage_size;	/* size of a segment usage	*/

	uint16_t	f_sbbytes;		/* byte count of CRC calculation
						   for super blocks. s_reserved
						   is excluded!			*/

	uint16_t	f_errors;		/* behaviour on detecting errors	*/

	uint32_t	f_erasesize;
	uint64_t	f_nsegments;		/* number of segm. in filesystem	*/
	nandfs_daddr_t	f_first_data_block;	/* 1st seg disk block number		*/
	uint32_t	f_blocks_per_segment;	/* number of blocks per segment		*/
	uint32_t	f_r_segments_percentage;	/* reserved segments percentage		*/

	struct uuid	f_uuid;			/* 128-bit uuid for volume		*/
	char		f_volume_name[16];	/* volume name				*/
	uint32_t	f_pad[104];
} __packed;

#ifdef _KERNEL
CTASSERT(sizeof(struct nandfs_fsdata) == 512);
#endif

struct nandfs_super_block {
	uint16_t	s_magic;		/* magic value for identification */

	uint32_t	s_sum;			/* check sum of super block       */

	uint64_t	s_last_cno;		/* last checkpoint number         */
	uint64_t	s_last_pseg;		/* addr part. segm. written last  */
	uint64_t	s_last_seq;		/* seq.number of seg written last */
	uint64_t	s_free_blocks_count;	/* free blocks count              */

	uint64_t	s_mtime;		/* mount time                     */
	uint64_t	s_wtime;		/* write time                     */
	uint16_t	s_state;		/* file system state              */

	char		s_last_mounted[64];	/* directory where last mounted   */

	uint32_t	s_c_interval;		/* commit interval of segment     */
	uint32_t	s_c_block_max;		/* threshold of data amount for
						   the segment construction */
	uint32_t	s_reserved[32];		/* padding to end of the block    */
} __packed;

#ifdef _KERNEL
CTASSERT(sizeof(struct nandfs_super_block) == 256);
#endif

#define	NANDFS_FSDATA_MAGIC	0xf8da
#define	NANDFS_SUPER_MAGIC	0x8008

#define	NANDFS_NFSAREAS		4
#define	NANDFS_DATA_OFFSET_BYTES(esize)	(NANDFS_NFSAREAS * (esize))

#define	NANDFS_SBLOCK_OFFSET_BYTES (sizeof(struct nandfs_fsdata))

#define	NANDFS_DEF_BLOCKSIZE	4096
#define	NANDFS_MIN_BLOCKSIZE	512

#define	NANDFS_DEF_ERASESIZE	(2 << 16)

#define	NANDFS_MIN_SEGSIZE	NANDFS_DEF_ERASESIZE

#define	NANDFS_CURRENT_REV	9	/* current major revision */

#define	NANDFS_FSDATA_CRC_BYTES offsetof(struct nandfs_fsdata, f_pad)
/* Bytes count of super_block for CRC-calculation */
#define	NANDFS_SB_BYTES  offsetof(struct nandfs_super_block, s_reserved)

/* Maximal count of links to a file */
#define	NANDFS_LINK_MAX		32000

/*
 * Structure of a directory entry.
 *
 * Note that they can't span blocks; the rec_len fills out.
 */

#define	NANDFS_NAME_LEN 255
struct nandfs_dir_entry {
	uint64_t	inode;			/* inode number */
	uint16_t	rec_len;		/* directory entry length */
	uint8_t		name_len;		/* name length */
	uint8_t		file_type;
	char		name[NANDFS_NAME_LEN];	/* file name */
	char		pad;
};

/*
 * NANDFS_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 8
 */
#define	NANDFS_DIR_PAD			8
#define	NANDFS_DIR_ROUND		(NANDFS_DIR_PAD - 1)
#define	NANDFS_DIR_NAME_OFFSET		(offsetof(struct nandfs_dir_entry, name))
#define	NANDFS_DIR_REC_LEN(name_len)					\
	(((name_len) + NANDFS_DIR_NAME_OFFSET + NANDFS_DIR_ROUND)	\
	& ~NANDFS_DIR_ROUND)
#define	NANDFS_DIR_NAME_LEN(name_len)	\
	(NANDFS_DIR_REC_LEN(name_len) - NANDFS_DIR_NAME_OFFSET)

/*
 * NiLFS/NANDFS devides the disc into fixed length segments. Each segment is
 * filled with one or more partial segments of variable lengths.
 *
 * Each partial segment has a segment summary header followed by updates of
 * files and optionally a super root.
 */

/*
 * Virtual to physical block translation information. For data blocks it maps
 * logical block number bi_blkoff to virtual block nr bi_vblocknr. For non
 * datablocks it is the virtual block number assigned to an indirect block
 * and has no bi_blkoff. The physical block number is the next
 * available data block in the partial segment after all the binfo's.
 */
struct nandfs_binfo_v {
	uint64_t	bi_ino;		/* file's inode			     */
	uint64_t	bi_vblocknr;	/* assigned virtual block number     */
	uint64_t	bi_blkoff;	/* for file's logical block number   */
};

/*
 * DAT allocation. For data blocks just the logical block number that maps on
 * the next available data block in the partial segment after the binfo's.
 */
struct nandfs_binfo_dat {
	uint64_t	bi_ino;
	uint64_t	bi_blkoff;	/* DAT file's logical block number */
	uint8_t		bi_level;	/* whether this is meta block */
	uint8_t		bi_pad[7];
};

#ifdef _KERNEL
CTASSERT(sizeof(struct nandfs_binfo_v) == sizeof(struct nandfs_binfo_dat));
#endif

/* Convenience union for both types of binfo's */
union nandfs_binfo {
	struct nandfs_binfo_v bi_v;
	struct nandfs_binfo_dat bi_dat;
};

/* Indirect buffers path */
struct nandfs_indir {
	nandfs_daddr_t	in_lbn;
	int		in_off;
};

/* The (partial) segment summary */
struct nandfs_segment_summary {
	uint32_t	ss_datasum;	/* CRC of complete data block        */
	uint32_t	ss_sumsum;	/* CRC of segment summary only       */
	uint32_t	ss_magic;	/* magic to identify segment summary */
	uint16_t	ss_bytes;	/* size of segment summary structure */
	uint16_t	ss_flags;	/* NANDFS_SS_* flags                  */
	uint64_t	ss_seq;		/* sequence number of this segm. sum */
	uint64_t	ss_create;	/* creation timestamp in seconds     */
	uint64_t	ss_next;	/* blocknumber of next segment       */
	uint32_t	ss_nblocks;	/* number of blocks used by summary  */
	uint32_t	ss_nbinfos;	/* number of binfo structures	     */
	uint32_t	ss_sumbytes;	/* total size of segment summary     */
	uint32_t	ss_pad;
	/* stream of binfo structures */
};

#define	NANDFS_SEGSUM_MAGIC	0x8e680011	/* segment summary magic number */

/* Segment summary flags */
#define	NANDFS_SS_LOGBGN	0x0001	/* begins a logical segment */
#define	NANDFS_SS_LOGEND	0x0002	/* ends a logical segment */
#define	NANDFS_SS_SR		0x0004	/* has super root */
#define	NANDFS_SS_SYNDT		0x0008	/* includes data only updates */
#define	NANDFS_SS_GC		0x0010	/* segment written for cleaner operation */
#define	NANDFS_SS_FLAG_BITS	"\20\1LOGBGN\2LOGEND\3SR\4SYNDT\5GC"

/* Segment summary constrains */
#define	NANDFS_SEG_MIN_BLOCKS	16	/* minimum number of blocks in a
					   full segment */
#define	NANDFS_PSEG_MIN_BLOCKS	2	/* minimum number of blocks in a
					   partial segment */
#define	NANDFS_MIN_NRSVSEGS	8	/* minimum number of reserved
					   segments */

/*
 * Structure of DAT/inode file.
 *
 * A DAT file is divided into groups. The maximum number of groups is the
 * number of block group descriptors that fit into one block; this descriptor
 * only gives the number of free entries in the associated group.
 *
 * Each group has a block sized bitmap indicating if an entry is taken or
 * empty. Each bit stands for a DAT entry.
 *
 * The inode file has exactly the same format only the entries are inode
 * entries.
 */

struct nandfs_block_group_desc {
	uint32_t	bg_nfrees;	/* num. free entries in block group  */
};

/* DAT entry in a super root's DAT file */
struct nandfs_dat_entry {
	uint64_t	de_blocknr;	/* block number                      */
	uint64_t	de_start;	/* valid from checkpoint             */
	uint64_t	de_end;		/* valid till checkpoint             */
	uint64_t	de_rsv;		/* reserved for future use           */
};

/*
 * Structure of CP file.
 *
 * A snapshot is just a checkpoint only it's protected against removal by the
 * cleaner. The snapshots are kept on a double linked list of checkpoints.
 */
struct nandfs_snapshot_list {
	uint64_t	ssl_next;	/* checkpoint nr. forward */
	uint64_t	ssl_prev;	/* checkpoint nr. back    */
};

/* Checkpoint entry structure */
struct nandfs_checkpoint {
	uint32_t	cp_flags;		/* NANDFS_CHECKPOINT_* flags          */
	uint32_t	cp_checkpoints_count;	/* ZERO, not used anymore?           */
	struct nandfs_snapshot_list cp_snapshot_list; /* list of snapshots   */
	uint64_t	cp_cno;			/* checkpoint number                 */
	uint64_t	cp_create;		/* creation timestamp                */
	uint64_t	cp_nblk_inc;		/* number of blocks incremented      */
	uint64_t	cp_blocks_count;	/* reserved (might be deleted)       */
	struct nandfs_inode cp_ifile_inode;	/* inode file inode          */
};

/* Checkpoint flags */
#define	NANDFS_CHECKPOINT_SNAPSHOT	1
#define	NANDFS_CHECKPOINT_INVALID	2
#define	NANDFS_CHECKPOINT_SKETCH	4
#define	NANDFS_CHECKPOINT_MINOR		8
#define	NANDFS_CHECKPOINT_BITS		"\20\1SNAPSHOT\2INVALID\3SKETCH\4MINOR"

/* Header of the checkpoint file */
struct nandfs_cpfile_header {
	uint64_t	ch_ncheckpoints;	/* number of checkpoints             */
	uint64_t	ch_nsnapshots;	/* number of snapshots               */
	struct nandfs_snapshot_list ch_snapshot_list;	/* snapshot list     */
};

#define	NANDFS_CPFILE_FIRST_CHECKPOINT_OFFSET		\
	((sizeof(struct nandfs_cpfile_header) +		\
	sizeof(struct nandfs_checkpoint) - 1) /		\
	sizeof(struct nandfs_checkpoint))


#define NANDFS_NOSEGMENT        0xffffffff

/*
 * Structure of SU file.
 *
 * The segment usage file sums up how each of the segments are used. They are
 * indexed by their segment number.
 */

/* Segment usage entry */
struct nandfs_segment_usage {
	uint64_t	su_lastmod;	/* last modified timestamp           */
	uint32_t	su_nblocks;	/* number of blocks in segment       */
	uint32_t	su_flags;	/* NANDFS_SEGMENT_USAGE_* flags       */
};

/* Segment usage flag */
#define	NANDFS_SEGMENT_USAGE_ACTIVE	1
#define	NANDFS_SEGMENT_USAGE_DIRTY	2
#define	NANDFS_SEGMENT_USAGE_ERROR	4
#define	NANDFS_SEGMENT_USAGE_GC		8
#define	NANDFS_SEGMENT_USAGE_BITS	"\20\1ACTIVE\2DIRTY\3ERROR"

/* Header of the segment usage file */
struct nandfs_sufile_header {
	uint64_t	sh_ncleansegs;	/* number of segments marked clean   */
	uint64_t	sh_ndirtysegs;	/* number of segments marked dirty   */
	uint64_t	sh_last_alloc;	/* last allocated segment number     */
};

#define	NANDFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET	\
	((sizeof(struct nandfs_sufile_header) +		\
	sizeof(struct nandfs_segment_usage) - 1) /	\
	sizeof(struct nandfs_segment_usage))

struct nandfs_seg_stat {
	uint64_t	nss_nsegs;
	uint64_t	nss_ncleansegs;
	uint64_t	nss_ndirtysegs;
	uint64_t	nss_ctime;
	uint64_t	nss_nongc_ctime;
	uint64_t	nss_prot_seq;
};

enum {
	NANDFS_CHECKPOINT,
	NANDFS_SNAPSHOT
};

#define	NANDFS_CPINFO_MAX		512

struct nandfs_cpinfo {
	uint32_t	nci_flags;
	uint32_t	nci_pad;
	uint64_t	nci_cno;
	uint64_t	nci_create;
	uint64_t	nci_nblk_inc;
	uint64_t	nci_blocks_count;
	uint64_t	nci_next;
};

#define	NANDFS_SEGMENTS_MAX	512

struct nandfs_suinfo {
	uint64_t	nsi_num;
	uint64_t	nsi_lastmod;
	uint32_t	nsi_blocks;
	uint32_t	nsi_flags;
};

#define	NANDFS_VINFO_MAX	512

struct nandfs_vinfo {
	uint64_t	nvi_ino;
	uint64_t	nvi_vblocknr;
	uint64_t	nvi_start;
	uint64_t	nvi_end;
	uint64_t	nvi_blocknr;
	int		nvi_alive;
};

struct nandfs_cpmode {
	uint64_t	ncpm_cno;
	uint32_t	ncpm_mode;
	uint32_t	ncpm_pad;
};

struct nandfs_argv {
	uint64_t	nv_base;
	uint32_t	nv_nmembs;
	uint16_t	nv_size;
	uint16_t	nv_flags;
	uint64_t	nv_index;
};

struct nandfs_cpstat {
	uint64_t	ncp_cno;
	uint64_t	ncp_ncps;
	uint64_t	ncp_nss;
};

struct nandfs_period {
	uint64_t	p_start;
	uint64_t	p_end;
};

struct nandfs_vdesc {
	uint64_t	vd_ino;
	uint64_t	vd_cno;
	uint64_t	vd_vblocknr;
	struct nandfs_period	vd_period;
	uint64_t	vd_blocknr;
	uint64_t	vd_offset;
	uint32_t	vd_flags;
	uint32_t	vd_pad;
};

struct nandfs_bdesc {
	uint64_t	bd_ino;
	uint64_t	bd_oblocknr;
	uint64_t	bd_blocknr;
	uint64_t	bd_offset;
	uint32_t	bd_level;
	uint32_t	bd_alive;
};

#ifndef _KERNEL
#ifndef	MNAMELEN
#define	MNAMELEN	1024
#endif
#endif

struct nandfs_fsinfo {
	struct nandfs_fsdata		fs_fsdata;
	struct nandfs_super_block	fs_super;
	char				fs_dev[MNAMELEN];
};

#define	NANDFS_MAX_MOUNTS	65535

#define	NANDFS_IOCTL_GET_SUSTAT		_IOR('N', 100, struct nandfs_seg_stat)
#define	NANDFS_IOCTL_CHANGE_CPMODE	_IOWR('N', 101, struct nandfs_cpmode)
#define	NANDFS_IOCTL_GET_CPINFO		_IOWR('N', 102, struct nandfs_argv)
#define	NANDFS_IOCTL_DELETE_CP		_IOWR('N', 103, uint64_t[2])
#define	NANDFS_IOCTL_GET_CPSTAT		_IOR('N', 104, struct nandfs_cpstat)
#define	NANDFS_IOCTL_GET_SUINFO		_IOWR('N', 105, struct nandfs_argv)
#define	NANDFS_IOCTL_GET_VINFO		_IOWR('N', 106, struct nandfs_argv)
#define	NANDFS_IOCTL_GET_BDESCS		_IOWR('N', 107, struct nandfs_argv)
#define	NANDFS_IOCTL_GET_FSINFO		_IOR('N', 108, struct nandfs_fsinfo)
#define	NANDFS_IOCTL_MAKE_SNAP		_IOWR('N', 109, uint64_t)
#define	NANDFS_IOCTL_DELETE_SNAP	_IOWR('N', 110, uint64_t)
#define	NANDFS_IOCTL_SYNC		_IOWR('N', 111, uint64_t)

#endif /* _NANDFS_FS_H */
