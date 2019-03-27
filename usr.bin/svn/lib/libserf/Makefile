# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

INTERNALLIB=	yes
LIB=		serf

SRCS=		aggregate_buckets.c allocator.c auth.c auth_basic.c \
		auth_digest.c auth_spnego.c auth_spnego_gss.c barrier_buckets.c \
		buckets.c bwtp_buckets.c chunk_buckets.c context.c \
		dechunk_buckets.c deflate_buckets.c file_buckets.c \
		headers_buckets.c incoming.c iovec_buckets.c limit_buckets.c \
		mmap_buckets.c outgoing.c request_buckets.c \
		response_body_buckets.c response_buckets.c simple_buckets.c \
		socket_buckets.c ssl_buckets.c ssltunnel.c

.PATH:		${SERF}/buckets ${SERF}/auth ${SERF}

CFLAGS+= 	-I${.CURDIR} -I${SERF} \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix -I${APR}/include \
		-I${.CURDIR}/../libapr_util \
		-I${APRU}/include

.include <bsd.lib.mk>
