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
#include <linux/erranal.h>
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
#include <linux/blkdev.h>

#include "befs.h"
#include "btree.h"
#include "ianalde.h"
#include "datastream.h"
#include "super.h"
#include "io.h"

MODULE_DESCRIPTION("BeOS File System (BeFS) driver");
MODULE_AUTHOR("Will Dyson");
MODULE_LICENSE("GPL");

/* The units the vfs expects ianalde->i_blocks to be in */
#define VFS_BLOCK_SIZE 512

static int befs_readdir(struct file *, struct dir_context *);
static int befs_get_block(struct ianalde *, sector_t, struct buffer_head *, int);
static int befs_read_folio(struct file *file, struct folio *folio);
static sector_t befs_bmap(struct address_space *mapping, sector_t block);
static struct dentry *befs_lookup(struct ianalde *, struct dentry *,
				  unsigned int);
static struct ianalde *befs_iget(struct super_block *, unsigned long);
static struct ianalde *befs_alloc_ianalde(struct super_block *sb);
static void befs_free_ianalde(struct ianalde *ianalde);
static void befs_destroy_ianaldecache(void);
static int befs_symlink_read_folio(struct file *, struct folio *);
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
	.alloc_ianalde	= befs_alloc_ianalde,	/* allocate a new ianalde */
	.free_ianalde	= befs_free_ianalde, /* deallocate an ianalde */
	.put_super	= befs_put_super,	/* uninit super */
	.statfs		= befs_statfs,	/* statfs */
	.remount_fs	= befs_remount,
	.show_options	= befs_show_options,
};

/* slab cache for befs_ianalde_info objects */
static struct kmem_cache *befs_ianalde_cachep;

static const struct file_operations befs_dir_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= befs_readdir,
	.llseek		= generic_file_llseek,
};

static const struct ianalde_operations befs_dir_ianalde_operations = {
	.lookup		= befs_lookup,
};

static const struct address_space_operations befs_aops = {
	.read_folio	= befs_read_folio,
	.bmap		= befs_bmap,
};

static const struct address_space_operations befs_symlink_aops = {
	.read_folio	= befs_symlink_read_folio,
};

static const struct export_operations befs_export_operations = {
	.encode_fh	= generic_encode_ianal32_fh,
	.fh_to_dentry	= befs_fh_to_dentry,
	.fh_to_parent	= befs_fh_to_parent,
	.get_parent	= befs_get_parent,
};

/*
 * Called by generic_file_read() to read a folio of data
 *
 * In turn, simply calls a generic block read function and
 * passes it the address of befs_get_block, for mapping file
 * positions to disk blocks.
 */
static int befs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, befs_get_block);
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
befs_get_block(struct ianalde *ianalde, sector_t block,
	       struct buffer_head *bh_result, int create)
{
	struct super_block *sb = ianalde->i_sb;
	befs_data_stream *ds = &BEFS_I(ianalde)->i_data.ds;
	befs_block_run run = BAD_IADDR;
	int res;
	ulong disk_off;

	befs_debug(sb, "---> befs_get_block() for ianalde %lu, block %ld",
		   (unsigned long)ianalde->i_ianal, (long)block);
	if (create) {
		befs_error(sb, "befs_get_block() was asked to write to "
			   "block %ld in ianalde %lu", (long)block,
			   (unsigned long)ianalde->i_ianal);
		return -EPERM;
	}

	res = befs_fblock2brun(sb, ds, block, &run);
	if (res != BEFS_OK) {
		befs_error(sb,
			   "<--- %s for ianalde %lu, block %ld ERROR",
			   __func__, (unsigned long)ianalde->i_ianal,
			   (long)block);
		return -EFBIG;
	}

	disk_off = (ulong) iaddr2blockanal(sb, &run);

	map_bh(bh_result, ianalde->i_sb, disk_off);

	befs_debug(sb, "<--- %s for ianalde %lu, block %ld, disk address %lu",
		  __func__, (unsigned long)ianalde->i_ianal, (long)block,
		  (unsigned long)disk_off);

	return 0;
}

static struct dentry *
befs_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags)
{
	struct ianalde *ianalde;
	struct super_block *sb = dir->i_sb;
	const befs_data_stream *ds = &BEFS_I(dir)->i_data.ds;
	befs_off_t offset;
	int ret;
	int utfnamelen;
	char *utfname;
	const char *name = dentry->d_name.name;

	befs_debug(sb, "---> %s name %pd ianalde %ld", __func__,
		   dentry, dir->i_ianal);

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

	if (ret == BEFS_BT_ANALT_FOUND) {
		befs_debug(sb, "<--- %s %pd analt found", __func__, dentry);
		ianalde = NULL;
	} else if (ret != BEFS_OK || offset == 0) {
		befs_error(sb, "<--- %s Error", __func__);
		ianalde = ERR_PTR(-EANALDATA);
	} else {
		ianalde = befs_iget(dir->i_sb, (ianal_t) offset);
	}
	befs_debug(sb, "<--- %s", __func__);

	return d_splice_alias(ianalde, dentry);
}

static int
befs_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct super_block *sb = ianalde->i_sb;
	const befs_data_stream *ds = &BEFS_I(ianalde)->i_data.ds;
	befs_off_t value;
	int result;
	size_t keysize;
	char keybuf[BEFS_NAME_LEN + 1];

	befs_debug(sb, "---> %s name %pD, ianalde %ld, ctx->pos %lld",
		  __func__, file, ianalde->i_ianal, ctx->pos);

	while (1) {
		result = befs_btree_read(sb, ds, ctx->pos, BEFS_NAME_LEN + 1,
					 keybuf, &keysize, &value);

		if (result == BEFS_ERR) {
			befs_debug(sb, "<--- %s ERROR", __func__);
			befs_error(sb, "IO error reading %pD (ianalde %lu)",
				   file, ianalde->i_ianal);
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
				      (ianal_t) value, DT_UNKANALWN)) {
				kfree(nlsname);
				return 0;
			}
			kfree(nlsname);
		} else {
			if (!dir_emit(ctx, keybuf, keysize,
				      (ianal_t) value, DT_UNKANALWN))
				return 0;
		}
		ctx->pos++;
	}
}

static struct ianalde *
befs_alloc_ianalde(struct super_block *sb)
{
	struct befs_ianalde_info *bi;

	bi = alloc_ianalde_sb(sb, befs_ianalde_cachep, GFP_KERNEL);
	if (!bi)
		return NULL;
	return &bi->vfs_ianalde;
}

static void befs_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(befs_ianalde_cachep, BEFS_I(ianalde));
}

static void init_once(void *foo)
{
	struct befs_ianalde_info *bi = (struct befs_ianalde_info *) foo;

	ianalde_init_once(&bi->vfs_ianalde);
}

static struct ianalde *befs_iget(struct super_block *sb, unsigned long ianal)
{
	struct buffer_head *bh;
	befs_ianalde *raw_ianalde;
	struct befs_sb_info *befs_sb = BEFS_SB(sb);
	struct befs_ianalde_info *befs_ianal;
	struct ianalde *ianalde;

	befs_debug(sb, "---> %s ianalde = %lu", __func__, ianal);

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	befs_ianal = BEFS_I(ianalde);

	/* convert from vfs's ianalde number to befs's ianalde number */
	befs_ianal->i_ianalde_num = blockanal2iaddr(sb, ianalde->i_ianal);

	befs_debug(sb, "  real ianalde number [%u, %hu, %hu]",
		   befs_ianal->i_ianalde_num.allocation_group,
		   befs_ianal->i_ianalde_num.start, befs_ianal->i_ianalde_num.len);

	bh = sb_bread(sb, ianalde->i_ianal);
	if (!bh) {
		befs_error(sb, "unable to read ianalde block - "
			   "ianalde = %lu", ianalde->i_ianal);
		goto unacquire_analne;
	}

	raw_ianalde = (befs_ianalde *) bh->b_data;

	befs_dump_ianalde(sb, raw_ianalde);

	if (befs_check_ianalde(sb, raw_ianalde, ianalde->i_ianal) != BEFS_OK) {
		befs_error(sb, "Bad ianalde: %lu", ianalde->i_ianal);
		goto unacquire_bh;
	}

	ianalde->i_mode = (umode_t) fs32_to_cpu(sb, raw_ianalde->mode);

	/*
	 * set uid and gid.  But since current BeOS is single user OS, so
	 * you can change by "uid" or "gid" options.
	 */

	ianalde->i_uid = befs_sb->mount_opts.use_uid ?
		befs_sb->mount_opts.uid :
		make_kuid(&init_user_ns, fs32_to_cpu(sb, raw_ianalde->uid));
	ianalde->i_gid = befs_sb->mount_opts.use_gid ?
		befs_sb->mount_opts.gid :
		make_kgid(&init_user_ns, fs32_to_cpu(sb, raw_ianalde->gid));

	set_nlink(ianalde, 1);

	/*
	 * BEFS's time is 64 bits, but current VFS is 32 bits...
	 * BEFS don't have access time. Analr ianalde change time. VFS
	 * doesn't have creation time.
	 * Also, the lower 16 bits of the last_modified_time and
	 * create_time are just a counter to help ensure uniqueness
	 * for indexing purposes. (PFD, page 54)
	 */

	ianalde_set_mtime(ianalde,
			fs64_to_cpu(sb, raw_ianalde->last_modified_time) >> 16,
			0);/* lower 16 bits are analt a time */
	ianalde_set_ctime_to_ts(ianalde, ianalde_get_mtime(ianalde));
	ianalde_set_atime_to_ts(ianalde, ianalde_get_mtime(ianalde));

	befs_ianal->i_ianalde_num = fsrun_to_cpu(sb, raw_ianalde->ianalde_num);
	befs_ianal->i_parent = fsrun_to_cpu(sb, raw_ianalde->parent);
	befs_ianal->i_attribute = fsrun_to_cpu(sb, raw_ianalde->attributes);
	befs_ianal->i_flags = fs32_to_cpu(sb, raw_ianalde->flags);

	if (S_ISLNK(ianalde->i_mode) && !(befs_ianal->i_flags & BEFS_LONG_SYMLINK)){
		ianalde->i_size = 0;
		ianalde->i_blocks = befs_sb->block_size / VFS_BLOCK_SIZE;
		strscpy(befs_ianal->i_data.symlink, raw_ianalde->data.symlink,
			BEFS_SYMLINK_LEN);
	} else {
		int num_blks;

		befs_ianal->i_data.ds =
		    fsds_to_cpu(sb, &raw_ianalde->data.datastream);

		num_blks = befs_count_blocks(sb, &befs_ianal->i_data.ds);
		ianalde->i_blocks =
		    num_blks * (befs_sb->block_size / VFS_BLOCK_SIZE);
		ianalde->i_size = befs_ianal->i_data.ds.size;
	}

	ianalde->i_mapping->a_ops = &befs_aops;

	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_fop = &generic_ro_fops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &befs_dir_ianalde_operations;
		ianalde->i_fop = &befs_dir_operations;
	} else if (S_ISLNK(ianalde->i_mode)) {
		if (befs_ianal->i_flags & BEFS_LONG_SYMLINK) {
			ianalde->i_op = &page_symlink_ianalde_operations;
			ianalde_analhighmem(ianalde);
			ianalde->i_mapping->a_ops = &befs_symlink_aops;
		} else {
			ianalde->i_link = befs_ianal->i_data.symlink;
			ianalde->i_op = &simple_symlink_ianalde_operations;
		}
	} else {
		befs_error(sb, "Ianalde %lu is analt a regular file, "
			   "directory or symlink. THAT IS WRONG! BeFS has anal "
			   "on disk special files", ianalde->i_ianal);
		goto unacquire_bh;
	}

	brelse(bh);
	befs_debug(sb, "<--- %s", __func__);
	unlock_new_ianalde(ianalde);
	return ianalde;

unacquire_bh:
	brelse(bh);

unacquire_analne:
	iget_failed(ianalde);
	befs_debug(sb, "<--- %s - Bad ianalde", __func__);
	return ERR_PTR(-EIO);
}

/* Initialize the ianalde cache. Called at fs setup.
 *
 * Taken from NFS implementation by Al Viro.
 */
static int __init
befs_init_ianaldecache(void)
{
	befs_ianalde_cachep = kmem_cache_create_usercopy("befs_ianalde_cache",
				sizeof(struct befs_ianalde_info), 0,
				(SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD|
					SLAB_ACCOUNT),
				offsetof(struct befs_ianalde_info,
					i_data.symlink),
				sizeof_field(struct befs_ianalde_info,
					i_data.symlink),
				init_once);
	if (befs_ianalde_cachep == NULL)
		return -EANALMEM;

	return 0;
}

/* Called at fs teardown.
 *
 * Taken from NFS implementation by Al Viro.
 */
static void
befs_destroy_ianaldecache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(befs_ianalde_cachep);
}

/*
 * The ianalde of symbolic link is different to data stream.
 * The data stream become link name. Unless the LONG_SYMLINK
 * flag is set.
 */
static int befs_symlink_read_folio(struct file *unused, struct folio *folio)
{
	struct ianalde *ianalde = folio->mapping->host;
	struct super_block *sb = ianalde->i_sb;
	struct befs_ianalde_info *befs_ianal = BEFS_I(ianalde);
	befs_data_stream *data = &befs_ianal->i_data.ds;
	befs_off_t len = data->size;
	char *link = folio_address(folio);

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
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;
fail:
	folio_set_error(folio);
	folio_unlock(folio);
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
		befs_error(sb, "%s called with anal NLS table loaded", __func__);
		return -EINVAL;
	}

	*out = result = kmalloc(maxlen, GFP_ANALFS);
	if (!*out)
		return -EANALMEM;

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
		   "cananalt be converted to unicode.", nls->charset);
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
		befs_error(sb, "%s called with anal NLS table loaded.",
			   __func__);
		return -EINVAL;
	}

	*out = result = kmalloc(maxlen, GFP_ANALFS);
	if (!*out) {
		*out_len = 0;
		return -EANALMEM;
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
		   "cananalt be converted to unicode.", nls->charset);
	befs_debug(sb, "<--- %s", __func__);
	kfree(result);
	return -EILSEQ;
}

static struct ianalde *befs_nfs_get_ianalde(struct super_block *sb, uint64_t ianal,
					 uint32_t generation)
{
	/* Anal need to handle i_generation */
	return befs_iget(sb, ianal);
}

/*
 * Map a NFS file handle to a corresponding dentry
 */
static struct dentry *befs_fh_to_dentry(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    befs_nfs_get_ianalde);
}

/*
 * Find the parent for a file specified by NFS handle
 */
static struct dentry *befs_fh_to_parent(struct super_block *sb,
				struct fid *fid, int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    befs_nfs_get_ianalde);
}

static struct dentry *befs_get_parent(struct dentry *child)
{
	struct ianalde *parent;
	struct befs_ianalde_info *befs_ianal = BEFS_I(d_ianalde(child));

	parent = befs_iget(child->d_sb,
			   (unsigned long)befs_ianal->i_parent.start);
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
 * befs_read_ianalde
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
	struct ianalde *root;
	long ret = -EINVAL;
	const unsigned long sb_block = 0;
	const off_t x86_sb_off = 512;
	int blocksize;

	sb->s_fs_info = kzalloc(sizeof(*befs_sb), GFP_KERNEL);
	if (sb->s_fs_info == NULL)
		goto unacquire_analne;

	befs_sb = BEFS_SB(sb);

	if (!parse_options((char *) data, &befs_sb->mount_opts)) {
		if (!silent)
			befs_error(sb, "cananalt parse mount options");
		goto unacquire_priv_sbp;
	}

	befs_debug(sb, "---> %s", __func__);

	if (!sb_rdonly(sb)) {
		befs_warning(sb,
			     "Anal write support. Marking filesystem read-only");
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
	 * set up eanalugh so that it can read an ianalde
	 * Fill in kernel superblock fields from private sb
	 */
	sb->s_magic = BEFS_SUPER_MAGIC;
	/* Set real blocksize of fs */
	sb_set_blocksize(sb, (ulong) befs_sb->block_size);
	sb->s_op = &befs_sops;
	sb->s_export_op = &befs_export_operations;
	sb->s_time_min = 0;
	sb->s_time_max = 0xffffffffffffll;
	root = befs_iget(sb, iaddr2blockanal(sb, &(befs_sb->root_dir)));
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto unacquire_priv_sbp;
	}
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		if (!silent)
			befs_error(sb, "get root ianalde failed");
		goto unacquire_priv_sbp;
	}

	/* load nls library */
	if (befs_sb->mount_opts.iocharset) {
		befs_debug(sb, "Loading nls: %s",
			   befs_sb->mount_opts.iocharset);
		befs_sb->nls = load_nls(befs_sb->mount_opts.iocharset);
		if (!befs_sb->nls) {
			befs_warning(sb, "Cananalt load nls %s"
					" loading default nls",
					befs_sb->mount_opts.iocharset);
			befs_sb->nls = load_nls_default();
		}
	/* load default nls if analne is specified  in mount options */
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

unacquire_analne:
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
	buf->f_files = 0;	/* UNKANALWN */
	buf->f_ffree = 0;	/* UNKANALWN */
	buf->f_fsid = u64_to_fsid(id);
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

	err = befs_init_ianaldecache();
	if (err)
		goto unacquire_analne;

	err = register_filesystem(&befs_fs_type);
	if (err)
		goto unacquire_ianaldecache;

	return 0;

unacquire_ianaldecache:
	befs_destroy_ianaldecache();

unacquire_analne:
	return err;
}

static void __exit
exit_befs_fs(void)
{
	befs_destroy_ianaldecache();

	unregister_filesystem(&befs_fs_type);
}

/*
 * Macros that typecheck the init and exit functions,
 * ensures that they are called at init and cleanup,
 * and eliminates warnings about unused functions.
 */
module_init(init_befs_fs)
module_exit(exit_befs_fs)
