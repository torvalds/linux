# $FreeBSD$

_spath=	${SRCTOP}/contrib/ofed/infiniband-diags
.PATH: ${_spath}/src ${_spath}/man

BINDIR?= /usr/bin
SRCS+= ibdiag_common.c ibdiag_sa.c
CFLAGS+= -I${SYSROOT:U${DESTDIR}}/${INCLUDEDIR}/infiniband
CFLAGS+= -DHAVE_CONFIG_H=1
CFLAGS+= -I${_spath} -I${_spath}/src
LIBADD+= osmcomp ibmad ibumad

