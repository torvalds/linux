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

#define BTF_DATA_FILE "resolve_btfids.test.o.BTF"

#define DECL_TAG_FASTCALL "bpf_fastcall"
#define DECL_TAG_KFUNC "bpf_kfunc"
#define TYPE_ATTR_ARENA "address_space(1)"
#define ARENA_ARG(n) (1U << (n))

#ifndef KF_FASTCALL
#define KF_FASTCALL (1 << 12)
#endif
#ifndef KF_ARENA_RET
#define KF_ARENA_RET  (1 << 13)
#endif
#ifndef KF_ARENA_ARG1
#define KF_ARENA_ARG1 (1 << 14)
#endif
#ifndef KF_ARENA_ARG2
#define KF_ARENA_ARG2 (1 << 15)
#endif

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

struct kfunc_symbol {
	const char	*name;
	s32		 id;
	u32		 flags;
	u32		 arena_args;
	bool		 arena_ret;
};

static struct kfunc_symbol kfunc_symbols[] = {
	{ "kfunc_a", -1, 0, 0, false },
	{ "kfunc_b", -1, KF_FASTCALL, 0, false },
	{ "kfunc_c", -1, KF_ARENA_RET | KF_ARENA_ARG1 | KF_ARENA_ARG2,
	  ARENA_ARG(0) | ARENA_ARG(1), true },
	{ "kfunc_d", -1, KF_ARENA_ARG2, ARENA_ARG(1), false },
	{ "kfunc_e", -1, 0, ARENA_ARG(0) | ARENA_ARG(1) | ARENA_ARG(2) |
	  ARENA_ARG(3) | ARENA_ARG(4), false },
	{ "kfunc_f", -1, 0, ARENA_ARG(1), false },
	{ "kfunc_g", -1, KF_ARENA_RET, ARENA_ARG(0) | ARENA_ARG(1), true },
};

/* Align the .BTF_ids section to 4 bytes */
asm (
".pushsection " BTF_IDS_SECTION " ,\"a\"; \n"
".balign 4, 0;                            \n"
".popsection;                             \n");

/*
 * test_list_local, test_set and test_kfunc_set are .local symbols placed
 * in .BTF_ids by inline asm, and are read here directly by C name. To the
 * compiler they are plain, default-visibility extern objects.
 *
 * When test_progs is linked as a position-independent executable (PIE),
 * taking the address of such an extern is routed through the GOT. The
 * GNU assembler on aarch64 unconditionally converts references to .local
 * symbols into section + addend form (".BTF_ids + <offset>"), but a GOT
 * slot cannot carry an addend (the AArch64 ELF spec mandates zero), so
 * the linker resolves it to the .BTF_ids base.
 *
 * Mark them hidden so the compiler treats them as non-interposable and
 * emits a direct, addend-preserving PC-relative access instead of a GOT
 * load, in both PIE and non-PIE builds. test_list_global is .globl and
 * not affected, so it is left at default visibility.
 */
#pragma GCC visibility push(hidden)
BTF_ID_LIST(test_list_local)
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

BTF_KFUNCS_START(test_kfunc_set)
BTF_ID_FLAGS(func, kfunc_a)
BTF_ID_FLAGS(func, kfunc_b, KF_FASTCALL)
BTF_ID_FLAGS(func, kfunc_c, KF_ARENA_RET | KF_ARENA_ARG1 | KF_ARENA_ARG2)
BTF_ID_FLAGS(func, kfunc_d, KF_ARENA_ARG2)
BTF_ID_FLAGS(func, kfunc_e)
BTF_ID_FLAGS(func, kfunc_f)
BTF_ID_FLAGS(func, kfunc_g, KF_ARENA_RET)
BTF_KFUNCS_END(test_kfunc_set)

/*
 * Same kfuncs in reverse declaration order, so resolve_btfids has to
 * actually sort at least one of the two sets.
 */
BTF_KFUNCS_START(test_kfunc_set_rev)
BTF_ID_FLAGS(func, kfunc_g, KF_ARENA_RET)
BTF_ID_FLAGS(func, kfunc_f)
BTF_ID_FLAGS(func, kfunc_e)
BTF_ID_FLAGS(func, kfunc_d, KF_ARENA_ARG2)
BTF_ID_FLAGS(func, kfunc_c, KF_ARENA_RET | KF_ARENA_ARG1 | KF_ARENA_ARG2)
BTF_ID_FLAGS(func, kfunc_b, KF_FASTCALL)
BTF_ID_FLAGS(func, kfunc_a)
BTF_KFUNCS_END(test_kfunc_set_rev)
#pragma GCC visibility pop

extern __u32 test_list_global[];
BTF_ID_LIST_GLOBAL(test_list_global, 1)
BTF_ID_UNUSED
BTF_ID(typedef, S)
BTF_ID(typedef, T)
BTF_ID(typedef, U)
BTF_ID(struct,  S)
BTF_ID(union,   U)
BTF_ID(func,    func)

static int
__resolve_symbol(struct btf *btf, int type_id)
{
	const struct btf_type *type;
	const char *str;
	unsigned int i;

	type = btf__type_by_id(btf, type_id);
	if (!ASSERT_OK_PTR(type, "btf__type_by_id"))
		return -1;

	str = btf__name_by_offset(btf, type->name_off);

	for (i = 0; i < ARRAY_SIZE(test_symbols); i++) {
		if (test_symbols[i].id >= 0)
			continue;

		if (BTF_INFO_KIND(type->info) != test_symbols[i].type)
			continue;

		if (!strcmp(str, test_symbols[i].name))
			test_symbols[i].id = type_id;
	}

	if (!btf_is_func(type))
		return 0;

	for (i = 0; i < ARRAY_SIZE(kfunc_symbols); i++) {
		if (kfunc_symbols[i].id >= 0)
			continue;
		if (!strcmp(str, kfunc_symbols[i].name))
			kfunc_symbols[i].id = type_id;
	}

	return 0;
}

static int resolve_symbols(struct btf *btf)
{
	__u32 nr = btf__type_cnt(btf);
	int type_id;

	for (type_id = 1; type_id < nr; type_id++) {
		if (__resolve_symbol(btf, type_id))
			return -1;
	}
	return 0;
}

static bool btf_has_decl_tag(struct btf *btf, const char *tag_name, s32 target_id)
{
	const struct btf_type *t;
	const char *name;
	int nr, id;

	nr = btf__type_cnt(btf);
	for (id = 1; id < nr; id++) {
		t = btf__type_by_id(btf, id);
		if (!btf_is_decl_tag(t))
			continue;
		if (t->type != (__u32)target_id)
			continue;
		if (btf_decl_tag(t)->component_idx != -1)
			continue;
		name = btf__name_by_offset(btf, t->name_off);
		if (strcmp(name, tag_name) == 0)
			return true;
	}
	return false;
}

static void check_kfunc_set(struct btf_id_set8 *set)
{
	unsigned int i, j;

	ASSERT_EQ(set->flags, BTF_SET8_KFUNCS, "kfunc_set_flags");
	ASSERT_EQ(set->cnt, ARRAY_SIZE(kfunc_symbols), "kfunc_set_cnt");

	for (i = 0; i < set->cnt; i++) {
		for (j = 0; j < ARRAY_SIZE(kfunc_symbols); j++) {
			if (kfunc_symbols[j].id == (s32)set->pairs[i].id) {
				ASSERT_EQ(set->pairs[i].flags,
					  kfunc_symbols[j].flags, "kfunc_flags_check");
				break;
			}
		}

		ASSERT_TRUE(j < ARRAY_SIZE(kfunc_symbols), "kfunc_id_found");

		if (i > 0) {
			ASSERT_LE(set->pairs[i - 1].id,
				  set->pairs[i].id, "kfunc_sort_check");
		}
	}
}

/* True if @id is PTR -> TYPE_TAG(kflag=1, "address_space(1)") -> pointee */
static bool is_arena_tagged_ptr(struct btf *btf, __u32 id)
{
	const struct btf_type *ptr, *tag;
	const char *name;

	ptr = btf__type_by_id(btf, id);
	if (!btf_is_ptr(ptr))
		return false;
	tag = btf__type_by_id(btf, ptr->type);
	if (!btf_is_type_tag(tag) || !btf_kflag(tag))
		return false;
	name = btf__name_by_offset(btf, tag->name_off);
	return strcmp(name, TYPE_ATTR_ARENA) == 0;
}

void test_resolve_btfids(void)
{
	__u32 *test_list, *test_lists[] = { test_list_local, test_list_global };
	unsigned int i, j;
	struct btf *btf;

	btf = btf__parse_raw(BTF_DATA_FILE);
	if (!ASSERT_OK_PTR(btf, "btf_parse"))
		return;

	if (resolve_symbols(btf))
		goto out;

	/* Check BTF_ID_LIST(test_list_local) and
	 * BTF_ID_LIST_GLOBAL(test_list_global) IDs
	 */
	for (j = 0; j < ARRAY_SIZE(test_lists); j++) {
		test_list = test_lists[j];
		for (i = 0; i < ARRAY_SIZE(test_symbols); i++)
			ASSERT_EQ(test_list[i], test_symbols[i].id, test_symbols[i].name);
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

		if (!ASSERT_TRUE(found, "id_in_test_symbols"))
			break;

		if (i > 0)
			ASSERT_LE(test_set.ids[i - 1], test_set.ids[i], "sort_check");
	}

	check_kfunc_set(&test_kfunc_set);
	check_kfunc_set(&test_kfunc_set_rev);

	/* Check resolve_btfids emitted a bpf_kfunc decl_tag for each kfunc */
	for (i = 0; i < ARRAY_SIZE(kfunc_symbols); i++) {
		ASSERT_TRUE(btf_has_decl_tag(btf, DECL_TAG_KFUNC,
					     kfunc_symbols[i].id),
			    kfunc_symbols[i].name);
	}

	/* Check resolve_btfids emitted bpf_fastcall for KF_FASTCALL kfuncs */
	for (i = 0; i < ARRAY_SIZE(kfunc_symbols); i++) {
		if (kfunc_symbols[i].flags & KF_FASTCALL) {
			ASSERT_TRUE(btf_has_decl_tag(btf, DECL_TAG_FASTCALL,
						     kfunc_symbols[i].id),
				    kfunc_symbols[i].name);
		}
	}

	/*
	 * Check resolve_btfids wrapped exactly the arena-flagged or suffixed
	 * return/args with the address_space(1) type attribute, and left other
	 * pointers/returns untouched.
	 */
	for (i = 0; i < ARRAY_SIZE(kfunc_symbols); i++) {
		const struct btf_type *fn, *proto;
		const struct btf_param *params;
		const char *name = kfunc_symbols[i].name;
		u32 arena_args = kfunc_symbols[i].arena_args;
		__u32 nr;

		fn = btf__type_by_id(btf, kfunc_symbols[i].id);
		if (!ASSERT_TRUE(btf_is_func(fn), name))
			continue;
		proto = btf__type_by_id(btf, fn->type);
		if (!ASSERT_TRUE(btf_is_func_proto(proto), name))
			continue;
		params = btf_params(proto);
		nr = btf_vlen(proto);

		ASSERT_EQ(is_arena_tagged_ptr(btf, proto->type),
			  kfunc_symbols[i].arena_ret, name);
		for (j = 0; j < nr; j++)
			ASSERT_EQ(is_arena_tagged_ptr(btf, params[j].type),
				  !!(arena_args & ARENA_ARG(j)), name);
	}

out:
	btf__free(btf);
}
