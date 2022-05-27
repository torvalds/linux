/*
 * Copyright 2012-2016 by the PaX Team <pageexec@freemail.hu>
 * Copyright 2016 by Emese Revfy <re.emese@gmail.com>
 * Licensed under the GPL v2
 *
 * Note: the choice of the license means that the compilation process is
 *       NOT 'eligible' as defined by gcc's library exception to the GPL v3,
 *       but for the kernel it doesn't matter since it doesn't link against
 *       any of the gcc libraries
 *
 * This gcc plugin helps generate a little bit of entropy from program state,
 * used throughout the uptime of the kernel. Here is an instrumentation example:
 *
 * before:
 * void __latent_entropy test(int argc, char *argv[])
 * {
 *	if (argc <= 1)
 *		printf("%s: no command arguments :(\n", *argv);
 *	else
 *		printf("%s: %d command arguments!\n", *argv, args - 1);
 * }
 *
 * after:
 * void __latent_entropy test(int argc, char *argv[])
 * {
 *	// latent_entropy_execute() 1.
 *	unsigned long local_entropy;
 *	// init_local_entropy() 1.
 *	void *local_entropy_frameaddr;
 *	// init_local_entropy() 3.
 *	unsigned long tmp_latent_entropy;
 *
 *	// init_local_entropy() 2.
 *	local_entropy_frameaddr = __builtin_frame_address(0);
 *	local_entropy = (unsigned long) local_entropy_frameaddr;
 *
 *	// init_local_entropy() 4.
 *	tmp_latent_entropy = latent_entropy;
 *	// init_local_entropy() 5.
 *	local_entropy ^= tmp_latent_entropy;
 *
 *	// latent_entropy_execute() 3.
 *	if (argc <= 1) {
 *		// perturb_local_entropy()
 *		local_entropy += 4623067384293424948;
 *		printf("%s: no command arguments :(\n", *argv);
 *		// perturb_local_entropy()
 *	} else {
 *		local_entropy ^= 3896280633962944730;
 *		printf("%s: %d command arguments!\n", *argv, args - 1);
 *	}
 *
 *	// latent_entropy_execute() 4.
 *	tmp_latent_entropy = rol(tmp_latent_entropy, local_entropy);
 *	latent_entropy = tmp_latent_entropy;
 * }
 *
 * TODO:
 * - add ipa pass to identify not explicitly marked candidate functions
 * - mix in more program state (function arguments/return values,
 *   loop variables, etc)
 * - more instrumentation control via attribute parameters
 *
 * BUGS:
 * - none known
 *
 * Options:
 * -fplugin-arg-latent_entropy_plugin-disable
 *
 * Attribute: __attribute__((latent_entropy))
 *  The latent_entropy gcc attribute can be only on functions and variables.
 *  If it is on a function then the plugin will instrument it. If the attribute
 *  is on a variable then the plugin will initialize it with a random value.
 *  The variable must be an integer, an integer array type or a structure
 *  with integer fields.
 */

#include "gcc-common.h"

__visible int plugin_is_GPL_compatible;

static GTY(()) tree latent_entropy_decl;

static struct plugin_info latent_entropy_plugin_info = {
	.version	= PLUGIN_VERSION,
	.help		= "disable\tturn off latent entropy instrumentation\n",
};

static unsigned HOST_WIDE_INT deterministic_seed;
static unsigned HOST_WIDE_INT rnd_buf[32];
static size_t rnd_idx = ARRAY_SIZE(rnd_buf);
static int urandom_fd = -1;

static unsigned HOST_WIDE_INT get_random_const(void)
{
	if (deterministic_seed) {
		unsigned HOST_WIDE_INT w = deterministic_seed;
		w ^= w << 13;
		w ^= w >> 7;
		w ^= w << 17;
		deterministic_seed = w;
		return deterministic_seed;
	}

	if (urandom_fd < 0) {
		urandom_fd = open("/dev/urandom", O_RDONLY);
		gcc_assert(urandom_fd >= 0);
	}
	if (rnd_idx >= ARRAY_SIZE(rnd_buf)) {
		gcc_assert(read(urandom_fd, rnd_buf, sizeof(rnd_buf)) == sizeof(rnd_buf));
		rnd_idx = 0;
	}
	return rnd_buf[rnd_idx++];
}

static tree tree_get_random_const(tree type)
{
	unsigned long long mask;

	mask = 1ULL << (TREE_INT_CST_LOW(TYPE_SIZE(type)) - 1);
	mask = 2 * (mask - 1) + 1;

	if (TYPE_UNSIGNED(type))
		return build_int_cstu(type, mask & get_random_const());
	return build_int_cst(type, mask & get_random_const());
}

static tree handle_latent_entropy_attribute(tree *node, tree name,
						tree args __unused,
						int flags __unused,
						bool *no_add_attrs)
{
	tree type;
	vec<constructor_elt, va_gc> *vals;

	switch (TREE_CODE(*node)) {
	default:
		*no_add_attrs = true;
		error("%qE attribute only applies to functions and variables",
			name);
		break;

	case VAR_DECL:
		if (DECL_INITIAL(*node)) {
			*no_add_attrs = true;
			error("variable %qD with %qE attribute must not be initialized",
				*node, name);
			break;
		}

		if (!TREE_STATIC(*node)) {
			*no_add_attrs = true;
			error("variable %qD with %qE attribute must not be local",
				*node, name);
			break;
		}

		type = TREE_TYPE(*node);
		switch (TREE_CODE(type)) {
		default:
			*no_add_attrs = true;
			error("variable %qD with %qE attribute must be an integer or a fixed length integer array type or a fixed sized structure with integer fields",
				*node, name);
			break;

		case RECORD_TYPE: {
			tree fld, lst = TYPE_FIELDS(type);
			unsigned int nelt = 0;

			for (fld = lst; fld; nelt++, fld = TREE_CHAIN(fld)) {
				tree fieldtype;

				fieldtype = TREE_TYPE(fld);
				if (TREE_CODE(fieldtype) == INTEGER_TYPE)
					continue;

				*no_add_attrs = true;
				error("structure variable %qD with %qE attribute has a non-integer field %qE",
					*node, name, fld);
				break;
			}

			if (fld)
				break;

			vec_alloc(vals, nelt);

			for (fld = lst; fld; fld = TREE_CHAIN(fld)) {
				tree random_const, fld_t = TREE_TYPE(fld);

				random_const = tree_get_random_const(fld_t);
				CONSTRUCTOR_APPEND_ELT(vals, fld, random_const);
			}

			/* Initialize the fields with random constants */
			DECL_INITIAL(*node) = build_constructor(type, vals);
			break;
		}

		/* Initialize the variable with a random constant */
		case INTEGER_TYPE:
			DECL_INITIAL(*node) = tree_get_random_const(type);
			break;

		case ARRAY_TYPE: {
			tree elt_type, array_size, elt_size;
			unsigned int i, nelt;
			HOST_WIDE_INT array_size_int, elt_size_int;

			elt_type = TREE_TYPE(type);
			elt_size = TYPE_SIZE_UNIT(TREE_TYPE(type));
			array_size = TYPE_SIZE_UNIT(type);

			if (TREE_CODE(elt_type) != INTEGER_TYPE || !array_size
				|| TREE_CODE(array_size) != INTEGER_CST) {
				*no_add_attrs = true;
				error("array variable %qD with %qE attribute must be a fixed length integer array type",
					*node, name);
				break;
			}

			array_size_int = TREE_INT_CST_LOW(array_size);
			elt_size_int = TREE_INT_CST_LOW(elt_size);
			nelt = array_size_int / elt_size_int;

			vec_alloc(vals, nelt);

			for (i = 0; i < nelt; i++) {
				tree cst = size_int(i);
				tree rand_cst = tree_get_random_const(elt_type);

				CONSTRUCTOR_APPEND_ELT(vals, cst, rand_cst);
			}

			/*
			 * Initialize the elements of the array with random
			 * constants
			 */
			DECL_INITIAL(*node) = build_constructor(type, vals);
			break;
		}
		}
		break;

	case FUNCTION_DECL:
		break;
	}

	return NULL_TREE;
}

static struct attribute_spec latent_entropy_attr = { };

static void register_attributes(void *event_data __unused, void *data __unused)
{
	latent_entropy_attr.name		= "latent_entropy";
	latent_entropy_attr.decl_required	= true;
	latent_entropy_attr.handler		= handle_latent_entropy_attribute;

	register_attribute(&latent_entropy_attr);
}

static bool latent_entropy_gate(void)
{
	tree list;

	/* don't bother with noreturn functions for now */
	if (TREE_THIS_VOLATILE(current_function_decl))
		return false;

	/* gcc-4.5 doesn't discover some trivial noreturn functions */
	if (EDGE_COUNT(EXIT_BLOCK_PTR_FOR_FN(cfun)->preds) == 0)
		return false;

	list = DECL_ATTRIBUTES(current_function_decl);
	return lookup_attribute("latent_entropy", list) != NULL_TREE;
}

static tree create_var(tree type, const char *name)
{
	tree var;

	var = create_tmp_var(type, name);
	add_referenced_var(var);
	mark_sym_for_renaming(var);
	return var;
}

/*
 * Set up the next operation and its constant operand to use in the latent
 * entropy PRNG. When RHS is specified, the request is for perturbing the
 * local latent entropy variable, otherwise it is for perturbing the global
 * latent entropy variable where the two operands are already given by the
 * local and global latent entropy variables themselves.
 *
 * The operation is one of add/xor/rol when instrumenting the local entropy
 * variable and one of add/xor when perturbing the global entropy variable.
 * Rotation is not used for the latter case because it would transmit less
 * entropy to the global variable than the other two operations.
 */
static enum tree_code get_op(tree *rhs)
{
	static enum tree_code op;
	unsigned HOST_WIDE_INT random_const;

	random_const = get_random_const();

	switch (op) {
	case BIT_XOR_EXPR:
		op = PLUS_EXPR;
		break;

	case PLUS_EXPR:
		if (rhs) {
			op = LROTATE_EXPR;
			/*
			 * This code limits the value of random_const to
			 * the size of a long for the rotation
			 */
			random_const %= TYPE_PRECISION(long_unsigned_type_node);
			break;
		}

	case LROTATE_EXPR:
	default:
		op = BIT_XOR_EXPR;
		break;
	}
	if (rhs)
		*rhs = build_int_cstu(long_unsigned_type_node, random_const);
	return op;
}

static gimple create_assign(enum tree_code code, tree lhs, tree op1,
				tree op2)
{
	return gimple_build_assign_with_ops(code, lhs, op1, op2);
}

static void perturb_local_entropy(basic_block bb, tree local_entropy)
{
	gimple_stmt_iterator gsi;
	gimple assign;
	tree rhs;
	enum tree_code op;

	op = get_op(&rhs);
	assign = create_assign(op, local_entropy, local_entropy, rhs);
	gsi = gsi_after_labels(bb);
	gsi_insert_before(&gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);
}

static void __perturb_latent_entropy(gimple_stmt_iterator *gsi,
					tree local_entropy)
{
	gimple assign;
	tree temp;
	enum tree_code op;

	/* 1. create temporary copy of latent_entropy */
	temp = create_var(long_unsigned_type_node, "temp_latent_entropy");

	/* 2. read... */
	add_referenced_var(latent_entropy_decl);
	mark_sym_for_renaming(latent_entropy_decl);
	assign = gimple_build_assign(temp, latent_entropy_decl);
	gsi_insert_before(gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);

	/* 3. ...modify... */
	op = get_op(NULL);
	assign = create_assign(op, temp, temp, local_entropy);
	gsi_insert_after(gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);

	/* 4. ...write latent_entropy */
	assign = gimple_build_assign(latent_entropy_decl, temp);
	gsi_insert_after(gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);
}

static bool handle_tail_calls(basic_block bb, tree local_entropy)
{
	gimple_stmt_iterator gsi;

	for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
		gcall *call;
		gimple stmt = gsi_stmt(gsi);

		if (!is_gimple_call(stmt))
			continue;

		call = as_a_gcall(stmt);
		if (!gimple_call_tail_p(call))
			continue;

		__perturb_latent_entropy(&gsi, local_entropy);
		return true;
	}

	return false;
}

static void perturb_latent_entropy(tree local_entropy)
{
	edge_iterator ei;
	edge e, last_bb_e;
	basic_block last_bb;

	gcc_assert(single_pred_p(EXIT_BLOCK_PTR_FOR_FN(cfun)));
	last_bb_e = single_pred_edge(EXIT_BLOCK_PTR_FOR_FN(cfun));

	FOR_EACH_EDGE(e, ei, last_bb_e->src->preds) {
		if (ENTRY_BLOCK_PTR_FOR_FN(cfun) == e->src)
			continue;
		if (EXIT_BLOCK_PTR_FOR_FN(cfun) == e->src)
			continue;

		handle_tail_calls(e->src, local_entropy);
	}

	last_bb = single_pred(EXIT_BLOCK_PTR_FOR_FN(cfun));
	if (!handle_tail_calls(last_bb, local_entropy)) {
		gimple_stmt_iterator gsi = gsi_last_bb(last_bb);

		__perturb_latent_entropy(&gsi, local_entropy);
	}
}

static void init_local_entropy(basic_block bb, tree local_entropy)
{
	gimple assign, call;
	tree frame_addr, rand_const, tmp, fndecl, udi_frame_addr;
	enum tree_code op;
	unsigned HOST_WIDE_INT rand_cst;
	gimple_stmt_iterator gsi = gsi_after_labels(bb);

	/* 1. create local_entropy_frameaddr */
	frame_addr = create_var(ptr_type_node, "local_entropy_frameaddr");

	/* 2. local_entropy_frameaddr = __builtin_frame_address() */
	fndecl = builtin_decl_implicit(BUILT_IN_FRAME_ADDRESS);
	call = gimple_build_call(fndecl, 1, integer_zero_node);
	gimple_call_set_lhs(call, frame_addr);
	gsi_insert_before(&gsi, call, GSI_NEW_STMT);
	update_stmt(call);

	udi_frame_addr = fold_convert(long_unsigned_type_node, frame_addr);
	assign = gimple_build_assign(local_entropy, udi_frame_addr);
	gsi_insert_after(&gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);

	/* 3. create temporary copy of latent_entropy */
	tmp = create_var(long_unsigned_type_node, "temp_latent_entropy");

	/* 4. read the global entropy variable into local entropy */
	add_referenced_var(latent_entropy_decl);
	mark_sym_for_renaming(latent_entropy_decl);
	assign = gimple_build_assign(tmp, latent_entropy_decl);
	gsi_insert_after(&gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);

	/* 5. mix local_entropy_frameaddr into local entropy */
	assign = create_assign(BIT_XOR_EXPR, local_entropy, local_entropy, tmp);
	gsi_insert_after(&gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);

	rand_cst = get_random_const();
	rand_const = build_int_cstu(long_unsigned_type_node, rand_cst);
	op = get_op(NULL);
	assign = create_assign(op, local_entropy, local_entropy, rand_const);
	gsi_insert_after(&gsi, assign, GSI_NEW_STMT);
	update_stmt(assign);
}

static bool create_latent_entropy_decl(void)
{
	varpool_node_ptr node;

	if (latent_entropy_decl != NULL_TREE)
		return true;

	FOR_EACH_VARIABLE(node) {
		tree name, var = NODE_DECL(node);

		if (DECL_NAME_LENGTH(var) < sizeof("latent_entropy") - 1)
			continue;

		name = DECL_NAME(var);
		if (strcmp(IDENTIFIER_POINTER(name), "latent_entropy"))
			continue;

		latent_entropy_decl = var;
		break;
	}

	return latent_entropy_decl != NULL_TREE;
}

static unsigned int latent_entropy_execute(void)
{
	basic_block bb;
	tree local_entropy;

	if (!create_latent_entropy_decl())
		return 0;

	/* prepare for step 2 below */
	gcc_assert(single_succ_p(ENTRY_BLOCK_PTR_FOR_FN(cfun)));
	bb = single_succ(ENTRY_BLOCK_PTR_FOR_FN(cfun));
	if (!single_pred_p(bb)) {
		split_edge(single_succ_edge(ENTRY_BLOCK_PTR_FOR_FN(cfun)));
		gcc_assert(single_succ_p(ENTRY_BLOCK_PTR_FOR_FN(cfun)));
		bb = single_succ(ENTRY_BLOCK_PTR_FOR_FN(cfun));
	}

	/* 1. create the local entropy variable */
	local_entropy = create_var(long_unsigned_type_node, "local_entropy");

	/* 2. initialize the local entropy variable */
	init_local_entropy(bb, local_entropy);

	bb = bb->next_bb;

	/*
	 * 3. instrument each BB with an operation on the
	 *    local entropy variable
	 */
	while (bb != EXIT_BLOCK_PTR_FOR_FN(cfun)) {
		perturb_local_entropy(bb, local_entropy);
		bb = bb->next_bb;
	}

	/* 4. mix local entropy into the global entropy variable */
	perturb_latent_entropy(local_entropy);
	return 0;
}

static void latent_entropy_start_unit(void *gcc_data __unused,
					void *user_data __unused)
{
	tree type, id;
	int quals;

	if (in_lto_p)
		return;

	/* extern volatile unsigned long latent_entropy */
	quals = TYPE_QUALS(long_unsigned_type_node) | TYPE_QUAL_VOLATILE;
	type = build_qualified_type(long_unsigned_type_node, quals);
	id = get_identifier("latent_entropy");
	latent_entropy_decl = build_decl(UNKNOWN_LOCATION, VAR_DECL, id, type);

	TREE_STATIC(latent_entropy_decl) = 1;
	TREE_PUBLIC(latent_entropy_decl) = 1;
	TREE_USED(latent_entropy_decl) = 1;
	DECL_PRESERVE_P(latent_entropy_decl) = 1;
	TREE_THIS_VOLATILE(latent_entropy_decl) = 1;
	DECL_EXTERNAL(latent_entropy_decl) = 1;
	DECL_ARTIFICIAL(latent_entropy_decl) = 1;
	lang_hooks.decls.pushdecl(latent_entropy_decl);
}

#define PASS_NAME latent_entropy
#define PROPERTIES_REQUIRED PROP_gimple_leh | PROP_cfg
#define TODO_FLAGS_FINISH TODO_verify_ssa | TODO_verify_stmts | TODO_dump_func \
	| TODO_update_ssa
#include "gcc-generate-gimple-pass.h"

__visible int plugin_init(struct plugin_name_args *plugin_info,
			  struct plugin_gcc_version *version)
{
	bool enabled = true;
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument * const argv = plugin_info->argv;
	int i;

	/*
	 * Call get_random_seed() with noinit=true, so that this returns
	 * 0 in the case where no seed has been passed via -frandom-seed.
	 */
	deterministic_seed = get_random_seed(true);

	static const struct ggc_root_tab gt_ggc_r_gt_latent_entropy[] = {
		{
			.base = &latent_entropy_decl,
			.nelt = 1,
			.stride = sizeof(latent_entropy_decl),
			.cb = &gt_ggc_mx_tree_node,
			.pchw = &gt_pch_nx_tree_node
		},
		LAST_GGC_ROOT_TAB
	};

	PASS_INFO(latent_entropy, "optimized", 1, PASS_POS_INSERT_BEFORE);

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("incompatible gcc/plugin versions"));
		return 1;
	}

	for (i = 0; i < argc; ++i) {
		if (!(strcmp(argv[i].key, "disable"))) {
			enabled = false;
			continue;
		}
		error(G_("unknown option '-fplugin-arg-%s-%s'"), plugin_name, argv[i].key);
	}

	register_callback(plugin_name, PLUGIN_INFO, NULL,
				&latent_entropy_plugin_info);
	if (enabled) {
		register_callback(plugin_name, PLUGIN_START_UNIT,
					&latent_entropy_start_unit, NULL);
		register_callback(plugin_name, PLUGIN_REGISTER_GGC_ROOTS,
				  NULL, (void *)&gt_ggc_r_gt_latent_entropy);
		register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL,
					&latent_entropy_pass_info);
	}
	register_callback(plugin_name, PLUGIN_ATTRIBUTES, register_attributes,
				NULL);

	return 0;
}
