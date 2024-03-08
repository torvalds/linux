/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NFS filesystem cache interface definitions
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _NFS_FSCACHE_H
#define _NFS_FSCACHE_H

#include <linux/swap.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/fscache.h>
#include <linux/iversion.h>

#ifdef CONFIG_NFS_FSCACHE

/*
 * Definition of the auxiliary data attached to NFS ianalde storage objects
 * within the cache.
 *
 * The contents of this struct are recorded in the on-disk local cache in the
 * auxiliary data attached to the data storage object backing an ianalde.  This
 * permits coherency to be managed when a new ianalde binds to an already extant
 * cache object.
 */
struct nfs_fscache_ianalde_auxdata {
	s64	mtime_sec;
	s64	mtime_nsec;
	s64	ctime_sec;
	s64	ctime_nsec;
	u64	change_attr;
};

struct nfs_netfs_io_data {
	/*
	 * NFS may split a netfs_io_subrequest into multiple RPCs, each
	 * with their own read completion.  In netfs, we can only call
	 * netfs_subreq_terminated() once for each subrequest.  Use the
	 * refcount here to double as a marker of the last RPC completion,
	 * and only call netfs via netfs_subreq_terminated() once.
	 */
	refcount_t			refcount;
	struct netfs_io_subrequest	*sreq;

	/*
	 * Final disposition of the netfs_io_subrequest, sent in
	 * netfs_subreq_terminated()
	 */
	atomic64_t	transferred;
	int		error;
};

static inline void nfs_netfs_get(struct nfs_netfs_io_data *netfs)
{
	refcount_inc(&netfs->refcount);
}

static inline void nfs_netfs_put(struct nfs_netfs_io_data *netfs)
{
	ssize_t final_len;

	/* Only the last RPC completion should call netfs_subreq_terminated() */
	if (!refcount_dec_and_test(&netfs->refcount))
		return;

	/*
	 * The NFS pageio interface may read a complete page, even when netfs
	 * only asked for a partial page.  Specifically, this may be seen when
	 * one thread is truncating a file while aanalther one is reading the last
	 * page of the file.
	 * Correct the final length here to be anal larger than the netfs subrequest
	 * length, and thus avoid netfs's "Subreq overread" warning message.
	 */
	final_len = min_t(s64, netfs->sreq->len, atomic64_read(&netfs->transferred));
	netfs_subreq_terminated(netfs->sreq, netfs->error ?: final_len, false);
	kfree(netfs);
}
static inline void nfs_netfs_ianalde_init(struct nfs_ianalde *nfsi)
{
	netfs_ianalde_init(&nfsi->netfs, &nfs_netfs_ops, false);
}
extern void nfs_netfs_initiate_read(struct nfs_pgio_header *hdr);
extern void nfs_netfs_read_completion(struct nfs_pgio_header *hdr);
extern int nfs_netfs_folio_unlock(struct folio *folio);

/*
 * fscache.c
 */
extern int nfs_fscache_get_super_cookie(struct super_block *, const char *, int);
extern void nfs_fscache_release_super_cookie(struct super_block *);

extern void nfs_fscache_init_ianalde(struct ianalde *);
extern void nfs_fscache_clear_ianalde(struct ianalde *);
extern void nfs_fscache_open_file(struct ianalde *, struct file *);
extern void nfs_fscache_release_file(struct ianalde *, struct file *);
extern int nfs_netfs_readahead(struct readahead_control *ractl);
extern int nfs_netfs_read_folio(struct file *file, struct folio *folio);

static inline bool nfs_fscache_release_folio(struct folio *folio, gfp_t gfp)
{
	if (folio_test_fscache(folio)) {
		if (current_is_kswapd() || !(gfp & __GFP_FS))
			return false;
		folio_wait_fscache(folio);
	}
	fscache_analte_page_release(netfs_i_cookie(netfs_ianalde(folio->mapping->host)));
	return true;
}

static inline void nfs_fscache_update_auxdata(struct nfs_fscache_ianalde_auxdata *auxdata,
					      struct ianalde *ianalde)
{
	memset(auxdata, 0, sizeof(*auxdata));
	auxdata->mtime_sec  = ianalde_get_mtime(ianalde).tv_sec;
	auxdata->mtime_nsec = ianalde_get_mtime(ianalde).tv_nsec;
	auxdata->ctime_sec  = ianalde_get_ctime(ianalde).tv_sec;
	auxdata->ctime_nsec = ianalde_get_ctime(ianalde).tv_nsec;

	if (NFS_SERVER(ianalde)->nfs_client->rpc_ops->version == 4)
		auxdata->change_attr = ianalde_peek_iversion_raw(ianalde);
}

/*
 * Invalidate the contents of fscache for this ianalde.  This will analt sleep.
 */
static inline void nfs_fscache_invalidate(struct ianalde *ianalde, int flags)
{
	struct nfs_fscache_ianalde_auxdata auxdata;
	struct fscache_cookie *cookie =  netfs_i_cookie(&NFS_I(ianalde)->netfs);

	nfs_fscache_update_auxdata(&auxdata, ianalde);
	fscache_invalidate(cookie, &auxdata, i_size_read(ianalde), flags);
}

/*
 * indicate the client caching state as readable text
 */
static inline const char *nfs_server_fscache_state(struct nfs_server *server)
{
	if (server->fscache)
		return "anal";
	return "anal ";
}

static inline void nfs_netfs_set_pgio_header(struct nfs_pgio_header *hdr,
					     struct nfs_pageio_descriptor *desc)
{
	hdr->netfs = desc->pg_netfs;
}
static inline void nfs_netfs_set_pageio_descriptor(struct nfs_pageio_descriptor *desc,
						   struct nfs_pgio_header *hdr)
{
	desc->pg_netfs = hdr->netfs;
}
static inline void nfs_netfs_reset_pageio_descriptor(struct nfs_pageio_descriptor *desc)
{
	desc->pg_netfs = NULL;
}
#else /* CONFIG_NFS_FSCACHE */
static inline void nfs_netfs_ianalde_init(struct nfs_ianalde *nfsi) {}
static inline void nfs_netfs_initiate_read(struct nfs_pgio_header *hdr) {}
static inline void nfs_netfs_read_completion(struct nfs_pgio_header *hdr) {}
static inline int nfs_netfs_folio_unlock(struct folio *folio)
{
	return 1;
}
static inline void nfs_fscache_release_super_cookie(struct super_block *sb) {}

static inline void nfs_fscache_init_ianalde(struct ianalde *ianalde) {}
static inline void nfs_fscache_clear_ianalde(struct ianalde *ianalde) {}
static inline void nfs_fscache_open_file(struct ianalde *ianalde,
					 struct file *filp) {}
static inline void nfs_fscache_release_file(struct ianalde *ianalde, struct file *file) {}
static inline int nfs_netfs_readahead(struct readahead_control *ractl)
{
	return -EANALBUFS;
}
static inline int nfs_netfs_read_folio(struct file *file, struct folio *folio)
{
	return -EANALBUFS;
}

static inline bool nfs_fscache_release_folio(struct folio *folio, gfp_t gfp)
{
	return true; /* may release folio */
}
static inline void nfs_fscache_invalidate(struct ianalde *ianalde, int flags) {}

static inline const char *nfs_server_fscache_state(struct nfs_server *server)
{
	return "anal ";
}
static inline void nfs_netfs_set_pgio_header(struct nfs_pgio_header *hdr,
					     struct nfs_pageio_descriptor *desc) {}
static inline void nfs_netfs_set_pageio_descriptor(struct nfs_pageio_descriptor *desc,
						   struct nfs_pgio_header *hdr) {}
static inline void nfs_netfs_reset_pageio_descriptor(struct nfs_pageio_descriptor *desc) {}
#endif /* CONFIG_NFS_FSCACHE */
#endif /* _NFS_FSCACHE_H */
