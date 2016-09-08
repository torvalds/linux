/* Miscellaneous bits
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

/*
 * The maximum listening backlog queue size that may be set on a socket by
 * listen().
 */
unsigned int rxrpc_max_backlog __read_mostly = 10;

/*
 * How long to wait before scheduling ACK generation after seeing a
 * packet with RXRPC_REQUEST_ACK set (in jiffies).
 */
unsigned int rxrpc_requested_ack_delay = 1;

/*
 * How long to wait before scheduling an ACK with subtype DELAY (in jiffies).
 *
 * We use this when we've received new data packets.  If those packets aren't
 * all consumed within this time we will send a DELAY ACK if an ACK was not
 * requested to let the sender know it doesn't need to resend.
 */
unsigned int rxrpc_soft_ack_delay = 1 * HZ;

/*
 * How long to wait before scheduling an ACK with subtype IDLE (in jiffies).
 *
 * We use this when we've consumed some previously soft-ACK'd packets when
 * further packets aren't immediately received to decide when to send an IDLE
 * ACK let the other end know that it can free up its Tx buffer space.
 */
unsigned int rxrpc_idle_ack_delay = 0.5 * HZ;

/*
 * Receive window size in packets.  This indicates the maximum number of
 * unconsumed received packets we're willing to retain in memory.  Once this
 * limit is hit, we should generate an EXCEEDS_WINDOW ACK and discard further
 * packets.
 */
unsigned int rxrpc_rx_window_size = RXRPC_RXTX_BUFF_SIZE - 46;

/*
 * Maximum Rx MTU size.  This indicates to the sender the size of jumbo packet
 * made by gluing normal packets together that we're willing to handle.
 */
unsigned int rxrpc_rx_mtu = 5692;

/*
 * The maximum number of fragments in a received jumbo packet that we tell the
 * sender that we're willing to handle.
 */
unsigned int rxrpc_rx_jumbo_max = 4;

/*
 * Time till packet resend (in jiffies).
 */
unsigned int rxrpc_resend_timeout = 4 * HZ;

const char *const rxrpc_pkts[] = {
	"?00",
	"DATA", "ACK", "BUSY", "ABORT", "ACKALL", "CHALL", "RESP", "DEBUG",
	"?09", "?10", "?11", "?12", "VERSION", "?14", "?15"
};

const s8 rxrpc_ack_priority[] = {
	[0]				= 0,
	[RXRPC_ACK_DELAY]		= 1,
	[RXRPC_ACK_REQUESTED]		= 2,
	[RXRPC_ACK_IDLE]		= 3,
	[RXRPC_ACK_PING_RESPONSE]	= 4,
	[RXRPC_ACK_DUPLICATE]		= 5,
	[RXRPC_ACK_OUT_OF_SEQUENCE]	= 6,
	[RXRPC_ACK_EXCEEDS_WINDOW]	= 7,
	[RXRPC_ACK_NOSPACE]		= 8,
};

const char *rxrpc_acks(u8 reason)
{
	static const char *const str[] = {
		"---", "REQ", "DUP", "OOS", "WIN", "MEM", "PNG", "PNR", "DLY",
		"IDL", "-?-"
	};

	if (reason >= ARRAY_SIZE(str))
		reason = ARRAY_SIZE(str) - 1;
	return str[reason];
}
