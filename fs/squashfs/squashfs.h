/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * squashfs.h
 */

#define TRACE(s, args...)	pr_debug("SQUASHFS: "s, ## args)

#define ERROR(s, args...)	pr_err("SQUASHFS error: "s, ## args)

#define WARNING(s, args...)	pr_warn("SQUASHFS: "s, ## args)

#ifdef CONFIG_SQUASHFS_FILE_CACHE
#define SQUASHFS_READ_PAGES msblk->max_thread_num
#else
#define SQUASHFS_READ_PAGES 0
#endif

/* block.c */
extern int squashfs_read_data(struct super_block *, u64, int, u64 *,
				struct squashfs_page_actor *);

/* cache.c */
extern struct squashfs_cache *squashfs_cache_init(char *, int, int);
extern void squashfs_cache_delete(struct squashfs_cache *);
extern struct squashfs_cache_entry *squashfs_cache_get(struct super_block *,
				struct squashfs_cache *, u64, int);
extern void squashfs_cache_put(struct squashfs_cache_entry *);
extern int squashfs_copy_data(void *, struct squashfs_cache_entry *, int, int);
extern int squashfs_read_metadata(struct super_block *, void *, u64 *,
				int *, int);
extern struct squashfs_cache_entry *squashfs_get_fragment(struct super_block *,
				u64, int);
extern struct squashfs_cache_entry *squashfs_get_datablock(struct super_block *,
				u64, int);
extern void *squashfs_read_table(struct super_block *, u64, int);

/* decompressor.c */
extern const struct squashfs_decompressor *squashfs_lookup_decompressor(int);
extern void *squashfs_decompressor_setup(struct super_block *, unsigned short);

/* decompressor_xxx.c */

struct squashfs_decompressor_thread_ops {
	void * (*create)(struct squashfs_sb_info *msblk, void *comp_opts);
	void (*destroy)(struct squashfs_sb_info *msblk);
	int (*decompress)(struct squashfs_sb_info *msblk, struct bio *bio,
			  int offset, int length, struct squashfs_page_actor *output);
	int (*max_decompressors)(void);
};

#ifdef CONFIG_SQUASHFS_DECOMP_SINGLE
extern const struct squashfs_decompressor_thread_ops squashfs_decompressor_single;
#endif
#ifdef CONFIG_SQUASHFS_DECOMP_MULTI
extern const struct squashfs_decompressor_thread_ops squashfs_decompressor_multi;
#endif
#ifdef CONFIG_SQUASHFS_DECOMP_MULTI_PERCPU
extern const struct squashfs_decompressor_thread_ops squashfs_decompressor_percpu;
#endif

/* export.c */
extern __le64 *squashfs_read_inode_lookup_table(struct super_block *, u64, u64,
				unsigned int);

/* fragment.c */
extern int squashfs_frag_lookup(struct super_block *, unsigned int, u64 *);
extern __le64 *squashfs_read_fragment_index_table(struct super_block *,
				u64, u64, unsigned int);

/* file.c */
void squashfs_copy_cache(struct folio *, struct squashfs_cache_entry *,
		size_t bytes, size_t offset);

/* file_xxx.c */
int squashfs_readpage_block(struct folio *, u64 block, int bsize, int expected);

/* id.c */
extern int squashfs_get_id(struct super_block *, unsigned int, unsigned int *);
extern __le64 *squashfs_read_id_index_table(struct super_block *, u64, u64,
				unsigned short);

/* inode.c */
extern struct inode *squashfs_iget(struct super_block *, long long,
				unsigned int);
extern int squashfs_read_inode(struct inode *, long long);

/* xattr.c */
extern ssize_t squashfs_listxattr(struct dentry *, char *, size_t);

/*
 * Inodes, files,  decompressor and xattr operations
 */

/* dir.c */
extern const struct file_operations squashfs_dir_ops;

/* export.c */
extern const struct export_operations squashfs_export_ops;

/* file.c */
extern const struct address_space_operations squashfs_aops;

/* inode.c */
extern const struct inode_operations squashfs_inode_ops;

/* namei.c */
extern const struct inode_operations squashfs_dir_inode_ops;

/* symlink.c */
extern const struct address_space_operations squashfs_symlink_aops;
extern const struct inode_operations squashfs_symlink_inode_ops;

/* xattr.c */
extern const struct xattr_handler * const squashfs_xattr_handlers[];
