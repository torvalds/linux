# $FreeBSD$

SUBDIR=		capi
.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SUBDIR+=	padlock
.endif
SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
