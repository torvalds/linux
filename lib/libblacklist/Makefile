# $FreeBSD$

BLACKLIST_DIR=${SRCTOP}/contrib/blacklist

.PATH: ${BLACKLIST_DIR}/lib ${BLACKLIST_DIR}/include

LIB=	blacklist
SHLIB_MAJOR= 0

LIBADD+= pthread

CFLAGS.clang+=-Wno-thread-safety-analysis

CFLAGS+=-I${BLACKLIST_DIR}/include -I${BLACKLIST_DIR}/port \
	-D_REENTRANT -DHAVE_CONFIG_H -DHAVE_DB_H -DHAVE_LIBUTIL_H \
	-DHAVE_CLOCK_GETTIME -DHAVE_FGETLN -DHAVE_GETPROGNAME \
	-DHAVE_STRLCAT -DHAVE_STRLCPY -DHAVE_STRUCT_SOCKADDR_SA_LEN

SRCS=	bl.c blacklist.c
INCS=	blacklist.h
MAN=	libblacklist.3

MLINKS=	libblacklist.3 blacklist_open.3		\
	libblacklist.3 blacklist_close.3	\
	libblacklist.3 blacklist.3		\
	libblacklist.3 blacklist_r.3		\
	libblacklist.3 blacklist_sa.3		\
	libblacklist.3 blacklist_sa_r.3

.include <bsd.lib.mk>
