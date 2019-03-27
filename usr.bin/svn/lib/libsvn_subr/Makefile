# $FreeBSD$

.include "${.CURDIR}/../Makefile.inc"

.PATH:	${SVNDIR}/libsvn_subr ${SVNDIR}/libsvn_subr/lz4

INTERNALLIB=	yes
LIB=		svn_subr

SRCS=		adler32.c atomic.c auth.c base64.c bit_array.c \
		cache-inprocess.c cache-membuffer.c cache-memcache.c \
		cache.c cache-null.c cache_config.c checksum.c cmdline.c \
		compat.c compress_lz4.c compress_zlib.c \
		config.c config_auth.c config_file.c \
		config_win.c crypto.c ctype.c date.c debug.c \
		deprecated.c dirent_uri.c dso.c \
		encode.c eol.c error.c fnv1a.c \
		gpg_agent.c hash.c io.c iter.c lock.c log.c lz4.c \
		macos_keychain.c magic.c md5.c mergeinfo.c mutex.c nls.c \
		object_pool.c opt.c packed_data.c path.c \
		pool.c prefix_string.c prompt.c properties.c quoprint.c \
		root_pools.c simple_providers.c skel.c sorts.c spillbuf.c \
		sqlite.c sqlite3wrapper.c ssl_client_cert_providers.c \
		ssl_client_cert_pw_providers.c ssl_server_trust_providers.c \
		stream.c string.c subst.c sysinfo.c target.c \
		temp_serializer.c time.c token.c types.c user.c \
		username_providers.c utf.c utf8proc.c \
		utf_validate.c utf_width.c validate.c version.c \
		win32_crashrpt.c win32_crypto.c win32_xlate.c \
		x509info.c x509parse.c xml.c

CFLAGS+=	-I${SVNDIR}/include -I${SVNDIR} -I${.CURDIR}/../.. \
		-I${.CURDIR}/../libapr \
		-I${APR}/include/arch/unix \
		-I${APR}/include \
		-I${.CURDIR}/../libapr_util \
		-I${APRU}/include/private \
		-I${APRU}/include \
		-I${SQLITE}

.include <bsd.lib.mk>

