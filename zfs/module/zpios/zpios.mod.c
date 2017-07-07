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
	{ 0x6023ff19, __VMLINUX_SYMBOL_STR(dmu_tx_hold_free) },
	{ 0x8297790d, __VMLINUX_SYMBOL_STR(dmu_objset_create) },
	{ 0xe1a07d40, __VMLINUX_SYMBOL_STR(dmu_object_set_blocksize) },
	{ 0xda3e43d1, __VMLINUX_SYMBOL_STR(_raw_spin_unlock) },
	{ 0xee176c65, __VMLINUX_SYMBOL_STR(dmu_tx_abort) },
	{ 0xc8b57c27, __VMLINUX_SYMBOL_STR(autoremove_wake_function) },
	{ 0x79aa04a2, __VMLINUX_SYMBOL_STR(get_random_bytes) },
	{ 0x34184afe, __VMLINUX_SYMBOL_STR(current_kernel_time) },
	{ 0xb46da392, __VMLINUX_SYMBOL_STR(dmu_tx_wait) },
	{ 0xb342d16a, __VMLINUX_SYMBOL_STR(spl_kmem_alloc) },
	{ 0x4ed12f73, __VMLINUX_SYMBOL_STR(mutex_unlock) },
	{ 0xc35e4b4e, __VMLINUX_SYMBOL_STR(kthread_create_on_node) },
	{ 0xbc32eee7, __VMLINUX_SYMBOL_STR(spl_panic) },
	{ 0x35c2dc4c, __VMLINUX_SYMBOL_STR(dmu_tx_commit) },
	{ 0xf432dd3d, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0x71de9b3f, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0xf23b2e74, __VMLINUX_SYMBOL_STR(misc_register) },
	{ 0xfb578fc5, __VMLINUX_SYMBOL_STR(memset) },
	{ 0xb8c7ff88, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0x9a025cd5, __VMLINUX_SYMBOL_STR(__mutex_init) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x42f90a31, __VMLINUX_SYMBOL_STR(kthread_stop) },
	{ 0x9e68e189, __VMLINUX_SYMBOL_STR(dmu_objset_disown) },
	{ 0x9166fada, __VMLINUX_SYMBOL_STR(strncpy) },
	{ 0x9abdea30, __VMLINUX_SYMBOL_STR(mutex_lock) },
	{ 0x6c2c274e, __VMLINUX_SYMBOL_STR(spl_vmem_zalloc) },
	{ 0xc5fdef94, __VMLINUX_SYMBOL_STR(call_usermodehelper) },
	{ 0x96f73dc7, __VMLINUX_SYMBOL_STR(dmu_write) },
	{ 0x952664c5, __VMLINUX_SYMBOL_STR(do_exit) },
	{ 0x7da403ec, __VMLINUX_SYMBOL_STR(dsl_destroy_head) },
	{ 0x99d9f249, __VMLINUX_SYMBOL_STR(dmu_objset_own) },
	{ 0xc0fdaa0f, __VMLINUX_SYMBOL_STR(dmu_object_free) },
	{ 0xf0fdf6cb, __VMLINUX_SYMBOL_STR(__stack_chk_fail) },
	{ 0xd62c833f, __VMLINUX_SYMBOL_STR(schedule_timeout) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0x4b2a14f9, __VMLINUX_SYMBOL_STR(dmu_object_alloc) },
	{ 0xfe172afe, __VMLINUX_SYMBOL_STR(spl_vmem_alloc) },
	{ 0xe65cdceb, __VMLINUX_SYMBOL_STR(wake_up_process) },
	{ 0x23bd73ce, __VMLINUX_SYMBOL_STR(dmu_tx_create) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0xd52bf1ce, __VMLINUX_SYMBOL_STR(_raw_spin_lock) },
	{ 0xcf21d241, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x69acdf38, __VMLINUX_SYMBOL_STR(memcpy) },
	{ 0x5c8b5ce8, __VMLINUX_SYMBOL_STR(prepare_to_wait) },
	{ 0xfa66f77c, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x1acff3a1, __VMLINUX_SYMBOL_STR(dmu_tx_assign) },
	{ 0x28318305, __VMLINUX_SYMBOL_STR(snprintf) },
	{ 0x6d16801a, __VMLINUX_SYMBOL_STR(spl_kmem_free) },
	{ 0x5648b508, __VMLINUX_SYMBOL_STR(dmu_read) },
	{ 0xff749bc, __VMLINUX_SYMBOL_STR(spl_vmem_free) },
	{ 0x77e2f33, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0x2482e688, __VMLINUX_SYMBOL_STR(vsprintf) },
	{ 0xa1012e43, __VMLINUX_SYMBOL_STR(misc_deregister) },
	{ 0xd920669e, __VMLINUX_SYMBOL_STR(dmu_tx_hold_write) },
	{ 0x9b0325e8, __VMLINUX_SYMBOL_STR(spl_kmem_zalloc) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=zfs,spl";


MODULE_INFO(srcversion, "6224A8554C1154556C8A26B");
MODULE_INFO(rhelversion, "7.3");
