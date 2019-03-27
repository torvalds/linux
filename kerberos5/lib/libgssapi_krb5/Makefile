# $FreeBSD$

LIB=	gssapi_krb5
LDFLAGS= -Wl,-Bsymbolic -Wl,--no-undefined
LIBADD=	gssapi krb5 crypto roken asn1 com_err
SHLIB_MAJOR=	10

INCS=	${KRB5DIR}/lib/gssapi/gssapi/gssapi_krb5.h
INCSDIR= ${INCLUDEDIR}/gssapi

SRCS=	8003.c \
	accept_sec_context.c \
	acquire_cred.c \
	add_cred.c \
	address_to_krb5addr.c \
	aeap.c \
	arcfour.c \
	authorize_localname.c \
	canonicalize_name.c \
	ccache_name.c \
	cfx.c \
	compare_name.c \
	compat.c \
	context_time.c \
	copy_ccache.c \
	creds.c \
	decapsulate.c \
	delete_sec_context.c \
	display_name.c \
	display_status.c \
	duplicate_name.c \
	encapsulate.c \
	export_name.c \
	export_sec_context.c \
	external.c \
	get_mic.c \
	gkrb5_err.c \
	gkrb5_err.h \
	import_name.c \
	import_sec_context.c \
	indicate_mechs.c \
	init.c \
	init_sec_context.c \
	inquire_context.c \
	inquire_cred.c \
	inquire_cred_by_mech.c \
	inquire_cred_by_oid.c \
	inquire_mechs_for_name.c \
	inquire_names_for_mech.c \
	inquire_sec_context_by_oid.c \
	pname_to_uid.c \
	prefix.c \
	prf.c \
	process_context_token.c \
	release_buffer.c \
	release_cred.c \
	release_name.c \
	sequence.c \
	set_cred_option.c \
	set_sec_context_option.c \
	store_cred.c \
	ticket_flags.c \
	unwrap.c \
	verify_mic.c \
	wrap.c \
	gss_krb5.c \
	gss_oid.c

#SRCS+=	gss_add_oid_set_member.c \
#	gss_create_empty_oid_set.c \
#	gss_release_buffer.c \
#	gss_release_oid_set.c \
#	gss_test_oid_set_member.c \
#	gss_utils.c

CFLAGS+=-I${KRB5DIR}/lib/gssapi
CFLAGS+=-I${KRB5DIR}/lib/gssapi/krb5
CFLAGS+=-I${KRB5DIR}/lib/gssapi/gssapi
CFLAGS+=-I${KRB5DIR}/lib/krb5
CFLAGS+=-I${KRB5DIR}/lib/asn1
CFLAGS+=-I${KRB5DIR}/lib/roken
CFLAGS+=-I${SRCTOP}/contrib/com_err
CFLAGS+=-I.

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/gssapi/krb5 ${SRCTOP}/lib/libgssapi
