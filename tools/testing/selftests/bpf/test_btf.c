/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/kernel.h>
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
#include "bpf_util.h"

static uint32_t pass_cnt;
static uint32_t error_cnt;
static uint32_t skip_cnt;

#define CHECK(condition, format...) ({					\
	int __ret = !!(condition);					\
	if (__ret) {							\
		fprintf(stderr, "%s:%d:FAIL ", __func__, __LINE__);	\
		fprintf(stderr, format);				\
	}								\
	__ret;								\
})

static int count_result(int err)
{
	if (err)
		error_cnt++;
	else
		pass_cnt++;

	fprintf(stderr, "\n");
	return err;
}

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
	.hdr_len = sizeof(struct btf_header),
};

struct btf_raw_test {
	const char *descr;
	const char *str_sec;
	const char *map_name;
	const char *err_str;
	__u32 raw_types[MAX_NR_RAW_TYPES];
	__u32 str_sec_size;
	enum bpf_map_type map_type;
	__u32 key_size;
	__u32 value_size;
	__u32 key_type_id;
	__u32 value_type_id;
	__u32 max_entries;
	bool btf_load_err;
	bool map_create_err;
	bool ordered_map;
	bool lossless_map;
	bool percpu_map;
	int hdr_len_delta;
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
 *	unsigned long long m;
 *	int n;
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
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		BTF_MEMBER_ENC(NAME_TBD, 6, 384),/* int q[4][8]		*/
		BTF_MEMBER_ENC(NAME_TBD, 7, 1408), /* enum E r		*/
		/* } */
		/* int[4][8] */
		BTF_TYPE_ARRAY_ENC(4, 1, 4),			/* [6] */
		/* enum E */					/* [7] */
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
	.key_type_id = 1,
	.value_type_id = 5,
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
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 4,
},

{
	.descr = "struct test #3 Invalid member offset",
	.raw_types = {
		/* int */					/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int64 */					/* [2] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 64, 8),

		/* struct A { */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 16),
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),	/* int m;		*/
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),		/* int64 n; */
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0",
	.str_sec_size = sizeof("\0A\0m\0n\0"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_test3_map",
	.key_size = sizeof(int),
	.value_size = 16,
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid member bits_offset",
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
		BTF_MEMBER_ENC(NAME_TBD, 1, 32),/* int n; */
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n",
	.str_sec_size = sizeof("\0A\0m\0n"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "size_check1_map",
	.key_size = sizeof(int),
	.value_size = 1,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Member exceeds struct_size",
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
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Member exceeds struct_size",
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
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Member exceeds struct_size",
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
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Member exceeds struct_size",
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
	.key_type_id = 1,
	.value_type_id = 4,
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
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid member",
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
		/* typedef const void * const_void_ptr */	/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 3),
		/* const_void_ptr[4] */	/* [5] */
		BTF_TYPE_ARRAY_ENC(3, 1, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0const_void_ptr",
	.str_sec_size = sizeof("\0const_void_ptr"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "void_test3_map",
	.key_size = sizeof(int),
	.value_size = sizeof(void *) * 4,
	.key_type_id = 1,
	.value_type_id = 5,
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
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid elem",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
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
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Loop detected",
},

{
	.descr = "string section does not end with null",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int") - 1,
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid string section",
},

{
	.descr = "empty string section",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = 0,
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid string section",
},

{
	.descr = "empty type section",
	.raw_types = {
		BTF_END_RAW,
	},
	.str_sec = "\0int",
	.str_sec_size = sizeof("\0int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "hdr_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "No type found",
},

{
	.descr = "btf_header test. Longer hdr_len",
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
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.hdr_len_delta = 4,
	.err_str = "Unsupported btf_header",
},

{
	.descr = "btf_header test. Gap between hdr and type",
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
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.type_off_delta = 4,
	.err_str = "Unsupported section found",
},

{
	.descr = "btf_header test. Gap between type and str",
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
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_off_delta = 4,
	.err_str = "Unsupported section found",
},

{
	.descr = "btf_header test. Overlap between type and str",
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
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_off_delta = -4,
	.err_str = "Section overlap found",
},

{
	.descr = "btf_header test. Larger BTF size",
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
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_len_delta = -4,
	.err_str = "Unsupported section found",
},

{
	.descr = "btf_header test. Smaller BTF size",
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
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.str_len_delta = 4,
	.err_str = "Total section length too long",
},

{
	.descr = "array test. index_type/elem_type \"int\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int[16] */				/* [2] */
		BTF_TYPE_ARRAY_ENC(1, 1, 16),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "array test. index_type/elem_type \"const int\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int[16] */				/* [2] */
		BTF_TYPE_ARRAY_ENC(3, 3, 16),
		/* CONST type_id=1 */			/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 1),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "array test. index_type \"const int:31\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int:31 */				/* [2] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 31, 4),
		/* int[16] */				/* [3] */
		BTF_TYPE_ARRAY_ENC(1, 4, 16),
		/* CONST type_id=2 */			/* [4] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 2),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid index",
},

{
	.descr = "array test. elem_type \"const int:31\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int:31 */				/* [2] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 31, 4),
		/* int[16] */				/* [3] */
		BTF_TYPE_ARRAY_ENC(4, 1, 16),
		/* CONST type_id=2 */			/* [4] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 2),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid array of int",
},

{
	.descr = "array test. index_type \"void\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int[16] */				/* [2] */
		BTF_TYPE_ARRAY_ENC(1, 0, 16),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid index",
},

{
	.descr = "array test. index_type \"const void\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int[16] */				/* [2] */
		BTF_TYPE_ARRAY_ENC(1, 3, 16),
		/* CONST type_id=0 (void) */		/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 0),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid index",
},

{
	.descr = "array test. elem_type \"const void\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int[16] */				/* [2] */
		BTF_TYPE_ARRAY_ENC(3, 1, 16),
		/* CONST type_id=0 (void) */		/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 0),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid elem",
},

{
	.descr = "array test. elem_type \"const void *\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* const void *[16] */			/* [2] */
		BTF_TYPE_ARRAY_ENC(3, 1, 16),
		/* CONST type_id=4 */			/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 4),
		/* void* */				/* [4] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 0),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "array test. index_type \"const void *\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* const void *[16] */			/* [2] */
		BTF_TYPE_ARRAY_ENC(3, 3, 16),
		/* CONST type_id=4 */			/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 4),
		/* void* */				/* [4] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 0),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid index",
},

{
	.descr = "array test. t->size != 0\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int[16] */				/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ARRAY, 0, 0), 1),
		BTF_ARRAY_ENC(1, 1, 16),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "size != 0",
},

{
	.descr = "int test. invalid int_data",
	.raw_types = {
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_INT, 0, 0), 4),
		0x10000000,
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid int_data",
},

{
	.descr = "invalid BTF_INFO",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		BTF_TYPE_ENC(0, 0x10000000, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info",
},

{
	.descr = "fwd test. t->type != 0\"",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* fwd type */				/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FWD, 0, 0), 1),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "fwd_test_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "type != 0",
},

{
	.descr = "arraymap invalid btf key (a bit field)",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* 32 bit int with 32 bit offset */	/* [2] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 32, 32, 8),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_map_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 2,
	.value_type_id = 1,
	.max_entries = 4,
	.map_create_err = true,
},

{
	.descr = "arraymap invalid btf key (!= 32 bits)",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* 16 bit int with 0 bit offset */	/* [2] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 16, 2),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_map_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 2,
	.value_type_id = 1,
	.max_entries = 4,
	.map_create_err = true,
},

{
	.descr = "arraymap invalid btf value (too small)",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_map_check_btf",
	.key_size = sizeof(int),
	/* btf_value_size < map->value_size */
	.value_size = sizeof(__u64),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.map_create_err = true,
},

{
	.descr = "arraymap invalid btf value (too big)",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_map_check_btf",
	.key_size = sizeof(int),
	/* btf_value_size > map->value_size */
	.value_size = sizeof(__u16),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.map_create_err = true,
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
	if (CHECK(type_sec_size < 0, "Cannot get nr_raw_types"))
		return NULL;

	size_needed = sizeof(*hdr) + type_sec_size + str_sec_size;
	raw_btf = malloc(size_needed);
	if (CHECK(!raw_btf, "Cannot allocate memory for raw_btf"))
		return NULL;

	/* Copy header */
	memcpy(raw_btf, hdr, sizeof(*hdr));
	offset = sizeof(*hdr);

	/* Copy type section */
	ret_types = raw_btf + offset;
	for (i = 0; i < type_sec_size / sizeof(raw_types[0]); i++) {
		if (raw_types[i] == NAME_TBD) {
			next_str = get_next_str(next_str, end_str);
			if (CHECK(!next_str, "Error in getting next_str")) {
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
	ret_hdr->type_len = type_sec_size;
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

	hdr->hdr_len = (int)hdr->hdr_len + test->hdr_len_delta;
	hdr->type_off = (int)hdr->type_off + test->type_off_delta;
	hdr->str_off = (int)hdr->str_off + test->str_off_delta;
	hdr->str_len = (int)hdr->str_len + test->str_len_delta;

	*btf_log_buf = '\0';
	btf_fd = bpf_load_btf(raw_btf, raw_btf_size,
			      btf_log_buf, BTF_LOG_BUF_SIZE,
			      args.always_log);
	free(raw_btf);

	err = ((btf_fd == -1) != test->btf_load_err);
	if (CHECK(err, "btf_fd:%d test->btf_load_err:%u",
		  btf_fd, test->btf_load_err) ||
	    CHECK(test->err_str && !strstr(btf_log_buf, test->err_str),
		  "expected err_str:%s", test->err_str)) {
		err = -1;
		goto done;
	}

	if (err || btf_fd == -1)
		goto done;

	create_attr.name = test->map_name;
	create_attr.map_type = test->map_type;
	create_attr.key_size = test->key_size;
	create_attr.value_size = test->value_size;
	create_attr.max_entries = test->max_entries;
	create_attr.btf_fd = btf_fd;
	create_attr.btf_key_type_id = test->key_type_id;
	create_attr.btf_value_type_id = test->value_type_id;

	map_fd = bpf_create_map_xattr(&create_attr);

	err = ((map_fd == -1) != test->map_create_err);
	CHECK(err, "map_fd:%d test->map_create_err:%u",
	      map_fd, test->map_create_err);

done:
	if (!err)
		fprintf(stderr, "OK");

	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

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
		return count_result(do_test_raw(args.raw_test_num));

	for (i = 1; i <= ARRAY_SIZE(raw_tests); i++)
		err |= count_result(do_test_raw(i));

	return err;
}

struct btf_get_info_test {
	const char *descr;
	const char *str_sec;
	__u32 raw_types[MAX_NR_RAW_TYPES];
	__u32 str_sec_size;
	int btf_size_delta;
	int (*special_test)(unsigned int test_num);
};

static int test_big_btf_info(unsigned int test_num);
static int test_btf_id(unsigned int test_num);

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
	.btf_size_delta = 1,
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
	.btf_size_delta = -3,
},
{
	.descr = "Large bpf_btf_info",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.special_test = test_big_btf_info,
},
{
	.descr = "BTF ID",
	.raw_types = {
		/* int */				/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* unsigned int */			/* [2] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.special_test = test_btf_id,
},
};

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64)(unsigned long)ptr;
}

static int test_big_btf_info(unsigned int test_num)
{
	const struct btf_get_info_test *test = &get_info_tests[test_num - 1];
	uint8_t *raw_btf = NULL, *user_btf = NULL;
	unsigned int raw_btf_size;
	struct {
		struct bpf_btf_info info;
		uint64_t garbage;
	} info_garbage;
	struct bpf_btf_info *info;
	int btf_fd = -1, err;
	uint32_t info_len;

	raw_btf = btf_raw_create(&hdr_tmpl,
				 test->raw_types,
				 test->str_sec,
				 test->str_sec_size,
				 &raw_btf_size);

	if (!raw_btf)
		return -1;

	*btf_log_buf = '\0';

	user_btf = malloc(raw_btf_size);
	if (CHECK(!user_btf, "!user_btf")) {
		err = -1;
		goto done;
	}

	btf_fd = bpf_load_btf(raw_btf, raw_btf_size,
			      btf_log_buf, BTF_LOG_BUF_SIZE,
			      args.always_log);
	if (CHECK(btf_fd == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	/*
	 * GET_INFO should error out if the userspace info
	 * has non zero tailing bytes.
	 */
	info = &info_garbage.info;
	memset(info, 0, sizeof(*info));
	info_garbage.garbage = 0xdeadbeef;
	info_len = sizeof(info_garbage);
	info->btf = ptr_to_u64(user_btf);
	info->btf_size = raw_btf_size;

	err = bpf_obj_get_info_by_fd(btf_fd, info, &info_len);
	if (CHECK(!err, "!err")) {
		err = -1;
		goto done;
	}

	/*
	 * GET_INFO should succeed even info_len is larger than
	 * the kernel supported as long as tailing bytes are zero.
	 * The kernel supported info len should also be returned
	 * to userspace.
	 */
	info_garbage.garbage = 0;
	err = bpf_obj_get_info_by_fd(btf_fd, info, &info_len);
	if (CHECK(err || info_len != sizeof(*info),
		  "err:%d errno:%d info_len:%u sizeof(*info):%lu",
		  err, errno, info_len, sizeof(*info))) {
		err = -1;
		goto done;
	}

	fprintf(stderr, "OK");

done:
	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

	free(raw_btf);
	free(user_btf);

	if (btf_fd != -1)
		close(btf_fd);

	return err;
}

static int test_btf_id(unsigned int test_num)
{
	const struct btf_get_info_test *test = &get_info_tests[test_num - 1];
	struct bpf_create_map_attr create_attr = {};
	uint8_t *raw_btf = NULL, *user_btf[2] = {};
	int btf_fd[2] = {-1, -1}, map_fd = -1;
	struct bpf_map_info map_info = {};
	struct bpf_btf_info info[2] = {};
	unsigned int raw_btf_size;
	uint32_t info_len;
	int err, i, ret;

	raw_btf = btf_raw_create(&hdr_tmpl,
				 test->raw_types,
				 test->str_sec,
				 test->str_sec_size,
				 &raw_btf_size);

	if (!raw_btf)
		return -1;

	*btf_log_buf = '\0';

	for (i = 0; i < 2; i++) {
		user_btf[i] = malloc(raw_btf_size);
		if (CHECK(!user_btf[i], "!user_btf[%d]", i)) {
			err = -1;
			goto done;
		}
		info[i].btf = ptr_to_u64(user_btf[i]);
		info[i].btf_size = raw_btf_size;
	}

	btf_fd[0] = bpf_load_btf(raw_btf, raw_btf_size,
				 btf_log_buf, BTF_LOG_BUF_SIZE,
				 args.always_log);
	if (CHECK(btf_fd[0] == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	/* Test BPF_OBJ_GET_INFO_BY_ID on btf_id */
	info_len = sizeof(info[0]);
	err = bpf_obj_get_info_by_fd(btf_fd[0], &info[0], &info_len);
	if (CHECK(err, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	btf_fd[1] = bpf_btf_get_fd_by_id(info[0].id);
	if (CHECK(btf_fd[1] == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	ret = 0;
	err = bpf_obj_get_info_by_fd(btf_fd[1], &info[1], &info_len);
	if (CHECK(err || info[0].id != info[1].id ||
		  info[0].btf_size != info[1].btf_size ||
		  (ret = memcmp(user_btf[0], user_btf[1], info[0].btf_size)),
		  "err:%d errno:%d id0:%u id1:%u btf_size0:%u btf_size1:%u memcmp:%d",
		  err, errno, info[0].id, info[1].id,
		  info[0].btf_size, info[1].btf_size, ret)) {
		err = -1;
		goto done;
	}

	/* Test btf members in struct bpf_map_info */
	create_attr.name = "test_btf_id";
	create_attr.map_type = BPF_MAP_TYPE_ARRAY;
	create_attr.key_size = sizeof(int);
	create_attr.value_size = sizeof(unsigned int);
	create_attr.max_entries = 4;
	create_attr.btf_fd = btf_fd[0];
	create_attr.btf_key_type_id = 1;
	create_attr.btf_value_type_id = 2;

	map_fd = bpf_create_map_xattr(&create_attr);
	if (CHECK(map_fd == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	info_len = sizeof(map_info);
	err = bpf_obj_get_info_by_fd(map_fd, &map_info, &info_len);
	if (CHECK(err || map_info.btf_id != info[0].id ||
		  map_info.btf_key_type_id != 1 || map_info.btf_value_type_id != 2,
		  "err:%d errno:%d info.id:%u btf_id:%u btf_key_type_id:%u btf_value_type_id:%u",
		  err, errno, info[0].id, map_info.btf_id, map_info.btf_key_type_id,
		  map_info.btf_value_type_id)) {
		err = -1;
		goto done;
	}

	for (i = 0; i < 2; i++) {
		close(btf_fd[i]);
		btf_fd[i] = -1;
	}

	/* Test BTF ID is removed from the kernel */
	btf_fd[0] = bpf_btf_get_fd_by_id(map_info.btf_id);
	if (CHECK(btf_fd[0] == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}
	close(btf_fd[0]);
	btf_fd[0] = -1;

	/* The map holds the last ref to BTF and its btf_id */
	close(map_fd);
	map_fd = -1;
	btf_fd[0] = bpf_btf_get_fd_by_id(map_info.btf_id);
	if (CHECK(btf_fd[0] != -1, "BTF lingers")) {
		err = -1;
		goto done;
	}

	fprintf(stderr, "OK");

done:
	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

	free(raw_btf);
	if (map_fd != -1)
		close(map_fd);
	for (i = 0; i < 2; i++) {
		free(user_btf[i]);
		if (btf_fd[i] != -1)
			close(btf_fd[i]);
	}

	return err;
}

static int do_test_get_info(unsigned int test_num)
{
	const struct btf_get_info_test *test = &get_info_tests[test_num - 1];
	unsigned int raw_btf_size, user_btf_size, expected_nbytes;
	uint8_t *raw_btf = NULL, *user_btf = NULL;
	struct bpf_btf_info info = {};
	int btf_fd = -1, err, ret;
	uint32_t info_len;

	fprintf(stderr, "BTF GET_INFO test[%u] (%s): ",
		test_num, test->descr);

	if (test->special_test)
		return test->special_test(test_num);

	raw_btf = btf_raw_create(&hdr_tmpl,
				 test->raw_types,
				 test->str_sec,
				 test->str_sec_size,
				 &raw_btf_size);

	if (!raw_btf)
		return -1;

	*btf_log_buf = '\0';

	user_btf = malloc(raw_btf_size);
	if (CHECK(!user_btf, "!user_btf")) {
		err = -1;
		goto done;
	}

	btf_fd = bpf_load_btf(raw_btf, raw_btf_size,
			      btf_log_buf, BTF_LOG_BUF_SIZE,
			      args.always_log);
	if (CHECK(btf_fd == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	user_btf_size = (int)raw_btf_size + test->btf_size_delta;
	expected_nbytes = min(raw_btf_size, user_btf_size);
	if (raw_btf_size > expected_nbytes)
		memset(user_btf + expected_nbytes, 0xff,
		       raw_btf_size - expected_nbytes);

	info_len = sizeof(info);
	info.btf = ptr_to_u64(user_btf);
	info.btf_size = user_btf_size;

	ret = 0;
	err = bpf_obj_get_info_by_fd(btf_fd, &info, &info_len);
	if (CHECK(err || !info.id || info_len != sizeof(info) ||
		  info.btf_size != raw_btf_size ||
		  (ret = memcmp(raw_btf, user_btf, expected_nbytes)),
		  "err:%d errno:%d info.id:%u info_len:%u sizeof(info):%lu raw_btf_size:%u info.btf_size:%u expected_nbytes:%u memcmp:%d",
		  err, errno, info.id, info_len, sizeof(info),
		  raw_btf_size, info.btf_size, expected_nbytes, ret)) {
		err = -1;
		goto done;
	}

	while (expected_nbytes < raw_btf_size) {
		fprintf(stderr, "%u...", expected_nbytes);
		if (CHECK(user_btf[expected_nbytes++] != 0xff,
			  "user_btf[%u]:%x != 0xff", expected_nbytes - 1,
			  user_btf[expected_nbytes - 1])) {
			err = -1;
			goto done;
		}
	}

	fprintf(stderr, "OK");

done:
	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

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
		return count_result(do_test_get_info(args.get_info_test_num));

	for (i = 1; i <= ARRAY_SIZE(get_info_tests); i++)
		err |= count_result(do_test_get_info(i));

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

	if (CHECK(elf_version(EV_CURRENT) == EV_NONE,
		  "elf_version(EV_CURRENT) == EV_NONE"))
		return -1;

	elf_fd = open(fn, O_RDONLY);
	if (CHECK(elf_fd == -1, "open(%s): errno:%d", fn, errno))
		return -1;

	elf = elf_begin(elf_fd, ELF_C_READ, NULL);
	if (CHECK(!elf, "elf_begin(%s): %s", fn, elf_errmsg(elf_errno()))) {
		ret = -1;
		goto done;
	}

	if (CHECK(!gelf_getehdr(elf, &ehdr), "!gelf_getehdr(%s)", fn)) {
		ret = -1;
		goto done;
	}

	while ((scn = elf_nextscn(elf, scn))) {
		const char *sh_name;
		GElf_Shdr sh;

		if (CHECK(gelf_getshdr(scn, &sh) != &sh,
			  "file:%s gelf_getshdr != &sh", fn)) {
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
		fprintf(stderr, "SKIP. No ELF %s found", BTF_ELF_SEC);
		skip_cnt++;
		return 0;
	}

	obj = bpf_object__open(test->file);
	if (CHECK(IS_ERR(obj), "obj: %ld", PTR_ERR(obj)))
		return PTR_ERR(obj);

	err = bpf_object__btf_fd(obj);
	if (CHECK(err == -1, "bpf_object__btf_fd: -1"))
		goto done;

	prog = bpf_program__next(NULL, obj);
	if (CHECK(!prog, "Cannot find bpf_prog")) {
		err = -1;
		goto done;
	}

	bpf_program__set_type(prog, BPF_PROG_TYPE_TRACEPOINT);
	err = bpf_object__load(obj);
	if (CHECK(err < 0, "bpf_object__load: %d", err))
		goto done;

	map = bpf_object__find_map_by_name(obj, "btf_map");
	if (CHECK(!map, "btf_map not found")) {
		err = -1;
		goto done;
	}

	err = (bpf_map__btf_key_type_id(map) == 0 || bpf_map__btf_value_type_id(map) == 0)
		!= test->btf_kv_notfound;
	if (CHECK(err, "btf_key_type_id:%u btf_value_type_id:%u test->btf_kv_notfound:%u",
		  bpf_map__btf_key_type_id(map), bpf_map__btf_value_type_id(map),
		  test->btf_kv_notfound))
		goto done;

	fprintf(stderr, "OK");

done:
	bpf_object__close(obj);
	return err;
}

static int test_file(void)
{
	unsigned int i;
	int err = 0;

	if (args.file_test_num)
		return count_result(do_test_file(args.file_test_num));

	for (i = 1; i <= ARRAY_SIZE(file_tests); i++)
		err |= count_result(do_test_file(i));

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

static struct btf_raw_test pprint_test_template = {
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
		BTF_TYPE_ARRAY_ENC(9, 1, 8),
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 8), 32),
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
	.key_size = sizeof(unsigned int),
	.value_size = sizeof(struct pprint_mapv),
	.key_type_id = 3,	/* unsigned int */
	.value_type_id = 16,	/* struct pprint_mapv */
	.max_entries = 128 * 1024,
};

static struct btf_pprint_test_meta {
	const char *descr;
	enum bpf_map_type map_type;
	const char *map_name;
	bool ordered_map;
	bool lossless_map;
	bool percpu_map;
} pprint_tests_meta[] = {
{
	.descr = "BTF pretty print array",
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "pprint_test_array",
	.ordered_map = true,
	.lossless_map = true,
	.percpu_map = false,
},

{
	.descr = "BTF pretty print hash",
	.map_type = BPF_MAP_TYPE_HASH,
	.map_name = "pprint_test_hash",
	.ordered_map = false,
	.lossless_map = true,
	.percpu_map = false,
},

{
	.descr = "BTF pretty print lru hash",
	.map_type = BPF_MAP_TYPE_LRU_HASH,
	.map_name = "pprint_test_lru_hash",
	.ordered_map = false,
	.lossless_map = false,
	.percpu_map = false,
},

{
	.descr = "BTF pretty print percpu array",
	.map_type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.map_name = "pprint_test_percpu_array",
	.ordered_map = true,
	.lossless_map = true,
	.percpu_map = true,
},

{
	.descr = "BTF pretty print percpu hash",
	.map_type = BPF_MAP_TYPE_PERCPU_HASH,
	.map_name = "pprint_test_percpu_hash",
	.ordered_map = false,
	.lossless_map = true,
	.percpu_map = true,
},

{
	.descr = "BTF pretty print lru percpu hash",
	.map_type = BPF_MAP_TYPE_LRU_PERCPU_HASH,
	.map_name = "pprint_test_lru_percpu_hash",
	.ordered_map = false,
	.lossless_map = false,
	.percpu_map = true,
},

};


static void set_pprint_mapv(struct pprint_mapv *v, uint32_t i,
			    int num_cpus, int rounded_value_size)
{
	int cpu;

	for (cpu = 0; cpu < num_cpus; cpu++) {
		v->ui32 = i + cpu;
		v->si32 = -i;
		v->unused_bits2a = 3;
		v->bits28 = i;
		v->unused_bits2b = 3;
		v->ui64 = i;
		v->aenum = i & 0x03;
		v = (void *)v + rounded_value_size;
	}
}

static int check_line(const char *expected_line, int nexpected_line,
		      int expected_line_len, const char *line)
{
	if (CHECK(nexpected_line == expected_line_len,
		  "expected_line is too long"))
		return -1;

	if (strcmp(expected_line, line)) {
		fprintf(stderr, "unexpected pprint output\n");
		fprintf(stderr, "expected: %s", expected_line);
		fprintf(stderr, "    read: %s", line);
		return -1;
	}

	return 0;
}


static int do_test_pprint(void)
{
	const struct btf_raw_test *test = &pprint_test_template;
	struct bpf_create_map_attr create_attr = {};
	bool ordered_map, lossless_map, percpu_map;
	int err, ret, num_cpus, rounded_value_size;
	struct pprint_mapv *mapv = NULL;
	unsigned int key, nr_read_elems;
	int map_fd = -1, btf_fd = -1;
	unsigned int raw_btf_size;
	char expected_line[255];
	FILE *pin_file = NULL;
	char pin_path[255];
	size_t line_len = 0;
	char *line = NULL;
	uint8_t *raw_btf;
	ssize_t nread;

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

	if (CHECK(btf_fd == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	create_attr.name = test->map_name;
	create_attr.map_type = test->map_type;
	create_attr.key_size = test->key_size;
	create_attr.value_size = test->value_size;
	create_attr.max_entries = test->max_entries;
	create_attr.btf_fd = btf_fd;
	create_attr.btf_key_type_id = test->key_type_id;
	create_attr.btf_value_type_id = test->value_type_id;

	map_fd = bpf_create_map_xattr(&create_attr);
	if (CHECK(map_fd == -1, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	ret = snprintf(pin_path, sizeof(pin_path), "%s/%s",
		       "/sys/fs/bpf", test->map_name);

	if (CHECK(ret == sizeof(pin_path), "pin_path %s/%s is too long",
		  "/sys/fs/bpf", test->map_name)) {
		err = -1;
		goto done;
	}

	err = bpf_obj_pin(map_fd, pin_path);
	if (CHECK(err, "bpf_obj_pin(%s): errno:%d.", pin_path, errno))
		goto done;

	percpu_map = test->percpu_map;
	num_cpus = percpu_map ? bpf_num_possible_cpus() : 1;
	rounded_value_size = round_up(sizeof(struct pprint_mapv), 8);
	mapv = calloc(num_cpus, rounded_value_size);
	if (CHECK(!mapv, "mapv allocation failure")) {
		err = -1;
		goto done;
	}

	for (key = 0; key < test->max_entries; key++) {
		set_pprint_mapv(mapv, key, num_cpus, rounded_value_size);
		bpf_map_update_elem(map_fd, &key, mapv, 0);
	}

	pin_file = fopen(pin_path, "r");
	if (CHECK(!pin_file, "fopen(%s): errno:%d", pin_path, errno)) {
		err = -1;
		goto done;
	}

	/* Skip lines start with '#' */
	while ((nread = getline(&line, &line_len, pin_file)) > 0 &&
	       *line == '#')
		;

	if (CHECK(nread <= 0, "Unexpected EOF")) {
		err = -1;
		goto done;
	}

	nr_read_elems = 0;
	ordered_map = test->ordered_map;
	lossless_map = test->lossless_map;
	do {
		struct pprint_mapv *cmapv;
		ssize_t nexpected_line;
		unsigned int next_key;
		int cpu;

		next_key = ordered_map ? nr_read_elems : atoi(line);
		set_pprint_mapv(mapv, next_key, num_cpus, rounded_value_size);
		cmapv = mapv;

		for (cpu = 0; cpu < num_cpus; cpu++) {
			if (percpu_map) {
				/* for percpu map, the format looks like:
				 * <key>: {
				 *	cpu0: <value_on_cpu0>
				 *	cpu1: <value_on_cpu1>
				 *	...
				 *	cpun: <value_on_cpun>
				 * }
				 *
				 * let us verify the line containing the key here.
				 */
				if (cpu == 0) {
					nexpected_line = snprintf(expected_line,
								  sizeof(expected_line),
								  "%u: {\n",
								  next_key);

					err = check_line(expected_line, nexpected_line,
							 sizeof(expected_line), line);
					if (err == -1)
						goto done;
				}

				/* read value@cpu */
				nread = getline(&line, &line_len, pin_file);
				if (nread < 0)
					break;
			}

			nexpected_line = snprintf(expected_line, sizeof(expected_line),
						  "%s%u: {%u,0,%d,0x%x,0x%x,0x%x,"
						  "{%lu|[%u,%u,%u,%u,%u,%u,%u,%u]},%s}\n",
						  percpu_map ? "\tcpu" : "",
						  percpu_map ? cpu : next_key,
						  cmapv->ui32, cmapv->si32,
						  cmapv->unused_bits2a,
						  cmapv->bits28,
						  cmapv->unused_bits2b,
						  cmapv->ui64,
						  cmapv->ui8a[0], cmapv->ui8a[1],
						  cmapv->ui8a[2], cmapv->ui8a[3],
						  cmapv->ui8a[4], cmapv->ui8a[5],
						  cmapv->ui8a[6], cmapv->ui8a[7],
						  pprint_enum_str[cmapv->aenum]);

			err = check_line(expected_line, nexpected_line,
					 sizeof(expected_line), line);
			if (err == -1)
				goto done;

			cmapv = (void *)cmapv + rounded_value_size;
		}

		if (percpu_map) {
			/* skip the last bracket for the percpu map */
			nread = getline(&line, &line_len, pin_file);
			if (nread < 0)
				break;
		}

		nread = getline(&line, &line_len, pin_file);
	} while (++nr_read_elems < test->max_entries && nread > 0);

	if (lossless_map &&
	    CHECK(nr_read_elems < test->max_entries,
		  "Unexpected EOF. nr_read_elems:%u test->max_entries:%u",
		  nr_read_elems, test->max_entries)) {
		err = -1;
		goto done;
	}

	if (CHECK(nread > 0, "Unexpected extra pprint output: %s", line)) {
		err = -1;
		goto done;
	}

	err = 0;

done:
	if (mapv)
		free(mapv);
	if (!err)
		fprintf(stderr, "OK");
	if (*btf_log_buf && (err || args.always_log))
		fprintf(stderr, "\n%s", btf_log_buf);
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

static int test_pprint(void)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(pprint_tests_meta); i++) {
		pprint_test_template.descr = pprint_tests_meta[i].descr;
		pprint_test_template.map_type = pprint_tests_meta[i].map_type;
		pprint_test_template.map_name = pprint_tests_meta[i].map_name;
		pprint_test_template.ordered_map = pprint_tests_meta[i].ordered_map;
		pprint_test_template.lossless_map = pprint_tests_meta[i].lossless_map;
		pprint_test_template.percpu_map = pprint_tests_meta[i].percpu_map;

		err |= count_result(do_test_pprint());
	}

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

static void print_summary(void)
{
	fprintf(stderr, "PASS:%u SKIP:%u FAIL:%u\n",
		pass_cnt - skip_cnt, skip_cnt, error_cnt);
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
		goto done;

	err |= test_raw();
	err |= test_get_info();
	err |= test_file();

done:
	print_summary();
	return err;
}
