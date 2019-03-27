# $FreeBSD$

.PATH: ${SRCTOP}/sys/kgssapi ${SRCTOP}/sys/rpc/rpcsec_gss
KMOD=	kgssapi

SRCS=	gss_accept_sec_context.c \
	gss_add_oid_set_member.c \
	gss_acquire_cred.c \
	gss_canonicalize_name.c \
	gss_create_empty_oid_set.c \
	gss_delete_sec_context.c \
	gss_display_status.c \
	gss_export_name.c \
	gss_get_mic.c \
	gss_init_sec_context.c \
	gss_impl.c \
	gss_import_name.c \
	gss_names.c \
	gss_pname_to_uid.c \
	gss_release_buffer.c \
	gss_release_cred.c \
	gss_release_name.c \
	gss_release_oid_set.c \
	gss_set_cred_option.c \
	gss_test_oid_set_member.c \
	gss_unwrap.c \
	gss_verify_mic.c \
	gss_wrap.c \
	gss_wrap_size_limit.c \
	gssd_prot.c

SRCS+=	rpcsec_gss.c \
	rpcsec_gss_conf.c \
	rpcsec_gss_misc.c \
	rpcsec_gss_prot.c \
	svc_rpcsec_gss.c

SRCS+=		kgss_if.h kgss_if.c

SRCS+=		gssd.h gssd_xdr.c gssd_clnt.c
CLEANFILES=	gssd.h gssd_xdr.c gssd_clnt.c

S=		${SRCTOP}/sys

gssd.h:		$S/kgssapi/gssd.x
	RPCGEN_CPP=${CPP:Q} rpcgen -hM $S/kgssapi/gssd.x | grep -v pthread.h > gssd.h

gssd_xdr.c:	$S/kgssapi/gssd.x
	RPCGEN_CPP=${CPP:Q} rpcgen -c $S/kgssapi/gssd.x -o gssd_xdr.c

gssd_clnt.c:	$S/kgssapi/gssd.x
	RPCGEN_CPP=${CPP:Q} rpcgen -lM $S/kgssapi/gssd.x | grep -v string.h > gssd_clnt.c

.include <bsd.kmod.mk>
