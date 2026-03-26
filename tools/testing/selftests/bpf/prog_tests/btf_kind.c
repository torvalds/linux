// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026, Oracle and/or its affiliates. */

#include <test_progs.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>

/* Verify kind encoding exists for each kind */
static void test_btf_kind_encoding(void)
{
	LIBBPF_OPTS(btf_new_opts, opts);
	const struct btf_header *hdr;
	const void *raw_btf;
	struct btf *btf;
	__u32 raw_size;

	opts.add_layout = true;
	btf = btf__new_empty_opts(&opts);
	if (!ASSERT_OK_PTR(btf, "btf_new"))
		return;

	raw_btf = btf__raw_data(btf, &raw_size);
	if (!ASSERT_OK_PTR(raw_btf, "btf__raw_data"))
		return;

	hdr = raw_btf;

	ASSERT_EQ(hdr->layout_off % 4, 0, "layout_aligned");
	ASSERT_EQ(hdr->layout_len, sizeof(struct btf_layout) * NR_BTF_KINDS,
		  "layout_len");
	ASSERT_EQ(hdr->str_off, hdr->layout_off + hdr->layout_len, "str_after_layout");
	btf__free(btf);

	opts.add_layout = false;
	btf = btf__new_empty_opts(&opts);
	if (!ASSERT_OK_PTR(btf, "btf_new"))
		return;

	raw_btf = btf__raw_data(btf, &raw_size);
	if (!ASSERT_OK_PTR(raw_btf, "btf__raw_data"))
		return;

	hdr = raw_btf;

	ASSERT_EQ(hdr->layout_off, 0, "no_layout_off");
	ASSERT_EQ(hdr->layout_len, 0, "no_layout_len");
	ASSERT_EQ(hdr->str_off, hdr->type_off + hdr->type_len, "strs_after_types");
	btf__free(btf);
}

static int write_raw_btf(void *raw_btf, size_t raw_size, char *file)
{
	int fd = mkstemp(file);
	ssize_t n;

	if (!ASSERT_OK_FD(fd, "open_raw_btf"))
		return -1;
	n = write(fd, raw_btf, raw_size);
	close(fd);
	if (!ASSERT_EQ(n, (ssize_t)raw_size, "write_raw_btf"))
		return -1;
	return 0;
}

/*
 * Fabricate an unrecognized kind at BTF_KIND_MAX + 1, and after adding
 * the appropriate struct/typedefs to the BTF such that it recognizes
 * this kind, ensure that parsing of BTF containing the unrecognized kind
 * can succeed.
 */
void test_btf_kind_decoding(void)
{
	char btf_kind_file1[] = "/tmp/test_btf_kind.XXXXXX";
	char btf_kind_file2[] = "/tmp/test_btf_kind.XXXXXX";
	char btf_kind_file3[] = "/tmp/test_btf_kind.XXXXXX";
	struct btf *btf = NULL, *new_btf = NULL;
	__s32 int_id, unrec_id, id, id2;
	LIBBPF_OPTS(btf_new_opts, opts);
	struct btf_layout *l;
	struct btf_header *hdr;
	const void *raw_btf;
	struct btf_type *t;
	void *new_raw_btf;
	void *str_data;
	__u32 raw_size;

	opts.add_layout = true;
	btf = btf__new_empty_opts(&opts);
	if (!ASSERT_OK_PTR(btf, "btf_new"))
		return;

	int_id = btf__add_int(btf, "test_char", 1, BTF_INT_CHAR);
	if (!ASSERT_GT(int_id, 0, "add_int_id"))
		return;

	/*
	 * Create our type with unrecognized kind by adding a typedef kind
	 * we will overwrite it with our unrecognized kind value.
	 */
	unrec_id = btf__add_typedef(btf, "unrec_kind", int_id);
	if (!ASSERT_GT(unrec_id, 0, "add_unrec_id"))
		return;

	/*
	 * Add an id after it that we will look up to verify we can parse
	 * beyond unrecognized kinds.
	 */
	id = btf__add_typedef(btf, "test_lookup", int_id);
	if (!ASSERT_GT(id, 0, "add_test_lookup_id"))
		return;
	id2 = btf__add_typedef(btf, "test_lookup2", int_id);
	if (!ASSERT_GT(id2, 0, "add_test_lookup_id2"))
		return;

	raw_btf = (void *)btf__raw_data(btf, &raw_size);
	if (!ASSERT_OK_PTR(raw_btf, "btf__raw_data"))
		return;

	new_raw_btf = calloc(1, raw_size + sizeof(*l));
	if (!ASSERT_OK_PTR(new_raw_btf, "calloc_raw_btf"))
		return;
	memcpy(new_raw_btf, raw_btf, raw_size);

	hdr = new_raw_btf;

	/* Move strings to make space for one new layout description */
	raw_size += sizeof(*l);
	str_data = new_raw_btf + hdr->hdr_len + hdr->str_off;
	memmove(str_data + sizeof(*l), str_data, hdr->str_len);
	hdr->str_off += sizeof(*l);

	/* Add new layout description */
	hdr->layout_len += sizeof(*l);
	l = new_raw_btf + hdr->hdr_len + hdr->layout_off;
	l[NR_BTF_KINDS].info_sz = 0;
	l[NR_BTF_KINDS].elem_sz = 0;
	l[NR_BTF_KINDS].flags = 0;

	/* Now modify typedef added above to be an unrecognized kind. */
	t = (void *)hdr + hdr->hdr_len + hdr->type_off + sizeof(struct btf_type) +
		sizeof(__u32);
	t->info = (NR_BTF_KINDS << 24);

	/* Write BTF to a raw file, ready for parsing. */
	if (write_raw_btf(new_raw_btf, raw_size, btf_kind_file1))
		goto out;

	/*
	 * Verify parsing succeeds, and that we can read type info past
	 * the unrecognized kind.
	 */
	new_btf = btf__parse_raw(btf_kind_file1);
	if (ASSERT_OK_PTR(new_btf, "btf__parse_raw")) {
		ASSERT_EQ(btf__find_by_name(new_btf, "unrec_kind"), unrec_id,
			  "unrec_kind_found");
		ASSERT_EQ(btf__find_by_name_kind(new_btf, "test_lookup",
						 BTF_KIND_TYPEDEF), id,
			  "verify_id_lookup");
		ASSERT_EQ(btf__find_by_name_kind(new_btf, "test_lookup2",
						 BTF_KIND_TYPEDEF), id2,
			  "verify_id2_lookup");
	}
	btf__free(new_btf);
	new_btf = NULL;

	/*
	 * Next, change info_sz to equal sizeof(struct btf_type); this means the
	 * "test_lookup" kind will be reinterpreted as a singular info element
	 * following the unrecognized kind.
	 */
	l[NR_BTF_KINDS].info_sz = sizeof(struct btf_type);
	if (write_raw_btf(new_raw_btf, raw_size, btf_kind_file2))
		goto out;

	new_btf = btf__parse_raw(btf_kind_file2);
	if (ASSERT_OK_PTR(new_btf, "btf__parse_raw")) {
		ASSERT_EQ(btf__find_by_name_kind(new_btf, "test_lookup",
						 BTF_KIND_TYPEDEF), -ENOENT,
			  "verify_id_not_found");
		/* id of "test_lookup2" will be id2 -1 as we have removed one type */
		ASSERT_EQ(btf__find_by_name_kind(new_btf, "test_lookup2",
						 BTF_KIND_TYPEDEF), id2 - 1,
			  "verify_id_lookup2");

	}
	btf__free(new_btf);
	new_btf = NULL;

	/*
	 * Change elem_sz to equal sizeof(struct btf_type) and set vlen
	 * associated with unrecognized type to 1; this allows us to verify
	 * vlen-specified BTF can still be parsed.
	 */
	l[NR_BTF_KINDS].info_sz = 0;
	l[NR_BTF_KINDS].elem_sz = sizeof(struct btf_type);
	t->info |= 1;
	if (write_raw_btf(new_raw_btf, raw_size, btf_kind_file3))
		goto out;

	new_btf = btf__parse_raw(btf_kind_file3);
	if (ASSERT_OK_PTR(new_btf, "btf__parse_raw")) {
		ASSERT_EQ(btf__find_by_name_kind(new_btf, "test_lookup",
						 BTF_KIND_TYPEDEF), -ENOENT,
			  "verify_id_not_found");
		/* id of "test_lookup2" will be id2 -1 as we have removed one type */
		ASSERT_EQ(btf__find_by_name_kind(new_btf, "test_lookup2",
						 BTF_KIND_TYPEDEF), id2 - 1,
			  "verify_id_lookup2");

	}
out:
	btf__free(new_btf);
	free(new_raw_btf);
	unlink(btf_kind_file1);
	unlink(btf_kind_file2);
	unlink(btf_kind_file3);
	btf__free(btf);
}

void test_btf_kind(void)
{
	if (test__start_subtest("btf_kind_encoding"))
		test_btf_kind_encoding();
	if (test__start_subtest("btf_kind_decoding"))
		test_btf_kind_decoding();
}
