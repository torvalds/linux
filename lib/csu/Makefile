# $FreeBSD$

.include <src.opts.mk>

.if exists(${.CURDIR}/${MACHINE_ARCH})
SUBDIR+= ${MACHINE_ARCH}
.else
SUBDIR+= ${MACHINE_CPUARCH}
.endif

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.subdir.mk>
