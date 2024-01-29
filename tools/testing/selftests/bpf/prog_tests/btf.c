/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/filter.h>
#include <linux/unistd.h>
#include <bpf/bpf.h>
#include <libelf.h>
#include <gelf.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <bpf/libbpf.h>
#include <bpf/btf.h>

#include "bpf_util.h"
#include "../test_btf.h"
#include "test_progs.h"

#define MAX_INSNS	512
#define MAX_SUBPROGS	16

static int duration = 0;
static bool always_log;

#undef CHECK
#define CHECK(condition, format...) _CHECK(condition, "check", duration, format)

#define NAME_TBD 0xdeadb33f

#define NAME_NTH(N) (0xfffe0000 | N)
#define IS_NAME_NTH(X) ((X & 0xffff0000) == 0xfffe0000)
#define GET_NAME_NTH_IDX(X) (X & 0x0000ffff)

#define MAX_NR_RAW_U32 1024
#define BTF_LOG_BUF_SIZE 65535

static char btf_log_buf[BTF_LOG_BUF_SIZE];

static struct btf_header hdr_tmpl = {
	.magic = BTF_MAGIC,
	.version = BTF_VERSION,
	.hdr_len = sizeof(struct btf_header),
};

/* several different mapv kinds(types) supported by pprint */
enum pprint_mapv_kind_t {
	PPRINT_MAPV_KIND_BASIC = 0,
	PPRINT_MAPV_KIND_INT128,
};

struct btf_raw_test {
	const char *descr;
	const char *str_sec;
	const char *map_name;
	const char *err_str;
	__u32 raw_types[MAX_NR_RAW_U32];
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
	enum pprint_mapv_kind_t mapv_kind;
};

#define BTF_STR_SEC(str) \
	.str_sec = str, .str_sec_size = sizeof(str)

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
/*
 * struct A {
 *	unsigned long long m;
 *	int n;
 *	char o;
 *	[3 bytes hole]
 *	int p[8];
 * };
 */
{
	.descr = "global data test #1",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_test1_map",
	.key_size = sizeof(int),
	.value_size = 48,
	.key_type_id = 1,
	.value_type_id = 5,
	.max_entries = 4,
},
/*
 * struct A {
 *	unsigned long long m;
 *	int n;
 *	char o;
 *	[3 bytes hole]
 *	int p[8];
 * };
 * static struct A t; <- in .bss
 */
{
	.descr = "global data test #2",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* .bss section */				/* [7] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 48),
		BTF_VAR_SECINFO_ENC(6, 0, 48),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 48,
	.key_type_id = 0,
	.value_type_id = 7,
	.max_entries = 1,
},
{
	.descr = "global data test #3",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* static int t */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		/* .bss section */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(2, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0t\0.bss",
	.str_sec_size = sizeof("\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 3,
	.max_entries = 1,
},
{
	.descr = "global data test #4, unsupported linkage",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* static int t */
		BTF_VAR_ENC(NAME_TBD, 1, 2),			/* [2] */
		/* .bss section */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(2, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0t\0.bss",
	.str_sec_size = sizeof("\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 3,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Linkage not supported",
},
{
	.descr = "global data test #5, invalid var type",
	.raw_types = {
		/* static void t */
		BTF_VAR_ENC(NAME_TBD, 0, 0),			/* [1] */
		/* .bss section */				/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(1, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0t\0.bss",
	.str_sec_size = sizeof("\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 2,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},
{
	.descr = "global data test #6, invalid var type (fwd type)",
	.raw_types = {
		/* union A */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_FWD, 1, 0), 0), /* [1] */
		/* static union A t */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		/* .bss section */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(2, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0.bss",
	.str_sec_size = sizeof("\0A\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 2,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type",
},
{
	.descr = "global data test #7, invalid var type (fwd type)",
	.raw_types = {
		/* union A */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_FWD, 1, 0), 0), /* [1] */
		/* static union A t */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		/* .bss section */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(1, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0.bss",
	.str_sec_size = sizeof("\0A\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 2,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type",
},
{
	.descr = "global data test #8, invalid var size",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* .bss section */				/* [7] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 48),
		BTF_VAR_SECINFO_ENC(6, 0, 47),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 48,
	.key_type_id = 0,
	.value_type_id = 7,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid size",
},
{
	.descr = "global data test #9, invalid var size",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* .bss section */				/* [7] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 46),
		BTF_VAR_SECINFO_ENC(6, 0, 48),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 48,
	.key_type_id = 0,
	.value_type_id = 7,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid size",
},
{
	.descr = "global data test #10, invalid var size",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* .bss section */				/* [7] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 46),
		BTF_VAR_SECINFO_ENC(6, 0, 46),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 48,
	.key_type_id = 0,
	.value_type_id = 7,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid size",
},
{
	.descr = "global data test #11, multiple section members",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* static int u */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [7] */
		/* .bss section */				/* [8] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 2), 62),
		BTF_VAR_SECINFO_ENC(6, 10, 48),
		BTF_VAR_SECINFO_ENC(7, 58, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0u\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0u\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 62,
	.key_type_id = 0,
	.value_type_id = 8,
	.max_entries = 1,
},
{
	.descr = "global data test #12, invalid offset",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* static int u */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [7] */
		/* .bss section */				/* [8] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 2), 62),
		BTF_VAR_SECINFO_ENC(6, 10, 48),
		BTF_VAR_SECINFO_ENC(7, 60, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0u\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0u\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 62,
	.key_type_id = 0,
	.value_type_id = 8,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid offset+size",
},
{
	.descr = "global data test #13, invalid offset",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* static int u */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [7] */
		/* .bss section */				/* [8] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 2), 62),
		BTF_VAR_SECINFO_ENC(6, 10, 48),
		BTF_VAR_SECINFO_ENC(7, 12, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0u\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0u\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 62,
	.key_type_id = 0,
	.value_type_id = 8,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid offset",
},
{
	.descr = "global data test #14, invalid offset",
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 4), 48),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* unsigned long long m;*/
		BTF_MEMBER_ENC(NAME_TBD, 1, 64),/* int n;		*/
		BTF_MEMBER_ENC(NAME_TBD, 3, 96),/* char o;		*/
		BTF_MEMBER_ENC(NAME_TBD, 4, 128),/* int p[8]		*/
		/* } */
		/* static struct A t */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		/* static int u */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [7] */
		/* .bss section */				/* [8] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 2), 62),
		BTF_VAR_SECINFO_ENC(7, 58, 4),
		BTF_VAR_SECINFO_ENC(6, 10, 48),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0m\0n\0o\0p\0t\0u\0.bss",
	.str_sec_size = sizeof("\0A\0m\0n\0o\0p\0t\0u\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 62,
	.key_type_id = 0,
	.value_type_id = 8,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid offset",
},
{
	.descr = "global data test #15, not var kind",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		/* .bss section */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(1, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0.bss",
	.str_sec_size = sizeof("\0A\0t\0.bss"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 3,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Not a VAR kind member",
},
{
	.descr = "global data test #16, invalid var referencing sec",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [2] */
		BTF_VAR_ENC(NAME_TBD, 2, 0),			/* [3] */
		/* a section */					/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(3, 0, 4),
		/* a section */					/* [5] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(6, 0, 4),
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [6] */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0s\0a\0a",
	.str_sec_size = sizeof("\0A\0t\0s\0a\0a"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 4,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},
{
	.descr = "global data test #17, invalid var referencing var",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		BTF_VAR_ENC(NAME_TBD, 2, 0),			/* [3] */
		/* a section */					/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(3, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0s\0a\0a",
	.str_sec_size = sizeof("\0A\0t\0s\0a\0a"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 4,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},
{
	.descr = "global data test #18, invalid var loop",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 2, 0),			/* [2] */
		/* .bss section */				/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
		BTF_VAR_SECINFO_ENC(2, 0, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0aaa",
	.str_sec_size = sizeof("\0A\0t\0aaa"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 4,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},
{
	.descr = "global data test #19, invalid var referencing var",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 3, 0),			/* [2] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [3] */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0s\0a\0a",
	.str_sec_size = sizeof("\0A\0t\0s\0a\0a"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 4,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},
{
	.descr = "global data test #20, invalid ptr referencing var",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* PTR type_id=3	*/			/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 3),
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [3] */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0s\0a\0a",
	.str_sec_size = sizeof("\0A\0t\0s\0a\0a"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 4,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},
{
	.descr = "global data test #21, var included in struct",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* struct A { */				/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), sizeof(int) * 2),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),	/* int m; */
		BTF_MEMBER_ENC(NAME_TBD, 3, 32),/* VAR type_id=3; */
		/* } */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [3] */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0s\0a\0a",
	.str_sec_size = sizeof("\0A\0t\0s\0a\0a"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 4,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid member",
},
{
	.descr = "global data test #22, array of var",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_ARRAY_ENC(3, 1, 4),			/* [2] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [3] */
		BTF_END_RAW,
	},
	.str_sec = "\0A\0t\0s\0a\0a",
	.str_sec_size = sizeof("\0A\0t\0s\0a\0a"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 0,
	.value_type_id = 4,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid elem",
},
{
	.descr = "var after datasec, ptr followed by modifier",
	.raw_types = {
		/* .bss section */				/* [1] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 2),
			sizeof(void*)+4),
		BTF_VAR_SECINFO_ENC(4, 0, sizeof(void*)),
		BTF_VAR_SECINFO_ENC(6, sizeof(void*), 4),
		/* int */					/* [2] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),
		/* int* */					/* [3] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 2),
		BTF_VAR_ENC(NAME_TBD, 3, 0),			/* [4] */
		/* const int */					/* [5] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 2),
		BTF_VAR_ENC(NAME_TBD, 5, 0),			/* [6] */
		BTF_END_RAW,
	},
	.str_sec = "\0a\0b\0c\0",
	.str_sec_size = sizeof("\0a\0b\0c\0"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = ".bss",
	.key_size = sizeof(int),
	.value_size = sizeof(void*)+4,
	.key_type_id = 0,
	.value_type_id = 1,
	.max_entries = 1,
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

/* Test member exceeds the size of struct
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

/* Test member exceeds the size of struct
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

/* Test member unexceeds the size of struct
 *
 * enum E {
 *     E0,
 *     E1,
 * };
 *
 * struct A {
 *     char m;
 *     enum E __attribute__((packed)) n;
 * };
 */
{
	.descr = "size check test #5",
	.raw_types = {
		/* int */			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, sizeof(int)),
		/* char */			/* [2] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 8, 1),
		/* enum E { */			/* [3] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 2), 1),
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_ENUM_ENC(NAME_TBD, 1),
		/* } */
		/* struct A { */		/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 2), 2),
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),	/* char m; */
		BTF_MEMBER_ENC(NAME_TBD, 3, 8),/* enum E __attribute__((packed)) n; */
		/* } */
		BTF_END_RAW,
	},
	.str_sec = "\0E\0E0\0E1\0A\0m\0n",
	.str_sec_size = sizeof("\0E\0E0\0E1\0A\0m\0n"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "size_check5_map",
	.key_size = sizeof(int),
	.value_size = 2,
	.key_type_id = 1,
	.value_type_id = 4,
	.max_entries = 4,
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
		BTF_TYPEDEF_ENC(NAME_TBD, 3),	/* [4] */
		/* struct A { */	/* [5] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), sizeof(void *)),
		/* const_void_ptr m; */
		BTF_MEMBER_ENC(NAME_TBD, 4, 0),
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
		/* typedef const void * const_void_ptr */
		BTF_TYPEDEF_ENC(NAME_TBD, 3),	/* [4] */
		/* const_void_ptr[4] */
		BTF_TYPE_ARRAY_ENC(4, 1, 4),	/* [5] */
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
		BTF_TYPE_ENC(0, 0x20000000, 4),
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
	.descr = "typedef (invalid name, name_off = 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPEDEF_ENC(0, 1),				/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0__int",
	.str_sec_size = sizeof("\0__int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "typedef_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "typedef (invalid name, invalid identifier)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPEDEF_ENC(NAME_TBD, 1),			/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0__!int",
	.str_sec_size = sizeof("\0__!int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "typedef_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "ptr type (invalid name, name_off <> 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0__int",
	.str_sec_size = sizeof("\0__int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "ptr_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "volatile type (invalid name, name_off <> 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_VOLATILE, 0, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0__int",
	.str_sec_size = sizeof("\0__int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "volatile_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "const type (invalid name, name_off <> 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0__int",
	.str_sec_size = sizeof("\0__int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "const_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "restrict type (invalid name, name_off <> 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 1),	/* [2] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_RESTRICT, 0, 0), 2),	/* [3] */
		BTF_END_RAW,
	},
	.str_sec = "\0__int",
	.str_sec_size = sizeof("\0__int"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "restrict_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "fwd type (invalid name, name_off = 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FWD, 0, 0), 0),	/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0__skb",
	.str_sec_size = sizeof("\0__skb"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "fwd_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "fwd type (invalid name, invalid identifier)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_FWD, 0, 0), 0),	/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0__!skb",
	.str_sec_size = sizeof("\0__!skb"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "fwd_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "array type (invalid name, name_off <> 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_ARRAY, 0, 0), 0),	/* [2] */
		BTF_ARRAY_ENC(1, 1, 4),
		BTF_END_RAW,
	},
	.str_sec = "\0__skb",
	.str_sec_size = sizeof("\0__skb"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "struct type (name_off = 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0,
			     BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 4),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0A",
	.str_sec_size = sizeof("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "struct type (invalid name, invalid identifier)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 4),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0A!\0B",
	.str_sec_size = sizeof("\0A!\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "struct member (name_off = 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0,
			     BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 4),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0A",
	.str_sec_size = sizeof("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "struct member (invalid name, invalid identifier)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 4),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0B*",
	.str_sec_size = sizeof("\0A\0B*"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "enum type (name_off = 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0,
			     BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1),
			     sizeof(int)),				/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0A\0B",
	.str_sec_size = sizeof("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "enum_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "enum type (invalid name, invalid identifier)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1),
			     sizeof(int)),				/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0A!\0B",
	.str_sec_size = sizeof("\0A!\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "enum_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "enum member (invalid name, name_off = 0)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0,
			     BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1),
			     sizeof(int)),				/* [2] */
		BTF_ENUM_ENC(0, 0),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "enum_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "enum member (invalid name, invalid identifier)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0,
			     BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1),
			     sizeof(int)),				/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0A!",
	.str_sec_size = sizeof("\0A!"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "enum_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
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

{
	.descr = "func proto (int (*)(int, unsigned int))",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* int (*)(int, unsigned int) */
		BTF_FUNC_PROTO_ENC(1, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(0, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 2),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "func proto (vararg)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int, unsigned int, ...) */
		BTF_FUNC_PROTO_ENC(0, 3),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(0, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 2),
			BTF_FUNC_PROTO_ARG_ENC(0, 0),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "func proto (vararg with name)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int b, ... c) */
		BTF_FUNC_PROTO_ENC(0, 3),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 0),
		BTF_END_RAW,
	},
	.str_sec = "\0a\0b\0c",
	.str_sec_size = sizeof("\0a\0b\0c"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid arg#3",
},

{
	.descr = "func proto (arg after vararg)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, ..., unsigned int b) */
		BTF_FUNC_PROTO_ENC(0, 3),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 0),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		BTF_END_RAW,
	},
	.str_sec = "\0a\0b",
	.str_sec_size = sizeof("\0a\0b"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid arg#2",
},

{
	.descr = "func proto (CONST=>TYPEDEF=>PTR=>FUNC_PROTO)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* typedef void (*func_ptr)(int, unsigned int) */
		BTF_TYPEDEF_ENC(NAME_TBD, 5),			/* [3] */
		/* const func_ptr */
		BTF_CONST_ENC(3),				/* [4] */
		BTF_PTR_ENC(6),					/* [5] */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [6] */
			BTF_FUNC_PROTO_ARG_ENC(0, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 2),
		BTF_END_RAW,
	},
	.str_sec = "\0func_ptr",
	.str_sec_size = sizeof("\0func_ptr"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "func proto (TYPEDEF=>FUNC_PROTO)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		BTF_TYPEDEF_ENC(NAME_TBD, 4),			/* [3] */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [4] */
			BTF_FUNC_PROTO_ARG_ENC(0, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 2),
		BTF_END_RAW,
	},
	.str_sec = "\0func_typedef",
	.str_sec_size = sizeof("\0func_typedef"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "func proto (btf_resolve(arg))",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* void (*)(const void *) */
		BTF_FUNC_PROTO_ENC(0, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(0, 3),
		BTF_CONST_ENC(4),				/* [3] */
		BTF_PTR_ENC(0),					/* [4] */
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "func proto (Not all arg has name)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int, unsigned int b) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(0, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		BTF_END_RAW,
	},
	.str_sec = "\0b",
	.str_sec_size = sizeof("\0b"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "func proto (Bad arg name_off)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int <bad_name_off>) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(0x0fffffff, 2),
		BTF_END_RAW,
	},
	.str_sec = "\0a",
	.str_sec_size = sizeof("\0a"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid arg#2",
},

{
	.descr = "func proto (Bad arg name)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int !!!) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		BTF_END_RAW,
	},
	.str_sec = "\0a\0!!!",
	.str_sec_size = sizeof("\0a\0!!!"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid arg#2",
},

{
	.descr = "func proto (Invalid return type)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* <bad_ret_type> (*)(int, unsigned int) */
		BTF_FUNC_PROTO_ENC(100, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(0, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 2),
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid return type",
},

{
	.descr = "func proto (with func name)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void func_proto(int, unsigned int) */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 2), 0),	/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(0, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 2),
		BTF_END_RAW,
	},
	.str_sec = "\0func_proto",
	.str_sec_size = sizeof("\0func_proto"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "func proto (const void arg)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(const void) */
		BTF_FUNC_PROTO_ENC(0, 1),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(0, 4),
		BTF_CONST_ENC(0),				/* [4] */
		BTF_END_RAW,
	},
	.str_sec = "",
	.str_sec_size = sizeof(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid arg#1",
},

{
	.descr = "func (void func(int a, unsigned int b))",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int b) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		/* void func(int a, unsigned int b) */
		BTF_FUNC_ENC(NAME_TBD, 3),			/* [4] */
		BTF_END_RAW,
	},
	.str_sec = "\0a\0b\0func",
	.str_sec_size = sizeof("\0a\0b\0func"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "func (No func name)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int b) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		/* void <no_name>(int a, unsigned int b) */
		BTF_FUNC_ENC(0, 3),				/* [4] */
		BTF_END_RAW,
	},
	.str_sec = "\0a\0b",
	.str_sec_size = sizeof("\0a\0b"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "func (Invalid func name)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int b) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		/* void !!!(int a, unsigned int b) */
		BTF_FUNC_ENC(NAME_TBD, 3),			/* [4] */
		BTF_END_RAW,
	},
	.str_sec = "\0a\0b\0!!!",
	.str_sec_size = sizeof("\0a\0b\0!!!"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid name",
},

{
	.descr = "func (Some arg has no name)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(0, 2),
		/* void func(int a, unsigned int) */
		BTF_FUNC_ENC(NAME_TBD, 3),			/* [4] */
		BTF_END_RAW,
	},
	.str_sec = "\0a\0func",
	.str_sec_size = sizeof("\0a\0func"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid arg#2",
},

{
	.descr = "func (Non zero vlen)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),		/* [2] */
		/* void (*)(int a, unsigned int b) */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		/* void func(int a, unsigned int b) */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_FUNC, 0, 2), 3), 	/* [4] */
		BTF_END_RAW,
	},
	.str_sec = "\0a\0b\0func",
	.str_sec_size = sizeof("\0a\0b\0func"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid func linkage",
},

{
	.descr = "func (Not referring to FUNC_PROTO)",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_ENC(NAME_TBD, 1),			/* [2] */
		BTF_END_RAW,
	},
	.str_sec = "\0func",
	.str_sec_size = sizeof("\0func"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},

{
	.descr = "invalid int kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_INT, 1, 0), 4),	/* [2] */
		BTF_INT_ENC(0, 0, 32),
		BTF_END_RAW,
	},
	BTF_STR_SEC(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "int_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "invalid ptr kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 1, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "ptr_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "invalid array kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ARRAY, 1, 0), 0),	/* [2] */
		BTF_ARRAY_ENC(1, 1, 1),
		BTF_END_RAW,
	},
	BTF_STR_SEC(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "array_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "valid fwd kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_FWD, 1, 0), 0),	/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "fwd_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "invalid typedef kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(NAME_TBD,
			     BTF_INFO_ENC(BTF_KIND_TYPEDEF, 1, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "typedef_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "invalid volatile kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_VOLATILE, 1, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "volatile_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "invalid const kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 1, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "const_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "invalid restrict kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_RESTRICT, 1, 0), 1),	/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "restrict_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "invalid func kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 0), 0),	/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_FUNC, 1, 0), 2),	/* [3] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "invalid func_proto kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 1, 0), 0),	/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC(""),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "func_proto_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},

{
	.descr = "valid struct, kind_flag, bitfield_size = 0",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 8),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(0, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(0, 32)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "valid struct, kind_flag, int member, bitfield_size != 0",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 4),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(4, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(4, 4)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "valid union, kind_flag, int member, bitfield_size != 0",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_UNION, 1, 2), 4),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(4, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(4, 0)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "union_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "valid struct, kind_flag, enum member, bitfield_size != 0",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),	/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 4),/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(4, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(4, 4)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B\0C"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "valid union, kind_flag, enum member, bitfield_size != 0",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),	/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_UNION, 1, 2), 4),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(4, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(4, 0)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B\0C"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "union_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "valid struct, kind_flag, typedef member, bitfield_size != 0",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),	/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 4),/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 4, BTF_MEMBER_OFFSET(4, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 5, BTF_MEMBER_OFFSET(4, 4)),
		BTF_TYPEDEF_ENC(NAME_TBD, 1),				/* [4] */
		BTF_TYPEDEF_ENC(NAME_TBD, 2),				/* [5] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B\0C\0D\0E"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "valid union, kind_flag, typedef member, bitfield_size != 0",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),	/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_UNION, 1, 2), 4),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 4, BTF_MEMBER_OFFSET(4, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 5, BTF_MEMBER_OFFSET(4, 0)),
		BTF_TYPEDEF_ENC(NAME_TBD, 1),				/* [4] */
		BTF_TYPEDEF_ENC(NAME_TBD, 2),				/* [5] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B\0C\0D\0E"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "union_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "invalid struct, kind_flag, bitfield_size greater than struct size",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 4),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(20, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(20, 20)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Member exceeds struct_size",
},

{
	.descr = "invalid struct, kind_flag, bitfield base_type int not regular",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 20, 4),			/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 4),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(20, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(20, 20)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid member base type",
},

{
	.descr = "invalid struct, kind_flag, base_type int not regular",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 12, 4),			/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 4),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(8, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(8, 8)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid member base type",
},

{
	.descr = "invalid union, kind_flag, bitfield_size greater than struct size",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),		/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_UNION, 1, 2), 2),	/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(8, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 1, BTF_MEMBER_OFFSET(20, 0)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "union_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Member exceeds struct_size",
},

{
	.descr = "invalid struct, kind_flag, int member, bitfield_size = 0, wrong byte alignment",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 12),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(0, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(0, 36)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid member offset",
},

{
	.descr = "invalid struct, kind_flag, enum member, bitfield_size = 0, wrong byte alignment",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),	/* [2] */
		BTF_ENUM_ENC(NAME_TBD, 0),
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 2), 12),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(0, 0)),
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(0, 36)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A\0B\0C"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
	.btf_load_err = true,
	.err_str = "Invalid member offset",
},

{
	.descr = "128-bit int",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 128, 16),		/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "int_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "struct, 128-bit int member",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 128, 16),		/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 16),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "struct, 120-bit int member bitfield",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 120, 16),		/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 1), 16),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "struct, kind_flag, 128-bit int member",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 128, 16),		/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 1), 16),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(0, 0)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},

{
	.descr = "struct, kind_flag, 120-bit int member bitfield",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 128, 16),		/* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 1), 16),	/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(120, 0)),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0A"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "struct_type_check_btf",
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 4,
},
/*
 * typedef int arr_t[16];
 * struct s {
 *	arr_t *a;
 * };
 */
{
	.descr = "struct->ptr->typedef->array->int size resolution",
	.raw_types = {
		BTF_STRUCT_ENC(NAME_TBD, 1, 8),			/* [1] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		BTF_PTR_ENC(3),					/* [2] */
		BTF_TYPEDEF_ENC(NAME_TBD, 4),			/* [3] */
		BTF_TYPE_ARRAY_ENC(5, 5, 16),			/* [4] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [5] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0s\0a\0arr_t"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "ptr_mod_chain_size_resolve_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int) * 16,
	.key_type_id = 5 /* int */,
	.value_type_id = 3 /* arr_t */,
	.max_entries = 4,
},
/*
 * typedef int arr_t[16][8][4];
 * struct s {
 *	arr_t *a;
 * };
 */
{
	.descr = "struct->ptr->typedef->multi-array->int size resolution",
	.raw_types = {
		BTF_STRUCT_ENC(NAME_TBD, 1, 8),			/* [1] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		BTF_PTR_ENC(3),					/* [2] */
		BTF_TYPEDEF_ENC(NAME_TBD, 4),			/* [3] */
		BTF_TYPE_ARRAY_ENC(5, 7, 16),			/* [4] */
		BTF_TYPE_ARRAY_ENC(6, 7, 8),			/* [5] */
		BTF_TYPE_ARRAY_ENC(7, 7, 4),			/* [6] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [7] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0s\0a\0arr_t"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "multi_arr_size_resolve_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int) * 16 * 8 * 4,
	.key_type_id = 7 /* int */,
	.value_type_id = 3 /* arr_t */,
	.max_entries = 4,
},
/*
 * typedef int int_t;
 * typedef int_t arr3_t[4];
 * typedef arr3_t arr2_t[8];
 * typedef arr2_t arr1_t[16];
 * struct s {
 *	arr1_t *a;
 * };
 */
{
	.descr = "typedef/multi-arr mix size resolution",
	.raw_types = {
		BTF_STRUCT_ENC(NAME_TBD, 1, 8),			/* [1] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		BTF_PTR_ENC(3),					/* [2] */
		BTF_TYPEDEF_ENC(NAME_TBD, 4),			/* [3] */
		BTF_TYPE_ARRAY_ENC(5, 10, 16),			/* [4] */
		BTF_TYPEDEF_ENC(NAME_TBD, 6),			/* [5] */
		BTF_TYPE_ARRAY_ENC(7, 10, 8),			/* [6] */
		BTF_TYPEDEF_ENC(NAME_TBD, 8),			/* [7] */
		BTF_TYPE_ARRAY_ENC(9, 10, 4),			/* [8] */
		BTF_TYPEDEF_ENC(NAME_TBD, 10),			/* [9] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [10] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0s\0a\0arr1_t\0arr2_t\0arr3_t\0int_t"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "typedef_arra_mix_size_resolve_map",
	.key_size = sizeof(int),
	.value_size = sizeof(int) * 16 * 8 * 4,
	.key_type_id = 10 /* int */,
	.value_type_id = 3 /* arr_t */,
	.max_entries = 4,
},
/*
 * elf .rodata section size 4 and btf .rodata section vlen 0.
 */
{
	.descr = "datasec: vlen == 0",
	.raw_types = {
		/* int */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		/* .rodata section */
		BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 0), 4),
								 /* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0.rodata"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(int),
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
},

{
	.descr = "float test #1, well-formed",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
								/* [1] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 2),		/* [2] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 4),		/* [3] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 8),		/* [4] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 12),		/* [5] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 16),		/* [6] */
		BTF_STRUCT_ENC(NAME_TBD, 5, 48),		/* [7] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		BTF_MEMBER_ENC(NAME_TBD, 3, 32),
		BTF_MEMBER_ENC(NAME_TBD, 4, 64),
		BTF_MEMBER_ENC(NAME_TBD, 5, 128),
		BTF_MEMBER_ENC(NAME_TBD, 6, 256),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0_Float16\0float\0double\0_Float80\0long_double"
		    "\0floats\0a\0b\0c\0d\0e"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "float_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 48,
	.key_type_id = 1,
	.value_type_id = 7,
	.max_entries = 1,
},
{
	.descr = "float test #2, invalid vlen",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
								/* [1] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_FLOAT, 0, 1), 4),
								/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0float"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "float_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "vlen != 0",
},
{
	.descr = "float test #3, invalid kind_flag",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
								/* [1] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_FLOAT, 1, 0), 4),
								/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0float"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "float_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},
{
	.descr = "float test #4, member does not fit",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
								/* [1] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 4),		/* [2] */
		BTF_STRUCT_ENC(NAME_TBD, 1, 2),			/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 0),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0float\0floats\0x"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "float_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Member exceeds struct_size",
},
{
	.descr = "float test #5, member is not properly aligned",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
								/* [1] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 4),		/* [2] */
		BTF_STRUCT_ENC(NAME_TBD, 1, 8),			/* [3] */
		BTF_MEMBER_ENC(NAME_TBD, 2, 8),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0float\0floats\0x"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "float_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Member is not properly aligned",
},
{
	.descr = "float test #6, invalid size",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),
								/* [1] */
		BTF_TYPE_FLOAT_ENC(NAME_TBD, 6),		/* [2] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0float"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "float_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 6,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_size",
},

{
	.descr = "decl_tag test #1, struct/member, well-formed",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_STRUCT_ENC(0, 2, 8),			/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_MEMBER_ENC(NAME_TBD, 1, 32),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, -1),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, 0),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, 1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0m1\0m2\0tag1\0tag2\0tag3"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 8,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
},
{
	.descr = "decl_tag test #2, union/member, well-formed",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_UNION_ENC(NAME_TBD, 2, 4),			/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, -1),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, 0),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, 1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0t\0m1\0m2\0tag1\0tag2\0tag3"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
},
{
	.descr = "decl_tag test #3, variable, well-formed",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		BTF_VAR_ENC(NAME_TBD, 1, 1),			/* [3] */
		BTF_DECL_TAG_ENC(NAME_TBD, 2, -1),
		BTF_DECL_TAG_ENC(NAME_TBD, 3, -1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0global\0tag1\0tag2"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
},
{
	.descr = "decl_tag test #4, func/parameter, well-formed",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_DECL_TAG_ENC(NAME_TBD, 3, -1),
		BTF_DECL_TAG_ENC(NAME_TBD, 3, 0),
		BTF_DECL_TAG_ENC(NAME_TBD, 3, 1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0arg1\0arg2\0f\0tag1\0tag2\0tag3"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
},
{
	.descr = "decl_tag test #5, invalid value",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		BTF_DECL_TAG_ENC(0, 2, -1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid value",
},
{
	.descr = "decl_tag test #6, invalid target type",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_DECL_TAG_ENC(NAME_TBD, 1, -1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag1"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type",
},
{
	.descr = "decl_tag test #7, invalid vlen",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DECL_TAG, 0, 1), 2), (0),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0tag1"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "vlen != 0",
},
{
	.descr = "decl_tag test #8, invalid kflag",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DECL_TAG, 1, 0), 2), (-1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0tag1"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid btf_info kind_flag",
},
{
	.descr = "decl_tag test #9, var, invalid component_idx",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),			/* [2] */
		BTF_DECL_TAG_ENC(NAME_TBD, 2, 0),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid component_idx",
},
{
	.descr = "decl_tag test #10, struct member, invalid component_idx",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_STRUCT_ENC(0, 2, 8),			/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_MEMBER_ENC(NAME_TBD, 1, 32),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, 2),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0m1\0m2\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 8,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid component_idx",
},
{
	.descr = "decl_tag test #11, func parameter, invalid component_idx",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_DECL_TAG_ENC(NAME_TBD, 3, 2),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0arg1\0arg2\0f\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid component_idx",
},
{
	.descr = "decl_tag test #12, < -1 component_idx",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(0, 2),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_DECL_TAG_ENC(NAME_TBD, 3, -2),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0arg1\0arg2\0f\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid component_idx",
},
{
	.descr = "decl_tag test #13, typedef, well-formed",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPEDEF_ENC(NAME_TBD, 1),			/* [2] */
		BTF_DECL_TAG_ENC(NAME_TBD, 2, -1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0t\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
},
{
	.descr = "decl_tag test #14, typedef, invalid component_idx",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPEDEF_ENC(NAME_TBD, 1),			/* [2] */
		BTF_DECL_TAG_ENC(NAME_TBD, 2, 0),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid component_idx",
},
{
	.descr = "decl_tag test #15, func, invalid func proto",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_DECL_TAG_ENC(NAME_TBD, 3, 0),		/* [2] */
		BTF_FUNC_ENC(NAME_TBD, 8),			/* [3] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag\0func"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Invalid type_id",
},
{
	.descr = "decl_tag test #16, func proto, return type",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),				/* [1] */
		BTF_VAR_ENC(NAME_TBD, 1, 0),						/* [2] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DECL_TAG, 0, 0), 2), (-1),	/* [3] */
		BTF_FUNC_PROTO_ENC(3, 0),						/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0tag1"),
	.btf_load_err = true,
	.err_str = "Invalid return type",
},
{
	.descr = "decl_tag test #17, func proto, argument",
	.raw_types = {
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_DECL_TAG, 0, 0), 4), (-1),	/* [1] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_PTR, 0, 0), 0), /* [2] */
		BTF_FUNC_PROTO_ENC(0, 1),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_VAR_ENC(NAME_TBD, 2, 0),			/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0local\0tag1\0var"),
	.btf_load_err = true,
	.err_str = "Invalid arg#1",
},
{
	.descr = "decl_tag test #18, decl_tag as the map key type",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_STRUCT_ENC(0, 2, 8),			/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_MEMBER_ENC(NAME_TBD, 1, 32),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, -1),		/* [3] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0m1\0m2\0tag"),
	.map_type = BPF_MAP_TYPE_HASH,
	.map_name = "tag_type_check_btf",
	.key_size = 8,
	.value_size = 4,
	.key_type_id = 3,
	.value_type_id = 1,
	.max_entries = 1,
	.map_create_err = true,
},
{
	.descr = "decl_tag test #19, decl_tag as the map value type",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_STRUCT_ENC(0, 2, 8),			/* [2] */
		BTF_MEMBER_ENC(NAME_TBD, 1, 0),
		BTF_MEMBER_ENC(NAME_TBD, 1, 32),
		BTF_DECL_TAG_ENC(NAME_TBD, 2, -1),		/* [3] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0m1\0m2\0tag"),
	.map_type = BPF_MAP_TYPE_HASH,
	.map_name = "tag_type_check_btf",
	.key_size = 4,
	.value_size = 8,
	.key_type_id = 1,
	.value_type_id = 3,
	.max_entries = 1,
	.map_create_err = true,
},
{
	.descr = "type_tag test #1",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 1),			/* [2] */
		BTF_PTR_ENC(2),					/* [3] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
},
{
	.descr = "type_tag test #2, type tag order",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_CONST_ENC(3),				/* [2] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 1),			/* [3] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Type tags don't precede modifiers",
},
{
	.descr = "type_tag test #3, type tag order",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 3),			/* [2] */
		BTF_CONST_ENC(4),				/* [3] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 1),			/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Type tags don't precede modifiers",
},
{
	.descr = "type_tag test #4, type tag order",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPEDEF_ENC(NAME_TBD, 3),			/* [2] */
		BTF_CONST_ENC(4),				/* [3] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 1),			/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Type tags don't precede modifiers",
},
{
	.descr = "type_tag test #5, type tag order",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 3),			/* [2] */
		BTF_CONST_ENC(1),				/* [3] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 2),			/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
},
{
	.descr = "type_tag test #6, type tag order",
	.raw_types = {
		BTF_PTR_ENC(2),					/* [1] */
		BTF_TYPE_TAG_ENC(NAME_TBD, 3),			/* [2] */
		BTF_CONST_ENC(4),				/* [3] */
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),  /* [4] */
		BTF_PTR_ENC(6),					/* [5] */
		BTF_CONST_ENC(2),				/* [6] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0tag"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 1,
	.max_entries = 1,
	.btf_load_err = true,
	.err_str = "Type tags don't precede modifiers",
},
{
	.descr = "enum64 test #1, unsigned, size 8",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 2), 8),	/* [2] */
		BTF_ENUM64_ENC(NAME_TBD, 0, 0),
		BTF_ENUM64_ENC(NAME_TBD, 1, 1),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0a\0b\0c"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 8,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
},
{
	.descr = "enum64 test #2, signed, size 4",
	.raw_types = {
		BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),			/* [1] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM64, 1, 2), 4),	/* [2] */
		BTF_ENUM64_ENC(NAME_TBD, -1, 0),
		BTF_ENUM64_ENC(NAME_TBD, 1, 0),
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0a\0b\0c"),
	.map_type = BPF_MAP_TYPE_ARRAY,
	.map_name = "tag_type_check_btf",
	.key_size = sizeof(int),
	.value_size = 4,
	.key_type_id = 1,
	.value_type_id = 2,
	.max_entries = 1,
},

}; /* struct btf_raw_test raw_tests[] */

static const char *get_next_str(const char *start, const char *end)
{
	return start < end - 1 ? start + 1 : NULL;
}

static int get_raw_sec_size(const __u32 *raw_types)
{
	int i;

	for (i = MAX_NR_RAW_U32 - 1;
	     i >= 0 && raw_types[i] != BTF_END_RAW;
	     i--)
		;

	return i < 0 ? i : i * sizeof(raw_types[0]);
}

static void *btf_raw_create(const struct btf_header *hdr,
			    const __u32 *raw_types,
			    const char *str,
			    unsigned int str_sec_size,
			    unsigned int *btf_size,
			    const char **ret_next_str)
{
	const char *next_str = str, *end_str = str + str_sec_size;
	const char **strs_idx = NULL, **tmp_strs_idx;
	int strs_cap = 0, strs_cnt = 0, next_str_idx = 0;
	unsigned int size_needed, offset;
	struct btf_header *ret_hdr;
	int i, type_sec_size, err = 0;
	uint32_t *ret_types;
	void *raw_btf = NULL;

	type_sec_size = get_raw_sec_size(raw_types);
	if (CHECK(type_sec_size < 0, "Cannot get nr_raw_types"))
		return NULL;

	size_needed = sizeof(*hdr) + type_sec_size + str_sec_size;
	raw_btf = malloc(size_needed);
	if (CHECK(!raw_btf, "Cannot allocate memory for raw_btf"))
		return NULL;

	/* Copy header */
	memcpy(raw_btf, hdr, sizeof(*hdr));
	offset = sizeof(*hdr);

	/* Index strings */
	while ((next_str = get_next_str(next_str, end_str))) {
		if (strs_cnt == strs_cap) {
			strs_cap += max(16, strs_cap / 2);
			tmp_strs_idx = realloc(strs_idx,
					       sizeof(*strs_idx) * strs_cap);
			if (CHECK(!tmp_strs_idx,
				  "Cannot allocate memory for strs_idx")) {
				err = -1;
				goto done;
			}
			strs_idx = tmp_strs_idx;
		}
		strs_idx[strs_cnt++] = next_str;
		next_str += strlen(next_str);
	}

	/* Copy type section */
	ret_types = raw_btf + offset;
	for (i = 0; i < type_sec_size / sizeof(raw_types[0]); i++) {
		if (raw_types[i] == NAME_TBD) {
			if (CHECK(next_str_idx == strs_cnt,
				  "Error in getting next_str #%d",
				  next_str_idx)) {
				err = -1;
				goto done;
			}
			ret_types[i] = strs_idx[next_str_idx++] - str;
		} else if (IS_NAME_NTH(raw_types[i])) {
			int idx = GET_NAME_NTH_IDX(raw_types[i]);

			if (CHECK(idx <= 0 || idx > strs_cnt,
				  "Error getting string #%d, strs_cnt:%d",
				  idx, strs_cnt)) {
				err = -1;
				goto done;
			}
			ret_types[i] = strs_idx[idx-1] - str;
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
	if (ret_next_str)
		*ret_next_str =
			next_str_idx < strs_cnt ? strs_idx[next_str_idx] : NULL;

done:
	free(strs_idx);
	if (err) {
		free(raw_btf);
		return NULL;
	}
	return raw_btf;
}

static int load_raw_btf(const void *raw_data, size_t raw_size)
{
	LIBBPF_OPTS(bpf_btf_load_opts, opts);
	int btf_fd;

	if (always_log) {
		opts.log_buf = btf_log_buf,
		opts.log_size = BTF_LOG_BUF_SIZE,
		opts.log_level = 1;
	}

	btf_fd = bpf_btf_load(raw_data, raw_size, &opts);
	if (btf_fd < 0 && !always_log) {
		opts.log_buf = btf_log_buf,
		opts.log_size = BTF_LOG_BUF_SIZE,
		opts.log_level = 1;
		btf_fd = bpf_btf_load(raw_data, raw_size, &opts);
	}

	return btf_fd;
}

static void do_test_raw(unsigned int test_num)
{
	struct btf_raw_test *test = &raw_tests[test_num - 1];
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	int map_fd = -1, btf_fd = -1;
	unsigned int raw_btf_size;
	struct btf_header *hdr;
	void *raw_btf;
	int err;

	if (!test__start_subtest(test->descr))
		return;

	raw_btf = btf_raw_create(&hdr_tmpl,
				 test->raw_types,
				 test->str_sec,
				 test->str_sec_size,
				 &raw_btf_size, NULL);
	if (!raw_btf)
		return;

	hdr = raw_btf;

	hdr->hdr_len = (int)hdr->hdr_len + test->hdr_len_delta;
	hdr->type_off = (int)hdr->type_off + test->type_off_delta;
	hdr->str_off = (int)hdr->str_off + test->str_off_delta;
	hdr->str_len = (int)hdr->str_len + test->str_len_delta;

	*btf_log_buf = '\0';
	btf_fd = load_raw_btf(raw_btf, raw_btf_size);
	free(raw_btf);

	err = ((btf_fd < 0) != test->btf_load_err);
	if (CHECK(err, "btf_fd:%d test->btf_load_err:%u",
		  btf_fd, test->btf_load_err) ||
	    CHECK(test->err_str && !strstr(btf_log_buf, test->err_str),
		  "expected err_str:%s\n", test->err_str)) {
		err = -1;
		goto done;
	}

	if (err || btf_fd < 0)
		goto done;

	opts.btf_fd = btf_fd;
	opts.btf_key_type_id = test->key_type_id;
	opts.btf_value_type_id = test->value_type_id;
	map_fd = bpf_map_create(test->map_type, test->map_name,
				test->key_size, test->value_size, test->max_entries, &opts);

	err = ((map_fd < 0) != test->map_create_err);
	CHECK(err, "map_fd:%d test->map_create_err:%u",
	      map_fd, test->map_create_err);

done:
	if (*btf_log_buf && (err || always_log))
		fprintf(stderr, "\n%s", btf_log_buf);
	if (btf_fd >= 0)
		close(btf_fd);
	if (map_fd >= 0)
		close(map_fd);
}

struct btf_get_info_test {
	const char *descr;
	const char *str_sec;
	__u32 raw_types[MAX_NR_RAW_U32];
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
				 &raw_btf_size, NULL);

	if (!raw_btf)
		return -1;

	*btf_log_buf = '\0';

	user_btf = malloc(raw_btf_size);
	if (CHECK(!user_btf, "!user_btf")) {
		err = -1;
		goto done;
	}

	btf_fd = load_raw_btf(raw_btf, raw_btf_size);
	if (CHECK(btf_fd < 0, "errno:%d", errno)) {
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

	err = bpf_btf_get_info_by_fd(btf_fd, info, &info_len);
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
	err = bpf_btf_get_info_by_fd(btf_fd, info, &info_len);
	if (CHECK(err || info_len != sizeof(*info),
		  "err:%d errno:%d info_len:%u sizeof(*info):%zu",
		  err, errno, info_len, sizeof(*info))) {
		err = -1;
		goto done;
	}

	fprintf(stderr, "OK");

done:
	if (*btf_log_buf && (err || always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

	free(raw_btf);
	free(user_btf);

	if (btf_fd >= 0)
		close(btf_fd);

	return err;
}

static int test_btf_id(unsigned int test_num)
{
	const struct btf_get_info_test *test = &get_info_tests[test_num - 1];
	LIBBPF_OPTS(bpf_map_create_opts, opts);
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
				 &raw_btf_size, NULL);

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

	btf_fd[0] = load_raw_btf(raw_btf, raw_btf_size);
	if (CHECK(btf_fd[0] < 0, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	/* Test BPF_OBJ_GET_INFO_BY_ID on btf_id */
	info_len = sizeof(info[0]);
	err = bpf_btf_get_info_by_fd(btf_fd[0], &info[0], &info_len);
	if (CHECK(err, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	btf_fd[1] = bpf_btf_get_fd_by_id(info[0].id);
	if (CHECK(btf_fd[1] < 0, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	ret = 0;
	err = bpf_btf_get_info_by_fd(btf_fd[1], &info[1], &info_len);
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
	opts.btf_fd = btf_fd[0];
	opts.btf_key_type_id = 1;
	opts.btf_value_type_id = 2;
	map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, "test_btf_id",
				sizeof(int), sizeof(int), 4, &opts);
	if (CHECK(map_fd < 0, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	info_len = sizeof(map_info);
	err = bpf_map_get_info_by_fd(map_fd, &map_info, &info_len);
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
	if (CHECK(btf_fd[0] < 0, "errno:%d", errno)) {
		err = -1;
		goto done;
	}
	close(btf_fd[0]);
	btf_fd[0] = -1;

	/* The map holds the last ref to BTF and its btf_id */
	close(map_fd);
	map_fd = -1;

	fprintf(stderr, "OK");

done:
	if (*btf_log_buf && (err || always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

	free(raw_btf);
	if (map_fd >= 0)
		close(map_fd);
	for (i = 0; i < 2; i++) {
		free(user_btf[i]);
		if (btf_fd[i] >= 0)
			close(btf_fd[i]);
	}

	return err;
}

static void do_test_get_info(unsigned int test_num)
{
	const struct btf_get_info_test *test = &get_info_tests[test_num - 1];
	unsigned int raw_btf_size, user_btf_size, expected_nbytes;
	uint8_t *raw_btf = NULL, *user_btf = NULL;
	struct bpf_btf_info info = {};
	int btf_fd = -1, err, ret;
	uint32_t info_len;

	if (!test__start_subtest(test->descr))
		return;

	if (test->special_test) {
		err = test->special_test(test_num);
		if (CHECK(err, "failed: %d\n", err))
			return;
	}

	raw_btf = btf_raw_create(&hdr_tmpl,
				 test->raw_types,
				 test->str_sec,
				 test->str_sec_size,
				 &raw_btf_size, NULL);

	if (!raw_btf)
		return;

	*btf_log_buf = '\0';

	user_btf = malloc(raw_btf_size);
	if (CHECK(!user_btf, "!user_btf")) {
		err = -1;
		goto done;
	}

	btf_fd = load_raw_btf(raw_btf, raw_btf_size);
	if (CHECK(btf_fd <= 0, "errno:%d", errno)) {
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
	err = bpf_btf_get_info_by_fd(btf_fd, &info, &info_len);
	if (CHECK(err || !info.id || info_len != sizeof(info) ||
		  info.btf_size != raw_btf_size ||
		  (ret = memcmp(raw_btf, user_btf, expected_nbytes)),
		  "err:%d errno:%d info.id:%u info_len:%u sizeof(info):%zu raw_btf_size:%u info.btf_size:%u expected_nbytes:%u memcmp:%d",
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
	if (*btf_log_buf && (err || always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

	free(raw_btf);
	free(user_btf);

	if (btf_fd >= 0)
		close(btf_fd);
}

struct btf_file_test {
	const char *file;
	bool btf_kv_notfound;
};

static struct btf_file_test file_tests[] = {
	{ .file = "test_btf_newkv.bpf.o", },
	{ .file = "test_btf_nokv.bpf.o", .btf_kv_notfound = true, },
};

static void do_test_file(unsigned int test_num)
{
	const struct btf_file_test *test = &file_tests[test_num - 1];
	const char *expected_fnames[] = {"_dummy_tracepoint",
					 "test_long_fname_1",
					 "test_long_fname_2"};
	struct btf_ext *btf_ext = NULL;
	struct bpf_prog_info info = {};
	struct bpf_object *obj = NULL;
	struct bpf_func_info *finfo;
	struct bpf_program *prog;
	__u32 info_len, rec_size;
	bool has_btf_ext = false;
	struct btf *btf = NULL;
	void *func_info = NULL;
	struct bpf_map *map;
	int i, err, prog_fd;

	if (!test__start_subtest(test->file))
		return;

	btf = btf__parse_elf(test->file, &btf_ext);
	err = libbpf_get_error(btf);
	if (err) {
		if (err == -ENOENT) {
			printf("%s:SKIP: No ELF %s found", __func__, BTF_ELF_SEC);
			test__skip();
			return;
		}
		return;
	}
	btf__free(btf);

	has_btf_ext = btf_ext != NULL;
	btf_ext__free(btf_ext);

	/* temporary disable LIBBPF_STRICT_MAP_DEFINITIONS to test legacy maps */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL & ~LIBBPF_STRICT_MAP_DEFINITIONS);
	obj = bpf_object__open(test->file);
	err = libbpf_get_error(obj);
	if (CHECK(err, "obj: %d", err))
		return;

	prog = bpf_object__next_program(obj, NULL);
	if (CHECK(!prog, "Cannot find bpf_prog")) {
		err = -1;
		goto done;
	}

	bpf_program__set_type(prog, BPF_PROG_TYPE_TRACEPOINT);
	err = bpf_object__load(obj);
	if (CHECK(err < 0, "bpf_object__load: %d", err))
		goto done;
	prog_fd = bpf_program__fd(prog);

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

	if (!has_btf_ext)
		goto skip;

	/* get necessary program info */
	info_len = sizeof(struct bpf_prog_info);
	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);

	if (CHECK(err < 0, "invalid get info (1st) errno:%d", errno)) {
		fprintf(stderr, "%s\n", btf_log_buf);
		err = -1;
		goto done;
	}
	if (CHECK(info.nr_func_info != 3,
		  "incorrect info.nr_func_info (1st) %d",
		  info.nr_func_info)) {
		err = -1;
		goto done;
	}
	rec_size = info.func_info_rec_size;
	if (CHECK(rec_size != sizeof(struct bpf_func_info),
		  "incorrect info.func_info_rec_size (1st) %d\n", rec_size)) {
		err = -1;
		goto done;
	}

	func_info = malloc(info.nr_func_info * rec_size);
	if (CHECK(!func_info, "out of memory")) {
		err = -1;
		goto done;
	}

	/* reset info to only retrieve func_info related data */
	memset(&info, 0, sizeof(info));
	info.nr_func_info = 3;
	info.func_info_rec_size = rec_size;
	info.func_info = ptr_to_u64(func_info);

	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);

	if (CHECK(err < 0, "invalid get info (2nd) errno:%d", errno)) {
		fprintf(stderr, "%s\n", btf_log_buf);
		err = -1;
		goto done;
	}
	if (CHECK(info.nr_func_info != 3,
		  "incorrect info.nr_func_info (2nd) %d",
		  info.nr_func_info)) {
		err = -1;
		goto done;
	}
	if (CHECK(info.func_info_rec_size != rec_size,
		  "incorrect info.func_info_rec_size (2nd) %d",
		  info.func_info_rec_size)) {
		err = -1;
		goto done;
	}

	btf = btf__load_from_kernel_by_id(info.btf_id);
	err = libbpf_get_error(btf);
	if (CHECK(err, "cannot get btf from kernel, err: %d", err))
		goto done;

	/* check three functions */
	finfo = func_info;
	for (i = 0; i < 3; i++) {
		const struct btf_type *t;
		const char *fname;

		t = btf__type_by_id(btf, finfo->type_id);
		if (CHECK(!t, "btf__type_by_id failure: id %u",
			  finfo->type_id)) {
			err = -1;
			goto done;
		}

		fname = btf__name_by_offset(btf, t->name_off);
		err = strcmp(fname, expected_fnames[i]);
		/* for the second and third functions in .text section,
		 * the compiler may order them either way.
		 */
		if (i && err)
			err = strcmp(fname, expected_fnames[3 - i]);
		if (CHECK(err, "incorrect fname %s", fname ? : "")) {
			err = -1;
			goto done;
		}

		finfo = (void *)finfo + rec_size;
	}

skip:
	fprintf(stderr, "OK");

done:
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	btf__free(btf);
	free(func_info);
	bpf_object__close(obj);
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
	uint32_t ui32b;
	uint32_t bits2c:2;
	uint8_t si8_4[2][2];
};

#ifdef __SIZEOF_INT128__
struct pprint_mapv_int128 {
	__int128 si128a;
	__int128 si128b;
	unsigned __int128 bits3:3;
	unsigned __int128 bits80:80;
	unsigned __int128 ui128;
};
#endif

static struct btf_raw_test pprint_test_template[] = {
{
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 11), 40),
		BTF_MEMBER_ENC(NAME_TBD, 11, 0),	/* uint32_t ui32 */
		BTF_MEMBER_ENC(NAME_TBD, 10, 32),	/* uint16_t ui16 */
		BTF_MEMBER_ENC(NAME_TBD, 12, 64),	/* int32_t si32 */
		BTF_MEMBER_ENC(NAME_TBD, 6, 96),	/* unused_bits2a */
		BTF_MEMBER_ENC(NAME_TBD, 7, 98),	/* bits28 */
		BTF_MEMBER_ENC(NAME_TBD, 6, 126),	/* unused_bits2b */
		BTF_MEMBER_ENC(0, 14, 128),		/* union (anon) */
		BTF_MEMBER_ENC(NAME_TBD, 15, 192),	/* aenum */
		BTF_MEMBER_ENC(NAME_TBD, 11, 224),	/* uint32_t ui32b */
		BTF_MEMBER_ENC(NAME_TBD, 6, 256),	/* bits2c */
		BTF_MEMBER_ENC(NAME_TBD, 17, 264),	/* si8_4 */
		BTF_TYPE_ARRAY_ENC(18, 1, 2),		/* [17] */
		BTF_TYPE_ARRAY_ENC(1, 1, 2),		/* [18] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0unsigned char\0unsigned short\0unsigned int\0int\0unsigned long long\0uint8_t\0uint16_t\0uint32_t\0int32_t\0uint64_t\0ui64\0ui8a\0ENUM_ZERO\0ENUM_ONE\0ENUM_TWO\0ENUM_THREE\0pprint_mapv\0ui32\0ui16\0si32\0unused_bits2a\0bits28\0unused_bits2b\0aenum\0ui32b\0bits2c\0si8_4"),
	.key_size = sizeof(unsigned int),
	.value_size = sizeof(struct pprint_mapv),
	.key_type_id = 3,	/* unsigned int */
	.value_type_id = 16,	/* struct pprint_mapv */
	.max_entries = 128,
},

{
	/* this type will have the same type as the
	 * first .raw_types definition, but struct type will
	 * be encoded with kind_flag set.
	 */
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
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),	/* [6] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),	/* [7] */
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 11), 40),
		BTF_MEMBER_ENC(NAME_TBD, 11, BTF_MEMBER_OFFSET(0, 0)),	/* uint32_t ui32 */
		BTF_MEMBER_ENC(NAME_TBD, 10, BTF_MEMBER_OFFSET(0, 32)),	/* uint16_t ui16 */
		BTF_MEMBER_ENC(NAME_TBD, 12, BTF_MEMBER_OFFSET(0, 64)),	/* int32_t si32 */
		BTF_MEMBER_ENC(NAME_TBD, 6, BTF_MEMBER_OFFSET(2, 96)),	/* unused_bits2a */
		BTF_MEMBER_ENC(NAME_TBD, 7, BTF_MEMBER_OFFSET(28, 98)),	/* bits28 */
		BTF_MEMBER_ENC(NAME_TBD, 6, BTF_MEMBER_OFFSET(2, 126)),	/* unused_bits2b */
		BTF_MEMBER_ENC(0, 14, BTF_MEMBER_OFFSET(0, 128)),	/* union (anon) */
		BTF_MEMBER_ENC(NAME_TBD, 15, BTF_MEMBER_OFFSET(0, 192)),	/* aenum */
		BTF_MEMBER_ENC(NAME_TBD, 11, BTF_MEMBER_OFFSET(0, 224)),	/* uint32_t ui32b */
		BTF_MEMBER_ENC(NAME_TBD, 6, BTF_MEMBER_OFFSET(2, 256)),	/* bits2c */
		BTF_MEMBER_ENC(NAME_TBD, 17, 264),	/* si8_4 */
		BTF_TYPE_ARRAY_ENC(18, 1, 2),		/* [17] */
		BTF_TYPE_ARRAY_ENC(1, 1, 2),		/* [18] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0unsigned char\0unsigned short\0unsigned int\0int\0unsigned long long\0uint8_t\0uint16_t\0uint32_t\0int32_t\0uint64_t\0ui64\0ui8a\0ENUM_ZERO\0ENUM_ONE\0ENUM_TWO\0ENUM_THREE\0pprint_mapv\0ui32\0ui16\0si32\0unused_bits2a\0bits28\0unused_bits2b\0aenum\0ui32b\0bits2c\0si8_4"),
	.key_size = sizeof(unsigned int),
	.value_size = sizeof(struct pprint_mapv),
	.key_type_id = 3,	/* unsigned int */
	.value_type_id = 16,	/* struct pprint_mapv */
	.max_entries = 128,
},

{
	/* this type will have the same layout as the
	 * first .raw_types definition. The struct type will
	 * be encoded with kind_flag set, bitfield members
	 * are added typedef/const/volatile, and bitfield members
	 * will have both int and enum types.
	 */
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
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),	/* [6] */
		BTF_TYPE_INT_ENC(0, 0, 0, 32, 4),	/* [7] */
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
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 11), 40),
		BTF_MEMBER_ENC(NAME_TBD, 11, BTF_MEMBER_OFFSET(0, 0)),	/* uint32_t ui32 */
		BTF_MEMBER_ENC(NAME_TBD, 10, BTF_MEMBER_OFFSET(0, 32)),	/* uint16_t ui16 */
		BTF_MEMBER_ENC(NAME_TBD, 12, BTF_MEMBER_OFFSET(0, 64)),	/* int32_t si32 */
		BTF_MEMBER_ENC(NAME_TBD, 17, BTF_MEMBER_OFFSET(2, 96)),	/* unused_bits2a */
		BTF_MEMBER_ENC(NAME_TBD, 7, BTF_MEMBER_OFFSET(28, 98)),	/* bits28 */
		BTF_MEMBER_ENC(NAME_TBD, 19, BTF_MEMBER_OFFSET(2, 126)),/* unused_bits2b */
		BTF_MEMBER_ENC(0, 14, BTF_MEMBER_OFFSET(0, 128)),	/* union (anon) */
		BTF_MEMBER_ENC(NAME_TBD, 15, BTF_MEMBER_OFFSET(0, 192)),	/* aenum */
		BTF_MEMBER_ENC(NAME_TBD, 11, BTF_MEMBER_OFFSET(0, 224)),	/* uint32_t ui32b */
		BTF_MEMBER_ENC(NAME_TBD, 17, BTF_MEMBER_OFFSET(2, 256)),	/* bits2c */
		BTF_MEMBER_ENC(NAME_TBD, 20, BTF_MEMBER_OFFSET(0, 264)),	/* si8_4 */
		/* typedef unsigned int ___int */	/* [17] */
		BTF_TYPEDEF_ENC(NAME_TBD, 18),
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_VOLATILE, 0, 0), 6),	/* [18] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_CONST, 0, 0), 15),	/* [19] */
		BTF_TYPE_ARRAY_ENC(21, 1, 2),					/* [20] */
		BTF_TYPE_ARRAY_ENC(1, 1, 2),					/* [21] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0unsigned char\0unsigned short\0unsigned int\0int\0unsigned long long\0uint8_t\0uint16_t\0uint32_t\0int32_t\0uint64_t\0ui64\0ui8a\0ENUM_ZERO\0ENUM_ONE\0ENUM_TWO\0ENUM_THREE\0pprint_mapv\0ui32\0ui16\0si32\0unused_bits2a\0bits28\0unused_bits2b\0aenum\0ui32b\0bits2c\0___int\0si8_4"),
	.key_size = sizeof(unsigned int),
	.value_size = sizeof(struct pprint_mapv),
	.key_type_id = 3,	/* unsigned int */
	.value_type_id = 16,	/* struct pprint_mapv */
	.max_entries = 128,
},

#ifdef __SIZEOF_INT128__
{
	/* test int128 */
	.raw_types = {
		/* unsigned int */				/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 32, 4),
		/* __int128 */					/* [2] */
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 128, 16),
		/* unsigned __int128 */				/* [3] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 128, 16),
		/* struct pprint_mapv_int128 */			/* [4] */
		BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 5), 64),
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(0, 0)),		/* si128a */
		BTF_MEMBER_ENC(NAME_TBD, 2, BTF_MEMBER_OFFSET(0, 128)),		/* si128b */
		BTF_MEMBER_ENC(NAME_TBD, 3, BTF_MEMBER_OFFSET(3, 256)),		/* bits3 */
		BTF_MEMBER_ENC(NAME_TBD, 3, BTF_MEMBER_OFFSET(80, 259)),	/* bits80 */
		BTF_MEMBER_ENC(NAME_TBD, 3, BTF_MEMBER_OFFSET(0, 384)),		/* ui128 */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0unsigned int\0__int128\0unsigned __int128\0pprint_mapv_int128\0si128a\0si128b\0bits3\0bits80\0ui128"),
	.key_size = sizeof(unsigned int),
	.value_size = sizeof(struct pprint_mapv_int128),
	.key_type_id = 1,
	.value_type_id = 4,
	.max_entries = 128,
	.mapv_kind = PPRINT_MAPV_KIND_INT128,
},
#endif

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

static size_t get_pprint_mapv_size(enum pprint_mapv_kind_t mapv_kind)
{
	if (mapv_kind == PPRINT_MAPV_KIND_BASIC)
		return sizeof(struct pprint_mapv);

#ifdef __SIZEOF_INT128__
	if (mapv_kind == PPRINT_MAPV_KIND_INT128)
		return sizeof(struct pprint_mapv_int128);
#endif

	assert(0);
	return 0;
}

static void set_pprint_mapv(enum pprint_mapv_kind_t mapv_kind,
			    void *mapv, uint32_t i,
			    int num_cpus, int rounded_value_size)
{
	int cpu;

	if (mapv_kind == PPRINT_MAPV_KIND_BASIC) {
		struct pprint_mapv *v = mapv;

		for (cpu = 0; cpu < num_cpus; cpu++) {
			v->ui32 = i + cpu;
			v->si32 = -i;
			v->unused_bits2a = 3;
			v->bits28 = i;
			v->unused_bits2b = 3;
			v->ui64 = i;
			v->aenum = i & 0x03;
			v->ui32b = 4;
			v->bits2c = 1;
			v->si8_4[0][0] = (cpu + i) & 0xff;
			v->si8_4[0][1] = (cpu + i + 1) & 0xff;
			v->si8_4[1][0] = (cpu + i + 2) & 0xff;
			v->si8_4[1][1] = (cpu + i + 3) & 0xff;
			v = (void *)v + rounded_value_size;
		}
	}

#ifdef __SIZEOF_INT128__
	if (mapv_kind == PPRINT_MAPV_KIND_INT128) {
		struct pprint_mapv_int128 *v = mapv;

		for (cpu = 0; cpu < num_cpus; cpu++) {
			v->si128a = i;
			v->si128b = -i;
			v->bits3 = i & 0x07;
			v->bits80 = (((unsigned __int128)1) << 64) + i;
			v->ui128 = (((unsigned __int128)2) << 64) + i;
			v = (void *)v + rounded_value_size;
		}
	}
#endif
}

ssize_t get_pprint_expected_line(enum pprint_mapv_kind_t mapv_kind,
				 char *expected_line, ssize_t line_size,
				 bool percpu_map, unsigned int next_key,
				 int cpu, void *mapv)
{
	ssize_t nexpected_line = -1;

	if (mapv_kind == PPRINT_MAPV_KIND_BASIC) {
		struct pprint_mapv *v = mapv;

		nexpected_line = snprintf(expected_line, line_size,
					  "%s%u: {%u,0,%d,0x%x,0x%x,0x%x,"
					  "{%llu|[%u,%u,%u,%u,%u,%u,%u,%u]},%s,"
					  "%u,0x%x,[[%d,%d],[%d,%d]]}\n",
					  percpu_map ? "\tcpu" : "",
					  percpu_map ? cpu : next_key,
					  v->ui32, v->si32,
					  v->unused_bits2a,
					  v->bits28,
					  v->unused_bits2b,
					  (__u64)v->ui64,
					  v->ui8a[0], v->ui8a[1],
					  v->ui8a[2], v->ui8a[3],
					  v->ui8a[4], v->ui8a[5],
					  v->ui8a[6], v->ui8a[7],
					  pprint_enum_str[v->aenum],
					  v->ui32b,
					  v->bits2c,
					  v->si8_4[0][0], v->si8_4[0][1],
					  v->si8_4[1][0], v->si8_4[1][1]);
	}

#ifdef __SIZEOF_INT128__
	if (mapv_kind == PPRINT_MAPV_KIND_INT128) {
		struct pprint_mapv_int128 *v = mapv;

		nexpected_line = snprintf(expected_line, line_size,
					  "%s%u: {0x%lx,0x%lx,0x%lx,"
					  "0x%lx%016lx,0x%lx%016lx}\n",
					  percpu_map ? "\tcpu" : "",
					  percpu_map ? cpu : next_key,
					  (uint64_t)v->si128a,
					  (uint64_t)v->si128b,
					  (uint64_t)v->bits3,
					  (uint64_t)(v->bits80 >> 64),
					  (uint64_t)v->bits80,
					  (uint64_t)(v->ui128 >> 64),
					  (uint64_t)v->ui128);
	}
#endif

	return nexpected_line;
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


static void do_test_pprint(int test_num)
{
	const struct btf_raw_test *test = &pprint_test_template[test_num];
	enum pprint_mapv_kind_t mapv_kind = test->mapv_kind;
	LIBBPF_OPTS(bpf_map_create_opts, opts);
	bool ordered_map, lossless_map, percpu_map;
	int err, ret, num_cpus, rounded_value_size;
	unsigned int key, nr_read_elems;
	int map_fd = -1, btf_fd = -1;
	unsigned int raw_btf_size;
	char expected_line[255];
	FILE *pin_file = NULL;
	char pin_path[255];
	size_t line_len = 0;
	char *line = NULL;
	void *mapv = NULL;
	uint8_t *raw_btf;
	ssize_t nread;

	if (!test__start_subtest(test->descr))
		return;

	raw_btf = btf_raw_create(&hdr_tmpl, test->raw_types,
				 test->str_sec, test->str_sec_size,
				 &raw_btf_size, NULL);

	if (!raw_btf)
		return;

	*btf_log_buf = '\0';
	btf_fd = load_raw_btf(raw_btf, raw_btf_size);
	free(raw_btf);

	if (CHECK(btf_fd < 0, "errno:%d\n", errno)) {
		err = -1;
		goto done;
	}

	opts.btf_fd = btf_fd;
	opts.btf_key_type_id = test->key_type_id;
	opts.btf_value_type_id = test->value_type_id;
	map_fd = bpf_map_create(test->map_type, test->map_name,
				test->key_size, test->value_size, test->max_entries, &opts);
	if (CHECK(map_fd < 0, "errno:%d", errno)) {
		err = -1;
		goto done;
	}

	ret = snprintf(pin_path, sizeof(pin_path), "%s/%s",
		       "/sys/fs/bpf", test->map_name);

	if (CHECK(ret >= sizeof(pin_path), "pin_path %s/%s is too long",
		  "/sys/fs/bpf", test->map_name)) {
		err = -1;
		goto done;
	}

	err = bpf_obj_pin(map_fd, pin_path);
	if (CHECK(err, "bpf_obj_pin(%s): errno:%d.", pin_path, errno))
		goto done;

	percpu_map = test->percpu_map;
	num_cpus = percpu_map ? bpf_num_possible_cpus() : 1;
	rounded_value_size = round_up(get_pprint_mapv_size(mapv_kind), 8);
	mapv = calloc(num_cpus, rounded_value_size);
	if (CHECK(!mapv, "mapv allocation failure")) {
		err = -1;
		goto done;
	}

	for (key = 0; key < test->max_entries; key++) {
		set_pprint_mapv(mapv_kind, mapv, key, num_cpus, rounded_value_size);
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
		ssize_t nexpected_line;
		unsigned int next_key;
		void *cmapv;
		int cpu;

		next_key = ordered_map ? nr_read_elems : atoi(line);
		set_pprint_mapv(mapv_kind, mapv, next_key, num_cpus, rounded_value_size);
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
					if (err < 0)
						goto done;
				}

				/* read value@cpu */
				nread = getline(&line, &line_len, pin_file);
				if (nread < 0)
					break;
			}

			nexpected_line = get_pprint_expected_line(mapv_kind, expected_line,
								  sizeof(expected_line),
								  percpu_map, next_key,
								  cpu, cmapv);
			err = check_line(expected_line, nexpected_line,
					 sizeof(expected_line), line);
			if (err < 0)
				goto done;

			cmapv = cmapv + rounded_value_size;
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
	if (*btf_log_buf && (err || always_log))
		fprintf(stderr, "\n%s", btf_log_buf);
	if (btf_fd >= 0)
		close(btf_fd);
	if (map_fd >= 0)
		close(map_fd);
	if (pin_file)
		fclose(pin_file);
	unlink(pin_path);
	free(line);
}

static void test_pprint(void)
{
	unsigned int i;

	/* test various maps with the first test template */
	for (i = 0; i < ARRAY_SIZE(pprint_tests_meta); i++) {
		pprint_test_template[0].descr = pprint_tests_meta[i].descr;
		pprint_test_template[0].map_type = pprint_tests_meta[i].map_type;
		pprint_test_template[0].map_name = pprint_tests_meta[i].map_name;
		pprint_test_template[0].ordered_map = pprint_tests_meta[i].ordered_map;
		pprint_test_template[0].lossless_map = pprint_tests_meta[i].lossless_map;
		pprint_test_template[0].percpu_map = pprint_tests_meta[i].percpu_map;

		do_test_pprint(0);
	}

	/* test rest test templates with the first map */
	for (i = 1; i < ARRAY_SIZE(pprint_test_template); i++) {
		pprint_test_template[i].descr = pprint_tests_meta[0].descr;
		pprint_test_template[i].map_type = pprint_tests_meta[0].map_type;
		pprint_test_template[i].map_name = pprint_tests_meta[0].map_name;
		pprint_test_template[i].ordered_map = pprint_tests_meta[0].ordered_map;
		pprint_test_template[i].lossless_map = pprint_tests_meta[0].lossless_map;
		pprint_test_template[i].percpu_map = pprint_tests_meta[0].percpu_map;
		do_test_pprint(i);
	}
}

#define BPF_LINE_INFO_ENC(insn_off, file_off, line_off, line_num, line_col) \
	(insn_off), (file_off), (line_off), ((line_num) << 10 | ((line_col) & 0x3ff))

static struct prog_info_raw_test {
	const char *descr;
	const char *str_sec;
	const char *err_str;
	__u32 raw_types[MAX_NR_RAW_U32];
	__u32 str_sec_size;
	struct bpf_insn insns[MAX_INSNS];
	__u32 prog_type;
	__u32 func_info[MAX_SUBPROGS][2];
	__u32 func_info_rec_size;
	__u32 func_info_cnt;
	__u32 line_info[MAX_NR_RAW_U32];
	__u32 line_info_rec_size;
	__u32 nr_jited_ksyms;
	bool expected_prog_load_failure;
	__u32 dead_code_cnt;
	__u32 dead_code_mask;
	__u32 dead_func_cnt;
	__u32 dead_func_mask;
} info_raw_tests[] = {
{
	.descr = "func_type (main func + one sub)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 32, 4),	/* [2] */
		BTF_FUNC_PROTO_ENC(1, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		BTF_FUNC_PROTO_ENC(1, 2),			/* [4] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 3),			/* [5] */
		BTF_FUNC_ENC(NAME_TBD, 4),			/* [6] */
		BTF_END_RAW,
	},
	.str_sec = "\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB",
	.str_sec_size = sizeof("\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB"),
	.insns = {
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info = { {0, 5}, {3, 6} },
	.func_info_rec_size = 8,
	.func_info_cnt = 2,
	.line_info = { BTF_END_RAW },
},

{
	.descr = "func_type (Incorrect func_info_rec_size)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 32, 4),	/* [2] */
		BTF_FUNC_PROTO_ENC(1, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		BTF_FUNC_PROTO_ENC(1, 2),			/* [4] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 3),			/* [5] */
		BTF_FUNC_ENC(NAME_TBD, 4),			/* [6] */
		BTF_END_RAW,
	},
	.str_sec = "\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB",
	.str_sec_size = sizeof("\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB"),
	.insns = {
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info = { {0, 5}, {3, 6} },
	.func_info_rec_size = 4,
	.func_info_cnt = 2,
	.line_info = { BTF_END_RAW },
	.expected_prog_load_failure = true,
},

{
	.descr = "func_type (Incorrect func_info_cnt)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 32, 4),	/* [2] */
		BTF_FUNC_PROTO_ENC(1, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		BTF_FUNC_PROTO_ENC(1, 2),			/* [4] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 3),			/* [5] */
		BTF_FUNC_ENC(NAME_TBD, 4),			/* [6] */
		BTF_END_RAW,
	},
	.str_sec = "\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB",
	.str_sec_size = sizeof("\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB"),
	.insns = {
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info = { {0, 5}, {3, 6} },
	.func_info_rec_size = 8,
	.func_info_cnt = 1,
	.line_info = { BTF_END_RAW },
	.expected_prog_load_failure = true,
},

{
	.descr = "func_type (Incorrect bpf_func_info.insn_off)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 32, 4),	/* [2] */
		BTF_FUNC_PROTO_ENC(1, 2),			/* [3] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
		BTF_FUNC_PROTO_ENC(1, 2),			/* [4] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 2),
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 3),			/* [5] */
		BTF_FUNC_ENC(NAME_TBD, 4),			/* [6] */
		BTF_END_RAW,
	},
	.str_sec = "\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB",
	.str_sec_size = sizeof("\0int\0unsigned int\0a\0b\0c\0d\0funcA\0funcB"),
	.insns = {
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 1, 0, 2),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 2),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info = { {0, 5}, {2, 6} },
	.func_info_rec_size = 8,
	.func_info_cnt = 2,
	.line_info = { BTF_END_RAW },
	.expected_prog_load_failure = true,
},

{
	.descr = "line_info (No subprog)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1;\0int b=2;\0return a + b;\0return a + b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(1, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 4, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 1,
},

{
	.descr = "line_info (No subprog. insn_off >= prog->len)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1;\0int b=2;\0return a + b;\0return a + b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(1, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 4, 7),
		BPF_LINE_INFO_ENC(4, 0, 0, 5, 6),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 1,
	.err_str = "line_info[4].insn_off",
	.expected_prog_load_failure = true,
},

{
	.descr = "line_info (Zero bpf insn code)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_TYPE_INT_ENC(NAME_TBD, 0, 0, 64, 8),	/* [2] */
		BTF_TYPEDEF_ENC(NAME_TBD, 2),			/* [3] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0unsigned long\0u64\0u64 a=1;\0return a;"),
	.insns = {
		BPF_LD_IMM64(BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(1, 0, 0, 2, 9),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 8),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 1,
	.err_str = "Invalid insn code at line_info[1]",
	.expected_prog_load_failure = true,
},

{
	.descr = "line_info (No subprog. zero tailing line_info",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1;\0int b=2;\0return a + b;\0return a + b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10), 0,
		BPF_LINE_INFO_ENC(1, 0, NAME_TBD, 2, 9), 0,
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 8), 0,
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 4, 7), 0,
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info) + sizeof(__u32),
	.nr_jited_ksyms = 1,
},

{
	.descr = "line_info (No subprog. nonzero tailing line_info)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1;\0int b=2;\0return a + b;\0return a + b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10), 0,
		BPF_LINE_INFO_ENC(1, 0, NAME_TBD, 2, 9), 0,
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 8), 0,
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 4, 7), 1,
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info) + sizeof(__u32),
	.nr_jited_ksyms = 1,
	.err_str = "nonzero tailing record in line_info",
	.expected_prog_load_failure = true,
},

{
	.descr = "line_info (subprog)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1+1;\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 4, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
},

{
	.descr = "line_info (subprog + func_info)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(1, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0x\0sub\0main\0int a=1+1;\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 2,
	.func_info_rec_size = 8,
	.func_info = { {0, 4}, {5, 3} },
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 4, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
},

{
	.descr = "line_info (subprog. missing 1st func line info)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1+1;\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(1, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 4, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
	.err_str = "missing bpf_line_info for func#0",
	.expected_prog_load_failure = true,
},

{
	.descr = "line_info (subprog. missing 2nd func line info)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1+1;\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(6, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 4, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
	.err_str = "missing bpf_line_info for func#1",
	.expected_prog_load_failure = true,
},

{
	.descr = "line_info (subprog. unordered insn offset)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1+1;\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 4, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
	.err_str = "Invalid line_info[2].insn_off",
	.expected_prog_load_failure = true,
},

{
	.descr = "line_info (dead start)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0/* dead jmp */\0int a=1;\0int b=2;\0return a + b;\0return a + b;"),
	.insns = {
		BPF_JMP_IMM(BPF_JA, 0, 0, 0),
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(1, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 4, 7),
		BPF_LINE_INFO_ENC(4, 0, NAME_TBD, 5, 6),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 1,
	.dead_code_cnt = 1,
	.dead_code_mask = 0x01,
},

{
	.descr = "line_info (dead end)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0int a=1;\0int b=2;\0return a + b;\0/* dead jmp */\0return a + b;\0/* dead exit */"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 1),
		BPF_MOV64_IMM(BPF_REG_1, 2),
		BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1),
		BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 10, 1),
		BPF_EXIT_INSN(),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 0,
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 12),
		BPF_LINE_INFO_ENC(1, 0, NAME_TBD, 2, 11),
		BPF_LINE_INFO_ENC(2, 0, NAME_TBD, 3, 10),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 4, 9),
		BPF_LINE_INFO_ENC(4, 0, NAME_TBD, 5, 8),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 6, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 1,
	.dead_code_cnt = 2,
	.dead_code_mask = 0x28,
},

{
	.descr = "line_info (dead code + subprog + func_info)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(1, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0x\0sub\0main\0int a=1+1;\0/* dead jmp */"
		    "\0/* dead */\0/* dead */\0/* dead */\0/* dead */"
		    "\0/* dead */\0/* dead */\0/* dead */\0/* dead */"
		    "\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, 1),
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_2),
		BPF_JMP_IMM(BPF_JGE, BPF_REG_2, 0, 8),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 2,
	.func_info_rec_size = 8,
	.func_info = { {0, 4}, {14, 3} },
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(4, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(6, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(8, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(9, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(10, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(11, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(12, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(14, 0, NAME_TBD, 3, 8),
		BPF_LINE_INFO_ENC(16, 0, NAME_TBD, 4, 7),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
	.dead_code_cnt = 9,
	.dead_code_mask = 0x3fe,
},

{
	.descr = "line_info (dead subprog)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(1, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [4] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [5] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0x\0dead\0main\0func\0int a=1+1;\0/* live call */"
		    "\0return 0;\0return 0;\0/* dead */\0/* dead */"
		    "\0/* dead */\0return bla + 1;\0return bla + 1;"
		    "\0return bla + 1;\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_JMP_IMM(BPF_JGE, BPF_REG_2, 0, 1),
		BPF_CALL_REL(3),
		BPF_CALL_REL(5),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_MOV64_REG(BPF_REG_0, 2),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 3,
	.func_info_rec_size = 8,
		.func_info = { {0, 4}, {6, 3}, {9, 5} },
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(4, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(6, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(8, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(9, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(10, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(11, 0, NAME_TBD, 2, 9),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
	.dead_code_cnt = 3,
	.dead_code_mask = 0x70,
	.dead_func_cnt = 1,
	.dead_func_mask = 0x2,
},

{
	.descr = "line_info (dead last subprog)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(1, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [5] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0x\0dead\0main\0int a=1+1;\0/* live call */"
		    "\0return 0;\0/* dead */\0/* dead */"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_JMP_IMM(BPF_JGE, BPF_REG_2, 0, 1),
		BPF_CALL_REL(2),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 2,
	.func_info_rec_size = 8,
		.func_info = { {0, 4}, {5, 3} },
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(4, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(6, 0, NAME_TBD, 1, 10),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 1,
	.dead_code_cnt = 2,
	.dead_code_mask = 0x18,
	.dead_func_cnt = 1,
	.dead_func_mask = 0x2,
},

{
	.descr = "line_info (dead subprog + dead start)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(1, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [4] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [5] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0x\0dead\0main\0func\0int a=1+1;\0/* dead */"
		    "\0return 0;\0return 0;\0return 0;"
		    "\0/* dead */\0/* dead */\0/* dead */\0/* dead */"
		    "\0return b + 1;\0return b + 1;\0return b + 1;"),
	.insns = {
		BPF_JMP_IMM(BPF_JA, 0, 0, 0),
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_JMP_IMM(BPF_JGE, BPF_REG_2, 0, 1),
		BPF_CALL_REL(3),
		BPF_CALL_REL(5),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0),
		BPF_MOV64_REG(BPF_REG_0, 2),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 3,
	.func_info_rec_size = 8,
		.func_info = { {0, 4}, {7, 3}, {10, 5} },
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(4, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(6, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(8, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(9, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(10, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(11, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(12, 0, NAME_TBD, 2, 9),
		BPF_LINE_INFO_ENC(13, 0, NAME_TBD, 2, 9),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
	.dead_code_cnt = 5,
	.dead_code_mask = 0x1e2,
	.dead_func_cnt = 1,
	.dead_func_mask = 0x2,
},

{
	.descr = "line_info (dead subprog + dead start w/ move)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(1, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [4] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [5] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0x\0dead\0main\0func\0int a=1+1;\0/* live call */"
		    "\0return 0;\0return 0;\0/* dead */\0/* dead */"
		    "\0/* dead */\0return bla + 1;\0return bla + 1;"
		    "\0return bla + 1;\0return func(a);\0b+=1;\0return b;"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_2, 1),
		BPF_JMP_IMM(BPF_JGE, BPF_REG_2, 0, 1),
		BPF_CALL_REL(3),
		BPF_CALL_REL(5),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_CALL_REL(1),
		BPF_EXIT_INSN(),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0),
		BPF_MOV64_REG(BPF_REG_0, 2),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_0, 1),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 3,
	.func_info_rec_size = 8,
		.func_info = { {0, 4}, {6, 3}, {9, 5} },
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(3, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(4, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(5, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(6, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(7, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(8, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(9, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(11, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(12, 0, NAME_TBD, 2, 9),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
	.dead_code_cnt = 3,
	.dead_code_mask = 0x70,
	.dead_func_cnt = 1,
	.dead_func_mask = 0x2,
},

{
	.descr = "line_info (dead end + subprog start w/ no linfo)",
	.raw_types = {
		BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
		BTF_FUNC_PROTO_ENC(1, 1),			/* [2] */
			BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [3] */
		BTF_FUNC_ENC(NAME_TBD, 2),			/* [4] */
		BTF_END_RAW,
	},
	BTF_STR_SEC("\0int\0x\0main\0func\0/* main linfo */\0/* func linfo */"),
	.insns = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_JMP_IMM(BPF_JGE, BPF_REG_0, 1, 3),
		BPF_CALL_REL(3),
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
		BPF_EXIT_INSN(),
		BPF_JMP_IMM(BPF_JA, 0, 0, 0),
		BPF_EXIT_INSN(),
	},
	.prog_type = BPF_PROG_TYPE_TRACEPOINT,
	.func_info_cnt = 2,
	.func_info_rec_size = 8,
	.func_info = { {0, 3}, {6, 4}, },
	.line_info = {
		BPF_LINE_INFO_ENC(0, 0, NAME_TBD, 1, 10),
		BPF_LINE_INFO_ENC(6, 0, NAME_TBD, 1, 10),
		BTF_END_RAW,
	},
	.line_info_rec_size = sizeof(struct bpf_line_info),
	.nr_jited_ksyms = 2,
},

};

static size_t probe_prog_length(const struct bpf_insn *fp)
{
	size_t len;

	for (len = MAX_INSNS - 1; len > 0; --len)
		if (fp[len].code != 0 || fp[len].imm != 0)
			break;
	return len + 1;
}

static __u32 *patch_name_tbd(const __u32 *raw_u32,
			     const char *str, __u32 str_off,
			     unsigned int str_sec_size,
			     unsigned int *ret_size)
{
	int i, raw_u32_size = get_raw_sec_size(raw_u32);
	const char *end_str = str + str_sec_size;
	const char *next_str = str + str_off;
	__u32 *new_u32 = NULL;

	if (raw_u32_size == -1)
		return ERR_PTR(-EINVAL);

	if (!raw_u32_size) {
		*ret_size = 0;
		return NULL;
	}

	new_u32 = malloc(raw_u32_size);
	if (!new_u32)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < raw_u32_size / sizeof(raw_u32[0]); i++) {
		if (raw_u32[i] == NAME_TBD) {
			next_str = get_next_str(next_str, end_str);
			if (CHECK(!next_str, "Error in getting next_str\n")) {
				free(new_u32);
				return ERR_PTR(-EINVAL);
			}
			new_u32[i] = next_str - str;
			next_str += strlen(next_str);
		} else {
			new_u32[i] = raw_u32[i];
		}
	}

	*ret_size = raw_u32_size;
	return new_u32;
}

static int test_get_finfo(const struct prog_info_raw_test *test,
			  int prog_fd)
{
	struct bpf_prog_info info = {};
	struct bpf_func_info *finfo;
	__u32 info_len, rec_size, i;
	void *func_info = NULL;
	__u32 nr_func_info;
	int err;

	/* get necessary lens */
	info_len = sizeof(struct bpf_prog_info);
	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	if (CHECK(err < 0, "invalid get info (1st) errno:%d", errno)) {
		fprintf(stderr, "%s\n", btf_log_buf);
		return -1;
	}
	nr_func_info = test->func_info_cnt - test->dead_func_cnt;
	if (CHECK(info.nr_func_info != nr_func_info,
		  "incorrect info.nr_func_info (1st) %d",
		  info.nr_func_info)) {
		return -1;
	}

	rec_size = info.func_info_rec_size;
	if (CHECK(rec_size != sizeof(struct bpf_func_info),
		  "incorrect info.func_info_rec_size (1st) %d", rec_size)) {
		return -1;
	}

	if (!info.nr_func_info)
		return 0;

	func_info = malloc(info.nr_func_info * rec_size);
	if (CHECK(!func_info, "out of memory"))
		return -1;

	/* reset info to only retrieve func_info related data */
	memset(&info, 0, sizeof(info));
	info.nr_func_info = nr_func_info;
	info.func_info_rec_size = rec_size;
	info.func_info = ptr_to_u64(func_info);
	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	if (CHECK(err < 0, "invalid get info (2nd) errno:%d", errno)) {
		fprintf(stderr, "%s\n", btf_log_buf);
		err = -1;
		goto done;
	}
	if (CHECK(info.nr_func_info != nr_func_info,
		  "incorrect info.nr_func_info (2nd) %d",
		  info.nr_func_info)) {
		err = -1;
		goto done;
	}
	if (CHECK(info.func_info_rec_size != rec_size,
		  "incorrect info.func_info_rec_size (2nd) %d",
		  info.func_info_rec_size)) {
		err = -1;
		goto done;
	}

	finfo = func_info;
	for (i = 0; i < nr_func_info; i++) {
		if (test->dead_func_mask & (1 << i))
			continue;
		if (CHECK(finfo->type_id != test->func_info[i][1],
			  "incorrect func_type %u expected %u",
			  finfo->type_id, test->func_info[i][1])) {
			err = -1;
			goto done;
		}
		finfo = (void *)finfo + rec_size;
	}

	err = 0;

done:
	free(func_info);
	return err;
}

static int test_get_linfo(const struct prog_info_raw_test *test,
			  const void *patched_linfo,
			  __u32 cnt, int prog_fd)
{
	__u32 i, info_len, nr_jited_ksyms, nr_jited_func_lens;
	__u64 *jited_linfo = NULL, *jited_ksyms = NULL;
	__u32 rec_size, jited_rec_size, jited_cnt;
	struct bpf_line_info *linfo = NULL;
	__u32 cur_func_len, ksyms_found;
	struct bpf_prog_info info = {};
	__u32 *jited_func_lens = NULL;
	__u64 cur_func_ksyms;
	__u32 dead_insns;
	int err;

	jited_cnt = cnt;
	rec_size = sizeof(*linfo);
	jited_rec_size = sizeof(*jited_linfo);
	if (test->nr_jited_ksyms)
		nr_jited_ksyms = test->nr_jited_ksyms;
	else
		nr_jited_ksyms = test->func_info_cnt - test->dead_func_cnt;
	nr_jited_func_lens = nr_jited_ksyms;

	info_len = sizeof(struct bpf_prog_info);
	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);
	if (CHECK(err < 0, "err:%d errno:%d", err, errno)) {
		err = -1;
		goto done;
	}

	if (!info.jited_prog_len) {
		/* prog is not jited */
		jited_cnt = 0;
		nr_jited_ksyms = 1;
		nr_jited_func_lens = 1;
	}

	if (CHECK(info.nr_line_info != cnt ||
		  info.nr_jited_line_info != jited_cnt ||
		  info.nr_jited_ksyms != nr_jited_ksyms ||
		  info.nr_jited_func_lens != nr_jited_func_lens ||
		  (!info.nr_line_info && info.nr_jited_line_info),
		  "info: nr_line_info:%u(expected:%u) nr_jited_line_info:%u(expected:%u) nr_jited_ksyms:%u(expected:%u) nr_jited_func_lens:%u(expected:%u)",
		  info.nr_line_info, cnt,
		  info.nr_jited_line_info, jited_cnt,
		  info.nr_jited_ksyms, nr_jited_ksyms,
		  info.nr_jited_func_lens, nr_jited_func_lens)) {
		err = -1;
		goto done;
	}

	if (CHECK(info.line_info_rec_size != sizeof(struct bpf_line_info) ||
		  info.jited_line_info_rec_size != sizeof(__u64),
		  "info: line_info_rec_size:%u(userspace expected:%u) jited_line_info_rec_size:%u(userspace expected:%u)",
		  info.line_info_rec_size, rec_size,
		  info.jited_line_info_rec_size, jited_rec_size)) {
		err = -1;
		goto done;
	}

	if (!cnt)
		return 0;

	rec_size = info.line_info_rec_size;
	jited_rec_size = info.jited_line_info_rec_size;

	memset(&info, 0, sizeof(info));

	linfo = calloc(cnt, rec_size);
	if (CHECK(!linfo, "!linfo")) {
		err = -1;
		goto done;
	}
	info.nr_line_info = cnt;
	info.line_info_rec_size = rec_size;
	info.line_info = ptr_to_u64(linfo);

	if (jited_cnt) {
		jited_linfo = calloc(jited_cnt, jited_rec_size);
		jited_ksyms = calloc(nr_jited_ksyms, sizeof(*jited_ksyms));
		jited_func_lens = calloc(nr_jited_func_lens,
					 sizeof(*jited_func_lens));
		if (CHECK(!jited_linfo || !jited_ksyms || !jited_func_lens,
			  "jited_linfo:%p jited_ksyms:%p jited_func_lens:%p",
			  jited_linfo, jited_ksyms, jited_func_lens)) {
			err = -1;
			goto done;
		}

		info.nr_jited_line_info = jited_cnt;
		info.jited_line_info_rec_size = jited_rec_size;
		info.jited_line_info = ptr_to_u64(jited_linfo);
		info.nr_jited_ksyms = nr_jited_ksyms;
		info.jited_ksyms = ptr_to_u64(jited_ksyms);
		info.nr_jited_func_lens = nr_jited_func_lens;
		info.jited_func_lens = ptr_to_u64(jited_func_lens);
	}

	err = bpf_prog_get_info_by_fd(prog_fd, &info, &info_len);

	/*
	 * Only recheck the info.*line_info* fields.
	 * Other fields are not the concern of this test.
	 */
	if (CHECK(err < 0 ||
		  info.nr_line_info != cnt ||
		  (jited_cnt && !info.jited_line_info) ||
		  info.nr_jited_line_info != jited_cnt ||
		  info.line_info_rec_size != rec_size ||
		  info.jited_line_info_rec_size != jited_rec_size,
		  "err:%d errno:%d info: nr_line_info:%u(expected:%u) nr_jited_line_info:%u(expected:%u) line_info_rec_size:%u(expected:%u) jited_linfo_rec_size:%u(expected:%u) line_info:%p jited_line_info:%p",
		  err, errno,
		  info.nr_line_info, cnt,
		  info.nr_jited_line_info, jited_cnt,
		  info.line_info_rec_size, rec_size,
		  info.jited_line_info_rec_size, jited_rec_size,
		  (void *)(long)info.line_info,
		  (void *)(long)info.jited_line_info)) {
		err = -1;
		goto done;
	}

	dead_insns = 0;
	while (test->dead_code_mask & (1 << dead_insns))
		dead_insns++;

	CHECK(linfo[0].insn_off, "linfo[0].insn_off:%u",
	      linfo[0].insn_off);
	for (i = 1; i < cnt; i++) {
		const struct bpf_line_info *expected_linfo;

		while (test->dead_code_mask & (1 << (i + dead_insns)))
			dead_insns++;

		expected_linfo = patched_linfo +
			((i + dead_insns) * test->line_info_rec_size);
		if (CHECK(linfo[i].insn_off <= linfo[i - 1].insn_off,
			  "linfo[%u].insn_off:%u <= linfo[%u].insn_off:%u",
			  i, linfo[i].insn_off,
			  i - 1, linfo[i - 1].insn_off)) {
			err = -1;
			goto done;
		}
		if (CHECK(linfo[i].file_name_off != expected_linfo->file_name_off ||
			  linfo[i].line_off != expected_linfo->line_off ||
			  linfo[i].line_col != expected_linfo->line_col,
			  "linfo[%u] (%u, %u, %u) != (%u, %u, %u)", i,
			  linfo[i].file_name_off,
			  linfo[i].line_off,
			  linfo[i].line_col,
			  expected_linfo->file_name_off,
			  expected_linfo->line_off,
			  expected_linfo->line_col)) {
			err = -1;
			goto done;
		}
	}

	if (!jited_cnt) {
		fprintf(stderr, "not jited. skipping jited_line_info check. ");
		err = 0;
		goto done;
	}

	if (CHECK(jited_linfo[0] != jited_ksyms[0],
		  "jited_linfo[0]:%lx != jited_ksyms[0]:%lx",
		  (long)(jited_linfo[0]), (long)(jited_ksyms[0]))) {
		err = -1;
		goto done;
	}

	ksyms_found = 1;
	cur_func_len = jited_func_lens[0];
	cur_func_ksyms = jited_ksyms[0];
	for (i = 1; i < jited_cnt; i++) {
		if (ksyms_found < nr_jited_ksyms &&
		    jited_linfo[i] == jited_ksyms[ksyms_found]) {
			cur_func_ksyms = jited_ksyms[ksyms_found];
			cur_func_len = jited_ksyms[ksyms_found];
			ksyms_found++;
			continue;
		}

		if (CHECK(jited_linfo[i] <= jited_linfo[i - 1],
			  "jited_linfo[%u]:%lx <= jited_linfo[%u]:%lx",
			  i, (long)jited_linfo[i],
			  i - 1, (long)(jited_linfo[i - 1]))) {
			err = -1;
			goto done;
		}

		if (CHECK(jited_linfo[i] - cur_func_ksyms > cur_func_len,
			  "jited_linfo[%u]:%lx - %lx > %u",
			  i, (long)jited_linfo[i], (long)cur_func_ksyms,
			  cur_func_len)) {
			err = -1;
			goto done;
		}
	}

	if (CHECK(ksyms_found != nr_jited_ksyms,
		  "ksyms_found:%u != nr_jited_ksyms:%u",
		  ksyms_found, nr_jited_ksyms)) {
		err = -1;
		goto done;
	}

	err = 0;

done:
	free(linfo);
	free(jited_linfo);
	free(jited_ksyms);
	free(jited_func_lens);
	return err;
}

static void do_test_info_raw(unsigned int test_num)
{
	const struct prog_info_raw_test *test = &info_raw_tests[test_num - 1];
	unsigned int raw_btf_size, linfo_str_off, linfo_size = 0;
	int btf_fd = -1, prog_fd = -1, err = 0;
	void *raw_btf, *patched_linfo = NULL;
	const char *ret_next_str;
	union bpf_attr attr = {};

	if (!test__start_subtest(test->descr))
		return;

	raw_btf = btf_raw_create(&hdr_tmpl, test->raw_types,
				 test->str_sec, test->str_sec_size,
				 &raw_btf_size, &ret_next_str);
	if (!raw_btf)
		return;

	*btf_log_buf = '\0';
	btf_fd = load_raw_btf(raw_btf, raw_btf_size);
	free(raw_btf);

	if (CHECK(btf_fd < 0, "invalid btf_fd errno:%d", errno)) {
		err = -1;
		goto done;
	}

	if (*btf_log_buf && always_log)
		fprintf(stderr, "\n%s", btf_log_buf);
	*btf_log_buf = '\0';

	linfo_str_off = ret_next_str - test->str_sec;
	patched_linfo = patch_name_tbd(test->line_info,
				       test->str_sec, linfo_str_off,
				       test->str_sec_size, &linfo_size);
	err = libbpf_get_error(patched_linfo);
	if (err) {
		fprintf(stderr, "error in creating raw bpf_line_info");
		err = -1;
		goto done;
	}

	attr.prog_type = test->prog_type;
	attr.insns = ptr_to_u64(test->insns);
	attr.insn_cnt = probe_prog_length(test->insns);
	attr.license = ptr_to_u64("GPL");
	attr.prog_btf_fd = btf_fd;
	attr.func_info_rec_size = test->func_info_rec_size;
	attr.func_info_cnt = test->func_info_cnt;
	attr.func_info = ptr_to_u64(test->func_info);
	attr.log_buf = ptr_to_u64(btf_log_buf);
	attr.log_size = BTF_LOG_BUF_SIZE;
	attr.log_level = 1;
	if (linfo_size) {
		attr.line_info_rec_size = test->line_info_rec_size;
		attr.line_info = ptr_to_u64(patched_linfo);
		attr.line_info_cnt = linfo_size / attr.line_info_rec_size;
	}

	prog_fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
	err = ((prog_fd < 0) != test->expected_prog_load_failure);
	if (CHECK(err, "prog_fd:%d expected_prog_load_failure:%u errno:%d",
		  prog_fd, test->expected_prog_load_failure, errno) ||
	    CHECK(test->err_str && !strstr(btf_log_buf, test->err_str),
		  "expected err_str:%s", test->err_str)) {
		err = -1;
		goto done;
	}

	if (prog_fd < 0)
		goto done;

	err = test_get_finfo(test, prog_fd);
	if (err)
		goto done;

	err = test_get_linfo(test, patched_linfo,
			     attr.line_info_cnt - test->dead_code_cnt,
			     prog_fd);
	if (err)
		goto done;

done:
	if (*btf_log_buf && (err || always_log))
		fprintf(stderr, "\n%s", btf_log_buf);

	if (btf_fd >= 0)
		close(btf_fd);
	if (prog_fd >= 0)
		close(prog_fd);

	if (!libbpf_get_error(patched_linfo))
		free(patched_linfo);
}

struct btf_raw_data {
	__u32 raw_types[MAX_NR_RAW_U32];
	const char *str_sec;
	__u32 str_sec_size;
};

struct btf_dedup_test {
	const char *descr;
	struct btf_raw_data input;
	struct btf_raw_data expect;
	struct btf_dedup_opts opts;
};

static struct btf_dedup_test dedup_tests[] = {

{
	.descr = "dedup: unused strings filtering",
	.input = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_NTH(2), BTF_INT_SIGNED, 0, 32, 4),
			BTF_TYPE_INT_ENC(NAME_NTH(5), BTF_INT_SIGNED, 0, 64, 8),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0unused\0int\0foo\0bar\0long"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 4),
			BTF_TYPE_INT_ENC(NAME_NTH(2), BTF_INT_SIGNED, 0, 64, 8),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0int\0long"),
	},
},
{
	.descr = "dedup: strings deduplication",
	.input = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 4),
			BTF_TYPE_INT_ENC(NAME_NTH(2), BTF_INT_SIGNED, 0, 64, 8),
			BTF_TYPE_INT_ENC(NAME_NTH(3), BTF_INT_SIGNED, 0, 32, 4),
			BTF_TYPE_INT_ENC(NAME_NTH(4), BTF_INT_SIGNED, 0, 64, 8),
			BTF_TYPE_INT_ENC(NAME_NTH(5), BTF_INT_SIGNED, 0, 32, 4),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0int\0long int\0int\0long int\0int"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 4),
			BTF_TYPE_INT_ENC(NAME_NTH(2), BTF_INT_SIGNED, 0, 64, 8),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0int\0long int"),
	},
},
{
	.descr = "dedup: struct example #1",
	/*
	 * struct s {
	 *	struct s *next;
	 *	const int *a;
	 *	int b[16];
	 *	int c;
	 * }
	 */
	.input = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			/* int[16] */
			BTF_TYPE_ARRAY_ENC(1, 1, 16),					/* [2] */
			/* struct s { */
			BTF_STRUCT_ENC(NAME_NTH(2), 5, 88),				/* [3] */
				BTF_MEMBER_ENC(NAME_NTH(3), 4, 0),	/* struct s *next;	*/
				BTF_MEMBER_ENC(NAME_NTH(4), 5, 64),	/* const int *a;	*/
				BTF_MEMBER_ENC(NAME_NTH(5), 2, 128),	/* int b[16];		*/
				BTF_MEMBER_ENC(NAME_NTH(6), 1, 640),	/* int c;		*/
				BTF_MEMBER_ENC(NAME_NTH(8), 15, 672),	/* float d;		*/
			/* ptr -> [3] struct s */
			BTF_PTR_ENC(3),							/* [4] */
			/* ptr -> [6] const int */
			BTF_PTR_ENC(6),							/* [5] */
			/* const -> [1] int */
			BTF_CONST_ENC(1),						/* [6] */
			/* tag -> [3] struct s */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 3, -1),				/* [7] */
			/* tag -> [3] struct s, member 1 */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 3, 1),				/* [8] */

			/* full copy of the above */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 4),	/* [9] */
			BTF_TYPE_ARRAY_ENC(9, 9, 16),					/* [10] */
			BTF_STRUCT_ENC(NAME_NTH(2), 5, 88),				/* [11] */
				BTF_MEMBER_ENC(NAME_NTH(3), 12, 0),
				BTF_MEMBER_ENC(NAME_NTH(4), 13, 64),
				BTF_MEMBER_ENC(NAME_NTH(5), 10, 128),
				BTF_MEMBER_ENC(NAME_NTH(6), 9, 640),
				BTF_MEMBER_ENC(NAME_NTH(8), 15, 672),
			BTF_PTR_ENC(11),						/* [12] */
			BTF_PTR_ENC(14),						/* [13] */
			BTF_CONST_ENC(9),						/* [14] */
			BTF_TYPE_FLOAT_ENC(NAME_NTH(7), 4),				/* [15] */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 11, -1),				/* [16] */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 11, 1),				/* [17] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0int\0s\0next\0a\0b\0c\0float\0d"),
	},
	.expect = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(NAME_NTH(5), BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			/* int[16] */
			BTF_TYPE_ARRAY_ENC(1, 1, 16),					/* [2] */
			/* struct s { */
			BTF_STRUCT_ENC(NAME_NTH(8), 5, 88),				/* [3] */
				BTF_MEMBER_ENC(NAME_NTH(7), 4, 0),	/* struct s *next;	*/
				BTF_MEMBER_ENC(NAME_NTH(1), 5, 64),	/* const int *a;	*/
				BTF_MEMBER_ENC(NAME_NTH(2), 2, 128),	/* int b[16];		*/
				BTF_MEMBER_ENC(NAME_NTH(3), 1, 640),	/* int c;		*/
				BTF_MEMBER_ENC(NAME_NTH(4), 9, 672),	/* float d;		*/
			/* ptr -> [3] struct s */
			BTF_PTR_ENC(3),							/* [4] */
			/* ptr -> [6] const int */
			BTF_PTR_ENC(6),							/* [5] */
			/* const -> [1] int */
			BTF_CONST_ENC(1),						/* [6] */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 3, -1),				/* [7] */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 3, 1),				/* [8] */
			BTF_TYPE_FLOAT_ENC(NAME_NTH(7), 4),				/* [9] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0a\0b\0c\0d\0int\0float\0next\0s"),
	},
},
{
	.descr = "dedup: struct <-> fwd resolution w/ hash collision",
	/*
	 * // CU 1:
	 * struct x;
	 * struct s {
	 *	struct x *x;
	 * };
	 * // CU 2:
	 * struct x {};
	 * struct s {
	 *	struct x *x;
	 * };
	 */
	.input = {
		.raw_types = {
			/* CU 1 */
			BTF_FWD_ENC(NAME_TBD, 0 /* struct fwd */),	/* [1] fwd x      */
			BTF_PTR_ENC(1),					/* [2] ptr -> [1] */
			BTF_STRUCT_ENC(NAME_TBD, 1, 8),			/* [3] struct s   */
				BTF_MEMBER_ENC(NAME_TBD, 2, 0),
			/* CU 2 */
			BTF_STRUCT_ENC(NAME_TBD, 0, 0),			/* [4] struct x   */
			BTF_PTR_ENC(4),					/* [5] ptr -> [4] */
			BTF_STRUCT_ENC(NAME_TBD, 1, 8),			/* [6] struct s   */
				BTF_MEMBER_ENC(NAME_TBD, 5, 0),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0x\0s\0x\0x\0s\0x\0"),
	},
	.expect = {
		.raw_types = {
			BTF_PTR_ENC(3),					/* [1] ptr -> [3] */
			BTF_STRUCT_ENC(NAME_TBD, 1, 8),			/* [2] struct s   */
				BTF_MEMBER_ENC(NAME_TBD, 1, 0),
			BTF_STRUCT_ENC(NAME_NTH(2), 0, 0),		/* [3] struct x   */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0s\0x"),
	},
	.opts = {
		.force_collisions = true, /* force hash collisions */
	},
},
{
	.descr = "dedup: void equiv check",
	/*
	 * // CU 1:
	 * struct s {
	 *	struct {} *x;
	 * };
	 * // CU 2:
	 * struct s {
	 *	int *x;
	 * };
	 */
	.input = {
		.raw_types = {
			/* CU 1 */
			BTF_STRUCT_ENC(0, 0, 1),				/* [1] struct {}  */
			BTF_PTR_ENC(1),						/* [2] ptr -> [1] */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 8),			/* [3] struct s   */
				BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			/* CU 2 */
			BTF_PTR_ENC(0),						/* [4] ptr -> void */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 8),			/* [5] struct s   */
				BTF_MEMBER_ENC(NAME_NTH(2), 4, 0),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0s\0x"),
	},
	.expect = {
		.raw_types = {
			/* CU 1 */
			BTF_STRUCT_ENC(0, 0, 1),				/* [1] struct {}  */
			BTF_PTR_ENC(1),						/* [2] ptr -> [1] */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 8),			/* [3] struct s   */
				BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			/* CU 2 */
			BTF_PTR_ENC(0),						/* [4] ptr -> void */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 8),			/* [5] struct s   */
				BTF_MEMBER_ENC(NAME_NTH(2), 4, 0),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0s\0x"),
	},
	.opts = {
		.force_collisions = true, /* force hash collisions */
	},
},
{
	.descr = "dedup: all possible kinds (no duplicates)",
	.input = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 8),		/* [1] int */
			BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 2), 4),	/* [2] enum */
				BTF_ENUM_ENC(NAME_TBD, 0),
				BTF_ENUM_ENC(NAME_TBD, 1),
			BTF_FWD_ENC(NAME_TBD, 1 /* union kind_flag */),			/* [3] fwd */
			BTF_TYPE_ARRAY_ENC(2, 1, 7),					/* [4] array */
			BTF_STRUCT_ENC(NAME_TBD, 1, 4),					/* [5] struct */
				BTF_MEMBER_ENC(NAME_TBD, 1, 0),
			BTF_UNION_ENC(NAME_TBD, 1, 4),					/* [6] union */
				BTF_MEMBER_ENC(NAME_TBD, 1, 0),
			BTF_TYPEDEF_ENC(NAME_TBD, 1),					/* [7] typedef */
			BTF_PTR_ENC(0),							/* [8] ptr */
			BTF_CONST_ENC(8),						/* [9] const */
			BTF_VOLATILE_ENC(8),						/* [10] volatile */
			BTF_RESTRICT_ENC(8),						/* [11] restrict */
			BTF_FUNC_PROTO_ENC(1, 2),					/* [12] func_proto */
				BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
				BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 18),
			BTF_FUNC_ENC(NAME_TBD, 12),					/* [13] func */
			BTF_TYPE_FLOAT_ENC(NAME_TBD, 2),				/* [14] float */
			BTF_DECL_TAG_ENC(NAME_TBD, 13, -1),				/* [15] decl_tag */
			BTF_DECL_TAG_ENC(NAME_TBD, 13, 1),				/* [16] decl_tag */
			BTF_DECL_TAG_ENC(NAME_TBD, 7, -1),				/* [17] decl_tag */
			BTF_TYPE_TAG_ENC(NAME_TBD, 8),					/* [18] type_tag */
			BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 2), 8),	/* [19] enum64 */
				BTF_ENUM64_ENC(NAME_TBD, 0, 0),
				BTF_ENUM64_ENC(NAME_TBD, 1, 1),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0N\0O\0P\0Q\0R\0S\0T\0U"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_TBD, BTF_INT_SIGNED, 0, 32, 8),		/* [1] int */
			BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM, 0, 2), 4),	/* [2] enum */
				BTF_ENUM_ENC(NAME_TBD, 0),
				BTF_ENUM_ENC(NAME_TBD, 1),
			BTF_FWD_ENC(NAME_TBD, 1 /* union kind_flag */),			/* [3] fwd */
			BTF_TYPE_ARRAY_ENC(2, 1, 7),					/* [4] array */
			BTF_STRUCT_ENC(NAME_TBD, 1, 4),					/* [5] struct */
				BTF_MEMBER_ENC(NAME_TBD, 1, 0),
			BTF_UNION_ENC(NAME_TBD, 1, 4),					/* [6] union */
				BTF_MEMBER_ENC(NAME_TBD, 1, 0),
			BTF_TYPEDEF_ENC(NAME_TBD, 1),					/* [7] typedef */
			BTF_PTR_ENC(0),							/* [8] ptr */
			BTF_CONST_ENC(8),						/* [9] const */
			BTF_VOLATILE_ENC(8),						/* [10] volatile */
			BTF_RESTRICT_ENC(8),						/* [11] restrict */
			BTF_FUNC_PROTO_ENC(1, 2),					/* [12] func_proto */
				BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 1),
				BTF_FUNC_PROTO_ARG_ENC(NAME_TBD, 18),
			BTF_FUNC_ENC(NAME_TBD, 12),					/* [13] func */
			BTF_TYPE_FLOAT_ENC(NAME_TBD, 2),				/* [14] float */
			BTF_DECL_TAG_ENC(NAME_TBD, 13, -1),				/* [15] decl_tag */
			BTF_DECL_TAG_ENC(NAME_TBD, 13, 1),				/* [16] decl_tag */
			BTF_DECL_TAG_ENC(NAME_TBD, 7, -1),				/* [17] decl_tag */
			BTF_TYPE_TAG_ENC(NAME_TBD, 8),					/* [18] type_tag */
			BTF_TYPE_ENC(NAME_TBD, BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 2), 8),	/* [19] enum64 */
				BTF_ENUM64_ENC(NAME_TBD, 0, 0),
				BTF_ENUM64_ENC(NAME_TBD, 1, 1),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0N\0O\0P\0Q\0R\0S\0T\0U"),
	},
},
{
	.descr = "dedup: no int/float duplicates",
	.input = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 8),
			/* different name */
			BTF_TYPE_INT_ENC(NAME_NTH(2), BTF_INT_SIGNED, 0, 32, 8),
			/* different encoding */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_CHAR, 0, 32, 8),
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_BOOL, 0, 32, 8),
			/* different bit offset */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 8, 32, 8),
			/* different bit size */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 27, 8),
			/* different byte size */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 4),
			/* all allowed sizes */
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 2),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 4),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 8),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 12),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 16),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0int\0some other int\0float"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 8),
			/* different name */
			BTF_TYPE_INT_ENC(NAME_NTH(2), BTF_INT_SIGNED, 0, 32, 8),
			/* different encoding */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_CHAR, 0, 32, 8),
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_BOOL, 0, 32, 8),
			/* different bit offset */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 8, 32, 8),
			/* different bit size */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 27, 8),
			/* different byte size */
			BTF_TYPE_INT_ENC(NAME_NTH(1), BTF_INT_SIGNED, 0, 32, 4),
			/* all allowed sizes */
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 2),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 4),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 8),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 12),
			BTF_TYPE_FLOAT_ENC(NAME_NTH(3), 16),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0int\0some other int\0float"),
	},
},
{
	.descr = "dedup: enum fwd resolution",
	.input = {
		.raw_types = {
			/* [1] fwd enum 'e1' before full enum */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 0), 4),
			/* [2] full enum 'e1' after fwd */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 123),
			/* [3] full enum 'e2' before fwd */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(4), 456),
			/* [4] fwd enum 'e2' after full enum */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 0), 4),
			/* [5] fwd enum with different size, size does not matter for fwd */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 0), 1),
			/* [6] incompatible full enum with different value */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 321),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0e2\0e2_val"),
	},
	.expect = {
		.raw_types = {
			/* [1] full enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 123),
			/* [2] full enum 'e2' */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(4), 456),
			/* [3] incompatible full enum with different value */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 321),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0e2\0e2_val"),
	},
},
{
	.descr = "dedup: datasec and vars pass-through",
	.input = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			/* static int t */
			BTF_VAR_ENC(NAME_NTH(2), 1, 0),			/* [2] */
			/* .bss section */				/* [3] */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
			BTF_VAR_SECINFO_ENC(2, 0, 4),
			/* int, referenced from [5] */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [4] */
			/* another static int t */
			BTF_VAR_ENC(NAME_NTH(2), 4, 0),			/* [5] */
			/* another .bss section */			/* [6] */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
			BTF_VAR_SECINFO_ENC(5, 0, 4),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0.bss\0t"),
	},
	.expect = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			/* static int t */
			BTF_VAR_ENC(NAME_NTH(2), 1, 0),			/* [2] */
			/* .bss section */				/* [3] */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
			BTF_VAR_SECINFO_ENC(2, 0, 4),
			/* another static int t */
			BTF_VAR_ENC(NAME_NTH(2), 1, 0),			/* [4] */
			/* another .bss section */			/* [5] */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_DATASEC, 0, 1), 4),
			BTF_VAR_SECINFO_ENC(4, 0, 4),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0.bss\0t"),
	},
	.opts = {
		.force_collisions = true
	},
},
{
	.descr = "dedup: func/func_arg/var tags",
	.input = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			/* static int t */
			BTF_VAR_ENC(NAME_NTH(1), 1, 0),			/* [2] */
			/* void f(int a1, int a2) */
			BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(2), 1),
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(3), 1),
			BTF_FUNC_ENC(NAME_NTH(4), 3),			/* [4] */
			/* tag -> t */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 2, -1),		/* [5] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 2, -1),		/* [6] */
			/* tag -> func */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 4, -1),		/* [7] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 4, -1),		/* [8] */
			/* tag -> func arg a1 */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 4, 1),		/* [9] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 4, 1),		/* [10] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0t\0a1\0a2\0f\0tag"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_VAR_ENC(NAME_NTH(1), 1, 0),			/* [2] */
			BTF_FUNC_PROTO_ENC(0, 2),			/* [3] */
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(2), 1),
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(3), 1),
			BTF_FUNC_ENC(NAME_NTH(4), 3),			/* [4] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 2, -1),		/* [5] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 4, -1),		/* [6] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 4, 1),		/* [7] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0t\0a1\0a2\0f\0tag"),
	},
},
{
	.descr = "dedup: func/func_param tags",
	.input = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			/* void f(int a1, int a2) */
			BTF_FUNC_PROTO_ENC(0, 2),			/* [2] */
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(1), 1),
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(2), 1),
			BTF_FUNC_ENC(NAME_NTH(3), 2),			/* [3] */
			/* void f(int a1, int a2) */
			BTF_FUNC_PROTO_ENC(0, 2),			/* [4] */
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(1), 1),
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(2), 1),
			BTF_FUNC_ENC(NAME_NTH(3), 4),			/* [5] */
			/* tag -> f: tag1, tag2 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 3, -1),		/* [6] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 3, -1),		/* [7] */
			/* tag -> f/a2: tag1, tag2 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 3, 1),		/* [8] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 3, 1),		/* [9] */
			/* tag -> f: tag1, tag3 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 5, -1),		/* [10] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 5, -1),		/* [11] */
			/* tag -> f/a2: tag1, tag3 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 5, 1),		/* [12] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 5, 1),		/* [13] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0a1\0a2\0f\0tag1\0tag2\0tag3"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_FUNC_PROTO_ENC(0, 2),			/* [2] */
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(1), 1),
				BTF_FUNC_PROTO_ARG_ENC(NAME_NTH(2), 1),
			BTF_FUNC_ENC(NAME_NTH(3), 2),			/* [3] */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 3, -1),		/* [4] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 3, -1),		/* [5] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 3, -1),		/* [6] */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 3, 1),		/* [7] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 3, 1),		/* [8] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 3, 1),		/* [9] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0a1\0a2\0f\0tag1\0tag2\0tag3"),
	},
},
{
	.descr = "dedup: struct/struct_member tags",
	.input = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_STRUCT_ENC(NAME_NTH(1), 2, 8),		/* [2] */
				BTF_MEMBER_ENC(NAME_NTH(2), 1, 0),
				BTF_MEMBER_ENC(NAME_NTH(3), 1, 32),
			BTF_STRUCT_ENC(NAME_NTH(1), 2, 8),		/* [3] */
				BTF_MEMBER_ENC(NAME_NTH(2), 1, 0),
				BTF_MEMBER_ENC(NAME_NTH(3), 1, 32),
			/* tag -> t: tag1, tag2 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 2, -1),		/* [4] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 2, -1),		/* [5] */
			/* tag -> t/m2: tag1, tag2 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 2, 1),		/* [6] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 2, 1),		/* [7] */
			/* tag -> t: tag1, tag3 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 3, -1),		/* [8] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 3, -1),		/* [9] */
			/* tag -> t/m2: tag1, tag3 */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 3, 1),		/* [10] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 3, 1),		/* [11] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0t\0m1\0m2\0tag1\0tag2\0tag3"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_STRUCT_ENC(NAME_NTH(1), 2, 8),		/* [2] */
				BTF_MEMBER_ENC(NAME_NTH(2), 1, 0),
				BTF_MEMBER_ENC(NAME_NTH(3), 1, 32),
			BTF_DECL_TAG_ENC(NAME_NTH(4), 2, -1),		/* [3] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 2, -1),		/* [4] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 2, -1),		/* [5] */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 2, 1),		/* [6] */
			BTF_DECL_TAG_ENC(NAME_NTH(5), 2, 1),		/* [7] */
			BTF_DECL_TAG_ENC(NAME_NTH(6), 2, 1),		/* [8] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0t\0m1\0m2\0tag1\0tag2\0tag3"),
	},
},
{
	.descr = "dedup: typedef tags",
	.input = {
		.raw_types = {
			/* int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPEDEF_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_TYPEDEF_ENC(NAME_NTH(1), 1),		/* [3] */
			/* tag -> t: tag1, tag2 */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 2, -1),		/* [4] */
			BTF_DECL_TAG_ENC(NAME_NTH(3), 2, -1),		/* [5] */
			/* tag -> t: tag1, tag3 */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 3, -1),		/* [6] */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 3, -1),		/* [7] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0t\0tag1\0tag2\0tag3"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPEDEF_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_DECL_TAG_ENC(NAME_NTH(2), 2, -1),		/* [3] */
			BTF_DECL_TAG_ENC(NAME_NTH(3), 2, -1),		/* [4] */
			BTF_DECL_TAG_ENC(NAME_NTH(4), 2, -1),		/* [5] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0t\0tag1\0tag2\0tag3"),
	},
},
{
	.descr = "dedup: btf_type_tag #1",
	.input = {
		.raw_types = {
			/* ptr -> tag2 -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 2),		/* [3] */
			BTF_PTR_ENC(3),					/* [4] */
			/* ptr -> tag2 -> tag1 -> int */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [5] */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 5),		/* [6] */
			BTF_PTR_ENC(6),					/* [7] */
			/* ptr -> tag1 -> int */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [8] */
			BTF_PTR_ENC(8),					/* [9] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0tag2"),
	},
	.expect = {
		.raw_types = {
			/* ptr -> tag2 -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 2),		/* [3] */
			BTF_PTR_ENC(3),					/* [4] */
			/* ptr -> tag1 -> int */
			BTF_PTR_ENC(2),					/* [5] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0tag2"),
	},
},
{
	.descr = "dedup: btf_type_tag #2",
	.input = {
		.raw_types = {
			/* ptr -> tag2 -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 2),		/* [3] */
			BTF_PTR_ENC(3),					/* [4] */
			/* ptr -> tag2 -> int */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 1),		/* [5] */
			BTF_PTR_ENC(5),					/* [6] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0tag2"),
	},
	.expect = {
		.raw_types = {
			/* ptr -> tag2 -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 2),		/* [3] */
			BTF_PTR_ENC(3),					/* [4] */
			/* ptr -> tag2 -> int */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 1),		/* [5] */
			BTF_PTR_ENC(5),					/* [6] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0tag2"),
	},
},
{
	.descr = "dedup: btf_type_tag #3",
	.input = {
		.raw_types = {
			/* ptr -> tag2 -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 2),		/* [3] */
			BTF_PTR_ENC(3),					/* [4] */
			/* ptr -> tag1 -> tag2 -> int */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 1),		/* [5] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 5),		/* [6] */
			BTF_PTR_ENC(6),					/* [7] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0tag2"),
	},
	.expect = {
		.raw_types = {
			/* ptr -> tag2 -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 2),		/* [3] */
			BTF_PTR_ENC(3),					/* [4] */
			/* ptr -> tag1 -> tag2 -> int */
			BTF_TYPE_TAG_ENC(NAME_NTH(2), 1),		/* [5] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 5),		/* [6] */
			BTF_PTR_ENC(6),					/* [7] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0tag2"),
	},
},
{
	.descr = "dedup: btf_type_tag #4",
	.input = {
		.raw_types = {
			/* ptr -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_PTR_ENC(2),					/* [3] */
			/* ptr -> tag1 -> long */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 64, 8),	/* [4] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 4),		/* [5] */
			BTF_PTR_ENC(5),					/* [6] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1"),
	},
	.expect = {
		.raw_types = {
			/* ptr -> tag1 -> int */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),	/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),		/* [2] */
			BTF_PTR_ENC(2),					/* [3] */
			/* ptr -> tag1 -> long */
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 64, 8),	/* [4] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 4),		/* [5] */
			BTF_PTR_ENC(5),					/* [6] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1"),
	},
},
{
	.descr = "dedup: btf_type_tag #5, struct",
	.input = {
		.raw_types = {
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),				/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),					/* [2] */
			BTF_TYPE_ENC(NAME_NTH(2), BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 1), 4),	/* [3] */
			BTF_MEMBER_ENC(NAME_NTH(3), 2, BTF_MEMBER_OFFSET(0, 0)),
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),					/* [4] */
			BTF_TYPE_ENC(NAME_NTH(2), BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 1), 4),	/* [5] */
			BTF_MEMBER_ENC(NAME_NTH(3), 4, BTF_MEMBER_OFFSET(0, 0)),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0t\0m"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4),				/* [1] */
			BTF_TYPE_TAG_ENC(NAME_NTH(1), 1),					/* [2] */
			BTF_TYPE_ENC(NAME_NTH(2), BTF_INFO_ENC(BTF_KIND_STRUCT, 1, 1), 4),	/* [3] */
			BTF_MEMBER_ENC(NAME_NTH(3), 2, BTF_MEMBER_OFFSET(0, 0)),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0tag1\0t\0m"),
	},
},
{
	.descr = "dedup: enum64, standalone",
	.input = {
		.raw_types = {
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 123),
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 123),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val"),
	},
	.expect = {
		.raw_types = {
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 123),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val"),
	},
},
{
	.descr = "dedup: enum64, fwd resolution",
	.input = {
		.raw_types = {
			/* [1] fwd enum64 'e1' before full enum */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 0), 8),
			/* [2] full enum64 'e1' after fwd */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 123),
			/* [3] full enum64 'e2' before fwd */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(4), 0, 456),
			/* [4] fwd enum64 'e2' after full enum */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 0), 8),
			/* [5] incompatible full enum64 with different value */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 0, 321),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0e2\0e2_val"),
	},
	.expect = {
		.raw_types = {
			/* [1] full enum64 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 123),
			/* [2] full enum64 'e2' */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(4), 0, 456),
			/* [3] incompatible full enum64 with different value */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 0, 321),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0e2\0e2_val"),
	},
},
{
	.descr = "dedup: enum and enum64, no dedup",
	.input = {
		.raw_types = {
			/* [1] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			/* [2] enum64 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 4),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 0),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val"),
	},
	.expect = {
		.raw_types = {
			/* [1] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			/* [2] enum64 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 4),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 0),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val"),
	},
},
{
	.descr = "dedup: enum of different size: no dedup",
	.input = {
		.raw_types = {
			/* [1] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			/* [2] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 2),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val"),
	},
	.expect = {
		.raw_types = {
			/* [1] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			/* [2] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 2),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val"),
	},
},
{
	.descr = "dedup: enum fwd to enum64",
	.input = {
		.raw_types = {
			/* [1] enum64 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 0),
			/* [2] enum 'e1' fwd */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 0), 4),
			/* [3] typedef enum 'e1' td */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0), 2),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0td"),
	},
	.expect = {
		.raw_types = {
			/* [1] enum64 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 1), 8),
				BTF_ENUM64_ENC(NAME_NTH(2), 1, 0),
			/* [2] typedef enum 'e1' td */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0), 1),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0td"),
	},
},
{
	.descr = "dedup: enum64 fwd to enum",
	.input = {
		.raw_types = {
			/* [1] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			/* [2] enum64 'e1' fwd */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM64, 0, 0), 8),
			/* [3] typedef enum 'e1' td */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0), 2),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0td"),
	},
	.expect = {
		.raw_types = {
			/* [1] enum 'e1' */
			BTF_TYPE_ENC(NAME_NTH(1), BTF_INFO_ENC(BTF_KIND_ENUM, 0, 1), 4),
				BTF_ENUM_ENC(NAME_NTH(2), 1),
			/* [2] typedef enum 'e1' td */
			BTF_TYPE_ENC(NAME_NTH(3), BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0), 1),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0e1\0e1_val\0td"),
	},
},
{
	.descr = "dedup: standalone fwd declaration struct",
	/*
	 * Verify that CU1:foo and CU2:foo would be unified and that
	 * typedef/ptr would be updated to point to CU1:foo.
	 *
	 * // CU 1:
	 * struct foo { int x; };
	 *
	 * // CU 2:
	 * struct foo;
	 * typedef struct foo *foo_ptr;
	 */
	.input = {
		.raw_types = {
			/* CU 1 */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 4),             /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			/* CU 2 */
			BTF_FWD_ENC(NAME_NTH(1), 0),                   /* [3] */
			BTF_PTR_ENC(3),                                /* [4] */
			BTF_TYPEDEF_ENC(NAME_NTH(3), 4),               /* [5] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0foo_ptr"),
	},
	.expect = {
		.raw_types = {
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 4),             /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			BTF_PTR_ENC(1),                                /* [3] */
			BTF_TYPEDEF_ENC(NAME_NTH(3), 3),               /* [4] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0foo_ptr"),
	},
},
{
	.descr = "dedup: standalone fwd declaration union",
	/*
	 * Verify that CU1:foo and CU2:foo would be unified and that
	 * typedef/ptr would be updated to point to CU1:foo.
	 * Same as "dedup: standalone fwd declaration struct" but for unions.
	 *
	 * // CU 1:
	 * union foo { int x; };
	 *
	 * // CU 2:
	 * union foo;
	 * typedef union foo *foo_ptr;
	 */
	.input = {
		.raw_types = {
			/* CU 1 */
			BTF_UNION_ENC(NAME_NTH(1), 1, 4),              /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			/* CU 2 */
			BTF_FWD_ENC(NAME_TBD, 1),                      /* [3] */
			BTF_PTR_ENC(3),                                /* [4] */
			BTF_TYPEDEF_ENC(NAME_NTH(3), 4),               /* [5] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0foo_ptr"),
	},
	.expect = {
		.raw_types = {
			BTF_UNION_ENC(NAME_NTH(1), 1, 4),              /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			BTF_PTR_ENC(1),                                /* [3] */
			BTF_TYPEDEF_ENC(NAME_NTH(3), 3),               /* [4] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0foo_ptr"),
	},
},
{
	.descr = "dedup: standalone fwd declaration wrong kind",
	/*
	 * Negative test for btf_dedup_resolve_fwds:
	 * - CU1:foo is a struct, C2:foo is a union, thus CU2:foo is not deduped;
	 * - typedef/ptr should remain unchanged as well.
	 *
	 * // CU 1:
	 * struct foo { int x; };
	 *
	 * // CU 2:
	 * union foo;
	 * typedef union foo *foo_ptr;
	 */
	.input = {
		.raw_types = {
			/* CU 1 */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 4),             /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			/* CU 2 */
			BTF_FWD_ENC(NAME_NTH(3), 1),                   /* [3] */
			BTF_PTR_ENC(3),                                /* [4] */
			BTF_TYPEDEF_ENC(NAME_NTH(3), 4),               /* [5] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0foo_ptr"),
	},
	.expect = {
		.raw_types = {
			/* CU 1 */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 4),             /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			/* CU 2 */
			BTF_FWD_ENC(NAME_NTH(3), 1),                   /* [3] */
			BTF_PTR_ENC(3),                                /* [4] */
			BTF_TYPEDEF_ENC(NAME_NTH(3), 4),               /* [5] */
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0foo_ptr"),
	},
},
{
	.descr = "dedup: standalone fwd declaration name conflict",
	/*
	 * Negative test for btf_dedup_resolve_fwds:
	 * - two candidates for CU2:foo dedup, thus it is unchanged;
	 * - typedef/ptr should remain unchanged as well.
	 *
	 * // CU 1:
	 * struct foo { int x; };
	 *
	 * // CU 2:
	 * struct foo;
	 * typedef struct foo *foo_ptr;
	 *
	 * // CU 3:
	 * struct foo { int x; int y; };
	 */
	.input = {
		.raw_types = {
			/* CU 1 */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 4),             /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			/* CU 2 */
			BTF_FWD_ENC(NAME_NTH(1), 0),                   /* [3] */
			BTF_PTR_ENC(3),                                /* [4] */
			BTF_TYPEDEF_ENC(NAME_NTH(4), 4),               /* [5] */
			/* CU 3 */
			BTF_STRUCT_ENC(NAME_NTH(1), 2, 8),             /* [6] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_MEMBER_ENC(NAME_NTH(3), 2, 0),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0y\0foo_ptr"),
	},
	.expect = {
		.raw_types = {
			/* CU 1 */
			BTF_STRUCT_ENC(NAME_NTH(1), 1, 4),             /* [1] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_TYPE_INT_ENC(0, BTF_INT_SIGNED, 0, 32, 4), /* [2] */
			/* CU 2 */
			BTF_FWD_ENC(NAME_NTH(1), 0),                   /* [3] */
			BTF_PTR_ENC(3),                                /* [4] */
			BTF_TYPEDEF_ENC(NAME_NTH(4), 4),               /* [5] */
			/* CU 3 */
			BTF_STRUCT_ENC(NAME_NTH(1), 2, 8),             /* [6] */
			BTF_MEMBER_ENC(NAME_NTH(2), 2, 0),
			BTF_MEMBER_ENC(NAME_NTH(3), 2, 0),
			BTF_END_RAW,
		},
		BTF_STR_SEC("\0foo\0x\0y\0foo_ptr"),
	},
},
};

static int btf_type_size(const struct btf_type *t)
{
	int base_size = sizeof(struct btf_type);
	__u16 vlen = BTF_INFO_VLEN(t->info);
	__u16 kind = BTF_INFO_KIND(t->info);

	switch (kind) {
	case BTF_KIND_FWD:
	case BTF_KIND_CONST:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_PTR:
	case BTF_KIND_TYPEDEF:
	case BTF_KIND_FUNC:
	case BTF_KIND_FLOAT:
	case BTF_KIND_TYPE_TAG:
		return base_size;
	case BTF_KIND_INT:
		return base_size + sizeof(__u32);
	case BTF_KIND_ENUM:
		return base_size + vlen * sizeof(struct btf_enum);
	case BTF_KIND_ENUM64:
		return base_size + vlen * sizeof(struct btf_enum64);
	case BTF_KIND_ARRAY:
		return base_size + sizeof(struct btf_array);
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		return base_size + vlen * sizeof(struct btf_member);
	case BTF_KIND_FUNC_PROTO:
		return base_size + vlen * sizeof(struct btf_param);
	case BTF_KIND_VAR:
		return base_size + sizeof(struct btf_var);
	case BTF_KIND_DATASEC:
		return base_size + vlen * sizeof(struct btf_var_secinfo);
	case BTF_KIND_DECL_TAG:
		return base_size + sizeof(struct btf_decl_tag);
	default:
		fprintf(stderr, "Unsupported BTF_KIND:%u\n", kind);
		return -EINVAL;
	}
}

static void dump_btf_strings(const char *strs, __u32 len)
{
	const char *cur = strs;
	int i = 0;

	while (cur < strs + len) {
		fprintf(stderr, "string #%d: '%s'\n", i, cur);
		cur += strlen(cur) + 1;
		i++;
	}
}

static void do_test_dedup(unsigned int test_num)
{
	struct btf_dedup_test *test = &dedup_tests[test_num - 1];
	__u32 test_nr_types, expect_nr_types, test_btf_size, expect_btf_size;
	const struct btf_header *test_hdr, *expect_hdr;
	struct btf *test_btf = NULL, *expect_btf = NULL;
	const void *test_btf_data, *expect_btf_data;
	const char *ret_test_next_str, *ret_expect_next_str;
	const char *test_strs, *expect_strs;
	const char *test_str_cur;
	const char *expect_str_cur, *expect_str_end;
	unsigned int raw_btf_size;
	void *raw_btf;
	int err = 0, i;

	if (!test__start_subtest(test->descr))
		return;

	raw_btf = btf_raw_create(&hdr_tmpl, test->input.raw_types,
				 test->input.str_sec, test->input.str_sec_size,
				 &raw_btf_size, &ret_test_next_str);
	if (!raw_btf)
		return;

	test_btf = btf__new((__u8 *)raw_btf, raw_btf_size);
	err = libbpf_get_error(test_btf);
	free(raw_btf);
	if (CHECK(err, "invalid test_btf errno:%d", err)) {
		err = -1;
		goto done;
	}

	raw_btf = btf_raw_create(&hdr_tmpl, test->expect.raw_types,
				 test->expect.str_sec,
				 test->expect.str_sec_size,
				 &raw_btf_size, &ret_expect_next_str);
	if (!raw_btf)
		return;
	expect_btf = btf__new((__u8 *)raw_btf, raw_btf_size);
	err = libbpf_get_error(expect_btf);
	free(raw_btf);
	if (CHECK(err, "invalid expect_btf errno:%d", err)) {
		err = -1;
		goto done;
	}

	test->opts.sz = sizeof(test->opts);
	err = btf__dedup(test_btf, &test->opts);
	if (CHECK(err, "btf_dedup failed errno:%d", err)) {
		err = -1;
		goto done;
	}

	test_btf_data = btf__raw_data(test_btf, &test_btf_size);
	expect_btf_data = btf__raw_data(expect_btf, &expect_btf_size);
	if (CHECK(test_btf_size != expect_btf_size,
		  "test_btf_size:%u != expect_btf_size:%u",
		  test_btf_size, expect_btf_size)) {
		err = -1;
		goto done;
	}

	test_hdr = test_btf_data;
	test_strs = test_btf_data + sizeof(*test_hdr) + test_hdr->str_off;
	expect_hdr = expect_btf_data;
	expect_strs = expect_btf_data + sizeof(*test_hdr) + expect_hdr->str_off;
	if (CHECK(test_hdr->str_len != expect_hdr->str_len,
		  "test_hdr->str_len:%u != expect_hdr->str_len:%u",
		  test_hdr->str_len, expect_hdr->str_len)) {
		fprintf(stderr, "\ntest strings:\n");
		dump_btf_strings(test_strs, test_hdr->str_len);
		fprintf(stderr, "\nexpected strings:\n");
		dump_btf_strings(expect_strs, expect_hdr->str_len);
		err = -1;
		goto done;
	}

	expect_str_cur = expect_strs;
	expect_str_end = expect_strs + expect_hdr->str_len;
	while (expect_str_cur < expect_str_end) {
		size_t test_len, expect_len;
		int off;

		off = btf__find_str(test_btf, expect_str_cur);
		if (CHECK(off < 0, "exp str '%s' not found: %d\n", expect_str_cur, off)) {
			err = -1;
			goto done;
		}
		test_str_cur = btf__str_by_offset(test_btf, off);

		test_len = strlen(test_str_cur);
		expect_len = strlen(expect_str_cur);
		if (CHECK(test_len != expect_len,
			  "test_len:%zu != expect_len:%zu "
			  "(test_str:%s, expect_str:%s)",
			  test_len, expect_len, test_str_cur, expect_str_cur)) {
			err = -1;
			goto done;
		}
		if (CHECK(strcmp(test_str_cur, expect_str_cur),
			  "test_str:%s != expect_str:%s",
			  test_str_cur, expect_str_cur)) {
			err = -1;
			goto done;
		}
		expect_str_cur += expect_len + 1;
	}

	test_nr_types = btf__type_cnt(test_btf);
	expect_nr_types = btf__type_cnt(expect_btf);
	if (CHECK(test_nr_types != expect_nr_types,
		  "test_nr_types:%u != expect_nr_types:%u",
		  test_nr_types, expect_nr_types)) {
		err = -1;
		goto done;
	}

	for (i = 1; i < test_nr_types; i++) {
		const struct btf_type *test_type, *expect_type;
		int test_size, expect_size;

		test_type = btf__type_by_id(test_btf, i);
		expect_type = btf__type_by_id(expect_btf, i);
		test_size = btf_type_size(test_type);
		expect_size = btf_type_size(expect_type);

		if (CHECK(test_size != expect_size,
			  "type #%d: test_size:%d != expect_size:%u",
			  i, test_size, expect_size)) {
			err = -1;
			goto done;
		}
		if (CHECK(btf_kind(test_type) != btf_kind(expect_type),
			  "type %d kind: exp %d != got %u\n",
			  i, btf_kind(expect_type), btf_kind(test_type))) {
			err = -1;
			goto done;
		}
		if (CHECK(test_type->info != expect_type->info,
			  "type %d info: exp %d != got %u\n",
			  i, expect_type->info, test_type->info)) {
			err = -1;
			goto done;
		}
		if (CHECK(test_type->size != expect_type->size,
			  "type %d size/type: exp %d != got %u\n",
			  i, expect_type->size, test_type->size)) {
			err = -1;
			goto done;
		}
	}

done:
	btf__free(test_btf);
	btf__free(expect_btf);
}

void test_btf(void)
{
	int i;

	always_log = env.verbosity > VERBOSE_NONE;

	for (i = 1; i <= ARRAY_SIZE(raw_tests); i++)
		do_test_raw(i);
	for (i = 1; i <= ARRAY_SIZE(get_info_tests); i++)
		do_test_get_info(i);
	for (i = 1; i <= ARRAY_SIZE(file_tests); i++)
		do_test_file(i);
	for (i = 1; i <= ARRAY_SIZE(info_raw_tests); i++)
		do_test_info_raw(i);
	for (i = 1; i <= ARRAY_SIZE(dedup_tests); i++)
		do_test_dedup(i);
	test_pprint();
}
