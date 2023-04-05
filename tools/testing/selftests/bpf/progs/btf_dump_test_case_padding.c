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

/* ----- START-EXPECTED-OUTPUT ----- */
struct exact_1byte {
	char x;
};

struct padded_1byte {
	char: 8;
};

struct exact_2bytes {
	short x;
};

struct padded_2bytes {
	short: 16;
};

struct exact_4bytes {
	int x;
};

struct padded_4bytes {
	int: 32;
};

struct exact_8bytes {
	long x;
};

struct padded_8bytes {
	long: 64;
};

struct ff_periodic_effect {
	int: 32;
	short magnitude;
	long: 0;
	short phase;
	long: 0;
	int: 32;
	int custom_len;
	short *custom_data;
};

struct ib_wc {
	long: 64;
	long: 64;
	int: 32;
	int byte_len;
	void *qp;
	union {} ex;
	long: 64;
	int slid;
	int wc_flags;
	long: 64;
	char smac[6];
	long: 0;
	char network_hdr_type;
};

struct acpi_object_method {
	long: 64;
	char: 8;
	char type;
	short reference_count;
	char flags;
	short: 0;
	char: 8;
	char sync_level;
	long: 64;
	void *node;
	void *aml_start;
	union {} dispatch;
	long: 64;
	int aml_length;
};

struct nested_unpacked {
	int x;
};

struct nested_packed {
	struct nested_unpacked a;
	char c;
} __attribute__((packed));

struct outer_mixed_but_unpacked {
	struct nested_packed b1;
	short a1;
	struct nested_packed b2;
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
	struct exact_1byte _100;
	struct padded_1byte _101;
	struct exact_2bytes _102;
	struct padded_2bytes _103;
	struct exact_4bytes _104;
	struct padded_4bytes _105;
	struct exact_8bytes _106;
	struct padded_8bytes _107;
	struct ff_periodic_effect _200;
	struct ib_wc _201;
	struct acpi_object_method _202;
	struct outer_mixed_but_unpacked _203;
} *_)
{
	return 0;
}
