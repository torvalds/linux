/*	$NetBSD: setjmp.h,v 1.5 2013/01/11 13:56:32 matt Exp $	*/
/* $FreeBSD$ */

/*
 * machine/setjmp.h: machine dependent setjmp-related information.
 */

#ifndef _MACHINE_SETJMP_H_
#define _MACHINE_SETJMP_H_

#define	_JBLEN	64		/* size, in longs, of a jmp_buf */

/*
 * NOTE: The internal structure of a jmp_buf is *PRIVATE*
 *       This information is provided as there is software
 *       that fiddles with this with obtain the stack pointer
 *	 (yes really ! and its commercial !).
 *
 * Description of the setjmp buffer
 *
 * word  0	magic number	(dependent on creator)
 *	13	fpscr		vfp status control register
 *	14	r4		register 4
 *	15	r5		register 5
 *	16	r6		register 6
 *	17	r7		register 7
 *	18	r8		register 8
 *	19	r9		register 9
 *	20	r10		register 10 (sl)
 *	21	r11		register 11 (fp)
 *	22	r12		register 12 (ip)
 *	23	r13		register 13 (sp)
 *	24	r14		register 14 (lr)
 *	25	signal mask	(dependent on magic)
 *	26	(con't)
 *	27	(con't)
 *	28	(con't)
 *	32-33	d8		(vfp register d8)
 *	34-35	d9		(vfp register d9)
 *	36-37	d10		(vfp register d10)
 *	38-39	d11		(vfp register d11)
 *	40-41	d12		(vfp register d12)
 *	42-43	d13		(vfp register d13)
 *	44-45	d14		(vfp register d14)
 *	46-47	d15		(vfp register d15)
 *
 * The magic number number identifies the jmp_buf and
 * how the buffer was created as well as providing
 * a sanity check
 *
 * A side note I should mention - Please do not tamper
 * with the floating point fields. While they are
 * always saved and restored at the moment this cannot
 * be garenteed especially if the compiler happens
 * to be generating soft-float code so no fp
 * registers will be used.
 *
 * Whilst this can be seen an encouraging people to
 * use the setjmp buffer in this way I think that it
 * is for the best then if changes occur compiles will
 * break rather than just having new builds falling over
 * mysteriously.
 */

#define _JB_MAGIC__SETJMP	0x4278f500
#define _JB_MAGIC_SETJMP	0x4278f501
#define _JB_MAGIC__SETJMP_VFP	0x4278f502
#define _JB_MAGIC_SETJMP_VFP	0x4278f503

/* Valid for all jmp_buf's */

#define _JB_MAGIC		 0
#define _JB_REG_FPSCR		13
#define _JB_REG_R4		14
#define _JB_REG_R5		15
#define _JB_REG_R6		16
#define _JB_REG_R7		17
#define _JB_REG_R8		18
#define _JB_REG_R9		19
#define _JB_REG_R10		20
#define _JB_REG_R11		21
#define _JB_REG_R12		22
#define _JB_REG_R13		23
#define _JB_REG_R14		24

/* Only valid with the _JB_MAGIC_SETJMP magic */

#define _JB_SIGMASK		25

#define	_JB_REG_D8		32
#define	_JB_REG_D9		34
#define	_JB_REG_D10		36
#define	_JB_REG_D11		38
#define	_JB_REG_D12		40
#define	_JB_REG_D13		42
#define	_JB_REG_D14		44
#define	_JB_REG_D15		46

#ifndef __ASSEMBLER__
#if __BSD_VISIBLE || __POSIX_VISIBLE || __XSI_VISIBLE
typedef struct _sigjmp_buf { int _sjb[_JBLEN + 1]; } sigjmp_buf[1];
#endif

typedef struct _jmp_buf { int _jb[_JBLEN + 1]; } jmp_buf[1];
#endif

#endif /* !_MACHINE_SETJMP_H_ */
