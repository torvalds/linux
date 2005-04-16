/* types.h: Rx types
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_TYPES_H
#define _LINUX_RXRPC_TYPES_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

typedef uint32_t	rxrpc_seq_t;	/* Rx message sequence number */
typedef uint32_t	rxrpc_serial_t;	/* Rx message serial number */
typedef __be32	rxrpc_seq_net_t; /* on-the-wire Rx message sequence number */
typedef __be32	rxrpc_serial_net_t; /* on-the-wire Rx message serial number */

struct rxrpc_call;
struct rxrpc_connection;
struct rxrpc_header;
struct rxrpc_message;
struct rxrpc_operation;
struct rxrpc_peer;
struct rxrpc_service;
typedef struct rxrpc_timer rxrpc_timer_t;
struct rxrpc_transport;

typedef void (*rxrpc_call_attn_func_t)(struct rxrpc_call *call);
typedef void (*rxrpc_call_error_func_t)(struct rxrpc_call *call);
typedef void (*rxrpc_call_aemap_func_t)(struct rxrpc_call *call);

#endif /* _LINUX_RXRPC_TYPES_H */
