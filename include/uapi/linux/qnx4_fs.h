/*
 *  Name                         : qnx4_fs.h
 *  Author                       : Richard Frowijn
 *  Function                     : qnx4 global filesystem definitions
 *  History                      : 23-03-1998 created
 */
#ifndef _LINUX_QNX4_FS_H
#define _LINUX_QNX4_FS_H

#include <linux/types.h>
#include <linux/qnxtypes.h>
#include <linux/magic.h>

#define QNX4_ROOT_INO 1

#define QNX4_MAX_XTNTS_PER_XBLK	60
/* for di_status */
#define QNX4_FILE_USED          0x01
#define QNX4_FILE_MODIFIED      0x02
#define QNX4_FILE_BUSY          0x04
#define QNX4_FILE_LINK          0x08
#define QNX4_FILE_INODE         0x10
#define QNX4_FILE_FSYSCLEAN     0x20

#define QNX4_I_MAP_SLOTS	8
#define QNX4_Z_MAP_SLOTS	64
#define QNX4_VALID_FS		0x0001	/* Clean fs. */
#define QNX4_ERROR_FS		0x0002	/* fs has errors. */
#define QNX4_BLOCK_SIZE         0x200	/* blocksize of 512 bytes */
#define QNX4_BLOCK_SIZE_BITS    9	/* blocksize shift */
#define QNX4_DIR_ENTRY_SIZE     0x040	/* dir entry size of 64 bytes */
#define QNX4_DIR_ENTRY_SIZE_BITS 6	/* dir entry size shift */
#define QNX4_XBLK_ENTRY_SIZE    0x200	/* xblk entry size */
#define QNX4_INODES_PER_BLOCK   0x08	/* 512 / 64 */

/* for filenames */
#define QNX4_SHORT_NAME_MAX	16
#define QNX4_NAME_MAX		48

/*
 * This is the original qnx4 inode layout on disk.
 */
struct qnx4_inode_entry {
	char		di_fname[QNX4_SHORT_NAME_MAX];
	qnx4_off_t	di_size;
	qnx4_xtnt_t	di_first_xtnt;
	__le32		di_xblk;
	__le32		di_ftime;
	__le32		di_mtime;
	__le32		di_atime;
	__le32		di_ctime;
	qnx4_nxtnt_t	di_num_xtnts;
	qnx4_mode_t	di_mode;
	qnx4_muid_t	di_uid;
	qnx4_mgid_t	di_gid;
	qnx4_nlink_t	di_nlink;
	__u8		di_zero[4];
	qnx4_ftype_t	di_type;
	__u8		di_status;
};

struct qnx4_link_info {
	char		dl_fname[QNX4_NAME_MAX];
	__le32		dl_inode_blk;
	__u8		dl_inode_ndx;
	__u8		dl_spare[10];
	__u8		dl_status;
};

struct qnx4_xblk {
	__le32		xblk_next_xblk;
	__le32		xblk_prev_xblk;
	__u8		xblk_num_xtnts;
	__u8		xblk_spare[3];
	__le32		xblk_num_blocks;
	qnx4_xtnt_t	xblk_xtnts[QNX4_MAX_XTNTS_PER_XBLK];
	char		xblk_signature[8];
	qnx4_xtnt_t	xblk_first_xtnt;
};

struct qnx4_super_block {
	struct qnx4_inode_entry RootDir;
	struct qnx4_inode_entry Inode;
	struct qnx4_inode_entry Boot;
	struct qnx4_inode_entry AltBoot;
};

#endif
