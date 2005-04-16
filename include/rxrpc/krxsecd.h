/* krxsecd.h: Rx RPC security kernel thread interface
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_KRXSECD_H
#define _LINUX_RXRPC_KRXSECD_H

#include <rxrpc/types.h>

extern int rxrpc_krxsecd_init(void);
extern void rxrpc_krxsecd_kill(void);
extern void rxrpc_krxsecd_clear_transport(struct rxrpc_transport *trans);
extern void rxrpc_krxsecd_queue_incoming_call(struct rxrpc_message *msg);

#endif /* _LINUX_RXRPC_KRXSECD_H */
