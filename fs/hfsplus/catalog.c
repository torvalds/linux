/*
 *  linux/fs/hfsplus/catalog.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Handling of catalog records
 */

#include <linux/sched.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

int hfsplus_cat_cmp_key(hfsplus_btree_key *k1, hfsplus_btree_key *k2)
{
	__be32 k1p, k2p;

	k1p = k1->cat.parent;
	k2p = k2->cat.parent;
	if (k1p != k2p)
		return be32_to_cpu(k1p) < be32_to_cpu(k2p) ? -1 : 1;

	return hfsplus_unistrcmp(&k1->cat.name, &k2->cat.name);
}

void hfsplus_cat_build_key(struct super_block *sb, hfsplus_btree_key *key,
			   u32 parent, struct qstr *str)
{
	int len;

	key->cat.parent = cpu_to_be32(parent);
	if (str) {
		hfsplus_asc2uni(sb, &key->cat.name, str->name, str->len);
		len = be16_to_cpu(key->cat.name.length);
	} else {
		key->cat.name.length = 0;
		len = 0;
	}
	key->key_len = cpu_to_be16(6 + 2 * len);
}

static void hfsplus_cat_build_key_uni(hfsplus_btree_key *key, u32 parent,
				      struct hfsplus_unistr *name)
{
	int ustrlen;

	ustrlen = be16_to_cpu(name->length);
	key->cat.parent = cpu_to_be32(parent);
	key->cat.name.length = cpu_to_be16(ustrlen);
	ustrlen *= 2;
	memcpy(key->cat.name.unicode, name->unicode, ustrlen);
	key->key_len = cpu_to_be16(6 + ustrlen);
}

static void hfsplus_set_perms(struct inode *inode, struct hfsplus_perm *perms)
{
	if (inode->i_flags & S_IMMUTABLE)
		perms->rootflags |= HFSPLUS_FLG_IMMUTABLE;
	else
		perms->rootflags &= ~HFSPLUS_FLG_IMMUTABLE;
	if (inode->i_flags & S_APPEND)
		perms->rootflags |= HFSPLUS_FLG_APPEND;
	else
		perms->rootflags &= ~HFSPLUS_FLG_APPEND;
	HFSPLUS_I(inode).rootflags = perms->rootflags;
	HFSPLUS_I(inode).userflags = perms->userflags;
	perms->mode = cpu_to_be16(inode->i_mode);
	perms->owner = cpu_to_be32(inode->i_uid);
	perms->group = cpu_to_be32(inode->i_gid);
}

static int hfsplus_cat_build_record(hfsplus_cat_entry *entry, u32 cnid, struct inode *inode)
{
	if (S_ISDIR(inode->i_mode)) {
		struct hfsplus_cat_folder *folder;

		folder = &entry->folder;
		memset(folder, 0, sizeof(*folder));
		folder->type = cpu_to_be16(HFSPLUS_FOLDER);
		folder->id = cpu_to_be32(inode->i_ino);
		folder->create_date = folder->content_mod_date =
			folder->attribute_mod_date = folder->access_date = hfsp_now2mt();
		hfsplus_set_perms(inode, &folder->permissions);
		if (inode == HFSPLUS_SB(inode->i_sb).hidden_dir)
			/* invisible and namelocked */
			folder->user_info.frFlags = cpu_to_be16(0x5000);
		return sizeof(*folder);
	} else {
		struct hfsplus_cat_file *file;

		file = &entry->file;
		memset(file, 0, sizeof(*file));
		file->type = cpu_to_be16(HFSPLUS_FILE);
		file->flags = cpu_to_be16(HFSPLUS_FILE_THREAD_EXISTS);
		file->id = cpu_to_be32(cnid);
		file->create_date = file->content_mod_date =
			file->attribute_mod_date = file->access_date = hfsp_now2mt();
		if (cnid == inode->i_ino) {
			hfsplus_set_perms(inode, &file->permissions);
			file->user_info.fdType = cpu_to_be32(HFSPLUS_SB(inode->i_sb).type);
			file->user_info.fdCreator = cpu_to_be32(HFSPLUS_SB(inode->i_sb).creator);
			if ((file->permissions.rootflags | file->permissions.userflags) & HFSPLUS_FLG_IMMUTABLE)
				file->flags |= cpu_to_be16(HFSPLUS_FILE_LOCKED);
		} else {
			file->user_info.fdType = cpu_to_be32(HFSP_HARDLINK_TYPE);
			file->user_info.fdCreator = cpu_to_be32(HFSP_HFSPLUS_CREATOR);
			file->user_info.fdFlags = cpu_to_be16(0x100);
			file->permissions.dev = cpu_to_be32(HFSPLUS_I(inode).dev);
		}
		return sizeof(*file);
	}
}

static int hfsplus_fill_cat_thread(struct super_block *sb,
				   hfsplus_cat_entry *entry, int type,
				   u32 parentid, struct qstr *str)
{
	entry->type = cpu_to_be16(type);
	entry->thread.reserved = 0;
	entry->thread.parentID = cpu_to_be32(parentid);
	hfsplus_asc2uni(sb, &entry->thread.nodeName, str->name, str->len);
	return 10 + be16_to_cpu(entry->thread.nodeName.length) * 2;
}

/* Try to get a catalog entry for given catalog id */
int hfsplus_find_cat(struct super_block *sb, u32 cnid,
		     struct hfs_find_data *fd)
{
	hfsplus_cat_entry tmp;
	int err;
	u16 type;

	hfsplus_cat_build_key(sb, fd->search_key, cnid, NULL);
	err = hfs_brec_read(fd, &tmp, sizeof(hfsplus_cat_entry));
	if (err)
		return err;

	type = be16_to_cpu(tmp.type);
	if (type != HFSPLUS_FOLDER_THREAD && type != HFSPLUS_FILE_THREAD) {
		printk("HFS+-fs: Found bad thread record in catalog\n");
		return -EIO;
	}

	hfsplus_cat_build_key_uni(fd->search_key, be32_to_cpu(tmp.thread.parentID),
				 &tmp.thread.nodeName);
	return hfs_brec_find(fd);
}

int hfsplus_create_cat(u32 cnid, struct inode *dir, struct qstr *str, struct inode *inode)
{
	struct hfs_find_data fd;
	struct super_block *sb;
	hfsplus_cat_entry entry;
	int entry_size;
	int err;

	dprint(DBG_CAT_MOD, "create_cat: %s,%u(%d)\n", str->name, cnid, inode->i_nlink);
	sb = dir->i_sb;
	hfs_find_init(HFSPLUS_SB(sb).cat_tree, &fd);

	hfsplus_cat_build_key(sb, fd.search_key, cnid, NULL);
	entry_size = hfsplus_fill_cat_thread(sb, &entry, S_ISDIR(inode->i_mode) ?
			HFSPLUS_FOLDER_THREAD : HFSPLUS_FILE_THREAD,
			dir->i_ino, str);
	err = hfs_brec_find(&fd);
	if (err != -ENOENT) {
		if (!err)
			err = -EEXIST;
		goto err2;
	}
	err = hfs_brec_insert(&fd, &entry, entry_size);
	if (err)
		goto err2;

	hfsplus_cat_build_key(sb, fd.search_key, dir->i_ino, str);
	entry_size = hfsplus_cat_build_record(&entry, cnid, inode);
	err = hfs_brec_find(&fd);
	if (err != -ENOENT) {
		/* panic? */
		if (!err)
			err = -EEXIST;
		goto err1;
	}
	err = hfs_brec_insert(&fd, &entry, entry_size);
	if (err)
		goto err1;

	dir->i_size++;
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
	hfs_find_exit(&fd);
	return 0;

err1:
	hfsplus_cat_build_key(sb, fd.search_key, cnid, NULL);
	if (!hfs_brec_find(&fd))
		hfs_brec_remove(&fd);
err2:
	hfs_find_exit(&fd);
	return err;
}

int hfsplus_delete_cat(u32 cnid, struct inode *dir, struct qstr *str)
{
	struct super_block *sb;
	struct hfs_find_data fd;
	struct hfsplus_fork_raw fork;
	struct list_head *pos;
	int err, off;
	u16 type;

	dprint(DBG_CAT_MOD, "delete_cat: %s,%u\n", str ? str->name : NULL, cnid);
	sb = dir->i_sb;
	hfs_find_init(HFSPLUS_SB(sb).cat_tree, &fd);

	if (!str) {
		int len;

		hfsplus_cat_build_key(sb, fd.search_key, cnid, NULL);
		err = hfs_brec_find(&fd);
		if (err)
			goto out;

		off = fd.entryoffset + offsetof(struct hfsplus_cat_thread, nodeName);
		fd.search_key->cat.parent = cpu_to_be32(dir->i_ino);
		hfs_bnode_read(fd.bnode, &fd.search_key->cat.name.length, off, 2);
		len = be16_to_cpu(fd.search_key->cat.name.length) * 2;
		hfs_bnode_read(fd.bnode, &fd.search_key->cat.name.unicode, off + 2, len);
		fd.search_key->key_len = cpu_to_be16(6 + len);
	} else
		hfsplus_cat_build_key(sb, fd.search_key, dir->i_ino, str);

	err = hfs_brec_find(&fd);
	if (err)
		goto out;

	type = hfs_bnode_read_u16(fd.bnode, fd.entryoffset);
	if (type == HFSPLUS_FILE) {
#if 0
		off = fd.entryoffset + offsetof(hfsplus_cat_file, data_fork);
		hfs_bnode_read(fd.bnode, &fork, off, sizeof(fork));
		hfsplus_free_fork(sb, cnid, &fork, HFSPLUS_TYPE_DATA);
#endif

		off = fd.entryoffset + offsetof(struct hfsplus_cat_file, rsrc_fork);
		hfs_bnode_read(fd.bnode, &fork, off, sizeof(fork));
		hfsplus_free_fork(sb, cnid, &fork, HFSPLUS_TYPE_RSRC);
	}

	list_for_each(pos, &HFSPLUS_I(dir).open_dir_list) {
		struct hfsplus_readdir_data *rd =
			list_entry(pos, struct hfsplus_readdir_data, list);
		if (fd.tree->keycmp(fd.search_key, (void *)&rd->key) < 0)
			rd->file->f_pos--;
	}

	err = hfs_brec_remove(&fd);
	if (err)
		goto out;

	hfsplus_cat_build_key(sb, fd.search_key, cnid, NULL);
	err = hfs_brec_find(&fd);
	if (err)
		goto out;

	err = hfs_brec_remove(&fd);
	if (err)
		goto out;

	dir->i_size--;
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dir);
out:
	hfs_find_exit(&fd);

	return err;
}

int hfsplus_rename_cat(u32 cnid,
		       struct inode *src_dir, struct qstr *src_name,
		       struct inode *dst_dir, struct qstr *dst_name)
{
	struct super_block *sb;
	struct hfs_find_data src_fd, dst_fd;
	hfsplus_cat_entry entry;
	int entry_size, type;
	int err = 0;

	dprint(DBG_CAT_MOD, "rename_cat: %u - %lu,%s - %lu,%s\n", cnid, src_dir->i_ino, src_name->name,
		dst_dir->i_ino, dst_name->name);
	sb = src_dir->i_sb;
	hfs_find_init(HFSPLUS_SB(sb).cat_tree, &src_fd);
	dst_fd = src_fd;

	/* find the old dir entry and read the data */
	hfsplus_cat_build_key(sb, src_fd.search_key, src_dir->i_ino, src_name);
	err = hfs_brec_find(&src_fd);
	if (err)
		goto out;

	hfs_bnode_read(src_fd.bnode, &entry, src_fd.entryoffset,
				src_fd.entrylength);

	/* create new dir entry with the data from the old entry */
	hfsplus_cat_build_key(sb, dst_fd.search_key, dst_dir->i_ino, dst_name);
	err = hfs_brec_find(&dst_fd);
	if (err != -ENOENT) {
		if (!err)
			err = -EEXIST;
		goto out;
	}

	err = hfs_brec_insert(&dst_fd, &entry, src_fd.entrylength);
	if (err)
		goto out;
	dst_dir->i_size++;
	dst_dir->i_mtime = dst_dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(dst_dir);

	/* finally remove the old entry */
	hfsplus_cat_build_key(sb, src_fd.search_key, src_dir->i_ino, src_name);
	err = hfs_brec_find(&src_fd);
	if (err)
		goto out;
	err = hfs_brec_remove(&src_fd);
	if (err)
		goto out;
	src_dir->i_size--;
	src_dir->i_mtime = src_dir->i_ctime = CURRENT_TIME_SEC;
	mark_inode_dirty(src_dir);

	/* remove old thread entry */
	hfsplus_cat_build_key(sb, src_fd.search_key, cnid, NULL);
	err = hfs_brec_find(&src_fd);
	if (err)
		goto out;
	type = hfs_bnode_read_u16(src_fd.bnode, src_fd.entryoffset);
	err = hfs_brec_remove(&src_fd);
	if (err)
		goto out;

	/* create new thread entry */
	hfsplus_cat_build_key(sb, dst_fd.search_key, cnid, NULL);
	entry_size = hfsplus_fill_cat_thread(sb, &entry, type, dst_dir->i_ino, dst_name);
	err = hfs_brec_find(&dst_fd);
	if (err != -ENOENT) {
		if (!err)
			err = -EEXIST;
		goto out;
	}
	err = hfs_brec_insert(&dst_fd, &entry, entry_size);
out:
	hfs_bnode_put(dst_fd.bnode);
	hfs_find_exit(&src_fd);
	return err;
}
