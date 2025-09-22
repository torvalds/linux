/*
 * dnstap/dnstap_fstrm.c - Frame Streams protocol for dnstap
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
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
 *
 */

/**
 * \file
 *
 * Definitions for the Frame Streams data transport protocol for
 * dnstap message logs.
 */

#include "config.h"
#include "dnstap/dnstap_fstrm.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"

void* fstrm_create_control_frame_start(char* contenttype, size_t* len)
{
	uint32_t* control;
	size_t n;
	/* start framestream message:
	 * 4byte 0: control indicator.
	 * 4byte bigendian: length of control frame
	 * 4byte bigendian: type START
	 * 4byte bigendian: option: content-type
	 * 4byte bigendian: length of string
	 * string of content type (dnstap)
	 */
	n = 4+4+4+4+4+strlen(contenttype);
	control = malloc(n);
	if(!control)
		return NULL;
	control[0] = 0;
	control[1] = htonl(4+4+4+strlen(contenttype));
	control[2] = htonl(FSTRM_CONTROL_FRAME_START);
	control[3] = htonl(FSTRM_CONTROL_FIELD_TYPE_CONTENT_TYPE);
	control[4] = htonl(strlen(contenttype));
	memmove(&control[5], contenttype, strlen(contenttype));
	*len = n;
	return control;
}

void* fstrm_create_control_frame_stop(size_t* len)
{
	uint32_t* control;
	size_t n;
	/* stop framestream message:
	 * 4byte 0: control indicator.
	 * 4byte bigendian: length of control frame
	 * 4byte bigendian: type STOP
	 */
	n = 4+4+4;
	control = malloc(n);
	if(!control)
		return NULL;
	control[0] = 0;
	control[1] = htonl(4);
	control[2] = htonl(FSTRM_CONTROL_FRAME_STOP);
	*len = n;
	return control;
}

void* fstrm_create_control_frame_ready(char* contenttype, size_t* len)
{
	uint32_t* control;
	size_t n;
	/* start bidirectional stream:
	 * 4 bytes 0 escape
	 * 4 bytes bigendian length of frame
	 * 4 bytes bigendian type READY
	 * 4 bytes bigendian frame option content type
	 * 4 bytes bigendian length of string
	 * string of content type.
	 */
	/* len includes the escape and framelength */
	n = 4+4+4+4+4+strlen(contenttype);
	control = malloc(n);
	if(!control) {
		return NULL;
	}
	control[0] = 0;
	control[1] = htonl(4+4+4+strlen(contenttype));
	control[2] = htonl(FSTRM_CONTROL_FRAME_READY);
	control[3] = htonl(FSTRM_CONTROL_FIELD_TYPE_CONTENT_TYPE);
	control[4] = htonl(strlen(contenttype));
	memmove(&control[5], contenttype, strlen(contenttype));
	*len = n;
	return control;
}

void* fstrm_create_control_frame_accept(char* contenttype, size_t* len)
{
	uint32_t* control;
	size_t n;
	/* control frame on reply:
	 * 4 bytes 0 escape
	 * 4 bytes bigendian length of frame
	 * 4 bytes bigendian type ACCEPT
	 * 4 bytes bigendian frame option content type
	 * 4 bytes bigendian length of string
	 * string of content type.
	 */
	/* len includes the escape and framelength */
	n = 4+4+4+4+4+strlen(contenttype);
	control = malloc(n);
	if(!control) {
		return NULL;
	}
	control[0] = 0;
	control[1] = htonl(4+4+4+strlen(contenttype));
	control[2] = htonl(FSTRM_CONTROL_FRAME_ACCEPT);
	control[3] = htonl(FSTRM_CONTROL_FIELD_TYPE_CONTENT_TYPE);
	control[4] = htonl(strlen(contenttype));
	memmove(&control[5], contenttype, strlen(contenttype));
	*len = n;
	return control;
}

void* fstrm_create_control_frame_finish(size_t* len)
{
	uint32_t* control;
	size_t n;
	/* control frame on reply:
	 * 4 bytes 0 escape
	 * 4 bytes bigendian length of frame
	 * 4 bytes bigendian type FINISH
	 */
	/* len includes the escape and framelength */
	n = 4+4+4;
	control = malloc(n);
	if(!control) {
		return NULL;
	}
	control[0] = 0;
	control[1] = htonl(4);
	control[2] = htonl(FSTRM_CONTROL_FRAME_FINISH);
	*len = n;
	return control;
}

char* fstrm_describe_control(void* pkt, size_t len)
{
	uint32_t frametype = 0;
	char buf[512];
	char* str = buf;
	size_t remain, slen = sizeof(buf);
	uint8_t* pos;

	buf[0]=0;
	if(len < 4) {
		snprintf(buf, sizeof(buf), "malformed control frame, "
			"too short, len=%u", (unsigned int)len);
		return strdup(buf);
	}
	frametype = sldns_read_uint32(pkt);
	if(frametype == FSTRM_CONTROL_FRAME_ACCEPT) {
		(void)sldns_str_print(&str, &slen, "accept");
	} else if(frametype == FSTRM_CONTROL_FRAME_START) {
		(void)sldns_str_print(&str, &slen, "start");
	} else if(frametype == FSTRM_CONTROL_FRAME_STOP) {
		(void)sldns_str_print(&str, &slen, "stop");
	} else if(frametype == FSTRM_CONTROL_FRAME_READY) {
		(void)sldns_str_print(&str, &slen, "ready");
	} else if(frametype == FSTRM_CONTROL_FRAME_FINISH) {
		(void)sldns_str_print(&str, &slen, "finish");
	} else {
		(void)sldns_str_print(&str, &slen, "type%d", (int)frametype);
	}

	/* show the content type options */
	pos = pkt + 4;
	remain = len - 4;
	while(remain >= 8) {
		uint32_t field_type = sldns_read_uint32(pos);
		uint32_t field_len = sldns_read_uint32(pos+4);
		if(remain < field_len) {
			(void)sldns_str_print(&str, &slen, "malformed_field");
			break;
		}
		if(field_type == FSTRM_CONTROL_FIELD_TYPE_CONTENT_TYPE) {
			char tempf[512];
			(void)sldns_str_print(&str, &slen, " content-type(");
			if(field_len < sizeof(tempf)-1) {
				memmove(tempf, pos+8, field_len);
				tempf[field_len] = 0;
				(void)sldns_str_print(&str, &slen, "%s", tempf);
			} else {
				(void)sldns_str_print(&str, &slen, "<error-too-long>");
			}
			(void)sldns_str_print(&str, &slen, ")");
		} else {
			(void)sldns_str_print(&str, &slen,
				" field(type %u, length %u)",
				(unsigned int)field_type,
				(unsigned int)field_len);
		}
		pos += 8 + field_len;
		remain -= (8 + field_len);
	}
	if(remain > 0)
		(void)sldns_str_print(&str, &slen, " trailing-bytes"
			"(length %u)", (unsigned int)remain);
	return strdup(buf);
}
