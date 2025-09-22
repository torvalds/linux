/*	$OpenBSD: trap.h,v 1.5 2013/04/02 13:24:57 kettenis Exp $	*/
/*	$NetBSD: trap.h,v 1.4 1999/06/07 05:28:04 eeh Exp $ */

/*
 * Copyright (c) 1996-1999 Eduardo Horvath
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */


#ifndef	_MACHINE_TRAP_H
#define	_MACHINE_TRAP_H

/*	trap		vec	  (pri) description	*/
/*			0x000	   unused */
#define	T_POR		0x001	/* (0) power on reset */
#define T_WDR		0x002	/* (1) watchdog reset */
#define T_XIR		0x003	/* (1) externally initiated reset */
#define T_SIR		0x004	/* (1) software initiated reset */
#define T_RED_EXCEPTION	0x005	/* (1) RED state exception */
/*			0x006	   unused */
/*			0x007	   unused */
#define T_INST_EXCEPT	0x008	/* (5) instruction access exception */
#define T_TEXTFAULT	0x009	/* (2) ? Text fault */
#define T_INST_ERROR	0x00a	/* (3) instruction access error */
/*			0x00b	   unused */
/*	through		0x00f	   unused */
#define T_ILLINST	0x010	/* (7) illegal instruction */
#define T_PRIVINST	0x011	/* (6) privileged opcode */
#define	T_UNIMP_LDD	0x012	/* (6) unimplemented LDD */
#define	T_UNIMP_STD	0x013	/* (6) unimplemented STD */
/*			0x014	   unused */
/*	through		0x01f	   unused */
#define T_FPDISABLED	0x020	/* (8) fpu disabled */
#define T_FP_IEEE_754	0x021	/* (11) ieee 754 exception */
#define T_FP_OTHER	0x022	/* (11) other fp exception */
#define T_TAGOF		0x023	/* (14) tag overflow */
#define T_CLEAN_WINDOW	0x024	/* (10) clean window exception */
/*			0x025	   unused */
/*	through		0x027	   unused */
#define T_DIV0		0x028	/* (15) division routine was handed 0 */
#define	T_PROCERR	0x029	/* (4) internal processor error */
/*			0x02a	   unused */
/*	through		0x02f	   unused */
#define	T_DATAFAULT	0x030	/* (12) address fault during data fetch */
#define	T_DATA_MMU_MISS	0x031	/* (12) data access MMU miss */
#define T_DATA_ERROR	0x032	/* (12) data access error */
#define T_DATA_PROT	0x033	/* (12) Data protection ??? */
#define	T_ALIGN		0x034	/* (10) address not properly aligned */
#define	T_LDDF_ALIGN	0x035	/* (10) LDDF address not properly aligned */
#define	T_STDF_ALIGN	0x036	/* (10) STDF address not properly aligned */
#define T_PRIVACT	0x037	/* (11) privileged action */
#define	T_LDQF_ALIGN	0x038	/* (10) LDQF address not properly aligned */
#define	T_STQF_ALIGN	0x039	/* (10) STQF address not properly aligned */
/*			0x03a	   unused */
/*	through		0x03f	   unused */
#define T_ASYNC_ERROR	0x040	/* (2) ???? */
#define	T_L1INT		0x041	/* (31) level 1 interrupt */
#define	T_L2INT		0x042	/* (30) level 2 interrupt */
#define	T_L3INT		0x043	/* (29) level 3 interrupt */
#define	T_L4INT		0x044	/* (28) level 4 interrupt */
#define	T_L5INT		0x045	/* (27) level 5 interrupt */
#define	T_L6INT		0x046	/* (26) level 6 interrupt */
#define	T_L7INT		0x047	/* (25) level 7 interrupt */
#define	T_L8INT		0x048	/* (24) level 8 interrupt */
#define	T_L9INT		0x049	/* (23) level 9 interrupt */
#define	T_L10INT	0x04a	/* (22) level 10 interrupt */
#define	T_L11INT	0x04b	/* (21) level 11 interrupt */
#define	T_L12INT	0x04c	/* (20) level 12 interrupt */
#define	T_L13INT	0x04d	/* (19) level 13 interrupt */
#define	T_L14INT	0x04e	/* (18) level 14 interrupt */
#define	T_L15INT	0x04f	/* (17) level 15 interrupt */
/*			0x050	   unused */
/*	through		0x05f	   unused */
#define T_INTVEC	0x060	/* (16) interrupt vector [Interrupt Global Regs]*/
#define T_PA_WATCHPT	0x061	/* (12) Physical addr data watchpoint */
#define T_VA_WATCHPT	0x062	/* (11) Virtual addr data watchpoint */
#define T_ECCERR	0x063	/* (33) ECC correction error */
#define T_FIMMU_MISS	0x064	/* (2) fast instruction access MMU miss */
/*	through		0x067	   unused */
#define T_FDMMU_MISS	0x068	/* (2) fast data access MMU miss */
/*	through		0x06b	   unused */
#define T_FDMMU_PROT	0x06c	/* (2) fast data access protection */
/*	through		0x06f	   unused */
/*  0x070...0x07f implementation dependent exceptions */
#define T_SPILL_N_NORM	0x080	/* (9) spill (n=0..7) normal */
/*	through		0x09f	   unused */
#define T_SPILL_N_OTHER	0x0a0	/* (9) spill (n=0..7) other */
/*	through		0x0bf	   unused */
#define T_FILL_N_NORM	0x0c0	/* (9) fill (n=0..7) normal */
/*	through		0x0df	   unused */
#define T_FILL_N_OTHER	0x0e0	/* (9) fill (n=0..7) other */
/*	through		0x0ff	   unused */

/* beginning of `user' vectors (from trap instructions) - all priority 16 */
#define	T_SUN_SYSCALL	0x100	/* system call */
#define	T_BREAKPOINT	0x101	/* breakpoint `instruction' */
#define	T_UDIV0		0x102	/* division routine was handed 0 */
#define	T_FLUSHWIN	0x103	/* flush windows */
#define	T_CLEANWIN	0x104	/* provide clean windows */
#define	T_RANGECHECK	0x105	/* ? */
#define	T_FIXALIGN	0x106	/* fix up unaligned accesses */
#define	T_INTOF		0x107	/* integer overflow ? */
#define	T_SVR4_SYSCALL	0x108	/* SVR4 system call */
#define	T_BSD_SYSCALL	0x109	/* BSD system call */
#define	T_KGDB_EXEC	0x10a	/* for kernel gdb */

/* 0x10b..0x1ff are currently unallocated, except the following */
#define T_SVR4_GETCC		0x120
#define T_SVR4_SETCC		0x121
#define T_SVR4_GETPSR		0x122
#define T_SVR4_SETPSR		0x123
#define T_SVR4_GETHRTIME	0x124
#define T_SVR4_GETHRVTIME	0x125
#define T_SVR4_GETHRESTIME	0x127
#define T_GETCC			0x132
#define T_SETCC			0x133
#define T_SVID_SYSCALL		0x164
#define	T_SPARC_INTL_SYSCALL	0x165
#define	T_OS_VENDOR_SYSCALL	0x166
#define	T_HW_OEM_SYSCALL	0x167
#define T_RTF_DEF_TRAP		0x168
#define T_MON_BREAKPOINT	0x17f

#ifdef _KERNEL			/* pseudo traps for locore.s */
#define	T_RWRET		-1	/* need first user window for trap return */
#define	T_AST		-2	/* no-op, just needed reschedule or profile */
#endif

/* flags to system call (flags in %g1 along with syscall number) */
#define	SYSCALL_G2RFLAG	0x400	/* on success, return to %g2 rather than npc */

/*
 * `software trap' macros to keep people happy (sparc v8 manual says not
 * to set the upper bits).
 */
#define	ST_BREAKPOINT	(T_BREAKPOINT & 0x7f)
#define	ST_DIV0		(T_DIV0 & 0x7f)
#define	ST_FLUSHWIN	(T_FLUSHWIN & 0x7f)
#define	ST_SYSCALL	(T_SUN_SYSCALL & 0x7f)

#endif /* _MACHINE_TRAP_H_ */
