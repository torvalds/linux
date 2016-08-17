/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/nvpair.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/param.h>
#ifndef _KERNEL
#include <stdlib.h>
#endif

/*
 * "Force" nvlist wrapper.
 *
 * These functions wrap the nvlist_* functions with assertions that assume
 * the operation is successful.  This allows the caller's code to be much
 * more readable, especially for the fnvlist_lookup_* and fnvpair_value_*
 * functions, which can return the requested value (rather than filling in
 * a pointer).
 *
 * These functions use NV_UNIQUE_NAME, encoding NV_ENCODE_NATIVE, and allocate
 * with KM_SLEEP.
 *
 * More wrappers should be added as needed -- for example
 * nvlist_lookup_*_array and nvpair_value_*_array.
 */

nvlist_t *
fnvlist_alloc(void)
{
	nvlist_t *nvl;
	VERIFY0(nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP));
	return (nvl);
}

void
fnvlist_free(nvlist_t *nvl)
{
	nvlist_free(nvl);
}

size_t
fnvlist_size(nvlist_t *nvl)
{
	size_t size;
	VERIFY0(nvlist_size(nvl, &size, NV_ENCODE_NATIVE));
	return (size);
}

/*
 * Returns allocated buffer of size *sizep.  Caller must free the buffer with
 * fnvlist_pack_free().
 */
char *
fnvlist_pack(nvlist_t *nvl, size_t *sizep)
{
	char *packed = 0;
	VERIFY3U(nvlist_pack(nvl, &packed, sizep, NV_ENCODE_NATIVE,
	    KM_SLEEP), ==, 0);
	return (packed);
}

/*ARGSUSED*/
void
fnvlist_pack_free(char *pack, size_t size)
{
#ifdef _KERNEL
	kmem_free(pack, size);
#else
	free(pack);
#endif
}

nvlist_t *
fnvlist_unpack(char *buf, size_t buflen)
{
	nvlist_t *rv;
	VERIFY0(nvlist_unpack(buf, buflen, &rv, KM_SLEEP));
	return (rv);
}

nvlist_t *
fnvlist_dup(nvlist_t *nvl)
{
	nvlist_t *rv;
	VERIFY0(nvlist_dup(nvl, &rv, KM_SLEEP));
	return (rv);
}

void
fnvlist_merge(nvlist_t *dst, nvlist_t *src)
{
	VERIFY0(nvlist_merge(dst, src, KM_SLEEP));
}

size_t
fnvlist_num_pairs(nvlist_t *nvl)
{
	size_t count = 0;
	nvpair_t *pair;

	for (pair = nvlist_next_nvpair(nvl, 0); pair != NULL;
	    pair = nvlist_next_nvpair(nvl, pair))
		count++;
	return (count);
}

void
fnvlist_add_boolean(nvlist_t *nvl, const char *name)
{
	VERIFY0(nvlist_add_boolean(nvl, name));
}

void
fnvlist_add_boolean_value(nvlist_t *nvl, const char *name, boolean_t val)
{
	VERIFY0(nvlist_add_boolean_value(nvl, name, val));
}

void
fnvlist_add_byte(nvlist_t *nvl, const char *name, uchar_t val)
{
	VERIFY0(nvlist_add_byte(nvl, name, val));
}

void
fnvlist_add_int8(nvlist_t *nvl, const char *name, int8_t val)
{
	VERIFY0(nvlist_add_int8(nvl, name, val));
}

void
fnvlist_add_uint8(nvlist_t *nvl, const char *name, uint8_t val)
{
	VERIFY0(nvlist_add_uint8(nvl, name, val));
}

void
fnvlist_add_int16(nvlist_t *nvl, const char *name, int16_t val)
{
	VERIFY0(nvlist_add_int16(nvl, name, val));
}

void
fnvlist_add_uint16(nvlist_t *nvl, const char *name, uint16_t val)
{
	VERIFY0(nvlist_add_uint16(nvl, name, val));
}

void
fnvlist_add_int32(nvlist_t *nvl, const char *name, int32_t val)
{
	VERIFY0(nvlist_add_int32(nvl, name, val));
}

void
fnvlist_add_uint32(nvlist_t *nvl, const char *name, uint32_t val)
{
	VERIFY0(nvlist_add_uint32(nvl, name, val));
}

void
fnvlist_add_int64(nvlist_t *nvl, const char *name, int64_t val)
{
	VERIFY0(nvlist_add_int64(nvl, name, val));
}

void
fnvlist_add_uint64(nvlist_t *nvl, const char *name, uint64_t val)
{
	VERIFY0(nvlist_add_uint64(nvl, name, val));
}

void
fnvlist_add_string(nvlist_t *nvl, const char *name, const char *val)
{
	VERIFY0(nvlist_add_string(nvl, name, val));
}

void
fnvlist_add_nvlist(nvlist_t *nvl, const char *name, nvlist_t *val)
{
	VERIFY0(nvlist_add_nvlist(nvl, name, val));
}

void
fnvlist_add_nvpair(nvlist_t *nvl, nvpair_t *pair)
{
	VERIFY0(nvlist_add_nvpair(nvl, pair));
}

void
fnvlist_add_boolean_array(nvlist_t *nvl, const char *name,
    boolean_t *val, uint_t n)
{
	VERIFY0(nvlist_add_boolean_array(nvl, name, val, n));
}

void
fnvlist_add_byte_array(nvlist_t *nvl, const char *name, uchar_t *val, uint_t n)
{
	VERIFY0(nvlist_add_byte_array(nvl, name, val, n));
}

void
fnvlist_add_int8_array(nvlist_t *nvl, const char *name, int8_t *val, uint_t n)
{
	VERIFY0(nvlist_add_int8_array(nvl, name, val, n));
}

void
fnvlist_add_uint8_array(nvlist_t *nvl, const char *name, uint8_t *val, uint_t n)
{
	VERIFY0(nvlist_add_uint8_array(nvl, name, val, n));
}

void
fnvlist_add_int16_array(nvlist_t *nvl, const char *name, int16_t *val, uint_t n)
{
	VERIFY0(nvlist_add_int16_array(nvl, name, val, n));
}

void
fnvlist_add_uint16_array(nvlist_t *nvl, const char *name,
    uint16_t *val, uint_t n)
{
	VERIFY0(nvlist_add_uint16_array(nvl, name, val, n));
}

void
fnvlist_add_int32_array(nvlist_t *nvl, const char *name, int32_t *val, uint_t n)
{
	VERIFY0(nvlist_add_int32_array(nvl, name, val, n));
}

void
fnvlist_add_uint32_array(nvlist_t *nvl, const char *name,
    uint32_t *val, uint_t n)
{
	VERIFY0(nvlist_add_uint32_array(nvl, name, val, n));
}

void
fnvlist_add_int64_array(nvlist_t *nvl, const char *name, int64_t *val, uint_t n)
{
	VERIFY0(nvlist_add_int64_array(nvl, name, val, n));
}

void
fnvlist_add_uint64_array(nvlist_t *nvl, const char *name,
    uint64_t *val, uint_t n)
{
	VERIFY0(nvlist_add_uint64_array(nvl, name, val, n));
}

void
fnvlist_add_string_array(nvlist_t *nvl, const char *name,
    char * const *val, uint_t n)
{
	VERIFY0(nvlist_add_string_array(nvl, name, val, n));
}

void
fnvlist_add_nvlist_array(nvlist_t *nvl, const char *name,
    nvlist_t **val, uint_t n)
{
	VERIFY0(nvlist_add_nvlist_array(nvl, name, val, n));
}

void
fnvlist_remove(nvlist_t *nvl, const char *name)
{
	VERIFY0(nvlist_remove_all(nvl, name));
}

void
fnvlist_remove_nvpair(nvlist_t *nvl, nvpair_t *pair)
{
	VERIFY0(nvlist_remove_nvpair(nvl, pair));
}

nvpair_t *
fnvlist_lookup_nvpair(nvlist_t *nvl, const char *name)
{
	nvpair_t *rv;
	VERIFY0(nvlist_lookup_nvpair(nvl, name, &rv));
	return (rv);
}

/* returns B_TRUE if the entry exists */
boolean_t
fnvlist_lookup_boolean(nvlist_t *nvl, const char *name)
{
	return (nvlist_lookup_boolean(nvl, name) == 0);
}

boolean_t
fnvlist_lookup_boolean_value(nvlist_t *nvl, const char *name)
{
	boolean_t rv;
	VERIFY0(nvlist_lookup_boolean_value(nvl, name, &rv));
	return (rv);
}

uchar_t
fnvlist_lookup_byte(nvlist_t *nvl, const char *name)
{
	uchar_t rv;
	VERIFY0(nvlist_lookup_byte(nvl, name, &rv));
	return (rv);
}

int8_t
fnvlist_lookup_int8(nvlist_t *nvl, const char *name)
{
	int8_t rv;
	VERIFY0(nvlist_lookup_int8(nvl, name, &rv));
	return (rv);
}

int16_t
fnvlist_lookup_int16(nvlist_t *nvl, const char *name)
{
	int16_t rv;
	VERIFY0(nvlist_lookup_int16(nvl, name, &rv));
	return (rv);
}

int32_t
fnvlist_lookup_int32(nvlist_t *nvl, const char *name)
{
	int32_t rv;
	VERIFY0(nvlist_lookup_int32(nvl, name, &rv));
	return (rv);
}

int64_t
fnvlist_lookup_int64(nvlist_t *nvl, const char *name)
{
	int64_t rv;
	VERIFY0(nvlist_lookup_int64(nvl, name, &rv));
	return (rv);
}

uint8_t
fnvlist_lookup_uint8(nvlist_t *nvl, const char *name)
{
	uint8_t rv;
	VERIFY0(nvlist_lookup_uint8(nvl, name, &rv));
	return (rv);
}

uint16_t
fnvlist_lookup_uint16(nvlist_t *nvl, const char *name)
{
	uint16_t rv;
	VERIFY0(nvlist_lookup_uint16(nvl, name, &rv));
	return (rv);
}

uint32_t
fnvlist_lookup_uint32(nvlist_t *nvl, const char *name)
{
	uint32_t rv;
	VERIFY0(nvlist_lookup_uint32(nvl, name, &rv));
	return (rv);
}

uint64_t
fnvlist_lookup_uint64(nvlist_t *nvl, const char *name)
{
	uint64_t rv;
	VERIFY0(nvlist_lookup_uint64(nvl, name, &rv));
	return (rv);
}

char *
fnvlist_lookup_string(nvlist_t *nvl, const char *name)
{
	char *rv;
	VERIFY0(nvlist_lookup_string(nvl, name, &rv));
	return (rv);
}

nvlist_t *
fnvlist_lookup_nvlist(nvlist_t *nvl, const char *name)
{
	nvlist_t *rv;
	VERIFY0(nvlist_lookup_nvlist(nvl, name, &rv));
	return (rv);
}

boolean_t
fnvpair_value_boolean_value(nvpair_t *nvp)
{
	boolean_t rv;
	VERIFY0(nvpair_value_boolean_value(nvp, &rv));
	return (rv);
}

uchar_t
fnvpair_value_byte(nvpair_t *nvp)
{
	uchar_t rv;
	VERIFY0(nvpair_value_byte(nvp, &rv));
	return (rv);
}

int8_t
fnvpair_value_int8(nvpair_t *nvp)
{
	int8_t rv;
	VERIFY0(nvpair_value_int8(nvp, &rv));
	return (rv);
}

int16_t
fnvpair_value_int16(nvpair_t *nvp)
{
	int16_t rv;
	VERIFY0(nvpair_value_int16(nvp, &rv));
	return (rv);
}

int32_t
fnvpair_value_int32(nvpair_t *nvp)
{
	int32_t rv;
	VERIFY0(nvpair_value_int32(nvp, &rv));
	return (rv);
}

int64_t
fnvpair_value_int64(nvpair_t *nvp)
{
	int64_t rv;
	VERIFY0(nvpair_value_int64(nvp, &rv));
	return (rv);
}

uint8_t
fnvpair_value_uint8(nvpair_t *nvp)
{
	uint8_t rv;
	VERIFY0(nvpair_value_uint8(nvp, &rv));
	return (rv);
}

uint16_t
fnvpair_value_uint16(nvpair_t *nvp)
{
	uint16_t rv;
	VERIFY0(nvpair_value_uint16(nvp, &rv));
	return (rv);
}

uint32_t
fnvpair_value_uint32(nvpair_t *nvp)
{
	uint32_t rv;
	VERIFY0(nvpair_value_uint32(nvp, &rv));
	return (rv);
}

uint64_t
fnvpair_value_uint64(nvpair_t *nvp)
{
	uint64_t rv;
	VERIFY0(nvpair_value_uint64(nvp, &rv));
	return (rv);
}

char *
fnvpair_value_string(nvpair_t *nvp)
{
	char *rv;
	VERIFY0(nvpair_value_string(nvp, &rv));
	return (rv);
}

nvlist_t *
fnvpair_value_nvlist(nvpair_t *nvp)
{
	nvlist_t *rv;
	VERIFY0(nvpair_value_nvlist(nvp, &rv));
	return (rv);
}

#if defined(_KERNEL) && defined(HAVE_SPL)

EXPORT_SYMBOL(fnvlist_alloc);
EXPORT_SYMBOL(fnvlist_free);
EXPORT_SYMBOL(fnvlist_size);
EXPORT_SYMBOL(fnvlist_pack);
EXPORT_SYMBOL(fnvlist_pack_free);
EXPORT_SYMBOL(fnvlist_unpack);
EXPORT_SYMBOL(fnvlist_dup);
EXPORT_SYMBOL(fnvlist_merge);

EXPORT_SYMBOL(fnvlist_add_nvpair);
EXPORT_SYMBOL(fnvlist_add_boolean);
EXPORT_SYMBOL(fnvlist_add_boolean_value);
EXPORT_SYMBOL(fnvlist_add_byte);
EXPORT_SYMBOL(fnvlist_add_int8);
EXPORT_SYMBOL(fnvlist_add_uint8);
EXPORT_SYMBOL(fnvlist_add_int16);
EXPORT_SYMBOL(fnvlist_add_uint16);
EXPORT_SYMBOL(fnvlist_add_int32);
EXPORT_SYMBOL(fnvlist_add_uint32);
EXPORT_SYMBOL(fnvlist_add_int64);
EXPORT_SYMBOL(fnvlist_add_uint64);
EXPORT_SYMBOL(fnvlist_add_string);
EXPORT_SYMBOL(fnvlist_add_nvlist);
EXPORT_SYMBOL(fnvlist_add_boolean_array);
EXPORT_SYMBOL(fnvlist_add_byte_array);
EXPORT_SYMBOL(fnvlist_add_int8_array);
EXPORT_SYMBOL(fnvlist_add_uint8_array);
EXPORT_SYMBOL(fnvlist_add_int16_array);
EXPORT_SYMBOL(fnvlist_add_uint16_array);
EXPORT_SYMBOL(fnvlist_add_int32_array);
EXPORT_SYMBOL(fnvlist_add_uint32_array);
EXPORT_SYMBOL(fnvlist_add_int64_array);
EXPORT_SYMBOL(fnvlist_add_uint64_array);
EXPORT_SYMBOL(fnvlist_add_string_array);
EXPORT_SYMBOL(fnvlist_add_nvlist_array);

EXPORT_SYMBOL(fnvlist_remove);
EXPORT_SYMBOL(fnvlist_remove_nvpair);

EXPORT_SYMBOL(fnvlist_lookup_nvpair);
EXPORT_SYMBOL(fnvlist_lookup_boolean);
EXPORT_SYMBOL(fnvlist_lookup_boolean_value);
EXPORT_SYMBOL(fnvlist_lookup_byte);
EXPORT_SYMBOL(fnvlist_lookup_int8);
EXPORT_SYMBOL(fnvlist_lookup_uint8);
EXPORT_SYMBOL(fnvlist_lookup_int16);
EXPORT_SYMBOL(fnvlist_lookup_uint16);
EXPORT_SYMBOL(fnvlist_lookup_int32);
EXPORT_SYMBOL(fnvlist_lookup_uint32);
EXPORT_SYMBOL(fnvlist_lookup_int64);
EXPORT_SYMBOL(fnvlist_lookup_uint64);
EXPORT_SYMBOL(fnvlist_lookup_string);
EXPORT_SYMBOL(fnvlist_lookup_nvlist);

EXPORT_SYMBOL(fnvpair_value_boolean_value);
EXPORT_SYMBOL(fnvpair_value_byte);
EXPORT_SYMBOL(fnvpair_value_int8);
EXPORT_SYMBOL(fnvpair_value_uint8);
EXPORT_SYMBOL(fnvpair_value_int16);
EXPORT_SYMBOL(fnvpair_value_uint16);
EXPORT_SYMBOL(fnvpair_value_int32);
EXPORT_SYMBOL(fnvpair_value_uint32);
EXPORT_SYMBOL(fnvpair_value_int64);
EXPORT_SYMBOL(fnvpair_value_uint64);
EXPORT_SYMBOL(fnvpair_value_string);
EXPORT_SYMBOL(fnvpair_value_nvlist);
EXPORT_SYMBOL(fnvlist_num_pairs);

#endif
