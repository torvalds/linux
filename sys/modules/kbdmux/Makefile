# $Id: Makefile,v 1.1 2005/06/23 00:30:50 max Exp $
# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/kbdmux

KMOD=	kbdmux
SRCS=	kbdmux.c opt_evdev.h opt_kbd.h opt_kbdmux.h bus_if.h \
	device_if.h

.if !defined(KERNBUILDDIR)
opt_kbd.h:
	echo "#define KBD_INSTALL_CDEV 1" > ${.TARGET}
.endif

.include <bsd.kmod.mk>
