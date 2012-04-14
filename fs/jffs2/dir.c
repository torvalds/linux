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
#include "nodelist.h"

static int jffs2_readdir (struct file *, void *, filldir_t);

static int jffs2_create (struct inode *,struct dentry *,umode_t,
			 struct nameidata *);
static struct dentry *jffs2_lookup (struct inode *,struct dentry *,
				    struct nameidata *);
static int jffs2_link (struct dentry *,struct inode *,struct dentry *);
static int jffs2_unlink (struct inode *,struct dentry *);
static int jffs2_symlink (struct inode *,struct dentry *,const char *);
static int jffs2_mkdir (struct inode *,struct dentry *,umode_t);
static int jffs2_rmdir (struct inode *,struct dentry *);
static int jffs2_mknod (struct inode *,struct dentry *,umode_t,dev_t);
static int jffs2_rename (struct inode *, struct dentry *,
			 struct inode *, struct dentry *);

const struct file_operations jffs2_dir_operations =
{
	.read =		generic_read_dir,
	.readdir =	jffs2_readdir,
	.unlocked_ioctl=jffs2_ioctl,
	.fsync =	jffs2_fsync,
	.llseek =	generic_file_llseek,
};


const struct inode_operations jffs2_dir_inode_operations =
{
	.create =	jffs2_create,
	.lookup =	jffs2_lookup,
	.link =		jffs2_link,
	.unlink =	jffs2_unlink,
	.symlink =	jffs2_symlink,
	.mkdir =	jffs2_mkdir,
	.rmdir =	jffs2_rmdir,
	.mknod =	jffs2_mknod,
	.rename =	jffs2_rename,
	.get_acl =	jffs2_get_acl,
	.setattr =	jffs2_setattr,
	.setxattr =	jffs2_setxattr,
	.getxattr =	jffs2_getxattr,
	.listxattr =	jffs2_listxattr,
	.removexattr =	jffs2_removexattr
};

/***********************************************************************/


/* We keep the dirent list sorted in increasing order of name hash,
   and we use the same hash function as the dentries. Makes this
   nice and simple
*/
static struct dentry *jffs2_lookup(struct inode *dir_i, struct dentry *target,
				   struct nameidata *nd)
{
	struct jffs2_inode_info *dir_f;
	struct jffs2_full_dirent *fd = NULL, *fd_list;
	uint32_t ino = 0;
	struct inode *inode = NULL;

	jffs2_dbg(1, "jffs2_lookup()\n");

	if (target->d_name.len > JFFS2_MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	dir_f = JFFS2_INODE_INFO(dir_i);

	mutex_lock(&dir_f->sem);

	/* NB: The 2.2 backport will need to explicitly check for '.' and '..' here */
	for (fd_list = dir_f->dents; fd_list && fd_list->nhash <= target->d_name.hash; fd_list = fd_list->next) {
		if (fd_list->nhash == target->d_name.hash &&
		    (!fd || fd_list->version > fd->version) &&
		    strlen(fd_list->name) == target->d_name.len &&
		    !strncmp(fd_list->name, target->d_name.name, target->d_name.len)) {
			fd = fd_list;
		}
	}
	if (fd)
		ino = fd->ino;
	mutex_unlock(&dir_f->sem);
	if (ino) {
		inode = jffs2_iget(dir_i->i_sb, ino);
		if (IS_ERR(inode))
			pr_warn("iget() failed for ino #%u\n", ino);
	}

	return d_splice_alias(inode, target);
}

/***********************************************************************/


static int jffs2_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct jffs2_inode_info *f;
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct jffs2_full_dirent *fd;
	unsigned long offset, curofs;

	jffs2_dbg(1, "jffs2_readdir() for dir_i #%lu\n",
		  filp->f_path.dentry->d_inode->i_ino);

	f = JFFS2_INODE_INFO(inode);

	offset = filp->f_pos;

	if (offset == 0) {
		jffs2_dbg(1, "Dirent 0: \".\", ino #%lu\n", inode->i_ino);
		if (filldir(dirent, ".", 1, 0, inode->i_ino, DT_DIR) < 0)
			goto out;
		offset++;
	}
	if (offset == 1) {
		unsigned long pino = parent_ino(filp->f_path.dentry);
		jffs2_dbg(1, "Dirent 1: \"..\", ino #%lu\n", pino);
		if (filldir(dirent, "..", 2, 1, pino, DT_DIR) < 0)
			goto out;
		offset++;
	}

	curofs=1;
	mutex_lock(&f->sem);
	for (fd = f->dents; fd; fd = fd->next) {

		curofs++;
		/* First loop: curofs = 2; offset = 2 */
		if (curofs < offset) {
			jffs2_dbg(2, "Skipping dirent: \"%s\", ino #%u, type %d, because curofs %ld < offset %ld\n",
				  fd->name, fd->ino, fd->type, curofs, offset);
			continue;
		}
		if (!fd->ino) {
			jffs2_dbg(2, "Skipping deletion dirent \"%s\"\n",
				  fd->name);
			offset++;
			continue;
		}
		jffs2_dbg(2, "Dirent %ld: \"%s\", ino #%u, type %d\n",
			  offset, fd->name, fd->ino, fd->type);
		if (filldir(dirent, fd->name, strlen(fd->name), offset, fd->ino, fd->type) < 0)
			break;
		offset++;
	}
	mutex_unlock(&f->sem);
 out:
	filp->f_pos = offset;
	return 0;
}

/***********************************************************************/


static int jffs2_create(struct inode *dir_i, struct dentry *dentry,
			umode_t mode, struct nameidata *nd)
{
	struct jffs2_raw_inode *ri;
	struct jffs2_inode_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct inode *inode;
	int ret;

	ri = jffs2_alloc_raw_inode();
	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	jffs2_dbg(1, "%s()\n", __func__);

	inode = jffs2_new_inode(dir_i, mode, ri);

	if (IS_ERR(inode)) {
		jffs2_dbg(1, "jffs2_new_inode() failed\n");
		jffs2_free_raw_inode(ri);
		return PTR_ERR(inode);
	}

	inode->i_op = &jffs2_file_inode_operations;
	inode->i_fop = &jffs2_file_operations;
	inode->i_mapping->a_ops = &jffs2_file_address_operations;
	inode->i_mapping->nrpages = 0;

	f = JFFS2_INODE_INFO(inode);
	dir_f = JFFS2_INODE_INFO(dir_i);

	/* jffs2_do_create() will want to lock it, _after_ reserving
	   space and taking c-alloc_sem. If we keep it locked here,
	   lockdep gets unhappy (although it's a false positive;
	   nothing else will be looking at this inode yet so there's
	   no chance of AB-BA deadlock involving its f->sem). */
	mutex_unlock(&f->sem);

	ret = jffs2_do_create(c, dir_f, f, ri, &dentry->d_name);
	if (ret)
		goto fail;

	dir_i->i_mtime = dir_i->i_ctime = ITIME(je32_to_cpu(ri->ctime));

	jffs2_free_raw_inode(ri);

	jffs2_dbg(1, "%s(): Created ino #%lu with mode %o, nlink %d(%d). nrpages %ld\n",
		  __func__, inode->i_ino, inode->i_mode, inode->i_nlink,
		  f->inocache->pino_nlink, inode->i_mapping->nrpages);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
	return 0;

 fail:
	iget_failed(inode);
	jffs2_free_raw_inode(ri);
	return ret;
}

/***********************************************************************/


static int jffs2_unlink(struct inode *dir_i, struct dentry *dentry)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(dir_i->i_sb);
	struct jffs2_inode_info *dir_f = JFFS2_INODE_INFO(dir_i);
	struct jffs2_inode_info *dead_f = JFFS2_INODE_INFO(dentry->d_inode);
	int ret;
	uint32_t now = get_seconds();

	ret = jffs2_do_unlink(c, dir_f, dentry->d_name.name,
			      dentry->d_name.len, dead_f, now);
	if (dead_f->inocache)
		set_nlink(dentry->d_inode, dead_f->inocache->pino_nlink);
	if (!ret)
		dir_i->i_mtime = dir_i->i_ctime = ITIME(now);
	return ret;
}
/***********************************************************************/


static int jffs2_link (struct dentry *old_dentry, struct inode *dir_i, struct dentry *dentry)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(old_dentry->d_inode->i_sb);
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(old_dentry->d_inode);
	struct jffs2_inode_info *dir_f = JFFS2_INODE_INFO(dir_i);
	int ret;
	uint8_t type;
	uint32_t now;

	/* Don't let people make hard links to bad inodes. */
	if (!f->inocache)
		return -EIO;

	if (S_ISDIR(old_dentry->d_inode->i_mode))
		return -EPERM;

	/* XXX: This is ugly */
	type = (old_dentry->d_inode->i_mode & S_IFMT) >> 12;
	if (!type) type = DT_REG;

	now = get_seconds();
	ret = jffs2_do_link(c, dir_f, f->inocache->ino, type, dentry->d_name.name, dentry->d_name.len, now);

	if (!ret) {
		mutex_lock(&f->sem);
		set_nlink(old_dentry->d_inode, ++f->inocache->pino_nlink);
		mutex_unlock(&f->sem);
		d_instantiate(dentry, old_dentry->d_inode);
		dir_i->i_mtime = dir_i->i_ctime = ITIME(now);
		ihold(old_dentry->d_inode);
	}
	return ret;
}

/***********************************************************************/

static int jffs2_symlink (struct inode *dir_i, struct dentry *dentry, const char *target)
{
	struct jffs2_inode_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct inode *inode;
	struct jffs2_raw_inode *ri;
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dnode *fn;
	struct jffs2_full_dirent *fd;
	int namelen;
	uint32_t alloclen;
	int ret, targetlen = strlen(target);

	/* FIXME: If you care. We'd need to use frags for the target
	   if it grows much more than this */
	if (targetlen > 254)
		return -ENAMETOOLONG;

	ri = jffs2_alloc_raw_inode();

	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	/* Try to reserve enough space for both node and dirent.
	 * Just the node will do for now, though
	 */
	namelen = dentry->d_name.len;
	ret = jffs2_reserve_space(c, sizeof(*ri) + targetlen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);

	if (ret) {
		jffs2_free_raw_inode(ri);
		return ret;
	}

	inode = jffs2_new_inode(dir_i, S_IFLNK | S_IRWXUGO, ri);

	if (IS_ERR(inode)) {
		jffs2_free_raw_inode(ri);
		jffs2_complete_reservation(c);
		return PTR_ERR(inode);
	}

	inode->i_op = &jffs2_symlink_inode_operations;

	f = JFFS2_INODE_INFO(inode);

	inode->i_size = targetlen;
	ri->isize = ri->dsize = ri->csize = cpu_to_je32(inode->i_size);
	ri->totlen = cpu_to_je32(sizeof(*ri) + inode->i_size);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unknown_node)-4));

	ri->compr = JFFS2_COMPR_NONE;
	ri->data_crc = cpu_to_je32(crc32(0, target, targetlen));
	ri->node_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));

	fn = jffs2_write_dnode(c, f, ri, target, targetlen, ALLOC_NORMAL);

	jffs2_free_raw_inode(ri);

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

	jffs2_dbg(1, "%s(): symlink's target '%s' cached\n",
		  __func__, (char *)f->target);

	/* No data here. Only a metadata node, which will be
	   obsoleted by the first data write
	*/
	f->metadata = fn;
	mutex_unlock(&f->sem);

	jffs2_complete_reservation(c);

	ret = jffs2_init_security(inode, dir_i, &dentry->d_name);
	if (ret)
		goto fail;

	ret = jffs2_init_acl_post(inode);
	if (ret)
		goto fail;

	ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
	if (ret)
		goto fail;

	rd = jffs2_alloc_raw_dirent();
	if (!rd) {
		/* Argh. Now we treat it like a normal delete */
		jffs2_complete_reservation(c);
		ret = -ENOMEM;
		goto fail;
	}

	dir_f = JFFS2_INODE_INFO(dir_i);
	mutex_lock(&dir_f->sem);

	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->nodetype = cpu_to_je16(JFFS2_NODETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unknown_node)-4));

	rd->pino = cpu_to_je32(dir_i->i_ino);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->ino = cpu_to_je32(inode->i_ino);
	rd->mctime = cpu_to_je32(get_seconds());
	rd->nsize = namelen;
	rd->type = DT_LNK;
	rd->node_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, dentry->d_name.name, namelen));

	fd = jffs2_write_dirent(c, dir_f, rd, dentry->d_name.name, namelen, ALLOC_NORMAL);

	if (IS_ERR(fd)) {
		/* dirent failed to write. Delete the inode normally
		   as if it were the final unlink() */
		jffs2_complete_reservation(c);
		jffs2_free_raw_dirent(rd);
		mutex_unlock(&dir_f->sem);
		ret = PTR_ERR(fd);
		goto fail;
	}

	dir_i->i_mtime = dir_i->i_ctime = ITIME(je32_to_cpu(rd->mctime));

	jffs2_free_raw_dirent(rd);

	/* Link the fd into the inode's list, obsoleting an old
	   one if necessary. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	mutex_unlock(&dir_f->sem);
	jffs2_complete_reservation(c);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
	return 0;

 fail:
	iget_failed(inode);
	return ret;
}


static int jffs2_mkdir (struct inode *dir_i, struct dentry *dentry, umode_t mode)
{
	struct jffs2_inode_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct inode *inode;
	struct jffs2_raw_inode *ri;
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dnode *fn;
	struct jffs2_full_dirent *fd;
	int namelen;
	uint32_t alloclen;
	int ret;

	mode |= S_IFDIR;

	ri = jffs2_alloc_raw_inode();
	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	/* Try to reserve enough space for both node and dirent.
	 * Just the node will do for now, though
	 */
	namelen = dentry->d_name.len;
	ret = jffs2_reserve_space(c, sizeof(*ri), &alloclen, ALLOC_NORMAL,
				  JFFS2_SUMMARY_INODE_SIZE);

	if (ret) {
		jffs2_free_raw_inode(ri);
		return ret;
	}

	inode = jffs2_new_inode(dir_i, mode, ri);

	if (IS_ERR(inode)) {
		jffs2_free_raw_inode(ri);
		jffs2_complete_reservation(c);
		return PTR_ERR(inode);
	}

	inode->i_op = &jffs2_dir_inode_operations;
	inode->i_fop = &jffs2_dir_operations;

	f = JFFS2_INODE_INFO(inode);

	/* Directories get nlink 2 at start */
	set_nlink(inode, 2);
	/* but ic->pino_nlink is the parent ino# */
	f->inocache->pino_nlink = dir_i->i_ino;

	ri->data_crc = cpu_to_je32(0);
	ri->node_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));

	fn = jffs2_write_dnode(c, f, ri, NULL, 0, ALLOC_NORMAL);

	jffs2_free_raw_inode(ri);

	if (IS_ERR(fn)) {
		/* Eeek. Wave bye bye */
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		ret = PTR_ERR(fn);
		goto fail;
	}
	/* No data here. Only a metadata node, which will be
	   obsoleted by the first data write
	*/
	f->metadata = fn;
	mutex_unlock(&f->sem);

	jffs2_complete_reservation(c);

	ret = jffs2_init_security(inode, dir_i, &dentry->d_name);
	if (ret)
		goto fail;

	ret = jffs2_init_acl_post(inode);
	if (ret)
		goto fail;

	ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
	if (ret)
		goto fail;

	rd = jffs2_alloc_raw_dirent();
	if (!rd) {
		/* Argh. Now we treat it like a normal delete */
		jffs2_complete_reservation(c);
		ret = -ENOMEM;
		goto fail;
	}

	dir_f = JFFS2_INODE_INFO(dir_i);
	mutex_lock(&dir_f->sem);

	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->nodetype = cpu_to_je16(JFFS2_NODETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unknown_node)-4));

	rd->pino = cpu_to_je32(dir_i->i_ino);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->ino = cpu_to_je32(inode->i_ino);
	rd->mctime = cpu_to_je32(get_seconds());
	rd->nsize = namelen;
	rd->type = DT_DIR;
	rd->node_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, dentry->d_name.name, namelen));

	fd = jffs2_write_dirent(c, dir_f, rd, dentry->d_name.name, namelen, ALLOC_NORMAL);

	if (IS_ERR(fd)) {
		/* dirent failed to write. Delete the inode normally
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

	/* Link the fd into the inode's list, obsoleting an old
	   one if necessary. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	mutex_unlock(&dir_f->sem);
	jffs2_complete_reservation(c);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
	return 0;

 fail:
	iget_failed(inode);
	return ret;
}

static int jffs2_rmdir (struct inode *dir_i, struct dentry *dentry)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(dir_i->i_sb);
	struct jffs2_inode_info *dir_f = JFFS2_INODE_INFO(dir_i);
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(dentry->d_inode);
	struct jffs2_full_dirent *fd;
	int ret;
	uint32_t now = get_seconds();

	for (fd = f->dents ; fd; fd = fd->next) {
		if (fd->ino)
			return -ENOTEMPTY;
	}

	ret = jffs2_do_unlink(c, dir_f, dentry->d_name.name,
			      dentry->d_name.len, f, now);
	if (!ret) {
		dir_i->i_mtime = dir_i->i_ctime = ITIME(now);
		clear_nlink(dentry->d_inode);
		drop_nlink(dir_i);
	}
	return ret;
}

static int jffs2_mknod (struct inode *dir_i, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct jffs2_inode_info *f, *dir_f;
	struct jffs2_sb_info *c;
	struct inode *inode;
	struct jffs2_raw_inode *ri;
	struct jffs2_raw_dirent *rd;
	struct jffs2_full_dnode *fn;
	struct jffs2_full_dirent *fd;
	int namelen;
	union jffs2_device_node dev;
	int devlen = 0;
	uint32_t alloclen;
	int ret;

	if (!new_valid_dev(rdev))
		return -EINVAL;

	ri = jffs2_alloc_raw_inode();
	if (!ri)
		return -ENOMEM;

	c = JFFS2_SB_INFO(dir_i->i_sb);

	if (S_ISBLK(mode) || S_ISCHR(mode))
		devlen = jffs2_encode_dev(&dev, rdev);

	/* Try to reserve enough space for both node and dirent.
	 * Just the node will do for now, though
	 */
	namelen = dentry->d_name.len;
	ret = jffs2_reserve_space(c, sizeof(*ri) + devlen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);

	if (ret) {
		jffs2_free_raw_inode(ri);
		return ret;
	}

	inode = jffs2_new_inode(dir_i, mode, ri);

	if (IS_ERR(inode)) {
		jffs2_free_raw_inode(ri);
		jffs2_complete_reservation(c);
		return PTR_ERR(inode);
	}
	inode->i_op = &jffs2_file_inode_operations;
	init_special_inode(inode, inode->i_mode, rdev);

	f = JFFS2_INODE_INFO(inode);

	ri->dsize = ri->csize = cpu_to_je32(devlen);
	ri->totlen = cpu_to_je32(sizeof(*ri) + devlen);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unknown_node)-4));

	ri->compr = JFFS2_COMPR_NONE;
	ri->data_crc = cpu_to_je32(crc32(0, &dev, devlen));
	ri->node_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));

	fn = jffs2_write_dnode(c, f, ri, (char *)&dev, devlen, ALLOC_NORMAL);

	jffs2_free_raw_inode(ri);

	if (IS_ERR(fn)) {
		/* Eeek. Wave bye bye */
		mutex_unlock(&f->sem);
		jffs2_complete_reservation(c);
		ret = PTR_ERR(fn);
		goto fail;
	}
	/* No data here. Only a metadata node, which will be
	   obsoleted by the first data write
	*/
	f->metadata = fn;
	mutex_unlock(&f->sem);

	jffs2_complete_reservation(c);

	ret = jffs2_init_security(inode, dir_i, &dentry->d_name);
	if (ret)
		goto fail;

	ret = jffs2_init_acl_post(inode);
	if (ret)
		goto fail;

	ret = jffs2_reserve_space(c, sizeof(*rd)+namelen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_DIRENT_SIZE(namelen));
	if (ret)
		goto fail;

	rd = jffs2_alloc_raw_dirent();
	if (!rd) {
		/* Argh. Now we treat it like a normal delete */
		jffs2_complete_reservation(c);
		ret = -ENOMEM;
		goto fail;
	}

	dir_f = JFFS2_INODE_INFO(dir_i);
	mutex_lock(&dir_f->sem);

	rd->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	rd->nodetype = cpu_to_je16(JFFS2_NODETYPE_DIRENT);
	rd->totlen = cpu_to_je32(sizeof(*rd) + namelen);
	rd->hdr_crc = cpu_to_je32(crc32(0, rd, sizeof(struct jffs2_unknown_node)-4));

	rd->pino = cpu_to_je32(dir_i->i_ino);
	rd->version = cpu_to_je32(++dir_f->highest_version);
	rd->ino = cpu_to_je32(inode->i_ino);
	rd->mctime = cpu_to_je32(get_seconds());
	rd->nsize = namelen;

	/* XXX: This is ugly. */
	rd->type = (mode & S_IFMT) >> 12;

	rd->node_crc = cpu_to_je32(crc32(0, rd, sizeof(*rd)-8));
	rd->name_crc = cpu_to_je32(crc32(0, dentry->d_name.name, namelen));

	fd = jffs2_write_dirent(c, dir_f, rd, dentry->d_name.name, namelen, ALLOC_NORMAL);

	if (IS_ERR(fd)) {
		/* dirent failed to write. Delete the inode normally
		   as if it were the final unlink() */
		jffs2_complete_reservation(c);
		jffs2_free_raw_dirent(rd);
		mutex_unlock(&dir_f->sem);
		ret = PTR_ERR(fd);
		goto fail;
	}

	dir_i->i_mtime = dir_i->i_ctime = ITIME(je32_to_cpu(rd->mctime));

	jffs2_free_raw_dirent(rd);

	/* Link the fd into the inode's list, obsoleting an old
	   one if necessary. */
	jffs2_add_fd_to_list(c, fd, &dir_f->dents);

	mutex_unlock(&dir_f->sem);
	jffs2_complete_reservation(c);

	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
	return 0;

 fail:
	iget_failed(inode);
	return ret;
}

static int jffs2_rename (struct inode *old_dir_i, struct dentry *old_dentry,
			 struct inode *new_dir_i, struct dentry *new_dentry)
{
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(old_dir_i->i_sb);
	struct jffs2_inode_info *victim_f = NULL;
	uint8_t type;
	uint32_t now;

	/* The VFS will check for us and prevent trying to rename a
	 * file over a directory and vice versa, but if it's a directory,
	 * the VFS can't check whether the victim is empty. The filesystem
	 * needs to do that for itself.
	 */
	if (new_dentry->d_inode) {
		victim_f = JFFS2_INODE_INFO(new_dentry->d_inode);
		if (S_ISDIR(new_dentry->d_inode->i_mode)) {
			struct jffs2_full_dirent *fd;

			mutex_lock(&victim_f->sem);
			for (fd = victim_f->dents; fd; fd = fd->next) {
				if (fd->ino) {
					mutex_unlock(&victim_f->sem);
					return -ENOTEMPTY;
				}
			}
			mutex_unlock(&victim_f->sem);
		}
	}

	/* XXX: We probably ought to alloc enough space for
	   both nodes at the same time. Writing the new link,
	   then getting -ENOSPC, is quite bad :)
	*/

	/* Make a hard link */

	/* XXX: This is ugly */
	type = (old_dentry->d_inode->i_mode & S_IFMT) >> 12;
	if (!type) type = DT_REG;

	now = get_seconds();
	ret = jffs2_do_link(c, JFFS2_INODE_INFO(new_dir_i),
			    old_dentry->d_inode->i_ino, type,
			    new_dentry->d_name.name, new_dentry->d_name.len, now);

	if (ret)
		return ret;

	if (victim_f) {
		/* There was a victim. Kill it off nicely */
		if (S_ISDIR(new_dentry->d_inode->i_mode))
			clear_nlink(new_dentry->d_inode);
		else
			drop_nlink(new_dentry->d_inode);
		/* Don't oops if the victim was a dirent pointing to an
		   inode which didn't exist. */
		if (victim_f->inocache) {
			mutex_lock(&victim_f->sem);
			if (S_ISDIR(new_dentry->d_inode->i_mode))
				victim_f->inocache->pino_nlink = 0;
			else
				victim_f->inocache->pino_nlink--;
			mutex_unlock(&victim_f->sem);
		}
	}

	/* If it was a directory we moved, and there was no victim,
	   increase i_nlink on its new parent */
	if (S_ISDIR(old_dentry->d_inode->i_mode) && !victim_f)
		inc_nlink(new_dir_i);

	/* Unlink the original */
	ret = jffs2_do_unlink(c, JFFS2_INODE_INFO(old_dir_i),
			      old_dentry->d_name.name, old_dentry->d_name.len, NULL, now);

	/* We don't touch inode->i_nlink */

	if (ret) {
		/* Oh shit. We really ought to make a single node which can do both atomically */
		struct jffs2_inode_info *f = JFFS2_INODE_INFO(old_dentry->d_inode);
		mutex_lock(&f->sem);
		inc_nlink(old_dentry->d_inode);
		if (f->inocache && !S_ISDIR(old_dentry->d_inode->i_mode))
			f->inocache->pino_nlink++;
		mutex_unlock(&f->sem);

		pr_notice("%s(): Link succeeded, unlink failed (err %d). You now have a hard link\n",
			  __func__, ret);
		/* Might as well let the VFS know */
		d_instantiate(new_dentry, old_dentry->d_inode);
		ihold(old_dentry->d_inode);
		new_dir_i->i_mtime = new_dir_i->i_ctime = ITIME(now);
		return ret;
	}

	if (S_ISDIR(old_dentry->d_inode->i_mode))
		drop_nlink(old_dir_i);

	new_dir_i->i_mtime = new_dir_i->i_ctime = old_dir_i->i_mtime = old_dir_i->i_ctime = ITIME(now);

	return 0;
}

