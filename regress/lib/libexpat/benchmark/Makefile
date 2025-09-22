# $OpenBSD: Makefile,v 1.1.1.1 2017/06/30 14:56:08 bluhm Exp $

# take any large xml file located in our source tree
XMLFILE?=	${BSDSRCDIR}/gnu/lib/libstdc++/libstdc++/docs/html/17_intro/porting-howto.xml

run-regress-benchmark: ${PROG}
	time ./${PROG} ${XMLFILE} 65535 10000

.include <bsd.regress.mk>
