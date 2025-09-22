#	$OpenBSD: Makefile,v 1.4 2011/05/01 04:25:40 guenther Exp $

PROG=	globtest

run-regress-${PROG}:
	mkdir -m 755 -p `sed 's@/[^/]*$$@@' ${.CURDIR}/files | sort -u`
	umask 022; touch `cat ${.CURDIR}/files`
	chmod 0755 `grep '/r[^/]*$$' ${.CURDIR}/files`
	chmod 0444 `grep '/s[^/]*$$' ${.CURDIR}/files`
	chmod 0711 `grep '/t[^/]*$$' ${.CURDIR}/files`
	./${PROG} ${.CURDIR}/${PROG}.in

clean:
	rm -rf fake ${PROG} ${OBJS} *.core

.include <bsd.regress.mk>
