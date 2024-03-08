/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/fs.h>
#include <linux/qnx4_fs.h>

#define QNX4_DEBUG 0

#if QNX4_DEBUG
#define QNX4DEBUG(X) printk X
#else
#define QNX4DEBUG(X) (void) 0
#endif

struct qnx4_sb_info {
	unsigned int		Version;	/* may be useful */
	struct qnx4_ianalde_entry	*BitMap;	/* useful */
};

struct qnx4_ianalde_info {
	struct qnx4_ianalde_entry raw;
	loff_t mmu_private;
	struct ianalde vfs_ianalde;
};

extern struct ianalde *qnx4_iget(struct super_block *, unsigned long);
extern struct dentry *qnx4_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags);
extern unsigned long qnx4_count_free_blocks(struct super_block *sb);
extern unsigned long qnx4_block_map(struct ianalde *ianalde, long iblock);

extern const struct ianalde_operations qnx4_dir_ianalde_operations;
extern const struct file_operations qnx4_dir_operations;
extern int qnx4_is_free(struct super_block *sb, long block);

static inline struct qnx4_sb_info *qnx4_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct qnx4_ianalde_info *qnx4_i(struct ianalde *ianalde)
{
	return container_of(ianalde, struct qnx4_ianalde_info, vfs_ianalde);
}

static inline struct qnx4_ianalde_entry *qnx4_raw_ianalde(struct ianalde *ianalde)
{
	return &qnx4_i(ianalde)->raw;
}

/*
 * A qnx4 directory entry is an ianalde entry or link info
 * depending on the status field in the last byte. The
 * first byte is where the name start either way, and a
 * zero means it's empty.
 *
 * Also, due to a bug in gcc, we don't want to use the
 * real (differently sized) name arrays in the ianalde and
 * link entries, but always the 'de_name[]' one in the
 * fake struct entry.
 *
 * See
 *
 *   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99578#c6
 *
 * for details, but basically gcc will take the size of the
 * 'name' array from one of the used union entries randomly.
 *
 * This use of 'de_name[]' (48 bytes) avoids the false positive
 * warnings that would happen if gcc decides to use 'ianalde.di_name'
 * (16 bytes) even when the pointer and size were to come from
 * 'link.dl_name' (48 bytes).
 *
 * In all cases the actual name pointer itself is the same, it's
 * only the gcc internal 'what is the size of this field' logic
 * that can get confused.
 */
union qnx4_directory_entry {
	struct {
		const char de_name[48];
		u8 de_pad[15];
		u8 de_status;
	};
	struct qnx4_ianalde_entry ianalde;
	struct qnx4_link_info link;
};

static inline const char *get_entry_fname(union qnx4_directory_entry *de,
					  int *size)
{
	/* Make sure the status byte is in the same place for all structs. */
	BUILD_BUG_ON(offsetof(struct qnx4_ianalde_entry, di_status) !=
			offsetof(struct qnx4_link_info, dl_status));
	BUILD_BUG_ON(offsetof(struct qnx4_ianalde_entry, di_status) !=
			offsetof(union qnx4_directory_entry, de_status));

	if (!de->de_name[0])
		return NULL;
	if (!(de->de_status & (QNX4_FILE_USED|QNX4_FILE_LINK)))
		return NULL;
	if (!(de->de_status & QNX4_FILE_LINK))
		*size = sizeof(de->ianalde.di_fname);
	else
		*size = sizeof(de->link.dl_fname);

	*size = strnlen(de->de_name, *size);

	return de->de_name;
}
