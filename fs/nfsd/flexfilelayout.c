// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Tom Haynes <loghyr@primarydata.com>
 *
 * The following implements a super-simple flex-file server
 * where the NFSv4.1 mds is also the ds. And the storage is
 * the same. I.e., writing to the mds via a NFSv4.1 WRITE
 * goes to the same location as the NFSv3 WRITE.
 */
#include <linux/slab.h>

#include <linux/nfsd/debug.h>

#include <linux/sunrpc/addr.h>

#include "flexfilelayoutxdr.h"
#include "pnfs.h"
#include "vfs.h"

#define NFSDDBG_FACILITY	NFSDDBG_PNFS

static __be32
nfsd4_ff_proc_layoutget(struct inode *inode, const struct svc_fh *fhp,
		struct nfsd4_layoutget *args)
{
	struct nfsd4_layout_seg *seg = &args->lg_seg;
	u32 device_generation = 0;
	int error;
	uid_t u;

	struct pnfs_ff_layout *fl;

	/*
	 * The super simple flex file server has 1 mirror, 1 data server,
	 * and 1 file handle. So instead of 4 allocs, do 1 for now.
	 * Zero it out for the stateid - don't want junk in there!
	 */
	error = -ENOMEM;
	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl)
		goto out_error;
	args->lg_content = fl;

	/*
	 * Avoid layout commit, try to force the I/O to the DS,
	 * and for fun, cause all IOMODE_RW layout segments to
	 * effectively be WRITE only.
	 */
	fl->flags = FF_FLAGS_NO_LAYOUTCOMMIT | FF_FLAGS_NO_IO_THRU_MDS |
		    FF_FLAGS_NO_READ_IO;

	/* Do not allow a IOMODE_READ segment to have write pemissions */
	if (seg->iomode == IOMODE_READ) {
		u = from_kuid(&init_user_ns, inode->i_uid) + 1;
		fl->uid = make_kuid(&init_user_ns, u);
	} else
		fl->uid = inode->i_uid;
	fl->gid = inode->i_gid;

	error = nfsd4_set_deviceid(&fl->deviceid, fhp, device_generation);
	if (error)
		goto out_error;

	fl->fh.size = fhp->fh_handle.fh_size;
	memcpy(fl->fh.data, &fhp->fh_handle.fh_raw, fl->fh.size);

	/* Give whole file layout segments */
	seg->offset = 0;
	seg->length = NFS4_MAX_UINT64;

	dprintk("GET: 0x%llx:0x%llx %d\n", seg->offset, seg->length,
		seg->iomode);
	return 0;

out_error:
	seg->length = 0;
	return nfserrno(error);
}

static __be32
nfsd4_ff_proc_getdeviceinfo(struct super_block *sb, struct svc_rqst *rqstp,
		struct nfs4_client *clp, struct nfsd4_getdeviceinfo *gdp)
{
	struct pnfs_ff_device_addr *da;

	u16 port;
	char addr[INET6_ADDRSTRLEN];

	da = kzalloc(sizeof(struct pnfs_ff_device_addr), GFP_KERNEL);
	if (!da)
		return nfserrno(-ENOMEM);

	gdp->gd_device = da;

	da->version = 3;
	da->minor_version = 0;

	da->rsize = svc_max_payload(rqstp);
	da->wsize = da->rsize;

	rpc_ntop((struct sockaddr *)&rqstp->rq_daddr,
		 addr, INET6_ADDRSTRLEN);
	if (rqstp->rq_daddr.ss_family == AF_INET) {
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)&rqstp->rq_daddr;
		port = ntohs(sin->sin_port);
		snprintf(da->netaddr.netid, FF_NETID_LEN + 1, "tcp");
		da->netaddr.netid_len = 3;
	} else {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)&rqstp->rq_daddr;
		port = ntohs(sin6->sin6_port);
		snprintf(da->netaddr.netid, FF_NETID_LEN + 1, "tcp6");
		da->netaddr.netid_len = 4;
	}

	da->netaddr.addr_len =
		snprintf(da->netaddr.addr, FF_ADDR_LEN + 1,
			 "%s.%d.%d", addr, port >> 8, port & 0xff);

	da->tightly_coupled = false;

	return 0;
}

const struct nfsd4_layout_ops ff_layout_ops = {
	.notify_types		=
			NOTIFY_DEVICEID4_DELETE | NOTIFY_DEVICEID4_CHANGE,
	.disable_recalls	= true,
	.proc_getdeviceinfo	= nfsd4_ff_proc_getdeviceinfo,
	.encode_getdeviceinfo	= nfsd4_ff_encode_getdeviceinfo,
	.proc_layoutget		= nfsd4_ff_proc_layoutget,
	.encode_layoutget	= nfsd4_ff_encode_layoutget,
};
