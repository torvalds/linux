#	$OpenBSD: Makefile,v 1.10 2023/03/13 20:20:59 miod Exp $
#	$NetBSD: Makefile,v 1.7 1997/04/10 23:03:38 cgd Exp $

SUBDIR=	 nboot

.if ${MACHINE} == "alpha"
SUBDIR+= bootxx netboot libsa libz
.endif

SUBDIR+= setnetbootinfo installboot

.include <bsd.subdir.mk>
