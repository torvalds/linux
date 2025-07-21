// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/crc32c.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/exportfs.h>
#include <linux/backing-dev.h>
#include "xattr.h"

#define CREATE_TRACE_POINTS
#include <trace/events/erofs.h>

static struct kmem_cache *erofs_inode_cachep __read_mostly;

void _erofs_printk(struct super_block *sb, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int level;

	va_start(args, fmt);

	level = printk_get_level(fmt);
	vaf.fmt = printk_skip_level(fmt);
	vaf.va = &args;
	if (sb)
		printk("%c%cerofs (device %s): %pV",
				KERN_SOH_ASCII, level, sb->s_id, &vaf);
	else
		printk("%c%cerofs: %pV", KERN_SOH_ASCII, level, &vaf);
	va_end(args);
}

static int erofs_superblock_csum_verify(struct super_block *sb, void *sbdata)
{
	struct erofs_super_block *dsb = sbdata + EROFS_SUPER_OFFSET;
	u32 len = 1 << EROFS_SB(sb)->blkszbits, crc;

	if (len > EROFS_SUPER_OFFSET)
		len -= EROFS_SUPER_OFFSET;
	len -= offsetof(struct erofs_super_block, checksum) +
			sizeof(dsb->checksum);

	/* skip .magic(pre-verified) and .checksum(0) fields */
	crc = crc32c(0x5045B54A, (&dsb->checksum) + 1, len);
	if (crc == le32_to_cpu(dsb->checksum))
		return 0;
	erofs_err(sb, "invalid checksum 0x%08x, 0x%08x expected",
		  crc, le32_to_cpu(dsb->checksum));
	return -EBADMSG;
}

static void erofs_inode_init_once(void *ptr)
{
	struct erofs_inode *vi = ptr;

	inode_init_once(&vi->vfs_inode);
}

static struct inode *erofs_alloc_inode(struct super_block *sb)
{
	struct erofs_inode *vi =
		alloc_inode_sb(sb, erofs_inode_cachep, GFP_KERNEL);

	if (!vi)
		return NULL;

	/* zero out everything except vfs_inode */
	memset(vi, 0, offsetof(struct erofs_inode, vfs_inode));
	return &vi->vfs_inode;
}

static void erofs_free_inode(struct inode *inode)
{
	struct erofs_inode *vi = EROFS_I(inode);

	if (inode->i_op == &erofs_fast_symlink_iops)
		kfree(inode->i_link);
	kfree(vi->xattr_shared_xattrs);
	kmem_cache_free(erofs_inode_cachep, vi);
}

/* read variable-sized metadata, offset will be aligned by 4-byte */
void *erofs_read_metadata(struct super_block *sb, struct erofs_buf *buf,
			  erofs_off_t *offset, int *lengthp)
{
	u8 *buffer, *ptr;
	int len, i, cnt;

	*offset = round_up(*offset, 4);
	ptr = erofs_bread(buf, *offset, true);
	if (IS_ERR(ptr))
		return ptr;

	len = le16_to_cpu(*(__le16 *)ptr);
	if (!len)
		len = U16_MAX + 1;
	buffer = kmalloc(len, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);
	*offset += sizeof(__le16);
	*lengthp = len;

	for (i = 0; i < len; i += cnt) {
		cnt = min_t(int, sb->s_blocksize - erofs_blkoff(sb, *offset),
			    len - i);
		ptr = erofs_bread(buf, *offset, true);
		if (IS_ERR(ptr)) {
			kfree(buffer);
			return ptr;
		}
		memcpy(buffer + i, ptr, cnt);
		*offset += cnt;
	}
	return buffer;
}

#ifndef CONFIG_EROFS_FS_ZIP
static int z_erofs_parse_cfgs(struct super_block *sb,
			      struct erofs_super_block *dsb)
{
	if (!dsb->u1.available_compr_algs)
		return 0;

	erofs_err(sb, "compression disabled, unable to mount compressed EROFS");
	return -EOPNOTSUPP;
}
#endif

static int erofs_init_device(struct erofs_buf *buf, struct super_block *sb,
			     struct erofs_device_info *dif, erofs_off_t *pos)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_fscache *fscache;
	struct erofs_deviceslot *dis;
	struct file *file;

	dis = erofs_read_metabuf(buf, sb, *pos, false);
	if (IS_ERR(dis))
		return PTR_ERR(dis);

	if (!sbi->devs->flatdev && !dif->path) {
		if (!dis->tag[0]) {
			erofs_err(sb, "empty device tag @ pos %llu", *pos);
			return -EINVAL;
		}
		dif->path = kmemdup_nul(dis->tag, sizeof(dis->tag), GFP_KERNEL);
		if (!dif->path)
			return -ENOMEM;
	}

	if (erofs_is_fscache_mode(sb)) {
		fscache = erofs_fscache_register_cookie(sb, dif->path, 0);
		if (IS_ERR(fscache))
			return PTR_ERR(fscache);
		dif->fscache = fscache;
	} else if (!sbi->devs->flatdev) {
		file = erofs_is_fileio_mode(sbi) ?
				filp_open(dif->path, O_RDONLY | O_LARGEFILE, 0) :
				bdev_file_open_by_path(dif->path,
						BLK_OPEN_READ, sb->s_type, NULL);
		if (IS_ERR(file)) {
			if (file == ERR_PTR(-ENOTBLK))
				return -EINVAL;
			return PTR_ERR(file);
		}

		if (!erofs_is_fileio_mode(sbi)) {
			dif->dax_dev = fs_dax_get_by_bdev(file_bdev(file),
					&dif->dax_part_off, NULL, NULL);
		} else if (!S_ISREG(file_inode(file)->i_mode)) {
			fput(file);
			return -EINVAL;
		}
		dif->file = file;
	}

	dif->blocks = le32_to_cpu(dis->blocks_lo);
	dif->uniaddr = le32_to_cpu(dis->uniaddr_lo);
	sbi->total_blocks += dif->blocks;
	*pos += EROFS_DEVT_SLOT_SIZE;
	return 0;
}

static int erofs_scan_devices(struct super_block *sb,
			      struct erofs_super_block *dsb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	unsigned int ondisk_extradevs;
	erofs_off_t pos;
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	struct erofs_device_info *dif;
	int id, err = 0;

	sbi->total_blocks = sbi->dif0.blocks;
	if (!erofs_sb_has_device_table(sbi))
		ondisk_extradevs = 0;
	else
		ondisk_extradevs = le16_to_cpu(dsb->extra_devices);

	if (sbi->devs->extra_devices &&
	    ondisk_extradevs != sbi->devs->extra_devices) {
		erofs_err(sb, "extra devices don't match (ondisk %u, given %u)",
			  ondisk_extradevs, sbi->devs->extra_devices);
		return -EINVAL;
	}
	if (!ondisk_extradevs)
		return 0;

	if (!sbi->devs->extra_devices && !erofs_is_fscache_mode(sb))
		sbi->devs->flatdev = true;

	sbi->device_id_mask = roundup_pow_of_two(ondisk_extradevs + 1) - 1;
	pos = le16_to_cpu(dsb->devt_slotoff) * EROFS_DEVT_SLOT_SIZE;
	down_read(&sbi->devs->rwsem);
	if (sbi->devs->extra_devices) {
		idr_for_each_entry(&sbi->devs->tree, dif, id) {
			err = erofs_init_device(&buf, sb, dif, &pos);
			if (err)
				break;
		}
	} else {
		for (id = 0; id < ondisk_extradevs; id++) {
			dif = kzalloc(sizeof(*dif), GFP_KERNEL);
			if (!dif) {
				err = -ENOMEM;
				break;
			}

			err = idr_alloc(&sbi->devs->tree, dif, 0, 0, GFP_KERNEL);
			if (err < 0) {
				kfree(dif);
				break;
			}
			++sbi->devs->extra_devices;

			err = erofs_init_device(&buf, sb, dif, &pos);
			if (err)
				break;
		}
	}
	up_read(&sbi->devs->rwsem);
	erofs_put_metabuf(&buf);
	return err;
}

static int erofs_read_superblock(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	struct erofs_super_block *dsb;
	void *data;
	int ret;

	data = erofs_read_metabuf(&buf, sb, 0, false);
	if (IS_ERR(data)) {
		erofs_err(sb, "cannot read erofs superblock");
		return PTR_ERR(data);
	}

	dsb = (struct erofs_super_block *)(data + EROFS_SUPER_OFFSET);
	ret = -EINVAL;
	if (le32_to_cpu(dsb->magic) != EROFS_SUPER_MAGIC_V1) {
		erofs_err(sb, "cannot find valid erofs superblock");
		goto out;
	}

	sbi->blkszbits = dsb->blkszbits;
	if (sbi->blkszbits < 9 || sbi->blkszbits > PAGE_SHIFT) {
		erofs_err(sb, "blkszbits %u isn't supported", sbi->blkszbits);
		goto out;
	}
	if (dsb->dirblkbits) {
		erofs_err(sb, "dirblkbits %u isn't supported", dsb->dirblkbits);
		goto out;
	}

	sbi->feature_compat = le32_to_cpu(dsb->feature_compat);
	if (erofs_sb_has_sb_chksum(sbi)) {
		ret = erofs_superblock_csum_verify(sb, data);
		if (ret)
			goto out;
	}

	ret = -EINVAL;
	sbi->feature_incompat = le32_to_cpu(dsb->feature_incompat);
	if (sbi->feature_incompat & ~EROFS_ALL_FEATURE_INCOMPAT) {
		erofs_err(sb, "unidentified incompatible feature %x, please upgrade kernel",
			  sbi->feature_incompat & ~EROFS_ALL_FEATURE_INCOMPAT);
		goto out;
	}

	sbi->sb_size = 128 + dsb->sb_extslots * EROFS_SB_EXTSLOT_SIZE;
	if (sbi->sb_size > PAGE_SIZE - EROFS_SUPER_OFFSET) {
		erofs_err(sb, "invalid sb_extslots %u (more than a fs block)",
			  sbi->sb_size);
		goto out;
	}
	sbi->dif0.blocks = le32_to_cpu(dsb->blocks_lo);
	sbi->meta_blkaddr = le32_to_cpu(dsb->meta_blkaddr);
#ifdef CONFIG_EROFS_FS_XATTR
	sbi->xattr_blkaddr = le32_to_cpu(dsb->xattr_blkaddr);
	sbi->xattr_prefix_start = le32_to_cpu(dsb->xattr_prefix_start);
	sbi->xattr_prefix_count = dsb->xattr_prefix_count;
	sbi->xattr_filter_reserved = dsb->xattr_filter_reserved;
#endif
	sbi->islotbits = ilog2(sizeof(struct erofs_inode_compact));
	if (erofs_sb_has_48bit(sbi) && dsb->rootnid_8b) {
		sbi->root_nid = le64_to_cpu(dsb->rootnid_8b);
		sbi->dif0.blocks = (sbi->dif0.blocks << 32) |
				le16_to_cpu(dsb->rb.blocks_hi);
	} else {
		sbi->root_nid = le16_to_cpu(dsb->rb.rootnid_2b);
	}
	sbi->packed_nid = le64_to_cpu(dsb->packed_nid);
	if (erofs_sb_has_metabox(sbi)) {
		if (sbi->sb_size <= offsetof(struct erofs_super_block,
					     metabox_nid))
			return -EFSCORRUPTED;
		sbi->metabox_nid = le64_to_cpu(dsb->metabox_nid);
		if (sbi->metabox_nid & BIT_ULL(EROFS_DIRENT_NID_METABOX_BIT))
			return -EFSCORRUPTED;	/* self-loop detection */
	}
	sbi->inos = le64_to_cpu(dsb->inos);

	sbi->epoch = (s64)le64_to_cpu(dsb->epoch);
	sbi->fixed_nsec = le32_to_cpu(dsb->fixed_nsec);
	super_set_uuid(sb, (void *)dsb->uuid, sizeof(dsb->uuid));

	/* parse on-disk compression configurations */
	ret = z_erofs_parse_cfgs(sb, dsb);
	if (ret < 0)
		goto out;

	/* handle multiple devices */
	ret = erofs_scan_devices(sb, dsb);

	if (erofs_sb_has_48bit(sbi))
		erofs_info(sb, "EXPERIMENTAL 48-bit layout support in use. Use at your own risk!");
	if (erofs_sb_has_metabox(sbi))
		erofs_info(sb, "EXPERIMENTAL metadata compression support in use. Use at your own risk!");
	if (erofs_is_fscache_mode(sb))
		erofs_info(sb, "[deprecated] fscache-based on-demand read feature in use. Use at your own risk!");
out:
	erofs_put_metabuf(&buf);
	return ret;
}

static void erofs_default_options(struct erofs_sb_info *sbi)
{
#ifdef CONFIG_EROFS_FS_ZIP
	sbi->opt.cache_strategy = EROFS_ZIP_CACHE_READAROUND;
	sbi->opt.max_sync_decompress_pages = 3;
	sbi->opt.sync_decompress = EROFS_SYNC_DECOMPRESS_AUTO;
#endif
#ifdef CONFIG_EROFS_FS_XATTR
	set_opt(&sbi->opt, XATTR_USER);
#endif
#ifdef CONFIG_EROFS_FS_POSIX_ACL
	set_opt(&sbi->opt, POSIX_ACL);
#endif
}

enum {
	Opt_user_xattr, Opt_acl, Opt_cache_strategy, Opt_dax, Opt_dax_enum,
	Opt_device, Opt_fsid, Opt_domain_id, Opt_directio, Opt_fsoffset,
};

static const struct constant_table erofs_param_cache_strategy[] = {
	{"disabled",	EROFS_ZIP_CACHE_DISABLED},
	{"readahead",	EROFS_ZIP_CACHE_READAHEAD},
	{"readaround",	EROFS_ZIP_CACHE_READAROUND},
	{}
};

static const struct constant_table erofs_dax_param_enums[] = {
	{"always",	EROFS_MOUNT_DAX_ALWAYS},
	{"never",	EROFS_MOUNT_DAX_NEVER},
	{}
};

static const struct fs_parameter_spec erofs_fs_parameters[] = {
	fsparam_flag_no("user_xattr",	Opt_user_xattr),
	fsparam_flag_no("acl",		Opt_acl),
	fsparam_enum("cache_strategy",	Opt_cache_strategy,
		     erofs_param_cache_strategy),
	fsparam_flag("dax",             Opt_dax),
	fsparam_enum("dax",		Opt_dax_enum, erofs_dax_param_enums),
	fsparam_string("device",	Opt_device),
	fsparam_string("fsid",		Opt_fsid),
	fsparam_string("domain_id",	Opt_domain_id),
	fsparam_flag_no("directio",	Opt_directio),
	fsparam_u64("fsoffset",		Opt_fsoffset),
	{}
};

static bool erofs_fc_set_dax_mode(struct fs_context *fc, unsigned int mode)
{
#ifdef CONFIG_FS_DAX
	struct erofs_sb_info *sbi = fc->s_fs_info;

	switch (mode) {
	case EROFS_MOUNT_DAX_ALWAYS:
		set_opt(&sbi->opt, DAX_ALWAYS);
		clear_opt(&sbi->opt, DAX_NEVER);
		return true;
	case EROFS_MOUNT_DAX_NEVER:
		set_opt(&sbi->opt, DAX_NEVER);
		clear_opt(&sbi->opt, DAX_ALWAYS);
		return true;
	default:
		DBG_BUGON(1);
		return false;
	}
#else
	errorfc(fc, "dax options not supported");
	return false;
#endif
}

static int erofs_fc_parse_param(struct fs_context *fc,
				struct fs_parameter *param)
{
	struct erofs_sb_info *sbi = fc->s_fs_info;
	struct fs_parse_result result;
	struct erofs_device_info *dif;
	int opt, ret;

	opt = fs_parse(fc, erofs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_user_xattr:
#ifdef CONFIG_EROFS_FS_XATTR
		if (result.boolean)
			set_opt(&sbi->opt, XATTR_USER);
		else
			clear_opt(&sbi->opt, XATTR_USER);
#else
		errorfc(fc, "{,no}user_xattr options not supported");
#endif
		break;
	case Opt_acl:
#ifdef CONFIG_EROFS_FS_POSIX_ACL
		if (result.boolean)
			set_opt(&sbi->opt, POSIX_ACL);
		else
			clear_opt(&sbi->opt, POSIX_ACL);
#else
		errorfc(fc, "{,no}acl options not supported");
#endif
		break;
	case Opt_cache_strategy:
#ifdef CONFIG_EROFS_FS_ZIP
		sbi->opt.cache_strategy = result.uint_32;
#else
		errorfc(fc, "compression not supported, cache_strategy ignored");
#endif
		break;
	case Opt_dax:
		if (!erofs_fc_set_dax_mode(fc, EROFS_MOUNT_DAX_ALWAYS))
			return -EINVAL;
		break;
	case Opt_dax_enum:
		if (!erofs_fc_set_dax_mode(fc, result.uint_32))
			return -EINVAL;
		break;
	case Opt_device:
		dif = kzalloc(sizeof(*dif), GFP_KERNEL);
		if (!dif)
			return -ENOMEM;
		dif->path = kstrdup(param->string, GFP_KERNEL);
		if (!dif->path) {
			kfree(dif);
			return -ENOMEM;
		}
		down_write(&sbi->devs->rwsem);
		ret = idr_alloc(&sbi->devs->tree, dif, 0, 0, GFP_KERNEL);
		up_write(&sbi->devs->rwsem);
		if (ret < 0) {
			kfree(dif->path);
			kfree(dif);
			return ret;
		}
		++sbi->devs->extra_devices;
		break;
#ifdef CONFIG_EROFS_FS_ONDEMAND
	case Opt_fsid:
		kfree(sbi->fsid);
		sbi->fsid = kstrdup(param->string, GFP_KERNEL);
		if (!sbi->fsid)
			return -ENOMEM;
		break;
	case Opt_domain_id:
		kfree(sbi->domain_id);
		sbi->domain_id = kstrdup(param->string, GFP_KERNEL);
		if (!sbi->domain_id)
			return -ENOMEM;
		break;
#else
	case Opt_fsid:
	case Opt_domain_id:
		errorfc(fc, "%s option not supported", erofs_fs_parameters[opt].name);
		break;
#endif
	case Opt_directio:
#ifdef CONFIG_EROFS_FS_BACKED_BY_FILE
		if (result.boolean)
			set_opt(&sbi->opt, DIRECT_IO);
		else
			clear_opt(&sbi->opt, DIRECT_IO);
#else
		errorfc(fc, "%s option not supported", erofs_fs_parameters[opt].name);
#endif
		break;
	case Opt_fsoffset:
		sbi->dif0.fsoff = result.uint_64;
		break;
	}
	return 0;
}

static int erofs_encode_fh(struct inode *inode, u32 *fh, int *max_len,
			   struct inode *parent)
{
	erofs_nid_t nid = EROFS_I(inode)->nid;
	int len = parent ? 6 : 3;

	if (*max_len < len) {
		*max_len = len;
		return FILEID_INVALID;
	}

	fh[0] = (u32)(nid >> 32);
	fh[1] = (u32)(nid & 0xffffffff);
	fh[2] = inode->i_generation;

	if (parent) {
		nid = EROFS_I(parent)->nid;

		fh[3] = (u32)(nid >> 32);
		fh[4] = (u32)(nid & 0xffffffff);
		fh[5] = parent->i_generation;
	}

	*max_len = len;
	return parent ? FILEID_INO64_GEN_PARENT : FILEID_INO64_GEN;
}

static struct dentry *erofs_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if ((fh_type != FILEID_INO64_GEN &&
	     fh_type != FILEID_INO64_GEN_PARENT) || fh_len < 3)
		return NULL;

	return d_obtain_alias(erofs_iget(sb,
		((u64)fid->raw[0] << 32) | fid->raw[1]));
}

static struct dentry *erofs_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	if (fh_type != FILEID_INO64_GEN_PARENT || fh_len < 6)
		return NULL;

	return d_obtain_alias(erofs_iget(sb,
		((u64)fid->raw[3] << 32) | fid->raw[4]));
}

static struct dentry *erofs_get_parent(struct dentry *child)
{
	erofs_nid_t nid;
	unsigned int d_type;
	int err;

	err = erofs_namei(d_inode(child), &dotdot_name, &nid, &d_type);
	if (err)
		return ERR_PTR(err);
	return d_obtain_alias(erofs_iget(child->d_sb, nid));
}

static const struct export_operations erofs_export_ops = {
	.encode_fh = erofs_encode_fh,
	.fh_to_dentry = erofs_fh_to_dentry,
	.fh_to_parent = erofs_fh_to_parent,
	.get_parent = erofs_get_parent,
};

static void erofs_set_sysfs_name(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	if (sbi->domain_id)
		super_set_sysfs_name_generic(sb, "%s,%s", sbi->domain_id,
					     sbi->fsid);
	else if (sbi->fsid)
		super_set_sysfs_name_generic(sb, "%s", sbi->fsid);
	else if (erofs_is_fileio_mode(sbi))
		super_set_sysfs_name_generic(sb, "%s",
					     bdi_dev_name(sb->s_bdi));
	else
		super_set_sysfs_name_id(sb);
}

static int erofs_fc_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct inode *inode;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	int err;

	sb->s_magic = EROFS_SUPER_MAGIC;
	sb->s_flags |= SB_RDONLY | SB_NOATIME;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_op = &erofs_sops;

	sbi->blkszbits = PAGE_SHIFT;
	if (!sb->s_bdev) {
		sb->s_blocksize = PAGE_SIZE;
		sb->s_blocksize_bits = PAGE_SHIFT;

		if (erofs_is_fscache_mode(sb)) {
			err = erofs_fscache_register_fs(sb);
			if (err)
				return err;
		}
		err = super_setup_bdi(sb);
		if (err)
			return err;
	} else {
		if (!sb_set_blocksize(sb, PAGE_SIZE)) {
			errorfc(fc, "failed to set initial blksize");
			return -EINVAL;
		}

		sbi->dif0.dax_dev = fs_dax_get_by_bdev(sb->s_bdev,
				&sbi->dif0.dax_part_off, NULL, NULL);
	}

	err = erofs_read_superblock(sb);
	if (err)
		return err;

	if (sb->s_blocksize_bits != sbi->blkszbits) {
		if (erofs_is_fscache_mode(sb)) {
			errorfc(fc, "unsupported blksize for fscache mode");
			return -EINVAL;
		}

		if (erofs_is_fileio_mode(sbi)) {
			sb->s_blocksize = 1 << sbi->blkszbits;
			sb->s_blocksize_bits = sbi->blkszbits;
		} else if (!sb_set_blocksize(sb, 1 << sbi->blkszbits)) {
			errorfc(fc, "failed to set erofs blksize");
			return -EINVAL;
		}
	}

	if (sbi->dif0.fsoff) {
		if (sbi->dif0.fsoff & (sb->s_blocksize - 1))
			return invalfc(fc, "fsoffset %llu is not aligned to block size %lu",
				       sbi->dif0.fsoff, sb->s_blocksize);
		if (erofs_is_fscache_mode(sb))
			return invalfc(fc, "cannot use fsoffset in fscache mode");
	}

	if (test_opt(&sbi->opt, DAX_ALWAYS)) {
		if (!sbi->dif0.dax_dev) {
			errorfc(fc, "DAX unsupported by block device. Turning off DAX.");
			clear_opt(&sbi->opt, DAX_ALWAYS);
		} else if (sbi->blkszbits != PAGE_SHIFT) {
			errorfc(fc, "unsupported blocksize for DAX");
			clear_opt(&sbi->opt, DAX_ALWAYS);
		}
	}

	sb->s_time_gran = 1;
	sb->s_xattr = erofs_xattr_handlers;
	sb->s_export_op = &erofs_export_ops;

	if (test_opt(&sbi->opt, POSIX_ACL))
		sb->s_flags |= SB_POSIXACL;
	else
		sb->s_flags &= ~SB_POSIXACL;

	err = z_erofs_init_super(sb);
	if (err)
		return err;

	if (erofs_sb_has_fragments(sbi) && sbi->packed_nid) {
		inode = erofs_iget(sb, sbi->packed_nid);
		if (IS_ERR(inode))
			return PTR_ERR(inode);
		sbi->packed_inode = inode;
	}
	if (erofs_sb_has_metabox(sbi)) {
		inode = erofs_iget(sb, sbi->metabox_nid);
		if (IS_ERR(inode))
			return PTR_ERR(inode);
		sbi->metabox_inode = inode;
	}

	inode = erofs_iget(sb, sbi->root_nid);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (!S_ISDIR(inode->i_mode)) {
		erofs_err(sb, "rootino(nid %llu) is not a directory(i_mode %o)",
			  sbi->root_nid, inode->i_mode);
		iput(inode);
		return -EINVAL;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	erofs_shrinker_register(sb);
	err = erofs_xattr_prefixes_init(sb);
	if (err)
		return err;

	erofs_set_sysfs_name(sb);
	err = erofs_register_sysfs(sb);
	if (err)
		return err;

	sbi->dir_ra_bytes = EROFS_DIR_RA_BYTES;
	erofs_info(sb, "mounted with root inode @ nid %llu.", sbi->root_nid);
	return 0;
}

static int erofs_fc_get_tree(struct fs_context *fc)
{
	struct erofs_sb_info *sbi = fc->s_fs_info;
	int ret;

	if (IS_ENABLED(CONFIG_EROFS_FS_ONDEMAND) && sbi->fsid)
		return get_tree_nodev(fc, erofs_fc_fill_super);

	ret = get_tree_bdev_flags(fc, erofs_fc_fill_super,
		IS_ENABLED(CONFIG_EROFS_FS_BACKED_BY_FILE) ?
			GET_TREE_BDEV_QUIET_LOOKUP : 0);
#ifdef CONFIG_EROFS_FS_BACKED_BY_FILE
	if (ret == -ENOTBLK) {
		struct file *file;

		if (!fc->source)
			return invalf(fc, "No source specified");
		file = filp_open(fc->source, O_RDONLY | O_LARGEFILE, 0);
		if (IS_ERR(file))
			return PTR_ERR(file);
		sbi->dif0.file = file;

		if (S_ISREG(file_inode(sbi->dif0.file)->i_mode) &&
		    sbi->dif0.file->f_mapping->a_ops->read_folio)
			return get_tree_nodev(fc, erofs_fc_fill_super);
	}
#endif
	return ret;
}

static int erofs_fc_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_sb_info *new_sbi = fc->s_fs_info;

	DBG_BUGON(!sb_rdonly(sb));

	if (new_sbi->fsid || new_sbi->domain_id)
		erofs_info(sb, "ignoring reconfiguration for fsid|domain_id.");

	if (test_opt(&new_sbi->opt, POSIX_ACL))
		fc->sb_flags |= SB_POSIXACL;
	else
		fc->sb_flags &= ~SB_POSIXACL;

	sbi->opt = new_sbi->opt;

	fc->sb_flags |= SB_RDONLY;
	return 0;
}

static int erofs_release_device_info(int id, void *ptr, void *data)
{
	struct erofs_device_info *dif = ptr;

	fs_put_dax(dif->dax_dev, NULL);
	if (dif->file)
		fput(dif->file);
	erofs_fscache_unregister_cookie(dif->fscache);
	dif->fscache = NULL;
	kfree(dif->path);
	kfree(dif);
	return 0;
}

static void erofs_free_dev_context(struct erofs_dev_context *devs)
{
	if (!devs)
		return;
	idr_for_each(&devs->tree, &erofs_release_device_info, NULL);
	idr_destroy(&devs->tree);
	kfree(devs);
}

static void erofs_sb_free(struct erofs_sb_info *sbi)
{
	erofs_free_dev_context(sbi->devs);
	kfree(sbi->fsid);
	kfree(sbi->domain_id);
	if (sbi->dif0.file)
		fput(sbi->dif0.file);
	kfree(sbi);
}

static void erofs_fc_free(struct fs_context *fc)
{
	struct erofs_sb_info *sbi = fc->s_fs_info;

	if (sbi) /* free here if an error occurs before transferring to sb */
		erofs_sb_free(sbi);
}

static const struct fs_context_operations erofs_context_ops = {
	.parse_param	= erofs_fc_parse_param,
	.get_tree       = erofs_fc_get_tree,
	.reconfigure    = erofs_fc_reconfigure,
	.free		= erofs_fc_free,
};

static int erofs_init_fs_context(struct fs_context *fc)
{
	struct erofs_sb_info *sbi;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sbi->devs = kzalloc(sizeof(struct erofs_dev_context), GFP_KERNEL);
	if (!sbi->devs) {
		kfree(sbi);
		return -ENOMEM;
	}
	fc->s_fs_info = sbi;

	idr_init(&sbi->devs->tree);
	init_rwsem(&sbi->devs->rwsem);
	erofs_default_options(sbi);
	fc->ops = &erofs_context_ops;
	return 0;
}

static void erofs_drop_internal_inodes(struct erofs_sb_info *sbi)
{
	iput(sbi->packed_inode);
	sbi->packed_inode = NULL;
	iput(sbi->metabox_inode);
	sbi->metabox_inode = NULL;
#ifdef CONFIG_EROFS_FS_ZIP
	iput(sbi->managed_cache);
	sbi->managed_cache = NULL;
#endif
}

static void erofs_kill_sb(struct super_block *sb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	if ((IS_ENABLED(CONFIG_EROFS_FS_ONDEMAND) && sbi->fsid) ||
	    sbi->dif0.file)
		kill_anon_super(sb);
	else
		kill_block_super(sb);
	erofs_drop_internal_inodes(sbi);
	fs_put_dax(sbi->dif0.dax_dev, NULL);
	erofs_fscache_unregister_fs(sb);
	erofs_sb_free(sbi);
	sb->s_fs_info = NULL;
}

static void erofs_put_super(struct super_block *sb)
{
	struct erofs_sb_info *const sbi = EROFS_SB(sb);

	erofs_unregister_sysfs(sb);
	erofs_shrinker_unregister(sb);
	erofs_xattr_prefixes_cleanup(sb);
	erofs_drop_internal_inodes(sbi);
	erofs_free_dev_context(sbi->devs);
	sbi->devs = NULL;
	erofs_fscache_unregister_fs(sb);
}

static struct file_system_type erofs_fs_type = {
	.owner          = THIS_MODULE,
	.name           = "erofs",
	.init_fs_context = erofs_init_fs_context,
	.kill_sb        = erofs_kill_sb,
	.fs_flags       = FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};
MODULE_ALIAS_FS("erofs");

static int __init erofs_module_init(void)
{
	int err;

	erofs_check_ondisk_layout_definitions();

	erofs_inode_cachep = kmem_cache_create("erofs_inode",
			sizeof(struct erofs_inode), 0,
			SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
			erofs_inode_init_once);
	if (!erofs_inode_cachep)
		return -ENOMEM;

	err = erofs_init_shrinker();
	if (err)
		goto shrinker_err;

	err = z_erofs_init_subsystem();
	if (err)
		goto zip_err;

	err = erofs_init_sysfs();
	if (err)
		goto sysfs_err;

	err = register_filesystem(&erofs_fs_type);
	if (err)
		goto fs_err;

	return 0;

fs_err:
	erofs_exit_sysfs();
sysfs_err:
	z_erofs_exit_subsystem();
zip_err:
	erofs_exit_shrinker();
shrinker_err:
	kmem_cache_destroy(erofs_inode_cachep);
	return err;
}

static void __exit erofs_module_exit(void)
{
	unregister_filesystem(&erofs_fs_type);

	/* Ensure all RCU free inodes / pclusters are safe to be destroyed. */
	rcu_barrier();

	erofs_exit_sysfs();
	z_erofs_exit_subsystem();
	erofs_exit_shrinker();
	kmem_cache_destroy(erofs_inode_cachep);
}

static int erofs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct erofs_sb_info *sbi = EROFS_SB(sb);

	buf->f_type = sb->s_magic;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = sbi->total_blocks;
	buf->f_bfree = buf->f_bavail = 0;
	buf->f_files = ULLONG_MAX;
	buf->f_ffree = ULLONG_MAX - sbi->inos;
	buf->f_namelen = EROFS_NAME_LEN;

	if (uuid_is_null(&sb->s_uuid))
		buf->f_fsid = u64_to_fsid(!sb->s_bdev ? 0 :
				huge_encode_dev(sb->s_bdev->bd_dev));
	else
		buf->f_fsid = uuid_to_fsid(sb->s_uuid.b);
	return 0;
}

static int erofs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct erofs_sb_info *sbi = EROFS_SB(root->d_sb);
	struct erofs_mount_opts *opt = &sbi->opt;

	if (IS_ENABLED(CONFIG_EROFS_FS_XATTR))
		seq_puts(seq, test_opt(opt, XATTR_USER) ?
				",user_xattr" : ",nouser_xattr");
	if (IS_ENABLED(CONFIG_EROFS_FS_POSIX_ACL))
		seq_puts(seq, test_opt(opt, POSIX_ACL) ? ",acl" : ",noacl");
	if (IS_ENABLED(CONFIG_EROFS_FS_ZIP))
		seq_printf(seq, ",cache_strategy=%s",
			  erofs_param_cache_strategy[opt->cache_strategy].name);
	if (test_opt(opt, DAX_ALWAYS))
		seq_puts(seq, ",dax=always");
	if (test_opt(opt, DAX_NEVER))
		seq_puts(seq, ",dax=never");
	if (erofs_is_fileio_mode(sbi) && test_opt(opt, DIRECT_IO))
		seq_puts(seq, ",directio");
#ifdef CONFIG_EROFS_FS_ONDEMAND
	if (sbi->fsid)
		seq_printf(seq, ",fsid=%s", sbi->fsid);
	if (sbi->domain_id)
		seq_printf(seq, ",domain_id=%s", sbi->domain_id);
#endif
	if (sbi->dif0.fsoff)
		seq_printf(seq, ",fsoffset=%llu", sbi->dif0.fsoff);
	return 0;
}

const struct super_operations erofs_sops = {
	.put_super = erofs_put_super,
	.alloc_inode = erofs_alloc_inode,
	.free_inode = erofs_free_inode,
	.statfs = erofs_statfs,
	.show_options = erofs_show_options,
};

module_init(erofs_module_init);
module_exit(erofs_module_exit);

MODULE_DESCRIPTION("Enhanced ROM File System");
MODULE_AUTHOR("Gao Xiang, Chao Yu, Miao Xie, CONSUMER BG, HUAWEI Inc.");
MODULE_LICENSE("GPL");
