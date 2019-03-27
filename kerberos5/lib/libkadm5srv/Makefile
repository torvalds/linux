# $FreeBSD$

LIB=	kadm5srv
LDFLAGS=	-Wl,--no-undefined
LIBADD=	com_err hdb krb5 roken
VERSION_MAP= ${KRB5DIR}/lib/kadm5/version-script.map
MAN=	kadm5_pwcheck.3

SRCS=	acl.c \
	bump_pw_expire.c \
	chpass_s.c \
	common_glue.c \
	context_s.c \
	create_s.c \
	delete_s.c \
	destroy_s.c \
	ent_setup.c \
	error.c \
	flush_s.c \
	free.c \
	get_princs_s.c \
	get_s.c \
	init_s.c \
	kadm5_err.c \
	kadm5_err.h \
	keys.c \
	log.c \
	marshall.c \
	modify_s.c \
	password_quality.c \
	privs_s.c \
	randkey_s.c \
	rename_s.c \
	server_glue.c \
	set_keys.c \
	set_modifier.c

CFLAGS+=-I${KRB5DIR}/lib/kadm5 -I${KRB5DIR}/lib/asn1 -I${KRB5DIR}/lib/roken \
	-I${SRCTOP}/contrib/com_err -I.

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/kadm5
