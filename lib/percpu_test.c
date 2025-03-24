// SPDX-License-Identifier: GPL-2.0-only
#include <linux/limits.h>
#include <linux/module.h>

/* validate @native and @pcp counter values match @expected */
#define CHECK(native, pcp, expected)                                    \
	do {                                                            \
		WARN((native) != (expected),                            \
		     "raw %ld (0x%lx) != expected %lld (0x%llx)",	\
		     (native), (native),				\
		     (long long)(expected), (long long)(expected));	\
		WARN(__this_cpu_read(pcp) != (expected),                \
		     "pcp %ld (0x%lx) != expected %lld (0x%llx)",	\
		     __this_cpu_read(pcp), __this_cpu_read(pcp),	\
		     (long long)(expected), (long long)(expected));	\
	} while (0)

static DEFINE_PER_CPU(long, long_counter);
static DEFINE_PER_CPU(unsigned long, ulong_counter);

static int __init percpu_test_init(void)
{
	/*
	 * volatile prevents compiler from optimizing it uses, otherwise the
	 * +ul_one/-ul_one below would replace with inc/dec instructions.
	 */
	volatile unsigned int ui_one = 1;
	unsigned long long ull = 0;
	unsigned long ul = 0;
	long l = 0;

	pr_info("percpu test start\n");

	preempt_disable();

	l += -1;
	__this_cpu_add(long_counter, -1);
	CHECK(l, long_counter, -1);

	l += 1;
	__this_cpu_add(long_counter, 1);
	CHECK(l, long_counter, 0);

	ul = 0;
	__this_cpu_write(ulong_counter, 0);

	ul += 1UL;
	__this_cpu_add(ulong_counter, 1UL);
	CHECK(ul, ulong_counter, 1);

	ul += -1UL;
	__this_cpu_add(ulong_counter, -1UL);
	CHECK(ul, ulong_counter, 0);

	ul += -(unsigned long)1;
	__this_cpu_add(ulong_counter, -(unsigned long)1);
	CHECK(ul, ulong_counter, -1);

	ul = 0;
	__this_cpu_write(ulong_counter, 0);

	ul -= 1;
	__this_cpu_dec(ulong_counter);
	CHECK(ul, ulong_counter, -1);
	CHECK(ul, ulong_counter, ULONG_MAX);

	l += -ui_one;
	__this_cpu_add(long_counter, -ui_one);
	CHECK(l, long_counter, 0xffffffff);

	l += ui_one;
	__this_cpu_add(long_counter, ui_one);
	CHECK(l, long_counter, (long)0x100000000LL);


	l = 0;
	__this_cpu_write(long_counter, 0);

	l -= ui_one;
	__this_cpu_sub(long_counter, ui_one);
	CHECK(l, long_counter, -1);

	l = 0;
	__this_cpu_write(long_counter, 0);

	l += ui_one;
	__this_cpu_add(long_counter, ui_one);
	CHECK(l, long_counter, 1);

	l += -ui_one;
	__this_cpu_add(long_counter, -ui_one);
	CHECK(l, long_counter, (long)0x100000000LL);

	l = 0;
	__this_cpu_write(long_counter, 0);

	l -= ui_one;
	this_cpu_sub(long_counter, ui_one);
	CHECK(l, long_counter, -1);
	CHECK(l, long_counter, ULONG_MAX);

	ul = 0;
	__this_cpu_write(ulong_counter, 0);

	ul += ui_one;
	__this_cpu_add(ulong_counter, ui_one);
	CHECK(ul, ulong_counter, 1);

	ul = 0;
	__this_cpu_write(ulong_counter, 0);

	ul -= ui_one;
	__this_cpu_sub(ulong_counter, ui_one);
	CHECK(ul, ulong_counter, -1);
	CHECK(ul, ulong_counter, ULONG_MAX);

	ul = ull = 0;
	__this_cpu_write(ulong_counter, 0);

	ul = ull += UINT_MAX;
	__this_cpu_add(ulong_counter, ull);
	CHECK(ul, ulong_counter, UINT_MAX);

	ul = 3;
	__this_cpu_write(ulong_counter, 3);

	ul = this_cpu_sub_return(ulong_counter, ui_one);
	CHECK(ul, ulong_counter, 2);

	ul = __this_cpu_sub_return(ulong_counter, ui_one);
	CHECK(ul, ulong_counter, 1);

	preempt_enable();

	pr_info("percpu test done\n");
	return -EAGAIN;  /* Fail will directly unload the module */
}

static void __exit percpu_test_exit(void)
{
}

module_init(percpu_test_init)
module_exit(percpu_test_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Thelen");
MODULE_DESCRIPTION("percpu operations test");
