/*
 * testcode/unitregional.c - unit test for regional allocator.
 *
 * Copyright (c) 2010, NLnet Labs. All rights reserved.
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
 * Tests the regional special purpose allocator.
 */

#include "config.h"
#include "testcode/unitmain.h"
#include "util/log.h"
#include "util/regional.h"

/** test regional corner cases, zero, one, end of structure */
static void
corner_cases(struct regional* r)
{
	size_t s; /* shadow count of allocated memory */
	void* a;
	size_t minsize = sizeof(uint64_t);
#ifndef UNBOUND_ALLOC_NONREGIONAL
	size_t mysize;
#endif
	char* str;
	unit_assert(r);
	/* alloc cases:
	 * 0, 1, 2.
	 * smaller than LARGE_OBJECT_SIZE.
	 * smaller but does not fit in remainder in regional.
	 * smaller but exactly fits in remainder of regional.
	 * size is remainder of regional - 8.
	 * size is remainder of regional + 8.
	 * larger than LARGE_OBJECT_SIZE.
	 */
	s = sizeof(struct regional);
	unit_assert((s % minsize) == 0);
	unit_assert(r->available == r->first_size - s);
	unit_assert(r->large_list == NULL);
	unit_assert(r->next == NULL);

	/* Note an alloc of 0 gets a pointer to current last
	 * position (where you should then use 0 bytes) */
	a = regional_alloc(r, 0);
	unit_assert(a);
	s+=0;
	unit_assert(r->available == r->first_size - s);

#ifndef UNBOUND_ALLOC_NONREGIONAL
	a = regional_alloc(r, 1);
	unit_assert(a);
	memset(a, 0x42, 1);
	s+=minsize;
	unit_assert(r->available == r->first_size - s);

	a = regional_alloc(r, 2);
	unit_assert(a);
	memset(a, 0x42, 2);
	s+=minsize;
	unit_assert(r->available == r->first_size - s);

	a = regional_alloc(r, 128);
	unit_assert(a);
	memset(a, 0x42, 128);
	s+=128;
	unit_assert(r->available == r->first_size - s);

	unit_assert(r->large_list == NULL);
	a = regional_alloc(r, 10240);
	unit_assert(a);
	unit_assert(r->large_list != NULL);
	memset(a, 0x42, 10240);
	/* s does not change */
	unit_assert(r->available == r->first_size - s);
	unit_assert(r->total_large == 10240+minsize);

	/* go towards the end of the current chunk */
	while(r->available > 1024) {
		a = regional_alloc(r, 1024);
		unit_assert(a);
		memset(a, 0x42, 1024);
		s += 1024;
		unit_assert(r->available == r->first_size - s);
	}

	unit_assert(r->next == NULL);
	mysize = 1280; /* does not fit in current chunk */
	a = regional_alloc(r, mysize);
	memset(a, 0x42, mysize);
	unit_assert(r->next != NULL);
	unit_assert(a);

	/* go towards the end of the current chunk */
	while(r->available > 864) {
		a = regional_alloc(r, 864);
		unit_assert(a);
		memset(a, 0x42, 864);
		s += 864;
	}

	mysize = r->available; /* exactly fits */
	a = regional_alloc(r, mysize);
	memset(a, 0x42, mysize);
	unit_assert(a);
	unit_assert(r->available == 0); /* implementation does not go ahead*/

	a = regional_alloc(r, 8192); /* another large allocation */
	unit_assert(a);
	memset(a, 0x42, 8192);
	unit_assert(r->available == 0);
	unit_assert(r->total_large == 10240 + 8192 + 2*minsize);

	a = regional_alloc(r, 32); /* make new chunk */
	unit_assert(a);
	memset(a, 0x42, 32);
	unit_assert(r->available > 0);
	unit_assert(r->total_large == 10240 + 8192 + 2*minsize);

	/* go towards the end of the current chunk */
	while(r->available > 1320) {
		a = regional_alloc(r, 1320);
		unit_assert(a);
		memset(a, 0x42, 1320);
		s += 1320;
	}

	mysize = r->available + 8; /* exact + 8 ; does not fit */
	a = regional_alloc(r, mysize);
	memset(a, 0x42, mysize);
	unit_assert(a);
	unit_assert(r->available > 0); /* new chunk */

	/* go towards the end of the current chunk */
	while(r->available > 1480) {
		a = regional_alloc(r, 1480);
		unit_assert(a);
		memset(a, 0x42, 1480);
		s += 1480;
	}

	mysize = r->available - 8; /* exact - 8 ; fits. */
	a = regional_alloc(r, mysize);
	memset(a, 0x42, mysize);
	unit_assert(a);
	unit_assert(r->available == 8);
#endif /* UNBOUND_ALLOC_NONREGIONAL */

	/* test if really copied over */
	str = "test12345";
	a = regional_alloc_init(r, str, 8);
	unit_assert(a);
	unit_assert(memcmp(a, str, 8) == 0);

	/* test if really zeroed */
	a = regional_alloc_zero(r, 32);
	str="\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	unit_assert(a);
	unit_assert(memcmp(a, str, 32) == 0);

	/* test if copied over (and null byte) */
	str = "an interesting string";
	a = regional_strdup(r, str);
	unit_assert(a);
	unit_assert(memcmp(a, str, strlen(str)+1) == 0);

	regional_free_all(r);
}

/** test specific cases */
static void
specific_cases(void)
{
	struct regional* r = regional_create();
	corner_cases(r);
	regional_destroy(r);
	r = regional_create_custom(2048); /* a small regional */
	unit_assert(r->first_size == 2048);
	unit_assert(regional_get_mem(r) == 2048);
	corner_cases(r);
	unit_assert(regional_get_mem(r) == 2048);
	regional_destroy(r);
}

/** put random stuff in a region and free it */
static void
burden_test(size_t max)
{
	size_t get;
	void* a;
	int i;
	struct regional* r = regional_create_custom(2048);
	for(i=0; i<1000; i++) {
		get = random() % max;
		a = regional_alloc(r, get);
		unit_assert(a);
		memset(a, 0x54, get);
	}
	regional_free_all(r);
	regional_destroy(r);
}

/** randomly allocate stuff */
static void
random_burden(void)
{
	size_t max_alloc = 2048 + 128; /* small chance of LARGE */
	int i;
	for(i=0; i<100; i++)
		burden_test(max_alloc);
}

void regional_test(void)
{
	unit_show_feature("regional");
	specific_cases();
	random_burden();
}
