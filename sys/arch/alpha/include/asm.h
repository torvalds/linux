/* $OpenBSD: asm.h,v 1.16 2023/12/06 06:15:33 miod Exp $ */
/* $NetBSD: asm.h,v 1.23 2000/06/23 12:18:45 kleink Exp $ */

/* 
 * Copyright (c) 1991,1990,1989,1994,1995,1996 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 *	Assembly coding style
 *
 *	This file contains macros and register defines to
 *	aid in writing more readable assembly code.
 *	Some rules to make assembly code understandable by
 *	a debugger are also noted.
 *
 *	The document
 *
 *		"ALPHA Calling Standard", DEC 27-Apr-90
 *
 *	defines (a superset of) the rules and conventions
 *	we use.  While we make no promise of adhering to
 *	such standard and its evolution (esp where we
 *	can get faster code paths) it is certainly intended
 *	that we be interoperable with such standard.
 *
 *	In this sense, this file is a proper part of the
 *	definition of the (software) Alpha architecture.
 */

/*
 *	Symbolic register names and register saving rules
 *
 *	Legend:
 *		T	Saved by caller (Temporaries)
 *		S	Saved by callee (call-Safe registers)
 */

#define	v0	$0	/* (T)		return value		*/
#define t0	$1	/* (T)		temporary registers	*/
#define t1	$2
#define t2	$3
#define t3	$4
#define t4	$5
#define t5	$6
#define t6	$7
#define t7	$8

#define s0	$9	/* (S)		call-safe registers	*/
#define s1	$10
#define s2	$11
#define s3	$12
#define s4	$13
#define s5	$14
#define s6	$15
#define a0	$16	/* (T)		argument registers	*/
#define a1	$17
#define a2	$18
#define a3	$19
#define a4	$20
#define a5	$21
#define t8	$22	/* (T)		temporary registers	*/
#define t9	$23
#define t10	$24
#define t11	$25
#define ra	$26	/* (T)		return address		*/
#define t12	$27	/* (T)		another temporary	*/
#define at_reg	$28	/* (T)		assembler scratch	*/
#define	gp	$29	/* (T)		(local) data pointer	*/
#define sp	$30	/* (S)		stack pointer		*/
#define zero	$31	/* 		wired zero		*/

/* Floating point registers  (XXXX VERIFY THIS) */
#define	fv0	$f0	/* (T)		return value (real)	*/
#define	fv1	$f1	/* (T)		return value (imaginary)*/
#define	ft0	fv1
#define	fs0	$f2	/* (S)		call-safe registers	*/
#define	fs1	$f3
#define	fs2	$f4
#define	fs3	$f5
#define	fs4	$f6
#define	fs5	$f7
#define	fs6	$f8
#define	fs7	$f9
#define	ft1	$f10	/* (T)		temporary registers	*/
#define	ft2	$f11
#define	ft3	$f12
#define	ft4	$f13
#define	ft5	$f14
#define	ft6	$f15
#define	fa0	$f16	/* (T)		argument registers	*/
#define	fa1	$f17
#define	fa2	$f18
#define	fa3	$f19
#define	fa4	$f20
#define	fa5	$f21
#define	ft7	$f22	/* (T)		more temporaries	*/
#define	ft8	$f23
#define	ft9	$f24
#define	ft10	$f25
#define	ft11	$f26
#define	ft12	$f27
#define	ft13	$f28
#define	ft14	$f29
#define	ft15	$f30
#define	fzero	$f31	/*		wired zero		*/


/* Other DEC standard names */
#define fp	$15	/* (S)		frame pointer		*/
#define ai	$25	/* (T)		argument information	*/
#define pv	$27	/* (T)		procedure value		*/
#define AT	$28	/* (T)		assembler scratch	*/


/*
 * Useful stuff.
 */
#ifdef __STDC__
#define	__CONCAT(a,b)	a ## b
#else
#define	__CONCAT(a,b)	a/**/b
#endif
#define ___CONCAT(a,b)	__CONCAT(a,b)

/*
 * Macro to make a local label name.
 */
#define	LLABEL(name,num)	___CONCAT(___CONCAT(L,name),num)

/*
 *
 * Debuggers need symbol table information to be able to properly
 * decode a stack trace.  The minimum that should be provided is:
 *
 * 	name:
 *		.proc	name,numargs
 *
 * where "name" 	is the function's name;
 *	 "numargs"	how many arguments it expects. For varargs
 *			procedures this should be a negative number,
 *			indicating the minimum required number of
 *			arguments (which is at least 1);
 *
 * NESTED functions (functions that call other functions) should define
 * how they handle their stack frame in a .frame directive:
 *
 *		.frame	framesize, pc_reg, i_mask, f_mask
 *
 * where "framesize"	is the size of the frame for this function, in bytes.
 *			That is:
 *				new_sp + framesize == old_sp
 *			Framesizes should be rounded to a cacheline size.
 *			Note that old_sp plays the role of a conventional
 *			"frame pointer";
 *	 "pc_reg"	is either a register which preserves the caller's PC
 *			or 'std', if std the saved PC should be stored at
 *				old_sp-8
 * 	 "i_mask"	is a bitmask that indicates which of the integer
 *			registers are saved. See the M_xx defines at the
 *			end for the encoding of this 32bit value.
 *	 "f_mask"	is the same, for floating point registers.
 *
 * Note, 10/31/97: This is interesting but it isn't the way gcc outputs
 * frame directives and it isn't the way the macros below output them
 * either. Frame directives look like this:
 *
 *		.frame	$15,framesize,$26,0
 *
 * If no fp is set up then $30 should be used instead of $15.
 * Also, gdb expects to find a <lda sp,-framesize(sp)> at the beginning
 * of a procedure. Don't use things like sub sp,framesize,sp for this
 * reason. End Note 10/31/97. ross@netbsd.org
 *
 * Note that registers should be saved starting at "old_sp-8", where the
 * return address should be stored. Other registers follow at -16-24-32..
 * starting from register 0 (if saved) and up. Then float registers (ifany)
 * are saved.
 *
 * If you need to alias a leaf function, or to provide multiple entry points
 * use the LEAF() macro for the main entry point and XLEAF() for the other
 * additional/alternate entry points.
 * "XLEAF"s must be nested within a "LEAF" and a ".end".
 * Similar rules for nested routines, e.g. use NESTED/XNESTED
 * Symbols that should not be exported can be declared with the STATIC_xxx
 * macros.
 *
 * All functions must be terminated by the END macro
 *
 * It is conceivable, although currently at the limits of compiler
 * technology, that while performing inter-procedural optimizations
 * the compiler/linker be able to avoid unnecessary register spills
 * if told about the register usage of LEAF procedures (and by transitive
 * closure of NESTED procedures as well).  Assembly code can help
 * this process using the .reguse directive:
 *
 *		.reguse	i_mask, f_mask
 *
 * where the register masks are built as above or-ing M_xx defines.
 *	
 *
 * All symbols are internal unless EXPORTed.  Symbols that are IMPORTed
 * must be appropriately described to the debugger.
 *
 */

/*
 * MCOUNT
 */

#ifndef GPROF
#define MCOUNT	/* nothing */
#else
#define MCOUNT							\
	.set noat;						\
	jsr	at_reg,_mcount;					\
	.set at
#endif
/*
 * PALVECT, ESETUP, and ERSAVE
 *	Declare a palcode transfer point, and carefully construct
 *	gdb symbols with an unusual _negative_ register-save offset
 *	so that gdb can find the otherwise lost PC and then
 *	invert the vector for traceback. Also, fix up framesize,
 *	allowing for the palframe for the same reason.
 */

#define PALVECT(_name_)						\
	ESETUP(_name_);						\
	ERSAVE()

#define	ESETUP(_name_)						\
	/* .loc	1 __LINE__; */					\
	.globl	_name_;						\
	.ent	_name_ 0;					\
_name_:;							\
	.set	noat;						\
	lda	sp,-(FRAME_SW_SIZE*8)(sp);			\
	.frame	$30,(FRAME_SW_SIZE+6)*8,$26,0;   /* give gdb the real size */\
	.mask	0x4000000,-0x28;				\
	.set	at

#define	ERSAVE()						\
	.set	noat;						\
	stq	at_reg,(FRAME_AT*8)(sp);			\
	.set	at;						\
	stq	ra,(FRAME_RA*8)(sp);				\
	/* .loc	1 __LINE__; */					\
	bsr	ra,exception_save_regs         /* jmp/CALL trashes pv/t12 */


/*
 * LEAF
 *	Declare a global leaf function.
 *	A leaf function does not call other functions AND does not
 *	use any register that is callee-saved AND does not modify
 *	the stack pointer.
 */
#define	LEAF(_name_,_n_args_)					\
	.globl	_name_;						\
	.ent	_name_ 0;					\
_name_:;							\
	.frame	sp,0,ra;					\
	MCOUNT
/* should have been
	.proc	_name_,_n_args_;				\
	.frame	0,ra,0,0
*/

#define	LEAF_NOPROFILE(_name_,_n_args_)					\
	.globl	_name_;						\
	.ent	_name_ 0;					\
_name_:;							\
	.frame	sp,0,ra
/* should have been
	.proc	_name_,_n_args_;				\
	.frame	0,ra,0,0
*/

/*
 * STATIC_LEAF
 *	Declare a local leaf function.
 */
#define STATIC_LEAF(_name_,_n_args_)				\
	.ent	_name_ 0;					\
_name_:;							\
	.frame	sp,0,ra;					\
	MCOUNT
/* should have been
	.proc	_name_,_n_args_;				\
	.frame	0,ra,0,0
*/
/*
 * XLEAF
 *	Global alias for a leaf function, or alternate entry point
 */
#define	XLEAF(_name_,_n_args_)					\
	.globl	_name_;						\
	.aent	_name_ 0;					\
_name_:
/* should have been
	.aproc	_name_,_n_args_;
*/

/*
 * STATIC_XLEAF
 *	Local alias for a leaf function, or alternate entry point
 */
#define	STATIC_XLEAF(_name_,_n_args_)				\
	.aent	_name_ 0;					\
_name_:
/* should have been
	.aproc	_name_,_n_args_;
*/

/*
 * NESTED
 *	Declare a (global) nested function
 *	A nested function calls other functions and needs
 *	therefore stack space to save/restore registers.
 */
#define	NESTED(_name_, _n_args_, _framesize_, _pc_reg_, _i_mask_, _f_mask_ ) \
	.globl	_name_;						\
	.ent	_name_ 0;					\
_name_:;							\
	.frame	sp,_framesize_,_pc_reg_;			\
	.livereg _i_mask_,_f_mask_;				\
	MCOUNT
/* should have been
	.proc	_name_,_n_args_;				\
	.frame	_framesize_, _pc_reg_, _i_mask_, _f_mask_
*/

#define	NESTED_NOPROFILE(_name_, _n_args_, _framesize_, _pc_reg_, _i_mask_, _f_mask_ ) \
	.globl	_name_;						\
	.ent	_name_ 0;					\
_name_:;							\
	.frame	sp,_framesize_,_pc_reg_;			\
	.livereg _i_mask_,_f_mask_
/* should have been
	.proc	_name_,_n_args_;				\
	.frame	_framesize_, _pc_reg_, _i_mask_, _f_mask_
*/

/*
 * STATIC_NESTED
 *	Declare a local nested function.
 */
#define	STATIC_NESTED(_name_, _n_args_, _framesize_, _pc_reg_, _i_mask_, _f_mask_ ) \
	.ent	_name_ 0;					\
_name_:;							\
	.frame	sp,_framesize_,_pc_reg_;			\
	.livereg _i_mask_,_f_mask_;				\
	MCOUNT
/* should have been
	.proc	_name_,_n_args_;				\
	.frame	_framesize_, _pc_reg_, _i_mask_, _f_mask_
*/

/*
 * XNESTED
 *	Same as XLEAF, for a nested function.
 */
#define	XNESTED(_name_,_n_args_)				\
	.globl	_name_;						\
	.aent	_name_ 0;					\
_name_:
/* should have been
	.aproc	_name_,_n_args_;
*/


/*
 * STATIC_XNESTED
 *	Same as STATIC_XLEAF, for a nested function.
 */
#define	STATIC_XNESTED(_name_,_n_args_)				\
	.aent	_name_ 0;					\
_name_:
/* should have been
	.aproc	_name_,_n_args_;
*/


/*
 * END
 *	Function delimiter
 */
#define	END(_name_)						\
	.end	_name_


/*
 * CALL
 *	Function invocation
 */
#define	CALL(_name_)						\
	/* .loc	1 __LINE__; */					\
	jsr	ra,_name_;					\
	ldgp	gp,0(ra)
/* but this would cover longer jumps
	br	ra,.+4;						\
	bsr	ra,_name_
*/


/*
 * RET
 *	Return from function
 */
#define	RET							\
	ret	zero,(ra),1


/*
 * EXPORT
 *	Export a symbol
 */
#define	EXPORT(_name_)						\
	.globl	_name_;						\
_name_:


/*
 * IMPORT
 *	Make an external name visible, typecheck the size
 */
#define	IMPORT(_name_, _size_)					\
	.extern	_name_,_size_


/*
 * ABS
 *	Define an absolute symbol
 */
#define	ABS(_name_, _value_)					\
	.globl	_name_;						\
_name_	=	_value_


/*
 * BSS
 *	Allocate un-initialized space for a global symbol
 */
#define	BSS(_name_,_numbytes_)					\
	.comm	_name_,_numbytes_

/*
 * VECTOR
 *	Make an exception entry point look like a called function,
 *	to make it digestible to the debugger (KERNEL only)
 */
#define	VECTOR(_name_, _i_mask_)				\
	.globl	_name_;						\
	.ent	_name_ 0;					\
_name_:;							\
	.mask	_i_mask_|IM_EXC,0;				\
	.frame	sp,MSS_SIZE,ra;				
/*	.livereg _i_mask_|IM_EXC,0	*/
/* should have been
	.proc	_name_,1;					\
	.frame	MSS_SIZE,$31,_i_mask_,0;			\
*/

/*
 * MSG
 *	Allocate space for a message (a read-only ascii string)
 */
#define	ASCIZ	.asciz
#define	MSG(msg,reg,label)					\
	lda reg, label;						\
	.data;							\
label:	ASCIZ msg;						\
	.text;

/*
 * PRINTF
 *	Print a message
 */
#define	PRINTF(msg,label)					\
	MSG(msg,a0,label);					\
	CALL(printf)

/*
 * PANIC
 *	Fatal error (KERNEL)
 */
#define	PANIC(msg,label)					\
	MSG(msg,a0,label);					\
	CALL(panic)

/*
 * Register mask defines, used to define both save
 * and use register sets.
 *
 * NOTE: The bit order should HAVE BEEN maintained when saving
 *	 registers on the stack: sp goes at the highest
 *	 address, gp lower on the stack, etc etc
 *	 BUT NOONE CARES ABOUT DEBUGGERS AT MIPS
 */

#define	IM_EXC	0x80000000
#define	IM_SP	0x40000000
#define	IM_GP	0x20000000
#define	IM_AT	0x10000000
#define	IM_T12	0x08000000
#	define	IM_PV	IM_T4
#define	IM_RA	0x04000000
#define	IM_T11	0x02000000
#	define	IM_AI	IM_T3
#define	IM_T10	0x01000000
#define	IM_T9	0x00800000
#define	IM_T8	0x00400000
#define	IM_A5	0x00200000
#define	IM_A4	0x00100000
#define	IM_A3	0x00080000
#define	IM_A2	0x00040000
#define	IM_A1	0x00020000
#define	IM_A0	0x00010000
#define	IM_S6	0x00008000
#define	IM_S5	0x00004000
#define	IM_S4	0x00002000
#define	IM_S3	0x00001000
#define	IM_S2	0x00000800
#define	IM_S1	0x00000400
#define	IM_S0	0x00000200
#define	IM_T7	0x00000100
#define	IM_T6	0x00000080
#define	IM_T5	0x00000040
#define	IM_T4	0x00000020
#define	IM_T3	0x00000010
#define	IM_T2	0x00000008
#define	IM_T1	0x00000004
#define	IM_T0	0x00000002
#define	IM_V0	0x00000001

#define	FM_T15	0x40000000
#define	FM_T14	0x20000000
#define	FM_T13	0x10000000
#define	FM_T12	0x08000000
#define	FM_T11	0x04000000
#define	FM_T10	0x02000000
#define	FM_T9	0x01000000
#define	FM_T8	0x00800000
#define	FM_T7	0x00400000
#define	FM_A5	0x00200000
#define	FM_A4	0x00100000
#define	FM_A3	0x00080000
#define	FM_A2	0x00040000
#define	FM_A1	0x00020000
#define	FM_A0	0x00010000
#define	FM_T6	0x00008000
#define	FM_T5	0x00004000
#define	FM_T4	0x00002000
#define	FM_T3	0x00001000
#define	FM_T2	0x00000800
#define	FM_T1	0x00000400
#define	FM_S7	0x00000200
#define	FM_S6	0x00000100
#define	FM_S5	0x00000080
#define	FM_S4	0x00000040
#define	FM_S3	0x00000020
#define	FM_S2	0x00000010
#define	FM_S1	0x00000008
#define	FM_S0	0x00000004
#define	FM_T0	0x00000002
#define	FM_V1	FM_T0
#define	FM_V0	0x00000001

/* Pull in PAL "function" codes. */
#include <machine/pal.h>

/*
 * Load the global pointer.
 */
#define	LDGP(reg)						\
	ldgp	gp, 0(reg)

/*
 * STRONG_ALIAS, WEAK_ALIAS
 *	Create a strong or weak alias.
 */
#define STRONG_ALIAS(alias,sym)					\
	.global alias;						\
	alias = sym
#define WEAK_ALIAS(alias,sym)					\
	.weak alias;						\
	alias = sym
