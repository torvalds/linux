// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/hfsplus/attributes.c
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Handling of records in attributes tree
 */

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

static struct kmem_cache *hfsplus_attr_tree_cachep;

int __init hfsplus_create_attr_tree_cache(void)
{
	if (hfsplus_attr_tree_cachep)
		return -EEXIST;

	hfsplus_attr_tree_cachep =
		kmem_cache_create("hfsplus_attr_cache",
			sizeof(hfsplus_attr_entry), 0,
			SLAB_HWCACHE_ALIGN, NULL);
	if (!hfsplus_attr_tree_cachep)
		return -ENOMEM;

	return 0;
}

void hfsplus_destroy_attr_tree_cache(void)
{
	kmem_cache_destroy(hfsplus_attr_tree_cachep);
}

int hfsplus_attr_bin_cmp_key(const hfsplus_btree_key *k1,
				const hfsplus_btree_key *k2)
{
	__be32 k1_cnid, k2_cnid;

	k1_cnid = k1->attr.cnid;
	k2_cnid = k2->attr.cnid;
	if (k1_cnid != k2_cnid)
		return be32_to_cpu(k1_cnid) < be32_to_cpu(k2_cnid) ? -1 : 1;

	return hfsplus_strcmp(
			(const struct hfsplus_unistr *)&k1->attr.key_name,
			(const struct hfsplus_unistr *)&k2->attr.key_name);
}

int hfsplus_attr_build_key(struct super_block *sb, hfsplus_btree_key *key,
			u32 cnid, const char *name)
{
	int len;

	memset(key, 0, sizeof(struct hfsplus_attr_key));
	key->attr.cnid = cpu_to_be32(cnid);
	if (name) {
		int res = hfsplus_asc2uni(sb,
				(struct hfsplus_unistr *)&key->attr.key_name,
				HFSPLUS_ATTR_MAX_STRLEN, name, strlen(name));
		if (res)
			return res;
		len = be16_to_cpu(key->attr.key_name.length);
	} else {
		key->attr.key_name.length = 0;
		len = 0;
	}

	/* The length of the key, as stored in key_len field, does not include
	 * the size of the key_len field itself.
	 * So, offsetof(hfsplus_attr_key, key_name) is a trick because
	 * it takes into consideration key_len field (__be16) of
	 * hfsplus_attr_key structure instead of length field (__be16) of
	 * hfsplus_attr_unistr structure.
	 */
	key->key_len =
		cpu_to_be16(offsetof(struct hfsplus_attr_key, key_name) +
				2 * len);

	return 0;
}

hfsplus_attr_entry *hfsplus_alloc_attr_entry(void)
{
	return kmem_cache_alloc(hfsplus_attr_tree_cachep, GFP_KERNEL);
}

void hfsplus_destroy_attr_entry(hfsplus_attr_entry *entry)
{
	if (entry)
		kmem_cache_free(hfsplus_attr_tree_cachep, entry);
}

#define HFSPLUS_INVALID_ATTR_RECORD -1

static int hfsplus_attr_build_record(hfsplus_attr_entry *entry, int record_type,
				u32 cnid, const void *value, size_t size)
{
	if (record_type == HFSPLUS_ATTR_FORK_DATA) {
		/*
		 * Mac OS X supports only inline data attributes.
		 * Do nothing
		 */
		memset(entry, 0, sizeof(*entry));
		return sizeof(struct hfsplus_attr_fork_data);
	} else if (record_type == HFSPLUS_ATTR_EXTENTS) {
		/*
		 * Mac OS X supports only inline data attributes.
		 * Do nothing.
		 */
		memset(entry, 0, sizeof(*entry));
		return sizeof(struct hfsplus_attr_extents);
	} else if (record_type == HFSPLUS_ATTR_INLINE_DATA) {
		u16 len;

		memset(entry, 0, sizeof(struct hfsplus_attr_inline_data));
		entry->inline_data.record_type = cpu_to_be32(record_type);
		if (size <= HFSPLUS_MAX_INLINE_DATA_SIZE)
			len = size;
		else
			return HFSPLUS_INVALID_ATTR_RECORD;
		entry->inline_data.length = cpu_to_be16(len);
		memcpy(entry->inline_data.raw_bytes, value, len);
		/*
		 * Align len on two-byte boundary.
		 * It needs to add pad byte if we have odd len.
		 */
		len = round_up(len, 2);
		return offsetof(struct hfsplus_attr_inline_data, raw_bytes) +
					len;
	} else /* invalid input */
		memset(entry, 0, sizeof(*entry));

	return HFSPLUS_INVALID_ATTR_RECORD;
}

int hfsplus_find_attr(struct super_block *sb, u32 cnid,
			const char *name, struct hfs_find_data *fd)
{
	int err = 0;

	hfs_dbg(ATTR_MOD, "find_attr: %s,%d\n", name ? name : NULL, cnid);

	if (!HFSPLUS_SB(sb)->attr_tree) {
		pr_err("attributes file doesn't exist\n");
		return -EINVAL;
	}

	if (name) {
		err = hfsplus_attr_build_key(sb, fd->search_key, cnid, name);
		if (err)
			goto failed_find_attr;
		err = hfs_brec_find(fd, hfs_find_rec_by_key);
		if (err)
			goto failed_find_attr;
	} else {
		err = hfsplus_attr_build_key(sb, fd->search_key, cnid, NULL);
		if (err)
			goto failed_find_attr;
		err = hfs_brec_find(fd, hfs_find_1st_rec_by_cnid);
		if (err)
			goto failed_find_attr;
	}

failed_find_attr:
	return err;
}

int hfsplus_attr_exists(struct inode *inode, const char *name)
{
	int err = 0;
	struct super_block *sb = inode->i_sb;
	struct hfs_find_data fd;

	if (!HFSPLUS_SB(sb)->attr_tree)
		return 0;

	err = hfs_find_init(HFSPLUS_SB(sb)->attr_tree, &fd);
	if (err)
		return 0;

	err = hfsplus_find_attr(sb, inode->i_ino, name, &fd);
	if (err)
		goto attr_not_found;

	hfs_find_exit(&fd);
	return 1;

attr_not_found:
	hfs_find_exit(&fd);
	return 0;
}

int hfsplus_create_attr(struct inode *inode,
				const char *name,
				const void *value, size_t size)
{
	struct super_block *sb = inode->i_sb;
	struct hfs_find_data fd;
	hfsplus_attr_entry *entry_ptr;
	int entry_size;
	int err;

	hfs_dbg(ATTR_MOD, "create_attr: %s,%ld\n",
		name ? name : NULL, inode->i_ino);

	if (!HFSPLUS_SB(sb)->attr_tree) {
		pr_err("attributes file doesn't exist\n");
		return -EINVAL;
	}

	entry_ptr = hfsplus_alloc_attr_entry();
	if (!entry_ptr)
		return -ENOMEM;

	err = hfs_find_init(HFSPLUS_SB(sb)->attr_tree, &fd);
	if (err)
		goto failed_init_create_attr;

	/* Fail early and avoid ENOSPC during the btree operation */
	err = hfs_bmap_reserve(fd.tree, fd.tree->depth + 1);
	if (err)
		goto failed_create_attr;

	if (name) {
		err = hfsplus_attr_build_key(sb, fd.search_key,
						inode->i_ino, name);
		if (err)
			goto failed_create_attr;
	} else {
		err = -EINVAL;
		goto failed_create_attr;
	}

	/* Mac OS X supports only inline data attributes. */
	entry_size = hfsplus_attr_build_record(entry_ptr,
					HFSPLUS_ATTR_INLINE_DATA,
					inode->i_ino,
					value, size);
	if (entry_size == HFSPLUS_INVALID_ATTR_RECORD) {
		err = -EINVAL;
		goto failed_create_attr;
	}

	err = hfs_brec_find(&fd, hfs_find_rec_by_key);
	if (err != -ENOENT) {
		if (!err)
			err = -EEXIST;
		goto failed_create_attr;
	}

	err = hfs_brec_insert(&fd, entry_ptr, entry_size);
	if (err)
		goto failed_create_attr;

	hfsplus_mark_inode_dirty(inode, HFSPLUS_I_ATTR_DIRTY);

failed_create_attr:
	hfs_find_exit(&fd);

failed_init_create_attr:
	hfsplus_destroy_attr_entry(entry_ptr);
	return err;
}

static int __hfsplus_delete_attr(struct inode *inode, u32 cnid,
					struct hfs_find_data *fd)
{
	int err = 0;
	__be32 found_cnid, record_type;

	hfs_bnode_read(fd->bnode, &found_cnid,
			fd->keyoffset +
			offsetof(struct hfsplus_attr_key, cnid),
			sizeof(__be32));
	if (cnid != be32_to_cpu(found_cnid))
		return -ENOENT;

	hfs_bnode_read(fd->bnode, &record_type,
			fd->entryoffset, sizeof(record_type));

	switch (be32_to_cpu(record_type)) {
	case HFSPLUS_ATTR_INLINE_DATA:
		/* All is OK. Do nothing. */
		break;
	case HFSPLUS_ATTR_FORK_DATA:
	case HFSPLUS_ATTR_EXTENTS:
		pr_err("only inline data xattr are supported\n");
		return -EOPNOTSUPP;
	default:
		pr_err("invalid extended attribute record\n");
		return -ENOENT;
	}

	/* Avoid btree corruption */
	hfs_bnode_read(fd->bnode, fd->search_key,
			fd->keyoffset, fd->keylength);

	err = hfs_brec_remove(fd);
	if (err)
		return err;

	hfsplus_mark_inode_dirty(inode, HFSPLUS_I_ATTR_DIRTY);
	return err;
}

int hfsplus_delete_attr(struct inode *inode, const char *name)
{
	int err = 0;
	struct super_block *sb = inode->i_sb;
	struct hfs_find_data fd;

	hfs_dbg(ATTR_MOD, "delete_attr: %s,%ld\n",
		name ? name : NULL, inode->i_ino);

	if (!HFSPLUS_SB(sb)->attr_tree) {
		pr_err("attributes file doesn't exist\n");
		return -EINVAL;
	}

	err = hfs_find_init(HFSPLUS_SB(sb)->attr_tree, &fd);
	if (err)
		return err;

	/* Fail early and avoid ENOSPC during the btree operation */
	err = hfs_bmap_reserve(fd.tree, fd.tree->depth);
	if (err)
		goto out;

	if (name) {
		err = hfsplus_attr_build_key(sb, fd.search_key,
						inode->i_ino, name);
		if (err)
			goto out;
	} else {
		pr_err("invalid extended attribute name\n");
		err = -EINVAL;
		goto out;
	}

	err = hfs_brec_find(&fd, hfs_find_rec_by_key);
	if (err)
		goto out;

	err = __hfsplus_delete_attr(inode, inode->i_ino, &fd);
	if (err)
		goto out;

out:
	hfs_find_exit(&fd);
	return err;
}

int hfsplus_delete_all_attrs(struct inode *dir, u32 cnid)
{
	int err = 0;
	struct hfs_find_data fd;

	hfs_dbg(ATTR_MOD, "delete_all_attrs: %d\n", cnid);

	if (!HFSPLUS_SB(dir->i_sb)->attr_tree) {
		pr_err("attributes file doesn't exist\n");
		return -EINVAL;
	}

	err = hfs_find_init(HFSPLUS_SB(dir->i_sb)->attr_tree, &fd);
	if (err)
		return err;

	for (;;) {
		err = hfsplus_find_attr(dir->i_sb, cnid, NULL, &fd);
		if (err) {
			if (err != -ENOENT)
				pr_err("xattr search failed\n");
			goto end_delete_all;
		}

		err = __hfsplus_delete_attr(dir, cnid, &fd);
		if (err)
			goto end_delete_all;
	}

end_delete_all:
	hfs_find_exit(&fd);
	return err;
}
