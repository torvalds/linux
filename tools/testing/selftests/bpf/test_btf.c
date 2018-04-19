/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <bpf/bpf.h>
#include <sys/resource.h>
#include <libelf.h>
#include <gelf.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <bpf/libbpf.h>
#include <bpf/btf.h>

#include "bpf_rlimit.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define __printf(a, b)	__attribute__((format(printf, a, b)))

__printf(1, 2)
static int __base_pr(const char *format, ...)
{
	va_list args;
	int err;

	va_start(args, format);
	err = vfprintf(stderr, format, args);
	va_end(args);
	return err;
}

#define BTF_INFO_ENC(kind, root, vlen)			\
	((!!(root) << 31) | ((kind) << 24) | ((vlen) & BTF_MAX_VLEN))

#define BTF_TYPE_ENC(name, info, size_or_type)	\
	(name), (info), (size_or_type)

#define BTF_INT_ENC(encoding, bits_offset, nr_bits)	\
	((encoding) << 24 | (bits_offset) << 16 | (nr_bits))
#define BTF_TYPE_INT_ENC(name, encoding, bits_offset, bits, sz)	\
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_INT, 0, 0), sz),	\
	BTF_INT_ENC(encoding, bits_offset, bits)

#define BTF_ARRAY_ENC(type, index_type, nr_elems)	\
	(type), (index_type), (nr_elems)
#define BTF_TYPE_ARRAY_ENC(type, index_type, nr_elems) \
	BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ARRAY, 0, 0), 0), \
	BTF_ARRAY_ENC(type, index_type, nr_elems)

#define BTF_MEMBER_ENC(name, type, bits_offset)	\
	(name), (type), (bits_offset)
#define BTF_ENUM_ENC(name, val) (name), (val)

#define BTF_TYPEDEF_ENC(name, type) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0), type)

#define BTF_PTR_ENC(name, type) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), type)

#define BTF_END_RAW 0xdeadbeef
#define NAME_TBD 0xdeadb33f

#define MAX_NR_RAW_TYPES 1024
#define BTF_LOG_BUF_SIZE 65535

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static struct args {
	unsigned int raw_test_num;
	unsigned int file_test_num;
	unsigned int get_info_test_num;
	bool raw_test;
	bool file_test;
	bool get_info_test;
	bool pprint_test;
	bool always_log;
} args;

static char btf_log_buf[BTF_LOG_BUF_SIZE];

static struct btf_header hdr_tmpl = {
	.magic = BTF_MAGIC,
	.version = BTF_VERSION,
};

struct btf_raw_test {
	const char *descr;
	const char *str_sec;
	const char *map_name;
	__u32 raw_types[MAX_NR_RAW_TYPES];
	__u32 str_sec_size;
	enum bpf_map_type map_type;
	__u32 key_size;
	__u32 value_size;
	__u32 key_id;
	__u32 value_id;
	__u32 max_entries;
	bool btf_load_err;
	bool map_create_err;
	int type_off_delta;
	int str_off_delta;
	int str_len_delta;
};

static struct btf_raw_test raw_tests[] = {
/* enum E {
 *     E0,
 *     E1,
 * };
 *
 * struct A {
 *	int m;
 *	unsigned long long n;
 *	char o;
 *	[3 bytes hole]
 *	int p[8];
 *	int q[4][8];
 *	enum E r;
 * };
 */
{
	.descr = "struct test #1",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* unsigned long long */
		BTF_TYPE_INT_ENC(0, 0, 0, 64, 8),		/* [2] */
		/* char */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 8, 1),	/* [3] */
		/* int[8] */
		BTF_TYPE_ARRAY_ENC(1, 1, 8),			/* [4] */
		/* struct A { */				/* [5] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 6), 180),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m;		*/
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* unsigned long long n;*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		BTF_MEMBER_ENC(NAME_TBD, 6, 384),/* int q[4][8]		*/
		BTF_MEMBER_ENC(NAME_TBD, 7, 1408), /* enum E r		*/
		/* } */
		/* int[4][8] */
		BTF_TYPE_ARRAY_ENC(4, 1, 4),			/* [6] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 2), sizeof(int)),
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_ENUM_ENC(NAME_TBD, 1),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0q\0r\0E\0E0\0E1",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0q\0r\0E\0E0\0E1"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_test1_map",
	.key_size = sizeof(int),
	.value_size = 180,
	.key_id = 1,
	.value_id = 5,
	.max_entries = 4,
},

/* typedef struct b Struct_B;
 *
 * struct A {
 *     int m;
 *     struct b n[4];
 *     const Struct_B o[4];
 * };
 *
 * struct B {
 *     int m;
 *     int n;
 * };
 */
{
	.descr = "struct test #2",
	.raw_types = {
		/* int */					/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* struct b [4] */				/* [2] */
		BTF_TYPE_ARRAY_ENC(4, 1, 4),

		/* struct A { */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 3), 68),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m;		*/
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* struct B n[4]	*/
		BTF_MEMBER_ENC(NAME_TBD, 8, 288),/* const Struct_B o[4];*/
		/* } */

		/* struct B { */				/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 8),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m; */
		BTF_MEMBER_ENC(NAME_TBD, 1, 32),/* int n; */
		/* } */

		/* const int */					/* [5] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 1),
		/* typedef struct b Struct_B */	/* [6] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0), 4),
		/* const Struct_B */				/* [7] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 6),
		/* const Struct_B [4] */			/* [8] */
		BTF_TYPE_ARRAY_ENC(7, 1, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0B\0m\0n\0Struct_B",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0B\0m\0n\0Struct_B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_test2_map",
	.key_size = sizeof(int),
	.value_size = 68,
	.key_id = 1,
	.value_id = 3,
	.max_entries = 4,
},

/* Test member exceeds the size of struct.
 *
 * struct A {
 *     int m;
 *     int n;
 * };
 */
{
	.descr = "size check test #1",
	.raw_types = {
		/* int */					/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* struct A { */				/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), sizeof(int) * 2 -  1),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m; */
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* int n; */
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n",
	.str_sec_size = sizeof("\0A\0m\0n"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "size_check1_map",
	.key_size = sizeof(int),
	.value_size = 1,
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

/* Test member exeeds the size of struct
 *
 * struct A {
 *     int m;
 *     int n[2];
 * };
 */
{
	.descr = "size check test #2",
	.raw_types = {
		/* int */					/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, sizeof(int)),
		/* int[2] */					/* [2] */
		BTF_TYPE_ARRAY_ENC(1, 1, 2),
		/* struct A { */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), sizeof(int) * 3 - 1),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m; */
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* int n[2]; */
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n",
	.str_sec_size = sizeof("\0A\0m\0n"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "size_check2_map",
	.key_size = sizeof(int),
	.value_size = 1,
	.key_id = 1,
	.value_id = 3,
	.max_entries = 4,
	.btf_load_err = true,

},

/* Test member exeeds the size of struct
 *
 * struct A {
 *     int m;
 *     void *n;
 * };
 */
{
	.descr = "size check test #3",
	.raw_types = {
		/* int */					/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, sizeof(int)),
		/* void* */					/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 0),
		/* struct A { */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), sizeof(int) + sizeof(void *) - 1),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m; */
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* void *n; */
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n",
	.str_sec_size = sizeof("\0A\0m\0n"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "size_check3_map",
	.key_size = sizeof(int),
	.value_size = 1,
	.key_id = 1,
	.value_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
},

/* Test member exceeds the size of struct
 *
 * enum E {
 *     E0,
 *     E1,
 * };
 *
 * struct A {
 *     int m;
 *     enum E n;
 * };
 */
{
	.descr = "size check test #4",
	.raw_types = {
		/* int */			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, sizeof(int)),
		/* enum E { */			/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 2), sizeof(int)),
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_ENUM_ENC(NAME_TBD, 1),
		/* } */
		/* struct A { */		/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), sizeof(int) * 2 - 1),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m; */
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* enum E n; */
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0E\0E0\0E1\0A\0m\0n",
	.str_sec_size = sizeof("\0E\0E0\0E1\0A\0m\0n"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "size_check4_map",
	.key_size = sizeof(int),
	.value_size = 1,
	.key_id = 1,
	.value_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
},

/* typedef const void * const_void_ptr;
 * struct A {
 *	const_void_ptr m;
 * };
 */
{
	.descr = "void test #1",
	.raw_types = {
		/* int */		/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* const void */	/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 0),
		/* const void* */	/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 2),
		/* typedef const void * const_void_ptr */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 3),
		/* struct A { */	/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), sizeof(void *)),
		/* const_void_ptr m; */
		BTF_MEMBER_ENC(NAME_TBD, 3, 0),
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0const_void_ptr\0A\0m",
	.str_sec_size = sizeof("\0const_void_ptr\0A\0m"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "void_test1_map",
	.key_size = sizeof(int),
	.value_size = sizeof(void *),
	.key_id = 1,
	.value_id = 4,
	.max_entries = 4,
},

/* struct A {
 *     const void m;
 * };
 */
{
	.descr = "void test #2",
	.raw_types = {
		/* int */		/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* const void */	/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 0),
		/* struct A { */	/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 8),
		/* const void m; */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m",
	.str_sec_size = sizeof("\0A\0m"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "void_test2_map",
	.key_size = sizeof(int),
	.value_size = sizeof(void *),
	.key_id = 1,
	.value_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
},

/* typedef const void * const_void_ptr;
 * const_void_ptr[4]
 */
{
	.descr = "void test #3",
	.raw_types = {
		/* int */		/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* const void */	/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 0),
		/* const void* */	/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 2),
		/* typedef const void * const_void_ptr */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 3),
		/* const_void_ptr[4] */	/* [4] */
		BTF_TYPE_ARRAY_ENC(3, 1, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0const_void_ptr",
	.str_sec_size = sizeof("\0const_void_ptr"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "void_test3_map",
	.key_size = sizeof(int),
	.value_size = sizeof(void *) * 4,
	.key_id = 1,
	.value_id = 4,
	.max_entries = 4,
},

/* const void[4]  */
{
	.descr = "void test #4",
	.raw_types = {
		/* int */		/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* const void */	/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 0),
		/* const void[4] */	/* [3] */
		BTF_TYPE_ARRAY_ENC(2, 1, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m",
	.str_sec_size = sizeof("\0A\0m"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "void_test4_map",
	.key_size = sizeof(int),
	.value_size = sizeof(void *) * 4,
	.key_id = 1,
	.value_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
},

/* Array_A  <------------------+
 *     elem_type == Array_B    |
 *                    |        |
 *                    |        |
 * Array_B  <-------- +        |
 *      elem_type == Array A --+
 */
{
	.descr = "loop test #1",
	.raw_types = {
		/* int */			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* Array_A */			/* [2] */
		BTF_TYPE_ARRAY_ENC(3, 1, 8),
		/* Array_B */			/* [3] */
		BTF_TYPE_ARRAY_ENC(2, 1, 8),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test1_map",
	.key_size = sizeof(int),
	.value_size = sizeof(sizeof(int) * 8),
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

/* typedef is _before_ the BTF type of Array_A and Array_B
 *
 * typedef Array_B int_array;
 *
 * Array_A  <------------------+
 *     elem_type == int_array  |
 *                    |        |
 *                    |        |
 * Array_B  <-------- +        |
 *      elem_type == Array_A --+
 */
{
	.descr = "loop test #2",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* typedef Array_B int_array */
		BTF_TYPEDEF_ENC(1, 4),				/* [2] */
		/* Array_A */
		BTF_TYPE_ARRAY_ENC(2, 1, 8),			/* [3] */
		/* Array_B */
		BTF_TYPE_ARRAY_ENC(3, 1, 8),			/* [4] */

		BTF_END_RAW,
	},
	.str_sec = "\0int_array\0",
	.str_sec_size = sizeof("\0int_array"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test2_map",
	.key_size = sizeof(int),
	.value_size = sizeof(sizeof(int) * 8),
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

/* Array_A  <------------------+
 *     elem_type == Array_B    |
 *                    |        |
 *                    |        |
 * Array_B  <-------- +        |
 *      elem_type == Array_A --+
 */
{
	.descr = "loop test #3",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* Array_A */				/* [2] */
		BTF_TYPE_ARRAY_ENC(3, 1, 8),
		/* Array_B */				/* [3] */
		BTF_TYPE_ARRAY_ENC(2, 1, 8),

		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test3_map",
	.key_size = sizeof(int),
	.value_size = sizeof(sizeof(int) * 8),
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

/* typedef is _between_ the BTF type of Array_A and Array_B
 *
 * typedef Array_B int_array;
 *
 * Array_A  <------------------+
 *     elem_type == int_array  |
 *                    |        |
 *                    |        |
 * Array_B  <-------- +        |
 *      elem_type == Array_A --+
 */
{
	.descr = "loop test #4",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* Array_A */				/* [2] */
		BTF_TYPE_ARRAY_ENC(3, 1, 8),
		/* typedef Array_B int_array */		/* [3] */
		BTF_TYPEDEF_ENC(NAME_TBD, 4),
		/* Array_B */				/* [4] */
		BTF_TYPE_ARRAY_ENC(2, 1, 8),
		BTF_END_RAW,
	},
	.str_sec = "\0int_array\0",
	.str_sec_size = sizeof("\0int_array"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test4_map",
	.key_size = sizeof(int),
	.value_size = sizeof(sizeof(int) * 8),
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

/* typedef struct B Struct_B
 *
 * struct A {
 *     int x;
 *     Struct_B y;
 * };
 *
 * struct B {
 *     int x;
 *     struct A y;
 * };
 */
{
	.descr = "loop test #5",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* struct A */					/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 8),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int x;	*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 32),/* Struct_B y;	*/
		/* typedef struct B Struct_B */
		BTF_TYPEDEF_ENC(NAME_TBD, 4),			/* [3] */
		/* struct B */					/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 8),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int x;	*/
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* struct A y;	*/
		BTF_END_RAW,
	},
	.str_sec = "\0A\0x\0y\0Struct_B\0B\0x\0y",
	.str_sec_size = sizeof("\0A\0x\0y\0Struct_B\0B\0x\0y"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test5_map",
	.key_size = sizeof(int),
	.value_size = 8,
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

/* struct A {
 *     int x;
 *     struct A array_a[4];
 * };
 */
{
	.descr = "loop test #6",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_ARRAY_ENC(3, 1, 4),			/* [2] */
		/* struct A */					/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 8),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int x;		*/
		BTF_MEMBER_ENC(NAME_TBD, 2, 32),/* struct A array_a[4];	*/
		BTF_END_RAW,
	},
	.str_sec = "\0A\0x\0y",
	.str_sec_size = sizeof("\0A\0x\0y"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test6_map",
	.key_size = sizeof(int),
	.value_size = 8,
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

{
	.descr = "loop test #7",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* struct A { */			/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), sizeof(void *)),
		/*     const void *m;	*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 0),
		/* CONST type_id=3	*/		/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 4),
		/* PTR type_id=2	*/		/* [4] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 3),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m",
	.str_sec_size = sizeof("\0A\0m"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test7_map",
	.key_size = sizeof(int),
	.value_size = sizeof(void *),
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

{
	.descr = "loop test #8",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* struct A { */			/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), sizeof(void *)),
		/*     const void *m;	*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 0),
		/* struct B { */			/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), sizeof(void *)),
		/*     const void *n;	*/
		BTF_MEMBER_ENC(NAME_TBD, 6, 0),
		/* CONST type_id=5	*/		/* [4] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 5),
		/* PTR type_id=6	*/		/* [5] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 6),
		/* CONST type_id=7	*/		/* [6] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 7),
		/* PTR type_id=4	*/		/* [7] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0B\0n",
	.str_sec_size = sizeof("\0A\0m\0B\0n"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "loop_test8_map",
	.key_size = sizeof(int),
	.value_size = sizeof(void *),
	.key_id = 1,
	.value_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
},

{
	.descr = "type_off == str_off",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_id = 1,
	.value_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.type_off_delta = sizeof(struct btf_type) + sizeof(int) + sizeof("\0int"),
},

{
	.descr = "Unaligned type_off",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_id = 1,
	.value_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.type_off_delta = 1,
},

{
	.descr = "str_off beyonds btf size",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_id = 1,
	.value_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_off_delta = sizeof("\0int") + 1,
},

{
	.descr = "str_len beyonds btf size",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_id = 1,
	.value_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_len_delta = 1,
},

{
	.descr = "String section does not end with null",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_id = 1,
	.value_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_len_delta = -1,
},

{
	.descr = "Empty string section",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_id = 1,
	.value_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_len_delta = 0 - (int)sizeof("\0int"),
},

}; /* struct btf_raw_test raw_tests[] */

static const char *get_next_str(const char *start, const char *end)
{
	return start < end - 1 ? start + 1 : NULL;
}

static int get_type_sec_size(const __u32 *raw_types)
{
	int i;

	for (i = MAX_NR_RAW_TYPES - 1;
	     i >= 0 && raw_types[i] != BTF_END_RAW;
	     i--)
		;

	return i < 0 ? i : i * sizeof(raw_types[0]);
}

static void *btf_raw_create(const struct btf_header *hdr,
			    const __u32 *raw_types,
			    const char *str,
			    unsigned int str_sec_size,
			    unsigned int *btf_size)
{
	const char *next_str = str, *end_str = str + str_sec_size;
	unsigned int size_needed, offset;
	struct btf_header *ret_hdr;
	int i, type_sec_size;
	uint32_t *ret_types;
	void *raw_btf;

	type_sec_size = get_type_sec_size(raw_types);
	if (type_sec_size < 0) {
		fprintf(stderr, "Cannot get nr_raw_types\n");
		return NULL;
	}

	size_needed = sizeof(*hdr) + type_sec_size + str_sec_size;
	raw_btf = malloc(size_needed);
	if (!raw_btf) {
		fprintf(stderr, "Cannot allocate memory for raw_btf\n");
		return NULL;
	}

	/* Copy header */
	memcpy(raw_btf, hdr, sizeof(*hdr));
	offset = sizeof(*hdr);

	/* Copy type section */
	ret_types = raw_btf + offset;
	for (i = 0; i < type_sec_size / sizeof(raw_types[0]); i++) {
		if (raw_types[i] == NAME_TBD) {
			next_str = get_next_str(next_str, end_str);
			if (!next_str) {
				fprintf(stderr, "Error in getting next_str\n");
				free(raw_btf);
				return NULL;
			}
			ret_types[i] = next_str - str;
			next_str += strlen(next_str);
		} else {
			ret_types[i] = raw_types[i];
		}
	}
	offset += type_sec_size;

	/* Copy string section */
	memcpy(raw_btf + offset, str, str_sec_size);

	ret_hdr = (struct btf_header *)raw_btf;
	ret_hdr->str_off = type_sec_size;
	ret_hdr->str_len = str_sec_size;

	*btf_size = size_needed;

	return raw_btf;
}

static int do_test_raw(unsigned int test_num)
{
	struct btf_raw_test *test = &raw_tests[test_num - 1];
	struct bpf_create_map_attr create_attr = {};
	int map_fd = -1, btf_fd = -1;
	unsigned int raw_btf_size;
	struct btf_header *hdr;
	void *raw_btf;
	int err;

	fprintf(stderr, "BTF raw test[%u] (%s): ", test_num, test->descr);
	raw_btf = btf_raw_create(&hdr_tmpl,
				 test->raw_types,
				 test->str_sec,
				 test->str_sec_size,
				 &raw_btf_size);

	if (!raw_btf)
		return -1;

	hdr = raw_btf;

	hdr->type_off = (int)hdr->type_off + test->type_off_delta;
	hdr->str_off = (int)hdr->str_off + test->str_off_delta;
	hdr->str_len = (int)hdr->str_len + test->str_len_delta;

	*btf_log_buf = '\0';
	btf_fd = bpf_load_btf(raw_btf, raw_btf_size,
			      btf_log_buf, BTF_LOG_BUF_SIZE,
			      args.always_log);
	free(raw_btf);

	err = ((btf_fd == -1) != test->btf_load_err);
	if (err)
		fprintf(stderr, "btf_load_err:%d btf_fd:%d\n",
			test->btf_load_err, btf_fd);

	if (err || btf_fd == -1)
		goto done;

	create_attr.name = test->map_name;
	create_attr.map_type = test->map_type;
	create_attr.key_size = test->key_size;
	create_attr.value_size = test->value_size;
	create_attr.max_entries = test->max_entries;
	create_attr.btf_fd = btf_fd;
	create_attr.btf_key_id = test->key_id;
	create_attr.btf_value_id = test->value_id;

	map_fd = bpf_create_map_xattr(&create_attr);

	err = ((map_fd == -1) != test->map_create_err);
	if (err)
		fprintf(stderr, "map_create_err:%d map_fd:%d\n",
			test->map_create_err, map_fd);

done:
	if (!err)
		fprintf(stderr, "OK\n");

	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "%s\n", btf_log_buf);

	if (btf_fd != -1)
		close(btf_fd);
	if (map_fd != -1)
		close(map_fd);

	return err;
}

static int test_raw(void)
{
	unsigned int i;
	int err = 0;

	if (args.raw_test_num)
		return do_test_raw(args.raw_test_num);

	for (i = 1; i <= ARRAY_SIZE(raw_tests); i++)
		err |= do_test_raw(i);

	return err;
}

struct btf_get_info_test {
	const char *descr;
	const char *str_sec;
	__u32 raw_types[MAX_NR_RAW_TYPES];
	__u32 str_sec_size;
	int info_size_delta;
};

const struct btf_get_info_test get_info_tests[] = {
{
	.descr = "== raw_btf_size+1",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.info_size_delta = 1,
},
{
	.descr = "== raw_btf_size-3",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.info_size_delta = -3,
},
};

static int do_test_get_info(unsigned int test_num)
{
	const struct btf_get_info_test *test = &get_info_tests[test_num - 1];
	unsigned int raw_btf_size, user_btf_size, expected_nbytes;
	uint8_t *raw_btf = NULL, *user_btf = NULL;
	int btf_fd = -1, err;

	fprintf(stderr, "BTF GET_INFO_BY_ID test[%u] (%s): ",
		test_num, test->descr);

	raw_btf = btf_raw_create(&hdr_tmpl,
				 test->raw_types,
				 test->str_sec,
				 test->str_sec_size,
				 &raw_btf_size);

	if (!raw_btf)
		return -1;

	*btf_log_buf = '\0';

	user_btf = malloc(raw_btf_size);
	if (!user_btf) {
		fprintf(stderr, "Cannot allocate memory for user_btf\n");
		err = -1;
		goto done;
	}

	btf_fd = bpf_load_btf(raw_btf, raw_btf_size,
			      btf_log_buf, BTF_LOG_BUF_SIZE,
			      args.always_log);
	if (btf_fd == -1) {
		fprintf(stderr, "bpf_load_btf:%s(%d)\n",
			strerror(errno), errno);
		err = -1;
		goto done;
	}

	user_btf_size = (int)raw_btf_size + test->info_size_delta;
	expected_nbytes = min(raw_btf_size, user_btf_size);
	if (raw_btf_size > expected_nbytes)
		memset(user_btf + expected_nbytes, 0xff,
		       raw_btf_size - expected_nbytes);

	err = bpf_obj_get_info_by_fd(btf_fd, user_btf, &user_btf_size);
	if (err || user_btf_size != raw_btf_size ||
	    memcmp(raw_btf, user_btf, expected_nbytes)) {
		fprintf(stderr,
			"err:%d(errno:%d) raw_btf_size:%u user_btf_size:%u expected_nbytes:%u memcmp:%d\n",
			err, errno,
			raw_btf_size, user_btf_size, expected_nbytes,
			memcmp(raw_btf, user_btf, expected_nbytes));
		err = -1;
		goto done;
	}

	while (expected_nbytes < raw_btf_size) {
		fprintf(stderr, "%u...", expected_nbytes);
		if (user_btf[expected_nbytes++] != 0xff) {
			fprintf(stderr, "!= 0xff\n");
			err = -1;
			goto done;
		}
	}

	fprintf(stderr, "OK\n");

done:
	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "%s\n", btf_log_buf);

	free(raw_btf);
	free(user_btf);

	if (btf_fd != -1)
		close(btf_fd);

	return err;
}

static int test_get_info(void)
{
	unsigned int i;
	int err = 0;

	if (args.get_info_test_num)
		return do_test_get_info(args.get_info_test_num);

	for (i = 1; i <= ARRAY_SIZE(get_info_tests); i++)
		err |= do_test_get_info(i);

	return err;
}

struct btf_file_test {
	const char *file;
	bool btf_kv_notfound;
};

static struct btf_file_test file_tests[] = {
{
	.file = "test_btf_haskv.o",
},
{
	.file = "test_btf_nokv.o",
	.btf_kv_notfound = true,
},
};

static int file_has_btf_elf(const char *fn)
{
	Elf_Scn *scn = NULL;
	GElf_Ehdr ehdr;
	int elf_fd;
	Elf *elf;
	int ret;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		fprintf(stderr, "Failed to init libelf\n");
		return -1;
	}

	elf_fd = open(fn, O_RDONLY);
	if (elf_fd == -1) {
		fprintf(stderr, "Cannot open file %s: %s(%d)\n",
			fn, strerror(errno), errno);
		return -1;
	}

	elf = elf_begin(elf_fd, ELF_C_READ, NULL);
	if (!elf) {
		fprintf(stderr, "Failed to read ELF from %s. %s\n", fn,
			elf_errmsg(elf_errno()));
		ret = -1;
		goto done;
	}

	if (!gelf_getehdr(elf, &ehdr)) {
		fprintf(stderr, "Failed to get EHDR from %s\n", fn);
		ret = -1;
		goto done;
	}

	while ((scn = elf_nextscn(elf, scn))) {
		const char *sh_name;
		GElf_Shdr sh;

		if (gelf_getshdr(scn, &sh) != &sh) {
			fprintf(stderr,
				"Failed to get section header from %s\n", fn);
			ret = -1;
			goto done;
		}

		sh_name = elf_strptr(elf, ehdr.e_shstrndx, sh.sh_name);
		if (!strcmp(sh_name, BTF_ELF_SEC)) {
			ret = 1;
			goto done;
		}
	}

	ret = 0;

done:
	close(elf_fd);
	elf_end(elf);
	return ret;
}

static int do_test_file(unsigned int test_num)
{
	const struct btf_file_test *test = &file_tests[test_num - 1];
	struct bpf_object *obj = NULL;
	struct bpf_program *prog;
	struct bpf_map *map;
	int err;

	fprintf(stderr, "BTF libbpf test[%u] (%s): ", test_num,
		test->file);

	err = file_has_btf_elf(test->file);
	if (err == -1)
		return err;

	if (err == 0) {
		fprintf(stderr, "SKIP. No ELF %s found\n", BTF_ELF_SEC);
		return 0;
	}

	obj = bpf_object__open(test->file);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = bpf_object__btf_fd(obj);
	if (err == -1) {
		fprintf(stderr, "bpf_object__btf_fd: -1\n");
		goto done;
	}

	prog = bpf_program__next(NULL, obj);
	if (!prog) {
		fprintf(stderr, "Cannot find bpf_prog\n");
		err = -1;
		goto done;
	}

	bpf_program__set_type(prog, BPF_PROG_TYPE_TRACEPOINT);
	err = bpf_object__load(obj);
	if (err < 0) {
		fprintf(stderr, "bpf_object__load: %d\n", err);
		goto done;
	}

	map = bpf_object__find_map_by_name(obj, "btf_map");
	if (!map) {
		fprintf(stderr, "btf_map not found\n");
		err = -1;
		goto done;
	}

	err = (bpf_map__btf_key_id(map) == 0 || bpf_map__btf_value_id(map) == 0)
		!= test->btf_kv_notfound;
	if (err) {
		fprintf(stderr,
			"btf_kv_notfound:%u btf_key_id:%u btf_value_id:%u\n",
			test->btf_kv_notfound,
			bpf_map__btf_key_id(map),
			bpf_map__btf_value_id(map));
		goto done;
	}

	fprintf(stderr, "OK\n");

done:
	bpf_object__close(obj);
	return err;
}

static int test_file(void)
{
	unsigned int i;
	int err = 0;

	if (args.file_test_num)
		return do_test_file(args.file_test_num);

	for (i = 1; i <= ARRAY_SIZE(file_tests); i++)
		err |= do_test_file(i);

	return err;
}

const char *pprint_enum_str[] = {
	"ENUM_ZERO",
	"ENUM_ONE",
	"ENUM_TWO",
	"ENUM_THREE",
};

struct pprint_mapv {
	uint32_t ui32;
	uint16_t ui16;
	/* 2 bytes hole */
	int32_t si32;
	uint32_t unused_bits2a:2,
		bits28:28,
		unused_bits2b:2;
	union {
		uint64_t ui64;
		uint8_t ui8a[8];
	};
	enum {
		ENUM_ZERO,
		ENUM_ONE,
		ENUM_TWO,
		ENUM_THREE,
	} aenum;
};

static struct btf_raw_test pprint_test = {
	.descr = "BTF pretty print test #1",
	.raw_types = {
		/* unsighed char */			/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 8, 1),
		/* unsigned short */			/* [2] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 16, 2),
		/* unsigned int */			/* [3] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 32, 4),
		/* int */				/* [4] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		/* unsigned long long */		/* [5] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 64, 8),
		/* 2 bits */				/* [6] */
		BTF_TYPE_INT_ENC(0, 0, 0, 2, 2),
		/* 28 bits */				/* [7] */
		BTF_TYPE_INT_ENC(0, 0, 0, 28, 4),
		/* uint8_t[8] */			/* [8] */
		BTF_TYPE_ARRAY_ENC(9, 3, 8),
		/* typedef unsigned char uint8_t */	/* [9] */
		BTF_TYPEDEF_ENC(NAME_TBD, 1),
		/* typedef unsigned short uint16_t */	/* [10] */
		BTF_TYPEDEF_ENC(NAME_TBD, 2),
		/* typedef unsigned int uint32_t */	/* [11] */
		BTF_TYPEDEF_ENC(NAME_TBD, 3),
		/* typedef int int32_t */		/* [12] */
		BTF_TYPEDEF_ENC(NAME_TBD, 4),
		/* typedef unsigned long long uint64_t *//* [13] */
		BTF_TYPEDEF_ENC(NAME_TBD, 5),
		/* union (anon) */			/* [14] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_UNION, 0, 2), 8),
		BTF_MEMBER_ENC(NAME_TBD, 13, 0),/* uint64_t ui64; */
		BTF_MEMBER_ENC(NAME_TBD, 8, 0),	/* uint8_t ui8a[8]; */
		/* enum (anon) */			/* [15] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 4), 4),
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_ENUM_ENC(NAME_TBD, 1),
		BTF_ENUM_ENC(NAME_TBD, 2),
		BTF_ENUM_ENC(NAME_TBD, 3),
		/* struct pprint_mapv */		/* [16] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 8), 28),
		BTF_MEMBER_ENC(NAME_TBD, 11, 0),	/* uint32_t ui32 */
		BTF_MEMBER_ENC(NAME_TBD, 10, 32),	/* uint16_t ui16 */
		BTF_MEMBER_ENC(NAME_TBD, 12, 64),	/* int32_t si32 */
		BTF_MEMBER_ENC(NAME_TBD, 6, 96),	/* unused_bits2a */
		BTF_MEMBER_ENC(NAME_TBD, 7, 98),	/* bits28 */
		BTF_MEMBER_ENC(NAME_TBD, 6, 126),	/* unused_bits2b */
		BTF_MEMBER_ENC(0, 14, 128),		/* union (anon) */
		BTF_MEMBER_ENC(NAME_TBD, 15, 192),	/* aenum */
		BTF_END_RAW,
	},
	.str_sec = "\0unsigned char\0unsigned short\0unsigned int\0int\0unsigned long long\0uint8_t\0uint16_t\0uint32_t\0int32_t\0uint64_t\0ui64\0ui8a\0ENUM_ZERO\0ENUM_ONE\0ENUM_TWO\0ENUM_THREE\0pprint_mapv\0ui32\0ui16\0si32\0unused_bits2a\0bits28\0unused_bits2b\0aenum",
	.str_sec_size = sizeof("\0unsigned char\0unsigned short\0unsigned int\0int\0unsigned long long\0uint8_t\0uint16_t\0uint32_t\0int32_t\0uint64_t\0ui64\0ui8a\0ENUM_ZERO\0ENUM_ONE\0ENUM_TWO\0ENUM_THREE\0pprint_mapv\0ui32\0ui16\0si32\0unused_bits2a\0bits28\0unused_bits2b\0aenum"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "pprint_test",
	.key_size = sizeof(unsigned int),
	.value_size = sizeof(struct pprint_mapv),
	.key_id = 3,	/* unsigned int */
	.value_id = 16,	/* struct pprint_mapv */
	.max_entries = 128 * 1024,
};

static void set_pprint_mapv(struct pprint_mapv *v, uint32_t i)
{
	v->ui32 = i;
	v->si32 = -i;
	v->unused_bits2a = 3;
	v->bits28 = i;
	v->unused_bits2b = 3;
	v->ui64 = i;
	v->aenum = i & 0x03;
}

static int test_pprint(void)
{
	const struct btf_raw_test *test = &pprint_test;
	struct bpf_create_map_attr create_attr = {};
	int map_fd = -1, btf_fd = -1;
	struct pprint_mapv mapv = {};
	unsigned int raw_btf_size;
	char expected_line[255];
	FILE *pin_file = NULL;
	char pin_path[255];
	size_t line_len = 0;
	char *line = NULL;
	unsigned int key;
	uint8_t *raw_btf;
	ssize_t nread;
	int err;

	fprintf(stderr, "%s......", test->descr);
	raw_btf = btf_raw_create(&hdr_tmpl, test->raw_types,
				 test->str_sec, test->str_sec_size,
				 &raw_btf_size);

	if (!raw_btf)
		return -1;

	*btf_log_buf = '\0';
	btf_fd = bpf_load_btf(raw_btf, raw_btf_size,
			      btf_log_buf, BTF_LOG_BUF_SIZE,
			      args.always_log);
	free(raw_btf);

	if (btf_fd == -1) {
		err = -1;
		fprintf(stderr, "bpf_load_btf: %s(%d)\n",
			strerror(errno), errno);
		goto done;
	}

	create_attr.name = test->map_name;
	create_attr.map_type = test->map_type;
	create_attr.key_size = test->key_size;
	create_attr.value_size = test->value_size;
	create_attr.max_entries = test->max_entries;
	create_attr.btf_fd = btf_fd;
	create_attr.btf_key_id = test->key_id;
	create_attr.btf_value_id = test->value_id;

	map_fd = bpf_create_map_xattr(&create_attr);
	if (map_fd == -1) {
		err = -1;
		fprintf(stderr, "bpf_creat_map_btf: %s(%d)\n",
			strerror(errno), errno);
		goto done;
	}

	if (snprintf(pin_path, sizeof(pin_path), "%s/%s",
		     "/sys/fs/bpf", test->map_name) == sizeof(pin_path)) {
		err = -1;
		fprintf(stderr, "pin_path is too long\n");
		goto done;
	}

	err = bpf_obj_pin(map_fd, pin_path);
	if (err) {
		fprintf(stderr, "Cannot pin to %s. %s(%d).\n", pin_path,
			strerror(errno), errno);
		goto done;
	}

	for (key = 0; key < test->max_entries; key++) {
		set_pprint_mapv(&mapv, key);
		bpf_map_update_elem(map_fd, &key, &mapv, 0);
	}

	pin_file = fopen(pin_path, "r");
	if (!pin_file) {
		err = -1;
		fprintf(stderr, "fopen(%s): %s(%d)\n", pin_path,
			strerror(errno), errno);
		goto done;
	}

	/* Skip lines start with '#' */
	while ((nread = getline(&line, &line_len, pin_file)) > 0 &&
	       *line == '#')
		;

	if (nread <= 0) {
		err = -1;
		fprintf(stderr, "Unexpected EOF\n");
		goto done;
	}

	key = 0;
	do {
		ssize_t nexpected_line;

		set_pprint_mapv(&mapv, key);
		nexpected_line = snprintf(expected_line, sizeof(expected_line),
					  "%u: {%u,0,%d,0x%x,0x%x,0x%x,{%lu|[%u,%u,%u,%u,%u,%u,%u,%u]},%s}\n",
					  key,
					  mapv.ui32, mapv.si32,
					  mapv.unused_bits2a, mapv.bits28, mapv.unused_bits2b,
					  mapv.ui64,
					  mapv.ui8a[0], mapv.ui8a[1], mapv.ui8a[2], mapv.ui8a[3],
					  mapv.ui8a[4], mapv.ui8a[5], mapv.ui8a[6], mapv.ui8a[7],
					  pprint_enum_str[mapv.aenum]);

		if (nexpected_line == sizeof(expected_line)) {
			err = -1;
			fprintf(stderr, "expected_line is too long\n");
			goto done;
		}

		if (strcmp(expected_line, line)) {
			err = -1;
			fprintf(stderr, "unexpected pprint output\n");
			fprintf(stderr, "expected: %s", expected_line);
			fprintf(stderr, "    read: %s", line);
			goto done;
		}

		nread = getline(&line, &line_len, pin_file);
	} while (++key < test->max_entries && nread > 0);

	if (key < test->max_entries) {
		err = -1;
		fprintf(stderr, "Unexpected EOF\n");
		goto done;
	}

	if (nread > 0) {
		err = -1;
		fprintf(stderr, "Unexpected extra pprint output: %s\n", line);
		goto done;
	}

	err = 0;

done:
	if (!err)
		fprintf(stderr, "OK\n");
	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "%s\n", btf_log_buf);
	if (btf_fd != -1)
		close(btf_fd);
	if (map_fd != -1)
		close(map_fd);
	if (pin_file)
		fclose(pin_file);
	unlink(pin_path);
	free(line);

	return err;
}

static void usage(const char *cmd)
{
	fprintf(stderr, "Usage: %s [-l] [[-r test_num (1 - %zu)] | [-g test_num (1 - %zu)] | [-f test_num (1 - %zu)] | [-p]]\n",
		cmd, ARRAY_SIZE(raw_tests), ARRAY_SIZE(get_info_tests),
		ARRAY_SIZE(file_tests));
}

static int parse_args(int argc, char **argv)
{
	const char *optstr = "lpf:r:g:";
	int opt;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		switch (opt) {
		case 'l':
			args.always_log = true;
			break;
		case 'f':
			args.file_test_num = atoi(optarg);
			args.file_test = true;
			break;
		case 'r':
			args.raw_test_num = atoi(optarg);
			args.raw_test = true;
			break;
		case 'g':
			args.get_info_test_num = atoi(optarg);
			args.get_info_test = true;
			break;
		case 'p':
			args.pprint_test = true;
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		default:
				usage(argv[0]);
				return -1;
		}
	}

	if (args.raw_test_num &&
	    (args.raw_test_num < 1 ||
	     args.raw_test_num > ARRAY_SIZE(raw_tests))) {
		fprintf(stderr, "BTF raw test number must be [1 - %zu]\n",
			ARRAY_SIZE(raw_tests));
		return -1;
	}

	if (args.file_test_num &&
	    (args.file_test_num < 1 ||
	     args.file_test_num > ARRAY_SIZE(file_tests))) {
		fprintf(stderr, "BTF file test number must be [1 - %zu]\n",
			ARRAY_SIZE(file_tests));
		return -1;
	}

	if (args.get_info_test_num &&
	    (args.get_info_test_num < 1 ||
	     args.get_info_test_num > ARRAY_SIZE(get_info_tests))) {
		fprintf(stderr, "BTF get info test number must be [1 - %zu]\n",
			ARRAY_SIZE(get_info_tests));
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int err = 0;

	err = parse_args(argc, argv);
	if (err)
		return err;

	if (args.always_log)
		libbpf_set_print(__base_pr, __base_pr, __base_pr);

	if (args.raw_test)
		err |= test_raw();

	if (args.get_info_test)
		err |= test_get_info();

	if (args.file_test)
		err |= test_file();

	if (args.pprint_test)
		err |= test_pprint();

	if (args.raw_test || args.get_info_test || args.file_test ||
	    args.pprint_test)
		return err;

	err |= test_raw();
	err |= test_get_info();
	err |= test_file();

	return err;
}
