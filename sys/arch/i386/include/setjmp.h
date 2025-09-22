/*	$OpenBSD: setjmp.h,v 1.4 2020/12/13 21:21:32 bluhm Exp $	*/
/*	$NetBSD: setjmp.h,v 1.1 1994/12/20 10:36:43 cgd Exp $	*/

/*
 * machine/setjmp.h: machine dependent setjmp-related information.
 */

#define _JB_EIP		0
#define _JB_EBX		1
#define _JB_ESP		2
#define _JB_EBP		3
#define _JB_ESI		4
#define _JB_EDI		5
#define _JB_SIGMASK	6
#define _JB_SIGFLAG	7
#define _JB_FCW		8

#define _JBLEN		10	/* size, in longs, of a jmp_buf */
