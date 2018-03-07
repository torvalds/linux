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
#include <linux/list.h>
#include <linux/mtd/mtd.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/vfs.h>
#include <linux/crc32.h>
#include "nodelist.h"

static int jffs2_flash_setup(struct jffs2_sb_info *c);

int jffs2_do_setattr (struct inode *inode, struct iattr *iattr)
{
	struct jffs2_full_dnode *old_metadata, *new_metadata;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_raw_inode *ri;
	union jffs2_device_node dev;
	unsigned char *mdata = NULL;
	int mdatalen = 0;
	unsigned int ivalid;
	uint32_t alloclen;
	int ret;
	int alloc_type = ALLOC_NORMAL;

	jffs2_dbg(1, "%s(): ino #%lu\n", __func__, inode->i_ino);

	/* Special cases - we don't want more than one data node
	   for these types on the medium at any time. So setattr
	   must read the original data associated with the node
	   (i.e. the device numbers or the target name) and write
	   it out again with the appropriate data attached */
	if (S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode)) {
		/* For these, we don't actually need to read the old node */
		mdatalen = jffs2_encode_dev(&dev, inode->i_rdev);
		mdata = (char *)&dev;
		jffs2_dbg(1, "%s(): Writing %d bytes of kdev_t\n",
			  __func__, mdatalen);
	} else if (S_ISLNK(inode->i_mode)) {
		mutex_lock(&f->sem);
		mdatalen = f->metadata->size;
		mdata = kmalloc(f->metadata->size, GFP_USER);
		if (!mdata) {
			mutex_unlock(&f->sem);
			return -ENOMEM;
		}
		ret = jffs2_read_dnode(c, f, f->metadata, mdata, 0, mdatalen);
		if (ret) {
			mutex_unlock(&f->sem);
			kfree(mdata);
			return ret;
		}
		mutex_unlock(&f->sem);
		jffs2_dbg(1, "%s(): Writing %d bytes of symlink target\n",
			  __func__, mdatalen);
	}

	ri = jffs2_alloc_raw_inode();
	if (!ri) {
		if (S_ISLNK(inode->i_mode))
			kfree(mdata);
		return -ENOMEM;
	}

	ret = jffs2_reserve_space(c, sizeof(*ri) + mdatalen, &alloclen,
				  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);
	if (ret) {
		jffs2_free_raw_inode(ri);
		if (S_ISLNK(inode->i_mode))
			 kfree(mdata);
		return ret;
	}
	mutex_lock(&f->sem);
	ivalid = iattr->ia_valid;

	ri->magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
	ri->nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
	ri->totlen = cpu_to_je32(sizeof(*ri) + mdatalen);
	ri->hdr_crc = cpu_to_je32(crc32(0, ri, sizeof(struct jffs2_unknown_node)-4));

	ri->ino = cpu_to_je32(inode->i_ino);
	ri->version = cpu_to_je32(++f->highest_version);

	ri->uid = cpu_to_je16((ivalid & ATTR_UID)?
		from_kuid(&init_user_ns, iattr->ia_uid):i_uid_read(inode));
	ri->gid = cpu_to_je16((ivalid & ATTR_GID)?
		from_kgid(&init_user_ns, iattr->ia_gid):i_gid_read(inode));

	if (ivalid & ATTR_MODE)
		ri->mode = cpu_to_jemode(iattr->ia_mode);
	else
		ri->mode = cpu_to_jemode(inode->i_mode);


	ri->isize = cpu_to_je32((ivalid & ATTR_SIZE)?iattr->ia_size:inode->i_size);
	ri->atime = cpu_to_je32(I_SEC((ivalid & ATTR_ATIME)?iattr->ia_atime:inode->i_atime));
	ri->mtime = cpu_to_je32(I_SEC((ivalid & ATTR_MTIME)?iattr->ia_mtime:inode->i_mtime));
	ri->ctime = cpu_to_je32(I_SEC((ivalid & ATTR_CTIME)?iattr->ia_ctime:inode->i_ctime));

	ri->offset = cpu_to_je32(0);
	ri->csize = ri->dsize = cpu_to_je32(mdatalen);
	ri->compr = JFFS2_COMPR_NONE;
	if (ivalid & ATTR_SIZE && inode->i_size < iattr->ia_size) {
		/* It's an extension. Make it a hole node */
		ri->compr = JFFS2_COMPR_ZERO;
		ri->dsize = cpu_to_je32(iattr->ia_size - inode->i_size);
		ri->offset = cpu_to_je32(inode->i_size);
	} else if (ivalid & ATTR_SIZE && !iattr->ia_size) {
		/* For truncate-to-zero, treat it as deletion because
		   it'll always be obsoleting all previous nodes */
		alloc_type = ALLOC_DELETION;
	}
	ri->node_crc = cpu_to_je32(crc32(0, ri, sizeof(*ri)-8));
	if (mdatalen)
		ri->data_crc = cpu_to_je32(crc32(0, mdata, mdatalen));
	else
		ri->data_crc = cpu_to_je32(0);

	new_metadata = jffs2_write_dnode(c, f, ri, mdata, mdatalen, alloc_type);
	if (S_ISLNK(inode->i_mode))
		kfree(mdata);

	if (IS_ERR(new_metadata)) {
		jffs2_complete_reservation(c);
		jffs2_free_raw_inode(ri);
		mutex_unlock(&f->sem);
		return PTR_ERR(new_metadata);
	}
	/* It worked. Update the inode */
	inode->i_atime = ITIME(je32_to_cpu(ri->atime));
	inode->i_ctime = ITIME(je32_to_cpu(ri->ctime));
	inode->i_mtime = ITIME(je32_to_cpu(ri->mtime));
	inode->i_mode = jemode_to_cpu(ri->mode);
	i_uid_write(inode, je16_to_cpu(ri->uid));
	i_gid_write(inode, je16_to_cpu(ri->gid));


	old_metadata = f->metadata;

	if (ivalid & ATTR_SIZE && inode->i_size > iattr->ia_size)
		jffs2_truncate_fragtree (c, &f->fragtree, iattr->ia_size);

	if (ivalid & ATTR_SIZE && inode->i_size < iattr->ia_size) {
		jffs2_add_full_dnode_to_inode(c, f, new_metadata);
		inode->i_size = iattr->ia_size;
		inode->i_blocks = (inode->i_size + 511) >> 9;
		f->metadata = NULL;
	} else {
		f->metadata = new_metadata;
	}
	if (old_metadata) {
		jffs2_mark_node_obsolete(c, old_metadata->raw);
		jffs2_free_full_dnode(old_metadata);
	}
	jffs2_free_raw_inode(ri);

	mutex_unlock(&f->sem);
	jffs2_complete_reservation(c);

	/* We have to do the truncate_setsize() without f->sem held, since
	   some pages may be locked and waiting for it in readpage().
	   We are protected from a simultaneous write() extending i_size
	   back past iattr->ia_size, because do_truncate() holds the
	   generic inode semaphore. */
	if (ivalid & ATTR_SIZE && inode->i_size > iattr->ia_size) {
		truncate_setsize(inode, iattr->ia_size);
		inode->i_blocks = (inode->i_size + 511) >> 9;
	}	

	return 0;
}

int jffs2_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int rc;

	rc = setattr_prepare(dentry, iattr);
	if (rc)
		return rc;

	rc = jffs2_do_setattr(inode, iattr);
	if (!rc && (iattr->ia_valid & ATTR_MODE))
		rc = posix_acl_chmod(inode, inode->i_mode);

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


void jffs2_evict_inode (struct inode *inode)
{
	/* We can forget about this inode for now - drop all
	 *  the nodelists associated with it, etc.
	 */
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);

	jffs2_dbg(1, "%s(): ino #%lu mode %o\n",
		  __func__, inode->i_ino, inode->i_mode);
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	jffs2_do_clear_inode(c, f);
}

struct inode *jffs2_iget(struct super_block *sb, unsigned long ino)
{
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	struct jffs2_raw_inode latest_node;
	union jffs2_device_node jdev;
	struct inode *inode;
	dev_t rdev = 0;
	int ret;

	jffs2_dbg(1, "%s(): ino == %lu\n", __func__, ino);

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	f = JFFS2_INODE_INFO(inode);
	c = JFFS2_SB_INFO(inode->i_sb);

	jffs2_init_inode_info(f);
	mutex_lock(&f->sem);

	ret = jffs2_do_read_inode(c, f, inode->i_ino, &latest_node);
	if (ret)
		goto error;

	inode->i_mode = jemode_to_cpu(latest_node.mode);
	i_uid_write(inode, je16_to_cpu(latest_node.uid));
	i_gid_write(inode, je16_to_cpu(latest_node.gid));
	inode->i_size = je32_to_cpu(latest_node.isize);
	inode->i_atime = ITIME(je32_to_cpu(latest_node.atime));
	inode->i_mtime = ITIME(je32_to_cpu(latest_node.mtime));
	inode->i_ctime = ITIME(je32_to_cpu(latest_node.ctime));

	set_nlink(inode, f->inocache->pino_nlink);

	inode->i_blocks = (inode->i_size + 511) >> 9;

	switch (inode->i_mode & S_IFMT) {

	case S_IFLNK:
		inode->i_op = &jffs2_symlink_inode_operations;
		inode->i_link = f->target;
		break;

	case S_IFDIR:
	{
		struct jffs2_full_dirent *fd;
		set_nlink(inode, 2); /* parent and '.' */

		for (fd=f->dents; fd; fd = fd->next) {
			if (fd->type == DT_DIR && fd->ino)
				inc_nlink(inode);
		}
		/* Root dir gets i_nlink 3 for some reason */
		if (inode->i_ino == 1)
			inc_nlink(inode);

		inode->i_op = &jffs2_dir_inode_operations;
		inode->i_fop = &jffs2_dir_operations;
		break;
	}
	case S_IFREG:
		inode->i_op = &jffs2_file_inode_operations;
		inode->i_fop = &jffs2_file_operations;
		inode->i_mapping->a_ops = &jffs2_file_address_operations;
		inode->i_mapping->nrpages = 0;
		break;

	case S_IFBLK:
	case S_IFCHR:
		/* Read the device numbers from the media */
		if (f->metadata->size != sizeof(jdev.old_id) &&
		    f->metadata->size != sizeof(jdev.new_id)) {
			pr_notice("Device node has strange size %d\n",
				  f->metadata->size);
			goto error_io;
		}
		jffs2_dbg(1, "Reading device numbers from flash\n");
		ret = jffs2_read_dnode(c, f, f->metadata, (char *)&jdev, 0, f->metadata->size);
		if (ret < 0) {
			/* Eep */
			pr_notice("Read device numbers for inode %lu failed\n",
				  (unsigned long)inode->i_ino);
			goto error;
		}
		if (f->metadata->size == sizeof(jdev.old_id))
			rdev = old_decode_dev(je16_to_cpu(jdev.old_id));
		else
			rdev = new_decode_dev(je32_to_cpu(jdev.new_id));

	case S_IFSOCK:
	case S_IFIFO:
		inode->i_op = &jffs2_file_inode_operations;
		init_special_inode(inode, inode->i_mode, rdev);
		break;

	default:
		pr_warn("%s(): Bogus i_mode %o for ino %lu\n",
			__func__, inode->i_mode, (unsigned long)inode->i_ino);
	}

	mutex_unlock(&f->sem);

	jffs2_dbg(1, "jffs2_read_inode() returning\n");
	unlock_new_inode(inode);
	return inode;

error_io:
	ret = -EIO;
error:
	mutex_unlock(&f->sem);
	jffs2_do_clear_inode(c, f);
	iget_failed(inode);
	return ERR_PTR(ret);
}

void jffs2_dirty_inode(struct inode *inode, int flags)
{
	struct iattr iattr;

	if (!(inode->i_state & I_DIRTY_DATASYNC)) {
		jffs2_dbg(2, "%s(): not calling setattr() for ino #%lu\n",
			  __func__, inode->i_ino);
		return;
	}

	jffs2_dbg(1, "%s(): calling setattr() for ino #%lu\n",
		  __func__, inode->i_ino);

	iattr.ia_valid = ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_ATIME|ATTR_MTIME|ATTR_CTIME;
	iattr.ia_mode = inode->i_mode;
	iattr.ia_uid = inode->i_uid;
	iattr.ia_gid = inode->i_gid;
	iattr.ia_atime = inode->i_atime;
	iattr.ia_mtime = inode->i_mtime;
	iattr.ia_ctime = inode->i_ctime;

	jffs2_do_setattr(inode, &iattr);
}

int jffs2_do_remount_fs(struct super_block *sb, int *flags, char *data)
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

	if (!(*flags & SB_RDONLY))
		jffs2_start_garbage_collect_thread(c);

	*flags |= SB_NOATIME;
	return 0;
}

/* jffs2_new_inode: allocate a new inode and inocache, add it to the hash,
   fill in the raw_inode while you're at it. */
struct inode *jffs2_new_inode (struct inode *dir_i, umode_t mode, struct jffs2_raw_inode *ri)
{
	struct inode *inode;
	struct super_block *sb = dir_i->i_sb;
	struct jffs2_sb_info *c;
	struct jffs2_inode_info *f;
	int ret;

	jffs2_dbg(1, "%s(): dir_i %ld, mode 0x%x\n",
		  __func__, dir_i->i_ino, mode);

	c = JFFS2_SB_INFO(sb);

	inode = new_inode(sb);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	f = JFFS2_INODE_INFO(inode);
	jffs2_init_inode_info(f);
	mutex_lock(&f->sem);

	memset(ri, 0, sizeof(*ri));
	/* Set OS-specific defaults for new inodes */
	ri->uid = cpu_to_je16(from_kuid(&init_user_ns, current_fsuid()));

	if (dir_i->i_mode & S_ISGID) {
		ri->gid = cpu_to_je16(i_gid_read(dir_i));
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		ri->gid = cpu_to_je16(from_kgid(&init_user_ns, current_fsgid()));
	}

	/* POSIX ACLs have to be processed now, at least partly.
	   The umask is only applied if there's no default ACL */
	ret = jffs2_init_acl_pre(dir_i, inode, &mode);
	if (ret) {
		mutex_unlock(&f->sem);
		make_bad_inode(inode);
		iput(inode);
		return ERR_PTR(ret);
	}
	ret = jffs2_do_new_inode (c, f, mode, ri);
	if (ret) {
		mutex_unlock(&f->sem);
		make_bad_inode(inode);
		iput(inode);
		return ERR_PTR(ret);
	}
	set_nlink(inode, 1);
	inode->i_ino = je32_to_cpu(ri->ino);
	inode->i_mode = jemode_to_cpu(ri->mode);
	i_gid_write(inode, je16_to_cpu(ri->gid));
	i_uid_write(inode, je16_to_cpu(ri->uid));
	inode->i_atime = inode->i_ctime = inode->i_mtime = current_time(inode);
	ri->atime = ri->mtime = ri->ctime = cpu_to_je32(I_SEC(inode->i_mtime));

	inode->i_blocks = 0;
	inode->i_size = 0;

	if (insert_inode_locked(inode) < 0) {
		mutex_unlock(&f->sem);
		make_bad_inode(inode);
		iput(inode);
		return ERR_PTR(-EINVAL);
	}

	return inode;
}

static int calculate_inocache_hashsize(uint32_t flash_size)
{
	/*
	 * Pick a inocache hash size based on the size of the medium.
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

int jffs2_do_fill_super(struct super_block *sb, void *data, int silent)
{
	struct jffs2_sb_info *c;
	struct inode *root_i;
	int ret;
	size_t blocks;

	c = JFFS2_SB_INFO(sb);

	/* Do not support the MLC nand */
	if (c->mtd->type == MTD_MLCNANDFLASH)
		return -EINVAL;

#ifndef CONFIG_JFFS2_FS_WRITEBUFFER
	if (c->mtd->type == MTD_NANDFLASH) {
		pr_err("Cannot operate on NAND flash unless jffs2 NAND support is compiled in\n");
		return -EINVAL;
	}
	if (c->mtd->type == MTD_DATAFLASH) {
		pr_err("Cannot operate on DataFlash unless jffs2 DataFlash support is compiled in\n");
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
		pr_info("Flash size not aligned to erasesize, reducing to %dKiB\n",
			c->flash_size / 1024);
	}

	if (c->flash_size < 5*c->sector_size) {
		pr_err("Too few erase blocks (%d)\n",
		       c->flash_size / c->sector_size);
		return -EINVAL;
	}

	c->cleanmarker_size = sizeof(struct jffs2_unknown_node);

	/* NAND (or other bizarre) flash... do setup accordingly */
	ret = jffs2_flash_setup(c);
	if (ret)
		return ret;

	c->inocache_hashsize = calculate_inocache_hashsize(c->flash_size);
	c->inocache_list = kcalloc(c->inocache_hashsize, sizeof(struct jffs2_inode_cache *), GFP_KERNEL);
	if (!c->inocache_list) {
		ret = -ENOMEM;
		goto out_wbuf;
	}

	jffs2_init_xattr_subsystem(c);

	if ((ret = jffs2_do_mount_fs(c)))
		goto out_inohash;

	jffs2_dbg(1, "%s(): Getting root inode\n", __func__);
	root_i = jffs2_iget(sb, 1);
	if (IS_ERR(root_i)) {
		jffs2_dbg(1, "get root inode failed\n");
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
	if (!sb_rdonly(sb))
		jffs2_start_garbage_collect_thread(c);
	return 0;

out_root:
	jffs2_free_ino_caches(c);
	jffs2_free_raw_node_refs(c);
	kvfree(c->blocks);
 out_inohash:
	jffs2_clear_xattr_subsystem(c);
	kfree(c->inocache_list);
 out_wbuf:
	jffs2_flash_cleanup(c);

	return ret;
}

void jffs2_gc_release_inode(struct jffs2_sb_info *c,
				   struct jffs2_inode_info *f)
{
	iput(OFNI_EDONI_2SFFJ(f));
}

struct jffs2_inode_info *jffs2_gc_fetch_inode(struct jffs2_sb_info *c,
					      int inum, int unlinked)
{
	struct inode *inode;
	struct jffs2_inode_cache *ic;

	if (unlinked) {
		/* The inode has zero nlink but its nodes weren't yet marked
		   obsolete. This has to be because we're still waiting for
		   the final (close() and) iput() to happen.

		   There's a possibility that the final iput() could have
		   happened while we were contemplating. In order to ensure
		   that we don't cause a new read_inode() (which would fail)
		   for the inode in question, we use ilookup() in this case
		   instead of iget().

		   The nlink can't _become_ zero at this point because we're
		   holding the alloc_sem, and jffs2_do_unlink() would also
		   need that while decrementing nlink on any inode.
		*/
		inode = ilookup(OFNI_BS_2SFFJ(c), inum);
		if (!inode) {
			jffs2_dbg(1, "ilookup() failed for ino #%u; inode is probably deleted.\n",
				  inum);

			spin_lock(&c->inocache_lock);
			ic = jffs2_get_ino_cache(c, inum);
			if (!ic) {
				jffs2_dbg(1, "Inode cache for ino #%u is gone\n",
					  inum);
				spin_unlock(&c->inocache_lock);
				return NULL;
			}
			if (ic->state != INO_STATE_CHECKEDABSENT) {
				/* Wait for progress. Don't just loop */
				jffs2_dbg(1, "Waiting for ino #%u in state %d\n",
					  ic->ino, ic->state);
				sleep_on_spinunlock(&c->inocache_wq, &c->inocache_lock);
			} else {
				spin_unlock(&c->inocache_lock);
			}

			return NULL;
		}
	} else {
		/* Inode has links to it still; they're not going away because
		   jffs2_do_unlink() would need the alloc_sem and we have it.
		   Just iget() it, and if read_inode() is necessary that's OK.
		*/
		inode = jffs2_iget(OFNI_BS_2SFFJ(c), inum);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	if (is_bad_inode(inode)) {
		pr_notice("Eep. read_inode() failed for ino #%u. unlinked %d\n",
			  inum, unlinked);
		/* NB. This will happen again. We need to do something appropriate here. */
		iput(inode);
		return ERR_PTR(-EIO);
	}

	return JFFS2_INODE_INFO(inode);
}

unsigned char *jffs2_gc_fetch_page(struct jffs2_sb_info *c,
				   struct jffs2_inode_info *f,
				   unsigned long offset,
				   unsigned long *priv)
{
	struct inode *inode = OFNI_EDONI_2SFFJ(f);
	struct page *pg;

	pg = read_cache_page(inode->i_mapping, offset >> PAGE_SHIFT,
			     (void *)jffs2_do_readpage_unlock, inode);
	if (IS_ERR(pg))
		return (void *)pg;

	*priv = (unsigned long)pg;
	return kmap(pg);
}

void jffs2_gc_release_page(struct jffs2_sb_info *c,
			   unsigned char *ptr,
			   unsigned long *priv)
{
	struct page *pg = (void *)*priv;

	kunmap(pg);
	put_page(pg);
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
	if (jffs2_nor_wbuf_flash(c)) {
		ret = jffs2_nor_wbuf_flash_setup(c);
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
	if (jffs2_nor_wbuf_flash(c)) {
		jffs2_nor_wbuf_flash_cleanup(c);
	}

	/* and an UBI volume */
	if (jffs2_ubivol(c)) {
		jffs2_ubivol_cleanup(c);
	}
}
