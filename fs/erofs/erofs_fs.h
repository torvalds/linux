/* SPDX-License-Identifier: GPL-2.0-only OR Apache-2.0 */
/*
 * EROFS (Enhanced ROM File System) on-disk format definition
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#ifndef __EROFS_FS_H
#define __EROFS_FS_H

#define EROFS_SUPER_OFFSET      1024

#define EROFS_FEATURE_COMPAT_SB_CHKSUM          0x00000001
#define EROFS_FEATURE_COMPAT_MTIME              0x00000002
#define EROFS_FEATURE_COMPAT_XATTR_FILTER	0x00000004

/*
 * Any bits that aren't in EROFS_ALL_FEATURE_INCOMPAT should
 * be incompatible with this kernel version.
 */
#define EROFS_FEATURE_INCOMPAT_ZERO_PADDING	0x00000001
#define EROFS_FEATURE_INCOMPAT_COMPR_CFGS	0x00000002
#define EROFS_FEATURE_INCOMPAT_BIG_PCLUSTER	0x00000002
#define EROFS_FEATURE_INCOMPAT_CHUNKED_FILE	0x00000004
#define EROFS_FEATURE_INCOMPAT_DEVICE_TABLE	0x00000008
#define EROFS_FEATURE_INCOMPAT_COMPR_HEAD2	0x00000008
#define EROFS_FEATURE_INCOMPAT_ZTAILPACKING	0x00000010
#define EROFS_FEATURE_INCOMPAT_FRAGMENTS	0x00000020
#define EROFS_FEATURE_INCOMPAT_DEDUPE		0x00000020
#define EROFS_FEATURE_INCOMPAT_XATTR_PREFIXES	0x00000040
#define EROFS_ALL_FEATURE_INCOMPAT		\
	(EROFS_FEATURE_INCOMPAT_ZERO_PADDING | \
	 EROFS_FEATURE_INCOMPAT_COMPR_CFGS | \
	 EROFS_FEATURE_INCOMPAT_BIG_PCLUSTER | \
	 EROFS_FEATURE_INCOMPAT_CHUNKED_FILE | \
	 EROFS_FEATURE_INCOMPAT_DEVICE_TABLE | \
	 EROFS_FEATURE_INCOMPAT_COMPR_HEAD2 | \
	 EROFS_FEATURE_INCOMPAT_ZTAILPACKING | \
	 EROFS_FEATURE_INCOMPAT_FRAGMENTS | \
	 EROFS_FEATURE_INCOMPAT_DEDUPE | \
	 EROFS_FEATURE_INCOMPAT_XATTR_PREFIXES)

#define EROFS_SB_EXTSLOT_SIZE	16

struct erofs_deviceslot {
	u8 tag[64];		/* digest(sha256), etc. */
	__le32 blocks;		/* total fs blocks of this device */
	__le32 mapped_blkaddr;	/* map starting at mapped_blkaddr */
	u8 reserved[56];
};
#define EROFS_DEVT_SLOT_SIZE	sizeof(struct erofs_deviceslot)

/* erofs on-disk super block (currently 128 bytes) */
struct erofs_super_block {
	__le32 magic;           /* file system magic number */
	__le32 checksum;        /* crc32c(super_block) */
	__le32 feature_compat;
	__u8 blkszbits;         /* filesystem block size in bit shift */
	__u8 sb_extslots;	/* superblock size = 128 + sb_extslots * 16 */

	__le16 root_nid;	/* nid of root directory */
	__le64 inos;            /* total valid ino # (== f_files - f_favail) */

	__le64 build_time;      /* compact inode time derivation */
	__le32 build_time_nsec;	/* compact inode time derivation in ns scale */
	__le32 blocks;          /* used for statfs */
	__le32 meta_blkaddr;	/* start block address of metadata area */
	__le32 xattr_blkaddr;	/* start block address of shared xattr area */
	__u8 uuid[16];          /* 128-bit uuid for volume */
	__u8 volume_name[16];   /* volume name */
	__le32 feature_incompat;
	union {
		/* bitmap for available compression algorithms */
		__le16 available_compr_algs;
		/* customized sliding window size instead of 64k by default */
		__le16 lz4_max_distance;
	} __packed u1;
	__le16 extra_devices;	/* # of devices besides the primary device */
	__le16 devt_slotoff;	/* startoff = devt_slotoff * devt_slotsize */
	__u8 dirblkbits;	/* directory block size in bit shift */
	__u8 xattr_prefix_count;	/* # of long xattr name prefixes */
	__le32 xattr_prefix_start;	/* start of long xattr prefixes */
	__le64 packed_nid;	/* nid of the special packed inode */
	__u8 xattr_filter_reserved; /* reserved for xattr name filter */
	__u8 reserved2[23];
};

/*
 * EROFS inode datalayout (i_format in on-disk inode):
 * 0 - uncompressed flat inode without tail-packing inline data:
 * 1 - compressed inode with non-compact indexes:
 * 2 - uncompressed flat inode with tail-packing inline data:
 * 3 - compressed inode with compact indexes:
 * 4 - chunk-based inode with (optional) multi-device support:
 * 5~7 - reserved
 */
enum {
	EROFS_INODE_FLAT_PLAIN			= 0,
	EROFS_INODE_COMPRESSED_FULL		= 1,
	EROFS_INODE_FLAT_INLINE			= 2,
	EROFS_INODE_COMPRESSED_COMPACT		= 3,
	EROFS_INODE_CHUNK_BASED			= 4,
	EROFS_INODE_DATALAYOUT_MAX
};

static inline bool erofs_inode_is_data_compressed(unsigned int datamode)
{
	return datamode == EROFS_INODE_COMPRESSED_COMPACT ||
		datamode == EROFS_INODE_COMPRESSED_FULL;
}

/* bit definitions of inode i_format */
#define EROFS_I_VERSION_MASK            0x01
#define EROFS_I_DATALAYOUT_MASK         0x07

#define EROFS_I_VERSION_BIT             0
#define EROFS_I_DATALAYOUT_BIT          1
#define EROFS_I_ALL_BIT			4

#define EROFS_I_ALL	((1 << EROFS_I_ALL_BIT) - 1)

/* indicate chunk blkbits, thus 'chunksize = blocksize << chunk blkbits' */
#define EROFS_CHUNK_FORMAT_BLKBITS_MASK		0x001F
/* with chunk indexes or just a 4-byte blkaddr array */
#define EROFS_CHUNK_FORMAT_INDEXES		0x0020

#define EROFS_CHUNK_FORMAT_ALL	\
	(EROFS_CHUNK_FORMAT_BLKBITS_MASK | EROFS_CHUNK_FORMAT_INDEXES)

/* 32-byte on-disk inode */
#define EROFS_INODE_LAYOUT_COMPACT	0
/* 64-byte on-disk inode */
#define EROFS_INODE_LAYOUT_EXTENDED	1

struct erofs_inode_chunk_info {
	__le16 format;		/* chunk blkbits, etc. */
	__le16 reserved;
};

union erofs_inode_i_u {
	/* total compressed blocks for compressed inodes */
	__le32 compressed_blocks;

	/* block address for uncompressed flat inodes */
	__le32 raw_blkaddr;

	/* for device files, used to indicate old/new device # */
	__le32 rdev;

	/* for chunk-based files, it contains the summary info */
	struct erofs_inode_chunk_info c;
};

/* 32-byte reduced form of an ondisk inode */
struct erofs_inode_compact {
	__le16 i_format;	/* inode format hints */

/* 1 header + n-1 * 4 bytes inline xattr to keep continuity */
	__le16 i_xattr_icount;
	__le16 i_mode;
	__le16 i_nlink;
	__le32 i_size;
	__le32 i_reserved;
	union erofs_inode_i_u i_u;

	__le32 i_ino;		/* only used for 32-bit stat compatibility */
	__le16 i_uid;
	__le16 i_gid;
	__le32 i_reserved2;
};

/* 64-byte complete form of an ondisk inode */
struct erofs_inode_extended {
	__le16 i_format;	/* inode format hints */

/* 1 header + n-1 * 4 bytes inline xattr to keep continuity */
	__le16 i_xattr_icount;
	__le16 i_mode;
	__le16 i_reserved;
	__le64 i_size;
	union erofs_inode_i_u i_u;

	__le32 i_ino;		/* only used for 32-bit stat compatibility */
	__le32 i_uid;
	__le32 i_gid;
	__le64 i_mtime;
	__le32 i_mtime_nsec;
	__le32 i_nlink;
	__u8   i_reserved2[16];
};

/*
 * inline xattrs (n == i_xattr_icount):
 * erofs_xattr_ibody_header(1) + (n - 1) * 4 bytes
 *          12 bytes           /                   \
 *                            /                     \
 *                           /-----------------------\
 *                           |  erofs_xattr_entries+ |
 *                           +-----------------------+
 * inline xattrs must starts in erofs_xattr_ibody_header,
 * for read-only fs, no need to introduce h_refcount
 */
struct erofs_xattr_ibody_header {
	__le32 h_name_filter;		/* bit value 1 indicates not-present */
	__u8   h_shared_count;
	__u8   h_reserved2[7];
	__le32 h_shared_xattrs[];       /* shared xattr id array */
};

/* Name indexes */
#define EROFS_XATTR_INDEX_USER              1
#define EROFS_XATTR_INDEX_POSIX_ACL_ACCESS  2
#define EROFS_XATTR_INDEX_POSIX_ACL_DEFAULT 3
#define EROFS_XATTR_INDEX_TRUSTED           4
#define EROFS_XATTR_INDEX_LUSTRE            5
#define EROFS_XATTR_INDEX_SECURITY          6

/*
 * bit 7 of e_name_index is set when it refers to a long xattr name prefix,
 * while the remained lower bits represent the index of the prefix.
 */
#define EROFS_XATTR_LONG_PREFIX		0x80
#define EROFS_XATTR_LONG_PREFIX_MASK	0x7f

#define EROFS_XATTR_FILTER_BITS		32
#define EROFS_XATTR_FILTER_DEFAULT	UINT32_MAX
#define EROFS_XATTR_FILTER_SEED		0x25BBE08F

/* xattr entry (for both inline & shared xattrs) */
struct erofs_xattr_entry {
	__u8   e_name_len;      /* length of name */
	__u8   e_name_index;    /* attribute name index */
	__le16 e_value_size;    /* size of attribute value */
	/* followed by e_name and e_value */
	char   e_name[];        /* attribute name */
};

/* long xattr name prefix */
struct erofs_xattr_long_prefix {
	__u8   base_index;	/* short xattr name prefix index */
	char   infix[];		/* infix apart from short prefix */
};

static inline unsigned int erofs_xattr_ibody_size(__le16 i_xattr_icount)
{
	if (!i_xattr_icount)
		return 0;

	return sizeof(struct erofs_xattr_ibody_header) +
		sizeof(__u32) * (le16_to_cpu(i_xattr_icount) - 1);
}

#define EROFS_XATTR_ALIGN(size) round_up(size, sizeof(struct erofs_xattr_entry))

static inline unsigned int erofs_xattr_entry_size(struct erofs_xattr_entry *e)
{
	return EROFS_XATTR_ALIGN(sizeof(struct erofs_xattr_entry) +
				 e->e_name_len + le16_to_cpu(e->e_value_size));
}

/* represent a zeroed chunk (hole) */
#define EROFS_NULL_ADDR			-1

/* 4-byte block address array */
#define EROFS_BLOCK_MAP_ENTRY_SIZE	sizeof(__le32)

/* 8-byte inode chunk indexes */
struct erofs_inode_chunk_index {
	__le16 advise;		/* always 0, don't care for now */
	__le16 device_id;	/* back-end storage id (with bits masked) */
	__le32 blkaddr;		/* start block address of this inode chunk */
};

/* dirent sorts in alphabet order, thus we can do binary search */
struct erofs_dirent {
	__le64 nid;     /* node number */
	__le16 nameoff; /* start offset of file name */
	__u8 file_type; /* file type */
	__u8 reserved;  /* reserved */
} __packed;

/*
 * EROFS file types should match generic FT_* types and
 * it seems no need to add BUILD_BUG_ONs since potential
 * unmatchness will break other fses as well...
 */

#define EROFS_NAME_LEN      255

/* maximum supported encoded size of a physical compressed cluster */
#define Z_EROFS_PCLUSTER_MAX_SIZE	(1024 * 1024)

/* maximum supported decoded size of a physical compressed cluster */
#define Z_EROFS_PCLUSTER_MAX_DSIZE	(12 * 1024 * 1024)

/* available compression algorithm types (for h_algorithmtype) */
enum {
	Z_EROFS_COMPRESSION_LZ4		= 0,
	Z_EROFS_COMPRESSION_LZMA	= 1,
	Z_EROFS_COMPRESSION_DEFLATE	= 2,
	Z_EROFS_COMPRESSION_ZSTD	= 3,
	Z_EROFS_COMPRESSION_MAX
};
#define Z_EROFS_ALL_COMPR_ALGS		((1 << Z_EROFS_COMPRESSION_MAX) - 1)

/* 14 bytes (+ length field = 16 bytes) */
struct z_erofs_lz4_cfgs {
	__le16 max_distance;
	__le16 max_pclusterblks;
	u8 reserved[10];
} __packed;

/* 14 bytes (+ length field = 16 bytes) */
struct z_erofs_lzma_cfgs {
	__le32 dict_size;
	__le16 format;
	u8 reserved[8];
} __packed;

#define Z_EROFS_LZMA_MAX_DICT_SIZE	(8 * Z_EROFS_PCLUSTER_MAX_SIZE)

/* 6 bytes (+ length field = 8 bytes) */
struct z_erofs_deflate_cfgs {
	u8 windowbits;			/* 8..15 for DEFLATE */
	u8 reserved[5];
} __packed;

/* 6 bytes (+ length field = 8 bytes) */
struct z_erofs_zstd_cfgs {
	u8 format;
	u8 windowlog;           /* windowLog - ZSTD_WINDOWLOG_ABSOLUTEMIN(10) */
	u8 reserved[4];
} __packed;

#define Z_EROFS_ZSTD_MAX_DICT_SIZE      Z_EROFS_PCLUSTER_MAX_SIZE

/*
 * bit 0 : COMPACTED_2B indexes (0 - off; 1 - on)
 *  e.g. for 4k logical cluster size,      4B        if compacted 2B is off;
 *                                  (4B) + 2B + (4B) if compacted 2B is on.
 * bit 1 : HEAD1 big pcluster (0 - off; 1 - on)
 * bit 2 : HEAD2 big pcluster (0 - off; 1 - on)
 * bit 3 : tailpacking inline pcluster (0 - off; 1 - on)
 * bit 4 : interlaced plain pcluster (0 - off; 1 - on)
 * bit 5 : fragment pcluster (0 - off; 1 - on)
 */
#define Z_EROFS_ADVISE_COMPACTED_2B		0x0001
#define Z_EROFS_ADVISE_BIG_PCLUSTER_1		0x0002
#define Z_EROFS_ADVISE_BIG_PCLUSTER_2		0x0004
#define Z_EROFS_ADVISE_INLINE_PCLUSTER		0x0008
#define Z_EROFS_ADVISE_INTERLACED_PCLUSTER	0x0010
#define Z_EROFS_ADVISE_FRAGMENT_PCLUSTER	0x0020

#define Z_EROFS_FRAGMENT_INODE_BIT              7
struct z_erofs_map_header {
	union {
		/* fragment data offset in the packed inode */
		__le32  h_fragmentoff;
		struct {
			__le16  h_reserved1;
			/* indicates the encoded size of tailpacking data */
			__le16  h_idata_size;
		};
	};
	__le16	h_advise;
	/*
	 * bit 0-3 : algorithm type of head 1 (logical cluster type 01);
	 * bit 4-7 : algorithm type of head 2 (logical cluster type 11).
	 */
	__u8	h_algorithmtype;
	/*
	 * bit 0-2 : logical cluster bits - 12, e.g. 0 for 4096;
	 * bit 3-6 : reserved;
	 * bit 7   : move the whole file into packed inode or not.
	 */
	__u8	h_clusterbits;
};

/*
 * On-disk logical cluster type:
 *    0   - literal (uncompressed) lcluster
 *    1,3 - compressed lcluster (for HEAD lclusters)
 *    2   - compressed lcluster (for NONHEAD lclusters)
 *
 * In detail,
 *    0 - literal (uncompressed) lcluster,
 *        di_advise = 0
 *        di_clusterofs = the literal data offset of the lcluster
 *        di_blkaddr = the blkaddr of the literal pcluster
 *
 *    1,3 - compressed lcluster (for HEAD lclusters)
 *        di_advise = 1 or 3
 *        di_clusterofs = the decompressed data offset of the lcluster
 *        di_blkaddr = the blkaddr of the compressed pcluster
 *
 *    2 - compressed lcluster (for NONHEAD lclusters)
 *        di_advise = 2
 *        di_clusterofs =
 *           the decompressed data offset in its own HEAD lcluster
 *        di_u.delta[0] = distance to this HEAD lcluster
 *        di_u.delta[1] = distance to the next HEAD lcluster
 */
enum {
	Z_EROFS_LCLUSTER_TYPE_PLAIN	= 0,
	Z_EROFS_LCLUSTER_TYPE_HEAD1	= 1,
	Z_EROFS_LCLUSTER_TYPE_NONHEAD	= 2,
	Z_EROFS_LCLUSTER_TYPE_HEAD2	= 3,
	Z_EROFS_LCLUSTER_TYPE_MAX
};

#define Z_EROFS_LI_LCLUSTER_TYPE_MASK	(Z_EROFS_LCLUSTER_TYPE_MAX - 1)

/* (noncompact only, HEAD) This pcluster refers to partial decompressed data */
#define Z_EROFS_LI_PARTIAL_REF		(1 << 15)

/*
 * D0_CBLKCNT will be marked _only_ at the 1st non-head lcluster to store the
 * compressed block count of a compressed extent (in logical clusters, aka.
 * block count of a pcluster).
 */
#define Z_EROFS_LI_D0_CBLKCNT		(1 << 11)

struct z_erofs_lcluster_index {
	__le16 di_advise;
	/* where to decompress in the head lcluster */
	__le16 di_clusterofs;

	union {
		/* for the HEAD lclusters */
		__le32 blkaddr;
		/*
		 * for the NONHEAD lclusters
		 * [0] - distance to its HEAD lcluster
		 * [1] - distance to the next HEAD lcluster
		 */
		__le16 delta[2];
	} di_u;
};

#define Z_EROFS_FULL_INDEX_ALIGN(end)	\
	(ALIGN(end, 8) + sizeof(struct z_erofs_map_header) + 8)

/* check the EROFS on-disk layout strictly at compile time */
static inline void erofs_check_ondisk_layout_definitions(void)
{
	const __le64 fmh = *(__le64 *)&(struct z_erofs_map_header) {
		.h_clusterbits = 1 << Z_EROFS_FRAGMENT_INODE_BIT
	};

	BUILD_BUG_ON(sizeof(struct erofs_super_block) != 128);
	BUILD_BUG_ON(sizeof(struct erofs_inode_compact) != 32);
	BUILD_BUG_ON(sizeof(struct erofs_inode_extended) != 64);
	BUILD_BUG_ON(sizeof(struct erofs_xattr_ibody_header) != 12);
	BUILD_BUG_ON(sizeof(struct erofs_xattr_entry) != 4);
	BUILD_BUG_ON(sizeof(struct erofs_inode_chunk_info) != 4);
	BUILD_BUG_ON(sizeof(struct erofs_inode_chunk_index) != 8);
	BUILD_BUG_ON(sizeof(struct z_erofs_map_header) != 8);
	BUILD_BUG_ON(sizeof(struct z_erofs_lcluster_index) != 8);
	BUILD_BUG_ON(sizeof(struct erofs_dirent) != 12);
	/* keep in sync between 2 index structures for better extendibility */
	BUILD_BUG_ON(sizeof(struct erofs_inode_chunk_index) !=
		     sizeof(struct z_erofs_lcluster_index));
	BUILD_BUG_ON(sizeof(struct erofs_deviceslot) != 128);

	/* exclude old compiler versions like gcc 7.5.0 */
	BUILD_BUG_ON(__builtin_constant_p(fmh) ?
		     fmh != cpu_to_le64(1ULL << 63) : 0);
}

#endif
