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

struct xfs_mount;
struct xfs_trans_res;

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
typedef uint32_t xlog_tid_t;

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

/* Minimum number of transactions that must fit in the log (defined by mkfs) */
#define XFS_MIN_LOG_FACTOR	3

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
#define XLOG_REG_TYPE_RUI_FORMAT	21
#define XLOG_REG_TYPE_RUD_FORMAT	22
#define XLOG_REG_TYPE_CUI_FORMAT	23
#define XLOG_REG_TYPE_CUD_FORMAT	24
#define XLOG_REG_TYPE_BUI_FORMAT	25
#define XLOG_REG_TYPE_BUD_FORMAT	26
#define XLOG_REG_TYPE_MAX		26

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
 * Transaction Header definitions.
 *
 * This is the structure written in the log at the head of every transaction. It
 * identifies the type and id of the transaction, and contains the number of
 * items logged by the transaction so we know how many to expect during
 * recovery.
 *
 * Do not change the below structure without redoing the code in
 * xlog_recover_add_to_trans() and xlog_recover_add_to_cont_trans().
 */
typedef struct xfs_trans_header {
	uint		th_magic;		/* magic number */
	uint		th_type;		/* transaction type */
	int32_t		th_tid;			/* transaction id (unused) */
	uint		th_num_items;		/* num items logged by trans */
} xfs_trans_header_t;

#define	XFS_TRANS_HEADER_MAGIC	0x5452414e	/* TRAN */

/*
 * The only type valid for th_type in CIL-enabled file system logs:
 */
#define XFS_TRANS_CHECKPOINT	40

/*
 * Log item types.
 */
#define	XFS_LI_EFI		0x1236
#define	XFS_LI_EFD		0x1237
#define	XFS_LI_IUNLINK		0x1238
#define	XFS_LI_INODE		0x123b	/* aligned ino chunks, var-size ibufs */
#define	XFS_LI_BUF		0x123c	/* v2 bufs, variable sized inode bufs */
#define	XFS_LI_DQUOT		0x123d
#define	XFS_LI_QUOTAOFF		0x123e
#define	XFS_LI_ICREATE		0x123f
#define	XFS_LI_RUI		0x1240	/* rmap update intent */
#define	XFS_LI_RUD		0x1241
#define	XFS_LI_CUI		0x1242	/* refcount update intent */
#define	XFS_LI_CUD		0x1243
#define	XFS_LI_BUI		0x1244	/* bmbt update intent */
#define	XFS_LI_BUD		0x1245

#define XFS_LI_TYPE_DESC \
	{ XFS_LI_EFI,		"XFS_LI_EFI" }, \
	{ XFS_LI_EFD,		"XFS_LI_EFD" }, \
	{ XFS_LI_IUNLINK,	"XFS_LI_IUNLINK" }, \
	{ XFS_LI_INODE,		"XFS_LI_INODE" }, \
	{ XFS_LI_BUF,		"XFS_LI_BUF" }, \
	{ XFS_LI_DQUOT,		"XFS_LI_DQUOT" }, \
	{ XFS_LI_QUOTAOFF,	"XFS_LI_QUOTAOFF" }, \
	{ XFS_LI_ICREATE,	"XFS_LI_ICREATE" }, \
	{ XFS_LI_RUI,		"XFS_LI_RUI" }, \
	{ XFS_LI_RUD,		"XFS_LI_RUD" }, \
	{ XFS_LI_CUI,		"XFS_LI_CUI" }, \
	{ XFS_LI_CUD,		"XFS_LI_CUD" }, \
	{ XFS_LI_BUI,		"XFS_LI_BUI" }, \
	{ XFS_LI_BUD,		"XFS_LI_BUD" }

/*
 * Inode Log Item Format definitions.
 *
 * This is the structure used to lay out an inode log item in the
 * log.  The size of the inline data/extents/b-tree root to be logged
 * (if any) is indicated in the ilf_dsize field.  Changes to this structure
 * must be added on to the end.
 */
typedef struct xfs_inode_log_format {
	uint16_t		ilf_type;	/* inode log item type */
	uint16_t		ilf_size;	/* size of this item */
	uint32_t		ilf_fields;	/* flags for fields logged */
	uint16_t		ilf_asize;	/* size of attr d/ext/root */
	uint16_t		ilf_dsize;	/* size of data/ext/root */
	uint64_t		ilf_ino;	/* inode number */
	union {
		uint32_t	ilfu_rdev;	/* rdev value for dev inode*/
		uuid_t		ilfu_uuid;	/* mount point value */
	} ilf_u;
	int64_t			ilf_blkno;	/* blkno of inode buffer */
	int32_t			ilf_len;	/* len of inode buffer */
	int32_t			ilf_boffset;	/* off of inode in buffer */
} xfs_inode_log_format_t;

typedef struct xfs_inode_log_format_32 {
	uint16_t		ilf_type;	/* inode log item type */
	uint16_t		ilf_size;	/* size of this item */
	uint32_t		ilf_fields;	/* flags for fields logged */
	uint16_t		ilf_asize;	/* size of attr d/ext/root */
	uint16_t		ilf_dsize;	/* size of data/ext/root */
	uint64_t		ilf_ino;	/* inode number */
	union {
		uint32_t	ilfu_rdev;	/* rdev value for dev inode*/
		uuid_t		ilfu_uuid;	/* mount point value */
	} ilf_u;
	int64_t			ilf_blkno;	/* blkno of inode buffer */
	int32_t			ilf_len;	/* len of inode buffer */
	int32_t			ilf_boffset;	/* off of inode in buffer */
} __attribute__((packed)) xfs_inode_log_format_32_t;

typedef struct xfs_inode_log_format_64 {
	uint16_t		ilf_type;	/* inode log item type */
	uint16_t		ilf_size;	/* size of this item */
	uint32_t		ilf_fields;	/* flags for fields logged */
	uint16_t		ilf_asize;	/* size of attr d/ext/root */
	uint16_t		ilf_dsize;	/* size of data/ext/root */
	uint32_t		ilf_pad;	/* pad for 64 bit boundary */
	uint64_t		ilf_ino;	/* inode number */
	union {
		uint32_t	ilfu_rdev;	/* rdev value for dev inode*/
		uuid_t		ilfu_uuid;	/* mount point value */
	} ilf_u;
	int64_t			ilf_blkno;	/* blkno of inode buffer */
	int32_t			ilf_len;	/* len of inode buffer */
	int32_t			ilf_boffset;	/* off of inode in buffer */
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
#define XFS_ILOG_DOWNER	0x200	/* change the data fork owner on replay */
#define XFS_ILOG_AOWNER	0x400	/* change the attr fork owner on replay */


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
				 XFS_ILOG_AEXT | XFS_ILOG_ABROOT | \
				 XFS_ILOG_DOWNER | XFS_ILOG_AOWNER)

#define	XFS_ILOG_DFORK		(XFS_ILOG_DDATA | XFS_ILOG_DEXT | \
				 XFS_ILOG_DBROOT)

#define	XFS_ILOG_AFORK		(XFS_ILOG_ADATA | XFS_ILOG_AEXT | \
				 XFS_ILOG_ABROOT)

#define	XFS_ILOG_ALL		(XFS_ILOG_CORE | XFS_ILOG_DDATA | \
				 XFS_ILOG_DEXT | XFS_ILOG_DBROOT | \
				 XFS_ILOG_DEV | XFS_ILOG_UUID | \
				 XFS_ILOG_ADATA | XFS_ILOG_AEXT | \
				 XFS_ILOG_ABROOT | XFS_ILOG_TIMESTAMP | \
				 XFS_ILOG_DOWNER | XFS_ILOG_AOWNER)

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
	int32_t		t_sec;		/* timestamp seconds */
	int32_t		t_nsec;		/* timestamp nanoseconds */
} xfs_ictimestamp_t;

/*
 * Define the format of the inode core that is logged. This structure must be
 * kept identical to struct xfs_dinode except for the endianness annotations.
 */
struct xfs_log_dinode {
	uint16_t	di_magic;	/* inode magic # = XFS_DINODE_MAGIC */
	uint16_t	di_mode;	/* mode and type of file */
	int8_t		di_version;	/* inode version */
	int8_t		di_format;	/* format of di_c data */
	uint8_t		di_pad3[2];	/* unused in v2/3 inodes */
	uint32_t	di_uid;		/* owner's user id */
	uint32_t	di_gid;		/* owner's group id */
	uint32_t	di_nlink;	/* number of links to file */
	uint16_t	di_projid_lo;	/* lower part of owner's project id */
	uint16_t	di_projid_hi;	/* higher part of owner's project id */
	uint8_t		di_pad[6];	/* unused, zeroed space */
	uint16_t	di_flushiter;	/* incremented on flush */
	xfs_ictimestamp_t di_atime;	/* time last accessed */
	xfs_ictimestamp_t di_mtime;	/* time last modified */
	xfs_ictimestamp_t di_ctime;	/* time created/inode modified */
	xfs_fsize_t	di_size;	/* number of bytes in file */
	xfs_rfsblock_t	di_nblocks;	/* # of direct & btree blocks used */
	xfs_extlen_t	di_extsize;	/* basic/minimum extent size for file */
	xfs_extnum_t	di_nextents;	/* number of extents in data fork */
	xfs_aextnum_t	di_anextents;	/* number of extents in attribute fork*/
	uint8_t		di_forkoff;	/* attr fork offs, <<3 for 64b align */
	int8_t		di_aformat;	/* format of attr fork's data */
	uint32_t	di_dmevmask;	/* DMIG event mask */
	uint16_t	di_dmstate;	/* DMIG state info */
	uint16_t	di_flags;	/* random flags, XFS_DIFLAG_... */
	uint32_t	di_gen;		/* generation number */

	/* di_next_unlinked is the only non-core field in the old dinode */
	xfs_agino_t	di_next_unlinked;/* agi unlinked list ptr */

	/* start of the extended dinode, writable fields */
	uint32_t	di_crc;		/* CRC of the inode */
	uint64_t	di_changecount;	/* number of attribute changes */
	xfs_lsn_t	di_lsn;		/* flush sequence */
	uint64_t	di_flags2;	/* more random flags */
	uint32_t	di_cowextsize;	/* basic cow extent size for file */
	uint8_t		di_pad2[12];	/* more padding for future expansion */

	/* fields only written to during inode creation */
	xfs_ictimestamp_t di_crtime;	/* time created */
	xfs_ino_t	di_ino;		/* inode number */
	uuid_t		di_uuid;	/* UUID of the filesystem */

	/* structure must be padded to 64 bit alignment */
};

static inline uint xfs_log_dinode_size(int version)
{
	if (version == 3)
		return sizeof(struct xfs_log_dinode);
	return offsetof(struct xfs_log_dinode, di_next_unlinked);
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
	unsigned short	blf_flags;	/* misc state */
	unsigned short	blf_len;	/* number of blocks in this buf */
	int64_t		blf_blkno;	/* starting blkno of this buf */
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
	XFS_BLFT_RTBITMAP_BUF,
	XFS_BLFT_RTSUMMARY_BUF,
	XFS_BLFT_MAX_BUF = (1 << XFS_BLFT_BITS),
};

static inline void
xfs_blft_to_flags(struct xfs_buf_log_format *blf, enum xfs_blft type)
{
	ASSERT(type > XFS_BLFT_UNKNOWN_BUF && type < XFS_BLFT_MAX_BUF);
	blf->blf_flags &= ~XFS_BLFT_MASK;
	blf->blf_flags |= ((type << XFS_BLFT_SHIFT) & XFS_BLFT_MASK);
}

static inline uint16_t
xfs_blft_from_flags(struct xfs_buf_log_format *blf)
{
	return (blf->blf_flags & XFS_BLFT_MASK) >> XFS_BLFT_SHIFT;
}

/*
 * EFI/EFD log format definitions
 */
typedef struct xfs_extent {
	xfs_fsblock_t	ext_start;
	xfs_extlen_t	ext_len;
} xfs_extent_t;

/*
 * Since an xfs_extent_t has types (start:64, len: 32)
 * there are different alignments on 32 bit and 64 bit kernels.
 * So we provide the different variants for use by a
 * conversion routine.
 */
typedef struct xfs_extent_32 {
	uint64_t	ext_start;
	uint32_t	ext_len;
} __attribute__((packed)) xfs_extent_32_t;

typedef struct xfs_extent_64 {
	uint64_t	ext_start;
	uint32_t	ext_len;
	uint32_t	ext_pad;
} xfs_extent_64_t;

/*
 * This is the structure used to lay out an efi log item in the
 * log.  The efi_extents field is a variable size array whose
 * size is given by efi_nextents.
 */
typedef struct xfs_efi_log_format {
	uint16_t		efi_type;	/* efi log item type */
	uint16_t		efi_size;	/* size of this item */
	uint32_t		efi_nextents;	/* # extents to free */
	uint64_t		efi_id;		/* efi identifier */
	xfs_extent_t		efi_extents[1];	/* array of extents to free */
} xfs_efi_log_format_t;

typedef struct xfs_efi_log_format_32 {
	uint16_t		efi_type;	/* efi log item type */
	uint16_t		efi_size;	/* size of this item */
	uint32_t		efi_nextents;	/* # extents to free */
	uint64_t		efi_id;		/* efi identifier */
	xfs_extent_32_t		efi_extents[1];	/* array of extents to free */
} __attribute__((packed)) xfs_efi_log_format_32_t;

typedef struct xfs_efi_log_format_64 {
	uint16_t		efi_type;	/* efi log item type */
	uint16_t		efi_size;	/* size of this item */
	uint32_t		efi_nextents;	/* # extents to free */
	uint64_t		efi_id;		/* efi identifier */
	xfs_extent_64_t		efi_extents[1];	/* array of extents to free */
} xfs_efi_log_format_64_t;

/*
 * This is the structure used to lay out an efd log item in the
 * log.  The efd_extents array is a variable size array whose
 * size is given by efd_nextents;
 */
typedef struct xfs_efd_log_format {
	uint16_t		efd_type;	/* efd log item type */
	uint16_t		efd_size;	/* size of this item */
	uint32_t		efd_nextents;	/* # of extents freed */
	uint64_t		efd_efi_id;	/* id of corresponding efi */
	xfs_extent_t		efd_extents[1];	/* array of extents freed */
} xfs_efd_log_format_t;

typedef struct xfs_efd_log_format_32 {
	uint16_t		efd_type;	/* efd log item type */
	uint16_t		efd_size;	/* size of this item */
	uint32_t		efd_nextents;	/* # of extents freed */
	uint64_t		efd_efi_id;	/* id of corresponding efi */
	xfs_extent_32_t		efd_extents[1];	/* array of extents freed */
} __attribute__((packed)) xfs_efd_log_format_32_t;

typedef struct xfs_efd_log_format_64 {
	uint16_t		efd_type;	/* efd log item type */
	uint16_t		efd_size;	/* size of this item */
	uint32_t		efd_nextents;	/* # of extents freed */
	uint64_t		efd_efi_id;	/* id of corresponding efi */
	xfs_extent_64_t		efd_extents[1];	/* array of extents freed */
} xfs_efd_log_format_64_t;

/*
 * RUI/RUD (reverse mapping) log format definitions
 */
struct xfs_map_extent {
	uint64_t		me_owner;
	uint64_t		me_startblock;
	uint64_t		me_startoff;
	uint32_t		me_len;
	uint32_t		me_flags;
};

/* rmap me_flags: upper bits are flags, lower byte is type code */
#define XFS_RMAP_EXTENT_MAP		1
#define XFS_RMAP_EXTENT_MAP_SHARED	2
#define XFS_RMAP_EXTENT_UNMAP		3
#define XFS_RMAP_EXTENT_UNMAP_SHARED	4
#define XFS_RMAP_EXTENT_CONVERT		5
#define XFS_RMAP_EXTENT_CONVERT_SHARED	6
#define XFS_RMAP_EXTENT_ALLOC		7
#define XFS_RMAP_EXTENT_FREE		8
#define XFS_RMAP_EXTENT_TYPE_MASK	0xFF

#define XFS_RMAP_EXTENT_ATTR_FORK	(1U << 31)
#define XFS_RMAP_EXTENT_BMBT_BLOCK	(1U << 30)
#define XFS_RMAP_EXTENT_UNWRITTEN	(1U << 29)

#define XFS_RMAP_EXTENT_FLAGS		(XFS_RMAP_EXTENT_TYPE_MASK | \
					 XFS_RMAP_EXTENT_ATTR_FORK | \
					 XFS_RMAP_EXTENT_BMBT_BLOCK | \
					 XFS_RMAP_EXTENT_UNWRITTEN)

/*
 * This is the structure used to lay out an rui log item in the
 * log.  The rui_extents field is a variable size array whose
 * size is given by rui_nextents.
 */
struct xfs_rui_log_format {
	uint16_t		rui_type;	/* rui log item type */
	uint16_t		rui_size;	/* size of this item */
	uint32_t		rui_nextents;	/* # extents to free */
	uint64_t		rui_id;		/* rui identifier */
	struct xfs_map_extent	rui_extents[];	/* array of extents to rmap */
};

static inline size_t
xfs_rui_log_format_sizeof(
	unsigned int		nr)
{
	return sizeof(struct xfs_rui_log_format) +
			nr * sizeof(struct xfs_map_extent);
}

/*
 * This is the structure used to lay out an rud log item in the
 * log.  The rud_extents array is a variable size array whose
 * size is given by rud_nextents;
 */
struct xfs_rud_log_format {
	uint16_t		rud_type;	/* rud log item type */
	uint16_t		rud_size;	/* size of this item */
	uint32_t		__pad;
	uint64_t		rud_rui_id;	/* id of corresponding rui */
};

/*
 * CUI/CUD (refcount update) log format definitions
 */
struct xfs_phys_extent {
	uint64_t		pe_startblock;
	uint32_t		pe_len;
	uint32_t		pe_flags;
};

/* refcount pe_flags: upper bits are flags, lower byte is type code */
/* Type codes are taken directly from enum xfs_refcount_intent_type. */
#define XFS_REFCOUNT_EXTENT_TYPE_MASK	0xFF

#define XFS_REFCOUNT_EXTENT_FLAGS	(XFS_REFCOUNT_EXTENT_TYPE_MASK)

/*
 * This is the structure used to lay out a cui log item in the
 * log.  The cui_extents field is a variable size array whose
 * size is given by cui_nextents.
 */
struct xfs_cui_log_format {
	uint16_t		cui_type;	/* cui log item type */
	uint16_t		cui_size;	/* size of this item */
	uint32_t		cui_nextents;	/* # extents to free */
	uint64_t		cui_id;		/* cui identifier */
	struct xfs_phys_extent	cui_extents[];	/* array of extents */
};

static inline size_t
xfs_cui_log_format_sizeof(
	unsigned int		nr)
{
	return sizeof(struct xfs_cui_log_format) +
			nr * sizeof(struct xfs_phys_extent);
}

/*
 * This is the structure used to lay out a cud log item in the
 * log.  The cud_extents array is a variable size array whose
 * size is given by cud_nextents;
 */
struct xfs_cud_log_format {
	uint16_t		cud_type;	/* cud log item type */
	uint16_t		cud_size;	/* size of this item */
	uint32_t		__pad;
	uint64_t		cud_cui_id;	/* id of corresponding cui */
};

/*
 * BUI/BUD (inode block mapping) log format definitions
 */

/* bmbt me_flags: upper bits are flags, lower byte is type code */
/* Type codes are taken directly from enum xfs_bmap_intent_type. */
#define XFS_BMAP_EXTENT_TYPE_MASK	0xFF

#define XFS_BMAP_EXTENT_ATTR_FORK	(1U << 31)
#define XFS_BMAP_EXTENT_UNWRITTEN	(1U << 30)

#define XFS_BMAP_EXTENT_FLAGS		(XFS_BMAP_EXTENT_TYPE_MASK | \
					 XFS_BMAP_EXTENT_ATTR_FORK | \
					 XFS_BMAP_EXTENT_UNWRITTEN)

/*
 * This is the structure used to lay out an bui log item in the
 * log.  The bui_extents field is a variable size array whose
 * size is given by bui_nextents.
 */
struct xfs_bui_log_format {
	uint16_t		bui_type;	/* bui log item type */
	uint16_t		bui_size;	/* size of this item */
	uint32_t		bui_nextents;	/* # extents to free */
	uint64_t		bui_id;		/* bui identifier */
	struct xfs_map_extent	bui_extents[];	/* array of extents to bmap */
};

static inline size_t
xfs_bui_log_format_sizeof(
	unsigned int		nr)
{
	return sizeof(struct xfs_bui_log_format) +
			nr * sizeof(struct xfs_map_extent);
}

/*
 * This is the structure used to lay out an bud log item in the
 * log.  The bud_extents array is a variable size array whose
 * size is given by bud_nextents;
 */
struct xfs_bud_log_format {
	uint16_t		bud_type;	/* bud log item type */
	uint16_t		bud_size;	/* size of this item */
	uint32_t		__pad;
	uint64_t		bud_bui_id;	/* id of corresponding bui */
};

/*
 * Dquot Log format definitions.
 *
 * The first two fields must be the type and size fitting into
 * 32 bits : log_recovery code assumes that.
 */
typedef struct xfs_dq_logformat {
	uint16_t		qlf_type;      /* dquot log item type */
	uint16_t		qlf_size;      /* size of this item */
	xfs_dqid_t		qlf_id;	       /* usr/grp/proj id : 32 bits */
	int64_t			qlf_blkno;     /* blkno of dquot buffer */
	int32_t			qlf_len;       /* len of dquot buffer */
	uint32_t		qlf_boffset;   /* off of dquot in buffer */
} xfs_dq_logformat_t;

/*
 * log format struct for QUOTAOFF records.
 * The first two fields must be the type and size fitting into
 * 32 bits : log_recovery code assumes that.
 * We write two LI_QUOTAOFF logitems per quotaoff, the last one keeps a pointer
 * to the first and ensures that the first logitem is taken out of the AIL
 * only when the last one is securely committed.
 */
typedef struct xfs_qoff_logformat {
	unsigned short		qf_type;	/* quotaoff log item type */
	unsigned short		qf_size;	/* size of this item */
	unsigned int		qf_flags;	/* USR and/or GRP */
	char			qf_pad[12];	/* padding for future */
} xfs_qoff_logformat_t;

/*
 * Disk quotas status in m_qflags, and also sb_qflags. 16 bits.
 */
#define XFS_UQUOTA_ACCT	0x0001  /* user quota accounting ON */
#define XFS_UQUOTA_ENFD	0x0002  /* user quota limits enforced */
#define XFS_UQUOTA_CHKD	0x0004  /* quotacheck run on usr quotas */
#define XFS_PQUOTA_ACCT	0x0008  /* project quota accounting ON */
#define XFS_OQUOTA_ENFD	0x0010  /* other (grp/prj) quota limits enforced */
#define XFS_OQUOTA_CHKD	0x0020  /* quotacheck run on other (grp/prj) quotas */
#define XFS_GQUOTA_ACCT	0x0040  /* group quota accounting ON */

/*
 * Conversion to and from the combined OQUOTA flag (if necessary)
 * is done only in xfs_sb_qflags_to_disk() and xfs_sb_qflags_from_disk()
 */
#define XFS_GQUOTA_ENFD	0x0080  /* group quota limits enforced */
#define XFS_GQUOTA_CHKD	0x0100  /* quotacheck run on group quotas */
#define XFS_PQUOTA_ENFD	0x0200  /* project quota limits enforced */
#define XFS_PQUOTA_CHKD	0x0400  /* quotacheck run on project quotas */

#define XFS_ALL_QUOTA_ACCT	\
		(XFS_UQUOTA_ACCT | XFS_GQUOTA_ACCT | XFS_PQUOTA_ACCT)
#define XFS_ALL_QUOTA_ENFD	\
		(XFS_UQUOTA_ENFD | XFS_GQUOTA_ENFD | XFS_PQUOTA_ENFD)
#define XFS_ALL_QUOTA_CHKD	\
		(XFS_UQUOTA_CHKD | XFS_GQUOTA_CHKD | XFS_PQUOTA_CHKD)

#define XFS_MOUNT_QUOTA_ALL	(XFS_UQUOTA_ACCT|XFS_UQUOTA_ENFD|\
				 XFS_UQUOTA_CHKD|XFS_GQUOTA_ACCT|\
				 XFS_GQUOTA_ENFD|XFS_GQUOTA_CHKD|\
				 XFS_PQUOTA_ACCT|XFS_PQUOTA_ENFD|\
				 XFS_PQUOTA_CHKD)

/*
 * Inode create log item structure
 *
 * Log recovery assumes the first two entries are the type and size and they fit
 * in 32 bits. Also in host order (ugh) so they have to be 32 bit aligned so
 * decoding can be done correctly.
 */
struct xfs_icreate_log {
	uint16_t	icl_type;	/* type of log format structure */
	uint16_t	icl_size;	/* size of log format structure */
	__be32		icl_ag;		/* ag being allocated in */
	__be32		icl_agbno;	/* start block of inode range */
	__be32		icl_count;	/* number of inodes to initialise */
	__be32		icl_isize;	/* size of inodes */
	__be32		icl_length;	/* length of extent to initialise */
	__be32		icl_gen;	/* inode generation number to use */
};

#endif /* __XFS_LOG_FORMAT_H__ */
