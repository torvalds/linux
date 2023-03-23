// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2011-2015 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bpf.h>
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

#include <uapi/linux/bpf.h>
#include <uapi/linux/btf.h>

#include <asm/tlb.h>

#include "trace_probe.h"
#include "trace.h"

#define CREATE_TRACE_POINTS
#include "bpf_trace.h"

#define bpf_event_rcu_dereference(p)					\
	rcu_dereference_protected(p, lockdep_is_held(&bpf_event_mutex))

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
	 * a heurisitc to speed up execution.
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
	ret = BPF_PROG_RUN_ARRAY_CHECK(call->prog_array, ctx, BPF_PROG_RUN);

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

static __always_inline int
bpf_probe_read_kernel_common(void *dst, u32 size, const void *unsafe_ptr)
{
	int ret;

	ret = copy_from_kernel_nofault(dst, unsafe_ptr, size);
	if (unlikely(ret < 0))
		memset(dst, 0, size);
	return ret;
}

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
	if (unlikely(uaccess_kernel()))
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
	.arg2_type	= ARG_PTR_TO_MEM,
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

static void bpf_trace_copy_string(char *buf, void *unsafe_ptr, char fmt_ptype,
		size_t bufsz)
{
	void __user *user_ptr = (__force void __user *)unsafe_ptr;

	buf[0] = 0;

	switch (fmt_ptype) {
	case 's':
#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
		if ((unsigned long)unsafe_ptr < TASK_SIZE) {
			strncpy_from_user_nofault(buf, user_ptr, bufsz);
			break;
		}
		fallthrough;
#endif
	case 'k':
		strncpy_from_kernel_nofault(buf, unsafe_ptr, bufsz);
		break;
	case 'u':
		strncpy_from_user_nofault(buf, user_ptr, bufsz);
		break;
	}
}

static DEFINE_RAW_SPINLOCK(trace_printk_lock);

#define BPF_TRACE_PRINTK_SIZE   1024

static __printf(1, 0) int bpf_do_trace_printk(const char *fmt, ...)
{
	static char buf[BPF_TRACE_PRINTK_SIZE];
	unsigned long flags;
	va_list ap;
	int ret;

	raw_spin_lock_irqsave(&trace_printk_lock, flags);
	va_start(ap, fmt);
	ret = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	/* vsnprintf() will not append null for zero-length strings */
	if (ret == 0)
		buf[0] = '\0';
	trace_bpf_trace_printk(buf);
	raw_spin_unlock_irqrestore(&trace_printk_lock, flags);

	return ret;
}

/*
 * Only limited trace_printk() conversion specifiers allowed:
 * %d %i %u %x %ld %li %lu %lx %lld %lli %llu %llx %p %pB %pks %pus %s
 */
BPF_CALL_5(bpf_trace_printk, char *, fmt, u32, fmt_size, u64, arg1,
	   u64, arg2, u64, arg3)
{
	int i, mod[3] = {}, fmt_cnt = 0;
	char buf[64], fmt_ptype;
	void *unsafe_ptr = NULL;
	bool str_seen = false;

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
		} else if (fmt[i] == 'p') {
			mod[fmt_cnt]++;
			if ((fmt[i + 1] == 'k' ||
			     fmt[i + 1] == 'u') &&
			    fmt[i + 2] == 's') {
				fmt_ptype = fmt[i + 1];
				i += 2;
				goto fmt_str;
			}

			if (fmt[i + 1] == 'B') {
				i++;
				goto fmt_next;
			}

			/* disallow any further format extensions */
			if (fmt[i + 1] != 0 &&
			    !isspace(fmt[i + 1]) &&
			    !ispunct(fmt[i + 1]))
				return -EINVAL;

			goto fmt_next;
		} else if (fmt[i] == 's') {
			mod[fmt_cnt]++;
			fmt_ptype = fmt[i];
fmt_str:
			if (str_seen)
				/* allow only one '%s' per fmt string */
				return -EINVAL;
			str_seen = true;

			if (fmt[i + 1] != 0 &&
			    !isspace(fmt[i + 1]) &&
			    !ispunct(fmt[i + 1]))
				return -EINVAL;

			switch (fmt_cnt) {
			case 0:
				unsafe_ptr = (void *)(long)arg1;
				arg1 = (long)buf;
				break;
			case 1:
				unsafe_ptr = (void *)(long)arg2;
				arg2 = (long)buf;
				break;
			case 2:
				unsafe_ptr = (void *)(long)arg3;
				arg3 = (long)buf;
				break;
			}

			bpf_trace_copy_string(buf, unsafe_ptr, fmt_ptype,
					sizeof(buf));
			goto fmt_next;
		}

		if (fmt[i] == 'l') {
			mod[fmt_cnt]++;
			i++;
		}

		if (fmt[i] != 'i' && fmt[i] != 'd' &&
		    fmt[i] != 'u' && fmt[i] != 'x')
			return -EINVAL;
fmt_next:
		fmt_cnt++;
	}

/* Horrid workaround for getting va_list handling working with different
 * argument type combinations generically for 32 and 64 bit archs.
 */
#define __BPF_TP_EMIT()	__BPF_ARG3_TP()
#define __BPF_TP(...)							\
	bpf_do_trace_printk(fmt, ##__VA_ARGS__)

#define __BPF_ARG1_TP(...)						\
	((mod[0] == 2 || (mod[0] == 1 && __BITS_PER_LONG == 64))	\
	  ? __BPF_TP(arg1, ##__VA_ARGS__)				\
	  : ((mod[0] == 1 || (mod[0] == 0 && __BITS_PER_LONG == 32))	\
	      ? __BPF_TP((long)arg1, ##__VA_ARGS__)			\
	      : __BPF_TP((u32)arg1, ##__VA_ARGS__)))

#define __BPF_ARG2_TP(...)						\
	((mod[1] == 2 || (mod[1] == 1 && __BITS_PER_LONG == 64))	\
	  ? __BPF_ARG1_TP(arg2, ##__VA_ARGS__)				\
	  : ((mod[1] == 1 || (mod[1] == 0 && __BITS_PER_LONG == 32))	\
	      ? __BPF_ARG1_TP((long)arg2, ##__VA_ARGS__)		\
	      : __BPF_ARG1_TP((u32)arg2, ##__VA_ARGS__)))

#define __BPF_ARG3_TP(...)						\
	((mod[2] == 2 || (mod[2] == 1 && __BITS_PER_LONG == 64))	\
	  ? __BPF_ARG2_TP(arg3, ##__VA_ARGS__)				\
	  : ((mod[2] == 1 || (mod[2] == 0 && __BITS_PER_LONG == 32))	\
	      ? __BPF_ARG2_TP((long)arg3, ##__VA_ARGS__)		\
	      : __BPF_ARG2_TP((u32)arg3, ##__VA_ARGS__)))

	return __BPF_TP_EMIT();
}

static const struct bpf_func_proto bpf_trace_printk_proto = {
	.func		= bpf_trace_printk,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_MEM,
	.arg2_type	= ARG_CONST_SIZE,
};

const struct bpf_func_proto *bpf_get_trace_printk_proto(void)
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

	return &bpf_trace_printk_proto;
}

#define MAX_SEQ_PRINTF_VARARGS		12
#define MAX_SEQ_PRINTF_MAX_MEMCPY	6
#define MAX_SEQ_PRINTF_STR_LEN		128

struct bpf_seq_printf_buf {
	char buf[MAX_SEQ_PRINTF_MAX_MEMCPY][MAX_SEQ_PRINTF_STR_LEN];
};
static DEFINE_PER_CPU(struct bpf_seq_printf_buf, bpf_seq_printf_buf);
static DEFINE_PER_CPU(int, bpf_seq_printf_buf_used);

BPF_CALL_5(bpf_seq_printf, struct seq_file *, m, char *, fmt, u32, fmt_size,
	   const void *, data, u32, data_len)
{
	int err = -EINVAL, fmt_cnt = 0, memcpy_cnt = 0;
	int i, buf_used, copy_size, num_args;
	u64 params[MAX_SEQ_PRINTF_VARARGS];
	struct bpf_seq_printf_buf *bufs;
	const u64 *args = data;

	buf_used = this_cpu_inc_return(bpf_seq_printf_buf_used);
	if (WARN_ON_ONCE(buf_used > 1)) {
		err = -EBUSY;
		goto out;
	}

	bufs = this_cpu_ptr(&bpf_seq_printf_buf);

	/*
	 * bpf_check()->check_func_arg()->check_stack_boundary()
	 * guarantees that fmt points to bpf program stack,
	 * fmt_size bytes of it were initialized and fmt_size > 0
	 */
	if (fmt[--fmt_size] != 0)
		goto out;

	if (data_len & 7)
		goto out;

	for (i = 0; i < fmt_size; i++) {
		if (fmt[i] == '%') {
			if (fmt[i + 1] == '%')
				i++;
			else if (!data || !data_len)
				goto out;
		}
	}

	num_args = data_len / 8;

	/* check format string for allowed specifiers */
	for (i = 0; i < fmt_size; i++) {
		/* only printable ascii for now. */
		if ((!isprint(fmt[i]) && !isspace(fmt[i])) || !isascii(fmt[i])) {
			err = -EINVAL;
			goto out;
		}

		if (fmt[i] != '%')
			continue;

		if (fmt[i + 1] == '%') {
			i++;
			continue;
		}

		if (fmt_cnt >= MAX_SEQ_PRINTF_VARARGS) {
			err = -E2BIG;
			goto out;
		}

		if (fmt_cnt >= num_args) {
			err = -EINVAL;
			goto out;
		}

		/* fmt[i] != 0 && fmt[last] == 0, so we can access fmt[i + 1] */
		i++;

		/* skip optional "[0 +-][num]" width formating field */
		while (fmt[i] == '0' || fmt[i] == '+'  || fmt[i] == '-' ||
		       fmt[i] == ' ')
			i++;
		if (fmt[i] >= '1' && fmt[i] <= '9') {
			i++;
			while (fmt[i] >= '0' && fmt[i] <= '9')
				i++;
		}

		if (fmt[i] == 's') {
			void *unsafe_ptr;

			/* try our best to copy */
			if (memcpy_cnt >= MAX_SEQ_PRINTF_MAX_MEMCPY) {
				err = -E2BIG;
				goto out;
			}

			unsafe_ptr = (void *)(long)args[fmt_cnt];
			err = strncpy_from_kernel_nofault(bufs->buf[memcpy_cnt],
					unsafe_ptr, MAX_SEQ_PRINTF_STR_LEN);
			if (err < 0)
				bufs->buf[memcpy_cnt][0] = '\0';
			params[fmt_cnt] = (u64)(long)bufs->buf[memcpy_cnt];

			fmt_cnt++;
			memcpy_cnt++;
			continue;
		}

		if (fmt[i] == 'p') {
			if (fmt[i + 1] == 0 ||
			    fmt[i + 1] == 'K' ||
			    fmt[i + 1] == 'x' ||
			    fmt[i + 1] == 'B') {
				/* just kernel pointers */
				params[fmt_cnt] = args[fmt_cnt];
				fmt_cnt++;
				continue;
			}

			/* only support "%pI4", "%pi4", "%pI6" and "%pi6". */
			if (fmt[i + 1] != 'i' && fmt[i + 1] != 'I') {
				err = -EINVAL;
				goto out;
			}
			if (fmt[i + 2] != '4' && fmt[i + 2] != '6') {
				err = -EINVAL;
				goto out;
			}

			if (memcpy_cnt >= MAX_SEQ_PRINTF_MAX_MEMCPY) {
				err = -E2BIG;
				goto out;
			}


			copy_size = (fmt[i + 2] == '4') ? 4 : 16;

			err = copy_from_kernel_nofault(bufs->buf[memcpy_cnt],
						(void *) (long) args[fmt_cnt],
						copy_size);
			if (err < 0)
				memset(bufs->buf[memcpy_cnt], 0, copy_size);
			params[fmt_cnt] = (u64)(long)bufs->buf[memcpy_cnt];

			i += 2;
			fmt_cnt++;
			memcpy_cnt++;
			continue;
		}

		if (fmt[i] == 'l') {
			i++;
			if (fmt[i] == 'l')
				i++;
		}

		if (fmt[i] != 'i' && fmt[i] != 'd' &&
		    fmt[i] != 'u' && fmt[i] != 'x' &&
		    fmt[i] != 'X') {
			err = -EINVAL;
			goto out;
		}

		params[fmt_cnt] = args[fmt_cnt];
		fmt_cnt++;
	}

	/* Maximumly we can have MAX_SEQ_PRINTF_VARARGS parameter, just give
	 * all of them to seq_printf().
	 */
	seq_printf(m, fmt, params[0], params[1], params[2], params[3],
		   params[4], params[5], params[6], params[7], params[8],
		   params[9], params[10], params[11]);

	err = seq_has_overflowed(m) ? -EOVERFLOW : 0;
out:
	this_cpu_dec(bpf_seq_printf_buf_used);
	return err;
}

BTF_ID_LIST_SINGLE(btf_seq_file_ids, struct, seq_file)

static const struct bpf_func_proto bpf_seq_printf_proto = {
	.func		= bpf_seq_printf,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &btf_seq_file_ids[0],
	.arg2_type	= ARG_PTR_TO_MEM,
	.arg3_type	= ARG_CONST_SIZE,
	.arg4_type      = ARG_PTR_TO_MEM_OR_NULL,
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
	.arg2_type	= ARG_PTR_TO_MEM,
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
	.arg2_type	= ARG_PTR_TO_MEM,
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
	struct bpf_trace_sample_data *sds = this_cpu_ptr(&bpf_trace_sds);
	int nest_level = this_cpu_inc_return(bpf_trace_nest_level);
	struct perf_raw_record raw = {
		.frag = {
			.size = size,
			.data = data,
		},
	};
	struct perf_sample_data *sd;
	int err;

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
	sd->raw = &raw;

	err = __bpf_perf_event_output(regs, map, flags, sd);

out:
	this_cpu_dec(bpf_trace_nest_level);
	return err;
}

static const struct bpf_func_proto bpf_perf_event_output_proto = {
	.func		= bpf_perf_event_output,
	.gpl_only	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_ANYTHING,
	.arg4_type	= ARG_PTR_TO_MEM,
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
	int nest_level = this_cpu_inc_return(bpf_event_output_nest_level);
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
	u64 ret;

	if (WARN_ON_ONCE(nest_level > ARRAY_SIZE(bpf_misc_sds.sds))) {
		ret = -EBUSY;
		goto out;
	}
	sd = this_cpu_ptr(&bpf_misc_sds.sds[nest_level - 1]);
	regs = this_cpu_ptr(&bpf_pt_regs.regs[nest_level - 1]);

	perf_fetch_caller_regs(regs);
	perf_sample_data_init(sd, 0, 0);
	sd->raw = &raw;

	ret = __bpf_perf_event_output(regs, map, flags, sd);
out:
	this_cpu_dec(bpf_event_output_nest_level);
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
	if (unlikely(uaccess_kernel()))
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
		if (atomic_read(&work->irq_work.flags) & IRQ_WORK_BUSY)
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
	long len;
	char *p;

	if (!sz)
		return 0;

	p = d_path(path, buf, sz);
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
	return btf_id_set_contains(&btf_allowlist_d_path, prog->aux->attach_btf_id);
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
	.arg3_type	= ARG_PTR_TO_MEM,
	.arg4_type	= ARG_CONST_SIZE,
	.arg5_type	= ARG_ANYTHING,
};

const struct bpf_func_proto *
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
		return security_locked_down(LOCKDOWN_BPF_READ) < 0 ?
		       NULL : &bpf_probe_read_kernel_proto;
	case BPF_FUNC_probe_read_user_str:
		return &bpf_probe_read_user_str_proto;
	case BPF_FUNC_probe_read_kernel_str:
		return security_locked_down(LOCKDOWN_BPF_READ) < 0 ?
		       NULL : &bpf_probe_read_kernel_str_proto;
#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
	case BPF_FUNC_probe_read:
		return security_locked_down(LOCKDOWN_BPF_READ) < 0 ?
		       NULL : &bpf_probe_read_compat_proto;
	case BPF_FUNC_probe_read_str:
		return security_locked_down(LOCKDOWN_BPF_READ) < 0 ?
		       NULL : &bpf_probe_read_compat_str_proto;
#endif
#ifdef CONFIG_CGROUPS
	case BPF_FUNC_get_current_cgroup_id:
		return &bpf_get_current_cgroup_id_proto;
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
		return prog->aux->sleepable ? &bpf_copy_from_user_proto : NULL;
	case BPF_FUNC_snprintf_btf:
		return &bpf_snprintf_btf_proto;
	case BPF_FUNC_per_cpu_ptr:
		return &bpf_per_cpu_ptr_proto;
	case BPF_FUNC_this_cpu_ptr:
		return &bpf_this_cpu_ptr_proto;
	default:
		return NULL;
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
	.arg4_type	= ARG_PTR_TO_MEM,
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
	.arg4_type	= ARG_PTR_TO_MEM,
	.arg5_type	= ARG_CONST_SIZE_OR_ZERO,
};

extern const struct bpf_func_proto bpf_skb_output_proto;
extern const struct bpf_func_proto bpf_xdp_output_proto;

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
	.arg2_type	= ARG_PTR_TO_MEM,
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
	default:
		return raw_tp_prog_func_proto(func_id, prog);
	}
}

static bool raw_tp_prog_is_valid_access(int off, int size,
					enum bpf_access_type type,
					const struct bpf_prog *prog,
					struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(__u64) * MAX_BPF_FUNC_ARGS)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	return true;
}

static bool tracing_prog_is_valid_access(int off, int size,
					 enum bpf_access_type type,
					 const struct bpf_prog *prog,
					 struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(__u64) * MAX_BPF_FUNC_ARGS)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	return btf_ctx_access(off, size, type, prog, info);
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
			       struct bpf_prog *prog)
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

	ret = bpf_prog_array_copy(old_array, NULL, prog, &new_array);
	if (ret < 0)
		goto unlock;

	/* set the new array to event->tp_event and set event->prog */
	event->prog = prog;
	rcu_assign_pointer(event->tp_event->prog_array, new_array);
	bpf_prog_array_free(old_array);

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
	ret = bpf_prog_array_copy(old_array, event->prog, NULL, &new_array);
	if (ret == -ENOENT)
		goto unlock;
	if (ret < 0) {
		bpf_prog_array_delete_safe(old_array, event->prog);
	} else {
		rcu_assign_pointer(event->tp_event->prog_array, new_array);
		bpf_prog_array_free(old_array);
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
	rcu_read_lock();
	(void) BPF_PROG_RUN(prog, args);
	rcu_read_unlock();
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
		*fd_type = BPF_FD_TYPE_TRACEPOINT;
		*probe_offset = 0x0;
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
						  probe_offset,
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
