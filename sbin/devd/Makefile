# $FreeBSD$

.include <src.opts.mk>

WARNS?= 3
PACKAGE=runtime
CONFGROUPS=	CONFS DEVD
CONFS=	devd.conf
DEVD=	devmatch.conf
DEVDDIR=	/etc/devd
.if ${MK_ACPI} != "no"
DEVD+=	asus.conf
.endif

.if ${MK_HYPERV} != "no"
DEVD+=	hyperv.conf
.endif

.if ${MK_USB} != "no"
DEVD+=	uath.conf ulpt.conf
.endif

.if ${MACHINE_ARCH} == "powerpc"
DEVD+=	apple.conf
.endif

.if ${MK_ZFS} != "no"
DEVD+=	zfs.conf
.endif

PROG_CXX=devd
SRCS=	devd.cc token.l parse.y y.tab.h
MAN=	devd.8 devd.conf.5

NO_SHARED?=YES

LIBADD=	l util

YFLAGS+=-v
CFLAGS+=-I. -I${.CURDIR}
CFLAGS.clang += -Wno-missing-variable-declarations
CFLAGS.gcc = -Wno-redundant-decls
CXXFLAGS.gcc = -Wno-redundant-decls

CLEANFILES= y.output y.tab.i

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
