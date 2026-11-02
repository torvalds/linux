// SPDX-License-Identifier: GPL-2.0
/*
 * C++ exceptions. llvm lowers unwinding to two insns:
 *   invoke foo unwind pad    call foo; unwind_goto pad
 *   resume                   r0 = 0; unwind_exit
 * so 'throw' is a jump or an exit, 'catch (...)' and the destructors that run on
 * the way are ordinary blocks.
 *
 * vmlinux.h and bpf_helpers.h are not C++.
 */
#define SEC(name) __attribute__((section(name), used))
#define __ksym __attribute__((section(".ksyms")))
#define __noinline __attribute__((noinline))
#define __always_inline inline __attribute__((always_inline))

#ifdef __BPF_FEATURE_UNWIND

struct __sk_buff {
	unsigned int len;
};

extern "C" {
void bpf_rcu_read_lock(void) noexcept __ksym;
void bpf_rcu_read_unlock(void) noexcept __ksym;
void bpf_preempt_disable(void) noexcept __ksym;
void bpf_preempt_enable(void) noexcept __ksym;

/* Starts an unwind. Not a kfunc: llvm lowers the call and it doesn't come back. */
int _Unwind_RaiseException(void *exc);

/* All that is left of the C++ runtime. Nobody looks at the exception object. */
static char exc_obj[16];

__always_inline void *__cxa_allocate_exception(unsigned long size) noexcept
{
	return exc_obj;
}

__always_inline void __cxa_throw(void *exc, void *tinfo, void (*dtor)(void *))
{
	_Unwind_RaiseException(exc);
}

__always_inline void *__cxa_begin_catch(void *exc) noexcept
{
	return exc;
}

__always_inline void __cxa_end_catch(void) noexcept
{
}
}

/* One decimal digit per destructor, in the order they ran */
volatile int dtors SEC(".data.dtors");

/* The verifier insists that both are undone on every path out of the prog */
struct rcu_guard {
	__always_inline rcu_guard()
	{
		bpf_rcu_read_lock();
	}
	__always_inline ~rcu_guard()
	{
		bpf_rcu_read_unlock();
		dtors = dtors * 10 + 1;
	}
};

struct plain_rcu_guard {
	__always_inline plain_rcu_guard()
	{
		bpf_rcu_read_lock();
	}
	__always_inline ~plain_rcu_guard()
	{
		bpf_rcu_read_unlock();
	}
};

struct preempt_guard {
	__always_inline preempt_guard()
	{
		bpf_preempt_disable();
	}
	__always_inline ~preempt_guard()
	{
		bpf_preempt_enable();
		dtors = dtors * 10 + 2;
	}
};

static __noinline int inner(int len)
{
	rcu_guard guard;

	if (len > 100)
		throw 1;
	return len;
}

static __noinline int middle(int len)
{
	preempt_guard guard;

	return inner(len) + 1;
}

using u128 = unsigned __int128;

/* Global subprogs returning 16 bytes use R0:R2, including unwind exits. */
extern "C" __noinline u128 aggregate_return(int len)
{
	plain_rcu_guard guard;

	if (len > 100)
		throw 1;
	return (static_cast<u128>(len) << 64) | 7;
}

extern "C" SEC("tc") int catches(struct __sk_buff *skb)
{
	try {
		return middle(skb->len);
	} catch (...) {
		return 42;
	}
}

/* The exception leaves the prog: it returns 0 */
extern "C" SEC("tc") int does_not_catch(struct __sk_buff *skb)
{
	return middle(skb->len);
}

extern "C" SEC("tc") int catches_aggregate(struct __sk_buff *skb)
{
	try {
		u128 value = aggregate_return(skb->len);

		return static_cast<unsigned long>(value) +
		       static_cast<unsigned long>(value >> 64);
	} catch (...) {
		return 42;
	}
}

#endif

char _license[] SEC("license") = "GPL";
