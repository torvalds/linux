# $FreeBSD$

PROG= test1
MAN=
NFSSERVER?= herring.nlsystems.com
CLEANFILES= test1.zeros test1.good test1.data test1.scratch

all: test1 test1.zeros test1.good
	@cp ${.OBJDIR}/test1.zeros ${.OBJDIR}/test1.data
	@if [ `hostname` != ${NFSSERVER} ] ; then \
		ssh ${NFSSERVER} touch ${.OBJDIR}/test1.data; \
	fi
	@cd ${.OBJDIR}; ${.OBJDIR}/test1
	@if cmp -s ${.OBJDIR}/test1.data ${.OBJDIR}/test1.good && \
	   cmp -s ${.OBJDIR}/test1.scratch ${.OBJDIR}/test1.good ; then \
		echo passed; \
	else \
		echo failed; \
	fi

test1.zeros: test1.zeros.uu
	uudecode $?

test1.good: test1.good.uu
	uudecode $?

.include <bsd.prog.mk>
