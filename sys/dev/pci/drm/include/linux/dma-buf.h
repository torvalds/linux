/*	$OpenBSD: dma-buf.h,v 1.5 2025/02/07 03:03:31 jsg Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_DMA_BUF_H
#define _LINUX_DMA_BUF_H

#include <sys/types.h>
#include <sys/systm.h>
#include <linux/dma-resv.h>
#include <linux/list.h>
#include <linux/file.h>

struct dma_buf_ops;
struct device;

struct dma_buf {
	const struct dma_buf_ops *ops;
	void *priv;
	size_t size;
	struct file *file;
	struct list_head attachments;
	struct dma_resv *resv;
};

struct dma_buf_attachment {
	struct dma_buf *dmabuf;
	void *importer_priv;
};

struct dma_buf_attach_ops {
	void (*move_notify)(struct dma_buf_attachment *);
	bool allow_peer2peer;
};

void	get_dma_buf(struct dma_buf *);
struct dma_buf *dma_buf_get(int);
void	dma_buf_put(struct dma_buf *);
int	dma_buf_fd(struct dma_buf *, int);

struct dma_buf_ops {
	void (*release)(struct dma_buf *);
};

struct dma_buf_export_info {
	const struct dma_buf_ops *ops;
	size_t size;
	int flags;
	void *priv;
	struct dma_resv *resv;
};

#define DEFINE_DMA_BUF_EXPORT_INFO(x)  struct dma_buf_export_info x 

struct dma_buf *dma_buf_export(const struct dma_buf_export_info *);

static inline struct dma_buf_attachment *
dma_buf_attach(struct dma_buf *buf, struct device *dev)
{
	return NULL;
}

static inline void
dma_buf_detach(struct dma_buf *buf, struct dma_buf_attachment *dba)
{
	panic("dma_buf_detach");
}

static inline bool
dma_buf_is_dynamic(struct dma_buf *buf)
{
	return false;
}
#endif
