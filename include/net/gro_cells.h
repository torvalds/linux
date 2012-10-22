#ifndef _NET_GRO_CELLS_H
#define _NET_GRO_CELLS_H

#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

struct gro_cell {
	struct sk_buff_head	napi_skbs;
	struct napi_struct	napi;
} ____cacheline_aligned_in_smp;

struct gro_cells {
	unsigned int		gro_cells_mask;
	struct gro_cell		*cells;
};

static inline void gro_cells_receive(struct gro_cells *gcells, struct sk_buff *skb)
{
	unsigned long flags;
	struct gro_cell *cell = gcells->cells;
	struct net_device *dev = skb->dev;

	if (!cell || skb_cloned(skb) || !(dev->features & NETIF_F_GRO)) {
		netif_rx(skb);
		return;
	}

	if (skb_rx_queue_recorded(skb))
		cell += skb_get_rx_queue(skb) & gcells->gro_cells_mask;

	if (skb_queue_len(&cell->napi_skbs) > netdev_max_backlog) {
		atomic_long_inc(&dev->rx_dropped);
		kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&cell->napi_skbs.lock, flags);

	__skb_queue_tail(&cell->napi_skbs, skb);
	if (skb_queue_len(&cell->napi_skbs) == 1)
		napi_schedule(&cell->napi);

	spin_unlock_irqrestore(&cell->napi_skbs.lock, flags);
}

static inline int gro_cell_poll(struct napi_struct *napi, int budget)
{
	struct gro_cell *cell = container_of(napi, struct gro_cell, napi);
	struct sk_buff *skb;
	int work_done = 0;

	while (work_done < budget) {
		skb = skb_dequeue(&cell->napi_skbs);
		if (!skb)
			break;

		napi_gro_receive(napi, skb);
		work_done++;
	}

	if (work_done < budget)
		napi_complete(napi);
	return work_done;
}

static inline int gro_cells_init(struct gro_cells *gcells, struct net_device *dev)
{
	int i;

	gcells->gro_cells_mask = roundup_pow_of_two(netif_get_num_default_rss_queues()) - 1;
	gcells->cells = kcalloc(sizeof(struct gro_cell),
				gcells->gro_cells_mask + 1,
				GFP_KERNEL);
	if (!gcells->cells)
		return -ENOMEM;

	for (i = 0; i <= gcells->gro_cells_mask; i++) {
		struct gro_cell *cell = gcells->cells + i;

		skb_queue_head_init(&cell->napi_skbs);
		netif_napi_add(dev, &cell->napi, gro_cell_poll, 64);
		napi_enable(&cell->napi);
	}
	return 0;
}

static inline void gro_cells_destroy(struct gro_cells *gcells)
{
	struct gro_cell *cell = gcells->cells;
	int i;

	if (!cell)
		return;
	for (i = 0; i <= gcells->gro_cells_mask; i++,cell++) {
		netif_napi_del(&cell->napi);
		skb_queue_purge(&cell->napi_skbs);
	}
	kfree(gcells->cells);
	gcells->cells = NULL;
}

#endif
