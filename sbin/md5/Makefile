#	@(#)Makefile	8.1 (Berkeley) 6/9/93
# $FreeBSD$

PACKAGE=runtime
PROG=	md5

LINKS=	${BINDIR}/md5 ${BINDIR}/rmd160 \
	${BINDIR}/md5 ${BINDIR}/sha1 \
	${BINDIR}/md5 ${BINDIR}/sha224 \
	${BINDIR}/md5 ${BINDIR}/sha256 \
	${BINDIR}/md5 ${BINDIR}/sha384 \
	${BINDIR}/md5 ${BINDIR}/sha512 \
	${BINDIR}/md5 ${BINDIR}/sha512t256 \
	${BINDIR}/md5 ${BINDIR}/skein256 \
	${BINDIR}/md5 ${BINDIR}/skein512 \
	${BINDIR}/md5 ${BINDIR}/skein1024

MLINKS=	md5.1 rmd160.1 \
	md5.1 sha1.1 \
	md5.1 sha224.1 \
	md5.1 sha256.1 \
	md5.1 sha384.1 \
	md5.1 sha512.1 \
	md5.1 sha512t256.1 \
	md5.1 skein256.1 \
	md5.1 skein512.1 \
	md5.1 skein1024.1

LIBADD=	md

.ifndef(BOOTSTRAPPING)
# Avoid depending on capsicum during bootstrap. caph_limit_stdout() is not
# available when building for Linux/MacOS or older FreeBSD hosts.
# We need to bootstrap md5 when building on Linux since the md5sum command there
# produces different output.
CFLAGS+=-DHAVE_CAPSICUM
.endif

.include <bsd.prog.mk>
