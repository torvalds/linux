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

struct pnfs_layout_segment {
	struct list_head pls_list;
	struct pnfs_layout_range pls_range;
	struct kref pls_refcount;
	struct pnfs_layout_hdr *pls_layout;
};

#ifdef CONFIG_NFS_V4_1

#define LAYOUT_NFSV4_1_MODULE_PREFIX "nfs-layouttype4"

enum {
	NFS_LAYOUT_RO_FAILED = 0,	/* get ro layout failed stop trying */
	NFS_LAYOUT_RW_FAILED,		/* get rw layout failed stop trying */
	NFS_LAYOUT_STATEID_SET,		/* have a valid layout stateid */
};

/* Per-layout driver specific registration structure */
struct pnfs_layoutdriver_type {
	struct list_head pnfs_tblid;
	const u32 id;
	const char *name;
	struct module *owner;
	int (*set_layoutdriver) (struct nfs_server *);
	int (*clear_layoutdriver) (struct nfs_server *);
	struct pnfs_layout_segment * (*alloc_lseg) (struct pnfs_layout_hdr *layoutid, struct nfs4_layoutget_res *lgr);
	void (*free_lseg) (struct pnfs_layout_segment *lseg);
};

struct pnfs_layout_hdr {
	unsigned long		refcount;
	struct list_head	layouts;   /* other client layouts */
	struct list_head	segs;      /* layout segments list */
	seqlock_t		seqlock;   /* Protects the stateid */
	nfs4_stateid		stateid;
	unsigned long		plh_flags;
	struct inode		*inode;
};

struct pnfs_device {
	struct nfs4_deviceid dev_id;
	unsigned int  layout_type;
	unsigned int  mincount;
	struct page **pages;
	void          *area;
	unsigned int  pgbase;
	unsigned int  pglen;
};

/*
 * Device ID RCU cache. A device ID is unique per client ID and layout type.
 */
#define NFS4_DEVICE_ID_HASH_BITS	5
#define NFS4_DEVICE_ID_HASH_SIZE	(1 << NFS4_DEVICE_ID_HASH_BITS)
#define NFS4_DEVICE_ID_HASH_MASK	(NFS4_DEVICE_ID_HASH_SIZE - 1)

static inline u32
nfs4_deviceid_hash(struct nfs4_deviceid *id)
{
	unsigned char *cptr = (unsigned char *)id->data;
	unsigned int nbytes = NFS4_DEVICEID4_SIZE;
	u32 x = 0;

	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x & NFS4_DEVICE_ID_HASH_MASK;
}

struct pnfs_deviceid_node {
	struct hlist_node	de_node;
	struct nfs4_deviceid	de_id;
	atomic_t		de_ref;
};

struct pnfs_deviceid_cache {
	spinlock_t		dc_lock;
	atomic_t		dc_ref;
	void			(*dc_free_callback)(struct pnfs_deviceid_node *);
	struct hlist_head	dc_deviceids[NFS4_DEVICE_ID_HASH_SIZE];
};

extern int pnfs_alloc_init_deviceid_cache(struct nfs_client *,
			void (*free_callback)(struct pnfs_deviceid_node *));
extern void pnfs_put_deviceid_cache(struct nfs_client *);
extern struct pnfs_deviceid_node *pnfs_find_get_deviceid(
				struct pnfs_deviceid_cache *,
				struct nfs4_deviceid *);
extern struct pnfs_deviceid_node *pnfs_add_deviceid(
				struct pnfs_deviceid_cache *,
				struct pnfs_deviceid_node *);
extern void pnfs_put_deviceid(struct pnfs_deviceid_cache *c,
			      struct pnfs_deviceid_node *devid);

extern int pnfs_register_layoutdriver(struct pnfs_layoutdriver_type *);
extern void pnfs_unregister_layoutdriver(struct pnfs_layoutdriver_type *);

/* nfs4proc.c */
extern int nfs4_proc_getdeviceinfo(struct nfs_server *server,
				   struct pnfs_device *dev);
extern int nfs4_proc_layoutget(struct nfs4_layoutget *lgp);

/* pnfs.c */
struct pnfs_layout_segment *
pnfs_update_layout(struct inode *ino, struct nfs_open_context *ctx,
		   enum pnfs_iomode access_type);
void set_pnfs_layoutdriver(struct nfs_server *, u32 id);
void unset_pnfs_layoutdriver(struct nfs_server *);
int pnfs_layout_process(struct nfs4_layoutget *lgp);
void pnfs_destroy_layout(struct nfs_inode *);
void pnfs_destroy_all_layouts(struct nfs_client *);
void put_layout_hdr(struct inode *inode);
void pnfs_get_layout_stateid(nfs4_stateid *dst, struct pnfs_layout_hdr *lo,
			     struct nfs4_state *open_state);


static inline int lo_fail_bit(u32 iomode)
{
	return iomode == IOMODE_RW ?
			 NFS_LAYOUT_RW_FAILED : NFS_LAYOUT_RO_FAILED;
}

/* Return true if a layout driver is being used for this mountpoint */
static inline int pnfs_enabled_sb(struct nfs_server *nfss)
{
	return nfss->pnfs_curr_ld != NULL;
}

#else  /* CONFIG_NFS_V4_1 */

static inline void pnfs_destroy_all_layouts(struct nfs_client *clp)
{
}

static inline void pnfs_destroy_layout(struct nfs_inode *nfsi)
{
}

static inline struct pnfs_layout_segment *
pnfs_update_layout(struct inode *ino, struct nfs_open_context *ctx,
		   enum pnfs_iomode access_type)
{
	return NULL;
}

static inline void set_pnfs_layoutdriver(struct nfs_server *s, u32 id)
{
}

static inline void unset_pnfs_layoutdriver(struct nfs_server *s)
{
}

#endif /* CONFIG_NFS_V4_1 */

#endif /* FS_NFS_PNFS_H */
