#	$OpenBSD: Makefile,v 1.4 2007/11/25 20:37:24 maja Exp $

.if ${MACHINE} == "hppa"
SUBDIR= libsa libz
.endif
SUBDIR+= mkboot boot
.if ${MACHINE} == "hppa"
SUBDIR+= cdboot
.endif

.include <bsd.subdir.mk>
