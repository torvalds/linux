#       from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD$

.PATH: ${LIBC_SRCTOP}/db/hash

SRCS+=	hash.c hash_bigkey.c hash_buf.c hash_func.c hash_log2.c \
	hash_page.c ndbm.c
