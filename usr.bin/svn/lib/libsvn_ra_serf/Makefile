# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${SVNDIR}/libsvn_ra_serf

INTERNALLIB=	yes
LIB=		svn_ra_serf

SRCS=		blame.c blncache.c commit.c eagain_bucket.c \
		get_deleted_rev.c get_file.c get_lock.c getdate.c \
		getlocations.c getlocationsegments.c getlocks.c \
		inherited_props.c list.c lock.c log.c merge.c mergeinfo.c \
		multistatus.c options.c property.c replay.c \
		request_body.c sb_bucket.c serf.c stat.c stream_bucket.c \
		update.c util.c util_error.c xml.c

CFLAGS+=	-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR}/../.. \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix \
		-I${APR}/include \
		-I${.CURDIR}/../libapr_util \
		-I${APRU}/include/private \
		-I${APRU}/include \
		-I${SERF}

.include <bsd.lib.mk>
