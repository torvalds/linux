// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#define _GNU_SOURCE
#include <string.h>
#include <byteswap.h>
#include <test_progs.h>
#include <bpf/btf.h>

void test_btf_endian() {
#if __BYTE_ORDER == __LITTLE_ENDIAN
	enum btf_endianness endian = BTF_LITTLE_ENDIAN;
#elif __BYTE_ORDER == __BIG_ENDIAN
	enum btf_endianness endian = BTF_BIG_ENDIAN;
#else
#error "Unrecognized __BYTE_ORDER"
#endif
	enum btf_endianness swap_endian = 1 - endian;
	struct btf *btf = NULL, *swap_btf = NULL;
	const void *raw_data, *swap_raw_data;
	const struct btf_type *t;
	const struct btf_header *hdr;
	__u32 raw_sz, swap_raw_sz;
	int var_id;

	/* Load BTF in native endianness */
	btf = btf__parse_elf("btf_dump_test_case_syntax.o", NULL);
	if (!ASSERT_OK_PTR(btf, "parse_native_btf"))
		goto err_out;

	ASSERT_EQ(btf__endianness(btf), endian, "endian");
	btf__set_endianness(btf, swap_endian);
	ASSERT_EQ(btf__endianness(btf), swap_endian, "endian");

	/* Get raw BTF data in non-native endianness... */
	raw_data = btf__get_raw_data(btf, &raw_sz);
	if (!ASSERT_OK_PTR(raw_data, "raw_data_inverted"))
		goto err_out;

	/* ...and open it as a new BTF instance */
	swap_btf = btf__new(raw_data, raw_sz);
	if (!ASSERT_OK_PTR(swap_btf, "parse_swap_btf"))
		goto err_out;

	ASSERT_EQ(btf__endianness(swap_btf), swap_endian, "endian");
	ASSERT_EQ(btf__get_nr_types(swap_btf), btf__get_nr_types(btf), "nr_types");

	swap_raw_data = btf__get_raw_data(swap_btf, &swap_raw_sz);
	if (!ASSERT_OK_PTR(swap_raw_data, "swap_raw_data"))
		goto err_out;

	/* both raw data should be identical (with non-native endianness) */
	ASSERT_OK(memcmp(raw_data, swap_raw_data, raw_sz), "mem_identical");

	/* make sure that at least BTF header data is really swapped */
	hdr = swap_raw_data;
	ASSERT_EQ(bswap_16(hdr->magic), BTF_MAGIC, "btf_magic_swapped");
	ASSERT_EQ(raw_sz, swap_raw_sz, "raw_sizes");

	/* swap it back to native endianness */
	btf__set_endianness(swap_btf, endian);
	swap_raw_data = btf__get_raw_data(swap_btf, &swap_raw_sz);
	if (!ASSERT_OK_PTR(swap_raw_data, "swap_raw_data"))
		goto err_out;

	/* now header should have native BTF_MAGIC */
	hdr = swap_raw_data;
	ASSERT_EQ(hdr->magic, BTF_MAGIC, "btf_magic_native");
	ASSERT_EQ(raw_sz, swap_raw_sz, "raw_sizes");

	/* now modify original BTF */
	var_id = btf__add_var(btf, "some_var", BTF_VAR_GLOBAL_ALLOCATED, 1);
	ASSERT_GT(var_id, 0, "var_id");

	btf__free(swap_btf);
	swap_btf = NULL;

	btf__set_endianness(btf, swap_endian);
	raw_data = btf__get_raw_data(btf, &raw_sz);
	if (!ASSERT_OK_PTR(raw_data, "raw_data_inverted"))
		goto err_out;

	/* and re-open swapped raw data again */
	swap_btf = btf__new(raw_data, raw_sz);
	if (!ASSERT_OK_PTR(swap_btf, "parse_swap_btf"))
		goto err_out;

	ASSERT_EQ(btf__endianness(swap_btf), swap_endian, "endian");
	ASSERT_EQ(btf__get_nr_types(swap_btf), btf__get_nr_types(btf), "nr_types");

	/* the type should appear as if it was stored in native endianness */
	t = btf__type_by_id(swap_btf, var_id);
	ASSERT_STREQ(btf__str_by_offset(swap_btf, t->name_off), "some_var", "var_name");
	ASSERT_EQ(btf_var(t)->linkage, BTF_VAR_GLOBAL_ALLOCATED, "var_linkage");
	ASSERT_EQ(t->type, 1, "var_type");

err_out:
	btf__free(btf);
	btf__free(swap_btf);
}
