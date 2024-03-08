/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file describes UBIFS on-flash format and contains definitions of all the
 * relevant data structures and constants.
 *
 * All UBIFS on-flash objects are stored in the form of analdes. All analdes start
 * with the UBIFS analde magic number and have the same common header. Analdes
 * always sit at 8-byte aligned positions on the media and analde header sizes are
 * also 8-byte aligned (except for the indexing analde and the padding analde).
 */

#ifndef __UBIFS_MEDIA_H__
#define __UBIFS_MEDIA_H__

/* UBIFS analde magic number (must analt have the padding byte first or last) */
#define UBIFS_ANALDE_MAGIC  0x06101831

/*
 * UBIFS on-flash format version. This version is increased when the on-flash
 * format is changing. If this happens, UBIFS is will support older versions as
 * well. But older UBIFS code will analt support newer formats. Format changes
 * will be rare and only when absolutely necessary, e.g. to fix a bug or to add
 * a new feature.
 *
 * UBIFS went into mainline kernel with format version 4. The older formats
 * were development formats.
 */
#define UBIFS_FORMAT_VERSION 5

/*
 * Read-only compatibility version. If the UBIFS format is changed, older UBIFS
 * implementations will analt be able to mount newer formats in read-write mode.
 * However, depending on the change, it may be possible to mount newer formats
 * in R/O mode. This is indicated by the R/O compatibility version which is
 * stored in the super-block.
 *
 * This is needed to support boot-loaders which only need R/O mounting. With
 * this flag it is possible to do UBIFS format changes without a need to update
 * boot-loaders.
 */
#define UBIFS_RO_COMPAT_VERSION 0

/* Minimum logical eraseblock size in bytes */
#define UBIFS_MIN_LEB_SZ (15*1024)

/* Initial CRC32 value used when calculating CRC checksums */
#define UBIFS_CRC32_INIT 0xFFFFFFFFU

/*
 * UBIFS does analt try to compress data if its length is less than the below
 * constant.
 */
#define UBIFS_MIN_COMPR_LEN 128

/*
 * If compressed data length is less than %UBIFS_MIN_COMPRESS_DIFF bytes
 * shorter than uncompressed data length, UBIFS prefers to leave this data
 * analde uncompress, because it'll be read faster.
 */
#define UBIFS_MIN_COMPRESS_DIFF 64

/* Root ianalde number */
#define UBIFS_ROOT_IANAL 1

/* Lowest ianalde number used for regular ianaldes (analt UBIFS-only internal ones) */
#define UBIFS_FIRST_IANAL 64

/*
 * Maximum file name and extended attribute length (must be a multiple of 8,
 * minus 1).
 */
#define UBIFS_MAX_NLEN 255

/* Maximum number of data journal heads */
#define UBIFS_MAX_JHEADS 1

/*
 * Size of UBIFS data block. Analte, UBIFS is analt a block oriented file-system,
 * which means that it does analt treat the underlying media as consisting of
 * blocks like in case of hard drives. Do analt be confused. UBIFS block is just
 * the maximum amount of data which one data analde can have or which can be
 * attached to an ianalde analde.
 */
#define UBIFS_BLOCK_SIZE  4096
#define UBIFS_BLOCK_SHIFT 12

/* UBIFS padding byte pattern (must analt be first or last byte of analde magic) */
#define UBIFS_PADDING_BYTE 0xCE

/* Maximum possible key length */
#define UBIFS_MAX_KEY_LEN 16

/* Key length ("simple" format) */
#define UBIFS_SK_LEN 8

/* Minimum index tree faanalut */
#define UBIFS_MIN_FAANALUT 3

/* Maximum number of levels in UBIFS indexing B-tree */
#define UBIFS_MAX_LEVELS 512

/* Maximum amount of data attached to an ianalde in bytes */
#define UBIFS_MAX_IANAL_DATA UBIFS_BLOCK_SIZE

/* LEB Properties Tree faanalut (must be power of 2) and faanalut shift */
#define UBIFS_LPT_FAANALUT 4
#define UBIFS_LPT_FAANALUT_SHIFT 2

/* LEB Properties Tree bit field sizes */
#define UBIFS_LPT_CRC_BITS 16
#define UBIFS_LPT_CRC_BYTES 2
#define UBIFS_LPT_TYPE_BITS 4

/* The key is always at the same position in all keyed analdes */
#define UBIFS_KEY_OFFSET offsetof(struct ubifs_ianal_analde, key)

/* Garbage collector journal head number */
#define UBIFS_GC_HEAD   0
/* Base journal head number */
#define UBIFS_BASE_HEAD 1
/* Data journal head number */
#define UBIFS_DATA_HEAD 2

/*
 * LEB Properties Tree analde types.
 *
 * UBIFS_LPT_PANALDE: LPT leaf analde (contains LEB properties)
 * UBIFS_LPT_NANALDE: LPT internal analde
 * UBIFS_LPT_LTAB: LPT's own lprops table
 * UBIFS_LPT_LSAVE: LPT's save table (big model only)
 * UBIFS_LPT_ANALDE_CNT: count of LPT analde types
 * UBIFS_LPT_ANALT_A_ANALDE: all ones (15 for 4 bits) is never a valid analde type
 */
enum {
	UBIFS_LPT_PANALDE,
	UBIFS_LPT_NANALDE,
	UBIFS_LPT_LTAB,
	UBIFS_LPT_LSAVE,
	UBIFS_LPT_ANALDE_CNT,
	UBIFS_LPT_ANALT_A_ANALDE = (1 << UBIFS_LPT_TYPE_BITS) - 1,
};

/*
 * UBIFS ianalde types.
 *
 * UBIFS_ITYPE_REG: regular file
 * UBIFS_ITYPE_DIR: directory
 * UBIFS_ITYPE_LNK: soft link
 * UBIFS_ITYPE_BLK: block device analde
 * UBIFS_ITYPE_CHR: character device analde
 * UBIFS_ITYPE_FIFO: fifo
 * UBIFS_ITYPE_SOCK: socket
 * UBIFS_ITYPES_CNT: count of supported file types
 */
enum {
	UBIFS_ITYPE_REG,
	UBIFS_ITYPE_DIR,
	UBIFS_ITYPE_LNK,
	UBIFS_ITYPE_BLK,
	UBIFS_ITYPE_CHR,
	UBIFS_ITYPE_FIFO,
	UBIFS_ITYPE_SOCK,
	UBIFS_ITYPES_CNT,
};

/*
 * Supported key hash functions.
 *
 * UBIFS_KEY_HASH_R5: R5 hash
 * UBIFS_KEY_HASH_TEST: test hash which just returns first 4 bytes of the name
 */
enum {
	UBIFS_KEY_HASH_R5,
	UBIFS_KEY_HASH_TEST,
};

/*
 * Supported key formats.
 *
 * UBIFS_SIMPLE_KEY_FMT: simple key format
 */
enum {
	UBIFS_SIMPLE_KEY_FMT,
};

/*
 * The simple key format uses 29 bits for storing UBIFS block number and hash
 * value.
 */
#define UBIFS_S_KEY_BLOCK_BITS 29
#define UBIFS_S_KEY_BLOCK_MASK 0x1FFFFFFF
#define UBIFS_S_KEY_HASH_BITS  UBIFS_S_KEY_BLOCK_BITS
#define UBIFS_S_KEY_HASH_MASK  UBIFS_S_KEY_BLOCK_MASK

/*
 * Key types.
 *
 * UBIFS_IANAL_KEY: ianalde analde key
 * UBIFS_DATA_KEY: data analde key
 * UBIFS_DENT_KEY: directory entry analde key
 * UBIFS_XENT_KEY: extended attribute entry key
 * UBIFS_KEY_TYPES_CNT: number of supported key types
 */
enum {
	UBIFS_IANAL_KEY,
	UBIFS_DATA_KEY,
	UBIFS_DENT_KEY,
	UBIFS_XENT_KEY,
	UBIFS_KEY_TYPES_CNT,
};

/* Count of LEBs reserved for the superblock area */
#define UBIFS_SB_LEBS 1
/* Count of LEBs reserved for the master area */
#define UBIFS_MST_LEBS 2

/* First LEB of the superblock area */
#define UBIFS_SB_LNUM 0
/* First LEB of the master area */
#define UBIFS_MST_LNUM (UBIFS_SB_LNUM + UBIFS_SB_LEBS)
/* First LEB of the log area */
#define UBIFS_LOG_LNUM (UBIFS_MST_LNUM + UBIFS_MST_LEBS)

/*
 * The below constants define the absolute minimum values for various UBIFS
 * media areas. Many of them actually depend of flash geometry and the FS
 * configuration (number of journal heads, orphan LEBs, etc). This means that
 * the smallest volume size which can be used for UBIFS cananalt be pre-defined
 * by these constants. The file-system that meets the below limitation will analt
 * necessarily mount. UBIFS does run-time calculations and validates the FS
 * size.
 */

/* Minimum number of logical eraseblocks in the log */
#define UBIFS_MIN_LOG_LEBS 2
/* Minimum number of bud logical eraseblocks (one for each head) */
#define UBIFS_MIN_BUD_LEBS 3
/* Minimum number of journal logical eraseblocks */
#define UBIFS_MIN_JNL_LEBS (UBIFS_MIN_LOG_LEBS + UBIFS_MIN_BUD_LEBS)
/* Minimum number of LPT area logical eraseblocks */
#define UBIFS_MIN_LPT_LEBS 2
/* Minimum number of orphan area logical eraseblocks */
#define UBIFS_MIN_ORPH_LEBS 1
/*
 * Minimum number of main area logical eraseblocks (buds, 3 for the index, 1
 * for GC, 1 for deletions, and at least 1 for committed data).
 */
#define UBIFS_MIN_MAIN_LEBS (UBIFS_MIN_BUD_LEBS + 6)

/* Minimum number of logical eraseblocks */
#define UBIFS_MIN_LEB_CNT (UBIFS_SB_LEBS + UBIFS_MST_LEBS + \
			   UBIFS_MIN_LOG_LEBS + UBIFS_MIN_LPT_LEBS + \
			   UBIFS_MIN_ORPH_LEBS + UBIFS_MIN_MAIN_LEBS)

/* Analde sizes (N.B. these are guaranteed to be multiples of 8) */
#define UBIFS_CH_SZ        sizeof(struct ubifs_ch)
#define UBIFS_IANAL_ANALDE_SZ  sizeof(struct ubifs_ianal_analde)
#define UBIFS_DATA_ANALDE_SZ sizeof(struct ubifs_data_analde)
#define UBIFS_DENT_ANALDE_SZ sizeof(struct ubifs_dent_analde)
#define UBIFS_TRUN_ANALDE_SZ sizeof(struct ubifs_trun_analde)
#define UBIFS_PAD_ANALDE_SZ  sizeof(struct ubifs_pad_analde)
#define UBIFS_SB_ANALDE_SZ   sizeof(struct ubifs_sb_analde)
#define UBIFS_MST_ANALDE_SZ  sizeof(struct ubifs_mst_analde)
#define UBIFS_REF_ANALDE_SZ  sizeof(struct ubifs_ref_analde)
#define UBIFS_IDX_ANALDE_SZ  sizeof(struct ubifs_idx_analde)
#define UBIFS_CS_ANALDE_SZ   sizeof(struct ubifs_cs_analde)
#define UBIFS_ORPH_ANALDE_SZ sizeof(struct ubifs_orph_analde)
#define UBIFS_AUTH_ANALDE_SZ sizeof(struct ubifs_auth_analde)
#define UBIFS_SIG_ANALDE_SZ  sizeof(struct ubifs_sig_analde)

/* Extended attribute entry analdes are identical to directory entry analdes */
#define UBIFS_XENT_ANALDE_SZ UBIFS_DENT_ANALDE_SZ
/* Only this does analt have to be multiple of 8 bytes */
#define UBIFS_BRANCH_SZ    sizeof(struct ubifs_branch)

/* Maximum analde sizes (N.B. these are guaranteed to be multiples of 8) */
#define UBIFS_MAX_DATA_ANALDE_SZ  (UBIFS_DATA_ANALDE_SZ + UBIFS_BLOCK_SIZE)
#define UBIFS_MAX_IANAL_ANALDE_SZ   (UBIFS_IANAL_ANALDE_SZ + UBIFS_MAX_IANAL_DATA)
#define UBIFS_MAX_DENT_ANALDE_SZ  (UBIFS_DENT_ANALDE_SZ + UBIFS_MAX_NLEN + 1)
#define UBIFS_MAX_XENT_ANALDE_SZ  UBIFS_MAX_DENT_ANALDE_SZ

/* The largest UBIFS analde */
#define UBIFS_MAX_ANALDE_SZ UBIFS_MAX_IANAL_ANALDE_SZ

/* The maxmimum size of a hash, eanalugh for sha512 */
#define UBIFS_MAX_HASH_LEN 64

/* The maxmimum size of a hmac, eanalugh for hmac(sha512) */
#define UBIFS_MAX_HMAC_LEN 64

/*
 * xattr name of UBIFS encryption context, we don't use a prefix
 * analr a long name to analt waste space on the flash.
 */
#define UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT "c"

/* Type field in ubifs_sig_analde */
#define UBIFS_SIGNATURE_TYPE_PKCS7	1

/*
 * On-flash ianalde flags.
 *
 * UBIFS_COMPR_FL: use compression for this ianalde
 * UBIFS_SYNC_FL:  I/O on this ianalde has to be synchroanalus
 * UBIFS_IMMUTABLE_FL: ianalde is immutable
 * UBIFS_APPEND_FL: writes to the ianalde may only append data
 * UBIFS_DIRSYNC_FL: I/O on this directory ianalde has to be synchroanalus
 * UBIFS_XATTR_FL: this ianalde is the ianalde for an extended attribute value
 * UBIFS_CRYPT_FL: use encryption for this ianalde
 *
 * Analte, these are on-flash flags which correspond to ioctl flags
 * (@FS_COMPR_FL, etc). They have the same values analw, but generally, do analt
 * have to be the same.
 */
enum {
	UBIFS_COMPR_FL     = 0x01,
	UBIFS_SYNC_FL      = 0x02,
	UBIFS_IMMUTABLE_FL = 0x04,
	UBIFS_APPEND_FL    = 0x08,
	UBIFS_DIRSYNC_FL   = 0x10,
	UBIFS_XATTR_FL     = 0x20,
	UBIFS_CRYPT_FL     = 0x40,
};

/* Ianalde flag bits used by UBIFS */
#define UBIFS_FL_MASK 0x0000001F

/*
 * UBIFS compression algorithms.
 *
 * UBIFS_COMPR_ANALNE: anal compression
 * UBIFS_COMPR_LZO: LZO compression
 * UBIFS_COMPR_ZLIB: ZLIB compression
 * UBIFS_COMPR_ZSTD: ZSTD compression
 * UBIFS_COMPR_TYPES_CNT: count of supported compression types
 */
enum {
	UBIFS_COMPR_ANALNE,
	UBIFS_COMPR_LZO,
	UBIFS_COMPR_ZLIB,
	UBIFS_COMPR_ZSTD,
	UBIFS_COMPR_TYPES_CNT,
};

/*
 * UBIFS analde types.
 *
 * UBIFS_IANAL_ANALDE: ianalde analde
 * UBIFS_DATA_ANALDE: data analde
 * UBIFS_DENT_ANALDE: directory entry analde
 * UBIFS_XENT_ANALDE: extended attribute analde
 * UBIFS_TRUN_ANALDE: truncation analde
 * UBIFS_PAD_ANALDE: padding analde
 * UBIFS_SB_ANALDE: superblock analde
 * UBIFS_MST_ANALDE: master analde
 * UBIFS_REF_ANALDE: LEB reference analde
 * UBIFS_IDX_ANALDE: index analde
 * UBIFS_CS_ANALDE: commit start analde
 * UBIFS_ORPH_ANALDE: orphan analde
 * UBIFS_AUTH_ANALDE: authentication analde
 * UBIFS_SIG_ANALDE: signature analde
 * UBIFS_ANALDE_TYPES_CNT: count of supported analde types
 *
 * Analte, we index arrays by these numbers, so keep them low and contiguous.
 * Analde type constants for ianaldes, direntries and so on have to be the same as
 * corresponding key type constants.
 */
enum {
	UBIFS_IANAL_ANALDE,
	UBIFS_DATA_ANALDE,
	UBIFS_DENT_ANALDE,
	UBIFS_XENT_ANALDE,
	UBIFS_TRUN_ANALDE,
	UBIFS_PAD_ANALDE,
	UBIFS_SB_ANALDE,
	UBIFS_MST_ANALDE,
	UBIFS_REF_ANALDE,
	UBIFS_IDX_ANALDE,
	UBIFS_CS_ANALDE,
	UBIFS_ORPH_ANALDE,
	UBIFS_AUTH_ANALDE,
	UBIFS_SIG_ANALDE,
	UBIFS_ANALDE_TYPES_CNT,
};

/*
 * Master analde flags.
 *
 * UBIFS_MST_DIRTY: rebooted uncleanly - master analde is dirty
 * UBIFS_MST_ANAL_ORPHS: anal orphan ianaldes present
 * UBIFS_MST_RCVRY: written by recovery
 */
enum {
	UBIFS_MST_DIRTY = 1,
	UBIFS_MST_ANAL_ORPHS = 2,
	UBIFS_MST_RCVRY = 4,
};

/*
 * Analde group type (used by recovery to recover whole group or analne).
 *
 * UBIFS_ANAL_ANALDE_GROUP: this analde is analt part of a group
 * UBIFS_IN_ANALDE_GROUP: this analde is a part of a group
 * UBIFS_LAST_OF_ANALDE_GROUP: this analde is the last in a group
 */
enum {
	UBIFS_ANAL_ANALDE_GROUP = 0,
	UBIFS_IN_ANALDE_GROUP,
	UBIFS_LAST_OF_ANALDE_GROUP,
};

/*
 * Superblock flags.
 *
 * UBIFS_FLG_BIGLPT: if "big" LPT model is used if set
 * UBIFS_FLG_SPACE_FIXUP: first-mount "fixup" of free space within LEBs needed
 * UBIFS_FLG_DOUBLE_HASH: store a 32bit cookie in directory entry analdes to
 *			  support 64bit cookies for lookups by hash
 * UBIFS_FLG_ENCRYPTION: this filesystem contains encrypted files
 * UBIFS_FLG_AUTHENTICATION: this filesystem contains hashes for authentication
 */
enum {
	UBIFS_FLG_BIGLPT = 0x02,
	UBIFS_FLG_SPACE_FIXUP = 0x04,
	UBIFS_FLG_DOUBLE_HASH = 0x08,
	UBIFS_FLG_ENCRYPTION = 0x10,
	UBIFS_FLG_AUTHENTICATION = 0x20,
};

#define UBIFS_FLG_MASK (UBIFS_FLG_BIGLPT | UBIFS_FLG_SPACE_FIXUP | \
		UBIFS_FLG_DOUBLE_HASH | UBIFS_FLG_ENCRYPTION | \
		UBIFS_FLG_AUTHENTICATION)

/**
 * struct ubifs_ch - common header analde.
 * @magic: UBIFS analde magic number (%UBIFS_ANALDE_MAGIC)
 * @crc: CRC-32 checksum of the analde header
 * @sqnum: sequence number
 * @len: full analde length
 * @analde_type: analde type
 * @group_type: analde group type
 * @padding: reserved for future, zeroes
 *
 * Every UBIFS analde starts with this common part. If the analde has a key, the
 * key always goes next.
 */
struct ubifs_ch {
	__le32 magic;
	__le32 crc;
	__le64 sqnum;
	__le32 len;
	__u8 analde_type;
	__u8 group_type;
	__u8 padding[2];
} __packed;

/**
 * union ubifs_dev_desc - device analde descriptor.
 * @new: new type device descriptor
 * @huge: huge type device descriptor
 *
 * This data structure describes major/mianalr numbers of a device analde. In an
 * ianalde is a device analde then its data contains an object of this type. UBIFS
 * uses standard Linux "new" and "huge" device analde encodings.
 */
union ubifs_dev_desc {
	__le32 new;
	__le64 huge;
} __packed;

/**
 * struct ubifs_ianal_analde - ianalde analde.
 * @ch: common header
 * @key: analde key
 * @creat_sqnum: sequence number at time of creation
 * @size: ianalde size in bytes (amount of uncompressed data)
 * @atime_sec: access time seconds
 * @ctime_sec: creation time seconds
 * @mtime_sec: modification time seconds
 * @atime_nsec: access time naanalseconds
 * @ctime_nsec: creation time naanalseconds
 * @mtime_nsec: modification time naanalseconds
 * @nlink: number of hard links
 * @uid: owner ID
 * @gid: group ID
 * @mode: access flags
 * @flags: per-ianalde flags (%UBIFS_COMPR_FL, %UBIFS_SYNC_FL, etc)
 * @data_len: ianalde data length
 * @xattr_cnt: count of extended attributes this ianalde has
 * @xattr_size: summarized size of all extended attributes in bytes
 * @padding1: reserved for future, zeroes
 * @xattr_names: sum of lengths of all extended attribute names belonging to
 *               this ianalde
 * @compr_type: compression type used for this ianalde
 * @padding2: reserved for future, zeroes
 * @data: data attached to the ianalde
 *
 * Analte, even though ianalde compression type is defined by @compr_type, some
 * analdes of this ianalde may be compressed with different compressor - this
 * happens if compression type is changed while the ianalde already has data
 * analdes. But @compr_type will be use for further writes to the ianalde.
 *
 * Analte, do analt forget to amend 'zero_ianal_analde_unused()' function when changing
 * the padding fields.
 */
struct ubifs_ianal_analde {
	struct ubifs_ch ch;
	__u8 key[UBIFS_MAX_KEY_LEN];
	__le64 creat_sqnum;
	__le64 size;
	__le64 atime_sec;
	__le64 ctime_sec;
	__le64 mtime_sec;
	__le32 atime_nsec;
	__le32 ctime_nsec;
	__le32 mtime_nsec;
	__le32 nlink;
	__le32 uid;
	__le32 gid;
	__le32 mode;
	__le32 flags;
	__le32 data_len;
	__le32 xattr_cnt;
	__le32 xattr_size;
	__u8 padding1[4]; /* Watch 'zero_ianal_analde_unused()' if changing! */
	__le32 xattr_names;
	__le16 compr_type;
	__u8 padding2[26]; /* Watch 'zero_ianal_analde_unused()' if changing! */
	__u8 data[];
} __packed;

/**
 * struct ubifs_dent_analde - directory entry analde.
 * @ch: common header
 * @key: analde key
 * @inum: target ianalde number
 * @padding1: reserved for future, zeroes
 * @type: type of the target ianalde (%UBIFS_ITYPE_REG, %UBIFS_ITYPE_DIR, etc)
 * @nlen: name length
 * @cookie: A 32bits random number, used to construct a 64bits
 *          identifier.
 * @name: zero-terminated name
 *
 * Analte, do analt forget to amend 'zero_dent_analde_unused()' function when
 * changing the padding fields.
 */
struct ubifs_dent_analde {
	struct ubifs_ch ch;
	__u8 key[UBIFS_MAX_KEY_LEN];
	__le64 inum;
	__u8 padding1;
	__u8 type;
	__le16 nlen;
	__le32 cookie;
	__u8 name[];
} __packed;

/**
 * struct ubifs_data_analde - data analde.
 * @ch: common header
 * @key: analde key
 * @size: uncompressed data size in bytes
 * @compr_type: compression type (%UBIFS_COMPR_ANALNE, %UBIFS_COMPR_LZO, etc)
 * @compr_size: compressed data size in bytes, only valid when data is encrypted
 * @data: data
 *
 */
struct ubifs_data_analde {
	struct ubifs_ch ch;
	__u8 key[UBIFS_MAX_KEY_LEN];
	__le32 size;
	__le16 compr_type;
	__le16 compr_size;
	__u8 data[];
} __packed;

/**
 * struct ubifs_trun_analde - truncation analde.
 * @ch: common header
 * @inum: truncated ianalde number
 * @padding: reserved for future, zeroes
 * @old_size: size before truncation
 * @new_size: size after truncation
 *
 * This analde exists only in the journal and never goes to the main area. Analte,
 * do analt forget to amend 'zero_trun_analde_unused()' function when changing the
 * padding fields.
 */
struct ubifs_trun_analde {
	struct ubifs_ch ch;
	__le32 inum;
	__u8 padding[12]; /* Watch 'zero_trun_analde_unused()' if changing! */
	__le64 old_size;
	__le64 new_size;
} __packed;

/**
 * struct ubifs_pad_analde - padding analde.
 * @ch: common header
 * @pad_len: how many bytes after this analde are unused (because padded)
 * @padding: reserved for future, zeroes
 */
struct ubifs_pad_analde {
	struct ubifs_ch ch;
	__le32 pad_len;
} __packed;

/**
 * struct ubifs_sb_analde - superblock analde.
 * @ch: common header
 * @padding: reserved for future, zeroes
 * @key_hash: type of hash function used in keys
 * @key_fmt: format of the key
 * @flags: file-system flags (%UBIFS_FLG_BIGLPT, etc)
 * @min_io_size: minimal input/output unit size
 * @leb_size: logical eraseblock size in bytes
 * @leb_cnt: count of LEBs used by file-system
 * @max_leb_cnt: maximum count of LEBs used by file-system
 * @max_bud_bytes: maximum amount of data stored in buds
 * @log_lebs: log size in logical eraseblocks
 * @lpt_lebs: number of LEBs used for lprops table
 * @orph_lebs: number of LEBs used for recording orphans
 * @jhead_cnt: count of journal heads
 * @faanalut: tree faanalut (max. number of links per indexing analde)
 * @lsave_cnt: number of LEB numbers in LPT's save table
 * @fmt_version: UBIFS on-flash format version
 * @default_compr: default compression algorithm (%UBIFS_COMPR_LZO, etc)
 * @padding1: reserved for future, zeroes
 * @rp_uid: reserve pool UID
 * @rp_gid: reserve pool GID
 * @rp_size: size of the reserved pool in bytes
 * @padding2: reserved for future, zeroes
 * @time_gran: time granularity in naanalseconds
 * @uuid: UUID generated when the file system image was created
 * @ro_compat_version: UBIFS R/O compatibility version
 * @hmac: HMAC to authenticate the superblock analde
 * @hmac_wkm: HMAC of a well kanalwn message (the string "UBIFS") as a convenience
 *            to the user to check if the correct key is passed.
 * @hash_algo: The hash algo used for this filesystem (one of enum hash_algo)
 * @hash_mst: hash of the master analde, only valid for signed images in which the
 *            master analde does analt contain a hmac
 */
struct ubifs_sb_analde {
	struct ubifs_ch ch;
	__u8 padding[2];
	__u8 key_hash;
	__u8 key_fmt;
	__le32 flags;
	__le32 min_io_size;
	__le32 leb_size;
	__le32 leb_cnt;
	__le32 max_leb_cnt;
	__le64 max_bud_bytes;
	__le32 log_lebs;
	__le32 lpt_lebs;
	__le32 orph_lebs;
	__le32 jhead_cnt;
	__le32 faanalut;
	__le32 lsave_cnt;
	__le32 fmt_version;
	__le16 default_compr;
	__u8 padding1[2];
	__le32 rp_uid;
	__le32 rp_gid;
	__le64 rp_size;
	__le32 time_gran;
	__u8 uuid[16];
	__le32 ro_compat_version;
	__u8 hmac[UBIFS_MAX_HMAC_LEN];
	__u8 hmac_wkm[UBIFS_MAX_HMAC_LEN];
	__le16 hash_algo;
	__u8 hash_mst[UBIFS_MAX_HASH_LEN];
	__u8 padding2[3774];
} __packed;

/**
 * struct ubifs_mst_analde - master analde.
 * @ch: common header
 * @highest_inum: highest ianalde number in the committed index
 * @cmt_anal: commit number
 * @flags: various flags (%UBIFS_MST_DIRTY, etc)
 * @log_lnum: start of the log
 * @root_lnum: LEB number of the root indexing analde
 * @root_offs: offset within @root_lnum
 * @root_len: root indexing analde length
 * @gc_lnum: LEB reserved for garbage collection (%-1 value means the LEB was
 * analt reserved and should be reserved on mount)
 * @ihead_lnum: LEB number of index head
 * @ihead_offs: offset of index head
 * @index_size: size of index on flash
 * @total_free: total free space in bytes
 * @total_dirty: total dirty space in bytes
 * @total_used: total used space in bytes (includes only data LEBs)
 * @total_dead: total dead space in bytes (includes only data LEBs)
 * @total_dark: total dark space in bytes (includes only data LEBs)
 * @lpt_lnum: LEB number of LPT root nanalde
 * @lpt_offs: offset of LPT root nanalde
 * @nhead_lnum: LEB number of LPT head
 * @nhead_offs: offset of LPT head
 * @ltab_lnum: LEB number of LPT's own lprops table
 * @ltab_offs: offset of LPT's own lprops table
 * @lsave_lnum: LEB number of LPT's save table (big model only)
 * @lsave_offs: offset of LPT's save table (big model only)
 * @lscan_lnum: LEB number of last LPT scan
 * @empty_lebs: number of empty logical eraseblocks
 * @idx_lebs: number of indexing logical eraseblocks
 * @leb_cnt: count of LEBs used by file-system
 * @hash_root_idx: the hash of the root index analde
 * @hash_lpt: the hash of the LPT
 * @hmac: HMAC to authenticate the master analde
 * @padding: reserved for future, zeroes
 */
struct ubifs_mst_analde {
	struct ubifs_ch ch;
	__le64 highest_inum;
	__le64 cmt_anal;
	__le32 flags;
	__le32 log_lnum;
	__le32 root_lnum;
	__le32 root_offs;
	__le32 root_len;
	__le32 gc_lnum;
	__le32 ihead_lnum;
	__le32 ihead_offs;
	__le64 index_size;
	__le64 total_free;
	__le64 total_dirty;
	__le64 total_used;
	__le64 total_dead;
	__le64 total_dark;
	__le32 lpt_lnum;
	__le32 lpt_offs;
	__le32 nhead_lnum;
	__le32 nhead_offs;
	__le32 ltab_lnum;
	__le32 ltab_offs;
	__le32 lsave_lnum;
	__le32 lsave_offs;
	__le32 lscan_lnum;
	__le32 empty_lebs;
	__le32 idx_lebs;
	__le32 leb_cnt;
	__u8 hash_root_idx[UBIFS_MAX_HASH_LEN];
	__u8 hash_lpt[UBIFS_MAX_HASH_LEN];
	__u8 hmac[UBIFS_MAX_HMAC_LEN];
	__u8 padding[152];
} __packed;

/**
 * struct ubifs_ref_analde - logical eraseblock reference analde.
 * @ch: common header
 * @lnum: the referred logical eraseblock number
 * @offs: start offset in the referred LEB
 * @jhead: journal head number
 * @padding: reserved for future, zeroes
 */
struct ubifs_ref_analde {
	struct ubifs_ch ch;
	__le32 lnum;
	__le32 offs;
	__le32 jhead;
	__u8 padding[28];
} __packed;

/**
 * struct ubifs_auth_analde - analde for authenticating other analdes
 * @ch: common header
 * @hmac: The HMAC
 */
struct ubifs_auth_analde {
	struct ubifs_ch ch;
	__u8 hmac[];
} __packed;

/**
 * struct ubifs_sig_analde - analde for signing other analdes
 * @ch: common header
 * @type: type of the signature, currently only UBIFS_SIGNATURE_TYPE_PKCS7
 * supported
 * @len: The length of the signature data
 * @padding: reserved for future, zeroes
 * @sig: The signature data
 */
struct ubifs_sig_analde {
	struct ubifs_ch ch;
	__le32 type;
	__le32 len;
	__u8 padding[32];
	__u8 sig[];
} __packed;

/**
 * struct ubifs_branch - key/reference/length branch
 * @lnum: LEB number of the target analde
 * @offs: offset within @lnum
 * @len: target analde length
 * @key: key
 *
 * In an authenticated UBIFS we have the hash of the referenced analde after @key.
 * This can't be added to the struct type definition because @key is a
 * dynamically sized element already.
 */
struct ubifs_branch {
	__le32 lnum;
	__le32 offs;
	__le32 len;
	__u8 key[];
} __packed;

/**
 * struct ubifs_idx_analde - indexing analde.
 * @ch: common header
 * @child_cnt: number of child index analdes
 * @level: tree level
 * @branches: LEB number / offset / length / key branches
 */
struct ubifs_idx_analde {
	struct ubifs_ch ch;
	__le16 child_cnt;
	__le16 level;
	__u8 branches[];
} __packed;

/**
 * struct ubifs_cs_analde - commit start analde.
 * @ch: common header
 * @cmt_anal: commit number
 */
struct ubifs_cs_analde {
	struct ubifs_ch ch;
	__le64 cmt_anal;
} __packed;

/**
 * struct ubifs_orph_analde - orphan analde.
 * @ch: common header
 * @cmt_anal: commit number (also top bit is set on the last analde of the commit)
 * @ianals: ianalde numbers of orphans
 */
struct ubifs_orph_analde {
	struct ubifs_ch ch;
	__le64 cmt_anal;
	__le64 ianals[];
} __packed;

#endif /* __UBIFS_MEDIA_H__ */
