# $FreeBSD$

LIB=	gssapi_ntlm
LDFLAGS= -Wl,-Bsymbolic -Wl,--no-undefined
LIBADD=	crypto gssapi krb5 heimntlm roken
SHLIB_MAJOR=	10

SRCS=	accept_sec_context.c \
	acquire_cred.c \
	add_cred.c \
	canonicalize_name.c \
	compare_name.c \
	context_time.c \
	creds.c \
	crypto.c \
	delete_sec_context.c \
	display_name.c \
	display_status.c \
	duplicate_name.c \
	export_name.c \
	export_sec_context.c \
	external.c \
	import_name.c \
	import_sec_context.c \
	indicate_mechs.c \
	init_sec_context.c \
	inquire_context.c \
	inquire_cred_by_mech.c \
	inquire_mechs_for_name.c \
	inquire_names_for_mech.c \
	inquire_sec_context_by_oid.c \
	iter_cred.c \
	kdc.c \
	prefix.c \
	process_context_token.c \
	release_cred.c \
	release_name.c \
	gss_oid.c

CFLAGS+=-I${KRB5DIR}/lib/gssapi
CFLAGS+=-I${KRB5DIR}/lib/gssapi/gssapi
CFLAGS+=-I${KRB5DIR}/lib/gssapi/ntlm
CFLAGS+=-I${KRB5DIR}/lib/krb5
CFLAGS+=-I${KRB5DIR}/lib/ntlm

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/gssapi/ntlm ${SRCTOP}/lib/libgssapi ${.CURDIR:H}/libgssapi_krb5
