#	$OpenBSD: Makefile,v 1.7 2015/09/02 04:12:49 deraadt Exp $

.if ${MACHINE} == "amd64"
SUBDIR=	mbr cdbr
.endif
SUBDIR+=biosboot boot cdboot fdboot pxeboot efiboot

.include <bsd.subdir.mk>
