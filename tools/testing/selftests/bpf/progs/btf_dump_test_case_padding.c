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
 *	long: 0;
 *	int b;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct padded_explicitly {
	int a;
	int: 1; /* algo will emit aligning `long: 0;` here */
	int b;
};

/* ----- START-EXPECTED-OUTPUT ----- */
struct padded_a_lot {
	int a;
	long: 64;
	long: 64;
	int b;
};

/* ------ END-EXPECTED-OUTPUT ------ */

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct padded_cache_line {
 *	int a;
 *	long: 64;
 *	long: 64;
 *	long: 64;
 *	int b;
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
 *	long: 0;
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

/* ----- START-EXPECTED-OUTPUT ----- */
struct padding_wo_named_members {
	long: 64;
	long: 64;
};

struct padding_weird_1 {
	int a;
	long: 64;
	short: 16;
	short b;
};

/* ------ END-EXPECTED-OUTPUT ------ */

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct padding_weird_2 {
 *	long: 56;
 *	char a;
 *	long: 56;
 *	char b;
 *	char: 8;
 *};
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */
struct padding_weird_2 {
	int: 32;	/* these paddings will be collapsed into `long: 56;` */
	short: 16;
	char: 8;
	char a;
	int: 32;	/* these paddings will be collapsed into `long: 56;` */
	short: 16;
	char: 8;
	char b;
	char: 8;
};

/* ------ END-EXPECTED-OUTPUT ------ */

int f(struct {
	struct padded_implicitly _1;
	struct padded_explicitly _2;
	struct padded_a_lot _3;
	struct padded_cache_line _4;
	struct zone _5;
	struct padding_wo_named_members _6;
	struct padding_weird_1 _7;
	struct padding_weird_2 _8;
} *_)
{
	return 0;
}
