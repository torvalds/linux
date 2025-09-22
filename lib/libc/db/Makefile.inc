#	$OpenBSD: Makefile.inc,v 1.8 2016/05/07 23:35:44 naddy Exp $
#
.PATH: ${LIBCSRCDIR}/db/btree

SRCS+=	bt_close.c bt_conv.c bt_debug.c bt_delete.c bt_get.c bt_open.c \
	bt_overflow.c bt_page.c bt_put.c bt_search.c bt_seq.c bt_split.c \
	bt_utils.c

.PATH: ${LIBCSRCDIR}/db/db

SRCS+=	db.c

.PATH: ${LIBCSRCDIR}/db/hash

SRCS+=	hash.c hash_bigkey.c hash_buf.c hash_func.c hash_log2.c \
	hash_page.c ndbm.c

.PATH: ${LIBCSRCDIR}/db/man

MAN+=	btree.3 dbopen.3 hash.3 ndbm.3 recno.3

.PATH: ${LIBCSRCDIR}/db/mpool

SRCS+=	mpool.c

.PATH: ${LIBCSRCDIR}/db/recno

SRCS+=	rec_close.c rec_delete.c rec_get.c rec_open.c rec_put.c rec_search.c \
	rec_seq.c rec_utils.c
