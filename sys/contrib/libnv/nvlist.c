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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/stdarg.h>

#else
#include <sys/socket.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msgio.h"
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
#define	PJDLOG_ABORT(...)		do {				\
	fprintf(stderr, "%s:%u: ", __FILE__, __LINE__);			\
	fprintf(stderr, __VA_ARGS__);					\
	fprintf(stderr, "\n");						\
	abort();							\
} while (0)
#endif
#endif

#define	NV_FLAG_PRIVATE_MASK	(NV_FLAG_BIG_ENDIAN | NV_FLAG_IN_ARRAY)
#define	NV_FLAG_PUBLIC_MASK	(NV_FLAG_IGNORE_CASE | NV_FLAG_NO_UNIQUE)
#define	NV_FLAG_ALL_MASK	(NV_FLAG_PRIVATE_MASK | NV_FLAG_PUBLIC_MASK)

#define	NVLIST_MAGIC	0x6e766c	/* "nvl" */
struct nvlist {
	int		 nvl_magic;
	int		 nvl_error;
	int		 nvl_flags;
	nvpair_t	*nvl_parent;
	nvpair_t	*nvl_array_next;
	struct nvl_head	 nvl_head;
};

#define	NVLIST_ASSERT(nvl)	do {					\
	PJDLOG_ASSERT((nvl) != NULL);					\
	PJDLOG_ASSERT((nvl)->nvl_magic == NVLIST_MAGIC);		\
} while (0)

#ifdef _KERNEL
MALLOC_DEFINE(M_NVLIST, "nvlist", "kernel nvlist");
#endif

#define	NVPAIR_ASSERT(nvp)	nvpair_assert(nvp)

#define	NVLIST_HEADER_MAGIC	0x6c
#define	NVLIST_HEADER_VERSION	0x00
struct nvlist_header {
	uint8_t		nvlh_magic;
	uint8_t		nvlh_version;
	uint8_t		nvlh_flags;
	uint64_t	nvlh_descriptors;
	uint64_t	nvlh_size;
} __packed;

nvlist_t *
nvlist_create(int flags)
{
	nvlist_t *nvl;

	PJDLOG_ASSERT((flags & ~(NV_FLAG_PUBLIC_MASK)) == 0);

	nvl = nv_malloc(sizeof(*nvl));
	if (nvl == NULL)
		return (NULL);
	nvl->nvl_error = 0;
	nvl->nvl_flags = flags;
	nvl->nvl_parent = NULL;
	nvl->nvl_array_next = NULL;
	TAILQ_INIT(&nvl->nvl_head);
	nvl->nvl_magic = NVLIST_MAGIC;

	return (nvl);
}

void
nvlist_destroy(nvlist_t *nvl)
{
	nvpair_t *nvp;

	if (nvl == NULL)
		return;

	ERRNO_SAVE();

	NVLIST_ASSERT(nvl);

	while ((nvp = nvlist_first_nvpair(nvl)) != NULL) {
		nvlist_remove_nvpair(nvl, nvp);
		nvpair_free(nvp);
	}
	if (nvl->nvl_array_next != NULL)
		nvpair_free_structure(nvl->nvl_array_next);
	nvl->nvl_array_next = NULL;
	nvl->nvl_parent = NULL;
	nvl->nvl_magic = 0;
	nv_free(nvl);

	ERRNO_RESTORE();
}

void
nvlist_set_error(nvlist_t *nvl, int error)
{

	PJDLOG_ASSERT(error != 0);

	/*
	 * Check for error != 0 so that we don't do the wrong thing if somebody
	 * tries to abuse this API when asserts are disabled.
	 */
	if (nvl != NULL && error != 0 && nvl->nvl_error == 0)
		nvl->nvl_error = error;
}

int
nvlist_error(const nvlist_t *nvl)
{

	if (nvl == NULL)
		return (ENOMEM);

	NVLIST_ASSERT(nvl);

	return (nvl->nvl_error);
}

nvpair_t *
nvlist_get_nvpair_parent(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return (nvl->nvl_parent);
}

const nvlist_t *
nvlist_get_parent(const nvlist_t *nvl, void **cookiep)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);

	nvp = nvl->nvl_parent;
	if (cookiep != NULL)
		*cookiep = nvp;
	if (nvp == NULL)
		return (NULL);

	return (nvpair_nvlist(nvp));
}

void
nvlist_set_parent(nvlist_t *nvl, nvpair_t *parent)
{

	NVLIST_ASSERT(nvl);

	nvl->nvl_parent = parent;
}

void
nvlist_set_array_next(nvlist_t *nvl, nvpair_t *ele)
{

	NVLIST_ASSERT(nvl);

	if (ele != NULL) {
		nvl->nvl_flags |= NV_FLAG_IN_ARRAY;
	} else {
		nvl->nvl_flags &= ~NV_FLAG_IN_ARRAY;
		nv_free(nvl->nvl_array_next);
	}

	nvl->nvl_array_next = ele;
}

nvpair_t *
nvlist_get_array_next_nvpair(nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return (nvl->nvl_array_next);
}

bool
nvlist_in_array(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return ((nvl->nvl_flags & NV_FLAG_IN_ARRAY) != 0);
}

const nvlist_t *
nvlist_get_array_next(const nvlist_t *nvl)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);

	nvp = nvl->nvl_array_next;
	if (nvp == NULL)
		return (NULL);

	return (nvpair_get_nvlist(nvp));
}

const nvlist_t *
nvlist_get_pararr(const nvlist_t *nvl, void **cookiep)
{
	const nvlist_t *ret;

	ret = nvlist_get_array_next(nvl);
	if (ret != NULL) {
		if (cookiep != NULL)
			*cookiep = NULL;
		return (ret);
	}

	return (nvlist_get_parent(nvl, cookiep));
}

bool
nvlist_empty(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	return (nvlist_first_nvpair(nvl) == NULL);
}

int
nvlist_flags(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	return (nvl->nvl_flags & NV_FLAG_PUBLIC_MASK);
}

void
nvlist_set_flags(nvlist_t *nvl, int flags)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	nvl->nvl_flags = flags;
}

void
nvlist_report_missing(int type, const char *name)
{

	PJDLOG_ABORT("Element '%s' of type %s doesn't exist.",
	    name, nvpair_type_string(type));
}

static nvpair_t *
nvlist_find(const nvlist_t *nvl, int type, const char *name)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		if (type != NV_TYPE_NONE && nvpair_type(nvp) != type)
			continue;
		if ((nvl->nvl_flags & NV_FLAG_IGNORE_CASE) != 0) {
			if (strcasecmp(nvpair_name(nvp), name) != 0)
				continue;
		} else {
			if (strcmp(nvpair_name(nvp), name) != 0)
				continue;
		}
		break;
	}

	if (nvp == NULL)
		ERRNO_SET(ENOENT);

	return (nvp);
}

bool
nvlist_exists_type(const nvlist_t *nvl, const char *name, int type)
{

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	return (nvlist_find(nvl, type, name) != NULL);
}

void
nvlist_free_type(nvlist_t *nvl, const char *name, int type)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);
	PJDLOG_ASSERT(type == NV_TYPE_NONE ||
	    (type >= NV_TYPE_FIRST && type <= NV_TYPE_LAST));

	nvp = nvlist_find(nvl, type, name);
	if (nvp != NULL)
		nvlist_free_nvpair(nvl, nvp);
	else
		nvlist_report_missing(type, name);
}

nvlist_t *
nvlist_clone(const nvlist_t *nvl)
{
	nvlist_t *newnvl;
	nvpair_t *nvp, *newnvp;

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		ERRNO_SET(nvl->nvl_error);
		return (NULL);
	}

	newnvl = nvlist_create(nvl->nvl_flags & NV_FLAG_PUBLIC_MASK);
	for (nvp = nvlist_first_nvpair(nvl); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		newnvp = nvpair_clone(nvp);
		if (newnvp == NULL)
			break;
		(void)nvlist_move_nvpair(newnvl, newnvp);
	}
	if (nvp != NULL) {
		nvlist_destroy(newnvl);
		return (NULL);
	}
	return (newnvl);
}

#ifndef _KERNEL
static bool
nvlist_dump_error_check(const nvlist_t *nvl, int fd, int level)
{

	if (nvlist_error(nvl) != 0) {
		dprintf(fd, "%*serror: %d\n", level * 4, "",
		    nvlist_error(nvl));
		return (true);
	}

	return (false);
}

/*
 * Dump content of nvlist.
 */
void
nvlist_dump(const nvlist_t *nvl, int fd)
{
	const nvlist_t *tmpnvl;
	nvpair_t *nvp, *tmpnvp;
	void *cookie;
	int level;

	level = 0;
	if (nvlist_dump_error_check(nvl, fd, level))
		return;

	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		dprintf(fd, "%*s%s (%s):", level * 4, "", nvpair_name(nvp),
		    nvpair_type_string(nvpair_type(nvp)));
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			dprintf(fd, " null\n");
			break;
		case NV_TYPE_BOOL:
			dprintf(fd, " %s\n", nvpair_get_bool(nvp) ?
			    "TRUE" : "FALSE");
			break;
		case NV_TYPE_NUMBER:
			dprintf(fd, " %ju (%jd) (0x%jx)\n",
			    (uintmax_t)nvpair_get_number(nvp),
			    (intmax_t)nvpair_get_number(nvp),
			    (uintmax_t)nvpair_get_number(nvp));
			break;
		case NV_TYPE_STRING:
			dprintf(fd, " [%s]\n", nvpair_get_string(nvp));
			break;
		case NV_TYPE_NVLIST:
			dprintf(fd, "\n");
			tmpnvl = nvpair_get_nvlist(nvp);
			if (nvlist_dump_error_check(tmpnvl, fd, level + 1))
				break;
			tmpnvp = nvlist_first_nvpair(tmpnvl);
			if (tmpnvp != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				level++;
				continue;
			}
			break;
		case NV_TYPE_DESCRIPTOR:
			dprintf(fd, " %d\n", nvpair_get_descriptor(nvp));
			break;
		case NV_TYPE_BINARY:
		    {
			const unsigned char *binary;
			unsigned int ii;
			size_t size;

			binary = nvpair_get_binary(nvp, &size);
			dprintf(fd, " %zu ", size);
			for (ii = 0; ii < size; ii++)
				dprintf(fd, "%02hhx", binary[ii]);
			dprintf(fd, "\n");
			break;
		    }
		case NV_TYPE_BOOL_ARRAY:
		    {
			const bool *value;
			unsigned int ii;
			size_t nitems;

			value = nvpair_get_bool_array(nvp, &nitems);
			dprintf(fd, " [ ");
			for (ii = 0; ii < nitems; ii++) {
				dprintf(fd, "%s", value[ii] ? "TRUE" : "FALSE");
				if (ii != nitems - 1)
					dprintf(fd, ", ");
			}
			dprintf(fd, " ]\n");
			break;
		    }
		case NV_TYPE_STRING_ARRAY:
		    {
			const char * const *value;
			unsigned int ii;
			size_t nitems;

			value = nvpair_get_string_array(nvp, &nitems);
			dprintf(fd, " [ ");
			for (ii = 0; ii < nitems; ii++) {
				if (value[ii] == NULL)
					dprintf(fd, "NULL");
				else
					dprintf(fd, "\"%s\"", value[ii]);
				if (ii != nitems - 1)
					dprintf(fd, ", ");
			}
			dprintf(fd, " ]\n");
			break;
		    }
		case NV_TYPE_NUMBER_ARRAY:
		    {
			const uint64_t *value;
			unsigned int ii;
			size_t nitems;

			value = nvpair_get_number_array(nvp, &nitems);
			dprintf(fd, " [ ");
			for (ii = 0; ii < nitems; ii++) {
				dprintf(fd, "%ju (%jd) (0x%jx)",
				    value[ii], value[ii], value[ii]);
				if (ii != nitems - 1)
					dprintf(fd, ", ");
			}
			dprintf(fd, " ]\n");
			break;
		    }
		case NV_TYPE_DESCRIPTOR_ARRAY:
		    {
			const int *value;
			unsigned int ii;
			size_t nitems;

			value = nvpair_get_descriptor_array(nvp, &nitems);
			dprintf(fd, " [ ");
			for (ii = 0; ii < nitems; ii++) {
				dprintf(fd, "%d", value[ii]);
				if (ii != nitems - 1)
					dprintf(fd, ", ");
			}
			dprintf(fd, " ]\n");
			break;
		    }
		case NV_TYPE_NVLIST_ARRAY:
		    {
			const nvlist_t * const *value;
			unsigned int ii;
			size_t nitems;

			value = nvpair_get_nvlist_array(nvp, &nitems);
			dprintf(fd, " %zu\n", nitems);
			tmpnvl = NULL;
			tmpnvp = NULL;
			for (ii = 0; ii < nitems; ii++) {
				if (nvlist_dump_error_check(value[ii], fd,
				    level + 1)) {
					break;
				}

				if (tmpnvl == NULL) {
					tmpnvp = nvlist_first_nvpair(value[ii]);
					if (tmpnvp != NULL) {
						tmpnvl = value[ii];
					} else {
						dprintf(fd, "%*s,\n",
						    (level + 1) * 4, "");
					}
				}
			}
			if (tmpnvp != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				level++;
				continue;
			}
			break;
		    }
		default:
			PJDLOG_ABORT("Unknown type: %d.", nvpair_type(nvp));
		}

		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			do {
				cookie = NULL;
				if (nvlist_in_array(nvl))
					dprintf(fd, "%*s,\n", level * 4, "");
				nvl = nvlist_get_pararr(nvl, &cookie);
				if (nvl == NULL)
					return;
				if (nvlist_in_array(nvl) && cookie == NULL) {
					nvp = nvlist_first_nvpair(nvl);
				} else {
					nvp = cookie;
					level--;
				}
			} while (nvp == NULL);
			if (nvlist_in_array(nvl) && cookie == NULL)
				break;
		}
	}
}

void
nvlist_fdump(const nvlist_t *nvl, FILE *fp)
{

	fflush(fp);
	nvlist_dump(nvl, fileno(fp));
}
#endif

/*
 * The function obtains size of the nvlist after nvlist_pack().
 */
size_t
nvlist_size(const nvlist_t *nvl)
{
	const nvlist_t *tmpnvl;
	const nvlist_t * const *nvlarray;
	const nvpair_t *nvp, *tmpnvp;
	void *cookie;
	size_t size, nitems;
	unsigned int ii;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	size = sizeof(struct nvlist_header);
	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		size += nvpair_header_size();
		size += strlen(nvpair_name(nvp)) + 1;
		if (nvpair_type(nvp) == NV_TYPE_NVLIST) {
			size += sizeof(struct nvlist_header);
			size += nvpair_header_size() + 1;
			tmpnvl = nvpair_get_nvlist(nvp);
			PJDLOG_ASSERT(tmpnvl->nvl_error == 0);
			tmpnvp = nvlist_first_nvpair(tmpnvl);
			if (tmpnvp != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				continue;
			}
		} else if (nvpair_type(nvp) == NV_TYPE_NVLIST_ARRAY) {
			nvlarray = nvpair_get_nvlist_array(nvp, &nitems);
			PJDLOG_ASSERT(nitems > 0);

			size += (nvpair_header_size() + 1) * nitems;
			size += sizeof(struct nvlist_header) * nitems;

			tmpnvl = NULL;
			tmpnvp = NULL;
			for (ii = 0; ii < nitems; ii++) {
				PJDLOG_ASSERT(nvlarray[ii]->nvl_error == 0);
				tmpnvp = nvlist_first_nvpair(nvlarray[ii]);
				if (tmpnvp != NULL) {
					tmpnvl = nvlarray[ii];
					break;
				}
			}
			if (tmpnvp != NULL) {
				nvp = tmpnvp;
				nvl = tmpnvl;
				continue;
			}

		} else {
			size += nvpair_size(nvp);
		}

		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			do {
				cookie = NULL;
				nvl = nvlist_get_pararr(nvl, &cookie);
				if (nvl == NULL)
					goto out;
				if (nvlist_in_array(nvl) && cookie == NULL) {
					nvp = nvlist_first_nvpair(nvl);
				} else {
					nvp = cookie;
				}
			} while (nvp == NULL);
			if (nvlist_in_array(nvl) && cookie == NULL)
				break;
		}
	}

out:
	return (size);
}

#ifndef _KERNEL
static int *
nvlist_xdescriptors(const nvlist_t *nvl, int *descs)
{
	void *cookie;
	nvpair_t *nvp;
	int type;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	cookie = NULL;
	do {
		while (nvlist_next(nvl, &type, &cookie) != NULL) {
			nvp = cookie;
			switch (type) {
			case NV_TYPE_DESCRIPTOR:
				*descs = nvpair_get_descriptor(nvp);
				descs++;
				break;
			case NV_TYPE_DESCRIPTOR_ARRAY:
			    {
				const int *value;
				size_t nitems;
				unsigned int ii;

				value = nvpair_get_descriptor_array(nvp,
				    &nitems);
				for (ii = 0; ii < nitems; ii++) {
					*descs = value[ii];
					descs++;
				}
				break;
			    }
			case NV_TYPE_NVLIST:
				nvl = nvpair_get_nvlist(nvp);
				cookie = NULL;
				break;
			case NV_TYPE_NVLIST_ARRAY:
			    {
				const nvlist_t * const *value;
				size_t nitems;

				value = nvpair_get_nvlist_array(nvp, &nitems);
				PJDLOG_ASSERT(value != NULL);
				PJDLOG_ASSERT(nitems > 0);

				nvl = value[0];
				cookie = NULL;
				break;
			    }
			}
		}
	} while ((nvl = nvlist_get_pararr(nvl, &cookie)) != NULL);

	return (descs);
}
#endif

#ifndef _KERNEL
int *
nvlist_descriptors(const nvlist_t *nvl, size_t *nitemsp)
{
	size_t nitems;
	int *fds;

	nitems = nvlist_ndescriptors(nvl);
	fds = nv_malloc(sizeof(fds[0]) * (nitems + 1));
	if (fds == NULL)
		return (NULL);
	if (nitems > 0)
		nvlist_xdescriptors(nvl, fds);
	fds[nitems] = -1;
	if (nitemsp != NULL)
		*nitemsp = nitems;
	return (fds);
}
#endif

size_t
nvlist_ndescriptors(const nvlist_t *nvl)
{
#ifndef _KERNEL
	void *cookie;
	nvpair_t *nvp;
	size_t ndescs;
	int type;

	NVLIST_ASSERT(nvl);
	PJDLOG_ASSERT(nvl->nvl_error == 0);

	ndescs = 0;
	cookie = NULL;
	do {
		while (nvlist_next(nvl, &type, &cookie) != NULL) {
			nvp = cookie;
			switch (type) {
			case NV_TYPE_DESCRIPTOR:
				ndescs++;
				break;
			case NV_TYPE_NVLIST:
				nvl = nvpair_get_nvlist(nvp);
				cookie = NULL;
				break;
			case NV_TYPE_NVLIST_ARRAY:
			    {
				const nvlist_t * const *value;
				size_t nitems;

				value = nvpair_get_nvlist_array(nvp, &nitems);
				PJDLOG_ASSERT(value != NULL);
				PJDLOG_ASSERT(nitems > 0);

				nvl = value[0];
				cookie = NULL;
				break;
			    }
			case NV_TYPE_DESCRIPTOR_ARRAY:
			    {
				size_t nitems;

				(void)nvpair_get_descriptor_array(nvp,
				    &nitems);
				ndescs += nitems;
				break;
			    }
			}
		}
	} while ((nvl = nvlist_get_pararr(nvl, &cookie)) != NULL);

	return (ndescs);
#else
	return (0);
#endif
}

static unsigned char *
nvlist_pack_header(const nvlist_t *nvl, unsigned char *ptr, size_t *leftp)
{
	struct nvlist_header nvlhdr;

	NVLIST_ASSERT(nvl);

	nvlhdr.nvlh_magic = NVLIST_HEADER_MAGIC;
	nvlhdr.nvlh_version = NVLIST_HEADER_VERSION;
	nvlhdr.nvlh_flags = nvl->nvl_flags;
#if BYTE_ORDER == BIG_ENDIAN
	nvlhdr.nvlh_flags |= NV_FLAG_BIG_ENDIAN;
#endif
	nvlhdr.nvlh_descriptors = nvlist_ndescriptors(nvl);
	nvlhdr.nvlh_size = *leftp - sizeof(nvlhdr);
	PJDLOG_ASSERT(*leftp >= sizeof(nvlhdr));
	memcpy(ptr, &nvlhdr, sizeof(nvlhdr));
	ptr += sizeof(nvlhdr);
	*leftp -= sizeof(nvlhdr);

	return (ptr);
}

static void *
nvlist_xpack(const nvlist_t *nvl, int64_t *fdidxp, size_t *sizep)
{
	unsigned char *buf, *ptr;
	size_t left, size;
	const nvlist_t *tmpnvl;
	nvpair_t *nvp, *tmpnvp;
	void *cookie;

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		ERRNO_SET(nvl->nvl_error);
		return (NULL);
	}

	size = nvlist_size(nvl);
	buf = nv_malloc(size);
	if (buf == NULL)
		return (NULL);

	ptr = buf;
	left = size;

	ptr = nvlist_pack_header(nvl, ptr, &left);

	nvp = nvlist_first_nvpair(nvl);
	while (nvp != NULL) {
		NVPAIR_ASSERT(nvp);

		nvpair_init_datasize(nvp);
		ptr = nvpair_pack_header(nvp, ptr, &left);
		if (ptr == NULL)
			goto fail;
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			ptr = nvpair_pack_null(nvp, ptr, &left);
			break;
		case NV_TYPE_BOOL:
			ptr = nvpair_pack_bool(nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER:
			ptr = nvpair_pack_number(nvp, ptr, &left);
			break;
		case NV_TYPE_STRING:
			ptr = nvpair_pack_string(nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST:
			tmpnvl = nvpair_get_nvlist(nvp);
			ptr = nvlist_pack_header(tmpnvl, ptr, &left);
			if (ptr == NULL)
				goto fail;
			tmpnvp = nvlist_first_nvpair(tmpnvl);
			if (tmpnvp != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				continue;
			}
			ptr = nvpair_pack_nvlist_up(ptr, &left);
			break;
#ifndef _KERNEL
		case NV_TYPE_DESCRIPTOR:
			ptr = nvpair_pack_descriptor(nvp, ptr, fdidxp, &left);
			break;
		case NV_TYPE_DESCRIPTOR_ARRAY:
			ptr = nvpair_pack_descriptor_array(nvp, ptr, fdidxp,
			    &left);
			break;
#endif
		case NV_TYPE_BINARY:
			ptr = nvpair_pack_binary(nvp, ptr, &left);
			break;
		case NV_TYPE_BOOL_ARRAY:
			ptr = nvpair_pack_bool_array(nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER_ARRAY:
			ptr = nvpair_pack_number_array(nvp, ptr, &left);
			break;
		case NV_TYPE_STRING_ARRAY:
			ptr = nvpair_pack_string_array(nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST_ARRAY:
		    {
			const nvlist_t * const * value;
			size_t nitems;
			unsigned int ii;

			tmpnvl = NULL;
			value = nvpair_get_nvlist_array(nvp, &nitems);
			for (ii = 0; ii < nitems; ii++) {
				ptr = nvlist_pack_header(value[ii], ptr, &left);
				if (ptr == NULL)
					goto out;
				tmpnvp = nvlist_first_nvpair(value[ii]);
				if (tmpnvp != NULL) {
					tmpnvl = value[ii];
					break;
				}
				ptr = nvpair_pack_nvlist_array_next(ptr, &left);
				if (ptr == NULL)
					goto out;
			}
			if (tmpnvl != NULL) {
				nvl = tmpnvl;
				nvp = tmpnvp;
				continue;
			}
			break;
		    }
		default:
			PJDLOG_ABORT("Invalid type (%d).", nvpair_type(nvp));
		}
		if (ptr == NULL)
			goto fail;
		while ((nvp = nvlist_next_nvpair(nvl, nvp)) == NULL) {
			do {
				cookie = NULL;
				if (nvlist_in_array(nvl)) {
					ptr = nvpair_pack_nvlist_array_next(ptr,
					    &left);
					if (ptr == NULL)
						goto fail;
				}
				nvl = nvlist_get_pararr(nvl, &cookie);
				if (nvl == NULL)
					goto out;
				if (nvlist_in_array(nvl) && cookie == NULL) {
					nvp = nvlist_first_nvpair(nvl);
					ptr = nvlist_pack_header(nvl, ptr,
					    &left);
					if (ptr == NULL)
						goto fail;
				} else if (nvpair_type((nvpair_t *)cookie) !=
				    NV_TYPE_NVLIST_ARRAY) {
					ptr = nvpair_pack_nvlist_up(ptr, &left);
					if (ptr == NULL)
						goto fail;
					nvp = cookie;
				} else {
					nvp = cookie;
				}
			} while (nvp == NULL);
			if (nvlist_in_array(nvl) && cookie == NULL)
				break;
		}
	}

out:
	if (sizep != NULL)
		*sizep = size;
	return (buf);
fail:
	nv_free(buf);
	return (NULL);
}

void *
nvlist_pack(const nvlist_t *nvl, size_t *sizep)
{

	NVLIST_ASSERT(nvl);

	if (nvl->nvl_error != 0) {
		ERRNO_SET(nvl->nvl_error);
		return (NULL);
	}

	if (nvlist_ndescriptors(nvl) > 0) {
		ERRNO_SET(EOPNOTSUPP);
		return (NULL);
	}

	return (nvlist_xpack(nvl, NULL, sizep));
}

static bool
nvlist_check_header(struct nvlist_header *nvlhdrp)
{

	if (nvlhdrp->nvlh_magic != NVLIST_HEADER_MAGIC) {
		ERRNO_SET(EINVAL);
		return (false);
	}
	if ((nvlhdrp->nvlh_flags & ~NV_FLAG_ALL_MASK) != 0) {
		ERRNO_SET(EINVAL);
		return (false);
	}
#if BYTE_ORDER == BIG_ENDIAN
	if ((nvlhdrp->nvlh_flags & NV_FLAG_BIG_ENDIAN) == 0) {
		nvlhdrp->nvlh_size = le64toh(nvlhdrp->nvlh_size);
		nvlhdrp->nvlh_descriptors = le64toh(nvlhdrp->nvlh_descriptors);
	}
#else
	if ((nvlhdrp->nvlh_flags & NV_FLAG_BIG_ENDIAN) != 0) {
		nvlhdrp->nvlh_size = be64toh(nvlhdrp->nvlh_size);
		nvlhdrp->nvlh_descriptors = be64toh(nvlhdrp->nvlh_descriptors);
	}
#endif
	return (true);
}

const unsigned char *
nvlist_unpack_header(nvlist_t *nvl, const unsigned char *ptr, size_t nfds,
    bool *isbep, size_t *leftp)
{
	struct nvlist_header nvlhdr;
	int inarrayf;

	if (*leftp < sizeof(nvlhdr))
		goto fail;

	memcpy(&nvlhdr, ptr, sizeof(nvlhdr));

	if (!nvlist_check_header(&nvlhdr))
		goto fail;

	if (nvlhdr.nvlh_size != *leftp - sizeof(nvlhdr))
		goto fail;

	/*
	 * nvlh_descriptors might be smaller than nfds in embedded nvlists.
	 */
	if (nvlhdr.nvlh_descriptors > nfds)
		goto fail;

	if ((nvlhdr.nvlh_flags & ~NV_FLAG_ALL_MASK) != 0)
		goto fail;

	inarrayf = (nvl->nvl_flags & NV_FLAG_IN_ARRAY);
	nvl->nvl_flags = (nvlhdr.nvlh_flags & NV_FLAG_PUBLIC_MASK) | inarrayf;

	ptr += sizeof(nvlhdr);
	if (isbep != NULL)
		*isbep = (((int)nvlhdr.nvlh_flags & NV_FLAG_BIG_ENDIAN) != 0);
	*leftp -= sizeof(nvlhdr);

	return (ptr);
fail:
	ERRNO_SET(EINVAL);
	return (NULL);
}

static nvlist_t *
nvlist_xunpack(const void *buf, size_t size, const int *fds, size_t nfds,
    int flags)
{
	const unsigned char *ptr;
	nvlist_t *nvl, *retnvl, *tmpnvl, *array;
	nvpair_t *nvp;
	size_t left;
	bool isbe;

	PJDLOG_ASSERT((flags & ~(NV_FLAG_PUBLIC_MASK)) == 0);

	left = size;
	ptr = buf;

	tmpnvl = array = NULL;
	nvl = retnvl = nvlist_create(0);
	if (nvl == NULL)
		goto fail;

	ptr = nvlist_unpack_header(nvl, ptr, nfds, &isbe, &left);
	if (ptr == NULL)
		goto fail;
	if (nvl->nvl_flags != flags) {
		ERRNO_SET(EILSEQ);
		goto fail;
	}

	while (left > 0) {
		ptr = nvpair_unpack(isbe, ptr, &left, &nvp);
		if (ptr == NULL)
			goto fail;
		switch (nvpair_type(nvp)) {
		case NV_TYPE_NULL:
			ptr = nvpair_unpack_null(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_BOOL:
			ptr = nvpair_unpack_bool(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER:
			ptr = nvpair_unpack_number(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_STRING:
			ptr = nvpair_unpack_string(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST:
			ptr = nvpair_unpack_nvlist(isbe, nvp, ptr, &left, nfds,
			    &tmpnvl);
			if (tmpnvl == NULL || ptr == NULL)
				goto fail;
			nvlist_set_parent(tmpnvl, nvp);
			break;
#ifndef _KERNEL
		case NV_TYPE_DESCRIPTOR:
			ptr = nvpair_unpack_descriptor(isbe, nvp, ptr, &left,
			    fds, nfds);
			break;
		case NV_TYPE_DESCRIPTOR_ARRAY:
			ptr = nvpair_unpack_descriptor_array(isbe, nvp, ptr,
			    &left, fds, nfds);
			break;
#endif
		case NV_TYPE_BINARY:
			ptr = nvpair_unpack_binary(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST_UP:
			if (nvl->nvl_parent == NULL)
				goto fail;
			nvl = nvpair_nvlist(nvl->nvl_parent);
			nvpair_free_structure(nvp);
			continue;
		case NV_TYPE_NVLIST_ARRAY_NEXT:
			if (nvl->nvl_array_next == NULL) {
				if (nvl->nvl_parent == NULL)
					goto fail;
				nvl = nvpair_nvlist(nvl->nvl_parent);
			} else {
				nvl = __DECONST(nvlist_t *,
				    nvlist_get_array_next(nvl));
				ptr = nvlist_unpack_header(nvl, ptr, nfds,
				    &isbe, &left);
				if (ptr == NULL)
					goto fail;
			}
			nvpair_free_structure(nvp);
			continue;
		case NV_TYPE_BOOL_ARRAY:
			ptr = nvpair_unpack_bool_array(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NUMBER_ARRAY:
			ptr = nvpair_unpack_number_array(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_STRING_ARRAY:
			ptr = nvpair_unpack_string_array(isbe, nvp, ptr, &left);
			break;
		case NV_TYPE_NVLIST_ARRAY:
			ptr = nvpair_unpack_nvlist_array(isbe, nvp, ptr, &left,
			    &array);
			if (ptr == NULL)
				goto fail;
			PJDLOG_ASSERT(array != NULL);
			tmpnvl = array;
			do {
				nvlist_set_parent(array, nvp);
				array = __DECONST(nvlist_t *,
				    nvlist_get_array_next(array));
			} while (array != NULL);
			ptr = nvlist_unpack_header(tmpnvl, ptr, nfds, &isbe,
			    &left);
			break;
		default:
			PJDLOG_ABORT("Invalid type (%d).", nvpair_type(nvp));
		}
		if (ptr == NULL)
			goto fail;
		if (!nvlist_move_nvpair(nvl, nvp))
			goto fail;
		if (tmpnvl != NULL) {
			nvl = tmpnvl;
			tmpnvl = NULL;
		}
	}

	return (retnvl);
fail:
	nvlist_destroy(retnvl);
	return (NULL);
}

nvlist_t *
nvlist_unpack(const void *buf, size_t size, int flags)
{

	return (nvlist_xunpack(buf, size, NULL, 0, flags));
}

#ifndef _KERNEL
int
nvlist_send(int sock, const nvlist_t *nvl)
{
	size_t datasize, nfds;
	int *fds;
	void *data;
	int64_t fdidx;
	int ret;

	if (nvlist_error(nvl) != 0) {
		ERRNO_SET(nvlist_error(nvl));
		return (-1);
	}

	fds = nvlist_descriptors(nvl, &nfds);
	if (fds == NULL)
		return (-1);

	ret = -1;
	fdidx = 0;

	data = nvlist_xpack(nvl, &fdidx, &datasize);
	if (data == NULL)
		goto out;

	if (buf_send(sock, data, datasize) == -1)
		goto out;

	if (nfds > 0) {
		if (fd_send(sock, fds, nfds) == -1)
			goto out;
	}

	ret = 0;
out:
	ERRNO_SAVE();
	nv_free(fds);
	nv_free(data);
	ERRNO_RESTORE();
	return (ret);
}

nvlist_t *
nvlist_recv(int sock, int flags)
{
	struct nvlist_header nvlhdr;
	nvlist_t *nvl, *ret;
	unsigned char *buf;
	size_t nfds, size, i;
	int *fds;

	if (buf_recv(sock, &nvlhdr, sizeof(nvlhdr)) == -1)
		return (NULL);

	if (!nvlist_check_header(&nvlhdr))
		return (NULL);

	nfds = (size_t)nvlhdr.nvlh_descriptors;
	size = sizeof(nvlhdr) + (size_t)nvlhdr.nvlh_size;

	buf = nv_malloc(size);
	if (buf == NULL)
		return (NULL);

	memcpy(buf, &nvlhdr, sizeof(nvlhdr));

	ret = NULL;
	fds = NULL;

	if (buf_recv(sock, buf + sizeof(nvlhdr), size - sizeof(nvlhdr)) == -1)
		goto out;

	if (nfds > 0) {
		fds = nv_malloc(nfds * sizeof(fds[0]));
		if (fds == NULL)
			goto out;
		if (fd_recv(sock, fds, nfds) == -1)
			goto out;
	}

	nvl = nvlist_xunpack(buf, size, fds, nfds, flags);
	if (nvl == NULL) {
		ERRNO_SAVE();
		for (i = 0; i < nfds; i++)
			close(fds[i]);
		ERRNO_RESTORE();
		goto out;
	}

	ret = nvl;
out:
	ERRNO_SAVE();
	nv_free(buf);
	nv_free(fds);
	ERRNO_RESTORE();

	return (ret);
}

nvlist_t *
nvlist_xfer(int sock, nvlist_t *nvl, int flags)
{

	if (nvlist_send(sock, nvl) < 0) {
		nvlist_destroy(nvl);
		return (NULL);
	}
	nvlist_destroy(nvl);
	return (nvlist_recv(sock, flags));
}
#endif

nvpair_t *
nvlist_first_nvpair(const nvlist_t *nvl)
{

	NVLIST_ASSERT(nvl);

	return (TAILQ_FIRST(&nvl->nvl_head));
}

nvpair_t *
nvlist_next_nvpair(const nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *retnvp;

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	retnvp = nvpair_next(nvp);
	PJDLOG_ASSERT(retnvp == NULL || nvpair_nvlist(retnvp) == nvl);

	return (retnvp);

}

nvpair_t *
nvlist_prev_nvpair(const nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *retnvp;

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	retnvp = nvpair_prev(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(retnvp) == nvl);

	return (retnvp);
}

const char *
nvlist_next(const nvlist_t *nvl, int *typep, void **cookiep)
{
	nvpair_t *nvp;

	NVLIST_ASSERT(nvl);

	if (cookiep == NULL || *cookiep == NULL)
		nvp = nvlist_first_nvpair(nvl);
	else
		nvp = nvlist_next_nvpair(nvl, *cookiep);
	if (nvp == NULL)
		return (NULL);
	if (typep != NULL)
		*typep = nvpair_type(nvp);
	if (cookiep != NULL)
		*cookiep = nvp;
	return (nvpair_name(nvp));
}

bool
nvlist_exists(const nvlist_t *nvl, const char *name)
{

	return (nvlist_find(nvl, NV_TYPE_NONE, name) != NULL);
}

#define	NVLIST_EXISTS(type, TYPE)					\
bool									\
nvlist_exists_##type(const nvlist_t *nvl, const char *name)		\
{									\
									\
	return (nvlist_find(nvl, NV_TYPE_##TYPE, name) != NULL);	\
}

NVLIST_EXISTS(null, NULL)
NVLIST_EXISTS(bool, BOOL)
NVLIST_EXISTS(number, NUMBER)
NVLIST_EXISTS(string, STRING)
NVLIST_EXISTS(nvlist, NVLIST)
NVLIST_EXISTS(binary, BINARY)
NVLIST_EXISTS(bool_array, BOOL_ARRAY)
NVLIST_EXISTS(number_array, NUMBER_ARRAY)
NVLIST_EXISTS(string_array, STRING_ARRAY)
NVLIST_EXISTS(nvlist_array, NVLIST_ARRAY)
#ifndef _KERNEL
NVLIST_EXISTS(descriptor, DESCRIPTOR)
NVLIST_EXISTS(descriptor_array, DESCRIPTOR_ARRAY)
#endif

#undef	NVLIST_EXISTS

void
nvlist_add_nvpair(nvlist_t *nvl, const nvpair_t *nvp)
{
	nvpair_t *newnvp;

	NVPAIR_ASSERT(nvp);

	if (nvlist_error(nvl) != 0) {
		ERRNO_SET(nvlist_error(nvl));
		return;
	}
	if ((nvl->nvl_flags & NV_FLAG_NO_UNIQUE) == 0) {
		if (nvlist_exists(nvl, nvpair_name(nvp))) {
			nvl->nvl_error = EEXIST;
			ERRNO_SET(nvlist_error(nvl));
			return;
		}
	}

	newnvp = nvpair_clone(nvp);
	if (newnvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvpair_insert(&nvl->nvl_head, newnvp, nvl);
}

void
nvlist_add_stringf(nvlist_t *nvl, const char *name, const char *valuefmt, ...)
{
	va_list valueap;

	va_start(valueap, valuefmt);
	nvlist_add_stringv(nvl, name, valuefmt, valueap);
	va_end(valueap);
}

void
nvlist_add_stringv(nvlist_t *nvl, const char *name, const char *valuefmt,
    va_list valueap)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_create_stringv(name, valuefmt, valueap);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

void
nvlist_add_null(nvlist_t *nvl, const char *name)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_create_null(name);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

void
nvlist_add_binary(nvlist_t *nvl, const char *name, const void *value,
    size_t size)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_create_binary(name, value, size);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}


#define	NVLIST_ADD(vtype, type)						\
void									\
nvlist_add_##type(nvlist_t *nvl, const char *name, vtype value)		\
{									\
	nvpair_t *nvp;							\
									\
	if (nvlist_error(nvl) != 0) {					\
		ERRNO_SET(nvlist_error(nvl));				\
		return;							\
	}								\
									\
	nvp = nvpair_create_##type(name, value);			\
	if (nvp == NULL) {						\
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);		\
		ERRNO_SET(nvl->nvl_error);				\
	} else {							\
		(void)nvlist_move_nvpair(nvl, nvp);			\
	}								\
}

NVLIST_ADD(bool, bool)
NVLIST_ADD(uint64_t, number)
NVLIST_ADD(const char *, string)
NVLIST_ADD(const nvlist_t *, nvlist)
#ifndef _KERNEL
NVLIST_ADD(int, descriptor);
#endif

#undef	NVLIST_ADD

#define	NVLIST_ADD_ARRAY(vtype, type)					\
void									\
nvlist_add_##type##_array(nvlist_t *nvl, const char *name, vtype value,	\
    size_t nitems)							\
{									\
	nvpair_t *nvp;							\
									\
	if (nvlist_error(nvl) != 0) {					\
		ERRNO_SET(nvlist_error(nvl));				\
		return;							\
	}								\
									\
	nvp = nvpair_create_##type##_array(name, value, nitems);	\
	if (nvp == NULL) {						\
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);		\
		ERRNO_SET(nvl->nvl_error);				\
	} else {							\
		(void)nvlist_move_nvpair(nvl, nvp);			\
	}								\
}

NVLIST_ADD_ARRAY(const bool *, bool)
NVLIST_ADD_ARRAY(const uint64_t *, number)
NVLIST_ADD_ARRAY(const char * const *, string)
NVLIST_ADD_ARRAY(const nvlist_t * const *, nvlist)
#ifndef _KERNEL
NVLIST_ADD_ARRAY(const int *, descriptor)
#endif

#undef	NVLIST_ADD_ARRAY

#define	NVLIST_APPEND_ARRAY(vtype, type, TYPE)				\
void									\
nvlist_append_##type##_array(nvlist_t *nvl, const char *name, vtype value)\
{									\
	nvpair_t *nvp;							\
									\
	if (nvlist_error(nvl) != 0) {					\
		ERRNO_SET(nvlist_error(nvl));				\
		return;							\
	}								\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE##_ARRAY, name);		\
	if (nvp == NULL) {						\
		nvlist_add_##type##_array(nvl, name, &value, 1);	\
		return;							\
	}								\
	if (nvpair_append_##type##_array(nvp, value) == -1) {		\
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);		\
		ERRNO_SET(nvl->nvl_error);				\
	}								\
}

NVLIST_APPEND_ARRAY(const bool, bool, BOOL)
NVLIST_APPEND_ARRAY(const uint64_t, number, NUMBER)
NVLIST_APPEND_ARRAY(const char *, string, STRING)
NVLIST_APPEND_ARRAY(const nvlist_t *, nvlist, NVLIST)
#ifndef _KERNEL
NVLIST_APPEND_ARRAY(const int, descriptor, DESCRIPTOR)
#endif

#undef	NVLIST_APPEND_ARRAY

bool
nvlist_move_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == NULL);

	if (nvlist_error(nvl) != 0) {
		nvpair_free(nvp);
		ERRNO_SET(nvlist_error(nvl));
		return (false);
	}
	if ((nvl->nvl_flags & NV_FLAG_NO_UNIQUE) == 0) {
		if (nvlist_exists(nvl, nvpair_name(nvp))) {
			nvpair_free(nvp);
			nvl->nvl_error = EEXIST;
			ERRNO_SET(nvl->nvl_error);
			return (false);
		}
	}

	nvpair_insert(&nvl->nvl_head, nvp, nvl);
	return (true);
}

void
nvlist_move_string(nvlist_t *nvl, const char *name, char *value)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		nv_free(value);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_string(name, value);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

void
nvlist_move_nvlist(nvlist_t *nvl, const char *name, nvlist_t *value)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		if (value != NULL && nvlist_get_nvpair_parent(value) != NULL)
			nvlist_destroy(value);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_nvlist(name, value);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

#ifndef _KERNEL
void
nvlist_move_descriptor(nvlist_t *nvl, const char *name, int value)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		close(value);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_descriptor(name, value);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}
#endif

void
nvlist_move_binary(nvlist_t *nvl, const char *name, void *value, size_t size)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		nv_free(value);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_binary(name, value, size);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

void
nvlist_move_bool_array(nvlist_t *nvl, const char *name, bool *value,
    size_t nitems)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		nv_free(value);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_bool_array(name, value, nitems);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

void
nvlist_move_string_array(nvlist_t *nvl, const char *name, char **value,
    size_t nitems)
{
	nvpair_t *nvp;
	size_t i;

	if (nvlist_error(nvl) != 0) {
		if (value != NULL) {
			for (i = 0; i < nitems; i++)
				nv_free(value[i]);
			nv_free(value);
		}
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_string_array(name, value, nitems);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

void
nvlist_move_nvlist_array(nvlist_t *nvl, const char *name, nvlist_t **value,
    size_t nitems)
{
	nvpair_t *nvp;
	size_t i;

	if (nvlist_error(nvl) != 0) {
		if (value != NULL) {
			for (i = 0; i < nitems; i++) {
				if (nvlist_get_pararr(value[i], NULL) == NULL)
					nvlist_destroy(value[i]);
			}
		}
		nv_free(value);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_nvlist_array(name, value, nitems);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

void
nvlist_move_number_array(nvlist_t *nvl, const char *name, uint64_t *value,
    size_t nitems)
{
	nvpair_t *nvp;

	if (nvlist_error(nvl) != 0) {
		nv_free(value);
		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_number_array(name, value, nitems);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}

#ifndef _KERNEL
void
nvlist_move_descriptor_array(nvlist_t *nvl, const char *name, int *value,
    size_t nitems)
{
	nvpair_t *nvp;
	size_t i;

	if (nvlist_error(nvl) != 0) {
		if (value != 0) {
			for (i = 0; i < nitems; i++)
				close(value[i]);
			nv_free(value);
		}

		ERRNO_SET(nvlist_error(nvl));
		return;
	}

	nvp = nvpair_move_descriptor_array(name, value, nitems);
	if (nvp == NULL) {
		nvl->nvl_error = ERRNO_OR_DEFAULT(ENOMEM);
		ERRNO_SET(nvl->nvl_error);
	} else {
		(void)nvlist_move_nvpair(nvl, nvp);
	}
}
#endif

const nvpair_t *
nvlist_get_nvpair(const nvlist_t *nvl, const char *name)
{

	return (nvlist_find(nvl, NV_TYPE_NONE, name));
}

#define	NVLIST_GET(ftype, type, TYPE)					\
ftype									\
nvlist_get_##type(const nvlist_t *nvl, const char *name)		\
{									\
	const nvpair_t *nvp;						\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE, name);			\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, name);		\
	return (nvpair_get_##type(nvp));				\
}

NVLIST_GET(bool, bool, BOOL)
NVLIST_GET(uint64_t, number, NUMBER)
NVLIST_GET(const char *, string, STRING)
NVLIST_GET(const nvlist_t *, nvlist, NVLIST)
#ifndef _KERNEL
NVLIST_GET(int, descriptor, DESCRIPTOR)
#endif

#undef	NVLIST_GET

const void *
nvlist_get_binary(const nvlist_t *nvl, const char *name, size_t *sizep)
{
	nvpair_t *nvp;

	nvp = nvlist_find(nvl, NV_TYPE_BINARY, name);
	if (nvp == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, name);

	return (nvpair_get_binary(nvp, sizep));
}

#define	NVLIST_GET_ARRAY(ftype, type, TYPE)				\
ftype									\
nvlist_get_##type##_array(const nvlist_t *nvl, const char *name,	\
    size_t *nitems)							\
{									\
	const nvpair_t *nvp;						\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE##_ARRAY, name);		\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE##_ARRAY, name);	\
	return (nvpair_get_##type##_array(nvp, nitems));		\
}

NVLIST_GET_ARRAY(const bool *, bool, BOOL)
NVLIST_GET_ARRAY(const uint64_t *, number, NUMBER)
NVLIST_GET_ARRAY(const char * const *, string, STRING)
NVLIST_GET_ARRAY(const nvlist_t * const *, nvlist, NVLIST)
#ifndef _KERNEL
NVLIST_GET_ARRAY(const int *, descriptor, DESCRIPTOR)
#endif

#undef	NVLIST_GET_ARRAY

#define	NVLIST_TAKE(ftype, type, TYPE)					\
ftype									\
nvlist_take_##type(nvlist_t *nvl, const char *name)			\
{									\
	nvpair_t *nvp;							\
	ftype value;							\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE, name);			\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE, name);		\
	value = (ftype)(intptr_t)nvpair_get_##type(nvp);		\
	nvlist_remove_nvpair(nvl, nvp);					\
	nvpair_free_structure(nvp);					\
	return (value);							\
}

NVLIST_TAKE(bool, bool, BOOL)
NVLIST_TAKE(uint64_t, number, NUMBER)
NVLIST_TAKE(char *, string, STRING)
NVLIST_TAKE(nvlist_t *, nvlist, NVLIST)
#ifndef _KERNEL
NVLIST_TAKE(int, descriptor, DESCRIPTOR)
#endif

#undef	NVLIST_TAKE

void *
nvlist_take_binary(nvlist_t *nvl, const char *name, size_t *sizep)
{
	nvpair_t *nvp;
	void *value;

	nvp = nvlist_find(nvl, NV_TYPE_BINARY, name);
	if (nvp == NULL)
		nvlist_report_missing(NV_TYPE_BINARY, name);

	value = (void *)(intptr_t)nvpair_get_binary(nvp, sizep);
	nvlist_remove_nvpair(nvl, nvp);
	nvpair_free_structure(nvp);
	return (value);
}

#define	NVLIST_TAKE_ARRAY(ftype, type, TYPE)				\
ftype									\
nvlist_take_##type##_array(nvlist_t *nvl, const char *name,		\
    size_t *nitems)							\
{									\
	nvpair_t *nvp;							\
	ftype value;							\
									\
	nvp = nvlist_find(nvl, NV_TYPE_##TYPE##_ARRAY, name);		\
	if (nvp == NULL)						\
		nvlist_report_missing(NV_TYPE_##TYPE##_ARRAY, name);	\
	value = (ftype)(intptr_t)nvpair_get_##type##_array(nvp, nitems);\
	nvlist_remove_nvpair(nvl, nvp);					\
	nvpair_free_structure(nvp);					\
	return (value);							\
}

NVLIST_TAKE_ARRAY(bool *, bool, BOOL)
NVLIST_TAKE_ARRAY(uint64_t *, number, NUMBER)
NVLIST_TAKE_ARRAY(char **, string, STRING)
NVLIST_TAKE_ARRAY(nvlist_t **, nvlist, NVLIST)
#ifndef _KERNEL
NVLIST_TAKE_ARRAY(int *, descriptor, DESCRIPTOR)
#endif

void
nvlist_remove_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	nvpair_remove(&nvl->nvl_head, nvp, nvl);
}

void
nvlist_free(nvlist_t *nvl, const char *name)
{

	nvlist_free_type(nvl, name, NV_TYPE_NONE);
}

#define	NVLIST_FREE(type, TYPE)						\
void									\
nvlist_free_##type(nvlist_t *nvl, const char *name)			\
{									\
									\
	nvlist_free_type(nvl, name, NV_TYPE_##TYPE);			\
}

NVLIST_FREE(null, NULL)
NVLIST_FREE(bool, BOOL)
NVLIST_FREE(number, NUMBER)
NVLIST_FREE(string, STRING)
NVLIST_FREE(nvlist, NVLIST)
NVLIST_FREE(binary, BINARY)
NVLIST_FREE(bool_array, BOOL_ARRAY)
NVLIST_FREE(number_array, NUMBER_ARRAY)
NVLIST_FREE(string_array, STRING_ARRAY)
NVLIST_FREE(nvlist_array, NVLIST_ARRAY)
#ifndef _KERNEL
NVLIST_FREE(descriptor, DESCRIPTOR)
NVLIST_FREE(descriptor_array, DESCRIPTOR_ARRAY)
#endif

#undef	NVLIST_FREE

void
nvlist_free_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{

	NVLIST_ASSERT(nvl);
	NVPAIR_ASSERT(nvp);
	PJDLOG_ASSERT(nvpair_nvlist(nvp) == nvl);

	nvlist_remove_nvpair(nvl, nvp);
	nvpair_free(nvp);
}

