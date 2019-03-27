# $FreeBSD$

PACKAGE=lib${LIB}
WARNS?=		3
LIB=		memstat
SHLIB_MAJOR=	3
LIBADD+=	kvm
SRCS+=		memstat.c
SRCS+=		memstat_all.c
SRCS+=		memstat_malloc.c
SRCS+=		memstat_uma.c
INCS=		memstat.h

MAN=		libmemstat.3

MLINKS+=	libmemstat.3 memstat_mtl_alloc.3
MLINKS+=	libmemstat.3 memstat_mtl_first.3
MLINKS+=	libmemstat.3 memstat_mtl_next.3
MLINKS+=	libmemstat.3 memstat_mtl_find.3
MLINKS+=	libmemstat.3 memstat_mtl_free.3
MLINKS+=	libmemstat.3 memstat_mtl_geterror.3
MLINKS+=	libmemstat.3 memstat_strerror.3
MLINKS+=	libmemstat.3 memstat_sysctl_all.3
MLINKS+=	libmemstat.3 memstat_sysctl_malloc.3
MLINKS+=	libmemstat.3 memstat_sysctl_uma.3
MLINKS+=	libmemstat.3 memstat_kvm_all.3
MLINKS+=	libmemstat.3 memstat_kvm_malloc.3
MLINKS+=	libmemstat.3 memstat_kvm_uma.3

.include <bsd.lib.mk>
