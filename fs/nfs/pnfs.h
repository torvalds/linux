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

#include <linux/nfs_page.h>

enum {
	NFS_LSEG_VALID = 0,	/* cleared when lseg is recalled/returned */
	NFS_LSEG_ROC,		/* roc bit received from server */
};

struct pnfs_layout_segment {
	struct list_head pls_list;
	struct pnfs_layout_range pls_range;
	atomic_t pls_refcount;
	unsigned long pls_flags;
	struct pnfs_layout_hdr *pls_layout;
	struct rpc_cred	*pls_lc_cred; /* LAYOUTCOMMIT credential */
	loff_t pls_end_pos; /* LAYOUTCOMMIT write end */
};

enum pnfs_try_status {
	PNFS_ATTEMPTED     = 0,
	PNFS_NOT_ATTEMPTED = 1,
};

#ifdef CONFIG_NFS_V4_1

#define LAYOUT_NFSV4_1_MODULE_PREFIX "nfs-layouttype4"

enum {
	NFS_LAYOUT_RO_FAILED = 0,	/* get ro layout failed stop trying */
	NFS_LAYOUT_RW_FAILED,		/* get rw layout failed stop trying */
	NFS_LAYOUT_BULK_RECALL,		/* bulk recall affecting layout */
	NFS_LAYOUT_ROC,			/* some lseg had roc bit set */
	NFS_LAYOUT_DESTROYED,		/* no new use of layout allowed */
};

/* Per-layout driver specific registration structure */
struct pnfs_layoutdriver_type {
	struct list_head pnfs_tblid;
	const u32 id;
	const char *name;
	struct module *owner;
	struct pnfs_layout_segment * (*alloc_lseg) (struct pnfs_layout_hdr *layoutid, struct nfs4_layoutget_res *lgr, gfp_t gfp_flags);
	void (*free_lseg) (struct pnfs_layout_segment *lseg);

	/* test for nfs page cache coalescing */
	int (*pg_test)(struct nfs_pageio_descriptor *, struct nfs_page *, struct nfs_page *);

	/* Returns true if layoutdriver wants to divert this request to
	 * driver's commit routine.
	 */
	bool (*mark_pnfs_commit)(struct pnfs_layout_segment *lseg);
	struct list_head * (*choose_commit_list) (struct nfs_page *req);
	int (*commit_pagelist)(struct inode *inode, struct list_head *mds_pages, int how);

	/*
	 * Return PNFS_ATTEMPTED to indicate the layout code has attempted
	 * I/O, else return PNFS_NOT_ATTEMPTED to fall back to normal NFS
	 */
	enum pnfs_try_status (*read_pagelist) (struct nfs_read_data *nfs_data);
	enum pnfs_try_status (*write_pagelist) (struct nfs_write_data *nfs_data, int how);
};

struct pnfs_layout_hdr {
	atomic_t		plh_refcount;
	struct list_head	plh_layouts;   /* other client layouts */
	struct list_head	plh_bulk_recall; /* clnt list of bulk recalls */
	struct list_head	plh_segs;      /* layout segments list */
	nfs4_stateid		plh_stateid;
	atomic_t		plh_outstanding; /* number of RPCs out */
	unsigned long		plh_block_lgets; /* block LAYOUTGET if >0 */
	u32			plh_barrier; /* ignore lower seqids */
	unsigned long		plh_flags;
	struct inode		*plh_inode;
};

struct pnfs_device {
	struct nfs4_deviceid dev_id;
	unsigned int  layout_type;
	unsigned int  mincount;
	struct page **pages;
	unsigned int  pgbase;
	unsigned int  pglen;
};

extern int pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *);
extern void pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *);

/* nfs4proc.c */
extern int nfs4_proc_getdeviceinfo(struct nfs_server *server,
				   struct pnfs_device *dev);
extern int nfs4_proc_layoutget(struct nfs4_layoutget *lgp);

/* pnfs.c */
void get_layout_hdr(struct pnfs_layout_hdr *lo);
void put_lseg(struct pnfs_layout_segment *lseg);
struct pnfs_layout_segment *
pnfs_update_layout(struct inode *ino, struct nfs_open_context *ctx,
		   enum pnfs_iomode access_type, gfp_t gfp_flags);
void set_pnfs_layoutdriver(struct nfs_server *, u32 id);
void unset_pnfs_layoutdriver(struct nfs_server *);
enum pnfs_try_status pnfs_try_to_write_data(struct nfs_write_data *,
					     const struct rpc_call_ops *, int);
enum pnfs_try_status pnfs_try_to_read_data(struct nfs_read_data *,
					    const struct rpc_call_ops *);
void pnfs_pageio_init_read(struct nfs_pageio_descriptor *, struct inode *);
void pnfs_pageio_init_write(struct nfs_pageio_descriptor *, struct inode *);
int pnfs_layout_process(struct nfs4_layoutget *lgp);
void pnfs_free_lseg_list(struct list_head *tmp_list);
void pnfs_destroy_layout(struct nfs_inode *);
void pnfs_destroy_all_layouts(struct nfs_client *);
void put_layout_hdr(struct pnfs_layout_hdr *lo);
void pnfs_set_layout_stateid(struct pnfs_layout_hdr *lo,
			     const nfs4_stateid *new,
			     bool update_barrier);
int pnfs_choose_layoutget_stateid(nfs4_stateid *dst,
				  struct pnfs_layout_hdr *lo,
				  struct nfs4_state *open_state);
int mark_matching_lsegs_invalid(struct pnfs_layout_hdr *lo,
				struct list_head *tmp_list,
				u32 iomode);
bool pnfs_roc(struct inode *ino);
void pnfs_roc_release(struct inode *ino);
void pnfs_roc_set_barrier(struct inode *ino, u32 barrier);
bool pnfs_roc_drain(struct inode *ino, u32 *barrier);
void pnfs_set_layoutcommit(struct nfs_write_data *wdata);
int pnfs_layoutcommit_inode(struct inode *inode, bool sync);

static inline int lo_fail_bit(u32 iomode)
{
	return iomode == IOMODE_RW ?
			 NFS_LAYOUT_RW_FAILED : NFS_LAYOUT_RO_FAILED;
}

static inline struct pnfs_layout_segment *
get_lseg(struct pnfs_layout_segment *lseg)
{
	if (lseg) {
		atomic_inc(&lseg->pls_refcount);
		smp_mb__after_atomic_inc();
	}
	return lseg;
}

/* Return true if a layout driver is being used for this mountpoint */
static inline int pnfs_enabled_sb(struct nfs_server *nfss)
{
	return nfss->pnfs_curr_ld != NULL;
}

static inline void
pnfs_mark_request_commit(struct nfs_page *req, struct pnfs_layout_segment *lseg)
{
	if (lseg) {
		struct pnfs_layoutdriver_type *ld;

		ld = NFS_SERVER(req->wb_page->mapping->host)->pnfs_curr_ld;
		if (ld->mark_pnfs_commit && ld->mark_pnfs_commit(lseg)) {
			set_bit(PG_PNFS_COMMIT, &req->wb_flags);
			req->wb_commit_lseg = get_lseg(lseg);
		}
	}
}

static inline int
pnfs_commit_list(struct inode *inode, struct list_head *mds_pages, int how)
{
	if (!test_and_clear_bit(NFS_INO_PNFS_COMMIT, &NFS_I(inode)->flags))
		return PNFS_NOT_ATTEMPTED;
	return NFS_SERVER(inode)->pnfs_curr_ld->commit_pagelist(inode, mds_pages, how);
}

static inline struct list_head *
pnfs_choose_commit_list(struct nfs_page *req, struct list_head *mds)
{
	struct list_head *rv;

	if (test_and_clear_bit(PG_PNFS_COMMIT, &req->wb_flags)) {
		struct inode *inode = req->wb_commit_lseg->pls_layout->plh_inode;

		set_bit(NFS_INO_PNFS_COMMIT, &NFS_I(inode)->flags);
		rv = NFS_SERVER(inode)->pnfs_curr_ld->choose_commit_list(req);
		/* matched by ref taken when PG_PNFS_COMMIT is set */
		put_lseg(req->wb_commit_lseg);
	} else
		rv = mds;
	return rv;
}

static inline void pnfs_clear_request_commit(struct nfs_page *req)
{
	if (test_and_clear_bit(PG_PNFS_COMMIT, &req->wb_flags))
		put_lseg(req->wb_commit_lseg);
}

#else  /* CONFIG_NFS_V4_1 */

static inline void pnfs_destroy_all_layouts(struct nfs_client *clp)
{
}

static inline void pnfs_destroy_layout(struct nfs_inode *nfsi)
{
}

static inline struct pnfs_layout_segment *
get_lseg(struct pnfs_layout_segment *lseg)
{
	return NULL;
}

static inline void put_lseg(struct pnfs_layout_segment *lseg)
{
}

static inline struct pnfs_layout_segment *
pnfs_update_layout(struct inode *ino, struct nfs_open_context *ctx,
		   enum pnfs_iomode access_type, gfp_t gfp_flags)
{
	return NULL;
}

static inline enum pnfs_try_status
pnfs_try_to_read_data(struct nfs_read_data *data,
		      const struct rpc_call_ops *call_ops)
{
	return PNFS_NOT_ATTEMPTED;
}

static inline enum pnfs_try_status
pnfs_try_to_write_data(struct nfs_write_data *data,
		       const struct rpc_call_ops *call_ops, int how)
{
	return PNFS_NOT_ATTEMPTED;
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

static inline bool
pnfs_roc_drain(struct inode *ino, u32 *barrier)
{
	return false;
}

static inline void set_pnfs_layoutdriver(struct nfs_server *s, u32 id)
{
}

static inline void unset_pnfs_layoutdriver(struct nfs_server *s)
{
}

static inline void
pnfs_pageio_init_read(struct nfs_pageio_descriptor *pgio, struct inode *ino)
{
	pgio->pg_test = NULL;
}

static inline void
pnfs_pageio_init_write(struct nfs_pageio_descriptor *pgio, struct inode *ino)
{
	pgio->pg_test = NULL;
}

static inline void
pnfs_mark_request_commit(struct nfs_page *req, struct pnfs_layout_segment *lseg)
{
}

static inline int
pnfs_commit_list(struct inode *inode, struct list_head *mds_pages, int how)
{
	return PNFS_NOT_ATTEMPTED;
}

static inline struct list_head *
pnfs_choose_commit_list(struct nfs_page *req, struct list_head *mds)
{
	return mds;
}

static inline void pnfs_clear_request_commit(struct nfs_page *req)
{
}

static inline int pnfs_layoutcommit_inode(struct inode *inode, bool sync)
{
	return 0;
}
#endif /* CONFIG_NFS_V4_1 */

#endif /* FS_NFS_PNFS_H */
