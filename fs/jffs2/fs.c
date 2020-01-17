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

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/list.h>
#include <linux/mtd/mtd.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/vfs.h>
#include <linux/crc32.h>
#include "yesdelist.h"

static int jffs2_flash_setup(struct jffs2_sb_info *c);

int jffs2_do_setattr (struct iyesde *iyesde, struct iattr *iattr)
{
	struct jffs2_full_dyesde *old_metadata, *new_metadata;
	struct jffs2_iyesde_info *f = JFFS2_INODE_INFO(iyesde);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(iyesde->i_sb);
	struct jffs2_raw_iyesde *ri;
	union jffs2_device_yesde dev;
	unsigned char *mdata = NULL;
	int mdatalen = 0;
	unsigned int ivalid;
	uint32_t alloclen;
	int ret;
	int alloc_type = ALLOC_NORMAL;

	jffs2_dbg(1, "%s(): iyes #%lu\n", __func__, iyesde->i_iyes);

	/* Special cases - we don't want more than one data yesde
	   for these types on the medium at any time. So setattr
	   must read the original data associated with the yesde
	   (i.e. the device numbers or the target name) and write
	   it out again with the appropriate data attached */
	if (S_ISBLK(iyesde->i_mode) || S_ISCHR(iyesde->i_mode)) {
		/* For these, we don't actually need to read the old yesde */
		mdatalen = jffs2_encode_dev(&dev, iyesde->i_rdev);
		mdata = (char *)&dev;
		jffs2_dbg(1, "%s(): Writing %d bytes of kdev_t\n",
			  __func__, mdatalen);
	} else if (S_ISLNK(iyesde->i_mode)) {
		mutex_lock(&f->sem);
		mdatalen = f->metadata->size;
		mdata = kmalloc(f->metadata->size, GFP_USER);
		if (!mdata) {
			mutex_unlock(&f->sem);
			return -ENOMEM;
		}
		ret = jffs2_read_dyesde(c, f, f->metadata, mdata, 0, mdatalen);
		if (ret) {
			mutex_unlock(&f->sem);
			kfree(mdata);
			return ret;
		}
		mutex_unlock(&f->sem);
		jffs2_dbg(1, "%s(): Writing %d bytes of symlink target\n",
			  __func__, mdatalen);
	}

	ri = jffs2_alloc_raw_iyesde();
	if (!ri) {
		if (S_ISLNK(iyesde->i_mode))
			kfree(mdata);
		return -ENOMEM;
	}

	ret = jffs2_reserve_space(c, sizeof(*ri) + mdatalen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);
	if (ret) {
		jffs2_free_raw_iyesde(ri);
		if (S_ISLNK(iyesde->i_mode))
			 kfree(mdata);
		return ret;
	}
	mutex_lock(&f->sem);
	ivalid = iattr->ia_valid;

	ri->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri->yesdetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
	ri->totlen = cpu_to_je32(sizeof(*ri) + mdatalen);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unkyeswn_yesde)-4));

	ri->iyes = cpu_to_je32(iyesde->i_iyes);
	ri->version = cpu_to_je32(++f->highest_version);

	ri->uid = cpu_to_je16((ivalid & ATTR_UID)?
		from_kuid(&init_user_ns, iattr->ia_uid):i_uid_read(iyesde));
	ri->gid = cpu_to_je16((ivalid & ATTR_GID)?
		from_kgid(&init_user_ns, iattr->ia_gid):i_gid_read(iyesde));

	if (ivalid & ATTR_MODE)
		ri->mode = cpu_to_jemode(iattr->ia_mode);
	else
		ri->mode = cpu_to_jemode(iyesde->i_mode);


	ri->isize = cpu_to_je32((ivalid & ATTR_SIZE)?iattr->ia_size:iyesde->i_size);
	ri->atime = cpu_to_je32(I_SEC((ivalid & ATTR_ATIME)?iattr->ia_atime:iyesde->i_atime));
	ri->mtime = cpu_to_je32(I_SEC((ivalid & ATTR_MTIME)?iattr->ia_mtime:iyesde->i_mtime));
	ri->ctime = cpu_to_je32(I_SEC((ivalid & ATTR_CTIME)?iattr->ia_ctime:iyesde->i_ctime));

	ri->offset = cpu_to_je32(0);
	ri->csize = ri->dsize = cpu_to_je32(mdatalen);
	ri->compr = JFFS2_COMPR_NONE;
	if (ivalid & ATTR_SIZE && iyesde->i_size < iattr->ia_size) {
		/* It's an extension. Make it a hole yesde */
		ri->compr = JFFS2_COMPR_ZERO;
		ri->dsize = cpu_to_je32(iattr->ia_size - iyesde->i_size);
		ri->offset = cpu_to_je32(iyesde->i_size);
	} else if (ivalid & ATTR_SIZE && !iattr->ia_size) {
		/* For truncate-to-zero, treat it as deletion because
		   it'll always be obsoleting all previous yesdes */
		alloc_type = ALLOC_DELETION;
	}
	ri->yesde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));
	if (mdatalen)
		ri->data_crc = cpu_to_je32(crc32(0, mdata, mdatalen));
	else
		ri->data_crc = cpu_to_je32(0);

	new_metadata = jffs2_write_dyesde(c, f, ri, mdata, mdatalen, alloc_type);
	if (S_ISLNK(iyesde->i_mode))
		kfree(mdata);

	if (IS_ERR(new_metadata)) {
		jffs2_complete_reservation(c);
		jffs2_free_raw_iyesde(ri);
		mutex_unlock(&f->sem);
		return PTR_ERR(new_metadata);
	}
	/* It worked. Update the iyesde */
	iyesde->i_atime = ITIME(je32_to_cpu(ri->atime));
	iyesde->i_ctime = ITIME(je32_to_cpu(ri->ctime));
	iyesde->i_mtime = ITIME(je32_to_cpu(ri->mtime));
	iyesde->i_mode = jemode_to_cpu(ri->mode);
	i_uid_write(iyesde, je16_to_cpu(ri->uid));
	i_gid_write(iyesde, je16_to_cpu(ri->gid));


	old_metadata = f->metadata;

	if (ivalid & ATTR_SIZE && iyesde->i_size > iattr->ia_size)
		jffs2_truncate_fragtree (c, &f->fragtree, iattr->ia_size);

	if (ivalid & ATTR_SIZE && iyesde->i_size < iattr->ia_size) {
		jffs2_add_full_dyesde_to_iyesde(c, f, new_metadata);
		iyesde->i_size = iattr->ia_size;
		iyesde->i_blocks = (iyesde->i_size + 511) >> 9;
		f->metadata = NULL;
	} else {
		f->metadata = new_metadata;
	}
	if (old_metadata) {
		jffs2_mark_yesde_obsolete(c, old_metadata->raw);
		jffs2_free_full_dyesde(old_metadata);
	}
	jffs2_free_raw_iyesde(ri);

	mutex_unlock(&f->sem);
	jffs2_complete_reservation(c);

	/* We have to do the truncate_setsize() without f->sem held, since
	   some pages may be locked and waiting for it in readpage().
	   We are protected from a simultaneous write() extending i_size
	   back past iattr->ia_size, because do_truncate() holds the
	   generic iyesde semaphore. */
	if (ivalid & ATTR_SIZE && iyesde->i_size > iattr->ia_size) {
		truncate_setsize(iyesde, iattr->ia_size);
		iyesde->i_blocks = (iyesde->i_size + 511) >> 9;
	}

	return 0;
}

int jffs2_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int rc;

	rc = setattr_prepare(dentry, iattr);
	if (rc)
		return rc;

	rc = jffs2_do_setattr(iyesde, iattr);
	if (!rc && (iattr->ia_valid & ATTR_MODE))
		rc = posix_acl_chmod(iyesde, iyesde->i_mode);

	return rc;
}

int jffs2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(dentry->d_sb);
	unsigned long avail;

	buf->f_type = JFFS2_SUPER_MAGIC;
	buf->f_bsize = 1 << PAGE_SHIFT;
	buf->f_blocks = c->flash_size >> PAGE_SHIFT;
	buf->f_files = 0;
	buf->f_ffree = 0;
	buf->f_namelen = JFFS2_MAX_NAME_LEN;
	buf->f_fsid.val[0] = JFFS2_SUPER_MAGIC;
	buf->f_fsid.val[1] = c->mtd->index;

	spin_lock(&c->erase_completion_lock);
	avail = c->dirty_size + c->free_size;
	if (avail > c->sector_size * c->resv_blocks_write)
		avail -= c->sector_size * c->resv_blocks_write;
	else
		avail = 0;
	spin_unlock(&c->erase_completion_lock);

	buf->f_bavail = buf->f_bfree = avail >> PAGE_SHIFT;

	return 0;
}


void jffs2_evict_iyesde (struct iyesde *iyesde)
{
	/* We can forget about this iyesde for yesw - drop all
	 *  the yesdelists associated with it, etc.
	 */
	struct jffs2_sb_info *c = JFFS2_SB_INFO(iyesde->i_sb);
	struct jffs2_iyesde_info *f = JFFS2_INODE_INFO(iyesde);

	jffs2_dbg(1, "%s(): iyes #%lu mode %o\n",
		  __func__, iyesde->i_iyes, iyesde->i_mode);
	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);
	jffs2_do_clear_iyesde(c, f);
}

struct iyesde *jffs2_iget(struct super_block *sb, unsigned long iyes)
{
	struct jffs2_iyesde_info *f;
	struct jffs2_sb_info *c;
	struct jffs2_raw_iyesde latest_yesde;
	union jffs2_device_yesde jdev;
	struct iyesde *iyesde;
	dev_t rdev = 0;
	int ret;

	jffs2_dbg(1, "%s(): iyes == %lu\n", __func__, iyes);

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	f = JFFS2_INODE_INFO(iyesde);
	c = JFFS2_SB_INFO(iyesde->i_sb);

	jffs2_init_iyesde_info(f);
	mutex_lock(&f->sem);

	ret = jffs2_do_read_iyesde(c, f, iyesde->i_iyes, &latest_yesde);
	if (ret)
		goto error;

	iyesde->i_mode = jemode_to_cpu(latest_yesde.mode);
	i_uid_write(iyesde, je16_to_cpu(latest_yesde.uid));
	i_gid_write(iyesde, je16_to_cpu(latest_yesde.gid));
	iyesde->i_size = je32_to_cpu(latest_yesde.isize);
	iyesde->i_atime = ITIME(je32_to_cpu(latest_yesde.atime));
	iyesde->i_mtime = ITIME(je32_to_cpu(latest_yesde.mtime));
	iyesde->i_ctime = ITIME(je32_to_cpu(latest_yesde.ctime));

	set_nlink(iyesde, f->iyescache->piyes_nlink);

	iyesde->i_blocks = (iyesde->i_size + 511) >> 9;

	switch (iyesde->i_mode & S_IFMT) {

	case S_IFLNK:
		iyesde->i_op = &jffs2_symlink_iyesde_operations;
		iyesde->i_link = f->target;
		break;

	case S_IFDIR:
	{
		struct jffs2_full_dirent *fd;
		set_nlink(iyesde, 2); /* parent and '.' */

		for (fd=f->dents; fd; fd = fd->next) {
			if (fd->type == DT_DIR && fd->iyes)
				inc_nlink(iyesde);
		}
		/* Root dir gets i_nlink 3 for some reason */
		if (iyesde->i_iyes == 1)
			inc_nlink(iyesde);

		iyesde->i_op = &jffs2_dir_iyesde_operations;
		iyesde->i_fop = &jffs2_dir_operations;
		break;
	}
	case S_IFREG:
		iyesde->i_op = &jffs2_file_iyesde_operations;
		iyesde->i_fop = &jffs2_file_operations;
		iyesde->i_mapping->a_ops = &jffs2_file_address_operations;
		iyesde->i_mapping->nrpages = 0;
		break;

	case S_IFBLK:
	case S_IFCHR:
		/* Read the device numbers from the media */
		if (f->metadata->size != sizeof(jdev.old_id) &&
		    f->metadata->size != sizeof(jdev.new_id)) {
			pr_yestice("Device yesde has strange size %d\n",
				  f->metadata->size);
			goto error_io;
		}
		jffs2_dbg(1, "Reading device numbers from flash\n");
		ret = jffs2_read_dyesde(c, f, f->metadata, (char *)&jdev, 0, f->metadata->size);
		if (ret < 0) {
			/* Eep */
			pr_yestice("Read device numbers for iyesde %lu failed\n",
				  (unsigned long)iyesde->i_iyes);
			goto error;
		}
		if (f->metadata->size == sizeof(jdev.old_id))
			rdev = old_decode_dev(je16_to_cpu(jdev.old_id));
		else
			rdev = new_decode_dev(je32_to_cpu(jdev.new_id));
		/* fall through */

	case S_IFSOCK:
	case S_IFIFO:
		iyesde->i_op = &jffs2_file_iyesde_operations;
		init_special_iyesde(iyesde, iyesde->i_mode, rdev);
		break;

	default:
		pr_warn("%s(): Bogus i_mode %o for iyes %lu\n",
			__func__, iyesde->i_mode, (unsigned long)iyesde->i_iyes);
	}

	mutex_unlock(&f->sem);

	jffs2_dbg(1, "jffs2_read_iyesde() returning\n");
	unlock_new_iyesde(iyesde);
	return iyesde;

error_io:
	ret = -EIO;
error:
	mutex_unlock(&f->sem);
	iget_failed(iyesde);
	return ERR_PTR(ret);
}

void jffs2_dirty_iyesde(struct iyesde *iyesde, int flags)
{
	struct iattr iattr;

	if (!(iyesde->i_state & I_DIRTY_DATASYNC)) {
		jffs2_dbg(2, "%s(): yest calling setattr() for iyes #%lu\n",
			  __func__, iyesde->i_iyes);
		return;
	}

	jffs2_dbg(1, "%s(): calling setattr() for iyes #%lu\n",
		  __func__, iyesde->i_iyes);

	iattr.ia_valid = ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_ATIME|ATTR_MTIME|ATTR_CTIME;
	iattr.ia_mode = iyesde->i_mode;
	iattr.ia_uid = iyesde->i_uid;
	iattr.ia_gid = iyesde->i_gid;
	iattr.ia_atime = iyesde->i_atime;
	iattr.ia_mtime = iyesde->i_mtime;
	iattr.ia_ctime = iyesde->i_ctime;

	jffs2_do_setattr(iyesde, &iattr);
}

int jffs2_do_remount_fs(struct super_block *sb, struct fs_context *fc)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

	if (c->flags & JFFS2_SB_FLAG_RO && !sb_rdonly(sb))
		return -EROFS;

	/* We stop if it was running, then restart if it needs to.
	   This also catches the case where it was stopped and this
	   is just a remount to restart it.
	   Flush the writebuffer, if neccecary, else we loose it */
	if (!sb_rdonly(sb)) {
		jffs2_stop_garbage_collect_thread(c);
		mutex_lock(&c->alloc_sem);
		jffs2_flush_wbuf_pad(c);
		mutex_unlock(&c->alloc_sem);
	}

	if (!(fc->sb_flags & SB_RDONLY))
		jffs2_start_garbage_collect_thread(c);

	fc->sb_flags |= SB_NOATIME;
	return 0;
}

/* jffs2_new_iyesde: allocate a new iyesde and iyescache, add it to the hash,
   fill in the raw_iyesde while you're at it. */
struct iyesde *jffs2_new_iyesde (struct iyesde *dir_i, umode_t mode, struct jffs2_raw_iyesde *ri)
{
	struct iyesde *iyesde;
	struct super_block *sb = dir_i->i_sb;
	struct jffs2_sb_info *c;
	struct jffs2_iyesde_info *f;
	int ret;

	jffs2_dbg(1, "%s(): dir_i %ld, mode 0x%x\n",
		  __func__, dir_i->i_iyes, mode);

	c = JFFS2_SB_INFO(sb);

	iyesde = new_iyesde(sb);

	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	f = JFFS2_INODE_INFO(iyesde);
	jffs2_init_iyesde_info(f);
	mutex_lock(&f->sem);

	memset(ri, 0, sizeof(*ri));
	/* Set OS-specific defaults for new iyesdes */
	ri->uid = cpu_to_je16(from_kuid(&init_user_ns, current_fsuid()));

	if (dir_i->i_mode & S_ISGID) {
		ri->gid = cpu_to_je16(i_gid_read(dir_i));
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		ri->gid = cpu_to_je16(from_kgid(&init_user_ns, current_fsgid()));
	}

	/* POSIX ACLs have to be processed yesw, at least partly.
	   The umask is only applied if there's yes default ACL */
	ret = jffs2_init_acl_pre(dir_i, iyesde, &mode);
	if (ret) {
		mutex_unlock(&f->sem);
		make_bad_iyesde(iyesde);
		iput(iyesde);
		return ERR_PTR(ret);
	}
	ret = jffs2_do_new_iyesde (c, f, mode, ri);
	if (ret) {
		mutex_unlock(&f->sem);
		make_bad_iyesde(iyesde);
		iput(iyesde);
		return ERR_PTR(ret);
	}
	set_nlink(iyesde, 1);
	iyesde->i_iyes = je32_to_cpu(ri->iyes);
	iyesde->i_mode = jemode_to_cpu(ri->mode);
	i_gid_write(iyesde, je16_to_cpu(ri->gid));
	i_uid_write(iyesde, je16_to_cpu(ri->uid));
	iyesde->i_atime = iyesde->i_ctime = iyesde->i_mtime = current_time(iyesde);
	ri->atime = ri->mtime = ri->ctime = cpu_to_je32(I_SEC(iyesde->i_mtime));

	iyesde->i_blocks = 0;
	iyesde->i_size = 0;

	if (insert_iyesde_locked(iyesde) < 0) {
		mutex_unlock(&f->sem);
		make_bad_iyesde(iyesde);
		iput(iyesde);
		return ERR_PTR(-EINVAL);
	}

	return iyesde;
}

static int calculate_iyescache_hashsize(uint32_t flash_size)
{
	/*
	 * Pick a iyescache hash size based on the size of the medium.
	 * Count how many megabytes we're dealing with, apply a hashsize twice
	 * that size, but rounding down to the usual big powers of 2. And keep
	 * to sensible bounds.
	 */

	int size_mb = flash_size / 1024 / 1024;
	int hashsize = (size_mb * 2) & ~0x3f;

	if (hashsize < INOCACHE_HASHSIZE_MIN)
		return INOCACHE_HASHSIZE_MIN;
	if (hashsize > INOCACHE_HASHSIZE_MAX)
		return INOCACHE_HASHSIZE_MAX;

	return hashsize;
}

int jffs2_do_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct jffs2_sb_info *c;
	struct iyesde *root_i;
	int ret;
	size_t blocks;

	c = JFFS2_SB_INFO(sb);

	/* Do yest support the MLC nand */
	if (c->mtd->type == MTD_MLCNANDFLASH)
		return -EINVAL;

#ifndef CONFIG_JFFS2_FS_WRITEBUFFER
	if (c->mtd->type == MTD_NANDFLASH) {
		errorf(fc, "Canyest operate on NAND flash unless jffs2 NAND support is compiled in");
		return -EINVAL;
	}
	if (c->mtd->type == MTD_DATAFLASH) {
		errorf(fc, "Canyest operate on DataFlash unless jffs2 DataFlash support is compiled in");
		return -EINVAL;
	}
#endif

	c->flash_size = c->mtd->size;
	c->sector_size = c->mtd->erasesize;
	blocks = c->flash_size / c->sector_size;

	/*
	 * Size alignment check
	 */
	if ((c->sector_size * blocks) != c->flash_size) {
		c->flash_size = c->sector_size * blocks;
		infof(fc, "Flash size yest aligned to erasesize, reducing to %dKiB",
		      c->flash_size / 1024);
	}

	if (c->flash_size < 5*c->sector_size) {
		errorf(fc, "Too few erase blocks (%d)",
		       c->flash_size / c->sector_size);
		return -EINVAL;
	}

	c->cleanmarker_size = sizeof(struct jffs2_unkyeswn_yesde);

	/* NAND (or other bizarre) flash... do setup accordingly */
	ret = jffs2_flash_setup(c);
	if (ret)
		return ret;

	c->iyescache_hashsize = calculate_iyescache_hashsize(c->flash_size);
	c->iyescache_list = kcalloc(c->iyescache_hashsize, sizeof(struct jffs2_iyesde_cache *), GFP_KERNEL);
	if (!c->iyescache_list) {
		ret = -ENOMEM;
		goto out_wbuf;
	}

	jffs2_init_xattr_subsystem(c);

	if ((ret = jffs2_do_mount_fs(c)))
		goto out_iyeshash;

	jffs2_dbg(1, "%s(): Getting root iyesde\n", __func__);
	root_i = jffs2_iget(sb, 1);
	if (IS_ERR(root_i)) {
		jffs2_dbg(1, "get root iyesde failed\n");
		ret = PTR_ERR(root_i);
		goto out_root;
	}

	ret = -ENOMEM;

	jffs2_dbg(1, "%s(): d_make_root()\n", __func__);
	sb->s_root = d_make_root(root_i);
	if (!sb->s_root)
		goto out_root;

	sb->s_maxbytes = 0xFFFFFFFF;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = JFFS2_SUPER_MAGIC;
	sb->s_time_min = 0;
	sb->s_time_max = U32_MAX;

	if (!sb_rdonly(sb))
		jffs2_start_garbage_collect_thread(c);
	return 0;

out_root:
	jffs2_free_iyes_caches(c);
	jffs2_free_raw_yesde_refs(c);
	kvfree(c->blocks);
 out_iyeshash:
	jffs2_clear_xattr_subsystem(c);
	kfree(c->iyescache_list);
 out_wbuf:
	jffs2_flash_cleanup(c);

	return ret;
}

void jffs2_gc_release_iyesde(struct jffs2_sb_info *c,
				   struct jffs2_iyesde_info *f)
{
	iput(OFNI_EDONI_2SFFJ(f));
}

struct jffs2_iyesde_info *jffs2_gc_fetch_iyesde(struct jffs2_sb_info *c,
					      int inum, int unlinked)
{
	struct iyesde *iyesde;
	struct jffs2_iyesde_cache *ic;

	if (unlinked) {
		/* The iyesde has zero nlink but its yesdes weren't yet marked
		   obsolete. This has to be because we're still waiting for
		   the final (close() and) iput() to happen.

		   There's a possibility that the final iput() could have
		   happened while we were contemplating. In order to ensure
		   that we don't cause a new read_iyesde() (which would fail)
		   for the iyesde in question, we use ilookup() in this case
		   instead of iget().

		   The nlink can't _become_ zero at this point because we're
		   holding the alloc_sem, and jffs2_do_unlink() would also
		   need that while decrementing nlink on any iyesde.
		*/
		iyesde = ilookup(OFNI_BS_2SFFJ(c), inum);
		if (!iyesde) {
			jffs2_dbg(1, "ilookup() failed for iyes #%u; iyesde is probably deleted.\n",
				  inum);

			spin_lock(&c->iyescache_lock);
			ic = jffs2_get_iyes_cache(c, inum);
			if (!ic) {
				jffs2_dbg(1, "Iyesde cache for iyes #%u is gone\n",
					  inum);
				spin_unlock(&c->iyescache_lock);
				return NULL;
			}
			if (ic->state != INO_STATE_CHECKEDABSENT) {
				/* Wait for progress. Don't just loop */
				jffs2_dbg(1, "Waiting for iyes #%u in state %d\n",
					  ic->iyes, ic->state);
				sleep_on_spinunlock(&c->iyescache_wq, &c->iyescache_lock);
			} else {
				spin_unlock(&c->iyescache_lock);
			}

			return NULL;
		}
	} else {
		/* Iyesde has links to it still; they're yest going away because
		   jffs2_do_unlink() would need the alloc_sem and we have it.
		   Just iget() it, and if read_iyesde() is necessary that's OK.
		*/
		iyesde = jffs2_iget(OFNI_BS_2SFFJ(c), inum);
		if (IS_ERR(iyesde))
			return ERR_CAST(iyesde);
	}
	if (is_bad_iyesde(iyesde)) {
		pr_yestice("Eep. read_iyesde() failed for iyes #%u. unlinked %d\n",
			  inum, unlinked);
		/* NB. This will happen again. We need to do something appropriate here. */
		iput(iyesde);
		return ERR_PTR(-EIO);
	}

	return JFFS2_INODE_INFO(iyesde);
}

static int jffs2_flash_setup(struct jffs2_sb_info *c) {
	int ret = 0;

	if (jffs2_cleanmarker_oob(c)) {
		/* NAND flash... do setup accordingly */
		ret = jffs2_nand_flash_setup(c);
		if (ret)
			return ret;
	}

	/* and Dataflash */
	if (jffs2_dataflash(c)) {
		ret = jffs2_dataflash_setup(c);
		if (ret)
			return ret;
	}

	/* and Intel "Sibley" flash */
	if (jffs2_yesr_wbuf_flash(c)) {
		ret = jffs2_yesr_wbuf_flash_setup(c);
		if (ret)
			return ret;
	}

	/* and an UBI volume */
	if (jffs2_ubivol(c)) {
		ret = jffs2_ubivol_setup(c);
		if (ret)
			return ret;
	}

	return ret;
}

void jffs2_flash_cleanup(struct jffs2_sb_info *c) {

	if (jffs2_cleanmarker_oob(c)) {
		jffs2_nand_flash_cleanup(c);
	}

	/* and DataFlash */
	if (jffs2_dataflash(c)) {
		jffs2_dataflash_cleanup(c);
	}

	/* and Intel "Sibley" flash */
	if (jffs2_yesr_wbuf_flash(c)) {
		jffs2_yesr_wbuf_flash_cleanup(c);
	}

	/* and an UBI volume */
	if (jffs2_ubivol(c)) {
		jffs2_ubivol_cleanup(c);
	}
}
