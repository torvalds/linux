# $OpenBSD: Makefile,v 1.1 2016/09/29 11:04:45 schwarze Exp $

REGRESS_TARGETS=check
KSH=/bin/ksh
PERL=/usr/bin/perl
CATEGORIES=pdksh,sh,ksh,posix,posix-upu

# Avoid /tmp for test temp files if we have an obj dir
.if ${.CURDIR} != ${.OBJDIR}
TEMPDIR=${.OBJDIR}
.else
TEMPDIR=/tmp
.endif

check test:
	${PERL} ${.CURDIR}/../th -s ${.CURDIR}/.. -p ${KSH} \
		-C ${CATEGORIES} -T ${TEMPDIR}

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
