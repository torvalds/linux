// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/hfsplus/xattr.c
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Logic of processing extended attributes
 */

#include "hfsplus_fs.h"
#include <linux/nls.h>
#include "xattr.h"

static int hfsplus_removexattr(struct inode *inode, const char *name);

const struct xattr_handler *hfsplus_xattr_handlers[] = {
	&hfsplus_xattr_osx_handler,
	&hfsplus_xattr_user_handler,
	&hfsplus_xattr_trusted_handler,
	&hfsplus_xattr_security_handler,
	NULL
};

static int strcmp_xattr_finder_info(const char *name)
{
	if (name) {
		return strncmp(name, HFSPLUS_XATTR_FINDER_INFO_NAME,
				sizeof(HFSPLUS_XATTR_FINDER_INFO_NAME));
	}
	return -1;
}

static int strcmp_xattr_acl(const char *name)
{
	if (name) {
		return strncmp(name, HFSPLUS_XATTR_ACL_NAME,
				sizeof(HFSPLUS_XATTR_ACL_NAME));
	}
	return -1;
}

static bool is_known_namespace(const char *name)
{
	if (strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN) &&
	    strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN) &&
	    strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN) &&
	    strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN))
		return false;

	return true;
}

static void hfsplus_init_header_node(struct inode *attr_file,
					u32 clump_size,
					char *buf, u16 node_size)
{
	struct hfs_bnode_desc *desc;
	struct hfs_btree_header_rec *head;
	u16 offset;
	__be16 *rec_offsets;
	u32 hdr_node_map_rec_bits;
	char *bmp;
	u32 used_nodes;
	u32 used_bmp_bytes;
	u64 tmp;

	hfs_dbg(ATTR_MOD, "init_hdr_attr_file: clump %u, node_size %u\n",
		clump_size, node_size);

	/* The end of the node contains list of record offsets */
	rec_offsets = (__be16 *)(buf + node_size);

	desc = (struct hfs_bnode_desc *)buf;
	desc->type = HFS_NODE_HEADER;
	desc->num_recs = cpu_to_be16(HFSPLUS_BTREE_HDR_NODE_RECS_COUNT);
	offset = sizeof(struct hfs_bnode_desc);
	*--rec_offsets = cpu_to_be16(offset);

	head = (struct hfs_btree_header_rec *)(buf + offset);
	head->node_size = cpu_to_be16(node_size);
	tmp = i_size_read(attr_file);
	do_div(tmp, node_size);
	head->node_count = cpu_to_be32(tmp);
	head->free_nodes = cpu_to_be32(be32_to_cpu(head->node_count) - 1);
	head->clump_size = cpu_to_be32(clump_size);
	head->attributes |= cpu_to_be32(HFS_TREE_BIGKEYS | HFS_TREE_VARIDXKEYS);
	head->max_key_len = cpu_to_be16(HFSPLUS_ATTR_KEYLEN - sizeof(u16));
	offset += sizeof(struct hfs_btree_header_rec);
	*--rec_offsets = cpu_to_be16(offset);
	offset += HFSPLUS_BTREE_HDR_USER_BYTES;
	*--rec_offsets = cpu_to_be16(offset);

	hdr_node_map_rec_bits = 8 * (node_size - offset - (4 * sizeof(u16)));
	if (be32_to_cpu(head->node_count) > hdr_node_map_rec_bits) {
		u32 map_node_bits;
		u32 map_nodes;

		desc->next = cpu_to_be32(be32_to_cpu(head->leaf_tail) + 1);
		map_node_bits = 8 * (node_size - sizeof(struct hfs_bnode_desc) -
					(2 * sizeof(u16)) - 2);
		map_nodes = (be32_to_cpu(head->node_count) -
				hdr_node_map_rec_bits +
				(map_node_bits - 1)) / map_node_bits;
		be32_add_cpu(&head->free_nodes, 0 - map_nodes);
	}

	bmp = buf + offset;
	used_nodes =
		be32_to_cpu(head->node_count) - be32_to_cpu(head->free_nodes);
	used_bmp_bytes = used_nodes / 8;
	if (used_bmp_bytes) {
		memset(bmp, 0xFF, used_bmp_bytes);
		bmp += used_bmp_bytes;
		used_nodes %= 8;
	}
	*bmp = ~(0xFF >> used_nodes);
	offset += hdr_node_map_rec_bits / 8;
	*--rec_offsets = cpu_to_be16(offset);
}

static int hfsplus_create_attributes_file(struct super_block *sb)
{
	int err = 0;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct inode *attr_file;
	struct hfsplus_inode_info *hip;
	u32 clump_size;
	u16 node_size = HFSPLUS_ATTR_TREE_NODE_SIZE;
	char *buf;
	int index, written;
	struct address_space *mapping;
	struct page *page;
	int old_state = HFSPLUS_EMPTY_ATTR_TREE;

	hfs_dbg(ATTR_MOD, "create_attr_file: ino %d\n", HFSPLUS_ATTR_CNID);

check_attr_tree_state_again:
	switch (atomic_read(&sbi->attr_tree_state)) {
	case HFSPLUS_EMPTY_ATTR_TREE:
		if (old_state != atomic_cmpxchg(&sbi->attr_tree_state,
						old_state,
						HFSPLUS_CREATING_ATTR_TREE))
			goto check_attr_tree_state_again;
		break;
	case HFSPLUS_CREATING_ATTR_TREE:
		/*
		 * This state means that another thread is in process
		 * of AttributesFile creation. Theoretically, it is
		 * possible to be here. But really __setxattr() method
		 * first of all calls hfs_find_init() for lookup in
		 * B-tree of CatalogFile. This method locks mutex of
		 * CatalogFile's B-tree. As a result, if some thread
		 * is inside AttributedFile creation operation then
		 * another threads will be waiting unlocking of
		 * CatalogFile's B-tree's mutex. However, if code will
		 * change then we will return error code (-EAGAIN) from
		 * here. Really, it means that first try to set of xattr
		 * fails with error but second attempt will have success.
		 */
		return -EAGAIN;
	case HFSPLUS_VALID_ATTR_TREE:
		return 0;
	case HFSPLUS_FAILED_ATTR_TREE:
		return -EOPNOTSUPP;
	default:
		BUG();
	}

	attr_file = hfsplus_iget(sb, HFSPLUS_ATTR_CNID);
	if (IS_ERR(attr_file)) {
		pr_err("failed to load attributes file\n");
		return PTR_ERR(attr_file);
	}

	BUG_ON(i_size_read(attr_file) != 0);

	hip = HFSPLUS_I(attr_file);

	clump_size = hfsplus_calc_btree_clump_size(sb->s_blocksize,
						    node_size,
						    sbi->sect_count,
						    HFSPLUS_ATTR_CNID);

	mutex_lock(&hip->extents_lock);
	hip->clump_blocks = clump_size >> sbi->alloc_blksz_shift;
	mutex_unlock(&hip->extents_lock);

	if (sbi->free_blocks <= (hip->clump_blocks << 1)) {
		err = -ENOSPC;
		goto end_attr_file_creation;
	}

	while (hip->alloc_blocks < hip->clump_blocks) {
		err = hfsplus_file_extend(attr_file, false);
		if (unlikely(err)) {
			pr_err("failed to extend attributes file\n");
			goto end_attr_file_creation;
		}
		hip->phys_size = attr_file->i_size =
			(loff_t)hip->alloc_blocks << sbi->alloc_blksz_shift;
		hip->fs_blocks = hip->alloc_blocks << sbi->fs_shift;
		inode_set_bytes(attr_file, attr_file->i_size);
	}

	buf = kzalloc(node_size, GFP_NOFS);
	if (!buf) {
		err = -ENOMEM;
		goto end_attr_file_creation;
	}

	hfsplus_init_header_node(attr_file, clump_size, buf, node_size);

	mapping = attr_file->i_mapping;

	index = 0;
	written = 0;
	for (; written < node_size; index++, written += PAGE_SIZE) {
		void *kaddr;

		page = read_mapping_page(mapping, index, NULL);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto failed_header_node_init;
		}

		kaddr = kmap_atomic(page);
		memcpy(kaddr, buf + written,
			min_t(size_t, PAGE_SIZE, node_size - written));
		kunmap_atomic(kaddr);

		set_page_dirty(page);
		put_page(page);
	}

	hfsplus_mark_inode_dirty(attr_file, HFSPLUS_I_ATTR_DIRTY);

	sbi->attr_tree = hfs_btree_open(sb, HFSPLUS_ATTR_CNID);
	if (!sbi->attr_tree)
		pr_err("failed to load attributes file\n");

failed_header_node_init:
	kfree(buf);

end_attr_file_creation:
	iput(attr_file);

	if (!err)
		atomic_set(&sbi->attr_tree_state, HFSPLUS_VALID_ATTR_TREE);
	else if (err == -ENOSPC)
		atomic_set(&sbi->attr_tree_state, HFSPLUS_EMPTY_ATTR_TREE);
	else
		atomic_set(&sbi->attr_tree_state, HFSPLUS_FAILED_ATTR_TREE);

	return err;
}

int __hfsplus_setxattr(struct inode *inode, const char *name,
			const void *value, size_t size, int flags)
{
	int err = 0;
	struct hfs_find_data cat_fd;
	hfsplus_cat_entry entry;
	u16 cat_entry_flags, cat_entry_type;
	u16 folder_finderinfo_len = sizeof(struct DInfo) +
					sizeof(struct DXInfo);
	u16 file_finderinfo_len = sizeof(struct FInfo) +
					sizeof(struct FXInfo);

	if ((!S_ISREG(inode->i_mode) &&
			!S_ISDIR(inode->i_mode)) ||
				HFSPLUS_IS_RSRC(inode))
		return -EOPNOTSUPP;

	if (value == NULL)
		return hfsplus_removexattr(inode, name);

	err = hfs_find_init(HFSPLUS_SB(inode->i_sb)->cat_tree, &cat_fd);
	if (err) {
		pr_err("can't init xattr find struct\n");
		return err;
	}

	err = hfsplus_find_cat(inode->i_sb, inode->i_ino, &cat_fd);
	if (err) {
		pr_err("catalog searching failed\n");
		goto end_setxattr;
	}

	if (!strcmp_xattr_finder_info(name)) {
		if (flags & XATTR_CREATE) {
			pr_err("xattr exists yet\n");
			err = -EOPNOTSUPP;
			goto end_setxattr;
		}
		hfs_bnode_read(cat_fd.bnode, &entry, cat_fd.entryoffset,
					sizeof(hfsplus_cat_entry));
		if (be16_to_cpu(entry.type) == HFSPLUS_FOLDER) {
			if (size == folder_finderinfo_len) {
				memcpy(&entry.folder.info, value,
						folder_finderinfo_len);
				hfs_bnode_write(cat_fd.bnode, &entry,
					cat_fd.entryoffset,
					sizeof(struct hfsplus_cat_folder));
				hfsplus_mark_inode_dirty(inode,
						HFSPLUS_I_CAT_DIRTY);
			} else {
				err = -ERANGE;
				goto end_setxattr;
			}
		} else if (be16_to_cpu(entry.type) == HFSPLUS_FILE) {
			if (size == file_finderinfo_len) {
				memcpy(&entry.file.info, value,
						file_finderinfo_len);
				hfs_bnode_write(cat_fd.bnode, &entry,
					cat_fd.entryoffset,
					sizeof(struct hfsplus_cat_file));
				hfsplus_mark_inode_dirty(inode,
						HFSPLUS_I_CAT_DIRTY);
			} else {
				err = -ERANGE;
				goto end_setxattr;
			}
		} else {
			err = -EOPNOTSUPP;
			goto end_setxattr;
		}
		goto end_setxattr;
	}

	if (!HFSPLUS_SB(inode->i_sb)->attr_tree) {
		err = hfsplus_create_attributes_file(inode->i_sb);
		if (unlikely(err))
			goto end_setxattr;
	}

	if (hfsplus_attr_exists(inode, name)) {
		if (flags & XATTR_CREATE) {
			pr_err("xattr exists yet\n");
			err = -EOPNOTSUPP;
			goto end_setxattr;
		}
		err = hfsplus_delete_attr(inode, name);
		if (err)
			goto end_setxattr;
		err = hfsplus_create_attr(inode, name, value, size);
		if (err)
			goto end_setxattr;
	} else {
		if (flags & XATTR_REPLACE) {
			pr_err("cannot replace xattr\n");
			err = -EOPNOTSUPP;
			goto end_setxattr;
		}
		err = hfsplus_create_attr(inode, name, value, size);
		if (err)
			goto end_setxattr;
	}

	cat_entry_type = hfs_bnode_read_u16(cat_fd.bnode, cat_fd.entryoffset);
	if (cat_entry_type == HFSPLUS_FOLDER) {
		cat_entry_flags = hfs_bnode_read_u16(cat_fd.bnode,
				    cat_fd.entryoffset +
				    offsetof(struct hfsplus_cat_folder, flags));
		cat_entry_flags |= HFSPLUS_XATTR_EXISTS;
		if (!strcmp_xattr_acl(name))
			cat_entry_flags |= HFSPLUS_ACL_EXISTS;
		hfs_bnode_write_u16(cat_fd.bnode, cat_fd.entryoffset +
				offsetof(struct hfsplus_cat_folder, flags),
				cat_entry_flags);
		hfsplus_mark_inode_dirty(inode, HFSPLUS_I_CAT_DIRTY);
	} else if (cat_entry_type == HFSPLUS_FILE) {
		cat_entry_flags = hfs_bnode_read_u16(cat_fd.bnode,
				    cat_fd.entryoffset +
				    offsetof(struct hfsplus_cat_file, flags));
		cat_entry_flags |= HFSPLUS_XATTR_EXISTS;
		if (!strcmp_xattr_acl(name))
			cat_entry_flags |= HFSPLUS_ACL_EXISTS;
		hfs_bnode_write_u16(cat_fd.bnode, cat_fd.entryoffset +
				    offsetof(struct hfsplus_cat_file, flags),
				    cat_entry_flags);
		hfsplus_mark_inode_dirty(inode, HFSPLUS_I_CAT_DIRTY);
	} else {
		pr_err("invalid catalog entry type\n");
		err = -EIO;
		goto end_setxattr;
	}

end_setxattr:
	hfs_find_exit(&cat_fd);
	return err;
}

static int name_len(const char *xattr_name, int xattr_name_len)
{
	int len = xattr_name_len + 1;

	if (!is_known_namespace(xattr_name))
		len += XATTR_MAC_OSX_PREFIX_LEN;

	return len;
}

static int copy_name(char *buffer, const char *xattr_name, int name_len)
{
	int len = name_len;
	int offset = 0;

	if (!is_known_namespace(xattr_name)) {
		memcpy(buffer, XATTR_MAC_OSX_PREFIX, XATTR_MAC_OSX_PREFIX_LEN);
		offset += XATTR_MAC_OSX_PREFIX_LEN;
		len += XATTR_MAC_OSX_PREFIX_LEN;
	}

	strncpy(buffer + offset, xattr_name, name_len);
	memset(buffer + offset + name_len, 0, 1);
	len += 1;

	return len;
}

int hfsplus_setxattr(struct inode *inode, const char *name,
		     const void *value, size_t size, int flags,
		     const char *prefix, size_t prefixlen)
{
	char *xattr_name;
	int res;

	xattr_name = kmalloc(NLS_MAX_CHARSET_SIZE * HFSPLUS_ATTR_MAX_STRLEN + 1,
		GFP_KERNEL);
	if (!xattr_name)
		return -ENOMEM;
	strcpy(xattr_name, prefix);
	strcpy(xattr_name + prefixlen, name);
	res = __hfsplus_setxattr(inode, xattr_name, value, size, flags);
	kfree(xattr_name);
	return res;
}

static ssize_t hfsplus_getxattr_finder_info(struct inode *inode,
						void *value, size_t size)
{
	ssize_t res = 0;
	struct hfs_find_data fd;
	u16 entry_type;
	u16 folder_rec_len = sizeof(struct DInfo) + sizeof(struct DXInfo);
	u16 file_rec_len = sizeof(struct FInfo) + sizeof(struct FXInfo);
	u16 record_len = max(folder_rec_len, file_rec_len);
	u8 folder_finder_info[sizeof(struct DInfo) + sizeof(struct DXInfo)];
	u8 file_finder_info[sizeof(struct FInfo) + sizeof(struct FXInfo)];

	if (size >= record_len) {
		res = hfs_find_init(HFSPLUS_SB(inode->i_sb)->cat_tree, &fd);
		if (res) {
			pr_err("can't init xattr find struct\n");
			return res;
		}
		res = hfsplus_find_cat(inode->i_sb, inode->i_ino, &fd);
		if (res)
			goto end_getxattr_finder_info;
		entry_type = hfs_bnode_read_u16(fd.bnode, fd.entryoffset);

		if (entry_type == HFSPLUS_FOLDER) {
			hfs_bnode_read(fd.bnode, folder_finder_info,
				fd.entryoffset +
				offsetof(struct hfsplus_cat_folder, user_info),
				folder_rec_len);
			memcpy(value, folder_finder_info, folder_rec_len);
			res = folder_rec_len;
		} else if (entry_type == HFSPLUS_FILE) {
			hfs_bnode_read(fd.bnode, file_finder_info,
				fd.entryoffset +
				offsetof(struct hfsplus_cat_file, user_info),
				file_rec_len);
			memcpy(value, file_finder_info, file_rec_len);
			res = file_rec_len;
		} else {
			res = -EOPNOTSUPP;
			goto end_getxattr_finder_info;
		}
	} else
		res = size ? -ERANGE : record_len;

end_getxattr_finder_info:
	if (size >= record_len)
		hfs_find_exit(&fd);
	return res;
}

ssize_t __hfsplus_getxattr(struct inode *inode, const char *name,
			 void *value, size_t size)
{
	struct hfs_find_data fd;
	hfsplus_attr_entry *entry;
	__be32 xattr_record_type;
	u32 record_type;
	u16 record_length = 0;
	ssize_t res = 0;

	if ((!S_ISREG(inode->i_mode) &&
			!S_ISDIR(inode->i_mode)) ||
				HFSPLUS_IS_RSRC(inode))
		return -EOPNOTSUPP;

	if (!strcmp_xattr_finder_info(name))
		return hfsplus_getxattr_finder_info(inode, value, size);

	if (!HFSPLUS_SB(inode->i_sb)->attr_tree)
		return -EOPNOTSUPP;

	entry = hfsplus_alloc_attr_entry();
	if (!entry) {
		pr_err("can't allocate xattr entry\n");
		return -ENOMEM;
	}

	res = hfs_find_init(HFSPLUS_SB(inode->i_sb)->attr_tree, &fd);
	if (res) {
		pr_err("can't init xattr find struct\n");
		goto failed_getxattr_init;
	}

	res = hfsplus_find_attr(inode->i_sb, inode->i_ino, name, &fd);
	if (res) {
		if (res == -ENOENT)
			res = -ENODATA;
		else
			pr_err("xattr searching failed\n");
		goto out;
	}

	hfs_bnode_read(fd.bnode, &xattr_record_type,
			fd.entryoffset, sizeof(xattr_record_type));
	record_type = be32_to_cpu(xattr_record_type);
	if (record_type == HFSPLUS_ATTR_INLINE_DATA) {
		record_length = hfs_bnode_read_u16(fd.bnode,
				fd.entryoffset +
				offsetof(struct hfsplus_attr_inline_data,
				length));
		if (record_length > HFSPLUS_MAX_INLINE_DATA_SIZE) {
			pr_err("invalid xattr record size\n");
			res = -EIO;
			goto out;
		}
	} else if (record_type == HFSPLUS_ATTR_FORK_DATA ||
			record_type == HFSPLUS_ATTR_EXTENTS) {
		pr_err("only inline data xattr are supported\n");
		res = -EOPNOTSUPP;
		goto out;
	} else {
		pr_err("invalid xattr record\n");
		res = -EIO;
		goto out;
	}

	if (size) {
		hfs_bnode_read(fd.bnode, entry, fd.entryoffset,
				offsetof(struct hfsplus_attr_inline_data,
					raw_bytes) + record_length);
	}

	if (size >= record_length) {
		memcpy(value, entry->inline_data.raw_bytes, record_length);
		res = record_length;
	} else
		res = size ? -ERANGE : record_length;

out:
	hfs_find_exit(&fd);

failed_getxattr_init:
	hfsplus_destroy_attr_entry(entry);
	return res;
}

ssize_t hfsplus_getxattr(struct inode *inode, const char *name,
			 void *value, size_t size,
			 const char *prefix, size_t prefixlen)
{
	int res;
	char *xattr_name;

	xattr_name = kmalloc(NLS_MAX_CHARSET_SIZE * HFSPLUS_ATTR_MAX_STRLEN + 1,
			     GFP_KERNEL);
	if (!xattr_name)
		return -ENOMEM;

	strcpy(xattr_name, prefix);
	strcpy(xattr_name + prefixlen, name);

	res = __hfsplus_getxattr(inode, xattr_name, value, size);
	kfree(xattr_name);
	return res;

}

static inline int can_list(const char *xattr_name)
{
	if (!xattr_name)
		return 0;

	return strncmp(xattr_name, XATTR_TRUSTED_PREFIX,
			XATTR_TRUSTED_PREFIX_LEN) ||
				capable(CAP_SYS_ADMIN);
}

static ssize_t hfsplus_listxattr_finder_info(struct dentry *dentry,
						char *buffer, size_t size)
{
	ssize_t res = 0;
	struct inode *inode = d_inode(dentry);
	struct hfs_find_data fd;
	u16 entry_type;
	u8 folder_finder_info[sizeof(struct DInfo) + sizeof(struct DXInfo)];
	u8 file_finder_info[sizeof(struct FInfo) + sizeof(struct FXInfo)];
	unsigned long len, found_bit;
	int xattr_name_len, symbols_count;

	res = hfs_find_init(HFSPLUS_SB(inode->i_sb)->cat_tree, &fd);
	if (res) {
		pr_err("can't init xattr find struct\n");
		return res;
	}

	res = hfsplus_find_cat(inode->i_sb, inode->i_ino, &fd);
	if (res)
		goto end_listxattr_finder_info;

	entry_type = hfs_bnode_read_u16(fd.bnode, fd.entryoffset);
	if (entry_type == HFSPLUS_FOLDER) {
		len = sizeof(struct DInfo) + sizeof(struct DXInfo);
		hfs_bnode_read(fd.bnode, folder_finder_info,
				fd.entryoffset +
				offsetof(struct hfsplus_cat_folder, user_info),
				len);
		found_bit = find_first_bit((void *)folder_finder_info, len*8);
	} else if (entry_type == HFSPLUS_FILE) {
		len = sizeof(struct FInfo) + sizeof(struct FXInfo);
		hfs_bnode_read(fd.bnode, file_finder_info,
				fd.entryoffset +
				offsetof(struct hfsplus_cat_file, user_info),
				len);
		found_bit = find_first_bit((void *)file_finder_info, len*8);
	} else {
		res = -EOPNOTSUPP;
		goto end_listxattr_finder_info;
	}

	if (found_bit >= (len*8))
		res = 0;
	else {
		symbols_count = sizeof(HFSPLUS_XATTR_FINDER_INFO_NAME) - 1;
		xattr_name_len =
			name_len(HFSPLUS_XATTR_FINDER_INFO_NAME, symbols_count);
		if (!buffer || !size) {
			if (can_list(HFSPLUS_XATTR_FINDER_INFO_NAME))
				res = xattr_name_len;
		} else if (can_list(HFSPLUS_XATTR_FINDER_INFO_NAME)) {
			if (size < xattr_name_len)
				res = -ERANGE;
			else {
				res = copy_name(buffer,
						HFSPLUS_XATTR_FINDER_INFO_NAME,
						symbols_count);
			}
		}
	}

end_listxattr_finder_info:
	hfs_find_exit(&fd);

	return res;
}

ssize_t hfsplus_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	ssize_t err;
	ssize_t res = 0;
	struct inode *inode = d_inode(dentry);
	struct hfs_find_data fd;
	u16 key_len = 0;
	struct hfsplus_attr_key attr_key;
	char *strbuf;
	int xattr_name_len;

	if ((!S_ISREG(inode->i_mode) &&
			!S_ISDIR(inode->i_mode)) ||
				HFSPLUS_IS_RSRC(inode))
		return -EOPNOTSUPP;

	res = hfsplus_listxattr_finder_info(dentry, buffer, size);
	if (res < 0)
		return res;
	else if (!HFSPLUS_SB(inode->i_sb)->attr_tree)
		return (res == 0) ? -EOPNOTSUPP : res;

	err = hfs_find_init(HFSPLUS_SB(inode->i_sb)->attr_tree, &fd);
	if (err) {
		pr_err("can't init xattr find struct\n");
		return err;
	}

	strbuf = kmalloc(NLS_MAX_CHARSET_SIZE * HFSPLUS_ATTR_MAX_STRLEN +
			XATTR_MAC_OSX_PREFIX_LEN + 1, GFP_KERNEL);
	if (!strbuf) {
		res = -ENOMEM;
		goto out;
	}

	err = hfsplus_find_attr(inode->i_sb, inode->i_ino, NULL, &fd);
	if (err) {
		if (err == -ENOENT) {
			if (res == 0)
				res = -ENODATA;
			goto end_listxattr;
		} else {
			res = err;
			goto end_listxattr;
		}
	}

	for (;;) {
		key_len = hfs_bnode_read_u16(fd.bnode, fd.keyoffset);
		if (key_len == 0 || key_len > fd.tree->max_key_len) {
			pr_err("invalid xattr key length: %d\n", key_len);
			res = -EIO;
			goto end_listxattr;
		}

		hfs_bnode_read(fd.bnode, &attr_key,
				fd.keyoffset, key_len + sizeof(key_len));

		if (be32_to_cpu(attr_key.cnid) != inode->i_ino)
			goto end_listxattr;

		xattr_name_len = NLS_MAX_CHARSET_SIZE * HFSPLUS_ATTR_MAX_STRLEN;
		if (hfsplus_uni2asc(inode->i_sb,
			(const struct hfsplus_unistr *)&fd.key->attr.key_name,
					strbuf, &xattr_name_len)) {
			pr_err("unicode conversion failed\n");
			res = -EIO;
			goto end_listxattr;
		}

		if (!buffer || !size) {
			if (can_list(strbuf))
				res += name_len(strbuf, xattr_name_len);
		} else if (can_list(strbuf)) {
			if (size < (res + name_len(strbuf, xattr_name_len))) {
				res = -ERANGE;
				goto end_listxattr;
			} else
				res += copy_name(buffer + res,
						strbuf, xattr_name_len);
		}

		if (hfs_brec_goto(&fd, 1))
			goto end_listxattr;
	}

end_listxattr:
	kfree(strbuf);
out:
	hfs_find_exit(&fd);
	return res;
}

static int hfsplus_removexattr(struct inode *inode, const char *name)
{
	int err = 0;
	struct hfs_find_data cat_fd;
	u16 flags;
	u16 cat_entry_type;
	int is_xattr_acl_deleted = 0;
	int is_all_xattrs_deleted = 0;

	if (!HFSPLUS_SB(inode->i_sb)->attr_tree)
		return -EOPNOTSUPP;

	if (!strcmp_xattr_finder_info(name))
		return -EOPNOTSUPP;

	err = hfs_find_init(HFSPLUS_SB(inode->i_sb)->cat_tree, &cat_fd);
	if (err) {
		pr_err("can't init xattr find struct\n");
		return err;
	}

	err = hfsplus_find_cat(inode->i_sb, inode->i_ino, &cat_fd);
	if (err) {
		pr_err("catalog searching failed\n");
		goto end_removexattr;
	}

	err = hfsplus_delete_attr(inode, name);
	if (err)
		goto end_removexattr;

	is_xattr_acl_deleted = !strcmp_xattr_acl(name);
	is_all_xattrs_deleted = !hfsplus_attr_exists(inode, NULL);

	if (!is_xattr_acl_deleted && !is_all_xattrs_deleted)
		goto end_removexattr;

	cat_entry_type = hfs_bnode_read_u16(cat_fd.bnode, cat_fd.entryoffset);

	if (cat_entry_type == HFSPLUS_FOLDER) {
		flags = hfs_bnode_read_u16(cat_fd.bnode, cat_fd.entryoffset +
				offsetof(struct hfsplus_cat_folder, flags));
		if (is_xattr_acl_deleted)
			flags &= ~HFSPLUS_ACL_EXISTS;
		if (is_all_xattrs_deleted)
			flags &= ~HFSPLUS_XATTR_EXISTS;
		hfs_bnode_write_u16(cat_fd.bnode, cat_fd.entryoffset +
				offsetof(struct hfsplus_cat_folder, flags),
				flags);
		hfsplus_mark_inode_dirty(inode, HFSPLUS_I_CAT_DIRTY);
	} else if (cat_entry_type == HFSPLUS_FILE) {
		flags = hfs_bnode_read_u16(cat_fd.bnode, cat_fd.entryoffset +
				offsetof(struct hfsplus_cat_file, flags));
		if (is_xattr_acl_deleted)
			flags &= ~HFSPLUS_ACL_EXISTS;
		if (is_all_xattrs_deleted)
			flags &= ~HFSPLUS_XATTR_EXISTS;
		hfs_bnode_write_u16(cat_fd.bnode, cat_fd.entryoffset +
				offsetof(struct hfsplus_cat_file, flags),
				flags);
		hfsplus_mark_inode_dirty(inode, HFSPLUS_I_CAT_DIRTY);
	} else {
		pr_err("invalid catalog entry type\n");
		err = -EIO;
		goto end_removexattr;
	}

end_removexattr:
	hfs_find_exit(&cat_fd);
	return err;
}

static int hfsplus_osx_getxattr(const struct xattr_handler *handler,
				struct dentry *unused, struct inode *inode,
				const char *name, void *buffer, size_t size)
{
	/*
	 * Don't allow retrieving properly prefixed attributes
	 * by prepending them with "osx."
	 */
	if (is_known_namespace(name))
		return -EOPNOTSUPP;

	/*
	 * osx is the namespace we use to indicate an unprefixed
	 * attribute on the filesystem (like the ones that OS X
	 * creates), so we pass the name through unmodified (after
	 * ensuring it doesn't conflict with another namespace).
	 */
	return __hfsplus_getxattr(inode, name, buffer, size);
}

static int hfsplus_osx_setxattr(const struct xattr_handler *handler,
				struct mnt_idmap *idmap,
				struct dentry *unused, struct inode *inode,
				const char *name, const void *buffer,
				size_t size, int flags)
{
	/*
	 * Don't allow setting properly prefixed attributes
	 * by prepending them with "osx."
	 */
	if (is_known_namespace(name))
		return -EOPNOTSUPP;

	/*
	 * osx is the namespace we use to indicate an unprefixed
	 * attribute on the filesystem (like the ones that OS X
	 * creates), so we pass the name through unmodified (after
	 * ensuring it doesn't conflict with another namespace).
	 */
	return __hfsplus_setxattr(inode, name, buffer, size, flags);
}

const struct xattr_handler hfsplus_xattr_osx_handler = {
	.prefix	= XATTR_MAC_OSX_PREFIX,
	.get	= hfsplus_osx_getxattr,
	.set	= hfsplus_osx_setxattr,
};
