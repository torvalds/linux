#	$OpenBSD: Makefile,v 1.16 2023/08/13 07:02:36 miod Exp $

SUBDIR+= exp fenv floor fpaccuracy lgamma modf msun nextafter rint round \
	 tgamma toint trivial1

.if defined(REGRESS_FULL) || make(clean) || make(cleandir) || make(obj)
SUBDIR+= cephes
.endif

install:

.include <bsd.subdir.mk>
