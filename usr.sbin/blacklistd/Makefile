# $FreeBSD$

BLACKLIST_DIR=${SRCTOP}/contrib/blacklist
.PATH: ${BLACKLIST_DIR}/bin ${BLACKLIST_DIR}/port

CONFS=	blacklistd.conf
PROG=	blacklistd
SRCS=	blacklistd.c conf.c run.c state.c support.c internal.c \
	sockaddr_snprintf.c pidfile.c strtoi.c popenve.c
MAN=	blacklistd.8 blacklistd.conf.5

LDFLAGS+=-L${LIBBLACKLISTDIR}
LIBADD+= blacklist util

CFLAGS+=-I${BLACKLIST_DIR}/include -I${BLACKLIST_DIR}/port \
	-D_PATH_BLCONTROL=\"/usr/libexec/blacklistd-helper\" \
	-DHAVE_CONFIG_H -DHAVE_DB_H -DHAVE_LIBUTIL_H \
	-DHAVE_CLOCK_GETTIME -DHAVE_FGETLN -DHAVE_FPARSELN \
	-DHAVE_GETPROGNAME -DHAVE_STRLCAT -DHAVE_STRLCPY \
	-DHAVE_STRUCT_SOCKADDR_SA_LEN
# CFLAGS+= -D_REENTRANT

.include <bsd.prog.mk>
