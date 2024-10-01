/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (C) 2019 - 2023 Intel Corporation
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

/* max number of file descriptors that can be sent/received in a message */
#define UM_TIMETRAVEL_MAX_FDS 2

/**
 * enum um_timetravel_shared_mem_fds - fds sent in ACK message for START message
 */
enum um_timetravel_shared_mem_fds {
	/**
	 * @UM_TIMETRAVEL_SHARED_MEMFD: Index of the shared memory file
	 *	descriptor in the control message
	 */
	UM_TIMETRAVEL_SHARED_MEMFD,
	/**
	 * @UM_TIMETRAVEL_SHARED_LOGFD: Index of the logging file descriptor
	 *	in the control message
	 */
	UM_TIMETRAVEL_SHARED_LOGFD,
	UM_TIMETRAVEL_SHARED_MAX_FDS,
};

/**
 * enum um_timetravel_start_ack - ack-time mask for start message
 */
enum um_timetravel_start_ack {
	/**
	 * @UM_TIMETRAVEL_START_ACK_ID: client ID that controller allocated.
	 */
	UM_TIMETRAVEL_START_ACK_ID = 0xffff,
};

/**
 * enum um_timetravel_ops - Operation codes
 */
enum um_timetravel_ops {
	/**
	 * @UM_TIMETRAVEL_ACK: response (ACK) to any previous message,
	 *	this usually doesn't carry any data in the 'time' field
	 *	unless otherwise specified below, note: while using shared
	 *	memory no ACK for WAIT and RUN messages, for more info see
	 *	&struct um_timetravel_schedshm.
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

	/**
	 * @UM_TIMETRAVEL_BROADCAST: Send/Receive a broadcast message.
	 *	This message can be used to sync all components in the system
	 *	with a single message, if the calender gets the message, the
	 *	calender broadcast the message to all components, and if a
	 *	component receives it it should act based on it e.g print a
	 *	message to it's log system.
	 *	(calendar <-> host)
	 */
	UM_TIMETRAVEL_BROADCAST		= 9,
};

/* version of struct um_timetravel_schedshm */
#define UM_TIMETRAVEL_SCHEDSHM_VERSION 2

/**
 * enum um_timetravel_schedshm_cap - time travel capabilities of every client
 *
 * These flags must be set immediately after processing the ACK to
 * the START message, before sending any message to the controller.
 */
enum um_timetravel_schedshm_cap {
	/**
	 * @UM_TIMETRAVEL_SCHEDSHM_CAP_TIME_SHARE: client can read current time
	 *	update internal time request to shared memory and read
	 *	free until and send no Ack on RUN and doesn't expect ACK on
	 *	WAIT.
	 */
	UM_TIMETRAVEL_SCHEDSHM_CAP_TIME_SHARE = 0x1,
};

/**
 * enum um_timetravel_schedshm_flags - time travel flags of every client
 */
enum um_timetravel_schedshm_flags {
	/**
	 * @UM_TIMETRAVEL_SCHEDSHM_FLAGS_REQ_RUN: client has a request to run.
	 *	It's set by client when it has a request to run, if (and only
	 *	if) the @running_id points to a client that is able to use
	 *	shared memory, i.e. has %UM_TIMETRAVEL_SCHEDSHM_CAP_TIME_SHARE
	 *	(this includes the client itself). Otherwise, a message must
	 *	be used.
	 */
	UM_TIMETRAVEL_SCHEDSHM_FLAGS_REQ_RUN = 0x1,
};

/**
 * DOC: Time travel shared memory overview
 *
 * The main purpose of the shared memory is to avoid all time travel message
 * that don't need any action, for example current time can be held in shared
 * memory without the need of any client to send a message UM_TIMETRAVEL_GET
 * in order to know what's the time.
 *
 * Since this is shared memory with all clients and controller and controller
 * creates the shared memory space, all time values are absolute to controller
 * time. So first time client connects to shared memory mode it should take the
 * current_time value in shared memory and keep it internally as a diff to
 * shared memory times, and once shared memory is initialized, any interaction
 * with the controller must happen in the controller time domain, including any
 * messages (for clients that are not using shared memory, the controller will
 * handle an offset and make the clients think they start at time zero.)
 *
 * Along with the shared memory file descriptor is sent to the client a logging
 * file descriptor, to have all logs related to shared memory,
 * logged into one place. note: to have all logs synced into log file at write,
 * file should be flushed (fflush) after writing to it.
 *
 * To avoid memory corruption, we define below for each field who can write to
 * it at what time, defined in the structure fields.
 *
 * To avoid having to pack this struct, all fields in it must be naturally aligned
 * (i.e. aligned to their size).
 */

/**
 * union um_timetravel_schedshm_client - UM time travel client struct
 *
 * Every entity using the shared memory including the controller has a place in
 * the um_timetravel_schedshm clients array, that holds info related to the client
 * using the shared memory, and can be set only by the client after it gets the
 * fd memory.
 *
 * @capa: bit fields with client capabilities see
 *	&enum um_timetravel_schedshm_cap, set by client once after getting the
 *	shared memory file descriptor.
 * @flags: bit fields for flags see &enum um_timetravel_schedshm_flags for doc.
 * @req_time: request time to run, set by client on every request it needs.
 * @name: unique id sent to the controller by client with START message.
 */
union um_timetravel_schedshm_client {
	struct {
		__u32 capa;
		__u32 flags;
		__u64 req_time;
		__u64 name;
	};
	char reserve[128]; /* reserved for future usage */
};

/**
 * struct um_timetravel_schedshm - UM time travel shared memory struct
 *
 * @hdr: header fields:
 * @version: Current version struct UM_TIMETRAVEL_SCHEDSHM_VERSION,
 *	set by controller once at init, clients must check this after mapping
 *	and work without shared memory if they cannot handle the indicated
 *	version.
 * @len: Length of all the memory including header (@hdr), clients should once
 *	per connection first mmap the header and take the length (@len) to remap the entire size.
 *	This is done in order to support dynamic struct size letting number of
 *	clients be dynamic based on controller support.
 * @free_until: Stores the next request to run by any client, in order for the
 *	current client to know how long it can still run. A client needs to (at
 *	least) reload this value immediately after communicating with any other
 *	client, since the controller will update this field when a new request
 *	is made by any client. Clients also must update this value when they
 *	insert/update an own request into the shared memory while not running
 *	themselves, and the new request is before than the current value.
 * current_time: Current time, can only be set by the client in running state
 *	(indicated by @running_id), though that client may only run until @free_until,
 *	so it must remain smaller than @free_until.
 * @running_id: The current client in state running, set before a client is
 *	notified that it's now running.
 * @max_clients: size of @clients array, set once at init by the controller.
 * @clients: clients array see &union um_timetravel_schedshm_client for doc,
 *	set only by client.
 */
struct um_timetravel_schedshm {
	union {
		struct {
			__u32 version;
			__u32 len;
			__u64 free_until;
			__u64 current_time;
			__u16 running_id;
			__u16 max_clients;
		};
		char hdr[4096]; /* align to 4K page size */
	};
	union um_timetravel_schedshm_client clients[];
};
#endif /* _UAPI_LINUX_UM_TIMETRAVEL_H */
