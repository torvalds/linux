// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "smbdirect_internal.h"

__maybe_unused /* this is temporary while this file is included in others */
static struct smbdirect_recv_io *smbdirect_connection_get_recv_io(struct smbdirect_socket *sc)
{
	struct smbdirect_recv_io *msg = NULL;
	unsigned long flags;

	spin_lock_irqsave(&sc->recv_io.free.lock, flags);
	if (likely(!sc->first_error))
		msg = list_first_entry_or_null(&sc->recv_io.free.list,
					       struct smbdirect_recv_io,
					       list);
	if (likely(msg)) {
		list_del(&msg->list);
		sc->statistics.get_receive_buffer++;
	}
	spin_unlock_irqrestore(&sc->recv_io.free.lock, flags);

	return msg;
}

__maybe_unused /* this is temporary while this file is included in others */
static void smbdirect_connection_put_recv_io(struct smbdirect_recv_io *msg)
{
	struct smbdirect_socket *sc = msg->socket;
	unsigned long flags;

	if (likely(msg->sge.length != 0)) {
		ib_dma_unmap_single(sc->ib.dev,
				    msg->sge.addr,
				    msg->sge.length,
				    DMA_FROM_DEVICE);
		msg->sge.length = 0;
	}

	spin_lock_irqsave(&sc->recv_io.free.lock, flags);
	list_add_tail(&msg->list, &sc->recv_io.free.list);
	sc->statistics.put_receive_buffer++;
	spin_unlock_irqrestore(&sc->recv_io.free.lock, flags);

	queue_work(sc->workqueue, &sc->recv_io.posted.refill_work);
}
