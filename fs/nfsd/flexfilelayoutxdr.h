/*
 * Copyright (c) 2016 Tom Haynes <loghyr@primarydata.com>
 */
#ifndef _NFSD_FLEXFILELAYOUTXDR_H
#define _NFSD_FLEXFILELAYOUTXDR_H 1

#include <linux/inet.h>
#include "xdr4.h"

#define FF_FLAGS_NO_LAYOUTCOMMIT 1
#define FF_FLAGS_NO_IO_THRU_MDS  2
#define FF_FLAGS_NO_READ_IO      4

struct xdr_stream;

#define FF_NETID_LEN		(4)
#define FF_ADDR_LEN		(INET6_ADDRSTRLEN + 8)
struct pnfs_ff_netaddr {
	char				netid[FF_NETID_LEN + 1];
	char				addr[FF_ADDR_LEN + 1];
	u32				netid_len;
	u32				addr_len;
};

struct pnfs_ff_device_addr {
	struct pnfs_ff_netaddr		netaddr;
	u32				version;
	u32				minor_version;
	u32				rsize;
	u32				wsize;
	bool				tightly_coupled;
};

struct pnfs_ff_layout {
	u32				flags;
	u32				stats_collect_hint;
	kuid_t				uid;
	kgid_t				gid;
	struct nfsd4_deviceid		deviceid;
	stateid_t			stateid;
	struct nfs_fh			fh;
};

__be32 nfsd4_ff_encode_getdeviceinfo(struct xdr_stream *xdr,
		struct nfsd4_getdeviceinfo *gdp);
__be32 nfsd4_ff_encode_layoutget(struct xdr_stream *xdr,
		struct nfsd4_layoutget *lgp);

#endif /* _NFSD_FLEXFILELAYOUTXDR_H */
