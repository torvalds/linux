# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	rpcsec_gss
SHLIB_MAJOR= 1
SRCS+=	rpcsec_gss.c rpcsec_gss_prot.c rpcsec_gss_conf.c rpcsec_gss_misc.c \
	svc_rpcsec_gss.c

LIBADD=	gssapi

VERSION_DEF=	${SRCTOP}/lib/libc/Versions.def
SYMBOL_MAPS=	${.CURDIR}/Symbol.map

CFLAGS+= -I${SRCTOP}/include
CFLAGS+= -I${SRCTOP}/lib/libc_rpc
MK_PROFILE=	no

MAN=	rpcsec_gss.3
MAN+=	rpc_gss_seccreate.3
MAN+=	rpc_gss_set_defaults.3
MAN+=	rpc_gss_max_data_length.3
MAN+=	rpc_gss_get_error.3

MAN+=	rpc_gss_mech_to_oid.3
MAN+=	rpc_gss_oid_to_mech.3
MAN+=	rpc_gss_qop_to_num.3
MAN+=	rpc_gss_get_mechanisms.3
MAN+=	rpc_gss_get_mech_info.3
MAN+=	rpc_gss_get_versions.3
MAN+=	rpc_gss_is_installed.3

MAN+=	rpc_gss_set_svc_name.3
MAN+=	rpc_gss_getcred.3
MAN+=	rpc_gss_set_callback.3
MAN+=	rpc_gss_get_principal_name.3
MAN+=	rpc_gss_svc_max_data_length.3

.include <bsd.lib.mk>
