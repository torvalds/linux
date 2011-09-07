/*
 * fs/logfs/dir.c	- directory-related code
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 */
#include "logfs.h"
#include <linux/slab.h>

/*
 * Atomic dir operations
 *
 * Directory operations are by default not atomic.  Dentries and Inodes are
 * created/removed/altered in separate operations.  Therefore we need to do
 * a small amount of journaling.
 *
 * Create, link, mkdir, mknod and symlink all share the same function to do
 * the work: __logfs_create.  This function works in two atomic steps:
 * 1. allocate inode (remember in journal)
 * 2. allocate dentry (clear journal)
 *
 * As we can only get interrupted between the two, when the inode we just
 * created is simply stored in the anchor.  On next mount, if we were
 * interrupted, we delete the inode.  From a users point of view the
 * operation never happened.
 *
 * Unlink and rmdir also share the same function: unlink.  Again, this
 * function works in two atomic steps
 * 1. remove dentry (remember inode in journal)
 * 2. unlink inode (clear journal)
 *
 * And again, on the next mount, if we were interrupted, we delete the inode.
 * From a users point of view the operation succeeded.
 *
 * Rename is the real pain to deal with, harder than all the other methods
 * combined.  Depending on the circumstances we can run into three cases.
 * A "target rename" where the target dentry already existed, a "local
 * rename" where both parent directories are identical or a "cross-directory
 * rename" in the remaining case.
 *
 * Local rename is atomic, as the old dentry is simply rewritten with a new
 * name.
 *
 * Cross-directory rename works in two steps, similar to __logfs_create and
 * logfs_unlink:
 * 1. Write new dentry (remember old dentry in journal)
 * 2. Remove old dentry (clear journal)
 *
 * Here we remember a dentry instead of an inode.  On next mount, if we were
 * interrupted, we delete the dentry.  From a users point of view, the
 * operation succeeded.
 *
 * Target rename works in three atomic steps:
 * 1. Attach old inode to new dentry (remember old dentry and new inode)
 * 2. Remove old dentry (still remember the new inode)
 * 3. Remove victim inode
 *
 * Here we remember both an inode an a dentry.  If we get interrupted
 * between steps 1 and 2, we delete both the dentry and the inode.  If
 * we get interrupted between steps 2 and 3, we delete just the inode.
 * In either case, the remaining objects are deleted on next mount.  From
 * a users point of view, the operation succeeded.
 */

static int write_dir(struct inode *dir, struct logfs_disk_dentry *dd,
		loff_t pos)
{
	return logfs_inode_write(dir, dd, sizeof(*dd), pos, WF_LOCK, NULL);
}

static int write_inode(struct inode *inode)
{
	return __logfs_write_inode(inode, WF_LOCK);
}

static s64 dir_seek_data(struct inode *inode, s64 pos)
{
	s64 new_pos = logfs_seek_data(inode, pos);

	return max(pos, new_pos - 1);
}

static int beyond_eof(struct inode *inode, loff_t bix)
{
	loff_t pos = bix << inode->i_sb->s_blocksize_bits;
	return pos >= i_size_read(inode);
}

/*
 * Prime value was chosen to be roughly 256 + 26.  r5 hash uses 11,
 * so short names (len <= 9) don't even occupy the complete 32bit name
 * space.  A prime >256 ensures short names quickly spread the 32bit
 * name space.  Add about 26 for the estimated amount of information
 * of each character and pick a prime nearby, preferably a bit-sparse
 * one.
 */
static u32 hash_32(const char *s, int len, u32 seed)
{
	u32 hash = seed;
	int i;

	for (i = 0; i < len; i++)
		hash = hash * 293 + s[i];
	return hash;
}

/*
 * We have to satisfy several conflicting requirements here.  Small
 * directories should stay fairly compact and not require too many
 * indirect blocks.  The number of possible locations for a given hash
 * should be small to make lookup() fast.  And we should try hard not
 * to overflow the 32bit name space or nfs and 32bit host systems will
 * be unhappy.
 *
 * So we use the following scheme.  First we reduce the hash to 0..15
 * and try a direct block.  If that is occupied we reduce the hash to
 * 16..255 and try an indirect block.  Same for 2x and 3x indirect
 * blocks.  Lastly we reduce the hash to 0x800_0000 .. 0xffff_ffff,
 * but use buckets containing eight entries instead of a single one.
 *
 * Using 16 entries should allow for a reasonable amount of hash
 * collisions, so the 32bit name space can be packed fairly tight
 * before overflowing.  Oh and currently we don't overflow but return
 * and error.
 *
 * How likely are collisions?  Doing the appropriate math is beyond me
 * and the Bronstein textbook.  But running a test program to brute
 * force collisions for a couple of days showed that on average the
 * first collision occurs after 598M entries, with 290M being the
 * smallest result.  Obviously 21 entries could already cause a
 * collision if all entries are carefully chosen.
 */
static pgoff_t hash_index(u32 hash, int round)
{
	u32 i0_blocks = I0_BLOCKS;
	u32 i1_blocks = I1_BLOCKS;
	u32 i2_blocks = I2_BLOCKS;
	u32 i3_blocks = I3_BLOCKS;

	switch (round) {
	case 0:
		return hash % i0_blocks;
	case 1:
		return i0_blocks + hash % (i1_blocks - i0_blocks);
	case 2:
		return i1_blocks + hash % (i2_blocks - i1_blocks);
	case 3:
		return i2_blocks + hash % (i3_blocks - i2_blocks);
	case 4 ... 19:
		return i3_blocks + 16 * (hash % (((1<<31) - i3_blocks) / 16))
			+ round - 4;
	}
	BUG();
}

static struct page *logfs_get_dd_page(struct inode *dir, struct dentry *dentry)
{
	struct qstr *name = &dentry->d_name;
	struct page *page;
	struct logfs_disk_dentry *dd;
	u32 hash = hash_32(name->name, name->len, 0);
	pgoff_t index;
	int round;

	if (name->len > LOGFS_MAX_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	for (round = 0; round < 20; round++) {
		index = hash_index(hash, round);

		if (beyond_eof(dir, index))
			return NULL;
		if (!logfs_exist_block(dir, index))
			continue;
		page = read_cache_page(dir->i_mapping, index,
				(filler_t *)logfs_readpage, NULL);
		if (IS_ERR(page))
			return page;
		dd = kmap_atomic(page, KM_USER0);
		BUG_ON(dd->namelen == 0);

		if (name->len != be16_to_cpu(dd->namelen) ||
				memcmp(name->name, dd->name, name->len)) {
			kunmap_atomic(dd, KM_USER0);
			page_cache_release(page);
			continue;
		}

		kunmap_atomic(dd, KM_USER0);
		return page;
	}
	return NULL;
}

static int logfs_remove_inode(struct inode *inode)
{
	int ret;

	inode->i_nlink--;
	ret = write_inode(inode);
	LOGFS_BUG_ON(ret, inode->i_sb);
	return ret;
}

static void abort_transaction(struct inode *inode, struct logfs_transaction *ta)
{
	if (logfs_inode(inode)->li_block)
		logfs_inode(inode)->li_block->ta = NULL;
	kfree(ta);
}

static int logfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct logfs_super *super = logfs_super(dir->i_sb);
	struct inode *inode = dentry->d_inode;
	struct logfs_transaction *ta;
	struct page *page;
	pgoff_t index;
	int ret;

	ta = kzalloc(sizeof(*ta), GFP_KERNEL);
	if (!ta)
		return -ENOMEM;

	ta->state = UNLINK_1;
	ta->ino = inode->i_ino;

	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;

	page = logfs_get_dd_page(dir, dentry);
	if (!page) {
		kfree(ta);
		return -ENOENT;
	}
	if (IS_ERR(page)) {
		kfree(ta);
		return PTR_ERR(page);
	}
	index = page->index;
	page_cache_release(page);

	mutex_lock(&super->s_dirop_mutex);
	logfs_add_transaction(dir, ta);

	ret = logfs_delete(dir, index, NULL);
	if (!ret)
		ret = write_inode(dir);

	if (ret) {
		abort_transaction(dir, ta);
		printk(KERN_ERR"LOGFS: unable to delete inode\n");
		goto out;
	}

	ta->state = UNLINK_2;
	logfs_add_transaction(inode, ta);
	ret = logfs_remove_inode(inode);
out:
	mutex_unlock(&super->s_dirop_mutex);
	return ret;
}

static inline int logfs_empty_dir(struct inode *dir)
{
	u64 data;

	data = logfs_seek_data(dir, 0) << dir->i_sb->s_blocksize_bits;
	return data >= i_size_read(dir);
}

static int logfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	if (!logfs_empty_dir(inode))
		return -ENOTEMPTY;

	return logfs_unlink(dir, dentry);
}

/* FIXME: readdir currently has it's own dir_walk code.  I don't see a good
 * way to combine the two copies */
#define IMPLICIT_NODES 2
static int __logfs_readdir(struct file *file, void *buf, filldir_t filldir)
{
	struct inode *dir = file->f_dentry->d_inode;
	loff_t pos = file->f_pos - IMPLICIT_NODES;
	struct page *page;
	struct logfs_disk_dentry *dd;
	int full;

	BUG_ON(pos < 0);
	for (;; pos++) {
		if (beyond_eof(dir, pos))
			break;
		if (!logfs_exist_block(dir, pos)) {
			/* deleted dentry */
			pos = dir_seek_data(dir, pos);
			continue;
		}
		page = read_cache_page(dir->i_mapping, pos,
				(filler_t *)logfs_readpage, NULL);
		if (IS_ERR(page))
			return PTR_ERR(page);
		dd = kmap(page);
		BUG_ON(dd->namelen == 0);

		full = filldir(buf, (char *)dd->name, be16_to_cpu(dd->namelen),
				pos, be64_to_cpu(dd->ino), dd->type);
		kunmap(page);
		page_cache_release(page);
		if (full)
			break;
	}

	file->f_pos = pos + IMPLICIT_NODES;
	return 0;
}

static int logfs_readdir(struct file *file, void *buf, filldir_t filldir)
{
	struct inode *inode = file->f_dentry->d_inode;
	ino_t pino = parent_ino(file->f_dentry);
	int err;

	if (file->f_pos < 0)
		return -EINVAL;

	if (file->f_pos == 0) {
		if (filldir(buf, ".", 1, 1, inode->i_ino, DT_DIR) < 0)
			return 0;
		file->f_pos++;
	}
	if (file->f_pos == 1) {
		if (filldir(buf, "..", 2, 2, pino, DT_DIR) < 0)
			return 0;
		file->f_pos++;
	}

	err = __logfs_readdir(file, buf, filldir);
	return err;
}

static void logfs_set_name(struct logfs_disk_dentry *dd, struct qstr *name)
{
	dd->namelen = cpu_to_be16(name->len);
	memcpy(dd->name, name->name, name->len);
}

static struct dentry *logfs_lookup(struct inode *dir, struct dentry *dentry,
		struct nameidata *nd)
{
	struct page *page;
	struct logfs_disk_dentry *dd;
	pgoff_t index;
	u64 ino = 0;
	struct inode *inode;

	page = logfs_get_dd_page(dir, dentry);
	if (IS_ERR(page))
		return ERR_CAST(page);
	if (!page) {
		d_add(dentry, NULL);
		return NULL;
	}
	index = page->index;
	dd = kmap_atomic(page, KM_USER0);
	ino = be64_to_cpu(dd->ino);
	kunmap_atomic(dd, KM_USER0);
	page_cache_release(page);

	inode = logfs_iget(dir->i_sb, ino);
	if (IS_ERR(inode))
		printk(KERN_ERR"LogFS: Cannot read inode #%llx for dentry (%lx, %lx)n",
				ino, dir->i_ino, index);
	return d_splice_alias(inode, dentry);
}

static void grow_dir(struct inode *dir, loff_t index)
{
	index = (index + 1) << dir->i_sb->s_blocksize_bits;
	if (i_size_read(dir) < index)
		i_size_write(dir, index);
}

static int logfs_write_dir(struct inode *dir, struct dentry *dentry,
		struct inode *inode)
{
	struct page *page;
	struct logfs_disk_dentry *dd;
	u32 hash = hash_32(dentry->d_name.name, dentry->d_name.len, 0);
	pgoff_t index;
	int round, err;

	for (round = 0; round < 20; round++) {
		index = hash_index(hash, round);

		if (logfs_exist_block(dir, index))
			continue;
		page = find_or_create_page(dir->i_mapping, index, GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		dd = kmap_atomic(page, KM_USER0);
		memset(dd, 0, sizeof(*dd));
		dd->ino = cpu_to_be64(inode->i_ino);
		dd->type = logfs_type(inode);
		logfs_set_name(dd, &dentry->d_name);
		kunmap_atomic(dd, KM_USER0);

		err = logfs_write_buf(dir, page, WF_LOCK);
		unlock_page(page);
		page_cache_release(page);
		if (!err)
			grow_dir(dir, index);
		return err;
	}
	/* FIXME: Is there a better return value?  In most cases neither
	 * the filesystem nor the directory are full.  But we have had
	 * too many collisions for this particular hash and no fallback.
	 */
	return -ENOSPC;
}

static int __logfs_create(struct inode *dir, struct dentry *dentry,
		struct inode *inode, const char *dest, long destlen)
{
	struct logfs_super *super = logfs_super(dir->i_sb);
	struct logfs_inode *li = logfs_inode(inode);
	struct logfs_transaction *ta;
	int ret;

	ta = kzalloc(sizeof(*ta), GFP_KERNEL);
	if (!ta) {
		inode->i_nlink--;
		iput(inode);
		return -ENOMEM;
	}

	ta->state = CREATE_1;
	ta->ino = inode->i_ino;
	mutex_lock(&super->s_dirop_mutex);
	logfs_add_transaction(inode, ta);

	if (dest) {
		/* symlink */
		ret = logfs_inode_write(inode, dest, destlen, 0, WF_LOCK, NULL);
		if (!ret)
			ret = write_inode(inode);
	} else {
		/* creat/mkdir/mknod */
		ret = write_inode(inode);
	}
	if (ret) {
		abort_transaction(inode, ta);
		li->li_flags |= LOGFS_IF_STILLBORN;
		/* FIXME: truncate symlink */
		inode->i_nlink--;
		iput(inode);
		goto out;
	}

	ta->state = CREATE_2;
	logfs_add_transaction(dir, ta);
	ret = logfs_write_dir(dir, dentry, inode);
	/* sync directory */
	if (!ret)
		ret = write_inode(dir);

	if (ret) {
		logfs_del_transaction(dir, ta);
		ta->state = CREATE_2;
		logfs_add_transaction(inode, ta);
		logfs_remove_inode(inode);
		iput(inode);
		goto out;
	}
	d_instantiate(dentry, inode);
out:
	mutex_unlock(&super->s_dirop_mutex);
	return ret;
}

static int logfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode;

	/*
	 * FIXME: why do we have to fill in S_IFDIR, while the mode is
	 * correct for mknod, creat, etc.?  Smells like the vfs *should*
	 * do it for us but for some reason fails to do so.
	 */
	inode = logfs_new_inode(dir, S_IFDIR | mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &logfs_dir_iops;
	inode->i_fop = &logfs_dir_fops;

	return __logfs_create(dir, dentry, inode, NULL, 0);
}

static int logfs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	struct inode *inode;

	inode = logfs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &logfs_reg_iops;
	inode->i_fop = &logfs_reg_fops;
	inode->i_mapping->a_ops = &logfs_reg_aops;

	return __logfs_create(dir, dentry, inode, NULL, 0);
}

static int logfs_mknod(struct inode *dir, struct dentry *dentry, int mode,
		dev_t rdev)
{
	struct inode *inode;

	if (dentry->d_name.len > LOGFS_MAX_NAMELEN)
		return -ENAMETOOLONG;

	inode = logfs_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	init_special_inode(inode, mode, rdev);

	return __logfs_create(dir, dentry, inode, NULL, 0);
}

static int logfs_symlink(struct inode *dir, struct dentry *dentry,
		const char *target)
{
	struct inode *inode;
	size_t destlen = strlen(target) + 1;

	if (destlen > dir->i_sb->s_blocksize)
		return -ENAMETOOLONG;

	inode = logfs_new_inode(dir, S_IFLNK | 0777);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &logfs_symlink_iops;
	inode->i_mapping->a_ops = &logfs_reg_aops;

	return __logfs_create(dir, dentry, inode, target, destlen);
}

static int logfs_link(struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	if (inode->i_nlink >= LOGFS_LINK_MAX)
		return -EMLINK;

	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	ihold(inode);
	inode->i_nlink++;
	mark_inode_dirty_sync(inode);

	return __logfs_create(dir, dentry, inode, NULL, 0);
}

static int logfs_get_dd(struct inode *dir, struct dentry *dentry,
		struct logfs_disk_dentry *dd, loff_t *pos)
{
	struct page *page;
	void *map;

	page = logfs_get_dd_page(dir, dentry);
	if (IS_ERR(page))
		return PTR_ERR(page);
	*pos = page->index;
	map = kmap_atomic(page, KM_USER0);
	memcpy(dd, map, sizeof(*dd));
	kunmap_atomic(map, KM_USER0);
	page_cache_release(page);
	return 0;
}

static int logfs_delete_dd(struct inode *dir, loff_t pos)
{
	/*
	 * Getting called with pos somewhere beyond eof is either a goofup
	 * within this file or means someone maliciously edited the
	 * (crc-protected) journal.
	 */
	BUG_ON(beyond_eof(dir, pos));
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	log_dir(" Delete dentry (%lx, %llx)\n", dir->i_ino, pos);
	return logfs_delete(dir, pos, NULL);
}

/*
 * Cross-directory rename, target does not exist.  Just a little nasty.
 * Create a new dentry in the target dir, then remove the old dentry,
 * all the while taking care to remember our operation in the journal.
 */
static int logfs_rename_cross(struct inode *old_dir, struct dentry *old_dentry,
			      struct inode *new_dir, struct dentry *new_dentry)
{
	struct logfs_super *super = logfs_super(old_dir->i_sb);
	struct logfs_disk_dentry dd;
	struct logfs_transaction *ta;
	loff_t pos;
	int err;

	/* 1. locate source dd */
	err = logfs_get_dd(old_dir, old_dentry, &dd, &pos);
	if (err)
		return err;

	ta = kzalloc(sizeof(*ta), GFP_KERNEL);
	if (!ta)
		return -ENOMEM;

	ta->state = CROSS_RENAME_1;
	ta->dir = old_dir->i_ino;
	ta->pos = pos;

	/* 2. write target dd */
	mutex_lock(&super->s_dirop_mutex);
	logfs_add_transaction(new_dir, ta);
	err = logfs_write_dir(new_dir, new_dentry, old_dentry->d_inode);
	if (!err)
		err = write_inode(new_dir);

	if (err) {
		super->s_rename_dir = 0;
		super->s_rename_pos = 0;
		abort_transaction(new_dir, ta);
		goto out;
	}

	/* 3. remove source dd */
	ta->state = CROSS_RENAME_2;
	logfs_add_transaction(old_dir, ta);
	err = logfs_delete_dd(old_dir, pos);
	if (!err)
		err = write_inode(old_dir);
	LOGFS_BUG_ON(err, old_dir->i_sb);
out:
	mutex_unlock(&super->s_dirop_mutex);
	return err;
}

static int logfs_replace_inode(struct inode *dir, struct dentry *dentry,
		struct logfs_disk_dentry *dd, struct inode *inode)
{
	loff_t pos;
	int err;

	err = logfs_get_dd(dir, dentry, dd, &pos);
	if (err)
		return err;
	dd->ino = cpu_to_be64(inode->i_ino);
	dd->type = logfs_type(inode);

	err = write_dir(dir, dd, pos);
	if (err)
		return err;
	log_dir("Replace dentry (%lx, %llx) %s -> %llx\n", dir->i_ino, pos,
			dd->name, be64_to_cpu(dd->ino));
	return write_inode(dir);
}

/* Target dentry exists - the worst case.  We need to attach the source
 * inode to the target dentry, then remove the orphaned target inode and
 * source dentry.
 */
static int logfs_rename_target(struct inode *old_dir, struct dentry *old_dentry,
			       struct inode *new_dir, struct dentry *new_dentry)
{
	struct logfs_super *super = logfs_super(old_dir->i_sb);
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	int isdir = S_ISDIR(old_inode->i_mode);
	struct logfs_disk_dentry dd;
	struct logfs_transaction *ta;
	loff_t pos;
	int err;

	BUG_ON(isdir != S_ISDIR(new_inode->i_mode));
	if (isdir) {
		if (!logfs_empty_dir(new_inode))
			return -ENOTEMPTY;
	}

	/* 1. locate source dd */
	err = logfs_get_dd(old_dir, old_dentry, &dd, &pos);
	if (err)
		return err;

	ta = kzalloc(sizeof(*ta), GFP_KERNEL);
	if (!ta)
		return -ENOMEM;

	ta->state = TARGET_RENAME_1;
	ta->dir = old_dir->i_ino;
	ta->pos = pos;
	ta->ino = new_inode->i_ino;

	/* 2. attach source inode to target dd */
	mutex_lock(&super->s_dirop_mutex);
	logfs_add_transaction(new_dir, ta);
	err = logfs_replace_inode(new_dir, new_dentry, &dd, old_inode);
	if (err) {
		super->s_rename_dir = 0;
		super->s_rename_pos = 0;
		super->s_victim_ino = 0;
		abort_transaction(new_dir, ta);
		goto out;
	}

	/* 3. remove source dd */
	ta->state = TARGET_RENAME_2;
	logfs_add_transaction(old_dir, ta);
	err = logfs_delete_dd(old_dir, pos);
	if (!err)
		err = write_inode(old_dir);
	LOGFS_BUG_ON(err, old_dir->i_sb);

	/* 4. remove target inode */
	ta->state = TARGET_RENAME_3;
	logfs_add_transaction(new_inode, ta);
	err = logfs_remove_inode(new_inode);

out:
	mutex_unlock(&super->s_dirop_mutex);
	return err;
}

static int logfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	if (new_dentry->d_inode)
		return logfs_rename_target(old_dir, old_dentry,
					   new_dir, new_dentry);
	return logfs_rename_cross(old_dir, old_dentry, new_dir, new_dentry);
}

/* No locking done here, as this is called before .get_sb() returns. */
int logfs_replay_journal(struct super_block *sb)
{
	struct logfs_super *super = logfs_super(sb);
	struct inode *inode;
	u64 ino, pos;
	int err;

	if (super->s_victim_ino) {
		/* delete victim inode */
		ino = super->s_victim_ino;
		printk(KERN_INFO"LogFS: delete unmapped inode #%llx\n", ino);
		inode = logfs_iget(sb, ino);
		if (IS_ERR(inode))
			goto fail;

		LOGFS_BUG_ON(i_size_read(inode) > 0, sb);
		super->s_victim_ino = 0;
		err = logfs_remove_inode(inode);
		iput(inode);
		if (err) {
			super->s_victim_ino = ino;
			goto fail;
		}
	}
	if (super->s_rename_dir) {
		/* delete old dd from rename */
		ino = super->s_rename_dir;
		pos = super->s_rename_pos;
		printk(KERN_INFO"LogFS: delete unbacked dentry (%llx, %llx)\n",
				ino, pos);
		inode = logfs_iget(sb, ino);
		if (IS_ERR(inode))
			goto fail;

		super->s_rename_dir = 0;
		super->s_rename_pos = 0;
		err = logfs_delete_dd(inode, pos);
		iput(inode);
		if (err) {
			super->s_rename_dir = ino;
			super->s_rename_pos = pos;
			goto fail;
		}
	}
	return 0;
fail:
	LOGFS_BUG(sb);
	return -EIO;
}

const struct inode_operations logfs_symlink_iops = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
};

const struct inode_operations logfs_dir_iops = {
	.create		= logfs_create,
	.link		= logfs_link,
	.lookup		= logfs_lookup,
	.mkdir		= logfs_mkdir,
	.mknod		= logfs_mknod,
	.rename		= logfs_rename,
	.rmdir		= logfs_rmdir,
	.symlink	= logfs_symlink,
	.unlink		= logfs_unlink,
};
const struct file_operations logfs_dir_fops = {
	.fsync		= logfs_fsync,
	.unlocked_ioctl	= logfs_ioctl,
	.readdir	= logfs_readdir,
	.read		= generic_read_dir,
	.llseek		= default_llseek,
};
