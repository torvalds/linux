#	$OpenBSD: Makefile,v 1.1 2003/02/11 19:39:30 miod Exp $

AWK=	awk
PROG=	devlist2h.awk
SOURCE=	hildevs

${SOURCE}_data.h: ${SOURCE} ${PROG}
	/bin/rm -f $@
	${AWK} -f ${PROG} ${SOURCE} > $@
