// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

/*
 * BTF-to-C type converter.
 *
 * Copyright (c) 2019 Facebook
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <limits.h>
#include <linux/err.h>
#include <linux/btf.h>
#include <linux/kernel.h>
#include "btf.h"
#include "hashmap.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "str_error.h"

static const char PREFIXES[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t";
static const size_t PREFIX_CNT = sizeof(PREFIXES) - 1;

static const char *pfx(int lvl)
{
	return lvl >= PREFIX_CNT ? PREFIXES : &PREFIXES[PREFIX_CNT - lvl];
}

enum btf_dump_type_order_state {
	NOT_ORDERED,
	ORDERING,
	ORDERED,
};

enum btf_dump_type_emit_state {
	NOT_EMITTED,
	EMITTING,
	EMITTED,
};

/* per-type auxiliary state */
struct btf_dump_type_aux_state {
	/* topological sorting state */
	enum btf_dump_type_order_state order_state: 2;
	/* emitting state used to determine the need for forward declaration */
	enum btf_dump_type_emit_state emit_state: 2;
	/* whether forward declaration was already emitted */
	__u8 fwd_emitted: 1;
	/* whether unique non-duplicate name was already assigned */
	__u8 name_resolved: 1;
	/* whether type is referenced from any other type */
	__u8 referenced: 1;
};

/* indent string length; one indent string is added for each indent level */
#define BTF_DATA_INDENT_STR_LEN			32

/*
 * Common internal data for BTF type data dump operations.
 */
struct btf_dump_data {
	const void *data_end;		/* end of valid data to show */
	bool compact;
	bool skip_names;
	bool emit_zeroes;
	__u8 indent_lvl;	/* base indent level */
	char indent_str[BTF_DATA_INDENT_STR_LEN];
	/* below are used during iteration */
	int depth;
	bool is_array_member;
	bool is_array_terminated;
	bool is_array_char;
};

struct btf_dump {
	const struct btf *btf;
	btf_dump_printf_fn_t printf_fn;
	void *cb_ctx;
	int ptr_sz;
	bool strip_mods;
	bool skip_anon_defs;
	int last_id;

	/* per-type auxiliary state */
	struct btf_dump_type_aux_state *type_states;
	size_t type_states_cap;
	/* per-type optional cached unique name, must be freed, if present */
	const char **cached_names;
	size_t cached_names_cap;

	/* topo-sorted list of dependent type definitions */
	__u32 *emit_queue;
	int emit_queue_cap;
	int emit_queue_cnt;

	/*
	 * stack of type declarations (e.g., chain of modifiers, arrays,
	 * funcs, etc)
	 */
	__u32 *decl_stack;
	int decl_stack_cap;
	int decl_stack_cnt;

	/* maps struct/union/enum name to a number of name occurrences */
	struct hashmap *type_names;
	/*
	 * maps typedef identifiers and enum value names to a number of such
	 * name occurrences
	 */
	struct hashmap *ident_names;
	/*
	 * data for typed display; allocated if needed.
	 */
	struct btf_dump_data *typed_dump;
};

static size_t str_hash_fn(long key, void *ctx)
{
	return str_hash((void *)key);
}

static bool str_equal_fn(long a, long b, void *ctx)
{
	return strcmp((void *)a, (void *)b) == 0;
}

static const char *btf_name_of(const struct btf_dump *d, __u32 name_off)
{
	return btf__name_by_offset(d->btf, name_off);
}

static void btf_dump_printf(const struct btf_dump *d, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	d->printf_fn(d->cb_ctx, fmt, args);
	va_end(args);
}

static int btf_dump_mark_referenced(struct btf_dump *d);
static int btf_dump_resize(struct btf_dump *d);

struct btf_dump *btf_dump__new(const struct btf *btf,
			       btf_dump_printf_fn_t printf_fn,
			       void *ctx,
			       const struct btf_dump_opts *opts)
{
	struct btf_dump *d;
	int err;

	if (!OPTS_VALID(opts, btf_dump_opts))
		return libbpf_err_ptr(-EINVAL);

	if (!printf_fn)
		return libbpf_err_ptr(-EINVAL);

	d = calloc(1, sizeof(struct btf_dump));
	if (!d)
		return libbpf_err_ptr(-ENOMEM);

	d->btf = btf;
	d->printf_fn = printf_fn;
	d->cb_ctx = ctx;
	d->ptr_sz = btf__pointer_size(btf) ? : sizeof(void *);

	d->type_names = hashmap__new(str_hash_fn, str_equal_fn, NULL);
	if (IS_ERR(d->type_names)) {
		err = PTR_ERR(d->type_names);
		d->type_names = NULL;
		goto err;
	}
	d->ident_names = hashmap__new(str_hash_fn, str_equal_fn, NULL);
	if (IS_ERR(d->ident_names)) {
		err = PTR_ERR(d->ident_names);
		d->ident_names = NULL;
		goto err;
	}

	err = btf_dump_resize(d);
	if (err)
		goto err;

	return d;
err:
	btf_dump__free(d);
	return libbpf_err_ptr(err);
}

static int btf_dump_resize(struct btf_dump *d)
{
	int err, last_id = btf__type_cnt(d->btf) - 1;

	if (last_id <= d->last_id)
		return 0;

	if (libbpf_ensure_mem((void **)&d->type_states, &d->type_states_cap,
			      sizeof(*d->type_states), last_id + 1))
		return -ENOMEM;
	if (libbpf_ensure_mem((void **)&d->cached_names, &d->cached_names_cap,
			      sizeof(*d->cached_names), last_id + 1))
		return -ENOMEM;

	if (d->last_id == 0) {
		/* VOID is special */
		d->type_states[0].order_state = ORDERED;
		d->type_states[0].emit_state = EMITTED;
	}

	/* eagerly determine referenced types for anon enums */
	err = btf_dump_mark_referenced(d);
	if (err)
		return err;

	d->last_id = last_id;
	return 0;
}

static void btf_dump_free_names(struct hashmap *map)
{
	size_t bkt;
	struct hashmap_entry *cur;

	hashmap__for_each_entry(map, cur, bkt)
		free((void *)cur->pkey);

	hashmap__free(map);
}

void btf_dump__free(struct btf_dump *d)
{
	int i;

	if (IS_ERR_OR_NULL(d))
		return;

	free(d->type_states);
	if (d->cached_names) {
		/* any set cached name is owned by us and should be freed */
		for (i = 0; i <= d->last_id; i++) {
			if (d->cached_names[i])
				free((void *)d->cached_names[i]);
		}
	}
	free(d->cached_names);
	free(d->emit_queue);
	free(d->decl_stack);
	btf_dump_free_names(d->type_names);
	btf_dump_free_names(d->ident_names);

	free(d);
}

static int btf_dump_order_type(struct btf_dump *d, __u32 id, bool through_ptr);
static void btf_dump_emit_type(struct btf_dump *d, __u32 id, __u32 cont_id);

/*
 * Dump BTF type in a compilable C syntax, including all the necessary
 * dependent types, necessary for compilation. If some of the dependent types
 * were already emitted as part of previous btf_dump__dump_type() invocation
 * for another type, they won't be emitted again. This API allows callers to
 * filter out BTF types according to user-defined criterias and emitted only
 * minimal subset of types, necessary to compile everything. Full struct/union
 * definitions will still be emitted, even if the only usage is through
 * pointer and could be satisfied with just a forward declaration.
 *
 * Dumping is done in two high-level passes:
 *   1. Topologically sort type definitions to satisfy C rules of compilation.
 *   2. Emit type definitions in C syntax.
 *
 * Returns 0 on success; <0, otherwise.
 */
int btf_dump__dump_type(struct btf_dump *d, __u32 id)
{
	int err, i;

	if (id >= btf__type_cnt(d->btf))
		return libbpf_err(-EINVAL);

	err = btf_dump_resize(d);
	if (err)
		return libbpf_err(err);

	d->emit_queue_cnt = 0;
	err = btf_dump_order_type(d, id, false);
	if (err < 0)
		return libbpf_err(err);

	for (i = 0; i < d->emit_queue_cnt; i++)
		btf_dump_emit_type(d, d->emit_queue[i], 0 /*top-level*/);

	return 0;
}

/*
 * Mark all types that are referenced from any other type. This is used to
 * determine top-level anonymous enums that need to be emitted as an
 * independent type declarations.
 * Anonymous enums come in two flavors: either embedded in a struct's field
 * definition, in which case they have to be declared inline as part of field
 * type declaration; or as a top-level anonymous enum, typically used for
 * declaring global constants. It's impossible to distinguish between two
 * without knowing whether given enum type was referenced from other type:
 * top-level anonymous enum won't be referenced by anything, while embedded
 * one will.
 */
static int btf_dump_mark_referenced(struct btf_dump *d)
{
	int i, j, n = btf__type_cnt(d->btf);
	const struct btf_type *t;
	__u16 vlen;

	for (i = d->last_id + 1; i < n; i++) {
		t = btf__type_by_id(d->btf, i);
		vlen = btf_vlen(t);

		switch (btf_kind(t)) {
		case BTF_KIND_INT:
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
		case BTF_KIND_FWD:
		case BTF_KIND_FLOAT:
			break;

		case BTF_KIND_VOLATILE:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_PTR:
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_FUNC:
		case BTF_KIND_VAR:
		case BTF_KIND_DECL_TAG:
		case BTF_KIND_TYPE_TAG:
			d->type_states[t->type].referenced = 1;
			break;

		case BTF_KIND_ARRAY: {
			const struct btf_array *a = btf_array(t);

			d->type_states[a->index_type].referenced = 1;
			d->type_states[a->type].referenced = 1;
			break;
		}
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION: {
			const struct btf_member *m = btf_members(t);

			for (j = 0; j < vlen; j++, m++)
				d->type_states[m->type].referenced = 1;
			break;
		}
		case BTF_KIND_FUNC_PROTO: {
			const struct btf_param *p = btf_params(t);

			for (j = 0; j < vlen; j++, p++)
				d->type_states[p->type].referenced = 1;
			break;
		}
		case BTF_KIND_DATASEC: {
			const struct btf_var_secinfo *v = btf_var_secinfos(t);

			for (j = 0; j < vlen; j++, v++)
				d->type_states[v->type].referenced = 1;
			break;
		}
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static int btf_dump_add_emit_queue_id(struct btf_dump *d, __u32 id)
{
	__u32 *new_queue;
	size_t new_cap;

	if (d->emit_queue_cnt >= d->emit_queue_cap) {
		new_cap = max(16, d->emit_queue_cap * 3 / 2);
		new_queue = libbpf_reallocarray(d->emit_queue, new_cap, sizeof(new_queue[0]));
		if (!new_queue)
			return -ENOMEM;
		d->emit_queue = new_queue;
		d->emit_queue_cap = new_cap;
	}

	d->emit_queue[d->emit_queue_cnt++] = id;
	return 0;
}

/*
 * Determine order of emitting dependent types and specified type to satisfy
 * C compilation rules.  This is done through topological sorting with an
 * additional complication which comes from C rules. The main idea for C is
 * that if some type is "embedded" into a struct/union, it's size needs to be
 * known at the time of definition of containing type. E.g., for:
 *
 *	struct A {};
 *	struct B { struct A x; }
 *
 * struct A *HAS* to be defined before struct B, because it's "embedded",
 * i.e., it is part of struct B layout. But in the following case:
 *
 *	struct A;
 *	struct B { struct A *x; }
 *	struct A {};
 *
 * it's enough to just have a forward declaration of struct A at the time of
 * struct B definition, as struct B has a pointer to struct A, so the size of
 * field x is known without knowing struct A size: it's sizeof(void *).
 *
 * Unfortunately, there are some trickier cases we need to handle, e.g.:
 *
 *	struct A {}; // if this was forward-declaration: compilation error
 *	struct B {
 *		struct { // anonymous struct
 *			struct A y;
 *		} *x;
 *	};
 *
 * In this case, struct B's field x is a pointer, so it's size is known
 * regardless of the size of (anonymous) struct it points to. But because this
 * struct is anonymous and thus defined inline inside struct B, *and* it
 * embeds struct A, compiler requires full definition of struct A to be known
 * before struct B can be defined. This creates a transitive dependency
 * between struct A and struct B. If struct A was forward-declared before
 * struct B definition and fully defined after struct B definition, that would
 * trigger compilation error.
 *
 * All this means that while we are doing topological sorting on BTF type
 * graph, we need to determine relationships between different types (graph
 * nodes):
 *   - weak link (relationship) between X and Y, if Y *CAN* be
 *   forward-declared at the point of X definition;
 *   - strong link, if Y *HAS* to be fully-defined before X can be defined.
 *
 * The rule is as follows. Given a chain of BTF types from X to Y, if there is
 * BTF_KIND_PTR type in the chain and at least one non-anonymous type
 * Z (excluding X, including Y), then link is weak. Otherwise, it's strong.
 * Weak/strong relationship is determined recursively during DFS traversal and
 * is returned as a result from btf_dump_order_type().
 *
 * btf_dump_order_type() is trying to avoid unnecessary forward declarations,
 * but it is not guaranteeing that no extraneous forward declarations will be
 * emitted.
 *
 * To avoid extra work, algorithm marks some of BTF types as ORDERED, when
 * it's done with them, but not for all (e.g., VOLATILE, CONST, RESTRICT,
 * ARRAY, FUNC_PROTO), as weak/strong semantics for those depends on the
 * entire graph path, so depending where from one came to that BTF type, it
 * might cause weak or strong ordering. For types like STRUCT/UNION/INT/ENUM,
 * once they are processed, there is no need to do it again, so they are
 * marked as ORDERED. We can mark PTR as ORDERED as well, as it semi-forces
 * weak link, unless subsequent referenced STRUCT/UNION/ENUM is anonymous. But
 * in any case, once those are processed, no need to do it again, as the
 * result won't change.
 *
 * Returns:
 *   - 1, if type is part of strong link (so there is strong topological
 *   ordering requirements);
 *   - 0, if type is part of weak link (so can be satisfied through forward
 *   declaration);
 *   - <0, on error (e.g., unsatisfiable type loop detected).
 */
static int btf_dump_order_type(struct btf_dump *d, __u32 id, bool through_ptr)
{
	/*
	 * Order state is used to detect strong link cycles, but only for BTF
	 * kinds that are or could be an independent definition (i.e.,
	 * stand-alone fwd decl, enum, typedef, struct, union). Ptrs, arrays,
	 * func_protos, modifiers are just means to get to these definitions.
	 * Int/void don't need definitions, they are assumed to be always
	 * properly defined.  We also ignore datasec, var, and funcs for now.
	 * So for all non-defining kinds, we never even set ordering state,
	 * for defining kinds we set ORDERING and subsequently ORDERED if it
	 * forms a strong link.
	 */
	struct btf_dump_type_aux_state *tstate = &d->type_states[id];
	const struct btf_type *t;
	__u16 vlen;
	int err, i;

	/* return true, letting typedefs know that it's ok to be emitted */
	if (tstate->order_state == ORDERED)
		return 1;

	t = btf__type_by_id(d->btf, id);

	if (tstate->order_state == ORDERING) {
		/* type loop, but resolvable through fwd declaration */
		if (btf_is_composite(t) && through_ptr && t->name_off != 0)
			return 0;
		pr_warn("unsatisfiable type cycle, id:[%u]\n", id);
		return -ELOOP;
	}

	switch (btf_kind(t)) {
	case BTF_KIND_INT:
	case BTF_KIND_FLOAT:
		tstate->order_state = ORDERED;
		return 0;

	case BTF_KIND_PTR:
		err = btf_dump_order_type(d, t->type, true);
		tstate->order_state = ORDERED;
		return err;

	case BTF_KIND_ARRAY:
		return btf_dump_order_type(d, btf_array(t)->type, false);

	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *m = btf_members(t);
		/*
		 * struct/union is part of strong link, only if it's embedded
		 * (so no ptr in a path) or it's anonymous (so has to be
		 * defined inline, even if declared through ptr)
		 */
		if (through_ptr && t->name_off != 0)
			return 0;

		tstate->order_state = ORDERING;

		vlen = btf_vlen(t);
		for (i = 0; i < vlen; i++, m++) {
			err = btf_dump_order_type(d, m->type, false);
			if (err < 0)
				return err;
		}

		if (t->name_off != 0) {
			err = btf_dump_add_emit_queue_id(d, id);
			if (err < 0)
				return err;
		}

		tstate->order_state = ORDERED;
		return 1;
	}
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
	case BTF_KIND_FWD:
		/*
		 * non-anonymous or non-referenced enums are top-level
		 * declarations and should be emitted. Same logic can be
		 * applied to FWDs, it won't hurt anyways.
		 */
		if (t->name_off != 0 || !tstate->referenced) {
			err = btf_dump_add_emit_queue_id(d, id);
			if (err)
				return err;
		}
		tstate->order_state = ORDERED;
		return 1;

	case BTF_KIND_TYPEDEF: {
		int is_strong;

		is_strong = btf_dump_order_type(d, t->type, through_ptr);
		if (is_strong < 0)
			return is_strong;

		/* typedef is similar to struct/union w.r.t. fwd-decls */
		if (through_ptr && !is_strong)
			return 0;

		/* typedef is always a named definition */
		err = btf_dump_add_emit_queue_id(d, id);
		if (err)
			return err;

		d->type_states[id].order_state = ORDERED;
		return 1;
	}
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPE_TAG:
		return btf_dump_order_type(d, t->type, through_ptr);

	case BTF_KIND_FUNC_PROTO: {
		const struct btf_param *p = btf_params(t);
		bool is_strong;

		err = btf_dump_order_type(d, t->type, through_ptr);
		if (err < 0)
			return err;
		is_strong = err > 0;

		vlen = btf_vlen(t);
		for (i = 0; i < vlen; i++, p++) {
			err = btf_dump_order_type(d, p->type, through_ptr);
			if (err < 0)
				return err;
			if (err > 0)
				is_strong = true;
		}
		return is_strong;
	}
	case BTF_KIND_FUNC:
	case BTF_KIND_VAR:
	case BTF_KIND_DATASEC:
	case BTF_KIND_DECL_TAG:
		d->type_states[id].order_state = ORDERED;
		return 0;

	default:
		return -EINVAL;
	}
}

static void btf_dump_emit_missing_aliases(struct btf_dump *d, __u32 id,
					  const struct btf_type *t);

static void btf_dump_emit_struct_fwd(struct btf_dump *d, __u32 id,
				     const struct btf_type *t);
static void btf_dump_emit_struct_def(struct btf_dump *d, __u32 id,
				     const struct btf_type *t, int lvl);

static void btf_dump_emit_enum_fwd(struct btf_dump *d, __u32 id,
				   const struct btf_type *t);
static void btf_dump_emit_enum_def(struct btf_dump *d, __u32 id,
				   const struct btf_type *t, int lvl);

static void btf_dump_emit_fwd_def(struct btf_dump *d, __u32 id,
				  const struct btf_type *t);

static void btf_dump_emit_typedef_def(struct btf_dump *d, __u32 id,
				      const struct btf_type *t, int lvl);

/* a local view into a shared stack */
struct id_stack {
	const __u32 *ids;
	int cnt;
};

static void btf_dump_emit_type_decl(struct btf_dump *d, __u32 id,
				    const char *fname, int lvl);
static void btf_dump_emit_type_chain(struct btf_dump *d,
				     struct id_stack *decl_stack,
				     const char *fname, int lvl);

static const char *btf_dump_type_name(struct btf_dump *d, __u32 id);
static const char *btf_dump_ident_name(struct btf_dump *d, __u32 id);
static size_t btf_dump_name_dups(struct btf_dump *d, struct hashmap *name_map,
				 const char *orig_name);

static bool btf_dump_is_blacklisted(struct btf_dump *d, __u32 id)
{
	const struct btf_type *t = btf__type_by_id(d->btf, id);

	/* __builtin_va_list is a compiler built-in, which causes compilation
	 * errors, when compiling w/ different compiler, then used to compile
	 * original code (e.g., GCC to compile kernel, Clang to use generated
	 * C header from BTF). As it is built-in, it should be already defined
	 * properly internally in compiler.
	 */
	if (t->name_off == 0)
		return false;
	return strcmp(btf_name_of(d, t->name_off), "__builtin_va_list") == 0;
}

/*
 * Emit C-syntax definitions of types from chains of BTF types.
 *
 * High-level handling of determining necessary forward declarations are handled
 * by btf_dump_emit_type() itself, but all nitty-gritty details of emitting type
 * declarations/definitions in C syntax  are handled by a combo of
 * btf_dump_emit_type_decl()/btf_dump_emit_type_chain() w/ delegation to
 * corresponding btf_dump_emit_*_{def,fwd}() functions.
 *
 * We also keep track of "containing struct/union type ID" to determine when
 * we reference it from inside and thus can avoid emitting unnecessary forward
 * declaration.
 *
 * This algorithm is designed in such a way, that even if some error occurs
 * (either technical, e.g., out of memory, or logical, i.e., malformed BTF
 * that doesn't comply to C rules completely), algorithm will try to proceed
 * and produce as much meaningful output as possible.
 */
static void btf_dump_emit_type(struct btf_dump *d, __u32 id, __u32 cont_id)
{
	struct btf_dump_type_aux_state *tstate = &d->type_states[id];
	bool top_level_def = cont_id == 0;
	const struct btf_type *t;
	__u16 kind;

	if (tstate->emit_state == EMITTED)
		return;

	t = btf__type_by_id(d->btf, id);
	kind = btf_kind(t);

	if (tstate->emit_state == EMITTING) {
		if (tstate->fwd_emitted)
			return;

		switch (kind) {
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
			/*
			 * if we are referencing a struct/union that we are
			 * part of - then no need for fwd declaration
			 */
			if (id == cont_id)
				return;
			if (t->name_off == 0) {
				pr_warn("anonymous struct/union loop, id:[%u]\n",
					id);
				return;
			}
			btf_dump_emit_struct_fwd(d, id, t);
			btf_dump_printf(d, ";\n\n");
			tstate->fwd_emitted = 1;
			break;
		case BTF_KIND_TYPEDEF:
			/*
			 * for typedef fwd_emitted means typedef definition
			 * was emitted, but it can be used only for "weak"
			 * references through pointer only, not for embedding
			 */
			if (!btf_dump_is_blacklisted(d, id)) {
				btf_dump_emit_typedef_def(d, id, t, 0);
				btf_dump_printf(d, ";\n\n");
			}
			tstate->fwd_emitted = 1;
			break;
		default:
			break;
		}

		return;
	}

	switch (kind) {
	case BTF_KIND_INT:
		/* Emit type alias definitions if necessary */
		btf_dump_emit_missing_aliases(d, id, t);

		tstate->emit_state = EMITTED;
		break;
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		if (top_level_def) {
			btf_dump_emit_enum_def(d, id, t, 0);
			btf_dump_printf(d, ";\n\n");
		}
		tstate->emit_state = EMITTED;
		break;
	case BTF_KIND_PTR:
	case BTF_KIND_VOLATILE:
	case BTF_KIND_CONST:
	case BTF_KIND_RESTRICT:
	case BTF_KIND_TYPE_TAG:
		btf_dump_emit_type(d, t->type, cont_id);
		break;
	case BTF_KIND_ARRAY:
		btf_dump_emit_type(d, btf_array(t)->type, cont_id);
		break;
	case BTF_KIND_FWD:
		btf_dump_emit_fwd_def(d, id, t);
		btf_dump_printf(d, ";\n\n");
		tstate->emit_state = EMITTED;
		break;
	case BTF_KIND_TYPEDEF:
		tstate->emit_state = EMITTING;
		btf_dump_emit_type(d, t->type, id);
		/*
		 * typedef can server as both definition and forward
		 * declaration; at this stage someone depends on
		 * typedef as a forward declaration (refers to it
		 * through pointer), so unless we already did it,
		 * emit typedef as a forward declaration
		 */
		if (!tstate->fwd_emitted && !btf_dump_is_blacklisted(d, id)) {
			btf_dump_emit_typedef_def(d, id, t, 0);
			btf_dump_printf(d, ";\n\n");
		}
		tstate->emit_state = EMITTED;
		break;
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		tstate->emit_state = EMITTING;
		/* if it's a top-level struct/union definition or struct/union
		 * is anonymous, then in C we'll be emitting all fields and
		 * their types (as opposed to just `struct X`), so we need to
		 * make sure that all types, referenced from struct/union
		 * members have necessary forward-declarations, where
		 * applicable
		 */
		if (top_level_def || t->name_off == 0) {
			const struct btf_member *m = btf_members(t);
			__u16 vlen = btf_vlen(t);
			int i, new_cont_id;

			new_cont_id = t->name_off == 0 ? cont_id : id;
			for (i = 0; i < vlen; i++, m++)
				btf_dump_emit_type(d, m->type, new_cont_id);
		} else if (!tstate->fwd_emitted && id != cont_id) {
			btf_dump_emit_struct_fwd(d, id, t);
			btf_dump_printf(d, ";\n\n");
			tstate->fwd_emitted = 1;
		}

		if (top_level_def) {
			btf_dump_emit_struct_def(d, id, t, 0);
			btf_dump_printf(d, ";\n\n");
			tstate->emit_state = EMITTED;
		} else {
			tstate->emit_state = NOT_EMITTED;
		}
		break;
	case BTF_KIND_FUNC_PROTO: {
		const struct btf_param *p = btf_params(t);
		__u16 n = btf_vlen(t);
		int i;

		btf_dump_emit_type(d, t->type, cont_id);
		for (i = 0; i < n; i++, p++)
			btf_dump_emit_type(d, p->type, cont_id);

		break;
	}
	default:
		break;
	}
}

static bool btf_is_struct_packed(const struct btf *btf, __u32 id,
				 const struct btf_type *t)
{
	const struct btf_member *m;
	int max_align = 1, align, i, bit_sz;
	__u16 vlen;

	m = btf_members(t);
	vlen = btf_vlen(t);
	/* all non-bitfield fields have to be naturally aligned */
	for (i = 0; i < vlen; i++, m++) {
		align = btf__align_of(btf, m->type);
		bit_sz = btf_member_bitfield_size(t, i);
		if (align && bit_sz == 0 && m->offset % (8 * align) != 0)
			return true;
		max_align = max(align, max_align);
	}
	/* size of a non-packed struct has to be a multiple of its alignment */
	if (t->size % max_align != 0)
		return true;
	/*
	 * if original struct was marked as packed, but its layout is
	 * naturally aligned, we'll detect that it's not packed
	 */
	return false;
}

static void btf_dump_emit_bit_padding(const struct btf_dump *d,
				      int cur_off, int next_off, int next_align,
				      bool in_bitfield, int lvl)
{
	const struct {
		const char *name;
		int bits;
	} pads[] = {
		{"long", d->ptr_sz * 8}, {"int", 32}, {"short", 16}, {"char", 8}
	};
	int new_off = 0, pad_bits = 0, bits, i;
	const char *pad_type = NULL;

	if (cur_off >= next_off)
		return; /* no gap */

	/* For filling out padding we want to take advantage of
	 * natural alignment rules to minimize unnecessary explicit
	 * padding. First, we find the largest type (among long, int,
	 * short, or char) that can be used to force naturally aligned
	 * boundary. Once determined, we'll use such type to fill in
	 * the remaining padding gap. In some cases we can rely on
	 * compiler filling some gaps, but sometimes we need to force
	 * alignment to close natural alignment with markers like
	 * `long: 0` (this is always the case for bitfields).  Note
	 * that even if struct itself has, let's say 4-byte alignment
	 * (i.e., it only uses up to int-aligned types), using `long:
	 * X;` explicit padding doesn't actually change struct's
	 * overall alignment requirements, but compiler does take into
	 * account that type's (long, in this example) natural
	 * alignment requirements when adding implicit padding. We use
	 * this fact heavily and don't worry about ruining correct
	 * struct alignment requirement.
	 */
	for (i = 0; i < ARRAY_SIZE(pads); i++) {
		pad_bits = pads[i].bits;
		pad_type = pads[i].name;

		new_off = roundup(cur_off, pad_bits);
		if (new_off <= next_off)
			break;
	}

	if (new_off > cur_off && new_off <= next_off) {
		/* We need explicit `<type>: 0` aligning mark if next
		 * field is right on alignment offset and its
		 * alignment requirement is less strict than <type>'s
		 * alignment (so compiler won't naturally align to the
		 * offset we expect), or if subsequent `<type>: X`,
		 * will actually completely fit in the remaining hole,
		 * making compiler basically ignore `<type>: X`
		 * completely.
		 */
		if (in_bitfield ||
		    (new_off == next_off && roundup(cur_off, next_align * 8) != new_off) ||
		    (new_off != next_off && next_off - new_off <= new_off - cur_off))
			/* but for bitfields we'll emit explicit bit count */
			btf_dump_printf(d, "\n%s%s: %d;", pfx(lvl), pad_type,
					in_bitfield ? new_off - cur_off : 0);
		cur_off = new_off;
	}

	/* Now we know we start at naturally aligned offset for a chosen
	 * padding type (long, int, short, or char), and so the rest is just
	 * a straightforward filling of remaining padding gap with full
	 * `<type>: sizeof(<type>);` markers, except for the last one, which
	 * might need smaller than sizeof(<type>) padding.
	 */
	while (cur_off != next_off) {
		bits = min(next_off - cur_off, pad_bits);
		if (bits == pad_bits) {
			btf_dump_printf(d, "\n%s%s: %d;", pfx(lvl), pad_type, pad_bits);
			cur_off += bits;
			continue;
		}
		/* For the remainder padding that doesn't cover entire
		 * pad_type bit length, we pick the smallest necessary type.
		 * This is pure aesthetics, we could have just used `long`,
		 * but having smallest necessary one communicates better the
		 * scale of the padding gap.
		 */
		for (i = ARRAY_SIZE(pads) - 1; i >= 0; i--) {
			pad_type = pads[i].name;
			pad_bits = pads[i].bits;
			if (pad_bits < bits)
				continue;

			btf_dump_printf(d, "\n%s%s: %d;", pfx(lvl), pad_type, bits);
			cur_off += bits;
			break;
		}
	}
}

static void btf_dump_emit_struct_fwd(struct btf_dump *d, __u32 id,
				     const struct btf_type *t)
{
	btf_dump_printf(d, "%s%s%s",
			btf_is_struct(t) ? "struct" : "union",
			t->name_off ? " " : "",
			btf_dump_type_name(d, id));
}

static void btf_dump_emit_struct_def(struct btf_dump *d,
				     __u32 id,
				     const struct btf_type *t,
				     int lvl)
{
	const struct btf_member *m = btf_members(t);
	bool is_struct = btf_is_struct(t);
	bool packed, prev_bitfield = false;
	int align, i, off = 0;
	__u16 vlen = btf_vlen(t);

	align = btf__align_of(d->btf, id);
	packed = is_struct ? btf_is_struct_packed(d->btf, id, t) : 0;

	btf_dump_printf(d, "%s%s%s {",
			is_struct ? "struct" : "union",
			t->name_off ? " " : "",
			btf_dump_type_name(d, id));

	for (i = 0; i < vlen; i++, m++) {
		const char *fname;
		int m_off, m_sz, m_align;
		bool in_bitfield;

		fname = btf_name_of(d, m->name_off);
		m_sz = btf_member_bitfield_size(t, i);
		m_off = btf_member_bit_offset(t, i);
		m_align = packed ? 1 : btf__align_of(d->btf, m->type);

		in_bitfield = prev_bitfield && m_sz != 0;

		btf_dump_emit_bit_padding(d, off, m_off, m_align, in_bitfield, lvl + 1);
		btf_dump_printf(d, "\n%s", pfx(lvl + 1));
		btf_dump_emit_type_decl(d, m->type, fname, lvl + 1);

		if (m_sz) {
			btf_dump_printf(d, ": %d", m_sz);
			off = m_off + m_sz;
			prev_bitfield = true;
		} else {
			m_sz = max((__s64)0, btf__resolve_size(d->btf, m->type));
			off = m_off + m_sz * 8;
			prev_bitfield = false;
		}

		btf_dump_printf(d, ";");
	}

	/* pad at the end, if necessary */
	if (is_struct)
		btf_dump_emit_bit_padding(d, off, t->size * 8, align, false, lvl + 1);

	/*
	 * Keep `struct empty {}` on a single line,
	 * only print newline when there are regular or padding fields.
	 */
	if (vlen || t->size) {
		btf_dump_printf(d, "\n");
		btf_dump_printf(d, "%s}", pfx(lvl));
	} else {
		btf_dump_printf(d, "}");
	}
	if (packed)
		btf_dump_printf(d, " __attribute__((packed))");
}

static const char *missing_base_types[][2] = {
	/*
	 * GCC emits typedefs to its internal __PolyX_t types when compiling Arm
	 * SIMD intrinsics. Alias them to standard base types.
	 */
	{ "__Poly8_t",		"unsigned char" },
	{ "__Poly16_t",		"unsigned short" },
	{ "__Poly64_t",		"unsigned long long" },
	{ "__Poly128_t",	"unsigned __int128" },
};

static void btf_dump_emit_missing_aliases(struct btf_dump *d, __u32 id,
					  const struct btf_type *t)
{
	const char *name = btf_dump_type_name(d, id);
	int i;

	for (i = 0; i < ARRAY_SIZE(missing_base_types); i++) {
		if (strcmp(name, missing_base_types[i][0]) == 0) {
			btf_dump_printf(d, "typedef %s %s;\n\n",
					missing_base_types[i][1], name);
			break;
		}
	}
}

static void btf_dump_emit_enum_fwd(struct btf_dump *d, __u32 id,
				   const struct btf_type *t)
{
	btf_dump_printf(d, "enum %s", btf_dump_type_name(d, id));
}

static void btf_dump_emit_enum32_val(struct btf_dump *d,
				     const struct btf_type *t,
				     int lvl, __u16 vlen)
{
	const struct btf_enum *v = btf_enum(t);
	bool is_signed = btf_kflag(t);
	const char *fmt_str;
	const char *name;
	size_t dup_cnt;
	int i;

	for (i = 0; i < vlen; i++, v++) {
		name = btf_name_of(d, v->name_off);
		/* enumerators share namespace with typedef idents */
		dup_cnt = btf_dump_name_dups(d, d->ident_names, name);
		if (dup_cnt > 1) {
			fmt_str = is_signed ? "\n%s%s___%zd = %d," : "\n%s%s___%zd = %u,";
			btf_dump_printf(d, fmt_str, pfx(lvl + 1), name, dup_cnt, v->val);
		} else {
			fmt_str = is_signed ? "\n%s%s = %d," : "\n%s%s = %u,";
			btf_dump_printf(d, fmt_str, pfx(lvl + 1), name, v->val);
		}
	}
}

static void btf_dump_emit_enum64_val(struct btf_dump *d,
				     const struct btf_type *t,
				     int lvl, __u16 vlen)
{
	const struct btf_enum64 *v = btf_enum64(t);
	bool is_signed = btf_kflag(t);
	const char *fmt_str;
	const char *name;
	size_t dup_cnt;
	__u64 val;
	int i;

	for (i = 0; i < vlen; i++, v++) {
		name = btf_name_of(d, v->name_off);
		dup_cnt = btf_dump_name_dups(d, d->ident_names, name);
		val = btf_enum64_value(v);
		if (dup_cnt > 1) {
			fmt_str = is_signed ? "\n%s%s___%zd = %lldLL,"
					    : "\n%s%s___%zd = %lluULL,";
			btf_dump_printf(d, fmt_str,
					pfx(lvl + 1), name, dup_cnt,
					(unsigned long long)val);
		} else {
			fmt_str = is_signed ? "\n%s%s = %lldLL,"
					    : "\n%s%s = %lluULL,";
			btf_dump_printf(d, fmt_str,
					pfx(lvl + 1), name,
					(unsigned long long)val);
		}
	}
}
static void btf_dump_emit_enum_def(struct btf_dump *d, __u32 id,
				   const struct btf_type *t,
				   int lvl)
{
	__u16 vlen = btf_vlen(t);

	btf_dump_printf(d, "enum%s%s",
			t->name_off ? " " : "",
			btf_dump_type_name(d, id));

	if (!vlen)
		return;

	btf_dump_printf(d, " {");
	if (btf_is_enum(t))
		btf_dump_emit_enum32_val(d, t, lvl, vlen);
	else
		btf_dump_emit_enum64_val(d, t, lvl, vlen);
	btf_dump_printf(d, "\n%s}", pfx(lvl));

	/* special case enums with special sizes */
	if (t->size == 1) {
		/* one-byte enums can be forced with mode(byte) attribute */
		btf_dump_printf(d, " __attribute__((mode(byte)))");
	} else if (t->size == 8 && d->ptr_sz == 8) {
		/* enum can be 8-byte sized if one of the enumerator values
		 * doesn't fit in 32-bit integer, or by adding mode(word)
		 * attribute (but probably only on 64-bit architectures); do
		 * our best here to try to satisfy the contract without adding
		 * unnecessary attributes
		 */
		bool needs_word_mode;

		if (btf_is_enum(t)) {
			/* enum can't represent 64-bit values, so we need word mode */
			needs_word_mode = true;
		} else {
			/* enum64 needs mode(word) if none of its values has
			 * non-zero upper 32-bits (which means that all values
			 * fit in 32-bit integers and won't cause compiler to
			 * bump enum to be 64-bit naturally
			 */
			int i;

			needs_word_mode = true;
			for (i = 0; i < vlen; i++) {
				if (btf_enum64(t)[i].val_hi32 != 0) {
					needs_word_mode = false;
					break;
				}
			}
		}
		if (needs_word_mode)
			btf_dump_printf(d, " __attribute__((mode(word)))");
	}

}

static void btf_dump_emit_fwd_def(struct btf_dump *d, __u32 id,
				  const struct btf_type *t)
{
	const char *name = btf_dump_type_name(d, id);

	if (btf_kflag(t))
		btf_dump_printf(d, "union %s", name);
	else
		btf_dump_printf(d, "struct %s", name);
}

static void btf_dump_emit_typedef_def(struct btf_dump *d, __u32 id,
				     const struct btf_type *t, int lvl)
{
	const char *name = btf_dump_ident_name(d, id);

	/*
	 * Old GCC versions are emitting invalid typedef for __gnuc_va_list
	 * pointing to VOID. This generates warnings from btf_dump() and
	 * results in uncompilable header file, so we are fixing it up here
	 * with valid typedef into __builtin_va_list.
	 */
	if (t->type == 0 && strcmp(name, "__gnuc_va_list") == 0) {
		btf_dump_printf(d, "typedef __builtin_va_list __gnuc_va_list");
		return;
	}

	btf_dump_printf(d, "typedef ");
	btf_dump_emit_type_decl(d, t->type, name, lvl);
}

static int btf_dump_push_decl_stack_id(struct btf_dump *d, __u32 id)
{
	__u32 *new_stack;
	size_t new_cap;

	if (d->decl_stack_cnt >= d->decl_stack_cap) {
		new_cap = max(16, d->decl_stack_cap * 3 / 2);
		new_stack = libbpf_reallocarray(d->decl_stack, new_cap, sizeof(new_stack[0]));
		if (!new_stack)
			return -ENOMEM;
		d->decl_stack = new_stack;
		d->decl_stack_cap = new_cap;
	}

	d->decl_stack[d->decl_stack_cnt++] = id;

	return 0;
}

/*
 * Emit type declaration (e.g., field type declaration in a struct or argument
 * declaration in function prototype) in correct C syntax.
 *
 * For most types it's trivial, but there are few quirky type declaration
 * cases worth mentioning:
 *   - function prototypes (especially nesting of function prototypes);
 *   - arrays;
 *   - const/volatile/restrict for pointers vs other types.
 *
 * For a good discussion of *PARSING* C syntax (as a human), see
 * Peter van der Linden's "Expert C Programming: Deep C Secrets",
 * Ch.3 "Unscrambling Declarations in C".
 *
 * It won't help with BTF to C conversion much, though, as it's an opposite
 * problem. So we came up with this algorithm in reverse to van der Linden's
 * parsing algorithm. It goes from structured BTF representation of type
 * declaration to a valid compilable C syntax.
 *
 * For instance, consider this C typedef:
 *	typedef const int * const * arr[10] arr_t;
 * It will be represented in BTF with this chain of BTF types:
 *	[typedef] -> [array] -> [ptr] -> [const] -> [ptr] -> [const] -> [int]
 *
 * Notice how [const] modifier always goes before type it modifies in BTF type
 * graph, but in C syntax, const/volatile/restrict modifiers are written to
 * the right of pointers, but to the left of other types. There are also other
 * quirks, like function pointers, arrays of them, functions returning other
 * functions, etc.
 *
 * We handle that by pushing all the types to a stack, until we hit "terminal"
 * type (int/enum/struct/union/fwd). Then depending on the kind of a type on
 * top of a stack, modifiers are handled differently. Array/function pointers
 * have also wildly different syntax and how nesting of them are done. See
 * code for authoritative definition.
 *
 * To avoid allocating new stack for each independent chain of BTF types, we
 * share one bigger stack, with each chain working only on its own local view
 * of a stack frame. Some care is required to "pop" stack frames after
 * processing type declaration chain.
 */
int btf_dump__emit_type_decl(struct btf_dump *d, __u32 id,
			     const struct btf_dump_emit_type_decl_opts *opts)
{
	const char *fname;
	int lvl, err;

	if (!OPTS_VALID(opts, btf_dump_emit_type_decl_opts))
		return libbpf_err(-EINVAL);

	err = btf_dump_resize(d);
	if (err)
		return libbpf_err(err);

	fname = OPTS_GET(opts, field_name, "");
	lvl = OPTS_GET(opts, indent_level, 0);
	d->strip_mods = OPTS_GET(opts, strip_mods, false);
	btf_dump_emit_type_decl(d, id, fname, lvl);
	d->strip_mods = false;
	return 0;
}

static void btf_dump_emit_type_decl(struct btf_dump *d, __u32 id,
				    const char *fname, int lvl)
{
	struct id_stack decl_stack;
	const struct btf_type *t;
	int err, stack_start;

	stack_start = d->decl_stack_cnt;
	for (;;) {
		t = btf__type_by_id(d->btf, id);
		if (d->strip_mods && btf_is_mod(t))
			goto skip_mod;

		err = btf_dump_push_decl_stack_id(d, id);
		if (err < 0) {
			/*
			 * if we don't have enough memory for entire type decl
			 * chain, restore stack, emit warning, and try to
			 * proceed nevertheless
			 */
			pr_warn("not enough memory for decl stack: %s\n", errstr(err));
			d->decl_stack_cnt = stack_start;
			return;
		}
skip_mod:
		/* VOID */
		if (id == 0)
			break;

		switch (btf_kind(t)) {
		case BTF_KIND_PTR:
		case BTF_KIND_VOLATILE:
		case BTF_KIND_CONST:
		case BTF_KIND_RESTRICT:
		case BTF_KIND_FUNC_PROTO:
		case BTF_KIND_TYPE_TAG:
			id = t->type;
			break;
		case BTF_KIND_ARRAY:
			id = btf_array(t)->type;
			break;
		case BTF_KIND_INT:
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
		case BTF_KIND_FWD:
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
		case BTF_KIND_TYPEDEF:
		case BTF_KIND_FLOAT:
			goto done;
		default:
			pr_warn("unexpected type in decl chain, kind:%u, id:[%u]\n",
				btf_kind(t), id);
			goto done;
		}
	}
done:
	/*
	 * We might be inside a chain of declarations (e.g., array of function
	 * pointers returning anonymous (so inlined) structs, having another
	 * array field). Each of those needs its own "stack frame" to handle
	 * emitting of declarations. Those stack frames are non-overlapping
	 * portions of shared btf_dump->decl_stack. To make it a bit nicer to
	 * handle this set of nested stacks, we create a view corresponding to
	 * our own "stack frame" and work with it as an independent stack.
	 * We'll need to clean up after emit_type_chain() returns, though.
	 */
	decl_stack.ids = d->decl_stack + stack_start;
	decl_stack.cnt = d->decl_stack_cnt - stack_start;
	btf_dump_emit_type_chain(d, &decl_stack, fname, lvl);
	/*
	 * emit_type_chain() guarantees that it will pop its entire decl_stack
	 * frame before returning. But it works with a read-only view into
	 * decl_stack, so it doesn't actually pop anything from the
	 * perspective of shared btf_dump->decl_stack, per se. We need to
	 * reset decl_stack state to how it was before us to avoid it growing
	 * all the time.
	 */
	d->decl_stack_cnt = stack_start;
}

static void btf_dump_emit_mods(struct btf_dump *d, struct id_stack *decl_stack)
{
	const struct btf_type *t;
	__u32 id;

	while (decl_stack->cnt) {
		id = decl_stack->ids[decl_stack->cnt - 1];
		t = btf__type_by_id(d->btf, id);

		switch (btf_kind(t)) {
		case BTF_KIND_VOLATILE:
			btf_dump_printf(d, "volatile ");
			break;
		case BTF_KIND_CONST:
			btf_dump_printf(d, "const ");
			break;
		case BTF_KIND_RESTRICT:
			btf_dump_printf(d, "restrict ");
			break;
		default:
			return;
		}
		decl_stack->cnt--;
	}
}

static void btf_dump_drop_mods(struct btf_dump *d, struct id_stack *decl_stack)
{
	const struct btf_type *t;
	__u32 id;

	while (decl_stack->cnt) {
		id = decl_stack->ids[decl_stack->cnt - 1];
		t = btf__type_by_id(d->btf, id);
		if (!btf_is_mod(t))
			return;
		decl_stack->cnt--;
	}
}

static void btf_dump_emit_name(const struct btf_dump *d,
			       const char *name, bool last_was_ptr)
{
	bool separate = name[0] && !last_was_ptr;

	btf_dump_printf(d, "%s%s", separate ? " " : "", name);
}

static void btf_dump_emit_type_chain(struct btf_dump *d,
				     struct id_stack *decls,
				     const char *fname, int lvl)
{
	/*
	 * last_was_ptr is used to determine if we need to separate pointer
	 * asterisk (*) from previous part of type signature with space, so
	 * that we get `int ***`, instead of `int * * *`. We default to true
	 * for cases where we have single pointer in a chain. E.g., in ptr ->
	 * func_proto case. func_proto will start a new emit_type_chain call
	 * with just ptr, which should be emitted as (*) or (*<fname>), so we
	 * don't want to prepend space for that last pointer.
	 */
	bool last_was_ptr = true;
	const struct btf_type *t;
	const char *name;
	__u16 kind;
	__u32 id;

	while (decls->cnt) {
		id = decls->ids[--decls->cnt];
		if (id == 0) {
			/* VOID is a special snowflake */
			btf_dump_emit_mods(d, decls);
			btf_dump_printf(d, "void");
			last_was_ptr = false;
			continue;
		}

		t = btf__type_by_id(d->btf, id);
		kind = btf_kind(t);

		switch (kind) {
		case BTF_KIND_INT:
		case BTF_KIND_FLOAT:
			btf_dump_emit_mods(d, decls);
			name = btf_name_of(d, t->name_off);
			btf_dump_printf(d, "%s", name);
			break;
		case BTF_KIND_STRUCT:
		case BTF_KIND_UNION:
			btf_dump_emit_mods(d, decls);
			/* inline anonymous struct/union */
			if (t->name_off == 0 && !d->skip_anon_defs)
				btf_dump_emit_struct_def(d, id, t, lvl);
			else
				btf_dump_emit_struct_fwd(d, id, t);
			break;
		case BTF_KIND_ENUM:
		case BTF_KIND_ENUM64:
			btf_dump_emit_mods(d, decls);
			/* inline anonymous enum */
			if (t->name_off == 0 && !d->skip_anon_defs)
				btf_dump_emit_enum_def(d, id, t, lvl);
			else
				btf_dump_emit_enum_fwd(d, id, t);
			break;
		case BTF_KIND_FWD:
			btf_dump_emit_mods(d, decls);
			btf_dump_emit_fwd_def(d, id, t);
			break;
		case BTF_KIND_TYPEDEF:
			btf_dump_emit_mods(d, decls);
			btf_dump_printf(d, "%s", btf_dump_ident_name(d, id));
			break;
		case BTF_KIND_PTR:
			btf_dump_printf(d, "%s", last_was_ptr ? "*" : " *");
			break;
		case BTF_KIND_VOLATILE:
			btf_dump_printf(d, " volatile");
			break;
		case BTF_KIND_CONST:
			btf_dump_printf(d, " const");
			break;
		case BTF_KIND_RESTRICT:
			btf_dump_printf(d, " restrict");
			break;
		case BTF_KIND_TYPE_TAG:
			btf_dump_emit_mods(d, decls);
			name = btf_name_of(d, t->name_off);
			if (btf_kflag(t))
				btf_dump_printf(d, " __attribute__((%s))", name);
			else
				btf_dump_printf(d, " __attribute__((btf_type_tag(\"%s\")))", name);
			break;
		case BTF_KIND_ARRAY: {
			const struct btf_array *a = btf_array(t);
			const struct btf_type *next_t;
			__u32 next_id;
			bool multidim;
			/*
			 * GCC has a bug
			 * (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=8354)
			 * which causes it to emit extra const/volatile
			 * modifiers for an array, if array's element type has
			 * const/volatile modifiers. Clang doesn't do that.
			 * In general, it doesn't seem very meaningful to have
			 * a const/volatile modifier for array, so we are
			 * going to silently skip them here.
			 */
			btf_dump_drop_mods(d, decls);

			if (decls->cnt == 0) {
				btf_dump_emit_name(d, fname, last_was_ptr);
				btf_dump_printf(d, "[%u]", a->nelems);
				return;
			}

			next_id = decls->ids[decls->cnt - 1];
			next_t = btf__type_by_id(d->btf, next_id);
			multidim = btf_is_array(next_t);
			/* we need space if we have named non-pointer */
			if (fname[0] && !last_was_ptr)
				btf_dump_printf(d, " ");
			/* no parentheses for multi-dimensional array */
			if (!multidim)
				btf_dump_printf(d, "(");
			btf_dump_emit_type_chain(d, decls, fname, lvl);
			if (!multidim)
				btf_dump_printf(d, ")");
			btf_dump_printf(d, "[%u]", a->nelems);
			return;
		}
		case BTF_KIND_FUNC_PROTO: {
			const struct btf_param *p = btf_params(t);
			__u16 vlen = btf_vlen(t);
			int i;

			/*
			 * GCC emits extra volatile qualifier for
			 * __attribute__((noreturn)) function pointers. Clang
			 * doesn't do it. It's a GCC quirk for backwards
			 * compatibility with code written for GCC <2.5. So,
			 * similarly to extra qualifiers for array, just drop
			 * them, instead of handling them.
			 */
			btf_dump_drop_mods(d, decls);
			if (decls->cnt) {
				btf_dump_printf(d, " (");
				btf_dump_emit_type_chain(d, decls, fname, lvl);
				btf_dump_printf(d, ")");
			} else {
				btf_dump_emit_name(d, fname, last_was_ptr);
			}
			btf_dump_printf(d, "(");
			/*
			 * Clang for BPF target generates func_proto with no
			 * args as a func_proto with a single void arg (e.g.,
			 * `int (*f)(void)` vs just `int (*f)()`). We are
			 * going to emit valid empty args (void) syntax for
			 * such case. Similarly and conveniently, valid
			 * no args case can be special-cased here as well.
			 */
			if (vlen == 0 || (vlen == 1 && p->type == 0)) {
				btf_dump_printf(d, "void)");
				return;
			}

			for (i = 0; i < vlen; i++, p++) {
				if (i > 0)
					btf_dump_printf(d, ", ");

				/* last arg of type void is vararg */
				if (i == vlen - 1 && p->type == 0) {
					btf_dump_printf(d, "...");
					break;
				}

				name = btf_name_of(d, p->name_off);
				btf_dump_emit_type_decl(d, p->type, name, lvl);
			}

			btf_dump_printf(d, ")");
			return;
		}
		default:
			pr_warn("unexpected type in decl chain, kind:%u, id:[%u]\n",
				kind, id);
			return;
		}

		last_was_ptr = kind == BTF_KIND_PTR;
	}

	btf_dump_emit_name(d, fname, last_was_ptr);
}

/* show type name as (type_name) */
static void btf_dump_emit_type_cast(struct btf_dump *d, __u32 id,
				    bool top_level)
{
	const struct btf_type *t;

	/* for array members, we don't bother emitting type name for each
	 * member to avoid the redundancy of
	 * .name = (char[4])[(char)'f',(char)'o',(char)'o',]
	 */
	if (d->typed_dump->is_array_member)
		return;

	/* avoid type name specification for variable/section; it will be done
	 * for the associated variable value(s).
	 */
	t = btf__type_by_id(d->btf, id);
	if (btf_is_var(t) || btf_is_datasec(t))
		return;

	if (top_level)
		btf_dump_printf(d, "(");

	d->skip_anon_defs = true;
	d->strip_mods = true;
	btf_dump_emit_type_decl(d, id, "", 0);
	d->strip_mods = false;
	d->skip_anon_defs = false;

	if (top_level)
		btf_dump_printf(d, ")");
}

/* return number of duplicates (occurrences) of a given name */
static size_t btf_dump_name_dups(struct btf_dump *d, struct hashmap *name_map,
				 const char *orig_name)
{
	char *old_name, *new_name;
	size_t dup_cnt = 0;
	int err;

	new_name = strdup(orig_name);
	if (!new_name)
		return 1;

	(void)hashmap__find(name_map, orig_name, &dup_cnt);
	dup_cnt++;

	err = hashmap__set(name_map, new_name, dup_cnt, &old_name, NULL);
	if (err)
		free(new_name);

	free(old_name);

	return dup_cnt;
}

static const char *btf_dump_resolve_name(struct btf_dump *d, __u32 id,
					 struct hashmap *name_map)
{
	struct btf_dump_type_aux_state *s = &d->type_states[id];
	const struct btf_type *t = btf__type_by_id(d->btf, id);
	const char *orig_name = btf_name_of(d, t->name_off);
	const char **cached_name = &d->cached_names[id];
	size_t dup_cnt;

	if (t->name_off == 0)
		return "";

	if (s->name_resolved)
		return *cached_name ? *cached_name : orig_name;

	if (btf_is_fwd(t) || (btf_is_enum(t) && btf_vlen(t) == 0)) {
		s->name_resolved = 1;
		return orig_name;
	}

	dup_cnt = btf_dump_name_dups(d, name_map, orig_name);
	if (dup_cnt > 1) {
		const size_t max_len = 256;
		char new_name[max_len];

		snprintf(new_name, max_len, "%s___%zu", orig_name, dup_cnt);
		*cached_name = strdup(new_name);
	}

	s->name_resolved = 1;
	return *cached_name ? *cached_name : orig_name;
}

static const char *btf_dump_type_name(struct btf_dump *d, __u32 id)
{
	return btf_dump_resolve_name(d, id, d->type_names);
}

static const char *btf_dump_ident_name(struct btf_dump *d, __u32 id)
{
	return btf_dump_resolve_name(d, id, d->ident_names);
}

static int btf_dump_dump_type_data(struct btf_dump *d,
				   const char *fname,
				   const struct btf_type *t,
				   __u32 id,
				   const void *data,
				   __u8 bits_offset,
				   __u8 bit_sz);

static const char *btf_dump_data_newline(struct btf_dump *d)
{
	return d->typed_dump->compact || d->typed_dump->depth == 0 ? "" : "\n";
}

static const char *btf_dump_data_delim(struct btf_dump *d)
{
	return d->typed_dump->depth == 0 ? "" : ",";
}

static void btf_dump_data_pfx(struct btf_dump *d)
{
	int i, lvl = d->typed_dump->indent_lvl + d->typed_dump->depth;

	if (d->typed_dump->compact)
		return;

	for (i = 0; i < lvl; i++)
		btf_dump_printf(d, "%s", d->typed_dump->indent_str);
}

/* A macro is used here as btf_type_value[s]() appends format specifiers
 * to the format specifier passed in; these do the work of appending
 * delimiters etc while the caller simply has to specify the type values
 * in the format specifier + value(s).
 */
#define btf_dump_type_values(d, fmt, ...)				\
	btf_dump_printf(d, fmt "%s%s",					\
			##__VA_ARGS__,					\
			btf_dump_data_delim(d),				\
			btf_dump_data_newline(d))

static int btf_dump_unsupported_data(struct btf_dump *d,
				     const struct btf_type *t,
				     __u32 id)
{
	btf_dump_printf(d, "<unsupported kind:%u>", btf_kind(t));
	return -ENOTSUP;
}

static int btf_dump_get_bitfield_value(struct btf_dump *d,
				       const struct btf_type *t,
				       const void *data,
				       __u8 bits_offset,
				       __u8 bit_sz,
				       __u64 *value)
{
	__u16 left_shift_bits, right_shift_bits;
	const __u8 *bytes = data;
	__u8 nr_copy_bits;
	__u64 num = 0;
	int i;

	/* Maximum supported bitfield size is 64 bits */
	if (t->size > 8) {
		pr_warn("unexpected bitfield size %d\n", t->size);
		return -EINVAL;
	}

	/* Bitfield value retrieval is done in two steps; first relevant bytes are
	 * stored in num, then we left/right shift num to eliminate irrelevant bits.
	 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	for (i = t->size - 1; i >= 0; i--)
		num = num * 256 + bytes[i];
	nr_copy_bits = bit_sz + bits_offset;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	for (i = 0; i < t->size; i++)
		num = num * 256 + bytes[i];
	nr_copy_bits = t->size * 8 - bits_offset;
#else
# error "Unrecognized __BYTE_ORDER__"
#endif
	left_shift_bits = 64 - nr_copy_bits;
	right_shift_bits = 64 - bit_sz;

	*value = (num << left_shift_bits) >> right_shift_bits;

	return 0;
}

static int btf_dump_bitfield_check_zero(struct btf_dump *d,
					const struct btf_type *t,
					const void *data,
					__u8 bits_offset,
					__u8 bit_sz)
{
	__u64 check_num;
	int err;

	err = btf_dump_get_bitfield_value(d, t, data, bits_offset, bit_sz, &check_num);
	if (err)
		return err;
	if (check_num == 0)
		return -ENODATA;
	return 0;
}

static int btf_dump_bitfield_data(struct btf_dump *d,
				  const struct btf_type *t,
				  const void *data,
				  __u8 bits_offset,
				  __u8 bit_sz)
{
	__u64 print_num;
	int err;

	err = btf_dump_get_bitfield_value(d, t, data, bits_offset, bit_sz, &print_num);
	if (err)
		return err;

	btf_dump_type_values(d, "0x%llx", (unsigned long long)print_num);

	return 0;
}

/* ints, floats and ptrs */
static int btf_dump_base_type_check_zero(struct btf_dump *d,
					 const struct btf_type *t,
					 __u32 id,
					 const void *data)
{
	static __u8 bytecmp[16] = {};
	int nr_bytes;

	/* For pointer types, pointer size is not defined on a per-type basis.
	 * On dump creation however, we store the pointer size.
	 */
	if (btf_kind(t) == BTF_KIND_PTR)
		nr_bytes = d->ptr_sz;
	else
		nr_bytes = t->size;

	if (nr_bytes < 1 || nr_bytes > 16) {
		pr_warn("unexpected size %d for id [%u]\n", nr_bytes, id);
		return -EINVAL;
	}

	if (memcmp(data, bytecmp, nr_bytes) == 0)
		return -ENODATA;
	return 0;
}

static bool ptr_is_aligned(const struct btf *btf, __u32 type_id,
			   const void *data)
{
	int alignment = btf__align_of(btf, type_id);

	if (alignment == 0)
		return false;

	return ((uintptr_t)data) % alignment == 0;
}

static int btf_dump_int_data(struct btf_dump *d,
			     const struct btf_type *t,
			     __u32 type_id,
			     const void *data,
			     __u8 bits_offset)
{
	__u8 encoding = btf_int_encoding(t);
	bool sign = encoding & BTF_INT_SIGNED;
	char buf[16] __attribute__((aligned(16)));
	int sz = t->size;

	if (sz == 0 || sz > sizeof(buf)) {
		pr_warn("unexpected size %d for id [%u]\n", sz, type_id);
		return -EINVAL;
	}

	/* handle packed int data - accesses of integers not aligned on
	 * int boundaries can cause problems on some platforms.
	 */
	if (!ptr_is_aligned(d->btf, type_id, data)) {
		memcpy(buf, data, sz);
		data = buf;
	}

	switch (sz) {
	case 16: {
		const __u64 *ints = data;
		__u64 lsi, msi;

		/* avoid use of __int128 as some 32-bit platforms do not
		 * support it.
		 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		lsi = ints[0];
		msi = ints[1];
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		lsi = ints[1];
		msi = ints[0];
#else
# error "Unrecognized __BYTE_ORDER__"
#endif
		if (msi == 0)
			btf_dump_type_values(d, "0x%llx", (unsigned long long)lsi);
		else
			btf_dump_type_values(d, "0x%llx%016llx", (unsigned long long)msi,
					     (unsigned long long)lsi);
		break;
	}
	case 8:
		if (sign)
			btf_dump_type_values(d, "%lld", *(long long *)data);
		else
			btf_dump_type_values(d, "%llu", *(unsigned long long *)data);
		break;
	case 4:
		if (sign)
			btf_dump_type_values(d, "%d", *(__s32 *)data);
		else
			btf_dump_type_values(d, "%u", *(__u32 *)data);
		break;
	case 2:
		if (sign)
			btf_dump_type_values(d, "%d", *(__s16 *)data);
		else
			btf_dump_type_values(d, "%u", *(__u16 *)data);
		break;
	case 1:
		if (d->typed_dump->is_array_char) {
			/* check for null terminator */
			if (d->typed_dump->is_array_terminated)
				break;
			if (*(char *)data == '\0') {
				btf_dump_type_values(d, "'\\0'");
				d->typed_dump->is_array_terminated = true;
				break;
			}
			if (isprint(*(char *)data)) {
				btf_dump_type_values(d, "'%c'", *(char *)data);
				break;
			}
		}
		if (sign)
			btf_dump_type_values(d, "%d", *(__s8 *)data);
		else
			btf_dump_type_values(d, "%u", *(__u8 *)data);
		break;
	default:
		pr_warn("unexpected sz %d for id [%u]\n", sz, type_id);
		return -EINVAL;
	}
	return 0;
}

union float_data {
	long double ld;
	double d;
	float f;
};

static int btf_dump_float_data(struct btf_dump *d,
			       const struct btf_type *t,
			       __u32 type_id,
			       const void *data)
{
	const union float_data *flp = data;
	union float_data fl;
	int sz = t->size;

	/* handle unaligned data; copy to local union */
	if (!ptr_is_aligned(d->btf, type_id, data)) {
		memcpy(&fl, data, sz);
		flp = &fl;
	}

	switch (sz) {
	case 16:
		btf_dump_type_values(d, "%Lf", flp->ld);
		break;
	case 8:
		btf_dump_type_values(d, "%lf", flp->d);
		break;
	case 4:
		btf_dump_type_values(d, "%f", flp->f);
		break;
	default:
		pr_warn("unexpected size %d for id [%u]\n", sz, type_id);
		return -EINVAL;
	}
	return 0;
}

static int btf_dump_var_data(struct btf_dump *d,
			     const struct btf_type *v,
			     __u32 id,
			     const void *data)
{
	enum btf_func_linkage linkage = btf_var(v)->linkage;
	const struct btf_type *t;
	const char *l;
	__u32 type_id;

	switch (linkage) {
	case BTF_FUNC_STATIC:
		l = "static ";
		break;
	case BTF_FUNC_EXTERN:
		l = "extern ";
		break;
	case BTF_FUNC_GLOBAL:
	default:
		l = "";
		break;
	}

	/* format of output here is [linkage] [type] [varname] = (type)value,
	 * for example "static int cpu_profile_flip = (int)1"
	 */
	btf_dump_printf(d, "%s", l);
	type_id = v->type;
	t = btf__type_by_id(d->btf, type_id);
	btf_dump_emit_type_cast(d, type_id, false);
	btf_dump_printf(d, " %s = ", btf_name_of(d, v->name_off));
	return btf_dump_dump_type_data(d, NULL, t, type_id, data, 0, 0);
}

static int btf_dump_array_data(struct btf_dump *d,
			       const struct btf_type *t,
			       __u32 id,
			       const void *data)
{
	const struct btf_array *array = btf_array(t);
	const struct btf_type *elem_type;
	__u32 i, elem_type_id;
	__s64 elem_size;
	bool is_array_member;
	bool is_array_terminated;

	elem_type_id = array->type;
	elem_type = skip_mods_and_typedefs(d->btf, elem_type_id, NULL);
	elem_size = btf__resolve_size(d->btf, elem_type_id);
	if (elem_size <= 0) {
		pr_warn("unexpected elem size %zd for array type [%u]\n",
			(ssize_t)elem_size, id);
		return -EINVAL;
	}

	if (btf_is_int(elem_type)) {
		/*
		 * BTF_INT_CHAR encoding never seems to be set for
		 * char arrays, so if size is 1 and element is
		 * printable as a char, we'll do that.
		 */
		if (elem_size == 1)
			d->typed_dump->is_array_char = true;
	}

	/* note that we increment depth before calling btf_dump_print() below;
	 * this is intentional.  btf_dump_data_newline() will not print a
	 * newline for depth 0 (since this leaves us with trailing newlines
	 * at the end of typed display), so depth is incremented first.
	 * For similar reasons, we decrement depth before showing the closing
	 * parenthesis.
	 */
	d->typed_dump->depth++;
	btf_dump_printf(d, "[%s", btf_dump_data_newline(d));

	/* may be a multidimensional array, so store current "is array member"
	 * status so we can restore it correctly later.
	 */
	is_array_member = d->typed_dump->is_array_member;
	d->typed_dump->is_array_member = true;
	is_array_terminated = d->typed_dump->is_array_terminated;
	d->typed_dump->is_array_terminated = false;
	for (i = 0; i < array->nelems; i++, data += elem_size) {
		if (d->typed_dump->is_array_terminated)
			break;
		btf_dump_dump_type_data(d, NULL, elem_type, elem_type_id, data, 0, 0);
	}
	d->typed_dump->is_array_member = is_array_member;
	d->typed_dump->is_array_terminated = is_array_terminated;
	d->typed_dump->depth--;
	btf_dump_data_pfx(d);
	btf_dump_type_values(d, "]");

	return 0;
}

static int btf_dump_struct_data(struct btf_dump *d,
				const struct btf_type *t,
				__u32 id,
				const void *data)
{
	const struct btf_member *m = btf_members(t);
	__u16 n = btf_vlen(t);
	int i, err = 0;

	/* note that we increment depth before calling btf_dump_print() below;
	 * this is intentional.  btf_dump_data_newline() will not print a
	 * newline for depth 0 (since this leaves us with trailing newlines
	 * at the end of typed display), so depth is incremented first.
	 * For similar reasons, we decrement depth before showing the closing
	 * parenthesis.
	 */
	d->typed_dump->depth++;
	btf_dump_printf(d, "{%s", btf_dump_data_newline(d));

	for (i = 0; i < n; i++, m++) {
		const struct btf_type *mtype;
		const char *mname;
		__u32 moffset;
		__u8 bit_sz;

		mtype = btf__type_by_id(d->btf, m->type);
		mname = btf_name_of(d, m->name_off);
		moffset = btf_member_bit_offset(t, i);

		bit_sz = btf_member_bitfield_size(t, i);
		err = btf_dump_dump_type_data(d, mname, mtype, m->type, data + moffset / 8,
					      moffset % 8, bit_sz);
		if (err < 0)
			return err;
	}
	d->typed_dump->depth--;
	btf_dump_data_pfx(d);
	btf_dump_type_values(d, "}");
	return err;
}

union ptr_data {
	unsigned int p;
	unsigned long long lp;
};

static int btf_dump_ptr_data(struct btf_dump *d,
			      const struct btf_type *t,
			      __u32 id,
			      const void *data)
{
	if (ptr_is_aligned(d->btf, id, data) && d->ptr_sz == sizeof(void *)) {
		btf_dump_type_values(d, "%p", *(void **)data);
	} else {
		union ptr_data pt;

		memcpy(&pt, data, d->ptr_sz);
		if (d->ptr_sz == 4)
			btf_dump_type_values(d, "0x%x", pt.p);
		else
			btf_dump_type_values(d, "0x%llx", pt.lp);
	}
	return 0;
}

static int btf_dump_get_enum_value(struct btf_dump *d,
				   const struct btf_type *t,
				   const void *data,
				   __u32 id,
				   __s64 *value)
{
	bool is_signed = btf_kflag(t);

	if (!ptr_is_aligned(d->btf, id, data)) {
		__u64 val;
		int err;

		err = btf_dump_get_bitfield_value(d, t, data, 0, 0, &val);
		if (err)
			return err;
		*value = (__s64)val;
		return 0;
	}

	switch (t->size) {
	case 8:
		*value = *(__s64 *)data;
		return 0;
	case 4:
		*value = is_signed ? (__s64)*(__s32 *)data : *(__u32 *)data;
		return 0;
	case 2:
		*value = is_signed ? *(__s16 *)data : *(__u16 *)data;
		return 0;
	case 1:
		*value = is_signed ? *(__s8 *)data : *(__u8 *)data;
		return 0;
	default:
		pr_warn("unexpected size %d for enum, id:[%u]\n", t->size, id);
		return -EINVAL;
	}
}

static int btf_dump_enum_data(struct btf_dump *d,
			      const struct btf_type *t,
			      __u32 id,
			      const void *data)
{
	bool is_signed;
	__s64 value;
	int i, err;

	err = btf_dump_get_enum_value(d, t, data, id, &value);
	if (err)
		return err;

	is_signed = btf_kflag(t);
	if (btf_is_enum(t)) {
		const struct btf_enum *e;

		for (i = 0, e = btf_enum(t); i < btf_vlen(t); i++, e++) {
			if (value != e->val)
				continue;
			btf_dump_type_values(d, "%s", btf_name_of(d, e->name_off));
			return 0;
		}

		btf_dump_type_values(d, is_signed ? "%d" : "%u", value);
	} else {
		const struct btf_enum64 *e;

		for (i = 0, e = btf_enum64(t); i < btf_vlen(t); i++, e++) {
			if (value != btf_enum64_value(e))
				continue;
			btf_dump_type_values(d, "%s", btf_name_of(d, e->name_off));
			return 0;
		}

		btf_dump_type_values(d, is_signed ? "%lldLL" : "%lluULL",
				     (unsigned long long)value);
	}
	return 0;
}

static int btf_dump_datasec_data(struct btf_dump *d,
				 const struct btf_type *t,
				 __u32 id,
				 const void *data)
{
	const struct btf_var_secinfo *vsi;
	const struct btf_type *var;
	__u32 i;
	int err;

	btf_dump_type_values(d, "SEC(\"%s\") ", btf_name_of(d, t->name_off));

	for (i = 0, vsi = btf_var_secinfos(t); i < btf_vlen(t); i++, vsi++) {
		var = btf__type_by_id(d->btf, vsi->type);
		err = btf_dump_dump_type_data(d, NULL, var, vsi->type, data + vsi->offset, 0, 0);
		if (err < 0)
			return err;
		btf_dump_printf(d, ";");
	}
	return 0;
}

/* return size of type, or if base type overflows, return -E2BIG. */
static int btf_dump_type_data_check_overflow(struct btf_dump *d,
					     const struct btf_type *t,
					     __u32 id,
					     const void *data,
					     __u8 bits_offset,
					     __u8 bit_sz)
{
	__s64 size;

	if (bit_sz) {
		/* bits_offset is at most 7. bit_sz is at most 128. */
		__u8 nr_bytes = (bits_offset + bit_sz + 7) / 8;

		/* When bit_sz is non zero, it is called from
		 * btf_dump_struct_data() where it only cares about
		 * negative error value.
		 * Return nr_bytes in success case to make it
		 * consistent as the regular integer case below.
		 */
		return data + nr_bytes > d->typed_dump->data_end ? -E2BIG : nr_bytes;
	}

	size = btf__resolve_size(d->btf, id);

	if (size < 0 || size >= INT_MAX) {
		pr_warn("unexpected size [%zu] for id [%u]\n",
			(size_t)size, id);
		return -EINVAL;
	}

	/* Only do overflow checking for base types; we do not want to
	 * avoid showing part of a struct, union or array, even if we
	 * do not have enough data to show the full object.  By
	 * restricting overflow checking to base types we can ensure
	 * that partial display succeeds, while avoiding overflowing
	 * and using bogus data for display.
	 */
	t = skip_mods_and_typedefs(d->btf, id, NULL);
	if (!t) {
		pr_warn("unexpected error skipping mods/typedefs for id [%u]\n",
			id);
		return -EINVAL;
	}

	switch (btf_kind(t)) {
	case BTF_KIND_INT:
	case BTF_KIND_FLOAT:
	case BTF_KIND_PTR:
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		if (data + bits_offset / 8 + size > d->typed_dump->data_end)
			return -E2BIG;
		break;
	default:
		break;
	}
	return (int)size;
}

static int btf_dump_type_data_check_zero(struct btf_dump *d,
					 const struct btf_type *t,
					 __u32 id,
					 const void *data,
					 __u8 bits_offset,
					 __u8 bit_sz)
{
	__s64 value;
	int i, err;

	/* toplevel exceptions; we show zero values if
	 * - we ask for them (emit_zeros)
	 * - if we are at top-level so we see "struct empty { }"
	 * - or if we are an array member and the array is non-empty and
	 *   not a char array; we don't want to be in a situation where we
	 *   have an integer array 0, 1, 0, 1 and only show non-zero values.
	 *   If the array contains zeroes only, or is a char array starting
	 *   with a '\0', the array-level check_zero() will prevent showing it;
	 *   we are concerned with determining zero value at the array member
	 *   level here.
	 */
	if (d->typed_dump->emit_zeroes || d->typed_dump->depth == 0 ||
	    (d->typed_dump->is_array_member &&
	     !d->typed_dump->is_array_char))
		return 0;

	t = skip_mods_and_typedefs(d->btf, id, NULL);

	switch (btf_kind(t)) {
	case BTF_KIND_INT:
		if (bit_sz)
			return btf_dump_bitfield_check_zero(d, t, data, bits_offset, bit_sz);
		return btf_dump_base_type_check_zero(d, t, id, data);
	case BTF_KIND_FLOAT:
	case BTF_KIND_PTR:
		return btf_dump_base_type_check_zero(d, t, id, data);
	case BTF_KIND_ARRAY: {
		const struct btf_array *array = btf_array(t);
		const struct btf_type *elem_type;
		__u32 elem_type_id, elem_size;
		bool ischar;

		elem_type_id = array->type;
		elem_size = btf__resolve_size(d->btf, elem_type_id);
		elem_type = skip_mods_and_typedefs(d->btf, elem_type_id, NULL);

		ischar = btf_is_int(elem_type) && elem_size == 1;

		/* check all elements; if _any_ element is nonzero, all
		 * of array is displayed.  We make an exception however
		 * for char arrays where the first element is 0; these
		 * are considered zeroed also, even if later elements are
		 * non-zero because the string is terminated.
		 */
		for (i = 0; i < array->nelems; i++) {
			if (i == 0 && ischar && *(char *)data == 0)
				return -ENODATA;
			err = btf_dump_type_data_check_zero(d, elem_type,
							    elem_type_id,
							    data +
							    (i * elem_size),
							    bits_offset, 0);
			if (err != -ENODATA)
				return err;
		}
		return -ENODATA;
	}
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION: {
		const struct btf_member *m = btf_members(t);
		__u16 n = btf_vlen(t);

		/* if any struct/union member is non-zero, the struct/union
		 * is considered non-zero and dumped.
		 */
		for (i = 0; i < n; i++, m++) {
			const struct btf_type *mtype;
			__u32 moffset;

			mtype = btf__type_by_id(d->btf, m->type);
			moffset = btf_member_bit_offset(t, i);

			/* btf_int_bits() does not store member bitfield size;
			 * bitfield size needs to be stored here so int display
			 * of member can retrieve it.
			 */
			bit_sz = btf_member_bitfield_size(t, i);
			err = btf_dump_type_data_check_zero(d, mtype, m->type, data + moffset / 8,
							    moffset % 8, bit_sz);
			if (err != ENODATA)
				return err;
		}
		return -ENODATA;
	}
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		err = btf_dump_get_enum_value(d, t, data, id, &value);
		if (err)
			return err;
		if (value == 0)
			return -ENODATA;
		return 0;
	default:
		return 0;
	}
}

/* returns size of data dumped, or error. */
static int btf_dump_dump_type_data(struct btf_dump *d,
				   const char *fname,
				   const struct btf_type *t,
				   __u32 id,
				   const void *data,
				   __u8 bits_offset,
				   __u8 bit_sz)
{
	int size, err = 0;

	size = btf_dump_type_data_check_overflow(d, t, id, data, bits_offset, bit_sz);
	if (size < 0)
		return size;
	err = btf_dump_type_data_check_zero(d, t, id, data, bits_offset, bit_sz);
	if (err) {
		/* zeroed data is expected and not an error, so simply skip
		 * dumping such data.  Record other errors however.
		 */
		if (err == -ENODATA)
			return size;
		return err;
	}
	btf_dump_data_pfx(d);

	if (!d->typed_dump->skip_names) {
		if (fname && strlen(fname) > 0)
			btf_dump_printf(d, ".%s = ", fname);
		btf_dump_emit_type_cast(d, id, true);
	}

	t = skip_mods_and_typedefs(d->btf, id, NULL);

	switch (btf_kind(t)) {
	case BTF_KIND_UNKN:
	case BTF_KIND_FWD:
	case BTF_KIND_FUNC:
	case BTF_KIND_FUNC_PROTO:
	case BTF_KIND_DECL_TAG:
		err = btf_dump_unsupported_data(d, t, id);
		break;
	case BTF_KIND_INT:
		if (bit_sz)
			err = btf_dump_bitfield_data(d, t, data, bits_offset, bit_sz);
		else
			err = btf_dump_int_data(d, t, id, data, bits_offset);
		break;
	case BTF_KIND_FLOAT:
		err = btf_dump_float_data(d, t, id, data);
		break;
	case BTF_KIND_PTR:
		err = btf_dump_ptr_data(d, t, id, data);
		break;
	case BTF_KIND_ARRAY:
		err = btf_dump_array_data(d, t, id, data);
		break;
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
		err = btf_dump_struct_data(d, t, id, data);
		break;
	case BTF_KIND_ENUM:
	case BTF_KIND_ENUM64:
		/* handle bitfield and int enum values */
		if (bit_sz) {
			__u64 print_num;
			__s64 enum_val;

			err = btf_dump_get_bitfield_value(d, t, data, bits_offset, bit_sz,
							  &print_num);
			if (err)
				break;
			enum_val = (__s64)print_num;
			err = btf_dump_enum_data(d, t, id, &enum_val);
		} else
			err = btf_dump_enum_data(d, t, id, data);
		break;
	case BTF_KIND_VAR:
		err = btf_dump_var_data(d, t, id, data);
		break;
	case BTF_KIND_DATASEC:
		err = btf_dump_datasec_data(d, t, id, data);
		break;
	default:
		pr_warn("unexpected kind [%u] for id [%u]\n",
			BTF_INFO_KIND(t->info), id);
		return -EINVAL;
	}
	if (err < 0)
		return err;
	return size;
}

int btf_dump__dump_type_data(struct btf_dump *d, __u32 id,
			     const void *data, size_t data_sz,
			     const struct btf_dump_type_data_opts *opts)
{
	struct btf_dump_data typed_dump = {};
	const struct btf_type *t;
	int ret;

	if (!OPTS_VALID(opts, btf_dump_type_data_opts))
		return libbpf_err(-EINVAL);

	t = btf__type_by_id(d->btf, id);
	if (!t)
		return libbpf_err(-ENOENT);

	d->typed_dump = &typed_dump;
	d->typed_dump->data_end = data + data_sz;
	d->typed_dump->indent_lvl = OPTS_GET(opts, indent_level, 0);

	/* default indent string is a tab */
	if (!OPTS_GET(opts, indent_str, NULL))
		d->typed_dump->indent_str[0] = '\t';
	else
		libbpf_strlcpy(d->typed_dump->indent_str, opts->indent_str,
			       sizeof(d->typed_dump->indent_str));

	d->typed_dump->compact = OPTS_GET(opts, compact, false);
	d->typed_dump->skip_names = OPTS_GET(opts, skip_names, false);
	d->typed_dump->emit_zeroes = OPTS_GET(opts, emit_zeroes, false);

	ret = btf_dump_dump_type_data(d, NULL, t, id, data, 0, 0);

	d->typed_dump = NULL;

	return libbpf_err(ret);
}
