/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PKEYS_HELPER_H
#define _PKEYS_HELPER_H
#define _GNU_SOURCE
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/mman.h>

/* Define some kernel-like types */
#define  u8 __u8
#define u16 __u16
#define u32 __u32
#define u64 __u64

#define PTR_ERR_ENOTSUP ((void *)-ENOTSUP)

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif
#define DPRINT_IN_SIGNAL_BUF_SIZE 4096
extern int dprint_in_signal;
extern char dprint_in_signal_buffer[DPRINT_IN_SIGNAL_BUF_SIZE];

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
static inline void sigsafe_printf(const char *format, ...)
{
	va_list ap;

	if (!dprint_in_signal) {
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
	} else {
		int ret;
		/*
		 * No printf() functions are signal-safe.
		 * They deadlock easily. Write the format
		 * string to get some output, even if
		 * incomplete.
		 */
		ret = write(1, format, strlen(format));
		if (ret < 0)
			exit(1);
	}
}
#define dprintf_level(level, args...) do {	\
	if (level <= DEBUG_LEVEL)		\
		sigsafe_printf(args);		\
} while (0)
#define dprintf0(args...) dprintf_level(0, args)
#define dprintf1(args...) dprintf_level(1, args)
#define dprintf2(args...) dprintf_level(2, args)
#define dprintf3(args...) dprintf_level(3, args)
#define dprintf4(args...) dprintf_level(4, args)

extern void abort_hooks(void);
#define pkey_assert(condition) do {		\
	if (!(condition)) {			\
		dprintf0("assert() at %s::%d test_nr: %d iteration: %d\n", \
				__FILE__, __LINE__,	\
				test_nr, iteration_nr);	\
		dprintf0("errno at assert: %d", errno);	\
		abort_hooks();			\
		exit(__LINE__);			\
	}					\
} while (0)

__attribute__((noinline)) int read_ptr(int *ptr);
void expected_pkey_fault(int pkey);

#if defined(__i386__) || defined(__x86_64__) /* arch */
#include "pkey-x86.h"
#else /* arch */
#error Architecture not supported
#endif /* arch */

#define PKEY_MASK	(PKEY_DISABLE_ACCESS | PKEY_DISABLE_WRITE)

static inline u64 set_pkey_bits(u64 reg, int pkey, u64 flags)
{
	u32 shift = pkey_bit_position(pkey);
	/* mask out bits from pkey in old value */
	reg &= ~((u64)PKEY_MASK << shift);
	/* OR in new bits for pkey */
	reg |= (flags & PKEY_MASK) << shift;
	return reg;
}

static inline u64 get_pkey_bits(u64 reg, int pkey)
{
	u32 shift = pkey_bit_position(pkey);
	/*
	 * shift down the relevant bits to the lowest two, then
	 * mask off all the other higher bits
	 */
	return ((reg >> shift) & PKEY_MASK);
}

extern u64 shadow_pkey_reg;

static inline u64 _read_pkey_reg(int line)
{
	u64 pkey_reg = __read_pkey_reg();

	dprintf4("read_pkey_reg(line=%d) pkey_reg: %016llx"
			" shadow: %016llx\n",
			line, pkey_reg, shadow_pkey_reg);
	assert(pkey_reg == shadow_pkey_reg);

	return pkey_reg;
}

#define read_pkey_reg() _read_pkey_reg(__LINE__)

static inline void write_pkey_reg(u64 pkey_reg)
{
	dprintf4("%s() changing %016llx to %016llx\n", __func__,
			__read_pkey_reg(), pkey_reg);
	/* will do the shadow check for us: */
	read_pkey_reg();
	__write_pkey_reg(pkey_reg);
	shadow_pkey_reg = pkey_reg;
	dprintf4("%s(%016llx) pkey_reg: %016llx\n", __func__,
			pkey_reg, __read_pkey_reg());
}

/*
 * These are technically racy. since something could
 * change PKEY register between the read and the write.
 */
static inline void __pkey_access_allow(int pkey, int do_allow)
{
	u64 pkey_reg = read_pkey_reg();
	int bit = pkey * 2;

	if (do_allow)
		pkey_reg &= (1<<bit);
	else
		pkey_reg |= (1<<bit);

	dprintf4("pkey_reg now: %016llx\n", read_pkey_reg());
	write_pkey_reg(pkey_reg);
}

static inline void __pkey_write_allow(int pkey, int do_allow_write)
{
	u64 pkey_reg = read_pkey_reg();
	int bit = pkey * 2 + 1;

	if (do_allow_write)
		pkey_reg &= (1<<bit);
	else
		pkey_reg |= (1<<bit);

	write_pkey_reg(pkey_reg);
	dprintf4("pkey_reg now: %016llx\n", read_pkey_reg());
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#define ALIGN_UP(x, align_to)	(((x) + ((align_to)-1)) & ~((align_to)-1))
#define ALIGN_DOWN(x, align_to) ((x) & ~((align_to)-1))
#define ALIGN_PTR_UP(p, ptr_align_to)	\
	((typeof(p))ALIGN_UP((unsigned long)(p), ptr_align_to))
#define ALIGN_PTR_DOWN(p, ptr_align_to)	\
	((typeof(p))ALIGN_DOWN((unsigned long)(p), ptr_align_to))
#define __stringify_1(x...)     #x
#define __stringify(x...)       __stringify_1(x)

static inline u32 *siginfo_get_pkey_ptr(siginfo_t *si)
{
#ifdef si_pkey
	return &si->si_pkey;
#else
	return (u32 *)(((u8 *)si) + si_pkey_offset);
#endif
}

#endif /* _PKEYS_HELPER_H */
