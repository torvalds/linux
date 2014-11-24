/*
 * Copyright (c) 2014 Christoph Hellwig.
 */
#include <linux/sunrpc/svc.h>
#include <linux/blkdev.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_xdr.h>

#include "blocklayout.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS_LD

static void
bl_free_device(struct pnfs_block_dev *dev)
{
	if (dev->nr_children) {
		int i;

		for (i = 0; i < dev->nr_children; i++)
			bl_free_device(&dev->children[i]);
		kfree(dev->children);
	} else {
		if (dev->bdev)
			blkdev_put(dev->bdev, FMODE_READ);
	}
}

void
bl_free_deviceid_node(struct nfs4_deviceid_node *d)
{
	struct pnfs_block_dev *dev =
		container_of(d, struct pnfs_block_dev, node);

	bl_free_device(dev);
	kfree(dev);
}

static int
nfs4_block_decode_volume(struct xdr_stream *xdr, struct pnfs_block_volume *b)
{
	__be32 *p;
	int i;

	p = xdr_inline_decode(xdr, 4);
	if (!p)
		return -EIO;
	b->type = be32_to_cpup(p++);

	switch (b->type) {
	case PNFS_BLOCK_VOLUME_SIMPLE:
		p = xdr_inline_decode(xdr, 4);
		if (!p)
			return -EIO;
		b->simple.nr_sigs = be32_to_cpup(p++);
		if (!b->simple.nr_sigs) {
			dprintk("no signature\n");
			return -EIO;
		}

		b->simple.len = 4 + 4;
		for (i = 0; i < b->simple.nr_sigs; i++) {
			p = xdr_inline_decode(xdr, 8 + 4);
			if (!p)
				return -EIO;
			p = xdr_decode_hyper(p, &b->simple.sigs[i].offset);
			b->simple.sigs[i].sig_len = be32_to_cpup(p++);

			p = xdr_inline_decode(xdr, b->simple.sigs[i].sig_len);
			if (!p)
				return -EIO;
			memcpy(&b->simple.sigs[i].sig, p,
				b->simple.sigs[i].sig_len);

			b->simple.len += 8 + 4 + b->simple.sigs[i].sig_len;
		}
		break;
	case PNFS_BLOCK_VOLUME_SLICE:
		p = xdr_inline_decode(xdr, 8 + 8 + 4);
		if (!p)
			return -EIO;
		p = xdr_decode_hyper(p, &b->slice.start);
		p = xdr_decode_hyper(p, &b->slice.len);
		b->slice.volume = be32_to_cpup(p++);
		break;
	case PNFS_BLOCK_VOLUME_CONCAT:
		p = xdr_inline_decode(xdr, 4);
		if (!p)
			return -EIO;
		b->concat.volumes_count = be32_to_cpup(p++);

		p = xdr_inline_decode(xdr, b->concat.volumes_count * 4);
		if (!p)
			return -EIO;
		for (i = 0; i < b->concat.volumes_count; i++)
			b->concat.volumes[i] = be32_to_cpup(p++);
		break;
	case PNFS_BLOCK_VOLUME_STRIPE:
		p = xdr_inline_decode(xdr, 8 + 4);
		if (!p)
			return -EIO;
		p = xdr_decode_hyper(p, &b->stripe.chunk_size);
		b->stripe.volumes_count = be32_to_cpup(p++);

		p = xdr_inline_decode(xdr, b->stripe.volumes_count * 4);
		if (!p)
			return -EIO;
		for (i = 0; i < b->stripe.volumes_count; i++)
			b->stripe.volumes[i] = be32_to_cpup(p++);
		break;
	default:
		dprintk("unknown volume type!\n");
		return -EIO;
	}

	return 0;
}

static bool bl_map_simple(struct pnfs_block_dev *dev, u64 offset,
		struct pnfs_block_dev_map *map)
{
	map->start = dev->start;
	map->len = dev->len;
	map->disk_offset = dev->disk_offset;
	map->bdev = dev->bdev;
	return true;
}

static bool bl_map_concat(struct pnfs_block_dev *dev, u64 offset,
		struct pnfs_block_dev_map *map)
{
	int i;

	for (i = 0; i < dev->nr_children; i++) {
		struct pnfs_block_dev *child = &dev->children[i];

		if (child->start > offset ||
		    child->start + child->len <= offset)
			continue;

		child->map(child, offset - child->start, map);
		return true;
	}

	dprintk("%s: ran off loop!\n", __func__);
	return false;
}

static bool bl_map_stripe(struct pnfs_block_dev *dev, u64 offset,
		struct pnfs_block_dev_map *map)
{
	struct pnfs_block_dev *child;
	u64 chunk;
	u32 chunk_idx;
	u64 disk_offset;

	chunk = div_u64(offset, dev->chunk_size);
	div_u64_rem(chunk, dev->nr_children, &chunk_idx);

	if (chunk_idx > dev->nr_children) {
		dprintk("%s: invalid chunk idx %d (%lld/%lld)\n",
			__func__, chunk_idx, offset, dev->chunk_size);
		/* error, should not happen */
		return false;
	}

	/* truncate offset to the beginning of the stripe */
	offset = chunk * dev->chunk_size;

	/* disk offset of the stripe */
	disk_offset = div_u64(offset, dev->nr_children);

	child = &dev->children[chunk_idx];
	child->map(child, disk_offset, map);

	map->start += offset;
	map->disk_offset += disk_offset;
	map->len = dev->chunk_size;
	return true;
}

static int
bl_parse_deviceid(struct nfs_server *server, struct pnfs_block_dev *d,
		struct pnfs_block_volume *volumes, int idx, gfp_t gfp_mask);


static int
bl_parse_simple(struct nfs_server *server, struct pnfs_block_dev *d,
		struct pnfs_block_volume *volumes, int idx, gfp_t gfp_mask)
{
	struct pnfs_block_volume *v = &volumes[idx];
	dev_t dev;

	dev = bl_resolve_deviceid(server, v, gfp_mask);
	if (!dev)
		return -EIO;

	d->bdev = blkdev_get_by_dev(dev, FMODE_READ, NULL);
	if (IS_ERR(d->bdev)) {
		printk(KERN_WARNING "pNFS: failed to open device %d:%d (%ld)\n",
			MAJOR(dev), MINOR(dev), PTR_ERR(d->bdev));
		return PTR_ERR(d->bdev);
	}


	d->len = i_size_read(d->bdev->bd_inode);
	d->map = bl_map_simple;

	printk(KERN_INFO "pNFS: using block device %s\n",
		d->bdev->bd_disk->disk_name);
	return 0;
}

static int
bl_parse_slice(struct nfs_server *server, struct pnfs_block_dev *d,
		struct pnfs_block_volume *volumes, int idx, gfp_t gfp_mask)
{
	struct pnfs_block_volume *v = &volumes[idx];
	int ret;

	ret = bl_parse_deviceid(server, d, volumes, v->slice.volume, gfp_mask);
	if (ret)
		return ret;

	d->disk_offset = v->slice.start;
	d->len = v->slice.len;
	return 0;
}

static int
bl_parse_concat(struct nfs_server *server, struct pnfs_block_dev *d,
		struct pnfs_block_volume *volumes, int idx, gfp_t gfp_mask)
{
	struct pnfs_block_volume *v = &volumes[idx];
	u64 len = 0;
	int ret, i;

	d->children = kcalloc(v->concat.volumes_count,
			sizeof(struct pnfs_block_dev), GFP_KERNEL);
	if (!d->children)
		return -ENOMEM;

	for (i = 0; i < v->concat.volumes_count; i++) {
		ret = bl_parse_deviceid(server, &d->children[i],
				volumes, v->concat.volumes[i], gfp_mask);
		if (ret)
			return ret;

		d->nr_children++;
		d->children[i].start += len;
		len += d->children[i].len;
	}

	d->len = len;
	d->map = bl_map_concat;
	return 0;
}

static int
bl_parse_stripe(struct nfs_server *server, struct pnfs_block_dev *d,
		struct pnfs_block_volume *volumes, int idx, gfp_t gfp_mask)
{
	struct pnfs_block_volume *v = &volumes[idx];
	u64 len = 0;
	int ret, i;

	d->children = kcalloc(v->stripe.volumes_count,
			sizeof(struct pnfs_block_dev), GFP_KERNEL);
	if (!d->children)
		return -ENOMEM;

	for (i = 0; i < v->stripe.volumes_count; i++) {
		ret = bl_parse_deviceid(server, &d->children[i],
				volumes, v->stripe.volumes[i], gfp_mask);
		if (ret)
			return ret;

		d->nr_children++;
		len += d->children[i].len;
	}

	d->len = len;
	d->chunk_size = v->stripe.chunk_size;
	d->map = bl_map_stripe;
	return 0;
}

static int
bl_parse_deviceid(struct nfs_server *server, struct pnfs_block_dev *d,
		struct pnfs_block_volume *volumes, int idx, gfp_t gfp_mask)
{
	switch (volumes[idx].type) {
	case PNFS_BLOCK_VOLUME_SIMPLE:
		return bl_parse_simple(server, d, volumes, idx, gfp_mask);
	case PNFS_BLOCK_VOLUME_SLICE:
		return bl_parse_slice(server, d, volumes, idx, gfp_mask);
	case PNFS_BLOCK_VOLUME_CONCAT:
		return bl_parse_concat(server, d, volumes, idx, gfp_mask);
	case PNFS_BLOCK_VOLUME_STRIPE:
		return bl_parse_stripe(server, d, volumes, idx, gfp_mask);
	default:
		dprintk("unsupported volume type: %d\n", volumes[idx].type);
		return -EIO;
	}
}

struct nfs4_deviceid_node *
bl_alloc_deviceid_node(struct nfs_server *server, struct pnfs_device *pdev,
		gfp_t gfp_mask)
{
	struct nfs4_deviceid_node *node = NULL;
	struct pnfs_block_volume *volumes;
	struct pnfs_block_dev *top;
	struct xdr_stream xdr;
	struct xdr_buf buf;
	struct page *scratch;
	int nr_volumes, ret, i;
	__be32 *p;

	scratch = alloc_page(gfp_mask);
	if (!scratch)
		goto out;

	xdr_init_decode_pages(&xdr, &buf, pdev->pages, pdev->pglen);
	xdr_set_scratch_buffer(&xdr, page_address(scratch), PAGE_SIZE);

	p = xdr_inline_decode(&xdr, sizeof(__be32));
	if (!p)
		goto out_free_scratch;
	nr_volumes = be32_to_cpup(p++);

	volumes = kcalloc(nr_volumes, sizeof(struct pnfs_block_volume),
			  gfp_mask);
	if (!volumes)
		goto out_free_scratch;

	for (i = 0; i < nr_volumes; i++) {
		ret = nfs4_block_decode_volume(&xdr, &volumes[i]);
		if (ret < 0)
			goto out_free_volumes;
	}

	top = kzalloc(sizeof(*top), gfp_mask);
	if (!top)
		goto out_free_volumes;

	ret = bl_parse_deviceid(server, top, volumes, nr_volumes - 1, gfp_mask);
	if (ret) {
		bl_free_device(top);
		kfree(top);
		goto out_free_volumes;
	}

	node = &top->node;
	nfs4_init_deviceid_node(node, server, &pdev->dev_id);

out_free_volumes:
	kfree(volumes);
out_free_scratch:
	__free_page(scratch);
out:
	return node;
}
