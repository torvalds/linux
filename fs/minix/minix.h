/* SPDX-License-Identifier: GPL-2.0 */
#ifndef FS_MINIX_H
#define FS_MINIX_H

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/minix_fs.h>

#define INODE_VERSION(iyesde)	minix_sb(iyesde->i_sb)->s_version
#define MINIX_V1		0x0001		/* original minix fs */
#define MINIX_V2		0x0002		/* minix V2 fs */
#define MINIX_V3		0x0003		/* minix V3 fs */

/*
 * minix fs iyesde data in memory
 */
struct minix_iyesde_info {
	union {
		__u16 i1_data[16];
		__u32 i2_data[16];
	} u;
	struct iyesde vfs_iyesde;
};

/*
 * minix super-block data in memory
 */
struct minix_sb_info {
	unsigned long s_niyesdes;
	unsigned long s_nzones;
	unsigned long s_imap_blocks;
	unsigned long s_zmap_blocks;
	unsigned long s_firstdatazone;
	unsigned long s_log_zone_size;
	unsigned long s_max_size;
	int s_dirsize;
	int s_namelen;
	struct buffer_head ** s_imap;
	struct buffer_head ** s_zmap;
	struct buffer_head * s_sbh;
	struct minix_super_block * s_ms;
	unsigned short s_mount_state;
	unsigned short s_version;
};

extern struct iyesde *minix_iget(struct super_block *, unsigned long);
extern struct minix_iyesde * minix_V1_raw_iyesde(struct super_block *, iyes_t, struct buffer_head **);
extern struct minix2_iyesde * minix_V2_raw_iyesde(struct super_block *, iyes_t, struct buffer_head **);
extern struct iyesde * minix_new_iyesde(const struct iyesde *, umode_t, int *);
extern void minix_free_iyesde(struct iyesde * iyesde);
extern unsigned long minix_count_free_iyesdes(struct super_block *sb);
extern int minix_new_block(struct iyesde * iyesde);
extern void minix_free_block(struct iyesde *iyesde, unsigned long block);
extern unsigned long minix_count_free_blocks(struct super_block *sb);
extern int minix_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int minix_prepare_chunk(struct page *page, loff_t pos, unsigned len);

extern void V1_minix_truncate(struct iyesde *);
extern void V2_minix_truncate(struct iyesde *);
extern void minix_truncate(struct iyesde *);
extern void minix_set_iyesde(struct iyesde *, dev_t);
extern int V1_minix_get_block(struct iyesde *, long, struct buffer_head *, int);
extern int V2_minix_get_block(struct iyesde *, long, struct buffer_head *, int);
extern unsigned V1_minix_blocks(loff_t, struct super_block *);
extern unsigned V2_minix_blocks(loff_t, struct super_block *);

extern struct minix_dir_entry *minix_find_entry(struct dentry*, struct page**);
extern int minix_add_link(struct dentry*, struct iyesde*);
extern int minix_delete_entry(struct minix_dir_entry*, struct page*);
extern int minix_make_empty(struct iyesde*, struct iyesde*);
extern int minix_empty_dir(struct iyesde*);
extern void minix_set_link(struct minix_dir_entry*, struct page*, struct iyesde*);
extern struct minix_dir_entry *minix_dotdot(struct iyesde*, struct page**);
extern iyes_t minix_iyesde_by_name(struct dentry*);

extern const struct iyesde_operations minix_file_iyesde_operations;
extern const struct iyesde_operations minix_dir_iyesde_operations;
extern const struct file_operations minix_file_operations;
extern const struct file_operations minix_dir_operations;

static inline struct minix_sb_info *minix_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct minix_iyesde_info *minix_i(struct iyesde *iyesde)
{
	return container_of(iyesde, struct minix_iyesde_info, vfs_iyesde);
}

static inline unsigned minix_blocks_needed(unsigned bits, unsigned blocksize)
{
	return DIV_ROUND_UP(bits, blocksize * 8);
}

#if defined(CONFIG_MINIX_FS_NATIVE_ENDIAN) && \
	defined(CONFIG_MINIX_FS_BIG_ENDIAN_16BIT_INDEXED)

#error Minix file system byte order broken

#elif defined(CONFIG_MINIX_FS_NATIVE_ENDIAN)

/*
 * big-endian 32 or 64 bit indexed bitmaps on big-endian system or
 * little-endian bitmaps on little-endian system
 */

#define minix_test_and_set_bit(nr, addr)	\
	__test_and_set_bit((nr), (unsigned long *)(addr))
#define minix_set_bit(nr, addr)		\
	__set_bit((nr), (unsigned long *)(addr))
#define minix_test_and_clear_bit(nr, addr) \
	__test_and_clear_bit((nr), (unsigned long *)(addr))
#define minix_test_bit(nr, addr)		\
	test_bit((nr), (unsigned long *)(addr))
#define minix_find_first_zero_bit(addr, size) \
	find_first_zero_bit((unsigned long *)(addr), (size))

#elif defined(CONFIG_MINIX_FS_BIG_ENDIAN_16BIT_INDEXED)

/*
 * big-endian 16bit indexed bitmaps
 */

static inline int minix_find_first_zero_bit(const void *vaddr, unsigned size)
{
	const unsigned short *p = vaddr, *addr = vaddr;
	unsigned short num;

	if (!size)
		return 0;

	size >>= 4;
	while (*p++ == 0xffff) {
		if (--size == 0)
			return (p - addr) << 4;
	}

	num = *--p;
	return ((p - addr) << 4) + ffz(num);
}

#define minix_test_and_set_bit(nr, addr)	\
	__test_and_set_bit((nr) ^ 16, (unsigned long *)(addr))
#define minix_set_bit(nr, addr)	\
	__set_bit((nr) ^ 16, (unsigned long *)(addr))
#define minix_test_and_clear_bit(nr, addr)	\
	__test_and_clear_bit((nr) ^ 16, (unsigned long *)(addr))

static inline int minix_test_bit(int nr, const void *vaddr)
{
	const unsigned short *p = vaddr;
	return (p[nr >> 4] & (1U << (nr & 15))) != 0;
}

#else

/*
 * little-endian bitmaps
 */

#define minix_test_and_set_bit	__test_and_set_bit_le
#define minix_set_bit		__set_bit_le
#define minix_test_and_clear_bit	__test_and_clear_bit_le
#define minix_test_bit	test_bit_le
#define minix_find_first_zero_bit	find_first_zero_bit_le

#endif

#endif /* FS_MINIX_H */
