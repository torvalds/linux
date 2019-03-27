# $FreeBSD$

TESTNUM=	sys

CLEANFILES+= test.c

.include <../Makefile.inc>
.include <bsd.lib.mk>

test.c: ../../makesyscalls-fake.sh
	sh ${.CURDIR}/../../makesyscalls-fake.sh /usr/src/sys/kern/syscalls.master > ${.TARGET}
