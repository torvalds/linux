/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 * Copyright © 2004-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/crc32.h>
#include <linux/jffs2.h>
#include "jffs2_fs_i.h"
#include "jffs2_fs_sb.h"
#include <linux/time.h>
#include "yesdelist.h"

static int jffs2_readdir (struct file *, struct dir_context *);

static int jffs2_create (struct iyesde *,struct dentry *,umode_t,
			 bool);
static struct dentry *jffs2_lookup (struct iyesde *,struct dentry *,
				    unsigned int);
static int jffs2_link (struct dentry *,struct iyesde *,struct dentry *);
static int jffs2_unlink (struct iyesde *,struct dentry *);
static int jffs2_symlink (struct iyesde *,struct dentry *,const char *);
static int jffs2_mkdir (struct iyesde *,struct dentry *,umode_t);
static int jffs2_rmdir (struct iyesde *,struct dentry *);
static int jffs2_mkyesd (struct iyesde *,struct dentry *,umode_t,dev_t);
static int jffs2_rename (struct iyesde *, struct dentry *,
			 struct iyesde *, struct dentry *,
			 unsigned int);

const struct file_operations jffs2_dir_operations =
{
	.read =		generic_read_dir,
	.iterate_shared=jffs2_readdir,
	.unlocked_ioctl=jffs2_ioctl,
	.fsync =	jffs2_fsync,
	.llseek =	generic_file_llseek,
};


const struct iyesde_operations jffs2_dir_iyesde_operations =
{
	.create =	jffs2_create,
	.lookup =	jffs2_lookup,
	.link =		jffs2_link,
	.unlink =	jffs2_unlink,
	.symlink =	jffs2_symlink,
	.mkdir =	jffs2_mkdir,
	.rmdir =	jffs2_rmdir,
	.mkyesd =	jffs2_mkyesd,
	.rename =	jffs2_rename,
	.get_acl =	jffs2_get_acl,
	.set_acl =	jffs2_set_acl,
	.setattr =	jffs2_setattr,
	.listxattr =	jffs2_listxattr,
};

/***********************************************************************/


/* We keep the dirent list sorted in increasing order of name hash,
   and we use the same hash function as the dentries. Makes this
   nice and simple
*/
static struct dentry *jffs2_lookup(struct iyesde *dir_i, struct dentry *target,
				   unsigned int flags)
{
	struct jffs2_iyesde_info *dir_f;
	struct jffs2_full_dirent *fd = NULL, *fd_list;
	uint32_t iyes = 0;
	struct iyesde *iyesde = NULL;
	unsigned int nhash;

	jffs2_dbg(1, "jffs2_lookup()\n");

	if (target->d_name.len > JFFS2_MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	dir_f = JFFS2_INODE_INFO(dir_i);

	/* The 'nhash' on the fd_list is yest the same as the dentry hash */
	nhash = full_name_hash(NULL, target->d_name.name, target->d_name.len);

	mutex_lock(&dir_f->sem);

	/* NB: The 2.2 backport will need to explicitly check for '.' and '..' here */
	for (fd_list = dir_f->dents; fd_list && fd_list->nhash <= nhash; fd_list = fd_list->next) {
		if (fd_list->nhash == nhash &&
		    (!fd || fd_list->version > fd->version) &&
		    strlen(fd_list->name) == target->d_name.len &&
		    !strncmp(fd_list->name, target->d_name.name, target->d_name.len)) {
			fd = fd_list;
		}
	}
	if (fd)
		iyes = fd->iyes;
	mutex_unlock(&dir_f->sem);
	if (iyes) {
		iyesde = jffs2_iget(dir_i->i_sb, iyes);
		if (IS_ERR(iyesde))
			pr_warn("iget() failed for iyes #%u\n", iyes);
	}

	return d_splice_alias(iyesde, target);
}

/***********************************************************************/


static int jffs2_readdir(struct file *file, struct dir_context *ctx)
{
	struct iyesde *iyesde = file_iyesde(file);
	struct jffs2_iyesde_info *f = JFFS2_INODE_INFO(iyesde);
	struct jffs2_full_dirent *fd;
	unsigned long curofs = 1;

	jffs2_dbg(1, "jffs2_readdir() for dir_i #%lu\n", iyesde->i_iyes);

	if (!dir_emit_dots(file, ctx))
		return 0;

	mutex_lock(&f->sem);
	for (fd = f->dents; fd; fd = fd->next) {
		curofs++;
		/* First loop: curofs = 2; pos = 2 */
		if (curofs < ctx->pos) {
			jffs2_dbg(2, "Skipping dirent: \"%s\", iyes #%u, type %d, because curofs %ld < offset %ld\n",
				  fd->name, fd->iyes, fd->type, curofs, (unsigned long)ctx->pos);
			continue;
		}
		if (!fd->iyes) {
			jffs2_dbg(2, "Skipping deletion dirent \"%s\"\n",
				  fd->name);
			ctx->pos++;
			continue;
		}
		jffs2_dbg(2, "Dirent %ld: \"%s\", iyes #%u, type %d\n",
			  (unsigned long)ctx->pos, fd->name, fd->iyes, fd->type);
		if (!dir_emit(ctx, fd->name, strlen(fd->name), fd->iyes, fd->type))
			break;
		ctx->pos++;
	}
	mutex_unlock(&f->sem);
	return 0;
}

/***********************************************************************/


static int jffs2_create(struct iyesde *dir_i, struct dentry *dentry,
			umode_t mode, bool excl)
{
	struct jffs2_raw_iyesde *ri;
	struct jffs2_iyesde_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct iyesde *iyesde;
	int ret;

	ri = jffs2_alloc_raw_iyesde();
	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	jffs2_dbg(1, "%s()\n", __func__);

	iyesde = jffs2_new_iyesde(dir_i, mode, ri);

	if (IS_ERR(iyesde)) {
		jffs2_dbg(1, "jffs2_new_iyesde() failed\n");
		jffs2_free_raw_iyesde(ri);
		return PTR_ERR(iyesde);
	}

	iyesde->i_op = &jffs2_file_iyesde_operations;
	iyesde->i_fop = &jffs2_file_operations;
	iyesde->i_mapping->a_ops = &jffs2_file_address_operations;
	iyesde->i_mapping->nrpages = 0;

	f = JFFS2_INODE_INFO(iyesde);
	dir_f = JFFS2_INODE_INFO(dir_i);

	/* jffs2_do_create() will want to lock it, _after_ reserving
	   space and taking c-alloc_sem. If we keep it locked here,
	   lockdep gets unhappy (although it's a false positive;
	   yesthing else will be looking at this iyesde yet so there's
	   yes chance of AB-BA deadlock involving its f->sem). */
	mutex_unlock(&f->sem);

	ret = jffs2_do_create(c, dir_f, f, ri, &dentry->d_name);
	if (ret)
		goto fail;

	dir_i->i_mtime = dir_i->i_ctime = ITIME(je32_to_cpu(ri->ctime));

	jffs2_free_raw_iyesde(ri);

	jffs2_dbg(1, "%s(): Created iyes #%lu with mode %o, nlink %d(%d). nrpages %ld\n",
		  __func__, iyesde->i_iyes, iyesde->i_mode, iyesde->i_nlink,
		  f->iyescache->piyes_nlink, iyesde->i_mapping->nrpages);

	d_instantiate_new(dentry, iyesde);
	return 0;

 fail:
	iget_failed(iyesde);
	jffs2_free_raw_iyesde(ri);
	return ret;
}

/***********************************************************************/


static int jffs2_unlink(struct iyesde *dir_i, struct dentry *dentry)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(dir_i->i_sb);
	struct jffs2_iyesde_info *dir_f = JFFS2_INODE_INFO(dir_i);
	struct jffs2_iyesde_info *dead_f = JFFS2_INODE_INFO(d_iyesde(dentry));
	int ret;
	uint32_t yesw = JFFS2_NOW();

	ret = jffs2_do_unlink(c, dir_f, dentry->d_name.name,
			      dentry->d_name.len, dead_f, yesw);
	if (dead_f->iyescache)
		set_nlink(d_iyesde(dentry), dead_f->iyescache->piyes_nlink);
	if (!ret)
		dir_i->i_mtime = dir_i->i_ctime = ITIME(yesw);
	return ret;
}
/***********************************************************************/


static int jffs2_link (struct dentry *old_dentry, struct iyesde *dir_i, struct dentry *dentry)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(old_dentry->d_sb);
	struct jffs2_iyesde_info *f = JFFS2_INODE_INFO(d_iyesde(old_dentry));
	struct jffs2_iyesde_info *dir_f = JFFS2_INODE_INFO(dir_i);
	int ret;
	uint8_t type;
	uint32_t yesw;

	/* Don't let people make hard links to bad iyesdes. */
	if (!f->iyescache)
		return -EIO;

	if (d_is_dir(old_dentry))
		return -EPERM;

	/* XXX: This is ugly */
	type = (d_iyesde(old_dentry)->i_mode & S_IFMT) >> 12;
	if (!type) type = DT_REG;

	yesw = JFFS2_NOW();
	ret = jffs2_do_link(c, dir_f, f->iyescache->iyes, type, dentry->d_name.name, dentry->d_name.len, yesw);

	if (!ret) {
		mutex_lock(&f->sem);
		set_nlink(d_iyesde(old_dentry), ++f->iyescache->piyes_nlink);
		mutex_unlock(&f->sem);
		d_instantiate(dentry, d_iyesde(old_dentry));
		dir_i->i_mtime = dir_i->i_ctime = ITIME(yesw);
		ihold(d_iyesde(old_dentry));
	}
	return ret;
}

/***********************************************************************/

static int jffs2_symlink (struct iyesde *dir_i, struct dentry *dentry, const char *target)
{
	struct jffs2_iyesde_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct iyesde *iyesde;
	struct jffs2_raw_iyesde *ri;
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dyesde *fn;
	struct jffs2_full_dirent *fd;
	int namelen;
	uint32_t alloclen;
	int ret, targetlen = strlen(target);

	/* FIXME: If you care. We'd need to use frags for the target
	   if it grows much more than this */
	if (targetlen > 254)
		return -ENAMETOOLONG;

	ri = jffs2_alloc_raw_iyesde();

	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	/* Try to reserve eyesugh space for both yesde and dirent.
	 * Just the yesde will do for yesw, though
	 */
	namelen = dentry->d_name.len;
	ret = jffs2_reserve_space(c, sizeof(*ri) + targetlen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);

	if (ret) {
		jffs2_free_raw_iyesde(ri);
		return ret;
	}

	iyesde = jffs2_new_iyesde(dir_i, S_IFLNK | S_IRWXUGO, ri);

	if (IS_ERR(iyesde)) {
		jffs2_free_raw_iyesde(ri);
		jffs2_complete_reservation(c);
		return PTR_ERR(iyesde);
	}

	iyesde->i_op = &jffs2_symlink_iyesde_operations;

	f = JFFS2_INODE_INFO(iyesde);

	iyesde->i_size = targetlen;
	ri->isize = ri->dsize = ri->csize = cpu_to_je32(iyesde->i_size);
	ri->totlen = cpu_to_je32(sizeof(*ri) + iyesde->i_size);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unkyeswn_yesde)-4));

	ri->compr = JFFS2_COMPR_NONE;
	ri->data_crc = cpu_to_je32(crc32(0, target, targetlen));
	ri->yesde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));

	fn = jffs2_write_dyesde(c, f, ri, target, targetlen, ALLOC_NORMAL);

	jffs2_free_raw_iyesde(ri);

	if (IS_ERR(fn)) {
		/* Eeek. Wave bye bye */
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		ret = PTR_ERR(fn);
		goto fail;
	}

	/* We use f->target field to store the target path. */
	f->target = kmemdup(target, targetlen + 1, GFP_KERNEL);
	if (!f->target) {
		pr_warn("Can't allocate %d bytes of memory\n", targetlen + 1);
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		ret = -ENOMEM;
		goto fail;
	}
	iyesde->i_link = f->target;

	jffs2_dbg(1, "%s(): symlink's target '%s' cached\n",
		  __func__, (char *)f->target);

	/* No data here. Only a metadata yesde, which will be
	   obsoleted by the first data write
	*/
	f->metadata = fn;
	mutex_unlock(&f->sem);

	jffs2_complete_reservation(c);

	ret = jffs2_init_security(iyesde, dir_i, &dentry->d_name);
	if (ret)
		goto fail;

	ret = jffs2_init_acl_post(iyesde);
	if (ret)
		goto fail;

	ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
	if (ret)
		goto fail;

	rd = jffs2_alloc_raw_dirent();
	if (!rd) {
		/* Argh. Now we treat it like a yesrmal delete */
		jffs2_complete_reservation(c);
		ret = -ENOMEM;
		goto fail;
	}

	dir_f = JFFS2_INODE_INFO(dir_i);
	mutex_lock(&dir_f->sem);

	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->yesdetype = cpu_to_je16(JFFS2_NODETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unkyeswn_yesde)-4));

	rd->piyes = cpu_to_je32(dir_i->i_iyes);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->iyes = cpu_to_je32(iyesde->i_iyes);
	rd->mctime = cpu_to_je32(JFFS2_NOW());
	rd->nsize = namelen;
	rd->type = DT_LNK;
	rd->yesde_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, dentry->d_name.name, namelen));

	fd = jffs2_write_dirent(c, dir_f, rd, dentry->d_name.name, namelen, ALLOC_NORMAL);

	if (IS_ERR(fd)) {
		/* dirent failed to write. Delete the iyesde yesrmally
		   as if it were the final unlink() */
		jffs2_complete_reservation(c);
		jffs2_free_raw_dirent(rd);
		mutex_unlock(&dir_f->sem);
		ret = PTR_ERR(fd);
		goto fail;
	}

	dir_i->i_mtime = dir_i->i_ctime = ITIME(je32_to_cpu(rd->mctime));

	jffs2_free_raw_dirent(rd);

	/* Link the fd into the iyesde's list, obsoleting an old
	   one if necessary. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	mutex_unlock(&dir_f->sem);
	jffs2_complete_reservation(c);

	d_instantiate_new(dentry, iyesde);
	return 0;

 fail:
	iget_failed(iyesde);
	return ret;
}


static int jffs2_mkdir (struct iyesde *dir_i, struct dentry *dentry, umode_t mode)
{
	struct jffs2_iyesde_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct iyesde *iyesde;
	struct jffs2_raw_iyesde *ri;
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dyesde *fn;
	struct jffs2_full_dirent *fd;
	int namelen;
	uint32_t alloclen;
	int ret;

	mode |= S_IFDIR;

	ri = jffs2_alloc_raw_iyesde();
	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	/* Try to reserve eyesugh space for both yesde and dirent.
	 * Just the yesde will do for yesw, though
	 */
	namelen = dentry->d_name.len;
	ret = jffs2_reserve_space(c, sizeof(*ri), &alloclen, ALLOC_NORMAL,
				  JFFS2_SUMMARY_INODE_SIZE);

	if (ret) {
		jffs2_free_raw_iyesde(ri);
		return ret;
	}

	iyesde = jffs2_new_iyesde(dir_i, mode, ri);

	if (IS_ERR(iyesde)) {
		jffs2_free_raw_iyesde(ri);
		jffs2_complete_reservation(c);
		return PTR_ERR(iyesde);
	}

	iyesde->i_op = &jffs2_dir_iyesde_operations;
	iyesde->i_fop = &jffs2_dir_operations;

	f = JFFS2_INODE_INFO(iyesde);

	/* Directories get nlink 2 at start */
	set_nlink(iyesde, 2);
	/* but ic->piyes_nlink is the parent iyes# */
	f->iyescache->piyes_nlink = dir_i->i_iyes;

	ri->data_crc = cpu_to_je32(0);
	ri->yesde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));

	fn = jffs2_write_dyesde(c, f, ri, NULL, 0, ALLOC_NORMAL);

	jffs2_free_raw_iyesde(ri);

	if (IS_ERR(fn)) {
		/* Eeek. Wave bye bye */
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		ret = PTR_ERR(fn);
		goto fail;
	}
	/* No data here. Only a metadata yesde, which will be
	   obsoleted by the first data write
	*/
	f->metadata = fn;
	mutex_unlock(&f->sem);

	jffs2_complete_reservation(c);

	ret = jffs2_init_security(iyesde, dir_i, &dentry->d_name);
	if (ret)
		goto fail;

	ret = jffs2_init_acl_post(iyesde);
	if (ret)
		goto fail;

	ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
	if (ret)
		goto fail;

	rd = jffs2_alloc_raw_dirent();
	if (!rd) {
		/* Argh. Now we treat it like a yesrmal delete */
		jffs2_complete_reservation(c);
		ret = -ENOMEM;
		goto fail;
	}

	dir_f = JFFS2_INODE_INFO(dir_i);
	mutex_lock(&dir_f->sem);

	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->yesdetype = cpu_to_je16(JFFS2_NODETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unkyeswn_yesde)-4));

	rd->piyes = cpu_to_je32(dir_i->i_iyes);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->iyes = cpu_to_je32(iyesde->i_iyes);
	rd->mctime = cpu_to_je32(JFFS2_NOW());
	rd->nsize = namelen;
	rd->type = DT_DIR;
	rd->yesde_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, dentry->d_name.name, namelen));

	fd = jffs2_write_dirent(c, dir_f, rd, dentry->d_name.name, namelen, ALLOC_NORMAL);

	if (IS_ERR(fd)) {
		/* dirent failed to write. Delete the iyesde yesrmally
		   as if it were the final unlink() */
		jffs2_complete_reservation(c);
		jffs2_free_raw_dirent(rd);
		mutex_unlock(&dir_f->sem);
		ret = PTR_ERR(fd);
		goto fail;
	}

	dir_i->i_mtime = dir_i->i_ctime = ITIME(je32_to_cpu(rd->mctime));
	inc_nlink(dir_i);

	jffs2_free_raw_dirent(rd);

	/* Link the fd into the iyesde's list, obsoleting an old
	   one if necessary. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	mutex_unlock(&dir_f->sem);
	jffs2_complete_reservation(c);

	d_instantiate_new(dentry, iyesde);
	return 0;

 fail:
	iget_failed(iyesde);
	return ret;
}

static int jffs2_rmdir (struct iyesde *dir_i, struct dentry *dentry)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(dir_i->i_sb);
	struct jffs2_iyesde_info *dir_f = JFFS2_INODE_INFO(dir_i);
	struct jffs2_iyesde_info *f = JFFS2_INODE_INFO(d_iyesde(dentry));
	struct jffs2_full_dirent *fd;
	int ret;
	uint32_t yesw = JFFS2_NOW();

	for (fd = f->dents ; fd; fd = fd->next) {
		if (fd->iyes)
			return -ENOTEMPTY;
	}

	ret = jffs2_do_unlink(c, dir_f, dentry->d_name.name,
			      dentry->d_name.len, f, yesw);
	if (!ret) {
		dir_i->i_mtime = dir_i->i_ctime = ITIME(yesw);
		clear_nlink(d_iyesde(dentry));
		drop_nlink(dir_i);
	}
	return ret;
}

static int jffs2_mkyesd (struct iyesde *dir_i, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct jffs2_iyesde_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct iyesde *iyesde;
	struct jffs2_raw_iyesde *ri;
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dyesde *fn;
	struct jffs2_full_dirent *fd;
	int namelen;
	union jffs2_device_yesde dev;
	int devlen = 0;
	uint32_t alloclen;
	int ret;

	ri = jffs2_alloc_raw_iyesde();
	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	if (S_ISBLK(mode) || S_ISCHR(mode))
		devlen = jffs2_encode_dev(&dev, rdev);

	/* Try to reserve eyesugh space for both yesde and dirent.
	 * Just the yesde will do for yesw, though
	 */
	namelen = dentry->d_name.len;
	ret = jffs2_reserve_space(c, sizeof(*ri) + devlen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);

	if (ret) {
		jffs2_free_raw_iyesde(ri);
		return ret;
	}

	iyesde = jffs2_new_iyesde(dir_i, mode, ri);

	if (IS_ERR(iyesde)) {
		jffs2_free_raw_iyesde(ri);
		jffs2_complete_reservation(c);
		return PTR_ERR(iyesde);
	}
	iyesde->i_op = &jffs2_file_iyesde_operations;
	init_special_iyesde(iyesde, iyesde->i_mode, rdev);

	f = JFFS2_INODE_INFO(iyesde);

	ri->dsize = ri->csize = cpu_to_je32(devlen);
	ri->totlen = cpu_to_je32(sizeof(*ri) + devlen);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unkyeswn_yesde)-4));

	ri->compr = JFFS2_COMPR_NONE;
	ri->data_crc = cpu_to_je32(crc32(0, &dev, devlen));
	ri->yesde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));

	fn = jffs2_write_dyesde(c, f, ri, (char *)&dev, devlen, ALLOC_NORMAL);

	jffs2_free_raw_iyesde(ri);

	if (IS_ERR(fn)) {
		/* Eeek. Wave bye bye */
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		ret = PTR_ERR(fn);
		goto fail;
	}
	/* No data here. Only a metadata yesde, which will be
	   obsoleted by the first data write
	*/
	f->metadata = fn;
	mutex_unlock(&f->sem);

	jffs2_complete_reservation(c);

	ret = jffs2_init_security(iyesde, dir_i, &dentry->d_name);
	if (ret)
		goto fail;

	ret = jffs2_init_acl_post(iyesde);
	if (ret)
		goto fail;

	ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
	if (ret)
		goto fail;

	rd = jffs2_alloc_raw_dirent();
	if (!rd) {
		/* Argh. Now we treat it like a yesrmal delete */
		jffs2_complete_reservation(c);
		ret = -ENOMEM;
		goto fail;
	}

	dir_f = JFFS2_INODE_INFO(dir_i);
	mutex_lock(&dir_f->sem);

	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->yesdetype = cpu_to_je16(JFFS2_NODETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unkyeswn_yesde)-4));

	rd->piyes = cpu_to_je32(dir_i->i_iyes);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->iyes = cpu_to_je32(iyesde->i_iyes);
	rd->mctime = cpu_to_je32(JFFS2_NOW());
	rd->nsize = namelen;

	/* XXX: This is ugly. */
	rd->type = (mode & S_IFMT) >> 12;

	rd->yesde_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, dentry->d_name.name, namelen));

	fd = jffs2_write_dirent(c, dir_f, rd, dentry->d_name.name, namelen, ALLOC_NORMAL);

	if (IS_ERR(fd)) {
		/* dirent failed to write. Delete the iyesde yesrmally
		   as if it were the final unlink() */
		jffs2_complete_reservation(c);
		jffs2_free_raw_dirent(rd);
		mutex_unlock(&dir_f->sem);
		ret = PTR_ERR(fd);
		goto fail;
	}

	dir_i->i_mtime = dir_i->i_ctime = ITIME(je32_to_cpu(rd->mctime));

	jffs2_free_raw_dirent(rd);

	/* Link the fd into the iyesde's list, obsoleting an old
	   one if necessary. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	mutex_unlock(&dir_f->sem);
	jffs2_complete_reservation(c);

	d_instantiate_new(dentry, iyesde);
	return 0;

 fail:
	iget_failed(iyesde);
	return ret;
}

static int jffs2_rename (struct iyesde *old_dir_i, struct dentry *old_dentry,
			 struct iyesde *new_dir_i, struct dentry *new_dentry,
			 unsigned int flags)
{
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(old_dir_i->i_sb);
	struct jffs2_iyesde_info *victim_f = NULL;
	uint8_t type;
	uint32_t yesw;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	/* The VFS will check for us and prevent trying to rename a
	 * file over a directory and vice versa, but if it's a directory,
	 * the VFS can't check whether the victim is empty. The filesystem
	 * needs to do that for itself.
	 */
	if (d_really_is_positive(new_dentry)) {
		victim_f = JFFS2_INODE_INFO(d_iyesde(new_dentry));
		if (d_is_dir(new_dentry)) {
			struct jffs2_full_dirent *fd;

			mutex_lock(&victim_f->sem);
			for (fd = victim_f->dents; fd; fd = fd->next) {
				if (fd->iyes) {
					mutex_unlock(&victim_f->sem);
					return -ENOTEMPTY;
				}
			}
			mutex_unlock(&victim_f->sem);
		}
	}

	/* XXX: We probably ought to alloc eyesugh space for
	   both yesdes at the same time. Writing the new link,
	   then getting -ENOSPC, is quite bad :)
	*/

	/* Make a hard link */

	/* XXX: This is ugly */
	type = (d_iyesde(old_dentry)->i_mode & S_IFMT) >> 12;
	if (!type) type = DT_REG;

	yesw = JFFS2_NOW();
	ret = jffs2_do_link(c, JFFS2_INODE_INFO(new_dir_i),
			    d_iyesde(old_dentry)->i_iyes, type,
			    new_dentry->d_name.name, new_dentry->d_name.len, yesw);

	if (ret)
		return ret;

	if (victim_f) {
		/* There was a victim. Kill it off nicely */
		if (d_is_dir(new_dentry))
			clear_nlink(d_iyesde(new_dentry));
		else
			drop_nlink(d_iyesde(new_dentry));
		/* Don't oops if the victim was a dirent pointing to an
		   iyesde which didn't exist. */
		if (victim_f->iyescache) {
			mutex_lock(&victim_f->sem);
			if (d_is_dir(new_dentry))
				victim_f->iyescache->piyes_nlink = 0;
			else
				victim_f->iyescache->piyes_nlink--;
			mutex_unlock(&victim_f->sem);
		}
	}

	/* If it was a directory we moved, and there was yes victim,
	   increase i_nlink on its new parent */
	if (d_is_dir(old_dentry) && !victim_f)
		inc_nlink(new_dir_i);

	/* Unlink the original */
	ret = jffs2_do_unlink(c, JFFS2_INODE_INFO(old_dir_i),
			      old_dentry->d_name.name, old_dentry->d_name.len, NULL, yesw);

	/* We don't touch iyesde->i_nlink */

	if (ret) {
		/* Oh shit. We really ought to make a single yesde which can do both atomically */
		struct jffs2_iyesde_info *f = JFFS2_INODE_INFO(d_iyesde(old_dentry));
		mutex_lock(&f->sem);
		inc_nlink(d_iyesde(old_dentry));
		if (f->iyescache && !d_is_dir(old_dentry))
			f->iyescache->piyes_nlink++;
		mutex_unlock(&f->sem);

		pr_yestice("%s(): Link succeeded, unlink failed (err %d). You yesw have a hard link\n",
			  __func__, ret);
		/*
		 * We can't keep the target in dcache after that.
		 * For one thing, we can't afford dentry aliases for directories.
		 * For ayesther, if there was a victim, we _can't_ set new iyesde
		 * for that sucker and we have to trigger mount eviction - the
		 * caller won't do it on its own since we are returning an error.
		 */
		d_invalidate(new_dentry);
		new_dir_i->i_mtime = new_dir_i->i_ctime = ITIME(yesw);
		return ret;
	}

	if (d_is_dir(old_dentry))
		drop_nlink(old_dir_i);

	new_dir_i->i_mtime = new_dir_i->i_ctime = old_dir_i->i_mtime = old_dir_i->i_ctime = ITIME(yesw);

	return 0;
}

