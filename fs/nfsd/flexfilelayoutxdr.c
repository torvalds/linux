// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Tom Haynes <loghyr@primarydata.com>
 */
#include <linux/sunrpc/svc.h>
#include <linux/nfs4.h>

#include "nfsd.h"
#include "flexfilelayoutxdr.h"

#define NFSDDBG_FACILITY	NFSDDBG_PNFS

struct ff_idmap {
	char buf[11];
	int len;
};

__be32
nfsd4_ff_encode_layoutget(struct xdr_stream *xdr,
		const struct nfsd4_layoutget *lgp)
{
	const struct pnfs_ff_layout *fl = lgp->lg_content;
	int len, mirror_len, ds_len, fh_len;
	__be32 *p;

	/*
	 * Unlike nfsd4_encode_user, we know these will
	 * always be stringified.
	 */
	struct ff_idmap uid;
	struct ff_idmap gid;

	fh_len = 4 + fl->fh.size;

	uid.len = sprintf(uid.buf, "%u", from_kuid(&init_user_ns, fl->uid));
	gid.len = sprintf(gid.buf, "%u", from_kgid(&init_user_ns, fl->gid));

	/* 8 + len for recording the length, name, and padding */
	ds_len = 20 + sizeof(stateid_opaque_t) + 4 + fh_len +
		 8 + uid.len + 8 + gid.len;

	mirror_len = 4 + ds_len;

	/* The layout segment */
	len = 20 + mirror_len;

	p = xdr_reserve_space(xdr, sizeof(__be32) + len);
	if (!p)
		return nfserr_toosmall;

	*p++ = cpu_to_be32(len);
	p = xdr_encode_hyper(p, 0);		/* stripe unit of 1 */

	*p++ = cpu_to_be32(1);			/* single mirror */
	*p++ = cpu_to_be32(1);			/* single data server */

	p = xdr_encode_opaque_fixed(p, &fl->deviceid,
			sizeof(struct nfsd4_deviceid));

	*p++ = cpu_to_be32(1);			/* efficiency */

	*p++ = cpu_to_be32(fl->stateid.si_generation);
	p = xdr_encode_opaque_fixed(p, &fl->stateid.si_opaque,
				    sizeof(stateid_opaque_t));

	*p++ = cpu_to_be32(1);			/* single file handle */
	p = xdr_encode_opaque(p, fl->fh.data, fl->fh.size);

	p = xdr_encode_opaque(p, uid.buf, uid.len);
	p = xdr_encode_opaque(p, gid.buf, gid.len);

	*p++ = cpu_to_be32(fl->flags);
	*p++ = cpu_to_be32(0);			/* No stats collect hint */

	return 0;
}

__be32
nfsd4_ff_encode_getdeviceinfo(struct xdr_stream *xdr,
		const struct nfsd4_getdeviceinfo *gdp)
{
	struct pnfs_ff_device_addr *da = gdp->gd_device;
	int len;
	int ver_len;
	int addr_len;
	__be32 *p;

	/*
	 * See paragraph 5 of RFC 8881 S18.40.3.
	 */
	if (!gdp->gd_maxcount) {
		if (xdr_stream_encode_u32(xdr, 0) != XDR_UNIT)
			return nfserr_resource;
		return nfs_ok;
	}

	/* len + padding for two strings */
	addr_len = 16 + da->netaddr.netid_len + da->netaddr.addr_len;
	ver_len = 20;

	len = 4 + ver_len + 4 + addr_len;

	p = xdr_reserve_space(xdr, len + sizeof(__be32));
	if (!p)
		return nfserr_resource;

	/*
	 * Fill in the overall length and number of volumes at the beginning
	 * of the layout.
	 */
	*p++ = cpu_to_be32(len);
	*p++ = cpu_to_be32(1);			/* 1 netaddr */
	p = xdr_encode_opaque(p, da->netaddr.netid, da->netaddr.netid_len);
	p = xdr_encode_opaque(p, da->netaddr.addr, da->netaddr.addr_len);

	*p++ = cpu_to_be32(1);			/* 1 versions */

	*p++ = cpu_to_be32(da->version);
	*p++ = cpu_to_be32(da->minor_version);
	*p++ = cpu_to_be32(da->rsize);
	*p++ = cpu_to_be32(da->wsize);
	*p++ = cpu_to_be32(da->tightly_coupled);

	return 0;
}
