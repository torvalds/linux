/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COOKIE_H
#define __LINUX_COOKIE_H

#include <linux/atomic.h>
#include <linux/percpu.h>
#include <asm/local.h>

struct pcpu_gen_cookie {
	local_t nesting;
	u64 last;
} __aligned(16);

struct gen_cookie {
	struct pcpu_gen_cookie __percpu *local;
	atomic64_t forward_last ____cacheline_aligned_in_smp;
	atomic64_t reverse_last;
};

#define COOKIE_LOCAL_BATCH	4096

#define DEFINE_COOKIE(name)						\
	static DEFINE_PER_CPU(struct pcpu_gen_cookie, __##name);	\
	static struct gen_cookie name = {				\
		.local		= &__##name,				\
		.forward_last	= ATOMIC64_INIT(0),			\
		.reverse_last	= ATOMIC64_INIT(0),			\
	}

static __always_inline u64 gen_cookie_next(struct gen_cookie *gc)
{
	struct pcpu_gen_cookie *local = this_cpu_ptr(gc->local);
	u64 val;

	if (likely(local_inc_return(&local->nesting) == 1)) {
		val = local->last;
		if (__is_defined(CONFIG_SMP) &&
		    unlikely((val & (COOKIE_LOCAL_BATCH - 1)) == 0)) {
			s64 next = atomic64_add_return(COOKIE_LOCAL_BATCH,
						       &gc->forward_last);
			val = next - COOKIE_LOCAL_BATCH;
		}
		local->last = ++val;
	} else {
		val = atomic64_dec_return(&gc->reverse_last);
	}
	local_dec(&local->nesting);
	return val;
}

#endif /* __LINUX_COOKIE_H */
