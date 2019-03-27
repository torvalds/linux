# $FreeBSD$

LIB=	hdb
LDFLAGS=	-Wl,--no-undefined ${LDAPLDFLAGS}
VERSION_MAP=	${KRB5DIR}/lib/hdb/version-script.map
LIBADD=	asn1 com_err krb5 roken sqlite3
LDADD=	${LDAPLDADD}
DPADD=	${LDAPDPADD}

MAN=	HDB.3 hdb_entry_ex.3

MLINKS=	HDB.3 hdb__del.3 \
	HDB.3 hdb__get.3 \
	HDB.3 hdb__put.3 \
	HDB.3 hdb_auth_status.3 \
	HDB.3 hdb_check_constrained_delegation.3 \
	HDB.3 hdb_check_pkinit_ms_upn_match.3 \
	HDB.3 hdb_check_s4u2self.3 \
	HDB.3 hdb_close.3 \
	HDB.3 hdb_destroy.3 \
	HDB.3 hdb_fetch_kvno.3 \
	HDB.3 hdb_firstkey.3 \
	HDB.3 hdb_free.3 \
	HDB.3 hdb_get_realms.3 \
	HDB.3 hdb_lock.3 \
	HDB.3 hdb_name.3 \
	HDB.3 hdb_nextkey.3 \
	HDB.3 hdb_open.3 \
	HDB.3 hdb_password.3 \
	HDB.3 hdb_remove.3 \
	HDB.3 hdb_rename.3 \
	HDB.3 hdb_store.3 \
	HDB.3 hdb_unlock.3

INCS=	hdb-protos.h \
	hdb.h \
	hdb_asn1.h \
	hdb_err.h

SRCS=	common.c \
	db.c \
	db3.c \
	dbinfo.c \
	ext.c \
	hdb-ldap.c \
	hdb.c \
	hdb_err.c \
	hdb_err.h \
	hdb-sqlite.c \
	hdb-keytab.c \
	hdb-mitdb.c \
	keys.c \
	keytab.c \
	mkey.c \
	ndbm.c \
	print.c \
	${GEN:S/.x$/.c/:S/.hx$/.h/}

CFLAGS+=-I${KRB5DIR}/lib/hdb -I${KRB5DIR}/lib/asn1 \
	-I${KRB5DIR}/lib/roken -I${SRCTOP}/contrib/sqlite3/ \
	-I${KRB5DIR}/lib/krb5 \
	-I${SRCTOP}/contrib/com_err \
	-I. ${LDAPCFLAGS}
CFLAGS+=-DHDB_DB_DIR="\"/var/heimdal\""

GEN=	asn1_Salt.x \
	asn1_Key.x \
	asn1_Event.x \
	asn1_HDBFlags.x \
	asn1_GENERATION.x \
	asn1_HDB_Ext_PKINIT_acl.x \
	asn1_HDB_Ext_PKINIT_cert.x \
	asn1_HDB_Ext_PKINIT_hash.x \
	asn1_HDB_Ext_Constrained_delegation_acl.x \
	asn1_HDB_Ext_Lan_Manager_OWF.x \
	asn1_HDB_Ext_Password.x \
	asn1_HDB_Ext_Aliases.x \
	asn1_HDB_extension.x \
	asn1_HDB_extensions.x \
	asn1_hdb_entry.x \
	asn1_hdb_entry_alias.x \
	asn1_hdb_keyset.x \
	hdb_asn1.hx \
	hdb_asn1-priv.hx

CLEANFILES=	${GEN} ${GEN:S/.x$/.c/:S/.hx$/.h/} hdb_asn1_files \
		hdb_asn1-template.[ch]*

.ORDER: ${GEN}
${GEN:[2..-1]}: .NOMETA
${GEN}: hdb.asn1
	${ASN1_COMPILE} ${.ALLSRC:M*.asn1} hdb_asn1

.SUFFIXES: .h .c .x .hx

.x.c:   
	${CP} ${.IMPSRC} ${.TARGET}

.hx.h:
	${CP} ${.IMPSRC} ${.TARGET}

.include <bsd.lib.mk>

.SUFFIXES: .h .c .x .hx

.PATH: ${KRB5DIR}/lib/hdb ${KRB5DIR}/doc/doxyout/hdb/man/man3/
