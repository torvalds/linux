/* skb_dma_map.c: DMA mapping helpers for socket buffers.
 *
 * Copyright (C) David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/skbuff.h>

int skb_dma_map(struct device *dev, struct sk_buff *skb,
		enum dma_data_direction dir)
{
	struct skb_shared_info *sp = skb_shinfo(skb);
	dma_addr_t map;
	int i;

	map = dma_map_single(dev, skb->data,
			     skb_headlen(skb), dir);
	if (dma_mapping_error(dev, map))
		goto out_err;

	sp->dma_head = map;
	for (i = 0; i < sp->nr_frags; i++) {
		skb_frag_t *fp = &sp->frags[i];

		map = dma_map_page(dev, fp->page, fp->page_offset,
				   fp->size, dir);
		if (dma_mapping_error(dev, map))
			goto unwind;
		sp->dma_maps[i] = map;
	}

	return 0;

unwind:
	while (--i >= 0) {
		skb_frag_t *fp = &sp->frags[i];

		dma_unmap_page(dev, sp->dma_maps[i],
			       fp->size, dir);
	}
	dma_unmap_single(dev, sp->dma_head,
			 skb_headlen(skb), dir);
out_err:
	return -ENOMEM;
}
EXPORT_SYMBOL(skb_dma_map);

void skb_dma_unmap(struct device *dev, struct sk_buff *skb,
		   enum dma_data_direction dir)
{
	struct skb_shared_info *sp = skb_shinfo(skb);
	int i;

	dma_unmap_single(dev, sp->dma_head,
			 skb_headlen(skb), dir);
	for (i = 0; i < sp->nr_frags; i++) {
		skb_frag_t *fp = &sp->frags[i];

		dma_unmap_page(dev, sp->dma_maps[i],
			       fp->size, dir);
	}
}
EXPORT_SYMBOL(skb_dma_unmap);
