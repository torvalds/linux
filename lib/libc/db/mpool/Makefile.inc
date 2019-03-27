#	from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD$

.PATH: ${LIBC_SRCTOP}/db/mpool

SRCS+=	mpool.c
.if ${MK_SYMVER} == yes
SRCS+=	mpool-compat.c
.endif
