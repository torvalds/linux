/*
 * netio.c -- network I/O support.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>

#include "netio.h"
#include "util.h"

#define MAX_NETIO_FDS 1024

netio_type *
netio_create(region_type *region)
{
	netio_type *result;

	assert(region);

	result = (netio_type *) region_alloc(region, sizeof(netio_type));
	result->region = region;
	result->handlers = NULL;
	result->deallocated = NULL;
	result->dispatch_next = NULL;
	return result;
}

void
netio_add_handler(netio_type *netio, netio_handler_type *handler)
{
	netio_handler_list_type *elt;

	assert(netio);
	assert(handler);

	if (netio->deallocated) {
		/*
		 * If we have deallocated handler list elements, reuse
		 * the first one.
		 */
		elt = netio->deallocated;
		netio->deallocated = elt->next;
	} else {
		/*
		 * Allocate a new one.
		 */
		elt = (netio_handler_list_type *) region_alloc(
			netio->region, sizeof(netio_handler_list_type));
	}

	elt->next = netio->handlers;
	elt->handler = handler;
	elt->handler->pfd = -1;
	netio->handlers = elt;
}

void
netio_remove_handler(netio_type *netio, netio_handler_type *handler)
{
	netio_handler_list_type **elt_ptr;

	assert(netio);
	assert(handler);

	for (elt_ptr = &netio->handlers; *elt_ptr; elt_ptr = &(*elt_ptr)->next) {
		if ((*elt_ptr)->handler == handler) {
			netio_handler_list_type *next = (*elt_ptr)->next;
			if ((*elt_ptr) == netio->dispatch_next)
				netio->dispatch_next = next;
			(*elt_ptr)->handler = NULL;
			(*elt_ptr)->next = netio->deallocated;
			netio->deallocated = *elt_ptr;
			*elt_ptr = next;
			break;
		}
	}
}

const struct timespec *
netio_current_time(netio_type *netio)
{
	assert(netio);

	if (!netio->have_current_time) {
		struct timeval current_timeval;
		if (gettimeofday(&current_timeval, NULL) == -1) {
			log_msg(LOG_ERR, "gettimeofday: %s, aborting.", strerror(errno));
			abort();
		}
		timeval_to_timespec(&netio->cached_current_time, &current_timeval);
		netio->have_current_time = 1;
	}

	return &netio->cached_current_time;
}

int
netio_dispatch(netio_type *netio, const struct timespec *timeout, const sigset_t *sigmask)
{
	/* static arrays to avoid allocation */
	static struct pollfd fds[MAX_NETIO_FDS];
	int numfd;
	int have_timeout = 0;
	struct timespec minimum_timeout;
	netio_handler_type *timeout_handler = NULL;
	netio_handler_list_type *elt;
	int rc;
	int result = 0;
#ifndef HAVE_PPOLL
	sigset_t origmask;
#endif

	assert(netio);

	/*
	 * Clear the cached current time.
	 */
	netio->have_current_time = 0;

	/*
	 * Initialize the minimum timeout with the timeout parameter.
	 */
	if (timeout) {
		have_timeout = 1;
		memcpy(&minimum_timeout, timeout, sizeof(struct timespec));
	}

	/*
	 * Initialize the fd_sets and timeout based on the handler
	 * information.
	 */
	numfd = 0;

	for (elt = netio->handlers; elt; elt = elt->next) {
		netio_handler_type *handler = elt->handler;
		if (handler->fd != -1 && numfd < MAX_NETIO_FDS) {
			fds[numfd].fd = handler->fd;
			fds[numfd].events = 0;
			fds[numfd].revents = 0;
			handler->pfd = numfd;
			if ((handler->event_types & NETIO_EVENT_READ)) {
				fds[numfd].events |= POLLIN;
			}
			if ((handler->event_types & NETIO_EVENT_WRITE)) {
				fds[numfd].events |= POLLOUT;
			}
			numfd++;
		} else {
			handler->pfd = -1;
		}
		if (handler->timeout && (handler->event_types & NETIO_EVENT_TIMEOUT)) {
			struct timespec relative;

			relative.tv_sec = handler->timeout->tv_sec;
			relative.tv_nsec = handler->timeout->tv_nsec;
			timespec_subtract(&relative, netio_current_time(netio));

			if (!have_timeout ||
			    timespec_compare(&relative, &minimum_timeout) < 0)
			{
				have_timeout = 1;
				minimum_timeout.tv_sec = relative.tv_sec;
				minimum_timeout.tv_nsec = relative.tv_nsec;
				timeout_handler = handler;
			}
		}
	}

	if (have_timeout && minimum_timeout.tv_sec < 0) {
		/*
		 * On negative timeout for a handler, immediately
		 * dispatch the timeout event without checking for
		 * other events.
		 */
		if (timeout_handler && (timeout_handler->event_types & NETIO_EVENT_TIMEOUT)) {
			timeout_handler->event_handler(netio, timeout_handler, NETIO_EVENT_TIMEOUT);
		}
		return result;
	}

	/* Check for events.  */
#ifdef HAVE_PPOLL
	rc = ppoll(fds, numfd, (have_timeout?&minimum_timeout:NULL), sigmask);
#else
	sigprocmask(SIG_SETMASK, sigmask, &origmask);
	rc = poll(fds, numfd, (have_timeout?minimum_timeout.tv_sec*1000+
		minimum_timeout.tv_nsec/1000000:-1));
	sigprocmask(SIG_SETMASK, &origmask, NULL);
#endif /* HAVE_PPOLL */
	if (rc == -1) {
		if(errno == EINVAL || errno == EACCES || errno == EBADF) {
			log_msg(LOG_ERR, "fatal error poll: %s.", 
				strerror(errno));
			exit(1);
		}
		return -1;
	}

	/*
	 * Clear the cached current_time (pselect(2) may block for
	 * some time so the cached value is likely to be old).
	 */
	netio->have_current_time = 0;

	if (rc == 0) {
		/*
		 * No events before the minimum timeout expired.
		 * Dispatch to handler if interested.
		 */
		if (timeout_handler && (timeout_handler->event_types & NETIO_EVENT_TIMEOUT)) {
			timeout_handler->event_handler(netio, timeout_handler, NETIO_EVENT_TIMEOUT);
		}
	} else {
		/*
		 * Dispatch all the events to interested handlers
		 * based on the fd_sets.  Note that a handler might
		 * deinstall itself, so store the next handler before
		 * calling the current handler!
		 */
		assert(netio->dispatch_next == NULL);

		for (elt = netio->handlers; elt && rc; ) {
			netio_handler_type *handler = elt->handler;
			netio->dispatch_next = elt->next;
			if (handler->fd != -1 && handler->pfd != -1) {
				netio_event_types_type event_types
					= NETIO_EVENT_NONE;
				if ((fds[handler->pfd].revents & POLLIN)) {
					event_types |= NETIO_EVENT_READ;
				}
				if ((fds[handler->pfd].revents & POLLOUT)) {
					event_types |= NETIO_EVENT_WRITE;
				}
				if ((fds[handler->pfd].revents &
					(POLLNVAL|POLLHUP|POLLERR))) {
					/* closed/error: give a read event,
					 * or otherwise, a write event */
					if((handler->event_types&NETIO_EVENT_READ))
						event_types |= NETIO_EVENT_READ;
					else if((handler->event_types&NETIO_EVENT_WRITE))
						event_types |= NETIO_EVENT_WRITE;
				}

				if ((event_types & handler->event_types)) {
					handler->event_handler(netio, handler, event_types & handler->event_types);
					++result;
				}
			}
			elt = netio->dispatch_next;
		}
		netio->dispatch_next = NULL;
	}

	return result;
}
