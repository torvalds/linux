/*-
 * Copyright (c) 2013-2017, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <dev/mlx5/driver.h>

#include "mlx5_core.h"

/* Handling for queue buffers -- we allocate a bunch of memory and
 * register it in a memory region at HCA virtual address 0.  If the
 * requested size is > max_direct, we split the allocation into
 * multiple pages, so we don't require too much contiguous memory.
 */

static void
mlx5_buf_load_mem_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct mlx5_buf *buf;
	uint8_t owned;
	int x;

	buf = (struct mlx5_buf *)arg;
	owned = MLX5_DMA_OWNED(buf->dev);

	if (!owned)
		MLX5_DMA_LOCK(buf->dev);

	if (error == 0) {
		for (x = 0; x != nseg; x++) {
			buf->page_list[x] = segs[x].ds_addr;
			KASSERT(segs[x].ds_len == PAGE_SIZE, ("Invalid segment size"));
		}
		buf->load_done = MLX5_LOAD_ST_SUCCESS;
	} else {
		buf->load_done = MLX5_LOAD_ST_FAILURE;
	}
	MLX5_DMA_DONE(buf->dev);

	if (!owned)
		MLX5_DMA_UNLOCK(buf->dev);
}

int
mlx5_buf_alloc(struct mlx5_core_dev *dev, int size,
    int max_direct, struct mlx5_buf *buf)
{
	int err;

	buf->npages = howmany(size, PAGE_SIZE);
	buf->page_shift = PAGE_SHIFT;
	buf->load_done = MLX5_LOAD_ST_NONE;
	buf->dev = dev;
	buf->page_list = kcalloc(buf->npages, sizeof(*buf->page_list),
	    GFP_KERNEL);

	err = -bus_dma_tag_create(
	    bus_get_dma_tag(dev->pdev->dev.bsddev),
	    PAGE_SIZE,		/* alignment */
	    0,			/* no boundary */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    NULL, NULL,		/* filter, filterarg */
	    PAGE_SIZE * buf->npages,	/* maxsize */
	    buf->npages,	/* nsegments */
	    PAGE_SIZE,		/* maxsegsize */
	    0,			/* flags */
	    NULL, NULL,		/* lockfunc, lockfuncarg */
	    &buf->dma_tag);

	if (err != 0)
		goto err_dma_tag;

	/* allocate memory */
	err = -bus_dmamem_alloc(buf->dma_tag, &buf->direct.buf,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT, &buf->dma_map);
	if (err != 0)
		goto err_dma_alloc;

	/* load memory into DMA */
	MLX5_DMA_LOCK(dev);
	err = bus_dmamap_load(
	    buf->dma_tag, buf->dma_map, buf->direct.buf,
	    PAGE_SIZE * buf->npages, &mlx5_buf_load_mem_cb,
	    buf, BUS_DMA_WAITOK | BUS_DMA_COHERENT);

	while (buf->load_done == MLX5_LOAD_ST_NONE)
		MLX5_DMA_WAIT(dev);
	MLX5_DMA_UNLOCK(dev);

	/* check for error */
	if (buf->load_done != MLX5_LOAD_ST_SUCCESS) {
		err = -ENOMEM;
		goto err_dma_load;
	}

	/* clean memory */
	memset(buf->direct.buf, 0, PAGE_SIZE * buf->npages);

	/* flush memory to RAM */
	bus_dmamap_sync(buf->dev->cmd.dma_tag, buf->dma_map, BUS_DMASYNC_PREWRITE);
	return (0);

err_dma_load:
	bus_dmamem_free(buf->dma_tag, buf->direct.buf, buf->dma_map);
err_dma_alloc:
	bus_dma_tag_destroy(buf->dma_tag);
err_dma_tag:
	kfree(buf->page_list);
	return (err);
}

void mlx5_buf_free(struct mlx5_core_dev *dev, struct mlx5_buf *buf)
{

	bus_dmamap_unload(buf->dma_tag, buf->dma_map);
	bus_dmamem_free(buf->dma_tag, buf->direct.buf, buf->dma_map);
	bus_dma_tag_destroy(buf->dma_tag);
	kfree(buf->page_list);
}
EXPORT_SYMBOL_GPL(mlx5_buf_free);

static struct mlx5_db_pgdir *mlx5_alloc_db_pgdir(struct mlx5_core_dev *dev,
						 int node)
{
	struct mlx5_db_pgdir *pgdir;

	pgdir = kzalloc(sizeof(*pgdir), GFP_KERNEL);

	bitmap_fill(pgdir->bitmap, MLX5_DB_PER_PAGE);

	pgdir->fw_page = mlx5_fwp_alloc(dev, GFP_KERNEL, 1);
	if (pgdir->fw_page != NULL) {
		pgdir->db_page = pgdir->fw_page->virt_addr;
		pgdir->db_dma = pgdir->fw_page->dma_addr;

		/* clean allocated memory */
		memset(pgdir->db_page, 0, MLX5_ADAPTER_PAGE_SIZE);

		/* flush memory to RAM */
		mlx5_fwp_flush(pgdir->fw_page);
	}
	if (!pgdir->db_page) {
		kfree(pgdir);
		return NULL;
	}

	return pgdir;
}

static int mlx5_alloc_db_from_pgdir(struct mlx5_db_pgdir *pgdir,
				    struct mlx5_db *db)
{
	int offset;
	int i;

	i = find_first_bit(pgdir->bitmap, MLX5_DB_PER_PAGE);
	if (i >= MLX5_DB_PER_PAGE)
		return -ENOMEM;

	__clear_bit(i, pgdir->bitmap);

	db->u.pgdir = pgdir;
	db->index   = i;
	offset = db->index * L1_CACHE_BYTES;
	db->db      = pgdir->db_page + offset / sizeof(*pgdir->db_page);
	db->dma     = pgdir->db_dma  + offset;

	db->db[0] = 0;
	db->db[1] = 0;

	return 0;
}

int mlx5_db_alloc_node(struct mlx5_core_dev *dev, struct mlx5_db *db, int node)
{
	struct mlx5_db_pgdir *pgdir;
	int ret = 0;

	mutex_lock(&dev->priv.pgdir_mutex);

	list_for_each_entry(pgdir, &dev->priv.pgdir_list, list)
		if (!mlx5_alloc_db_from_pgdir(pgdir, db))
			goto out;

	pgdir = mlx5_alloc_db_pgdir(dev, node);
	if (!pgdir) {
		ret = -ENOMEM;
		goto out;
	}

	list_add(&pgdir->list, &dev->priv.pgdir_list);

	/* This should never fail -- we just allocated an empty page: */
	WARN_ON(mlx5_alloc_db_from_pgdir(pgdir, db));

out:
	mutex_unlock(&dev->priv.pgdir_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mlx5_db_alloc_node);

int mlx5_db_alloc(struct mlx5_core_dev *dev, struct mlx5_db *db)
{
	return mlx5_db_alloc_node(dev, db, dev->priv.numa_node);
}
EXPORT_SYMBOL_GPL(mlx5_db_alloc);

void mlx5_db_free(struct mlx5_core_dev *dev, struct mlx5_db *db)
{
	mutex_lock(&dev->priv.pgdir_mutex);

	__set_bit(db->index, db->u.pgdir->bitmap);

	if (bitmap_full(db->u.pgdir->bitmap, MLX5_DB_PER_PAGE)) {
		mlx5_fwp_free(db->u.pgdir->fw_page);
		list_del(&db->u.pgdir->list);
		kfree(db->u.pgdir);
	}

	mutex_unlock(&dev->priv.pgdir_mutex);
}
EXPORT_SYMBOL_GPL(mlx5_db_free);

void
mlx5_fill_page_array(struct mlx5_buf *buf, __be64 *pas)
{
	int i;

	for (i = 0; i != buf->npages; i++)
		pas[i] = cpu_to_be64(buf->page_list[i]);
}
EXPORT_SYMBOL_GPL(mlx5_fill_page_array);
