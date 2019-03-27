/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998-2002,2010 Luigi Rizzo, Universita` di Pisa
 * All rights reserved
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

/*
 * Userland code for testing binary heaps and hash tables
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/param.h>

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>

#include  "dn_heap.h"
#define log(x, arg...)	fprintf(stderr, ## arg)
#define panic(x...)	fprintf(stderr, ## x), exit(1)

#include <string.h>

struct x {
	struct x *ht_link;
	char buf[0];
};

uint32_t hf(uintptr_t key, int flags, void *arg)
{
	return (flags & DNHT_KEY_IS_OBJ) ?
		((struct x *)key)->buf[0] : *(char *)key;
}

int matchf(void *obj, uintptr_t key, int flags, void *arg)
{
	char *s = (flags & DNHT_KEY_IS_OBJ) ?
		((struct x *)key)->buf : (char *)key;
	return (strcmp(((struct x *)obj)->buf, s) == 0);
}

void *newfn(uintptr_t key, int flags, void *arg)
{
	char *s = (char *)key;
	struct x *p = malloc(sizeof(*p) + 1 + strlen(s));
	if (p)
		strcpy(p->buf, s);
	return p;
}

char *strings[] = {
	"undici", "unico", "doppio", "devoto",
	"uno", "due", "tre", "quattro", "cinque", "sei",
	"uno", "due", "tre", "quattro", "cinque", "sei",
	NULL,
};

int doprint(void *_x, void *arg)
{
	struct x *x = _x;
	printf("found element <%s>\n", x->buf);
	return (int)arg;
}

static void
test_hash()
{
	char **p;
	struct dn_ht *h;
	uintptr_t x = 0;
	uintptr_t x1 = 0;

	/* first, find and allocate */
	h = dn_ht_init(NULL, 10, 0, hf, matchf, newfn);

	for (p = strings; *p; p++) {
		dn_ht_find(h, (uintptr_t)*p, DNHT_INSERT, NULL);
	}
	dn_ht_scan(h, doprint, 0);
	printf("/* second -- find without allocate */\n");
	h = dn_ht_init(NULL, 10, 0, hf, matchf, NULL);
	for (p = strings; *p; p++) {
		void **y = newfn((uintptr_t)*p, 0, NULL);
		if (x == 0)
			x = (uintptr_t)y;
		else {
			if (x1 == 0)
				x1 = (uintptr_t)*p;
		}
		dn_ht_find(h, (uintptr_t)y, DNHT_INSERT | DNHT_KEY_IS_OBJ, NULL);
	}
	dn_ht_scan(h, doprint, 0);
	printf("remove %p gives %p\n", (void *)x,
		dn_ht_find(h, x, DNHT_KEY_IS_OBJ | DNHT_REMOVE, NULL));
	printf("remove %p gives %p\n", (void *)x,
		dn_ht_find(h, x, DNHT_KEY_IS_OBJ | DNHT_REMOVE, NULL));
	printf("remove %p gives %p\n", (void *)x,
		dn_ht_find(h, x1, DNHT_REMOVE, NULL));
	printf("remove %p gives %p\n", (void *)x,
		dn_ht_find(h, x1, DNHT_REMOVE, NULL));
	dn_ht_scan(h, doprint, 0);
}

int
main(int argc, char *argv[])
{
	struct dn_heap h;
	int i, n, n2, n3;

	test_hash();
	return 0;

	/* n = elements, n2 = cycles */
	n = (argc > 1) ? atoi(argv[1]) : 0;
	if (n <= 0 || n > 1000000)
		n = 100;
	n2 = (argc > 2) ? atoi(argv[2]) : 0;
	if (n2 <= 0)
		n = 1000000;
	n3 = (argc > 3) ? atoi(argv[3]) : 0;
	bzero(&h, sizeof(h));
	heap_init(&h, n, -1);
	while (n2-- > 0) {
		uint64_t prevk = 0;
		for (i=0; i < n; i++)
			heap_insert(&h, n3 ? n-i: random(), (void *)(100+i));
		
		for (i=0; h.elements > 0; i++) {
			uint64_t k = h.p[0].key;
			if (k < prevk)
				panic("wrong sequence\n");
			prevk = k;
			if (0)
			printf("%d key %llu, val %p\n",
				i, h.p[0].key, h.p[0].object);
			heap_extract(&h, NULL);
		}
	}
	return 0;
}
