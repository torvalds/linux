/*	$OpenBSD: setjmp.h,v 1.6 2023/04/11 00:45:07 jsg Exp $	*/
/*	$NetBSD: setjmp.h,v 1.2 2001/08/25 14:45:59 bjh21 Exp $	*/

/*
 * machine/setjmp.h: machine dependent setjmp-related information.
 */

#define	_JBLEN	64		/* size, in longs, of a jmp_buf */

/*
 * Description of the setjmp buffer
 *
 * word  0	magic number	(dependant on creator)
 *       1	fpscr		fpscr
 *       2 - 17	d8 - d15	vfp registers
 *	18	r13		register 13 (sp) XOR cookie0
 *	19	r14		register 14 (lr) XOR cookie1
 *	20	r4		register 4
 *	21	r5		register 5
 *	22	r6		register 6
 *	23	r7		register 7
 *	24	r8		register 8
 *	25	r9		register 9
 *	26	r10		register 10 (sl)
 *	27	r11		register 11 (fp)
 *	28	unused		unused
 *	29	signal mask	(dependant on magic)
 *	30	(con't)
 *	31	(con't)
 *	32	(con't)
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
#define _JB_REG_R4		20
#define _JB_REG_R5		21
#define _JB_REG_R6		22
#define _JB_REG_R7		23
#define _JB_REG_R8		24
#define _JB_REG_R9		25
#define _JB_REG_R10		26
#define _JB_REG_R11		27

/* Only valid with the _JB_MAGIC_SETJMP magic */

#define _JB_SIGMASK		29
