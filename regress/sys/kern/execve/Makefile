#	$OpenBSD: Makefile,v 1.5 2002/09/02 20:01:44 avsm Exp $

PROG=	doexec
LDSTATIC= -static

RP=	${.OBJDIR}/${PROG}
TD=	${.CURDIR}/tests
OD=	${.CURDIR}/good

TESTFILES= empty nonexistshell devnullscript badinterplen goodscript \
	   scriptarg scriptarg-nospace goodaout truncaout

CLEANFILES+= ${TESTFILES}

all:	${PROG} ${TESTFILES}

REGRESS_TARGETS=	test-empty test-nonexist \
		test-nonexistshell test-devnullscript test-badinterplen \
		test-goodscript test-scriptarg test-scriptarg-nospace \
		test-goodaout test-truncaout

empty: ${TD}/empty.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-empty:	${PROG} empty
	${RP} ${.OBJDIR}/empty | diff - ${OD}/empty

nonexist: ${TD}/nonexist.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-nonexist:	${PROG}
	${RP} ${.OBJDIR}/nonexistent | diff - ${OD}/nonexistent

nonexistshell: ${TD}/nonexistshell.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-nonexistshell: ${PROG} nonexistshell
	${RP} ${.OBJDIR}/nonexistshell | diff - ${OD}/nonexistshell

devnullscript: ${TD}/devnullscript.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-devnullscript: ${PROG} devnullscript
	${RP} ${.OBJDIR}/devnullscript | diff - ${OD}/devnullscript

badinterplen: ${TD}/badinterplen.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-badinterplen: ${PROG} badinterplen
	${RP} ${.OBJDIR}/badinterplen | diff - ${OD}/badinterplen

goodscript: ${TD}/goodscript.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-goodscript: ${PROG} goodscript
	${RP} ${.OBJDIR}/goodscript | diff - ${OD}/goodscript

scriptarg: ${TD}/scriptarg.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-scriptarg: ${PROG} scriptarg
	${RP} ${.OBJDIR}/scriptarg 2>&1 | diff - ${OD}/scriptarg

scriptarg-nospace: ${TD}/scriptarg-nospace.in
	${INSTALL} -c -m 755 ${.ALLSRC} ${.TARGET}

test-scriptarg-nospace: ${PROG} scriptarg-nospace
	${RP} ${.OBJDIR}/scriptarg-nospace 2>&1 | diff - ${OD}/scriptarg-nospace

goodaout: ${TD}/goodaout.c
	${LINK.c} ${LDSTATIC} -o ${.TARGET} ${TD}/goodaout.c ${LDLIBS}

test-goodaout: ${PROG} goodaout
	${RP} ${.OBJDIR}/goodaout | diff - ${OD}/goodaout

truncaout: goodaout
	/bin/rm -rf truncaout
	dd if=${.OBJDIR}/goodaout of=truncaout bs=16 count=1
	chmod a+x truncaout

test-truncaout: ${PROG} truncaout
	${RP} ${.OBJDIR}/truncaout | diff - ${OD}/truncaout

.include <bsd.regress.mk>
