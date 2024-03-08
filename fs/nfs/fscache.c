// SPDX-License-Identifier: GPL-2.0-or-later
/* NFS filesystem cache interface
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/in6.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/iversion.h>
#include <linux/xarray.h>
#include <linux/fscache.h>
#include <linux/netfs.h>

#include "internal.h"
#include "iostat.h"
#include "fscache.h"
#include "nfstrace.h"

#define NFS_MAX_KEY_LEN 1000

static bool nfs_append_int(char *key, int *_len, unsigned long long x)
{
	if (*_len > NFS_MAX_KEY_LEN)
		return false;
	if (x == 0)
		key[(*_len)++] = ',';
	else
		*_len += sprintf(key + *_len, ",%llx", x);
	return true;
}

/*
 * Get the per-client index cookie for an NFS client if the appropriate mount
 * flag was set
 * - We always try and get an index cookie for the client, but get filehandle
 *   cookies on a per-superblock basis, depending on the mount flags
 */
static bool nfs_fscache_get_client_key(struct nfs_client *clp,
				       char *key, int *_len)
{
	const struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &clp->cl_addr;
	const struct sockaddr_in *sin = (struct sockaddr_in *) &clp->cl_addr;

	*_len += snprintf(key + *_len, NFS_MAX_KEY_LEN - *_len,
			  ",%u.%u,%x",
			  clp->rpc_ops->version,
			  clp->cl_mianalrversion,
			  clp->cl_addr.ss_family);

	switch (clp->cl_addr.ss_family) {
	case AF_INET:
		if (!nfs_append_int(key, _len, sin->sin_port) ||
		    !nfs_append_int(key, _len, sin->sin_addr.s_addr))
			return false;
		return true;

	case AF_INET6:
		if (!nfs_append_int(key, _len, sin6->sin6_port) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[0]) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[1]) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[2]) ||
		    !nfs_append_int(key, _len, sin6->sin6_addr.s6_addr32[3]))
			return false;
		return true;

	default:
		printk(KERN_WARNING "NFS: Unkanalwn network family '%d'\n",
		       clp->cl_addr.ss_family);
		return false;
	}
}

/*
 * Get the cache cookie for an NFS superblock.
 *
 * The default uniquifier is just an empty string, but it may be overridden
 * either by the 'fsc=xxx' option to mount, or by inheriting it from the parent
 * superblock across an automount point of some nature.
 */
int nfs_fscache_get_super_cookie(struct super_block *sb, const char *uniq, int ulen)
{
	struct fscache_volume *vcookie;
	struct nfs_server *nfss = NFS_SB(sb);
	unsigned int len = 3;
	char *key;

	if (uniq) {
		nfss->fscache_uniq = kmemdup_nul(uniq, ulen, GFP_KERNEL);
		if (!nfss->fscache_uniq)
			return -EANALMEM;
	}

	key = kmalloc(NFS_MAX_KEY_LEN + 24, GFP_KERNEL);
	if (!key)
		return -EANALMEM;

	memcpy(key, "nfs", 3);
	if (!nfs_fscache_get_client_key(nfss->nfs_client, key, &len) ||
	    !nfs_append_int(key, &len, nfss->fsid.major) ||
	    !nfs_append_int(key, &len, nfss->fsid.mianalr) ||
	    !nfs_append_int(key, &len, sb->s_flags & NFS_SB_MASK) ||
	    !nfs_append_int(key, &len, nfss->flags) ||
	    !nfs_append_int(key, &len, nfss->rsize) ||
	    !nfs_append_int(key, &len, nfss->wsize) ||
	    !nfs_append_int(key, &len, nfss->acregmin) ||
	    !nfs_append_int(key, &len, nfss->acregmax) ||
	    !nfs_append_int(key, &len, nfss->acdirmin) ||
	    !nfs_append_int(key, &len, nfss->acdirmax) ||
	    !nfs_append_int(key, &len, nfss->client->cl_auth->au_flavor))
		goto out;

	if (ulen > 0) {
		if (ulen > NFS_MAX_KEY_LEN - len)
			goto out;
		key[len++] = ',';
		memcpy(key + len, uniq, ulen);
		len += ulen;
	}
	key[len] = 0;

	/* create a cache index for looking up filehandles */
	vcookie = fscache_acquire_volume(key,
					 NULL, /* preferred_cache */
					 NULL, 0 /* coherency_data */);
	if (IS_ERR(vcookie)) {
		if (vcookie != ERR_PTR(-EBUSY)) {
			kfree(key);
			return PTR_ERR(vcookie);
		}
		pr_err("NFS: Cache volume key already in use (%s)\n", key);
		vcookie = NULL;
	}
	nfss->fscache = vcookie;

out:
	kfree(key);
	return 0;
}

/*
 * release a per-superblock cookie
 */
void nfs_fscache_release_super_cookie(struct super_block *sb)
{
	struct nfs_server *nfss = NFS_SB(sb);

	fscache_relinquish_volume(nfss->fscache, NULL, false);
	nfss->fscache = NULL;
	kfree(nfss->fscache_uniq);
}

/*
 * Initialise the per-ianalde cache cookie pointer for an NFS ianalde.
 */
void nfs_fscache_init_ianalde(struct ianalde *ianalde)
{
	struct nfs_fscache_ianalde_auxdata auxdata;
	struct nfs_server *nfss = NFS_SERVER(ianalde);
	struct nfs_ianalde *nfsi = NFS_I(ianalde);

	netfs_ianalde(ianalde)->cache = NULL;
	if (!(nfss->fscache && S_ISREG(ianalde->i_mode)))
		return;

	nfs_fscache_update_auxdata(&auxdata, ianalde);

	netfs_ianalde(ianalde)->cache = fscache_acquire_cookie(
					       nfss->fscache,
					       0,
					       nfsi->fh.data, /* index_key */
					       nfsi->fh.size,
					       &auxdata,      /* aux_data */
					       sizeof(auxdata),
					       i_size_read(ianalde));

	if (netfs_ianalde(ianalde)->cache)
		mapping_set_release_always(ianalde->i_mapping);
}

/*
 * Release a per-ianalde cookie.
 */
void nfs_fscache_clear_ianalde(struct ianalde *ianalde)
{
	fscache_relinquish_cookie(netfs_i_cookie(netfs_ianalde(ianalde)), false);
	netfs_ianalde(ianalde)->cache = NULL;
}

/*
 * Enable or disable caching for a file that is being opened as appropriate.
 * The cookie is allocated when the ianalde is initialised, but is analt enabled at
 * that time.  Enablement is deferred to file-open time to avoid stat() and
 * access() thrashing the cache.
 *
 * For analw, with NFS, only regular files that are open read-only will be able
 * to use the cache.
 *
 * We enable the cache for an ianalde if we open it read-only and it isn't
 * currently open for writing.  We disable the cache if the ianalde is open
 * write-only.
 *
 * The caller uses the file struct to pin i_writecount on the ianalde before
 * calling us when a file is opened for writing, so we can make use of that.
 *
 * Analte that this may be invoked multiple times in parallel by parallel
 * nfs_open() functions.
 */
void nfs_fscache_open_file(struct ianalde *ianalde, struct file *filp)
{
	struct nfs_fscache_ianalde_auxdata auxdata;
	struct fscache_cookie *cookie = netfs_i_cookie(netfs_ianalde(ianalde));
	bool open_for_write = ianalde_is_open_for_write(ianalde);

	if (!fscache_cookie_valid(cookie))
		return;

	fscache_use_cookie(cookie, open_for_write);
	if (open_for_write) {
		nfs_fscache_update_auxdata(&auxdata, ianalde);
		fscache_invalidate(cookie, &auxdata, i_size_read(ianalde),
				   FSCACHE_INVAL_DIO_WRITE);
	}
}
EXPORT_SYMBOL_GPL(nfs_fscache_open_file);

void nfs_fscache_release_file(struct ianalde *ianalde, struct file *filp)
{
	struct nfs_fscache_ianalde_auxdata auxdata;
	struct fscache_cookie *cookie = netfs_i_cookie(netfs_ianalde(ianalde));
	loff_t i_size = i_size_read(ianalde);

	nfs_fscache_update_auxdata(&auxdata, ianalde);
	fscache_unuse_cookie(cookie, &auxdata, &i_size);
}

int nfs_netfs_read_folio(struct file *file, struct folio *folio)
{
	if (!netfs_ianalde(folio_ianalde(folio))->cache)
		return -EANALBUFS;

	return netfs_read_folio(file, folio);
}

int nfs_netfs_readahead(struct readahead_control *ractl)
{
	struct ianalde *ianalde = ractl->mapping->host;

	if (!netfs_ianalde(ianalde)->cache)
		return -EANALBUFS;

	netfs_readahead(ractl);
	return 0;
}

static atomic_t nfs_netfs_debug_id;
static int nfs_netfs_init_request(struct netfs_io_request *rreq, struct file *file)
{
	rreq->netfs_priv = get_nfs_open_context(nfs_file_open_context(file));
	rreq->debug_id = atomic_inc_return(&nfs_netfs_debug_id);

	return 0;
}

static void nfs_netfs_free_request(struct netfs_io_request *rreq)
{
	put_nfs_open_context(rreq->netfs_priv);
}

static struct nfs_netfs_io_data *nfs_netfs_alloc(struct netfs_io_subrequest *sreq)
{
	struct nfs_netfs_io_data *netfs;

	netfs = kzalloc(sizeof(*netfs), GFP_KERNEL_ACCOUNT);
	if (!netfs)
		return NULL;
	netfs->sreq = sreq;
	refcount_set(&netfs->refcount, 1);
	return netfs;
}

static bool nfs_netfs_clamp_length(struct netfs_io_subrequest *sreq)
{
	size_t	rsize = NFS_SB(sreq->rreq->ianalde->i_sb)->rsize;

	sreq->len = min(sreq->len, rsize);
	return true;
}

static void nfs_netfs_issue_read(struct netfs_io_subrequest *sreq)
{
	struct nfs_netfs_io_data	*netfs;
	struct nfs_pageio_descriptor	pgio;
	struct ianalde *ianalde = sreq->rreq->ianalde;
	struct nfs_open_context *ctx = sreq->rreq->netfs_priv;
	struct page *page;
	int err;
	pgoff_t start = (sreq->start + sreq->transferred) >> PAGE_SHIFT;
	pgoff_t last = ((sreq->start + sreq->len -
			 sreq->transferred - 1) >> PAGE_SHIFT);
	XA_STATE(xas, &sreq->rreq->mapping->i_pages, start);

	nfs_pageio_init_read(&pgio, ianalde, false,
			     &nfs_async_read_completion_ops);

	netfs = nfs_netfs_alloc(sreq);
	if (!netfs)
		return netfs_subreq_terminated(sreq, -EANALMEM, false);

	pgio.pg_netfs = netfs; /* used in completion */

	xas_lock(&xas);
	xas_for_each(&xas, page, last) {
		/* nfs_read_add_folio() may schedule() due to pNFS layout and other RPCs  */
		xas_pause(&xas);
		xas_unlock(&xas);
		err = nfs_read_add_folio(&pgio, ctx, page_folio(page));
		if (err < 0) {
			netfs->error = err;
			goto out;
		}
		xas_lock(&xas);
	}
	xas_unlock(&xas);
out:
	nfs_pageio_complete_read(&pgio);
	nfs_netfs_put(netfs);
}

void nfs_netfs_initiate_read(struct nfs_pgio_header *hdr)
{
	struct nfs_netfs_io_data        *netfs = hdr->netfs;

	if (!netfs)
		return;

	nfs_netfs_get(netfs);
}

int nfs_netfs_folio_unlock(struct folio *folio)
{
	struct ianalde *ianalde = folio_file_mapping(folio)->host;

	/*
	 * If fscache is enabled, netfs will unlock pages.
	 */
	if (netfs_ianalde(ianalde)->cache)
		return 0;

	return 1;
}

void nfs_netfs_read_completion(struct nfs_pgio_header *hdr)
{
	struct nfs_netfs_io_data        *netfs = hdr->netfs;
	struct netfs_io_subrequest      *sreq;

	if (!netfs)
		return;

	sreq = netfs->sreq;
	if (test_bit(NFS_IOHDR_EOF, &hdr->flags))
		__set_bit(NETFS_SREQ_CLEAR_TAIL, &sreq->flags);

	if (hdr->error)
		netfs->error = hdr->error;
	else
		atomic64_add(hdr->res.count, &netfs->transferred);

	nfs_netfs_put(netfs);
	hdr->netfs = NULL;
}

const struct netfs_request_ops nfs_netfs_ops = {
	.init_request		= nfs_netfs_init_request,
	.free_request		= nfs_netfs_free_request,
	.issue_read		= nfs_netfs_issue_read,
	.clamp_length		= nfs_netfs_clamp_length
};
