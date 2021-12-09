// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C dumper test for majority of C syntax quirks.
 *
 * Copyright (c) 2019 Facebook
 */
/* ----- START-EXPECTED-OUTPUT ----- */
enum e1 {
	A = 0,
	B = 1,
};

enum e2 {
	C = 100,
	D = 4294967295,
	E = 0,
};

typedef enum e2 e2_t;

typedef enum {
	F = 0,
	G = 1,
	H = 2,
} e3_t;

typedef int int_t;

typedef volatile const int * volatile const crazy_ptr_t;

typedef int *****we_need_to_go_deeper_ptr_t;

typedef volatile const we_need_to_go_deeper_ptr_t * restrict * volatile * const * restrict volatile * restrict const * volatile const * restrict volatile const how_about_this_ptr_t;

typedef int *ptr_arr_t[10];

typedef void (*fn_ptr1_t)(int);

typedef void (*printf_fn_t)(const char *, ...);

/* ------ END-EXPECTED-OUTPUT ------ */
/*
 * While previous function pointers are pretty trivial (C-syntax-level
 * trivial), the following are deciphered here for future generations:
 *
 * - `fn_ptr2_t`: function, taking anonymous struct as a first arg and pointer
 *   to a function, that takes int and returns int, as a second arg; returning
 *   a pointer to a const pointer to a char. Equivalent to:
 *	typedef struct { int a; } s_t;
 *	typedef int (*fn_t)(int);
 *	typedef char * const * (*fn_ptr2_t)(s_t, fn_t);
 *
 * - `fn_complext_t`: pointer to a function returning struct and accepting
 *   union and struct. All structs and enum are anonymous and defined inline.
 *
 * - `signal_t: pointer to a function accepting a pointer to a function as an
 *   argument and returning pointer to a function as a result. Sane equivalent:
 *	typedef void (*signal_handler_t)(int);
 *	typedef signal_handler_t (*signal_ptr_t)(int, signal_handler_t);
 *
 * - fn_ptr_arr1_t: array of pointers to a function accepting pointer to
 *   a pointer to an int and returning pointer to a char. Easy.
 *
 * - fn_ptr_arr2_t: array of const pointers to a function taking no arguments
 *   and returning a const pointer to a function, that takes pointer to a
 *   `int -> char *` function and returns pointer to a char. Equivalent:
 *   typedef char * (*fn_input_t)(int);
 *   typedef char * (*fn_output_outer_t)(fn_input_t);
 *   typedef const fn_output_outer_t (* fn_output_inner_t)();
 *   typedef const fn_output_inner_t fn_ptr_arr2_t[5];
 */
/* ----- START-EXPECTED-OUTPUT ----- */
typedef char * const * (*fn_ptr2_t)(struct {
	int a;
}, int (*)(int));

typedef struct {
	int a;
	void (*b)(int, struct {
		int c;
	}, union {
		char d;
		int e[5];
	});
} (*fn_complex_t)(union {
	void *f;
	char g[16];
}, struct {
	int h;
});

typedef void (* (*signal_t)(int, void (*)(int)))(int);

typedef char * (*fn_ptr_arr1_t[10])(int **);

typedef char * (* const (* const fn_ptr_arr2_t[5])())(char * (*)(int));

struct struct_w_typedefs {
	int_t a;
	crazy_ptr_t b;
	we_need_to_go_deeper_ptr_t c;
	how_about_this_ptr_t d;
	ptr_arr_t e;
	fn_ptr1_t f;
	printf_fn_t g;
	fn_ptr2_t h;
	fn_complex_t i;
	signal_t j;
	fn_ptr_arr1_t k;
	fn_ptr_arr2_t l;
};

typedef struct {
	int x;
	int y;
	int z;
} anon_struct_t;

struct struct_fwd;

typedef struct struct_fwd struct_fwd_t;

typedef struct struct_fwd *struct_fwd_ptr_t;

union union_fwd;

typedef union union_fwd union_fwd_t;

typedef union union_fwd *union_fwd_ptr_t;

struct struct_empty {};

struct struct_simple {
	int a;
	char b;
	const int_t *p;
	struct struct_empty s;
	enum e2 e;
	enum {
		ANON_VAL1 = 1,
		ANON_VAL2 = 2,
	} f;
	int arr1[13];
	enum e2 arr2[5];
};

union union_empty {};

union union_simple {
	void *ptr;
	int num;
	int_t num2;
	union union_empty u;
};

struct struct_in_struct {
	struct struct_simple simple;
	union union_simple also_simple;
	struct {
		int a;
	} not_so_hard_as_well;
	union {
		int b;
		int c;
	} anon_union_is_good;
	struct {
		int d;
		int e;
	};
	union {
		int f;
		int g;
	};
};

struct struct_in_array {};

struct struct_in_array_typed {};

typedef struct struct_in_array_typed struct_in_array_t[2];

struct struct_with_embedded_stuff {
	int a;
	struct {
		int b;
		struct {
			struct struct_with_embedded_stuff *c;
			const char *d;
		} e;
		union {
			volatile long f;
			void * restrict g;
		};
	};
	union {
		const int_t *h;
		void (*i)(char, int, void *);
	} j;
	enum {
		K = 100,
		L = 200,
	} m;
	char n[16];
	struct {
		char o;
		int p;
		void (*q)(int);
	} r[5];
	struct struct_in_struct s[10];
	int t[11];
	struct struct_in_array (*u)[2];
	struct_in_array_t *v;
};

struct float_struct {
	float f;
	const double *d;
	volatile long double *ld;
};

struct root_struct {
	enum e1 _1;
	enum e2 _2;
	e2_t _2_1;
	e3_t _2_2;
	struct struct_w_typedefs _3;
	anon_struct_t _7;
	struct struct_fwd *_8;
	struct_fwd_t *_9;
	struct_fwd_ptr_t _10;
	union union_fwd *_11;
	union_fwd_t *_12;
	union_fwd_ptr_t _13;
	struct struct_with_embedded_stuff _14;
	struct float_struct _15;
};

/* ------ END-EXPECTED-OUTPUT ------ */

int f(struct root_struct *s)
{
	return 0;
}
