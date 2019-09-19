// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C dumper tests for bitfield.
 *
 * Copyright (c) 2019 Facebook
 */
#include <stdbool.h>

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct bitfields_only_mixed_types {
 *	int a: 3;
 *	long int b: 2;
 *	_Bool c: 1;
 *	enum {
 *		A = 0,
 *		B = 1,
 *	} d: 1;
 *	short e: 5;
 *	int: 20;
 *	unsigned int f: 30;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct bitfields_only_mixed_types {
	int a: 3;
	long int b: 2;
	bool c: 1; /* it's really a _Bool type */
	enum {
		A, /* A = 0, dumper is very explicit */
		B, /* B = 1, same */
	} d: 1;
	short e: 5;
	/* 20-bit padding here */
	unsigned f: 30; /* this gets aligned on 4-byte boundary */
};

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct bitfield_mixed_with_others {
 *	char: 4;
 *	int a: 4;
 *	short b;
 *	long int c;
 *	long int d: 8;
 *	int e;
 *	int f;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */
struct bitfield_mixed_with_others {
	long: 4; /* char is enough as a backing field */
	int a: 4;
	/* 8-bit implicit padding */
	short b; /* combined with previous bitfield */
	/* 4 more bytes of implicit padding */
	long c;
	long d: 8;
	/* 24 bits implicit padding */
	int e; /* combined with previous bitfield */
	int f;
	/* 4 bytes of padding */
};

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct bitfield_flushed {
 *	int a: 4;
 *	long: 60;
 *	long int b: 16;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */
struct bitfield_flushed {
	int a: 4;
	long: 0; /* flush until next natural alignment boundary */
	long b: 16;
};

int f(struct {
	struct bitfields_only_mixed_types _1;
	struct bitfield_mixed_with_others _2;
	struct bitfield_flushed _3;
} *_)
{
	return 0;
}
