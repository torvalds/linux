# $FreeBSD$

PROG=	newgrp

LIBADD=	crypt util

.if defined(ENABLE_SUID_NEWGRP)
BINMODE=	4555
PRECIOUSPROG=
.endif

.include <bsd.prog.mk>
