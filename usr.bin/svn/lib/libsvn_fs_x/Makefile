# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${SVNDIR}/libsvn_fs_x

INTERNALLIB=	yes
LIB=		svn_fs_x

SRCS=		batch_fsync.c cached_data.c caching.c changes.c \
		dag_cache.c dag.c fs.c fs_id.c fs_x.c \
		hotcopy.c id.c index.c lock.c low_level.c noderevs.c pack.c \
		recovery.c rep-cache.c reps.c rev_file.c revprops.c \
		string_table.c temp_serializer.c transaction.c tree.c \
		util.c verify.c

CFLAGS+=	-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR}/../.. \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix \
		-I${APR}/include \
		-I${.CURDIR}/../libapr_util \
		-I${APRU}/include/private \
		-I${APRU}/include

.include <bsd.lib.mk>
