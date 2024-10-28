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

/* returns the length of a struct qstr, ignoring trailing dots if necessary */
static unsigned int exfat_striptail_len(unsigned int len, const char *name,
					bool keep_last_dots)
{
	if (!keep_last_dots) {
		while (len && name[len - 1] == '.')
			len--;
	}
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
	unsigned int len = exfat_striptail_len(qstr->len, qstr->name,
			   EXFAT_SB(sb)->options.keep_last_dots);
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
	unsigned int alen = exfat_striptail_len(name->len, name->name,
				EXFAT_SB(sb)->options.keep_last_dots);
	unsigned int blen = exfat_striptail_len(len, str,
				EXFAT_SB(sb)->options.keep_last_dots);
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
	unsigned int len = exfat_striptail_len(qstr->len, qstr->name,
			       EXFAT_SB(sb)->options.keep_last_dots);
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
	unsigned int alen = exfat_striptail_len(name->len, name->name,
				EXFAT_SB(sb)->options.keep_last_dots);
	unsigned int blen = exfat_striptail_len(len, str,
				EXFAT_SB(sb)->options.keep_last_dots);

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

/* search EMPTY CONTINUOUS "num_entries" entries */
static int exfat_search_empty_slot(struct super_block *sb,
		struct exfat_hint_femp *hint_femp, struct exfat_chain *p_dir,
		int num_entries, struct exfat_entry_set_cache *es)
{
	int i, dentry, ret;
	int dentries_per_clu;
	struct exfat_chain clu;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int total_entries = EXFAT_CLU_TO_DEN(p_dir->size, sbi);

	dentries_per_clu = sbi->dentries_per_clu;

	if (hint_femp->eidx != EXFAT_HINT_NONE) {
		dentry = hint_femp->eidx;

		/*
		 * If hint_femp->count is enough, it is needed to check if
		 * there are actual empty entries.
		 * Otherwise, and if "dentry + hint_famp->count" is also equal
		 * to "p_dir->size * dentries_per_clu", it means ENOSPC.
		 */
		if (dentry + hint_femp->count == total_entries &&
		    num_entries > hint_femp->count)
			return -ENOSPC;

		hint_femp->eidx = EXFAT_HINT_NONE;
		exfat_chain_dup(&clu, &hint_femp->cur);
	} else {
		exfat_chain_dup(&clu, p_dir);
		dentry = 0;
	}

	while (dentry + num_entries < total_entries &&
	       clu.dir != EXFAT_EOF_CLUSTER) {
		i = dentry & (dentries_per_clu - 1);

		ret = exfat_get_empty_dentry_set(es, sb, &clu, i, num_entries);
		if (ret < 0)
			return ret;
		else if (ret == 0)
			return dentry;

		dentry += ret;
		i += ret;

		while (i >= dentries_per_clu) {
			if (clu.flags == ALLOC_NO_FAT_CHAIN) {
				if (--clu.size > 0)
					clu.dir++;
				else
					clu.dir = EXFAT_EOF_CLUSTER;
			} else {
				if (exfat_get_next_cluster(sb, &clu.dir))
					return -EIO;
			}

			i -= dentries_per_clu;
		}
	}

	hint_femp->eidx = dentry;
	hint_femp->count = 0;
	if (dentry == total_entries || clu.dir == EXFAT_EOF_CLUSTER)
		exfat_chain_set(&hint_femp->cur, EXFAT_EOF_CLUSTER, 0,
				clu.flags);
	else
		hint_femp->cur = clu;

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
		struct exfat_chain *p_dir, int num_entries,
		struct exfat_entry_set_cache *es)
{
	int dentry;
	unsigned int ret, last_clu;
	loff_t size = 0;
	struct exfat_chain clu;
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
					num_entries, es)) < 0) {
		if (dentry == -EIO)
			break;

		if (exfat_check_max_dentries(inode))
			return -ENOSPC;

		/*
		 * Allocate new cluster to this directory
		 */
		if (ei->start_clu != EXFAT_EOF_CLUSTER) {
			/* we trust p_dir->size regardless of FAT type */
			if (exfat_find_last_cluster(sb, p_dir, &last_clu))
				return -EIO;

			exfat_chain_set(&clu, last_clu + 1, 0, p_dir->flags);
		} else {
			/* This directory is empty */
			exfat_chain_set(&clu, EXFAT_EOF_CLUSTER, 0,
					ALLOC_NO_FAT_CHAIN);
		}

		/* allocate a cluster */
		ret = exfat_alloc_cluster(inode, 1, &clu, IS_DIRSYNC(inode));
		if (ret)
			return ret;

		if (exfat_zeroed_cluster(inode, clu.dir))
			return -EIO;

		if (ei->start_clu == EXFAT_EOF_CLUSTER) {
			ei->start_clu = clu.dir;
			p_dir->dir = clu.dir;
			hint_femp.eidx = 0;
		}

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

		if (hint_femp.cur.dir == EXFAT_EOF_CLUSTER)
			exfat_chain_set(&hint_femp.cur, clu.dir, 0, clu.flags);

		hint_femp.count += sbi->dentries_per_clu;

		hint_femp.cur.size++;
		p_dir->size++;
		size = EXFAT_CLU_TO_B(p_dir->size, sbi);

		/* directory inode should be updated in here */
		i_size_write(inode, size);
		ei->valid_size += sbi->cluster_size;
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
	int pathlen = strlen(path);

	/*
	 * get the length of the pathname excluding
	 * trailing periods, if any.
	 */
	namelen = exfat_striptail_len(pathlen, path, false);
	if (EXFAT_SB(sb)->options.keep_last_dots) {
		/*
		 * Do not allow the creation of files with names
		 * ending with period(s).
		 */
		if (!lookup && (namelen < pathlen))
			return -EINVAL;
		namelen = pathlen;
	}
	if (!namelen)
		return -ENOENT;
	if (pathlen > (MAX_NAME_LENGTH * MAX_CHARSET_SIZE))
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
		return (lossy & NLS_NAME_OVERLEN) ? -ENAMETOOLONG : -EINVAL;

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
	struct timespec64 ts = current_time(inode);
	struct exfat_entry_set_cache es;
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
	dentry = exfat_find_empty_entry(inode, p_dir, num_entries, &es);
	if (dentry < 0) {
		ret = dentry; /* -EIO or -ENOSPC */
		goto out;
	}

	if (type == TYPE_DIR && !sbi->options.zero_size_dir) {
		ret = exfat_alloc_new_dir(inode, &clu);
		if (ret) {
			exfat_put_dentry_set(&es, false);
			goto out;
		}
		start_clu = clu.dir;
		clu_size = sbi->cluster_size;
	}

	/* update the directory entry */
	/* fill the dos name directory entry information of the created file.
	 * the first cluster is not determined yet. (0)
	 */
	exfat_init_dir_entry(&es, type, start_clu, clu_size, &ts);
	exfat_init_ext_entry(&es, num_entries, &uniname);

	ret = exfat_put_dentry_set(&es, IS_DIRSYNC(inode));
	if (ret)
		goto out;

	info->dir = *p_dir;
	info->entry = dentry;
	info->flags = ALLOC_NO_FAT_CHAIN;
	info->type = type;

	if (type == TYPE_FILE) {
		info->attr = EXFAT_ATTR_ARCHIVE;
		info->start_clu = EXFAT_EOF_CLUSTER;
		info->size = 0;
		info->num_subdirs = 0;
	} else {
		info->attr = EXFAT_ATTR_SUBDIR;
		if (sbi->options.zero_size_dir)
			info->start_clu = EXFAT_EOF_CLUSTER;
		else
			info->start_clu = start_clu;
		info->size = clu_size;
		info->num_subdirs = EXFAT_MIN_SUBDIR;
	}
	info->valid_size = info->size;

	memset(&info->crtime, 0, sizeof(info->crtime));
	memset(&info->mtime, 0, sizeof(info->mtime));
	memset(&info->atime, 0, sizeof(info->atime));
out:
	return ret;
}

static int exfat_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct exfat_chain cdir;
	struct exfat_dir_entry info;
	loff_t i_pos;
	int err;
	loff_t size = i_size_read(dir);

	if (unlikely(exfat_forced_shutdown(sb)))
		return -EIO;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	exfat_set_volume_dirty(sb);
	err = exfat_add_entry(dir, dentry->d_name.name, &cdir, TYPE_FILE,
		&info);
	if (err)
		goto unlock;

	inode_inc_iversion(dir);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	if (IS_DIRSYNC(dir) && size != i_size_read(dir))
		exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	i_pos = exfat_make_i_pos(&info);
	inode = exfat_build_inode(sb, &info, i_pos);
	err = PTR_ERR_OR_ZERO(inode);
	if (err)
		goto unlock;

	inode_inc_iversion(inode);
	EXFAT_I(inode)->i_crtime = simple_inode_init_ts(inode);
	exfat_truncate_inode_atime(inode);

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
	int ret, dentry, count;
	struct exfat_chain cdir;
	struct exfat_uni_name uni_name;
	struct super_block *sb = dir->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(dir);
	struct exfat_dentry *ep, *ep2;
	struct exfat_entry_set_cache es;
	/* for optimized dir & entry to prevent long traverse of cluster chain */
	struct exfat_hint hint_opt;

	if (qname->len == 0)
		return -ENOENT;

	/* check the validity of directory name in the given pathname */
	ret = exfat_resolve_path_for_lookup(dir, qname->name, &cdir, &uni_name);
	if (ret)
		return ret;

	/* check the validation of hint_stat and initialize it if required */
	if (ei->version != (inode_peek_iversion_raw(dir) & 0xffffffff)) {
		ei->hint_stat.clu = cdir.dir;
		ei->hint_stat.eidx = 0;
		ei->version = (inode_peek_iversion_raw(dir) & 0xffffffff);
		ei->hint_femp.eidx = EXFAT_HINT_NONE;
	}

	/* search the file name for directories */
	dentry = exfat_find_dir_entry(sb, ei, &cdir, &uni_name, &hint_opt);
	if (dentry < 0)
		return dentry; /* -error value */

	info->dir = cdir;
	info->entry = dentry;
	info->num_subdirs = 0;

	/* adjust cdir to the optimized value */
	cdir.dir = hint_opt.clu;
	if (cdir.flags & ALLOC_NO_FAT_CHAIN)
		cdir.size -= dentry / sbi->dentries_per_clu;
	dentry = hint_opt.eidx;
	if (exfat_get_dentry_set(&es, sb, &cdir, dentry, ES_2_ENTRIES))
		return -EIO;
	ep = exfat_get_dentry_cached(&es, ES_IDX_FILE);
	ep2 = exfat_get_dentry_cached(&es, ES_IDX_STREAM);

	info->type = exfat_get_entry_type(ep);
	info->attr = le16_to_cpu(ep->dentry.file.attr);
	info->size = le64_to_cpu(ep2->dentry.stream.valid_size);
	info->valid_size = le64_to_cpu(ep2->dentry.stream.valid_size);
	info->size = le64_to_cpu(ep2->dentry.stream.size);

	info->start_clu = le32_to_cpu(ep2->dentry.stream.start_clu);
	if (!is_valid_cluster(sbi, info->start_clu) && info->size) {
		exfat_warn(sb, "start_clu is invalid cluster(0x%x)",
				info->start_clu);
		info->size = 0;
		info->valid_size = 0;
	}

	if (info->valid_size > info->size) {
		exfat_warn(sb, "valid_size(%lld) is greater than size(%lld)",
				info->valid_size, info->size);
		info->valid_size = info->size;
	}

	if (info->size == 0) {
		info->flags = ALLOC_NO_FAT_CHAIN;
		info->start_clu = EXFAT_EOF_CLUSTER;
	} else
		info->flags = ep2->dentry.stream.flags;

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
	exfat_put_dentry_set(&es, false);

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
	struct super_block *sb = dir->i_sb;
	struct inode *inode = dentry->d_inode;
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct exfat_entry_set_cache es;
	int entry, err = 0;

	if (unlikely(exfat_forced_shutdown(sb)))
		return -EIO;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	exfat_chain_dup(&cdir, &ei->dir);
	entry = ei->entry;
	if (ei->dir.dir == DIR_DELETED) {
		exfat_err(sb, "abnormal access to deleted dentry");
		err = -ENOENT;
		goto unlock;
	}

	err = exfat_get_dentry_set(&es, sb, &cdir, entry, ES_ALL_ENTRIES);
	if (err) {
		err = -EIO;
		goto unlock;
	}

	exfat_set_volume_dirty(sb);

	/* update the directory entry */
	exfat_remove_entries(inode, &es, ES_IDX_FILE);

	err = exfat_put_dentry_set(&es, IS_DIRSYNC(inode));
	if (err)
		goto unlock;

	/* This doesn't modify ei */
	ei->dir.dir = DIR_DELETED;

	inode_inc_iversion(dir);
	simple_inode_init_ts(dir);
	exfat_truncate_inode_atime(dir);
	mark_inode_dirty(dir);

	clear_nlink(inode);
	simple_inode_init_ts(inode);
	exfat_truncate_inode_atime(inode);
	exfat_unhash_inode(inode);
	exfat_d_version_set(dentry, inode_query_iversion(dir));
unlock:
	mutex_unlock(&EXFAT_SB(sb)->s_lock);
	return err;
}

static int exfat_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct exfat_dir_entry info;
	struct exfat_chain cdir;
	loff_t i_pos;
	int err;
	loff_t size = i_size_read(dir);

	if (unlikely(exfat_forced_shutdown(sb)))
		return -EIO;

	mutex_lock(&EXFAT_SB(sb)->s_lock);
	exfat_set_volume_dirty(sb);
	err = exfat_add_entry(dir, dentry->d_name.name, &cdir, TYPE_DIR,
		&info);
	if (err)
		goto unlock;

	inode_inc_iversion(dir);
	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	if (IS_DIRSYNC(dir) && size != i_size_read(dir))
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
	EXFAT_I(inode)->i_crtime = simple_inode_init_ts(inode);
	exfat_truncate_inode_atime(inode);
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

	if (p_dir->dir == EXFAT_EOF_CLUSTER)
		return 0;

	exfat_chain_dup(&clu, p_dir);

	while (clu.dir != EXFAT_EOF_CLUSTER) {
		for (i = 0; i < dentries_per_clu; i++) {
			ep = exfat_get_dentry(sb, &clu, i, &bh);
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
	struct exfat_chain cdir, clu_to_free;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct exfat_entry_set_cache es;
	int entry, err;

	if (unlikely(exfat_forced_shutdown(sb)))
		return -EIO;

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

	err = exfat_get_dentry_set(&es, sb, &cdir, entry, ES_ALL_ENTRIES);
	if (err) {
		err = -EIO;
		goto unlock;
	}

	exfat_set_volume_dirty(sb);

	exfat_remove_entries(inode, &es, ES_IDX_FILE);

	err = exfat_put_dentry_set(&es, IS_DIRSYNC(dir));
	if (err)
		goto unlock;

	ei->dir.dir = DIR_DELETED;

	inode_inc_iversion(dir);
	simple_inode_init_ts(dir);
	exfat_truncate_inode_atime(dir);
	if (IS_DIRSYNC(dir))
		exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);
	drop_nlink(dir);

	clear_nlink(inode);
	simple_inode_init_ts(inode);
	exfat_truncate_inode_atime(inode);
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
	int ret, num_new_entries;
	struct exfat_dentry *epold, *epnew;
	struct super_block *sb = inode->i_sb;
	struct exfat_entry_set_cache old_es, new_es;
	int sync = IS_DIRSYNC(inode);

	if (unlikely(exfat_forced_shutdown(sb)))
		return -EIO;

	num_new_entries = exfat_calc_num_entries(p_uniname);
	if (num_new_entries < 0)
		return num_new_entries;

	ret = exfat_get_dentry_set(&old_es, sb, p_dir, oldentry, ES_ALL_ENTRIES);
	if (ret) {
		ret = -EIO;
		return ret;
	}

	epold = exfat_get_dentry_cached(&old_es, ES_IDX_FILE);

	if (old_es.num_entries < num_new_entries) {
		int newentry;

		newentry = exfat_find_empty_entry(inode, p_dir, num_new_entries,
				&new_es);
		if (newentry < 0) {
			ret = newentry; /* -EIO or -ENOSPC */
			goto put_old_es;
		}

		epnew = exfat_get_dentry_cached(&new_es, ES_IDX_FILE);
		*epnew = *epold;
		if (exfat_get_entry_type(epnew) == TYPE_FILE) {
			epnew->dentry.file.attr |= cpu_to_le16(EXFAT_ATTR_ARCHIVE);
			ei->attr |= EXFAT_ATTR_ARCHIVE;
		}

		epold = exfat_get_dentry_cached(&old_es, ES_IDX_STREAM);
		epnew = exfat_get_dentry_cached(&new_es, ES_IDX_STREAM);
		*epnew = *epold;

		exfat_init_ext_entry(&new_es, num_new_entries, p_uniname);

		ret = exfat_put_dentry_set(&new_es, sync);
		if (ret)
			goto put_old_es;

		exfat_remove_entries(inode, &old_es, ES_IDX_FILE);
		ei->dir = *p_dir;
		ei->entry = newentry;
	} else {
		if (exfat_get_entry_type(epold) == TYPE_FILE) {
			epold->dentry.file.attr |= cpu_to_le16(EXFAT_ATTR_ARCHIVE);
			ei->attr |= EXFAT_ATTR_ARCHIVE;
		}

		exfat_remove_entries(inode, &old_es, ES_IDX_FIRST_FILENAME + 1);
		exfat_init_ext_entry(&old_es, num_new_entries, p_uniname);
	}
	return exfat_put_dentry_set(&old_es, sync);

put_old_es:
	exfat_put_dentry_set(&old_es, false);
	return ret;
}

static int exfat_move_file(struct inode *inode, struct exfat_chain *p_olddir,
		int oldentry, struct exfat_chain *p_newdir,
		struct exfat_uni_name *p_uniname, struct exfat_inode_info *ei)
{
	int ret, newentry, num_new_entries;
	struct exfat_dentry *epmov, *epnew;
	struct super_block *sb = inode->i_sb;
	struct exfat_entry_set_cache mov_es, new_es;

	num_new_entries = exfat_calc_num_entries(p_uniname);
	if (num_new_entries < 0)
		return num_new_entries;

	ret = exfat_get_dentry_set(&mov_es, sb, p_olddir, oldentry,
			ES_ALL_ENTRIES);
	if (ret)
		return -EIO;

	newentry = exfat_find_empty_entry(inode, p_newdir, num_new_entries,
			&new_es);
	if (newentry < 0) {
		ret = newentry; /* -EIO or -ENOSPC */
		goto put_mov_es;
	}

	epmov = exfat_get_dentry_cached(&mov_es, ES_IDX_FILE);
	epnew = exfat_get_dentry_cached(&new_es, ES_IDX_FILE);
	*epnew = *epmov;
	if (exfat_get_entry_type(epnew) == TYPE_FILE) {
		epnew->dentry.file.attr |= cpu_to_le16(EXFAT_ATTR_ARCHIVE);
		ei->attr |= EXFAT_ATTR_ARCHIVE;
	}

	epmov = exfat_get_dentry_cached(&mov_es, ES_IDX_STREAM);
	epnew = exfat_get_dentry_cached(&new_es, ES_IDX_STREAM);
	*epnew = *epmov;

	exfat_init_ext_entry(&new_es, num_new_entries, p_uniname);
	exfat_remove_entries(inode, &mov_es, ES_IDX_FILE);

	exfat_chain_set(&ei->dir, p_newdir->dir, p_newdir->size,
		p_newdir->flags);

	ei->entry = newentry;

	ret = exfat_put_dentry_set(&new_es, IS_DIRSYNC(inode));
	if (ret)
		goto put_mov_es;

	return exfat_put_dentry_set(&mov_es, IS_DIRSYNC(inode));

put_mov_es:
	exfat_put_dentry_set(&mov_es, false);

	return ret;
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
	struct exfat_inode_info *new_ei = NULL;
	unsigned int new_entry_type = TYPE_UNUSED;
	int new_entry = 0;
	struct buffer_head *new_bh = NULL;

	/* check the validity of pointer parameters */
	if (new_path == NULL || strlen(new_path) == 0)
		return -EINVAL;

	if (ei->dir.dir == DIR_DELETED) {
		exfat_err(sb, "abnormal access to deleted source dentry");
		return -ENOENT;
	}

	exfat_chain_set(&olddir, EXFAT_I(old_parent_inode)->start_clu,
		EXFAT_B_TO_CLU_ROUND_UP(i_size_read(old_parent_inode), sbi),
		EXFAT_I(old_parent_inode)->flags);
	dentry = ei->entry;

	/* check whether new dir is existing directory and empty */
	if (new_inode) {
		ret = -EIO;
		new_ei = EXFAT_I(new_inode);

		if (new_ei->dir.dir == DIR_DELETED) {
			exfat_err(sb, "abnormal access to deleted target dentry");
			goto out;
		}

		p_dir = &(new_ei->dir);
		new_entry = new_ei->entry;
		ep = exfat_get_dentry(sb, p_dir, new_entry, &new_bh);
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
		struct exfat_entry_set_cache es;

		/* delete entries of new_dir */
		ret = exfat_get_dentry_set(&es, sb, p_dir, new_entry,
				ES_ALL_ENTRIES);
		if (ret) {
			ret = -EIO;
			goto del_out;
		}

		exfat_remove_entries(new_inode, &es, ES_IDX_FILE);

		ret = exfat_put_dentry_set(&es, IS_DIRSYNC(new_inode));
		if (ret)
			goto del_out;

		/* Free the clusters if new_inode is a dir(as if exfat_rmdir) */
		if (new_entry_type == TYPE_DIR &&
		    new_ei->start_clu != EXFAT_EOF_CLUSTER) {
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
			new_ei->valid_size = 0;
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
out:
	return ret;
}

static int exfat_rename(struct mnt_idmap *idmap,
			struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct inode *old_inode, *new_inode;
	struct super_block *sb = old_dir->i_sb;
	loff_t i_pos;
	int err;
	loff_t size = i_size_read(new_dir);

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
	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);
	EXFAT_I(new_dir)->i_crtime = current_time(new_dir);
	exfat_truncate_inode_atime(new_dir);
	if (IS_DIRSYNC(new_dir) && size != i_size_read(new_dir))
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
	if (new_dir != old_dir)
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
		EXFAT_I(new_inode)->i_crtime = current_time(new_inode);
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
