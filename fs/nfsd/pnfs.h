#ifndef _FS_NFSD_PNFS_H
#define _FS_NFSD_PNFS_H 1

#ifdef CONFIG_NFSD_V4
#include <linux/exportfs.h>
#include <linux/nfsd/export.h>

#include "state.h"
#include "xdr4.h"

struct xdr_stream;

struct nfsd4_deviceid_map {
	struct list_head	hash;
	u64			idx;
	int			fsid_type;
	u32			fsid[];
};

struct nfsd4_layout_ops {
	u32		notify_types;

	__be32 (*proc_getdeviceinfo)(struct super_block *sb,
			struct svc_rqst *rqstp,
			struct nfs4_client *clp,
			struct nfsd4_getdeviceinfo *gdevp);
	__be32 (*encode_getdeviceinfo)(struct xdr_stream *xdr,
			struct nfsd4_getdeviceinfo *gdevp);

	__be32 (*proc_layoutget)(struct inode *, const struct svc_fh *fhp,
			struct nfsd4_layoutget *lgp);
	__be32 (*encode_layoutget)(struct xdr_stream *,
			struct nfsd4_layoutget *lgp);

	__be32 (*proc_layoutcommit)(struct inode *inode,
			struct nfsd4_layoutcommit *lcp);

	void (*fence_client)(struct nfs4_layout_stateid *ls);
};

extern const struct nfsd4_layout_ops *nfsd4_layout_ops[];
#ifdef CONFIG_NFSD_BLOCKLAYOUT
extern const struct nfsd4_layout_ops bl_layout_ops;
#endif
#ifdef CONFIG_NFSD_SCSILAYOUT
extern const struct nfsd4_layout_ops scsi_layout_ops;
#endif
#ifdef CONFIG_NFSD_FLEXFILELAYOUT
extern const struct nfsd4_layout_ops ff_layout_ops;
#endif

__be32 nfsd4_preprocess_layout_stateid(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate, stateid_t *stateid,
		bool create, u32 layout_type, struct nfs4_layout_stateid **lsp);
__be32 nfsd4_insert_layout(struct nfsd4_layoutget *lgp,
		struct nfs4_layout_stateid *ls);
__be32 nfsd4_return_file_layouts(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_layoutreturn *lrp);
__be32 nfsd4_return_client_layouts(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_layoutreturn *lrp);
int nfsd4_set_deviceid(struct nfsd4_deviceid *id, const struct svc_fh *fhp,
		u32 device_generation);
struct nfsd4_deviceid_map *nfsd4_find_devid_map(int idx);
#endif /* CONFIG_NFSD_V4 */

#ifdef CONFIG_NFSD_PNFS
void nfsd4_setup_layout_type(struct svc_export *exp);
void nfsd4_return_all_client_layouts(struct nfs4_client *);
void nfsd4_return_all_file_layouts(struct nfs4_client *clp,
		struct nfs4_file *fp);
int nfsd4_init_pnfs(void);
void nfsd4_exit_pnfs(void);
#else
struct nfs4_client;
struct nfs4_file;

static inline void nfsd4_setup_layout_type(struct svc_export *exp)
{
}

static inline void nfsd4_return_all_client_layouts(struct nfs4_client *clp)
{
}
static inline void nfsd4_return_all_file_layouts(struct nfs4_client *clp,
		struct nfs4_file *fp)
{
}
static inline void nfsd4_exit_pnfs(void)
{
}
static inline int nfsd4_init_pnfs(void)
{
	return 0;
}
#endif /* CONFIG_NFSD_PNFS */
#endif /* _FS_NFSD_PNFS_H */
