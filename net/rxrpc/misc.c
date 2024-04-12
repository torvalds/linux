// SPDX-License-Identifier: GPL-2.0-or-later
/* Miscellaneous bits
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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
 * How long to wait before scheduling an ACK with subtype DELAY (in ms).
 *
 * We use this when we've received new data packets.  If those packets aren't
 * all consumed within this time we will send a DELAY ACK if an ACK was not
 * requested to let the sender know it doesn't need to resend.
 */
unsigned long rxrpc_soft_ack_delay = 1000;

/*
 * How long to wait before scheduling an ACK with subtype IDLE (in ms).
 *
 * We use this when we've consumed some previously soft-ACK'd packets when
 * further packets aren't immediately received to decide when to send an IDLE
 * ACK let the other end know that it can free up its Tx buffer space.
 */
unsigned long rxrpc_idle_ack_delay = 500;

/*
 * Receive window size in packets.  This indicates the maximum number of
 * unconsumed received packets we're willing to retain in memory.  Once this
 * limit is hit, we should generate an EXCEEDS_WINDOW ACK and discard further
 * packets.
 */
unsigned int rxrpc_rx_window_size = 255;

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

#ifdef CONFIG_AF_RXRPC_INJECT_RX_DELAY
/*
 * The delay to inject into packet reception.
 */
unsigned long rxrpc_inject_rx_delay;
#endif
