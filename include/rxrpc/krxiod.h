/* krxiod.h: Rx RPC I/O kernel thread interface
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_KRXIOD_H
#define _LINUX_RXRPC_KRXIOD_H

#include <rxrpc/types.h>

extern int rxrpc_krxiod_init(void);
extern void rxrpc_krxiod_kill(void);
extern void rxrpc_krxiod_queue_transport(struct rxrpc_transport *trans);
extern void rxrpc_krxiod_dequeue_transport(struct rxrpc_transport *trans);
extern void rxrpc_krxiod_queue_peer(struct rxrpc_peer *peer);
extern void rxrpc_krxiod_dequeue_peer(struct rxrpc_peer *peer);
extern void rxrpc_krxiod_clear_peers(struct rxrpc_transport *trans);
extern void rxrpc_krxiod_queue_call(struct rxrpc_call *call);
extern void rxrpc_krxiod_dequeue_call(struct rxrpc_call *call);

#endif /* _LINUX_RXRPC_KRXIOD_H */
