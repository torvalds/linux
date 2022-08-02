// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

const volatile struct {
	/* thread to activate trace programs for */
	pid_t tgid;
	/* return error from __init function */
	int inject_error;
	/* uffd monitored range start address */
	void *fault_addr;
} bpf_mod_race_config = { -1 };

int bpf_blocking = 0;
int res_try_get_module = -1;

static __always_inline bool check_thread_id(void)
{
	struct task_struct *task = bpf_get_current_task_btf();

	return task->tgid == bpf_mod_race_config.tgid;
}

/* The trace of execution is something like this:
 *
 * finit_module()
 *   load_module()
 *     prepare_coming_module()
 *       notifier_call(MODULE_STATE_COMING)
 *         btf_parse_module()
 *         btf_alloc_id()		// Visible to userspace at this point
 *         list_add(btf_mod->list, &btf_modules)
 *     do_init_module()
 *       freeinit = kmalloc()
 *       ret = mod->init()
 *         bpf_prog_widen_race()
 *           bpf_copy_from_user()
 *             ...<sleep>...
 *       if (ret < 0)
 *         ...
 *         free_module()
 * return ret
 *
 * At this point, module loading thread is blocked, we now load the program:
 *
 * bpf_check
 *   add_kfunc_call/check_pseudo_btf_id
 *     btf_try_get_module
 *       try_get_module_live == false
 *     return -ENXIO
 *
 * Without the fix (try_get_module_live in btf_try_get_module):
 *
 * bpf_check
 *   add_kfunc_call/check_pseudo_btf_id
 *     btf_try_get_module
 *       try_get_module == true
 *     <store module reference in btf_kfunc_tab or used_btf array>
 *   ...
 * return fd
 *
 * Now, if we inject an error in the blocked program, our module will be freed
 * (going straight from MODULE_STATE_COMING to MODULE_STATE_GOING).
 * Later, when bpf program is freed, it will try to module_put already freed
 * module. This is why try_get_module_live returns false if mod->state is not
 * MODULE_STATE_LIVE.
 */

SEC("fmod_ret.s/bpf_fentry_test1")
int BPF_PROG(widen_race, int a, int ret)
{
	char dst;

	if (!check_thread_id())
		return 0;
	/* Indicate that we will attempt to block */
	bpf_blocking = 1;
	bpf_copy_from_user(&dst, 1, bpf_mod_race_config.fault_addr);
	return bpf_mod_race_config.inject_error;
}

SEC("fexit/do_init_module")
int BPF_PROG(fexit_init_module, struct module *mod, int ret)
{
	if (!check_thread_id())
		return 0;
	/* Indicate that we finished blocking */
	bpf_blocking = 2;
	return 0;
}

SEC("fexit/btf_try_get_module")
int BPF_PROG(fexit_module_get, const struct btf *btf, struct module *mod)
{
	res_try_get_module = !!mod;
	return 0;
}

char _license[] SEC("license") = "GPL";
