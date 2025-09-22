/*	$OpenBSD: setjmp.h,v 1.2 2020/10/21 19:00:14 kettenis Exp $	*/
/*	$NetBSD: setjmp.h,v 1.1 2003/04/26 18:39:47 fvdl Exp $	*/

/*
 * machine/setjmp.h: machine dependent setjmp-related information.
 * These are only the callee-saved registers, code calling setjmp
 * will expect the rest to be clobbered anyway.
 */

#define _JB_RBX		0
#define _JB_RBP		1
#define _JB_R12		2
#define _JB_R13		3
#define _JB_R14		4
#define _JB_R15		5
#define _JB_RSP		6
#define _JB_PC		7
#define _JB_SIGFLAG	8
#define _JB_SIGMASK	9
#define _JB_MXCSR	10

#define	_JBLEN	11		/* size, in longs, of a jmp_buf */
