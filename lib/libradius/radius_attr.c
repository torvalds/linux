/*	$OpenBSD: radius_attr.c,v 1.3 2024/07/24 08:19:16 yasuoka Exp $ */

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

#include <netinet/in.h>
#include <arpa/inet.h>

#include <endian.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "radius.h"

#include "radius_local.h"

#define FIND_ATTRIBUTE_BEGIN(constness, redolabel)		\
	constness RADIUS_ATTRIBUTE* attr;			\
	constness RADIUS_ATTRIBUTE* end;			\
								\
	attr = ATTRS_BEGIN(packet->pdata);			\
	end  = ATTRS_END(packet->pdata);			\
								\
	for (;; ATTRS_ADVANCE(attr))				\
	{							\
	redolabel						\
		if (attr >= end)				\
			break;					\
		if (attr->type != type)				\
			continue;				\
		{

#define FIND_ATTRIBUTE_END \
	} }

#define FIND_VS_ATTRIBUTE_BEGIN(constness, redolabel)		\
	constness RADIUS_ATTRIBUTE* attr;			\
	constness RADIUS_ATTRIBUTE* end;			\
								\
	attr = ATTRS_BEGIN(packet->pdata);			\
	end  = ATTRS_END(packet->pdata);			\
								\
	for (;; ATTRS_ADVANCE(attr))				\
	{							\
	redolabel						\
		if (attr >= end)				\
			break;					\
		if (attr->type != RADIUS_TYPE_VENDOR_SPECIFIC)	\
			continue;				\
		if (attr->vendor != htonl(vendor))		\
			continue;				\
		if (attr->vtype != vtype)			\
			continue;				\
		{

#define FIND_VS_ATTRIBUTE_END					\
	} }


int
radius_get_raw_attr_ptr(const RADIUS_PACKET * packet, uint8_t type,
    const void **ptr, size_t * length)
{
	FIND_ATTRIBUTE_BEGIN(const,) {
		*length = attr->length - 2;
		*ptr = attr->data;
		return (0);
	} FIND_ATTRIBUTE_END;

	return (-1);
}

int
radius_get_vs_raw_attr_ptr(const RADIUS_PACKET * packet, uint32_t vendor,
    uint8_t vtype, const void **ptr, size_t * length)
{
	FIND_VS_ATTRIBUTE_BEGIN(const,) {
		*length = attr->vlength - 2;
		*ptr = attr->vdata;
		return (0);
	} FIND_VS_ATTRIBUTE_END;

	return (-1);
}

int
radius_get_raw_attr(const RADIUS_PACKET * packet, uint8_t type, void *buf,
    size_t * length)
{
	FIND_ATTRIBUTE_BEGIN(const,) {
		*length = MINIMUM(attr->length - 2, *length);
		memcpy(buf, attr->data, *length);
		return (0);
	} FIND_ATTRIBUTE_END;

	return (-1);
}

int
radius_get_vs_raw_attr(const RADIUS_PACKET * packet, uint32_t vendor,
    uint8_t vtype, void *buf, size_t * length)
{
	FIND_VS_ATTRIBUTE_BEGIN(const,) {
		*length = MINIMUM(attr->vlength - 2, *length);
		memcpy(buf, attr->vdata, *length);
		return (0);
	} FIND_VS_ATTRIBUTE_END;

	return (-1);
}

int
radius_get_raw_attr_cat(const RADIUS_PACKET * packet, uint8_t type, void *buf,
    size_t * length)
{
	size_t	 off = 0;

	FIND_ATTRIBUTE_BEGIN(const,) {
		if (buf != NULL) {
			if (off + attr->length - 2 <= *length)
				memcpy((char *)buf + off, attr->data,
				    attr->length - 2);
			else
				return (-1);
		}
		off += attr->length - 2;
	} FIND_ATTRIBUTE_END;

	*length = off;

	return (0);
}

int
radius_get_vs_raw_attr_cat(const RADIUS_PACKET * packet, uint32_t vendor,
    uint8_t vtype, void *buf, size_t * length)
{
	size_t	 off = 0;

	FIND_VS_ATTRIBUTE_BEGIN(const,) {
		if (buf != NULL) {
			if (off + attr->vlength - 2 <= *length)
				memcpy((char *)buf + off, attr->vdata,
				    attr->vlength - 2);
			else
				return (-1);
		}
		off += attr->vlength - 2;
	} FIND_VS_ATTRIBUTE_END;

	*length = off;

	return (0);
}

int
radius_put_raw_attr(RADIUS_PACKET * packet, uint8_t type, const void *buf,
    size_t length)
{
	RADIUS_ATTRIBUTE	*newattr;

	if (length > 255 - 2)
		return (-1);

	if (radius_ensure_add_capacity(packet, length + 2) != 0)
		return (-1);

	newattr = ATTRS_END(packet->pdata);
	newattr->type = type;
	newattr->length = length + 2;
	memcpy(newattr->data, buf, length);
	packet->pdata->length = htons(radius_get_length(packet) + length + 2);

	return (0);
}

int
radius_unshift_raw_attr(RADIUS_PACKET * packet, uint8_t type, const void *buf,
    size_t length)
{
	RADIUS_ATTRIBUTE	*newattr;

	if (length > 255 - 2)
		return (-1);

	if (radius_ensure_add_capacity(packet, length + 2) != 0)
		return (-1);

	memmove(packet->pdata->attributes + length + 2,
	    packet->pdata->attributes,
	    radius_get_length(packet) - sizeof(RADIUS_PACKET_DATA));

	newattr = ATTRS_BEGIN(packet->pdata);
	newattr->type = type;
	newattr->length = length + 2;
	memcpy(newattr->data, buf, length);
	packet->pdata->length = htons(radius_get_length(packet) + length + 2);

	return (0);
}

int
radius_put_vs_raw_attr(RADIUS_PACKET * packet, uint32_t vendor, uint8_t vtype,
    const void *buf, size_t length)
{
	RADIUS_ATTRIBUTE	*newattr;

	if (length > 255 - 8)
		return (-1);
	if ((vendor & 0xff000000U) != 0)
		return (-1);

	if (radius_ensure_add_capacity(packet, length + 8) != 0)
		return (-1);

	newattr = ATTRS_END(packet->pdata);
	newattr->type = RADIUS_TYPE_VENDOR_SPECIFIC;
	newattr->length = length + 8;
	newattr->vendor = htonl(vendor);
	newattr->vtype = vtype;
	newattr->vlength = length + 2;
	memcpy(newattr->vdata, buf, length);
	packet->pdata->length = htons(radius_get_length(packet) + length + 8);

	return (0);
}

int
radius_put_raw_attr_cat(RADIUS_PACKET * packet, uint8_t type, const void *buf,
    size_t length)
{
	int	 off, len0;

	off = 0;
	while (off < length) {
		len0 = MINIMUM(length - off, 255 - 2);

		if (radius_put_raw_attr(packet, type, (const char *)buf + off,
		    len0) != 0)
			return (-1);

		off += len0;
	}

	return (0);
}

int
radius_put_vs_raw_attr_cat(RADIUS_PACKET * packet, uint32_t vendor,
    uint8_t vtype, const void *buf, size_t length)
{
	int	 off, len0;

	off = 0;
	while (off < length) {
		len0 = MINIMUM(length - off, 255 - 8);

		if (radius_put_vs_raw_attr(packet, vendor, vtype,
		    (const char *)buf + off, len0) != 0)
			return (-1);

		off += len0;
	}

	return (0);
}

int
radius_set_raw_attr(RADIUS_PACKET * packet,
    uint8_t type, const void *buf, size_t length)
{
	FIND_ATTRIBUTE_BEGIN(,) {
		if (length != attr->length - 2)
			return (-1);
		memcpy(attr->data, buf, length);
		return (0);
	} FIND_ATTRIBUTE_END;

	return (-1);
}

int
radius_set_vs_raw_attr(RADIUS_PACKET * packet,
    uint32_t vendor, uint8_t vtype, const void *buf, size_t length)
{
	FIND_VS_ATTRIBUTE_BEGIN(,) {
		if (length != attr->vlength - 2)
			return (-1);
		memcpy(attr->vdata, buf, length);
		return (0);
	} FIND_VS_ATTRIBUTE_END;

	return (-1);
}

int
radius_del_attr_all(RADIUS_PACKET * packet, uint8_t type)
{
	FIND_ATTRIBUTE_BEGIN(, redo:) {
		RADIUS_ATTRIBUTE *next = ATTRS_NEXT(attr);
		packet->pdata->length =
		    htons(ntohs(packet->pdata->length) - attr->length);
		memmove(attr, next, ((char *)end) - ((char *)next));
		end = ATTRS_END(packet->pdata);
		goto redo;
	} FIND_ATTRIBUTE_END;

	return (0);
}

int
radius_del_vs_attr_all(RADIUS_PACKET * packet, uint32_t vendor, uint8_t vtype)
{
	FIND_VS_ATTRIBUTE_BEGIN(, redo:) {
		RADIUS_ATTRIBUTE *next = ATTRS_NEXT(attr);
		packet->pdata->length =
		    htons(ntohs(packet->pdata->length) - attr->length);
		memmove(attr, next, ((char *)end) - ((char *)next));
		end = ATTRS_END(packet->pdata);
		goto redo;
	} FIND_VS_ATTRIBUTE_END;

	return (0);
}

bool
radius_has_attr(const RADIUS_PACKET * packet, uint8_t type)
{
	FIND_ATTRIBUTE_BEGIN(const,) {
		return (true);
	} FIND_VS_ATTRIBUTE_END;

	return (false);
}

bool
radius_has_vs_attr(const RADIUS_PACKET * packet, uint32_t vendor, uint8_t vtype)
{
	FIND_VS_ATTRIBUTE_BEGIN(const,) {
		return (true);
	} FIND_VS_ATTRIBUTE_END;

	return (false);
}

int
radius_get_string_attr(const RADIUS_PACKET * packet, uint8_t type, char *str,
    size_t len)
{
	const void	*p;
	size_t		 origlen;

	if (radius_get_raw_attr_ptr(packet, type, &p, &origlen) != 0)
		return (-1);
	if (memchr(p, 0, origlen) != NULL)
		return (-1);
	if (len >= 1) {
		len = MINIMUM(origlen, len - 1);
		memcpy(str, (const char *)p, len);
		str[len] = '\0';
	}
	return (0);
}

int
radius_get_vs_string_attr(const RADIUS_PACKET * packet,
    uint32_t vendor, uint8_t vtype, char *str, size_t len)
{
	const void *p;
	size_t origlen;

	if (radius_get_vs_raw_attr_ptr(packet,
		vendor, vtype, &p, &origlen) != 0)
		return (-1);
	if (memchr(p, 0, origlen) != NULL)
		return (-1);
	if (len >= 1) {
		len = MINIMUM(origlen, len - 1);
		memcpy(str, (const char *)p, len);
		str[len] = '\0';
	}

	return (0);
}

int
radius_put_string_attr(RADIUS_PACKET * packet, uint8_t type, const char *str)
{
	return radius_put_raw_attr(packet, type, str, strlen(str));
}

int
radius_put_vs_string_attr(RADIUS_PACKET * packet,
    uint32_t vendor, uint8_t vtype, const char *str)
{
	return radius_put_vs_raw_attr(packet, vendor, vtype, str, strlen(str));
}


#define DEFINE_TYPED_ATTRIBUTE_ACCESSOR_BYVAL(ftname, tname, hton, ntoh) \
int radius_get_ ## ftname ## _attr(const RADIUS_PACKET *packet,		\
	uint8_t type, tname *val)					\
{									\
	const void *p;							\
	tname nval;							\
	size_t len;							\
									\
	if (radius_get_raw_attr_ptr(packet, type, &p, &len) != 0)	\
		return (-1);						\
	if (len != sizeof(tname))					\
		return (-1);						\
	memcpy(&nval, p, sizeof(tname));				\
	*val = ntoh(nval);						\
	return (0);							\
}									\
									\
int radius_get_vs_ ## ftname ## _attr(const RADIUS_PACKET *packet,	\
	uint32_t vendor, uint8_t vtype, tname *val)			\
{									\
	const void *p;							\
	tname nval;							\
	size_t len;							\
									\
	if (radius_get_vs_raw_attr_ptr(packet,				\
			vendor, vtype, &p, &len) != 0)			\
		return (-1);						\
	if (len != sizeof(tname))					\
		return (-1);						\
	memcpy(&nval, p, sizeof(tname));				\
	*val = ntoh(nval);						\
	return (0);							\
}									\
									\
int radius_put_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint8_t type, tname val)					\
{									\
	tname nval;							\
									\
	nval = hton(val);						\
	return radius_put_raw_attr(packet, type, &nval, sizeof(tname));	\
}									\
									\
int radius_put_vs_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint32_t vendor, uint8_t vtype, tname val)			\
{									\
	tname nval;							\
									\
	nval = hton(val);						\
	return radius_put_vs_raw_attr(packet, vendor, vtype,		\
			&nval, sizeof(tname));				\
}									\
									\
int radius_set_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint8_t type, tname val)					\
{									\
	tname nval;							\
									\
	nval = hton(val);						\
	return radius_set_raw_attr(packet, type, &nval, sizeof(tname));	\
}									\
									\
int radius_set_vs_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint32_t vendor, uint8_t vtype, tname val)			\
{									\
	tname nval;							\
									\
	nval = hton(val);						\
	return radius_set_vs_raw_attr(packet, vendor, vtype,		\
			&nval, sizeof(tname));				\
}

#define DEFINE_TYPED_ATTRIBUTE_ACCESSOR_BYPTR(ftname, tname) \
int radius_get_ ## ftname ## _attr(const RADIUS_PACKET *packet,		\
	uint8_t type, tname *val)					\
{									\
	const void	*p;						\
	size_t		 len;						\
									\
	if (radius_get_raw_attr_ptr(packet, type, &p, &len) != 0)	\
		return (-1);						\
	if (len != sizeof(tname))					\
		return (-1);						\
	memcpy(val, p, sizeof(tname));					\
	return (0);							\
}									\
									\
int radius_get_vs_ ## ftname ## _attr(const RADIUS_PACKET *packet,	\
	uint32_t vendor, uint8_t vtype, tname *val)			\
{									\
	const void	*p;						\
	size_t		 len;						\
									\
	if (radius_get_vs_raw_attr_ptr(packet,				\
			vendor, vtype, &p, &len) != 0)			\
		return (-1);						\
	if (len != sizeof(tname))					\
		return (-1);						\
	memcpy(val, p, sizeof(tname));					\
	return (0);							\
}									\
									\
int radius_put_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint8_t type, const tname *val)					\
{									\
	return radius_put_raw_attr(packet, type, val, sizeof(tname));	\
}									\
									\
int radius_put_vs_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint32_t vendor, uint8_t vtype, const tname *val)		\
{									\
	return radius_put_vs_raw_attr(packet, vendor, vtype,		\
			val, sizeof(tname));				\
}									\
									\
int radius_set_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint8_t type, const tname *val)					\
{									\
	return radius_set_raw_attr(packet, type, val, sizeof(tname));	\
}									\
									\
int radius_set_vs_ ## ftname ## _attr(RADIUS_PACKET *packet,		\
	uint32_t vendor, uint8_t vtype, const tname *val)		\
{									\
	return radius_set_vs_raw_attr(packet, vendor, vtype,		\
			val, sizeof(tname));				\
}

DEFINE_TYPED_ATTRIBUTE_ACCESSOR_BYVAL(uint16, uint16_t, htons, ntohs)
DEFINE_TYPED_ATTRIBUTE_ACCESSOR_BYVAL(uint32, uint32_t, htonl, ntohl)
DEFINE_TYPED_ATTRIBUTE_ACCESSOR_BYVAL(uint64, uint64_t, htobe64, betoh64)
DEFINE_TYPED_ATTRIBUTE_ACCESSOR_BYVAL(ipv4, struct in_addr,,)
DEFINE_TYPED_ATTRIBUTE_ACCESSOR_BYPTR(ipv6, struct in6_addr)
