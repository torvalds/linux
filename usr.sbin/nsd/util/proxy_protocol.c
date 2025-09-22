/*
 * util/proxy_protocol.c - event notification
 *
 * Copyright (c) 2022, NLnet Labs. All rights reserved.
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
 * This file contains PROXY protocol functions.
 */
#include "util/proxy_protocol.h"

/**
 * Internal struct initialized with function pointers for writing uint16 and
 * uint32.
 */
struct proxy_protocol_data {
	void (*write_uint16)(void* buf, uint16_t data);
	void (*write_uint32)(void* buf, uint32_t data);
};
struct proxy_protocol_data pp_data;

/**
 * Internal lookup table; could be further generic like sldns_lookup_table
 * for all the future generic stuff.
 */
struct proxy_protocol_lookup_table {
	int id;
	const char *text;
};

/**
 * Internal parsing error text; could be exposed with pp_lookup_error.
 */
static struct proxy_protocol_lookup_table pp_parse_errors_data[] = {
	{ PP_PARSE_NOERROR, "no parse error" },
	{ PP_PARSE_SIZE, "not enough space for header" },
	{ PP_PARSE_WRONG_HEADERv2, "could not match PROXYv2 header" },
	{ PP_PARSE_UNKNOWN_CMD, "unknown command" },
	{ PP_PARSE_UNKNOWN_FAM_PROT, "unknown family and protocol" },
};

void
pp_init(void (*write_uint16)(void* buf, uint16_t data),
	void (*write_uint32)(void* buf, uint32_t data)) {
	pp_data.write_uint16 = write_uint16;
	pp_data.write_uint32 = write_uint32;
}

const char*
pp_lookup_error(enum pp_parse_errors error) {
	return pp_parse_errors_data[error].text;
}

size_t
pp2_write_to_buf(uint8_t* buf, size_t buflen,
#ifdef INET6
	struct sockaddr_storage* src,
#else
	struct sockaddr_in* src,
#endif
	int stream)
{
	int af;
	size_t expected_size;
	if(!src) return 0;
	af = (int)((struct sockaddr_in*)src)->sin_family;
	expected_size = PP2_HEADER_SIZE + (af==AF_INET?12:36);
	if(buflen < expected_size) {
		return 0;
	}
	/* sig */
	memcpy(buf, PP2_SIG, PP2_SIG_LEN);
	buf += PP2_SIG_LEN;
	/* version and command */
	*buf = (PP2_VERSION << 4) | PP2_CMD_PROXY;
	buf++;
	switch(af) {
	case AF_INET:
		/* family and protocol */
		*buf = (PP2_AF_INET<<4) |
			(stream?PP2_PROT_STREAM:PP2_PROT_DGRAM);
		buf++;
		/* length */
		(*pp_data.write_uint16)(buf, 12);
		buf += 2;
		/* src addr */
		memcpy(buf,
			&((struct sockaddr_in*)src)->sin_addr.s_addr, 4);
		buf += 4;
		/* dst addr */
		(*pp_data.write_uint32)(buf, 0);
		buf += 4;
		/* src port */
		memcpy(buf,
			&((struct sockaddr_in*)src)->sin_port, 2);
		buf += 2;
		/* dst addr */
		/* dst port */
		(*pp_data.write_uint16)(buf, 12);
		break;
#ifdef INET6
	case AF_INET6:
		/* family and protocol */
		*buf = (PP2_AF_INET6<<4) |
			(stream?PP2_PROT_STREAM:PP2_PROT_DGRAM);
		buf++;
		/* length */
		(*pp_data.write_uint16)(buf, 36);
		buf += 2;
		/* src addr */
		memcpy(buf,
			&((struct sockaddr_in6*)src)->sin6_addr, 16);
		buf += 16;
		/* dst addr */
		memset(buf, 0, 16);
		buf += 16;
		/* src port */
		memcpy(buf, &((struct sockaddr_in6*)src)->sin6_port, 2);
		buf += 2;
		/* dst port */
		(*pp_data.write_uint16)(buf, 0);
		break;
#endif /* INET6 */
	case AF_UNIX:
		/* fallthrough */
	default:
		return 0;
	}
	return expected_size;
}

int
pp2_read_header(uint8_t* buf, size_t buflen)
{
	size_t size;
	struct pp2_header* header = (struct pp2_header*)buf;
	/* Try to fail all the unsupported cases first. */
	if(buflen < PP2_HEADER_SIZE) {
		return PP_PARSE_SIZE;
	}
	/* Check for PROXYv2 header */
	if(memcmp(header, PP2_SIG, PP2_SIG_LEN) != 0 ||
		((header->ver_cmd & 0xF0)>>4) != PP2_VERSION) {
		return PP_PARSE_WRONG_HEADERv2;
	}
	/* Check the length */
	size = PP2_HEADER_SIZE + ntohs(header->len);
	if(buflen < size) {
		return PP_PARSE_SIZE;
	}
	/* Check for supported commands */
	if((header->ver_cmd & 0xF) != PP2_CMD_LOCAL &&
		(header->ver_cmd & 0xF) != PP2_CMD_PROXY) {
		return PP_PARSE_UNKNOWN_CMD;
	}
	/* Check for supported family and protocol */
	if(header->fam_prot != PP2_UNSPEC_UNSPEC &&
		header->fam_prot != PP2_INET_STREAM &&
		header->fam_prot != PP2_INET_DGRAM &&
		header->fam_prot != PP2_INET6_STREAM &&
		header->fam_prot != PP2_INET6_DGRAM &&
		header->fam_prot != PP2_UNIX_STREAM &&
		header->fam_prot != PP2_UNIX_DGRAM) {
		return PP_PARSE_UNKNOWN_FAM_PROT;
	}
	/* We have a correct header */
	return PP_PARSE_NOERROR;
}
