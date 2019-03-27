# $FreeBSD$

.include "../Makefile.inc0"

.PATH: ${SRCDIR}/libiberty

LIB=	iberty
SRCS=	argv.c \
	concat.c \
	choose-temp.c \
	cp-demangle.c \
	cp-demint.c \
	cplus-dem.c \
	dyn-string.c \
	getpwd.c \
	getruntime.c \
	filename_cmp.c \
	floatformat.c \
	hashtab.c \
	hex.c \
	lbasename.c \
	lrealpath.c \
	make-relative-prefix.c \
	make-temp-file.c \
	objalloc.c \
	obstack.c \
	safe-ctype.c \
	unlink-if-ordinary.c \
	xatexit.c \
	xexit.c \
	xmalloc.c \
	xstrdup.c \
	xstrerror.c

# The following files are needed by gdb(1)
SRCS+=	splay-tree.c

WARNS?=	2
CFLAGS+=	-DHAVE_CONFIG_H
INTERNALLIB=

.include <bsd.lib.mk>
