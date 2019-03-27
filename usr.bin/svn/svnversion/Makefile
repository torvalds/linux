# $FreeBSD$

MAN=

.include "${.CURDIR:H}/Makefile.inc"

.PATH:	${SVNDIR}/svnversion

PROG=	svn${SVNLITE}version

SRCS=	svnversion.c

CFLAGS+=-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR:H} \
	-I${.CURDIR:H}/lib/libapr \
	-I${APR}/include/arch/unix \
	-I${APR}/include \
	-I${.CURDIR:H}/lib/libapr_util \
	-I${APRU}/include/private \
	-I${APRU}/include

LDADD=	-L${LIBSVN_WCDIR} -lsvn_wc${PIE_SUFFIX} \
	-L${LIBSVN_DELTADIR} -lsvn_delta${PIE_SUFFIX} \
	-L${LIBSVN_DIFFDIR} -lsvn_diff${PIE_SUFFIX} \
	-L${LIBSVN_SUBRDIR} -lsvn_subr${PIE_SUFFIX} \
	-L${LIBAPR_UTILDIR} -lapr-util${PIE_SUFFIX} \
	-L${LIBAPRDIR} -lapr${PIE_SUFFIX}
LIBADD+=	bsdxml sqlite3 z pthread

DPADD=	${LIBSVN_WC} ${LIBSVN_DELTA} ${LIBSVN_DIFF} ${LIBSVN_SUBR} \
	${LIBSERF} ${LIBAPR_UTIL} ${LIBAPR}

.include <bsd.prog.mk>
