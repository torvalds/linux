# $OpenBSD: Makefile,v 1.1 2003/02/04 21:21:00 marc Exp $
#

PROG=		stdfiles

PROG2=		stdfiles2
OBJS2=		stdfiles.o

CLEANFILES+=	${PROG2} threads no-threads

REGRESS_TARGETS=diff

# run program after linking with pthreads
#
threads: ${PROG}
	./${PROG} > threads

# build the same program, but not linked with pthreads
#
${PROG2}: ${LIBCRT0} ${OBJS} ${LIBC} ${CRTBEGIN} ${CRTEND} ${DPADD}
	${CC} ${LDFLAGS} ${LDSTATIC} -o ${.TARGET} ${OBJS}

# run the non-pthread version
#
no-threads: ${PROG2}
	./${PROG2} > no-threads

# compare the thread output to the non-thread output, should be the same
#
diff: threads no-threads
	diff -u threads no-threads

.PHONY: threads no-threads diff

.include <bsd.regress.mk>
