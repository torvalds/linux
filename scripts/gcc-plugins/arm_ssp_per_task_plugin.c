// SPDX-License-Identifier: GPL-2.0

#include "gcc-common.h"

__visible int plugin_is_GPL_compatible;

static unsigned int sp_mask, canary_offset;

static unsigned int arm_pertask_ssp_rtl_execute(void)
{
	rtx_insn *insn;

	for (insn = get_insns(); insn; insn = NEXT_INSN(insn)) {
		const char *sym;
		rtx body;
		rtx mask, masked_sp;

		/*
		 * Find a SET insn involving a SYMBOL_REF to __stack_chk_guard
		 */
		if (!INSN_P(insn))
			continue;
		body = PATTERN(insn);
		if (GET_CODE(body) != SET ||
		    GET_CODE(SET_SRC(body)) != SYMBOL_REF)
			continue;
		sym = XSTR(SET_SRC(body), 0);
		if (strcmp(sym, "__stack_chk_guard"))
			continue;

		/*
		 * Replace the source of the SET insn with an expression that
		 * produces the address of the copy of the stack canary value
		 * stored in struct thread_info
		 */
		mask = GEN_INT(sext_hwi(sp_mask, GET_MODE_PRECISION(Pmode)));
		masked_sp = gen_reg_rtx(Pmode);

		emit_insn_before(gen_rtx_set(masked_sp,
					     gen_rtx_AND(Pmode,
							 stack_pointer_rtx,
							 mask)),
				 insn);

		SET_SRC(body) = gen_rtx_PLUS(Pmode, masked_sp,
					     GEN_INT(canary_offset));
	}
	return 0;
}

#define PASS_NAME arm_pertask_ssp_rtl

#define NO_GATE
#include "gcc-generate-rtl-pass.h"

#if BUILDING_GCC_VERSION >= 9000
static bool no(void)
{
	return false;
}

static void arm_pertask_ssp_start_unit(void *gcc_data, void *user_data)
{
	targetm.have_stack_protect_combined_set = no;
	targetm.have_stack_protect_combined_test = no;
}
#endif

__visible int plugin_init(struct plugin_name_args *plugin_info,
			  struct plugin_gcc_version *version)
{
	const char * const plugin_name = plugin_info->base_name;
	const int argc = plugin_info->argc;
	const struct plugin_argument *argv = plugin_info->argv;
	int tso = 0;
	int i;

	if (!plugin_default_version_check(version, &gcc_version)) {
		error(G_("incompatible gcc/plugin versions"));
		return 1;
	}

	for (i = 0; i < argc; ++i) {
		if (!strcmp(argv[i].key, "disable"))
			return 0;

		/* all remaining options require a value */
		if (!argv[i].value) {
			error(G_("no value supplied for option '-fplugin-arg-%s-%s'"),
			      plugin_name, argv[i].key);
			return 1;
		}

		if (!strcmp(argv[i].key, "tso")) {
			tso = atoi(argv[i].value);
			continue;
		}

		if (!strcmp(argv[i].key, "offset")) {
			canary_offset = atoi(argv[i].value);
			continue;
		}
		error(G_("unknown option '-fplugin-arg-%s-%s'"),
		      plugin_name, argv[i].key);
		return 1;
	}

	/* create the mask that produces the base of the stack */
	sp_mask = ~((1U << (12 + tso)) - 1);

	PASS_INFO(arm_pertask_ssp_rtl, "expand", 1, PASS_POS_INSERT_AFTER);

	register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP,
			  NULL, &arm_pertask_ssp_rtl_pass_info);

#if BUILDING_GCC_VERSION >= 9000
	register_callback(plugin_info->base_name, PLUGIN_START_UNIT,
			  arm_pertask_ssp_start_unit, NULL);
#endif

	return 0;
}
