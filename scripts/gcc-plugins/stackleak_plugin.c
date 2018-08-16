/*
 * Copyright 2011-2017 by the PaX Team <pageexec@freemail.hu>
 * Modified by Alexander Popov <alex.popov@linux.com>
 * Licensed under the GPL v2
 *
 * Note: the choice of the license means that the compilation process is
 * NOT 'eligible' as defined by gcc's library exception to the GPL v3,
 * but for the kernel it doesn't matter since it doesn't link against
 * any of the gcc libraries
 *
 * This gcc plugin is needed for tracking the lowest border of the kernel stack.
 * It instruments the kernel code inserting stackleak_track_stack() calls:
 *  - after alloca();
 *  - for the functions with a stack frame size greater than or equal
 *     to the "track-min-size" plugin parameter.
 *
 * This plugin is ported from grsecurity/PaX. For more information see:
 *   https://grsecurity.net/
 *   https://pax.grsecurity.net/
 *
 * Debugging:
 *  - use fprintf() to stderr, debug_generic_expr(), debug_gimple_stmt(),
 *     print_rtl() and print_simple_rtl();
 *  - add "-fdump-tree-all -fdump-rtl-all" to the plugin CFLAGS in
 *     Makefile.gcc-plugins to see the verbose dumps of the gcc passes;
 *  - use gcc -E to understand the preprocessing shenanigans;
 *  - use gcc with enabled CFG/GIMPLE/SSA verification (--enable-checking).
 */

#include "gcc-common.h"

__visible int plugin_is_GPL_compatible;

static int track_frame_size = -1;
static const char track_function[] = "stackleak_track_stack";

/*
 * Mark these global variables (roots) for gcc garbage collector since
 * they point to the garbage-collected memory.
 */
static GTY(()) tree track_function_decl;

static struct plugin_info stackleak_plugin_info = {
	.version = "201707101337",
	.help = "track-min-size=nn\ttrack stack for functions with a stack frame size >= nn bytes\n"
		"disable\t\tdo not activate the plugin\n"
};

static void stackleak_add_track_stack(gimple_stmt_iterator *gsi, bool after)
{
	gimple stmt;
	gcall *stackleak_track_stack;
	cgraph_node_ptr node;
	int frequency;
	basic_block bb;

	/* Insert call to void stackleak_track_stack(void) */
	stmt = gimple_build_call(track_function_decl, 0);
	stackleak_track_stack = as_a_gcall(stmt);
	if (after) {
		gsi_insert_after(gsi, stackleak_track_stack,
						GSI_CONTINUE_LINKING);
	} else {
		gsi_insert_before(gsi, stackleak_track_stack, GSI_SAME_STMT);
	}

	/* Update the cgraph */
	bb = gimple_bb(stackleak_track_stack);
	node = cgraph_get_create_node(track_function_decl);
	gcc_assert(node);
	frequency = compute_call_stmt_bb_frequency(current_function_decl, bb);
	cgraph_create_edge(cgraph_get_node(current_function_decl), node,
			stackleak_track_stack, bb->count, frequency);
}

static bool is_alloca(gimple stmt)
{
	if (gimple_call_builtin_p(stmt, BUILT_IN_ALLOCA))
		return true;

#if BUILDING_GCC_VERSION >= 4007
	if (gimple_call_builtin_p(stmt, BUILT_IN_ALLOCA_WITH_ALIGN))
		return true;
#endif

	return false;
}

/*
 * Work with the GIMPLE representation of the code. Insert the
 * stackleak_track_stack() call after alloca() and into the beginning
 * of the function if it is not instrumented.
 */
static unsigned int stackleak_instrument_execute(void)
{
	basic_block bb, entry_bb;
	bool prologue_instrumented = false, is_leaf = true;
	gimple_stmt_iterator gsi;

	/*
	 * ENTRY_BLOCK_PTR is a basic block which represents possible entry
	 * point of a function. This block does not contain any code and
	 * has a CFG edge to its successor.
	 */
	gcc_assert(single_succ_p(ENTRY_BLOCK_PTR_FOR_FN(cfun)));
	entry_bb = single_succ(ENTRY_BLOCK_PTR_FOR_FN(cfun));

	/*
	 * Loop through the GIMPLE statements in each of cfun basic blocks.
	 * cfun is a global variable which represents the function that is
	 * currently processed.
	 */
	FOR_EACH_BB_FN(bb, cfun) {
		for (gsi = gsi_start_bb(bb); !gsi_end_p(gsi); gsi_next(&gsi)) {
			gimple stmt;

			stmt = gsi_stmt(gsi);

			/* Leaf function is a function which makes no calls */
			if (is_gimple_call(stmt))
				is_leaf = false;

			if (!is_alloca(stmt))
				continue;

			/* Insert stackleak_track_stack() call after alloca() */
			stackleak_add_track_stack(&gsi, true);
			if (bb == entry_bb)
				prologue_instrumented = true;
		}
	}

	if (prologue_instrumented)
		return 0;

	/*
	 * Special cases to skip the instrumentation.
	 *
	 * Taking the address of static inline functions materializes them,
	 * but we mustn't instrument some of them as the resulting stack
	 * alignment required by the function call ABI will break other
	 * assumptions regarding the expected (but not otherwise enforced)
	 * register clobbering ABI.
	 *
	 * Case in point: native_save_fl on amd64 when optimized for size
	 * clobbers rdx if it were instrumented here.
	 *
	 * TODO: any more special cases?
	 */
	if (is_leaf &&
	    !TREE_PUBLIC(current_function_decl) &&
	    DECL_DECLARED_INLINE_P(current_function_decl)) {
		return 0;
	}

	if (is_leaf &&
	    !strncmp(IDENTIFIER_POINTER(DECL_NAME(current_function_decl)),
		     "_paravirt_", 10)) {
		return 0;
	}

	/* Insert stackleak_track_stack() call at the function beginning */
	bb = entry_bb;
	if (!single_pred_p(bb)) {
		/* gcc_assert(bb_loop_depth(bb) ||
				(bb->flags & BB_IRREDUCIBLE_LOOP)); */
		split_edge(single_succ_edge(ENTRY_BLOCK_PTR_FOR_FN(cfun)));
		gcc_assert(single_succ_p(ENTRY_BLOCK_PTR_FOR_FN(cfun)));
		bb = single_succ(ENTRY_BLOCK_PTR_FOR_FN(cfun));
	}
	gsi = gsi_after_labels(bb);
	stackleak_add_track_stack(&gsi, false);

	return 0;
}

static bool large_stack_frame(void)
{
#if BUILDING_GCC_VERSION >= 8000
	return maybe_ge(get_frame_size(), track_frame_size);
#else
	return (get_frame_size() >= track_frame_size);
#endif
}

/*
 * Work with the RTL representation of the code.
 * Remove the unneeded stackleak_track_stack() calls from the functions
 * which don't call alloca() and don't have a large enough stack frame size.
 */
static unsigned int stackleak_cleanup_execute(void)
{
	rtx_insn *insn, *next;

	if (cfun->calls_alloca)
		return 0;

	if (large_stack_frame())
		return 0;

	/*
	 * Find stackleak_track_stack() calls. Loop through the chain of insns,
	 * which is an RTL representation of the code for a function.
	 *
	 * The example of a matching insn:
	 *  (call_insn 8 4 10 2 (call (mem (symbol_ref ("stackleak_track_stack")
	 *  [flags 0x41] <function_decl 0x7f7cd3302a80 stackleak_track_stack>)
	 *  [0 stackleak_track_stack S1 A8]) (0)) 675 {*call} (expr_list
	 *  (symbol_ref ("stackleak_track_stack") [flags 0x41] <function_decl
	 *  0x7f7cd3302a80 stackleak_track_stack>) (expr_list (0) (nil))) (nil))
	 */
	for (insn = get_insns(); insn; insn = next) {
		rtx body;

		next = NEXT_INSN(insn);

		/* Check the expression code of the insn */
		if (!CALL_P(insn))
			continue;

		/*
		 * Check the expression code of the insn body, which is an RTL
		 * Expression (RTX) describing the side effect performed by
		 * that insn.
		 */
		body = PATTERN(insn);

		if (GET_CODE(body) == PARALLEL)
			body = XVECEXP(body, 0, 0);

		if (GET_CODE(body) != CALL)
			continue;

		/*
		 * Check the first operand of the call expression. It should
		 * be a mem RTX describing the needed subroutine with a
		 * symbol_ref RTX.
		 */
		body = XEXP(body, 0);
		if (GET_CODE(body) != MEM)
			continue;

		body = XEXP(body, 0);
		if (GET_CODE(body) != SYMBOL_REF)
			continue;

		if (SYMBOL_REF_DECL(body) != track_function_decl)
			continue;

		/* Delete the stackleak_track_stack() call */
		delete_insn_and_edges(insn);
#if BUILDING_GCC_VERSION >= 4007 && BUILDING_GCC_VERSION < 8000
		if (GET_CODE(next) == NOTE &&
		    NOTE_KIND(next) == NOTE_INSN_CALL_ARG_LOCATION) {
			insn = next;
			next = NEXT_INSN(insn);
			delete_insn_and_edges(insn);
		}
#endif
	}

	return 0;
}

static bool stackleak_gate(void)
{
	tree section;

	section = lookup_attribute("section",
				   DECL_ATTRIBUTES(current_function_decl));
	if (section && TREE_VALUE(section)) {
		section = TREE_VALUE(TREE_VALUE(section));

		if (!strncmp(TREE_STRING_POINTER(section), ".init.text", 10))
			return false;
		if (!strncmp(TREE_STRING_POINTER(section), ".devinit.text", 13))
			return false;
		if (!strncmp(TREE_STRING_POINTER(section), ".cpuinit.text", 13))
			return false;
		if (!strncmp(TREE_STRING_POINTER(section), ".meminit.text", 13))
			return false;
	}

	return track_frame_size >= 0;
}

/* Build the function declaration for stackleak_track_stack() */
static void stackleak_start_unit(void *gcc_data __unused,
				 void *user_data __unused)
{
	tree fntype;

	/* void stackleak_track_stack(void) */
	fntype = build_function_type_list(void_type_node, NULL_TREE);
	track_function_decl = build_fn_decl(track_function, fntype);
	DECL_ASSEMBLER_NAME(track_function_decl); /* for LTO */
	TREE_PUBLIC(track_function_decl) = 1;
	TREE_USED(track_function_decl) = 1;
	DECL_EXTERNAL(track_function_decl) = 1;
	DECL_ARTIFICIAL(track_function_decl) = 1;
	DECL_PRESERVE_P(track_function_decl) = 1;
}

/*
 * Pass gate function is a predicate function that gets executed before the
 * corresponding pass. If the return value is 'true' the pass gets executed,
 * otherwise, it is skipped.
 */
static bool stackleak_instrument_gate(void)
{
	return stackleak_gate();
}

#define PASS_NAME stackleak_instrument
#define PROPERTIES_REQUIRED PROP_gimple_leh | PROP_cfg
#define TODO_FLAGS_START TODO_verify_ssa | TODO_verify_flow | TODO_verify_stmts
#define TODO_FLAGS_FINISH TODO_verify_ssa | TODO_verify_stmts | TODO_dump_func \
			| TODO_update_ssa | TODO_rebuild_cgraph_edges
#include "gcc-generate-gimple-pass.h"

static bool stackleak_cleanup_gate(void)
{
	return stackleak_gate();
}

#define PASS_NAME stackleak_cleanup
#define TODO_FLAGS_FINISH TODO_dump_func
#include "gcc-generate-rtl-pass.h"

/*
 * Every gcc plugin exports a plugin_init() function that is called right
 * after the plugin is loaded. This function is responsible for registering
 * the plugin callbacks and doing other required initialization.
 */
__visible int plugin_init(struct plugin_name_args *plugin_info,
			  struct plugin_gcc_version *version)
{
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument * const argv = plugin_info->argv;
	int i = 0;

	/* Extra GGC root tables describing our GTY-ed data */
	static const struct ggc_root_tab gt_ggc_r_gt_stackleak[] = {
		{
			.base = &track_function_decl,
			.nelt = 1,
			.stride = sizeof(track_function_decl),
			.cb = &gt_ggc_mx_tree_node,
			.pchw = &gt_pch_nx_tree_node
		},
		LAST_GGC_ROOT_TAB
	};

	/*
	 * The stackleak_instrument pass should be executed before the
	 * "optimized" pass, which is the control flow graph cleanup that is
	 * performed just before expanding gcc trees to the RTL. In former
	 * versions of the plugin this new pass was inserted before the
	 * "tree_profile" pass, which is currently called "profile".
	 */
	PASS_INFO(stackleak_instrument, "optimized", 1,
						PASS_POS_INSERT_BEFORE);

	/*
	 * The stackleak_cleanup pass should be executed after the
	 * "reload" pass, when the stack frame size is final.
	 */
	PASS_INFO(stackleak_cleanup, "reload", 1, PASS_POS_INSERT_AFTER);

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("incompatible gcc/plugin versions"));
		return 1;
	}

	/* Parse the plugin arguments */
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i].key, "disable"))
			return 0;

		if (!strcmp(argv[i].key, "track-min-size")) {
			if (!argv[i].value) {
				error(G_("no value supplied for option '-fplugin-arg-%s-%s'"),
					plugin_name, argv[i].key);
				return 1;
			}

			track_frame_size = atoi(argv[i].value);
			if (track_frame_size < 0) {
				error(G_("invalid option argument '-fplugin-arg-%s-%s=%s'"),
					plugin_name, argv[i].key, argv[i].value);
				return 1;
			}
		} else {
			error(G_("unknown option '-fplugin-arg-%s-%s'"),
					plugin_name, argv[i].key);
			return 1;
		}
	}

	/* Give the information about the plugin */
	register_callback(plugin_name, PLUGIN_INFO, NULL,
						&stackleak_plugin_info);

	/* Register to be called before processing a translation unit */
	register_callback(plugin_name, PLUGIN_START_UNIT,
					&stackleak_start_unit, NULL);

	/* Register an extra GCC garbage collector (GGC) root table */
	register_callback(plugin_name, PLUGIN_REGISTER_GGC_ROOTS, NULL,
					(void *)&gt_ggc_r_gt_stackleak);

	/*
	 * Hook into the Pass Manager to register new gcc passes.
	 *
	 * The stack frame size info is available only at the last RTL pass,
	 * when it's too late to insert complex code like a function call.
	 * So we register two gcc passes to instrument every function at first
	 * and remove the unneeded instrumentation later.
	 */
	register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL,
					&stackleak_instrument_pass_info);
	register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL,
					&stackleak_cleanup_pass_info);

	return 0;
}
