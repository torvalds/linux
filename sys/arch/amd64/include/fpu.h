/*	$OpenBSD: fpu.h,v 1.20 2024/04/14 09:59:04 kettenis Exp $	*/
/*	$NetBSD: fpu.h,v 1.1 2003/04/26 18:39:40 fvdl Exp $	*/

#ifndef	_MACHINE_FPU_H_
#define	_MACHINE_FPU_H_

#include <sys/types.h>

/*
 * If the CPU supports xsave/xrstor then we use them so that we can provide
 * AVX support.  Otherwise we require fxsave/fxrstor, as the SSE registers
 * are part of the ABI for passing floating point values.
 * While fxsave/fxrstor only required 16-byte alignment for the save area,
 * xsave/xrstor requires the save area to have 64-byte alignment.
 */

struct fxsave64 {
	u_int16_t  fx_fcw;
	u_int16_t  fx_fsw;
	u_int8_t   fx_ftw;
	u_int8_t   fx_unused1;
	u_int16_t  fx_fop;
	u_int64_t  fx_rip;
	u_int64_t  fx_rdp;
	u_int32_t  fx_mxcsr;
	u_int32_t  fx_mxcsr_mask;
	u_int64_t  fx_st[8][2];   /* 8 normal FP regs */
	u_int64_t  fx_xmm[16][2]; /* 16 SSE2 registers */
	u_int8_t   fx_unused3[96];
} __packed;

struct xstate_hdr {
	uint64_t	xstate_bv;
	uint64_t	xstate_xcomp_bv;
	uint8_t		xstate_rsrv0[8];
	uint8_t		xstate_rsrv[40];
} __packed;

struct savefpu {
	struct fxsave64 fp_fxsave;	/* see above */
	struct xstate_hdr fp_xstate;
	u_int64_t fp_ymm[16][2];
	u_int8_t fp_components[1856];	/* enough for AVX-512 */
};

/*
 * The i387 defaults to Intel extended precision mode and round to nearest,
 * with all exceptions masked.
 */
#define	__INITIAL_NPXCW__	0x037f
#define __INITIAL_MXCSR__ 	0x1f80
#define __INITIAL_MXCSR_MASK__	0xffbf

#ifdef _KERNEL
/*
 * XXX
 */
struct trapframe;
struct cpu_info;

extern size_t	fpu_save_len;
extern uint32_t	fpu_mxcsr_mask;
extern uint64_t	xsave_mask;
extern int cpu_use_xsaves;

void fpuinit(struct cpu_info *);
int fputrap(int _type);
void fpusave(struct savefpu *);
void fpusavereset(struct savefpu *);
void fpu_kernel_enter(void);
void fpu_kernel_exit(void);

/* pointer to fxsave/xsave/xsaves data with everything reset */
#define	fpu_cleandata	(&proc0.p_addr->u_pcb.pcb_savefpu)

int	xrstor_user(struct savefpu *_addr, uint64_t _mask);
void	xrstor_kern(struct savefpu *_addr, uint64_t _mask);
#define	fpureset() \
	xrstor_kern(fpu_cleandata, xsave_mask)
int	xsetbv_user(uint32_t _reg, uint64_t _mask);

#define fninit()		__asm("fninit")
#define fwait()			__asm("fwait")
/* should be fxsave64, but where we use this it doesn't matter */
#define fxsave(addr)		__asm("fxsave %0" : "=m" (*addr))
#define ldmxcsr(addr)		__asm("ldmxcsr %0" : : "m" (*addr))
#define fldcw(addr)		__asm("fldcw %0" : : "m" (*addr))

static inline void
xsave(struct savefpu *addr, uint64_t mask)
{
	uint32_t lo, hi;

	lo = mask;
	hi = mask >> 32;
	__asm volatile("xsave64 %0" : "+m" (*addr) : "a" (lo), "d" (hi));
}

static inline void
xrstors(const struct savefpu *addr, uint64_t mask)
{
	uint32_t lo, hi;

	lo = mask;
	hi = mask >> 32;
	__asm volatile("xrstors64 %0" : : "m" (*addr), "a" (lo), "d" (hi));
}

#endif

#endif /* _MACHINE_FPU_H_ */
