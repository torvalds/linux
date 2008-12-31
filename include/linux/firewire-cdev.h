/*
 * Char device interface.
 *
 * Copyright (C) 2005-2006  Kristian Hoegsberg <krh@bitplanet.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _LINUX_FIREWIRE_CDEV_H
#define _LINUX_FIREWIRE_CDEV_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/firewire-constants.h>

#define FW_CDEV_EVENT_BUS_RESET		0x00
#define FW_CDEV_EVENT_RESPONSE		0x01
#define FW_CDEV_EVENT_REQUEST		0x02
#define FW_CDEV_EVENT_ISO_INTERRUPT	0x03

/**
 * struct fw_cdev_event_common - Common part of all fw_cdev_event_ types
 * @closure:	For arbitrary use by userspace
 * @type:	Discriminates the fw_cdev_event_ types
 *
 * This struct may be used to access generic members of all fw_cdev_event_
 * types regardless of the specific type.
 *
 * Data passed in the @closure field for a request will be returned in the
 * corresponding event.  It is big enough to hold a pointer on all platforms.
 * The ioctl used to set @closure depends on the @type of event.
 */
struct fw_cdev_event_common {
	__u64 closure;
	__u32 type;
};

/**
 * struct fw_cdev_event_bus_reset - Sent when a bus reset occurred
 * @closure:	See &fw_cdev_event_common; set by %FW_CDEV_IOC_GET_INFO ioctl
 * @type:	See &fw_cdev_event_common; always %FW_CDEV_EVENT_BUS_RESET
 * @node_id:       New node ID of this node
 * @local_node_id: Node ID of the local node, i.e. of the controller
 * @bm_node_id:    Node ID of the bus manager
 * @irm_node_id:   Node ID of the iso resource manager
 * @root_node_id:  Node ID of the root node
 * @generation:    New bus generation
 *
 * This event is sent when the bus the device belongs to goes through a bus
 * reset.  It provides information about the new bus configuration, such as
 * new node ID for this device, new root ID, and others.
 */
struct fw_cdev_event_bus_reset {
	__u64 closure;
	__u32 type;
	__u32 node_id;
	__u32 local_node_id;
	__u32 bm_node_id;
	__u32 irm_node_id;
	__u32 root_node_id;
	__u32 generation;
};

/**
 * struct fw_cdev_event_response - Sent when a response packet was received
 * @closure:	See &fw_cdev_event_common;
 *		set by %FW_CDEV_IOC_SEND_REQUEST ioctl
 * @type:	See &fw_cdev_event_common; always %FW_CDEV_EVENT_RESPONSE
 * @rcode:	Response code returned by the remote node
 * @length:	Data length, i.e. the response's payload size in bytes
 * @data:	Payload data, if any
 *
 * This event is sent when the stack receives a response to an outgoing request
 * sent by %FW_CDEV_IOC_SEND_REQUEST ioctl.  The payload data for responses
 * carrying data (read and lock responses) follows immediately and can be
 * accessed through the @data field.
 */
struct fw_cdev_event_response {
	__u64 closure;
	__u32 type;
	__u32 rcode;
	__u32 length;
	__u32 data[0];
};

/**
 * struct fw_cdev_event_request - Sent on incoming request to an address region
 * @closure:	See &fw_cdev_event_common; set by %FW_CDEV_IOC_ALLOCATE ioctl
 * @type:	See &fw_cdev_event_common; always %FW_CDEV_EVENT_REQUEST
 * @tcode:	Transaction code of the incoming request
 * @offset:	The offset into the 48-bit per-node address space
 * @handle:	Reference to the kernel-side pending request
 * @length:	Data length, i.e. the request's payload size in bytes
 * @data:	Incoming data, if any
 *
 * This event is sent when the stack receives an incoming request to an address
 * region registered using the %FW_CDEV_IOC_ALLOCATE ioctl.  The request is
 * guaranteed to be completely contained in the specified region.  Userspace is
 * responsible for sending the response by %FW_CDEV_IOC_SEND_RESPONSE ioctl,
 * using the same @handle.
 *
 * The payload data for requests carrying data (write and lock requests)
 * follows immediately and can be accessed through the @data field.
 */
struct fw_cdev_event_request {
	__u64 closure;
	__u32 type;
	__u32 tcode;
	__u64 offset;
	__u32 handle;
	__u32 length;
	__u32 data[0];
};

/**
 * struct fw_cdev_event_iso_interrupt - Sent when an iso packet was completed
 * @closure:	See &fw_cdev_event_common;
 *		set by %FW_CDEV_CREATE_ISO_CONTEXT ioctl
 * @type:	See &fw_cdev_event_common; always %FW_CDEV_EVENT_ISO_INTERRUPT
 * @cycle:	Cycle counter of the interrupt packet
 * @header_length: Total length of following headers, in bytes
 * @header:	Stripped headers, if any
 *
 * This event is sent when the controller has completed an &fw_cdev_iso_packet
 * with the %FW_CDEV_ISO_INTERRUPT bit set.  In the receive case, the headers
 * stripped of all packets up until and including the interrupt packet are
 * returned in the @header field.
 */
struct fw_cdev_event_iso_interrupt {
	__u64 closure;
	__u32 type;
	__u32 cycle;
	__u32 header_length;
	__u32 header[0];
};

/**
 * union fw_cdev_event - Convenience union of fw_cdev_event_ types
 * @common:        Valid for all types
 * @bus_reset:     Valid if @common.type == %FW_CDEV_EVENT_BUS_RESET
 * @response:      Valid if @common.type == %FW_CDEV_EVENT_RESPONSE
 * @request:       Valid if @common.type == %FW_CDEV_EVENT_REQUEST
 * @iso_interrupt: Valid if @common.type == %FW_CDEV_EVENT_ISO_INTERRUPT
 *
 * Convenience union for userspace use.  Events could be read(2) into an
 * appropriately aligned char buffer and then cast to this union for further
 * processing.  Note that for a request, response or iso_interrupt event,
 * the data[] or header[] may make the size of the full event larger than
 * sizeof(union fw_cdev_event).  Also note that if you attempt to read(2)
 * an event into a buffer that is not large enough for it, the data that does
 * not fit will be discarded so that the next read(2) will return a new event.
 */
union fw_cdev_event {
	struct fw_cdev_event_common common;
	struct fw_cdev_event_bus_reset bus_reset;
	struct fw_cdev_event_response response;
	struct fw_cdev_event_request request;
	struct fw_cdev_event_iso_interrupt iso_interrupt;
};

#define FW_CDEV_IOC_GET_INFO		_IOWR('#', 0x00, struct fw_cdev_get_info)
#define FW_CDEV_IOC_SEND_REQUEST	_IOW('#', 0x01, struct fw_cdev_send_request)
#define FW_CDEV_IOC_ALLOCATE		_IOWR('#', 0x02, struct fw_cdev_allocate)
#define FW_CDEV_IOC_DEALLOCATE		_IOW('#', 0x03, struct fw_cdev_deallocate)
#define FW_CDEV_IOC_SEND_RESPONSE	_IOW('#', 0x04, struct fw_cdev_send_response)
#define FW_CDEV_IOC_INITIATE_BUS_RESET	_IOW('#', 0x05, struct fw_cdev_initiate_bus_reset)
#define FW_CDEV_IOC_ADD_DESCRIPTOR	_IOWR('#', 0x06, struct fw_cdev_add_descriptor)
#define FW_CDEV_IOC_REMOVE_DESCRIPTOR	_IOW('#', 0x07, struct fw_cdev_remove_descriptor)

#define FW_CDEV_IOC_CREATE_ISO_CONTEXT	_IOWR('#', 0x08, struct fw_cdev_create_iso_context)
#define FW_CDEV_IOC_QUEUE_ISO		_IOWR('#', 0x09, struct fw_cdev_queue_iso)
#define FW_CDEV_IOC_START_ISO		_IOW('#', 0x0a, struct fw_cdev_start_iso)
#define FW_CDEV_IOC_STOP_ISO		_IOW('#', 0x0b, struct fw_cdev_stop_iso)
#define FW_CDEV_IOC_GET_CYCLE_TIMER	_IOR('#', 0x0c, struct fw_cdev_get_cycle_timer)

/* FW_CDEV_VERSION History
 *
 * 1	Feb 18, 2007:  Initial version.
 */
#define FW_CDEV_VERSION		1

/**
 * struct fw_cdev_get_info - General purpose information ioctl
 * @version:	The version field is just a running serial number.
 *		We never break backwards compatibility, but may add more
 *		structs and ioctls in later revisions.
 * @rom_length:	If @rom is non-zero, at most rom_length bytes of configuration
 *		ROM will be copied into that user space address.  In either
 *		case, @rom_length is updated with the actual length of the
 *		configuration ROM.
 * @rom:	If non-zero, address of a buffer to be filled by a copy of the
 *		local node's configuration ROM
 * @bus_reset:	If non-zero, address of a buffer to be filled by a
 *		&struct fw_cdev_event_bus_reset with the current state
 *		of the bus.  This does not cause a bus reset to happen.
 * @bus_reset_closure: Value of &closure in this and subsequent bus reset events
 * @card:	The index of the card this device belongs to
 */
struct fw_cdev_get_info {
	__u32 version;
	__u32 rom_length;
	__u64 rom;
	__u64 bus_reset;
	__u64 bus_reset_closure;
	__u32 card;
};

/**
 * struct fw_cdev_send_request - Send an asynchronous request packet
 * @tcode:	Transaction code of the request
 * @length:	Length of outgoing payload, in bytes
 * @offset:	48-bit offset at destination node
 * @closure:	Passed back to userspace in the response event
 * @data:	Userspace pointer to payload
 * @generation:	The bus generation where packet is valid
 *
 * Send a request to the device.  This ioctl implements all outgoing requests.
 * Both quadlet and block request specify the payload as a pointer to the data
 * in the @data field.  Once the transaction completes, the kernel writes an
 * &fw_cdev_event_request event back.  The @closure field is passed back to
 * user space in the response event.
 */
struct fw_cdev_send_request {
	__u32 tcode;
	__u32 length;
	__u64 offset;
	__u64 closure;
	__u64 data;
	__u32 generation;
};

/**
 * struct fw_cdev_send_response - Send an asynchronous response packet
 * @rcode:	Response code as determined by the userspace handler
 * @length:	Length of outgoing payload, in bytes
 * @data:	Userspace pointer to payload
 * @handle:	The handle from the &fw_cdev_event_request
 *
 * Send a response to an incoming request.  By setting up an address range using
 * the %FW_CDEV_IOC_ALLOCATE ioctl, userspace can listen for incoming requests.  An
 * incoming request will generate an %FW_CDEV_EVENT_REQUEST, and userspace must
 * send a reply using this ioctl.  The event has a handle to the kernel-side
 * pending transaction, which should be used with this ioctl.
 */
struct fw_cdev_send_response {
	__u32 rcode;
	__u32 length;
	__u64 data;
	__u32 handle;
};

/**
 * struct fw_cdev_allocate - Allocate a CSR address range
 * @offset:	Start offset of the address range
 * @closure:	To be passed back to userspace in request events
 * @length:	Length of the address range, in bytes
 * @handle:	Handle to the allocation, written by the kernel
 *
 * Allocate an address range in the 48-bit address space on the local node
 * (the controller).  This allows userspace to listen for requests with an
 * offset within that address range.  When the kernel receives a request
 * within the range, an &fw_cdev_event_request event will be written back.
 * The @closure field is passed back to userspace in the response event.
 * The @handle field is an out parameter, returning a handle to the allocated
 * range to be used for later deallocation of the range.
 */
struct fw_cdev_allocate {
	__u64 offset;
	__u64 closure;
	__u32 length;
	__u32 handle;
};

/**
 * struct fw_cdev_deallocate - Free an address range allocation
 * @handle:	Handle to the address range, as returned by the kernel when the
 *		range was allocated
 */
struct fw_cdev_deallocate {
	__u32 handle;
};

#define FW_CDEV_LONG_RESET	0
#define FW_CDEV_SHORT_RESET	1

/**
 * struct fw_cdev_initiate_bus_reset - Initiate a bus reset
 * @type:	%FW_CDEV_SHORT_RESET or %FW_CDEV_LONG_RESET
 *
 * Initiate a bus reset for the bus this device is on.  The bus reset can be
 * either the original (long) bus reset or the arbitrated (short) bus reset
 * introduced in 1394a-2000.
 */
struct fw_cdev_initiate_bus_reset {
	__u32 type;	/* FW_CDEV_SHORT_RESET or FW_CDEV_LONG_RESET */
};

/**
 * struct fw_cdev_add_descriptor - Add contents to the local node's config ROM
 * @immediate:	If non-zero, immediate key to insert before pointer
 * @key:	Upper 8 bits of root directory pointer
 * @data:	Userspace pointer to contents of descriptor block
 * @length:	Length of descriptor block data, in bytes
 * @handle:	Handle to the descriptor, written by the kernel
 *
 * Add a descriptor block and optionally a preceding immediate key to the local
 * node's configuration ROM.
 *
 * The @key field specifies the upper 8 bits of the descriptor root directory
 * pointer and the @data and @length fields specify the contents. The @key
 * should be of the form 0xXX000000. The offset part of the root directory entry
 * will be filled in by the kernel.
 *
 * If not 0, the @immediate field specifies an immediate key which will be
 * inserted before the root directory pointer.
 *
 * If successful, the kernel adds the descriptor and writes back a handle to the
 * kernel-side object to be used for later removal of the descriptor block and
 * immediate key.
 */
struct fw_cdev_add_descriptor {
	__u32 immediate;
	__u32 key;
	__u64 data;
	__u32 length;
	__u32 handle;
};

/**
 * struct fw_cdev_remove_descriptor - Remove contents from the configuration ROM
 * @handle:	Handle to the descriptor, as returned by the kernel when the
 *		descriptor was added
 *
 * Remove a descriptor block and accompanying immediate key from the local
 * node's configuration ROM.
 */
struct fw_cdev_remove_descriptor {
	__u32 handle;
};

#define FW_CDEV_ISO_CONTEXT_TRANSMIT	0
#define FW_CDEV_ISO_CONTEXT_RECEIVE	1

/**
 * struct fw_cdev_create_iso_context - Create a context for isochronous IO
 * @type:	%FW_CDEV_ISO_CONTEXT_TRANSMIT or %FW_CDEV_ISO_CONTEXT_RECEIVE
 * @header_size: Header size to strip for receive contexts
 * @channel:	Channel to bind to
 * @speed:	Speed to transmit at
 * @closure:	To be returned in &fw_cdev_event_iso_interrupt
 * @handle:	Handle to context, written back by kernel
 *
 * Prior to sending or receiving isochronous I/O, a context must be created.
 * The context records information about the transmit or receive configuration
 * and typically maps to an underlying hardware resource.  A context is set up
 * for either sending or receiving.  It is bound to a specific isochronous
 * channel.
 *
 * If a context was successfully created, the kernel writes back a handle to the
 * context, which must be passed in for subsequent operations on that context.
 */
struct fw_cdev_create_iso_context {
	__u32 type;
	__u32 header_size;
	__u32 channel;
	__u32 speed;
	__u64 closure;
	__u32 handle;
};

#define FW_CDEV_ISO_PAYLOAD_LENGTH(v)	(v)
#define FW_CDEV_ISO_INTERRUPT		(1 << 16)
#define FW_CDEV_ISO_SKIP		(1 << 17)
#define FW_CDEV_ISO_SYNC		(1 << 17)
#define FW_CDEV_ISO_TAG(v)		((v) << 18)
#define FW_CDEV_ISO_SY(v)		((v) << 20)
#define FW_CDEV_ISO_HEADER_LENGTH(v)	((v) << 24)

/**
 * struct fw_cdev_iso_packet - Isochronous packet
 * @control:	Contains the header length (8 uppermost bits), the sy field
 *		(4 bits), the tag field (2 bits), a sync flag (1 bit),
 *		a skip flag (1 bit), an interrupt flag (1 bit), and the
 *		payload length (16 lowermost bits)
 * @header:	Header and payload
 *
 * &struct fw_cdev_iso_packet is used to describe isochronous packet queues.
 *
 * Use the FW_CDEV_ISO_ macros to fill in @control.  The sy and tag fields are
 * specified by IEEE 1394a and IEC 61883.
 *
 * FIXME - finish this documentation
 */
struct fw_cdev_iso_packet {
	__u32 control;
	__u32 header[0];
};

/**
 * struct fw_cdev_queue_iso - Queue isochronous packets for I/O
 * @packets:	Userspace pointer to packet data
 * @data:	Pointer into mmap()'ed payload buffer
 * @size:	Size of packet data in bytes
 * @handle:	Isochronous context handle
 *
 * Queue a number of isochronous packets for reception or transmission.
 * This ioctl takes a pointer to an array of &fw_cdev_iso_packet structs,
 * which describe how to transmit from or receive into a contiguous region
 * of a mmap()'ed payload buffer.  As part of the packet descriptors,
 * a series of headers can be supplied, which will be prepended to the
 * payload during DMA.
 *
 * The kernel may or may not queue all packets, but will write back updated
 * values of the @packets, @data and @size fields, so the ioctl can be
 * resubmitted easily.
 */
struct fw_cdev_queue_iso {
	__u64 packets;
	__u64 data;
	__u32 size;
	__u32 handle;
};

#define FW_CDEV_ISO_CONTEXT_MATCH_TAG0		 1
#define FW_CDEV_ISO_CONTEXT_MATCH_TAG1		 2
#define FW_CDEV_ISO_CONTEXT_MATCH_TAG2		 4
#define FW_CDEV_ISO_CONTEXT_MATCH_TAG3		 8
#define FW_CDEV_ISO_CONTEXT_MATCH_ALL_TAGS	15

/**
 * struct fw_cdev_start_iso - Start an isochronous transmission or reception
 * @cycle:	Cycle in which to start I/O.  If @cycle is greater than or
 *		equal to 0, the I/O will start on that cycle.
 * @sync:	Determines the value to wait for for receive packets that have
 *		the %FW_CDEV_ISO_SYNC bit set
 * @tags:	Tag filter bit mask.  Only valid for isochronous reception.
 *		Determines the tag values for which packets will be accepted.
 *		Use FW_CDEV_ISO_CONTEXT_MATCH_ macros to set @tags.
 * @handle:	Isochronous context handle within which to transmit or receive
 */
struct fw_cdev_start_iso {
	__s32 cycle;
	__u32 sync;
	__u32 tags;
	__u32 handle;
};

/**
 * struct fw_cdev_stop_iso - Stop an isochronous transmission or reception
 * @handle:	Handle of isochronous context to stop
 */
struct fw_cdev_stop_iso {
	__u32 handle;
};

/**
 * struct fw_cdev_get_cycle_timer - read cycle timer register
 * @local_time:   system time, in microseconds since the Epoch
 * @cycle_timer:  isochronous cycle timer, as per OHCI 1.1 clause 5.13
 *
 * The %FW_CDEV_IOC_GET_CYCLE_TIMER ioctl reads the isochronous cycle timer
 * and also the system clock.  This allows to express the receive time of an
 * isochronous packet as a system time with microsecond accuracy.
 */
struct fw_cdev_get_cycle_timer {
	__u64 local_time;
	__u32 cycle_timer;
};

#endif /* _LINUX_FIREWIRE_CDEV_H */
