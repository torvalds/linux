# $FreeBSD$

MAN=

.include <src.opts.mk>

.PATH: ${SRCTOP}/contrib/ntp/ntpdc

PROG=	ntpdc
SRCS= 	ntpdc.c ntpdc_ops.c ntpdc-opts.c version.c

CFLAGS+= -I${SRCTOP}/contrib/ntp/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/unix/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/pthreads/include \
	-I${SRCTOP}/contrib/ntp/sntp/libopts \
	-I${.CURDIR}/lib/libc/${MACHINE_ARCH} \
	-I${.CURDIR:H} -I${.CURDIR}

LIBADD=	edit ntp m opts pthread
CFLAGS+=	-DHAVE_LIBEDIT -DHAVE_READLINE_READLINE_H \
		-I${SYSROOT:U${DESTDIR}}/${INCLUDEDIR}/edit

.if ${MK_OPENSSL} != "no"
LIBADD+=	crypto
.else
LIBADD+=	md
.endif

CLEANFILES+= .version version.c

version.c:
	sh -e ${.CURDIR:H}/scripts/mkver ntpdc

.include <bsd.prog.mk>
