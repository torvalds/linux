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

/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct nested_packed_but_aligned_struct {
 *	int x1;
 *	int x2;
 *};
 *
 *struct outer_implicitly_packed_struct {
 *	char y1;
 *	struct nested_packed_but_aligned_struct y2;
 *} __attribute__((packed));
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct nested_packed_but_aligned_struct {
	int x1;
	int x2;
} __attribute__((packed));

struct outer_implicitly_packed_struct {
	char y1;
	struct nested_packed_but_aligned_struct y2;
};
/* ----- START-EXPECTED-OUTPUT ----- */
/*
 *struct usb_ss_ep_comp_descriptor {
 *	char: 8;
 *	char bDescriptorType;
 *	char bMaxBurst;
 *	short wBytesPerInterval;
 *};
 *
 *struct usb_host_endpoint {
 *	long: 64;
 *	char: 8;
 *	struct usb_ss_ep_comp_descriptor ss_ep_comp;
 *	long: 0;
 *} __attribute__((packed));
 *
 */
/* ------ END-EXPECTED-OUTPUT ------ */

struct usb_ss_ep_comp_descriptor {
	char: 8;
	char bDescriptorType;
	char bMaxBurst;
	int: 0;
	short wBytesPerInterval;
} __attribute__((packed));

struct usb_host_endpoint {
	long: 64;
	char: 8;
	struct usb_ss_ep_comp_descriptor ss_ep_comp;
	long: 0;
};

/* ----- START-EXPECTED-OUTPUT ----- */
struct nested_packed_struct {
	int a;
	char b;
} __attribute__((packed));

struct outer_nonpacked_struct {
	short a;
	struct nested_packed_struct b;
};

struct outer_packed_struct {
	short a;
	struct nested_packed_struct b;
} __attribute__((packed));

/* ------ END-EXPECTED-OUTPUT ------ */

int f(struct {
	struct packed_trailing_space _1;
	struct non_packed_trailing_space _2;
	struct packed_fields _3;
	struct non_packed_fields _4;
	struct nested_packed _5;
	union union_is_never_packed _6;
	union union_does_not_need_packing _7;
	union jump_code_union _8;
	struct outer_implicitly_packed_struct _9;
	struct usb_host_endpoint _10;
	struct outer_nonpacked_struct _11;
	struct outer_packed_struct _12;
} *_)
{
	return 0;
}
