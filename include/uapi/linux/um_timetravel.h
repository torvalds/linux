/*
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Copyright (C) 2019 Intel Corporation
 */
#ifndef _UAPI_LINUX_UM_TIMETRAVEL_H
#define _UAPI_LINUX_UM_TIMETRAVEL_H
#include <linux/types.h>

/**
 * struct um_timetravel_msg - UM time travel message
 *
 * This is the basic message type, going in both directions.
 *
 * This is the message passed between the host (user-mode Linux instance)
 * and the calendar (the application on the other side of the socket) in
 * order to implement common scheduling.
 *
 * Whenever UML has an event it will request runtime for it from the
 * calendar, and then wait for its turn until it can run, etc. Note
 * that it will only ever request the single next runtime, i.e. multiple
 * REQUEST messages override each other.
 */
struct um_timetravel_msg {
	/**
	 * @op: operation value from &enum um_timetravel_ops
	 */
	__u32 op;

	/**
	 * @seq: sequence number for the message - shall be reflected in
	 *	the ACK response, and should be checked while processing
	 *	the response to see if it matches
	 */
	__u32 seq;

	/**
	 * @time: time in nanoseconds
	 */
	__u64 time;
};

/**
 * enum um_timetravel_ops - Operation codes
 */
enum um_timetravel_ops {
	/**
	 * @UM_TIMETRAVEL_ACK: response (ACK) to any previous message,
	 *	this usually doesn't carry any data in the 'time' field
	 *	unless otherwise specified below
	 */
	UM_TIMETRAVEL_ACK		= 0,

	/**
	 * @UM_TIMETRAVEL_START: initialize the connection, the time
	 *	field contains an (arbitrary) ID to possibly be able
	 *	to distinguish the connections.
	 */
	UM_TIMETRAVEL_START		= 1,

	/**
	 * @UM_TIMETRAVEL_REQUEST: request to run at the given time
	 *	(host -> calendar)
	 */
	UM_TIMETRAVEL_REQUEST		= 2,

	/**
	 * @UM_TIMETRAVEL_WAIT: Indicate waiting for the previously requested
	 *	runtime, new requests may be made while waiting (e.g. due to
	 *	interrupts); the time field is ignored. The calendar must process
	 *	this message and later	send a %UM_TIMETRAVEL_RUN message when
	 *	the host can run again.
	 *	(host -> calendar)
	 */
	UM_TIMETRAVEL_WAIT		= 3,

	/**
	 * @UM_TIMETRAVEL_GET: return the current time from the calendar in the
	 *	ACK message, the time in the request message is ignored
	 *	(host -> calendar)
	 */
	UM_TIMETRAVEL_GET		= 4,

	/**
	 * @UM_TIMETRAVEL_UPDATE: time update to the calendar, must be sent e.g.
	 *	before kicking an interrupt to another calendar
	 *	(host -> calendar)
	 */
	UM_TIMETRAVEL_UPDATE		= 5,

	/**
	 * @UM_TIMETRAVEL_RUN: run time request granted, current time is in
	 *	the time field
	 *	(calendar -> host)
	 */
	UM_TIMETRAVEL_RUN		= 6,

	/**
	 * @UM_TIMETRAVEL_FREE_UNTIL: Enable free-running until the given time,
	 *	this is a message from the calendar telling the host that it can
	 *	freely do its own scheduling for anything before the indicated
	 *	time.
	 *	Note that if a calendar sends this message once, the host may
	 *	assume that it will also do so in the future, if it implements
	 *	wraparound semantics for the time field.
	 *	(calendar -> host)
	 */
	UM_TIMETRAVEL_FREE_UNTIL	= 7,

	/**
	 * @UM_TIMETRAVEL_GET_TOD: Return time of day, typically used once at
	 *	boot by the virtual machines to get a synchronized time from
	 *	the simulation.
	 */
	UM_TIMETRAVEL_GET_TOD		= 8,
};

#endif /* _UAPI_LINUX_UM_TIMETRAVEL_H */
