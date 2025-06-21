// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014-2016 Christoph Hellwig.
 */
#include <linux/sunrpc/svc.h>
#include <linux/exportfs.h>
#include <linux/iomap.h>
#include <linux/nfs4.h>

#include "nfsd.h"
#include "blocklayoutxdr.h"
#include "vfs.h"

#define NFSDDBG_FACILITY	NFSDDBG_PNFS


__be32
nfsd4_block_encode_layoutget(struct xdr_stream *xdr,
		const struct nfsd4_layoutget *lgp)
{
	const struct pnfs_block_extent *b = lgp->lg_content;
	int len = sizeof(__be32) + 5 * sizeof(__be64) + sizeof(__be32);
	__be32 *p;

	p = xdr_reserve_space(xdr, sizeof(__be32) + len);
	if (!p)
		return nfserr_toosmall;

	*p++ = cpu_to_be32(len);
	*p++ = cpu_to_be32(1);		/* we always return a single extent */

	p = xdr_encode_opaque_fixed(p, &b->vol_id,
			sizeof(struct nfsd4_deviceid));
	p = xdr_encode_hyper(p, b->foff);
	p = xdr_encode_hyper(p, b->len);
	p = xdr_encode_hyper(p, b->soff);
	*p++ = cpu_to_be32(b->es);
	return 0;
}

static int
nfsd4_block_encode_volume(struct xdr_stream *xdr, struct pnfs_block_volume *b)
{
	__be32 *p;
	int len;

	switch (b->type) {
	case PNFS_BLOCK_VOLUME_SIMPLE:
		len = 4 + 4 + 8 + 4 + (XDR_QUADLEN(b->simple.sig_len) << 2);
		p = xdr_reserve_space(xdr, len);
		if (!p)
			return -ETOOSMALL;

		*p++ = cpu_to_be32(b->type);
		*p++ = cpu_to_be32(1);	/* single signature */
		p = xdr_encode_hyper(p, b->simple.offset);
		p = xdr_encode_opaque(p, b->simple.sig, b->simple.sig_len);
		break;
	case PNFS_BLOCK_VOLUME_SCSI:
		len = 4 + 4 + 4 + 4 + (XDR_QUADLEN(b->scsi.designator_len) << 2) + 8;
		p = xdr_reserve_space(xdr, len);
		if (!p)
			return -ETOOSMALL;

		*p++ = cpu_to_be32(b->type);
		*p++ = cpu_to_be32(b->scsi.code_set);
		*p++ = cpu_to_be32(b->scsi.designator_type);
		p = xdr_encode_opaque(p, b->scsi.designator, b->scsi.designator_len);
		p = xdr_encode_hyper(p, b->scsi.pr_key);
		break;
	default:
		return -ENOTSUPP;
	}

	return len;
}

__be32
nfsd4_block_encode_getdeviceinfo(struct xdr_stream *xdr,
		const struct nfsd4_getdeviceinfo *gdp)
{
	struct pnfs_block_deviceaddr *dev = gdp->gd_device;
	int len = sizeof(__be32), ret, i;
	__be32 *p;

	/*
	 * See paragraph 5 of RFC 8881 S18.40.3.
	 */
	if (!gdp->gd_maxcount) {
		if (xdr_stream_encode_u32(xdr, 0) != XDR_UNIT)
			return nfserr_resource;
		return nfs_ok;
	}

	p = xdr_reserve_space(xdr, len + sizeof(__be32));
	if (!p)
		return nfserr_resource;

	for (i = 0; i < dev->nr_volumes; i++) {
		ret = nfsd4_block_encode_volume(xdr, &dev->volumes[i]);
		if (ret < 0)
			return nfserrno(ret);
		len += ret;
	}

	/*
	 * Fill in the overall length and number of volumes at the beginning
	 * of the layout.
	 */
	*p++ = cpu_to_be32(len);
	*p++ = cpu_to_be32(dev->nr_volumes);
	return 0;
}

/**
 * nfsd4_block_decode_layoutupdate - decode the block layout extent array
 * @p: pointer to the xdr data
 * @len: number of bytes to decode
 * @iomapp: pointer to store the decoded extent array
 * @nr_iomapsp: pointer to store the number of extents
 * @block_size: alignment of extent offset and length
 *
 * This function decodes the opaque field of the layoutupdate4 structure
 * in a layoutcommit request for the block layout driver. The field is
 * actually an array of extents sent by the client. It also checks that
 * the file offset, storage offset and length of each extent are aligned
 * by @block_size.
 *
 * Return values:
 *   %nfs_ok: Successful decoding, @iomapp and @nr_iomapsp are valid
 *   %nfserr_bad_xdr: The encoded array in @p is invalid
 *   %nfserr_inval: An unaligned extent found
 *   %nfserr_delay: Failed to allocate memory for @iomapp
 */
__be32
nfsd4_block_decode_layoutupdate(__be32 *p, u32 len, struct iomap **iomapp,
		int *nr_iomapsp, u32 block_size)
{
	struct iomap *iomaps;
	u32 nr_iomaps, i;

	if (len < sizeof(u32))
		return nfserr_bad_xdr;
	len -= sizeof(u32);
	if (len % PNFS_BLOCK_EXTENT_SIZE)
		return nfserr_bad_xdr;

	nr_iomaps = be32_to_cpup(p++);
	if (nr_iomaps != len / PNFS_BLOCK_EXTENT_SIZE)
		return nfserr_bad_xdr;

	iomaps = kcalloc(nr_iomaps, sizeof(*iomaps), GFP_KERNEL);
	if (!iomaps)
		return nfserr_delay;

	for (i = 0; i < nr_iomaps; i++) {
		struct pnfs_block_extent bex;

		memcpy(&bex.vol_id, p, sizeof(struct nfsd4_deviceid));
		p += XDR_QUADLEN(sizeof(struct nfsd4_deviceid));

		p = xdr_decode_hyper(p, &bex.foff);
		if (bex.foff & (block_size - 1)) {
			goto fail;
		}
		p = xdr_decode_hyper(p, &bex.len);
		if (bex.len & (block_size - 1)) {
			goto fail;
		}
		p = xdr_decode_hyper(p, &bex.soff);
		if (bex.soff & (block_size - 1)) {
			goto fail;
		}
		bex.es = be32_to_cpup(p++);
		if (bex.es != PNFS_BLOCK_READWRITE_DATA) {
			goto fail;
		}

		iomaps[i].offset = bex.foff;
		iomaps[i].length = bex.len;
	}

	*iomapp = iomaps;
	*nr_iomapsp = nr_iomaps;
	return nfs_ok;
fail:
	kfree(iomaps);
	return nfserr_inval;
}

/**
 * nfsd4_scsi_decode_layoutupdate - decode the scsi layout extent array
 * @p: pointer to the xdr data
 * @len: number of bytes to decode
 * @iomapp: pointer to store the decoded extent array
 * @nr_iomapsp: pointer to store the number of extents
 * @block_size: alignment of extent offset and length
 *
 * This function decodes the opaque field of the layoutupdate4 structure
 * in a layoutcommit request for the scsi layout driver. The field is
 * actually an array of extents sent by the client. It also checks that
 * the offset and length of each extent are aligned by @block_size.
 *
 * Return values:
 *   %nfs_ok: Successful decoding, @iomapp and @nr_iomapsp are valid
 *   %nfserr_bad_xdr: The encoded array in @p is invalid
 *   %nfserr_inval: An unaligned extent found
 *   %nfserr_delay: Failed to allocate memory for @iomapp
 */
__be32
nfsd4_scsi_decode_layoutupdate(__be32 *p, u32 len, struct iomap **iomapp,
		int *nr_iomapsp, u32 block_size)
{
	struct iomap *iomaps;
	u32 nr_iomaps, expected, i;

	if (len < sizeof(u32))
		return nfserr_bad_xdr;

	nr_iomaps = be32_to_cpup(p++);
	expected = sizeof(__be32) + nr_iomaps * PNFS_SCSI_RANGE_SIZE;
	if (len != expected)
		return nfserr_bad_xdr;

	iomaps = kcalloc(nr_iomaps, sizeof(*iomaps), GFP_KERNEL);
	if (!iomaps)
		return nfserr_delay;

	for (i = 0; i < nr_iomaps; i++) {
		u64 val;

		p = xdr_decode_hyper(p, &val);
		if (val & (block_size - 1)) {
			goto fail;
		}
		iomaps[i].offset = val;

		p = xdr_decode_hyper(p, &val);
		if (val & (block_size - 1)) {
			goto fail;
		}
		iomaps[i].length = val;
	}

	*iomapp = iomaps;
	*nr_iomapsp = nr_iomaps;
	return nfs_ok;
fail:
	kfree(iomaps);
	return nfserr_inval;
}
