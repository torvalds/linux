/*
 * util/ub_event.h - indirection layer for pluggable events
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains prototypes for event loop functions.
 *
 */

#ifndef UB_EVENT_H
#define UB_EVENT_H

struct ub_event_base;
struct ub_event;
struct comm_base;
struct event_base;

/** event timeout */
#define UB_EV_TIMEOUT      0x01
/** event fd readable */
#define UB_EV_READ         0x02
/** event fd writable */
#define UB_EV_WRITE        0x04
/** event signal */
#define UB_EV_SIGNAL       0x08
/** event must persist */
#define UB_EV_PERSIST      0x10

/** Returns event-base type. Could be "mini-event", "winsock-event" for the
 * daemon compile, and will be "pluggable-event<PACKAGE_VERSION>" for 
 * libunbound.
 */
const char* ub_event_get_version(void);
/** Return the name, system and method for the pluggable event base */
void ub_get_event_sys(struct ub_event_base*, const char** n, const char** s,
	const char** m);
/** Return a default event base. In the daemon this will be the only event 
 * bases used.
 */
struct ub_event_base* ub_default_event_base(int, time_t*, struct timeval*);
/** Return an ub_event_base constructed for the given libevent event base */
struct ub_event_base* ub_libevent_event_base(struct event_base*);
/** Return the libevent base underlying the given ub_event_base.  Will return
 * NULL when the ub_event_base does not have an underlying libevent event base
 */
struct event_base* ub_libevent_get_event_base(struct ub_event_base*);
/** Free event base. Free events yourself */
void ub_event_base_free(struct ub_event_base*);
/** Run the event base */
int ub_event_base_dispatch(struct ub_event_base*);
/** exit that loop */
int ub_event_base_loopexit(struct ub_event_base*);

/** Create a new ub_event for the event base */
struct ub_event* ub_event_new(struct ub_event_base*,
	int fd, short bits, void (*cb)(int, short, void*), void* arg);
/** Create a new ub_event signal for the event base */
struct ub_event* ub_signal_new(struct ub_event_base*, int fd,
	void (*cb)(int, short, void*), void* arg);
/** Create a new ub_event associated with the wsaevent for the event base */
struct ub_event* ub_winsock_register_wsaevent(struct ub_event_base*,
	void* wsaevent, void (*cb)(int, short, void*), void* arg);

/** Add event bits for this event to fire on */
void ub_event_add_bits(struct ub_event*, short bits);
 /** Configure the event so it will not longer fire on given bits */
void ub_event_del_bits(struct ub_event*, short bits);
/** Change or set the file descriptor on the event */
void ub_event_set_fd(struct ub_event*, int fd);
/** free the event */
void ub_event_free(struct ub_event*);
/** Activate the event.  The given timeval is an timeout value. */
int ub_event_add(struct ub_event*, struct timeval*);
/** Deactivate the event */
int ub_event_del(struct ub_event*);
/** Reconfigure and activate a timeout event */
int ub_timer_add(struct ub_event*, struct ub_event_base*,
	void (*cb)(int, short, void*), void* arg, struct timeval*);
/** Deactivate the timeout event */
int ub_timer_del(struct ub_event*);
/** Activate a signal event */
int ub_signal_add(struct ub_event*, struct timeval*);
/** Deactivate a signal event */
int ub_signal_del(struct ub_event*);
/** Free a with a wsaevent associated event */
void ub_winsock_unregister_wsaevent(struct ub_event* ev);
/** Signal the eventloop when a TCP windows socket will block on next read
 * or write (given by the eventbits)
 */
void ub_winsock_tcp_wouldblock(struct ub_event*, int bits);
/** Equip the comm_base with the current time */
void ub_comm_base_now(struct comm_base* cb);

#endif /* UB_EVENT_H */
