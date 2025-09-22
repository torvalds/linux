# $OpenBSD: Makefile,v 1.4 2021/02/09 16:58:00 claudio Exp $

PROG=	syscall_library
.if ${.CURDIR} == ${.OBJDIR}
LIBDIR=	../libgadgetsyscall
.else
LIBDIR=	${.CURDIR}/../libgadgetsyscall/${__objdir}
.endif
LDADD+=	-L${LIBDIR} -lgadgetsyscall

run-regress-${PROG}: ${PROG}
	rm -f ${PROG}.core
	ulimit -c unlimited && ! LD_LIBRARY_PATH=${LIBDIR} ./${PROG}
	if [ ! -e ${PROG}.core ]; then echo "No coredump"; false; fi

.include <bsd.regress.mk>
