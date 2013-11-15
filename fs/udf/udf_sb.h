#ifndef __LINUX_UDF_SB_H
#define __LINUX_UDF_SB_H

#include <linux/mutex.h>
#include <linux/bitops.h>

/* Since UDF 2.01 is ISO 13346 based... */
#define UDF_SUPER_MAGIC			0x15013346

#define UDF_MAX_READ_VERSION		0x0250
#define UDF_MAX_WRITE_VERSION		0x0201

#define UDF_FLAG_USE_EXTENDED_FE	0
#define UDF_VERS_USE_EXTENDED_FE	0x0200
#define UDF_FLAG_USE_STREAMS		1
#define UDF_VERS_USE_STREAMS		0x0200
#define UDF_FLAG_USE_SHORT_AD		2
#define UDF_FLAG_USE_AD_IN_ICB		3
#define UDF_FLAG_USE_FILE_CTIME_EA	4
#define UDF_FLAG_STRICT			5
#define UDF_FLAG_UNDELETE		6
#define UDF_FLAG_UNHIDE			7
#define UDF_FLAG_VARCONV		8
#define UDF_FLAG_NLS_MAP		9
#define UDF_FLAG_UTF8			10
#define UDF_FLAG_UID_FORGET     11    /* save -1 for uid to disk */
#define UDF_FLAG_UID_IGNORE     12    /* use sb uid instead of on disk uid */
#define UDF_FLAG_GID_FORGET     13
#define UDF_FLAG_GID_IGNORE     14
#define UDF_FLAG_UID_SET	15
#define UDF_FLAG_GID_SET	16
#define UDF_FLAG_SESSION_SET	17
#define UDF_FLAG_LASTBLOCK_SET	18
#define UDF_FLAG_BLOCKSIZE_SET	19

#define UDF_PART_FLAG_UNALLOC_BITMAP	0x0001
#define UDF_PART_FLAG_UNALLOC_TABLE	0x0002
#define UDF_PART_FLAG_FREED_BITMAP	0x0004
#define UDF_PART_FLAG_FREED_TABLE	0x0008
#define UDF_PART_FLAG_READ_ONLY		0x0010
#define UDF_PART_FLAG_WRITE_ONCE	0x0020
#define UDF_PART_FLAG_REWRITABLE	0x0040
#define UDF_PART_FLAG_OVERWRITABLE	0x0080

#define UDF_MAX_BLOCK_LOADED	8

#define UDF_TYPE1_MAP15			0x1511U
#define UDF_VIRTUAL_MAP15		0x1512U
#define UDF_VIRTUAL_MAP20		0x2012U
#define UDF_SPARABLE_MAP15		0x1522U
#define UDF_METADATA_MAP25		0x2511U

#define UDF_INVALID_MODE		((umode_t)-1)

#pragma pack(1) /* XXX(hch): Why?  This file just defines in-core structures */

#define MF_DUPLICATE_MD		0x01
#define MF_MIRROR_FE_LOADED	0x02

struct udf_meta_data {
	__u32	s_meta_file_loc;
	__u32	s_mirror_file_loc;
	__u32	s_bitmap_file_loc;
	__u32	s_alloc_unit_size;
	__u16	s_align_unit_size;
	int	s_flags;
	struct inode *s_metadata_fe;
	struct inode *s_mirror_fe;
	struct inode *s_bitmap_fe;
};

struct udf_sparing_data {
	__u16	s_packet_len;
	struct buffer_head *s_spar_map[4];
};

struct udf_virtual_data {
	__u32	s_num_entries;
	__u16	s_start_offset;
};

struct udf_bitmap {
	__u32			s_extPosition;
	int			s_nr_groups;
	struct buffer_head 	*s_block_bitmap[0];
};

struct udf_part_map {
	union {
		struct udf_bitmap	*s_bitmap;
		struct inode		*s_table;
	} s_uspace;
	union {
		struct udf_bitmap	*s_bitmap;
		struct inode		*s_table;
	} s_fspace;
	__u32	s_partition_root;
	__u32	s_partition_len;
	__u16	s_partition_type;
	__u16	s_partition_num;
	union {
		struct udf_sparing_data s_sparing;
		struct udf_virtual_data s_virtual;
		struct udf_meta_data s_metadata;
	} s_type_specific;
	__u32	(*s_partition_func)(struct super_block *, __u32, __u16, __u32);
	__u16	s_volumeseqnum;
	__u16	s_partition_flags;
};

#pragma pack()

struct udf_sb_info {
	struct udf_part_map	*s_partmaps;
	__u8			s_volume_ident[32];

	/* Overall info */
	__u16			s_partitions;
	__u16			s_partition;

	/* Sector headers */
	__s32			s_session;
	__u32			s_anchor;
	__u32			s_last_block;

	struct buffer_head	*s_lvid_bh;

	/* Default permissions */
	umode_t			s_umask;
	kgid_t			s_gid;
	kuid_t			s_uid;
	umode_t			s_fmode;
	umode_t			s_dmode;
	/* Lock protecting consistency of above permission settings */
	rwlock_t		s_cred_lock;

	/* Root Info */
	struct timespec		s_record_time;

	/* Fileset Info */
	__u16			s_serial_number;

	/* highest UDF revision we have recorded to this media */
	__u16			s_udfrev;

	/* Miscellaneous flags */
	unsigned long		s_flags;

	/* Encoding info */
	struct nls_table	*s_nls_map;

	/* VAT inode */
	struct inode		*s_vat_inode;

	struct mutex		s_alloc_mutex;
	/* Protected by s_alloc_mutex */
	unsigned int		s_lvid_dirty;
};

static inline struct udf_sb_info *UDF_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

struct logicalVolIntegrityDescImpUse *udf_sb_lvidiu(struct super_block *sb);

int udf_compute_nr_groups(struct super_block *sb, u32 partition);

static inline int UDF_QUERY_FLAG(struct super_block *sb, int flag)
{
	return test_bit(flag, &UDF_SB(sb)->s_flags);
}

static inline void UDF_SET_FLAG(struct super_block *sb, int flag)
{
	set_bit(flag, &UDF_SB(sb)->s_flags);
}

static inline void UDF_CLEAR_FLAG(struct super_block *sb, int flag)
{
	clear_bit(flag, &UDF_SB(sb)->s_flags);
}

#endif /* __LINUX_UDF_SB_H */
