/*
 * Copyright 2015-2017 by Emese Revfy <re.emese@gmail.com>
 * Licensed under the GPL v2
 *
 * Homepage:
 * https://github.com/ephox-gcc-plugins/initify
 *
 * This plugin has two passes. The first one tries to find all functions that
 * can be become __init/__exit. The second one moves string constants
 * (local variables and function string arguments marked by
 * the nocapture attribute) only referenced in __init/__exit functions
 * to __initconst/__exitconst sections.
 * Based on an idea from Mathias Krause <minipli@ld-linux.so>.
 *
 * The instrumentation pass of the latent_entropy plugin must run after
 * the initify plugin to increase coverage.
 *
 * Options:
 * -fplugin-arg-initify_plugin-disable
 * -fplugin-arg-initify_plugin-verbose
 * -fplugin-arg-initify_plugin-print_missing_attr
 * -fplugin-arg-initify_plugin-search_init_exit_functions
 * -fplugin-arg-initify_plugin-enable_init_to_exit_moves
 * -fplugin-arg-initify_plugin-disable_verify_nocapture_functions
 *
 * Attribute: __attribute__((nocapture(x, y ...)))
 *  The nocapture gcc attribute can be on functions only.
 *  The attribute takes one or more unsigned integer constants as parameters
 *  that specify the function argument(s) of const char* type to initify.
 *  If the marked argument is a vararg then the plugin initifies
 *  all vararg arguments.
 *  There can be one negative value which means that the return of the function
 *  will be followed to find it is a nocapture attribute or not.
 *
 * Attribute: __attribute__((unverified_nocapture(x, y ...)))
 *  This attribute disables the compile data flow verification of the designated
 *  nocapture parameters of the function. Use it only on function parameters
 *  that are difficult for the plugin to analyze.
 *
 * Usage:
 * $ make
 * $ make run
 */

#include "gcc-common.h"

__visible int plugin_is_GPL_compatible;

static struct plugin_info initify_plugin_info = {
	.version	=	"20170119vanilla",
	.help		=	"disable\tturn off the initify plugin\n"
				"verbose\tprint all initified strings and all"
				" functions which should be __init/__exit\n"
				"print_missing_attr\tprint functions which"
				" can be marked by nocapture attribute\n"
				"search_init_exit_functions\tfind functions"
				" which should be marked by __init or __exit"
				" attribute\n"
				"enable_init_to_exit_moves\tmove a function"
				" to the exit section if it is called by __init"
				" and __exit functions too\n"
				"disable_verify_nocapture_functions\tdisable"
				" the search of capture uses in nocapture"
				" functions\n"
};

#define ARGNUM_NONE 0
static bool verbose, print_missing_attr, search_init_exit_functions;
static bool enable_init_to_exit_moves, disable_verify_nocapture_functions;

enum section_type {
	INIT, EXIT, BOTH, NONE
};

enum attribute_type {
	UNVERIFIED, NOCAPTURE, PRINTF, BUILTINS, SYSCALL, NONE_ATTRIBUTE
};


#if BUILDING_GCC_VERSION >= 5000
typedef struct hash_set<const_gimple> gimple_set;

static inline bool pointer_set_insert(gimple_set *visited, const_gimple stmt)
{
	return visited->add(stmt);
}

static inline bool pointer_set_contains(gimple_set *visited, const_gimple stmt)
{
	return visited->contains(stmt);
}

static inline gimple_set* pointer_set_create(void)
{
	return new hash_set<const_gimple>;
}

static inline void pointer_set_destroy(gimple_set *visited)
{
	delete visited;
}

typedef struct hash_set<const_tree> tree_set;

static inline bool pointer_set_insert(tree_set *visited, const_tree node)
{
	return visited->add(node);
}

static inline tree_set* tree_pointer_set_create(void)
{
	return new hash_set<const_tree>;
}

static inline void pointer_set_destroy(tree_set *visited)
{
	delete visited;
}

typedef struct hash_set<struct cgraph_node *> cgraph_set;

static inline bool pointer_set_insert(cgraph_set *visited, struct cgraph_node *node)
{
	return visited->add(node);
}

static inline cgraph_set* cgraph_pointer_set_create(void)
{
	return new hash_set<struct cgraph_node *>;
}

static inline void pointer_set_destroy(cgraph_set *visited)
{
	delete visited;
}
#else
typedef struct pointer_set_t gimple_set;
typedef struct pointer_set_t tree_set;
typedef struct pointer_set_t cgraph_set;

static inline tree_set *tree_pointer_set_create(void)
{
	return pointer_set_create();
}

static inline cgraph_set *cgraph_pointer_set_create(void)
{
	return pointer_set_create();
}
#endif

static gimple initify_get_def_stmt(const_tree node)
{
	gcc_assert(node != NULL_TREE);

	if (TREE_CODE(node) != SSA_NAME)
		return NULL;
	return SSA_NAME_DEF_STMT(node);
}

static void search_constant_strings(bool *has_str_cst, gimple_set *visited, tree node);
static bool has_capture_use_local_var(const_tree vardecl);
static bool search_capture_ssa_use(gimple_set *visited_defs, tree node);

#define FUNCTION_PTR_P(node) \
	(TREE_CODE(TREE_TYPE(node)) == POINTER_TYPE && \
	(TREE_CODE(TREE_TYPE(TREE_TYPE(node))) == FUNCTION_TYPE || \
	TREE_CODE(TREE_TYPE(TREE_TYPE(node))) == METHOD_TYPE))

static bool is_vararg_arg(tree arg_list, unsigned int num)
{
	if (tree_last(arg_list) == void_list_node)
		return false;

	return num >= (unsigned int)list_length(arg_list);
}

static const_tree get_ptr_type(const_tree type)
{
	gcc_assert(type != NULL_TREE);

	if (TREE_CODE(type) != POINTER_TYPE)
		return type;
	return get_ptr_type(TREE_TYPE(type));
}

static bool check_parameter(tree *node, tree type_args, int idx)
{
	const_tree type_arg, type, type_type, type_name, ptr_type;

	if (is_vararg_arg(type_args, idx))
		return true;

	type_arg = chain_index(idx - 1, type_args);
	type = TREE_VALUE(type_arg);
	gcc_assert(type != NULL_TREE);
	type_type = TREE_TYPE(type);
	gcc_assert(type_type != NULL_TREE);

	type_name = TYPE_NAME(type_type);
	if (type_name != NULL_TREE && TREE_CODE(type_name) == IDENTIFIER_NODE && !strcmp(TYPE_NAME_POINTER(type_type), "va_format"))
		return true;

	if (TREE_CODE(type) != POINTER_TYPE) {
		error("%u. parameter of the %qE function must be a pointer", idx, *node);
		return false;
	}

	ptr_type = get_ptr_type(type_type);
	if (!TYPE_READONLY(ptr_type)) {
		error("%u. parameter of the %qE function must be readonly", idx, *node);
		return false;
	}

	if (TREE_THIS_VOLATILE(ptr_type)) {
		error("%u. parameter of the %qE function can't be volatile", idx, *node);
		return false;
	}

	return true;
}

static bool check_marked_parameters(tree *node, tree type_args, const_tree args, const_tree name)
{
	const_tree arg;
	bool negative_val;

	negative_val = false;
	for (arg = args; arg; arg = TREE_CHAIN(arg)) {
		int idx;
		unsigned int abs_idx;
		tree position = TREE_VALUE(arg);

		if (TREE_CODE(position) != INTEGER_CST) {
			error("%qE parameter of the %qE attribute isn't an integer (fn: %qE)", position, name, *node);
			return false;
		}

		idx = (int)tree_to_shwi(position);
		if (negative_val && idx < 0) {
			error("Only one negative attribute value is supported (attribute: %qE fn: %qE)", name, *node);
			return false;
		}

		if (idx < 0)
			negative_val = true;

		abs_idx = abs(idx);
		if (abs_idx == 0)
			continue;

		if (!check_parameter(node, type_args, abs_idx))
			return false;
	}
	return true;
}

static bool check_all_parameters(tree *node, tree type_args)
{
	int arg, len = list_length(type_args);

	if (tree_last(type_args) == void_list_node)
		len -= 1;

	for (arg = 1; arg <= len; arg++) {
		if (!check_parameter(node, type_args, arg))
			return false;
	}
	return true;
}

/* nocapture attribute:
 *  * to mark nocapture function arguments. If used on a vararg argument
 *    it applies to all of them that have no other uses.
 *  * attribute value 0 is ignored to allow reusing print attribute arguments
 */
static bool handle_initify_attributes(tree *node, tree name, tree args)
{
	tree type_args = NULL_TREE;

	switch (TREE_CODE(*node)) {
	case FUNCTION_DECL:
		type_args = TYPE_ARG_TYPES(TREE_TYPE(*node));
		break;

	case FUNCTION_TYPE:
	case METHOD_TYPE:
		type_args = TYPE_ARG_TYPES(*node);
		break;

	case TYPE_DECL: {
		enum tree_code fn_code;
		const_tree fntype = TREE_TYPE(*node);

		fn_code = TREE_CODE(fntype);
		if (fn_code == POINTER_TYPE)
			fntype = TREE_TYPE(fntype);
		fn_code = TREE_CODE(fntype);
		if (fn_code == FUNCTION_TYPE || fn_code == METHOD_TYPE) {
			type_args = TYPE_ARG_TYPES(fntype);
			break;
		}
		/* FALLTHROUGH */
	}

	default:
		debug_tree(*node);
		error("%s: %qE attribute only applies to functions", __func__, name);
		return false;
	}

	gcc_assert(type_args != NULL_TREE);

	if (!check_marked_parameters(node, type_args, args, name))
		return false;
	return args != NULL_TREE || check_all_parameters(node, type_args);
}

static tree handle_nocapture_attribute(tree *node, tree name, tree args, int __unused flags, bool *no_add_attrs)
{
	tree nocapture_attr;

	*no_add_attrs = true;

	if (!handle_initify_attributes(node, name, args))
		return NULL_TREE;

	nocapture_attr = lookup_attribute("nocapture", DECL_ATTRIBUTES(*node));
	if (nocapture_attr)
		chainon(TREE_VALUE(nocapture_attr), args);
	else
		*no_add_attrs = false;

	return NULL_TREE;
}

static tree handle_unverified_nocapture_attribute(tree *node, tree name, tree args, int __unused flags, bool *no_add_attrs)
{
	tree unverified_attr;

	*no_add_attrs = true;

	if (!handle_initify_attributes(node, name, args))
		return NULL_TREE;

	unverified_attr = lookup_attribute("unverified_nocapture", DECL_ATTRIBUTES(*node));
	if (unverified_attr)
		chainon(TREE_VALUE(unverified_attr), args);
	else
		*no_add_attrs = false;

	return NULL_TREE;
}

static struct attribute_spec nocapture_attr = {
	.name				= "nocapture",
	.min_length			= 0,
	.max_length			= -1,
	.decl_required			= true,
	.type_required			= false,
	.function_type_required		= false,
	.handler			= handle_nocapture_attribute,
#if BUILDING_GCC_VERSION >= 4007
	.affects_type_identity		= false
#endif
};

static struct attribute_spec unverified_nocapture_attr = {
	.name				= "unverified_nocapture",
	.min_length			= 0,
	.max_length			= -1,
	.decl_required			= true,
	.type_required			= false,
	.function_type_required		= false,
	.handler			= handle_unverified_nocapture_attribute,
#if BUILDING_GCC_VERSION >= 4007
	.affects_type_identity		= false
#endif
};

static void register_attributes(void __unused *event_data, void __unused *data)
{
	register_attribute(&nocapture_attr);
	register_attribute(&unverified_nocapture_attr);
}

/* Determine whether the function is in the init or exit sections. */
static enum section_type get_init_exit_section(const_tree decl)
{
	const char *str;
	const_tree section, attr_value;

	section = lookup_attribute("section", DECL_ATTRIBUTES(decl));
	if (!section)
		return NONE;

	attr_value = TREE_VALUE(section);
	gcc_assert(attr_value != NULL_TREE);
	gcc_assert(list_length(attr_value) == 1);

	str = TREE_STRING_POINTER(TREE_VALUE(attr_value));

	if (!strncmp(str, ".init.", 6))
		return INIT;
	if (!strncmp(str, ".exit.", 6))
		return EXIT;
	return NONE;
}

static tree get_string_cst(tree var)
{
	if (var == NULL_TREE)
		return NULL_TREE;

	if (TREE_CODE(var) == STRING_CST)
		return var;

	switch (TREE_CODE_CLASS(TREE_CODE(var))) {
	case tcc_expression:
	case tcc_reference: {
		int i;

		for (i = 0; i < TREE_OPERAND_LENGTH(var); i++) {
			tree ret = get_string_cst(TREE_OPERAND(var, i));
			if (ret != NULL_TREE)
				return ret;
		}
		break;
	}

	default:
		break;
	}

	return NULL_TREE;
}

static bool set_init_exit_section(tree decl)
{
	gcc_assert(DECL_P(decl));

	if (get_init_exit_section(decl) != NONE)
		return false;

	if (get_init_exit_section(current_function_decl) == INIT)
		set_decl_section_name(decl, ".init.rodata.str");
	else
		set_decl_section_name(decl, ".exit.rodata.str");
	return true;
}

/* Syscalls are always nocapture functions. */
static bool is_syscall(const_tree fn)
{
	const char *name = DECL_NAME_POINTER(fn);

	if (!strncmp(name, "sys_", 4))
		return true;

	if (!strncmp(name, "sys32_", 6))
		return true;

	if (!strncmp(name, "compat_sys_", 11))
		return true;

	return false;
}

/* These builtins are nocapture functions. */
static bool allowed_builtins(const_tree fn)
{
	const char *name = DECL_NAME_POINTER(fn);

	if (!strcmp(name, "__builtin_va_start"))
		return true;
	if (!strcmp(name, "__builtin_expect"))
		return true;
	if (!strcmp(name, "__builtin_memcpy"))
		return true;
	return false;
}

static enum attribute_type search_argnum_in_attribute_params(const_tree attr, int fn_arg_num, int fntype_arg_len)
{
	const_tree attr_val;

	for (attr_val = TREE_VALUE(attr); attr_val; attr_val = TREE_CHAIN(attr_val)) {
		int attr_arg_val;

		if (TREE_CODE(TREE_VALUE(attr_val)) == IDENTIFIER_NODE)
			continue;

		attr_arg_val = (int)abs(tree_to_shwi(TREE_VALUE(attr_val)));
		if (attr_arg_val == fn_arg_num)
			return NOCAPTURE;
		if (attr_arg_val > fntype_arg_len && fn_arg_num >= attr_arg_val)
			return NOCAPTURE;
	}
	return NONE_ATTRIBUTE;
}

/* Check that fn_arg_num is a nocapture argument, handle cloned functions too. */
static enum attribute_type lookup_nocapture_argument(const_tree fndecl, const_tree attr, int fn_arg_num, int fntype_arg_len)
{
	const_tree orig_decl, clone_arg, orig_arg;
	tree decl_list, orig_decl_list;
	enum attribute_type orig_attribute;
	struct cgraph_node *node = cgraph_get_node(fndecl);

	orig_attribute = search_argnum_in_attribute_params(attr, fn_arg_num, fntype_arg_len);
	if (orig_attribute == NONE_ATTRIBUTE)
		return orig_attribute;

	gcc_assert(node);
	if (node->clone_of && node->clone.tree_map)
		gcc_assert(!node->clone.args_to_skip);

	if (!DECL_ARTIFICIAL(fndecl) && DECL_ABSTRACT_ORIGIN(fndecl) == NULL_TREE)
		return orig_attribute;

	orig_decl = DECL_ABSTRACT_ORIGIN(fndecl);
	gcc_assert(orig_decl != NULL_TREE);

	decl_list = DECL_ARGUMENTS(fndecl);
	orig_decl_list = DECL_ARGUMENTS(orig_decl);

	if (decl_list == NULL_TREE || orig_decl_list == NULL_TREE)
		return NONE_ATTRIBUTE;

	if (list_length(decl_list) == list_length(orig_decl_list))
		return orig_attribute;

	clone_arg = chain_index(fn_arg_num - 1, decl_list);
	gcc_assert(clone_arg != NULL_TREE);

	orig_arg = chain_index(fn_arg_num - 1, orig_decl_list);
	gcc_assert(orig_arg != NULL_TREE);

	if (!strcmp(DECL_NAME_POINTER(clone_arg), DECL_NAME_POINTER(orig_arg)))
		return orig_attribute;
	return NONE_ATTRIBUTE;
}

/* Check whether the function argument is nocapture. */
static enum attribute_type is_fndecl_nocapture_arg(const_tree fndecl, int fn_arg_num)
{
	int fntype_arg_len;
	const_tree type, attr = NULL_TREE;
	bool fnptr = FUNCTION_PTR_P(fndecl);

	if (!fnptr && is_syscall(fndecl))
		return SYSCALL;

	if (!fnptr && DECL_BUILT_IN(fndecl) && allowed_builtins(fndecl))
		return BUILTINS;

	if (fnptr)
		type = TREE_TYPE(TREE_TYPE(fndecl));
	else
		type = TREE_TYPE(fndecl);

	fntype_arg_len = type_num_arguments(type);

	if (!fnptr)
		attr = lookup_attribute("unverified_nocapture", DECL_ATTRIBUTES(fndecl));
	if (attr != NULL_TREE && lookup_nocapture_argument(fndecl, attr, fn_arg_num, fntype_arg_len) != NONE_ATTRIBUTE)
		return UNVERIFIED;

	attr = lookup_attribute("format", TYPE_ATTRIBUTES(type));
	if (attr != NULL_TREE && lookup_nocapture_argument(fndecl, attr, fn_arg_num, fntype_arg_len) != NONE_ATTRIBUTE)
		return PRINTF;

	if (fnptr)
		return NONE_ATTRIBUTE;

	attr = lookup_attribute("nocapture", DECL_ATTRIBUTES(fndecl));
	if (attr == NULL_TREE)
		return NONE_ATTRIBUTE;

	if (TREE_VALUE(attr) == NULL_TREE)
		return NOCAPTURE;

	return lookup_nocapture_argument(fndecl, attr, fn_arg_num, fntype_arg_len);
}

/* Check whether arg_num is a nocapture argument that can be returned. */
static bool is_negative_nocapture_arg(const_tree fndecl, int arg_num)
{
	const_tree attr, attr_val;

	gcc_assert(arg_num <= 0);

	if (FUNCTION_PTR_P(fndecl))
		return false;

	attr = lookup_attribute("nocapture", DECL_ATTRIBUTES(fndecl));
	if (attr == NULL_TREE)
		return false;

	for (attr_val = TREE_VALUE(attr); attr_val; attr_val = TREE_CHAIN(attr_val)) {
		int attr_arg_val;

		if (arg_num == 0 && tree_int_cst_lt(TREE_VALUE(attr_val), integer_zero_node))
			return true;

		attr_arg_val = (int)tree_to_shwi(TREE_VALUE(attr_val));
		if (attr_arg_val == arg_num)
			return true;
	}

	return false;
}

static bool is_same_vardecl(const_tree op, const_tree vardecl)
{
	const_tree decl;

	if (op == vardecl)
		return true;
	if (TREE_CODE(op) == SSA_NAME)
		decl = SSA_NAME_VAR(op);
	else
		decl = op;

	if (decl == NULL_TREE || !DECL_P(decl))
		return false;

	if (TREE_CODE(decl) != TREE_CODE(vardecl))
		return false;

	return DECL_NAME(decl) && !strcmp(DECL_NAME_POINTER(decl), DECL_NAME_POINTER(vardecl));
}

static bool search_same_vardecl(const_tree value, const_tree vardecl)
{
	int i;

	for (i = 0; i < TREE_OPERAND_LENGTH(value); i++) {
		const_tree op = TREE_OPERAND(value, i);

		if (op == NULL_TREE)
			continue;
		if (is_same_vardecl(op, vardecl))
			return true;
		if (search_same_vardecl(op, vardecl))
			return true;
	}
	return false;
}

static bool check_constructor(const_tree constructor, const_tree vardecl)
{
	unsigned HOST_WIDE_INT cnt __unused;
	tree value;

	FOR_EACH_CONSTRUCTOR_VALUE(CONSTRUCTOR_ELTS(constructor), cnt, value) {
		if (TREE_CODE(value) == CONSTRUCTOR)
			return check_constructor(value, vardecl);
		if (is_gimple_constant(value))
			continue;

		gcc_assert(TREE_OPERAND_LENGTH(value) > 0);
		if (search_same_vardecl(value, vardecl))
			return true;
	}
	return false;
}

static bool compare_ops(const_tree vardecl, tree op)
{
	if (TREE_CODE(op) == TREE_LIST)
		op = TREE_VALUE(op);
	if (TREE_CODE(op) == SSA_NAME)
		op = SSA_NAME_VAR(op);
	if (op == NULL_TREE)
		return false;

	switch (TREE_CODE_CLASS(TREE_CODE(op))) {
	case tcc_declaration:
		return is_same_vardecl(op, vardecl);

	case tcc_exceptional:
		return check_constructor(op, vardecl);

	case tcc_constant:
	case tcc_statement:
	case tcc_comparison:
		return false;

	default:
		break;
	}

	gcc_assert(TREE_OPERAND_LENGTH(op) > 0);
	return search_same_vardecl(op, vardecl);
}

static bool is_stmt_nocapture_arg(const gcall *stmt, int arg_num)
{
	tree fndecl;

	fndecl = gimple_call_fndecl(stmt);
	if (fndecl == NULL_TREE)
		fndecl = gimple_call_fn(stmt);

	gcc_assert(fndecl != NULL_TREE);
	if (is_fndecl_nocapture_arg(fndecl, arg_num) != NONE_ATTRIBUTE)
		return true;

	/*
	 * These are potentially nocapture functions that must be checked
	 *  manually.
	 */
	if (print_missing_attr)
		inform(gimple_location(stmt), "nocapture attribute is missing (fn: %E, arg: %u)\n", fndecl, arg_num);
	return false;
}

/* Find the argument position of arg. */
static int get_arg_num(const gcall *call, const_tree arg)
{
	int idx;

	for (idx = 0; idx < (int)gimple_call_num_args(call); idx++) {
		const_tree cur_arg = gimple_call_arg(call, idx);

		if (cur_arg == arg)
			return idx + 1;
	}

	debug_tree(arg);
	debug_gimple_stmt(call);
	gcc_unreachable();
}

/* Determine if the variable uses are only in nocapture functions. */
static bool only_nocapture_call(const_tree decl)
{
	struct cgraph_edge *e;
	struct cgraph_node *caller;
	bool has_call = false;

	gcc_assert(TREE_CODE(decl) == VAR_DECL);

	caller = cgraph_get_node(current_function_decl);
	for (e = caller->callees; e; e = e->next_callee) {
		int idx;
		const gcall *call = as_a_const_gcall(e->call_stmt);

		for (idx = 0; idx < (int)gimple_call_num_args(call); idx++) {
			const_tree arg = gimple_call_arg(call, idx);

			if (TREE_CODE(arg) != ADDR_EXPR)
				continue;
			if (TREE_OPERAND(arg, 0) != decl)
				continue;

			has_call = true;
			if (!is_stmt_nocapture_arg(call, idx + 1))
				return false;
		}
	}

	gcc_assert(has_call);
	return has_call;
}

/* Determine if all uses of a va_format typed variable are nocapture. */
static bool is_va_format_use_nocapture(const_tree node)
{
	const_tree decl, type;

	if (TREE_CODE(node) != COMPONENT_REF)
		return false;

	decl = TREE_OPERAND(node, 0);
	type = TREE_TYPE(decl);
	gcc_assert(TREE_CODE(type) == RECORD_TYPE);

	if (!TYPE_NAME(type) || strcmp(TYPE_NAME_POINTER(type), "va_format"))
		return false;

	return only_nocapture_call(decl);
}

/* If there is a cast to integer (from const char) then it is a nocapture data flow */
static bool is_cast_to_integer_type(gassign *assign)
{
	const_tree lhs_type, lhs;

	if (!gimple_assign_cast_p(assign))
		return false;

	lhs = gimple_assign_rhs1(assign);
	lhs_type = TREE_TYPE(lhs);
	return TYPE_MODE(lhs_type) != QImode;
}

/* Search the uses of a return value. */
static bool is_return_value_captured(gimple_set *visited_defs, const gcall *call)
{
	tree ret = gimple_call_lhs(call);

	gcc_assert(ret != NULL_TREE);
	return search_capture_ssa_use(visited_defs, ret);
}

/* Check if arg_num is a nocapture argument. */
static bool is_call_arg_nocapture(gimple_set *visited_defs, const gcall *call, int arg_num)
{
	tree fndecl = gimple_call_fndecl(call);

	if (fndecl == NULL_TREE)
		fndecl = gimple_call_fn(call);
	gcc_assert(fndecl != NULL_TREE);

	if (is_negative_nocapture_arg(fndecl, -arg_num) && is_return_value_captured(visited_defs, call))
		return false;

	return is_stmt_nocapture_arg(call, arg_num);
}

/* Determine whether the function has at least one nocapture argument. */
static bool has_nocapture_param(const_tree fndecl)
{
	const_tree attr;

	if (fndecl == NULL_TREE)
		return false;

	if (is_syscall(fndecl))
		return true;

	attr = lookup_attribute("nocapture", DECL_ATTRIBUTES(fndecl));
	if (attr == NULL_TREE)
		attr = lookup_attribute("format", TYPE_ATTRIBUTES(TREE_TYPE(fndecl)));
	return attr != NULL_TREE;
}

static void walk_def_stmt(bool *has_capture_use, gimple_set *visited, tree node)
{
	gimple def_stmt;
	const_tree parm_decl;

	if (*has_capture_use)
		return;

	if (TREE_CODE(node) != SSA_NAME)
		goto true_out;

	parm_decl = SSA_NAME_VAR(node);
	if (parm_decl != NULL_TREE && TREE_CODE(parm_decl) == PARM_DECL)
		return;

	def_stmt = initify_get_def_stmt(node);
	if (pointer_set_insert(visited, def_stmt))
		return;

	switch (gimple_code(def_stmt)) {
	case GIMPLE_CALL: {
		tree fndecl = gimple_call_fndecl(def_stmt);

		if (fndecl == NULL_TREE)
			fndecl = gimple_call_fn(def_stmt);

		gcc_assert(fndecl != NULL_TREE);
		if (has_nocapture_param(fndecl))
			goto true_out;
		return;
	}

	case GIMPLE_ASM:
	case GIMPLE_ASSIGN:
		goto true_out;

	case GIMPLE_NOP:
		return;

	case GIMPLE_PHI: {
		unsigned int i;

		for (i = 0; i < gimple_phi_num_args(def_stmt); i++) {
			tree arg = gimple_phi_arg_def(def_stmt, i);

			walk_def_stmt(has_capture_use, visited, arg);
		}
		return;
	}

	default:
		debug_gimple_stmt(def_stmt);
		error("%s: unknown gimple code", __func__);
		gcc_unreachable();
	}
	gcc_unreachable();

true_out:
	*has_capture_use = true;
}

static bool search_return_capture_use(const greturn *ret_stmt)
{
	gimple_set *def_visited;
	tree ret;
	bool has_capture_use;

	if (is_negative_nocapture_arg(current_function_decl, 0))
		return false;

	def_visited = pointer_set_create();
	ret = gimple_return_retval(ret_stmt);
	has_capture_use = false;
	walk_def_stmt(&has_capture_use, def_visited, ret);
	pointer_set_destroy(def_visited);

	return has_capture_use;
}

static bool lhs_is_a_nocapture_parm_decl(const_tree lhs)
{
	int arg_idx, len;
	tree arg_list;

	if (TREE_CODE(lhs) != PARM_DECL)
		return false;

	arg_list = DECL_ARGUMENTS(current_function_decl);
	len = list_length(arg_list);

	for (arg_idx = 0; arg_idx < len; arg_idx++) {
		const_tree arg = chain_index(arg_idx, arg_list);

		if (arg == lhs)
			return is_fndecl_nocapture_arg(current_function_decl, arg_idx + 1) != NONE_ATTRIBUTE;
	}

	debug_tree(current_function_decl);
	debug_tree(lhs);
	gcc_unreachable();
}

static void has_capture_use_ssa_var(bool *has_capture_use, gimple_set *visited_defs, tree_set *use_visited, tree node)
{
	imm_use_iterator imm_iter;
	use_operand_p use_p;

	if (pointer_set_insert(use_visited, node))
		return;

	if (*has_capture_use)
		return;

	if (is_va_format_use_nocapture(node))
		return;

	if (lhs_is_a_nocapture_parm_decl(node))
		return;

	if (TREE_CODE(node) != SSA_NAME)
		goto true_out;

	FOR_EACH_IMM_USE_FAST(use_p, imm_iter, node) {
		gimple use_stmt = USE_STMT(use_p);

		if (use_stmt == NULL)
			return;
		if (is_gimple_debug(use_stmt))
			continue;

		if (pointer_set_insert(visited_defs, use_stmt))
			continue;

		switch (gimple_code(use_stmt)) {
		case GIMPLE_COND:
		case GIMPLE_SWITCH:
			return;

		case GIMPLE_ASM:
			goto true_out;

		case GIMPLE_CALL: {
			const gcall *call = as_a_const_gcall(use_stmt);
			int arg_num = get_arg_num(call, node);

			if (is_call_arg_nocapture(visited_defs, call, arg_num))
				return;
			goto true_out;
		}

		case GIMPLE_ASSIGN: {
			tree lhs;
			gassign *assign = as_a_gassign(use_stmt);
			const_tree rhs = gimple_assign_rhs1(assign);

			if (TREE_CODE(rhs) == INDIRECT_REF)
				return;
#if BUILDING_GCC_VERSION >= 4006
			if (TREE_CODE(rhs) == MEM_REF)
				return;
#endif
			if (is_cast_to_integer_type(assign))
				return;

			lhs = gimple_assign_lhs(assign);
			has_capture_use_ssa_var(has_capture_use, visited_defs, use_visited, lhs);
			return;
		}

		case GIMPLE_PHI: {
			tree result = gimple_phi_result(use_stmt);

			has_capture_use_ssa_var(has_capture_use, visited_defs, use_visited, result);
			return;
		}

		case GIMPLE_RETURN:
			if (search_return_capture_use(as_a_const_greturn(use_stmt)))
				goto true_out;
			return;

		default:
			debug_tree(node);
			debug_gimple_stmt(use_stmt);
			gcc_unreachable();
		}
	}
	return;

true_out:
	*has_capture_use = true;
}

static bool search_capture_ssa_use(gimple_set *visited_defs, tree node)
{
	tree_set *use_visited;
	bool has_capture_use = false;

	use_visited = tree_pointer_set_create();
	has_capture_use_ssa_var(&has_capture_use, visited_defs, use_visited, node);
	pointer_set_destroy(use_visited);

	return has_capture_use;
}

static bool search_capture_use(const_tree vardecl, gimple stmt)
{
	unsigned int i;
	gimple_set *visited_defs = pointer_set_create();

	for (i = 0; i < gimple_num_ops(stmt); i++) {
		int arg_num;
		tree op = *(gimple_op_ptr(stmt, i));

		if (op == NULL_TREE)
			continue;
		if (is_gimple_constant(op))
			continue;

		if (!compare_ops(vardecl, op))
			continue;

		switch (gimple_code(stmt)) {
		case GIMPLE_COND:
			break;

		case GIMPLE_ASM:
			gcc_assert(get_init_exit_section(vardecl) == NONE);
			goto true_out;

		case GIMPLE_CALL:
			if (i == 0)
				break;
			/* return, fndecl */
			gcc_assert(i >= 3);
			arg_num = i - 2;

			if (is_call_arg_nocapture(visited_defs, as_a_const_gcall(stmt), arg_num))
				break;
			goto true_out;

		case GIMPLE_ASSIGN: {
			tree lhs;
			const_tree rhs = gimple_assign_rhs1(stmt);

			if (TREE_CODE(rhs) == INDIRECT_REF)
				break;
#if BUILDING_GCC_VERSION >= 4006
			if (TREE_CODE(rhs) == MEM_REF)
				break;
#endif

			lhs = gimple_assign_lhs(stmt);
			if (lhs_is_a_nocapture_parm_decl(lhs))
				break;

			if (!search_capture_ssa_use(visited_defs, lhs))
				break;
			gcc_assert(get_init_exit_section(vardecl) == NONE);
			goto true_out;
		}

		case GIMPLE_RETURN:
			if (search_return_capture_use(as_a_const_greturn(stmt)))
				goto true_out;
			break;
		default:
			debug_tree(vardecl);
			debug_gimple_stmt(stmt);
			gcc_unreachable();
		}
	}

	pointer_set_destroy(visited_defs);
	return false;

true_out:
	pointer_set_destroy(visited_defs);
	return true;

}

/* Check all initialized local variables for nocapture uses. */
static bool is_in_capture_init(const_tree vardecl)
{
	unsigned int i __unused;
	tree var;

	if (TREE_CODE(vardecl) == PARM_DECL)
		return false;

	FOR_EACH_LOCAL_DECL(cfun, i, var) {
		const_tree type, initial = DECL_INITIAL(var);

		if (DECL_EXTERNAL(var))
			continue;
		if (initial == NULL_TREE)
			continue;
		if (TREE_CODE(initial) != CONSTRUCTOR)
			continue;

		type = TREE_TYPE(var);
		gcc_assert(TREE_CODE(type) == RECORD_TYPE || DECL_P(var));
		if (check_constructor(initial, vardecl))
			return true;
	}
	return false;
}

static bool has_capture_use_local_var(const_tree vardecl)
{
	basic_block bb;
	enum tree_code code = TREE_CODE(vardecl);

	gcc_assert(code == VAR_DECL || code == PARM_DECL);

	if (is_in_capture_init(vardecl))
		return true;

	FOR_EACH_BB_FN(bb, cfun) {
		gimple_stmt_iterator gsi;

		for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
			if (search_capture_use(vardecl, gsi_stmt(gsi)))
				return true;
		}
	}

	return false;
}

/* Search local variables that have only nocapture uses. */
static void find_local_str(void)
{
	unsigned int i __unused;
	tree var;

	FOR_EACH_LOCAL_DECL(cfun, i, var) {
		tree str, init_val;

		if (TREE_CODE(TREE_TYPE(var)) != ARRAY_TYPE)
			continue;

		init_val = DECL_INITIAL(var);
		if (init_val == NULL_TREE || init_val == error_mark_node)
			continue;
		if (TREE_CODE(init_val) != STRING_CST)
			continue;

		if (has_capture_use_local_var(var))
			continue;

		str = get_string_cst(init_val);
		gcc_assert(str);

		if (set_init_exit_section(var) && verbose)
			inform(DECL_SOURCE_LOCATION(var), "initified local var: %s: %s", DECL_NAME_POINTER(current_function_decl), TREE_STRING_POINTER(str));
	}
}

static tree create_decl(tree node)
{
	tree str, decl, type, name, type_type;
	location_t loc;

	str = get_string_cst(node);
	type = TREE_TYPE(str);
	gcc_assert(TREE_CODE(type) == ARRAY_TYPE);

	type_type = TREE_TYPE(type);
	gcc_assert(type_type != NULL_TREE && TREE_CODE(type_type) == INTEGER_TYPE);

	name = create_tmp_var_name("initify");
	loc = DECL_SOURCE_LOCATION(current_function_decl);
	decl = build_decl(loc, VAR_DECL, name, type);

	DECL_INITIAL(decl) = str;
	DECL_CONTEXT(decl) = current_function_decl;
	DECL_ARTIFICIAL(decl) = 1;

	TREE_STATIC(decl) = 1;
	TREE_READONLY(decl) = 1;
	TREE_ADDRESSABLE(decl) = 1;
	TREE_USED(decl) = 1;

	add_referenced_var(decl);
	add_local_decl(cfun, decl);

	varpool_add_new_variable(decl);
	varpool_mark_needed_node(varpool_node(decl));

	DECL_CHAIN(decl) = BLOCK_VARS(DECL_INITIAL(current_function_decl));
	BLOCK_VARS(DECL_INITIAL(current_function_decl)) = decl;

	return build_fold_addr_expr_loc(loc, decl);
}

static void set_section_call_assign(gimple stmt, tree node, unsigned int num)
{
	tree decl;

	decl = create_decl(node);

	switch (gimple_code(stmt)) {
	case GIMPLE_ASSIGN:
		gcc_assert(gimple_num_ops(stmt) == 2);
		gimple_assign_set_rhs1(stmt, decl);
		break;

	case GIMPLE_CALL:
		gimple_call_set_arg(stmt, num, decl);
		break;

	default:
		debug_gimple_stmt(stmt);
		error("%s: unknown gimple code", __func__);
		gcc_unreachable();
	}

	update_stmt(stmt);

	if (set_init_exit_section(TREE_OPERAND(decl, 0)) && verbose)
		inform(gimple_location(stmt), "initified function arg: %E: [%E]", current_function_decl, get_string_cst(node));
}

static tree initify_create_new_var(tree type)
{
	tree new_var = create_tmp_var(type, "initify");

	add_referenced_var(new_var);
	mark_sym_for_renaming(new_var);
	return new_var;
}

static void initify_create_new_phi_arg(gimple_set *visited_defs, tree ssa_var, gphi *stmt, unsigned int i)
{
	gassign *assign;
	gimple_stmt_iterator gsi;
	basic_block arg_bb;
	tree decl, arg;
	const_tree str;
	location_t loc;

	arg = gimple_phi_arg_def(stmt, i);

	if (search_capture_ssa_use(visited_defs, arg))
		return;

	decl = create_decl(arg);

	assign = gimple_build_assign(ssa_var, decl);

	arg_bb = gimple_phi_arg_edge(stmt, i)->src;
	gcc_assert(arg_bb->index != 0);

	gsi = gsi_after_labels(arg_bb);
	gsi_insert_before(&gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);

	if (!set_init_exit_section(TREE_OPERAND(decl, 0)) || !verbose)
		return;

	loc = gimple_location(stmt);
	str = get_string_cst(arg);
	inform(loc, "initified local var, phi arg: %E: [%E]", current_function_decl, str);
}

static void set_section_phi(bool *has_str_cst, gimple_set *visited, gphi *stmt)
{
	tree result, ssa_var;
	unsigned int i;

	result = gimple_phi_result(stmt);
	ssa_var = initify_create_new_var(TREE_TYPE(result));

	for (i = 0; i < gimple_phi_num_args(stmt); i++) {
		tree arg = gimple_phi_arg_def(stmt, i);

		if (get_string_cst(arg) == NULL_TREE)
			search_constant_strings(has_str_cst, visited, arg);
		else
			initify_create_new_phi_arg(visited, ssa_var, stmt, i);
	}
}

static void search_constant_strings(bool *has_str_cst, gimple_set *visited, tree node)
{
	gimple def_stmt;
	const_tree parm_decl;

	if (!*has_str_cst)
		return;

	if (TREE_CODE(node) != SSA_NAME)
		goto false_out;

	parm_decl = SSA_NAME_VAR(node);
	if (parm_decl != NULL_TREE && TREE_CODE(parm_decl) == PARM_DECL)
		goto false_out;

	def_stmt = initify_get_def_stmt(node);
	if (pointer_set_insert(visited, def_stmt))
		return;

	switch (gimple_code(def_stmt)) {
	case GIMPLE_NOP:
	case GIMPLE_CALL:
	case GIMPLE_ASM:
	case GIMPLE_RETURN:
		goto false_out;

	case GIMPLE_PHI:
		set_section_phi(has_str_cst, visited, as_a_gphi(def_stmt));
		return;

	case GIMPLE_ASSIGN: {
		tree rhs1, str;

		if (gimple_num_ops(def_stmt) != 2)
			goto false_out;

		rhs1 = gimple_assign_rhs1(def_stmt);
		search_constant_strings(has_str_cst, visited, rhs1);
		if (!*has_str_cst)
			return;

		if (search_capture_ssa_use(visited, node))
			goto false_out;

		str = get_string_cst(rhs1);
		gcc_assert(str != NULL_TREE);
		set_section_call_assign(def_stmt, rhs1, 0);
		return;
	}

	default:
		debug_gimple_stmt(def_stmt);
		error("%s: unknown gimple code", __func__);
		gcc_unreachable();
	}
	gcc_unreachable();

false_out:
	*has_str_cst = false;
}

/* Search constant strings assigned to variables. */
static void search_var_param(gcall *stmt)
{
	int num;
	gimple_set *visited = pointer_set_create();

	pointer_set_insert(visited, stmt);

	for (num = 0; num < (int)gimple_call_num_args(stmt); num++) {
		const_tree type, fndecl;
		bool has_str_cst = true;
		tree str, arg = gimple_call_arg(stmt, num);

		str = get_string_cst(arg);
		if (str != NULL_TREE)
			continue;

		if (TREE_CODE(TREE_TYPE(arg)) != POINTER_TYPE)
			continue;
		type = TREE_TYPE(TREE_TYPE(arg));
		if (!TYPE_STRING_FLAG(type))
			continue;

		fndecl = gimple_call_fndecl(stmt);
		if (is_negative_nocapture_arg(fndecl, -(num + 1)) && is_return_value_captured(visited, stmt))
			continue;

		if (is_fndecl_nocapture_arg(fndecl, num + 1) != NONE_ATTRIBUTE)
			search_constant_strings(&has_str_cst, visited, arg);
	}

	pointer_set_destroy(visited);
}

/* Search constant strings passed as arguments. */
static void search_str_param(gcall *stmt)
{
	int num;
	gimple_set *visited = pointer_set_create();

	pointer_set_insert(visited, stmt);

	for (num = 0; num < (int)gimple_call_num_args(stmt); num++) {
		const_tree fndecl;
		tree str, arg = gimple_call_arg(stmt, num);

		str = get_string_cst(arg);
		if (str == NULL_TREE)
			continue;

		fndecl = gimple_call_fndecl(stmt);
		if (is_negative_nocapture_arg(fndecl, -(num + 1)) && is_return_value_captured(visited, stmt))
			continue;

		if (is_fndecl_nocapture_arg(fndecl, num + 1) != NONE_ATTRIBUTE)
			set_section_call_assign(stmt, arg, num);
	}

	pointer_set_destroy(visited);
}

/* Search constant strings in arguments of nocapture functions. */
static void search_const_strs(void)
{
	basic_block bb;

	FOR_EACH_BB_FN(bb, cfun) {
		gimple_stmt_iterator gsi;

		for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
			gcall *call_stmt;
			gimple stmt = gsi_stmt(gsi);

			if (!is_gimple_call(stmt))
				continue;

			call_stmt = as_a_gcall(stmt);
			if (!has_nocapture_param(gimple_call_fndecl(call_stmt)))
				continue;
			search_str_param(call_stmt);
			search_var_param(call_stmt);
		}
	}
}

/*
 * Verify the data flows of the uses of function arguments marked by the nocapture attribute.
 * The printf attribute is ignored temporarily.
 */
static void verify_nocapture_functions(void)
{
	int i, len;
	tree arg_list;

	if (disable_verify_nocapture_functions)
		return;

	if (is_syscall(current_function_decl))
		return;

	if (!has_nocapture_param(current_function_decl))
		return;

	arg_list = DECL_ARGUMENTS(current_function_decl);
	len = list_length(arg_list);
	for (i = 0; i < len; i++) {
		const_tree arg;

		if (is_fndecl_nocapture_arg(current_function_decl, i + 1) != NOCAPTURE)
			continue;

		arg = chain_index(i, arg_list);
		gcc_assert(arg != NULL_TREE);

		if (has_capture_use_local_var(arg))
			warning(0, "%qE captures its %u (%qD) parameter, please remove it from the nocapture attribute.", current_function_decl, i + 1, arg);
	}
}

/* Find and move constant strings to the proper init or exit read-only data section. */
static unsigned int initify_function_transform(struct cgraph_node *node __unused)
{
	verify_nocapture_functions();

	if (get_init_exit_section(current_function_decl) == NONE)
		return 0;

	find_local_str();
	search_const_strs();

	return TODO_dump_func | TODO_verify_ssa | TODO_verify_stmts
		| TODO_remove_unused_locals | TODO_cleanup_cfg
		| TODO_ggc_collect | TODO_verify_flow | TODO_update_ssa;
}

static void __unused debug_print_section_type(struct cgraph_node *node)
{
	enum section_type section;

	section = (enum section_type)(unsigned long)NODE_SYMBOL(node)->aux;
	switch (section) {
	case INIT:
		fprintf(stderr, "init\n");
		break;

	case EXIT:
		fprintf(stderr, "exit\n");
		break;

	case BOTH:
		fprintf(stderr, "init and exit\n");
		break;

	case NONE:
		fprintf(stderr, "none\n");
		break;
	}
}

static bool has_non_init_caller(struct cgraph_node *callee)
{
	struct cgraph_edge *e = callee->callers;

	if (!e)
		return true;

	for (; e; e = e->next_caller) {
		enum section_type caller_section;
		struct cgraph_node *caller = e->caller;

		caller_section = get_init_exit_section(NODE_DECL(caller));
		if (caller_section == NONE && NODE_SYMBOL(caller)->aux == (void *)NONE)
			return true;
	}

	return false;
}

static bool has_non_init_clone(cgraph_set *visited, struct cgraph_node *node)
{
	if (!node)
		return false;

	if (pointer_set_insert(visited, node))
		return false;

	if (has_non_init_caller(node))
		return true;

	if (has_non_init_clone(visited, node->clones))
		return true;

	return has_non_init_clone(visited, node->clone_of);
}

/*
 * If the function is called by only __init/__exit functions then it can become
 * an __init/__exit function as well.
 */
static bool should_init_exit(struct cgraph_node *callee)
{
	cgraph_set *visited;
	bool has_non_init;
	const_tree callee_decl = NODE_DECL(callee);

	if (NODE_SYMBOL(callee)->aux != (void *)NONE)
		return false;
	if (get_init_exit_section(callee_decl) != NONE)
		return false;

	/* If gcc isn't in LTO mode then we can handle only static functions. */
	if (!in_lto_p && TREE_PUBLIC(callee_decl))
		return false;

	if (NODE_SYMBOL(callee)->address_taken)
		return false;

	visited = cgraph_pointer_set_create();
	has_non_init = has_non_init_clone(visited, callee);
	pointer_set_destroy(visited);

	return !has_non_init;
}

static bool inherit_section(struct cgraph_node *callee, struct cgraph_node *caller, enum section_type caller_section)
{
	enum section_type callee_section;

	if (caller_section == NONE)
		caller_section = (enum section_type)(unsigned long)NODE_SYMBOL(caller)->aux;

	callee_section = (enum section_type)(unsigned long)NODE_SYMBOL(callee)->aux;
	if (caller_section == INIT && callee_section == EXIT)
		goto both_section;

	if (caller_section == EXIT && callee_section == INIT)
		goto both_section;

	if (caller_section == BOTH && (callee_section == INIT || callee_section == EXIT))
		goto both_section;

	if (!should_init_exit(callee))
		return false;

	gcc_assert(callee_section == NONE);
	NODE_SYMBOL(callee)->aux = (void *)caller_section;
	return true;

both_section:
	NODE_SYMBOL(callee)->aux = (void *)BOTH;
	return true;
}

/*
 * Try to propagate __init/__exit to callees in __init/__exit functions.
 * If a function is called by __init and __exit functions as well then it can be
 * an __exit function at most.
 */
static bool search_init_exit_callers(void)
{
	struct cgraph_node *node;
	bool change = false;

	FOR_EACH_FUNCTION(node) {
		struct cgraph_edge *e;
		enum section_type section;
		const_tree cur_fndecl = NODE_DECL(node);

		if (DECL_BUILT_IN(cur_fndecl))
			continue;

		section = get_init_exit_section(cur_fndecl);
		if (section == NONE && NODE_SYMBOL(node)->aux == (void *)NONE)
			continue;

		for (e = node->callees; e; e = e->next_callee) {
			if (e->callee->global.inlined_to)
				continue;

			if (inherit_section(e->callee, node, section))
				change = true;
		}
	}

	return change;
}

/* We can't move functions to the init/exit sections from certain sections. */
static bool can_move_to_init_exit(const_tree fndecl)
{
	const char *section_name = get_decl_section_name(fndecl);

	if (!section_name)
		return true;

	if (!strcmp(section_name, ".ref.text"))
		return false;

	if (!strcmp(section_name, ".meminit.text"))
		return false;

	inform(DECL_SOURCE_LOCATION(fndecl), "Section of %qE: %s\n", fndecl, section_name);
	gcc_unreachable();
}

static void move_function_to_init_exit_text(struct cgraph_node *node)
{
	const char *section_name;
	tree id, attr;
	tree section_str, attr_args, fndecl = NODE_DECL(node);

	/*
	 * If the function is a candidate for both __init and __exit and enable_init_to_exit_moves is false
	 * then these functions arent't moved to the exit section.
	 */
	if (NODE_SYMBOL(node)->aux == (void *)BOTH) {
		if (enable_init_to_exit_moves)
			NODE_SYMBOL(node)->aux = (void *)EXIT;
		else
			return;
	}

	if (NODE_SYMBOL(node)->aux == (void *)NONE)
		return;

	if (!can_move_to_init_exit(fndecl))
		return;

	if (verbose) {
		const char *attr_name;
		location_t loc = DECL_SOURCE_LOCATION(fndecl);

		attr_name = NODE_SYMBOL(node)->aux == (void *)INIT ? "__init" : "__exit";

		if (in_lto_p && TREE_PUBLIC(fndecl))
			inform(loc, "%s attribute is missing from the %qE function (public)", attr_name, fndecl);

		if (!in_lto_p && !TREE_PUBLIC(fndecl))
			inform(loc, "%s attribute is missing from the %qE function (static)", attr_name, fndecl);
	}

	if (in_lto_p)
		return;

	/* Add the init/exit section attribute to the function declaration. */
	DECL_ATTRIBUTES(fndecl) = copy_list(DECL_ATTRIBUTES(fndecl));

	section_name = NODE_SYMBOL(node)->aux == (void *)INIT ? ".init.text" : ".exit.text";
	section_str = build_const_char_string(strlen(section_name) + 1, section_name);
	TREE_READONLY(section_str) = 1;
	TREE_STATIC(section_str) = 1;
	attr_args = build_tree_list(NULL_TREE, section_str);

	id = get_identifier("__section__");
	attr = DECL_ATTRIBUTES(fndecl);
	DECL_ATTRIBUTES(fndecl) = tree_cons(id, attr_args, attr);

#if BUILDING_GCC_VERSION < 5000
	DECL_SECTION_NAME(fndecl) = section_str;
#endif
	set_decl_section_name(fndecl, section_name);
}

/* Find all functions that can become __init/__exit functions */
static unsigned int initify_execute(void)
{
	struct cgraph_node *node;

	if (!search_init_exit_functions)
		return 0;

	if (flag_lto && !in_lto_p)
		return 0;

	FOR_EACH_FUNCTION(node)
		NODE_SYMBOL(node)->aux = (void *)NONE;

	while (search_init_exit_callers()) {};

	FOR_EACH_FUNCTION(node) {
		move_function_to_init_exit_text(node);

		NODE_SYMBOL(node)->aux = NULL;
	}

	return 0;
}

#define PASS_NAME initify
#define NO_WRITE_SUMMARY
#define NO_GENERATE_SUMMARY
#define NO_READ_SUMMARY
#define NO_READ_OPTIMIZATION_SUMMARY
#define NO_WRITE_OPTIMIZATION_SUMMARY
#define NO_STMT_FIXUP
#define NO_VARIABLE_TRANSFORM
#define NO_GATE

#include "gcc-generate-ipa-pass.h"

static unsigned int (*old_section_type_flags)(tree decl, const char *name, int reloc);

static unsigned int initify_section_type_flags(tree decl, const char *name, int reloc)
{
	if (!strcmp(name, ".init.rodata.str") || !strcmp(name, ".exit.rodata.str")) {
		gcc_assert(TREE_CODE(decl) == VAR_DECL);
		gcc_assert(DECL_INITIAL(decl));
		gcc_assert(TREE_CODE(DECL_INITIAL(decl)) == STRING_CST);

		return 1 | SECTION_MERGE | SECTION_STRINGS;
	}

	return old_section_type_flags(decl, name, reloc);
}

static void initify_start_unit(void __unused *gcc_data, void __unused *user_data)
{
	old_section_type_flags = targetm.section_type_flags;
	targetm.section_type_flags = initify_section_type_flags;
}

__visible int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version)
{
	int i;
	const int argc = plugin_info->argc;
	bool enabled = true;
	const struct plugin_argument * const argv = plugin_info->argv;
	const char * const plugin_name = plugin_info->base_name;

	PASS_INFO(initify, "inline", 1, PASS_POS_INSERT_AFTER);

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("incompatible gcc/plugin versions"));
		return 1;
	}

	for (i = 0; i < argc; ++i) {
		if (!(strcmp(argv[i].key, "disable"))) {
			enabled = false;
			continue;
		}
		if (!strcmp(argv[i].key, "verbose")) {
			verbose = true;
			continue;
		}
		if (!strcmp(argv[i].key, "print_missing_attr")) {
			print_missing_attr = true;
			continue;
		}
		if (!strcmp(argv[i].key, "search_init_exit_functions")) {
			search_init_exit_functions = true;
			continue;
		}
		if (!strcmp(argv[i].key, "enable_init_to_exit_moves")) {
			enable_init_to_exit_moves = true;
			continue;
		}

		if (!strcmp(argv[i].key, "disable_verify_nocapture_functions")) {
			disable_verify_nocapture_functions = true;
			continue;
		}

		error(G_("unkown option '-fplugin-arg-%s-%s'"), plugin_name, argv[i].key);
	}

	register_callback(plugin_name, PLUGIN_INFO, NULL, &initify_plugin_info);
	if (enabled) {
		register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &initify_pass_info);
		register_callback(plugin_name, PLUGIN_START_UNIT, initify_start_unit, NULL);
	}
	register_callback(plugin_name, PLUGIN_ATTRIBUTES, register_attributes, NULL);

	return 0;
}
