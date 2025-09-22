/*	$OpenBSD: event.h,v 1.31 2019/04/29 17:11:51 tobias Exp $	*/

/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#ifndef _EVENT_H_
#define _EVENT_H_

/** @mainpage

  @section intro Introduction

  libevent is an event notification library for developing scalable network
  servers.  The libevent API provides a mechanism to execute a callback
  function when a specific event occurs on a file descriptor or after a
  timeout has been reached. Furthermore, libevent also support callbacks due
  to signals or regular timeouts.

  libevent is meant to replace the event loop found in event driven network
  servers. An application just needs to call event_dispatch() and then add or
  remove events dynamically without having to change the event loop.

  Currently, libevent supports /dev/poll, kqueue(2), select(2), poll(2) and
  epoll(4). It also has experimental support for real-time signals. The
  internal event mechanism is completely independent of the exposed event API,
  and a simple update of libevent can provide new functionality without having
  to redesign the applications. As a result, Libevent allows for portable
  application development and provides the most scalable event notification
  mechanism available on an operating system. Libevent can also be used for
  multi-threaded applications; see Steven Grimm's explanation. Libevent should
  compile on Linux, *BSD, Mac OS X, Solaris and Windows.

  @section usage Standard usage

  Every program that uses libevent must include the <event.h> header, and pass
  the -levent flag to the linker.  Before using any of the functions in the
  library, you must call event_init() or event_base_new() to perform one-time
  initialization of the libevent library.

  @section event Event notification

  For each file descriptor that you wish to monitor, you must declare an event
  structure and call event_set() to initialize the members of the structure.
  To enable notification, you add the structure to the list of monitored
  events by calling event_add().  The event structure must remain allocated as
  long as it is active, so it should be allocated on the heap. Finally, you
  call event_dispatch() to loop and dispatch events.

  @section bufferevent I/O Buffers

  libevent provides an abstraction on top of the regular event callbacks. This
  abstraction is called a buffered event. A buffered event provides input and
  output buffers that get filled and drained automatically. The user of a
  buffered event no longer deals directly with the I/O, but instead is reading
  from input and writing to output buffers.

  Once initialized via bufferevent_new(), the bufferevent structure can be
  used repeatedly with bufferevent_enable() and bufferevent_disable().
  Instead of reading and writing directly to a socket, you would call
  bufferevent_read() and bufferevent_write().

  When read enabled the bufferevent will try to read from the file descriptor
  and call the read callback. The write callback is executed whenever the
  output buffer is drained below the write low watermark, which is 0 by
  default.

  @section timers Timers

  libevent can also be used to create timers that invoke a callback after a
  certain amount of time has expired. The evtimer_set() function prepares an
  event struct to be used as a timer. To activate the timer, call
  evtimer_add(). Timers can be deactivated by calling evtimer_del().

  @section timeouts Timeouts

  In addition to simple timers, libevent can assign timeout events to file
  descriptors that are triggered whenever a certain amount of time has passed
  with no activity on a file descriptor.  The timeout_set() function
  initializes an event struct for use as a timeout. Once initialized, the
  event must be activated by using timeout_add().  To cancel the timeout, call
  timeout_del().

  @section evdns Asynchronous DNS resolution

  libevent provides an asynchronous DNS resolver that should be used instead
  of the standard DNS resolver functions.  These functions can be imported by
  including the <evdns.h> header in your program. Before using any of the
  resolver functions, you must call evdns_init() to initialize the library. To
  convert a hostname to an IP address, you call the evdns_resolve_ipv4()
  function.  To perform a reverse lookup, you would call the
  evdns_resolve_reverse() function.  All of these functions use callbacks to
  avoid blocking while the lookup is performed.

  @section evhttp Event-driven HTTP servers

  libevent provides a very simple event-driven HTTP server that can be
  embedded in your program and used to service HTTP requests.

  To use this capability, you need to include the <evhttp.h> header in your
  program.  You create the server by calling evhttp_new(). Add addresses and
  ports to listen on with evhttp_bind_socket(). You then register one or more
  callbacks to handle incoming requests.  Each URI can be assigned a callback
  via the evhttp_set_cb() function.  A generic callback function can also be
  registered via evhttp_set_gencb(); this callback will be invoked if no other
  callbacks have been registered for a given URI.

  @section evrpc A framework for RPC servers and clients

  libevents provides a framework for creating RPC servers and clients.  It
  takes care of marshaling and unmarshaling all data structures.

  @section api API Reference

  To browse the complete documentation of the libevent API, click on any of
  the following links.

  event.h
  The primary libevent header

  evdns.h
  Asynchronous DNS resolution

  evhttp.h
  An embedded libevent-based HTTP server

  evrpc.h
  A framework for creating RPC servers and clients

 */

/** @file event.h

  A library for writing event-driven network servers

 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/time.h>
#include <sys/queue.h>

#include <stdarg.h>
#include <stdint.h>

#define ev_uint64_t uint64_t
#define ev_int64_t int64_t
#define ev_uint32_t uint32_t
#define ev_uint16_t uint16_t
#define ev_uint8_t uint8_t

#define EVLIST_TIMEOUT	0x01
#define EVLIST_INSERTED	0x02
#define EVLIST_SIGNAL	0x04
#define EVLIST_ACTIVE	0x08
#define EVLIST_INTERNAL	0x10
#define EVLIST_INIT	0x80

/* EVLIST_X_ Private space: 0x1000-0xf000 */
#define EVLIST_ALL	(0xf000 | 0x9f)

#define EV_TIMEOUT	0x01
#define EV_READ		0x02
#define EV_WRITE	0x04
#define EV_SIGNAL	0x08
#define EV_PERSIST	0x10	/* Persistent event */

struct event_base;
#ifndef EVENT_NO_STRUCT
struct event {
	TAILQ_ENTRY (event) ev_next;
	TAILQ_ENTRY (event) ev_active_next;
	TAILQ_ENTRY (event) ev_signal_next;
	size_t min_heap_idx;	/* for managing timeouts */

	struct event_base *ev_base;

	int ev_fd;
	short ev_events;
	short ev_ncalls;
	short *ev_pncalls;	/* Allows deletes in callback */

	struct timeval ev_timeout;

	int ev_pri;		/* smaller numbers are higher priority */

	void (*ev_callback)(int, short, void *arg);
	void *ev_arg;

	int ev_res;		/* result passed to event callback */
	int ev_flags;
};
#else
struct event;
#endif

#define EVENT_SIGNAL(ev)	(int)(ev)->ev_fd
#define EVENT_FD(ev)		(int)(ev)->ev_fd

/*
 * Key-Value pairs.  Can be used for HTTP headers but also for
 * query argument parsing.
 */
struct evkeyval {
	TAILQ_ENTRY(evkeyval) next;

	char *key;
	char *value;
};

TAILQ_HEAD (event_list, event);
TAILQ_HEAD (evkeyvalq, evkeyval);

/**
  Initialize the event API.

  Use event_base_new() to initialize a new event base, but does not set
  the current_base global.   If using only event_base_new(), each event
  added must have an event base set with event_base_set()

  @see event_base_set(), event_base_free(), event_init()
 */
struct event_base *event_base_new(void);

/**
  Initialize the event API.

  The event API needs to be initialized with event_init() before it can be
  used.  Sets the current_base global representing the default base for
  events that have no base associated with them.

  @see event_base_set(), event_base_new()
 */
struct event_base *event_init(void);

/**
  Reinitialized the event base after a fork

  Some event mechanisms do not survive across fork.   The event base needs
  to be reinitialized with the event_reinit() function.

  @param base the event base that needs to be re-initialized
  @return 0 if successful, or -1 if some events could not be re-added.
  @see event_base_new(), event_init()
*/
int event_reinit(struct event_base *base);

/**
  Loop to process events.

  In order to process events, an application needs to call
  event_dispatch().  This function only returns on error, and should
  replace the event core of the application program.

  @see event_base_dispatch()
 */
int event_dispatch(void);


/**
  Threadsafe event dispatching loop.

  @param eb the event_base structure returned by event_init()
  @see event_init(), event_dispatch()
 */
int event_base_dispatch(struct event_base *);


/**
 Get the kernel event notification mechanism used by libevent.

 @param eb the event_base structure returned by event_base_new()
 @return a string identifying the kernel event mechanism (kqueue, epoll, etc.)
 */
const char *event_base_get_method(struct event_base *);


/**
  Deallocate all memory associated with an event_base, and free the base.

  Note that this function will not close any fds or free any memory passed
  to event_set as the argument to callback.

  @param eb an event_base to be freed
 */
void event_base_free(struct event_base *);


#define _EVENT_LOG_DEBUG 0
#define _EVENT_LOG_MSG   1
#define _EVENT_LOG_WARN  2
#define _EVENT_LOG_ERR   3
typedef void (*event_log_cb)(int severity, const char *msg);
/**
  Redirect libevent's log messages.

  @param cb a function taking two arguments: an integer severity between
     _EVENT_LOG_DEBUG and _EVENT_LOG_ERR, and a string.  If cb is NULL,
	 then the default log is used.
  */
void event_set_log_callback(event_log_cb cb);

/**
  Associate a different event base with an event.

  @param eb the event base
  @param ev the event
 */
int event_base_set(struct event_base *, struct event *);

/**
 event_loop() flags
 */
/*@{*/
#define EVLOOP_ONCE	0x01	/**< Block at most once. */
#define EVLOOP_NONBLOCK	0x02	/**< Do not block. */
/*@}*/

/**
  Handle events.

  This is a more flexible version of event_dispatch().

  @param flags any combination of EVLOOP_ONCE | EVLOOP_NONBLOCK
  @return 0 if successful, -1 if an error occurred, or 1 if no events were
    registered.
  @see event_loopexit(), event_base_loop()
*/
int event_loop(int);

/**
  Handle events (threadsafe version).

  This is a more flexible version of event_base_dispatch().

  @param eb the event_base structure returned by event_init()
  @param flags any combination of EVLOOP_ONCE | EVLOOP_NONBLOCK
  @return 0 if successful, -1 if an error occurred, or 1 if no events were
    registered.
  @see event_loopexit(), event_base_loop()
  */
int event_base_loop(struct event_base *, int);

/**
  Exit the event loop after the specified time.

  The next event_loop() iteration after the given timer expires will
  complete normally (handling all queued events) then exit without
  blocking for events again.

  Subsequent invocations of event_loop() will proceed normally.

  @param tv the amount of time after which the loop should terminate.
  @return 0 if successful, or -1 if an error occurred
  @see event_loop(), event_base_loop(), event_base_loopexit()
  */
int event_loopexit(const struct timeval *);


/**
  Exit the event loop after the specified time (threadsafe variant).

  The next event_base_loop() iteration after the given timer expires will
  complete normally (handling all queued events) then exit without
  blocking for events again.

  Subsequent invocations of event_base_loop() will proceed normally.

  @param eb the event_base structure returned by event_init()
  @param tv the amount of time after which the loop should terminate.
  @return 0 if successful, or -1 if an error occurred
  @see event_loopexit()
 */
int event_base_loopexit(struct event_base *, const struct timeval *);

/**
  Abort the active event_loop() immediately.

  event_loop() will abort the loop after the next event is completed;
  event_loopbreak() is typically invoked from this event's callback.
  This behavior is analogous to the "break;" statement.

  Subsequent invocations of event_loop() will proceed normally.

  @return 0 if successful, or -1 if an error occurred
  @see event_base_loopbreak(), event_loopexit()
 */
int event_loopbreak(void);

/**
  Abort the active event_base_loop() immediately.

  event_base_loop() will abort the loop after the next event is completed;
  event_base_loopbreak() is typically invoked from this event's callback.
  This behavior is analogous to the "break;" statement.

  Subsequent invocations of event_loop() will proceed normally.

  @param eb the event_base structure returned by event_init()
  @return 0 if successful, or -1 if an error occurred
  @see event_base_loopexit
 */
int event_base_loopbreak(struct event_base *);


/**
  Add a timer event.

  @param ev the event struct
  @param tv timeval struct
 */
#define evtimer_add(ev, tv)		event_add(ev, tv)


/**
  Define a timer event.

  @param ev event struct to be modified
  @param cb callback function
  @param arg argument that will be passed to the callback function
 */
#define evtimer_set(ev, cb, arg)	event_set(ev, -1, 0, cb, arg)


/**
 * Delete a timer event.
 *
 * @param ev the event struct to be disabled
 */
#define evtimer_del(ev)			event_del(ev)
#define evtimer_pending(ev, tv)		event_pending(ev, EV_TIMEOUT, tv)
#define evtimer_initialized(ev)		((ev)->ev_flags & EVLIST_INIT)

#ifdef EVENT_DEPRECATED
/*
 * timeout_* are collision-prone names for macros, and they are
 * deprecated. Define EVENT_DEPRECATED to expose them anyway.
 *
 * It is recommended evtimer_* be used instead.
 */

/**
 * Add a timeout event.
 *
 * @param ev the event struct to be disabled
 * @param tv the timeout value, in seconds
 */
#define timeout_add(ev, tv)		event_add(ev, tv)


/**
 * Define a timeout event.
 *
 * @param ev the event struct to be defined
 * @param cb the callback to be invoked when the timeout expires
 * @param arg the argument to be passed to the callback
 */
#define timeout_set(ev, cb, arg)	event_set(ev, -1, 0, cb, arg)


/**
 * Disable a timeout event.
 *
 * @param ev the timeout event to be disabled
 */
#define timeout_del(ev)			event_del(ev)

#define timeout_pending(ev, tv)		event_pending(ev, EV_TIMEOUT, tv)
#define timeout_initialized(ev)		((ev)->ev_flags & EVLIST_INIT)

#endif /* EVENT_DEPRECATED */

#define signal_add(ev, tv)		event_add(ev, tv)
#define signal_set(ev, x, cb, arg)	\
	event_set(ev, x, EV_SIGNAL|EV_PERSIST, cb, arg)
#define signal_del(ev)			event_del(ev)
#define signal_pending(ev, tv)		event_pending(ev, EV_SIGNAL, tv)
#define signal_initialized(ev)		((ev)->ev_flags & EVLIST_INIT)

/**
  Prepare an event structure to be added.

  The function event_set() prepares the event structure ev to be used in
  future calls to event_add() and event_del().  The event will be prepared to
  call the function specified by the fn argument with an int argument
  indicating the file descriptor, a short argument indicating the type of
  event, and a void * argument given in the arg argument.  The fd indicates
  the file descriptor that should be monitored for events.  The events can be
  either EV_READ, EV_WRITE, or both.  Indicating that an application can read
  or write from the file descriptor respectively without blocking.

  The function fn will be called with the file descriptor that triggered the
  event and the type of event which will be either EV_TIMEOUT, EV_SIGNAL,
  EV_READ, or EV_WRITE.  The additional flag EV_PERSIST makes an event_add()
  persistent until event_del() has been called.

  @param ev an event struct to be modified
  @param fd the file descriptor to be monitored
  @param event desired events to monitor; can be EV_READ and/or EV_WRITE
  @param fn callback function to be invoked when the event occurs
  @param arg an argument to be passed to the callback function

  @see event_add(), event_del(), event_once()

 */
void event_set(struct event *, int, short, void (*)(int, short, void *), void *);

/**
  Schedule a one-time event to occur.

  The function event_once() is similar to event_set().  However, it schedules
  a callback to be called exactly once and does not require the caller to
  prepare an event structure.

  @param fd a file descriptor to monitor
  @param events event(s) to monitor; can be any of EV_TIMEOUT | EV_READ |
         EV_WRITE
  @param callback callback function to be invoked when the event occurs
  @param arg an argument to be passed to the callback function
  @param timeout the maximum amount of time to wait for the event, or NULL
         to wait forever
  @return 0 if successful, or -1 if an error occurred
  @see event_set()

 */
int event_once(int, short, void (*)(int, short, void *), void *,
    const struct timeval *);


/**
  Schedule a one-time event (threadsafe variant)

  The function event_base_once() is similar to event_set().  However, it
  schedules a callback to be called exactly once and does not require the
  caller to prepare an event structure.

  @param base an event_base returned by event_init()
  @param fd a file descriptor to monitor
  @param events event(s) to monitor; can be any of EV_TIMEOUT | EV_READ |
         EV_WRITE
  @param callback callback function to be invoked when the event occurs
  @param arg an argument to be passed to the callback function
  @param timeout the maximum amount of time to wait for the event, or NULL
         to wait forever
  @return 0 if successful, or -1 if an error occurred
  @see event_once()
 */
int event_base_once(struct event_base *base, int fd, short events,
    void (*callback)(int, short, void *), void *arg,
    const struct timeval *timeout);


/**
  Add an event to the set of monitored events.

  The function event_add() schedules the execution of the ev event when the
  event specified in event_set() occurs or in at least the time specified in
  the tv.  If tv is NULL, no timeout occurs and the function will only be
  called if a matching event occurs on the file descriptor.  The event in the
  ev argument must be already initialized by event_set() and may not be used
  in calls to event_set() until it has timed out or been removed with
  event_del().  If the event in the ev argument already has a scheduled
  timeout, the old timeout will be replaced by the new one.

  @param ev an event struct initialized via event_set()
  @param timeout the maximum amount of time to wait for the event, or NULL
         to wait forever
  @return 0 if successful, or -1 if an error occurred
  @see event_del(), event_set()
  */
int event_add(struct event *ev, const struct timeval *timeout);


/**
  Remove an event from the set of monitored events.

  The function event_del() will cancel the event in the argument ev.  If the
  event has already executed or has never been added the call will have no
  effect.

  @param ev an event struct to be removed from the working set
  @return 0 if successful, or -1 if an error occurred
  @see event_add()
 */
int event_del(struct event *);

void event_active(struct event *, int, short);


/**
  Checks if a specific event is pending or scheduled.

  @param ev an event struct previously passed to event_add()
  @param event the requested event type; any of EV_TIMEOUT|EV_READ|
         EV_WRITE|EV_SIGNAL
  @param tv an alternate timeout (FIXME - is this true?)

  @return 1 if the event is pending, or 0 if the event has not occurred

 */
int event_pending(struct event *ev, short event, struct timeval *tv);


/**
  Test if an event structure has been initialized.

  The event_initialized() macro can be used to check if an event has been
  initialized.

  @param ev an event structure to be tested
  @return 1 if the structure has been initialized, or 0 if it has not been
         initialized
 */
#define event_initialized(ev)		((ev)->ev_flags & EVLIST_INIT)


/**
  Get the libevent version number.

  @return a string containing the version number of libevent
 */
const char *event_get_version(void);


/**
  Get the kernel event notification mechanism used by libevent.

  @return a string identifying the kernel event mechanism (kqueue, epoll, etc.)
 */
const char *event_get_method(void);


/**
  Set the number of different event priorities.

  By default libevent schedules all active events with the same priority.
  However, some time it is desirable to process some events with a higher
  priority than others.  For that reason, libevent supports strict priority
  queues.  Active events with a lower priority are always processed before
  events with a higher priority.

  The number of different priorities can be set initially with the
  event_priority_init() function.  This function should be called before the
  first call to event_dispatch().  The event_priority_set() function can be
  used to assign a priority to an event.  By default, libevent assigns the
  middle priority to all events unless their priority is explicitly set.

  @param npriorities the maximum number of priorities
  @return 0 if successful, or -1 if an error occurred
  @see event_base_priority_init(), event_priority_set()

 */
int	event_priority_init(int);


/**
  Set the number of different event priorities (threadsafe variant).

  See the description of event_priority_init() for more information.

  @param eb the event_base structure returned by event_init()
  @param npriorities the maximum number of priorities
  @return 0 if successful, or -1 if an error occurred
  @see event_priority_init(), event_priority_set()
 */
int	event_base_priority_init(struct event_base *, int);


/**
  Assign a priority to an event.

  @param ev an event struct
  @param priority the new priority to be assigned
  @return 0 if successful, or -1 if an error occurred
  @see event_priority_init()
  */
int	event_priority_set(struct event *, int);


/* Simple helpers for ASR async resolution API. */

/* We don't want to pull asr.h here */
struct asr_query;
struct asr_result;

struct event_asr;

/**
 * Schedule an async query to run in the libevent event loop, and trigger
 * a callback when done. Returns an opaque async event handle.
 */
struct event_asr * event_asr_run(struct asr_query *,
    void (*)(struct asr_result *, void *), void *);

/**
 * Cancel a running async query associated to an handle.
 */
void event_asr_abort(struct event_asr *);


/* These functions deal with buffering input and output */

struct evbuffer {
	u_char *buffer;
	u_char *orig_buffer;

	size_t misalign;
	size_t totallen;
	size_t off;

	void (*cb)(struct evbuffer *, size_t, size_t, void *);
	void *cbarg;
};

/* Just for error reporting - use other constants otherwise */
#define EVBUFFER_READ		0x01
#define EVBUFFER_WRITE		0x02
#define EVBUFFER_EOF		0x10
#define EVBUFFER_ERROR		0x20
#define EVBUFFER_TIMEOUT	0x40

struct bufferevent;
typedef void (*evbuffercb)(struct bufferevent *, void *);
typedef void (*everrorcb)(struct bufferevent *, short what, void *);

struct event_watermark {
	size_t low;
	size_t high;
};

#ifndef EVENT_NO_STRUCT
struct bufferevent {
	struct event_base *ev_base;

	struct event ev_read;
	struct event ev_write;

	struct evbuffer *input;
	struct evbuffer *output;

	struct event_watermark wm_read;
	struct event_watermark wm_write;

	evbuffercb readcb;
	evbuffercb writecb;
	everrorcb errorcb;
	void *cbarg;

	int timeout_read;	/* in seconds */
	int timeout_write;	/* in seconds */

	short enabled;	/* events that are currently enabled */
};
#endif

/**
  Create a new bufferevent.

  libevent provides an abstraction on top of the regular event callbacks.
  This abstraction is called a buffered event.  A buffered event provides
  input and output buffers that get filled and drained automatically.  The
  user of a buffered event no longer deals directly with the I/O, but
  instead is reading from input and writing to output buffers.

  Once initialized, the bufferevent structure can be used repeatedly with
  bufferevent_enable() and bufferevent_disable().

  When read enabled the bufferevent will try to read from the file descriptor
  and call the read callback.  The write callback is executed whenever the
  output buffer is drained below the write low watermark, which is 0 by
  default.

  If multiple bases are in use, bufferevent_base_set() must be called before
  enabling the bufferevent for the first time.

  @param fd the file descriptor from which data is read and written to.
         This file descriptor is not allowed to be a pipe(2).
  @param readcb callback to invoke when there is data to be read, or NULL if
         no callback is desired
  @param writecb callback to invoke when the file descriptor is ready for
         writing, or NULL if no callback is desired
  @param errorcb callback to invoke when there is an error on the file
         descriptor
  @param cbarg an argument that will be supplied to each of the callbacks
         (readcb, writecb, and errorcb)
  @return a pointer to a newly allocated bufferevent struct, or NULL if an
         error occurred
  @see bufferevent_base_set(), bufferevent_free()
  */
struct bufferevent *bufferevent_new(int fd,
    evbuffercb readcb, evbuffercb writecb, everrorcb errorcb, void *cbarg);


/**
  Assign a bufferevent to a specific event_base.

  @param base an event_base returned by event_init()
  @param bufev a bufferevent struct returned by bufferevent_new()
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_new()
 */
int bufferevent_base_set(struct event_base *base, struct bufferevent *bufev);


/**
  Assign a priority to a bufferevent.

  @param bufev a bufferevent struct
  @param pri the priority to be assigned
  @return 0 if successful, or -1 if an error occurred
  */
int bufferevent_priority_set(struct bufferevent *bufev, int pri);


/**
  Deallocate the storage associated with a bufferevent structure.

  @param bufev the bufferevent structure to be freed.
  */
void bufferevent_free(struct bufferevent *bufev);


/**
  Changes the callbacks for a bufferevent.

  @param bufev the bufferevent object for which to change callbacks
  @param readcb callback to invoke when there is data to be read, or NULL if
         no callback is desired
  @param writecb callback to invoke when the file descriptor is ready for
         writing, or NULL if no callback is desired
  @param errorcb callback to invoke when there is an error on the file
         descriptor
  @param cbarg an argument that will be supplied to each of the callbacks
         (readcb, writecb, and errorcb)
  @see bufferevent_new()
  */
void bufferevent_setcb(struct bufferevent *bufev,
    evbuffercb readcb, evbuffercb writecb, everrorcb errorcb, void *cbarg);

/**
  Changes the file descriptor on which the bufferevent operates.

  @param bufev the bufferevent object for which to change the file descriptor
  @param fd the file descriptor to operate on
*/
void bufferevent_setfd(struct bufferevent *bufev, int fd);

/**
  Write data to a bufferevent buffer.

  The bufferevent_write() function can be used to write data to the file
  descriptor.  The data is appended to the output buffer and written to the
  descriptor automatically as it becomes available for writing.

  @param bufev the bufferevent to be written to
  @param data a pointer to the data to be written
  @param size the length of the data, in bytes
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_write_buffer()
  */
int bufferevent_write(struct bufferevent *bufev,
    const void *data, size_t size);


/**
  Write data from an evbuffer to a bufferevent buffer.  The evbuffer is
  being drained as a result.

  @param bufev the bufferevent to be written to
  @param buf the evbuffer to be written
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_write()
 */
int bufferevent_write_buffer(struct bufferevent *bufev, struct evbuffer *buf);


/**
  Read data from a bufferevent buffer.

  The bufferevent_read() function is used to read data from the input buffer.

  @param bufev the bufferevent to be read from
  @param data pointer to a buffer that will store the data
  @param size the size of the data buffer, in bytes
  @return the amount of data read, in bytes.
 */
size_t bufferevent_read(struct bufferevent *bufev, void *data, size_t size);

/**
  Enable a bufferevent.

  @param bufev the bufferevent to be enabled
  @param event any combination of EV_READ | EV_WRITE.
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_disable()
 */
int bufferevent_enable(struct bufferevent *bufev, short event);


/**
  Disable a bufferevent.

  @param bufev the bufferevent to be disabled
  @param event any combination of EV_READ | EV_WRITE.
  @return 0 if successful, or -1 if an error occurred
  @see bufferevent_enable()
 */
int bufferevent_disable(struct bufferevent *bufev, short event);


/**
  Set the read and write timeout for a buffered event.

  @param bufev the bufferevent to be modified
  @param timeout_read the read timeout
  @param timeout_write the write timeout
 */
void bufferevent_settimeout(struct bufferevent *bufev,
    int timeout_read, int timeout_write);


/**
  Sets the watermarks for read and write events.

  On input, a bufferevent does not invoke the user read callback unless
  there is at least low watermark data in the buffer.   If the read buffer
  is beyond the high watermark, the buffevent stops reading from the network.

  On output, the user write callback is invoked whenever the buffered data
  falls below the low watermark.

  @param bufev the bufferevent to be modified
  @param events EV_READ, EV_WRITE or both
  @param lowmark the lower watermark to set
  @param highmark the high watermark to set
*/

void bufferevent_setwatermark(struct bufferevent *bufev, short events,
    size_t lowmark, size_t highmark);

#define EVBUFFER_LENGTH(x)	(x)->off
#define EVBUFFER_DATA(x)	(x)->buffer
#define EVBUFFER_INPUT(x)	(x)->input
#define EVBUFFER_OUTPUT(x)	(x)->output


/**
  Allocate storage for a new evbuffer.

  @return a pointer to a newly allocated evbuffer struct, or NULL if an error
         occurred
 */
struct evbuffer *evbuffer_new(void);


/**
  Deallocate storage for an evbuffer.

  @param pointer to the evbuffer to be freed
 */
void evbuffer_free(struct evbuffer *);


/**
  Expands the available space in an event buffer.

  Expands the available space in the event buffer to at least datlen

  @param buf the event buffer to be expanded
  @param datlen the new minimum length requirement
  @return 0 if successful, or -1 if an error occurred
*/
int evbuffer_expand(struct evbuffer *, size_t);


/**
  Append data to the end of an evbuffer.

  @param buf the event buffer to be appended to
  @param data pointer to the beginning of the data buffer
  @param datlen the number of bytes to be copied from the data buffer
 */
int evbuffer_add(struct evbuffer *, const void *, size_t);



/**
  Read data from an event buffer and drain the bytes read.

  @param buf the event buffer to be read from
  @param data the destination buffer to store the result
  @param datlen the maximum size of the destination buffer
  @return the number of bytes read
 */
int evbuffer_remove(struct evbuffer *, void *, size_t);


/**
 * Read a single line from an event buffer.
 *
 * Reads a line terminated by either '\r\n', '\n\r' or '\r' or '\n'.
 * The returned buffer needs to be freed by the caller.
 *
 * @param buffer the evbuffer to read from
 * @return pointer to a single line, or NULL if an error occurred
 */
char *evbuffer_readline(struct evbuffer *);


/** Used to tell evbuffer_readln what kind of line-ending to look for.
 */
enum evbuffer_eol_style {
	/** Any sequence of CR and LF characters is acceptable as an EOL. */
	EVBUFFER_EOL_ANY,
	/** An EOL is an LF, optionally preceded by a CR.  This style is
	 * most useful for implementing text-based internet protocols. */
	EVBUFFER_EOL_CRLF,
	/** An EOL is a CR followed by an LF. */
	EVBUFFER_EOL_CRLF_STRICT,
	/** An EOL is a LF. */
        EVBUFFER_EOL_LF
};

/**
 * Read a single line from an event buffer.
 *
 * Reads a line terminated by an EOL as determined by the evbuffer_eol_style
 * argument.  Returns a newly allocated nul-terminated string; the caller must
 * free the returned value.  The EOL is not included in the returned string.
 *
 * @param buffer the evbuffer to read from
 * @param n_read_out if non-NULL, points to a size_t that is set to the
 *       number of characters in the returned string.  This is useful for
 *       strings that can contain NUL characters.
 * @param eol_style the style of line-ending to use.
 * @return pointer to a single line, or NULL if an error occurred
 */
char *evbuffer_readln(struct evbuffer *buffer, size_t *n_read_out,
    enum evbuffer_eol_style eol_style);


/**
  Move data from one evbuffer into another evbuffer.

  This is a destructive add.  The data from one buffer moves into
  the other buffer. The destination buffer is expanded as needed.

  @param outbuf the output buffer
  @param inbuf the input buffer
  @return 0 if successful, or -1 if an error occurred
 */
int evbuffer_add_buffer(struct evbuffer *, struct evbuffer *);


/**
  Append a formatted string to the end of an evbuffer.

  @param buf the evbuffer that will be appended to
  @param fmt a format string
  @param ... arguments that will be passed to printf(3)
  @return The number of bytes added if successful, or -1 if an error occurred.
 */
int evbuffer_add_printf(struct evbuffer *, const char *fmt, ...)
#ifdef __GNUC__
  __attribute__((format(printf, 2, 3)))
#endif
;


/**
  Append a va_list formatted string to the end of an evbuffer.

  @param buf the evbuffer that will be appended to
  @param fmt a format string
  @param ap a varargs va_list argument array that will be passed to vprintf(3)
  @return The number of bytes added if successful, or -1 if an error occurred.
 */
int evbuffer_add_vprintf(struct evbuffer *, const char *fmt, va_list ap);


/**
  Remove a specified number of bytes data from the beginning of an evbuffer.

  @param buf the evbuffer to be drained
  @param len the number of bytes to drain from the beginning of the buffer
 */
void evbuffer_drain(struct evbuffer *, size_t);


/**
  Write the contents of an evbuffer to a file descriptor.

  The evbuffer will be drained after the bytes have been successfully written.

  @param buffer the evbuffer to be written and drained
  @param fd the file descriptor to be written to
  @return the number of bytes written, or -1 if an error occurred
  @see evbuffer_read()
 */
int evbuffer_write(struct evbuffer *, int);


/**
  Read from a file descriptor and store the result in an evbuffer.

  @param buf the evbuffer to store the result
  @param fd the file descriptor to read from
  @param howmuch the number of bytes to be read
  @return the number of bytes read, or -1 if an error occurred
  @see evbuffer_write()
 */
int evbuffer_read(struct evbuffer *, int, int);


/**
  Find a string within an evbuffer.

  @param buffer the evbuffer to be searched
  @param what the string to be searched for
  @param len the length of the search string
  @return a pointer to the beginning of the search string, or NULL if the search failed.
 */
u_char *evbuffer_find(struct evbuffer *, const u_char *, size_t);

/**
  Set a callback to invoke when the evbuffer is modified.

  @param buffer the evbuffer to be monitored
  @param cb the callback function to invoke when the evbuffer is modified
  @param cbarg an argument to be provided to the callback function
 */
void evbuffer_setcb(struct evbuffer *, void (*)(struct evbuffer *, size_t, size_t, void *), void *);

/*
 * Marshaling tagged data - We assume that all tags are inserted in their
 * numeric order - so that unknown tags will always be higher than the
 * known ones - and we can just ignore the end of an event buffer.
 */

void evtag_init(void);

void evtag_marshal(struct evbuffer *evbuf, ev_uint32_t tag, const void *data,
    ev_uint32_t len);

/**
  Encode an integer and store it in an evbuffer.

  We encode integer's by nibbles; the first nibble contains the number
  of significant nibbles - 1;  this allows us to encode up to 64-bit
  integers.  This function is byte-order independent.

  @param evbuf evbuffer to store the encoded number
  @param number a 32-bit integer
 */
void encode_int(struct evbuffer *evbuf, ev_uint32_t number);

void evtag_marshal_int(struct evbuffer *evbuf, ev_uint32_t tag,
    ev_uint32_t integer);

void evtag_marshal_string(struct evbuffer *buf, ev_uint32_t tag,
    const char *string);

void evtag_marshal_timeval(struct evbuffer *evbuf, ev_uint32_t tag,
    struct timeval *tv);

int evtag_unmarshal(struct evbuffer *src, ev_uint32_t *ptag,
    struct evbuffer *dst);
int evtag_peek(struct evbuffer *evbuf, ev_uint32_t *ptag);
int evtag_peek_length(struct evbuffer *evbuf, ev_uint32_t *plength);
int evtag_payload_length(struct evbuffer *evbuf, ev_uint32_t *plength);
int evtag_consume(struct evbuffer *evbuf);

int evtag_unmarshal_int(struct evbuffer *evbuf, ev_uint32_t need_tag,
    ev_uint32_t *pinteger);

int evtag_unmarshal_fixed(struct evbuffer *src, ev_uint32_t need_tag,
    void *data, size_t len);

int evtag_unmarshal_string(struct evbuffer *evbuf, ev_uint32_t need_tag,
    char **pstring);

int evtag_unmarshal_timeval(struct evbuffer *evbuf, ev_uint32_t need_tag,
    struct timeval *ptv);

#define _EVENT_VERSION "1.4.15-stable"

#ifdef __cplusplus
}
#endif

#endif /* _EVENT_H_ */
