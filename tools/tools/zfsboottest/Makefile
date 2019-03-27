# $FreeBSD$

.PATH:		${SRCTOP}/stand/zfs ${SRCTOP}/sys/cddl/boot/zfs

BINDIR?=	/usr/bin
SCRIPTSDIR?=	/usr/bin

PROG=		zfsboottest
SCRIPTS=	zfsboottest.sh
SCRIPTSNAME=	zfsboottest.sh
MAN=

CFLAGS=	-O1 \
	-I${SRCTOP}/stand/zfs \
	-I${SRCTOP}/sys/cddl/boot/zfs \
	-I. \
	-fdiagnostics-show-option \
	-W -Wextra -Wno-sign-compare -Wno-unused-parameter
CFLAGS+=-m32
LIBADD+=	md

.if ${MACHINE_CPUARCH} == "amd64"
beforedepend zfsboottest.o: machine
CLEANFILES+=	machine
machine:
	ln -sf ${SRCTOP}/sys/i386/include machine
.endif

.include <bsd.prog.mk>
