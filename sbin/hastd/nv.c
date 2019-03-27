/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>

#include <bitstring.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ebuf.h>
#include <pjdlog.h>

#include "nv.h"

#ifndef	PJDLOG_ASSERT
#include <assert.h>
#define	PJDLOG_ASSERT(...)	assert(__VA_ARGS__)
#endif
#ifndef	PJDLOG_ABORT
#define	PJDLOG_ABORT(...)	abort()
#endif

#define	NV_TYPE_NONE		0

#define	NV_TYPE_INT8		1
#define	NV_TYPE_UINT8		2
#define	NV_TYPE_INT16		3
#define	NV_TYPE_UINT16		4
#define	NV_TYPE_INT32		5
#define	NV_TYPE_UINT32		6
#define	NV_TYPE_INT64		7
#define	NV_TYPE_UINT64		8
#define	NV_TYPE_INT8_ARRAY	9
#define	NV_TYPE_UINT8_ARRAY	10
#define	NV_TYPE_INT16_ARRAY	11
#define	NV_TYPE_UINT16_ARRAY	12
#define	NV_TYPE_INT32_ARRAY	13
#define	NV_TYPE_UINT32_ARRAY	14
#define	NV_TYPE_INT64_ARRAY	15
#define	NV_TYPE_UINT64_ARRAY	16
#define	NV_TYPE_STRING		17

#define	NV_TYPE_MASK		0x7f
#define	NV_TYPE_FIRST		NV_TYPE_INT8
#define	NV_TYPE_LAST		NV_TYPE_STRING

#define	NV_ORDER_NETWORK	0x00
#define	NV_ORDER_HOST		0x80

#define	NV_ORDER_MASK		0x80

#define	NV_MAGIC	0xaea1e
struct nv {
	int	nv_magic;
	int	nv_error;
	struct ebuf *nv_ebuf;
};

struct nvhdr {
	uint8_t		nvh_type;
	uint8_t		nvh_namesize;
	uint32_t	nvh_dsize;
	char		nvh_name[0];
} __packed;
#define	NVH_DATA(nvh)	((unsigned char *)nvh + NVH_HSIZE(nvh))
#define	NVH_HSIZE(nvh)	\
	(sizeof(struct nvhdr) + roundup2((nvh)->nvh_namesize, 8))
#define	NVH_DSIZE(nvh)	\
	(((nvh)->nvh_type & NV_ORDER_MASK) == NV_ORDER_HOST ?		\
	(nvh)->nvh_dsize :						\
	le32toh((nvh)->nvh_dsize))
#define	NVH_SIZE(nvh)	(NVH_HSIZE(nvh) + roundup2(NVH_DSIZE(nvh), 8))

#define	NV_CHECK(nv)	do {						\
	PJDLOG_ASSERT((nv) != NULL);					\
	PJDLOG_ASSERT((nv)->nv_magic == NV_MAGIC);			\
} while (0)

static void nv_add(struct nv *nv, const unsigned char *value, size_t vsize,
    int type, const char *name);
static void nv_addv(struct nv *nv, const unsigned char *value, size_t vsize,
    int type, const char *namefmt, va_list nameap);
static struct nvhdr *nv_find(struct nv *nv, int type, const char *namefmt,
    va_list nameap);
static void nv_swap(struct nvhdr *nvh, bool tohost);

/*
 * Allocate and initialize new nv structure.
 * Return NULL in case of malloc(3) failure.
 */
struct nv *
nv_alloc(void)
{
	struct nv *nv;

	nv = malloc(sizeof(*nv));
	if (nv == NULL)
		return (NULL);
	nv->nv_ebuf = ebuf_alloc(0);
	if (nv->nv_ebuf == NULL) {
		free(nv);
		return (NULL);
	}
	nv->nv_error = 0;
	nv->nv_magic = NV_MAGIC;
	return (nv);
}

/*
 * Free the given nv structure.
 */
void
nv_free(struct nv *nv)
{

	if (nv == NULL)
		return;

	NV_CHECK(nv);

	nv->nv_magic = 0;
	ebuf_free(nv->nv_ebuf);
	free(nv);
}

/*
 * Return error for the given nv structure.
 */
int
nv_error(const struct nv *nv)
{

	if (nv == NULL)
		return (ENOMEM);

	NV_CHECK(nv);

	return (nv->nv_error);
}

/*
 * Set error for the given nv structure and return previous error.
 */
int
nv_set_error(struct nv *nv, int error)
{
	int preverr;

	if (nv == NULL)
		return (ENOMEM);

	NV_CHECK(nv);

	preverr = nv->nv_error;
	nv->nv_error = error;
	return (preverr);
}

/*
 * Validate correctness of the entire nv structure and all its elements.
 * If extrap is not NULL, store number of extra bytes at the end of the buffer.
 */
int
nv_validate(struct nv *nv, size_t *extrap)
{
	struct nvhdr *nvh;
	unsigned char *data, *ptr;
	size_t dsize, size, vsize;
	int error;

	if (nv == NULL) {
		errno = ENOMEM;
		return (-1);
	}

	NV_CHECK(nv);
	PJDLOG_ASSERT(nv->nv_error == 0);

	/* TODO: Check that names are unique? */

	error = 0;
	ptr = ebuf_data(nv->nv_ebuf, &size);
	while (size > 0) {
		/*
		 * Zeros at the end of the buffer are acceptable.
		 */
		if (ptr[0] == '\0')
			break;
		/*
		 * Minimum size at this point is size of nvhdr structure, one
		 * character long name plus terminating '\0'.
		 */
		if (size < sizeof(*nvh) + 2) {
			error = EINVAL;
			break;
		}
		nvh = (struct nvhdr *)ptr;
		if (size < NVH_HSIZE(nvh)) {
			error = EINVAL;
			break;
		}
		if (nvh->nvh_name[nvh->nvh_namesize - 1] != '\0') {
			error = EINVAL;
			break;
		}
		if (strlen(nvh->nvh_name) !=
		    (size_t)(nvh->nvh_namesize - 1)) {
			error = EINVAL;
			break;
		}
		if ((nvh->nvh_type & NV_TYPE_MASK) < NV_TYPE_FIRST ||
		    (nvh->nvh_type & NV_TYPE_MASK) > NV_TYPE_LAST) {
			error = EINVAL;
			break;
		}
		dsize = NVH_DSIZE(nvh);
		if (dsize == 0) {
			error = EINVAL;
			break;
		}
		if (size < NVH_SIZE(nvh)) {
			error = EINVAL;
			break;
		}
		vsize = 0;
		switch (nvh->nvh_type & NV_TYPE_MASK) {
		case NV_TYPE_INT8:
		case NV_TYPE_UINT8:
			if (vsize == 0)
				vsize = 1;
			/* FALLTHROUGH */
		case NV_TYPE_INT16:
		case NV_TYPE_UINT16:
			if (vsize == 0)
				vsize = 2;
			/* FALLTHROUGH */
		case NV_TYPE_INT32:
		case NV_TYPE_UINT32:
			if (vsize == 0)
				vsize = 4;
			/* FALLTHROUGH */
		case NV_TYPE_INT64:
		case NV_TYPE_UINT64:
			if (vsize == 0)
				vsize = 8;
			if (dsize != vsize) {
				error = EINVAL;
				break;
			}
			break;
		case NV_TYPE_INT8_ARRAY:
		case NV_TYPE_UINT8_ARRAY:
			break;
		case NV_TYPE_INT16_ARRAY:
		case NV_TYPE_UINT16_ARRAY:
			if (vsize == 0)
				vsize = 2;
			/* FALLTHROUGH */
		case NV_TYPE_INT32_ARRAY:
		case NV_TYPE_UINT32_ARRAY:
			if (vsize == 0)
				vsize = 4;
			/* FALLTHROUGH */
		case NV_TYPE_INT64_ARRAY:
		case NV_TYPE_UINT64_ARRAY:
			if (vsize == 0)
				vsize = 8;
			if ((dsize % vsize) != 0) {
				error = EINVAL;
				break;
			}
			break;
		case NV_TYPE_STRING:
			data = NVH_DATA(nvh);
			if (data[dsize - 1] != '\0') {
				error = EINVAL;
				break;
			}
			if (strlen((char *)data) != dsize - 1) {
				error = EINVAL;
				break;
			}
			break;
		default:
			PJDLOG_ABORT("invalid condition");
		}
		if (error != 0)
			break;
		ptr += NVH_SIZE(nvh);
		size -= NVH_SIZE(nvh);
	}
	if (error != 0) {
		errno = error;
		if (nv->nv_error == 0)
			nv->nv_error = error;
		return (-1);
	}
	if (extrap != NULL)
		*extrap = size;
	return (0);
}

/*
 * Convert the given nv structure to network byte order and return ebuf
 * structure.
 */
struct ebuf *
nv_hton(struct nv *nv)
{
	struct nvhdr *nvh;
	unsigned char *ptr;
	size_t size;

	NV_CHECK(nv);
	PJDLOG_ASSERT(nv->nv_error == 0);

	ptr = ebuf_data(nv->nv_ebuf, &size);
	while (size > 0) {
		/*
		 * Minimum size at this point is size of nvhdr structure,
		 * one character long name plus terminating '\0'.
		 */
		PJDLOG_ASSERT(size >= sizeof(*nvh) + 2);
		nvh = (struct nvhdr *)ptr;
		PJDLOG_ASSERT(NVH_SIZE(nvh) <= size);
		nv_swap(nvh, false);
		ptr += NVH_SIZE(nvh);
		size -= NVH_SIZE(nvh);
	}

	return (nv->nv_ebuf);
}

/*
 * Create nv structure based on ebuf received from the network.
 */
struct nv *
nv_ntoh(struct ebuf *eb)
{
	struct nv *nv;
	size_t extra;
	int rerrno;

	PJDLOG_ASSERT(eb != NULL);

	nv = malloc(sizeof(*nv));
	if (nv == NULL)
		return (NULL);
	nv->nv_error = 0;
	nv->nv_ebuf = eb;
	nv->nv_magic = NV_MAGIC;

	if (nv_validate(nv, &extra) == -1) {
		rerrno = errno;
		nv->nv_magic = 0;
		free(nv);
		errno = rerrno;
		return (NULL);
	}
	/*
	 * Remove extra zeros at the end of the buffer.
	 */
	ebuf_del_tail(eb, extra);

	return (nv);
}

#define	NV_DEFINE_ADD(type, TYPE)					\
void									\
nv_add_##type(struct nv *nv, type##_t value, const char *namefmt, ...)	\
{									\
	va_list nameap;							\
									\
	va_start(nameap, namefmt);					\
	nv_addv(nv, (unsigned char *)&value, sizeof(value),		\
	    NV_TYPE_##TYPE, namefmt, nameap);				\
	va_end(nameap);							\
}

NV_DEFINE_ADD(int8, INT8)
NV_DEFINE_ADD(uint8, UINT8)
NV_DEFINE_ADD(int16, INT16)
NV_DEFINE_ADD(uint16, UINT16)
NV_DEFINE_ADD(int32, INT32)
NV_DEFINE_ADD(uint32, UINT32)
NV_DEFINE_ADD(int64, INT64)
NV_DEFINE_ADD(uint64, UINT64)

#undef	NV_DEFINE_ADD

#define	NV_DEFINE_ADD_ARRAY(type, TYPE)					\
void									\
nv_add_##type##_array(struct nv *nv, const type##_t *value,		\
    size_t nsize, const char *namefmt, ...)				\
{									\
	va_list nameap;							\
									\
	va_start(nameap, namefmt);					\
	nv_addv(nv, (const unsigned char *)value,			\
	    sizeof(value[0]) * nsize, NV_TYPE_##TYPE##_ARRAY, namefmt,	\
	    nameap);							\
	va_end(nameap);							\
}

NV_DEFINE_ADD_ARRAY(int8, INT8)
NV_DEFINE_ADD_ARRAY(uint8, UINT8)
NV_DEFINE_ADD_ARRAY(int16, INT16)
NV_DEFINE_ADD_ARRAY(uint16, UINT16)
NV_DEFINE_ADD_ARRAY(int32, INT32)
NV_DEFINE_ADD_ARRAY(uint32, UINT32)
NV_DEFINE_ADD_ARRAY(int64, INT64)
NV_DEFINE_ADD_ARRAY(uint64, UINT64)

#undef	NV_DEFINE_ADD_ARRAY

void
nv_add_string(struct nv *nv, const char *value, const char *namefmt, ...)
{
	va_list nameap;
	size_t size;

	size = strlen(value) + 1;

	va_start(nameap, namefmt);
	nv_addv(nv, (const unsigned char *)value, size, NV_TYPE_STRING,
	    namefmt, nameap);
	va_end(nameap);
}

void
nv_add_stringf(struct nv *nv, const char *name, const char *valuefmt, ...)
{
	va_list valueap;

	va_start(valueap, valuefmt);
	nv_add_stringv(nv, name, valuefmt, valueap);
	va_end(valueap);
}

void
nv_add_stringv(struct nv *nv, const char *name, const char *valuefmt,
    va_list valueap)
{
	char *value;
	ssize_t size;

	size = vasprintf(&value, valuefmt, valueap);
	if (size == -1) {
		if (nv->nv_error == 0)
			nv->nv_error = ENOMEM;
		return;
	}
	size++;
	nv_add(nv, (const unsigned char *)value, size, NV_TYPE_STRING, name);
	free(value);
}

#define	NV_DEFINE_GET(type, TYPE)					\
type##_t								\
nv_get_##type(struct nv *nv, const char *namefmt, ...)			\
{									\
	struct nvhdr *nvh;						\
	va_list nameap;							\
	type##_t value;							\
									\
	va_start(nameap, namefmt);					\
	nvh = nv_find(nv, NV_TYPE_##TYPE, namefmt, nameap);		\
	va_end(nameap);							\
	if (nvh == NULL)						\
		return (0);						\
	PJDLOG_ASSERT((nvh->nvh_type & NV_ORDER_MASK) == NV_ORDER_HOST);\
	PJDLOG_ASSERT(sizeof(value) == nvh->nvh_dsize);			\
	bcopy(NVH_DATA(nvh), &value, sizeof(value));			\
									\
	return (value);							\
}

NV_DEFINE_GET(int8, INT8)
NV_DEFINE_GET(uint8, UINT8)
NV_DEFINE_GET(int16, INT16)
NV_DEFINE_GET(uint16, UINT16)
NV_DEFINE_GET(int32, INT32)
NV_DEFINE_GET(uint32, UINT32)
NV_DEFINE_GET(int64, INT64)
NV_DEFINE_GET(uint64, UINT64)

#undef	NV_DEFINE_GET

#define	NV_DEFINE_GET_ARRAY(type, TYPE)					\
const type##_t *							\
nv_get_##type##_array(struct nv *nv, size_t *sizep,			\
    const char *namefmt, ...)						\
{									\
	struct nvhdr *nvh;						\
	va_list nameap;							\
									\
	va_start(nameap, namefmt);					\
	nvh = nv_find(nv, NV_TYPE_##TYPE##_ARRAY, namefmt, nameap);	\
	va_end(nameap);							\
	if (nvh == NULL)						\
		return (NULL);						\
	PJDLOG_ASSERT((nvh->nvh_type & NV_ORDER_MASK) == NV_ORDER_HOST);\
	PJDLOG_ASSERT((nvh->nvh_dsize % sizeof(type##_t)) == 0);	\
	if (sizep != NULL)						\
		*sizep = nvh->nvh_dsize / sizeof(type##_t);		\
	return ((type##_t *)(void *)NVH_DATA(nvh));			\
}

NV_DEFINE_GET_ARRAY(int8, INT8)
NV_DEFINE_GET_ARRAY(uint8, UINT8)
NV_DEFINE_GET_ARRAY(int16, INT16)
NV_DEFINE_GET_ARRAY(uint16, UINT16)
NV_DEFINE_GET_ARRAY(int32, INT32)
NV_DEFINE_GET_ARRAY(uint32, UINT32)
NV_DEFINE_GET_ARRAY(int64, INT64)
NV_DEFINE_GET_ARRAY(uint64, UINT64)

#undef	NV_DEFINE_GET_ARRAY

const char *
nv_get_string(struct nv *nv, const char *namefmt, ...)
{
	struct nvhdr *nvh;
	va_list nameap;
	char *str;

	va_start(nameap, namefmt);
	nvh = nv_find(nv, NV_TYPE_STRING, namefmt, nameap);
	va_end(nameap);
	if (nvh == NULL)
		return (NULL);
	PJDLOG_ASSERT((nvh->nvh_type & NV_ORDER_MASK) == NV_ORDER_HOST);
	PJDLOG_ASSERT(nvh->nvh_dsize >= 1);
	str = (char *)NVH_DATA(nvh);
	PJDLOG_ASSERT(str[nvh->nvh_dsize - 1] == '\0');
	PJDLOG_ASSERT(strlen(str) == nvh->nvh_dsize - 1);
	return (str);
}

static bool
nv_vexists(struct nv *nv, const char *namefmt, va_list nameap)
{
	struct nvhdr *nvh;
	int snverror, serrno;

	if (nv == NULL)
		return (false);

	serrno = errno;
	snverror = nv->nv_error;

	nvh = nv_find(nv, NV_TYPE_NONE, namefmt, nameap);

	errno = serrno;
	nv->nv_error = snverror;

	return (nvh != NULL);
}

bool
nv_exists(struct nv *nv, const char *namefmt, ...)
{
	va_list nameap;
	bool ret;

	va_start(nameap, namefmt);
	ret = nv_vexists(nv, namefmt, nameap);
	va_end(nameap);

	return (ret);
}

void
nv_assert(struct nv *nv, const char *namefmt, ...)
{
	va_list nameap;

	va_start(nameap, namefmt);
	PJDLOG_ASSERT(nv_vexists(nv, namefmt, nameap));
	va_end(nameap);
}

/*
 * Dump content of the nv structure.
 */
void
nv_dump(struct nv *nv)
{
	struct nvhdr *nvh;
	unsigned char *data, *ptr;
	size_t dsize, size;
	unsigned int ii;
	bool swap;

	if (nv_validate(nv, NULL) == -1) {
		printf("error: %d\n", errno);
		return;
	}

	NV_CHECK(nv);
	PJDLOG_ASSERT(nv->nv_error == 0);

	ptr = ebuf_data(nv->nv_ebuf, &size);
	while (size > 0) {
		PJDLOG_ASSERT(size >= sizeof(*nvh) + 2);
		nvh = (struct nvhdr *)ptr;
		PJDLOG_ASSERT(size >= NVH_SIZE(nvh));
		swap = ((nvh->nvh_type & NV_ORDER_MASK) == NV_ORDER_NETWORK);
		dsize = NVH_DSIZE(nvh);
		data = NVH_DATA(nvh);
		printf("  %s", nvh->nvh_name);
		switch (nvh->nvh_type & NV_TYPE_MASK) {
		case NV_TYPE_INT8:
			printf("(int8): %jd", (intmax_t)(*(int8_t *)data));
			break;
		case NV_TYPE_UINT8:
			printf("(uint8): %ju", (uintmax_t)(*(uint8_t *)data));
			break;
		case NV_TYPE_INT16:
			printf("(int16): %jd", swap ?
			    (intmax_t)le16toh(*(int16_t *)(void *)data) :
			    (intmax_t)*(int16_t *)(void *)data);
			break;
		case NV_TYPE_UINT16:
			printf("(uint16): %ju", swap ?
			    (uintmax_t)le16toh(*(uint16_t *)(void *)data) :
			    (uintmax_t)*(uint16_t *)(void *)data);
			break;
		case NV_TYPE_INT32:
			printf("(int32): %jd", swap ?
			    (intmax_t)le32toh(*(int32_t *)(void *)data) :
			    (intmax_t)*(int32_t *)(void *)data);
			break;
		case NV_TYPE_UINT32:
			printf("(uint32): %ju", swap ?
			    (uintmax_t)le32toh(*(uint32_t *)(void *)data) :
			    (uintmax_t)*(uint32_t *)(void *)data);
			break;
		case NV_TYPE_INT64:
			printf("(int64): %jd", swap ?
			    (intmax_t)le64toh(*(int64_t *)(void *)data) :
			    (intmax_t)*(int64_t *)(void *)data);
			break;
		case NV_TYPE_UINT64:
			printf("(uint64): %ju", swap ?
			    (uintmax_t)le64toh(*(uint64_t *)(void *)data) :
			    (uintmax_t)*(uint64_t *)(void *)data);
			break;
		case NV_TYPE_INT8_ARRAY:
			printf("(int8 array):");
			for (ii = 0; ii < dsize; ii++)
				printf(" %jd", (intmax_t)((int8_t *)data)[ii]);
			break;
		case NV_TYPE_UINT8_ARRAY:
			printf("(uint8 array):");
			for (ii = 0; ii < dsize; ii++)
				printf(" %ju", (uintmax_t)((uint8_t *)data)[ii]);
			break;
		case NV_TYPE_INT16_ARRAY:
			printf("(int16 array):");
			for (ii = 0; ii < dsize / 2; ii++) {
				printf(" %jd", swap ?
				    (intmax_t)le16toh(((int16_t *)(void *)data)[ii]) :
				    (intmax_t)((int16_t *)(void *)data)[ii]);
			}
			break;
		case NV_TYPE_UINT16_ARRAY:
			printf("(uint16 array):");
			for (ii = 0; ii < dsize / 2; ii++) {
				printf(" %ju", swap ?
				    (uintmax_t)le16toh(((uint16_t *)(void *)data)[ii]) :
				    (uintmax_t)((uint16_t *)(void *)data)[ii]);
			}
			break;
		case NV_TYPE_INT32_ARRAY:
			printf("(int32 array):");
			for (ii = 0; ii < dsize / 4; ii++) {
				printf(" %jd", swap ?
				    (intmax_t)le32toh(((int32_t *)(void *)data)[ii]) :
				    (intmax_t)((int32_t *)(void *)data)[ii]);
			}
			break;
		case NV_TYPE_UINT32_ARRAY:
			printf("(uint32 array):");
			for (ii = 0; ii < dsize / 4; ii++) {
				printf(" %ju", swap ?
				    (uintmax_t)le32toh(((uint32_t *)(void *)data)[ii]) :
				    (uintmax_t)((uint32_t *)(void *)data)[ii]);
			}
			break;
		case NV_TYPE_INT64_ARRAY:
			printf("(int64 array):");
			for (ii = 0; ii < dsize / 8; ii++) {
				printf(" %ju", swap ?
				    (uintmax_t)le64toh(((uint64_t *)(void *)data)[ii]) :
				    (uintmax_t)((uint64_t *)(void *)data)[ii]);
			}
			break;
		case NV_TYPE_UINT64_ARRAY:
			printf("(uint64 array):");
			for (ii = 0; ii < dsize / 8; ii++) {
				printf(" %ju", swap ?
				    (uintmax_t)le64toh(((uint64_t *)(void *)data)[ii]) :
				    (uintmax_t)((uint64_t *)(void *)data)[ii]);
			}
			break;
		case NV_TYPE_STRING:
			printf("(string): %s", (char *)data);
			break;
		default:
			PJDLOG_ABORT("invalid condition");
		}
		printf("\n");
		ptr += NVH_SIZE(nvh);
		size -= NVH_SIZE(nvh);
	}
}

/*
 * Local routines below.
 */

static void
nv_add(struct nv *nv, const unsigned char *value, size_t vsize, int type,
    const char *name)
{
	static unsigned char align[7];
	struct nvhdr *nvh;
	size_t namesize;

	if (nv == NULL) {
		errno = ENOMEM;
		return;
	}

	NV_CHECK(nv);

	namesize = strlen(name) + 1;

	nvh = malloc(sizeof(*nvh) + roundup2(namesize, 8));
	if (nvh == NULL) {
		if (nv->nv_error == 0)
			nv->nv_error = ENOMEM;
		return;
	}
	nvh->nvh_type = NV_ORDER_HOST | type;
	nvh->nvh_namesize = (uint8_t)namesize;
	nvh->nvh_dsize = (uint32_t)vsize;
	bcopy(name, nvh->nvh_name, namesize);

	/* Add header first. */
	if (ebuf_add_tail(nv->nv_ebuf, nvh, NVH_HSIZE(nvh)) == -1) {
		PJDLOG_ASSERT(errno != 0);
		if (nv->nv_error == 0)
			nv->nv_error = errno;
		free(nvh);
		return;
	}
	free(nvh);
	/* Add the actual data. */
	if (ebuf_add_tail(nv->nv_ebuf, value, vsize) == -1) {
		PJDLOG_ASSERT(errno != 0);
		if (nv->nv_error == 0)
			nv->nv_error = errno;
		return;
	}
	/* Align the data (if needed). */
	vsize = roundup2(vsize, 8) - vsize;
	if (vsize == 0)
		return;
	PJDLOG_ASSERT(vsize > 0 && vsize <= sizeof(align));
	if (ebuf_add_tail(nv->nv_ebuf, align, vsize) == -1) {
		PJDLOG_ASSERT(errno != 0);
		if (nv->nv_error == 0)
			nv->nv_error = errno;
		return;
	}
}

static void
nv_addv(struct nv *nv, const unsigned char *value, size_t vsize, int type,
    const char *namefmt, va_list nameap)
{
	char name[255];
	size_t namesize;

	namesize = vsnprintf(name, sizeof(name), namefmt, nameap);
	PJDLOG_ASSERT(namesize > 0 && namesize < sizeof(name));

	nv_add(nv, value, vsize, type, name);
}

static struct nvhdr *
nv_find(struct nv *nv, int type, const char *namefmt, va_list nameap)
{
	char name[255];
	struct nvhdr *nvh;
	unsigned char *ptr;
	size_t size, namesize;

	if (nv == NULL) {
		errno = ENOMEM;
		return (NULL);
	}

	NV_CHECK(nv);

	namesize = vsnprintf(name, sizeof(name), namefmt, nameap);
	PJDLOG_ASSERT(namesize > 0 && namesize < sizeof(name));
	namesize++;

	ptr = ebuf_data(nv->nv_ebuf, &size);
	while (size > 0) {
		PJDLOG_ASSERT(size >= sizeof(*nvh) + 2);
		nvh = (struct nvhdr *)ptr;
		PJDLOG_ASSERT(size >= NVH_SIZE(nvh));
		nv_swap(nvh, true);
		if (strcmp(nvh->nvh_name, name) == 0) {
			if (type != NV_TYPE_NONE &&
			    (nvh->nvh_type & NV_TYPE_MASK) != type) {
				errno = EINVAL;
				if (nv->nv_error == 0)
					nv->nv_error = EINVAL;
				return (NULL);
			}
			return (nvh);
		}
		ptr += NVH_SIZE(nvh);
		size -= NVH_SIZE(nvh);
	}
	errno = ENOENT;
	if (nv->nv_error == 0)
		nv->nv_error = ENOENT;
	return (NULL);
}

static void
nv_swap(struct nvhdr *nvh, bool tohost)
{
	unsigned char *data, *end, *p;
	size_t vsize;

	data = NVH_DATA(nvh);
	if (tohost) {
		if ((nvh->nvh_type & NV_ORDER_MASK) == NV_ORDER_HOST)
			return;
		nvh->nvh_dsize = le32toh(nvh->nvh_dsize);
		end = data + nvh->nvh_dsize;
		nvh->nvh_type &= ~NV_ORDER_MASK;
		nvh->nvh_type |= NV_ORDER_HOST;
	} else {
		if ((nvh->nvh_type & NV_ORDER_MASK) == NV_ORDER_NETWORK)
			return;
		end = data + nvh->nvh_dsize;
		nvh->nvh_dsize = htole32(nvh->nvh_dsize);
		nvh->nvh_type &= ~NV_ORDER_MASK;
		nvh->nvh_type |= NV_ORDER_NETWORK;
	}

	vsize = 0;

	switch (nvh->nvh_type & NV_TYPE_MASK) {
	case NV_TYPE_INT8:
	case NV_TYPE_UINT8:
	case NV_TYPE_INT8_ARRAY:
	case NV_TYPE_UINT8_ARRAY:
		break;
	case NV_TYPE_INT16:
	case NV_TYPE_UINT16:
	case NV_TYPE_INT16_ARRAY:
	case NV_TYPE_UINT16_ARRAY:
		if (vsize == 0)
			vsize = 2;
		/* FALLTHROUGH */
	case NV_TYPE_INT32:
	case NV_TYPE_UINT32:
	case NV_TYPE_INT32_ARRAY:
	case NV_TYPE_UINT32_ARRAY:
		if (vsize == 0)
			vsize = 4;
		/* FALLTHROUGH */
	case NV_TYPE_INT64:
	case NV_TYPE_UINT64:
	case NV_TYPE_INT64_ARRAY:
	case NV_TYPE_UINT64_ARRAY:
		if (vsize == 0)
			vsize = 8;
		for (p = data; p < end; p += vsize) {
			if (tohost) {
				switch (vsize) {
				case 2:
					*(uint16_t *)(void *)p =
					    le16toh(*(uint16_t *)(void *)p);
					break;
				case 4:
					*(uint32_t *)(void *)p =
					    le32toh(*(uint32_t *)(void *)p);
					break;
				case 8:
					*(uint64_t *)(void *)p =
					    le64toh(*(uint64_t *)(void *)p);
					break;
				default:
					PJDLOG_ABORT("invalid condition");
				}
			} else {
				switch (vsize) {
				case 2:
					*(uint16_t *)(void *)p =
					    htole16(*(uint16_t *)(void *)p);
					break;
				case 4:
					*(uint32_t *)(void *)p =
					    htole32(*(uint32_t *)(void *)p);
					break;
				case 8:
					*(uint64_t *)(void *)p =
					    htole64(*(uint64_t *)(void *)p);
					break;
				default:
					PJDLOG_ABORT("invalid condition");
				}
			}
		}
		break;
	case NV_TYPE_STRING:
		break;
	default:
		PJDLOG_ABORT("unrecognized type");
	}
}
