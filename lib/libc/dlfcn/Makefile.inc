#	$OpenBSD: Makefile.inc,v 1.7 2024/01/19 17:02:28 deraadt Exp $

.PATH: ${LIBCSRCDIR}/dlfcn

.include <bsd.own.mk>

SRCS+=		dlfcn_stubs.c init.c tib.c
