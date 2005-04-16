/* krxtimod.h: RxRPC timeout daemon
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_KRXTIMOD_H
#define _LINUX_RXRPC_KRXTIMOD_H

#include <rxrpc/types.h>

struct rxrpc_timer_ops {
	/* called when the front of the timer queue has timed out */
	void (*timed_out)(struct rxrpc_timer *timer);
};

/*****************************************************************************/
/*
 * RXRPC timer/timeout record
 */
struct rxrpc_timer
{
	struct list_head		link;		/* link in timer queue */
	unsigned long			timo_jif;	/* timeout time */
	const struct rxrpc_timer_ops	*ops;		/* timeout expiry function */
};

static inline void rxrpc_timer_init(rxrpc_timer_t *timer, const struct rxrpc_timer_ops *ops)
{
	INIT_LIST_HEAD(&timer->link);
	timer->ops = ops;
}

extern int rxrpc_krxtimod_start(void);
extern void rxrpc_krxtimod_kill(void);

extern void rxrpc_krxtimod_add_timer(rxrpc_timer_t *timer, unsigned long timeout);
extern int rxrpc_krxtimod_del_timer(rxrpc_timer_t *timer);

#endif /* _LINUX_RXRPC_KRXTIMOD_H */
