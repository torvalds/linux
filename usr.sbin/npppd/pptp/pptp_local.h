/*	$OpenBSD: pptp_local.h,v 1.5 2021/03/29 03:54:40 yasuoka Exp $	*/

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef	PPTP_LOCAL_H
#define	PPTP_LOCAL_H 1

#ifndef	countof
#define	countof(x)	(sizeof(x) / sizeof((x)[0]))
#endif

struct pptp_gre_header {
#if     BYTE_ORDER == LITTLE_ENDIAN
	uint8_t		recur:3,
			s:1,
			S:1,
			K:1,
			R:1,
			C:1;
	uint8_t		ver:3,
			flags:4,
			A:1;
#else
	uint8_t		C:1,
			R:1,
			K:1,
			S:1,
			s:1,
			recur:3;
	uint8_t		A:1,
			flags:4,
			ver:3;
#endif
	uint16_t	protocol_type;
	uint16_t	payload_length;
	uint16_t	call_id;
} __attribute__((__packed__));


/* Common part of the PPTP control packet */
struct pptp_ctrl_header {
	uint16_t	length;
	uint16_t	pptp_message_type;
	uint32_t	magic_cookie;
	uint16_t	control_message_type;
	uint16_t	reserved0;
} __attribute__((__packed__));

/* Start-Control-Connection-{Request,Reply} packet */
struct pptp_scc {
	struct pptp_ctrl_header header;
	uint16_t	protocol_version;
	uint8_t		result_code;
	uint8_t		error_code;
	uint32_t	framing_caps;
	uint32_t	bearer_caps;
	uint16_t	max_channels;
	uint16_t	firmware_revision;
	u_char		host_name[64];
	u_char		vendor_string[64];
} __attribute__((__packed__));

/* Outgoing-Call-Request packet */
struct pptp_ocrq {
	struct pptp_ctrl_header header;
	uint16_t	call_id;
	uint16_t	call_serial_number;
	uint32_t	maximum_bps;
	uint32_t	minimum_bps;
	uint32_t	bearer_type;
	uint32_t	framing_type;
	uint16_t	recv_winsz;
	uint16_t	packet_proccessing_delay;
	uint16_t	phone_number_length;
	uint16_t	reservied1;
	u_char		phone_number[64];
	u_char		subaddress[64];
} __attribute__((__packed__));

/* Outgoing-Call-Reply packet */
struct pptp_ocrp {
	struct pptp_ctrl_header header;
	uint16_t	call_id;
	uint16_t	peers_call_id;
	uint8_t		result_code;
	uint8_t		error_code;
	uint16_t	cause_code;
	uint32_t	connect_speed;
	uint16_t	recv_winsz;
	uint16_t	packet_proccessing_delay;
	uint32_t	physical_channel_id;
} __attribute__((__packed__));

/* Echo-Request packet */
struct pptp_echo_rq {
	struct pptp_ctrl_header header;
	uint32_t	identifier;
} __attribute__((__packed__));

/* Echo-Reply packet */
struct pptp_echo_rp {
	struct pptp_ctrl_header header;
	uint32_t	identifier;
	uint8_t		result_code;
	uint8_t		error_code;
	uint16_t	reserved1;
} __attribute__((__packed__));

/* Set-Link-Info packet */
struct pptp_sli {
	struct pptp_ctrl_header header;
	uint16_t	peers_call_id;
	uint16_t	reserved;
	uint32_t	send_accm;
	uint32_t	recv_accm;
} __attribute__((__packed__));

struct pptp_stop_ccrq {
	struct pptp_ctrl_header header;
	uint8_t		reason;
	uint8_t		reserved1;
	uint16_t	reserved2;
} __attribute__((__packed__));

struct pptp_stop_ccrp {
	struct pptp_ctrl_header header;
	uint8_t		result;
	uint8_t		error;
	uint16_t	reserved2;
} __attribute__((__packed__));


/* Call-Clear-Request packet */
struct pptp_ccr {
	struct pptp_ctrl_header header;
	uint16_t	call_id;
	uint16_t	reserved1;
} __attribute__((__packed__));

/* Call-Disconnect-Notify packet */
struct pptp_cdn {
	struct pptp_ctrl_header header;
	uint16_t	call_id;
	uint8_t		result_code;
	uint8_t		error_code;
	uint16_t	cause_code;
	uint16_t	reserved1;
	char		statistics[128];
} __attribute__((__packed__));


#define	PPTP_GRE_PKT_SEQ_PRESENT	0x0001
#define	PPTP_GRE_PKT_ACK_PRESENT	0x0002

#endif

