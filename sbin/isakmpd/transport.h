/* $OpenBSD: transport.h,v 1.24 2022/01/28 05:24:15 guenther Exp $	 */
/* $EOM: transport.h,v 1.16 2000/07/17 18:57:59 provos Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2001, 2004 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

/*
 * The transport module tries to separate out details concerning the
 * actual transferral of ISAKMP messages to other parties.
 */

#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "message.h"

struct transport;

LIST_HEAD(transport_list, transport);
extern struct transport_list transport_list;

/* This describes a transport "method" like UDP or similar.  */
struct transport_vtbl {
	/* All transport methods are linked together.  */
	LIST_ENTRY(transport_vtbl) link;

	/* A textual name of the transport method.  */
	char           *name;

	/* Create a transport instance of this method.  */
	struct transport *(*create) (char *);

	/* Reinitialize specific transport.  */
	void            (*reinit) (void);

	/* Remove a transport instance of this method.  */
	void            (*remove) (struct transport *);

	/* Report status of given transport */
	void            (*report) (struct transport *);

	/* Let the given transport set its bit in the fd_set passed in.  */
	int             (*fd_set) (struct transport *, fd_set *, int);

	/* Is the given transport ready for I/O?  */
	int             (*fd_isset) (struct transport *, fd_set *);

	/*
	 * Read a message from the transport's incoming pipe and start
	 * handling it.
	 */
	void            (*handle_message) (struct transport *);

	/* Send a message through the outgoing pipe.  */
	int             (*send_message) (struct message *, struct transport *);

	/*
	 * Fill out a sockaddr structure with the transport's destination end's
	 * address info.
	 */
	void            (*get_dst) (struct transport *, struct sockaddr **);

	/*
	 * Fill out a sockaddr structure with the transport's source end's
	 * address info.
	 */
	void            (*get_src) (struct transport *, struct sockaddr **);

	/*
	 * Return a string with decoded src and dst information
	 */
	char           *(*decode_ids) (struct transport *);

	/*
	 * Clone a transport for outbound use.
	 */
	struct transport *(*clone) (struct transport *, struct sockaddr *);

	/*
	 * Locate the correct sendq to use for outbound messages.
	 */
	struct msg_head *(*get_queue) (struct message *);
};

struct transport {
	/* All transports used are linked together.  */
	LIST_ENTRY(transport) link;

	/* What transport method is this an instance of?  */
	struct transport_vtbl *vtbl;

	/* The queue holding messages to send on this transport.  */
	struct msg_head sendq;

	/*
	 * Prioritized send queue.  Messages in this queue will be transmitted
	 * before the normal sendq, they will also all be transmitted prior
	 * to a daemon shutdown.  Currently only used for DELETE notifications.
	 */
	struct msg_head prio_sendq;

	/* Flags describing the transport.  */
	int             flags;

	/* Reference counter.  */
	int             refcnt;

	/* Pointer to parent virtual transport, if any.  */
	struct transport *virtual;
};

/* Set if this is a transport we want to listen on.  */
#define TRANSPORT_LISTEN	1
/* Used for mark-and-sweep-type garbage collection of transports */
#define TRANSPORT_MARK		2

extern struct transport *transport_create(char *, char *);
extern int      transport_fd_set(fd_set *);
extern void     transport_handle_messages(fd_set *);
extern void     transport_init(void);
extern void     transport_method_add(struct transport_vtbl *);
extern int      transport_pending_wfd_set(fd_set *);
extern int      transport_prio_sendqs_empty(void);
extern void     transport_reference(struct transport *);
extern void     transport_reinit(void);
extern void     transport_release(struct transport *);
extern void     transport_report(void);
extern void     transport_send_messages(fd_set *);
extern void     transport_setup(struct transport *, int);
#endif				/* _TRANSPORT_H_ */
