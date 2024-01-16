// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <string.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include <linux/btf.h>
#include <linux/kernel.h>
#define CONFIG_DEBUG_INFO_BTF
#include <linux/btf_ids.h>
#include "test_progs.h"

static int duration;

struct symbol {
	const char	*name;
	int		 type;
	int		 id;
};

struct symbol test_symbols[] = {
	{ "unused",  BTF_KIND_UNKN,     0 },
	{ "S",       BTF_KIND_TYPEDEF, -1 },
	{ "T",       BTF_KIND_TYPEDEF, -1 },
	{ "U",       BTF_KIND_TYPEDEF, -1 },
	{ "S",       BTF_KIND_STRUCT,  -1 },
	{ "U",       BTF_KIND_UNION,   -1 },
	{ "func",    BTF_KIND_FUNC,    -1 },
};

/* Align the .BTF_ids section to 4 bytes */
asm (
".pushsection " BTF_IDS_SECTION " ,\"a\"; \n"
".balign 4, 0;                            \n"
".popsection;                             \n");

BTF_ID_LIST(test_list_local)
BTF_ID_UNUSED
BTF_ID(typedef, S)
BTF_ID(typedef, T)
BTF_ID(typedef, U)
BTF_ID(struct,  S)
BTF_ID(union,   U)
BTF_ID(func,    func)

extern __u32 test_list_global[];
BTF_ID_LIST_GLOBAL(test_list_global, 1)
BTF_ID_UNUSED
BTF_ID(typedef, S)
BTF_ID(typedef, T)
BTF_ID(typedef, U)
BTF_ID(struct,  S)
BTF_ID(union,   U)
BTF_ID(func,    func)

BTF_SET_START(test_set)
BTF_ID(typedef, S)
BTF_ID(typedef, T)
BTF_ID(typedef, U)
BTF_ID(struct,  S)
BTF_ID(union,   U)
BTF_ID(func,    func)
BTF_SET_END(test_set)

static int
__resolve_symbol(struct btf *btf, int type_id)
{
	const struct btf_type *type;
	const char *str;
	unsigned int i;

	type = btf__type_by_id(btf, type_id);
	if (!type) {
		PRINT_FAIL("Failed to get type for ID %d\n", type_id);
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(test_symbols); i++) {
		if (test_symbols[i].id >= 0)
			continue;

		if (BTF_INFO_KIND(type->info) != test_symbols[i].type)
			continue;

		str = btf__name_by_offset(btf, type->name_off);
		if (!str) {
			PRINT_FAIL("Failed to get name for BTF ID %d\n", type_id);
			return -1;
		}

		if (!strcmp(str, test_symbols[i].name))
			test_symbols[i].id = type_id;
	}

	return 0;
}

static int resolve_symbols(void)
{
	struct btf *btf;
	int type_id;
	__u32 nr;

	btf = btf__parse_elf("btf_data.bpf.o", NULL);
	if (CHECK(libbpf_get_error(btf), "resolve",
		  "Failed to load BTF from btf_data.o\n"))
		return -1;

	nr = btf__type_cnt(btf);

	for (type_id = 1; type_id < nr; type_id++) {
		if (__resolve_symbol(btf, type_id))
			break;
	}

	btf__free(btf);
	return 0;
}

void test_resolve_btfids(void)
{
	__u32 *test_list, *test_lists[] = { test_list_local, test_list_global };
	unsigned int i, j;
	int ret = 0;

	if (resolve_symbols())
		return;

	/* Check BTF_ID_LIST(test_list_local) and
	 * BTF_ID_LIST_GLOBAL(test_list_global) IDs
	 */
	for (j = 0; j < ARRAY_SIZE(test_lists); j++) {
		test_list = test_lists[j];
		for (i = 0; i < ARRAY_SIZE(test_symbols); i++) {
			ret = CHECK(test_list[i] != test_symbols[i].id,
				    "id_check",
				    "wrong ID for %s (%d != %d)\n",
				    test_symbols[i].name,
				    test_list[i], test_symbols[i].id);
			if (ret)
				return;
		}
	}

	/* Check BTF_SET_START(test_set) IDs */
	for (i = 0; i < test_set.cnt; i++) {
		bool found = false;

		for (j = 0; j < ARRAY_SIZE(test_symbols); j++) {
			if (test_symbols[j].id != test_set.ids[i])
				continue;
			found = true;
			break;
		}

		ret = CHECK(!found, "id_check",
			    "ID %d not found in test_symbols\n",
			    test_set.ids[i]);
		if (ret)
			break;

		if (i > 0) {
			if (!ASSERT_LE(test_set.ids[i - 1], test_set.ids[i], "sort_check"))
				return;
		}
	}
}
