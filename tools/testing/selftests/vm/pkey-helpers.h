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

#define NR_PKEYS 16
#define PKEY_BITS_PER_PKEY 2

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif
#define DPRINT_IN_SIGNAL_BUF_SIZE 4096
extern int dprint_in_signal;
extern char dprint_in_signal_buffer[DPRINT_IN_SIGNAL_BUF_SIZE];
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

extern unsigned int shadow_pkey_reg;
static inline unsigned int __read_pkey_reg(void)
{
	unsigned int eax, edx;
	unsigned int ecx = 0;
	unsigned int pkey_reg;

	asm volatile(".byte 0x0f,0x01,0xee\n\t"
		     : "=a" (eax), "=d" (edx)
		     : "c" (ecx));
	pkey_reg = eax;
	return pkey_reg;
}

static inline unsigned int _read_pkey_reg(int line)
{
	unsigned int pkey_reg = __read_pkey_reg();

	dprintf4("read_pkey_reg(line=%d) pkey_reg: %x shadow: %x\n",
			line, pkey_reg, shadow_pkey_reg);
	assert(pkey_reg == shadow_pkey_reg);

	return pkey_reg;
}

#define read_pkey_reg() _read_pkey_reg(__LINE__)

static inline void __write_pkey_reg(unsigned int pkey_reg)
{
	unsigned int eax = pkey_reg;
	unsigned int ecx = 0;
	unsigned int edx = 0;

	dprintf4("%s() changing %08x to %08x\n", __func__,
			__read_pkey_reg(), pkey_reg);
	asm volatile(".byte 0x0f,0x01,0xef\n\t"
		     : : "a" (eax), "c" (ecx), "d" (edx));
	assert(pkey_reg == __read_pkey_reg());
}

static inline void write_pkey_reg(unsigned int pkey_reg)
{
	dprintf4("%s() changing %08x to %08x\n", __func__,
			__read_pkey_reg(), pkey_reg);
	/* will do the shadow check for us: */
	read_pkey_reg();
	__write_pkey_reg(pkey_reg);
	shadow_pkey_reg = pkey_reg;
	dprintf4("%s(%08x) pkey_reg: %08x\n", __func__,
			pkey_reg, __read_pkey_reg());
}

/*
 * These are technically racy. since something could
 * change PKEY register between the read and the write.
 */
static inline void __pkey_access_allow(int pkey, int do_allow)
{
	unsigned int pkey_reg = read_pkey_reg();
	int bit = pkey * 2;

	if (do_allow)
		pkey_reg &= (1<<bit);
	else
		pkey_reg |= (1<<bit);

	dprintf4("pkey_reg now: %08x\n", read_pkey_reg());
	write_pkey_reg(pkey_reg);
}

static inline void __pkey_write_allow(int pkey, int do_allow_write)
{
	long pkey_reg = read_pkey_reg();
	int bit = pkey * 2 + 1;

	if (do_allow_write)
		pkey_reg &= (1<<bit);
	else
		pkey_reg |= (1<<bit);

	write_pkey_reg(pkey_reg);
	dprintf4("pkey_reg now: %08x\n", read_pkey_reg());
}

#define PROT_PKEY0     0x10            /* protection key value (bit 0) */
#define PROT_PKEY1     0x20            /* protection key value (bit 1) */
#define PROT_PKEY2     0x40            /* protection key value (bit 2) */
#define PROT_PKEY3     0x80            /* protection key value (bit 3) */

#define PAGE_SIZE 4096
#define MB	(1<<20)

static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
		unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile(
		"cpuid;"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

/* Intel-defined CPU features, CPUID level 0x00000007:0 (ecx) */
#define X86_FEATURE_PKU        (1<<3) /* Protection Keys for Userspace */
#define X86_FEATURE_OSPKE      (1<<4) /* OS Protection Keys Enable */

static inline int cpu_has_pku(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;

	eax = 0x7;
	ecx = 0x0;
	__cpuid(&eax, &ebx, &ecx, &edx);

	if (!(ecx & X86_FEATURE_PKU)) {
		dprintf2("cpu does not have PKU\n");
		return 0;
	}
	if (!(ecx & X86_FEATURE_OSPKE)) {
		dprintf2("cpu does not have OSPKE\n");
		return 0;
	}
	return 1;
}

#define XSTATE_PKEY_BIT	(9)
#define XSTATE_PKEY	0x200

int pkey_reg_xstate_offset(void)
{
	unsigned int eax;
	unsigned int ebx;
	unsigned int ecx;
	unsigned int edx;
	int xstate_offset;
	int xstate_size;
	unsigned long XSTATE_CPUID = 0xd;
	int leaf;

	/* assume that XSTATE_PKEY is set in XCR0 */
	leaf = XSTATE_PKEY_BIT;
	{
		eax = XSTATE_CPUID;
		ecx = leaf;
		__cpuid(&eax, &ebx, &ecx, &edx);

		if (leaf == XSTATE_PKEY_BIT) {
			xstate_offset = ebx;
			xstate_size = eax;
		}
	}

	if (xstate_size == 0) {
		printf("could not find size/offset of PKEY in xsave state\n");
		return 0;
	}

	return xstate_offset;
}

#endif /* _PKEYS_HELPER_H */
