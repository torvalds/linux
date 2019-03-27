# $FreeBSD$

# allow building mkimg as a host tool.

MKIMG_SRC = ${SRCTOP}/usr.bin/mkimg
.PATH: ${MKIMG_SRC}

.if ${.MAKE.OS} == "Linux"
WARNS= 0

CFLAGS+= \
	-D"__FBSDID(x)=" \
	-D_XOPEN_SOURCE -D_GNU_SOURCE

.PATH: ${SRCTOP}/lib/libutil
SRCS+= expand_number.c

.endif

MK_STAGING=no
MK_TESTS= no

.include <${MKIMG_SRC}/Makefile>

.if ${.MAKE.OS} == "Linux"
LDADD+= -lbsd
.endif
