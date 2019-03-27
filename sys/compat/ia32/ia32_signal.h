/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Marcel Moolenaar
 * Copyright (c) 2003 Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer 
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_COMPAT_IA32_IA32_SIGNAL_H
#define	_COMPAT_IA32_IA32_SIGNAL_H

#include <compat/freebsd32/freebsd32_signal.h>

#define	_MC_IA32_HASSEGS	0x1
#define	_MC_IA32_HASBASES	0x2
#define	_MC_IA32_HASFPXSTATE	0x4
#define	_MC_IA32_FLAG_MASK	\
    (_MC_IA32_HASSEGS | _MC_IA32_HASBASES | _MC_IA32_HASFPXSTATE)

struct ia32_mcontext {
	u_int32_t	mc_onstack;		/* XXX - sigcontext compat. */
	u_int32_t	mc_gs;			/* machine state (struct trapframe) */
	u_int32_t	mc_fs;
	u_int32_t	mc_es;
	u_int32_t	mc_ds;
	u_int32_t	mc_edi;
	u_int32_t	mc_esi;
	u_int32_t	mc_ebp;
	u_int32_t	mc_isp;
	u_int32_t	mc_ebx;
	u_int32_t	mc_edx;
	u_int32_t	mc_ecx;
	u_int32_t	mc_eax;
	u_int32_t	mc_trapno;
	u_int32_t	mc_err;
	u_int32_t	mc_eip;
	u_int32_t	mc_cs;
	u_int32_t	mc_eflags;
	u_int32_t	mc_esp;
	u_int32_t	mc_ss;
	u_int32_t	mc_len;			/* sizeof(struct ia32_mcontext) */
	/* We use the same values for fpformat and ownedfp */
	u_int32_t	mc_fpformat;
	u_int32_t	mc_ownedfp;
	u_int32_t	mc_flags;
	/*
	 * See <i386/include/npx.h> for the internals of mc_fpstate[].
	 */
	u_int32_t	mc_fpstate[128] __aligned(16);
	u_int32_t	mc_fsbase;
	u_int32_t	mc_gsbase;
	u_int32_t	mc_xfpustate;
	u_int32_t	mc_xfpustate_len;
	u_int32_t	mc_spare2[4];
};

struct ia32_ucontext {
	sigset_t		uc_sigmask;
	struct ia32_mcontext	uc_mcontext;
	u_int32_t		uc_link;
	struct sigaltstack32	uc_stack;
	u_int32_t		uc_flags;
	u_int32_t		__spare__[4];
};


#if defined(COMPAT_FREEBSD4)
struct ia32_mcontext4 {
	u_int32_t	mc_onstack;		/* XXX - sigcontext compat. */
	u_int32_t	mc_gs;			/* machine state (struct trapframe) */
	u_int32_t	mc_fs;
	u_int32_t	mc_es;
	u_int32_t	mc_ds;
	u_int32_t	mc_edi;
	u_int32_t	mc_esi;
	u_int32_t	mc_ebp;
	u_int32_t	mc_isp;
	u_int32_t	mc_ebx;
	u_int32_t	mc_edx;
	u_int32_t	mc_ecx;
	u_int32_t	mc_eax;
	u_int32_t	mc_trapno;
	u_int32_t	mc_err;
	u_int32_t	mc_eip;
	u_int32_t	mc_cs;
	u_int32_t	mc_eflags;
	u_int32_t	mc_esp;	
	u_int32_t	mc_ss;
	u_int32_t	mc_fpregs[28];
	u_int32_t	__spare__[17];
};

struct ia32_ucontext4 {
	sigset_t		uc_sigmask;
	struct ia32_mcontext4	uc_mcontext;
	u_int32_t		uc_link;
	struct sigaltstack32	uc_stack;
	u_int32_t		__spare__[8];
};
#endif

#ifdef COMPAT_43
struct ia32_sigcontext3 {
	u_int32_t	sc_onstack;
	u_int32_t	sc_mask;
	u_int32_t	sc_esp;	
	u_int32_t	sc_ebp;
	u_int32_t	sc_isp;
	u_int32_t	sc_eip;
	u_int32_t	sc_eflags;
	u_int32_t	sc_es;
	u_int32_t	sc_ds;
	u_int32_t	sc_cs;
	u_int32_t	sc_ss;
	u_int32_t	sc_edi;
	u_int32_t	sc_esi;
	u_int32_t	sc_ebx;
	u_int32_t	sc_edx;
	u_int32_t	sc_ecx;
	u_int32_t	sc_eax;
	u_int32_t	sc_gs;
	u_int32_t	sc_fs;
	u_int32_t	sc_trapno;
	u_int32_t	sc_err;
};
#endif

/*
 * Signal frames, arguments passed to application signal handlers.
 */

#ifdef COMPAT_FREEBSD4
struct ia32_sigframe4 {
	u_int32_t		sf_signum;
	u_int32_t		sf_siginfo;	/* code or pointer to sf_si */
	u_int32_t		sf_ucontext;	/* points to sf_uc */
	u_int32_t		sf_addr;	/* undocumented 4th arg */
	u_int32_t		sf_ah;		/* action/handler pointer */
	struct ia32_ucontext4	sf_uc;		/* = *sf_ucontext */
	struct siginfo32	sf_si;		/* = *sf_siginfo (SA_SIGINFO case) */
};
#endif

struct ia32_sigframe {
	u_int32_t		sf_signum;
	u_int32_t		sf_siginfo;	/* code or pointer to sf_si */
	u_int32_t		sf_ucontext;	/* points to sf_uc */
	u_int32_t		sf_addr;	/* undocumented 4th arg */
	u_int32_t		sf_ah;		/* action/handler pointer */
	/* Beware, hole due to ucontext being 16 byte aligned! */
	struct ia32_ucontext	sf_uc;		/* = *sf_ucontext */
	struct siginfo32	sf_si;		/* = *sf_siginfo (SA_SIGINFO case) */
};

#ifdef COMPAT_43
struct ia32_siginfo3 {
	struct ia32_sigcontext3 si_sc;
	int			si_signo;
	int			si_code;
	union sigval32		si_value;
};
struct ia32_sigframe3 {
	int			sf_signum;
	u_int32_t		sf_arg2;	/* int or siginfo_t */
	u_int32_t		sf_scp;
	u_int32_t		sf_addr;
	u_int32_t		sf_ah;		/* action/handler pointer */
	struct ia32_siginfo3	sf_siginfo;
};
#endif

struct ksiginfo;
struct image_params;
extern char ia32_sigcode[];
extern char freebsd4_ia32_sigcode[];
extern char ia32_osigcode[];
extern char lcall_tramp;
extern int sz_ia32_sigcode;
extern int sz_freebsd4_ia32_sigcode;
extern int sz_ia32_osigcode;
extern int sz_lcall_tramp;
void ia32_sendsig(sig_t, struct ksiginfo *, sigset_t *);
void ia32_setregs(struct thread *td, struct image_params *imgp,
    u_long stack);
int setup_lcall_gate(void);

#endif
