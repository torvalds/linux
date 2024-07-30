// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module for testing dynamic_debug
 *
 * Authors:
 *      Jim Cromie	<jim.cromie@gmail.com>
 */

#define pr_fmt(fmt) "test_dd: " fmt

#include <linux/module.h>

/* run tests by reading or writing sysfs node: do_prints */

static void do_prints(void); /* device under test */
static int param_set_do_prints(const char *instr, const struct kernel_param *kp)
{
	do_prints();
	return 0;
}
static int param_get_do_prints(char *buffer, const struct kernel_param *kp)
{
	do_prints();
	return scnprintf(buffer, PAGE_SIZE, "did do_prints\n");
}
static const struct kernel_param_ops param_ops_do_prints = {
	.set = param_set_do_prints,
	.get = param_get_do_prints,
};
module_param_cb(do_prints, &param_ops_do_prints, NULL, 0600);

/*
 * Using the CLASSMAP api:
 * - classmaps must have corresponding enum
 * - enum symbols must match/correlate with class-name strings in the map.
 * - base must equal enum's 1st value
 * - multiple maps must set their base to share the 0-30 class_id space !!
 *   (build-bug-on tips welcome)
 * Additionally, here:
 * - tie together sysname, mapname, bitsname, flagsname
 */
#define DD_SYS_WRAP(_model, _flags)					\
	static unsigned long bits_##_model;				\
	static struct ddebug_class_param _flags##_model = {		\
		.bits = &bits_##_model,					\
		.flags = #_flags,					\
		.map = &map_##_model,					\
	};								\
	module_param_cb(_flags##_##_model, &param_ops_dyndbg_classes, &_flags##_model, 0600)

/* numeric input, independent bits */
enum cat_disjoint_bits {
	D2_CORE = 0,
	D2_DRIVER,
	D2_KMS,
	D2_PRIME,
	D2_ATOMIC,
	D2_VBL,
	D2_STATE,
	D2_LEASE,
	D2_DP,
	D2_DRMRES };
DECLARE_DYNDBG_CLASSMAP(map_disjoint_bits, DD_CLASS_TYPE_DISJOINT_BITS, 0,
			"D2_CORE",
			"D2_DRIVER",
			"D2_KMS",
			"D2_PRIME",
			"D2_ATOMIC",
			"D2_VBL",
			"D2_STATE",
			"D2_LEASE",
			"D2_DP",
			"D2_DRMRES");
DD_SYS_WRAP(disjoint_bits, p);
DD_SYS_WRAP(disjoint_bits, T);

/* symbolic input, independent bits */
enum cat_disjoint_names { LOW = 11, MID, HI };
DECLARE_DYNDBG_CLASSMAP(map_disjoint_names, DD_CLASS_TYPE_DISJOINT_NAMES, 10,
			"LOW", "MID", "HI");
DD_SYS_WRAP(disjoint_names, p);
DD_SYS_WRAP(disjoint_names, T);

/* numeric verbosity, V2 > V1 related */
enum cat_level_num { V0 = 14, V1, V2, V3, V4, V5, V6, V7 };
DECLARE_DYNDBG_CLASSMAP(map_level_num, DD_CLASS_TYPE_LEVEL_NUM, 14,
		       "V0", "V1", "V2", "V3", "V4", "V5", "V6", "V7");
DD_SYS_WRAP(level_num, p);
DD_SYS_WRAP(level_num, T);

/* symbolic verbosity */
enum cat_level_names { L0 = 22, L1, L2, L3, L4, L5, L6, L7 };
DECLARE_DYNDBG_CLASSMAP(map_level_names, DD_CLASS_TYPE_LEVEL_NAMES, 22,
			"L0", "L1", "L2", "L3", "L4", "L5", "L6", "L7");
DD_SYS_WRAP(level_names, p);
DD_SYS_WRAP(level_names, T);

/* stand-in for all pr_debug etc */
#define prdbg(SYM) __pr_debug_cls(SYM, #SYM " msg\n")

static void do_cats(void)
{
	pr_debug("doing categories\n");

	prdbg(LOW);
	prdbg(MID);
	prdbg(HI);

	prdbg(D2_CORE);
	prdbg(D2_DRIVER);
	prdbg(D2_KMS);
	prdbg(D2_PRIME);
	prdbg(D2_ATOMIC);
	prdbg(D2_VBL);
	prdbg(D2_STATE);
	prdbg(D2_LEASE);
	prdbg(D2_DP);
	prdbg(D2_DRMRES);
}

static void do_levels(void)
{
	pr_debug("doing levels\n");

	prdbg(V1);
	prdbg(V2);
	prdbg(V3);
	prdbg(V4);
	prdbg(V5);
	prdbg(V6);
	prdbg(V7);

	prdbg(L1);
	prdbg(L2);
	prdbg(L3);
	prdbg(L4);
	prdbg(L5);
	prdbg(L6);
	prdbg(L7);
}

static void do_prints(void)
{
	do_cats();
	do_levels();
}

static int __init test_dynamic_debug_init(void)
{
	pr_debug("init start\n");
	do_prints();
	pr_debug("init done\n");
	return 0;
}

static void __exit test_dynamic_debug_exit(void)
{
	pr_debug("exited\n");
}

module_init(test_dynamic_debug_init);
module_exit(test_dynamic_debug_exit);

MODULE_AUTHOR("Jim Cromie <jim.cromie@gmail.com>");
MODULE_DESCRIPTION("Kernel module for testing dynamic_debug");
MODULE_LICENSE("GPL");
