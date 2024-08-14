/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GCC_COMMON_H_INCLUDED
#define GCC_COMMON_H_INCLUDED

#include "bversion.h"
#if BUILDING_GCC_VERSION >= 6000
#include "gcc-plugin.h"
#else
#include "plugin.h"
#endif
#include "plugin-version.h"
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "line-map.h"
#include "input.h"
#include "tree.h"

#include "tree-inline.h"
#include "version.h"
#include "rtl.h"
#include "tm_p.h"
#include "flags.h"
#include "hard-reg-set.h"
#include "output.h"
#include "except.h"
#include "function.h"
#include "toplev.h"
#include "expr.h"
#include "basic-block.h"
#include "intl.h"
#include "ggc.h"
#include "timevar.h"

#if BUILDING_GCC_VERSION < 10000
#include "params.h"
#endif

#include "hash-map.h"

#if BUILDING_GCC_VERSION >= 7000
#include "memmodel.h"
#endif
#include "emit-rtl.h"
#include "debug.h"
#include "target.h"
#include "langhooks.h"
#include "cfgloop.h"
#include "cgraph.h"
#include "opts.h"
#include "tree-pretty-print.h"
#include "gimple-pretty-print.h"
#include "c-family/c-common.h"
#include "tree-cfgcleanup.h"
#include "tree-ssa-operands.h"
#include "tree-into-ssa.h"
#include "is-a.h"
#include "diagnostic.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "pass_manager.h"
#include "predict.h"
#include "ipa-utils.h"
#include "stringpool.h"
#include "attribs.h"
#include "varasm.h"
#include "stor-layout.h"
#include "internal-fn.h"
#include "gimple.h"
#include "gimple-expr.h"
#include "gimple-iterator.h"
#include "gimple-fold.h"
#include "context.h"
#include "tree-ssa-alias.h"
#include "tree-ssa.h"
#if BUILDING_GCC_VERSION >= 7000
#include "tree-vrp.h"
#endif
#include "tree-ssanames.h"
#include "print-tree.h"
#include "tree-eh.h"
#include "stmt.h"
#include "gimplify.h"
#include "tree-phinodes.h"
#include "tree-cfg.h"
#include "gimple-ssa.h"
#include "ssa-iterators.h"

#include "builtins.h"

/* missing from basic_block.h... */
void debug_dominance_info(enum cdi_direction dir);
void debug_dominance_tree(enum cdi_direction dir, basic_block root);

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#ifndef __visible
#define __visible __attribute__((visibility("default")))
#endif

#define DECL_NAME_POINTER(node) IDENTIFIER_POINTER(DECL_NAME(node))
#define DECL_NAME_LENGTH(node) IDENTIFIER_LENGTH(DECL_NAME(node))
#define TYPE_NAME_POINTER(node) IDENTIFIER_POINTER(TYPE_NAME(node))
#define TYPE_NAME_LENGTH(node) IDENTIFIER_LENGTH(TYPE_NAME(node))

/* should come from c-tree.h if only it were installed for gcc 4.5... */
#define C_TYPE_FIELDS_READONLY(TYPE) TREE_LANG_FLAG_1(TYPE)

static inline tree build_const_char_string(int len, const char *str)
{
	tree cstr, elem, index, type;

	cstr = build_string(len, str);
	elem = build_type_variant(char_type_node, 1, 0);
	index = build_index_type(size_int(len - 1));
	type = build_array_type(elem, index);
	TREE_TYPE(cstr) = type;
	TREE_CONSTANT(cstr) = 1;
	TREE_READONLY(cstr) = 1;
	TREE_STATIC(cstr) = 1;
	return cstr;
}

#define PASS_INFO(NAME, REF, ID, POS)		\
struct register_pass_info NAME##_pass_info = {	\
	.pass = make_##NAME##_pass(),		\
	.reference_pass_name = REF,		\
	.ref_pass_instance_number = ID,		\
	.pos_op = POS,				\
}

#define add_referenced_var(var)
#define mark_sym_for_renaming(var)
#define varpool_mark_needed_node(node)
#define create_var_ann(var)
#define TODO_dump_func 0
#define TODO_dump_cgraph 0

#define TODO_ggc_collect 0
#define NODE_SYMBOL(node) (node)
#define NODE_DECL(node) (node)->decl
#define cgraph_node_name(node) (node)->name()
#define NODE_IMPLICIT_ALIAS(node) (node)->cpp_implicit_alias

static inline opt_pass *get_pass_for_id(int id)
{
	return g->get_passes()->get_pass_for_id(id);
}

#if BUILDING_GCC_VERSION < 6000
/* gimple related */
template <>
template <>
inline bool is_a_helper<const gassign *>::test(const_gimple gs)
{
	return gs->code == GIMPLE_ASSIGN;
}
#endif

#define TODO_verify_ssa TODO_verify_il
#define TODO_verify_flow TODO_verify_il
#define TODO_verify_stmts TODO_verify_il
#define TODO_verify_rtl_sharing TODO_verify_il

#define INSN_DELETED_P(insn) (insn)->deleted()

static inline const char *get_decl_section_name(const_tree decl)
{
	return DECL_SECTION_NAME(decl);
}

/* symtab/cgraph related */
#define debug_cgraph_node(node) (node)->debug()
#define cgraph_get_node(decl) cgraph_node::get(decl)
#define cgraph_get_create_node(decl) cgraph_node::get_create(decl)
#define cgraph_create_node(decl) cgraph_node::create(decl)
#define cgraph_n_nodes symtab->cgraph_count
#define cgraph_max_uid symtab->cgraph_max_uid
#define varpool_get_node(decl) varpool_node::get(decl)
#define dump_varpool_node(file, node) (node)->dump(file)

#if BUILDING_GCC_VERSION >= 8000
#define cgraph_create_edge(caller, callee, call_stmt, count, freq) \
	(caller)->create_edge((callee), (call_stmt), (count))

#define cgraph_create_edge_including_clones(caller, callee,	\
		old_call_stmt, call_stmt, count, freq, reason)	\
	(caller)->create_edge_including_clones((callee),	\
		(old_call_stmt), (call_stmt), (count), (reason))
#else
#define cgraph_create_edge(caller, callee, call_stmt, count, freq) \
	(caller)->create_edge((callee), (call_stmt), (count), (freq))

#define cgraph_create_edge_including_clones(caller, callee,	\
		old_call_stmt, call_stmt, count, freq, reason)	\
	(caller)->create_edge_including_clones((callee),	\
		(old_call_stmt), (call_stmt), (count), (freq), (reason))
#endif

typedef struct cgraph_node *cgraph_node_ptr;
typedef struct cgraph_edge *cgraph_edge_p;
typedef struct varpool_node *varpool_node_ptr;

static inline void change_decl_assembler_name(tree decl, tree name)
{
	symtab->change_decl_assembler_name(decl, name);
}

static inline void varpool_finalize_decl(tree decl)
{
	varpool_node::finalize_decl(decl);
}

static inline void varpool_add_new_variable(tree decl)
{
	varpool_node::add(decl);
}

static inline unsigned int rebuild_cgraph_edges(void)
{
	return cgraph_edge::rebuild_edges();
}

static inline cgraph_node_ptr cgraph_function_node(cgraph_node_ptr node, enum availability *availability)
{
	return node->function_symbol(availability);
}

static inline cgraph_node_ptr cgraph_function_or_thunk_node(cgraph_node_ptr node, enum availability *availability = NULL)
{
	return node->ultimate_alias_target(availability);
}

static inline bool cgraph_only_called_directly_p(cgraph_node_ptr node)
{
	return node->only_called_directly_p();
}

static inline enum availability cgraph_function_body_availability(cgraph_node_ptr node)
{
	return node->get_availability();
}

static inline cgraph_node_ptr cgraph_alias_target(cgraph_node_ptr node)
{
	return node->get_alias_target();
}

static inline bool cgraph_for_node_and_aliases(cgraph_node_ptr node, bool (*callback)(cgraph_node_ptr, void *), void *data, bool include_overwritable)
{
	return node->call_for_symbol_thunks_and_aliases(callback, data, include_overwritable);
}

static inline struct cgraph_node_hook_list *cgraph_add_function_insertion_hook(cgraph_node_hook hook, void *data)
{
	return symtab->add_cgraph_insertion_hook(hook, data);
}

static inline void cgraph_remove_function_insertion_hook(struct cgraph_node_hook_list *entry)
{
	symtab->remove_cgraph_insertion_hook(entry);
}

static inline struct cgraph_node_hook_list *cgraph_add_node_removal_hook(cgraph_node_hook hook, void *data)
{
	return symtab->add_cgraph_removal_hook(hook, data);
}

static inline void cgraph_remove_node_removal_hook(struct cgraph_node_hook_list *entry)
{
	symtab->remove_cgraph_removal_hook(entry);
}

static inline struct cgraph_2node_hook_list *cgraph_add_node_duplication_hook(cgraph_2node_hook hook, void *data)
{
	return symtab->add_cgraph_duplication_hook(hook, data);
}

static inline void cgraph_remove_node_duplication_hook(struct cgraph_2node_hook_list *entry)
{
	symtab->remove_cgraph_duplication_hook(entry);
}

static inline void cgraph_call_node_duplication_hooks(cgraph_node_ptr node, cgraph_node_ptr node2)
{
	symtab->call_cgraph_duplication_hooks(node, node2);
}

static inline void cgraph_call_edge_duplication_hooks(cgraph_edge *cs1, cgraph_edge *cs2)
{
	symtab->call_edge_duplication_hooks(cs1, cs2);
}

#if BUILDING_GCC_VERSION >= 6000
typedef gimple *gimple_ptr;
typedef const gimple *const_gimple_ptr;
#define gimple gimple_ptr
#define const_gimple const_gimple_ptr
#undef CONST_CAST_GIMPLE
#define CONST_CAST_GIMPLE(X) CONST_CAST(gimple, (X))
#endif

/* gimple related */
static inline gimple gimple_build_assign_with_ops(enum tree_code subcode, tree lhs, tree op1, tree op2 MEM_STAT_DECL)
{
	return gimple_build_assign(lhs, subcode, op1, op2 PASS_MEM_STAT);
}

#if BUILDING_GCC_VERSION < 10000
template <>
template <>
inline bool is_a_helper<const ggoto *>::test(const_gimple gs)
{
	return gs->code == GIMPLE_GOTO;
}

template <>
template <>
inline bool is_a_helper<const greturn *>::test(const_gimple gs)
{
	return gs->code == GIMPLE_RETURN;
}
#endif

static inline gasm *as_a_gasm(gimple stmt)
{
	return as_a<gasm *>(stmt);
}

static inline const gasm *as_a_const_gasm(const_gimple stmt)
{
	return as_a<const gasm *>(stmt);
}

static inline gassign *as_a_gassign(gimple stmt)
{
	return as_a<gassign *>(stmt);
}

static inline const gassign *as_a_const_gassign(const_gimple stmt)
{
	return as_a<const gassign *>(stmt);
}

static inline gcall *as_a_gcall(gimple stmt)
{
	return as_a<gcall *>(stmt);
}

static inline const gcall *as_a_const_gcall(const_gimple stmt)
{
	return as_a<const gcall *>(stmt);
}

static inline ggoto *as_a_ggoto(gimple stmt)
{
	return as_a<ggoto *>(stmt);
}

static inline const ggoto *as_a_const_ggoto(const_gimple stmt)
{
	return as_a<const ggoto *>(stmt);
}

static inline gphi *as_a_gphi(gimple stmt)
{
	return as_a<gphi *>(stmt);
}

static inline const gphi *as_a_const_gphi(const_gimple stmt)
{
	return as_a<const gphi *>(stmt);
}

static inline greturn *as_a_greturn(gimple stmt)
{
	return as_a<greturn *>(stmt);
}

static inline const greturn *as_a_const_greturn(const_gimple stmt)
{
	return as_a<const greturn *>(stmt);
}

/* IPA/LTO related */
#define ipa_ref_list_referring_iterate(L, I, P)	\
	(L)->referring.iterate((I), &(P))
#define ipa_ref_list_reference_iterate(L, I, P)	\
	(L)->reference.iterate((I), &(P))

static inline cgraph_node_ptr ipa_ref_referring_node(struct ipa_ref *ref)
{
	return dyn_cast<cgraph_node_ptr>(ref->referring);
}

static inline void ipa_remove_stmt_references(symtab_node *referring_node, gimple stmt)
{
	referring_node->remove_stmt_references(stmt);
}

#if BUILDING_GCC_VERSION < 6000
#define get_inner_reference(exp, pbitsize, pbitpos, poffset, pmode, punsignedp, preversep, pvolatilep, keep_aligning)	\
	get_inner_reference(exp, pbitsize, pbitpos, poffset, pmode, punsignedp, pvolatilep, keep_aligning)
#define gen_rtx_set(ARG0, ARG1) gen_rtx_SET(VOIDmode, (ARG0), (ARG1))
#endif

#if BUILDING_GCC_VERSION >= 6000
#define gen_rtx_set(ARG0, ARG1) gen_rtx_SET((ARG0), (ARG1))
#endif

#ifdef __cplusplus
static inline void debug_tree(const_tree t)
{
	debug_tree(CONST_CAST_TREE(t));
}

static inline void debug_gimple_stmt(const_gimple s)
{
	debug_gimple_stmt(CONST_CAST_GIMPLE(s));
}
#else
#define debug_tree(t) debug_tree(CONST_CAST_TREE(t))
#define debug_gimple_stmt(s) debug_gimple_stmt(CONST_CAST_GIMPLE(s))
#endif

#if BUILDING_GCC_VERSION >= 7000
#define get_inner_reference(exp, pbitsize, pbitpos, poffset, pmode, punsignedp, preversep, pvolatilep, keep_aligning)	\
	get_inner_reference(exp, pbitsize, pbitpos, poffset, pmode, punsignedp, preversep, pvolatilep)
#endif

#if BUILDING_GCC_VERSION < 7000
#define SET_DECL_ALIGN(decl, align)	DECL_ALIGN(decl) = (align)
#define SET_DECL_MODE(decl, mode)	DECL_MODE(decl) = (mode)
#endif

#if BUILDING_GCC_VERSION >= 14000
#define last_stmt(x)			last_nondebug_stmt(x)
#endif

#endif
