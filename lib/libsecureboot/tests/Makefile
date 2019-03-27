# $FreeBSD$

.include <src.opts.mk>

PROG= tvo

SRCS+= tvo.c
CFLAGS+= -DUNIT_TEST -g -O0

LIBADD+= bearssl
MAN=
NO_SHARED=

# we want to test verify_file api too
# which requires a kludge or two
.include "../Makefile.libsa.inc"
BRSSL_CFLAGS := ${BRSSL_CFLAGS:N-DNO_STDIO}
XCFLAGS.verify_file += -DSOPEN_MAX=64

.include <bsd.prog.mk>
