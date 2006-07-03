/* rxrpc_syms.c: exported Rx RPC layer interface symbols
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>

#include <rxrpc/transport.h>
#include <rxrpc/connection.h>
#include <rxrpc/call.h>
#include <rxrpc/krxiod.h>

/* call.c */
EXPORT_SYMBOL(rxrpc_create_call);
EXPORT_SYMBOL(rxrpc_put_call);
EXPORT_SYMBOL(rxrpc_call_abort);
EXPORT_SYMBOL(rxrpc_call_read_data);
EXPORT_SYMBOL(rxrpc_call_write_data);

/* connection.c */
EXPORT_SYMBOL(rxrpc_create_connection);
EXPORT_SYMBOL(rxrpc_put_connection);

/* transport.c */
EXPORT_SYMBOL(rxrpc_create_transport);
EXPORT_SYMBOL(rxrpc_put_transport);
EXPORT_SYMBOL(rxrpc_add_service);
EXPORT_SYMBOL(rxrpc_del_service);
