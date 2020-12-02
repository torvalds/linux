/*
 * Copyright 2011-2016 by Emese Revfy <re.emese@gmail.com>
 * Licensed under the GPL v2, or (at your option) v3
 *
 * Homepage:
 * https://github.com/ephox-gcc-plugins/sancov
 *
 * This plugin inserts a __sanitizer_cov_trace_pc() call at the start of basic blocks.
 * It supports all gcc versions with plugin support (from gcc-4.5 on).
 * It is based on the commit "Add fuzzing coverage support" by Dmitry Vyukov <dvyukov@google.com>.
 *
 * You can read about it more here:
 *  https://gcc.gnu.org/viewcvs/gcc?limit_changes=0&view=revision&revision=231296
 *  https://lwn.net/Articles/674854/
 *  https://github.com/google/syzkaller
 *  https://lwn.net/Articles/677764/
 *
 * Usage:
 * make run
 */

#include "gcc-common.h"

__visible int plugin_is_GPL_compatible;

tree sancov_fndecl;

static struct plugin_info sancov_plugin_info = {
	.version	= "20160402",
	.help		= "sancov plugin\n",
};

static unsigned int sancov_execute(void)
{
	basic_block bb;

	/* Remove this line when this plugin and kcov will be in the kernel.
	if (!strcmp(DECL_NAME_POINTER(current_function_decl), DECL_NAME_POINTER(sancov_fndecl)))
		return 0;
	*/

	FOR_EACH_BB_FN(bb, cfun) {
		const_gimple stmt;
		gcall *gcall;
		gimple_stmt_iterator gsi = gsi_after_labels(bb);

		if (gsi_end_p(gsi))
			continue;

		stmt = gsi_stmt(gsi);
		gcall = as_a_gcall(gimple_build_call(sancov_fndecl, 0));
		gimple_set_location(gcall, gimple_location(stmt));
		gsi_insert_before(&gsi, gcall, GSI_SAME_STMT);
	}
	return 0;
}

#define PASS_NAME sancov

#define NO_GATE
#define TODO_FLAGS_FINISH TODO_dump_func | TODO_verify_stmts | TODO_update_ssa_no_phi | TODO_verify_flow

#include "gcc-generate-gimple-pass.h"

static void sancov_start_unit(void __unused *gcc_data, void __unused *user_data)
{
	tree leaf_attr, nothrow_attr;
	tree BT_FN_VOID = build_function_type_list(void_type_node, NULL_TREE);

	sancov_fndecl = build_fn_decl("__sanitizer_cov_trace_pc", BT_FN_VOID);

	DECL_ASSEMBLER_NAME(sancov_fndecl);
	TREE_PUBLIC(sancov_fndecl) = 1;
	DECL_EXTERNAL(sancov_fndecl) = 1;
	DECL_ARTIFICIAL(sancov_fndecl) = 1;
	DECL_PRESERVE_P(sancov_fndecl) = 1;
	DECL_UNINLINABLE(sancov_fndecl) = 1;
	TREE_USED(sancov_fndecl) = 1;

	nothrow_attr = tree_cons(get_identifier("nothrow"), NULL, NULL);
	decl_attributes(&sancov_fndecl, nothrow_attr, 0);
	gcc_assert(TREE_NOTHROW(sancov_fndecl));
	leaf_attr = tree_cons(get_identifier("leaf"), NULL, NULL);
	decl_attributes(&sancov_fndecl, leaf_attr, 0);
}

__visible int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version)
{
	int i;
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument * const argv = plugin_info->argv;
	bool enable = true;

	static const struct ggc_root_tab gt_ggc_r_gt_sancov[] = {
		{
			.base = &sancov_fndecl,
			.nelt = 1,
			.stride = sizeof(sancov_fndecl),
			.cb = &gt_ggc_mx_tree_node,
			.pchw = &gt_pch_nx_tree_node
		},
		LAST_GGC_ROOT_TAB
	};

	/* BBs can be split afterwards?? */
	PASS_INFO(sancov, "asan", 0, PASS_POS_INSERT_BEFORE);

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("incompatible gcc/plugin versions"));
		return 1;
	}

	for (i = 0; i < argc; ++i) {
		if (!strcmp(argv[i].key, "no-sancov")) {
			enable = false;
			continue;
		}
		error(G_("unknown option '-fplugin-arg-%s-%s'"), plugin_name, argv[i].key);
	}

	register_callback(plugin_name, PLUGIN_INFO, NULL, &sancov_plugin_info);

	if (!enable)
		return 0;

#if BUILDING_GCC_VERSION < 6000
	register_callback(plugin_name, PLUGIN_START_UNIT, &sancov_start_unit, NULL);
	register_callback(plugin_name, PLUGIN_REGISTER_GGC_ROOTS, NULL, (void *)&gt_ggc_r_gt_sancov);
	register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &sancov_pass_info);
#endif

	return 0;
}
