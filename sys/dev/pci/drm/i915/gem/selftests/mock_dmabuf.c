/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#include <linux/vmalloc.h>
#include "mock_dmabuf.h"

static struct sg_table *mock_map_dma_buf(struct dma_buf_attachment *attachment,
					 enum dma_data_direction dir)
{
	struct mock_dmabuf *mock = to_mock(attachment->dmabuf);
	struct sg_table *st;
	struct scatterlist *sg;
	int i, err;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return ERR_PTR(-ENOMEM);

	err = sg_alloc_table(st, mock->npages, GFP_KERNEL);
	if (err)
		goto err_free;

	sg = st->sgl;
	for (i = 0; i < mock->npages; i++) {
		sg_set_page(sg, mock->pages[i], PAGE_SIZE, 0);
		sg = sg_next(sg);
	}

	err = dma_map_sgtable(attachment->dev, st, dir, 0);
	if (err)
		goto err_st;

	return st;

err_st:
	sg_free_table(st);
err_free:
	kfree(st);
	return ERR_PTR(err);
}

static void mock_unmap_dma_buf(struct dma_buf_attachment *attachment,
			       struct sg_table *st,
			       enum dma_data_direction dir)
{
	dma_unmap_sgtable(attachment->dev, st, dir, 0);
	sg_free_table(st);
	kfree(st);
}

static void mock_dmabuf_release(struct dma_buf *dma_buf)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);
	int i;

	for (i = 0; i < mock->npages; i++)
		put_page(mock->pages[i]);

	kfree(mock);
}

static int mock_dmabuf_vmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);
	void *vaddr;

	vaddr = vm_map_ram(mock->pages, mock->npages, 0);
	if (!vaddr)
		return -ENOMEM;
	iosys_map_set_vaddr(map, vaddr);

	return 0;
}

static void mock_dmabuf_vunmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct mock_dmabuf *mock = to_mock(dma_buf);

	vm_unmap_ram(map->vaddr, mock->npages);
}

static int mock_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	return -ENODEV;
}

static const struct dma_buf_ops mock_dmabuf_ops =  {
	.map_dma_buf = mock_map_dma_buf,
	.unmap_dma_buf = mock_unmap_dma_buf,
	.release = mock_dmabuf_release,
	.mmap = mock_dmabuf_mmap,
	.vmap = mock_dmabuf_vmap,
	.vunmap = mock_dmabuf_vunmap,
};

static struct dma_buf *mock_dmabuf(int npages)
{
	struct mock_dmabuf *mock;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int i;

	mock = kmalloc(sizeof(*mock) + npages * sizeof(struct vm_page *),
		       GFP_KERNEL);
	if (!mock)
		return ERR_PTR(-ENOMEM);

	mock->npages = npages;
	for (i = 0; i < npages; i++) {
		mock->pages[i] = alloc_page(GFP_KERNEL);
		if (!mock->pages[i])
			goto err;
	}

	exp_info.ops = &mock_dmabuf_ops;
	exp_info.size = npages * PAGE_SIZE;
	exp_info.flags = O_CLOEXEC;
	exp_info.priv = mock;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf))
		goto err;

	return dmabuf;

err:
	while (i--)
		put_page(mock->pages[i]);
	kfree(mock);
	return ERR_PTR(-ENOMEM);
}
