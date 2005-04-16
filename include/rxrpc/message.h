/* message.h: Rx message caching
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_MESSAGE_H
#define _LINUX_RXRPC_MESSAGE_H

#include <rxrpc/packet.h>

/*****************************************************************************/
/*
 * Rx message record
 */
struct rxrpc_message
{
	atomic_t		usage;
	struct list_head	link;		/* list link */
	struct timeval		stamp;		/* time received or last sent */
	rxrpc_seq_t		seq;		/* message sequence number */

	int			state;		/* the state the message is currently in */
#define RXRPC_MSG_PREPARED	0
#define RXRPC_MSG_SENT		1
#define RXRPC_MSG_ACKED		2		/* provisionally ACK'd */
#define RXRPC_MSG_DONE		3		/* definitively ACK'd (msg->seq<ack.firstPacket) */
#define RXRPC_MSG_RECEIVED	4
#define RXRPC_MSG_ERROR		-1
	char			rttdone;	/* used for RTT */

	struct rxrpc_transport	*trans;		/* transport received through */
	struct rxrpc_connection	*conn;		/* connection received over */
	struct sk_buff		*pkt;		/* received packet */
	off_t			offset;		/* offset into pkt of next byte of data */

	struct rxrpc_header	hdr;		/* message header */

	int			dcount;		/* data part count */
	size_t			dsize;		/* data size */
#define RXRPC_MSG_MAX_IOCS 8
	struct kvec		data[RXRPC_MSG_MAX_IOCS]; /* message data */
	unsigned long		dfree;		/* bit mask indicating kfree(data[x]) if T */
};

#define rxrpc_get_message(M) do { atomic_inc(&(M)->usage); } while(0)

extern void __rxrpc_put_message(struct rxrpc_message *msg);
static inline void rxrpc_put_message(struct rxrpc_message *msg)
{
	BUG_ON(atomic_read(&msg->usage)<=0);
	if (atomic_dec_and_test(&msg->usage))
		__rxrpc_put_message(msg);
}

extern int rxrpc_conn_newmsg(struct rxrpc_connection *conn,
			     struct rxrpc_call *call,
			     uint8_t type,
			     int count,
			     struct kvec *diov,
			     int alloc_flags,
			     struct rxrpc_message **_msg);

extern int rxrpc_conn_sendmsg(struct rxrpc_connection *conn, struct rxrpc_message *msg);

#endif /* _LINUX_RXRPC_MESSAGE_H */
