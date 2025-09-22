#	$OpenBSD: Makefile,v 1.7 2023/05/09 19:07:37 otto Exp $

REGRESS_TARGETS=        t1 t2 t3 t4 t5 t6 t7 t8 t9 t10 t11 t12
PROG=			malloc_general

.include <bsd.regress.mk>

t1:	malloc_general
	MALLOC_OPTIONS=su ${.OBJDIR}/malloc_general

t2:	malloc_general
	MALLOC_OPTIONS=suC ${.OBJDIR}/malloc_general

t3:	malloc_general
	MALLOC_OPTIONS=suJ ${.OBJDIR}/malloc_general

t4:	malloc_general
	MALLOC_OPTIONS=suF ${.OBJDIR}/malloc_general

t5:	malloc_general
	MALLOC_OPTIONS=suG ${.OBJDIR}/malloc_general

t6:	malloc_general
	MALLOC_OPTIONS=suS ${.OBJDIR}/malloc_general

t7:	malloc_general
	MALLOC_OPTIONS=suFGJ ${.OBJDIR}/malloc_general

t8:	malloc_general
	MALLOC_OPTIONS=suCJ ${.OBJDIR}/malloc_general

t9:	malloc_general
	MALLOC_OPTIONS=suCJJ ${.OBJDIR}/malloc_general

t10:	malloc_general
	MALLOC_OPTIONS=suJS ${.OBJDIR}/malloc_general

t11:	malloc_general
	MALLOC_OPTIONS=suFGJJ ${.OBJDIR}/malloc_general

t12:	malloc_general
	MALLOC_OPTIONS=suFCJJ ${.OBJDIR}/malloc_general

