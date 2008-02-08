#ifndef __LINUX_UDF_SB_H
#define __LINUX_UDF_SB_H

/* Since UDF 2.01 is ISO 13346 based... */
#define UDF_SUPER_MAGIC			0x15013346

#define UDF_MAX_READ_VERSION		0x0201
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

#define UDF_PART_FLAG_UNALLOC_BITMAP	0x0001
#define UDF_PART_FLAG_UNALLOC_TABLE	0x0002
#define UDF_PART_FLAG_FREED_BITMAP	0x0004
#define UDF_PART_FLAG_FREED_TABLE	0x0008
#define UDF_PART_FLAG_READ_ONLY		0x0010
#define UDF_PART_FLAG_WRITE_ONCE	0x0020
#define UDF_PART_FLAG_REWRITABLE	0x0040
#define UDF_PART_FLAG_OVERWRITABLE	0x0080

static inline struct udf_sb_info *UDF_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

struct logicalVolIntegrityDescImpUse *udf_sb_lvidiu(struct udf_sb_info *sbi);

#define UDF_SB_ALLOC_BITMAP(X,Y,Z)\
{\
	struct udf_sb_info *sbi = UDF_SB(X);\
	int nr_groups = ((sbi->s_partmaps[(Y)].s_partition_len + (sizeof(struct spaceBitmapDesc) << 3) +\
		((X)->s_blocksize * 8) - 1) / ((X)->s_blocksize * 8));\
	int size = sizeof(struct udf_bitmap) + (sizeof(struct buffer_head *) * nr_groups);\
	if (size <= PAGE_SIZE)\
		sbi->s_partmaps[(Y)].Z.s_bitmap = kmalloc(size, GFP_KERNEL);\
	else\
		sbi->s_partmaps[(Y)].Z.s_bitmap = vmalloc(size);\
	if (sbi->s_partmaps[(Y)].Z.s_bitmap != NULL) {\
		memset(sbi->s_partmaps[(Y)].Z.s_bitmap, 0x00, size);\
		sbi->s_partmaps[(Y)].Z.s_bitmap->s_block_bitmap =\
			(struct buffer_head **)(sbi->s_partmaps[(Y)].Z.s_bitmap + 1);\
		sbi->s_partmaps[(Y)].Z.s_bitmap->s_nr_groups = nr_groups;\
	} else {\
		udf_error(X, __FUNCTION__, "Unable to allocate space for bitmap and %d buffer_head pointers", nr_groups);\
	}\
}

#define UDF_SB_FREE_BITMAP(X,Y,Z)\
{\
	int i;\
	int nr_groups = UDF_SB_BITMAP_NR_GROUPS(X,Y,Z);\
	int size = sizeof(struct udf_bitmap) + (sizeof(struct buffer_head *) * nr_groups);\
	for (i = 0; i < nr_groups; i++) {\
		if (UDF_SB_BITMAP(X,Y,Z,i))\
			brelse(UDF_SB_BITMAP(X,Y,Z,i));\
	}\
	if (size <= PAGE_SIZE)\
		kfree(UDF_SB(X)->s_partmaps[Y].Z.s_bitmap);\
	else\
		vfree(UDF_SB(X)->s_partmaps[Y].Z.s_bitmap);\
}

#define UDF_QUERY_FLAG(X,Y)			( UDF_SB(X)->s_flags & ( 1 << (Y) ) )
#define UDF_SET_FLAG(X,Y)			( UDF_SB(X)->s_flags |= ( 1 << (Y) ) )
#define UDF_CLEAR_FLAG(X,Y)			( UDF_SB(X)->s_flags &= ~( 1 << (Y) ) )

#define UDF_SB_BITMAP(X,Y,Z,I)			( UDF_SB(X)->s_partmaps[(Y)].Z.s_bitmap->s_block_bitmap[I] )
#define UDF_SB_BITMAP_NR_GROUPS(X,Y,Z)		( UDF_SB(X)->s_partmaps[(Y)].Z.s_bitmap->s_nr_groups )

#endif /* __LINUX_UDF_SB_H */
