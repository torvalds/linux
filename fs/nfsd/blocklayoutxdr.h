#ifndef _NFSD_BLOCKLAYOUTXDR_H
#define _NFSD_BLOCKLAYOUTXDR_H 1

#include <linux/blkdev.h>
#include "xdr4.h"

struct iomap;
struct xdr_stream;

struct pnfs_block_extent {
	struct nfsd4_deviceid		vol_id;
	u64				foff;
	u64				len;
	u64				soff;
	enum pnfs_block_extent_state	es;
};

struct pnfs_block_range {
	u64				foff;
	u64				len;
};

/*
 * Random upper cap for the uuid length to avoid unbounded allocation.
 * Not actually limited by the protocol.
 */
#define PNFS_BLOCK_UUID_LEN	128

struct pnfs_block_volume {
	enum pnfs_block_volume_type	type;
	union {
		struct {
			u64		offset;
			u32		sig_len;
			u8		sig[PNFS_BLOCK_UUID_LEN];
		} simple;
		struct {
			enum scsi_code_set		code_set;
			enum scsi_designator_type	designator_type;
			int				designator_len;
			u8				designator[256];
			u64				pr_key;
		} scsi;
	};
};

struct pnfs_block_deviceaddr {
	u32				nr_volumes;
	struct pnfs_block_volume	volumes[];
};

__be32 nfsd4_block_encode_getdeviceinfo(struct xdr_stream *xdr,
		struct nfsd4_getdeviceinfo *gdp);
__be32 nfsd4_block_encode_layoutget(struct xdr_stream *xdr,
		struct nfsd4_layoutget *lgp);
int nfsd4_block_decode_layoutupdate(__be32 *p, u32 len, struct iomap **iomapp,
		u32 block_size);
int nfsd4_scsi_decode_layoutupdate(__be32 *p, u32 len, struct iomap **iomapp,
		u32 block_size);

#endif /* _NFSD_BLOCKLAYOUTXDR_H */
