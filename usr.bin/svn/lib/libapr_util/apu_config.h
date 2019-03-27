/* $FreeBSD$ */

/* include/private/apu_config.h.  Generated from apu_config.h.in by configure.  */
/* include/private/apu_config.h.in.  Generated from configure.in by autoheader.  */

/* Define if the system crypt() function is threadsafe */
/* #undef APU_CRYPT_THREADSAFE */

/* Define to 1 if modular components are built as DSOs */
/* #undef APU_DSO_BUILD */

/* Define to be absolute path to DSO directory */
/* #undef APU_DSO_LIBDIR */

/* Define if the inbuf parm to iconv() is const char ** */
/* #undef APU_ICONV_INBUF_CONST */

/* Define that OpenSSL uses const buffers */
#define CRYPTO_OPENSSL_CONST_BUFFERS 1

/* Define if crypt_r has uses CRYPTD */
/* #undef CRYPT_R_CRYPTD */

/* Define if crypt_r uses struct crypt_data */
/* #undef CRYPT_R_STRUCT_CRYPT_DATA */

/* Define if CODESET is defined in langinfo.h */
#define HAVE_CODESET 1

/* Define to 1 if you have the `crypt_r' function. */
/* #undef HAVE_CRYPT_R */

/* Define to 1 if you have the declaration of `EVP_PKEY_CTX_new', and to 0 if
   you don't. */
#define HAVE_DECL_EVP_PKEY_CTX_NEW 1

/* Define if expat.h is available */
#define HAVE_EXPAT_H 1

/* Define to 1 if you have the <freetds/sybdb.h> header file. */
/* #undef HAVE_FREETDS_SYBDB_H */

/* Define to 1 if you have the <iconv.h> header file. */
/* #undef HAVE_ICONV_H */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <langinfo.h> header file. */
#define HAVE_LANGINFO_H 1

/* Define to 1 if you have the <lber.h> header file. */
/* #undef HAVE_LBER_H */

/* Defined if ldap.h is present */
/* #undef HAVE_LDAP_H */

/* Define to 1 if you have the <ldap_ssl.h> header file. */
/* #undef HAVE_LDAP_SSL_H */

/* Define to 1 if you have the <libpq-fe.h> header file. */
/* #undef HAVE_LIBPQ_FE_H */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <mysql.h> header file. */
/* #undef HAVE_MYSQL_H */

/* Define to 1 if you have the <mysql/mysql.h> header file. */
/* #undef HAVE_MYSQL_MYSQL_H */

/* Define to 1 if you have the <mysql/my_global.h> header file. */
/* #undef HAVE_MYSQL_MY_GLOBAL_H */

/* Define to 1 if you have the <mysql/my_sys.h> header file. */
/* #undef HAVE_MYSQL_MY_SYS_H */

/* Define to 1 if you have the <my_global.h> header file. */
/* #undef HAVE_MY_GLOBAL_H */

/* Define to 1 if you have the <my_sys.h> header file. */
/* #undef HAVE_MY_SYS_H */

/* Define to 1 if you have the `nl_langinfo' function. */
#define HAVE_NL_LANGINFO 1

/* Define to 1 if you have the <nss.h> header file. */
/* #undef HAVE_NSS_H */

/* Define to 1 if you have the <nss/nss.h> header file. */
/* #undef HAVE_NSS_NSS_H */

/* Define to 1 if you have the <nss/pk11pub.h> header file. */
/* #undef HAVE_NSS_PK11PUB_H */

/* Define to 1 if you have the <oci.h> header file. */
/* #undef HAVE_OCI_H */

/* Define to 1 if you have the <odbc/sql.h> header file. */
/* #undef HAVE_ODBC_SQL_H */

/* Define to 1 if you have the <openssl/x509.h> header file. */
#define HAVE_OPENSSL_X509_H 1

/* Define to 1 if you have the <pk11pub.h> header file. */
/* #undef HAVE_PK11PUB_H */

/* Define to 1 if you have the <postgresql/libpq-fe.h> header file. */
/* #undef HAVE_POSTGRESQL_LIBPQ_FE_H */

/* Define to 1 if you have the <prerror.h> header file. */
/* #undef HAVE_PRERROR_H */

/* Define to 1 if you have the <sqlite3.h> header file. */
/* #undef HAVE_SQLITE3_H */

/* Define to 1 if you have the <sqlite.h> header file. */
/* #undef HAVE_SQLITE_H */

/* Define to 1 if you have the <sql.h> header file. */
/* #undef HAVE_SQL_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sybdb.h> header file. */
/* #undef HAVE_SYBDB_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define if xmlparse/xmlparse.h is available */
/* #undef HAVE_XMLPARSE_XMLPARSE_H */

/* Define if xmltok/xmlparse.h is available */
/* #undef HAVE_XMLTOK_XMLPARSE_H */

/* Define if xml/xmlparse.h is available */
/* #undef HAVE_XML_XMLPARSE_H */

/* Define if ldap_set_rebind_proc takes three arguments */
/* #undef LDAP_SET_REBIND_PROC_THREE */

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1
