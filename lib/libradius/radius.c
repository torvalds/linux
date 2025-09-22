/*	$OpenBSD: radius.c,v 1.6 2024/08/14 04:50:31 yasuoka Exp $ */

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
 * THIS SOFTWARE IS PROVIDED BY THE"AUTHOR" AND CONTRIBUTORS AS IS'' AND
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

#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/inet.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/md5.h>

#include "radius.h"

#include "radius_local.h"

static uint8_t radius_id_counter = 0;

static int
radius_check_packet_data(const RADIUS_PACKET_DATA * pdata, size_t length)
{
	const RADIUS_ATTRIBUTE	*attr;
	const RADIUS_ATTRIBUTE	*end;

	if (length < sizeof(RADIUS_PACKET_DATA))
		return (-1);
	if (length > 0xffff)
		return (-1);
	if (length != (size_t) ntohs(pdata->length))
		return (-1);

	attr = ATTRS_BEGIN(pdata);
	end = ATTRS_END(pdata);
	for (; attr < end; ATTRS_ADVANCE(attr)) {
		if (attr->length < 2)
			return (-1);
		if (attr->type == RADIUS_TYPE_VENDOR_SPECIFIC) {
			if (attr->length < 8)
				return (-1);
			if ((attr->vendor & htonl(0xff000000U)) != 0)
				return (-1);
			if (attr->length != attr->vlength + 6)
				return (-1);
		}
	}

	if (attr != end)
		return (-1);

	return (0);
}

int
radius_ensure_add_capacity(RADIUS_PACKET * packet, size_t capacity)
{
	size_t	 newsize;
	void	*newptr;

	/*
	 * The maximum size is 64KB.
	 * We use little bit smaller value for our safety(?).
	 */
	if (ntohs(packet->pdata->length) + capacity > 0xfe00)
		return (-1);

	if (ntohs(packet->pdata->length) + capacity > packet->capacity) {
		newsize = ntohs(packet->pdata->length) + capacity +
		    RADIUS_PACKET_CAPACITY_INCREMENT;
		newptr = realloc(packet->pdata, newsize);
		if (newptr == NULL)
			return (-1);
		packet->capacity = newsize;
		packet->pdata = (RADIUS_PACKET_DATA *)newptr;
	}
	return (0);
}

RADIUS_PACKET *
radius_new_request_packet(uint8_t code)
{
	RADIUS_PACKET	*packet;

	packet = malloc(sizeof(RADIUS_PACKET));
	if (packet == NULL)
		return (NULL);
	packet->pdata = malloc(RADIUS_PACKET_CAPACITY_INITIAL);
	if (packet->pdata == NULL) {
		free(packet);
		return (NULL);
	}
	packet->capacity = RADIUS_PACKET_CAPACITY_INITIAL;
	packet->request = NULL;
	packet->pdata->code = code;
	packet->pdata->id = radius_id_counter++;
	packet->pdata->length = htons(sizeof(RADIUS_PACKET_DATA));
	arc4random_buf(packet->pdata->authenticator,
	    sizeof(packet->pdata->authenticator));

	return (packet);
}

RADIUS_PACKET *
radius_new_response_packet(uint8_t code, const RADIUS_PACKET * request)
{
	RADIUS_PACKET	*packet;

	packet = radius_new_request_packet(code);
	if (packet == NULL)
		return (NULL);
	packet->request = request;
	packet->pdata->id = request->pdata->id;

	return (packet);
}

RADIUS_PACKET *
radius_convert_packet(const void *pdata, size_t length)
{
	RADIUS_PACKET *packet;

	if (radius_check_packet_data((const RADIUS_PACKET_DATA *)pdata,
	    length) != 0)
		return (NULL);
	packet = malloc(sizeof(RADIUS_PACKET));
	if (packet == NULL)
		return (NULL);
	packet->pdata = malloc(length);
	packet->capacity = length;
	packet->request = NULL;
	if (packet->pdata == NULL) {
		free(packet);
		return (NULL);
	}
	memcpy(packet->pdata, pdata, length);

	return (packet);
}

int
radius_delete_packet(RADIUS_PACKET * packet)
{
	free(packet->pdata);
	free(packet);
	return (0);
}

uint8_t
radius_get_code(const RADIUS_PACKET * packet)
{
	return (packet->pdata->code);
}

uint8_t
radius_get_id(const RADIUS_PACKET * packet)
{
	return (packet->pdata->id);
}

void
radius_update_id(RADIUS_PACKET * packet)
{
	packet->pdata->id = radius_id_counter++;
}

void
radius_set_id(RADIUS_PACKET * packet, uint8_t id)
{
	packet->pdata->id = id;
}

void
radius_get_authenticator(const RADIUS_PACKET * packet, void *authenticator)
{
	memcpy(authenticator, packet->pdata->authenticator, 16);
}

uint8_t *
radius_get_authenticator_retval(const RADIUS_PACKET * packet)
{
	return (packet->pdata->authenticator);
}

uint8_t *
radius_get_request_authenticator_retval(const RADIUS_PACKET * packet)
{
	if (packet->request == NULL)
		return (packet->pdata->authenticator);
	else
		return (packet->request->pdata->authenticator);
}

void
radius_set_request_packet(RADIUS_PACKET * packet,
    const RADIUS_PACKET * request)
{
	packet->request = request;
}

const RADIUS_PACKET *
radius_get_request_packet(const RADIUS_PACKET * packet)
{
	return (packet->request);
}

static void
radius_calc_authenticator(uint8_t * authenticator_dst,
    const RADIUS_PACKET * packet, const uint8_t * authenticator_src,
    const char *secret)
{
	MD5_CTX	 ctx;

	MD5_Init(&ctx);
	MD5_Update(&ctx, (unsigned char *)packet->pdata, 4);
	MD5_Update(&ctx, (unsigned char *)authenticator_src, 16);
	MD5_Update(&ctx,
	    (unsigned char *)packet->pdata->attributes,
	    radius_get_length(packet) - 20);
	MD5_Update(&ctx, (unsigned char *)secret, strlen(secret));
	MD5_Final((unsigned char *)authenticator_dst, &ctx);
}

static void
radius_calc_response_authenticator(uint8_t * authenticator_dst,
    const RADIUS_PACKET * packet, const char *secret)
{
	radius_calc_authenticator(authenticator_dst,
	    packet, packet->request->pdata->authenticator, secret);
}

int
radius_check_response_authenticator(const RADIUS_PACKET * packet,
    const char *secret)
{
	uint8_t authenticator[16];

	radius_calc_response_authenticator(authenticator, packet, secret);
	return (timingsafe_bcmp(authenticator, packet->pdata->authenticator,
	    16));
}

void
radius_set_response_authenticator(RADIUS_PACKET * packet,
    const char *secret)
{
	radius_calc_response_authenticator(packet->pdata->authenticator,
	    packet, secret);
}

static void
radius_calc_accounting_request_authenticator(uint8_t * authenticator_dst,
    const RADIUS_PACKET * packet, const char *secret)
{
	uint8_t	 zero[16];

	memset(zero, 0, sizeof(zero));
	radius_calc_authenticator(authenticator_dst,
	    packet, zero, secret);
}

void
radius_set_accounting_request_authenticator(RADIUS_PACKET * packet,
    const char *secret)
{
	radius_calc_accounting_request_authenticator(
	    packet->pdata->authenticator, packet, secret);
}

int
radius_check_accounting_request_authenticator(const RADIUS_PACKET * packet,
    const char *secret)
{
	uint8_t authenticator[16];

	radius_calc_accounting_request_authenticator(authenticator, packet,
	    secret);
	return (timingsafe_bcmp(authenticator, packet->pdata->authenticator,
	    16));
}


uint16_t
radius_get_length(const RADIUS_PACKET * packet)
{
	return (ntohs(packet->pdata->length));
}


const void *
radius_get_data(const RADIUS_PACKET * packet)
{
	return (packet->pdata);
}

RADIUS_PACKET *
radius_recvfrom(int s, int flags, struct sockaddr * sa, socklen_t * slen)
{
	char	 buf[0x10000];
	ssize_t	 n;

	n = recvfrom(s, buf, sizeof(buf), flags, sa, slen);
	if (n <= 0)
		return (NULL);

	return (radius_convert_packet(buf, (size_t) n));
}

int
radius_sendto(int s, const RADIUS_PACKET * packet,
    int flags, const struct sockaddr * sa, socklen_t slen)
{
	ssize_t	 n;

	n = sendto(s, packet->pdata, radius_get_length(packet), flags, sa,
	    slen);
	if (n != radius_get_length(packet))
		return (-1);

	return (0);
}

RADIUS_PACKET *
radius_recv(int s, int flags)
{
	char	 buf[0x10000];
	ssize_t	 n;

	n = recv(s, buf, sizeof(buf), flags);
	if (n <= 0)
		return (NULL);

	return (radius_convert_packet(buf, (size_t) n));
}

int
radius_send(int s, const RADIUS_PACKET * packet, int flags)
{
	ssize_t	 n;

	n = send(s, packet->pdata, radius_get_length(packet), flags);
	if (n != radius_get_length(packet))
		return (-1);

	return (0);
}

RADIUS_PACKET *
radius_recvmsg(int s, struct msghdr * msg, int flags)
{
	struct iovec	 iov;
	char		 buf[0x10000];
	ssize_t		 n;

	if (msg->msg_iov != NULL || msg->msg_iovlen != 0)
		return (NULL);

	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg->msg_iov = &iov;
	msg->msg_iovlen = 1;
	n = recvmsg(s, msg, flags);
	msg->msg_iov = NULL;
	msg->msg_iovlen = 0;
	if (n <= 0)
		return (NULL);

	return (radius_convert_packet(buf, (size_t) n));
}

int
radius_sendmsg(int s, const RADIUS_PACKET * packet,
    const struct msghdr * msg, int flags)
{
	struct msghdr	 msg0;
	struct iovec	 iov;
	ssize_t		 n;

	if (msg->msg_iov != NULL || msg->msg_iovlen != 0)
		return (-1);

	iov.iov_base = packet->pdata;
	iov.iov_len = radius_get_length(packet);
	msg0 = *msg;
	msg0.msg_iov = &iov;
	msg0.msg_iovlen = 1;
	n = sendmsg(s, &msg0, flags);
	if (n != radius_get_length(packet))
		return (-1);

	return (0);
}
