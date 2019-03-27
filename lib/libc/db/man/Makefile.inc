#       from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD$

.PATH: ${LIBC_SRCTOP}/db/man

MAN+=	btree.3 dbm.3 dbopen.3 hash.3 mpool.3 recno.3

MLINKS+= dbm.3 dbm_clearerr.3
MLINKS+= dbm.3 dbm_close.3
MLINKS+= dbm.3 dbm_delete.3
MLINKS+= dbm.3 dbm_dirnfo.3
MLINKS+= dbm.3 dbm_error.3
MLINKS+= dbm.3 dbm_fetch.3
MLINKS+= dbm.3 dbm_firstkey.3
MLINKS+= dbm.3 dbm_nextkey.3
MLINKS+= dbm.3 dbm_open.3
MLINKS+= dbm.3 dbm_store.3
MLINKS+= dbopen.3 db.3
