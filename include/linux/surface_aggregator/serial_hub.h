/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Surface Serial Hub (SSH) protocol and communication interface.
 *
 * Lower-level communication layers and SSH protocol definitions for the
 * Surface System Aggregator Module (SSAM). Provides the interface for basic
 * packet- and request-based communication with the SSAM EC via SSH.
 *
 * Copyright (C) 2019-2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _LINUX_SURFACE_AGGREGATOR_SERIAL_HUB_H
#define _LINUX_SURFACE_AGGREGATOR_SERIAL_HUB_H

#include <linux/crc-ccitt.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/types.h>


/* -- Data structures for SAM-over-SSH communication. ----------------------- */

/**
 * enum ssh_frame_type - Frame types for SSH frames.
 *
 * @SSH_FRAME_TYPE_DATA_SEQ:
 *	Indicates a data frame, followed by a payload with the length specified
 *	in the ``struct ssh_frame.len`` field. This frame is sequenced, meaning
 *	that an ACK is required.
 *
 * @SSH_FRAME_TYPE_DATA_NSQ:
 *	Same as %SSH_FRAME_TYPE_DATA_SEQ, but unsequenced, meaning that the
 *	message does not have to be ACKed.
 *
 * @SSH_FRAME_TYPE_ACK:
 *	Indicates an ACK message.
 *
 * @SSH_FRAME_TYPE_NAK:
 *	Indicates an error response for previously sent frame. In general, this
 *	means that the frame and/or payload is malformed, e.g. a CRC is wrong.
 *	For command-type payloads, this can also mean that the command is
 *	invalid.
 */
enum ssh_frame_type {
	SSH_FRAME_TYPE_DATA_SEQ = 0x80,
	SSH_FRAME_TYPE_DATA_NSQ = 0x00,
	SSH_FRAME_TYPE_ACK      = 0x40,
	SSH_FRAME_TYPE_NAK      = 0x04,
};

/**
 * struct ssh_frame - SSH communication frame.
 * @type: The type of the frame. See &enum ssh_frame_type.
 * @len:  The length of the frame payload directly following the CRC for this
 *        frame. Does not include the final CRC for that payload.
 * @seq:  The sequence number for this message/exchange.
 */
struct ssh_frame {
	u8 type;
	__le16 len;
	u8 seq;
} __packed;

static_assert(sizeof(struct ssh_frame) == 4);

/*
 * SSH_FRAME_MAX_PAYLOAD_SIZE - Maximum SSH frame payload length in bytes.
 *
 * This is the physical maximum length of the protocol. Implementations may
 * set a more constrained limit.
 */
#define SSH_FRAME_MAX_PAYLOAD_SIZE	U16_MAX

/**
 * enum ssh_payload_type - Type indicator for the SSH payload.
 * @SSH_PLD_TYPE_CMD: The payload is a command structure with optional command
 *                    payload.
 */
enum ssh_payload_type {
	SSH_PLD_TYPE_CMD = 0x80,
};

/**
 * struct ssh_command - Payload of a command-type frame.
 * @type:    The type of the payload. See &enum ssh_payload_type. Should be
 *           SSH_PLD_TYPE_CMD for this struct.
 * @tc:      Command target category.
 * @tid_out: Output target ID. Should be zero if this an incoming (EC to host)
 *           message.
 * @tid_in:  Input target ID. Should be zero if this is an outgoing (host to
 *           EC) message.
 * @iid:     Instance ID.
 * @rqid:    Request ID. Used to match requests with responses and differentiate
 *           between responses and events.
 * @cid:     Command ID.
 */
struct ssh_command {
	u8 type;
	u8 tc;
	u8 tid_out;
	u8 tid_in;
	u8 iid;
	__le16 rqid;
	u8 cid;
} __packed;

static_assert(sizeof(struct ssh_command) == 8);

/*
 * SSH_COMMAND_MAX_PAYLOAD_SIZE - Maximum SSH command payload length in bytes.
 *
 * This is the physical maximum length of the protocol. Implementations may
 * set a more constrained limit.
 */
#define SSH_COMMAND_MAX_PAYLOAD_SIZE \
	(SSH_FRAME_MAX_PAYLOAD_SIZE - sizeof(struct ssh_command))

/*
 * SSH_MSG_LEN_BASE - Base-length of a SSH message.
 *
 * This is the minimum number of bytes required to form a message. The actual
 * message length is SSH_MSG_LEN_BASE plus the length of the frame payload.
 */
#define SSH_MSG_LEN_BASE	(sizeof(struct ssh_frame) + 3ull * sizeof(u16))

/*
 * SSH_MSG_LEN_CTRL - Length of a SSH control message.
 *
 * This is the length of a SSH control message, which is equal to a SSH
 * message without any payload.
 */
#define SSH_MSG_LEN_CTRL	SSH_MSG_LEN_BASE

/**
 * SSH_MESSAGE_LENGTH() - Compute length of SSH message.
 * @payload_size: Length of the payload inside the SSH frame.
 *
 * Return: Returns the length of a SSH message with payload of specified size.
 */
#define SSH_MESSAGE_LENGTH(payload_size) (SSH_MSG_LEN_BASE + (payload_size))

/**
 * SSH_COMMAND_MESSAGE_LENGTH() - Compute length of SSH command message.
 * @payload_size: Length of the command payload.
 *
 * Return: Returns the length of a SSH command message with command payload of
 * specified size.
 */
#define SSH_COMMAND_MESSAGE_LENGTH(payload_size) \
	SSH_MESSAGE_LENGTH(sizeof(struct ssh_command) + (payload_size))

/**
 * SSH_MSGOFFSET_FRAME() - Compute offset in SSH message to specified field in
 * frame.
 * @field: The field for which the offset should be computed.
 *
 * Return: Returns the offset of the specified &struct ssh_frame field in the
 * raw SSH message data as. Takes SYN bytes (u16) preceding the frame into
 * account.
 */
#define SSH_MSGOFFSET_FRAME(field) \
	(sizeof(u16) + offsetof(struct ssh_frame, field))

/**
 * SSH_MSGOFFSET_COMMAND() - Compute offset in SSH message to specified field
 * in command.
 * @field: The field for which the offset should be computed.
 *
 * Return: Returns the offset of the specified &struct ssh_command field in
 * the raw SSH message data. Takes SYN bytes (u16) preceding the frame and the
 * frame CRC (u16) between frame and command into account.
 */
#define SSH_MSGOFFSET_COMMAND(field) \
	(2ull * sizeof(u16) + sizeof(struct ssh_frame) \
		+ offsetof(struct ssh_command, field))

/*
 * SSH_MSG_SYN - SSH message synchronization (SYN) bytes as u16.
 */
#define SSH_MSG_SYN		((u16)0x55aa)

/**
 * ssh_crc() - Compute CRC for SSH messages.
 * @buf: The pointer pointing to the data for which the CRC should be computed.
 * @len: The length of the data for which the CRC should be computed.
 *
 * Return: Returns the CRC computed on the provided data, as used for SSH
 * messages.
 */
static inline u16 ssh_crc(const u8 *buf, size_t len)
{
	return crc_ccitt_false(0xffff, buf, len);
}

/*
 * SSH_NUM_EVENTS - The number of reserved event IDs.
 *
 * The number of reserved event IDs, used for registering an SSH event
 * handler. Valid event IDs are numbers below or equal to this value, with
 * exception of zero, which is not an event ID. Thus, this is also the
 * absolute maximum number of event handlers that can be registered.
 */
#define SSH_NUM_EVENTS		34

/*
 * SSH_NUM_TARGETS - The number of communication targets used in the protocol.
 */
#define SSH_NUM_TARGETS		2

/**
 * ssh_rqid_next_valid() - Return the next valid request ID.
 * @rqid: The current request ID.
 *
 * Return: Returns the next valid request ID, following the current request ID
 * provided to this function. This function skips any request IDs reserved for
 * events.
 */
static inline u16 ssh_rqid_next_valid(u16 rqid)
{
	return rqid > 0 ? rqid + 1u : rqid + SSH_NUM_EVENTS + 1u;
}

/**
 * ssh_rqid_to_event() - Convert request ID to its corresponding event ID.
 * @rqid: The request ID to convert.
 */
static inline u16 ssh_rqid_to_event(u16 rqid)
{
	return rqid - 1u;
}

/**
 * ssh_rqid_is_event() - Check if given request ID is a valid event ID.
 * @rqid: The request ID to check.
 */
static inline bool ssh_rqid_is_event(u16 rqid)
{
	return ssh_rqid_to_event(rqid) < SSH_NUM_EVENTS;
}

/**
 * ssh_tc_to_rqid() - Convert target category to its corresponding request ID.
 * @tc: The target category to convert.
 */
static inline u16 ssh_tc_to_rqid(u8 tc)
{
	return tc;
}

/**
 * ssh_tid_to_index() - Convert target ID to its corresponding target index.
 * @tid: The target ID to convert.
 */
static inline u8 ssh_tid_to_index(u8 tid)
{
	return tid - 1u;
}

/**
 * ssh_tid_is_valid() - Check if target ID is valid/supported.
 * @tid: The target ID to check.
 */
static inline bool ssh_tid_is_valid(u8 tid)
{
	return ssh_tid_to_index(tid) < SSH_NUM_TARGETS;
}

/**
 * struct ssam_span - Reference to a buffer region.
 * @ptr: Pointer to the buffer region.
 * @len: Length of the buffer region.
 *
 * A reference to a (non-owned) buffer segment, consisting of pointer and
 * length. Use of this struct indicates non-owned data, i.e. data of which the
 * life-time is managed (i.e. it is allocated/freed) via another pointer.
 */
struct ssam_span {
	u8    *ptr;
	size_t len;
};

/*
 * Known SSH/EC target categories.
 *
 * List of currently known target category values; "Known" as in we know they
 * exist and are valid on at least some device/model. Detailed functionality
 * or the full category name is only known for some of these categories and
 * is detailed in the respective comment below.
 *
 * These values and abbreviations have been extracted from strings inside the
 * Windows driver.
 */
enum ssam_ssh_tc {
				/* Category 0x00 is invalid for EC use. */
	SSAM_SSH_TC_SAM = 0x01,	/* Generic system functionality, real-time clock. */
	SSAM_SSH_TC_BAT = 0x02,	/* Battery/power subsystem. */
	SSAM_SSH_TC_TMP = 0x03,	/* Thermal subsystem. */
	SSAM_SSH_TC_PMC = 0x04,
	SSAM_SSH_TC_FAN = 0x05,
	SSAM_SSH_TC_PoM = 0x06,
	SSAM_SSH_TC_DBG = 0x07,
	SSAM_SSH_TC_KBD = 0x08,	/* Legacy keyboard (Laptop 1/2). */
	SSAM_SSH_TC_FWU = 0x09,
	SSAM_SSH_TC_UNI = 0x0a,
	SSAM_SSH_TC_LPC = 0x0b,
	SSAM_SSH_TC_TCL = 0x0c,
	SSAM_SSH_TC_SFL = 0x0d,
	SSAM_SSH_TC_KIP = 0x0e,
	SSAM_SSH_TC_EXT = 0x0f,
	SSAM_SSH_TC_BLD = 0x10,
	SSAM_SSH_TC_BAS = 0x11,	/* Detachment system (Surface Book 2/3). */
	SSAM_SSH_TC_SEN = 0x12,
	SSAM_SSH_TC_SRQ = 0x13,
	SSAM_SSH_TC_MCU = 0x14,
	SSAM_SSH_TC_HID = 0x15,	/* Generic HID input subsystem. */
	SSAM_SSH_TC_TCH = 0x16,
	SSAM_SSH_TC_BKL = 0x17,
	SSAM_SSH_TC_TAM = 0x18,
	SSAM_SSH_TC_ACC = 0x19,
	SSAM_SSH_TC_UFI = 0x1a,
	SSAM_SSH_TC_USC = 0x1b,
	SSAM_SSH_TC_PEN = 0x1c,
	SSAM_SSH_TC_VID = 0x1d,
	SSAM_SSH_TC_AUD = 0x1e,
	SSAM_SSH_TC_SMC = 0x1f,
	SSAM_SSH_TC_KPD = 0x20,
	SSAM_SSH_TC_REG = 0x21,	/* Extended event registry. */
};


/* -- Packet transport layer (ptl). ----------------------------------------- */

/**
 * enum ssh_packet_base_priority - Base priorities for &struct ssh_packet.
 * @SSH_PACKET_PRIORITY_FLUSH: Base priority for flush packets.
 * @SSH_PACKET_PRIORITY_DATA:  Base priority for normal data packets.
 * @SSH_PACKET_PRIORITY_NAK:   Base priority for NAK packets.
 * @SSH_PACKET_PRIORITY_ACK:   Base priority for ACK packets.
 */
enum ssh_packet_base_priority {
	SSH_PACKET_PRIORITY_FLUSH = 0,	/* same as DATA to sequence flush */
	SSH_PACKET_PRIORITY_DATA  = 0,
	SSH_PACKET_PRIORITY_NAK   = 1,
	SSH_PACKET_PRIORITY_ACK   = 2,
};

/*
 * Same as SSH_PACKET_PRIORITY() below, only with actual values.
 */
#define __SSH_PACKET_PRIORITY(base, try) \
	(((base) << 4) | ((try) & 0x0f))

/**
 * SSH_PACKET_PRIORITY() - Compute packet priority from base priority and
 * number of tries.
 * @base: The base priority as suffix of &enum ssh_packet_base_priority, e.g.
 *        ``FLUSH``, ``DATA``, ``ACK``, or ``NAK``.
 * @try:  The number of tries (must be less than 16).
 *
 * Compute the combined packet priority. The combined priority is dominated by
 * the base priority, whereas the number of (re-)tries decides the precedence
 * of packets with the same base priority, giving higher priority to packets
 * that already have more tries.
 *
 * Return: Returns the computed priority as value fitting inside a &u8. A
 * higher number means a higher priority.
 */
#define SSH_PACKET_PRIORITY(base, try) \
	__SSH_PACKET_PRIORITY(SSH_PACKET_PRIORITY_##base, (try))

/**
 * ssh_packet_priority_get_try() - Get number of tries from packet priority.
 * @priority: The packet priority.
 *
 * Return: Returns the number of tries encoded in the specified packet
 * priority.
 */
static inline u8 ssh_packet_priority_get_try(u8 priority)
{
	return priority & 0x0f;
}

/**
 * ssh_packet_priority_get_base - Get base priority from packet priority.
 * @priority: The packet priority.
 *
 * Return: Returns the base priority encoded in the given packet priority.
 */
static inline u8 ssh_packet_priority_get_base(u8 priority)
{
	return (priority & 0xf0) >> 4;
}

enum ssh_packet_flags {
	/* state flags */
	SSH_PACKET_SF_LOCKED_BIT,
	SSH_PACKET_SF_QUEUED_BIT,
	SSH_PACKET_SF_PENDING_BIT,
	SSH_PACKET_SF_TRANSMITTING_BIT,
	SSH_PACKET_SF_TRANSMITTED_BIT,
	SSH_PACKET_SF_ACKED_BIT,
	SSH_PACKET_SF_CANCELED_BIT,
	SSH_PACKET_SF_COMPLETED_BIT,

	/* type flags */
	SSH_PACKET_TY_FLUSH_BIT,
	SSH_PACKET_TY_SEQUENCED_BIT,
	SSH_PACKET_TY_BLOCKING_BIT,

	/* mask for state flags */
	SSH_PACKET_FLAGS_SF_MASK =
		  BIT(SSH_PACKET_SF_LOCKED_BIT)
		| BIT(SSH_PACKET_SF_QUEUED_BIT)
		| BIT(SSH_PACKET_SF_PENDING_BIT)
		| BIT(SSH_PACKET_SF_TRANSMITTING_BIT)
		| BIT(SSH_PACKET_SF_TRANSMITTED_BIT)
		| BIT(SSH_PACKET_SF_ACKED_BIT)
		| BIT(SSH_PACKET_SF_CANCELED_BIT)
		| BIT(SSH_PACKET_SF_COMPLETED_BIT),

	/* mask for type flags */
	SSH_PACKET_FLAGS_TY_MASK =
		  BIT(SSH_PACKET_TY_FLUSH_BIT)
		| BIT(SSH_PACKET_TY_SEQUENCED_BIT)
		| BIT(SSH_PACKET_TY_BLOCKING_BIT),
};

struct ssh_ptl;
struct ssh_packet;

/**
 * struct ssh_packet_ops - Callback operations for a SSH packet.
 * @release:  Function called when the packet reference count reaches zero.
 *            This callback must be relied upon to ensure that the packet has
 *            left the transport system(s).
 * @complete: Function called when the packet is completed, either with
 *            success or failure. In case of failure, the reason for the
 *            failure is indicated by the value of the provided status code
 *            argument. This value will be zero in case of success. Note that
 *            a call to this callback does not guarantee that the packet is
 *            not in use by the transport system any more.
 */
struct ssh_packet_ops {
	void (*release)(struct ssh_packet *p);
	void (*complete)(struct ssh_packet *p, int status);
};

/**
 * struct ssh_packet - SSH transport packet.
 * @ptl:      Pointer to the packet transport layer. May be %NULL if the packet
 *            (or enclosing request) has not been submitted yet.
 * @refcnt:   Reference count of the packet.
 * @priority: Priority of the packet. Must be computed via
 *            SSH_PACKET_PRIORITY(). Must only be accessed while holding the
 *            queue lock after first submission.
 * @data:     Raw message data.
 * @data.len: Length of the raw message data.
 * @data.ptr: Pointer to the raw message data buffer.
 * @state:    State and type flags describing current packet state (dynamic)
 *            and type (static). See &enum ssh_packet_flags for possible
 *            options.
 * @timestamp: Timestamp specifying when the latest transmission of a
 *            currently pending packet has been started. May be %KTIME_MAX
 *            before or in-between transmission attempts. Used for the packet
 *            timeout implementation. Must only be accessed while holding the
 *            pending lock after first submission.
 * @queue_node:	The list node for the packet queue.
 * @pending_node: The list node for the set of pending packets.
 * @ops:      Packet operations.
 */
struct ssh_packet {
	struct ssh_ptl *ptl;
	struct kref refcnt;

	u8 priority;

	struct {
		size_t len;
		u8 *ptr;
	} data;

	unsigned long state;
	ktime_t timestamp;

	struct list_head queue_node;
	struct list_head pending_node;

	const struct ssh_packet_ops *ops;
};

struct ssh_packet *ssh_packet_get(struct ssh_packet *p);
void ssh_packet_put(struct ssh_packet *p);

/**
 * ssh_packet_set_data() - Set raw message data of packet.
 * @p:   The packet for which the message data should be set.
 * @ptr: Pointer to the memory holding the message data.
 * @len: Length of the message data.
 *
 * Sets the raw message data buffer of the packet to the provided memory. The
 * memory is not copied. Instead, the caller is responsible for management
 * (i.e. allocation and deallocation) of the memory. The caller must ensure
 * that the provided memory is valid and contains a valid SSH message,
 * starting from the time of submission of the packet until the ``release``
 * callback has been called. During this time, the memory may not be altered
 * in any way.
 */
static inline void ssh_packet_set_data(struct ssh_packet *p, u8 *ptr, size_t len)
{
	p->data.ptr = ptr;
	p->data.len = len;
}


/* -- Request transport layer (rtl). ---------------------------------------- */

enum ssh_request_flags {
	/* state flags */
	SSH_REQUEST_SF_LOCKED_BIT,
	SSH_REQUEST_SF_QUEUED_BIT,
	SSH_REQUEST_SF_PENDING_BIT,
	SSH_REQUEST_SF_TRANSMITTING_BIT,
	SSH_REQUEST_SF_TRANSMITTED_BIT,
	SSH_REQUEST_SF_RSPRCVD_BIT,
	SSH_REQUEST_SF_CANCELED_BIT,
	SSH_REQUEST_SF_COMPLETED_BIT,

	/* type flags */
	SSH_REQUEST_TY_FLUSH_BIT,
	SSH_REQUEST_TY_HAS_RESPONSE_BIT,

	/* mask for state flags */
	SSH_REQUEST_FLAGS_SF_MASK =
		  BIT(SSH_REQUEST_SF_LOCKED_BIT)
		| BIT(SSH_REQUEST_SF_QUEUED_BIT)
		| BIT(SSH_REQUEST_SF_PENDING_BIT)
		| BIT(SSH_REQUEST_SF_TRANSMITTING_BIT)
		| BIT(SSH_REQUEST_SF_TRANSMITTED_BIT)
		| BIT(SSH_REQUEST_SF_RSPRCVD_BIT)
		| BIT(SSH_REQUEST_SF_CANCELED_BIT)
		| BIT(SSH_REQUEST_SF_COMPLETED_BIT),

	/* mask for type flags */
	SSH_REQUEST_FLAGS_TY_MASK =
		  BIT(SSH_REQUEST_TY_FLUSH_BIT)
		| BIT(SSH_REQUEST_TY_HAS_RESPONSE_BIT),
};

struct ssh_rtl;
struct ssh_request;

/**
 * struct ssh_request_ops - Callback operations for a SSH request.
 * @release:  Function called when the request's reference count reaches zero.
 *            This callback must be relied upon to ensure that the request has
 *            left the transport systems (both, packet an request systems).
 * @complete: Function called when the request is completed, either with
 *            success or failure. The command data for the request response
 *            is provided via the &struct ssh_command parameter (``cmd``),
 *            the command payload of the request response via the &struct
 *            ssh_span parameter (``data``).
 *
 *            If the request does not have any response or has not been
 *            completed with success, both ``cmd`` and ``data`` parameters will
 *            be NULL. If the request response does not have any command
 *            payload, the ``data`` span will be an empty (zero-length) span.
 *
 *            In case of failure, the reason for the failure is indicated by
 *            the value of the provided status code argument (``status``). This
 *            value will be zero in case of success and a regular errno
 *            otherwise.
 *
 *            Note that a call to this callback does not guarantee that the
 *            request is not in use by the transport systems any more.
 */
struct ssh_request_ops {
	void (*release)(struct ssh_request *rqst);
	void (*complete)(struct ssh_request *rqst,
			 const struct ssh_command *cmd,
			 const struct ssam_span *data, int status);
};

/**
 * struct ssh_request - SSH transport request.
 * @packet: The underlying SSH transport packet.
 * @node:   List node for the request queue and pending set.
 * @state:  State and type flags describing current request state (dynamic)
 *          and type (static). See &enum ssh_request_flags for possible
 *          options.
 * @timestamp: Timestamp specifying when we start waiting on the response of
 *          the request. This is set once the underlying packet has been
 *          completed and may be %KTIME_MAX before that, or when the request
 *          does not expect a response. Used for the request timeout
 *          implementation.
 * @ops:    Request Operations.
 */
struct ssh_request {
	struct ssh_packet packet;
	struct list_head node;

	unsigned long state;
	ktime_t timestamp;

	const struct ssh_request_ops *ops;
};

/**
 * to_ssh_request() - Cast a SSH packet to its enclosing SSH request.
 * @p: The packet to cast.
 *
 * Casts the given &struct ssh_packet to its enclosing &struct ssh_request.
 * The caller is responsible for making sure that the packet is actually
 * wrapped in a &struct ssh_request.
 *
 * Return: Returns the &struct ssh_request wrapping the provided packet.
 */
static inline struct ssh_request *to_ssh_request(struct ssh_packet *p)
{
	return container_of(p, struct ssh_request, packet);
}

/**
 * ssh_request_get() - Increment reference count of request.
 * @r: The request to increment the reference count of.
 *
 * Increments the reference count of the given request by incrementing the
 * reference count of the underlying &struct ssh_packet, enclosed in it.
 *
 * See also ssh_request_put(), ssh_packet_get().
 *
 * Return: Returns the request provided as input.
 */
static inline struct ssh_request *ssh_request_get(struct ssh_request *r)
{
	return r ? to_ssh_request(ssh_packet_get(&r->packet)) : NULL;
}

/**
 * ssh_request_put() - Decrement reference count of request.
 * @r: The request to decrement the reference count of.
 *
 * Decrements the reference count of the given request by decrementing the
 * reference count of the underlying &struct ssh_packet, enclosed in it. If
 * the reference count reaches zero, the ``release`` callback specified in the
 * request's &struct ssh_request_ops, i.e. ``r->ops->release``, will be
 * called.
 *
 * See also ssh_request_get(), ssh_packet_put().
 */
static inline void ssh_request_put(struct ssh_request *r)
{
	if (r)
		ssh_packet_put(&r->packet);
}

/**
 * ssh_request_set_data() - Set raw message data of request.
 * @r:   The request for which the message data should be set.
 * @ptr: Pointer to the memory holding the message data.
 * @len: Length of the message data.
 *
 * Sets the raw message data buffer of the underlying packet to the specified
 * buffer. Does not copy the actual message data, just sets the buffer pointer
 * and length. Refer to ssh_packet_set_data() for more details.
 */
static inline void ssh_request_set_data(struct ssh_request *r, u8 *ptr, size_t len)
{
	ssh_packet_set_data(&r->packet, ptr, len);
}

#endif /* _LINUX_SURFACE_AGGREGATOR_SERIAL_HUB_H */
