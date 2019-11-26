// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C dumper tests for implicit and explicit padding between fields and
 * at the end of a struct.
 *
 * Copyright (c) 2019 Facebook
 */
/* ----- START-EXPECTED-OUTPUT ----- */
struct padded_implicitly {
	int a;
	long int b;
	char c;
};

/* ------ END-EXPECTED-OUTPUT ------ */

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct padded_explicitly {
 *	int a;
 *	int: 32;
 *	int b;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct padded_explicitly {
	int a;
	int: 1; /* algo will explicitly pad with full 32 bits here */
	int b;
};

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct padded_a_lot {
 *	int a;
 *	long: 32;
 *	long: 64;
 *	long: 64;
 *	int b;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct padded_a_lot {
	int a;
	/* 32 bit of implicit padding here, which algo will make explicit */
	long: 64;
	long: 64;
	int b;
};

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct padded_cache_line {
 *	int a;
 *	long: 32;
 *	long: 64;
 *	long: 64;
 *	long: 64;
 *	int b;
 *	long: 32;
 *	long: 64;
 *	long: 64;
 *	long: 64;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct padded_cache_line {
	int a;
	int b __attribute__((aligned(32)));
};

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct zone_padding {
 *	char x[0];
 *};
 *
 *struct zone {
 *	int a;
 *	short b;
 *	short: 16;
 *	struct zone_padding __pad__;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct zone_padding {
	char x[0];
} __attribute__((__aligned__(8)));

struct zone {
	int a;
	short b;
	struct zone_padding __pad__;
};

int f(struct {
	struct padded_implicitly _1;
	struct padded_explicitly _2;
	struct padded_a_lot _3;
	struct padded_cache_line _4;
	struct zone _5;
} *_)
{
	return 0;
}
