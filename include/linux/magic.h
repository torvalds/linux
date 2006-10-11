#ifndef __LINUX_MAGIC_H__
#define __LINUX_MAGIC_H__

#define ADFS_SUPER_MAGIC	0xadf5
#define AFFS_SUPER_MAGIC	0xadff
#define AUTOFS_SUPER_MAGIC	0x0187
#define CODA_SUPER_MAGIC	0x73757245
#define EFS_SUPER_MAGIC		0x414A53
#define EXT2_SUPER_MAGIC	0xEF53
#define EXT3_SUPER_MAGIC	0xEF53
#define EXT4_SUPER_MAGIC	0xEF53
#define HPFS_SUPER_MAGIC	0xf995e849
#define ISOFS_SUPER_MAGIC	0x9660
#define JFFS2_SUPER_MAGIC	0x72b6

#define MINIX_SUPER_MAGIC	0x137F		/* original minix fs */
#define MINIX_SUPER_MAGIC2	0x138F		/* minix fs, 30 char names */
#define MINIX2_SUPER_MAGIC	0x2468		/* minix V2 fs */
#define MINIX2_SUPER_MAGIC2	0x2478		/* minix V2 fs, 30 char names */

#define MSDOS_SUPER_MAGIC	0x4d44		/* MD */
#define NCP_SUPER_MAGIC		0x564c		/* Guess, what 0x564c is :-) */
#define NFS_SUPER_MAGIC		0x6969
#define OPENPROM_SUPER_MAGIC	0x9fa1
#define PROC_SUPER_MAGIC	0x9fa0
#define QNX4_SUPER_MAGIC	0x002f		/* qnx4 fs detection */

#define REISERFS_SUPER_MAGIC	0x52654973	/* used by gcc */
					/* used by file system utilities that
	                                   look at the superblock, etc.  */
#define REISERFS_SUPER_MAGIC_STRING	"ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING	"ReIsEr2Fs"
#define REISER2FS_JR_SUPER_MAGIC_STRING	"ReIsEr3Fs"

#define SMB_SUPER_MAGIC		0x517B
#define USBDEVICE_SUPER_MAGIC	0x9fa2

#endif /* __LINUX_MAGIC_H__ */
