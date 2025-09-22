#	$OpenBSD: Makefile,v 1.21 2014/01/20 08:49:58 deraadt Exp $

.if ${MACHINE} == "i386"
SUBDIR=	mbr cdbr
.endif
SUBDIR+=biosboot boot cdboot fdboot pxeboot

.include <bsd.subdir.mk>
