# $Id: Makefile,v 1.1 2004/08/13 18:30:24 max Exp $
# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/vkbd

KMOD=	vkbd
SRCS=	vkbd.c opt_kbd.h

.if !defined(KERNBUILDDIR)
opt_kbd.h:
	echo "#define KBD_INSTALL_CDEV 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
