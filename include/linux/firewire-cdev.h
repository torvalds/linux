/*						-*- c-basic-offset: 8 -*-
 *
 * fw-device-cdev.h -- Char device interface.
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

#ifndef __fw_cdev_h
#define __fw_cdev_h

#include <linux/ioctl.h>
#include <linux/types.h>

#define TCODE_WRITE_QUADLET_REQUEST	0
#define TCODE_WRITE_BLOCK_REQUEST	1
#define TCODE_WRITE_RESPONSE		2
#define TCODE_READ_QUADLET_REQUEST	4
#define TCODE_READ_BLOCK_REQUEST	5
#define TCODE_READ_QUADLET_RESPONSE	6
#define TCODE_READ_BLOCK_RESPONSE	7
#define TCODE_CYCLE_START		8
#define TCODE_LOCK_REQUEST		9
#define TCODE_STREAM_DATA		10
#define TCODE_LOCK_RESPONSE		11

#define TCODE_LOCK_MASK_SWAP		0x11
#define TCODE_LOCK_COMPARE_SWAP		0x12
#define TCODE_LOCK_FETCH_ADD		0x13
#define TCODE_LOCK_LITTLE_ADD		0x14
#define TCODE_LOCK_BOUNDED_ADD		0x15
#define TCODE_LOCK_WRAP_ADD		0x16
#define TCODE_LOCK_VENDOR_DEPENDENT	0x17

#define RCODE_COMPLETE			0x0
#define RCODE_CONFLICT_ERROR		0x4
#define RCODE_DATA_ERROR		0x5
#define RCODE_TYPE_ERROR		0x6
#define RCODE_ADDRESS_ERROR		0x7

#define RCODE_SEND_ERROR		0x10
#define RCODE_CANCELLED			0x11
#define RCODE_BUSY			0x12
#define RCODE_GENERATION		0x13
#define RCODE_NO_ACK			0x14

#define SCODE_100			0x0
#define SCODE_200			0x1
#define SCODE_400			0x2
#define SCODE_800			0x3
#define SCODE_1600			0x4
#define SCODE_3200			0x5

#define FW_CDEV_EVENT_BUS_RESET		0x00
#define FW_CDEV_EVENT_RESPONSE		0x01
#define FW_CDEV_EVENT_REQUEST		0x02
#define FW_CDEV_EVENT_ISO_INTERRUPT	0x03

/* The 'closure' fields are for user space to use.  Data passed in the
 * 'closure' field for a request will be returned in the corresponding
 * event.  It's a 64-bit type so that it's a fixed size type big
 * enough to hold a pointer on all platforms. */

struct fw_cdev_event_common {
	__u64 closure;
	__u32 type;
};

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

struct fw_cdev_event_response {
	__u64 closure;
	__u32 type;
	__u32 rcode;
	__u32 length;
	__u32 data[0];
};

struct fw_cdev_event_request {
	__u64 closure;
	__u32 type;
	__u32 tcode;
	__u64 offset;
	__u32 handle;
	__u32 length;
	__u32 data[0];
};

struct fw_cdev_event_iso_interrupt {
	__u64 closure;
	__u32 type;
	__u32 cycle;
	__u32 header_length;	/* Length in bytes of following headers. */
	__u32 header[0];
};

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

/* FW_CDEV_VERSION History
 *
 * 1	Feb 18, 2007:  Initial version.
 */
#define FW_CDEV_VERSION		1

struct fw_cdev_get_info {
	/* The version field is just a running serial number.  We
	 * never break backwards compatibility.  Userspace passes in
	 * the version it expects and the kernel passes back the
	 * highest version it can provide.  Even if the structs in
	 * this interface are extended in a later version, the kernel
	 * will not copy back more data than what was present in the
	 * interface version userspace expects. */
	__u32 version;

	/* If non-zero, at most rom_length bytes of config rom will be
	 * copied into that user space address.  In either case,
	 * rom_length is updated with the actual length of the config
	 * rom. */
	__u32 rom_length;
	__u64 rom;

	/* If non-zero, a fw_cdev_event_bus_reset struct will be
	 * copied here with the current state of the bus.  This does
	 * not cause a bus reset to happen.  The value of closure in
	 * this and sub-sequent bus reset events is set to
	 * bus_reset_closure. */
	__u64 bus_reset;
	__u64 bus_reset_closure;

	/* The index of the card this devices belongs to. */
	__u32 card;
};

struct fw_cdev_send_request {
	__u32 tcode;
	__u32 length;
	__u64 offset;
	__u64 closure;
	__u64 data;
	__u32 generation;
};

struct fw_cdev_send_response {
	__u32 rcode;
	__u32 length;
	__u64 data;
	__u32 handle;
};

struct fw_cdev_allocate {
	__u64 offset;
	__u64 closure;
	__u32 length;
	__u32 handle;
};

struct fw_cdev_deallocate {
	__u32 handle;
};

#define FW_CDEV_LONG_RESET	0
#define FW_CDEV_SHORT_RESET	1

struct fw_cdev_initiate_bus_reset {
	__u32 type;
};

struct fw_cdev_add_descriptor {
	__u32 immediate;
	__u32 key;
	__u64 data;
	__u32 length;
	__u32 handle;
};

struct fw_cdev_remove_descriptor {
	__u32 handle;
};

#define FW_CDEV_ISO_CONTEXT_TRANSMIT	0
#define FW_CDEV_ISO_CONTEXT_RECEIVE	1

#define FW_CDEV_ISO_CONTEXT_MATCH_TAG0		 1
#define FW_CDEV_ISO_CONTEXT_MATCH_TAG1		 2
#define FW_CDEV_ISO_CONTEXT_MATCH_TAG2		 4
#define FW_CDEV_ISO_CONTEXT_MATCH_TAG3		 8
#define FW_CDEV_ISO_CONTEXT_MATCH_ALL_TAGS	15

struct fw_cdev_create_iso_context {
	__u32 type;
	__u32 header_size;
	__u32 channel;
	__u32 speed;
	__u64 closure;
	__u32 handle;
};

struct fw_cdev_iso_packet {
	__u16 payload_length;	/* Length of indirect payload. */
	__u32 interrupt : 1;	/* Generate interrupt on this packet */
	__u32 skip : 1;		/* Set to not send packet at all. */
	__u32 tag : 2;
	__u32 sy : 4;
	__u32 header_length : 8;	/* Length of immediate header. */
	__u32 header[0];
};

struct fw_cdev_queue_iso {
	__u64 packets;
	__u64 data;
	__u32 size;
	__u32 handle;
};

struct fw_cdev_start_iso {
	__s32 cycle;
	__u32 sync;
	__u32 tags;
	__u32 handle;
};

struct fw_cdev_stop_iso {
	__u32 handle;
};

#endif /* __fw_cdev_h */
