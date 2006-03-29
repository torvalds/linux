#ifndef _LINUX_MSDOS_FS_H
#define _LINUX_MSDOS_FS_H

/*
 * The MS-DOS filesystem constants/structures
 */
#include <asm/byteorder.h>

#define SECTOR_SIZE	512		/* sector size (bytes) */
#define SECTOR_BITS	9		/* log2(SECTOR_SIZE) */
#define MSDOS_DPB	(MSDOS_DPS)	/* dir entries per block */
#define MSDOS_DPB_BITS	4		/* log2(MSDOS_DPB) */
#define MSDOS_DPS	(SECTOR_SIZE / sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS	4		/* log2(MSDOS_DPS) */
#define CF_LE_W(v)	le16_to_cpu(v)
#define CF_LE_L(v)	le32_to_cpu(v)
#define CT_LE_W(v)	cpu_to_le16(v)
#define CT_LE_L(v)	cpu_to_le32(v)


#define MSDOS_SUPER_MAGIC 0x4d44 /* MD */

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

/* valid file mode bits */
#define MSDOS_VALID_MODE (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO)
/* Convert attribute bits and a mask to the UNIX mode. */
#define MSDOS_MKMODE(a, m) (m & (a & ATTR_RO ? S_IRUGO|S_IXUGO : S_IRWXUGO))

#define MSDOS_NAME	11	/* maximum name length */
#define MSDOS_LONGNAME	256	/* maximum name length */
#define MSDOS_SLOTS	21	/* max # of slots for short and long names */
#define MSDOS_DOT	".          "	/* ".", padded to MSDOS_NAME chars */
#define MSDOS_DOTDOT	"..         "	/* "..", padded to MSDOS_NAME chars */

/* media of boot sector */
#define FAT_VALID_MEDIA(x)	((0xF8 <= (x) && (x) <= 0xFF) || (x) == 0xF0)
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

/*
 * ioctl commands
 */
#define VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct dirent [2])
#define VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct dirent [2])
/* <linux/videotext.h> has used 0x72 ('r') in collision, so skip a few */
#define FAT_IOCTL_GET_ATTRIBUTES	_IOR('r', 0x10, __u32)
#define FAT_IOCTL_SET_ATTRIBUTES	_IOW('r', 0x11, __u32)

/*
 * vfat shortname flags
 */
#define VFAT_SFN_DISPLAY_LOWER	0x0001 /* convert to lowercase for display */
#define VFAT_SFN_DISPLAY_WIN95	0x0002 /* emulate win95 rule for display */
#define VFAT_SFN_DISPLAY_WINNT	0x0004 /* emulate winnt rule for display */
#define VFAT_SFN_CREATE_WIN95	0x0100 /* emulate win95 rule for create */
#define VFAT_SFN_CREATE_WINNT	0x0200 /* emulate winnt rule for create */

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

struct msdos_dir_entry {
	__u8	name[8],ext[3];	/* name and extension */
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

struct fat_slot_info {
	loff_t i_pos;		/* on-disk position of directory entry */
	loff_t slot_off;	/* offset for slot or de start */
	int nr_slots;		/* number of slots + 1(de) in filename */
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
};

#ifdef __KERNEL__

#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/nls.h>
#include <linux/fs.h>
#include <linux/mutex.h>

struct fat_mount_options {
	uid_t fs_uid;
	gid_t fs_gid;
	unsigned short fs_fmask;
	unsigned short fs_dmask;
	unsigned short codepage;  /* Codepage for shortname conversions */
	char *iocharset;          /* Charset used for filename input/display */
	unsigned short shortname; /* flags for shortname display/create rule */
	unsigned char name_check; /* r = relaxed, n = normal, s = strict */
	unsigned quiet:1,         /* set = fake successful chmods and chowns */
		 showexec:1,      /* set = only set x bit for com/exe/bat */
		 sys_immutable:1, /* set = system files are immutable */
		 dotsOK:1,        /* set = hidden and system files are named '.filename' */
		 isvfat:1,        /* 0=no vfat long filename support, 1=vfat support */
		 utf8:1,	  /* Use of UTF-8 character set (Default) */
		 unicode_xlate:1, /* create escape sequences for unhandled Unicode */
		 numtail:1,       /* Does first alias have a numeric '~1' type tail? */
		 atari:1,         /* Use Atari GEMDOS variation of MS-DOS fs */
		 nocase:1;	  /* Does this need case conversion? 0=need case conversion*/
};

#define FAT_HASH_BITS	8
#define FAT_HASH_SIZE	(1UL << FAT_HASH_BITS)
#define FAT_HASH_MASK	(FAT_HASH_SIZE-1)

/*
 * MS-DOS file system in-core superblock data
 */
struct msdos_sb_info {
	unsigned short sec_per_clus; /* sectors/cluster */
	unsigned short cluster_bits; /* log2(cluster_size) */
	unsigned int cluster_size;   /* cluster size */
	unsigned char fats,fat_bits; /* number of FATs, FAT bits (12 or 16) */
	unsigned short fat_start;
	unsigned long fat_length;    /* FAT start & length (sec.) */
	unsigned long dir_start;
	unsigned short dir_entries;  /* root dir start & entries */
	unsigned long data_start;    /* first data sector */
	unsigned long max_cluster;   /* maximum cluster number */
	unsigned long root_cluster;  /* first cluster of the root directory */
	unsigned long fsinfo_sector; /* sector number of FAT32 fsinfo */
	struct mutex fat_lock;
	unsigned int prev_free;      /* previously allocated cluster number */
	unsigned int free_clusters;  /* -1 if undefined */
	struct fat_mount_options options;
	struct nls_table *nls_disk;  /* Codepage used on disk */
	struct nls_table *nls_io;    /* Charset used for input and display */
	void *dir_ops;		     /* Opaque; default directory operations */
	int dir_per_block;	     /* dir entries per block */
	int dir_per_block_bits;	     /* log2(dir_per_block) */

	int fatent_shift;
	struct fatent_operations *fatent_ops;

	spinlock_t inode_hash_lock;
	struct hlist_head inode_hashtable[FAT_HASH_SIZE];
};

#define FAT_CACHE_VALID	0	/* special case for valid cache */

/*
 * MS-DOS file system inode data in memory
 */
struct msdos_inode_info {
	spinlock_t cache_lru_lock;
	struct list_head cache_lru;
	int nr_caches;
	/* for avoiding the race between fat_free() and fat_get_cluster() */
	unsigned int cache_valid_id;

	loff_t mmu_private;
	int i_start;		/* first cluster or 0 */
	int i_logstart;		/* logical first cluster */
	int i_attrs;		/* unused attribute bits */
	loff_t i_pos;		/* on-disk position of directory entry or 0 */
	struct hlist_node i_fat_hash;	/* hash by i_location */
	struct inode vfs_inode;
};

static inline struct msdos_sb_info *MSDOS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct msdos_inode_info *MSDOS_I(struct inode *inode)
{
	return container_of(inode, struct msdos_inode_info, vfs_inode);
}

/* Return the FAT attribute byte for this inode */
static inline u8 fat_attr(struct inode *inode)
{
	return ((inode->i_mode & S_IWUGO) ? ATTR_NONE : ATTR_RO) |
		(S_ISDIR(inode->i_mode) ? ATTR_DIR : ATTR_NONE) |
		MSDOS_I(inode)->i_attrs;
}

static inline unsigned char fat_checksum(const __u8 *name)
{
	unsigned char s = name[0];
	s = (s<<7) + (s>>1) + name[1];	s = (s<<7) + (s>>1) + name[2];
	s = (s<<7) + (s>>1) + name[3];	s = (s<<7) + (s>>1) + name[4];
	s = (s<<7) + (s>>1) + name[5];	s = (s<<7) + (s>>1) + name[6];
	s = (s<<7) + (s>>1) + name[7];	s = (s<<7) + (s>>1) + name[8];
	s = (s<<7) + (s>>1) + name[9];	s = (s<<7) + (s>>1) + name[10];
	return s;
}

static inline sector_t fat_clus_to_blknr(struct msdos_sb_info *sbi, int clus)
{
	return ((sector_t)clus - FAT_START_ENT) * sbi->sec_per_clus
		+ sbi->data_start;
}

static inline void fat16_towchar(wchar_t *dst, const __u8 *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		*dst++ = src[0] | (src[1] << 8);
		src += 2;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

static inline void fatwchar_to16(__u8 *dst, const wchar_t *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		dst[0] = *src & 0x00FF;
		dst[1] = (*src & 0xFF00) >> 8;
		dst += 2;
		src++;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

/* fat/cache.c */
extern void fat_cache_inval_inode(struct inode *inode);
extern int fat_get_cluster(struct inode *inode, int cluster,
			   int *fclus, int *dclus);
extern int fat_bmap(struct inode *inode, sector_t sector, sector_t *phys,
		    unsigned long *mapped_blocks);

/* fat/dir.c */
extern const struct file_operations fat_dir_operations;
extern int fat_search_long(struct inode *inode, const unsigned char *name,
			   int name_len, struct fat_slot_info *sinfo);
extern int fat_dir_empty(struct inode *dir);
extern int fat_subdirs(struct inode *dir);
extern int fat_scan(struct inode *dir, const unsigned char *name,
		    struct fat_slot_info *sinfo);
extern int fat_get_dotdot_entry(struct inode *dir, struct buffer_head **bh,
				struct msdos_dir_entry **de, loff_t *i_pos);
extern int fat_alloc_new_dir(struct inode *dir, struct timespec *ts);
extern int fat_add_entries(struct inode *dir, void *slots, int nr_slots,
			   struct fat_slot_info *sinfo);
extern int fat_remove_entries(struct inode *dir, struct fat_slot_info *sinfo);

/* fat/fatent.c */
struct fat_entry {
	int entry;
	union {
		u8 *ent12_p[2];
		__le16 *ent16_p;
		__le32 *ent32_p;
	} u;
	int nr_bhs;
	struct buffer_head *bhs[2];
};

static inline void fatent_init(struct fat_entry *fatent)
{
	fatent->nr_bhs = 0;
	fatent->entry = 0;
	fatent->u.ent32_p = NULL;
	fatent->bhs[0] = fatent->bhs[1] = NULL;
}

static inline void fatent_set_entry(struct fat_entry *fatent, int entry)
{
	fatent->entry = entry;
	fatent->u.ent32_p = NULL;
}

static inline void fatent_brelse(struct fat_entry *fatent)
{
	int i;
	fatent->u.ent32_p = NULL;
	for (i = 0; i < fatent->nr_bhs; i++)
		brelse(fatent->bhs[i]);
	fatent->nr_bhs = 0;
	fatent->bhs[0] = fatent->bhs[1] = NULL;
}

extern void fat_ent_access_init(struct super_block *sb);
extern int fat_ent_read(struct inode *inode, struct fat_entry *fatent,
			int entry);
extern int fat_ent_write(struct inode *inode, struct fat_entry *fatent,
			 int new, int wait);
extern int fat_alloc_clusters(struct inode *inode, int *cluster,
			      int nr_cluster);
extern int fat_free_clusters(struct inode *inode, int cluster);
extern int fat_count_free_clusters(struct super_block *sb);

/* fat/file.c */
extern int fat_generic_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern const struct file_operations fat_file_operations;
extern struct inode_operations fat_file_inode_operations;
extern int fat_notify_change(struct dentry * dentry, struct iattr * attr);
extern void fat_truncate(struct inode *inode);

/* fat/inode.c */
extern void fat_attach(struct inode *inode, loff_t i_pos);
extern void fat_detach(struct inode *inode);
extern struct inode *fat_iget(struct super_block *sb, loff_t i_pos);
extern struct inode *fat_build_inode(struct super_block *sb,
			struct msdos_dir_entry *de, loff_t i_pos);
extern int fat_sync_inode(struct inode *inode);
extern int fat_fill_super(struct super_block *sb, void *data, int silent,
			struct inode_operations *fs_dir_inode_ops, int isvfat);

/* fat/misc.c */
extern void fat_fs_panic(struct super_block *s, const char *fmt, ...);
extern void fat_clusters_flush(struct super_block *sb);
extern int fat_chain_add(struct inode *inode, int new_dclus, int nr_cluster);
extern int date_dos2unix(unsigned short time, unsigned short date);
extern void fat_date_unix2dos(int unix_date, __le16 *time, __le16 *date);
extern int fat_sync_bhs(struct buffer_head **bhs, int nr_bhs);

int fat_cache_init(void);
void fat_cache_destroy(void);

#endif /* __KERNEL__ */

#endif
