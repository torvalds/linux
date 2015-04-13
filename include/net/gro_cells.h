#ifndef _NET_GRO_CELLS_H
#define _NET_GRO_CELLS_H

#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/netdevice.h>

struct gro_cell {
	struct sk_buff_head	napi_skbs;
	struct napi_struct	napi;
};

struct gro_cells {
	struct gro_cell __percpu	*cells;
};

static inline void gro_cells_receive(struct gro_cells *gcells, struct sk_buff *skb)
{
	struct gro_cell *cell;
	struct net_device *dev = skb->dev;

	if (!gcells->cells || skb_cloned(skb) || !(dev->features & NETIF_F_GRO)) {
		netif_rx(skb);
		return;
	}

	cell = this_cpu_ptr(gcells->cells);

	if (skb_queue_len(&cell->napi_skbs) > netdev_max_backlog) {
		atomic_long_inc(&dev->rx_dropped);
		kfree_skb(skb);
		return;
	}

	/* We run in BH context */
	spin_lock(&cell->napi_skbs.lock);

	__skb_queue_tail(&cell->napi_skbs, skb);
	if (skb_queue_len(&cell->napi_skbs) == 1)
		napi_schedule(&cell->napi);

	spin_unlock(&cell->napi_skbs.lock);
}

/* called unser BH context */
static inline int gro_cell_poll(struct napi_struct *napi, int budget)
{
	struct gro_cell *cell = container_of(napi, struct gro_cell, napi);
	struct sk_buff *skb;
	int work_done = 0;

	spin_lock(&cell->napi_skbs.lock);
	while (work_done < budget) {
		skb = __skb_dequeue(&cell->napi_skbs);
		if (!skb)
			break;
		spin_unlock(&cell->napi_skbs.lock);
		napi_gro_receive(napi, skb);
		work_done++;
		spin_lock(&cell->napi_skbs.lock);
	}

	if (work_done < budget)
		napi_complete(napi);
	spin_unlock(&cell->napi_skbs.lock);
	return work_done;
}

static inline int gro_cells_init(struct gro_cells *gcells, struct net_device *dev)
{
	int i;

	gcells->cells = alloc_percpu(struct gro_cell);
	if (!gcells->cells)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		struct gro_cell *cell = per_cpu_ptr(gcells->cells, i);

		skb_queue_head_init(&cell->napi_skbs);
		netif_napi_add(dev, &cell->napi, gro_cell_poll, 64);
		napi_enable(&cell->napi);
	}
	return 0;
}

static inline void gro_cells_destroy(struct gro_cells *gcells)
{
	int i;

	if (!gcells->cells)
		return;
	for_each_possible_cpu(i) {
		struct gro_cell *cell = per_cpu_ptr(gcells->cells, i);
		netif_napi_del(&cell->napi);
		skb_queue_purge(&cell->napi_skbs);
	}
	free_percpu(gcells->cells);
	gcells->cells = NULL;
}

#endif
