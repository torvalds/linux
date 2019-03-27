/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013 The FreeBSD Foundation
 * Copyright (c) 2013-2015 Mariusz Zaborski <oshogbo@FreeBSD.org>
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
#include <sys/queue.h>

#ifdef _KERNEL

#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/stdarg.h>

#else
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common_impl.h"
#endif

#ifdef HAVE_PJDLOG
#include <pjdlog.h>
#endif

#include <sys/nv.h>

#include "nv_impl.h"
#include "nvlist_impl.h"
#include "nvpair_impl.h"

#ifndef	HAVE_PJDLOG
#ifdef _KERNEL
#define	PJDLOG_ASSERT(...)		MPASS(__VA_ARGS__)
#define	PJDLOG_RASSERT(expr, ...)	KASSERT(expr, (__VA_ARGS__))
#define	PJDLOG_ABORT(...)		panic(__VA_ARGS__)
#else
#include <assert.h>
#define	PJDLOG_ASSERT(...)		assert(__VA_ARGS__)
#define	PJDLOG_RASSERT(expr, ...)	assert(expr)
#define	PJDLOG_ABORT(...)		abort()
#endif
#endif

#define	NVPAIR_MAGIC	0x6e7670	/* "nvp" */
struct nvpair {
	int		 nvp_magic;
	char		*nvp_name;
	int		 nvp_type;
	uint64_t	 nvp_data;
	size_t		 nvp_datasize;
	size_t		 nvp_nitems;	/* Used only for array types. */
	nvlist_t	*nvp_list;
	TAILQ_ENTRY(nvpair) nvp_next;
};

#define	NVPAIR_ASSERT(nvp)	do {					\
	PJDLOG_ASSERT((nvp) != NULL);					\
	PJDLOG_ASSERT((nvp)->nvp_magic == NVPAIR_MAGIC);		\
} while (0)

struct nvpair_header {
	uint8_t		nvph_type;
	uint16_t	nvph_namesize;
	uint64_t	nvph_datasize;
	uint64_t	nvph_nitems;
} __packed;


void
nvpair_assert(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
}

static nvpair_t *
nvpair_allocv(const char *name, int type, uint64_t data, size_t datasize,
    size_t nitems)
{
	nvpair_t *nvp;
	size_t namelen;

	PJDLOG_ASSERT(type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST);

	namelen = strlen(name);
	if (namelen >= NV_NAME_MAX) {
		ERRNO_SET(ENAMETOOLONG);
		return (NULL);
	}

	nvp = nv_calloc(1, sizeof(*nvp) + namelen + 1);
	if (nvp != NULL) {
		nvp->nvp_name = (char *)(nvp + 1);
		memcpy(nvp->nvp_name, name, namelen);
		nvp->nvp_name[namelen] = '\0';
		nvp->nvp_type = type;
		nvp->nvp_data = data;
		nvp->nvp_datasize = datasize;
		nvp->nvp_nitems = nitems;
		nvp->nvp_magic = NVPAIR_MAGIC;
	}

	return (nvp);
}

static int
nvpair_append(nvpair_t *nvp, const void *value, size_t valsize, size_t datasize)
{
	void *olddata, *data, *valp;
	size_t oldlen;

	oldlen = nvp->nvp_nitems * valsize;
	olddata = (void *)(uintptr_t)nvp->nvp_data;
	data = nv_realloc(olddata, oldlen + valsize);
	if (data == NULL) {
		ERRNO_SET(ENOMEM);
		return (-1);
	}
	valp = (unsigned char *)data + oldlen;
	memcpy(valp, value, valsize);

	nvp->nvp_data = (uint64_t)(uintptr_t)data;
	nvp->nvp_datasize += datasize;
	nvp->nvp_nitems++;
	return (0);
}

nvlist_t *
nvpair_nvlist(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);

	return (nvp->nvp_list);
}

nvpair_t *
nvpair_next(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_list != NULL);

	return (TAILQ_NEXT(nvp, nvp_next));
}

nvpair_t *
nvpair_prev(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_list != NULL);

	return (TAILQ_PREV(nvp, nvl_head, nvp_next));
}

void
nvpair_insert(struct nvl_head *head, nvpair_t *nvp, nvlist_t *nvl)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_list == NULL);
	PJDLOG_ASSERT((nvlist_flags(nvl) & NV_FLAG_NO_UNIQUE) != 0 ||
	    !nvlist_exists(nvl, nvpair_name(nvp)));

	TAILQ_INSERT_TAIL(head, nvp, nvp_next);
	nvp->nvp_list = nvl;
}

static void
nvpair_remove_nvlist(nvpair_t *nvp)
{
	nvlist_t *nvl;

	/* XXX: DECONST is bad, mkay? */
	nvl = __DECONST(nvlist_t *, nvpair_get_nvlist(nvp));
	PJDLOG_ASSERT(nvl != NULL);
	nvlist_set_parent(nvl, NULL);
}

static void
nvpair_remove_nvlist_array(nvpair_t *nvp)
{
	nvlist_t **nvlarray;
	size_t count, i;

	/* XXX: DECONST is bad, mkay? */
	nvlarray = __DECONST(nvlist_t **,
	    nvpair_get_nvlist_array(nvp, &count));
	for (i = 0; i < count; i++) {
		nvlist_set_array_next(nvlarray[i], NULL);
		nvlist_set_parent(nvlarray[i], NULL);
	}
}

void
nvpair_remove(struct nvl_head *head, nvpair_t *nvp, const nvlist_t *nvl)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_list == nvl);

	if (nvpair_type(nvp) == NV_TYPE_NVLIST)
		nvpair_remove_nvlist(nvp);
	else if (nvpair_type(nvp) == NV_TYPE_NVLIST_ARRAY)
		nvpair_remove_nvlist_array(nvp);

	TAILQ_REMOVE(head, nvp, nvp_next);
	nvp->nvp_list = NULL;
}

nvpair_t *
nvpair_clone(const nvpair_t *nvp)
{
	nvpair_t *newnvp;
	const char *name;
	const void *data;
	size_t datasize;

	NVPAIR_ASSERT(nvp);

	name = nvpair_name(nvp);

	switch (nvpair_type(nvp)) {
	case NV_TYPE_NULL:
		newnvp = nvpair_create_null(name);
		break;
	case NV_TYPE_BOOL:
		newnvp = nvpair_create_bool(name, nvpair_get_bool(nvp));
		break;
	case NV_TYPE_NUMBER:
		newnvp = nvpair_create_number(name, nvpair_get_number(nvp));
		break;
	case NV_TYPE_STRING:
		newnvp = nvpair_create_string(name, nvpair_get_string(nvp));
		break;
	case NV_TYPE_NVLIST:
		newnvp = nvpair_create_nvlist(name, nvpair_get_nvlist(nvp));
		break;
	case NV_TYPE_BINARY:
		data = nvpair_get_binary(nvp, &datasize);
		newnvp = nvpair_create_binary(name, data, datasize);
		break;
	case NV_TYPE_BOOL_ARRAY:
		data = nvpair_get_bool_array(nvp, &datasize);
		newnvp = nvpair_create_bool_array(name, data, datasize);
		break;
	case NV_TYPE_NUMBER_ARRAY:
		data = nvpair_get_number_array(nvp, &datasize);
		newnvp = nvpair_create_number_array(name, data, datasize);
		break;
	case NV_TYPE_STRING_ARRAY:
		data = nvpair_get_string_array(nvp, &datasize);
		newnvp = nvpair_create_string_array(name, data, datasize);
		break;
	case NV_TYPE_NVLIST_ARRAY:
		data = nvpair_get_nvlist_array(nvp, &datasize);
		newnvp = nvpair_create_nvlist_array(name, data, datasize);
		break;
#ifndef _KERNEL
	case NV_TYPE_DESCRIPTOR:
		newnvp = nvpair_create_descriptor(name,
		    nvpair_get_descriptor(nvp));
		break;
	case NV_TYPE_DESCRIPTOR_ARRAY:
		data = nvpair_get_descriptor_array(nvp, &datasize);
		newnvp = nvpair_create_descriptor_array(name, data, datasize);
		break;
#endif
	default:
		PJDLOG_ABORT("Unknown type: %d.", nvpair_type(nvp));
	}

	return (newnvp);
}

size_t
nvpair_header_size(void)
{

	return (sizeof(struct nvpair_header));
}

size_t
nvpair_size(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);

	return (nvp->nvp_datasize);
}

unsigned char *
nvpair_pack_header(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{
	struct nvpair_header nvphdr;
	size_t namesize;

	NVPAIR_ASSERT(nvp);

	nvphdr.nvph_type = nvp->nvp_type;
	namesize = strlen(nvp->nvp_name) + 1;
	PJDLOG_ASSERT(namesize > 0 && namesize <= UINT16_MAX);
	nvphdr.nvph_namesize = namesize;
	nvphdr.nvph_datasize = nvp->nvp_datasize;
	nvphdr.nvph_nitems = nvp->nvp_nitems;
	PJDLOG_ASSERT(*leftp >= sizeof(nvphdr));
	memcpy(ptr, &nvphdr, sizeof(nvphdr));
	ptr += sizeof(nvphdr);
	*leftp -= sizeof(nvphdr);

	PJDLOG_ASSERT(*leftp >= namesize);
	memcpy(ptr, nvp->nvp_name, namesize);
	ptr += namesize;
	*leftp -= namesize;

	return (ptr);
}

unsigned char *
nvpair_pack_null(const nvpair_t *nvp, unsigned char *ptr,
    size_t *leftp __unused)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NULL);

	return (ptr);
}

unsigned char *
nvpair_pack_bool(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{
	uint8_t value;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BOOL);

	value = (uint8_t)nvp->nvp_data;

	PJDLOG_ASSERT(*leftp >= sizeof(value));
	memcpy(ptr, &value, sizeof(value));
	ptr += sizeof(value);
	*leftp -= sizeof(value);

	return (ptr);
}

unsigned char *
nvpair_pack_number(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{
	uint64_t value;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NUMBER);

	value = (uint64_t)nvp->nvp_data;

	PJDLOG_ASSERT(*leftp >= sizeof(value));
	memcpy(ptr, &value, sizeof(value));
	ptr += sizeof(value);
	*leftp -= sizeof(value);

	return (ptr);
}

unsigned char *
nvpair_pack_string(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_STRING);

	PJDLOG_ASSERT(*leftp >= nvp->nvp_datasize);
	memcpy(ptr, (const void *)(intptr_t)nvp->nvp_data, nvp->nvp_datasize);
	ptr += nvp->nvp_datasize;
	*leftp -= nvp->nvp_datasize;

	return (ptr);
}

unsigned char *
nvpair_pack_nvlist_up(unsigned char *ptr, size_t *leftp)
{
	struct nvpair_header nvphdr;
	size_t namesize;
	const char *name = "";

	namesize = 1;
	nvphdr.nvph_type = NV_TYPE_NVLIST_UP;
	nvphdr.nvph_namesize = namesize;
	nvphdr.nvph_datasize = 0;
	nvphdr.nvph_nitems = 0;
	PJDLOG_ASSERT(*leftp >= sizeof(nvphdr));
	memcpy(ptr, &nvphdr, sizeof(nvphdr));
	ptr += sizeof(nvphdr);
	*leftp -= sizeof(nvphdr);

	PJDLOG_ASSERT(*leftp >= namesize);
	memcpy(ptr, name, namesize);
	ptr += namesize;
	*leftp -= namesize;

	return (ptr);
}

unsigned char *
nvpair_pack_nvlist_array_next(unsigned char *ptr, size_t *leftp)
{
	struct nvpair_header nvphdr;
	size_t namesize;
	const char *name = "";

	namesize = 1;
	nvphdr.nvph_type = NV_TYPE_NVLIST_ARRAY_NEXT;
	nvphdr.nvph_namesize = namesize;
	nvphdr.nvph_datasize = 0;
	nvphdr.nvph_nitems = 0;
	PJDLOG_ASSERT(*leftp >= sizeof(nvphdr));
	memcpy(ptr, &nvphdr, sizeof(nvphdr));
	ptr += sizeof(nvphdr);
	*leftp -= sizeof(nvphdr);

	PJDLOG_ASSERT(*leftp >= namesize);
	memcpy(ptr, name, namesize);
	ptr += namesize;
	*leftp -= namesize;

	return (ptr);
}

#ifndef _KERNEL
unsigned char *
nvpair_pack_descriptor(const nvpair_t *nvp, unsigned char *ptr, int64_t *fdidxp,
    size_t *leftp)
{
	int64_t value;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR);

	value = (int64_t)nvp->nvp_data;
	if (value != -1) {
		/*
		 * If there is a real descriptor here, we change its number
		 * to position in the array of descriptors send via control
		 * message.
		 */
		PJDLOG_ASSERT(fdidxp != NULL);

		value = *fdidxp;
		(*fdidxp)++;
	}

	PJDLOG_ASSERT(*leftp >= sizeof(value));
	memcpy(ptr, &value, sizeof(value));
	ptr += sizeof(value);
	*leftp -= sizeof(value);

	return (ptr);
}
#endif

unsigned char *
nvpair_pack_binary(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BINARY);

	PJDLOG_ASSERT(*leftp >= nvp->nvp_datasize);
	memcpy(ptr, (const void *)(intptr_t)nvp->nvp_data, nvp->nvp_datasize);
	ptr += nvp->nvp_datasize;
	*leftp -= nvp->nvp_datasize;

	return (ptr);
}

unsigned char *
nvpair_pack_bool_array(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BOOL_ARRAY);
	PJDLOG_ASSERT(*leftp >= nvp->nvp_datasize);

	memcpy(ptr, (const void *)(intptr_t)nvp->nvp_data, nvp->nvp_datasize);
	ptr += nvp->nvp_datasize;
	*leftp -= nvp->nvp_datasize;

	return (ptr);
}

unsigned char *
nvpair_pack_number_array(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NUMBER_ARRAY);
	PJDLOG_ASSERT(*leftp >= nvp->nvp_datasize);

	memcpy(ptr, (const void *)(intptr_t)nvp->nvp_data, nvp->nvp_datasize);
	ptr += nvp->nvp_datasize;
	*leftp -= nvp->nvp_datasize;

	return (ptr);
}

unsigned char *
nvpair_pack_string_array(const nvpair_t *nvp, unsigned char *ptr, size_t *leftp)
{
	unsigned int ii;
	size_t size, len;
	const char * const *array;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_STRING_ARRAY);
	PJDLOG_ASSERT(*leftp >= nvp->nvp_datasize);

	size = 0;
	array = nvpair_get_string_array(nvp, NULL);
	PJDLOG_ASSERT(array != NULL);

	for (ii = 0; ii < nvp->nvp_nitems; ii++) {
		len = strlen(array[ii]) + 1;
		PJDLOG_ASSERT(*leftp >= len);

		memcpy(ptr, (const void *)array[ii], len);
		size += len;
		ptr += len;
		*leftp -= len;
	}

	PJDLOG_ASSERT(size == nvp->nvp_datasize);

	return (ptr);
}

#ifndef _KERNEL
unsigned char *
nvpair_pack_descriptor_array(const nvpair_t *nvp, unsigned char *ptr,
    int64_t *fdidxp, size_t *leftp)
{
	int64_t value;
	const int *array;
	unsigned int ii;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR_ARRAY);
	PJDLOG_ASSERT(*leftp >= nvp->nvp_datasize);

	array = nvpair_get_descriptor_array(nvp, NULL);
	PJDLOG_ASSERT(array != NULL);

	for (ii = 0; ii < nvp->nvp_nitems; ii++) {
		PJDLOG_ASSERT(*leftp >= sizeof(value));

		value = array[ii];
		if (value != -1) {
			/*
			 * If there is a real descriptor here, we change its
			 * number to position in the array of descriptors send
			 * via control message.
			 */
			PJDLOG_ASSERT(fdidxp != NULL);

			value = *fdidxp;
			(*fdidxp)++;
		}
		memcpy(ptr, &value, sizeof(value));
		ptr += sizeof(value);
		*leftp -= sizeof(value);
	}

	return (ptr);
}
#endif

void
nvpair_init_datasize(nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);

	if (nvp->nvp_type == NV_TYPE_NVLIST) {
		if (nvp->nvp_data == 0) {
			nvp->nvp_datasize = 0;
		} else {
			nvp->nvp_datasize =
			    nvlist_size((const nvlist_t *)(intptr_t)nvp->nvp_data);
		}
	}
}

const unsigned char *
nvpair_unpack_header(bool isbe, nvpair_t *nvp, const unsigned char *ptr,
    size_t *leftp)
{
	struct nvpair_header nvphdr;

	if (*leftp < sizeof(nvphdr))
		goto fail;

	memcpy(&nvphdr, ptr, sizeof(nvphdr));
	ptr += sizeof(nvphdr);
	*leftp -= sizeof(nvphdr);

#if NV_TYPE_FIRST > 0
	if (nvphdr.nvph_type < NV_TYPE_FIRST)
		goto fail;
#endif
	if (nvphdr.nvph_type > NV_TYPE_LAST &&
	    nvphdr.nvph_type != NV_TYPE_NVLIST_UP &&
	    nvphdr.nvph_type != NV_TYPE_NVLIST_ARRAY_NEXT) {
		goto fail;
	}

#if BYTE_ORDER == BIG_ENDIAN
	if (!isbe) {
		nvphdr.nvph_namesize = le16toh(nvphdr.nvph_namesize);
		nvphdr.nvph_datasize = le64toh(nvphdr.nvph_datasize);
	}
#else
	if (isbe) {
		nvphdr.nvph_namesize = be16toh(nvphdr.nvph_namesize);
		nvphdr.nvph_datasize = be64toh(nvphdr.nvph_datasize);
	}
#endif

	if (nvphdr.nvph_namesize > NV_NAME_MAX)
		goto fail;
	if (*leftp < nvphdr.nvph_namesize)
		goto fail;
	if (nvphdr.nvph_namesize < 1)
		goto fail;
	if (strnlen((const char *)ptr, nvphdr.nvph_namesize) !=
	    (size_t)(nvphdr.nvph_namesize - 1)) {
		goto fail;
	}

	memcpy(nvp->nvp_name, ptr, nvphdr.nvph_namesize);
	ptr += nvphdr.nvph_namesize;
	*leftp -= nvphdr.nvph_namesize;

	if (*leftp < nvphdr.nvph_datasize)
		goto fail;

	nvp->nvp_type = nvphdr.nvph_type;
	nvp->nvp_data = 0;
	nvp->nvp_datasize = nvphdr.nvph_datasize;
	nvp->nvp_nitems = nvphdr.nvph_nitems;

	return (ptr);
fail:
	ERRNO_SET(EINVAL);
	return (NULL);
}

const unsigned char *
nvpair_unpack_null(bool isbe __unused, nvpair_t *nvp, const unsigned char *ptr,
    size_t *leftp __unused)
{

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NULL);

	if (nvp->nvp_datasize != 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	return (ptr);
}

const unsigned char *
nvpair_unpack_bool(bool isbe __unused, nvpair_t *nvp, const unsigned char *ptr,
    size_t *leftp)
{
	uint8_t value;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BOOL);

	if (nvp->nvp_datasize != sizeof(value)) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}
	if (*leftp < sizeof(value)) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	memcpy(&value, ptr, sizeof(value));
	ptr += sizeof(value);
	*leftp -= sizeof(value);

	if (value != 0 && value != 1) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp->nvp_data = (uint64_t)value;

	return (ptr);
}

const unsigned char *
nvpair_unpack_number(bool isbe, nvpair_t *nvp, const unsigned char *ptr,
     size_t *leftp)
{

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NUMBER);

	if (nvp->nvp_datasize != sizeof(uint64_t)) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}
	if (*leftp < sizeof(uint64_t)) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	if (isbe)
		nvp->nvp_data = be64dec(ptr);
	else
		nvp->nvp_data = le64dec(ptr);

	ptr += sizeof(uint64_t);
	*leftp -= sizeof(uint64_t);

	return (ptr);
}

const unsigned char *
nvpair_unpack_string(bool isbe __unused, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp)
{

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_STRING);

	if (*leftp < nvp->nvp_datasize || nvp->nvp_datasize == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	if (strnlen((const char *)ptr, nvp->nvp_datasize) !=
	    nvp->nvp_datasize - 1) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp->nvp_data = (uint64_t)(uintptr_t)nv_strdup((const char *)ptr);
	if (nvp->nvp_data == 0)
		return (NULL);

	ptr += nvp->nvp_datasize;
	*leftp -= nvp->nvp_datasize;

	return (ptr);
}

const unsigned char *
nvpair_unpack_nvlist(bool isbe __unused, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp, size_t nfds, nvlist_t **child)
{
	nvlist_t *value;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NVLIST);

	if (*leftp < nvp->nvp_datasize || nvp->nvp_datasize == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	value = nvlist_create(0);
	if (value == NULL)
		return (NULL);

	ptr = nvlist_unpack_header(value, ptr, nfds, NULL, leftp);
	if (ptr == NULL)
		return (NULL);

	nvp->nvp_data = (uint64_t)(uintptr_t)value;
	*child = value;

	return (ptr);
}

#ifndef _KERNEL
const unsigned char *
nvpair_unpack_descriptor(bool isbe, nvpair_t *nvp, const unsigned char *ptr,
    size_t *leftp, const int *fds, size_t nfds)
{
	int64_t idx;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR);

	if (nvp->nvp_datasize != sizeof(idx)) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}
	if (*leftp < sizeof(idx)) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	if (isbe)
		idx = be64dec(ptr);
	else
		idx = le64dec(ptr);

	if (idx < 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	if ((size_t)idx >= nfds) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp->nvp_data = (uint64_t)fds[idx];

	ptr += sizeof(idx);
	*leftp -= sizeof(idx);

	return (ptr);
}
#endif

const unsigned char *
nvpair_unpack_binary(bool isbe __unused, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp)
{
	void *value;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BINARY);

	if (*leftp < nvp->nvp_datasize || nvp->nvp_datasize == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	value = nv_malloc(nvp->nvp_datasize);
	if (value == NULL)
		return (NULL);

	memcpy(value, ptr, nvp->nvp_datasize);
	ptr += nvp->nvp_datasize;
	*leftp -= nvp->nvp_datasize;

	nvp->nvp_data = (uint64_t)(uintptr_t)value;

	return (ptr);
}

const unsigned char *
nvpair_unpack_bool_array(bool isbe __unused, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp)
{
	uint8_t *value;
	size_t size;
	unsigned int i;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BOOL_ARRAY);

	size = sizeof(*value) * nvp->nvp_nitems;
	if (nvp->nvp_datasize != size || *leftp < size ||
	    nvp->nvp_nitems == 0 || size < nvp->nvp_nitems) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	value = nv_malloc(size);
	if (value == NULL)
		return (NULL);

	for (i = 0; i < nvp->nvp_nitems; i++) {
		value[i] = *(const uint8_t *)ptr;

		ptr += sizeof(*value);
		*leftp -= sizeof(*value);
	}

	nvp->nvp_data = (uint64_t)(uintptr_t)value;

	return (ptr);
}

const unsigned char *
nvpair_unpack_number_array(bool isbe, nvpair_t *nvp, const unsigned char *ptr,
     size_t *leftp)
{
	uint64_t *value;
	size_t size;
	unsigned int i;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NUMBER_ARRAY);

	size = sizeof(*value) * nvp->nvp_nitems;
	if (nvp->nvp_datasize != size || *leftp < size ||
	    nvp->nvp_nitems == 0 || size < nvp->nvp_nitems) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	value = nv_malloc(size);
	if (value == NULL)
		return (NULL);

	for (i = 0; i < nvp->nvp_nitems; i++) {
		if (isbe)
			value[i] = be64dec(ptr);
		else
			value[i] = le64dec(ptr);

		ptr += sizeof(*value);
		*leftp -= sizeof(*value);
	}

	nvp->nvp_data = (uint64_t)(uintptr_t)value;

	return (ptr);
}

const unsigned char *
nvpair_unpack_string_array(bool isbe __unused, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp)
{
	ssize_t size;
	size_t len;
	const char *tmp;
	char **value;
	unsigned int ii, j;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_STRING_ARRAY);

	if (*leftp < nvp->nvp_datasize || nvp->nvp_datasize == 0 ||
	    nvp->nvp_nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	size = nvp->nvp_datasize;
	tmp = (const char *)ptr;
	for (ii = 0; ii < nvp->nvp_nitems; ii++) {
		len = strnlen(tmp, size - 1) + 1;
		size -= len;
		if (size < 0) {
			ERRNO_SET(EINVAL);
			return (NULL);
		}
		tmp += len;
	}
	if (size != 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	value = nv_malloc(sizeof(*value) * nvp->nvp_nitems);
	if (value == NULL)
		return (NULL);

	for (ii = 0; ii < nvp->nvp_nitems; ii++) {
		value[ii] = nv_strdup((const char *)ptr);
		if (value[ii] == NULL)
			goto out;
		len = strlen(value[ii]) + 1;
		ptr += len;
		*leftp -= len;
	}
	nvp->nvp_data = (uint64_t)(uintptr_t)value;

	return (ptr);
out:
	for (j = 0; j < ii; j++)
		nv_free(value[j]);
	nv_free(value);
	return (NULL);
}

#ifndef _KERNEL
const unsigned char *
nvpair_unpack_descriptor_array(bool isbe, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp, const int *fds, size_t nfds)
{
	int64_t idx;
	size_t size;
	unsigned int ii;
	int *array;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR_ARRAY);

	size = sizeof(idx) * nvp->nvp_nitems;
	if (nvp->nvp_datasize != size || *leftp < size ||
	    nvp->nvp_nitems == 0 || size < nvp->nvp_nitems) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	array = (int *)nv_malloc(size);
	if (array == NULL)
		return (NULL);

	for (ii = 0; ii < nvp->nvp_nitems; ii++) {
		if (isbe)
			idx = be64dec(ptr);
		else
			idx = le64dec(ptr);

		if (idx < 0) {
			ERRNO_SET(EINVAL);
			nv_free(array);
			return (NULL);
		}

		if ((size_t)idx >= nfds) {
			ERRNO_SET(EINVAL);
			nv_free(array);
			return (NULL);
		}

		array[ii] = (uint64_t)fds[idx];

		ptr += sizeof(idx);
		*leftp -= sizeof(idx);
	}

	nvp->nvp_data = (uint64_t)(uintptr_t)array;

	return (ptr);
}
#endif

const unsigned char *
nvpair_unpack_nvlist_array(bool isbe __unused, nvpair_t *nvp,
    const unsigned char *ptr, size_t *leftp, nvlist_t **firstel)
{
	nvlist_t **value;
	nvpair_t *tmpnvp;
	unsigned int ii, j;
	size_t sizeup;

	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NVLIST_ARRAY);

	sizeup = sizeof(struct nvpair_header) * nvp->nvp_nitems;
	if (nvp->nvp_nitems == 0 || sizeup < nvp->nvp_nitems ||
	    sizeup > *leftp) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	value = nv_malloc(nvp->nvp_nitems * sizeof(*value));
	if (value == NULL)
		return (NULL);

	for (ii = 0; ii < nvp->nvp_nitems; ii++) {
		value[ii] = nvlist_create(0);
		if (value[ii] == NULL)
			goto fail;
		if (ii > 0) {
			tmpnvp = nvpair_allocv(" ", NV_TYPE_NVLIST,
			    (uint64_t)(uintptr_t)value[ii], 0, 0);
			if (tmpnvp == NULL)
				goto fail;
			nvlist_set_array_next(value[ii - 1], tmpnvp);
		}
	}
	nvlist_set_flags(value[nvp->nvp_nitems - 1], NV_FLAG_IN_ARRAY);

	nvp->nvp_data = (uint64_t)(uintptr_t)value;
	*firstel = value[0];

	return (ptr);
fail:
	ERRNO_SAVE();
	for (j = 0; j <= ii; j++)
		nvlist_destroy(value[j]);
	nv_free(value);
	ERRNO_RESTORE();

	return (NULL);
}

const unsigned char *
nvpair_unpack(bool isbe, const unsigned char *ptr, size_t *leftp,
    nvpair_t **nvpp)
{
	nvpair_t *nvp, *tmp;

	nvp = nv_calloc(1, sizeof(*nvp) + NV_NAME_MAX);
	if (nvp == NULL)
		return (NULL);
	nvp->nvp_name = (char *)(nvp + 1);

	ptr = nvpair_unpack_header(isbe, nvp, ptr, leftp);
	if (ptr == NULL)
		goto fail;
	tmp = nv_realloc(nvp, sizeof(*nvp) + strlen(nvp->nvp_name) + 1);
	if (tmp == NULL)
		goto fail;
	nvp = tmp;

	/* Update nvp_name after realloc(). */
	nvp->nvp_name = (char *)(nvp + 1);
	nvp->nvp_data = 0x00;
	nvp->nvp_magic = NVPAIR_MAGIC;
	*nvpp = nvp;
	return (ptr);
fail:
	nv_free(nvp);
	return (NULL);
}

int
nvpair_type(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);

	return (nvp->nvp_type);
}

const char *
nvpair_name(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);

	return (nvp->nvp_name);
}

nvpair_t *
nvpair_create_stringf(const char *name, const char *valuefmt, ...)
{
	va_list valueap;
	nvpair_t *nvp;

	va_start(valueap, valuefmt);
	nvp = nvpair_create_stringv(name, valuefmt, valueap);
	va_end(valueap);

	return (nvp);
}

nvpair_t *
nvpair_create_stringv(const char *name, const char *valuefmt, va_list valueap)
{
	nvpair_t *nvp;
	char *str;
	int len;

	len = nv_vasprintf(&str, valuefmt, valueap);
	if (len < 0)
		return (NULL);
	nvp = nvpair_create_string(name, str);
	nv_free(str);
	return (nvp);
}

nvpair_t *
nvpair_create_null(const char *name)
{

	return (nvpair_allocv(name, NV_TYPE_NULL, 0, 0, 0));
}

nvpair_t *
nvpair_create_bool(const char *name, bool value)
{

	return (nvpair_allocv(name, NV_TYPE_BOOL, value ? 1 : 0,
	    sizeof(uint8_t), 0));
}

nvpair_t *
nvpair_create_number(const char *name, uint64_t value)
{

	return (nvpair_allocv(name, NV_TYPE_NUMBER, value, sizeof(value), 0));
}

nvpair_t *
nvpair_create_string(const char *name, const char *value)
{
	nvpair_t *nvp;
	size_t size;
	char *data;

	if (value == NULL) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	data = nv_strdup(value);
	if (data == NULL)
		return (NULL);
	size = strlen(value) + 1;

	nvp = nvpair_allocv(name, NV_TYPE_STRING, (uint64_t)(uintptr_t)data,
	    size, 0);
	if (nvp == NULL)
		nv_free(data);

	return (nvp);
}

nvpair_t *
nvpair_create_nvlist(const char *name, const nvlist_t *value)
{
	nvlist_t *nvl;
	nvpair_t *nvp;

	if (value == NULL) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvl = nvlist_clone(value);
	if (nvl == NULL)
		return (NULL);

	nvp = nvpair_allocv(name, NV_TYPE_NVLIST, (uint64_t)(uintptr_t)nvl, 0,
	    0);
	if (nvp == NULL)
		nvlist_destroy(nvl);
	else
		nvlist_set_parent(nvl, nvp);

	return (nvp);
}

#ifndef _KERNEL
nvpair_t *
nvpair_create_descriptor(const char *name, int value)
{
	nvpair_t *nvp;

	value = fcntl(value, F_DUPFD_CLOEXEC, 0);
	if (value < 0)
		return (NULL);

	nvp = nvpair_allocv(name, NV_TYPE_DESCRIPTOR, (uint64_t)value,
	    sizeof(int64_t), 0);
	if (nvp == NULL) {
		ERRNO_SAVE();
		close(value);
		ERRNO_RESTORE();
	}

	return (nvp);
}
#endif

nvpair_t *
nvpair_create_binary(const char *name, const void *value, size_t size)
{
	nvpair_t *nvp;
	void *data;

	if (value == NULL || size == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	data = nv_malloc(size);
	if (data == NULL)
		return (NULL);
	memcpy(data, value, size);

	nvp = nvpair_allocv(name, NV_TYPE_BINARY, (uint64_t)(uintptr_t)data,
	    size, 0);
	if (nvp == NULL)
		nv_free(data);

	return (nvp);
}

nvpair_t *
nvpair_create_bool_array(const char *name, const bool *value, size_t nitems)
{
	nvpair_t *nvp;
	size_t size;
	void *data;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	size = sizeof(value[0]) * nitems;
	data = nv_malloc(size);
	if (data == NULL)
		return (NULL);

	memcpy(data, value, size);
	nvp = nvpair_allocv(name, NV_TYPE_BOOL_ARRAY, (uint64_t)(uintptr_t)data,
	    size, nitems);
	if (nvp == NULL) {
		ERRNO_SAVE();
		nv_free(data);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_create_number_array(const char *name, const uint64_t *value,
    size_t nitems)
{
	nvpair_t *nvp;
	size_t size;
	void *data;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	size = sizeof(value[0]) * nitems;
	data = nv_malloc(size);
	if (data == NULL)
		return (NULL);

	memcpy(data, value, size);
	nvp = nvpair_allocv(name, NV_TYPE_NUMBER_ARRAY,
	    (uint64_t)(uintptr_t)data, size, nitems);
	if (nvp == NULL) {
		ERRNO_SAVE();
		nv_free(data);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_create_string_array(const char *name, const char * const *value,
    size_t nitems)
{
	nvpair_t *nvp;
	unsigned int ii;
	size_t datasize, size;
	char **data;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp = NULL;
	datasize = 0;
	data = nv_malloc(sizeof(value[0]) * nitems);
	if (data == NULL)
		return (NULL);

	for (ii = 0; ii < nitems; ii++) {
		if (value[ii] == NULL) {
			ERRNO_SET(EINVAL);
			goto fail;
		}

		size = strlen(value[ii]) + 1;
		datasize += size;
		data[ii] = nv_strdup(value[ii]);
		if (data[ii] == NULL)
			goto fail;
	}
	nvp = nvpair_allocv(name, NV_TYPE_STRING_ARRAY,
	    (uint64_t)(uintptr_t)data, datasize, nitems);

fail:
	if (nvp == NULL) {
		ERRNO_SAVE();
		for (; ii > 0; ii--)
			nv_free(data[ii - 1]);
		nv_free(data);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_create_nvlist_array(const char *name, const nvlist_t * const *value,
    size_t nitems)
{
	unsigned int ii;
	nvlist_t **nvls;
	nvpair_t *parent;
	int flags;

	nvls = NULL;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvls = nv_malloc(sizeof(value[0]) * nitems);
	if (nvls == NULL)
		return (NULL);

	for (ii = 0; ii < nitems; ii++) {
		if (value[ii] == NULL) {
			ERRNO_SET(EINVAL);
			goto fail;
		}

		nvls[ii] = nvlist_clone(value[ii]);
		if (nvls[ii] == NULL)
			goto fail;

		if (ii > 0) {
			nvpair_t *nvp;

			nvp = nvpair_allocv(" ", NV_TYPE_NVLIST,
			    (uint64_t)(uintptr_t)nvls[ii], 0, 0);
			if (nvp == NULL) {
				ERRNO_SAVE();
				nvlist_destroy(nvls[ii]);
				ERRNO_RESTORE();
				goto fail;
			}
			nvlist_set_array_next(nvls[ii - 1], nvp);
		}
	}
	flags = nvlist_flags(nvls[nitems - 1]) | NV_FLAG_IN_ARRAY;
	nvlist_set_flags(nvls[nitems - 1], flags);

	parent = nvpair_allocv(name, NV_TYPE_NVLIST_ARRAY,
	    (uint64_t)(uintptr_t)nvls, 0, nitems);
	if (parent == NULL)
		goto fail;

	for (ii = 0; ii < nitems; ii++)
		nvlist_set_parent(nvls[ii], parent);

	return (parent);

fail:
	ERRNO_SAVE();
	for (; ii > 0; ii--)
		nvlist_destroy(nvls[ii - 1]);
	nv_free(nvls);
	ERRNO_RESTORE();

	return (NULL);
}

#ifndef _KERNEL
nvpair_t *
nvpair_create_descriptor_array(const char *name, const int *value,
    size_t nitems)
{
	unsigned int ii;
	nvpair_t *nvp;
	int *fds;

	if (value == NULL) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp = NULL;

	fds = nv_malloc(sizeof(value[0]) * nitems);
	if (fds == NULL)
		return (NULL);
	for (ii = 0; ii < nitems; ii++) {
		if (value[ii] == -1) {
			fds[ii] = -1;
		} else {
			fds[ii] = fcntl(value[ii], F_DUPFD_CLOEXEC, 0);
			if (fds[ii] == -1)
				goto fail;
		}
	}

	nvp = nvpair_allocv(name, NV_TYPE_DESCRIPTOR_ARRAY,
	    (uint64_t)(uintptr_t)fds, sizeof(int64_t) * nitems, nitems);

fail:
	if (nvp == NULL) {
		ERRNO_SAVE();
		for (; ii > 0; ii--) {
			if (fds[ii - 1] != -1)
				close(fds[ii - 1]);
		}
		nv_free(fds);
		ERRNO_RESTORE();
	}

	return (nvp);
}
#endif

nvpair_t *
nvpair_move_string(const char *name, char *value)
{
	nvpair_t *nvp;

	if (value == NULL) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp = nvpair_allocv(name, NV_TYPE_STRING, (uint64_t)(uintptr_t)value,
	    strlen(value) + 1, 0);
	if (nvp == NULL) {
		ERRNO_SAVE();
		nv_free(value);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_move_nvlist(const char *name, nvlist_t *value)
{
	nvpair_t *nvp;

	if (value == NULL || nvlist_get_nvpair_parent(value) != NULL) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	if (nvlist_error(value) != 0) {
		ERRNO_SET(nvlist_error(value));
		nvlist_destroy(value);
		return (NULL);
	}

	nvp = nvpair_allocv(name, NV_TYPE_NVLIST, (uint64_t)(uintptr_t)value,
	    0, 0);
	if (nvp == NULL)
		nvlist_destroy(value);
	else
		nvlist_set_parent(value, nvp);

	return (nvp);
}

#ifndef _KERNEL
nvpair_t *
nvpair_move_descriptor(const char *name, int value)
{
	nvpair_t *nvp;

	if (value < 0 || !fd_is_valid(value)) {
		ERRNO_SET(EBADF);
		return (NULL);
	}

	nvp = nvpair_allocv(name, NV_TYPE_DESCRIPTOR, (uint64_t)value,
	    sizeof(int64_t), 0);
	if (nvp == NULL) {
		ERRNO_SAVE();
		close(value);
		ERRNO_RESTORE();
	}

	return (nvp);
}
#endif

nvpair_t *
nvpair_move_binary(const char *name, void *value, size_t size)
{
	nvpair_t *nvp;

	if (value == NULL || size == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp = nvpair_allocv(name, NV_TYPE_BINARY, (uint64_t)(uintptr_t)value,
	    size, 0);
	if (nvp == NULL) {
		ERRNO_SAVE();
		nv_free(value);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_move_bool_array(const char *name, bool *value, size_t nitems)
{
	nvpair_t *nvp;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp = nvpair_allocv(name, NV_TYPE_BOOL_ARRAY,
	    (uint64_t)(uintptr_t)value, sizeof(value[0]) * nitems, nitems);
	if (nvp == NULL) {
		ERRNO_SAVE();
		nv_free(value);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_move_string_array(const char *name, char **value, size_t nitems)
{
	nvpair_t *nvp;
	size_t i, size;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	size = 0;
	for (i = 0; i < nitems; i++) {
		if (value[i] == NULL) {
			ERRNO_SET(EINVAL);
			return (NULL);
		}

		size += strlen(value[i]) + 1;
	}

	nvp = nvpair_allocv(name, NV_TYPE_STRING_ARRAY,
	    (uint64_t)(uintptr_t)value, size, nitems);
	if (nvp == NULL) {
		ERRNO_SAVE();
		for (i = 0; i < nitems; i++)
			nv_free(value[i]);
		nv_free(value);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_move_number_array(const char *name, uint64_t *value, size_t nitems)
{
	nvpair_t *nvp;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	nvp = nvpair_allocv(name, NV_TYPE_NUMBER_ARRAY,
	    (uint64_t)(uintptr_t)value, sizeof(value[0]) * nitems, nitems);
	if (nvp == NULL) {
		ERRNO_SAVE();
		nv_free(value);
		ERRNO_RESTORE();
	}

	return (nvp);
}

nvpair_t *
nvpair_move_nvlist_array(const char *name, nvlist_t **value, size_t nitems)
{
	nvpair_t *parent;
	unsigned int ii;
	int flags;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	for (ii = 0; ii < nitems; ii++) {
		if (value == NULL || nvlist_error(value[ii]) != 0 ||
		    nvlist_get_pararr(value[ii], NULL) != NULL) {
			ERRNO_SET(EINVAL);
			goto fail;
		}
		if (ii > 0) {
			nvpair_t *nvp;

			nvp = nvpair_allocv(" ", NV_TYPE_NVLIST,
			    (uint64_t)(uintptr_t)value[ii], 0, 0);
			if (nvp == NULL)
				goto fail;
			nvlist_set_array_next(value[ii - 1], nvp);
		}
	}
	flags = nvlist_flags(value[nitems - 1]) | NV_FLAG_IN_ARRAY;
	nvlist_set_flags(value[nitems - 1], flags);

	parent = nvpair_allocv(name, NV_TYPE_NVLIST_ARRAY,
	    (uint64_t)(uintptr_t)value, 0, nitems);
	if (parent == NULL)
		goto fail;

	for (ii = 0; ii < nitems; ii++)
		nvlist_set_parent(value[ii], parent);

	return (parent);
fail:
	ERRNO_SAVE();
	for (ii = 0; ii < nitems; ii++) {
		if (value[ii] != NULL &&
		    nvlist_get_pararr(value[ii], NULL) != NULL) {
			nvlist_destroy(value[ii]);
		}
	}
	nv_free(value);
	ERRNO_RESTORE();

	return (NULL);
}

#ifndef _KERNEL
nvpair_t *
nvpair_move_descriptor_array(const char *name, int *value, size_t nitems)
{
	nvpair_t *nvp;
	size_t i;

	if (value == NULL || nitems == 0) {
		ERRNO_SET(EINVAL);
		return (NULL);
	}

	for (i = 0; i < nitems; i++) {
		if (value[i] != -1 && !fd_is_valid(value[i])) {
			ERRNO_SET(EBADF);
			goto fail;
		}
	}

	nvp = nvpair_allocv(name, NV_TYPE_DESCRIPTOR_ARRAY,
	    (uint64_t)(uintptr_t)value, sizeof(value[0]) * nitems, nitems);
	if (nvp == NULL)
		goto fail;

	return (nvp);
fail:
	ERRNO_SAVE();
	for (i = 0; i < nitems; i++) {
		if (fd_is_valid(value[i]))
			close(value[i]);
	}
	nv_free(value);
	ERRNO_RESTORE();

	return (NULL);
}
#endif

bool
nvpair_get_bool(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);

	return (nvp->nvp_data == 1);
}

uint64_t
nvpair_get_number(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);

	return (nvp->nvp_data);
}

const char *
nvpair_get_string(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_STRING);

	return ((const char *)(intptr_t)nvp->nvp_data);
}

const nvlist_t *
nvpair_get_nvlist(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NVLIST);

	return ((const nvlist_t *)(intptr_t)nvp->nvp_data);
}

#ifndef _KERNEL
int
nvpair_get_descriptor(const nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR);

	return ((int)nvp->nvp_data);
}
#endif

const void *
nvpair_get_binary(const nvpair_t *nvp, size_t *sizep)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BINARY);

	if (sizep != NULL)
		*sizep = nvp->nvp_datasize;

	return ((const void *)(intptr_t)nvp->nvp_data);
}

const bool *
nvpair_get_bool_array(const nvpair_t *nvp, size_t *nitems)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BOOL_ARRAY);

	if (nitems != NULL)
		*nitems = nvp->nvp_nitems;

	return ((const bool *)(intptr_t)nvp->nvp_data);
}

const uint64_t *
nvpair_get_number_array(const nvpair_t *nvp, size_t *nitems)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NUMBER_ARRAY);

	if (nitems != NULL)
		*nitems = nvp->nvp_nitems;

	return ((const uint64_t *)(intptr_t)nvp->nvp_data);
}

const char * const *
nvpair_get_string_array(const nvpair_t *nvp, size_t *nitems)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_STRING_ARRAY);

	if (nitems != NULL)
		*nitems = nvp->nvp_nitems;

	return ((const char * const *)(intptr_t)nvp->nvp_data);
}

const nvlist_t * const *
nvpair_get_nvlist_array(const nvpair_t *nvp, size_t *nitems)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NVLIST_ARRAY);

	if (nitems != NULL)
		*nitems = nvp->nvp_nitems;

	return ((const nvlist_t * const *)((intptr_t)nvp->nvp_data));
}

#ifndef _KERNEL
const int *
nvpair_get_descriptor_array(const nvpair_t *nvp, size_t *nitems)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR_ARRAY);

	if (nitems != NULL)
		*nitems = nvp->nvp_nitems;

	return ((const int *)(intptr_t)nvp->nvp_data);
}
#endif

int
nvpair_append_bool_array(nvpair_t *nvp, const bool value)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_BOOL_ARRAY);
	return (nvpair_append(nvp, &value, sizeof(value), sizeof(value)));
}

int
nvpair_append_number_array(nvpair_t *nvp, const uint64_t value)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NUMBER_ARRAY);
	return (nvpair_append(nvp, &value, sizeof(value), sizeof(value)));
}

int
nvpair_append_string_array(nvpair_t *nvp, const char *value)
{
	char *str;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_STRING_ARRAY);
	if (value == NULL) {
		ERRNO_SET(EINVAL);
		return (-1);
	}
	str = nv_strdup(value);
	if (str == NULL) {
		return (-1);
	}
	if (nvpair_append(nvp, &str, sizeof(str), strlen(str) + 1) == -1) {
		nv_free(str);
		return (-1);
	}
	return (0);
}

int
nvpair_append_nvlist_array(nvpair_t *nvp, const nvlist_t *value)
{
	nvpair_t *tmpnvp;
	nvlist_t *nvl, *prev;
	int flags;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_NVLIST_ARRAY);
	if (value == NULL || nvlist_error(value) != 0 ||
	    nvlist_get_pararr(value, NULL) != NULL) {
		ERRNO_SET(EINVAL);
		return (-1);
	}
	nvl = nvlist_clone(value);
	if (nvl == NULL) {
		return (-1);
	}
	flags = nvlist_flags(nvl) | NV_FLAG_IN_ARRAY;
	nvlist_set_flags(nvl, flags);

	tmpnvp = NULL;
	prev = NULL;
	if (nvp->nvp_nitems > 0) {
		nvlist_t **nvls = (void *)(uintptr_t)nvp->nvp_data;

		prev = nvls[nvp->nvp_nitems - 1];
		PJDLOG_ASSERT(prev != NULL);

		tmpnvp = nvpair_allocv(" ", NV_TYPE_NVLIST,
		    (uint64_t)(uintptr_t)nvl, 0, 0);
		if (tmpnvp == NULL) {
			goto fail;
		}
	}
	if (nvpair_append(nvp, &nvl, sizeof(nvl), 0) == -1) {
		goto fail;
	}
	if (tmpnvp) {
		NVPAIR_ASSERT(tmpnvp);
		nvlist_set_array_next(prev, tmpnvp);
	}
	nvlist_set_parent(nvl, nvp);
	return (0);
fail:
	if (tmpnvp) {
		nvpair_free(tmpnvp);
	}
	nvlist_destroy(nvl);
	return (-1);
}

#ifndef _KERNEL
int
nvpair_append_descriptor_array(nvpair_t *nvp, const int value)
{
	int fd;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_type == NV_TYPE_DESCRIPTOR_ARRAY);
	fd = fcntl(value, F_DUPFD_CLOEXEC, 0);
	if (fd == -1) {
		return (-1);
	}
	if (nvpair_append(nvp, &fd, sizeof(fd), sizeof(fd)) == -1) {
		close(fd);
		return (-1);
	}
	return (0);
}
#endif

void
nvpair_free(nvpair_t *nvp)
{
	size_t i;

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_list == NULL);

	nvp->nvp_magic = 0;
	switch (nvp->nvp_type) {
#ifndef _KERNEL
	case NV_TYPE_DESCRIPTOR:
		close((int)nvp->nvp_data);
		break;
	case NV_TYPE_DESCRIPTOR_ARRAY:
		for (i = 0; i < nvp->nvp_nitems; i++)
			close(((int *)(intptr_t)nvp->nvp_data)[i]);
		nv_free((int *)(intptr_t)nvp->nvp_data);
		break;
#endif
	case NV_TYPE_NVLIST:
		nvlist_destroy((nvlist_t *)(intptr_t)nvp->nvp_data);
		break;
	case NV_TYPE_STRING:
		nv_free((char *)(intptr_t)nvp->nvp_data);
		break;
	case NV_TYPE_BINARY:
		nv_free((void *)(intptr_t)nvp->nvp_data);
		break;
	case NV_TYPE_NVLIST_ARRAY:
		for (i = 0; i < nvp->nvp_nitems; i++) {
			nvlist_destroy(
			    ((nvlist_t **)(intptr_t)nvp->nvp_data)[i]);
		}
		nv_free(((nvlist_t **)(intptr_t)nvp->nvp_data));
		break;
	case NV_TYPE_NUMBER_ARRAY:
		nv_free((uint64_t *)(intptr_t)nvp->nvp_data);
		break;
	case NV_TYPE_BOOL_ARRAY:
		nv_free((bool *)(intptr_t)nvp->nvp_data);
		break;
	case NV_TYPE_STRING_ARRAY:
		for (i = 0; i < nvp->nvp_nitems; i++)
			nv_free(((char **)(intptr_t)nvp->nvp_data)[i]);
		nv_free((char **)(intptr_t)nvp->nvp_data);
		break;
	}
	nv_free(nvp);
}

void
nvpair_free_structure(nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvp->nvp_list == NULL);

	nvp->nvp_magic = 0;
	nv_free(nvp);
}

const char *
nvpair_type_string(int type)
{

	switch (type) {
	case NV_TYPE_NULL:
		return ("NULL");
	case NV_TYPE_BOOL:
		return ("BOOL");
	case NV_TYPE_NUMBER:
		return ("NUMBER");
	case NV_TYPE_STRING:
		return ("STRING");
	case NV_TYPE_NVLIST:
		return ("NVLIST");
	case NV_TYPE_DESCRIPTOR:
		return ("DESCRIPTOR");
	case NV_TYPE_BINARY:
		return ("BINARY");
	case NV_TYPE_BOOL_ARRAY:
		return ("BOOL ARRAY");
	case NV_TYPE_NUMBER_ARRAY:
		return ("NUMBER ARRAY");
	case NV_TYPE_STRING_ARRAY:
		return ("STRING ARRAY");
	case NV_TYPE_NVLIST_ARRAY:
		return ("NVLIST ARRAY");
	case NV_TYPE_DESCRIPTOR_ARRAY:
		return ("DESCRIPTOR ARRAY");
	default:
		return ("<UNKNOWN>");
	}
}

