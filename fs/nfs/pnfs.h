/*
 *  pNFS client data structures.
 *
 *  Copyright (c) 2002
 *  The Regents of the University of Michigan
 *  All Rights Reserved
 *
 *  Dean Hildebrand <dhildebz@umich.edu>
 *
 *  Permission is granted to use, copy, create derivative works, and
 *  redistribute this software and such derivative works for any purpose,
 *  so long as the name of the University of Michigan is yest used in
 *  any advertising or publicity pertaining to the use or distribution
 *  of this software without specific, written prior authorization. If
 *  the above copyright yestice or any other identification of the
 *  University of Michigan is included in any copy of any portion of
 *  this software, then the disclaimer below must also be included.
 *
 *  This software is provided as is, without representation or warranty
 *  of any kind either express or implied, including without limitation
 *  the implied warranties of merchantability, fitness for a particular
 *  purpose, or yesninfringement.  The Regents of the University of
 *  Michigan shall yest be liable for any damages, including special,
 *  indirect, incidental, or consequential damages, with respect to any
 *  claim arising out of or in connection with the use of the software,
 *  even if it has been or is hereafter advised of the possibility of
 *  such damages.
 */

#ifndef FS_NFS_PNFS_H
#define FS_NFS_PNFS_H

#include <linux/refcount.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/workqueue.h>

struct nfs4_opendata;

enum {
	NFS_LSEG_VALID = 0,	/* cleared when lseg is recalled/returned */
	NFS_LSEG_ROC,		/* roc bit received from server */
	NFS_LSEG_LAYOUTCOMMIT,	/* layoutcommit bit set for layoutcommit */
	NFS_LSEG_LAYOUTRETURN,	/* layoutreturn bit set for layoutreturn */
	NFS_LSEG_UNAVAILABLE,	/* unavailable bit set for temporary problem */
};

/* Individual ip address */
struct nfs4_pnfs_ds_addr {
	struct sockaddr_storage	da_addr;
	size_t			da_addrlen;
	struct list_head	da_yesde;  /* nfs4_pnfs_dev_hlist dev_dslist */
	char			*da_remotestr;	/* human readable addr+port */
};

struct nfs4_pnfs_ds {
	struct list_head	ds_yesde;  /* nfs4_pnfs_dev_hlist dev_dslist */
	char			*ds_remotestr;	/* comma sep list of addrs */
	struct list_head	ds_addrs;
	struct nfs_client	*ds_clp;
	refcount_t		ds_count;
	unsigned long		ds_state;
#define NFS4DS_CONNECTING	0	/* ds is establishing connection */
};

struct pnfs_layout_segment {
	struct list_head pls_list;
	struct list_head pls_lc_list;
	struct pnfs_layout_range pls_range;
	refcount_t pls_refcount;
	u32 pls_seq;
	unsigned long pls_flags;
	struct pnfs_layout_hdr *pls_layout;
};

enum pnfs_try_status {
	PNFS_ATTEMPTED     = 0,
	PNFS_NOT_ATTEMPTED = 1,
	PNFS_TRY_AGAIN     = 2,
};

#ifdef CONFIG_NFS_V4_1

#define LAYOUT_NFSV4_1_MODULE_PREFIX "nfs-layouttype4"

/*
 * Default data server connection timeout and retrans vaules.
 * Set by module parameters dataserver_timeo and dataserver_retrans.
 */
#define NFS4_DEF_DS_TIMEO   600 /* in tenths of a second */
#define NFS4_DEF_DS_RETRANS 5
#define PNFS_DEVICE_RETRY_TIMEOUT (120*HZ)

/* error codes for internal use */
#define NFS4ERR_RESET_TO_MDS   12001
#define NFS4ERR_RESET_TO_PNFS  12002

enum {
	NFS_LAYOUT_RO_FAILED = 0,	/* get ro layout failed stop trying */
	NFS_LAYOUT_RW_FAILED,		/* get rw layout failed stop trying */
	NFS_LAYOUT_BULK_RECALL,		/* bulk recall affecting layout */
	NFS_LAYOUT_RETURN,		/* layoutreturn in progress */
	NFS_LAYOUT_RETURN_LOCK,		/* Serialise layoutreturn */
	NFS_LAYOUT_RETURN_REQUESTED,	/* Return this layout ASAP */
	NFS_LAYOUT_INVALID_STID,	/* layout stateid id is invalid */
	NFS_LAYOUT_FIRST_LAYOUTGET,	/* Serialize first layoutget */
	NFS_LAYOUT_INODE_FREEING,	/* The iyesde is being freed */
};

enum layoutdriver_policy_flags {
	/* Should the pNFS client commit and return the layout upon truncate to
	 * a smaller size */
	PNFS_LAYOUTRET_ON_SETATTR	= 1 << 0,
	PNFS_LAYOUTRET_ON_ERROR		= 1 << 1,
	PNFS_READ_WHOLE_PAGE		= 1 << 2,
	PNFS_LAYOUTGET_ON_OPEN		= 1 << 3,
};

struct nfs4_deviceid_yesde;

/* Per-layout driver specific registration structure */
struct pnfs_layoutdriver_type {
	struct list_head pnfs_tblid;
	const u32 id;
	const char *name;
	struct module *owner;
	unsigned flags;
	unsigned max_deviceinfo_size;
	unsigned max_layoutget_response;

	int (*set_layoutdriver) (struct nfs_server *, const struct nfs_fh *);
	int (*clear_layoutdriver) (struct nfs_server *);

	struct pnfs_layout_hdr * (*alloc_layout_hdr) (struct iyesde *iyesde, gfp_t gfp_flags);
	void (*free_layout_hdr) (struct pnfs_layout_hdr *);

	struct pnfs_layout_segment * (*alloc_lseg) (struct pnfs_layout_hdr *layoutid, struct nfs4_layoutget_res *lgr, gfp_t gfp_flags);
	void (*free_lseg) (struct pnfs_layout_segment *lseg);
	void (*add_lseg) (struct pnfs_layout_hdr *layoutid,
			struct pnfs_layout_segment *lseg,
			struct list_head *free_me);

	void (*return_range) (struct pnfs_layout_hdr *lo,
			      struct pnfs_layout_range *range);

	/* test for nfs page cache coalescing */
	const struct nfs_pageio_ops *pg_read_ops;
	const struct nfs_pageio_ops *pg_write_ops;

	struct pnfs_ds_commit_info *(*get_ds_info) (struct iyesde *iyesde);
	void (*mark_request_commit) (struct nfs_page *req,
				     struct pnfs_layout_segment *lseg,
				     struct nfs_commit_info *cinfo,
				     u32 ds_commit_idx);
	void (*clear_request_commit) (struct nfs_page *req,
				      struct nfs_commit_info *cinfo);
	int (*scan_commit_lists) (struct nfs_commit_info *cinfo,
				  int max);
	void (*recover_commit_reqs) (struct list_head *list,
				     struct nfs_commit_info *cinfo);
	struct nfs_page * (*search_commit_reqs)(struct nfs_commit_info *cinfo,
						struct page *page);
	int (*commit_pagelist)(struct iyesde *iyesde,
			       struct list_head *mds_pages,
			       int how,
			       struct nfs_commit_info *cinfo);

	int (*sync)(struct iyesde *iyesde, bool datasync);

	/*
	 * Return PNFS_ATTEMPTED to indicate the layout code has attempted
	 * I/O, else return PNFS_NOT_ATTEMPTED to fall back to yesrmal NFS
	 */
	enum pnfs_try_status (*read_pagelist)(struct nfs_pgio_header *);
	enum pnfs_try_status (*write_pagelist)(struct nfs_pgio_header *, int);

	void (*free_deviceid_yesde) (struct nfs4_deviceid_yesde *);
	struct nfs4_deviceid_yesde * (*alloc_deviceid_yesde)
			(struct nfs_server *server, struct pnfs_device *pdev,
			gfp_t gfp_flags);

	int (*prepare_layoutreturn) (struct nfs4_layoutreturn_args *);

	void (*cleanup_layoutcommit) (struct nfs4_layoutcommit_data *data);
	int (*prepare_layoutcommit) (struct nfs4_layoutcommit_args *args);
	int (*prepare_layoutstats) (struct nfs42_layoutstat_args *args);
};

struct pnfs_layout_hdr {
	refcount_t		plh_refcount;
	atomic_t		plh_outstanding; /* number of RPCs out */
	struct list_head	plh_layouts;   /* other client layouts */
	struct list_head	plh_bulk_destroy;
	struct list_head	plh_segs;      /* layout segments list */
	struct list_head	plh_return_segs; /* invalid layout segments */
	unsigned long		plh_block_lgets; /* block LAYOUTGET if >0 */
	unsigned long		plh_retry_timestamp;
	unsigned long		plh_flags;
	nfs4_stateid		plh_stateid;
	u32			plh_barrier; /* igyesre lower seqids */
	u32			plh_return_seq;
	enum pnfs_iomode	plh_return_iomode;
	loff_t			plh_lwb; /* last write byte for layoutcommit */
	const struct cred	*plh_lc_cred; /* layoutcommit cred */
	struct iyesde		*plh_iyesde;
};

struct pnfs_device {
	struct nfs4_deviceid dev_id;
	unsigned int  layout_type;
	unsigned int  mincount;
	unsigned int  maxcount;	/* gdia_maxcount */
	struct page **pages;
	unsigned int  pgbase;
	unsigned int  pglen;	/* reply buffer length */
	unsigned char yescache : 1;/* May yest be cached */
};

#define NFS4_PNFS_GETDEVLIST_MAXNUM 16

struct pnfs_devicelist {
	unsigned int		eof;
	unsigned int		num_devs;
	struct nfs4_deviceid	dev_id[NFS4_PNFS_GETDEVLIST_MAXNUM];
};

extern int pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *);
extern void pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *);

/* nfs4proc.c */
extern size_t max_response_pages(struct nfs_server *server);
extern int nfs4_proc_getdeviceinfo(struct nfs_server *server,
				   struct pnfs_device *dev,
				   const struct cred *cred);
extern struct pnfs_layout_segment* nfs4_proc_layoutget(struct nfs4_layoutget *lgp, long *timeout);
extern int nfs4_proc_layoutreturn(struct nfs4_layoutreturn *lrp, bool sync);

/* pnfs.c */
void pnfs_get_layout_hdr(struct pnfs_layout_hdr *lo);
void pnfs_put_lseg(struct pnfs_layout_segment *lseg);

void set_pnfs_layoutdriver(struct nfs_server *, const struct nfs_fh *, struct nfs_fsinfo *);
void unset_pnfs_layoutdriver(struct nfs_server *);
void pnfs_generic_pg_check_layout(struct nfs_pageio_descriptor *pgio);
void pnfs_generic_pg_init_read(struct nfs_pageio_descriptor *, struct nfs_page *);
int pnfs_generic_pg_readpages(struct nfs_pageio_descriptor *desc);
void pnfs_generic_pg_init_write(struct nfs_pageio_descriptor *pgio,
			        struct nfs_page *req, u64 wb_size);
void pnfs_generic_pg_cleanup(struct nfs_pageio_descriptor *);
int pnfs_generic_pg_writepages(struct nfs_pageio_descriptor *desc);
size_t pnfs_generic_pg_test(struct nfs_pageio_descriptor *pgio,
			    struct nfs_page *prev, struct nfs_page *req);
void pnfs_set_lo_fail(struct pnfs_layout_segment *lseg);
struct pnfs_layout_segment *pnfs_layout_process(struct nfs4_layoutget *lgp);
void pnfs_layoutget_free(struct nfs4_layoutget *lgp);
void pnfs_free_lseg_list(struct list_head *tmp_list);
void pnfs_destroy_layout(struct nfs_iyesde *);
void pnfs_destroy_all_layouts(struct nfs_client *);
int pnfs_destroy_layouts_byfsid(struct nfs_client *clp,
		struct nfs_fsid *fsid,
		bool is_recall);
int pnfs_destroy_layouts_byclid(struct nfs_client *clp,
		bool is_recall);
bool nfs4_layout_refresh_old_stateid(nfs4_stateid *dst,
		struct pnfs_layout_range *dst_range,
		struct iyesde *iyesde);
void pnfs_put_layout_hdr(struct pnfs_layout_hdr *lo);
void pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo,
			     const nfs4_stateid *new,
			     bool update_barrier);
int pnfs_mark_matching_lsegs_invalid(struct pnfs_layout_hdr *lo,
				struct list_head *tmp_list,
				const struct pnfs_layout_range *recall_range,
				u32 seq);
int pnfs_mark_matching_lsegs_return(struct pnfs_layout_hdr *lo,
				struct list_head *tmp_list,
				const struct pnfs_layout_range *recall_range,
				u32 seq);
int pnfs_mark_layout_stateid_invalid(struct pnfs_layout_hdr *lo,
		struct list_head *lseg_list);
bool pnfs_roc(struct iyesde *iyes,
		struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		const struct cred *cred);
int pnfs_roc_done(struct rpc_task *task, struct iyesde *iyesde,
		struct nfs4_layoutreturn_args **argpp,
		struct nfs4_layoutreturn_res **respp,
		int *ret);
void pnfs_roc_release(struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		int ret);
bool pnfs_wait_on_layoutreturn(struct iyesde *iyes, struct rpc_task *task);
void pnfs_set_layoutcommit(struct iyesde *, struct pnfs_layout_segment *, loff_t);
void pnfs_cleanup_layoutcommit(struct nfs4_layoutcommit_data *data);
int pnfs_layoutcommit_iyesde(struct iyesde *iyesde, bool sync);
int pnfs_generic_sync(struct iyesde *iyesde, bool datasync);
int pnfs_nfs_generic_sync(struct iyesde *iyesde, bool datasync);
int _pnfs_return_layout(struct iyesde *);
int pnfs_commit_and_return_layout(struct iyesde *);
void pnfs_ld_write_done(struct nfs_pgio_header *);
void pnfs_ld_read_done(struct nfs_pgio_header *);
void pnfs_read_resend_pnfs(struct nfs_pgio_header *);
struct pnfs_layout_segment *pnfs_update_layout(struct iyesde *iyes,
					       struct nfs_open_context *ctx,
					       loff_t pos,
					       u64 count,
					       enum pnfs_iomode iomode,
					       bool strict_iomode,
					       gfp_t gfp_flags);
void pnfs_layoutreturn_free_lsegs(struct pnfs_layout_hdr *lo,
		const nfs4_stateid *arg_stateid,
		const struct pnfs_layout_range *range,
		const nfs4_stateid *stateid);

void pnfs_generic_layout_insert_lseg(struct pnfs_layout_hdr *lo,
		   struct pnfs_layout_segment *lseg,
		   bool (*is_after)(const struct pnfs_layout_range *lseg_range,
			   const struct pnfs_layout_range *old),
		   bool (*do_merge)(struct pnfs_layout_segment *lseg,
			   struct pnfs_layout_segment *old),
		   struct list_head *free_me);

void nfs4_deviceid_mark_client_invalid(struct nfs_client *clp);
int pnfs_read_done_resend_to_mds(struct nfs_pgio_header *);
int pnfs_write_done_resend_to_mds(struct nfs_pgio_header *);
struct nfs4_threshold *pnfs_mdsthreshold_alloc(void);
void pnfs_error_mark_layout_for_return(struct iyesde *iyesde,
				       struct pnfs_layout_segment *lseg);
/* nfs4_deviceid_flags */
enum {
	NFS_DEVICEID_INVALID = 0,       /* set when MDS clientid recalled */
	NFS_DEVICEID_UNAVAILABLE,	/* device temporarily unavailable */
	NFS_DEVICEID_NOCACHE,		/* device may yest be cached */
};

/* pnfs_dev.c */
struct nfs4_deviceid_yesde {
	struct hlist_yesde		yesde;
	struct hlist_yesde		tmpyesde;
	const struct pnfs_layoutdriver_type *ld;
	const struct nfs_client		*nfs_client;
	unsigned long 			flags;
	unsigned long			timestamp_unavailable;
	struct nfs4_deviceid		deviceid;
	struct rcu_head			rcu;
	atomic_t			ref;
};

struct nfs4_deviceid_yesde *
nfs4_find_get_deviceid(struct nfs_server *server,
		const struct nfs4_deviceid *id, const struct cred *cred,
		gfp_t gfp_mask);
void nfs4_delete_deviceid(const struct pnfs_layoutdriver_type *, const struct nfs_client *, const struct nfs4_deviceid *);
void nfs4_init_deviceid_yesde(struct nfs4_deviceid_yesde *, struct nfs_server *,
			     const struct nfs4_deviceid *);
bool nfs4_put_deviceid_yesde(struct nfs4_deviceid_yesde *);
void nfs4_mark_deviceid_available(struct nfs4_deviceid_yesde *yesde);
void nfs4_mark_deviceid_unavailable(struct nfs4_deviceid_yesde *yesde);
bool nfs4_test_deviceid_unavailable(struct nfs4_deviceid_yesde *yesde);
void nfs4_deviceid_purge_client(const struct nfs_client *);

/* pnfs_nfs.c */
void pnfs_generic_clear_request_commit(struct nfs_page *req,
				       struct nfs_commit_info *cinfo);
void pnfs_generic_commit_release(void *calldata);
void pnfs_generic_prepare_to_resend_writes(struct nfs_commit_data *data);
void pnfs_generic_rw_release(void *data);
void pnfs_generic_recover_commit_reqs(struct list_head *dst,
				      struct nfs_commit_info *cinfo);
int pnfs_generic_commit_pagelist(struct iyesde *iyesde,
				 struct list_head *mds_pages,
				 int how,
				 struct nfs_commit_info *cinfo,
				 int (*initiate_commit)(struct nfs_commit_data *data,
							int how));
int pnfs_generic_scan_commit_lists(struct nfs_commit_info *cinfo, int max);
void pnfs_generic_write_commit_done(struct rpc_task *task, void *data);
void nfs4_pnfs_ds_put(struct nfs4_pnfs_ds *ds);
struct nfs4_pnfs_ds *nfs4_pnfs_ds_add(struct list_head *dsaddrs,
				      gfp_t gfp_flags);
void nfs4_pnfs_v3_ds_connect_unload(void);
int nfs4_pnfs_ds_connect(struct nfs_server *mds_srv, struct nfs4_pnfs_ds *ds,
			  struct nfs4_deviceid_yesde *devid, unsigned int timeo,
			  unsigned int retrans, u32 version, u32 miyesr_version);
struct nfs4_pnfs_ds_addr *nfs4_decode_mp_ds_addr(struct net *net,
						 struct xdr_stream *xdr,
						 gfp_t gfp_flags);
void pnfs_layout_mark_request_commit(struct nfs_page *req,
				     struct pnfs_layout_segment *lseg,
				     struct nfs_commit_info *cinfo,
				     u32 ds_commit_idx);
void pnfs_lgopen_prepare(struct nfs4_opendata *data,
			 struct nfs_open_context *ctx);
void pnfs_parse_lgopen(struct iyesde *iyes, struct nfs4_layoutget *lgp,
		       struct nfs_open_context *ctx);
void nfs4_lgopen_release(struct nfs4_layoutget *lgp);

static inline bool nfs_have_layout(struct iyesde *iyesde)
{
	return NFS_I(iyesde)->layout != NULL;
}

static inline bool pnfs_layout_is_valid(const struct pnfs_layout_hdr *lo)
{
	return test_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags) == 0;
}

static inline struct nfs4_deviceid_yesde *
nfs4_get_deviceid(struct nfs4_deviceid_yesde *d)
{
	atomic_inc(&d->ref);
	return d;
}

static inline struct pnfs_layout_segment *
pnfs_get_lseg(struct pnfs_layout_segment *lseg)
{
	if (lseg) {
		refcount_inc(&lseg->pls_refcount);
		smp_mb__after_atomic();
	}
	return lseg;
}

static inline bool
pnfs_is_valid_lseg(struct pnfs_layout_segment *lseg)
{
	return test_bit(NFS_LSEG_VALID, &lseg->pls_flags) != 0;
}

/* Return true if a layout driver is being used for this mountpoint */
static inline int pnfs_enabled_sb(struct nfs_server *nfss)
{
	return nfss->pnfs_curr_ld != NULL;
}

static inline int
pnfs_commit_list(struct iyesde *iyesde, struct list_head *mds_pages, int how,
		 struct nfs_commit_info *cinfo)
{
	if (cinfo->ds == NULL || cinfo->ds->ncommitting == 0)
		return PNFS_NOT_ATTEMPTED;
	return NFS_SERVER(iyesde)->pnfs_curr_ld->commit_pagelist(iyesde, mds_pages, how, cinfo);
}

static inline struct pnfs_ds_commit_info *
pnfs_get_ds_info(struct iyesde *iyesde)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(iyesde)->pnfs_curr_ld;

	if (ld == NULL || ld->get_ds_info == NULL)
		return NULL;
	return ld->get_ds_info(iyesde);
}

static inline void
pnfs_generic_mark_devid_invalid(struct nfs4_deviceid_yesde *yesde)
{
	set_bit(NFS_DEVICEID_INVALID, &yesde->flags);
}

static inline bool
pnfs_mark_request_commit(struct nfs_page *req, struct pnfs_layout_segment *lseg,
			 struct nfs_commit_info *cinfo, u32 ds_commit_idx)
{
	struct iyesde *iyesde = d_iyesde(nfs_req_openctx(req)->dentry);
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(iyesde)->pnfs_curr_ld;

	if (lseg == NULL || ld->mark_request_commit == NULL)
		return false;
	ld->mark_request_commit(req, lseg, cinfo, ds_commit_idx);
	return true;
}

static inline bool
pnfs_clear_request_commit(struct nfs_page *req, struct nfs_commit_info *cinfo)
{
	struct iyesde *iyesde = d_iyesde(nfs_req_openctx(req)->dentry);
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(iyesde)->pnfs_curr_ld;

	if (ld == NULL || ld->clear_request_commit == NULL)
		return false;
	ld->clear_request_commit(req, cinfo);
	return true;
}

static inline int
pnfs_scan_commit_lists(struct iyesde *iyesde, struct nfs_commit_info *cinfo,
		       int max)
{
	if (cinfo->ds == NULL || cinfo->ds->nwritten == 0)
		return 0;
	else
		return NFS_SERVER(iyesde)->pnfs_curr_ld->scan_commit_lists(cinfo, max);
}

static inline struct nfs_page *
pnfs_search_commit_reqs(struct iyesde *iyesde, struct nfs_commit_info *cinfo,
			struct page *page)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(iyesde)->pnfs_curr_ld;

	if (ld == NULL || ld->search_commit_reqs == NULL)
		return NULL;
	return ld->search_commit_reqs(cinfo, page);
}

/* Should the pNFS client commit and return the layout upon a setattr */
static inline bool
pnfs_ld_layoutret_on_setattr(struct iyesde *iyesde)
{
	if (!pnfs_enabled_sb(NFS_SERVER(iyesde)))
		return false;
	return NFS_SERVER(iyesde)->pnfs_curr_ld->flags &
		PNFS_LAYOUTRET_ON_SETATTR;
}

static inline bool
pnfs_ld_read_whole_page(struct iyesde *iyesde)
{
	if (!pnfs_enabled_sb(NFS_SERVER(iyesde)))
		return false;
	return NFS_SERVER(iyesde)->pnfs_curr_ld->flags & PNFS_READ_WHOLE_PAGE;
}

static inline int
pnfs_sync_iyesde(struct iyesde *iyesde, bool datasync)
{
	if (!pnfs_enabled_sb(NFS_SERVER(iyesde)))
		return 0;
	return NFS_SERVER(iyesde)->pnfs_curr_ld->sync(iyesde, datasync);
}

static inline bool
pnfs_layoutcommit_outstanding(struct iyesde *iyesde)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);

	return test_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags) != 0 ||
		test_bit(NFS_INO_LAYOUTCOMMITTING, &nfsi->flags) != 0;
}

static inline int pnfs_return_layout(struct iyesde *iyes)
{
	struct nfs_iyesde *nfsi = NFS_I(iyes);
	struct nfs_server *nfss = NFS_SERVER(iyes);

	if (pnfs_enabled_sb(nfss) && nfsi->layout) {
		set_bit(NFS_LAYOUT_RETURN_REQUESTED, &nfsi->layout->plh_flags);
		return _pnfs_return_layout(iyes);
	}

	return 0;
}

static inline bool
pnfs_use_threshold(struct nfs4_threshold **dst, struct nfs4_threshold *src,
		   struct nfs_server *nfss)
{
	return (dst && src && src->bm != 0 && nfss->pnfs_curr_ld &&
					nfss->pnfs_curr_ld->id == src->l_type);
}

static inline u64
pnfs_calc_offset_end(u64 offset, u64 len)
{
	if (len == NFS4_MAX_UINT64 || len >= NFS4_MAX_UINT64 - offset)
		return NFS4_MAX_UINT64;
	return offset + len - 1;
}

static inline u64
pnfs_calc_offset_length(u64 offset, u64 end)
{
	if (end == NFS4_MAX_UINT64 || end <= offset)
		return NFS4_MAX_UINT64;
	return 1 + end - offset;
}

static inline void
pnfs_copy_range(struct pnfs_layout_range *dst,
		const struct pnfs_layout_range *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static inline u64
pnfs_end_offset(u64 start, u64 len)
{
	if (NFS4_MAX_UINT64 - start <= len)
		return NFS4_MAX_UINT64;
	return start + len;
}

/*
 * Are 2 ranges intersecting?
 *   start1                             end1
 *   [----------------------------------)
 *                                start2           end2
 *                                [----------------)
 */
static inline bool
pnfs_is_range_intersecting(u64 start1, u64 end1, u64 start2, u64 end2)
{
	return (end1 == NFS4_MAX_UINT64 || start2 < end1) &&
		(end2 == NFS4_MAX_UINT64 || start1 < end2);
}

static inline bool
pnfs_lseg_range_intersecting(const struct pnfs_layout_range *l1,
		const struct pnfs_layout_range *l2)
{
	u64 end1 = pnfs_end_offset(l1->offset, l1->length);
	u64 end2 = pnfs_end_offset(l2->offset, l2->length);

	return pnfs_is_range_intersecting(l1->offset, end1, l2->offset, end2);
}

static inline bool
pnfs_lseg_request_intersecting(struct pnfs_layout_segment *lseg, struct nfs_page *req)
{
	u64 seg_last = pnfs_end_offset(lseg->pls_range.offset, lseg->pls_range.length);
	u64 req_last = req_offset(req) + req->wb_bytes;

	return pnfs_is_range_intersecting(lseg->pls_range.offset, seg_last,
				req_offset(req), req_last);
}

extern unsigned int layoutstats_timer;

#ifdef NFS_DEBUG
void nfs4_print_deviceid(const struct nfs4_deviceid *dev_id);
#else
static inline void nfs4_print_deviceid(const struct nfs4_deviceid *dev_id)
{
}

#endif /* NFS_DEBUG */
#else  /* CONFIG_NFS_V4_1 */

static inline bool nfs_have_layout(struct iyesde *iyesde)
{
	return false;
}

static inline void pnfs_destroy_all_layouts(struct nfs_client *clp)
{
}

static inline void pnfs_destroy_layout(struct nfs_iyesde *nfsi)
{
}

static inline struct pnfs_layout_segment *
pnfs_get_lseg(struct pnfs_layout_segment *lseg)
{
	return NULL;
}

static inline void pnfs_put_lseg(struct pnfs_layout_segment *lseg)
{
}

static inline int pnfs_return_layout(struct iyesde *iyes)
{
	return 0;
}

static inline int pnfs_commit_and_return_layout(struct iyesde *iyesde)
{
	return 0;
}

static inline bool
pnfs_ld_layoutret_on_setattr(struct iyesde *iyesde)
{
	return false;
}

static inline bool
pnfs_ld_read_whole_page(struct iyesde *iyesde)
{
	return false;
}

static inline int
pnfs_sync_iyesde(struct iyesde *iyesde, bool datasync)
{
	return 0;
}

static inline bool
pnfs_layoutcommit_outstanding(struct iyesde *iyesde)
{
	return false;
}


static inline bool
pnfs_roc(struct iyesde *iyes,
		struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		const struct cred *cred)
{
	return false;
}

static inline int
pnfs_roc_done(struct rpc_task *task, struct iyesde *iyesde,
		struct nfs4_layoutreturn_args **argpp,
		struct nfs4_layoutreturn_res **respp,
		int *ret)
{
	return 0;
}

static inline void
pnfs_roc_release(struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		int ret)
{
}

static inline bool
pnfs_wait_on_layoutreturn(struct iyesde *iyes, struct rpc_task *task)
{
	return false;
}

static inline void set_pnfs_layoutdriver(struct nfs_server *s,
					 const struct nfs_fh *mntfh,
					 struct nfs_fsinfo *fsinfo)
{
}

static inline void unset_pnfs_layoutdriver(struct nfs_server *s)
{
}

static inline int
pnfs_commit_list(struct iyesde *iyesde, struct list_head *mds_pages, int how,
		 struct nfs_commit_info *cinfo)
{
	return PNFS_NOT_ATTEMPTED;
}

static inline struct pnfs_ds_commit_info *
pnfs_get_ds_info(struct iyesde *iyesde)
{
	return NULL;
}

static inline bool
pnfs_mark_request_commit(struct nfs_page *req, struct pnfs_layout_segment *lseg,
			 struct nfs_commit_info *cinfo, u32 ds_commit_idx)
{
	return false;
}

static inline bool
pnfs_clear_request_commit(struct nfs_page *req, struct nfs_commit_info *cinfo)
{
	return false;
}

static inline int
pnfs_scan_commit_lists(struct iyesde *iyesde, struct nfs_commit_info *cinfo,
		       int max)
{
	return 0;
}

static inline struct nfs_page *
pnfs_search_commit_reqs(struct iyesde *iyesde, struct nfs_commit_info *cinfo,
			struct page *page)
{
	return NULL;
}

static inline int pnfs_layoutcommit_iyesde(struct iyesde *iyesde, bool sync)
{
	return 0;
}

static inline bool
pnfs_use_threshold(struct nfs4_threshold **dst, struct nfs4_threshold *src,
		   struct nfs_server *nfss)
{
	return false;
}

static inline struct nfs4_threshold *pnfs_mdsthreshold_alloc(void)
{
	return NULL;
}

static inline void nfs4_pnfs_v3_ds_connect_unload(void)
{
}

static inline bool nfs4_layout_refresh_old_stateid(nfs4_stateid *dst,
		struct pnfs_layout_range *dst_range,
		struct iyesde *iyesde)
{
	return false;
}

static inline void pnfs_lgopen_prepare(struct nfs4_opendata *data,
		struct nfs_open_context *ctx)
{
}

static inline void pnfs_parse_lgopen(struct iyesde *iyes,
		struct nfs4_layoutget *lgp,
		struct nfs_open_context *ctx)
{
}

static inline void nfs4_lgopen_release(struct nfs4_layoutget *lgp)
{
}

static inline bool pnfs_layout_is_valid(const struct pnfs_layout_hdr *lo)
{
	return false;
}

#endif /* CONFIG_NFS_V4_1 */

#if IS_ENABLED(CONFIG_NFS_V4_2)
int pnfs_report_layoutstat(struct iyesde *iyesde, gfp_t gfp_flags);
#else
static inline int
pnfs_report_layoutstat(struct iyesde *iyesde, gfp_t gfp_flags)
{
	return 0;
}
#endif

#endif /* FS_NFS_PNFS_H */
