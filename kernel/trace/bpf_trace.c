/* Copyright (c) 2011-2015 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include "trace.h"

/**
 * trace_call_bpf - invoke BPF program
 * @prog: BPF program
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
unsigned int trace_call_bpf(struct bpf_prog *prog, void *ctx)
{
	unsigned int ret;

	if (in_nmi()) /* not supported yet */
		return 1;

	preempt_disable();

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

	rcu_read_lock();
	ret = BPF_PROG_RUN(prog, ctx);
	rcu_read_unlock();

 out:
	__this_cpu_dec(bpf_prog_active);
	preempt_enable();

	return ret;
}
EXPORT_SYMBOL_GPL(trace_call_bpf);

static u64 bpf_probe_read(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	void *dst = (void *) (long) r1;
	int ret, size = (int) r2;
	void *unsafe_ptr = (void *) (long) r3;

	ret = probe_kernel_read(dst, unsafe_ptr, size);
	if (unlikely(ret < 0))
		memset(dst, 0, size);

	return ret;
}

static const struct bpf_func_proto bpf_probe_read_proto = {
	.func		= bpf_probe_read,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_RAW_STACK,
	.arg2_type	= ARG_CONST_STACK_SIZE,
	.arg3_type	= ARG_ANYTHING,
};

/*
 * limited trace_printk()
 * only %d %u %x %ld %lu %lx %lld %llu %llx %p %s conversion specifiers allowed
 */
static u64 bpf_trace_printk(u64 r1, u64 fmt_size, u64 r3, u64 r4, u64 r5)
{
	char *fmt = (char *) (long) r1;
	bool str_seen = false;
	int mod[3] = {};
	int fmt_cnt = 0;
	u64 unsafe_addr;
	char buf[64];
	int i;

	/*
	 * bpf_check()->check_func_arg()->check_stack_boundary()
	 * guarantees that fmt points to bpf program stack,
	 * fmt_size bytes of it were initialized and fmt_size > 0
	 */
	if (fmt[--fmt_size] != 0)
		return -EINVAL;

	/* check format string for allowed specifiers */
	for (i = 0; i < fmt_size; i++) {
		if ((!isprint(fmt[i]) && !isspace(fmt[i])) || !isascii(fmt[i]))
			return -EINVAL;

		if (fmt[i] != '%')
			continue;

		if (fmt_cnt >= 3)
			return -EINVAL;

		/* fmt[i] != 0 && fmt[last] == 0, so we can access fmt[i + 1] */
		i++;
		if (fmt[i] == 'l') {
			mod[fmt_cnt]++;
			i++;
		} else if (fmt[i] == 'p' || fmt[i] == 's') {
			mod[fmt_cnt]++;
			i++;
			if (!isspace(fmt[i]) && !ispunct(fmt[i]) && fmt[i] != 0)
				return -EINVAL;
			fmt_cnt++;
			if (fmt[i - 1] == 's') {
				if (str_seen)
					/* allow only one '%s' per fmt string */
					return -EINVAL;
				str_seen = true;

				switch (fmt_cnt) {
				case 1:
					unsafe_addr = r3;
					r3 = (long) buf;
					break;
				case 2:
					unsafe_addr = r4;
					r4 = (long) buf;
					break;
				case 3:
					unsafe_addr = r5;
					r5 = (long) buf;
					break;
				}
				buf[0] = 0;
				strncpy_from_unsafe(buf,
						    (void *) (long) unsafe_addr,
						    sizeof(buf));
			}
			continue;
		}

		if (fmt[i] == 'l') {
			mod[fmt_cnt]++;
			i++;
		}

		if (fmt[i] != 'd' && fmt[i] != 'u' && fmt[i] != 'x')
			return -EINVAL;
		fmt_cnt++;
	}

	return __trace_printk(1/* fake ip will not be printed */, fmt,
			      mod[0] == 2 ? r3 : mod[0] == 1 ? (long) r3 : (u32) r3,
			      mod[1] == 2 ? r4 : mod[1] == 1 ? (long) r4 : (u32) r4,
			      mod[2] == 2 ? r5 : mod[2] == 1 ? (long) r5 : (u32) r5);
}

static const struct bpf_func_proto bpf_trace_printk_proto = {
	.func		= bpf_trace_printk,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_STACK,
	.arg2_type	= ARG_CONST_STACK_SIZE,
};

const struct bpf_func_proto *bpf_get_trace_printk_proto(void)
{
	/*
	 * this program might be calling bpf_trace_printk,
	 * so allocate per-cpu printk buffers
	 */
	trace_printk_init_buffers();

	return &bpf_trace_printk_proto;
}

static u64 bpf_perf_event_read(u64 r1, u64 index, u64 r3, u64 r4, u64 r5)
{
	struct bpf_map *map = (struct bpf_map *) (unsigned long) r1;
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	struct perf_event *event;
	struct file *file;

	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	file = READ_ONCE(array->ptrs[index]);
	if (unlikely(!file))
		return -ENOENT;

	event = file->private_data;

	/* make sure event is local and doesn't have pmu::count */
	if (event->oncpu != smp_processor_id() ||
	    event->pmu->count)
		return -EINVAL;

	/*
	 * we don't know if the function is run successfully by the
	 * return value. It can be judged in other places, such as
	 * eBPF programs.
	 */
	return perf_event_read_local(event);
}

static const struct bpf_func_proto bpf_perf_event_read_proto = {
	.func		= bpf_perf_event_read,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_ANYTHING,
};

static u64 bpf_perf_event_output(u64 r1, u64 r2, u64 flags, u64 r4, u64 size)
{
	struct pt_regs *regs = (struct pt_regs *) (long) r1;
	struct bpf_map *map = (struct bpf_map *) (long) r2;
	struct bpf_array *array = container_of(map, struct bpf_array, map);
	u64 index = flags & BPF_F_INDEX_MASK;
	void *data = (void *) (long) r4;
	struct perf_sample_data sample_data;
	struct perf_event *event;
	struct file *file;
	struct perf_raw_record raw = {
		.size = size,
		.data = data,
	};

	if (unlikely(flags & ~(BPF_F_INDEX_MASK)))
		return -EINVAL;
	if (index == BPF_F_CURRENT_CPU)
		index = raw_smp_processor_id();
	if (unlikely(index >= array->map.max_entries))
		return -E2BIG;

	file = READ_ONCE(array->ptrs[index]);
	if (unlikely(!file))
		return -ENOENT;

	event = file->private_data;

	if (unlikely(event->attr.type != PERF_TYPE_SOFTWARE ||
		     event->attr.config != PERF_COUNT_SW_BPF_OUTPUT))
		return -EINVAL;

	if (unlikely(event->oncpu != smp_processor_id()))
		return -EOPNOTSUPP;

	perf_sample_data_init(&sample_data, 0, 0);
	sample_data.raw = &raw;
	perf_event_output(event, &sample_data, regs);
	return 0;
}

static const struct bpf_func_proto bpf_perf_event_output_proto = {
	.func		= bpf_perf_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_STACK,
	.arg5_type	= ARG_CONST_STACK_SIZE,
};

static DEFINE_PER_CPU(struct pt_regs, bpf_pt_regs);

static u64 bpf_event_output(u64 r1, u64 r2, u64 flags, u64 r4, u64 size)
{
	struct pt_regs *regs = this_cpu_ptr(&bpf_pt_regs);

	perf_fetch_caller_regs(regs);

	return bpf_perf_event_output((long)regs, r2, flags, r4, size);
}

static const struct bpf_func_proto bpf_event_output_proto = {
	.func		= bpf_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_STACK,
	.arg5_type	= ARG_CONST_STACK_SIZE,
};

const struct bpf_func_proto *bpf_get_event_output_proto(void)
{
	return &bpf_event_output_proto;
}

static const struct bpf_func_proto *tracing_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_map_lookup_elem:
		return &bpf_map_lookup_elem_proto;
	case BPF_FUNC_map_update_elem:
		return &bpf_map_update_elem_proto;
	case BPF_FUNC_map_delete_elem:
		return &bpf_map_delete_elem_proto;
	case BPF_FUNC_probe_read:
		return &bpf_probe_read_proto;
	case BPF_FUNC_ktime_get_ns:
		return &bpf_ktime_get_ns_proto;
	case BPF_FUNC_tail_call:
		return &bpf_tail_call_proto;
	case BPF_FUNC_get_current_pid_tgid:
		return &bpf_get_current_pid_tgid_proto;
	case BPF_FUNC_get_current_uid_gid:
		return &bpf_get_current_uid_gid_proto;
	case BPF_FUNC_get_current_comm:
		return &bpf_get_current_comm_proto;
	case BPF_FUNC_trace_printk:
		return bpf_get_trace_printk_proto();
	case BPF_FUNC_get_smp_processor_id:
		return &bpf_get_smp_processor_id_proto;
	case BPF_FUNC_perf_event_read:
		return &bpf_perf_event_read_proto;
	default:
		return NULL;
	}
}

static const struct bpf_func_proto *kprobe_prog_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto;
	default:
		return tracing_func_proto(func_id);
	}
}

/* bpf+kprobe programs can access fields of 'struct pt_regs' */
static bool kprobe_prog_is_valid_access(int off, int size, enum bpf_access_type type)
{
	/* check bounds */
	if (off < 0 || off >= sizeof(struct pt_regs))
		return false;

	/* only read is allowed */
	if (type != BPF_READ)
		return false;

	/* disallow misaligned access */
	if (off % size != 0)
		return false;

	return true;
}

static const struct bpf_verifier_ops kprobe_prog_ops = {
	.get_func_proto  = kprobe_prog_func_proto,
	.is_valid_access = kprobe_prog_is_valid_access,
};

static struct bpf_prog_type_list kprobe_tl = {
	.ops	= &kprobe_prog_ops,
	.type	= BPF_PROG_TYPE_KPROBE,
};

static u64 bpf_perf_event_output_tp(u64 r1, u64 r2, u64 index, u64 r4, u64 size)
{
	/*
	 * r1 points to perf tracepoint buffer where first 8 bytes are hidden
	 * from bpf program and contain a pointer to 'struct pt_regs'. Fetch it
	 * from there and call the same bpf_perf_event_output() helper
	 */
	u64 ctx = *(long *)(uintptr_t)r1;

	return bpf_perf_event_output(ctx, r2, index, r4, size);
}

static const struct bpf_func_proto bpf_perf_event_output_proto_tp = {
	.func		= bpf_perf_event_output_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_STACK,
	.arg5_type	= ARG_CONST_STACK_SIZE,
};

static u64 bpf_get_stackid_tp(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
{
	u64 ctx = *(long *)(uintptr_t)r1;

	return bpf_get_stackid(ctx, r2, r3, r4, r5);
}

static const struct bpf_func_proto bpf_get_stackid_proto_tp = {
	.func		= bpf_get_stackid_tp,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *tp_prog_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_perf_event_output:
		return &bpf_perf_event_output_proto_tp;
	case BPF_FUNC_get_stackid:
		return &bpf_get_stackid_proto_tp;
	default:
		return tracing_func_proto(func_id);
	}
}

static bool tp_prog_is_valid_access(int off, int size, enum bpf_access_type type)
{
	if (off < sizeof(void *) || off >= PERF_MAX_TRACE_SIZE)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	return true;
}

static const struct bpf_verifier_ops tracepoint_prog_ops = {
	.get_func_proto  = tp_prog_func_proto,
	.is_valid_access = tp_prog_is_valid_access,
};

static struct bpf_prog_type_list tracepoint_tl = {
	.ops	= &tracepoint_prog_ops,
	.type	= BPF_PROG_TYPE_TRACEPOINT,
};

static int __init register_kprobe_prog_ops(void)
{
	bpf_register_prog_type(&kprobe_tl);
	bpf_register_prog_type(&tracepoint_tl);
	return 0;
}
late_initcall(register_kprobe_prog_ops);
