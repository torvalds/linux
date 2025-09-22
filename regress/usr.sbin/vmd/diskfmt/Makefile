#	$OpenBSD: Makefile,v 1.4 2021/06/18 14:35:59 dv Exp $

# This regression test creates a raw disk image and a
# qcow disk image, and scribbles the same data to both
# of them. It verifies that they both have the same
# result.
VMD_DIR=		usr.sbin/vmd
.PATH:			${.CURDIR}/../../../../${VMD_DIR}

VMCTL?=			/usr/sbin/vmctl

PROG=			vioscribble
SRCS=			vioscribble.c vioqcow2.c vioraw.c log.c
CFLAGS+=		-I${BSDSRCDIR}/${VMD_DIR}
LDADD+=			-lpthread

CLEANFILES=		scribble.{raw,qcow2}
REGRESS_CLEANUP=	cleanup
cleanup:
	rm -f ${CLEANFILES}

REGRESS_SETUP=		setup
setup: cleanup
	${VMCTL} create -s 4G scribble.raw
	${VMCTL} create -s 4G scribble.qcow2

.include <bsd.regress.mk>
