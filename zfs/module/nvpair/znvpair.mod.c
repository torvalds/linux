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
	{ 0x349cba85, __VMLINUX_SYMBOL_STR(strchr) },
	{ 0x754d539c, __VMLINUX_SYMBOL_STR(strlen) },
	{ 0xb342d16a, __VMLINUX_SYMBOL_STR(spl_kmem_alloc) },
	{ 0xbc32eee7, __VMLINUX_SYMBOL_STR(spl_panic) },
	{ 0xe2d5255a, __VMLINUX_SYMBOL_STR(strcmp) },
	{ 0xfb578fc5, __VMLINUX_SYMBOL_STR(memset) },
	{ 0x5a921311, __VMLINUX_SYMBOL_STR(strncmp) },
	{ 0xf0fdf6cb, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0xfe172afe, __VMLINUX_SYMBOL_STR(spl_vmem_alloc) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0x82fe53e1, __VMLINUX_SYMBOL_STR(ddi_strtol) },
	{ 0xddd4a3d3, __VMLINUX_SYMBOL_STR(xdrmem_create) },
	{ 0xb0e602eb, __VMLINUX_SYMBOL_STR(memmove) },
	{ 0x6d16801a, __VMLINUX_SYMBOL_STR(spl_kmem_free) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=spl";


MODULE_INFO(srcversion, "539CDAEA0E30550B4284AAA");
MODULE_INFO(rhelversion, "7.3");
