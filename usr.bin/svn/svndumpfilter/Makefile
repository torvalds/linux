# $FreeBSD$

MAN=

.include "${.CURDIR:H}/Makefile.inc"

.PATH:	${SVNDIR}/svndumpfilter

PROG=	svn${SVNLITE}dumpfilter

SRCS=	svndumpfilter.c

CFLAGS+=-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR:H} \
	-I${.CURDIR:H}/lib/libapr \
	-I${APR}/include/arch/unix \
	-I${APR}/include \
	-I${.CURDIR:H}/lib/libapr_util \
	-I${APRU}/include/private \
	-I${APRU}/include

LDADD=	-L${LIBSVN_REPOSDIR} -lsvn_repos${PIE_SUFFIX} \
	-L${LIBSVN_FSDIR} -lsvn_fs${PIE_SUFFIX} \
	-L${LIBSVN_FS_FSDIR} -lsvn_fs_fs${PIE_SUFFIX} \
	-L${LIBSVN_FS_XDIR} -lsvn_fs_x${PIE_SUFFIX} \
	-L${LIBSVN_FS_UTILDIR} -lsvn_fs_util${PIE_SUFFIX} \
	-L${LIBSVN_DELTADIR} -lsvn_delta${PIE_SUFFIX} \
	-L${LIBSVN_SUBRDIR} -lsvn_subr${PIE_SUFFIX} \
	-L${LIBAPR_UTILDIR} -lapr-util${PIE_SUFFIX} \
	-L${LIBAPRDIR} -lapr${PIE_SUFFIX}
LIBADD+=	bsdxml sqlite3 z crypt pthread

DPADD=	${LIBSVN_REPOS} ${LIBSVN_FS} ${LIBSVN_FS_FS} ${LIBSVN_FS_UTIL} \
	${LIBSVN_DELTA} ${LIBSVN_SUBR} ${LIBAPR_UTIL} \
	${LIBAPR}

.include <bsd.prog.mk>
