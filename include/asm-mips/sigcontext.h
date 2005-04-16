/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIGCONTEXT_H
#define _ASM_SIGCONTEXT_H

#include <asm/sgidefs.h>
                                                                                
#if _MIPS_SIM == _MIPS_SIM_ABI32

/*
 * Keep this struct definition in sync with the sigcontext fragment
 * in arch/mips/tools/offset.c
 */
struct sigcontext {
	unsigned int		sc_regmask;	/* Unused */
	unsigned int		sc_status;
	unsigned long long	sc_pc;
	unsigned long long	sc_regs[32];
	unsigned long long	sc_fpregs[32];
	unsigned int		sc_ownedfp;	/* Unused */
	unsigned int		sc_fpc_csr;
	unsigned int		sc_fpc_eir;	/* Unused */
	unsigned int		sc_used_math;
	unsigned int		sc_ssflags;	/* Unused */
	unsigned long long	sc_mdhi;
	unsigned long long	sc_mdlo;

	unsigned int		sc_cause;	/* Unused */
	unsigned int		sc_badvaddr;	/* Unused */

	unsigned long		sc_sigset[4];	/* kernel's sigset_t */
};

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 */
                                                                                
#if _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32

/*
 * Keep this struct definition in sync with the sigcontext fragment
 * in arch/mips/tools/offset.c
 *
 * Warning: this structure illdefined with sc_badvaddr being just an unsigned
 * int so it was changed to unsigned long in 2.6.0-test1.  This may break
 * binary compatibility - no prisoners.
 */
struct sigcontext {
	unsigned long	sc_regs[32];
	unsigned long	sc_fpregs[32];
	unsigned long	sc_mdhi;
	unsigned long	sc_mdlo;
	unsigned long	sc_pc;
	unsigned long	sc_badvaddr;
	unsigned int	sc_status;
	unsigned int	sc_fpc_csr;
	unsigned int	sc_fpc_eir;
	unsigned int	sc_used_math;
	unsigned int	sc_cause;
};

#ifdef __KERNEL__

#include <linux/posix_types.h>

struct sigcontext32 {
	__u32	sc_regmask;		/* Unused */
	__u32	sc_status;
	__u64	sc_pc;
	__u64	sc_regs[32];
	__u64	sc_fpregs[32];
	__u32	sc_ownedfp;		/* Unused */
	__u32	sc_fpc_csr;
	__u32	sc_fpc_eir;		/* Unused */
	__u32	sc_used_math;
	__u32	sc_ssflags;		/* Unused */
	__u64	sc_mdhi;
	__u64	sc_mdlo;

	__u32	sc_cause;		/* Unused */
	__u32	sc_badvaddr;		/* Unused */

	__u32	sc_sigset[4];		/* kernel's sigset_t */
};
#endif /* __KERNEL__ */

#endif /* _MIPS_SIM == _MIPS_SIM_ABI64 || _MIPS_SIM == _MIPS_SIM_NABI32 */

#endif /* _ASM_SIGCONTEXT_H */
