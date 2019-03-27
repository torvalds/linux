/*	$FreeBSD$  */
/*	from:	NetBSD: setjmp.S,v 1.1 1998/01/27 15:13:12 sakamoto Exp $  */
/*	from:	OpenBSD: setjmp.S,v 1.2 1996/12/28 06:22:18 rahnds Exp 	*/
/* kernel version of this file, does not have signal goop */
/* int setjmp(jmp_buf env) */

#include <machine/asm.h>

#ifdef __powerpc64__
#define LD_REG	ld
#define	ST_REG	std
#define	REGWIDTH 8
#else
#define	LD_REG	lwz
#define	ST_REG	stw
#define	REGWIDTH 4
#endif

#define JMP_r1	1*REGWIDTH
#define JMP_r2	2*REGWIDTH
#define JMP_r14	3*REGWIDTH
#define JMP_r15 4*REGWIDTH
#define JMP_r16 5*REGWIDTH
#define JMP_r17 6*REGWIDTH
#define JMP_r18 7*REGWIDTH
#define JMP_r19 8*REGWIDTH
#define JMP_r20 9*REGWIDTH
#define JMP_r21 10*REGWIDTH
#define JMP_r22 11*REGWIDTH
#define JMP_r23 12*REGWIDTH
#define JMP_r24 13*REGWIDTH
#define JMP_r25 14*REGWIDTH
#define JMP_r26 15*REGWIDTH
#define JMP_r27 16*REGWIDTH
#define JMP_r28 17*REGWIDTH
#define JMP_r29 18*REGWIDTH
#define JMP_r30 19*REGWIDTH
#define JMP_r31 20*REGWIDTH
#define JMP_lr 	21*REGWIDTH
#define JMP_cr	22*REGWIDTH
#define JMP_ctr	23*REGWIDTH
#define JMP_xer	24*REGWIDTH
#define JMP_sig	25*REGWIDTH

ASENTRY_NOPROF(_setjmp)
	ST_REG 31, JMP_r31(3)
	/* r1, r2, r14-r30 */
	ST_REG 1,  JMP_r1 (3)
	ST_REG 2,  JMP_r2 (3)
	ST_REG 14, JMP_r14(3)
	ST_REG 15, JMP_r15(3)
	ST_REG 16, JMP_r16(3)
	ST_REG 17, JMP_r17(3)
	ST_REG 18, JMP_r18(3)
	ST_REG 19, JMP_r19(3)
	ST_REG 20, JMP_r20(3)
	ST_REG 21, JMP_r21(3)
	ST_REG 22, JMP_r22(3)
	ST_REG 23, JMP_r23(3)
	ST_REG 24, JMP_r24(3)
	ST_REG 25, JMP_r25(3)
	ST_REG 26, JMP_r26(3)
	ST_REG 27, JMP_r27(3)
	ST_REG 28, JMP_r28(3)
	ST_REG 29, JMP_r29(3)
	ST_REG 30, JMP_r30(3)
	/* cr, lr, ctr, xer */
	mfcr 0
	ST_REG 0, JMP_cr(3)
	mflr 0
	ST_REG 0, JMP_lr(3)
	mfctr 0
	ST_REG 0, JMP_ctr(3)
	mfxer 0
	ST_REG 0, JMP_xer(3)
	/* f14-f31, fpscr */
	li 3, 0
	blr


.extern sigsetmask
ASENTRY_NOPROF(_longjmp)
	LD_REG 31, JMP_r31(3)
	/* r1, r2, r14-r30 */
	LD_REG 1,  JMP_r1 (3)
	LD_REG 2,  JMP_r2 (3)
	LD_REG 14, JMP_r14(3)
	LD_REG 15, JMP_r15(3)
	LD_REG 16, JMP_r16(3)
	LD_REG 17, JMP_r17(3)
	LD_REG 18, JMP_r18(3)
	LD_REG 19, JMP_r19(3)
	LD_REG 20, JMP_r20(3)
	LD_REG 21, JMP_r21(3)
	LD_REG 22, JMP_r22(3)
	LD_REG 23, JMP_r23(3)
	LD_REG 24, JMP_r24(3)
	LD_REG 25, JMP_r25(3)
	LD_REG 26, JMP_r26(3)
	LD_REG 27, JMP_r27(3)
	LD_REG 28, JMP_r28(3)
	LD_REG 29, JMP_r29(3)
	LD_REG 30, JMP_r30(3)
	/* cr, lr, ctr, xer */
	LD_REG 0, JMP_cr(3)
	mtcr 0
	LD_REG 0, JMP_lr(3)
	mtlr 0
	LD_REG 0, JMP_ctr(3)
	mtctr 0
	LD_REG 0, JMP_xer(3)
	mtxer 0
	/* f14-f31, fpscr */
	mr 3, 4
	blr
