// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/iversion.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/nls.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

static inline unsigned long exfat_d_version(struct dentry *dentry)
{
	return (unsigned long) dentry->d_fsdata;
}

static inline void exfat_d_version_set(struct dentry *dentry,
		unsigned long version)
{
	dentry->d_fsdata = (void *) version;
}

/*
 * If new entry was created in the parent, it could create the 8.3 alias (the
 * shortname of logname).  So, the parent may have the negative-dentry which
 * matches the created 8.3 alias.
 *
 * If it happened, the negative dentry isn't actually negative anymore.  So,
 * drop it.
 */
static int exfat_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	int ret;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	/*
	 * This is not negative dentry. Always valid.
	 *
	 * Note, rename() to existing directory entry will have ->d_inode, and
	 * will use existing name which isn't specified name by user.
	 *
	 * We may be able to drop this positive dentry here. But dropping
	 * positive dentry isn't good idea. So it's unsupported like
	 * rename("filename", "FILENAME") for now.
	 */
	if (d_really_is_positive(dentry))
		return 1;

	/*
	 * Drop the negative dentry, in order to make sure to use the case
	 * sensitive name which is specified by user if this is for creation.
	 */
	if (flags & (LOOKUP_CREATE | LOOKUP_RENAME_TARGET))
		return 0;

	spin_lock(&dentry->d_lock);
	ret = inode_eq_iversion(d_inode(dentry->d_parent),
			exfat_d_version(dentry));
	spin_unlock(&dentry->d_lock);
	return ret;
}

/* returns the length of a struct qstr, ignoring trailing dots */
static unsigned int exfat_striptail_len(unsigned int len, const char *name)
{
	while (len && name[len - 1] == '.')
		len--;
	return len;
}

/*
 * Compute the hash for the exfat name corresponding to the dentry.  If the name
 * is invalid, we leave the hash code unchanged so that the existing dentry can
 * be used. The exfat fs routines will return ENOENT or EINVAL as appropriate.
 */
static int exfat_d_hash(const struct dentry *dentry, struct qstr *qstr)
{
	struct super_block *sb = dentry->d_sb;
	struct nls_table *t = EXFAT_SB(sb)->nls_io;
	const unsigned char *name = qstr->name;
	unsigned int len = exfat_striptail_len(qstr->len, qstr->name);
	unsigned long hash = init_name_hash(dentry);
	int i, charlen;
	wchar_t c;

	for (i = 0; i < len; i += charlen) {
		charlen = t->char2uni(&name[i], len - i, &c);
		if (charlen < 0)
			return charlen;
		hash = partial_name_hash(exfat_toupper(sb, c), hash);
	}

	qstr->hash = end_name_hash(hash);
	return 0;
}

static int exfat_d_cmp(const struct dentry *dentry, unsigned int len,
		const char *str, const struct qstr *name)
{
	struct super_block *sb = dentry->d_sb;
	struct nls_table *t = EXFAT_SB(sb)->nls_io;
	unsigned int alen = exfat_striptail_len(name->len, name->name);
	unsigned int blen = exfat_striptail_len(len, str);
	wchar_t c1, c2;
	int charlen, i;

	if (alen != blen)
		return 1;

	for (i = 0; i < len; i += charlen) {
		charlen = t->char2uni(&name->name[i], alen - i, &c1);
		if (charlen < 0)
			return 1;
		if (charlen != t->char2uni(&str[i], blen - i, &c2))
			return 1;

		if (exfat_toupper(sb, c1) != exfat_toupper(sb, c2))
			return 1;
	}

	return 0;
}

const struct dentry_operations exfat_dentry_ops = {
	.d_revalidate	= exfat_d_revalidate,
	.d_hash		= exfat_d_hash,
	.d_compare	= exfat_d_cmp,
};

static int exfat_utf8_d_hash(const struct dentry *dentry, struct qstr *qstr)
{
	struct super_block *sb = dentry->d_sb;
	const unsigned char *name = qstr->name;
	unsigned int len = exfat_striptail_len(qstr->len, qstr->name);
	unsigned long hash = init_name_hash(dentry);
	int i, charlen;
	unicode_t u;

	for (i = 0; i < len; i += charlen) {
		charlen = utf8_to_utf32(&name[i], len - i, &u);
		if (charlen < 0)
			return charlen;

		/*
		 * exfat_toupper() works only for code points up to the U+FFFF.
		 */
		hash = partial_name_hash(u <= 0xFFFF ? exfat_toupper(sb, u) : u,
					 hash);
	}

	qstr->hash = end_name_hash(hash);
	return 0;
}

static int exfat_utf8_d_cmp(const struct dentry *dentry, unsigned int len,
		const char *str, const struct qstr *name)
{
	struct super_block *sb = dentry->d_sb;
	unsigned int alen = exfat_striptail_len(name->len, name->name);
	unsigned int blen = exfat_striptail_len(len, str);
	unicode_t u_a, u_b;
	int charlen, i;

	if (alen != blen)
		return 1;

	for (i = 0; i < alen; i += charlen) {
		charlen = utf8_to_utf32(&name->name[i], alen - i, &u_a);
		if (charlen < 0)
			return 1;
		if (charlen != utf8_to_utf32(&str[i], blen - i, &u_b))
			return 1;

		if (u_a <= 0xFFFF && u_b <= 0xFFFF) {
			if (exfat_toupper(sb, u_a) != exfat_toupper(sb, u_b))
				return 1;
		} else {
			if (u_a != u_b)
				return 1;
		}
	}

	return 0;
}

const struct dentry_operations exfat_utf8_dentry_ops = {
	.d_revalidate	= exfat_d_revalidate,
	.d_hash		= exfat_utf8_d_hash,
	.d_compare	= exfat_utf8_d_cmp,
};

/* used only in search empty_slot() */
#define CNT_UNUSED_NOHIT        (-1)
#define CNT_UNUSED_HIT          (-2)
/* search EMPTY CONTINUOUS "num_entries" entries */
static int exfat_search_empty_slot(struct super_block *sb,
		struct exfat_hint_femp *hint_femp, struct exfat_chain *p_dir,
		int num_entries)
{
	int i, dentry, num_empty = 0;
	int dentries_per_clu;
	unsigned int type;
	struct exfat_chain clu;
	struct exfat_dentry *ep;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct buffer_head *bh;

	dentries_per_clu = sbi->dentries_per_clu;

	if (hint_femp->eidx != EXFAT_HINT_NONE) {
		dentry = hint_femp->eidx;
		if (num_entries <= hint_femp->count) {
			hint_femp->eidx = EXFAT_HINT_NONE;
			return dentry;
		}

		exfat_chain_dup(&clu, &hint_femp->cur);
	} else {
		exfat_chain_dup(&clu, p_dir);
		dentry = 0;
	}

	while (clu.dir != EXFAT_EOF_CLUSTER) {
		i = dentry & (dentries_per_clu - 1);

		for (; i < dentries_per_clu; i++, dentry++) {
			ep = exfat_get_dentry(sb, &clu, i, &bh, NULL);
			if (!ep)
				return -EIO;
			type = exfat_get_entry_type(ep);
			brelse(bh);

			if (type == TYPE_UNUSED || type == TYPE_DELETED) {
				num_empty++;
				if (hint_femp->eidx == EXFAT_HINT_NONE) {
					hint_femp->eidx = dentry;
					hint_femp->count = CNT_UNUSED_NOHIT;
					exfat_chain_set(&hint_femp->cur,
						clu.dir, clu.size, clu.flags);
				}

				if (type == TYPE_UNUSED &&
				    hint_femp->count != CNT_UNUSED_HIT)
					hint_femp->count = CNT_UNUSED_HIT;
			} else {
				if (hint_femp->eidx != EXFAT_HINT_NONE &&
				    hint_femp->count == CNT_UNUSED_HIT) {
					/* unused empty group means
					 * an empty group which includes
					 * unused dentry
					 */
					exfat_fs_error(sb,
						"found bogus dentry(%d) beyond unused empty group(%d) (start_clu : %u, cur_clu : %u)",
						dentry, hint_femp->eidx,
						p_dir->dir, clu.dir);
					return -EIO;
				}

				num_empty = 0;
				hint_femp->eidx = EXFAT_HINT_NONE;
			}

			if (num_empty >= num_entries) {
				/* found and invalidate hint_femp */
				hint_femp->eidx = EXFAT_HINT_NONE;
				return (dentry - (num_entries - 1));
			}
		}

		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			if (--clu.size > 0)
				clu.dir++;
			else
				clu.dir = EXFAT_EOF_CLUSTER;
		} else {
			if (exfat_get_next_cluster(sb, &clu.dir))
				return -EIO;
		}
	}

	return -ENOSPC;
}

static int exfat_check_max_dentries(struct inode *inode)
{
	if (EXFAT_B_TO_DEN(i_size_read(inode)) >= MAX_EXFAT_DENTRIES) {
		/*
		 * exFAT spec allows a dir to grow up to 8388608(256MB)
		 * dentries
		 */
		return -ENOSPC;
	}
	return 0;
}

/* find empty directory entry.
 * if there isn't any empty slot, expand cluster chain.
 */
static int exfat_find_empty_entry(struct inode *inode,
		struct exfat_chain *p_dir, int num_entries)
{
	int dentry;
	unsigned int ret, last_clu;
	sector_t sector;
	loff_t size = 0;
	struct exfat_chain clu;
	struct exfat_dentry *ep = NULL;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct exfat_hint_femp hint_femp;

	hint_femp.eidx = EXFAT_HINT_NONE;

	if (ei->hint_femp.eidx != EXFAT_HINT_NONE) {
		hint_femp = ei->hint_femp;
		ei->hint_femp.eidx = EXFAT_HINT_NONE;
	}

	while ((dentry = exfat_search_empty_slot(sb, &hint_femp, p_dir,
					num_entries)) < 0) {
		if (dentry == -EIO)
			break;

		if (exfat_check_max_dentries(inode))
			return -ENOSPC;

		/* we trust p_dir->size regardless of FAT type */
		if (exfat_find_last_cluster(sb, p_dir, &last_clu))
			return -EIO;

		/*
		 * Allocate new cluster to this directory
		 */
		exfat_chain_set(&clu, last_clu + 1, 0, p_dir->flags);

		/* allocate a cluster */
		ret = exfat_alloc_cluster(inode, 1, &clu, IS_DIRSYNC(inode));
		if (ret)
			return ret;

		if (exfat_zeroed_cluster(inode, clu.dir))
			return -EIO;

		/* append to the FAT chain */
		if (clu.flags != p_dir->flags) {
			/* no-fat-chain bit is disabled,
			 * so fat-chain should be synced with alloc-bitmap
			 */
			exfat_chain_cont_cluster(sb, p_dir->dir, p_dir->size);
			p_dir->flags = ALLOC_FAT_CHAIN;
			hint_femp.cur.flags = ALLOC_FAT_CHAIN;
		}

		if (clu.flags == ALLOC_FAT_CHAIN)
			if (exfat_ent_set(sb, last_clu, clu.dir))
				return -EIO;

		if (hint_femp.eidx == EXFAT_HINT_NONE) {
			/* the special case that new dentry
			 * should be allocated from the start of new cluster
			 */
			hint_femp.eidx = EXFAT_B_TO_DEN_IDX(p_dir->size, sbi);
			hint_femp.count = sbi->dentries_per_clu;

			exfat_chain_set(&hint_femp.cur, clu.dir, 0, clu.flags);
		}
		hint_femp.cur.size++;
		p_dir->size++;
		size = EXFAT_CLU_TO_B(p_dir->size, sbi);

		/* update the directory entry */
		if (p_dir->dir != sbi->root_dir) {
			struct buffer_head *bh;

			ep = exfat_get_dentry(sb,
				&(ei->dir), ei->entry + 1, &bh, &sector);
			if (!ep)
				return -EIO;

			ep->dentry.stream.valid_size = cpu_to_le64(size);
			ep->dentry.stream.size = ep->dentry.stream.valid_size;
			ep->dentry.stream.flags = p_dir->flags;
			exfat_update_bh(bh, IS_DIRSYNC(inode));
			brelse(bh);
			if (exfat_update_dir_chksum(inode, &(ei->dir),
			    ei->entry))
				return -EIO;
		}

		/* directory inode should be updated in here */
		i_size_write(inode, size);
		ei->i_size_ondisk += sbi->cluster_size;
		ei->i_size_aligned += sbi->cluster_size;
		ei->flags = p_dir->flags;
		inode->i_blocks += sbi->cluster_size >> 9;
	}

	return dentry;
}

/*
 * Name Resolution Functions :
 * Zero if it was successful; otherwise nonzero.
 */
static int __exfat_resolve_path(struct inode *inode, const unsigned char *path,
		struct exfat_chain *p_dir, struct exfat_uni_name *p_uniname,
		int lookup)
{
	int namelen;
	int lossy = NLS_NAME_NO_LOSSY;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);

	/* strip all trailing periods */
	namelen = exfat_striptail_len(strlen(path), path);
	if (!namelen)
		return -ENOENT;

	if (strlen(path) > (MAX_NAME_LENGTH * MAX_CHARSET_SIZE))
		return -ENAMETOOLONG;

	/*
	 * strip all leading spaces :
	 * "MS windows 7" supports leading spaces.
	 * So we should skip this preprocessing for compatibility.
	 */

	/* file name conversion :
	 * If lookup case, we allow bad-name for compatibility.
	 */
	namelen = exfat_nls_to_utf16(sb, path, namelen, p_uniname,
			&lossy);
	if (namelen < 0)
		return namelen; /* return error value */

	if ((lossy && !lookup) || !namelen)
		return -EINVAL;

	exfat_chain_set(p_dir, ei->start_clu,
		EXFAT_B_TO_CLU(i_size_read(inode), sbi), ei->flags);

	return 0;
}

static inline int exfat_resolve_path(struct inode *inode,
		const unsigned char *path, struct exfat_chain *dir,
		struct exfat_uni_name *uni)
{
	return __exfat_resolve_path(inode, path, dir, uni, 0);
}

static inline int exfat_resolve_path_for_lookup(struct inode *inode,
		const unsigned char *path, struct exfat_chain *dir,
		struct exfat_uni_name *uni)
{
	return __exfat_resolve_path(inode, path, dir, uni, 1);
}

static inline loff_t exfat_make_i_pos(struct exfat_dir_entry *info)
{
	return ((loff_t) info->dir.dir << 32) | (info->entry & 0xffffffff);
}

static int exfat_add_entry(struct inode *inode, const char *path,
		struct exfat_chain *p_dir, unsigned int type,
		struct exfat_dir_entry *info)
{
	int ret, dentry, num_entries;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_uni_name uniname;
	struct exfat_chain clu;
	int clu_size = 0;
	unsigned int start_clu = EXFAT_FREE_CLUSTER;

	ret = exfat_resolve_path(inode, path, p_dir, &uniname);
	if (ret)
		goto out;

	num_entries = exfat_calc_num_entries(&uniname);
	if (num_entries < 0) {
		ret = num_entries;
		goto out;
	}

	/* exfat_find_empty_entry must be called before alloc_cluster() */
	dentry = exfat_find_empty_entry(inode, p_dir, num_entries);
	if (dentry < 0) {
		ret = dentry; /* -EIO or -ENOSPC */
		goto out;
	}

	if (type == TYPE_DIR) {
		ret = exfat_alloc_new_dir(inode, &clu);
		if (ret)
			goto out;
		start_clu = clu.dir;
		clu_size = sbi->cluster_size;
	}

	/* update the directory entry */
	/* fill the dos name directory entry information of the created file.
	 * the first cluster is not determined yet. (0)
	 */
	ret = exfat_init_dir_entry(inode, p_dir, dentry, type,
		start_clu, clu_size);
	if (ret)
		goto out;

	ret = exfat_init_ext_entry(inode, p_dir, dentry, num_entries, &uniname);
	if (ret)
		goto out;

	info->dir = *p_dir;
	info->entry = dentry;
	info->flags = ALLOC_NO_FAT_CHAIN;
	info->type = type;

	if (type == TYPE_FILE) {
		info->attr = ATTR_ARCHIVE;
		info->start_clu = EXFAT_EOF_CLUSTER;
		info->size = 0;
		info->num_subdirs = 0;
	} else {
		info->attr = ATTR_SUBDIR;
		info->start_clu = start_clu;
		info->size = clu_size;
		info->num_subdirs = EXFAT_MIN_SUBDIR;
	}
	memset(&info->crtime, 0, sizeof(info->crtime));
	memset(&info->mtime, 0, sizeof(info->mtime));
	memset(&info->atime, 0, sizeof(info->atime));
out:
	return ret;
}

static int exfat_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct exfat_chain cdir;
	struct exfat_dir_entry info;
	loff_t i_pos;
	int err;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	exfat_set_volume_dirty(sb);
	err = exfat_add_entry(dir, dentry->d_name.name, &cdir, TYPE_FILE,
		&info);
	exfat_clear_volume_dirty(sb);
	if (err)
		goto unlock;

	inode_inc_iversion(dir);
	dir->i_ctime = dir->i_mtime = current_time(dir);
	if (IS_DIRSYNC(dir))
		exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	i_pos = exfat_make_i_pos(&info);
	inode = exfat_build_inode(sb, &info, i_pos);
	err = PTR_ERR_OR_ZERO(inode);
	if (err)
		goto unlock;

	inode_inc_iversion(inode);
	inode->i_mtime = inode->i_atime = inode->i_ctime =
		EXFAT_I(inode)->i_crtime = current_time(inode);
	exfat_truncate_atime(&inode->i_atime);
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	d_instantiate(dentry, inode);
unlock:
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	return err;
}

/* lookup a file */
static int exfat_find(struct inode *dir, struct qstr *qname,
		struct exfat_dir_entry *info)
{
	int ret, dentry, num_entries, count;
	struct exfat_chain cdir;
	struct exfat_uni_name uni_name;
	struct super_block *sb = dir->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(dir);
	struct exfat_dentry *ep, *ep2;
	struct exfat_entry_set_cache *es;

	if (qname->len == 0)
		return -ENOENT;

	/* check the validity of directory name in the given pathname */
	ret = exfat_resolve_path_for_lookup(dir, qname->name, &cdir, &uni_name);
	if (ret)
		return ret;

	num_entries = exfat_calc_num_entries(&uni_name);
	if (num_entries < 0)
		return num_entries;

	/* check the validation of hint_stat and initialize it if required */
	if (ei->version != (inode_peek_iversion_raw(dir) & 0xffffffff)) {
		ei->hint_stat.clu = cdir.dir;
		ei->hint_stat.eidx = 0;
		ei->version = (inode_peek_iversion_raw(dir) & 0xffffffff);
		ei->hint_femp.eidx = EXFAT_HINT_NONE;
	}

	/* search the file name for directories */
	dentry = exfat_find_dir_entry(sb, ei, &cdir, &uni_name,
			num_entries, TYPE_ALL);

	if (dentry < 0)
		return dentry; /* -error value */

	info->dir = cdir;
	info->entry = dentry;
	info->num_subdirs = 0;

	es = exfat_get_dentry_set(sb, &cdir, dentry, ES_2_ENTRIES);
	if (!es)
		return -EIO;
	ep = exfat_get_dentry_cached(es, 0);
	ep2 = exfat_get_dentry_cached(es, 1);

	info->type = exfat_get_entry_type(ep);
	info->attr = le16_to_cpu(ep->dentry.file.attr);
	info->size = le64_to_cpu(ep2->dentry.stream.valid_size);
	if ((info->type == TYPE_FILE) && (info->size == 0)) {
		info->flags = ALLOC_NO_FAT_CHAIN;
		info->start_clu = EXFAT_EOF_CLUSTER;
	} else {
		info->flags = ep2->dentry.stream.flags;
		info->start_clu =
			le32_to_cpu(ep2->dentry.stream.start_clu);
	}

	exfat_get_entry_time(sbi, &info->crtime,
			     ep->dentry.file.create_tz,
			     ep->dentry.file.create_time,
			     ep->dentry.file.create_date,
			     ep->dentry.file.create_time_cs);
	exfat_get_entry_time(sbi, &info->mtime,
			     ep->dentry.file.modify_tz,
			     ep->dentry.file.modify_time,
			     ep->dentry.file.modify_date,
			     ep->dentry.file.modify_time_cs);
	exfat_get_entry_time(sbi, &info->atime,
			     ep->dentry.file.access_tz,
			     ep->dentry.file.access_time,
			     ep->dentry.file.access_date,
			     0);
	exfat_free_dentry_set(es, false);

	if (ei->start_clu == EXFAT_FREE_CLUSTER) {
		exfat_fs_error(sb,
			       "non-zero size file starts with zero cluster (size : %llu, p_dir : %u, entry : 0x%08x)",
			       i_size_read(dir), ei->dir.dir, ei->entry);
		return -EIO;
	}

	if (info->type == TYPE_DIR) {
		exfat_chain_set(&cdir, info->start_clu,
				EXFAT_B_TO_CLU(info->size, sbi), info->flags);
		count = exfat_count_dir_entries(sb, &cdir);
		if (count < 0)
			return -EIO;

		info->num_subdirs = count + EXFAT_MIN_SUBDIR;
	}
	return 0;
}

static int exfat_d_anon_disconn(struct dentry *dentry)
{
	return IS_ROOT(dentry) && (dentry->d_flags & DCACHE_DISCONNECTED);
}

static struct dentry *exfat_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct dentry *alias;
	struct exfat_dir_entry info;
	int err;
	loff_t i_pos;
	mode_t i_mode;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	err = exfat_find(dir, &dentry->d_name, &info);
	if (err) {
		if (err == -ENOENT) {
			inode = NULL;
			goto out;
		}
		goto unlock;
	}

	i_pos = exfat_make_i_pos(&info);
	inode = exfat_build_inode(sb, &info, i_pos);
	err = PTR_ERR_OR_ZERO(inode);
	if (err)
		goto unlock;

	i_mode = inode->i_mode;
	alias = d_find_alias(inode);

	/*
	 * Checking "alias->d_parent == dentry->d_parent" to make sure
	 * FS is not corrupted (especially double linked dir).
	 */
	if (alias && alias->d_parent == dentry->d_parent &&
			!exfat_d_anon_disconn(alias)) {

		/*
		 * Unhashed alias is able to exist because of revalidate()
		 * called by lookup_fast. You can easily make this status
		 * by calling create and lookup concurrently
		 * In such case, we reuse an alias instead of new dentry
		 */
		if (d_unhashed(alias)) {
			WARN_ON(alias->d_name.hash_len !=
				dentry->d_name.hash_len);
			exfat_info(sb, "rehashed a dentry(%p) in read lookup",
				   alias);
			d_drop(dentry);
			d_rehash(alias);
		} else if (!S_ISDIR(i_mode)) {
			/*
			 * This inode has non anonymous-DCACHE_DISCONNECTED
			 * dentry. This means, the user did ->lookup() by an
			 * another name (longname vs 8.3 alias of it) in past.
			 *
			 * Switch to new one for reason of locality if possible.
			 */
			d_move(alias, dentry);
		}
		iput(inode);
		mutex_unlock(&EXFAT_SB(sb)->s_lock);
		return alias;
	}
	dput(alias);
out:
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	if (!inode)
		exfat_d_version_set(dentry, inode_query_iversion(dir));

	return d_splice_alias(inode, dentry);
unlock:
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	return ERR_PTR(err);
}

/* remove an entry, BUT don't truncate */
static int exfat_unlink(struct inode *dir, struct dentry *dentry)
{
	struct exfat_chain cdir;
	struct exfat_dentry *ep;
	struct super_block *sb = dir->i_sb;
	struct inode *inode = dentry->d_inode;
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct buffer_head *bh;
	sector_t sector;
	int num_entries, entry, err = 0;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	exfat_chain_dup(&cdir, &ei->dir);
	entry = ei->entry;
	if (ei->dir.dir == DIR_DELETED) {
		exfat_err(sb, "abnormal access to deleted dentry");
		err = -ENOENT;
		goto unlock;
	}

	ep = exfat_get_dentry(sb, &cdir, entry, &bh, &sector);
	if (!ep) {
		err = -EIO;
		goto unlock;
	}
	num_entries = exfat_count_ext_entries(sb, &cdir, entry, ep);
	if (num_entries < 0) {
		err = -EIO;
		brelse(bh);
		goto unlock;
	}
	num_entries++;
	brelse(bh);

	exfat_set_volume_dirty(sb);
	/* update the directory entry */
	if (exfat_remove_entries(dir, &cdir, entry, 0, num_entries)) {
		err = -EIO;
		goto unlock;
	}

	/* This doesn't modify ei */
	ei->dir.dir = DIR_DELETED;
	exfat_clear_volume_dirty(sb);

	inode_inc_iversion(dir);
	dir->i_mtime = dir->i_atime = current_time(dir);
	exfat_truncate_atime(&dir->i_atime);
	if (IS_DIRSYNC(dir))
		exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	clear_nlink(inode);
	inode->i_mtime = inode->i_atime = current_time(inode);
	exfat_truncate_atime(&inode->i_atime);
	exfat_unhash_inode(inode);
	exfat_d_version_set(dentry, inode_query_iversion(dir));
unlock:
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	return err;
}

static int exfat_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct exfat_dir_entry info;
	struct exfat_chain cdir;
	loff_t i_pos;
	int err;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	exfat_set_volume_dirty(sb);
	err = exfat_add_entry(dir, dentry->d_name.name, &cdir, TYPE_DIR,
		&info);
	exfat_clear_volume_dirty(sb);
	if (err)
		goto unlock;

	inode_inc_iversion(dir);
	dir->i_ctime = dir->i_mtime = current_time(dir);
	if (IS_DIRSYNC(dir))
		exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);
	inc_nlink(dir);

	i_pos = exfat_make_i_pos(&info);
	inode = exfat_build_inode(sb, &info, i_pos);
	err = PTR_ERR_OR_ZERO(inode);
	if (err)
		goto unlock;

	inode_inc_iversion(inode);
	inode->i_mtime = inode->i_atime = inode->i_ctime =
		EXFAT_I(inode)->i_crtime = current_time(inode);
	exfat_truncate_atime(&inode->i_atime);
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	d_instantiate(dentry, inode);

unlock:
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	return err;
}

static int exfat_check_dir_empty(struct super_block *sb,
		struct exfat_chain *p_dir)
{
	int i, dentries_per_clu;
	unsigned int type;
	struct exfat_chain clu;
	struct exfat_dentry *ep;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct buffer_head *bh;

	dentries_per_clu = sbi->dentries_per_clu;

	exfat_chain_dup(&clu, p_dir);

	while (clu.dir != EXFAT_EOF_CLUSTER) {
		for (i = 0; i < dentries_per_clu; i++) {
			ep = exfat_get_dentry(sb, &clu, i, &bh, NULL);
			if (!ep)
				return -EIO;
			type = exfat_get_entry_type(ep);
			brelse(bh);
			if (type == TYPE_UNUSED)
				return 0;

			if (type != TYPE_FILE && type != TYPE_DIR)
				continue;

			return -ENOTEMPTY;
		}

		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			if (--clu.size > 0)
				clu.dir++;
			else
				clu.dir = EXFAT_EOF_CLUSTER;
		} else {
			if (exfat_get_next_cluster(sb, &(clu.dir)))
				return -EIO;
		}
	}

	return 0;
}

static int exfat_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct exfat_dentry *ep;
	struct exfat_chain cdir, clu_to_free;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct buffer_head *bh;
	sector_t sector;
	int num_entries, entry, err;

	mutex_lock(&EXFAT_SB(inode->i_sb)->s_lock);

	exfat_chain_dup(&cdir, &ei->dir);
	entry = ei->entry;

	if (ei->dir.dir == DIR_DELETED) {
		exfat_err(sb, "abnormal access to deleted dentry");
		err = -ENOENT;
		goto unlock;
	}

	exfat_chain_set(&clu_to_free, ei->start_clu,
		EXFAT_B_TO_CLU_ROUND_UP(i_size_read(inode), sbi), ei->flags);

	err = exfat_check_dir_empty(sb, &clu_to_free);
	if (err) {
		if (err == -EIO)
			exfat_err(sb, "failed to exfat_check_dir_empty : err(%d)",
				  err);
		goto unlock;
	}

	ep = exfat_get_dentry(sb, &cdir, entry, &bh, &sector);
	if (!ep) {
		err = -EIO;
		goto unlock;
	}

	num_entries = exfat_count_ext_entries(sb, &cdir, entry, ep);
	if (num_entries < 0) {
		err = -EIO;
		brelse(bh);
		goto unlock;
	}
	num_entries++;
	brelse(bh);

	exfat_set_volume_dirty(sb);
	err = exfat_remove_entries(dir, &cdir, entry, 0, num_entries);
	if (err) {
		exfat_err(sb, "failed to exfat_remove_entries : err(%d)", err);
		goto unlock;
	}
	ei->dir.dir = DIR_DELETED;
	exfat_clear_volume_dirty(sb);

	inode_inc_iversion(dir);
	dir->i_mtime = dir->i_atime = current_time(dir);
	exfat_truncate_atime(&dir->i_atime);
	if (IS_DIRSYNC(dir))
		exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);
	drop_nlink(dir);

	clear_nlink(inode);
	inode->i_mtime = inode->i_atime = current_time(inode);
	exfat_truncate_atime(&inode->i_atime);
	exfat_unhash_inode(inode);
	exfat_d_version_set(dentry, inode_query_iversion(dir));
unlock:
	mutex_unlock(&EXFAT_SB(inode->i_sb)->s_lock);
	return err;
}

static int exfat_rename_file(struct inode *inode, struct exfat_chain *p_dir,
		int oldentry, struct exfat_uni_name *p_uniname,
		struct exfat_inode_info *ei)
{
	int ret, num_old_entries, num_new_entries;
	sector_t sector_old, sector_new;
	struct exfat_dentry *epold, *epnew;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *new_bh, *old_bh;
	int sync = IS_DIRSYNC(inode);

	epold = exfat_get_dentry(sb, p_dir, oldentry, &old_bh, &sector_old);
	if (!epold)
		return -EIO;

	num_old_entries = exfat_count_ext_entries(sb, p_dir, oldentry, epold);
	if (num_old_entries < 0)
		return -EIO;
	num_old_entries++;

	num_new_entries = exfat_calc_num_entries(p_uniname);
	if (num_new_entries < 0)
		return num_new_entries;

	if (num_old_entries < num_new_entries) {
		int newentry;

		newentry =
			exfat_find_empty_entry(inode, p_dir, num_new_entries);
		if (newentry < 0)
			return newentry; /* -EIO or -ENOSPC */

		epnew = exfat_get_dentry(sb, p_dir, newentry, &new_bh,
			&sector_new);
		if (!epnew)
			return -EIO;

		*epnew = *epold;
		if (exfat_get_entry_type(epnew) == TYPE_FILE) {
			epnew->dentry.file.attr |= cpu_to_le16(ATTR_ARCHIVE);
			ei->attr |= ATTR_ARCHIVE;
		}
		exfat_update_bh(new_bh, sync);
		brelse(old_bh);
		brelse(new_bh);

		epold = exfat_get_dentry(sb, p_dir, oldentry + 1, &old_bh,
			&sector_old);
		if (!epold)
			return -EIO;
		epnew = exfat_get_dentry(sb, p_dir, newentry + 1, &new_bh,
			&sector_new);
		if (!epnew) {
			brelse(old_bh);
			return -EIO;
		}

		*epnew = *epold;
		exfat_update_bh(new_bh, sync);
		brelse(old_bh);
		brelse(new_bh);

		ret = exfat_init_ext_entry(inode, p_dir, newentry,
			num_new_entries, p_uniname);
		if (ret)
			return ret;

		exfat_remove_entries(inode, p_dir, oldentry, 0,
			num_old_entries);
		ei->entry = newentry;
	} else {
		if (exfat_get_entry_type(epold) == TYPE_FILE) {
			epold->dentry.file.attr |= cpu_to_le16(ATTR_ARCHIVE);
			ei->attr |= ATTR_ARCHIVE;
		}
		exfat_update_bh(old_bh, sync);
		brelse(old_bh);
		ret = exfat_init_ext_entry(inode, p_dir, oldentry,
			num_new_entries, p_uniname);
		if (ret)
			return ret;

		exfat_remove_entries(inode, p_dir, oldentry, num_new_entries,
			num_old_entries);
	}
	return 0;
}

static int exfat_move_file(struct inode *inode, struct exfat_chain *p_olddir,
		int oldentry, struct exfat_chain *p_newdir,
		struct exfat_uni_name *p_uniname, struct exfat_inode_info *ei)
{
	int ret, newentry, num_new_entries, num_old_entries;
	sector_t sector_mov, sector_new;
	struct exfat_dentry *epmov, *epnew;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *mov_bh, *new_bh;

	epmov = exfat_get_dentry(sb, p_olddir, oldentry, &mov_bh, &sector_mov);
	if (!epmov)
		return -EIO;

	num_old_entries = exfat_count_ext_entries(sb, p_olddir, oldentry,
		epmov);
	if (num_old_entries < 0)
		return -EIO;
	num_old_entries++;

	num_new_entries = exfat_calc_num_entries(p_uniname);
	if (num_new_entries < 0)
		return num_new_entries;

	newentry = exfat_find_empty_entry(inode, p_newdir, num_new_entries);
	if (newentry < 0)
		return newentry; /* -EIO or -ENOSPC */

	epnew = exfat_get_dentry(sb, p_newdir, newentry, &new_bh, &sector_new);
	if (!epnew)
		return -EIO;

	*epnew = *epmov;
	if (exfat_get_entry_type(epnew) == TYPE_FILE) {
		epnew->dentry.file.attr |= cpu_to_le16(ATTR_ARCHIVE);
		ei->attr |= ATTR_ARCHIVE;
	}
	exfat_update_bh(new_bh, IS_DIRSYNC(inode));
	brelse(mov_bh);
	brelse(new_bh);

	epmov = exfat_get_dentry(sb, p_olddir, oldentry + 1, &mov_bh,
		&sector_mov);
	if (!epmov)
		return -EIO;
	epnew = exfat_get_dentry(sb, p_newdir, newentry + 1, &new_bh,
		&sector_new);
	if (!epnew) {
		brelse(mov_bh);
		return -EIO;
	}

	*epnew = *epmov;
	exfat_update_bh(new_bh, IS_DIRSYNC(inode));
	brelse(mov_bh);
	brelse(new_bh);

	ret = exfat_init_ext_entry(inode, p_newdir, newentry, num_new_entries,
		p_uniname);
	if (ret)
		return ret;

	exfat_remove_entries(inode, p_olddir, oldentry, 0, num_old_entries);

	exfat_chain_set(&ei->dir, p_newdir->dir, p_newdir->size,
		p_newdir->flags);

	ei->entry = newentry;
	return 0;
}

static void exfat_update_parent_info(struct exfat_inode_info *ei,
		struct inode *parent_inode)
{
	struct exfat_sb_info *sbi = EXFAT_SB(parent_inode->i_sb);
	struct exfat_inode_info *parent_ei = EXFAT_I(parent_inode);
	loff_t parent_isize = i_size_read(parent_inode);

	/*
	 * the problem that struct exfat_inode_info caches wrong parent info.
	 *
	 * because of flag-mismatch of ei->dir,
	 * there is abnormal traversing cluster chain.
	 */
	if (unlikely(parent_ei->flags != ei->dir.flags ||
		     parent_isize != EXFAT_CLU_TO_B(ei->dir.size, sbi) ||
		     parent_ei->start_clu != ei->dir.dir)) {
		exfat_chain_set(&ei->dir, parent_ei->start_clu,
			EXFAT_B_TO_CLU_ROUND_UP(parent_isize, sbi),
			parent_ei->flags);
	}
}

/* rename or move a old file into a new file */
static int __exfat_rename(struct inode *old_parent_inode,
		struct exfat_inode_info *ei, struct inode *new_parent_inode,
		struct dentry *new_dentry)
{
	int ret;
	int dentry;
	struct exfat_chain olddir, newdir;
	struct exfat_chain *p_dir = NULL;
	struct exfat_uni_name uni_name;
	struct exfat_dentry *ep;
	struct super_block *sb = old_parent_inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	const unsigned char *new_path = new_dentry->d_name.name;
	struct inode *new_inode = new_dentry->d_inode;
	int num_entries;
	struct exfat_inode_info *new_ei = NULL;
	unsigned int new_entry_type = TYPE_UNUSED;
	int new_entry = 0;
	struct buffer_head *old_bh, *new_bh = NULL;

	/* check the validity of pointer parameters */
	if (new_path == NULL || strlen(new_path) == 0)
		return -EINVAL;

	if (ei->dir.dir == DIR_DELETED) {
		exfat_err(sb, "abnormal access to deleted source dentry");
		return -ENOENT;
	}

	exfat_update_parent_info(ei, old_parent_inode);

	exfat_chain_dup(&olddir, &ei->dir);
	dentry = ei->entry;

	ep = exfat_get_dentry(sb, &olddir, dentry, &old_bh, NULL);
	if (!ep) {
		ret = -EIO;
		goto out;
	}
	brelse(old_bh);

	/* check whether new dir is existing directory and empty */
	if (new_inode) {
		ret = -EIO;
		new_ei = EXFAT_I(new_inode);

		if (new_ei->dir.dir == DIR_DELETED) {
			exfat_err(sb, "abnormal access to deleted target dentry");
			goto out;
		}

		exfat_update_parent_info(new_ei, new_parent_inode);

		p_dir = &(new_ei->dir);
		new_entry = new_ei->entry;
		ep = exfat_get_dentry(sb, p_dir, new_entry, &new_bh, NULL);
		if (!ep)
			goto out;

		new_entry_type = exfat_get_entry_type(ep);
		brelse(new_bh);

		/* if new_inode exists, update ei */
		if (new_entry_type == TYPE_DIR) {
			struct exfat_chain new_clu;

			new_clu.dir = new_ei->start_clu;
			new_clu.size =
				EXFAT_B_TO_CLU_ROUND_UP(i_size_read(new_inode),
				sbi);
			new_clu.flags = new_ei->flags;

			ret = exfat_check_dir_empty(sb, &new_clu);
			if (ret)
				goto out;
		}
	}

	/* check the validity of directory name in the given new pathname */
	ret = exfat_resolve_path(new_parent_inode, new_path, &newdir,
			&uni_name);
	if (ret)
		goto out;

	exfat_set_volume_dirty(sb);

	if (olddir.dir == newdir.dir)
		ret = exfat_rename_file(new_parent_inode, &olddir, dentry,
				&uni_name, ei);
	else
		ret = exfat_move_file(new_parent_inode, &olddir, dentry,
				&newdir, &uni_name, ei);

	if (!ret && new_inode) {
		/* delete entries of new_dir */
		ep = exfat_get_dentry(sb, p_dir, new_entry, &new_bh, NULL);
		if (!ep) {
			ret = -EIO;
			goto del_out;
		}

		num_entries = exfat_count_ext_entries(sb, p_dir, new_entry, ep);
		if (num_entries < 0) {
			ret = -EIO;
			goto del_out;
		}
		brelse(new_bh);

		if (exfat_remove_entries(new_inode, p_dir, new_entry, 0,
				num_entries + 1)) {
			ret = -EIO;
			goto del_out;
		}

		/* Free the clusters if new_inode is a dir(as if exfat_rmdir) */
		if (new_entry_type == TYPE_DIR) {
			/* new_ei, new_clu_to_free */
			struct exfat_chain new_clu_to_free;

			exfat_chain_set(&new_clu_to_free, new_ei->start_clu,
				EXFAT_B_TO_CLU_ROUND_UP(i_size_read(new_inode),
				sbi), new_ei->flags);

			if (exfat_free_cluster(new_inode, &new_clu_to_free)) {
				/* just set I/O error only */
				ret = -EIO;
			}

			i_size_write(new_inode, 0);
			new_ei->start_clu = EXFAT_EOF_CLUSTER;
			new_ei->flags = ALLOC_NO_FAT_CHAIN;
		}
del_out:
		/* Update new_inode ei
		 * Prevent syncing removed new_inode
		 * (new_ei is already initialized above code ("if (new_inode)")
		 */
		new_ei->dir.dir = DIR_DELETED;
	}
	exfat_clear_volume_dirty(sb);
out:
	return ret;
}

static int exfat_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry,
		unsigned int flags)
{
	struct inode *old_inode, *new_inode;
	struct super_block *sb = old_dir->i_sb;
	loff_t i_pos;
	int err;

	/*
	 * The VFS already checks for existence, so for local filesystems
	 * the RENAME_NOREPLACE implementation is equivalent to plain rename.
	 * Don't support any other flags
	 */
	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;

	err = __exfat_rename(old_dir, EXFAT_I(old_inode), new_dir, new_dentry);
	if (err)
		goto unlock;

	inode_inc_iversion(new_dir);
	new_dir->i_ctime = new_dir->i_mtime = new_dir->i_atime =
		EXFAT_I(new_dir)->i_crtime = current_time(new_dir);
	exfat_truncate_atime(&new_dir->i_atime);
	if (IS_DIRSYNC(new_dir))
		exfat_sync_inode(new_dir);
	else
		mark_inode_dirty(new_dir);

	i_pos = ((loff_t)EXFAT_I(old_inode)->dir.dir << 32) |
		(EXFAT_I(old_inode)->entry & 0xffffffff);
	exfat_unhash_inode(old_inode);
	exfat_hash_inode(old_inode, i_pos);
	if (IS_DIRSYNC(new_dir))
		exfat_sync_inode(old_inode);
	else
		mark_inode_dirty(old_inode);

	if (S_ISDIR(old_inode->i_mode) && old_dir != new_dir) {
		drop_nlink(old_dir);
		if (!new_inode)
			inc_nlink(new_dir);
	}

	inode_inc_iversion(old_dir);
	old_dir->i_ctime = old_dir->i_mtime = current_time(old_dir);
	if (IS_DIRSYNC(old_dir))
		exfat_sync_inode(old_dir);
	else
		mark_inode_dirty(old_dir);

	if (new_inode) {
		exfat_unhash_inode(new_inode);

		/* skip drop_nlink if new_inode already has been dropped */
		if (new_inode->i_nlink) {
			drop_nlink(new_inode);
			if (S_ISDIR(new_inode->i_mode))
				drop_nlink(new_inode);
		} else {
			exfat_warn(sb, "abnormal access to an inode dropped");
			WARN_ON(new_inode->i_nlink == 0);
		}
		new_inode->i_ctime = EXFAT_I(new_inode)->i_crtime =
			current_time(new_inode);
	}

unlock:
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	return err;
}

const struct inode_operations exfat_dir_inode_operations = {
	.create		= exfat_create,
	.lookup		= exfat_lookup,
	.unlink		= exfat_unlink,
	.mkdir		= exfat_mkdir,
	.rmdir		= exfat_rmdir,
	.rename		= exfat_rename,
	.setattr	= exfat_setattr,
	.getattr	= exfat_getattr,
};
