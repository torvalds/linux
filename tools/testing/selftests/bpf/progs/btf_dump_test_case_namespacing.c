// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C dumper test validating no name versioning happens between
 * independent C namespaces (struct/union/enum vs typedef/enum values).
 *
 * Copyright (c) 2019 Facebook
 */
/* ----- START-EXPECTED-OUTPUT ----- */
struct S {
	int S;
	int U;
};

typedef struct S S;

union U {
	int S;
	int U;
};

typedef union U U;

enum E {
	V = 0,
};

typedef enum E E;

struct A {};

union B {};

enum C {
	A = 1,
	B = 2,
	C = 3,
};

struct X {};

union Y {};

enum Z;

typedef int X;

typedef int Y;

typedef int Z;

/*------ END-EXPECTED-OUTPUT ------ */

int f(struct {
	struct S _1;
	S _2;
	union U _3;
	U _4;
	enum E _5;
	E _6;
	struct A a;
	union B b;
	enum C c;
	struct X x;
	union Y y;
	enum Z *z;
	X xx;
	Y yy;
	Z zz;
} *_)
{
	return 0;
}
