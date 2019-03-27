# $FreeBSD$

PROG=	localedef
SRCS=	charmap.c \
	collate.c \
	ctype.c \
	localedef.c \
	messages.c \
	monetary.c \
	numeric.c \
	parser.y \
	scanner.c \
	time.c \
	wide.c

WARNS=	3
${SRCS:M*.c}: parser.h
parser.h: parser.y

IGNORE_PRAGMA=	yes

CFLAGS+=	-I. -I${.CURDIR}
CFLAGS+=	-I${SRCTOP}/lib/libc/locale
CFLAGS+=	-I${SRCTOP}/lib/libc/stdtime

.include <bsd.prog.mk>
