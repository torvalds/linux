# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

INTERNALLIB=	yes
LIB=		apr-util

SRCS=		apr_base64.c apr_brigade.c apr_buckets.c apr_buckets_alloc.c \
		apr_buckets_eos.c apr_buckets_file.c apr_buckets_flush.c \
		apr_buckets_heap.c apr_buckets_mmap.c apr_buckets_pipe.c \
		apr_buckets_pool.c apr_buckets_refcount.c apr_buckets_simple.c \
		apr_buckets_socket.c apr_crypto.c apr_date.c apr_dbd.c \
		apr_hooks.c apr_md4.c apr_md5.c apr_memcache.c apr_passwd.c \
		apr_queue.c apr_reslist.c apr_rmm.c apr_sha1.c apr_strmatch.c \
		apr_thread_pool.c apr_uri.c apr_xml.c apu_dso.c apu_version.c \
		crypt_blowfish.c getuuid.c uuid.c xlate.c

.PATH:		${APRU}/buckets ${APRU}/crypto ${APRU}/dbd \
		${APRU}/encoding ${APRU}/hooks \
		${APRU}/memcache ${APRU}/misc ${APRU}/strmatch \
		${APRU}/uri ${APRU}/xlate ${APRU}/xml ${APRU}/include

CFLAGS+= 	-DHAVE_CONFIG_H \
		-I${.CURDIR} \
		-I${APRU}/include/private \
		-I${APRU}/include \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix \
		-I${APR}/include

.include <src.opts.mk>

.if ${MK_ICONV} == "yes"
CFLAGS+=	-DHAVE_ICONV_H=1 -DAPU_HAVE_ICONV=1
.endif

.include <bsd.lib.mk>

