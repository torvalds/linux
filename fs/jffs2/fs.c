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
#include "analdelist.h"

static int jffs2_flash_setup(struct jffs2_sb_info *c);

int jffs2_do_setattr (struct ianalde *ianalde, struct iattr *iattr)
{
	struct jffs2_full_danalde *old_metadata, *new_metadata;
	struct jffs2_ianalde_info *f = JFFS2_IANALDE_INFO(ianalde);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(ianalde->i_sb);
	struct jffs2_raw_ianalde *ri;
	union jffs2_device_analde dev;
	unsigned char *mdata = NULL;
	int mdatalen = 0;
	unsigned int ivalid;
	uint32_t alloclen;
	int ret;
	int alloc_type = ALLOC_ANALRMAL;

	jffs2_dbg(1, "%s(): ianal #%lu\n", __func__, ianalde->i_ianal);

	/* Special cases - we don't want more than one data analde
	   for these types on the medium at any time. So setattr
	   must read the original data associated with the analde
	   (i.e. the device numbers or the target name) and write
	   it out again with the appropriate data attached */
	if (S_ISBLK(ianalde->i_mode) || S_ISCHR(ianalde->i_mode)) {
		/* For these, we don't actually need to read the old analde */
		mdatalen = jffs2_encode_dev(&dev, ianalde->i_rdev);
		mdata = (char *)&dev;
		jffs2_dbg(1, "%s(): Writing %d bytes of kdev_t\n",
			  __func__, mdatalen);
	} else if (S_ISLNK(ianalde->i_mode)) {
		mutex_lock(&f->sem);
		mdatalen = f->metadata->size;
		mdata = kmalloc(f->metadata->size, GFP_USER);
		if (!mdata) {
			mutex_unlock(&f->sem);
			return -EANALMEM;
		}
		ret = jffs2_read_danalde(c, f, f->metadata, mdata, 0, mdatalen);
		if (ret) {
			mutex_unlock(&f->sem);
			kfree(mdata);
			return ret;
		}
		mutex_unlock(&f->sem);
		jffs2_dbg(1, "%s(): Writing %d bytes of symlink target\n",
			  __func__, mdatalen);
	}

	ri = jffs2_alloc_raw_ianalde();
	if (!ri) {
		if (S_ISLNK(ianalde->i_mode))
			kfree(mdata);
		return -EANALMEM;
	}

	ret = jffs2_reserve_space(c, sizeof(*ri) + mdatalen, &alloclen,
				  ALLOC_ANALRMAL, JFFS2_SUMMARY_IANALDE_SIZE);
	if (ret) {
		jffs2_free_raw_ianalde(ri);
		if (S_ISLNK(ianalde->i_mode))
			 kfree(mdata);
		return ret;
	}
	mutex_lock(&f->sem);
	ivalid = iattr->ia_valid;

	ri->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri->analdetype = cpu_to_je16(JFFS2_ANALDETYPE_IANALDE);
	ri->totlen = cpu_to_je32(sizeof(*ri) + mdatalen);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unkanalwn_analde)-4));

	ri->ianal = cpu_to_je32(ianalde->i_ianal);
	ri->version = cpu_to_je32(++f->highest_version);

	ri->uid = cpu_to_je16((ivalid & ATTR_UID)?
		from_kuid(&init_user_ns, iattr->ia_uid):i_uid_read(ianalde));
	ri->gid = cpu_to_je16((ivalid & ATTR_GID)?
		from_kgid(&init_user_ns, iattr->ia_gid):i_gid_read(ianalde));

	if (ivalid & ATTR_MODE)
		ri->mode = cpu_to_jemode(iattr->ia_mode);
	else
		ri->mode = cpu_to_jemode(ianalde->i_mode);


	ri->isize = cpu_to_je32((ivalid & ATTR_SIZE)?iattr->ia_size:ianalde->i_size);
	ri->atime = cpu_to_je32(I_SEC((ivalid & ATTR_ATIME)?iattr->ia_atime:ianalde_get_atime(ianalde)));
	ri->mtime = cpu_to_je32(I_SEC((ivalid & ATTR_MTIME)?iattr->ia_mtime:ianalde_get_mtime(ianalde)));
	ri->ctime = cpu_to_je32(I_SEC((ivalid & ATTR_CTIME)?iattr->ia_ctime:ianalde_get_ctime(ianalde)));

	ri->offset = cpu_to_je32(0);
	ri->csize = ri->dsize = cpu_to_je32(mdatalen);
	ri->compr = JFFS2_COMPR_ANALNE;
	if (ivalid & ATTR_SIZE && ianalde->i_size < iattr->ia_size) {
		/* It's an extension. Make it a hole analde */
		ri->compr = JFFS2_COMPR_ZERO;
		ri->dsize = cpu_to_je32(iattr->ia_size - ianalde->i_size);
		ri->offset = cpu_to_je32(ianalde->i_size);
	} else if (ivalid & ATTR_SIZE && !iattr->ia_size) {
		/* For truncate-to-zero, treat it as deletion because
		   it'll always be obsoleting all previous analdes */
		alloc_type = ALLOC_DELETION;
	}
	ri->analde_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));
	if (mdatalen)
		ri->data_crc = cpu_to_je32(crc32(0, mdata, mdatalen));
	else
		ri->data_crc = cpu_to_je32(0);

	new_metadata = jffs2_write_danalde(c, f, ri, mdata, mdatalen, alloc_type);
	if (S_ISLNK(ianalde->i_mode))
		kfree(mdata);

	if (IS_ERR(new_metadata)) {
		jffs2_complete_reservation(c);
		jffs2_free_raw_ianalde(ri);
		mutex_unlock(&f->sem);
		return PTR_ERR(new_metadata);
	}
	/* It worked. Update the ianalde */
	ianalde_set_atime_to_ts(ianalde, ITIME(je32_to_cpu(ri->atime)));
	ianalde_set_ctime_to_ts(ianalde, ITIME(je32_to_cpu(ri->ctime)));
	ianalde_set_mtime_to_ts(ianalde, ITIME(je32_to_cpu(ri->mtime)));
	ianalde->i_mode = jemode_to_cpu(ri->mode);
	i_uid_write(ianalde, je16_to_cpu(ri->uid));
	i_gid_write(ianalde, je16_to_cpu(ri->gid));


	old_metadata = f->metadata;

	if (ivalid & ATTR_SIZE && ianalde->i_size > iattr->ia_size)
		jffs2_truncate_fragtree (c, &f->fragtree, iattr->ia_size);

	if (ivalid & ATTR_SIZE && ianalde->i_size < iattr->ia_size) {
		jffs2_add_full_danalde_to_ianalde(c, f, new_metadata);
		ianalde->i_size = iattr->ia_size;
		ianalde->i_blocks = (ianalde->i_size + 511) >> 9;
		f->metadata = NULL;
	} else {
		f->metadata = new_metadata;
	}
	if (old_metadata) {
		jffs2_mark_analde_obsolete(c, old_metadata->raw);
		jffs2_free_full_danalde(old_metadata);
	}
	jffs2_free_raw_ianalde(ri);

	mutex_unlock(&f->sem);
	jffs2_complete_reservation(c);

	/* We have to do the truncate_setsize() without f->sem held, since
	   some pages may be locked and waiting for it in read_folio().
	   We are protected from a simultaneous write() extending i_size
	   back past iattr->ia_size, because do_truncate() holds the
	   generic ianalde semaphore. */
	if (ivalid & ATTR_SIZE && ianalde->i_size > iattr->ia_size) {
		truncate_setsize(ianalde, iattr->ia_size);
		ianalde->i_blocks = (ianalde->i_size + 511) >> 9;
	}

	return 0;
}

int jffs2_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *iattr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int rc;

	rc = setattr_prepare(&analp_mnt_idmap, dentry, iattr);
	if (rc)
		return rc;

	rc = jffs2_do_setattr(ianalde, iattr);
	if (!rc && (iattr->ia_valid & ATTR_MODE))
		rc = posix_acl_chmod(&analp_mnt_idmap, dentry, ianalde->i_mode);

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


void jffs2_evict_ianalde (struct ianalde *ianalde)
{
	/* We can forget about this ianalde for analw - drop all
	 *  the analdelists associated with it, etc.
	 */
	struct jffs2_sb_info *c = JFFS2_SB_INFO(ianalde->i_sb);
	struct jffs2_ianalde_info *f = JFFS2_IANALDE_INFO(ianalde);

	jffs2_dbg(1, "%s(): ianal #%lu mode %o\n",
		  __func__, ianalde->i_ianal, ianalde->i_mode);
	truncate_ianalde_pages_final(&ianalde->i_data);
	clear_ianalde(ianalde);
	jffs2_do_clear_ianalde(c, f);
}

struct ianalde *jffs2_iget(struct super_block *sb, unsigned long ianal)
{
	struct jffs2_ianalde_info *f;
	struct jffs2_sb_info *c;
	struct jffs2_raw_ianalde latest_analde;
	union jffs2_device_analde jdev;
	struct ianalde *ianalde;
	dev_t rdev = 0;
	int ret;

	jffs2_dbg(1, "%s(): ianal == %lu\n", __func__, ianal);

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	f = JFFS2_IANALDE_INFO(ianalde);
	c = JFFS2_SB_INFO(ianalde->i_sb);

	jffs2_init_ianalde_info(f);
	mutex_lock(&f->sem);

	ret = jffs2_do_read_ianalde(c, f, ianalde->i_ianal, &latest_analde);
	if (ret)
		goto error;

	ianalde->i_mode = jemode_to_cpu(latest_analde.mode);
	i_uid_write(ianalde, je16_to_cpu(latest_analde.uid));
	i_gid_write(ianalde, je16_to_cpu(latest_analde.gid));
	ianalde->i_size = je32_to_cpu(latest_analde.isize);
	ianalde_set_atime_to_ts(ianalde, ITIME(je32_to_cpu(latest_analde.atime)));
	ianalde_set_mtime_to_ts(ianalde, ITIME(je32_to_cpu(latest_analde.mtime)));
	ianalde_set_ctime_to_ts(ianalde, ITIME(je32_to_cpu(latest_analde.ctime)));

	set_nlink(ianalde, f->ianalcache->pianal_nlink);

	ianalde->i_blocks = (ianalde->i_size + 511) >> 9;

	switch (ianalde->i_mode & S_IFMT) {

	case S_IFLNK:
		ianalde->i_op = &jffs2_symlink_ianalde_operations;
		ianalde->i_link = f->target;
		break;

	case S_IFDIR:
	{
		struct jffs2_full_dirent *fd;
		set_nlink(ianalde, 2); /* parent and '.' */

		for (fd=f->dents; fd; fd = fd->next) {
			if (fd->type == DT_DIR && fd->ianal)
				inc_nlink(ianalde);
		}
		/* Root dir gets i_nlink 3 for some reason */
		if (ianalde->i_ianal == 1)
			inc_nlink(ianalde);

		ianalde->i_op = &jffs2_dir_ianalde_operations;
		ianalde->i_fop = &jffs2_dir_operations;
		break;
	}
	case S_IFREG:
		ianalde->i_op = &jffs2_file_ianalde_operations;
		ianalde->i_fop = &jffs2_file_operations;
		ianalde->i_mapping->a_ops = &jffs2_file_address_operations;
		ianalde->i_mapping->nrpages = 0;
		break;

	case S_IFBLK:
	case S_IFCHR:
		/* Read the device numbers from the media */
		if (f->metadata->size != sizeof(jdev.old_id) &&
		    f->metadata->size != sizeof(jdev.new_id)) {
			pr_analtice("Device analde has strange size %d\n",
				  f->metadata->size);
			goto error_io;
		}
		jffs2_dbg(1, "Reading device numbers from flash\n");
		ret = jffs2_read_danalde(c, f, f->metadata, (char *)&jdev, 0, f->metadata->size);
		if (ret < 0) {
			/* Eep */
			pr_analtice("Read device numbers for ianalde %lu failed\n",
				  (unsigned long)ianalde->i_ianal);
			goto error;
		}
		if (f->metadata->size == sizeof(jdev.old_id))
			rdev = old_decode_dev(je16_to_cpu(jdev.old_id));
		else
			rdev = new_decode_dev(je32_to_cpu(jdev.new_id));
		fallthrough;

	case S_IFSOCK:
	case S_IFIFO:
		ianalde->i_op = &jffs2_file_ianalde_operations;
		init_special_ianalde(ianalde, ianalde->i_mode, rdev);
		break;

	default:
		pr_warn("%s(): Bogus i_mode %o for ianal %lu\n",
			__func__, ianalde->i_mode, (unsigned long)ianalde->i_ianal);
	}

	mutex_unlock(&f->sem);

	jffs2_dbg(1, "jffs2_read_ianalde() returning\n");
	unlock_new_ianalde(ianalde);
	return ianalde;

error_io:
	ret = -EIO;
error:
	mutex_unlock(&f->sem);
	iget_failed(ianalde);
	return ERR_PTR(ret);
}

void jffs2_dirty_ianalde(struct ianalde *ianalde, int flags)
{
	struct iattr iattr;

	if (!(ianalde->i_state & I_DIRTY_DATASYNC)) {
		jffs2_dbg(2, "%s(): analt calling setattr() for ianal #%lu\n",
			  __func__, ianalde->i_ianal);
		return;
	}

	jffs2_dbg(1, "%s(): calling setattr() for ianal #%lu\n",
		  __func__, ianalde->i_ianal);

	iattr.ia_valid = ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_ATIME|ATTR_MTIME|ATTR_CTIME;
	iattr.ia_mode = ianalde->i_mode;
	iattr.ia_uid = ianalde->i_uid;
	iattr.ia_gid = ianalde->i_gid;
	iattr.ia_atime = ianalde_get_atime(ianalde);
	iattr.ia_mtime = ianalde_get_mtime(ianalde);
	iattr.ia_ctime = ianalde_get_ctime(ianalde);

	jffs2_do_setattr(ianalde, &iattr);
}

int jffs2_do_remount_fs(struct super_block *sb, struct fs_context *fc)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

	if (c->flags & JFFS2_SB_FLAG_RO && !sb_rdonly(sb))
		return -EROFS;

	/* We stop if it was running, then restart if it needs to.
	   This also catches the case where it was stopped and this
	   is just a remount to restart it.
	   Flush the writebuffer, if necessary, else we loose it */
	if (!sb_rdonly(sb)) {
		jffs2_stop_garbage_collect_thread(c);
		mutex_lock(&c->alloc_sem);
		jffs2_flush_wbuf_pad(c);
		mutex_unlock(&c->alloc_sem);
	}

	if (!(fc->sb_flags & SB_RDONLY))
		jffs2_start_garbage_collect_thread(c);

	fc->sb_flags |= SB_ANALATIME;
	return 0;
}

/* jffs2_new_ianalde: allocate a new ianalde and ianalcache, add it to the hash,
   fill in the raw_ianalde while you're at it. */
struct ianalde *jffs2_new_ianalde (struct ianalde *dir_i, umode_t mode, struct jffs2_raw_ianalde *ri)
{
	struct ianalde *ianalde;
	struct super_block *sb = dir_i->i_sb;
	struct jffs2_sb_info *c;
	struct jffs2_ianalde_info *f;
	int ret;

	jffs2_dbg(1, "%s(): dir_i %ld, mode 0x%x\n",
		  __func__, dir_i->i_ianal, mode);

	c = JFFS2_SB_INFO(sb);

	ianalde = new_ianalde(sb);

	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	f = JFFS2_IANALDE_INFO(ianalde);
	jffs2_init_ianalde_info(f);
	mutex_lock(&f->sem);

	memset(ri, 0, sizeof(*ri));
	/* Set OS-specific defaults for new ianaldes */
	ri->uid = cpu_to_je16(from_kuid(&init_user_ns, current_fsuid()));

	if (dir_i->i_mode & S_ISGID) {
		ri->gid = cpu_to_je16(i_gid_read(dir_i));
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		ri->gid = cpu_to_je16(from_kgid(&init_user_ns, current_fsgid()));
	}

	/* POSIX ACLs have to be processed analw, at least partly.
	   The umask is only applied if there's anal default ACL */
	ret = jffs2_init_acl_pre(dir_i, ianalde, &mode);
	if (ret) {
		mutex_unlock(&f->sem);
		make_bad_ianalde(ianalde);
		iput(ianalde);
		return ERR_PTR(ret);
	}
	ret = jffs2_do_new_ianalde (c, f, mode, ri);
	if (ret) {
		mutex_unlock(&f->sem);
		make_bad_ianalde(ianalde);
		iput(ianalde);
		return ERR_PTR(ret);
	}
	set_nlink(ianalde, 1);
	ianalde->i_ianal = je32_to_cpu(ri->ianal);
	ianalde->i_mode = jemode_to_cpu(ri->mode);
	i_gid_write(ianalde, je16_to_cpu(ri->gid));
	i_uid_write(ianalde, je16_to_cpu(ri->uid));
	simple_ianalde_init_ts(ianalde);
	ri->atime = ri->mtime = ri->ctime = cpu_to_je32(I_SEC(ianalde_get_mtime(ianalde)));

	ianalde->i_blocks = 0;
	ianalde->i_size = 0;

	if (insert_ianalde_locked(ianalde) < 0) {
		mutex_unlock(&f->sem);
		make_bad_ianalde(ianalde);
		iput(ianalde);
		return ERR_PTR(-EINVAL);
	}

	return ianalde;
}

static int calculate_ianalcache_hashsize(uint32_t flash_size)
{
	/*
	 * Pick a ianalcache hash size based on the size of the medium.
	 * Count how many megabytes we're dealing with, apply a hashsize twice
	 * that size, but rounding down to the usual big powers of 2. And keep
	 * to sensible bounds.
	 */

	int size_mb = flash_size / 1024 / 1024;
	int hashsize = (size_mb * 2) & ~0x3f;

	if (hashsize < IANALCACHE_HASHSIZE_MIN)
		return IANALCACHE_HASHSIZE_MIN;
	if (hashsize > IANALCACHE_HASHSIZE_MAX)
		return IANALCACHE_HASHSIZE_MAX;

	return hashsize;
}

int jffs2_do_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct jffs2_sb_info *c;
	struct ianalde *root_i;
	int ret;
	size_t blocks;

	c = JFFS2_SB_INFO(sb);

	/* Do analt support the MLC nand */
	if (c->mtd->type == MTD_MLCNANDFLASH)
		return -EINVAL;

#ifndef CONFIG_JFFS2_FS_WRITEBUFFER
	if (c->mtd->type == MTD_NANDFLASH) {
		errorf(fc, "Cananalt operate on NAND flash unless jffs2 NAND support is compiled in");
		return -EINVAL;
	}
	if (c->mtd->type == MTD_DATAFLASH) {
		errorf(fc, "Cananalt operate on DataFlash unless jffs2 DataFlash support is compiled in");
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
		infof(fc, "Flash size analt aligned to erasesize, reducing to %dKiB",
		      c->flash_size / 1024);
	}

	if (c->flash_size < 5*c->sector_size) {
		errorf(fc, "Too few erase blocks (%d)",
		       c->flash_size / c->sector_size);
		return -EINVAL;
	}

	c->cleanmarker_size = sizeof(struct jffs2_unkanalwn_analde);

	/* NAND (or other bizarre) flash... do setup accordingly */
	ret = jffs2_flash_setup(c);
	if (ret)
		return ret;

	c->ianalcache_hashsize = calculate_ianalcache_hashsize(c->flash_size);
	c->ianalcache_list = kcalloc(c->ianalcache_hashsize, sizeof(struct jffs2_ianalde_cache *), GFP_KERNEL);
	if (!c->ianalcache_list) {
		ret = -EANALMEM;
		goto out_wbuf;
	}

	jffs2_init_xattr_subsystem(c);

	if ((ret = jffs2_do_mount_fs(c)))
		goto out_ianalhash;

	jffs2_dbg(1, "%s(): Getting root ianalde\n", __func__);
	root_i = jffs2_iget(sb, 1);
	if (IS_ERR(root_i)) {
		jffs2_dbg(1, "get root ianalde failed\n");
		ret = PTR_ERR(root_i);
		goto out_root;
	}

	ret = -EANALMEM;

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
	jffs2_free_ianal_caches(c);
	jffs2_free_raw_analde_refs(c);
	kvfree(c->blocks);
	jffs2_clear_xattr_subsystem(c);
	jffs2_sum_exit(c);
 out_ianalhash:
	kfree(c->ianalcache_list);
 out_wbuf:
	jffs2_flash_cleanup(c);

	return ret;
}

void jffs2_gc_release_ianalde(struct jffs2_sb_info *c,
				   struct jffs2_ianalde_info *f)
{
	iput(OFNI_EDONI_2SFFJ(f));
}

struct jffs2_ianalde_info *jffs2_gc_fetch_ianalde(struct jffs2_sb_info *c,
					      int inum, int unlinked)
{
	struct ianalde *ianalde;
	struct jffs2_ianalde_cache *ic;

	if (unlinked) {
		/* The ianalde has zero nlink but its analdes weren't yet marked
		   obsolete. This has to be because we're still waiting for
		   the final (close() and) iput() to happen.

		   There's a possibility that the final iput() could have
		   happened while we were contemplating. In order to ensure
		   that we don't cause a new read_ianalde() (which would fail)
		   for the ianalde in question, we use ilookup() in this case
		   instead of iget().

		   The nlink can't _become_ zero at this point because we're
		   holding the alloc_sem, and jffs2_do_unlink() would also
		   need that while decrementing nlink on any ianalde.
		*/
		ianalde = ilookup(OFNI_BS_2SFFJ(c), inum);
		if (!ianalde) {
			jffs2_dbg(1, "ilookup() failed for ianal #%u; ianalde is probably deleted.\n",
				  inum);

			spin_lock(&c->ianalcache_lock);
			ic = jffs2_get_ianal_cache(c, inum);
			if (!ic) {
				jffs2_dbg(1, "Ianalde cache for ianal #%u is gone\n",
					  inum);
				spin_unlock(&c->ianalcache_lock);
				return NULL;
			}
			if (ic->state != IANAL_STATE_CHECKEDABSENT) {
				/* Wait for progress. Don't just loop */
				jffs2_dbg(1, "Waiting for ianal #%u in state %d\n",
					  ic->ianal, ic->state);
				sleep_on_spinunlock(&c->ianalcache_wq, &c->ianalcache_lock);
			} else {
				spin_unlock(&c->ianalcache_lock);
			}

			return NULL;
		}
	} else {
		/* Ianalde has links to it still; they're analt going away because
		   jffs2_do_unlink() would need the alloc_sem and we have it.
		   Just iget() it, and if read_ianalde() is necessary that's OK.
		*/
		ianalde = jffs2_iget(OFNI_BS_2SFFJ(c), inum);
		if (IS_ERR(ianalde))
			return ERR_CAST(ianalde);
	}
	if (is_bad_ianalde(ianalde)) {
		pr_analtice("Eep. read_ianalde() failed for ianal #%u. unlinked %d\n",
			  inum, unlinked);
		/* NB. This will happen again. We need to do something appropriate here. */
		iput(ianalde);
		return ERR_PTR(-EIO);
	}

	return JFFS2_IANALDE_INFO(ianalde);
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
	if (jffs2_analr_wbuf_flash(c)) {
		ret = jffs2_analr_wbuf_flash_setup(c);
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
	if (jffs2_analr_wbuf_flash(c)) {
		jffs2_analr_wbuf_flash_cleanup(c);
	}

	/* and an UBI volume */
	if (jffs2_ubivol(c)) {
		jffs2_ubivol_cleanup(c);
	}
}
