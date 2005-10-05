/* call.h: Rx call record
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RXRPC_CALL_H
#define _LINUX_RXRPC_CALL_H

#include <rxrpc/types.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/packet.h>
#include <linux/timer.h>

#define RXRPC_CALL_ACK_WINDOW_SIZE	16

extern unsigned rxrpc_call_rcv_timeout;		/* receive activity timeout (secs) */

/* application call state
 * - only state 0 and ffff are reserved, the state is set to 1 after an opid is received
 */
enum rxrpc_app_cstate {
	RXRPC_CSTATE_COMPLETE		= 0,	/* operation complete */
	RXRPC_CSTATE_ERROR,			/* operation ICMP error or aborted */
	RXRPC_CSTATE_SRVR_RCV_OPID,		/* [SERVER] receiving operation ID */
	RXRPC_CSTATE_SRVR_RCV_ARGS,		/* [SERVER] receiving operation data */
	RXRPC_CSTATE_SRVR_GOT_ARGS,		/* [SERVER] completely received operation data */
	RXRPC_CSTATE_SRVR_SND_REPLY,		/* [SERVER] sending operation reply */
	RXRPC_CSTATE_SRVR_RCV_FINAL_ACK,	/* [SERVER] receiving final ACK */
	RXRPC_CSTATE_CLNT_SND_ARGS,		/* [CLIENT] sending operation args */
	RXRPC_CSTATE_CLNT_RCV_REPLY,		/* [CLIENT] receiving operation reply */
	RXRPC_CSTATE_CLNT_GOT_REPLY,		/* [CLIENT] completely received operation reply */
} __attribute__((packed));

extern const char *rxrpc_call_states[];

enum rxrpc_app_estate {
	RXRPC_ESTATE_NO_ERROR		= 0,	/* no error */
	RXRPC_ESTATE_LOCAL_ABORT,		/* aborted locally by application layer */
	RXRPC_ESTATE_PEER_ABORT,		/* aborted remotely by peer */
	RXRPC_ESTATE_LOCAL_ERROR,		/* local ICMP network error */
	RXRPC_ESTATE_REMOTE_ERROR,		/* remote ICMP network error */
} __attribute__((packed));

extern const char *rxrpc_call_error_states[];

/*****************************************************************************/
/*
 * Rx call record and application scratch buffer
 * - the call record occupies the bottom of a complete page
 * - the application scratch buffer occupies the rest
 */
struct rxrpc_call
{
	atomic_t		usage;
	struct rxrpc_connection	*conn;		/* connection upon which active */
	spinlock_t		lock;		/* access lock */
	struct module		*owner;		/* owner module */
	wait_queue_head_t	waitq;		/* wait queue for events to happen */
	struct list_head	link;		/* general internal list link */
	struct list_head	call_link;	/* master call list link */
	__be32			chan_ix;	/* connection channel index  */
	__be32			call_id;	/* call ID on connection  */
	unsigned long		cjif;		/* jiffies at call creation */
	unsigned long		flags;		/* control flags */
#define RXRPC_CALL_ACKS_TIMO	0x00000001	/* ACKS timeout reached */
#define RXRPC_CALL_ACKR_TIMO	0x00000002	/* ACKR timeout reached */
#define RXRPC_CALL_RCV_TIMO	0x00000004	/* RCV timeout reached */
#define RXRPC_CALL_RCV_PKT	0x00000008	/* received packet */

	/* transmission */
	rxrpc_seq_t		snd_seq_count;	/* outgoing packet sequence number counter */
	struct rxrpc_message	*snd_nextmsg;	/* next message being constructed for sending */
	struct rxrpc_message	*snd_ping;	/* last ping message sent */
	unsigned short		snd_resend_cnt;	/* count of resends since last ACK */

	/* transmission ACK tracking */
	struct list_head	acks_pendq;	/* messages pending ACK (ordered by seq) */
	unsigned		acks_pend_cnt;	/* number of un-ACK'd packets */
	rxrpc_seq_t		acks_dftv_seq;	/* highest definitively ACK'd msg seq */
	struct timer_list	acks_timeout;	/* timeout on expected ACK */

	/* reception */
	struct list_head	rcv_receiveq;	/* messages pending reception (ordered by seq) */
	struct list_head	rcv_krxiodq_lk;	/* krxiod queue for new inbound packets */
	struct timer_list	rcv_timeout;	/* call receive activity timeout */

	/* reception ACK'ing */
	rxrpc_seq_t		ackr_win_bot;	/* bottom of ACK window */
	rxrpc_seq_t		ackr_win_top;	/* top of ACK window */
	rxrpc_seq_t		ackr_high_seq;	/* highest seqno yet received */
	rxrpc_seq_net_t		ackr_prev_seq;	/* previous seqno received */
	unsigned		ackr_pend_cnt;	/* number of pending ACKs */
	struct timer_list	ackr_dfr_timo;	/* timeout on deferred ACK */
	char			ackr_dfr_perm;	/* request for deferred ACKs permitted */
	rxrpc_seq_t		ackr_dfr_seq;	/* seqno for deferred ACK */
	struct rxrpc_ackpacket	ackr;		/* pending normal ACK packet */
	uint8_t			ackr_array[RXRPC_CALL_ACK_WINDOW_SIZE];	/* ACK records */

	/* presentation layer */
	char			app_last_rcv;	/* T if received last packet from remote end */
	enum rxrpc_app_cstate	app_call_state;	/* call state */
	enum rxrpc_app_estate	app_err_state;	/* abort/error state */
	struct list_head	app_readyq;	/* ordered ready received packet queue */
	struct list_head	app_unreadyq;	/* ordered post-hole recv'd packet queue */
	rxrpc_seq_t		app_ready_seq;	/* last seq number dropped into readyq */
	size_t			app_ready_qty;	/* amount of data ready in readyq */
	unsigned		app_opcode;	/* operation ID */
	unsigned		app_abort_code;	/* abort code (when aborted) */
	int			app_errno;	/* error number (when ICMP error received) */

	/* statisics */
	unsigned		pkt_rcv_count;	/* count of received packets on this call */
	unsigned		pkt_snd_count;	/* count of sent packets on this call */
	unsigned		app_read_count;	/* number of reads issued */

	/* bits for the application to use */
	rxrpc_call_attn_func_t	app_attn_func;	/* callback when attention required */
	rxrpc_call_error_func_t	app_error_func;	/* callback when abort sent (cleanup and put) */
	rxrpc_call_aemap_func_t	app_aemap_func;	/* callback to map abort code to/from errno */
	void			*app_user;	/* application data */
	struct list_head	app_link;	/* application list linkage */
	struct list_head	app_attn_link;	/* application attention list linkage */
	size_t			app_mark;	/* trigger callback when app_ready_qty>=app_mark */
	char			app_async_read;	/* T if in async-read mode */
	uint8_t			*app_read_buf;	/* application async read buffer (app_mark size) */
	uint8_t			*app_scr_alloc;	/* application scratch allocation pointer */
	void			*app_scr_ptr;	/* application pointer into scratch buffer */

#define RXRPC_APP_MARK_EOF 0xFFFFFFFFU	/* mark at end of input */

	/* application scratch buffer */
	uint8_t			app_scratch[0] __attribute__((aligned(sizeof(long))));
};

#define RXRPC_CALL_SCRATCH_SIZE (PAGE_SIZE - sizeof(struct rxrpc_call))

#define rxrpc_call_reset_scratch(CALL) \
do { (CALL)->app_scr_alloc = (CALL)->app_scratch; } while(0)

#define rxrpc_call_alloc_scratch(CALL,SIZE)						\
({											\
	void *ptr;									\
	ptr = (CALL)->app_scr_alloc;							\
	(CALL)->app_scr_alloc += (SIZE);						\
	if ((SIZE)>RXRPC_CALL_SCRATCH_SIZE ||						\
	    (size_t)((CALL)->app_scr_alloc - (u8*)(CALL)) > RXRPC_CALL_SCRATCH_SIZE) {	\
		printk("rxrpc_call_alloc_scratch(%p,%Zu)\n",(CALL),(size_t)(SIZE));	\
		BUG();									\
	}										\
	ptr;										\
})

#define rxrpc_call_alloc_scratch_s(CALL,TYPE)						\
({											\
	size_t size = sizeof(TYPE);							\
	TYPE *ptr;									\
	ptr = (TYPE*)(CALL)->app_scr_alloc;						\
	(CALL)->app_scr_alloc += size;							\
	if (size>RXRPC_CALL_SCRATCH_SIZE ||						\
	    (size_t)((CALL)->app_scr_alloc - (u8*)(CALL)) > RXRPC_CALL_SCRATCH_SIZE) {	\
		printk("rxrpc_call_alloc_scratch(%p,%Zu)\n",(CALL),size);		\
		BUG();									\
	}										\
	ptr;										\
})

#define rxrpc_call_is_ack_pending(CALL) ((CALL)->ackr.reason != 0)

extern int rxrpc_create_call(struct rxrpc_connection *conn,
			     rxrpc_call_attn_func_t attn,
			     rxrpc_call_error_func_t error,
			     rxrpc_call_aemap_func_t aemap,
			     struct rxrpc_call **_call);

extern int rxrpc_incoming_call(struct rxrpc_connection *conn,
			       struct rxrpc_message *msg,
			       struct rxrpc_call **_call);

static inline void rxrpc_get_call(struct rxrpc_call *call)
{
	BUG_ON(atomic_read(&call->usage)<=0);
	atomic_inc(&call->usage);
	/*printk("rxrpc_get_call(%p{u=%d})\n",(C),atomic_read(&(C)->usage));*/
}

extern void rxrpc_put_call(struct rxrpc_call *call);

extern void rxrpc_call_do_stuff(struct rxrpc_call *call);

extern int rxrpc_call_abort(struct rxrpc_call *call, int error);

#define RXRPC_CALL_READ_BLOCK	0x0001	/* block if not enough data and not yet EOF */
#define RXRPC_CALL_READ_ALL	0x0002	/* error if insufficient data received */
extern int rxrpc_call_read_data(struct rxrpc_call *call, void *buffer, size_t size, int flags);

extern int rxrpc_call_write_data(struct rxrpc_call *call,
				 size_t sioc,
				 struct kvec *siov,
				 uint8_t rxhdr_flags,
				 unsigned int __nocast alloc_flags,
				 int dup_data,
				 size_t *size_sent);

extern void rxrpc_call_handle_error(struct rxrpc_call *conn, int local, int errno);

#endif /* _LINUX_RXRPC_CALL_H */
