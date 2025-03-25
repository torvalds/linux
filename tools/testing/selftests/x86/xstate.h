// SPDX-License-Identifier: GPL-2.0-only
#ifndef __SELFTESTS_X86_XSTATE_H
#define __SELFTESTS_X86_XSTATE_H

#include <stdint.h>

#include "../kselftest.h"

#define XSAVE_HDR_OFFSET	512
#define XSAVE_HDR_SIZE		64

/*
 * List of XSAVE features Linux knows about. Copied from
 * arch/x86/include/asm/fpu/types.h
 */
enum xfeature {
	XFEATURE_FP,
	XFEATURE_SSE,
	XFEATURE_YMM,
	XFEATURE_BNDREGS,
	XFEATURE_BNDCSR,
	XFEATURE_OPMASK,
	XFEATURE_ZMM_Hi256,
	XFEATURE_Hi16_ZMM,
	XFEATURE_PT_UNIMPLEMENTED_SO_FAR,
	XFEATURE_PKRU,
	XFEATURE_PASID,
	XFEATURE_CET_USER,
	XFEATURE_CET_KERNEL_UNUSED,
	XFEATURE_RSRVD_COMP_13,
	XFEATURE_RSRVD_COMP_14,
	XFEATURE_LBR,
	XFEATURE_RSRVD_COMP_16,
	XFEATURE_XTILECFG,
	XFEATURE_XTILEDATA,

	XFEATURE_MAX,
};

/* Copied from arch/x86/kernel/fpu/xstate.c */
static const char *xfeature_names[] =
{
	"x87 floating point registers",
	"SSE registers",
	"AVX registers",
	"MPX bounds registers",
	"MPX CSR",
	"AVX-512 opmask",
	"AVX-512 Hi256",
	"AVX-512 ZMM_Hi256",
	"Processor Trace (unused)",
	"Protection Keys User registers",
	"PASID state",
	"Control-flow User registers",
	"Control-flow Kernel registers (unused)",
	"unknown xstate feature",
	"unknown xstate feature",
	"unknown xstate feature",
	"unknown xstate feature",
	"AMX Tile config",
	"AMX Tile data",
	"unknown xstate feature",
};

struct xsave_buffer {
	union {
		struct {
			char legacy[XSAVE_HDR_OFFSET];
			char header[XSAVE_HDR_SIZE];
			char extended[0];
		};
		char bytes[0];
	};
};

static inline void xsave(struct xsave_buffer *xbuf, uint64_t rfbm)
{
	uint32_t rfbm_hi = rfbm >> 32;
	uint32_t rfbm_lo = rfbm;

	asm volatile("xsave (%%rdi)"
		     : : "D" (xbuf), "a" (rfbm_lo), "d" (rfbm_hi)
		     : "memory");
}

static inline void xrstor(struct xsave_buffer *xbuf, uint64_t rfbm)
{
	uint32_t rfbm_hi = rfbm >> 32;
	uint32_t rfbm_lo = rfbm;

	asm volatile("xrstor (%%rdi)"
		     : : "D" (xbuf), "a" (rfbm_lo), "d" (rfbm_hi));
}

#define CPUID_LEAF_XSTATE		0xd
#define CPUID_SUBLEAF_XSTATE_USER	0x0

static inline uint32_t get_xbuf_size(void)
{
	uint32_t eax, ebx, ecx, edx;

	__cpuid_count(CPUID_LEAF_XSTATE, CPUID_SUBLEAF_XSTATE_USER,
		      eax, ebx, ecx, edx);

	/*
	 * EBX enumerates the size (in bytes) required by the XSAVE
	 * instruction for an XSAVE area containing all the user state
	 * components corresponding to bits currently set in XCR0.
	 */
	return ebx;
}

struct xstate_info {
	const char *name;
	uint32_t num;
	uint32_t mask;
	uint32_t xbuf_offset;
	uint32_t size;
};

static inline struct xstate_info get_xstate_info(uint32_t xfeature_num)
{
	struct xstate_info xstate = { };
	uint32_t eax, ebx, ecx, edx;

	if (xfeature_num >= XFEATURE_MAX) {
		ksft_print_msg("unknown state\n");
		return xstate;
	}

	xstate.name = xfeature_names[xfeature_num];
	xstate.num  = xfeature_num;
	xstate.mask = 1 << xfeature_num;

	__cpuid_count(CPUID_LEAF_XSTATE, xfeature_num,
		      eax, ebx, ecx, edx);
	xstate.size        = eax;
	xstate.xbuf_offset = ebx;
	return xstate;
}

static inline struct xsave_buffer *alloc_xbuf(void)
{
	uint32_t xbuf_size = get_xbuf_size();

	/* XSAVE buffer should be 64B-aligned. */
	return aligned_alloc(64, xbuf_size);
}

static inline void clear_xstate_header(struct xsave_buffer *xbuf)
{
	memset(&xbuf->header, 0, sizeof(xbuf->header));
}

static inline void set_xstatebv(struct xsave_buffer *xbuf, uint64_t bv)
{
	/* XSTATE_BV is at the beginning of the header: */
	*(uint64_t *)(&xbuf->header) = bv;
}

/* See 'struct _fpx_sw_bytes' at sigcontext.h */
#define SW_BYTES_OFFSET		464
/* N.B. The struct's field name varies so read from the offset. */
#define SW_BYTES_BV_OFFSET	(SW_BYTES_OFFSET + 8)

static inline struct _fpx_sw_bytes *get_fpx_sw_bytes(void *xbuf)
{
	return xbuf + SW_BYTES_OFFSET;
}

static inline uint64_t get_fpx_sw_bytes_features(void *buffer)
{
	return *(uint64_t *)(buffer + SW_BYTES_BV_OFFSET);
}

static inline void set_rand_data(struct xstate_info *xstate, struct xsave_buffer *xbuf)
{
	int *ptr = (int *)&xbuf->bytes[xstate->xbuf_offset];
	int data, i;

	/*
	 * Ensure that 'data' is never 0.  This ensures that
	 * the registers are never in their initial configuration
	 * and thus never tracked as being in the init state.
	 */
	data = rand() | 1;

	for (i = 0; i < xstate->size / sizeof(int); i++, ptr++)
		*ptr = data;
}

/* Testing kernel's context switching and ABI support for the xstate. */
void test_xstate(uint32_t feature_num);

#endif /* __SELFTESTS_X86_XSTATE_H */
