#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

#undef unix
struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
 .name = __stringify(KBUILD_MODNAME),
 .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
 .exit = cleanup_module,
#endif
};

static const struct modversion_info ____versions[]
__attribute_used__
__attribute__((section("__versions"))) = {
	{ 0x316962fc, "struct_module" },
	{ 0x5a34a45c, "__kmalloc" },
	{ 0x724beef2, "malloc_sizes" },
	{ 0x3fa03a97, "memset" },
	{ 0xc16fe12d, "__memcpy" },
	{ 0xdd132261, "printk" },
	{ 0x859204af, "sscanf" },
	{ 0x3656bf5a, "lock_kernel" },
	{ 0x1e6d26a8, "strstr" },
	{ 0x41ede9df, "lm_register_proto" },
	{ 0xb1f975aa, "unlock_kernel" },
	{ 0x87b0b01f, "posix_lock_file_wait" },
	{ 0x75f29cfd, "kmem_cache_alloc" },
	{ 0x69384280, "lm_unregister_proto" },
	{ 0x37a0cba, "kfree" },
	{ 0x5d16bfe6, "posix_test_lock" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=gfs2";


MODULE_INFO(srcversion, "123E446F965A386A0C017C4");
