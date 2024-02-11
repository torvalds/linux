// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2011-2015 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/bpf_perf_event.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/kprobes.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/error-injection.h>
#include <linux/btf_ids.h>
#include <linux/bpf_lsm.h>
#include <linux/fprobe.h>
#include <linux/bsearch.h>
#include <linux/sort.h>
#include <linux/key.h>
#include <linux/verification.h>
#include <linux/namei.h>

#include <net/bpf_sk_storage.h>

#include <uapi/linux/bpf.h>
#include <uapi/linux/btf.h>

#include <asm/tlb.h>

#include "trace_probe.h"
#include "trace.h"

#define CREATE_TRACE_POINTS
#include "bpf_trace.h"

#define bpf_event_rcu_dereference(p)					\
	rcu_dereference_protected(p, lockdep_is_held(&bpf_event_mutex))

#define MAX_UPROBE_MULTI_CNT (1U << 20)
#define MAX_KPROBE_MULTI_CNT (1U << 20)

#ifdef CONFIG_MODULES
struct bpf_trace_module {
	struct module *module;
	struct list_head list;
};

static LIST_HEAD(bpf_trace_modules);
static DEFINE_MUTEX(bpf_module_mutex);

static struct bpf_raw_event_map *bpf_get_raw_tracepoint_module(const char *name)
{
	struct bpf_raw_event_map *btp, *ret = NULL;
	struct bpf_trace_module *btm;
	unsigned int i;

	mutex_lock(&bpf_module_mutex);
	list_for_each_entry(btm, &bpf_trace_modules, list) {
		for (i = 0; i < btm->module->num_bpf_raw_events; ++i) {
			btp = &btm->module->bpf_raw_events[i];
			if (!strcmp(btp->tp->name, name)) {
				if (try_module_get(btm->module))
					ret = btp;
				goto out;
			}
		}
	}
out:
	mutex_unlock(&bpf_module_mutex);
	return ret;
}
#else
static struct bpf_raw_event_map *bpf_get_raw_tracepoint_module(const char *name)
{
	return NULL;
}
#endif /* CONFIG_MODULES */

u64 bpf_get_stackid(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
u64 bpf_get_stack(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);

static int bpf_btf_printf_prepare(struct btf_ptr *ptr, u32 btf_ptr_size,
				  u64 flags, const struct btf **btf,
				  s32 *btf_id);
static u64 bpf_kprobe_multi_cookie(struct bpf_run_ctx *ctx);
static u64 bpf_kprobe_multi_entry_ip(struct bpf_run_ctx *ctx);

static u64 bpf_uprobe_multi_cookie(struct bpf_run_ctx *ctx);
static u64 bpf_uprobe_multi_entry_ip(struct bpf_run_ctx *ctx);

/**
 * trace_call_bpf - invoke BPF program
 * @call: tracepoint event
 * @ctx: opaque context pointer
 *
 * kprobe handlers execute BPF programs via this helper.
 * Can be used from static tracepoints in the future.
 *
 * Return: BPF programs always return an integer which is interpreted by
 * kprobe handler as:
 * 0 - return from kprobe (event is filtered out)
 * 1 - store kprobe event into ring buffer
 * Other values are reserved and currently alias to 1
 */
unsigned int trace_call_bpf(struct trace_event_call *call, void *ctx)
{
	unsigned int ret;

	cant_sleep();

	if (unlikely(__this_cpu_inc_return(bpf_prog_active) != 1)) {
		/*
		 * since some bpf program is already running on this cpu,
		 * don't call into another bpf program (same or different)
		 * and don't send kprobe event into ring-buffer,
		 * so return zero here
		 */
		ret = 0;
		goto out;
	}

	/*
	 * Instead of moving rcu_read_lock/rcu_dereference/rcu_read_unlock
	 * to all call sites, we did a bpf_prog_array_valid() there to check
	 * whether call->prog_array is empty or not, which is
	 * a heuristic to speed up execution.
	 *
	 * If bpf_prog_array_valid() fetched prog_array was
	 * non-NULL, we go into trace_call_bpf() and do the actual
	 * proper rcu_dereference() under RCU lock.
	 * If it turns out that prog_array is NULL then, we bail out.
	 * For the opposite, if the bpf_prog_array_valid() fetched pointer
	 * was NULL, you'll skip the prog_array with the risk of missing
	 * out of events when it was updated in between this and the
	 * rcu_dereference() which is accepted risk.
	 */
	rcu_read_lock();
	ret = bpf_prog_run_array(rcu_dereference(call->prog_array),
				 ctx, bpf_prog_run);
	rcu_read_unlock();

 out:
	__this_cpu_dec(bpf_prog_active);

	return ret;
}

#ifdef CONFIG_BPF_KPROBE_OVERRIDE
BPF_CALL_2(bpf_override_return, struct pt_regs *, regs, unsigned long, rc)
{
	regs_set_return_value(regs, rc);
	override_function_with_return(regs);
	return 0;
}

static const struct bpf_func_proto bpf_override_return_proto = {
	.func		= bpf_override_return,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
};
#endif

static __always_inline int
bpf_probe_read_user_common(void *dst, u32 size, const void __user *unsafe_ptr)
{
	int ret;

	ret = copy_from_user_nofault(dst, unsafe_ptr, size);
	if (unlikely(ret < 0))
		memset(dst, 0, size);
	return ret;
}

BPF_CALL_3(bpf_probe_read_user, void *, dst, u32, size,
	   const void __user *, unsafe_ptr)
{
	return bpf_probe_read_user_common(dst, size, unsafe_ptr);
}

const struct bpf_func_proto bpf_probe_read_user_proto = {
	.func		= bpf_probe_read_user,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

static __always_inline int
bpf_probe_read_user_str_common(void *dst, u32 size,
			       const void __user *unsafe_ptr)
{
	int ret;

	/*
	 * NB: We rely on strncpy_from_user() not copying junk past the NUL
	 * terminator into `dst`.
	 *
	 * strncpy_from_user() does long-sized strides in the fast path. If the
	 * strncpy does not mask out the bytes after the NUL in `unsafe_ptr`,
	 * then there could be junk after the NUL in `dst`. If user takes `dst`
	 * and keys a hash map with it, then semantically identical strings can
	 * occupy multiple entries in the map.
	 */
	ret = strncpy_from_user_nofault(dst, unsafe_ptr, size);
	if (unlikely(ret < 0))
		memset(dst, 0, size);
	return ret;
}

BPF_CALL_3(bpf_probe_read_user_str, void *, dst, u32, size,
	   const void __user *, unsafe_ptr)
{
	return bpf_probe_read_user_str_common(dst, size, unsafe_ptr);
}

const struct bpf_func_proto bpf_probe_read_user_str_proto = {
	.func		= bpf_probe_read_user_str,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_probe_read_kernel, void *, dst, u32, size,
	   const void *, unsafe_ptr)
{
	return bpf_probe_read_kernel_common(dst, size, unsafe_ptr);
}

const struct bpf_func_proto bpf_probe_read_kernel_proto = {
	.func		= bpf_probe_read_kernel,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

static __always_inline int
bpf_probe_read_kernel_str_common(void *dst, u32 size, const void *unsafe_ptr)
{
	int ret;

	/*
	 * The strncpy_from_kernel_nofault() call will likely not fill the
	 * entire buffer, but that's okay in this circumstance as we're probing
	 * arbitrary memory anyway similar to bpf_probe_read_*() and might
	 * as well probe the stack. Thus, memory is explicitly cleared
	 * only in error case, so that improper users ignoring return
	 * code altogether don't copy garbage; otherwise length of string
	 * is returned that can be used for bpf_perf_event_output() et al.
	 */
	ret = strncpy_from_kernel_nofault(dst, unsafe_ptr, size);
	if (unlikely(ret < 0))
		memset(dst, 0, size);
	return ret;
}

BPF_CALL_3(bpf_probe_read_kernel_str, void *, dst, u32, size,
	   const void *, unsafe_ptr)
{
	return bpf_probe_read_kernel_str_common(dst, size, unsafe_ptr);
}

const struct bpf_func_proto bpf_probe_read_kernel_str_proto = {
	.func		= bpf_probe_read_kernel_str,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
BPF_CALL_3(bpf_probe_read_compat, void *, dst, u32, size,
	   const void *, unsafe_ptr)
{
	if ((unsigned long)unsafe_ptr < TASK_SIZE) {
		return bpf_probe_read_user_common(dst, size,
				(__force void __user *)unsafe_ptr);
	}
	return bpf_probe_read_kernel_common(dst, size, unsafe_ptr);
}

static const struct bpf_func_proto bpf_probe_read_compat_proto = {
	.func		= bpf_probe_read_compat,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_probe_read_compat_str, void *, dst, u32, size,
	   const void *, unsafe_ptr)
{
	if ((unsigned long)unsafe_ptr < TASK_SIZE) {
		return bpf_probe_read_user_str_common(dst, size,
				(__force void __user *)unsafe_ptr);
	}
	return bpf_probe_read_kernel_str_common(dst, size, unsafe_ptr);
}

static const struct bpf_func_proto bpf_probe_read_compat_str_proto = {
	.func		= bpf_probe_read_compat_str,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg3_type	= ARG_ANYTHING,
};
#endif /* CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE */

BPF_CALL_3(bpf_probe_write_user, void __user *, unsafe_ptr, const void *, src,
	   u32, size)
{
	/*
	 * Ensure we're in user context which is safe for the helper to
	 * run. This helper has no business in a kthread.
	 *
	 * access_ok() should prevent writing to non-user memory, but in
	 * some situations (nommu, temporary switch, etc) access_ok() does
	 * not provide enough validation, hence the check on KERNEL_DS.
	 *
	 * nmi_uaccess_okay() ensures the probe is not run in an interim
	 * state, when the task or mm are switched. This is specifically
	 * required to prevent the use of temporary mm.
	 */

	if (unlikely(in_interrupt() ||
		     current->flags & (PF_KTHREAD | PF_EXITING)))
		return -EPERM;
	if (unlikely(!nmi_uaccess_okay()))
		return -EPERM;

	return copy_to_user_nofault(unsafe_ptr, src, size);
}

static const struct bpf_func_proto bpf_probe_write_user_proto = {
	.func		= bpf_probe_write_user,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE,
};

static const struct bpf_func_proto *bpf_get_probe_write_proto(void)
{
	if (!capable(CAP_SYS_ADMIN))
		return NULL;

	pr_warn_ratelimited("%s[%d] is installing a program with bpf_probe_write_user helper that may corrupt user memory!",
			    current->comm, task_pid_nr(current));

	return &bpf_probe_write_user_proto;
}

#define MAX_TRACE_PRINTK_VARARGS	3
#define BPF_TRACE_PRINTK_SIZE		1024

BPF_CALL_5(bpf_trace_printk, char *, fmt, u32, fmt_size, u64, arg1,
	   u64, arg2, u64, arg3)
{
	u64 args[MAX_TRACE_PRINTK_VARARGS] = { arg1, arg2, arg3 };
	struct bpf_bprintf_data data = {
		.get_bin_args	= true,
		.get_buf	= true,
	};
	int ret;

	ret = bpf_bprintf_prepare(fmt, fmt_size, args,
				  MAX_TRACE_PRINTK_VARARGS, &data);
	if (ret < 0)
		return ret;

	ret = bstr_printf(data.buf, MAX_BPRINTF_BUF, fmt, data.bin_args);

	trace_bpf_trace_printk(data.buf);

	bpf_bprintf_cleanup(&data);

	return ret;
}

static const struct bpf_func_proto bpf_trace_printk_proto = {
	.func		= bpf_trace_printk,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg2_type	= ARG_CONST_SIZE,
};

static void __set_printk_clr_event(void)
{
	/*
	 * This program might be calling bpf_trace_printk,
	 * so enable the associated bpf_trace/bpf_trace_printk event.
	 * Repeat this each time as it is possible a user has
	 * disabled bpf_trace_printk events.  By loading a program
	 * calling bpf_trace_printk() however the user has expressed
	 * the intent to see such events.
	 */
	if (trace_set_clr_event("bpf_trace", "bpf_trace_printk", 1))
		pr_warn_ratelimited("could not enable bpf_trace_printk events");
}

const struct bpf_func_proto *bpf_get_trace_printk_proto(void)
{
	__set_printk_clr_event();
	return &bpf_trace_printk_proto;
}

BPF_CALL_4(bpf_trace_vprintk, char *, fmt, u32, fmt_size, const void *, args,
	   u32, data_len)
{
	struct bpf_bprintf_data data = {
		.get_bin_args	= true,
		.get_buf	= true,
	};
	int ret, num_args;

	if (data_len & 7 || data_len > MAX_BPRINTF_VARARGS * 8 ||
	    (data_len && !args))
		return -EINVAL;
	num_args = data_len / 8;

	ret = bpf_bprintf_prepare(fmt, fmt_size, args, num_args, &data);
	if (ret < 0)
		return ret;

	ret = bstr_printf(data.buf, MAX_BPRINTF_BUF, fmt, data.bin_args);

	trace_bpf_trace_printk(data.buf);

	bpf_bprintf_cleanup(&data);

	return ret;
}

static const struct bpf_func_proto bpf_trace_vprintk_proto = {
	.func		= bpf_trace_vprintk,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg2_type	= ARG_CONST_SIZE,
	.arg3_type	= ARG_PTR_TO_MEM | PTR_MAYBE_NULL | MEM_RDONLY,
	.arg4_type	= ARG_CONST_SIZE_OR_ZERO,
};

const struct bpf_func_proto *bpf_get_trace_vprintk_proto(void)
{
	__set_printk_clr_event();
	return &bpf_trace_vprintk_proto;
}

BPF_CALL_5(bpf_seq_printf, struct seq_file *, m, char *, fmt, u32, fmt_size,
	   const void *, args, u32, data_len)
{
	struct bpf_bprintf_data data = {
		.get_bin_args	= true,
	};
	int err, num_args;

	if (data_len & 7 || data_len > MAX_BPRINTF_VARARGS * 8 ||
	    (data_len && !args))
		return -EINVAL;
	num_args = data_len / 8;

	err = bpf_bprintf_prepare(fmt, fmt_size, args, num_args, &data);
	if (err < 0)
		return err;

	seq_bprintf(m, fmt, data.bin_args);

	bpf_bprintf_cleanup(&data);

	return seq_has_overflowed(m) ? -EOVERFLOW : 0;
}

BTF_ID_LIST_SINGLE(btf_seq_file_ids, struct, seq_file)

static const struct bpf_func_proto bpf_seq_printf_proto = {
	.func		= bpf_seq_printf,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &btf_seq_file_ids[0],
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type      = ARG_PTR_TO_MEM | PTR_MAYBE_NULL | MEM_RDONLY,
	.arg5_type      = ARG_CONST_SIZE_OR_ZERO,
};

BPF_CALL_3(bpf_seq_write, struct seq_file *, m, const void *, data, u32, len)
{
	return seq_write(m, data, len) ? -EOVERFLOW : 0;
}

static const struct bpf_func_proto bpf_seq_write_proto = {
	.func		= bpf_seq_write,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &btf_seq_file_ids[0],
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
};

BPF_CALL_4(bpf_seq_printf_btf, struct seq_file *, m, struct btf_ptr *, ptr,
	   u32, btf_ptr_size, u64, flags)
{
	const struct btf *btf;
	s32 btf_id;
	int ret;

	ret = bpf_btf_printf_prepare(ptr, btf_ptr_size, flags, &btf, &btf_id);
	if (ret)
		return ret;

	return btf_type_seq_show_flags(btf, btf_id, ptr->ptr, m, flags);
}

static const struct bpf_func_proto bpf_seq_printf_btf_proto = {
	.func		= bpf_seq_printf_btf,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &btf_seq_file_ids[0],
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

static __always_inline int
get_map_perf_counter(struct bpf_map *map, u64 flags,
		     u64 *value, u64 *enabled, u64 *running)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	unsigned int cpu = smp_processor_id();
	u64 index = flags & BPF_F_INDEX_MASK;
	struct bpf_event_entry *ee;

	if (unlikely(flags & ~(BPF_F_INDEX_MASK)))
		return -EINVAL;
	if (index == BPF_F_CURRENT_CPU)
		index = cpu;
	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	ee = READ_ONCE(array->ptrs[index]);
	if (!ee)
		return -ENOENT;

	return perf_event_read_local(ee->event, value, enabled, running);
}

BPF_CALL_2(bpf_perf_event_read, struct bpf_map *, map, u64, flags)
{
	u64 value = 0;
	int err;

	err = get_map_perf_counter(map, flags, &value, NULL, NULL);
	/*
	 * this api is ugly since we miss [-22..-2] range of valid
	 * counter values, but that's uapi
	 */
	if (err)
		return err;
	return value;
}

static const struct bpf_func_proto bpf_perf_event_read_proto = {
	.func		= bpf_perf_event_read,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_perf_event_read_value, struct bpf_map *, map, u64, flags,
	   struct bpf_perf_event_value *, buf, u32, size)
{
	int err = -EINVAL;

	if (unlikely(size != sizeof(struct bpf_perf_event_value)))
		goto clear;
	err = get_map_perf_counter(map, flags, &buf->counter, &buf->enabled,
				   &buf->running);
	if (unlikely(err))
		goto clear;
	return 0;
clear:
	memset(buf, 0, size);
	return err;
}

static const struct bpf_func_proto bpf_perf_event_read_value_proto = {
	.func		= bpf_perf_event_read_value,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg4_type	= ARG_CONST_SIZE,
};

static __always_inline u64
__bpf_perf_event_output(struct pt_regs *regs, struct bpf_map *map,
			u64 flags, struct perf_sample_data *sd)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	unsigned int cpu = smp_processor_id();
	u64 index = flags & BPF_F_INDEX_MASK;
	struct bpf_event_entry *ee;
	struct perf_event *event;

	if (index == BPF_F_CURRENT_CPU)
		index = cpu;
	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	ee = READ_ONCE(array->ptrs[index]);
	if (!ee)
		return -ENOENT;

	event = ee->event;
	if (unlikely(event->attr.type != PERF_TYPE_SOFTWARE ||
		     event->attr.config != PERF_COUNT_SW_BPF_OUTPUT))
		return -EINVAL;

	if (unlikely(event->oncpu != cpu))
		return -EOPNOTSUPP;

	return perf_event_output(event, sd, regs);
}

/*
 * Support executing tracepoints in normal, irq, and nmi context that each call
 * bpf_perf_event_output
 */
struct bpf_trace_sample_data {
	struct perf_sample_data sds[3];
};

static DEFINE_PER_CPU(struct bpf_trace_sample_data, bpf_trace_sds);
static DEFINE_PER_CPU(int, bpf_trace_nest_level);
BPF_CALL_5(bpf_perf_event_output, struct pt_regs *, regs, struct bpf_map *, map,
	   u64, flags, void *, data, u64, size)
{
	struct bpf_trace_sample_data *sds;
	struct perf_raw_record raw = {
		.frag = {
			.size = size,
			.data = data,
		},
	};
	struct perf_sample_data *sd;
	int nest_level, err;

	preempt_disable();
	sds = this_cpu_ptr(&bpf_trace_sds);
	nest_level = this_cpu_inc_return(bpf_trace_nest_level);

	if (WARN_ON_ONCE(nest_level > ARRAY_SIZE(sds->sds))) {
		err = -EBUSY;
		goto out;
	}

	sd = &sds->sds[nest_level - 1];

	if (unlikely(flags & ~(BPF_F_INDEX_MASK))) {
		err = -EINVAL;
		goto out;
	}

	perf_sample_data_init(sd, 0, 0);
	perf_sample_save_raw_data(sd, &raw);

	err = __bpf_perf_event_output(regs, map, flags, sd);
out:
	this_cpu_dec(bpf_trace_nest_level);
	preempt_enable();
	return err;
}

static const struct bpf_func_proto bpf_perf_event_output_proto = {
	.func		= bpf_perf_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

static DEFINE_PER_CPU(int, bpf_event_output_nest_level);
struct bpf_nested_pt_regs {
	struct pt_regs regs[3];
};
static DEFINE_PER_CPU(struct bpf_nested_pt_regs, bpf_pt_regs);
static DEFINE_PER_CPU(struct bpf_trace_sample_data, bpf_misc_sds);

u64 bpf_event_output(struct bpf_map *map, u64 flags, void *meta, u64 meta_size,
		     void *ctx, u64 ctx_size, bpf_ctx_copy_t ctx_copy)
{
	struct perf_raw_frag frag = {
		.copy		= ctx_copy,
		.size		= ctx_size,
		.data		= ctx,
	};
	struct perf_raw_record raw = {
		.frag = {
			{
				.next	= ctx_size ? &frag : NULL,
			},
			.size	= meta_size,
			.data	= meta,
		},
	};
	struct perf_sample_data *sd;
	struct pt_regs *regs;
	int nest_level;
	u64 ret;

	preempt_disable();
	nest_level = this_cpu_inc_return(bpf_event_output_nest_level);

	if (WARN_ON_ONCE(nest_level > ARRAY_SIZE(bpf_misc_sds.sds))) {
		ret = -EBUSY;
		goto out;
	}
	sd = this_cpu_ptr(&bpf_misc_sds.sds[nest_level - 1]);
	regs = this_cpu_ptr(&bpf_pt_regs.regs[nest_level - 1]);

	perf_fetch_caller_regs(regs);
	perf_sample_data_init(sd, 0, 0);
	perf_sample_save_raw_data(sd, &raw);

	ret = __bpf_perf_event_output(regs, map, flags, sd);
out:
	this_cpu_dec(bpf_event_output_nest_level);
	preempt_enable();
	return ret;
}

BPF_CALL_0(bpf_get_current_task)
{
	return (long) current;
}

const struct bpf_func_proto bpf_get_current_task_proto = {
	.func		= bpf_get_current_task,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
};

BPF_CALL_0(bpf_get_current_task_btf)
{
	return (unsigned long) current;
}

const struct bpf_func_proto bpf_get_current_task_btf_proto = {
	.func		= bpf_get_current_task_btf,
	.gpl_only	= true,
	.ret_type	= RET_PTR_TO_BTF_ID_TRUSTED,
	.ret_btf_id	= &btf_tracing_ids[BTF_TRACING_TYPE_TASK],
};

BPF_CALL_1(bpf_task_pt_regs, struct task_struct *, task)
{
	return (unsigned long) task_pt_regs(task);
}

BTF_ID_LIST(bpf_task_pt_regs_ids)
BTF_ID(struct, pt_regs)

const struct bpf_func_proto bpf_task_pt_regs_proto = {
	.func		= bpf_task_pt_regs,
	.gpl_only	= true,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &btf_tracing_ids[BTF_TRACING_TYPE_TASK],
	.ret_type	= RET_PTR_TO_BTF_ID,
	.ret_btf_id	= &bpf_task_pt_regs_ids[0],
};

BPF_CALL_2(bpf_current_task_under_cgroup, struct bpf_map *, map, u32, idx)
{
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct cgroup *cgrp;

	if (unlikely(idx >= array->map.max_entries))
		return -E2BIG;

	cgrp = READ_ONCE(array->ptrs[idx]);
	if (unlikely(!cgrp))
		return -EAGAIN;

	return task_under_cgroup_hierarchy(current, cgrp);
}

static const struct bpf_func_proto bpf_current_task_under_cgroup_proto = {
	.func           = bpf_current_task_under_cgroup,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_CONST_MAP_PTR,
	.arg2_type      = ARG_ANYTHING,
};

struct send_signal_irq_work {
	struct irq_work irq_work;
	struct task_struct *task;
	u32 sig;
	enum pid_type type;
};

static DEFINE_PER_CPU(struct send_signal_irq_work, send_signal_work);

static void do_bpf_send_signal(struct irq_work *entry)
{
	struct send_signal_irq_work *work;

	work = container_of(entry, struct send_signal_irq_work, irq_work);
	group_send_sig_info(work->sig, SEND_SIG_PRIV, work->task, work->type);
	put_task_struct(work->task);
}

static int bpf_send_signal_common(u32 sig, enum pid_type type)
{
	struct send_signal_irq_work *work = NULL;

	/* Similar to bpf_probe_write_user, task needs to be
	 * in a sound condition and kernel memory access be
	 * permitted in order to send signal to the current
	 * task.
	 */
	if (unlikely(current->flags & (PF_KTHREAD | PF_EXITING)))
		return -EPERM;
	if (unlikely(!nmi_uaccess_okay()))
		return -EPERM;
	/* Task should not be pid=1 to avoid kernel panic. */
	if (unlikely(is_global_init(current)))
		return -EPERM;

	if (irqs_disabled()) {
		/* Do an early check on signal validity. Otherwise,
		 * the error is lost in deferred irq_work.
		 */
		if (unlikely(!valid_signal(sig)))
			return -EINVAL;

		work = this_cpu_ptr(&send_signal_work);
		if (irq_work_is_busy(&work->irq_work))
			return -EBUSY;

		/* Add the current task, which is the target of sending signal,
		 * to the irq_work. The current task may change when queued
		 * irq works get executed.
		 */
		work->task = get_task_struct(current);
		work->sig = sig;
		work->type = type;
		irq_work_queue(&work->irq_work);
		return 0;
	}

	return group_send_sig_info(sig, SEND_SIG_PRIV, current, type);
}

BPF_CALL_1(bpf_send_signal, u32, sig)
{
	return bpf_send_signal_common(sig, PIDTYPE_TGID);
}

static const struct bpf_func_proto bpf_send_signal_proto = {
	.func		= bpf_send_signal,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
};

BPF_CALL_1(bpf_send_signal_thread, u32, sig)
{
	return bpf_send_signal_common(sig, PIDTYPE_PID);
}

static const struct bpf_func_proto bpf_send_signal_thread_proto = {
	.func		= bpf_send_signal_thread,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_d_path, struct path *, path, char *, buf, u32, sz)
{
	struct path copy;
	long len;
	char *p;

	if (!sz)
		return 0;

	/*
	 * The path pointer is verified as trusted and safe to use,
	 * but let's double check it's valid anyway to workaround
	 * potentially broken verifier.
	 */
	len = copy_from_kernel_nofault(&copy, path, sizeof(*path));
	if (len < 0)
		return len;

	p = d_path(&copy, buf, sz);
	if (IS_ERR(p)) {
		len = PTR_ERR(p);
	} else {
		len = buf + sz - p;
		memmove(buf, p, len);
	}

	return len;
}

BTF_SET_START(btf_allowlist_d_path)
#ifdef CONFIG_SECURITY
BTF_ID(func, security_file_permission)
BTF_ID(func, security_inode_getattr)
BTF_ID(func, security_file_open)
#endif
#ifdef CONFIG_SECURITY_PATH
BTF_ID(func, security_path_truncate)
#endif
BTF_ID(func, vfs_truncate)
BTF_ID(func, vfs_fallocate)
BTF_ID(func, dentry_open)
BTF_ID(func, vfs_getattr)
BTF_ID(func, filp_close)
BTF_SET_END(btf_allowlist_d_path)

static bool bpf_d_path_allowed(const struct bpf_prog *prog)
{
	if (prog->type == BPF_PROG_TYPE_TRACING &&
	    prog->expected_attach_type == BPF_TRACE_ITER)
		return true;

	if (prog->type == BPF_PROG_TYPE_LSM)
		return bpf_lsm_is_sleepable_hook(prog->aux->attach_btf_id);

	return btf_id_set_contains(&btf_allowlist_d_path,
				   prog->aux->attach_btf_id);
}

BTF_ID_LIST_SINGLE(bpf_d_path_btf_ids, struct, path)

static const struct bpf_func_proto bpf_d_path_proto = {
	.func		= bpf_d_path,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &bpf_d_path_btf_ids[0],
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.allowed	= bpf_d_path_allowed,
};

#define BTF_F_ALL	(BTF_F_COMPACT  | BTF_F_NONAME | \
			 BTF_F_PTR_RAW | BTF_F_ZERO)

static int bpf_btf_printf_prepare(struct btf_ptr *ptr, u32 btf_ptr_size,
				  u64 flags, const struct btf **btf,
				  s32 *btf_id)
{
	const struct btf_type *t;

	if (unlikely(flags & ~(BTF_F_ALL)))
		return -EINVAL;

	if (btf_ptr_size != sizeof(struct btf_ptr))
		return -EINVAL;

	*btf = bpf_get_btf_vmlinux();

	if (IS_ERR_OR_NULL(*btf))
		return IS_ERR(*btf) ? PTR_ERR(*btf) : -EINVAL;

	if (ptr->type_id > 0)
		*btf_id = ptr->type_id;
	else
		return -EINVAL;

	if (*btf_id > 0)
		t = btf_type_by_id(*btf, *btf_id);
	if (*btf_id <= 0 || !t)
		return -ENOENT;

	return 0;
}

BPF_CALL_5(bpf_snprintf_btf, char *, str, u32, str_size, struct btf_ptr *, ptr,
	   u32, btf_ptr_size, u64, flags)
{
	const struct btf *btf;
	s32 btf_id;
	int ret;

	ret = bpf_btf_printf_prepare(ptr, btf_ptr_size, flags, &btf, &btf_id);
	if (ret)
		return ret;

	return btf_type_snprintf_show(btf, btf_id, ptr->ptr, str, str_size,
				      flags);
}

const struct bpf_func_proto bpf_snprintf_btf_proto = {
	.func		= bpf_snprintf_btf,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM,
	.arg2_type	= ARG_CONST_SIZE,
	.arg3_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg4_type	= ARG_CONST_SIZE,
	.arg5_type	= ARG_ANYTHING,
};

BPF_CALL_1(bpf_get_func_ip_tracing, void *, ctx)
{
	/* This helper call is inlined by verifier. */
	return ((u64 *)ctx)[-2];
}

static const struct bpf_func_proto bpf_get_func_ip_proto_tracing = {
	.func		= bpf_get_func_ip_tracing,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

#ifdef CONFIG_X86_KERNEL_IBT
static unsigned long get_entry_ip(unsigned long fentry_ip)
{
	u32 instr;

	/* Being extra safe in here in case entry ip is on the page-edge. */
	if (get_kernel_nofault(instr, (u32 *) fentry_ip - 1))
		return fentry_ip;
	if (is_endbr(instr))
		fentry_ip -= ENDBR_INSN_SIZE;
	return fentry_ip;
}
#else
#define get_entry_ip(fentry_ip) fentry_ip
#endif

BPF_CALL_1(bpf_get_func_ip_kprobe, struct pt_regs *, regs)
{
	struct bpf_trace_run_ctx *run_ctx __maybe_unused;
	struct kprobe *kp;

#ifdef CONFIG_UPROBES
	run_ctx = container_of(current->bpf_ctx, struct bpf_trace_run_ctx, run_ctx);
	if (run_ctx->is_uprobe)
		return ((struct uprobe_dispatch_data *)current->utask->vaddr)->bp_addr;
#endif

	kp = kprobe_running();

	if (!kp || !(kp->flags & KPROBE_FLAG_ON_FUNC_ENTRY))
		return 0;

	return get_entry_ip((uintptr_t)kp->addr);
}

static const struct bpf_func_proto bpf_get_func_ip_proto_kprobe = {
	.func		= bpf_get_func_ip_kprobe,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_func_ip_kprobe_multi, struct pt_regs *, regs)
{
	return bpf_kprobe_multi_entry_ip(current->bpf_ctx);
}

static const struct bpf_func_proto bpf_get_func_ip_proto_kprobe_multi = {
	.func		= bpf_get_func_ip_kprobe_multi,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_attach_cookie_kprobe_multi, struct pt_regs *, regs)
{
	return bpf_kprobe_multi_cookie(current->bpf_ctx);
}

static const struct bpf_func_proto bpf_get_attach_cookie_proto_kmulti = {
	.func		= bpf_get_attach_cookie_kprobe_multi,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_func_ip_uprobe_multi, struct pt_regs *, regs)
{
	return bpf_uprobe_multi_entry_ip(current->bpf_ctx);
}

static const struct bpf_func_proto bpf_get_func_ip_proto_uprobe_multi = {
	.func		= bpf_get_func_ip_uprobe_multi,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_attach_cookie_uprobe_multi, struct pt_regs *, regs)
{
	return bpf_uprobe_multi_cookie(current->bpf_ctx);
}

static const struct bpf_func_proto bpf_get_attach_cookie_proto_umulti = {
	.func		= bpf_get_attach_cookie_uprobe_multi,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_attach_cookie_trace, void *, ctx)
{
	struct bpf_trace_run_ctx *run_ctx;

	run_ctx = container_of(current->bpf_ctx, struct bpf_trace_run_ctx, run_ctx);
	return run_ctx->bpf_cookie;
}

static const struct bpf_func_proto bpf_get_attach_cookie_proto_trace = {
	.func		= bpf_get_attach_cookie_trace,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_attach_cookie_pe, struct bpf_perf_event_data_kern *, ctx)
{
	return ctx->event->bpf_cookie;
}

static const struct bpf_func_proto bpf_get_attach_cookie_proto_pe = {
	.func		= bpf_get_attach_cookie_pe,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_1(bpf_get_attach_cookie_tracing, void *, ctx)
{
	struct bpf_trace_run_ctx *run_ctx;

	run_ctx = container_of(current->bpf_ctx, struct bpf_trace_run_ctx, run_ctx);
	return run_ctx->bpf_cookie;
}

static const struct bpf_func_proto bpf_get_attach_cookie_proto_tracing = {
	.func		= bpf_get_attach_cookie_tracing,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

BPF_CALL_3(bpf_get_branch_snapshot, void *, buf, u32, size, u64, flags)
{
#ifndef CONFIG_X86
	return -ENOENT;
#else
	static const u32 br_entry_size = sizeof(struct perf_branch_entry);
	u32 entry_cnt = size / br_entry_size;

	entry_cnt = static_call(perf_snapshot_branch_stack)(buf, entry_cnt);

	if (unlikely(flags))
		return -EINVAL;

	if (!entry_cnt)
		return -ENOENT;

	return entry_cnt * br_entry_size;
#endif
}

static const struct bpf_func_proto bpf_get_branch_snapshot_proto = {
	.func		= bpf_get_branch_snapshot,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg2_type	= ARG_CONST_SIZE_OR_ZERO,
};

BPF_CALL_3(get_func_arg, void *, ctx, u32, n, u64 *, value)
{
	/* This helper call is inlined by verifier. */
	u64 nr_args = ((u64 *)ctx)[-1];

	if ((u64) n >= nr_args)
		return -EINVAL;
	*value = ((u64 *)ctx)[n];
	return 0;
}

static const struct bpf_func_proto bpf_get_func_arg_proto = {
	.func		= get_func_arg,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_LONG,
};

BPF_CALL_2(get_func_ret, void *, ctx, u64 *, value)
{
	/* This helper call is inlined by verifier. */
	u64 nr_args = ((u64 *)ctx)[-1];

	*value = ((u64 *)ctx)[nr_args];
	return 0;
}

static const struct bpf_func_proto bpf_get_func_ret_proto = {
	.func		= get_func_ret,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_LONG,
};

BPF_CALL_1(get_func_arg_cnt, void *, ctx)
{
	/* This helper call is inlined by verifier. */
	return ((u64 *)ctx)[-1];
}

static const struct bpf_func_proto bpf_get_func_arg_cnt_proto = {
	.func		= get_func_arg_cnt,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
};

#ifdef CONFIG_KEYS
__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "kfuncs which will be used in BPF programs");

/**
 * bpf_lookup_user_key - lookup a key by its serial
 * @serial: key handle serial number
 * @flags: lookup-specific flags
 *
 * Search a key with a given *serial* and the provided *flags*.
 * If found, increment the reference count of the key by one, and
 * return it in the bpf_key structure.
 *
 * The bpf_key structure must be passed to bpf_key_put() when done
 * with it, so that the key reference count is decremented and the
 * bpf_key structure is freed.
 *
 * Permission checks are deferred to the time the key is used by
 * one of the available key-specific kfuncs.
 *
 * Set *flags* with KEY_LOOKUP_CREATE, to attempt creating a requested
 * special keyring (e.g. session keyring), if it doesn't yet exist.
 * Set *flags* with KEY_LOOKUP_PARTIAL, to lookup a key without waiting
 * for the key construction, and to retrieve uninstantiated keys (keys
 * without data attached to them).
 *
 * Return: a bpf_key pointer with a valid key pointer if the key is found, a
 *         NULL pointer otherwise.
 */
__bpf_kfunc struct bpf_key *bpf_lookup_user_key(u32 serial, u64 flags)
{
	key_ref_t key_ref;
	struct bpf_key *bkey;

	if (flags & ~KEY_LOOKUP_ALL)
		return NULL;

	/*
	 * Permission check is deferred until the key is used, as the
	 * intent of the caller is unknown here.
	 */
	key_ref = lookup_user_key(serial, flags, KEY_DEFER_PERM_CHECK);
	if (IS_ERR(key_ref))
		return NULL;

	bkey = kmalloc(sizeof(*bkey), GFP_KERNEL);
	if (!bkey) {
		key_put(key_ref_to_ptr(key_ref));
		return NULL;
	}

	bkey->key = key_ref_to_ptr(key_ref);
	bkey->has_ref = true;

	return bkey;
}

/**
 * bpf_lookup_system_key - lookup a key by a system-defined ID
 * @id: key ID
 *
 * Obtain a bpf_key structure with a key pointer set to the passed key ID.
 * The key pointer is marked as invalid, to prevent bpf_key_put() from
 * attempting to decrement the key reference count on that pointer. The key
 * pointer set in such way is currently understood only by
 * verify_pkcs7_signature().
 *
 * Set *id* to one of the values defined in include/linux/verification.h:
 * 0 for the primary keyring (immutable keyring of system keys);
 * VERIFY_USE_SECONDARY_KEYRING for both the primary and secondary keyring
 * (where keys can be added only if they are vouched for by existing keys
 * in those keyrings); VERIFY_USE_PLATFORM_KEYRING for the platform
 * keyring (primarily used by the integrity subsystem to verify a kexec'ed
 * kerned image and, possibly, the initramfs signature).
 *
 * Return: a bpf_key pointer with an invalid key pointer set from the
 *         pre-determined ID on success, a NULL pointer otherwise
 */
__bpf_kfunc struct bpf_key *bpf_lookup_system_key(u64 id)
{
	struct bpf_key *bkey;

	if (system_keyring_id_check(id) < 0)
		return NULL;

	bkey = kmalloc(sizeof(*bkey), GFP_ATOMIC);
	if (!bkey)
		return NULL;

	bkey->key = (struct key *)(unsigned long)id;
	bkey->has_ref = false;

	return bkey;
}

/**
 * bpf_key_put - decrement key reference count if key is valid and free bpf_key
 * @bkey: bpf_key structure
 *
 * Decrement the reference count of the key inside *bkey*, if the pointer
 * is valid, and free *bkey*.
 */
__bpf_kfunc void bpf_key_put(struct bpf_key *bkey)
{
	if (bkey->has_ref)
		key_put(bkey->key);

	kfree(bkey);
}

#ifdef CONFIG_SYSTEM_DATA_VERIFICATION
/**
 * bpf_verify_pkcs7_signature - verify a PKCS#7 signature
 * @data_ptr: data to verify
 * @sig_ptr: signature of the data
 * @trusted_keyring: keyring with keys trusted for signature verification
 *
 * Verify the PKCS#7 signature *sig_ptr* against the supplied *data_ptr*
 * with keys in a keyring referenced by *trusted_keyring*.
 *
 * Return: 0 on success, a negative value on error.
 */
__bpf_kfunc int bpf_verify_pkcs7_signature(struct bpf_dynptr_kern *data_ptr,
			       struct bpf_dynptr_kern *sig_ptr,
			       struct bpf_key *trusted_keyring)
{
	int ret;

	if (trusted_keyring->has_ref) {
		/*
		 * Do the permission check deferred in bpf_lookup_user_key().
		 * See bpf_lookup_user_key() for more details.
		 *
		 * A call to key_task_permission() here would be redundant, as
		 * it is already done by keyring_search() called by
		 * find_asymmetric_key().
		 */
		ret = key_validate(trusted_keyring->key);
		if (ret < 0)
			return ret;
	}

	return verify_pkcs7_signature(data_ptr->data,
				      __bpf_dynptr_size(data_ptr),
				      sig_ptr->data,
				      __bpf_dynptr_size(sig_ptr),
				      trusted_keyring->key,
				      VERIFYING_UNSPECIFIED_SIGNATURE, NULL,
				      NULL);
}
#endif /* CONFIG_SYSTEM_DATA_VERIFICATION */

__diag_pop();

BTF_SET8_START(key_sig_kfunc_set)
BTF_ID_FLAGS(func, bpf_lookup_user_key, KF_ACQUIRE | KF_RET_NULL | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_lookup_system_key, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_key_put, KF_RELEASE)
#ifdef CONFIG_SYSTEM_DATA_VERIFICATION
BTF_ID_FLAGS(func, bpf_verify_pkcs7_signature, KF_SLEEPABLE)
#endif
BTF_SET8_END(key_sig_kfunc_set)

static const struct btf_kfunc_id_set bpf_key_sig_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &key_sig_kfunc_set,
};

static int __init bpf_key_sig_kfuncs_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING,
					 &bpf_key_sig_kfunc_set);
}

late_initcall(bpf_key_sig_kfuncs_init);
#endif /* CONFIG_KEYS */

static const struct bpf_func_proto *
bpf_tracing_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_map_push_elem:
		return &bpf_map_push_elem_proto;
	case BPF_FUNC_map_pop_elem:
		return &bpf_map_pop_elem_proto;
	case BPF_FUNC_map_peek_elem:
		return &bpf_map_peek_elem_proto;
	case BPF_FUNC_map_lookup_percpu_elem:
		return &bpf_map_lookup_percpu_elem_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	case BPF_FUNC_ktime_get_boot_ns:
		return &bpf_ktime_get_boot_ns_proto;
	case BPF_FUNC_tail_call:
		return &bpf_tail_call_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_current_task:
		return &bpf_get_current_task_proto;
	case BPF_FUNC_get_current_task_btf:
		return &bpf_get_current_task_btf_proto;
	case BPF_FUNC_task_pt_regs:
		return &bpf_task_pt_regs_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
	case BPF_FUNC_trace_printk:
		return bpf_get_trace_printk_proto();
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	case BPF_FUNC_get_numa_node_id:
		return &bpf_get_numa_node_id_proto;
	case BPF_FUNC_perf_event_read:
		return &bpf_perf_event_read_proto;
	case BPF_FUNC_current_task_under_cgroup:
		return &bpf_current_task_under_cgroup_proto;
	case BPF_FUNC_get_prandom_u32:
		return &bpf_get_prandom_u32_proto;
	case BPF_FUNC_probe_write_user:
		return security_locked_down(LOCKDOWN_BPF_WRITE_USER) < 0 ?
		       NULL : bpf_get_probe_write_proto();
	case BPF_FUNC_probe_read_user:
		return &bpf_probe_read_user_proto;
	case BPF_FUNC_probe_read_kernel:
		return security_locked_down(LOCKDOWN_BPF_READ_KERNEL) < 0 ?
		       NULL : &bpf_probe_read_kernel_proto;
	case BPF_FUNC_probe_read_user_str:
		return &bpf_probe_read_user_str_proto;
	case BPF_FUNC_probe_read_kernel_str:
		return security_locked_down(LOCKDOWN_BPF_READ_KERNEL) < 0 ?
		       NULL : &bpf_probe_read_kernel_str_proto;
#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
	case BPF_FUNC_probe_read:
		return security_locked_down(LOCKDOWN_BPF_READ_KERNEL) < 0 ?
		       NULL : &bpf_probe_read_compat_proto;
	case BPF_FUNC_probe_read_str:
		return security_locked_down(LOCKDOWN_BPF_READ_KERNEL) < 0 ?
		       NULL : &bpf_probe_read_compat_str_proto;
#endif
#ifdef CONFIG_CGROUPS
	case BPF_FUNC_cgrp_storage_get:
		return &bpf_cgrp_storage_get_proto;
	case BPF_FUNC_cgrp_storage_delete:
		return &bpf_cgrp_storage_delete_proto;
#endif
	case BPF_FUNC_send_signal:
		return &bpf_send_signal_proto;
	case BPF_FUNC_send_signal_thread:
		return &bpf_send_signal_thread_proto;
	case BPF_FUNC_perf_event_read_value:
		return &bpf_perf_event_read_value_proto;
	case BPF_FUNC_get_ns_current_pid_tgid:
		return &bpf_get_ns_current_pid_tgid_proto;
	case BPF_FUNC_ringbuf_output:
		return &bpf_ringbuf_output_proto;
	case BPF_FUNC_ringbuf_reserve:
		return &bpf_ringbuf_reserve_proto;
	case BPF_FUNC_ringbuf_submit:
		return &bpf_ringbuf_submit_proto;
	case BPF_FUNC_ringbuf_discard:
		return &bpf_ringbuf_discard_proto;
	case BPF_FUNC_ringbuf_query:
		return &bpf_ringbuf_query_proto;
	case BPF_FUNC_jiffies64:
		return &bpf_jiffies64_proto;
	case BPF_FUNC_get_task_stack:
		return &bpf_get_task_stack_proto;
	case BPF_FUNC_copy_from_user:
		return &bpf_copy_from_user_proto;
	case BPF_FUNC_copy_from_user_task:
		return &bpf_copy_from_user_task_proto;
	case BPF_FUNC_snprintf_btf:
		return &bpf_snprintf_btf_proto;
	case BPF_FUNC_per_cpu_ptr:
		return &bpf_per_cpu_ptr_proto;
	case BPF_FUNC_this_cpu_ptr:
		return &bpf_this_cpu_ptr_proto;
	case BPF_FUNC_task_storage_get:
		if (bpf_prog_check_recur(prog))
			return &bpf_task_storage_get_recur_proto;
		return &bpf_task_storage_get_proto;
	case BPF_FUNC_task_storage_delete:
		if (bpf_prog_check_recur(prog))
			return &bpf_task_storage_delete_recur_proto;
		return &bpf_task_storage_delete_proto;
	case BPF_FUNC_for_each_map_elem:
		return &bpf_for_each_map_elem_proto;
	case BPF_FUNC_snprintf:
		return &bpf_snprintf_proto;
	case BPF_FUNC_get_func_ip:
		return &bpf_get_func_ip_proto_tracing;
	case BPF_FUNC_get_branch_snapshot:
		return &bpf_get_branch_snapshot_proto;
	case BPF_FUNC_find_vma:
		return &bpf_find_vma_proto;
	case BPF_FUNC_trace_vprintk:
		return bpf_get_trace_vprintk_proto();
	default:
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_func_proto *
kprobe_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto;
	case BPF_FUNC_get_stack:
		return &bpf_get_stack_proto;
#ifdef CONFIG_BPF_KPROBE_OVERRIDE
	case BPF_FUNC_override_return:
		return &bpf_override_return_proto;
#endif
	case BPF_FUNC_get_func_ip:
		if (prog->expected_attach_type == BPF_TRACE_KPROBE_MULTI)
			return &bpf_get_func_ip_proto_kprobe_multi;
		if (prog->expected_attach_type == BPF_TRACE_UPROBE_MULTI)
			return &bpf_get_func_ip_proto_uprobe_multi;
		return &bpf_get_func_ip_proto_kprobe;
	case BPF_FUNC_get_attach_cookie:
		if (prog->expected_attach_type == BPF_TRACE_KPROBE_MULTI)
			return &bpf_get_attach_cookie_proto_kmulti;
		if (prog->expected_attach_type == BPF_TRACE_UPROBE_MULTI)
			return &bpf_get_attach_cookie_proto_umulti;
		return &bpf_get_attach_cookie_proto_trace;
	default:
		return bpf_tracing_func_proto(func_id, prog);
	}
}

/* bpf+kprobe programs can access fields of 'struct pt_regs' */
static bool kprobe_prog_is_valid_access(int off, int size, enum bpf_access_type type,
					const struct bpf_prog *prog,
					struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(struct pt_regs))
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	/*
	 * Assertion for 32 bit to make sure last 8 byte access
	 * (BPF_DW) to the last 4 byte member is disallowed.
	 */
	if (off + size > sizeof(struct pt_regs))
		return false;

	return true;
}

const struct bpf_verifier_ops kprobe_verifier_ops = {
	.get_func_proto  = kprobe_prog_func_proto,
	.is_valid_access = kprobe_prog_is_valid_access,
};

const struct bpf_prog_ops kprobe_prog_ops = {
};

BPF_CALL_5(bpf_perf_event_output_tp, void *, tp_buff, struct bpf_map *, map,
	   u64, flags, void *, data, u64, size)
{
	struct pt_regs *regs = *(struct pt_regs **)tp_buff;

	/*
	 * r1 points to perf tracepoint buffer where first 8 bytes are hidden
	 * from bpf program and contain a pointer to 'struct pt_regs'. Fetch it
	 * from there and call the same bpf_perf_event_output() helper inline.
	 */
	return ____bpf_perf_event_output(regs, map, flags, data, size);
}

static const struct bpf_func_proto bpf_perf_event_output_proto_tp = {
	.func		= bpf_perf_event_output_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

BPF_CALL_3(bpf_get_stackid_tp, void *, tp_buff, struct bpf_map *, map,
	   u64, flags)
{
	struct pt_regs *regs = *(struct pt_regs **)tp_buff;

	/*
	 * Same comment as in bpf_perf_event_output_tp(), only that this time
	 * the other helper's function body cannot be inlined due to being
	 * external, thus we need to call raw helper function.
	 */
	return bpf_get_stackid((unsigned long) regs, (unsigned long) map,
			       flags, 0, 0);
}

static const struct bpf_func_proto bpf_get_stackid_proto_tp = {
	.func		= bpf_get_stackid_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_get_stack_tp, void *, tp_buff, void *, buf, u32, size,
	   u64, flags)
{
	struct pt_regs *regs = *(struct pt_regs **)tp_buff;

	return bpf_get_stack((unsigned long) regs, (unsigned long) buf,
			     (unsigned long) size, flags, 0);
}

static const struct bpf_func_proto bpf_get_stack_proto_tp = {
	.func		= bpf_get_stack_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_UNINIT_MEM,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *
tp_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto_tp;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto_tp;
	case BPF_FUNC_get_stack:
		return &bpf_get_stack_proto_tp;
	case BPF_FUNC_get_attach_cookie:
		return &bpf_get_attach_cookie_proto_trace;
	default:
		return bpf_tracing_func_proto(func_id, prog);
	}
}

static bool tp_prog_is_valid_access(int off, int size, enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	if (off < sizeof(void *) || off >= PERF_MAX_TRACE_SIZE)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;

	BUILD_BUG_ON(PERF_MAX_TRACE_SIZE % sizeof(__u64));
	return true;
}

const struct bpf_verifier_ops tracepoint_verifier_ops = {
	.get_func_proto  = tp_prog_func_proto,
	.is_valid_access = tp_prog_is_valid_access,
};

const struct bpf_prog_ops tracepoint_prog_ops = {
};

BPF_CALL_3(bpf_perf_prog_read_value, struct bpf_perf_event_data_kern *, ctx,
	   struct bpf_perf_event_value *, buf, u32, size)
{
	int err = -EINVAL;

	if (unlikely(size != sizeof(struct bpf_perf_event_value)))
		goto clear;
	err = perf_event_read_local(ctx->event, &buf->counter, &buf->enabled,
				    &buf->running);
	if (unlikely(err))
		goto clear;
	return 0;
clear:
	memset(buf, 0, size);
	return err;
}

static const struct bpf_func_proto bpf_perf_prog_read_value_proto = {
         .func           = bpf_perf_prog_read_value,
         .gpl_only       = true,
         .ret_type       = RET_INTEGER,
         .arg1_type      = ARG_PTR_TO_CTX,
         .arg2_type      = ARG_PTR_TO_UNINIT_MEM,
         .arg3_type      = ARG_CONST_SIZE,
};

BPF_CALL_4(bpf_read_branch_records, struct bpf_perf_event_data_kern *, ctx,
	   void *, buf, u32, size, u64, flags)
{
	static const u32 br_entry_size = sizeof(struct perf_branch_entry);
	struct perf_branch_stack *br_stack = ctx->data->br_stack;
	u32 to_copy;

	if (unlikely(flags & ~BPF_F_GET_BRANCH_RECORDS_SIZE))
		return -EINVAL;

	if (unlikely(!(ctx->data->sample_flags & PERF_SAMPLE_BRANCH_STACK)))
		return -ENOENT;

	if (unlikely(!br_stack))
		return -ENOENT;

	if (flags & BPF_F_GET_BRANCH_RECORDS_SIZE)
		return br_stack->nr * br_entry_size;

	if (!buf || (size % br_entry_size != 0))
		return -EINVAL;

	to_copy = min_t(u32, br_stack->nr * br_entry_size, size);
	memcpy(buf, br_stack->entries, to_copy);

	return to_copy;
}

static const struct bpf_func_proto bpf_read_branch_records_proto = {
	.func           = bpf_read_branch_records,
	.gpl_only       = true,
	.ret_type       = RET_INTEGER,
	.arg1_type      = ARG_PTR_TO_CTX,
	.arg2_type      = ARG_PTR_TO_MEM_OR_NULL,
	.arg3_type      = ARG_CONST_SIZE_OR_ZERO,
	.arg4_type      = ARG_ANYTHING,
};

static const struct bpf_func_proto *
pe_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto_tp;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto_pe;
	case BPF_FUNC_get_stack:
		return &bpf_get_stack_proto_pe;
	case BPF_FUNC_perf_prog_read_value:
		return &bpf_perf_prog_read_value_proto;
	case BPF_FUNC_read_branch_records:
		return &bpf_read_branch_records_proto;
	case BPF_FUNC_get_attach_cookie:
		return &bpf_get_attach_cookie_proto_pe;
	default:
		return bpf_tracing_func_proto(func_id, prog);
	}
}

/*
 * bpf_raw_tp_regs are separate from bpf_pt_regs used from skb/xdp
 * to avoid potential recursive reuse issue when/if tracepoints are added
 * inside bpf_*_event_output, bpf_get_stackid and/or bpf_get_stack.
 *
 * Since raw tracepoints run despite bpf_prog_active, support concurrent usage
 * in normal, irq, and nmi context.
 */
struct bpf_raw_tp_regs {
	struct pt_regs regs[3];
};
static DEFINE_PER_CPU(struct bpf_raw_tp_regs, bpf_raw_tp_regs);
static DEFINE_PER_CPU(int, bpf_raw_tp_nest_level);
static struct pt_regs *get_bpf_raw_tp_regs(void)
{
	struct bpf_raw_tp_regs *tp_regs = this_cpu_ptr(&bpf_raw_tp_regs);
	int nest_level = this_cpu_inc_return(bpf_raw_tp_nest_level);

	if (WARN_ON_ONCE(nest_level > ARRAY_SIZE(tp_regs->regs))) {
		this_cpu_dec(bpf_raw_tp_nest_level);
		return ERR_PTR(-EBUSY);
	}

	return &tp_regs->regs[nest_level - 1];
}

static void put_bpf_raw_tp_regs(void)
{
	this_cpu_dec(bpf_raw_tp_nest_level);
}

BPF_CALL_5(bpf_perf_event_output_raw_tp, struct bpf_raw_tracepoint_args *, args,
	   struct bpf_map *, map, u64, flags, void *, data, u64, size)
{
	struct pt_regs *regs = get_bpf_raw_tp_regs();
	int ret;

	if (IS_ERR(regs))
		return PTR_ERR(regs);

	perf_fetch_caller_regs(regs);
	ret = ____bpf_perf_event_output(regs, map, flags, data, size);

	put_bpf_raw_tp_regs();
	return ret;
}

static const struct bpf_func_proto bpf_perf_event_output_proto_raw_tp = {
	.func		= bpf_perf_event_output_raw_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

extern const struct bpf_func_proto bpf_skb_output_proto;
extern const struct bpf_func_proto bpf_xdp_output_proto;
extern const struct bpf_func_proto bpf_xdp_get_buff_len_trace_proto;

BPF_CALL_3(bpf_get_stackid_raw_tp, struct bpf_raw_tracepoint_args *, args,
	   struct bpf_map *, map, u64, flags)
{
	struct pt_regs *regs = get_bpf_raw_tp_regs();
	int ret;

	if (IS_ERR(regs))
		return PTR_ERR(regs);

	perf_fetch_caller_regs(regs);
	/* similar to bpf_perf_event_output_tp, but pt_regs fetched differently */
	ret = bpf_get_stackid((unsigned long) regs, (unsigned long) map,
			      flags, 0, 0);
	put_bpf_raw_tp_regs();
	return ret;
}

static const struct bpf_func_proto bpf_get_stackid_proto_raw_tp = {
	.func		= bpf_get_stackid_raw_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_get_stack_raw_tp, struct bpf_raw_tracepoint_args *, args,
	   void *, buf, u32, size, u64, flags)
{
	struct pt_regs *regs = get_bpf_raw_tp_regs();
	int ret;

	if (IS_ERR(regs))
		return PTR_ERR(regs);

	perf_fetch_caller_regs(regs);
	ret = bpf_get_stack((unsigned long) regs, (unsigned long) buf,
			    (unsigned long) size, flags, 0);
	put_bpf_raw_tp_regs();
	return ret;
}

static const struct bpf_func_proto bpf_get_stack_proto_raw_tp = {
	.func		= bpf_get_stack_raw_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_CONST_SIZE_OR_ZERO,
	.arg4_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *
raw_tp_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto_raw_tp;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto_raw_tp;
	case BPF_FUNC_get_stack:
		return &bpf_get_stack_proto_raw_tp;
	default:
		return bpf_tracing_func_proto(func_id, prog);
	}
}

const struct bpf_func_proto *
tracing_prog_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	const struct bpf_func_proto *fn;

	switch (func_id) {
#ifdef CONFIG_NET
	case BPF_FUNC_skb_output:
		return &bpf_skb_output_proto;
	case BPF_FUNC_xdp_output:
		return &bpf_xdp_output_proto;
	case BPF_FUNC_skc_to_tcp6_sock:
		return &bpf_skc_to_tcp6_sock_proto;
	case BPF_FUNC_skc_to_tcp_sock:
		return &bpf_skc_to_tcp_sock_proto;
	case BPF_FUNC_skc_to_tcp_timewait_sock:
		return &bpf_skc_to_tcp_timewait_sock_proto;
	case BPF_FUNC_skc_to_tcp_request_sock:
		return &bpf_skc_to_tcp_request_sock_proto;
	case BPF_FUNC_skc_to_udp6_sock:
		return &bpf_skc_to_udp6_sock_proto;
	case BPF_FUNC_skc_to_unix_sock:
		return &bpf_skc_to_unix_sock_proto;
	case BPF_FUNC_skc_to_mptcp_sock:
		return &bpf_skc_to_mptcp_sock_proto;
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_tracing_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_tracing_proto;
	case BPF_FUNC_sock_from_file:
		return &bpf_sock_from_file_proto;
	case BPF_FUNC_get_socket_cookie:
		return &bpf_get_socket_ptr_cookie_proto;
	case BPF_FUNC_xdp_get_buff_len:
		return &bpf_xdp_get_buff_len_trace_proto;
#endif
	case BPF_FUNC_seq_printf:
		return prog->expected_attach_type == BPF_TRACE_ITER ?
		       &bpf_seq_printf_proto :
		       NULL;
	case BPF_FUNC_seq_write:
		return prog->expected_attach_type == BPF_TRACE_ITER ?
		       &bpf_seq_write_proto :
		       NULL;
	case BPF_FUNC_seq_printf_btf:
		return prog->expected_attach_type == BPF_TRACE_ITER ?
		       &bpf_seq_printf_btf_proto :
		       NULL;
	case BPF_FUNC_d_path:
		return &bpf_d_path_proto;
	case BPF_FUNC_get_func_arg:
		return bpf_prog_has_trampoline(prog) ? &bpf_get_func_arg_proto : NULL;
	case BPF_FUNC_get_func_ret:
		return bpf_prog_has_trampoline(prog) ? &bpf_get_func_ret_proto : NULL;
	case BPF_FUNC_get_func_arg_cnt:
		return bpf_prog_has_trampoline(prog) ? &bpf_get_func_arg_cnt_proto : NULL;
	case BPF_FUNC_get_attach_cookie:
		return bpf_prog_has_trampoline(prog) ? &bpf_get_attach_cookie_proto_tracing : NULL;
	default:
		fn = raw_tp_prog_func_proto(func_id, prog);
		if (!fn && prog->expected_attach_type == BPF_TRACE_ITER)
			fn = bpf_iter_get_func_proto(func_id, prog);
		return fn;
	}
}

static bool raw_tp_prog_is_valid_access(int off, int size,
					enum bpf_access_type type,
					const struct bpf_prog *prog,
					struct bpf_insn_access_aux *info)
{
	return bpf_tracing_ctx_access(off, size, type);
}

static bool tracing_prog_is_valid_access(int off, int size,
					 enum bpf_access_type type,
					 const struct bpf_prog *prog,
					 struct bpf_insn_access_aux *info)
{
	return bpf_tracing_btf_ctx_access(off, size, type, prog, info);
}

int __weak bpf_prog_test_run_tracing(struct bpf_prog *prog,
				     const union bpf_attr *kattr,
				     union bpf_attr __user *uattr)
{
	return -ENOTSUPP;
}

const struct bpf_verifier_ops raw_tracepoint_verifier_ops = {
	.get_func_proto  = raw_tp_prog_func_proto,
	.is_valid_access = raw_tp_prog_is_valid_access,
};

const struct bpf_prog_ops raw_tracepoint_prog_ops = {
#ifdef CONFIG_NET
	.test_run = bpf_prog_test_run_raw_tp,
#endif
};

const struct bpf_verifier_ops tracing_verifier_ops = {
	.get_func_proto  = tracing_prog_func_proto,
	.is_valid_access = tracing_prog_is_valid_access,
};

const struct bpf_prog_ops tracing_prog_ops = {
	.test_run = bpf_prog_test_run_tracing,
};

static bool raw_tp_writable_prog_is_valid_access(int off, int size,
						 enum bpf_access_type type,
						 const struct bpf_prog *prog,
						 struct bpf_insn_access_aux *info)
{
	if (off == 0) {
		if (size != sizeof(u64) || type != BPF_READ)
			return false;
		info->reg_type = PTR_TO_TP_BUFFER;
	}
	return raw_tp_prog_is_valid_access(off, size, type, prog, info);
}

const struct bpf_verifier_ops raw_tracepoint_writable_verifier_ops = {
	.get_func_proto  = raw_tp_prog_func_proto,
	.is_valid_access = raw_tp_writable_prog_is_valid_access,
};

const struct bpf_prog_ops raw_tracepoint_writable_prog_ops = {
};

static bool pe_prog_is_valid_access(int off, int size, enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	const int size_u64 = sizeof(u64);

	if (off < 0 || off >= sizeof(struct bpf_perf_event_data))
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0) {
		if (sizeof(unsigned long) != 4)
			return false;
		if (size != 8)
			return false;
		if (off % size != 4)
			return false;
	}

	switch (off) {
	case bpf_ctx_range(struct bpf_perf_event_data, sample_period):
		bpf_ctx_record_field_size(info, size_u64);
		if (!bpf_ctx_narrow_access_ok(off, size, size_u64))
			return false;
		break;
	case bpf_ctx_range(struct bpf_perf_event_data, addr):
		bpf_ctx_record_field_size(info, size_u64);
		if (!bpf_ctx_narrow_access_ok(off, size, size_u64))
			return false;
		break;
	default:
		if (size != sizeof(long))
			return false;
	}

	return true;
}

static u32 pe_prog_convert_ctx_access(enum bpf_access_type type,
				      const struct bpf_insn *si,
				      struct bpf_insn *insn_buf,
				      struct bpf_prog *prog, u32 *target_size)
{
	struct bpf_insn *insn = insn_buf;

	switch (si->off) {
	case offsetof(struct bpf_perf_event_data, sample_period):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_perf_event_data_kern,
						       data), si->dst_reg, si->src_reg,
				      offsetof(struct bpf_perf_event_data_kern, data));
		*insn++ = BPF_LDX_MEM(BPF_DW, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct perf_sample_data, period, 8,
						     target_size));
		break;
	case offsetof(struct bpf_perf_event_data, addr):
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_perf_event_data_kern,
						       data), si->dst_reg, si->src_reg,
				      offsetof(struct bpf_perf_event_data_kern, data));
		*insn++ = BPF_LDX_MEM(BPF_DW, si->dst_reg, si->dst_reg,
				      bpf_target_off(struct perf_sample_data, addr, 8,
						     target_size));
		break;
	default:
		*insn++ = BPF_LDX_MEM(BPF_FIELD_SIZEOF(struct bpf_perf_event_data_kern,
						       regs), si->dst_reg, si->src_reg,
				      offsetof(struct bpf_perf_event_data_kern, regs));
		*insn++ = BPF_LDX_MEM(BPF_SIZEOF(long), si->dst_reg, si->dst_reg,
				      si->off);
		break;
	}

	return insn - insn_buf;
}

const struct bpf_verifier_ops perf_event_verifier_ops = {
	.get_func_proto		= pe_prog_func_proto,
	.is_valid_access	= pe_prog_is_valid_access,
	.convert_ctx_access	= pe_prog_convert_ctx_access,
};

const struct bpf_prog_ops perf_event_prog_ops = {
};

static DEFINE_MUTEX(bpf_event_mutex);

#define BPF_TRACE_MAX_PROGS 64

int perf_event_attach_bpf_prog(struct perf_event *event,
			       struct bpf_prog *prog,
			       u64 bpf_cookie)
{
	struct bpf_prog_array *old_array;
	struct bpf_prog_array *new_array;
	int ret = -EEXIST;

	/*
	 * Kprobe override only works if they are on the function entry,
	 * and only if they are on the opt-in list.
	 */
	if (prog->kprobe_override &&
	    (!trace_kprobe_on_func_entry(event->tp_event) ||
	     !trace_kprobe_error_injectable(event->tp_event)))
		return -EINVAL;

	mutex_lock(&bpf_event_mutex);

	if (event->prog)
		goto unlock;

	old_array = bpf_event_rcu_dereference(event->tp_event->prog_array);
	if (old_array &&
	    bpf_prog_array_length(old_array) >= BPF_TRACE_MAX_PROGS) {
		ret = -E2BIG;
		goto unlock;
	}

	ret = bpf_prog_array_copy(old_array, NULL, prog, bpf_cookie, &new_array);
	if (ret < 0)
		goto unlock;

	/* set the new array to event->tp_event and set event->prog */
	event->prog = prog;
	event->bpf_cookie = bpf_cookie;
	rcu_assign_pointer(event->tp_event->prog_array, new_array);
	bpf_prog_array_free_sleepable(old_array);

unlock:
	mutex_unlock(&bpf_event_mutex);
	return ret;
}

void perf_event_detach_bpf_prog(struct perf_event *event)
{
	struct bpf_prog_array *old_array;
	struct bpf_prog_array *new_array;
	int ret;

	mutex_lock(&bpf_event_mutex);

	if (!event->prog)
		goto unlock;

	old_array = bpf_event_rcu_dereference(event->tp_event->prog_array);
	ret = bpf_prog_array_copy(old_array, event->prog, NULL, 0, &new_array);
	if (ret == -ENOENT)
		goto unlock;
	if (ret < 0) {
		bpf_prog_array_delete_safe(old_array, event->prog);
	} else {
		rcu_assign_pointer(event->tp_event->prog_array, new_array);
		bpf_prog_array_free_sleepable(old_array);
	}

	bpf_prog_put(event->prog);
	event->prog = NULL;

unlock:
	mutex_unlock(&bpf_event_mutex);
}

int perf_event_query_prog_array(struct perf_event *event, void __user *info)
{
	struct perf_event_query_bpf __user *uquery = info;
	struct perf_event_query_bpf query = {};
	struct bpf_prog_array *progs;
	u32 *ids, prog_cnt, ids_len;
	int ret;

	if (!perfmon_capable())
		return -EPERM;
	if (event->attr.type != PERF_TYPE_TRACEPOINT)
		return -EINVAL;
	if (copy_from_user(&query, uquery, sizeof(query)))
		return -EFAULT;

	ids_len = query.ids_len;
	if (ids_len > BPF_TRACE_MAX_PROGS)
		return -E2BIG;
	ids = kcalloc(ids_len, sizeof(u32), GFP_USER | __GFP_NOWARN);
	if (!ids)
		return -ENOMEM;
	/*
	 * The above kcalloc returns ZERO_SIZE_PTR when ids_len = 0, which
	 * is required when user only wants to check for uquery->prog_cnt.
	 * There is no need to check for it since the case is handled
	 * gracefully in bpf_prog_array_copy_info.
	 */

	mutex_lock(&bpf_event_mutex);
	progs = bpf_event_rcu_dereference(event->tp_event->prog_array);
	ret = bpf_prog_array_copy_info(progs, ids, ids_len, &prog_cnt);
	mutex_unlock(&bpf_event_mutex);

	if (copy_to_user(&uquery->prog_cnt, &prog_cnt, sizeof(prog_cnt)) ||
	    copy_to_user(uquery->ids, ids, ids_len * sizeof(u32)))
		ret = -EFAULT;

	kfree(ids);
	return ret;
}

extern struct bpf_raw_event_map __start__bpf_raw_tp[];
extern struct bpf_raw_event_map __stop__bpf_raw_tp[];

struct bpf_raw_event_map *bpf_get_raw_tracepoint(const char *name)
{
	struct bpf_raw_event_map *btp = __start__bpf_raw_tp;

	for (; btp < __stop__bpf_raw_tp; btp++) {
		if (!strcmp(btp->tp->name, name))
			return btp;
	}

	return bpf_get_raw_tracepoint_module(name);
}

void bpf_put_raw_tracepoint(struct bpf_raw_event_map *btp)
{
	struct module *mod;

	preempt_disable();
	mod = __module_address((unsigned long)btp);
	module_put(mod);
	preempt_enable();
}

static __always_inline
void __bpf_trace_run(struct bpf_prog *prog, u64 *args)
{
	cant_sleep();
	if (unlikely(this_cpu_inc_return(*(prog->active)) != 1)) {
		bpf_prog_inc_misses_counter(prog);
		goto out;
	}
	rcu_read_lock();
	(void) bpf_prog_run(prog, args);
	rcu_read_unlock();
out:
	this_cpu_dec(*(prog->active));
}

#define UNPACK(...)			__VA_ARGS__
#define REPEAT_1(FN, DL, X, ...)	FN(X)
#define REPEAT_2(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_1(FN, DL, __VA_ARGS__)
#define REPEAT_3(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_2(FN, DL, __VA_ARGS__)
#define REPEAT_4(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_3(FN, DL, __VA_ARGS__)
#define REPEAT_5(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_4(FN, DL, __VA_ARGS__)
#define REPEAT_6(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_5(FN, DL, __VA_ARGS__)
#define REPEAT_7(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_6(FN, DL, __VA_ARGS__)
#define REPEAT_8(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_7(FN, DL, __VA_ARGS__)
#define REPEAT_9(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_8(FN, DL, __VA_ARGS__)
#define REPEAT_10(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_9(FN, DL, __VA_ARGS__)
#define REPEAT_11(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_10(FN, DL, __VA_ARGS__)
#define REPEAT_12(FN, DL, X, ...)	FN(X) UNPACK DL REPEAT_11(FN, DL, __VA_ARGS__)
#define REPEAT(X, FN, DL, ...)		REPEAT_##X(FN, DL, __VA_ARGS__)

#define SARG(X)		u64 arg##X
#define COPY(X)		args[X] = arg##X

#define __DL_COM	(,)
#define __DL_SEM	(;)

#define __SEQ_0_11	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11

#define BPF_TRACE_DEFN_x(x)						\
	void bpf_trace_run##x(struct bpf_prog *prog,			\
			      REPEAT(x, SARG, __DL_COM, __SEQ_0_11))	\
	{								\
		u64 args[x];						\
		REPEAT(x, COPY, __DL_SEM, __SEQ_0_11);			\
		__bpf_trace_run(prog, args);				\
	}								\
	EXPORT_SYMBOL_GPL(bpf_trace_run##x)
BPF_TRACE_DEFN_x(1);
BPF_TRACE_DEFN_x(2);
BPF_TRACE_DEFN_x(3);
BPF_TRACE_DEFN_x(4);
BPF_TRACE_DEFN_x(5);
BPF_TRACE_DEFN_x(6);
BPF_TRACE_DEFN_x(7);
BPF_TRACE_DEFN_x(8);
BPF_TRACE_DEFN_x(9);
BPF_TRACE_DEFN_x(10);
BPF_TRACE_DEFN_x(11);
BPF_TRACE_DEFN_x(12);

static int __bpf_probe_register(struct bpf_raw_event_map *btp, struct bpf_prog *prog)
{
	struct tracepoint *tp = btp->tp;

	/*
	 * check that program doesn't access arguments beyond what's
	 * available in this tracepoint
	 */
	if (prog->aux->max_ctx_offset > btp->num_args * sizeof(u64))
		return -EINVAL;

	if (prog->aux->max_tp_access > btp->writable_size)
		return -EINVAL;

	return tracepoint_probe_register_may_exist(tp, (void *)btp->bpf_func,
						   prog);
}

int bpf_probe_register(struct bpf_raw_event_map *btp, struct bpf_prog *prog)
{
	return __bpf_probe_register(btp, prog);
}

int bpf_probe_unregister(struct bpf_raw_event_map *btp, struct bpf_prog *prog)
{
	return tracepoint_probe_unregister(btp->tp, (void *)btp->bpf_func, prog);
}

int bpf_get_perf_event_info(const struct perf_event *event, u32 *prog_id,
			    u32 *fd_type, const char **buf,
			    u64 *probe_offset, u64 *probe_addr)
{
	bool is_tracepoint, is_syscall_tp;
	struct bpf_prog *prog;
	int flags, err = 0;

	prog = event->prog;
	if (!prog)
		return -ENOENT;

	/* not supporting BPF_PROG_TYPE_PERF_EVENT yet */
	if (prog->type == BPF_PROG_TYPE_PERF_EVENT)
		return -EOPNOTSUPP;

	*prog_id = prog->aux->id;
	flags = event->tp_event->flags;
	is_tracepoint = flags & TRACE_EVENT_FL_TRACEPOINT;
	is_syscall_tp = is_syscall_trace_event(event->tp_event);

	if (is_tracepoint || is_syscall_tp) {
		*buf = is_tracepoint ? event->tp_event->tp->name
				     : event->tp_event->name;
		/* We allow NULL pointer for tracepoint */
		if (fd_type)
			*fd_type = BPF_FD_TYPE_TRACEPOINT;
		if (probe_offset)
			*probe_offset = 0x0;
		if (probe_addr)
			*probe_addr = 0x0;
	} else {
		/* kprobe/uprobe */
		err = -EOPNOTSUPP;
#ifdef CONFIG_KPROBE_EVENTS
		if (flags & TRACE_EVENT_FL_KPROBE)
			err = bpf_get_kprobe_info(event, fd_type, buf,
						  probe_offset, probe_addr,
						  event->attr.type == PERF_TYPE_TRACEPOINT);
#endif
#ifdef CONFIG_UPROBE_EVENTS
		if (flags & TRACE_EVENT_FL_UPROBE)
			err = bpf_get_uprobe_info(event, fd_type, buf,
						  probe_offset, probe_addr,
						  event->attr.type == PERF_TYPE_TRACEPOINT);
#endif
	}

	return err;
}

static int __init send_signal_irq_work_init(void)
{
	int cpu;
	struct send_signal_irq_work *work;

	for_each_possible_cpu(cpu) {
		work = per_cpu_ptr(&send_signal_work, cpu);
		init_irq_work(&work->irq_work, do_bpf_send_signal);
	}
	return 0;
}

subsys_initcall(send_signal_irq_work_init);

#ifdef CONFIG_MODULES
static int bpf_event_notify(struct notifier_block *nb, unsigned long op,
			    void *module)
{
	struct bpf_trace_module *btm, *tmp;
	struct module *mod = module;
	int ret = 0;

	if (mod->num_bpf_raw_events == 0 ||
	    (op != MODULE_STATE_COMING && op != MODULE_STATE_GOING))
		goto out;

	mutex_lock(&bpf_module_mutex);

	switch (op) {
	case MODULE_STATE_COMING:
		btm = kzalloc(sizeof(*btm), GFP_KERNEL);
		if (btm) {
			btm->module = module;
			list_add(&btm->list, &bpf_trace_modules);
		} else {
			ret = -ENOMEM;
		}
		break;
	case MODULE_STATE_GOING:
		list_for_each_entry_safe(btm, tmp, &bpf_trace_modules, list) {
			if (btm->module == module) {
				list_del(&btm->list);
				kfree(btm);
				break;
			}
		}
		break;
	}

	mutex_unlock(&bpf_module_mutex);

out:
	return notifier_from_errno(ret);
}

static struct notifier_block bpf_module_nb = {
	.notifier_call = bpf_event_notify,
};

static int __init bpf_event_init(void)
{
	register_module_notifier(&bpf_module_nb);
	return 0;
}

fs_initcall(bpf_event_init);
#endif /* CONFIG_MODULES */

#ifdef CONFIG_FPROBE
struct bpf_kprobe_multi_link {
	struct bpf_link link;
	struct fprobe fp;
	unsigned long *addrs;
	u64 *cookies;
	u32 cnt;
	u32 mods_cnt;
	struct module **mods;
	u32 flags;
};

struct bpf_kprobe_multi_run_ctx {
	struct bpf_run_ctx run_ctx;
	struct bpf_kprobe_multi_link *link;
	unsigned long entry_ip;
};

struct user_syms {
	const char **syms;
	char *buf;
};

static int copy_user_syms(struct user_syms *us, unsigned long __user *usyms, u32 cnt)
{
	unsigned long __user usymbol;
	const char **syms = NULL;
	char *buf = NULL, *p;
	int err = -ENOMEM;
	unsigned int i;

	syms = kvmalloc_array(cnt, sizeof(*syms), GFP_KERNEL);
	if (!syms)
		goto error;

	buf = kvmalloc_array(cnt, KSYM_NAME_LEN, GFP_KERNEL);
	if (!buf)
		goto error;

	for (p = buf, i = 0; i < cnt; i++) {
		if (__get_user(usymbol, usyms + i)) {
			err = -EFAULT;
			goto error;
		}
		err = strncpy_from_user(p, (const char __user *) usymbol, KSYM_NAME_LEN);
		if (err == KSYM_NAME_LEN)
			err = -E2BIG;
		if (err < 0)
			goto error;
		syms[i] = p;
		p += err + 1;
	}

	us->syms = syms;
	us->buf = buf;
	return 0;

error:
	if (err) {
		kvfree(syms);
		kvfree(buf);
	}
	return err;
}

static void kprobe_multi_put_modules(struct module **mods, u32 cnt)
{
	u32 i;

	for (i = 0; i < cnt; i++)
		module_put(mods[i]);
}

static void free_user_syms(struct user_syms *us)
{
	kvfree(us->syms);
	kvfree(us->buf);
}

static void bpf_kprobe_multi_link_release(struct bpf_link *link)
{
	struct bpf_kprobe_multi_link *kmulti_link;

	kmulti_link = container_of(link, struct bpf_kprobe_multi_link, link);
	unregister_fprobe(&kmulti_link->fp);
	kprobe_multi_put_modules(kmulti_link->mods, kmulti_link->mods_cnt);
}

static void bpf_kprobe_multi_link_dealloc(struct bpf_link *link)
{
	struct bpf_kprobe_multi_link *kmulti_link;

	kmulti_link = container_of(link, struct bpf_kprobe_multi_link, link);
	kvfree(kmulti_link->addrs);
	kvfree(kmulti_link->cookies);
	kfree(kmulti_link->mods);
	kfree(kmulti_link);
}

static int bpf_kprobe_multi_link_fill_link_info(const struct bpf_link *link,
						struct bpf_link_info *info)
{
	u64 __user *uaddrs = u64_to_user_ptr(info->kprobe_multi.addrs);
	struct bpf_kprobe_multi_link *kmulti_link;
	u32 ucount = info->kprobe_multi.count;
	int err = 0, i;

	if (!uaddrs ^ !ucount)
		return -EINVAL;

	kmulti_link = container_of(link, struct bpf_kprobe_multi_link, link);
	info->kprobe_multi.count = kmulti_link->cnt;
	info->kprobe_multi.flags = kmulti_link->flags;

	if (!uaddrs)
		return 0;
	if (ucount < kmulti_link->cnt)
		err = -ENOSPC;
	else
		ucount = kmulti_link->cnt;

	if (kallsyms_show_value(current_cred())) {
		if (copy_to_user(uaddrs, kmulti_link->addrs, ucount * sizeof(u64)))
			return -EFAULT;
	} else {
		for (i = 0; i < ucount; i++) {
			if (put_user(0, uaddrs + i))
				return -EFAULT;
		}
	}
	return err;
}

static const struct bpf_link_ops bpf_kprobe_multi_link_lops = {
	.release = bpf_kprobe_multi_link_release,
	.dealloc = bpf_kprobe_multi_link_dealloc,
	.fill_link_info = bpf_kprobe_multi_link_fill_link_info,
};

static void bpf_kprobe_multi_cookie_swap(void *a, void *b, int size, const void *priv)
{
	const struct bpf_kprobe_multi_link *link = priv;
	unsigned long *addr_a = a, *addr_b = b;
	u64 *cookie_a, *cookie_b;

	cookie_a = link->cookies + (addr_a - link->addrs);
	cookie_b = link->cookies + (addr_b - link->addrs);

	/* swap addr_a/addr_b and cookie_a/cookie_b values */
	swap(*addr_a, *addr_b);
	swap(*cookie_a, *cookie_b);
}

static int bpf_kprobe_multi_addrs_cmp(const void *a, const void *b)
{
	const unsigned long *addr_a = a, *addr_b = b;

	if (*addr_a == *addr_b)
		return 0;
	return *addr_a < *addr_b ? -1 : 1;
}

static int bpf_kprobe_multi_cookie_cmp(const void *a, const void *b, const void *priv)
{
	return bpf_kprobe_multi_addrs_cmp(a, b);
}

static u64 bpf_kprobe_multi_cookie(struct bpf_run_ctx *ctx)
{
	struct bpf_kprobe_multi_run_ctx *run_ctx;
	struct bpf_kprobe_multi_link *link;
	u64 *cookie, entry_ip;
	unsigned long *addr;

	if (WARN_ON_ONCE(!ctx))
		return 0;
	run_ctx = container_of(current->bpf_ctx, struct bpf_kprobe_multi_run_ctx, run_ctx);
	link = run_ctx->link;
	if (!link->cookies)
		return 0;
	entry_ip = run_ctx->entry_ip;
	addr = bsearch(&entry_ip, link->addrs, link->cnt, sizeof(entry_ip),
		       bpf_kprobe_multi_addrs_cmp);
	if (!addr)
		return 0;
	cookie = link->cookies + (addr - link->addrs);
	return *cookie;
}

static u64 bpf_kprobe_multi_entry_ip(struct bpf_run_ctx *ctx)
{
	struct bpf_kprobe_multi_run_ctx *run_ctx;

	run_ctx = container_of(current->bpf_ctx, struct bpf_kprobe_multi_run_ctx, run_ctx);
	return run_ctx->entry_ip;
}

static int
kprobe_multi_link_prog_run(struct bpf_kprobe_multi_link *link,
			   unsigned long entry_ip, struct pt_regs *regs)
{
	struct bpf_kprobe_multi_run_ctx run_ctx = {
		.link = link,
		.entry_ip = entry_ip,
	};
	struct bpf_run_ctx *old_run_ctx;
	int err;

	if (unlikely(__this_cpu_inc_return(bpf_prog_active) != 1)) {
		err = 0;
		goto out;
	}

	migrate_disable();
	rcu_read_lock();
	old_run_ctx = bpf_set_run_ctx(&run_ctx.run_ctx);
	err = bpf_prog_run(link->link.prog, regs);
	bpf_reset_run_ctx(old_run_ctx);
	rcu_read_unlock();
	migrate_enable();

 out:
	__this_cpu_dec(bpf_prog_active);
	return err;
}

static int
kprobe_multi_link_handler(struct fprobe *fp, unsigned long fentry_ip,
			  unsigned long ret_ip, struct pt_regs *regs,
			  void *data)
{
	struct bpf_kprobe_multi_link *link;

	link = container_of(fp, struct bpf_kprobe_multi_link, fp);
	kprobe_multi_link_prog_run(link, get_entry_ip(fentry_ip), regs);
	return 0;
}

static void
kprobe_multi_link_exit_handler(struct fprobe *fp, unsigned long fentry_ip,
			       unsigned long ret_ip, struct pt_regs *regs,
			       void *data)
{
	struct bpf_kprobe_multi_link *link;

	link = container_of(fp, struct bpf_kprobe_multi_link, fp);
	kprobe_multi_link_prog_run(link, get_entry_ip(fentry_ip), regs);
}

static int symbols_cmp_r(const void *a, const void *b, const void *priv)
{
	const char **str_a = (const char **) a;
	const char **str_b = (const char **) b;

	return strcmp(*str_a, *str_b);
}

struct multi_symbols_sort {
	const char **funcs;
	u64 *cookies;
};

static void symbols_swap_r(void *a, void *b, int size, const void *priv)
{
	const struct multi_symbols_sort *data = priv;
	const char **name_a = a, **name_b = b;

	swap(*name_a, *name_b);

	/* If defined, swap also related cookies. */
	if (data->cookies) {
		u64 *cookie_a, *cookie_b;

		cookie_a = data->cookies + (name_a - data->funcs);
		cookie_b = data->cookies + (name_b - data->funcs);
		swap(*cookie_a, *cookie_b);
	}
}

struct modules_array {
	struct module **mods;
	int mods_cnt;
	int mods_cap;
};

static int add_module(struct modules_array *arr, struct module *mod)
{
	struct module **mods;

	if (arr->mods_cnt == arr->mods_cap) {
		arr->mods_cap = max(16, arr->mods_cap * 3 / 2);
		mods = krealloc_array(arr->mods, arr->mods_cap, sizeof(*mods), GFP_KERNEL);
		if (!mods)
			return -ENOMEM;
		arr->mods = mods;
	}

	arr->mods[arr->mods_cnt] = mod;
	arr->mods_cnt++;
	return 0;
}

static bool has_module(struct modules_array *arr, struct module *mod)
{
	int i;

	for (i = arr->mods_cnt - 1; i >= 0; i--) {
		if (arr->mods[i] == mod)
			return true;
	}
	return false;
}

static int get_modules_for_addrs(struct module ***mods, unsigned long *addrs, u32 addrs_cnt)
{
	struct modules_array arr = {};
	u32 i, err = 0;

	for (i = 0; i < addrs_cnt; i++) {
		struct module *mod;

		preempt_disable();
		mod = __module_address(addrs[i]);
		/* Either no module or we it's already stored  */
		if (!mod || has_module(&arr, mod)) {
			preempt_enable();
			continue;
		}
		if (!try_module_get(mod))
			err = -EINVAL;
		preempt_enable();
		if (err)
			break;
		err = add_module(&arr, mod);
		if (err) {
			module_put(mod);
			break;
		}
	}

	/* We return either err < 0 in case of error, ... */
	if (err) {
		kprobe_multi_put_modules(arr.mods, arr.mods_cnt);
		kfree(arr.mods);
		return err;
	}

	/* or number of modules found if everything is ok. */
	*mods = arr.mods;
	return arr.mods_cnt;
}

static int addrs_check_error_injection_list(unsigned long *addrs, u32 cnt)
{
	u32 i;

	for (i = 0; i < cnt; i++) {
		if (!within_error_injection_list(addrs[i]))
			return -EINVAL;
	}
	return 0;
}

int bpf_kprobe_multi_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_kprobe_multi_link *link = NULL;
	struct bpf_link_primer link_primer;
	void __user *ucookies;
	unsigned long *addrs;
	u32 flags, cnt, size;
	void __user *uaddrs;
	u64 *cookies = NULL;
	void __user *usyms;
	int err;

	/* no support for 32bit archs yet */
	if (sizeof(u64) != sizeof(void *))
		return -EOPNOTSUPP;

	if (prog->expected_attach_type != BPF_TRACE_KPROBE_MULTI)
		return -EINVAL;

	flags = attr->link_create.kprobe_multi.flags;
	if (flags & ~BPF_F_KPROBE_MULTI_RETURN)
		return -EINVAL;

	uaddrs = u64_to_user_ptr(attr->link_create.kprobe_multi.addrs);
	usyms = u64_to_user_ptr(attr->link_create.kprobe_multi.syms);
	if (!!uaddrs == !!usyms)
		return -EINVAL;

	cnt = attr->link_create.kprobe_multi.cnt;
	if (!cnt)
		return -EINVAL;
	if (cnt > MAX_KPROBE_MULTI_CNT)
		return -E2BIG;

	size = cnt * sizeof(*addrs);
	addrs = kvmalloc_array(cnt, sizeof(*addrs), GFP_KERNEL);
	if (!addrs)
		return -ENOMEM;

	ucookies = u64_to_user_ptr(attr->link_create.kprobe_multi.cookies);
	if (ucookies) {
		cookies = kvmalloc_array(cnt, sizeof(*addrs), GFP_KERNEL);
		if (!cookies) {
			err = -ENOMEM;
			goto error;
		}
		if (copy_from_user(cookies, ucookies, size)) {
			err = -EFAULT;
			goto error;
		}
	}

	if (uaddrs) {
		if (copy_from_user(addrs, uaddrs, size)) {
			err = -EFAULT;
			goto error;
		}
	} else {
		struct multi_symbols_sort data = {
			.cookies = cookies,
		};
		struct user_syms us;

		err = copy_user_syms(&us, usyms, cnt);
		if (err)
			goto error;

		if (cookies)
			data.funcs = us.syms;

		sort_r(us.syms, cnt, sizeof(*us.syms), symbols_cmp_r,
		       symbols_swap_r, &data);

		err = ftrace_lookup_symbols(us.syms, cnt, addrs);
		free_user_syms(&us);
		if (err)
			goto error;
	}

	if (prog->kprobe_override && addrs_check_error_injection_list(addrs, cnt)) {
		err = -EINVAL;
		goto error;
	}

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link) {
		err = -ENOMEM;
		goto error;
	}

	bpf_link_init(&link->link, BPF_LINK_TYPE_KPROBE_MULTI,
		      &bpf_kprobe_multi_link_lops, prog);

	err = bpf_link_prime(&link->link, &link_primer);
	if (err)
		goto error;

	if (flags & BPF_F_KPROBE_MULTI_RETURN)
		link->fp.exit_handler = kprobe_multi_link_exit_handler;
	else
		link->fp.entry_handler = kprobe_multi_link_handler;

	link->addrs = addrs;
	link->cookies = cookies;
	link->cnt = cnt;
	link->flags = flags;

	if (cookies) {
		/*
		 * Sorting addresses will trigger sorting cookies as well
		 * (check bpf_kprobe_multi_cookie_swap). This way we can
		 * find cookie based on the address in bpf_get_attach_cookie
		 * helper.
		 */
		sort_r(addrs, cnt, sizeof(*addrs),
		       bpf_kprobe_multi_cookie_cmp,
		       bpf_kprobe_multi_cookie_swap,
		       link);
	}

	err = get_modules_for_addrs(&link->mods, addrs, cnt);
	if (err < 0) {
		bpf_link_cleanup(&link_primer);
		return err;
	}
	link->mods_cnt = err;

	err = register_fprobe_ips(&link->fp, addrs, cnt);
	if (err) {
		kprobe_multi_put_modules(link->mods, link->mods_cnt);
		bpf_link_cleanup(&link_primer);
		return err;
	}

	return bpf_link_settle(&link_primer);

error:
	kfree(link);
	kvfree(addrs);
	kvfree(cookies);
	return err;
}
#else /* !CONFIG_FPROBE */
int bpf_kprobe_multi_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}
static u64 bpf_kprobe_multi_cookie(struct bpf_run_ctx *ctx)
{
	return 0;
}
static u64 bpf_kprobe_multi_entry_ip(struct bpf_run_ctx *ctx)
{
	return 0;
}
#endif

#ifdef CONFIG_UPROBES
struct bpf_uprobe_multi_link;

struct bpf_uprobe {
	struct bpf_uprobe_multi_link *link;
	loff_t offset;
	u64 cookie;
	struct uprobe_consumer consumer;
};

struct bpf_uprobe_multi_link {
	struct path path;
	struct bpf_link link;
	u32 cnt;
	struct bpf_uprobe *uprobes;
	struct task_struct *task;
};

struct bpf_uprobe_multi_run_ctx {
	struct bpf_run_ctx run_ctx;
	unsigned long entry_ip;
	struct bpf_uprobe *uprobe;
};

static void bpf_uprobe_unregister(struct path *path, struct bpf_uprobe *uprobes,
				  u32 cnt)
{
	u32 i;

	for (i = 0; i < cnt; i++) {
		uprobe_unregister(d_real_inode(path->dentry), uprobes[i].offset,
				  &uprobes[i].consumer);
	}
}

static void bpf_uprobe_multi_link_release(struct bpf_link *link)
{
	struct bpf_uprobe_multi_link *umulti_link;

	umulti_link = container_of(link, struct bpf_uprobe_multi_link, link);
	bpf_uprobe_unregister(&umulti_link->path, umulti_link->uprobes, umulti_link->cnt);
}

static void bpf_uprobe_multi_link_dealloc(struct bpf_link *link)
{
	struct bpf_uprobe_multi_link *umulti_link;

	umulti_link = container_of(link, struct bpf_uprobe_multi_link, link);
	if (umulti_link->task)
		put_task_struct(umulti_link->task);
	path_put(&umulti_link->path);
	kvfree(umulti_link->uprobes);
	kfree(umulti_link);
}

static const struct bpf_link_ops bpf_uprobe_multi_link_lops = {
	.release = bpf_uprobe_multi_link_release,
	.dealloc = bpf_uprobe_multi_link_dealloc,
};

static int uprobe_prog_run(struct bpf_uprobe *uprobe,
			   unsigned long entry_ip,
			   struct pt_regs *regs)
{
	struct bpf_uprobe_multi_link *link = uprobe->link;
	struct bpf_uprobe_multi_run_ctx run_ctx = {
		.entry_ip = entry_ip,
		.uprobe = uprobe,
	};
	struct bpf_prog *prog = link->link.prog;
	bool sleepable = prog->aux->sleepable;
	struct bpf_run_ctx *old_run_ctx;
	int err = 0;

	if (link->task && current != link->task)
		return 0;

	if (sleepable)
		rcu_read_lock_trace();
	else
		rcu_read_lock();

	migrate_disable();

	old_run_ctx = bpf_set_run_ctx(&run_ctx.run_ctx);
	err = bpf_prog_run(link->link.prog, regs);
	bpf_reset_run_ctx(old_run_ctx);

	migrate_enable();

	if (sleepable)
		rcu_read_unlock_trace();
	else
		rcu_read_unlock();
	return err;
}

static bool
uprobe_multi_link_filter(struct uprobe_consumer *con, enum uprobe_filter_ctx ctx,
			 struct mm_struct *mm)
{
	struct bpf_uprobe *uprobe;

	uprobe = container_of(con, struct bpf_uprobe, consumer);
	return uprobe->link->task->mm == mm;
}

static int
uprobe_multi_link_handler(struct uprobe_consumer *con, struct pt_regs *regs)
{
	struct bpf_uprobe *uprobe;

	uprobe = container_of(con, struct bpf_uprobe, consumer);
	return uprobe_prog_run(uprobe, instruction_pointer(regs), regs);
}

static int
uprobe_multi_link_ret_handler(struct uprobe_consumer *con, unsigned long func, struct pt_regs *regs)
{
	struct bpf_uprobe *uprobe;

	uprobe = container_of(con, struct bpf_uprobe, consumer);
	return uprobe_prog_run(uprobe, func, regs);
}

static u64 bpf_uprobe_multi_entry_ip(struct bpf_run_ctx *ctx)
{
	struct bpf_uprobe_multi_run_ctx *run_ctx;

	run_ctx = container_of(current->bpf_ctx, struct bpf_uprobe_multi_run_ctx, run_ctx);
	return run_ctx->entry_ip;
}

static u64 bpf_uprobe_multi_cookie(struct bpf_run_ctx *ctx)
{
	struct bpf_uprobe_multi_run_ctx *run_ctx;

	run_ctx = container_of(current->bpf_ctx, struct bpf_uprobe_multi_run_ctx, run_ctx);
	return run_ctx->uprobe->cookie;
}

int bpf_uprobe_multi_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct bpf_uprobe_multi_link *link = NULL;
	unsigned long __user *uref_ctr_offsets;
	unsigned long *ref_ctr_offsets = NULL;
	struct bpf_link_primer link_primer;
	struct bpf_uprobe *uprobes = NULL;
	struct task_struct *task = NULL;
	unsigned long __user *uoffsets;
	u64 __user *ucookies;
	void __user *upath;
	u32 flags, cnt, i;
	struct path path;
	char *name;
	pid_t pid;
	int err;

	/* no support for 32bit archs yet */
	if (sizeof(u64) != sizeof(void *))
		return -EOPNOTSUPP;

	if (prog->expected_attach_type != BPF_TRACE_UPROBE_MULTI)
		return -EINVAL;

	flags = attr->link_create.uprobe_multi.flags;
	if (flags & ~BPF_F_UPROBE_MULTI_RETURN)
		return -EINVAL;

	/*
	 * path, offsets and cnt are mandatory,
	 * ref_ctr_offsets and cookies are optional
	 */
	upath = u64_to_user_ptr(attr->link_create.uprobe_multi.path);
	uoffsets = u64_to_user_ptr(attr->link_create.uprobe_multi.offsets);
	cnt = attr->link_create.uprobe_multi.cnt;

	if (!upath || !uoffsets || !cnt)
		return -EINVAL;
	if (cnt > MAX_UPROBE_MULTI_CNT)
		return -E2BIG;

	uref_ctr_offsets = u64_to_user_ptr(attr->link_create.uprobe_multi.ref_ctr_offsets);
	ucookies = u64_to_user_ptr(attr->link_create.uprobe_multi.cookies);

	name = strndup_user(upath, PATH_MAX);
	if (IS_ERR(name)) {
		err = PTR_ERR(name);
		return err;
	}

	err = kern_path(name, LOOKUP_FOLLOW, &path);
	kfree(name);
	if (err)
		return err;

	if (!d_is_reg(path.dentry)) {
		err = -EBADF;
		goto error_path_put;
	}

	pid = attr->link_create.uprobe_multi.pid;
	if (pid) {
		rcu_read_lock();
		task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
		rcu_read_unlock();
		if (!task) {
			err = -ESRCH;
			goto error_path_put;
		}
	}

	err = -ENOMEM;

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	uprobes = kvcalloc(cnt, sizeof(*uprobes), GFP_KERNEL);

	if (!uprobes || !link)
		goto error_free;

	if (uref_ctr_offsets) {
		ref_ctr_offsets = kvcalloc(cnt, sizeof(*ref_ctr_offsets), GFP_KERNEL);
		if (!ref_ctr_offsets)
			goto error_free;
	}

	for (i = 0; i < cnt; i++) {
		if (ucookies && __get_user(uprobes[i].cookie, ucookies + i)) {
			err = -EFAULT;
			goto error_free;
		}
		if (uref_ctr_offsets && __get_user(ref_ctr_offsets[i], uref_ctr_offsets + i)) {
			err = -EFAULT;
			goto error_free;
		}
		if (__get_user(uprobes[i].offset, uoffsets + i)) {
			err = -EFAULT;
			goto error_free;
		}

		uprobes[i].link = link;

		if (flags & BPF_F_UPROBE_MULTI_RETURN)
			uprobes[i].consumer.ret_handler = uprobe_multi_link_ret_handler;
		else
			uprobes[i].consumer.handler = uprobe_multi_link_handler;

		if (pid)
			uprobes[i].consumer.filter = uprobe_multi_link_filter;
	}

	link->cnt = cnt;
	link->uprobes = uprobes;
	link->path = path;
	link->task = task;

	bpf_link_init(&link->link, BPF_LINK_TYPE_UPROBE_MULTI,
		      &bpf_uprobe_multi_link_lops, prog);

	for (i = 0; i < cnt; i++) {
		err = uprobe_register_refctr(d_real_inode(link->path.dentry),
					     uprobes[i].offset,
					     ref_ctr_offsets ? ref_ctr_offsets[i] : 0,
					     &uprobes[i].consumer);
		if (err) {
			bpf_uprobe_unregister(&path, uprobes, i);
			goto error_free;
		}
	}

	err = bpf_link_prime(&link->link, &link_primer);
	if (err)
		goto error_free;

	kvfree(ref_ctr_offsets);
	return bpf_link_settle(&link_primer);

error_free:
	kvfree(ref_ctr_offsets);
	kvfree(uprobes);
	kfree(link);
	if (task)
		put_task_struct(task);
error_path_put:
	path_put(&path);
	return err;
}
#else /* !CONFIG_UPROBES */
int bpf_uprobe_multi_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}
static u64 bpf_uprobe_multi_cookie(struct bpf_run_ctx *ctx)
{
	return 0;
}
static u64 bpf_uprobe_multi_entry_ip(struct bpf_run_ctx *ctx)
{
	return 0;
}
#endif /* CONFIG_UPROBES */
