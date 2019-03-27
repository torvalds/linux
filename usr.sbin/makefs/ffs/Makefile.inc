#	$FreeBSD$
#

.PATH:	${SRCDIR}/ffs ${SRCTOP}/sys/ufs/ffs
SRCS+=	ffs_alloc.c ffs_balloc.c ffs_bswap.c ffs_subr.c ufs_bmap.c
SRCS+=	buf.c mkfs.c

# Reach-over source from sys/ufs/ffs
SRCS+=	ffs_tables.c
