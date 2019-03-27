/*-
 * rfcomm_sdp.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: rfcomm_sdp.c,v 1.1 2003/09/07 18:15:55 max Exp $
 * $FreeBSD$
 */
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <sdp.h>
#include <stdio.h>

#undef	PROTOCOL_DESCRIPTOR_LIST_BUFFER_SIZE
#define	PROTOCOL_DESCRIPTOR_LIST_BUFFER_SIZE	256

#undef	PROTOCOL_DESCRIPTOR_LIST_MINIMAL_SIZE
#define	PROTOCOL_DESCRIPTOR_LIST_MINIMAL_SIZE	12

static int rfcomm_proto_list_parse (uint8_t const *start, uint8_t const *end,
					int *channel, int *error);

/*
 * Lookup RFCOMM channel number in the Protocol Descriptor List
 */

#undef	rfcomm_channel_lookup_exit
#define	rfcomm_channel_lookup_exit(e) { \
	if (error != NULL) \
		*error = (e); \
	if (ss != NULL) { \
		sdp_close(ss); \
		ss = NULL; \
	} \
	return (((e) == 0)? 0 : -1); \
}

int
rfcomm_channel_lookup(bdaddr_t const *local, bdaddr_t const *remote,
			int service, int *channel, int *error)
{
	uint8_t		 buffer[PROTOCOL_DESCRIPTOR_LIST_BUFFER_SIZE];
	void		*ss    = NULL;
	uint16_t	 serv  = (uint16_t) service;
	uint32_t	 attr  = SDP_ATTR_RANGE(
					SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST,
					SDP_ATTR_PROTOCOL_DESCRIPTOR_LIST);
	sdp_attr_t	 proto = { SDP_ATTR_INVALID,0,sizeof(buffer),buffer };
	uint32_t	 type, len;

	if (local == NULL)
		local = NG_HCI_BDADDR_ANY;
	if (remote == NULL || channel == NULL)
		rfcomm_channel_lookup_exit(EINVAL);

	if ((ss = sdp_open(local, remote)) == NULL)
		rfcomm_channel_lookup_exit(ENOMEM);
	if (sdp_error(ss) != 0)
		rfcomm_channel_lookup_exit(sdp_error(ss));

	if (sdp_search(ss, 1, &serv, 1, &attr, 1, &proto) != 0)
		rfcomm_channel_lookup_exit(sdp_error(ss));
	if (proto.flags != SDP_ATTR_OK)
		rfcomm_channel_lookup_exit(ENOATTR);

	sdp_close(ss);
	ss = NULL;

	/*
	 * If it is possible for more than one kind of protocol stack to be 
	 * used to gain access to the service, the ProtocolDescriptorList
	 * takes the form of a data element alternative. We always use the
	 * first protocol stack.
	 *
	 * A minimal Protocol Descriptor List for RFCOMM based service would
	 * look like
	 *
	 * seq8 len8			- 2 bytes
	 *	seq8 len8		- 2 bytes
	 *		uuid16 value16	- 3 bytes	L2CAP
	 *	seq8 len8		- 2 bytes
	 *		uuid16 value16	- 3 bytes	RFCOMM
	 *		uint8  value8	- 2 bytes	RFCOMM param #1 
	 *				=========
	 *				 14 bytes
	 *
	 * Lets not count first [seq8 len8] wrapper, so the minimal size of 
	 * the Protocol Descriptor List (the data we are actually interested
	 * in) for RFCOMM based service would be 12 bytes.
	 */

	if (proto.vlen < PROTOCOL_DESCRIPTOR_LIST_MINIMAL_SIZE)
		rfcomm_channel_lookup_exit(EINVAL);

	SDP_GET8(type, proto.value);

	if (type == SDP_DATA_ALT8) {
		SDP_GET8(len, proto.value);
	} else if (type == SDP_DATA_ALT16) {
		SDP_GET16(len, proto.value);
	} else if (type == SDP_DATA_ALT32) {
		SDP_GET32(len, proto.value);
	} else
		len = 0;

	if (len > 0)
		SDP_GET8(type, proto.value);

	switch (type) {
	case SDP_DATA_SEQ8:
		SDP_GET8(len, proto.value);
		break;

	case SDP_DATA_SEQ16:
		SDP_GET16(len, proto.value);
		break;

	case SDP_DATA_SEQ32:
		SDP_GET32(len, proto.value);
		break;

	default:
		rfcomm_channel_lookup_exit(ENOATTR);
		/* NOT REACHED */
	}

	if (len < PROTOCOL_DESCRIPTOR_LIST_MINIMAL_SIZE)
		rfcomm_channel_lookup_exit(EINVAL);

	return (rfcomm_proto_list_parse(proto.value,
					buffer + proto.vlen, channel, error));
}
	
/*
 * Parse protocol descriptor list
 *
 * The ProtocolDescriptorList attribute describes one or more protocol
 * stacks that may be used to gain access to the service described by 
 * the service record. If the ProtocolDescriptorList describes a single
 * stack, it takes the form of a data element sequence in which each 
 * element of the sequence is a protocol descriptor.
 */

#undef	rfcomm_proto_list_parse_exit
#define	rfcomm_proto_list_parse_exit(e) { \
	if (error != NULL) \
		*error = (e); \
	return (((e) == 0)? 0 : -1); \
}

static int
rfcomm_proto_list_parse(uint8_t const *start, uint8_t const *end,
			int *channel, int *error)
{
	int	type, len, value;

	while (start < end) {

		/* 
		 * Parse protocol descriptor
		 *
		 * A protocol descriptor identifies a communications protocol 
		 * and provides protocol specific parameters. A protocol 
		 * descriptor is represented as a data element sequence. The 
		 * first data element in the sequence must be the UUID that 
		 * identifies the protocol. Additional data elements optionally
		 * provide protocol specific information, such as the L2CAP 
		 * protocol/service multiplexer (PSM) and the RFCOMM server
		 * channel number (CN).
		 */

		/* We must have at least one byte (type) */
		if (end - start < 1)
			rfcomm_proto_list_parse_exit(EINVAL)

		SDP_GET8(type, start);
		switch (type) {
		case SDP_DATA_SEQ8:
			SDP_GET8(len, start);
			break;

		case SDP_DATA_SEQ16:
			SDP_GET16(len, start);
			break;

		case SDP_DATA_SEQ32:
			SDP_GET32(len, start);
			break;

		default:
			rfcomm_proto_list_parse_exit(ENOATTR)
			/* NOT REACHED */
		}

		/* We must have at least 3 bytes (type + UUID16) */
		if (end - start < 3)
			rfcomm_proto_list_parse_exit(EINVAL);

		/* Get protocol UUID */
		SDP_GET8(type, start); len -= sizeof(uint8_t);
		switch (type) {
		case SDP_DATA_UUID16:
			SDP_GET16(value, start); len -= sizeof(uint16_t);
			if (value != SDP_UUID_PROTOCOL_RFCOMM)
				goto next_protocol;
			break;

		case SDP_DATA_UUID32:  /* XXX FIXME can we have 32-bit UUID */
		case SDP_DATA_UUID128: /* XXX FIXME can we have 128-bit UUID */
		default:
			rfcomm_proto_list_parse_exit(ENOATTR);
			/* NOT REACHED */
		}

		/*
		 * First protocol specific parameter for RFCOMM procotol must
		 * be uint8 that represents RFCOMM channel number. So we must
		 * have at least two bytes.
		 */

		if (end - start < 2)
			rfcomm_proto_list_parse_exit(EINVAL);

		SDP_GET8(type, start);
		if (type != SDP_DATA_UINT8)
			rfcomm_proto_list_parse_exit(ENOATTR);

		SDP_GET8(*channel, start);

		rfcomm_proto_list_parse_exit(0);
		/* NOT REACHED */
next_protocol:
		start += len;
	}

	/*
	 * If we got here then it means we could not find RFCOMM protocol 
	 * descriptor, but the reply format was actually valid.
	 */

	rfcomm_proto_list_parse_exit(ENOATTR);
}

