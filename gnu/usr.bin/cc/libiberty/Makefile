# $FreeBSD$

.include <bsd.own.mk>

#
# Make sure we will pick up our config.h file first, not the one from
# cc_tools.
#
CFLAGS+=	-I${.CURDIR}

.include "../Makefile.inc"

.PATH: ${GCCLIB}/libiberty

LIB=	iberty
SRCS=	argv.c choose-temp.c concat.c cp-demangle.c cp-demint.c cplus-dem.c \
	dyn-string.c fibheap.c fopen_unlocked.c getpwd.c getruntime.c \
	hashtab.c hex.c lbasename.c make-temp-file.c md5.c obstack.c \
	partition.c pex-unix.c physmem.c safe-ctype.c splay-tree.c xexit.c \
	xmalloc.c xmemdup.c xstrdup.c xstrerror.c make-relative-prefix.c \
	pexecute.c pex-common.c pex-one.c lrealpath.c strverscmp.c \
	regex.c
INTERNALLIB=
WARNS?=	1

CFLAGS+=	-UPREFIX

.include <bsd.lib.mk>
