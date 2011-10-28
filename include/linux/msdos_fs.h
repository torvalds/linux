#ifndef _LINUX_MSDOS_FS_H
#define _LINUX_MSDOS_FS_H

#include <linux/types.h>
#include <linux/magic.h>
#include <asm/byteorder.h>

/*
 * The MS-DOS filesystem constants/structures
 */

#define SECTOR_SIZE	512		/* sector size (bytes) */
#define SECTOR_BITS	9		/* log2(SECTOR_SIZE) */
#define MSDOS_DPB	(MSDOS_DPS)	/* dir entries per block */
#define MSDOS_DPB_BITS	4		/* log2(MSDOS_DPB) */
#define MSDOS_DPS	(SECTOR_SIZE / sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS	4		/* log2(MSDOS_DPS) */
#define MSDOS_LONGNAME	256		/* maximum name length */
#define CF_LE_W(v)	le16_to_cpu(v)
#define CF_LE_L(v)	le32_to_cpu(v)
#define CT_LE_W(v)	cpu_to_le16(v)
#define CT_LE_L(v)	cpu_to_le32(v)


#define MSDOS_ROOT_INO	1	/* == MINIX_ROOT_INO */
#define MSDOS_DIR_BITS	5	/* log2(sizeof(struct msdos_dir_entry)) */

/* directory limit */
#define FAT_MAX_DIR_ENTRIES	(65536)
#define FAT_MAX_DIR_SIZE	(FAT_MAX_DIR_ENTRIES << MSDOS_DIR_BITS)

#define ATTR_NONE	0	/* no attribute bits */
#define ATTR_RO		1	/* read-only */
#define ATTR_HIDDEN	2	/* hidden */
#define ATTR_SYS	4	/* system */
#define ATTR_VOLUME	8	/* volume label */
#define ATTR_DIR	16	/* directory */
#define ATTR_ARCH	32	/* archived */

/* attribute bits that are copied "as is" */
#define ATTR_UNUSED	(ATTR_VOLUME | ATTR_ARCH | ATTR_SYS | ATTR_HIDDEN)
/* bits that are used by the Windows 95/Windows NT extended FAT */
#define ATTR_EXT	(ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)

#define CASE_LOWER_BASE	8	/* base is lower case */
#define CASE_LOWER_EXT	16	/* extension is lower case */

#define DELETED_FLAG	0xe5	/* marks file as deleted when in name[0] */
#define IS_FREE(n)	(!*(n) || *(n) == DELETED_FLAG)

#define FAT_LFN_LEN	255	/* maximum long name length */
#define MSDOS_NAME	11	/* maximum name length */
#define MSDOS_SLOTS	21	/* max # of slots for short and long names */
#define MSDOS_DOT	".          "	/* ".", padded to MSDOS_NAME chars */
#define MSDOS_DOTDOT	"..         "	/* "..", padded to MSDOS_NAME chars */

#define FAT_FIRST_ENT(s, x)	((MSDOS_SB(s)->fat_bits == 32 ? 0x0FFFFF00 : \
	MSDOS_SB(s)->fat_bits == 16 ? 0xFF00 : 0xF00) | (x))

/* start of data cluster's entry (number of reserved clusters) */
#define FAT_START_ENT	2

/* maximum number of clusters */
#define MAX_FAT12	0xFF4
#define MAX_FAT16	0xFFF4
#define MAX_FAT32	0x0FFFFFF6
#define MAX_FAT(s)	(MSDOS_SB(s)->fat_bits == 32 ? MAX_FAT32 : \
	MSDOS_SB(s)->fat_bits == 16 ? MAX_FAT16 : MAX_FAT12)

/* bad cluster mark */
#define BAD_FAT12	0xFF7
#define BAD_FAT16	0xFFF7
#define BAD_FAT32	0x0FFFFFF7

/* standard EOF */
#define EOF_FAT12	0xFFF
#define EOF_FAT16	0xFFFF
#define EOF_FAT32	0x0FFFFFFF

#define FAT_ENT_FREE	(0)
#define FAT_ENT_BAD	(BAD_FAT32)
#define FAT_ENT_EOF	(EOF_FAT32)

#define FAT_FSINFO_SIG1	0x41615252
#define FAT_FSINFO_SIG2	0x61417272
#define IS_FSINFO(x)	(le32_to_cpu((x)->signature1) == FAT_FSINFO_SIG1 \
			 && le32_to_cpu((x)->signature2) == FAT_FSINFO_SIG2)

struct __fat_dirent {
	long		d_ino;
	__kernel_off_t	d_off;
	unsigned short	d_reclen;
	char		d_name[256]; /* We must not include limits.h! */
};

/*
 * ioctl commands
 */
#define VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct __fat_dirent[2])
#define VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct __fat_dirent[2])
/* <linux/videotext.h> has used 0x72 ('r') in collision, so skip a few */
#define FAT_IOCTL_GET_ATTRIBUTES	_IOR('r', 0x10, __u32)
#define FAT_IOCTL_SET_ATTRIBUTES	_IOW('r', 0x11, __u32)
#define VFAT_IOCTL_GET_VOLUME_ID	_IOR('r', 0x12, __u32)

struct fat_boot_sector {
	__u8	ignored[3];	/* Boot strap short or near jump */
	__u8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	__u8	sector_size[2];	/* bytes per logical sector */
	__u8	sec_per_clus;	/* sectors/cluster */
	__le16	reserved;	/* reserved sectors */
	__u8	fats;		/* number of FATs */
	__u8	dir_entries[2];	/* root directory entries */
	__u8	sectors[2];	/* number of sectors */
	__u8	media;		/* media code */
	__le16	fat_length;	/* sectors/FAT */
	__le16	secs_track;	/* sectors per track */
	__le16	heads;		/* number of heads */
	__le32	hidden;		/* hidden sectors (unused) */
	__le32	total_sect;	/* number of sectors (if sectors == 0) */

	/* The following fields are only used by FAT32 */
	__le32	fat32_length;	/* sectors/FAT */
	__le16	flags;		/* bit 8: fat mirroring, low 4: active fat */
	__u8	version[2];	/* major, minor filesystem version */
	__le32	root_cluster;	/* first cluster in root directory */
	__le16	info_sector;	/* filesystem info sector */
	__le16	backup_boot;	/* backup boot sector */
	__le16	reserved2[6];	/* Unused */
};

struct fat_boot_fsinfo {
	__le32   signature1;	/* 0x41615252L */
	__le32   reserved1[120];	/* Nothing as far as I can tell */
	__le32   signature2;	/* 0x61417272L */
	__le32   free_clusters;	/* Free cluster count.  -1 if unknown */
	__le32   next_cluster;	/* Most recently allocated cluster */
	__le32   reserved2[4];
};

struct fat_boot_bsx {
	__u8     drive;		    /* drive number */
	__u8     reserved1;
	__u8     signature;	    /* extended boot signature */
	__u8     vol_id[4];     /* volume ID */
	__u8     vol_label[11]; /* volume label */
	__u8     type[8];       /* file system type */
};
#define FAT16_BSX_OFFSET	36 /* offset of fat_boot_bsx in FAT12 and FAT16 */
#define FAT32_BSX_OFFSET	64 /* offset of fat_boot_bsx in FAT32 */

struct msdos_dir_entry {
	__u8	name[MSDOS_NAME];/* name and extension */
	__u8	attr;		/* attribute bits */
	__u8    lcase;		/* Case for base and extension */
	__u8	ctime_cs;	/* Creation time, centiseconds (0-199) */
	__le16	ctime;		/* Creation time */
	__le16	cdate;		/* Creation date */
	__le16	adate;		/* Last access date */
	__le16	starthi;	/* High 16 bits of cluster in FAT32 */
	__le16	time,date,start;/* time, date and first cluster */
	__le32	size;		/* file size (in bytes) */
};

/* Up to 13 characters of the name */
struct msdos_dir_slot {
	__u8    id;		/* sequence number for slot */
	__u8    name0_4[10];	/* first 5 characters in name */
	__u8    attr;		/* attribute byte */
	__u8    reserved;	/* always 0 */
	__u8    alias_checksum;	/* checksum for 8.3 alias */
	__u8    name5_10[12];	/* 6 more characters in name */
	__le16   start;		/* starting cluster number, 0 in long slots */
	__u8    name11_12[4];	/* last 2 characters in name */
};

#ifdef __KERNEL__
/* media of boot sector */
static inline int fat_valid_media(u8 media)
{
	return 0xf8 <= media || media == 0xf0;
}
#endif /* !__KERNEL__ */
#endif /* !_LINUX_MSDOS_FS_H */
