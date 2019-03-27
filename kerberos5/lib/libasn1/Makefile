# $FreeBSD$

LIB=	asn1
LDFLAGS=	-Wl,--no-undefined
INCS=	asn1_err.h asn1-common.h heim_asn1.h der.h der-protos.h der-private.h
LIBADD=	com_err roken
VERSION_MAP=	${.CURDIR}/version.map

SRCS=	asn1_err.c \
	asn1_err.h \
	der.c \
	der_copy.c \
	der_cmp.c \
	der_free.c \
	der_format.c \
	der_get.c \
	der_length.c \
	der_put.c \
	extra.c \
	template.c \
	timegm.c \
	${GEN:S/.x$/.c/:S/.hx$/.h/}

CFLAGS+=-I${KRB5DIR}/lib/asn1 -I${KRB5DIR}/lib/roken \
	-I${SRCTOP}/contrib/com_err -I.

GEN_RFC2459=	asn1_rfc2459_asn1.x rfc2459_asn1.hx rfc2459_asn1-priv.hx
GEN_CMS= 	asn1_cms_asn1.x cms_asn1.hx cms_asn1-priv.hx
GEN_K5=		asn1_krb5_asn1.x krb5_asn1.hx krb5_asn1-priv.hx
GEN_PKINIT= 	asn1_pkinit_asn1.x pkinit_asn1.hx pkinit_asn1-priv.hx
GEN_PKCS8=	asn1_pkcs8_asn1.x pkcs8_asn1.hx pkcs8_asn1-priv.hx
GEN_PKCS9=	asn1_pkcs9_asn1.x pkcs9_asn1.hx pkcs9_asn1-priv.hx
GEN_PKCS12=	asn1_pkcs12_asn1.x pkcs12_asn1.hx pkcs12_asn1-priv.hx
GEN_DIGEST=	asn1_digest_asn1.x digest_asn1.hx digest_asn1-priv.hx
GEN_KX509=	asn1_kx509_asn1.x kx509_asn1.hx kx509_asn1-priv.hx

GEN+=	${GEN_RFC2459}
GEN+=	${GEN_CMS}
GEN+=	${GEN_K5}
GEN+=	${GEN_PKINIT}
GEN+=	${GEN_PKCS8}
GEN+=	${GEN_PKCS9}
GEN+=	${GEN_PKCS12}
GEN+=	${GEN_DIGEST}
GEN+=	${GEN_KX509}

CLEANFILES= ${GEN} ${GEN:S/.x$/.c/:S/.hx$/.h/} *_asn1_files

INCS+=	krb5_asn1.h \
	pkinit_asn1.h \
	cms_asn1.h \
	rfc2459_asn1.h \
	pkcs8_asn1.h \
	pkcs9_asn1.h \
	pkcs12_asn1.h \
	digest_asn1.h \
	kx509_asn1.h

.ORDER: ${GEN_CMS}
${GEN_CMS:[2..-1]}: .NOMETA
${GEN_CMS}: cms.asn1 cms.opt
	${ASN1_COMPILE} --one-code-file \
		--option-file=${.ALLSRC:M*.opt} ${.ALLSRC:M*.asn1} cms_asn1

.ORDER: ${GEN_RFC2459}
${GEN_RFC2459:[2..-1]}: .NOMETA
${GEN_RFC2459}: rfc2459.asn1
	${ASN1_COMPILE} \
		--one-code-file \
		--preserve-binary=TBSCertificate \
		--preserve-binary=TBSCRLCertList \
		--preserve-binary=Name \
		--sequence=GeneralNames \
		--sequence=Extensions \
		--sequence=CRLDistributionPoints ${.ALLSRC:M*.asn1} rfc2459_asn1

.ORDER: ${GEN_K5}
${GEN_K5:[2..-1]}: .NOMETA
${GEN_K5}: krb5.asn1 krb5.opt
	${ASN1_COMPILE} \
		--one-code-file \
		--option-file=${.ALLSRC:M*.opt} \
		${.ALLSRC:M*.asn1} krb5_asn1

.ORDER: ${GEN_PKINIT}
${GEN_PKINIT:[2..-1]}: .NOMETA
${GEN_PKINIT}: pkinit.asn1
	${ASN1_COMPILE} --one-code-file \
		${.ALLSRC:M*.asn1} pkinit_asn1

.ORDER: ${GEN_PKCS8}
${GEN_PKCS8:[2..-1]}: .NOMETA
${GEN_PKCS8}: pkcs8.asn1
	${ASN1_COMPILE} --one-code-file \
		${.ALLSRC:M*.asn1} pkcs8_asn1

.ORDER: ${GEN_PKCS9}
${GEN_PKCS9:[2..-1]}: .NOMETA
${GEN_PKCS9}: pkcs9.asn1
	${ASN1_COMPILE} --one-code-file \
		${.ALLSRC:M*.asn1} pkcs9_asn1

.ORDER: ${GEN_PKCS12}
${GEN_PKCS12:[2..-1]}: .NOMETA
${GEN_PKCS12}: pkcs12.asn1
	${ASN1_COMPILE} --one-code-file \
		${.ALLSRC:M*.asn1} pkcs12_asn1

.ORDER: ${GEN_DIGEST}
${GEN_DIGEST:[2..-1]}: .NOMETA
${GEN_DIGEST}: digest.asn1
	${ASN1_COMPILE} --one-code-file \
		${.ALLSRC:M*.asn1} digest_asn1

.ORDER: ${GEN_KX509}
${GEN_KX509:[2..-1]}: .NOMETA
${GEN_KX509}: kx509.asn1
	${ASN1_COMPILE} --one-code-file \
		${.ALLSRC:M*.asn1} kx509_asn1

.SUFFIXES: .h .c .x .hx

.x.c:
	${CP} ${.IMPSRC} ${.TARGET}

.hx.h:
	${CP} ${.IMPSRC} ${.TARGET}

.include <bsd.lib.mk>

.SUFFIXES: .h .c .x .hx

.PATH: ${KRB5DIR}/lib/asn1
