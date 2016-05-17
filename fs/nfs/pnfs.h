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
 *  so long as the name of the University of Michigan is not used in
 *  any advertising or publicity pertaining to the use or distribution
 *  of this software without specific, written prior authorization. If
 *  the above copyright notice or any other identification of the
 *  University of Michigan is included in any copy of any portion of
 *  this software, then the disclaimer below must also be included.
 *
 *  This software is provided as is, without representation or warranty
 *  of any kind either express or implied, including without limitation
 *  the implied warranties of merchantability, fitness for a particular
 *  purpose, or noninfringement.  The Regents of the University of
 *  Michigan shall not be liable for any damages, including special,
 *  indirect, incidental, or consequential damages, with respect to any
 *  claim arising out of or in connection with the use of the software,
 *  even if it has been or is hereafter advised of the possibility of
 *  such damages.
 */

#ifndef FS_NFS_PNFS_H
#define FS_NFS_PNFS_H

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/workqueue.h>

enum {
	NFS_LSEG_VALID = 0,	/* cleared when lseg is recalled/returned */
	NFS_LSEG_ROC,		/* roc bit received from server */
	NFS_LSEG_LAYOUTCOMMIT,	/* layoutcommit bit set for layoutcommit */
	NFS_LSEG_LAYOUTRETURN,	/* layoutreturn bit set for layoutreturn */
};

/* Individual ip address */
struct nfs4_pnfs_ds_addr {
	struct sockaddr_storage	da_addr;
	size_t			da_addrlen;
	struct list_head	da_node;  /* nfs4_pnfs_dev_hlist dev_dslist */
	char			*da_remotestr;	/* human readable addr+port */
};

struct nfs4_pnfs_ds {
	struct list_head	ds_node;  /* nfs4_pnfs_dev_hlist dev_dslist */
	char			*ds_remotestr;	/* comma sep list of addrs */
	struct list_head	ds_addrs;
	struct nfs_client	*ds_clp;
	atomic_t		ds_count;
	unsigned long		ds_state;
#define NFS4DS_CONNECTING	0	/* ds is establishing connection */
};

struct pnfs_layout_segment {
	struct list_head pls_list;
	struct list_head pls_lc_list;
	struct pnfs_layout_range pls_range;
	atomic_t pls_refcount;
	u32 pls_seq;
	unsigned long pls_flags;
	struct pnfs_layout_hdr *pls_layout;
	struct work_struct pls_work;
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

/* error codes for internal use */
#define NFS4ERR_RESET_TO_MDS   12001
#define NFS4ERR_RESET_TO_PNFS  12002

enum {
	NFS_LAYOUT_RO_FAILED = 0,	/* get ro layout failed stop trying */
	NFS_LAYOUT_RW_FAILED,		/* get rw layout failed stop trying */
	NFS_LAYOUT_BULK_RECALL,		/* bulk recall affecting layout */
	NFS_LAYOUT_RETURN,		/* layoutreturn in progress */
	NFS_LAYOUT_RETURN_REQUESTED,	/* Return this layout ASAP */
	NFS_LAYOUT_INVALID_STID,	/* layout stateid id is invalid */
	NFS_LAYOUT_FIRST_LAYOUTGET,	/* Serialize first layoutget */
};

enum layoutdriver_policy_flags {
	/* Should the pNFS client commit and return the layout upon truncate to
	 * a smaller size */
	PNFS_LAYOUTRET_ON_SETATTR	= 1 << 0,
	PNFS_LAYOUTRET_ON_ERROR		= 1 << 1,
	PNFS_READ_WHOLE_PAGE		= 1 << 2,
};

struct nfs4_deviceid_node;

/* Per-layout driver specific registration structure */
struct pnfs_layoutdriver_type {
	struct list_head pnfs_tblid;
	const u32 id;
	const char *name;
	struct module *owner;
	unsigned flags;
	unsigned max_deviceinfo_size;

	int (*set_layoutdriver) (struct nfs_server *, const struct nfs_fh *);
	int (*clear_layoutdriver) (struct nfs_server *);

	struct pnfs_layout_hdr * (*alloc_layout_hdr) (struct inode *inode, gfp_t gfp_flags);
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

	struct pnfs_ds_commit_info *(*get_ds_info) (struct inode *inode);
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
	int (*commit_pagelist)(struct inode *inode,
			       struct list_head *mds_pages,
			       int how,
			       struct nfs_commit_info *cinfo);

	int (*sync)(struct inode *inode, bool datasync);

	/*
	 * Return PNFS_ATTEMPTED to indicate the layout code has attempted
	 * I/O, else return PNFS_NOT_ATTEMPTED to fall back to normal NFS
	 */
	enum pnfs_try_status (*read_pagelist)(struct nfs_pgio_header *);
	enum pnfs_try_status (*write_pagelist)(struct nfs_pgio_header *, int);

	void (*free_deviceid_node) (struct nfs4_deviceid_node *);
	struct nfs4_deviceid_node * (*alloc_deviceid_node)
			(struct nfs_server *server, struct pnfs_device *pdev,
			gfp_t gfp_flags);

	void (*encode_layoutreturn) (struct pnfs_layout_hdr *layoutid,
				     struct xdr_stream *xdr,
				     const struct nfs4_layoutreturn_args *args);

	void (*cleanup_layoutcommit) (struct nfs4_layoutcommit_data *data);
	int (*prepare_layoutcommit) (struct nfs4_layoutcommit_args *args);
	void (*encode_layoutcommit) (struct pnfs_layout_hdr *lo,
				     struct xdr_stream *xdr,
				     const struct nfs4_layoutcommit_args *args);
	int (*prepare_layoutstats) (struct nfs42_layoutstat_args *args);
	void (*cleanup_layoutstats) (struct nfs42_layoutstat_data *data);
};

struct pnfs_layout_hdr {
	atomic_t		plh_refcount;
	atomic_t		plh_outstanding; /* number of RPCs out */
	struct list_head	plh_layouts;   /* other client layouts */
	struct list_head	plh_bulk_destroy;
	struct list_head	plh_segs;      /* layout segments list */
	unsigned long		plh_block_lgets; /* block LAYOUTGET if >0 */
	unsigned long		plh_retry_timestamp;
	unsigned long		plh_flags;
	nfs4_stateid		plh_stateid;
	u32			plh_barrier; /* ignore lower seqids */
	enum pnfs_iomode	plh_return_iomode;
	loff_t			plh_lwb; /* last write byte for layoutcommit */
	struct rpc_cred		*plh_lc_cred; /* layoutcommit cred */
	struct inode		*plh_inode;
};

struct pnfs_device {
	struct nfs4_deviceid dev_id;
	unsigned int  layout_type;
	unsigned int  mincount;
	unsigned int  maxcount;	/* gdia_maxcount */
	struct page **pages;
	unsigned int  pgbase;
	unsigned int  pglen;	/* reply buffer length */
	unsigned char nocache : 1;/* May not be cached */
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
extern int nfs4_proc_getdeviceinfo(struct nfs_server *server,
				   struct pnfs_device *dev,
				   struct rpc_cred *cred);
extern struct pnfs_layout_segment* nfs4_proc_layoutget(struct nfs4_layoutget *lgp, gfp_t gfp_flags);
extern int nfs4_proc_layoutreturn(struct nfs4_layoutreturn *lrp, bool sync);

/* pnfs.c */
void pnfs_get_layout_hdr(struct pnfs_layout_hdr *lo);
void pnfs_put_lseg(struct pnfs_layout_segment *lseg);
void pnfs_put_lseg_locked(struct pnfs_layout_segment *lseg);

void set_pnfs_layoutdriver(struct nfs_server *, const struct nfs_fh *, u32);
void unset_pnfs_layoutdriver(struct nfs_server *);
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
void pnfs_free_lseg_list(struct list_head *tmp_list);
void pnfs_destroy_layout(struct nfs_inode *);
void pnfs_destroy_all_layouts(struct nfs_client *);
int pnfs_destroy_layouts_byfsid(struct nfs_client *clp,
		struct nfs_fsid *fsid,
		bool is_recall);
int pnfs_destroy_layouts_byclid(struct nfs_client *clp,
		bool is_recall);
void pnfs_put_layout_hdr(struct pnfs_layout_hdr *lo);
void pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo,
			     const nfs4_stateid *new,
			     bool update_barrier);
int pnfs_choose_layoutget_stateid(nfs4_stateid *dst,
				  struct pnfs_layout_hdr *lo,
				  const struct pnfs_layout_range *range,
				  struct nfs4_state *open_state);
int pnfs_mark_matching_lsegs_invalid(struct pnfs_layout_hdr *lo,
				struct list_head *tmp_list,
				const struct pnfs_layout_range *recall_range);
int pnfs_mark_matching_lsegs_return(struct pnfs_layout_hdr *lo,
				struct list_head *tmp_list,
				const struct pnfs_layout_range *recall_range);
bool pnfs_roc(struct inode *ino);
void pnfs_roc_release(struct inode *ino);
void pnfs_roc_set_barrier(struct inode *ino, u32 barrier);
void pnfs_roc_get_barrier(struct inode *ino, u32 *barrier);
bool pnfs_wait_on_layoutreturn(struct inode *ino, struct rpc_task *task);
void pnfs_set_layoutcommit(struct inode *, struct pnfs_layout_segment *, loff_t);
void pnfs_cleanup_layoutcommit(struct nfs4_layoutcommit_data *data);
int pnfs_layoutcommit_inode(struct inode *inode, bool sync);
int pnfs_generic_sync(struct inode *inode, bool datasync);
int pnfs_nfs_generic_sync(struct inode *inode, bool datasync);
int _pnfs_return_layout(struct inode *);
int pnfs_commit_and_return_layout(struct inode *);
void pnfs_ld_write_done(struct nfs_pgio_header *);
void pnfs_ld_read_done(struct nfs_pgio_header *);
void pnfs_read_resend_pnfs(struct nfs_pgio_header *);
struct pnfs_layout_segment *pnfs_update_layout(struct inode *ino,
					       struct nfs_open_context *ctx,
					       loff_t pos,
					       u64 count,
					       enum pnfs_iomode iomode,
					       gfp_t gfp_flags);
void pnfs_clear_layoutreturn_waitbit(struct pnfs_layout_hdr *lo);

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
void pnfs_error_mark_layout_for_return(struct inode *inode,
				       struct pnfs_layout_segment *lseg);
/* nfs4_deviceid_flags */
enum {
	NFS_DEVICEID_INVALID = 0,       /* set when MDS clientid recalled */
	NFS_DEVICEID_UNAVAILABLE,	/* device temporarily unavailable */
	NFS_DEVICEID_NOCACHE,		/* device may not be cached */
};

/* pnfs_dev.c */
struct nfs4_deviceid_node {
	struct hlist_node		node;
	struct hlist_node		tmpnode;
	const struct pnfs_layoutdriver_type *ld;
	const struct nfs_client		*nfs_client;
	unsigned long 			flags;
	unsigned long			timestamp_unavailable;
	struct nfs4_deviceid		deviceid;
	struct rcu_head			rcu;
	atomic_t			ref;
};

struct nfs4_deviceid_node *
nfs4_find_get_deviceid(struct nfs_server *server,
		const struct nfs4_deviceid *id, struct rpc_cred *cred,
		gfp_t gfp_mask);
void nfs4_delete_deviceid(const struct pnfs_layoutdriver_type *, const struct nfs_client *, const struct nfs4_deviceid *);
void nfs4_init_deviceid_node(struct nfs4_deviceid_node *, struct nfs_server *,
			     const struct nfs4_deviceid *);
bool nfs4_put_deviceid_node(struct nfs4_deviceid_node *);
void nfs4_mark_deviceid_unavailable(struct nfs4_deviceid_node *node);
bool nfs4_test_deviceid_unavailable(struct nfs4_deviceid_node *node);
void nfs4_deviceid_purge_client(const struct nfs_client *);

/* pnfs_nfs.c */
void pnfs_generic_clear_request_commit(struct nfs_page *req,
				       struct nfs_commit_info *cinfo);
void pnfs_generic_commit_release(void *calldata);
void pnfs_generic_prepare_to_resend_writes(struct nfs_commit_data *data);
void pnfs_generic_rw_release(void *data);
void pnfs_generic_recover_commit_reqs(struct list_head *dst,
				      struct nfs_commit_info *cinfo);
int pnfs_generic_commit_pagelist(struct inode *inode,
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
void nfs4_pnfs_ds_connect(struct nfs_server *mds_srv, struct nfs4_pnfs_ds *ds,
			  struct nfs4_deviceid_node *devid, unsigned int timeo,
			  unsigned int retrans, u32 version, u32 minor_version,
			  rpc_authflavor_t au_flavor);
struct nfs4_pnfs_ds_addr *nfs4_decode_mp_ds_addr(struct net *net,
						 struct xdr_stream *xdr,
						 gfp_t gfp_flags);
void pnfs_layout_mark_request_commit(struct nfs_page *req,
				     struct pnfs_layout_segment *lseg,
				     struct nfs_commit_info *cinfo,
				     u32 ds_commit_idx);

static inline bool nfs_have_layout(struct inode *inode)
{
	return NFS_I(inode)->layout != NULL;
}

static inline struct nfs4_deviceid_node *
nfs4_get_deviceid(struct nfs4_deviceid_node *d)
{
	atomic_inc(&d->ref);
	return d;
}

static inline struct pnfs_layout_segment *
pnfs_get_lseg(struct pnfs_layout_segment *lseg)
{
	if (lseg) {
		atomic_inc(&lseg->pls_refcount);
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
pnfs_commit_list(struct inode *inode, struct list_head *mds_pages, int how,
		 struct nfs_commit_info *cinfo)
{
	if (cinfo->ds == NULL || cinfo->ds->ncommitting == 0)
		return PNFS_NOT_ATTEMPTED;
	return NFS_SERVER(inode)->pnfs_curr_ld->commit_pagelist(inode, mds_pages, how, cinfo);
}

static inline struct pnfs_ds_commit_info *
pnfs_get_ds_info(struct inode *inode)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(inode)->pnfs_curr_ld;

	if (ld == NULL || ld->get_ds_info == NULL)
		return NULL;
	return ld->get_ds_info(inode);
}

static inline void
pnfs_generic_mark_devid_invalid(struct nfs4_deviceid_node *node)
{
	set_bit(NFS_DEVICEID_INVALID, &node->flags);
}

static inline bool
pnfs_mark_request_commit(struct nfs_page *req, struct pnfs_layout_segment *lseg,
			 struct nfs_commit_info *cinfo, u32 ds_commit_idx)
{
	struct inode *inode = d_inode(req->wb_context->dentry);
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(inode)->pnfs_curr_ld;

	if (lseg == NULL || ld->mark_request_commit == NULL)
		return false;
	ld->mark_request_commit(req, lseg, cinfo, ds_commit_idx);
	return true;
}

static inline bool
pnfs_clear_request_commit(struct nfs_page *req, struct nfs_commit_info *cinfo)
{
	struct inode *inode = d_inode(req->wb_context->dentry);
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(inode)->pnfs_curr_ld;

	if (ld == NULL || ld->clear_request_commit == NULL)
		return false;
	ld->clear_request_commit(req, cinfo);
	return true;
}

static inline int
pnfs_scan_commit_lists(struct inode *inode, struct nfs_commit_info *cinfo,
		       int max)
{
	if (cinfo->ds == NULL || cinfo->ds->nwritten == 0)
		return 0;
	else
		return NFS_SERVER(inode)->pnfs_curr_ld->scan_commit_lists(cinfo, max);
}

static inline struct nfs_page *
pnfs_search_commit_reqs(struct inode *inode, struct nfs_commit_info *cinfo,
			struct page *page)
{
	struct pnfs_layoutdriver_type *ld = NFS_SERVER(inode)->pnfs_curr_ld;

	if (ld == NULL || ld->search_commit_reqs == NULL)
		return NULL;
	return ld->search_commit_reqs(cinfo, page);
}

/* Should the pNFS client commit and return the layout upon a setattr */
static inline bool
pnfs_ld_layoutret_on_setattr(struct inode *inode)
{
	if (!pnfs_enabled_sb(NFS_SERVER(inode)))
		return false;
	return NFS_SERVER(inode)->pnfs_curr_ld->flags &
		PNFS_LAYOUTRET_ON_SETATTR;
}

static inline bool
pnfs_ld_read_whole_page(struct inode *inode)
{
	if (!pnfs_enabled_sb(NFS_SERVER(inode)))
		return false;
	return NFS_SERVER(inode)->pnfs_curr_ld->flags & PNFS_READ_WHOLE_PAGE;
}

static inline int
pnfs_sync_inode(struct inode *inode, bool datasync)
{
	if (!pnfs_enabled_sb(NFS_SERVER(inode)))
		return 0;
	return NFS_SERVER(inode)->pnfs_curr_ld->sync(inode, datasync);
}

static inline bool
pnfs_layoutcommit_outstanding(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	return test_bit(NFS_INO_LAYOUTCOMMIT, &nfsi->flags) != 0 ||
		test_bit(NFS_INO_LAYOUTCOMMITTING, &nfsi->flags) != 0;
}

static inline int pnfs_return_layout(struct inode *ino)
{
	struct nfs_inode *nfsi = NFS_I(ino);
	struct nfs_server *nfss = NFS_SERVER(ino);

	if (pnfs_enabled_sb(nfss) && nfsi->layout)
		return _pnfs_return_layout(ino);

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

/**
 * pnfs_mark_layout_returned_if_empty - marks the layout as returned
 * @lo: layout header
 *
 * Note: Caller must hold inode->i_lock
 */
static inline void
pnfs_mark_layout_returned_if_empty(struct pnfs_layout_hdr *lo)
{
	if (list_empty(&lo->plh_segs))
		set_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags);
}

static inline void
pnfs_copy_range(struct pnfs_layout_range *dst,
		const struct pnfs_layout_range *src)
{
	memcpy(dst, src, sizeof(*dst));
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

static inline bool nfs_have_layout(struct inode *inode)
{
	return false;
}

static inline void pnfs_destroy_all_layouts(struct nfs_client *clp)
{
}

static inline void pnfs_destroy_layout(struct nfs_inode *nfsi)
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

static inline int pnfs_return_layout(struct inode *ino)
{
	return 0;
}

static inline int pnfs_commit_and_return_layout(struct inode *inode)
{
	return 0;
}

static inline bool
pnfs_ld_layoutret_on_setattr(struct inode *inode)
{
	return false;
}

static inline bool
pnfs_ld_read_whole_page(struct inode *inode)
{
	return false;
}

static inline int
pnfs_sync_inode(struct inode *inode, bool datasync)
{
	return 0;
}

static inline bool
pnfs_roc(struct inode *ino)
{
	return false;
}

static inline void
pnfs_roc_release(struct inode *ino)
{
}

static inline void
pnfs_roc_set_barrier(struct inode *ino, u32 barrier)
{
}

static inline void
pnfs_roc_get_barrier(struct inode *ino, u32 *barrier)
{
}

static inline bool
pnfs_wait_on_layoutreturn(struct inode *ino, struct rpc_task *task)
{
	return false;
}

static inline void set_pnfs_layoutdriver(struct nfs_server *s,
					 const struct nfs_fh *mntfh, u32 id)
{
}

static inline void unset_pnfs_layoutdriver(struct nfs_server *s)
{
}

static inline int
pnfs_commit_list(struct inode *inode, struct list_head *mds_pages, int how,
		 struct nfs_commit_info *cinfo)
{
	return PNFS_NOT_ATTEMPTED;
}

static inline struct pnfs_ds_commit_info *
pnfs_get_ds_info(struct inode *inode)
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
pnfs_scan_commit_lists(struct inode *inode, struct nfs_commit_info *cinfo,
		       int max)
{
	return 0;
}

static inline struct nfs_page *
pnfs_search_commit_reqs(struct inode *inode, struct nfs_commit_info *cinfo,
			struct page *page)
{
	return NULL;
}

static inline int pnfs_layoutcommit_inode(struct inode *inode, bool sync)
{
	return 0;
}

static inline bool
pnfs_use_threshold(struct nfs4_threshold **dst, struct nfs4_threshold *src,
		   struct nfs_server *nfss)
{
	return false;
}

static inline bool
pnfs_layoutcommit_outstanding(struct inode *inode)
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

#endif /* CONFIG_NFS_V4_1 */

#if IS_ENABLED(CONFIG_NFS_V4_2)
int pnfs_report_layoutstat(struct inode *inode, gfp_t gfp_flags);
#else
static inline int
pnfs_report_layoutstat(struct inode *inode, gfp_t gfp_flags)
{
	return 0;
}
#endif

#endif /* FS_NFS_PNFS_H */
