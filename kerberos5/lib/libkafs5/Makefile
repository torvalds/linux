# $FreeBSD$

LIB=	kafs5
LDFLAGS=	-Wl,--no-undefined
LIBADD=	asn1 krb5 roken
INCS=	kafs.h
MAN=	kafs5.3
VERSION_MAP=	${.CURDIR}/version.map

MLINKS=	kafs5.3 k_afs_cell_of_file.3 \
	kafs5.3 k_hasafs.3 \
	kafs5.3 k_pioctl.3 \
	kafs5.3 k_setpag.3 \
	kafs5.3 k_unlog.3 \
	kafs5.3 kafs.3 \
	kafs5.3 kafs_set_verbose.3 \
	kafs5.3 kafs_settoken.3 \
	kafs5.3 kafs_settoken5.3 \
	kafs5.3 kafs_settoken_rxkad.3 \
	kafs5.3 krb5_afslog.3 \
	kafs5.3 krb5_afslog_uid.3 \
	kafs5.3 krb_afslog.3 \
	kafs5.3 krb_afslog_uid.3

SRCS=	afssys.c afskrb5.c common.c

CFLAGS+=	-I${KRB5DIR}/lib/kafs \
		-I${KRB5DIR}/lib/krb5 \
		-I${.OBJDIR:H}/libkrb5 \
		-I${KRB5DIR}/lib/roken

CLEANFILES= kafs5.3

kafs5.3: kafs.3
	sed -e 's/libkafs, -lkafs/libkafs5, -lkafs5/g' ${.ALLSRC} >${.TARGET}

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/kafs
