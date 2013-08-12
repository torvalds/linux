/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#ifndef	__XFS_LOG_FORMAT_H__
#define __XFS_LOG_FORMAT_H__

/*
 * On-disk Log Format definitions.
 *
 * This file contains all the on-disk format definitions used within the log. It
 * includes the physical log structure itself, as well as all the log item
 * format structures that are written into the log and intepreted by log
 * recovery. We start with the physical log format definitions, and then work
 * through all the log items definitions and everything they encode into the
 * log.
 */
typedef __uint32_t xlog_tid_t;

#define XLOG_MIN_ICLOGS		2
#define XLOG_MAX_ICLOGS		8
#define XLOG_HEADER_MAGIC_NUM	0xFEEDbabe	/* Invalid cycle number */
#define XLOG_VERSION_1		1
#define XLOG_VERSION_2		2		/* Large IClogs, Log sunit */
#define XLOG_VERSION_OKBITS	(XLOG_VERSION_1 | XLOG_VERSION_2)
#define XLOG_MIN_RECORD_BSIZE	(16*1024)	/* eventually 32k */
#define XLOG_BIG_RECORD_BSIZE	(32*1024)	/* 32k buffers */
#define XLOG_MAX_RECORD_BSIZE	(256*1024)
#define XLOG_HEADER_CYCLE_SIZE	(32*1024)	/* cycle data in header */
#define XLOG_MIN_RECORD_BSHIFT	14		/* 16384 == 1 << 14 */
#define XLOG_BIG_RECORD_BSHIFT	15		/* 32k == 1 << 15 */
#define XLOG_MAX_RECORD_BSHIFT	18		/* 256k == 1 << 18 */
#define XLOG_BTOLSUNIT(log, b)  (((b)+(log)->l_mp->m_sb.sb_logsunit-1) / \
                                 (log)->l_mp->m_sb.sb_logsunit)
#define XLOG_LSUNITTOB(log, su) ((su) * (log)->l_mp->m_sb.sb_logsunit)

#define XLOG_HEADER_SIZE	512

#define XLOG_REC_SHIFT(log) \
	BTOBB(1 << (xfs_sb_version_haslogv2(&log->l_mp->m_sb) ? \
	 XLOG_MAX_RECORD_BSHIFT : XLOG_BIG_RECORD_BSHIFT))
#define XLOG_TOTAL_REC_SHIFT(log) \
	BTOBB(XLOG_MAX_ICLOGS << (xfs_sb_version_haslogv2(&log->l_mp->m_sb) ? \
	 XLOG_MAX_RECORD_BSHIFT : XLOG_BIG_RECORD_BSHIFT))

/* get lsn fields */
#define CYCLE_LSN(lsn) ((uint)((lsn)>>32))
#define BLOCK_LSN(lsn) ((uint)(lsn))

/* this is used in a spot where we might otherwise double-endian-flip */
#define CYCLE_LSN_DISK(lsn) (((__be32 *)&(lsn))[0])

static inline xfs_lsn_t xlog_assign_lsn(uint cycle, uint block)
{
	return ((xfs_lsn_t)cycle << 32) | block;
}

static inline uint xlog_get_cycle(char *ptr)
{
	if (be32_to_cpu(*(__be32 *)ptr) == XLOG_HEADER_MAGIC_NUM)
		return be32_to_cpu(*((__be32 *)ptr + 1));
	else
		return be32_to_cpu(*(__be32 *)ptr);
}

/* Log Clients */
#define XFS_TRANSACTION		0x69
#define XFS_VOLUME		0x2
#define XFS_LOG			0xaa

#define XLOG_UNMOUNT_TYPE	0x556e	/* Un for Unmount */

/* Region types for iovec's i_type */
#define XLOG_REG_TYPE_BFORMAT		1
#define XLOG_REG_TYPE_BCHUNK		2
#define XLOG_REG_TYPE_EFI_FORMAT	3
#define XLOG_REG_TYPE_EFD_FORMAT	4
#define XLOG_REG_TYPE_IFORMAT		5
#define XLOG_REG_TYPE_ICORE		6
#define XLOG_REG_TYPE_IEXT		7
#define XLOG_REG_TYPE_IBROOT		8
#define XLOG_REG_TYPE_ILOCAL		9
#define XLOG_REG_TYPE_IATTR_EXT		10
#define XLOG_REG_TYPE_IATTR_BROOT	11
#define XLOG_REG_TYPE_IATTR_LOCAL	12
#define XLOG_REG_TYPE_QFORMAT		13
#define XLOG_REG_TYPE_DQUOT		14
#define XLOG_REG_TYPE_QUOTAOFF		15
#define XLOG_REG_TYPE_LRHEADER		16
#define XLOG_REG_TYPE_UNMOUNT		17
#define XLOG_REG_TYPE_COMMIT		18
#define XLOG_REG_TYPE_TRANSHDR		19
#define XLOG_REG_TYPE_ICREATE		20
#define XLOG_REG_TYPE_MAX		20

/*
 * Flags to log operation header
 *
 * The first write of a new transaction will be preceded with a start
 * record, XLOG_START_TRANS.  Once a transaction is committed, a commit
 * record is written, XLOG_COMMIT_TRANS.  If a single region can not fit into
 * the remainder of the current active in-core log, it is split up into
 * multiple regions.  Each partial region will be marked with a
 * XLOG_CONTINUE_TRANS until the last one, which gets marked with XLOG_END_TRANS.
 *
 */
#define XLOG_START_TRANS	0x01	/* Start a new transaction */
#define XLOG_COMMIT_TRANS	0x02	/* Commit this transaction */
#define XLOG_CONTINUE_TRANS	0x04	/* Cont this trans into new region */
#define XLOG_WAS_CONT_TRANS	0x08	/* Cont this trans into new region */
#define XLOG_END_TRANS		0x10	/* End a continued transaction */
#define XLOG_UNMOUNT_TRANS	0x20	/* Unmount a filesystem transaction */


typedef struct xlog_op_header {
	__be32	   oh_tid;	/* transaction id of operation	:  4 b */
	__be32	   oh_len;	/* bytes in data region		:  4 b */
	__u8	   oh_clientid;	/* who sent me this		:  1 b */
	__u8	   oh_flags;	/*				:  1 b */
	__u16	   oh_res2;	/* 32 bit align			:  2 b */
} xlog_op_header_t;


/* valid values for h_fmt */
#define XLOG_FMT_UNKNOWN  0
#define XLOG_FMT_LINUX_LE 1
#define XLOG_FMT_LINUX_BE 2
#define XLOG_FMT_IRIX_BE  3

/* our fmt */
#ifdef XFS_NATIVE_HOST
#define XLOG_FMT XLOG_FMT_LINUX_BE
#else
#define XLOG_FMT XLOG_FMT_LINUX_LE
#endif

typedef struct xlog_rec_header {
	__be32	  h_magicno;	/* log record (LR) identifier		:  4 */
	__be32	  h_cycle;	/* write cycle of log			:  4 */
	__be32	  h_version;	/* LR version				:  4 */
	__be32	  h_len;	/* len in bytes; should be 64-bit aligned: 4 */
	__be64	  h_lsn;	/* lsn of this LR			:  8 */
	__be64	  h_tail_lsn;	/* lsn of 1st LR w/ buffers not committed: 8 */
	__le32	  h_crc;	/* crc of log record                    :  4 */
	__be32	  h_prev_block; /* block number to previous LR		:  4 */
	__be32	  h_num_logops;	/* number of log operations in this LR	:  4 */
	__be32	  h_cycle_data[XLOG_HEADER_CYCLE_SIZE / BBSIZE];
	/* new fields */
	__be32    h_fmt;        /* format of log record                 :  4 */
	uuid_t	  h_fs_uuid;    /* uuid of FS                           : 16 */
	__be32	  h_size;	/* iclog size				:  4 */
} xlog_rec_header_t;

typedef struct xlog_rec_ext_header {
	__be32	  xh_cycle;	/* write cycle of log			: 4 */
	__be32	  xh_cycle_data[XLOG_HEADER_CYCLE_SIZE / BBSIZE]; /*	: 256 */
} xlog_rec_ext_header_t;

/*
 * Quite misnamed, because this union lays out the actual on-disk log buffer.
 */
typedef union xlog_in_core2 {
	xlog_rec_header_t	hic_header;
	xlog_rec_ext_header_t	hic_xheader;
	char			hic_sector[XLOG_HEADER_SIZE];
} xlog_in_core_2_t;

/* not an on-disk structure, but needed by log recovery in userspace */
typedef struct xfs_log_iovec {
	void		*i_addr;	/* beginning address of region */
	int		i_len;		/* length in bytes of region */
	uint		i_type;		/* type of region */
} xfs_log_iovec_t;


/*
 * Inode Log Item Format definitions.
 *
 * This is the structure used to lay out an inode log item in the
 * log.  The size of the inline data/extents/b-tree root to be logged
 * (if any) is indicated in the ilf_dsize field.  Changes to this structure
 * must be added on to the end.
 */
typedef struct xfs_inode_log_format {
	__uint16_t		ilf_type;	/* inode log item type */
	__uint16_t		ilf_size;	/* size of this item */
	__uint32_t		ilf_fields;	/* flags for fields logged */
	__uint16_t		ilf_asize;	/* size of attr d/ext/root */
	__uint16_t		ilf_dsize;	/* size of data/ext/root */
	__uint64_t		ilf_ino;	/* inode number */
	union {
		__uint32_t	ilfu_rdev;	/* rdev value for dev inode*/
		uuid_t		ilfu_uuid;	/* mount point value */
	} ilf_u;
	__int64_t		ilf_blkno;	/* blkno of inode buffer */
	__int32_t		ilf_len;	/* len of inode buffer */
	__int32_t		ilf_boffset;	/* off of inode in buffer */
} xfs_inode_log_format_t;

typedef struct xfs_inode_log_format_32 {
	__uint16_t		ilf_type;	/* inode log item type */
	__uint16_t		ilf_size;	/* size of this item */
	__uint32_t		ilf_fields;	/* flags for fields logged */
	__uint16_t		ilf_asize;	/* size of attr d/ext/root */
	__uint16_t		ilf_dsize;	/* size of data/ext/root */
	__uint64_t		ilf_ino;	/* inode number */
	union {
		__uint32_t	ilfu_rdev;	/* rdev value for dev inode*/
		uuid_t		ilfu_uuid;	/* mount point value */
	} ilf_u;
	__int64_t		ilf_blkno;	/* blkno of inode buffer */
	__int32_t		ilf_len;	/* len of inode buffer */
	__int32_t		ilf_boffset;	/* off of inode in buffer */
} __attribute__((packed)) xfs_inode_log_format_32_t;

typedef struct xfs_inode_log_format_64 {
	__uint16_t		ilf_type;	/* inode log item type */
	__uint16_t		ilf_size;	/* size of this item */
	__uint32_t		ilf_fields;	/* flags for fields logged */
	__uint16_t		ilf_asize;	/* size of attr d/ext/root */
	__uint16_t		ilf_dsize;	/* size of data/ext/root */
	__uint32_t		ilf_pad;	/* pad for 64 bit boundary */
	__uint64_t		ilf_ino;	/* inode number */
	union {
		__uint32_t	ilfu_rdev;	/* rdev value for dev inode*/
		uuid_t		ilfu_uuid;	/* mount point value */
	} ilf_u;
	__int64_t		ilf_blkno;	/* blkno of inode buffer */
	__int32_t		ilf_len;	/* len of inode buffer */
	__int32_t		ilf_boffset;	/* off of inode in buffer */
} xfs_inode_log_format_64_t;

/*
 * Flags for xfs_trans_log_inode flags field.
 */
#define	XFS_ILOG_CORE	0x001	/* log standard inode fields */
#define	XFS_ILOG_DDATA	0x002	/* log i_df.if_data */
#define	XFS_ILOG_DEXT	0x004	/* log i_df.if_extents */
#define	XFS_ILOG_DBROOT	0x008	/* log i_df.i_broot */
#define	XFS_ILOG_DEV	0x010	/* log the dev field */
#define	XFS_ILOG_UUID	0x020	/* log the uuid field */
#define	XFS_ILOG_ADATA	0x040	/* log i_af.if_data */
#define	XFS_ILOG_AEXT	0x080	/* log i_af.if_extents */
#define	XFS_ILOG_ABROOT	0x100	/* log i_af.i_broot */


/*
 * The timestamps are dirty, but not necessarily anything else in the inode
 * core.  Unlike the other fields above this one must never make it to disk
 * in the ilf_fields of the inode_log_format, but is purely store in-memory in
 * ili_fields in the inode_log_item.
 */
#define XFS_ILOG_TIMESTAMP	0x4000

#define	XFS_ILOG_NONCORE	(XFS_ILOG_DDATA | XFS_ILOG_DEXT | \
				 XFS_ILOG_DBROOT | XFS_ILOG_DEV | \
				 XFS_ILOG_UUID | XFS_ILOG_ADATA | \
				 XFS_ILOG_AEXT | XFS_ILOG_ABROOT)

#define	XFS_ILOG_DFORK		(XFS_ILOG_DDATA | XFS_ILOG_DEXT | \
				 XFS_ILOG_DBROOT)

#define	XFS_ILOG_AFORK		(XFS_ILOG_ADATA | XFS_ILOG_AEXT | \
				 XFS_ILOG_ABROOT)

#define	XFS_ILOG_ALL		(XFS_ILOG_CORE | XFS_ILOG_DDATA | \
				 XFS_ILOG_DEXT | XFS_ILOG_DBROOT | \
				 XFS_ILOG_DEV | XFS_ILOG_UUID | \
				 XFS_ILOG_ADATA | XFS_ILOG_AEXT | \
				 XFS_ILOG_ABROOT | XFS_ILOG_TIMESTAMP)

static inline int xfs_ilog_fbroot(int w)
{
	return (w == XFS_DATA_FORK ? XFS_ILOG_DBROOT : XFS_ILOG_ABROOT);
}

static inline int xfs_ilog_fext(int w)
{
	return (w == XFS_DATA_FORK ? XFS_ILOG_DEXT : XFS_ILOG_AEXT);
}

static inline int xfs_ilog_fdata(int w)
{
	return (w == XFS_DATA_FORK ? XFS_ILOG_DDATA : XFS_ILOG_ADATA);
}

/*
 * Incore version of the on-disk inode core structures. We log this directly
 * into the journal in host CPU format (for better or worse) and as such
 * directly mirrors the xfs_dinode structure as it must contain all the same
 * information.
 */
typedef struct xfs_ictimestamp {
	__int32_t	t_sec;		/* timestamp seconds */
	__int32_t	t_nsec;		/* timestamp nanoseconds */
} xfs_ictimestamp_t;

/*
 * NOTE:  This structure must be kept identical to struct xfs_dinode
 *	  in xfs_dinode.h except for the endianness annotations.
 */
typedef struct xfs_icdinode {
	__uint16_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	__uint16_t	di_mode;	/* mode and type of file */
	__int8_t	di_version;	/* inode version */
	__int8_t	di_format;	/* format of di_c data */
	__uint16_t	di_onlink;	/* old number of links to file */
	__uint32_t	di_uid;		/* owner's user id */
	__uint32_t	di_gid;		/* owner's group id */
	__uint32_t	di_nlink;	/* number of links to file */
	__uint16_t	di_projid_lo;	/* lower part of owner's project id */
	__uint16_t	di_projid_hi;	/* higher part of owner's project id */
	__uint8_t	di_pad[6];	/* unused, zeroed space */
	__uint16_t	di_flushiter;	/* incremented on flush */
	xfs_ictimestamp_t di_atime;	/* time last accessed */
	xfs_ictimestamp_t di_mtime;	/* time last modified */
	xfs_ictimestamp_t di_ctime;	/* time created/inode modified */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_drfsbno_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	__uint8_t	di_forkoff;	/* attr fork offs, <<3 for 64b align */
	__int8_t	di_aformat;	/* format of attr fork's data */
	__uint32_t	di_dmevmask;	/* DMIG event mask */
	__uint16_t	di_dmstate;	/* DMIG state info */
	__uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */
	__uint32_t	di_gen;		/* generation number */

	/* di_next_unlinked is the only non-core field in the old dinode */
	xfs_agino_t	di_next_unlinked;/* agi unlinked list ptr */

	/* start of the extended dinode, writable fields */
	__uint32_t	di_crc;		/* CRC of the inode */
	__uint64_t	di_changecount;	/* number of attribute changes */
	xfs_lsn_t	di_lsn;		/* flush sequence */
	__uint64_t	di_flags2;	/* more random flags */
	__uint8_t	di_pad2[16];	/* more padding for future expansion */

	/* fields only written to during inode creation */
	xfs_ictimestamp_t di_crtime;	/* time created */
	xfs_ino_t	di_ino;		/* inode number */
	uuid_t		di_uuid;	/* UUID of the filesystem */

	/* structure must be padded to 64 bit alignment */
} xfs_icdinode_t;

static inline uint xfs_icdinode_size(int version)
{
	if (version == 3)
		return sizeof(struct xfs_icdinode);
	return offsetof(struct xfs_icdinode, di_next_unlinked);
}

/*
 * Buffer Log Format defintions
 *
 * These are the physical dirty bitmap defintions for the log format structure.
 */
#define	XFS_BLF_CHUNK		128
#define	XFS_BLF_SHIFT		7
#define	BIT_TO_WORD_SHIFT	5
#define	NBWORD			(NBBY * sizeof(unsigned int))

/*
 * This flag indicates that the buffer contains on disk inodes
 * and requires special recovery handling.
 */
#define	XFS_BLF_INODE_BUF	(1<<0)

/*
 * This flag indicates that the buffer should not be replayed
 * during recovery because its blocks are being freed.
 */
#define	XFS_BLF_CANCEL		(1<<1)

/*
 * This flag indicates that the buffer contains on disk
 * user or group dquots and may require special recovery handling.
 */
#define	XFS_BLF_UDQUOT_BUF	(1<<2)
#define XFS_BLF_PDQUOT_BUF	(1<<3)
#define	XFS_BLF_GDQUOT_BUF	(1<<4)

/*
 * This is the structure used to lay out a buf log item in the
 * log.  The data map describes which 128 byte chunks of the buffer
 * have been logged.
 */
#define XFS_BLF_DATAMAP_SIZE	((XFS_MAX_BLOCKSIZE / XFS_BLF_CHUNK) / NBWORD)

typedef struct xfs_buf_log_format {
	unsigned short	blf_type;	/* buf log item type indicator */
	unsigned short	blf_size;	/* size of this item */
	ushort		blf_flags;	/* misc state */
	ushort		blf_len;	/* number of blocks in this buf */
	__int64_t	blf_blkno;	/* starting blkno of this buf */
	unsigned int	blf_map_size;	/* used size of data bitmap in words */
	unsigned int	blf_data_map[XFS_BLF_DATAMAP_SIZE]; /* dirty bitmap */
} xfs_buf_log_format_t;

/*
 * All buffers now need to tell recovery where the magic number
 * is so that it can verify and calculate the CRCs on the buffer correctly
 * once the changes have been replayed into the buffer.
 *
 * The type value is held in the upper 5 bits of the blf_flags field, which is
 * an unsigned 16 bit field. Hence we need to shift it 11 bits up and down.
 */
#define XFS_BLFT_BITS	5
#define XFS_BLFT_SHIFT	11
#define XFS_BLFT_MASK	(((1 << XFS_BLFT_BITS) - 1) << XFS_BLFT_SHIFT)

enum xfs_blft {
	XFS_BLFT_UNKNOWN_BUF = 0,
	XFS_BLFT_UDQUOT_BUF,
	XFS_BLFT_PDQUOT_BUF,
	XFS_BLFT_GDQUOT_BUF,
	XFS_BLFT_BTREE_BUF,
	XFS_BLFT_AGF_BUF,
	XFS_BLFT_AGFL_BUF,
	XFS_BLFT_AGI_BUF,
	XFS_BLFT_DINO_BUF,
	XFS_BLFT_SYMLINK_BUF,
	XFS_BLFT_DIR_BLOCK_BUF,
	XFS_BLFT_DIR_DATA_BUF,
	XFS_BLFT_DIR_FREE_BUF,
	XFS_BLFT_DIR_LEAF1_BUF,
	XFS_BLFT_DIR_LEAFN_BUF,
	XFS_BLFT_DA_NODE_BUF,
	XFS_BLFT_ATTR_LEAF_BUF,
	XFS_BLFT_ATTR_RMT_BUF,
	XFS_BLFT_SB_BUF,
	XFS_BLFT_MAX_BUF = (1 << XFS_BLFT_BITS),
};

static inline void
xfs_blft_to_flags(struct xfs_buf_log_format *blf, enum xfs_blft type)
{
	ASSERT(type > XFS_BLFT_UNKNOWN_BUF && type < XFS_BLFT_MAX_BUF);
	blf->blf_flags &= ~XFS_BLFT_MASK;
	blf->blf_flags |= ((type << XFS_BLFT_SHIFT) & XFS_BLFT_MASK);
}

static inline __uint16_t
xfs_blft_from_flags(struct xfs_buf_log_format *blf)
{
	return (blf->blf_flags & XFS_BLFT_MASK) >> XFS_BLFT_SHIFT;
}

#endif /* __XFS_LOG_FORMAT_H__ */
