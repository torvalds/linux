/*
 *  fs/ext4/extents_status.h
 *
 * Written by Yongqiang Yang <xiaoqiangnk@gmail.com>
 * Modified by
 *	Allison Henderson <achender@linux.vnet.ibm.com>
 *	Zheng Liu <wenqing.lz@taobao.com>
 *
 */

#ifndef _EXT4_EXTENTS_STATUS_H
#define _EXT4_EXTENTS_STATUS_H

/*
 * Turn on ES_DEBUG__ to get lots of info about extent status operations.
 */
#ifdef ES_DEBUG__
#define es_debug(fmt, ...)	printk(fmt, ##__VA_ARGS__)
#else
#define es_debug(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif

struct extent_status {
	struct rb_node rb_node;
	ext4_lblk_t start;	/* first block extent covers */
	ext4_lblk_t len;	/* length of extent in block */
};

struct ext4_es_tree {
	struct rb_root root;
	struct extent_status *cache_es;	/* recently accessed extent */
};

extern int __init ext4_init_es(void);
extern void ext4_exit_es(void);
extern void ext4_es_init_tree(struct ext4_es_tree *tree);

extern int ext4_es_insert_extent(struct inode *inode, ext4_lblk_t start,
				 ext4_lblk_t len);
extern int ext4_es_remove_extent(struct inode *inode, ext4_lblk_t start,
				 ext4_lblk_t len);
extern ext4_lblk_t ext4_es_find_extent(struct inode *inode,
				struct extent_status *es);

#endif /* _EXT4_EXTENTS_STATUS_H */
