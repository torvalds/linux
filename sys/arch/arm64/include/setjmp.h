/*	$OpenBSD: setjmp.h,v 1.2 2023/04/11 00:45:07 jsg Exp $	*/
/*	$NetBSD: setjmp.h,v 1.2 2001/08/25 14:45:59 bjh21 Exp $	*/

/*
 * machine/setjmp.h: machine dependent setjmp-related information.
 */

#define	_JBLEN	64		/* size, in longs, of a jmp_buf */

/*
 * NOTE: The internal structure of a jmp_buf is *PRIVATE*
 *       This information is provided as there is software
 *       that fiddles with this with obtain the stack pointer
 *	 (yes really ! and it's commercial !).
 *
 * Description of the setjmp buffer
 *
 * word  0	magic number	(dependant on creator)
 *       1 -  3	f4		fp register 4
 *	 4 -  6	f5		fp register 5
 *	 7 -  9 f6		fp register 6
 *	10 - 12	f7		fp register 7
 *	13	fpsr		fp status register
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
 *	25	signal mask	(dependant on magic)
 *	26	(con't)
 *	27	(con't)
 *	28	(con't)
 *
 * The magic number identifies the jmp_buf and
 * how the buffer was created as well as providing
 * a sanity check.
 *
 * A side note I should mention - please do not tamper
 * with the floating point fields. While they are
 * always saved and restored at the moment this cannot
 * be guaranteed especially if the compiler happens
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

/* Valid for all jmp_buf's */

#define _JB_MAGIC		 0
#define _JB_REG_F4		 1
#define _JB_REG_F5		 4
#define _JB_REG_F6		 7
#define _JB_REG_F7		10
#define _JB_REG_FPSR		13
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
