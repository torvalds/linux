# $FreeBSD$

.include <src.opts.mk>

PACKAGE=runtime
PROG=	camcontrol
SRCS=	camcontrol.c util.c
.if !defined(RELEASE_CRUNCH)
SRCS+=	attrib.c epc.c fwdownload.c modeedit.c persist.c progress.c timestamp.c zone.c
.else
CFLAGS+= -DMINIMALISTIC
.endif
.if ${MK_NVME} != "no"
.PATH:	${SRCTOP}/sbin/nvmecontrol
CFLAGS+= -I${SRCTOP}/sbin/nvmecontrol -DWITH_NVME
SRCS+=	identify_ext.c nc_util.c
.PATH:	${SRCTOP}/sys/dev/nvme
SRCS+=	nvme_util.c
.endif
# This is verboten
.if ${MACHINE_CPUARCH} == "arm"
WARNS?= 3
.endif
LIBADD=	cam sbuf util
MAN=	camcontrol.8

.include <bsd.prog.mk>
