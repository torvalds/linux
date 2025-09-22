/*
 * netio.h -- network I/O support.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 *
 * The netio module implements event based I/O handling using
 * pselect(2).  Multiple event handlers can wait for a certain event
 * to occur simultaneously.  Each event handler is called when an
 * event occurs that the event handler has indicated that it is
 * willing to handle.
 *
 * There are four types of events that can be handled:
 *
 *   NETIO_EVENT_READ: reading will not block.
 *   NETIO_EVENT_WRITE: writing will not block.
 *   NETIO_EVENT_TIMEOUT: the timeout expired.
 *
 * A file descriptor must be specified if the handler is interested in
 * the first three event types.  A timeout must be specified if the
 * event handler is interested in timeouts.  These event types can be
 * OR'ed together if the handler is willing to handle multiple types
 * of events.
 *
 * The special event type NETIO_EVENT_NONE is available if you wish to
 * temporarily disable the event handler without removing and adding
 * the handler to the netio structure.
 *
 * The event callbacks are free to modify the netio_handler_type
 * structure to change the file descriptor, timeout, event types, user
 * data, or handler functions.
 *
 * The main loop of the program must call netio_dispatch to check for
 * events and dispatch them to the handlers.  An additional timeout
 * can be specified as well as the signal mask to install while
 * blocked in pselect(2).
 */

#ifndef NETIO_H
#define NETIO_H

#ifdef	HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <signal.h>

#include "region-allocator.h"

/*
 * The type of events a handler is interested in.  These can be OR'ed
 * together to specify multiple event types.
 */
enum netio_event_types {
	NETIO_EVENT_NONE    = 0,
	NETIO_EVENT_READ    = 1,
	NETIO_EVENT_WRITE   = 2,
	NETIO_EVENT_TIMEOUT = 4,
};
typedef enum netio_event_types netio_event_types_type;

typedef struct netio netio_type;
typedef struct netio_handler netio_handler_type;
typedef struct netio_handler_list netio_handler_list_type;

struct netio
{
	region_type             *region;
	netio_handler_list_type *handlers;
	netio_handler_list_type *deallocated;

	/*
	 * Cached value of the current time.  The cached value is
	 * cleared at the start of netio_dispatch to calculate the
	 * relative timeouts of the event handlers and after calling
	 * pselect(2) so handlers can use it to calculate a new
	 * absolute timeout.
	 *
	 * Use netio_current_time() to read the current time.
	 */
	int have_current_time;
	struct timespec cached_current_time;

	/*
	 * Next handler in the dispatch. Only valid during callbacks.
	 * To make sure that deletes respect the state of the iterator.
	 */
	netio_handler_list_type *dispatch_next;
};

typedef void (*netio_event_handler_type)(netio_type *netio,
					 netio_handler_type *handler,
					 netio_event_types_type event_types);

struct netio_handler
{
	/*
	 * The file descriptor that should be checked for events.  If
	 * the file descriptor is negative only timeout events are
	 * checked for.
	 */
	int fd;

	/** index of the pollfd array for this handler */
	int pfd;

	/*
	 * The time when no events should be checked for and the
	 * handler should be called with the NETIO_EVENT_TIMEOUT
	 * event type.  Unlike most timeout parameters the time should
	 * be absolute, not relative!
	 */
	struct timespec *timeout;

	/*
	 * Additional user data.
	 */
	void *user_data;

	/*
	 * The type of events that should be checked for.  These types
	 * can be OR'ed together to wait for multiple types of events.
	 */
	netio_event_types_type event_types;

	/*
	 * The event handler.  The event_types parameter contains the
	 * OR'ed set of event types that actually triggered.  The
	 * event handler is allowed to modify this handler object.
	 * The event handler SHOULD NOT block.
	 */
	netio_event_handler_type event_handler;
};


struct netio_handler_list
{
	netio_handler_list_type *next;
	netio_handler_type      *handler;
};


/*
 * Create a new netio instance using the specified REGION.  The netio
 * instance is cleaned up when the REGION is deallocated.
 */
netio_type *netio_create(region_type *region);

/*
 * Add a new HANDLER to NETIO.
 */
void netio_add_handler(netio_type *netio, netio_handler_type *handler);

/*
 * Remove the HANDLER from NETIO.
 */
void netio_remove_handler(netio_type *netio, netio_handler_type *handler);

/*
 * Retrieve the current time (using gettimeofday(2).
 */
const struct timespec *netio_current_time(netio_type *netio);

/*
 * Check for events and dispatch them to the handlers.  If TIMEOUT is
 * specified it specifies the maximum time to wait for an event to
 * arrive.  SIGMASK is passed to the underlying pselect(2) call.
 * Returns the number of non-timeout events dispatched, 0 on timeout,
 * and -1 on error (with errno set appropriately).
 */
int netio_dispatch(netio_type *netio,
		   const struct timespec *timeout,
		   const sigset_t *sigmask);

#endif /* NETIO_H */
