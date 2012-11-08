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

struct extent_status {
	struct rb_node rb_node;
	ext4_lblk_t start;	/* first block extent covers */
	ext4_lblk_t len;	/* length of extent in block */
};

struct ext4_es_tree {
	struct rb_root root;
	struct extent_status *cache_es;	/* recently accessed extent */
};

#endif /* _EXT4_EXTENTS_STATUS_H */
