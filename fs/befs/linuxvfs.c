// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/fs/befs/linuxvfs.c
 *
 * Copyright (C) 2001 Will Dyson <will_dyson@pobox.com
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/erryes.h>
#include <linux/stat.h>
#include <linux/nls.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/parser.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/exportfs.h>
#include <linux/seq_file.h>

#include "befs.h"
#include "btree.h"
#include "iyesde.h"
#include "datastream.h"
#include "super.h"
#include "io.h"

MODULE_DESCRIPTION("BeOS File System (BeFS) driver");
MODULE_AUTHOR("Will Dyson");
MODULE_LICENSE("GPL");

/* The units the vfs expects iyesde->i_blocks to be in */
#define VFS_BLOCK_SIZE 512

static int befs_readdir(struct file *, struct dir_context *);
static int befs_get_block(struct iyesde *, sector_t, struct buffer_head *, int);
static int befs_readpage(struct file *file, struct page *page);
static sector_t befs_bmap(struct address_space *mapping, sector_t block);
static struct dentry *befs_lookup(struct iyesde *, struct dentry *,
				  unsigned int);
static struct iyesde *befs_iget(struct super_block *, unsigned long);
static struct iyesde *befs_alloc_iyesde(struct super_block *sb);
static void befs_free_iyesde(struct iyesde *iyesde);
static void befs_destroy_iyesdecache(void);
static int befs_symlink_readpage(struct file *, struct page *);
static int befs_utf2nls(struct super_block *sb, const char *in, int in_len,
			char **out, int *out_len);
static int befs_nls2utf(struct super_block *sb, const char *in, int in_len,
			char **out, int *out_len);
static void befs_put_super(struct super_block *);
static int befs_remount(struct super_block *, int *, char *);
static int befs_statfs(struct dentry *, struct kstatfs *);
static int befs_show_options(struct seq_file *, struct dentry *);
static int parse_options(char *, struct befs_mount_options *);
static struct dentry *befs_fh_to_dentry(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type);
static struct dentry *befs_fh_to_parent(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type);
static struct dentry *befs_get_parent(struct dentry *child);

static const struct super_operations befs_sops = {
	.alloc_iyesde	= befs_alloc_iyesde,	/* allocate a new iyesde */
	.free_iyesde	= befs_free_iyesde, /* deallocate an iyesde */
	.put_super	= befs_put_super,	/* uninit super */
	.statfs		= befs_statfs,	/* statfs */
	.remount_fs	= befs_remount,
	.show_options	= befs_show_options,
};

/* slab cache for befs_iyesde_info objects */
static struct kmem_cache *befs_iyesde_cachep;

static const struct file_operations befs_dir_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= befs_readdir,
	.llseek		= generic_file_llseek,
};

static const struct iyesde_operations befs_dir_iyesde_operations = {
	.lookup		= befs_lookup,
};

static const struct address_space_operations befs_aops = {
	.readpage	= befs_readpage,
	.bmap		= befs_bmap,
};

static const struct address_space_operations befs_symlink_aops = {
	.readpage	= befs_symlink_readpage,
};

static const struct export_operations befs_export_operations = {
	.fh_to_dentry	= befs_fh_to_dentry,
	.fh_to_parent	= befs_fh_to_parent,
	.get_parent	= befs_get_parent,
};

/*
 * Called by generic_file_read() to read a page of data
 *
 * In turn, simply calls a generic block read function and
 * passes it the address of befs_get_block, for mapping file
 * positions to disk blocks.
 */
static int
befs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, befs_get_block);
}

static sector_t
befs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, befs_get_block);
}

/*
 * Generic function to map a file position (block) to a
 * disk offset (passed back in bh_result).
 *
 * Used by many higher level functions.
 *
 * Calls befs_fblock2brun() in datastream.c to do the real work.
 */

static int
befs_get_block(struct iyesde *iyesde, sector_t block,
	       struct buffer_head *bh_result, int create)
{
	struct super_block *sb = iyesde->i_sb;
	befs_data_stream *ds = &BEFS_I(iyesde)->i_data.ds;
	befs_block_run run = BAD_IADDR;
	int res;
	ulong disk_off;

	befs_debug(sb, "---> befs_get_block() for iyesde %lu, block %ld",
		   (unsigned long)iyesde->i_iyes, (long)block);
	if (create) {
		befs_error(sb, "befs_get_block() was asked to write to "
			   "block %ld in iyesde %lu", (long)block,
			   (unsigned long)iyesde->i_iyes);
		return -EPERM;
	}

	res = befs_fblock2brun(sb, ds, block, &run);
	if (res != BEFS_OK) {
		befs_error(sb,
			   "<--- %s for iyesde %lu, block %ld ERROR",
			   __func__, (unsigned long)iyesde->i_iyes,
			   (long)block);
		return -EFBIG;
	}

	disk_off = (ulong) iaddr2blockyes(sb, &run);

	map_bh(bh_result, iyesde->i_sb, disk_off);

	befs_debug(sb, "<--- %s for iyesde %lu, block %ld, disk address %lu",
		  __func__, (unsigned long)iyesde->i_iyes, (long)block,
		  (unsigned long)disk_off);

	return 0;
}

static struct dentry *
befs_lookup(struct iyesde *dir, struct dentry *dentry, unsigned int flags)
{
	struct iyesde *iyesde;
	struct super_block *sb = dir->i_sb;
	const befs_data_stream *ds = &BEFS_I(dir)->i_data.ds;
	befs_off_t offset;
	int ret;
	int utfnamelen;
	char *utfname;
	const char *name = dentry->d_name.name;

	befs_debug(sb, "---> %s name %pd iyesde %ld", __func__,
		   dentry, dir->i_iyes);

	/* Convert to UTF-8 */
	if (BEFS_SB(sb)->nls) {
		ret =
		    befs_nls2utf(sb, name, strlen(name), &utfname, &utfnamelen);
		if (ret < 0) {
			befs_debug(sb, "<--- %s ERROR", __func__);
			return ERR_PTR(ret);
		}
		ret = befs_btree_find(sb, ds, utfname, &offset);
		kfree(utfname);

	} else {
		ret = befs_btree_find(sb, ds, name, &offset);
	}

	if (ret == BEFS_BT_NOT_FOUND) {
		befs_debug(sb, "<--- %s %pd yest found", __func__, dentry);
		iyesde = NULL;
	} else if (ret != BEFS_OK || offset == 0) {
		befs_error(sb, "<--- %s Error", __func__);
		iyesde = ERR_PTR(-ENODATA);
	} else {
		iyesde = befs_iget(dir->i_sb, (iyes_t) offset);
	}
	befs_debug(sb, "<--- %s", __func__);

	return d_splice_alias(iyesde, dentry);
}

static int
befs_readdir(struct file *file, struct dir_context *ctx)
{
	struct iyesde *iyesde = file_iyesde(file);
	struct super_block *sb = iyesde->i_sb;
	const befs_data_stream *ds = &BEFS_I(iyesde)->i_data.ds;
	befs_off_t value;
	int result;
	size_t keysize;
	char keybuf[BEFS_NAME_LEN + 1];

	befs_debug(sb, "---> %s name %pD, iyesde %ld, ctx->pos %lld",
		  __func__, file, iyesde->i_iyes, ctx->pos);

	while (1) {
		result = befs_btree_read(sb, ds, ctx->pos, BEFS_NAME_LEN + 1,
					 keybuf, &keysize, &value);

		if (result == BEFS_ERR) {
			befs_debug(sb, "<--- %s ERROR", __func__);
			befs_error(sb, "IO error reading %pD (iyesde %lu)",
				   file, iyesde->i_iyes);
			return -EIO;

		} else if (result == BEFS_BT_END) {
			befs_debug(sb, "<--- %s END", __func__);
			return 0;

		} else if (result == BEFS_BT_EMPTY) {
			befs_debug(sb, "<--- %s Empty directory", __func__);
			return 0;
		}

		/* Convert to NLS */
		if (BEFS_SB(sb)->nls) {
			char *nlsname;
			int nlsnamelen;

			result =
			    befs_utf2nls(sb, keybuf, keysize, &nlsname,
					 &nlsnamelen);
			if (result < 0) {
				befs_debug(sb, "<--- %s ERROR", __func__);
				return result;
			}
			if (!dir_emit(ctx, nlsname, nlsnamelen,
				      (iyes_t) value, DT_UNKNOWN)) {
				kfree(nlsname);
				return 0;
			}
			kfree(nlsname);
		} else {
			if (!dir_emit(ctx, keybuf, keysize,
				      (iyes_t) value, DT_UNKNOWN))
				return 0;
		}
		ctx->pos++;
	}
}

static struct iyesde *
befs_alloc_iyesde(struct super_block *sb)
{
	struct befs_iyesde_info *bi;

	bi = kmem_cache_alloc(befs_iyesde_cachep, GFP_KERNEL);
	if (!bi)
		return NULL;
	return &bi->vfs_iyesde;
}

static void befs_free_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(befs_iyesde_cachep, BEFS_I(iyesde));
}

static void init_once(void *foo)
{
	struct befs_iyesde_info *bi = (struct befs_iyesde_info *) foo;

	iyesde_init_once(&bi->vfs_iyesde);
}

static struct iyesde *befs_iget(struct super_block *sb, unsigned long iyes)
{
	struct buffer_head *bh;
	befs_iyesde *raw_iyesde;
	struct befs_sb_info *befs_sb = BEFS_SB(sb);
	struct befs_iyesde_info *befs_iyes;
	struct iyesde *iyesde;

	befs_debug(sb, "---> %s iyesde = %lu", __func__, iyes);

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	befs_iyes = BEFS_I(iyesde);

	/* convert from vfs's iyesde number to befs's iyesde number */
	befs_iyes->i_iyesde_num = blockyes2iaddr(sb, iyesde->i_iyes);

	befs_debug(sb, "  real iyesde number [%u, %hu, %hu]",
		   befs_iyes->i_iyesde_num.allocation_group,
		   befs_iyes->i_iyesde_num.start, befs_iyes->i_iyesde_num.len);

	bh = sb_bread(sb, iyesde->i_iyes);
	if (!bh) {
		befs_error(sb, "unable to read iyesde block - "
			   "iyesde = %lu", iyesde->i_iyes);
		goto unacquire_yesne;
	}

	raw_iyesde = (befs_iyesde *) bh->b_data;

	befs_dump_iyesde(sb, raw_iyesde);

	if (befs_check_iyesde(sb, raw_iyesde, iyesde->i_iyes) != BEFS_OK) {
		befs_error(sb, "Bad iyesde: %lu", iyesde->i_iyes);
		goto unacquire_bh;
	}

	iyesde->i_mode = (umode_t) fs32_to_cpu(sb, raw_iyesde->mode);

	/*
	 * set uid and gid.  But since current BeOS is single user OS, so
	 * you can change by "uid" or "gid" options.
	 */

	iyesde->i_uid = befs_sb->mount_opts.use_uid ?
		befs_sb->mount_opts.uid :
		make_kuid(&init_user_ns, fs32_to_cpu(sb, raw_iyesde->uid));
	iyesde->i_gid = befs_sb->mount_opts.use_gid ?
		befs_sb->mount_opts.gid :
		make_kgid(&init_user_ns, fs32_to_cpu(sb, raw_iyesde->gid));

	set_nlink(iyesde, 1);

	/*
	 * BEFS's time is 64 bits, but current VFS is 32 bits...
	 * BEFS don't have access time. Nor iyesde change time. VFS
	 * doesn't have creation time.
	 * Also, the lower 16 bits of the last_modified_time and
	 * create_time are just a counter to help ensure uniqueness
	 * for indexing purposes. (PFD, page 54)
	 */

	iyesde->i_mtime.tv_sec =
	    fs64_to_cpu(sb, raw_iyesde->last_modified_time) >> 16;
	iyesde->i_mtime.tv_nsec = 0;   /* lower 16 bits are yest a time */
	iyesde->i_ctime = iyesde->i_mtime;
	iyesde->i_atime = iyesde->i_mtime;

	befs_iyes->i_iyesde_num = fsrun_to_cpu(sb, raw_iyesde->iyesde_num);
	befs_iyes->i_parent = fsrun_to_cpu(sb, raw_iyesde->parent);
	befs_iyes->i_attribute = fsrun_to_cpu(sb, raw_iyesde->attributes);
	befs_iyes->i_flags = fs32_to_cpu(sb, raw_iyesde->flags);

	if (S_ISLNK(iyesde->i_mode) && !(befs_iyes->i_flags & BEFS_LONG_SYMLINK)){
		iyesde->i_size = 0;
		iyesde->i_blocks = befs_sb->block_size / VFS_BLOCK_SIZE;
		strlcpy(befs_iyes->i_data.symlink, raw_iyesde->data.symlink,
			BEFS_SYMLINK_LEN);
	} else {
		int num_blks;

		befs_iyes->i_data.ds =
		    fsds_to_cpu(sb, &raw_iyesde->data.datastream);

		num_blks = befs_count_blocks(sb, &befs_iyes->i_data.ds);
		iyesde->i_blocks =
		    num_blks * (befs_sb->block_size / VFS_BLOCK_SIZE);
		iyesde->i_size = befs_iyes->i_data.ds.size;
	}

	iyesde->i_mapping->a_ops = &befs_aops;

	if (S_ISREG(iyesde->i_mode)) {
		iyesde->i_fop = &generic_ro_fops;
	} else if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op = &befs_dir_iyesde_operations;
		iyesde->i_fop = &befs_dir_operations;
	} else if (S_ISLNK(iyesde->i_mode)) {
		if (befs_iyes->i_flags & BEFS_LONG_SYMLINK) {
			iyesde->i_op = &page_symlink_iyesde_operations;
			iyesde_yeshighmem(iyesde);
			iyesde->i_mapping->a_ops = &befs_symlink_aops;
		} else {
			iyesde->i_link = befs_iyes->i_data.symlink;
			iyesde->i_op = &simple_symlink_iyesde_operations;
		}
	} else {
		befs_error(sb, "Iyesde %lu is yest a regular file, "
			   "directory or symlink. THAT IS WRONG! BeFS has yes "
			   "on disk special files", iyesde->i_iyes);
		goto unacquire_bh;
	}

	brelse(bh);
	befs_debug(sb, "<--- %s", __func__);
	unlock_new_iyesde(iyesde);
	return iyesde;

unacquire_bh:
	brelse(bh);

unacquire_yesne:
	iget_failed(iyesde);
	befs_debug(sb, "<--- %s - Bad iyesde", __func__);
	return ERR_PTR(-EIO);
}

/* Initialize the iyesde cache. Called at fs setup.
 *
 * Taken from NFS implementation by Al Viro.
 */
static int __init
befs_init_iyesdecache(void)
{
	befs_iyesde_cachep = kmem_cache_create_usercopy("befs_iyesde_cache",
				sizeof(struct befs_iyesde_info), 0,
				(SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|
					SLAB_ACCOUNT),
				offsetof(struct befs_iyesde_info,
					i_data.symlink),
				sizeof_field(struct befs_iyesde_info,
					i_data.symlink),
				init_once);
	if (befs_iyesde_cachep == NULL)
		return -ENOMEM;

	return 0;
}

/* Called at fs teardown.
 *
 * Taken from NFS implementation by Al Viro.
 */
static void
befs_destroy_iyesdecache(void)
{
	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(befs_iyesde_cachep);
}

/*
 * The iyesde of symbolic link is different to data stream.
 * The data stream become link name. Unless the LONG_SYMLINK
 * flag is set.
 */
static int befs_symlink_readpage(struct file *unused, struct page *page)
{
	struct iyesde *iyesde = page->mapping->host;
	struct super_block *sb = iyesde->i_sb;
	struct befs_iyesde_info *befs_iyes = BEFS_I(iyesde);
	befs_data_stream *data = &befs_iyes->i_data.ds;
	befs_off_t len = data->size;
	char *link = page_address(page);

	if (len == 0 || len > PAGE_SIZE) {
		befs_error(sb, "Long symlink with illegal length");
		goto fail;
	}
	befs_debug(sb, "Follow long symlink");

	if (befs_read_lsymlink(sb, data, link, len) != len) {
		befs_error(sb, "Failed to read entire long symlink");
		goto fail;
	}
	link[len - 1] = '\0';
	SetPageUptodate(page);
	unlock_page(page);
	return 0;
fail:
	SetPageError(page);
	unlock_page(page);
	return -EIO;
}

/*
 * UTF-8 to NLS charset convert routine
 *
 * Uses uni2char() / char2uni() rather than the nls tables directly
 */
static int
befs_utf2nls(struct super_block *sb, const char *in,
	     int in_len, char **out, int *out_len)
{
	struct nls_table *nls = BEFS_SB(sb)->nls;
	int i, o;
	unicode_t uni;
	int unilen, utflen;
	char *result;
	/* The utf8->nls conversion won't make the final nls string bigger
	 * than the utf one, but if the string is pure ascii they'll have the
	 * same width and an extra char is needed to save the additional \0
	 */
	int maxlen = in_len + 1;

	befs_debug(sb, "---> %s", __func__);

	if (!nls) {
		befs_error(sb, "%s called with yes NLS table loaded", __func__);
		return -EINVAL;
	}

	*out = result = kmalloc(maxlen, GFP_NOFS);
	if (!*out)
		return -ENOMEM;

	for (i = o = 0; i < in_len; i += utflen, o += unilen) {

		/* convert from UTF-8 to Unicode */
		utflen = utf8_to_utf32(&in[i], in_len - i, &uni);
		if (utflen < 0)
			goto conv_err;

		/* convert from Unicode to nls */
		if (uni > MAX_WCHAR_T)
			goto conv_err;
		unilen = nls->uni2char(uni, &result[o], in_len - o);
		if (unilen < 0)
			goto conv_err;
	}
	result[o] = '\0';
	*out_len = o;

	befs_debug(sb, "<--- %s", __func__);

	return o;

conv_err:
	befs_error(sb, "Name using character set %s contains a character that "
		   "canyest be converted to unicode.", nls->charset);
	befs_debug(sb, "<--- %s", __func__);
	kfree(result);
	return -EILSEQ;
}

/**
 * befs_nls2utf - Convert NLS string to utf8 encodeing
 * @sb: Superblock
 * @in: Input string buffer in NLS format
 * @in_len: Length of input string in bytes
 * @out: The output string in UTF-8 format
 * @out_len: Length of the output buffer
 *
 * Converts input string @in, which is in the format of the loaded NLS map,
 * into a utf8 string.
 *
 * The destination string @out is allocated by this function and the caller is
 * responsible for freeing it with kfree()
 *
 * On return, *@out_len is the length of @out in bytes.
 *
 * On success, the return value is the number of utf8 characters written to
 * the output buffer @out.
 *
 * On Failure, a negative number coresponding to the error code is returned.
 */

static int
befs_nls2utf(struct super_block *sb, const char *in,
	     int in_len, char **out, int *out_len)
{
	struct nls_table *nls = BEFS_SB(sb)->nls;
	int i, o;
	wchar_t uni;
	int unilen, utflen;
	char *result;
	/*
	 * There are nls characters that will translate to 3-chars-wide UTF-8
	 * characters, an additional byte is needed to save the final \0
	 * in special cases
	 */
	int maxlen = (3 * in_len) + 1;

	befs_debug(sb, "---> %s\n", __func__);

	if (!nls) {
		befs_error(sb, "%s called with yes NLS table loaded.",
			   __func__);
		return -EINVAL;
	}

	*out = result = kmalloc(maxlen, GFP_NOFS);
	if (!*out) {
		*out_len = 0;
		return -ENOMEM;
	}

	for (i = o = 0; i < in_len; i += unilen, o += utflen) {

		/* convert from nls to unicode */
		unilen = nls->char2uni(&in[i], in_len - i, &uni);
		if (unilen < 0)
			goto conv_err;

		/* convert from unicode to UTF-8 */
		utflen = utf32_to_utf8(uni, &result[o], 3);
		if (utflen <= 0)
			goto conv_err;
	}

	result[o] = '\0';
	*out_len = o;

	befs_debug(sb, "<--- %s", __func__);

	return i;

conv_err:
	befs_error(sb, "Name using character set %s contains a character that "
		   "canyest be converted to unicode.", nls->charset);
	befs_debug(sb, "<--- %s", __func__);
	kfree(result);
	return -EILSEQ;
}

static struct iyesde *befs_nfs_get_iyesde(struct super_block *sb, uint64_t iyes,
					 uint32_t generation)
{
	/* No need to handle i_generation */
	return befs_iget(sb, iyes);
}

/*
 * Map a NFS file handle to a corresponding dentry
 */
static struct dentry *befs_fh_to_dentry(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    befs_nfs_get_iyesde);
}

/*
 * Find the parent for a file specified by NFS handle
 */
static struct dentry *befs_fh_to_parent(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    befs_nfs_get_iyesde);
}

static struct dentry *befs_get_parent(struct dentry *child)
{
	struct iyesde *parent;
	struct befs_iyesde_info *befs_iyes = BEFS_I(d_iyesde(child));

	parent = befs_iget(child->d_sb,
			   (unsigned long)befs_iyes->i_parent.start);
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	return d_obtain_alias(parent);
}

enum {
	Opt_uid, Opt_gid, Opt_charset, Opt_debug, Opt_err,
};

static const match_table_t befs_tokens = {
	{Opt_uid, "uid=%d"},
	{Opt_gid, "gid=%d"},
	{Opt_charset, "iocharset=%s"},
	{Opt_debug, "debug"},
	{Opt_err, NULL}
};

static int
parse_options(char *options, struct befs_mount_options *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	kuid_t uid;
	kgid_t gid;

	/* Initialize options */
	opts->uid = GLOBAL_ROOT_UID;
	opts->gid = GLOBAL_ROOT_GID;
	opts->use_uid = 0;
	opts->use_gid = 0;
	opts->iocharset = NULL;
	opts->debug = 0;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, befs_tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
				return 0;
			uid = INVALID_UID;
			if (option >= 0)
				uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(uid)) {
				pr_err("Invalid uid %d, "
				       "using default\n", option);
				break;
			}
			opts->uid = uid;
			opts->use_uid = 1;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 0;
			gid = INVALID_GID;
			if (option >= 0)
				gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid)) {
				pr_err("Invalid gid %d, "
				       "using default\n", option);
				break;
			}
			opts->gid = gid;
			opts->use_gid = 1;
			break;
		case Opt_charset:
			kfree(opts->iocharset);
			opts->iocharset = match_strdup(&args[0]);
			if (!opts->iocharset) {
				pr_err("allocation failure for "
				       "iocharset string\n");
				return 0;
			}
			break;
		case Opt_debug:
			opts->debug = 1;
			break;
		default:
			pr_err("Unrecognized mount option \"%s\" "
			       "or missing value\n", p);
			return 0;
		}
	}
	return 1;
}

static int befs_show_options(struct seq_file *m, struct dentry *root)
{
	struct befs_sb_info *befs_sb = BEFS_SB(root->d_sb);
	struct befs_mount_options *opts = &befs_sb->mount_opts;

	if (!uid_eq(opts->uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, opts->uid));
	if (!gid_eq(opts->gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, opts->gid));
	if (opts->iocharset)
		seq_printf(m, ",charset=%s", opts->iocharset);
	if (opts->debug)
		seq_puts(m, ",debug");
	return 0;
}

/* This function has the responsibiltiy of getting the
 * filesystem ready for unmounting.
 * Basically, we free everything that we allocated in
 * befs_read_iyesde
 */
static void
befs_put_super(struct super_block *sb)
{
	kfree(BEFS_SB(sb)->mount_opts.iocharset);
	BEFS_SB(sb)->mount_opts.iocharset = NULL;
	unload_nls(BEFS_SB(sb)->nls);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

/* Allocate private field of the superblock, fill it.
 *
 * Finish filling the public superblock fields
 * Make the root directory
 * Load a set of NLS translations if needed.
 */
static int
befs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh;
	struct befs_sb_info *befs_sb;
	befs_super_block *disk_sb;
	struct iyesde *root;
	long ret = -EINVAL;
	const unsigned long sb_block = 0;
	const off_t x86_sb_off = 512;
	int blocksize;

	sb->s_fs_info = kzalloc(sizeof(*befs_sb), GFP_KERNEL);
	if (sb->s_fs_info == NULL)
		goto unacquire_yesne;

	befs_sb = BEFS_SB(sb);

	if (!parse_options((char *) data, &befs_sb->mount_opts)) {
		if (!silent)
			befs_error(sb, "canyest parse mount options");
		goto unacquire_priv_sbp;
	}

	befs_debug(sb, "---> %s", __func__);

	if (!sb_rdonly(sb)) {
		befs_warning(sb,
			     "No write support. Marking filesystem read-only");
		sb->s_flags |= SB_RDONLY;
	}

	/*
	 * Set dummy blocksize to read super block.
	 * Will be set to real fs blocksize later.
	 *
	 * Linux 2.4.10 and later refuse to read blocks smaller than
	 * the logical block size for the device. But we also need to read at
	 * least 1k to get the second 512 bytes of the volume.
	 */
	blocksize = sb_min_blocksize(sb, 1024);
	if (!blocksize) {
		if (!silent)
			befs_error(sb, "unable to set blocksize");
		goto unacquire_priv_sbp;
	}

	bh = sb_bread(sb, sb_block);
	if (!bh) {
		if (!silent)
			befs_error(sb, "unable to read superblock");
		goto unacquire_priv_sbp;
	}

	/* account for offset of super block on x86 */
	disk_sb = (befs_super_block *) bh->b_data;
	if ((disk_sb->magic1 == BEFS_SUPER_MAGIC1_LE) ||
	    (disk_sb->magic1 == BEFS_SUPER_MAGIC1_BE)) {
		befs_debug(sb, "Using PPC superblock location");
	} else {
		befs_debug(sb, "Using x86 superblock location");
		disk_sb =
		    (befs_super_block *) ((void *) bh->b_data + x86_sb_off);
	}

	if ((befs_load_sb(sb, disk_sb) != BEFS_OK) ||
	    (befs_check_sb(sb) != BEFS_OK))
		goto unacquire_bh;

	befs_dump_super_block(sb, disk_sb);

	brelse(bh);

	if (befs_sb->num_blocks > ~((sector_t)0)) {
		if (!silent)
			befs_error(sb, "blocks count: %llu is larger than the host can use",
					befs_sb->num_blocks);
		goto unacquire_priv_sbp;
	}

	/*
	 * set up eyesugh so that it can read an iyesde
	 * Fill in kernel superblock fields from private sb
	 */
	sb->s_magic = BEFS_SUPER_MAGIC;
	/* Set real blocksize of fs */
	sb_set_blocksize(sb, (ulong) befs_sb->block_size);
	sb->s_op = &befs_sops;
	sb->s_export_op = &befs_export_operations;
	sb->s_time_min = 0;
	sb->s_time_max = 0xffffffffffffll;
	root = befs_iget(sb, iaddr2blockyes(sb, &(befs_sb->root_dir)));
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto unacquire_priv_sbp;
	}
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		if (!silent)
			befs_error(sb, "get root iyesde failed");
		goto unacquire_priv_sbp;
	}

	/* load nls library */
	if (befs_sb->mount_opts.iocharset) {
		befs_debug(sb, "Loading nls: %s",
			   befs_sb->mount_opts.iocharset);
		befs_sb->nls = load_nls(befs_sb->mount_opts.iocharset);
		if (!befs_sb->nls) {
			befs_warning(sb, "Canyest load nls %s"
					" loading default nls",
					befs_sb->mount_opts.iocharset);
			befs_sb->nls = load_nls_default();
		}
	/* load default nls if yesne is specified  in mount options */
	} else {
		befs_debug(sb, "Loading default nls");
		befs_sb->nls = load_nls_default();
	}

	return 0;

unacquire_bh:
	brelse(bh);

unacquire_priv_sbp:
	kfree(befs_sb->mount_opts.iocharset);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;

unacquire_yesne:
	return ret;
}

static int
befs_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	if (!(*flags & SB_RDONLY))
		return -EINVAL;
	return 0;
}

static int
befs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	befs_debug(sb, "---> %s", __func__);

	buf->f_type = BEFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = BEFS_SB(sb)->num_blocks;
	buf->f_bfree = BEFS_SB(sb)->num_blocks - BEFS_SB(sb)->used_blocks;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = 0;	/* UNKNOWN */
	buf->f_ffree = 0;	/* UNKNOWN */
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = BEFS_NAME_LEN;

	befs_debug(sb, "<--- %s", __func__);

	return 0;
}

static struct dentry *
befs_mount(struct file_system_type *fs_type, int flags, const char *dev_name,
	    void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, befs_fill_super);
}

static struct file_system_type befs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "befs",
	.mount		= befs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("befs");

static int __init
init_befs_fs(void)
{
	int err;

	pr_info("version: %s\n", BEFS_VERSION);

	err = befs_init_iyesdecache();
	if (err)
		goto unacquire_yesne;

	err = register_filesystem(&befs_fs_type);
	if (err)
		goto unacquire_iyesdecache;

	return 0;

unacquire_iyesdecache:
	befs_destroy_iyesdecache();

unacquire_yesne:
	return err;
}

static void __exit
exit_befs_fs(void)
{
	befs_destroy_iyesdecache();

	unregister_filesystem(&befs_fs_type);
}

/*
 * Macros that typecheck the init and exit functions,
 * ensures that they are called at init and cleanup,
 * and eliminates warnings about unused functions.
 */
module_init(init_befs_fs)
module_exit(exit_befs_fs)
