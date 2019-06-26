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
#if BUILDING_GCC_VERSION >= 5000
#include "expr.h"
#endif
#include "basic-block.h"
#include "intl.h"
#include "ggc.h"
#include "timevar.h"

#include "params.h"

#if BUILDING_GCC_VERSION <= 4009
#include "pointer-set.h"
#else
#include "hash-map.h"
#endif

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

#if BUILDING_GCC_VERSION == 4005
#include <sys/mman.h>
#endif

#if BUILDING_GCC_VERSION >= 4007
#include "tree-pretty-print.h"
#include "gimple-pretty-print.h"
#endif

#if BUILDING_GCC_VERSION >= 4006
/*
 * The c-family headers were moved into a subdirectory in GCC version
 * 4.7, but most plugin-building users of GCC 4.6 are using the Debian
 * or Ubuntu package, which has an out-of-tree patch to move this to the
 * same location as found in 4.7 and later:
 * https://sources.debian.net/src/gcc-4.6/4.6.3-14/debian/patches/pr45078.diff/
 */
#include "c-family/c-common.h"
#else
#include "c-common.h"
#endif

#if BUILDING_GCC_VERSION <= 4008
#include "tree-flow.h"
#else
#include "tree-cfgcleanup.h"
#include "tree-ssa-operands.h"
#include "tree-into-ssa.h"
#endif

#if BUILDING_GCC_VERSION >= 4008
#include "is-a.h"
#endif

#include "diagnostic.h"
#include "tree-dump.h"
#include "tree-pass.h"
#if BUILDING_GCC_VERSION >= 4009
#include "pass_manager.h"
#endif
#include "predict.h"
#include "ipa-utils.h"

#if BUILDING_GCC_VERSION >= 8000
#include "stringpool.h"
#endif

#if BUILDING_GCC_VERSION >= 4009
#include "attribs.h"
#include "varasm.h"
#include "stor-layout.h"
#include "internal-fn.h"
#include "gimple-expr.h"
#include "gimple-fold.h"
#include "context.h"
#include "tree-ssa-alias.h"
#include "tree-ssa.h"
#include "stringpool.h"
#if BUILDING_GCC_VERSION >= 7000
#include "tree-vrp.h"
#endif
#include "tree-ssanames.h"
#include "print-tree.h"
#include "tree-eh.h"
#include "stmt.h"
#include "gimplify.h"
#endif

#include "gimple.h"

#if BUILDING_GCC_VERSION >= 4009
#include "tree-ssa-operands.h"
#include "tree-phinodes.h"
#include "tree-cfg.h"
#include "gimple-iterator.h"
#include "gimple-ssa.h"
#include "ssa-iterators.h"
#endif

#if BUILDING_GCC_VERSION >= 5000
#include "builtins.h"
#endif

/* missing from basic_block.h... */
void debug_dominance_info(enum cdi_direction dir);
void debug_dominance_tree(enum cdi_direction dir, basic_block root);

#if BUILDING_GCC_VERSION == 4006
void debug_gimple_stmt(gimple);
void debug_gimple_seq(gimple_seq);
void print_gimple_seq(FILE *, gimple_seq, int, int);
void print_gimple_stmt(FILE *, gimple, int, int);
void print_gimple_expr(FILE *, gimple, int, int);
void dump_gimple_stmt(pretty_printer *, gimple, int, int);
#endif

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

#if BUILDING_GCC_VERSION == 4005
#define FOR_EACH_LOCAL_DECL(FUN, I, D)			\
	for (tree vars = (FUN)->local_decls, (I) = 0;	\
		vars && ((D) = TREE_VALUE(vars));	\
		vars = TREE_CHAIN(vars), (I)++)
#define DECL_CHAIN(NODE) (TREE_CHAIN(DECL_MINIMAL_CHECK(NODE)))
#define FOR_EACH_VEC_ELT(T, V, I, P) \
	for (I = 0; VEC_iterate(T, (V), (I), (P)); ++(I))
#define TODO_rebuild_cgraph_edges 0
#define SCOPE_FILE_SCOPE_P(EXP) (!(EXP))

#ifndef O_BINARY
#define O_BINARY 0
#endif

typedef struct varpool_node *varpool_node_ptr;

static inline bool gimple_call_builtin_p(gimple stmt, enum built_in_function code)
{
	tree fndecl;

	if (!is_gimple_call(stmt))
		return false;
	fndecl = gimple_call_fndecl(stmt);
	if (!fndecl || DECL_BUILT_IN_CLASS(fndecl) != BUILT_IN_NORMAL)
		return false;
	return DECL_FUNCTION_CODE(fndecl) == code;
}

static inline bool is_simple_builtin(tree decl)
{
	if (decl && DECL_BUILT_IN_CLASS(decl) != BUILT_IN_NORMAL)
		return false;

	switch (DECL_FUNCTION_CODE(decl)) {
	/* Builtins that expand to constants. */
	case BUILT_IN_CONSTANT_P:
	case BUILT_IN_EXPECT:
	case BUILT_IN_OBJECT_SIZE:
	case BUILT_IN_UNREACHABLE:
	/* Simple register moves or loads from stack. */
	case BUILT_IN_RETURN_ADDRESS:
	case BUILT_IN_EXTRACT_RETURN_ADDR:
	case BUILT_IN_FROB_RETURN_ADDR:
	case BUILT_IN_RETURN:
	case BUILT_IN_AGGREGATE_INCOMING_ADDRESS:
	case BUILT_IN_FRAME_ADDRESS:
	case BUILT_IN_VA_END:
	case BUILT_IN_STACK_SAVE:
	case BUILT_IN_STACK_RESTORE:
	/* Exception state returns or moves registers around. */
	case BUILT_IN_EH_FILTER:
	case BUILT_IN_EH_POINTER:
	case BUILT_IN_EH_COPY_VALUES:
	return true;

	default:
	return false;
	}
}

static inline void add_local_decl(struct function *fun, tree d)
{
	gcc_assert(TREE_CODE(d) == VAR_DECL);
	fun->local_decls = tree_cons(NULL_TREE, d, fun->local_decls);
}
#endif

#if BUILDING_GCC_VERSION <= 4006
#define ANY_RETURN_P(rtx) (GET_CODE(rtx) == RETURN)
#define C_DECL_REGISTER(EXP) DECL_LANG_FLAG_4(EXP)
#define EDGE_PRESERVE 0ULL
#define HOST_WIDE_INT_PRINT_HEX_PURE "%" HOST_WIDE_INT_PRINT "x"
#define flag_fat_lto_objects true

#define get_random_seed(noinit) ({						\
	unsigned HOST_WIDE_INT seed;						\
	sscanf(get_random_seed(noinit), "%" HOST_WIDE_INT_PRINT "x", &seed);	\
	seed * seed; })

#define int_const_binop(code, arg1, arg2)	\
	int_const_binop((code), (arg1), (arg2), 0)

static inline bool gimple_clobber_p(gimple s __unused)
{
	return false;
}

static inline bool gimple_asm_clobbers_memory_p(const_gimple stmt)
{
	unsigned i;

	for (i = 0; i < gimple_asm_nclobbers(stmt); i++) {
		tree op = gimple_asm_clobber_op(stmt, i);

		if (!strcmp(TREE_STRING_POINTER(TREE_VALUE(op)), "memory"))
			return true;
	}

	return false;
}

static inline tree builtin_decl_implicit(enum built_in_function fncode)
{
	return implicit_built_in_decls[fncode];
}

static inline int ipa_reverse_postorder(struct cgraph_node **order)
{
	return cgraph_postorder(order);
}

static inline struct cgraph_node *cgraph_create_node(tree decl)
{
	return cgraph_node(decl);
}

static inline struct cgraph_node *cgraph_get_create_node(tree decl)
{
	struct cgraph_node *node = cgraph_get_node(decl);

	return node ? node : cgraph_node(decl);
}

static inline bool cgraph_function_with_gimple_body_p(struct cgraph_node *node)
{
	return node->analyzed && !node->thunk.thunk_p && !node->alias;
}

static inline struct cgraph_node *cgraph_first_function_with_gimple_body(void)
{
	struct cgraph_node *node;

	for (node = cgraph_nodes; node; node = node->next)
		if (cgraph_function_with_gimple_body_p(node))
			return node;
	return NULL;
}

static inline struct cgraph_node *cgraph_next_function_with_gimple_body(struct cgraph_node *node)
{
	for (node = node->next; node; node = node->next)
		if (cgraph_function_with_gimple_body_p(node))
			return node;
	return NULL;
}

static inline bool cgraph_for_node_and_aliases(cgraph_node_ptr node, bool (*callback)(cgraph_node_ptr, void *), void *data, bool include_overwritable)
{
	cgraph_node_ptr alias;

	if (callback(node, data))
		return true;

	for (alias = node->same_body; alias; alias = alias->next) {
		if (include_overwritable || cgraph_function_body_availability(alias) > AVAIL_OVERWRITABLE)
			if (cgraph_for_node_and_aliases(alias, callback, data, include_overwritable))
				return true;
	}

	return false;
}

#define FOR_EACH_FUNCTION_WITH_GIMPLE_BODY(node) \
	for ((node) = cgraph_first_function_with_gimple_body(); (node); \
		(node) = cgraph_next_function_with_gimple_body(node))

static inline void varpool_add_new_variable(tree decl)
{
	varpool_finalize_decl(decl);
}
#endif

#if BUILDING_GCC_VERSION <= 4007
#define FOR_EACH_FUNCTION(node)	\
	for (node = cgraph_nodes; node; node = node->next)
#define FOR_EACH_VARIABLE(node)	\
	for (node = varpool_nodes; node; node = node->next)
#define PROP_loops 0
#define NODE_SYMBOL(node) (node)
#define NODE_DECL(node) (node)->decl
#define INSN_LOCATION(INSN) RTL_LOCATION(INSN)
#define vNULL NULL

static inline int bb_loop_depth(const_basic_block bb)
{
	return bb->loop_father ? loop_depth(bb->loop_father) : 0;
}

static inline bool gimple_store_p(gimple gs)
{
	tree lhs = gimple_get_lhs(gs);

	return lhs && !is_gimple_reg(lhs);
}

static inline void gimple_init_singleton(gimple g __unused)
{
}
#endif

#if BUILDING_GCC_VERSION == 4007 || BUILDING_GCC_VERSION == 4008
static inline struct cgraph_node *cgraph_alias_target(struct cgraph_node *n)
{
	return cgraph_alias_aliased_node(n);
}
#endif

#if BUILDING_GCC_VERSION <= 4008
#define ENTRY_BLOCK_PTR_FOR_FN(FN)	ENTRY_BLOCK_PTR_FOR_FUNCTION(FN)
#define EXIT_BLOCK_PTR_FOR_FN(FN)	EXIT_BLOCK_PTR_FOR_FUNCTION(FN)
#define basic_block_info_for_fn(FN)	((FN)->cfg->x_basic_block_info)
#define n_basic_blocks_for_fn(FN)	((FN)->cfg->x_n_basic_blocks)
#define n_edges_for_fn(FN)		((FN)->cfg->x_n_edges)
#define last_basic_block_for_fn(FN)	((FN)->cfg->x_last_basic_block)
#define label_to_block_map_for_fn(FN)	((FN)->cfg->x_label_to_block_map)
#define profile_status_for_fn(FN)	((FN)->cfg->x_profile_status)
#define BASIC_BLOCK_FOR_FN(FN, N)	BASIC_BLOCK_FOR_FUNCTION((FN), (N))
#define NODE_IMPLICIT_ALIAS(node)	(node)->same_body_alias
#define VAR_P(NODE)			(TREE_CODE(NODE) == VAR_DECL)

static inline bool tree_fits_shwi_p(const_tree t)
{
	if (t == NULL_TREE || TREE_CODE(t) != INTEGER_CST)
		return false;

	if (TREE_INT_CST_HIGH(t) == 0 && (HOST_WIDE_INT)TREE_INT_CST_LOW(t) >= 0)
		return true;

	if (TREE_INT_CST_HIGH(t) == -1 && (HOST_WIDE_INT)TREE_INT_CST_LOW(t) < 0 && !TYPE_UNSIGNED(TREE_TYPE(t)))
		return true;

	return false;
}

static inline bool tree_fits_uhwi_p(const_tree t)
{
	if (t == NULL_TREE || TREE_CODE(t) != INTEGER_CST)
		return false;

	return TREE_INT_CST_HIGH(t) == 0;
}

static inline HOST_WIDE_INT tree_to_shwi(const_tree t)
{
	gcc_assert(tree_fits_shwi_p(t));
	return TREE_INT_CST_LOW(t);
}

static inline unsigned HOST_WIDE_INT tree_to_uhwi(const_tree t)
{
	gcc_assert(tree_fits_uhwi_p(t));
	return TREE_INT_CST_LOW(t);
}

static inline const char *get_tree_code_name(enum tree_code code)
{
	gcc_assert(code < MAX_TREE_CODES);
	return tree_code_name[code];
}

#define ipa_remove_stmt_references(cnode, stmt)

typedef union gimple_statement_d gasm;
typedef union gimple_statement_d gassign;
typedef union gimple_statement_d gcall;
typedef union gimple_statement_d gcond;
typedef union gimple_statement_d gdebug;
typedef union gimple_statement_d ggoto;
typedef union gimple_statement_d gphi;
typedef union gimple_statement_d greturn;

static inline gasm *as_a_gasm(gimple stmt)
{
	return stmt;
}

static inline const gasm *as_a_const_gasm(const_gimple stmt)
{
	return stmt;
}

static inline gassign *as_a_gassign(gimple stmt)
{
	return stmt;
}

static inline const gassign *as_a_const_gassign(const_gimple stmt)
{
	return stmt;
}

static inline gcall *as_a_gcall(gimple stmt)
{
	return stmt;
}

static inline const gcall *as_a_const_gcall(const_gimple stmt)
{
	return stmt;
}

static inline gcond *as_a_gcond(gimple stmt)
{
	return stmt;
}

static inline const gcond *as_a_const_gcond(const_gimple stmt)
{
	return stmt;
}

static inline gdebug *as_a_gdebug(gimple stmt)
{
	return stmt;
}

static inline const gdebug *as_a_const_gdebug(const_gimple stmt)
{
	return stmt;
}

static inline ggoto *as_a_ggoto(gimple stmt)
{
	return stmt;
}

static inline const ggoto *as_a_const_ggoto(const_gimple stmt)
{
	return stmt;
}

static inline gphi *as_a_gphi(gimple stmt)
{
	return stmt;
}

static inline const gphi *as_a_const_gphi(const_gimple stmt)
{
	return stmt;
}

static inline greturn *as_a_greturn(gimple stmt)
{
	return stmt;
}

static inline const greturn *as_a_const_greturn(const_gimple stmt)
{
	return stmt;
}
#endif

#if BUILDING_GCC_VERSION == 4008
#define NODE_SYMBOL(node) (&(node)->symbol)
#define NODE_DECL(node) (node)->symbol.decl
#endif

#if BUILDING_GCC_VERSION >= 4008
#define add_referenced_var(var)
#define mark_sym_for_renaming(var)
#define varpool_mark_needed_node(node)
#define create_var_ann(var)
#define TODO_dump_func 0
#define TODO_dump_cgraph 0
#endif

#if BUILDING_GCC_VERSION <= 4009
#define TODO_verify_il 0
#define AVAIL_INTERPOSABLE AVAIL_OVERWRITABLE

#define section_name_prefix LTO_SECTION_NAME_PREFIX
#define fatal_error(loc, gmsgid, ...) fatal_error((gmsgid), __VA_ARGS__)

rtx emit_move_insn(rtx x, rtx y);

typedef struct rtx_def rtx_insn;

static inline const char *get_decl_section_name(const_tree decl)
{
	if (DECL_SECTION_NAME(decl) == NULL_TREE)
		return NULL;

	return TREE_STRING_POINTER(DECL_SECTION_NAME(decl));
}

static inline void set_decl_section_name(tree node, const char *value)
{
	if (value)
		DECL_SECTION_NAME(node) = build_string(strlen(value) + 1, value);
	else
		DECL_SECTION_NAME(node) = NULL;
}
#endif

#if BUILDING_GCC_VERSION == 4009
typedef struct gimple_statement_asm gasm;
typedef struct gimple_statement_base gassign;
typedef struct gimple_statement_call gcall;
typedef struct gimple_statement_base gcond;
typedef struct gimple_statement_base gdebug;
typedef struct gimple_statement_base ggoto;
typedef struct gimple_statement_phi gphi;
typedef struct gimple_statement_base greturn;

static inline gasm *as_a_gasm(gimple stmt)
{
	return as_a<gasm>(stmt);
}

static inline const gasm *as_a_const_gasm(const_gimple stmt)
{
	return as_a<const gasm>(stmt);
}

static inline gassign *as_a_gassign(gimple stmt)
{
	return stmt;
}

static inline const gassign *as_a_const_gassign(const_gimple stmt)
{
	return stmt;
}

static inline gcall *as_a_gcall(gimple stmt)
{
	return as_a<gcall>(stmt);
}

static inline const gcall *as_a_const_gcall(const_gimple stmt)
{
	return as_a<const gcall>(stmt);
}

static inline gcond *as_a_gcond(gimple stmt)
{
	return stmt;
}

static inline const gcond *as_a_const_gcond(const_gimple stmt)
{
	return stmt;
}

static inline gdebug *as_a_gdebug(gimple stmt)
{
	return stmt;
}

static inline const gdebug *as_a_const_gdebug(const_gimple stmt)
{
	return stmt;
}

static inline ggoto *as_a_ggoto(gimple stmt)
{
	return stmt;
}

static inline const ggoto *as_a_const_ggoto(const_gimple stmt)
{
	return stmt;
}

static inline gphi *as_a_gphi(gimple stmt)
{
	return as_a<gphi>(stmt);
}

static inline const gphi *as_a_const_gphi(const_gimple stmt)
{
	return as_a<const gphi>(stmt);
}

static inline greturn *as_a_greturn(gimple stmt)
{
	return stmt;
}

static inline const greturn *as_a_const_greturn(const_gimple stmt)
{
	return stmt;
}
#endif

#if BUILDING_GCC_VERSION >= 4009
#define TODO_ggc_collect 0
#define NODE_SYMBOL(node) (node)
#define NODE_DECL(node) (node)->decl
#define cgraph_node_name(node) (node)->name()
#define NODE_IMPLICIT_ALIAS(node) (node)->cpp_implicit_alias

static inline opt_pass *get_pass_for_id(int id)
{
	return g->get_passes()->get_pass_for_id(id);
}
#endif

#if BUILDING_GCC_VERSION >= 5000 && BUILDING_GCC_VERSION < 6000
/* gimple related */
template <>
template <>
inline bool is_a_helper<const gassign *>::test(const_gimple gs)
{
	return gs->code == GIMPLE_ASSIGN;
}
#endif

#if BUILDING_GCC_VERSION >= 5000
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
#endif

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

#endif
