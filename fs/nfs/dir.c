// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/nfs/dir.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs directory handling functions
 *
 * 10 Apr 1996	Added silly rename for unlink	--okir
 * 28 Sep 1996	Improved directory cache --okir
 * 23 Aug 1997  Claus Heine claus@momo.math.rwth-aachen.de 
 *              Re-implemented silly rename for unlink, newly implemented
 *              silly rename for nfs_rename() following the suggestions
 *              of Olaf Kirch (okir) found in this file.
 *              Following Linus comments on my original hack, this version
 *              depends only on the dcache stuff and doesn't touch the inode
 *              layer (iput() and friends).
 *  6 Jun 1999	Cache readdir lookups in the page cache. -DaveM
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/swap.h>
#include <linux/sched.h>
#include <linux/kmemleak.h>
#include <linux/xattr.h>

#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"

#include "nfstrace.h"

/* #define NFS_DEBUG_VERBOSE 1 */

static int nfs_opendir(struct inode *, struct file *);
static int nfs_closedir(struct inode *, struct file *);
static int nfs_readdir(struct file *, struct dir_context *);
static int nfs_fsync_dir(struct file *, loff_t, loff_t, int);
static loff_t nfs_llseek_dir(struct file *, loff_t, int);
static void nfs_readdir_clear_array(struct page*);

const struct file_operations nfs_dir_operations = {
	.llseek		= nfs_llseek_dir,
	.read		= generic_read_dir,
	.iterate_shared	= nfs_readdir,
	.open		= nfs_opendir,
	.release	= nfs_closedir,
	.fsync		= nfs_fsync_dir,
};

const struct address_space_operations nfs_dir_aops = {
	.freepage = nfs_readdir_clear_array,
};

static struct nfs_open_dir_context *alloc_nfs_open_dir_context(struct inode *dir)
{
	struct nfs_inode *nfsi = NFS_I(dir);
	struct nfs_open_dir_context *ctx;
	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx != NULL) {
		ctx->duped = 0;
		ctx->attr_gencount = nfsi->attr_gencount;
		ctx->dir_cookie = 0;
		ctx->dup_cookie = 0;
		spin_lock(&dir->i_lock);
		if (list_empty(&nfsi->open_files) &&
		    (nfsi->cache_validity & NFS_INO_DATA_INVAL_DEFER))
			nfs_set_cache_invalid(dir,
					      NFS_INO_INVALID_DATA |
						      NFS_INO_REVAL_FORCED);
		list_add(&ctx->list, &nfsi->open_files);
		spin_unlock(&dir->i_lock);
		return ctx;
	}
	return  ERR_PTR(-ENOMEM);
}

static void put_nfs_open_dir_context(struct inode *dir, struct nfs_open_dir_context *ctx)
{
	spin_lock(&dir->i_lock);
	list_del(&ctx->list);
	spin_unlock(&dir->i_lock);
	kfree(ctx);
}

/*
 * Open file
 */
static int
nfs_opendir(struct inode *inode, struct file *filp)
{
	int res = 0;
	struct nfs_open_dir_context *ctx;

	dfprintk(FILE, "NFS: open dir(%pD2)\n", filp);

	nfs_inc_stats(inode, NFSIOS_VFSOPEN);

	ctx = alloc_nfs_open_dir_context(inode);
	if (IS_ERR(ctx)) {
		res = PTR_ERR(ctx);
		goto out;
	}
	filp->private_data = ctx;
out:
	return res;
}

static int
nfs_closedir(struct inode *inode, struct file *filp)
{
	put_nfs_open_dir_context(file_inode(filp), filp->private_data);
	return 0;
}

struct nfs_cache_array_entry {
	u64 cookie;
	u64 ino;
	const char *name;
	unsigned int name_len;
	unsigned char d_type;
};

struct nfs_cache_array {
	u64 last_cookie;
	unsigned int size;
	unsigned char page_full : 1,
		      page_is_eof : 1,
		      cookies_are_ordered : 1;
	struct nfs_cache_array_entry array[];
};

struct nfs_readdir_descriptor {
	struct file	*file;
	struct page	*page;
	struct dir_context *ctx;
	pgoff_t		page_index;
	u64		dir_cookie;
	u64		last_cookie;
	u64		dup_cookie;
	loff_t		current_index;
	loff_t		prev_index;

	__be32		verf[NFS_DIR_VERIFIER_SIZE];
	unsigned long	dir_verifier;
	unsigned long	timestamp;
	unsigned long	gencount;
	unsigned long	attr_gencount;
	unsigned int	cache_entry_index;
	signed char duped;
	bool plus;
	bool eof;
};

static void nfs_readdir_array_init(struct nfs_cache_array *array)
{
	memset(array, 0, sizeof(struct nfs_cache_array));
}

static void nfs_readdir_page_init_array(struct page *page, u64 last_cookie)
{
	struct nfs_cache_array *array;

	array = kmap_atomic(page);
	nfs_readdir_array_init(array);
	array->last_cookie = last_cookie;
	array->cookies_are_ordered = 1;
	kunmap_atomic(array);
}

/*
 * we are freeing strings created by nfs_add_to_readdir_array()
 */
static
void nfs_readdir_clear_array(struct page *page)
{
	struct nfs_cache_array *array;
	int i;

	array = kmap_atomic(page);
	for (i = 0; i < array->size; i++)
		kfree(array->array[i].name);
	nfs_readdir_array_init(array);
	kunmap_atomic(array);
}

static struct page *
nfs_readdir_page_array_alloc(u64 last_cookie, gfp_t gfp_flags)
{
	struct page *page = alloc_page(gfp_flags);
	if (page)
		nfs_readdir_page_init_array(page, last_cookie);
	return page;
}

static void nfs_readdir_page_array_free(struct page *page)
{
	if (page) {
		nfs_readdir_clear_array(page);
		put_page(page);
	}
}

static void nfs_readdir_array_set_eof(struct nfs_cache_array *array)
{
	array->page_is_eof = 1;
	array->page_full = 1;
}

static bool nfs_readdir_array_is_full(struct nfs_cache_array *array)
{
	return array->page_full;
}

/*
 * the caller is responsible for freeing qstr.name
 * when called by nfs_readdir_add_to_array, the strings will be freed in
 * nfs_clear_readdir_array()
 */
static const char *nfs_readdir_copy_name(const char *name, unsigned int len)
{
	const char *ret = kmemdup_nul(name, len, GFP_KERNEL);

	/*
	 * Avoid a kmemleak false positive. The pointer to the name is stored
	 * in a page cache page which kmemleak does not scan.
	 */
	if (ret != NULL)
		kmemleak_not_leak(ret);
	return ret;
}

/*
 * Check that the next array entry lies entirely within the page bounds
 */
static int nfs_readdir_array_can_expand(struct nfs_cache_array *array)
{
	struct nfs_cache_array_entry *cache_entry;

	if (array->page_full)
		return -ENOSPC;
	cache_entry = &array->array[array->size + 1];
	if ((char *)cache_entry - (char *)array > PAGE_SIZE) {
		array->page_full = 1;
		return -ENOSPC;
	}
	return 0;
}

static
int nfs_readdir_add_to_array(struct nfs_entry *entry, struct page *page)
{
	struct nfs_cache_array *array;
	struct nfs_cache_array_entry *cache_entry;
	const char *name;
	int ret;

	name = nfs_readdir_copy_name(entry->name, entry->len);
	if (!name)
		return -ENOMEM;

	array = kmap_atomic(page);
	ret = nfs_readdir_array_can_expand(array);
	if (ret) {
		kfree(name);
		goto out;
	}

	cache_entry = &array->array[array->size];
	cache_entry->cookie = entry->prev_cookie;
	cache_entry->ino = entry->ino;
	cache_entry->d_type = entry->d_type;
	cache_entry->name_len = entry->len;
	cache_entry->name = name;
	array->last_cookie = entry->cookie;
	if (array->last_cookie <= cache_entry->cookie)
		array->cookies_are_ordered = 0;
	array->size++;
	if (entry->eof != 0)
		nfs_readdir_array_set_eof(array);
out:
	kunmap_atomic(array);
	return ret;
}

static struct page *nfs_readdir_page_get_locked(struct address_space *mapping,
						pgoff_t index, u64 last_cookie)
{
	struct page *page;

	page = grab_cache_page(mapping, index);
	if (page && !PageUptodate(page)) {
		nfs_readdir_page_init_array(page, last_cookie);
		if (invalidate_inode_pages2_range(mapping, index + 1, -1) < 0)
			nfs_zap_mapping(mapping->host, mapping);
		SetPageUptodate(page);
	}

	return page;
}

static u64 nfs_readdir_page_last_cookie(struct page *page)
{
	struct nfs_cache_array *array;
	u64 ret;

	array = kmap_atomic(page);
	ret = array->last_cookie;
	kunmap_atomic(array);
	return ret;
}

static bool nfs_readdir_page_needs_filling(struct page *page)
{
	struct nfs_cache_array *array;
	bool ret;

	array = kmap_atomic(page);
	ret = !nfs_readdir_array_is_full(array);
	kunmap_atomic(array);
	return ret;
}

static void nfs_readdir_page_set_eof(struct page *page)
{
	struct nfs_cache_array *array;

	array = kmap_atomic(page);
	nfs_readdir_array_set_eof(array);
	kunmap_atomic(array);
}

static void nfs_readdir_page_unlock_and_put(struct page *page)
{
	unlock_page(page);
	put_page(page);
}

static struct page *nfs_readdir_page_get_next(struct address_space *mapping,
					      pgoff_t index, u64 cookie)
{
	struct page *page;

	page = nfs_readdir_page_get_locked(mapping, index, cookie);
	if (page) {
		if (nfs_readdir_page_last_cookie(page) == cookie)
			return page;
		nfs_readdir_page_unlock_and_put(page);
	}
	return NULL;
}

static inline
int is_32bit_api(void)
{
#ifdef CONFIG_COMPAT
	return in_compat_syscall();
#else
	return (BITS_PER_LONG == 32);
#endif
}

static
bool nfs_readdir_use_cookie(const struct file *filp)
{
	if ((filp->f_mode & FMODE_32BITHASH) ||
	    (!(filp->f_mode & FMODE_64BITHASH) && is_32bit_api()))
		return false;
	return true;
}

static int nfs_readdir_search_for_pos(struct nfs_cache_array *array,
				      struct nfs_readdir_descriptor *desc)
{
	loff_t diff = desc->ctx->pos - desc->current_index;
	unsigned int index;

	if (diff < 0)
		goto out_eof;
	if (diff >= array->size) {
		if (array->page_is_eof)
			goto out_eof;
		return -EAGAIN;
	}

	index = (unsigned int)diff;
	desc->dir_cookie = array->array[index].cookie;
	desc->cache_entry_index = index;
	return 0;
out_eof:
	desc->eof = true;
	return -EBADCOOKIE;
}

static bool
nfs_readdir_inode_mapping_valid(struct nfs_inode *nfsi)
{
	if (nfsi->cache_validity & (NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA))
		return false;
	smp_rmb();
	return !test_bit(NFS_INO_INVALIDATING, &nfsi->flags);
}

static bool nfs_readdir_array_cookie_in_range(struct nfs_cache_array *array,
					      u64 cookie)
{
	if (!array->cookies_are_ordered)
		return true;
	/* Optimisation for monotonically increasing cookies */
	if (cookie >= array->last_cookie)
		return false;
	if (array->size && cookie < array->array[0].cookie)
		return false;
	return true;
}

static int nfs_readdir_search_for_cookie(struct nfs_cache_array *array,
					 struct nfs_readdir_descriptor *desc)
{
	int i;
	loff_t new_pos;
	int status = -EAGAIN;

	if (!nfs_readdir_array_cookie_in_range(array, desc->dir_cookie))
		goto check_eof;

	for (i = 0; i < array->size; i++) {
		if (array->array[i].cookie == desc->dir_cookie) {
			struct nfs_inode *nfsi = NFS_I(file_inode(desc->file));

			new_pos = desc->current_index + i;
			if (desc->attr_gencount != nfsi->attr_gencount ||
			    !nfs_readdir_inode_mapping_valid(nfsi)) {
				desc->duped = 0;
				desc->attr_gencount = nfsi->attr_gencount;
			} else if (new_pos < desc->prev_index) {
				if (desc->duped > 0
				    && desc->dup_cookie == desc->dir_cookie) {
					if (printk_ratelimit()) {
						pr_notice("NFS: directory %pD2 contains a readdir loop."
								"Please contact your server vendor.  "
								"The file: %s has duplicate cookie %llu\n",
								desc->file, array->array[i].name, desc->dir_cookie);
					}
					status = -ELOOP;
					goto out;
				}
				desc->dup_cookie = desc->dir_cookie;
				desc->duped = -1;
			}
			if (nfs_readdir_use_cookie(desc->file))
				desc->ctx->pos = desc->dir_cookie;
			else
				desc->ctx->pos = new_pos;
			desc->prev_index = new_pos;
			desc->cache_entry_index = i;
			return 0;
		}
	}
check_eof:
	if (array->page_is_eof) {
		status = -EBADCOOKIE;
		if (desc->dir_cookie == array->last_cookie)
			desc->eof = true;
	}
out:
	return status;
}

static int nfs_readdir_search_array(struct nfs_readdir_descriptor *desc)
{
	struct nfs_cache_array *array;
	int status;

	array = kmap_atomic(desc->page);

	if (desc->dir_cookie == 0)
		status = nfs_readdir_search_for_pos(array, desc);
	else
		status = nfs_readdir_search_for_cookie(array, desc);

	if (status == -EAGAIN) {
		desc->last_cookie = array->last_cookie;
		desc->current_index += array->size;
		desc->page_index++;
	}
	kunmap_atomic(array);
	return status;
}

/* Fill a page with xdr information before transferring to the cache page */
static int nfs_readdir_xdr_filler(struct nfs_readdir_descriptor *desc,
				  __be32 *verf, u64 cookie,
				  struct page **pages, size_t bufsize,
				  __be32 *verf_res)
{
	struct inode *inode = file_inode(desc->file);
	struct nfs_readdir_arg arg = {
		.dentry = file_dentry(desc->file),
		.cred = desc->file->f_cred,
		.verf = verf,
		.cookie = cookie,
		.pages = pages,
		.page_len = bufsize,
		.plus = desc->plus,
	};
	struct nfs_readdir_res res = {
		.verf = verf_res,
	};
	unsigned long	timestamp, gencount;
	int		error;

 again:
	timestamp = jiffies;
	gencount = nfs_inc_attr_generation_counter();
	desc->dir_verifier = nfs_save_change_attribute(inode);
	error = NFS_PROTO(inode)->readdir(&arg, &res);
	if (error < 0) {
		/* We requested READDIRPLUS, but the server doesn't grok it */
		if (error == -ENOTSUPP && desc->plus) {
			NFS_SERVER(inode)->caps &= ~NFS_CAP_READDIRPLUS;
			clear_bit(NFS_INO_ADVISE_RDPLUS, &NFS_I(inode)->flags);
			desc->plus = arg.plus = false;
			goto again;
		}
		goto error;
	}
	desc->timestamp = timestamp;
	desc->gencount = gencount;
error:
	return error;
}

static int xdr_decode(struct nfs_readdir_descriptor *desc,
		      struct nfs_entry *entry, struct xdr_stream *xdr)
{
	struct inode *inode = file_inode(desc->file);
	int error;

	error = NFS_PROTO(inode)->decode_dirent(xdr, entry, desc->plus);
	if (error)
		return error;
	entry->fattr->time_start = desc->timestamp;
	entry->fattr->gencount = desc->gencount;
	return 0;
}

/* Match file and dirent using either filehandle or fileid
 * Note: caller is responsible for checking the fsid
 */
static
int nfs_same_file(struct dentry *dentry, struct nfs_entry *entry)
{
	struct inode *inode;
	struct nfs_inode *nfsi;

	if (d_really_is_negative(dentry))
		return 0;

	inode = d_inode(dentry);
	if (is_bad_inode(inode) || NFS_STALE(inode))
		return 0;

	nfsi = NFS_I(inode);
	if (entry->fattr->fileid != nfsi->fileid)
		return 0;
	if (entry->fh->size && nfs_compare_fh(entry->fh, &nfsi->fh) != 0)
		return 0;
	return 1;
}

static
bool nfs_use_readdirplus(struct inode *dir, struct dir_context *ctx)
{
	if (!nfs_server_capable(dir, NFS_CAP_READDIRPLUS))
		return false;
	if (test_and_clear_bit(NFS_INO_ADVISE_RDPLUS, &NFS_I(dir)->flags))
		return true;
	if (ctx->pos == 0)
		return true;
	return false;
}

/*
 * This function is called by the lookup and getattr code to request the
 * use of readdirplus to accelerate any future lookups in the same
 * directory.
 */
void nfs_advise_use_readdirplus(struct inode *dir)
{
	struct nfs_inode *nfsi = NFS_I(dir);

	if (nfs_server_capable(dir, NFS_CAP_READDIRPLUS) &&
	    !list_empty(&nfsi->open_files))
		set_bit(NFS_INO_ADVISE_RDPLUS, &nfsi->flags);
}

/*
 * This function is mainly for use by nfs_getattr().
 *
 * If this is an 'ls -l', we want to force use of readdirplus.
 * Do this by checking if there is an active file descriptor
 * and calling nfs_advise_use_readdirplus, then forcing a
 * cache flush.
 */
void nfs_force_use_readdirplus(struct inode *dir)
{
	struct nfs_inode *nfsi = NFS_I(dir);

	if (nfs_server_capable(dir, NFS_CAP_READDIRPLUS) &&
	    !list_empty(&nfsi->open_files)) {
		set_bit(NFS_INO_ADVISE_RDPLUS, &nfsi->flags);
		invalidate_mapping_pages(dir->i_mapping,
			nfsi->page_index + 1, -1);
	}
}

static
void nfs_prime_dcache(struct dentry *parent, struct nfs_entry *entry,
		unsigned long dir_verifier)
{
	struct qstr filename = QSTR_INIT(entry->name, entry->len);
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
	struct dentry *dentry;
	struct dentry *alias;
	struct inode *inode;
	int status;

	if (!(entry->fattr->valid & NFS_ATTR_FATTR_FILEID))
		return;
	if (!(entry->fattr->valid & NFS_ATTR_FATTR_FSID))
		return;
	if (filename.len == 0)
		return;
	/* Validate that the name doesn't contain any illegal '\0' */
	if (strnlen(filename.name, filename.len) != filename.len)
		return;
	/* ...or '/' */
	if (strnchr(filename.name, filename.len, '/'))
		return;
	if (filename.name[0] == '.') {
		if (filename.len == 1)
			return;
		if (filename.len == 2 && filename.name[1] == '.')
			return;
	}
	filename.hash = full_name_hash(parent, filename.name, filename.len);

	dentry = d_lookup(parent, &filename);
again:
	if (!dentry) {
		dentry = d_alloc_parallel(parent, &filename, &wq);
		if (IS_ERR(dentry))
			return;
	}
	if (!d_in_lookup(dentry)) {
		/* Is there a mountpoint here? If so, just exit */
		if (!nfs_fsid_equal(&NFS_SB(dentry->d_sb)->fsid,
					&entry->fattr->fsid))
			goto out;
		if (nfs_same_file(dentry, entry)) {
			if (!entry->fh->size)
				goto out;
			nfs_set_verifier(dentry, dir_verifier);
			status = nfs_refresh_inode(d_inode(dentry), entry->fattr);
			if (!status)
				nfs_setsecurity(d_inode(dentry), entry->fattr, entry->label);
			goto out;
		} else {
			d_invalidate(dentry);
			dput(dentry);
			dentry = NULL;
			goto again;
		}
	}
	if (!entry->fh->size) {
		d_lookup_done(dentry);
		goto out;
	}

	inode = nfs_fhget(dentry->d_sb, entry->fh, entry->fattr, entry->label);
	alias = d_splice_alias(inode, dentry);
	d_lookup_done(dentry);
	if (alias) {
		if (IS_ERR(alias))
			goto out;
		dput(dentry);
		dentry = alias;
	}
	nfs_set_verifier(dentry, dir_verifier);
out:
	dput(dentry);
}

/* Perform conversion from xdr to cache array */
static int nfs_readdir_page_filler(struct nfs_readdir_descriptor *desc,
				   struct nfs_entry *entry,
				   struct page **xdr_pages,
				   unsigned int buflen,
				   struct page **arrays,
				   size_t narrays)
{
	struct address_space *mapping = desc->file->f_mapping;
	struct xdr_stream stream;
	struct xdr_buf buf;
	struct page *scratch, *new, *page = *arrays;
	int status;

	scratch = alloc_page(GFP_KERNEL);
	if (scratch == NULL)
		return -ENOMEM;

	xdr_init_decode_pages(&stream, &buf, xdr_pages, buflen);
	xdr_set_scratch_page(&stream, scratch);

	do {
		if (entry->label)
			entry->label->len = NFS4_MAXLABELLEN;

		status = xdr_decode(desc, entry, &stream);
		if (status != 0)
			break;

		if (desc->plus)
			nfs_prime_dcache(file_dentry(desc->file), entry,
					desc->dir_verifier);

		status = nfs_readdir_add_to_array(entry, page);
		if (status != -ENOSPC)
			continue;

		if (page->mapping != mapping) {
			if (!--narrays)
				break;
			new = nfs_readdir_page_array_alloc(entry->prev_cookie,
							   GFP_KERNEL);
			if (!new)
				break;
			arrays++;
			*arrays = page = new;
		} else {
			new = nfs_readdir_page_get_next(mapping,
							page->index + 1,
							entry->prev_cookie);
			if (!new)
				break;
			if (page != *arrays)
				nfs_readdir_page_unlock_and_put(page);
			page = new;
		}
		status = nfs_readdir_add_to_array(entry, page);
	} while (!status && !entry->eof);

	switch (status) {
	case -EBADCOOKIE:
		if (entry->eof) {
			nfs_readdir_page_set_eof(page);
			status = 0;
		}
		break;
	case -ENOSPC:
	case -EAGAIN:
		status = 0;
		break;
	}

	if (page != *arrays)
		nfs_readdir_page_unlock_and_put(page);

	put_page(scratch);
	return status;
}

static void nfs_readdir_free_pages(struct page **pages, size_t npages)
{
	while (npages--)
		put_page(pages[npages]);
	kfree(pages);
}

/*
 * nfs_readdir_alloc_pages() will allocate pages that must be freed with a call
 * to nfs_readdir_free_pages()
 */
static struct page **nfs_readdir_alloc_pages(size_t npages)
{
	struct page **pages;
	size_t i;

	pages = kmalloc_array(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return NULL;
	for (i = 0; i < npages; i++) {
		struct page *page = alloc_page(GFP_KERNEL);
		if (page == NULL)
			goto out_freepages;
		pages[i] = page;
	}
	return pages;

out_freepages:
	nfs_readdir_free_pages(pages, i);
	return NULL;
}

static int nfs_readdir_xdr_to_array(struct nfs_readdir_descriptor *desc,
				    __be32 *verf_arg, __be32 *verf_res,
				    struct page **arrays, size_t narrays)
{
	struct page **pages;
	struct page *page = *arrays;
	struct nfs_entry *entry;
	size_t array_size;
	struct inode *inode = file_inode(desc->file);
	size_t dtsize = NFS_SERVER(inode)->dtsize;
	int status = -ENOMEM;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->cookie = nfs_readdir_page_last_cookie(page);
	entry->fh = nfs_alloc_fhandle();
	entry->fattr = nfs_alloc_fattr();
	entry->server = NFS_SERVER(inode);
	if (entry->fh == NULL || entry->fattr == NULL)
		goto out;

	entry->label = nfs4_label_alloc(NFS_SERVER(inode), GFP_NOWAIT);
	if (IS_ERR(entry->label)) {
		status = PTR_ERR(entry->label);
		goto out;
	}

	array_size = (dtsize + PAGE_SIZE - 1) >> PAGE_SHIFT;
	pages = nfs_readdir_alloc_pages(array_size);
	if (!pages)
		goto out_release_label;

	do {
		unsigned int pglen;
		status = nfs_readdir_xdr_filler(desc, verf_arg, entry->cookie,
						pages, dtsize,
						verf_res);
		if (status < 0)
			break;

		pglen = status;
		if (pglen == 0) {
			nfs_readdir_page_set_eof(page);
			break;
		}

		verf_arg = verf_res;

		status = nfs_readdir_page_filler(desc, entry, pages, pglen,
						 arrays, narrays);
	} while (!status && nfs_readdir_page_needs_filling(page));

	nfs_readdir_free_pages(pages, array_size);
out_release_label:
	nfs4_label_free(entry->label);
out:
	nfs_free_fattr(entry->fattr);
	nfs_free_fhandle(entry->fh);
	kfree(entry);
	return status;
}

static void nfs_readdir_page_put(struct nfs_readdir_descriptor *desc)
{
	put_page(desc->page);
	desc->page = NULL;
}

static void
nfs_readdir_page_unlock_and_put_cached(struct nfs_readdir_descriptor *desc)
{
	unlock_page(desc->page);
	nfs_readdir_page_put(desc);
}

static struct page *
nfs_readdir_page_get_cached(struct nfs_readdir_descriptor *desc)
{
	return nfs_readdir_page_get_locked(desc->file->f_mapping,
					   desc->page_index,
					   desc->last_cookie);
}

/*
 * Returns 0 if desc->dir_cookie was found on page desc->page_index
 * and locks the page to prevent removal from the page cache.
 */
static int find_and_lock_cache_page(struct nfs_readdir_descriptor *desc)
{
	struct inode *inode = file_inode(desc->file);
	struct nfs_inode *nfsi = NFS_I(inode);
	__be32 verf[NFS_DIR_VERIFIER_SIZE];
	int res;

	desc->page = nfs_readdir_page_get_cached(desc);
	if (!desc->page)
		return -ENOMEM;
	if (nfs_readdir_page_needs_filling(desc->page)) {
		res = nfs_readdir_xdr_to_array(desc, nfsi->cookieverf, verf,
					       &desc->page, 1);
		if (res < 0) {
			nfs_readdir_page_unlock_and_put_cached(desc);
			if (res == -EBADCOOKIE || res == -ENOTSYNC) {
				invalidate_inode_pages2(desc->file->f_mapping);
				desc->page_index = 0;
				return -EAGAIN;
			}
			return res;
		}
		/*
		 * Set the cookie verifier if the page cache was empty
		 */
		if (desc->page_index == 0)
			memcpy(nfsi->cookieverf, verf,
			       sizeof(nfsi->cookieverf));
	}
	res = nfs_readdir_search_array(desc);
	if (res == 0) {
		nfsi->page_index = desc->page_index;
		return 0;
	}
	nfs_readdir_page_unlock_and_put_cached(desc);
	return res;
}

static bool nfs_readdir_dont_search_cache(struct nfs_readdir_descriptor *desc)
{
	struct address_space *mapping = desc->file->f_mapping;
	struct inode *dir = file_inode(desc->file);
	unsigned int dtsize = NFS_SERVER(dir)->dtsize;
	loff_t size = i_size_read(dir);

	/*
	 * Default to uncached readdir if the page cache is empty, and
	 * we're looking for a non-zero cookie in a large directory.
	 */
	return desc->dir_cookie != 0 && mapping->nrpages == 0 && size > dtsize;
}

/* Search for desc->dir_cookie from the beginning of the page cache */
static int readdir_search_pagecache(struct nfs_readdir_descriptor *desc)
{
	int res;

	if (nfs_readdir_dont_search_cache(desc))
		return -EBADCOOKIE;

	do {
		if (desc->page_index == 0) {
			desc->current_index = 0;
			desc->prev_index = 0;
			desc->last_cookie = 0;
		}
		res = find_and_lock_cache_page(desc);
	} while (res == -EAGAIN);
	return res;
}

/*
 * Once we've found the start of the dirent within a page: fill 'er up...
 */
static void nfs_do_filldir(struct nfs_readdir_descriptor *desc,
			   const __be32 *verf)
{
	struct file	*file = desc->file;
	struct nfs_cache_array *array;
	unsigned int i = 0;

	array = kmap(desc->page);
	for (i = desc->cache_entry_index; i < array->size; i++) {
		struct nfs_cache_array_entry *ent;

		ent = &array->array[i];
		if (!dir_emit(desc->ctx, ent->name, ent->name_len,
		    nfs_compat_user_ino64(ent->ino), ent->d_type)) {
			desc->eof = true;
			break;
		}
		memcpy(desc->verf, verf, sizeof(desc->verf));
		if (i < (array->size-1))
			desc->dir_cookie = array->array[i+1].cookie;
		else
			desc->dir_cookie = array->last_cookie;
		if (nfs_readdir_use_cookie(file))
			desc->ctx->pos = desc->dir_cookie;
		else
			desc->ctx->pos++;
		if (desc->duped != 0)
			desc->duped = 1;
	}
	if (array->page_is_eof)
		desc->eof = true;

	kunmap(desc->page);
	dfprintk(DIRCACHE, "NFS: nfs_do_filldir() filling ended @ cookie %llu\n",
			(unsigned long long)desc->dir_cookie);
}

/*
 * If we cannot find a cookie in our cache, we suspect that this is
 * because it points to a deleted file, so we ask the server to return
 * whatever it thinks is the next entry. We then feed this to filldir.
 * If all goes well, we should then be able to find our way round the
 * cache on the next call to readdir_search_pagecache();
 *
 * NOTE: we cannot add the anonymous page to the pagecache because
 *	 the data it contains might not be page aligned. Besides,
 *	 we should already have a complete representation of the
 *	 directory in the page cache by the time we get here.
 */
static int uncached_readdir(struct nfs_readdir_descriptor *desc)
{
	struct page	**arrays;
	size_t		i, sz = 512;
	__be32		verf[NFS_DIR_VERIFIER_SIZE];
	int		status = -ENOMEM;

	dfprintk(DIRCACHE, "NFS: uncached_readdir() searching for cookie %llu\n",
			(unsigned long long)desc->dir_cookie);

	arrays = kcalloc(sz, sizeof(*arrays), GFP_KERNEL);
	if (!arrays)
		goto out;
	arrays[0] = nfs_readdir_page_array_alloc(desc->dir_cookie, GFP_KERNEL);
	if (!arrays[0])
		goto out;

	desc->page_index = 0;
	desc->last_cookie = desc->dir_cookie;
	desc->duped = 0;

	status = nfs_readdir_xdr_to_array(desc, desc->verf, verf, arrays, sz);

	for (i = 0; !desc->eof && i < sz && arrays[i]; i++) {
		desc->page = arrays[i];
		nfs_do_filldir(desc, verf);
	}
	desc->page = NULL;


	for (i = 0; i < sz && arrays[i]; i++)
		nfs_readdir_page_array_free(arrays[i]);
out:
	kfree(arrays);
	dfprintk(DIRCACHE, "NFS: %s: returns %d\n", __func__, status);
	return status;
}

/* The file offset position represents the dirent entry number.  A
   last cookie cache takes care of the common case of reading the
   whole directory.
 */
static int nfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry	*dentry = file_dentry(file);
	struct inode	*inode = d_inode(dentry);
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_dir_context *dir_ctx = file->private_data;
	struct nfs_readdir_descriptor *desc;
	int res;

	dfprintk(FILE, "NFS: readdir(%pD2) starting at cookie %llu\n",
			file, (long long)ctx->pos);
	nfs_inc_stats(inode, NFSIOS_VFSGETDENTS);

	/*
	 * ctx->pos points to the dirent entry number.
	 * *desc->dir_cookie has the cookie for the next entry. We have
	 * to either find the entry with the appropriate number or
	 * revalidate the cookie.
	 */
	if (ctx->pos == 0 || nfs_attribute_cache_expired(inode)) {
		res = nfs_revalidate_mapping(inode, file->f_mapping);
		if (res < 0)
			goto out;
	}

	res = -ENOMEM;
	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		goto out;
	desc->file = file;
	desc->ctx = ctx;
	desc->plus = nfs_use_readdirplus(inode, ctx);

	spin_lock(&file->f_lock);
	desc->dir_cookie = dir_ctx->dir_cookie;
	desc->dup_cookie = dir_ctx->dup_cookie;
	desc->duped = dir_ctx->duped;
	desc->attr_gencount = dir_ctx->attr_gencount;
	memcpy(desc->verf, dir_ctx->verf, sizeof(desc->verf));
	spin_unlock(&file->f_lock);

	do {
		res = readdir_search_pagecache(desc);

		if (res == -EBADCOOKIE) {
			res = 0;
			/* This means either end of directory */
			if (desc->dir_cookie && !desc->eof) {
				/* Or that the server has 'lost' a cookie */
				res = uncached_readdir(desc);
				if (res == 0)
					continue;
				if (res == -EBADCOOKIE || res == -ENOTSYNC)
					res = 0;
			}
			break;
		}
		if (res == -ETOOSMALL && desc->plus) {
			clear_bit(NFS_INO_ADVISE_RDPLUS, &nfsi->flags);
			nfs_zap_caches(inode);
			desc->page_index = 0;
			desc->plus = false;
			desc->eof = false;
			continue;
		}
		if (res < 0)
			break;

		nfs_do_filldir(desc, nfsi->cookieverf);
		nfs_readdir_page_unlock_and_put_cached(desc);
	} while (!desc->eof);

	spin_lock(&file->f_lock);
	dir_ctx->dir_cookie = desc->dir_cookie;
	dir_ctx->dup_cookie = desc->dup_cookie;
	dir_ctx->duped = desc->duped;
	dir_ctx->attr_gencount = desc->attr_gencount;
	memcpy(dir_ctx->verf, desc->verf, sizeof(dir_ctx->verf));
	spin_unlock(&file->f_lock);

	kfree(desc);

out:
	dfprintk(FILE, "NFS: readdir(%pD2) returns %d\n", file, res);
	return res;
}

static loff_t nfs_llseek_dir(struct file *filp, loff_t offset, int whence)
{
	struct nfs_open_dir_context *dir_ctx = filp->private_data;

	dfprintk(FILE, "NFS: llseek dir(%pD2, %lld, %d)\n",
			filp, offset, whence);

	switch (whence) {
	default:
		return -EINVAL;
	case SEEK_SET:
		if (offset < 0)
			return -EINVAL;
		spin_lock(&filp->f_lock);
		break;
	case SEEK_CUR:
		if (offset == 0)
			return filp->f_pos;
		spin_lock(&filp->f_lock);
		offset += filp->f_pos;
		if (offset < 0) {
			spin_unlock(&filp->f_lock);
			return -EINVAL;
		}
	}
	if (offset != filp->f_pos) {
		filp->f_pos = offset;
		if (nfs_readdir_use_cookie(filp))
			dir_ctx->dir_cookie = offset;
		else
			dir_ctx->dir_cookie = 0;
		if (offset == 0)
			memset(dir_ctx->verf, 0, sizeof(dir_ctx->verf));
		dir_ctx->duped = 0;
	}
	spin_unlock(&filp->f_lock);
	return offset;
}

/*
 * All directory operations under NFS are synchronous, so fsync()
 * is a dummy operation.
 */
static int nfs_fsync_dir(struct file *filp, loff_t start, loff_t end,
			 int datasync)
{
	dfprintk(FILE, "NFS: fsync dir(%pD2) datasync %d\n", filp, datasync);

	nfs_inc_stats(file_inode(filp), NFSIOS_VFSFSYNC);
	return 0;
}

/**
 * nfs_force_lookup_revalidate - Mark the directory as having changed
 * @dir: pointer to directory inode
 *
 * This forces the revalidation code in nfs_lookup_revalidate() to do a
 * full lookup on all child dentries of 'dir' whenever a change occurs
 * on the server that might have invalidated our dcache.
 *
 * Note that we reserve bit '0' as a tag to let us know when a dentry
 * was revalidated while holding a delegation on its inode.
 *
 * The caller should be holding dir->i_lock
 */
void nfs_force_lookup_revalidate(struct inode *dir)
{
	NFS_I(dir)->cache_change_attribute += 2;
}
EXPORT_SYMBOL_GPL(nfs_force_lookup_revalidate);

/**
 * nfs_verify_change_attribute - Detects NFS remote directory changes
 * @dir: pointer to parent directory inode
 * @verf: previously saved change attribute
 *
 * Return "false" if the verifiers doesn't match the change attribute.
 * This would usually indicate that the directory contents have changed on
 * the server, and that any dentries need revalidating.
 */
static bool nfs_verify_change_attribute(struct inode *dir, unsigned long verf)
{
	return (verf & ~1UL) == nfs_save_change_attribute(dir);
}

static void nfs_set_verifier_delegated(unsigned long *verf)
{
	*verf |= 1UL;
}

#if IS_ENABLED(CONFIG_NFS_V4)
static void nfs_unset_verifier_delegated(unsigned long *verf)
{
	*verf &= ~1UL;
}
#endif /* IS_ENABLED(CONFIG_NFS_V4) */

static bool nfs_test_verifier_delegated(unsigned long verf)
{
	return verf & 1;
}

static bool nfs_verifier_is_delegated(struct dentry *dentry)
{
	return nfs_test_verifier_delegated(dentry->d_time);
}

static void nfs_set_verifier_locked(struct dentry *dentry, unsigned long verf)
{
	struct inode *inode = d_inode(dentry);

	if (!nfs_verifier_is_delegated(dentry) &&
	    !nfs_verify_change_attribute(d_inode(dentry->d_parent), verf))
		goto out;
	if (inode && NFS_PROTO(inode)->have_delegation(inode, FMODE_READ))
		nfs_set_verifier_delegated(&verf);
out:
	dentry->d_time = verf;
}

/**
 * nfs_set_verifier - save a parent directory verifier in the dentry
 * @dentry: pointer to dentry
 * @verf: verifier to save
 *
 * Saves the parent directory verifier in @dentry. If the inode has
 * a delegation, we also tag the dentry as having been revalidated
 * while holding a delegation so that we know we don't have to
 * look it up again after a directory change.
 */
void nfs_set_verifier(struct dentry *dentry, unsigned long verf)
{

	spin_lock(&dentry->d_lock);
	nfs_set_verifier_locked(dentry, verf);
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL_GPL(nfs_set_verifier);

#if IS_ENABLED(CONFIG_NFS_V4)
/**
 * nfs_clear_verifier_delegated - clear the dir verifier delegation tag
 * @inode: pointer to inode
 *
 * Iterates through the dentries in the inode alias list and clears
 * the tag used to indicate that the dentry has been revalidated
 * while holding a delegation.
 * This function is intended for use when the delegation is being
 * returned or revoked.
 */
void nfs_clear_verifier_delegated(struct inode *inode)
{
	struct dentry *alias;

	if (!inode)
		return;
	spin_lock(&inode->i_lock);
	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&alias->d_lock);
		nfs_unset_verifier_delegated(&alias->d_time);
		spin_unlock(&alias->d_lock);
	}
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_clear_verifier_delegated);
#endif /* IS_ENABLED(CONFIG_NFS_V4) */

/*
 * A check for whether or not the parent directory has changed.
 * In the case it has, we assume that the dentries are untrustworthy
 * and may need to be looked up again.
 * If rcu_walk prevents us from performing a full check, return 0.
 */
static int nfs_check_verifier(struct inode *dir, struct dentry *dentry,
			      int rcu_walk)
{
	if (IS_ROOT(dentry))
		return 1;
	if (NFS_SERVER(dir)->flags & NFS_MOUNT_LOOKUP_CACHE_NONE)
		return 0;
	if (!nfs_verify_change_attribute(dir, dentry->d_time))
		return 0;
	/* Revalidate nfsi->cache_change_attribute before we declare a match */
	if (nfs_mapping_need_revalidate_inode(dir)) {
		if (rcu_walk)
			return 0;
		if (__nfs_revalidate_inode(NFS_SERVER(dir), dir) < 0)
			return 0;
	}
	if (!nfs_verify_change_attribute(dir, dentry->d_time))
		return 0;
	return 1;
}

/*
 * Use intent information to check whether or not we're going to do
 * an O_EXCL create using this path component.
 */
static int nfs_is_exclusive_create(struct inode *dir, unsigned int flags)
{
	if (NFS_PROTO(dir)->version == 2)
		return 0;
	return flags & LOOKUP_EXCL;
}

/*
 * Inode and filehandle revalidation for lookups.
 *
 * We force revalidation in the cases where the VFS sets LOOKUP_REVAL,
 * or if the intent information indicates that we're about to open this
 * particular file and the "nocto" mount flag is not set.
 *
 */
static
int nfs_lookup_verify_inode(struct inode *inode, unsigned int flags)
{
	struct nfs_server *server = NFS_SERVER(inode);
	int ret;

	if (IS_AUTOMOUNT(inode))
		return 0;

	if (flags & LOOKUP_OPEN) {
		switch (inode->i_mode & S_IFMT) {
		case S_IFREG:
			/* A NFSv4 OPEN will revalidate later */
			if (server->caps & NFS_CAP_ATOMIC_OPEN)
				goto out;
			fallthrough;
		case S_IFDIR:
			if (server->flags & NFS_MOUNT_NOCTO)
				break;
			/* NFS close-to-open cache consistency validation */
			goto out_force;
		}
	}

	/* VFS wants an on-the-wire revalidation */
	if (flags & LOOKUP_REVAL)
		goto out_force;
out:
	return (inode->i_nlink == 0) ? -ESTALE : 0;
out_force:
	if (flags & LOOKUP_RCU)
		return -ECHILD;
	ret = __nfs_revalidate_inode(server, inode);
	if (ret != 0)
		return ret;
	goto out;
}

static void nfs_mark_dir_for_revalidate(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	nfs_set_cache_invalid(inode, NFS_INO_REVAL_PAGECACHE);
	spin_unlock(&inode->i_lock);
}

/*
 * We judge how long we want to trust negative
 * dentries by looking at the parent inode mtime.
 *
 * If parent mtime has changed, we revalidate, else we wait for a
 * period corresponding to the parent's attribute cache timeout value.
 *
 * If LOOKUP_RCU prevents us from performing a full check, return 1
 * suggesting a reval is needed.
 *
 * Note that when creating a new file, or looking up a rename target,
 * then it shouldn't be necessary to revalidate a negative dentry.
 */
static inline
int nfs_neg_need_reval(struct inode *dir, struct dentry *dentry,
		       unsigned int flags)
{
	if (flags & (LOOKUP_CREATE | LOOKUP_RENAME_TARGET))
		return 0;
	if (NFS_SERVER(dir)->flags & NFS_MOUNT_LOOKUP_CACHE_NONEG)
		return 1;
	return !nfs_check_verifier(dir, dentry, flags & LOOKUP_RCU);
}

static int
nfs_lookup_revalidate_done(struct inode *dir, struct dentry *dentry,
			   struct inode *inode, int error)
{
	switch (error) {
	case 1:
		dfprintk(LOOKUPCACHE, "NFS: %s(%pd2) is valid\n",
			__func__, dentry);
		return 1;
	case 0:
		/*
		 * We can't d_drop the root of a disconnected tree:
		 * its d_hash is on the s_anon list and d_drop() would hide
		 * it from shrink_dcache_for_unmount(), leading to busy
		 * inodes on unmount and further oopses.
		 */
		if (inode && IS_ROOT(dentry))
			return 1;
		dfprintk(LOOKUPCACHE, "NFS: %s(%pd2) is invalid\n",
				__func__, dentry);
		return 0;
	}
	dfprintk(LOOKUPCACHE, "NFS: %s(%pd2) lookup returned error %d\n",
				__func__, dentry, error);
	return error;
}

static int
nfs_lookup_revalidate_negative(struct inode *dir, struct dentry *dentry,
			       unsigned int flags)
{
	int ret = 1;
	if (nfs_neg_need_reval(dir, dentry, flags)) {
		if (flags & LOOKUP_RCU)
			return -ECHILD;
		ret = 0;
	}
	return nfs_lookup_revalidate_done(dir, dentry, NULL, ret);
}

static int
nfs_lookup_revalidate_delegated(struct inode *dir, struct dentry *dentry,
				struct inode *inode)
{
	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
	return nfs_lookup_revalidate_done(dir, dentry, inode, 1);
}

static int
nfs_lookup_revalidate_dentry(struct inode *dir, struct dentry *dentry,
			     struct inode *inode)
{
	struct nfs_fh *fhandle;
	struct nfs_fattr *fattr;
	struct nfs4_label *label;
	unsigned long dir_verifier;
	int ret;

	ret = -ENOMEM;
	fhandle = nfs_alloc_fhandle();
	fattr = nfs_alloc_fattr();
	label = nfs4_label_alloc(NFS_SERVER(inode), GFP_KERNEL);
	if (fhandle == NULL || fattr == NULL || IS_ERR(label))
		goto out;

	dir_verifier = nfs_save_change_attribute(dir);
	ret = NFS_PROTO(dir)->lookup(dir, dentry, fhandle, fattr, label);
	if (ret < 0) {
		switch (ret) {
		case -ESTALE:
		case -ENOENT:
			ret = 0;
			break;
		case -ETIMEDOUT:
			if (NFS_SERVER(inode)->flags & NFS_MOUNT_SOFTREVAL)
				ret = 1;
		}
		goto out;
	}
	ret = 0;
	if (nfs_compare_fh(NFS_FH(inode), fhandle))
		goto out;
	if (nfs_refresh_inode(inode, fattr) < 0)
		goto out;

	nfs_setsecurity(inode, fattr, label);
	nfs_set_verifier(dentry, dir_verifier);

	/* set a readdirplus hint that we had a cache miss */
	nfs_force_use_readdirplus(dir);
	ret = 1;
out:
	nfs_free_fattr(fattr);
	nfs_free_fhandle(fhandle);
	nfs4_label_free(label);

	/*
	 * If the lookup failed despite the dentry change attribute being
	 * a match, then we should revalidate the directory cache.
	 */
	if (!ret && nfs_verify_change_attribute(dir, dentry->d_time))
		nfs_mark_dir_for_revalidate(dir);
	return nfs_lookup_revalidate_done(dir, dentry, inode, ret);
}

/*
 * This is called every time the dcache has a lookup hit,
 * and we should check whether we can really trust that
 * lookup.
 *
 * NOTE! The hit can be a negative hit too, don't assume
 * we have an inode!
 *
 * If the parent directory is seen to have changed, we throw out the
 * cached dentry and do a new lookup.
 */
static int
nfs_do_lookup_revalidate(struct inode *dir, struct dentry *dentry,
			 unsigned int flags)
{
	struct inode *inode;
	int error;

	nfs_inc_stats(dir, NFSIOS_DENTRYREVALIDATE);
	inode = d_inode(dentry);

	if (!inode)
		return nfs_lookup_revalidate_negative(dir, dentry, flags);

	if (is_bad_inode(inode)) {
		dfprintk(LOOKUPCACHE, "%s: %pd2 has dud inode\n",
				__func__, dentry);
		goto out_bad;
	}

	if (nfs_verifier_is_delegated(dentry))
		return nfs_lookup_revalidate_delegated(dir, dentry, inode);

	/* Force a full look up iff the parent directory has changed */
	if (!(flags & (LOOKUP_EXCL | LOOKUP_REVAL)) &&
	    nfs_check_verifier(dir, dentry, flags & LOOKUP_RCU)) {
		error = nfs_lookup_verify_inode(inode, flags);
		if (error) {
			if (error == -ESTALE)
				nfs_mark_dir_for_revalidate(dir);
			goto out_bad;
		}
		nfs_advise_use_readdirplus(dir);
		goto out_valid;
	}

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (NFS_STALE(inode))
		goto out_bad;

	trace_nfs_lookup_revalidate_enter(dir, dentry, flags);
	error = nfs_lookup_revalidate_dentry(dir, dentry, inode);
	trace_nfs_lookup_revalidate_exit(dir, dentry, flags, error);
	return error;
out_valid:
	return nfs_lookup_revalidate_done(dir, dentry, inode, 1);
out_bad:
	if (flags & LOOKUP_RCU)
		return -ECHILD;
	return nfs_lookup_revalidate_done(dir, dentry, inode, 0);
}

static int
__nfs_lookup_revalidate(struct dentry *dentry, unsigned int flags,
			int (*reval)(struct inode *, struct dentry *, unsigned int))
{
	struct dentry *parent;
	struct inode *dir;
	int ret;

	if (flags & LOOKUP_RCU) {
		parent = READ_ONCE(dentry->d_parent);
		dir = d_inode_rcu(parent);
		if (!dir)
			return -ECHILD;
		ret = reval(dir, dentry, flags);
		if (parent != READ_ONCE(dentry->d_parent))
			return -ECHILD;
	} else {
		parent = dget_parent(dentry);
		ret = reval(d_inode(parent), dentry, flags);
		dput(parent);
	}
	return ret;
}

static int nfs_lookup_revalidate(struct dentry *dentry, unsigned int flags)
{
	return __nfs_lookup_revalidate(dentry, flags, nfs_do_lookup_revalidate);
}

/*
 * A weaker form of d_revalidate for revalidating just the d_inode(dentry)
 * when we don't really care about the dentry name. This is called when a
 * pathwalk ends on a dentry that was not found via a normal lookup in the
 * parent dir (e.g.: ".", "..", procfs symlinks or mountpoint traversals).
 *
 * In this situation, we just want to verify that the inode itself is OK
 * since the dentry might have changed on the server.
 */
static int nfs_weak_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct inode *inode = d_inode(dentry);
	int error = 0;

	/*
	 * I believe we can only get a negative dentry here in the case of a
	 * procfs-style symlink. Just assume it's correct for now, but we may
	 * eventually need to do something more here.
	 */
	if (!inode) {
		dfprintk(LOOKUPCACHE, "%s: %pd2 has negative inode\n",
				__func__, dentry);
		return 1;
	}

	if (is_bad_inode(inode)) {
		dfprintk(LOOKUPCACHE, "%s: %pd2 has dud inode\n",
				__func__, dentry);
		return 0;
	}

	error = nfs_lookup_verify_inode(inode, flags);
	dfprintk(LOOKUPCACHE, "NFS: %s: inode %lu is %s\n",
			__func__, inode->i_ino, error ? "invalid" : "valid");
	return !error;
}

/*
 * This is called from dput() when d_count is going to 0.
 */
static int nfs_dentry_delete(const struct dentry *dentry)
{
	dfprintk(VFS, "NFS: dentry_delete(%pd2, %x)\n",
		dentry, dentry->d_flags);

	/* Unhash any dentry with a stale inode */
	if (d_really_is_positive(dentry) && NFS_STALE(d_inode(dentry)))
		return 1;

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		/* Unhash it, so that ->d_iput() would be called */
		return 1;
	}
	if (!(dentry->d_sb->s_flags & SB_ACTIVE)) {
		/* Unhash it, so that ancestors of killed async unlink
		 * files will be cleaned up during umount */
		return 1;
	}
	return 0;

}

/* Ensure that we revalidate inode->i_nlink */
static void nfs_drop_nlink(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	/* drop the inode if we're reasonably sure this is the last link */
	if (inode->i_nlink > 0)
		drop_nlink(inode);
	NFS_I(inode)->attr_gencount = nfs_inc_attr_generation_counter();
	nfs_set_cache_invalid(
		inode, NFS_INO_INVALID_CHANGE | NFS_INO_INVALID_CTIME |
			       NFS_INO_INVALID_NLINK);
	spin_unlock(&inode->i_lock);
}

/*
 * Called when the dentry loses inode.
 * We use it to clean up silly-renamed files.
 */
static void nfs_dentry_iput(struct dentry *dentry, struct inode *inode)
{
	if (S_ISDIR(inode->i_mode))
		/* drop any readdir cache as it could easily be old */
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_DATA);

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		nfs_complete_unlink(dentry, inode);
		nfs_drop_nlink(inode);
	}
	iput(inode);
}

static void nfs_d_release(struct dentry *dentry)
{
	/* free cached devname value, if it survived that far */
	if (unlikely(dentry->d_fsdata)) {
		if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
			WARN_ON(1);
		else
			kfree(dentry->d_fsdata);
	}
}

const struct dentry_operations nfs_dentry_operations = {
	.d_revalidate	= nfs_lookup_revalidate,
	.d_weak_revalidate	= nfs_weak_revalidate,
	.d_delete	= nfs_dentry_delete,
	.d_iput		= nfs_dentry_iput,
	.d_automount	= nfs_d_automount,
	.d_release	= nfs_d_release,
};
EXPORT_SYMBOL_GPL(nfs_dentry_operations);

struct dentry *nfs_lookup(struct inode *dir, struct dentry * dentry, unsigned int flags)
{
	struct dentry *res;
	struct inode *inode = NULL;
	struct nfs_fh *fhandle = NULL;
	struct nfs_fattr *fattr = NULL;
	struct nfs4_label *label = NULL;
	unsigned long dir_verifier;
	int error;

	dfprintk(VFS, "NFS: lookup(%pd2)\n", dentry);
	nfs_inc_stats(dir, NFSIOS_VFSLOOKUP);

	if (unlikely(dentry->d_name.len > NFS_SERVER(dir)->namelen))
		return ERR_PTR(-ENAMETOOLONG);

	/*
	 * If we're doing an exclusive create, optimize away the lookup
	 * but don't hash the dentry.
	 */
	if (nfs_is_exclusive_create(dir, flags) || flags & LOOKUP_RENAME_TARGET)
		return NULL;

	res = ERR_PTR(-ENOMEM);
	fhandle = nfs_alloc_fhandle();
	fattr = nfs_alloc_fattr();
	if (fhandle == NULL || fattr == NULL)
		goto out;

	label = nfs4_label_alloc(NFS_SERVER(dir), GFP_NOWAIT);
	if (IS_ERR(label))
		goto out;

	dir_verifier = nfs_save_change_attribute(dir);
	trace_nfs_lookup_enter(dir, dentry, flags);
	error = NFS_PROTO(dir)->lookup(dir, dentry, fhandle, fattr, label);
	if (error == -ENOENT)
		goto no_entry;
	if (error < 0) {
		res = ERR_PTR(error);
		goto out_label;
	}
	inode = nfs_fhget(dentry->d_sb, fhandle, fattr, label);
	res = ERR_CAST(inode);
	if (IS_ERR(res))
		goto out_label;

	/* Notify readdir to use READDIRPLUS */
	nfs_force_use_readdirplus(dir);

no_entry:
	res = d_splice_alias(inode, dentry);
	if (res != NULL) {
		if (IS_ERR(res))
			goto out_label;
		dentry = res;
	}
	nfs_set_verifier(dentry, dir_verifier);
out_label:
	trace_nfs_lookup_exit(dir, dentry, flags, error);
	nfs4_label_free(label);
out:
	nfs_free_fattr(fattr);
	nfs_free_fhandle(fhandle);
	return res;
}
EXPORT_SYMBOL_GPL(nfs_lookup);

#if IS_ENABLED(CONFIG_NFS_V4)
static int nfs4_lookup_revalidate(struct dentry *, unsigned int);

const struct dentry_operations nfs4_dentry_operations = {
	.d_revalidate	= nfs4_lookup_revalidate,
	.d_weak_revalidate	= nfs_weak_revalidate,
	.d_delete	= nfs_dentry_delete,
	.d_iput		= nfs_dentry_iput,
	.d_automount	= nfs_d_automount,
	.d_release	= nfs_d_release,
};
EXPORT_SYMBOL_GPL(nfs4_dentry_operations);

static fmode_t flags_to_mode(int flags)
{
	fmode_t res = (__force fmode_t)flags & FMODE_EXEC;
	if ((flags & O_ACCMODE) != O_WRONLY)
		res |= FMODE_READ;
	if ((flags & O_ACCMODE) != O_RDONLY)
		res |= FMODE_WRITE;
	return res;
}

static struct nfs_open_context *create_nfs_open_context(struct dentry *dentry, int open_flags, struct file *filp)
{
	return alloc_nfs_open_context(dentry, flags_to_mode(open_flags), filp);
}

static int do_open(struct inode *inode, struct file *filp)
{
	nfs_fscache_open_file(inode, filp);
	return 0;
}

static int nfs_finish_open(struct nfs_open_context *ctx,
			   struct dentry *dentry,
			   struct file *file, unsigned open_flags)
{
	int err;

	err = finish_open(file, dentry, do_open);
	if (err)
		goto out;
	if (S_ISREG(file->f_path.dentry->d_inode->i_mode))
		nfs_file_set_open_context(file, ctx);
	else
		err = -EOPENSTALE;
out:
	return err;
}

int nfs_atomic_open(struct inode *dir, struct dentry *dentry,
		    struct file *file, unsigned open_flags,
		    umode_t mode)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
	struct nfs_open_context *ctx;
	struct dentry *res;
	struct iattr attr = { .ia_valid = ATTR_OPEN };
	struct inode *inode;
	unsigned int lookup_flags = 0;
	bool switched = false;
	int created = 0;
	int err;

	/* Expect a negative dentry */
	BUG_ON(d_inode(dentry));

	dfprintk(VFS, "NFS: atomic_open(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	err = nfs_check_flags(open_flags);
	if (err)
		return err;

	/* NFS only supports OPEN on regular files */
	if ((open_flags & O_DIRECTORY)) {
		if (!d_in_lookup(dentry)) {
			/*
			 * Hashed negative dentry with O_DIRECTORY: dentry was
			 * revalidated and is fine, no need to perform lookup
			 * again
			 */
			return -ENOENT;
		}
		lookup_flags = LOOKUP_OPEN|LOOKUP_DIRECTORY;
		goto no_open;
	}

	if (dentry->d_name.len > NFS_SERVER(dir)->namelen)
		return -ENAMETOOLONG;

	if (open_flags & O_CREAT) {
		struct nfs_server *server = NFS_SERVER(dir);

		if (!(server->attr_bitmask[2] & FATTR4_WORD2_MODE_UMASK))
			mode &= ~current_umask();

		attr.ia_valid |= ATTR_MODE;
		attr.ia_mode = mode;
	}
	if (open_flags & O_TRUNC) {
		attr.ia_valid |= ATTR_SIZE;
		attr.ia_size = 0;
	}

	if (!(open_flags & O_CREAT) && !d_in_lookup(dentry)) {
		d_drop(dentry);
		switched = true;
		dentry = d_alloc_parallel(dentry->d_parent,
					  &dentry->d_name, &wq);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
		if (unlikely(!d_in_lookup(dentry)))
			return finish_no_open(file, dentry);
	}

	ctx = create_nfs_open_context(dentry, open_flags, file);
	err = PTR_ERR(ctx);
	if (IS_ERR(ctx))
		goto out;

	trace_nfs_atomic_open_enter(dir, ctx, open_flags);
	inode = NFS_PROTO(dir)->open_context(dir, ctx, open_flags, &attr, &created);
	if (created)
		file->f_mode |= FMODE_CREATED;
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		trace_nfs_atomic_open_exit(dir, ctx, open_flags, err);
		put_nfs_open_context(ctx);
		d_drop(dentry);
		switch (err) {
		case -ENOENT:
			d_splice_alias(NULL, dentry);
			nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
			break;
		case -EISDIR:
		case -ENOTDIR:
			goto no_open;
		case -ELOOP:
			if (!(open_flags & O_NOFOLLOW))
				goto no_open;
			break;
			/* case -EINVAL: */
		default:
			break;
		}
		goto out;
	}

	err = nfs_finish_open(ctx, ctx->dentry, file, open_flags);
	trace_nfs_atomic_open_exit(dir, ctx, open_flags, err);
	put_nfs_open_context(ctx);
out:
	if (unlikely(switched)) {
		d_lookup_done(dentry);
		dput(dentry);
	}
	return err;

no_open:
	res = nfs_lookup(dir, dentry, lookup_flags);
	if (switched) {
		d_lookup_done(dentry);
		if (!res)
			res = dentry;
		else
			dput(dentry);
	}
	if (IS_ERR(res))
		return PTR_ERR(res);
	return finish_no_open(file, res);
}
EXPORT_SYMBOL_GPL(nfs_atomic_open);

static int
nfs4_do_lookup_revalidate(struct inode *dir, struct dentry *dentry,
			  unsigned int flags)
{
	struct inode *inode;

	if (!(flags & LOOKUP_OPEN) || (flags & LOOKUP_DIRECTORY))
		goto full_reval;
	if (d_mountpoint(dentry))
		goto full_reval;

	inode = d_inode(dentry);

	/* We can't create new files in nfs_open_revalidate(), so we
	 * optimize away revalidation of negative dentries.
	 */
	if (inode == NULL)
		goto full_reval;

	if (nfs_verifier_is_delegated(dentry))
		return nfs_lookup_revalidate_delegated(dir, dentry, inode);

	/* NFS only supports OPEN on regular files */
	if (!S_ISREG(inode->i_mode))
		goto full_reval;

	/* We cannot do exclusive creation on a positive dentry */
	if (flags & (LOOKUP_EXCL | LOOKUP_REVAL))
		goto reval_dentry;

	/* Check if the directory changed */
	if (!nfs_check_verifier(dir, dentry, flags & LOOKUP_RCU))
		goto reval_dentry;

	/* Let f_op->open() actually open (and revalidate) the file */
	return 1;
reval_dentry:
	if (flags & LOOKUP_RCU)
		return -ECHILD;
	return nfs_lookup_revalidate_dentry(dir, dentry, inode);

full_reval:
	return nfs_do_lookup_revalidate(dir, dentry, flags);
}

static int nfs4_lookup_revalidate(struct dentry *dentry, unsigned int flags)
{
	return __nfs_lookup_revalidate(dentry, flags,
			nfs4_do_lookup_revalidate);
}

#endif /* CONFIG_NFSV4 */

struct dentry *
nfs_add_or_obtain(struct dentry *dentry, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr,
				struct nfs4_label *label)
{
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = d_inode(parent);
	struct inode *inode;
	struct dentry *d;
	int error;

	d_drop(dentry);

	if (fhandle->size == 0) {
		error = NFS_PROTO(dir)->lookup(dir, dentry, fhandle, fattr, NULL);
		if (error)
			goto out_error;
	}
	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
	if (!(fattr->valid & NFS_ATTR_FATTR)) {
		struct nfs_server *server = NFS_SB(dentry->d_sb);
		error = server->nfs_client->rpc_ops->getattr(server, fhandle,
				fattr, NULL, NULL);
		if (error < 0)
			goto out_error;
	}
	inode = nfs_fhget(dentry->d_sb, fhandle, fattr, label);
	d = d_splice_alias(inode, dentry);
out:
	dput(parent);
	return d;
out_error:
	d = ERR_PTR(error);
	goto out;
}
EXPORT_SYMBOL_GPL(nfs_add_or_obtain);

/*
 * Code common to create, mkdir, and mknod.
 */
int nfs_instantiate(struct dentry *dentry, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr,
				struct nfs4_label *label)
{
	struct dentry *d;

	d = nfs_add_or_obtain(dentry, fhandle, fattr, label);
	if (IS_ERR(d))
		return PTR_ERR(d);

	/* Callers don't care */
	dput(d);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_instantiate);

/*
 * Following a failed create operation, we drop the dentry rather
 * than retain a negative dentry. This avoids a problem in the event
 * that the operation succeeded on the server, but an error in the
 * reply path made it appear to have failed.
 */
int nfs_create(struct user_namespace *mnt_userns, struct inode *dir,
	       struct dentry *dentry, umode_t mode, bool excl)
{
	struct iattr attr;
	int open_flags = excl ? O_CREAT | O_EXCL : O_CREAT;
	int error;

	dfprintk(VFS, "NFS: create(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	attr.ia_mode = mode;
	attr.ia_valid = ATTR_MODE;

	trace_nfs_create_enter(dir, dentry, open_flags);
	error = NFS_PROTO(dir)->create(dir, dentry, &attr, open_flags);
	trace_nfs_create_exit(dir, dentry, open_flags, error);
	if (error != 0)
		goto out_err;
	return 0;
out_err:
	d_drop(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_create);

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
int
nfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
	  struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct iattr attr;
	int status;

	dfprintk(VFS, "NFS: mknod(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	attr.ia_mode = mode;
	attr.ia_valid = ATTR_MODE;

	trace_nfs_mknod_enter(dir, dentry);
	status = NFS_PROTO(dir)->mknod(dir, dentry, &attr, rdev);
	trace_nfs_mknod_exit(dir, dentry, status);
	if (status != 0)
		goto out_err;
	return 0;
out_err:
	d_drop(dentry);
	return status;
}
EXPORT_SYMBOL_GPL(nfs_mknod);

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
int nfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
	      struct dentry *dentry, umode_t mode)
{
	struct iattr attr;
	int error;

	dfprintk(VFS, "NFS: mkdir(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	attr.ia_valid = ATTR_MODE;
	attr.ia_mode = mode | S_IFDIR;

	trace_nfs_mkdir_enter(dir, dentry);
	error = NFS_PROTO(dir)->mkdir(dir, dentry, &attr);
	trace_nfs_mkdir_exit(dir, dentry, error);
	if (error != 0)
		goto out_err;
	return 0;
out_err:
	d_drop(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_mkdir);

static void nfs_dentry_handle_enoent(struct dentry *dentry)
{
	if (simple_positive(dentry))
		d_delete(dentry);
}

int nfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: rmdir(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	trace_nfs_rmdir_enter(dir, dentry);
	if (d_really_is_positive(dentry)) {
		down_write(&NFS_I(d_inode(dentry))->rmdir_sem);
		error = NFS_PROTO(dir)->rmdir(dir, &dentry->d_name);
		/* Ensure the VFS deletes this inode */
		switch (error) {
		case 0:
			clear_nlink(d_inode(dentry));
			break;
		case -ENOENT:
			nfs_dentry_handle_enoent(dentry);
		}
		up_write(&NFS_I(d_inode(dentry))->rmdir_sem);
	} else
		error = NFS_PROTO(dir)->rmdir(dir, &dentry->d_name);
	trace_nfs_rmdir_exit(dir, dentry, error);

	return error;
}
EXPORT_SYMBOL_GPL(nfs_rmdir);

/*
 * Remove a file after making sure there are no pending writes,
 * and after checking that the file has only one user. 
 *
 * We invalidate the attribute cache and free the inode prior to the operation
 * to avoid possible races if the server reuses the inode.
 */
static int nfs_safe_remove(struct dentry *dentry)
{
	struct inode *dir = d_inode(dentry->d_parent);
	struct inode *inode = d_inode(dentry);
	int error = -EBUSY;
		
	dfprintk(VFS, "NFS: safe_remove(%pd2)\n", dentry);

	/* If the dentry was sillyrenamed, we simply call d_delete() */
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		error = 0;
		goto out;
	}

	trace_nfs_remove_enter(dir, dentry);
	if (inode != NULL) {
		error = NFS_PROTO(dir)->remove(dir, dentry);
		if (error == 0)
			nfs_drop_nlink(inode);
	} else
		error = NFS_PROTO(dir)->remove(dir, dentry);
	if (error == -ENOENT)
		nfs_dentry_handle_enoent(dentry);
	trace_nfs_remove_exit(dir, dentry, error);
out:
	return error;
}

/*  We do silly rename. In case sillyrename() returns -EBUSY, the inode
 *  belongs to an active ".nfs..." file and we return -EBUSY.
 *
 *  If sillyrename() returns 0, we do nothing, otherwise we unlink.
 */
int nfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;
	int need_rehash = 0;

	dfprintk(VFS, "NFS: unlink(%s/%lu, %pd)\n", dir->i_sb->s_id,
		dir->i_ino, dentry);

	trace_nfs_unlink_enter(dir, dentry);
	spin_lock(&dentry->d_lock);
	if (d_count(dentry) > 1) {
		spin_unlock(&dentry->d_lock);
		/* Start asynchronous writeout of the inode */
		write_inode_now(d_inode(dentry), 0);
		error = nfs_sillyrename(dir, dentry);
		goto out;
	}
	if (!d_unhashed(dentry)) {
		__d_drop(dentry);
		need_rehash = 1;
	}
	spin_unlock(&dentry->d_lock);
	error = nfs_safe_remove(dentry);
	if (!error || error == -ENOENT) {
		nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
	} else if (need_rehash)
		d_rehash(dentry);
out:
	trace_nfs_unlink_exit(dir, dentry, error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_unlink);

/*
 * To create a symbolic link, most file systems instantiate a new inode,
 * add a page to it containing the path, then write it out to the disk
 * using prepare_write/commit_write.
 *
 * Unfortunately the NFS client can't create the in-core inode first
 * because it needs a file handle to create an in-core inode (see
 * fs/nfs/inode.c:nfs_fhget).  We only have a file handle *after* the
 * symlink request has completed on the server.
 *
 * So instead we allocate a raw page, copy the symname into it, then do
 * the SYMLINK request with the page as the buffer.  If it succeeds, we
 * now have a new file handle and can instantiate an in-core NFS inode
 * and move the raw page into its mapping.
 */
int nfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
		struct dentry *dentry, const char *symname)
{
	struct page *page;
	char *kaddr;
	struct iattr attr;
	unsigned int pathlen = strlen(symname);
	int error;

	dfprintk(VFS, "NFS: symlink(%s/%lu, %pd, %s)\n", dir->i_sb->s_id,
		dir->i_ino, dentry, symname);

	if (pathlen > PAGE_SIZE)
		return -ENAMETOOLONG;

	attr.ia_mode = S_IFLNK | S_IRWXUGO;
	attr.ia_valid = ATTR_MODE;

	page = alloc_page(GFP_USER);
	if (!page)
		return -ENOMEM;

	kaddr = page_address(page);
	memcpy(kaddr, symname, pathlen);
	if (pathlen < PAGE_SIZE)
		memset(kaddr + pathlen, 0, PAGE_SIZE - pathlen);

	trace_nfs_symlink_enter(dir, dentry);
	error = NFS_PROTO(dir)->symlink(dir, dentry, page, pathlen, &attr);
	trace_nfs_symlink_exit(dir, dentry, error);
	if (error != 0) {
		dfprintk(VFS, "NFS: symlink(%s/%lu, %pd, %s) error %d\n",
			dir->i_sb->s_id, dir->i_ino,
			dentry, symname, error);
		d_drop(dentry);
		__free_page(page);
		return error;
	}

	/*
	 * No big deal if we can't add this page to the page cache here.
	 * READLINK will get the missing page from the server if needed.
	 */
	if (!add_to_page_cache_lru(page, d_inode(dentry)->i_mapping, 0,
							GFP_KERNEL)) {
		SetPageUptodate(page);
		unlock_page(page);
		/*
		 * add_to_page_cache_lru() grabs an extra page refcount.
		 * Drop it here to avoid leaking this page later.
		 */
		put_page(page);
	} else
		__free_page(page);

	return 0;
}
EXPORT_SYMBOL_GPL(nfs_symlink);

int
nfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	int error;

	dfprintk(VFS, "NFS: link(%pd2 -> %pd2)\n",
		old_dentry, dentry);

	trace_nfs_link_enter(inode, dir, dentry);
	d_drop(dentry);
	error = NFS_PROTO(dir)->link(inode, dir, &dentry->d_name);
	if (error == 0) {
		ihold(inode);
		d_add(dentry, inode);
	}
	trace_nfs_link_exit(inode, dir, dentry, error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_link);

/*
 * RENAME
 * FIXME: Some nfsds, like the Linux user space nfsd, may generate a
 * different file handle for the same inode after a rename (e.g. when
 * moving to a different directory). A fail-safe method to do so would
 * be to look up old_dir/old_name, create a link to new_dir/new_name and
 * rename the old file using the sillyrename stuff. This way, the original
 * file in old_dir will go away when the last process iput()s the inode.
 *
 * FIXED.
 * 
 * It actually works quite well. One needs to have the possibility for
 * at least one ".nfs..." file in each directory the file ever gets
 * moved or linked to which happens automagically with the new
 * implementation that only depends on the dcache stuff instead of
 * using the inode layer
 *
 * Unfortunately, things are a little more complicated than indicated
 * above. For a cross-directory move, we want to make sure we can get
 * rid of the old inode after the operation.  This means there must be
 * no pending writes (if it's a file), and the use count must be 1.
 * If these conditions are met, we can drop the dentries before doing
 * the rename.
 */
int nfs_rename(struct user_namespace *mnt_userns, struct inode *old_dir,
	       struct dentry *old_dentry, struct inode *new_dir,
	       struct dentry *new_dentry, unsigned int flags)
{
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct dentry *dentry = NULL, *rehash = NULL;
	struct rpc_task *task;
	int error = -EBUSY;

	if (flags)
		return -EINVAL;

	dfprintk(VFS, "NFS: rename(%pd2 -> %pd2, ct=%d)\n",
		 old_dentry, new_dentry,
		 d_count(new_dentry));

	trace_nfs_rename_enter(old_dir, old_dentry, new_dir, new_dentry);
	/*
	 * For non-directories, check whether the target is busy and if so,
	 * make a copy of the dentry and then do a silly-rename. If the
	 * silly-rename succeeds, the copied dentry is hashed and becomes
	 * the new target.
	 */
	if (new_inode && !S_ISDIR(new_inode->i_mode)) {
		/*
		 * To prevent any new references to the target during the
		 * rename, we unhash the dentry in advance.
		 */
		if (!d_unhashed(new_dentry)) {
			d_drop(new_dentry);
			rehash = new_dentry;
		}

		if (d_count(new_dentry) > 2) {
			int err;

			/* copy the target dentry's name */
			dentry = d_alloc(new_dentry->d_parent,
					 &new_dentry->d_name);
			if (!dentry)
				goto out;

			/* silly-rename the existing target ... */
			err = nfs_sillyrename(new_dir, new_dentry);
			if (err)
				goto out;

			new_dentry = dentry;
			rehash = NULL;
			new_inode = NULL;
		}
	}

	task = nfs_async_rename(old_dir, new_dir, old_dentry, new_dentry, NULL);
	if (IS_ERR(task)) {
		error = PTR_ERR(task);
		goto out;
	}

	error = rpc_wait_for_completion_task(task);
	if (error != 0) {
		((struct nfs_renamedata *)task->tk_calldata)->cancelled = 1;
		/* Paired with the atomic_dec_and_test() barrier in rpc_do_put_task() */
		smp_wmb();
	} else
		error = task->tk_status;
	rpc_put_task(task);
	/* Ensure the inode attributes are revalidated */
	if (error == 0) {
		spin_lock(&old_inode->i_lock);
		NFS_I(old_inode)->attr_gencount = nfs_inc_attr_generation_counter();
		nfs_set_cache_invalid(old_inode, NFS_INO_INVALID_CHANGE |
							 NFS_INO_INVALID_CTIME |
							 NFS_INO_REVAL_FORCED);
		spin_unlock(&old_inode->i_lock);
	}
out:
	if (rehash)
		d_rehash(rehash);
	trace_nfs_rename_exit(old_dir, old_dentry,
			new_dir, new_dentry, error);
	if (!error) {
		if (new_inode != NULL)
			nfs_drop_nlink(new_inode);
		/*
		 * The d_move() should be here instead of in an async RPC completion
		 * handler because we need the proper locks to move the dentry.  If
		 * we're interrupted by a signal, the async RPC completion handler
		 * should mark the directories for revalidation.
		 */
		d_move(old_dentry, new_dentry);
		nfs_set_verifier(old_dentry,
					nfs_save_change_attribute(new_dir));
	} else if (error == -ENOENT)
		nfs_dentry_handle_enoent(old_dentry);

	/* new dentry created? */
	if (dentry)
		dput(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_rename);

static DEFINE_SPINLOCK(nfs_access_lru_lock);
static LIST_HEAD(nfs_access_lru_list);
static atomic_long_t nfs_access_nr_entries;

static unsigned long nfs_access_max_cachesize = 4*1024*1024;
module_param(nfs_access_max_cachesize, ulong, 0644);
MODULE_PARM_DESC(nfs_access_max_cachesize, "NFS access maximum total cache length");

static void nfs_access_free_entry(struct nfs_access_entry *entry)
{
	put_cred(entry->cred);
	kfree_rcu(entry, rcu_head);
	smp_mb__before_atomic();
	atomic_long_dec(&nfs_access_nr_entries);
	smp_mb__after_atomic();
}

static void nfs_access_free_list(struct list_head *head)
{
	struct nfs_access_entry *cache;

	while (!list_empty(head)) {
		cache = list_entry(head->next, struct nfs_access_entry, lru);
		list_del(&cache->lru);
		nfs_access_free_entry(cache);
	}
}

static unsigned long
nfs_do_access_cache_scan(unsigned int nr_to_scan)
{
	LIST_HEAD(head);
	struct nfs_inode *nfsi, *next;
	struct nfs_access_entry *cache;
	long freed = 0;

	spin_lock(&nfs_access_lru_lock);
	list_for_each_entry_safe(nfsi, next, &nfs_access_lru_list, access_cache_inode_lru) {
		struct inode *inode;

		if (nr_to_scan-- == 0)
			break;
		inode = &nfsi->vfs_inode;
		spin_lock(&inode->i_lock);
		if (list_empty(&nfsi->access_cache_entry_lru))
			goto remove_lru_entry;
		cache = list_entry(nfsi->access_cache_entry_lru.next,
				struct nfs_access_entry, lru);
		list_move(&cache->lru, &head);
		rb_erase(&cache->rb_node, &nfsi->access_cache);
		freed++;
		if (!list_empty(&nfsi->access_cache_entry_lru))
			list_move_tail(&nfsi->access_cache_inode_lru,
					&nfs_access_lru_list);
		else {
remove_lru_entry:
			list_del_init(&nfsi->access_cache_inode_lru);
			smp_mb__before_atomic();
			clear_bit(NFS_INO_ACL_LRU_SET, &nfsi->flags);
			smp_mb__after_atomic();
		}
		spin_unlock(&inode->i_lock);
	}
	spin_unlock(&nfs_access_lru_lock);
	nfs_access_free_list(&head);
	return freed;
}

unsigned long
nfs_access_cache_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	int nr_to_scan = sc->nr_to_scan;
	gfp_t gfp_mask = sc->gfp_mask;

	if ((gfp_mask & GFP_KERNEL) != GFP_KERNEL)
		return SHRINK_STOP;
	return nfs_do_access_cache_scan(nr_to_scan);
}


unsigned long
nfs_access_cache_count(struct shrinker *shrink, struct shrink_control *sc)
{
	return vfs_pressure_ratio(atomic_long_read(&nfs_access_nr_entries));
}

static void
nfs_access_cache_enforce_limit(void)
{
	long nr_entries = atomic_long_read(&nfs_access_nr_entries);
	unsigned long diff;
	unsigned int nr_to_scan;

	if (nr_entries < 0 || nr_entries <= nfs_access_max_cachesize)
		return;
	nr_to_scan = 100;
	diff = nr_entries - nfs_access_max_cachesize;
	if (diff < nr_to_scan)
		nr_to_scan = diff;
	nfs_do_access_cache_scan(nr_to_scan);
}

static void __nfs_access_zap_cache(struct nfs_inode *nfsi, struct list_head *head)
{
	struct rb_root *root_node = &nfsi->access_cache;
	struct rb_node *n;
	struct nfs_access_entry *entry;

	/* Unhook entries from the cache */
	while ((n = rb_first(root_node)) != NULL) {
		entry = rb_entry(n, struct nfs_access_entry, rb_node);
		rb_erase(n, root_node);
		list_move(&entry->lru, head);
	}
	nfsi->cache_validity &= ~NFS_INO_INVALID_ACCESS;
}

void nfs_access_zap_cache(struct inode *inode)
{
	LIST_HEAD(head);

	if (test_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags) == 0)
		return;
	/* Remove from global LRU init */
	spin_lock(&nfs_access_lru_lock);
	if (test_and_clear_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags))
		list_del_init(&NFS_I(inode)->access_cache_inode_lru);

	spin_lock(&inode->i_lock);
	__nfs_access_zap_cache(NFS_I(inode), &head);
	spin_unlock(&inode->i_lock);
	spin_unlock(&nfs_access_lru_lock);
	nfs_access_free_list(&head);
}
EXPORT_SYMBOL_GPL(nfs_access_zap_cache);

static struct nfs_access_entry *nfs_access_search_rbtree(struct inode *inode, const struct cred *cred)
{
	struct rb_node *n = NFS_I(inode)->access_cache.rb_node;

	while (n != NULL) {
		struct nfs_access_entry *entry =
			rb_entry(n, struct nfs_access_entry, rb_node);
		int cmp = cred_fscmp(cred, entry->cred);

		if (cmp < 0)
			n = n->rb_left;
		else if (cmp > 0)
			n = n->rb_right;
		else
			return entry;
	}
	return NULL;
}

static int nfs_access_get_cached_locked(struct inode *inode, const struct cred *cred, struct nfs_access_entry *res, bool may_block)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_access_entry *cache;
	bool retry = true;
	int err;

	spin_lock(&inode->i_lock);
	for(;;) {
		if (nfsi->cache_validity & NFS_INO_INVALID_ACCESS)
			goto out_zap;
		cache = nfs_access_search_rbtree(inode, cred);
		err = -ENOENT;
		if (cache == NULL)
			goto out;
		/* Found an entry, is our attribute cache valid? */
		if (!nfs_check_cache_invalid(inode, NFS_INO_INVALID_ACCESS))
			break;
		if (!retry)
			break;
		err = -ECHILD;
		if (!may_block)
			goto out;
		spin_unlock(&inode->i_lock);
		err = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
		if (err)
			return err;
		spin_lock(&inode->i_lock);
		retry = false;
	}
	res->cred = cache->cred;
	res->mask = cache->mask;
	list_move_tail(&cache->lru, &nfsi->access_cache_entry_lru);
	err = 0;
out:
	spin_unlock(&inode->i_lock);
	return err;
out_zap:
	spin_unlock(&inode->i_lock);
	nfs_access_zap_cache(inode);
	return -ENOENT;
}

static int nfs_access_get_cached_rcu(struct inode *inode, const struct cred *cred, struct nfs_access_entry *res)
{
	/* Only check the most recently returned cache entry,
	 * but do it without locking.
	 */
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_access_entry *cache;
	int err = -ECHILD;
	struct list_head *lh;

	rcu_read_lock();
	if (nfsi->cache_validity & NFS_INO_INVALID_ACCESS)
		goto out;
	lh = rcu_dereference(list_tail_rcu(&nfsi->access_cache_entry_lru));
	cache = list_entry(lh, struct nfs_access_entry, lru);
	if (lh == &nfsi->access_cache_entry_lru ||
	    cred_fscmp(cred, cache->cred) != 0)
		cache = NULL;
	if (cache == NULL)
		goto out;
	if (nfs_check_cache_invalid(inode, NFS_INO_INVALID_ACCESS))
		goto out;
	res->cred = cache->cred;
	res->mask = cache->mask;
	err = 0;
out:
	rcu_read_unlock();
	return err;
}

int nfs_access_get_cached(struct inode *inode, const struct cred *cred, struct
nfs_access_entry *res, bool may_block)
{
	int status;

	status = nfs_access_get_cached_rcu(inode, cred, res);
	if (status != 0)
		status = nfs_access_get_cached_locked(inode, cred, res,
		    may_block);

	return status;
}
EXPORT_SYMBOL_GPL(nfs_access_get_cached);

static void nfs_access_add_rbtree(struct inode *inode, struct nfs_access_entry *set)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct rb_root *root_node = &nfsi->access_cache;
	struct rb_node **p = &root_node->rb_node;
	struct rb_node *parent = NULL;
	struct nfs_access_entry *entry;
	int cmp;

	spin_lock(&inode->i_lock);
	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct nfs_access_entry, rb_node);
		cmp = cred_fscmp(set->cred, entry->cred);

		if (cmp < 0)
			p = &parent->rb_left;
		else if (cmp > 0)
			p = &parent->rb_right;
		else
			goto found;
	}
	rb_link_node(&set->rb_node, parent, p);
	rb_insert_color(&set->rb_node, root_node);
	list_add_tail(&set->lru, &nfsi->access_cache_entry_lru);
	spin_unlock(&inode->i_lock);
	return;
found:
	rb_replace_node(parent, &set->rb_node, root_node);
	list_add_tail(&set->lru, &nfsi->access_cache_entry_lru);
	list_del(&entry->lru);
	spin_unlock(&inode->i_lock);
	nfs_access_free_entry(entry);
}

void nfs_access_add_cache(struct inode *inode, struct nfs_access_entry *set)
{
	struct nfs_access_entry *cache = kmalloc(sizeof(*cache), GFP_KERNEL);
	if (cache == NULL)
		return;
	RB_CLEAR_NODE(&cache->rb_node);
	cache->cred = get_cred(set->cred);
	cache->mask = set->mask;

	/* The above field assignments must be visible
	 * before this item appears on the lru.  We cannot easily
	 * use rcu_assign_pointer, so just force the memory barrier.
	 */
	smp_wmb();
	nfs_access_add_rbtree(inode, cache);

	/* Update accounting */
	smp_mb__before_atomic();
	atomic_long_inc(&nfs_access_nr_entries);
	smp_mb__after_atomic();

	/* Add inode to global LRU list */
	if (!test_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags)) {
		spin_lock(&nfs_access_lru_lock);
		if (!test_and_set_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags))
			list_add_tail(&NFS_I(inode)->access_cache_inode_lru,
					&nfs_access_lru_list);
		spin_unlock(&nfs_access_lru_lock);
	}
	nfs_access_cache_enforce_limit();
}
EXPORT_SYMBOL_GPL(nfs_access_add_cache);

#define NFS_MAY_READ (NFS_ACCESS_READ)
#define NFS_MAY_WRITE (NFS_ACCESS_MODIFY | \
		NFS_ACCESS_EXTEND | \
		NFS_ACCESS_DELETE)
#define NFS_FILE_MAY_WRITE (NFS_ACCESS_MODIFY | \
		NFS_ACCESS_EXTEND)
#define NFS_DIR_MAY_WRITE NFS_MAY_WRITE
#define NFS_MAY_LOOKUP (NFS_ACCESS_LOOKUP)
#define NFS_MAY_EXECUTE (NFS_ACCESS_EXECUTE)
static int
nfs_access_calc_mask(u32 access_result, umode_t umode)
{
	int mask = 0;

	if (access_result & NFS_MAY_READ)
		mask |= MAY_READ;
	if (S_ISDIR(umode)) {
		if ((access_result & NFS_DIR_MAY_WRITE) == NFS_DIR_MAY_WRITE)
			mask |= MAY_WRITE;
		if ((access_result & NFS_MAY_LOOKUP) == NFS_MAY_LOOKUP)
			mask |= MAY_EXEC;
	} else if (S_ISREG(umode)) {
		if ((access_result & NFS_FILE_MAY_WRITE) == NFS_FILE_MAY_WRITE)
			mask |= MAY_WRITE;
		if ((access_result & NFS_MAY_EXECUTE) == NFS_MAY_EXECUTE)
			mask |= MAY_EXEC;
	} else if (access_result & NFS_MAY_WRITE)
			mask |= MAY_WRITE;
	return mask;
}

void nfs_access_set_mask(struct nfs_access_entry *entry, u32 access_result)
{
	entry->mask = access_result;
}
EXPORT_SYMBOL_GPL(nfs_access_set_mask);

static int nfs_do_access(struct inode *inode, const struct cred *cred, int mask)
{
	struct nfs_access_entry cache;
	bool may_block = (mask & MAY_NOT_BLOCK) == 0;
	int cache_mask = -1;
	int status;

	trace_nfs_access_enter(inode);

	status = nfs_access_get_cached(inode, cred, &cache, may_block);
	if (status == 0)
		goto out_cached;

	status = -ECHILD;
	if (!may_block)
		goto out;

	/*
	 * Determine which access bits we want to ask for...
	 */
	cache.mask = NFS_ACCESS_READ | NFS_ACCESS_MODIFY | NFS_ACCESS_EXTEND;
	if (nfs_server_capable(inode, NFS_CAP_XATTR)) {
		cache.mask |= NFS_ACCESS_XAREAD | NFS_ACCESS_XAWRITE |
		    NFS_ACCESS_XALIST;
	}
	if (S_ISDIR(inode->i_mode))
		cache.mask |= NFS_ACCESS_DELETE | NFS_ACCESS_LOOKUP;
	else
		cache.mask |= NFS_ACCESS_EXECUTE;
	cache.cred = cred;
	status = NFS_PROTO(inode)->access(inode, &cache);
	if (status != 0) {
		if (status == -ESTALE) {
			if (!S_ISDIR(inode->i_mode))
				nfs_set_inode_stale(inode);
			else
				nfs_zap_caches(inode);
		}
		goto out;
	}
	nfs_access_add_cache(inode, &cache);
out_cached:
	cache_mask = nfs_access_calc_mask(cache.mask, inode->i_mode);
	if ((mask & ~cache_mask & (MAY_READ | MAY_WRITE | MAY_EXEC)) != 0)
		status = -EACCES;
out:
	trace_nfs_access_exit(inode, mask, cache_mask, status);
	return status;
}

static int nfs_open_permission_mask(int openflags)
{
	int mask = 0;

	if (openflags & __FMODE_EXEC) {
		/* ONLY check exec rights */
		mask = MAY_EXEC;
	} else {
		if ((openflags & O_ACCMODE) != O_WRONLY)
			mask |= MAY_READ;
		if ((openflags & O_ACCMODE) != O_RDONLY)
			mask |= MAY_WRITE;
	}

	return mask;
}

int nfs_may_open(struct inode *inode, const struct cred *cred, int openflags)
{
	return nfs_do_access(inode, cred, nfs_open_permission_mask(openflags));
}
EXPORT_SYMBOL_GPL(nfs_may_open);

static int nfs_execute_ok(struct inode *inode, int mask)
{
	struct nfs_server *server = NFS_SERVER(inode);
	int ret = 0;

	if (S_ISDIR(inode->i_mode))
		return 0;
	if (nfs_check_cache_invalid(inode, NFS_INO_INVALID_MODE)) {
		if (mask & MAY_NOT_BLOCK)
			return -ECHILD;
		ret = __nfs_revalidate_inode(server, inode);
	}
	if (ret == 0 && !execute_ok(inode))
		ret = -EACCES;
	return ret;
}

int nfs_permission(struct user_namespace *mnt_userns,
		   struct inode *inode,
		   int mask)
{
	const struct cred *cred = current_cred();
	int res = 0;

	nfs_inc_stats(inode, NFSIOS_VFSACCESS);

	if ((mask & (MAY_READ | MAY_WRITE | MAY_EXEC)) == 0)
		goto out;
	/* Is this sys_access() ? */
	if (mask & (MAY_ACCESS | MAY_CHDIR))
		goto force_lookup;

	switch (inode->i_mode & S_IFMT) {
		case S_IFLNK:
			goto out;
		case S_IFREG:
			if ((mask & MAY_OPEN) &&
			   nfs_server_capable(inode, NFS_CAP_ATOMIC_OPEN))
				return 0;
			break;
		case S_IFDIR:
			/*
			 * Optimize away all write operations, since the server
			 * will check permissions when we perform the op.
			 */
			if ((mask & MAY_WRITE) && !(mask & MAY_READ))
				goto out;
	}

force_lookup:
	if (!NFS_PROTO(inode)->access)
		goto out_notsup;

	res = nfs_do_access(inode, cred, mask);
out:
	if (!res && (mask & MAY_EXEC))
		res = nfs_execute_ok(inode, mask);

	dfprintk(VFS, "NFS: permission(%s/%lu), mask=0x%x, res=%d\n",
		inode->i_sb->s_id, inode->i_ino, mask, res);
	return res;
out_notsup:
	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	res = nfs_revalidate_inode(inode, NFS_INO_INVALID_MODE |
						  NFS_INO_INVALID_OTHER);
	if (res == 0)
		res = generic_permission(&init_user_ns, inode, mask);
	goto out;
}
EXPORT_SYMBOL_GPL(nfs_permission);
