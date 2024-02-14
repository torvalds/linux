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
	struct list_head	da_node;  /* nfs4_pnfs_dev_hlist dev_dslist */
	char			*da_remotestr;	/* human readable addr+port */
	const char		*da_netid;
	int			da_transport;
};

struct nfs4_pnfs_ds {
	struct list_head	ds_node;  /* nfs4_pnfs_dev_hlist dev_dslist */
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
	struct list_head pls_commits;
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

enum {
	NFS_LAYOUT_RO_FAILED = 0,	/* get ro layout failed stop trying */
	NFS_LAYOUT_RW_FAILED,		/* get rw layout failed stop trying */
	NFS_LAYOUT_BULK_RECALL,		/* bulk recall affecting layout */
	NFS_LAYOUT_RETURN,		/* layoutreturn in progress */
	NFS_LAYOUT_RETURN_LOCK,		/* Serialise layoutreturn */
	NFS_LAYOUT_RETURN_REQUESTED,	/* Return this layout ASAP */
	NFS_LAYOUT_INVALID_STID,	/* layout stateid id is invalid */
	NFS_LAYOUT_FIRST_LAYOUTGET,	/* Serialize first layoutget */
	NFS_LAYOUT_INODE_FREEING,	/* The inode is being freed */
	NFS_LAYOUT_HASHED,		/* The layout visible */
	NFS_LAYOUT_DRAIN,
};

enum layoutdriver_policy_flags {
	/* Should the pNFS client commit and return the layout upon truncate to
	 * a smaller size */
	PNFS_LAYOUTRET_ON_SETATTR	= 1 << 0,
	PNFS_LAYOUTRET_ON_ERROR		= 1 << 1,
	PNFS_READ_WHOLE_PAGE		= 1 << 2,
	PNFS_LAYOUTGET_ON_OPEN		= 1 << 3,
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
	unsigned max_layoutget_response;

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

	int (*prepare_layoutreturn) (struct nfs4_layoutreturn_args *);

	void (*cleanup_layoutcommit) (struct nfs4_layoutcommit_data *data);
	int (*prepare_layoutcommit) (struct nfs4_layoutcommit_args *args);
	int (*prepare_layoutstats) (struct nfs42_layoutstat_args *args);

	void (*cancel_io)(struct pnfs_layout_segment *lseg);
};

struct pnfs_commit_ops {
	void (*setup_ds_info)(struct pnfs_ds_commit_info *,
			      struct pnfs_layout_segment *);
	void (*release_ds_info)(struct pnfs_ds_commit_info *,
				struct inode *inode);
	int (*commit_pagelist)(struct inode *inode,
			       struct list_head *mds_pages,
			       int how,
			       struct nfs_commit_info *cinfo);
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
	u32			plh_barrier; /* ignore lower seqids */
	u32			plh_return_seq;
	enum pnfs_iomode	plh_return_iomode;
	loff_t			plh_lwb; /* last write byte for layoutcommit */
	const struct cred	*plh_lc_cred; /* layoutcommit cred */
	struct inode		*plh_inode;
	struct rcu_head		plh_rcu;
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
extern const struct pnfs_layoutdriver_type *pnfs_find_layoutdriver(u32 id);
extern void pnfs_put_layoutdriver(const struct pnfs_layoutdriver_type *ld);

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
void pnfs_generic_pg_check_range(struct nfs_pageio_descriptor *pgio, struct nfs_page *req);
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
void pnfs_destroy_layout(struct nfs_inode *);
void pnfs_destroy_layout_final(struct nfs_inode *);
void pnfs_destroy_all_layouts(struct nfs_client *);
int pnfs_destroy_layouts_byfsid(struct nfs_client *clp,
		struct nfs_fsid *fsid,
		bool is_recall);
int pnfs_destroy_layouts_byclid(struct nfs_client *clp,
		bool is_recall);
bool nfs4_layout_refresh_old_stateid(nfs4_stateid *dst,
		struct pnfs_layout_range *dst_range,
		struct inode *inode);
void pnfs_put_layout_hdr(struct pnfs_layout_hdr *lo);
void pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo,
			     const nfs4_stateid *new,
			     const struct cred *cred,
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
bool pnfs_roc(struct inode *ino,
		struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		const struct cred *cred);
int pnfs_roc_done(struct rpc_task *task, struct nfs4_layoutreturn_args **argpp,
		  struct nfs4_layoutreturn_res **respp, int *ret);
void pnfs_roc_release(struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		int ret);
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
void pnfs_read_resend_pnfs(struct nfs_pgio_header *, unsigned int mirror_idx);
struct pnfs_layout_segment *pnfs_update_layout(struct inode *ino,
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
void pnfs_error_mark_layout_for_return(struct inode *inode,
				       struct pnfs_layout_segment *lseg);
void pnfs_layout_return_unused_byclid(struct nfs_client *clp,
				      enum pnfs_iomode iomode);

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
		const struct nfs4_deviceid *id, const struct cred *cred,
		gfp_t gfp_mask);
void nfs4_delete_deviceid(const struct pnfs_layoutdriver_type *, const struct nfs_client *, const struct nfs4_deviceid *);
void nfs4_init_deviceid_node(struct nfs4_deviceid_node *, struct nfs_server *,
			     const struct nfs4_deviceid *);
bool nfs4_put_deviceid_node(struct nfs4_deviceid_node *);
void nfs4_mark_deviceid_available(struct nfs4_deviceid_node *node);
void nfs4_mark_deviceid_unavailable(struct nfs4_deviceid_node *node);
bool nfs4_test_deviceid_unavailable(struct nfs4_deviceid_node *node);
void nfs4_deviceid_purge_client(const struct nfs_client *);

/* pnfs_nfs.c */
struct pnfs_commit_array *pnfs_alloc_commit_array(size_t n, gfp_t gfp_flags);
void pnfs_free_commit_array(struct pnfs_commit_array *p);
struct pnfs_commit_array *pnfs_add_commit_array(struct pnfs_ds_commit_info *,
						struct pnfs_commit_array *,
						struct pnfs_layout_segment *);

void pnfs_generic_ds_cinfo_release_lseg(struct pnfs_ds_commit_info *fl_cinfo,
		struct pnfs_layout_segment *lseg);
void pnfs_generic_ds_cinfo_destroy(struct pnfs_ds_commit_info *fl_cinfo);

void pnfs_generic_clear_request_commit(struct nfs_page *req,
				       struct nfs_commit_info *cinfo);
void pnfs_generic_commit_release(void *calldata);
void pnfs_generic_prepare_to_resend_writes(struct nfs_commit_data *data);
void pnfs_generic_rw_release(void *data);
void pnfs_generic_recover_commit_reqs(struct list_head *dst,
				      struct nfs_commit_info *cinfo);
struct nfs_page *pnfs_generic_search_commit_reqs(struct nfs_commit_info *cinfo,
						 struct page *page);
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
int nfs4_pnfs_ds_connect(struct nfs_server *mds_srv, struct nfs4_pnfs_ds *ds,
			  struct nfs4_deviceid_node *devid, unsigned int timeo,
			  unsigned int retrans, u32 version, u32 minor_version);
struct nfs4_pnfs_ds_addr *nfs4_decode_mp_ds_addr(struct net *net,
						 struct xdr_stream *xdr,
						 gfp_t gfp_flags);
void pnfs_layout_mark_request_commit(struct nfs_page *req,
				     struct pnfs_layout_segment *lseg,
				     struct nfs_commit_info *cinfo,
				     u32 ds_commit_idx);
void pnfs_lgopen_prepare(struct nfs4_opendata *data,
			 struct nfs_open_context *ctx);
void pnfs_parse_lgopen(struct inode *ino, struct nfs4_layoutget *lgp,
		       struct nfs_open_context *ctx);
void nfs4_lgopen_release(struct nfs4_layoutget *lgp);

static inline bool nfs_have_layout(struct inode *inode)
{
	return NFS_I(inode)->layout != NULL;
}

static inline bool pnfs_layout_is_valid(const struct pnfs_layout_hdr *lo)
{
	return test_bit(NFS_LAYOUT_INVALID_STID, &lo->plh_flags) == 0;
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
pnfs_commit_list(struct inode *inode, struct list_head *mds_pages, int how,
		 struct nfs_commit_info *cinfo)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;

	if (fl_cinfo == NULL || fl_cinfo->ncommitting == 0)
		return PNFS_NOT_ATTEMPTED;
	return fl_cinfo->ops->commit_pagelist(inode, mds_pages, how, cinfo);
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
pnfs_init_ds_commit_info_ops(struct pnfs_ds_commit_info *fl_cinfo, struct inode *inode)
{
	struct pnfs_ds_commit_info *inode_cinfo = pnfs_get_ds_info(inode);
	if (inode_cinfo != NULL)
		fl_cinfo->ops = inode_cinfo->ops;
}

static inline void
pnfs_init_ds_commit_info(struct pnfs_ds_commit_info *fl_cinfo)
{
	INIT_LIST_HEAD(&fl_cinfo->commits);
	fl_cinfo->ops = NULL;
}

static inline void
pnfs_release_ds_info(struct pnfs_ds_commit_info *fl_cinfo, struct inode *inode)
{
	if (fl_cinfo->ops != NULL && fl_cinfo->ops->release_ds_info != NULL)
		fl_cinfo->ops->release_ds_info(fl_cinfo, inode);
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
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;

	if (!lseg || !fl_cinfo->ops || !fl_cinfo->ops->mark_request_commit)
		return false;
	fl_cinfo->ops->mark_request_commit(req, lseg, cinfo, ds_commit_idx);
	return true;
}

static inline bool
pnfs_clear_request_commit(struct nfs_page *req, struct nfs_commit_info *cinfo)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;

	if (!fl_cinfo || !fl_cinfo->ops || !fl_cinfo->ops->clear_request_commit)
		return false;
	fl_cinfo->ops->clear_request_commit(req, cinfo);
	return true;
}

static inline int
pnfs_scan_commit_lists(struct inode *inode, struct nfs_commit_info *cinfo,
		       int max)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;

	if (!fl_cinfo || fl_cinfo->nwritten == 0)
		return 0;
	return fl_cinfo->ops->scan_commit_lists(cinfo, max);
}

static inline void
pnfs_recover_commit_reqs(struct list_head *head, struct nfs_commit_info *cinfo)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;

	if (fl_cinfo && fl_cinfo->nwritten != 0)
		fl_cinfo->ops->recover_commit_reqs(head, cinfo);
}

static inline struct nfs_page *
pnfs_search_commit_reqs(struct inode *inode, struct nfs_commit_info *cinfo,
			struct page *page)
{
	struct pnfs_ds_commit_info *fl_cinfo = cinfo->ds;

	if (!fl_cinfo->ops || !fl_cinfo->ops->search_commit_reqs)
		return NULL;
	return fl_cinfo->ops->search_commit_reqs(cinfo, page);
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

	if (pnfs_enabled_sb(nfss) && nfsi->layout) {
		set_bit(NFS_LAYOUT_RETURN_REQUESTED, &nfsi->layout->plh_flags);
		return _pnfs_return_layout(ino);
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

static inline void pnfs_lseg_cancel_io(struct nfs_server *server,
				       struct pnfs_layout_segment *lseg)
{
	if (server->pnfs_curr_ld->cancel_io)
		server->pnfs_curr_ld->cancel_io(lseg);
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

static inline void pnfs_destroy_layout_final(struct nfs_inode *nfsi)
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
pnfs_layoutcommit_outstanding(struct inode *inode)
{
	return false;
}


static inline bool
pnfs_roc(struct inode *ino,
		struct nfs4_layoutreturn_args *args,
		struct nfs4_layoutreturn_res *res,
		const struct cred *cred)
{
	return false;
}

static inline int
pnfs_roc_done(struct rpc_task *task,
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
pnfs_wait_on_layoutreturn(struct inode *ino, struct rpc_task *task)
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

static inline void
pnfs_init_ds_commit_info_ops(struct pnfs_ds_commit_info *fl_cinfo, struct inode *inode)
{
}

static inline void
pnfs_init_ds_commit_info(struct pnfs_ds_commit_info *fl_cinfo)
{
}

static inline void
pnfs_release_ds_info(struct pnfs_ds_commit_info *fl_cinfo, struct inode *inode)
{
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

static inline void
pnfs_recover_commit_reqs(struct list_head *head, struct nfs_commit_info *cinfo)
{
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

static inline struct nfs4_threshold *pnfs_mdsthreshold_alloc(void)
{
	return NULL;
}

static inline void nfs4_pnfs_v3_ds_connect_unload(void)
{
}

static inline bool nfs4_layout_refresh_old_stateid(nfs4_stateid *dst,
		struct pnfs_layout_range *dst_range,
		struct inode *inode)
{
	return false;
}

static inline void pnfs_lgopen_prepare(struct nfs4_opendata *data,
		struct nfs_open_context *ctx)
{
}

static inline void pnfs_parse_lgopen(struct inode *ino,
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
int pnfs_report_layoutstat(struct inode *inode, gfp_t gfp_flags);
#else
static inline int
pnfs_report_layoutstat(struct inode *inode, gfp_t gfp_flags)
{
	return 0;
}
#endif

#endif /* FS_NFS_PNFS_H */
