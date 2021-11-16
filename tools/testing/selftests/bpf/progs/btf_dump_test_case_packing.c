// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C dumper tests for struct packing determination.
 *
 * Copyright (c) 2019 Facebook
 */
/* ----- START-EXPECTED-OUTPUT ----- */
struct packed_trailing_space {
	int a;
	short b;
} __attribute__((packed));

struct non_packed_trailing_space {
	int a;
	short b;
};

struct packed_fields {
	short a;
	int b;
} __attribute__((packed));

struct non_packed_fields {
	short a;
	int b;
};

struct nested_packed {
	char: 4;
	int a: 4;
	long b;
	struct {
		char c;
		int d;
	} __attribute__((packed)) e;
} __attribute__((packed));

union union_is_never_packed {
	int a: 4;
	char b;
	char c: 1;
};

union union_does_not_need_packing {
	struct {
		long a;
		int b;
	} __attribute__((packed));
	int c;
};

union jump_code_union {
	char code[5];
	struct {
		char jump;
		int offset;
	} __attribute__((packed));
};

/*------ END-EXPECTED-OUTPUT ------ */

int f(struct {
	struct packed_trailing_space _1;
	struct non_packed_trailing_space _2;
	struct packed_fields _3;
	struct non_packed_fields _4;
	struct nested_packed _5;
	union union_is_never_packed _6;
	union union_does_not_need_packing _7;
	union jump_code_union _8;
} *_)
{
	return 0;
}
