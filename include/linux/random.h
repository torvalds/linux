/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/list.h>

#include <uapi/linux/random.h>

struct notifier_block;

void add_device_randomness(const void *buf, size_t len);
void __init add_bootloader_randomness(const void *buf, size_t len);
void add_input_randomness(unsigned int type, unsigned int code,
			  unsigned int value) __latent_entropy;
void add_interrupt_randomness(int irq) __latent_entropy;
void add_hwgenerator_randomness(const void *buf, size_t len, size_t entropy, bool sleep_after);

static inline void add_latent_entropy(void)
{
#if defined(LATENT_ENTROPY_PLUGIN) && !defined(__CHECKER__)
	add_device_randomness((const void *)&latent_entropy, sizeof(latent_entropy));
#else
	add_device_randomness(NULL, 0);
#endif
}

#if IS_ENABLED(CONFIG_VMGENID)
void add_vmfork_randomness(const void *unique_vm_id, size_t len);
int register_random_vmfork_notifier(struct notifier_block *nb);
int unregister_random_vmfork_notifier(struct notifier_block *nb);
#else
static inline int register_random_vmfork_notifier(struct notifier_block *nb) { return 0; }
static inline int unregister_random_vmfork_notifier(struct notifier_block *nb) { return 0; }
#endif

void get_random_bytes(void *buf, size_t len);
u8 get_random_u8(void);
u16 get_random_u16(void);
u32 get_random_u32(void);
u64 get_random_u64(void);
static inline unsigned long get_random_long(void)
{
#if BITS_PER_LONG == 64
	return get_random_u64();
#else
	return get_random_u32();
#endif
}

u32 __get_random_u32_below(u32 ceil);

/*
 * Returns a random integer in the interval [0, ceil), with uniform
 * distribution, suitable for all uses. Fastest when ceil is a constant, but
 * still fast for variable ceil as well.
 */
static inline u32 get_random_u32_below(u32 ceil)
{
	if (!__builtin_constant_p(ceil))
		return __get_random_u32_below(ceil);

	/*
	 * For the fast path, below, all operations on ceil are precomputed by
	 * the compiler, so this incurs no overhead for checking pow2, doing
	 * divisions, or branching based on integer size. The resultant
	 * algorithm does traditional reciprocal multiplication (typically
	 * optimized by the compiler into shifts and adds), rejecting samples
	 * whose lower half would indicate a range indivisible by ceil.
	 */
	BUILD_BUG_ON_MSG(!ceil, "get_random_u32_below() must take ceil > 0");
	if (ceil <= 1)
		return 0;
	for (;;) {
		if (ceil <= 1U << 8) {
			u32 mult = ceil * get_random_u8();
			if (likely(is_power_of_2(ceil) || (u8)mult >= (1U << 8) % ceil))
				return mult >> 8;
		} else if (ceil <= 1U << 16) {
			u32 mult = ceil * get_random_u16();
			if (likely(is_power_of_2(ceil) || (u16)mult >= (1U << 16) % ceil))
				return mult >> 16;
		} else {
			u64 mult = (u64)ceil * get_random_u32();
			if (likely(is_power_of_2(ceil) || (u32)mult >= -ceil % ceil))
				return mult >> 32;
		}
	}
}

/*
 * Returns a random integer in the interval (floor, U32_MAX], with uniform
 * distribution, suitable for all uses. Fastest when floor is a constant, but
 * still fast for variable floor as well.
 */
static inline u32 get_random_u32_above(u32 floor)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(floor) && floor == U32_MAX,
			 "get_random_u32_above() must take floor < U32_MAX");
	return floor + 1 + get_random_u32_below(U32_MAX - floor);
}

/*
 * Returns a random integer in the interval [floor, ceil], with uniform
 * distribution, suitable for all uses. Fastest when floor and ceil are
 * constant, but still fast for variable floor and ceil as well.
 */
static inline u32 get_random_u32_inclusive(u32 floor, u32 ceil)
{
	BUILD_BUG_ON_MSG(__builtin_constant_p(floor) && __builtin_constant_p(ceil) &&
			 (floor > ceil || ceil - floor == U32_MAX),
			 "get_random_u32_inclusive() must take floor <= ceil");
	return floor + get_random_u32_below(ceil - floor + 1);
}

void __init random_init_early(const char *command_line);
void __init random_init(void);
bool rng_is_initialized(void);
int wait_for_random_bytes(void);
int execute_with_initialized_rng(struct notifier_block *nb);

/* Calls wait_for_random_bytes() and then calls get_random_bytes(buf, nbytes).
 * Returns the result of the call to wait_for_random_bytes. */
static inline int get_random_bytes_wait(void *buf, size_t nbytes)
{
	int ret = wait_for_random_bytes();
	get_random_bytes(buf, nbytes);
	return ret;
}

#define declare_get_random_var_wait(name, ret_type) \
	static inline int get_random_ ## name ## _wait(ret_type *out) { \
		int ret = wait_for_random_bytes(); \
		if (unlikely(ret)) \
			return ret; \
		*out = get_random_ ## name(); \
		return 0; \
	}
declare_get_random_var_wait(u8, u8)
declare_get_random_var_wait(u16, u16)
declare_get_random_var_wait(u32, u32)
declare_get_random_var_wait(u64, u32)
declare_get_random_var_wait(long, unsigned long)
#undef declare_get_random_var

/*
 * This is designed to be standalone for just prandom
 * users, but for now we include it from <linux/random.h>
 * for legacy reasons.
 */
#include <linux/prandom.h>

#ifdef CONFIG_SMP
int random_prepare_cpu(unsigned int cpu);
int random_online_cpu(unsigned int cpu);
#endif

#ifndef MODULE
extern const struct file_operations random_fops, urandom_fops;
#endif

#endif /* _LINUX_RANDOM_H */
