#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x28950ef1, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x4c4fef19, __VMLINUX_SYMBOL_STR(kernel_stack) },
	{ 0x754d539c, __VMLINUX_SYMBOL_STR(strlen) },
	{ 0xb342d16a, __VMLINUX_SYMBOL_STR(spl_kmem_alloc) },
	{ 0x167e7f9d, __VMLINUX_SYMBOL_STR(__get_user_1) },
	{ 0xe2d5255a, __VMLINUX_SYMBOL_STR(strcmp) },
	{ 0x71de9b3f, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0xee8843fa, __VMLINUX_SYMBOL_STR(nvpair_value_uint64) },
	{ 0x11089ac7, __VMLINUX_SYMBOL_STR(_ctype) },
	{ 0x5a921311, __VMLINUX_SYMBOL_STR(strncmp) },
	{ 0xe3a53f4c, __VMLINUX_SYMBOL_STR(sort) },
	{ 0xd42a96fa, __VMLINUX_SYMBOL_STR(nvpair_name) },
	{ 0x1bfac311, __VMLINUX_SYMBOL_STR(nvlist_lookup_nvlist) },
	{ 0x5d6e0bba, __VMLINUX_SYMBOL_STR(nvlist_lookup_uint64) },
	{ 0xf0fdf6cb, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0xd0920999, __VMLINUX_SYMBOL_STR(nvpair_value_uint32) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0x29c88b11, __VMLINUX_SYMBOL_STR(nvlist_next_nvpair) },
	{ 0x82027a4c, __VMLINUX_SYMBOL_STR(cmn_err) },
	{ 0x28318305, __VMLINUX_SYMBOL_STR(snprintf) },
	{ 0xec1cce40, __VMLINUX_SYMBOL_STR(nvlist_lookup_nvlist_array) },
	{ 0xb0e602eb, __VMLINUX_SYMBOL_STR(memmove) },
	{ 0x6d16801a, __VMLINUX_SYMBOL_STR(spl_kmem_free) },
	{ 0x77e2f33, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0xa66a6969, __VMLINUX_SYMBOL_STR(nvpair_value_nvlist) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=spl,znvpair";


MODULE_INFO(srcversion, "D94B05FC2B3769899B59647");
MODULE_INFO(rhelversion, "7.3");
