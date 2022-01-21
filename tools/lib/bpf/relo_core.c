// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2019 Facebook */

#ifdef __KERNEL__
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/string.h>
#include <linux/bpf_verifier.h>
#include "relo_core.h"

static const char *btf_kind_str(const struct btf_type *t)
{
	return btf_type_str(t);
}

static bool is_ldimm64_insn(struct bpf_insn *insn)
{
	return insn->code == (BPF_LD | BPF_IMM | BPF_DW);
}

static const struct btf_type *
skip_mods_and_typedefs(const struct btf *btf, u32 id, u32 *res_id)
{
	return btf_type_skip_modifiers(btf, id, res_id);
}

static const char *btf__name_by_offset(const struct btf *btf, u32 offset)
{
	return btf_name_by_offset(btf, offset);
}

static s64 btf__resolve_size(const struct btf *btf, u32 type_id)
{
	const struct btf_type *t;
	int size;

	t = btf_type_by_id(btf, type_id);
	t = btf_resolve_size(btf, t, &size);
	if (IS_ERR(t))
		return PTR_ERR(t);
	return size;
}

enum libbpf_print_level {
	LIBBPF_WARN,
	LIBBPF_INFO,
	LIBBPF_DEBUG,
};

#undef pr_warn
#undef pr_info
#undef pr_debug
#define pr_warn(fmt, log, ...)	bpf_log((void *)log, fmt, "", ##__VA_ARGS__)
#define pr_info(fmt, log, ...)	bpf_log((void *)log, fmt, "", ##__VA_ARGS__)
#define pr_debug(fmt, log, ...)	bpf_log((void *)log, fmt, "", ##__VA_ARGS__)
#define libbpf_print(level, fmt, ...)	bpf_log((void *)prog_name, fmt, ##__VA_ARGS__)
#else
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <linux/err.h>

#include "libbpf.h"
#include "bpf.h"
#include "btf.h"
#include "str_error.h"
#include "libbpf_internal.h"
#endif

static bool is_flex_arr(const struct btf *btf,
			const struct bpf_core_accessor *acc,
			const struct btf_array *arr)
{
	const struct btf_type *t;

	/* not a flexible array, if not inside a struct or has non-zero size */
	if (!acc->name || arr->nelems > 0)
		return false;

	/* has to be the last member of enclosing struct */
	t = btf_type_by_id(btf, acc->type_id);
	return acc->idx == btf_vlen(t) - 1;
}

static const char *core_relo_kind_str(enum bpf_core_relo_kind kind)
{
	switch (kind) {
	case BPF_CORE_FIELD_BYTE_OFFSET: return "byte_off";
	case BPF_CORE_FIELD_BYTE_SIZE: return "byte_sz";
	case BPF_CORE_FIELD_EXISTS: return "field_exists";
	case BPF_CORE_FIELD_SIGNED: return "signed";
	case BPF_CORE_FIELD_LSHIFT_U64: return "lshift_u64";
	case BPF_CORE_FIELD_RSHIFT_U64: return "rshift_u64";
	case BPF_CORE_TYPE_ID_LOCAL: return "local_type_id";
	case BPF_CORE_TYPE_ID_TARGET: return "target_type_id";
	case BPF_CORE_TYPE_EXISTS: return "type_exists";
	case BPF_CORE_TYPE_SIZE: return "type_size";
	case BPF_CORE_ENUMVAL_EXISTS: return "enumval_exists";
	case BPF_CORE_ENUMVAL_VALUE: return "enumval_value";
	default: return "unknown";
	}
}

static bool core_relo_is_field_based(enum bpf_core_relo_kind kind)
{
	switch (kind) {
	case BPF_CORE_FIELD_BYTE_OFFSET:
	case BPF_CORE_FIELD_BYTE_SIZE:
	case BPF_CORE_FIELD_EXISTS:
	case BPF_CORE_FIELD_SIGNED:
	case BPF_CORE_FIELD_LSHIFT_U64:
	case BPF_CORE_FIELD_RSHIFT_U64:
		return true;
	default:
		return false;
	}
}

static bool core_relo_is_type_based(enum bpf_core_relo_kind kind)
{
	switch (kind) {
	case BPF_CORE_TYPE_ID_LOCAL:
	case BPF_CORE_TYPE_ID_TARGET:
	case BPF_CORE_TYPE_EXISTS:
	case BPF_CORE_TYPE_SIZE:
		return true;
	default:
		return false;
	}
}

static bool core_relo_is_enumval_based(enum bpf_core_relo_kind kind)
{
	switch (kind) {
	case BPF_CORE_ENUMVAL_EXISTS:
	case BPF_CORE_ENUMVAL_VALUE:
		return true;
	default:
		return false;
	}
}

/*
 * Turn bpf_core_relo into a low- and high-level spec representation,
 * validating correctness along the way, as well as calculating resulting
 * field bit offset, specified by accessor string. Low-level spec captures
 * every single level of nestedness, including traversing anonymous
 * struct/union members. High-level one only captures semantically meaningful
 * "turning points": named fields and array indicies.
 * E.g., for this case:
 *
 *   struct sample {
 *       int __unimportant;
 *       struct {
 *           int __1;
 *           int __2;
 *           int a[7];
 *       };
 *   };
 *
 *   struct sample *s = ...;
 *
 *   int x = &s->a[3]; // access string = '0:1:2:3'
 *
 * Low-level spec has 1:1 mapping with each element of access string (it's
 * just a parsed access string representation): [0, 1, 2, 3].
 *
 * High-level spec will capture only 3 points:
 *   - intial zero-index access by pointer (&s->... is the same as &s[0]...);
 *   - field 'a' access (corresponds to '2' in low-level spec);
 *   - array element #3 access (corresponds to '3' in low-level spec).
 *
 * Type-based relocations (TYPE_EXISTS/TYPE_SIZE,
 * TYPE_ID_LOCAL/TYPE_ID_TARGET) don't capture any field information. Their
 * spec and raw_spec are kept empty.
 *
 * Enum value-based relocations (ENUMVAL_EXISTS/ENUMVAL_VALUE) use access
 * string to specify enumerator's value index that need to be relocated.
 */
static int bpf_core_parse_spec(const char *prog_name, const struct btf *btf,
			       __u32 type_id,
			       const char *spec_str,
			       enum bpf_core_relo_kind relo_kind,
			       struct bpf_core_spec *spec)
{
	int access_idx, parsed_len, i;
	struct bpf_core_accessor *acc;
	const struct btf_type *t;
	const char *name;
	__u32 id;
	__s64 sz;

	if (str_is_empty(spec_str) || *spec_str == ':')
		return -EINVAL;

	memset(spec, 0, sizeof(*spec));
	spec->btf = btf;
	spec->root_type_id = type_id;
	spec->relo_kind = relo_kind;

	/* type-based relocations don't have a field access string */
	if (core_relo_is_type_based(relo_kind)) {
		if (strcmp(spec_str, "0"))
			return -EINVAL;
		return 0;
	}

	/* parse spec_str="0:1:2:3:4" into array raw_spec=[0, 1, 2, 3, 4] */
	while (*spec_str) {
		if (*spec_str == ':')
			++spec_str;
		if (sscanf(spec_str, "%d%n", &access_idx, &parsed_len) != 1)
			return -EINVAL;
		if (spec->raw_len == BPF_CORE_SPEC_MAX_LEN)
			return -E2BIG;
		spec_str += parsed_len;
		spec->raw_spec[spec->raw_len++] = access_idx;
	}

	if (spec->raw_len == 0)
		return -EINVAL;

	t = skip_mods_and_typedefs(btf, type_id, &id);
	if (!t)
		return -EINVAL;

	access_idx = spec->raw_spec[0];
	acc = &spec->spec[0];
	acc->type_id = id;
	acc->idx = access_idx;
	spec->len++;

	if (core_relo_is_enumval_based(relo_kind)) {
		if (!btf_is_enum(t) || spec->raw_len > 1 || access_idx >= btf_vlen(t))
			return -EINVAL;

		/* record enumerator name in a first accessor */
		acc->name = btf__name_by_offset(btf, btf_enum(t)[access_idx].name_off);
		return 0;
	}

	if (!core_relo_is_field_based(relo_kind))
		return -EINVAL;

	sz = btf__resolve_size(btf, id);
	if (sz < 0)
		return sz;
	spec->bit_offset = access_idx * sz * 8;

	for (i = 1; i < spec->raw_len; i++) {
		t = skip_mods_and_typedefs(btf, id, &id);
		if (!t)
			return -EINVAL;

		access_idx = spec->raw_spec[i];
		acc = &spec->spec[spec->len];

		if (btf_is_composite(t)) {
			const struct btf_member *m;
			__u32 bit_offset;

			if (access_idx >= btf_vlen(t))
				return -EINVAL;

			bit_offset = btf_member_bit_offset(t, access_idx);
			spec->bit_offset += bit_offset;

			m = btf_members(t) + access_idx;
			if (m->name_off) {
				name = btf__name_by_offset(btf, m->name_off);
				if (str_is_empty(name))
					return -EINVAL;

				acc->type_id = id;
				acc->idx = access_idx;
				acc->name = name;
				spec->len++;
			}

			id = m->type;
		} else if (btf_is_array(t)) {
			const struct btf_array *a = btf_array(t);
			bool flex;

			t = skip_mods_and_typedefs(btf, a->type, &id);
			if (!t)
				return -EINVAL;

			flex = is_flex_arr(btf, acc - 1, a);
			if (!flex && access_idx >= a->nelems)
				return -EINVAL;

			spec->spec[spec->len].type_id = id;
			spec->spec[spec->len].idx = access_idx;
			spec->len++;

			sz = btf__resolve_size(btf, id);
			if (sz < 0)
				return sz;
			spec->bit_offset += access_idx * sz * 8;
		} else {
			pr_warn("prog '%s': relo for [%u] %s (at idx %d) captures type [%d] of unexpected kind %s\n",
				prog_name, type_id, spec_str, i, id, btf_kind_str(t));
			return -EINVAL;
		}
	}

	return 0;
}

/* Check two types for compatibility for the purpose of field access
 * relocation. const/volatile/restrict and typedefs are skipped to ensure we
 * are relocating semantically compatible entities:
 *   - any two STRUCTs/UNIONs are compatible and can be mixed;
 *   - any two FWDs are compatible, if their names match (modulo flavor suffix);
 *   - any two PTRs are always compatible;
 *   - for ENUMs, names should be the same (ignoring flavor suffix) or at
 *     least one of enums should be anonymous;
 *   - for ENUMs, check sizes, names are ignored;
 *   - for INT, size and signedness are ignored;
 *   - any two FLOATs are always compatible;
 *   - for ARRAY, dimensionality is ignored, element types are checked for
 *     compatibility recursively;
 *   - everything else shouldn't be ever a target of relocation.
 * These rules are not set in stone and probably will be adjusted as we get
 * more experience with using BPF CO-RE relocations.
 */
static int bpf_core_fields_are_compat(const struct btf *local_btf,
				      __u32 local_id,
				      const struct btf *targ_btf,
				      __u32 targ_id)
{
	const struct btf_type *local_type, *targ_type;

recur:
	local_type = skip_mods_and_typedefs(local_btf, local_id, &local_id);
	targ_type = skip_mods_and_typedefs(targ_btf, targ_id, &targ_id);
	if (!local_type || !targ_type)
		return -EINVAL;

	if (btf_is_composite(local_type) && btf_is_composite(targ_type))
		return 1;
	if (btf_kind(local_type) != btf_kind(targ_type))
		return 0;

	switch (btf_kind(local_type)) {
	case BTF_KIND_PTR:
	case BTF_KIND_FLOAT:
		return 1;
	case BTF_KIND_FWD:
	case BTF_KIND_ENUM: {
		const char *local_name, *targ_name;
		size_t local_len, targ_len;

		local_name = btf__name_by_offset(local_btf,
						 local_type->name_off);
		targ_name = btf__name_by_offset(targ_btf, targ_type->name_off);
		local_len = bpf_core_essential_name_len(local_name);
		targ_len = bpf_core_essential_name_len(targ_name);
		/* one of them is anonymous or both w/ same flavor-less names */
		return local_len == 0 || targ_len == 0 ||
		       (local_len == targ_len &&
			strncmp(local_name, targ_name, local_len) == 0);
	}
	case BTF_KIND_INT:
		/* just reject deprecated bitfield-like integers; all other
		 * integers are by default compatible between each other
		 */
		return btf_int_offset(local_type) == 0 &&
		       btf_int_offset(targ_type) == 0;
	case BTF_KIND_ARRAY:
		local_id = btf_array(local_type)->type;
		targ_id = btf_array(targ_type)->type;
		goto recur;
	default:
		return 0;
	}
}

/*
 * Given single high-level named field accessor in local type, find
 * corresponding high-level accessor for a target type. Along the way,
 * maintain low-level spec for target as well. Also keep updating target
 * bit offset.
 *
 * Searching is performed through recursive exhaustive enumeration of all
 * fields of a struct/union. If there are any anonymous (embedded)
 * structs/unions, they are recursively searched as well. If field with
 * desired name is found, check compatibility between local and target types,
 * before returning result.
 *
 * 1 is returned, if field is found.
 * 0 is returned if no compatible field is found.
 * <0 is returned on error.
 */
static int bpf_core_match_member(const struct btf *local_btf,
				 const struct bpf_core_accessor *local_acc,
				 const struct btf *targ_btf,
				 __u32 targ_id,
				 struct bpf_core_spec *spec,
				 __u32 *next_targ_id)
{
	const struct btf_type *local_type, *targ_type;
	const struct btf_member *local_member, *m;
	const char *local_name, *targ_name;
	__u32 local_id;
	int i, n, found;

	targ_type = skip_mods_and_typedefs(targ_btf, targ_id, &targ_id);
	if (!targ_type)
		return -EINVAL;
	if (!btf_is_composite(targ_type))
		return 0;

	local_id = local_acc->type_id;
	local_type = btf_type_by_id(local_btf, local_id);
	local_member = btf_members(local_type) + local_acc->idx;
	local_name = btf__name_by_offset(local_btf, local_member->name_off);

	n = btf_vlen(targ_type);
	m = btf_members(targ_type);
	for (i = 0; i < n; i++, m++) {
		__u32 bit_offset;

		bit_offset = btf_member_bit_offset(targ_type, i);

		/* too deep struct/union/array nesting */
		if (spec->raw_len == BPF_CORE_SPEC_MAX_LEN)
			return -E2BIG;

		/* speculate this member will be the good one */
		spec->bit_offset += bit_offset;
		spec->raw_spec[spec->raw_len++] = i;

		targ_name = btf__name_by_offset(targ_btf, m->name_off);
		if (str_is_empty(targ_name)) {
			/* embedded struct/union, we need to go deeper */
			found = bpf_core_match_member(local_btf, local_acc,
						      targ_btf, m->type,
						      spec, next_targ_id);
			if (found) /* either found or error */
				return found;
		} else if (strcmp(local_name, targ_name) == 0) {
			/* matching named field */
			struct bpf_core_accessor *targ_acc;

			targ_acc = &spec->spec[spec->len++];
			targ_acc->type_id = targ_id;
			targ_acc->idx = i;
			targ_acc->name = targ_name;

			*next_targ_id = m->type;
			found = bpf_core_fields_are_compat(local_btf,
							   local_member->type,
							   targ_btf, m->type);
			if (!found)
				spec->len--; /* pop accessor */
			return found;
		}
		/* member turned out not to be what we looked for */
		spec->bit_offset -= bit_offset;
		spec->raw_len--;
	}

	return 0;
}

/*
 * Try to match local spec to a target type and, if successful, produce full
 * target spec (high-level, low-level + bit offset).
 */
static int bpf_core_spec_match(struct bpf_core_spec *local_spec,
			       const struct btf *targ_btf, __u32 targ_id,
			       struct bpf_core_spec *targ_spec)
{
	const struct btf_type *targ_type;
	const struct bpf_core_accessor *local_acc;
	struct bpf_core_accessor *targ_acc;
	int i, sz, matched;

	memset(targ_spec, 0, sizeof(*targ_spec));
	targ_spec->btf = targ_btf;
	targ_spec->root_type_id = targ_id;
	targ_spec->relo_kind = local_spec->relo_kind;

	if (core_relo_is_type_based(local_spec->relo_kind)) {
		return bpf_core_types_are_compat(local_spec->btf,
						 local_spec->root_type_id,
						 targ_btf, targ_id);
	}

	local_acc = &local_spec->spec[0];
	targ_acc = &targ_spec->spec[0];

	if (core_relo_is_enumval_based(local_spec->relo_kind)) {
		size_t local_essent_len, targ_essent_len;
		const struct btf_enum *e;
		const char *targ_name;

		/* has to resolve to an enum */
		targ_type = skip_mods_and_typedefs(targ_spec->btf, targ_id, &targ_id);
		if (!btf_is_enum(targ_type))
			return 0;

		local_essent_len = bpf_core_essential_name_len(local_acc->name);

		for (i = 0, e = btf_enum(targ_type); i < btf_vlen(targ_type); i++, e++) {
			targ_name = btf__name_by_offset(targ_spec->btf, e->name_off);
			targ_essent_len = bpf_core_essential_name_len(targ_name);
			if (targ_essent_len != local_essent_len)
				continue;
			if (strncmp(local_acc->name, targ_name, local_essent_len) == 0) {
				targ_acc->type_id = targ_id;
				targ_acc->idx = i;
				targ_acc->name = targ_name;
				targ_spec->len++;
				targ_spec->raw_spec[targ_spec->raw_len] = targ_acc->idx;
				targ_spec->raw_len++;
				return 1;
			}
		}
		return 0;
	}

	if (!core_relo_is_field_based(local_spec->relo_kind))
		return -EINVAL;

	for (i = 0; i < local_spec->len; i++, local_acc++, targ_acc++) {
		targ_type = skip_mods_and_typedefs(targ_spec->btf, targ_id,
						   &targ_id);
		if (!targ_type)
			return -EINVAL;

		if (local_acc->name) {
			matched = bpf_core_match_member(local_spec->btf,
							local_acc,
							targ_btf, targ_id,
							targ_spec, &targ_id);
			if (matched <= 0)
				return matched;
		} else {
			/* for i=0, targ_id is already treated as array element
			 * type (because it's the original struct), for others
			 * we should find array element type first
			 */
			if (i > 0) {
				const struct btf_array *a;
				bool flex;

				if (!btf_is_array(targ_type))
					return 0;

				a = btf_array(targ_type);
				flex = is_flex_arr(targ_btf, targ_acc - 1, a);
				if (!flex && local_acc->idx >= a->nelems)
					return 0;
				if (!skip_mods_and_typedefs(targ_btf, a->type,
							    &targ_id))
					return -EINVAL;
			}

			/* too deep struct/union/array nesting */
			if (targ_spec->raw_len == BPF_CORE_SPEC_MAX_LEN)
				return -E2BIG;

			targ_acc->type_id = targ_id;
			targ_acc->idx = local_acc->idx;
			targ_acc->name = NULL;
			targ_spec->len++;
			targ_spec->raw_spec[targ_spec->raw_len] = targ_acc->idx;
			targ_spec->raw_len++;

			sz = btf__resolve_size(targ_btf, targ_id);
			if (sz < 0)
				return sz;
			targ_spec->bit_offset += local_acc->idx * sz * 8;
		}
	}

	return 1;
}

static int bpf_core_calc_field_relo(const char *prog_name,
				    const struct bpf_core_relo *relo,
				    const struct bpf_core_spec *spec,
				    __u32 *val, __u32 *field_sz, __u32 *type_id,
				    bool *validate)
{
	const struct bpf_core_accessor *acc;
	const struct btf_type *t;
	__u32 byte_off, byte_sz, bit_off, bit_sz, field_type_id;
	const struct btf_member *m;
	const struct btf_type *mt;
	bool bitfield;
	__s64 sz;

	*field_sz = 0;

	if (relo->kind == BPF_CORE_FIELD_EXISTS) {
		*val = spec ? 1 : 0;
		return 0;
	}

	if (!spec)
		return -EUCLEAN; /* request instruction poisoning */

	acc = &spec->spec[spec->len - 1];
	t = btf_type_by_id(spec->btf, acc->type_id);

	/* a[n] accessor needs special handling */
	if (!acc->name) {
		if (relo->kind == BPF_CORE_FIELD_BYTE_OFFSET) {
			*val = spec->bit_offset / 8;
			/* remember field size for load/store mem size */
			sz = btf__resolve_size(spec->btf, acc->type_id);
			if (sz < 0)
				return -EINVAL;
			*field_sz = sz;
			*type_id = acc->type_id;
		} else if (relo->kind == BPF_CORE_FIELD_BYTE_SIZE) {
			sz = btf__resolve_size(spec->btf, acc->type_id);
			if (sz < 0)
				return -EINVAL;
			*val = sz;
		} else {
			pr_warn("prog '%s': relo %d at insn #%d can't be applied to array access\n",
				prog_name, relo->kind, relo->insn_off / 8);
			return -EINVAL;
		}
		if (validate)
			*validate = true;
		return 0;
	}

	m = btf_members(t) + acc->idx;
	mt = skip_mods_and_typedefs(spec->btf, m->type, &field_type_id);
	bit_off = spec->bit_offset;
	bit_sz = btf_member_bitfield_size(t, acc->idx);

	bitfield = bit_sz > 0;
	if (bitfield) {
		byte_sz = mt->size;
		byte_off = bit_off / 8 / byte_sz * byte_sz;
		/* figure out smallest int size necessary for bitfield load */
		while (bit_off + bit_sz - byte_off * 8 > byte_sz * 8) {
			if (byte_sz >= 8) {
				/* bitfield can't be read with 64-bit read */
				pr_warn("prog '%s': relo %d at insn #%d can't be satisfied for bitfield\n",
					prog_name, relo->kind, relo->insn_off / 8);
				return -E2BIG;
			}
			byte_sz *= 2;
			byte_off = bit_off / 8 / byte_sz * byte_sz;
		}
	} else {
		sz = btf__resolve_size(spec->btf, field_type_id);
		if (sz < 0)
			return -EINVAL;
		byte_sz = sz;
		byte_off = spec->bit_offset / 8;
		bit_sz = byte_sz * 8;
	}

	/* for bitfields, all the relocatable aspects are ambiguous and we
	 * might disagree with compiler, so turn off validation of expected
	 * value, except for signedness
	 */
	if (validate)
		*validate = !bitfield;

	switch (relo->kind) {
	case BPF_CORE_FIELD_BYTE_OFFSET:
		*val = byte_off;
		if (!bitfield) {
			*field_sz = byte_sz;
			*type_id = field_type_id;
		}
		break;
	case BPF_CORE_FIELD_BYTE_SIZE:
		*val = byte_sz;
		break;
	case BPF_CORE_FIELD_SIGNED:
		/* enums will be assumed unsigned */
		*val = btf_is_enum(mt) ||
		       (btf_int_encoding(mt) & BTF_INT_SIGNED);
		if (validate)
			*validate = true; /* signedness is never ambiguous */
		break;
	case BPF_CORE_FIELD_LSHIFT_U64:
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		*val = 64 - (bit_off + bit_sz - byte_off  * 8);
#else
		*val = (8 - byte_sz) * 8 + (bit_off - byte_off * 8);
#endif
		break;
	case BPF_CORE_FIELD_RSHIFT_U64:
		*val = 64 - bit_sz;
		if (validate)
			*validate = true; /* right shift is never ambiguous */
		break;
	case BPF_CORE_FIELD_EXISTS:
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int bpf_core_calc_type_relo(const struct bpf_core_relo *relo,
				   const struct bpf_core_spec *spec,
				   __u32 *val, bool *validate)
{
	__s64 sz;

	/* by default, always check expected value in bpf_insn */
	if (validate)
		*validate = true;

	/* type-based relos return zero when target type is not found */
	if (!spec) {
		*val = 0;
		return 0;
	}

	switch (relo->kind) {
	case BPF_CORE_TYPE_ID_TARGET:
		*val = spec->root_type_id;
		/* type ID, embedded in bpf_insn, might change during linking,
		 * so enforcing it is pointless
		 */
		if (validate)
			*validate = false;
		break;
	case BPF_CORE_TYPE_EXISTS:
		*val = 1;
		break;
	case BPF_CORE_TYPE_SIZE:
		sz = btf__resolve_size(spec->btf, spec->root_type_id);
		if (sz < 0)
			return -EINVAL;
		*val = sz;
		break;
	case BPF_CORE_TYPE_ID_LOCAL:
	/* BPF_CORE_TYPE_ID_LOCAL is handled specially and shouldn't get here */
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int bpf_core_calc_enumval_relo(const struct bpf_core_relo *relo,
				      const struct bpf_core_spec *spec,
				      __u32 *val)
{
	const struct btf_type *t;
	const struct btf_enum *e;

	switch (relo->kind) {
	case BPF_CORE_ENUMVAL_EXISTS:
		*val = spec ? 1 : 0;
		break;
	case BPF_CORE_ENUMVAL_VALUE:
		if (!spec)
			return -EUCLEAN; /* request instruction poisoning */
		t = btf_type_by_id(spec->btf, spec->spec[0].type_id);
		e = btf_enum(t) + spec->spec[0].idx;
		*val = e->val;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

struct bpf_core_relo_res
{
	/* expected value in the instruction, unless validate == false */
	__u32 orig_val;
	/* new value that needs to be patched up to */
	__u32 new_val;
	/* relocation unsuccessful, poison instruction, but don't fail load */
	bool poison;
	/* some relocations can't be validated against orig_val */
	bool validate;
	/* for field byte offset relocations or the forms:
	 *     *(T *)(rX + <off>) = rY
	 *     rX = *(T *)(rY + <off>),
	 * we remember original and resolved field size to adjust direct
	 * memory loads of pointers and integers; this is necessary for 32-bit
	 * host kernel architectures, but also allows to automatically
	 * relocate fields that were resized from, e.g., u32 to u64, etc.
	 */
	bool fail_memsz_adjust;
	__u32 orig_sz;
	__u32 orig_type_id;
	__u32 new_sz;
	__u32 new_type_id;
};

/* Calculate original and target relocation values, given local and target
 * specs and relocation kind. These values are calculated for each candidate.
 * If there are multiple candidates, resulting values should all be consistent
 * with each other. Otherwise, libbpf will refuse to proceed due to ambiguity.
 * If instruction has to be poisoned, *poison will be set to true.
 */
static int bpf_core_calc_relo(const char *prog_name,
			      const struct bpf_core_relo *relo,
			      int relo_idx,
			      const struct bpf_core_spec *local_spec,
			      const struct bpf_core_spec *targ_spec,
			      struct bpf_core_relo_res *res)
{
	int err = -EOPNOTSUPP;

	res->orig_val = 0;
	res->new_val = 0;
	res->poison = false;
	res->validate = true;
	res->fail_memsz_adjust = false;
	res->orig_sz = res->new_sz = 0;
	res->orig_type_id = res->new_type_id = 0;

	if (core_relo_is_field_based(relo->kind)) {
		err = bpf_core_calc_field_relo(prog_name, relo, local_spec,
					       &res->orig_val, &res->orig_sz,
					       &res->orig_type_id, &res->validate);
		err = err ?: bpf_core_calc_field_relo(prog_name, relo, targ_spec,
						      &res->new_val, &res->new_sz,
						      &res->new_type_id, NULL);
		if (err)
			goto done;
		/* Validate if it's safe to adjust load/store memory size.
		 * Adjustments are performed only if original and new memory
		 * sizes differ.
		 */
		res->fail_memsz_adjust = false;
		if (res->orig_sz != res->new_sz) {
			const struct btf_type *orig_t, *new_t;

			orig_t = btf_type_by_id(local_spec->btf, res->orig_type_id);
			new_t = btf_type_by_id(targ_spec->btf, res->new_type_id);

			/* There are two use cases in which it's safe to
			 * adjust load/store's mem size:
			 *   - reading a 32-bit kernel pointer, while on BPF
			 *   size pointers are always 64-bit; in this case
			 *   it's safe to "downsize" instruction size due to
			 *   pointer being treated as unsigned integer with
			 *   zero-extended upper 32-bits;
			 *   - reading unsigned integers, again due to
			 *   zero-extension is preserving the value correctly.
			 *
			 * In all other cases it's incorrect to attempt to
			 * load/store field because read value will be
			 * incorrect, so we poison relocated instruction.
			 */
			if (btf_is_ptr(orig_t) && btf_is_ptr(new_t))
				goto done;
			if (btf_is_int(orig_t) && btf_is_int(new_t) &&
			    btf_int_encoding(orig_t) != BTF_INT_SIGNED &&
			    btf_int_encoding(new_t) != BTF_INT_SIGNED)
				goto done;

			/* mark as invalid mem size adjustment, but this will
			 * only be checked for LDX/STX/ST insns
			 */
			res->fail_memsz_adjust = true;
		}
	} else if (core_relo_is_type_based(relo->kind)) {
		err = bpf_core_calc_type_relo(relo, local_spec, &res->orig_val, &res->validate);
		err = err ?: bpf_core_calc_type_relo(relo, targ_spec, &res->new_val, NULL);
	} else if (core_relo_is_enumval_based(relo->kind)) {
		err = bpf_core_calc_enumval_relo(relo, local_spec, &res->orig_val);
		err = err ?: bpf_core_calc_enumval_relo(relo, targ_spec, &res->new_val);
	}

done:
	if (err == -EUCLEAN) {
		/* EUCLEAN is used to signal instruction poisoning request */
		res->poison = true;
		err = 0;
	} else if (err == -EOPNOTSUPP) {
		/* EOPNOTSUPP means unknown/unsupported relocation */
		pr_warn("prog '%s': relo #%d: unrecognized CO-RE relocation %s (%d) at insn #%d\n",
			prog_name, relo_idx, core_relo_kind_str(relo->kind),
			relo->kind, relo->insn_off / 8);
	}

	return err;
}

/*
 * Turn instruction for which CO_RE relocation failed into invalid one with
 * distinct signature.
 */
static void bpf_core_poison_insn(const char *prog_name, int relo_idx,
				 int insn_idx, struct bpf_insn *insn)
{
	pr_debug("prog '%s': relo #%d: substituting insn #%d w/ invalid insn\n",
		 prog_name, relo_idx, insn_idx);
	insn->code = BPF_JMP | BPF_CALL;
	insn->dst_reg = 0;
	insn->src_reg = 0;
	insn->off = 0;
	/* if this instruction is reachable (not a dead code),
	 * verifier will complain with the following message:
	 * invalid func unknown#195896080
	 */
	insn->imm = 195896080; /* => 0xbad2310 => "bad relo" */
}

static int insn_bpf_size_to_bytes(struct bpf_insn *insn)
{
	switch (BPF_SIZE(insn->code)) {
	case BPF_DW: return 8;
	case BPF_W: return 4;
	case BPF_H: return 2;
	case BPF_B: return 1;
	default: return -1;
	}
}

static int insn_bytes_to_bpf_size(__u32 sz)
{
	switch (sz) {
	case 8: return BPF_DW;
	case 4: return BPF_W;
	case 2: return BPF_H;
	case 1: return BPF_B;
	default: return -1;
	}
}

/*
 * Patch relocatable BPF instruction.
 *
 * Patched value is determined by relocation kind and target specification.
 * For existence relocations target spec will be NULL if field/type is not found.
 * Expected insn->imm value is determined using relocation kind and local
 * spec, and is checked before patching instruction. If actual insn->imm value
 * is wrong, bail out with error.
 *
 * Currently supported classes of BPF instruction are:
 * 1. rX = <imm> (assignment with immediate operand);
 * 2. rX += <imm> (arithmetic operations with immediate operand);
 * 3. rX = <imm64> (load with 64-bit immediate value);
 * 4. rX = *(T *)(rY + <off>), where T is one of {u8, u16, u32, u64};
 * 5. *(T *)(rX + <off>) = rY, where T is one of {u8, u16, u32, u64};
 * 6. *(T *)(rX + <off>) = <imm>, where T is one of {u8, u16, u32, u64}.
 */
static int bpf_core_patch_insn(const char *prog_name, struct bpf_insn *insn,
			       int insn_idx, const struct bpf_core_relo *relo,
			       int relo_idx, const struct bpf_core_relo_res *res)
{
	__u32 orig_val, new_val;
	__u8 class;

	class = BPF_CLASS(insn->code);

	if (res->poison) {
poison:
		/* poison second part of ldimm64 to avoid confusing error from
		 * verifier about "unknown opcode 00"
		 */
		if (is_ldimm64_insn(insn))
			bpf_core_poison_insn(prog_name, relo_idx, insn_idx + 1, insn + 1);
		bpf_core_poison_insn(prog_name, relo_idx, insn_idx, insn);
		return 0;
	}

	orig_val = res->orig_val;
	new_val = res->new_val;

	switch (class) {
	case BPF_ALU:
	case BPF_ALU64:
		if (BPF_SRC(insn->code) != BPF_K)
			return -EINVAL;
		if (res->validate && insn->imm != orig_val) {
			pr_warn("prog '%s': relo #%d: unexpected insn #%d (ALU/ALU64) value: got %u, exp %u -> %u\n",
				prog_name, relo_idx,
				insn_idx, insn->imm, orig_val, new_val);
			return -EINVAL;
		}
		orig_val = insn->imm;
		insn->imm = new_val;
		pr_debug("prog '%s': relo #%d: patched insn #%d (ALU/ALU64) imm %u -> %u\n",
			 prog_name, relo_idx, insn_idx,
			 orig_val, new_val);
		break;
	case BPF_LDX:
	case BPF_ST:
	case BPF_STX:
		if (res->validate && insn->off != orig_val) {
			pr_warn("prog '%s': relo #%d: unexpected insn #%d (LDX/ST/STX) value: got %u, exp %u -> %u\n",
				prog_name, relo_idx, insn_idx, insn->off, orig_val, new_val);
			return -EINVAL;
		}
		if (new_val > SHRT_MAX) {
			pr_warn("prog '%s': relo #%d: insn #%d (LDX/ST/STX) value too big: %u\n",
				prog_name, relo_idx, insn_idx, new_val);
			return -ERANGE;
		}
		if (res->fail_memsz_adjust) {
			pr_warn("prog '%s': relo #%d: insn #%d (LDX/ST/STX) accesses field incorrectly. "
				"Make sure you are accessing pointers, unsigned integers, or fields of matching type and size.\n",
				prog_name, relo_idx, insn_idx);
			goto poison;
		}

		orig_val = insn->off;
		insn->off = new_val;
		pr_debug("prog '%s': relo #%d: patched insn #%d (LDX/ST/STX) off %u -> %u\n",
			 prog_name, relo_idx, insn_idx, orig_val, new_val);

		if (res->new_sz != res->orig_sz) {
			int insn_bytes_sz, insn_bpf_sz;

			insn_bytes_sz = insn_bpf_size_to_bytes(insn);
			if (insn_bytes_sz != res->orig_sz) {
				pr_warn("prog '%s': relo #%d: insn #%d (LDX/ST/STX) unexpected mem size: got %d, exp %u\n",
					prog_name, relo_idx, insn_idx, insn_bytes_sz, res->orig_sz);
				return -EINVAL;
			}

			insn_bpf_sz = insn_bytes_to_bpf_size(res->new_sz);
			if (insn_bpf_sz < 0) {
				pr_warn("prog '%s': relo #%d: insn #%d (LDX/ST/STX) invalid new mem size: %u\n",
					prog_name, relo_idx, insn_idx, res->new_sz);
				return -EINVAL;
			}

			insn->code = BPF_MODE(insn->code) | insn_bpf_sz | BPF_CLASS(insn->code);
			pr_debug("prog '%s': relo #%d: patched insn #%d (LDX/ST/STX) mem_sz %u -> %u\n",
				 prog_name, relo_idx, insn_idx, res->orig_sz, res->new_sz);
		}
		break;
	case BPF_LD: {
		__u64 imm;

		if (!is_ldimm64_insn(insn) ||
		    insn[0].src_reg != 0 || insn[0].off != 0 ||
		    insn[1].code != 0 || insn[1].dst_reg != 0 ||
		    insn[1].src_reg != 0 || insn[1].off != 0) {
			pr_warn("prog '%s': relo #%d: insn #%d (LDIMM64) has unexpected form\n",
				prog_name, relo_idx, insn_idx);
			return -EINVAL;
		}

		imm = insn[0].imm + ((__u64)insn[1].imm << 32);
		if (res->validate && imm != orig_val) {
			pr_warn("prog '%s': relo #%d: unexpected insn #%d (LDIMM64) value: got %llu, exp %u -> %u\n",
				prog_name, relo_idx,
				insn_idx, (unsigned long long)imm,
				orig_val, new_val);
			return -EINVAL;
		}

		insn[0].imm = new_val;
		insn[1].imm = 0; /* currently only 32-bit values are supported */
		pr_debug("prog '%s': relo #%d: patched insn #%d (LDIMM64) imm64 %llu -> %u\n",
			 prog_name, relo_idx, insn_idx,
			 (unsigned long long)imm, new_val);
		break;
	}
	default:
		pr_warn("prog '%s': relo #%d: trying to relocate unrecognized insn #%d, code:0x%x, src:0x%x, dst:0x%x, off:0x%x, imm:0x%x\n",
			prog_name, relo_idx, insn_idx, insn->code,
			insn->src_reg, insn->dst_reg, insn->off, insn->imm);
		return -EINVAL;
	}

	return 0;
}

/* Output spec definition in the format:
 * [<type-id>] (<type-name>) + <raw-spec> => <offset>@<spec>,
 * where <spec> is a C-syntax view of recorded field access, e.g.: x.a[3].b
 */
static void bpf_core_dump_spec(const char *prog_name, int level, const struct bpf_core_spec *spec)
{
	const struct btf_type *t;
	const struct btf_enum *e;
	const char *s;
	__u32 type_id;
	int i;

	type_id = spec->root_type_id;
	t = btf_type_by_id(spec->btf, type_id);
	s = btf__name_by_offset(spec->btf, t->name_off);

	libbpf_print(level, "[%u] %s %s", type_id, btf_kind_str(t), str_is_empty(s) ? "<anon>" : s);

	if (core_relo_is_type_based(spec->relo_kind))
		return;

	if (core_relo_is_enumval_based(spec->relo_kind)) {
		t = skip_mods_and_typedefs(spec->btf, type_id, NULL);
		e = btf_enum(t) + spec->raw_spec[0];
		s = btf__name_by_offset(spec->btf, e->name_off);

		libbpf_print(level, "::%s = %u", s, e->val);
		return;
	}

	if (core_relo_is_field_based(spec->relo_kind)) {
		for (i = 0; i < spec->len; i++) {
			if (spec->spec[i].name)
				libbpf_print(level, ".%s", spec->spec[i].name);
			else if (i > 0 || spec->spec[i].idx > 0)
				libbpf_print(level, "[%u]", spec->spec[i].idx);
		}

		libbpf_print(level, " (");
		for (i = 0; i < spec->raw_len; i++)
			libbpf_print(level, "%s%d", i == 0 ? "" : ":", spec->raw_spec[i]);

		if (spec->bit_offset % 8)
			libbpf_print(level, " @ offset %u.%u)",
				     spec->bit_offset / 8, spec->bit_offset % 8);
		else
			libbpf_print(level, " @ offset %u)", spec->bit_offset / 8);
		return;
	}
}

/*
 * CO-RE relocate single instruction.
 *
 * The outline and important points of the algorithm:
 * 1. For given local type, find corresponding candidate target types.
 *    Candidate type is a type with the same "essential" name, ignoring
 *    everything after last triple underscore (___). E.g., `sample`,
 *    `sample___flavor_one`, `sample___flavor_another_one`, are all candidates
 *    for each other. Names with triple underscore are referred to as
 *    "flavors" and are useful, among other things, to allow to
 *    specify/support incompatible variations of the same kernel struct, which
 *    might differ between different kernel versions and/or build
 *    configurations.
 *
 *    N.B. Struct "flavors" could be generated by bpftool's BTF-to-C
 *    converter, when deduplicated BTF of a kernel still contains more than
 *    one different types with the same name. In that case, ___2, ___3, etc
 *    are appended starting from second name conflict. But start flavors are
 *    also useful to be defined "locally", in BPF program, to extract same
 *    data from incompatible changes between different kernel
 *    versions/configurations. For instance, to handle field renames between
 *    kernel versions, one can use two flavors of the struct name with the
 *    same common name and use conditional relocations to extract that field,
 *    depending on target kernel version.
 * 2. For each candidate type, try to match local specification to this
 *    candidate target type. Matching involves finding corresponding
 *    high-level spec accessors, meaning that all named fields should match,
 *    as well as all array accesses should be within the actual bounds. Also,
 *    types should be compatible (see bpf_core_fields_are_compat for details).
 * 3. It is supported and expected that there might be multiple flavors
 *    matching the spec. As long as all the specs resolve to the same set of
 *    offsets across all candidates, there is no error. If there is any
 *    ambiguity, CO-RE relocation will fail. This is necessary to accomodate
 *    imprefection of BTF deduplication, which can cause slight duplication of
 *    the same BTF type, if some directly or indirectly referenced (by
 *    pointer) type gets resolved to different actual types in different
 *    object files. If such situation occurs, deduplicated BTF will end up
 *    with two (or more) structurally identical types, which differ only in
 *    types they refer to through pointer. This should be OK in most cases and
 *    is not an error.
 * 4. Candidate types search is performed by linearly scanning through all
 *    types in target BTF. It is anticipated that this is overall more
 *    efficient memory-wise and not significantly worse (if not better)
 *    CPU-wise compared to prebuilding a map from all local type names to
 *    a list of candidate type names. It's also sped up by caching resolved
 *    list of matching candidates per each local "root" type ID, that has at
 *    least one bpf_core_relo associated with it. This list is shared
 *    between multiple relocations for the same type ID and is updated as some
 *    of the candidates are pruned due to structural incompatibility.
 */
int bpf_core_apply_relo_insn(const char *prog_name, struct bpf_insn *insn,
			     int insn_idx,
			     const struct bpf_core_relo *relo,
			     int relo_idx,
			     const struct btf *local_btf,
			     struct bpf_core_cand_list *cands,
			     struct bpf_core_spec *specs_scratch)
{
	struct bpf_core_spec *local_spec = &specs_scratch[0];
	struct bpf_core_spec *cand_spec = &specs_scratch[1];
	struct bpf_core_spec *targ_spec = &specs_scratch[2];
	struct bpf_core_relo_res cand_res, targ_res;
	const struct btf_type *local_type;
	const char *local_name;
	__u32 local_id;
	const char *spec_str;
	int i, j, err;

	local_id = relo->type_id;
	local_type = btf_type_by_id(local_btf, local_id);
	local_name = btf__name_by_offset(local_btf, local_type->name_off);
	if (!local_name)
		return -EINVAL;

	spec_str = btf__name_by_offset(local_btf, relo->access_str_off);
	if (str_is_empty(spec_str))
		return -EINVAL;

	err = bpf_core_parse_spec(prog_name, local_btf, local_id, spec_str,
				  relo->kind, local_spec);
	if (err) {
		pr_warn("prog '%s': relo #%d: parsing [%d] %s %s + %s failed: %d\n",
			prog_name, relo_idx, local_id, btf_kind_str(local_type),
			str_is_empty(local_name) ? "<anon>" : local_name,
			spec_str, err);
		return -EINVAL;
	}

	pr_debug("prog '%s': relo #%d: kind <%s> (%d), spec is ", prog_name,
		 relo_idx, core_relo_kind_str(relo->kind), relo->kind);
	bpf_core_dump_spec(prog_name, LIBBPF_DEBUG, local_spec);
	libbpf_print(LIBBPF_DEBUG, "\n");

	/* TYPE_ID_LOCAL relo is special and doesn't need candidate search */
	if (relo->kind == BPF_CORE_TYPE_ID_LOCAL) {
		/* bpf_insn's imm value could get out of sync during linking */
		memset(&targ_res, 0, sizeof(targ_res));
		targ_res.validate = false;
		targ_res.poison = false;
		targ_res.orig_val = local_spec->root_type_id;
		targ_res.new_val = local_spec->root_type_id;
		goto patch_insn;
	}

	/* libbpf doesn't support candidate search for anonymous types */
	if (str_is_empty(spec_str)) {
		pr_warn("prog '%s': relo #%d: <%s> (%d) relocation doesn't support anonymous types\n",
			prog_name, relo_idx, core_relo_kind_str(relo->kind), relo->kind);
		return -EOPNOTSUPP;
	}

	for (i = 0, j = 0; i < cands->len; i++) {
		err = bpf_core_spec_match(local_spec, cands->cands[i].btf,
					  cands->cands[i].id, cand_spec);
		if (err < 0) {
			pr_warn("prog '%s': relo #%d: error matching candidate #%d ",
				prog_name, relo_idx, i);
			bpf_core_dump_spec(prog_name, LIBBPF_WARN, cand_spec);
			libbpf_print(LIBBPF_WARN, ": %d\n", err);
			return err;
		}

		pr_debug("prog '%s': relo #%d: %s candidate #%d ", prog_name,
			 relo_idx, err == 0 ? "non-matching" : "matching", i);
		bpf_core_dump_spec(prog_name, LIBBPF_DEBUG, cand_spec);
		libbpf_print(LIBBPF_DEBUG, "\n");

		if (err == 0)
			continue;

		err = bpf_core_calc_relo(prog_name, relo, relo_idx, local_spec, cand_spec, &cand_res);
		if (err)
			return err;

		if (j == 0) {
			targ_res = cand_res;
			*targ_spec = *cand_spec;
		} else if (cand_spec->bit_offset != targ_spec->bit_offset) {
			/* if there are many field relo candidates, they
			 * should all resolve to the same bit offset
			 */
			pr_warn("prog '%s': relo #%d: field offset ambiguity: %u != %u\n",
				prog_name, relo_idx, cand_spec->bit_offset,
				targ_spec->bit_offset);
			return -EINVAL;
		} else if (cand_res.poison != targ_res.poison || cand_res.new_val != targ_res.new_val) {
			/* all candidates should result in the same relocation
			 * decision and value, otherwise it's dangerous to
			 * proceed due to ambiguity
			 */
			pr_warn("prog '%s': relo #%d: relocation decision ambiguity: %s %u != %s %u\n",
				prog_name, relo_idx,
				cand_res.poison ? "failure" : "success", cand_res.new_val,
				targ_res.poison ? "failure" : "success", targ_res.new_val);
			return -EINVAL;
		}

		cands->cands[j++] = cands->cands[i];
	}

	/*
	 * For BPF_CORE_FIELD_EXISTS relo or when used BPF program has field
	 * existence checks or kernel version/config checks, it's expected
	 * that we might not find any candidates. In this case, if field
	 * wasn't found in any candidate, the list of candidates shouldn't
	 * change at all, we'll just handle relocating appropriately,
	 * depending on relo's kind.
	 */
	if (j > 0)
		cands->len = j;

	/*
	 * If no candidates were found, it might be both a programmer error,
	 * as well as expected case, depending whether instruction w/
	 * relocation is guarded in some way that makes it unreachable (dead
	 * code) if relocation can't be resolved. This is handled in
	 * bpf_core_patch_insn() uniformly by replacing that instruction with
	 * BPF helper call insn (using invalid helper ID). If that instruction
	 * is indeed unreachable, then it will be ignored and eliminated by
	 * verifier. If it was an error, then verifier will complain and point
	 * to a specific instruction number in its log.
	 */
	if (j == 0) {
		pr_debug("prog '%s': relo #%d: no matching targets found\n",
			 prog_name, relo_idx);

		/* calculate single target relo result explicitly */
		err = bpf_core_calc_relo(prog_name, relo, relo_idx, local_spec, NULL, &targ_res);
		if (err)
			return err;
	}

patch_insn:
	/* bpf_core_patch_insn() should know how to handle missing targ_spec */
	err = bpf_core_patch_insn(prog_name, insn, insn_idx, relo, relo_idx, &targ_res);
	if (err) {
		pr_warn("prog '%s': relo #%d: failed to patch insn #%u: %d\n",
			prog_name, relo_idx, relo->insn_off / 8, err);
		return -EINVAL;
	}

	return 0;
}
