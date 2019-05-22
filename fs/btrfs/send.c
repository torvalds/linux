// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Alexander Block.  All rights reserved.
 */

#include <linux/bsearch.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sort.h>
#include <linux/mount.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/radix-tree.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/compat.h>
#include <linux/crc32c.h>

#include "send.h"
#include "backref.h"
#include "locking.h"
#include "disk-io.h"
#include "btrfs_inode.h"
#include "transaction.h"
#include "compression.h"

/*
 * A fs_path is a helper to dynamically build path names with unknown size.
 * It reallocates the internal buffer on demand.
 * It allows fast adding of path elements on the right side (normal path) and
 * fast adding to the left side (reversed path). A reversed path can also be
 * unreversed if needed.
 */
struct fs_path {
	union {
		struct {
			char *start;
			char *end;

			char *buf;
			unsigned short buf_len:15;
			unsigned short reversed:1;
			char inline_buf[];
		};
		/*
		 * Average path length does not exceed 200 bytes, we'll have
		 * better packing in the slab and higher chance to satisfy
		 * a allocation later during send.
		 */
		char pad[256];
	};
};
#define FS_PATH_INLINE_SIZE \
	(sizeof(struct fs_path) - offsetof(struct fs_path, inline_buf))


/* reused for each extent */
struct clone_root {
	struct btrfs_root *root;
	u64 ino;
	u64 offset;

	u64 found_refs;
};

#define SEND_CTX_MAX_NAME_CACHE_SIZE 128
#define SEND_CTX_NAME_CACHE_CLEAN_SIZE (SEND_CTX_MAX_NAME_CACHE_SIZE * 2)

struct send_ctx {
	struct file *send_filp;
	loff_t send_off;
	char *send_buf;
	u32 send_size;
	u32 send_max_size;
	u64 total_send_size;
	u64 cmd_send_size[BTRFS_SEND_C_MAX + 1];
	u64 flags;	/* 'flags' member of btrfs_ioctl_send_args is u64 */

	struct btrfs_root *send_root;
	struct btrfs_root *parent_root;
	struct clone_root *clone_roots;
	int clone_roots_cnt;

	/* current state of the compare_tree call */
	struct btrfs_path *left_path;
	struct btrfs_path *right_path;
	struct btrfs_key *cmp_key;

	/*
	 * infos of the currently processed inode. In case of deleted inodes,
	 * these are the values from the deleted inode.
	 */
	u64 cur_ino;
	u64 cur_inode_gen;
	int cur_inode_new;
	int cur_inode_new_gen;
	int cur_inode_deleted;
	u64 cur_inode_size;
	u64 cur_inode_mode;
	u64 cur_inode_rdev;
	u64 cur_inode_last_extent;
	u64 cur_inode_next_write_offset;
	bool ignore_cur_inode;

	u64 send_progress;

	struct list_head new_refs;
	struct list_head deleted_refs;

	struct radix_tree_root name_cache;
	struct list_head name_cache_list;
	int name_cache_size;

	struct file_ra_state ra;

	char *read_buf;

	/*
	 * We process inodes by their increasing order, so if before an
	 * incremental send we reverse the parent/child relationship of
	 * directories such that a directory with a lower inode number was
	 * the parent of a directory with a higher inode number, and the one
	 * becoming the new parent got renamed too, we can't rename/move the
	 * directory with lower inode number when we finish processing it - we
	 * must process the directory with higher inode number first, then
	 * rename/move it and then rename/move the directory with lower inode
	 * number. Example follows.
	 *
	 * Tree state when the first send was performed:
	 *
	 * .
	 * |-- a                   (ino 257)
	 *     |-- b               (ino 258)
	 *         |
	 *         |
	 *         |-- c           (ino 259)
	 *         |   |-- d       (ino 260)
	 *         |
	 *         |-- c2          (ino 261)
	 *
	 * Tree state when the second (incremental) send is performed:
	 *
	 * .
	 * |-- a                   (ino 257)
	 *     |-- b               (ino 258)
	 *         |-- c2          (ino 261)
	 *             |-- d2      (ino 260)
	 *                 |-- cc  (ino 259)
	 *
	 * The sequence of steps that lead to the second state was:
	 *
	 * mv /a/b/c/d /a/b/c2/d2
	 * mv /a/b/c /a/b/c2/d2/cc
	 *
	 * "c" has lower inode number, but we can't move it (2nd mv operation)
	 * before we move "d", which has higher inode number.
	 *
	 * So we just memorize which move/rename operations must be performed
	 * later when their respective parent is processed and moved/renamed.
	 */

	/* Indexed by parent directory inode number. */
	struct rb_root pending_dir_moves;

	/*
	 * Reverse index, indexed by the inode number of a directory that
	 * is waiting for the move/rename of its immediate parent before its
	 * own move/rename can be performed.
	 */
	struct rb_root waiting_dir_moves;

	/*
	 * A directory that is going to be rm'ed might have a child directory
	 * which is in the pending directory moves index above. In this case,
	 * the directory can only be removed after the move/rename of its child
	 * is performed. Example:
	 *
	 * Parent snapshot:
	 *
	 * .                        (ino 256)
	 * |-- a/                   (ino 257)
	 *     |-- b/               (ino 258)
	 *         |-- c/           (ino 259)
	 *         |   |-- x/       (ino 260)
	 *         |
	 *         |-- y/           (ino 261)
	 *
	 * Send snapshot:
	 *
	 * .                        (ino 256)
	 * |-- a/                   (ino 257)
	 *     |-- b/               (ino 258)
	 *         |-- YY/          (ino 261)
	 *              |-- x/      (ino 260)
	 *
	 * Sequence of steps that lead to the send snapshot:
	 * rm -f /a/b/c/foo.txt
	 * mv /a/b/y /a/b/YY
	 * mv /a/b/c/x /a/b/YY
	 * rmdir /a/b/c
	 *
	 * When the child is processed, its move/rename is delayed until its
	 * parent is processed (as explained above), but all other operations
	 * like update utimes, chown, chgrp, etc, are performed and the paths
	 * that it uses for those operations must use the orphanized name of
	 * its parent (the directory we're going to rm later), so we need to
	 * memorize that name.
	 *
	 * Indexed by the inode number of the directory to be deleted.
	 */
	struct rb_root orphan_dirs;
};

struct pending_dir_move {
	struct rb_node node;
	struct list_head list;
	u64 parent_ino;
	u64 ino;
	u64 gen;
	struct list_head update_refs;
};

struct waiting_dir_move {
	struct rb_node node;
	u64 ino;
	/*
	 * There might be some directory that could not be removed because it
	 * was waiting for this directory inode to be moved first. Therefore
	 * after this directory is moved, we can try to rmdir the ino rmdir_ino.
	 */
	u64 rmdir_ino;
	bool orphanized;
};

struct orphan_dir_info {
	struct rb_node node;
	u64 ino;
	u64 gen;
	u64 last_dir_index_offset;
};

struct name_cache_entry {
	struct list_head list;
	/*
	 * radix_tree has only 32bit entries but we need to handle 64bit inums.
	 * We use the lower 32bit of the 64bit inum to store it in the tree. If
	 * more then one inum would fall into the same entry, we use radix_list
	 * to store the additional entries. radix_list is also used to store
	 * entries where two entries have the same inum but different
	 * generations.
	 */
	struct list_head radix_list;
	u64 ino;
	u64 gen;
	u64 parent_ino;
	u64 parent_gen;
	int ret;
	int need_later_update;
	int name_len;
	char name[];
};

__cold
static void inconsistent_snapshot_error(struct send_ctx *sctx,
					enum btrfs_compare_tree_result result,
					const char *what)
{
	const char *result_string;

	switch (result) {
	case BTRFS_COMPARE_TREE_NEW:
		result_string = "new";
		break;
	case BTRFS_COMPARE_TREE_DELETED:
		result_string = "deleted";
		break;
	case BTRFS_COMPARE_TREE_CHANGED:
		result_string = "updated";
		break;
	case BTRFS_COMPARE_TREE_SAME:
		ASSERT(0);
		result_string = "unchanged";
		break;
	default:
		ASSERT(0);
		result_string = "unexpected";
	}

	btrfs_err(sctx->send_root->fs_info,
		  "Send: inconsistent snapshot, found %s %s for inode %llu without updated inode item, send root is %llu, parent root is %llu",
		  result_string, what, sctx->cmp_key->objectid,
		  sctx->send_root->root_key.objectid,
		  (sctx->parent_root ?
		   sctx->parent_root->root_key.objectid : 0));
}

static int is_waiting_for_move(struct send_ctx *sctx, u64 ino);

static struct waiting_dir_move *
get_waiting_dir_move(struct send_ctx *sctx, u64 ino);

static int is_waiting_for_rm(struct send_ctx *sctx, u64 dir_ino);

static int need_send_hole(struct send_ctx *sctx)
{
	return (sctx->parent_root && !sctx->cur_inode_new &&
		!sctx->cur_inode_new_gen && !sctx->cur_inode_deleted &&
		S_ISREG(sctx->cur_inode_mode));
}

static void fs_path_reset(struct fs_path *p)
{
	if (p->reversed) {
		p->start = p->buf + p->buf_len - 1;
		p->end = p->start;
		*p->start = 0;
	} else {
		p->start = p->buf;
		p->end = p->start;
		*p->start = 0;
	}
}

static struct fs_path *fs_path_alloc(void)
{
	struct fs_path *p;

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return NULL;
	p->reversed = 0;
	p->buf = p->inline_buf;
	p->buf_len = FS_PATH_INLINE_SIZE;
	fs_path_reset(p);
	return p;
}

static struct fs_path *fs_path_alloc_reversed(void)
{
	struct fs_path *p;

	p = fs_path_alloc();
	if (!p)
		return NULL;
	p->reversed = 1;
	fs_path_reset(p);
	return p;
}

static void fs_path_free(struct fs_path *p)
{
	if (!p)
		return;
	if (p->buf != p->inline_buf)
		kfree(p->buf);
	kfree(p);
}

static int fs_path_len(struct fs_path *p)
{
	return p->end - p->start;
}

static int fs_path_ensure_buf(struct fs_path *p, int len)
{
	char *tmp_buf;
	int path_len;
	int old_buf_len;

	len++;

	if (p->buf_len >= len)
		return 0;

	if (len > PATH_MAX) {
		WARN_ON(1);
		return -ENOMEM;
	}

	path_len = p->end - p->start;
	old_buf_len = p->buf_len;

	/*
	 * First time the inline_buf does not suffice
	 */
	if (p->buf == p->inline_buf) {
		tmp_buf = kmalloc(len, GFP_KERNEL);
		if (tmp_buf)
			memcpy(tmp_buf, p->buf, old_buf_len);
	} else {
		tmp_buf = krealloc(p->buf, len, GFP_KERNEL);
	}
	if (!tmp_buf)
		return -ENOMEM;
	p->buf = tmp_buf;
	/*
	 * The real size of the buffer is bigger, this will let the fast path
	 * happen most of the time
	 */
	p->buf_len = ksize(p->buf);

	if (p->reversed) {
		tmp_buf = p->buf + old_buf_len - path_len - 1;
		p->end = p->buf + p->buf_len - 1;
		p->start = p->end - path_len;
		memmove(p->start, tmp_buf, path_len + 1);
	} else {
		p->start = p->buf;
		p->end = p->start + path_len;
	}
	return 0;
}

static int fs_path_prepare_for_add(struct fs_path *p, int name_len,
				   char **prepared)
{
	int ret;
	int new_len;

	new_len = p->end - p->start + name_len;
	if (p->start != p->end)
		new_len++;
	ret = fs_path_ensure_buf(p, new_len);
	if (ret < 0)
		goto out;

	if (p->reversed) {
		if (p->start != p->end)
			*--p->start = '/';
		p->start -= name_len;
		*prepared = p->start;
	} else {
		if (p->start != p->end)
			*p->end++ = '/';
		*prepared = p->end;
		p->end += name_len;
		*p->end = 0;
	}

out:
	return ret;
}

static int fs_path_add(struct fs_path *p, const char *name, int name_len)
{
	int ret;
	char *prepared;

	ret = fs_path_prepare_for_add(p, name_len, &prepared);
	if (ret < 0)
		goto out;
	memcpy(prepared, name, name_len);

out:
	return ret;
}

static int fs_path_add_path(struct fs_path *p, struct fs_path *p2)
{
	int ret;
	char *prepared;

	ret = fs_path_prepare_for_add(p, p2->end - p2->start, &prepared);
	if (ret < 0)
		goto out;
	memcpy(prepared, p2->start, p2->end - p2->start);

out:
	return ret;
}

static int fs_path_add_from_extent_buffer(struct fs_path *p,
					  struct extent_buffer *eb,
					  unsigned long off, int len)
{
	int ret;
	char *prepared;

	ret = fs_path_prepare_for_add(p, len, &prepared);
	if (ret < 0)
		goto out;

	read_extent_buffer(eb, prepared, off, len);

out:
	return ret;
}

static int fs_path_copy(struct fs_path *p, struct fs_path *from)
{
	int ret;

	p->reversed = from->reversed;
	fs_path_reset(p);

	ret = fs_path_add_path(p, from);

	return ret;
}


static void fs_path_unreverse(struct fs_path *p)
{
	char *tmp;
	int len;

	if (!p->reversed)
		return;

	tmp = p->start;
	len = p->end - p->start;
	p->start = p->buf;
	p->end = p->start + len;
	memmove(p->start, tmp, len + 1);
	p->reversed = 0;
}

static struct btrfs_path *alloc_path_for_send(void)
{
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	if (!path)
		return NULL;
	path->search_commit_root = 1;
	path->skip_locking = 1;
	path->need_commit_sem = 1;
	return path;
}

static int write_buf(struct file *filp, const void *buf, u32 len, loff_t *off)
{
	int ret;
	u32 pos = 0;

	while (pos < len) {
		ret = kernel_write(filp, buf + pos, len - pos, off);
		/* TODO handle that correctly */
		/*if (ret == -ERESTARTSYS) {
			continue;
		}*/
		if (ret < 0)
			return ret;
		if (ret == 0) {
			return -EIO;
		}
		pos += ret;
	}

	return 0;
}

static int tlv_put(struct send_ctx *sctx, u16 attr, const void *data, int len)
{
	struct btrfs_tlv_header *hdr;
	int total_len = sizeof(*hdr) + len;
	int left = sctx->send_max_size - sctx->send_size;

	if (unlikely(left < total_len))
		return -EOVERFLOW;

	hdr = (struct btrfs_tlv_header *) (sctx->send_buf + sctx->send_size);
	hdr->tlv_type = cpu_to_le16(attr);
	hdr->tlv_len = cpu_to_le16(len);
	memcpy(hdr + 1, data, len);
	sctx->send_size += total_len;

	return 0;
}

#define TLV_PUT_DEFINE_INT(bits) \
	static int tlv_put_u##bits(struct send_ctx *sctx,	 	\
			u##bits attr, u##bits value)			\
	{								\
		__le##bits __tmp = cpu_to_le##bits(value);		\
		return tlv_put(sctx, attr, &__tmp, sizeof(__tmp));	\
	}

TLV_PUT_DEFINE_INT(64)

static int tlv_put_string(struct send_ctx *sctx, u16 attr,
			  const char *str, int len)
{
	if (len == -1)
		len = strlen(str);
	return tlv_put(sctx, attr, str, len);
}

static int tlv_put_uuid(struct send_ctx *sctx, u16 attr,
			const u8 *uuid)
{
	return tlv_put(sctx, attr, uuid, BTRFS_UUID_SIZE);
}

static int tlv_put_btrfs_timespec(struct send_ctx *sctx, u16 attr,
				  struct extent_buffer *eb,
				  struct btrfs_timespec *ts)
{
	struct btrfs_timespec bts;
	read_extent_buffer(eb, &bts, (unsigned long)ts, sizeof(bts));
	return tlv_put(sctx, attr, &bts, sizeof(bts));
}


#define TLV_PUT(sctx, attrtype, data, attrlen) \
	do { \
		ret = tlv_put(sctx, attrtype, data, attrlen); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)

#define TLV_PUT_INT(sctx, attrtype, bits, value) \
	do { \
		ret = tlv_put_u##bits(sctx, attrtype, value); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)

#define TLV_PUT_U8(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 8, data)
#define TLV_PUT_U16(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 16, data)
#define TLV_PUT_U32(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 32, data)
#define TLV_PUT_U64(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 64, data)
#define TLV_PUT_STRING(sctx, attrtype, str, len) \
	do { \
		ret = tlv_put_string(sctx, attrtype, str, len); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)
#define TLV_PUT_PATH(sctx, attrtype, p) \
	do { \
		ret = tlv_put_string(sctx, attrtype, p->start, \
			p->end - p->start); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while(0)
#define TLV_PUT_UUID(sctx, attrtype, uuid) \
	do { \
		ret = tlv_put_uuid(sctx, attrtype, uuid); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)
#define TLV_PUT_BTRFS_TIMESPEC(sctx, attrtype, eb, ts) \
	do { \
		ret = tlv_put_btrfs_timespec(sctx, attrtype, eb, ts); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)

static int send_header(struct send_ctx *sctx)
{
	struct btrfs_stream_header hdr;

	strcpy(hdr.magic, BTRFS_SEND_STREAM_MAGIC);
	hdr.version = cpu_to_le32(BTRFS_SEND_STREAM_VERSION);

	return write_buf(sctx->send_filp, &hdr, sizeof(hdr),
					&sctx->send_off);
}

/*
 * For each command/item we want to send to userspace, we call this function.
 */
static int begin_cmd(struct send_ctx *sctx, int cmd)
{
	struct btrfs_cmd_header *hdr;

	if (WARN_ON(!sctx->send_buf))
		return -EINVAL;

	BUG_ON(sctx->send_size);

	sctx->send_size += sizeof(*hdr);
	hdr = (struct btrfs_cmd_header *)sctx->send_buf;
	hdr->cmd = cpu_to_le16(cmd);

	return 0;
}

static int send_cmd(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_cmd_header *hdr;
	u32 crc;

	hdr = (struct btrfs_cmd_header *)sctx->send_buf;
	hdr->len = cpu_to_le32(sctx->send_size - sizeof(*hdr));
	hdr->crc = 0;

	crc = btrfs_crc32c(0, (unsigned char *)sctx->send_buf, sctx->send_size);
	hdr->crc = cpu_to_le32(crc);

	ret = write_buf(sctx->send_filp, sctx->send_buf, sctx->send_size,
					&sctx->send_off);

	sctx->total_send_size += sctx->send_size;
	sctx->cmd_send_size[le16_to_cpu(hdr->cmd)] += sctx->send_size;
	sctx->send_size = 0;

	return ret;
}

/*
 * Sends a move instruction to user space
 */
static int send_rename(struct send_ctx *sctx,
		     struct fs_path *from, struct fs_path *to)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret;

	btrfs_debug(fs_info, "send_rename %s -> %s", from->start, to->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_RENAME);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, from);
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH_TO, to);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Sends a link instruction to user space
 */
static int send_link(struct send_ctx *sctx,
		     struct fs_path *path, struct fs_path *lnk)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret;

	btrfs_debug(fs_info, "send_link %s -> %s", path->start, lnk->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_LINK);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH_LINK, lnk);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Sends an unlink instruction to user space
 */
static int send_unlink(struct send_ctx *sctx, struct fs_path *path)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret;

	btrfs_debug(fs_info, "send_unlink %s", path->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_UNLINK);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Sends a rmdir instruction to user space
 */
static int send_rmdir(struct send_ctx *sctx, struct fs_path *path)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret;

	btrfs_debug(fs_info, "send_rmdir %s", path->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_RMDIR);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Helper function to retrieve some fields from an inode item.
 */
static int __get_inode_info(struct btrfs_root *root, struct btrfs_path *path,
			  u64 ino, u64 *size, u64 *gen, u64 *mode, u64 *uid,
			  u64 *gid, u64 *rdev)
{
	int ret;
	struct btrfs_inode_item *ii;
	struct btrfs_key key;

	key.objectid = ino;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		return ret;
	}

	ii = btrfs_item_ptr(path->nodes[0], path->slots[0],
			struct btrfs_inode_item);
	if (size)
		*size = btrfs_inode_size(path->nodes[0], ii);
	if (gen)
		*gen = btrfs_inode_generation(path->nodes[0], ii);
	if (mode)
		*mode = btrfs_inode_mode(path->nodes[0], ii);
	if (uid)
		*uid = btrfs_inode_uid(path->nodes[0], ii);
	if (gid)
		*gid = btrfs_inode_gid(path->nodes[0], ii);
	if (rdev)
		*rdev = btrfs_inode_rdev(path->nodes[0], ii);

	return ret;
}

static int get_inode_info(struct btrfs_root *root,
			  u64 ino, u64 *size, u64 *gen,
			  u64 *mode, u64 *uid, u64 *gid,
			  u64 *rdev)
{
	struct btrfs_path *path;
	int ret;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;
	ret = __get_inode_info(root, path, ino, size, gen, mode, uid, gid,
			       rdev);
	btrfs_free_path(path);
	return ret;
}

typedef int (*iterate_inode_ref_t)(int num, u64 dir, int index,
				   struct fs_path *p,
				   void *ctx);

/*
 * Helper function to iterate the entries in ONE btrfs_inode_ref or
 * btrfs_inode_extref.
 * The iterate callback may return a non zero value to stop iteration. This can
 * be a negative value for error codes or 1 to simply stop it.
 *
 * path must point to the INODE_REF or INODE_EXTREF when called.
 */
static int iterate_inode_ref(struct btrfs_root *root, struct btrfs_path *path,
			     struct btrfs_key *found_key, int resolve,
			     iterate_inode_ref_t iterate, void *ctx)
{
	struct extent_buffer *eb = path->nodes[0];
	struct btrfs_item *item;
	struct btrfs_inode_ref *iref;
	struct btrfs_inode_extref *extref;
	struct btrfs_path *tmp_path;
	struct fs_path *p;
	u32 cur = 0;
	u32 total;
	int slot = path->slots[0];
	u32 name_len;
	char *start;
	int ret = 0;
	int num = 0;
	int index;
	u64 dir;
	unsigned long name_off;
	unsigned long elem_size;
	unsigned long ptr;

	p = fs_path_alloc_reversed();
	if (!p)
		return -ENOMEM;

	tmp_path = alloc_path_for_send();
	if (!tmp_path) {
		fs_path_free(p);
		return -ENOMEM;
	}


	if (found_key->type == BTRFS_INODE_REF_KEY) {
		ptr = (unsigned long)btrfs_item_ptr(eb, slot,
						    struct btrfs_inode_ref);
		item = btrfs_item_nr(slot);
		total = btrfs_item_size(eb, item);
		elem_size = sizeof(*iref);
	} else {
		ptr = btrfs_item_ptr_offset(eb, slot);
		total = btrfs_item_size_nr(eb, slot);
		elem_size = sizeof(*extref);
	}

	while (cur < total) {
		fs_path_reset(p);

		if (found_key->type == BTRFS_INODE_REF_KEY) {
			iref = (struct btrfs_inode_ref *)(ptr + cur);
			name_len = btrfs_inode_ref_name_len(eb, iref);
			name_off = (unsigned long)(iref + 1);
			index = btrfs_inode_ref_index(eb, iref);
			dir = found_key->offset;
		} else {
			extref = (struct btrfs_inode_extref *)(ptr + cur);
			name_len = btrfs_inode_extref_name_len(eb, extref);
			name_off = (unsigned long)&extref->name;
			index = btrfs_inode_extref_index(eb, extref);
			dir = btrfs_inode_extref_parent(eb, extref);
		}

		if (resolve) {
			start = btrfs_ref_to_path(root, tmp_path, name_len,
						  name_off, eb, dir,
						  p->buf, p->buf_len);
			if (IS_ERR(start)) {
				ret = PTR_ERR(start);
				goto out;
			}
			if (start < p->buf) {
				/* overflow , try again with larger buffer */
				ret = fs_path_ensure_buf(p,
						p->buf_len + p->buf - start);
				if (ret < 0)
					goto out;
				start = btrfs_ref_to_path(root, tmp_path,
							  name_len, name_off,
							  eb, dir,
							  p->buf, p->buf_len);
				if (IS_ERR(start)) {
					ret = PTR_ERR(start);
					goto out;
				}
				BUG_ON(start < p->buf);
			}
			p->start = start;
		} else {
			ret = fs_path_add_from_extent_buffer(p, eb, name_off,
							     name_len);
			if (ret < 0)
				goto out;
		}

		cur += elem_size + name_len;
		ret = iterate(num, dir, index, p, ctx);
		if (ret)
			goto out;
		num++;
	}

out:
	btrfs_free_path(tmp_path);
	fs_path_free(p);
	return ret;
}

typedef int (*iterate_dir_item_t)(int num, struct btrfs_key *di_key,
				  const char *name, int name_len,
				  const char *data, int data_len,
				  u8 type, void *ctx);

/*
 * Helper function to iterate the entries in ONE btrfs_dir_item.
 * The iterate callback may return a non zero value to stop iteration. This can
 * be a negative value for error codes or 1 to simply stop it.
 *
 * path must point to the dir item when called.
 */
static int iterate_dir_item(struct btrfs_root *root, struct btrfs_path *path,
			    iterate_dir_item_t iterate, void *ctx)
{
	int ret = 0;
	struct extent_buffer *eb;
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_key di_key;
	char *buf = NULL;
	int buf_len;
	u32 name_len;
	u32 data_len;
	u32 cur;
	u32 len;
	u32 total;
	int slot;
	int num;
	u8 type;

	/*
	 * Start with a small buffer (1 page). If later we end up needing more
	 * space, which can happen for xattrs on a fs with a leaf size greater
	 * then the page size, attempt to increase the buffer. Typically xattr
	 * values are small.
	 */
	buf_len = PATH_MAX;
	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	eb = path->nodes[0];
	slot = path->slots[0];
	item = btrfs_item_nr(slot);
	di = btrfs_item_ptr(eb, slot, struct btrfs_dir_item);
	cur = 0;
	len = 0;
	total = btrfs_item_size(eb, item);

	num = 0;
	while (cur < total) {
		name_len = btrfs_dir_name_len(eb, di);
		data_len = btrfs_dir_data_len(eb, di);
		type = btrfs_dir_type(eb, di);
		btrfs_dir_item_key_to_cpu(eb, di, &di_key);

		if (type == BTRFS_FT_XATTR) {
			if (name_len > XATTR_NAME_MAX) {
				ret = -ENAMETOOLONG;
				goto out;
			}
			if (name_len + data_len >
					BTRFS_MAX_XATTR_SIZE(root->fs_info)) {
				ret = -E2BIG;
				goto out;
			}
		} else {
			/*
			 * Path too long
			 */
			if (name_len + data_len > PATH_MAX) {
				ret = -ENAMETOOLONG;
				goto out;
			}
		}

		if (name_len + data_len > buf_len) {
			buf_len = name_len + data_len;
			if (is_vmalloc_addr(buf)) {
				vfree(buf);
				buf = NULL;
			} else {
				char *tmp = krealloc(buf, buf_len,
						GFP_KERNEL | __GFP_NOWARN);

				if (!tmp)
					kfree(buf);
				buf = tmp;
			}
			if (!buf) {
				buf = kvmalloc(buf_len, GFP_KERNEL);
				if (!buf) {
					ret = -ENOMEM;
					goto out;
				}
			}
		}

		read_extent_buffer(eb, buf, (unsigned long)(di + 1),
				name_len + data_len);

		len = sizeof(*di) + name_len + data_len;
		di = (struct btrfs_dir_item *)((char *)di + len);
		cur += len;

		ret = iterate(num, &di_key, buf, name_len, buf + name_len,
				data_len, type, ctx);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}

		num++;
	}

out:
	kvfree(buf);
	return ret;
}

static int __copy_first_ref(int num, u64 dir, int index,
			    struct fs_path *p, void *ctx)
{
	int ret;
	struct fs_path *pt = ctx;

	ret = fs_path_copy(pt, p);
	if (ret < 0)
		return ret;

	/* we want the first only */
	return 1;
}

/*
 * Retrieve the first path of an inode. If an inode has more then one
 * ref/hardlink, this is ignored.
 */
static int get_inode_path(struct btrfs_root *root,
			  u64 ino, struct fs_path *path)
{
	int ret;
	struct btrfs_key key, found_key;
	struct btrfs_path *p;

	p = alloc_path_for_send();
	if (!p)
		return -ENOMEM;

	fs_path_reset(path);

	key.objectid = ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(root, &key, p, 1, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = 1;
		goto out;
	}
	btrfs_item_key_to_cpu(p->nodes[0], &found_key, p->slots[0]);
	if (found_key.objectid != ino ||
	    (found_key.type != BTRFS_INODE_REF_KEY &&
	     found_key.type != BTRFS_INODE_EXTREF_KEY)) {
		ret = -ENOENT;
		goto out;
	}

	ret = iterate_inode_ref(root, p, &found_key, 1,
				__copy_first_ref, path);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	btrfs_free_path(p);
	return ret;
}

struct backref_ctx {
	struct send_ctx *sctx;

	/* number of total found references */
	u64 found;

	/*
	 * used for clones found in send_root. clones found behind cur_objectid
	 * and cur_offset are not considered as allowed clones.
	 */
	u64 cur_objectid;
	u64 cur_offset;

	/* may be truncated in case it's the last extent in a file */
	u64 extent_len;

	/* data offset in the file extent item */
	u64 data_offset;

	/* Just to check for bugs in backref resolving */
	int found_itself;
};

static int __clone_root_cmp_bsearch(const void *key, const void *elt)
{
	u64 root = (u64)(uintptr_t)key;
	struct clone_root *cr = (struct clone_root *)elt;

	if (root < cr->root->root_key.objectid)
		return -1;
	if (root > cr->root->root_key.objectid)
		return 1;
	return 0;
}

static int __clone_root_cmp_sort(const void *e1, const void *e2)
{
	struct clone_root *cr1 = (struct clone_root *)e1;
	struct clone_root *cr2 = (struct clone_root *)e2;

	if (cr1->root->root_key.objectid < cr2->root->root_key.objectid)
		return -1;
	if (cr1->root->root_key.objectid > cr2->root->root_key.objectid)
		return 1;
	return 0;
}

/*
 * Called for every backref that is found for the current extent.
 * Results are collected in sctx->clone_roots->ino/offset/found_refs
 */
static int __iterate_backrefs(u64 ino, u64 offset, u64 root, void *ctx_)
{
	struct backref_ctx *bctx = ctx_;
	struct clone_root *found;

	/* First check if the root is in the list of accepted clone sources */
	found = bsearch((void *)(uintptr_t)root, bctx->sctx->clone_roots,
			bctx->sctx->clone_roots_cnt,
			sizeof(struct clone_root),
			__clone_root_cmp_bsearch);
	if (!found)
		return 0;

	if (found->root == bctx->sctx->send_root &&
	    ino == bctx->cur_objectid &&
	    offset == bctx->cur_offset) {
		bctx->found_itself = 1;
	}

	/*
	 * Make sure we don't consider clones from send_root that are
	 * behind the current inode/offset.
	 */
	if (found->root == bctx->sctx->send_root) {
		/*
		 * TODO for the moment we don't accept clones from the inode
		 * that is currently send. We may change this when
		 * BTRFS_IOC_CLONE_RANGE supports cloning from and to the same
		 * file.
		 */
		if (ino >= bctx->cur_objectid)
			return 0;
	}

	bctx->found++;
	found->found_refs++;
	if (ino < found->ino) {
		found->ino = ino;
		found->offset = offset;
	} else if (found->ino == ino) {
		/*
		 * same extent found more then once in the same file.
		 */
		if (found->offset > offset + bctx->extent_len)
			found->offset = offset;
	}

	return 0;
}

/*
 * Given an inode, offset and extent item, it finds a good clone for a clone
 * instruction. Returns -ENOENT when none could be found. The function makes
 * sure that the returned clone is usable at the point where sending is at the
 * moment. This means, that no clones are accepted which lie behind the current
 * inode+offset.
 *
 * path must point to the extent item when called.
 */
static int find_extent_clone(struct send_ctx *sctx,
			     struct btrfs_path *path,
			     u64 ino, u64 data_offset,
			     u64 ino_size,
			     struct clone_root **found)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret;
	int extent_type;
	u64 logical;
	u64 disk_byte;
	u64 num_bytes;
	u64 extent_item_pos;
	u64 flags = 0;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *eb = path->nodes[0];
	struct backref_ctx *backref_ctx = NULL;
	struct clone_root *cur_clone_root;
	struct btrfs_key found_key;
	struct btrfs_path *tmp_path;
	int compressed;
	u32 i;

	tmp_path = alloc_path_for_send();
	if (!tmp_path)
		return -ENOMEM;

	/* We only use this path under the commit sem */
	tmp_path->need_commit_sem = 0;

	backref_ctx = kmalloc(sizeof(*backref_ctx), GFP_KERNEL);
	if (!backref_ctx) {
		ret = -ENOMEM;
		goto out;
	}

	if (data_offset >= ino_size) {
		/*
		 * There may be extents that lie behind the file's size.
		 * I at least had this in combination with snapshotting while
		 * writing large files.
		 */
		ret = 0;
		goto out;
	}

	fi = btrfs_item_ptr(eb, path->slots[0],
			struct btrfs_file_extent_item);
	extent_type = btrfs_file_extent_type(eb, fi);
	if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
		ret = -ENOENT;
		goto out;
	}
	compressed = btrfs_file_extent_compression(eb, fi);

	num_bytes = btrfs_file_extent_num_bytes(eb, fi);
	disk_byte = btrfs_file_extent_disk_bytenr(eb, fi);
	if (disk_byte == 0) {
		ret = -ENOENT;
		goto out;
	}
	logical = disk_byte + btrfs_file_extent_offset(eb, fi);

	down_read(&fs_info->commit_root_sem);
	ret = extent_from_logical(fs_info, disk_byte, tmp_path,
				  &found_key, &flags);
	up_read(&fs_info->commit_root_sem);
	btrfs_release_path(tmp_path);

	if (ret < 0)
		goto out;
	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		ret = -EIO;
		goto out;
	}

	/*
	 * Setup the clone roots.
	 */
	for (i = 0; i < sctx->clone_roots_cnt; i++) {
		cur_clone_root = sctx->clone_roots + i;
		cur_clone_root->ino = (u64)-1;
		cur_clone_root->offset = 0;
		cur_clone_root->found_refs = 0;
	}

	backref_ctx->sctx = sctx;
	backref_ctx->found = 0;
	backref_ctx->cur_objectid = ino;
	backref_ctx->cur_offset = data_offset;
	backref_ctx->found_itself = 0;
	backref_ctx->extent_len = num_bytes;
	/*
	 * For non-compressed extents iterate_extent_inodes() gives us extent
	 * offsets that already take into account the data offset, but not for
	 * compressed extents, since the offset is logical and not relative to
	 * the physical extent locations. We must take this into account to
	 * avoid sending clone offsets that go beyond the source file's size,
	 * which would result in the clone ioctl failing with -EINVAL on the
	 * receiving end.
	 */
	if (compressed == BTRFS_COMPRESS_NONE)
		backref_ctx->data_offset = 0;
	else
		backref_ctx->data_offset = btrfs_file_extent_offset(eb, fi);

	/*
	 * The last extent of a file may be too large due to page alignment.
	 * We need to adjust extent_len in this case so that the checks in
	 * __iterate_backrefs work.
	 */
	if (data_offset + num_bytes >= ino_size)
		backref_ctx->extent_len = ino_size - data_offset;

	/*
	 * Now collect all backrefs.
	 */
	if (compressed == BTRFS_COMPRESS_NONE)
		extent_item_pos = logical - found_key.objectid;
	else
		extent_item_pos = 0;
	ret = iterate_extent_inodes(fs_info, found_key.objectid,
				    extent_item_pos, 1, __iterate_backrefs,
				    backref_ctx, false);

	if (ret < 0)
		goto out;

	if (!backref_ctx->found_itself) {
		/* found a bug in backref code? */
		ret = -EIO;
		btrfs_err(fs_info,
			  "did not find backref in send_root. inode=%llu, offset=%llu, disk_byte=%llu found extent=%llu",
			  ino, data_offset, disk_byte, found_key.objectid);
		goto out;
	}

	btrfs_debug(fs_info,
		    "find_extent_clone: data_offset=%llu, ino=%llu, num_bytes=%llu, logical=%llu",
		    data_offset, ino, num_bytes, logical);

	if (!backref_ctx->found)
		btrfs_debug(fs_info, "no clones found");

	cur_clone_root = NULL;
	for (i = 0; i < sctx->clone_roots_cnt; i++) {
		if (sctx->clone_roots[i].found_refs) {
			if (!cur_clone_root)
				cur_clone_root = sctx->clone_roots + i;
			else if (sctx->clone_roots[i].root == sctx->send_root)
				/* prefer clones from send_root over others */
				cur_clone_root = sctx->clone_roots + i;
		}

	}

	if (cur_clone_root) {
		*found = cur_clone_root;
		ret = 0;
	} else {
		ret = -ENOENT;
	}

out:
	btrfs_free_path(tmp_path);
	kfree(backref_ctx);
	return ret;
}

static int read_symlink(struct btrfs_root *root,
			u64 ino,
			struct fs_path *dest)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_file_extent_item *ei;
	u8 type;
	u8 compression;
	unsigned long off;
	int len;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		/*
		 * An empty symlink inode. Can happen in rare error paths when
		 * creating a symlink (transaction committed before the inode
		 * eviction handler removed the symlink inode items and a crash
		 * happened in between or the subvol was snapshoted in between).
		 * Print an informative message to dmesg/syslog so that the user
		 * can delete the symlink.
		 */
		btrfs_err(root->fs_info,
			  "Found empty symlink inode %llu at root %llu",
			  ino, root->root_key.objectid);
		ret = -EIO;
		goto out;
	}

	ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
			struct btrfs_file_extent_item);
	type = btrfs_file_extent_type(path->nodes[0], ei);
	compression = btrfs_file_extent_compression(path->nodes[0], ei);
	BUG_ON(type != BTRFS_FILE_EXTENT_INLINE);
	BUG_ON(compression);

	off = btrfs_file_extent_inline_start(ei);
	len = btrfs_file_extent_ram_bytes(path->nodes[0], ei);

	ret = fs_path_add_from_extent_buffer(dest, path->nodes[0], off, len);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Helper function to generate a file name that is unique in the root of
 * send_root and parent_root. This is used to generate names for orphan inodes.
 */
static int gen_unique_name(struct send_ctx *sctx,
			   u64 ino, u64 gen,
			   struct fs_path *dest)
{
	int ret = 0;
	struct btrfs_path *path;
	struct btrfs_dir_item *di;
	char tmp[64];
	int len;
	u64 idx = 0;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	while (1) {
		len = snprintf(tmp, sizeof(tmp), "o%llu-%llu-%llu",
				ino, gen, idx);
		ASSERT(len < sizeof(tmp));

		di = btrfs_lookup_dir_item(NULL, sctx->send_root,
				path, BTRFS_FIRST_FREE_OBJECTID,
				tmp, strlen(tmp), 0);
		btrfs_release_path(path);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		}
		if (di) {
			/* not unique, try again */
			idx++;
			continue;
		}

		if (!sctx->parent_root) {
			/* unique */
			ret = 0;
			break;
		}

		di = btrfs_lookup_dir_item(NULL, sctx->parent_root,
				path, BTRFS_FIRST_FREE_OBJECTID,
				tmp, strlen(tmp), 0);
		btrfs_release_path(path);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		}
		if (di) {
			/* not unique, try again */
			idx++;
			continue;
		}
		/* unique */
		break;
	}

	ret = fs_path_add(dest, tmp, strlen(tmp));

out:
	btrfs_free_path(path);
	return ret;
}

enum inode_state {
	inode_state_no_change,
	inode_state_will_create,
	inode_state_did_create,
	inode_state_will_delete,
	inode_state_did_delete,
};

static int get_cur_inode_state(struct send_ctx *sctx, u64 ino, u64 gen)
{
	int ret;
	int left_ret;
	int right_ret;
	u64 left_gen;
	u64 right_gen;

	ret = get_inode_info(sctx->send_root, ino, NULL, &left_gen, NULL, NULL,
			NULL, NULL);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	left_ret = ret;

	if (!sctx->parent_root) {
		right_ret = -ENOENT;
	} else {
		ret = get_inode_info(sctx->parent_root, ino, NULL, &right_gen,
				NULL, NULL, NULL, NULL);
		if (ret < 0 && ret != -ENOENT)
			goto out;
		right_ret = ret;
	}

	if (!left_ret && !right_ret) {
		if (left_gen == gen && right_gen == gen) {
			ret = inode_state_no_change;
		} else if (left_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_create;
			else
				ret = inode_state_will_create;
		} else if (right_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_delete;
			else
				ret = inode_state_will_delete;
		} else  {
			ret = -ENOENT;
		}
	} else if (!left_ret) {
		if (left_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_create;
			else
				ret = inode_state_will_create;
		} else {
			ret = -ENOENT;
		}
	} else if (!right_ret) {
		if (right_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_delete;
			else
				ret = inode_state_will_delete;
		} else {
			ret = -ENOENT;
		}
	} else {
		ret = -ENOENT;
	}

out:
	return ret;
}

static int is_inode_existent(struct send_ctx *sctx, u64 ino, u64 gen)
{
	int ret;

	if (ino == BTRFS_FIRST_FREE_OBJECTID)
		return 1;

	ret = get_cur_inode_state(sctx, ino, gen);
	if (ret < 0)
		goto out;

	if (ret == inode_state_no_change ||
	    ret == inode_state_did_create ||
	    ret == inode_state_will_delete)
		ret = 1;
	else
		ret = 0;

out:
	return ret;
}

/*
 * Helper function to lookup a dir item in a dir.
 */
static int lookup_dir_item_inode(struct btrfs_root *root,
				 u64 dir, const char *name, int name_len,
				 u64 *found_inode,
				 u8 *found_type)
{
	int ret = 0;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_path *path;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	di = btrfs_lookup_dir_item(NULL, root, path,
			dir, name, name_len, 0);
	if (IS_ERR_OR_NULL(di)) {
		ret = di ? PTR_ERR(di) : -ENOENT;
		goto out;
	}
	btrfs_dir_item_key_to_cpu(path->nodes[0], di, &key);
	if (key.type == BTRFS_ROOT_ITEM_KEY) {
		ret = -ENOENT;
		goto out;
	}
	*found_inode = key.objectid;
	*found_type = btrfs_dir_type(path->nodes[0], di);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Looks up the first btrfs_inode_ref of a given ino. It returns the parent dir,
 * generation of the parent dir and the name of the dir entry.
 */
static int get_first_ref(struct btrfs_root *root, u64 ino,
			 u64 *dir, u64 *dir_gen, struct fs_path *name)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	int len;
	u64 parent_dir;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (!ret)
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				path->slots[0]);
	if (ret || found_key.objectid != ino ||
	    (found_key.type != BTRFS_INODE_REF_KEY &&
	     found_key.type != BTRFS_INODE_EXTREF_KEY)) {
		ret = -ENOENT;
		goto out;
	}

	if (found_key.type == BTRFS_INODE_REF_KEY) {
		struct btrfs_inode_ref *iref;
		iref = btrfs_item_ptr(path->nodes[0], path->slots[0],
				      struct btrfs_inode_ref);
		len = btrfs_inode_ref_name_len(path->nodes[0], iref);
		ret = fs_path_add_from_extent_buffer(name, path->nodes[0],
						     (unsigned long)(iref + 1),
						     len);
		parent_dir = found_key.offset;
	} else {
		struct btrfs_inode_extref *extref;
		extref = btrfs_item_ptr(path->nodes[0], path->slots[0],
					struct btrfs_inode_extref);
		len = btrfs_inode_extref_name_len(path->nodes[0], extref);
		ret = fs_path_add_from_extent_buffer(name, path->nodes[0],
					(unsigned long)&extref->name, len);
		parent_dir = btrfs_inode_extref_parent(path->nodes[0], extref);
	}
	if (ret < 0)
		goto out;
	btrfs_release_path(path);

	if (dir_gen) {
		ret = get_inode_info(root, parent_dir, NULL, dir_gen, NULL,
				     NULL, NULL, NULL);
		if (ret < 0)
			goto out;
	}

	*dir = parent_dir;

out:
	btrfs_free_path(path);
	return ret;
}

static int is_first_ref(struct btrfs_root *root,
			u64 ino, u64 dir,
			const char *name, int name_len)
{
	int ret;
	struct fs_path *tmp_name;
	u64 tmp_dir;

	tmp_name = fs_path_alloc();
	if (!tmp_name)
		return -ENOMEM;

	ret = get_first_ref(root, ino, &tmp_dir, NULL, tmp_name);
	if (ret < 0)
		goto out;

	if (dir != tmp_dir || name_len != fs_path_len(tmp_name)) {
		ret = 0;
		goto out;
	}

	ret = !memcmp(tmp_name->start, name, name_len);

out:
	fs_path_free(tmp_name);
	return ret;
}

/*
 * Used by process_recorded_refs to determine if a new ref would overwrite an
 * already existing ref. In case it detects an overwrite, it returns the
 * inode/gen in who_ino/who_gen.
 * When an overwrite is detected, process_recorded_refs does proper orphanizing
 * to make sure later references to the overwritten inode are possible.
 * Orphanizing is however only required for the first ref of an inode.
 * process_recorded_refs does an additional is_first_ref check to see if
 * orphanizing is really required.
 */
static int will_overwrite_ref(struct send_ctx *sctx, u64 dir, u64 dir_gen,
			      const char *name, int name_len,
			      u64 *who_ino, u64 *who_gen, u64 *who_mode)
{
	int ret = 0;
	u64 gen;
	u64 other_inode = 0;
	u8 other_type = 0;

	if (!sctx->parent_root)
		goto out;

	ret = is_inode_existent(sctx, dir, dir_gen);
	if (ret <= 0)
		goto out;

	/*
	 * If we have a parent root we need to verify that the parent dir was
	 * not deleted and then re-created, if it was then we have no overwrite
	 * and we can just unlink this entry.
	 */
	if (sctx->parent_root && dir != BTRFS_FIRST_FREE_OBJECTID) {
		ret = get_inode_info(sctx->parent_root, dir, NULL, &gen, NULL,
				     NULL, NULL, NULL);
		if (ret < 0 && ret != -ENOENT)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}
		if (gen != dir_gen)
			goto out;
	}

	ret = lookup_dir_item_inode(sctx->parent_root, dir, name, name_len,
			&other_inode, &other_type);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	if (ret) {
		ret = 0;
		goto out;
	}

	/*
	 * Check if the overwritten ref was already processed. If yes, the ref
	 * was already unlinked/moved, so we can safely assume that we will not
	 * overwrite anything at this point in time.
	 */
	if (other_inode > sctx->send_progress ||
	    is_waiting_for_move(sctx, other_inode)) {
		ret = get_inode_info(sctx->parent_root, other_inode, NULL,
				who_gen, who_mode, NULL, NULL, NULL);
		if (ret < 0)
			goto out;

		ret = 1;
		*who_ino = other_inode;
	} else {
		ret = 0;
	}

out:
	return ret;
}

/*
 * Checks if the ref was overwritten by an already processed inode. This is
 * used by __get_cur_name_and_parent to find out if the ref was orphanized and
 * thus the orphan name needs be used.
 * process_recorded_refs also uses it to avoid unlinking of refs that were
 * overwritten.
 */
static int did_overwrite_ref(struct send_ctx *sctx,
			    u64 dir, u64 dir_gen,
			    u64 ino, u64 ino_gen,
			    const char *name, int name_len)
{
	int ret = 0;
	u64 gen;
	u64 ow_inode;
	u8 other_type;

	if (!sctx->parent_root)
		goto out;

	ret = is_inode_existent(sctx, dir, dir_gen);
	if (ret <= 0)
		goto out;

	if (dir != BTRFS_FIRST_FREE_OBJECTID) {
		ret = get_inode_info(sctx->send_root, dir, NULL, &gen, NULL,
				     NULL, NULL, NULL);
		if (ret < 0 && ret != -ENOENT)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}
		if (gen != dir_gen)
			goto out;
	}

	/* check if the ref was overwritten by another ref */
	ret = lookup_dir_item_inode(sctx->send_root, dir, name, name_len,
			&ow_inode, &other_type);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	if (ret) {
		/* was never and will never be overwritten */
		ret = 0;
		goto out;
	}

	ret = get_inode_info(sctx->send_root, ow_inode, NULL, &gen, NULL, NULL,
			NULL, NULL);
	if (ret < 0)
		goto out;

	if (ow_inode == ino && gen == ino_gen) {
		ret = 0;
		goto out;
	}

	/*
	 * We know that it is or will be overwritten. Check this now.
	 * The current inode being processed might have been the one that caused
	 * inode 'ino' to be orphanized, therefore check if ow_inode matches
	 * the current inode being processed.
	 */
	if ((ow_inode < sctx->send_progress) ||
	    (ino != sctx->cur_ino && ow_inode == sctx->cur_ino &&
	     gen == sctx->cur_inode_gen))
		ret = 1;
	else
		ret = 0;

out:
	return ret;
}

/*
 * Same as did_overwrite_ref, but also checks if it is the first ref of an inode
 * that got overwritten. This is used by process_recorded_refs to determine
 * if it has to use the path as returned by get_cur_path or the orphan name.
 */
static int did_overwrite_first_ref(struct send_ctx *sctx, u64 ino, u64 gen)
{
	int ret = 0;
	struct fs_path *name = NULL;
	u64 dir;
	u64 dir_gen;

	if (!sctx->parent_root)
		goto out;

	name = fs_path_alloc();
	if (!name)
		return -ENOMEM;

	ret = get_first_ref(sctx->parent_root, ino, &dir, &dir_gen, name);
	if (ret < 0)
		goto out;

	ret = did_overwrite_ref(sctx, dir, dir_gen, ino, gen,
			name->start, fs_path_len(name));

out:
	fs_path_free(name);
	return ret;
}

/*
 * Insert a name cache entry. On 32bit kernels the radix tree index is 32bit,
 * so we need to do some special handling in case we have clashes. This function
 * takes care of this with the help of name_cache_entry::radix_list.
 * In case of error, nce is kfreed.
 */
static int name_cache_insert(struct send_ctx *sctx,
			     struct name_cache_entry *nce)
{
	int ret = 0;
	struct list_head *nce_head;

	nce_head = radix_tree_lookup(&sctx->name_cache,
			(unsigned long)nce->ino);
	if (!nce_head) {
		nce_head = kmalloc(sizeof(*nce_head), GFP_KERNEL);
		if (!nce_head) {
			kfree(nce);
			return -ENOMEM;
		}
		INIT_LIST_HEAD(nce_head);

		ret = radix_tree_insert(&sctx->name_cache, nce->ino, nce_head);
		if (ret < 0) {
			kfree(nce_head);
			kfree(nce);
			return ret;
		}
	}
	list_add_tail(&nce->radix_list, nce_head);
	list_add_tail(&nce->list, &sctx->name_cache_list);
	sctx->name_cache_size++;

	return ret;
}

static void name_cache_delete(struct send_ctx *sctx,
			      struct name_cache_entry *nce)
{
	struct list_head *nce_head;

	nce_head = radix_tree_lookup(&sctx->name_cache,
			(unsigned long)nce->ino);
	if (!nce_head) {
		btrfs_err(sctx->send_root->fs_info,
	      "name_cache_delete lookup failed ino %llu cache size %d, leaking memory",
			nce->ino, sctx->name_cache_size);
	}

	list_del(&nce->radix_list);
	list_del(&nce->list);
	sctx->name_cache_size--;

	/*
	 * We may not get to the final release of nce_head if the lookup fails
	 */
	if (nce_head && list_empty(nce_head)) {
		radix_tree_delete(&sctx->name_cache, (unsigned long)nce->ino);
		kfree(nce_head);
	}
}

static struct name_cache_entry *name_cache_search(struct send_ctx *sctx,
						    u64 ino, u64 gen)
{
	struct list_head *nce_head;
	struct name_cache_entry *cur;

	nce_head = radix_tree_lookup(&sctx->name_cache, (unsigned long)ino);
	if (!nce_head)
		return NULL;

	list_for_each_entry(cur, nce_head, radix_list) {
		if (cur->ino == ino && cur->gen == gen)
			return cur;
	}
	return NULL;
}

/*
 * Removes the entry from the list and adds it back to the end. This marks the
 * entry as recently used so that name_cache_clean_unused does not remove it.
 */
static void name_cache_used(struct send_ctx *sctx, struct name_cache_entry *nce)
{
	list_del(&nce->list);
	list_add_tail(&nce->list, &sctx->name_cache_list);
}

/*
 * Remove some entries from the beginning of name_cache_list.
 */
static void name_cache_clean_unused(struct send_ctx *sctx)
{
	struct name_cache_entry *nce;

	if (sctx->name_cache_size < SEND_CTX_NAME_CACHE_CLEAN_SIZE)
		return;

	while (sctx->name_cache_size > SEND_CTX_MAX_NAME_CACHE_SIZE) {
		nce = list_entry(sctx->name_cache_list.next,
				struct name_cache_entry, list);
		name_cache_delete(sctx, nce);
		kfree(nce);
	}
}

static void name_cache_free(struct send_ctx *sctx)
{
	struct name_cache_entry *nce;

	while (!list_empty(&sctx->name_cache_list)) {
		nce = list_entry(sctx->name_cache_list.next,
				struct name_cache_entry, list);
		name_cache_delete(sctx, nce);
		kfree(nce);
	}
}

/*
 * Used by get_cur_path for each ref up to the root.
 * Returns 0 if it succeeded.
 * Returns 1 if the inode is not existent or got overwritten. In that case, the
 * name is an orphan name. This instructs get_cur_path to stop iterating. If 1
 * is returned, parent_ino/parent_gen are not guaranteed to be valid.
 * Returns <0 in case of error.
 */
static int __get_cur_name_and_parent(struct send_ctx *sctx,
				     u64 ino, u64 gen,
				     u64 *parent_ino,
				     u64 *parent_gen,
				     struct fs_path *dest)
{
	int ret;
	int nce_ret;
	struct name_cache_entry *nce = NULL;

	/*
	 * First check if we already did a call to this function with the same
	 * ino/gen. If yes, check if the cache entry is still up-to-date. If yes
	 * return the cached result.
	 */
	nce = name_cache_search(sctx, ino, gen);
	if (nce) {
		if (ino < sctx->send_progress && nce->need_later_update) {
			name_cache_delete(sctx, nce);
			kfree(nce);
			nce = NULL;
		} else {
			name_cache_used(sctx, nce);
			*parent_ino = nce->parent_ino;
			*parent_gen = nce->parent_gen;
			ret = fs_path_add(dest, nce->name, nce->name_len);
			if (ret < 0)
				goto out;
			ret = nce->ret;
			goto out;
		}
	}

	/*
	 * If the inode is not existent yet, add the orphan name and return 1.
	 * This should only happen for the parent dir that we determine in
	 * __record_new_ref
	 */
	ret = is_inode_existent(sctx, ino, gen);
	if (ret < 0)
		goto out;

	if (!ret) {
		ret = gen_unique_name(sctx, ino, gen, dest);
		if (ret < 0)
			goto out;
		ret = 1;
		goto out_cache;
	}

	/*
	 * Depending on whether the inode was already processed or not, use
	 * send_root or parent_root for ref lookup.
	 */
	if (ino < sctx->send_progress)
		ret = get_first_ref(sctx->send_root, ino,
				    parent_ino, parent_gen, dest);
	else
		ret = get_first_ref(sctx->parent_root, ino,
				    parent_ino, parent_gen, dest);
	if (ret < 0)
		goto out;

	/*
	 * Check if the ref was overwritten by an inode's ref that was processed
	 * earlier. If yes, treat as orphan and return 1.
	 */
	ret = did_overwrite_ref(sctx, *parent_ino, *parent_gen, ino, gen,
			dest->start, dest->end - dest->start);
	if (ret < 0)
		goto out;
	if (ret) {
		fs_path_reset(dest);
		ret = gen_unique_name(sctx, ino, gen, dest);
		if (ret < 0)
			goto out;
		ret = 1;
	}

out_cache:
	/*
	 * Store the result of the lookup in the name cache.
	 */
	nce = kmalloc(sizeof(*nce) + fs_path_len(dest) + 1, GFP_KERNEL);
	if (!nce) {
		ret = -ENOMEM;
		goto out;
	}

	nce->ino = ino;
	nce->gen = gen;
	nce->parent_ino = *parent_ino;
	nce->parent_gen = *parent_gen;
	nce->name_len = fs_path_len(dest);
	nce->ret = ret;
	strcpy(nce->name, dest->start);

	if (ino < sctx->send_progress)
		nce->need_later_update = 0;
	else
		nce->need_later_update = 1;

	nce_ret = name_cache_insert(sctx, nce);
	if (nce_ret < 0)
		ret = nce_ret;
	name_cache_clean_unused(sctx);

out:
	return ret;
}

/*
 * Magic happens here. This function returns the first ref to an inode as it
 * would look like while receiving the stream at this point in time.
 * We walk the path up to the root. For every inode in between, we check if it
 * was already processed/sent. If yes, we continue with the parent as found
 * in send_root. If not, we continue with the parent as found in parent_root.
 * If we encounter an inode that was deleted at this point in time, we use the
 * inodes "orphan" name instead of the real name and stop. Same with new inodes
 * that were not created yet and overwritten inodes/refs.
 *
 * When do we have orphan inodes:
 * 1. When an inode is freshly created and thus no valid refs are available yet
 * 2. When a directory lost all it's refs (deleted) but still has dir items
 *    inside which were not processed yet (pending for move/delete). If anyone
 *    tried to get the path to the dir items, it would get a path inside that
 *    orphan directory.
 * 3. When an inode is moved around or gets new links, it may overwrite the ref
 *    of an unprocessed inode. If in that case the first ref would be
 *    overwritten, the overwritten inode gets "orphanized". Later when we
 *    process this overwritten inode, it is restored at a new place by moving
 *    the orphan inode.
 *
 * sctx->send_progress tells this function at which point in time receiving
 * would be.
 */
static int get_cur_path(struct send_ctx *sctx, u64 ino, u64 gen,
			struct fs_path *dest)
{
	int ret = 0;
	struct fs_path *name = NULL;
	u64 parent_inode = 0;
	u64 parent_gen = 0;
	int stop = 0;

	name = fs_path_alloc();
	if (!name) {
		ret = -ENOMEM;
		goto out;
	}

	dest->reversed = 1;
	fs_path_reset(dest);

	while (!stop && ino != BTRFS_FIRST_FREE_OBJECTID) {
		struct waiting_dir_move *wdm;

		fs_path_reset(name);

		if (is_waiting_for_rm(sctx, ino)) {
			ret = gen_unique_name(sctx, ino, gen, name);
			if (ret < 0)
				goto out;
			ret = fs_path_add_path(dest, name);
			break;
		}

		wdm = get_waiting_dir_move(sctx, ino);
		if (wdm && wdm->orphanized) {
			ret = gen_unique_name(sctx, ino, gen, name);
			stop = 1;
		} else if (wdm) {
			ret = get_first_ref(sctx->parent_root, ino,
					    &parent_inode, &parent_gen, name);
		} else {
			ret = __get_cur_name_and_parent(sctx, ino, gen,
							&parent_inode,
							&parent_gen, name);
			if (ret)
				stop = 1;
		}

		if (ret < 0)
			goto out;

		ret = fs_path_add_path(dest, name);
		if (ret < 0)
			goto out;

		ino = parent_inode;
		gen = parent_gen;
	}

out:
	fs_path_free(name);
	if (!ret)
		fs_path_unreverse(dest);
	return ret;
}

/*
 * Sends a BTRFS_SEND_C_SUBVOL command/item to userspace
 */
static int send_subvol_begin(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_root *send_root = sctx->send_root;
	struct btrfs_root *parent_root = sctx->parent_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root_ref *ref;
	struct extent_buffer *leaf;
	char *name = NULL;
	int namelen;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	name = kmalloc(BTRFS_PATH_NAME_MAX, GFP_KERNEL);
	if (!name) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	key.objectid = send_root->root_key.objectid;
	key.type = BTRFS_ROOT_BACKREF_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(send_root->fs_info->tree_root,
				&key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (key.type != BTRFS_ROOT_BACKREF_KEY ||
	    key.objectid != send_root->root_key.objectid) {
		ret = -ENOENT;
		goto out;
	}
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_ref);
	namelen = btrfs_root_ref_name_len(leaf, ref);
	read_extent_buffer(leaf, name, (unsigned long)(ref + 1), namelen);
	btrfs_release_path(path);

	if (parent_root) {
		ret = begin_cmd(sctx, BTRFS_SEND_C_SNAPSHOT);
		if (ret < 0)
			goto out;
	} else {
		ret = begin_cmd(sctx, BTRFS_SEND_C_SUBVOL);
		if (ret < 0)
			goto out;
	}

	TLV_PUT_STRING(sctx, BTRFS_SEND_A_PATH, name, namelen);

	if (!btrfs_is_empty_uuid(sctx->send_root->root_item.received_uuid))
		TLV_PUT_UUID(sctx, BTRFS_SEND_A_UUID,
			    sctx->send_root->root_item.received_uuid);
	else
		TLV_PUT_UUID(sctx, BTRFS_SEND_A_UUID,
			    sctx->send_root->root_item.uuid);

	TLV_PUT_U64(sctx, BTRFS_SEND_A_CTRANSID,
		    le64_to_cpu(sctx->send_root->root_item.ctransid));
	if (parent_root) {
		if (!btrfs_is_empty_uuid(parent_root->root_item.received_uuid))
			TLV_PUT_UUID(sctx, BTRFS_SEND_A_CLONE_UUID,
				     parent_root->root_item.received_uuid);
		else
			TLV_PUT_UUID(sctx, BTRFS_SEND_A_CLONE_UUID,
				     parent_root->root_item.uuid);
		TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_CTRANSID,
			    le64_to_cpu(sctx->parent_root->root_item.ctransid));
	}

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	btrfs_free_path(path);
	kfree(name);
	return ret;
}

static int send_truncate(struct send_ctx *sctx, u64 ino, u64 gen, u64 size)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret = 0;
	struct fs_path *p;

	btrfs_debug(fs_info, "send_truncate %llu size=%llu", ino, size);

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_TRUNCATE);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_SIZE, size);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(p);
	return ret;
}

static int send_chmod(struct send_ctx *sctx, u64 ino, u64 gen, u64 mode)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret = 0;
	struct fs_path *p;

	btrfs_debug(fs_info, "send_chmod %llu mode=%llu", ino, mode);

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_CHMOD);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_MODE, mode & 07777);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(p);
	return ret;
}

static int send_chown(struct send_ctx *sctx, u64 ino, u64 gen, u64 uid, u64 gid)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret = 0;
	struct fs_path *p;

	btrfs_debug(fs_info, "send_chown %llu uid=%llu, gid=%llu",
		    ino, uid, gid);

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_CHOWN);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_UID, uid);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_GID, gid);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(p);
	return ret;
}

static int send_utimes(struct send_ctx *sctx, u64 ino, u64 gen)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret = 0;
	struct fs_path *p = NULL;
	struct btrfs_inode_item *ii;
	struct btrfs_path *path = NULL;
	struct extent_buffer *eb;
	struct btrfs_key key;
	int slot;

	btrfs_debug(fs_info, "send_utimes %llu", ino);

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	path = alloc_path_for_send();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = ino;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, sctx->send_root, &key, path, 0, 0);
	if (ret > 0)
		ret = -ENOENT;
	if (ret < 0)
		goto out;

	eb = path->nodes[0];
	slot = path->slots[0];
	ii = btrfs_item_ptr(eb, slot, struct btrfs_inode_item);

	ret = begin_cmd(sctx, BTRFS_SEND_C_UTIMES);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_BTRFS_TIMESPEC(sctx, BTRFS_SEND_A_ATIME, eb, &ii->atime);
	TLV_PUT_BTRFS_TIMESPEC(sctx, BTRFS_SEND_A_MTIME, eb, &ii->mtime);
	TLV_PUT_BTRFS_TIMESPEC(sctx, BTRFS_SEND_A_CTIME, eb, &ii->ctime);
	/* TODO Add otime support when the otime patches get into upstream */

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(p);
	btrfs_free_path(path);
	return ret;
}

/*
 * Sends a BTRFS_SEND_C_MKXXX or SYMLINK command to user space. We don't have
 * a valid path yet because we did not process the refs yet. So, the inode
 * is created as orphan.
 */
static int send_create_inode(struct send_ctx *sctx, u64 ino)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret = 0;
	struct fs_path *p;
	int cmd;
	u64 gen;
	u64 mode;
	u64 rdev;

	btrfs_debug(fs_info, "send_create_inode %llu", ino);

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	if (ino != sctx->cur_ino) {
		ret = get_inode_info(sctx->send_root, ino, NULL, &gen, &mode,
				     NULL, NULL, &rdev);
		if (ret < 0)
			goto out;
	} else {
		gen = sctx->cur_inode_gen;
		mode = sctx->cur_inode_mode;
		rdev = sctx->cur_inode_rdev;
	}

	if (S_ISREG(mode)) {
		cmd = BTRFS_SEND_C_MKFILE;
	} else if (S_ISDIR(mode)) {
		cmd = BTRFS_SEND_C_MKDIR;
	} else if (S_ISLNK(mode)) {
		cmd = BTRFS_SEND_C_SYMLINK;
	} else if (S_ISCHR(mode) || S_ISBLK(mode)) {
		cmd = BTRFS_SEND_C_MKNOD;
	} else if (S_ISFIFO(mode)) {
		cmd = BTRFS_SEND_C_MKFIFO;
	} else if (S_ISSOCK(mode)) {
		cmd = BTRFS_SEND_C_MKSOCK;
	} else {
		btrfs_warn(sctx->send_root->fs_info, "unexpected inode type %o",
				(int)(mode & S_IFMT));
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = begin_cmd(sctx, cmd);
	if (ret < 0)
		goto out;

	ret = gen_unique_name(sctx, ino, gen, p);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_INO, ino);

	if (S_ISLNK(mode)) {
		fs_path_reset(p);
		ret = read_symlink(sctx->send_root, ino, p);
		if (ret < 0)
			goto out;
		TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH_LINK, p);
	} else if (S_ISCHR(mode) || S_ISBLK(mode) ||
		   S_ISFIFO(mode) || S_ISSOCK(mode)) {
		TLV_PUT_U64(sctx, BTRFS_SEND_A_RDEV, new_encode_dev(rdev));
		TLV_PUT_U64(sctx, BTRFS_SEND_A_MODE, mode);
	}

	ret = send_cmd(sctx);
	if (ret < 0)
		goto out;


tlv_put_failure:
out:
	fs_path_free(p);
	return ret;
}

/*
 * We need some special handling for inodes that get processed before the parent
 * directory got created. See process_recorded_refs for details.
 * This function does the check if we already created the dir out of order.
 */
static int did_create_dir(struct send_ctx *sctx, u64 dir)
{
	int ret = 0;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_key di_key;
	struct extent_buffer *eb;
	struct btrfs_dir_item *di;
	int slot;

	path = alloc_path_for_send();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = dir;
	key.type = BTRFS_DIR_INDEX_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, sctx->send_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		eb = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(eb)) {
			ret = btrfs_next_leaf(sctx->send_root, path);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = 0;
				break;
			}
			continue;
		}

		btrfs_item_key_to_cpu(eb, &found_key, slot);
		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			ret = 0;
			goto out;
		}

		di = btrfs_item_ptr(eb, slot, struct btrfs_dir_item);
		btrfs_dir_item_key_to_cpu(eb, di, &di_key);

		if (di_key.type != BTRFS_ROOT_ITEM_KEY &&
		    di_key.objectid < sctx->send_progress) {
			ret = 1;
			goto out;
		}

		path->slots[0]++;
	}

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Only creates the inode if it is:
 * 1. Not a directory
 * 2. Or a directory which was not created already due to out of order
 *    directories. See did_create_dir and process_recorded_refs for details.
 */
static int send_create_inode_if_needed(struct send_ctx *sctx)
{
	int ret;

	if (S_ISDIR(sctx->cur_inode_mode)) {
		ret = did_create_dir(sctx, sctx->cur_ino);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}
	}

	ret = send_create_inode(sctx, sctx->cur_ino);
	if (ret < 0)
		goto out;

out:
	return ret;
}

struct recorded_ref {
	struct list_head list;
	char *name;
	struct fs_path *full_path;
	u64 dir;
	u64 dir_gen;
	int name_len;
};

static void set_ref_path(struct recorded_ref *ref, struct fs_path *path)
{
	ref->full_path = path;
	ref->name = (char *)kbasename(ref->full_path->start);
	ref->name_len = ref->full_path->end - ref->name;
}

/*
 * We need to process new refs before deleted refs, but compare_tree gives us
 * everything mixed. So we first record all refs and later process them.
 * This function is a helper to record one ref.
 */
static int __record_ref(struct list_head *head, u64 dir,
		      u64 dir_gen, struct fs_path *path)
{
	struct recorded_ref *ref;

	ref = kmalloc(sizeof(*ref), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;

	ref->dir = dir;
	ref->dir_gen = dir_gen;
	set_ref_path(ref, path);
	list_add_tail(&ref->list, head);
	return 0;
}

static int dup_ref(struct recorded_ref *ref, struct list_head *list)
{
	struct recorded_ref *new;

	new = kmalloc(sizeof(*ref), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->dir = ref->dir;
	new->dir_gen = ref->dir_gen;
	new->full_path = NULL;
	INIT_LIST_HEAD(&new->list);
	list_add_tail(&new->list, list);
	return 0;
}

static void __free_recorded_refs(struct list_head *head)
{
	struct recorded_ref *cur;

	while (!list_empty(head)) {
		cur = list_entry(head->next, struct recorded_ref, list);
		fs_path_free(cur->full_path);
		list_del(&cur->list);
		kfree(cur);
	}
}

static void free_recorded_refs(struct send_ctx *sctx)
{
	__free_recorded_refs(&sctx->new_refs);
	__free_recorded_refs(&sctx->deleted_refs);
}

/*
 * Renames/moves a file/dir to its orphan name. Used when the first
 * ref of an unprocessed inode gets overwritten and for all non empty
 * directories.
 */
static int orphanize_inode(struct send_ctx *sctx, u64 ino, u64 gen,
			  struct fs_path *path)
{
	int ret;
	struct fs_path *orphan;

	orphan = fs_path_alloc();
	if (!orphan)
		return -ENOMEM;

	ret = gen_unique_name(sctx, ino, gen, orphan);
	if (ret < 0)
		goto out;

	ret = send_rename(sctx, path, orphan);

out:
	fs_path_free(orphan);
	return ret;
}

static struct orphan_dir_info *
add_orphan_dir_info(struct send_ctx *sctx, u64 dir_ino)
{
	struct rb_node **p = &sctx->orphan_dirs.rb_node;
	struct rb_node *parent = NULL;
	struct orphan_dir_info *entry, *odi;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct orphan_dir_info, node);
		if (dir_ino < entry->ino) {
			p = &(*p)->rb_left;
		} else if (dir_ino > entry->ino) {
			p = &(*p)->rb_right;
		} else {
			return entry;
		}
	}

	odi = kmalloc(sizeof(*odi), GFP_KERNEL);
	if (!odi)
		return ERR_PTR(-ENOMEM);
	odi->ino = dir_ino;
	odi->gen = 0;
	odi->last_dir_index_offset = 0;

	rb_link_node(&odi->node, parent, p);
	rb_insert_color(&odi->node, &sctx->orphan_dirs);
	return odi;
}

static struct orphan_dir_info *
get_orphan_dir_info(struct send_ctx *sctx, u64 dir_ino)
{
	struct rb_node *n = sctx->orphan_dirs.rb_node;
	struct orphan_dir_info *entry;

	while (n) {
		entry = rb_entry(n, struct orphan_dir_info, node);
		if (dir_ino < entry->ino)
			n = n->rb_left;
		else if (dir_ino > entry->ino)
			n = n->rb_right;
		else
			return entry;
	}
	return NULL;
}

static int is_waiting_for_rm(struct send_ctx *sctx, u64 dir_ino)
{
	struct orphan_dir_info *odi = get_orphan_dir_info(sctx, dir_ino);

	return odi != NULL;
}

static void free_orphan_dir_info(struct send_ctx *sctx,
				 struct orphan_dir_info *odi)
{
	if (!odi)
		return;
	rb_erase(&odi->node, &sctx->orphan_dirs);
	kfree(odi);
}

/*
 * Returns 1 if a directory can be removed at this point in time.
 * We check this by iterating all dir items and checking if the inode behind
 * the dir item was already processed.
 */
static int can_rmdir(struct send_ctx *sctx, u64 dir, u64 dir_gen,
		     u64 send_progress)
{
	int ret = 0;
	struct btrfs_root *root = sctx->parent_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_key loc;
	struct btrfs_dir_item *di;
	struct orphan_dir_info *odi = NULL;

	/*
	 * Don't try to rmdir the top/root subvolume dir.
	 */
	if (dir == BTRFS_FIRST_FREE_OBJECTID)
		return 0;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = dir;
	key.type = BTRFS_DIR_INDEX_KEY;
	key.offset = 0;

	odi = get_orphan_dir_info(sctx, dir);
	if (odi)
		key.offset = odi->last_dir_index_offset;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		struct waiting_dir_move *dm;

		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;
			continue;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				      path->slots[0]);
		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type)
			break;

		di = btrfs_item_ptr(path->nodes[0], path->slots[0],
				struct btrfs_dir_item);
		btrfs_dir_item_key_to_cpu(path->nodes[0], di, &loc);

		dm = get_waiting_dir_move(sctx, loc.objectid);
		if (dm) {
			odi = add_orphan_dir_info(sctx, dir);
			if (IS_ERR(odi)) {
				ret = PTR_ERR(odi);
				goto out;
			}
			odi->gen = dir_gen;
			odi->last_dir_index_offset = found_key.offset;
			dm->rmdir_ino = dir;
			ret = 0;
			goto out;
		}

		if (loc.objectid > send_progress) {
			odi = add_orphan_dir_info(sctx, dir);
			if (IS_ERR(odi)) {
				ret = PTR_ERR(odi);
				goto out;
			}
			odi->gen = dir_gen;
			odi->last_dir_index_offset = found_key.offset;
			ret = 0;
			goto out;
		}

		path->slots[0]++;
	}
	free_orphan_dir_info(sctx, odi);

	ret = 1;

out:
	btrfs_free_path(path);
	return ret;
}

static int is_waiting_for_move(struct send_ctx *sctx, u64 ino)
{
	struct waiting_dir_move *entry = get_waiting_dir_move(sctx, ino);

	return entry != NULL;
}

static int add_waiting_dir_move(struct send_ctx *sctx, u64 ino, bool orphanized)
{
	struct rb_node **p = &sctx->waiting_dir_moves.rb_node;
	struct rb_node *parent = NULL;
	struct waiting_dir_move *entry, *dm;

	dm = kmalloc(sizeof(*dm), GFP_KERNEL);
	if (!dm)
		return -ENOMEM;
	dm->ino = ino;
	dm->rmdir_ino = 0;
	dm->orphanized = orphanized;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct waiting_dir_move, node);
		if (ino < entry->ino) {
			p = &(*p)->rb_left;
		} else if (ino > entry->ino) {
			p = &(*p)->rb_right;
		} else {
			kfree(dm);
			return -EEXIST;
		}
	}

	rb_link_node(&dm->node, parent, p);
	rb_insert_color(&dm->node, &sctx->waiting_dir_moves);
	return 0;
}

static struct waiting_dir_move *
get_waiting_dir_move(struct send_ctx *sctx, u64 ino)
{
	struct rb_node *n = sctx->waiting_dir_moves.rb_node;
	struct waiting_dir_move *entry;

	while (n) {
		entry = rb_entry(n, struct waiting_dir_move, node);
		if (ino < entry->ino)
			n = n->rb_left;
		else if (ino > entry->ino)
			n = n->rb_right;
		else
			return entry;
	}
	return NULL;
}

static void free_waiting_dir_move(struct send_ctx *sctx,
				  struct waiting_dir_move *dm)
{
	if (!dm)
		return;
	rb_erase(&dm->node, &sctx->waiting_dir_moves);
	kfree(dm);
}

static int add_pending_dir_move(struct send_ctx *sctx,
				u64 ino,
				u64 ino_gen,
				u64 parent_ino,
				struct list_head *new_refs,
				struct list_head *deleted_refs,
				const bool is_orphan)
{
	struct rb_node **p = &sctx->pending_dir_moves.rb_node;
	struct rb_node *parent = NULL;
	struct pending_dir_move *entry = NULL, *pm;
	struct recorded_ref *cur;
	int exists = 0;
	int ret;

	pm = kmalloc(sizeof(*pm), GFP_KERNEL);
	if (!pm)
		return -ENOMEM;
	pm->parent_ino = parent_ino;
	pm->ino = ino;
	pm->gen = ino_gen;
	INIT_LIST_HEAD(&pm->list);
	INIT_LIST_HEAD(&pm->update_refs);
	RB_CLEAR_NODE(&pm->node);

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct pending_dir_move, node);
		if (parent_ino < entry->parent_ino) {
			p = &(*p)->rb_left;
		} else if (parent_ino > entry->parent_ino) {
			p = &(*p)->rb_right;
		} else {
			exists = 1;
			break;
		}
	}

	list_for_each_entry(cur, deleted_refs, list) {
		ret = dup_ref(cur, &pm->update_refs);
		if (ret < 0)
			goto out;
	}
	list_for_each_entry(cur, new_refs, list) {
		ret = dup_ref(cur, &pm->update_refs);
		if (ret < 0)
			goto out;
	}

	ret = add_waiting_dir_move(sctx, pm->ino, is_orphan);
	if (ret)
		goto out;

	if (exists) {
		list_add_tail(&pm->list, &entry->list);
	} else {
		rb_link_node(&pm->node, parent, p);
		rb_insert_color(&pm->node, &sctx->pending_dir_moves);
	}
	ret = 0;
out:
	if (ret) {
		__free_recorded_refs(&pm->update_refs);
		kfree(pm);
	}
	return ret;
}

static struct pending_dir_move *get_pending_dir_moves(struct send_ctx *sctx,
						      u64 parent_ino)
{
	struct rb_node *n = sctx->pending_dir_moves.rb_node;
	struct pending_dir_move *entry;

	while (n) {
		entry = rb_entry(n, struct pending_dir_move, node);
		if (parent_ino < entry->parent_ino)
			n = n->rb_left;
		else if (parent_ino > entry->parent_ino)
			n = n->rb_right;
		else
			return entry;
	}
	return NULL;
}

static int path_loop(struct send_ctx *sctx, struct fs_path *name,
		     u64 ino, u64 gen, u64 *ancestor_ino)
{
	int ret = 0;
	u64 parent_inode = 0;
	u64 parent_gen = 0;
	u64 start_ino = ino;

	*ancestor_ino = 0;
	while (ino != BTRFS_FIRST_FREE_OBJECTID) {
		fs_path_reset(name);

		if (is_waiting_for_rm(sctx, ino))
			break;
		if (is_waiting_for_move(sctx, ino)) {
			if (*ancestor_ino == 0)
				*ancestor_ino = ino;
			ret = get_first_ref(sctx->parent_root, ino,
					    &parent_inode, &parent_gen, name);
		} else {
			ret = __get_cur_name_and_parent(sctx, ino, gen,
							&parent_inode,
							&parent_gen, name);
			if (ret > 0) {
				ret = 0;
				break;
			}
		}
		if (ret < 0)
			break;
		if (parent_inode == start_ino) {
			ret = 1;
			if (*ancestor_ino == 0)
				*ancestor_ino = ino;
			break;
		}
		ino = parent_inode;
		gen = parent_gen;
	}
	return ret;
}

static int apply_dir_move(struct send_ctx *sctx, struct pending_dir_move *pm)
{
	struct fs_path *from_path = NULL;
	struct fs_path *to_path = NULL;
	struct fs_path *name = NULL;
	u64 orig_progress = sctx->send_progress;
	struct recorded_ref *cur;
	u64 parent_ino, parent_gen;
	struct waiting_dir_move *dm = NULL;
	u64 rmdir_ino = 0;
	u64 ancestor;
	bool is_orphan;
	int ret;

	name = fs_path_alloc();
	from_path = fs_path_alloc();
	if (!name || !from_path) {
		ret = -ENOMEM;
		goto out;
	}

	dm = get_waiting_dir_move(sctx, pm->ino);
	ASSERT(dm);
	rmdir_ino = dm->rmdir_ino;
	is_orphan = dm->orphanized;
	free_waiting_dir_move(sctx, dm);

	if (is_orphan) {
		ret = gen_unique_name(sctx, pm->ino,
				      pm->gen, from_path);
	} else {
		ret = get_first_ref(sctx->parent_root, pm->ino,
				    &parent_ino, &parent_gen, name);
		if (ret < 0)
			goto out;
		ret = get_cur_path(sctx, parent_ino, parent_gen,
				   from_path);
		if (ret < 0)
			goto out;
		ret = fs_path_add_path(from_path, name);
	}
	if (ret < 0)
		goto out;

	sctx->send_progress = sctx->cur_ino + 1;
	ret = path_loop(sctx, name, pm->ino, pm->gen, &ancestor);
	if (ret < 0)
		goto out;
	if (ret) {
		LIST_HEAD(deleted_refs);
		ASSERT(ancestor > BTRFS_FIRST_FREE_OBJECTID);
		ret = add_pending_dir_move(sctx, pm->ino, pm->gen, ancestor,
					   &pm->update_refs, &deleted_refs,
					   is_orphan);
		if (ret < 0)
			goto out;
		if (rmdir_ino) {
			dm = get_waiting_dir_move(sctx, pm->ino);
			ASSERT(dm);
			dm->rmdir_ino = rmdir_ino;
		}
		goto out;
	}
	fs_path_reset(name);
	to_path = name;
	name = NULL;
	ret = get_cur_path(sctx, pm->ino, pm->gen, to_path);
	if (ret < 0)
		goto out;

	ret = send_rename(sctx, from_path, to_path);
	if (ret < 0)
		goto out;

	if (rmdir_ino) {
		struct orphan_dir_info *odi;
		u64 gen;

		odi = get_orphan_dir_info(sctx, rmdir_ino);
		if (!odi) {
			/* already deleted */
			goto finish;
		}
		gen = odi->gen;

		ret = can_rmdir(sctx, rmdir_ino, gen, sctx->cur_ino);
		if (ret < 0)
			goto out;
		if (!ret)
			goto finish;

		name = fs_path_alloc();
		if (!name) {
			ret = -ENOMEM;
			goto out;
		}
		ret = get_cur_path(sctx, rmdir_ino, gen, name);
		if (ret < 0)
			goto out;
		ret = send_rmdir(sctx, name);
		if (ret < 0)
			goto out;
	}

finish:
	ret = send_utimes(sctx, pm->ino, pm->gen);
	if (ret < 0)
		goto out;

	/*
	 * After rename/move, need to update the utimes of both new parent(s)
	 * and old parent(s).
	 */
	list_for_each_entry(cur, &pm->update_refs, list) {
		/*
		 * The parent inode might have been deleted in the send snapshot
		 */
		ret = get_inode_info(sctx->send_root, cur->dir, NULL,
				     NULL, NULL, NULL, NULL, NULL);
		if (ret == -ENOENT) {
			ret = 0;
			continue;
		}
		if (ret < 0)
			goto out;

		ret = send_utimes(sctx, cur->dir, cur->dir_gen);
		if (ret < 0)
			goto out;
	}

out:
	fs_path_free(name);
	fs_path_free(from_path);
	fs_path_free(to_path);
	sctx->send_progress = orig_progress;

	return ret;
}

static void free_pending_move(struct send_ctx *sctx, struct pending_dir_move *m)
{
	if (!list_empty(&m->list))
		list_del(&m->list);
	if (!RB_EMPTY_NODE(&m->node))
		rb_erase(&m->node, &sctx->pending_dir_moves);
	__free_recorded_refs(&m->update_refs);
	kfree(m);
}

static void tail_append_pending_moves(struct send_ctx *sctx,
				      struct pending_dir_move *moves,
				      struct list_head *stack)
{
	if (list_empty(&moves->list)) {
		list_add_tail(&moves->list, stack);
	} else {
		LIST_HEAD(list);
		list_splice_init(&moves->list, &list);
		list_add_tail(&moves->list, stack);
		list_splice_tail(&list, stack);
	}
	if (!RB_EMPTY_NODE(&moves->node)) {
		rb_erase(&moves->node, &sctx->pending_dir_moves);
		RB_CLEAR_NODE(&moves->node);
	}
}

static int apply_children_dir_moves(struct send_ctx *sctx)
{
	struct pending_dir_move *pm;
	struct list_head stack;
	u64 parent_ino = sctx->cur_ino;
	int ret = 0;

	pm = get_pending_dir_moves(sctx, parent_ino);
	if (!pm)
		return 0;

	INIT_LIST_HEAD(&stack);
	tail_append_pending_moves(sctx, pm, &stack);

	while (!list_empty(&stack)) {
		pm = list_first_entry(&stack, struct pending_dir_move, list);
		parent_ino = pm->ino;
		ret = apply_dir_move(sctx, pm);
		free_pending_move(sctx, pm);
		if (ret)
			goto out;
		pm = get_pending_dir_moves(sctx, parent_ino);
		if (pm)
			tail_append_pending_moves(sctx, pm, &stack);
	}
	return 0;

out:
	while (!list_empty(&stack)) {
		pm = list_first_entry(&stack, struct pending_dir_move, list);
		free_pending_move(sctx, pm);
	}
	return ret;
}

/*
 * We might need to delay a directory rename even when no ancestor directory
 * (in the send root) with a higher inode number than ours (sctx->cur_ino) was
 * renamed. This happens when we rename a directory to the old name (the name
 * in the parent root) of some other unrelated directory that got its rename
 * delayed due to some ancestor with higher number that got renamed.
 *
 * Example:
 *
 * Parent snapshot:
 * .                                       (ino 256)
 * |---- a/                                (ino 257)
 * |     |---- file                        (ino 260)
 * |
 * |---- b/                                (ino 258)
 * |---- c/                                (ino 259)
 *
 * Send snapshot:
 * .                                       (ino 256)
 * |---- a/                                (ino 258)
 * |---- x/                                (ino 259)
 *       |---- y/                          (ino 257)
 *             |----- file                 (ino 260)
 *
 * Here we can not rename 258 from 'b' to 'a' without the rename of inode 257
 * from 'a' to 'x/y' happening first, which in turn depends on the rename of
 * inode 259 from 'c' to 'x'. So the order of rename commands the send stream
 * must issue is:
 *
 * 1 - rename 259 from 'c' to 'x'
 * 2 - rename 257 from 'a' to 'x/y'
 * 3 - rename 258 from 'b' to 'a'
 *
 * Returns 1 if the rename of sctx->cur_ino needs to be delayed, 0 if it can
 * be done right away and < 0 on error.
 */
static int wait_for_dest_dir_move(struct send_ctx *sctx,
				  struct recorded_ref *parent_ref,
				  const bool is_orphan)
{
	struct btrfs_fs_info *fs_info = sctx->parent_root->fs_info;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key di_key;
	struct btrfs_dir_item *di;
	u64 left_gen;
	u64 right_gen;
	int ret = 0;
	struct waiting_dir_move *wdm;

	if (RB_EMPTY_ROOT(&sctx->waiting_dir_moves))
		return 0;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = parent_ref->dir;
	key.type = BTRFS_DIR_ITEM_KEY;
	key.offset = btrfs_name_hash(parent_ref->name, parent_ref->name_len);

	ret = btrfs_search_slot(NULL, sctx->parent_root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		ret = 0;
		goto out;
	}

	di = btrfs_match_dir_item_name(fs_info, path, parent_ref->name,
				       parent_ref->name_len);
	if (!di) {
		ret = 0;
		goto out;
	}
	/*
	 * di_key.objectid has the number of the inode that has a dentry in the
	 * parent directory with the same name that sctx->cur_ino is being
	 * renamed to. We need to check if that inode is in the send root as
	 * well and if it is currently marked as an inode with a pending rename,
	 * if it is, we need to delay the rename of sctx->cur_ino as well, so
	 * that it happens after that other inode is renamed.
	 */
	btrfs_dir_item_key_to_cpu(path->nodes[0], di, &di_key);
	if (di_key.type != BTRFS_INODE_ITEM_KEY) {
		ret = 0;
		goto out;
	}

	ret = get_inode_info(sctx->parent_root, di_key.objectid, NULL,
			     &left_gen, NULL, NULL, NULL, NULL);
	if (ret < 0)
		goto out;
	ret = get_inode_info(sctx->send_root, di_key.objectid, NULL,
			     &right_gen, NULL, NULL, NULL, NULL);
	if (ret < 0) {
		if (ret == -ENOENT)
			ret = 0;
		goto out;
	}

	/* Different inode, no need to delay the rename of sctx->cur_ino */
	if (right_gen != left_gen) {
		ret = 0;
		goto out;
	}

	wdm = get_waiting_dir_move(sctx, di_key.objectid);
	if (wdm && !wdm->orphanized) {
		ret = add_pending_dir_move(sctx,
					   sctx->cur_ino,
					   sctx->cur_inode_gen,
					   di_key.objectid,
					   &sctx->new_refs,
					   &sctx->deleted_refs,
					   is_orphan);
		if (!ret)
			ret = 1;
	}
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Check if inode ino2, or any of its ancestors, is inode ino1.
 * Return 1 if true, 0 if false and < 0 on error.
 */
static int check_ino_in_path(struct btrfs_root *root,
			     const u64 ino1,
			     const u64 ino1_gen,
			     const u64 ino2,
			     const u64 ino2_gen,
			     struct fs_path *fs_path)
{
	u64 ino = ino2;

	if (ino1 == ino2)
		return ino1_gen == ino2_gen;

	while (ino > BTRFS_FIRST_FREE_OBJECTID) {
		u64 parent;
		u64 parent_gen;
		int ret;

		fs_path_reset(fs_path);
		ret = get_first_ref(root, ino, &parent, &parent_gen, fs_path);
		if (ret < 0)
			return ret;
		if (parent == ino1)
			return parent_gen == ino1_gen;
		ino = parent;
	}
	return 0;
}

/*
 * Check if ino ino1 is an ancestor of inode ino2 in the given root for any
 * possible path (in case ino2 is not a directory and has multiple hard links).
 * Return 1 if true, 0 if false and < 0 on error.
 */
static int is_ancestor(struct btrfs_root *root,
		       const u64 ino1,
		       const u64 ino1_gen,
		       const u64 ino2,
		       struct fs_path *fs_path)
{
	bool free_fs_path = false;
	int ret = 0;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;

	if (!fs_path) {
		fs_path = fs_path_alloc();
		if (!fs_path)
			return -ENOMEM;
		free_fs_path = true;
	}

	path = alloc_path_for_send();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = ino2;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (true) {
		struct extent_buffer *leaf = path->nodes[0];
		int slot = path->slots[0];
		u32 cur_offset = 0;
		u32 item_size;

		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != ino2)
			break;
		if (key.type != BTRFS_INODE_REF_KEY &&
		    key.type != BTRFS_INODE_EXTREF_KEY)
			break;

		item_size = btrfs_item_size_nr(leaf, slot);
		while (cur_offset < item_size) {
			u64 parent;
			u64 parent_gen;

			if (key.type == BTRFS_INODE_EXTREF_KEY) {
				unsigned long ptr;
				struct btrfs_inode_extref *extref;

				ptr = btrfs_item_ptr_offset(leaf, slot);
				extref = (struct btrfs_inode_extref *)
					(ptr + cur_offset);
				parent = btrfs_inode_extref_parent(leaf,
								   extref);
				cur_offset += sizeof(*extref);
				cur_offset += btrfs_inode_extref_name_len(leaf,
								  extref);
			} else {
				parent = key.offset;
				cur_offset = item_size;
			}

			ret = get_inode_info(root, parent, NULL, &parent_gen,
					     NULL, NULL, NULL, NULL);
			if (ret < 0)
				goto out;
			ret = check_ino_in_path(root, ino1, ino1_gen,
						parent, parent_gen, fs_path);
			if (ret)
				goto out;
		}
		path->slots[0]++;
	}
	ret = 0;
 out:
	btrfs_free_path(path);
	if (free_fs_path)
		fs_path_free(fs_path);
	return ret;
}

static int wait_for_parent_move(struct send_ctx *sctx,
				struct recorded_ref *parent_ref,
				const bool is_orphan)
{
	int ret = 0;
	u64 ino = parent_ref->dir;
	u64 ino_gen = parent_ref->dir_gen;
	u64 parent_ino_before, parent_ino_after;
	struct fs_path *path_before = NULL;
	struct fs_path *path_after = NULL;
	int len1, len2;

	path_after = fs_path_alloc();
	path_before = fs_path_alloc();
	if (!path_after || !path_before) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Our current directory inode may not yet be renamed/moved because some
	 * ancestor (immediate or not) has to be renamed/moved first. So find if
	 * such ancestor exists and make sure our own rename/move happens after
	 * that ancestor is processed to avoid path build infinite loops (done
	 * at get_cur_path()).
	 */
	while (ino > BTRFS_FIRST_FREE_OBJECTID) {
		u64 parent_ino_after_gen;

		if (is_waiting_for_move(sctx, ino)) {
			/*
			 * If the current inode is an ancestor of ino in the
			 * parent root, we need to delay the rename of the
			 * current inode, otherwise don't delayed the rename
			 * because we can end up with a circular dependency
			 * of renames, resulting in some directories never
			 * getting the respective rename operations issued in
			 * the send stream or getting into infinite path build
			 * loops.
			 */
			ret = is_ancestor(sctx->parent_root,
					  sctx->cur_ino, sctx->cur_inode_gen,
					  ino, path_before);
			if (ret)
				break;
		}

		fs_path_reset(path_before);
		fs_path_reset(path_after);

		ret = get_first_ref(sctx->send_root, ino, &parent_ino_after,
				    &parent_ino_after_gen, path_after);
		if (ret < 0)
			goto out;
		ret = get_first_ref(sctx->parent_root, ino, &parent_ino_before,
				    NULL, path_before);
		if (ret < 0 && ret != -ENOENT) {
			goto out;
		} else if (ret == -ENOENT) {
			ret = 0;
			break;
		}

		len1 = fs_path_len(path_before);
		len2 = fs_path_len(path_after);
		if (ino > sctx->cur_ino &&
		    (parent_ino_before != parent_ino_after || len1 != len2 ||
		     memcmp(path_before->start, path_after->start, len1))) {
			u64 parent_ino_gen;

			ret = get_inode_info(sctx->parent_root, ino, NULL,
					     &parent_ino_gen, NULL, NULL, NULL,
					     NULL);
			if (ret < 0)
				goto out;
			if (ino_gen == parent_ino_gen) {
				ret = 1;
				break;
			}
		}
		ino = parent_ino_after;
		ino_gen = parent_ino_after_gen;
	}

out:
	fs_path_free(path_before);
	fs_path_free(path_after);

	if (ret == 1) {
		ret = add_pending_dir_move(sctx,
					   sctx->cur_ino,
					   sctx->cur_inode_gen,
					   ino,
					   &sctx->new_refs,
					   &sctx->deleted_refs,
					   is_orphan);
		if (!ret)
			ret = 1;
	}

	return ret;
}

static int update_ref_path(struct send_ctx *sctx, struct recorded_ref *ref)
{
	int ret;
	struct fs_path *new_path;

	/*
	 * Our reference's name member points to its full_path member string, so
	 * we use here a new path.
	 */
	new_path = fs_path_alloc();
	if (!new_path)
		return -ENOMEM;

	ret = get_cur_path(sctx, ref->dir, ref->dir_gen, new_path);
	if (ret < 0) {
		fs_path_free(new_path);
		return ret;
	}
	ret = fs_path_add(new_path, ref->name, ref->name_len);
	if (ret < 0) {
		fs_path_free(new_path);
		return ret;
	}

	fs_path_free(ref->full_path);
	set_ref_path(ref, new_path);

	return 0;
}

/*
 * This does all the move/link/unlink/rmdir magic.
 */
static int process_recorded_refs(struct send_ctx *sctx, int *pending_move)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret = 0;
	struct recorded_ref *cur;
	struct recorded_ref *cur2;
	struct list_head check_dirs;
	struct fs_path *valid_path = NULL;
	u64 ow_inode = 0;
	u64 ow_gen;
	u64 ow_mode;
	int did_overwrite = 0;
	int is_orphan = 0;
	u64 last_dir_ino_rm = 0;
	bool can_rename = true;
	bool orphanized_dir = false;
	bool orphanized_ancestor = false;

	btrfs_debug(fs_info, "process_recorded_refs %llu", sctx->cur_ino);

	/*
	 * This should never happen as the root dir always has the same ref
	 * which is always '..'
	 */
	BUG_ON(sctx->cur_ino <= BTRFS_FIRST_FREE_OBJECTID);
	INIT_LIST_HEAD(&check_dirs);

	valid_path = fs_path_alloc();
	if (!valid_path) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * First, check if the first ref of the current inode was overwritten
	 * before. If yes, we know that the current inode was already orphanized
	 * and thus use the orphan name. If not, we can use get_cur_path to
	 * get the path of the first ref as it would like while receiving at
	 * this point in time.
	 * New inodes are always orphan at the beginning, so force to use the
	 * orphan name in this case.
	 * The first ref is stored in valid_path and will be updated if it
	 * gets moved around.
	 */
	if (!sctx->cur_inode_new) {
		ret = did_overwrite_first_ref(sctx, sctx->cur_ino,
				sctx->cur_inode_gen);
		if (ret < 0)
			goto out;
		if (ret)
			did_overwrite = 1;
	}
	if (sctx->cur_inode_new || did_overwrite) {
		ret = gen_unique_name(sctx, sctx->cur_ino,
				sctx->cur_inode_gen, valid_path);
		if (ret < 0)
			goto out;
		is_orphan = 1;
	} else {
		ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				valid_path);
		if (ret < 0)
			goto out;
	}

	list_for_each_entry(cur, &sctx->new_refs, list) {
		/*
		 * We may have refs where the parent directory does not exist
		 * yet. This happens if the parent directories inum is higher
		 * than the current inum. To handle this case, we create the
		 * parent directory out of order. But we need to check if this
		 * did already happen before due to other refs in the same dir.
		 */
		ret = get_cur_inode_state(sctx, cur->dir, cur->dir_gen);
		if (ret < 0)
			goto out;
		if (ret == inode_state_will_create) {
			ret = 0;
			/*
			 * First check if any of the current inodes refs did
			 * already create the dir.
			 */
			list_for_each_entry(cur2, &sctx->new_refs, list) {
				if (cur == cur2)
					break;
				if (cur2->dir == cur->dir) {
					ret = 1;
					break;
				}
			}

			/*
			 * If that did not happen, check if a previous inode
			 * did already create the dir.
			 */
			if (!ret)
				ret = did_create_dir(sctx, cur->dir);
			if (ret < 0)
				goto out;
			if (!ret) {
				ret = send_create_inode(sctx, cur->dir);
				if (ret < 0)
					goto out;
			}
		}

		/*
		 * Check if this new ref would overwrite the first ref of
		 * another unprocessed inode. If yes, orphanize the
		 * overwritten inode. If we find an overwritten ref that is
		 * not the first ref, simply unlink it.
		 */
		ret = will_overwrite_ref(sctx, cur->dir, cur->dir_gen,
				cur->name, cur->name_len,
				&ow_inode, &ow_gen, &ow_mode);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = is_first_ref(sctx->parent_root,
					   ow_inode, cur->dir, cur->name,
					   cur->name_len);
			if (ret < 0)
				goto out;
			if (ret) {
				struct name_cache_entry *nce;
				struct waiting_dir_move *wdm;

				ret = orphanize_inode(sctx, ow_inode, ow_gen,
						cur->full_path);
				if (ret < 0)
					goto out;
				if (S_ISDIR(ow_mode))
					orphanized_dir = true;

				/*
				 * If ow_inode has its rename operation delayed
				 * make sure that its orphanized name is used in
				 * the source path when performing its rename
				 * operation.
				 */
				if (is_waiting_for_move(sctx, ow_inode)) {
					wdm = get_waiting_dir_move(sctx,
								   ow_inode);
					ASSERT(wdm);
					wdm->orphanized = true;
				}

				/*
				 * Make sure we clear our orphanized inode's
				 * name from the name cache. This is because the
				 * inode ow_inode might be an ancestor of some
				 * other inode that will be orphanized as well
				 * later and has an inode number greater than
				 * sctx->send_progress. We need to prevent
				 * future name lookups from using the old name
				 * and get instead the orphan name.
				 */
				nce = name_cache_search(sctx, ow_inode, ow_gen);
				if (nce) {
					name_cache_delete(sctx, nce);
					kfree(nce);
				}

				/*
				 * ow_inode might currently be an ancestor of
				 * cur_ino, therefore compute valid_path (the
				 * current path of cur_ino) again because it
				 * might contain the pre-orphanization name of
				 * ow_inode, which is no longer valid.
				 */
				ret = is_ancestor(sctx->parent_root,
						  ow_inode, ow_gen,
						  sctx->cur_ino, NULL);
				if (ret > 0) {
					orphanized_ancestor = true;
					fs_path_reset(valid_path);
					ret = get_cur_path(sctx, sctx->cur_ino,
							   sctx->cur_inode_gen,
							   valid_path);
				}
				if (ret < 0)
					goto out;
			} else {
				ret = send_unlink(sctx, cur->full_path);
				if (ret < 0)
					goto out;
			}
		}

		if (S_ISDIR(sctx->cur_inode_mode) && sctx->parent_root) {
			ret = wait_for_dest_dir_move(sctx, cur, is_orphan);
			if (ret < 0)
				goto out;
			if (ret == 1) {
				can_rename = false;
				*pending_move = 1;
			}
		}

		if (S_ISDIR(sctx->cur_inode_mode) && sctx->parent_root &&
		    can_rename) {
			ret = wait_for_parent_move(sctx, cur, is_orphan);
			if (ret < 0)
				goto out;
			if (ret == 1) {
				can_rename = false;
				*pending_move = 1;
			}
		}

		/*
		 * link/move the ref to the new place. If we have an orphan
		 * inode, move it and update valid_path. If not, link or move
		 * it depending on the inode mode.
		 */
		if (is_orphan && can_rename) {
			ret = send_rename(sctx, valid_path, cur->full_path);
			if (ret < 0)
				goto out;
			is_orphan = 0;
			ret = fs_path_copy(valid_path, cur->full_path);
			if (ret < 0)
				goto out;
		} else if (can_rename) {
			if (S_ISDIR(sctx->cur_inode_mode)) {
				/*
				 * Dirs can't be linked, so move it. For moved
				 * dirs, we always have one new and one deleted
				 * ref. The deleted ref is ignored later.
				 */
				ret = send_rename(sctx, valid_path,
						  cur->full_path);
				if (!ret)
					ret = fs_path_copy(valid_path,
							   cur->full_path);
				if (ret < 0)
					goto out;
			} else {
				/*
				 * We might have previously orphanized an inode
				 * which is an ancestor of our current inode,
				 * so our reference's full path, which was
				 * computed before any such orphanizations, must
				 * be updated.
				 */
				if (orphanized_dir) {
					ret = update_ref_path(sctx, cur);
					if (ret < 0)
						goto out;
				}
				ret = send_link(sctx, cur->full_path,
						valid_path);
				if (ret < 0)
					goto out;
			}
		}
		ret = dup_ref(cur, &check_dirs);
		if (ret < 0)
			goto out;
	}

	if (S_ISDIR(sctx->cur_inode_mode) && sctx->cur_inode_deleted) {
		/*
		 * Check if we can already rmdir the directory. If not,
		 * orphanize it. For every dir item inside that gets deleted
		 * later, we do this check again and rmdir it then if possible.
		 * See the use of check_dirs for more details.
		 */
		ret = can_rmdir(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				sctx->cur_ino);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = send_rmdir(sctx, valid_path);
			if (ret < 0)
				goto out;
		} else if (!is_orphan) {
			ret = orphanize_inode(sctx, sctx->cur_ino,
					sctx->cur_inode_gen, valid_path);
			if (ret < 0)
				goto out;
			is_orphan = 1;
		}

		list_for_each_entry(cur, &sctx->deleted_refs, list) {
			ret = dup_ref(cur, &check_dirs);
			if (ret < 0)
				goto out;
		}
	} else if (S_ISDIR(sctx->cur_inode_mode) &&
		   !list_empty(&sctx->deleted_refs)) {
		/*
		 * We have a moved dir. Add the old parent to check_dirs
		 */
		cur = list_entry(sctx->deleted_refs.next, struct recorded_ref,
				list);
		ret = dup_ref(cur, &check_dirs);
		if (ret < 0)
			goto out;
	} else if (!S_ISDIR(sctx->cur_inode_mode)) {
		/*
		 * We have a non dir inode. Go through all deleted refs and
		 * unlink them if they were not already overwritten by other
		 * inodes.
		 */
		list_for_each_entry(cur, &sctx->deleted_refs, list) {
			ret = did_overwrite_ref(sctx, cur->dir, cur->dir_gen,
					sctx->cur_ino, sctx->cur_inode_gen,
					cur->name, cur->name_len);
			if (ret < 0)
				goto out;
			if (!ret) {
				/*
				 * If we orphanized any ancestor before, we need
				 * to recompute the full path for deleted names,
				 * since any such path was computed before we
				 * processed any references and orphanized any
				 * ancestor inode.
				 */
				if (orphanized_ancestor) {
					ret = update_ref_path(sctx, cur);
					if (ret < 0)
						goto out;
				}
				ret = send_unlink(sctx, cur->full_path);
				if (ret < 0)
					goto out;
			}
			ret = dup_ref(cur, &check_dirs);
			if (ret < 0)
				goto out;
		}
		/*
		 * If the inode is still orphan, unlink the orphan. This may
		 * happen when a previous inode did overwrite the first ref
		 * of this inode and no new refs were added for the current
		 * inode. Unlinking does not mean that the inode is deleted in
		 * all cases. There may still be links to this inode in other
		 * places.
		 */
		if (is_orphan) {
			ret = send_unlink(sctx, valid_path);
			if (ret < 0)
				goto out;
		}
	}

	/*
	 * We did collect all parent dirs where cur_inode was once located. We
	 * now go through all these dirs and check if they are pending for
	 * deletion and if it's finally possible to perform the rmdir now.
	 * We also update the inode stats of the parent dirs here.
	 */
	list_for_each_entry(cur, &check_dirs, list) {
		/*
		 * In case we had refs into dirs that were not processed yet,
		 * we don't need to do the utime and rmdir logic for these dirs.
		 * The dir will be processed later.
		 */
		if (cur->dir > sctx->cur_ino)
			continue;

		ret = get_cur_inode_state(sctx, cur->dir, cur->dir_gen);
		if (ret < 0)
			goto out;

		if (ret == inode_state_did_create ||
		    ret == inode_state_no_change) {
			/* TODO delayed utimes */
			ret = send_utimes(sctx, cur->dir, cur->dir_gen);
			if (ret < 0)
				goto out;
		} else if (ret == inode_state_did_delete &&
			   cur->dir != last_dir_ino_rm) {
			ret = can_rmdir(sctx, cur->dir, cur->dir_gen,
					sctx->cur_ino);
			if (ret < 0)
				goto out;
			if (ret) {
				ret = get_cur_path(sctx, cur->dir,
						   cur->dir_gen, valid_path);
				if (ret < 0)
					goto out;
				ret = send_rmdir(sctx, valid_path);
				if (ret < 0)
					goto out;
				last_dir_ino_rm = cur->dir;
			}
		}
	}

	ret = 0;

out:
	__free_recorded_refs(&check_dirs);
	free_recorded_refs(sctx);
	fs_path_free(valid_path);
	return ret;
}

static int record_ref(struct btrfs_root *root, u64 dir, struct fs_path *name,
		      void *ctx, struct list_head *refs)
{
	int ret = 0;
	struct send_ctx *sctx = ctx;
	struct fs_path *p;
	u64 gen;

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	ret = get_inode_info(root, dir, NULL, &gen, NULL, NULL,
			NULL, NULL);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, dir, gen, p);
	if (ret < 0)
		goto out;
	ret = fs_path_add_path(p, name);
	if (ret < 0)
		goto out;

	ret = __record_ref(refs, dir, gen, p);

out:
	if (ret)
		fs_path_free(p);
	return ret;
}

static int __record_new_ref(int num, u64 dir, int index,
			    struct fs_path *name,
			    void *ctx)
{
	struct send_ctx *sctx = ctx;
	return record_ref(sctx->send_root, dir, name, ctx, &sctx->new_refs);
}


static int __record_deleted_ref(int num, u64 dir, int index,
				struct fs_path *name,
				void *ctx)
{
	struct send_ctx *sctx = ctx;
	return record_ref(sctx->parent_root, dir, name, ctx,
			  &sctx->deleted_refs);
}

static int record_new_ref(struct send_ctx *sctx)
{
	int ret;

	ret = iterate_inode_ref(sctx->send_root, sctx->left_path,
				sctx->cmp_key, 0, __record_new_ref, sctx);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	return ret;
}

static int record_deleted_ref(struct send_ctx *sctx)
{
	int ret;

	ret = iterate_inode_ref(sctx->parent_root, sctx->right_path,
				sctx->cmp_key, 0, __record_deleted_ref, sctx);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	return ret;
}

struct find_ref_ctx {
	u64 dir;
	u64 dir_gen;
	struct btrfs_root *root;
	struct fs_path *name;
	int found_idx;
};

static int __find_iref(int num, u64 dir, int index,
		       struct fs_path *name,
		       void *ctx_)
{
	struct find_ref_ctx *ctx = ctx_;
	u64 dir_gen;
	int ret;

	if (dir == ctx->dir && fs_path_len(name) == fs_path_len(ctx->name) &&
	    strncmp(name->start, ctx->name->start, fs_path_len(name)) == 0) {
		/*
		 * To avoid doing extra lookups we'll only do this if everything
		 * else matches.
		 */
		ret = get_inode_info(ctx->root, dir, NULL, &dir_gen, NULL,
				     NULL, NULL, NULL);
		if (ret)
			return ret;
		if (dir_gen != ctx->dir_gen)
			return 0;
		ctx->found_idx = num;
		return 1;
	}
	return 0;
}

static int find_iref(struct btrfs_root *root,
		     struct btrfs_path *path,
		     struct btrfs_key *key,
		     u64 dir, u64 dir_gen, struct fs_path *name)
{
	int ret;
	struct find_ref_ctx ctx;

	ctx.dir = dir;
	ctx.name = name;
	ctx.dir_gen = dir_gen;
	ctx.found_idx = -1;
	ctx.root = root;

	ret = iterate_inode_ref(root, path, key, 0, __find_iref, &ctx);
	if (ret < 0)
		return ret;

	if (ctx.found_idx == -1)
		return -ENOENT;

	return ctx.found_idx;
}

static int __record_changed_new_ref(int num, u64 dir, int index,
				    struct fs_path *name,
				    void *ctx)
{
	u64 dir_gen;
	int ret;
	struct send_ctx *sctx = ctx;

	ret = get_inode_info(sctx->send_root, dir, NULL, &dir_gen, NULL,
			     NULL, NULL, NULL);
	if (ret)
		return ret;

	ret = find_iref(sctx->parent_root, sctx->right_path,
			sctx->cmp_key, dir, dir_gen, name);
	if (ret == -ENOENT)
		ret = __record_new_ref(num, dir, index, name, sctx);
	else if (ret > 0)
		ret = 0;

	return ret;
}

static int __record_changed_deleted_ref(int num, u64 dir, int index,
					struct fs_path *name,
					void *ctx)
{
	u64 dir_gen;
	int ret;
	struct send_ctx *sctx = ctx;

	ret = get_inode_info(sctx->parent_root, dir, NULL, &dir_gen, NULL,
			     NULL, NULL, NULL);
	if (ret)
		return ret;

	ret = find_iref(sctx->send_root, sctx->left_path, sctx->cmp_key,
			dir, dir_gen, name);
	if (ret == -ENOENT)
		ret = __record_deleted_ref(num, dir, index, name, sctx);
	else if (ret > 0)
		ret = 0;

	return ret;
}

static int record_changed_ref(struct send_ctx *sctx)
{
	int ret = 0;

	ret = iterate_inode_ref(sctx->send_root, sctx->left_path,
			sctx->cmp_key, 0, __record_changed_new_ref, sctx);
	if (ret < 0)
		goto out;
	ret = iterate_inode_ref(sctx->parent_root, sctx->right_path,
			sctx->cmp_key, 0, __record_changed_deleted_ref, sctx);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	return ret;
}

/*
 * Record and process all refs at once. Needed when an inode changes the
 * generation number, which means that it was deleted and recreated.
 */
static int process_all_refs(struct send_ctx *sctx,
			    enum btrfs_compare_tree_result cmd)
{
	int ret;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	int slot;
	iterate_inode_ref_t cb;
	int pending_move = 0;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	if (cmd == BTRFS_COMPARE_TREE_NEW) {
		root = sctx->send_root;
		cb = __record_new_ref;
	} else if (cmd == BTRFS_COMPARE_TREE_DELETED) {
		root = sctx->parent_root;
		cb = __record_deleted_ref;
	} else {
		btrfs_err(sctx->send_root->fs_info,
				"Wrong command %d in process_all_refs", cmd);
		ret = -EINVAL;
		goto out;
	}

	key.objectid = sctx->cmp_key->objectid;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		eb = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(eb)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(eb, &found_key, slot);

		if (found_key.objectid != key.objectid ||
		    (found_key.type != BTRFS_INODE_REF_KEY &&
		     found_key.type != BTRFS_INODE_EXTREF_KEY))
			break;

		ret = iterate_inode_ref(root, path, &found_key, 0, cb, sctx);
		if (ret < 0)
			goto out;

		path->slots[0]++;
	}
	btrfs_release_path(path);

	/*
	 * We don't actually care about pending_move as we are simply
	 * re-creating this inode and will be rename'ing it into place once we
	 * rename the parent directory.
	 */
	ret = process_recorded_refs(sctx, &pending_move);
out:
	btrfs_free_path(path);
	return ret;
}

static int send_set_xattr(struct send_ctx *sctx,
			  struct fs_path *path,
			  const char *name, int name_len,
			  const char *data, int data_len)
{
	int ret = 0;

	ret = begin_cmd(sctx, BTRFS_SEND_C_SET_XATTR);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);
	TLV_PUT_STRING(sctx, BTRFS_SEND_A_XATTR_NAME, name, name_len);
	TLV_PUT(sctx, BTRFS_SEND_A_XATTR_DATA, data, data_len);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

static int send_remove_xattr(struct send_ctx *sctx,
			  struct fs_path *path,
			  const char *name, int name_len)
{
	int ret = 0;

	ret = begin_cmd(sctx, BTRFS_SEND_C_REMOVE_XATTR);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);
	TLV_PUT_STRING(sctx, BTRFS_SEND_A_XATTR_NAME, name, name_len);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

static int __process_new_xattr(int num, struct btrfs_key *di_key,
			       const char *name, int name_len,
			       const char *data, int data_len,
			       u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;
	struct fs_path *p;
	struct posix_acl_xattr_header dummy_acl;

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	/*
	 * This hack is needed because empty acls are stored as zero byte
	 * data in xattrs. Problem with that is, that receiving these zero byte
	 * acls will fail later. To fix this, we send a dummy acl list that
	 * only contains the version number and no entries.
	 */
	if (!strncmp(name, XATTR_NAME_POSIX_ACL_ACCESS, name_len) ||
	    !strncmp(name, XATTR_NAME_POSIX_ACL_DEFAULT, name_len)) {
		if (data_len == 0) {
			dummy_acl.a_version =
					cpu_to_le32(POSIX_ACL_XATTR_VERSION);
			data = (char *)&dummy_acl;
			data_len = sizeof(dummy_acl);
		}
	}

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	ret = send_set_xattr(sctx, p, name, name_len, data, data_len);

out:
	fs_path_free(p);
	return ret;
}

static int __process_deleted_xattr(int num, struct btrfs_key *di_key,
				   const char *name, int name_len,
				   const char *data, int data_len,
				   u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;
	struct fs_path *p;

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	ret = send_remove_xattr(sctx, p, name, name_len);

out:
	fs_path_free(p);
	return ret;
}

static int process_new_xattr(struct send_ctx *sctx)
{
	int ret = 0;

	ret = iterate_dir_item(sctx->send_root, sctx->left_path,
			       __process_new_xattr, sctx);

	return ret;
}

static int process_deleted_xattr(struct send_ctx *sctx)
{
	return iterate_dir_item(sctx->parent_root, sctx->right_path,
				__process_deleted_xattr, sctx);
}

struct find_xattr_ctx {
	const char *name;
	int name_len;
	int found_idx;
	char *found_data;
	int found_data_len;
};

static int __find_xattr(int num, struct btrfs_key *di_key,
			const char *name, int name_len,
			const char *data, int data_len,
			u8 type, void *vctx)
{
	struct find_xattr_ctx *ctx = vctx;

	if (name_len == ctx->name_len &&
	    strncmp(name, ctx->name, name_len) == 0) {
		ctx->found_idx = num;
		ctx->found_data_len = data_len;
		ctx->found_data = kmemdup(data, data_len, GFP_KERNEL);
		if (!ctx->found_data)
			return -ENOMEM;
		return 1;
	}
	return 0;
}

static int find_xattr(struct btrfs_root *root,
		      struct btrfs_path *path,
		      struct btrfs_key *key,
		      const char *name, int name_len,
		      char **data, int *data_len)
{
	int ret;
	struct find_xattr_ctx ctx;

	ctx.name = name;
	ctx.name_len = name_len;
	ctx.found_idx = -1;
	ctx.found_data = NULL;
	ctx.found_data_len = 0;

	ret = iterate_dir_item(root, path, __find_xattr, &ctx);
	if (ret < 0)
		return ret;

	if (ctx.found_idx == -1)
		return -ENOENT;
	if (data) {
		*data = ctx.found_data;
		*data_len = ctx.found_data_len;
	} else {
		kfree(ctx.found_data);
	}
	return ctx.found_idx;
}


static int __process_changed_new_xattr(int num, struct btrfs_key *di_key,
				       const char *name, int name_len,
				       const char *data, int data_len,
				       u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;
	char *found_data = NULL;
	int found_data_len  = 0;

	ret = find_xattr(sctx->parent_root, sctx->right_path,
			 sctx->cmp_key, name, name_len, &found_data,
			 &found_data_len);
	if (ret == -ENOENT) {
		ret = __process_new_xattr(num, di_key, name, name_len, data,
				data_len, type, ctx);
	} else if (ret >= 0) {
		if (data_len != found_data_len ||
		    memcmp(data, found_data, data_len)) {
			ret = __process_new_xattr(num, di_key, name, name_len,
					data, data_len, type, ctx);
		} else {
			ret = 0;
		}
	}

	kfree(found_data);
	return ret;
}

static int __process_changed_deleted_xattr(int num, struct btrfs_key *di_key,
					   const char *name, int name_len,
					   const char *data, int data_len,
					   u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;

	ret = find_xattr(sctx->send_root, sctx->left_path, sctx->cmp_key,
			 name, name_len, NULL, NULL);
	if (ret == -ENOENT)
		ret = __process_deleted_xattr(num, di_key, name, name_len, data,
				data_len, type, ctx);
	else if (ret >= 0)
		ret = 0;

	return ret;
}

static int process_changed_xattr(struct send_ctx *sctx)
{
	int ret = 0;

	ret = iterate_dir_item(sctx->send_root, sctx->left_path,
			__process_changed_new_xattr, sctx);
	if (ret < 0)
		goto out;
	ret = iterate_dir_item(sctx->parent_root, sctx->right_path,
			__process_changed_deleted_xattr, sctx);

out:
	return ret;
}

static int process_all_new_xattrs(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	int slot;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	root = sctx->send_root;

	key.objectid = sctx->cmp_key->objectid;
	key.type = BTRFS_XATTR_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		eb = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(eb)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = 0;
				break;
			}
			continue;
		}

		btrfs_item_key_to_cpu(eb, &found_key, slot);
		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			ret = 0;
			goto out;
		}

		ret = iterate_dir_item(root, path, __process_new_xattr, sctx);
		if (ret < 0)
			goto out;

		path->slots[0]++;
	}

out:
	btrfs_free_path(path);
	return ret;
}

static ssize_t fill_read_buf(struct send_ctx *sctx, u64 offset, u32 len)
{
	struct btrfs_root *root = sctx->send_root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct inode *inode;
	struct page *page;
	char *addr;
	struct btrfs_key key;
	pgoff_t index = offset >> PAGE_SHIFT;
	pgoff_t last_index;
	unsigned pg_offset = offset_in_page(offset);
	ssize_t ret = 0;

	key.objectid = sctx->cur_ino;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	inode = btrfs_iget(fs_info->sb, &key, root, NULL);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (offset + len > i_size_read(inode)) {
		if (offset > i_size_read(inode))
			len = 0;
		else
			len = offset - i_size_read(inode);
	}
	if (len == 0)
		goto out;

	last_index = (offset + len - 1) >> PAGE_SHIFT;

	/* initial readahead */
	memset(&sctx->ra, 0, sizeof(struct file_ra_state));
	file_ra_state_init(&sctx->ra, inode->i_mapping);

	while (index <= last_index) {
		unsigned cur_len = min_t(unsigned, len,
					 PAGE_SIZE - pg_offset);

		page = find_lock_page(inode->i_mapping, index);
		if (!page) {
			page_cache_sync_readahead(inode->i_mapping, &sctx->ra,
				NULL, index, last_index + 1 - index);

			page = find_or_create_page(inode->i_mapping, index,
					GFP_KERNEL);
			if (!page) {
				ret = -ENOMEM;
				break;
			}
		}

		if (PageReadahead(page)) {
			page_cache_async_readahead(inode->i_mapping, &sctx->ra,
				NULL, page, index, last_index + 1 - index);
		}

		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				put_page(page);
				ret = -EIO;
				break;
			}
		}

		addr = kmap(page);
		memcpy(sctx->read_buf + ret, addr + pg_offset, cur_len);
		kunmap(page);
		unlock_page(page);
		put_page(page);
		index++;
		pg_offset = 0;
		len -= cur_len;
		ret += cur_len;
	}
out:
	iput(inode);
	return ret;
}

/*
 * Read some bytes from the current inode/file and send a write command to
 * user space.
 */
static int send_write(struct send_ctx *sctx, u64 offset, u32 len)
{
	struct btrfs_fs_info *fs_info = sctx->send_root->fs_info;
	int ret = 0;
	struct fs_path *p;
	ssize_t num_read = 0;

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	btrfs_debug(fs_info, "send_write offset=%llu, len=%d", offset, len);

	num_read = fill_read_buf(sctx, offset, len);
	if (num_read <= 0) {
		if (num_read < 0)
			ret = num_read;
		goto out;
	}

	ret = begin_cmd(sctx, BTRFS_SEND_C_WRITE);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_FILE_OFFSET, offset);
	TLV_PUT(sctx, BTRFS_SEND_A_DATA, sctx->read_buf, num_read);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(p);
	if (ret < 0)
		return ret;
	return num_read;
}

/*
 * Send a clone command to user space.
 */
static int send_clone(struct send_ctx *sctx,
		      u64 offset, u32 len,
		      struct clone_root *clone_root)
{
	int ret = 0;
	struct fs_path *p;
	u64 gen;

	btrfs_debug(sctx->send_root->fs_info,
		    "send_clone offset=%llu, len=%d, clone_root=%llu, clone_inode=%llu, clone_offset=%llu",
		    offset, len, clone_root->root->root_key.objectid,
		    clone_root->ino, clone_root->offset);

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_CLONE);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	TLV_PUT_U64(sctx, BTRFS_SEND_A_FILE_OFFSET, offset);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_LEN, len);
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);

	if (clone_root->root == sctx->send_root) {
		ret = get_inode_info(sctx->send_root, clone_root->ino, NULL,
				&gen, NULL, NULL, NULL, NULL);
		if (ret < 0)
			goto out;
		ret = get_cur_path(sctx, clone_root->ino, gen, p);
	} else {
		ret = get_inode_path(clone_root->root, clone_root->ino, p);
	}
	if (ret < 0)
		goto out;

	/*
	 * If the parent we're using has a received_uuid set then use that as
	 * our clone source as that is what we will look for when doing a
	 * receive.
	 *
	 * This covers the case that we create a snapshot off of a received
	 * subvolume and then use that as the parent and try to receive on a
	 * different host.
	 */
	if (!btrfs_is_empty_uuid(clone_root->root->root_item.received_uuid))
		TLV_PUT_UUID(sctx, BTRFS_SEND_A_CLONE_UUID,
			     clone_root->root->root_item.received_uuid);
	else
		TLV_PUT_UUID(sctx, BTRFS_SEND_A_CLONE_UUID,
			     clone_root->root->root_item.uuid);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_CTRANSID,
		    le64_to_cpu(clone_root->root->root_item.ctransid));
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_CLONE_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_OFFSET,
			clone_root->offset);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(p);
	return ret;
}

/*
 * Send an update extent command to user space.
 */
static int send_update_extent(struct send_ctx *sctx,
			      u64 offset, u32 len)
{
	int ret = 0;
	struct fs_path *p;

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_UPDATE_EXTENT);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_FILE_OFFSET, offset);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_SIZE, len);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(p);
	return ret;
}

static int send_hole(struct send_ctx *sctx, u64 end)
{
	struct fs_path *p = NULL;
	u64 offset = sctx->cur_inode_last_extent;
	u64 len;
	int ret = 0;

	/*
	 * A hole that starts at EOF or beyond it. Since we do not yet support
	 * fallocate (for extent preallocation and hole punching), sending a
	 * write of zeroes starting at EOF or beyond would later require issuing
	 * a truncate operation which would undo the write and achieve nothing.
	 */
	if (offset >= sctx->cur_inode_size)
		return 0;

	/*
	 * Don't go beyond the inode's i_size due to prealloc extents that start
	 * after the i_size.
	 */
	end = min_t(u64, end, sctx->cur_inode_size);

	if (sctx->flags & BTRFS_SEND_FLAG_NO_FILE_DATA)
		return send_update_extent(sctx, offset, end - offset);

	p = fs_path_alloc();
	if (!p)
		return -ENOMEM;
	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto tlv_put_failure;
	memset(sctx->read_buf, 0, BTRFS_SEND_READ_SIZE);
	while (offset < end) {
		len = min_t(u64, end - offset, BTRFS_SEND_READ_SIZE);

		ret = begin_cmd(sctx, BTRFS_SEND_C_WRITE);
		if (ret < 0)
			break;
		TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
		TLV_PUT_U64(sctx, BTRFS_SEND_A_FILE_OFFSET, offset);
		TLV_PUT(sctx, BTRFS_SEND_A_DATA, sctx->read_buf, len);
		ret = send_cmd(sctx);
		if (ret < 0)
			break;
		offset += len;
	}
	sctx->cur_inode_next_write_offset = offset;
tlv_put_failure:
	fs_path_free(p);
	return ret;
}

static int send_extent_data(struct send_ctx *sctx,
			    const u64 offset,
			    const u64 len)
{
	u64 sent = 0;

	if (sctx->flags & BTRFS_SEND_FLAG_NO_FILE_DATA)
		return send_update_extent(sctx, offset, len);

	while (sent < len) {
		u64 size = len - sent;
		int ret;

		if (size > BTRFS_SEND_READ_SIZE)
			size = BTRFS_SEND_READ_SIZE;
		ret = send_write(sctx, offset + sent, size);
		if (ret < 0)
			return ret;
		if (!ret)
			break;
		sent += ret;
	}
	return 0;
}

static int clone_range(struct send_ctx *sctx,
		       struct clone_root *clone_root,
		       const u64 disk_byte,
		       u64 data_offset,
		       u64 offset,
		       u64 len)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;
	u64 clone_src_i_size;

	/*
	 * Prevent cloning from a zero offset with a length matching the sector
	 * size because in some scenarios this will make the receiver fail.
	 *
	 * For example, if in the source filesystem the extent at offset 0
	 * has a length of sectorsize and it was written using direct IO, then
	 * it can never be an inline extent (even if compression is enabled).
	 * Then this extent can be cloned in the original filesystem to a non
	 * zero file offset, but it may not be possible to clone in the
	 * destination filesystem because it can be inlined due to compression
	 * on the destination filesystem (as the receiver's write operations are
	 * always done using buffered IO). The same happens when the original
	 * filesystem does not have compression enabled but the destination
	 * filesystem has.
	 */
	if (clone_root->offset == 0 &&
	    len == sctx->send_root->fs_info->sectorsize)
		return send_extent_data(sctx, offset, len);

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	/*
	 * There are inodes that have extents that lie behind its i_size. Don't
	 * accept clones from these extents.
	 */
	ret = __get_inode_info(clone_root->root, path, clone_root->ino,
			       &clone_src_i_size, NULL, NULL, NULL, NULL, NULL);
	btrfs_release_path(path);
	if (ret < 0)
		goto out;

	/*
	 * We can't send a clone operation for the entire range if we find
	 * extent items in the respective range in the source file that
	 * refer to different extents or if we find holes.
	 * So check for that and do a mix of clone and regular write/copy
	 * operations if needed.
	 *
	 * Example:
	 *
	 * mkfs.btrfs -f /dev/sda
	 * mount /dev/sda /mnt
	 * xfs_io -f -c "pwrite -S 0xaa 0K 100K" /mnt/foo
	 * cp --reflink=always /mnt/foo /mnt/bar
	 * xfs_io -c "pwrite -S 0xbb 50K 50K" /mnt/foo
	 * btrfs subvolume snapshot -r /mnt /mnt/snap
	 *
	 * If when we send the snapshot and we are processing file bar (which
	 * has a higher inode number than foo) we blindly send a clone operation
	 * for the [0, 100K[ range from foo to bar, the receiver ends up getting
	 * a file bar that matches the content of file foo - iow, doesn't match
	 * the content from bar in the original filesystem.
	 */
	key.objectid = clone_root->ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = clone_root->offset;
	ret = btrfs_search_slot(NULL, clone_root->root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0 && path->slots[0] > 0) {
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0] - 1);
		if (key.objectid == clone_root->ino &&
		    key.type == BTRFS_EXTENT_DATA_KEY)
			path->slots[0]--;
	}

	while (true) {
		struct extent_buffer *leaf = path->nodes[0];
		int slot = path->slots[0];
		struct btrfs_file_extent_item *ei;
		u8 type;
		u64 ext_len;
		u64 clone_len;
		u64 clone_data_offset;

		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(clone_root->root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, slot);

		/*
		 * We might have an implicit trailing hole (NO_HOLES feature
		 * enabled). We deal with it after leaving this loop.
		 */
		if (key.objectid != clone_root->ino ||
		    key.type != BTRFS_EXTENT_DATA_KEY)
			break;

		ei = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
		type = btrfs_file_extent_type(leaf, ei);
		if (type == BTRFS_FILE_EXTENT_INLINE) {
			ext_len = btrfs_file_extent_ram_bytes(leaf, ei);
			ext_len = PAGE_ALIGN(ext_len);
		} else {
			ext_len = btrfs_file_extent_num_bytes(leaf, ei);
		}

		if (key.offset + ext_len <= clone_root->offset)
			goto next;

		if (key.offset > clone_root->offset) {
			/* Implicit hole, NO_HOLES feature enabled. */
			u64 hole_len = key.offset - clone_root->offset;

			if (hole_len > len)
				hole_len = len;
			ret = send_extent_data(sctx, offset, hole_len);
			if (ret < 0)
				goto out;

			len -= hole_len;
			if (len == 0)
				break;
			offset += hole_len;
			clone_root->offset += hole_len;
			data_offset += hole_len;
		}

		if (key.offset >= clone_root->offset + len)
			break;

		if (key.offset >= clone_src_i_size)
			break;

		if (key.offset + ext_len > clone_src_i_size)
			ext_len = clone_src_i_size - key.offset;

		clone_data_offset = btrfs_file_extent_offset(leaf, ei);
		if (btrfs_file_extent_disk_bytenr(leaf, ei) == disk_byte) {
			clone_root->offset = key.offset;
			if (clone_data_offset < data_offset &&
				clone_data_offset + ext_len > data_offset) {
				u64 extent_offset;

				extent_offset = data_offset - clone_data_offset;
				ext_len -= extent_offset;
				clone_data_offset += extent_offset;
				clone_root->offset += extent_offset;
			}
		}

		clone_len = min_t(u64, ext_len, len);

		if (btrfs_file_extent_disk_bytenr(leaf, ei) == disk_byte &&
		    clone_data_offset == data_offset) {
			const u64 src_end = clone_root->offset + clone_len;
			const u64 sectorsize = SZ_64K;

			/*
			 * We can't clone the last block, when its size is not
			 * sector size aligned, into the middle of a file. If we
			 * do so, the receiver will get a failure (-EINVAL) when
			 * trying to clone or will silently corrupt the data in
			 * the destination file if it's on a kernel without the
			 * fix introduced by commit ac765f83f1397646
			 * ("Btrfs: fix data corruption due to cloning of eof
			 * block).
			 *
			 * So issue a clone of the aligned down range plus a
			 * regular write for the eof block, if we hit that case.
			 *
			 * Also, we use the maximum possible sector size, 64K,
			 * because we don't know what's the sector size of the
			 * filesystem that receives the stream, so we have to
			 * assume the largest possible sector size.
			 */
			if (src_end == clone_src_i_size &&
			    !IS_ALIGNED(src_end, sectorsize) &&
			    offset + clone_len < sctx->cur_inode_size) {
				u64 slen;

				slen = ALIGN_DOWN(src_end - clone_root->offset,
						  sectorsize);
				if (slen > 0) {
					ret = send_clone(sctx, offset, slen,
							 clone_root);
					if (ret < 0)
						goto out;
				}
				ret = send_extent_data(sctx, offset + slen,
						       clone_len - slen);
			} else {
				ret = send_clone(sctx, offset, clone_len,
						 clone_root);
			}
		} else {
			ret = send_extent_data(sctx, offset, clone_len);
		}

		if (ret < 0)
			goto out;

		len -= clone_len;
		if (len == 0)
			break;
		offset += clone_len;
		clone_root->offset += clone_len;
		data_offset += clone_len;
next:
		path->slots[0]++;
	}

	if (len > 0)
		ret = send_extent_data(sctx, offset, len);
	else
		ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int send_write_or_clone(struct send_ctx *sctx,
			       struct btrfs_path *path,
			       struct btrfs_key *key,
			       struct clone_root *clone_root)
{
	int ret = 0;
	struct btrfs_file_extent_item *ei;
	u64 offset = key->offset;
	u64 len;
	u8 type;
	u64 bs = sctx->send_root->fs_info->sb->s_blocksize;

	ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
			struct btrfs_file_extent_item);
	type = btrfs_file_extent_type(path->nodes[0], ei);
	if (type == BTRFS_FILE_EXTENT_INLINE) {
		len = btrfs_file_extent_ram_bytes(path->nodes[0], ei);
		/*
		 * it is possible the inline item won't cover the whole page,
		 * but there may be items after this page.  Make
		 * sure to send the whole thing
		 */
		len = PAGE_ALIGN(len);
	} else {
		len = btrfs_file_extent_num_bytes(path->nodes[0], ei);
	}

	if (offset >= sctx->cur_inode_size) {
		ret = 0;
		goto out;
	}
	if (offset + len > sctx->cur_inode_size)
		len = sctx->cur_inode_size - offset;
	if (len == 0) {
		ret = 0;
		goto out;
	}

	if (clone_root && IS_ALIGNED(offset + len, bs)) {
		u64 disk_byte;
		u64 data_offset;

		disk_byte = btrfs_file_extent_disk_bytenr(path->nodes[0], ei);
		data_offset = btrfs_file_extent_offset(path->nodes[0], ei);
		ret = clone_range(sctx, clone_root, disk_byte, data_offset,
				  offset, len);
	} else {
		ret = send_extent_data(sctx, offset, len);
	}
	sctx->cur_inode_next_write_offset = offset + len;
out:
	return ret;
}

static int is_extent_unchanged(struct send_ctx *sctx,
			       struct btrfs_path *left_path,
			       struct btrfs_key *ekey)
{
	int ret = 0;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;
	struct extent_buffer *eb;
	int slot;
	struct btrfs_key found_key;
	struct btrfs_file_extent_item *ei;
	u64 left_disknr;
	u64 right_disknr;
	u64 left_offset;
	u64 right_offset;
	u64 left_offset_fixed;
	u64 left_len;
	u64 right_len;
	u64 left_gen;
	u64 right_gen;
	u8 left_type;
	u8 right_type;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	eb = left_path->nodes[0];
	slot = left_path->slots[0];
	ei = btrfs_item_ptr(eb, slot, struct btrfs_file_extent_item);
	left_type = btrfs_file_extent_type(eb, ei);

	if (left_type != BTRFS_FILE_EXTENT_REG) {
		ret = 0;
		goto out;
	}
	left_disknr = btrfs_file_extent_disk_bytenr(eb, ei);
	left_len = btrfs_file_extent_num_bytes(eb, ei);
	left_offset = btrfs_file_extent_offset(eb, ei);
	left_gen = btrfs_file_extent_generation(eb, ei);

	/*
	 * Following comments will refer to these graphics. L is the left
	 * extents which we are checking at the moment. 1-8 are the right
	 * extents that we iterate.
	 *
	 *       |-----L-----|
	 * |-1-|-2a-|-3-|-4-|-5-|-6-|
	 *
	 *       |-----L-----|
	 * |--1--|-2b-|...(same as above)
	 *
	 * Alternative situation. Happens on files where extents got split.
	 *       |-----L-----|
	 * |-----------7-----------|-6-|
	 *
	 * Alternative situation. Happens on files which got larger.
	 *       |-----L-----|
	 * |-8-|
	 * Nothing follows after 8.
	 */

	key.objectid = ekey->objectid;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = ekey->offset;
	ret = btrfs_search_slot_for_read(sctx->parent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = 0;
		goto out;
	}

	/*
	 * Handle special case where the right side has no extents at all.
	 */
	eb = path->nodes[0];
	slot = path->slots[0];
	btrfs_item_key_to_cpu(eb, &found_key, slot);
	if (found_key.objectid != key.objectid ||
	    found_key.type != key.type) {
		/* If we're a hole then just pretend nothing changed */
		ret = (left_disknr) ? 0 : 1;
		goto out;
	}

	/*
	 * We're now on 2a, 2b or 7.
	 */
	key = found_key;
	while (key.offset < ekey->offset + left_len) {
		ei = btrfs_item_ptr(eb, slot, struct btrfs_file_extent_item);
		right_type = btrfs_file_extent_type(eb, ei);
		if (right_type != BTRFS_FILE_EXTENT_REG &&
		    right_type != BTRFS_FILE_EXTENT_INLINE) {
			ret = 0;
			goto out;
		}

		if (right_type == BTRFS_FILE_EXTENT_INLINE) {
			right_len = btrfs_file_extent_ram_bytes(eb, ei);
			right_len = PAGE_ALIGN(right_len);
		} else {
			right_len = btrfs_file_extent_num_bytes(eb, ei);
		}

		/*
		 * Are we at extent 8? If yes, we know the extent is changed.
		 * This may only happen on the first iteration.
		 */
		if (found_key.offset + right_len <= ekey->offset) {
			/* If we're a hole just pretend nothing changed */
			ret = (left_disknr) ? 0 : 1;
			goto out;
		}

		/*
		 * We just wanted to see if when we have an inline extent, what
		 * follows it is a regular extent (wanted to check the above
		 * condition for inline extents too). This should normally not
		 * happen but it's possible for example when we have an inline
		 * compressed extent representing data with a size matching
		 * the page size (currently the same as sector size).
		 */
		if (right_type == BTRFS_FILE_EXTENT_INLINE) {
			ret = 0;
			goto out;
		}

		right_disknr = btrfs_file_extent_disk_bytenr(eb, ei);
		right_offset = btrfs_file_extent_offset(eb, ei);
		right_gen = btrfs_file_extent_generation(eb, ei);

		left_offset_fixed = left_offset;
		if (key.offset < ekey->offset) {
			/* Fix the right offset for 2a and 7. */
			right_offset += ekey->offset - key.offset;
		} else {
			/* Fix the left offset for all behind 2a and 2b */
			left_offset_fixed += key.offset - ekey->offset;
		}

		/*
		 * Check if we have the same extent.
		 */
		if (left_disknr != right_disknr ||
		    left_offset_fixed != right_offset ||
		    left_gen != right_gen) {
			ret = 0;
			goto out;
		}

		/*
		 * Go to the next extent.
		 */
		ret = btrfs_next_item(sctx->parent_root, path);
		if (ret < 0)
			goto out;
		if (!ret) {
			eb = path->nodes[0];
			slot = path->slots[0];
			btrfs_item_key_to_cpu(eb, &found_key, slot);
		}
		if (ret || found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			key.offset += right_len;
			break;
		}
		if (found_key.offset != key.offset + right_len) {
			ret = 0;
			goto out;
		}
		key = found_key;
	}

	/*
	 * We're now behind the left extent (treat as unchanged) or at the end
	 * of the right side (treat as changed).
	 */
	if (key.offset >= ekey->offset + left_len)
		ret = 1;
	else
		ret = 0;


out:
	btrfs_free_path(path);
	return ret;
}

static int get_last_extent(struct send_ctx *sctx, u64 offset)
{
	struct btrfs_path *path;
	struct btrfs_root *root = sctx->send_root;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	u64 extent_end;
	u8 type;
	int ret;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	sctx->cur_inode_last_extent = 0;

	key.objectid = sctx->cur_ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = offset;
	ret = btrfs_search_slot_for_read(root, &key, path, 0, 1);
	if (ret < 0)
		goto out;
	ret = 0;
	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
	if (key.objectid != sctx->cur_ino || key.type != BTRFS_EXTENT_DATA_KEY)
		goto out;

	fi = btrfs_item_ptr(path->nodes[0], path->slots[0],
			    struct btrfs_file_extent_item);
	type = btrfs_file_extent_type(path->nodes[0], fi);
	if (type == BTRFS_FILE_EXTENT_INLINE) {
		u64 size = btrfs_file_extent_ram_bytes(path->nodes[0], fi);
		extent_end = ALIGN(key.offset + size,
				   sctx->send_root->fs_info->sectorsize);
	} else {
		extent_end = key.offset +
			btrfs_file_extent_num_bytes(path->nodes[0], fi);
	}
	sctx->cur_inode_last_extent = extent_end;
out:
	btrfs_free_path(path);
	return ret;
}

static int range_is_hole_in_parent(struct send_ctx *sctx,
				   const u64 start,
				   const u64 end)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root *root = sctx->parent_root;
	u64 search_start = start;
	int ret;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = sctx->cur_ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = search_start;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret > 0 && path->slots[0] > 0)
		path->slots[0]--;

	while (search_start < end) {
		struct extent_buffer *leaf = path->nodes[0];
		int slot = path->slots[0];
		struct btrfs_file_extent_item *fi;
		u64 extent_end;

		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid < sctx->cur_ino ||
		    key.type < BTRFS_EXTENT_DATA_KEY)
			goto next;
		if (key.objectid > sctx->cur_ino ||
		    key.type > BTRFS_EXTENT_DATA_KEY ||
		    key.offset >= end)
			break;

		fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE) {
			u64 size = btrfs_file_extent_ram_bytes(leaf, fi);

			extent_end = ALIGN(key.offset + size,
					   root->fs_info->sectorsize);
		} else {
			extent_end = key.offset +
				btrfs_file_extent_num_bytes(leaf, fi);
		}
		if (extent_end <= start)
			goto next;
		if (btrfs_file_extent_disk_bytenr(leaf, fi) == 0) {
			search_start = extent_end;
			goto next;
		}
		ret = 0;
		goto out;
next:
		path->slots[0]++;
	}
	ret = 1;
out:
	btrfs_free_path(path);
	return ret;
}

static int maybe_send_hole(struct send_ctx *sctx, struct btrfs_path *path,
			   struct btrfs_key *key)
{
	struct btrfs_file_extent_item *fi;
	u64 extent_end;
	u8 type;
	int ret = 0;

	if (sctx->cur_ino != key->objectid || !need_send_hole(sctx))
		return 0;

	if (sctx->cur_inode_last_extent == (u64)-1) {
		ret = get_last_extent(sctx, key->offset - 1);
		if (ret)
			return ret;
	}

	fi = btrfs_item_ptr(path->nodes[0], path->slots[0],
			    struct btrfs_file_extent_item);
	type = btrfs_file_extent_type(path->nodes[0], fi);
	if (type == BTRFS_FILE_EXTENT_INLINE) {
		u64 size = btrfs_file_extent_ram_bytes(path->nodes[0], fi);
		extent_end = ALIGN(key->offset + size,
				   sctx->send_root->fs_info->sectorsize);
	} else {
		extent_end = key->offset +
			btrfs_file_extent_num_bytes(path->nodes[0], fi);
	}

	if (path->slots[0] == 0 &&
	    sctx->cur_inode_last_extent < key->offset) {
		/*
		 * We might have skipped entire leafs that contained only
		 * file extent items for our current inode. These leafs have
		 * a generation number smaller (older) than the one in the
		 * current leaf and the leaf our last extent came from, and
		 * are located between these 2 leafs.
		 */
		ret = get_last_extent(sctx, key->offset - 1);
		if (ret)
			return ret;
	}

	if (sctx->cur_inode_last_extent < key->offset) {
		ret = range_is_hole_in_parent(sctx,
					      sctx->cur_inode_last_extent,
					      key->offset);
		if (ret < 0)
			return ret;
		else if (ret == 0)
			ret = send_hole(sctx, key->offset);
		else
			ret = 0;
	}
	sctx->cur_inode_last_extent = extent_end;
	return ret;
}

static int process_extent(struct send_ctx *sctx,
			  struct btrfs_path *path,
			  struct btrfs_key *key)
{
	struct clone_root *found_clone = NULL;
	int ret = 0;

	if (S_ISLNK(sctx->cur_inode_mode))
		return 0;

	if (sctx->parent_root && !sctx->cur_inode_new) {
		ret = is_extent_unchanged(sctx, path, key);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = 0;
			goto out_hole;
		}
	} else {
		struct btrfs_file_extent_item *ei;
		u8 type;

		ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_file_extent_item);
		type = btrfs_file_extent_type(path->nodes[0], ei);
		if (type == BTRFS_FILE_EXTENT_PREALLOC ||
		    type == BTRFS_FILE_EXTENT_REG) {
			/*
			 * The send spec does not have a prealloc command yet,
			 * so just leave a hole for prealloc'ed extents until
			 * we have enough commands queued up to justify rev'ing
			 * the send spec.
			 */
			if (type == BTRFS_FILE_EXTENT_PREALLOC) {
				ret = 0;
				goto out;
			}

			/* Have a hole, just skip it. */
			if (btrfs_file_extent_disk_bytenr(path->nodes[0], ei) == 0) {
				ret = 0;
				goto out;
			}
		}
	}

	ret = find_extent_clone(sctx, path, key->objectid, key->offset,
			sctx->cur_inode_size, &found_clone);
	if (ret != -ENOENT && ret < 0)
		goto out;

	ret = send_write_or_clone(sctx, path, key, found_clone);
	if (ret)
		goto out;
out_hole:
	ret = maybe_send_hole(sctx, path, key);
out:
	return ret;
}

static int process_all_extents(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	int slot;

	root = sctx->send_root;
	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = sctx->cmp_key->objectid;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		eb = path->nodes[0];
		slot = path->slots[0];

		if (slot >= btrfs_header_nritems(eb)) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = 0;
				break;
			}
			continue;
		}

		btrfs_item_key_to_cpu(eb, &found_key, slot);

		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			ret = 0;
			goto out;
		}

		ret = process_extent(sctx, path, &found_key);
		if (ret < 0)
			goto out;

		path->slots[0]++;
	}

out:
	btrfs_free_path(path);
	return ret;
}

static int process_recorded_refs_if_needed(struct send_ctx *sctx, int at_end,
					   int *pending_move,
					   int *refs_processed)
{
	int ret = 0;

	if (sctx->cur_ino == 0)
		goto out;
	if (!at_end && sctx->cur_ino == sctx->cmp_key->objectid &&
	    sctx->cmp_key->type <= BTRFS_INODE_EXTREF_KEY)
		goto out;
	if (list_empty(&sctx->new_refs) && list_empty(&sctx->deleted_refs))
		goto out;

	ret = process_recorded_refs(sctx, pending_move);
	if (ret < 0)
		goto out;

	*refs_processed = 1;
out:
	return ret;
}

static int finish_inode_if_needed(struct send_ctx *sctx, int at_end)
{
	int ret = 0;
	u64 left_mode;
	u64 left_uid;
	u64 left_gid;
	u64 right_mode;
	u64 right_uid;
	u64 right_gid;
	int need_chmod = 0;
	int need_chown = 0;
	int need_truncate = 1;
	int pending_move = 0;
	int refs_processed = 0;

	if (sctx->ignore_cur_inode)
		return 0;

	ret = process_recorded_refs_if_needed(sctx, at_end, &pending_move,
					      &refs_processed);
	if (ret < 0)
		goto out;

	/*
	 * We have processed the refs and thus need to advance send_progress.
	 * Now, calls to get_cur_xxx will take the updated refs of the current
	 * inode into account.
	 *
	 * On the other hand, if our current inode is a directory and couldn't
	 * be moved/renamed because its parent was renamed/moved too and it has
	 * a higher inode number, we can only move/rename our current inode
	 * after we moved/renamed its parent. Therefore in this case operate on
	 * the old path (pre move/rename) of our current inode, and the
	 * move/rename will be performed later.
	 */
	if (refs_processed && !pending_move)
		sctx->send_progress = sctx->cur_ino + 1;

	if (sctx->cur_ino == 0 || sctx->cur_inode_deleted)
		goto out;
	if (!at_end && sctx->cmp_key->objectid == sctx->cur_ino)
		goto out;

	ret = get_inode_info(sctx->send_root, sctx->cur_ino, NULL, NULL,
			&left_mode, &left_uid, &left_gid, NULL);
	if (ret < 0)
		goto out;

	if (!sctx->parent_root || sctx->cur_inode_new) {
		need_chown = 1;
		if (!S_ISLNK(sctx->cur_inode_mode))
			need_chmod = 1;
		if (sctx->cur_inode_next_write_offset == sctx->cur_inode_size)
			need_truncate = 0;
	} else {
		u64 old_size;

		ret = get_inode_info(sctx->parent_root, sctx->cur_ino,
				&old_size, NULL, &right_mode, &right_uid,
				&right_gid, NULL);
		if (ret < 0)
			goto out;

		if (left_uid != right_uid || left_gid != right_gid)
			need_chown = 1;
		if (!S_ISLNK(sctx->cur_inode_mode) && left_mode != right_mode)
			need_chmod = 1;
		if ((old_size == sctx->cur_inode_size) ||
		    (sctx->cur_inode_size > old_size &&
		     sctx->cur_inode_next_write_offset == sctx->cur_inode_size))
			need_truncate = 0;
	}

	if (S_ISREG(sctx->cur_inode_mode)) {
		if (need_send_hole(sctx)) {
			if (sctx->cur_inode_last_extent == (u64)-1 ||
			    sctx->cur_inode_last_extent <
			    sctx->cur_inode_size) {
				ret = get_last_extent(sctx, (u64)-1);
				if (ret)
					goto out;
			}
			if (sctx->cur_inode_last_extent <
			    sctx->cur_inode_size) {
				ret = send_hole(sctx, sctx->cur_inode_size);
				if (ret)
					goto out;
			}
		}
		if (need_truncate) {
			ret = send_truncate(sctx, sctx->cur_ino,
					    sctx->cur_inode_gen,
					    sctx->cur_inode_size);
			if (ret < 0)
				goto out;
		}
	}

	if (need_chown) {
		ret = send_chown(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				left_uid, left_gid);
		if (ret < 0)
			goto out;
	}
	if (need_chmod) {
		ret = send_chmod(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				left_mode);
		if (ret < 0)
			goto out;
	}

	/*
	 * If other directory inodes depended on our current directory
	 * inode's move/rename, now do their move/rename operations.
	 */
	if (!is_waiting_for_move(sctx, sctx->cur_ino)) {
		ret = apply_children_dir_moves(sctx);
		if (ret)
			goto out;
		/*
		 * Need to send that every time, no matter if it actually
		 * changed between the two trees as we have done changes to
		 * the inode before. If our inode is a directory and it's
		 * waiting to be moved/renamed, we will send its utimes when
		 * it's moved/renamed, therefore we don't need to do it here.
		 */
		sctx->send_progress = sctx->cur_ino + 1;
		ret = send_utimes(sctx, sctx->cur_ino, sctx->cur_inode_gen);
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

struct parent_paths_ctx {
	struct list_head *refs;
	struct send_ctx *sctx;
};

static int record_parent_ref(int num, u64 dir, int index, struct fs_path *name,
			     void *ctx)
{
	struct parent_paths_ctx *ppctx = ctx;

	return record_ref(ppctx->sctx->parent_root, dir, name, ppctx->sctx,
			  ppctx->refs);
}

/*
 * Issue unlink operations for all paths of the current inode found in the
 * parent snapshot.
 */
static int btrfs_unlink_all_paths(struct send_ctx *sctx)
{
	LIST_HEAD(deleted_refs);
	struct btrfs_path *path;
	struct btrfs_key key;
	struct parent_paths_ctx ctx;
	int ret;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = sctx->cur_ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, sctx->parent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	ctx.refs = &deleted_refs;
	ctx.sctx = sctx;

	while (true) {
		struct extent_buffer *eb = path->nodes[0];
		int slot = path->slots[0];

		if (slot >= btrfs_header_nritems(eb)) {
			ret = btrfs_next_leaf(sctx->parent_root, path);
			if (ret < 0)
				goto out;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(eb, &key, slot);
		if (key.objectid != sctx->cur_ino)
			break;
		if (key.type != BTRFS_INODE_REF_KEY &&
		    key.type != BTRFS_INODE_EXTREF_KEY)
			break;

		ret = iterate_inode_ref(sctx->parent_root, path, &key, 1,
					record_parent_ref, &ctx);
		if (ret < 0)
			goto out;

		path->slots[0]++;
	}

	while (!list_empty(&deleted_refs)) {
		struct recorded_ref *ref;

		ref = list_first_entry(&deleted_refs, struct recorded_ref, list);
		ret = send_unlink(sctx, ref->full_path);
		if (ret < 0)
			goto out;
		fs_path_free(ref->full_path);
		list_del(&ref->list);
		kfree(ref);
	}
	ret = 0;
out:
	btrfs_free_path(path);
	if (ret)
		__free_recorded_refs(&deleted_refs);
	return ret;
}

static int changed_inode(struct send_ctx *sctx,
			 enum btrfs_compare_tree_result result)
{
	int ret = 0;
	struct btrfs_key *key = sctx->cmp_key;
	struct btrfs_inode_item *left_ii = NULL;
	struct btrfs_inode_item *right_ii = NULL;
	u64 left_gen = 0;
	u64 right_gen = 0;

	sctx->cur_ino = key->objectid;
	sctx->cur_inode_new_gen = 0;
	sctx->cur_inode_last_extent = (u64)-1;
	sctx->cur_inode_next_write_offset = 0;
	sctx->ignore_cur_inode = false;

	/*
	 * Set send_progress to current inode. This will tell all get_cur_xxx
	 * functions that the current inode's refs are not updated yet. Later,
	 * when process_recorded_refs is finished, it is set to cur_ino + 1.
	 */
	sctx->send_progress = sctx->cur_ino;

	if (result == BTRFS_COMPARE_TREE_NEW ||
	    result == BTRFS_COMPARE_TREE_CHANGED) {
		left_ii = btrfs_item_ptr(sctx->left_path->nodes[0],
				sctx->left_path->slots[0],
				struct btrfs_inode_item);
		left_gen = btrfs_inode_generation(sctx->left_path->nodes[0],
				left_ii);
	} else {
		right_ii = btrfs_item_ptr(sctx->right_path->nodes[0],
				sctx->right_path->slots[0],
				struct btrfs_inode_item);
		right_gen = btrfs_inode_generation(sctx->right_path->nodes[0],
				right_ii);
	}
	if (result == BTRFS_COMPARE_TREE_CHANGED) {
		right_ii = btrfs_item_ptr(sctx->right_path->nodes[0],
				sctx->right_path->slots[0],
				struct btrfs_inode_item);

		right_gen = btrfs_inode_generation(sctx->right_path->nodes[0],
				right_ii);

		/*
		 * The cur_ino = root dir case is special here. We can't treat
		 * the inode as deleted+reused because it would generate a
		 * stream that tries to delete/mkdir the root dir.
		 */
		if (left_gen != right_gen &&
		    sctx->cur_ino != BTRFS_FIRST_FREE_OBJECTID)
			sctx->cur_inode_new_gen = 1;
	}

	/*
	 * Normally we do not find inodes with a link count of zero (orphans)
	 * because the most common case is to create a snapshot and use it
	 * for a send operation. However other less common use cases involve
	 * using a subvolume and send it after turning it to RO mode just
	 * after deleting all hard links of a file while holding an open
	 * file descriptor against it or turning a RO snapshot into RW mode,
	 * keep an open file descriptor against a file, delete it and then
	 * turn the snapshot back to RO mode before using it for a send
	 * operation. So if we find such cases, ignore the inode and all its
	 * items completely if it's a new inode, or if it's a changed inode
	 * make sure all its previous paths (from the parent snapshot) are all
	 * unlinked and all other the inode items are ignored.
	 */
	if (result == BTRFS_COMPARE_TREE_NEW ||
	    result == BTRFS_COMPARE_TREE_CHANGED) {
		u32 nlinks;

		nlinks = btrfs_inode_nlink(sctx->left_path->nodes[0], left_ii);
		if (nlinks == 0) {
			sctx->ignore_cur_inode = true;
			if (result == BTRFS_COMPARE_TREE_CHANGED)
				ret = btrfs_unlink_all_paths(sctx);
			goto out;
		}
	}

	if (result == BTRFS_COMPARE_TREE_NEW) {
		sctx->cur_inode_gen = left_gen;
		sctx->cur_inode_new = 1;
		sctx->cur_inode_deleted = 0;
		sctx->cur_inode_size = btrfs_inode_size(
				sctx->left_path->nodes[0], left_ii);
		sctx->cur_inode_mode = btrfs_inode_mode(
				sctx->left_path->nodes[0], left_ii);
		sctx->cur_inode_rdev = btrfs_inode_rdev(
				sctx->left_path->nodes[0], left_ii);
		if (sctx->cur_ino != BTRFS_FIRST_FREE_OBJECTID)
			ret = send_create_inode_if_needed(sctx);
	} else if (result == BTRFS_COMPARE_TREE_DELETED) {
		sctx->cur_inode_gen = right_gen;
		sctx->cur_inode_new = 0;
		sctx->cur_inode_deleted = 1;
		sctx->cur_inode_size = btrfs_inode_size(
				sctx->right_path->nodes[0], right_ii);
		sctx->cur_inode_mode = btrfs_inode_mode(
				sctx->right_path->nodes[0], right_ii);
	} else if (result == BTRFS_COMPARE_TREE_CHANGED) {
		/*
		 * We need to do some special handling in case the inode was
		 * reported as changed with a changed generation number. This
		 * means that the original inode was deleted and new inode
		 * reused the same inum. So we have to treat the old inode as
		 * deleted and the new one as new.
		 */
		if (sctx->cur_inode_new_gen) {
			/*
			 * First, process the inode as if it was deleted.
			 */
			sctx->cur_inode_gen = right_gen;
			sctx->cur_inode_new = 0;
			sctx->cur_inode_deleted = 1;
			sctx->cur_inode_size = btrfs_inode_size(
					sctx->right_path->nodes[0], right_ii);
			sctx->cur_inode_mode = btrfs_inode_mode(
					sctx->right_path->nodes[0], right_ii);
			ret = process_all_refs(sctx,
					BTRFS_COMPARE_TREE_DELETED);
			if (ret < 0)
				goto out;

			/*
			 * Now process the inode as if it was new.
			 */
			sctx->cur_inode_gen = left_gen;
			sctx->cur_inode_new = 1;
			sctx->cur_inode_deleted = 0;
			sctx->cur_inode_size = btrfs_inode_size(
					sctx->left_path->nodes[0], left_ii);
			sctx->cur_inode_mode = btrfs_inode_mode(
					sctx->left_path->nodes[0], left_ii);
			sctx->cur_inode_rdev = btrfs_inode_rdev(
					sctx->left_path->nodes[0], left_ii);
			ret = send_create_inode_if_needed(sctx);
			if (ret < 0)
				goto out;

			ret = process_all_refs(sctx, BTRFS_COMPARE_TREE_NEW);
			if (ret < 0)
				goto out;
			/*
			 * Advance send_progress now as we did not get into
			 * process_recorded_refs_if_needed in the new_gen case.
			 */
			sctx->send_progress = sctx->cur_ino + 1;

			/*
			 * Now process all extents and xattrs of the inode as if
			 * they were all new.
			 */
			ret = process_all_extents(sctx);
			if (ret < 0)
				goto out;
			ret = process_all_new_xattrs(sctx);
			if (ret < 0)
				goto out;
		} else {
			sctx->cur_inode_gen = left_gen;
			sctx->cur_inode_new = 0;
			sctx->cur_inode_new_gen = 0;
			sctx->cur_inode_deleted = 0;
			sctx->cur_inode_size = btrfs_inode_size(
					sctx->left_path->nodes[0], left_ii);
			sctx->cur_inode_mode = btrfs_inode_mode(
					sctx->left_path->nodes[0], left_ii);
		}
	}

out:
	return ret;
}

/*
 * We have to process new refs before deleted refs, but compare_trees gives us
 * the new and deleted refs mixed. To fix this, we record the new/deleted refs
 * first and later process them in process_recorded_refs.
 * For the cur_inode_new_gen case, we skip recording completely because
 * changed_inode did already initiate processing of refs. The reason for this is
 * that in this case, compare_tree actually compares the refs of 2 different
 * inodes. To fix this, process_all_refs is used in changed_inode to handle all
 * refs of the right tree as deleted and all refs of the left tree as new.
 */
static int changed_ref(struct send_ctx *sctx,
		       enum btrfs_compare_tree_result result)
{
	int ret = 0;

	if (sctx->cur_ino != sctx->cmp_key->objectid) {
		inconsistent_snapshot_error(sctx, result, "reference");
		return -EIO;
	}

	if (!sctx->cur_inode_new_gen &&
	    sctx->cur_ino != BTRFS_FIRST_FREE_OBJECTID) {
		if (result == BTRFS_COMPARE_TREE_NEW)
			ret = record_new_ref(sctx);
		else if (result == BTRFS_COMPARE_TREE_DELETED)
			ret = record_deleted_ref(sctx);
		else if (result == BTRFS_COMPARE_TREE_CHANGED)
			ret = record_changed_ref(sctx);
	}

	return ret;
}

/*
 * Process new/deleted/changed xattrs. We skip processing in the
 * cur_inode_new_gen case because changed_inode did already initiate processing
 * of xattrs. The reason is the same as in changed_ref
 */
static int changed_xattr(struct send_ctx *sctx,
			 enum btrfs_compare_tree_result result)
{
	int ret = 0;

	if (sctx->cur_ino != sctx->cmp_key->objectid) {
		inconsistent_snapshot_error(sctx, result, "xattr");
		return -EIO;
	}

	if (!sctx->cur_inode_new_gen && !sctx->cur_inode_deleted) {
		if (result == BTRFS_COMPARE_TREE_NEW)
			ret = process_new_xattr(sctx);
		else if (result == BTRFS_COMPARE_TREE_DELETED)
			ret = process_deleted_xattr(sctx);
		else if (result == BTRFS_COMPARE_TREE_CHANGED)
			ret = process_changed_xattr(sctx);
	}

	return ret;
}

/*
 * Process new/deleted/changed extents. We skip processing in the
 * cur_inode_new_gen case because changed_inode did already initiate processing
 * of extents. The reason is the same as in changed_ref
 */
static int changed_extent(struct send_ctx *sctx,
			  enum btrfs_compare_tree_result result)
{
	int ret = 0;

	if (sctx->cur_ino != sctx->cmp_key->objectid) {

		if (result == BTRFS_COMPARE_TREE_CHANGED) {
			struct extent_buffer *leaf_l;
			struct extent_buffer *leaf_r;
			struct btrfs_file_extent_item *ei_l;
			struct btrfs_file_extent_item *ei_r;

			leaf_l = sctx->left_path->nodes[0];
			leaf_r = sctx->right_path->nodes[0];
			ei_l = btrfs_item_ptr(leaf_l,
					      sctx->left_path->slots[0],
					      struct btrfs_file_extent_item);
			ei_r = btrfs_item_ptr(leaf_r,
					      sctx->right_path->slots[0],
					      struct btrfs_file_extent_item);

			/*
			 * We may have found an extent item that has changed
			 * only its disk_bytenr field and the corresponding
			 * inode item was not updated. This case happens due to
			 * very specific timings during relocation when a leaf
			 * that contains file extent items is COWed while
			 * relocation is ongoing and its in the stage where it
			 * updates data pointers. So when this happens we can
			 * safely ignore it since we know it's the same extent,
			 * but just at different logical and physical locations
			 * (when an extent is fully replaced with a new one, we
			 * know the generation number must have changed too,
			 * since snapshot creation implies committing the current
			 * transaction, and the inode item must have been updated
			 * as well).
			 * This replacement of the disk_bytenr happens at
			 * relocation.c:replace_file_extents() through
			 * relocation.c:btrfs_reloc_cow_block().
			 */
			if (btrfs_file_extent_generation(leaf_l, ei_l) ==
			    btrfs_file_extent_generation(leaf_r, ei_r) &&
			    btrfs_file_extent_ram_bytes(leaf_l, ei_l) ==
			    btrfs_file_extent_ram_bytes(leaf_r, ei_r) &&
			    btrfs_file_extent_compression(leaf_l, ei_l) ==
			    btrfs_file_extent_compression(leaf_r, ei_r) &&
			    btrfs_file_extent_encryption(leaf_l, ei_l) ==
			    btrfs_file_extent_encryption(leaf_r, ei_r) &&
			    btrfs_file_extent_other_encoding(leaf_l, ei_l) ==
			    btrfs_file_extent_other_encoding(leaf_r, ei_r) &&
			    btrfs_file_extent_type(leaf_l, ei_l) ==
			    btrfs_file_extent_type(leaf_r, ei_r) &&
			    btrfs_file_extent_disk_bytenr(leaf_l, ei_l) !=
			    btrfs_file_extent_disk_bytenr(leaf_r, ei_r) &&
			    btrfs_file_extent_disk_num_bytes(leaf_l, ei_l) ==
			    btrfs_file_extent_disk_num_bytes(leaf_r, ei_r) &&
			    btrfs_file_extent_offset(leaf_l, ei_l) ==
			    btrfs_file_extent_offset(leaf_r, ei_r) &&
			    btrfs_file_extent_num_bytes(leaf_l, ei_l) ==
			    btrfs_file_extent_num_bytes(leaf_r, ei_r))
				return 0;
		}

		inconsistent_snapshot_error(sctx, result, "extent");
		return -EIO;
	}

	if (!sctx->cur_inode_new_gen && !sctx->cur_inode_deleted) {
		if (result != BTRFS_COMPARE_TREE_DELETED)
			ret = process_extent(sctx, sctx->left_path,
					sctx->cmp_key);
	}

	return ret;
}

static int dir_changed(struct send_ctx *sctx, u64 dir)
{
	u64 orig_gen, new_gen;
	int ret;

	ret = get_inode_info(sctx->send_root, dir, NULL, &new_gen, NULL, NULL,
			     NULL, NULL);
	if (ret)
		return ret;

	ret = get_inode_info(sctx->parent_root, dir, NULL, &orig_gen, NULL,
			     NULL, NULL, NULL);
	if (ret)
		return ret;

	return (orig_gen != new_gen) ? 1 : 0;
}

static int compare_refs(struct send_ctx *sctx, struct btrfs_path *path,
			struct btrfs_key *key)
{
	struct btrfs_inode_extref *extref;
	struct extent_buffer *leaf;
	u64 dirid = 0, last_dirid = 0;
	unsigned long ptr;
	u32 item_size;
	u32 cur_offset = 0;
	int ref_name_len;
	int ret = 0;

	/* Easy case, just check this one dirid */
	if (key->type == BTRFS_INODE_REF_KEY) {
		dirid = key->offset;

		ret = dir_changed(sctx, dirid);
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
	while (cur_offset < item_size) {
		extref = (struct btrfs_inode_extref *)(ptr +
						       cur_offset);
		dirid = btrfs_inode_extref_parent(leaf, extref);
		ref_name_len = btrfs_inode_extref_name_len(leaf, extref);
		cur_offset += ref_name_len + sizeof(*extref);
		if (dirid == last_dirid)
			continue;
		ret = dir_changed(sctx, dirid);
		if (ret)
			break;
		last_dirid = dirid;
	}
out:
	return ret;
}

/*
 * Updates compare related fields in sctx and simply forwards to the actual
 * changed_xxx functions.
 */
static int changed_cb(struct btrfs_path *left_path,
		      struct btrfs_path *right_path,
		      struct btrfs_key *key,
		      enum btrfs_compare_tree_result result,
		      void *ctx)
{
	int ret = 0;
	struct send_ctx *sctx = ctx;

	if (result == BTRFS_COMPARE_TREE_SAME) {
		if (key->type == BTRFS_INODE_REF_KEY ||
		    key->type == BTRFS_INODE_EXTREF_KEY) {
			ret = compare_refs(sctx, left_path, key);
			if (!ret)
				return 0;
			if (ret < 0)
				return ret;
		} else if (key->type == BTRFS_EXTENT_DATA_KEY) {
			return maybe_send_hole(sctx, left_path, key);
		} else {
			return 0;
		}
		result = BTRFS_COMPARE_TREE_CHANGED;
		ret = 0;
	}

	sctx->left_path = left_path;
	sctx->right_path = right_path;
	sctx->cmp_key = key;

	ret = finish_inode_if_needed(sctx, 0);
	if (ret < 0)
		goto out;

	/* Ignore non-FS objects */
	if (key->objectid == BTRFS_FREE_INO_OBJECTID ||
	    key->objectid == BTRFS_FREE_SPACE_OBJECTID)
		goto out;

	if (key->type == BTRFS_INODE_ITEM_KEY) {
		ret = changed_inode(sctx, result);
	} else if (!sctx->ignore_cur_inode) {
		if (key->type == BTRFS_INODE_REF_KEY ||
		    key->type == BTRFS_INODE_EXTREF_KEY)
			ret = changed_ref(sctx, result);
		else if (key->type == BTRFS_XATTR_ITEM_KEY)
			ret = changed_xattr(sctx, result);
		else if (key->type == BTRFS_EXTENT_DATA_KEY)
			ret = changed_extent(sctx, result);
	}

out:
	return ret;
}

static int full_send_tree(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_root *send_root = sctx->send_root;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *eb;
	int slot;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_FIRST_FREE_OBJECTID;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(send_root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (ret)
		goto out_finish;

	while (1) {
		eb = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(eb, &key, slot);

		ret = changed_cb(path, NULL, &key,
				 BTRFS_COMPARE_TREE_NEW, sctx);
		if (ret < 0)
			goto out;

		ret = btrfs_next_item(send_root, path);
		if (ret < 0)
			goto out;
		if (ret) {
			ret  = 0;
			break;
		}
	}

out_finish:
	ret = finish_inode_if_needed(sctx, 1);

out:
	btrfs_free_path(path);
	return ret;
}

static int send_subvol(struct send_ctx *sctx)
{
	int ret;

	if (!(sctx->flags & BTRFS_SEND_FLAG_OMIT_STREAM_HEADER)) {
		ret = send_header(sctx);
		if (ret < 0)
			goto out;
	}

	ret = send_subvol_begin(sctx);
	if (ret < 0)
		goto out;

	if (sctx->parent_root) {
		ret = btrfs_compare_trees(sctx->send_root, sctx->parent_root,
				changed_cb, sctx);
		if (ret < 0)
			goto out;
		ret = finish_inode_if_needed(sctx, 1);
		if (ret < 0)
			goto out;
	} else {
		ret = full_send_tree(sctx);
		if (ret < 0)
			goto out;
	}

out:
	free_recorded_refs(sctx);
	return ret;
}

/*
 * If orphan cleanup did remove any orphans from a root, it means the tree
 * was modified and therefore the commit root is not the same as the current
 * root anymore. This is a problem, because send uses the commit root and
 * therefore can see inode items that don't exist in the current root anymore,
 * and for example make calls to btrfs_iget, which will do tree lookups based
 * on the current root and not on the commit root. Those lookups will fail,
 * returning a -ESTALE error, and making send fail with that error. So make
 * sure a send does not see any orphans we have just removed, and that it will
 * see the same inodes regardless of whether a transaction commit happened
 * before it started (meaning that the commit root will be the same as the
 * current root) or not.
 */
static int ensure_commit_roots_uptodate(struct send_ctx *sctx)
{
	int i;
	struct btrfs_trans_handle *trans = NULL;

again:
	if (sctx->parent_root &&
	    sctx->parent_root->node != sctx->parent_root->commit_root)
		goto commit_trans;

	for (i = 0; i < sctx->clone_roots_cnt; i++)
		if (sctx->clone_roots[i].root->node !=
		    sctx->clone_roots[i].root->commit_root)
			goto commit_trans;

	if (trans)
		return btrfs_end_transaction(trans);

	return 0;

commit_trans:
	/* Use any root, all fs roots will get their commit roots updated. */
	if (!trans) {
		trans = btrfs_join_transaction(sctx->send_root);
		if (IS_ERR(trans))
			return PTR_ERR(trans);
		goto again;
	}

	return btrfs_commit_transaction(trans);
}

/*
 * Make sure any existing dellaloc is flushed for any root used by a send
 * operation so that we do not miss any data and we do not race with writeback
 * finishing and changing a tree while send is using the tree. This could
 * happen if a subvolume is in RW mode, has delalloc, is turned to RO mode and
 * a send operation then uses the subvolume.
 * After flushing delalloc ensure_commit_roots_uptodate() must be called.
 */
static int flush_delalloc_roots(struct send_ctx *sctx)
{
	struct btrfs_root *root = sctx->parent_root;
	int ret;
	int i;

	if (root) {
		ret = btrfs_start_delalloc_snapshot(root);
		if (ret)
			return ret;
		btrfs_wait_ordered_extents(root, U64_MAX, 0, U64_MAX);
	}

	for (i = 0; i < sctx->clone_roots_cnt; i++) {
		root = sctx->clone_roots[i].root;
		ret = btrfs_start_delalloc_snapshot(root);
		if (ret)
			return ret;
		btrfs_wait_ordered_extents(root, U64_MAX, 0, U64_MAX);
	}

	return 0;
}

static void btrfs_root_dec_send_in_progress(struct btrfs_root* root)
{
	spin_lock(&root->root_item_lock);
	root->send_in_progress--;
	/*
	 * Not much left to do, we don't know why it's unbalanced and
	 * can't blindly reset it to 0.
	 */
	if (root->send_in_progress < 0)
		btrfs_err(root->fs_info,
			  "send_in_progress unbalanced %d root %llu",
			  root->send_in_progress, root->root_key.objectid);
	spin_unlock(&root->root_item_lock);
}

static void dedupe_in_progress_warn(const struct btrfs_root *root)
{
	btrfs_warn_rl(root->fs_info,
"cannot use root %llu for send while deduplications on it are in progress (%d in progress)",
		      root->root_key.objectid, root->dedupe_in_progress);
}

long btrfs_ioctl_send(struct file *mnt_file, struct btrfs_ioctl_send_args *arg)
{
	int ret = 0;
	struct btrfs_root *send_root = BTRFS_I(file_inode(mnt_file))->root;
	struct btrfs_fs_info *fs_info = send_root->fs_info;
	struct btrfs_root *clone_root;
	struct btrfs_key key;
	struct send_ctx *sctx = NULL;
	u32 i;
	u64 *clone_sources_tmp = NULL;
	int clone_sources_to_rollback = 0;
	unsigned alloc_size;
	int sort_clone_roots = 0;
	int index;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * The subvolume must remain read-only during send, protect against
	 * making it RW. This also protects against deletion.
	 */
	spin_lock(&send_root->root_item_lock);
	if (btrfs_root_readonly(send_root) && send_root->dedupe_in_progress) {
		dedupe_in_progress_warn(send_root);
		spin_unlock(&send_root->root_item_lock);
		return -EAGAIN;
	}
	send_root->send_in_progress++;
	spin_unlock(&send_root->root_item_lock);

	/*
	 * This is done when we lookup the root, it should already be complete
	 * by the time we get here.
	 */
	WARN_ON(send_root->orphan_cleanup_state != ORPHAN_CLEANUP_DONE);

	/*
	 * Userspace tools do the checks and warn the user if it's
	 * not RO.
	 */
	if (!btrfs_root_readonly(send_root)) {
		ret = -EPERM;
		goto out;
	}

	/*
	 * Check that we don't overflow at later allocations, we request
	 * clone_sources_count + 1 items, and compare to unsigned long inside
	 * access_ok.
	 */
	if (arg->clone_sources_count >
	    ULONG_MAX / sizeof(struct clone_root) - 1) {
		ret = -EINVAL;
		goto out;
	}

	if (!access_ok(arg->clone_sources,
			sizeof(*arg->clone_sources) *
			arg->clone_sources_count)) {
		ret = -EFAULT;
		goto out;
	}

	if (arg->flags & ~BTRFS_SEND_FLAG_MASK) {
		ret = -EINVAL;
		goto out;
	}

	sctx = kzalloc(sizeof(struct send_ctx), GFP_KERNEL);
	if (!sctx) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&sctx->new_refs);
	INIT_LIST_HEAD(&sctx->deleted_refs);
	INIT_RADIX_TREE(&sctx->name_cache, GFP_KERNEL);
	INIT_LIST_HEAD(&sctx->name_cache_list);

	sctx->flags = arg->flags;

	sctx->send_filp = fget(arg->send_fd);
	if (!sctx->send_filp) {
		ret = -EBADF;
		goto out;
	}

	sctx->send_root = send_root;
	/*
	 * Unlikely but possible, if the subvolume is marked for deletion but
	 * is slow to remove the directory entry, send can still be started
	 */
	if (btrfs_root_dead(sctx->send_root)) {
		ret = -EPERM;
		goto out;
	}

	sctx->clone_roots_cnt = arg->clone_sources_count;

	sctx->send_max_size = BTRFS_SEND_BUF_SIZE;
	sctx->send_buf = kvmalloc(sctx->send_max_size, GFP_KERNEL);
	if (!sctx->send_buf) {
		ret = -ENOMEM;
		goto out;
	}

	sctx->read_buf = kvmalloc(BTRFS_SEND_READ_SIZE, GFP_KERNEL);
	if (!sctx->read_buf) {
		ret = -ENOMEM;
		goto out;
	}

	sctx->pending_dir_moves = RB_ROOT;
	sctx->waiting_dir_moves = RB_ROOT;
	sctx->orphan_dirs = RB_ROOT;

	alloc_size = sizeof(struct clone_root) * (arg->clone_sources_count + 1);

	sctx->clone_roots = kzalloc(alloc_size, GFP_KERNEL);
	if (!sctx->clone_roots) {
		ret = -ENOMEM;
		goto out;
	}

	alloc_size = arg->clone_sources_count * sizeof(*arg->clone_sources);

	if (arg->clone_sources_count) {
		clone_sources_tmp = kvmalloc(alloc_size, GFP_KERNEL);
		if (!clone_sources_tmp) {
			ret = -ENOMEM;
			goto out;
		}

		ret = copy_from_user(clone_sources_tmp, arg->clone_sources,
				alloc_size);
		if (ret) {
			ret = -EFAULT;
			goto out;
		}

		for (i = 0; i < arg->clone_sources_count; i++) {
			key.objectid = clone_sources_tmp[i];
			key.type = BTRFS_ROOT_ITEM_KEY;
			key.offset = (u64)-1;

			index = srcu_read_lock(&fs_info->subvol_srcu);

			clone_root = btrfs_read_fs_root_no_name(fs_info, &key);
			if (IS_ERR(clone_root)) {
				srcu_read_unlock(&fs_info->subvol_srcu, index);
				ret = PTR_ERR(clone_root);
				goto out;
			}
			spin_lock(&clone_root->root_item_lock);
			if (!btrfs_root_readonly(clone_root) ||
			    btrfs_root_dead(clone_root)) {
				spin_unlock(&clone_root->root_item_lock);
				srcu_read_unlock(&fs_info->subvol_srcu, index);
				ret = -EPERM;
				goto out;
			}
			if (clone_root->dedupe_in_progress) {
				dedupe_in_progress_warn(clone_root);
				spin_unlock(&clone_root->root_item_lock);
				srcu_read_unlock(&fs_info->subvol_srcu, index);
				ret = -EAGAIN;
				goto out;
			}
			clone_root->send_in_progress++;
			spin_unlock(&clone_root->root_item_lock);
			srcu_read_unlock(&fs_info->subvol_srcu, index);

			sctx->clone_roots[i].root = clone_root;
			clone_sources_to_rollback = i + 1;
		}
		kvfree(clone_sources_tmp);
		clone_sources_tmp = NULL;
	}

	if (arg->parent_root) {
		key.objectid = arg->parent_root;
		key.type = BTRFS_ROOT_ITEM_KEY;
		key.offset = (u64)-1;

		index = srcu_read_lock(&fs_info->subvol_srcu);

		sctx->parent_root = btrfs_read_fs_root_no_name(fs_info, &key);
		if (IS_ERR(sctx->parent_root)) {
			srcu_read_unlock(&fs_info->subvol_srcu, index);
			ret = PTR_ERR(sctx->parent_root);
			goto out;
		}

		spin_lock(&sctx->parent_root->root_item_lock);
		sctx->parent_root->send_in_progress++;
		if (!btrfs_root_readonly(sctx->parent_root) ||
				btrfs_root_dead(sctx->parent_root)) {
			spin_unlock(&sctx->parent_root->root_item_lock);
			srcu_read_unlock(&fs_info->subvol_srcu, index);
			ret = -EPERM;
			goto out;
		}
		if (sctx->parent_root->dedupe_in_progress) {
			dedupe_in_progress_warn(sctx->parent_root);
			spin_unlock(&sctx->parent_root->root_item_lock);
			srcu_read_unlock(&fs_info->subvol_srcu, index);
			ret = -EAGAIN;
			goto out;
		}
		spin_unlock(&sctx->parent_root->root_item_lock);

		srcu_read_unlock(&fs_info->subvol_srcu, index);
	}

	/*
	 * Clones from send_root are allowed, but only if the clone source
	 * is behind the current send position. This is checked while searching
	 * for possible clone sources.
	 */
	sctx->clone_roots[sctx->clone_roots_cnt++].root = sctx->send_root;

	/* We do a bsearch later */
	sort(sctx->clone_roots, sctx->clone_roots_cnt,
			sizeof(*sctx->clone_roots), __clone_root_cmp_sort,
			NULL);
	sort_clone_roots = 1;

	ret = flush_delalloc_roots(sctx);
	if (ret)
		goto out;

	ret = ensure_commit_roots_uptodate(sctx);
	if (ret)
		goto out;

	current->journal_info = BTRFS_SEND_TRANS_STUB;
	ret = send_subvol(sctx);
	current->journal_info = NULL;
	if (ret < 0)
		goto out;

	if (!(sctx->flags & BTRFS_SEND_FLAG_OMIT_END_CMD)) {
		ret = begin_cmd(sctx, BTRFS_SEND_C_END);
		if (ret < 0)
			goto out;
		ret = send_cmd(sctx);
		if (ret < 0)
			goto out;
	}

out:
	WARN_ON(sctx && !ret && !RB_EMPTY_ROOT(&sctx->pending_dir_moves));
	while (sctx && !RB_EMPTY_ROOT(&sctx->pending_dir_moves)) {
		struct rb_node *n;
		struct pending_dir_move *pm;

		n = rb_first(&sctx->pending_dir_moves);
		pm = rb_entry(n, struct pending_dir_move, node);
		while (!list_empty(&pm->list)) {
			struct pending_dir_move *pm2;

			pm2 = list_first_entry(&pm->list,
					       struct pending_dir_move, list);
			free_pending_move(sctx, pm2);
		}
		free_pending_move(sctx, pm);
	}

	WARN_ON(sctx && !ret && !RB_EMPTY_ROOT(&sctx->waiting_dir_moves));
	while (sctx && !RB_EMPTY_ROOT(&sctx->waiting_dir_moves)) {
		struct rb_node *n;
		struct waiting_dir_move *dm;

		n = rb_first(&sctx->waiting_dir_moves);
		dm = rb_entry(n, struct waiting_dir_move, node);
		rb_erase(&dm->node, &sctx->waiting_dir_moves);
		kfree(dm);
	}

	WARN_ON(sctx && !ret && !RB_EMPTY_ROOT(&sctx->orphan_dirs));
	while (sctx && !RB_EMPTY_ROOT(&sctx->orphan_dirs)) {
		struct rb_node *n;
		struct orphan_dir_info *odi;

		n = rb_first(&sctx->orphan_dirs);
		odi = rb_entry(n, struct orphan_dir_info, node);
		free_orphan_dir_info(sctx, odi);
	}

	if (sort_clone_roots) {
		for (i = 0; i < sctx->clone_roots_cnt; i++)
			btrfs_root_dec_send_in_progress(
					sctx->clone_roots[i].root);
	} else {
		for (i = 0; sctx && i < clone_sources_to_rollback; i++)
			btrfs_root_dec_send_in_progress(
					sctx->clone_roots[i].root);

		btrfs_root_dec_send_in_progress(send_root);
	}
	if (sctx && !IS_ERR_OR_NULL(sctx->parent_root))
		btrfs_root_dec_send_in_progress(sctx->parent_root);

	kvfree(clone_sources_tmp);

	if (sctx) {
		if (sctx->send_filp)
			fput(sctx->send_filp);

		kvfree(sctx->clone_roots);
		kvfree(sctx->send_buf);
		kvfree(sctx->read_buf);

		name_cache_free(sctx);

		kfree(sctx);
	}

	return ret;
}
