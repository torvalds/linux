# $FreeBSD$

SCRIPTS=krb5-config
MAN=	krb5-config.1

CLEANFILES= krb5-config

krb5-config: krb5-config.in
	sed -e "s,@PACKAGE@,FreeBSD heimdal,g" \
	    -e "s,@VERSION@,1.1.0,g" \
	    -e "s,@prefix@,/usr,g" \
	    -e "s,@exec_prefix@,/usr,g" \
	    -e "s,@libdir@,${LIBDIR},g" \
	    -e "s,@includedir@,${INCLUDEDIR},g" \
	    -e "s,@LIB_crypt@,-lcrypt,g" \
	    -e "s,@LIB_dbopen@,,g" \
	    -e "s,@LIB_hcrypto_appl@,-lcrypto,g" \
	    -e "s,@LIB_pkinit@,-lhx509,g" \
	    -e "s,@LIB_dlopen@,,g" \
	    -e "s,@LIB_door_create@,,g" \
	    -e "s,@PTHREAD_LIBADD@,-pthread,g" \
	    -e "s,@LIBS@,,g" \
	    -e "s,@INCLUDE_hcrypto@,,g" \
	    ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/tools
