/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __FAST_COMMIT_H__
#define __FAST_COMMIT_H__

/* Number of blocks in journal area to allocate for fast commits */
#define EXT4_NUM_FC_BLKS		256

/* Fast commit tags */
#define EXT4_FC_TAG_ADD_RANGE		0x0001
#define EXT4_FC_TAG_DEL_RANGE		0x0002
#define EXT4_FC_TAG_CREAT		0x0003
#define EXT4_FC_TAG_LINK		0x0004
#define EXT4_FC_TAG_UNLINK		0x0005
#define EXT4_FC_TAG_INODE		0x0006
#define EXT4_FC_TAG_PAD			0x0007
#define EXT4_FC_TAG_TAIL		0x0008
#define EXT4_FC_TAG_HEAD		0x0009

#define EXT4_FC_SUPPORTED_FEATURES	0x0

/* On disk fast commit tlv value structures */

/* Fast commit on disk tag length structure */
struct ext4_fc_tl {
	__le16 fc_tag;
	__le16 fc_len;
};

/* Value structure for tag EXT4_FC_TAG_HEAD. */
struct ext4_fc_head {
	__le32 fc_features;
	__le32 fc_tid;
};

/* Value structure for EXT4_FC_TAG_ADD_RANGE. */
struct ext4_fc_add_range {
	__le32 fc_ino;
	__u8 fc_ex[12];
};

/* Value structure for tag EXT4_FC_TAG_DEL_RANGE. */
struct ext4_fc_del_range {
	__le32 fc_ino;
	__le32 fc_lblk;
	__le32 fc_len;
};

/*
 * This is the value structure for tags EXT4_FC_TAG_CREAT, EXT4_FC_TAG_LINK
 * and EXT4_FC_TAG_UNLINK.
 */
struct ext4_fc_dentry_info {
	__le32 fc_parent_ino;
	__le32 fc_ino;
	u8 fc_dname[0];
};

/* Value structure for EXT4_FC_TAG_INODE and EXT4_FC_TAG_INODE_PARTIAL. */
struct ext4_fc_inode {
	__le32 fc_ino;
	__u8 fc_raw_inode[0];
};

/* Value structure for tag EXT4_FC_TAG_TAIL. */
struct ext4_fc_tail {
	__le32 fc_tid;
	__le32 fc_crc;
};

/*
 * In memory list of dentry updates that are performed on the file
 * system used by fast commit code.
 */
struct ext4_fc_dentry_update {
	int fcd_op;		/* Type of update create / unlink / link */
	int fcd_parent;		/* Parent inode number */
	int fcd_ino;		/* Inode number */
	struct qstr fcd_name;	/* Dirent name */
	unsigned char fcd_iname[DNAME_INLINE_LEN];	/* Dirent name string */
	struct list_head fcd_list;
};

/*
 * Fast commit reason codes
 */
enum {
	/*
	 * Commit status codes:
	 */
	EXT4_FC_REASON_OK = 0,
	EXT4_FC_REASON_INELIGIBLE,
	EXT4_FC_REASON_ALREADY_COMMITTED,
	EXT4_FC_REASON_FC_START_FAILED,
	EXT4_FC_REASON_FC_FAILED,

	/*
	 * Fast commit ineligiblity reasons:
	 */
	EXT4_FC_REASON_XATTR = 0,
	EXT4_FC_REASON_CROSS_RENAME,
	EXT4_FC_REASON_JOURNAL_FLAG_CHANGE,
	EXT4_FC_REASON_MEM,
	EXT4_FC_REASON_SWAP_BOOT,
	EXT4_FC_REASON_RESIZE,
	EXT4_FC_REASON_RENAME_DIR,
	EXT4_FC_REASON_FALLOC_RANGE,
	EXT4_FC_COMMIT_FAILED,
	EXT4_FC_REASON_MAX
};

struct ext4_fc_stats {
	unsigned int fc_ineligible_reason_count[EXT4_FC_REASON_MAX];
	unsigned long fc_num_commits;
	unsigned long fc_ineligible_commits;
	unsigned long fc_numblks;
};

#define EXT4_FC_REPLAY_REALLOC_INCREMENT	4

/*
 * Physical block regions added to different inodes due to fast commit
 * recovery. These are set during the SCAN phase. During the replay phase,
 * our allocator excludes these from its allocation. This ensures that
 * we don't accidentally allocating a block that is going to be used by
 * another inode.
 */
struct ext4_fc_alloc_region {
	ext4_lblk_t lblk;
	ext4_fsblk_t pblk;
	int ino, len;
};

/*
 * Fast commit replay state.
 */
struct ext4_fc_replay_state {
	int fc_replay_num_tags;
	int fc_replay_expected_off;
	int fc_current_pass;
	int fc_cur_tag;
	int fc_crc;
	struct ext4_fc_alloc_region *fc_regions;
	int fc_regions_size, fc_regions_used, fc_regions_valid;
	int *fc_modified_inodes;
	int fc_modified_inodes_used, fc_modified_inodes_size;
};

#define region_last(__region) (((__region)->lblk) + ((__region)->len) - 1)

#define fc_for_each_tl(__start, __end, __tl)				\
	for (tl = (struct ext4_fc_tl *)start;				\
		(u8 *)tl < (u8 *)end;					\
		tl = (struct ext4_fc_tl *)((u8 *)tl +			\
					sizeof(struct ext4_fc_tl) +	\
					+ le16_to_cpu(tl->fc_len)))


#endif /* __FAST_COMMIT_H__ */
